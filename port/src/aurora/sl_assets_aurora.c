/* Super Luigi's own art (port/include/port/superluigi.h, "Our own art"): the
 * PNGs of port/assets/super_luigi's textures/ and art/ (the title cut-outs,
 * the friends' faces), built into the executable by CMakeLists.txt
 * (generated/sl_assets.c), and made into GX textures for the game's drawing:
 *  - the friends' faces in the partner icons (icondrv.c asks for every icon:
 *    while a friend stands in a partner's slot, that partner's face icons, in
 *    the HUD, the menus and the battle menu, are his);
 *  - the chapter card's cut-outs (rels/sys/src/sys.c, Mario's chapter card,
 *    which Luigi's chapters play too): they pop up from the bottom of the
 *    card like a storybook's. */
#include "port/superluigi.h"
#include "data/item_data.h"
#include "mario/mario_pouch.h"
#include "port/art_recipe.h"
#include "port/luigi.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "mario/mariost.h"
#include "icon_ids.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "version.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const struct {
    const char* path;
    const unsigned char* data;
    unsigned int size;
} portSlAssetTable[];

/* an asset: an image a recipe made from the player's disc (art_recipes_aurora.c), or a built-in one */
const void* portSlAsset(const char* path, unsigned int* size) {
    const void* data = NULL;
    if (path != NULL && portArtAsset(path, &data, size)) {
        return data;
    }
    return portSlAssetBuiltIn(path, size);
}

const void* portSlAssetBuiltIn(const char* path, unsigned int* size) {
    int i;
    for (i = 0; path != NULL && portSlAssetTable[i].path != NULL; i++) {
        if (strcmp(portSlAssetTable[i].path, path) == 0) {
            if (size != NULL) {
                *size = portSlAssetTable[i].size;
            }
            return portSlAssetTable[i].data;
        }
    }
    return NULL;
}

const char* portSlAssetIn(const char* folder, int index) {
    const size_t n = strlen(folder);
    int i;
    for (i = 0; portSlAssetTable[i].path != NULL; i++) {
        if (strncmp(portSlAssetTable[i].path, folder, n) == 0 && index-- == 0) {
            return portSlAssetTable[i].path;
        }
    }
    /* then the recipes' images there */
    return portArtAssetIn(folder, index);
}

/* --- RGBA pixels to a GX texture --- */

#define TEXTURES 64

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

