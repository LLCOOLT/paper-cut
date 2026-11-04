/* Blooey, Luigi's friend from chapter 1 (docs/SUPER_LUIGI.md, "Luigi's
 * friends"): a partner of Luigi's in Goombella's slot (the framework swaps
 * him in, port/include/port/superluigi.h's PortSlFriend): his field entry
 * (model, poses, ability), party-menu entry, battle unit and the texts that
 * name the slot; nothing on the disc or in a save changes.
 *
 * Field: Swim. Luigi rides Blooey across the lava or water a room marks
 * (evt_sl_swim_point: the framework's PORT_SL_ABILITY_SWIM points); anywhere
 * else Blooey says so.
 * Battle: his own move scripts on the game's action commands: Torpedo (the
 * charge gauge: hold left, let go on the star; he shoots head first and
 * rebounds), Ink Squirt (mash A; every grounded foe dizzy), Tentacle Hold
 * (a timed A; the foe can't move for a turn), and Blooey Toss in the
 * statue's fight (the Torpedo, high in an arc), with his A_1A-A_3C poses. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "battle/battle.h"
#include "battle/battle_party.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/party_data.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "party/party_kuribo.h"
#include "unit/party/unit_party_christine.h"
#include "unit/party/unit_party_clauda.h"
#include "win/win_main.h"
#include "driver/msgdrv.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <string.h>



BattleWeapon* BattleGetSelectWeapon(BattleWork* wp);
extern BattleUnitKindPart unitpartsdata_Party_Christine[];
#include "eff/n64/eff_opuku_jetw_n64.h"
#include "driver/effdrv.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_sub.h"
#include "battle/battle_camera.h"
#include "battle/battle_event_default.h"
#include "battle/battle_audience.h"
#include "evt/evt_audience.h"

#define SLOT 1 /* Goombella's: partyDataTbl, partyPoseTbl, pouch partyData */

/* Blooey's poses by the field pose index (party.c's partyPoseTbl columns),
 * the friends' animations (docs/LUIGI_ANIMS.md, "Luigi's friends") */
static char* const kPoses[15] = {"Z_1", "S_1", "W_1", "R_1", "J_1A", "J_1B", "N_1", "T_1",
                                 "D_1", "D_4", "S_1", "S_1", "D_2", "W_2", "R_2"};

