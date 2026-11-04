/* Super Luigi, chapter 3: the Stadium (tou_l33, on tou_03 with the tou2
 * REL), the Bumper Bouts and the King's Bout (docs/super_luigi/ch3.md,
 * "Battles").
 *
 * Gasket's desk (superluigi_ch03_aurora.c) sends Luigi to the ring with the
 * next bout; here the walk in, Lakky's intro, the gong, the battle and what
 * follows. Each bout is a Glitz Pit match of the tou2 REL's own table (its
 * stage and audience), with its group made up at Btl_UnitSetup from the
 * REL's units: the Lug Nuts (Bob-omb x3), the Spike Strip, the Shell
 * Shockers, the Pokey Pistons, the exhibitions, and King Revvo with Lug
 * (Hammer Bro) and Nut (Boomerang Bro).
 *
 * King Revvo is the Dark Koopatrol's unit (its model, flip, defence and hit
 * reactions) with our numbers and his own turns: Royal Rev (a spin along
 * the front, 4), the Crown Bazooka (loaded one turn, fired the next: 3 on
 * Luigi and the partner; Torque's Spike Guard sends it back), and at 10 HP
 * or less the Victory Lap (a turn revving up, cancelled by any hit, then
 * two rams of 4). Flipped (Shell Slide, POW, Big Bang) he's DEF 0 and spends
 * his turn getting up (22 HP). Lug repairs him (Pit Stop, 4 HP, every other turn)
 * while he's under 15; Nut's boomerang hits twice, as the Bro's own. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/superluigi_ch03.h"
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
#include "data/item_data.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include "driver/swdrv.h"

#include <stdio.h>
#include <string.h>

#define M(tag, who) CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who))
#define SYS(tag) CALL(evt_msg_print, 0, STRING(tag), 0, 0)

/* ======================================================================
 * The bouts' groups (Btl_UnitSetup)
 * ====================================================================== */

/* bout: 1-4 the license bouts, 5 the King's, 6-8 the exhibitions; its tou2 battle */
static const int kBoutBattle[9] = {0, 8, 5, 13, 3, 19, 10, 11, 9};
static int sBout; /* the bout being fought (0: none) */

static BattleUnitKind sRevvo, sLug;
static BattleUnitKindPart sRevvoParts[1], sLugParts[1];
static EvtWord* sKoopatrolInit;
static EvtWord* sBroInit;
static BattleUnitSetup sKingUnits[3];
static BattleGroupSetup sKingGroup;
static BattleUnitSetup sBombUnits[3];
static BattleGroupSetup sBombGroup;

extern EvtWord revvo_init[], lug_init[];

static NpcSetupBattleInfo* touBattle(int index) {
    AreaData* area = areaDataPtr("tou");
    return area != NULL && area->btlSetupTbl != NULL ? &((NpcSetupBattleInfo*)area->btlSetupTbl)[index] : NULL;
}

static BattleGroupSetup* touGroup(int index) {
    NpcSetupBattleInfo* b = touBattle(index);
    return b != NULL && b->select != NULL ? b->select[0].groupSetup : NULL;
}

/* King Revvo's group: Revvo up front, Lug and Nut behind him */
static BattleGroupSetup* kingGroup(void) {
    BattleGroupSetup* koop = touGroup(19);
    BattleGroupSetup* bros = touGroup(17);
    BattleUnitKind *hammer = NULL, *boomer = NULL;
    int i;
    if (koop == NULL || bros == NULL) {
        return NULL;
    }
    for (i = 0; i < bros->numEnemies; i++) {
        BattleUnitKind* k = bros->enemyData[i].unitKindParams;
        if (k->unitType == UNIT_HAMMER_BRO) {
            hammer = k;
        } else if (k->unitType == UNIT_BOOMERANG_BRO) {
            boomer = k;
        }
    }
    if (hammer == NULL || boomer == NULL) {
        return NULL;
    }
    /* Revvo: the Koopatrol's unit, his numbers, his own turns */
    sRevvo = *koop->enemyData[0].unitKindParams;
    sKoopatrolInit = sRevvo.initEventCode;
    sRevvoParts[0] = sRevvo.parts[0];
    sRevvoParts[0].partName = "sl_un_revvo";
    sRevvo.parts = sRevvoParts;
    sRevvo.name = "sl_un_revvo";
    sRevvo.maxHp = 22; /* 30 in the plan: Luigi comes with the plain Hammer, half the damage Mario has at the Glitz Pit */
    sRevvo.level = 40;
    sRevvo.initEventCode = revvo_init;
    /* Lug: the Hammer Bro, with a Pit Stop */
    sLug = *hammer;
    if (sLug.numParts == 1) {
        sLugParts[0] = sLug.parts[0];
        sLugParts[0].partName = "sl_un_lug";
        sLug.parts = sLugParts;
    }
    sBroInit = sLug.initEventCode;
    sLug.name = "sl_un_lug";
    sLug.initEventCode = lug_init;
    memset(sKingUnits, 0, sizeof(sKingUnits));
    sKingUnits[0] = koop->enemyData[0];
    sKingUnits[0].unitKindParams = &sRevvo;
    sKingUnits[0].position = (Vec){45.0f, 0.0f, 10.0f};
    sKingUnits[1] = bros->enemyData[0];
    sKingUnits[1].unitKindParams = &sLug;
    sKingUnits[1].position = (Vec){100.0f, 0.0f, -15.0f};
    sKingUnits[2] = bros->enemyData[0];
    sKingUnits[2].unitKindParams = boomer;
    sKingUnits[2].position = (Vec){150.0f, 0.0f, 15.0f};
    sKingGroup = *koop;
    sKingGroup.numEnemies = 3;
    sKingGroup.enemyData = sKingUnits;
    return &sKingGroup;
}

