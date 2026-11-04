/* Action Command cue (settings: ac_cue; port/include/port/ac_cue.h): when
 * to press A for the timed presses the game judges as a jump lands.
 *
 * Attacks: a timed-A command (ac_timing_a: Jump, the partners' stomps) is
 * judged (its result handler) a few frames after the attacker lands, from
 * the presses in the frames before (battleAcTimingA_GetSuccessFrame). Guards:
 * an enemy's jump onto Mario or the partner is judged as it hits
 * (BattleActionCommandCheckDefence), from the presses in the last 10-5
 * frames by difficulty. A unit's jump counts its frames down to the landing
 * (moveFrames), so when such a jump starts the judging frame is known: the
 * landing plus a lag, learned from the last judgement of that kind. The cue
 * aims at the middle of the window and gives a lead-in (a ring that closes
 * on it, port/settings/settings_ui.cpp), since a human needs one: a flash at
 * the window's start would come too late to react to.
 *
 * Other commands (gauges, the hammer's stars) show their own timing, and
 * enemies that don't jump at you can't be predicted this way. */
#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_unit.h"
#include "battle/ac/ac_timing_a.h"
#include "mario/mariost.h"
#include "port/ac_cue.h"
#include "port/gameplus.h"
#include "port/online.h"
#include "port/settings.h"
#include <dolphin/vi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

int gPortAcCue = 0;
int gPortAcCueState = 0;
int gPortAcCueLead = 0;
void (*gPortHapticsCue)(int port, int kind) = NULL;
int gPortHapticsTriggers[4];
unsigned gPortHapticsTriggersAt;
int gPortAcCueToJudge = -1;
int gPortAcCueWindow = 0;

s32 battleAcTimingA_GetSuccessFrame(BattleWork* work);
s32 battleAcResult_TimingA(BattleWork* work);
s32 battleAcMain_GaugeTiming(BattleWork* work);
s32 battleAcMain_PowerGauge(BattleWork* work);

#define LEAD_IN 24 /* frames of ring before the ideal press */

static BattleUnit* sJumpUnit; /* the unit whose jump started last */
static int sKind;             /* its landing will be judged: 1 attack, 2 guard */
static int sWindow;           /* that judgement's window, frames */
static u32 sLanded;           /* retrace of the landing (0: not yet) */
static int sLag[3] = {0, 4, 4}; /* judgement after the landing, by kind (learned; these as measured on US) */
/* the same by the jumper's unit kind, once seen (+1; 0 not yet): a Goomba's
 * stomp is judged as it lands, a Spiky Goomba's 4 frames after */
static s8 sLagOf[3][256];
static int sTest = -1;
static BattleUnit* sDoneUnit; /* judged last: its bounce away is no new attack */
static u32 sDoneUntil;
static BattleUnit* sGuarder;  /* a guard's: the unit landed on */
static int sCued;             /* this judgement's haptic pulse given */
static u32 sCuedAt;           /* its retrace, for the log */
static int* sHaptics;         /* the setting (port/settings/features/haptics.cpp) */
static int sHapticsLog = -1;

static int testing(void) {
    if (sTest < 0) {
        sTest = getenv("TTYD_AC_CUE_PRESS") != NULL; /* tests: on, and logged */
    }
    return sTest;
}

/* Controller haptics: on, and logged ($TTYD_HAPTICS_LOG, or the cue's tests) */
static int hapticsOn(void) {
    if (sHaptics == NULL) {
        sHaptics = portSettingsDeclare("controller_haptics", 1, 0, 1);
    }
    return *sHaptics;
}

static int hapticsLog(void) {
    if (sHapticsLog < 0) {
        sHapticsLog = getenv("TTYD_HAPTICS_LOG") != NULL || getenv("TTYD_HAPTICS_PRESS") != NULL;
    }
    return sHapticsLog;
}

/* the controller on this machine of player 0 (Mario's) or 1 (the second
 * player's); online each machine has its own player on its port 0, and the
 * other's cue is felt over there (-1) */
static int localPort(int player) {
    if (portOnlineActive()) {
        return (player == 0) == (portOnlineIsHost() != 0) ? 0 : -1;
    }
    return player;
}

/* A timed press's window opens next frame (toJudge == the window: the press
 * read next frame is the first the judgement counts): a pulse on the
 * presser's controller, as the game's own rumble allows (its Rumble option,
 * and not over a rumble it is giving). Output only. */
