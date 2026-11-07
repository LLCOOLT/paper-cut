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

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <stdio.h>
#include <string.h>
#include "port/tr.h"
void portOnlineInviteEvent(const void* sdlEvent); /* invite.cpp */

/* Online co-op's wait (online_net_pc.c: for the other player's input, reconnecting,
 * a watcher waiting for the players), from inside the game's frame: the window's
 * title says it, and once it has lasted HOLD_NOTICE_MS a notice over the picture
 * (online_hold.cpp), for a player in full screen. Nothing of the game's runs
 * here: no aurora_update (input, its events: pumped only, as before), no game
 * threads or disc work (retrace's, between frames: two games went out of step
 * after a reconnect when they ran in here), no drawing of the game's. The game's
 * frame is already drawn when it reads the pads (marioStDisp, then marioStMain),
 * so the notice goes over it and the frame is shown then (endFrame, once, as at
 * its retrace); that retrace then shows nothing new (retrace(0), as battery
 * saver's undrawn frames) and the next frame draws into the one begun when the
 * wait ends (portOnlineHoldEnd). A second notice in the same wait (waiting, then
 * reconnecting) can't keep the picture (the game's draw list is gone by then):
 * it is shown on a dark screen. The game says "Reconnected" once it goes on
 * (online_pc.c). NULL: the wait is over (each frame, online_pc.c). Draw-only:
 * the game's state and random numbers are never touched. */
void portOnlineHoldOverlay(const char* utf8, int backdrop); /* online_hold.cpp */
static void endFrame(void);
#define HOLD_NOTICE_MS 500        /* the notice after the wait's first word (already a second in, waiting) */
#define HOLD_NOTICE_WATCH_MS 1500 /* a watcher's: its batches come every half second */
static int sHeld;
static Uint64 sHeldSince;
static int sHoldShown;   /* this frame's picture shown, with the notice (the Aurora frame ended) */
static int sHoldUndrawn; /* the wait is over: this frame's retrace shows nothing new */
static char sHoldSaid[200];  /* the notice shown */
static char sTitleSaid[200]; /* the window's title */

/* the wait's words in the menu language: whom it waits for, by name */
static void holdText(const char* english, char* out, size_t size) {
    const PortOnlineHello* peer = portOnlinePeer();
    const char* name = peer != NULL ? peer->name : "";
    if (strcmp(english, "Waiting for the other player...") == 0 && portOnlinePeerAway() && name[0] != '\0') {
        snprintf(out, size, portTr("%s minimized the game."), name);
    } else if (strcmp(english, "Waiting for the other player...") == 0 && name[0] != '\0') {
        snprintf(out, size, portTr("Waiting for %s..."), name);
    } else {
        snprintf(out, size, "%s", portTr(english));
    }
}

/* the window can show a frame now (minimized: Aurora would wait in its events) */
static int holdCanShow(void) {
    SDL_Window* w = aurora_get_window();
    if (w == NULL || (SDL_GetWindowFlags(w) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0) {
        return 0;
    }
    return getenv("TTYD_ONLINE_NO_HOLD_NOTICE") == NULL; /* (tests: as before) */
}

/* $TTYD_HOLD_SHOT (tests): each notice shown, its picture (the player's screenshot, to the user
 * folder's Photos) and the notice's layer (launcher_shot.cpp: Dear ImGui drawn again on the CPU,
 * as Aurora's capture leaves it out) to $TTYD_SHOT_DIR/hold_N.png, for the test to put together */
void portShotRequestUser(void);         /* shot_aurora.c */
void portImguiLayerShot(const char* name); /* launcher_shot.cpp */
static void holdShot(int layer) {
    static int sN;
    char name[32];
    if (getenv("TTYD_HOLD_SHOT") == NULL) {
        return;
    }
    if (!layer) {
        portShotRequestUser();
        return;
    }
    snprintf(name, sizeof(name), "hold_%d", ++sN);
    portImguiLayerShot(name);
}

void portOnlineHoldFrame(const char* english) {
    SDL_Window* w = aurora_get_window();
    char said[200];
    if (english == NULL) {
        if (sHeld && w != NULL) {
            SDL_SetWindowTitle(w, "Paper Cut");
        }
        sHeld = 0;
        return;
    }
    holdText(english, said, sizeof(said));
    if (!sHeld) {
        sHeldSince = SDL_GetTicks();
        sHoldSaid[0] = '\0';
    }
    if ((!sHeld || strcmp(said, sTitleSaid) != 0) && w != NULL) {
        char title[260];
        snprintf(sTitleSaid, sizeof(sTitleSaid), "%s", said);
        snprintf(title, sizeof(title), "Paper Cut: %s", said);
        SDL_SetWindowTitle(w, title);
    }
    sHeld = 1;
    SDL_PumpEvents();
    if (SDL_GetTicks() - sHeldSince >= (portOnlineWatching() ? HOLD_NOTICE_WATCH_MS : HOLD_NOTICE_MS) &&
        strcmp(said, sHoldSaid) != 0 && holdCanShow()) {
        if (!sHoldShown) {
            holdShot(0);
            portOnlineHoldOverlay(said, 0); /* over the game's frame, drawn */
            endFrame();
            holdShot(1);
            sHoldShown = 1;
        } else if (aurora_begin_frame()) {
            portDisplayFrameStart();
            portOnlineHoldOverlay(said, 1);
            endFrame();
            holdShot(1);
        } else {
            return; /* (said again once it can be) */
        }
        snprintf(sHoldSaid, sizeof(sHoldSaid), "%s", said);
    }
}

/* the wait is over (online_pc.c, at once): a frame begun again for the rest of the game's,
 * if this one's was shown; its retrace then shows nothing new (VIWaitForRetrace) */
void portOnlineHoldEnd(void) {
    if (sHoldShown) {
        sHoldShown = 0;
        sHoldUndrawn = 1;
        portAuroraBeginFrame();
    }
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
            portOnlineAway(1); /* online: the other player told (and the connection kept, below) */
        } else if (event->type == AURORA_UNPAUSED && sWindowPaused) {
            sWindowPaused = FALSE;
            portResume();
            portOnlineAway(0);
        }
    }
}

