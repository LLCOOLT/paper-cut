/* Parakarry's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/parakarry.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include.
 *
 * Rows (BattleParakarry's animations): Still 0, Idle 1, Walk 2, Run 3, PreDive 4, Dive 5, PostDive 6,
 * EnterShell 7, ShellFly 8, ShellFlyFastest 10, FlyFast 20, LiftLight 21, Think 23.
 * Frame counts, speeds and gravities in the comments are PM64's (30 fps); the scripts pass them through
 * PM64_FRAMES / PM64_SPEED / PM64_ACCEL (TTYD's 60), the shared blocks (pm64Zoom, sHitAllEvt's LW(11)) raw. */

/* PM64's mash meters run 90 frames (Air Raid) and 87 (Air Lift) at 30 fps; TTYD's mash gauge (PM64_AC_MASH,
 * Bob-ombast's) runs LW(0) frames (150-300 by difficulty), gaining LW(1) a press and draining LW(2) a frame. This
 * sets its time to PM64's (PM64_FRAMES: at 60 fps) and scales the gain and the drain by the same factor, so the
 * gauge fills at the same press rate as TTYD's does at that difficulty, only in PM64's time. Right after
 * PM64_AC_MASH (its LW(0)-LW(2)), before StartAC. frames: PM64's (30 fps) count. */
#define PARAKARRY_MASH_FRAMES(frames)                                                                         \
    MULTIPLY(LW(1), LW(0))                                                                                    \
    DIVIDE(LW(1), PM64_FRAMES(frames))                                                                        \
    MULTIPLY(LW(2), LW(0))                                                                                    \
    DIVIDE(LW(2), PM64_FRAMES(frames))                                                                        \
    CALL(btlevtcmd_AcSetParam, 1, PM64_FRAMES(frames))                                                        \
    CALL(btlevtcmd_AcSetParam, 3, LW(1))                                                                      \
    CALL(btlevtcmd_AcSetParam, 4, LW(2))

/* parakarryGrab(unit): before Air Lift's hit on the unit, its turn as it is (Gale Force's reaction turns it) and
 * no damage code yet (parakarryCarried reads the one the hit leaves) */
static Vec sParakarryHeldRot;
static USER_FUNC(parakarryGrab) {
    BattleUnit* unit = evtUnit(event, event->args[0]);
    if (unit != NULL) {
        sParakarryHeldRot = unit->rotation;
        unit->damageCode = 0;
    }
    return EVT_RETURN_DONE;
}

/* parakarryCarried(unit, out): out := 1 when Air Lift's hit blew the unit away (TTYD's Gale Force landed). Then
 * TTYD's blow-away reaction (subsetevt_blow_dead or the unit's own event 58: it spins and drifts off by itself) is
 * stopped, the unit turned back, and Parakarry carries it off himself (parakarryCarry), as PM64 does. */
static USER_FUNC(parakarryCarried) {
    BattleUnit* unit = evtUnit(event, event->args[0]);
    int held = 0;
    if (unit != NULL && (unit->damageCode & DAMAGE_HIT_MASK) == DAMAGE_HIT_GALE_FORCE) {
        if (unit->damageEventId != 0) {
            evtDeleteID(unit->damageEventId);
            unit->damageEventId = 0;
        }
        unit->rotation = sParakarryHeldRot;
        held = 1;
    }
    evtSetValue(event, event->args[1], held);
    return EVT_RETURN_DONE;
}

/* parakarryCarry(unit, held): the unit flies off forward with held hanging where it is now, 2 a frame, rising
 * half a unit a frame with a flap's bob (1.4 x sin(2 pi sin t), t 6 degrees a frame), until it is past 240.
 * Blocks. PM64: parakarry.c N(CarryAway). */