static int unitSetupPre(BOOL* result, BattleWork** wp) {
    NpcBattleInfo* info;
    (void)result;
    if (sBout == 0 || !portSuperLuigiActive() || portSuperLuigiChapter() != 3 || (*wp)->fbatInfo == NULL || (info = (*wp)->fbatInfo->setup) == NULL) {
        return 0;
    }
    if (sBout == 1 && info->groupSetup != NULL) {
        /* the Lug Nuts: three of the squad's four */
        sBombGroup = *info->groupSetup;
        memcpy(sBombUnits, sBombGroup.enemyData, sizeof(sBombUnits));
        sBombGroup.numEnemies = 3;
        sBombGroup.enemyData = sBombUnits;
        info->groupSetup = &sBombGroup;
    } else if (sBout == 5) {
        BattleGroupSetup* g = kingGroup();
        if (g != NULL) {
            info->groupSetup = g;
        }
    }
    info->bgmName = sBout == 5 ? "BGM_BOSS_STG3_CHAMP1" : sBout >= 4 ? "BGM_KOBOSS_BATTLE1" : "BGM_ZAKO_BATTLE1";
    printf("[sl3] bout %d\n", sBout);
    fflush(stdout);
    return 0;
}

/* in: the bout (0 none) */
void portTorqueSlidePierce(int on); /* superluigi_torque_aurora.c */
static USER_FUNC(sl3_bout_arm) {
    (void)isFirstCall;
    sBout = (int)evtGetValue(event, event->args[0]);
    portTorqueSlidePierce(sBout == 7);
    return EVT_RETURN_DONE;
}

/* in: the bout; out: its battle's index in tou2's table */
static USER_FUNC(sl3_bout_battle) {
    const int b = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], b > 0 && b < 9 ? kBoutBattle[b] : 0);
    return EVT_RETURN_DONE;
}

/* ======================================================================
 * King Revvo and Lug
 * ====================================================================== */

#define UW_TURN 5
#define UW_BAZOOKA 6 /* 1: loaded */
#define UW_LAP 7     /* 0 not yet, 1 revving, 2 done */
#define UW_PIT 8     /* Lug's turns till the next Pit Stop */

static BattleWeapon kRev, kRocket;
static EvtWord* sBroAttack;
static BattleUnitDefense* sRevvoDef;
static BattleUnitDefenseAttr* sRevvoDefAttr;
static PoseTableEntry* sRevvoPoses;
static BattleDataEntry* sRevvoData;

