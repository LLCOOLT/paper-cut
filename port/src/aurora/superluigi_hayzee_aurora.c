/* Hayzee, Luigi's friend from chapter 4 (docs/super_luigi/ch4.md, "Hayzee"):
 * a Dayzee theatre producer, a partner of Luigi's in Flurrie's slot (the
 * framework swaps him in, port/include/port/superluigi.h's PortSlFriend). He
 * walks (the slot's floating field code is swapped for the walking one while
 * he's in it) and has a battle unit and moves of his own.
 *
 * Field: Sing. A choice of the songs he knows (Shake a Leaf, Rise and Shine,
 * and Echo once Petunia has taught it): he sings, and whatever listens within
 * a sing point's radius (PORT_SL_ABILITY_SING; value 0 any song, 1 Shake a
 * Leaf, 2 Rise and Shine, 3 Echo) runs its script; field enemies nearby
 * dance on the spot for five seconds (touch one for a first strike).
 * Echo: a point's value is 3 | phrase << 8 (four notes, two bits each: A B X
 * Y); evt_sl_hz_echo plays a phrase and has the player sing it back.
 *
 * Battle: Lullaby (sleep on every foe, four beats of A), Encore (Luigi acts
 * again, the gauge stopped in its band) and Showstopper (every foe hit by
 * the audience, A mashed), his own move scripts on the game's action
 * commands; Lie Low only in the Drama Slam (superluigi_slam_aurora.c). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "battle/battle_audience.h"
#include "battle/battle_database_common.h"
#include "battle/battle_sub.h"
#include "evt/evt_audience.h"
#include "battle/battle_camera.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/party_data.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "party/party_kuribo.h"
#include "party/party_move.h"
#include "unit/party/unit_party_clauda.h"
#include "win/win_main.h"
#include "driver/msgdrv.h"
#include "pmario_sound.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <string.h>


extern BattleUnitKindPart unitpartsdata_Party_Clauda[];
#include "driver/swdrv.h"
#define SLOT 5 /* Flurrie's: partyDataTbl, partyPoseTbl, pouch partyData */
#define F_ECHO SL_GSWF(4, 90) /* chapter 4: Petunia taught him Echo (superluigi_ch04_aurora.c) */

/* Hayzee's poses by the field pose index (partyPoseTbl's columns) */
static char* const kPoses[15] = {"Z_1", "S_1", "W_1", "R_1", "J_1A", "J_1B", "N_1", "T_1",
                                 "D_1", "D_4", "S_1", "S_1", "D_2", "W_2", "R_2"};

static PoseTableEntry kBattlePoses[] = {
    {9, "D_5"}, {2, "D_5"}, {3, "X_1"}, {4, "D_2"}, {5, "X_1"}, {1, "N_1"}, {27, "D_3"},
    {BTL_POSE_NORMAL, "S_1"}, {37, "G_2"}, {31, "G_1"}, {BTL_POSE_DAMAGE, "D_1"},
    {BTL_POSE_WALK, "W_1"}, {BTL_POSE_RUN, "R_1"}, {BTL_POSE_PERIL, "D_2"}, {BTL_POSE_DANGER, "D_2"},
    {BTL_POSE_ESCAPE_FAIL, "D_4"}, {BTL_POSE_ESCAPE_FAIL_END, "D_2"}, {BTL_POSE_TALK, "T_1"},
    {BTL_POSE_ITEM_USE, "I_1"}, {BTL_POSE_COMMAND_WAIT, "C_1"}, {BTL_POSE_APPEAL, "O_1"},
    {BTL_POSE_ZERO_GRAVITY, "J_1B"}, {49, "J_1A"}, {BTL_POSE_DEFEND, "G_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};

static BattleWeapon sSoftShoe, sLullaby, sEncore, sShow, sLieLow, sApplied;
static BattleWeapon* sWaza[4] = {&sSoftShoe, &sLullaby, &sEncore, &sShow};
static BattleUnitKind sKind;
static BattleUnitKindPart sParts[1];
static BattleDataEntry sData[16];

int portSlSlamBattle(void);          /* superluigi_slam_aurora.c: the Drama Slam's on (weak 0 below) */
int portSlSlamHouse(void);           /* the House, or -1 */
void portSlSlamLieLow(void);         /* Lie Low: the next attack on Luigi this turn misses */
int portSlSlamLieLowUsed(void);      /* once an act */
__attribute__((weak)) int portSlSlamBattle(void) { return 0; }
__attribute__((weak)) int portSlSlamHouse(void) { return -1; }
__attribute__((weak)) void portSlSlamLieLow(void) {}
__attribute__((weak)) int portSlSlamLieLowUsed(void) { return 1; }

const BattleWeapon* portSlHayzeeLullaby(void) {
    return &sLullaby;
}

static int rank(void) {
    return pouchGetPtr()->partyData[SLOT].attackLevel;
}

/* --- the battle menu --- */

static void addMove(BattleCommand* command, s32* count, BattleWeapon* weapon) {
    command->weapon[*count].index = -1;
    command->weapon[*count].itemId = 0;
    command->weapon[*count].weapon = weapon;
    command->weapon[*count].enabled = 0;
    command->weapon[*count].icon = weapon->icon;
    command->weapon[*count].name = msgSearch(weapon->name);
    (*count)++;
}

/* unit data tag 1: Soft Shoe, Lullaby, Encore, Showstopper; Lie Low in the Drama Slam */
static void makeMenu(BattleCommand* command, s32* count) {
    sEncore.baseFpCost = rank() >= 2 ? 3 : 4;
    addMove(command, count, &sSoftShoe);
    addMove(command, count, &sLullaby);
    addMove(command, count, &sEncore);
    addMove(command, count, &sShow);
    if (portSlSlamBattle() && !portSlSlamLieLowUsed()) {
        addMove(command, count, &sLieLow);
    }
}

/* --- the moves' own functions --- */

/* in: the Lullaby's beats hit (0-4): the weapon applied, with that sleep chance */
static USER_FUNC(hz_lullaby_weapon) {
    s32 hits = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    sApplied = sLullaby;
    sApplied.sleepChance = (u8)(hits >= 4 ? 100 : hits * 25);
    sApplied.sleepTime = (s8)(rank() >= 1 ? 3 : 2);
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)&sApplied);
    return EVT_RETURN_DONE;
}

