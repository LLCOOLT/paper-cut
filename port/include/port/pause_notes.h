#pragma once
/* The Start menu's Online tab's Paper notes page (port/src/aurora/pause_notes_aurora.c,
 * port/include/port/notes.h): a page of its own over the tab's, as its room
 * list. Leave a note where Mario stands (a phrase and a word), rate this
 * room's others, fetch them again. */
#include "win/win_main.h"

#ifdef __cplusplus
extern "C" {
#endif

void portPauseNotesOpen(void);
void portPauseNotesClose(void);
int portPauseNotesIsOpen(void);
/* each frame it's open: 0 once B closed it */
int portPauseNotesMain(WindowWork* wp);
void portPauseNotesDisp(WindowWork* wp, f32 x, f32 y);
/* the help box's text (English) for the selected row */
const char* portPauseNotesHelp(void);

#ifdef __cplusplus
}
#endif
