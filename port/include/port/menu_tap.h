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
/* A selection list is up (taps on the picture are its rows, not A). */
int portMenuOpen(void);
#ifndef __cplusplus
struct WinMgrSelect;
struct WinMgrEntry;
/* The selection list's frame: the tapped row (cursor index), or -1. */
int portMenuTapRow(struct WinMgrSelect* sel, struct WinMgrEntry* list);
extern int portMenuConfirm; /* this frame: the selected row was tapped (A) */
#endif

#ifdef __cplusplus
}
#endif

#endif
