/* Super Luigi's new models (port/include/port/sl_models.h, docs/LUIGI_ANIMS.md,
 * "Super Luigi's models"): the files of port/assets/super_luigi/sl_models.bin,
 * our own work built into the executable, handed to the game as a/<name>
 * (portModsSetHostFile), so an NPC, a battle unit or a scene loads them by
 * name like any disc model. The container is luigi_pack's LUFR
 * (tools/port/sl_models.py writes it): "LUFR", u32 version, u32 count, then per
 * entry name[32], u32 offset, u32 size. An entry is:
 *  - a whole file (a model or its TPL): a/<name> as it is;
 *  - "<variant>@<base>": a pack (LUPK) of records and animations to merge into
 *    a model the port made at startup (c_luigi_pc) or, failing that, the
 *    disc's own (c_heavy for Sergeant Bulkhead), as a/<variant>
 *    (tools/port/sl_attach.py); "<variant>-@<base>-": a TPL of our drawings to
 *    put after the base's textures. The pack points into the base's tables by
 *    count, so it is checked against the base's position and texture counts.
 * $TTYD_SL_DUMP=DIR writes each made variant there (sl_models.py's merge is the
 * same, byte for byte). */
#include "port/sl_models.h"
#include "port/art_recipe.h"
#include "port/luigi.h"
#include "port/mods.h"
#include "port/superluigi.h"
#include <dolphin/dvd.h>
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char portSuperLuigiModels[];
extern const unsigned int portSuperLuigiModels_size;

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void dump(const char* name, const uint8_t* p, size_t n) {
    const char* dir = SDL_getenv("TTYD_SL_DUMP");
    char path[512];
    SDL_IOStream* f;
    if (dir == NULL) {
        return;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = SDL_IOFromFile(path, "wb");
    if (f != NULL) {
        SDL_WriteIO(f, p, n);
        SDL_CloseIO(f);
    }
}

/* a file of the disc, whole (malloc'd), or NULL */
static uint8_t* readDisc(const char* path, size_t* size) {
    DVDFileInfo info;
    uint8_t* data;
    uint32_t len;
    if (!DVDOpen(path, &info)) {
        return NULL;
    }
    *size = info.length;
    len = (info.length + 31) & ~31u;
    data = malloc(len);
    if (data != NULL && DVDReadPrio(&info, data, (s32)len, 0, 2) < 0) {
        free(data);
        data = NULL;
    }
    DVDClose(&info);
    return data;
}

/* a variant's base: a file the port made (a host file), else the disc's */
static const uint8_t* baseFile(const char* name, size_t* size, uint8_t** owned) {
    char path[48];
    const uint8_t* p;
    snprintf(path, sizeof(path), "/a/%s", name);
    p = portModsGetHostFile(path, size);
    if (p == NULL) {
        snprintf(path, sizeof(path), "a/%s", name);
        p = *owned = readDisc(path, size);
    }
    return p;
}

/* the bytes of a CMPR/I4/... texture (luigi_pack._tex_size) */
static uint32_t texSize(uint32_t w, uint32_t h, uint32_t fmt) {
    uint32_t bw, bh, bpp;
    switch (fmt) {
    case 0: case 14: bw = 8; bh = 8; bpp = 4; break;
    case 1: case 2: bw = 8; bh = 4; bpp = 8; break;
    case 3: case 4: case 5: bw = 4; bh = 4; bpp = 16; break;
    case 6: bw = 4; bh = 4; bpp = 32; break;
    default: return 0;
    }
    return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * bw * bh * bpp / 8;
}

/* TPL a's textures then b's, laid out as the disc's (luigi_pack.layout_tpl) */
static uint8_t* appendTpl(const uint8_t* a, size_t an, const uint8_t* b, size_t bn, size_t* outN) {
    const uint8_t* src[2] = {a, b};
    size_t srcN[2] = {an, bn};
    uint32_t na = be32(a + 4), nb = be32(b + 4), n = na + nb, i, k = 0, s;
    size_t cap = 0xC + 8 * (size_t)n + 0x24 * (size_t)n + 32, at;
    uint8_t* out;
    for (s = 0; s < 2; s++) {
        uint32_t cnt = be32(src[s] + 4), tab = be32(src[s] + 8);
        for (i = 0; i < cnt; i++) {
            uint32_t th = be32(src[s] + tab + 8 * i);
            if (th + 0x24 > srcN[s]) {
                return NULL;
            }
            cap += texSize(be32(src[s] + th) & 0xFFFF, be32(src[s] + th) >> 16, be32(src[s] + th + 4)) + 32;
        }
    }
    out = calloc(1, cap + 32);
    if (out == NULL) {
        return NULL;
    }
    put32(out, 0x0020AF30);
    put32(out + 4, n);
    put32(out + 8, 0xC);
    at = (0xC + 8 * (size_t)n + 0x24 * (size_t)n + 31) & ~(size_t)31;
    for (s = 0; s < 2; s++) {
        uint32_t cnt = be32(src[s] + 4), tab = be32(src[s] + 8);
        for (i = 0; i < cnt; i++, k++) {
            uint32_t th = be32(src[s] + tab + 8 * i);
            uint32_t hh = be32(src[s] + th) >> 16, ww = be32(src[s] + th) & 0xFFFF, fmt = be32(src[s] + th + 4);
            uint32_t off = be32(src[s] + th + 8), size = texSize(ww, hh, fmt);
            size_t hoff = 0xC + 8 * (size_t)n + 0x24 * (size_t)k;
            if (size == 0 || off + (size_t)size > srcN[s]) {
                free(out);
                return NULL;
            }
            put32(out + 0xC + 8 * k, (uint32_t)hoff);
            at = (at + 31) & ~(size_t)31;
            memcpy(out + hoff, src[s] + th, 0x24);
            put32(out + hoff + 8, (uint32_t)at);
            memcpy(out + at, src[s] + off, size);
            at += size;
        }
    }
    *outN = (at + 31) & ~(size_t)31;
    return out;
}

/* "<variant>@<base>": a pack merged into the base's model, or "<variant>-@<base>-": textures after its own */
static void variant(const char* name, const uint8_t* data, uint32_t size) {
    char vname[33], path[48];
    const char* at = strchr(name, '@');
    size_t baseN = 0, outN = 0;
    const uint8_t* base;
    uint8_t *out = NULL, *ownBase = NULL, *ownTpl = NULL;
    memcpy(vname, name, (size_t)(at - name));
    vname[at - name] = 0;
    base = baseFile(at + 1, &baseN, &ownBase);
    if (base == NULL) {
        SDL_Log("super luigi: %s has no %s to go on", vname, at + 1);
        return;
    }
    if (size >= 0xE8 && memcmp(data, "LUPK", 4) == 0) {
        /* the counts the pack was made for: its first shape's first position,
         * its first texture bind's texture (the base's TPL comes first) */
        uint32_t shOff = be32(data + 0x10), shN = be32(data + 0x14), bOff = be32(data + 0x10 + 8 * 19),
                 bN = be32(data + 0x10 + 8 * 19 + 4);
        char tplName[40];
        size_t tplN = 0;
        const uint8_t* tpl;
        snprintf(tplName, sizeof(tplName), "%s-", at + 1);
        tpl = baseFile(tplName, &tplN, &ownTpl);
        /* our first drawing, a texture swap's (a texture bind's texture set,
         * luigi_pack.table_patch) or else a card's, is just after the base's */
        uint32_t np = be32(data + 0xD0), po = be32(data + 0xD4), k, first = UINT32_MAX;
        for (k = 0; k < np && po + 12 * (uint64_t)(k + 1) <= size; k++) {
            if (be32(data + po + 12 * k) >> 24 == 20 && be32(data + po + 12 * k + 4) == 4 &&
                be32(data + po + 12 * k + 8) < first) {
                first = be32(data + po + 12 * k + 8);
            }
        }
        for (k = 0; k < bN && bOff + 0x40 * (uint64_t)(k + 1) <= size; k++) {
            if (be32(data + bOff + 0x40 * k + 4) < first) {
                first = be32(data + bOff + 0x40 * k + 4);
            }
        }
        if (baseN < 0x1B0 || tpl == NULL || tplN < 12 || shOff + 0xA8 > size || bOff + 0x40 > size ||
            (shN && be32(data + shOff + 0x40) != be32(base + 0xF0)) || (first != UINT32_MAX && first != be32(tpl + 4))) {
            SDL_Log("super luigi: %s isn't the model %s was made for; it's left out", at + 1, vname);
            free(ownBase);
            free(ownTpl);
            return;
        }
        free(ownTpl);
        if (!portLuigiMergePack(base, (uint32_t)baseN, data, vname, &out, &outN)) {
            SDL_Log("super luigi: couldn't make %s", vname);
            free(ownBase);
            return;
        }
    } else if (size >= 12 && be32(data) == 0x0020AF30 && baseN >= 12) {
        out = appendTpl(base, baseN, data, size, &outN);
        if (out == NULL) {
            SDL_Log("super luigi: couldn't make %s's textures", vname);
            free(ownBase);
            return;
        }
    } else {
        free(ownBase);
        return;
    }
    free(ownBase);
    snprintf(path, sizeof(path), "/a/%s", vname);
    portModsSetHostFile(path, out, outN);
    dump(vname, out, outN);
    free(out);
}

/* ---- a model's textures made from the disc (art/RECIPES.md, "A model's cards from the disc") ----
 * A recipe image at art/models/<model>/<n>.png replaces image n of a/<model>-, whatever made that
 * file: our container (the drawn card stays the fallback: an image that can't be made just leaves it
 * there), a variant, or the disc. So what ships is the geometry, the motions and the drawn cards; the
 * cards the player sees are the player's own disc's. */

/* one 4x4 block of RGBA as CMPR: the two farthest colours of the block for its ends, as the disc's
 * art (a block of a recoloured CMPR source stays near its own two ends). Any clear texel: the
 * three-colour mode, whose fourth is clear. */
static void cmprBlock(const uint8_t* b, uint8_t* out) {
    int i, j, clear = 0, a = -1, z = -1;
    long best = -1;
    uint16_t c0, c1;
    uint32_t bits = 0;
    uint8_t pal[4][3];
    for (i = 0; i < 16; i++) {
        if (b[i * 4 + 3] < 128) {
            clear = 1;
            continue;
        }
        for (j = i; j < 16; j++) {
            long d;
            if (b[j * 4 + 3] < 128) {
                continue;
            }
            d = (b[i * 4] - b[j * 4]) * (b[i * 4] - b[j * 4]) + (b[i * 4 + 1] - b[j * 4 + 1]) * (b[i * 4 + 1] - b[j * 4 + 1]) +
                (b[i * 4 + 2] - b[j * 4 + 2]) * (b[i * 4 + 2] - b[j * 4 + 2]);
            if (d > best) {
                best = d;
                a = i;
                z = j;
            }
        }
    }
    if (a < 0) { /* all clear */
        memset(out, 0, 4);
        memset(out + 4, 0xFF, 4);
        return;
    }
    c0 = (uint16_t)((b[a * 4] >> 3) << 11 | (b[a * 4 + 1] >> 2) << 5 | b[a * 4 + 2] >> 3);
    c1 = (uint16_t)((b[z * 4] >> 3) << 11 | (b[z * 4 + 1] >> 2) << 5 | b[z * 4 + 2] >> 3);
    if (clear ? c0 > c1 : c0 < c1) {
        uint16_t t = c0;
        c0 = c1;
        c1 = t;
    }
    if (!clear && c0 == c1) {
        if (c0 == 0xFFFF) {
            c1 = c0 - 1;
        } else {
            c0++;
        }
    }
    for (i = 0; i < 2; i++) {
        const uint16_t c = i ? c1 : c0;
        const int r = (c >> 11) & 31, g = (c >> 5) & 63, bl = c & 31;
        pal[i][0] = (uint8_t)(r << 3 | r >> 2);
        pal[i][1] = (uint8_t)(g << 2 | g >> 4);
        pal[i][2] = (uint8_t)(bl << 3 | bl >> 2);
    }
    for (i = 0; i < 3; i++) {
        pal[2][i] = (uint8_t)(clear ? (pal[0][i] + pal[1][i]) / 2 : (2 * pal[0][i] + pal[1][i]) / 3);
        pal[3][i] = (uint8_t)(clear ? 0 : (pal[0][i] + 2 * pal[1][i]) / 3);
    }
    for (i = 0; i < 16; i++) {
        int k, pick = 3;
        long bd = 0x7FFFFFFF;
        if (b[i * 4 + 3] >= 128) {
            for (k = 0; k < (clear ? 3 : 4); k++) {
                const int dr = b[i * 4] - pal[k][0], dg = b[i * 4 + 1] - pal[k][1], db = b[i * 4 + 2] - pal[k][2];
                const long d = dr * dr + dg * dg + db * db;
                if (d < bd) {
                    bd = d;
                    pick = k;
                }
            }
        }
        bits = bits << 2 | (uint32_t)pick;
    }
    out[0] = (uint8_t)(c0 >> 8);
    out[1] = (uint8_t)c0;
    out[2] = (uint8_t)(c1 >> 8);
    out[3] = (uint8_t)c1;
    put32(out + 4, bits);
}

/* RGBA (w, h multiples of 8) as CMPR, GX's order: 8x8 tiles of four 4x4 blocks */
static void encodeCmpr(const uint8_t* px, int w, int h, uint8_t* out) {
    int bx, by, sub, j;
    for (by = 0; by < h; by += 8) {
        for (bx = 0; bx < w; bx += 8) {
            for (sub = 0; sub < 4; sub++) {
                const int sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
                uint8_t blk[64];
                for (j = 0; j < 16; j++) {
                    memcpy(blk + j * 4, px + ((sy + (j >> 2)) * w + sx + (j & 3)) * 4, 4);
                }
                cmprBlock(blk, out);
                out += 8;
            }
        }
    }
}

/* RGBA as RGB5A3, for a texture with soft edges (glows, the see-through): 4x4 tiles */
static void encodeRgb5a3(const uint8_t* px, int w, int h, uint8_t* out) {
    int bx, by, x, y;
    for (by = 0; by < h; by += 4) {
        for (bx = 0; bx < w; bx += 4) {
            for (y = by; y < by + 4; y++) {
                for (x = bx; x < bx + 4; x++) {
                    const uint8_t* p = px + ((size_t)y * w + x) * 4;
                    const uint16_t v = p[3] >= 0xE0 ? (uint16_t)(0x8000 | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | p[2] >> 3)
                                                    : (uint16_t)((p[3] >> 5) << 12 | (p[0] >> 4) << 8 | (p[1] >> 4) << 4 | p[2] >> 4);
                    *out++ = (uint8_t)(v >> 8);
                    *out++ = (uint8_t)v;
                }
            }
        }
    }
}

typedef struct NewTex {
    uint32_t w, h, fmt, size;
    uint8_t* data;
} NewTex;

/* a recipe image's PNG as a GX texture; 0 if it can't be */
static int encodeArt(const char* path, NewTex* t) {
    const void* png;
    unsigned int n = 0;
    SDL_IOStream* io;
    SDL_Surface* s;
    SDL_Surface* rgba;
    int ok = 0, y, x, soft = 0;
    if (!portArtAsset(path, &png, &n) || png == NULL) {
        return 0;
    }
    io = SDL_IOFromConstMem(png, n);
    s = io != NULL ? SDL_LoadPNG_IO(io, true) : NULL;
    rgba = s != NULL ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : NULL;
    SDL_DestroySurface(s);
    if (rgba == NULL) {
        SDL_Log("art: %s: not a PNG (%s)", path, SDL_GetError());
        return 0;
    }
    if (rgba->w % 8 == 0 && rgba->h % 8 == 0 && rgba->w > 0 && rgba->h > 0) {
        uint8_t* px = malloc((size_t)rgba->w * rgba->h * 4);
        if (px != NULL) {
            for (y = 0; y < rgba->h; y++) {
                memcpy(px + (size_t)y * rgba->w * 4, (const uint8_t*)rgba->pixels + (size_t)y * rgba->pitch, (size_t)rgba->w * 4);
                for (x = 0; x < rgba->w; x++) {
                    const uint8_t a = px[((size_t)y * rgba->w + x) * 4 + 3];
                    soft |= a != 0 && a != 255;
                }
            }
            t->w = (uint32_t)rgba->w;
            t->h = (uint32_t)rgba->h;
            t->fmt = soft ? 5 : 14;
            t->size = texSize(t->w, t->h, t->fmt);
            t->data = malloc(t->size);
            if (t->data != NULL) {
                (soft ? encodeRgb5a3 : encodeCmpr)(px, rgba->w, rgba->h, t->data);
                ok = 1;
            }
            free(px);
        }
    } else {
        SDL_Log("art: %s is %dx%d: a model's texture has sides in eights; left as it was", path, rgba->w, rgba->h);
    }
    SDL_DestroySurface(rgba);
    return ok;
}

/* the images art/models/<model>/ has, into a/<model>- */
static void modelArt(const char* model, const char* folder) {
    char name[48], path[48];
    size_t n = 0, cap, at;
    const uint8_t* tpl;
    uint8_t *own = NULL, *out;
    uint32_t count, tab, i;
    NewTex* tex;
    int index;
    const char* art;
    snprintf(name, sizeof(name), "%s-", model);
    tpl = baseFile(name, &n, &own);
    if (tpl == NULL || n < 12 || be32(tpl) != 0x0020AF30) {
        SDL_Log("art: %s has no textures to replace", model);
        free(own);
        return;
    }
    count = be32(tpl + 4);
    tab = be32(tpl + 8);
    tex = calloc(count, sizeof(NewTex));
    if (tex == NULL) {
        free(own);
        return;
    }
    for (index = 0; (art = portArtAssetIn(folder, index)) != NULL; index++) {
        const uint32_t k = (uint32_t)atoi(art + strlen(folder));
        if (k >= count) {
            SDL_Log("art: %s: %s has %u textures", art, model, count);
        } else if (tex[k].data == NULL) {
            encodeArt(art, &tex[k]);
        }
    }
    cap = 0xC + 8 * (size_t)count + 0x24 * (size_t)count + 64;
    for (i = 0; i < count; i++) {
        const uint32_t th = be32(tpl + tab + 8 * i);
        cap += (tex[i].data != NULL ? tex[i].size : texSize(be32(tpl + th) & 0xFFFF, be32(tpl + th) >> 16, be32(tpl + th + 4))) + 32;
    }
    out = calloc(1, cap);
    if (out != NULL) {
        put32(out, 0x0020AF30);
        put32(out + 4, count);
        put32(out + 8, 0xC);
        at = (0xC + 8 * (size_t)count + 0x24 * (size_t)count + 31) & ~(size_t)31;
        for (i = 0; i < count; i++) {
            const uint32_t th = be32(tpl + tab + 8 * i);
            const size_t hoff = 0xC + 8 * (size_t)count + 0x24 * (size_t)i;
            put32(out + 0xC + 8 * i, (uint32_t)hoff);
            memcpy(out + hoff, tpl + th, 0x24);
            at = (at + 31) & ~(size_t)31;
            put32(out + hoff + 8, (uint32_t)at);
            if (tex[i].data != NULL) {
                put32(out + hoff, tex[i].h << 16 | tex[i].w);
                put32(out + hoff + 4, tex[i].fmt);
                memcpy(out + at, tex[i].data, tex[i].size);
                at += tex[i].size;
            } else {
                const uint32_t size = texSize(be32(tpl + th) & 0xFFFF, be32(tpl + th) >> 16, be32(tpl + th + 4));
                memcpy(out + at, tpl + be32(tpl + th + 8), size);
                at += size;
            }
        }
        snprintf(path, sizeof(path), "/a/%s-", model);
        at = (at + 31) & ~(size_t)31;
        portModsSetHostFile(path, out, at);
        dump(name, out, at);
        free(out);
    }
    for (i = 0; i < count; i++) {
        free(tex[i].data);
    }
    free(tex);
    free(own);
}

void portSuperLuigiModelArt(void) {
    /* art/models_jp/: an image whose source is another on the Japanese disc (one image per path) */
    static const char* const kRoots[2] = {"art/models/", "art/models_jp/"};
    const Uint64 start = SDL_GetTicks();
    int nDone = 0, images = 0, k;
    for (k = 0; k < 2; k++) {
        const char* art;
        char done[128][33];
        int i, n = 0;
        for (i = 0; (art = portArtAssetIn(kRoots[k], i)) != NULL && n < 128; i++) {
            char model[33], folder[64];
            const char* rest = art + strlen(kRoots[k]);
            const char* slash = strchr(rest, '/');
            int j;
            images++;
            if (slash == NULL || (size_t)(slash - rest) >= sizeof(model)) {
                continue;
            }
            memcpy(model, rest, (size_t)(slash - rest));
            model[slash - rest] = 0;
            for (j = 0; j < n && strcmp(done[j], model) != 0; j++) {
            }
            if (j < n) {
                continue;
            }
            strcpy(done[n++], model);
            snprintf(folder, sizeof(folder), "%s%s/", kRoots[k], model);
            modelArt(model, folder);
        }
        nDone += n;
    }
    if (images) {
        SDL_Log("art: %d images onto %d models in %d ms", images, nDone, (int)(SDL_GetTicks() - start));
    }
}

void portSuperLuigiModelsInit(void) {
    const uint8_t* d = portSuperLuigiModels;
    uint32_t size = portSuperLuigiModels_size, n, i;
    if (size < 12 || memcmp(d, "LUFR", 4) != 0) {
        return;
    }
    n = be32(d + 8);
    for (i = 0; i < n && 12 + 0x28 * (uint64_t)(i + 1) <= size; i++) {
        const uint8_t* e = d + 12 + 0x28 * i;
        uint32_t off = be32(e + 32), len = be32(e + 36);
        char name[33], path[48];
        memcpy(name, e, 32);
        name[32] = 0;
        if (off + (uint64_t)len > size || name[0] == 0) {
            continue;
        }
        if (strchr(name, '@') != NULL) {
            variant(name, d + off, len);
            continue;
        }
        snprintf(path, sizeof(path), "/a/%s", name);
        portModsSetHostFile(path, d + off, len);
    }
}

/* ---- a model on a disc model's rig: its own motions by the names the chapter scripts use ---- */
const char* portSlPoseAlias(const char* file, const char* name) {
    static const struct {
        const char* model;
        const char* from;
        const char* to;
    } kAlias[] = {
        /* none now: Eclair has her own rig and poses (tools/port/sl_eclair.py). The empty row never matches. */
        {"", "", ""},
    };
    size_t i;
    if (file == NULL || (uintptr_t)name < 0x10000) {
        return name;
    }
    /* a disc tribe set on one of ours names its poses with the disc model's prefix: the Toad tribe on
     * the karts (chapter 3, for the talk) asks c_lp_kart for KNP_S_1, the Bob-omb tribe on Jerry asks
     * c_lp_bom for BOM_S_1; our models carry the bare names (S_1), so their first pose (Z_1) played */
    if (strncmp(file, "c_lp_", 5) == 0 && (strncmp(name, "KNP_", 4) == 0 || strncmp(name, "BOM_", 4) == 0)) {
        return name + 4;
    }
    /* the disc's Amazy Dayzee tribe names c_kpansy's poses with a _K (PAN_S_1_K) the model hasn't got,
     * so it stands in PAN_Z_1. Mario's game does the same and is left so; chapter 4's Dayzees (the
     * Choir's master, the Slam's, their battle NPCs as they're set up, run and flee) play the model's
     * own (PAN_S_1) */
    if (portSuperLuigiActive() && strcmp(file, "c_kpansy") == 0) {
        static const char* const kAmazy[][2] = {
            {"PAN_S_1_K", "PAN_S_1"}, {"PAN_Z_1_K", "PAN_Z_1"}, {"PAN_T_1_K", "PAN_T_1"}, {"PAN_W_1_K", "PAN_W_1"},
            {"PAN_R_1_K", "PAN_R_1"}, {"PAN_D_1_K", "PAN_D_1"}, {"PAN_K_1_K", "PAN_K_1"}, {"PAN_X_1_K", "PAN_X_1"},
        };
        for (i = 0; i < sizeof(kAmazy) / sizeof(kAmazy[0]); i++) {
            if (strcmp(name, kAmazy[i][0]) == 0) {
                return kAmazy[i][1];
            }
        }
    }
    for (i = 0; i < sizeof(kAlias) / sizeof(kAlias[0]); i++) {
        if (strcmp(name, kAlias[i].from) == 0 && strcmp(file, kAlias[i].model) == 0) {
            return kAlias[i].to;
        }
    }
    return name;
}

/* ---- $TTYD_SL_GALLERY (tests): models stood in the room, to see their cards in the game's own light ----
 * "NAME[:POSE[:X[:SCALE]]] ...": each drawn beside the player, X units along the room (default: side
 * by side), POSE (default S_1) played. portSlGalleryFrame runs each frame from the framework's hook. */
#include "driver/animdrv.h"
#include "driver/dispdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"

typedef struct GalleryEntry {
    s32 pose;
    f32 x, scale;
} GalleryEntry;

static GalleryEntry sGallery[16];
static int sGalleryN;
static char sGalleryMap[16];
static char sGalleryPose[16][24];

static void galleryDisp(CameraId camId, void* param) {
    GalleryEntry* e = (GalleryEntry*)param;
    MarioWork* mp = marioGetPtr();
    (void)camId;
    animPoseDraw(e->pose, mp->position.x + e->x, mp->position.y, mp->position.z + 12.0f, 0.0f, e->scale, 1);
    animPoseDraw(e->pose, mp->position.x + e->x, mp->position.y, mp->position.z + 12.0f, 0.0f, e->scale, 2);
    animPoseDraw(e->pose, mp->position.x + e->x, mp->position.y, mp->position.z + 12.0f, 0.0f, e->scale, 3);
}

void portSlGalleryFrame(int inField) {
    const char* v = SDL_getenv("TTYD_SL_GALLERY");
    int i;
    if (v == NULL || !inField || gp == NULL) {
        return;
    }
    if (strcmp(sGalleryMap, gp->currentMapName) != 0) { /* a new room: the old poses went with it */
        const char* p = v;
        snprintf(sGalleryMap, sizeof(sGalleryMap), "%s", gp->currentMapName);
        sGalleryN = 0;
        while (*p && sGalleryN < 16) {
            char item[96], name[40], pose[24] = "S_1";
            float x = 40.0f + 60.0f * sGalleryN, scale = 1.0f;
            size_t n = strcspn(p, " ");
            snprintf(item, sizeof(item), "%.*s", (int)n, p);
            p += n;
            p += *p == ' ';
            snprintf(name, sizeof(name), "%.*s", (int)strcspn(item, ":"), item);
            if (strchr(item, ':') != NULL) {
                const char* q = strchr(item, ':') + 1;
                snprintf(pose, sizeof(pose), "%.*s", (int)strcspn(q, ":"), q);
                q = strchr(q, ':');
                if (q != NULL) {
                    x = (float)atof(q + 1);
                    q = strchr(q + 1, ':');
                    if (q != NULL) {
                        scale = (float)atof(q + 1);
                    }
                }
            }
            snprintf(sGalleryPose[sGalleryN], sizeof(sGalleryPose[0]), "%s", pose);
            sGallery[sGalleryN].pose = animPoseEntry(name, 0);
            animPoseSetAnim(sGallery[sGalleryN].pose, pose, 1);
            sGallery[sGalleryN].x = x;
            sGallery[sGalleryN].scale = scale;
            sGalleryN++;
        }
    }
    {
        /* $TTYD_SL_GALLERY_LOOP=N (tests): the poses restart every N frames, to see a one-shot through */
        static int sLoopFrames;
        const char* loop = SDL_getenv("TTYD_SL_GALLERY_LOOP");
        if (loop != NULL && ++sLoopFrames >= atoi(loop)) {
            sLoopFrames = 0;
            for (i = 0; i < sGalleryN; i++) {
                animPoseSetAnim(sGallery[i].pose, sGalleryPose[i], 1);
            }
        }
    }
    if (SDL_getenv("TTYD_SL_GALLERY_SOLO") != NULL) {
        /* $TTYD_SL_GALLERY_SOLO (tests): the player and partner not drawn, so a lineup can stand Luigi
         * (c_luigi_pc) anywhere in it rather than at the camera's centre */
        MarioWork* mp = marioGetPtr();
        mp->dispFlags |= MARIO_DISP_FLAG_HIDDEN | MARIO_DISP_FLAG_NO_SHADOW;
        marioPartyKill();
    }
    for (i = 0; i < sGalleryN; i++) {
        animPoseMain(sGallery[i].pose);
        dispEntry(CAMERA_3D, 0.0f, 2, galleryDisp, &sGallery[i]);
    }
}

/* ---- $TTYD_SL_GALLERY2D (tests): "PATH:X:Y:SCALE ..." images of the art on the 2D camera, at their own size times
 * SCALE, as the game's overlays draw them (the fight's own Future Sight needs phase 2 of Crepe's battle) ---- */
#include <dolphin/gx.h>
int portSlAssetTexture(const char* path, void* texObj, int* width, int* height);

static void gallery2dDisp(CameraId camId, void* param) {
    const char* p = SDL_getenv("TTYD_SL_GALLERY2D");
    (void)camId;
    (void)param;
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetNumTevStages(1);
    GXSetNumIndStages(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    while (p != NULL && *p) {
        static const GXColor kWhite = {0xFF, 0xFF, 0xFF, 0xFF};
        char item[128], path[100];
        float x = 0.0f, y = 0.0f, s = 1.0f;
        size_t n = strcspn(p, " ");
        GXTexObj tex;
        int tw, th;
        snprintf(item, sizeof(item), "%.*s", (int)n, p);
        p += n;
        p += *p == ' ';
        if (sscanf(item, "%99[^:]:%f:%f:%f", path, &x, &y, &s) < 1 || !portSlAssetTexture(path, &tex, &tw, &th)) {
            continue;
        }
        GXSetChanMatColor(GX_COLOR0A0, kWhite);
        GXLoadTexObj(&tex, GX_TEXMAP0);
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(x - tw * s / 2, y + th * s / 2, 0.0f);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(x + tw * s / 2, y + th * s / 2, 0.0f);
        GXTexCoord2f32(1.0f, 0.0f);
        GXPosition3f32(x + tw * s / 2, y - th * s / 2, 0.0f);
        GXTexCoord2f32(1.0f, 1.0f);
        GXPosition3f32(x - tw * s / 2, y - th * s / 2, 0.0f);
        GXTexCoord2f32(0.0f, 1.0f);
        GXEnd();
    }
}

void mapGrpFlagOn(const char* name, u32 flags); /* driver/mapdrv.h */

void portSlGallery2dFrame(int inField) {
    /* $TTYD_SL_MAPHIDE=NAME,...: map groups hidden, to find which object something on screen is (tests) */
    const char* hide = SDL_getenv("TTYD_SL_MAPHIDE");
    static char sHidden[16];
    if (hide != NULL && inField && gp != NULL && strcmp(sHidden, gp->currentMapName) != 0) {
        snprintf(sHidden, sizeof(sHidden), "%s", gp->currentMapName);
        while (*hide) {
            char name[40];
            size_t n = strcspn(hide, ",");
            snprintf(name, sizeof(name), "%.*s", (int)n, hide);
            mapGrpFlagOn(name, 0x4000);
            hide += n;
            hide += *hide == ',';
        }
    }
    if (inField && SDL_getenv("TTYD_SL_GALLERY2D") != NULL) {
        dispEntry(CAMERA_2D, 1, 900.0f, gallery2dDisp, NULL);
    }
}