/* in: the mash's level (AirGauge output 0: 0-4): the Showstopper as it lands, its damage
 * 1 + 1 per 50 in the House (or the audience), +1 at the top level, +1 at Ultra rank */
static USER_FUNC(hz_show_weapon) {
    s32 level = evtGetValue(event, event->args[0]);
    s32 house = portSlSlamHouse();
    s32 crowd = house >= 0 ? house : BattleAudience_GetAudienceNum();
    s32 dmg = 1 + crowd / 50 + (level >= 4 ? 1 : 0) + (rank() >= 2 ? 1 : 0);
    (void)isFirstCall;
    if (level <= 0) {
        dmg = 1;
    }
    sApplied = sShow;
    memset(sApplied.hpDamageWork, 0, sizeof(sApplied.hpDamageWork));
    sApplied.hpDamageWork[0] = dmg;
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)&sApplied);
    printf("[superluigi] Showstopper: level %d, crowd %d, damage %d\n", (int)level, (int)crowd, (int)dmg);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* in: a unit: it acts again this turn (Goombella's Rally Wink does the same) */
static USER_FUNC(hz_encore) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit = BattleGetUnitPtr(wp, BattleTransID(event, evtGetValue(event, event->args[0])));
    (void)isFirstCall;
    if (unit != NULL) {
        if (unit->movesRemaining <= 0) {
            unit->movesRemaining++;
            unit->moveState = 0;
            unit->moveColorLv = 0xFF;
            if (unit->maxMovesThisTurn <= 0) {
                unit->maxMovesThisTurn = 1;
                if (BtlUnit_CheckStatus(unit, STATUS_SLOW)) {
                    unit->tokenFlags |= TOKEN_FLAG_RALLY_WINK;
                }
            }
        } else {
            BtlUnit_OnStatusFlag(unit, STATUS_FLAG_EXTRA_MOVE);
        }
    }
    return EVT_RETURN_DONE;
}

static USER_FUNC(hz_lie_low) {
    (void)event;
    (void)isFirstCall;
    portSlSlamLieLow();
    return EVT_RETURN_DONE;
}

/* --- the battle unit's scripts --- */

static EVT_BEGIN(hz_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hz_damage)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

static EVT_BEGIN(hz_attack)
    RETURN
EVT_END()

/* he walks in from the wing, with a little hop */
static EVT_BEGIN(hz_entry)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 250)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

static EVT_BEGIN(hz_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(hz_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(hz_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(hz_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(btldefaultevt_Confuse))
    CALL(btlevtcmd_SetWalkSound, BTL_UNIT_SELF, STRING("SFX_FLD_ENM_PANZI_MOVE1L"), STRING("SFX_FLD_ENM_PANZI_MOVE1R"), 0, 15, 15)
    CALL(btlevtcmd_SetRunSound, BTL_UNIT_SELF, STRING("SFX_FLD_ENM_PANZI_MOVE1L"), STRING("SFX_FLD_ENM_PANZI_MOVE1R"), 0, 8, 8)
    CALL(btlevtcmd_SetJumpSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), STRING("SFX_PARTY_BATTLE_LANDING1"))
    CALL(btlevtcmd_SetEventEntry, BTL_UNIT_SELF, PTR(hz_entry))
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Appeal (data tag 5): a twirl and jazz leaves */
static EVT_BEGIN(hz_appeal)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_1"))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_MARIO_APPEAL1"), LW(0), LW(1), LW(2), 0)
    ADD(LW(1), 30)
    CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AudienceAppeal, BTL_UNIT_SELF)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Victory (data tag 64): the bow */
