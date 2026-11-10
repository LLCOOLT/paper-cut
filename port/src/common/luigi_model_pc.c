/* The Play as characters' models (port/include/port/luigi.h,
 * docs/LUIGI_ANIMS.md, docs/PLAY_AS.md): at startup, a/c_luigi_pc and
 * a/c_luigi_pc- made from the user's own disc (c_luigi, c_luigi-, a_mario-)
 * and the pack of our own animations and hammer built in
 * (port/assets/luigi/luigi_anims.bin), as disc files the game loads like any
 * other (portModsSetHostFile); likewise Peach's c_peach_pc and c_peach_r_pc
 * and Bowser's c_koopa_pc and c_koopa_r_pc (his back view, made over
 * c_koopa: tools/port/bowser_back.py) from their packs. His back view's own
 * drawings (his shell whole, and its rim to bend round it) are made here from
 * the disc by their recipe (port/assets/luigi/art/recipes/bowser_back.recipe,
 * port/src/common/art_recipe.c) and follow the hammer's in c_koopa_r_pc-.
 * Peach's parasol cards take the hammer drawings' places later
 * (portPlayAsHammerCards). Byte for byte what tools/port/luigi_pack.py's
 * merge() makes (before the cards). On every backend: headless serves the
 * files from port/src/headless/sdk/dvd_pc.c. */
#include "port/file_pc.h"
#include "port/art_recipe.h"
#include "port/luigi.h"
#include "port/mods.h"

#include <dolphin/dvd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (both backends: on headless too, so the tests play the same animations) */
#define LOG(...) (printf(__VA_ARGS__), putchar('\n'), fflush(stdout))

extern const unsigned char portLuigiPack[];
extern const unsigned int portLuigiPack_size;
extern const unsigned char portPeachPack[];
extern const unsigned int portPeachPack_size;
extern const unsigned char portPeachRPack[];
extern const unsigned int portPeachRPack_size;
extern const unsigned char portBowserPack[];
extern const unsigned int portBowserPack_size;
extern const unsigned char portBowserRPack[];
extern const unsigned int portBowserRPack_size;
extern const unsigned char portBowserBackRecipe[];
extern const unsigned int portBowserBackRecipe_size;

/* Bowser's back view's drawings, in the order its cards use them (bowser_back.py ART) */
static const char* const kBowserBackArt[] = {
    "art/models/c_koopa_r_pc/shell.png",
    "art/models/c_koopa_r_pc/rim.png",
};

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

