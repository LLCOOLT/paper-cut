/* Screamy, Luigi's friend from chapter 5 (docs/super_luigi/ch5.md, "Screamy";
 * docs/SUPER_LUIGI.md, "Luigi's friends"): a little Smorg from the white
 * ruin, a partner of Luigi's in Vivian's slot (the framework swaps him in).
 *
 * Field: Float and Look, one button. At a float point (evt_sl_float_point:
 * the two edges of a gap) Screamy lifts Luigi and drifts across; anywhere
 * else he Looks: his eyes glow, the room dims, and every look point in reach
 * (evt_sl_look_point: hidden blocks, doors, edges) runs its reveal.
 * Battle: his own move scripts, with the game's action commands:
 *   Swarm  (0 FP) darts at one foe, a hit for every light answered with A
 *          (the signal command), and one more to finish;
 *   Scream (4 FP) hold A, let go at the light (Bobbery's fuse command):
 *          every foe may be frightened off, or freeze for a turn;
 *   Shade  (3 FP) press the buttons shown: Luigi turns Dodgy until
 *          Screamy's next turn (Invisible at Ultra rank). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "battle/battle.h"
#include "battle/battle_attack_audience.h"
#include "battle/battle_camera.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_party.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/party_data.h"
#include "driver/msgdrv.h"
#include "evt/evt_audience.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mario/mario_sbr.h"
#include "driver/camdrv.h"

f32 marioGetDispDir(void); /* mario_sbr.c */
#include "party.h"
#include "unit/party/unit_party_vivian.h"
#include "unit/party/unit_party_clauda.h"
#include "win/win_main.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

USER_FUNC(evt_luigi_story_pose);

extern BattleWeapon ItemWeaponData_Attiike_Shisshi;
extern BattleUnitKindPart unitpartsdata_Party_Vivian[];

#define SLOT 6 /* Vivian's: partyDataTbl, partyPoseTbl, pouch partyData */

/* his poses by the field pose index (party.c's partyPoseTbl columns) */
static char* const kPoses[15] = {"Z_1", "S_1", "W_1", "R_1", "J_1A", "J_1B", "N_1", "T_1",
                                 "D_1", "D_4", "S_1", "S_1", "D_2", "W_2", "R_2"};

/* the battle poses, as the partners' tables have them (docs/LUIGI_ANIMS.md, "The poses") */
static PoseTableEntry kBattlePoses[] = {
    {9, "D_5"}, {2, "D_5"}, {3, "X_1"}, {4, "D_2"}, {5, "X_1"}, {1, "N_1"}, {27, "D_3"},
    {BTL_POSE_NORMAL, "S_1"}, {37, "G_2"}, {31, "G_1"}, {BTL_POSE_DAMAGE, "D_1"},
    {BTL_POSE_WALK, "W_1"}, {BTL_POSE_RUN, "R_1"}, {BTL_POSE_PERIL, "D_2"}, {BTL_POSE_DANGER, "D_2"},
    {BTL_POSE_ESCAPE_FAIL, "D_4"}, {BTL_POSE_ESCAPE_FAIL_END, "D_2"}, {BTL_POSE_TALK, "T_1"},
    {BTL_POSE_ITEM_USE, "I_1"}, {BTL_POSE_COMMAND_WAIT, "C_1"}, {BTL_POSE_APPEAL, "O_1"},
    {BTL_POSE_ZERO_GRAVITY, "J_1B"}, {49, "J_1A"}, {BTL_POSE_DEFEND, "G_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};

static BattleWeapon sSwarm, sScream, sScreamWeak, sShade, sShadeUltra;
static BattleWeapon* sWaza[4] = {&sSwarm, &sScream, &sShade, &sShade};
static BattleUnitKind sKind;
static BattleUnitKindPart sParts[1];
static BattleUnitDefense sDefense = {{0, 0, 0, 0, 0}};
static BattleUnitDefenseAttr sDefenseAttr = {{0, 0, 0, 0, 0}};

static void addMove(BattleCommand* command, s32* count, BattleWeapon* weapon) {
    command->weapon[*count].index = -1;
    command->weapon[*count].itemId = 0;
    command->weapon[*count].weapon = weapon;
    command->weapon[*count].enabled = 0;
    command->weapon[*count].icon = weapon->icon;
    command->weapon[*count].name = msgSearch(weapon->name);
    (*count)++;
}

/* his battle menu (unit data tag 1): Swarm, Scream, Shade */
static void makeMenu(BattleCommand* command, s32* count) {
    addMove(command, count, &sSwarm);
    addMove(command, count, &sScream);
    addMove(command, count, &sShade);
}

/* out: his rank (0 normal, 1 Super, 2 Ultra) */
static USER_FUNC(sc_rank) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], pouchGetPtr()->partyData[SLOT].attackLevel);
    return EVT_RETURN_DONE;
}

