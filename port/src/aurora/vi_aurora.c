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
#include "port/handheld.h"
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
#include "port/stereo.h"
#include "port/ui_shot.h"
#include "port/players.h"
#include "port/launcher_back.h"
#include "port/playmodes.h"
#include "port/settings.h"
#include "port/statesync.h"
#include <stdio.h>
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif
#ifdef __EMSCRIPTEN__
#include "port/web.h"
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
static OSTime sNextRetrace; /* when the next game frame is due on screen */
static OSTime sSlept;       /* game time spent waiting here since the last frame shown (catch-up's measure) */

static void waitUntil(OSTime when) {
    static int sFast = -1;
    OSTime now = OSGetTime();
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
    }
#ifdef __EMSCRIPTEN__
    /* the browser: no sleeping here; the frame's start waits for the screen (web_pc.c) */
    portWebPaceTo(!sFast ? when : 0);
    (void)now;
    return;
#endif
    if (!sFast && now < when) {
        SDL_DelayPrecise((Uint64)OSTicksToMicroseconds(when - now) * 1000);
        sSlept += OSGetTime() - now;
    }
}

/* --- Keep game speed when the GPU falls behind ("keep_game_speed", port/include/port/toggles.h) ---
 * The game's logic is a step per field (60 a second), and each is shown with the in-between frames
 * of a higher frame rate after it. When drawing them takes longer than a field (ray-traced lighting at
 * 120, a slow GPU), every field came later and the whole game played slower (90 shown of 120: 75 %).
 * Now, while the frames shown cost more than the fields give them (sOverloaded: the work of one,
 * averaged, times the frames a field asks for), a game that is behind its clock first leaves out the
 * in-between frames that come too late, then runs its next game frames undrawn (battery_aurora.c's
 * undrawn frames, portPacingBehind) until it is on time again, at most CATCHUP_MAX in a row so a long
 * stall doesn't fast-forward (paceFrame's resynchronizing after 4 fields does the rest). Smoothness
 * drops; the game keeps its real speed, and the audio (on its own clock) stays with it. A small drift
 * (a 59.94 Hz display's vsync) is no overload: such a game still runs as before.
 * Never with $TTYD_FAST, in lockstep (replays, shared online battles), a ghost race, console-accurate
 * mode, fast-forward or frame advance: there every frame is drawn, exactly as before. */
#define CATCHUP_MAX 2 /* undrawn game frames in a row at most (33 ms at 60) */
static OSTime sLastShown;    /* game time a frame was last shown (0: start again) */
static OSTime sWorkPerShown; /* the work for each frame shown, averaged (game ticks) */
static int sOverloaded;
static unsigned sCaughtUp, sDropped; /* since the last [pacing] line */
static BOOL sWindowPaused;
static BOOL sDrawAgainLog; /* the first frame begun after a pause, logged (a phone back from a picker or home) */

static int catchUpAllowed(void) {
    static int* sSetting;
    static int sFast = -1;
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("keep_game_speed", 1, 0, 1);
        sFast = getenv("TTYD_FAST") != NULL;
    }
    return *sSetting && !sFast && !portLockstep() && !portReplayActive() && !portGhostRacing() &&
           !portConsoleAccurate() && !portSpeedActive() && !portFrameAdvanceHeld() && !sWindowPaused;
}

/* a frame was just shown: its work (since the one before, less the waits) into the average, and
 * whether the frames a field asks for still fit in it (on above 105 %, off again below 100 %) */
