/* The game's clock on Aurora (aurora_overlay/dolphin/os.h routes OSGetTime
 * and OSGetTick here). Normally Aurora's real clock. With $TTYD_FAST, as in
 * the headless build (port/src/headless/sdk/os_pc.c): game time is virtual,
 * starting at the real time of the first read and advancing one 60 Hz frame
 * per retrace (portFastClockFrame, from vi_aurora.c), plus 1 us per read so
 * a busy-wait on the clock still ends. */
#include <dolphin/os.h>
#include <stdlib.h>
#include "port/platform.h"

#undef OSGetTime
#undef OSGetTick
/* Aurora's own clock: the overlay's renames above also renamed its
 * declarations, and undeclared these returned an int (time cut to 32 bits). */
OSTime OSGetTime(void);
OSTick OSGetTick(void);

static int sFast = -1;
static OSTime sBase, sVirtual;
/* portPause: the clock stands still while paused, and the real time spent
 * paused is left out of it afterwards */
static OSTime sPaused, sPauseStart;
static BOOL sClockPaused;

static BOOL fast(void) {
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
        sBase = OSGetTime();
    }
    return sFast;
}

void portFastClockFrame(void) {
    if (fast()) {
        sVirtual += OS_TIMER_CLOCK / 60;
    }
}

OSTime portOSGetTime(void) {
    if (fast()) {
        sVirtual += OSMicrosecondsToTicks(1);
        return sBase + sVirtual;
    }
    return (sClockPaused ? sPauseStart : OSGetTime()) - sPaused;
}

void portClockPause(void) {
    sPauseStart = OSGetTime();
    sClockPaused = TRUE;
}

void portClockResume(void) {
    sPaused += OSGetTime() - sPauseStart;
    sClockPaused = FALSE;
}

OSTick portOSGetTick(void) {
    return fast() ? (OSTick)portOSGetTime() : OSGetTick();
}
