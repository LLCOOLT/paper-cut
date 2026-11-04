#ifndef PORT_LUIGI_H
#define PORT_LUIGI_H

/* Play as Luigi (Gameplay+ "playable_luigi", off by default;
 * port/src/common/luigi_pc.c, docs/PLAYABLE_LUIGI.md): Luigi's field model
 * (c_luigi, with our animations) is drawn in place of Mario's pose while
 * Mario's current animation has a Luigi version; otherwise Mario's model
 * shows, in the L Emblem's colours. Mario's poses still run as before:
 * Luigi's is a stand-in drawn instead of them (field, battle, the menus'
 * figures). Also his name in the text, his sounds, and the brothers swapped
 * wherever Luigi appears (his NPCs drawn as Mario, the credits). */

#include <dolphin/mtx.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_LUIGI_FIELD 0 /* Mario in the field (mario.c) */
#define PORT_LUIGI_BATTLE 1 /* Mario's unit's main part in battle (battle_disp.c) */
#define PORT_LUIGI_MENU 2   /* the pause menu's Mario page and badge page figure (win_mario.c) */
#define PORT_LUIGI_LOG 3    /* the journal map's walking figure (win_log.c) */

/* The option is on. */
int portLuigiOn(void);
/* Once a frame, after the game stepped Mario's pose poseId: Luigi stands in
 * for it (where = PORT_LUIGI_FIELD or _BATTLE) when allowed and Mario's
 * animation has a Luigi version; his pose follows Mario's. */
void portLuigiUpdate(int where, int poseId, int allowed);
/* animPoseDrawMtx, drawing Luigi's stand-in for poseId when there is one
 * (with poseId's material state as it is now). */
void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale);

/* animPaperPoseDispSub: the pose a paper form's capture draws for Mario's
 * pose (his Luigi stand-in while there is one, else the pose itself). */
struct AnimationPose* portLuigiPaperPose(struct AnimationPose* pose);
/* animPoseRelease for a pose Luigi may stand in for (the menus): his
 * stand-in for it goes with it. */
void portLuigiRelease(int poseId);

/* msgSearch: the message as the game shows it, with the brothers' names
 * swapped while the option is on (every language: Mario <-> Luigi, MARIO <->
 * LUIGI, マリオ <-> ルイージ; not inside words, "Paper Mario", "Mario Bros.",
 * "Luigi's Mansion" and the like). The text returned stays valid (a copy kept
 * for as long as the game runs). */
const char* portLuigiText(const char* msg);

/* __psndSFXOn: the sound id (as searchPSSFXList gives it) to play: Luigi's
 * NPC steps, jump and landing for Mario's plain-floor ones while the option
 * is on. */
int portLuigiSfx(int id);

/* npcdrv's draws, after the game stepped an NPC's pose poseId: while the
 * option is on, an NPC drawn with c_luigi (NPC_FLAG_LUIGI) gets a stand-in
 * of Mario (a_mario, a_mario_r, e_mario) and one drawn with a_mario a
 * stand-in of Luigi, which portLuigiDrawMtx draws instead. The NPC itself is
 * untouched: it moves, talks and collides as before. */
void portLuigiNpcUpdate(int poseId);

/* The credits' parade (end_00.c): Luigi's model and walk in Mario's place,
 * and Mario's in Luigi's, while the option is on. */
void portLuigiCreditActor(const char** model, const char** anim);

/* Luigi's story poses (SL_* in c_luigi_pc, docs/LUIGI_ANIMS.md "Story
 * poses"): while name is set (NULL or "" clears it), Luigi in the field plays
 * that animation of his own whatever Mario's is. Scripts:
 * evt_luigi_story_pose(name) / evt_luigi_story_pose(0). Setting Mario's own
 * pose to an SL_* name is harmless but shows nothing of it: his model has no
 * such animation, so he plays his first (animPoseGetAnimIdx's fallback). */
void portLuigiSetStoryAnim(const char* name);

/* At startup, before the mods apply (port/src/aurora/luigi_model_aurora.c):
 * a/c_luigi_pc and a/c_luigi_pc-, Luigi's model with our animations and his
 * hammer (docs/LUIGI_ANIMS.md), made from the user's disc. */
void portLuigiModelInit(void);
/* a model file plus a pack of luigi_pack's (records, group fields, animations),
 * named `name`: 1 and the new file in *out (malloc'd), or 0 */
int portLuigiMergePack(const uint8_t* base, uint32_t baseSize, const uint8_t* pack, const char* name, uint8_t** out,
                       size_t* outSize);
/* those files were made (else Luigi is the disc's c_luigi) */
int portLuigiModelReady(void);
void portLuigiSetModelReady(int ready);
/* Super Luigi: Luigi in a costume, a variant of his made model ("c_luigi_bride": chapter 2,
 * "c_luigi_grass": chapter 4), for the stand-in (field and battle), or NULL for his own; it
 * takes on the next frame, once the made models are there. Scripts: evt_luigi_costume(model or 0) */
void portLuigiSetCostume(const char* model);

#ifdef __cplusplus
}
#endif

#endif
