#ifndef PORT_REWIND_H
#define PORT_REWIND_H

/* Rewind (Aurora backend with save states, port/src/aurora/rewind_aurora.c;
 * settings: rewind, rewind_seconds, rewind_key, rewind_pad): the last
 * seconds of play kept as states in memory, and while the Rewind key or
 * button is held the game steps back through them. Not in online co-op. */

#ifdef __cplusplus
extern "C" {
#endif

/* The Rewind control went down (1) or up (0). */
void portRewindHold(int down);
/* Tests (an input script's `rewind N`): held for the next frames. */
void portRewindTest(int frames);
/* End of each frame (vi_aurora.c): a state taken, or a step back while held. */
void portRewindFrame(void);
/* Stepping back now. */
int portRewindActive(void);
/* How far back it can go now, seconds (0: nothing kept). */
float portRewindSeconds(void);
/* This build can rewind (save states), and not online now. */
int portRewindSupported(void);
/* What the states cost, for the settings page: memory in MiB (the states,
 * the copy of the game they start from) and the time a state takes, ms. */
void portRewindCosts(float* statesMiB, float* mirrorMiB, float* avgMs, float* maxMs);

#ifdef __cplusplus
}
#endif

#endif
