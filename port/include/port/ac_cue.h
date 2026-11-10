#ifndef PORT_AC_CUE_H
#define PORT_AC_CUE_H

/* Action Command cue (port/src/common/ac_cue_pc.c): when the timed A press a
 * jump's landing is judged on (a Jump attack, guarding an enemy's stomp) is
 * due, so the overlay can show it. */
#ifdef __cplusplus
extern "C" {
#endif

extern int gPortAcCue;      /* the setting: 0 off, 1 on screen, 2 also a tap (phones) */
extern int gPortAcCueState; /* now: 0 nothing, 1 attack, 2 guard */
extern int gPortAcCueLead;  /* game frames until the ideal press (<= 0: now) */
/* a battle unit's jump starts (btlevtcmd_JumpPosition, MarioJumpPosition) */
void portAcCueJump(void* unit);
/* an attack is declared (btlevtcmd_AttackDeclare): attacker, target, and the
 * declaring script's place, which tells the attacks apart */
void portAcCueAttack(void* attacker, void* target, const void* where);
/* a save state or rewind put back: the units the cue followed forgotten */
void portAcCueStateLoaded(void);

/* Controller haptics (Controls > Controller haptics, port/settings/features/
 * haptics.cpp): a pulse on the player's controller as a timed press's window
 * opens, read from the same prediction as the cue. Output only: nothing the
 * game reads changes. */
enum {
    PORT_HAPTICS_ATTACK = 1, /* a timed press's window opens */
    PORT_HAPTICS_GUARD = 2,  /* a guard's window opens */
    PORT_HAPTICS_TICK = 3,   /* a gauge or stick command steps on (a light, a level) */
    PORT_HAPTICS_ZONE = 4,   /* a gauge or stick command is where it succeeds: press or let go now */
};
/* the pulse (Aurora sets it; NULL headless): local controller port, kind */
extern void (*gPortHapticsCue)(int port, int kind);
/* the triggers the running action command holds, per local port: bit 0 L, bit 1 R */
extern int gPortHapticsTriggers[4];
extern unsigned gPortHapticsTriggersAt; /* the retrace they were last set (stale: out of battle) */
/* tests: frames from this frame to the judgement (-1 none), and its window */
extern int gPortAcCueToJudge;
extern int gPortAcCueWindow;
/* second_player_pc.c: the player (0 or 1) who presses for unit's attack
 * (guard 0) or its guard (guard 1) */
int portSecondPlayerPlayerFor(void* unit, int guard);

/* Tests and media captures: a scripted controller's press timed to an action command, not a frame
 * ($TTYD_P2_PRESS=KEYS@ac..., input_pc.c). Each battle frame, the commands say whose press they wait for:
 * port, the local controller port; toIdeal, frames from this frame to the one whose press is ideal (a timed
 * press: the duo moves' mark, a stomp's or a guard's window middle; 0 for the others). */
enum {
    PORT_PRESS_CUE_AC = 0,    /* a timed press is coming (toIdeal counts down to it) */
    PORT_PRESS_CUE_START = 1, /* a gauge, stick or mash command is running for this port */
    PORT_PRESS_CUE_ZONE = 2,  /* that command's gauge or marker is where it succeeds */
    PORT_PRESS_CUE_KINDS = 3
};
void portPressCue(int port, int kind, int toIdeal);
/* a $TTYD_Pn_PRESS waits on these: the commands report them even with haptics and the cue off */
int portPressCueWanted(void);

#ifdef __cplusplus
}
#endif

#endif
