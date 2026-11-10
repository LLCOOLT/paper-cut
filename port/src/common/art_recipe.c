/* Art recipes: the runner (port/include/port/art_recipe.h; the format is
 * port/assets/luigi/art/RECIPES.md). Each step is the same arithmetic as
 * port/assets/luigi/art/tools/recipes.py, in the same order, in doubles
 * where that is in doubles (Python's floats): colorsys's HSV, the lightness
 * 0.299 R + 0.587 G + 0.114 B, rounding half to even. No fused multiply-adds
 * (the build's -ffp-contract=off), so every platform makes the same pixels. */
#include "port/art_recipe.h"
#include "icon_ids.h"

#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIDE 1024
#define MAX_TOKENS 160
#define NAME_LEN 32

/* ---------------------------------------------------------------- the disc */

#define DISC_FILES 16

struct PortArtDisc {
    char id[8];
    PortArtReadFn read;
    void* user;
    struct {
        char path[96];
        uint8_t* data;
        uint32_t size;
    } file[DISC_FILES];
    int count, next;
};

PortArtDisc* portArtDiscOpen(PortArtReadFn read, void* user, const char* gameId) {
    PortArtDisc* d = (PortArtDisc*)calloc(1, sizeof(PortArtDisc));
    if (d != NULL) {
        snprintf(d->id, sizeof(d->id), "%s", gameId != NULL ? gameId : "");
        d->read = read;
        d->user = user;
    }
    return d;
}

void portArtDiscClose(PortArtDisc* d) {
    int i;
    if (d == NULL) {
        return;
    }
    for (i = 0; i < d->count; i++) {
        free(d->file[i].data);
    }
    free(d);
}

/* a disc file, kept (the oldest goes when full); NULL if it isn't there */
static const uint8_t* discFile(PortArtDisc* d, const char* path, uint32_t* size) {
    int i;
    uint8_t* data;
    for (i = 0; i < d->count; i++) {
        if (strcmp(d->file[i].path, path) == 0) {
            *size = d->file[i].size;
            return d->file[i].data;
        }
    }
    *size = 0;
    data = d->read(d->user, path, size);
    if (data == NULL) {
        return NULL;
    }
    if (d->count < DISC_FILES) {
        i = d->count++;
    } else {
        i = d->next;
        d->next = (d->next + 1) % DISC_FILES;
        free(d->file[i].data);
    }
    snprintf(d->file[i].path, sizeof(d->file[i].path), "%s", path);
    d->file[i].data = data;
    d->file[i].size = *size;
    return data;
}

/* ------------------------------------------------------------- the runner */

typedef struct Img {
    int w, h;
    uint8_t* px;
} Img;

typedef struct Line {
    int num;
    int ntok;
    char** tok; /* NULL for a row */
    char* row;
} Line;

typedef struct Part {
    char mode; /* + - & */
    int kind;  /* SHAPE_* */
    int a[4];
    double* pts; /* poly: x, y pairs */
    int npts;
    int* spans; /* spans: y0, y1 per x (y0 < 0: none) */
    char** rows; /* rows */
    int nrows;
    const struct Part* sub; /* mask: the other mask's parts, as they were then */
    int nsub;
} Part;

enum { SHAPE_ALL, SHAPE_ALPHA, SHAPE_RECT, SHAPE_POLY, SHAPE_SPANS, SHAPE_ROWS, SHAPE_MASK, SHAPE_CIRCLE };

typedef struct Mask {
    char name[NAME_LEN];
    Part* parts;
    int nparts;
} Mask;

typedef struct Colour {
    char name[NAME_LEN];
    int c[4];
} Colour;

typedef struct Grad {
    char name[NAME_LEN];
    double t[32];
    int c[32][4];
    int n;
} Grad;

typedef struct Piece {
    char name[NAME_LEN];
    Img img;
} Piece;

typedef struct Clause {
    char key, op[3];
    double a, b;
    int range;
} Clause;

typedef struct Sel {
    Clause cl[64];
    int alt[64]; /* the alternative each clause belongs to */
    int n, nalt;
} Sel;

typedef struct Steps {
    char name[NAME_LEN];
    int from, to; /* its lines: ls[from] up to (not including) ls[to], its `end` */
} Steps;

typedef struct Run {
    jmp_buf fail;
    Line* ls; /* the file's lines */
    Steps* steps;
    int nsteps;
    int notHere; /* `discs` without this one */
    int drawnHere; /* `drawn` with this one: its fallback stands in */
    char* err;
    size_t errSize;
    int line;
    PortArtDisc* disc;
    void** allocs;
    int nallocs, capallocs;
    Colour* colours;
    int ncolours;
    Grad* grads;
    int ngrads;
    Mask* masks;
    int nmasks;
    Piece* pieces;
    int npieces;
    Img canvas;
} Run;

static void fail(Run* r, const char* fmt, ...) {
    va_list ap;
    int n = snprintf(r->err, r->errSize, "line %d: ", r->line);
    va_start(ap, fmt);
    if (n >= 0 && (size_t)n < r->errSize) {
        vsnprintf(r->err + n, r->errSize - (size_t)n, fmt, ap);
    }
    va_end(ap);
    longjmp(r->fail, 1);
}

/* memory for this run, freed when it ends */
static void* ralloc(Run* r, size_t n) {
    void* p;
    if (r->nallocs == r->capallocs) {
        int cap = r->capallocs ? r->capallocs * 2 : 64;
        void** q = (void**)realloc(r->allocs, sizeof(void*) * (size_t)cap);
        if (q == NULL) {
            fail(r, "out of memory");
        }
        r->allocs = q;
        r->capallocs = cap;
    }
    p = calloc(1, n ? n : 1);
    if (p == NULL) {
        fail(r, "out of memory");
    }
    r->allocs[r->nallocs++] = p;
    return p;
}

/* grows an array of this run (old contents copied) */
static void* rgrow(Run* r, void* old, size_t oldBytes, size_t newBytes) {
    void* p = ralloc(r, newBytes);
    if (old != NULL) {
        memcpy(p, old, oldBytes);
    }
    return p;
}

static Img newImg(Run* r, int w, int h) {
    Img m;
    m.w = w;
    m.h = h;
    m.px = (uint8_t*)ralloc(r, (size_t)w * h * 4);
    return m;
}

static uint8_t* at(Img* m, int x, int y) {
    return m->px + ((size_t)y * m->w + x) * 4;
}

/* ---- numbers ---- */

/* round half to even (Python's round) */
static double rnd(double x) {
    double f = floor(x), d = x - f;
    if (d > 0.5 || (d == 0.5 && fmod(f, 2.0) != 0.0)) {
        f += 1.0;
    }
    return f;
}

static int clampByte(double v) {
    return v < 0.0 ? 0 : v > 255.0 ? 255 : (int)v;
}

static double lum(const int* c) {
    return 0.299 * (double)c[0] + 0.587 * (double)c[1] + 0.114 * (double)c[2];
}

/* Python's float % (the result has the divisor's sign) */
static double pymod(double a, double b) {
    double m = fmod(a, b);
    if (m != 0.0 && ((b < 0.0) != (m < 0.0))) {
        m += b;
    }
    return m;
}

/* colorsys.rgb_to_hsv on c / 255 */
static void toHsv(const int* c, double* h, double* s, double* v) {
    double r = (double)c[0] / 255.0, g = (double)c[1] / 255.0, b = (double)c[2] / 255.0;
    double maxc = r, minc = r, rangec, rc, gc, bc, hh;
    if (g > maxc) maxc = g;
    if (b > maxc) maxc = b;
    if (g < minc) minc = g;
    if (b < minc) minc = b;
    rangec = maxc - minc;
    *v = maxc;
    if (minc == maxc) {
        *h = 0.0;
        *s = 0.0;
        return;
    }
    *s = rangec / maxc;
    rc = (maxc - r) / rangec;
    gc = (maxc - g) / rangec;
    bc = (maxc - b) / rangec;
    if (r == maxc) {
        hh = bc - gc;
    } else if (g == maxc) {
        hh = 2.0 + rc - bc;
    } else {
        hh = 4.0 + gc - rc;
    }
    *h = pymod(hh / 6.0, 1.0);
}

/* colorsys.hsv_to_rgb */
static void fromHsv(double h, double s, double v, double* out) {
    int i;
    double f, p, q, t;
    if (s == 0.0) {
        out[0] = out[1] = out[2] = v;
        return;
    }
    i = (int)(h * 6.0);
    f = (h * 6.0) - (double)i;
    p = v * (1.0 - s);
    q = v * (1.0 - s * f);
    t = v * (1.0 - s * (1.0 - f));
    switch (((i % 6) + 6) % 6) {
        case 0: out[0] = v, out[1] = t, out[2] = p; break;
        case 1: out[0] = q, out[1] = v, out[2] = p; break;
        case 2: out[0] = p, out[1] = v, out[2] = t; break;
        case 3: out[0] = p, out[1] = q, out[2] = v; break;
        case 4: out[0] = t, out[1] = p, out[2] = v; break;
        default: out[0] = v, out[1] = p, out[2] = q; break;
    }
}

/* -?digits(.digits)? */
static int isNum(const char* s) {
    if (*s == '-') {
        s++;
    }
    if (!isdigit((unsigned char)*s)) {
        return 0;
    }
    while (isdigit((unsigned char)*s)) {
        s++;
    }
    if (*s == '.') {
        s++;
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
        while (isdigit((unsigned char)*s)) {
            s++;
        }
    }
    return *s == 0;
}

static double num(Run* r, const char* s) {
    if (!isNum(s)) {
        fail(r, "%s: not a number", s);
    }
    return strtod(s, NULL);
}

static int integer(Run* r, const char* s) {
    const char* p = s;
    long v;
    if (p[0] == '0' && p[1] == 'x') {
        if (!isxdigit((unsigned char)p[2])) {
            fail(r, "%s: not a whole number", s);
        }
        for (p += 2; *p; p++) {
            if (!isxdigit((unsigned char)*p)) {
                fail(r, "%s: not a whole number", s);
            }
        }
        v = strtol(s + 2, NULL, 16);
    } else {
        if (*p == '-') {
            p++;
        }
        if (!*p) {
            fail(r, "%s: not a whole number", s);
        }
        for (; *p; p++) {
            if (!isdigit((unsigned char)*p)) {
                fail(r, "%s: not a whole number", s);
            }
        }
        v = strtol(s, NULL, 10);
    }
    if (v > 100000 || v < -100000) {
        fail(r, "%s: too big", s);
    }
    return (int)v;
}

/* 'A-B' or 'A' */
static void range(Run* r, const char* s, int* a, int* b) {
    char buf[32];
    char* dash;
    snprintf(buf, sizeof(buf), "%s", s);
    dash = strchr(buf, '-');
    if (dash != NULL && dash != buf) {
        *dash = 0;
        *a = integer(r, buf);
        *b = integer(r, dash + 1);
    } else {
        *a = *b = integer(r, buf);
    }
}

static void colour(Run* r, const char* s, int* out) {
    int i, n = (int)strlen(s);
    if (s[0] == '#' && (n == 7 || n == 9)) {
        for (i = 1; i < n; i++) {
            if (!isxdigit((unsigned char)s[i])) {
                break;
            }
        }
        if (i == n) {
            for (i = 0; i < (n - 1) / 2; i++) {
                char hex[3] = {s[1 + 2 * i], s[2 + 2 * i], 0};
                out[i] = (int)strtol(hex, NULL, 16);
            }
            if (n == 7) {
                out[3] = 255;
            }
            return;
        }
    }
    for (i = r->ncolours - 1; i >= 0; i--) {
        if (strcmp(r->colours[i].name, s) == 0) {
            memcpy(out, r->colours[i].c, sizeof(int) * 4);
            return;
        }
    }
    fail(r, "%s: not a colour (#rrggbb, #rrggbbaa or a name from `colours`)", s);
}

static void setColour(Run* r, const char* name, const int* c) {
    int i;
    for (i = 0; i < r->ncolours; i++) {
        if (strcmp(r->colours[i].name, name) == 0) {
            memcpy(r->colours[i].c, c, sizeof(int) * 4);
            return;
        }
    }
    if (strlen(name) >= NAME_LEN) {
        fail(r, "%s: too long a name", name);
    }
    r->colours = (Colour*)rgrow(r, r->colours, sizeof(Colour) * (size_t)r->ncolours,
                                sizeof(Colour) * (size_t)(r->ncolours + 1));
    snprintf(r->colours[r->ncolours].name, NAME_LEN, "%s", name);
    memcpy(r->colours[r->ncolours].c, c, sizeof(int) * 4);
    r->ncolours++;
}

