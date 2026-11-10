/* Goombario's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/goombario.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include.
 *
 * His battle rows (npc_sprite_names.yaml BattleGoombario animations): Still 0, Idle 1, Walk 2, Run 3, Jump 4,
 * Fall 5, Headbonk 6, PreHeadbonk 7, HurtStill 10, Block 14, Talk 15, CelebrateLoop 16, Celebrate 17, Think 18.
 *
 * Rotation: PM64 turns his sprite about z by minus the way he travels (0 up, 90 towards the enemies, 180 down:
 * his head leads); here that angle times his facing (TTYD's rotation is the world's, not mirrored).
 *
 * Frames: the numbers in the comments are PM64's (30 a second); the scripts run them at TTYD's 60 through
 * PM64_FRAMES / PM64_SPEED / PM64_ACCEL / PM64_PER_FRAME (pm64_partner_pc.c). Online: what the command decides
 * (the bounce's rating, Multibonk going on) is read from GetResultAC / GetResultCountAC after ResultAC only. */

#include "battle/battle_mario.h"
#include "battle/battle_monosiri.h"
#include "evt/evt_msg.h"

extern EvtWord christine_dictionary_event[]; /* (unit_party_christine.c: Goombella's tattle card) */

/* Tattle's help weapon (its row's help): no action command, so no help text (acHelpMsg NULL) */
static BattleWeapon sGoombarioNoHelp;

/* His leap onto an enemy (PM64's N(JumpOnTarget)), and the fall when it isn't there (N(OnMissHeadbonk)). */
static struct {
    float x, y, z;        /* the straight line under the arc, this frame */
    float gx, gy, gz;     /* where he lands */
    float sx, sy, sz;     /* along the line per frame */
    float height;         /* the arc's height */
    float phase, step;    /* how far through the arc (0..pi), the mean step */
    float wave, waveStep; /* the step's swing: faster at the ends, slower at the top */
    float swing;          /* 0.53, or 0.8 for the bounce */
    float lastDy;         /* the last frame's rise (the miss keeps falling from it) */
    int left, frame, mode;
} sGoombarioLeap;

/* goombarioLeap(target, part, frames, mode): he leaps from where he is onto the target part's hit point in frames
 * (0: PM64's N(calcJumpTime): (the x distance - 20) / 10.588 + 15), PM64's arc: the line to it plus height *
 * sin(phase), height (distance - 20) / 6 + 47 (x 1.12 mode 2, x 1.25 mode 4); the phase steps pi / frames times
 * 1 + swing * sin(wave), wave from 90 by 360 / frames (swing 0.53, 0.8 from mode 3 on). Leaning head first along
 * the way; mode 3 (the bounce) flips forward 30 a frame from frame 4 to 9 (PM64's thread over it). Blocks. */
