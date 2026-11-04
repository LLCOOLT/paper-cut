/* Art recipes, the game's side (port/include/port/art_recipe.h; the format is
 * port/assets/luigi/art/RECIPES.md): the recipes built in with the drawn art
 * (art/recipes/*.recipe) run on the player's own disc when the game
 * starts, and their images are kept in the user folder
 * (<user>/art_cache/<game id>/<asset path>, a PNG and its .key), so later
 * starts read them back. The game then gets them as assets at those paths
 * (portArtAssetData), exactly where it gets the built-in ones.
 *
 * An image that can't be made (this disc lacks a source, or a recipe is
 * wrong) is logged and its `fallback` asset (drawn art) is used instead, or
 * the game's own where it has none. An image not made yet (Preparing skipped:
 * $TTYD_ART=lazy) is made at its first use. */
#include "port/art_recipe.h"

#include <dolphin/dvd.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECIPES "art/recipes/"
#define MAX_IMAGES 2048

typedef struct Image {
    char path[128];
    const char* text; /* its recipe file's (NUL-terminated copy) */
    int state;        /* 0 not tried, 1 made (data), 2 can't be made, 3 not made on this disc (`discs`) */
    uint8_t* data;    /* the PNG */
    unsigned int size;
    int saved;        /* in the cache (read from it, or kept there) */
} Image;

static Image sImages[MAX_IMAGES];
static int sCount;
static char sCacheDir[512]; /* <user>/art_cache/<game id> ("" : no cache) */
static char sGameId[8];
static PortArtDisc* sDisc;

/* ---- the disc ---- */

static uint8_t* readDisc(void* user, const char* path, uint32_t* size) {
    DVDFileInfo info;
    uint8_t* data;
    uint32_t len;
    (void)user;
    if (!DVDOpen(path, &info)) {
        return NULL;
    }
    *size = info.length;
    len = (*size + 31) & ~31u;
    data = (uint8_t*)malloc(len ? len : 32);
    if (data != NULL && DVDReadPrio(&info, data, (s32)len, 0, 2) < 0) {
        free(data);
        data = NULL;
    }
    DVDClose(&info);
    return data;
}

/* ---- the cache ---- */

/* FNV-1a 64 of the recipe file, the image, the disc and the runner: the image's key */
static uint64_t imageKey(const Image* im) {
    uint64_t h = 0xCBF29CE484222325ull;
    char extra[192];
    const char* parts[2];
    int k;
    snprintf(extra, sizeof(extra), "%s|%s|%d", im->path, sGameId, PORT_ART_RUNNER_VERSION);
    parts[0] = im->text;
    parts[1] = extra;
    for (k = 0; k < 2; k++) {
        const unsigned char* p;
        for (p = (const unsigned char*)parts[k]; *p; p++) {
            h = (h ^ *p) * 0x100000001B3ull;
        }
        h = (h ^ 0xFF) * 0x100000001B3ull;
    }
    return h;
}

static void cachePath(const Image* im, const char* ext, char* out, size_t size) {
    snprintf(out, size, "%s/%s%s", sCacheDir, im->path, ext);
}

static int fromCache(Image* im) {
    char path[768], key[32], want[32];
    size_t n = 0;
    void* k;
    if (!sCacheDir[0]) {
        return 0;
    }
    cachePath(im, ".key", path, sizeof(path));
    k = SDL_LoadFile(path, &n);
    if (k == NULL) {
        return 0;
    }
    snprintf(want, sizeof(want), "%016llx", (unsigned long long)imageKey(im));
    snprintf(key, sizeof(key), "%.*s", (int)(n < 31 ? n : 31), (const char*)k);
    SDL_free(k);
    if (strncmp(key, want, 16) != 0) {
        return 0;
    }
    cachePath(im, "", path, sizeof(path));
    k = SDL_LoadFile(path, &n);
    if (k == NULL) {
        return 0;
    }
    im->data = (uint8_t*)malloc(n);
    if (im->data == NULL) {
        SDL_free(k);
        return 0;
    }
    memcpy(im->data, k, n);
    im->size = (unsigned int)n;
    SDL_free(k);
    im->state = 1;
    im->saved = 1;
    return 1;
}