static int findMask(Run* r, const char* name) {
    int i;
    for (i = 0; i < r->nmasks; i++) {
        if (strcmp(r->masks[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static Piece* findPiece(Run* r, const char* name) {
    int i;
    for (i = 0; i < r->npieces; i++) {
        if (strcmp(r->pieces[i].name, name) == 0) {
            return &r->pieces[i];
        }
    }
    return NULL;
}

/* ---- the lines ---- */

/* the text as lines of tokens; rows ('|' lines) kept whole */
static Line* splitLines(Run* r, const char* text, int* count) {
    size_t len = strlen(text);
    char* buf = (char*)ralloc(r, len + 1);
    Line* lines;
    int n = 0, cap = 64, num = 0;
    char* p = buf;
    memcpy(buf, text, len + 1);
    lines = (Line*)ralloc(r, sizeof(Line) * (size_t)cap);
    while (*p) {
        char* end = p;
        char* s;
        char* e;
        num++;
        while (*end && *end != '\n') {
            end++;
        }
        if (*end) {
            *end++ = 0;
        }
        s = p;
        p = end;
        while (*s && isspace((unsigned char)*s)) {
            s++;
        }
        e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1])) {
            *--e = 0;
        }
        if (n == cap) {
            lines = (Line*)rgrow(r, lines, sizeof(Line) * (size_t)cap, sizeof(Line) * (size_t)cap * 2);
            cap *= 2;
        }
        if (*s == '|') {
            s++;
            while (*s && isspace((unsigned char)*s)) {
                s++;
            }
            lines[n].num = num;
            lines[n].ntok = 0;
            lines[n].tok = NULL;
            lines[n].row = s;
            n++;
            continue;
        }
        {
            char* c;
            char* toks[MAX_TOKENS];
            int nt = 0;
            for (c = s; *c; c++) {
                if (*c == '#' && (c[1] == 0 || c[1] == ' ' || c[1] == '\t' || c[1] == '#')) {
                    *c = 0;
                    break;
                }
            }
            c = s;
            while (*c) {
                while (*c && isspace((unsigned char)*c)) {
                    c++;
                }
                if (!*c) {
                    break;
                }
                if (nt == MAX_TOKENS) {
                    r->line = num;
                    fail(r, "too long a line");
                }
                toks[nt++] = c;
                while (*c && !isspace((unsigned char)*c)) {
                    c++;
                }
                if (*c) {
                    *c++ = 0;
                }
            }
            if (nt == 0) {
                continue;
            }
            lines[n].num = num;
            lines[n].ntok = nt;
            lines[n].tok = (char**)ralloc(r, sizeof(char*) * (size_t)nt);
            memcpy(lines[n].tok, toks, sizeof(char*) * (size_t)nt);
            lines[n].row = NULL;
            n++;
        }
    }
    *count = n;
    return lines;
}

/* ---- masks ---- */

static void shapeRaster(Run* r, const Part* s, Img* img, uint8_t* out);

static uint8_t* rasterParts(Run* r, const Part* parts, int nparts, Img* img) {
    const int n = img->w * img->h;
    uint8_t* m = (uint8_t*)ralloc(r, (size_t)n);
    uint8_t* s = (uint8_t*)ralloc(r, (size_t)n);
    int k, i;
    for (k = 0; k < nparts; k++) {
        const Part* p = &parts[k];
        shapeRaster(r, p, img, s);
        for (i = 0; i < n; i++) {
            m[i] = p->mode == '+' ? (m[i] || s[i]) : p->mode == '-' ? (m[i] && !s[i]) : (m[i] && s[i]);
        }
    }
    return m;
}

static uint8_t* raster(Run* r, int mask, Img* img) {
    return rasterParts(r, r->masks[mask].parts, r->masks[mask].nparts, img);
}

static int inside(const double* pts, int n, double x, double y) {
    int i, hit = 0;
    for (i = 0; i < n; i++) {
        double x1 = pts[2 * i], y1 = pts[2 * i + 1];
        double x2 = pts[2 * ((i + 1) % n)], y2 = pts[2 * ((i + 1) % n) + 1];
        if ((y1 > y) != (y2 > y) && x < x1 + (y - y1) * (x2 - x1) / (y2 - y1)) {
            hit = !hit;
        }
    }
    return hit;
}

static void shapeRaster(Run* r, const Part* s, Img* img, uint8_t* out) {
    const int w = img->w, h = img->h;
    int x, y;
    memset(out, 0, (size_t)w * h);
    switch (s->kind) {
        case SHAPE_ALL: memset(out, 1, (size_t)w * h); break;
        case SHAPE_ALPHA:
            for (x = 0; x < w * h; x++) {
                out[x] = img->px[x * 4 + 3] >= 128;
            }
            break;
        case SHAPE_MASK: {
            uint8_t* m = rasterParts(r, s->sub, s->nsub, img);
            memcpy(out, m, (size_t)w * h);
            break;
        }
        case SHAPE_RECT:
            for (y = s->a[1] > 0 ? s->a[1] : 0; y <= (s->a[3] < h - 1 ? s->a[3] : h - 1); y++) {
                for (x = s->a[0] > 0 ? s->a[0] : 0; x <= (s->a[2] < w - 1 ? s->a[2] : w - 1); x++) {
                    out[y * w + x] = 1;
                }
            }
            break;
        case SHAPE_POLY:
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    out[y * w + x] = (uint8_t)inside(s->pts, s->npts, (double)x + 0.5, (double)y + 0.5);
                }
            }
            break;
        case SHAPE_CIRCLE:
            /* the texels whose centre is within R of (CX, CY) */
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    const double dx = (double)x + 0.5 - s->pts[0], dy = (double)y + 0.5 - s->pts[1];
                    out[y * w + x] = (uint8_t)(dx * dx + dy * dy <= s->pts[2] * s->pts[2]);
                }
            }
            break;
        case SHAPE_SPANS:
            for (x = 0; x < w && x < MAX_SIDE; x++) {
                if (s->spans[2 * x] < -99999) {
                    continue;
                }
                for (y = s->spans[2 * x] > 0 ? s->spans[2 * x] : 0; y <= s->spans[2 * x + 1] && y < h; y++) {
                    out[y * w + x] = 1;
                }
            }
            break;
        case SHAPE_ROWS: {
            int j;
            for (j = 0; j < s->nrows; j++) {
                const char* row = s->rows[j];
                int i;
                for (i = 0; row[i]; i++) {
                    x = s->a[0] + i;
                    y = s->a[1] + j;
                    if (row[i] == 'x' && x >= 0 && x < w && y >= 0 && y < h) {
                        out[y * w + x] = 1;
                    }
                }
            }
            break;
        }
    }
}

/* mask NAME [+|-|&] SHAPE ... */
static void maskLine(Run* r, char** a, int n, char** rows, int nrows) {
    char mode = '=';
    int m, k = 1;
    Part p;
    if (n < 2) {
        fail(r, "mask NAME [+ | - | &] SHAPE");
    }
    m = findMask(r, a[0]);
    if (strcmp(a[1], "+") == 0 || strcmp(a[1], "-") == 0 || strcmp(a[1], "&") == 0) {
        mode = a[1][0];
        k = 2;
        if (m < 0) {
            fail(r, "no mask %s to change", a[0]);
        }
    }
    if (k >= n) {
        fail(r, "a shape: all, rect, circle, poly, spans, rows, alpha or mask NAME");
    }
    memset(&p, 0, sizeof(p));
    p.mode = mode == '=' ? '+' : mode;
    {
        const char* kind = a[k];
        char** v = a + k + 1;
        int nv = n - k - 1, i;
        if (strcmp(kind, "all") == 0 && nv == 0) {
            p.kind = SHAPE_ALL;
        } else if (strcmp(kind, "alpha") == 0 && nv == 0) {
            p.kind = SHAPE_ALPHA;
        } else if (strcmp(kind, "rect") == 0 && nv == 4) {
            p.kind = SHAPE_RECT;
            for (i = 0; i < 4; i++) {
                p.a[i] = integer(r, v[i]);
            }
        } else if (strcmp(kind, "circle") == 0 && nv == 3) {
            p.kind = SHAPE_CIRCLE;
            p.npts = 1;
            p.pts = (double*)ralloc(r, sizeof(double) * 3);
            for (i = 0; i < 3; i++) {
                p.pts[i] = num(r, v[i]);
            }
        } else if (strcmp(kind, "poly") == 0 && nv >= 3) {
            p.kind = SHAPE_POLY;
            p.npts = nv;
            p.pts = (double*)ralloc(r, sizeof(double) * 2 * (size_t)nv);
            for (i = 0; i < nv; i++) {
                char buf[64];
                char* comma;
                snprintf(buf, sizeof(buf), "%s", v[i]);
                comma = strchr(buf, ',');
                if (comma == NULL) {
                    fail(r, "%s: a point is X,Y", v[i]);
                }
                *comma = 0;
                p.pts[2 * i] = num(r, buf);
                p.pts[2 * i + 1] = num(r, comma + 1);
            }
        } else if (strcmp(kind, "spans") == 0 && nv >= 1) {
            p.kind = SHAPE_SPANS;
            p.spans = (int*)ralloc(r, sizeof(int) * 2 * MAX_SIDE);
            for (i = 0; i < MAX_SIDE; i++) {
                p.spans[2 * i] = -100000;
            }
            for (i = 0; i < nv; i++) {
                char buf[64];
                char* colon;
                int x0, x1, y0, y1, x;
                snprintf(buf, sizeof(buf), "%s", v[i]);
                colon = strchr(buf, ':');
                if (colon == NULL) {
                    fail(r, "%s: a span is X:Y0-Y1 (or X0-X1:Y0-Y1)", v[i]);
                }
                *colon = 0;
                range(r, buf, &x0, &x1);
                range(r, colon + 1, &y0, &y1);
                for (x = x0; x <= x1; x++) {
                    if (x >= 0 && x < MAX_SIDE) {
                        p.spans[2 * x] = y0;
                        p.spans[2 * x + 1] = y1;
                    }
                }
            }
        } else if (strcmp(kind, "rows") == 0 && nv == 2) {
            int j;
            p.kind = SHAPE_ROWS;
            p.a[0] = integer(r, v[0]);
            p.a[1] = integer(r, v[1]);
            if (nrows == 0) {
                fail(r, "mask ... rows X Y: the rows ('|' lines, x in, . out) follow");
            }
            for (j = 0; j < nrows; j++) {
                const char* c;
                for (c = rows[j]; *c; c++) {
                    if (*c != 'x' && *c != '.') {
                        fail(r, "| %s: a mask's rows are x (in) and . (out)", rows[j]);
                    }
                }
            }
            p.rows = rows;
            p.nrows = nrows;
        } else if (strcmp(kind, "mask") == 0 && nv == 1) {
            /* the other mask as it is now (a later change to it doesn't reach this one) */
            const int o = findMask(r, v[0]);
            if (o < 0) {
                fail(r, "no mask %s", v[0]);
            }
            p.kind = SHAPE_MASK;
            p.sub = r->masks[o].parts;
            p.nsub = r->masks[o].nparts;
        } else {
            fail(r, "%s: not a shape", kind);
        }
    }
    if (mode == '=') {
        if (strlen(a[0]) >= NAME_LEN) {
            fail(r, "%s: too long a name", a[0]);
        }
        if (m < 0) {
            r->masks = (Mask*)rgrow(r, r->masks, sizeof(Mask) * (size_t)r->nmasks,
                                    sizeof(Mask) * (size_t)(r->nmasks + 1));
            m = r->nmasks++;
            snprintf(r->masks[m].name, NAME_LEN, "%s", a[0]);
        }
        r->masks[m].parts = NULL;
        r->masks[m].nparts = 0;
    }
    {
        /* a new array each time: masks made from this one keep the parts they had */
        Mask* mk = &r->masks[m];
        mk->parts = (Part*)rgrow(r, mk->parts, sizeof(Part) * (size_t)mk->nparts, sizeof(Part) * (size_t)(mk->nparts + 1));
        mk->parts[mk->nparts++] = p;
    }
}

/* ---- [in MASK] [where ...] [:] ---- */

