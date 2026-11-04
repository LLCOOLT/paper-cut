#pragma once
/* The Journal's "where next?" hint (port/src/aurora/journal_hint_aurora.c):
 * Y on the Start menu's Journal tab shows where the story goes next. */
#ifdef __cplusplus
extern "C" {
#endif

/* msgSearch: the hint's messages ("pc_menu_kiroku": the Journal tab's help
 * with how to ask; "pc_menu_hint": the hint), or NULL */
const char* portJournalHintMsg(const char* tag);
/* Where the story goes next (the second screen, port/include/port/companion.h):
 * its area (the game names it menu_monosiri_<area>) and the step's English
 * name; 0 at the story's end or with no game. */
int portJournalNextStep(const char** area, const char** name);

#ifdef __cplusplus
}
#endif