static USER_FUNC(goombarioLeap) {
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
    float face, ox, oy, ny;
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        const int id = BattleTransID(event, evtGetValue(event, event->args[0]));
        BattleUnit* target = BattleGetUnitPtr(_battleWorkPointer, id);
        BattleUnitPart* part = BattleGetUnitPartsPtr(id, evtGetValue(event, event->args[1]));
        int frames = evtGetValue(event, event->args[2]);
        float dx, dz, dist;
        sGoombarioLeap.mode = evtGetValue(event, event->args[3]);
        if (target == NULL || part == NULL) {
            return EVT_RETURN_DONE;
        }
        BtlUnit_GetHitPos(target, part, &sGoombarioLeap.gx, &sGoombarioLeap.gy, &sGoombarioLeap.gz);
        sGoombarioLeap.gz += 5.0f; /* (in front of it, as Goombella's) */
        sGoombarioLeap.x = unit->position.x;
        sGoombarioLeap.y = unit->position.y;
        sGoombarioLeap.z = unit->position.z;
        dx = sGoombarioLeap.gx - sGoombarioLeap.x;
        dz = sGoombarioLeap.gz - sGoombarioLeap.z;
        dist = sqrtf(dx * dx + dz * dz);
        if (frames <= 0) {
            frames = (int)((fabsf(dx) - 20.0f) / 10.588f + 15.0f);
        }
        if (frames <= 0) {
            BtlUnit_SetPos(unit, sGoombarioLeap.gx, sGoombarioLeap.gy, sGoombarioLeap.gz);
            return EVT_RETURN_DONE;
        }
        /* (PM64's frames at 30 a second, ours at 60: twice the frames, half the steps; PM64 frame k is our 2k) */
        sGoombarioLeap.waveStep = PM64_PER_FRAME((float)(360 / frames));
        frames = PM64_FRAMES(frames);
        sGoombarioLeap.left = frames;
        sGoombarioLeap.frame = 0;
        sGoombarioLeap.sx = dx / (float)frames;
        sGoombarioLeap.sy = (sGoombarioLeap.gy - sGoombarioLeap.y) / (float)frames;
        sGoombarioLeap.sz = dz / (float)frames;
        sGoombarioLeap.step = 3.14159265f / (float)frames;
        sGoombarioLeap.wave = 90.0f;
        sGoombarioLeap.swing = sGoombarioLeap.mode < 3 ? 0.53f : 0.8f;
        sGoombarioLeap.height = (dist - 20.0f) / 6.0f + 47.0f;
        if (sGoombarioLeap.mode == 2) {
            sGoombarioLeap.height *= 1.12f;
        } else if (sGoombarioLeap.mode == 4) {
            sGoombarioLeap.height *= 1.25f;
        }
        /* (the first step is taken before the first frame, at the swing's top) */
        sGoombarioLeap.phase = sGoombarioLeap.step * (1.0f + sGoombarioLeap.swing);
        sGoombarioLeap.lastDy = 0.0f;
    }
    face = unit->faceDirection < 0 ? -1.0f : 1.0f;
    ox = unit->position.x;
    oy = unit->position.y;
    sGoombarioLeap.x += sGoombarioLeap.sx;
    sGoombarioLeap.y += sGoombarioLeap.sy;
    sGoombarioLeap.z += sGoombarioLeap.sz;
    ny = sGoombarioLeap.y + sGoombarioLeap.height * sinf(sGoombarioLeap.phase);
    if (sGoombarioLeap.gy > ny && sGoombarioLeap.left < PM64_FRAMES(3)) {
        ny = sGoombarioLeap.gy;
    }
    {
        float lean = -57.29578f * atan2f((sGoombarioLeap.x - ox) * face, ny - oy);
        /* (PM64's flip, 30 a frame on its frames 4-9: 15 a frame on our 8-19) */
        if (sGoombarioLeap.mode >= 3 && sGoombarioLeap.frame >= PM64_FRAMES(4) &&
            sGoombarioLeap.frame <= PM64_FRAMES(9) + 1) {
            lean = -15.0f * (float)(sGoombarioLeap.frame - (PM64_FRAMES(4) - 1));
        }
        unit->rotation.z = lean * face;
    }
    BtlUnit_SetPos(unit, sGoombarioLeap.x, ny, sGoombarioLeap.z);
    sGoombarioLeap.lastDy = ny - oy;
    sGoombarioLeap.phase +=
        sGoombarioLeap.step * (1.0f + sGoombarioLeap.swing * sinf(0.017453292f * sGoombarioLeap.wave));
    sGoombarioLeap.wave += sGoombarioLeap.waveStep;
    if (sGoombarioLeap.wave >= 360.0f) {
        sGoombarioLeap.wave -= 360.0f;
    }
    sGoombarioLeap.frame++;
    if (--sGoombarioLeap.left <= 0) {
        BtlUnit_SetPos(unit, sGoombarioLeap.x, sGoombarioLeap.gy, sGoombarioLeap.z);
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* goombarioFall(): after a leap onto nothing he keeps going: on along the leap's line, falling from its last
 * frame's speed, 1.8 faster a frame, until under 10 up (PM64's N(OnMissHeadbonk)). Blocks. */
