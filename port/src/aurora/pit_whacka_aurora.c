/* Whacka in the Pit of 100 Trials, and the Pit Mover (port/include/port/postgame_extra.h).
 *
 * With "pg_pit_whacka" on, once Bonetail has fallen (on an earlier visit),
 * Whacka waits on the 100th floor (jon_06, the Pit's floor 99 counting from
 * 0). Talk to him for a fight: a battle unit of Paper Cut's own on Whacka's
 * field model from the disc (c_koburon), on Bonetail's stage, with three
 * moves of its own and their choice (below). Each win drops a Whacka Bump;
 * the first sets PORT_GSWF_PG_WHACKA_BEATEN, after which the Whacka on
 * Keelhaul Key never runs out of bumps (his count, GSW_MUJ00_WHACKA_BUMPS,
 * is kept below his farewell). A loss is the Pit's: a Game Over.
 *
 * His moves:
 *   - Bump Rush: runs at one of the party and butts it (5).
 *   - Pound Quake: hops high and lands on the stage; everyone on the ground
 *     is hit (3, a chance of dizzy). Every third turn.
 *   - Bump Snack: below half his HP, eats a bump of his own: 15 HP back,
 *     then not again for three turns.
 *
 * The Pit Mover: from the Pit's entrance (tik_06) to the 50th floor's rest
 * room (jon_04, floor 49), once the Pit has been cleared (Bonetail beaten). */
#include "port/statesync.h"
#include "port/postgame_extra.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture (keeps the pointer) */
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_damage.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_item.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_snd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <stdio.h>

void npcSetupBattleInfo(NpcEntry* npc, NpcSetupBattleInfo* setup); /* npcdrv.c */
#include <string.h>

/* a unit type the game never uses, in the enemies' range (UNIT_GOOMBA up to UNIT_UNUSED_TREE): Mario's
 * side's moves leave out the kinds past it (TARGET_CLASS_NO_SYSTEM_UNIT, battle_detect.c), so as
 * UNIT_UNUSED_TESTNPC he could not be jumped on or hammered. Its Tattle entry is ours; it has no Tattle
 * log page, so the Journal's counts leave it out */
#define WHACKA_TYPE UNIT_UNUSED_FLAVIO
#define WHACKA "pg_whacka"
#define FLOOR_100 99
#define BONETAIL_SETUP 51 /* btlsetup_jon_tbl's Bonetail: his stage */

static int* sOn;

int portPitWhackaOn(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pg_pit_whacka", 0, 0, 1);
    }
    return sOn != NULL && *sOn && !portStateSyncLinked() && !portReplayActive();
}

int portPitWhackaBeaten(void) {
    return gp != NULL && swGet(PORT_GSWF_PG_WHACKA_BEATEN);
}

/* --- his battle unit ------------------------------------------------------- */

enum { UW_TURN = 0, UW_SNACK_WAIT = 1 };

static BattleUnitDefense sDefence = {{1, 1, 1, 1, 1}};
static BattleUnitDefenseAttr sDefenceAttr = {{0, 0, 0, 0, 0}};
/* (percent, 100 normal: sleep, stop, dizzy, poison, confuse, electric, burn, freeze, huge, tiny, attack up,
 * attack down, defence up, defence down, allergic, fright, gale force, fast, slow, dodgy, invisible, OHKO) */
static StatusVulnerability sRegist = {60, 50, 80, 80, 60, 100, 100, 80, 100, 80, 100, 100, 100, 100, 100, 0, 0,
                                      100, 70, 100, 100, 0};

