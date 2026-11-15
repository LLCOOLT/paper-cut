/* A recipe's animations made from the disc's (port/include/port/anim_recipe.h): tools/port/anim_recipe.py's
 * make() and PoseFile.build() in C, step for step and in the same order of arithmetic, so the file is byte for
 * byte Python's (tests/port/anim_recipe_check.py runs both). The notes name the Python each part follows. */
#include "port/anim_recipe.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (Python's arithmetic: every multiply and add rounded on its own) */
#pragma STDC FP_CONTRACT OFF
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#define HEADER_SIZE 0x1B0
#define ANIM_HEADER 0x5C
#define NODE_SIZE 24
/* the model's table indices (ttyd_anim.TABLES) and their header count words */
#define T_SHAPES 0
#define T_POS 2
#define T_TEXANIM 17
#define T_VIS 21
#define T_NODE 22
#define T_GROUPS 23
#define C_POS 2
#define C_SHAPE 0
#define C_TEXANIM 17
#define C_VIS 21
#define C_NODE 22
#define C_GROUP 23
#define C_ANIM 24

static const double kDegToRad = 3.14159265358979323846 / 180.0; /* (math.radians) */
static const double kRadToDeg = 180.0 / 3.14159265358979323846; /* (math.degrees) */

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static float bef(const uint8_t* p) {
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static void putf(uint8_t* p, double v) {
    float f = (float)v;
    uint32_t u;
    memcpy(&u, &f, 4);
    put32(p, u);
}

/* Python's int(round(x)): half to even */
static long pyRound(double x) {
    return (long)nearbyint(x); /* (the default rounding: to nearest, ties to even) */
}

/* round(x, 4) (correctly rounded; no exact ties are possible at four decimals) */
static double pyRound4(double x) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", x);
    return strtod(buf, NULL);
}

static double tanDeg(int a) {
    return tan((double)a * kDegToRad);
}

/* sum() of floats from 0 (CPython 3.12+: Neumaier's compensated sum) */
typedef struct {
    double f, c;
    int n;
} PySum;

static void pySumAdd(PySum* s, double x) {
    if (s->n++ == 0) {
        s->f = 0.0 + x;
        s->c = 0.0;
        return;
    }
    {
        const double t = s->f + x;
        if (fabs(s->f) >= fabs(x)) {
            s->c += (s->f - t) + x;
        } else {
            s->c += (x - t) + s->f;
        }
        s->f = t;
    }
}

static double pySumEnd(const PySum* s) {
    double f = s->n ? s->f : 0.0;
    if (s->n && s->c != 0.0 && isfinite(s->c)) {
        f += s->c;
    }
    return f;
}

/* ----------------------------------------------------------- a growing buffer */

typedef struct {
    uint8_t* p;
    size_t n, cap;
    int bad;
} Buf;

static uint8_t* bufAdd(Buf* b, size_t n) {
    if (b->bad) {
        return NULL;
    }
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        uint8_t* q;
        while (cap < b->n + n) {
            cap *= 2;
        }
        q = realloc(b->p, cap);
        if (q == NULL) {
            b->bad = 1;
            return NULL;
        }
        b->p = q;
        b->cap = cap;
    }
    memset(b->p + b->n, 0, n);
    b->n += n;
    return b->p + b->n - n;
}

static void bufAlign(Buf* b, size_t a) {
    bufAdd(b, (a - b->n % a) % a);
}

static void bufBytes(Buf* b, int s, int x, int y, int z) {
    uint8_t* p = bufAdd(b, 4);
    if (p != NULL) {
        p[0] = (uint8_t)s, p[1] = (uint8_t)x, p[2] = (uint8_t)y, p[3] = (uint8_t)z;
    }
}

/* ------------------------------------------------------------------ the model */

typedef struct {
    const uint8_t* d;
    uint32_t size;
    uint32_t counts[25], ptrs[25];
    int nNode, nVis, nPos, nTex, nGroups, nShapes;
    double* node; /* node_values() */
    int* vis;
    double* pos; /* 3 a vertex */
    int* texBind;
    double* texST; /* 2 a slot */
} Model;

static int tableOk(const Model* m, int t, int c, uint32_t rec) {
    return (uint64_t)m->ptrs[t] + (uint64_t)m->counts[c] * rec <= m->size;
}

static const uint8_t* groupRec(const Model* m, int g) {
    return m->d + m->ptrs[T_GROUPS] + 0x58 * (uint32_t)g;
}

static int groupInt(const Model* m, int g, int off) {
    return (int)be32(groupRec(m, g) + off);
}

static int modelOpen(Model* m, const uint8_t* d, uint32_t size) {
    int i;
    memset(m, 0, sizeof(*m));
    m->d = d;
    m->size = size;
    if (size < HEADER_SIZE) {
        return 0;
    }
    for (i = 0; i < 25; i++) {
        m->counts[i] = be32(d + 0xE8 + 4 * i);
        m->ptrs[i] = be32(d + 0x14C + 4 * i);
    }
    if (!tableOk(m, T_NODE, C_NODE, 4) || !tableOk(m, T_VIS, C_VIS, 1) || !tableOk(m, T_POS, C_POS, 12) ||
        !tableOk(m, T_TEXANIM, C_TEXANIM, 0x18) || !tableOk(m, T_GROUPS, C_GROUP, 0x58) ||
        !tableOk(m, T_SHAPES, C_SHAPE, 0xA8) || !tableOk(m, 24, C_ANIM, 0x40) || m->counts[C_NODE] > 1000000 ||
        m->counts[C_POS] > 1000000 || m->counts[C_GROUP] == 0) {
        return 0;
    }
    m->nNode = (int)m->counts[C_NODE];
    m->nVis = (int)m->counts[C_VIS];
    m->nPos = (int)m->counts[C_POS];
    m->nTex = (int)m->counts[C_TEXANIM];
    m->nGroups = (int)m->counts[C_GROUP];
    m->nShapes = (int)m->counts[C_SHAPE];
    m->node = malloc(sizeof(double) * (size_t)(m->nNode + 1));
    m->vis = malloc(sizeof(int) * (size_t)(m->nVis + 1));
    m->pos = malloc(sizeof(double) * 3 * (size_t)(m->nPos + 1));
    m->texBind = malloc(sizeof(int) * (size_t)(m->nTex + 1));
    m->texST = malloc(sizeof(double) * 2 * (size_t)(m->nTex + 1));
    if (m->node == NULL || m->vis == NULL || m->pos == NULL || m->texBind == NULL || m->texST == NULL) {
        return 0;
    }
    for (i = 0; i < m->nNode; i++) {
        m->node[i] = bef(d + m->ptrs[T_NODE] + 4 * i);
    }
    for (i = 0; i < m->nVis; i++) {
        m->vis[i] = (signed char)d[m->ptrs[T_VIS] + i];
    }
    for (i = 0; i < 3 * m->nPos; i++) {
        m->pos[i] = bef(d + m->ptrs[T_POS] + 4 * i);
    }
    for (i = 0; i < m->nTex; i++) {
        const uint8_t* r = d + m->ptrs[T_TEXANIM] + 0x18 * i;
        m->texBind[i] = r[0];
        m->texST[2 * i] = bef(r + 4);
        m->texST[2 * i + 1] = bef(r + 8);
    }
    for (i = 0; i < m->nGroups; i++) {
        const int nd = groupInt(m, i, 0x50), vi = groupInt(m, i, 0x4C), sh = groupInt(m, i, 0x48);
        if (nd < 0 || nd + NODE_SIZE > m->nNode || vi < 0 || vi >= m->nVis || sh >= m->nShapes) {
            return 0;
        }
        if (sh >= 0) {
            const uint8_t* s = d + m->ptrs[T_SHAPES] + 0xA8 * (uint32_t)sh;
            const int p0 = (int)be32(s + 0x40), pn = (int)be32(s + 0x44);
            if (p0 < 0 || pn < 0 || p0 + pn > m->nPos) {
                return 0;
            }
        }
    }
    return 1;
}

static void modelClose(Model* m) {
    free(m->node);
    free(m->vis);
    free(m->pos);
    free(m->texBind);
    free(m->texST);
}

/* --------------------------------------------- an animation, decoded (Anim.parse) */

typedef struct {
    int idx, n;
    int first, count; /* its chunks: entries in the node buffer */
} NodeRun;

typedef struct {
    int idx;
    int v[3];
} VecRun;

typedef struct {
    int idx, v;
} VisE;

typedef struct {
    int step, add;
    double s, t;
} TexE;