static int parseClause(const char* t, Clause* c) {
    char buf[64];
    const char* p = t + 1;
    char* dots;
    if (strchr("hsvlrgba", t[0]) == NULL || t[0] == 0) {
        return 0;
    }
    memset(c, 0, sizeof(*c));
    c->key = t[0];
    if ((p[0] == '<' || p[0] == '>') && p[1] == '=') {
        c->op[0] = p[0];
        c->op[1] = '=';
        p += 2;
    } else if (p[0] == '<' || p[0] == '>' || p[0] == '=') {
        c->op[0] = p[0];
        p += 1;
    } else {
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", p);
    dots = strstr(buf, "..");
    if (dots != NULL) {
        *dots = 0;
        if (!isNum(buf) || !isNum(dots + 2)) {
            return 0;
        }
        c->a = strtod(buf, NULL);
        c->b = strtod(dots + 2, NULL);
        c->range = 1;
        return 2;
    }
    if (!isNum(buf)) {
        return 0;
    }
    c->a = strtod(buf, NULL);
    return 1;
}

typedef struct Select {
    uint8_t* mask; /* NULL: all */
    Sel* sel;      /* NULL: all */
} Select;

static int selectArgs(Run* r, char** a, int n, Img* img, Select* out) {
    int i = 0;
    out->mask = NULL;
    out->sel = NULL;
    while (i < n) {
        if (strcmp(a[i], "in") == 0 && i + 1 < n) {
            int m = findMask(r, a[i + 1]);
            if (m < 0) {
                fail(r, "no mask %s", a[i + 1]);
            }
            out->mask = raster(r, m, img);
            i += 2;
        } else if (strcmp(a[i], "where") == 0) {
            Sel* s = (Sel*)ralloc(r, sizeof(Sel));
            int cur = 0;
            i++;
            while (i < n) {
                Clause c;
                int k;
                if (strcmp(a[i], "or") == 0) {
                    if (cur == 0) {
                        fail(r, "`or` without a clause before it");
                    }
                    s->nalt++;
                    cur = 0;
                    i++;
                    continue;
                }
                k = parseClause(a[i], &c);
                if (k == 0) {
                    break;
                }
                if (k == 2 && (c.key != 'h' || c.op[0] != '=' || c.op[1] != 0)) {
                    fail(r, "%s: a range is h=A..B", a[i]);
                }
                if (s->n == 64) {
                    fail(r, "too many clauses");
                }
                s->cl[s->n] = c;
                s->alt[s->n] = s->nalt;
                s->n++;
                cur++;
                i++;
            }
            if (cur == 0) {
                fail(r, "`where` needs a clause");
            }
            s->nalt++;
            out->sel = s;
        } else {
            break;
        }
    }
    if (i < n && strcmp(a[i], ":") == 0) {
        i++;
    }
    return i;
}

static int selected(const Sel* s, const int* c) {
    double h, sat, v, vals[8];
    int alt, k;
    if (s == NULL) {
        return 1;
    }
    toHsv(c, &h, &sat, &v);
    vals[0] = h * 360.0;
    vals[1] = sat;
    vals[2] = v;
    vals[3] = lum(c);
    vals[4] = (double)c[0];
    vals[5] = (double)c[1];
    vals[6] = (double)c[2];
    vals[7] = (double)c[3];
    for (alt = 0; alt < s->nalt; alt++) {
        int ok = 1;
        for (k = 0; k < s->n && ok; k++) {
            const Clause* cl = &s->cl[k];
            double x;
            if (s->alt[k] != alt) {
                continue;
            }
            x = vals[strchr("hsvlrgba", cl->key) - "hsvlrgba"];
            if (cl->range) {
                ok = cl->a <= cl->b ? (cl->a <= x && x <= cl->b) : (x >= cl->a || x <= cl->b);
            } else if (cl->op[0] == '<') {
                ok = cl->op[1] ? x <= cl->a : x < cl->a;
            } else if (cl->op[0] == '>') {
                ok = cl->op[1] ? x >= cl->a : x > cl->a;
            } else {
                ok = x == cl->a;
            }
        }
        if (ok) {
            return 1;
        }
    }
    return 0;
}

/* the texels a step works on; skipClear: not the fully transparent */
static int picked(const Select* s, Img* img, int x, int y, int skipClear, int* c) {
    const uint8_t* p = at(img, x, y);
    int k;
    for (k = 0; k < 4; k++) {
        c[k] = p[k];
    }
    if (skipClear && c[3] == 0) {
        return 0;
    }
    if (s->mask != NULL && !s->mask[y * img->w + x]) {
        return 0;
    }
    return selected(s->sel, c);
}

static void store(Img* img, int x, int y, const int* c) {
    uint8_t* p = at(img, x, y);
    int k;
    for (k = 0; k < 4; k++) {
        p[k] = (uint8_t)c[k];
    }
}

/* ---- sources ---- */

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t be16(const uint8_t* p) {
    return (uint32_t)p[0] << 8 | p[1];
}

static void c565(uint32_t v, int* o) {
    int r = (int)(v >> 11) & 31, g = (int)(v >> 5) & 63, b = (int)v & 31;
    o[0] = r << 3 | r >> 2;
    o[1] = g << 2 | g >> 4;
    o[2] = b << 3 | b >> 2;
    o[3] = 255;
}

static void c5a3(uint32_t v, int* o) {
    if (v & 0x8000) {
        int r = (int)(v >> 10) & 31, g = (int)(v >> 5) & 31, b = (int)v & 31;
        o[0] = r << 3 | r >> 2;
        o[1] = g << 3 | g >> 2;
        o[2] = b << 3 | b >> 2;
        o[3] = 255;
    } else {
        int a = (int)(v >> 12) & 7;
        o[0] = (int)((v >> 8) & 15) * 17;
        o[1] = (int)((v >> 4) & 15) * 17;
        o[2] = (int)(v & 15) * 17;
        o[3] = a << 5 | a << 2 | a >> 1;
    }
}

static void cIa8(uint32_t v, int* o) {
    o[0] = o[1] = o[2] = (int)(v & 0xFF);
    o[3] = (int)(v >> 8);
}

/* image n of a TPL (as tools/port/tpl2png.py decodes) */
static Img tplImage(Run* r, const char* path, const uint8_t* d, uint32_t size, int n) {
    static const int kBlocks[15][3] = {{8, 8, 4}, {8, 4, 8}, {8, 4, 8}, {4, 4, 16}, {4, 4, 16}, {4, 4, 16}, {4, 4, 32},
                                       {0, 0, 0}, {8, 8, 4}, {8, 4, 8}, {4, 4, 16}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
                                       {8, 8, 4}};
    uint32_t count, table, imgOff, palOff, fmt, off, need;
    int w, h, bw, bh, bpp, bx, by, i, (*pal)[4] = NULL, npal = 0;
    Img m;
    uint32_t p;
    if (size < 12 || be32(d) != 0x0020AF30u) {
        fail(r, "%s: not a TPL", path);
    }
    count = be32(d + 4);
    table = be32(d + 8);
    if (n < 0 || (uint32_t)n >= count) {
        fail(r, "%s: no image %d (it has %u)", path, n, (unsigned)count);
    }
    if ((uint64_t)table + 8 * ((uint64_t)n + 1) > size) {
        fail(r, "%s: cut short", path);
    }
    imgOff = be32(d + table + 8 * n);
    palOff = be32(d + table + 8 * n + 4);
    if ((uint64_t)imgOff + 12 > size) {
        fail(r, "%s: cut short", path);
    }
    h = (int)be16(d + imgOff);
    w = (int)be16(d + imgOff + 2);
    fmt = be32(d + imgOff + 4);
    off = be32(d + imgOff + 8);
    if (fmt > 14 || kBlocks[fmt][0] == 0) {
        fail(r, "%s: image %d: format %u isn't supported", path, n, (unsigned)fmt);
    }
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) {
        fail(r, "%s: image %d is %dx%d", path, n, w, h);
    }
    bw = kBlocks[fmt][0];
    bh = kBlocks[fmt][1];
    bpp = kBlocks[fmt][2];
    need = (uint32_t)(((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bw * bh * bpp / 8);
    if ((uint64_t)off + need > size) {
        fail(r, "%s: cut short", path);
    }
    if (fmt == 8 || fmt == 9 || fmt == 10) {
        uint32_t pfmt, pdata;
        if (palOff == 0 || (uint64_t)palOff + 12 > size) {
            fail(r, "%s: image %d: no palette", path, n);
        }
        npal = (int)be16(d + palOff);
        pfmt = be32(d + palOff + 4);
        pdata = be32(d + palOff + 8);
        if (pfmt > 2 || (uint64_t)pdata + 2 * (uint64_t)npal > size) {
            fail(r, "%s: image %d: a bad palette", path, n);
        }
        pal = (int(*)[4])ralloc(r, sizeof(int) * 4 * (size_t)(npal ? npal : 1));
        for (i = 0; i < npal; i++) {
            uint32_t v = be16(d + pdata + 2 * i);
            if (pfmt == 0) {
                cIa8(v, pal[i]);
            } else if (pfmt == 1) {
                c565(v, pal[i]);
            } else {
                c5a3(v, pal[i]);
            }
        }
    }
    m = newImg(r, w, h);
    p = off;
    for (by = 0; by < (h + bh - 1) / bh; by++) {
        for (bx = 0; bx < (w + bw - 1) / bw; bx++) {
            const int x0 = bx * bw, y0 = by * bh;
            int c[4];
            if (fmt == 14) {
                int sb;
                for (sb = 0; sb < 4; sb++) {
                    uint32_t c0 = be16(d + p), c1 = be16(d + p + 2), bits = be32(d + p + 4);
                    int pc[4][4], k;
                    const int sx = x0 + (sb & 1) * 4, sy = y0 + (sb >> 1) * 4;
                    c565(c0, pc[0]);
                    c565(c1, pc[1]);
                    for (k = 0; k < 3; k++) {
                        if (c0 > c1) {
                            pc[2][k] = (2 * pc[0][k] + pc[1][k]) / 3;
                            pc[3][k] = (pc[0][k] + 2 * pc[1][k]) / 3;
                        } else {
                            pc[2][k] = (pc[0][k] + pc[1][k]) / 2;
                            pc[3][k] = 0;
                        }
                    }
                    pc[2][3] = 255;
                    pc[3][3] = c0 > c1 ? 255 : 0;
                    p += 8;
                    for (i = 0; i < 16; i++) {
                        const int x = sx + i % 4, y = sy + i / 4;
                        if (x < w && y < h) {
                            store(&m, x, y, pc[(bits >> (30 - 2 * i)) & 3]);
                        }
                    }
                }
                continue;
            }
            if (fmt == 6) {
                for (i = 0; i < 16; i++) {
                    const int x = x0 + i % 4, y = y0 + i / 4;
                    c[0] = d[p + 2 * i + 1];
                    c[1] = d[p + 32 + 2 * i];
                    c[2] = d[p + 33 + 2 * i];
                    c[3] = d[p + 2 * i];
                    if (x < w && y < h) {
                        store(&m, x, y, c);
                    }
                }
                p += 64;
                continue;
            }
            for (i = 0; i < bw * bh; i++) {
                const int x = x0 + i % bw, y = y0 + i / bw;
                if (bpp == 4) {
                    int v = (d[p + i / 2] >> (i % 2 == 0 ? 4 : 0)) & 15;
                    if (fmt == 0) {
                        c[0] = c[1] = c[2] = v * 17;
                        c[3] = 255;
                    } else {
                        if (v >= npal) {
                            fail(r, "%s: image %d: a texel past its palette", path, n);
                        }
                        memcpy(c, pal[v], sizeof(c));
                    }
                } else if (bpp == 8) {
                    int v = d[p + i];
                    if (fmt == 1) {
                        c[0] = c[1] = c[2] = v;
                        c[3] = 255;
                    } else if (fmt == 2) {
                        c[0] = c[1] = c[2] = (v & 15) * 17;
                        c[3] = (v >> 4) * 17;
                    } else {
                        if (v >= npal) {
                            fail(r, "%s: image %d: a texel past its palette", path, n);
                        }
                        memcpy(c, pal[v], sizeof(c));
                    }
                } else {
                    uint32_t v = be16(d + p + 2 * i);
                    if (fmt == 3) {
                        cIa8(v, c);
                    } else if (fmt == 4) {
                        c565(v, c);
                    } else if (fmt == 5) {
                        c5a3(v, c);
                    } else {
                        if ((int)(v & 0x3FFF) >= npal) {
                            fail(r, "%s: image %d: a texel past its palette", path, n);
                        }
                        memcpy(c, pal[v & 0x3FFF], sizeof(c));
                    }
                }
                if (x < w && y < h) {
                    store(&m, x, y, c);
                }
            }
            p += (uint32_t)(bw * bh * bpp / 8);
        }
    }
    return m;
}

/* ICON_xxx is named by the JP id; its value is this version's (icon_ids.h) */
#define ICON_ROW(p)                                                                                                  \
    ICON_##p##0, ICON_##p##1, ICON_##p##2, ICON_##p##3, ICON_##p##4, ICON_##p##5, ICON_##p##6, ICON_##p##7,           \
        ICON_##p##8, ICON_##p##9, ICON_##p##A, ICON_##p##B, ICON_##p##C, ICON_##p##D, ICON_##p##E, ICON_##p##F,
static const unsigned short kIcons[] = {
    ICON_ROW(00) ICON_ROW(01) ICON_ROW(02) ICON_ROW(03) ICON_ROW(04) ICON_ROW(05) ICON_ROW(06) ICON_ROW(07)
    ICON_ROW(08) ICON_ROW(09) ICON_ROW(0A) ICON_ROW(0B) ICON_ROW(0C) ICON_ROW(0D) ICON_ROW(0E) ICON_ROW(0F)
    ICON_ROW(10) ICON_ROW(11) ICON_ROW(12) ICON_ROW(13) ICON_ROW(14) ICON_ROW(15) ICON_ROW(16) ICON_ROW(17)
    ICON_ROW(18) ICON_ROW(19) ICON_ROW(1A) ICON_ROW(1B) ICON_ROW(1C) ICON_ROW(1D) ICON_ROW(1E) ICON_ROW(1F)
    ICON_ROW(20)
    ICON_210, ICON_211, ICON_212, ICON_213, ICON_214, ICON_215, ICON_216, ICON_217,
};

static const uint8_t* need(Run* r, const char* path, uint32_t* size) {
    const uint8_t* d = discFile(r->disc, path, size);
    const char* us = strstr(path, "/us/");
    if (d == NULL && us != NULL) { /* the Japanese disc's folder for the language */
        char jp[96];
        snprintf(jp, sizeof(jp), "%.*s/jp/%s", (int)(us - path), path, us + 4);
        d = discFile(r->disc, jp, size);
    }
    if (d == NULL) {
        fail(r, "%s isn't on this disc", path);
    }
    return d;
}

/* tpl PATH N | model NAME N | icon ICON_xxx [frame N]: the image; *used set to the tokens read */
static Img loadSource(Run* r, char** a, int n, int* used) {
    char path[96];
    const uint8_t* d;
    uint32_t size;
    int index;
    if (n >= 3 && strcmp(a[0], "tpl") == 0) {
        snprintf(path, sizeof(path), "%s", a[1]);
        index = integer(r, a[2]);
        *used = 3;
    } else if (n >= 3 && strcmp(a[0], "model") == 0) {
        snprintf(path, sizeof(path), "a/%s-", a[1]);
        index = integer(r, a[2]);
        *used = 3;
    } else if (n >= 2 && strcmp(a[0], "icon") == 0) {
        int id, frame = 0, count, nframes;
        uint32_t off;
        const uint8_t* bin;
        uint32_t binSize;
        *used = 2;
        if (n >= 4 && strcmp(a[2], "frame") == 0) {
            frame = integer(r, a[3]);
            *used = 4;
        }
        if (strncmp(a[1], "0x", 2) == 0) {
            id = integer(r, a[1]);
        } else {
            char* end;
            long jp;
            if (strncmp(a[1], "ICON_", 5) != 0 || !isxdigit((unsigned char)a[1][5])) {
                fail(r, "%s: no such icon", a[1]);
            }
            jp = strtol(a[1] + 5, &end, 16);
            /* named as icon_ids.h names them: three upper-case hex digits */
            if (*end || strlen(a[1]) != 8 || jp < 0 || jp >= (long)(sizeof(kIcons) / sizeof(kIcons[0]))) {
                fail(r, "%s: no such icon", a[1]);
            }
            {
                const char* c;
                for (c = a[1] + 5; *c; c++) {
                    if (islower((unsigned char)*c)) {
                        fail(r, "%s: no such icon", a[1]);
                    }
                }
            }
            id = kIcons[jp];
        }
        bin = need(r, "icon.bin", &binSize);
        count = binSize >= 2 ? (int)be16(bin) : 0;
        if (id < 0 || id >= count || (uint32_t)(2 + 2 * id + 2) > binSize) {
            fail(r, "%s: icon %#x isn't on this disc", a[1], id);
        }
        off = be16(bin + 2 + 2 * id);
        if (off + 8 > binSize) {
            fail(r, "icon.bin: cut short");
        }
        nframes = (int)be16(bin + off + 6);
        if (frame < 0 || frame >= nframes || off + 8 + 4 * (uint32_t)frame + 2 > binSize) {
            fail(r, "%s: no frame %d", a[1], frame);
        }
        index = (int)be16(bin + off + 8 + 4 * frame);
        snprintf(path, sizeof(path), "icon.tpl");
    } else {
        fail(r, "a source is tpl PATH N, model NAME N or icon ICON_xxx");
        return newImg(r, 1, 1);
    }
    d = need(r, path, &size);
    return tplImage(r, path, d, size, index);
}

static Img blank(Run* r, char** a, int n) {
    Img m;
    int w, h, x, c[4] = {0, 0, 0, 0};
    if (n != 2 && n != 3) {
        fail(r, "canvas W H [COLOUR]");
    }
    w = integer(r, a[0]);
    h = integer(r, a[1]);
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) {
        fail(r, "a canvas is 1 to 1024 texels a side");
    }
    if (n == 3) {
        colour(r, a[2], c);
    }
    m = newImg(r, w, h);
    for (x = 0; x < w * h; x++) {
        int k;
        for (k = 0; k < 4; k++) {
            m.px[x * 4 + k] = (uint8_t)c[k];
        }
    }
    return m;
}

static Img copyImg(Run* r, const Img* s) {
    Img m = newImg(r, s->w, s->h);
    memcpy(m.px, s->px, (size_t)s->w * s->h * 4);
    return m;
}

/* ---- the steps ---- */

static Grad* findGrad(Run* r, const char* name) {
    int i;
    for (i = r->ngrads - 1; i >= 0; i--) {
        if (strcmp(r->grads[i].name, name) == 0) {
            return &r->grads[i];
        }
    }
    return NULL;
}

static void stepRamp(Run* r, Img* img, char** a, int n) {
    Select s;
    int i = selectArgs(r, a, n, img, &s), x, y, c[4], base[4];
    double b;
    Grad* g;
    if (n - i != 4 || strcmp(a[i], "base") != 0 || strcmp(a[i + 2], "to") != 0) {
        fail(r, "ramp [in MASK] [where ...] base COLOUR to GRADIENT");
    }
    colour(r, a[i + 1], base);
    b = lum(base);
    g = findGrad(r, a[i + 3]);
    if (g == NULL) {
        fail(r, "no gradient %s", a[i + 3]);
    }
    if (b <= 0.0) {
        fail(r, "the base colour is black");
    }
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            double t;
            int k, j, done = 0;
            if (!picked(&s, img, x, y, 1, c)) {
                continue;
            }
            t = lum(c) / b;
            for (j = 0; j + 1 < g->n && !done; j++) {
                if (t <= g->t[j + 1]) {
                    double f = (t - g->t[j]) / (g->t[j + 1] - g->t[j]);
                    if (f < 0.0) {
                        f = 0.0;
                    }
                    for (k = 0; k < 3; k++) {
                        c[k] = clampByte(rnd((double)g->c[j][k] * (1.0 - f) + (double)g->c[j + 1][k] * f));
                    }
                    done = 1;
                }
            }
            if (!done) {
                for (k = 0; k < 3; k++) {
                    c[k] = g->c[g->n - 1][k];
                }
            }
            store(img, x, y, c);
        }
    }
}