static EVT_BEGIN(hz_win)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_2"))
    CALL(btlevtcmd_AnimeWaitPlayComplete, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
    RETURN
EVT_END()

/* The notes a song sends up: LW(15) frames of them, from his head */
static EVT_BEGIN(hz_notes)
    LOOP(LW(15))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 26)
        CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
        WAIT_FRAMES(14)
    END_LOOP
    RETURN
EVT_END()

/* The move's end: a stylish window, the audience's share, back to his place */
static EVT_BEGIN(hz_move_end)
    CALL(btlevtcmd_ACRStart, BTL_UNIT_SELF, 0, 25, 25, 20)
    CALL(btlevtcmd_ACRGetResult, LW(6), LW(7))
    IF_INT_NOT_EQUAL(LW(6), 2)
        CALL(evt_audience_acrobat_notry)
    ELSE
        CALL(btlevtcmd_AudienceDeclareAcrobatResult, LW(12), 1, 0, 0, 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_2"))
        WAIT_MS(800)
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 0)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* Lullaby: a slow swaying song; A on each of four beats. Every foe it reaches
 * may fall asleep: a quarter of a chance a beat hit, all four sure. */
static EVT_BEGIN(hz_lullaby_evt)
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
    WAIT_FRAMES(20)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    /* four signals a beat apart, A each (the game's signal timing) */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcSetParamAll, 4, 1, 40, 1, 0, 0, 2, 4)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 18, 1, 0)
    /* the stage dims as he draws a breath (A_1A); then he sways and sings */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    SET_VALUE(LW(0), 255)
    LOOP(0)
        IF_INT_LESS(LW(0), 200)
            BREAK_LOOP
        END_IF
        CALL(evt_map_set_blend, 0, LW(0), LW(0), 255, 255)
        SUBTRACT(LW(0), 3)
        WAIT_FRAMES(1)
    END_LOOP
    WAIT_FRAMES(8)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_PANSY_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    BEGIN_SIBLING_EVENT_GET_ID(LW(14))
        SET_VALUE(LW(15), 30)
        CALL_CHILD(hz_notes)
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 0, LW(5))
    CALL(btlevtcmd_StopAC)
    DELETE_EVENT(LW(14))
    /* "sleep tight" (A_1B) as they nod off */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    CALL(hz_lullaby_weapon, LW(5), LW(12))
    IF_INT_GREATER_OR_EQUAL(LW(5), 3)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(6))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    /* every foe it reaches */
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    LABEL(10)
    IF_INT_EQUAL(LW(3), -1)
        GOTO(20)
    END_IF
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(6))
    IF_INT_EQUAL(LW(6), 1)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(7))
    END_IF
    WAIT_MS(200)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    GOTO(10)
    LABEL(20)
    SET_VALUE(LW(0), 200)
    LOOP(0)
        IF_INT_GREATER_OR_EQUAL(LW(0), 256)
            BREAK_LOOP
        END_IF
        CALL(evt_map_set_blend, 0, LW(0), LW(0), 255, 255)
        ADD(LW(0), 3)
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_map_blend_off, 0)
    CALL_CHILD(hz_move_end)
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Encore: the spotlight swells on Luigi; A stops the gauge in its band, and
 * Luigi takes another bow (another move this turn) */
static EVT_BEGIN(hz_encore_evt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    /* the wind-up: a leaf flung out at Luigi (A_2A); then the cheer (A_2) while the gauge swells */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_AUDIENCE_PANSY_SINGING1"), EVT_NULLPTR, 0, LW(13))
    /* the gauge: the game's gauge timing (AC 19), A (type 1), the gauge
     * swelling and ebbing (mode 1, a swell each 40 frames): A while it's
     * over 80 lights the star. (Mode 0 was a marker running off from the
     * band's middle: only a press on the first frame could land.) */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcSetParamAll, 1, 240, 1, 80, 80, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 19, 1, 0)
    WAIT_FRAMES(22)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(6))
    CALL(btlevtcmd_StopAC)
    CALL(evt_snd_sfxoff, LW(13))
    IF_BITS_SET(LW(6), 2)
        /* landed: a twirl, presenting him (A_2B) */
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_SYSTEM, 0, LW(7))
        CALL(btlevtcmd_ACSuccessEffect, LW(7), LW(0), LW(1), LW(2))
        CALL(evt_eff, 0, STRING("recovery"), 6, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_snd_sfxon_3d, STRING("SFX_AUDIENCE_CLAPPING1"), LW(0), LW(1), LW(2), 0)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(7))
        CALL(hz_encore, LW(3))
        WAIT_FRAMES(30)
        CALL(evt_msg_print, 2, STRING("sl_hz_encore_ok"), 0, BTL_UNIT_SELF)
    ELSE
        /* missed: the cheer goes out of him (A_2C) */
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2C"))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        WAIT_FRAMES(20)
        CALL(evt_msg_print, 2, STRING("sl_hz_encore_miss"), 0, BTL_UNIT_SELF)
    END_IF
    CALL_CHILD(hz_move_end)
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Soft Shoe, his free move, as every partner has one: he hops over and taps the foe with a heel;
 * A as he lands (the game's timed press, as Goombella's Headbonk) makes it count */
