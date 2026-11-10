/* Watt's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/watt.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include.
 *
 * Watt's rows (BattleWatt in npc_sprite_names.yaml): Idle 1, Run 3, Strain 5, StrainBigger 6, StrainBiggest 7
 * (drawn with the bolts row 16 over it), Celebrate 8, Hurt 12. PM64 darkens the stage's backdrop while he charges
 * (its SetBackgroundAlpha, up to 200-225): pm64Backdrop, a dark behind the hindmost unit only (TTYD's own stage
 * darkening dims the units too, so it isn't used); his glow stays.
 *
 * Timings: PM64's frame counts through PM64_FRAMES (PM64 30 a second, TTYD 60), per-frame steps through
 * PM64_PER_FRAME, gravity through PM64_ACCEL; the shared blocks (sHitAllEvt's LW(11), PM64_HOME, ...) take PM64's
 * numbers raw. Online: whatever the command decides is read after ResultAC (GetResultAC / AcGetOutputParam),
 * never from the pad. */


/* the action-command help texts of the TTYD commands the scripts set up (TTYD's own message tags, so another
 * partner's row changing a host weapon's text never changes Watt's) */
static BattleWeapon sWattHelpHoldA = {.acHelpMsg = "msg_ac_bakuhatsu"};        /* Bobbery's Bomb: hold A, let go */
static BattleWeapon sWattHelpMash = {.acHelpMsg = "msg_ac_super_bakuhatsu"};   /* Bob-ombast: mash A */
static BattleWeapon sWattHelpThreeLights = {.acHelpMsg = "msg_ac_wonder_egg"}; /* Mini-Egg: A at each light */

/* hold A, let go when the gauge is full (Bobbery's Bomb's command), the gauge filling in PM64 Electro Dash's 102
 * frames (its body-slam command's length when A goes down at once) */
#define WATT_AC_HOLD_A                                                                                        \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(1), 60)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(1), 45)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(1), 30)                                                                                  \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(1), 25)                                                                                  \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(1), 20)                                                                                  \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(1), 12)                                                                                  \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(1), 6)                                                                                   \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 2, PM64_FRAMES(102), LW(1), 0, 100, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)  \
    CALL(btlevtcmd_AcSetFlag, 15)                                                                             \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 15, 1, 0)

/* mash A to fill the gauge in frames (TTYD frames: give it PM64_FRAMES(PM64's length)); TTYD's mash gauge,
 * Bob-ombast's presses and drain by difficulty; a full gauge stays full and is the Nice: PM64's Power Shock and
 * Mega Shock mash meters. AcGetOutputParam 1 (after ResultAC): the fill */
#define WATT_AC_MASH(frames)                                                                                  \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(1), 20)                                                                                  \
        SET_VALUE(LW(2), 20)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(1), 18)                                                                                  \
        SET_VALUE(LW(2), 22)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(1), 17)                                                                                  \
        SET_VALUE(LW(2), 23)                                                                                  \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(1), 16)                                                                                  \
        SET_VALUE(LW(2), 24)                                                                                  \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(1), 15)                                                                                  \
        SET_VALUE(LW(2), 26)                                                                                  \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(1), 14)                                                                                  \
        SET_VALUE(LW(2), 28)                                                                                  \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(1), 13)                                                                                  \
        SET_VALUE(LW(2), 28)                                                                                  \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, 1, frames, 178, LW(1), LW(2), 100, 100, EVT_NULLPTR)                        \
    CALL(btlevtcmd_AcSetGaugeParam, 45, 75, 100, 100)                                                         \
    CALL(btlevtcmd_AcSetFlag, 8)                                                                              \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)

/* three lights along a bar filling in PM64's 100 frames, A as each comes on (Mini-Egg's command): PM64's
 * three-chances command. AcGetOutputParam 0 (after ResultAC): how many were caught (0-3) */
#define WATT_AC_THREE_LIGHTS                                                                                  \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcSetParamAll, 0, PM64_FRAMES(100), 3, 0, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR) \
    CALL(btlevtcmd_AcSetFlag, 0)                                                                              \
    CALL(btlevtcmd_AcSetOutputParam, 1, 0)                                                                    \
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 20, 1, 60)

/* wattMarioFree(out): out := 1 when Mario can move (not asleep, stopped or frozen: PM64's immobilized test that
 * picks Turbo Charge's healthy or immobile version) */