static USER_FUNC(goombarioFall) {
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        sGoombarioLeap.x = unit->position.x;
        sGoombarioLeap.y = unit->position.y;
        sGoombarioLeap.z = unit->position.z;
    }
    sGoombarioLeap.y += sGoombarioLeap.lastDy;
    sGoombarioLeap.lastDy -= PM64_PER_FRAME(PM64_PER_FRAME(1.8f)); /* (an acceleration: a quarter at 60) */
    sGoombarioLeap.x += sGoombarioLeap.sx;
    sGoombarioLeap.z += sGoombarioLeap.sz;
    if (sGoombarioLeap.y < 10.0f) {
        BtlUnit_SetPos(unit, sGoombarioLeap.x, 10.0f, sGoombarioLeap.z);
        return EVT_RETURN_DONE;
    }
    BtlUnit_SetPos(unit, sGoombarioLeap.x, sGoombarioLeap.y, sGoombarioLeap.z);
    return EVT_RETURN_BLOCK;
}

/* goombarioReturnFrames(x, y, z, out): PM64's frames (at 30 a second) of his leap back to (x, y, z): 15 per 100 of
 * the way, 10 to 20 (PM64's N(GetReturnMoveTime), the action command learned) */
static USER_FUNC(goombarioReturnFrames) {
    const BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
    const float x = (float)evtGetValue(event, event->args[0]);
    const float y = (float)evtGetValue(event, event->args[1]);
    const float z = (float)evtGetValue(event, event->args[2]);
    int frames = 10;
    if (unit != NULL) {
        const float dx = x - unit->position.x, dy = y - unit->position.y, dz = z - unit->position.z;
        frames = (int)(sqrtf(dx * dx + dy * dy + dz * dz) * 15.0f / 100.0f);
    }
    frames = frames > 20 ? 20 : frames < 10 ? 10 : frames;
    evtSetValue(event, event->args[3], frames);
    return EVT_RETURN_DONE;
}

/* goombarioChargeWeapon(weapon): weapon := a copy of it charging +2, PM64's every time (N(ChargeAtPos): Charge
 * adds 2 to his next attack, at every rank, and adds up) */
static USER_FUNC(goombarioChargeWeapon) {
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    sCopy.chargeStrength = 2;
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* goombarioTattle(unit, out): out := the unit's tattle line (TTYD's, as Goombella's Tattle reads it); the unit's
 * kind is marked tattled (its HP shows from now on, its log entry opens) */
static USER_FUNC(goombarioTattle) {
    const BattleUnit* unit =
        BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, event->args[0])));
    const TattleEntry* entry;
    if (unit == NULL || (entry = battleGetUnitMonosiriPtr(unit->currentKind)) == NULL) {
        evtSetValue(event, event->args[1], 0);
        return EVT_RETURN_DONE;
    }
    evtSetValue(event, event->args[1], (EvtWord)entry->tattleBtlTag);
    battleSetUnitMonosiriFlag(unit->currentKind);
    return EVT_RETURN_DONE;
}

/* the crouch before a leap and after landing: PreHeadbonk, the sprite 18 then 19 up (PM64's SetActorDispOffset) */
#define GOOMBARIO_CROUCH(first)                                                                               \
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)                                                                   \
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 18, 0)                                                    \
    WAIT_FRAMES(PM64_FRAMES(first))                                                                           \
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 19, 0)                                                    \
    WAIT_FRAMES(PM64_FRAMES(1))
/* the landing squash (1.1 x 0.8, then 1.3 x 0.5, a PM64 frame each) */
#define GOOMBARIO_SQUASH                                                                                      \
    BEGIN_SIBLING_EVENT                                                                                       \
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.1f), FLOAT(0.8f), FLOAT(1.0f))                        \
        WAIT_FRAMES(PM64_FRAMES(1))                                                                           \
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.3f), FLOAT(0.5f), FLOAT(1.0f))                        \
        WAIT_FRAMES(PM64_FRAMES(1))                                                                           \
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))                        \
    END_SIBLING_EVENT                                                                                         \
    WAIT_FRAMES(PM64_FRAMES(1))
/* the camera closes in a moment on each bounce (PM64's BTL_CAM_GOOMBARIO_BONK_FOLLOWUP_1, then _2 5 frames on) */
#define GOOMBARIO_BONK_CAM                                                                                    \
    BEGIN_INLINE_EVENT                                                                                        \
        CALL(evt_btl_camera_set_moveSpeedLv, 0, 0)                                                            \
        CALL(evt_btl_camera_set_zoom, 0, 250)                                                                 \
        WAIT_FRAMES(PM64_FRAMES(5))                                                                           \
        CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)                                                            \
        CALL(evt_btl_camera_set_zoom, 0, 300)                                                                 \
    END_INLINE_EVENT
