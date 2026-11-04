/* Luigi's model for "Play as Luigi" (port/include/port/luigi.h,
 * docs/LUIGI_ANIMS.md): at startup, a/c_luigi_pc and a/c_luigi_pc- made from
 * the user's own disc (c_luigi, c_luigi-, a_mario-) and the pack of our own
 * animations and hammer built in (port/assets/luigi/luigi_anims.bin), as
 * disc files the game loads like any other (portModsSetHostFile). Byte for
 * byte what tools/port/luigi_pack.py's merge() makes. */
#include "port/luigi.h"
#include "port/mods.h"

#include <dolphin/dvd.h>
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char portLuigiPack[];
extern const unsigned int portLuigiPack_size;
extern const unsigned char portFriendsPack[];
extern const unsigned int portFriendsPack_size;

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

static uint8_t* readDisc(const char* path, uint32_t* size) {
    DVDFileInfo info;
    uint8_t* data;
    uint32_t len;
    if (!DVDOpen(path, &info)) {
        return NULL;
    }
    *size = info.length;
    len = (*size + 31) & ~31u;
    data = malloc(len);
    if (data != NULL && DVDReadPrio(&info, data, (s32)len, 0, 2) < 0) {
        free(data);
        data = NULL;
    }
    DVDClose(&info);
    return data;
}

/* a growing output buffer */
typedef struct Out {
    uint8_t* p;
    size_t n, cap;
} Out;

static int grow(Out* o, size_t n) {
    if (o->n + n > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        uint8_t* q;
        while (cap < o->n + n) {
            cap *= 2;
        }
        q = realloc(o->p, cap);
        if (q == NULL) {
            return 0;
        }
        o->p = q;
        o->cap = cap;
    }
    return 1;
}

static int put(Out* o, const void* src, size_t n) {
    if (!grow(o, n)) {
        return 0;
    }
    if (src != NULL) {
        memcpy(o->p + o->n, src, n);
    } else {
        memset(o->p + o->n, 0, n);
    }
    o->n += n;
    return 1;
}

static int align(Out* o, size_t a) {
    return put(o, NULL, (a - o->n % a) % a);
}

/* the model's tables: record size and header count word (ttyd_anim.TABLES) */
static const struct {
    uint32_t size, count;
} kTables[24] = {
    {0xA8, 0}, {8, 1}, {12, 2}, {4, 3}, {12, 4}, {4, 5}, {4, 6}, {4, 7},
    {4, 8}, {4, 9}, {4, 10}, {4, 11}, {4, 12}, {4, 13}, {4, 14}, {4, 15},
    {8, 16}, {0x18, 17}, {8, 18}, {0x40, 19}, {0x6C, 20}, {1, 21}, {4, 22}, {0x58, 23},
};
#define GROUP_TABLE 23
#define COUNT_ANIMS 24
#define HEADER_SIZE 0x1B0
#define MODEL_NAME "c_luigi_pc"