static void makeWeapons(void) {
    memset(&kRev, 0, sizeof(kRev));
    kRev.baseAccuracy = 100;
    kRev.superguardsAllowed = 1;
    kRev.stylishCommandMultiplier = 1;
    kRev.bingoSlotChance = 100;
    kRev.hpDamageFn = weaponGetPowerDefault;
    kRev.hpDamageWork[0] = 4;
    kRev.targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE |
                            TARGET_CLASS_NO_SELF | TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_SINGLE;
    kRev.targetPropertyFlags = TARGET_PROP_NO_CEILING | TARGET_PROP_FRONTMOST | TARGET_PROP_START_OPPOSING_SIDE;
    kRev.damagePattern = 3;
    kRev.special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_SHATTERS_FREEZE;
    kRev.counterResistFlags = COUNTER_RESIST_TOP_SPIKY;
    kRev.targetWeightFlags = TARGET_WEIGHT_PREFER_FRONT | TARGET_WEIGHT_RANDOM;
    kRev.stageEffectRates[3] = 100;
    /* the rocket: down on both, a falling hit (Spike Guard's spikes meet it) */
    kRocket = kRev;
    kRocket.hpDamageWork[0] = 3;
    kRocket.targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT |
                               TARGET_CLASS_NO_OWN_ALLIANCE | TARGET_CLASS_NO_SELF | TARGET_CLASS_MAIN_PARTS |
                               TARGET_CLASS_MULTIPLE;
    kRocket.targetPropertyFlags = TARGET_PROP_JUMPLIKE | TARGET_PROP_START_OPPOSING_SIDE;
    kRocket.counterResistFlags = 0;
    kRocket.damagePattern = 6;
}

static BattleUnit* unitOf(EventEntry* event, s32 id) {
    return BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, id));
}

/* out: the unit kind's original init, to run first */
static USER_FUNC(sl3_orig_init) {
    BattleUnit* u = unitOf(event, BTL_UNIT_SELF);
    (void)isFirstCall;
    evtSetValue(event, event->args[0],
                (EvtWord)(AddrWord)(u != NULL && u->kindParams == &sLug ? sBroInit : sKoopatrolInit));
    return EVT_RETURN_DONE;
}

/* after the original init: Revvo's tables (for getting up) and Lug's own attack kept */
static USER_FUNC(sl3_after_init) {
    BattleUnit* u = unitOf(event, BTL_UNIT_SELF);
    (void)isFirstCall;
    if (u != NULL && u->kindParams == &sLug) {
        sBroAttack = u->attackEventCode;
    } else if (u != NULL) {
        sRevvoDef = u->parts != NULL ? u->parts->defense : sRevvoParts[0].defense; /* the chapter's stats may have set it */
        sRevvoDefAttr = sRevvoParts[0].defenseAttr;
        sRevvoPoses = sRevvoParts[0].poseTable;
        sRevvoData = sRevvo.dataTable;
    }
    return EVT_RETURN_DONE;
}

/* out: Revvo's upright tables (defence, attr, statuses, poses, data) */
static USER_FUNC(sl3_revvo_tables) {
    EvtWord* a = event->args;
    (void)isFirstCall;
    evtSetValue(event, a[0], (EvtWord)(AddrWord)sRevvoDef);
    evtSetValue(event, a[1], (EvtWord)(AddrWord)sRevvoDefAttr);
    evtSetValue(event, a[2], (EvtWord)(AddrWord)sRevvo.statusVulnerability);
    evtSetValue(event, a[3], (EvtWord)(AddrWord)sRevvoPoses);
    evtSetValue(event, a[4], (EvtWord)(AddrWord)sRevvoData);
    return EVT_RETURN_DONE;
}

/* Revvo's upright poses and data table back (btlevtcmd_AnimeSetPoseTable and
 * btlevtcmd_ChangeDataTable read theirs raw, so an LW there crashes the battle) */
static USER_FUNC(sl3_revvo_poses) {
    BattleUnitPart* part = BattleGetUnitPartsPtr(BattleTransID(event, BTL_UNIT_SELF), 1);
    BattleUnit* u = unitOf(event, BTL_UNIT_SELF);
    (void)isFirstCall;
    if (part != NULL) {
        part->poseTable = sRevvoPoses;
    }
    if (u != NULL) {
        u->dataTable = sRevvoData;
    }
    return EVT_RETURN_DONE;
}

/* out: Lug's own attack */
static USER_FUNC(sl3_bro_attack) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], (EvtWord)(AddrWord)sBroAttack);
    return EVT_RETURN_DONE;
}

/* out: Revvo's unit id (-1 gone) and HP */
static USER_FUNC(sl3_find_revvo) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    (void)isFirstCall;
    evtSetValue(event, event->args[0], -1);
    evtSetValue(event, event->args[1], 0);
    for (i = 0; wp != NULL && i < 64; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u != NULL && u->kindParams == &sRevvo && u->currentHp > 0) {
            evtSetValue(event, event->args[0], u->unitId);
            evtSetValue(event, event->args[1], u->currentHp);
            break;
        }
    }
    return EVT_RETURN_DONE;
}