/* the Ultra-rank bounce spins him about y, 133 a PM64 frame through its 24 (Headbonk 3, Multibonk): 66 and 67 on
 * our frames */
#define GOOMBARIO_SPIN_Y                                                                                      \
    BEGIN_SIBLING_EVENT                                                                                       \
        LOOP(24)                                                                                              \
            CALL(btlevtcmd_AddRotate, BTL_UNIT_SELF, 0, 66, 0)                                                \
            WAIT_FRAMES(1)                                                                                    \
            CALL(btlevtcmd_AddRotate, BTL_UNIT_SELF, 0, 67, 0)                                                \
            WAIT_FRAMES(1)                                                                                    \
        END_LOOP                                                                                              \
    END_SIBLING_EVENT

/* Back from a Headbonk that didn't go well. PM64: battle/partner/goombario.c N(EVS_ReturnHome_Miss). Idle; he
 * leaps back 30 to the ground (gravity 1.2, N(GetReturnMoveTime) frames), from frame 4 tumbling about 12 up
 * (180 + 60 a frame, 3 frames, then upright); lands crouched (PreHeadbonk 18 / 19 up, a frame each), hops 10
 * back (6 frames), crouches again, Idle 2 frames. (The run home follows: the caller's.) */
static EVT_BEGIN(sGoombarioMissHomeEvt)
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_GetRotate, BTL_UNIT_SELF, EVT_NULLPTR, EVT_NULLPTR, LW(7))
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(7))
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(4))
        CALL(btlevtcmd_GetFaceDirection, BTL_UNIT_SELF, LW(14))
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 12, 0)
        SET_VALUE(LW(13), 180)
        LOOP(PM64_FRAMES(3))
            ADD(LW(13), 30)
            SET_VALUE(LW(11), LW(13))
            MULTIPLY(LW(11), LW(14))
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(11))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 0, 0)
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 30)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.2f))
    CALL(goombarioReturnFrames, LW(0), LW(1), LW(2), LW(6))
    MULTIPLY(LW(6), 2)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(6), -1)
    GOOMBARIO_CROUCH(1)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 10)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(6), -1)
    GOOMBARIO_CROUCH(1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    WAIT_FRAMES(PM64_FRAMES(2))
    RETURN
EVT_END()

/* Back from a Headbonk that went well. PM64: battle/partner/goombario.c N(EVS_ReturnHome_Success). Idle; he leaps
 * back to 60 in front of home (gravity 1.4, N(GetReturnMoveTime) frames) somersaulting about 12 up from frame 4
 * (180 + 60 a frame, an odd number of 3-frame thirds: (frames - 4) / 3, at least 1), lands crouched a frame, hops
 * 20 back (6 frames), crouches a frame, hops 10 back (4 frames), crouches a frame, Idle 2 frames. (The run home
 * follows: the caller's.) */
static EVT_BEGIN(sGoombarioNiceHomeEvt)
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_GetRotate, BTL_UNIT_SELF, EVT_NULLPTR, EVT_NULLPTR, LW(7))
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(7))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 60)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.4f))
    CALL(goombarioReturnFrames, LW(0), LW(1), LW(2), LW(6))
    SET_VALUE(LW(7), LW(6))
    MULTIPLY(LW(7), 2)
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(4))
        CALL(btlevtcmd_GetFaceDirection, BTL_UNIT_SELF, LW(14))
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 12, 0)
        SET_VALUE(LW(8), LW(6))
        SUBTRACT(LW(8), 4)
        DIVIDE(LW(8), 3)
        IF_INT_LESS(LW(8), 1)
            SET_VALUE(LW(8), 1)
        END_IF
        SET_VALUE(LW(11), LW(8))
        DIVIDE(LW(11), 2)
        MULTIPLY(LW(11), 2)
        IF_INT_EQUAL(LW(11), LW(8))
            SUBTRACT(LW(8), 1)
        END_IF
        SET_VALUE(LW(13), 180)
        LOOP(LW(8))
            LOOP(PM64_FRAMES(3))
                ADD(LW(13), 30)
                SET_VALUE(LW(11), LW(13))
                MULTIPLY(LW(11), LW(14))
                CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(11))
                WAIT_FRAMES(1)
            END_LOOP
        END_LOOP
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 0, 0)
    END_SIBLING_EVENT
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), LW(7), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 18, 0)
    WAIT_FRAMES(PM64_FRAMES(1))
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(6), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 18, 0)
    WAIT_FRAMES(PM64_FRAMES(1))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 10)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(4), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 18, 0)
    WAIT_FRAMES(PM64_FRAMES(1))
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(2))
    RETURN
