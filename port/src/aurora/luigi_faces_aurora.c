/* Luigi's faces in Play as Luigi (port/include/port/luigi.h, docs/PLAYABLE_LUIGI.md):
 * the game has no icons of him, so its icons of Mario's head are made at the
 * start from Luigi's own head on the player's disc by the art recipes
 * (port/assets/luigi/art/recipes/luigi_icons.recipe, art_recipes_aurora.c),
 * with drawn ones (port/assets/luigi/art/icons/luigi/drawn/) standing in where
 * they can't be made. While the option is on, icondrv.c draws them in place
 * of Mario's (portIconTexOverride), and level-up's BP panel is his through
 * the "*#luigi" texture set. With the option off, nothing here changes a
 * texel: the game's own icons.
 *
 * Also the built-in art: port/assets/luigi/art's recipes and PNGs, built into
 * the executable by CMakeLists.txt (generated/luigi_art.c). */
#include "port/art_recipe.h"
#include "port/cosmetics.h"
#include "port/luigi.h"
#include "port/mods.h"
#include "icon_ids.h"
#include <dolphin/gx.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct {
    const char* path;
    const unsigned char* data;
    unsigned int size;
} portLuigiArtTable[];

/* --- the assets --- */

const void* portArtAssetData(const char* path, unsigned int* size) {
    const void* data = NULL;
    if (path != NULL && portArtAsset(path, &data, size)) {
        return data;
    }
    return portArtBuiltIn(path, size);
}

const void* portArtBuiltIn(const char* path, unsigned int* size) {
    int i;
    for (i = 0; path != NULL && portLuigiArtTable[i].path != NULL; i++) {
        if (strcmp(portLuigiArtTable[i].path, path) == 0) {
            if (size != NULL) {
                *size = portLuigiArtTable[i].size;
            }
            return portLuigiArtTable[i].data;
        }
    }
    return NULL;
}

const char* portArtBuiltInIn(const char* folder, int index) {
    const size_t n = strlen(folder);
    int i;
    for (i = 0; portLuigiArtTable[i].path != NULL; i++) {
        if (strncmp(portLuigiArtTable[i].path, folder, n) == 0 && index-- == 0) {
            return portLuigiArtTable[i].path;
        }
    }
    /* then the recipes' images there */
    return portArtAssetIn(folder, index);
}

/* --- PNGs to GX textures --- */

#define TEXTURES 16

static struct {
    char key[96];
    void* data; /* GX_TF_RGBA8, 4x4 tiles */
    int w, h;
} sTex[TEXTURES];
static int sTexCount;

/* rgba (w x h, 4 bytes a pixel, rows of pitch bytes) as GX RGBA8: each 4x4 tile is
 * 32 bytes of alpha-red pairs, then 32 of green-blue */
static void* toGx(const uint8_t* rgba, int w, int h, int pitch) {
    const int tw = (w + 3) / 4, th = (h + 3) / 4;
    uint8_t* out = (uint8_t*)SDL_aligned_alloc(32, (size_t)tw * th * 64);
    int tx, ty, x, y;
    if (out == NULL) {
        return NULL;
    }
    for (ty = 0; ty < th; ty++) {
        for (tx = 0; tx < tw; tx++) {
            uint8_t* tile = out + ((size_t)ty * tw + tx) * 64;
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    const int px = tx * 4 + x, py = ty * 4 + y, k = y * 4 + x;
                    const uint8_t* p = px < w && py < h ? rgba + (size_t)py * pitch + (size_t)px * 4 : NULL;
                    tile[k * 2] = p != NULL ? p[3] : 0;
                    tile[k * 2 + 1] = p != NULL ? p[0] : 0;
                    tile[32 + k * 2] = p != NULL ? p[1] : 0;
                    tile[32 + k * 2 + 1] = p != NULL ? p[2] : 0;
                }
            }
        }
    }
    return out;
}