static EVT_BEGIN(hz_soft_evt)
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
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 1, 1, 0)
    /* up to the foe */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 45)
    CALL(btlevtcmd_TransStageFloorPosition, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetMoveFrame, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, LW(14))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(14), -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(6))
    /* the hop: the command's window is as he lands */
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("J_1A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 12)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.5f))
    /* up on J_1A, down on J_1B from the top (the jump's 20 frames: its peak at 10) */
    BEGIN_SIBLING_EVENT_GET_ID(LW(13))
        WAIT_FRAMES(10)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("J_1B"))
    END_SIBLING_EVENT
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 20, -1)
    DELETE_EVENT(LW(13))
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(7))
    CALL(btlevtcmd_StopAC)
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
        WAIT_FRAMES(20)
        GOTO(90)
    END_IF
    /* the heel (his own A_4: it played the Showstopper's finale) */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_4"))
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    WAIT_FRAMES(20)
    LABEL(90)
    /* back to his mark */
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.69921875f))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetMoveFrame, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, LW(14))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(14), -1, 0)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(hz_move_end)
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Showstopper: he winds up, conducts, and the crowd sings along while A is
 * mashed; every foe takes the audience's damage */
static EVT_BEGIN(hz_show_evt)
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
    WAIT_FRAMES(20)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3A"))
    CALL(btlevtcmd_AnimeWaitPlayComplete, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(1))
    CALL(btlevtcmd_AcSetParamAll, 1, 240, 4, 30, 55, 75, 95, LW(1))
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 14, 1, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3C"))
    CALL(evt_btl_camera_set_zoom, 0, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_AUDIENCE_PANSY_SINGING1"), EVT_NULLPTR, 0, LW(13))
    BEGIN_SIBLING_EVENT_GET_ID(LW(14))
        SET_VALUE(LW(15), 40)
        CALL_CHILD(hz_notes)
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 0, LW(5))
    CALL(btlevtcmd_StopAC)
    DELETE_EVENT(LW(14))
    CALL(evt_snd_sfxoff, LW(13))
    /* the finale: head thrown back, the house roars */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3B"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_CHEER_ALLY2"), 0)
    CALL(evt_btl_camera_shake_h, 0, 3, 0, 30, 13)
    CALL(hz_show_weapon, LW(5), LW(12))
    IF_INT_GREATER_OR_EQUAL(LW(5), 3)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_NONE, 0, LW(6))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    LABEL(10)
    IF_INT_EQUAL(LW(3), -1)
        GOTO(20)
    END_IF
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(6))
    IF_INT_EQUAL(LW(6), 1)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(7))
    END_IF
    WAIT_FRAMES(12)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    GOTO(10)
    LABEL(20)
    WAIT_MS(400)
    CALL_CHILD(hz_move_end)
    LABEL(99)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Lie Low (the Drama Slam only): Hayzee cues the lights off the Grass */
