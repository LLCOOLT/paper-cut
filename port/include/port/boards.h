#ifndef PORT_BOARDS_H
#define PORT_BOARDS_H
/* Leaderboards backed by replays (docs/LEADERBOARDS.md): boss rush and Pit of
 * 100 Trials times on the relay, each with the replay that made it.
 *
 * - A run is a replay (port/include/port/replay.h) recorded for a board: the
 *   game starts again from where the player is, the run is armed at start-up
 *   (a boss rush starts at once, as the Modes tab's would; a Pit run's clock
 *   starts on entering floor 1), and its time is the frames from its start to
 *   its end (the boss rush won; floor 100 reached). The recording is saved
 *   then, with a MODE trailer: the board and the frames.
 * - The run's clock (port/src/aurora/board_run_aurora.c) only reads the game;
 *   the one thing a run changes, the boss rush's start, is asked for at
 *   start-up in the recording and its playback alike, so it happens on the
 *   same frame in both: the playback plays out the same.
 * - Checked by re-simulating: a run goes on a board only after this game
 *   played its replay back and the run ended on the same frame; each game
 *   that plays an entry back tells the relay whether it did too (the board
 *   shows how many checked it). port/settings/features/boards.cpp.
 *
 * None of it is on online co-op's path: replays are off during online play. */

#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_BOARD_RUSH_MID, PORT_BOARD_RUSH_LATE, PORT_BOARD_PIT, PORT_BOARD_COUNT };

/* the board's relay name ("bossrush-mid", ...) and its English title */
const char* portBoardKey(int board);
const char* portBoardTitle(int board);

/* --- the run's clock (board_run_aurora.c) --- */

enum { PORT_BOARD_RUN_NONE, PORT_BOARD_RUN_ARMED, PORT_BOARD_RUN_RUNNING, PORT_BOARD_RUN_DONE, PORT_BOARD_RUN_FAILED };

/* At start-up (the recording's or playback's): this run is for board. */
void portBoardRunArm(int board);
/* src/system.c, each frame. */
void portBoardRunFrame(void);
/* the state, the board, and the run's frames so far (its time once done) */
int portBoardRunState(void);
int portBoardRunBoard(void);
unsigned portBoardRunFrames(void);

#ifdef __cplusplus
}
#endif

#endif
