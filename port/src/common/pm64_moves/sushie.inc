/* Sushie's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/sushie.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include. Frame counts, speeds and
 * gravities in the comments are PM64's (30 a second); the scripts make them TTYD's (60) with PM64_FRAMES,
 * PM64_SPEED, PM64_ACCEL and PM64_PER_FRAME. TTYD's commands stay TTYD's; the effects' times from a PM64 script are PM64's too (PM64_FRAMES). */

#include "eff/n64/eff_opuku_jetw_n64.h"
#include "eff/n64/eff_opuku_jetwrain_n64.h"

/* Sushie's Belly Flop. PM64: battle/partner/sushie.c N(EVS_Move_BellyFlop), N(runToTarget), N(EVS_ReturnHome_
 * BellyFlop_Success/_Miss). She runs (Run, speed 4) 40 forward and idles; hold the stick left, let go when the light
 * comes on (PM64's hammer command; TTYD's Shell Toss, the same kind), tensing (Tense1). She leaps (Jump, then Fall
 * after 10) onto the enemy in 20 frames, spinning a turn in 10 frames about 12 up and holding at 20 degrees; done
 * right she swells from her 13th frame (to 1.3 / 1.55 / 1.8 by rank) and drops harder. She lands (Land) for 1 / 2 /
 * 3, or 3 / 4 / 5 done right; no splash (PM64 has none). Done right: she bounces back (Fall) 60, 30, 20 and 10 to
 * the ground (20, 8, 6, 4 frames), idles 15 and runs home (speed 6); otherwise 15 and 5 (8, 4 frames). Dodged: she
 * hops on 15 and 10 (20, 10 frames), waits 10, runs home. */
static EVT_BEGIN(sBellyFlopEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_CAM_SELF
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    PM64_AC_HOLD_LEFT
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    WAIT_FRAMES(1) /* (a frame after SetupAC: the command reads its parameters then; started the same frame it never does) */
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    PM64_CAM_TARGET
    CALL(btlevtcmd_GetResultAC, LW(7))
    CALL(pm64WeaponRank, LW(12), LW(9))
    /* the spin (and, done right, the swell) alongside the leap */
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 12, 0)
        SET_VALUE(LW(0), 0)
        LOOP(PM64_FRAMES(10))
            ADD(LW(0), 18)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(0))
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 20)
    END_SIBLING_EVENT
    IF_BITS_SET(LW(7), 2)
        BEGIN_SIBLING_EVENT
            WAIT_FRAMES(PM64_FRAMES(13))
            SWITCH(LW(9))
                CASE_EQUAL(0)
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.1f), FLOAT(1.1f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(2))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(2))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.3f), FLOAT(1.3f), FLOAT(1.0f))
                CASE_EQUAL(1)
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.15f), FLOAT(1.15f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.35f), FLOAT(1.35f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.4f), FLOAT(1.4f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.55f), FLOAT(1.55f), FLOAT(1.0f))
                CASE_DEFAULT
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.2f), FLOAT(1.2f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.3f), FLOAT(1.3f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.5f), FLOAT(1.5f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.6f), FLOAT(1.6f), FLOAT(1.0f))
                    WAIT_FRAMES(PM64_FRAMES(1))
                    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.8f), FLOAT(1.8f), FLOAT(1.0f))
            END_SWITCH
        END_SIBLING_EVENT
    END_IF
    BEGIN_SIBLING_EVENT
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    END_SIBLING_EVENT
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_TIDAL_WAVE_LEAP_A|SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.6f))
    ELSE
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
    END_IF
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 7, -1)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 0, 0)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 15)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 10)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -1)
        WAIT_FRAMES(PM64_FRAMES(10))
        GOTO(95)
    END_IF
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(3))
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    END_SIBLING_EVENT
    SET_VALUE(LW(15), 0)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.4f))
        PM64_BOUNCE(6, 60, 20)
        PM64_BOUNCE(6, 30, 8)
        PM64_BOUNCE(6, 20, 6)
        PM64_BOUNCE(6, 10, 4)
    ELSE
        PM64_BOUNCE(6, 15, 8)
        PM64_BOUNCE(6, 5, 4)
    END_IF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(15))
    LABEL(95)
    PM64_HOME(3, 6.0f)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    PM64_END
EVT_END()

/* --- Sushie's own helpers (static, "sushie" names: PARTNER_GUIDE.md) */

