/* Dolphin OS replacement: time, arena, interrupts, caches, reporting.
 * The heap allocator itself is the SDK's portable OSAlloc.c. */
#include <dolphin.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "port/platform.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Main RAM is 24 MiB on retail; extra headroom costs nothing on PC. */
#define PC_ARENA_SIZE (128u * 1024 * 1024)

/* OSTime counts timer ticks (bus clock / 4) since 2000-01-01 00:00. */
#define OS_TIME_EPOCH_UNIX 946684800LL

static void* sArenaLo;
static void* sArenaHi;
static void* sArenaBase; /* the arena's start (sArenaLo moves as the game takes from it) */
static OSTime sTimeBase;
static s64 sMonoBaseNs;
static BOOL sInitialized;

static s64 monotonicNs(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (s64)((double)now.QuadPart * 1e9 / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (s64)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#endif
}

static OSTime nsToTicks(s64 ns) {
    /* 40.5 MHz timer: ticks = ns * 81 / 2000. */
    return (OSTime)(ns / 2000 * 81 + (ns % 2000) * 81 / 2000);
}

void OSInit(void) {
    struct timespec ts;
    if (sInitialized) {
        return;
    }
    sInitialized = TRUE;
    timespec_get(&ts, TIME_UTC);
    sTimeBase = nsToTicks(((s64)ts.tv_sec - OS_TIME_EPOCH_UNIX) * 1000000000LL + ts.tv_nsec);
    sMonoBaseNs = monotonicNs();

    sArenaLo = calloc(1, PC_ARENA_SIZE);
    if (sArenaLo == NULL) {
        fprintf(stderr, "OSInit: cannot allocate %u byte arena\n", PC_ARENA_SIZE);
        abort();
    }
    sArenaHi = (u8*)sArenaLo + PC_ARENA_SIZE;
    portCheckHostAddress(sArenaLo, "the game arena");
    sArenaBase = sArenaLo;
}

/* the whole arena (the game's MEM1): game_address_pc.c reads script pointers as offsets into it */
void portHeadlessArena(void** base, size_t* size) {
    *base = sArenaBase;
    *size = PC_ARENA_SIZE;
}

/* Arena: OSArena.c is not used because it reads lomem globals. */
void* OSGetArenaHi(void) { return sArenaHi; }
void* OSGetArenaLo(void) { return sArenaLo; }
void OSSetArenaHi(void* newHi) { sArenaHi = newHi; }
void OSSetArenaLo(void* newLo) { sArenaLo = newLo; }

void* OSAllocFromArenaLo(u32 size, u32 align) {
    uintptr_t p = ((uintptr_t)sArenaLo + align - 1) & ~(uintptr_t)(align - 1);
    sArenaLo = (void*)((p + size + align - 1) & ~(uintptr_t)(align - 1));
    return (void*)p;
}

void* OSAllocFromArenaHi(u32 size, u32 align) {
    uintptr_t p = ((uintptr_t)sArenaHi) & ~(uintptr_t)(align - 1);
    p = (p - size) & ~(uintptr_t)(align - 1);
    sArenaHi = (void*)p;
    return (void*)p;
}

/* $TTYD_FAST: game time is virtual, exactly as on Aurora (os_time_aurora.c),
 * so a run's frames and fingerprints are the same on both backends: it
 * starts at the real time of the first read and each retrace advances it by
 * one field (portFastClockFrame), so timed waits (the logo and US health
 * notice, message pacing, WAIT_MS) take the same number of frames as in real
 * time; each read also adds 1 us so a busy-wait on the clock still ends. */
static int sFast = -1;
static OSTime sBase, sVirtual;
/* portPause: the real clock stands still while paused, and the real time
 * spent paused is left out of it afterwards */
static s64 sPausedNs, sPauseStartNs;
static BOOL sClockPaused;

/* the real clock; $TTYD_CLOCK_DAYS=N moves it N days on (os_time_aurora.c) */
static OSTime realNow(void) {
    static int sRead;
    static OSTime sShift;
    if (!sRead) {
        const char* days = getenv("TTYD_CLOCK_DAYS");
        sRead = 1;
        sShift = days != NULL ? (OSTime)atoi(days) * 86400 * (OSTime)OS_TIMER_CLOCK : 0;
    }
    if (!sInitialized) {
        OSInit();
    }
    return sTimeBase + sShift +
           nsToTicks((sClockPaused ? sPauseStartNs : monotonicNs()) - sMonoBaseNs - sPausedNs);
}

static BOOL fast(void) {
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
        sBase = realNow();
    }
    return sFast;
}

