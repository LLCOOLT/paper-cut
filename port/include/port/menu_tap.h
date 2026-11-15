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
/* The game's X (or Y), read as an action while a menu is up, stays on the controller button the field has it
 * on (ports a mask, buttons PAD_BUTTON_X/Y): the battle menus' X while an audience member winds up a throw.
 * The menu layer then moves only what doesn't take it from there; as a mark, for this frame and two more. */
void portMenuMarkKeep(unsigned ports, unsigned buttons);
/* the game's face buttons kept so for a port now (portMenuLayerFrame's) */
unsigned portMenuLayerKeep(int port);
/* A help that shows the field's buttons is up over a menu (the battle's action command help, its buttons
 * pressed after the menu closes): the button pictures stay the field's; the menu's B still backs out */
void portMenuMarkFieldPictures(unsigned ports);
/* The menu layer's places (port/include/port/menu_buttons.h), for a controller whose confirm and back give the
 * game's face buttons confirm and back (0-3: A B X Y) out of the menus: to[i] the game's face button that the
 * one giving i out of the menus gives in them. Only confirm and back move (onto A and B), and only what they
 * displace moves with them: X and Y stay where the field has them whenever they can, keep's (PAD_BUTTON_*)
 * never move. Nonzero when anything moves. */
int portMenuLayerPlace(int confirm, int back, unsigned keep, int to[4]);
/* tests ($TTYD_INPUT): a script read the game's buttons (evt_sub.c's evt_key_get_*): logged when it has a face
 * button, with the menu layer's state then (a lesson's read: off, the field's buttons) */
void portMenuScriptKeys(unsigned keys);
/* the touch controls: a tap at (nx, ny), 0-1 across the picture, is on a zone (portMenuTapZone) */
int portMenuTapInZone(float nx, float ny);
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
/* A place a tap is wanted on this frame though no list is up (the same units, x0..x1, y0..y1): a paper note's
 * sheet, balloon and choices, the players' bars. The touch controls hand a tap there over (portMenuTap), as
 * they do with a list up, instead of pressing A for it; a zone lasts 3 frames. */
void portMenuTapZone(float x0, float y0, float x1, float y1);
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