/* the battle poses, as the partners' tables have them */
static PoseTableEntry kBattlePoses[] = {
    {9, "D_5"}, {2, "D_5"}, {3, "X_1"}, {4, "D_2"}, {5, "X_1"}, {1, "N_1"}, {27, "D_3"},
    {BTL_POSE_NORMAL, "S_1"}, {37, "G_2"}, {31, "G_1"}, {BTL_POSE_DAMAGE, "D_1"},
    {BTL_POSE_WALK, "W_1"}, {BTL_POSE_RUN, "R_1"}, {BTL_POSE_PERIL, "D_2"}, {BTL_POSE_DANGER, "D_2"},
    {BTL_POSE_ESCAPE_FAIL, "D_4"}, {BTL_POSE_ESCAPE_FAIL_END, "D_2"}, {BTL_POSE_TALK, "T_1"},
    {BTL_POSE_ITEM_USE, "I_1"}, {BTL_POSE_COMMAND_WAIT, "C_1"}, {BTL_POSE_APPEAL, "O_1"},
    {BTL_POSE_ZERO_GRAVITY, "J_1B"}, {49, "J_1A"}, {BTL_POSE_DEFEND, "G_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};

/* his moves: the weapons (made from Goombella's Headbonk and Flurrie's Gale
 * Force for their targeting and stats) and the move scripts below */
static BattleWeapon sTorpedo, sInk, sInkWeak, sHold, sHoldWeak, sToss;
int portSlBossBattle(void); /* superluigi_statue_aurora.c: the statue's fight */
static BattleWeapon* sWaza[4] = {&sTorpedo, &sInk, &sHold, NULL};
static BattleUnitKind sKind;
static BattleUnitKindPart sParts[1];
static BattleDataEntry sData[16];

/* Blooey Toss: the statue's gem's move (superluigi_statue_aurora.c's gate) */
const BattleWeapon* portSlBlooeyToss(void) {
    return &sToss;
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

/* Blooey's battle menu (unit data tag 1): Torpedo and Ink Squirt, Tentacle
 * Hold from Super rank, Blooey Toss in the statue's fight */
static void makeMenu(BattleCommand* command, s32* count) {
    addMove(command, count, &sTorpedo);
    addMove(command, count, &sInk);
    if (pouchGetPtr()->partyData[SLOT].attackLevel >= 1) {
        addMove(command, count, &sHold);
    }
    if (portSlBossBattle()) {
        addMove(command, count, &sToss);
    }
}

/* in: the unit (Blooey) and how long (frames): the ink he squirts ahead of him,
 * black, as the Blooper's own (unit_boss_gesso.c's _ikasumi_effect, which faces left at 135: the
 * jet runs along its local up turned by rotZ). Blooey faces right, at the foes: -100, a little down;
 * 60 sent it up and back, off the stage's left edge behind him */
static USER_FUNC(bl_ink_effect) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit = BattleGetUnitPtr(wp, BattleTransID(event, evtGetValue(event, event->args[0])));
    EffectEntry* eff;
    EffOpukuJetwN64Work* w;
    Vec pos;
    (void)isFirstCall;
    BtlUnit_GetPos(unit, &pos.x, &pos.y, &pos.z);
    pos.x += 22.0f;
    pos.y += 18.0f;
    eff = effOpukuJetwN64Entry(0, evtGetValue(event, event->args[1]), pos.x, pos.y, pos.z, 1.8f);
    w = (EffOpukuJetwN64Work*)eff->userdata;
    w->rotZ = -100.0f;
    w->colorR = w->colorG = w->colorB = 0;
    w->color2R = w->color2G = w->color2B = 0;
    return EVT_RETURN_DONE;
}

/* in: a weapon address, the action command's result: out: the weapon to deal (the strong one on a success) */
static USER_FUNC(bl_pick_weapon) {
    EvtWord* a = event->args;
    BattleWeapon* w = (BattleWeapon*)evtGetValue(event, a[0]);
    const s32 ok = (evtGetValue(event, a[1]) & 2) != 0;
    (void)isFirstCall;
    if (w == &sInk || w == &sInkWeak) {
        w = ok ? &sInk : &sInkWeak;
    } else if (w == &sHold || w == &sHoldWeak) {
        w = ok ? &sHold : &sHoldWeak;
    }
    evtSetValue(event, a[2], (EvtWord)(AddrWord)w);
    return EVT_RETURN_DONE;
}

/* out: 1 if the move chosen is Blooey Toss */
static USER_FUNC(bl_is_toss) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], BattleGetSelectWeapon(_battleWorkPointer) == &sToss);
    return EVT_RETURN_DONE;
}

/* Torpedo, and Blooey Toss: he winds up while the stick is held left (the
 * game's charge gauge, as Koops's Shell Toss), and on the release shoots head
 * first at the foe, low along the stage (Torpedo) or high in an arc (the Toss,
 * reaching what no jump can); he bounces off and rights himself, with a
 * stylish window on the rebound. */