int portLockstep(void) {
    return fast();
}

void portLockstepBegin(s64 base) {
    sFast = 1;
    sBase = base;
    sVirtual = 0;
}

s64 portLockstepClock(void) {
    return fast() ? sBase + sVirtual : realNow();
}

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

OSTime OSGetTime(void) {
    if (fast()) {
        sVirtual += OSMicrosecondsToTicks(1);
        return sBase + sVirtual;
    }
    return realNow();
}

/* OSGetTime without moving the virtual clock (draws, snapshots: port/platform.h) */
s64 portGameClockPeek(void) {
    return fast() ? sBase + sVirtual : realNow();
}

/* days since 1970-01-01 (UTC) by the lockstep clock */
unsigned portLockstepDay(void) {
    const OSTime t = (OSTime)portLockstepClock();
    return t > 0 ? (unsigned)(t / ((OSTime)OS_TIMER_CLOCK * 86400)) + 10957u : 10957u;
}

void portClockPause(void) {
    sPauseStartNs = monotonicNs();
    sClockPaused = TRUE;
}

void portClockResume(void) {
    sPausedNs += monotonicNs() - sPauseStartNs;
    sClockPaused = FALSE;
}

OSTick OSGetTick(void) {
    return (OSTick)OSGetTime();
}

static int isLeap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

void OSTicksToCalendarTime(OSTime ticks, OSCalendarTime* td) {
    static const int sDays[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    };
    s64 secs = ticks / OS_TIMER_CLOCK;
    s64 rem = ticks % OS_TIMER_CLOCK;
    s64 days;
    int year = 2000;

    if (rem < 0) {
        rem += OS_TIMER_CLOCK;
        secs--;
    }
    td->usec = (int)(rem * 8 / (OS_TIMER_CLOCK / 125000) % 1000);
    td->msec = (int)(rem / (OS_TIMER_CLOCK / 1000));

    days = secs / 86400;
    secs %= 86400;
    if (secs < 0) {
        secs += 86400;
        days--;
    }
    td->hour = (int)(secs / 3600);
    td->min = (int)(secs / 60 % 60);
    td->sec = (int)(secs % 60);
    td->wday = (int)((days + 6) % 7); /* 2000-01-01 was a Saturday */
    if (td->wday < 0) {
        td->wday += 7;
    }
    while (days >= (isLeap(year) ? 366 : 365)) {
        days -= isLeap(year) ? 366 : 365;
        year++;
    }
    while (days < 0) {
        year--;
        days += isLeap(year) ? 366 : 365;
    }
    td->year = year;
    td->yday = (int)days;
    for (td->mon = 0; days >= sDays[isLeap(year)][td->mon]; td->mon++) {
        days -= sDays[isLeap(year)][td->mon];
    }
    td->mday = (int)days + 1;
}

/* Single-threaded game loop on PC: interrupts are a no-op. */
BOOL OSDisableInterrupts(void) { return TRUE; }
BOOL OSEnableInterrupts(void) { return TRUE; }
BOOL OSRestoreInterrupts(BOOL level) { return level; }

/* No incoherent caches or DMA engines on PC. */
void DCInvalidateRange(void* addr, u32 nBytes) {}
void DCFlushRange(void* addr, u32 nBytes) {}
void DCStoreRange(void* addr, u32 nBytes) {}
void DCFlushRangeNoSync(void* addr, u32 nBytes) {}
void DCStoreRangeNoSync(void* addr, u32 nBytes) {}
void ICInvalidateRange(void* addr, u32 nBytes) {}
void LCEnable(void) {}
void LCDisable(void) {}

void OSVReport(const char* msg, va_list list) {
    vfprintf(stderr, msg, list);
}

void OSReport(const char* msg, ...) {
    va_list list;
    va_start(list, msg);
    OSVReport(msg, list);
    va_end(list);
}

void OSPanic(const char* file, int line, const char* msg, ...) {
    va_list list;
    fprintf(stderr, "OSPanic %s:%d: ", file, line);
    va_start(list, msg);
    vfprintf(stderr, msg, list);
    va_end(list);
    fputc('\n', stderr);
    abort();
}

void* OSPhysicalToCached(u32 paddr) {
    OSPanic(__FILE__, __LINE__, "OSPhysicalToCached(0x%08x): no physical memory on PC", paddr);
    return NULL;
}