/* in: a weapon slot (0 Scream good, 1 Scream weak, 2 Shade, 3 Shade Ultra); out: its address */
static USER_FUNC(sc_weapon) {
    static BattleWeapon* const kWeapons[] = {&sScream, &sScreamWeak, &sShade, &sShadeUltra};
    (void)isFirstCall;
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)kWeapons[evtGetValue(event, event->args[0]) & 3]);
    return EVT_RETURN_DONE;
}

/* --- the battle unit's own events --- */

static EVT_BEGIN(sc_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

static EVT_BEGIN(sc_attack)
    RETURN
EVT_END()

static EVT_BEGIN(sc_damage)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

/* he floats in from behind as the battle starts */
static EVT_BEGIN(sc_entry)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 250)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -2, 0)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

static EVT_BEGIN(sc_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(sc_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(sc_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(sc_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(btldefaultevt_Confuse))
    CALL(btlevtcmd_SetWalkSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_MOVE1L"), STRING("SFX_PARTY_BATTLE_MOVE1R"), 0, 20, 20)
    CALL(btlevtcmd_SetRunSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_MOVE1L"), STRING("SFX_PARTY_BATTLE_MOVE1R"), 0, 11, 11)
    CALL(btlevtcmd_SetJumpSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), STRING("SFX_PARTY_BATTLE_LANDING1"))
    CALL(btlevtcmd_SetEventEntry, BTL_UNIT_SELF, PTR(sc_entry))
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Appeal (data tag 5) */
static EVT_BEGIN(sc_appeal)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_APPEAL)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_MARIO_APPEAL1"), LW(0), LW(1), LW(2), 0)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AudienceAppeal, BTL_UNIT_SELF)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* victory (data tag 64): a happy wobble */
static EVT_BEGIN(sc_win)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
    WAIT_FRAMES(56)
    RETURN
EVT_END()

/* --- Swarm: darts at one foe; the signal command (18), one A per light ---
 * Each light answered is a hit as it lands; one more hit finishes the move. */
EVT_BEGIN(sc_swarm)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    /* gathering himself, then over to the foe */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_SUBHANG1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(12)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 34)
    ADD(LW(2), 6)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(7.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(13), LW(14), LW(9))
    /* the lights: 3, 4 from Super rank */
    CALL(sc_rank, LW(0))
    SET_VALUE(LW(7), 3)
    IF_INT_GREATER_OR_EQUAL(LW(0), 1)
        SET_VALUE(LW(7), 4)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcSetParamAll, LW(7), 1, 26, 1, 0, 0, 1, 2)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 18, 1, 0)
    WAIT_FRAMES(22)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    /* while it runs: a hit each time a light is answered */
    SET_VALUE(LW(10), 0)
    SET_VALUE(LW(8), LW(7))
    ADD(LW(8), 2)
    MULTIPLY(LW(8), 26)
    ADD(LW(8), 30)
    LOOP(LW(8))
        CALL(btlevtcmd_AcGetOutputParam, 0, LW(6))
        IF_INT_GREATER(LW(6), LW(10))
            ADD(LW(10), 1)
            CALL(btlevtcmd_GetHp, LW(3), LW(5))
            IF_INT_GREATER(LW(5), 0)
                /* a dart: in and back */
                CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(13), 14)
                CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(13), LW(14), LW(9))
                CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_VIVIAN_ATTACK4"), EVT_NULLPTR, 0, EVT_NULLPTR)
                CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_ALWAYS_HIT, LW(5))
                IF_INT_EQUAL(LW(5), 1)
                    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_AC_SUCCESS, LW(5))
                END_IF
                CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(13), LW(14), LW(9))
                CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(13), 14)
                WAIT_FRAMES(3)
                CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(13), LW(14), LW(9))
            END_IF
        END_IF
        WAIT_FRAMES(1)
    END_LOOP
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(6))
    /* the finishing hit */
    CALL(btlevtcmd_GetHp, LW(3), LW(5))
    IF_INT_GREATER(LW(5), 0)
        CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        IF_INT_NOT_EQUAL(LW(5), 1)
            IF_INT_EQUAL(LW(5), 3)
                CALL(btlevtcmd_StartAvoid, LW(3), 38)
            END_IF
            IF_INT_EQUAL(LW(5), 6)
                CALL(btlevtcmd_StartAvoid, LW(3), 39)
            END_IF
            IF_INT_EQUAL(LW(5), 2)
                CALL(btlevtcmd_StartAvoid, LW(3), 40)
            END_IF
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
            GOTO(90)
        END_IF
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(13), 16)
        CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(13), LW(14), LW(9))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_VIVIAN_ATTACK4"), EVT_NULLPTR, 0, EVT_NULLPTR)
        IF_BITS_SET(LW(6), 2)
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
            CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
        ELSE
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        END_IF
    END_IF
    LABEL(90)
    CALL(btlevtcmd_StopAC)
    WAIT_MS(300)
    /* home, with a stylish as he turns */
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(0), LW(2), 1, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_ACRStart, BTL_UNIT_SELF, 6, 18, 18, 0)
    END_SIBLING_EVENT
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_ACRGetResult, LW(6), LW(7))
    IF_INT_EQUAL(LW(6), 2)
        CALL(btlevtcmd_AudienceDeclareAcrobatResult, LW(12), 1, 0, 0, 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
        WAIT_FRAMES(40)
    END_IF
    LABEL(99)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- Scream: hold A while he draws himself in, let go at the light
 * (Bobbery's fuse command, 15). Every foe may be frightened off. --- */
EVT_BEGIN(sc_scream)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(evt_btl_camera_set_mode, 0, 3)
    CALL(evt_btl_camera_set_moveto, 1, LW(0), 60, 330, LW(0), 30, 0, 30, 5)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-2)
            SET_VALUE(LW(1), 50)
        CASE_EQUAL(-1)
            SET_VALUE(LW(1), 35)
        CASE_EQUAL(0)
            SET_VALUE(LW(1), 26)
        CASE_EQUAL(1)
            SET_VALUE(LW(1), 18)
        CASE_DEFAULT
            SET_VALUE(LW(1), 10)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, 2, 110, LW(1), 0, 100, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 15)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 15, 1, 0)
    WAIT_FRAMES(22)
    /* he swells while A is held */
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        SET_VALUE(LW(0), 5)
        LOOP(LW(0))
            CALL(btlevtcmd_AcGetOutputParam, 0, LW(1))
            IF_INT_EQUAL(LW(1), 0)
                SET_VALUE(LW(0), 5)
            ELSE
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_WaitEventEnd, LW(15))
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(6))
    CALL(btlevtcmd_StopAC)
    /* the scream: good, or a squeak */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    IF_BITS_SET(LW(6), 2)
        CALL(sc_weapon, 0, LW(12))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_AUDIENCE_PANIC_SCREAM1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_btl_camera_shake_h, 0, 4, 0, 40, 13)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 20)
        CALL(evt_eff, 0, STRING("toge_flush"), 0, LW(0), LW(1), LW(2), 60, 0, 0, 0, 0, 0, 0, 0)
    ELSE
        CALL(sc_weapon, 1, LW(12))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_VIVIAN_MAGIC1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    END_IF
    WAIT_FRAMES(30)
    /* every foe */
    LABEL(10)
    IF_INT_EQUAL(LW(3), -1)
        GOTO(80)
    END_IF
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
        CASE_EQUAL(1)
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    END_SWITCH
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    GOTO(10)
    LABEL(80)
    IF_BITS_SET(LW(6), 2)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(7))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(7), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(7))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    WAIT_MS(800)
    LABEL(99)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- Shade: he spreads over Luigi; press the buttons shown (10) --- */