/* a model plus a pack (luigi_pack.merge_model): named `name`, or keeping its own when NULL */
static int mergeModel(const uint8_t* lu, uint32_t luSize, const uint8_t* pk, const char* name, Out* o) {
    uint32_t counts[25], ptrs[25];
    uint32_t i, j, nAnim, nPack, nOut, tableAt;
    const uint8_t* names[256];
    const uint8_t* blobs[256];
    uint32_t sizes[256];
    for (i = 0; i < 25; i++) {
        counts[i] = be32(lu + 0xE8 + 4 * i);
        ptrs[i] = be32(lu + 0x14C + 4 * i);
    }
    if (!put(o, lu, HEADER_SIZE)) {
        return 0;
    }
    for (i = 0; i < 24; i++) {
        uint32_t sz = kTables[i].size, n = counts[kTables[i].count];
        uint32_t addOff = be32(pk + 0x10 + 8 * i), addN = be32(pk + 0x10 + 8 * i + 4);
        /* an empty table's offset can be anything (c_pansy's unused texture
         * coordinate sets point nowhere): neither checked nor read */
        if (n != 0 && ptrs[i] + (uint64_t)n * sz > luSize) {
            return 0;
        }
        if (!align(o, 4)) {
            return 0;
        }
        ptrs[i] = (uint32_t)o->n;
        if (i == GROUP_TABLE) {
            /* the hammer's group before the root, the root keeping the last
             * index; then the pack's group fields (the front glove's child) */
            uint32_t np = be32(pk + 0xD0), po = be32(pk + 0xD4), k;
            uint8_t* first;
            if (n == 0 || !put(o, lu + be32(lu + 0x14C + 4 * i), (size_t)(n - 1) * sz) ||
                !put(o, pk + addOff, (size_t)addN * sz) || !put(o, lu + be32(lu + 0x14C + 4 * i) + (n - 1) * sz, sz)) {
                return 0;
            }
            first = o->p + ptrs[i];
            for (k = 0; k < np; k++) {
                uint32_t g = be32(pk + po + 12 * k), off = be32(pk + po + 12 * k + 4);
                if (g >> 24) {
                    continue; /* another table's (below) */
                }
                if (g >= n + addN || off + 4 > sz) {
                    return 0;
                }
                memcpy(first + g * sz + off, pk + po + 12 * k + 8, 4);
            }
        } else if (!put(o, n ? lu + be32(lu + 0x14C + 4 * i) : NULL, (size_t)n * sz) ||
                   !put(o, addN ? pk + addOff : NULL, (size_t)addN * sz)) {
            return 0;
        }
        counts[kTables[i].count] = n + addN;
    }
    /* the pack's fields of other tables (luigi_pack.table_patch: the table's
     * index + 1 in the record's top byte; a variant's texture swap) */
    {
        uint32_t np = be32(pk + 0xD0), po = be32(pk + 0xD4), k;
        for (k = 0; k < np; k++) {
            uint32_t g = be32(pk + po + 12 * k), off = be32(pk + po + 12 * k + 4), t = (g >> 24) - 1;
            if (!(g >> 24)) {
                continue;
            }
            g &= 0xFFFFFF;
            if (t >= 24 || t == GROUP_TABLE || g >= counts[kTables[t].count] || off + 4 > kTables[t].size) {
                return 0;
            }
            memcpy(o->p + ptrs[t] + g * kTables[t].size + off, pk + po + 12 * k + 8, 4);
        }
    }
    /* animations: c_luigi's, one of the pack's replacing one of the same name, the rest after */
    nAnim = counts[COUNT_ANIMS];
    nPack = be32(pk + 0xE0);
    if (nAnim + nPack > 256) {
        return 0;
    }
    for (i = 0; i < nAnim; i++) {
        const uint8_t* e = lu + be32(lu + 0x14C + 4 * 24) + 0x40 * i;
        names[i] = e;
        blobs[i] = lu + be32(e + 0x3C);
        sizes[i] = be32(blobs[i]);
    }
    nOut = nAnim;
    for (j = 0; j < nPack; j++) {
        const uint8_t* e = pk + be32(pk + 0xE4) + 0x48 * j;
        for (i = 0; i < nOut; i++) {
            if (strncmp((const char*)names[i], (const char*)e, 60) == 0) {
                break;
            }
        }
        names[i] = e;
        blobs[i] = pk + be32(e + 60);
        sizes[i] = be32(e + 64);
        if (i == nOut) {
            nOut++;
        }
    }
    if (!align(o, 4)) {
        return 0;
    }
    ptrs[24] = (uint32_t)o->n;
    counts[COUNT_ANIMS] = nOut;
    tableAt = (uint32_t)o->n;
    if (!put(o, NULL, 0x40 * (size_t)nOut) || !align(o, 4)) {
        return 0;
    }
    put32(o->p, (uint32_t)o->n); /* the model's size: up to the first animation */
    for (i = 0; i < nOut; i++) {
        uint8_t* rec;
        size_t len;
        if (!align(o, 4)) {
            return 0;
        }
        rec = o->p + tableAt + 0x40 * i;
        len = strnlen((const char*)names[i], 59);
        memcpy(rec, names[i], len);
        put32(rec + 0x3C, (uint32_t)o->n);
        if (!put(o, blobs[i], sizes[i])) {
            return 0;
        }
    }
    for (i = 0; i < 25; i++) {
        put32(o->p + 0xE8 + 4 * i, counts[i]);
        put32(o->p + 0x14C + 4 * i, ptrs[i]);
    }
    if (name != NULL) {
        memset(o->p + 0x4, 0, 128);
        memcpy(o->p + 0x4, name, strnlen(name, 63));
        memcpy(o->p + 0x44, name, strnlen(name, 63));
    }
    return align(o, 32);
}