typedef struct {
    double time;
    NodeRun* node;
    int nNode;
    VecRun* pos;
    int nPos;
    VisE* vis;
    int nVis;
    TexE* tex;
    int nTex;
} SrcKey;

typedef struct {
    int loop;
    double start, end;
    int nKeys;
    SrcKey* keys;
    const uint8_t* nodeBuf; /* 4 bytes an entry: step, value, start angle, end angle */
} Src;

static void srcFree(Src* s) {
    int k;
    for (k = 0; s->keys != NULL && k < s->nKeys; k++) {
        free(s->keys[k].node);
        free(s->keys[k].pos);
        free(s->keys[k].vis);
        free(s->keys[k].tex);
    }
    free(s->keys);
    memset(s, 0, sizeof(*s));
}

/* the AnimData at d (n bytes) decoded; 0 if it doesn't fit */
static int srcParse(Src* s, const uint8_t* d, uint32_t n) {
    uint32_t c[9], p[8];
    int i, k;
    memset(s, 0, sizeof(*s));
    if (n < 0x44) {
        return 0;
    }
    for (i = 0; i < 9; i++) {
        c[i] = be32(d + 4 * i);
    }
    for (i = 0; i < 8; i++) {
        p[i] = be32(d + 36 + 4 * i);
    }
    if (c[1] == 0 || c[2] == 0 || (uint64_t)p[0] + 12 > n || (uint64_t)p[1] + 0x2CULL * c[2] > n ||
        (uint64_t)p[2] + 4ULL * c[3] > n || (uint64_t)p[4] + 12ULL * c[5] > n || (uint64_t)p[5] + 2ULL * c[6] > n ||
        (uint64_t)p[6] + 4ULL * c[7] > n || c[2] > 100000) {
        return 0;
    }
    s->loop = (int)be32(d + p[0]) != 0;
    s->start = bef(d + p[0] + 4);
    s->end = bef(d + p[0] + 8);
    s->nKeys = (int)c[2];
    s->nodeBuf = d + p[6];
    s->keys = calloc((size_t)s->nKeys, sizeof(SrcKey));
    if (s->keys == NULL) {
        return 0;
    }
    for (k = 0; k < s->nKeys; k++) {
        const uint8_t* kf = d + p[1] + 0x2C * (uint32_t)k;
        SrcKey* key = &s->keys[k];
        uint32_t f[10], j;
        int idx, cur;
        for (i = 0; i < 10; i++) {
            f[i] = be32(kf + 4 + 4 * i);
        }
        key->time = bef(kf);
        if ((uint64_t)f[0] + f[1] > c[3] || (uint64_t)f[4] + f[5] > c[5] || (uint64_t)f[6] + f[7] > c[6] ||
            (uint64_t)f[8] + f[9] > c[7]) {
            return 0;
        }
        /* vertex positions: runs of chunks at an index, each run's sum */
        key->pos = calloc((size_t)f[1] + 1, sizeof(VecRun));
        key->vis = calloc((size_t)f[7] + 1, sizeof(VisE));
        key->tex = calloc((size_t)f[5] + 1, sizeof(TexE));
        key->node = calloc((size_t)f[9] + 1, sizeof(NodeRun));
        if (key->pos == NULL || key->vis == NULL || key->tex == NULL || key->node == NULL) {
            return 0;
        }
        idx = 0;
        cur = -1;
        for (j = f[0]; j < f[0] + f[1]; j++) {
            const uint8_t* e = d + p[2] + 4 * j;
            if (e[0] == 0 && cur >= 0) {
                for (i = 0; i < 3; i++) {
                    key->pos[cur].v[i] += (signed char)e[1 + i];
                }
            } else {
                idx += e[0];
                cur = key->nPos++;
                key->pos[cur].idx = idx;
                for (i = 0; i < 3; i++) {
                    key->pos[cur].v[i] = (signed char)e[1 + i];
                }
            }
        }
        for (j = f[4]; j < f[4] + f[5]; j++) {
            const uint8_t* e = d + p[4] + 12 * j;
            TexE* t = &key->tex[key->nTex++];
            t->step = e[0];
            t->add = e[1];
            t->s = bef(e + 4);
            t->t = bef(e + 8);
        }
        idx = 0;
        for (j = f[6]; j < f[6] + f[7]; j++) {
            const uint8_t* e = d + p[5] + 2 * j;
            idx += e[0];
            for (i = 0; i < key->nVis && key->vis[i].idx != idx; i++) {
            }
            if (i == key->nVis) {
                key->vis[key->nVis].idx = idx;
                key->vis[key->nVis++].v = 0;
            }
            key->vis[i].v += (signed char)e[1];
        }
        /* node values: runs at an index; a run of one empty chunk (a 255-step filler) is none */
        idx = 0;
        cur = -1;
        for (j = f[8]; j < f[8] + f[9]; j++) {
            const uint8_t* e = s->nodeBuf + 4 * j;
            if (e[0] == 0 && cur >= 0) {
                key->node[cur].count++;
                key->node[cur].n += (signed char)e[1];
            } else {
                idx += e[0];
                cur = key->nNode++;
                key->node[cur].idx = idx;
                key->node[cur].first = (int)j;
                key->node[cur].count = 1;
                key->node[cur].n = (signed char)e[1];
            }
        }
        for (i = j = 0; i < key->nNode; i++) {
            const uint8_t* e = s->nodeBuf + 4 * key->node[i].first;
            if (key->node[i].count == 1 && e[1] == 0 && e[2] == 0 && e[3] == 0) {
                continue;
            }
            key->node[j++] = key->node[i];
        }
        key->nNode = (int)j;
        for (i = 0; i < key->nNode; i++) {
            if (key->node[i].idx >= 0x7FFFFFFF) {
                return 0;
            }
        }
    }
    return 1;
}

/* ---------------------------------------------- the source's curves (Curves) */

typedef struct {
    int step;
    double sa, dv, sb;
} Seg;

typedef struct {
    const Model* m;
    const Src* src;
    double* times;
    double* atNode; /* per key: the node values, the visibility, the vertices, the texture slots after it */
    int* atVis;
    double* atPos;
    int* atTexB;
    double* atTexST;
    int* floats;
    int nFloats;
} Curves;

static void curvesFree(Curves* c) {
    free(c->times);
    free(c->atNode);
    free(c->atVis);
    free(c->atPos);
    free(c->atTexB);
    free(c->atTexST);
    free(c->floats);
}

static int intCmp(const void* a, const void* b) {
    const int x = *(const int*)a, y = *(const int*)b;
    return x < y ? -1 : x > y;
}

