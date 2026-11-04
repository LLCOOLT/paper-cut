#pragma once
/* The Start menu's bindings page (port/src/aurora/pause_bindings_aurora.c):
 * player 1's keys and controller buttons, for the game's buttons and Paper
 * Cut's actions, in the game's windows and font. A settings category's row
 * opens it; while it is open the category's page hands it each frame:
 *
 *   portPauseBindingsOpen(wp)        on the row's A: the page from its top
 *   portPauseBindingsMain(wp)        each frame instead of the category's own input: 0 stays, -1 back
 *   portPauseBindingsDisp(wp, x, y, inside)  draws its rows where the category's would be (inside:
 *                                    the menu's focus is in the tab), after its window and panel
 *   portPauseBindingsHelp(tag)       msgSearch's text for its help line's tags ("pc_menu_bindings_*"), or NULL
 */
#include "win/win_main.h"

#ifdef __cplusplus
extern "C" {
#endif

void portPauseBindingsOpen(WindowWork* wp);
s32 portPauseBindingsMain(WindowWork* wp);
void portPauseBindingsDisp(WindowWork* wp, f32 x, f32 y, int inside);
const char* portPauseBindingsHelp(const char* tag);

#ifdef __cplusplus
}
#endif
