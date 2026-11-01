#ifndef PORT_HUD_H
#define PORT_HUD_H

/* HUD scale (Aurora backend, port/src/aurora/hud_aurora.c; settings: hud_scale,
 * 50-150 %, 100 off): the status bar, the message and choice windows and the
 * battle menus drawn larger or smaller than the game's, each about its own
 * anchor (the status bar the top of the screen, a message window its middle,
 * the battle menus the left edge), with GXSetProjectionTransform; the rest of
 * the 2D and the 3D view as they were. With the widescreen HUD on, the status
 * bar's parts stay at the frame's edges (portWideHudDx). */
#include "driver/dispdrv.h"

#ifdef __cplusplus
extern "C" {
#endif

/* dispdrv's dispDraw, before each draw callback of a camera (and with
 * callback NULL after the last). */
void portHudDraw(CameraId camId, DispCallback callback, void* param);
/* The game's files tell which draws are which (TARGET_PC). */
int portHudIsStatusBar(DispCallback callback); /* statuswindow.c */
int portHudIsMessage(DispCallback callback);   /* driver/msgdrv.c */
/* The status bar's scale now (1 off; widescreen_pc.c's portWideHudDx). */
extern float gPortHudStatusScale;

#ifdef __cplusplus
}
#endif

#endif