static uint32_t texSize(uint32_t w, uint32_t h, uint32_t fmt) {
    uint32_t bw, bh, bpp;
    switch (fmt) {
        case 0:
        case 14: bw = 8, bh = 8, bpp = 4; break;
        case 1:
        case 2: bw = 8, bh = 4, bpp = 8; break;
        case 3:
        case 4:
        case 5: bw = 4, bh = 4, bpp = 16; break;
        case 6: bw = 4, bh = 4, bpp = 32; break;
        default: return 0;
    }
    return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bw * bh * bpp / 8;
}

/* a texture of a TPL: its header and data */
static int tplEntry(const uint8_t* t, uint32_t size, uint32_t i, const uint8_t** hdr, const uint8_t** data,
                    uint32_t* len) {
    uint32_t n = be32(t + 4), tab = be32(t + 8), th, off;
    if (be32(t) != 0x0020AF30u || i >= n || tab + 8 * (i + 1) > size) {
        return 0;
    }
    th = be32(t + tab + 8 * i);
    if (be32(t + tab + 8 * i + 4) != 0 || th + 0x24 > size) {
        return 0; /* (no palettes in these) */
    }
    *hdr = t + th;
    off = be32(t + th + 8);
    *len = texSize((uint32_t)(t[th + 2] << 8 | t[th + 3]), (uint32_t)(t[th] << 8 | t[th + 1]), be32(t + th + 4));
    if (*len == 0 || off + *len > size) {
        return 0;
    }
    *data = t + off;
    return 1;
}

/* c_luigi-'s textures, then a_mario-'s hammer drawings (luigi_pack.merge_tpl) */
static int mergeTpl(const uint8_t* a, uint32_t aSize, const uint8_t* b, uint32_t bSize, const uint8_t* pk, Out* o) {
    uint32_t na = be32(a + 4), nb = be32(pk + 0xD8), toff = be32(pk + 0xDC), n = na + nb, i;
    uint8_t head[12];
    if (!put(o, NULL, 0xC + 8 * (size_t)n + 0x24 * (size_t)n) || !align(o, 32)) {
        return 0;
    }
    put32(head, 0x0020AF30u);
    put32(head + 4, n);
    put32(head + 8, 0xC);
    memcpy(o->p, head, 12);
    for (i = 0; i < n; i++) {
        const uint8_t *hdr, *data;
        uint32_t len, hoff = 0xC + 8 * n + 0x24 * i;
        int ok = i < na ? tplEntry(a, aSize, i, &hdr, &data, &len)
                        : tplEntry(b, bSize, be32(pk + toff + 4 * (i - na)), &hdr, &data, &len);
        if (!ok) {
            return 0;
        }
        put32(o->p + 0xC + 8 * i, hoff);
        put32(o->p + 0xC + 8 * i + 4, 0);
        memcpy(o->p + hoff, hdr, 0x24);
        if (!align(o, 32)) {
            return 0;
        }
        put32(o->p + hoff + 8, (uint32_t)o->n);
        if (!put(o, data, len)) {
            return 0;
        }
    }
    return align(o, 32);
}

/* ---- textures made at startup from the disc's (luigi_pack.apply_tx: the
 * LUTX recipe; integers only, so the same bytes as the Python) ---- */

static void expand565(uint32_t v, int c[3]) {
    int r = (int)(v >> 11) & 31, g = (int)(v >> 5) & 63, b = (int)v & 31;
    c[0] = (r << 3) | (r >> 2);
    c[1] = (g << 2) | (g >> 4);
    c[2] = (b << 3) | (b >> 2);
}