EVT_BEGIN(sc_shade)
    SET_VALUE(LW(3), -3)
    CALL(btlevtcmd_GetBodyId, LW(3), LW(4))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, 1)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    /* over Luigi */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 42)
    ADD(LW(2), 4)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(3.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3"))
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-2)
            SET_VALUE(LW(0), 300)
        CASE_EQUAL(-1)
            SET_VALUE(LW(0), 240)
        CASE_EQUAL(0)
            SET_VALUE(LW(0), 200)
        CASE_EQUAL(1)
            SET_VALUE(LW(0), 160)
        CASE_DEFAULT
            SET_VALUE(LW(0), 120)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, LW(0), 1, 2, -2, 2, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 10, 1, 0)
    WAIT_FRAMES(22)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(6))
    CALL(btlevtcmd_StopAC)
    IF_BITS_CLEAR(LW(6), 2)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("E_1"))
        WAIT_FRAMES(40)
        GOTO(90)
    END_IF
    /* a dark cloud settles over him */
    CALL(sc_rank, LW(0))
    IF_INT_GREATER_OR_EQUAL(LW(0), 2)
        CALL(sc_weapon, 3, LW(12))
    ELSE
        CALL(sc_weapon, 2, LW(12))
    END_IF
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 20)
    CALL(evt_eff, STRING(""), STRING("kemuri_test"), 8, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PARTY_VIVIAN_SHADOW1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
    CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(6))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    WAIT_FRAMES(50)
    LABEL(90)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(3.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static BattleDataEntry sData[] = {
    {5, sc_appeal},
    {1, (void*)makeMenu},
    {48, btldefaultevt_Dummy},
    {64, sc_win},
    {7, subset_attack_ready_jump_event},
    {47, btldefaultevt_avoid_counter_event},
    {0, NULL},
};

static BattleUnitKind* screamyUnit(void) {
    /* Swarm: one foe, 1 a hit (Vivian's Shade Fist's targeting: reaches the air) */
    sSwarm = partyWeapon_VivianNormalAttack;
    sSwarm.name = "sl_sc_swarm";
    sSwarm.description = "sl_sc_swarm_h";
    sSwarm.acHelpMsg = "sl_sc_swarm_ac";
    sSwarm.baseFpCost = 0;
    sSwarm.hpDamageFn = weaponGetPowerFromPartyAttackLv;
    memset(sSwarm.hpDamageWork, 0, sizeof(sSwarm.hpDamageWork));
    sSwarm.hpDamageWork[0] = sSwarm.hpDamageWork[1] = sSwarm.hpDamageWork[2] = 1;
    sSwarm.hpDamageWork[3] = sSwarm.hpDamageWork[4] = sSwarm.hpDamageWork[5] = 1;
    sSwarm.special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_USABLE_IF_CONFUSED;
    sSwarm.burnChance = 0;
    sSwarm.burnTime = 0;
    sSwarm.attackEvtScript = sc_swarm;
    /* Scream: every foe; frightened (it may run) or frozen stiff for a turn */
    sScream = ItemWeaponData_Attiike_Shisshi;
    sScream.name = "sl_sc_scream";
    sScream.description = "sl_sc_scream_h";
    sScream.acHelpMsg = "sl_sc_scream_ac";
    sScream.icon = partyWeapon_VivianMagicalPowder.icon;
    sScream.itemId = 0;
    sScream.baseFpCost = 4;
    sScream.frightChance = 60;
    sScream.stopChance = 50;
    sScream.stopTime = 1;
    sScream.attackEvtScript = sc_scream;
    sScreamWeak = sScream;
    sScreamWeak.frightChance = 20;
    sScreamWeak.stopChance = 0;
    sScreamWeak.stopTime = 0;
    /* Shade: Luigi, Dodgy until Screamy's next turn */
    sShade = partyWeapon_ClaudaKumogakureAttack;
    sShade.name = "sl_sc_shade";
    sShade.description = "sl_sc_shade_h";
    sShade.acHelpMsg = "sl_sc_shade_ac";
    sShade.icon = partyWeapon_VivianShadowGuard.icon;
    sShade.baseFpCost = 3;
    sShade.dodgyChance = 100;
    sShade.dodgyTime = 1;
    sShade.attackEvtScript = sc_shade;
    sShadeUltra = sShade;
    sShadeUltra.invisibleChance = 100;
    sShadeUltra.invisibleTime = 1;

    sParts[0] = unitpartsdata_Party_Vivian[0];
    sParts[0].partName = "sl_name_screamy";
    sParts[0].modelName = "c_lp_moa";
    sParts[0].hitBaseOffset.y = 12.0f;
    sParts[0].hitCursorBaseOffset.y = 20.0f;
    sParts[0].hitHeight = 18;
    sParts[0].defense = &sDefense;
    sParts[0].defenseAttr = &sDefenseAttr;
    sParts[0].poseTable = kBattlePoses;
    sKind = unitdata_Party_Vivian;
    sKind.name = "sl_name_screamy";
    sKind.parts = sParts;
    sKind.numParts = 1;
    sKind.initEventCode = sc_init;
    sKind.dataTable = sData;
    return &sKind;
}

/* ======================================================================
 * Float and Look
 * ====================================================================== */

/* in: a float point (0-7) and its two edges (x, y, z each) */
USER_FUNC(evt_sl_float_point) {
    EvtWord* a = event->args;
    PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_FLOAT, (int)evtGetValue(event, a[0]));
    (void)isFirstCall;
    if (p != NULL) {
        p->on = 1;
        p->used = 0;
        p->x = (f32)evtGetValue(event, a[1]);
        p->y = (f32)evtGetValue(event, a[2]);
        p->z = (f32)evtGetValue(event, a[3]);
        p->x2 = (f32)evtGetValue(event, a[4]);
        p->y2 = (f32)evtGetValue(event, a[5]);
        p->z2 = (f32)evtGetValue(event, a[6]);
        p->radius = 60.0f;
        p->script = NULL;
    }
    return EVT_RETURN_DONE;
}