static void stepMap(Run* r, Img* img, char** a, int n) {
    Select s;
    int i = selectArgs(r, a, n, img, &s), np, x, y, c[4], k;
    int(*src)[4];
    int(*dst)[4];
    if (n - i == 0 || (n - i) % 3 != 0) {
        fail(r, "map [in MASK] [where ...] FROM -> TO ...");
    }
    np = (n - i) / 3;
    src = (int(*)[4])ralloc(r, sizeof(int) * 4 * (size_t)np);
    dst = (int(*)[4])ralloc(r, sizeof(int) * 4 * (size_t)np);
    for (k = 0; k < np; k++) {
        if (strcmp(a[i + 3 * k + 1], "->") != 0) {
            fail(r, "map [in MASK] [where ...] FROM -> TO ...");
        }
        colour(r, a[i + 3 * k], src[k]);
        colour(r, a[i + 3 * k + 2], dst[k]);
    }
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            long best = -1;
            int pick = 0, j;
            double ls, m;
            if (!picked(&s, img, x, y, 1, c)) {
                continue;
            }
            for (k = 0; k < np; k++) {
                long dr = src[k][0] - c[0], dg = src[k][1] - c[1], db = src[k][2] - c[2];
                long dd = dr * dr + dg * dg + db * db;
                if (best < 0 || dd < best) {
                    best = dd;
                    pick = k;
                }
            }
            ls = lum(src[pick]);
            m = ls > 0.0 ? lum(c) / ls : 1.0;
            for (j = 0; j < 3; j++) {
                c[j] = clampByte(rnd((double)dst[pick][j] * m));
            }
            store(img, x, y, c);
        }
    }
}

static void stepHsv(Run* r, Img* img, char** a, int n) {
    Select s;
    int i = selectArgs(r, a, n, img, &s), x, y, c[4];
    char hueOp = 0, satOp = 0, valOp = 0;
    double hueV = 0, satV = 0, valV = 0;
    for (; i < n; i += 2) {
        char op;
        double v;
        if (i + 1 >= n || (strcmp(a[i], "hue") != 0 && strcmp(a[i], "sat") != 0 && strcmp(a[i], "val") != 0)) {
            fail(r, "hsv [in MASK] [where ...] [hue =D|+D|-D] [sat =K|*K] [val =K|*K|+K|-K]");
        }
        op = a[i + 1][0];
        if (strchr("=*+-", op) == NULL || op == 0 || a[i + 1][1] == 0) {
            fail(r, "%s %s: =N, *N, +N or -N", a[i], a[i + 1]);
        }
        v = num(r, a[i + 1] + 1);
        if (a[i][0] == 'h') {
            if (op == '*') {
                fail(r, "hue =D, +D or -D (degrees)");
            }
            hueOp = op, hueV = v;
        } else if (a[i][0] == 's') {
            if (op == '+' || op == '-') {
                fail(r, "sat =K or *K");
            }
            satOp = op, satV = v;
        } else {
            valOp = op, valV = v;
        }
    }
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            double h, sa, v, out[3];
            int k;
            if (!picked(&s, img, x, y, 1, c)) {
                continue;
            }
            toHsv(c, &h, &sa, &v);
            if (hueOp) {
                h = hueOp == '=' ? hueV / 360.0 : pymod(h + (hueOp == '+' ? hueV : -hueV) / 360.0, 1.0);
            }
            if (satOp) {
                sa = satOp == '=' ? satV : sa * satV;
                sa = sa < 0.0 ? 0.0 : sa > 1.0 ? 1.0 : sa;
            }
            if (valOp) {
                v = valOp == '=' ? valV : valOp == '*' ? v * valV : valOp == '+' ? v + valV : v - valV;
                v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
            }
            fromHsv(h, sa, v, out);
            for (k = 0; k < 3; k++) {
                c[k] = clampByte(rnd(out[k] * 255.0));
            }
            store(img, x, y, c);
        }
    }
}

static void stepFill(Run* r, Img* img, char** a, int n) {
    Select s;
    int i = selectArgs(r, a, n, img, &s), x, y, c[4], col[4];
    if (n - i != 1) {
        fail(r, "fill [in MASK] [where ...] COLOUR");
    }
    colour(r, a[i], col);
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            if (picked(&s, img, x, y, 0, c)) {
                store(img, x, y, col);
            }
        }
    }
}

static void stepPaint(Run* r, Img* img, char** a, int n, char** rows, int nrows) {
    int x0, y0, i, j;
    if (n != 2 || nrows == 0) {
        fail(r, "paint X Y, then the rows ('|' lines: a colour name a texel, . keeps, _ clears)");
    }
    x0 = integer(r, a[0]);
    y0 = integer(r, a[1]);
    for (j = 0; j < nrows; j++) {
        for (i = 0; rows[j][i]; i++) {
            const char ch = rows[j][i];
            int c[4] = {0, 0, 0, 0};
            const int x = x0 + i, y = y0 + j;
            if (ch == '.') {
                continue;
            }
            if (x < 0 || x >= img->w || y < 0 || y >= img->h) {
                fail(r, "paint: texel %d,%d is outside the image", x, y);
            }
            if (ch != '_') {
                char name[2] = {ch, 0};
                colour(r, name, c);
            }
            store(img, x, y, c);
        }
    }
}

static void stepStamp(Run* r, Img* img, char** a, int n) {
    static const char* kTone[4] = {"edge", "top", "bottom", "fill"};
    int tone[4][4], set[4] = {0, 0, 0, 0}, m, x, y, k;
    uint8_t* mk;
    const int w = img->w, h = img->h;
    if (n < 1 || (m = findMask(r, a[0])) < 0) {
        fail(r, "stamp MASK COLOUR | stamp MASK [edge C] [top C] [bottom C] [fill C]");
    }
    if (n == 2) {
        colour(r, a[1], tone[0]);
        for (k = 1; k < 4; k++) {
            memcpy(tone[k], tone[0], sizeof(tone[0]));
        }
    } else {
        int i;
        if (n < 3 || (n - 1) % 2) {
            fail(r, "stamp MASK COLOUR | stamp MASK [edge C] [top C] [bottom C] [fill C]");
        }
        for (i = 1; i < n; i += 2) {
            for (k = 0; k < 4 && strcmp(a[i], kTone[k]) != 0; k++) {
            }
            if (k == 4) {
                fail(r, "%s: edge, top, bottom or fill", a[i]);
            }
            colour(r, a[i + 1], tone[k]);
            set[k] = 1;
        }
        if (!set[3]) {
            fail(r, "stamp: a fill colour");
        }
        for (k = 0; k < 3; k++) {
            if (!set[k]) {
                memcpy(tone[k], tone[3], sizeof(tone[0]));
            }
        }
    }
    mk = raster(r, m, img);
#define ON(X, Y) ((X) >= 0 && (X) < w && (Y) >= 0 && (Y) < h && mk[(Y) * w + (X)])
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (!mk[y * w + x]) {
                continue;
            }
            if (!(ON(x - 1, y) && ON(x + 1, y) && ON(x, y - 1) && ON(x, y + 1))) {
                k = 0;
            } else if (!ON(x, y - 2)) {
                k = 1;
            } else if (!ON(x, y + 2)) {
                k = 2;
            } else {
                k = 3;
            }
            store(img, x, y, tone[k]);
        }
    }
#undef ON
}

static void stepSlide(Run* r, Img* img, char** a, int n) {
    int dir, at0, along, across, i;
    uint8_t* line;
    if (n < 3 || (strcmp(a[0], "down") != 0 && strcmp(a[0], "up") != 0 && strcmp(a[0], "right") != 0 &&
                  strcmp(a[0], "left") != 0)) {
        fail(r, "slide down|up|right|left AT FROM-TO:BY ...");
    }
    dir = a[0][0];
    at0 = integer(r, a[1]);
    along = dir == 'd' || dir == 'u' ? img->h : img->w;
    across = dir == 'd' || dir == 'u' ? img->w : img->h;
    if (!(0 < at0 && at0 < along - 1)) {
        fail(r, "slide: %d isn't inside the image", at0);
    }
    line = (uint8_t*)ralloc(r, (size_t)along * 4);
    for (i = 2; i < n; i++) {
        char buf[64];
        char* colon;
        int a0, a1, by, ac, t;
        snprintf(buf, sizeof(buf), "%s", a[i]);
        colon = strchr(buf, ':');
        if (colon == NULL) {
            fail(r, "%s: FROM-TO:BY", a[i]);
        }
        *colon = 0;
        range(r, buf, &a0, &a1);
        by = integer(r, colon + 1);
        if (by < 0 || by >= along) {
            fail(r, "%s: BY is 0 to the image's size", a[i]);
        }
        for (ac = a0; ac <= a1; ac++) {
            if (ac < 0 || ac >= across) {
                fail(r, "slide: %d is outside the image", ac);
            }
#define TEXEL(T) (dir == 'd' || dir == 'u' ? at(img, ac, (T)) : at(img, (T), ac))
            for (t = 0; t < along; t++) {
                memcpy(line + t * 4, TEXEL(t), 4);
            }
            for (t = 0; t < along; t++) {
                if (dir == 'd' || dir == 'r') {
                    if (t >= at0 + by) {
                        memcpy(TEXEL(t), line + (t - by) * 4, 4);
                    } else if (t >= at0) {
                        memcpy(TEXEL(t), line + (at0 - 1) * 4, 4);
                    }
                } else {
                    if (t <= at0 - by) {
                        memcpy(TEXEL(t), line + (t + by) * 4, 4);
                    } else if (t <= at0) {
                        memcpy(TEXEL(t), line + (at0 + 1) * 4, 4);
                    }
                }
            }
#undef TEXEL
        }
    }
}

static Img stepCrop(Run* r, Img* img, char** a, int n) {
    int x0, y0, w, h, x, y;
    Img out;
    if (n != 4) {
        fail(r, "crop X Y W H");
    }
    x0 = integer(r, a[0]);
    y0 = integer(r, a[1]);
    w = integer(r, a[2]);
    h = integer(r, a[3]);
    if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) {
        fail(r, "crop: 1 to 1024 texels a side");
    }
    out = newImg(r, w, h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (x0 + x >= 0 && x0 + x < img->w && y0 + y >= 0 && y0 + y < img->h) {
                memcpy(at(&out, x, y), at(img, x0 + x, y0 + y), 4);
            }
        }
    }
    return out;
}

static Img stepFlip(Run* r, Img* img, char** a, int n) {
    Img out;
    int x, y;
    if (n != 1 || (strcmp(a[0], "x") != 0 && strcmp(a[0], "y") != 0)) {
        fail(r, "flip x | flip y");
    }
    out = newImg(r, img->w, img->h);
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            memcpy(at(&out, x, y), a[0][0] == 'x' ? at(img, img->w - 1 - x, y) : at(img, x, img->h - 1 - y), 4);
        }
    }
    return out;
}

/* a / 8192, rounded down */
static int floor8192(int a) {
    return a >= 0 ? a / 8192 : -((-a + 8191) / 8192);
}

/* rotate D: turned D degrees clockwise about the middle, each texel the source texel under its centre
 * (whole-number sums on 1/4096ths, so both runners agree), the size kept */
