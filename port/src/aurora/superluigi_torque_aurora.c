/* Torque, Luigi's friend from chapter 3 (docs/super_luigi/ch3.md, "Torque";
 * docs/SUPER_LUIGI.md, "Luigi's friends"): a Buzzy Beetle mechanic, partner
 * of Luigi's in Koops's slot (the framework swaps him in): his field entry
 * (model, poses, ability), party-menu entry, battle unit and his own texts.
 *
 * Field: Tune-up. Near a broken machine a room marks (a PORT_SL_ABILITY_TUNE
 * point, evt_sl_point), Torque gets under it with his wrench and the player
 * taps A to fill a gauge (the game's action-command gauge, drawn in the
 * field); full, the point's script runs. A point's value 1 is the big one
 * (Big Green 01 on the grid: a larger gauge against a 15-second clock that
 * refills and goes again if it runs out). Anywhere else he grumbles.
 * Battle: his own move scripts, with the game's action commands:
 *   Shell Slide  0 FP  tucks in and slides along the ground through every
 *                      grounded foe, flipping shelled ones (hold the stick
 *                      left, release when the star lights)
 *   Wrench Work  3 FP  fixes Luigi up: 5 HP, 6 or 7 with more of the buttons
 *                      shown pressed in order
 *   Spike Guard  4 FP  hops in front of Luigi and bristles (A as he lands):
 *                      until Torque's next turn, a foe that lands on Luigi
 *                      from above meets the spikes instead (the game's
 *                      top-spike counter on Luigi's body)
 * His poses (docs/LUIGI_ANIMS.md): F_1/F_2 Tune-up, A_1A-C Shell Slide,
 * A_2A/B Wrench Work, A_3A/B Spike Guard, and the partners' standard set. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_disp.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_audience.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "battle/battle_damage.h"
#include "battle/battle_sub.h"
#include "evt/evt_audience.h"
#include "manager/fontmgr.h"
#include "mario/mario_party.h"
#include "pmario_sound.h"
#include "data/party_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/msgdrv.h"
#include "evt/evt_cmd.h"
#include "icon_ids.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "system.h"
#include "unit/party/unit_party_nokotarou.h"
#include <dolphin/gx.h>
#include <dolphin/pad.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOT 2 /* Koops's: partyDataTbl, partyPoseTbl, pouch partyData */
#define MODEL "c_lp_met"

BattleWeapon* BattleGetSelectWeapon(BattleWork* wp);
extern EvtWord nokotarou_appeal_event[];

/* field poses, partyPoseTbl's columns (docs/LUIGI_ANIMS.md, "The poses") */
static char* const kPoses[15] = {"Z_1", "S_1", "W_1", "R_1", "J_1A", "J_1B", "N_1", "T_1",
                                 "D_1", "D_4", "S_1", "S_1", "D_2", "W_2", "R_2"};