/* in: a look point (0-7), where (x, y, z), how near (radius) and its reveal script (LW(0) its id) */
USER_FUNC(evt_sl_look_point) {
    EvtWord* a = event->args;
    PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_LOOK, (int)evtGetValue(event, a[0]));
    (void)isFirstCall;
    if (p != NULL) {
        p->on = 1;
        p->used = 0;
        p->x = p->x2 = (f32)evtGetValue(event, a[1]);
        p->y = p->y2 = (f32)evtGetValue(event, a[2]);
        p->z = p->z2 = (f32)evtGetValue(event, a[3]);
        p->radius = (f32)evtGetValue(event, a[4]);
        p->value = 0;
        p->script = (void*)evtGetValue(event, a[5]);
    }
    return EVT_RETURN_DONE;
}

/* out: the edge to float to from Luigi's place (x, y, z), and 1, or 0 when none is in reach */
static USER_FUNC(float_find) {
    EvtWord* a = event->args;
    MarioWork* m = marioGetPtr();
    int end = 0;
    PortSlPoint* p = portSlPointNear(PORT_SL_ABILITY_FLOAT, m->position.x, m->position.z, &end);
    (void)isFirstCall;
    if (p != NULL) {
        /* only toward the gap: facing away from the far edge, it's a Look (a float edge is
         * where Luigi stands most, and Look must still work there) */
        const f32 tx = end ? p->x : p->x2, tz = end ? p->z : p->z2;
        const f32 yaw = camGetPtr(CAMERA_3D)->viewYaw;
        const f32 to = reviseAngle(angleABf(m->position.x, m->position.z, tx, tz) - yaw);
        const f32 face = reviseAngle(marioGetDispDir() - yaw);
        const f32 side = to < 180.0f ? to : 360.0f - to; /* about 90: across the screen; 0 or 180: into it */
        if (side > 30.0f && side < 150.0f && (to < 180.0f) != (face < 180.0f)) {
            p = NULL; /* across the screen, and he faces the other way */
        }
    }
    evtSetValue(event, a[0], p == NULL ? 0 : (s32)(end ? p->x : p->x2));
    evtSetValue(event, a[1], p == NULL ? 0 : (s32)(end ? p->y : p->y2));
    evtSetValue(event, a[2], p == NULL ? 0 : (s32)(end ? p->z : p->z2));
    evtSetValue(event, a[3], p != NULL);
    return EVT_RETURN_DONE;
}