static USER_FUNC(wattMarioFree) {
    const BattleUnit* mario = evtUnit(event, (EvtWord)BTL_UNIT_MARIO);
    const int canMove = mario != NULL && mario->sleepTurns <= 0 && mario->stopTurns <= 0 && mario->freezeTurns <= 0;
    evtSetValue(event, event->args[0], canMove);
    return EVT_RETURN_DONE;
}

/* wattOrbit(spin): Turbo Charge's whirl (as PM64 watt.c N(TurboChargeUnwindWatt) moves him): the unit circles a
 * point 36 over Mario at the distance it stood from Mario's feet, from his far side (angle 90; 0 is straight
 * below, 180 straight above), PM64's 5 degrees a frame growing by 0.5 a frame for 90 frames, then on at that speed
 * until it comes round past the bottom to under 45 degrees. It faces back over the first half turn, forward over
 * the second. spin 1: Mario spins on the spot meanwhile, PM64's 10 degrees a frame, one more each frame. Blocks.
 * (TTYD's frames are half PM64's: each step halved; a growth per frame quartered, as it is per frame squared.) */
static USER_FUNC(wattOrbit) {
    static struct {
        float cx, cy, z, radius, angle, speed, spin;
        int accel;
    } o;
    BattleUnit* self = evtUnit(event, (EvtWord)BTL_UNIT_SELF);
    BattleUnit* mario = evtUnit(event, (EvtWord)BTL_UNIT_MARIO);
    const int spin = evtGetValue(event, event->args[0]);
    float a;
    if (self == NULL || mario == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        const float dx = self->position.x - mario->position.x, dy = self->position.y - mario->position.y;
        o.radius = sqrtf(dx * dx + dy * dy);
        o.cx = mario->position.x;
        o.cy = mario->position.y + 36.0f;
        o.z = self->position.z;
        o.angle = 90.0f;
        o.speed = PM64_PER_FRAME(5.0f);
        o.spin = PM64_PER_FRAME(10.0f);
        o.accel = PM64_FRAMES(90);
    }
    if (o.accel > 0) {
        o.speed += PM64_PER_FRAME(PM64_PER_FRAME(0.5f));
    } else {
        a = o.angle + o.speed;
        while (a >= 360.0f) {
            a -= 360.0f;
        }
        if (a < 45.0f) {
            /* round again: done, both facing the enemies */
            self->faceDirection = 1;
            mario->rotation.y = 0.0f;
            return EVT_RETURN_DONE;
        }
    }
    a = 0.017453292f * o.angle;
    BtlUnit_SetPos(self, o.cx + o.radius * sinf(a), o.cy - o.radius * cosf(a), o.z);
    o.angle += o.speed;
    while (o.angle >= 360.0f) {
        o.angle -= 360.0f;
    }
    if (o.accel > 0) {
        o.accel--;
    }
    self->faceDirection = o.angle > 180.0f ? 1 : -1;
    if (spin) {
        mario->rotation.y += o.spin;
        while (mario->rotation.y >= 360.0f) {
            mario->rotation.y -= 360.0f;
        }
        o.spin += PM64_PER_FRAME(PM64_PER_FRAME(1.0f));
    }
    return EVT_RETURN_BLOCK;
}

/* wattTurboWeapon(weapon, turns): weapon := a copy of it giving Mario attack +1 for turns (1-3) turns (PM64's
 * N(ApplyTurboCharge): the turns are the command's result) */