/* CMPR to RGBA (alpha 0 or 255) */
static void cmprDecode(const uint8_t* d, uint32_t w, uint32_t h, uint8_t* px) {
    uint32_t bx, by, sub, j, off = 0;
    memset(px, 0, (size_t)w * h * 4);
    for (by = 0; by < h; by += 8) {
        for (bx = 0; bx < w; bx += 8) {
            for (sub = 0; sub < 4; sub++) {
                uint32_t sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
                uint32_t c0 = (uint32_t)(d[off] << 8 | d[off + 1]), c1 = (uint32_t)(d[off + 2] << 8 | d[off + 3]);
                uint32_t bits = be32(d + off + 4);
                int p[4][4], a[3], b[3], k;
                off += 8;
                expand565(c0, a);
                expand565(c1, b);
                for (k = 0; k < 3; k++) {
                    p[0][k] = a[k];
                    p[1][k] = b[k];
                    if (c0 > c1) {
                        p[2][k] = (2 * a[k] + b[k]) / 3;
                        p[3][k] = (a[k] + 2 * b[k]) / 3;
                    } else {
                        p[2][k] = (a[k] + b[k]) / 2;
                        p[3][k] = 0;
                    }
                }
                p[0][3] = p[1][3] = p[2][3] = 255;
                p[3][3] = c0 > c1 ? 255 : 0;
                for (j = 0; j < 16; j++) {
                    uint32_t x = sx + (j & 3), y = sy + (j >> 2), sel = (bits >> (30 - 2 * j)) & 3;
                    if (x < w && y < h) {
                        uint8_t* q = px + 4 * ((size_t)y * w + x);
                        for (k = 0; k < 4; k++) {
                            q[k] = (uint8_t)p[sel][k];
                        }
                    }
                }
            }
        }
    }
}

static uint32_t to565(const int* c) {
    return (uint32_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
}

/* RGBA to CMPR, as luigi_pack.cmpr_encode */
static void cmprEncode(const uint8_t* px, uint32_t w, uint32_t h, uint8_t* d) {
    uint32_t bx, by, sub, j, off = 0;
    for (by = 0; by < h; by += 8) {
        for (bx = 0; bx < w; bx += 8) {
            for (sub = 0; sub < 4; sub++) {
                uint32_t sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4, c0, c1, bits = 0;
                int blk[16][4], nOpaque = 0, lo = -1, hi = -1, k, np, pal[4][3], ea[3], eb[3];
                long loL = 0, hiL = 0;
                for (j = 0; j < 16; j++) {
                    uint32_t x = sx + (j & 3), y = sy + (j >> 2);
                    for (k = 0; k < 4; k++) {
                        blk[j][k] = (x < w && y < h) ? px[4 * ((size_t)y * w + x) + k] : 0;
                    }
                    if (blk[j][3] >= 128) {
                        long L = 299L * blk[j][0] + 587L * blk[j][1] + 114L * blk[j][2];
                        if (lo < 0 || L < loL) {
                            lo = (int)j;
                            loL = L;
                        }
                        if (hi < 0 || L > hiL) {
                            hi = (int)j;
                            hiL = L;
                        }
                        nOpaque++;
                    }
                }
                if (nOpaque == 0) {
                    memset(d + off, 0, 4);
                    put32(d + off + 4, 0xFFFFFFFFu);
                    off += 8;
                    continue;
                }
                c0 = to565(blk[hi]);
                c1 = to565(blk[lo]);
                if (nOpaque < 16) {
                    if (c0 > c1) {
                        uint32_t t = c0;
                        c0 = c1;
                        c1 = t;
                    }
                    np = 3;
                } else {
                    if (c0 == c1) {
                        d[off] = (uint8_t)(c0 >> 8);
                        d[off + 1] = (uint8_t)c0;
                        d[off + 2] = (uint8_t)(c1 >> 8);
                        d[off + 3] = (uint8_t)c1;
                        put32(d + off + 4, 0);
                        off += 8;
                        continue;
                    }
                    if (c0 < c1) {
                        uint32_t t = c0;
                        c0 = c1;
                        c1 = t;
                    }
                    np = 4;
                }
                expand565(c0, ea);
                expand565(c1, eb);
                for (k = 0; k < 3; k++) {
                    pal[0][k] = ea[k];
                    pal[1][k] = eb[k];
                    if (np == 3) {
                        pal[2][k] = (ea[k] + eb[k]) / 2;
                    } else {
                        pal[2][k] = (2 * ea[k] + eb[k]) / 3;
                        pal[3][k] = (ea[k] + 2 * eb[k]) / 3;
                    }
                }
                for (j = 0; j < 16; j++) {
                    int sel = 0;
                    if (np == 3 && blk[j][3] < 128) {
                        sel = 3;
                    } else {
                        long best = -1;
                        for (k = 0; k < np; k++) {
                            long dr = pal[k][0] - blk[j][0], dg = pal[k][1] - blk[j][1], db = pal[k][2] - blk[j][2];
                            long dd = dr * dr + dg * dg + db * db;
                            if (best < 0 || dd < best) {
                                best = dd;
                                sel = k;
                            }
                        }
                    }
                    bits |= (uint32_t)sel << (30 - 2 * j);
                }
                d[off] = (uint8_t)(c0 >> 8);
                d[off + 1] = (uint8_t)c0;
                d[off + 2] = (uint8_t)(c1 >> 8);
                d[off + 3] = (uint8_t)c1;
                put32(d + off + 4, bits);
                off += 8;
            }
        }
    }
}

/* the drawing burnt black (luigi_pack.soot): soot from each texel's
 * brightness, ember spots (x, y, radius) glowing on it */
static void sootPixels(uint8_t* px, uint32_t w, uint32_t h, const uint8_t* spots, uint32_t nSpots) {
    static const int ember[2][3] = {{255, 140, 40}, {255, 214, 120}};
    uint32_t x, y, s;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t* q = px + 4 * ((size_t)y * w + x);
            int c[3], k, so;
            if (q[3] < 128) {
                continue;
            }
            so = 16 + (299 * q[0] + 587 * q[1] + 114 * q[2]) / 1000 * 28 / 100;
            c[0] = so + 8;
            c[1] = so + 3;
            c[2] = so;
            for (s = 0; s < nSpots; s++) {
                int sx = (int)be32(spots + 12 * s), sy = (int)be32(spots + 12 * s + 4), r = (int)be32(spots + 12 * s + 8);
                int dx = (int)x - sx, dy = (int)y - sy, d2 = dx * dx + dy * dy;
                if (d2 < r * r) {
                    int t = 255 - d2 * 255 / (r * r);
                    const int* tg = ember[t > 190 ? 1 : 0];
                    for (k = 0; k < 3; k++) {
                        int diff = (tg[k] - c[k]) * t;
                        /* Python's // floors; C's / truncates: the same for these (non-negative) */
                        c[k] += diff >= 0 ? diff / 255 : -((-diff + 254) / 255);
                    }
                }
            }
            q[0] = (uint8_t)c[0];
            q[1] = (uint8_t)c[1];
            q[2] = (uint8_t)c[2];
            q[3] = 255;
        }
    }
}