static int sLooks;
static s32 sLookIds[PORT_SL_POINTS];
static int sLookCount;

/* Look: every look point within its reach of Luigi reveals; out: how many did */
static USER_FUNC(look_run) {
    MarioWork* m = marioGetPtr();
    int i, n = 0;
    (void)isFirstCall;
    sLookCount = 0;
    for (i = 0; i < PORT_SL_POINTS; i++) {
        PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_LOOK, i);
        if (p->on && hypotf(m->position.x - p->x, m->position.z - p->z) <= p->radius) {
            EventEntry* e = (EventEntry*)portSlPointRun(PORT_SL_ABILITY_LOOK, p);
            if (e != NULL) {
                sLookIds[sLookCount++] = e->eventId;
                n++;
            }
        }
    }
    evtSetValue(event, event->args[0], n);
    evtSetValue(event, event->args[1], sLooks++ % 3);
    return EVT_RETURN_DONE;
}

/* blocks until every reveal look_run started has finished. They are plain scripts, stopped until the
 * ability hands the field back, so this comes after evt_sl_ability_done */
static USER_FUNC(look_wait) {
    int i;
    (void)event;
    (void)isFirstCall;
    for (i = 0; i < sLookCount; i++) {
        if (evtCheckID(sLookIds[i])) {
            return EVT_RETURN_BLOCK;
        }
    }
    return EVT_RETURN_DONE;
}

