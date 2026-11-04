/* Jerry, Luigi's friend from chapter 2 (docs/super_luigi/ch2.md, "Jerry"):
 * Plumpbelly's champion, a Bob-omb warrior, in Bobbery's partner slot (3).
 * The framework swaps him in (port/include/port/superluigi.h's PortSlFriend);
 * nothing on the disc or in a save changes.
 *
 * Field: Blast. The ability lights his fuse; he steps out from behind Luigi
 * (toward the camera, where he's seen) and stands fizzing for 2.5 seconds
 * (the ability again sets him off at once), then goes off where he stands,
 * reaching from there and from where he was lit: the game's own Bobbery blast (effect, sound,
 * quake), the game's partner-bomb hit on field enemies (a first strike), and
 * every Blast point of the room within reach runs its script (a cracked wall,
 * packed ice, a fuse). Luigi too close is thrown back, unhurt.
 *
 * Battle: his own unit (on Bobbery's kind, his model and poses) and three
 * moves of his own, each its own script with the game's action commands:
 *   Lit Fuse (0 FP)   a bomb lit from his fuse, flung at one foe: hold A
 *                     and let go as the fuse meter lights (the power gauge);
 *   Big Bang (4 FP)   he swells and bursts over every foe: mash A to fill
 *                     the blast gauge before time runs out;
 *   Short Fuse (2 FP) he winds himself up and charges Luigi's next attack:
 *                     rotate the stick until the fuse catches.
 * His field first strike (the Blast catching a foe) is his own as well. */
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
#include "battle/battle_party.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/party_data.h"
#include "driver/animdrv.h"
#include "driver/dispdrv.h"
#include "driver/hitdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "eff/eff_sandars.h"
#include "evt/evt_audience.h"
#include "evt/evt_cmd.h"
#include "evt/evt_eff.h"
#include "evt/evt_sub.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "pmario_sound.h"
#include "system.h"
#include "unit/party/unit_party_sanders.h"
#include "win/win_main.h"

#include <dolphin/mtx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

BattleWeapon* BattleGetSelectWeapon(BattleWork* wp);

#define SLOT 3            /* Bobbery's: partyDataTbl, partyPoseTbl, pouch partyData */
#define FUSE_FRAMES 150   /* 2.5 s */
#define BLAST_REACH 60.0f /* the Blast points' reach around him */

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

static BattleWeapon sLitFuse, sBigBang, sShortFuse, sFirst;
static BattleWeapon* sWaza[4] = {&sLitFuse, &sBigBang, &sShortFuse, &sShortFuse};
static BattleUnitKind sKind;
static BattleUnitKindPart sParts[1];
static BattleDataEntry sData[16];

static int inSlot(void) {
    const PortSlFriend* f = portSlFriendInSlot(SLOT);
    return f != NULL && f->id == PORT_SL_FRIEND_JERRY;
}

/* superluigi_ch02_aurora.c: Lit Fuse on a lit Hissguard (the mid-boss's gimmick) */
__attribute__((weak)) int portSlC2LitGuard(BattleUnit* target) {
    (void)target;
    return 0;
}

/* ======================================================================
 * Battle
 * ====================================================================== */

static void addMove(BattleCommand* command, s32* count, BattleWeapon* weapon) {
    command->weapon[*count].index = -1;
    command->weapon[*count].itemId = 0;
    command->weapon[*count].weapon = weapon;
    command->weapon[*count].enabled = 0;
    command->weapon[*count].icon = weapon->icon;
    command->weapon[*count].name = msgSearch(weapon->name);
    (*count)++;
}

/* His battle menu (unit data tag 1): all three from the start */
static void makeMenu(BattleCommand* command, s32* count) {
    addMove(command, count, &sLitFuse);
    addMove(command, count, &sBigBang);
    addMove(command, count, &sShortFuse);
}