/* in: a weapon: the shell spins up and runs along the front into the target */
static EVT_BEGIN(royal_rev)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(9))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(9), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(btlevtcmd_WeaponAftereffect, LW(9))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_MOVE1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_SHELL1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_MOVE2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    BEGIN_SIBLING_EVENT_GET_ID(LW(14))
        LOOP(0)
            CALL(btlevtcmd_AddRotate, BTL_UNIT_SELF, 0, 40, 0)
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    WAIT_MS(500)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(12.0f))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, EVT_NULLPTR, LW(5), EVT_NULLPTR)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(5), LW(2), 0, 0, 0)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(3)
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
        CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
        CALL(btlevtcmd_StartAvoid, LW(3), 40)
        CASE_EQUAL(1)
        CALL(btlevtcmd_ResultACDefence, LW(3), LW(9))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
        CASE_DEFAULT
    END_SWITCH
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(12.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, 0, 0)
    DELETE_EVENT(LW(14))
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_SHELL2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(10.0f), FLOAT(0.4f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 20, -1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* the Crown Bazooka fires: a whistle, and the rocket comes down on both */
static EVT_BEGIN(bazooka_fire)
    SET_VALUE(LW(9), PTR(&kRocket))
    CALL(btlevtcmd_WeaponAftereffect, LW(9))
    CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)
    CALL(btlevtcmd_WaitGuardMove)
    CALL(evt_btl_camera_set_mode, 0, 3)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("T_1"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_THUNDERS_BOMB1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 50)
    CALL(evt_eff, 0, STRING("kemuri_test"), 0, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
    WAIT_MS(800)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    LABEL(10)
    IF_INT_NOT_EQUAL(LW(3), -1)
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(evt_eff, 0, STRING("bomb"), 0, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_KUPPA_STONE_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_btl_camera_shake_h, 1, 6, 0, 16, 13)
        IF_INT_EQUAL(LW(5), 1)
            CALL(btlevtcmd_ResultACDefence, LW(3), LW(9))
        END_IF
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(9), DAMAGE_FLAG_LAST_HIT, LW(5))
        WAIT_MS(400)
        CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
        GOTO(10)
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* Revvo's turn */
EVT_BEGIN(revvo_attack)
    /* on his back: he spends the turn getting up */
    CALL(btlevtcmd_GetOverTurnCount, BTL_UNIT_SELF, LW(0))
    IF_INT_GREATER_OR_EQUAL(LW(0), 1)
        SUBTRACT(LW(0), 1)
        CALL(btlevtcmd_SetOverTurnCount, BTL_UNIT_SELF, LW(0))
        IF_INT_EQUAL(LW(0), 0)
            CALL(sl3_revvo_tables, LW(1), LW(2), LW(3), LW(4), LW(5))
            CALL(btlevtcmd_SetPartsDefenceTable, BTL_UNIT_SELF, 1, LW(1))
            CALL(btlevtcmd_SetPartsDefenceAttrTable, BTL_UNIT_SELF, 1, LW(2))
            CALL(btlevtcmd_SetRegistStatus, BTL_UNIT_SELF, LW(3))
            CALL(sl3_revvo_poses)
            CALL(btlevtcmd_OffStatusFlag, BTL_UNIT_SELF, STATUS_FLAG_FLIPPED)
            CALL(btlevtcmd_OnPartsCounterAttribute, BTL_UNIT_SELF, 1, COUNTER_ATTR_TOP_SPIKY)
            CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_INSIDE2"), EVT_NULLPTR, 0, EVT_NULLPTR)
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("S_1"))
            CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(1.0f))
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 12, -1)
            CALL(evt_msg_print, 2, STRING("sl_c3_boss_up"), 0, BTL_UNIT_SELF)
        END_IF
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    /* the Victory Lap: revved last turn, two rams now */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_LAP, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAP, 2)
        SET_VALUE(LW(9), PTR(&kRev))
        CALL_CHILD(royal_rev)
        SET_VALUE(LW(9), PTR(&kRev))
        CALL_CHILD(royal_rev)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(1))
    IF_INT_EQUAL(LW(0), 0)
        IF_INT_LESS_OR_EQUAL(LW(1), 10)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAP, 1)
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("R_1"))
            CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_MOVE3"), EVT_NULLPTR, 0, EVT_NULLPTR)
            CALL(evt_btl_camera_shake_h, 0, 3, 0, 30, 13)
            CALL(evt_msg_print, 0, STRING("sl_c3_boss_revving"), 0, 0)
            CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
            RETURN
        END_IF
    END_IF
    /* the bazooka: loaded last turn, fired now */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_BAZOOKA, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_BAZOOKA, 0)
        CALL_CHILD(bazooka_fire)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURN, 1)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURN, LW(0))
    MODULO(LW(0), 3)
    IF_INT_EQUAL(LW(0), 0)
        /* every third turn: out comes the bazooka */
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_BAZOOKA, 1)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("T_1"))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_CHARGE1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_msg_print, 0, STRING("sl_c3_boss_bazooka"), 0, 0)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    SET_VALUE(LW(9), PTR(&kRev))
    CALL_CHILD(royal_rev)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* hit while revving: he spins out and the Victory Lap is off */