/* Float: Screamy rises over Luigi, Luigi takes hold, and they drift across */
static EVT_BEGIN(float_evt)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_get_pos, 0, LW(4), LW(5), LW(6))
    ADD(LW(5), 38)
    CALL(evt_party_set_pose, 0, STRING("F_1"))
    CALL(evt_party_jump_pos, 0, LW(4), LW(5), LW(6), 400, 0, -1000)
    CALL(evt_party_set_pose, 0, STRING("F_2"))
    CALL(evt_snd_sfxon, STRING("SFX_PARTY_BATTLE_JUMP1"), 0)
    CALL(evt_luigi_story_pose, STRING("SL_HANG"))
    CALL(evt_msg_print, 0, STRING("sl_sc_float"), 0, STRING("party"))
    CALL(evt_snd_sfxon, STRING("SFX_PARTY_BATTLE_SUBHANG1"), 0)
    BEGIN_INLINE_EVENT
        SET_VALUE(LW(7), LW(1))
        ADD(LW(7), 38)
        CALL(evt_party_jump_pos, 0, LW(0), LW(7), LW(2), 1600, 0, 40)
    END_INLINE_EVENT
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), 1600, 1, 40)
    CALL(evt_luigi_story_pose, 0)
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    WAIT_FRAMES(10)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* Look: eyes wide, the room dims, the hidden shows */
static EVT_BEGIN(look_evt)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_set_pose, 0, STRING("F_3"))
    CALL(evt_party_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(1), 16)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_VIVIAN_MAGIC1"), 0)
    CALL(evt_eff, STRING(""), STRING("stardust"), 2, LW(0), LW(1), LW(2), 30, 30, 30, 60, 0, 0, 0, 0)
    BEGIN_INLINE_EVENT
        CALL(evt_sub_intpl_msec_init, 0, 255, 150, 300)
        LOOP(0)
            CALL(evt_sub_intpl_msec_get_value)
            CALL(evt_map_set_blend, 0, LW(0), LW(0), LW(0), 255)
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
        END_LOOP
        WAIT_MS(5000)
        CALL(evt_sub_intpl_msec_init, 0, 150, 255, 700)
        LOOP(0)
            CALL(evt_sub_intpl_msec_get_value)
            CALL(evt_map_set_blend, 0, LW(0), LW(0), LW(0), 255)
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
        END_LOOP
        /* off, not left on at white (a room change clears it anyway: mapLoad) */
        CALL(evt_map_blend_off, 0)
    END_INLINE_EVENT
    WAIT_MS(700)
    CALL(look_run, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), 0)
        SWITCH(LW(4))
            CASE_EQUAL(0)
                CALL(evt_msg_print, 0, STRING("sl_sc_nothing_0"), 0, STRING("party"))
            CASE_EQUAL(1)
                CALL(evt_msg_print, 0, STRING("sl_sc_nothing_1"), 0, STRING("party"))
            CASE_DEFAULT
                CALL(evt_msg_print, 0, STRING("sl_sc_nothing_2"), 0, STRING("party"))
        END_SWITCH
    ELSE
        WAIT_MS(900)
    END_IF
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    RETURN
EVT_END()