/* the way "toward the enemies" runs for unit (PM64's +x) */
static float sushieDir(const BattleUnit* unit) {
    return unit != NULL && unit->faceDirection < 0 ? -1.0f : 1.0f;
}

/* PM64's eases of a third / a quarter of the way a PM64 frame, as a TTYD frame's share (two make one) */
#define SUSHIE_EASE_THIRD 0.18350342f   /* 1 - sqrt(2/3) */
#define SUSHIE_EASE_QUARTER 0.13397460f /* 1 - sqrt(3/4) */

static BattleUnit* sushieSelf(EventEntry* event) {
    return BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
}

/* (x, y, z) := where unit's part takes a hit (btlevtcmd_GetHitPos's place, as floats) */
static int sushieHitPos(EventEntry* event, EvtWord unitArg, EvtWord partArg, float* x, float* y, float* z) {
    const s32 id = BattleTransID(event, evtGetValue(event, unitArg));
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, id);
    BattleUnitPart* part = BattleGetUnitPartsPtr(id, evtGetValue(event, partArg));
    if (unit == NULL || part == NULL) {
        return 0;
    }
    BtlUnit_GetHitPos(unit, part, x, y, z);
    return 1;
}

/* sushieSquirtAim(target, part): she tilts back toward a target above her: a quarter of the angle it is up from
 * her mouth (8 forward, 16 up), never down (PM64: sushie.c N(SetSquirtAngle)) */
static USER_FUNC(sushieSquirtAim) {
    BattleUnit* self = sushieSelf(event);
    float gx, gy, gz, rx, ry, rz, up;
    if (self == NULL || !sushieHitPos(event, event->args[0], event->args[1], &gx, &gy, &gz)) {
        return EVT_RETURN_DONE;
    }
    {
        const float dir = sushieDir(self);
        const float dx = (gx - (self->position.x + 8.0f * dir)) * dir;
        const float dy = gy - (self->position.y + 16.0f);
        up = atan2f(dy, dx) * 57.29578f;
        if (up < 0.0f || up > 90.0f) {
            up = 0.0f;
        }
        BtlUnit_GetRotate(self, &rx, &ry, &rz);
        BtlUnit_SetRotate(self, rx, ry, up * 0.25f * dir);
    }
    return EVT_RETURN_DONE;
}

/* sushieSquirtSwell(): while the hold-A gauge runs she swells with it to twice her size (x and y; PM64's loop in
 * N(EVS_Move_Squirt): 1 + fill, and an empty gauge eases her back a quarter of the way each frame) */
static USER_FUNC(sushieSquirtSwell) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* self = sushieSelf(event);
    float s;
    if (self == NULL) {
        return EVT_RETURN_DONE;
    }
    if (!isFirstCall && !(wp->actionCommands.result & 1)) {
        return EVT_RETURN_DONE;
    }
    s = self->scale.x;
    if (wp->actionCommands.disp.gaugeRatio > 0.0f) {
        s = 1.0f + wp->actionCommands.disp.gaugeRatio;
    } else {
        s -= (s - 1.0f) * SUSHIE_EASE_QUARTER;
    }
    BtlUnit_SetScale(self, s, s, 1.0f);
    return EVT_RETURN_BLOCK;
}

/* sushieSquirtJet(target, part, miss): the jet from her mouth (10 forward, 11 up) to the target in 10 frames, as
 * big as she has swelled (PM64: EFFECT_SQUIRT with her scale); missing, on 300 past it (N(GetSquirtTargetPos)) */
static USER_FUNC(sushieSquirtJet) {
    BattleUnit* self = sushieSelf(event);
    const int miss = evtGetValue(event, event->args[2]);
    float gx, gy, gz;
    if (self == NULL || !sushieHitPos(event, event->args[0], event->args[1], &gx, &gy, &gz)) {
        return EVT_RETURN_DONE;
    }
    {
        const float ox = self->position.x + 10.0f * sushieDir(self);
        const float oy = self->position.y + 11.0f;
        const float oz = self->position.z + 5.0f;
        float s = self->scale.x;
        if (s < 1.0f) {
            s = 1.0f;
        }
        if (miss) {
            const float dx = gx - ox, dy = gy - oy;
            const float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.001f) {
                gx = ox + 300.0f * dx / len;
                gy = oy + 300.0f * dy / len;
            }
        }
        effOpukuWgunN64Entry(0, PM64_FRAMES(10), ox, oy, oz, gx, gy, gz + 5.0f, s);
    }
    return EVT_RETURN_DONE;
}