static USER_FUNC(wattTurboWeapon) {
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    int turns = evtGetValue(event, event->args[1]);
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    turns = turns < 1 ? 1 : turns > 3 ? 3 : turns;
    sCopy.atkChangeChance = 100;
    sCopy.atkChangeTime = (s8)turns;
    sCopy.atkChangeStrength = 1;
    if (pm64Log()) {
        printf("[pm64] Turbo Charge: %d turns\n", turns);
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* Watt's Turbo Charge. PM64: battle/partner/watt.c N(EVS_Move_TurboCharge) -> N(EVS_TurboCharge_HealthyPlayer) /
 * N(EVS_TurboCharge_ImmobilePlayer), N(TurboChargeUnwindWatt), N(TurboChargeFX), N(ApplyTurboCharge).
 * PM64: wait 5; the camera on Mario's side; Watt turns round to face Mario (yaw 0 to 180 in 30-degree steps a frame)
 * and waits 10; the three-chances command starts (100 frames). Alongside: Watt (Strain, blurred) whirls round Mario
 * (N(TurboChargeUnwindWatt): 36 over Mario, 5 degrees a frame +0.5 a frame for 90 frames, then on until past the
 * bottom), Mario spinning on the spot (+10 degrees a frame, one more each frame) unless he can't move; then Run, home
 * in 10 frames (a 10 dip, cubic out), Idle, 10 more. Meanwhile: 30 frames in, the charge sound and the thunderbolt
 * ring over Mario (42 up, 60 frames); 59 later the camera pulls back and Mario hops on the spot (20 frames, gravity
 * 1.0, Jump/Fall/Land, Idle 4 later; not when he can't move); 30 later the camera home, the turns = the command's
 * result (0-3: none, nothing happens), the stat-change mark over Mario and "Turbo Charge for N turns".
 * Ours: the same steps and timings; Mini-Egg's three-lights command for PM64's three chances (each light caught a
 * turn, read from the command after it ends; Mario's hop then); the facing flips instead of PM64's 3D turn; TTYD's
 * electricity on Mario for the thunderbolt ring; TTYD's attack-up (+1, the turns caught) and its status display
 * for the mark and message. No blur (TTYD has none for the sprite); Mario's UsePower pose left out. */
static EVT_BEGIN(sTurboChargeEvt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(wattMarioFree, LW(9))
    WAIT_FRAMES(PM64_FRAMES(5))
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    WAIT_FRAMES(PM64_FRAMES(15))
    WATT_AC_THREE_LIGHTS
    WAIT_FRAMES(1) /* (a frame after SetupAC: the command reads its parameters then; started the same frame it never does) */
    CALL(btlevtcmd_StartAC, 1)
    CALL(evt_btl_camera_set_zoom, 0, 175)
    BEGIN_SIBLING_EVENT
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        CALL(wattOrbit, LW(9))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -10, 5, 0, -1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    END_SIBLING_EVENT
    WAIT_FRAMES(PM64_FRAMES(30))
    CALL(evt_snd_sfxon, STRING("SOUND_WATT_TURBO_CHARGE|SFX_CONDITION_CHARGE1"), 0)
    CALL(pm64Fx, 2, LW(3), 42)
    WAIT_FRAMES(PM64_FRAMES(59))
    CALL(pm64Fx, 0, LW(3), 0)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 0, LW(8))
    CALL(btlevtcmd_StopAC)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    IF_INT_EQUAL(LW(9), 1)
        BEGIN_SIBLING_EVENT
            CALL(btlevtcmd_snd_se, BTL_UNIT_MARIO, STRING("SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_MARIO, 1, STRING("M_J_1B"))
            CALL(btlevtcmd_SetFallAccel, BTL_UNIT_MARIO, PM64_ACCEL(1.0f))
            CALL(btlevtcmd_GetPos, BTL_UNIT_MARIO, LW(0), LW(1), LW(2))
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_MARIO, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
            WAIT_FRAMES(PM64_FRAMES(4))
            CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_MARIO, 1, BTL_POSE_STAY)
        END_SIBLING_EVENT
    END_IF
    WAIT_FRAMES(PM64_FRAMES(30))
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    IF_INT_GREATER(LW(8), 0)
        CALL(wattTurboWeapon, LW(12), LW(8))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        SET_VALUE(LW(0), LW(8))
        SUBTRACT(LW(0), 1)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_SYSTEM, LW(0), LW(6))
        IF_INT_GREATER_OR_EQUAL(LW(6), 0)
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        END_IF
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    WAIT_FRAMES(30)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(99)
    CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, -1, -1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Watt's Electro Dash. PM64: battle/partner/watt.c N(EVS_Attack_ElectroDash), N(dashToTarget), N(charge),
 * N(EVS_ElectroDashDelay), N(EVS_ReturnHome_Success/_Miss), N(ElectroDashFX).
 * PM64: the body-slam command (hold A, let go when it fills), wait 10; up to 60 frames for A to go down, then he
 * flies (Run) at the enemy, to 20 short of it, in 20 frames (linear), while A is held; there he charges (Strain ->
 * StrainBigger, the radial shimmer on him 12 up at 1.3, the backdrop darkening 30 a frame to 200) 55 frames, then 30
 * more. Letting go of A at any point (or the time running out) ends it there: wait 2, Strain, and he darts from
 * wherever he is to 5 short of the enemy in 5 frames (linear), the backdrop back 2 frames in. Hit: one shock hit
 * through defense, 1, or 3 / 4 / 5 by rank when the command went well; then home (Run, 30 frames, cosine in-out,
 * Idle). Dodged: he carries on to 40 past it and 10 up in 10 frames (a 20 dip, quadratic out; Idle 5 frames in),
 * waits 20 and flies home the same way.
 * Ours: the same path, frames, easing and rows; TTYD's hold-A command (Bobbery's Bomb's) filling in PM64's 102
 * frames; the dash and charge run beside the command and stop the moment it ends (ResultAC: the same on every
 * screen online), as PM64's threads are killed. He sets off at once rather than on A going down (the pad is each
 * screen's own; the gauge itself waits for A). Watt's charge glow for the shimmer; pm64Backdrop for the backdrop (behind the units only). */
static EVT_BEGIN(sElectroDashEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    WATT_AC_HOLD_A
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    WAIT_FRAMES(PM64_FRAMES(10))
    CALL(btlevtcmd_StartAC, 1)
    /* the dash and the charge, cut short when the command ends */
    BEGIN_INLINE_EVENT_GET_ID(LW(10))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), 0, 0, 0, -1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        CALL(evt_snd_sfxon, STRING("SOUND_WATT_CHARGE|SFX_CONDITION_CHARGE1"), 0)
        CALL(pm64Fx, 5, BTL_UNIT_SELF, 12)
        CALL(pm64Backdrop, 200, PM64_FRAMES(7)) /* (PM64: 30 a frame to 200) */
        CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
        WAIT_FRAMES(PM64_FRAMES(55))
        WAIT_FRAMES(PM64_FRAMES(30))
    END_INLINE_EVENT
    CALL(btlevtcmd_ResultAC)
    DELETE_EVENT(LW(10))
    WAIT_FRAMES(PM64_FRAMES(2))
    CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
    CALL(evt_snd_sfxon, STRING("SOUND_WATT_POWER_SHOCK|SFX_BTL_DAMAGE_BIRIBIRI1"), 0)
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(2))
        CALL(pm64Backdrop, 0, 0)
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 5)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(5), 0, 0, 0, -1)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        BEGIN_SIBLING_EVENT
            WAIT_FRAMES(PM64_FRAMES(5))
            CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
        END_SIBLING_EVENT
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
        ADD(LW(1), 10)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -20, 4, 0, -1)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        WAIT_FRAMES(PM64_FRAMES(20))
        GOTO(95)
    END_IF
    SET_VALUE(LW(15), 0)
    CALL(pm64Fx, 6, LW(3), 12)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    LABEL(95)
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    PM64_END
EVT_END()