static Img stepRotate(Run* r, Img* img, char** a, int n) {
    Img out;
    int x, y, c, s;
    double d;
    if (n != 1) {
        fail(r, "rotate D");
    }
    d = integer(r, a[0]) * 3.14159265358979323846 / 180.0;
    c = (int)floor(cos(d) * 4096.0 + 0.5);
    s = (int)floor(sin(d) * 4096.0 + 0.5);
    out = newImg(r, img->w, img->h);
    for (y = 0; y < img->h; y++) {
        const int dy = 2 * y + 1 - img->h;
        for (x = 0; x < img->w; x++) {
            const int dx = 2 * x + 1 - img->w;
            const int sx = floor8192(c * dx + s * dy + img->w * 4096);
            const int sy = floor8192(-s * dx + c * dy + img->h * 4096);
            if (sx >= 0 && sx < img->w && sy >= 0 && sy < img->h) {
                memcpy(at(&out, x, y), at(img, sx, sy), 4);
            }
        }
    }
    return out;
}

/* grow R COLOUR: a border round the opaque shape, R texels wide (a round brush: dx^2 + dy^2 <=
 * R^2 + R), in the colour; only clear texels change. A sticker's white edge: `grow 2 #e7e7e7` */
static Img stepGrow(Run* r, Img* img, char** a, int n) {
    Img out;
    int x, y, dx, dy, rad, col[4];
    if (n != 2) {
        fail(r, "grow R COLOUR");
    }
    rad = integer(r, a[0]);
    if (rad < 1 || rad > 16) {
        fail(r, "grow: R is 1 to 16");
    }
    colour(r, a[1], col);
    out = copyImg(r, img);
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            int hit = 0;
            if (at(img, x, y)[3] >= 128) {
                continue;
            }
            for (dy = -rad; dy <= rad && !hit; dy++) {
                for (dx = -rad; dx <= rad && !hit; dx++) {
                    const int sx = x + dx, sy = y + dy;
                    if (dx * dx + dy * dy <= rad * rad + rad && sx >= 0 && sy >= 0 && sx < img->w &&
                        sy < img->h && at(img, sx, sy)[3] >= 128) {
                        hit = 1;
                    }
                }
            }
            if (hit) {
                store(&out, x, y, col);
            }
        }
    }
    return out;
}

static Img stepScale(Run* r, Img* img, char** a, int n) {
    int dw, dh, x, y, nearest, smooth;
    const int sw = img->w, sh = img->h;
    Img out;
    if (n != 3 || (strcmp(a[2], "nearest") != 0 && strcmp(a[2], "texel") != 0 && strcmp(a[2], "smooth") != 0)) {
        fail(r, "scale W H nearest|texel|smooth");
    }
    dw = integer(r, a[0]);
    dh = integer(r, a[1]);
    if (dw <= 0 || dh <= 0 || dw > MAX_SIDE || dh > MAX_SIDE) {
        fail(r, "scale: 1 to 1024 texels a side");
    }
    nearest = a[2][0] == 'n';
    smooth = a[2][0] == 's';
    out = newImg(r, dw, dh);
    for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
            long long tw = 0, ta = 0, tc[3] = {0, 0, 0};
            int i, j, k, j1, i1;
            if (nearest) {
                memcpy(at(&out, x, y), at(img, (2 * x + 1) * sw / (2 * dw), (2 * y + 1) * sh / (2 * dh)), 4);
                continue;
            }
            if (smooth) {
                /* bilinear between the four source texels round the texel's centre, by alpha (premultiplied),
                 * in exact integers: positions in 1/(2 dw), 1/(2 dh) of a source texel; in where half covered */
                const long long denX = 2LL * dw, denY = 2LL * dh;
                long long nx = (2LL * x + 1) * sw - dw, ny = (2LL * y + 1) * sh - dh, fx, fy;
                int x0, y0, xs[2], ys[2];
                long long wxs[2], wys[2];
                nx = nx < 0 ? 0 : nx;
                ny = ny < 0 ? 0 : ny;
                x0 = (int)(nx / denX);
                y0 = (int)(ny / denY);
                fx = nx - x0 * denX;
                fy = ny - y0 * denY;
                if (x0 >= sw - 1) {
                    x0 = sw - 1;
                    fx = 0;
                }
                if (y0 >= sh - 1) {
                    y0 = sh - 1;
                    fy = 0;
                }
                xs[0] = x0;
                xs[1] = x0 + 1 < sw ? x0 + 1 : sw - 1;
                ys[0] = y0;
                ys[1] = y0 + 1 < sh ? y0 + 1 : sh - 1;
                wxs[0] = denX - fx;
                wxs[1] = fx;
                wys[0] = denY - fy;
                wys[1] = fy;
                tw = denX * denY;
                for (j = 0; j < 2; j++) {
                    for (i = 0; i < 2; i++) {
                        const uint8_t* c = at(img, xs[i], ys[j]);
                        const long long wgt = wxs[i] * wys[j];
                        ta += wgt * c[3];
                        for (k = 0; k < 3; k++) {
                            tc[k] += wgt * c[3] * c[k];
                        }
                    }
                }
                if (ta == 0 || 2 * ta < 255 * tw) {
                    continue;
                }
                {
                    uint8_t* o = at(&out, x, y);
                    for (k = 0; k < 3; k++) {
                        o[k] = (uint8_t)((tc[k] + ta / 2) / ta);
                    }
                    o[3] = 255;
                }
                continue;
            }
            j1 = ((y + 1) * sh + dh - 1) / dh;
            i1 = ((x + 1) * sw + dw - 1) / dw;
            for (j = y * sh / dh; j < (sh < j1 ? sh : j1); j++) {
                const int hiY = (j + 1) * dh < (y + 1) * sh ? (j + 1) * dh : (y + 1) * sh;
                const int loY = j * dh > y * sh ? j * dh : y * sh;
                const int wy = hiY - loY;
                if (wy <= 0) {
                    continue;
                }
                for (i = x * sw / dw; i < (sw < i1 ? sw : i1); i++) {
                    const int hiX = (i + 1) * dw < (x + 1) * sw ? (i + 1) * dw : (x + 1) * sw;
                    const int loX = i * dw > x * sw ? i * dw : x * sw;
                    const int wx = hiX - loX;
                    const uint8_t* c = at(img, i, j);
                    long long wgt;
                    if (wx <= 0) {
                        continue;
                    }
                    wgt = (long long)wx * wy;
                    tw += wgt;
                    ta += wgt * c[3];
                    for (k = 0; k < 3; k++) {
                        tc[k] += wgt * c[3] * c[k];
                    }
                }
            }
            if (ta == 0 || 2 * ta < 255 * tw) {
                continue;
            }
            {
                uint8_t* o = at(&out, x, y);
                for (k = 0; k < 3; k++) {
                    o[k] = (uint8_t)((tc[k] + ta / 2) / ta);
                }
                o[3] = 255;
            }
        }
    }
    return out;
}

static void stepPaste(Run* r, Img* img, char** a, int n) {
    Piece* p;
    int x0, y0, x, y, copy;
    if ((n != 3 && n != 4) || (n == 4 && strcmp(a[3], "over") != 0 && strcmp(a[3], "copy") != 0)) {
        fail(r, "paste PIECE X Y [over|copy]");
    }
    p = findPiece(r, a[0]);
    if (p == NULL) {
        fail(r, "no piece %s", a[0]);
    }
    x0 = integer(r, a[1]);
    y0 = integer(r, a[2]);
    copy = n == 4 && strcmp(a[3], "copy") == 0;
    for (y = 0; y < p->img.h; y++) {
        for (x = 0; x < p->img.w; x++) {
            const int tx = x0 + x, ty = y0 + y;
            const uint8_t* s;
            uint8_t* d;
            if (tx < 0 || tx >= img->w || ty < 0 || ty >= img->h) {
                continue;
            }
            s = at(&p->img, x, y);
            d = at(img, tx, ty);
            if (copy || s[3] == 255) {
                memcpy(d, s, 4);
            } else if (s[3]) {
                const int al = s[3];
                int k;
                for (k = 0; k < 3; k++) {
                    d[k] = (uint8_t)((s[k] * al + d[k] * (255 - al) + 127) / 255);
                }
                d[3] = (uint8_t)(al + (d[3] * (255 - al) + 127) / 255);
            }
        }
    }
}

static void stepAlpha(Run* r, Img* img, char** a, int n) {
    int x, y, c[4];
    if (n == 2 && strcmp(a[0], "cut") == 0) {
        const int t = integer(r, a[1]);
        for (x = 0; x < img->w * img->h; x++) {
            img->px[x * 4 + 3] = img->px[x * 4 + 3] >= t ? 255 : 0;
        }
        return;
    }
    if (n >= 2 && strcmp(a[0], "set") == 0) {
        Select s;
        const int al = integer(r, a[1]);
        const int i = selectArgs(r, a + 2, n - 2, img, &s);
        if (i != n - 2 || al < 0 || al > 255) {
            fail(r, "alpha set A [in MASK] [where ...]");
        }
        for (y = 0; y < img->h; y++) {
            for (x = 0; x < img->w; x++) {
                if (picked(&s, img, x, y, 0, c)) {
                    at(img, x, y)[3] = (uint8_t)al;
                }
            }
        }
        return;
    }
    fail(r, "alpha cut T | alpha set A [in MASK] [where ...]");
}

/* ---- map meshes (recipes.py's map_mesh, mesh_tris and draw_mesh, line for line) ----
 * `mesh`: an object of a map drawn into the image. The map's `d` file is read as the game reads
 * it (packed display lists; ver1.02's fixed-point positions and texture coordinates), each joint
 * under the named one placed by its scale and translation (a turned joint is refused). */

typedef struct MeshTri {
    int part, pole, tex; /* tex: its texture's index in the map's table, or -1 */
    double p[3][3], uv[3][2];
    int col[3][3];
} MeshTri;

typedef struct MapRead {
    Run* r;
    const uint8_t* d;
    uint32_t size;
    int v102, found;
    uint32_t joint, texTable;
    const char* name;
    const char* pole;
    int poleSeen, parts;
    MeshTri* tris;
    int ntris, cap;
} MapRead;

static void mapNeed(MapRead* m, uint32_t off, uint32_t len) {
    if (off > m->size || len > m->size - off) {
        fail(m->r, "the map file is cut short");
    }
}

static uint32_t mapU32(MapRead* m, uint32_t off) {
    mapNeed(m, off, 4);
    return be32(m->d + off);
}

/* a pointer field: its file offset (0: none) */
static uint32_t mapPtr(MapRead* m, uint32_t off) {
    const uint32_t v = mapU32(m, off);
    return v ? 0x20 + v : 0;
}

static double mapF32(MapRead* m, uint32_t off) {
    union {
        uint32_t u;
        float f;
    } v;
    v.u = mapU32(m, off);
    return (double)v.f;
}

static int mapS16(MapRead* m, uint32_t off) {
    mapNeed(m, off, 2);
    return (int)(int16_t)be16(m->d + off);
}

/* the C string at off ("" for 0), checked to end inside the file */
static const char* mapStr(MapRead* m, uint32_t off) {
    if (off == 0) {
        return "";
    }
    mapNeed(m, off, 1);
    if (memchr(m->d + off, 0, m->size - off) == NULL) {
        fail(m->r, "the map file is cut short");
    }
    return (const char*)m->d + off;
}

static void mapFind(MapRead* m, uint32_t j) {
    while (j && !m->found) {
        if (strcmp(mapStr(m, mapPtr(m, j)), m->name) == 0) {
            m->found = 1;
            m->joint = j;
            return;
        }
        if (mapPtr(m, j + 0xC)) {
            mapFind(m, mapPtr(m, j + 0xC));
        }
        j = mapPtr(m, j + 0x10);
    }
}

typedef struct MeshVert {
    double p[3], uv[2];
    int c[3];
} MeshVert;

static void meshVert(MapRead* m, const uint32_t* idx, int ncol, int ntex, uint32_t posA, uint32_t colA, uint32_t texA,
                     double posK, double texK, MeshVert* v) {
    if (m->v102) {
        v->p[0] = (double)mapS16(m, posA + 4 + 6 * idx[0]) * posK;
        v->p[1] = (double)mapS16(m, posA + 4 + 6 * idx[0] + 2) * posK;
        v->p[2] = (double)mapS16(m, posA + 4 + 6 * idx[0] + 4) * posK;
    } else {
        v->p[0] = mapF32(m, posA + 4 + 12 * idx[0]);
        v->p[1] = mapF32(m, posA + 4 + 12 * idx[0] + 4);
        v->p[2] = mapF32(m, posA + 4 + 12 * idx[0] + 8);
    }
    v->uv[0] = v->uv[1] = 0.0;
    if (ntex && texA) {
        const uint32_t t = idx[2 + ncol];
        if (m->v102) {
            v->uv[0] = (double)mapS16(m, texA + 4 + 4 * t) * texK;
            v->uv[1] = (double)mapS16(m, texA + 4 + 4 * t + 2) * texK;
        } else {
            v->uv[0] = mapF32(m, texA + 4 + 8 * t);
            v->uv[1] = mapF32(m, texA + 4 + 8 * t + 4);
        }
    }
    v->c[0] = v->c[1] = v->c[2] = 255;
    if (ncol && colA) {
        mapNeed(m, colA + 4 + 4 * idx[2], 3);
        v->c[0] = m->d[colA + 4 + 4 * idx[2]];
        v->c[1] = m->d[colA + 4 + 4 * idx[2] + 1];
        v->c[2] = m->d[colA + 4 + 4 * idx[2] + 2];
    }
}