static int find(const char* key) {
    int i;
    for (i = 0; i < sTexCount; i++) {
        if (strcmp(sTex[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

/* the asset's PNG as a texture, decoded once and kept: its index, or -1 */
static int texture(const char* path) {
    unsigned int size = 0;
    const void* png;
    SDL_Surface* s;
    SDL_Surface* c;
    int i = find(path);
    if (i >= 0) {
        return i;
    }
    if (sTexCount == TEXTURES || (png = portArtAssetData(path, &size)) == NULL) {
        return -1;
    }
    s = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
    if (s == NULL) {
        SDL_Log("luigi: %s: not a PNG (%s)", path, SDL_GetError());
        return -1;
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (c == NULL) {
        return -1;
    }
    sTex[sTexCount].data = toGx((const uint8_t*)c->pixels, c->w, c->h, c->pitch);
    if (sTex[sTexCount].data == NULL) {
        SDL_DestroySurface(c);
        return -1;
    }
    snprintf(sTex[sTexCount].key, sizeof(sTex[0].key), "%s", path);
    sTex[sTexCount].w = c->w;
    sTex[sTexCount].h = c->h;
    SDL_DestroySurface(c);
    return sTexCount++;
}

/* --- the faces --- */

/* Mario's own faces, Luigi's while the option is on */
static const struct {
    u16 icon;
    const char* path;
} kLuigiFaces[PORT_PLAY_AS_COUNT][3] = {
    {{0, NULL}, {0, NULL}, {0, NULL}},
    {
        {ICON_1A3, "art/icons/luigi/luigi_hud.png"},    /* the HP face: the status bar, field and battle; Sweet Treat */
        {ICON_19E, "art/icons/luigi/luigi_head.png"},   /* the pause menu's Mario page, the item menu's "use on" */
        {ICON_1A1, "art/icons/luigi/luigi_emblem.png"}, /* the Mario page's level row, the status tables */
    },
    /* Play as Peach or Bowser: their heads from their own models (peach_icons.recipe, bowser_icons.recipe) */
    {
        {ICON_1A3, "art/icons/peach/peach_hud.png"},
        {ICON_19E, "art/icons/peach/peach_head.png"},
        {ICON_1A1, "art/icons/peach/peach_emblem.png"},
    },
    {
        {ICON_1A3, "art/icons/bowser/bowser_hud.png"},
        {ICON_19E, "art/icons/bowser/bowser_head.png"},
        {ICON_1A1, "art/icons/bowser/bowser_emblem.png"},
    },
};

static int sFaceForce = -1;

void portLuigiFaceForce(int luigi) {
    sFaceForce = luigi;
}

int portIconTexOverride(unsigned int iconId, void* out) {
    size_t k;
    /* (forced: a versus battle's Luigi side; otherwise whoever the text names,
     * portPlayAsText: Mario in versus and with Brothers mode on) */
    const int chr = sFaceForce > 0 ? PORT_PLAY_AS_LUIGI : sFaceForce == 0 ? PORT_PLAY_AS_MARIO : portPlayAsText();
    if (chr == PORT_PLAY_AS_MARIO) {
        return 0;
    }
    for (k = 0; k < 3; k++) {
        if (iconId == kLuigiFaces[chr][k].icon) {
            const int i = texture(kLuigiFaces[chr][k].path);
            if (i < 0) {
                return 0;
            }
            GXInitTexObj((GXTexObj*)out, sTex[i].data, (u16)sTex[i].w, (u16)sTex[i].h, GX_TF_RGBA8, GX_CLAMP,
                         GX_CLAMP, GX_FALSE);
            GXInitTexObjLOD((GXTexObj*)out, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
            return 1;
        }
    }
    return 0;
}

/* the Wardrobe's colours on the pick's faces (cosmetics.cpp recolours them by
 * their texels' address, as it does the game's icons) */
int portPlayAsFaceTextures(int chr, PortTexInfo* out, int max) {
    int k, n = 0;
    if (chr <= PORT_PLAY_AS_MARIO || chr >= PORT_PLAY_AS_COUNT) {
        return 0;
    }
    for (k = 0; k < 3 && n < max; k++) {
        const int i = texture(kLuigiFaces[chr][k].path);
        if (i < 0) {
            continue;
        }
        out[n].data = sTex[i].data;
        out[n].width = sTex[i].w;
        out[n].height = sTex[i].h;
        out[n].format = GX_TF_RGBA8;
        out[n].tlut = NULL;
        out[n].tlutFormat = 0;
        out[n].tlutCount = 0;
        n++;
    }
    return n;
}

/* a folder's images (named as their texture's replacement, tex1_...) in the map-scoped texture set pattern */
static void mapTextures(const char* pattern, const char* folder) {
    const char* path;
    int i;
    for (i = 0; (path = portArtBuiltInIn(folder, i)) != NULL; i++) {
        unsigned int size = 0;
        const void* data = portArtAssetData(path, &size);
        if (data != NULL && strstr(path, "tex1_") != NULL) {
            portModsAddMapTexture(pattern, path, data, size);
        }
    }
}

void portLuigiFacesInit(void) {
    /* level-up's BP panel (battle_common.tpl image 51; 38 on the JP disc, the same data), in every map
     * while the option is on: mod_game_aurora.c names the map with "#luigi" then */
    mapTextures("*#luigi", "art/icons/luigi/ui/");
    mapTextures("*#luigi", "art/icons/luigi/ui_jp/");
    /* Peach's and Bowser's (their recipes' level-up panel), "#peach" / "#bowser" likewise */
    mapTextures("*#peach", "art/icons/peach/ui/");
    mapTextures("*#peach", "art/icons/peach/ui_jp/");
    mapTextures("*#bowser", "art/icons/bowser/ui/");
    mapTextures("*#bowser", "art/icons/bowser/ui_jp/");
}
