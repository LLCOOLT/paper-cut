/* Chapter 1's boss, the walking statue of Rumblebump Volcano
 * (docs/SUPER_LUIGI.md, "Chapter 1"), and the volcano's lava.
 *
 * The statue is a battle unit of ours, put in place of the field encounter's
 * enemies (a host hook on Btl_UnitSetup) when its field scene arms it. Two
 * parts: the stone body (defense 1: Luigi's blows do less) and the red gem on
 * its forehead, which holds no armour but sits where no jump or hammer
 * reaches. Blooey's Toss reaches it, and only while it shows: the statue
 * keeps the gem under its arm, and after a turn of stomping it stops to rest
 * and the gem is open for one turn. A solid blow on the stone (a command
 * landed) staggers it and opens the gem at once, for Blooey to follow up. At
 * a third of its HP it stomps twice a turn.
 *
 * Its model: c_lp_statue, the friends' animation work (docs/LUIGI_ANIMS.md,
 * "The stone statue"): the stomp A_1A/A_1B, resting with the gem aglow A_3A,
 * hurt D_1, the gem struck D_2, cracked K_1, shattering D_3. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define kStatueModel "c_lp_statue"

#define GEM_PART 2
#define UW_GEM_SHUT 0  /* 1 while the arm covers the gem */
#define UW_ANGRY 1     /* 1 from a third of its HP: two stomps a turn */
#define UW_TURN 2      /* the statue's turns so far */
#define UW_STRUCK 3    /* the last hit: 0 nothing more, 1 it staggered, 2 the gem was struck */
#define UW_CHIPS 4     /* hits on the stone since its last turn (a full jump's two stomps stagger it) */
#define UW_TOLD 5      /* Blooey's stagger hint given */

static int sArmed;

int portSlBossBattle(void) {
    return sArmed && gp != NULL && gp->inBattle;
}

const BattleWeapon* portSlBlooeyToss(void); /* superluigi_blooey_aurora.c */

/* in: the statue's HP, and 1 while the gem shows: the finishing blow, when
 * one Toss can end it, is the only move Blooey is offered, with the prompt */
static USER_FUNC(statue_finish) {
    const s32 hp = evtGetValue(event, event->args[0]);
    const s32 open = evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    if (open && hp <= 4) {
        portSlForceCommand(1, PORT_SL_ACT_PARTNER, portSlBlooeyToss(), "sl_bt_finish");
    } else {
        portSlForceCommand(1, -1, NULL, NULL);
    }
    return EVT_RETURN_DONE;
}

/* The damage event's first word: what the hit did (UW_STRUCK). A Toss hit
 * the gem (the gate lets it reach nothing else); a blow on the stone that
 * still did damage after its defense (a hammer with its command, a Power
 * Smash, a Fire Flower), or the second stomp of a jump (both landed: the
 * command), staggers it, while the gem is covered */
static USER_FUNC(statue_struck) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u = BattleGetUnitPtr(wp, BattleTransID(event, BTL_UNIT_SELF));
    s32 r = 0;
    (void)isFirstCall;
    if (u != NULL) {
        if (wp->weaponTargets.weapon == portSlBlooeyToss()) {
            r = 2;
        } else if (u->work[UW_GEM_SHUT] == 1 && u->currentHp > 0) {
            u->work[UW_CHIPS]++;
            if (u->hpDamageTaken > 0 || u->work[UW_CHIPS] >= 2) {
                r = 1;
            }
        }
        u->work[UW_STRUCK] = r;
        if (getenv("TTYD_SL_DEBUG") != NULL) {
            printf("[superluigi] statue struck: %d damage, %d hits, -> %d (hp %d)\n", u->hpDamageTaken,
                   u->work[UW_CHIPS], r, u->currentHp);
            fflush(stdout);
        }
    }
    return EVT_RETURN_DONE;
}

/* its pose at rest, unless a status has its own (asleep, dizzy, frozen):
 * cracked and wobbling at a third of its HP, else resting with the gem aglow
 * while the gem shows, else standing guard */