EVT_END()

/* The run-up and the first leap (Headbonk and Multibonk). PM64: battle/partner/goombario.c N(runToTarget),
 * N(calcJumpTime), the first half of N(EVS_Attack_Headbonk1/2/3) and N(EVS_Move_Multibonk). He runs (Run, speed 5)
 * to 70 short of the enemy, Idle; crouches (PreHeadbonk, 18 up 5 frames, 19 up 1); the timed press starts (TTYD's
 * jump press for PM64's jump command); Headbonk, 9 up, he leaps onto it (goombarioLeap, N(calcJumpTime) frames).
 * Not there (dodged, out of reach): he falls on through (goombarioFall), dust, the camera shakes 5 frames, 20
 * frames, back (sGoombarioMissHomeEvt), and LW(5) is then -1. Out: LW(5) 1 when he's on its head. */
static EVT_BEGIN(sGoombarioLeapInEvt)
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(5.0f))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 70)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(6), LW(1), LW(7))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    GOOMBARIO_CROUCH(5)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 9, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(goombarioLeap, LW(3), LW(4), 0, 0)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(goombarioFall)
        CALL(pm64Fx, 3, BTL_UNIT_SELF, 0)
        CALL(evt_btl_camera_shake_h, 0, 1, 0, PM64_FRAMES(5), 13)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        WAIT_FRAMES(PM64_FRAMES(20))
        CALL_CHILD(sGoombarioMissHomeEvt)
        SET_VALUE(LW(5), -1)
    END_IF
    RETURN
EVT_END()

/* The first bonk's hit, as he lands (both moves): the squash, the timed press's window, the damage (in time: the
 * rating, not the last hit). Out: LW(6) 1 when the press was in time; otherwise that was the last hit and he is
 * back (sGoombarioMissHomeEvt), LW(6) 0. */
static EVT_BEGIN(sGoombarioFirstBonkEvt)
    GOOMBARIO_SQUASH
    PM64_AC_TIMED_WINDOW
    CALL(btlevtcmd_GetResultAC, LW(6))
    IF_BITS_CLEAR(LW(6), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(btlevtcmd_StopAC)
        CALL_CHILD(sGoombarioMissHomeEvt)
        SET_VALUE(LW(6), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_AC_SUCCESS, LW(5))
    CALL(btlevtcmd_GetResultPrizeLv, LW(3), 0, LW(6))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    SET_VALUE(LW(6), 1)
    RETURN
EVT_END()

/* The follow-up bonk turned away (the target evasive, invisible or intangible by now, or a pre-emptive front
 * spike): the first bonk was not the last hit, so a target it took to 0 HP is knocked out now, not left standing
 * at 0. Only the KO (pm64KoAt0): no second hit with the weapon, so no damage again and no counter on him. */
#define GOOMBARIO_LAST_HIT_AT_0 CALL(pm64KoAt0, LW(3), LW(4))

/* The end of both: the run home (Run, speed 8), Idle, everything about him back to plain. */
#define GOOMBARIO_HOME                                                                                        \
    CALL(evt_audience_ap_recovery)                                                                            \
    CALL(btlevtcmd_InviteApInfoReport)                                                                        \
    PM64_HOME(3, 8.0f)                                                                                        \
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)                                                                   \
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)                                                         \
    CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 0, 0)                                                   \
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)                                                     \
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))