/* battle poses, as the partners' tables have them; 50 is the Shell Slide's spin */
static PoseTableEntry kBattlePoses[] = {
    {9, "D_5"}, {2, "D_5"}, {3, "X_1"}, {4, "D_2"}, {5, "X_1"}, {1, "N_1"}, {27, "D_3"},
    {BTL_POSE_NORMAL, "S_1"}, {37, "G_2"}, {31, "G_1"}, {BTL_POSE_DAMAGE, "D_1"},
    {BTL_POSE_WALK, "W_1"}, {BTL_POSE_RUN, "R_1"}, {50, "A_1B"}, {BTL_POSE_PERIL, "D_2"}, {BTL_POSE_DANGER, "D_2"},
    {BTL_POSE_ESCAPE_FAIL, "D_4"}, {BTL_POSE_ESCAPE_FAIL_END, "D_2"}, {BTL_POSE_TALK, "T_1"},
    {BTL_POSE_ITEM_USE, "I_1"}, {BTL_POSE_COMMAND_WAIT, "C_1"}, {BTL_POSE_APPEAL, "O_1"},
    {BTL_POSE_ZERO_GRAVITY, "J_1B"}, {49, "J_1A"}, {BTL_POSE_DEFEND, "G_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};
/* while Spike Guard is up: braced, bristling */
static PoseTableEntry kGuardPoses[] = {
    {9, "D_5"}, {2, "D_5"}, {3, "X_1"}, {4, "D_2"}, {5, "X_1"}, {1, "N_1"}, {27, "D_3"},
    {BTL_POSE_NORMAL, "A_3B"}, {37, "G_2"}, {31, "G_1"}, {BTL_POSE_DAMAGE, "D_1"},
    {BTL_POSE_WALK, "W_1"}, {BTL_POSE_RUN, "R_1"}, {50, "A_1B"}, {BTL_POSE_PERIL, "A_3B"}, {BTL_POSE_DANGER, "A_3B"},
    {BTL_POSE_ESCAPE_FAIL, "D_4"}, {BTL_POSE_ESCAPE_FAIL_END, "D_2"}, {BTL_POSE_TALK, "T_1"},
    {BTL_POSE_ITEM_USE, "I_1"}, {BTL_POSE_COMMAND_WAIT, "C_1"}, {BTL_POSE_APPEAL, "O_1"},
    {BTL_POSE_ZERO_GRAVITY, "J_1B"}, {49, "J_1A"}, {BTL_POSE_DEFEND, "G_1"}, {BTL_POSE_STAY_DEFAULT, "A_3B"},
};

static BattleWeapon sSlide, sWrench, sSpike;
static BattleWeapon* sWaza[4] = {&sSlide, &sWrench, &sSpike, &sSpike};
static BattleUnitKind sKind;
static BattleUnitKindPart sParts[1];
static BattleDataEntry sData[12];
static BattleUnitDefense kDefense = {{1, 1, 1, 1, 1}};

/* --- Spike Guard's state: on Luigi's body part until Torque's next turn --- */

static int sSpikeOn;

static BattleUnitPart* luigiBody(void) {
    BattleUnit* m = _battleWorkPointer != NULL ? BattleGetMarioPtr(_battleWorkPointer) : NULL;
    return m != NULL ? m->parts : NULL;
}

static BattleUnitPart* torqueBody(void) {
    BattleUnit* p = _battleWorkPointer != NULL ? BattleGetPartyPtr(_battleWorkPointer) : NULL;
    return p != NULL ? p->parts : NULL;
}

static void spikeOff(void) {
    BattleUnitPart* b;
    if (!sSpikeOn) {
        return;
    }
    sSpikeOn = 0;
    if ((b = luigiBody()) != NULL) {
        b->counterAttributes &= ~COUNTER_ATTR_TOP_SPIKY;
    }
    if ((b = torqueBody()) != NULL && b->poseTable == kGuardPoses) {
        b->poseTable = kBattlePoses;
    }
}

static USER_FUNC(tq_spike_on) {
    BattleUnitPart* b;
    (void)event, (void)isFirstCall;
    if ((b = luigiBody()) != NULL) {
        b->counterAttributes |= COUNTER_ATTR_TOP_SPIKY;
        sSpikeOn = 1;
    }
    if ((b = torqueBody()) != NULL) {
        b->poseTable = kGuardPoses;
    }
    return EVT_RETURN_DONE;
}

/* out: Wrench Work's healing from the presses (LW in: presses, out: HP) */
static USER_FUNC(tq_wrench_hp) {
    const s32 presses = evtGetValue(event, event->args[0]);
    const int rank = pouchGetPtr()->partyData[SLOT].attackLevel;
    s32 hp = 5 + (presses >= 2 ? presses - 1 : 0) + (rank >= 2 ? 2 : 0);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], hp);
    return EVT_RETURN_DONE;
}

static void addMove(BattleCommand* command, s32* count, BattleWeapon* weapon) {
    command->weapon[*count].index = -1;
    command->weapon[*count].itemId = 0;
    command->weapon[*count].weapon = weapon;
    command->weapon[*count].enabled = 0;
    command->weapon[*count].icon = weapon->icon;
    command->weapon[*count].name = msgSearch(weapon->name);
    (*count)++;
}