/* the disc's model plus the pack: ours, named `name` (luigi_pack.merge) */
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
        if (ptrs[i] + (uint64_t)n * sz > luSize) {
            return 0;
        }
        if (!align(o, 4)) {
            return 0;
        }
        ptrs[i] = (uint32_t)o->n;
        if (i == GROUP_TABLE) {
            /* the hammer's group before the root, the root keeping the last
             * index */
            if (n == 0 || !put(o, lu + be32(lu + 0x14C + 4 * i), (size_t)(n - 1) * sz) ||
                !put(o, pk + addOff, (size_t)addN * sz) || !put(o, lu + be32(lu + 0x14C + 4 * i) + (n - 1) * sz, sz)) {
                return 0;
            }
        } else if (!put(o, lu + be32(lu + 0x14C + 4 * i), (size_t)n * sz) || !put(o, pk + addOff, (size_t)addN * sz)) {
            return 0;
        }
        counts[kTables[i].count] = n + addN;
    }
    /* the pack's fields: groups' (the front glove's child: the hammer), or in
     * the top byte another table plus 1 (a back view's node values) */
    {
        uint32_t np = be32(pk + 0xD0), po = be32(pk + 0xD4), k;
        for (k = 0; k < np; k++) {
            uint32_t g = be32(pk + po + 12 * k), off = be32(pk + po + 12 * k + 4);
            uint32_t t = g >> 24 == 0 ? GROUP_TABLE : (g >> 24) - 1, r = g & 0xFFFFFF;
            if (t >= 24 || r >= counts[kTables[t].count] || off + 4 > kTables[t].size) {
                return 0;
            }
            memcpy(o->p + ptrs[t] + r * kTables[t].size + off, pk + po + 12 * k + 8, 4);
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
    memset(o->p + 0x4, 0, 128);
    memcpy(o->p + 0x4, name, strnlen(name, 63));
    memcpy(o->p + 0x44, name, strnlen(name, 63));
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

/* an image's texels as RGBA8 (format 6) in 4x4 blocks, each its 16 texels' alpha and red, then their
 * green and blue (luigi_pack.rgba8): texSize(w, h, 6) bytes at data */
static void rgba8Data(const PortArtImage* im, uint8_t* data) {
    int bx, by, i;
    uint8_t* p = data;
    for (by = 0; by < im->h; by += 4) {
        for (bx = 0; bx < im->w; bx += 4) {
            for (i = 0; i < 16; i++) {
                int x = bx + i % 4, y = by + i / 4;
                const uint8_t* t = x < im->w && y < im->h ? im->px + ((size_t)y * im->w + x) * 4 : NULL;
                p[2 * i] = t ? t[3] : 0;
                p[2 * i + 1] = t ? t[0] : 0;
                p[32 + 2 * i] = t ? t[1] : 0;
                p[33 + 2 * i] = t ? t[2] : 0;
            }
            p += 64;
        }
    }
}

/* a recipe's image as a TPL texture: its header (RGBA8, linear filters) and its texels */
static void rgba8(const PortArtImage* im, uint8_t* hdr, uint8_t* data) {
    memset(hdr, 0, 0x24);
    hdr[0] = (uint8_t)(im->h >> 8), hdr[1] = (uint8_t)im->h;
    hdr[2] = (uint8_t)(im->w >> 8), hdr[3] = (uint8_t)im->w;
    put32(hdr + 4, 6);
    put32(hdr + 0x14, 1); /* min and mag filter linear, as the disc's */
    put32(hdr + 0x18, 1);
    rgba8Data(im, data);
}

/* c_luigi-'s textures, then a_mario-'s hammer drawings (luigi_pack.merge_tpl), then `art`; with cards
 * (ncards of them, Peach's parasol), the hammer drawings' places hold those instead, as RGBA8 with the
 * drawings' own sampler settings */
static int mergeTpl(const uint8_t* a, uint32_t aSize, const uint8_t* b, uint32_t bSize, const uint8_t* pk,
                    const PortArtImage* cards, int ncards, const PortArtImage* art, uint32_t nArt, Out* o) {
    uint32_t na = be32(a + 4), nb = be32(pk + 0xD8), toff = be32(pk + 0xDC), n = na + nb + nArt, i;
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
        put32(o->p + 0xC + 8 * i, hoff);
        put32(o->p + 0xC + 8 * i + 4, 0);
        if (i >= na + nb) { /* a recipe's drawing */
            const PortArtImage* im = &art[i - na - nb];
            len = texSize((uint32_t)im->w, (uint32_t)im->h, 6);
            if (!align(o, 32) || !put(o, NULL, len)) {
                return 0;
            }
            rgba8(im, o->p + hoff, o->p + o->n - len);
            put32(o->p + hoff + 8, (uint32_t)(o->n - len));
            continue;
        }
        if (!(i < na ? tplEntry(a, aSize, i, &hdr, &data, &len)
                     : tplEntry(b, bSize, be32(pk + toff + 4 * (i - na)), &hdr, &data, &len))) {
            return 0;
        }
        memcpy(o->p + hoff, hdr, 0x24);
        if (!align(o, 32)) {
            return 0;
        }
        put32(o->p + hoff + 8, (uint32_t)o->n);
        if (i >= na && (int)(i - na) < ncards && cards[i - na].px != NULL) {
            const PortArtImage* c = &cards[i - na];
            o->p[hoff] = (uint8_t)(c->h >> 8);
            o->p[hoff + 1] = (uint8_t)c->h;
            o->p[hoff + 2] = (uint8_t)(c->w >> 8);
            o->p[hoff + 3] = (uint8_t)c->w;
            put32(o->p + hoff + 4, 6); /* GX_TF_RGBA8 */
            o->p[hoff + 0x21] = 0; /* (one level) */
            o->p[hoff + 0x22] = 0;
            len = texSize((uint32_t)c->w, (uint32_t)c->h, 6);
            if (!put(o, NULL, len)) {
                return 0;
            }
            rgba8Data(c, o->p + o->n - len);
        } else if (!put(o, data, len)) {
            return 0;
        }
    }
    return align(o, 32);
}

static void dump(const char* path, const Out* o) {
    FILE* f = portFopen(path, "wb");
    if (f != NULL) {
        fwrite(o->p, 1, o->n, f);
        fclose(f);
    }
}

/* each pack: the disc's model it fits, ours, whose it is (back: her back-facing one), and the
 * drawings of its own a recipe makes (the recipe's text, their paths) */
#define MAX_ART 8
static const struct {
    const char *base, *name;
    const unsigned char* pack;
    const unsigned int* size;
    int chr, back;
    const unsigned char* recipe;
    const unsigned int* recipeSize;
    const char* const* art;
    uint32_t nArt;
} kPacks[] = {
    {"c_luigi", "c_luigi_pc", portLuigiPack, &portLuigiPack_size, PORT_PLAY_AS_LUIGI, 0, NULL, NULL, NULL, 0},
    {"c_peach", "c_peach_pc", portPeachPack, &portPeachPack_size, PORT_PLAY_AS_PEACH, 0, NULL, NULL, NULL, 0},
    {"c_peach_r", "c_peach_r_pc", portPeachRPack, &portPeachRPack_size, PORT_PLAY_AS_PEACH, 1, NULL, NULL, NULL, 0},
    {"c_koopa", "c_koopa_pc", portBowserPack, &portBowserPack_size, PORT_PLAY_AS_BOWSER, 0, NULL, NULL, NULL, 0},
    {"c_koopa", "c_koopa_r_pc", portBowserRPack, &portBowserRPack_size, PORT_PLAY_AS_BOWSER, 1,
     portBowserBackRecipe, &portBowserBackRecipe_size, kBowserBackArt,
     sizeof(kBowserBackArt) / sizeof(kBowserBackArt[0])},
};

static uint8_t* readArt(void* user, const char* path, uint32_t* size) {
    (void)user;
    return readDisc(path, size);
}

/* pack k's recipe drawings made from the disc into art[]: 1, or 0 (logged) if one can't be */
static int makeArt(size_t k, PortArtImage* art) {
    char* text;
    PortArtDisc* disc;
    uint32_t i;
    int ok = 1;
    if (kPacks[k].nArt == 0) {
        return 1;
    }
    if (kPacks[k].nArt > MAX_ART) {
        return 0;
    }
    text = malloc(*kPacks[k].recipeSize + 1);
    disc = portArtDiscOpen(readArt, NULL, NULL);
    if (text == NULL || disc == NULL) {
        free(text);
        portArtDiscClose(disc);
        return 0;
    }
    memcpy(text, kPacks[k].recipe, *kPacks[k].recipeSize);
    text[*kPacks[k].recipeSize] = 0;
    for (i = 0; i < kPacks[k].nArt && ok; i++) {
        char err[256];
        if (portArtMake(text, kPacks[k].art[i], disc, &art[i], err, sizeof(err)) != 1) {
            LOG("play as: %s can't be made from this disc (%s)", kPacks[k].art[i], err);
            art[i].px = NULL;
            ok = 0;
        }
    }
    free(text);
    portArtDiscClose(disc);
    return ok;
}

void portLuigiModelInit(void) {
    uint32_t mtSize = 0;
    uint8_t* mt;
    size_t k;

    for (k = 0; k < sizeof(kPacks) / sizeof(kPacks[0]); k++) {
        portPlayAsSetModelReady(kPacks[k].chr, kPacks[k].back, 0);
    }
    /* $TTYD_LUIGI_DISC: the disc's models as they are (side-by-side checks) */
    if (getenv("TTYD_LUIGI_DISC") != NULL) {
        return;
    }
    mt = readDisc("a/a_mario-", &mtSize);
    for (k = 0; k < sizeof(kPacks) / sizeof(kPacks[0]); k++) {
        const uint8_t* pk = kPacks[k].pack;
        uint32_t luSize = 0, ltSize = 0;
        uint8_t *lu, *lt;
        char path[96];
        Out model = {0}, tex = {0};
        PortArtImage art[MAX_ART];
        uint32_t i;
        memset(art, 0, sizeof(art));
        if (*kPacks[k].size < 0xE8 || memcmp(pk, "LUPK", 4) != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "a/%s", kPacks[k].base);
        lu = readDisc(path, &luSize);
        snprintf(path, sizeof(path), "a/%s-", kPacks[k].base);
        lt = readDisc(path, &ltSize);
        if (lu == NULL || lt == NULL || mt == NULL) {
            LOG("play as: the disc's %s, its textures or a_mario- is missing; the disc's model stays",
                kPacks[k].base);
        } else if (luSize != be32(pk + 8) || crc32(lu, luSize) != be32(pk + 12)) {
            LOG("play as: %s isn't the model the animation pack fits; the disc's model stays", kPacks[k].base);
        } else if (!makeArt(k, art) || !mergeModel(lu, luSize, pk, kPacks[k].name, &model) ||
                   !mergeTpl(lt, ltSize, mt, mtSize, pk, NULL, 0, art, kPacks[k].nArt, &tex)) {
            LOG("play as: couldn't make %s; the disc's model stays", kPacks[k].name);
        } else {
            snprintf(path, sizeof(path), "/a/%s", kPacks[k].name);
            portModsSetHostFile(path, model.p, model.n);
            snprintf(path, sizeof(path), "/a/%s-", kPacks[k].name);
            portModsSetHostFile(path, tex.p, tex.n);
            portPlayAsSetModelReady(kPacks[k].chr, kPacks[k].back, 1);
            /* the checks against luigi_pack.py merge: $TTYD_LUIGI_DUMP (Luigi's
             * model, a file), $TTYD_PLAY_AS_DUMP (every one, a folder) */
            if (kPacks[k].chr == PORT_PLAY_AS_LUIGI && getenv("TTYD_LUIGI_DUMP") != NULL) {
                dump(getenv("TTYD_LUIGI_DUMP"), &model);
            }
            if (getenv("TTYD_PLAY_AS_DUMP") != NULL) {
                char out[512];
                snprintf(out, sizeof(out), "%s/%s", getenv("TTYD_PLAY_AS_DUMP"), kPacks[k].name);
                dump(out, &model);
                snprintf(out, sizeof(out), "%s/%s-", getenv("TTYD_PLAY_AS_DUMP"), kPacks[k].name);
                dump(out, &tex);
            }
        }
        for (i = 0; i < MAX_ART; i++) {
            free(art[i].px);
        }
        free(model.p);
        free(tex.p);
        free(lu);
        free(lt);
    }
    free(mt);
}

/* Peach's parasol (art/recipes/peach_parasol.recipe, docs/PLAY_AS.md): her two models' textures made
 * again with the cards in the hammer drawings' places (luigi_pack.PARASOL_TEXTURES: the twelve closed,
 * then the three open) */
static int sParasol;

int portPlayAsHammerCards(int chr, const PortArtImage* cards, int ncards) {
    uint32_t mtSize = 0;
    uint8_t* mt;
    size_t k;
    int made = 0, want = 0;
    sParasol = 0;
    if (getenv("TTYD_LUIGI_DISC") != NULL || (mt = readDisc("a/a_mario-", &mtSize)) == NULL) {
        return 0;
    }
    for (k = 0; k < sizeof(kPacks) / sizeof(kPacks[0]); k++) {
        const uint8_t* pk = kPacks[k].pack;
        uint32_t ltSize = 0;
        uint8_t* lt;
        char path[96];
        Out tex = {0};
        PortArtImage art[MAX_ART];
        uint32_t i;
        if (kPacks[k].chr != chr || *kPacks[k].size < 0xE8 || memcmp(pk, "LUPK", 4) != 0 ||
            (int)be32(pk + 0xD8) != ncards) {
            continue;
        }
        want++;
        memset(art, 0, sizeof(art));
        snprintf(path, sizeof(path), "a/%s-", kPacks[k].base);
        lt = readDisc(path, &ltSize);
        if (lt != NULL && makeArt(k, art) && mergeTpl(lt, ltSize, mt, mtSize, pk, cards, ncards, art, kPacks[k].nArt, &tex)) {
            snprintf(path, sizeof(path), "/a/%s-", kPacks[k].name);
            portModsSetHostFile(path, tex.p, tex.n);
            made++;
        }
        for (i = 0; i < MAX_ART; i++) {
            free(art[i].px);
        }
        free(tex.p);
        free(lt);
    }
    free(mt);
    sParasol = want > 0 && made == want;
    if (!sParasol && want > 0) {
        LOG("play as: the parasol couldn't be put on %d of %d models; the hammer stays", want - made, want);
    }
    return sParasol;
}

int portPlayAsParasol(void) {
    return sParasol;
}