static void toCache(Image* im) {
    char path[768], dir[768], key[32];
    char* slash;
    if (!sCacheDir[0]) {
        return;
    }
    cachePath(im, "", path, sizeof(path));
    snprintf(dir, sizeof(dir), "%s", path);
    slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = 0;
    }
    if (!SDL_CreateDirectory(dir) || !SDL_SaveFile(path, im->data, im->size)) {
        SDL_Log("art: can't keep %s in %s (%s)", im->path, sCacheDir, SDL_GetError());
        return;
    }
    cachePath(im, ".key", path, sizeof(path));
    snprintf(key, sizeof(key), "%016llx\n", (unsigned long long)imageKey(im));
    im->saved = SDL_SaveFile(path, key, strlen(key));
}

/* ---- making ---- */

/* RGBA as a PNG (malloc'd) */
static uint8_t* toPng(const PortArtImage* img, unsigned int* size) {
    SDL_Surface* s = SDL_CreateSurfaceFrom(img->w, img->h, SDL_PIXELFORMAT_RGBA32, img->px, img->w * 4);
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    uint8_t* out = NULL;
    if (s != NULL && io != NULL && SDL_SavePNG_IO(s, io, false)) {
        const Sint64 n = SDL_GetIOSize(io);
        const void* mem = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
        if (n > 0 && mem != NULL && (out = (uint8_t*)malloc((size_t)n)) != NULL) {
            memcpy(out, mem, (size_t)n);
            *size = (unsigned int)n;
        }
    }
    if (io != NULL) {
        SDL_CloseIO(io);
    }
    SDL_DestroySurface(s);
    return out;
}

/* the open disc's id ("G8ME01"): which recipes are for it (`discs`), and the cache's folder */
static void gameId(void) {
    const DVDDiskID* id;
    if (sGameId[0]) {
        return;
    }
    id = DVDGetCurrentDiskID();
    memcpy(sGameId, id->gameName, 4);
    memcpy(sGameId + 4, id->company, 2);
    sGameId[6] = 0;
}

static void make(Image* im) {
    PortArtImage img;
    char err[256];
    if (im->state != 0) {
        return;
    }
    if (SDL_getenv("TTYD_ART") != NULL && strcmp(SDL_getenv("TTYD_ART"), "off") == 0) {
        im->state = 2; /* the fallback stands in */
        return;
    }
    gameId();
    if (fromCache(im)) {
        return;
    }
    im->state = 2;
    if (sDisc == NULL) {
        sDisc = portArtDiscOpen(readDisc, NULL, sGameId);
        if (sDisc == NULL) {
            return;
        }
    }
    switch (portArtMake(im->text, im->path, sDisc, &img, err, sizeof(err))) {
        case 1: break;
        case -1: im->state = 3; return;
        case -2: return; /* `drawn` on this disc: its fallback stands in (state 2), nothing to log */
        default:
            SDL_Log("art: %s can't be made from this disc (%s); %s", im->path, err, "its fallback stands in");
            return;
    }
    im->data = toPng(&img, &im->size);
    free(img.px);
    if (im->data == NULL) {
        SDL_Log("art: %s: no PNG (%s)", im->path, SDL_GetError());
        return;
    }
    im->state = 1;
    toCache(im);
}

static void addImage(void* user, const char* path) {
    const char* text = (const char*)user;
    int i;
    for (i = 0; i < sCount; i++) {
        if (strcmp(sImages[i].path, path) == 0) {
            SDL_Log("art: %s is made by two recipes; the first is used", path);
            return;
        }
    }
    if (sCount == MAX_IMAGES || strlen(path) >= sizeof(sImages[0].path)) {
        return;
    }
    snprintf(sImages[sCount].path, sizeof(sImages[0].path), "%s", path);
    sImages[sCount].text = text;
    sCount++;
}