/* Torque's battle menu (unit data tag 1), made as his turn comes: his last
 * Spike Guard ends here */
static void makeMenu(BattleCommand* command, s32* count) {
    spikeOff();
    addMove(command, count, &sSlide);
    addMove(command, count, &sWrench);
    addMove(command, count, &sSpike);
}

/* --- his moves --- */

/* Shell Slide: in, spin, slide through the grounded foes, out */
static EVT_BEGIN(tq_slide_evt)
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
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, 1)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(evt_btl_camera_set_posoffset, 0, 20, 0, 0)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    /* he pulls in head and legs, and the shell starts to turn */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_TOGENOKO_SHELL1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(12)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK1"), LW(13))
    /* hold the stick left, let go when the star lights (Koops's Shell Toss command) */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-3)
        SET_VALUE(LW(0), 360)
        SET_VALUE(LW(1), 100)
        CASE_EQUAL(-2)
        SET_VALUE(LW(0), 280)
        SET_VALUE(LW(1), 150)
        CASE_EQUAL(-1)
        SET_VALUE(LW(0), 240)
        SET_VALUE(LW(1), 200)
        CASE_EQUAL(0)
        SET_VALUE(LW(0), 200)
        SET_VALUE(LW(1), 250)
        CASE_EQUAL(1)
        SET_VALUE(LW(0), 200)
        SET_VALUE(LW(1), 360)
        CASE_DEFAULT
        SET_VALUE(LW(0), 200)
        SET_VALUE(LW(1), 600)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, 2, LW(0), 0, 1, LW(1), EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 3)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 19, 1, 60)
    WAIT_FRAMES(22)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 4)
    CALL(evt_btl_camera_set_zoom, 0, 400)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(evt_snd_sfxoff, LW(13))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK2"), 0)
    CALL(btlevtcmd_GetResultAC, LW(14))
    CALL(btlevtcmd_StopAC)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_posoffset, 0, 0, 0, 0)
    SET_VALUE(LW(10), 0)
    /* one foe after another along the ground */
    LABEL(10)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(10.0f))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_TransStageFloorPosition, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetMoveFrame, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, LW(15))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(15), 0, 0)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(6))
    IF_INT_NOT_EQUAL(LW(6), 1)
        IF_INT_EQUAL(LW(6), 3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        END_IF
        IF_INT_EQUAL(LW(6), 6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        END_IF
        IF_INT_EQUAL(LW(6), 2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
        END_IF
        GOTO(20)
    END_IF
    IF_BITS_SET(LW(14), 2)
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        IF_INT_EQUAL(LW(10), 0)
            CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
        END_IF
    ELSE
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
        IF_INT_EQUAL(LW(10), 0)
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        END_IF
    END_IF
    ADD(LW(10), 1)
    LABEL(20)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(10)
    END_IF
    /* out the far side, then he pops out and runs home */
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1C"))
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.6f))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 14, -1)
    WAIT_FRAMES(10)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Wrench Work: to Luigi with the wrench; the buttons shown, in order */
static EVT_BEGIN(tq_wrench_evt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    /* he trots up beside Luigi */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 22)
    ADD(LW(2), 8)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PEACH_MACHINE_SWITCH1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(16)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        LOOP(0)
            CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_STG7_CRANE_BUTTON_PUSH1"), EVT_NULLPTR, 0, EVT_NULLPTR)
            WAIT_FRAMES(16)
        END_LOOP
    END_SIBLING_EVENT
    /* the buttons shown, in order (Vivian's Veil's command), three of them */
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
    CALL(btlevtcmd_AcSetParamAll, LW(0), 1, 3, -3, 3, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 2)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 10, 1, 0)
    WAIT_FRAMES(22)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 0, LW(6))
    CALL(btlevtcmd_GetResultAC, LW(0))
    DELETE_EVENT(LW(15))
    IF_BITS_SET(LW(0), 2)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(7))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(7), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(7))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        SET_VALUE(LW(6), 0)
    END_IF
    CALL(btlevtcmd_StopAC)
    /* the fix: sparkles over Luigi and his HP back */
    CALL(tq_wrench_hp, LW(6), LW(5))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 50)
    CALL(evt_eff, STRING(""), STRING("recovery"), 4, LW(0), LW(1), LW(2), LW(5), 0, 0, 0, 0, 0, 0, 0)
    CALL(btlevtcmd_RecoverHp, LW(3), LW(4), LW(5))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PEACH_MACHINE_OPEN1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
    WAIT_FRAMES(40)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Spike Guard: a hop in front of Luigi, A as he lands, and the spikes come up */