static EVT_BEGIN(hz_lielow_evt)
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("T_1"))
    CALL(evt_msg_print, 2, STRING("sl_hz_lielow"), 0, BTL_UNIT_SELF)
    CALL(evt_snd_sfxon, STRING("SFX_STG6_CROSSING_OPEN1"), 0)
    CALL(evt_map_set_blend, 0, 150, 150, 170, 255)
    WAIT_MS(500)
    CALL(evt_map_blend_off, 0)
    CALL(hz_lie_low)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static BattleUnitKind* hayzeeUnit(void) {
    const BattleDataEntry* d;
    int n = 0;
    /* Soft Shoe: one foe, free; Flurrie's Body Slam's numbers */
    sSoftShoe = partyWeapon_ClaudaNormalAttack;
    sSoftShoe.name = "sl_hz_soft";
    sSoftShoe.description = "sl_hz_soft_h";
    sSoftShoe.acHelpMsg = "sl_hz_soft_ac";
    sSoftShoe.attackEvtScript = hz_soft_evt;
    /* Lullaby: every foe, no damage, sleep */
    sLullaby = partyWeapon_ClaudaBreathAttack;
    sLullaby.name = "sl_hz_lullaby";
    sLullaby.description = "sl_hz_lullaby_h";
    sLullaby.acHelpMsg = "sl_hz_lullaby_ac";
    sLullaby.baseFpCost = 3;
    sLullaby.hpDamageFn = NULL;
    memset(sLullaby.hpDamageWork, 0, sizeof(sLullaby.hpDamageWork));
    sLullaby.galeForceChance = 0;
    sLullaby.sleepChance = 100;
    sLullaby.sleepTime = 2;
    sLullaby.special = SPECIAL_UNGUARDABLE;
    sLullaby.attackEvtScript = hz_lullaby_evt;
    /* Encore: Luigi, another move */
    sEncore = partyWeapon_ClaudaKumogakureAttack;
    sEncore.name = "sl_hz_encore";
    sEncore.description = "sl_hz_encore_h";
    sEncore.acHelpMsg = "sl_hz_encore_ac";
    sEncore.baseFpCost = 4;
    sEncore.dodgyChance = 0;
    sEncore.dodgyTime = 0;
    sEncore.attackEvtScript = hz_encore_evt;
    /* Showstopper: every foe, the audience's damage */
    sShow = partyWeapon_ClaudaBreathAttack;
    sShow.name = "sl_hz_show";
    sShow.description = "sl_hz_show_h";
    sShow.acHelpMsg = "sl_hz_show_ac";
    sShow.baseFpCost = 5;
    sShow.hpDamageFn = weaponGetPowerDefault;
    memset(sShow.hpDamageWork, 0, sizeof(sShow.hpDamageWork));
    sShow.hpDamageWork[0] = 2;
    sShow.galeForceChance = 0;
    sShow.special = SPECIAL_PIERCES_DEFENSE | SPECIAL_UNGUARDABLE | SPECIAL_BADGE_BUFFABLE;
    sShow.attackEvtScript = hz_show_evt;
    /* Lie Low (the Drama Slam) */
    sLieLow = sEncore;
    sLieLow.name = "sl_hz_lielow_n";
    sLieLow.description = "sl_hz_lielow_h";
    sLieLow.acHelpMsg = NULL;
    sLieLow.baseFpCost = 0;
    sLieLow.attackEvtScript = hz_lielow_evt;

    sParts[0] = unitpartsdata_Party_Clauda[0];
    sParts[0].partName = "sl_name_hayzee";
    sParts[0].modelName = "c_lp_pansy";
    sParts[0].poseTable = kBattlePoses;
    for (d = unitdata_Party_Clauda.dataTable; d != NULL && d->tag != 0 && n < 15; d++) {
        sData[n] = *d;
        if (d->tag == 1) {
            sData[n].data = (void*)makeMenu;
        } else if (d->tag == 5) {
            sData[n].data = (void*)hz_appeal;
        } else if (d->tag == 64) {
            sData[n].data = (void*)hz_win;
        }
        n++;
    }
    sData[n].tag = 0;
    sData[n].data = NULL;
    sKind = unitdata_Party_Clauda;
    sKind.name = "sl_name_hayzee";
    sKind.parts = sParts;
    sKind.dataTable = sData;
    sKind.initEventCode = hz_init;
    sKind.attributeFlags &= ~UNIT_ATTR_FLOATING;
    sKind.height = 30;
    sKind.width = 22;
    return &sKind;
}

/* ======================================================================
 * Sing, in the field
 * ====================================================================== */

enum { SONG_SHAKE = 1, SONG_RISE = 2, SONG_ECHO = 3 };

static int sSong;      /* the song sung last */
static int sQuip;      /* the "no audience" lines, in turn */
static int sSongOrder[4];
static int sDanceT;    /* frames the field's dancers have left */
static char sDancers[8][32];
static int sDancerCount;

/* out: the song sung (sing points' scripts ask) */
USER_FUNC(evt_sl_hz_song) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sSong);
    return EVT_RETURN_DONE;
}

/* the song list: the songs he knows, and "Not now" (the cancel) */
static USER_FUNC(sing_menu) {
    static char text[256];
    int n = 0;
    (void)isFirstCall;
    sSongOrder[n++] = SONG_SHAKE;
    sSongOrder[n++] = SONG_RISE;
    if (swGet(SL_FLAG(4, 90))) {
        sSongOrder[n++] = SONG_ECHO;
    }
    /* one row a line: Echo, once learnt, under Rise and Shine (a blank row
     * between them put Echo a row down from where the cursor lands) */
    snprintf(text, sizeof(text), "<select 0 %d 0 40>\n%s\n%s%s%s\n%s", n, msgSearch("sl_hz_song_1"),
             msgSearch("sl_hz_song_2"), n > 2 ? "\n" : "", n > 2 ? msgSearch("sl_hz_song_3") : "",
             msgSearch("sl_hz_song_no"));
    portModsSetMessage("sl_hz_songs", text);
    evtSetValue(event, event->args[0], n);
    return EVT_RETURN_DONE;
}

