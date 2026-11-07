#ifndef PORT_SOFT_RESET_H
#define PORT_SOFT_RESET_H

/* The GameCube's reset (port/src/common/soft_reset_pc.c): B+X+Start held for
 * half a second, the reset button, or the memory card scripts' "go to the
 * Memory Card Screen" (gp->resetType 1 or 2). The console restarts itself;
 * on PC the game goes back to the title in the same process instead, by the
 * game's own way there (a map change to "title": the area unloaded, every
 * manager re-initialised, as a Game Over's or the title's Modes file's), the
 * window and the sound and disc managers staying as they are.
 *
 * Online (a session, watching or seated) the reset is refused: the games
 * would leave lockstep. Replays and input scripts reset as offline, from the
 * same frame on every run (it reads nothing but the game's own state). */

#ifdef __cplusplus
extern "C" {
#endif

/* The game's reset check (L_gcResetCheck, every frame), in place of the
 * console's: handles a requested reset (gp->resetType). */
void portSoftResetCheck(void);
/* A reset is under way (waiting for a sequence change to settle, or on its
 * way to the title). */
int portSoftResetBusy(void);

#ifdef __cplusplus
}
#endif

#endif