static int keep(const char* key, void* data, int w, int h) {
    if (data == NULL || sTexCount == TEXTURES) {
        return -1;
    }
    snprintf(sTex[sTexCount].key, sizeof(sTex[0].key), "%s", key);
    sTex[sTexCount].data = data;
    sTex[sTexCount].w = w;
    sTex[sTexCount].h = h;
    return sTexCount++;
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

static int loadPng(const char* path) {
    unsigned int size = 0;
    const void* png = portSlAsset(path, &size);
    SDL_Surface* s;
    SDL_Surface* c;
    int i;
    if (png == NULL) {
        return -1;
    }
    s = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
    if (s == NULL) {
        printf("[superluigi] %s: not a PNG (%s)\n", path, SDL_GetError());
        fflush(stdout);
        return -1;
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (c == NULL) {
        return -1;
    }
    i = keep(path, toGx((const uint8_t*)c->pixels, c->w, c->h, c->pitch), c->w, c->h);
    SDL_DestroySurface(c);
    return i;
}

static void texObj(int i, GXTexObj* out) {
    GXInitTexObj(out, sTex[i].data, (u16)sTex[i].w, (u16)sTex[i].h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(out, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

int portSlAssetTexture(const char* path, void* out, int* width, int* height) {
    int i = find(path);
    if (i < 0) {
        i = loadPng(path);
    }
    if (i < 0) {
        return 0;
    }
    texObj(i, (GXTexObj*)out);
    if (width != NULL) {
        *width = sTex[i].w;
    }
    if (height != NULL) {
        *height = sTex[i].h;
    }
    return 1;
}

/* --- the friends' faces --- */

/* a face to stand in until the art comes: a round head in the friend's colour, two eyes */
static int placeholderFace(int friendId) {
    static const uint32_t kColour[PORT_SL_FRIENDS] = {0xC8C8C8, 0xF4F4F8, 0x3A4060, 0x8C8C98, 0x78B848, 0x9070C0};
    uint8_t px[32 * 32 * 4];
    char key[32];
    int x, y, i;
    snprintf(key, sizeof(key), "face:%d", friendId);
    if ((i = find(key)) >= 0) {
        return i;
    }
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            uint8_t* p = px + (y * 32 + x) * 4;
            const float dx = (float)x - 15.5f, dy = (float)y - 14.5f;
            const float r = sqrtf(dx * dx + dy * dy);
            const uint32_t c = kColour[friendId > 0 && friendId < PORT_SL_FRIENDS ? friendId : 0];
            const int eye = (fabsf(fabsf(dx) - 5.0f) < 2.0f && fabsf(dy + 1.0f) < 3.5f);
            if (r > 14.0f) {
                p[0] = p[1] = p[2] = p[3] = 0;
            } else if (r > 12.5f || eye) {
                p[0] = 0x30, p[1] = 0x28, p[2] = 0x30, p[3] = 0xFF;
            } else {
                p[0] = (uint8_t)(c >> 16), p[1] = (uint8_t)(c >> 8), p[2] = (uint8_t)c, p[3] = 0xFF;
            }
        }
    }
    return keep(key, toGx(px, 32, 32, 32 * 4), 32, 32);
}

/* each partner slot's face icons: the menus' and battle's, and the HUD's */
static const struct {
    int partyId;
    u16 menu, hud;
} kFaces[] = {
    {1, ICON_155, ICON_1A4}, {2, ICON_156, ICON_1A5}, {3, ICON_157, ICON_1A6},
    {5, ICON_15F, ICON_1AE}, {6, ICON_160, ICON_1AF}, {7, ICON_161, ICON_1B0},
};

/* a friend's faces for a while (chapter 6's sooty Blooey): menu, HUD; NULL puts his own back */
static const char* sFaceOverride[PORT_SL_FRIENDS][2];

void portSlFriendIcons(int friendId, const char* menu, const char* hud) {
    if (friendId > 0 && friendId < PORT_SL_FRIENDS) {
        sFaceOverride[friendId][0] = menu;
        sFaceOverride[friendId][1] = hud;
    }
}

/* Mario's own faces, Luigi's while he's the player (Super Luigi, Play as
 * Luigi): the game has none of him, so they're made from Mario's on the
 * player's disc at the start (art/recipes/luigi_icons.recipe, art_recipes_aurora.c) */
static const struct {
    u16 icon;
    const char* path;
} kLuigiFaces[] = {
    {ICON_1A3, "art/icons/luigi/luigi_hud.png"},    /* the HP face: the status bar, field and battle; Sweet Treat */
    {ICON_19E, "art/icons/luigi/luigi_head.png"},   /* the pause menu's Mario page, the item menu's "use on" */
    {ICON_1A1, "art/icons/luigi/luigi_emblem.png"}, /* the Mario page's level row, the status tables */
};

int portIconTexOverride(unsigned int iconId, void* out) {
    size_t k;
    if (portLuigiOn()) {
        for (k = 0; k < sizeof(kLuigiFaces) / sizeof(kLuigiFaces[0]); k++) {
            if (iconId == kLuigiFaces[k].icon) {
                int i = find(kLuigiFaces[k].path);
                if (i < 0) {
                    i = loadPng(kLuigiFaces[k].path);
                }
                if (i < 0) {
                    return 0;
                }
                texObj(i, (GXTexObj*)out);
                return 1;
            }
        }
    }
    if (!portSuperLuigiActive()) {
        return 0;
    }
    /* chapter 5's Button, held up in place of an unused key item (sl5_got_button) */
    if (portSuperLuigiChapter() == 5 && iconId == (unsigned int)itemDataTable[ITEM_WEDDING_RING].icon_id) {
        int i = find("art/icons/button.png");
        if (i < 0) {
            i = loadPng("art/icons/button.png");
        }
        if (i >= 0) {
            texObj(i, (GXTexObj*)out);
            return 1;
        }
    }
    for (k = 0; k < sizeof(kFaces) / sizeof(kFaces[0]); k++) {
        if (iconId == kFaces[k].menu || iconId == kFaces[k].hud) {
            const PortSlFriend* f = portSlFriendInSlot(kFaces[k].partyId);
            const char* path;
            int i = -1;
            if (f == NULL) {
                return 0;
            }
            path = iconId == kFaces[k].hud && f->iconHud != NULL ? f->iconHud : f->iconMenu;
            if (f->id > 0 && f->id < PORT_SL_FRIENDS && sFaceOverride[f->id][0] != NULL) {
                path = sFaceOverride[f->id][iconId == kFaces[k].hud && sFaceOverride[f->id][1] != NULL ? 1 : 0];
            }
            if (path != NULL && (i = find(path)) < 0) {
                i = loadPng(path);
            }
            if (i < 0) {
                i = placeholderFace(f->id);
            }
            if (i < 0) {
                return 0;
            }
            texObj(i, (GXTexObj*)out);
            return 1;
        }
    }
    return 0;
}

/* in: a friend, his faces for now (asset paths: menu, HUD), or 0 0 for his own */
USER_FUNC(evt_sl_friend_icons) {
    EvtWord* a = event->args;
    (void)isFirstCall;
    portSlFriendIcons((int)evtGetValue(event, a[0]), (const char*)evtGetValue(event, a[1]),
                      (const char*)evtGetValue(event, a[2]));
    return EVT_RETURN_DONE;
}