static EVT_BEGIN(tq_spike_evt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 1, 1, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3A"))
    WAIT_FRAMES(12)
    CALL(btlevtcmd_StartAC, 1)
    /* the hop, over and down just in front of Luigi */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("J_1A"))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(0), 24)
    ADD(LW(2), 6)
    SET_VALUE(LW(8), 30)
    CALL(btlevtcmd_MarioJumpParam, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(8))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_MarioJumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(8), 0, 0)
    CALL(btlevtcmd_ac_timing_flag_onoff, 1, 1)
    CALL(btlevtcmd_ac_timing_get_success_frame, LW(0))
    DIVIDE(LW(0), 2)
    WAIT_FRAMES(LW(0))
    CALL(btlevtcmd_ac_timing_flag_onoff, 0, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(6))
    CALL(btlevtcmd_StopAC)
    CALL(btlevtcmd_JumpContinue, BTL_UNIT_SELF)
    IF_BITS_SET(LW(6), 2)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(7))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 20)
        CALL(btlevtcmd_ACSuccessEffect, LW(7), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(7))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3B"))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_MARIO_STAMP_THORN1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_btl_camera_shake_h, 0, 2, 0, 10, 13)
        CALL(tq_spike_on)
        WAIT_FRAMES(50)
    ELSE
        /* he lands off balance: no spikes this time */
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("D_4"))
        WAIT_FRAMES(40)
    END_IF
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* data tags: switched in (4), victory (64), revived (67) */
static EVT_BEGIN(tq_spawn_evt)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_PARTNER, 1)
    RETURN
EVT_END()

static EVT_BEGIN(tq_win_evt)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
    RETURN
EVT_END()

static EVT_BEGIN(tq_revive_evt)
    CALL(btlevtcmd_AnimeSetPoseTable, LW(10), LW(11), PTR(kBattlePoses))
    RETURN
EVT_END()

/* the Red Shells' exhibition: upright they're Defense 4, a flip lasts only till their turn, and Luigi acts
 * first: the Hammer and the Slide turned them over and back for ever with no harm. For that bout he goes through. */
static int sSlidePierce;
void portTorqueSlidePierce(int on) {
    sSlidePierce = on;
    if (on) {
        sSlide.special |= SPECIAL_PIERCES_DEFENSE;
    } else {
        sSlide.special &= ~SPECIAL_PIERCES_DEFENSE;
    }
}