static void meshAdd(MapRead* m, int part, int pole, int tex, const MeshVert* a, const MeshVert* b, const MeshVert* c,
                    const double* o, const double* q) {
    const MeshVert* v[3];
    MeshTri* t;
    int i, k;
    if (m->ntris == m->cap) {
        const int cap = m->cap ? m->cap * 2 : 256;
        if (cap > 65536) {
            fail(m->r, "too many triangles");
        }
        m->tris = (MeshTri*)rgrow(m->r, m->tris, sizeof(MeshTri) * (size_t)m->cap, sizeof(MeshTri) * (size_t)cap);
        m->cap = cap;
    }
    v[0] = a;
    v[1] = b;
    v[2] = c;
    t = &m->tris[m->ntris++];
    t->part = part;
    t->pole = pole;
    t->tex = tex;
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 3; k++) {
            t->p[i][k] = v[i]->p[k] * q[k] + o[k];
            t->col[i][k] = v[i]->c[k];
        }
        t->uv[i][0] = v[i]->uv[0];
        t->uv[i][1] = v[i]->uv[1];
    }
}

/* a packed mesh's triangles */
static void meshTris(MapRead* m, uint32_t mesh, int part, int pole, int tex, const double* o, const double* q) {
    uint32_t mask, vcd, posA, colA, texA, li, n;
    int ncol = 0, ntex = 0, stride, i;
    double posK, texK;
    mapNeed(m, mesh, 0x10);
    if (!m->d[mesh + 3]) {
        fail(m->r, "an unpacked mesh: not drawn");
    }
    n = mapU32(m, mesh + 4);
    mask = mapU32(m, mesh + 8);
    vcd = mapPtr(m, mesh + 0xC);
    posA = mapPtr(m, vcd);
    colA = mapPtr(m, vcd + 0xC);
    texA = mapPtr(m, vcd + 0x18);
    posK = ldexp(1.0, -(int)mapU32(m, vcd + 0x44));
    texK = ldexp(1.0, -(int)mapU32(m, vcd + 0x48));
    for (i = 0; i < 2; i++) {
        ncol += (mask & (1u << (i + 2))) ? 1 : 0;
    }
    for (i = 0; i < 8 && (mask & (1u << (i + 4))); i++) {
        ntex++;
    }
    stride = 2 + ncol + ntex;
    if ((int32_t)n < 0 || n > 4096) {
        fail(m->r, "the map file is cut short");
    }
    for (li = 0; li < n; li++) {
        uint32_t o2 = mapPtr(m, mesh + 0x10 + 8 * li);
        const uint32_t end = o2 + mapU32(m, mesh + 0x10 + 8 * li + 4);
        while (o2 < end) {
            uint32_t cnt, k, idx[10];
            uint8_t cmd, prim;
            MeshVert* vs;
            mapNeed(m, o2, 1);
            cmd = m->d[o2];
            if (cmd == 0) {
                o2++;
                continue;
            }
            mapNeed(m, o2, 3);
            cnt = be16(m->d + o2 + 1);
            o2 += 3;
            vs = (MeshVert*)ralloc(m->r, sizeof(MeshVert) * (cnt ? cnt : 1));
            for (k = 0; k < cnt; k++) {
                int s;
                mapNeed(m, o2, 2 * (uint32_t)stride);
                for (s = 0; s < stride; s++) {
                    idx[s] = be16(m->d + o2 + 2 * s);
                }
                meshVert(m, idx, ncol, ntex, posA, colA, texA, posK, texK, &vs[k]);
                o2 += 2 * (uint32_t)stride;
            }
            prim = cmd & 0xF8;
            if (prim == 0x98) {
                for (k = 0; k + 2 < cnt; k++) {
                    if (k % 2 == 0) {
                        meshAdd(m, part, pole, tex, &vs[k], &vs[k + 1], &vs[k + 2], o, q);
                    } else {
                        meshAdd(m, part, pole, tex, &vs[k + 1], &vs[k], &vs[k + 2], o, q);
                    }
                }
            } else if (prim == 0x90) {
                for (k = 0; k + 2 < cnt; k += 3) {
                    meshAdd(m, part, pole, tex, &vs[k], &vs[k + 1], &vs[k + 2], o, q);
                }
            } else if (prim == 0xA0) {
                for (k = 1; k + 1 < cnt; k++) {
                    meshAdd(m, part, pole, tex, &vs[0], &vs[k], &vs[k + 1], o, q);
                }
            } else {
                fail(m->r, "display list command %#x: not drawn", cmd);
            }
        }
    }
}

static void mapWalk(MapRead* m, uint32_t j, const double* off, const double* k, int top) {
    while (j) {
        double s[3], rt[3], t[3], p[3], q[3];
        const char* name = mapStr(m, mapPtr(m, j));
        int i, nparts;
        for (i = 0; i < 3; i++) {
            s[i] = mapF32(m, j + 0x18 + 4 * i);
            rt[i] = mapF32(m, j + 0x24 + 4 * i);
            t[i] = mapF32(m, j + 0x30 + 4 * i);
        }
        if (top) { /* the named object itself: where it stands in the map doesn't count */
            s[0] = s[1] = s[2] = 1.0;
            t[0] = t[1] = t[2] = 0.0;
        } else if (rt[0] != 0.0 || rt[1] != 0.0 || rt[2] != 0.0) {
            fail(m->r, "%s is turned: not drawn", name);
        }
        for (i = 0; i < 3; i++) {
            p[i] = off[i] + t[i] * k[i];
            q[i] = k[i] * s[i];
        }
        nparts = (int)mapU32(m, j + 0x5C);
        for (i = 0; i < nparts && i < 64; i++) {
            const uint32_t mat = mapPtr(m, j + 0x60 + 8 * i), mesh = mapPtr(m, j + 0x60 + 8 * i + 4);
            int tex = -1;
            if (!mesh) {
                continue;
            }
            if (mat) {
                mapNeed(m, mat + 0xB, 1);
            }
            if (mat && m->d[mat + 0xB]) {
                const char* tn = mapStr(m, mapPtr(m, mapPtr(m, mapPtr(m, mat + 0xC))));
                if (tn[0] && m->texTable) {
                    const uint32_t nt = mapU32(m, m->texTable);
                    uint32_t x;
                    for (x = 0; x < nt && x < 4096; x++) {
                        if (strcmp(mapStr(m, mapPtr(m, m->texTable + 4 + 4 * x)), tn) == 0) {
                            tex = (int)x;
                            break;
                        }
                    }
                }
            }
            if (strcmp(name, m->pole) == 0) {
                m->poleSeen = 1;
            }
            meshTris(m, mesh, m->parts++, strcmp(name, m->pole) == 0, tex, p, q);
        }
        if (mapPtr(m, j + 0xC)) {
            mapWalk(m, mapPtr(m, j + 0xC), p, q, 0);
        }
        if (top) {
            return;
        }
        j = mapPtr(m, j + 0x10);
    }
}

/* cos and sin of whole degrees, on 1/65536ths (recipes.py's unit_turn) */
static void unitTurn(int deg, double* c, double* s) {
    const double a = (double)deg * (3.14159265358979323846 / 180.0);
    *c = floor(cos(a) * 65536.0 + 0.5) / 65536.0;
    *s = floor(sin(a) * 65536.0 + 0.5) / 65536.0;
}

static void norm3(double x, double y, double z, double* o) {
    const double n = sqrt(x * x + y * y + z * z);
    if (n > 0.0) {
        o[0] = x / n;
        o[1] = y / n;
        o[2] = z / n;
    } else {
        o[0] = o[1] = o[2] = 0.0;
    }
}

static void triNormal(double (*v)[3], double* o) {
    const double ux = v[1][0] - v[0][0], uy = v[1][1] - v[0][1], uz = v[1][2] - v[0][2];
    const double wx = v[2][0] - v[0][0], wy = v[2][1] - v[0][1], wz = v[2][2] - v[0][2];
    norm3(uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx, o);
}

/* the shading bands: flat per panel, as drawn */
static double meshShade(double d, int inside) {
    const double f = d >= 0.45 ? 1.0 : d >= 0.0 ? 0.86 : 0.72;
    return inside ? f * 0.8 : f;
}

static double min3(double a, double b, double c) {
    const double m = a < b ? a : b;
    return m < c ? m : c;
}

static double max3(double a, double b, double c) {
    const double m = a > b ? a : b;
    return m > c ? m : c;
}

