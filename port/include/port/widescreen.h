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
 * instead, per camera.
 *
 * The aspect: settings aspect_ratio (PORT_ASPECT_*; the switch above by
 * default: 16:9 or 4:3), and aspect_max, the widest the frame gets (bars
 * beside it past that, blurred with side_bars). The edge scale of
 * include/wide_x.h follows, which widens the game's culling limits (map,
 * NPCs, effects) to the frame's edges. */
enum {
    PORT_ASPECT_SWITCH, /* the Widescreen (16:9) switch: 16:9 or 4:3 */
    PORT_ASPECT_4_3,
    PORT_ASPECT_16_9,
    PORT_ASPECT_16_10,
    PORT_ASPECT_21_9,
    PORT_ASPECT_32_9,
    PORT_ASPECT_WINDOW, /* the window's own */
};
void portWidescreenInit(void);
/* camdrv's camera loop, before each camera's projection is loaded. */
void portWidescreenCamera(int cameraId);
/* camdrv's camera loop, after the last camera: mirror mode's flip undone. */
void portWidescreenCameraDone(void);
/* The widescreen switch (or its preference / $TTYD_WIDESCREEN). */
int portWidescreenEnabled(void);
/* The frame is wider than 4:3 now, and its aspect (4:3 when not). */
int portWidescreenWide(void);
float portWidescreenAspect(void);
/* Once a frame: follows the settings and the window's shape. */
void portWidescreenFrame(void);
/* Switched while running (the settings page). */
void portWidescreenSet(int on);
/* Widescreen HUD on (the settings page; port/src/common/widescreen_pc.c) */
extern int gPortWideHud;

#endif
