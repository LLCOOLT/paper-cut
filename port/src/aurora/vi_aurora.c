/* VI functions Aurora leaves to the application. VIWaitForRetrace is the
 * frame boundary: the game calls it once per frame after GXCopyDisp. */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/dvd.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif
#include "port/aurora_pc.h"
#include "port/os_pc.h"
#include "port/dtk_pc.h"
#include "port/platform.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif

void salPcAuroraPump(double seconds); /* hw_pc.c: MusyX null sink without a device */

static u32 sRetraceCount;
static VIRetraceCallback sPreCallback;
static VIRetraceCallback sPostCallback;

/* 60 Hz pacing, as headless (vi_pc.c): the game counts time in frames, and
 * Aurora presents without waiting for vsync (mailbox). $TTYD_FAST: none. */
#define FIELD_TICKS (OS_TIMER_CLOCK / 60)
static OSTime sNextRetrace;

static void paceFrame(void) {
    OSTime now = OSGetTime();
    if (sNextRetrace == 0) {
        sNextRetrace = now;
    }
    if (now < sNextRetrace && getenv("TTYD_FAST") == NULL) {
        s64 us = OSTicksToMicroseconds(sNextRetrace - now);
#ifdef _WIN32
        Sleep((DWORD)(us / 1000));
#else
        struct timespec ts = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000};
        nanosleep(&ts, NULL);
#endif
    }
    /* After a stall, resynchronize instead of running frames back to back. */
    if (OSGetTime() - sNextRetrace > FIELD_TICKS * 4) {
        sNextRetrace = OSGetTime();
    }
    sNextRetrace += FIELD_TICKS;
}

/* Aurora reports a change of its paused state (window minimized or hidden,
 * app in the background, or unfocused with $TTYD_PAUSE_UNFOCUSED): the game
 * waits here, paused (portPause), until it can draw again. */
void portAuroraBeginFrame(void) {
    static BOOL sWindowPaused;
    for (;;) {
        const AuroraEvent* event = aurora_update();
        for (; event != NULL && event->type != AURORA_NONE; event++) {
            if (event->type == AURORA_EXIT) {
                aurora_shutdown();
                exit(0);
            }
#ifdef PORT_TOUCH_OVERLAY
            if (event->type == AURORA_SDL_EVENT) {
                portTouchEvent(&event->sdl);
            }
#endif
            if (event->type == AURORA_PAUSED && !sWindowPaused) {
                sWindowPaused = TRUE;
#ifdef PORT_TOUCH_OVERLAY
                portTouchReleaseAll();
#endif
                portPause();
            } else if (event->type == AURORA_UNPAUSED && sWindowPaused) {
                sWindowPaused = FALSE;
                portResume();
            }
        }
        if (!sWindowPaused && aurora_begin_frame()) {
            return;
        }
    }
}

void VIWaitForRetrace(void) {
    static int sFast = -1;
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
    }
    /* $TTYD_FAST: the disc reads queued this frame complete before the next
     * one (Aurora runs them on a worker thread, in wall time), so a run takes
     * the same frames however busy the machine is, as headless. */
    if (sFast) {
        aurora_dvd_wait_idle();
    }
#ifdef PORT_TOUCH_OVERLAY
    portTouchDraw(); /* on-screen controls, over the game's frame */
#endif
    aurora_end_frame();
    if (sPreCallback != NULL) {
        sPreCallback(sRetraceCount);
    }
    sRetraceCount++;
    /* Game threads (DVD manager, disc check) run while the GameCube's main
     * thread would be blocked here. */
    portThreadsRunSlice();
    portDTKPump();
    portARQPoll();
    portShotFrame();
    portFastClockFrame();
    salPcAuroraPump(1.0 / 60.0);
    portTraceFrame(sRetraceCount); /* $TTYD_TRACE / $TTYD_WARP, as headless */
    paceFrame();
    if (sPostCallback != NULL) {
        sPostCallback(sRetraceCount);
    }
    portAuroraBeginFrame();
}

void VISetNextFrameBuffer(void* fb) {}
void VISetBlack(BOOL black) {}

u32 VIGetRetraceCount(void) {
    return sRetraceCount;
}

u32 VIGetNextField(void) {
    return 1;
}

u32 VIGetDTVStatus(void) {
    return 1;
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