/* every recipe file's images (once) */
static void collect(void) {
    static int sDone;
    const char* path;
    int i;
    if (sDone) {
        return;
    }
    sDone = 1;
    for (i = 0; (path = portArtBuiltInIn(RECIPES, i)) != NULL; i++) {
        unsigned int size = 0;
        const void* data;
        char* text;
        const size_t n = strlen(path);
        if (strchr(path + strlen(RECIPES), '/') != NULL || n < 7 || strcmp(path + n - 7, ".recipe") != 0) {
            continue;
        }
        data = portArtBuiltIn(path, &size);
        text = data != NULL ? (char*)malloc((size_t)size + 1) : NULL;
        if (text == NULL) {
            continue;
        }
        memcpy(text, data, size);
        text[size] = 0;
        portArtImages(text, addImage, text);
    }
}

void portArtPrepare(const char* userPath) {
    const char* mode = SDL_getenv("TTYD_ART");
    const Uint64 start = SDL_GetTicks();
    int i, made = 0, kept = 0, failed = 0, other = 0;
    gameId();
    if (userPath != NULL && userPath[0]) {
        const size_t n = strlen(userPath);
        snprintf(sCacheDir, sizeof(sCacheDir), "%s%sart_cache/%s", userPath,
                 userPath[n - 1] == '/' || userPath[n - 1] == '\\' ? "" : "/", sGameId);
    }
    collect();
    if (mode != NULL && strcmp(mode, "off") == 0) {
        SDL_Log("art: recipes off ($TTYD_ART): the drawn art stands in");
        return;
    }
    if (mode != NULL && strcmp(mode, "lazy") == 0) {
        for (i = 0; i < sCount; i++) {
            if (sImages[i].state == 1 && !sImages[i].saved) {
                toCache(&sImages[i]); /* made before this, at a first use */
            }
        }
        return;
    }
    for (i = 0; i < sCount; i++) {
        const int cached = sImages[i].state == 0 && fromCache(&sImages[i]);
        make(&sImages[i]);
        /* (made before this, at a first use: kept now that the cache's folder is known) */
        if (sImages[i].state == 1 && !sImages[i].saved) {
            toCache(&sImages[i]);
        }
        kept += cached;
        made += !cached && sImages[i].state == 1;
        failed += sImages[i].state == 2;
        other += sImages[i].state == 3;
    }
    portArtDiscClose(sDisc);
    sDisc = NULL;
    SDL_Log("art: %d images from the disc's (%d made, %d kept from before, %d not made, %d for other discs) in %d ms",
            sCount, made, kept, failed, other, (int)(SDL_GetTicks() - start));
}

/* an image's PNG: made (now, if it wasn't), or its fallback's; NULL if neither or not on this disc */
static const void* imageData(Image* im, unsigned int* size) {
    char fallback[128];
    make(im);
    if (im->state == 1) {
        if (size != NULL) {
            *size = im->size;
        }
        return im->data;
    }
    if (im->state == 2 && portArtFallback(im->text, im->path, fallback, sizeof(fallback))) {
        return portArtBuiltIn(fallback, size);
    }
    return NULL;
}

int portArtAsset(const char* path, const void** data, unsigned int* size) {
    int i;
    collect();
    for (i = 0; i < sCount; i++) {
        Image* im = &sImages[i];
        if (strcmp(im->path, path) != 0) {
            continue;
        }
        *data = imageData(im, size);
        /* not made on this disc: as if there were no recipe (a built-in asset of that name, or none) */
        return im->state != 3;
    }
    return 0;
}

const char* portArtAssetIn(const char* folder, int index) {
    const size_t n = strlen(folder);
    int i;
    collect();
    for (i = 0; i < sCount; i++) {
        /* only those there are (made, or a fallback) */
        if (strncmp(sImages[i].path, folder, n) == 0 && imageData(&sImages[i], NULL) != NULL && index-- == 0) {
            return sImages[i].path;
        }
    }
    return NULL;
}