/* Goombario's Headbonk. PM64: battle/partner/goombario.c N(EVS_Attack_Headbonk1) / 2 / 3 (by rank), with
 * N(runToTarget), N(calcJumpTime), N(JumpOnTarget), N(OnMissHeadbonk), N(EVS_ReturnHome_Miss/_Success). The run-up
 * and leap (sGoombarioLeapInEvt); on its head: squash, 1 / 2 / 3 by rank (+ Charge). Not pressed in time: he
 * leaps back (ReturnHome_Miss). In time: the camera closes in, a second timed press (24 frames), and he bounces
 * straight up off its head and down again (24 frames, flipping forward from frame 4 to 9, at Ultra rank also
 * spinning about y), squash, 1 / 2 / 3 again; back by somersaults (ReturnHome_Success); runs home (speed 8).
 * Ours: TTYD's timed press (Goombella's); PM64's second hit lands whatever the second press, TTYD's press only
 * rates it; PM64's motion blur on the bounce left out. */
static EVT_BEGIN(sHeadbonkEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_AC_TIMED
    CALL_CHILD(sGoombarioLeapInEvt)
    IF_INT_NOT_EQUAL(LW(5), 1)
        GOTO(98)
    END_IF
    CALL_CHILD(sGoombarioFirstBonkEvt)
    IF_INT_NOT_EQUAL(LW(6), 1)
        GOTO(98)
    END_IF
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_ALWAYS_HIT, LW(5))
    IF_INT_NOT_EQUAL(LW(5), 1)
        CALL(btlevtcmd_StopAC)
        GOOMBARIO_LAST_HIT_AT_0
        GOTO(97)
    END_IF
    GOOMBARIO_BONK_CAM
    CALL(pm64WeaponRank, LW(12), LW(9))
    IF_INT_GREATER_OR_EQUAL(LW(9), 2)
        GOOMBARIO_SPIN_Y
    END_IF
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_KURI_ATTACK2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(goombarioLeap, LW(3), LW(4), 24, 3)
    GOOMBARIO_SQUASH
    PM64_AC_TIMED_WINDOW
    CALL(btlevtcmd_GetResultAC, LW(6))
    IF_BITS_SET(LW(6), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), 1, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    END_IF
    CALL(btlevtcmd_StopAC)
    LABEL(97)
    CALL_CHILD(sGoombarioNiceHomeEvt)
    LABEL(98)
    GOOMBARIO_HOME
    PM64_END
EVT_END()

/* Goombario's Multibonk. PM64: battle/partner/goombario.c N(EVS_Move_Multibonk), N(AdjustMultibonkChance). The
 * run-up and leap as Headbonk's; 3 (+ Charge) on its head. Not in time: back (ReturnHome_Miss). In time: he
 * bounces again and again (24 frames each, flipping forward from frame 4 to 9 and spinning about y, the camera
 * closing in each time), 3 each, the press harder every bounce, until a press is missed (the last hit, then
 * ReturnHome_Miss) or the enemy's power-bounce chance runs out (the last hit, then ReturnHome_Success); runs home.
 * Ours: TTYD's timed press narrowing bounce by bounce (Goombella's Multibonk's); PM64's chance (200, times the
 * enemy's power-bounce chance a bounce) as TTYD's per-enemy Power Bounce limit (mario_get_renzoku_count_max);
 * PM64's motion blur left out. */
static EVT_BEGIN(sMultibonkEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_AC_TIMED
    CALL(btlevtcmd_ac_timing_flag_onoff, 1, 2)
    CALL_CHILD(sGoombarioLeapInEvt)
    IF_INT_NOT_EQUAL(LW(5), 1)
        GOTO(98)
    END_IF
    CALL_CHILD(sGoombarioFirstBonkEvt)
    IF_INT_NOT_EQUAL(LW(6), 1)
        GOTO(98)
    END_IF
    SET_VALUE(LW(10), 1)
    LABEL(10)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_ALWAYS_HIT, LW(5))
    IF_INT_NOT_EQUAL(LW(5), 1)
        CALL(btlevtcmd_StopAC)
        GOOMBARIO_LAST_HIT_AT_0
        GOTO(97)
    END_IF
    CALL(btlevtcmd_StartAC, 0)
    GOOMBARIO_BONK_CAM
    GOOMBARIO_SPIN_Y
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_KURI_ATTACK2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(goombarioLeap, LW(3), LW(4), 24, 3)
    GOOMBARIO_SQUASH
    PM64_AC_TIMED_WINDOW
    CALL(btlevtcmd_GetResultAC, LW(6))
    IF_BITS_CLEAR(LW(6), 2)
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        SET_VALUE(LW(6), LW(10))
        SUBTRACT(LW(6), 1)
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), LW(6), LW(6))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
        CALL(btlevtcmd_StopAC)
        CALL_CHILD(sGoombarioMissHomeEvt)
        GOTO(98)
    END_IF
    CALL(btlevtcmd_GetResultCountAC, LW(6))
    CALL(mario_get_renzoku_count_max, LW(3), LW(6), LW(7))
    IF_INT_LESS(LW(6), LW(7))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_AC_SUCCESS, LW(5))
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), LW(10), LW(6))
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        ADD(LW(10), 1)
        GOTO(10)
    END_IF
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
    CALL(btlevtcmd_GetResultPrizeLv, LW(3), LW(10), LW(6))
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    CALL(btlevtcmd_StopAC)
    LABEL(97)
    CALL_CHILD(sGoombarioNiceHomeEvt)
    LABEL(98)
    GOOMBARIO_HOME
    PM64_END
