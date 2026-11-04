#pragma once
/* The Journal's "where next?" hint (port/src/aurora/journal_hint_aurora.c):
 * Y on the Start menu's Journal tab shows where the story goes next. */
#ifdef __cplusplus
extern "C" {
#endif

/* msgSearch: the hint's messages ("pc_menu_kiroku": the Journal tab's help
 * with how to ask; "pc_menu_hint": the hint), or NULL */
const char* portJournalHintMsg(const char* tag);

#ifdef __cplusplus
}
#endif
