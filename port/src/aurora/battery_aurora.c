/* Battery saver (port/include/port/toggles.h): on battery (or always, as the
 * setting says), three things that cost the most power go:
 *
 *  - every other frame isn't drawn: the game still runs all 60 of its frames
 *    a second, but on every second one main.c runs the frame's logic, the
 *    cameras' matrices and the draw list's reset without drawing anything
 *    (dispdrv.c dispDraw returns at once), and vi_aurora.c paces it and does
 *    the port's own work for it without ending the frame on screen; the
 *    picture stays the last one shown, so 30 a second reach the screen. A
 *    draw-time step that assumes a frame each time (a paper character's
 *    turn, animdrv.c) takes the frames it missed (portDrawFrames).
 *  - the internal resolution is halved (the window's own size counts as its
 *    multiple of 480 lines);
 *  - no in-between frames (the Frame rate setting as 60).
 *
 * The skipped draws change what the drawing code keeps (the game's font
 * takes random numbers per letter drawn), so online play, replays (recording
 * or playing) and $TTYD_FAST runs always draw every frame; so do frame
 * advance and fast-forward. The resolution and frame rate parts are the
 * picture's alone and apply everywhere.
 *
 * The same undrawn frames keep the game's speed when the GPU falls behind
 * ("keep_game_speed", vi_aurora.c portPacingBehind): a frame already due on
 * screen when it starts runs undrawn, a few in a row at most. */
#include <aurora/aurora.h>
#include <SDL3/SDL_power.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/platform.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/scene_skip.h"
#include "port/settings.h"
#include "port/speed.h"
#include "port/toggles.h"
#include "driver/dispdrv.h"
#include <stdint.h>

#define CHECK_MS 5000 /* the power state looked at again */

extern s32 entry_n;          /* dispdrv.c: this frame's draw list */
extern DispEntry* pDispWork;

static int sSaving;
static int sUndrawn;      /* this game frame isn't drawn */
static int sSinceDrawn = 1, sDrawFrames = 1;
static unsigned sFrame;

static int* mode(void) {
    static int* sMode;
    if (sMode == NULL) {
        sMode = portSettingsDeclare("battery_saver", 0, 0, 2);
    }
    return sMode;
}

static int onBattery(void) {
    int percent = -1;
    return SDL_GetPowerInfo(NULL, &percent) == SDL_POWERSTATE_ON_BATTERY;
}

/* the power state now and then; a change applies the display settings again */
static void update(void) {
    static Uint64 sNext;
    const Uint64 now = SDL_GetTicks();
    int saving;
    if (now < sNext) {
        return;
    }
    sNext = now + CHECK_MS;
    saving = *mode() == 2 || (*mode() == 1 && onBattery());
    if (saving != sSaving) {
        sSaving = saving;
        printf("[battery] saver %s\n", saving ? "on" : "off");
        fflush(stdout);
        portSettingsApply();
    }
}

int portBatterySaving(void) {
    return sSaving;
}

/* A draw callback this frame's list has that the frame before's hadn't: a pass made once whose work lasts
 * (extdrv.c extEntry's offscreen pass that renders a crowd's sprites to textures: the prologue's X-Naut
 * crowd, made in the ambush scene; punidrv.c's Punies; a picture captured once for a scene). On an undrawn
 * frame dispDraw lets the list go and that work is never done: skipping the ambush left the whole crowd
 * invisible, its shadows alone on the dock (playtest 1). So such a frame is always drawn, while a scene is
 * skipped and while the battery saver skips frames. Callbacks queued every frame (each NPC's under one) are
 * in the frame before's list too, so the frames stay undrawn as before. */
#define PASS_SLOTS 2048 /* a power of two, twice dispdrv.c's 1024 entries */
static int newDrawPass(void) {
    static DispCallback sSeen[2][PASS_SLOTS];
    static unsigned sGen[2][PASS_SLOTS];
    static unsigned sStamp = 1;
    static int sCur;
    const int prev = sCur ^ 1;
    int found = 0;
    s32 i;
    sStamp++;
    for (i = 0; i < entry_n; i++) {
        const DispCallback cb = pDispWork[i].callback;
        const unsigned h = (unsigned)(((uintptr_t)cb >> 4) * 2654435761u) & (PASS_SLOTS - 1);
        unsigned k;
        if (cb == NULL) {
            continue;
        }
        for (k = h; sGen[sCur][k] == sStamp && sSeen[sCur][k] != cb; k = (k + 1) & (PASS_SLOTS - 1)) {
        }
        if (sGen[sCur][k] == sStamp) {
            continue; /* (this frame's already) */
        }
        sGen[sCur][k] = sStamp;
        sSeen[sCur][k] = cb;
        for (k = h; sGen[prev][k] == sStamp - 1 && sSeen[prev][k] != cb; k = (k + 1) & (PASS_SLOTS - 1)) {
        }
        if (sGen[prev][k] != sStamp - 1) {
            found = 1; /* not in the frame before's */
        }
    }
    sCur = prev;
    return found;
}

int portBatterySkipDraw(void) {
    static int sFast = -1;
    int skip;
    const int newPass = newDrawPass(); /* (every frame: the set kept for the next) */
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
    }
    update();
    sFrame++;
    skip = sSaving && (sFrame & 1) && !sFast && !portLockstep() && !portReplayActive() &&
           !portSpeedActive() && !portFrameAdvanceHeld();
    if (portSceneSkipUndrawn() && !portFrameAdvanceHeld()) {
        skip = 1; /* a scene skipped: all but a few of its frames undrawn (port/scene_skip.h) */
    }
    if (!skip && portPacingBehind(sSinceDrawn)) {
        skip = 2; /* keep game speed: the GPU fell behind and this frame is due on screen already (vi_aurora.c) */
    }
    if (skip && newPass) {
        skip = 0; /* a pass made once (newDrawPass) */
        if (portSceneSkipUndrawn()) {
            static unsigned sTold;
            if (sTold++ < 8) { /* (the tests' check; a few) */
                printf("[sceneskip] a new draw pass: this frame drawn\n");
                fflush(stdout);
            }
        }
    }
    if (skip == 2) {
        portPacingCaughtUp();
        skip = 1;
    }
    if (skip) {
        sSinceDrawn++;
    } else {
        sDrawFrames = sSinceDrawn;
        sSinceDrawn = 1;
    }
    sUndrawn = skip;
    return skip;
}

int portBatteryUndrawn(void) {
    return sUndrawn;
}

int portDrawFrames(void) {
    return sDrawFrames;
}

const char* portBatteryFramerate(const char* framerate) {
    return sSaving ? "60" : framerate;
}

int portBatteryResolution(int resolution) {
    int scale = resolution;
    if (!sSaving) {
        return resolution;
    }
    if (scale <= 0) {
        /* the window's own size: its multiple of 480 lines */
        const AuroraWindowSize size = aurora_get_window_size();
        scale = (int)(size.fb_height + 240) / 480;
    }
    scale /= 2;
    return scale < 1 ? 1 : scale;
}