static USER_FUNC(parakarryCarry) {
    static struct {
        float ox, oy, oz, t;
    } c;
    BattleUnit* self = evtUnit(event, event->args[0]);
    BattleUnit* held = evtUnit(event, event->args[1]);
    float dir, x, y;
    if (self == NULL || held == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        c.ox = held->position.x - self->position.x;
        c.oy = held->position.y - self->position.y;
        c.oz = held->position.z - self->position.z;
        c.t = 0.0f;
    }
    /* (PM64's 30 fps steps halved at TTYD's 60: the same path in the same time) */
    dir = self->faceDirection < 0 ? -1.0f : 1.0f;
    x = self->position.x + PM64_PER_FRAME(2.0f) * dir;
    y = self->position.y +
        PM64_PER_FRAME(1.4f * sinf(2.0f * 3.14159265f * sinf(c.t * 3.14159265f / 180.0f)) + 0.5f);
    c.t += PM64_PER_FRAME(6.0f);
    if (c.t >= 360.0f) {
        c.t -= 360.0f;
    }
    BtlUnit_SetPos(self, x, y, self->position.z);
    BtlUnit_SetPos(held, x + c.ox, y + c.oy, self->position.z + c.oz);
    return x * dir > 240.0f ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* The two hops back off the enemy after Sky Dive and Shell Shot. PM64: parakarry.c N(EVS_ReturnHome_Success) (LW(9)
 * 1) and N(EVS_ReturnHome_Miss) (0), as row LW(10) (Success: Dive / ShellFly; Miss: Think): he jumps back 50
 * (Miss: 30) in 15 frames at gravity 1.4 (Miss: 1.0) at the height he is, holds Still, and hops 30 (Miss: 20) more
 * in 5 frames, turning to Run 4 frames into it; Idle 2 frames. The caller flies him home. */
static EVT_BEGIN(sParakarryHopsEvt)
    CALL(pm64SetAnim, BTL_UNIT_SELF, LW(10), -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    IF_INT_EQUAL(LW(9), 1)
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.4f))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 50)
    ELSE
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 30)
    END_IF
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(15), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 0, -1)
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(4))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    IF_INT_EQUAL(LW(9), 1)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 30)
    ELSE
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
    END_IF
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(5), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(2))
    RETURN
EVT_END()

/* Parakarry's Sky Dive. PM64: battle/partner/parakarry.c N(skyDive), N(EVS_ReturnHome_Success/_Miss). He flies
 * (Run, speed 5, a 10 dip, cosine in-out) to 20 short of the enemy and holds Still 3 frames; the jump command
 * starts (32 frames; TTYD's: Headbonk's timed press, its window as he lands); he flicks up 10 (FlyFast, 5 frames),
 * 2 frames on, and winds up to 46 short and 35 above (20 frames, a 20 dip, quartic out), turning to PreDive 3
 * frames in; he dives in to 10 short (Dive, 5 frames). Hit: PostDive, the press, 1 / 2 / 4 or 2 / 3 / 5 pressed in
 * time; then the hops back (sParakarryHopsEvt: Dive when pressed in time, else Think) and home (Run, speed 8, a 5
 * dip), floating (Walk). Dodged: he overshoots to 40 past and 10 above it (10 frames, a 20 dip, quadratic out),
 * Idle 5 frames in, waits 20 and flies home. Not here: PM64's motion blur on the dive. */
static EVT_BEGIN(sSkyDiveEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_AC_TIMED
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(5.0f))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -10, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 0, -1)
    WAIT_FRAMES(PM64_FRAMES(3))
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_PARAKARRY_PREDIVE|SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 20, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 10)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(5), 0, 0, 0, -1)
    WAIT_FRAMES(PM64_FRAMES(2))
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(3))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 46)
    ADD(LW(1), 35)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -20, 6, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 10)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(5), 0, 0, 0, -1)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        BEGIN_SIBLING_EVENT
            WAIT_FRAMES(PM64_FRAMES(5))
            CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
        END_SIBLING_EVENT
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
        ADD(LW(1), 10)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -20, 4, 0, -1)
        WAIT_FRAMES(PM64_FRAMES(20))
        GOTO(95)
    END_IF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    PM64_AC_TIMED_WINDOW
    SET_VALUE(LW(15), 0)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    IF_INT_GREATER(LW(15), 0)
        SET_VALUE(LW(9), 1)
        SET_VALUE(LW(10), 5)
    ELSE
        SET_VALUE(LW(9), 0)
        SET_VALUE(LW(10), 23)
    END_IF
    PM64_CAM_STAGE
    CALL_CHILD(sParakarryHopsEvt)
    LABEL(95)
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(8.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -5, 0, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)
    PM64_END
EVT_END()

/* Parakarry's Shell Shot. PM64: battle/partner/parakarry.c N(shellShot), N(ShellShotActionCommand),
 * N(GetShellShotDamage), N(EVS_ReturnHome_Success/_Miss). From where he is he tucks into his shell (EnterShell, 15
 * frames) and aims in it (ShellFly) while you hold the stick left (TTYD's: Shell Toss's hold-left for PM64's sweeping
 * aim line, let go on the target); he shoots (ShellFlyFastest) in 7 frames: aimed, into the enemy, 5 / 6 / 7 by
 * rank, then the hops back (ShellFly) and home; off, 50 past it and 50 above or below (PM64: by which side the line
 * was on; ours: above when let go early, below when held too long: the command's output 2, synced; never under
 * the floor), no damage, the hops back puzzled (Think) and home;
 * dodged, 40 past it, Think. Home: Run, speed 8, a 5 dip; Walk. Not here: PM64's aim line and target marker
 * (TTYD's gauge instead), its motion blur. */