static EVT_BEGIN(bl_torpedo)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(bl_is_toss, LW(9))
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
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
    /* the wind-up */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK1"), LW(13))
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
        CASE_EQUAL(2)
        SET_VALUE(LW(0), 200)
        SET_VALUE(LW(1), 600)
        CASE_DEFAULT
        SET_VALUE(LW(0), 200)
        SET_VALUE(LW(1), 960)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, 2, LW(0), 0, 1, LW(1), EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 3)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 19, 1, 60)
    WAIT_FRAMES(22)
    /* held: the whirr rises as he winds tighter */
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        LOOP(10000)
            CALL(btlevtcmd_AcGetOutputParam, 0, LW(0))
            IF_INT_NOT_EQUAL(LW(0), 0)
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
        SET_VALUE(LW(6), 0)
        LOOP(10000)
            ADD(LW(6), 2)
            IF_INT_GREATER(LW(6), 100)
                SET_VALUE(LW(6), 100)
            END_IF
            SET_VALUE(LW(0), 1000)
            ADD(LW(0), LW(6))
            ADD(LW(0), LW(6))
            CALL(evt_snd_sfx_pit, LW(13), LW(0))
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(6))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 4)
    CALL(evt_btl_camera_set_zoom, 0, 400)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    DELETE_EVENT(LW(15))
    CALL(evt_snd_sfxoff, LW(13))
    /* released: the White Torpedo */
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_NOKO_ATTACK2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_posoffset, 0, 0, 0, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    ADD(LW(2), 5)
    IF_INT_EQUAL(LW(9), 1)
        /* the Toss: high, in an arc, onto the part itself */
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.5f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 30, -1)
    ELSE
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(10.0f))
        CALL(btlevtcmd_GetMoveFrame, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, LW(13))
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(13), 0, 0)
    END_IF
    /* a miss: past it, and home */
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
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
        CALL(btlevtcmd_TransStageFloorPosition, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.69921875f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 16, -1)
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
        WAIT_FRAMES(30)
        GOTO(95)
    END_IF
    CALL(btlevtcmd_GetResultAC, LW(0))
    IF_BITS_SET(LW(0), 2)
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(0), LW(6))
    ELSE
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(0), -1)
    END_IF
    /* the rebound: he bounces off and rights himself, a stylish window on the way */
    CALL(btlevtcmd_StopAC)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1C"))
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.69921875f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_TransStageFloorPosition, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 40)
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_ACRStart, BTL_UNIT_SELF, 0, 17, 17, 0)
    END_SIBLING_EVENT
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 20, -1)
    CALL(btlevtcmd_ACRGetResult, LW(6), LW(7))
    IF_INT_EQUAL(LW(6), 2)
        /* stylish: a full spin in the air and a pose */
        CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_AudienceDeclareAcrobatResult, LW(0), 1, 0, 0, 0)
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 15, 0)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        BEGIN_SIBLING_EVENT
            LOOP(12)
                CALL(btlevtcmd_AddRotate, BTL_UNIT_SELF, 0, 0, 30)
                WAIT_FRAMES(1)
            END_LOOP
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
        END_SIBLING_EVENT
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 14, -1)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_1"))
        WAIT_FRAMES(40)
    ELSE
        CALL(evt_audience_acrobat_notry)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 15)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 10, -1)
        WAIT_FRAMES(20)
    END_IF
    LABEL(95)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
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

/* Ink Squirt: he draws a breath, his head swelling, while A is pressed again
 * and again (the game's mash gauge, as Goombella's Rally Wink); then he
 * squirts a cloud of ink over every foe on the ground. A full gauge inks them
 * dizzy for two turns; less, a chance of one. */