static BattleUnitKind* torqueUnit(void) {
    int n = 0;
    /* Shell Slide: every grounded foe in turn; flips shelled ones */
    sSlide = partyWeapon_NokotarouSyubibinKoura;
    sSlide.name = "sl_tq_slide";
    sSlide.description = "sl_tq_slide_h";
    sSlide.baseFpCost = 0;
    sSlide.hpDamageWork[0] = 1, sSlide.hpDamageWork[1] = 2;
    sSlide.hpDamageWork[2] = 1, sSlide.hpDamageWork[3] = 2; /* he joins at rank 1: the plan's base */
    sSlide.hpDamageWork[4] = 2, sSlide.hpDamageWork[5] = 3;
    sSlide.special |= SPECIAL_FLIPS_SHELLED | (sSlidePierce ? SPECIAL_PIERCES_DEFENSE : 0);
    sSlide.attackEvtScript = tq_slide_evt;
    /* Wrench Work: Luigi only */
    sWrench = partyWeapon_NokotarouKouraGuard;
    sWrench.name = "sl_tq_wrench";
    sWrench.description = "sl_tq_wrench_h";
    sWrench.baseFpCost = 3;
    sWrench.attackEvtScript = tq_wrench_evt;
    /* Spike Guard: Luigi only */
    sSpike = partyWeapon_NokotarouKouraGuard;
    sSpike.name = "sl_tq_spike";
    sSpike.description = "sl_tq_spike_h";
    sSpike.baseFpCost = 4;
    sSpike.attackEvtScript = tq_spike_evt;

    sParts[0] = unitdata_Party_Nokotarou.parts[0];
    sParts[0].partName = "sl_name_torque";
    sParts[0].modelName = MODEL;
    sParts[0].poseTable = kBattlePoses;
    sParts[0].defense = &kDefense;
    sParts[0].attributes &= ~(PART_ATTR_SHELLED | PART_ATTR_GUARD_COVER);
    sData[n].tag = 5, sData[n++].data = nokotarou_appeal_event;
    sData[n].tag = 4, sData[n++].data = tq_spawn_evt;
    sData[n].tag = 1, sData[n++].data = (void*)makeMenu;
    sData[n].tag = 48, sData[n++].data = btldefaultevt_Dummy;
    sData[n].tag = 64, sData[n++].data = tq_win_evt;
    sData[n].tag = 7, sData[n++].data = subset_attack_ready_jump_event;
    sData[n].tag = 67, sData[n++].data = tq_revive_evt;
    sData[n].tag = 47, sData[n++].data = btldefaultevt_avoid_counter_event;
    sData[n].tag = 0, sData[n].data = NULL;
    sKind = unitdata_Party_Nokotarou;
    sKind.name = "sl_name_torque";
    sKind.parts = sParts;
    sKind.dataTable = sData;
    return &sKind;
}

/* ======================================================================
 * Tune-up: the gauge in the field
 * ====================================================================== */

static struct {
    int on;
    float value;    /* 0-100 */
    float shown;    /* eased toward value */
    int frame;
    int big;        /* the big one: a clock */
    int framesLeft;
} sGauge;

static void gaugeDisp(CameraId cameraId, void* param) {
    CameraEntry* cam = camGetPtr(CAMERA_2D);
    const float x = 30.0f, y = sGauge.big ? 60.0f : 0.0f;
    const float width = 176.0f;
    Vec pos = {0.0f, 0.0f, 0.0f};
    (void)cameraId, (void)param;
    if (!sGauge.on) {
        return;
    }
    /* the frame, the fill (green, flashing gold once full), the A button */
    pos.x = x - 200.0f;
    pos.y = y + 25.0f;
    iconDispGx(&pos, ICON_DISP_2D, ICON_08C, 1.0f);
    btlDispGXInit2DRasta();
    GXLoadPosMtxImm(cam->view, 0);
    btlDispGXQuads2DRasta(x - 289.0f, y + 50.0f, x - 289.0f + width, y + 32.0f, 0x21, 0x21, 0x75, 0xFF);
    if (sGauge.shown > 0.5f) {
        const int full = sGauge.shown >= 99.5f;
        const u8 g = full && (sGauge.frame / 4 & 1) ? 0xFF : 0xE4;
        btlDispGXQuads2DRasta(x - 289.0f, y + 50.0f, x - 289.0f + width * sGauge.shown / 100.0f, y + 32.0f,
                              full ? 0xFF : 0x00, g, full ? 0x40 : 0x86, 0xFF);
    }
    pos.x = x - 200.0f;
    pos.y = y + 70.0f;
    iconDispGx(&pos, ICON_DISP_2D, (sGauge.frame / 7 & 1) ? ICON_064 : ICON_065, 1.0f);
    if (sGauge.big) {
        /* the pit stop's clock: seconds left */
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (sGauge.framesLeft + 59) / 60);
        FontDrawStart();
        FontDrawMessage((s32)(x - 80.0f), (s32)(y + 72.0f), buf);
    }
}

/* in: big (0, 1); blocks until the gauge is full (out: 1), the gauge drawn meanwhile.
 * Normal: about 15 taps; it drains when not tapped. Big: about 25 taps in 15 seconds,
 * and if the clock runs out it empties and starts again. */