/* his ability: Float at a float point, else Look */
static EVT_BEGIN(screamy_evt)
    CALL(evt_sl_where, STRING("screamy's ability"))
    CALL(float_find, LW(0), LW(1), LW(2), LW(3))
    IF_INT_EQUAL(LW(3), 1)
        CALL_CHILD(float_evt)
    ELSE
        /* Luigi stays held until what Look found has said its piece (look_evt's key off) */
        CALL_CHILD(look_evt)
        CALL(evt_sl_ability_done)
        CALL(look_wait)
        CALL(evt_mario_key_onoff, 1)
        RETURN
    END_IF
    CALL(evt_sl_ability_done)
    RETURN
EVT_END()

static void screamyUse(PartyEntry* party) {
    if (party->useMotionId == 0) {
        printf("[superluigi] Screamy's ability\n");
        fflush(stdout);
    }
    portSlFieldUse(party, screamy_evt);
}

static const char* const kText[] = {
    "sl_name_screamy", "Screamy",
    "sl_sc_swarm", "Swarm",
    "sl_sc_swarm_h", "Screamy darts at one foe, again\nand again: a hit for every light.",
    "sl_sc_swarm_ac", "Press <icon PAD_A 0.6 0 22 0> each time a light\ncomes on!",
    "sl_sc_scream", "Scream",
    "sl_sc_scream_h", "A scream so loud that foes may\nrun away, or freeze for a turn.",
    "sl_sc_scream_ac", "Hold <icon PAD_A 0.6 0 22 0>, and let go when\nthe light comes on!",
    "sl_sc_shade", "Shade",
    "sl_sc_shade_h", "Hides Luigi in Screamy's shadow:\nfoes may miss him for a turn.",
    "sl_sc_shade_ac", "Press the buttons as they\nappear!",
    "sl_sc_float", "Hold on! Don't look down!\nI'm not looking down either!",
    "sl_sc_nothing_0", "Nothing here. Just walls.\nWalls, walls, walls.",
    "sl_sc_nothing_1", "I see... you. Hi.",
    "sl_sc_nothing_2", "Everything's where it should be.\nSpooky.",
    NULL,
};

static const PortSlFriend kScreamy = {
    PORT_SL_FRIEND_SCREAMY,
    SLOT,
    "Screamy",
    "c_lp_moa",
    "A little Smorg from the white\nruin. Sees what's hidden.\nStartles easily. Very easily.",
    kPoses,
    screamyUse,
    NULL,
    "S_1",
    screamyUnit,
    sWaza,
    "art/icons/screamy/screamy_menu.png",
    "art/icons/screamy/screamy_hud.png",
    15,
    0,
    kText,
    NULL,
};

const PortSlFriend* portSlFriendScreamy(void) {
    return &kScreamy;
}