/* sushieDeflate(): back to her own size, a third of the way a PM64 frame, 7 PM64 frames (PM64: N(InflateSushie)) */
static USER_FUNC(sushieDeflate) {
    BattleUnit* self = sushieSelf(event);
    float s;
    if (self == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        event->userdata[0] = PM64_FRAMES(7) - 1;
    }
    s = self->scale.x + (1.0f - self->scale.x) * SUSHIE_EASE_THIRD;
    BtlUnit_SetScale(self, s, s, self->scale.z + (1.0f - self->scale.z) * SUSHIE_EASE_THIRD);
    if (event->userdata[0]-- > 0) {
        return EVT_RETURN_BLOCK;
    }
    BtlUnit_SetScale(self, 1.0f, 1.0f, 1.0f);
    return EVT_RETURN_DONE;
}

/* sushieWaterFx(kind, unit, up): Water Block's effects (TTYD's ports of PM64's own: eff/n64 opuku_*):
 *   1 her arc of water from her (5.5 back, 15.5 up) onto unit, 30 frames, 1 to 2 big (PM64: N(PlaySquirtFX))
 *   2 a waterfall over unit, up above it, 50 frames (EFFECT_WATERFALL)
 *   3 the block of water round unit, 1.5 big (N(PlayWaterBlockFX)) */
static USER_FUNC(sushieWaterFx) {
    const int kind = evtGetValue(event, event->args[0]);
    const BattleUnit* unit = evtUnit(event, event->args[1]);
    const float up = (float)evtGetValue(event, event->args[2]);
    BattleUnit* self = sushieSelf(event);
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (kind == 1 && self != NULL) {
        effOpukuWgunN64Entry(1, PM64_FRAMES(30), self->position.x - 5.5f * sushieDir(self), self->position.y + 15.5f,
                             self->position.z + 5.0f, unit->position.x, unit->position.y, unit->position.z,
                             1.0f + 0.1f * (float)irand(10));
    } else if (kind == 2) {
        effOpukuWcurtainN64Entry(0, PM64_FRAMES(50), unit->position.x, unit->position.y + up, unit->position.z, 1.0f);
    } else if (kind == 3) {
        effOpukuWprotectN64Entry(0, PM64_FRAMES(90), unit->position.x, unit->position.y + up, unit->position.z, 1.5f);
    }
    return EVT_RETURN_DONE;
}

/* sushieWaterBlockWeapon(weapon, turns): weapon := a copy with Mario's defense up for that many turns (PM64:
 * N(ApplyWaterBlock), the command's result the turns) */
