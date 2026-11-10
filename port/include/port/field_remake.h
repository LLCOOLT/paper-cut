/* The remake's field changes (port/src/common/field_remake_pc.c,
 * port/settings/features/field_remake.cpp): each one a setting, off by
 * default, marked "changes play" (so Console-accurate mode locks it and online
 * the host's value is the one played). */
#ifndef PORT_FIELD_REMAKE_H
#define PORT_FIELD_REMAKE_H

#ifdef __cplusplus
extern "C" {
#endif

struct NpcEntry;
struct HitEntry;
struct MarioWork;

/* Declares the settings (also called by the getters, so headless has them). */
void portFieldRemakeDeclare(void);

/* --- Shorter enemy notice range ("remake_notice_range") ---
 * npcdrv.c npcMain's sight check: the distance to Mario is multiplied by this
 * before it is held against the enemy's search range (1 when off). */
float portNoticeDistScale(void);

/* --- Field projectiles hurt ("remake_projectile_hurt") ---
 * npcdrv.c fbatEncountCheck: an enemy's thrown or fired thing (a slave NPC
 * with an attack mode: bones, hammers, Bullet Bills, beams) touching Mario
 * takes 1 HP and makes him blink instead of starting a battle. Returns 1 when
 * it did (the battle is not started). At 1 HP the battle starts as before. */
int portFieldProjectileHit(struct NpcEntry* npc, unsigned kind);

/* --- Pipes with A ("remake_pipe_a") ---
 * casedrv.c caseMain: a pipe (the stick-down case) goes on A instead of the
 * stick; mario_motion.c marioChkJump: no jump on one; mario_balloon.c: the
 * game's "!" balloon over Mario standing on one. */
int portPipeByA(void);
/* the floor hit is a pipe's (a stick-down case on it) */
int portPipeUnder(struct HitEntry* standOn);
/* evt_bero.c, once Mario is out of a pipe: he turns to face the camera. */
struct EventEntry;
int portEvtPipeFaceFront(struct EventEntry* event, int isFirstCall);

/* --- Field hammer ("remake_field_hammer") ---
 * npcdrv.c fbatHitCheck: the swing also hits behind Mario while the head
 * comes round over him (frame 0-10 of the swing). */
int portFieldHammer(void);
int portFieldHammerBackFrame(int frame);
/* casedrv.c caseMain: a bush's check case also fires on a hammer hit.
 * mot_hammer.c, the swing's impact: friendly NPCs and the partner flinch,
 * Rogueport's cockroaches drop a coin, and it is kept for: */
void portFieldHammerImpact(void);
/* evt_mobj.c, a hidden Star Piece panel: a swing landed within r of it on
 * this or the last frame (it half-flips, showing itself). */
int portFieldHammerNear(float x, float y, float z, float r);
/* every frame (npcdrv.c npcMain): the flinches */
void portFieldRemakeFrame(void);

/* --- Koops' aim ("remake_koops_aim") ---
 * party_nokonoko.c nokonoko_use, every frame: while the shell goes out or
 * is held, the game's own Shell Slam effect (MOBJ_EFF_nokotarou3) marks
 * where it stops, stretched back to Koops as the direction, and a white ring
 * (the game's ripple, as Vivian's shadow ring) pulses out on the ground
 * there: the streak alone was too faint to see on grass. */
struct PartyEntry;
void portKoopsAim(struct PartyEntry* party);

/* --- Steadier Paper Plane ("remake_plane_steady") ---
 * mot_plane.c flyMain: at the start of the glide, then each frame of it:
 * the plane keeps its height until it is a panel's width from where the
 * glide began. */
void portPlaneGlideStart(void);
void portPlaneSteady(void);

/* --- Yoshi Kid ("remake_yoshi") ---
 * party_yoshi.c: off an edge Yoshi keeps his speed with the stick let go;
 * the flutter lasts this many frames of air (68 in the original): */
int portYoshi(void);
int portYoshiFlutterFrames(void);
/* A on the ground is the talk or the door ahead's (not a flutter); */
int portYoshiActionAhead(void);
/* mario_motion.c marioChkTalkable: talking while riding; marioChgTalkMotion:
 * Mario stays on Yoshi (1: keep the ride); */
int portYoshiTalk(void);
/* evt_mario.c evt_mario_jump_jumpstand: a red jump stand's leap carries
 * Mario alone; riding when it began, he is back on Yoshi where he lands
 * (start: at its first frame; land: 1 when it remounted, else the caller
 * stands him as before). */
void portYoshiJumpstandStart(void);
int portYoshiJumpstandLand(void);

/* --- Ledge teetering ("remake_teeter") ---
 * mot_stay.c, standing with the stick let go: at a ledge (the floor gone just
 * past his front foot) he plays his flailing pose (M_D_6, which every Play as
 * pick has); 1 while he does (the idle animations wait). */
int portLedgeTeeter(void);

/* --- Guard stances ("remake_guard_stance") ---
 * battle_ac.c BattleActionCommandManager, every frame: A or B pressed while
 * an enemy's attack on Mario is coming shows a stance for a moment, the
 * Defend brace (M_B_2) for a Guard, arms thrust out (M_C_4) for a
 * Superguard. */
struct BattleWork;
void portGuardStanceUpdate(struct BattleWork* wp);

#ifdef __cplusplus
}
#endif

#endif
