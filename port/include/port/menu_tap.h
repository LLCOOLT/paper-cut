#ifndef PORT_MENU_TAP_H
#define PORT_MENU_TAP_H

/* Tapping a row of the game's selection lists (port/src/common/menu_tap_pc.c):
 * the touch overlay hands over a tap on the picture; a selection list's
 * frame (src/manager/winmgr.c) takes it as its row. */
#ifdef __cplusplus
extern "C" {
#endif

/* A tap at (nx, ny), 0-1 across the game's picture (touch overlay). */
void portMenuTap(float nx, float ny);
/* The mouse pointer moved to (nx, ny): the row under it is selected, never
 * confirmed (port/settings/features/menu_mouse.cpp). */
void portMenuHover(float nx, float ny);
/* Aurora: each SDL event, for the mouse in menus (menu_mouse.cpp). */
void portMenuMouseEvent(const void* sdlEvent);
/* A menu reading its buttons this frame, for ports (a mask): the controllers' B backs out in it (the menu
 * layer, port/include/port/menu_buttons.h) */
void portMenuMark(unsigned ports);
/* A message reading its buttons: the layer as for a menu, the button pictures left as the field's */
void portMenuMarkText(unsigned ports);
/* A selection list is up (taps on the picture are its rows, not A). */
int portMenuOpen(void);
/* Where the mouse's and Esc's back is the game's B: a menu up (portMenuOpen), or a message or choice
 * window (a talk, a sign, a shop's lines: B turns its page or picks its cancel, as a controller's). */
int portMenuBackOpen(void);
/* With a menu or a message up (portMenuBackOpen): a right click or the mouse's back button is B, a notch of
 * the wheel tilts the stick up or down once (menu_mouse.cpp). */
void portMenuBack(void);
void portMenuWheel(int notches);
/* Esc on the keyboard (settings_ui.cpp, after every page and feature had its look): with a game menu up and
 * Esc on none of the game's buttons, the menu's back (portMenuBack); nonzero when it was taken
 * (menu_mouse.cpp) */
int portMenuEscape(void);
/* The game's buttons (PAD_BUTTON_*) a port (0-3) has held this frame from no controller of its own: the touch
 * controls', the mouse's and Esc's B (portMenuBack). They are the game's buttons as they are, never a
 * controller's confirm and back moved (portMenuButtons) */
unsigned portMenuVirtualHeld(int port);
/* The touch controls' buttons on their port this frame (0: none; port/touch/touch_overlay.cpp) */
void portMenuSetVirtualHeld(int port, unsigned buttons);
/* A controller touchpad's finger move (0-1 across the pad) with a menu up:
 * the stick tilted against it on the next frame (controller_light.cpp). */
void portMenuTouchpad(float dx, float dy);
#ifndef __cplusplus
struct WinMgrSelect;
struct WinMgrEntry;
struct PADStatus;
/* The selection list's frame: the tapped row (cursor index), or -1. */
int portMenuTapRow(struct WinMgrSelect* sel, struct WinMgrEntry* list);
extern int portMenuConfirm; /* this frame: the selected row was tapped (A) */
/* Menus that keep their own cursor (the battle commands, the file select, the
 * pause menu), each frame they take input: the pointer this frame in the 2D
 * units (608 x 480 from the centre, y up), 1 if it clicked, 2 if it moved, 0
 * if neither. Asking marks a menu as up; every caller of a frame sees the
 * same answer. */
int portMenuPoint(float* gx, float* gy);
/* A tap waiting (a click), where (the same units), without taking it or marking a menu up: 1 if there is
 * one (the players' bars look whether it is theirs); portMenuTapTake takes it (no one else gets it). */
int portMenuTapPeek(float* gx, float* gy);
void portMenuTapTake(void);
/* The battle command menu (battle_menu_disp.c): the ring icon (list 0) or
 * list row (list 1) under the pointer this frame, or -1; *click as
 * portMenuPoint's answer. */
int portBattleMenuPick(int list, int* click);
/* Each frame the ring (list 0) or a sub-menu's list (1) takes input: moves
 * its cursor to the pointer; nonzero when a click chose the cursor's one (A). */
int portBattleMenuMouse(int list);
/* The pause menu's lists (win_root.c): choice first + k (k < n, cols to a
 * row) is where the game's cursor hand points at it, (x + k % cols * dx,
 * y - k / cols * dy), and spans w right of that and h about it. The one under
 * the pointer gets *cur (sfx, if not NULL); nonzero when a click chose *cur
 * (A). */
int portWinMouse(int* cur, int first, int n, float x, float y, float dx, float dy, int cols, float w, float h,
                 const char* sfx);
/* After the pad is read: the B and stick presses portMenuBack and
 * portMenuWheel asked for (input_pc.c). */
void portMenuApplyPad(struct PADStatus* pad);
#endif

#ifdef __cplusplus
}
#endif

#endif
