/* Pausing the game from outside it: the window minimized or hidden (Aurora's
 * AURORA_PAUSED), the app sent to the background (mobile), or an input
 * script's `pause`. The caller holds the main thread, so the game, its
 * cooperative threads and pending ARQ callbacks stop with it; this stops what
 * runs on its own: the game clock (paused time is skipped, so timers and
 * pacing don't jump on resume) and the audio device. */
#include "port/platform.h"

static int sDepth;

void portPause(void) {
    if (sDepth++ == 0) {
        portClockPause();
        salPcSetPaused(TRUE);
    }
}

void portResume(void) {
    if (sDepth > 0 && --sDepth == 0) {
        portClockResume();
        salPcSetPaused(FALSE);
    }
}

BOOL portIsPaused(void) {
    return sDepth > 0;
}