/* a disc TPL and a LUTX recipe: the TPL with the recipe's textures after its own */
static int applyTx(const uint8_t* base, uint32_t baseSize, const uint8_t* tx, uint32_t txSize, Out* o) {
    uint32_t na, nOps, n, i, at = 20;
    const uint8_t* hdrs[64];
    const uint8_t* datas[64];
    uint8_t* made[64];
    uint32_t lens[64];
    uint8_t madeHdr[64][0x24];
    int ok = 1;
    if (txSize < 20 || be32(base) != 0x0020AF30u || baseSize != be32(tx + 8) || crc32(base, baseSize) != be32(tx + 12)) {
        return 0;
    }
    na = be32(base + 4);
    nOps = be32(tx + 16);
    n = na + nOps;
    if (n > 64) {
        return 0;
    }
    memset(made, 0, sizeof(made));
    for (i = 0; i < na; i++) {
        if (!tplEntry(base, baseSize, i, &hdrs[i], &datas[i], &lens[i])) {
            return 0;
        }
    }
    for (i = na; i < n && ok; i++) {
        uint32_t kind, len;
        const uint8_t* p;
        if (at + 8 > txSize) {
            ok = 0;
            break;
        }
        kind = be32(tx + at);
        len = be32(tx + at + 4);
        p = tx + at + 8;
        if (at + 8 + len > txSize) {
            ok = 0;
            break;
        }
        at += 8 + len + ((4 - len % 4) % 4);
        memset(madeHdr[i], 0, 0x24);
        if (kind == 1 && len >= 8) { /* soot */
            uint32_t src = be32(p), nSpots = be32(p + 4), w, h;
            uint8_t* px;
            if (src >= na || 8 + 12 * nSpots > len || be32(hdrs[src] + 4) != 14) {
                ok = 0;
                break;
            }
            h = (uint32_t)(hdrs[src][0] << 8 | hdrs[src][1]);
            w = (uint32_t)(hdrs[src][2] << 8 | hdrs[src][3]);
            px = malloc((size_t)w * h * 4);
            made[i] = malloc(lens[src]);
            if (px == NULL || made[i] == NULL) {
                free(px);
                ok = 0;
                break;
            }
            cmprDecode(datas[src], w, h, px);
            sootPixels(px, w, h, p + 8, nSpots);
            cmprEncode(px, w, h, made[i]);
            free(px);
            memcpy(madeHdr[i], hdrs[src], 0x24);
            hdrs[i] = madeHdr[i];
            datas[i] = made[i];
            lens[i] = lens[src];
        } else if (kind == 2 && len >= 8) { /* a texture of our own */
            uint32_t h = (uint32_t)(p[0] << 8 | p[1]), w = (uint32_t)(p[2] << 8 | p[3]), fmt = be32(p + 4);
            uint32_t size = texSize(w, h, fmt);
            if (size == 0 || 8 + size > len) {
                ok = 0;
                break;
            }
            memcpy(madeHdr[i], p, 8);
            put32(madeHdr[i] + 0x14, 1); /* min and mag filter: linear, as the disc's */
            put32(madeHdr[i] + 0x18, 1);
            hdrs[i] = madeHdr[i];
            datas[i] = p + 8;
            lens[i] = size;
        } else {
            ok = 0;
        }
    }
    if (ok) {
        uint8_t head[12];
        ok = put(o, NULL, 0xC + 8 * (size_t)n + 0x24 * (size_t)n) && align(o, 32);
        put32(head, 0x0020AF30u);
        put32(head + 4, n);
        put32(head + 8, 0xC);
        if (ok) {
            memcpy(o->p, head, 12);
        }
        for (i = 0; i < n && ok; i++) {
            uint32_t hoff = 0xC + 8 * n + 0x24 * i;
            put32(o->p + 0xC + 8 * i, hoff);
            put32(o->p + 0xC + 8 * i + 4, 0);
            memcpy(o->p + hoff, hdrs[i], 0x24);
            ok = align(o, 32);
            if (ok) {
                put32(o->p + hoff + 8, (uint32_t)o->n);
                ok = put(o, datas[i], lens[i]);
            }
        }
        ok = ok && align(o, 32);
    }
    for (i = 0; i < 64; i++) {
        free(made[i]);
    }
    return ok;
}

