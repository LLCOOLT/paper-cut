/* Fast-forward: while the fast-forward control is held (or toggled on) the
 * game runs at the settings' speed: its clock (os_time_aurora.c) and the
 * frame pacing (vi_aurora.c, which waits on that clock) both scale, so
 * frame-counted and timed things keep in step. VSync is off meanwhile (a
 * vsynced present would hold the frame rate to the display's). The audio
 * device keeps real time.
 *
 * Controls: Tab, a controller's right stick click, the on-screen ">>"
 * (port/touch/touch_overlay.cpp). With "Skip the intro" the boot logos also
 * run fast (portIntroFrame, seq_logo.c).
 *
 * Slow speed (Accessibility, port/settings/features/slow_speed.cpp), its
 * opposite: the game at 50-90 % (in battle only, if so set), for more time
 * for the action commands. The game's clock runs that much slower and the
 * frames, paced on it, come that much later: the game sees the same frames,
 * only later. With $TTYD_FAST's virtual clock (tests) the frames are paced
 * on the wall clock instead (portSpeedPercent, vi_aurora.c). Not online (both
 * games keep the same pace), not while fast-forwarding. The audio device
 * keeps real time, as with fast-forward: the music and sounds stay smooth.
 *
 * Faster battles (port/include/port/battle_speed.h) set a battle's speed
 * the same way, 1.5x or 2x while the player isn't acting; fast-forward held
 * on top of it wins. */
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <stdlib.h>
#include "port/platform.h"
#include "port/settings.h"
#include "port/gameplay.h"
#include "port/speed.h"
#include "port/replay.h"
#include "port/online.h"
#include "driver/seqdrv.h"
#include "port/battle_speed.h"

#define INTRO_SPEED 16

extern int gPortIntroFrames; /* gameplay_pc.c */

static int sHeld;   /* the control held (hold mode) */
static int sToggled; /* toggled on (toggle mode) */
static int sVsyncOff;
static int sReplaySpeed = 1;

int portReplaySpeed(void) {
    return sReplaySpeed < 1 ? 1 : sReplaySpeed;
}

static int sPercent = 100; /* slow speed this frame */

/* slow speed as the settings say (100: off) */
static int slowWanted(void) {
    static const int* sChoice;
    static const int* sBattleOnly;
    if (sChoice == NULL) {
        sChoice = portSettingsFind("slow_speed"); /* 0 off, 1-5: 90 % down to 50 % */
        sBattleOnly = portSettingsFind("slow_speed_battle");
    }
    if (sChoice == NULL || *sChoice <= 0 || portOnlineActive()) {
        return 100;
    }
    if (sBattleOnly != NULL && *sBattleOnly && seqGetSeq() != SEQ_BATTLE) {
        return 100;
    }
    return 100 - 10 * (*sChoice < 5 ? *sChoice : 5);
}

int portSpeedPercent(void) {
    return sPercent;
}

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
    return portClockSpeedPct() > 100;
}

void portSpeedFrame(void) {
    static int sFast = -1, sTest = -1;
    int speed = 1;
    if (sFast < 0) {
        sFast = getenv("TTYD_FAST") != NULL; /* tests: a virtual clock already */
    }
    sPercent = slowWanted();
    if (sFast || portLockstep()) {
        /* online co-op too: the other game can't run faster; a replay's
           playback can (its frames sooner, the game clock as it was) */
        sReplaySpeed = portReplayPlaying() && (portSettings()->ffToggle ? sToggled : sHeld) ? portSettings()->ffSpeed : 1;
        return;
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
    speed *= 100;
    if (speed < portBattleSpeedPct()) {
        speed = portBattleSpeedPct();
    }
    if (speed > 100) {
        sPercent = 100; /* fast-forward and a faster battle win over slow speed */
    }
    portClockSetSpeedPct(speed);
    portClockSetPercent(sPercent);
    /* a faster battle's vsync off for all of it, not at each change of speed */
    if ((speed > 100 || portBattleSpeedOn()) != sVsyncOff) {
        sVsyncOff = speed > 100 || portBattleSpeedOn();
        aurora_enable_vsync(!sVsyncOff && portSettings()->vsync);
    }
}

/* The settings changed vsync: not while fast-forwarding. */
int portSpeedVsync(int vsync) {
    return vsync && !sVsyncOff;
}
