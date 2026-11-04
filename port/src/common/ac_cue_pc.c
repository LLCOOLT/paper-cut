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
#include "port/ac_cue.h"
#include <dolphin/vi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

int gPortAcCue = 0;
int gPortAcCueState = 0;
int gPortAcCueLead = 0;
int gPortAcCueHold = 0;
int gPortAcCueHoldLead = 0;

s32 battleAcTimingA_GetSuccessFrame(BattleWork* work);
s32 battleAcResult_TimingA(BattleWork* work);

#define LEAD_IN 24 /* frames of ring before the ideal press */

static BattleUnit* sJumpUnit; /* the unit whose jump started last */
static int sKind;             /* its landing will be judged: 1 attack, 2 guard */
static int sWindow;           /* that judgement's window, frames */
static u32 sLanded;           /* retrace of the landing (0: not yet) */
static int sLag[3] = {0, 5, 0}; /* judgement after the landing, by kind (learned) */
static int sTest = -1;
static BattleUnit* sDoneUnit; /* judged last: its bounce away is no new attack */
static u32 sDoneUntil;

static int testing(void) {
    if (sTest < 0) {
        sTest = getenv("TTYD_AC_CUE_PRESS") != NULL || getenv("TTYD_AC_PLAY") != NULL; /* tests: on, and logged */
    }
    return sTest;
}

void portAcCueJump(void* unit) {
    if (testing()) {
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
}

/* a jump's landing on Mario or the partner (not back home, nor in front) */
static int landsOnPlayer(BattleWork* wp, BattleUnit* jumper) {
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
            return 1;
        }
    }
    return 0;
}

static int guardWindow(BattleWork* wp) {
    static const u8 normal[7] = {10, 9, 8, 8, 7, 6, 5};
    BattleUnit* m = BattleGetMarioPtr(wp);
    int diff = 3;
    if (m != NULL) {
        diff += m->badgesEquipped.Unsimplifier - m->badgesEquipped.Simplifier;
    }
    return normal[diff < 0 ? 0 : diff > 6 ? 6 : diff];
}

void portAcCueJudged(int kind, int result) {
    if (kind == sKind && sLanded != 0) {
        /* learn when this kind is judged after a landing */
        const int lag = (int)(VIGetRetraceCount() - sLanded);
        if (lag >= 0 && lag < 30) {
            sLag[kind] = lag;
        }
    }
    if (testing()) {
        printf("[accue] retrace %u: %s verdict %d (%s)\n", (unsigned)VIGetRetraceCount(), kind == 1 ? "timed-A" : "guard",
               result, kind == 1 ? "3 = in time" : "4 guard, 5 superguard");
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
    if (gPortAcCueHold && (gPortAcCue || testing())) {
        gPortAcCueState = 2;
        gPortAcCueLead = gPortAcCueHoldLead;
        return;
    }
    if ((!gPortAcCue && !testing()) || j == NULL) {
        gPortAcCueState = 0;
        return;
    }
    if (sKind == 0 && sLanded == 0 && j->moveFrames > 0) {
        if (ac->resultcb == battleAcResult_TimingA && ac->acState == 100 && ac->acUnit == j) {
            sKind = 1;
            sWindow = battleAcTimingA_GetSuccessFrame(wp);
        } else if (ac->maincb == NULL && j != BattleGetMarioPtr(wp) && j != BattleGetPartyPtr(wp) &&
                   !(j == sDoneUnit && (s32)(VIGetRetraceCount() - sDoneUntil) < 0) && landsOnPlayer(wp, j)) {
            sKind = 2;
            sWindow = guardWindow(wp);
        }
    }
    if (sKind != 0) {
        /* frames until the judgement, then until the ideal press (the
         * window's middle, before it) */
        const int toJudge = sLanded == 0 ? j->moveFrames + sLag[sKind]
                                         : sLag[sKind] - (int)(VIGetRetraceCount() - sLanded);
        lead = toJudge - sWindow / 2;
        if (lead <= LEAD_IN && toJudge >= 0) {
            state = sKind;
        }
        if (toJudge < -10) {
            sJumpUnit = NULL; /* never judged (missed, cancelled) */
            sKind = 0;
        }
    }
    if (sLanded == 0 && j->moveFrames <= 0) {
        sLanded = VIGetRetraceCount();
    }
    if (testing() && state != gPortAcCueState) {
        printf("[accue] retrace %u: cue %d (lead %d)\n", (unsigned)VIGetRetraceCount(), state, lead);
        fflush(stdout);
    }
    gPortAcCueState = state;
    gPortAcCueLead = lead;
}
