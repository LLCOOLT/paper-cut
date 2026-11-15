#ifndef PORT_SAFE_GRAPHICS_H
#define PORT_SAFE_GRAPHICS_H

/* Safe graphics and the graphics API (GitHub #39: a phone's GPU that took the whole device down).
 *
 * Settings > Graphics > Safe graphics (safe_graphics, every platform): the least the GPU can be asked for. From
 * the next start (portSafeGraphicsConfig, main_aurora.c, before Aurora starts): no anti-aliasing, the smaller
 * graphics buffers (TTYD_LOW_MEMORY=1), texture limits of an older GPU (AURORA_LOW_LIMITS), per-draw values in
 * a uniform binding instead of push constants (AURORA_NO_IMMEDIATES), pipeline waits of 0.1 s at most. For the
 * run, at once (port/settings/features/safe_graphics.cpp; settings.txt keeps the player's values): the picture
 * at 480 lines, no supersampling, texture upscaling, anisotropic filtering, bloom, ray-traced lighting, paper
 * shadows and edges, picture filters, HDR, stereoscopic 3D or in-between frames, texture pack memory Low,
 * drawn graphics never waited for, the second screen's live menu held.
 *
 * Settings > Graphics > Graphics API (gfx_backend, Android): Auto (Vulkan), Vulkan or OpenGL ES, from the next
 * start; $TTYD_GPU still says. Android's frames hold for pipelines at most 0.1 s (AURORA_BLOCKING_BUDGET_MS).
 * On Windows: Auto (Direct3D 12), Direct3D 12, Vulkan or Direct3D 11 (an old or Intel driver's Direct3D 12 that
 * goes white or stops); Safe graphics with it on Auto starts on Direct3D 11.
 *
 * After a run that stopped on screen with no clean exit (port/include/port/flight.h, the memory guard's record),
 * the launcher offers Safe graphics, the other API (Android) and the flight record. $TTYD_SAFE_GRAPHICS=0/1
 * overrides the setting at start; $TTYD_FLIGHT_OFFER=1 shows the offer on any platform (tests). */

#ifdef __cplusplus
extern "C" {
#endif

/* main_aurora.c: the AuroraConfig (an AuroraConfig*), before portHandheldConfig */
void portSafeGraphicsConfig(void* config);
/* Safe graphics was on when the game started */
int portSafeGraphicsAtStart(void);
/* main_aurora.c, after portSettingsLoad: Safe graphics' settings for the run */
void portSafeGraphicsStart(void);

#ifdef __cplusplus
}
#endif

#endif