static void catchUpMeasure(void) {
    const OSTime now = OSGetTime();
    const OSTime field = FIELD_TICKS;
    if (portSpeedActive()) {
        /* fast-forward (and the intro's): the game clock runs faster than the drawing, no measure;
         * after it, measured again from nothing */
        sWorkPerShown = 0;
        sOverloaded = 0;
        sLastShown = 0;
        sSlept = 0;
        return;
    }
    if (sLastShown != 0 && now > sLastShown) {
        OSTime work = now - sLastShown - sSlept;
        OSTime wanted;
        int frames = portInterpFrames();
        if (work < 0) {
            work = 0;
        }
        if (work > field * 4) {
            /* a stall (a map's load, a hitch): no measure of the drawing; paceFrame starts the clock
             * again after one rather than catching up */
            sLastShown = now;
            sSlept = 0;
            return;
        }
        /* (from 0: a sustained load is seen within half a second, a few slow first frames aren't) */
        sWorkPerShown += (work - sWorkPerShown) / 16;
        if (portBatterySaving()) {
            frames = 1; /* (no in-between frames then) */
        }
        wanted = sWorkPerShown * frames;
        if (wanted > field * 21 / 20) {
            sOverloaded = 1;
        } else if (wanted < field) {
            sOverloaded = 0;
        }
    }
    sLastShown = now;
    sSlept = 0;
}

/* a hold (frame advance, a lost controller): the time in it isn't work */
static void catchUpForget(void) {
    sLastShown = 0;
    sSlept = 0;
}

/* battery_aurora.c, at a game frame's start: 1 when it is due on screen already (a field or more
 * behind) and may go undrawn; sinceDrawn: the game frames since the last one drawn (1: none undrawn) */
int portPacingBehind(int sinceDrawn) {
    const int cap = portStateSyncLinked() ? 1 : CATCHUP_MAX; /* online: gentler */
    if (!sOverloaded || sinceDrawn > cap || !catchUpAllowed()) {
        return 0;
    }
    return OSGetTime() >= sNextRetrace;
}

/* it went undrawn (battery_aurora.c: not when a new draw pass made it drawn) */
void portPacingCaughtUp(void) {
    sCaughtUp++;
}

/* in-between frame k of frames: left out when it comes a slot or more late */
static int catchUpDropInBetween(int k, int frames) {
    const OSTime slot = sNextRetrace - FIELD_TICKS + FIELD_TICKS * k / frames;
    if (!sOverloaded || !catchUpAllowed() || OSGetTime() - slot < FIELD_TICKS / frames) {
        return 0;
    }
    sDropped++;
    return 1;
}

/* "[pacing] caught up N frames", at most every 5 seconds (and a line a second of the game's own rate,
 * with $TTYD_FPS_LOG: game frames run, drawn and in-between frames shown, for the tests) */
static void pacingLog(int drawn, int inBetween) {
    static Uint64 sToldAt, sFrom;
    static unsigned sGame, sDrawn, sBetween;
    static int sFpsLog = -1;
    const Uint64 now = SDL_GetTicksNS();
    if ((sCaughtUp != 0 || sDropped != 0) && (sToldAt == 0 || now - sToldAt >= 5000000000ull)) {
        printf("[pacing] caught up %u frames (%u in-between frames left out); %.1f ms of work a frame shown, "
               "%d a field, at retrace %u\n",
               sCaughtUp, sDropped, (double)OSTicksToMicroseconds(sWorkPerShown) / 1000.0, portInterpFrames(),
               (unsigned)sRetraceCount);
        fflush(stdout);
        sToldAt = now;
        sCaughtUp = sDropped = 0;
    }
    if (sFpsLog < 0) {
        const char* v = getenv("TTYD_FPS_LOG");
        sFpsLog = v != NULL && v[0] != '\0' && v[0] != '0';
        sFrom = now;
    }
    if (!sFpsLog) {
        return;
    }
    sGame++;
    sDrawn += drawn;
    sBetween += inBetween;
    {
        /* and every 10 seconds, the game frames run in them: 60 a second when it keeps its speed */
        static Uint64 sWinFrom;
        static unsigned sWinGame, sWinShown;
        if (sWinFrom == 0) {
            sWinFrom = now;
        }
        sWinGame++;
        sWinShown += drawn + inBetween;
        if (now - sWinFrom >= 10000000000ull) {
            const double s = (double)(now - sWinFrom) / 1e9;
            printf("[pacing] game %.1f frames/s over 10 s, %.1f shown/s\n", sWinGame / s, sWinShown / s);
            fflush(stdout);
            sWinFrom = now;
            sWinGame = sWinShown = 0;
        }
    }
    if (now - sFrom >= 1000000000ull) {
        const double s = (double)(now - sFrom) / 1e9;
        printf("[pacing] game %.1f frames/s, drawn %.1f/s, in-between %.1f/s, retrace %u%s\n", sGame / s,
               sDrawn / s, sBetween / s, (unsigned)sRetraceCount, sOverloaded ? ", behind" : "");
        fflush(stdout);
        sFrom = now;
        sGame = sDrawn = sBetween = 0;
    }
}