static void hapticsFire(int kind, BattleUnit* presser, int toJudge) {
    const int player = portSecondPlayerPlayerFor(presser, kind == PORT_HAPTICS_GUARD);
    const int port = localPort(player);
    const int allowed = gp != NULL && gp->rumbleEnabled && port >= 0 && gp->rumbleStatus[port] != 1;
    sCued = 1;
    sCuedAt = VIGetRetraceCount();
    if (hapticsLog()) {
        printf("[haptics] retrace %u: %s cue, its %d-frame window opens next frame (judged in %d), player %d, "
               "port %d%s\n",
               (unsigned)sCuedAt, kind == PORT_HAPTICS_ATTACK ? "attack" : "guard", sWindow, toJudge, player, port,
               allowed ? "" : " (no rumble: the game's option, its own rumble, or the other machine's)");
        fflush(stdout);
    }
    if (allowed && gPortHapticsCue != NULL) {
        gPortHapticsCue(port, kind);
    }
}

/* The triggers the running command holds (Koops's Shell Toss style gauges,
 * held with R, or L and R), for the DualSense's resistance on them */
static void hapticsTriggers(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    int mask = 0, port, i;
    if (hapticsOn() && ac->maincb != NULL && ac->acUnit != NULL && ac->acUnit->alliance == 0 && ac->acState != 0) {
        if (ac->maincb == battleAcMain_GaugeTiming && ac->params[0] == 0) {
            mask = 2;
        } else if (ac->maincb == battleAcMain_PowerGauge && ac->params[0] == 0) {
            mask = 3;
        } else if (ac->maincb == battleAcMain_PowerGauge && ac->params[0] == 3) {
            mask = 2;
        }
    }
    port = mask != 0 ? localPort(portSecondPlayerPlayerFor(ac->acUnit, 0)) : -1;
    for (i = 0; i < 4; i++) {
        const int m = i == port ? mask : 0;
        if (m != gPortHapticsTriggers[i] && hapticsLog()) {
            printf("[haptics] retrace %u: port %d triggers %s\n", (unsigned)VIGetRetraceCount(), i,
                   m == 0 ? "free" : m == 2 ? "R resists" : "L and R resist");
            fflush(stdout);
        }
        gPortHapticsTriggers[i] = m;
    }
    gPortHapticsTriggersAt = VIGetRetraceCount();
}

void portAcCueJump(void* unit) {
    if (testing() || hapticsLog()) {
        BattleUnit* u = (BattleUnit*)unit;
        printf("[accue] retrace %u: jump of unit %d (alliance %d, kind %d) to %.0f %.0f %.0f in %d frames\n",
               (unsigned)VIGetRetraceCount(), (int)u->unitId, (int)u->alliance, (int)u->currentKind,
               u->moveTargetPos.x, u->moveTargetPos.y, u->moveTargetPos.z, (int)u->moveFrames);
        fflush(stdout);
    }
    if (sKind == 1 && sJumpUnit != NULL && sJumpUnit != (BattleUnit*)unit) {
        return; /* an attack being judged keeps the cue (the target's reaction hops) */
    }
    sJumpUnit = (BattleUnit*)unit;
    sKind = 0;
    sLanded = 0;
    sCued = 0;
}

/* a jump's landing on Mario or the partner (not back home, nor in front):
 * the one landed on */
static BattleUnit* landsOnPlayer(BattleWork* wp, BattleUnit* jumper) {
    BattleUnit* units[2];
    int i;
    units[0] = BattleGetMarioPtr(wp);
    units[1] = BattleGetPartyPtr(wp);
    for (i = 0; i < 2; i++) {
        f32 x, y, z, dx, dz;
        if (units[i] == NULL) {
            continue;
        }
        BtlUnit_GetPos(units[i], &x, &y, &z);
        dx = jumper->moveTargetPos.x - x;
        dz = jumper->moveTargetPos.z - z;
        if (dx * dx + dz * dz < 25.0f * 25.0f) {
            return units[i];
        }
    }
    return NULL;
}

/* the guard's window, as BattleActionCommandCheckDefence judges it: the
 * guarding unit's Simplifiers and Unsimplifiers, Accessibility's wider windows */
static int guardWindow(BattleUnit* guarder) {
    static const u8 normal[7] = {10, 9, 8, 8, 7, 6, 5};
    int diff = 3, w;
    if (guarder != NULL) {
        diff += guarder->badgesEquipped.Unsimplifier - guarder->badgesEquipped.Simplifier;
    }
    w = normal[diff < 0 ? 0 : diff > 6 ? 6 : diff];
    return portGameplus("ac_guard_assist") == 1 ? w * 2 : w;
}