EVT_BEGIN(revvo_damage)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_LAP, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAP, 2)
        BEGIN_SIBLING_EVENT
            WAIT_MS(500)
            CALL(evt_msg_print, 0, STRING("sl_c3_boss_spinout"), 0, 0)
        END_SIBLING_EVENT
    END_IF
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

EVT_BEGIN(revvo_init)
    CALL(sl3_orig_init, LW(0))
    CALL_CHILD(LW(0))
    CALL(sl3_after_init)
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(revvo_attack))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(revvo_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(revvo_damage))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TURN, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_BAZOOKA, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAP, 0)
    RETURN
EVT_END()

/* Lug's turn: a Pit Stop when the King's under 15 (of his 22), else his own hammers */
EVT_BEGIN(lug_attack)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PIT, LW(6))
    CALL(sl3_find_revvo, LW(7), LW(8))
    IF_INT_NOT_EQUAL(LW(7), -1)
        IF_INT_LESS(LW(8), 15)
            IF_INT_LESS_OR_EQUAL(LW(6), 0)
                CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PIT, 2)
                CALL(evt_btl_camera_set_mode, 0, 3)
                CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
                CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
                CALL(btlevtcmd_GetPos, LW(7), LW(0), LW(1), LW(2))
                ADD(LW(0), 25)
                ADD(LW(2), 5)
                CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
                CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_STG7_CRANE_BUTTON_PUSH1"), EVT_NULLPTR, 0, EVT_NULLPTR)
                CALL(btlevtcmd_GetPos, LW(7), LW(0), LW(1), LW(2))
                ADD(LW(1), 40)
                CALL(evt_eff, STRING(""), STRING("recovery"), 4, LW(0), LW(1), LW(2), 6, 0, 0, 0, 0, 0, 0, 0)
                CALL(btlevtcmd_RecoverHp, LW(7), 1, 4)
                CALL(evt_msg_print, 0, STRING("sl_c3_boss_pitstop"), 0, 0)
                CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
                CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
                CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
                CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
                CALL(evt_btl_camera_set_mode, 0, 0)
                CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
                RETURN
            END_IF
        END_IF
    END_IF
    SUBTRACT(LW(6), 1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PIT, LW(6))
    CALL(sl3_bro_attack, LW(0))
    CALL_CHILD(LW(0))
    RETURN
EVT_END()

EVT_BEGIN(lug_init)
    CALL(sl3_orig_init, LW(0))
    CALL_CHILD(LW(0))
    CALL(sl3_after_init)
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(lug_attack))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PIT, 0)
    RETURN
EVT_END()

/* ======================================================================
 * tou_l33, the Stadium
 * ====================================================================== */