/* Luigi's friends (docs/LUIGI_ANIMS.md, "Luigi's friends"): each pack of
 * port/assets/luigi/friend_anims.bin merged into the disc's own model file,
 * which then has its new poses under its own name (luigi_pack.merge_friends).
 * A model that isn't the one its pack was made for stays the disc's. An entry
 * that isn't a pack is a whole file of our own, a new model or its textures
 * (docs/LUIGI_ANIMS.md, "New models"), added as a/<name>; or a texture recipe
 * (LUTX: the disc's TPL with textures made from it, soot-black Blooey).
 * $TTYD_FRIENDS_DISC skips it; $TTYD_FRIENDS_DUMP=DIR writes each made file. */
/* $TTYD_FRIENDS_DUMP=DIR: each made file there (luigi_anims.py friends-portcheck) */
static void friendsDump(const char* name, const uint8_t* p, size_t n) {
    char out[512];
    SDL_IOStream* f;
    if (SDL_getenv("TTYD_FRIENDS_DUMP") == NULL) {
        return;
    }
    snprintf(out, sizeof(out), "%s/%s", SDL_getenv("TTYD_FRIENDS_DUMP"), name);
    f = SDL_IOFromFile(out, "wb");
    if (f != NULL) {
        SDL_WriteIO(f, p, n);
        SDL_CloseIO(f);
    }
}

