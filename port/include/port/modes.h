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

#include "port/mode_lending.h"

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
    int from;              /* the boss rush's first step (1: from the start; only then is its time a best) */
} PortModeStatus;

/* Starts a mode in the field; 0 and why when it can't now. A boss rush starts
 * at the "Start at" choice below. */
int portModeStart(int mode, int loadout, const char** why);
/* The boss rush's "Start at", one choice for the Modes tab, the Start menu's
 * form and the Multiplayer page's Boss rush: the story's bosses (0: from the
 * start), then "Paper Mario 64 bosses only" (from Jr. Troopa: the rush takes
 * them in even with "pm64_bosses" off) while they can be fought. Names in
 * English. A choice no longer there (the ROM unlinked, online) is the start. */
int portModeRushStartCount(void);
const char* portModeRushStartName(int choice);
int portModeRushStart(void);
void portModeRushStartSet(int choice);
/* Starts a mode on the next frame in the field, unchecked (a leaderboard
 * run's start, asked for at start-up: port/include/port/boards.h). */
void portModeQueue(int mode, int loadout);
/* Ends it: in a battle, once the battle is over; then the file's pouch and the player's difficulty back and
 * Mario back where the mode started (the modes' maps have no way out). */
void portModeStop(void);
/* Why no mode may start now, or NULL (every mode's start asks it: another mode lending, inside a house the
 * game shows on walking in, a stretch of the story fast travel refuses, someone following Mario). English. */
const char* portModeStartRefusal(void);
/* Each frame: the run's state (a run ends at a Game Over or the title). */
const PortModeStatus* portModeStatus(void);
/* The endless Glitz Pit is there once the story's Glitz Pit is done. */
int portModePitUnlocked(void);

/* Boss refights: the bosses (in story order), their names, and whether this
 * file has beaten them in the story. */
int portModeBossCount(void);
const char* portModeBossName(int boss);
int portModeBossBeaten(int boss);
/* Refights boss (0..count-1) with a loadout; 0 and why when it can't now.
 * Paper Mario 64's bosses are open from the start (portModeBossBeaten 1). */
int portModeRefight(int boss, int loadout, const char** why);
/* The Paper Mario 64 boss list (pm64_media.cpp, the Star Haven corner's board:
 * pm64_board_aurora.c): fight (0..PM64_BOSS_FIGHTS-1, the rush's order) on its
 * own, with a ROM linked whatever "pm64_bosses" says; a loss doesn't end the
 * game; won, lost or fled, back to the room and the place it started from.
 * Winning marks it beaten on this file (portPm64BossBeaten). */
int portModePm64Fight(int fight, int loadout, const char** why);
/* The same, early 1: the boss's first fight where it has one (portPm64FightHasEarly: Jr. Troopa's prologue fight;
 * the lists' "First fight"), else its last (the default). A story meeting in Rogueport is none of these: an
 * ordinary battle on the boss's field NPC (pm64_story_aurora.c), lost to the game's Game Over. */
int portModePm64FightAs(int fight, int loadout, int early, const char** why);

/* Lends Mario boots and hammer (0 none, 1 normal, 2 Super, 3 Ultra) on a lent
 * pouch (a loadout: the boss rush, refights, raids, the dojo, puzzles, versus):
 * the level and the key item it is read back from (pouchGetJumpLv and
 * pouchGetHammerLv recount it from the key items at a hammer swing, a field
 * object, the Mario menu), so the field can't put the file's back. */
void portModeLendGear(int jump, int hammer);
/* After a lent pouch's levels are set: with fitBadges, the badges worn that the
 * lent BP can't carry come off (the last first); then HP, FP and BP are worked
 * out again as the game does (HP Plus, the partners' ranks, the badges' cost)
 * and Mario and every partner start full. */
void portModeLendRevise(int fitBadges);
/* What any mode lends now: portModeLending (port/include/port/mode_lending.h, included above). */

/* A story fight that can't be won on its own and ends interrupted (battle result 5) where the story goes on to
 * another: the entry of the area battle table (files: a map of the area, its first three letters the area) to
 * fight next, or -1. The Shadow Queen's first form (btlsetup_las_tbl's entry 16) is held off for three turns; the story then plays the
 * crystal stars' scene and the real fight, entry 17. A mode, a refight or a puzzle with entry 16 fights entry 17
 * after it, the party healed first as the scene does. */
int portModeStoryPhase2(const char* files, int battle);

/* The maps the modes add (mods.cpp's host maps): registered once. */
void portModesRegisterMaps(void);

#ifdef __cplusplus
}
#endif

#endif