void portAcCueJudged(int kind, int result) {
    if (kind == sKind && sLanded != 0) {
        /* learn when this kind is judged after a landing */
        const int lag = (int)(VIGetRetraceCount() - sLanded);
        if (lag >= 0 && lag < 30) {
            sLag[kind] = lag;
            if (sJumpUnit != NULL) {
                sLagOf[kind][sJumpUnit->currentKind & 0xFF] = (s8)(lag + 1);
            }
        }
    }
    if (testing()) {
        printf("[accue] retrace %u: %s verdict %d (%s)\n", (unsigned)VIGetRetraceCount(), kind == 1 ? "timed-A" : "guard",
               result, kind == 1 ? "3 = in time" : "4 guard, 5 superguard");
        fflush(stdout);
    }
    if (hapticsLog()) {
        if (kind == sKind && sCued) {
            printf("[haptics] retrace %u: %s judged, verdict %d; the cue came %d frames before (window %d)\n",
                   (unsigned)VIGetRetraceCount(), kind == 1 ? "attack" : "guard", result,
                   (int)(VIGetRetraceCount() - sCuedAt), sWindow);
        } else {
            printf("[haptics] retrace %u: %s judged, verdict %d, with no cue\n", (unsigned)VIGetRetraceCount(),
                   kind == 1 ? "attack" : "guard", result);
        }
        fflush(stdout);
    }
    if (kind == sKind) {
        sDoneUnit = sJumpUnit;
        sDoneUntil = VIGetRetraceCount() + 60;
        sJumpUnit = NULL;
        sKind = 0;
    }
}

void portAcCueUpdate(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    BattleUnit* j = sJumpUnit;
    int state = 0, lead = 0;
    {
        /* $TTYD_AC_CUE_SHOW=<lead> (tests): the cue held on screen at that lead */
        const char* show = getenv("TTYD_AC_CUE_SHOW");
        if (show != NULL) {
            gPortAcCueState = 1;
            gPortAcCueLead = atoi(show);
            return;
        }
    }
    hapticsTriggers(wp);
    gPortAcCueToJudge = -1;
    if ((!gPortAcCue && !testing() && !hapticsOn()) || j == NULL) {
        gPortAcCueState = 0;
        return;
    }
    if (sKind == 0 && sLanded == 0 && j->moveFrames > 0) {
        if (ac->resultcb == battleAcResult_TimingA && ac->acState == 100 && ac->acUnit == j) {
            sKind = 1;
            sWindow = battleAcTimingA_GetSuccessFrame(wp);
            sCued = 0;
        } else if (ac->maincb == NULL && j != BattleGetMarioPtr(wp) && j != BattleGetPartyPtr(wp) &&
                   !(j == sDoneUnit && (s32)(VIGetRetraceCount() - sDoneUntil) < 0) &&
                   (sGuarder = landsOnPlayer(wp, j)) != NULL) {
            sKind = 2;
            sWindow = guardWindow(sGuarder);
            sCued = 0;
        }
    }
    if (sKind != 0) {
        /* frames until the judgement, then until the ideal press (the
         * window's middle, before it) */
        const int known = sLagOf[sKind][j->currentKind & 0xFF];
        const int lag = known > 0 ? known - 1 : sLag[sKind];
        const int toJudge = sLanded == 0 ? j->moveFrames + lag : lag - (int)(VIGetRetraceCount() - sLanded);
        lead = toJudge - sWindow / 2;
        if (lead <= LEAD_IN && toJudge >= 0) {
            state = sKind;
        }
        gPortAcCueToJudge = toJudge >= 0 ? toJudge : -1;
        gPortAcCueWindow = sWindow;
        if (hapticsOn() && !sCued && toJudge >= 0 && toJudge <= sWindow) {
            hapticsFire(sKind, sKind == 1 ? j : sGuarder, toJudge);
        }
        if (toJudge < -10) {
            sJumpUnit = NULL; /* never judged (missed, cancelled) */
            sKind = 0;
        }
    }
    if (sLanded == 0 && j->moveFrames <= 0) {
        sLanded = VIGetRetraceCount();
    }
    if (!gPortAcCue && !testing()) {
        state = 0; /* haptics alone: no ring on screen */
    }
    if (testing() && state != gPortAcCueState) {
        printf("[accue] retrace %u: cue %d (lead %d)\n", (unsigned)VIGetRetraceCount(), state, lead);
        fflush(stdout);
    }
    gPortAcCueState = state;
    gPortAcCueLead = lead;
}
