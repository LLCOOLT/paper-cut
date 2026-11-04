#ifndef PORT_ART_RECIPE_H
#define PORT_ART_RECIPE_H
/* Art recipes (port/assets/luigi/art/RECIPES.md): images made from the
 * player's own disc, so the repo holds only the recipe. The runner is plain C
 * on RGBA buffers (port/src/common/art_recipe.c); the game's side, which reads
 * the disc, keeps the results in the user folder and hands them to the game
 * as assets, is port/src/aurora/art_recipes_aurora.c. The authoring tool
 * port/assets/luigi/art/tools/recipes.py runs the same format with the
 * same arithmetic: both make the same pixels (tests/port/art_recipe_check.py). */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortArtImage {
    int w, h;
    uint8_t* px; /* RGBA8, rows of w * 4 bytes, malloc'd */
} PortArtImage;

/* a disc file's bytes (malloc'd; *size set), or NULL if it isn't there */
typedef uint8_t* (*PortArtReadFn)(void* user, const char* path, uint32_t* size);

/* The disc the recipes read (its files are kept until closed). */
typedef struct PortArtDisc PortArtDisc;
/* gameId: the disc's six-character id ("G8ME01"), for recipes made only on some discs (`discs`) */
PortArtDisc* portArtDiscOpen(PortArtReadFn read, void* user, const char* gameId);
void portArtDiscClose(PortArtDisc* disc);

/* The images a recipe file makes: fn(user, path) for each, in order. Returns their count. */
int portArtImages(const char* text, void (*fn)(void* user, const char* path), void* user);
/* An image's `fallback` (the asset to use if it can't be made): 1 and out set, or 0. */
int portArtFallback(const char* text, const char* path, char* out, size_t size);
/* Makes image `path` of a recipe file: 1 and *out set (free out->px); 0 and
 * why in err ("line N: ..."); or -1 if it isn't made on this disc (`discs`). */
int portArtMake(const char* text, const char* path, PortArtDisc* disc, PortArtImage* out, char* err, size_t errSize);
/* The runner's version: part of each cached image's key, so a change to the
 * runner's arithmetic makes every image again. */
#define PORT_ART_RUNNER_VERSION 1

/* ---- the game's side (port/src/aurora/art_recipes_aurora.c) ----
 * The recipes are port/assets/luigi/art/recipes/<name>.recipe, built in with the
 * drawn art (port/src/aurora/luigi_faces_aurora.c); each image they make is
 * an asset at its path, as a PNG, which portArtAssetData hands out like a
 * built-in one. */

/* "Preparing graphics": every recipe's image made from the open disc, or read
 * back from <userPath>/art_cache/<game id>/ where an earlier start kept it.
 * $TTYD_ART=lazy: none now, each at its first use; =off: none (each image's
 * fallback, the drawn art). */
void portArtPrepare(const char* userPath);
/* 1 if path is a recipe's image: *data is its PNG (made now if it wasn't),
 * or its fallback's if it can't be made, or NULL if it has none. 0: not a
 * recipe's (a built-in asset, or nothing). */
int portArtAsset(const char* path, const void** data, unsigned int* size);
/* The i-th recipe image under a folder ("art/icons/luigi/ui/"), or NULL past the last */
const char* portArtAssetIn(const char* folder, int index);

/* ---- the built-in art (port/src/aurora/luigi_faces_aurora.c) ----
 * port/assets/luigi/art's recipes and drawn PNGs, built into the executable
 * by CMakeLists.txt (generated/luigi_art.c), by their path under
 * port/assets/luigi ("art/recipes/luigi_icons.recipe"). */

/* An asset: a recipe's image made from the player's disc, or a built-in one; NULL if neither */
const void* portArtAssetData(const char* path, unsigned int* size);
/* A built-in asset only: the recipes' text, the fallbacks */
const void* portArtBuiltIn(const char* path, unsigned int* size);
/* The i-th built-in asset under a folder ("art/recipes/"), then the recipes' images there; NULL past the last */
const char* portArtBuiltInIn(const char* folder, int index);

#ifdef __cplusplus
}
#endif
#endif