static EVT_BEGIN(d33_w)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.38867188f))
    CALL(evt_mapobj_rotate, 1, STRING("aka_door_b"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 1, STRING("aka_door_a"), 0, LW(0), 0)
    RETURN
EVT_END()
static EVT_BEGIN(d33_e)
    MULTIPLY_FLOAT(LW(0), FLOAT(0.38867188f))
    CALL(evt_mapobj_rotate, 1, STRING("ao_ddor_b"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 1, STRING("ao_door_a"), 0, LW(0), 0)
    RETURN
EVT_END()
static EVT_BEGIN(d33_s1)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.38867188f))
    CALL(evt_mapobj_rotate, 1, STRING("door_2_b"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 1, STRING("door_2_a"), 0, LW(0), 0)
    RETURN
EVT_END()
static EVT_BEGIN(d33_s2)
    MULTIPLY_FLOAT(LW(0), FLOAT(-0.38867188f))
    CALL(evt_mapobj_rotate, 1, STRING("door_3_b"), 0, LW(0), 0)
    MULTIPLY_FLOAT(LW(0), FLOAT(-1.0f))
    CALL(evt_mapobj_rotate, 1, STRING("door_3_a"), 0, LW(0), 0)
    RETURN
EVT_END()

static BeroEntry b33[] = {
    {"w_bero", 1, 6, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l34", "n_bero_2", 5, 5, d33_w, d33_w},
    {"e_bero", 1, 6, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l34", "n_bero_1", 5, 5, d33_e, d33_e},
    {"s_bero_1", 1, 6, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l32", "n_bero_1", 5, 5, d33_s1, d33_s1},
    {"s_bero_2", 1, 6, BERO_DIR_AUTO, BERO_POS_AUTO, 0, 0, -1, NULL, -1, NULL, "tou_l32", "n_bero_2", 5, 5, d33_s2, d33_s2},
    {0},
};

/* the bout's foe out in the ring, by bout (field models) */
static const char* const kFoeModel[9] = {"", "c_bomhey", "c_togezo", "c_uranoko", "c_sanbo",
                                         "c_togenoko_b", "c_sinemon", "c_toge", "c_borodo"};
static USER_FUNC(sl3_foe_model) {
    const int b = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)kFoeModel[b > 0 && b < 9 ? b : 1]);
    return EVT_RETURN_DONE;
}
/* out: the intro's message for a bout */
static USER_FUNC(sl3_bout_intro) {
    static const char* const kIntro[9] = {"", "sl_c3_bout1_intro", "sl_c3_bout2_intro", "sl_c3_bout3_intro",
                                          "sl_c3_bout4_intro", "sl_c3_boss_01", "sl_c3_boutE1_intro",
                                          "sl_c3_boutE2_intro", "sl_c3_boutE3_intro"};
    const int b = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)kIntro[b > 0 && b < 9 ? b : 1]);
    return EVT_RETURN_DONE;
}
/* the bout's flag set (won) */
static USER_FUNC(sl3_bout_won) {
    static const int kFlag[9] = {0, F_BOUT1, F_BOUT2, F_BOUT3, F_BOUT4, F_KING, F_EX1, F_EX2, F_EX3};
    const int b = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (b > 0 && b < 9) {
        swSet(SL3_FLAG_BASE + kFlag[b]);
    }
    return EVT_RETURN_DONE;
}
static USER_FUNC(sl3_give) {
    (void)isFirstCall;
    pouchGetItem((ItemType)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}
USER_FUNC(sl3_bout_get);
USER_FUNC(sl3_bout_set);

/* after a bout, won or lost: a ladder of five fought back to back, with no bed but Torque's, ran Luigi out
 * of HP by the second (13 of 25 after Bout 1, carried in) */
static USER_FUNC(sl3_bout_medic) {
    PouchData* p = pouchGetPtr();
    (void)isFirstCall;
    int i;
    p->currentHP = p->maximumHP;
    p->currentFP = p->maximumFP;
    /* and the partner's: Torque knocked out in Bout 4 went into the King's Bout at 0 HP */
    for (i = 0; i < 8; i++) {
        p->partyData[i].currentHP = p->partyData[i].maximumHP;
    }
    return EVT_RETURN_DONE;
}

/* the bout: the walk in, Lakky's intro, the gong, the fight, the verdict */
static EVT_BEGIN(bout_scene)
    /* black until the ring's ready: the room comes up by its front door, and the camera on the shut doors */
    CALL(evt_seq_wait, 2)
    CALL(evt_fade_out, 0)
    WAIT_MS(800)
    CALL(evt_mario_key_onoff, 0)
    CALL(sl3_bout_get, LW(10))
    CALL(sl3_bout_set, 0)
    CALL(evt_mario_set_pos, -60, 60, 0)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_party_set_pos, 0, -100, 60, -10)
    CALL(sl3_foe_model, LW(10), LW(1))
    CALL(evt_npc_entry, STRING("sl_foe"), LW(1))
    CALL(evt_npc_set_position, STRING("sl_foe"), 70, 60, 0)
    CALL(evt_npc_set_ry, STRING("sl_foe"), 270)
    IF_INT_EQUAL(LW(10), 5)
        CALL(evt_npc_entry, STRING("sl_crown"), STRING("c_sl_crown"))
        CALL(evt_npc_set_position, STRING("sl_crown"), 70, 98, 1)
        CALL(evt_npc_entry, STRING("sl_lug"), STRING("c_burosu_h"))
        CALL(evt_npc_set_position, STRING("sl_lug"), 120, 60, -30)
        CALL(evt_npc_set_ry, STRING("sl_lug"), 270)
        CALL(evt_npc_entry, STRING("sl_nut"), STRING("c_burosu_b"))
        CALL(evt_npc_set_position, STRING("sl_nut"), 140, 60, 30)
        CALL(evt_npc_set_ry, STRING("sl_nut"), 270)
    END_IF
    CALL(evt_npc_entry, STRING("sl_lakky"), STRING("c_jugemu"))
    CALL(evt_npc_set_tribe, STRING("sl_lakky"), STRING(SJ3_LAKITU))
    CALL(evt_npc_set_position, STRING("sl_lakky"), 0, 170, -40)
    CALL(evt_npc_entry, STRING("sl_mic"), STRING("c_sl_mic"))
    CALL(evt_npc_set_position, STRING("sl_mic"), -12, 178, -38)
    CALL(evt_npc_set_anim, STRING("sl_mic"), STRING("T_1"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG3_TOUGI1"))
    CALL(evt_cam3d_evt_set, 0, 180, 517, 0, 95, -145, 0, 11)
    CALL(evt_seq_wait, 2)
    WAIT_FRAMES(2)
    CALL(evt_fade_in, 500)
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE1"), 0)
    CALL(evt_cam3d_evt_set, 0, 160, 420, 0, 80, 0, 1200, 11)
    WAIT_MS(1200)
    CALL(sl3_bout_intro, LW(10), LW(1))
    CALL(evt_msg_print, 0, LW(1), 0, STRING("sl_lakky"))
    IF_INT_EQUAL(LW(10), 5)
        M("sl_c3_boss_02", "sl_lakky")
        M("sl_c3_boss_03", "sl_foe")
        M("sl_c3_boss_04", "sl_foe")
    END_IF
    WAIT_MS(300)
    CALL(evt_snd_sfxon, STRING("SFX_STG3_GONG1"), 0)
    WAIT_MS(500)
    CALL(evt_cam3d_evt_off, 0, 11)
    CALL(sl3_bout_arm, LW(10))
    CALL(sl3_bout_battle, LW(10), LW(11))
    CALL(evt_npc_set_battle_info, STRING("sl_foe"), LW(11))
    CALL(evt_npc_battle_start, STRING("sl_foe"))
    CALL(evt_npc_wait_battle_end)
    CALL(sl3_bout_arm, 0)
    CALL(evt_npc_get_battle_result, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 1)
        /* a lost bout (the arena's are no Game Over): carried out, and back in Pit Lane */
        CALL(evt_cam3d_evt_set, 0, 160, 420, 0, 80, 0, 0, 11)
        M("sl_c3_bout_lost", "sl_lakky")
        CALL(sl3_bout_medic)
        SYS("sl_c3_bout_medic")
        CALL(evt_mario_key_onoff, 1)
        CALL(evt_bero_mapchange, STRING("tou_l34"), STRING("n_bero_1"))
        RETURN
    END_IF
    CALL(evt_cam3d_evt_set, 0, 160, 420, 0, 80, 0, 0, 11)
    CALL(evt_npc_check, STRING("sl_foe"), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_npc_set_position, STRING("sl_foe"), 70, 60, 0)
    END_IF
    CALL(sl3_bout_won, LW(10))
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE2"), 0)
    SWITCH(LW(10))
        CASE_EQUAL(5)
            M("sl_c3_boss_05", "sl_foe")
            M("sl_c3_boss_06", "sl_lakky")
            M("sl_c3_boss_07", "sl_foe")
            M("sl_c3_boss_08", "sl_foe")
            CALL(evt_mario_set_pose, STRING("M_I_2"))
            CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_GET_IMPORTANT_ITEM1"))
            SYS("sl_c3_boss_license")
            CALL(evt_mario_set_pose, STRING("M_S_1"))
            CALL(evt_snd_sfxon, STRING("SFX_COIN_GET1"), 0)
            CALL(evt_pouch_add_coin, 30)
        CASE_EQUAL(6)
            M("sl_c3_boutE_won", "sl_lakky")
            CALL(sl3_give, ITEM_CHARGE)
            SYS("sl_c3_boutE1_prize")
        CASE_EQUAL(7)
            M("sl_c3_boutE_won", "sl_lakky")
            CALL(sl3_give, ITEM_STAR_PIECE)
            SYS("sl_c3_boutE2_prize")
        CASE_EQUAL(8)
            M("sl_c3_boutE_won", "sl_lakky")
            CALL(evt_pouch_add_coin, 30)
            SYS("sl_c3_boutE3_prize")
        CASE_DEFAULT
            M("sl_c3_bout_won", "sl_lakky")
    END_SWITCH
    CALL(sl3_bout_medic)
    SYS("sl_c3_bout_medic")
    /* out of the ring */
    CALL(evt_npc_delete, STRING("sl_foe"))
    CALL(evt_npc_check, STRING("sl_crown"), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_npc_delete, STRING("sl_crown"))
        CALL(evt_npc_delete, STRING("sl_lug"))
        CALL(evt_npc_delete, STRING("sl_nut"))
    END_IF
    CALL(evt_npc_delete, STRING("sl_lakky"))
    CALL(evt_npc_delete, STRING("sl_mic"))
    /* back out through the fighters' door, to Pit Lane */
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_bero_mapchange, STRING("tou_l34"), STRING("n_bero_1"))
    RETURN
EVT_END()

/* the track gate at the far side, once Big Green 01 is built */
static EVT_BEGIN(gate_talk)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SL3F(F_KING), 1)
        IF_INT_EQUAL(SL3F(F_NIGHT), 0)
            M("sl_c3_gate_tomorrow", "sl_gate")
            CALL(evt_mario_key_onoff, 1)
            RETURN
        END_IF
        IF_INT_EQUAL(SL3F(F_RACE_WON), 0)
            M("sl_c3_gate_race", "sl_gate")
        ELSE
            M("sl_c3_gate_practice", "sl_gate")
        END_IF
    ELSE
        M("sl_c3_gate_practice", "sl_gate")
    END_IF
    CALL(evt_msg_select, 0, STRING("sl_c3_gate_yesno"))
    CALL(evt_msg_continue)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_snd_bgmoff, PSND_FADE_OUT)
        CALL(evt_bero_mapchange, STRING("yuu_l30"), STRING("sl_gate"))
        RETURN
    END_IF
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