/* in: the choice (0..n): the song, or 0 for none */
static USER_FUNC(sing_pick) {
    int i = evtGetValue(event, event->args[0]);
    int n = evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    sSong = i >= 0 && i < n ? sSongOrder[i] : 0;
    evtSetValue(event, event->args[2], sSong);
    return EVT_RETURN_DONE;
}

static void setAnim(NpcEntry* npc, const char* anim) {
    char name[32];
    snprintf(name, sizeof(name), "%s", anim);
    strcpy(npc->currentAnim, name);
    animPoseSetAnim(npc->poseId, npc->currentAnim, 1);
}

static int songMatches(const PortSlPoint* p) {
    int tune = (int)(p->value & 0xFF);
    return tune == 0 || tune == sSong;
}

/* out: the sing point that listens (its script run), 1; or 0 with a quip
 * tag in LW. Field enemies near Hayzee dance (Shake a Leaf). */
static USER_FUNC(sing_find) {
    MarioWork* m = marioGetPtr();
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    float x = party != NULL ? party->position.x : m->position.x;
    float z = party != NULL ? party->position.z : m->position.z;
    PortSlPoint* best = NULL;
    float bestD = 1e9f;
    int i, danced = 0;
    static const char* const kQuips[3] = {"sl_hz_nobody_1", "sl_hz_nobody_2", "sl_hz_nobody_3"};
    (void)isFirstCall;
    for (i = 0; i < PORT_SL_POINTS; i++) {
        PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_SING, i);
        float d1 = hypotf(m->position.x - p->x, m->position.z - p->z);
        float d2 = hypotf(x - p->x, z - p->z);
        float d = d1 < d2 ? d1 : d2;
        if (p->on && songMatches(p) && d <= p->radius && d < bestD) {
            best = p, bestD = d;
        }
    }
    /* Shake a Leaf: the field enemies near him dance on the spot */
    if (sSong == SONG_SHAKE) {
        NpcWork* w = npcGetWorkPtr();
        sDancerCount = 0;
        for (i = 0; i < w->total && sDancerCount < 8; i++) {
            NpcEntry* npc = &w->entries[i];
            if (!(npc->flags & NPC_FLAG_USED) || !(npc->flags & NPC_FLAG_ENEMY) || (npc->reactionFlags & NPC_STATUS_DEAD)) {
                continue;
            }
            if (hypotf(npc->position.x - x, npc->position.z - z) > 220.0f) {
                continue;
            }
            npc->reactionFlags |= NPC_STATUS_FROZEN;
            npc->flags |= NPC_FLAG_NO_FIND_EVENTS;
            if (npc->talkAnim[0]) {
                setAnim(npc, npc->talkAnim);
            }
            snprintf(sDancers[sDancerCount++], sizeof(sDancers[0]), "%s", npc->description);
            danced++;
        }
        if (danced) {
            sDanceT = 300;
        }
    }
    if (best != NULL) {
        portSlPointRun(PORT_SL_ABILITY_SING, best);
        evtSetValue(event, event->args[0], 1);
        evtSetValue(event, event->args[1], 0);
    } else {
        evtSetValue(event, event->args[0], danced ? 2 : 0);
        evtSetValue(event, event->args[1], (EvtWord)(AddrWord)(danced ? "sl_hz_dance" : kQuips[sQuip++ % 3]));
    }
    return EVT_RETURN_DONE;
}

/* a dancing enemy touched: Luigi strikes first (fbatHitCheck) */
static int dancing(const NpcEntry* npc) {
    int i;
    if (sDanceT <= 0 || npc == NULL) {
        return 0;
    }
    for (i = 0; i < sDancerCount; i++) {
        if (strcmp(sDancers[i], npc->description) == 0) {
            return 1;
        }
    }
    return 0;
}

static void hitCheckPost(NpcEntry** result, u32 flags, void* unused) {
    NpcEntry* npc = *result;
    (void)flags, (void)unused;
    if (npc != NULL && dancing(npc) &&
        (npc->fbatHitKind == FBAT_HIT_CONTACT || npc->fbatHitKind == FBAT_HIT_ENEMY_FIRST)) {
        npc->fbatHitKind = FBAT_HIT_JUMP;
        fbatGetPointer()->hitKind = FBAT_HIT_JUMP;
    }
}