static USER_FUNC(statue_pose) {
    BattleUnit* u = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
    BattleUnitPart* part;
    (void)isFirstCall;
    if (u == NULL || u->currentHp <= 0 || BtlUnit_CheckStatus(u, STATUS_SLEEP) || BtlUnit_CheckStatus(u, STATUS_DIZZY) ||
        BtlUnit_CheckStatus(u, STATUS_FREEZE) || BtlUnit_CheckStatus(u, STATUS_STOP)) {
        return EVT_RETURN_DONE;
    }
    part = BattleGetUnitPartsPtr(u->unitId, 1);
    if (part != NULL) {
        BtlUnit_SetAnim(part, u->currentHp * 3 <= u->maxHp ? "K_1" : u->work[UW_GEM_SHUT] == 0 ? "A_3A" : "S_1");
    }
    return EVT_RETURN_DONE;
}

/* The statue's poses: stand, walk, the stomp (rising, slamming), resting
 * with the gem shown, guarding it, hurt, crumbling, talk */
static PoseTableEntry kStatuePoses[] = {
    {1, "N_1"},  {2, "D_5"},  {9, "D_5"},  {3, "X_1"},  {5, "X_1"},  {4, "K_1"},  {27, "D_3"},
    {28, "S_1"}, {29, "S_1"}, {30, "S_1"}, {31, "S_1"}, {39, "D_1"},
    {50, "A_1A"}, {51, "A_1B"}, {42, "R_1"}, {41, "W_1"}, {40, "W_1"},
    {56, "A_3A"}, {57, "S_1"}, {BTL_POSE_NORMAL, "S_1"}, {BTL_POSE_DAMAGE, "D_1"}, {BTL_POSE_WALK, "W_1"},
    {BTL_POSE_RUN, "R_1"}, {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};

/* the stone: hard (Luigi's blows do 1 less), and a solid one staggers it (statue_struck) */
static BattleUnitDefense kStone = {{1, 1, 1, 1, 1}};
static BattleUnitDefense kGem = {{0, 0, 0, 0, 0}};
static BattleUnitDefenseAttr kNoAttr = {{0, 0, 0, 0, 0}};
static StatusVulnerability kRegist = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

extern EvtWord statue_init[], statue_attack[], statue_damage[], statue_wait[];

static BattleUnitKindPart kParts[] = {
    {1, "sl_un_statue", kStatueModel, 0.0f, 0.0f, 0.0f, 0.0f, 30.0f, 0.0f, 0.0f, 40.0f, 0.0f, 20, 30, 255, &kStone,
     &kNoAttr, PART_ATTR_MAIN | PART_ATTR_UNTATTLEABLE, 0, kStatuePoses},
    {GEM_PART, "sl_un_gem", kStatueModel, 0.0f, 0.0f, 0.0f, -3.0f, 52.0f, 6.0f, -3.0f, 60.0f, 6.0f, 20, 30, 255, &kGem,
     &kNoAttr,
     PART_ATTR_HIDDEN | PART_ATTR_NO_SHADOW | PART_ATTR_JUMPLIKE_CANNOT_TARGET | PART_ATTR_HAMMERLIKE_CANNOT_TARGET |
         PART_ATTR_SHELL_TOSSLIKE_CANNOT_TARGET | PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE,
     0, kStatuePoses},
};

static BattleDataEntry kData[] = {
    {48, btldefaultevt_Dummy},
    {0, NULL},
};

static BattleUnitKind kStatue = {
    1, "sl_un_statue", 20, 0, 5, 2, 60, 0, 10, 100, 0, 0, 9999, 60, 70, 0, 70,
    0.0f, 50.0f, 0.0f, 0, 10, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 50.0f, 0.0f, 0.0f, 50.0f, 50.0f, 0.0f, 0.0f, 50.0f, 0.0f, 100.0f, 110.0f,
    0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f,
    "SFX_BTL_DAMAGE_PRESS1", "SFX_BTL_DAMAGE_FIRE1", "SFX_BTL_DAMAGE_ICE1", "SFX_BTL_DAMAGE_BIRIBIRI1",
    0, &kRegist, 2, kParts, statue_init, kData,
};

static BattleUnitSetup kStatueSetup[] = {
    {&kStatue, 1, {0}, BATTLE_PHASE_ENEMY, {80.0f, 0.0f, 10.0f}, 0, {0, 0, 0, 0}, NULL},
};
static BattleGroupSetup kStatueGroup = {1, kStatueSetup, 0, 0, 100, battle_heart_drop_param_default,
                                        battle_flower_drop_param_default, 0};

/* the stomp: 3; once it's angry, two of 2 a turn */
static BattleWeapon kStomp = {
    NULL, 0, {0}, 0, NULL, 100, 0, 0, 1, 1.0f, 1, 1, 1, 1, weaponGetPowerDefault, {3, 0, 0, 0, 0, 0, 0, 0}, NULL, {0},
    TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE | TARGET_CLASS_NO_SELF |
        TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_SINGLE,
    TARGET_PROP_JUMPLIKE | TARGET_PROP_START_OPPOSING_SIDE, ELEMENT_NORMAL, 0, 3, 2, NULL,
    SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_SHATTERS_FREEZE, 0,
    TARGET_WEIGHT_PREFER_FRONT | TARGET_WEIGHT_RANDOM,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}, NULL, {0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0},
};
static BattleWeapon kStompAngry;