/* $TTYD_TEST_MINIMIZE=<frame>:<ms> (tests): the window minimized at that retrace and shown
 * again that much later, once (online_hold.cpp: without the focus) */
void portTestWindowMinimized(int minimized); /* online_hold.cpp */
static Uint64 sTestRestoreAt;
static void testMinimize(void) {
    static int sDone;
    const char* at = getenv("TTYD_TEST_MINIMIZE");
    if (!sDone && at != NULL && sRetraceCount == (u32)atoi(at)) {
        const char* ms = strchr(at, ':');
        sDone = 1;
        sTestRestoreAt = SDL_GetTicks() + (Uint64)(ms != NULL ? atoi(ms + 1) : 5000);
        printf("[online] test: the window minimized at frame %u\n", (unsigned)sRetraceCount);
        fflush(stdout);
        portTestWindowMinimized(1);
    }
    if (sTestRestoreAt != 0 && SDL_GetTicks() >= sTestRestoreAt) {
        sTestRestoreAt = 0;
        printf("[online] test: the window shown again\n");
        fflush(stdout);
        portTestWindowMinimized(0);
    }
}

/* $TTYD_TEST_F1_AT=<frame>[:<ms>] (tests): the full settings page opened at that retrace, as F1
 * does, once; with ms, closed again that much later (input scripts drive the pad only) */
void portSettingsToggle(void); /* port/settings/settings_ui.cpp */
static Uint64 sTestF1CloseAt;
static void testF1(void) {
    static int sDone;
    const char* at = getenv("TTYD_TEST_F1_AT");
    if (!sDone && at != NULL && sRetraceCount == (u32)atoi(at)) {
        const char* ms = strchr(at, ':');
        sDone = 1;
        sTestF1CloseAt = ms != NULL ? SDL_GetTicks() + (Uint64)atoi(ms + 1) : 0;
        printf("[test] the settings page opened at frame %u\n", (unsigned)sRetraceCount);
        fflush(stdout);
        portSettingsToggle();
    }
    if (sTestF1CloseAt != 0 && SDL_GetTicks() >= sTestF1CloseAt) {
        sTestF1CloseAt = 0;
        printf("[test] the settings page closed at frame %u\n", (unsigned)sRetraceCount);
        fflush(stdout);
        portSettingsToggle();
    }
}

void portAuroraBeginFrame(void) {
    for (;;) {
        testMinimize();
        testF1();
        if (sWindowPaused && portOnlineAwayWanted()) {
            /* minimized online: the game can't draw (so can't play) until it's back, but the
               connection goes on and the other player is told again each second, rather than
               this side going quiet; Aurora's events looked at once one is there (Aurora itself
               would wait for one) */
            portOnlineAwayPump();
            if (!SDL_WaitEventTimeout(NULL, 100)) {
                continue;
            }
        }
        handleEvents(aurora_update());
        if (!sWindowPaused && aurora_begin_frame()) {
            static int sBegun;
            if (sBegun < 2 && ++sBegun == 2) {
                /* a frame shown: a restart's old window can go now (restart.cpp) */
                void portRestartHandoffReady(void);
                portRestartHandoffReady();
            }
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
    /* (online, held: this frame already shown with the wait's notice, portOnlineHoldFrame) */
    const int drawn = !sHoldUndrawn;
    sHoldUndrawn = 0;
    retrace(drawn);
}

void portBatteryFrameUndrawn(void) {
    sHoldUndrawn = 0;
    retrace(0);
}

void VISetNextFrameBuffer(void* fb) {}
void VISetBlack(BOOL black) {}

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
