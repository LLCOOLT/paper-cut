/* The Start menu's Modes hub (port/include/port/modes_hub.h): which hub
 * button each group of the Modes tab is under, the buttons' names, and their
 * pictures. The pictures are the art recipes' (port/assets/luigi/art/recipes/
 * modes_hub.recipe), made from the player's own disc at Preparing graphics:
 * the game's own icons as stickers. Here they become GX textures, once, kept
 * for the run (outside the Start menu's statics: the second screen's menu
 * draws them too). */
#include "port/modes_hub.h"
#include "port/art_recipe.h"
#include "icon_ids.h"
#include <dolphin/gx.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* The hub's buttons, by the Modes tab's groups (port/settings/settings_layout.cpp), in its order.
 * fold: the group's rows go under another group's button (a group too small for a button of its
 * own); name: a shorter name for the button (NULL: the group's); picture: the recipe's image;
 * icon: the game's icon drawn instead when the picture can't be made.
 * New groups (a new mode's) add a line here; without one they still get a button. */
static const struct {
    const char* group;
    const char* fold;
    const char* name;
    const char* picture;
    int icon;
} kButtons[] = {
    {"Party games", NULL, NULL, "art/menu/modes/party.png", ICON_19E},
    {"Battles", NULL, NULL, "art/menu/modes/battles.png", ICON_09D},
    {"After the story", NULL, NULL, "art/menu/modes/after.png", ICON_19B},
    {"Every day", NULL, NULL, "art/menu/modes/daily.png", ICON_112},
    {"New ways through the story", NULL, "Story twists", "art/menu/modes/story.png", ICON_132},
    {"Just for fun", NULL, NULL, "art/menu/modes/fun.png", ICON_184},
    /* the Director's Cut journal: with the Director's Cut itself */
    {"Journal", "New ways through the story", NULL, NULL, -1},
};
#define BUTTONS ((int)(sizeof(kButtons) / sizeof(kButtons[0])))

static int entry(const char* group) {
    int i;
    for (i = 0; group != NULL && i < BUTTONS; i++) {
        if (strcmp(kButtons[i].group, group) == 0) {
            return i;
        }
    }
    return -1;
}

const char* portModesHubButton(const char* group) {
    const int i = entry(group);
    return i >= 0 && kButtons[i].fold != NULL ? kButtons[i].fold : group;
}

const char* portModesHubName(const char* button) {
    const int i = entry(button);
    return i >= 0 && kButtons[i].name != NULL ? kButtons[i].name : button;
}

/* --- the pictures: each recipe image's PNG as a GX RGBA8 texture, decoded at its first use --- */

static struct {
    void* data; /* GX_TF_RGBA8, 4x4 tiles */
    int w, h;
    int tried;
} sPictures[16];

/* rgba (rows of pitch bytes) as GX RGBA8: each 4x4 tile 32 bytes of alpha-red pairs, then 32 of green-blue */
static void* toGx(const Uint8* rgba, int w, int h, int pitch) {
    const int tw = (w + 3) / 4, th = (h + 3) / 4;
    Uint8* out = (Uint8*)SDL_aligned_alloc(32, (size_t)tw * th * 64);
    int tx, ty, x, y;
    if (out == NULL) {
        return NULL;
    }
    for (ty = 0; ty < th; ty++) {
        for (tx = 0; tx < tw; tx++) {
            Uint8* tile = out + ((size_t)ty * tw + tx) * 64;
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    const int px = tx * 4 + x, py = ty * 4 + y, k = y * 4 + x;
                    const Uint8* p = px < w && py < h ? rgba + (size_t)py * pitch + (size_t)px * 4 : NULL;
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

static void load(int i) {
    unsigned int size = 0;
    const void* png;
    SDL_Surface* s;
    SDL_Surface* c;
    sPictures[i].tried = 1;
    if (kButtons[i].picture == NULL || (png = portArtAssetData(kButtons[i].picture, &size)) == NULL) {
        return;
    }
    s = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
    if (s == NULL) {
        SDL_Log("modes hub: %s: not a PNG (%s)", kButtons[i].picture, SDL_GetError());
        return;
    }
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (c == NULL) {
        return;
    }
    sPictures[i].data = toGx((const Uint8*)c->pixels, c->w, c->h, c->pitch);
    sPictures[i].w = c->w;
    sPictures[i].h = c->h;
    SDL_DestroySurface(c);
}

int portModesHubPicture(const char* button, void* texObj, float* w, float* h, int* icon) {
    const int i = entry(button);
    *icon = i >= 0 ? kButtons[i].icon : -1;
    if (i < 0 || i >= (int)(sizeof(sPictures) / sizeof(sPictures[0]))) {
        return 0;
    }
    if (!sPictures[i].tried) {
        load(i);
    }
    if (sPictures[i].data == NULL) {
        return 0;
    }
    GXInitTexObj((GXTexObj*)texObj, sPictures[i].data, (u16)sPictures[i].w, (u16)sPictures[i].h, GX_TF_RGBA8, GX_CLAMP,
                 GX_CLAMP, GX_FALSE);
    /* the stickers' texels as the game's icons draw theirs */
    GXInitTexObjLOD((GXTexObj*)texObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    *w = (float)sPictures[i].w;
    *h = (float)sPictures[i].h;
    return 1;
}