EVT_BEGIN(statue_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(statue_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(statue_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(statue_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(statue_attack))
    CALL(btlevtcmd_SetWalkSound, BTL_UNIT_SELF, STRING("SFX_BOSS_MBTN_STEPON1"), STRING("SFX_BOSS_MBTN_STEPON1"), 0, 20, 20)
    CALL(btlevtcmd_SetRunSound, BTL_UNIT_SELF, STRING("SFX_BOSS_MBTN_STEPON1"), STRING("SFX_BOSS_MBTN_STEPON1"), 0, 20, 20)
    CALL(btlevtcmd_SetJumpSound, BTL_UNIT_SELF, STRING("SFX_ENM_KURI_JUMP1"), STRING("SFX_BOSS_MBTN_STEPON1"))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_GEM_SHUT, 1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_ANGRY, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TURN, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STRUCK, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_CHIPS, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TOLD, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

EVT_BEGIN(statue_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    CALL(statue_pose)
    RETURN
EVT_END()

/* A hit: the default hurt (a jolt, the number), then what it did to the
 * statue: the gem struck reels it back and cracks the gem; a solid blow on
 * the stone staggers it, and its arm drops from the gem until its turn */
EVT_BEGIN(statue_damage)
    CALL(statue_struck)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(10))
    IF_INT_LESS_OR_EQUAL(LW(10), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STRUCK, LW(0))
    IF_INT_EQUAL(LW(0), 2)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_KUPPA_STONE_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("D_2"))
        WAIT_FRAMES(50)
        CALL(statue_finish, LW(10), 1)
    END_IF
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_btl_camera_shake_h, 1, 4, 0, 20, 13)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_MBTN_STEPON1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("X_1"))
        WAIT_FRAMES(40)
        CALL(btlevtcmd_OffPartsAttribute, BTL_UNIT_SELF, GEM_PART, PART_ATTR_NOT_TARGETABLE)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_GEM_SHUT, 0)
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 56)
        CALL(evt_msg_print, 0, STRING("sl_bt_stagger"), 0, 0)
        CALL(statue_finish, LW(10), 1)
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TOLD, LW(1))
        IF_INT_EQUAL(LW(1), 0)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TOLD, 1)
            /* no friend out: no line (the unit lookup would crash) */
            CALL(btlevtcmd_GetUnitId, BTL_UNIT_PARTNER, LW(0))
            IF_INT_NOT_EQUAL(LW(0), -1)
                CALL(evt_msg_print, 2, STRING("sl_bt_stagger_hint"), 0, BTL_UNIT_PARTNER)
            END_IF
        END_IF
    END_IF
    CALL(statue_pose)
    RETURN
EVT_END()

/* One stomp at a target: it strides up, rears and slams down. LW(12): the weapon. */
static EVT_BEGIN(statue_stomp)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 150)
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    /* it strides up: each step shakes the room */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 40)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 70)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(3.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 1)
    CALL(evt_btl_camera_shake_h, 0, 2, 0, 10, 13)
    /* it rears up... */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 50)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    ADD(LW(6), 40)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(5), LW(6), LW(7), 24, -1)
    WAIT_MS(250)
    /* ...and slams down */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 51)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 30)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 12, -1)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    CALL(evt_btl_camera_shake_h, 1, 8, 0, 20, 13)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_MBTN_STEPON1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    SWITCH(LW(5))
        CASE_EQUAL(3)
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
        CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
        CALL(btlevtcmd_StartAvoid, LW(3), 40)
        CASE_EQUAL(4)
        CASE_DEFAULT
        CALL(btlevtcmd_ResultACDefence, LW(3), LW(12))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    END_SWITCH
    WAIT_MS(300)
    /* back to its place */
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 41)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -2, 0)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* Its turn: on a resting turn it stomps once and stops to rest, the gem open
 * until its next turn; on a guarding turn the arm covers the gem and it
 * stomps (twice once angry). */