static PoseTableEntry sPoses[] = {
    {1, "S_1"},  {2, "S_1"},  {9, "S_1"},  {5, "S_1"},  {4, "S_1"},  {3, "S_1"},  {28, "S_1"},
    {29, "D_1"}, {30, "D_1"}, {31, "S_1"}, {39, "D_1"}, {50, "S_1"}, {51, "S_1"}, {42, "S_1"},
    {41, "S_1"}, {40, "S_1"}, {56, "S_1"}, {57, "D_1"}, {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};

static BattleDataEntry sData[] = {
    {48, btldefaultevt_Dummy},
    {0, NULL},
};

static BattleUnitKindPart sParts[] = {
    {1, "pc_un_whacka", "c_koburon", {0.0f, 0.0f, 0.0f}, {0.0f, 40.0f, 0.0f}, {0.0f, 56.0f, 0.0f}, 40, 50, 255,
     &sDefence, &sDefenceAttr, PART_ATTR_MAIN | PART_ATTR_GUARD_COVER, 0, sPoses},
};


#define WHACKA_TARGETS                                                                                                 \
    (TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE | TARGET_CLASS_NO_SELF | \
     TARGET_CLASS_MAIN_PARTS)

/* Bump Rush: one target, a butt from the front */
static BattleWeapon sRush = {
    .baseAccuracy = 100,
    .superguardsAllowed = 1,
    .reserved_14 = 1.0f,
    .stylishCommandMultiplier = 1,
    .reserved_19 = 1,
    .bingoSlotChance = 1,
    .reserved_1B = 1,
    .hpDamageFn = weaponGetPowerDefault,
    .hpDamageWork = {5, 0, 0, 0, 0, 0, 0, 0},
    .targetClassFlags = WHACKA_TARGETS | TARGET_CLASS_SINGLE,
    .targetPropertyFlags = TARGET_PROP_START_OPPOSING_SIDE,
    .element = ELEMENT_NORMAL,
    .acLevel = 3,
    .reserved_6F = 2,
    .special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_SHATTERS_FREEZE,
    .targetWeightFlags = TARGET_WEIGHT_PREFER_FRONT | TARGET_WEIGHT_RANDOM,
    .stageEffectRates = {0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0},
};

/* Pound Quake: everyone on the ground */
static BattleWeapon sQuake = {
    .baseAccuracy = 100,
    .superguardsAllowed = 0,
    .reserved_14 = 1.0f,
    .stylishCommandMultiplier = 1,
    .reserved_19 = 1,
    .bingoSlotChance = 1,
    .reserved_1B = 1,
    .hpDamageFn = weaponGetPowerDefault,
    .hpDamageWork = {3, 0, 0, 0, 0, 0, 0, 0},
    .targetClassFlags = WHACKA_TARGETS | TARGET_CLASS_MULTIPLE,
    .targetPropertyFlags = TARGET_PROP_NO_FLOATING | TARGET_PROP_START_OPPOSING_SIDE,
    .element = ELEMENT_NORMAL,
    .acLevel = 3,
    .reserved_6F = 2,
    .special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_GROUNDS_WINGED,
    .targetWeightFlags = TARGET_WEIGHT_RANDOM,
    .dizzyChance = 30,
    .dizzyTime = 1,
    .stageEffectRates = {0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0},
};

static EVT_BEGIN(whacka_damage)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

static EVT_BEGIN(whacka_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

/* Bump Snack: 15 HP back */
static EVT_BEGIN(whacka_snack)
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("T_1"))
    WAIT_FRAMES(20)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_EAT1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_RecoverHp, BTL_UNIT_SELF, 1, 15)
    WAIT_FRAMES(40)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SNACK_WAIT, 3)
    RETURN
EVT_END()

/* Bump Rush: LW(3), LW(4) the target */
static EVT_BEGIN(whacka_rush)
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_WeaponAftereffect, PTR(&sRush))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_PayWeaponCost, BTL_UNIT_SELF, PTR(&sRush))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 70)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(0), LW(2), 1, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 1)
    WAIT_MS(200)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(10.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 1)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), PTR(&sRush), 256, LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
            GOTO(90)
        CASE_EQUAL(6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
            GOTO(90)
        CASE_EQUAL(2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
            GOTO(90)
        CASE_EQUAL(1)
            GOTO(91)
        CASE_DEFAULT
            GOTO(90)
    END_SWITCH
    LABEL(91)
    CALL(evt_btl_camera_shake_h, 0, 2, 1, 10, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_KURI_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_ResultACDefence, LW(3), PTR(&sRush))
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), PTR(&sRush), 256, LW(5))
    LABEL(90)
    WAIT_MS(300)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -2, 0)
    RETURN
EVT_END()

/* Pound Quake: everyone on the ground */
static EVT_BEGIN(whacka_quake)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), PTR(&sQuake))
    CALL(btlevtcmd_ChoiceSamplingEnemy, PTR(&sQuake), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(btlevtcmd_WeaponAftereffect, PTR(&sQuake))
    CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_PayWeaponCost, BTL_UNIT_SELF, PTR(&sQuake))
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 50, -1)
    CALL(evt_btl_camera_shake_h, 0, 6, 0, 30, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_WAN_LANDING1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    SET_VALUE(LW(6), 0)
    LABEL(0)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), PTR(&sQuake), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_EQUAL(LW(5), 1)
        IF_INT_EQUAL(LW(6), 0)
            CALL(btlevtcmd_ResultACDefence, LW(3), PTR(&sQuake))
            SET_VALUE(LW(6), 1)
        END_IF
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), PTR(&sQuake), DAMAGE_FLAG_LAST_HIT, LW(5))
    ELSE
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
    END_IF
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(0)
    END_IF
    WAIT_MS(500)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

