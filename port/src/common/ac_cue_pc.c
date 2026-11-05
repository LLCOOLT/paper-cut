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
 * Other commands (gauges, the hammer's stars) show their own timing; for
 * them the haptics give a tick as they step on and a pulse where they
 * succeed (acHaptics). Enemies that don't jump at you can't be predicted
 * from a jump: their guards are learned instead, by attack, from the attack's
 * declaration to its hit (attackCue). */
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
#include <string.h>

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
s32 battleAcMain_GaugeTiming2(BattleWork* work);
s32 battleAcMain_AirGauge(BattleWork* work);
s32 battleAcMain_PowerGaugeLv2(BattleWork* work);
s32 battleAcMain_Repeatedly(BattleWork* work);
s32 battleAcMain_RepeatedlyLv(BattleWork* work);
s32 battleAcMain_ShotTarget(BattleWork* work);
s32 battleAcMain_StickKeepLeft(BattleWork* work);

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
        return player == portOnlineSelf() ? 0 : -1; /* (this computer's own player: its controller 1) */
    }
    return player;
}

/* A pulse on the controller of the player who presses for presser's attack
 * (guard 0) or guard (guard 1), as the game's own rumble allows (its Rumble
 * option, and not over a rumble it is giving). Output only. */
static void hapticsPulse(int kind, BattleUnit* presser, int guard, const char* what) {
    const int player = portSecondPlayerPlayerFor(presser, guard);
    const int port = localPort(player);
    const int allowed = gp != NULL && gp->rumbleEnabled && port >= 0 && gp->rumbleStatus[port] != 1;
    if (hapticsLog()) {
        printf("[haptics] retrace %u: %s, player %d, port %d%s\n", (unsigned)VIGetRetraceCount(), what, player, port,
               allowed ? "" : " (no rumble: the game's option, its own rumble, or the other machine's)");
        fflush(stdout);
    }
    if (allowed && gPortHapticsCue != NULL) {
        gPortHapticsCue(port, kind);
    }
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

/* ---- Gauge and stick commands: a tick as they step on, a pulse where they
 * succeed. Read from the running command's own work (the layouts of the
 * ac_*.c files' structs, which keep their work in BattleActions.work). */

static BattleACMainCallback sAcMain; /* the command these are for */
static int sAcFresh;                 /* its first frame: what it starts in is no event */
static int sAcIn;                    /* in its success zone last frame */
static int sAcPeak;                  /* the highest step or level reached */
static u8 sAcTargetIn[8];            /* Mini-Egg: in each target's reach last frame */

#define AC_WORK_F(ac, off) (*(f32*)((u8*)(ac)->work + (off)))
#define AC_WORK_S32(ac, off) (*(s32*)((u8*)(ac)->work + (off)))
#define AC_WORK_S8(ac, off) (*(s8*)((u8*)(ac)->work + (off)))

/* the level a gauge at g has reached against thresholds t[0..n-1] */
static int levelOf(f32 g, const s32* t, int n) {
    int level = 0;
    while (level < n && g >= (f32)t[level]) {
        level++;
    }
    return level;
}

/* zone: in the success zone now (a pulse on entering it); level: a new peak
 * is a tick, or a pulse at top (> 0) and above */
static void acEvent(BattleActions* ac, int zone, int level, int top, const char* name) {
    if (sAcFresh) {
        sAcIn = zone;
        sAcPeak = level;
        sAcFresh = 0;
        return;
    }
    if (zone && !sAcIn) {
        hapticsPulse(PORT_HAPTICS_ZONE, ac->acUnit, 0, name);
    }
    sAcIn = zone;
    if (level > sAcPeak) {
        sAcPeak = level;
        if (!zone) {
            hapticsPulse(top > 0 && level >= top ? PORT_HAPTICS_ZONE : PORT_HAPTICS_TICK, ac->acUnit, 0, name);
        }
    }
}

static void acHaptics(BattleWork* wp) {
    BattleActions* ac = &wp->actionCommands;
    if (!hapticsOn() || ac->maincb == NULL || ac->acUnit == NULL || ac->acUnit->alliance != 0) {
        sAcMain = NULL;
        return;
    }
    if (ac->maincb != sAcMain || ac->acState < 100) {
        sAcMain = ac->maincb;
        sAcFresh = 1;
        memset(sAcTargetIn, 0, sizeof(sAcTargetIn));
        if (ac->acState < 100) {
            return;
        }
    }
    if (ac->acState > 1002 || ac->acUnit->badgesEquipped.AutoCommand) {
        return; /* done, or playing itself */
    }
    if (ac->maincb == battleAcMain_GaugeTiming) {
        /* Shell Toss, Power Shell: stop the marker within 12 of the middle
         * (mode 0) or at params[3] and above (mode 1) */
        const f32 g = AC_WORK_F(ac, 0x4);
        const int zone = ac->params[2] == 1 ? g >= (f32)ac->params[3] : (g - 50.0f <= 12.0f && 50.0f - g <= 12.0f);
        acEvent(ac, zone, 0, 0, "gauge: in the zone");
    } else if (ac->maincb == battleAcMain_PowerGauge) {
        /* Bomb, Gulp, Kiss Thief: at params[4] and above */
        acEvent(ac, AC_WORK_F(ac, 0x4) >= (f32)ac->params[4], 0, 0, "power gauge: far enough");
    } else if (ac->maincb == battleAcMain_GaugeTiming2) {
        /* Mini-Egg: the bar within 6 of a target not scored yet */
        const f32 cursor = AC_WORK_F(ac, 0x4);
        const int n = ac->params[2] < 0 ? 0 : ac->params[2] > 8 ? 8 : ac->params[2];
        int i;
        for (i = 0; i < n; i++) {
            const f32 d = cursor - AC_WORK_F(ac, 0x18 + 4 * i);
            const int in = AC_WORK_S8(ac, 0x38 + i) == 0 && d <= 6.0f && d >= -6.0f;
            if (in && !sAcTargetIn[i] && !sAcFresh) {
                hapticsPulse(PORT_HAPTICS_ZONE, ac->acUnit, 0, "gauge: at a target");
            }
            sAcTargetIn[i] = (u8)in;
        }
        sAcFresh = 0;
    } else if (ac->maincb == battleAcMain_AirGauge) {
        /* Gale Force: levels against params[3..], params[2] of them */
        const int n = ac->params[2] - 1 < 0 ? 0 : ac->params[2] - 1 > 4 ? 4 : ac->params[2] - 1;
        acEvent(ac, 0, levelOf(AC_WORK_F(ac, 0x4), &ac->params[3], n), n, "air gauge: a level up");
    } else if (ac->maincb == battleAcMain_PowerGaugeLv2) {
        /* Lip Lock: the level in outputParams[0]; success from params[4] */
        acEvent(ac, 0, ac->outputParams[0], ac->params[4], "power gauge: a level up");
    } else if (ac->maincb == battleAcMain_Repeatedly) {
        /* running away, Rally Wink: the gauge at the moving mark; a tick when full */
        const f32 g = AC_WORK_F(ac, 0x18), mark = AC_WORK_F(ac, 0x10);
        acEvent(ac, mark > 0.0f && g >= mark, g >= 100.0f ? 1 : 0, 0, "mash gauge: at the mark");
    } else if (ac->maincb == battleAcMain_RepeatedlyLv) {
        /* Shell Slam, Ground Pound, Bob-ombast...: the gauge (outputParams[1])
         * at params[6]; a tick when full */
        const int g = ac->outputParams[1];
        acEvent(ac, ac->params[6] > 0 && g >= ac->params[6], g >= 100 ? 1 : 0, 0, "mash gauge: far enough");
    } else if (ac->maincb == battleAcMain_ShotTarget) {
        /* Body Slam: the stick has the cursor on the target */
        acEvent(ac, AC_WORK_S32(ac, 0x20) != 0, 0, 0, "aim: on the target");
    } else if (ac->maincb == battleAcMain_StickKeepLeft) {
        /* the Hammer: a tick as each light comes on (outputParams[1]), the
         * pulse at the last, big one: let go now */
        acEvent(ac, 0, ac->outputParams[1], ac->params[0] > 0 ? ac->params[0] : 0, "stick: a light");
    } else {
        sAcFresh = 0;
    }
}

/* ---- Guards against attacks that aren't jumps: learned per attack (the
 * place in the script that declares it, the attacker's kind and where it
 * stands) as the frames from the declaration to the hit. Once two of the
 * same attack took the same time (to a frame), the next is cued. */

typedef struct AttackTiming {
    const void* where;
    s16 kind;
    s16 x;
    s16 lag[2]; /* the last two, newest first (-1: not yet) */
} AttackTiming;
static AttackTiming sAtkTable[64];
static int sAtkNext;
static AttackTiming* sAtk; /* the attack running (NULL: none) */
static u32 sAtkAt;         /* its declaration's retrace */
static BattleUnit* sAtkTarget;
static int sAtkCued;

#define ATK_TABLE_N ((int)(sizeof(sAtkTable) / sizeof(sAtkTable[0])))

void portAcCueAttack(void* attacker, void* target, const void* where) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* a = (BattleUnit*)attacker;
    BattleUnit* t = (BattleUnit*)target;
    AttackTiming* e = NULL;
    s16 x;
    int i;
    if (a == NULL || t == NULL || wp == NULL || a->alliance == 0 ||
        (t != BattleGetMarioPtr(wp) && t != BattleGetPartyPtr(wp))) {
        return;
    }
    x = (s16)((s32)a->homePosition.x / 8);
    for (i = 0; i < ATK_TABLE_N; i++) {
        AttackTiming* c = &sAtkTable[i];
        if (c->where == where && c->kind == (s16)a->currentKind && c->x == x) {
            e = c;
            break;
        }
    }
    if (e == NULL) {
        e = &sAtkTable[sAtkNext];
        sAtkNext = (sAtkNext + 1) % ATK_TABLE_N;
        e->where = where;
        e->kind = (s16)a->currentKind;
        e->x = x;
        e->lag[0] = e->lag[1] = -1;
    }
    sAtk = e;
    sAtkAt = VIGetRetraceCount();
    sAtkTarget = t;
    sAtkCued = 0;
    if (hapticsLog()) {
        printf("[haptics] retrace %u: attack of kind %d on unit %d declared; learned %d, %d frames to the hit\n",
               (unsigned)sAtkAt, (int)a->currentKind, (int)t->unitId, e->lag[0], e->lag[1]);
        fflush(stdout);
    }
}

