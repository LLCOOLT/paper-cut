#ifndef PORT_LUIGI_H
#define PORT_LUIGI_H

/* Play as Luigi (Enhancements "playable_luigi", off by default;
 * port/src/common/luigi_pc.c, docs/PLAYABLE_LUIGI.md): Luigi's field model
 * (c_luigi, with our animations) is drawn in place of Mario's pose while
 * Mario's current animation has a Luigi version; otherwise Mario's model
 * shows, in the L Emblem's colours. Mario's poses still run as before:
 * Luigi's is a stand-in drawn instead of them (field, battle, the menus'
 * figures). Also his name in the text, his sounds, and the brothers swapped
 * wherever Luigi appears (his NPCs drawn as Mario, the credits). */

#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_LUIGI_FIELD 0 /* Mario in the field (mario.c) */
#define PORT_LUIGI_BATTLE 1 /* Mario's unit's main part in battle (battle_disp.c) */
#define PORT_LUIGI_MENU 2   /* the pause menu's Mario page and badge page figure (win_mario.c) */
#define PORT_LUIGI_LOG 3    /* the journal map's walking figure (win_log.c) */
#define PORT_LUIGI_VERSUS 4 /* a versus battle's Luigi: the enemy side's Mario unit (battle_disp.c), whatever the option */

/* Play as (the setting playable_luigi, docs/PLAY_AS.md): who stands in for
 * Mario. Luigi's own extras (his faces, Mario in green where he has no
 * version, the brothers swapped in scenes) are his; Peach and Bowser have a
 * stand-in for every pose (the nearest of their moves). */
#define PORT_PLAY_AS_MARIO 0
#define PORT_PLAY_AS_LUIGI 1
#define PORT_PLAY_AS_PEACH 2
#define PORT_PLAY_AS_BOWSER 3
#define PORT_PLAY_AS_COUNT 4
int portPlayAs(void);
/* a character's model with our animations was made at startup (else the disc's) */
void portPlayAsSetModelReady(int chr, int ready);

/* Luigi is the pick. */
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

/* Luigi's faces (port/src/aurora/luigi_faces_aurora.c), made at the start
 * from Luigi's own head on the player's disc (port/assets/luigi/art):
 * icondrv.c's icon iconId drawn from our own texture (a GXTexObj*): while the
 * option is on, the game's three icons of Mario's head are Luigi's: 1, or 0
 * for the game's own. */
int portIconTexOverride(unsigned int iconId, void* texObj);
/* Draws after this have Luigi's faces (1), Mario's (0) or as the option is
 * (-1, the default): a versus battle's Luigi side (versus_aurora.c). */
void portLuigiFaceForce(int luigi);
/* At startup, after portArtPrepare, before the mods apply: level-up's BP
 * panel as Luigi's, in the "*#luigi" texture set (mod_game_aurora.c flags
 * every map so while the option is on). */
void portLuigiFacesInit(void);

/* npcdrv's draws, after the game stepped an NPC's pose poseId: while the
 * option is on, an NPC drawn with c_luigi (NPC_FLAG_LUIGI) gets a stand-in
 * of Mario (a_mario, a_mario_r, e_mario) and one drawn with a_mario a
 * stand-in of Luigi, which portLuigiDrawMtx draws instead. The NPC itself is
 * untouched: it moves, talks and collides as before. */
void portLuigiNpcUpdate(int poseId);

/* The credits' parade (end_00.c): Luigi's model and walk in Mario's place,
 * and Mario's in Luigi's, while the option is on. */
void portLuigiCreditActor(const char** model, const char** anim);

/* At startup, before the mods apply (port/src/aurora/luigi_model_aurora.c):
 * a/c_luigi_pc and a/c_luigi_pc-, Luigi's model with our animations and his
 * hammer (docs/LUIGI_ANIMS.md), made from the user's disc. */
void portLuigiModelInit(void);
/* those files were made (else Luigi is the disc's c_luigi) */
int portLuigiModelReady(void);
void portLuigiSetModelReady(int ready);

#ifdef __cplusplus
}
#endif

#endif