/* home (Run, 30 frames, cosine in-out, Idle): PM64's N(EVS_ReturnHome_Success/_Miss) */
#define WATT_HOME                                                                                             \
    PM64_CAM_STAGE                                                                                            \
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)                                                                   \
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))                                            \
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)           \
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)

/* Watt's Power Shock. PM64: battle/partner/watt.c N(EVS_Attack_PowerShock), N(PowerShockFX),
 * N(PowerShockDischargeFX), N(TargetParalyzeChance), N(EVS_ReturnHome_Success/_Miss).
 * PM64: he flies (Run) to 15 short of the enemy and 10 under its middle in 30 frames (cosine in-out), Idle; the
 * camera closes in (80 frames). The mash command runs (72 frames, harder the less the enemy can be paralyzed):
 * snaking static on him (15 up, 75 frames), StrainBigger, the backdrop darkening 3 a frame (75 frames), wait 75.
 * Then the power-shock sound; he swells 1.0 -> 2.2 in 3 frames (+0.4 a frame) with the energy in-out burst 8 up;
 * the thunderbolt ring 20 up, the bulb glow 20 frames; the backdrop flickers (clear / dark, 1 frame each, 3 times)
 * and clears; wait 10; he shrinks back 2.2 -> 1.0 over 12 frames (centred 20 up), Idle. Dodged: wait 15, home.
 * Hit: no damage, paralysis 3 turns (always, with a full meter; at its chance otherwise); he backs off to 40 short
 * at the enemy's middle (15 frames, a 10 dip, cosine in-out), waits 30 and flies home (Run, 30 frames, Idle).
 * Ours: the same path, frames and rows; TTYD's mash gauge for PM64's (PM64's 72 frames), its fill (read after the
 * command) the paralysis chance (TTYD's resistance on top, for PM64's harder meter); TTYD's electricity for the
 * snaking static, the charge sparkles for the energy burst, his glow for the bulb;
 * the shock flash on the enemy as it is struck (PM64's shock hit, sized to it); pm64Backdrop for the backdrop; the charge sparkles for
 * the ring (no big flash: PM64 has no bolts). The
 * swell grows from his feet (TTYD scales from there). The ceiling case (PM64 backs off 10 down) is left out. */