static int curvesOpen(Curves* c, const Model* m, const Src* s) {
    const int n = s->nKeys;
    int k, i, total = 0;
    memset(c, 0, sizeof(*c));
    c->m = m;
    c->src = s;
    c->times = malloc(sizeof(double) * (size_t)n);
    c->atNode = malloc(sizeof(double) * (size_t)n * (size_t)(m->nNode + 1));
    c->atVis = malloc(sizeof(int) * (size_t)n * (size_t)(m->nVis + 1));
    c->atPos = malloc(sizeof(double) * 3 * (size_t)n * (size_t)(m->nPos + 1));
    c->atTexB = malloc(sizeof(int) * (size_t)n * (size_t)(m->nTex + 1));
    c->atTexST = malloc(sizeof(double) * 2 * (size_t)n * (size_t)(m->nTex + 1));
    for (k = 0; k < n; k++) {
        total += s->keys[k].nNode;
    }
    c->floats = malloc(sizeof(int) * (size_t)(total + 1));
    if (c->times == NULL || c->atNode == NULL || c->atVis == NULL || c->atPos == NULL || c->atTexB == NULL ||
        c->atTexST == NULL || c->floats == NULL) {
        return 0;
    }
    /* at_key, kept for each key: the same additions in the same order as from the model's own values */
    for (k = 0; k < n; k++) {
        const SrcKey* key = &s->keys[k];
        double* node = c->atNode + (size_t)k * m->nNode;
        int* vis = c->atVis + (size_t)k * m->nVis;
        double* pos = c->atPos + (size_t)k * 3 * m->nPos;
        int* tb = c->atTexB + (size_t)k * m->nTex;
        double* ts = c->atTexST + (size_t)k * 2 * m->nTex;
        int slot = 0;
        c->times[k] = key->time;
        if (k == 0) {
            memcpy(node, m->node, sizeof(double) * m->nNode);
            memcpy(vis, m->vis, sizeof(int) * m->nVis);
            memcpy(pos, m->pos, sizeof(double) * 3 * m->nPos);
            memcpy(tb, m->texBind, sizeof(int) * m->nTex);
            memcpy(ts, m->texST, sizeof(double) * 2 * m->nTex);
        } else {
            memcpy(node, node - m->nNode, sizeof(double) * m->nNode);
            memcpy(vis, vis - m->nVis, sizeof(int) * m->nVis);
            memcpy(pos, pos - 3 * m->nPos, sizeof(double) * 3 * m->nPos);
            memcpy(tb, tb - m->nTex, sizeof(int) * m->nTex);
            memcpy(ts, ts - 2 * m->nTex, sizeof(double) * 2 * m->nTex);
        }
        for (i = 0; i < key->nNode; i++) {
            if (key->node[i].idx >= m->nNode) {
                return 0;
            }
            node[key->node[i].idx] += key->node[i].n / 16.0;
        }
        for (i = 0; i < key->nPos; i++) {
            int q;
            if (key->pos[i].idx >= m->nPos) {
                return 0;
            }
            for (q = 0; q < 3; q++) {
                pos[3 * key->pos[i].idx + q] += key->pos[i].v[q] / 16.0;
            }
        }
        for (i = 0; i < key->nVis; i++) {
            if (key->vis[i].idx >= m->nVis) {
                return 0;
            }
            vis[key->vis[i].idx] = ((vis[key->vis[i].idx] + key->vis[i].v + 128) & 0xFF) - 128;
        }
        for (i = 0; i < key->nTex; i++) {
            slot += key->tex[i].step;
            if (slot >= m->nTex) {
                return 0;
            }
            tb[slot] = (tb[slot] + key->tex[i].add) & 0xFF;
            ts[2 * slot] += key->tex[i].s;
            ts[2 * slot + 1] += key->tex[i].t;
        }
    }
    for (k = 0; k < n; k++) {
        for (i = 0; i < s->keys[k].nNode; i++) {
            c->floats[c->nFloats++] = s->keys[k].node[i].idx;
        }
    }
    qsort(c->floats, (size_t)c->nFloats, sizeof(int), intCmp);
    for (i = k = 0; i < c->nFloats; i++) {
        if (k == 0 || c->floats[k - 1] != c->floats[i]) {
            c->floats[k++] = c->floats[i];
        }
    }
    c->nFloats = k;
    return 1;
}

/* key k's run at node float i, or NULL */
static const NodeRun* runAt(const Curves* c, int k, int i) {
    const SrcKey* key = &c->src->keys[k];
    int j;
    for (j = 0; j < key->nNode; j++) {
        if (key->node[j].idx == i) {
            return &key->node[j];
        }
    }
    return NULL;
}

/* seg[k - 1][i] (the segment into key k): 0 none, else 1 with *g */
static int segAt(const Curves* c, int k, int i, Seg* g) {
    const NodeRun* r = runAt(c, k, i);
    int j;
    if (r == NULL) {
        return 0;
    }
    g->step = 1;
    g->sa = g->sb = 0.0;
    for (j = 0; j < r->count; j++) {
        const uint8_t* e = c->src->nodeBuf + 4 * (r->first + j);
        const int a = (signed char)e[2], b = (signed char)e[3];
        if (-89 < b && b < 89) {
            g->step = 0;
            g->sa += -89 < a && a < 89 ? tanDeg(a) : 0.0;
            g->sb += tanDeg(b);
        }
    }
    g->dv = r->n / 16.0;
    return 1;
}

/* segment(t): the key at or before t, and 1 with *u in the segment to the next, or 0 when held */
static int segment(const Curves* c, double t, int* k, double* u) {
    const int n = c->src->nKeys;
    int j = 0;
    while (j + 1 < n && c->times[j + 1] <= t) {
        j++;
    }
    *k = j;
    if (j + 1 >= n || c->times[j + 1] == c->times[j]) {
        return 0;
    }
    *u = (t - c->times[j]) / (c->times[j + 1] - c->times[j]);
    return 1;
}

static void curveNode(const Curves* c, int i, double t, int side, double* v, double* slope) {
    int k, live;
    double u = 0.0, base, dt, u2, u3;
    Seg g;
    live = segment(c, t, &k, &u);
    if (side < 0 && k > 0 && t == c->times[k] && c->times[k - 1] < t) {
        k = k - 1;
        u = 1.0;
        live = 1;
    }
    base = c->atNode[(size_t)k * c->m->nNode + i];
    if (!live || !segAt(c, k + 1, i, &g) || g.step) {
        *v = base;
        *slope = 0.0;
        return;
    }
    dt = c->times[k + 1] - c->times[k];
    u2 = u * u;
    u3 = u * u * u;
    *v = base + (u + u3 - 2 * u2) * dt * g.sa + (-2 * u3 + 3 * u2) * g.dv + (u3 - u2) * dt * g.sb;
    *slope = (1 - 4 * u + 3 * u2) * g.sa + (6 * u - 6 * u2) * g.dv / dt + (3 * u2 - 2 * u) * g.sb;
}

static int stepsInto(const Curves* c, int i, double t) {
    int k;
    Seg g;
    for (k = 1; k < c->src->nKeys; k++) {
        if (c->times[k] == t) {
            return segAt(c, k, i, &g) && g.step;
        }
    }
    return 0;
}

/* morph(t): every vertex at t (linear between keys) into out */
static void curveMorph(const Curves* c, double t, double* out) {
    int k, v;
    double u = 0.0;
    const int live = segment(c, t, &k, &u);
    const double* p0 = c->atPos + (size_t)k * 3 * c->m->nPos;
    if (!live || u == 0.0) {
        memcpy(out, p0, sizeof(double) * 3 * c->m->nPos);
        return;
    }
    {
        const double* p1 = p0 + 3 * c->m->nPos;
        for (v = 0; v < 3 * c->m->nPos; v++) {
            out[v] = p0[v] + u * (p1[v] - p0[v]);
        }
    }
}

static int curveKey(const Curves* c, double t) {
    int k;
    double u;
    segment(c, t, &k, &u);
    return k;
}

/* ---------------------------------------------------- our tracks (_track) */

typedef struct {
    const PortRecipePoint* p;
    int n;
    double* slope;
    int fi;  /* node float */
    int mul; /* 0 add, 1 multiply */
    double k;
} Track;

