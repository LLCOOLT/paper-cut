#include "port_hook.h"
#ifdef TARGET_PC
#include "port/field_remake.h"
#endif
// Action command manager: sets up, runs, stops and scores action commands for the current attack or defense.
#include "battle/battle_ac.h"
#include "battle/battle.h"
#include "battle/ac/ac_table.h"
#include "driver/dispdrv.h"
#include "driver/camdrv.h"
#include "driver/icondrv.h"
#include "battle/battle_disp.h"
#include "battle/battle_unit.h"
#include "icon_ids.h"
#ifdef PORT_MODS
#include "port/hooks/src_battle_battle_ac.h"
#endif
#include "pmario_sound.h"
#include "battle/battle_audience.h"

#define AC_SE_ID(wp) ((wp)->actionCommands.seId)
#define AC_SE_VAL(wp) ((wp)->actionCommands.seValue)
#define AC_GUARD_TIMER(wp) ((wp)->actionCommands.countdown)

// local prototypes
static s32 BattleACPadCheckRecordTrigger(s32 chan, s32 mask);

#ifdef TARGET_PC
void portAcCueUpdate(BattleWork* wp); /* port/src/common/ac_cue_pc.c */
void portAcCueJudged(int kind, int result);
BattleWorkPad* portSecondPlayerGuardPad(BattleUnit* unit); /* port/src/common/second_player_pc.c */
static BattleWorkPad* sGuardPad; /* the pad record this guard is judged from (NULL: actionCommands.pad) */
#include "port/gameplus.h"
#include "port/settings.h" /* (the tests' log: hold_to_mash) */
#include "port/battle_speed.h"
#include "port/versus.h"
#include "port/dojo.h" /* the action-command dojo, timing assist by command type */
#include <stdio.h>
#include <stdlib.h>
/* Accessibility action-command assist (port/include/port/gameplus.h): an automatic
 * attack command runs as with an Auto Command badge on Mario or the partner;
 * so does a versus computer's when its roll came up (port/include/port/versus.h) */
static u8 portAutoAttackOn(BattleWork* wp) {
    BattleUnit* unit = wp->actionCommands.acUnit;
    const int cpu = portVersusAutoCommand(unit); /* (-1: not the computer's) */
    u8 saved;
    if (unit == NULL || !portVersusPlayerUnit(unit) || (cpu < 0 ? portGameplus("ac_attack_assist") != 2 : cpu == 0)) {
        return 0xFF;
    }
    saved = unit->badgesEquipped.AutoCommand;
    unit->badgesEquipped.AutoCommand = 1;
    return saved;
}
static void portAutoAttackOff(BattleWork* wp, u8 saved) {
    if (saved != 0xFF) {
        wp->actionCommands.acUnit->badgesEquipped.AutoCommand = saved;
    }
}
/* tests ($TTYD_AC_LOG): each attack command's outcome, once, when it is judged */
static s32 sLogId = -1; /* the command set up last (ac_table.c's id); -1 once logged */
static void portAcLog(BattleWork* wp) {
    static int on = -1;
    if (on < 0) {
        on = getenv("TTYD_AC_LOG") != NULL;
    }
    if (on && sLogId >= 0 && !(wp->actionCommands.result & 1)) {
        BattleUnit* unit = wp->actionCommands.acUnit;
        printf("[ac] command %d of unit kind %d: result %d (%s), auto %d, out %d %d %d, assist %d, mash %d\n",
               (int)sLogId, unit != NULL ? (int)unit->currentKind : -1, (int)wp->actionCommands.result,
               (wp->actionCommands.result & 2) ? "success" : "fail", portGameplus("ac_attack_assist") == 2,
               (int)wp->actionCommands.outputParams[0], (int)wp->actionCommands.outputParams[1],
               (int)wp->actionCommands.outputParams[2], portGameplus("ac_attack_assist"),
               portSettingsFind("hold_to_mash") != NULL ? *portSettingsFind("hold_to_mash") : -1);
        fflush(stdout);
        sLogId = -1;
    }
}
/* tests ($TTYD_AC_LOG): each guard judged (5 Superguard, 4 Guard, 2 no press, 1 mashed), its windows and assists */
static void portAcGuardLog(BattleUnit* unit, s32 result, s32 normal, s32 superguard) {
    static int on = -1;
    if (on < 0) {
        on = getenv("TTYD_AC_LOG") != NULL;
    }
    if (on) {
        printf("[ac] guard by unit kind %d: result %d, windows %d/%d, guard assist %d, auto badge %d\n",
               unit != NULL ? (int)unit->currentKind : -1, (int)result, (int)normal, (int)superguard,
               portGameplus("ac_guard_assist"), unit != NULL ? (int)unit->badgesEquipped.AutoCommand : -1);
        fflush(stdout);
    }
}
#endif

