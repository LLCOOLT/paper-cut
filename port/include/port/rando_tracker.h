#ifndef PORT_RANDO_TRACKER_H
#define PORT_RANDO_TRACKER_H

/* The randomizer's check tracker (docs/RANDOMIZER.md, "The tracker"): the
 * places this file's or this run's seed shuffled, by area, each found (its
 * flag set), reachable now (the seed's logic, with what Mario holds) or not
 * yet. Read-only: it reads the game's flags and the pouch, never changes them.
 * Shown in the Journal (port/src/aurora/tracker_journal_aurora.c, which the
 * second screen's menu runs as well) and on the Randomizer tab with a search
 * box (port/settings/features/randomizer.cpp). port/archipelago/rando_tracker.cpp,
 * Aurora. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_TRACKER_LOCKED = 0,    /* not reachable with what is in hand */
    PORT_TRACKER_REACHABLE = 1, /* reachable now, not found yet */
    PORT_TRACKER_FOUND = 2,
};

typedef struct PortTrackerArea {
    const char* name; /* the apworld's region name (English) */
    int total, found, reachable; /* reachable: not found yet but reachable now */
} PortTrackerArea;

/* Made again from the game's state now: 1 if this file (Paper Cut's open
 * world) or this run (the built-in randomizer) has a seed to track. */
int portTrackerRefresh(void);
/* 1 while a seed is tracked (as the last refresh found; cheap) */
int portTrackerAvailable(void);
/* 1 if its logic says what is reachable (0: every place counts as reachable:
 * the game's own story, or no logic) */
int portTrackerHasLogic(void);
int portTrackerAreaCount(void);
const PortTrackerArea* portTrackerArea(int i);
int portTrackerPlaceCount(int area);
/* the place's name inside its area (the apworld's, after its room) */
const char* portTrackerPlaceName(int area, int i);
/* the room it is in (the apworld's name before the colon) */
const char* portTrackerPlaceRoom(int area, int i);
int portTrackerPlaceState(int area, int i);
/* all areas: the total, and the found and reachable counts out */
int portTrackerTotals(int* found, int* reachable);

/* --- Aurora: the Journal's Tracker (port/src/aurora/tracker_journal_aurora.c; its button in
 * pause_settings_aurora.c), as its Stats --- */
struct WindowWork;
/* the entry shows: a seed is tracked (this file's or this run's) */
int portTrackerJournalEntry(void);
void portTrackerJournalButton(struct WindowWork* wp, float x, float y, int selected);
void portTrackerJournalOpen(struct WindowWork* wp);
/* once a frame: 0 stay, -1 back to the entries, -2 the menu closed */
int portTrackerJournalMain(struct WindowWork* wp);
void portTrackerJournalDisp(struct WindowWork* wp, float x, float y);
/* the help box's text for its tags ("pc_menu_tracker..."), or NULL */
const char* portTrackerJournalMsg(const char* tag);

#ifdef __cplusplus
}
#endif

#endif