static USER_FUNC(sushieWaterBlockWeapon) {
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    const int turns = evtGetValue(event, event->args[1]);
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    sCopy.defChangeChance = 100;
    sCopy.defChangeStrength = 1;
    sCopy.defChangeTime = (s8)(turns < 1 ? 1 : turns);
    if (pm64Log()) {
        printf("[pm64] Water Block: %d turns\n", turns);
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* sushieTidalSwell(buttons): while the buttons command runs she swells toward 1 + 3 x the share of buttons
 * pressed, a third of the way each frame (PM64: N(SetScaleTidalWaveCharge), 1 + 3 x the meter) */
static USER_FUNC(sushieTidalSwell) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* self = sushieSelf(event);
    const int buttons = evtGetValue(event, event->args[0]);
    float target, s;
    if (self == NULL || buttons <= 0) {
        return EVT_RETURN_DONE;
    }
    if (!isFirstCall && !(wp->actionCommands.result & 1)) {
        return EVT_RETURN_DONE;
    }
    /* (the live count: the look only; sushieTidalSize sets her from the synced one after) */
    target = 1.0f + 3.0f * (float)(s32)wp->actionCommands.outputParams[0] / (float)buttons;
    s = self->scale.x + (target - self->scale.x) * SUSHIE_EASE_THIRD;
    BtlUnit_SetScale(self, s, s, s);
    return EVT_RETURN_BLOCK;
}

/* sushieTidalSize(count, buttons): her size for the wave, 1 + 3 x the share of buttons pressed (the command's
 * synced outcome) */
static USER_FUNC(sushieTidalSize) {
    BattleUnit* self = sushieSelf(event);
    const int count = evtGetValue(event, event->args[0]);
    const int buttons = evtGetValue(event, event->args[1]);
    float s;
    if (self == NULL || buttons <= 0) {
        return EVT_RETURN_DONE;
    }
    s = 1.0f + 3.0f * (float)(count < 0 ? 0 : count > buttons ? buttons : count) / (float)buttons;
    BtlUnit_SetScale(self, s, s, s);
    return EVT_RETURN_DONE;
}

/* sushieTidalWave(): she is the wave (PM64: sushie.c N(ProcessTidalWave)). From where she holds the water, 32 a
 * PM64 frame from 315 degrees (PM64's: 0 down, 90 toward the enemies, 180 up), in a box x -160..160, y 0..130:
 * at a side she turns back if heading out, with a new drift of -2..2 (half a degree a frame for each); the
 * enemies' side, the floor and the top sound her leaps. Her tilt follows her course (course - 315), her
 * swell shrinks 0.06 a frame to her size, a fountain of water trails her (15 x her size behind, turned
 * with her: one, made once), a splash where she is each frame; 20 frames in, the water's sound and the screen
 * ripples (fx_underwater: TTYD's opuku_jetwrain, 120 frames; on PC its draw is PM64's grid over the 3D camera's
 * frame, the HUD over it: src/effect/n64/eff_opuku_jetwrain_n64.c). After 90 frames the next side she meets she runs on through, 5 frames more, and the
 * fountain ends. Blocks. All in PM64 frames: at TTYD's 60 a second the steps are halved and the times doubled
 * (PM64_PER_FRAME, PM64_FRAMES), a splash every other frame. */
static USER_FUNC(sushieTidalWave) {
    static struct {
        float x, y, angle, drift;
        int time, timeUp, phase, tail, leap, tick;
        EffectEntry* fountain;
        void* fountainWork; /* (its userdata and type: a pool entry taken by another effect since is left be) */
        const char* fountainType;
    } w;
    BattleUnit* self = sushieSelf(event);
    float dir, rx, ry, rz;
    int crossed = 0;
    if (self == NULL) {
        return EVT_RETURN_DONE;
    }
    dir = sushieDir(self);
    if (w.fountain != NULL && !effStillOurs(w.fountain, w.fountainWork, w.fountainType)) {
        w.fountain = NULL; /* (gone with a battle torn down, or its pool entry another's now) */
    }
    if (isFirstCall) {
        w.x = self->position.x * dir;
        w.y = self->position.y;
        w.angle = 315.0f;
        w.drift = 0.0f;
        w.time = PM64_FRAMES(90);
        w.tick = 0;
        w.timeUp = 0;
        w.phase = 1;
        w.tail = 0;
        w.leap = 0;
        if (w.fountain != NULL) {
            effSoftDelete(w.fountain); /* (one left by a wave cut short) */
        }
        w.fountain = effOpukuJetwN64Entry(1, 0, self->position.x, w.y, self->position.z, 1.0f);
        w.fountainWork = w.fountain != NULL ? w.fountain->userdata : NULL;
        w.fountainType = w.fountain != NULL ? w.fountain->type : NULL;
    }
    if (w.phase == 1) {
        w.x += PM64_PER_FRAME(32.0f) * sinf(w.angle * 0.017453292f);
        w.y -= PM64_PER_FRAME(32.0f) * cosf(w.angle * 0.017453292f);
        if (w.x < -160.0f) {
            if (w.timeUp) {
                crossed = 1;
            } else if (w.angle >= 180.0f && w.angle < 360.0f) {
                w.angle = 360.0f - w.angle;
                w.drift = (float)(irand(5) - 2);
            }
        }
        if (!crossed && w.x > 160.0f) {
            if (w.timeUp) {
                crossed = 1;
            } else {
                if (w.angle >= 0.0f && w.angle < 180.0f) {
                    w.angle = 360.0f - w.angle;
                    w.drift = (float)(irand(5) - 2);
                }
                w.leap ^= 1;
                psndSFXOn(w.leap ? "SFX_BTL_STAGE_FALL_WATER1" : "SFX_PARTY_BATTLE_JUMP1");
            }
        }
        if (!crossed && (w.y < 0.0f || w.y > 130.0f)) {
            if (w.timeUp) {
                crossed = 1;
            } else {
                const int atFloor = w.y < 0.0f;
                if (atFloor ? (w.angle >= 270.0f || w.angle < 90.0f) : (w.angle >= 90.0f && w.angle < 270.0f)) {
                    w.angle = 180.0f - w.angle;
                    if (w.angle < 0.0f) {
                        w.angle += 360.0f;
                    }
                    w.drift = (float)(irand(5) - 2);
                }
                w.leap ^= 1;
                psndSFXOn(w.leap ? "SFX_BTL_STAGE_FALL_WATER1" : "SFX_PARTY_BATTLE_JUMP1");
            }
        }
        if (crossed) {
            w.phase = 2;
            w.tail = PM64_FRAMES(5);
        } else {
            float s, tilt;
            w.angle += PM64_PER_FRAME(w.drift * 0.5f);
            while (w.angle < 0.0f) {
                w.angle += 360.0f;
            }
            while (w.angle >= 360.0f) {
                w.angle -= 360.0f;
            }
            tilt = w.angle - 315.0f;
            if (tilt < 0.0f) {
                tilt += 360.0f;
            }
            BtlUnit_GetRotate(self, &rx, &ry, &rz);
            BtlUnit_SetRotate(self, rx, ry, tilt * dir);
            s = self->scale.x - PM64_PER_FRAME(0.06f);
            if (s < 1.0f) {
                s = 1.0f;
            }
            BtlUnit_SetScale(self, s, s, s);
            if (w.fountain != NULL) {
                EffOpukuJetwN64Work* f = (EffOpukuJetwN64Work*)w.fountain->userdata;
                f->position.x = (w.x - 15.0f * s * sinf(w.angle * 0.017453292f)) * dir;
                f->position.y = w.y + 15.0f * s * cosf(w.angle * 0.017453292f);
                f->position.z = self->position.z + 5.0f;
                f->rotZ = dir > 0.0f ? w.angle : 360.0f - w.angle;
                f->scaleX = s;
                f->scaleY = s;
            }
            if (w.time == PM64_FRAMES(70)) {
                psndSFXOn("SFX_BTL_STAGE_FALL_WATER1");
                effOpukuJetwrainN64Entry(0, -50.0f * dir, 20.0f, 0.0f, 1.0f, PM64_FRAMES(120)); /* the ripple */
            }
            if (w.time != 0) {
                w.time--;
            } else {
                w.timeUp = 1;
            }
        }
    }
    if (w.phase == 2) {
        w.x += PM64_PER_FRAME(32.0f) * sinf(w.angle * 0.017453292f);
        w.y -= PM64_PER_FRAME(32.0f) * cosf(w.angle * 0.017453292f);
        if (w.fountain != NULL) {
            EffOpukuJetwN64Work* f = (EffOpukuJetwN64Work*)w.fountain->userdata;
            f->position.x = w.x * dir;
            f->position.y = w.y;
            f->position.z = self->position.z;
        }
        if (w.tail == 0) {
            BtlUnit_GetRotate(self, &rx, &ry, &rz);
            BtlUnit_SetRotate(self, rx, ry, 0.0f);
            if (w.fountain != NULL) {
                effSoftDelete(w.fountain);
                w.fountain = NULL;
            }
            return EVT_RETURN_DONE;
        }
        w.tail--;
    }
    BtlUnit_SetPos(self, w.x * dir, w.y, self->position.z);
    if ((w.tick++ & 1) == 0) { /* (a splash each PM64 frame) */
        effSplashEntry(1, w.x * dir, w.y, self->position.z + 5.0f, 1.0f);
    }
    return EVT_RETURN_BLOCK;
}

/* Sushie's Squirt. PM64: battle/partner/sushie.c N(EVS_Move_Squirt), N(SetSquirtAngle), N(InflateSushie),
 * N(GetSquirtTargetPos), N(EVS_ReturnHome_Squirt_Success/_Miss). The camera close on her; she hops in place (10
 * frames, gravity 1.0: Jump, Fall after 5), tilts back toward the enemy (a quarter of its angle above her
 * mouth), and draws in water (Inhale) while the meter runs: she swells with it to twice her size (x, y). The
 * meter is TTYD's: Bobbery's hold A and let go when it's full, standing for PM64's hold-A meter (90 frames).
 * Then the camera on the enemies; she shoots (Squirt): the jet from 10 forward, 11 up to the enemy in 10 frames,
 * as big as she swelled, while she shrinks back a third of the way a frame; 10 frames, the hit (water, no
 * contact; damage the meter's: the row). Dodged: the jet runs on 300 past it, she is her size at once, 20
 * frames. Either way she flies home (Run) in 30 frames, cos in-out, and idles. Online: her size as she shoots
 * comes from the command's synced outcome (twice her size on a good one, her own on a failed one); the swell
 * while charging follows the gauge, the look only. */
static EVT_BEGIN(sSquirtEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_CAM_SELF
    PM64_FACE_TARGET
    BEGIN_SIBLING_EVENT
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        WAIT_FRAMES(PM64_FRAMES(5))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    END_SIBLING_EVENT
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -1)
    CALL(sushieSquirtAim, LW(3), LW(4))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 9, -1)
    PM64_AC_HOLD_A
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    WAIT_FRAMES(1) /* (a frame after SetupAC: the command reads its parameters then; started the same frame it never does) */
    CALL(btlevtcmd_StartAC, 1)
    CALL(sushieSquirtSwell)
    CALL(btlevtcmd_ResultAC)
    /* her size as she shoots: the command's outcome (synced online), full on a good one, her own on a failed
       one (the gauge's live fill above is the look only) */
    CALL(btlevtcmd_GetResultAC, LW(7))
    IF_BITS_SET(LW(7), 2)
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(2.0f), FLOAT(2.0f), FLOAT(1.0f))
    ELSE
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    END_IF
    CALL(evt_snd_sfxon, STRING("SOUND_SUSHIE_SQUIRT|SFX_BTL_STAGE_FALL_WATER1"), 0)
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
    IF_INT_NOT_EQUAL(LW(5), 1)
        CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
        CALL(sushieSquirtJet, LW(3), LW(4), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        WAIT_FRAMES(PM64_FRAMES(20))
        GOTO(90)
    END_IF
    CALL(sushieSquirtJet, LW(3), LW(4), 0)
    BEGIN_SIBLING_EVENT
        CALL(sushieDeflate)
    END_SIBLING_EVENT
    WAIT_FRAMES(PM64_FRAMES(10))
    SET_VALUE(LW(15), 0)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    LABEL(90)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    PM64_CAM_STAGE
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    PM64_END
EVT_END()

/* Sushie's Water Block. PM64: battle/partner/sushie.c N(EVS_Move_WaterBlock), N(PlaySquirtFX),
 * N(PlayWaterBlockFX), N(ApplyWaterBlock). The camera on Mario's place (30 frames); 10 frames; she hops 30 toward
 * him (Inhale, 10 frames, gravity 1.0) and turns round (yaw 45 a frame, 4 frames), 4 frames, and holds the water
 * (HoldWater) for the three-chances command (PM64's 100 frames, then 110 in all). Ours: TTYD's timed A (Mario's
 * jump's A, lit as the light) three times, 66 TTYD frames apart (PM64's 100 over three): each chance the A
 * lights for 24 TTYD frames; a press while it's lit wins, a press before it (or twice) loses that chance. The
 * chance's outcome is read after each ResultAC (GetResultAC, AcGetOutputParam: synced online). The chance won is the turns: 3, 2, 1
 * (Ultra rank: 4, 3, 2; PM64's three-chances quality, not in the decomp here). None won: she hops home.
 * Won: she spits (SpitArc; camera a little wider) an arc of water onto Mario (30 frames; she celebrates 65
 * later); 45 frames on a waterfall pours over him (83 up, 50 frames) and, 20 later, five sparkles fall down him
 * (100 to 20 up, 5 frames apart); 40 frames on the block of water forms round him (1.5); 30 frames on his
 * defense goes up 1 for the turns (TTYD's defense-up for PM64's water block: no fire immunity in TTYD), 4
 * frames, the camera back. She hops home (Run, 10 frames) and turns round at once. */
static EVT_BEGIN(sWaterBlockEvt)
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
    WAIT_FRAMES(PM64_FRAMES(10))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 9, -1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 30)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -1)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 1, 1, 0)
    SET_VALUE(LW(0), 0)
    /* (PM64's 45 a frame for 4: 22 and 23 a TTYD frame for 8) */
    LOOP(4)
        ADD(LW(0), 22)
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
        WAIT_FRAMES(1)
        ADD(LW(0), 23)
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
        WAIT_FRAMES(1)
    END_LOOP
    WAIT_FRAMES(PM64_FRAMES(4))
    CALL(btlevtcmd_StartAC, 1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 11, -1)
    /* (the A slides in) */
    WAIT_FRAMES(14)
    /* the three chances: LW(9) the turns this chance gives, LW(10) the turns won */
    CALL(pm64WeaponRank, LW(12), LW(0))
    SET_VALUE(LW(9), 3)
    IF_INT_GREATER_OR_EQUAL(LW(0), 2)
        SET_VALUE(LW(9), 4)
    END_IF
    SET_VALUE(LW(10), 0)
    SET_VALUE(LW(11), 2)
    LOOP(3)
        WAIT_FRAMES(42)
        CALL(btlevtcmd_ac_timing_flag_onoff, 1, 1)
        CALL(btlevtcmd_ac_timing_get_success_frame, LW(1))
        WAIT_FRAMES(LW(1))
        SET_VALUE(LW(8), 0)
        LOOP(0)
            CALL(btlevtcmd_ResultAC)
            CALL(btlevtcmd_GetResultAC, LW(2))
            IF_BITS_SET(LW(2), 2)
                SET_VALUE(LW(8), 1)
                BREAK_LOOP
            END_IF
            CALL(btlevtcmd_AcGetOutputParam, 0, LW(2))
            IF_INT_EQUAL(LW(2), 1)
                BREAK_LOOP
            END_IF
            IF_INT_GREATER_OR_EQUAL(LW(1), 24)
                BREAK_LOOP
            END_IF
            WAIT_FRAMES(4)
            ADD(LW(1), 4)
        END_LOOP
        CALL(btlevtcmd_ac_timing_flag_onoff, 0, 1)
        IF_INT_EQUAL(LW(8), 1)
            SET_VALUE(LW(10), LW(9))
            BREAK_LOOP
        END_IF
        SUBTRACT(LW(9), 1)
        SUBTRACT(LW(11), 1)
        /* (what's left of the 24 lit frames) */
        SET_VALUE(LW(0), 24)
        SUBTRACT(LW(0), LW(1))
        IF_INT_GREATER(LW(0), 0)
            WAIT_FRAMES(LW(0))
        END_IF
    END_LOOP
    CALL(btlevtcmd_StopAC)
    IF_INT_EQUAL(LW(10), 0)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        GOTO(90)
    END_IF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 12, -1)
    CALL(evt_btl_camera_set_zoom, 0, 220)
    BEGIN_SIBLING_EVENT
        CALL(evt_snd_sfxon, STRING("SOUND_CREATE_WATER_BLOCK|SFX_BTL_STAGE_FALL_WATER1"), 0)
        CALL(sushieWaterFx, 1, LW(3), 0)
        WAIT_FRAMES(PM64_FRAMES(65))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 17, -1)
    END_SIBLING_EVENT
    WAIT_FRAMES(PM64_FRAMES(45))
    CALL(sushieWaterFx, 2, LW(3), 83)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_STAGE_FALL_WATER1"), 0)
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(20))
        SET_VALUE(LW(0), 100)
        LOOP(5)
            CALL(pm64Fx, 1, LW(3), LW(0))
            SUBTRACT(LW(0), 20)
            WAIT_FRAMES(PM64_FRAMES(5))
        END_LOOP
    END_SIBLING_EVENT
    WAIT_FRAMES(PM64_FRAMES(40))
    CALL(sushieWaterFx, 3, LW(3), 0)
    WAIT_FRAMES(PM64_FRAMES(30))
    CALL(sushieWaterBlockWeapon, LW(12), LW(10))
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL(btlevtcmd_GetResultPrizeLv, LW(3), LW(11), LW(6))
    IF_INT_GREATER_OR_EQUAL(LW(6), 0)
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        ADD(LW(1), 35)
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    END_IF
    WAIT_FRAMES(PM64_FRAMES(4))
    LABEL(90)
    PM64_CAM_STAGE
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -1)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    PM64_END
EVT_END()