/* in: a unit: out: 1 if it is a lit Hissguard Lit Fuse sets off on the spot */
static USER_FUNC(jerry_lit_guard) {
    BattleUnit* u = BattleGetUnitPtr(_battleWorkPointer, evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    evtSetValue(event, event->args[1], u != NULL ? portSlC2LitGuard(u) : 0);
    return EVT_RETURN_DONE;
}

/* in: a move's shout (a tag), or 0 to start a battle afresh: out: 1 the
 * first time that move is used in this battle */
static USER_FUNC(jerry_shout_first) {
    static const char* sShouted[4];
    const char* tag = (const char*)evtGetValue(event, event->args[0]);
    int i;
    (void)isFirstCall;
    if (tag == NULL) {
        memset(sShouted, 0, sizeof(sShouted));
        return EVT_RETURN_DONE;
    }
    for (i = 0; i < 4 && sShouted[i] != NULL; i++) {
        if (strcmp(sShouted[i], tag) == 0) {
            evtSetValue(event, event->args[1], 0);
            return EVT_RETURN_DONE;
        }
    }
    if (i < 4) {
        sShouted[i] = tag;
    }
    evtSetValue(event, event->args[1], 1);
    return EVT_RETURN_DONE;
}

/* the line he shouts as a move starts (his own text, over the battle): the
 * first time in a battle only, as a box he must wait out each turn drags */
static EVT_BEGIN(jerry_shout)
    CALL(jerry_shout_first, LW(14), LW(13))
    IF_INT_EQUAL(LW(13), 1)
        CALL(evt_msg_print, 2, LW(14), 0, BTL_UNIT_SELF)
    END_IF
    RETURN
EVT_END()

/* Lit Fuse's thrown bomb: the game's Bomb Squad bomb (c_bakudan, its lit-fuse pose), drawn by itself
 * along the arc; no unit, so nothing on the stage can target it */
#define BOMB_FRAMES 18
#define BOMB_PEAK 40.0f
static struct {
    s32 pose; /* -1 when none */
    int t;
    Vec from, to, at;
} sBomb = {-1};

static void bombDisp(CameraId cam, void* param) {
    Mtx m, s;
    (void)cam;
    (void)param;
    if (sBomb.pose < 0) {
        return;
    }
    MTXTrans(m, sBomb.at.x, sBomb.at.y, sBomb.at.z);
    MTXScale(s, 1.6f, 1.6f, 1.6f);
    MTXConcat(m, s, m);
    animPoseDrawMtx(sBomb.pose, m, 1, 0.0f, 2.0f);
    animPoseDrawMtx(sBomb.pose, m, 2, 0.0f, 2.0f);
    animPoseDrawMtx(sBomb.pose, m, 3, 0.0f, 2.0f);
}

static void bombDrop(void) {
    if (sBomb.pose >= 0) {
        animPoseRelease(sBomb.pose);
        sBomb.pose = -1;
    }
}

/* in: from x, y, z, to x, y, z: the bomb flies from his hand to the foe over BOMB_FRAMES, then is gone */
static USER_FUNC(jerry_bomb_fly) {
    float k;
    if (isFirstCall) {
        bombDrop();
        sBomb.from = (Vec){(f32)evtGetValue(event, event->args[0]), (f32)evtGetValue(event, event->args[1]),
                           (f32)evtGetValue(event, event->args[2])};
        sBomb.to = (Vec){(f32)evtGetValue(event, event->args[3]), (f32)evtGetValue(event, event->args[4]),
                         (f32)evtGetValue(event, event->args[5])};
        sBomb.t = 0;
        sBomb.pose = animPoseEntry("c_bakudan", 1);
        if (sBomb.pose >= 0) {
            animPoseSetAnim(sBomb.pose, "B_2", 1);
        }
    }
    if (sBomb.pose < 0 || sBomb.t >= BOMB_FRAMES) {
        bombDrop();
        return EVT_RETURN_DONE;
    }
    sBomb.t++;
    k = (float)sBomb.t / BOMB_FRAMES;
    sBomb.at.x = sBomb.from.x + (sBomb.to.x - sBomb.from.x) * k;
    sBomb.at.y = sBomb.from.y + (sBomb.to.y - sBomb.from.y) * k + 4.0f * BOMB_PEAK * k * (1.0f - k);
    sBomb.at.z = sBomb.from.z + (sBomb.to.z - sBomb.from.z) * k;
    animPoseMain(sBomb.pose);
    dispEntry(CAMERA_3D, dispCalcZ(&sBomb.at), 1, bombDisp, NULL);
    return EVT_RETURN_BLOCK;
}

/* After a blast: thrown off the top of the screen, he falls back to his
 * place; A as he lands for a Stylish bounce (as Bobbery's) */
static EVT_BEGIN(jerry_blown_home)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("J_1A"))
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(30)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("J_1B"))
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    END_SIBLING_EVENT
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.4f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 90, -1)
    END_SIBLING_EVENT
    WAIT_FRAMES(60)
    DELETE_EVENT(LW(15))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_GetStageSize, EVT_NULLPTR, LW(8), EVT_NULLPTR)
    ADD(LW(6), LW(8))
    DIVIDE(LW(6), 2)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.25f))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 30, -1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, FLOAT(0.7f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 16, -1)
    END_SIBLING_EVENT
    CALL(btlevtcmd_ACRStart, BTL_UNIT_SELF, 0, 15, 15, 15)
    CALL(btlevtcmd_ACRGetResult, LW(6), LW(7))
    CALL(btlevtcmd_WaitEventEnd, LW(15))
    IF_INT_GREATER_OR_EQUAL(LW(6), 2)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
        CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_AudienceDeclareAcrobatResult, LW(0), 1, 0, 0, 0)
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.3f), FLOAT(0.7f), FLOAT(1.0f))
        WAIT_FRAMES(6)
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(0.8f), FLOAT(1.25f), FLOAT(1.0f))
        WAIT_FRAMES(6)
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
        WAIT_FRAMES(36)
    ELSE
        CALL(evt_audience_acrobat_notry)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("D_4"))
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_THUNDERS_DOWN1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        WAIT_FRAMES(40)
    END_IF
    RETURN