static void trackEval(const Track* tr, double t, int side, double* v, double* dv) {
    const PortRecipePoint* p = tr->p;
    const int n = tr->n;
    int j = 0;
    double t0, v0, t1, v1, dt, u, m0, m1, u2, u3;
    if (t < p[0].t || (side < 0 && t == p[0].t)) {
        *v = p[0].v, *dv = 0.0;
        return;
    }
    if (t > p[n - 1].t || (side > 0 && t == p[n - 1].t)) {
        *v = p[n - 1].v, *dv = 0.0;
        return;
    }
    while (j + 2 < n && (p[j + 1].t < t || (side > 0 && p[j + 1].t == t))) {
        j++;
    }
    t0 = p[j].t, v0 = p[j].v, t1 = p[j + 1].t, v1 = p[j + 1].v;
    dt = t1 - t0;
    u = (t - t0) / dt;
    if (p[j].ease == 2) {
        *v = v0, *dv = 0.0;
        return;
    }
    if (p[j].ease == 1) {
        *v = v0 + u * (v1 - v0);
        *dv = (v1 - v0) / dt;
        return;
    }
    m0 = tr->slope[j] * dt;
    m1 = tr->slope[j + 1] * dt;
    u2 = u * u;
    u3 = u * u * u;
    *v = (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * m1;
    *dv = ((6 * u2 - 6 * u) * v0 + (3 * u2 - 4 * u + 1) * m0 + (-6 * u2 + 6 * u) * v1 + (3 * u2 - 2 * u) * m1) / dt;
}

static int trackStepAt(const Track* tr, double t) {
    int j;
    for (j = 0; j + 1 < tr->n; j++) {
        if (tr->p[j].ease == 2 && tr->p[j + 1].t == t) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------- the animation we make */

typedef struct {
    int on, n, step;
    double sa, sb;
} NKey;

typedef struct {
    int nKeys, nFloats;
    double* times; /* (doubles: the bounds are found on these, before they are written) */
    int* floats;
    NKey* node;   /* nKeys x nFloats */
    int* pos;     /* nKeys x nPos x 3 deltas */
    int* vis;     /* nKeys x nVis */
    Buf tex;      /* per key: its entries (step, add, s, t) as 16 bytes, counted in texN */
    int* texN;
    int loop;
    double end;
    double bbox[6];
} Made;

static void madeFree(Made* a) {
    free(a->times);
    free(a->floats);
    free(a->node);
    free(a->pos);
    free(a->vis);
    free(a->tex.p);
    free(a->texN);
}

/* split_value */
static int splitValue(int n, int* out) {
    int k = 0;
    while (n > 127) {
        out[k++] = 127;
        n -= 127;
    }
    while (n < -128) {
        out[k++] = -128;
        n += 128;
    }
    if (n != 0 || k == 0) {
        out[k++] = n;
    }
    return k;
}

static int truncDeg(double x) {
    const double d = atan(x) * kRadToDeg;
    return (int)(d + (d >= 0 ? 1e-4 : -1e-4));
}

static int clamp88(long a) {
    return a < -88 ? -88 : a > 88 ? 88 : (int)a;
}

/* chunks(exact=True): what the bounds are found with (*values, angles a and b); the count */
#define MAX_CHUNKS 600
static int chunksExact(const NKey* k, int* v, int* a, int* b) {
    int n = splitValue(k->n, v), j;
    for (j = 0; j < n; j++) {
        if (k->step) {
            a[j] = 0, b[j] = 90;
        } else if (k->n == 0) {
            a[j] = truncDeg(k->sa), b[j] = truncDeg(k->sb);
        } else {
            a[j] = truncDeg(k->sa * v[j] / k->n);
            b[j] = truncDeg(k->sb * v[j] / k->n);
        }
    }
    return n;
}

/* _fit_angles */
static void fitAngles(double slope, const int* vals, int nv, int n, int* out) {
    int j;
    PySum s = {0};
    if (n == 0) {
        out[0] = clamp88(pyRound(atan(slope) * kRadToDeg));
        return;
    }
    for (j = 0; j < nv; j++) {
        out[j] = clamp88(pyRound(atan(slope * vals[j] / n) * kRadToDeg));
    }
    for (j = 0; j < nv - 1; j++) {
        pySumAdd(&s, tanDeg(out[j]));
    }
    out[nv - 1] = clamp88(pyRound(atan(slope - pySumEnd(&s)) * kRadToDeg));
}

/* --------------------------------------------- the bounds (char_motions.anim_bbox) */

typedef double Mat[4][4];

static void matIdent(Mat m) {
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m[i][j] = i == j ? 1.0 : 0.0;
        }
    }
}

/* _mul: each entry sum() of four products */
static void matMul(Mat a, Mat b, Mat out) {
    Mat r;
    int i, j, k;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            PySum s = {0};
            for (k = 0; k < 4; k++) {
                pySumAdd(&s, a[i][k] * b[k][j]);
            }
            r[i][j] = pySumEnd(&s);
        }
    }
    memcpy(out, r, sizeof(Mat));
}

static void matTrans(Mat m, double x, double y, double z) {
    matIdent(m);
    m[0][3] = x, m[1][3] = y, m[2][3] = z;
}

static void matScale(Mat m, double x, double y, double z) {
    matIdent(m);
    m[0][0] = x, m[1][1] = y, m[2][2] = z;
}

static void matRot(Mat m, int axis, double deg) {
    const double r = deg * kDegToRad, c = cos(r), s = sin(r);
    matIdent(m);
    if (axis == 0) {
        m[1][1] = c, m[1][2] = -s, m[2][1] = s, m[2][2] = c;
    } else if (axis == 1) {
        m[0][0] = c, m[0][2] = s, m[2][0] = -s, m[2][2] = c;
    } else {
        m[0][0] = c, m[0][1] = -s, m[1][0] = s, m[1][1] = c;
    }
}

static void rotMatrix(double rx, double ry, double rz, Mat out) {
    Mat a, b, c;
    matRot(a, 2, rz);
    matRot(b, 1, ry);
    matMul(a, b, a);
    matRot(c, 0, rx);
    matMul(a, c, out);
}

static void transformMatrix(const double* n, Mat out) {
    Mat s, r, t, u;
    matScale(s, n[3], n[4], n[5]);
    matTrans(t, n[21], n[22], n[23]);
    matTrans(u, n[15], n[16], n[17]);
    matMul(t, u, t);
    matMul(t, s, t);
    matTrans(u, -n[15], -n[16], -n[17]);
    matMul(t, u, s);
    rotMatrix(2 * n[6], 2 * n[7], 2 * n[8], r);
    matTrans(t, n[18], n[19], n[20]);
    matTrans(u, n[12], n[13], n[14]);
    matMul(t, u, t);
    matMul(t, r, t);
    matTrans(u, -n[12], -n[13], -n[14]);
    matMul(t, u, r);
    matTrans(t, n[0], n[1], n[2]);
    matMul(t, r, t);
    matMul(t, s, out);
}

static void jointMatrix(const double* n, const double* ps, Mat out) {
    Mat inv, r1, r2, t, s;
    if (ps[0] != 0.0 && ps[1] != 0.0 && ps[2] != 0.0) {
        matScale(inv, 1 / ps[0], 1 / ps[1], 1 / ps[2]);
    } else {
        matIdent(inv);
    }
    rotMatrix(n[9], n[10], n[11], r2);
    rotMatrix(2 * n[6], 2 * n[7], 2 * n[8], r1);
    matMul(r2, r1, r2);
    matTrans(t, n[0], n[1], n[2]);
    matMul(t, inv, t);
    matMul(t, r2, t);
    matScale(s, n[3], n[4], n[5]);
    matMul(t, s, out);
}

typedef struct {
    const Model* m;
    const double* node;
    const int* vis;
    const double* pos;
    double lo[3], hi[3];
    int depth;
} Bounds;

static void boundsGroup(Bounds* b, int g, Mat world) {
    const Model* m = b->m;
    const int sh = groupInt(m, g, 0x48);
    int v;
    if (sh < 0) {
        return;
    }
    {
        const uint8_t* s = m->d + m->ptrs[T_SHAPES] + 0xA8 * (uint32_t)sh;
        const int p0 = (int)be32(s + 0x40), pn = (int)be32(s + 0x44);
        for (v = p0; v < p0 + pn; v++) {
            const double x = b->pos[3 * v], y = b->pos[3 * v + 1], z = b->pos[3 * v + 2];
            int r;
            for (r = 0; r < 3; r++) {
                const double w = world[r][0] * x + world[r][1] * y + world[r][2] * z + world[r][3];
                b->lo[r] = w < b->lo[r] ? w : b->lo[r];
                b->hi[r] = w > b->hi[r] ? w : b->hi[r];
            }
        }
    }
}

/* world_matrices' walk, each drawn group's points taken in as it is reached */
static void boundsWalk(Bounds* b, int parent, int g, Mat top) {
    const Model* m = b->m;
    while (g != -1) {
        if (g < 0 || g >= m->nGroups || ++b->depth > 100000) {
            return;
        }
        if (b->vis[groupInt(m, g, 0x4C)] != 0) {
            const double* nv = b->node + groupInt(m, g, 0x50);
            Mat local, world;
            if (groupInt(m, g, 0x54) != 0) {
                double ps[3] = {1.0, 1.0, 1.0};
                if (parent != -1 && groupInt(m, parent, 0x54) != 0) {
                    const double* pn = b->node + groupInt(m, parent, 0x50);
                    ps[0] = pn[3], ps[1] = pn[4], ps[2] = pn[5];
                }
                jointMatrix(nv, ps, local);
            } else {
                transformMatrix(nv, local);
            }
            matMul(top, local, world);
            boundsGroup(b, g, world);
            boundsWalk(b, g, groupInt(m, g, 0x44), world);
        }
        g = groupInt(m, g, 0x40);
    }
}

