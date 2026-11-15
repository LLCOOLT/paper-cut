/* Undrawn frames on the headless build (port/include/port/toggles.h; Aurora's: port/src/aurora/battery_aurora.c).
 * Headless draws every frame, but for $TTYD_TEST_UNDRAWN (tests): game frames run without their draw, as a
 * phone's do when its GPU falls behind ("keep_game_speed", vi_aurora.c portPacingBehind: online, never two in a
 * row) or with the battery saver (every other frame). The same rules as Aurora's: never in a shared battle (its
 * lockstep, which $TTYD_FAST otherwise makes every test run), and a frame whose draw list has a callback the
 * frame before's hadn't (a pass made once) is drawn.
 *   TTYD_TEST_UNDRAWN=1  every other frame undrawn
 *   TTYD_TEST_UNDRAWN=N  (N >= 2) a frame undrawn about one in N, never two in a row (seeded by the frame) */
#include "port/statesync.h"
#include "port/toggles.h"
#include "driver/dispdrv.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern s32 entry_n;
extern DispEntry* pDispWork;
void VIWaitForRetrace(void);

static int sUndrawn, sSinceDrawn = 1, sDrawFrames = 1;
static unsigned sFrame, sCount;

#define PASS_SLOTS 2048
static int newDrawPass(void) {
    static DispCallback sSeen[2][PASS_SLOTS];
    static unsigned sGen[2][PASS_SLOTS];
    static unsigned sStamp = 1;
    static int sCur;
    const int prev = sCur ^ 1;
    int found = 0;
    s32 i;
    sStamp++;
    for (i = 0; i < entry_n; i++) {
        const DispCallback cb = pDispWork[i].callback;
        const unsigned h = (unsigned)(((uintptr_t)cb >> 4) * 2654435761u) & (PASS_SLOTS - 1);
        unsigned k;
        if (cb == NULL) {
            continue;
        }
        for (k = h; sGen[sCur][k] == sStamp && sSeen[sCur][k] != cb; k = (k + 1) & (PASS_SLOTS - 1)) {
        }
        if (sGen[sCur][k] == sStamp) {
            continue;
        }
        sGen[sCur][k] = sStamp;
        sSeen[sCur][k] = cb;
        for (k = h; sGen[prev][k] == sStamp - 1 && sSeen[prev][k] != cb; k = (k + 1) & (PASS_SLOTS - 1)) {
        }
        if (sGen[prev][k] != sStamp - 1) {
            found = 1;
        }
    }
    sCur = prev;
    return found;
}

int portBatterySkipDraw(void) {
    static int sMode = -1;
    int skip = 0, newPass;
    if (sMode < 0) {
        const char* v = getenv("TTYD_TEST_UNDRAWN");
        sMode = v != NULL ? atoi(v) : 0;
    }
    if (sMode <= 0) {
        return 0;
    }
    newPass = newDrawPass();
    sFrame++;
    if (!portStateSyncSharedBattle()) {
        if (sMode == 1) {
            skip = sFrame & 1;
        } else {
            skip = sSinceDrawn == 1 && ((sFrame * 2654435761u) >> 16) % (unsigned)sMode == 0;
        }
    }
    if (skip && newPass) {
        skip = 0;
    }
    if (skip) {
        sSinceDrawn++;
        if (sCount++ % 600 == 0) {
            printf("[undrawn] %u frames undrawn so far (test)\n", sCount);
            fflush(stdout);
        }
    } else {
        sDrawFrames = sSinceDrawn;
        sSinceDrawn = 1;
    }
    sUndrawn = skip;
    return skip;
}

void portBatteryFrameUndrawn(void) {
    VIWaitForRetrace(); /* (the frame's own port work, as Aurora's retrace(0)) */
}

int portBatteryUndrawn(void) {
    return sUndrawn;
}

int portDrawFrames(void) {
    return sDrawFrames;
}
