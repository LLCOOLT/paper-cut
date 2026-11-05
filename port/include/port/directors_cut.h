#ifndef PORT_DIRECTORS_CUT_H
#define PORT_DIRECTORS_CUT_H
/* Director's Cut (Modes > Director's Cut; port/src/aurora/directors_cut_aurora.c):
 * content the game's code still has but never uses, put back, opt-in.
 * The setting ("directors_cut") is part of the settings online's host sends,
 * and everything here follows from it and the game's own state, so both
 * games of a session play the same. What is restored, and why the rest
 * isn't yet: docs/DIRECTORS_CUT.md. */
#ifdef __cplusplus
extern "C" {
#endif

/* on */
int portDirectorsCut(void);
/* npcdrv's group choice (npcSetupBattleInfo): a GSWF that picks a field
 * enemy's alternate groups, taken as set (gswf: the flag's number) */
int portDirectorsCutFlag(int gswf);
/* Dazzle (evt_badgeshop.c): the restored badges put in (or, off, taken out
 * of) his stock where the player has none; returns how many there are to add
 * to his menu's size (0 off) */
int portDirectorsCutDazzle(void);
/* msgSearch: the restored content's names and texts (NULL: not one of them) */
const char* portDirectorsCutMsg(const char* tag);

/* The Journal's pages on each restored thing, for the settings page and the
 * Start menu (port/settings/features/directors_cut.cpp): count, then each
 * one's English title and text */
int portDirectorsCutPages(void);
const char* portDirectorsCutPageTitle(int i);
const char* portDirectorsCutPageText(int i);

#ifdef __cplusplus
}
#endif
#endif