EVT_BEGIN(sl3_stadium_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG3_TOU3"))
    /* as tou_03's own init: the balcony's upper stands ("ue") are hidden unless Luigi came in on
     * the balcony (s_bero_2): from the side doors their empty top showed white above the stands.
     * The spare stage ("butai_2") is hidden and the ring's lights run */
    CALL(evt_bero_get_entername, LW(0))
    IF_STR_NOT_EQUAL(LW(0), STRING("s_bero_2"))
        CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("ue"), MAPOBJ_FLAG_HIDE)
    END_IF
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("butai_2"), MAPOBJ_FLAG_HIDE)
    /* the big screen over the ring is drawn by tou_03's own TEV callback (screen_tev, the REL's):
     * without it, it was a white board at the top left from the side doors */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("monittor"), MAPOBJ_FLAG_HIDE)
    CALL(evt_map_playanim, STRING("light_coloer"), 1, 0)
    CALL(evt_map_playanim, STRING("light_l"), 1, 0)
    CALL(evt_map_playanim, STRING("light_r"), 1, 0)
    SET_VALUE(LW(0), PTR(b33))
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    IF_INT_EQUAL(SL3F(F_KING), 1)
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_TOU3"))
    ELSE
        IF_INT_EQUAL(SL3F(F_BOUT2), 1)
            CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_TOU2"))
        ELSE
            CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_TOU1"))
        END_IF
    END_IF
    /* a Star Piece in the front row's corner */
    CALL(evt_item_entry, STRING("piece33"), ITEM_STAR_PIECE, 300, 0, 230, ITEM_ENTRY_FIXED, SL3F(F_33_PIECE), 0)
    IF_INT_EQUAL(SL3F(F_KART), 1)
        CALL(evt_npc_entry, STRING("sl_gate"), STRING("c_kinopio_g"))
        CALL(evt_npc_set_tribe, STRING("sl_gate"), STRING(SJ3_TOAD_G))
        CALL(evt_npc_set_position, STRING("sl_gate"), 260, 0, -160)
        CALL(evt_npc_set_ry, STRING("sl_gate"), 270)
        CALL(evt_npc_change_interrupt, STRING("sl_gate"), 6, PTR(gate_talk))
    END_IF
    CALL(sl3_bout_get, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        SPAWN_EVENT(bout_scene)
    END_IF
    RETURN
EVT_END()

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portSl3BattleRegister(void) {
    makeWeapons();
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
}