/* mesh MAP OBJECT pole PART [closed] [turn D] [pitch D] size K at X Y [line COLOUR] */
static void stepMesh(Run* r, Img* img, char** a, int n) {
    static const char* kUsage = "mesh MAP OBJECT pole PART [closed] [turn D] [pitch D] size K at X Y [line COLOUR]";
    MapRead m;
    const char* pole = NULL;
    int closed = 0, turn = 0, pitch = 0, hasAt = 0, hasLine = 0, i, cc;
    double size = -1.0, ax = 0.0, ay = 0.0, cx = 0.0, cy = 0.0, cz = 0.0, np, fd, ex, ey, ez, sa[4], sb[4], ax3[3];
    double side, s, c, R[3][3], bx = 0.0, by = 0.0, bz = 0.0, rmax = 0.0, tip = 0.0, tc, ts, pc, ps, light[3];
    int line[4], haveBy = 0, haveTip = 0, k;
    const double* far;
    uint8_t* flips;
    double* zb;
    int* ids;
    Img* texImg;
    char path[96];
    uint32_t tsize;
    const uint8_t* tfile = NULL;
    if (n < 2) {
        fail(r, "%s", kUsage);
    }
    i = 2;
    while (i < n) {
        const char* kw = a[i];
        if (strcmp(kw, "closed") == 0) {
            closed = 1;
            i++;
        } else if ((strcmp(kw, "pole") == 0 || strcmp(kw, "turn") == 0 || strcmp(kw, "pitch") == 0 ||
                    strcmp(kw, "size") == 0 || strcmp(kw, "line") == 0) &&
                   i + 1 < n) {
            if (strcmp(kw, "pole") == 0) {
                pole = a[i + 1];
            } else if (strcmp(kw, "turn") == 0) {
                turn = integer(r, a[i + 1]);
            } else if (strcmp(kw, "pitch") == 0) {
                pitch = integer(r, a[i + 1]);
            } else if (strcmp(kw, "size") == 0) {
                size = num(r, a[i + 1]);
            } else {
                colour(r, a[i + 1], line);
                hasLine = 1;
            }
            i += 2;
        } else if (strcmp(kw, "at") == 0 && i + 2 < n) {
            ax = num(r, a[i + 1]);
            ay = num(r, a[i + 2]);
            hasAt = 1;
            i += 3;
        } else {
            fail(r, "%s: not understood (%s)", kw, kUsage);
        }
    }
    if (pole == NULL || !hasAt || !(size > 0.0 && size <= 256.0)) {
        fail(r, "%s", kUsage);
    }
    memset(&m, 0, sizeof(m));
    m.r = r;
    m.name = a[1];
    m.pole = pole;
    snprintf(path, sizeof(path), "%s/d", a[0]);
    m.d = need(r, path, &m.size);
    { /* the chunks: information and texture_table */
        uint32_t dsz, rc, cnt, strtab, info = 0, x;
        dsz = mapU32(&m, 4);
        rc = mapU32(&m, 8);
        cnt = mapU32(&m, 12);
        if (dsz > m.size || rc > m.size || cnt > 4096) {
            fail(r, "not a map file");
        }
        strtab = 0x20 + dsz + rc * 4 + cnt * 8;
        for (x = 0; x < cnt; x++) {
            const uint32_t at0 = 0x20 + dsz + rc * 4 + x * 8;
            const char* nm = mapStr(&m, strtab + mapU32(&m, at0 + 4));
            if (strcmp(nm, "information") == 0) {
                info = 0x20 + mapU32(&m, at0);
            } else if (strcmp(nm, "texture_table") == 0) {
                m.texTable = 0x20 + mapU32(&m, at0);
            }
        }
        if (info == 0) {
            fail(r, "not a map file");
        }
        m.v102 = strncmp(mapStr(&m, mapPtr(&m, info)), "ver1.02", 7) == 0;
        mapFind(&m, mapPtr(&m, info + 4));
        if (!m.found) {
            fail(r, "no object %s in the map", a[1]);
        }
    }
    {
        const double zero[3] = {0.0, 0.0, 0.0}, one[3] = {1.0, 1.0, 1.0};
        mapWalk(&m, m.joint, zero, one, 1);
    }
    if (!m.poleSeen) {
        fail(r, "no pole %s under %s", pole, a[1]);
    }
    /* the pole's line: from its middle, toward its farthest corner and away */
    np = 0.0;
    for (i = 0; i < m.ntris; i++) {
        if (m.tris[i].pole) {
            for (k = 0; k < 3; k++) {
                cx += m.tris[i].p[k][0];
                cy += m.tris[i].p[k][1];
                cz += m.tris[i].p[k][2];
                np += 1.0;
            }
        }
    }
    cx = cx / np;
    cy = cy / np;
    cz = cz / np;
    far = NULL;
    fd = -1.0;
    for (i = 0; i < m.ntris; i++) {
        if (m.tris[i].pole) {
            for (k = 0; k < 3; k++) {
                const double* p = m.tris[i].p[k];
                const double dd = (p[0] - cx) * (p[0] - cx) + (p[1] - cy) * (p[1] - cy) + (p[2] - cz) * (p[2] - cz);
                if (far == NULL) {
                    far = p;
                }
                if (dd > fd) {
                    far = p;
                    fd = dd;
                }
            }
        }
    }
    ex = far[0] - cx;
    ey = far[1] - cy;
    ez = far[2] - cz;
    memset(sa, 0, sizeof(sa));
    memset(sb, 0, sizeof(sb));
    for (i = 0; i < m.ntris; i++) {
        if (m.tris[i].pole) {
            for (k = 0; k < 3; k++) {
                const double* p = m.tris[i].p[k];
                double* sp = (p[0] - cx) * ex + (p[1] - cy) * ey + (p[2] - cz) * ez > 0.0 ? sa : sb;
                sp[0] += p[0];
                sp[1] += p[1];
                sp[2] += p[2];
                sp[3] += 1.0;
            }
        }
    }
    if (sa[3] == 0.0 || sb[3] == 0.0) {
        fail(r, "the pole %s has no length", pole);
    }
    norm3(sa[0] / sa[3] - sb[0] / sb[3], sa[1] / sa[3] - sb[1] / sb[3], sa[2] / sa[3] - sb[2] / sb[3], ax3);
    /* the pole points up the canopy: toward the side the other parts are on */
    side = 0.0;
    for (i = 0; i < m.ntris; i++) {
        if (!m.tris[i].pole) {
            for (k = 0; k < 3; k++) {
                const double* p = m.tris[i].p[k];
                side += (p[0] - cx) * ax3[0] + (p[1] - cy) * ax3[1] + (p[2] - cz) * ax3[2];
            }
        }
    }
    if (side < 0.0) {
        ax3[0] = -ax3[0];
        ax3[1] = -ax3[1];
        ax3[2] = -ax3[2];
    }
    /* turned onto +y (Rodrigues, about axis x (0, 1, 0)) */
    s = sqrt(ax3[2] * ax3[2] + ax3[0] * ax3[0]);
    c = ax3[1];
    if (s < 1e-9) {
        memset(R, 0, sizeof(R));
        R[0][0] = 1.0;
        R[1][1] = c > 0.0 ? 1.0 : -1.0;
        R[2][2] = c > 0.0 ? 1.0 : -1.0;
    } else {
        const double kx = -ax3[2] / s, kz = ax3[0] / s, t = 1.0 - c;
        R[0][0] = c + kx * kx * t;
        R[0][1] = -kz * s;
        R[0][2] = kx * kz * t;
        R[1][0] = kz * s;
        R[1][1] = c;
        R[1][2] = -kx * s;
        R[2][0] = kx * kz * t;
        R[2][1] = kx * s;
        R[2][2] = c + kz * kz * t;
    }
    for (i = 0; i < m.ntris; i++) {
        for (k = 0; k < 3; k++) {
            double* p = m.tris[i].p[k];
            const double x = R[0][0] * p[0] + R[0][1] * p[1] + R[0][2] * p[2];
            const double y = R[1][0] * p[0] + R[1][1] * p[1] + R[1][2] * p[2];
            const double z = R[2][0] * p[0] + R[2][1] * p[1] + R[2][2] * p[2];
            p[0] = x;
            p[1] = y;
            p[2] = z;
        }
    }
    for (i = 0; i < m.ntris; i++) {
        if (m.tris[i].pole) {
            for (k = 0; k < 3; k++) {
                bx += m.tris[i].p[k][0];
                bz += m.tris[i].p[k][2];
                if (!haveBy || m.tris[i].p[k][1] < by) {
                    by = m.tris[i].p[k][1];
                    haveBy = 1;
                }
            }
        }
    }
    bx = bx / np;
    bz = bz / np;
    for (i = 0; i < m.ntris; i++) {
        for (k = 0; k < 3; k++) {
            m.tris[i].p[k][0] = m.tris[i].p[k][0] - bx;
            m.tris[i].p[k][1] = m.tris[i].p[k][1] - by;
            m.tris[i].p[k][2] = m.tris[i].p[k][2] - bz;
        }
    }
    /* outward faces: the canopy's away from a point under its tip, the pole's away from its line */
    for (i = 0; i < m.ntris; i++) {
        if (!m.tris[i].pole) {
            for (k = 0; k < 3; k++) {
                const double* p = m.tris[i].p[k];
                const double rr = sqrt(p[0] * p[0] + p[2] * p[2]);
                rmax = rr > rmax ? rr : rmax;
                if (!haveTip || p[1] > tip) {
                    tip = p[1];
                    haveTip = 1;
                }
            }
        }
    }
    flips = (uint8_t*)ralloc(r, (size_t)m.ntris + 1);
    for (i = 0; i < m.ntris; i++) {
        double nv[3], mx, my, mz, out;
        double(*v)[3] = m.tris[i].p;
        triNormal(v, nv);
        mx = (v[0][0] + v[1][0] + v[2][0]) / 3.0;
        my = (v[0][1] + v[1][1] + v[2][1]) / 3.0;
        mz = (v[0][2] + v[1][2] + v[2][2]) / 3.0;
        out = m.tris[i].pole ? nv[0] * mx + nv[2] * mz : nv[0] * mx + nv[1] * (my - (tip - rmax)) + nv[2] * mz;
        flips[i] = out < 0.0;
    }
    if (closed && rmax > 0.0) {
        for (i = 0; i < m.ntris; i++) {
            if (m.tris[i].pole) {
                continue;
            }
            for (k = 0; k < 3; k++) {
                double* p = m.tris[i].p[k];
                const double rr = sqrt(p[0] * p[0] + p[2] * p[2]);
                double kk, u, nr;
                if (rr <= 1e-6) {
                    continue;
                }
                kk = rr / rmax;
                u = 0.15 + 0.85 * kk;
                u = u > 1.0 ? 1.0 : u;
                nr = rmax * (0.05 + 0.2 * sqrt(4.0 * u * (1.0 - u)));
                p[0] = p[0] * (nr / rr);
                p[1] = tip - 0.3 - kk * rmax * 1.45;
                p[2] = p[2] * (nr / rr);
            }
        }
    }
    /* the textures the triangles use */
    texImg = (Img*)ralloc(r, sizeof(Img) * 4096);
    for (i = 0; i < m.ntris; i++) {
        const int t = m.tris[i].tex;
        if (t >= 0 && t < 4096 && texImg[t].px == NULL) {
            if (tfile == NULL) {
                snprintf(path, sizeof(path), "%s/t", a[0]);
                tfile = need(r, path, &tsize);
            }
            texImg[t] = tplImage(r, path, tfile, tsize, t);
        }
    }
    unitTurn(turn, &tc, &ts);
    unitTurn(pitch, &pc, &ps);
    norm3(-0.5, 0.7, -0.5, light);
    zb = (double*)ralloc(r, sizeof(double) * (size_t)img->w * img->h);
    ids = (int*)ralloc(r, sizeof(int) * (size_t)img->w * img->h);
    for (k = 0; k < img->w * img->h; k++) {
        ids[k] = -1;
    }
    for (i = 0; i < m.ntris; i++) {
        const MeshTri* t = &m.tris[i];
        double v[3][3], nv[3], shade, pts[3][3], area;
        const int* base;
        int inside, col[4], pid, loX, hiX, loY, hiY, px, py, tcol[3] = {255, 255, 255};
        for (k = 0; k < 3; k++) {
            v[k][0] = t->p[k][0] * tc - t->p[k][2] * ts;
            v[k][1] = t->p[k][1];
            v[k][2] = t->p[k][0] * ts + t->p[k][2] * tc;
        }
        triNormal(v, nv);
        if (flips[i]) {
            nv[0] = -nv[0];
            nv[1] = -nv[1];
            nv[2] = -nv[2];
        }
        inside = nv[1] * ps - nv[2] * pc < 0.0;
        if (inside) {
            nv[0] = -nv[0];
            nv[1] = -nv[1];
            nv[2] = -nv[2];
        }
        shade = meshShade(nv[0] * light[0] + nv[1] * light[1] + nv[2] * light[2], inside);
        /* the panel's own colour: its lightest vertex's (the disc's baked shading off) */
        base = t->col[0];
        for (k = 1; k < 3; k++) {
            if (299 * t->col[k][0] + 587 * t->col[k][1] + 114 * t->col[k][2] > 299 * base[0] + 587 * base[1] + 114 * base[2]) {
                base = t->col[k];
            }
        }
        if (t->tex >= 0 && t->tex < 4096 && texImg[t->tex].px != NULL) {
            const Img* tx = &texImg[t->tex];
            const double u = (t->uv[0][0] + t->uv[1][0] + t->uv[2][0]) / 3.0;
            const double vv = (t->uv[0][1] + t->uv[1][1] + t->uv[2][1]) / 3.0;
            int sx = (int)floor(pymod(u, 1.0) * (double)tx->w), sy = (int)floor(pymod(vv, 1.0) * (double)tx->h);
            sx = sx > tx->w - 1 ? tx->w - 1 : sx;
            sy = sy > tx->h - 1 ? tx->h - 1 : sy;
            for (k = 0; k < 3; k++) {
                tcol[k] = tx->px[((size_t)sy * tx->w + sx) * 4 + k];
            }
        }
        for (k = 0; k < 3; k++) {
            col[k] = clampByte(rnd((double)(base[k] * tcol[k]) / 255.0 * shade));
        }
        col[3] = 255;
        pid = t->part * 2 + (inside ? 1 : 0);
        for (k = 0; k < 3; k++) {
            pts[k][0] = ax + v[k][0] * size;
            pts[k][1] = ay - (v[k][1] * pc + v[k][2] * ps) * size;
            pts[k][2] = v[k][2] * pc - v[k][1] * ps;
        }
        area = (pts[1][0] - pts[0][0]) * (pts[2][1] - pts[0][1]) - (pts[1][1] - pts[0][1]) * (pts[2][0] - pts[0][0]);
        if (area == 0.0) {
            continue;
        }
        {
            const double fx0 = floor(min3(pts[0][0], pts[1][0], pts[2][0])), fx1 = floor(max3(pts[0][0], pts[1][0], pts[2][0]));
            const double fy0 = floor(min3(pts[0][1], pts[1][1], pts[2][1])), fy1 = floor(max3(pts[0][1], pts[1][1], pts[2][1]));
            if (fx1 < 0.0 || fy1 < 0.0 || fx0 > (double)(img->w - 1) || fy0 > (double)(img->h - 1)) {
                continue;
            }
            loX = fx0 < 0.0 ? 0 : (int)fx0;
            hiX = fx1 > (double)(img->w - 1) ? img->w - 1 : (int)fx1;
            loY = fy0 < 0.0 ? 0 : (int)fy0;
            hiY = fy1 > (double)(img->h - 1) ? img->h - 1 : (int)fy1;
        }
        for (py = loY; py <= hiY; py++) {
            const double qy = (double)py + 0.5;
            for (px = loX; px <= hiX; px++) {
                const double qx = (double)px + 0.5;
                const double w0 = (pts[1][0] - qx) * (pts[2][1] - qy) - (pts[1][1] - qy) * (pts[2][0] - qx);
                const double w1 = (pts[2][0] - qx) * (pts[0][1] - qy) - (pts[2][1] - qy) * (pts[0][0] - qx);
                const double w2 = (pts[0][0] - qx) * (pts[1][1] - qy) - (pts[0][1] - qy) * (pts[1][0] - qx);
                double dep;
                size_t kk;
                if (area > 0.0) {
                    if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) {
                        continue;
                    }
                } else if (w0 > 0.0 || w1 > 0.0 || w2 > 0.0) {
                    continue;
                }
                dep = (w0 * pts[0][2] + w1 * pts[1][2] + w2 * pts[2][2]) / area;
                kk = (size_t)py * img->w + px;
                if (ids[kk] < 0 || dep < zb[kk]) {
                    zb[kk] = dep;
                    ids[kk] = pid;
                    store(img, px, py, col);
                }
            }
        }
    }
    if (hasLine) {
        uint8_t* marks = (uint8_t*)ralloc(r, (size_t)img->w * img->h);
        static const int kN[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        int px, py;
        for (py = 0; py < img->h; py++) {
            for (px = 0; px < img->w; px++) {
                const size_t kk = (size_t)py * img->w + px;
                if (ids[kk] < 0) {
                    continue;
                }
                for (cc = 0; cc < 4; cc++) {
                    const int qx = px + kN[cc][0], qy = py + kN[cc][1];
                    if (qx >= 0 && qx < img->w && qy >= 0 && qy < img->h) {
                        const size_t q = (size_t)qy * img->w + qx;
                        if (ids[q] >= 0 && ids[q] != ids[kk] && zb[q] < zb[kk]) {
                            marks[kk] = 1;
                            break;
                        }
                    }
                }
            }
        }
        for (py = 0; py < img->h; py++) {
            for (px = 0; px < img->w; px++) {
                if (marks[(size_t)py * img->w + px]) {
                    store(img, px, py, line);
                }
            }
        }
    }
}

/* ---- one line ---- */

static Img* target(Run* r, const char* piece) {
    if (piece != NULL) {
        Piece* p = findPiece(r, piece);
        if (p == NULL) {
            fail(r, "no piece %s", piece);
        }
        return &p->img;
    }
    if (r->canvas.px == NULL) {
        fail(r, "no canvas yet (`load` or `canvas` first)");
    }
    return &r->canvas;
}

static void setPiece(Run* r, const char* name, Img img) {
    Piece* p = findPiece(r, name);
    if (p == NULL) {
        if (strlen(name) >= NAME_LEN) {
            fail(r, "%s: too long a name", name);
        }
        r->pieces = (Piece*)rgrow(r, r->pieces, sizeof(Piece) * (size_t)r->npieces,
                                  sizeof(Piece) * (size_t)(r->npieces + 1));
        p = &r->pieces[r->npieces++];
        snprintf(p->name, NAME_LEN, "%s", name);
    }
    p->img = img;
}