/* each frame: the dancers' five seconds */
static void danceFrame(void) {
    int i;
    if (sDanceT <= 0 || gp == NULL) {
        return;
    }
    if (--sDanceT > 0 && !gp->inBattle) {
        if (sDanceT % 20 == 0) {
            for (i = 0; i < sDancerCount; i++) {
                NpcEntry* npc = npcNameToPtr_NoAssert(sDancers[i]);
                if (npc != NULL) {
                    npc->scale.x = -npc->scale.x; /* a paper twirl, on the beat */
                }
            }
        }
        return;
    }
    for (i = 0; i < sDancerCount; i++) {
        NpcEntry* npc = npcNameToPtr_NoAssert(sDancers[i]);
        if (npc != NULL) {
            npc->reactionFlags &= ~NPC_STATUS_FROZEN;
            npc->flags &= ~NPC_FLAG_NO_FIND_EVENTS;
            npc->scale.x = fabsf(npc->scale.x);
            if (npc->stayAnim[0]) {
                setAnim(npc, npc->stayAnim);
            }
        }
    }
    sDanceT = 0;
    sDancerCount = 0;
}

/* --- Echo --- */

static const char* const kNoteIcon[4] = {"PAD_A", "PAD_B", "PAD_X", "PAD_Y"};
static const int kNotePitch[4] = {0, 300, 700, 1000};

/* in: the phrase (four notes, two bits each, first in the low bits): the
 * prompt shows the notes as they're sung back; out: 1 sung right, 0 wrong.
 * Each press sings its note (Hayzee), a wrong one sours. */
USER_FUNC(evt_sl_hz_echo) {
    static int pos;
    static int wait;
    int phrase = evtGetValue(event, event->args[0]);
    u32 trg;
    static const u32 kButtons[4] = {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y};
    int i;
    if (isFirstCall) {
        /* the prompt shows the stone's phrase, the buttons in its order (heard
         * as four pitches too: A lowest, Y highest) */
        static char text[160];
        int n;
        snprintf(text, sizeof(text), "%s", msgSearch("sl_hz_echo_prompt"));
        for (n = 0; n < 4; n++) {
            char note[40];
            snprintf(note, sizeof(note), " <icon %s 0.6 0 22 0>", kNoteIcon[(phrase >> (n * 2)) & 3]);
            strncat(text, note, sizeof(text) - strlen(text) - 1);
        }
        portModsSetMessage("sl_hz_echo_now", text);
        pos = 0;
        wait = 12;
        portSlPrompt("sl_hz_echo_now", 600000);
    }
    if (wait > 0) {
        wait--;
        return EVT_RETURN_BLOCK;
    }
    trg = keyGetButtonTrg(0);
    for (i = 0; i < 4; i++) {
        if (trg & kButtons[i]) {
            int want = (phrase >> (pos * 2)) & 3;
            s32 id;
            if (i != want) {
                id = psndSFXOn("SFX_AC_FAIL1");
                (void)id;
                portSlPrompt(NULL, 0);
                evtSetValue(event, event->args[1], 0);
                return EVT_RETURN_DONE;
            }
            id = psndSFXOn("SFX_AUDIENCE_PANSY_SINGING1");
            psndSFX_pit(id, (s16)kNotePitch[i]);
            if (++pos >= 4) {
                portSlPrompt(NULL, 0);
                evtSetValue(event, event->args[1], 1);
                return EVT_RETURN_DONE;
            }
            wait = 8;
            break;
        }
    }
    return EVT_RETURN_BLOCK;
}

