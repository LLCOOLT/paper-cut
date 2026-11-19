#ifndef PORT_PM64_SPIN_H
#define PORT_PM64_SPIN_H

/* Paper Mario 64's Spin Dash in the field (GitHub #51; docs/ROM_EXTRAS.md "Spin Dash";
 * port/src/common/pm64_spin_pc.c). With a Paper Mario ROM linked and the setting "pm64_spin" on (off by
 * default, off in Console-accurate mode; online the host's rule, and only when every player has the ROM, as
 * the badges), the Spin Dash control makes Mario dash forward spinning, as Z does in Paper Mario: the
 * "Spin Dash" row of Controls > Bindings (Z on the keyboard; no controller button by default) or the game's
 * L, which Mario doesn't use in the field (a controller's left trigger, the touch controls' L; the touch controls'
 * own Spin Dash button, a swirl, as the binding): on its press,
 * or, while the paper notes are on (L is theirs too: a note's reading, the quick wheel when held), on a tap of
 * it where no note is near. Brothers mode's Luigi spins too (his player's L or binding).
 *
 * Paper Mario's spin badges, sold by Rowf with the other Paper Mario badges (pm64_badges_pc.c): Speedy Spin
 * makes it faster and longer; Spin Attack makes a spin into an enemy a first strike (the game's own: the spin
 * hammer's, through fbatEncountCheck); Dizzy Attack leaves the enemy in front dizzy for three turns of the
 * battle it starts. Without either, a spin into an enemy is a plain touch, as in Paper Mario. */

#ifdef __cplusplus
extern "C" {
#endif

struct NpcEntry;
struct BattleWork;
struct PartyEntry;

/* the move is there now: the setting, a ROM linked, not Console-accurate, online every player's ROM */
int portSpinDashOn(void);

/* mario.c marioMain, each frame Mario's own code runs, before his motion: a press starts the spin; the spin
 * under way steers, slows and ends */
void portSpinDashFrame(void);

/* mario.c marioMoveMain: 1 while Mario's forced run is his spin's: it moves him as a run does, against walls
 * (a road exit's push), slopes and moving floors, not straight through them as a script's walk */
int portSpinDashMoving(void);

/* mario_sbr.c marioSetForceMove: a forced move set now (a script's walk, an exit's walk-out); while a spin is
 * under way, not its own, it takes Mario over from the spin */
void portSpinDashForceMoveSet(void);

/* npcdrv.c fbatEncountCheck, after the game's checks and the co-op touch: an enemy touched by Mario's (or
 * Luigi's) spin with Spin Attack worn is a first strike (kind: the hit's u32, changed); with Dizzy Attack, a
 * plain touch whose battle starts with the front enemy dizzy. found is returned. */
struct NpcEntry* portSpinDashHit(struct NpcEntry* found, void* kind);

/* battle_seq.c btlseqFirstAct, once the first strike is over: Dizzy Attack's dizzy */
void portSpinDashBattleStart(struct BattleWork* wp);

/* A spin's shape, by Speedy Spin (src/world/action/spin.c's numbers at 60 frames a second): its frames of free
 * spinning (then it turns on to its end), the frames at full speed, its full speed as a multiple of the run's,
 * its slowing a frame after (of the run's), and its paper's turn a frame in degrees */
typedef struct PortSpinShape {
    int frames, full;
    float scale, friction, rate;
} PortSpinShape;
void portSpinDashShape(PortSpinShape* out);
/* the speed frame t (0 ..) of a spin shaped so goes at, run being the run's speed */
float portSpinDashSpeed(const PortSpinShape* s, int t, float run);
/* the way the paper turns (1, -1) for a spin toward dir (degrees) with the camera's yaw camYaw */
float portSpinDashTurnSign(float dir, float camYaw);
/* the paper's facing (degrees, 0 left, 180 right) at frame t of a spin, turned from *facing; ending: its time up
 * early (a wall). 1 when it has turned to its end: the spin is over */
int portSpinDashTurn(const PortSpinShape* s, int t, int ending, float sign, float* facing);
/* mario.c marioChgPose, marioChgPoseTime: a pose asked for while the spin holds his (his once it ends) */
void portSpinDashPoseAsked(const char* name);
/* Mario's pose (TTYD's animation name) while he spins */
const char* portSpinDashPoseName(void);
/* Mario's Paper Mario 64 look: the Mario1 animation his spin plays now (ANIM_Mario1_Spin, SpeedySpin,
 * SpinAttack), -1 if he isn't spinning */
int portSpinDashLookAnim(void);
/* his paper's facing now (degrees) and 1 frame of PM64's (2 of ours) before, while he spins; 0 if he isn't */
int portSpinDashLookFacing(float* now, float* before);

/* player (0 Mario's, 1 Brothers mode's Luigi's) pressed the Spin Dash control this frame: their binding
 * (Aurora) or the game's L on their controller (l: that controller's L held now; called every frame) */
int portSpinDashPressed(int player, int l);

/* the touch controls' Spin Dash button (port/touch/touch_overlay.cpp, a swirl): held now by the player on port (0-3),
 * or let go (port -1). Read as that player's binding is: its press spins, as a press of the binding does */
void portSpinDashTouchHeld(int port);

/* the spin's sound and dust at pos (x, y, z) as it starts (the ROM's own Spin, Speedy Spin or Spin Attack
 * sound, else the game's spin hammer's) */
void portSpinDashStartFx(float x, float y, float z);
void portSpinDashDust(float x, float y, float z, float dir);

/* The badges' ribbons round a spinner (Paper Mario's fx_effect_46, the game's eff_whirlwind_n64): started for
 * target (a MarioWork, or NULL: moved by portSpinDashRibbonFollow) turning sign's way; NULL without a spin badge.
 * work: portSpinDashRibbonWork's (the effect's own, to know it is still the same); End fades it out. */
void* portSpinDashRibbonStart(void* target, float sign);
void* portSpinDashRibbonWork(void* fx);
void portSpinDashRibbonFollow(void* fx, void* work, float x, float y, float z);
void portSpinDashRibbonEnd(void* fx, void* work);

/* brothers_pc.c: Luigi spinning now, decided in this game (the host's, for an online guest's Luigi); NULL not */
const struct PartyEntry* portBrothersSpinning(void);

#ifdef __cplusplus
}
#endif

#endif