/* at_frame of the animation we made, into node, vis and pos */
static void madeAtFrame(const Model* m, const Made* a, double frame, double* node, int* vis, double* pos) {
    const int n = a->nKeys;
    int i = 0, k, nk, j, q;
    double start, length, t;
    while (i < n && a->times[i] <= frame) {
        i++;
    }
    k = i > 0 ? i - 1 : 0; /* (the first key is at 0, where the bounds start) */
    nk = k + 1;
    if (!a->loop && nk >= n) {
        nk = k;
    }
    start = a->times[k];
    if (a->loop && nk >= n) {
        nk = 0;
        length = a->end - a->times[k];
    } else {
        length = a->times[nk] - start;
    }
    t = length == 0 ? 0.0 : (frame - start) / length;
    memcpy(node, m->node, sizeof(double) * m->nNode);
    memcpy(vis, m->vis, sizeof(int) * m->nVis);
    memcpy(pos, m->pos, sizeof(double) * 3 * m->nPos);
    for (j = 0; j <= k; j++) {
        for (i = 0; i < a->nFloats; i++) {
            const NKey* e = &a->node[(size_t)j * a->nFloats + i];
            if (e->on) {
                node[a->floats[i]] += e->n / 16.0;
            }
        }
        /* (every vertex a key moves: its three, a zero among them added as Python does) */
        for (i = 0; i < m->nPos; i++) {
            const int* d = &a->pos[((size_t)j * m->nPos + i) * 3];
            if (d[0] || d[1] || d[2]) {
                for (q = 0; q < 3; q++) {
                    pos[3 * i + q] += d[q] / 16.0;
                }
            }
        }
        for (i = 0; i < m->nVis; i++) {
            const int d = a->vis[(size_t)j * m->nVis + i];
            if (d != 0) {
                vis[i] = ((vis[i] + d + 128) & 0xFF) - 128;
            }
        }
    }
    if (k != nk && k <= nk) {
        const double dt = a->times[nk] - a->times[k];
        const double t2 = t * t, t3 = t * t * t;
        const double h01 = -2 * t3 + 3 * t2;
        const double h10 = t + (t3 - 2 * t2);
        const double h11 = t3 - t2;
        for (i = 0; i < m->nPos; i++) {
            const int* d = &a->pos[((size_t)nk * m->nPos + i) * 3];
            if (d[0] || d[1] || d[2]) {
                for (q = 0; q < 3; q++) {
                    pos[3 * i + q] += d[q] / 16.0 * t;
                }
            }
        }
        for (i = 0; i < a->nFloats; i++) {
            const NKey* e = &a->node[(size_t)nk * a->nFloats + i];
            int v[MAX_CHUNKS], ca[MAX_CHUNKS], cb[MAX_CHUNKS], c, nc;
            if (!e->on) {
                continue;
            }
            nc = chunksExact(e, v, ca, cb);
            for (c = 0; c < nc; c++) {
                if (-89 < cb[c] && cb[c] < 89) {
                    const double ta = -89 < ca[c] && ca[c] < 89 ? tanDeg(ca[c]) : 0.0;
                    node[a->floats[i]] += h10 * (dt * ta) + h01 * (v[c] / 16.0) + h11 * (dt * tanDeg(cb[c]));
                }
            }
        }
    }
}

static int madeBounds(const Model* m, Made* a) {
    double* node = malloc(sizeof(double) * (size_t)m->nNode);
    int* vis = malloc(sizeof(int) * (size_t)(m->nVis + 1));
    double* pos = malloc(sizeof(double) * 3 * (size_t)(m->nPos + 1));
    Bounds b;
    double f = 0.0; /* (the start: 0) */
    int r;
    if (node == NULL || vis == NULL || pos == NULL) {
        free(node);
        free(vis);
        free(pos);
        return 0;
    }
    memset(&b, 0, sizeof(b));
    b.m = m;
    for (r = 0; r < 3; r++) {
        b.lo[r] = 1e9;
        b.hi[r] = -1e9;
    }
    while (f <= a->end + 1e-9) {
        Mat top;
        madeAtFrame(m, a, f, node, vis, pos);
        b.node = node, b.vis = vis, b.pos = pos, b.depth = 0;
        matIdent(top);
        boundsWalk(&b, -1, m->nGroups - 1, top);
        f += 1.0;
    }
    for (r = 0; r < 3; r++) {
        a->bbox[r] = b.lo[r] - 0.5;
        a->bbox[3 + r] = b.hi[r] + 0.5;
    }
    free(node);
    free(vis);
    free(pos);
    return 1;
}

/* ------------------------------------------------------------ make() */

#define FAIL(...) (snprintf(why, whySize, __VA_ARGS__), 0)

/* the disc's animation of this name (its first): its AnimData and size, 0 if none */
static int findAnim(const Model* m, const char* name, const uint8_t** data, uint32_t* size) {
    uint32_t j;
    const uint32_t tab = m->ptrs[24], n = m->counts[C_ANIM];
    for (j = 0; j < n; j++) {
        const uint8_t* e = m->d + tab + 0x40 * j;
        if (strncmp((const char*)e, name, 0x3C) == 0 && strlen(name) < 0x3C) {
            const uint32_t off = be32(e + 0x3C);
            if (off < tab + 0x40 * n || (uint64_t)off + 4 > m->size || be32(m->d + off) > m->size - off) {
                return 0; /* (no AnimData of its own: no keys) */
            }
            *data = m->d + off;
            *size = be32(m->d + off);
            return 1;
        }
    }
    return 0;
}

/* the group (by its name, "|" aside: the last of the name, as a dict keeps) */
static int findGroup(const Model* m, const char* name) {
    int g, found = -1;
    while (*name == '|') {
        name++;
    }
    for (g = 0; g < m->nGroups; g++) {
        const char* gn = (const char*)groupRec(m, g);
        size_t len = strnlen(gn, 64);
        while (len > 0 && *gn == '|') {
            gn++, len--;
        }
        if (strlen(name) == len && strncmp(gn, name, len) == 0) {
            found = g;
        }
    }
    return found;
}

/* an animation's tracks at most (a PM64 partner's row poses many joints: pm64_poses.py checks it) */
#define RECIPE_TRACKS 256

typedef struct {
    double tau, sigma;
} Tau;

static int tauCmp(const void* x, const void* y) {
    const double a = ((const Tau*)x)->tau, b = ((const Tau*)y)->tau;
    return a < b ? -1 : a > b;
}

static int tauPut(Tau* taus, int* n, int cap, double tau, double sigma) {
    int i;
    tau = pyRound4(tau);
    for (i = 0; i < *n; i++) {
        if (taus[i].tau == tau) {
            return 1;
        }
    }
    if (*n >= cap) {
        return 0;
    }
    taus[*n].tau = tau;
    taus[(*n)++].sigma = sigma;
    return 1;
}

static uint8_t* (*sLoader)(const char* model, uint32_t* size);

void portAnimRecipeSetLoader(uint8_t* (*load)(const char* model, uint32_t* size)) {
    sLoader = load;
}