static EVT_BEGIN(sShellShotEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_CAM_SELF
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_AIM_SHELL_SHOT|SFX_BTL_NOKO_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 8, -1)
    PM64_AC_HOLD_LEFT
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    WAIT_FRAMES(1) /* (a frame after SetupAC: the command reads its parameters then; started the same frame it never does) */
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(6))
    CALL(btlevtcmd_AcGetOutputParam, 2, LW(8))
    SET_VALUE(LW(7), 0)
    IF_BITS_SET(LW(6), 2)
        SET_VALUE(LW(7), 1)
    END_IF
    CALL(pm64Trace, STRING("Shell Shot: pre-check, command result, aim"), LW(5), LW(6), LW(8))
    PM64_CAM_TARGET
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_PARAKARRY_SHELL_SHOT|SFX_BTL_NOKO_ATTACK2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    SET_VALUE(LW(9), 0)
    SET_VALUE(LW(10), 23)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(7), 0, 0, 0, -1)
        GOTO(90)
    END_IF
    IF_INT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 50)
        IF_INT_NOT_EQUAL(LW(8), 2)
            ADD(LW(1), 50)
        ELSE
            SUBTRACT(LW(1), 50)
            IF_INT_LESS(LW(1), 0)
                SET_VALUE(LW(1), 0)
            END_IF
        END_IF
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(7), 0, 0, 0, -1)
        GOTO(90)
    END_IF
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(7), 0, 0, 0, -1)
    SET_VALUE(LW(15), 0)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    IF_INT_NOT_EQUAL(LW(5), -1)
        SET_VALUE(LW(9), 1)
        SET_VALUE(LW(10), 8)
    END_IF
    LABEL(90)
    PM64_CAM_STAGE
    CALL_CHILD(sParakarryHopsEvt)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(8.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -5, 0, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)
    PM64_END
EVT_END()

/* Parakarry's Air Raid. PM64: battle/partner/parakarry.c N(airRaid), N(FlyAround), N(GetAirRaidDamage). He flies
 * 70 forward in 15 frames (Run, a 2 dip), Idle; the command starts (PM64's 90-frame mash; TTYD's: Bob-ombast's mash
 * gauge, set to 90 frames: PARAKARRY_MASH_FRAMES); 2 frames on he winds up 50 back, 30 up and 40 toward the
 * camera in 10 frames (PreDive, a 10 dip, cosine in-out), 2 frames, and zooms about the whole stage in his shell
 * (ShellFlyFastest; pm64Zoom 1: 48 a frame, bouncing off a box x -190..190, y -30..160, for 90 frames, then out the
 * next side) while you mash. Every enemy then takes 2 + fill / 25 (2 to 6; PM64's 2 + fill * 100 / 2499), no
 * contact, 5 frames apart. 10 frames on he comes back in from 200 behind, 50 up (Run, speed 12), floating (Walk).
 * PM64's lens flares and sparkles on him every 18 frames, five times (pm64Fx 19 and 1). Not here: its motion
 * blur and the trail behind him (fx_effect_65). */
static EVT_BEGIN(sAirRaidEvt)
    PM64_BEGIN(PM64_ALL)
    PM64_AC_MASH
    PARAKARRY_MASH_FRAMES(90)
    PM64_CAM_SELF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 70)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(15), -2, 0, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_StartAC, 1)
    WAIT_FRAMES(PM64_FRAMES(2))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_PARAKARRY_PREDIVE|SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 50)
    ADD(LW(1), 30)
    ADD(LW(2), 40)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -10, 11, 0, -1)
    WAIT_FRAMES(PM64_FRAMES(2))
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_PARAKARRY_AIR_RAID_1|SFX_BTL_NOKO_ATTACK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    /* (parakarry.c FlyAround: EFFECT_LENS_FLARE and EFFECT_SPARKLES on him every 18 frames, five times) */
    BEGIN_SIBLING_EVENT
        LOOP(5)
            WAIT_FRAMES(PM64_FRAMES(18))
            CALL(pm64Fx, 19, BTL_UNIT_SELF, 20)
            CALL(pm64Fx, 1, BTL_UNIT_SELF, 20)
        END_LOOP
    END_SIBLING_EVENT
    CALL(pm64Zoom, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_PARAKARRY_AIR_RAID_2|SFX_BTL_NOKO_ATTACK2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_StopAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    CALL(pm64GaugeDamage, LW(12), LW(8), 2, 25)
    SET_VALUE(LW(11), 5)
    SET_VALUE(LW(13), -1)
    SET_VALUE(LW(14), 0)
    CALL_CHILD(sHitAllEvt)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    WAIT_FRAMES(PM64_FRAMES(10))
    SET_VALUE(LW(0), 0)
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 200)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, EVT_NULLPTR, LW(2))
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), 50, LW(2))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(12.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, 0, 0, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)
    PM64_END
