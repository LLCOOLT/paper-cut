#ifndef PORT_VIEW_H
#define PORT_VIEW_H

/* The view (port/src/common/view_pc.c): two settings that change only how
 * the game is seen, never what it plays out (each player's own, not synced
 * online).
 *
 * free_camera: in the field, while Mario has control, the right stick
 * (C-stick) turns the main 3D camera a little around Mario (up to 35 degrees
 * to either side) and moves it in (up) or out and higher (down), easing back
 * to the game's own camera when let go; the mouse turns it too (below). Only the draw sees it: camDraw (and
 * the in-between frames) swap the 3D cameras' view in for the draw and back
 * after, so everything the game reads (the stick's direction, what is on
 * screen) keeps the game's own camera. The C-stick still reaches the game.
 *
 * mirror_mode: the 3D picture flipped left-right (the 3D, background and fade
 * cameras' projections scaled by -1 in x: portWidescreenCamera), the HUD,
 * menus and message windows (the 2D camera) as they are; and while Mario has
 * control in the field, the stick's x and the D-pad's left and right turned
 * around at the pad read, so moving feels as it looks. In battles and menus
 * the controls are the game's. */

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DEMOPadRead, after the port's own input (script, second player, keyboard)
 * and before the game sees it: the C-stick for the free camera, the mirror's
 * controls. */
void portViewPad(PADStatus* pad);
/* camDraw's camera loop (and the in-between frames' drawScene): the free
 * camera's view in before the first camera, the game's back after the last. */
void portViewBegin(void);
void portViewEnd(void);
/* The free camera (or photo mode) has the view away from the game's own. */
int portViewFreeActive(void);
/* Mirror mode is on. */
int portViewMirrored(void);
/* The mouse camera (mouse_camera, with mouse_camera_speed and _invert): while
 * the free camera can turn (the field, Mario's controls), the mouse turns it
 * too, and it stays where the mouse leaves it; port/settings/features/
 * view.cpp captures the cursor only then (portViewMouseLook) and passes the
 * mouse's moves in window pixels. A middle click centres it again. */
int portViewMouseLook(void);
void portViewMouse(float dx, float dy);
void portViewMouseCentre(void);

/* Photo mode (port/settings/features/photo_mode.cpp): the game held (frame
 * advance's hold), its last frame drawn again from a camera turned all the way
 * round Mario (yaw), up or down (pitch, radians: positive higher) and in or
 * out (zoom: the eye's distance, times), the HUD and message windows (the 2D
 * camera) hidden or not. In the field or a battle (round the point the battle
 * camera looks at), not online. */
int portPhotoAvailable(void);
void portPhotoSet(int active, float yaw, float pitch, float zoom, int hideHud);
int portPhotoHideHud(void);
/* where Mario's middle is in photo mode's picture (0-1 from the top left), for its depth of field */
void portPhotoFocus(float* x, float* y);
/* The Start menu's row: photo mode asked for, then taken (once) when Mario
 * has the controls again, the menu closed. */
void portPhotoRequest(void);
int portPhotoTakeRequest(void);
/* Photo mode by touch (photo_mode.cpp, for port/touch/touch_overlay.cpp):
 * whether it is on; on or off (the on-screen camera button); the on-screen
 * sticks as held now (-1..1: x right, y up, z the C-stick up, in); a drag
 * across the picture (fractions of its size: turns and tilts the camera); an
 * on-screen button pressed (PAD_BUTTON_A, _B, _X, _Y, PAD_TRIGGER_L, _R:
 * picture, back, filter, HUD, depth of field, frame), nonzero if it was
 * photo mode's. */
int portPhotoOn(void);
void portPhotoToggle(void);
void portPhotoTouchAxes(float x, float y, float z);
void portPhotoTouchDrag(float dx, float dy);
int portPhotoTouchButton(unsigned int button);

#ifdef __cplusplus
}
#endif

#endif
