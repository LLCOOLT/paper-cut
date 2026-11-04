/* Replays, the pad read's side (port/include/port/replay.h).
 *
 * Recording: each pad read's input of controllers 1 and 2 (after the
 * keyboard, the view's controls and online co-op's: what the game gets),
 * run-length, and controllers 3 and 4 none, in this game as in the
 * playback's. Every PORT_REPLAY_CHECK_EVERY reads the game's fingerprint
 * (online_pc.c) is kept, taken at the pad read before the frame plays.
 *
 * Playback: the controllers' input replaced by the recording's, frame by
 * frame, the fingerprints compared as the same frames come: one that
 * differs means the game went out of step (another version, other mods, a
 * setting...), said once and kept for the label. B on controller 1 asks to
 * stop. Past the end nothing is pressed. */
#include "port/online.h"
#include "port/replay.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IDLE, RECORDING, PLAYING };

static int sMode;
static unsigned sFrame;
static PortReplayRun* sRuns;
static size_t sRunCount, sRunCap;
static PortReplayCheck* sChecks;
static size_t sCheckCount, sCheckCap;
/* playback */
static unsigned sLength, sChecked, sInStep;
static size_t sRunAt, sCheckAt;
static unsigned sInRun; /* frames of sRuns[sRunAt] played */
static long sOutOfStep = -1;
static int sStopAsked, sFinished;
static u16 sLastButtons;

static void* grow(void* p, size_t* cap, size_t need, size_t size) {
    if (need <= *cap) {
        return p;
    }
    *cap = *cap < 64 ? 64 : *cap * 2;
    if (*cap < need) {
        *cap = need;
    }
    p = realloc(p, *cap * size);
    if (p == NULL) {
        fprintf(stderr, "[replay] out of memory\n");
        abort();
    }
    return p;
}

static void clearAll(void) {
    sRunCount = sCheckCount = 0;
    sFrame = sLength = sChecked = sInStep = 0;
    sRunAt = sCheckAt = 0;
    sInRun = 0;
    sOutOfStep = -1;
    sStopAsked = sFinished = 0;
    sLastButtons = 0;
}

void portReplayRecordBegin(void) {
    clearAll();
    sMode = RECORDING;
    printf("[replay] recording begins\n");
    fflush(stdout);
}

void portReplayRecordEnd(void) {
    if (sMode == RECORDING) {
        sMode = IDLE;
        printf("[replay] recording ends: %u frames, %u runs, %u checks\n", sFrame, (unsigned)sRunCount,
               (unsigned)sCheckCount);
        fflush(stdout);
    }
}

int portReplayRecording(void) {
    return sMode == RECORDING;
}

const PortReplayRun* portReplayRuns(size_t* count) {
    *count = sRunCount;
    return sRuns;
}

const PortReplayCheck* portReplayChecks(size_t* count) {
    *count = sCheckCount;
    return sChecks;
}

void portReplayPlayBegin(const PortReplayRun* runs, size_t runCount, const PortReplayCheck* checks,
                         size_t checkCount) {
    size_t i;
    clearAll();
    sRuns = grow(sRuns, &sRunCap, runCount, sizeof(*sRuns));
    sChecks = grow(sChecks, &sCheckCap, checkCount, sizeof(*sChecks));
    if (runCount > 0) {
        memcpy(sRuns, runs, runCount * sizeof(*sRuns));
    }
    if (checkCount > 0) {
        memcpy(sChecks, checks, checkCount * sizeof(*sChecks));
    }
    sRunCount = runCount;
    sCheckCount = checkCount;
    for (i = 0; i < runCount; i++) {
        sLength += runs[i].frames;
    }
    sMode = PLAYING;
    printf("[replay] playback begins: %u frames, %u checks\n", sLength, (unsigned)checkCount);
    fflush(stdout);
}

int portReplayPlaying(void) {
    return sMode == PLAYING;
}

int portReplayFinished(void) {
    return sMode == PLAYING && sFinished;
}

int portReplayStopAsked(void) {
    return sStopAsked;
}

long portReplayOutOfStep(void) {
    return sOutOfStep;
}

int portReplayActive(void) {
    return sMode != IDLE;
}

unsigned portReplayFrame(void) {
    return sFrame;
}

