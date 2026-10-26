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

#ifdef __cplusplus
}
#endif

#endif
