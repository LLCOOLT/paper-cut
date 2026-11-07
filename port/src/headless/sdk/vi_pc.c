/* Video interface: frame pacing at 60 Hz and presentation. */
#include <dolphin.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "port/os_pc.h"
#include "port/platform.h"
#include "port/dtk_pc.h"

void salPcHeadlessPump(double seconds);

#define FIELD_TICKS (OS_TIMER_CLOCK / portFieldRate()) /* 60, or 50: PAL in 50 Hz */

static u32 sRetraceCount;
static OSTime sNextRetrace;
static BOOL sBlack = TRUE;
static VIRetraceCallback sPreCallback;
static VIRetraceCallback sPostCallback;

static void sleepTicks(OSTime ticks) {
    s64 us = OSTicksToMicroseconds(ticks);
#ifdef _WIN32
    Sleep((DWORD)(us / 1000));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(us / 1000000);
    ts.tv_nsec = (long)(us % 1000000) * 1000;
    nanosleep(&ts, NULL);
#endif
}

void VIInit(void) {
    sNextRetrace = OSGetTime() + FIELD_TICKS;
}

void VIWaitForRetrace(void) {
    OSTime now;
    /* Other game threads run while the main thread would be blocked. */
    portThreadsRunSlice();
    portDTKPump();
    portARQPoll();
    /* MusyX: render the audio of one video frame (null sink). */
    salPcHeadlessPump(1.0 / portFieldRate());
    if (!portPlatformPoll()) {
        exit(0);
    }
    portTraceFrame(sRetraceCount);
    portFastClockFrame();
    now = OSGetTime();
    /* $TTYD_FAST: run frames back to back (bring-up and tests). */
    if (now < sNextRetrace && getenv("TTYD_FAST") == NULL) {
        sleepTicks(sNextRetrace - now);
    }
    /* After a stall, resynchronize instead of running frames back to back. */
    if (OSGetTime() - sNextRetrace > FIELD_TICKS * 4) {
        sNextRetrace = OSGetTime();
    }
    sNextRetrace += FIELD_TICKS;
    if (sPreCallback != NULL) {
        sPreCallback(sRetraceCount);
    }
    sRetraceCount++;
    if (sPostCallback != NULL) {
        sPostCallback(sRetraceCount);
    }
}

void VIConfigure(const GXRenderModeObj* rm) {}
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height) {}

void VIFlush(void) {
    if (!sBlack) {
        portPlatformPresent();
    }
}

void VISetNextFrameBuffer(void* fb) {}

void VISetBlack(BOOL black) {
    sBlack = black;
}

u32 VIGetRetraceCount(void) {
    return sRetraceCount;
}

/* the retrace count a save state put back had (the console's own, which the
 * game reads each frame as gp->retraceCount: savestate_aurora.c) */
void portVISetRetraceCount(u32 count) {
    sRetraceCount = count;
}

u32 VIGetNextField(void) {
    return 1;
}

u32 VIGetTvFormat(void) {
    return VI_NTSC;
}

u32 VIGetDTVStatus(void) {
    return 1; /* progressive-capable, so the game offers 480p */
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = sPreCallback;
    sPreCallback = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback old = sPostCallback;
    sPostCallback = cb;
    return old;
}