unsigned portReplayLength(void) {
    return sMode == PLAYING ? sLength : sFrame;
}

static void record(const unsigned char pads[PORT_REPLAY_PADS][PORT_REPLAY_PAD_BYTES]) {
    if (sRunCount > 0 && sRuns[sRunCount - 1].frames < 0xFFFFFFFFu &&
        memcmp(sRuns[sRunCount - 1].pads, pads, sizeof(sRuns[0].pads)) == 0) {
        sRuns[sRunCount - 1].frames++;
        return;
    }
    sRuns = grow(sRuns, &sRunCap, sRunCount + 1, sizeof(*sRuns));
    sRuns[sRunCount].frames = 1;
    memcpy(sRuns[sRunCount].pads, pads, sizeof(sRuns[0].pads));
    sRunCount++;
}

static void check(unsigned frame) {
    const unsigned long long hash = portOnlineFingerprint();
    if (sMode == RECORDING) {
        sChecks = grow(sChecks, &sCheckCap, sCheckCount + 1, sizeof(*sChecks));
        sChecks[sCheckCount].frame = frame;
        sChecks[sCheckCount].hash = hash;
        sCheckCount++;
        return;
    }
    while (sCheckAt < sCheckCount && sChecks[sCheckAt].frame < frame) {
        sCheckAt++;
    }
    if (sCheckAt >= sCheckCount || sChecks[sCheckAt].frame != frame) {
        return;
    }
    sChecked++;
    if (sChecks[sCheckAt].hash != hash) {
        if (sOutOfStep < 0) {
            sOutOfStep = (long)frame;
            printf("[replay] out of step at frame %u: %016llx here, %016llx recorded\n", frame, hash,
                   sChecks[sCheckAt].hash);
            fflush(stdout);
        }
    } else {
        sInStep++;
        printf("[replay] in step at frame %u\n", frame);
        fflush(stdout);
    }
    sCheckAt++;
}

static void noController(PADStatus* pads) {
    int i;
    for (i = PORT_REPLAY_PADS; i < PAD_MAX_CONTROLLERS; i++) {
        memset(&pads[i], 0, sizeof(pads[i]));
        pads[i].err = PAD_ERR_NO_CONTROLLER;
    }
}

void portReplayApplyPad(PADStatus* pads) {
    const unsigned f = sFrame;
    int i;
    if (sMode == IDLE) {
        return;
    }
    if (f > 0 && f % PORT_REPLAY_CHECK_EVERY == 0) {
        check(f);
    }
    if (sMode == RECORDING) {
        unsigned char packed[PORT_REPLAY_PADS][PORT_REPLAY_PAD_BYTES];
        noController(pads);
        for (i = 0; i < PORT_REPLAY_PADS; i++) {
            portOnlinePackPad(&pads[i], packed[i]);
        }
        record((const unsigned char(*)[PORT_REPLAY_PAD_BYTES])packed);
        sFrame++;
        return;
    }
    /* playback: B (newly pressed) on the player's own controller 1 asks to stop */
    if ((pads[0].button & PAD_BUTTON_B) && !(sLastButtons & PAD_BUTTON_B) && pads[0].err == PAD_ERR_NONE) {
        if (!sStopAsked) {
            printf("[replay] stop asked at frame %u\n", f);
            fflush(stdout);
        }
        sStopAsked = 1;
    }
    sLastButtons = pads[0].err == PAD_ERR_NONE ? pads[0].button : 0;
    while (sRunAt < sRunCount && sInRun >= sRuns[sRunAt].frames) {
        sRunAt++;
        sInRun = 0;
    }
    if (sRunAt >= sRunCount) {
        if (!sFinished) {
            sFinished = 1;
            printf("[replay] playback finished at frame %u: %u of %u checks in step\n", f, sInStep, sChecked);
            fflush(stdout);
        }
        for (i = 0; i < PORT_REPLAY_PADS; i++) {
            memset(&pads[i], 0, sizeof(pads[i]));
            pads[i].err = PAD_ERR_NONE;
        }
    } else {
        for (i = 0; i < PORT_REPLAY_PADS; i++) {
            portOnlineUnpackPad(sRuns[sRunAt].pads[i], &pads[i]);
        }
        sInRun++;
    }
    noController(pads);
    sFrame++;
}