static int makeAnim(const Model* m, const PortRecipe* r, const PortRecipeAnim* spec, Made* out, char* why,
                    size_t whySize) {
    static const struct {
        int f, mul;
        double k;
    } kChan[9] = {{0, 0, 1.0}, {1, 0, 1.0}, {2, 0, 1.0}, {3, 1, 1.0}, {4, 1, 1.0},
                  {5, 1, 1.0}, {6, 0, 0.5}, {7, 0, 0.5}, {8, 0, 0.5}};
    const uint8_t* data;
    uint32_t dsize;
    Src src;
    Curves cur;
    Track tracks[RECIPE_TRACKS];
    int nTracks = 0, i, j, turns, p, nTaus = 0, cap, ok = 0;
    double a, b, end, span, scale, inv;
    int loop;
    Tau* taus = NULL;
    Model fm;
    uint8_t* fromData = NULL;
    uint32_t fromSize = 0;
    double* sig = NULL;
    double* vals = NULL;
    double* right = NULL;
    double* left = NULL;
    int* steps = NULL;
    double* morph = NULL;

    memset(out, 0, sizeof(*out));
    memset(&cur, 0, sizeof(cur));
    memset(&fm, 0, sizeof(fm));
    if (spec->fromModel != NULL && spec->fromModel[0] != '\0') {
        /* (another model's animation of the same rig: its keys on this one's rest, as the disc's are) */
        if (sLoader == NULL || (fromData = sLoader(spec->fromModel, &fromSize)) == NULL) {
            return FAIL("%s: the model %s couldn't be read", spec->name, spec->fromModel);
        }
        if (!modelOpen(&fm, fromData, fromSize) || !findAnim(&fm, spec->from, &data, &dsize) ||
            !srcParse(&src, data, dsize)) {
            srcFree(&src);
            modelClose(&fm);
            free(fromData);
            return FAIL("%s: %s has no %s with keys", spec->name, spec->fromModel, spec->from);
        }
    } else if (!findAnim(m, spec->from, &data, &dsize) || !srcParse(&src, data, dsize)) {
        srcFree(&src);
        return FAIL("%s: the model has no %s with keys", spec->name, spec->from);
    }
    if (!curvesOpen(&cur, m, &src)) {
        FAIL("%s: %s doesn't fit the model", spec->name, spec->from);
        goto done;
    }
    a = spec->hasRange ? spec->a : src.start;
    b = spec->hasRange ? spec->b : src.end;
    if (b < a) {
        FAIL("%s: its range ends before it starts", spec->name);
        goto done;
    }
    end = spec->hasEnd ? spec->end : b - a;
    loop = spec->hasLoop ? spec->loop : src.loop;
    span = b - a;
    if (span > 0 && end > 0 && (loop || spec->turns > 0)) {
        long t = pyRound(end / span);
        turns = spec->turns > 0 ? spec->turns : (int)(t > 1 ? t : 1);
    } else {
        turns = 1;
    }
    if (turns > 1000) {
        FAIL("%s: too many turns", spec->name);
        goto done;
    }
    scale = span > 0 ? end / (span * turns) : 0.0;
    /* our tracks */
    for (i = 0; i < spec->nTracks; i++) {
        const PortRecipeTrack* t = &r->tracks[spec->firstTrack + i];
        const int g = findGroup(m, t->group);
        Track* tr = &tracks[nTracks];
        int q;
        if (g < 0) {
            FAIL("%s: the model has no group %s", spec->name, t->group);
            goto done;
        }
        if (nTracks >= RECIPE_TRACKS || t->channel < 0 || t->channel > 8 || t->count < 1) {
            FAIL("%s: a track the maker can't take", spec->name);
            goto done;
        }
        tr->p = &r->points[t->first];
        tr->n = t->count;
        tr->fi = groupInt(m, g, 0x50) + kChan[t->channel].f;
        tr->mul = kChan[t->channel].mul;
        tr->k = kChan[t->channel].k;
        tr->slope = malloc(sizeof(double) * (size_t)tr->n);
        if (tr->slope == NULL) {
            FAIL("out of memory");
            goto done;
        }
        nTracks++;
        for (q = 0; q < tr->n; q++) {
            if (q > 0 && q < tr->n - 1 && tr->p[q].ease == 0) {
                tr->slope[q] = (tr->p[q + 1].v - tr->p[q - 1].v) / (tr->p[q + 1].t - tr->p[q - 1].t);
            } else {
                tr->slope[q] = 0.0;
            }
        }
    }
    /* the new keys: each pass's source keys and its ends, then our tracks' */
    cap = (src.nKeys + 2) * turns + 2;
    for (i = 0; i < nTracks; i++) {
        cap += tracks[i].n;
    }
    taus = malloc(sizeof(Tau) * (size_t)cap);
    if (taus == NULL) {
        FAIL("out of memory");
        goto done;
    }
    for (p = 0; p < turns; p++) {
        tauPut(taus, &nTaus, cap, p * span * scale, a);
        for (j = 0; j < src.nKeys; j++) {
            const double t = cur.times[j];
            if (a < t && t < b) {
                tauPut(taus, &nTaus, cap, (p * span + t - a) * scale, t);
            }
        }
    }
    tauPut(taus, &nTaus, cap, turns * span * scale, b);
    for (i = 0; i < nTracks; i++) {
        for (j = 0; j < tracks[i].n; j++) {
            const double t = tracks[i].p[j].t;
            if (0 <= t && t <= end) {
                if (span > 0 && scale > 0) {
                    int pp = (int)(t / (span * scale));
                    pp = pp < turns - 1 ? pp : turns - 1;
                    tauPut(taus, &nTaus, cap, t, a + (t - pp * span * scale) / scale);
                } else {
                    tauPut(taus, &nTaus, cap, t, a);
                }
            }
        }
    }
    qsort(taus, (size_t)nTaus, sizeof(Tau), tauCmp);
    sig = malloc(sizeof(double) * (size_t)nTaus);
    if (sig == NULL) {
        FAIL("out of memory");
        goto done;
    }
    for (j = 0; j < nTaus; j++) {
        sig[j] = taus[j].sigma;
    }
    /* a pass's end is the next pass's start: there, the next pass's (the loop joined at the turn) */
    for (j = 1; j < nTaus - 1; j++) {
        if (span > 0) {
            double x;
            if (span * scale == 0.0) {
                FAIL("%s: a zero-length pass", spec->name);
                goto done;
            }
            x = taus[j].tau / (span * scale);
            if (fabs(x - (double)pyRound(x)) < 1e-9 && scale > 0) {
                sig[j] = a;
            }
        }
    }
    /* the node floats: the source's and our tracks' */
    out->nKeys = nTaus;
    out->loop = loop;
    out->end = end;
    out->times = malloc(sizeof(double) * (size_t)nTaus);
    out->floats = malloc(sizeof(int) * (size_t)(cur.nFloats + nTracks + 1));
    if (out->times == NULL || out->floats == NULL) {
        FAIL("out of memory");
        goto done;
    }
    for (j = 0; j < nTaus; j++) {
        out->times[j] = taus[j].tau;
    }
    for (i = 0; i < cur.nFloats; i++) {
        out->floats[out->nFloats++] = cur.floats[i];
    }
    for (i = 0; i < nTracks; i++) {
        out->floats[out->nFloats++] = tracks[i].fi;
    }
    qsort(out->floats, (size_t)out->nFloats, sizeof(int), intCmp);
    for (i = j = 0; i < out->nFloats; i++) {
        if (j == 0 || out->floats[j - 1] != out->floats[i]) {
            out->floats[j++] = out->floats[i];
        }
    }
    out->nFloats = j;
    out->node = calloc((size_t)nTaus * (size_t)(out->nFloats + 1), sizeof(NKey));
    out->pos = calloc((size_t)nTaus * (size_t)(m->nPos + 1) * 3, sizeof(int));
    out->vis = calloc((size_t)nTaus * (size_t)(m->nVis + 1), sizeof(int));
    out->texN = calloc((size_t)nTaus, sizeof(int));
    vals = malloc(sizeof(double) * (size_t)nTaus);
    right = malloc(sizeof(double) * (size_t)nTaus);
    left = malloc(sizeof(double) * (size_t)nTaus);
    steps = malloc(sizeof(int) * (size_t)nTaus);
    morph = malloc(sizeof(double) * 3 * (size_t)(m->nPos + 1));
    if (out->node == NULL || out->pos == NULL || out->vis == NULL || out->texN == NULL || vals == NULL ||
        right == NULL || left == NULL || steps == NULL || morph == NULL) {
        FAIL("out of memory");
        goto done;
    }
    inv = scale > 0 ? 1.0 / scale : 0.0;
    for (i = 0; i < out->nFloats; i++) {
        const int fi = out->floats[i];
        long qPrev = 0;
        for (j = 0; j < nTaus; j++) {
            const double tau = taus[j].tau, s = sig[j];
            double v, sr, sl, dummy;
            int t;
            curveNode(&cur, fi, s, +1, &v, &sr);
            curveNode(&cur, fi, s, -1, &dummy, &sl);
            if (j > 0 && sig[j] <= sig[j - 1]) {
                curveNode(&cur, fi, b, -1, &dummy, &sl); /* (joined at a turn: coming in as the part ends) */
            }
            sr = sr * inv;
            sl = sl * inv;
            for (t = 0; t < nTracks; t++) {
                double ov, osr, osl, odummy;
                if (tracks[t].fi != fi) {
                    continue;
                }
                trackEval(&tracks[t], tau, +1, &ov, &osr);
                trackEval(&tracks[t], tau, -1, &odummy, &osl);
                if (!tracks[t].mul) {
                    const double k = tracks[t].k;
                    v = v + ov * k;
                    sr = sr + osr * k;
                    sl = sl + osl * k;
                } else {
                    const double v0 = v;
                    v = v0 * ov;
                    sr = sr * ov + v0 * osr;
                    sl = sl * ov + v0 * osl;
                }
            }
            vals[j] = v;
            right[j] = sr;
            left[j] = sl;
            steps[j] = j > 0 && sig[j] > sig[j - 1] ? stepsInto(&cur, fi, s) : 0;
            for (t = 0; t < nTracks && !steps[j]; t++) {
                if (tracks[t].fi == fi && trackStepAt(&tracks[t], tau)) {
                    steps[j] = 1;
                }
            }
        }
        for (j = 0; j < nTaus; j++) {
            const long q = pyRound((vals[j] - m->node[fi]) * 16);
            const long n = q - qPrev;
            NKey* e = &out->node[(size_t)j * out->nFloats + i];
            qPrev = q;
            if (n > 100000 || n < -100000) {
                FAIL("%s: a value out of range", spec->name);
                goto done;
            }
            if (j == 0) {
                if (n != 0) {
                    e->on = 1, e->n = (int)n;
                }
                continue;
            }
            if (steps[j]) {
                if (n != 0) {
                    e->on = 1, e->n = (int)n, e->step = 1;
                }
                continue;
            }
            if (n == 0 && fabs(right[j - 1]) < 1e-6 && fabs(left[j]) < 1e-6) {
                continue;
            }
            e->on = 1, e->n = (int)n, e->sa = right[j - 1], e->sb = left[j];
        }
    }
    /* vertex morphs, visibility and texture slots: the source's at each key's time */
    {
        int* prevPos = calloc((size_t)(m->nPos + 1) * 3, sizeof(int));
        int* prevVis = malloc(sizeof(int) * (size_t)(m->nVis + 1));
        int* prevTB = malloc(sizeof(int) * (size_t)(m->nTex + 1));
        double* prevTS = malloc(sizeof(double) * 2 * (size_t)(m->nTex + 1));
        if (prevPos == NULL || prevVis == NULL || prevTB == NULL || prevTS == NULL) {
            free(prevPos), free(prevVis), free(prevTB), free(prevTS);
            FAIL("out of memory");
            goto done;
        }
        memcpy(prevVis, m->vis, sizeof(int) * m->nVis);
        memcpy(prevTB, m->texBind, sizeof(int) * m->nTex);
        memcpy(prevTS, m->texST, sizeof(double) * 2 * m->nTex);
        for (j = 0; j < nTaus; j++) {
            const int k = curveKey(&cur, sig[j]);
            const int* vis = cur.atVis + (size_t)k * m->nVis;
            const int* tb = cur.atTexB + (size_t)k * m->nTex;
            const double* ts = cur.atTexST + (size_t)k * 2 * m->nTex;
            int v, last = 0;
            curveMorph(&cur, sig[j], morph);
            for (v = 0; v < 3 * m->nPos; v++) {
                const long q = pyRound((morph[v] - m->pos[v]) * 16);
                out->pos[(size_t)j * 3 * m->nPos + v] = (int)(q - prevPos[v]);
                prevPos[v] = (int)q;
            }
            for (v = 0; v < m->nVis; v++) {
                if (vis[v] != prevVis[v]) {
                    out->vis[(size_t)j * m->nVis + v] = ((vis[v] - prevVis[v] + 128) & 0xFF) - 128;
                }
            }
            for (v = 0; v < m->nTex; v++) {
                if (prevTB[v] != tb[v] || prevTS[2 * v] != ts[2 * v] || prevTS[2 * v + 1] != ts[2 * v + 1]) {
                    uint8_t* e = bufAdd(&out->tex, 16);
                    if (e == NULL) {
                        break;
                    }
                    put32(e, (uint32_t)(v - last));
                    put32(e + 4, (uint32_t)((tb[v] - prevTB[v]) & 0xFF));
                    putf(e + 8, ts[2 * v] - prevTS[2 * v]);
                    putf(e + 12, ts[2 * v + 1] - prevTS[2 * v + 1]);
                    out->texN[j]++;
                    last = v;
                }
            }
            memcpy(prevVis, vis, sizeof(int) * m->nVis);
            memcpy(prevTB, tb, sizeof(int) * m->nTex);
            memcpy(prevTS, ts, sizeof(double) * 2 * m->nTex);
        }
        free(prevPos), free(prevVis), free(prevTB), free(prevTS);
        if (out->tex.bad) {
            FAIL("out of memory");
            goto done;
        }
    }
    if (!madeBounds(m, out)) {
        FAIL("out of memory");
        goto done;
    }
    ok = 1;
done:
    for (i = 0; i < nTracks; i++) {
        free(tracks[i].slope);
    }
    free(taus);
    free(sig);
    free(vals);
    free(right);
    free(left);
    free(steps);
    free(morph);
    curvesFree(&cur);
    srcFree(&src);
    if (fromData != NULL) {
        modelClose(&fm);
        free(fromData);
    }
    return ok;
}

