#ifndef PORT_SPEED_H
#define PORT_SPEED_H

/* Fast-forward and slow speed (Aurora backend, port/src/aurora/speed_aurora.c). */

#ifdef __cplusplus
extern "C" {
#endif

/* The fast-forward control went down (1) or up (0): held, or toggled,
 * as the settings say. */
void portSpeedControl(int down);
/* The game runs faster than real time this frame. */
int portSpeedActive(void);
/* Once per game frame (vi_aurora.c): the speed for the next one. */
void portSpeedFrame(void);
/* Slow speed (Accessibility): the game's pace this frame, 50-100 %; 100
 * normally, while fast-forwarding and online. */
int portSpeedPercent(void);
/* The vsync to apply for the settings' value. */
int portSpeedVsync(int vsync);

#ifdef __cplusplus
}
#endif

#endif