EVT_END()

/* Goombario's Tattle. PM64: battle/partner/goombario.c N(EVS_Move_Tattle). He runs 30 forward (Run, speed 6),
 * Idle; the tattle window opens on the enemy (25 frames), he says its tattle (Talk while he speaks, then Idle);
 * the window closes (12 frames), 32 frames more, and he runs home (Run, speed 4), Idle. No action command.
 * Ours: TTYD's tattle card (Goombella's, christine_dictionary_event) for PM64's window, TTYD's tattle line (the
 * enemies are TTYD's); always succeeds, as PM64's. */
static EVT_BEGIN(sTattleEvt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    PM64_CAM_SELF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(6.0f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 30)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    /* the card (unit work 0, Goombella's UW_CHRISTINE_TATTLE_STATE: 1 open, 3 shown, 4 close, 0 closed) */
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, 0, 0)
    SPAWN_EVENT(christine_dictionary_event)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, 0, 1)
    PM64_CAM_STAGE
    LABEL(10)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 0, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 3)
        WAIT_FRAMES(1)
        GOTO(10)
    END_IF
    CALL(goombarioTattle, LW(3), LW(0))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 15, -1)
    CALL(evt_msg_print, 2, LW(0), 0, BTL_UNIT_SELF)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, 0, 4)
    LABEL(20)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 0, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        WAIT_FRAMES(1)
        GOTO(20)
    END_IF
    WAIT_FRAMES(PM64_FRAMES(32))
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    PM64_HOME(3, 4.0f)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    PM64_END
EVT_END()

/* Goombario's Charge. PM64: battle/partner/goombario.c N(EVS_Move_Charge), N(ChargeAtPos), N(PlayChargeFX). The
 * camera closes on him; 10 frames; he crouches (PreHeadbonk, 19 up), the power gathering in on him (energy in,
 * twice, 3 frames apart, 15 up; 15 frames on, a wave of it); 30 frames; Idle, the camera back; +2 attack for his
 * next move (adding up), and he hops in place (Jump, 20 frames, gravity 1.4), Idle; the message, 60 frames.
 * Ours: the charge sparkles for PM64's energy effects; TTYD's Charge status (+2 at every rank:
 * goombarioChargeWeapon) and its pop-up for PM64's message box. */
static EVT_BEGIN(sChargeEvt)
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_CommandPayWeaponCost, BTL_UNIT_SELF)
    CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
    CALL(goombarioChargeWeapon, LW(12))
    PM64_CAM_SELF
    WAIT_FRAMES(PM64_FRAMES(10))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 19, 0)
    CALL(evt_snd_sfxon, STRING("SFX_CONDITION_CHARGE1"), 0)
    CALL(pm64Fx, 1, BTL_UNIT_SELF, 15)
    WAIT_FRAMES(PM64_FRAMES(3))
    CALL(pm64Fx, 1, BTL_UNIT_SELF, 15)
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(15))
        CALL(pm64Fx, 1, BTL_UNIT_SELF, 15)
    END_SIBLING_EVENT
    WAIT_FRAMES(PM64_FRAMES(30))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    PM64_CAM_STAGE
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    WAIT_FRAMES(PM64_FRAMES(4))
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.4f))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(60))
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(99)
    CALL(pm64SetAnim, BTL_UNIT_SELF, -1, -1)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()
