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
/* A selection list is up (taps on the picture are its rows, not A). */
int portMenuOpen(void);
/* With a menu up: a right click or the mouse's back button is B, a notch of
 * the wheel tilts the stick up or down once (menu_mouse.cpp). */
void portMenuBack(void);
void portMenuWheel(int notches);
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
