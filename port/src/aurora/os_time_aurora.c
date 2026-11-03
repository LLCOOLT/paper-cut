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
/* The game clock runs at sSpeed times the real one (fast-forward,
 * speed_aurora.c): sGame was the game time at real time sReal. portPause
 * stops it, and the real time spent paused is left out afterwards. */
static OSTime sGame, sReal;
static int sSpeed = 1;
static BOOL sClockPaused, sClockStarted;

static BOOL fast(void) {
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
        sBase = OSGetTime();
    }
    return sFast;
}

void portFastClockFrame(void) {
    if (fast()) {
        sVirtual += OS_TIMER_CLOCK / portFieldRate();
    }
}

s64 portFastClockGet(void) {
    return sVirtual;
}

void portFastClockSet(s64 t) {
    sVirtual = t;
}

OSTime portOSGetTime(void) {
    if (fast()) {
        sVirtual += OSMicrosecondsToTicks(1);
        return sBase + sVirtual;
    }
    if (!sClockStarted) {
        sClockStarted = TRUE;
        sGame = sReal = OSGetTime();
    }
    return sClockPaused ? sGame : sGame + (OSGetTime() - sReal) * sSpeed;
}

/* the game time now becomes the base of what follows */
static void rebase(void) {
    sGame = portOSGetTime();
    sReal = OSGetTime();
}

void portClockPause(void) {
    rebase();
    sClockPaused = TRUE;
}

void portClockResume(void) {
    sReal = OSGetTime();
    sClockPaused = FALSE;
}

void portClockSetGame(OSTime t) {
    sGame = t;
    if (!sClockPaused) {
        sReal = OSGetTime();
    }
}

void portClockSetSpeed(int speed) {
    if (speed != sSpeed) {
        if (!sClockPaused) {
            rebase();
        }
        sSpeed = speed;
    }
}

int portClockSpeed(void) {
    return sSpeed;
}

OSTick portOSGetTick(void) {
    return fast() ? (OSTick)portOSGetTime() : OSGetTick();
}