/* ------------------------------------------------------ Anim.build() */

static void encodeVec(Buf* b, const int* d, int count) {
    int i, last = 0;
    for (i = 0; i < count; i++) {
        int v[3] = {d[3 * i], d[3 * i + 1], d[3 * i + 2]}, first = 1;
        if (!v[0] && !v[1] && !v[2]) {
            continue;
        }
        /* split_vec: 127 / -128 chunks while any part is past them, then the rest */
        for (;;) {
            int ch[3], q, more = 0, step;
            for (q = 0; q < 3; q++) {
                more |= v[q] > 127 || v[q] < -128;
            }
            if (more) {
                for (q = 0; q < 3; q++) {
                    ch[q] = v[q] > 127 ? 127 : v[q] < -128 ? -128 : 0;
                    v[q] -= ch[q];
                }
            } else if (v[0] || v[1] || v[2] || first) {
                ch[0] = v[0], ch[1] = v[1], ch[2] = v[2];
                v[0] = v[1] = v[2] = 0;
            } else {
                break;
            }
            step = first ? i - last : 0;
            while (step > 255) {
                bufBytes(b, 255, 0, 0, 0);
                step -= 255;
                last += 255;
            }
            bufBytes(b, step, ch[0], ch[1], ch[2]);
            last = i;
            if (!more) {
                break;
            }
            first = 0;
        }
    }
}

static void encodeVis(Buf* b, const int* d, int count) {
    int i, last = 0;
    for (i = 0; i < count; i++) {
        int vals[64], n, c, step;
        uint8_t* p;
        if (d[i] == 0) {
            continue;
        }
        step = i - last;
        while (step > 255) {
            p = bufAdd(b, 2);
            if (p != NULL) {
                p[0] = 255, p[1] = 0;
            }
            step -= 255;
            last += 255;
        }
        n = splitValue(d[i], vals);
        for (c = 0; c < n; c++) {
            p = bufAdd(b, 2);
            if (p != NULL) {
                p[0] = (uint8_t)(c == 0 ? step : 0), p[1] = (uint8_t)vals[c];
            }
        }
        last = i;
    }
}

static void encodeNodes(Buf* b, const Made* a, int key) {
    int i, last = 0;
    for (i = 0; i < a->nFloats; i++) {
        const NKey* e = &a->node[(size_t)key * a->nFloats + i];
        int v[MAX_CHUNKS], ca[MAX_CHUNKS], cb[MAX_CHUNKS], n, c, step;
        if (!e->on) {
            continue;
        }
        n = splitValue(e->n, v);
        if (e->step) {
            for (c = 0; c < n; c++) {
                ca[c] = 0, cb[c] = 90;
            }
        } else {
            fitAngles(e->sa, v, n, e->n, ca);
            fitAngles(e->sb, v, n, e->n, cb);
        }
        if (n == 1 && v[0] == 0 && ca[0] == 0 && cb[0] == 0) {
            continue;
        }
        step = a->floats[i] - last;
        while (step > 255) {
            bufBytes(b, 255, 0, 0, 0);
            step -= 255;
            last += 255;
        }
        for (c = 0; c < n; c++) {
            bufBytes(b, c == 0 ? step : 0, v[c], ca[c], cb[c]);
        }
        last = a->floats[i];
    }
}

