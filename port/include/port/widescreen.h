#ifndef PORT_WIDESCREEN_H
#define PORT_WIDESCREEN_H

/* Widescreen (Aurora backend, port/src/aurora/widescreen_aurora.c).
 *
 * Off by default (much of the game was built for 4:3 only). On with the
 * `widescreen` preference (the iOS Settings app; macOS `defaults write`) or
 * $TTYD_WIDESCREEN=1 (which overrides it, 0 too). The frame is 16:9
 * (VILockAspectRatio) and every draw to it has clip-space x scaled by
 * (608/480) / (16/9) (GXSetProjectionScaleX), so 3D shows more to the sides
 * and 2D keeps its shape, centred; the game's projections are unchanged.
 * Full-screen layers (fades, backgrounds) are stretched to the full width
 * instead, per camera. */
void portWidescreenInit(void);
/* camdrv's camera loop, before each camera's projection is loaded. */
void portWidescreenCamera(int cameraId);
int portWidescreenEnabled(void);
/* Switched while running (the settings page). */
void portWidescreenSet(int on);

#endif