/* $TTYD_TEST_RENDER_DELAY_MS (tests): a slow GPU, that long more for every frame shown */
static void testRenderDelay(void) {
    static int sMs = -1;
    if (sMs < 0) {
        const char* v = getenv("TTYD_TEST_RENDER_DELAY_MS");
        sMs = v != NULL ? atoi(v) : 0;
        if (sMs > 0) {
            printf("[pacing] test: %d ms more for every frame shown\n", sMs);
            fflush(stdout);
        }
    }
    if (sMs > 0) {
        SDL_DelayPrecise((Uint64)sMs * 1000000ull);
    }
}

/* Lockstep without $TTYD_FAST (shared online battles, replays): the game clock is
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
    portStereoCompose(); /* stereoscopic 3D: the two eyes into the frame, under what follows (stereo_aurora.c) */
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
    testRenderDelay();
    aurora_end_frame();
    catchUpMeasure();
    portUiShotFrame(); /* tests: captures with the ImGui layer (port/ui_shot.h) */
}

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <stdio.h>
#include <string.h>
void portOnlineInviteEvent(const void* sdlEvent); /* invite.cpp */

/* Aurora reports a change of its paused state (window minimized or hidden,
 * app in the background, or unfocused with $TTYD_PAUSE_UNFOCUSED): the game
 * waits here, paused (portPause), until it can draw again (sWindowPaused, above). */

/* Aurora's events since the last look: SDL's to everyone, the window's pause */
static void handleEvents(const AuroraEvent* event) {
    for (; event != NULL && event->type != AURORA_NONE; event++) {
        if (event->type == AURORA_EXIT) {
            portCrashQuitStarted(); /* ending on purpose: a fault from here on is no crash (#27) */
            portResumeFlush(); /* quick resume: where the player was */
            portCompanionShutdown();
            aurora_shutdown();
            exit(0);
        }
        if (event->type == AURORA_SDL_EVENT && event->sdl.type == SDL_EVENT_TERMINATING) {
            portCrashQuitStarted(); /* the system ending the app (a phone's): no crash report from here */
        }
        if (event->type == AURORA_SDL_EVENT && portCompanionEvent(&event->sdl)) {
            continue; /* the second screen's window's (its clicks are not the game's) */
        }
        if (event->type == AURORA_SDL_EVENT) {
            portOnlineInviteEvent(&event->sdl); /* an invite link given to the running game */
            portSettingsEvent(&event->sdl);
            portHandheldEvent(&event->sdl); /* a handheld's Select + Start and power key (handheld_aurora.c) */
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
            sDrawAgainLog = TRUE;
            portResume();
            catchUpForget();
        }
    }
}

/* $TTYD_TEST_F1_AT=<frame>[:<ms>] (tests): the full settings page opened at that retrace, as F1
 * does, once; with ms, closed again that much later (input scripts drive the pad only) */