/** Clears the action-command manager at battle start: resets its pad history and drops every command callback. */
void PORT_HOOKABLE(BattleActionCommandManagerInit)(BattleWork* wp) {
    BtlPad_WorkInit(&wp->actionCommands.pad);
    wp->actionCommands.maincb = NULL;
    wp->actionCommands.resultcb = NULL;
    wp->actionCommands.dispcb = NULL;
    wp->actionCommands.deletecb = NULL;
    wp->actionCommands.seId = -1;
#ifdef TARGET_PC
    portDojoBattleStart(); /* the dojo's window: a new battle */
#endif
}

/**
 * Per-frame action-command driver: records the pad, runs the active command's main callback until it returns 0,
 * and queues its draw callback on the 2D camera.
 */
void PORT_HOOKABLE(BattleActionCommandManager)(BattleWork* wp) {
    BattleACMainCallback main;
    DispCallback disp;

    BtlPad_WorkUpdate(&wp->actionCommands.pad, 0);
#ifdef TARGET_PC
	portAcCueUpdate(wp); /* the Action Command cue setting */
	portGuardStanceUpdate(wp); /* remake: Guard and Superguard stances (field_remake_pc.c) */
	portBattleSpeedUpdate(wp); /* faster battles: this frame's speed (port/include/port/battle_speed.h) */
	portDojoBattleFrame(wp); /* the dojo's judging and window (port/include/port/dojo.h) */
#endif
    main = wp->actionCommands.maincb;
    if (main) {
#ifdef TARGET_PC
        const u8 autoSaved = portAutoAttackOn(wp);
        const s32 more = main(wp);
        portAutoAttackOff(wp, autoSaved);
        if (!more) {
#else
        if (!main(wp)) {
#endif
            wp->actionCommands.maincb = NULL;
        }

        disp = wp->actionCommands.dispcb;
        if (disp) {
            dispEntry(CAMERA_2D, 900.0f, 1, disp, wp);
        }
    }
}

/**
 * Returns the active command's result through its result callback,
 * or bit 1 of the raw result (success) when it has none.
 */
s32 PORT_HOOKABLE(BattleActionCommandResult)(BattleWork* wp) {
    BattleACResultCallback result;

    result = wp->actionCommands.resultcb;
    if (result) {
#ifdef TARGET_PC
		const u8 autoSaved = portAutoAttackOn(wp);
		portDojoBeforeResult(wp);
		const s32 r = result(wp);
		portDojoAfterResult(wp);
		portAutoAttackOff(wp, autoSaved);
		portAcLog(wp);
		portAcCueJudged(1, wp->actionCommands.outputParams[0]); /* port/src/common/ac_cue_pc.c */
		return r;
#else
        return result(wp);
#endif
    } else {
        return wp->actionCommands.result & 2;
    }
}

/**
 * Reports an action command's outcome to the crowd: -1 counts as a flop (audience reacts badly);
 * otherwise the weapon, its Bingo-slot chance and a Star Power multiplier of 0.5 + 0.25 * result are stored for
 * the bonus that follows.
 */
void PORT_HOOKABLE(BattleActionCommandDeclareACResult)(BattleWork* work, BattleWeapon* weapon, s32 result) {
    StarPowerInfo* info = &work->impendingBonuses;
    switch (result) {
        case -1:
            BattleAudience_Case_ActionCommandBad(weapon);
            break;
        default:
            info->weapon = weapon;
            info->spAcSuccessMultiplier = 0.25f * (f32)result + 0.5f;
            info->reserved_09 = weapon->reserved_19;
            info->bingoSlotChance = weapon->bingoSlotChance;
            BattleAudience_Case_ActionCommandGood(weapon);
            break;
    }
}

/**
 * Prepares action command id (from ActionCommandList) for unit:
 * stores the setup flags and parameter, resets the state and result, and installs the command's main,
 * result and delete callbacks. A confused unit gets no on-screen prompt (no draw callback).
 */
void PORT_HOOKABLE(BattleActionCommandSetup)(BattleWork* work, s32 id, BattleUnit* unit, s32 setupFlags, s32 setupParam) {
    ActionCommandEntry* entry; // r6

    entry = ActionCommandList;
    work->actionCommands.acUnit = unit;
    work->actionCommands.setupFlags = setupFlags;
    work->actionCommands.acState = 0;
    work->actionCommands.resultCount = 0;
    work->actionCommands.result = 1;
    work->actionCommands.maincb = NULL;
    work->actionCommands.dispcb = NULL;
    work->actionCommands.resultcb = NULL;
    work->actionCommands.deletecb = NULL;
    work->actionCommands.setupParam = setupParam;
    work->actionCommands.doneFlags = 0;
#ifdef TARGET_PC
    sLogId = id;
#endif
    for (; entry->id; entry++) {
        if (entry->id == id) {
            work->actionCommands.maincb = entry->main;
            if (!(work->actionCommands.acUnit->tokenFlags & TOKEN_FLAG_CONFUSED_ACTION)) {
                work->actionCommands.dispcb = entry->disp;
            }
            work->actionCommands.resultcb = entry->result;
            work->actionCommands.deletecb = entry->delete;
            return;
        }
    }
}

/** Starts the prepared action command (state 100 tells its main callback to begin). */
void PORT_HOOKABLE(BattleActionCommandStart)(BattleWork* work) {
    work->actionCommands.acState = 100;
}

/** Ends the active action command early through its delete callback. */
void PORT_HOOKABLE(BattleActionCommandStop)(BattleWork* work) {
    if (work->actionCommands.deletecb) {
        work->actionCommands.deletecb(work);
    }
}

// FAKE: inlining off (FAKES.md); without it 1 function changes length, e.g. BattleActionCommandCheckDefence 154 -> 212 instructions
#pragma dont_inline on
/**
 * Judges Mario's or a partner's guard against weapon from the recent pad history and returns defenseResult:
 * 5 = Superguard (B within the window, allowed by the weapon and while above 0 HP),
 * 4 = Guard (A within the window), 2 = no press,
 * 1 = failed because buttons were mashed (two or more presses in the last 15 frames).
 * The windows shrink or widen with Simplifier and Unsimplifier (A: 10..5 frames, B: 5..1);
 * Auto Command badges always Superguard.
 * While actionCommands.countdown is above 0, each check uses one count and passes automatically.
 */
s32 PORT_HOOKABLE(BattleActionCommandCheckDefence)(BattleUnit* unit, BattleWeapon* weapon) {
    BattleWork* wp;
    void* pad;
    s32 i;
    s32 count;
    s32 done;
    s32 superguard;
    s32 normal;
    s32 diff;
    const u8 normal_frames[7] = {10, 9, 8, 8, 7, 6, 5};
    const u8 super_frames[7] = {5, 4, 3, 3, 2, 1, 1};

    done = 0;
    wp = _battleWorkPointer;
    pad = &wp->actionCommands.pad;
    wp->actionCommands.defenseResult = 0;
#ifdef TARGET_PC
    sGuardPad = portSecondPlayerGuardPad(unit); // co-op: the second player guards the partner
#endif
    diff = unit->badgesEquipped.Unsimplifier + 3;
    diff -= unit->badgesEquipped.Simplifier;
    if (diff < 0) {
        diff = 0;
    }
    if (diff > 6) {
        diff = 6;
    }
    superguard = super_frames[diff];
    normal = normal_frames[diff];
#ifdef TARGET_PC
    portDojoAssistGuard(&normal, &superguard); // Accessibility: wider guard windows (or by type)
    // faster battles: as many frames as the windows' time at 1x
    normal = portBattleSpeedFrames(normal);
    superguard = portBattleSpeedFrames(superguard);
    const s32 mash = portBattleSpeedFrames(15);
#else
#define mash 15
#endif

    if (weapon->superguardsAllowed && unit->currentHp > 0) {
        if (AC_GUARD_TIMER(wp) > 0) {
            AC_GUARD_TIMER(wp)
            --;
            done = 1;
            wp->actionCommands.defenseResult = 5;
        } else if (unit->badgesEquipped.AutoCommand) {
            done = 1;
            wp->actionCommands.defenseResult = 5;
        } else {
            for (i = 0; i < superguard; i++) {
                if (BattleACPadCheckRecordTrigger(i, 0x200)) {
                    break;
                }
            }
            if (i < superguard) {
                count = 0;
                for (i = 0; i < mash; i++) {
                    if (BattleACPadCheckRecordTrigger(i, 0x100)) {
                        count++;
                    }
                    if (BattleACPadCheckRecordTrigger(i, 0x200)) {
                        count++;
                    }
                }
                if (count < 2) {
                    done = 1;
                    wp->actionCommands.defenseResult = 5;
                }
            }
        }
    }

    if (!done) {
        if (AC_GUARD_TIMER(wp) > 0) {
            AC_GUARD_TIMER(wp)
            --;
            wp->actionCommands.defenseResult = 4;
        } else {
            count = 0;
            for (i = 0; i < mash; i++) {
                if (BattleACPadCheckRecordTrigger(i, 0x100)) {
                    count++;
                }
                if (BattleACPadCheckRecordTrigger(i, 0x200)) {
                    count++;
                }
            }
            if (count >= 2) {
                wp->actionCommands.defenseResult = 1;
            } else {
                for (i = 0; i < normal; i++) {
                    if (BattleACPadCheckRecordTrigger(i, 0x100)) {
                        break;
                    }
                }
                if (i >= normal) {
                    if (count > 0) {
                        wp->actionCommands.defenseResult = 1;
                    } else {
                        wp->actionCommands.defenseResult = 2;
                    }
                } else {
                    wp->actionCommands.defenseResult = 4;
                }
            }
        }
    }
#ifdef TARGET_PC
    if (portGameplus("ac_guard_assist") == 2 && wp->actionCommands.defenseResult < 4) {
        wp->actionCommands.defenseResult = 4; // Accessibility: automatic guards (a Superguard is still the player's)
    }
    wp->actionCommands.defenseResult = portChallengeGuard(wp->actionCommands.defenseResult); // challenge: no guarding
    portAcGuardLog(unit, wp->actionCommands.defenseResult, normal, superguard);
    portDojoGuardJudged(unit, weapon, normal, superguard, mash, sGuardPad != NULL ? sGuardPad : (BattleWorkPad*)pad,
                        wp->actionCommands.defenseResult); // the dojo: how far from the window
#endif
    BtlPad_WorkInit(pad);
#ifndef TARGET_PC
#undef mash
#endif
#ifdef TARGET_PC
	portAcCueJudged(2, wp->actionCommands.defenseResult); /* port/src/common/ac_cue_pc.c */
#endif
    return wp->actionCommands.defenseResult;
}
/** Returns the buttons in mask that were newly pressed chan frames ago in the action-command pad history. */
static s32 BattleACPadCheckRecordTrigger(s32 chan, s32 mask) {
#ifdef TARGET_PC
    if (sGuardPad != NULL) {
        return sGuardPad->buttonsPressedHistory[chan] & mask;
    }
#endif
    return _battleWorkPointer->actionCommands.pad.buttonsPressedHistory[chan] & mask;
}
#pragma dont_inline reset

/** Returns the last guard result from BattleActionCommandCheckDefence (5 Superguard, 4 Guard, 2 none, 1 mashed). */
s32 PORT_HOOKABLE(BattleActionCommandGetDefenceResult)(void) {
    return _battleWorkPointer->actionCommands.defenseResult;
}

/**
 * Clears the guard result and the pad history, so damage that is not guardable (audience items) counts as
 * unguarded.
 */
void PORT_HOOKABLE(BattleActionCommandResetDefenceResult)(void) {
    BattleWork* wp = _battleWorkPointer;
    wp->actionCommands.defenseResult = 0;
    BtlPad_WorkInit(&wp->actionCommands.pad);
}

/** Returns the action-command difficulty level 0-6 after Simplifier/Unsimplifier adjustments. */
s32 PORT_HOOKABLE(BattleActionCommandGetDifficulty)(BattleWork* work) {
    return work->actionCommands.acDifficulty;
}

/**
 * Sets the base action-command difficulty and the adjusted one:
 * minus one per Simplifier, plus one per Unsimplifier the unit wears, clamped to 0-6.
 */
void PORT_HOOKABLE(BattleActionCommandSetDifficulty)(BattleWork* work, BattleUnit* unit, s32 difficulty) {
    s32 adjusted;
    s32 simp;

    work->actionCommands.baseAcDifficulty = difficulty;
    simp = unit->badgesEquipped.Simplifier;
    adjusted = difficulty - simp;
    adjusted += unit->badgesEquipped.Unsimplifier;
    if (adjusted < 0) {
        adjusted = 0;
    }
    if (adjusted > 6) {
        adjusted = 6;
    }
#ifdef TARGET_PC
    if (portVersusPlayerUnit(unit) && portGameplus("ac_attack_assist") == 1) { // (versus: both sides' players)
        adjusted = adjusted > 3 ? adjusted - 3 : 0; // Accessibility: wider attack windows
    } else if (portVersusPlayerUnit(unit) && portGameplus("ac_attack_assist") == 3) {
        adjusted = portDojoAssistAttack(unit, adjusted); // Accessibility: wider by command type
    }
    if (portVersusPlayerUnit(unit)) {
        adjusted = portChallengeCommandLevel(adjusted); // challenge: harder attack commands
    }
#endif
    work->actionCommands.acDifficulty = adjusted;
}

/**
 * Returns the prize level (0-6) for a successful command, lv plus the difficulty minus one,
 * so harder commands earn the better rating; -1 when the unit's damage code is 0x1E.
 */
s32 PORT_HOOKABLE(BattleActionCommandGetPrizeLv)(BattleWork* work, BattleUnit* unit, s32 lv) {
    if (unit != NULL && (u8)unit->damageCode == DAMAGE_HIT_NONE) {
        return -1;
    }
    lv += work->actionCommands.acDifficulty;
    lv--;
    if (lv < 0) {
        lv = 0;
    }
    if (lv >= 7) {
        lv = 6;
    }
    return lv;
}

/**
 * Returns the icon to show for a button in an action-command prompt (A, B, X, Y, Z, L, R and the stick
 * directions), in its pressed or released form.
 */
u16 PORT_HOOKABLE(BattleACGetButtonIcon)(s32 button, s32 pressed) {
    u16 icon = 0;

    switch (button) {
        case 0x100:
            if (pressed)
                icon = ICON_065;
            else
                icon = ICON_064;
            break;
        case 0x200:
            if (pressed)
                icon = ICON_067;
            else
                icon = ICON_066;
            break;
        case 0x400:
            if (pressed)
                icon = ICON_069;
            else
                icon = ICON_068;
            break;
        case 0x800:
            if (pressed)
                icon = ICON_06B;
            else
                icon = ICON_06A;
            break;
        case 0x20:
            if (pressed)
                icon = ICON_081;
            else
                icon = ICON_080;
            break;
        case 0x40:
            if (pressed)
                icon = ICON_07F;
            else
                icon = ICON_07E;
            break;
        case 0x10:
            if (pressed)
                icon = ICON_083;
            else
                icon = ICON_082;
            break;
        case 0x10000:
            if (pressed)
                icon = ICON_07C;
            else
                icon = ICON_079;
            break;
        case 0x20000:
            if (pressed)
                icon = ICON_076;
            else
                icon = ICON_079;
            break;
        case 0x40000:
            if (pressed)
                icon = ICON_078;
            else
                icon = ICON_079;
            break;
        case 0x80000:
            if (pressed)
                icon = ICON_07A;
            else
                icon = ICON_079;
            break;
        case 0x90000:
            if (pressed)
                icon = ICON_07D;
            else
                icon = ICON_079;
            break;
        case 0xA0000:
            if (pressed)
                icon = ICON_077;
            else
                icon = ICON_079;
            break;
        case 0x50000:
            if (pressed)
                icon = ICON_07B;
            else
                icon = ICON_079;
            break;
        case 0x60000:
            if (pressed)
                icon = ICON_075;
            else
                icon = ICON_079;
            break;
    }
    return icon;
}

/** The gauge's full-scale value, 100. */
static inline s32 gaugeMax(void) {
    return 100;
}

/** Draws one flat-coloured rectangle of the action-command gauge in screen space; skips it when it has no width. */
static inline void gaugeQuad(CameraEntry* cam, f32 x0, f32 y0, f32 x1, f32 y1, u8 r, u8 g, u8 b, u8 a) {
    f32 ty1, tx1, ty0, tx0;
    tx0 = x0;
    ty0 = y0;
    tx1 = x1;
    ty1 = y1;
    if (tx1 >= tx0) {
        btlDispGXInit2DRasta();
        GXLoadPosMtxImm(cam->view, 0);
        btlDispGXQuads2DRasta(tx0, ty0, tx1, ty1, r, g, b, a);
    }
}

// BattleAcDrawGauge, BattleAcGaugeSeInit, BattleAcGaugeSeUpdate and 1 more size-optimised, as in the original (the object differs without it)
#pragma optimize_for_size on
/**
 * Draws the action-command power gauge at (x, y), width pixels wide,
 * filled to ratio (0-1; below 0 draws only the frame).
 * From level 2 the background is split into coloured bands at thresholds a, b and c (percent).
 * flags 1 tints the fill with the command's display colour, flags 2 draws it red.
 * Used by the air gauge and timing gauge commands.
 */
void PORT_HOOKABLE(BattleAcDrawGauge)(s32 x, s32 y, s32 width, f32 ratio, s32 level, s32 a, s32 b, s32 c, s32 flags) {
    BattleWork* wp = _battleWorkPointer;
    s32 max = 100;
    u8* mod = (u8*)&wp->actionCommands.disp.color;
    Vec pos;
    GXColor color = {0x00, 0xE4, 0x86, 0xFF};
    CameraEntry* cam;
    f32 r;

    cam = camGetPtr(CAMERA_2D);
    pos = (Vec){0.0f, 0.0f, 0.0f};
    pos.x = x - 200;
    pos.y = y + 25;
    iconDispGx(&pos, ICON_DISP_2D, ICON_08C, 1.0f);

    if (level >= 2) {
        gaugeQuad(cam, x - 289, y + 50, x - 289 + width * a / max, y + 32, 0x21, 0x21, 0x75, 0xFF);
        if (b - a > 0) {
            gaugeQuad(cam, x - 289 + width * a / max, y + 50, x - 289 + width * b / max, y + 32, 0x1D, 0x23, 0xA3, 0xFF);
        }
        if (c - b > 0) {
            gaugeQuad(cam, x - 289 + width * b / max, y + 50, x - 289 + width * c / max, y + 32, 0x46, 0x0C, 0xB4, 0xFF);
        }
        if (100 - c > 0) {
            gaugeQuad(cam, x - 289 + width * c / max, y + 50, x - 289 + width * 100 / max, y + 32, 0x73, 0x0D, 0x13, 0xFF);
        }
    }

    if (level >= 2) {
        btlDispGXInit2DRasta();
        GXLoadPosMtxImm(cam->view, 0);
        btlDispGXQuads2DRasta(x - 289, y + 50, x - 289, y + 32, 0x2D, 0x38, 0xD2, 0xFF);
        if (a > 0) {
            {
                f32 tx0, ty0, tx1, ty1;
                tx1 = x - 289 + width * a / max;
                ty1 = y + 32;
                ty0 = y + 50;
                tx0 = tx1 - 2.0f;
                if (tx1 >= tx0) {
                    btlDispGXInit2DRasta();
                    GXLoadPosMtxImm(cam->view, 0);
                    btlDispGXQuads2DRasta(tx0, ty0, tx1, ty1, 0x54, 0x28, 0xD1, 0xFF);
                }
            }
        }
        if (b - a > 0) {
            {
                f32 tx0, ty0, tx1, ty1;
                tx1 = x - 289 + width * b / max;
                ty1 = y + 32;
                ty0 = y + 50;
                tx0 = tx1 - 2.0f;
                if (tx1 >= tx0) {
                    btlDispGXInit2DRasta();
                    GXLoadPosMtxImm(cam->view, 0);
                    btlDispGXQuads2DRasta(tx0, ty0, tx1, ty1, 0x7D, 0x2C, 0xB5, 0xFF);
                }
            }
        }
        if (c - b > 0) {
            {
                f32 tx0, ty0, tx1, ty1;
                tx1 = x - 289 + width * c / max;
                ty1 = y + 32;
                ty0 = y + 50;
                tx0 = tx1 - 2.0f;
                if (tx1 >= tx0) {
                    btlDispGXInit2DRasta();
                    GXLoadPosMtxImm(cam->view, 0);
                    btlDispGXQuads2DRasta(tx0, ty0, tx1, ty1, 0xA1, 0x1B, 0x55, 0xFF);
                }
            }
        }
    }

    if (ratio < 0.0f) {
        return;
    }

    if (flags & 3) {
        if (flags & 1) {
            color.r = color.r * mod[0] / 255;
            color.g = color.g * mod[1] / 255;
            color.b = color.b * mod[2] / 255;
            color.a = color.a * mod[3] / 255;
        } else {
            color = (GXColor){0xFF, 0x00, 0x00, 0xFF};
        }
        if (ratio > 0.0f) {
            f32 tx0 = x - 289, ty0 = y + 50, tx1 = (f32)(x - 289) + (f32)width * ratio, ty1 = y + 32;
            if (tx1 >= tx0) {
                btlDispGXInit2DRasta();
                GXLoadPosMtxImm(cam->view, 0);
                btlDispGXQuads2DRasta(tx0, ty0, tx1, ty1, color.r, color.g, color.b, color.a);
            }
        }
        return;
    }

    if (ratio <= 0.0f) {
        return;
    }
    ratio = 100.0f * ratio;

    r = ratio;
    if (ratio > a) {
        r = a;
    }
    if (r > 0.0f) {
        gaugeQuad(cam, x - 289, y + 50, (f32)(x - 289) + width * r / 100.0f, y + 32, 0x00, 0xE4, 0x86, 0xFF);
    }

    r = ratio;
    if (ratio > b) {
        r = b;
    }
    if (r - a > 0.0f) {
        gaugeQuad(cam, x - 289 + width * a / max, y + 50, (f32)(x - 289) + width * r / 100.0f, y + 32, 0x2E, 0xB4, 0xF2, 0xFF);
    }

    r = ratio;
    if (ratio > c) {
        r = c;
    }
    if (r - b > 0.0f) {
        gaugeQuad(cam, x - 289 + width * b / max, y + 50, (f32)(x - 289) + width * r / 100.0f, y + 32, 0x75, 0x70, 0xFF, 0xFF);
    }

    if (ratio > 100.0f) {
        ratio = 100.0f;
    }
    if (ratio - c > 0.0f) {
        gaugeQuad(cam, x - 289 + width * c / max, y + 50, (f32)(x - 289) + width * ratio / 100.0f, y + 32, 0xF3, 0x04, 0xBC, 0xFF);
    }
}

/** Starts the rising "power gauge" sound that accompanies a filling action-command gauge. */
void PORT_HOOKABLE(BattleAcGaugeSeInit)(void) {
    BattleWork* wp = _battleWorkPointer;

    AC_SE_ID(wp) = psndSFXOn("SFX_AC_POWER_GAUGE1");
    AC_SE_VAL(wp) = 0.0f;
}

/** Sets the gauge sound's pitch from value (0-100, clamped). */
void PORT_HOOKABLE(BattleAcGaugeSeUpdate)(f32 value) {
    BattleWork* wp = _battleWorkPointer;

    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 100.0f) {
        value = 100.0f;
    }
    AC_SE_VAL(wp) = value;
    if (AC_SE_ID(wp) != -1) {
        psndSFX_pit(AC_SE_ID(wp), 0.01f * (4092.0f * AC_SE_VAL(wp)));
    }
}

/** Stops the gauge sound, if playing. */
void PORT_HOOKABLE(BattleAcGaugeSeDelete)(void) {
    BattleWork* wp = _battleWorkPointer;

    if (AC_SE_ID(wp) != -1) {
        psndSFXOff(AC_SE_ID(wp));
        AC_SE_ID(wp) = -1;
    }
}
#pragma optimize_for_size reset

#ifdef PORT_MODS
#include "port/hooks/src_battle_battle_ac.inc"
#endif