/* his turn: the snack when he's low and may, the quake every third turn, the rush otherwise */
static EVT_BEGIN(whacka_attack)
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURN, 1)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_SNACK_WAIT, LW(0))
    IF_INT_GREATER(LW(0), 0)
        CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_SNACK_WAIT, -1)
    END_IF
    CALL(btlevtcmd_CheckToken, BTL_UNIT_SELF, TOKEN_FLAG_CONFUSED_ACTION, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_SNACK_WAIT, LW(0))
        IF_INT_EQUAL(LW(0), 0)
            CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(1))
            CALL(btlevtcmd_GetMaxHp, BTL_UNIT_SELF, LW(2))
            MULTIPLY(LW(1), 2)
            IF_INT_LESS(LW(1), LW(2))
                CALL_CHILD(whacka_snack)
                GOTO(99)
            END_IF
        END_IF
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURN, LW(0))
        MODULO(LW(0), 3)
        IF_INT_EQUAL(LW(0), 0)
            CALL_CHILD(whacka_quake)
            GOTO(99)
        END_IF
    END_IF
    CALL(btlevtcmd_check_battleflag, LW(0), 2)
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_GetFirstAttackTarget, LW(3), LW(4))
    ELSE
        CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), PTR(&sRush))
        CALL(btlevtcmd_ChoiceSamplingEnemy, PTR(&sRush), LW(3), LW(4))
    END_IF
    IF_INT_EQUAL(LW(3), -1)
        CALL(btlevtcmd_CheckToken, BTL_UNIT_SELF, TOKEN_FLAG_CONFUSED_ACTION, LW(0))
        IF_INT_NOT_EQUAL(LW(0), 0)
            CALL_CHILD(subsetevt_confuse_flustered)
            RETURN
        END_IF
        GOTO(99)
    END_IF
    CALL_CHILD(whacka_rush)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static EVT_BEGIN(whacka_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(whacka_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(whacka_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(whacka_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(whacka_attack))
    CALL(btlevtcmd_SetBaseScale, BTL_UNIT_SELF, FLOAT(2.0f), FLOAT(2.0f), FLOAT(2.0f))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TURN, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SNACK_WAIT, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static BattleUnitKind sWhackaKind = {
    .unitType = WHACKA_TYPE,
    .name = "pc_un_whacka",
    .maxHp = 150,
    .dangerHP = 40,
    .perilHP = 15,
    .level = 45,
    .bonusExp = 10,
    .baseCoin = 20,
    .runRate = 0,
    .pbSoftCap = 9999,
    .width = 44,
    .height = 52,
    .hitOffset = {6, 52},
    .centerOffset = {0.0f, 26.0f, 0.0f},
    .hpGaugeOffset = {0, 0},
    .talkTogeBaseOffset = {0.0f, 52.0f, 0.0f},
    .heldItemBaseOffset = {-20.0f, 26.0f, 0.0f},
    .burnFlameOffset = {0.0f, 30.0f, 0.0f},
    .bintaHitOffset = {0.0f, 30.0f, 0.0f},
    .kissHitOffset = {0.0f, 30.0f, 0.0f},
    .cutBaseOffset = {0.0f, 26.0f, 0.0f},
    .cutWidth = 44.0f,
    .cutHeight = 52.0f,
    .turnOrder = 0,
    .turnOrderVariance = 0,
    .swallowChance = 0,
    .hammerKnockbackChance = 0,
    .itemStealParam = 30,
    .starPointDispOffset = {0.0f, 0.0f, 0.0f},
    .damageSfxName = "SFX_BTL_DAMAGED_PLIABLE1",
    .fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1",
    .iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1",
    .explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1",
    .attributeFlags = 0,
    .statusVulnerability = &sRegist,
    .numParts = 1,
    .parts = sParts,
    .initEventCode = whacka_init,
    .dataTable = sData,
};

/* --- the fight ------------------------------------------------------------- */

static NpcSetupBattleInfo sInfo;
static NpcBattleSelect sSelect[2];
static BattleGroupSetup sGroup;
static BattleUnitSetup sUnit;
static PointDropData sNoDrops[1];
static int sState; /* 0 none, 1 waiting on the floor, 2 fighting, 3 beaten this visit */
static char sMap[16];
static int sArmed; /* Bonetail had fallen before this visit */

/* in: his name; out: 1 when the battle is set */
USER_FUNC(evt_pg_whacka_battle) {
    EvtWord* args = event->args;
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, args[0]));
    AreaData* ad = areaDataPtr("jon");
    const NpcSetupBattleInfo* tbl = ad != NULL ? (const NpcSetupBattleInfo*)ad->btlSetupTbl : NULL;
    TattleEntry* tattle = battleGetUnitMonosiriPtr(WHACKA_TYPE);
    (void)isFirstCall;
    if (npc == NULL || tbl == NULL || tbl[BONETAIL_SETUP].select == NULL) {
        evtSetValue(event, args[1], 0);
        return EVT_RETURN_DONE;
    }
    /* his name and Tattle (the unused type's entry is ours) */
    tattle->enemyTag = "pc_un_whacka";
    tattle->tattleBtlTag = "pc_hlp_whacka";
    tattle->modelName = "c_koburon";
    tattle->poseName = "S_1";
    memset(&sUnit, 0, sizeof(sUnit));
    sUnit.unitKindParams = &sWhackaKind;
    sUnit.alliance = 1;
    sUnit.attackPhase = BATTLE_PHASE_ENEMY;
    sUnit.position.x = 90.0f;
    memset(&sGroup, 0, sizeof(sGroup));
    sGroup.numEnemies = 1;
    sGroup.enemyData = &sUnit;
    sGroup.noItemWeight = 1;
    sGroup.hpDropTable = sNoDrops;
    sGroup.fpDropTable = sNoDrops;
    memset(sSelect, 0, sizeof(sSelect));
    sSelect[0].weight = 1;
    sSelect[0].groupSetup = &sGroup;
    sSelect[0].stageData = tbl[BONETAIL_SETUP].select[0].stageData;
    sInfo = tbl[BONETAIL_SETUP];
    sInfo.name = "pg_whacka";
    sInfo.fixedItem = 0;
    sInfo.swFlag = -1;
    sInfo.altSelect = NULL;
    sInfo.select = sSelect;
    sInfo.bgmName = "BGM_CHUBOSS_BATTLE1";
    npcSetupBattleInfo(npc, &sInfo);
    sState = 2;
    evtSetValue(event, args[1], 1);
    return EVT_RETURN_DONE;
}

/* in: the result (1 won) */
USER_FUNC(evt_pg_whacka_end) {
    (void)isFirstCall;
    if (evtGetValue(event, event->args[0]) == 1) {
        swSet(PORT_GSWF_PG_WHACKA_BEATEN);
        sState = 3;
        printf("[postgame] Whacka beaten in the Pit\n");
        fflush(stdout);
    } else if (sState == 2) {
        sState = 1;
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(whacka_talk)
    CALL(evt_msg_print, 0, STRING("pc_pg_whacka_00"), 0, STRING(WHACKA))
    CALL(evt_msg_select, 0, STRING("pc_pg_yesno"))
    CALL(evt_msg_continue)
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_msg_print, 0, STRING("pc_pg_whacka_01"), 0, STRING(WHACKA))
        RETURN
    END_IF
    CALL(evt_pg_whacka_battle, STRING(WHACKA), LW(1))
    IF_INT_EQUAL(LW(1), 0)
        RETURN
    END_IF
    CALL(evt_npc_battle_start, STRING(WHACKA))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_pg_whacka_end, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_npc_set_anim, STRING(WHACKA), STRING("D_1"))
        CALL(evt_msg_print, 0, STRING("pc_pg_whacka_02"), 0, STRING(WHACKA))
        CALL(evt_npc_get_position, STRING(WHACKA), LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        CALL(evt_item_entry, STRING("pg_bump"), ITEM_WHACKA_BUMP, LW(0), LW(1), LW(2), ITEM_ENTRY_FALL_THROWN, -1, 0)
        CALL(evt_npc_set_anim, STRING(WHACKA), STRING("A_2"))
        WAIT_MS(800)
        CALL(evt_npc_set_position, STRING(WHACKA), 0, -1000, 0)
        CALL(evt_npc_flag_onoff, 1, STRING(WHACKA), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE)
    END_IF
    RETURN
EVT_END()

static NpcSetupInfo sWhackaSetup[] = {
    {
        WHACKA,                                         // name
        NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_TALK_ANIM, // flags
        0,                                              // reactionFlags
        NULL,                                           // initEvent
        NULL,                                           // regularEvent
        whacka_talk,                                    // talkEvent
    },
    {NULL},
};

/* (the tribe "コブロン" in the game's Shift-JIS) */
static EVT_BEGIN(whacka_spawn)
    CALL(evt_npc_entry, STRING(WHACKA), STRING("c_koburon"))
    CALL(evt_npc_set_tribe, STRING(WHACKA), STRING("\x83\x52\x83\x75\x83\x8D\x83\x93"))
    CALL(evt_npc_set_position, STRING(WHACKA), 0, 0, -60)
    CALL(evt_npc_set_ry, STRING(WHACKA), 0)
    CALL(evt_npc_setup, PTR(sWhackaSetup))
    CALL(evt_npc_set_anim, STRING(WHACKA), STRING("S_1"))
    RETURN
EVT_END()

static const PortPgText kTexts[] = {
    {"pc_un_whacka", "Whacka", 0},
    {"pc_hlp_whacka",
     "That's Whacka! He came all the way down here?<p>Max HP is 150, Attack is 5, and Defence is 1.<p>He butts, "
     "he shakes the whole floor, and when he's hurt he snacks on his own bumps!<p>Hit him hard before he eats "
     "again, Mario!<k>",
     1},
    {"pc_pg_whacka_00",
     "Whacka! You made it all the way down? Whacka followed the bumps in the rock.<p>Whacka is strong down here. "
     "Want to see? Want to fight Whacka?<o>",
     1},
    {"pc_pg_whacka_01", "Whacka will wait. Whacka is good at waiting.<k>", 1},
    {"pc_pg_whacka_02",
     "Whacka! Ow ow ow... That was a good fight. Take a bump, it fell off just now.<p>Whacka goes home to the "
     "island now. Whacka's bumps grow back fast there, you'll see!<k>",
     1},
    {"pc_pg_yesno", "Yes|No", 2},
};

/* --- the Pit Mover ---------------------------------------------------------- */

static int sMoverAsked; /* __atomic */

int portPitMoverReady(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && strcmp(gp->currentMapName, "tik_06") == 0 &&
           swGet(GSWF_JON_BONETAIL_DEFEATED);
}

int portPitMoverGo(const char** why) {
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Use it in the field, with a file loaded.";
        return 0;
    }
    if (!swGet(GSWF_JON_BONETAIL_DEFEATED)) {
        *why = "The Pit Mover runs once the Pit has been cleared.";
        return 0;
    }
    if (strcmp(gp->currentMapName, "tik_06") != 0) {
        *why = "Stand by the Pit's entrance, under Rogueport.";
        return 0;
    }
    if (portStateSyncLinked() || portReplayActive()) {
        *why = "Not online or in a replay.";
        return 0;
    }
    __atomic_store_n(&sMoverAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

/* --- the frame ------------------------------------------------------------- */

void portPitWhackaFrame(void) {
    if (gp == NULL || seqGetSeq() != SEQ_GAME) {
        return;
    }
    if (__atomic_exchange_n(&sMoverAsked, 0, __ATOMIC_SEQ_CST)) {
        if (portPitMoverReady() && !seqCheckSeq()) {
            swByteSet(GSW_JON_FLOOR, 49); /* the 50th floor's rest room */
            seqSetSeq(SEQ_MAP_CHANGE, "jon_04", "dokan_2");
            printf("[postgame] Pit Mover: to the 50th floor\n");
            fflush(stdout);
            return;
        }
        /* (Mario left the Pit's entrance, or a room change began, before the trip: said, not dropped unseen) */
        printf("[postgame] Pit Mover: dropped\n");
        fflush(stdout);
        portCoopNotice("The Pit Mover can't run now: stand by the Pit's entrance.");
    }
    /* Keelhaul Key's Whacka: kept below his farewell (7) once he's been beaten in the Pit */
    if (portPitWhackaOn() && portPitWhackaBeaten() && swByteGet(GSW_MUJ00_WHACKA_BUMPS) > 5) {
        swByteSet(GSW_MUJ00_WHACKA_BUMPS, 5);
    }
    if (gp->inBattle || seqCheckSeq()) {
        return;
    }
    if (strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        /* a new room: Whacka waits if Bonetail fell before this visit */
        snprintf(sMap, sizeof(sMap), "%s", gp->currentMapName);
        sArmed = strcmp(sMap, "jon_06") == 0 && swByteGet(GSW_JON_FLOOR) == FLOOR_100 &&
                 swGet(GSWF_JON_BONETAIL_DEFEATED);
        sState = 0;
    }
    if (sArmed && sState == 0 && portPitWhackaOn() && marioStGetSystemLevel() == 0) {
        if (npcNameToPtr_NoAssert(WHACKA) == NULL) {
            evtEntry(whacka_spawn, 10, 0);
        }
        sState = 1;
    }
}

void portPitWhackaRegister(void) {
    portPgAddTexts(kTexts, (int)(sizeof(kTexts) / sizeof(kTexts[0])));
}