static void friendsInit(void) {
    const uint8_t* fr = portFriendsPack;
    uint32_t n, i;
    if (portFriendsPack_size < 12 || memcmp(fr, "LUFR", 4) != 0 || SDL_getenv("TTYD_FRIENDS_DISC") != NULL) {
        return;
    }
    n = be32(fr + 8);
    for (i = 0; i < n && 12 + 0x28 * (i + 1) <= portFriendsPack_size; i++) {
        const uint8_t* e = fr + 12 + 0x28 * i;
        uint32_t off = be32(e + 32), size = be32(e + 36), baseSize = 0;
        char name[33], path[48];
        uint8_t* base;
        Out model = {0};
        memcpy(name, e, 32);
        name[32] = 0;
        if (off + (uint64_t)size > portFriendsPack_size) {
            continue;
        }
        if (size >= 20 && memcmp(fr + off, "LUTX", 4) == 0) {
            /* a texture recipe: the disc's TPL with textures made from it */
            Out tex = {0};
            snprintf(path, sizeof(path), "a/%s", name);
            base = readDisc(path, &baseSize);
            if (base == NULL || !applyTx(base, baseSize, fr + off, size, &tex)) {
                SDL_Log("luigi: couldn't make %s's textures; it keeps the disc's", name);
            } else {
                snprintf(path, sizeof(path), "/a/%s", name);
                portModsSetHostFile(path, tex.p, tex.n);
                friendsDump(name, tex.p, tex.n);
            }
            free(tex.p);
            free(base);
            continue;
        }
        if (size < 0xE8 || memcmp(fr + off, "LUPK", 4) != 0) {
            /* a whole file of our own (a new model or its textures): as it is */
            snprintf(path, sizeof(path), "/a/%s", name);
            portModsSetHostFile(path, fr + off, size);
            continue;
        }
        snprintf(path, sizeof(path), "a/%s", name);
        base = readDisc(path, &baseSize);
        if (base == NULL || baseSize != be32(fr + off + 8) || crc32(base, baseSize) != be32(fr + off + 12)) {
            SDL_Log("luigi: %s isn't the model its animations fit; it keeps the disc's", name);
        } else if (!mergeModel(base, baseSize, fr + off, NULL, &model)) {
            SDL_Log("luigi: couldn't add %s's animations", name);
        } else {
            snprintf(path, sizeof(path), "/a/%s", name);
            portModsSetHostFile(path, model.p, model.n);
            friendsDump(name, model.p, model.n);
        }
        free(model.p);
        free(base);
    }
}

void portLuigiModelInit(void) {
    const uint8_t* pk = portLuigiPack;
    uint32_t luSize = 0, ltSize = 0, mtSize = 0;
    uint8_t *lu, *lt, *mt;
    Out model = {0}, tex = {0};

    friendsInit();
    portLuigiSetModelReady(0);
    /* $TTYD_LUIGI_DISC: the disc's c_luigi as it is (side-by-side checks) */
    if (portLuigiPack_size < 0xE8 || memcmp(pk, "LUPK", 4) != 0 || SDL_getenv("TTYD_LUIGI_DISC") != NULL) {
        return;
    }
    lu = readDisc("a/c_luigi", &luSize);
    lt = readDisc("a/c_luigi-", &ltSize);
    mt = readDisc("a/a_mario-", &mtSize);
    if (lu == NULL || lt == NULL || mt == NULL) {
        SDL_Log("luigi: the disc's c_luigi, c_luigi- or a_mario- is missing; Luigi keeps the disc's model");
    } else if (luSize != be32(pk + 8) || crc32(lu, luSize) != be32(pk + 12)) {
        SDL_Log("luigi: c_luigi isn't the model the animation pack fits; Luigi keeps the disc's model");
    } else if (!mergeModel(lu, luSize, pk, MODEL_NAME, &model) || !mergeTpl(lt, ltSize, mt, mtSize, pk, &tex)) {
        SDL_Log("luigi: couldn't make Luigi's model; Luigi keeps the disc's model");
    } else {
        portModsSetHostFile("/a/" MODEL_NAME, model.p, model.n);
        portModsSetHostFile("/a/" MODEL_NAME "-", tex.p, tex.n);
        portLuigiSetModelReady(1);
        if (SDL_getenv("TTYD_LUIGI_DUMP") != NULL) {
            /* the check against luigi_pack.py merge */
            SDL_IOStream* f = SDL_IOFromFile(SDL_getenv("TTYD_LUIGI_DUMP"), "wb");
            if (f != NULL) {
                SDL_WriteIO(f, model.p, model.n);
                SDL_CloseIO(f);
            }
        }
    }
    free(model.p);
    free(tex.p);
    free(lu);
    free(lt);
    free(mt);
}

/* a model plus a pack (mergeModel), for Super Luigi's variants of the models
 * made here (port/src/aurora/sl_models_aurora.c): the result in *out (malloc'd) */
int portLuigiMergePack(const uint8_t* base, uint32_t baseSize, const uint8_t* pack, const char* name, uint8_t** out,
                       size_t* outSize) {
    Out o = {0};
    if (!mergeModel(base, baseSize, pack, name, &o)) {
        free(o.p);
        return 0;
    }
    *out = o.p;
    *outSize = o.n;
    return 1;
}