EVT_END()

/* Parakarry's Air Lift. PM64: battle/partner/parakarry.c N(airLift), N(AirLiftChance), N(CarryAway). He flies
 * (Run, speed 5, a 4 dip, cosine in-out) onto the enemy, Idle 15 frames, takes hold of it (LiftLight) and 2 frames
 * on tests it. Missed: he rises 20 (10 frames, a 10 dip, cosine in), Idle 10 frames, turns round and flies home
 * slowly (Run, speed 4), Walk. Else 3 frames on the enemy struggles (its run pose; PM64: its Run at twice the speed,
 * sweating, when it can move) while you mash (PM64's 87-frame meter; TTYD's: Bob-ombast's gauge, set to 87 frames)
 * for 90 frames. Carried off at the gauge's chance (TTYD's Gale Force rule: the fill as the blow-away chance, by
 * the enemy's own resistance; PM64: the enemy's air-lift chance, more as it is hurt): the enemy hangs hurt (its
 * damage pose) and he flies off forward with it (LiftLight) 2 a frame, rising with a flap's bob, until past 240
 * (parakarryCarry); 30 frames on it is gone (its coins and Star Points won, as Gale Force's). Not carried: he lets
 * go, 20 up in 5 frames (a 10 dip, cosine in), Idle 10 frames. No damage. Then home from where he is (Run, speed
 * 12), Walk. A flap's sound as he takes hold. Not here: PM64's LiftLight at twice the speed, the camera closing in as
 * the meter fills, a flap's sound on every beat. */
static EVT_BEGIN(sAirLiftEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_AC_MASH
    PARAKARRY_MASH_FRAMES(87)
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(5.0f))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -4, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 21, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_PARAKARRY_FLAP|SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    WAIT_FRAMES(PM64_FRAMES(2))
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        PM64_CAM_STAGE
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 20)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -10, 13, 0, -1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
        WAIT_FRAMES(PM64_FRAMES(10))
        PM64_FACE_TARGET
        MULTIPLY(LW(15), -1)
        CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(4.0f))
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, 0, 0, 0, -1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)
        GOTO(99)
    END_IF
    WAIT_FRAMES(PM64_FRAMES(3))
    CALL(btlevtcmd_GetBodyId, LW(3), LW(7))
    CALL(btlevtcmd_CheckActStatus, LW(3), LW(8))
    IF_INT_EQUAL(LW(8), 1)
        CALL(btlevtcmd_AnimeChangePoseType, LW(3), LW(7), BTL_POSE_RUN)
    END_IF
    CALL(btlevtcmd_StartAC, 1)
    WAIT_FRAMES(PM64_FRAMES(90))
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_StopAC)
    IF_INT_EQUAL(LW(8), 1)
        CALL(btlevtcmd_AnimeChangePoseType, LW(3), LW(7), BTL_POSE_STAY)
    END_IF
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    CALL(pm64StatusWeapon, LW(12), 2, LW(8))
    CALL(parakarryGrab, LW(3))
    SET_VALUE(LW(15), 0)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    CALL(parakarryCarried, LW(3), LW(9))
    PM64_CAM_STAGE
    IF_INT_EQUAL(LW(9), 1)
        CALL(btlevtcmd_GetBodyId, LW(3), LW(7))
        CALL(btlevtcmd_AnimeChangePoseType, LW(3), LW(7), BTL_POSE_DAMAGE)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 21, -1)
        CALL(parakarryCarry, BTL_UNIT_SELF, LW(3))
        WAIT_FRAMES(PM64_FRAMES(30))
        CALL(btlevtcmd_GetCoin, LW(3), LW(0))
        CALL(btlevtcmd_StoreCoin, LW(0))
        CALL(btlevtcmd_GetExp, LW(3), LW(0))
        CALL(btlevtcmd_StoreExp, LW(0))
        CALL(btlevtcmd_KillUnit, LW(3), 0)
    ELSE
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 20)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(5), -10, 13, 0, -1)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
        WAIT_FRAMES(PM64_FRAMES(10))
    END_IF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(12.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, 0, 0, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)
    PM64_END
EVT_END()
