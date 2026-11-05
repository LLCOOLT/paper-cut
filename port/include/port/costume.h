#ifndef PORT_COSTUME_H
#define PORT_COSTUME_H

/* Mario's costumes, partner skins and stickers (docs/COSTUMES.md): how
 * things look only. Nothing here changes what the game reads, so a game
 * played together plays out the same; none of it goes through online play's
 * inputs.
 *
 * - Costumes (port/src/common/costume_pc.c): paper versions of enemies'
 *   outfits, a Koopa's shell, a Boo's sheet, an X-Naut's hood..., drawn on
 *   Mario (or whoever Play as puts in his place) from the enemy's own model and
 *   textures on the player's disc: only the outfit's parts of the enemy's
 *   model, fitted to his hat, body or whole figure as he moves. Each is
 *   unlocked once its enemy is in the Tattle Log (GSWF_TATTLE_BASE + its
 *   unit) on the file being played; chosen in the Start menu's Wardrobe
 *   (setting "costume").
 * - Partner skins (port/src/common/skin_pc.c, port/src/aurora/
 *   skins_aurora.c): a character pack made with the character tools
 *   (docs/CHARACTERS.md) put in the user folder's skins/ folder, drawn in a
 *   partner's place with its own animations, chosen per partner in the
 *   Wardrobe.
 * - Stickers (port/src/common/sticker_pc.c): partners' faces, items and
 *   enemies, unlocked as the file meets them, stuck in the world from the
 *   Wardrobe; kept per save file in the user folder (Stickers/<game id>/),
 *   drawn in the room they were stuck in (so photos have them too). */

#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the animation driver (src/driver/animdrv.c, TARGET_PC) */

/* While not NULL: the draw of a pose shows only the groups whose entry is
 * nonzero (indexed by group); a hidden group's children still draw. */
extern const unsigned char* gPortAnimShapeMask;
/* Each group's model matrix of pose poseId as its animation is now, under
 * matrix, into out[max] (all zero for a group the pose hides); the group
 * count, or 0 (no such pose, not stepped yet, more groups than max). */
int portAnimPoseGroupMatrices(int poseId, Mtx matrix, Mtx* out, int max);

/* --- poses of our own (costume_pc.c) */

/* A pose of model playing anim for the costumes and stickers (type 2, kept
 * across maps and battles, stepped once a frame), let go after a while
 * unwanted; -1 until it is loaded (wanted from the game's frame or a menu's,
 * portCosmeticPoseWant, it's loaded at the next frame's start: never
 * while drawing) or if it can't be. A draw only looks it up: online every game
 * of a session wants, loads and lets go of the same poses on the same frames,
 * whatever each one draws. */
int portCosmeticPose(const char* model, const char* anim);
/* Room in the pose pool, its model files and its texture files for a pose of
 * our own, with plenty left for the game's (animPoseEntry doesn't check). */
int portCosmeticPoolRoom(void);
struct AnimationPoseData;
/* d's animation named name (its own string), or NULL. */
const char* portCosmeticFindAnim(struct AnimationPoseData* d, const char* name);
int portCosmeticPoseWant(const char* model, const char* anim);
/* Once a frame, at its start (system.c): the poses asked for are loaded (the
 * costumes', the stickers', the skins'), what the save file has met noted,
 * this room's stickers drawn. */
void portLooksFrame(void);

/* --- costumes */

/* How many there are; one's name (English, for portTrGame); unlocked on the
 * file now; the one on (0: none, else 1 + its index) and stepping it through
 * the unlocked ones (dir -1 or 1, saved). */
int portCostumeCount(void);
const char* portCostumeName(int i);
int portCostumeUnlocked(int i);
int portCostumeCurrent(void);
void portCostumeStep(int dir);
/* Costume i fits the Play as pick (docs/PLAY_AS.md: each is fitted to Mario,
 * Luigi, Peach and Bowser; one that can't sit sensibly on someone isn't worn
 * by them, and stepping skips it). */
int portCostumeFits(int i);
/* portLuigiUpdate: poseId is Mario's at where (PORT_LUIGI_*), for this frame */
void portCostumeNoteMario(int where, int poseId);
/* portLuigiDrawMtx, after Mario's pose source was drawn as pose drawn (his
 * own or Luigi's stand-in) with mtx, xluMode, rotY, scale: his costume. */
void portCostumeDraw(int drawn, int source, Mtx mtx, int xluMode, float rotY, float scale);

/* --- partner skins */

/* The skins found at startup: how many, one's name (from its pack), and the
 * file it was made into (a/pc_skin_<n>). */
int portSkinCount(void);
const char* portSkinName(int i);
const char* portSkinModel(int i);
/* skins_aurora.c: one found (its name, and the model file it was made into) */
void portSkinAdd(const char* name, const char* model);
/* The skins' files made from skins/ (port/src/aurora/skins_aurora.c), at
 * startup before the mods apply. */
void portSkinsInit(const char* userPath);
/* Partner party (PartyId 1 Goombella .. 7 Ms. Mowz): its skin (-1: their
 * own) and stepping it (dir -1 or 1: their own, then each skin; saved). */
int portSkinOf(int party);
void portSkinStep(int party, int dir);
/* portLuigiDrawMtx: a skin's pose to draw in poseId's place (a partner's
 * model with a skin chosen), following its animation, or -1. *scale: the
 * size to draw it at, to the partner's height. */
int portSkinStandIn(int poseId, float* scale);
/* animPoseRelease for a pose a skin may stand in for: its stand-in goes too */
void portSkinRelease(int poseId);
/* portLooksFrame: the stand-ins asked for are loaded */
void portSkinsFrame(void);

/* --- stickers */

/* Every sticker: how many, its kind (PORT_STICKER_*), its name (in the
 * game's encoding), unlocked on this file; its icon (or -1) or, an enemy's,
 * a pose of its model (portCosmeticPose; or -1). */
enum { PORT_STICKER_FACE, PORT_STICKER_ITEM, PORT_STICKER_ENEMY, PORT_STICKER_KINDS };
int portStickerCount(void);
int portStickerKind(int i);
const char* portStickerName(int i);
int portStickerUnlocked(int i);
int portStickerIcon(int i);
int portStickerPose(int i);
/* the game's or a menu's frame: sticker i's pose (an enemy's) wanted (portCosmeticPoseWant) */
void portStickerPoseWant(int i);
/* The Wardrobe's choice: the kind shown and the sticker of it chosen (an
 * index, or -1: none of the kind unlocked yet), stepped (dir -1 or 1; the
 * stickers through the unlocked ones); how many of a kind are unlocked. */
int portStickerKindChosen(void);
void portStickerStepKind(int dir);
int portStickerChosen(void);
void portStickerStep(int dir);
int portStickerUnlockedCount(int kind);
/* Stick the chosen one where Mario stands (the field, a save file loaded);
 * peel the one nearest him off. 1 if done. */
int portStickerStick(void);
int portStickerPeel(void);
/* How many are stuck in this room. */
int portStickersHere(void);
/* A save file was loaded (portPracticeFileLoaded): its stickers are read. */
void portStickersFileLoaded(void);
/* portLooksFrame: what the file has met is noted, this room's stickers drawn */
void portStickersFrame(void);

#ifdef __cplusplus
}
#endif

#endif