/* Sushie's Tidal Wave. PM64: battle/partner/sushie.c N(EVS_Move_TidalWave), N(SetScaleTidalWaveCharge),
 * N(ProcessTidalWave). The camera on her side (20 frames); a little hop in place (Run, gravity 0.5, 5 frames),
 * then 30 toward the enemies and 40 up (gravity 1.0, 15 frames); she holds the water (HoldWater), turning about
 * 12 up, for the command (PM64's random A / B / C-down presses, 100 frames; ours TTYD's own press-the-buttons-
 * shown, Fiery Jinx's kind: up to 13 random A / B / X / Y one after another, about 100 frames at the normal
 * difficulty, a wrong button ends it with what was pressed). She swells toward 1 + 3 x the share pressed, a
 * third of the way a frame; 54 frames in she shudders (1 frame each: 8 x 0/-1, 7 x 1/-1, 8 x 1/-2). The camera
 * back (5 frames), 10 frames, and she is the wave (sushieTidalWave: N(ProcessTidalWave), above); then off the
 * left side (-260: past TTYD's wider stage; PM64 -220), 15 frames, the camera on the enemies (20 frames).
 * Damage: the buttons pressed, to every enemy (PM64: the meter's quality), 5 frames after each one hit;
 * 6 or more a Nice hit. She drops in from 150 over her place (Idle, gravity 1.0, 40 frames) and settles with
 * two hops in place (gravity 0.7: 20 and 10 frames). */