EVT_END()

/* Lit Fuse: he rears back with the fuse fizzing while the meter fills (hold
 * A, let go as it lights), then hops forward and flings the bomb at one foe,
 * on the ground or in the air. Explosive: a spiky foe is hit safely. */
static EVT_BEGIN(jerry_lit_fuse)
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
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    SET_VALUE(LW(14), PTR("sl_je_bt_lit"))
    CALL_CHILD(jerry_shout)
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    /* a few steps up to throwing range, out past Luigi and a little toward the camera (at 30 he stood
     * inside Luigi's drawing: his wind-up and throw went unseen) */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 80)
    ADD(LW(2), 10)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    /* the fuse meter: hold A and let go as it lights */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-2)
        SET_VALUE(LW(1), 50)
        CASE_EQUAL(-1)
        SET_VALUE(LW(1), 32)
        CASE_EQUAL(0)
        SET_VALUE(LW(1), 26)
        CASE_EQUAL(1)
        SET_VALUE(LW(1), 20)
        CASE_DEFAULT
        SET_VALUE(LW(1), 12)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, 2, 100, LW(1), 0, 100, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 15)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 15, 1, 0)
    /* he rears back as the meter comes up (after the wait he stood idle through it) */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    WAIT_FRAMES(22)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK2"), LW(13))
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        LOOP(0)
            CALL(btlevtcmd_AcGetOutputParam, 0, LW(1))
            IF_INT_NOT_EQUAL(LW(1), 0)
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_WaitEventEnd, LW(15))
    CALL(btlevtcmd_ResultAC)
    CALL(evt_snd_sfxoff, LW(13))
    /* the throw: a hop forward, the bomb away at 4 */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    WAIT_FRAMES(4)
    CALL(evt_snd_sfxon, STRING("SFX_PARTY_BATTLE_JUMP1"), 0)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 20)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(0.6f), 0, 0, 0, 0, 0, 0, 0)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(5), LW(6), LW(7))
    /* the bomb's flight: the bomb itself along its arc, sparks trailing it */
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        CALL(jerry_bomb_fly, LW(0), LW(1), LW(2), LW(5), LW(6), LW(7))
    END_SIBLING_EVENT
    SET_VALUE(LW(8), 0)
    LOOP(6)
        ADD(LW(8), 1)
        SET_VALUE(LW(9), LW(5))
        SUBTRACT(LW(9), LW(0))
        MULTIPLY(LW(9), LW(8))
        DIVIDE(LW(9), 6)
        ADD(LW(9), LW(0))
        SET_VALUE(LW(10), LW(6))
        SUBTRACT(LW(10), LW(1))
        MULTIPLY(LW(10), LW(8))
        DIVIDE(LW(10), 6)
        ADD(LW(10), LW(1))
        SET_VALUE(LW(11), 6)
        SUBTRACT(LW(11), LW(8))
        MULTIPLY(LW(11), LW(8))
        MULTIPLY(LW(11), 4) /* near the bomb's own arc (BOMB_PEAK 40) */
        ADD(LW(10), LW(11))
        SET_VALUE(LW(14), LW(7))
        SUBTRACT(LW(14), LW(2))
        MULTIPLY(LW(14), LW(8))
        DIVIDE(LW(14), 6)
        ADD(LW(14), LW(2))
        CALL(evt_eff, 0, STRING("stardust"), 0, LW(9), LW(10), LW(14), 0, 0, 0, 0, 0, 0, 0, 0)
        WAIT_FRAMES(3)
    END_LOOP
    CALL(btlevtcmd_WaitEventEnd, LW(15))
    /* it goes off on the foe */
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK3"), 0)
    CALL(evt_eff64, STRING(""), STRING("bomb_n64"), 0, LW(5), LW(6), LW(7), 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_btl_camera_shake_h, 0, 4, 0, 10, 13)
    CALL(btlevtcmd_StageDispellFog)
    CALL(jerry_lit_guard, LW(3), LW(9))
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
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
        GOTO(90)
    END_IF
    CALL(btlevtcmd_GetResultAC, LW(6))
    IF_BITS_SET(LW(6), 2)
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    LABEL(90)
    CALL(btlevtcmd_StopAC)
    WAIT_FRAMES(20)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_WALK)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Big Bang: he swells, shaking, while the gauge is mashed full, and bursts
 * where he stands: every foe is caught in it. */
