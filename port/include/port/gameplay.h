#ifndef PORT_GAMEPLAY_H
#define PORT_GAMEPLAY_H

/* Gameplay options the game code reads (TARGET_PC only): set from the
 * settings by the Aurora backend (port/src/aurora/settings_aurora.c); the
 * defaults (port/src/common/gameplay_pc.c) are the original game, so the
 * headless build and its golden runs are unchanged. */

#ifdef __cplusplus
extern "C" {
#endif

extern int gPortSkipIntro;  /* the boot logos run fast, then the title (no story demo) */
extern int gPortTextSpeed;  /* message text reveal rate, 100 = original */
extern float gPortRunSpeed; /* Mario's run speed scale, 1 = original */

/* seq_logo.c, every frame of the boot logos: with gPortSkipIntro the Aurora
 * backend fast-forwards them (port/src/aurora/speed_aurora.c). */
void portIntroFrame(void);
/* msgdrv.c: milliseconds of message time for ms of game time. */
int portTextMs(int ms);

#ifdef __cplusplus
}
#endif

#endif