static EVT_BEGIN(sTidalWaveEvt)
    PM64_BEGIN(PM64_ALL)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, 1)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 4)
    CALL(evt_btl_camera_set_zoom, 0, 400)
    CALL(evt_btl_camera_set_posoffset, 0, 20, 0, 0)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    SUBTRACT(LW(0), 3)
    SWITCH(LW(0))
        CASE_LESS_OR_EQUAL(-3)
        SET_VALUE(LW(0), 160)
        CASE_EQUAL(-2)
        SET_VALUE(LW(0), 130)
        CASE_EQUAL(-1)
        SET_VALUE(LW(0), 115)
        CASE_EQUAL(0)
        SET_VALUE(LW(0), 100)
        CASE_EQUAL(1)
        SET_VALUE(LW(0), 90)
        CASE_EQUAL(2)
        SET_VALUE(LW(0), 80)
        CASE_DEFAULT
        SET_VALUE(LW(0), 70)
    END_SWITCH
    CALL(btlevtcmd_AcSetParamAll, LW(0), 1, 13, -3, 1, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 7)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 10, 1, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(0.5f))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(5), -1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 30)
    ADD(LW(1), 40)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(15), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 11, -1)
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 12, 0)
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(54))
        LOOP(8)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -1, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
        LOOP(7)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 1, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -1, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
        LOOP(8)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 1, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -2, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    END_SIBLING_EVENT
    CALL(sushieTidalSwell, 13)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 0, LW(7))
    /* (her size from the synced count, not the live one: online the same everywhere) */
    CALL(sushieTidalSize, LW(7), 13)
    CALL(btlevtcmd_StopAC)
    PM64_CAM_STAGE
    WAIT_FRAMES(PM64_FRAMES(10))
    CALL(evt_snd_sfxon, STRING("SOUND_TIDAL_WAVE_WATER|SFX_BTL_STAGE_FALL_WATER1"), 0)
    CALL(sushieTidalWave)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, -260, 0, LW(2))
    CALL(btlevtcmd_SetScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    WAIT_FRAMES(PM64_FRAMES(15))
    /* damage: the buttons pressed (LW(7)), on every rank */
    CALL(pm64GaugeDamage, LW(12), LW(7), 0, 1)
    SET_VALUE(LW(15), 0)
    LABEL(10)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        GOTO(50)
    END_IF
    CALL(pm64Fx, 13, LW(3), 10)
    IF_INT_GREATER_OR_EQUAL(LW(7), 6)
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        SET_VALUE(LW(0), 0)
        IF_INT_GREATER_OR_EQUAL(LW(7), 9)
            SET_VALUE(LW(0), 1)
        END_IF
        IF_INT_GREATER_OR_EQUAL(LW(7), 12)
            SET_VALUE(LW(0), 2)
        END_IF
        CALL(btlevtcmd_GetResultPrizeLv, LW(3), LW(0), LW(6))
        IF_INT_GREATER_OR_EQUAL(LW(6), 0)
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
            ADD(LW(15), 1)
        END_IF
    ELSE
        CALL(btlevtcmd_CommandCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    END_IF
    WAIT_FRAMES(PM64_FRAMES(5))
    LABEL(50)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(10)
    END_IF
    IF_INT_EQUAL(LW(15), 0)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 150)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(40), -1)
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(0.7f))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -1)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    PM64_END
EVT_END()