static USER_FUNC(tq_gauge) {
    const u32 trg = keyGetButtonTrg(0);
    if (isFirstCall) {
        memset(&sGauge, 0, sizeof(sGauge));
        sGauge.on = 1;
        sGauge.big = evtGetValue(event, event->args[0]) != 0;
        sGauge.framesLeft = 15 * 60;
    }
    sGauge.frame++;
    if (trg & PAD_BUTTON_A) {
        sGauge.value += sGauge.big ? 4.5f : 7.5f;
        psndSFXOn(sGauge.frame & 1 ? "SFX_STG7_CRANE_BUTTON_PUSH1" : "SFX_PEACH_MACHINE_SWITCH1");
    } else if (sGauge.value > 0.0f) {
        sGauge.value -= 0.25f;
    }
    if (sGauge.value < 0.0f) {
        sGauge.value = 0.0f;
    }
    if (sGauge.value > 100.0f) {
        sGauge.value = 100.0f;
    }
    sGauge.shown += (sGauge.value - sGauge.shown) * 0.35f;
    if (sGauge.big && --sGauge.framesLeft <= 0) {
        sGauge.value = sGauge.shown = 0.0f;
        sGauge.framesLeft = 15 * 60;
        psndSFXOn("SFX_AC_BUTTON_PUSH1");
    }
    dispEntry(CAMERA_2D, 900.0f, 1, gaugeDisp, NULL);
    if (sGauge.value >= 100.0f) {
        sGauge.shown = 100.0f;
        sGauge.on = 0;
        evtSetValue(event, event->args[1], 1);
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* out: the tune point near Luigi or Torque (LW: found, x, z, big) */
static USER_FUNC(tq_find) {
    EvtWord* a = event->args;
    MarioWork* m = marioGetPtr();
    PortSlPoint* p = portSlPointNear(PORT_SL_ABILITY_TUNE, m->position.x, m->position.z, NULL);
    (void)isFirstCall;
    if (p == NULL) {
        PartyEntry* party = partyGetPtr(marioGetPartyId());
        if (party != NULL) {
            p = portSlPointNear(PORT_SL_ABILITY_TUNE, party->position.x, party->position.z, NULL);
        }
    }
    evtSetValue(event, a[0], p != NULL ? (s32)(p - portSlPointGet(PORT_SL_ABILITY_TUNE, 0)) : -1);
    evtSetValue(event, a[1], p != NULL ? (s32)p->x : 0);
    evtSetValue(event, a[2], p != NULL ? (s32)p->z : 0);
    evtSetValue(event, a[3], p != NULL ? (s32)p->value : 0);
    return EVT_RETURN_DONE;
}

/* in: the point: its script started (LW(0) its id), and the point off */
static USER_FUNC(tq_run) {
    PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_TUNE, (int)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    printf("[torque] tune-up point %d done (%s)\n", (int)evtGetValue(event, event->args[0]),
           p != NULL && p->script != NULL ? "its script" : "no script");
    fflush(stdout);
    if (p != NULL) {
        portSlPointRun(PORT_SL_ABILITY_TUNE, p);
        p->on = 0;
    }
    return EVT_RETURN_DONE;
}

/* the lines when there's nothing to fix, in turn */
static USER_FUNC(tq_grumble) {
    static int sTurn;
    static const char* const kLines[] = {"sl_tq_notune_1", "sl_tq_notune_2", "sl_tq_notune_3"};
    (void)isFirstCall;
    evtSetValue(event, event->args[0], (EvtWord)(AddrWord)kLines[sTurn++ % 3]);
    return EVT_RETURN_DONE;
}

/* in: x, z: Torque scuttles there (the partner's own moves wait for a free run mode, which
 * his use holds, so his place is stepped here), blocking till he's there */
static USER_FUNC(tq_glide) {
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    const f32 tx = (f32)evtGetValue(event, event->args[0]);
    const f32 tz = (f32)evtGetValue(event, event->args[1]);
    f32 dx, dz, d;
    (void)isFirstCall;
    if (party == NULL) {
        return EVT_RETURN_DONE;
    }
    dx = tx - party->position.x;
    dz = tz - party->position.z;
    d = sqrtf(dx * dx + dz * dz);
    if (d <= 3.0f) {
        party->position.x = tx;
        party->position.z = tz;
        return EVT_RETURN_DONE;
    }
    party->position.x += dx / d * 3.0f;
    party->position.z += dz / d * 3.0f;
    return EVT_RETURN_BLOCK;
}

static EVT_BEGIN(tune_evt)
    CALL(tq_find, LW(10), LW(0), LW(2), LW(11))
    IF_INT_EQUAL(LW(10), -1)
        CALL(tq_grumble, LW(0))
        CALL(evt_msg_print, 0, LW(0), 0, STRING("party"))
        CALL(evt_sl_ability_done)
        RETURN
    END_IF
    CALL(evt_mario_key_onoff, 0)
    /* he scuttles over and gets under it */
    CALL(evt_party_set_pose, 0, STRING("W_1"))
    CALL(tq_glide, LW(0), LW(2))
    CALL(evt_party_set_pose, 0, STRING("F_1"))
    CALL(evt_snd_sfxon, STRING("SFX_PEACH_MACHINE_MOVE1"), 0)
    CALL(tq_gauge, LW(11), LW(12))
    /* clank: fixed */
    CALL(evt_snd_sfxon, STRING("SFX_PEACH_MACHINE_OPEN1"), 0)
    CALL(evt_party_set_pose, 0, STRING("F_2"))
    CALL(evt_mario_get_pos, 0, LW(3), LW(4), LW(5))
    CALL(evt_eff, 0, STRING("kemuri_test"), 0, LW(0), 10, LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    WAIT_MS(500)
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_mario_key_onoff, 1)
    /* the field is let go first: the point's script is an ordinary event (the
     * ability's runs while the field is held, which would hold it too) */
    CALL(evt_sl_ability_done)
    WAIT_FRAMES(8)
    CALL(tq_run, LW(10))
    RETURN
EVT_END()

static void torqueUse(PartyEntry* party) {
    portSlFieldUse(party, tune_evt);
}

static void swapped(int on) {
    (void)on;
    sSpikeOn = 0;
}

static const char* const kText[] = {
    "sl_name_torque", "Torque",
    "sl_tq_slide", "Shell Slide",
    "sl_tq_slide_h", "Torque tucks in and slides along\nthe ground through every foe.\nFlips shelled foes over.",
    "sl_tq_wrench", "Wrench Work",
    "sl_tq_wrench_h", "Torque tunes Luigi up with his\nwrench. Press the buttons shown,\nin order, for more HP.",
    "sl_tq_spike", "Spike Guard",
    "sl_tq_spike_h", "Torque bristles in front of\nLuigi. A foe landing on Luigi\nmeets the spikes instead.",
    "sl_tq_notune_1", "Nothing to fix.\nExcept your posture.\n<k>",
    "sl_tq_notune_2", "Looks fine to me.\n<k>",
    "sl_tq_notune_3", "If it ain't broke,\ndon't make me.\n<k>",
    "sl_tq_tuneup_help", "<system>\n<col 0000ffff>Tune-up</col>: stand by a broken\nmachine and press <icon PAD_X 0.6 0 22 0>. Torque gets\nunder it with his wrench.\n<k>\n<p>\nTap <icon PAD_A 0.6 0 22 0> to keep him working\nuntil the gauge fills. A machine\nyou've fixed stays fixed.\n<k>",
    NULL,
};

static const PortSlFriend kTorque = {
    PORT_SL_FRIEND_TORQUE,
    SLOT,
    "Torque",
    MODEL,
    "Torque: a Buzzy Beetle mechanic.\nBuilds things. Fixes things.\nDoesn't talk about feelings.",
    kPoses,
    torqueUse,
    NULL,
    "S_1",
    torqueUnit,
    sWaza,
    "art/icons/torque/torque_menu.png",
    "art/icons/torque/torque_hud.png",
    10,
    1,
    kText,
    swapped,
};

const PortSlFriend* portSlFriendTorque(void) {
    return &kTorque;
}