static EVT_BEGIN(sPowerShockEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 15)
    SUBTRACT(LW(1), 10)
    IF_INT_LESS(LW(1), 0)
        SET_VALUE(LW(1), 0)
    END_IF
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL(evt_btl_camera_set_zoom, 0, 220)
    WATT_AC_MASH(PM64_FRAMES(72))
    WAIT_FRAMES(1) /* (a frame after SetupAC: the command reads its parameters then; started the same frame it never does) */
    CALL(btlevtcmd_StartAC, 1)
    CALL(pm64Fx, 2, BTL_UNIT_SELF, 15)
    CALL(evt_snd_sfxon, STRING("SOUND_WATT_CHARGE|SFX_CONDITION_CHARGE1"), 0)
    CALL(pm64Backdrop, 225, PM64_FRAMES(75)) /* (PM64: 3 a frame, 75 frames) */
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    /* the discharge */
    CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK1"), 0)
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.4f), FLOAT(1.4f), FLOAT(1.0f))
        WAIT_FRAMES(PM64_FRAMES(1))
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.8f), FLOAT(1.8f), FLOAT(1.0f))
        WAIT_FRAMES(PM64_FRAMES(1))
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(2.2f), FLOAT(2.2f), FLOAT(1.0f))
        WAIT_FRAMES(PM64_FRAMES(1))
        CALL(pm64Fx, 1, BTL_UNIT_SELF, 8)
    END_SIBLING_EVENT
    CALL(pm64Fx, 1, BTL_UNIT_SELF, 20)
    CALL(evt_btl_camera_set_zoom, 0, 300)
    BEGIN_SIBLING_EVENT
        CALL(pm64Fx, 5, BTL_UNIT_SELF, 12)
        WAIT_FRAMES(PM64_FRAMES(20))
        CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)
    END_SIBLING_EVENT
    LOOP(3) /* (PM64: the backdrop flickers clear / dark, then clears) */
        CALL(pm64Backdrop, 0, 0)
        WAIT_FRAMES(PM64_FRAMES(1))
        CALL(pm64Backdrop, 200, 0)
        WAIT_FRAMES(PM64_FRAMES(1))
    END_LOOP
    CALL(pm64Backdrop, 0, 0)
    WAIT_FRAMES(PM64_FRAMES(10))
    BEGIN_SIBLING_EVENT
        SET_FLOAT_VALUE(LW(0), FLOAT(2.2f))
        LOOP(12)
            SUBTRACT_FLOAT(LW(0), FLOAT(0.1f))
            CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, LW(0), LW(0), FLOAT(1.0f))
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    END_SIBLING_EVENT
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        WAIT_FRAMES(PM64_FRAMES(15))
        GOTO(95)
    END_IF
    CALL(pm64StatusWeapon, LW(12), 1, LW(8))
    SET_VALUE(LW(15), 0)
    CALL(evt_snd_sfxon, STRING("SOUND_WATT_POWER_SHOCK|SFX_BTL_DAMAGE_BIRIBIRI1"), 0)
    CALL(pm64Fx, 6, LW(3), 12)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 40)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(15), -10, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(30))
    LABEL(95)
    WATT_HOME
    PM64_END
EVT_END()

