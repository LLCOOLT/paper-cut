#ifndef PORT_MODES_H
#define PORT_MODES_H

/* Opt-in modes over the game's own battle setups (port/src/aurora/modes_aurora.c,
 * the Modes tab in port/settings/features/modes.cpp):
 *   boss rush          every boss in story order, from a chosen loadout, timed
 *   endless Glitz Pit  the Glitz Pit's teams one after another with a random
 *                      match condition each and stronger enemies each round,
 *                      once the story's Glitz Pit is done
 *   boss refights      any one boss this file has beaten in the story, fought
 *                      again; then back to the room the refight started in
 * Each fight is on a small map the mode adds in the boss's (or the Glitz
 * Pit's) area, so the area's battle table is the game's own. Nothing changes
 * until a mode is started from the Modes tab. */

#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_MODE_NONE, PORT_MODE_BOSS_RUSH, PORT_MODE_ENDLESS_PIT, PORT_MODE_REFIGHT };
enum { PORT_LOADOUT_OWN, PORT_LOADOUT_MID, PORT_LOADOUT_LATE, PORT_LOADOUT_COUNT };

typedef struct PortModeStatus {
    int mode;              /* PORT_MODE_*, NONE when none runs */
    int step, steps;       /* the boss (from 1) of steps; the Glitz Pit's round (steps 0) */
    const char* next;      /* the boss or team being fought */
    const char* condition; /* the Glitz Pit match's condition (the game's text), or NULL */
    unsigned long long ms; /* time since the start */
    int finished;          /* the last one ended the run: 1 won, 2 lost; ms is the final time */
    int loadout;           /* the boss rush's PORT_LOADOUT_* */
} PortModeStatus;

/* Starts a mode in the field; 0 and why when it can't now. */
int portModeStart(int mode, int loadout, const char** why);
/* Starts a mode on the next frame in the field, unchecked (a leaderboard
 * run's start, asked for at start-up: port/include/port/boards.h). */
void portModeQueue(int mode, int loadout);
/* Ends it (Mario stays where he is; a boss rush's loadout is taken back). */
void portModeStop(void);
/* Each frame: the run's state (a run ends at a Game Over or the title). */
const PortModeStatus* portModeStatus(void);
/* The endless Glitz Pit is there once the story's Glitz Pit is done. */
int portModePitUnlocked(void);

/* Boss refights: the bosses (in story order), their names, and whether this
 * file has beaten them in the story. */
int portModeBossCount(void);
const char* portModeBossName(int boss);
int portModeBossBeaten(int boss);
/* Refights boss (0..count-1) with a loadout; 0 and why when it can't now. */
int portModeRefight(int boss, int loadout, const char** why);

/* The maps the modes add (mods.cpp's host maps): registered once. */
void portModesRegisterMaps(void);

#ifdef __cplusplus
}
#endif

#endif