static EVT_BEGIN(bl_ink)
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
    /* up to a jet's length short of the first foe: from his place behind Luigi the jet ran through
     * Luigi and stopped halfway to the foes */
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 130)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    IF_INT_GREATER(LW(0), LW(5))
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(6), LW(7), 0, -1, 0)
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    END_IF
    WAIT_FRAMES(20)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 280)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-2)
        SET_VALUE(LW(0), 27)
        SET_VALUE(LW(1), 27)
        CASE_EQUAL(-1)
        SET_VALUE(LW(0), 25)
        SET_VALUE(LW(1), 30)
        CASE_EQUAL(0)
        SET_VALUE(LW(0), 23)
        SET_VALUE(LW(1), 30)
        CASE_EQUAL(1)
        SET_VALUE(LW(0), 20)
        SET_VALUE(LW(1), 30)
        CASE_DEFAULT
        SET_VALUE(LW(0), 20)
        SET_VALUE(LW(1), 35)
    END_SWITCH
    /* mode 1: A alone, as the help says (16, Rally Wink's, switches between A and B) */
    CALL(btlevtcmd_AcSetParamAll, 1, 220, 70, 0, 0, LW(0), LW(1), 0)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 5, 1, 60)
    /* the breath, the head swelling */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_CLAUD_BREATH1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(22)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(7))
    CALL(btlevtcmd_StopAC)
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_GetHeight, BTL_UNIT_SELF, LW(6))
        ADD(LW(1), LW(6))
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_SYSTEM, 0, LW(6))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
    END_IF
    /* the squirt */
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_zoom, 0, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    CALL(bl_ink_effect, BTL_UNIT_SELF, 60)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GESSO_SUMI1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(20)
    CALL(bl_pick_weapon, LW(12), LW(7), LW(12))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    LABEL(10)
    IF_INT_EQUAL(LW(3), -1)
        GOTO(80)
    END_IF
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(6))
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
        GOTO(50)
    END_IF
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), 0)
    ELSE
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    LABEL(50)
    WAIT_FRAMES(10)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    GOTO(10)
    LABEL(80)
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    CALL(btlevtcmd_ACRStart, BTL_UNIT_SELF, 0, 25, 25, 20)
    CALL(btlevtcmd_ACRGetResult, LW(6), LW(7))
    IF_INT_EQUAL(LW(6), 2)
        CALL(btlevtcmd_AudienceDeclareAcrobatResult, LW(12), 1, 0, 0, 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_1"))
        WAIT_FRAMES(50)
    ELSE
        CALL(evt_audience_acrobat_notry)
        WAIT_FRAMES(20)
    END_IF
    /* home again */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    LABEL(99)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Tentacle Hold: he lunges at the foe, tentacles flung wide, and A as they
 * close (the game's timed press, as Goombella's Headbonk) wraps it up tight:
 * it can't move for a turn. Late, he only squeezes it. */
static EVT_BEGIN(bl_hold)
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
    /* the lunge: the command's window is as the tentacles close */
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3A"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 12)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.5f))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 20, -1)
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
        GOTO(95)
    END_IF
    /* the squeeze */
    CALL(bl_pick_weapon, LW(12), LW(7), LW(12))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GESSO_ARM_APPEAR1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(20)
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    WAIT_FRAMES(30)
    /* letting go */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3C"))
    WAIT_FRAMES(16)
    LABEL(95)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.69921875f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("J_1A"))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 24, -1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Victory (data tag 64): his cheer */
static EVT_BEGIN(bl_win)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
    WAIT_FRAMES(56)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    RETURN
EVT_END()

static void setDamage(BattleWeapon* w, s32 a, s32 b, s32 c, s32 d, s32 e, s32 f) {
    memset(w->hpDamageWork, 0, sizeof(w->hpDamageWork));
    w->hpDamageWork[0] = a, w->hpDamageWork[1] = b, w->hpDamageWork[2] = c;
    w->hpDamageWork[3] = d, w->hpDamageWork[4] = e, w->hpDamageWork[5] = f;
}

