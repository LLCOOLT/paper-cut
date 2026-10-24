#ifndef PORT_TOUCH_OVERLAY_H
#define PORT_TOUCH_OVERLAY_H

/* On-screen touch controls (port/touch/touch_overlay.cpp, CMake option
 * TTYD_TOUCH_OVERLAY, defined as PORT_TOUCH_OVERLAY): touches become port 1's
 * virtual PAD status (Aurora's PADSetVirtualStatus), drawn over the frame with
 * Aurora's Dear ImGui. Shown while no physical gamepad is connected. Plain C
 * interface: no game or SDK types. */

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

/* Every SDL event Aurora reports (AURORA_SDL_EVENT); touches are consumed. */
void portTouchEvent(const union SDL_Event* event);
/* Once per frame, between aurora_begin_frame and aurora_end_frame. */
void portTouchDraw(void);
/* Release every control (pause, focus loss). */
void portTouchReleaseAll(void);

#ifdef __cplusplus
}
#endif

#endif