void portSettingsToggle(void); /* port/settings/settings_ui.cpp */
static Uint64 sTestF1CloseAt;
static void testF1(void) {
    static int sDone;
    static const char* at = (const char*)1; /* (read once: this runs every frame) */
    if (at == (const char*)1) {
        at = getenv("TTYD_TEST_F1_AT");
    }
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
#ifdef __EMSCRIPTEN__
        portWebYield(); /* the last frame to the browser, and its input in */
#endif
        testF1();
        handleEvents(aurora_update());
        if (!sWindowPaused && aurora_begin_frame()) {
            static int sBegun;
            if (sBegun < 2 && ++sBegun == 2) {
                /* a frame shown: a restart's old window can go now (restart.cpp) */
                void portRestartHandoffReady(void);
                portRestartHandoffReady();
            }
            if (sDrawAgainLog) {
                sDrawAgainLog = FALSE;
                printf("[aurora] drawing again after the pause\n");
                fflush(stdout);
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
    int frames, k, shown;
    /* $TTYD_FAST and lockstep (replays, shared online battles): the disc reads queued this frame complete
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
    portCrashTestFrame();          /* tests: $TTYD_CRASH_TEST, a crash on purpose (crash_pc.c) */
    portAutosaveFrame();           /* autosave at room changes (autosave_aurora.c) */
    portFingerprintFrame();        /* tests: the fingerprint file (fingerprint_pc.c) */
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
    shown = 0;
    if (frames > 1) {
        s64 fastClock = portFastClockGet();
        /* nothing to blend (a map or battle change): the shown frame stays */
        for (k = 1; k < frames && portInterpReady(); k++) {
            if (catchUpDropInBetween(k, frames)) {
                continue; /* behind: this one too late to show (keep game speed) */
            }
            shown++;
            portAuroraBeginFrame();
            portInterpDraw((f32)k / (f32)frames);
            portStereoCompose(); /* (before the capture) */
            portShotInbetween(sRetraceCount - 1, k);
            portDisplayWorkDone();
            waitUntil(sNextRetrace - FIELD_TICKS + FIELD_TICKS * k / frames);
            endFrame();
        }
        portInterpFrameDone();
        portFastClockSet(fastClock);
    } else if (!drawn && portInterpFrames() > 1) {
        /* an undrawn frame (caught up, a scene skipped): the snapshot moves on with the game, so the
         * next drawn frame's in-between frames blend from the state it shows, never from an older one */
        portInterpFrameDone();
    }
    pacingLog(drawn, shown);
    portSaveStateFrame(); /* between frames: a save state asked for */
    portRewindFrame();    /* rewind's state, or a step back (rewind_aurora.c) */
    if (!drawn) {
        handleEvents(aurora_update()); /* (input as every frame; the frame begun stays the next one's) */
        return;
    }
    /* Back to the launcher's steps (launcher_back_aurora.c): between frames, the one shown ended and the
     * next not begun, as the launcher at start runs (a phone's runs here, with frames of its own) */
    portLauncherBackFrame();
    if (portFrameAdvanceWait()) {
        /* frame advance: the game held here, its last frame drawn again (and
         * the settings page over it) until a step or the end of the hold */
        portPause();
        do {
            portAuroraBeginFrame();
            portInterpRedraw();
            portStereoCompose(); /* (before the capture) */
            portShotHeld(); /* a picture taken now (F12, photo mode) is of this one */
            endFrame();
            portShotFrame(); /* and written when its readback comes */
            portSaveStateFrame();
        } while (portFrameAdvanceWait());
        portResume();
        catchUpForget();
    }
    if (portCoopPadLost()) {
        /* player 1's controller disconnected (second_player_pc.c): the game held here as frame advance
         * holds it, this frame (with its note, player_menu_pc.c) drawn again until a Start on any controller */
        portPause();
        while (portCoopPadLostWait()) {
            portAuroraBeginFrame();
            portInterpRedraw();
            portStereoCompose();
            portShotHeld();
            endFrame();
            portShotFrame();
            SDL_Delay(8); /* (no game frame to pace it) */
        }
        portResume();
        catchUpForget();
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