/* in: the phrase: the stone plays its four notes, a beat apart */
USER_FUNC(evt_sl_hz_echo_play) {
    static int t;
    int phrase = evtGetValue(event, event->args[0]);
    if (isFirstCall) {
        t = 0;
    }
    if (t % 30 == 0 && t / 30 < 4) {
        s32 id = psndSFXOn("SFX_STAR_POINT_GET1");
        psndSFX_pit(id, (s16)kNotePitch[(phrase >> (t / 30 * 2)) & 3]);
    }
    return ++t >= 4 * 30 + 10 ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* Hayzee's ability: pick a song; he sings; whatever listens acts */
static EVT_BEGIN(sing_evt)
    CALL(evt_mario_key_onoff, 0)
    CALL(sing_menu, LW(9))
    CALL(evt_msg_print, 0, STRING("sl_hz_which"), 0, STRING("party"))
    CALL(evt_msg_select, 0, STRING("sl_hz_songs"))
    CALL(evt_msg_continue)
    CALL(sing_pick, LW(0), LW(9), LW(8))
    IF_INT_EQUAL(LW(8), 0)
        CALL(evt_mario_key_onoff, 1)
        CALL(evt_sl_ability_done)
        RETURN
    END_IF
    /* he sings: the bright song, notes rising, the Dayzees' singing */
    CALL(evt_party_set_pose, 0, STRING("F_1"))
    CALL(evt_snd_sfxon, STRING("SFX_AUDIENCE_PANSY_SINGING1"), LW(13))
    LOOP(8)
        CALL(evt_party_get_pos, 0, LW(0), LW(1), LW(2))
        ADD(LW(1), 30)
        CALL(evt_eff64, STRING(""), STRING("onpu_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
        WAIT_FRAMES(15)
    END_LOOP
    CALL(evt_party_set_pose, 0, STRING("F_2"))
    WAIT_FRAMES(30)
    CALL(evt_snd_sfxoff, LW(13))
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(sing_find, LW(0), LW(1))
    IF_INT_NOT_EQUAL(LW(0), 1)
        CALL(evt_msg_print, 0, LW(1), 0, STRING("party"))
    END_IF
    CALL(evt_mario_key_onoff, 1)
    CALL(evt_sl_ability_done)
    RETURN
EVT_END()

static void hayzeeUse(PartyEntry* party) {
    portSlFieldUse(party, sing_evt);
}

/* in Flurrie's slot he walks: the walking partner's field code, not her float */
static void hayzeeSwapped(int on) {
    if (on) {
        partyDataTbl[SLOT].init_func = kuribo_init;
        partyDataTbl[SLOT].move_func = partyMoveWalk;
        partyDataTbl[SLOT].bye_func = kuribo_bye;
        partyDataTbl[SLOT].exit_func = NULL;
    }
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static const char* const kText[] = {
    "sl_name_hayzee", "Hayzee",
    "sl_hz_soft", "Soft Shoe",
    "sl_hz_soft_h", "A little footwork: hop over and\ntap one foe with a heel.",
    "sl_hz_soft_ac", "Press <icon PAD_A 0.6 0 22 0> just as he lands!",
    "sl_hz_lullaby", "Lullaby",
    "sl_hz_lullaby_h", "Sings every foe a slow, soft\nsong. Sleepy ones nod off.",
    "sl_hz_lullaby_ac", "Press <icon PAD_A 0.6 0 22 0> on each beat,\nas the signals light up!",
    "sl_hz_encore", "Encore",
    "sl_hz_encore_h", "The spotlight swings back to\nLuigi: he gets another move.",
    "sl_hz_encore_ac", "Press <icon PAD_A 0.6 0 22 0> when the gauge\nswells into the band!",
    "sl_hz_encore_ok", "Encore! ENCORE! They want\nmore of you, baby!\n<k>",
    "sl_hz_encore_miss", "...Tough crowd. Next time,\nbaby.\n<k>",
    "sl_hz_show", "Showstopper",
    "sl_hz_show_h", "The crowd sings along, hitting\nevery foe. More crowd, more hit.",
    "sl_hz_show_ac", "Mash <icon PAD_A 0.6 0 22 0> to get the crowd\nsinging!",
    "sl_hz_lielow_n", "Lie Low",
    "sl_hz_lielow_h", "Lights off the Grass: the next\nhit on him misses. Once an act.",
    "sl_hz_lielow", "Lights! Off the Grass!\nNobody notices the grass,\nbaby.\n<k>",
    "sl_hz_which", "Which number, baby?\n<k>",
    "sl_hz_song_1", "Shake a Leaf",
    "sl_hz_song_2", "Rise and Shine",
    "sl_hz_song_3", "Echo",
    "sl_hz_song_no", "Not now",
    "sl_hz_nobody_1", "Tough crowd. And by crowd I\nmean nobody.\n<k>",
    "sl_hz_nobody_2", "Saving it for the matinee.\n<k>",
    "sl_hz_nobody_3", "No audience, baby. No show.\n<k>",
    "sl_hz_dance", "Look at 'em go! Can't stop,\nwon't stop. Walk right into one,\nbaby. They'll never know.\n<k>",
    "sl_hz_echo_prompt", "Sing it back:",
    NULL,
};

static const PortSlFriend kHayzee = {
    PORT_SL_FRIEND_HAYZEE,
    SLOT,
    "Hayzee",
    "c_lp_pansy",
    "Hayzee: a theatre producer.\nBelieves in you. Believes in\neveryone. Can't help it.",
    kPoses,
    hayzeeUse,
    NULL,
    "S_1",
    hayzeeUnit,
    sWaza,
    "art/icons/hayzee/hayzee_menu.png",
    "art/icons/hayzee/hayzee_hud.png",
    12,
    0,
    kText,
    hayzeeSwapped,
};

const PortSlFriend* portSlFriendHayzee(void) {
    return &kHayzee;
}

/* chapter 4's setup: the dancers' frame and their first strike */
void portSlHayzeeRegister(void) {
    portModHostAddHook(1, kSlot_fbatHitCheck, (void*)hitCheckPost);
    portModHostAddHook(1, kSlot_marioStMain, (void*)danceFrame);
}
