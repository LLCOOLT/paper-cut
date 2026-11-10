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
/* the pick; Mario (none) while Brothers mode is on: Mario and Luigi as themselves */
int portPlayAs(void);
/* a character's model with our animations (back: its back-facing one, Peach's
 * c_peach_r_pc) was made at startup (else the disc's) */
void portPlayAsSetModelReady(int chr, int back, int ready);

/* "Sync what matters" online (statesync_ghost_pc.c): the other player's pick chr
 * drawn as their ghost, for Mario's animation mario: the model to load (NULL:
 * no pick), the animation in that model's pose (NULL: Mario's own model then,
 * as Luigi where he has no version), the size in Mario's place */
const char* portPlayAsGhostModel(int chr, const char* mario);
const char* portPlayAsGhostAnim(int chr, const char* mario, int pose);
float portPlayAsScale(int chr);

/* Luigi is the pick. */
int portLuigiOn(void);
/* Luigi's higher jump (port/src/common/luigi_jump_pc.c): he is the pick and the setting
 * luigi_high_jump is on. The hooks in Mario's jump: its take-off (marioPreJump), each frame of its
 * rise (marioJump, prev: the speed before the frame's step) and its top. */
int portLuigiHighJump(void);
void portLuigiJumpStart(void);
void portLuigiJumpRise(float prev);
void portLuigiJumpTop(void);
/* Once a frame, after the game stepped Mario's pose poseId: Luigi stands in
 * for it (where = PORT_LUIGI_FIELD or _BATTLE) when allowed and Mario's
 * animation has a Luigi version; his pose follows Mario's. */
void portLuigiUpdate(int where, int poseId, int allowed);
/* animPoseDrawMtx, drawing Luigi's stand-in for poseId when there is one
 * (with poseId's material state as it is now). */
void portLuigiDrawMtx(int poseId, Mtx mtx, int xluMode, float rotY, float scale);
/* Once a frame, Mario's battle unit's part 3 (Hammer Throw's thrown hammer: Mario's model showing only
 * the hammer): its pose, whether it shows and where it is. Drawn as Peach's closed parasol (her hammer
 * rank's) or, Bowser's, not at all while his fireball flies in its place (port/include/port/fire_breath.h);
 * Luigi's stays Mario's hammer. Draw only. */
void portPlayAsThrown(int poseId, int shown, float x, float y, float z);
/* A new battle (portFireBreathBattleStart): the last battle's thrown-parasol pose is forgotten (its pose
 * id went with that battle's pool, and may now be someone else's). */
void portPlayAsThrownBattleStart(void);

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

/* The pick the text names (docs/PLAY_AS.md): the pick, but Mario in a
 * versus battle (always the brothers, Mario against Luigi) and while
 * Brothers mode is on (portPlayAs: no pick then, both brothers as themselves). */
int portPlayAsText(void);
/* its name in English ("Mario", "Luigi", "Peach", "Bowser"), for portTr */
const char* portPlayAsName(void);
/* text's encodings for portPlayAsSwapText: the game's (as msgSearch's, by
 * the game's language), Shift-JIS (the JP release's font), Latin-1 (the US
 * and EU releases' font), UTF-8 (the settings page) */
#define PORT_TEXT_GAME 0
#define PORT_TEXT_SJIS 1
#define PORT_TEXT_LATIN1 2
#define PORT_TEXT_UTF8 3
/* text may name Mario or the pick chr (the quick test before a swap) */
int portPlayAsHasName(const char* text, int chr);
/* text with Mario's and chr's names swapped both ways, by the game text's
 * rules (portLuigiText), into out (room for twice text and its end) */
void portPlayAsSwapText(const char* text, char* out, int chr, int enc);

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
/* After portArtPrepare: Peach's parasol cards onto her models (portPlayAsHammerCards) */
void portPeachParasolInit(void);

/* npcdrv's draws, after the game stepped an NPC's pose poseId: while the
 * option is on, an NPC drawn with c_luigi (NPC_FLAG_LUIGI) gets a stand-in
 * of Mario (a_mario, a_mario_r, e_mario) and one drawn with a_mario a
 * stand-in of Luigi, which portLuigiDrawMtx draws instead. The NPC itself is
 * untouched: it moves, talks and collides as before. */
void portLuigiNpcUpdate(int poseId);

/* The credits' parade (end_00.c): Luigi's model and walk in Mario's place,
 * and Mario's in Luigi's, while the option is on. */
void portLuigiCreditActor(const char** model, const char** anim);
/* the size the parade draws the pick at in Mario's place (model: the
 * parade's own, before portLuigiCreditActor), as in the field: Bowser 0.7 */
float portLuigiCreditScale(const char* model);

/* At startup, before the mods apply (port/src/common/luigi_model_pc.c):
 * a/c_luigi_pc and a/c_luigi_pc-, Luigi's model with our animations and his
 * hammer (docs/LUIGI_ANIMS.md), made from the user's disc; likewise Peach's
 * c_peach_pc and c_peach_r_pc and Bowser's c_koopa_pc (docs/PLAY_AS.md). */
void portLuigiModelInit(void);
/* Peach's parasol (art/recipes/peach_parasol.recipe, docs/PLAY_AS.md), after portArtPrepare: chr's
 * models' textures made again with cards (RGBA, as the recipes make them) in the hammer drawings'
 * places, one for each of the pack's slots (Peach: 12 closed, wooden, Super, Ultra by four turns,
 * then 3 open). 1 if every model of chr's took them. */
struct PortArtImage;
int portPlayAsHammerCards(int chr, const struct PortArtImage* cards, int ncards);
/* the parasol is on Peach's models (else Mario's hammer drawings) */
int portPlayAsParasol(void);
/* While Peach floats (peach_parasol_pc.c): the animation her stand-in plays in Mario's place (M_J_1P,
 * from behind M_J_1PR: both hands on the open parasol over her head), else NULL */
const char* portParasolAnim(int where, int back);
/* Once a frame, after a stand-in's pose was stepped (luigi_pc.c): while she floats, the open parasol
 * her hammer plane shows is her hammer's (peach_parasol_pc.c). */
void portParasolPose(int pose, int back, int where, const char* mario);
/* Peach's parasol float (peach_parasol_pc.c, docs/PLAY_AS.md): with Peach the pick and A held as she
 * comes down from a jump. portParasolFall: marioFall's fall speed this frame (spd, as the game's);
 * portParasolDrift: marioMove's air move distance (dist). Both as given otherwise. */
int portParasolFloating(void);
float portParasolFall(float spd);
float portParasolDrift(float dist);
/* those files were made (else Luigi is the disc's c_luigi) */
int portLuigiModelReady(void);
void portLuigiSetModelReady(int ready);

#ifdef __cplusplus
}
#endif

#endif
