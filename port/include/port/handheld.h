#ifndef PORT_HANDHELD_H
#define PORT_HANDHELD_H
/* Linux ARM64 handhelds (PortMaster: docs/HANDHELDS.md; handheld_aurora.c).
 *
 * A handheld is found by $TTYD_HANDHELD (1 or 0: the launch script's say), or
 * else by the board: an ARM64 Linux whose device tree names a handheld chip
 * (Allwinner H700/H616/H618, Rockchip RK3326/RK3566/RK3568/RK3588, Amlogic
 * S922X). On one:
 *  - before Aurora starts: OpenGL ES (no Vulkan to rely on), the smaller
 *    graphics buffers (also on any computer with under 6 GB), no 4x MSAA,
 *    full screen;
 *  - at the first start there: the Handheld preset (the screen's own
 *    resolution and shape, 30 frames a second, the costly effects off);
 *  - Select + Start held together quits cleanly (quick resume and the
 *    settings saved first), and the power or sleep key saves both at once;
 *    quick resume is also written every 10 seconds rather than every minute. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 on a handheld (found once) */
int portHandheld(void);
/* main_aurora.c, before aurora_initialize (config: an AuroraConfig*) */
void portHandheldConfig(void* config);
/* 1 when portHandheldConfig chose the smaller graphics buffers (a handheld, under 6 GB, $TTYD_LOW_MEMORY) */
int portLowMemoryGfx(void);
/* 1 on a weak computer (under 6 GB or 4 threads or fewer, $TTYD_LOW_END; never a handheld or phone), found by
 * portHandheldConfig: pipelines compile in the background by default (settings_aurora.c) */
int portLowEndPc(void);
/* main_aurora.c, after portSettingsLoad: the preset, the first time */
void portHandheldPreset(void);
/* vi_aurora.c: each of SDL's events (an SDL_Event*) */
void portHandheldEvent(const void* sdlEvent);

#ifdef __cplusplus
}
#endif
#endif