EVT_BEGIN(statue_attack)
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(10))
    CALL(btlevtcmd_GetMaxHp, BTL_UNIT_SELF, LW(11))
    MULTIPLY(LW(10), 3)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_ANGRY, LW(13))
    IF_INT_LESS_OR_EQUAL(LW(10), LW(11))
        IF_INT_EQUAL(LW(13), 0)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_ANGRY, 1)
            SET_VALUE(LW(13), 1)
            CALL(evt_btl_camera_shake_h, 1, 4, 0, 30, 13)
            CALL(evt_msg_print, 0, STRING("sl_bt_angry"), 0, 0)
        END_IF
    END_IF
    IF_INT_EQUAL(LW(13), 0)
        SET_VALUE(LW(12), PTR(&kStomp))
    ELSE
        SET_VALUE(LW(12), PTR(&kStompAngry))
    END_IF
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_CHIPS, 0)
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURN, 1)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURN, LW(14))
    MODULO(LW(14), 2)
    IF_INT_EQUAL(LW(14), 1)
        /* a resting turn: the arm comes down if it wasn't, one stomp, then the rest */
        CALL(btlevtcmd_OnPartsAttribute, BTL_UNIT_SELF, GEM_PART, PART_ATTR_NOT_TARGETABLE)
        CALL_CHILD(statue_stomp)
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 56)
        CALL(btlevtcmd_OffPartsAttribute, BTL_UNIT_SELF, GEM_PART, PART_ATTR_NOT_TARGETABLE)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_GEM_SHUT, 0)
        CALL(evt_msg_print, 0, STRING("sl_bt_gem_open"), 0, 0)
        CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(10))
        CALL(statue_finish, LW(10), 1)
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURN, LW(14))
        IF_INT_EQUAL(LW(14), 1)
            /* no friend out: no line (the unit lookup would crash) */
            CALL(btlevtcmd_GetUnitId, BTL_UNIT_PARTNER, LW(0))
            IF_INT_NOT_EQUAL(LW(0), -1)
                CALL(evt_msg_print, 2, STRING("sl_bt_hint"), 0, BTL_UNIT_PARTNER)
            END_IF
        END_IF
    ELSE
        /* a guarding turn */
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 57)
        CALL(btlevtcmd_OnPartsAttribute, BTL_UNIT_SELF, GEM_PART, PART_ATTR_NOT_TARGETABLE)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_GEM_SHUT, 1)
        CALL(statue_finish, 0, 0)
        CALL(evt_msg_print, 0, STRING("sl_bt_gem_shut"), 0, 0)
        CALL_CHILD(statue_stomp)
        IF_INT_EQUAL(LW(13), 1)
            CALL_CHILD(statue_stomp)
        END_IF
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 57)
    END_IF
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- arming the encounter --- */

/* in: 1 the next field battle is the statue's, 0 off */
USER_FUNC(evt_sl_boss_arm) {
    (void)isFirstCall;
    sArmed = evtGetValue(event, event->args[0]);
    return EVT_RETURN_DONE;
}

/* Btl_UnitSetup(wp): the encounter's group is the statue's */
static int unitSetupPre(BOOL* result, BattleWork** wp) {
    (void)result;
    if (sArmed && (*wp)->fbatInfo != NULL && (*wp)->fbatInfo->setup != NULL) {
        kStompAngry = kStomp;
        kStompAngry.hpDamageWork[0] = 2; /* angry: two stomps a turn, not harder ones */
        (*wp)->fbatInfo->setup->groupSetup = &kStatueGroup;
        (*wp)->fbatInfo->setup->bgmName = "BGM_BOSS_STG5_KORTESS1";
        printf("[superluigi] the statue's fight\n");
        fflush(stdout);
    }
    return 0;
}

