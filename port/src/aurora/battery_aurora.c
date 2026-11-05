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
 * picture's alone and apply everywhere. */
#include <aurora/aurora.h>
#include <SDL3/SDL_power.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/online.h"
#include "port/platform.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/speed.h"
#include "port/toggles.h"

#define CHECK_MS 5000 /* the power state looked at again */

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

int portBatterySkipDraw(void) {
    static int sFast = -1;
    int skip;
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL;
    }
    update();
    sFrame++;
    skip = sSaving && (sFrame & 1) && !sFast && !portLockstep() && !portOnlineActive() && !portReplayActive() &&
           !portSpeedActive() && !portFrameAdvanceHeld();
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