static BattleUnitKind* blooeyUnit(void) {
    const BattleDataEntry* d;
    int n = 0;
    /* Torpedo, 0 FP: one foe on the ground or flying low (a fail, a success, by rank: 1/2, 2/3, 3/4) */
    sTorpedo = partyWeapon_ChristineNormalAttack;
    sTorpedo.name = "sl_bl_torpedo";
    sTorpedo.description = "sl_bl_torpedo_h";
    sTorpedo.acHelpMsg = "sl_bl_torpedo_ac";
    sTorpedo.targetPropertyFlags = TARGET_PROP_NO_CEILING | TARGET_PROP_START_OPPOSING_SIDE;
    setDamage(&sTorpedo, 1, 2, 2, 3, 3, 4);
    sTorpedo.attackEvtScript = bl_torpedo;
    /* Ink Squirt, 3 FP: every foe on the ground, no damage; dizzy (2 turns) on a full gauge, else a chance of 1 */
    sInk = partyWeapon_ClaudaBreathAttack;
    sInk.name = "sl_bl_ink";
    sInk.description = "sl_bl_ink_h";
    sInk.acHelpMsg = "sl_bl_ink_ac";
    sInk.baseFpCost = 3;
    sInk.hpDamageFn = NULL;
    setDamage(&sInk, 0, 0, 0, 0, 0, 0);
    sInk.galeForceChance = 0;
    sInk.dizzyChance = 100;
    sInk.dizzyTime = 2;
    sInk.attackEvtScript = bl_ink;
    sInkWeak = sInk;
    sInkWeak.dizzyChance = 50;
    sInkWeak.dizzyTime = 1;
    /* Tentacle Hold, 2 FP (Super rank): one foe; on time it can't move for a turn */
    sHold = partyWeapon_ChristineNormalAttack;
    sHold.name = "sl_bl_hold";
    sHold.description = "sl_bl_hold_h";
    sHold.acHelpMsg = "sl_bl_hold_ac";
    sHold.baseFpCost = 2;
    sHold.targetPropertyFlags = TARGET_PROP_NO_CEILING | TARGET_PROP_START_OPPOSING_SIDE;
    setDamage(&sHold, 1, 1, 1, 1, 2, 2);
    sHold.stopChance = 100;
    sHold.stopTime = 1;
    sHold.attackEvtScript = bl_hold;
    sHoldWeak = sHold;
    sHoldWeak.stopChance = 0;
    sHoldWeak.stopTime = 0;
    /* Blooey Toss (the statue's fight only): the Torpedo's command, high in an arc */
    sToss = sTorpedo;
    sToss.name = "sl_bl_toss";
    sToss.description = "sl_bl_toss_h";
    sToss.targetClassFlags &= ~TARGET_CLASS_MAIN_PARTS;
    sToss.targetPropertyFlags = TARGET_PROP_START_OPPOSING_SIDE;
    setDamage(&sToss, 2, 3, 3, 4, 4, 5);

    sParts[0] = unitpartsdata_Party_Christine[0];
    sParts[0].partName = "sl_name_blooey";
    sParts[0].modelName = "c_lp_gesso";
    sParts[0].poseTable = kBattlePoses;
    for (d = unitdata_Party_Christine.dataTable; d != NULL && d->tag != 0 && n < 15; d++) {
        sData[n] = *d;
        if (d->tag == 1) {
            sData[n].data = (void*)makeMenu;
        } else if (d->tag == 64) {
            sData[n].data = (void*)bl_win; /* Goombella's played her PKR_Y_1, which he hasn't got */
        }
        n++;
    }
    sData[n].tag = 0;
    sData[n].data = NULL;
    sKind = unitdata_Party_Christine;
    sKind.name = "sl_name_blooey";
    sKind.parts = sParts;
    sKind.dataTable = sData;
    return &sKind;
}

/* --- Swim --- */

static int sRiding;

/* in: a swim point (0-7) and its two shores (x, y, z each); map scripts set them in their init */
USER_FUNC(evt_sl_swim_point) {
    EvtWord* a = event->args;
    PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_SWIM, (int)evtGetValue(event, a[0]));
    (void)isFirstCall;
    if (p != NULL) {
        p->on = 1;
        p->x = (f32)evtGetValue(event, a[1]);
        p->y = (f32)evtGetValue(event, a[2]);
        p->z = (f32)evtGetValue(event, a[3]);
        p->x2 = (f32)evtGetValue(event, a[4]);
        p->y2 = (f32)evtGetValue(event, a[5]);
        p->z2 = (f32)evtGetValue(event, a[6]);
        p->radius = 70.0f;
        p->script = NULL;
    }
    return EVT_RETURN_DONE;
}

/* kept for the rooms that call it: the points are cleared with every map change */
USER_FUNC(evt_sl_swim_clear) {
    (void)event;
    (void)isFirstCall;
    sRiding = 0;
    return EVT_RETURN_DONE;
}

/* out: whether Luigi rides Blooey now (the lava doesn't burn him) */
int portSlRiding(void) {
    return sRiding;
}

