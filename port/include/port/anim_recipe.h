#ifndef PORT_ANIM_RECIPE_H
#define PORT_ANIM_RECIPE_H

/* New animations for a model, made from its own disc animations by a recipe (port/src/common/anim_recipe_pc.c;
 * tools/port/anim_recipe.py, docs/LUIGI_ANIMS.md "New animations from a recipe"). A recipe is our own work
 * (names and numbers); what it makes carries the player's disc's joints and vertex morphs, so Play as makes it
 * at startup from the disc and nothing of it is in the repository. Byte for byte what anim_recipe.py's
 * make_pick makes (tests/port/anim_recipe_check.py). The tables: anim_recipe.py cinc. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a key of an offset track of ours: frame, value, ease (0 smooth, 1 linear, 2 step) */
typedef struct PortRecipePoint {
    double t, v;
    int ease;
} PortRecipePoint;

/* an offset track: on a group of the model (by name), a channel (0..8: tx ty tz added, sx sy sz multiplied,
 * rx ry rz added in degrees), its points */
typedef struct PortRecipeTrack {
    const char* group;
    int channel;
    int first, count;
} PortRecipeTrack;

/* one new animation: from a disc animation, the part of it (range, else its loop range), its length (else the
 * part's), loop (else the source's), turns (0: as many as fit a loop, once a one-shot), its tracks */
typedef struct PortRecipeAnim {
    const char* name;
    const char* from;
    int hasRange;
    double a, b;
    int hasEnd;
    double end;
    int hasLoop;
    int loop;
    int turns;
    int firstTrack, nTracks;
    const char* fromModel; /* NULL: `from` is this model's; else that model's (the same rig: Sushie from c_puku) */
} PortRecipeAnim;

typedef struct PortRecipe {
    const char* model; /* the disc's model it is for ("c_pkr") */
    const PortRecipeAnim* anims;
    int nAnims;
    const PortRecipeTrack* tracks;
    const PortRecipePoint* points;
} PortRecipe;

/* The disc's model file (model, size) with the recipe's animations added (one of the same name replaced),
 * named `name` (its file's name; its textures stay the disc's, so the Wardrobe's colours of the character are
 * its too): a malloc'd file in *out (*outSize bytes, 32-aligned). 1, or 0 with
 * why (a source animation or group the model hasn't, out of memory). Not checked here: anim_check.h is. */
int portAnimRecipeMake(const uint8_t* model, uint32_t size, const PortRecipe* recipe, const char* name, uint8_t** out,
                       uint32_t* outSize, char* why, size_t whySize);

/* where a recipe's "from_model" comes from: the disc's a/<model>, malloc'd (the maker frees it), NULL if missing */
void portAnimRecipeSetLoader(uint8_t* (*load)(const char* model, uint32_t* size));

/* the recipe's animation `anim` is one it makes (anim_check.h's strict test) */
int portAnimRecipeHas(const PortRecipe* recipe, const char* anim);

#ifdef __cplusplus
}
#endif

#endif
