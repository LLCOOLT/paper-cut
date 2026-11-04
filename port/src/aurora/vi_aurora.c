/* VI functions Aurora leaves to the application. VIWaitForRetrace is the
 * frame boundary: the game calls it once per frame after GXCopyDisp. */
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/dvd.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <SDL3/SDL_timer.h>
#include <stdlib.h>
#include "port/aurora_pc.h"
#include "port/resume.h"
#include "port/os_pc.h"
#include "port/dtk_pc.h"
#include "port/platform.h"
#include "port/speed.h"
#include "port/layout.h"
#include "port/savestate.h"
#include "port/mod_host.h"
#include "port/menu_tap.h"
#include "port/practice.h"
#include "port/display.h"
#include "port/online.h"
#include "port/companion.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif

void salPcAuroraPump(double seconds); /* hw_pc.c: MusyX null sink without a device */
void portShotInbetween(u32 frame, int k); /* shot_aurora.c */

static u32 sRetraceCount;
static VIRetraceCallback sPreCallback;
static VIRetraceCallback sPostCallback;

/* 60 Hz pacing, as headless (vi_pc.c): the game counts time in frames, and
 * Aurora presents without waiting for vsync (mailbox). Each frame is
 * presented at its field's start, and the in-between frames of a higher
 * $TTYD_FRAMERATE (interp_aurora.c) evenly through the field. $TTYD_FAST:
 * no waits. */
#define FIELD_TICKS (OS_TIMER_CLOCK / portFieldRate()) /* 60, or 50: PAL in 50 Hz */
static OSTime sNextRetrace;

static void waitUntil(OSTime when) {
    static int sFast = -1;
    OSTime now = OSGetTime();
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
    }
    if (!sFast && now < when) {
        SDL_DelayPrecise((Uint64)OSTicksToMicroseconds(when - now) * 1000);
    }
}

/* Waits for this field's start; the next one is FIELD_TICKS later. */
static void paceFrame(void) {
    if (sNextRetrace == 0) {
        sNextRetrace = OSGetTime();
    }
    waitUntil(sNextRetrace);
    /* After a stall, resynchronize instead of running frames back to back. */
    if (OSGetTime() - sNextRetrace > FIELD_TICKS * 4) {
        sNextRetrace = OSGetTime();
    }
    sNextRetrace += FIELD_TICKS;
}

static void endFrame(void) {
    portDisplayFrame(); /* the display options, and the frame's timing */
    portLayoutFrame(); /* the picture's place (portrait: at the top) */
    portModsDraw();    /* code mods' drawing, over the picture */
#ifdef PORT_TOUCH_OVERLAY
    portTouchDraw(); /* on-screen controls, over the game's frame */
#endif
    portSettingsDraw(); /* the settings page, when open */
    portPerfOverlayDraw(); /* the frame-time graph, when on */
    portCompanionDraw();   /* the second screen's window, when on (its own renderer) */
    aurora_end_frame();
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
                portResumeFlush(); /* quick resume: where the player was */
                portCompanionShutdown();
                aurora_shutdown();
                exit(0);
            }
            if (event->type == AURORA_SDL_EVENT && portCompanionEvent(&event->sdl)) {
                continue; /* the second screen's window's (its clicks are not the game's) */
            }
            if (event->type == AURORA_SDL_EVENT) {
                portSettingsEvent(&event->sdl);
                portMenuMouseEvent(&event->sdl); /* the mouse in the game's lists */
#ifdef PORT_TOUCH_OVERLAY
                portTouchEvent(&event->sdl);
#endif
            }
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
            portDisplayFrameStart();
            return;
        }
    }
}

void VIWaitForRetrace(void) {
    int frames, k;
    /* $TTYD_FAST and online co-op: the disc reads queued this frame complete
     * before the next one (Aurora runs them on a worker thread, in wall time),
     * so a run takes the same frames however busy the machine is, as headless,
     * and two computers' games stay in step. */
    if (portLockstep()) {
        aurora_dvd_wait_idle();
    }
    portDisplayWorkDone();
    paceFrame();
    endFrame();
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
    salPcAuroraPump(1.0 / portFieldRate());
    portTraceFrame(sRetraceCount); /* $TTYD_TRACE / $TTYD_WARP, as headless */
    portWarpFrame();               /* the settings page's save warps */
    portResumeFrame();             /* quick resume's save, made while playing */
    portAutosaveFrame();           /* autosave at room changes (autosave_aurora.c) */
    portOnlineFrame();             /* online co-op: the fingerprint (online_pc.c) */
    portCompanionFrame();          /* the second screen's snapshot (companion_pc.c) */
    if (sPostCallback != NULL) {
        sPostCallback(sRetraceCount);
    }
    /* The game's next frame is now computed (and its render clocks advanced):
     * the frames between the one just shown and that one. */
    frames = portInterpFrames();
    if (portSpeedActive() && frames > 1) {
        /* no in-between frames while fast-forwarding; the snapshot stays
         * current for when it ends */
        portInterpFrameDone();
        frames = 1;
    }
    portSpeedFrame();
    if (frames > 1) {
        s64 fastClock = portFastClockGet();
        /* nothing to blend (a map or battle change): the shown frame stays */
        for (k = 1; k < frames && portInterpReady(); k++) {
            portAuroraBeginFrame();
            portInterpDraw((f32)k / (f32)frames);
            portShotInbetween(sRetraceCount - 1, k);
            portDisplayWorkDone();
            waitUntil(sNextRetrace - FIELD_TICKS + FIELD_TICKS * k / frames);
            endFrame();
        }
        portInterpFrameDone();
        portFastClockSet(fastClock);
    }
    portSaveStateFrame(); /* between frames: a save state asked for */
    if (portFrameAdvanceWait()) {
        /* frame advance: the game held here, its last frame drawn again (and
         * the settings page over it) until a step or the end of the hold */
        portPause();
        do {
            portAuroraBeginFrame();
            portInterpRedraw();
            endFrame();
            portSaveStateFrame();
        } while (portFrameAdvanceWait());
        portResume();
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