/* out: the shore to swim to from Luigi's place (x, y, z), and 1, or 0 when
 * no shore of a swim point is within reach */
static USER_FUNC(swim_find) {
    EvtWord* a = event->args;
    MarioWork* m = marioGetPtr();
    int end = 0;
    PortSlPoint* p = portSlPointNear(PORT_SL_ABILITY_SWIM, m->position.x, m->position.z, &end);
    (void)isFirstCall;
    evtSetValue(event, a[0], p == NULL ? 0 : (s32)(end ? p->x : p->x2));
    evtSetValue(event, a[1], p == NULL ? 0 : (s32)(end ? p->y : p->y2));
    evtSetValue(event, a[2], p == NULL ? 0 : (s32)(end ? p->z : p->z2));
    evtSetValue(event, a[3], p != NULL);
    if (p != NULL) {
        /* a long crossing takes its time: 3 ms a unit, 1.8 s at the least */
        const float tx = end ? p->x : p->x2, tz = end ? p->z : p->z2;
        const int ms = (int)(hypotf(tx - m->position.x, tz - m->position.z) * 3.0f);
        evtSetValue(event, a[4], ms < 1800 ? 1800 : ms);
    }
    sRiding = p != NULL;
    return EVT_RETURN_DONE;
}

static USER_FUNC(swim_done) {
    (void)event;
    (void)isFirstCall;
    sRiding = 0;
    return EVT_RETURN_DONE;
}

/* Swim's ride: Blooey under Luigi, his back at Luigi's feet, never below the shore he set off from */
#define SWIM_RIDE_H 24
static int sFollow;
static f32 sFollowFloor;

static USER_FUNC(swim_follow_on) {
    PartyEntry* p = partyGetPtr(marioGetPartyId());
    (void)event;
    (void)isFirstCall;
    sFollow = 1;
    sFollowFloor = p != NULL ? p->position.y : marioGetPtr()->position.y;
    return EVT_RETURN_DONE;
}

static USER_FUNC(swim_follow_off) {
    (void)event;
    (void)isFirstCall;
    sFollow = 0;
    return EVT_RETURN_DONE;
}

/* each frame until swim_follow_off */
static USER_FUNC(swim_follow) {
    MarioWork* m = marioGetPtr();
    PartyEntry* p = partyGetPtr(marioGetPartyId());
    (void)event;
    (void)isFirstCall;
    if (!sFollow) {
        return EVT_RETURN_DONE;
    }
    if (p != NULL) {
        const f32 y = m->position.y - SWIM_RIDE_H;
        p->position.x = m->position.x;
        p->position.y = y > sFollowFloor ? y : sFollowFloor;
        p->position.z = m->position.z;
    }
    return EVT_RETURN_BLOCK;
}

/* out: 1 if the swim point here has a script of its own (a dive: Blooey goes
 * alone, chapter 2's well), which then runs once the ability's handed back */
static USER_FUNC(swim_dive) {
    MarioWork* m = marioGetPtr();
    PortSlPoint* p = portSlPointNear(PORT_SL_ABILITY_SWIM, m->position.x, m->position.z, NULL);
    EventEntry* e = NULL;
    (void)isFirstCall;
    if (p != NULL && p->script != NULL) {
        /* the dive runs inside the ability (type 26, as the ability's own script): a plain
         * evtEntry would wait for the field's system level 1 to end, which only the dive ends */
        p->used = 1;
        e = evtEntryType((EvtWord*)p->script, 0, 0, 26);
        if (e != NULL) {
            e->lwData[0] = (EvtWord)(p - portSlPointGet(PORT_SL_ABILITY_SWIM, 0));
        }
    }
    evtSetValue(event, event->args[0], e != NULL);
    return EVT_RETURN_DONE;
}

