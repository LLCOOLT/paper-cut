#ifndef PORT_PLAYMODES_H
#define PORT_PLAYMODES_H

/* Play and challenge modes over the story (docs/MODES.md), each off until
 * started or turned on, so the normal game is unchanged (and online co-op's
 * fingerprints with it):
 *
 * - Chapter select (port/src/aurora/chapters_aurora.c): once a file has
 *   finished the game, any chapter's story played again with the file's
 *   Mario; the file's story is put back as it was when the chapter ends.
 *
 * - Remix modes (port/src/aurora/remix_aurora.c), settings that change play:
 *   "remix_enemies" (each field enemy's fight drawn from its area's, by
 *   "remix_seed"), "remix_badge_draft" (three badges to choose from at each
 *   chapter's title card) and "hardcore_next" (the next new file has one
 *   life; its Game Over ends it for good and saves a run being recorded as
 *   a replay, a ghost to race).
 *
 * The game side runs on the game's frame (a marioStMain hook); the settings
 * page (port/settings/features/play_modes.cpp) asks for things through these
 * calls, which are safe from its thread. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- chapter select ------------------------------------------------------ */

#define PORT_CHAPTERS 8

/* The file has finished the game (the story's back in Rogueport after the credits). */
int portChapterSelectUnlocked(void);
/* Starts chapter 1-8 from its title card; 0 and why when it can't now. */
int portChapterStart(int chapter, const char** why);
/* The chapter being played again (1-8), or 0. */
int portChapterActive(void);
/* Ends the replay now: the file's story as it was, Mario back in the room it started from. */
void portChapterAbandon(void);
/* The last replay's end, for the page: the chapter (0 none) and its time in ms;
 * a different count each time one ends. */
int portChapterLastResult(int* chapter, unsigned long long* ms);
/* Milliseconds into the replay now. */
unsigned long long portChapterElapsed(void);

/* --- remix modes --------------------------------------------------------- */

/* The remix settings declared (the page and the game call it). */
void portRemixDeclare(void);
/* A hardcore (one-life) file is in play: no Retry on Game Over, no save state loads. */
int portHardcoreActive(void);
/* The remix's badge draft is up (save states refused meanwhile). */
int portRemixDrafting(void);

/* --- trick practice (port/src/aurora/tricks_aurora.c) --------------------- */

/* The set-ups: the built-in rooms first, then <user>/tricks.txt's spots. */
int portTrickCount(void);
const char* portTrickName(int i);
int portTrickBuiltin(int i);
void portTricksReload(void);
/* Warps to set-up i; 0 and why when it can't now. */
int portTrickWarp(int i, const char** why);
/* Keeps where Mario is (the room, the story, his place) as a spot named name. */
int portTrickSaveHere(const char* name, const char** why);

/* --- ghost races (port/src/aurora/ghost_aurora.c) ------------------------ */

/* The recording's ghost track as the replay file's last section (its size; 0
 * none); with out NULL or too small, only the size. */
size_t portGhostBlob(unsigned char* out, size_t room);
/* A replay file's rest after its checks holds a ghost track. */
int portGhostHasTrack(const unsigned char* data, size_t size);
/* A race against that track begins (from the start of the program); 0: not one. */
int portGhostRaceBegin(const unsigned char* data, size_t size, const char* name);
int portGhostRacing(void);
const char* portGhostRaceName(void);
double portGhostRaceSeconds(void);
/* The player's time in the room just entered minus the ghost's (seconds);
 * returns a count that changes with each room matched. */
int portGhostDelta(float* seconds);

/* --- the Pit of 100 Trials in co-op (port/src/aurora/pit_coop_aurora.c) --- */

/* The file has found the Pit (or is past chapter 2). */
int portPitCoopUnlocked(void);
/* A run from the Pit's first floor, the second player on the partner; 0 and why when it can't now. */
int portPitCoopStart(const char** why);
void portPitCoopStop(void);
int portPitCoopActive(void);
int portPitCoopFloor(void); /* 1-100 */
unsigned long long portPitCoopElapsed(void);
/* The last run's end (floor reached, ms); a count that changes as each ends. */
int portPitCoopResult(int* floor, unsigned long long* ms);
/* The deepest floor and the best time to the bottom (0: none). */
int portPitCoopBest(int* floor, unsigned long long* ms);

/* The game side, each frame (playmodes_aurora.c's marioStMain hook), and its hooks. */
void portChaptersFrame(void);
void portRemixFrame(void);
void portTricksFrame(void);
void portGhostFrame(void);
void portPitCoopFrame(void);
void portRemixInit(void);

/* Once, from the maps' registration (mods.cpp, after modes_aurora.c's). */
void portPlayModesInit(void);

#ifdef __cplusplus
}
#endif

#endif