/* --- the volcano's lava --- */

static struct {
    int on;
    f32 x0, z0, x1, z1, top;
    f32 sx, sy, sz;
} sLava;
static int sBurning;
static char sLavaMap[16];
int portSlRiding(void); /* superluigi_blooey_aurora.c */

/* in: the lava's x and z range, the height under which it burns, and where Luigi lands back (x, y, z) */
USER_FUNC(evt_sl_lava_zone) {
    EvtWord* a = event->args;
    (void)isFirstCall;
    sLava.on = 1;
    snprintf(sLavaMap, sizeof(sLavaMap), "%s", gp->currentMapName);
    sLava.x0 = (f32)evtGetValue(event, a[0]);
    sLava.z0 = (f32)evtGetValue(event, a[1]);
    sLava.x1 = (f32)evtGetValue(event, a[2]);
    sLava.z1 = (f32)evtGetValue(event, a[3]);
    sLava.top = (f32)evtGetValue(event, a[4]);
    sLava.sx = (f32)evtGetValue(event, a[5]);
    sLava.sy = (f32)evtGetValue(event, a[6]);
    sLava.sz = (f32)evtGetValue(event, a[7]);
    return EVT_RETURN_DONE;
}

static USER_FUNC(lava_hurt) {
    PouchData* p = pouchGetPtr();
    (void)isFirstCall;
    evtSetValue(event, event->args[0], (s32)sLava.sx);
    evtSetValue(event, event->args[1], (s32)sLava.sy);
    evtSetValue(event, event->args[2], (s32)sLava.sz);
    if (p->currentHP > 1) {
        p->currentHP--;
    }
    return EVT_RETURN_DONE;
}

static USER_FUNC(lava_done) {
    (void)event;
    (void)isFirstCall;
    sBurning = 0;
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(lava_burn)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_sfxon, STRING("SFX_MARIO_DAMAGE_FIRE1"), 0)
    CALL(evt_mario_set_pose, STRING("M_D_2"))
    CALL(evt_mario_get_pos, 0, LW(3), LW(4), LW(5))
    ADD(LW(4), 60)
    CALL(evt_mario_jump_pos, LW(3), LW(4), LW(5), 300, 1, 40)
    CALL(lava_hurt, LW(0), LW(1), LW(2))
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), 500, 0, 60)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_msg_print, 0, STRING("sl_lava_hot"), 0, STRING("mario"))
    CALL(evt_mario_key_onoff, 1)
    CALL(lava_done)
    RETURN
EVT_END()

static void lavaFrame(void) {
    MarioWork* m;
    if (gp == NULL || !sLava.on) {
        return;
    }
    if (strcmp(sLavaMap, gp->currentMapName) != 0) {
        /* a new room: its init sets its own lava */
        if (sLavaMap[0] != '\0') {
            sLava.on = 0;
        }
        snprintf(sLavaMap, sizeof(sLavaMap), "%s", gp->currentMapName);
        return;
    }
    m = marioGetPtr();
    if (sBurning || gp->inBattle || portSlRiding() || !marioChkKey()) {
        return;
    }
    if (m->position.x > sLava.x0 && m->position.x < sLava.x1 && m->position.z > sLava.z0 && m->position.z < sLava.z1 &&
        m->position.y < sLava.top) {
        sBurning = 1;
        evtEntry(lava_burn, 0, 0);
    }
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

const BattleWeapon* portSlBlooeyToss(void); /* superluigi_blooey_aurora.c */

/* chapter 1's setup (superluigi_ch01_aurora.c): the encounter's hook, the
 * lava, and the gem's gate: only Blooey Toss reaches it, only while it shows,
 * and the Toss reaches nothing else of the statue */
void portSlStatueRegister(void) {
    static PortSlTargetGate gate;
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    portModHostAddHook(1, kSlot_marioStMain, (void*)lavaFrame);
    gate.kind = &kStatue;
    gate.part = GEM_PART;
    gate.weapons[0] = portSlBlooeyToss();
    gate.openWork = UW_GEM_SHUT;
    gate.openValue = 0;
    gate.only = 1;
    portSlAddTargetGate(&gate);
}
