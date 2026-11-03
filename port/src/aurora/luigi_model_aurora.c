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
#include <stdlib.h>
#include <string.h>

extern const unsigned char portLuigiPack[];
extern const unsigned int portLuigiPack_size;

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

/* c_luigi plus the pack: the model (luigi_pack.merge) */
static int mergeModel(const uint8_t* lu, uint32_t luSize, const uint8_t* pk, Out* o) {
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
        if (ptrs[i] + (uint64_t)n * sz > luSize) {
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
                if (g >= n + addN || off + 4 > sz) {
                    return 0;
                }
                memcpy(first + g * sz + off, pk + po + 12 * k + 8, 4);
            }
        } else if (!put(o, lu + be32(lu + 0x14C + 4 * i), (size_t)n * sz) || !put(o, pk + addOff, (size_t)addN * sz)) {
            return 0;
        }
        counts[kTables[i].count] = n + addN;
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
    memset(o->p + 0x4, 0, 128);
    memcpy(o->p + 0x4, MODEL_NAME, sizeof(MODEL_NAME));
    memcpy(o->p + 0x44, MODEL_NAME, sizeof(MODEL_NAME));
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

void portLuigiModelInit(void) {
    const uint8_t* pk = portLuigiPack;
    uint32_t luSize = 0, ltSize = 0, mtSize = 0;
    uint8_t *lu, *lt, *mt;
    Out model = {0}, tex = {0};

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
    } else if (!mergeModel(lu, luSize, pk, &model) || !mergeTpl(lt, ltSize, mt, mtSize, pk, &tex)) {
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