static void line(Run* r, char** t, int nt, char** rows, int nrows, int inImage) {
    const char* piece = NULL;
    const char* op;
    char** a;
    int n;
    if (t[0][0] == '@') {
        piece = t[0] + 1;
        if (findPiece(r, piece) == NULL) {
            fail(r, "no piece %s", piece);
        }
        t++;
        nt--;
        if (nt == 0) {
            fail(r, "@%s: and the step?", piece);
        }
    }
    op = t[0];
    a = t + 1;
    n = nt - 1;
    if (!inImage && strcmp(op, "gradient") != 0 && strcmp(op, "colours") != 0 && strcmp(op, "mask") != 0 &&
        strcmp(op, "fallback") != 0) {
        fail(r, "`%s` outside an image", op);
    }
    if (strcmp(op, "fallback") == 0) {
        return;
    }
    if (strcmp(op, "discs") == 0) {
        int i;
        if (!inImage || n == 0) {
            fail(r, "discs ID... (in an image)");
        }
        for (i = 0; i < n && strcmp(a[i], r->disc->id) != 0; i++) {
        }
        if (i == n) {
            r->notHere = 1;
            fail(r, "not made on %s", r->disc->id);
        }
        return;
    }
    if (strcmp(op, "drawn") == 0) {
        int i;
        if (!inImage || n == 0) {
            fail(r, "drawn ID... (in an image)");
        }
        for (i = 0; i < n && strcmp(a[i], r->disc->id) != 0; i++) {
        }
        if (i < n) {
            r->drawnHere = 1;
            fail(r, "drawn on %s", r->disc->id);
        }
        return;
    }
    if (strcmp(op, "gradient") == 0) {
        Grad g;
        int i;
        if (n < 5 || n % 2 == 0 || (n - 1) / 2 > 32) {
            fail(r, "gradient NAME T COLOUR T COLOUR ... (2 to 32 stops)");
        }
        memset(&g, 0, sizeof(g));
        if (strlen(a[0]) >= NAME_LEN) {
            fail(r, "%s: too long a name", a[0]);
        }
        snprintf(g.name, NAME_LEN, "%s", a[0]);
        g.n = (n - 1) / 2;
        for (i = 0; i < g.n; i++) {
            g.t[i] = num(r, a[1 + 2 * i]);
            colour(r, a[2 + 2 * i], g.c[i]);
            if (i > 0 && g.t[i] <= g.t[i - 1]) {
                fail(r, "a gradient's stops go up");
            }
        }
        r->grads = (Grad*)rgrow(r, r->grads, sizeof(Grad) * (size_t)r->ngrads, sizeof(Grad) * (size_t)(r->ngrads + 1));
        r->grads[r->ngrads++] = g;
        return;
    }
    if (strcmp(op, "colours") == 0) {
        int i = 0, c[4];
        while (i < n) {
            if (i + 1 >= n) {
                fail(r, "colours NAME COLOUR ...");
            }
            if (strcmp(a[i + 1], "sample") == 0) {
                Img* img;
                int x, y, k;
                if (i + 3 >= n) {
                    fail(r, "sample X Y");
                }
                img = target(r, piece);
                x = integer(r, a[i + 2]);
                y = integer(r, a[i + 3]);
                if (x < 0 || x >= img->w || y < 0 || y >= img->h) {
                    fail(r, "sample %d %d: outside the image", x, y);
                }
                for (k = 0; k < 4; k++) {
                    c[k] = at(img, x, y)[k];
                }
                setColour(r, a[i], c);
                i += 4;
            } else {
                colour(r, a[i + 1], c);
                setColour(r, a[i], c);
                i += 2;
            }
        }
        return;
    }
    if (strcmp(op, "mask") == 0) {
        maskLine(r, a, n, rows, nrows);
        return;
    }
    if (strcmp(op, "load") == 0) {
        int used;
        Img img;
        if (piece != NULL) {
            fail(r, "`load` is the canvas's; a piece is `piece NAME SOURCE`");
        }
        img = loadSource(r, a, n, &used);
        if (used != n) {
            fail(r, "%s: not understood", a[used]);
        }
        r->canvas = img;
        return;
    }
    if (strcmp(op, "canvas") == 0) {
        if (piece != NULL) {
            fail(r, "`canvas` is the canvas's; a piece is `piece NAME canvas W H`");
        }
        r->canvas = blank(r, a, n);
        return;
    }
    if (strcmp(op, "piece") == 0) {
        Img img;
        if (n < 2) {
            fail(r, "piece NAME SOURCE | piece NAME canvas W H [COLOUR] | piece NAME copy");
        }
        if (strcmp(a[1], "canvas") == 0) {
            img = blank(r, a + 2, n - 2);
        } else if (strcmp(a[1], "copy") == 0) {
            if (n != 2) {
                fail(r, "%s: not understood", a[2]);
            }
            img = copyImg(r, target(r, NULL));
        } else {
            int used;
            img = loadSource(r, a + 1, n - 1, &used);
            if (used != n - 1) {
                fail(r, "%s: not understood", a[1 + used]);
            }
        }
        setPiece(r, a[0], img);
        return;
    }
    {
        Img* img = target(r, piece);
        if (strcmp(op, "ramp") == 0) {
            stepRamp(r, img, a, n);
        } else if (strcmp(op, "map") == 0) {
            stepMap(r, img, a, n);
        } else if (strcmp(op, "hsv") == 0) {
            stepHsv(r, img, a, n);
        } else if (strcmp(op, "fill") == 0) {
            stepFill(r, img, a, n);
        } else if (strcmp(op, "paint") == 0) {
            stepPaint(r, img, a, n, rows, nrows);
        } else if (strcmp(op, "stamp") == 0) {
            stepStamp(r, img, a, n);
        } else if (strcmp(op, "slide") == 0) {
            stepSlide(r, img, a, n);
        } else if (strcmp(op, "crop") == 0) {
            *img = stepCrop(r, img, a, n);
        } else if (strcmp(op, "flip") == 0) {
            *img = stepFlip(r, img, a, n);
        } else if (strcmp(op, "rotate") == 0) {
            *img = stepRotate(r, img, a, n);
        } else if (strcmp(op, "grow") == 0) {
            *img = stepGrow(r, img, a, n);
        } else if (strcmp(op, "scale") == 0) {
            *img = stepScale(r, img, a, n);
        } else if (strcmp(op, "paste") == 0) {
            stepPaste(r, img, a, n);
        } else if (strcmp(op, "alpha") == 0) {
            stepAlpha(r, img, a, n);
        } else if (strcmp(op, "mesh") == 0) {
            stepMesh(r, img, a, n);
        } else {
            fail(r, "`%s`: no such step", op);
        }
    }
}

static void freeRun(Run* r) {
    int i;
    for (i = 0; i < r->nallocs; i++) {
        free(r->allocs[i]);
    }
    free(r->allocs);
    r->allocs = NULL;
    r->nallocs = r->capallocs = 0;
}

static void runSpan(Run* r, int from, int to, int depth);

/* one line of an image, or of a `steps` block it uses: `use NAME` runs the block's lines here, as if
 * written in its place (an error names the block's line) */
static void runLine(Run* r, Line* l, char** rows, int nrows, int inImage, int depth) {
    r->line = l->num;
    if (nrows && strcmp(l->tok[0], "paint") != 0 && strcmp(l->tok[0], "mask") != 0 && l->tok[0][0] != '@') {
        fail(r, "`%s` takes no rows", l->tok[0]);
    }
    if (strcmp(l->tok[0], "use") == 0) {
        int i;
        if (!inImage || l->ntok != 2) {
            fail(r, "use NAME (in an image)");
        }
        /* the last block of that name */
        for (i = r->nsteps - 1; i >= 0 && strcmp(r->steps[i].name, l->tok[1]) != 0; i--) {
        }
        if (i < 0) {
            fail(r, "no steps %s", l->tok[1]);
        }
        if (depth >= 8) {
            fail(r, "use %s: steps used 8 deep", l->tok[1]);
        }
        runSpan(r, r->steps[i].from, r->steps[i].to, depth + 1);
        return;
    }
    line(r, l->tok, l->ntok, rows, nrows, inImage);
}

/* a `steps` block's lines, each with the rows under it */
static void runSpan(Run* r, int from, int to, int depth) {
    int k = from;
    while (k < to) {
        Line* l = &r->ls[k++];
        int first = k;
        while (k < to && r->ls[k].tok == NULL) {
            k++;
        }
        {
            int j, nrows = k - first;
            char** rows = (char**)ralloc(r, sizeof(char*) * (size_t)(nrows ? nrows : 1));
            for (j = 0; j < nrows; j++) {
                rows[j] = r->ls[first + j].row;
            }
            runLine(r, l, rows, nrows, 1, depth);
        }
    }
}

int portArtMake(const char* text, const char* path, PortArtDisc* disc, PortArtImage* out, char* err, size_t errSize) {
    Run* r = (Run*)calloc(1, sizeof(Run));
    char dummy[8];
    volatile int ok = 0;
    out->w = out->h = 0;
    out->px = NULL;
    if (r == NULL) {
        snprintf(err, errSize, "out of memory");
        return 0;
    }
    r->err = err != NULL ? err : dummy;
    r->errSize = err != NULL ? errSize : sizeof(dummy);
    r->err[0] = 0;
    r->disc = disc;
    if (setjmp(r->fail) == 0) {
        int nlines, k = 0;
        Line* ls = splitLines(r, text, &nlines);
        int state = 0; /* 0: between images, 1: this image, 2: another, 3: a `steps` block */
        r->ls = ls;
        while (k < nlines) {
            Line* l = &ls[k++];
            int first = k, nrows;
            char** rows;
            while (k < nlines && ls[k].tok == NULL) {
                k++;
            }
            nrows = k - first;
            rows = (char**)ralloc(r, sizeof(char*) * (size_t)(nrows ? nrows : 1));
            {
                int j;
                for (j = 0; j < nrows; j++) {
                    rows[j] = ls[first + j].row;
                }
            }
            r->line = l->num;
            if (l->tok == NULL) {
                fail(r, "a row ('|') without a `paint` or `mask ... rows` before it");
            }
            if (strcmp(l->tok[0], "image") == 0) {
                if (state != 0) {
                    fail(r, "`image` before the last one's `end`");
                }
                if (l->ntok != 2) {
                    fail(r, "image PATH");
                }
                state = strcmp(l->tok[1], path) == 0 ? 1 : 2;
                continue;
            }
            if (strcmp(l->tok[0], "steps") == 0) {
                Steps* s;
                if (state != 0) {
                    fail(r, "`steps` before the last one's `end`");
                }
                if (l->ntok != 2) {
                    fail(r, "steps NAME");
                }
                if (strlen(l->tok[1]) >= NAME_LEN) {
                    fail(r, "%s: too long a name", l->tok[1]);
                }
                r->steps = (Steps*)rgrow(r, r->steps, sizeof(Steps) * (size_t)r->nsteps,
                                         sizeof(Steps) * (size_t)(r->nsteps + 1));
                s = &r->steps[r->nsteps++];
                snprintf(s->name, NAME_LEN, "%s", l->tok[1]);
                s->from = s->to = k;
                state = 3;
                continue;
            }
            if (strcmp(l->tok[0], "end") == 0) {
                if (state == 0) {
                    fail(r, "`end` without an `image`");
                }
                if (state == 3) {
                    r->steps[r->nsteps - 1].to = (int)(l - ls);
                    state = 0;
                    continue;
                }
                if (state == 1) {
                    if (r->canvas.px == NULL) {
                        fail(r, "the image has no canvas (`load` or `canvas`)");
                    }
                    out->px = (uint8_t*)malloc((size_t)r->canvas.w * r->canvas.h * 4);
                    if (out->px == NULL) {
                        fail(r, "out of memory");
                    }
                    memcpy(out->px, r->canvas.px, (size_t)r->canvas.w * r->canvas.h * 4);
                    out->w = r->canvas.w;
                    out->h = r->canvas.h;
                    ok = 1;
                    break;
                }
                state = 0;
                continue;
            }
            if (state == 2 || state == 3) {
                continue;
            }
            runLine(r, l, rows, nrows, state == 1, 0);
        }
        if (!ok) {
            snprintf(r->err, r->errSize, state == 0 ? "no image %s" : "no `end`", path);
        }
    }
    if (r->notHere) {
        ok = -1;
    } else if (r->drawnHere) {
        ok = -2;
    }
    freeRun(r);
    free(r);
    return ok;
}

/* ---- the file's images ---- */

/* the tokens of each line, without the run's memory: fn(user, tokens, count) until it returns 0 */
static void eachLine(const char* text, int (*fn)(void* user, char** tok, int n), void* user) {
    const char* p = text;
    while (*p) {
        char buf[1024];
        char* toks[8];
        char* s;
        char* c;
        int n = 0;
        size_t len = 0;
        while (p[len] && p[len] != '\n') {
            len++;
        }
        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1;
        }
        memcpy(buf, p, len);
        buf[len] = 0;
        p += len;
        while (*p && *p != '\n') {
            p++;
        }
        if (*p) {
            p++;
        }
        for (s = buf; *s && isspace((unsigned char)*s); s++) {
        }
        if (*s == '|') {
            continue;
        }
        for (c = s; *c; c++) {
            if (*c == '#' && (c[1] == 0 || c[1] == ' ' || c[1] == '\t' || c[1] == '#')) {
                *c = 0;
                break;
            }
        }
        c = s;
        while (*c && n < 8) {
            while (*c && isspace((unsigned char)*c)) {
                c++;
            }
            if (!*c) {
                break;
            }
            toks[n++] = c;
            while (*c && !isspace((unsigned char)*c)) {
                c++;
            }
            if (*c) {
                *c++ = 0;
            }
        }
        if (n > 0 && !fn(user, toks, n)) {
            return;
        }
    }
}

typedef struct ImagesCtx {
    void (*fn)(void* user, const char* path);
    void* user;
    int count;
} ImagesCtx;

static int imagesLine(void* user, char** tok, int n) {
    ImagesCtx* c = (ImagesCtx*)user;
    if (strcmp(tok[0], "image") == 0 && n == 2) {
        if (c->fn != NULL) {
            c->fn(c->user, tok[1]);
        }
        c->count++;
    }
    return 1;
}

int portArtImages(const char* text, void (*fn)(void* user, const char* path), void* user) {
    ImagesCtx c;
    c.fn = fn;
    c.user = user;
    c.count = 0;
    eachLine(text, imagesLine, &c);
    return c.count;
}

typedef struct FallbackCtx {
    const char* path;
    char* out;
    size_t size;
    int on, found;
} FallbackCtx;

static int fallbackLine(void* user, char** tok, int n) {
    FallbackCtx* c = (FallbackCtx*)user;
    if (strcmp(tok[0], "image") == 0) {
        c->on = n == 2 && strcmp(tok[1], c->path) == 0;
    } else if (strcmp(tok[0], "end") == 0) {
        c->on = 0;
    } else if (c->on && strcmp(tok[0], "fallback") == 0 && n == 2) {
        snprintf(c->out, c->size, "%s", tok[1]);
        c->found = 1;
        return 0;
    }
    return 1;
}

int portArtFallback(const char* text, const char* path, char* out, size_t size) {
    FallbackCtx c;
    c.path = path;
    c.out = out;
    c.size = size;
    c.on = 0;
    c.found = 0;
    eachLine(text, fallbackLine, &c);
    return c.found;
}
