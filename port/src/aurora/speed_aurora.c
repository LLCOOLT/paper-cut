/* Fast-forward: while the fast-forward control is held (or toggled on) the
 * game runs at the settings' speed: its clock (os_time_aurora.c) and the
 * frame pacing (vi_aurora.c, which waits on that clock) both scale, so
 * frame-counted and timed things keep in step. VSync is off meanwhile (a
 * vsynced present would hold the frame rate to the display's). The audio
 * device keeps real time.
 *
 * Controls: Tab, a controller's right stick click, the on-screen ">>"
 * (port/touch/touch_overlay.cpp). With "Skip the intro" the boot logos also
 * run fast (portIntroFrame, seq_logo.c). */
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <stdlib.h>
#include "port/platform.h"
#include "port/settings.h"
#include "port/gameplay.h"
#include "port/speed.h"

#define INTRO_SPEED 16

extern int gPortIntroFrames; /* gameplay_pc.c */

static int sHeld;   /* the control held (hold mode) */
static int sToggled; /* toggled on (toggle mode) */
static int sVsyncOff;

void portSpeedControl(int down) {
    if (portSettings()->ffToggle) {
        if (down) {
            sToggled = !sToggled;
        }
    } else {
        sHeld = down;
    }
}

int portSpeedActive(void) {
    return portClockSpeed() > 1;
}

void portSpeedFrame(void) {
    static int sFast = -1, sTest = -1;
    int speed = 1;
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL; /* tests: a virtual clock already */
    }
    if (sFast || portLockstep()) {
        return; /* online co-op too: the other game can't run faster */
    }
    if (sTest < 0) {
        /* $TTYD_FF_TEST (tests): fast-forward held from the start */
        sTest = getenv("TTYD_FF_TEST") != NULL;
    }
    if (sTest || (portSettings()->ffToggle ? sToggled : sHeld)) {
        speed = portSettings()->ffSpeed;
    }
    if (gPortIntroFrames > 0 && gPortSkipIntro && speed < INTRO_SPEED) {
        speed = INTRO_SPEED;
    }
    gPortIntroFrames = 0;
    portClockSetSpeed(speed);
    if ((speed > 1) != sVsyncOff) {
        sVsyncOff = speed > 1;
        aurora_enable_vsync(!sVsyncOff && portSettings()->vsync);
    }
}

/* The settings changed vsync: not while fast-forwarding. */
int portSpeedVsync(int vsync) {
    return vsync && !sVsyncOff;
}