static EVT_BEGIN(jerry_big_bang)
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
    SET_VALUE(LW(14), PTR("sl_je_bt_bang"))
    CALL_CHILD(jerry_shout)
    /* to the middle of the stage, in front of the foes */
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    SET_VALUE(LW(0), 0)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    /* mash A to fill the blast gauge */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-2)
        SET_VALUE(LW(0), 300)
        SET_VALUE(LW(1), 20)
        SET_VALUE(LW(2), 20)
        CASE_EQUAL(-1)
        SET_VALUE(LW(0), 255)
        SET_VALUE(LW(1), 17)
        SET_VALUE(LW(2), 23)
        CASE_EQUAL(0)
        SET_VALUE(LW(0), 225)
        SET_VALUE(LW(1), 16)
        SET_VALUE(LW(2), 24)
        CASE_DEFAULT
        SET_VALUE(LW(0), 180)
        SET_VALUE(LW(1), 14)
        SET_VALUE(LW(2), 28)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, 1, LW(0), 178, LW(1), LW(2), 15, 46, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetGaugeParam, 45, 75, 100, 100)
    CALL(btlevtcmd_AcSetFlag, 64)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)
    WAIT_FRAMES(22)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK2"), LW(13))
    /* he swells with the gauge */
    BEGIN_SIBLING_EVENT_GET_ID(LW(15))
        LOOP(0)
            CALL(btlevtcmd_AcGetOutputParam, 1, LW(0))
            ADD(LW(0), 100)
            MULTIPLY_FLOAT(LW(0), FLOAT(0.009765625f))
            CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, LW(0), LW(0), FLOAT(1.0f))
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    DELETE_EVENT(LW(15))
    CALL(evt_snd_sfxoff, LW(13))
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    /* the burst */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    WAIT_FRAMES(4)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK4"), 0)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetResultAC, LW(6))
    IF_BITS_SET(LW(6), 2)
        CALL(evt_eff, STRING(""), STRING("sandars"), 3, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    ELSE
        CALL(evt_eff, STRING(""), STRING("sandars"), 4, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    END_IF
    CALL(btlevtcmd_StageDispellFog)
    CALL(evt_btl_camera_shake_h, 0, 10, 0, 20, 13)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    /* every foe in turn */
    LABEL(10)
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
        GOTO(50)
    END_IF
    CALL(btlevtcmd_GetResultAC, LW(0))
    IF_BITS_SET(LW(0), 2)
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    LABEL(50)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(10)
    END_IF
    CALL(btlevtcmd_StopAC)
    CALL_CHILD(jerry_blown_home)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Short Fuse: hunkered down, he winds himself up with his key (rotate the
 * stick until the fuse catches), springs up charged and passes it to Luigi:
 * his next attack does more (a charge) */
static EVT_BEGIN(jerry_short_fuse)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    SET_VALUE(LW(14), PTR("sl_je_bt_short"))
    CALL_CHILD(jerry_shout)
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 300)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3A"))
    /* rotate the stick: the game's stick-rotate command */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-1)
        SET_VALUE(LW(1), 3)
        CASE_EQUAL(0)
        SET_VALUE(LW(1), 4)
        CASE_DEFAULT
        SET_VALUE(LW(1), 5)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, 8, 150, 0, LW(1), 3, 5, 120, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 9, 1, 0)
    WAIT_FRAMES(22)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK2"), LW(13))
    CALL(btlevtcmd_StartAC, 1)
    /* each turn of the key, a spark off his fuse: the command shows no count of its own */
    BEGIN_SIBLING_EVENT
        SET_VALUE(LW(8), 1)
        LOOP(0)
            CALL(btlevtcmd_GetResultAC, LW(6))
            IF_BITS_CLEAR(LW(6), 1)
                BREAK_LOOP
            END_IF
            CALL(btlevtcmd_AcGetOutputParam, 0, LW(7))
            IF_INT_GREATER(LW(7), LW(8))
                SET_VALUE(LW(8), LW(7))
                CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
                ADD(LW(1), 34)
                CALL(evt_eff, 0, STRING("small_star"), 3, LW(0), LW(1), LW(2), 0, -1, 0, 3, 0, 0, 0, 0)
                CALL(evt_snd_sfxon, STRING("SFX_ENM_BOMB_SPARK1"), 0)
            END_IF
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_ResultAC)
    CALL(evt_snd_sfxoff, LW(13))
    CALL(btlevtcmd_StopAC)
    /* he springs up, charged */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3B"))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK3"), 0)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 20)
    CALL(evt_eff, 0, STRING("sandars"), 5, LW(0), LW(1), LW(2), 60, 0, 0, 0, 0, 0, 0, 0)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_GetResultAC, LW(6))
    IF_BITS_SET(LW(6), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), PTR(&sShortFuse), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    WAIT_FRAMES(30)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* His field first strike (the Blast caught a foe): he runs in, fuse lit, and goes off on it */
static EVT_BEGIN(jerry_first_attack)
    CALL(btlevtcmd_GetFirstAttackTarget, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    SET_VALUE(LW(12), PTR(&sFirst))
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 250)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("L_1"))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 20)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -2, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    WAIT_FRAMES(4)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK3"), 0)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 12)
    CALL(evt_eff64, STRING(""), STRING("bomb_n64"), 1, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_btl_camera_shake_h, 0, 5, 0, 10, 13)
    CALL(btlevtcmd_StageDispellFog)
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL_CHILD(jerry_blown_home)
    LABEL(99)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static EVT_BEGIN(jerry_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jerry_damage)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

