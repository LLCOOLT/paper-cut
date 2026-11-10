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
/* Layout editing, over the paused game: the controls dragged to new places,
 * sized, faded or hidden (for the current orientation) until Done (saved) or
 * Cancel; the game gets no input meanwhile. */
void portTouchEditLayout(void);
/* The same once the Start menu that asked has closed (a moment later). */
void portTouchEditRequest(void);
/* Player 1 plays on the screen now: no controller of theirs (port 1's) has taken the controls over. */
int portTouchPlayerOne(void);
/* The controller port (0-3) the touches are: 0 (player 1's) until a seat swap takes the screen's player
 * to another seat (portInputSwapPorts, port/include/port/platform.h). */
void portTouchSetPort(int port);

/* Tests (the device flows, port/src/common/flows_pc.c): the controls drawn now; a finger on a control by
 * name ("a", "start", "stick-right", ...: 0 none such), or at a window fraction, through SDL's event queue. */
int portTouchTestShown(void);
int portTouchTestEditing(void); /* the layout editor is open */
int portTouchTestControl(const char* name, int down);
void portTouchTestFinger(float x, float y, int down);

#ifdef __cplusplus
}
#endif

#endif