/* Blooey's ability: Luigi climbs on and they cross to the other shore */
static EVT_BEGIN(swim_evt)
    CALL(swim_dive, LW(3))
    IF_INT_EQUAL(LW(3), 1)
        /* the dive's script was entered at system level 1, so it waits stopped until the
         * field is back at 0: hand the friend back here, and it runs */
        CALL(evt_sl_ability_done)
        RETURN
    END_IF
    CALL(swim_find, LW(0), LW(1), LW(2), LW(3), LW(7))
    IF_INT_EQUAL(LW(3), 0)
        CALL(evt_msg_print, 0, STRING("sl_bl_noswim"), 0, STRING("party"))
        CALL(swim_done)
        CALL(evt_sl_ability_done)
        RETURN
    END_IF
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_get_pos, 0, LW(4), LW(5), LW(6))
    CALL(evt_party_jump_pos, 0, LW(4), LW(5), LW(6), 250, 0, -1000)
    CALL(evt_msg_print, 0, STRING("sl_bl_swim"), 0, STRING("party"))
    CALL(evt_party_set_pose, 0, STRING("F_1"))
    /* Blooey kept under Luigi's feet for the whole ride: he moved on his own (1.6 s against Luigi's
     * 1.8 s or more) and Luigi's arc went a body's height over him, so the two crossed apart */
    CALL(swim_follow_on)
    BEGIN_INLINE_EVENT
        CALL(swim_follow)
    END_INLINE_EVENT
    /* up onto his back, then the ride: a low glide to the far shore (a walk would sink him to the
     * pool's floor) */
    CALL(evt_snd_sfxon, STRING("SFX_PARTY_BATTLE_JUMP1"), 0)
    ADD(LW(5), SWIM_RIDE_H)
    CALL(evt_mario_jump_pos, LW(4), LW(5), LW(6), 300, 1, 25)
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), LW(7), 0, 8)
    CALL(swim_follow_off)
    /* and up onto the shore beside him */
    CALL(evt_party_jump_pos, 0, LW(0), LW(1), LW(2), 300, 0, -1000)
    CALL(evt_party_set_pose, 0, STRING("S_1"))
    CALL(evt_mario_key_onoff, 1)
    CALL(swim_done)
    CALL(evt_sl_ability_done)
    RETURN
EVT_END()

/* the party's use function in Goombella's slot while Blooey is in it */
static void blooeyUse(PartyEntry* party) {
    portSlFieldUse(party, swim_evt);
}

static const char* const kText[] = {
    "sl_name_blooey", "Blooey",
    "sl_bl_torpedo", "Torpedo",
    "sl_bl_torpedo_h", "The White Torpedo winds up and\nshoots head first at one foe,\nthen bounces off.",
    "sl_bl_torpedo_ac", "Hold the Control Stick left to\nwind him up. Let go when the\nstar lights up!",
    "sl_bl_ink", "Ink Squirt",
    "sl_bl_ink_h", "Squirts ink over every foe on\nthe ground. Inked foes get dizzy\nand can miss.",
    "sl_bl_ink_ac", "Press <icon PAD_A 0.6 0 22 0> over and over to\nfill the gauge before time\nruns out!",
    "sl_bl_hold", "Tentacle Hold",
    "sl_bl_hold_h", "Wraps one foe up tight.\nIt can't move for a turn.",
    "sl_bl_hold_ac", "Press <icon PAD_A 0.6 0 22 0> just as his\ntentacles close around\nthe foe!",
    "sl_bl_noswim", "Swim where, Luigi? There's\nno water here. Or lava. I can do\nlava, if you don't look too close.",
    "sl_bl_swim", "Hop on, man! Keep your feet\nup, and whatever you do,\ndon't look down!",
    NULL,
};

static const PortSlFriend kBlooey = {
    PORT_SL_FRIEND_BLOOEY,
    SLOT,
    "Blooey",
    "c_lp_gesso",
    "A Blooper with a big heart. His\nhometown calls him the White Torpedo.",
    kPoses,
    blooeyUse,
    NULL,
    "S_1",
    blooeyUnit,
    sWaza,
    "art/icons/blooey/blooey_menu.png",
    "art/icons/blooey/blooey_hud.png",
    10,
    1,
    kText,
    NULL,
};

const PortSlFriend* portSlFriendBlooey(void) {
    return &kBlooey;
}