static int buildAnim(const Model* m, const Made* a, Buf* out) {
    Buf pos = {0}, tex = {0}, vis = {0}, node = {0}, keys = {0};
    uint32_t offs[8], counts[8];
    size_t start = out->n, texAt = 0;
    int j, i;
    for (j = 0; j < a->nKeys; j++) {
        uint8_t* r = bufAdd(&keys, 0x2C);
        const size_t p0 = pos.n, t0 = tex.n, v0 = vis.n, n0 = node.n;
        encodeVec(&pos, a->pos + (size_t)j * 3 * m->nPos, m->nPos);
        for (i = 0; i < a->texN[j]; i++, texAt += 16) {
            const uint8_t* e = a->tex.p + texAt;
            uint8_t* t = bufAdd(&tex, 12);
            if (t != NULL) {
                t[0] = (uint8_t)be32(e);
                t[1] = (uint8_t)be32(e + 4);
                memcpy(t + 4, e + 8, 8);
            }
        }
        encodeVis(&vis, a->vis + (size_t)j * m->nVis, m->nVis);
        encodeNodes(&node, a, j);
        if (r == NULL) {
            break;
        }
        r = keys.p + keys.n - 0x2C;
        putf(r, a->times[j]);
        put32(r + 4, (uint32_t)(p0 / 4));
        put32(r + 8, (uint32_t)((pos.n - p0) / 4));
        put32(r + 12, 0);
        put32(r + 16, 0);
        put32(r + 20, (uint32_t)(t0 / 12));
        put32(r + 24, (uint32_t)((tex.n - t0) / 12));
        put32(r + 28, (uint32_t)(v0 / 2));
        put32(r + 32, (uint32_t)((vis.n - v0) / 2));
        put32(r + 36, (uint32_t)(n0 / 4));
        put32(r + 40, (uint32_t)((node.n - n0) / 4));
    }
    if (pos.bad || tex.bad || vis.bad || node.bad || keys.bad) {
        free(pos.p), free(tex.p), free(vis.p), free(node.p), free(keys.p);
        return 0;
    }
    bufAdd(out, ANIM_HEADER);
    offs[0] = (uint32_t)(out->n - start);
    {
        uint8_t* l = bufAdd(out, 12);
        if (l != NULL) {
            put32(l, a->loop ? 1 : 0);
            putf(l + 4, 0.0);
            putf(l + 8, a->end);
        }
    }
    offs[1] = (uint32_t)(out->n - start);
    {
        uint8_t* k = bufAdd(out, keys.n);
        if (k != NULL && keys.n > 0) {
            memcpy(k, keys.p, keys.n);
        }
    }
    {
        const Buf* bufs[5] = {&pos, NULL, &tex, &vis, &node};
        for (i = 0; i < 5; i++) {
            bufAlign(out, 4); /* (aligned from the animation's start: it starts 4-aligned) */
            offs[2 + i] = (uint32_t)(out->n - start);
            if (bufs[i] != NULL && bufs[i]->n > 0) {
                uint8_t* p = bufAdd(out, bufs[i]->n);
                if (p != NULL) {
                    memcpy(p, bufs[i]->p, bufs[i]->n);
                }
            }
        }
    }
    bufAlign(out, 4);
    offs[7] = (uint32_t)(out->n - start);
    bufAdd(out, 8); /* value keys: (0.0, 0) */
    counts[0] = 1;
    counts[1] = (uint32_t)a->nKeys;
    counts[2] = (uint32_t)(pos.n / 4);
    counts[3] = 0;
    counts[4] = (uint32_t)(tex.n / 12);
    counts[5] = (uint32_t)(vis.n / 2);
    counts[6] = (uint32_t)(node.n / 4);
    counts[7] = 1;
    free(pos.p), free(tex.p), free(vis.p), free(node.p), free(keys.p);
    if (out->bad) {
        return 0;
    }
    {
        uint8_t* h = out->p + start;
        put32(h, (uint32_t)(out->n - start));
        for (i = 0; i < 8; i++) {
            put32(h + 4 + 4 * i, counts[i]);
            put32(h + 36 + 4 * i, offs[i]);
        }
        for (i = 0; i < 6; i++) {
            putf(h + 68 + 4 * i, a->bbox[i]);
        }
    }
    return 1;
}

/* ------------------------------------------- apply() and PoseFile.build() */

int portAnimRecipeHas(const PortRecipe* recipe, const char* anim) {
    int i;
    for (i = 0; recipe != NULL && anim != NULL && i < recipe->nAnims; i++) {
        if (!strcmp(recipe->anims[i].name, anim)) {
            return 1;
        }
    }
    return 0;
}

int portAnimRecipeMake(const uint8_t* model, uint32_t size, const PortRecipe* r, const char* name, uint8_t** out,
                       uint32_t* outSize, char* why, size_t whySize) {
    Model m;
    Buf o = {0};
    uint32_t nDisc, j, tab, *at = NULL, *discOff = NULL;
    int i, nOut, ok = 0, *fromRecipe = NULL;
    const uint8_t** entry = NULL;
    *out = NULL;
    *outSize = 0;
    if (!modelOpen(&m, model, size)) {
        modelClose(&m);
        return FAIL("the model's tables don't fit it");
    }
    nDisc = m.counts[C_ANIM];
    tab = m.ptrs[24];
    /* the order: the disc's (one the recipe makes replaced where it is), then the recipe's others in its order */
    entry = calloc((size_t)nDisc + (size_t)r->nAnims + 1, sizeof(*entry));
    fromRecipe = calloc((size_t)nDisc + (size_t)r->nAnims + 1, sizeof(int));
    at = calloc((size_t)nDisc + (size_t)r->nAnims + 1, sizeof(uint32_t));
    discOff = calloc((size_t)nDisc + (size_t)r->nAnims + 1, sizeof(uint32_t));
    if (entry == NULL || fromRecipe == NULL || at == NULL || discOff == NULL) {
        FAIL("out of memory");
        goto done;
    }
    nOut = 0;
    for (j = 0; j < nDisc; j++) {
        entry[nOut] = m.d + tab + 0x40 * j;
        fromRecipe[nOut++] = -1;
    }
    for (i = 0; i < r->nAnims; i++) {
        int k;
        for (k = 0; k < (int)nDisc; k++) {
            if (fromRecipe[k] < 0 && strncmp((const char*)entry[k], r->anims[i].name, 0x3C) == 0) {
                int seen, q;
                /* (only the first of the name: a recipe's name made once) */
                for (seen = q = 0; q < i; q++) {
                    seen |= !strcmp(r->anims[q].name, r->anims[i].name);
                }
                if (!seen) {
                    fromRecipe[k] = i;
                    break;
                }
            }
        }
        if (k == (int)nDisc) {
            entry[nOut] = NULL;
            fromRecipe[nOut++] = i;
        }
    }
    /* the model's part as the disc's, then the table */
    if (tab < HEADER_SIZE || bufAdd(&o, tab) == NULL) {
        FAIL("out of memory");
        goto done;
    }
    memcpy(o.p, model, tab);
    if (o.bad || bufAdd(&o, 0x40 * (size_t)nOut) == NULL) {
        FAIL("out of memory");
        goto done;
    }
    bufAlign(&o, 4);
    put32(o.p, (uint32_t)o.n);
    for (i = 0; i < nOut; i++) {
        uint8_t* rec;
        const char* an = fromRecipe[i] >= 0 ? r->anims[fromRecipe[i]].name : (const char*)entry[i];
        const int ours = portAnimRecipeHas(r, an); /* (every animation of a name the recipe makes: no tail, never shared) */
        if (fromRecipe[i] >= 0) {
            Made a;
            if (strlen(an) >= 0x3C) {
                FAIL("%s: its name is too long", an);
                goto done;
            }
            if (!makeAnim(&m, r, &r->anims[fromRecipe[i]], &a, why, whySize)) {
                madeFree(&a);
                goto done;
            }
            bufAlign(&o, 4);
            at[i] = (uint32_t)o.n;
            if (!buildAnim(&m, &a, &o)) {
                madeFree(&a);
                FAIL("out of memory");
                goto done;
            }
            madeFree(&a);
        } else {
            const uint32_t off = be32(entry[i] + 0x3C);
            int k;
            discOff[i] = off;
            if (off < tab + 0x40 * nDisc) {
                at[i] = off; /* (no AnimData of its own: as it is) */
            } else {
                const uint32_t len = (uint64_t)off + 4 <= size ? be32(model + off) : 0;
                if (len == 0 || (uint64_t)off + len > size) {
                    FAIL("%.60s: its animation doesn't fit the model", an);
                    goto done;
                }
                for (k = 0; k < i; k++) {
                    if (!ours && fromRecipe[k] < 0 && discOff[k] == off && discOff[k] >= tab + 0x40 * nDisc &&
                        !portAnimRecipeHas(r, (const char*)entry[k])) {
                        break; /* (shared on the disc and still the same: written once) */
                    }
                }
                if (k < i) {
                    at[i] = at[k];
                } else {
                    uint8_t* p;
                    bufAlign(&o, 4);
                    at[i] = (uint32_t)o.n;
                    p = bufAdd(&o, len);
                    if (p != NULL) {
                        memcpy(p, model + off, len);
                    }
                }
            }
            if (ours) {
                discOff[i] = 0; /* (never shared) */
            }
        }
        if (o.bad) {
            FAIL("out of memory");
            goto done;
        }
        rec = o.p + tab + 0x40 * (uint32_t)i;
        memset(rec, 0, 0x40);
        if (fromRecipe[i] < 0 && !ours) {
            memcpy(rec, entry[i], 0x3C); /* (the name and the disc's bytes after it) */
        } else {
            memcpy(rec, an, strnlen(an, 0x3B));
        }
        put32(rec + 0x3C, at[i]);
    }
    put32(o.p + 0xE8 + 4 * C_ANIM, (uint32_t)nOut);
    memset(o.p + 0x4, 0, 64); /* (its file's name; its textures' stay the disc's) */
    memcpy(o.p + 0x4, name, strnlen(name, 63));
    bufAlign(&o, 32);
    if (o.bad) {
        FAIL("out of memory");
        goto done;
    }
    *out = o.p;
    *outSize = (uint32_t)o.n;
    o.p = NULL;
    ok = 1;
done:
    free(o.p);
    free(entry);
    free(fromRecipe);
    free(at);
    free(discOff);
    modelClose(&m);
    return ok;
}
