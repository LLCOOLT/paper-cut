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
#include "port/paper.h"
#include "port/dtk_pc.h"
#include "port/platform.h"
#include "port/speed.h"
#include "port/layout.h"
#include "port/savestate.h"
#include "port/rewind.h"
#include "port/mod_host.h"
#include "port/menu_tap.h"
#include "port/practice.h"
#include "port/display.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/companion_menu.h"
#include "port/companion.h"
#include "port/toggles.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif

void salPcAuroraPump(double seconds); /* hw_pc.c: MusyX null sink without a device */
void portShotInbetween(u32 frame, int k); /* shot_aurora.c */
void portShotHeld(void);                  /* shot_aurora.c */

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
    if (!sFast && now < when && !portOnlineCatchingUp()) { /* (a watcher well behind: unpaced) */
        SDL_DelayPrecise((Uint64)OSTicksToMicroseconds(when - now) * 1000);
    }
}

/* Lockstep without $TTYD_FAST (online co-op, replays): the game clock is
 * virtual, a field per frame, so waiting on it never waits; the frames keep
 * to the real clock instead (portRealTime), a replay's playback faster with
 * fast-forward (portReplaySpeed). */
static void paceReal(void) {
    static int sFast = -1;
    static OSTime sNext;
    OSTime now, field;
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
    }
    if (sFast || !portLockstep()) {
        return;
    }
    field = FIELD_TICKS / portReplaySpeed();
    now = portRealTime();
    if (sNext == 0 || now - sNext > field * 4) {
        sNext = now; /* the start, or after a stall */
    }
    if (now < sNext) {
        SDL_DelayPrecise((Uint64)OSTicksToMicroseconds(sNext - now) * 1000);
    }
    sNext += field;
}

/* Slow speed with $TTYD_FAST's virtual clock (tests; speed_aurora.c): the
 * game time stays one field a frame, the frames kept to the wall clock at
 * that pace instead. */
static void paceSlowFast(void) {
    static Uint64 sNextWall;
    const int percent = portSpeedPercent();
    Uint64 now = SDL_GetTicksNS();
    if (percent >= 100 || !portLockstep()) {
        sNextWall = 0;
        return;
    }
    if (sNextWall != 0 && now < sNextWall) {
        SDL_DelayPrecise(sNextWall - now);
        now = SDL_GetTicksNS();
    }
    if (sNextWall == 0 || (now > sNextWall && now - sNextWall > 100000000ull)) {
        sNextWall = now; /* the first frame, or after a stall: from now */
    }
    sNextWall += 1000000000ull * 100 / ((Uint64)portFieldRate() * (Uint64)percent);
}

/* Waits for this field's start; the next one is FIELD_TICKS later (in game
 * time: slow speed's slower clock makes it later). */
static void paceFrame(void) {
    paceReal();
    paceSlowFast();
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
    portPaperFrame();   /* paper looks: edges, and the frame's paper shadows */
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

#include <SDL3/SDL_video.h>
#include <stdio.h>
#include "port/tr.h"
void portOnlineInviteEvent(const void* sdlEvent); /* invite.cpp */

/* Online co-op's wait (online_net_pc.c: reconnecting, a watcher waiting for the
 * players), from inside the game's frame: the window's title says it, events go
 * on. No frames are drawn here: ending and beginning frames inside the game's
 * own let its disc reads and threads move on outside lockstep, and two games
 * went out of step after a reconnect. The game says "Reconnected" once it goes
 * on (online_pc.c). NULL: the wait is over (each frame, online_pc.c). */
void portOnlineHoldFrame(const char* english) {
    static int sHeld;
    SDL_Window* w = aurora_get_window();
    if (english == NULL) {
        if (sHeld && w != NULL) {
            SDL_SetWindowTitle(w, "Paper Cut");
        }
        sHeld = 0;
        return;
    }
    if (!sHeld && w != NULL) {
        char title[200];
        snprintf(title, sizeof(title), "Paper Cut: %s", portTr(english));
        SDL_SetWindowTitle(w, title);
    }
    sHeld = 1;
    SDL_PumpEvents();
}

/* Aurora reports a change of its paused state (window minimized or hidden,
 * app in the background, or unfocused with $TTYD_PAUSE_UNFOCUSED): the game
 * waits here, paused (portPause), until it can draw again. */
static BOOL sWindowPaused;

/* Aurora's events since the last look: SDL's to everyone, the window's pause */
static void handleEvents(const AuroraEvent* event) {
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
            portOnlineInviteEvent(&event->sdl); /* an invite link given to the running game */
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
}

void portAuroraBeginFrame(void) {
    for (;;) {
        handleEvents(aurora_update());
        if (!sWindowPaused && aurora_begin_frame()) {
            portDisplayFrameStart();
            return;
        }
    }
}

/* A frame's end. drawn: as the GameCube's, the frame shown and the next one
 * begun; 0, battery saver's frame that wasn't drawn (port/toggles.h): paced
 * and everything else done once a frame, but Aurora's frame stays open for
 * the next game frame to draw into, and the screen keeps the last one. */
static void retrace(int drawn) {
    int frames, k;
    /* $TTYD_FAST and online co-op: the disc reads queued this frame complete
     * before the next one (Aurora runs them on a worker thread, in wall time),
     * so a run takes the same frames however busy the machine is, as headless,
     * and two computers' games stay in step. */
    if (portLockstep()) {
        aurora_dvd_wait_idle();
    }
    if (drawn) {
        portDisplayWorkDone();
    }
    paceFrame();
    if (drawn) {
        endFrame();
    }
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
    frames = drawn ? portInterpFrames() : 1;
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
    portRewindFrame();    /* rewind's state, or a step back (rewind_aurora.c) */
    if (!drawn) {
        handleEvents(aurora_update()); /* (input as every frame; the frame begun stays the next one's) */
        return;
    }
    if (portFrameAdvanceWait()) {
        /* frame advance: the game held here, its last frame drawn again (and
         * the settings page over it) until a step or the end of the hold */
        portPause();
        do {
            portAuroraBeginFrame();
            portInterpRedraw();
            portShotHeld(); /* a picture taken now (F12, photo mode) is of this one */
            endFrame();
            portShotFrame(); /* and written when its readback comes */
            portSaveStateFrame();
        } while (portFrameAdvanceWait());
        portResume();
    }
    portAuroraBeginFrame();
    portCompanionMenuFrame(); /* the second screen's Start menu, drawn before the game's frame (companion_menu_aurora.c) */
}

void VIWaitForRetrace(void) {
    retrace(1);
}

void portBatteryFrameUndrawn(void) {
    retrace(0);
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
