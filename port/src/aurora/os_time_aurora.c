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

/* $TTYD_CLOCK_DAYS=N (tests, docs/PC_PORT.md): the real clock N days on (or
 * back, negative), for every read of it here: a run's dates (the daily seed,
 * the shop specials, the raid's week) as on another day, e.g. a replay played
 * back "tomorrow", which must still take its dates from the recording. */
static OSTime realNow(void) {
    static int sRead;
    static OSTime sShift;
    if (!sRead) {
        const char* days = getenv("TTYD_CLOCK_DAYS");
        sRead = 1;
        sShift = days != NULL ? (OSTime)atoi(days) * 86400 * (OSTime)OS_TIMER_CLOCK : 0;
    }
    return OSGetTime() + sShift;
}
/* The game clock runs at sSpeed percent of the real one (fast-forward and
 * faster battles, speed_aurora.c), and at sPercent % of that (slow speed):
 * sGame was the game time at real time sReal. portPause stops it, and the
 * real time spent paused is left out afterwards. */
static OSTime sGame, sReal;
static int sSpeed = 100, sPercent = 100; /* percent, both */
static BOOL sClockPaused, sClockStarted;

static BOOL fast(void) {
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
        sBase = realNow();
    }
    return sFast;
}

/* A shared battle online (port/include/port/statesync.h) or a replay: the
 * game clock virtual from here, as with $TTYD_FAST, from one start (base), so
 * every game reads the same times; the frames still keep to the wall clock
 * (vi_aurora.c paces them). */
void portLockstepBegin(s64 base) {
    sFast = 1;
    sBase = base;
    sVirtual = 0;
}

/* the game runs the same from the same input: $TTYD_FAST, a shared battle, a replay */
int portLockstep(void) {
    return fast();
}

s64 portLockstepClock(void) {
    return fast() ? sBase + sVirtual : realNow();
}

/* days since 1970-01-01 (UTC) by the lockstep clock (OSTime counts from 2000-01-01, UTC: 10957 days on) */
unsigned portLockstepDay(void) {
    const OSTime t = (OSTime)portLockstepClock();
    return t > 0 ? (unsigned)(t / ((OSTime)OS_TIMER_CLOCK * 86400)) + 10957u : 10957u;
}

/* the real clock now (the host's start for a shared battle) */
s64 portRealTime(void) {
    return realNow();
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

/* the game clock now, read without moving it (the virtual clock moves 1 us a read) */
static OSTime gameNow(void) {
    if (fast()) {
        return sBase + sVirtual;
    }
    if (!sClockStarted) {
        return realNow();
    }
    return sClockPaused ? sGame : sGame + (realNow() - sReal) * sSpeed * sPercent / 10000;
}

OSTime portOSGetTime(void) {
    if (fast()) {
        sVirtual += OSMicrosecondsToTicks(1);
        return sBase + sVirtual;
    }
    if (!sClockStarted) {
        sClockStarted = TRUE;
        sGame = sReal = realNow();
    }
    return gameNow();
}

s64 portGameClockPeek(void) {
    return gameNow();
}

/* The game time now becomes the base of what follows. Not with the virtual
 * clock ($TTYD_FAST, online, replays), which keeps no base and moves 1 us a
 * read: each caller here (pausing, a change of speed or of slow speed) can
 * run on one player's side only (a window hidden, the app in the background,
 * a speed changed), and a read there put two games out of step at once. */
static void rebase(void) {
    if (fast()) {
        return;
    }
    sGame = portOSGetTime();
    sReal = realNow();
}

void portClockPause(void) {
    rebase();
    sClockPaused = TRUE;
}

void portClockResume(void) {
    sReal = realNow();
    sClockPaused = FALSE;
}

void portClockSetGame(OSTime t) {
    sGame = t;
    if (!sClockPaused) {
        sReal = realNow();
    }
}

void portClockSetSpeedPct(int percent) {
    if (percent != sSpeed) {
        if (!sClockPaused) {
            rebase();
        }
        sSpeed = percent;
    }
}

int portClockSpeedPct(void) {
    return sSpeed;
}

int portClockSpeed(void) {
    return (sSpeed + 99) / 100;
}

void portClockSetPercent(int percent) {
    if (percent != sPercent) {
        if (!sClockPaused) {
            rebase();
        }
        sPercent = percent;
    }
}

OSTick portOSGetTick(void) {
    return fast() ? (OSTick)portOSGetTime() : OSGetTick();
}
