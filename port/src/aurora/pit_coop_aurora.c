/* The Pit of 100 Trials in co-op (port/include/port/playmodes.h): a run
 * down the Pit from its first floor with the second player on the partner
 * (Multiplayer > Second player, turned on for the run and put back after),
 * the floor and the time over the picture, the deepest floor and the best
 * time to the bottom kept in <user>/modes.txt's neighbour pit_coop.txt.
 *
 * The start is the Pit's own: GSW_JON_FLOOR to 0, and the map change the
 * pipe in Rogueport's underground makes (jon_00, "dokan_2"). The run ends
 * when Mario leaves the Pit (its pipe home, or the 100th floor's way out), at
 * a Game Over or at the title; then he goes back to the room he started in
 * if he isn't in Rogueport already. Not during online co-op: there both
 * players take the Pit's pipe together, as in the story. */
#include "port/playmodes.h"
#include "port/modes.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mariost.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <string.h>

static int sActive;          /* a run under way */
static int sEntered;         /* the Pit's first map reached */
static int sStartAsked;      /* __atomic */
static int sStopAsked;       /* __atomic */
static int sSavedSecond = -1; /* "second_player" as it was */
static char sReturnMap[16];
static Uint64 sStartedAt;
static int sResultCount, sResultFloor;
static unsigned long long sResultMs;
static int sBestFloor;
static unsigned long long sBestMs;
static int sBestRead;

static void bestPath(char* out, size_t size) {
    snprintf(out, size, "%s/pit_coop.txt", portSettingsUserPath());
}

static void readBest(void) {
    char p[1024];
    FILE* f;
    if (sBestRead) {
        return;
    }
    sBestRead = 1;
    bestPath(p, sizeof(p));
    f = fopen(p, "r");
    if (f != NULL) {
        if (fscanf(f, "floor %d ms %llu", &sBestFloor, &sBestMs) != 2) {
            sBestFloor = 0;
            sBestMs = 0;
        }
        fclose(f);
    }
}

static void writeBest(void) {
    char p[1024];
    FILE* f;
    bestPath(p, sizeof(p));
    f = fopen(p, "w");
    if (f != NULL) {
        fprintf(f, "floor %d ms %llu\n", sBestFloor, sBestMs);
        fclose(f);
    }
}

int portPitCoopUnlocked(void) {
    return gp != NULL && (swByteGet(GSW_JON_DEEPEST_FLOOR) > 0 || swByteGet(GSW_STORY_PROGRESS) >= STORY_STG2_END_TO_GLITZVILLE);
}

int portPitCoopActive(void) {
    return __atomic_load_n(&sActive, __ATOMIC_SEQ_CST);
}

int portPitCoopFloor(void) {
    return sActive && gp != NULL ? swByteGet(GSW_JON_FLOOR) + 1 : 0;
}

unsigned long long portPitCoopElapsed(void) {
    return sActive ? SDL_GetTicks() - sStartedAt : 0;
}

int portPitCoopResult(int* floor, unsigned long long* ms) {
    *floor = sResultFloor;
    *ms = sResultMs;
    return sResultCount;
}

int portPitCoopBest(int* floor, unsigned long long* ms) {
    readBest();
    *floor = sBestFloor;
    *ms = sBestMs;
    return sBestFloor > 0;
}

int portPitCoopStart(const char** why) {
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (portOnlineActive()) {
        *why = "In online co-op, take the Pit's pipe in Rogueport together.";
        return 0;
    }
    if (portReplayActive()) { /* (recording too: a start from the page is no input) */
        *why = "Not during a replay.";
        return 0;
    }
    if (!portPitCoopUnlocked()) {
        *why = "The Pit of 100 Trials opens once this file has found it (or after chapter 2).";
        return 0;
    }
    if (portPitCoopActive() || portChapterActive() ||
        (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished)) {
        *why = "Not while another mode runs.";
        return 0;
    }
    __atomic_store_n(&sStartAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

void portPitCoopStop(void) {
    __atomic_store_n(&sStopAsked, 1, __ATOMIC_SEQ_CST);
}

static void secondPlayerBack(void) {
    int* v = portSettingsFind("second_player");
    if (v != NULL && sSavedSecond >= 0) {
        *v = sSavedSecond;
    }
    portSettingsExtraSaveAs("second_player", 0, 0);
    sSavedSecond = -1;
}

static void endRun(int goHome) {
    const int floor = gp != NULL ? swByteGet(GSW_JON_FLOOR) + 1 : 0;
    const int bottom = gp != NULL && swByteGet(GSW_JON_FLOOR) >= 99;
    sResultFloor = sEntered ? floor : 0;
    sResultMs = SDL_GetTicks() - sStartedAt;
    sResultCount++;
    readBest();
    if (sResultFloor > sBestFloor) {
        sBestFloor = sResultFloor;
    }
    if (bottom && (sBestMs == 0 || sResultMs < sBestMs)) {
        sBestMs = sResultMs;
    }
    writeBest();
    printf("[pit] co-op run over at floor %d, %llu ms\n", sResultFloor, sResultMs);
    fflush(stdout);
    sActive = 0;
    sEntered = 0;
    secondPlayerBack();
    if (goHome && gp != NULL && strncmp(gp->currentMapName, "tik", 3) != 0 &&
        strncmp(gp->currentMapName, "gor", 3) != 0) {
        seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "tik_06", NULL);
    }
}

void portPitCoopFrame(void) {
    const SequenceType seq = seqGetSeq();
    if (sActive) {
        if (seq == SEQ_GAME_OVER || seq == SEQ_TITLE) {
            endRun(0);
            return;
        }
        if (seq != SEQ_GAME || gp == NULL) {
            return;
        }
        if (strncmp(gp->currentMapName, "jon", 3) == 0) {
            sEntered = 1;
        } else if (sEntered || __atomic_load_n(&sStopAsked, __ATOMIC_SEQ_CST)) {
            __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
            endRun(1); /* out of the Pit */
            return;
        }
        if (__atomic_exchange_n(&sStopAsked, 0, __ATOMIC_SEQ_CST)) {
            endRun(1);
        }
        return;
    }
    __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
    if (seq == SEQ_GAME && gp != NULL && !gp->inBattle && __atomic_exchange_n(&sStartAsked, 0, __ATOMIC_SEQ_CST)) {
        int* v = portSettingsFind("second_player");
        snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
        if (v != NULL) {
            sSavedSecond = *v;
            *v = 1; /* for the run; put back after (and never saved meanwhile) */
            portSettingsExtraSaveAs("second_player", 1, sSavedSecond);
        }
        swByteSet(GSW_JON_FLOOR, 0);
        sActive = 1;
        sEntered = 0;
        sStartedAt = SDL_GetTicks();
        printf("[pit] co-op run from %s\n", sReturnMap);
        fflush(stdout);
        seqSetSeq(SEQ_MAP_CHANGE, "jon_00", "dokan_2");
    }
}