/* Watt's Mega Shock. PM64: battle/partner/watt.c N(EVS_Attack_MegaShock), N(MegaShockFX), N(PowerShockDischargeFX),
 * N(AverageTargetParalyzeChance), N(EVS_ReturnHome_Success/_Miss).
 * PM64: the camera on his side; he flies (Run) 40 forward and 30 up from where he is in 20 frames (cosine in-out),
 * Idle; the backdrop darkens 3 a frame (75 frames). The mash command (87 frames, by the enemies' average paralysis
 * chance): snaking static on him (15 up) and the radial shimmer (12 up, 1.3), 90 frames; StrainBigger; the camera
 * closes in over 90; wait 90. Then the backdrop clears, flashes back to 60 and fades over 20 frames; the
 * mega-discharge shockwave from him (12 up), again 10 frames later; StrainBiggest 5 frames -> Strain 30 -> Idle; the
 * camera pulls out wide and drifts 30 frames; the bulb glow 10 frames, the thunderbolt ring 30 frames; each enemy in
 * turn, 5 frames apart: no damage, paralysis 3 turns at the meter's level (always when full). Wait 30, home (Run,
 * 30 frames, cosine in-out, Idle).
 * Ours: the same path, frames and rows; TTYD's mash gauge (PM64's 87 frames), its fill (read after the command)
 * the paralysis chance; TTYD's electricity and Watt's charge glow for the static and shimmer, the charge sparkles
 * twice for the shockwaves (no big flash: PM64 has no bolts) and the bolts row over StrainBiggest; the shock flash on each enemy as it is struck (sized to it, as PM64's); the
 * stage camera for PM64's wide shot; pm64Backdrop for the backdrop (behind the units only). */
static EVT_BEGIN(sMegaShockEvt)
    PM64_BEGIN(PM64_ALL)
    PM64_CAM_SELF
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
    ADD(LW(1), 30)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), 0, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(pm64Backdrop, 225, PM64_FRAMES(75)) /* (PM64: 3 a frame, 75 frames) */
    WATT_AC_MASH(PM64_FRAMES(87))
    WAIT_FRAMES(1) /* (a frame after SetupAC: the command reads its parameters then; started the same frame it never does) */
    CALL(btlevtcmd_StartAC, 1)
    CALL(pm64Fx, 2, BTL_UNIT_SELF, 15)
    CALL(pm64Fx, 5, BTL_UNIT_SELF, 12)
    CALL(evt_snd_sfxon, STRING("SOUND_WATT_CHARGE|SFX_CONDITION_CHARGE1"), 0)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    /* the discharge */
    CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)
    CALL(pm64Backdrop, 60, 0) /* (PM64: clear, then 60 fading 3 a frame over 20) */
    CALL(pm64Backdrop, 0, PM64_FRAMES(20))
    BEGIN_SIBLING_EVENT
        /* (EFFECT_FLASHING_BOX_SHOCKWAVE twice, 10 frames apart: the flash of light outward) */
        CALL(evt_snd_sfxon, STRING("SOUND_WATT_MEGA_CHARGE_WAVE|SFX_BTL_THUNDERS_ATTACK1"), 0)
        CALL(pm64Fx, 21, BTL_UNIT_SELF, 12)
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(evt_snd_sfxon, STRING("SOUND_WATT_MEGA_CHARGE_WAVE|SFX_BTL_THUNDERS_ATTACK1"), 0)
        CALL(pm64Fx, 21, BTL_UNIT_SELF, 12)
    END_SIBLING_EVENT
    BEGIN_SIBLING_EVENT
        CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
        WAIT_FRAMES(PM64_FRAMES(5))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        WAIT_FRAMES(PM64_FRAMES(30))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    END_SIBLING_EVENT
    BEGIN_SIBLING_EVENT
        CALL(pm64Fx, 5, BTL_UNIT_SELF, 12)
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(pm64Fx, 0, BTL_UNIT_SELF, 0)
    END_SIBLING_EVENT
    PM64_CAM_STAGE
    CALL(pm64StatusWeapon, LW(12), 1, LW(8))
    SET_VALUE(LW(11), 5)
    SET_VALUE(LW(13), 6)
    SET_VALUE(LW(14), 0)
    CALL(evt_snd_sfxon, STRING("SOUND_WATT_MEGA_DISCHARGE|SFX_BTL_DAMAGE_BIRIBIRI1"), 0)
    CALL_CHILD(sHitAllEvt)
    CALL(btlevtcmd_StopAC)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    WAIT_FRAMES(PM64_FRAMES(30))
    WATT_HOME
    PM64_END
EVT_END()