static EVT_BEGIN(jerry_attack)
    RETURN
EVT_END()

/* he marches in, fuse fizzing, to his place */
static EVT_BEGIN(jerry_entry)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 250)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_SetMoveSoundDataWork, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_SUBMOVE1L"), STRING("SFX_PARTY_BATTLE_SUBMOVE1R"), 0, 9, 9, 0, 0)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -2, 0)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

static EVT_BEGIN(jerry_init)
    CALL(jerry_shout_first, 0, 0)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(jerry_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(jerry_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(jerry_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(btldefaultevt_Confuse))
    CALL(btlevtcmd_SetWalkSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_MOVE1L"), STRING("SFX_PARTY_BATTLE_MOVE1R"), 0, 18, 18)
    CALL(btlevtcmd_SetRunSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_MOVE1L"), STRING("SFX_PARTY_BATTLE_MOVE1R"), 0, 9, 9)
    CALL(btlevtcmd_SetJumpSound, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), STRING("SFX_PARTY_BATTLE_LANDING1"))
    CALL(btlevtcmd_SetEventEntry, BTL_UNIT_SELF, PTR(jerry_entry))
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static EVT_BEGIN(jerry_win)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Y_1"))
    WAIT_FRAMES(48)
    RETURN
EVT_END()

/* his Appeal: a little fuse-lit flourish */
static EVT_BEGIN(jerry_appeal)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_APPEAL)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_MARIO_APPEAL1"), LW(0), LW(1), LW(2), 0)
    CALL(btlevtcmd_AnimeWaitPlayComplete, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_AudienceAppeal, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static void makeWeapons(void) {
    const short rank = pouchGetPtr()->partyData[SLOT].attackLevel;
    /* Lit Fuse: 0 FP, one foe (ground or air), explosive: 2/3 (Super and Ultra 3/4) */
    sLitFuse = partyWeapon_SandersNormalAttack;
    sLitFuse.name = "sl_je_lit";
    sLitFuse.description = "sl_je_lit_h";
    sLitFuse.acHelpMsg = "sl_je_lit_ac";
    sLitFuse.baseFpCost = 0;
    sLitFuse.hpDamageWork[0] = 2, sLitFuse.hpDamageWork[1] = 3;
    sLitFuse.hpDamageWork[2] = 3, sLitFuse.hpDamageWork[3] = 4;
    sLitFuse.hpDamageWork[4] = 3, sLitFuse.hpDamageWork[5] = 4;
    sLitFuse.targetPropertyFlags = TARGET_PROP_START_OPPOSING_SIDE;
    sLitFuse.special &= ~SPECIAL_UNGUARDABLE;
    sLitFuse.special |= SPECIAL_UNGUARDABLE;
    /* a bomb flips what a stomp can't touch: a Buzzy, a Spike Top (their defense 4 took it to 0, for ever) */
    sLitFuse.special |= SPECIAL_FLIPS_SHELLED | SPECIAL_FLIPS_BOMB_FLIPPABLE;
    sLitFuse.attackEvtScript = jerry_lit_fuse;
    /* Big Bang: 4 FP, every foe: 3/4 (Ultra 4/5) */
    sBigBang = partyWeapon_SandersSuperBombAttack;
    sBigBang.special |= SPECIAL_FLIPS_SHELLED | SPECIAL_FLIPS_BOMB_FLIPPABLE;
    sBigBang.name = "sl_je_bang";
    sBigBang.description = "sl_je_bang_h";
    sBigBang.acHelpMsg = "sl_je_bang_ac";
    sBigBang.baseFpCost = 4;
    sBigBang.hpDamageFn = weaponGetPowerFromPartyAttackLv;
    memset(sBigBang.hpDamageWork, 0, sizeof(sBigBang.hpDamageWork));
    sBigBang.hpDamageWork[0] = 3, sBigBang.hpDamageWork[1] = 4;
    sBigBang.hpDamageWork[2] = 3, sBigBang.hpDamageWork[3] = 4;
    sBigBang.hpDamageWork[4] = 4, sBigBang.hpDamageWork[5] = 5;
    sBigBang.attackEvtScript = jerry_big_bang;
    /* Short Fuse: 2 FP, Luigi only: a charge of 3 (1 on a failed command; Ultra 4) */
    memset(&sShortFuse, 0, sizeof(sShortFuse));
    sShortFuse.name = "sl_je_short";
    sShortFuse.icon = partyWeapon_SandersNormalAttack.icon;
    sShortFuse.description = "sl_je_short_h";
    sShortFuse.acHelpMsg = "sl_je_short_ac";
    sShortFuse.baseAccuracy = 100;
    sShortFuse.baseFpCost = 2;
    sShortFuse.reserved_14 = 1.0f;
    sShortFuse.stylishCommandMultiplier = 1;
    sShortFuse.reserved_19 = 1;
    sShortFuse.bingoSlotChance = 100;
    sShortFuse.reserved_1B = 50;
    sShortFuse.targetClassFlags = TARGET_CLASS_ONLY_MARIO | TARGET_CLASS_NO_OPPOSING_ALLIANCE | TARGET_CLASS_NO_SELF |
                                  TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_SINGLE;
    sShortFuse.acLevel = 3;
    sShortFuse.reserved_6F = 2;
    sShortFuse.special = SPECIAL_UNGUARDABLE;
    sShortFuse.counterResistFlags = COUNTER_RESIST_ALL;
    sShortFuse.chargeStrength = rank >= 2 ? 4 : 3;
    sShortFuse.attackEvtScript = jerry_short_fuse;
    /* (a failed command charges 1: a weapon of its own) */
    /* the field first strike */
    sFirst = partyWeapon_SandersFirstAttack;
    sFirst.hpDamageWork[0] = sFirst.hpDamageWork[1] = 3;
    sFirst.hpDamageWork[2] = sFirst.hpDamageWork[3] = 4;
    sFirst.hpDamageWork[4] = sFirst.hpDamageWork[5] = 4;
}

static BattleUnitKind* jerryUnit(void) {
    const BattleDataEntry* d;
    int n = 0;
    makeWeapons();
    sParts[0] = unitdata_Party_Sanders.parts[0];
    sParts[0].partName = "sl_name_jerry";
    sParts[0].modelName = "c_lp_bom";
    sParts[0].poseTable = kBattlePoses;
    for (d = unitdata_Party_Sanders.dataTable; d != NULL && d->tag != 0 && n < 15; d++) {
        sData[n] = *d;
        if (d->tag == 1) {
            sData[n].data = (void*)makeMenu;
        } else if (d->tag == 5) {
            sData[n].data = jerry_appeal;
        } else if (d->tag == 64) {
            sData[n].data = jerry_win;
        } else if (d->tag == 6) {
            sData[n].data = jerry_appeal; /* the audience attack: his appeal instead */
        }
        n++;
    }
    sData[n].tag = 0;
    sData[n].data = NULL;
    sKind = unitdata_Party_Sanders;
    sKind.name = "sl_name_jerry";
    sKind.maxHp = 10;
    sKind.parts = sParts;
    sKind.initEventCode = jerry_init;
    sKind.dataTable = sData;
    return &sKind;
}

/* While he's in the slot, the field first strike Bobbery's slot gives is his */
static EvtWord* sFirstWas;
static void swapped(int on) {
    if (on) {
        makeWeapons();
        if (sFirstWas == NULL) {
            sFirstWas = partyWeapon_SandersFirstAttack.attackEvtScript;
        }
        partyWeapon_SandersFirstAttack.attackEvtScript = jerry_first_attack;
    } else if (sFirstWas != NULL) {
        partyWeapon_SandersFirstAttack.attackEvtScript = sFirstWas;
        sFirstWas = NULL;
    }
}

/* ======================================================================
 * Field: Blast
 * ====================================================================== */

static struct {
    int state;   /* 0 off, 1 the fuse lit, 2 gone off */
    int frames;  /* left of the fuse, then of the burst */
    s32 fizz;    /* the fizz's sound */
    int hits;    /* what the blast reached */
    int lines;   /* the not-here lines' turn */
    int stepped; /* he has stepped out from behind Luigi */
    Vec lit;     /* where he stood as the fuse was lit (the blast reaches from there too) */
} sBlast;

static BOOL floorFilter(HitCheckQuery* q, HitEntry* e) {
    (void)q;
    return (e->attributes & (HIT_ATTR_MOBJ | HIT_ATTR_DEEP_WATER)) == 0;
}

/* 1 if there is solid floor at x, z within a step of height y */
static int floorAt(float x, float y, float z) {
    f32 hx, hy, hz, nx, ny, nz, dist = 60.0f;
    if (hitCheckFilter(floorFilter, &hx, &hy, &hz, &dist, &nx, &ny, &nz, x, y + 30.0f, z, 0.0f, -1.0f, 0.0f) == NULL) {
        return 0;
    }
    return fabsf(hy - y) < 12.0f;
}

/* As the fuse is lit he trails behind Luigi, and from the camera Luigi hides him: he steps out to
 * Luigi's side, toward the camera, and stands there fizzing (the first spot with a floor under it) */
static void stepOut(PartyEntry* party) {
    MarioWork* m = marioGetPtr();
    float side = party->position.x < m->position.x ? -1.0f : 1.0f;
    static const float kSpots[][2] = {{28.0f, 22.0f}, {-28.0f, 22.0f}, {34.0f, 8.0f}, {-34.0f, 8.0f}};
    int i;
    for (i = 0; i < 4; i++) {
        float x = m->position.x + side * kSpots[i][0];
        float z = m->position.z + kSpots[i][1];
        float d = hypotf(x - party->position.x, z - party->position.z);
        if (!floorAt(x, m->position.y, z)) {
            continue;
        }
        /* stood still first, so the walk ends standing there (it returns to the run mode before it) */
        partyStop(party);
        if (d > 4.0f) {
            partySetForceMove(party, (s32)(d / 2.5f + 0.5f), angleABf(party->position.x, party->position.z, x, z),
                              2.5f);
        }
        return;
    }
}

static PartyEntry* sParty; /* the entry his ability was used on */
static PartyEntry* jerryEntry(void) {
    if (sParty == NULL || !(sParty->flags & 1)) {
        return NULL;
    }
    return sParty->currentMemberId == SLOT ? sParty : NULL;
}

static EVT_BEGIN(blast_quake)
    CALL(evt_cam_shake, 4, FLOAT(0.03f), FLOAT(0.03f), 400)
    RETURN
EVT_END()

/* Luigi too close: thrown back, unhurt */
static EVT_BEGIN(blast_thrown)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_set_pose, STRING("M_D_2"))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    ADD(LW(0), LW(3))
    CALL(evt_mario_jump_pos, LW(0), LW(1), LW(2), 400, 0, 30)
    CALL(evt_mario_set_pose, STRING("M_S_1"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(blast_nothing)
    WAIT_MS(700)
    CALL(evt_msg_print, 0, LW(0), 0, STRING("party"))
    RETURN
EVT_END()

/* the first Blast ever: the room's own line, if it wants one (ch02's tutorial) */
__attribute__((weak)) void portSlC2BlastDone(int hits) {
    (void)hits;
}

static void goOff(PartyEntry* party) {
    MarioWork* m = marioGetPtr();
    EffectEntry* eff;
    int i;
    float dx, dz;
    if (sBlast.fizz >= 0) {
        psndSFXOff(sBlast.fizz);
        sBlast.fizz = -1;
    }
    party->flags2 &= ~PARTY_DISP_FLAG_POSE_LOCK;
    partyChgPose(party, "F_1");
    party->flags2 |= PARTY_DISP_FLAG_POSE_LOCK;
    party->forceMoveFrames = 0; /* (a step out cut short) */
    partyStop(party);
    psndSFXOn_3D((const char*)0x93B, &party->position);
    eff = effSandarsEntry(pouchGetPartyAttackLv((PartyId)SLOT), party->position.x, party->position.y + party->height,
                          party->position.z, 0.0f);
    if (eff != NULL) {
        ((EffSandarsWork*)eff->userdata)->camId = party->camId;
    }
    evtEntryType(blast_quake, 0, 0, 0);
    fbatSandersBombTriggerOn(); /* the game's partner-bomb hit: a field foe caught is a first strike */
    sBlast.hits = 0;
    for (i = 0; i < PORT_SL_POINTS; i++) {
        PortSlPoint* p = portSlPointGet(PORT_SL_ABILITY_BLAST, i);
        if (p == NULL || !p->on || p->used) {
            continue;
        }
        /* from where he stands, or from where he was lit (before he stepped out toward the camera) */
        if (hypotf(party->position.x - p->x, party->position.z - p->z) <= p->radius + BLAST_REACH * 0.5f ||
            hypotf(sBlast.lit.x - p->x, sBlast.lit.z - p->z) <= p->radius + BLAST_REACH * 0.5f) {
            portSlPointRun(PORT_SL_ABILITY_BLAST, p);
            sBlast.hits++;
        }
    }
    dx = m->position.x - party->position.x;
    dz = m->position.z - party->position.z;
    if (hypotf(dx, dz) < 32.0f && fabsf(m->position.y - party->position.y) < 40.0f) {
        EventEntry* e = evtEntry(blast_thrown, 0, 0);
        if (e != NULL) {
            e->lwData[3] = dx >= 0.0f ? 50 : -50;
        }
    }
    printf("[superluigi] Jerry's Blast at %.0f %.0f %.0f: %d point(s)\n", party->position.x, party->position.y,
           party->position.z, sBlast.hits);
    fflush(stdout);
    sBlast.state = 2;
    sBlast.frames = 48;
}

static void blastFrame(void) {
    PartyEntry* party;
    if (sBlast.state == 0 || gp == NULL) {
        return;
    }
    party = jerryEntry();
    if (party == NULL || gp->inBattle) {
        /* he went in (the party switched, a battle, a new room) */
        printf("[superluigi] Jerry's fuse out (party %d, in battle %d)\n", marioGetPartyId(), (int)gp->inBattle);
        fflush(stdout);
        if (sBlast.fizz >= 0) {
            psndSFXOff(sBlast.fizz);
            sBlast.fizz = -1;
        }
        if (party != NULL) {
            party->flags2 &= ~PARTY_DISP_FLAG_POSE_LOCK;
        }
        sBlast.state = 0;
        return;
    }
    if (sBlast.state == 1) {
        if (!sBlast.stepped) {
            sBlast.stepped = 1;
            sBlast.lit = party->position;
            stepOut(party);
        }
        /* the countdown: the fizz, and a blink as it runs down */
        if (sBlast.frames < 50 && (sBlast.frames & 7) == 0) {
            psndSFXOn_3D("SFX_ENM_BOMB_SPARK1", &party->position);
        }
        if (--sBlast.frames <= 0) {
            goOff(party);
        }
        return;
    }
    if (--sBlast.frames <= 0) {
        party->flags2 &= ~PARTY_DISP_FLAG_POSE_LOCK;
        partyChgPoseId(party, 1);
        partyRun(party);
        sBlast.state = 0;
        if (sBlast.hits == 0) {
            static const char* const kNothing[] = {"sl_je_noblast_1", "sl_je_noblast_2", "sl_je_noblast_3"};
            EventEntry* e = evtEntry(blast_nothing, 0, 0);
            if (e != NULL) {
                e->lwData[0] = (EvtWord)(AddrWord)kNothing[sBlast.lines++ % 3];
            }
        }
        portSlC2BlastDone(sBlast.hits);
    }
}

/* The slot's use function while Jerry is in it: lights the fuse and hands
 * him straight back to following (the countdown runs each frame, blastFrame);
 * used again while lit, he goes off at once */
static void jerryUse(PartyEntry* party) {
    sParty = party;
    if (sBlast.state == 0) {
        printf("[superluigi] Jerry lights his fuse\n");
        fflush(stdout);
    }
    if (sBlast.state == 1) {
        sBlast.frames = 1;
    } else if (sBlast.state == 0) {
        sBlast.state = 1;
        sBlast.frames = FUSE_FRAMES;
        sBlast.stepped = 0;
        partyChgPose(party, "L_1");
        party->flags2 |= PARTY_DISP_FLAG_POSE_LOCK;
        sBlast.fizz = psndSFXOn_3D((const char*)0x93C, &party->position);
        psndSFXOn_3D("SFX_ENM_BOMB_SPARK1", &party->position);
    }
    party->trigFlags &= ~PARTY_TRIG_FLAG_CHG_RUN_MODE;
    partyChgRunMode(party, 2);
    partyChgMoveMode(party, 0);
    partyChgMot(party, 0);
    party->flags &= ~PARTY_FLAG_IS_BEING_USED;
}

/* 1 while his fuse is lit or he's just gone off (the scenes wait for it) */
int portSlJerryBlasting(void) {
    return sBlast.state != 0;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void registerOnce(void) {
    static int done;
    if (!done) {
        done = 1;
        sBlast.fizz = -1;
        portModHostAddHook(1, kSlot_marioStMain, (void*)blastFrame);
    }
}

static void jerrySwapped(int on) {
    registerOnce();
    swapped(on);
}

static const char* const kText[] = {
    "sl_name_jerry", "Jerry",
    "sl_je_menu", "Plumpbelly's champion. A Bob-omb\nof honour, fuse and very few\nregrets.",
    "sl_je_lit", "Lit Fuse",
    "sl_je_lit_h", "Throws a bomb lit from his fuse\nat one foe, on the ground or in\nthe air. Spikes don't matter.",
    "sl_je_lit_ac", "Hold <icon PAD_A 0.6 0 22 0> and let go when the\nfuse meter lights up!",
    "sl_je_bang", "Big Bang",
    "sl_je_bang_h", "Jerry swells up and bursts,\ncatching every foe in the\nblast.",
    "sl_je_bang_ac", "Press <icon PAD_A 0.6 0 22 0> as fast as you can to\nfill the blast gauge!",
    "sl_je_short", "Short Fuse",
    "sl_je_short_h", "Jerry winds himself up and\ncharges Luigi's next attack.",
    "sl_je_short_ac", "Rotate <icon STICK 0.6 0 22 0> until the fuse\ncatches!",
    /* his shouts close by themselves (no key wait): his move goes straight on to its command */
    "sl_je_bt_lit", "Fire in the hole!<wait 500>",
    "sl_je_bt_bang", "For PLUMPBELLY!<wait 500>",
    "sl_je_bt_short", "Strike while I'm hot, sir!<wait 500>",
    "sl_je_noblast_1", "A waste of good powder. I shall\npretend that did not happen.\n<k>",
    "sl_je_noblast_2", "Boom, and nothing. Story of\nmy life.\n<k>",
    "sl_je_noblast_3", "I require a target, sir.\n<k>",
    NULL,
};

static const PortSlFriend kJerry = {
    PORT_SL_FRIEND_JERRY,
    SLOT,
    "Jerry",
    "c_lp_bom",
    "Plumpbelly's champion. A Bob-omb\nof honour, fuse and very few\nregrets.",
    kPoses,
    jerryUse,
    NULL,
    "S_1",
    jerryUnit,
    sWaza,
    "art/icons/jerry/jerry_menu.png",
    "art/icons/jerry/jerry_hud.png",
    10,
    0,
    kText,
    jerrySwapped,
};

const PortSlFriend* portSlFriendJerry(void) {
    return &kJerry;
}
