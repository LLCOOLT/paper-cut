/* A leaderboard run's clock (port/include/port/boards.h): armed at start-up
 * for a board, it counts the game's frames from the run's start to its end.
 * It reads the game (the sequence, the map, the Pit's floor, the boss rush's
 * state) and changes nothing; a boss rush's start is the Modes tab's own,
 * asked for at start-up (portModeQueue), so it comes on the same frame in a
 * recording and its playback. */
#include "port/boards.h"
#include "port/modes.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <string.h>

static const char* const kKeys[PORT_BOARD_COUNT] = {"bossrush-mid", "bossrush-late", "pit100"};
static const char* const kTitles[PORT_BOARD_COUNT] = {"Boss rush (mid-game)", "Boss rush (late game)",
                                                      "Pit of 100 Trials"};

static int sBoard = -1;
static int sState;
static unsigned sFrames;

const char* portBoardKey(int board) {
    return board >= 0 && board < PORT_BOARD_COUNT ? kKeys[board] : "";
}

const char* portBoardTitle(int board) {
    return board >= 0 && board < PORT_BOARD_COUNT ? kTitles[board] : "";
}

void portBoardRunArm(int board) {
    if (board < 0 || board >= PORT_BOARD_COUNT) {
        return;
    }
    sBoard = board;
    sState = PORT_BOARD_RUN_ARMED;
    sFrames = 0;
    if (board == PORT_BOARD_RUSH_MID || board == PORT_BOARD_RUSH_LATE) {
        /* started on the first frame in the field (modes_aurora.c's frameHook) */
        portModeQueue(PORT_MODE_BOSS_RUSH, board == PORT_BOARD_RUSH_MID ? PORT_LOADOUT_MID : PORT_LOADOUT_LATE);
    }
    printf("[boards] run armed: %s\n", kKeys[board]);
    fflush(stdout);
}

static void end(int state) {
    sState = state;
    printf("[boards] run %s: %s, %u frames\n", state == PORT_BOARD_RUN_DONE ? "done" : "failed", kKeys[sBoard],
           sFrames);
    fflush(stdout);
}

/* on one of the Pit of 100 Trials' floors */
static int inPit(void) {
    return strncmp(gp->currentMapName, "jon_", 4) == 0;
}

void portBoardRunFrame(void) {
    const int seq = seqGetSeq();
    if (sBoard < 0 || gp == NULL || (sState != PORT_BOARD_RUN_ARMED && sState != PORT_BOARD_RUN_RUNNING)) {
        return;
    }
    if (sState == PORT_BOARD_RUN_RUNNING) {
        sFrames++;
    }
    if (sBoard == PORT_BOARD_PIT) {
        if (seq != SEQ_GAME && seq != SEQ_GAME_OVER && seq != SEQ_TITLE) {
            return; /* (a map change, a battle: the clock runs on) */
        }
        if (sState == PORT_BOARD_RUN_ARMED) {
            /* floor 1 (GSW 1321 is the floor less one) */
            if (seq == SEQ_GAME && inPit() && swByteGet(GSW_JON_FLOOR) == 0) {
                sState = PORT_BOARD_RUN_RUNNING;
                printf("[boards] pit run started\n");
                fflush(stdout);
            }
        } else if (seq != SEQ_GAME || !inPit()) {
            end(PORT_BOARD_RUN_FAILED); /* a Game Over, or out of the Pit */
        } else if (swByteGet(GSW_JON_FLOOR) >= 99) {
            end(PORT_BOARD_RUN_DONE); /* floor 100 */
        }
        return;
    }
    {
        const PortModeStatus* s = portModeStatus();
        if (sState == PORT_BOARD_RUN_ARMED) {
            if (s->mode == PORT_MODE_BOSS_RUSH && !s->finished) {
                sState = PORT_BOARD_RUN_RUNNING;
                printf("[boards] boss rush run started\n");
                fflush(stdout);
            }
        } else if (s->mode != PORT_MODE_BOSS_RUSH) {
            end(PORT_BOARD_RUN_FAILED); /* stopped */
        } else if (s->finished) {
            end(s->finished == 1 ? PORT_BOARD_RUN_DONE : PORT_BOARD_RUN_FAILED);
        }
    }
}

int portBoardRunState(void) {
    return sState;
}

int portBoardRunBoard(void) {
    return sBoard;
}

unsigned portBoardRunFrames(void) {
    return sFrames;
}