static void attackJudged(void) {
    if (sAtk != NULL) {
        const int lag = (int)(VIGetRetraceCount() - sAtkAt);
        if (lag > 0 && lag < 0x7FFF) {
            sAtk->lag[1] = sAtk->lag[0];
            sAtk->lag[0] = (s16)lag;
        }
        sAtk = NULL; /* the first hit only */
    }
}

static void attackCue(void) {
    int toJudge, d;
    if (sAtk == NULL) {
        return;
    }
    toJudge = sAtk->lag[0] - (int)(VIGetRetraceCount() - sAtkAt);
    if ((int)(VIGetRetraceCount() - sAtkAt) > 1200) {
        sAtk = NULL; /* never hit (it missed, or was no attack on us after all) */
        return;
    }
    d = sAtk->lag[0] - sAtk->lag[1];
    if (sAtkCued || sAtk->lag[0] <= 0 || sAtk->lag[1] <= 0 || d > 1 || d < -1) {
        return;
    }
    if (sKind == 2 || (sCued && VIGetRetraceCount() - sCuedAt < 30)) {
        return; /* a jump's guard: cued from the jump */
    }
    if (toJudge >= 0 && toJudge <= guardWindow(sAtkTarget)) {
        sAtkCued = 1;
        hapticsPulse(PORT_HAPTICS_GUARD, sAtkTarget, 1, "guard cue (an attack learned)");
    }
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
    if (kind == 2) {
        attackJudged();
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
    acHaptics(wp);
    if (hapticsOn()) {
        attackCue();
    }
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
