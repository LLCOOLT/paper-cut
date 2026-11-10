/* Lakilester's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/lakilester.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include.
 *
 * His rows (BattleLakilester): Idle 1, Walk 2, Run 3, CloudNineIdle 4, CloudNineBegin 5, CloudNineEnd 6,
 * LiftSpiny 15, HoldSpiny 16, StrainHoldSpiny 17, ThrowSpiny 18, ThrowSpinyAlt 19, Spiny 20, SpinySpin 21,
 * HurricaneInhale 22, HurricaneExhale 23. The spiny he holds and throws is the overlay row drawn beside him
 * (PM64: his second part), placed and flown by the helpers below.
 *
 * PM64's numbers are its 30 fps ones, made TTYD's 60 with PM64_FRAMES / PM64_SPEED / PM64_ACCEL /
 * PM64_PER_FRAME (pm64_partner_pc.c); the helpers below take PM64's frame counts and convert them. */

/* the TTYD moves whose action-command help fits (Flurrie's Body Slam: line the sight up; Dodgy Fog: press A on
 * each signal) */
extern BattleWeapon partyWeapon_ClaudaNormalAttack, partyWeapon_ClaudaKumogakureAttack;

/* --- the spiny beside him: placed, or in flight. A new place or flight bumps sLakiGen; an older flight sees
 * that and stops. */
static struct {
    float sx, sy, sz, tx, ty, tz, height;
    int frame, frames, hide;
} sLakiFly;
static int sLakiGen;

/* his port-script slot, -1 if none is on */
static int lakiSlot(const BattleUnit* unit) {
    const int i = forcedSlot(unit, 0);
    return i >= 0 && sForced[i].anim >= 0 ? i : -1;
}

/* lakilesterAnim(unit, row): he plays row; the spiny beside him stays as it is (pm64SetAnim would clear it) */
static USER_FUNC(lakilesterAnim) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    int i;
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    i = lakiSlot(unit);
    if (i < 0) {
        i = forcedSlot(unit, 1);
        if (i < 0) {
            return EVT_RETURN_DONE;
        }
        sForced[i].overlay = -1;
    }
    sForced[i].anim = (signed char)evtGetValue(event, event->args[1]);
    sForced[i].owner = pm64Owner(event, unit);
    return EVT_RETURN_DONE;
}

/* lakilesterSpinyAt(unit, row, x, y, z): the spiny (row; -1 gone) at the stage place (x, y, z) */
static USER_FUNC(lakilesterSpinyAt) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    int i;
    if (unit == NULL || (i = lakiSlot(unit)) < 0) {
        return EVT_RETURN_DONE;
    }
    sLakiGen++;
    sForced[i].overlay = (signed char)evtGetValue(event, event->args[1]);
    sForced[i].ox = (float)evtGetValue(event, event->args[2]) - unit->position.x;
    sForced[i].oy = (float)evtGetValue(event, event->args[3]) - unit->position.y;
    sForced[i].oz = (float)evtGetValue(event, event->args[4]) - unit->position.z;
    return EVT_RETURN_DONE;
}

static void lakiFlyStart(EventEntry* event, const BattleUnit* unit, int i, int row, float x, float y, float z,
                         int frames, float height, int hide) {
    sLakiGen++;
    event->userdata[0] = (EvtWord)sLakiGen;
    if (row >= 0) {
        sForced[i].overlay = (signed char)row;
    }
    sLakiFly.sx = sForced[i].ox;
    sLakiFly.sy = sForced[i].oy;
    sLakiFly.sz = sForced[i].oz;
    sLakiFly.tx = x - unit->position.x;
    sLakiFly.ty = y - unit->position.y;
    sLakiFly.tz = z - unit->position.z;
    sLakiFly.height = height;
    sLakiFly.frame = 0;
    sLakiFly.frames = frames > 0 ? PM64_FRAMES(frames) : 1;
    sLakiFly.hide = hide;
}

static int lakiFlyStep(EventEntry* event, const BattleUnit* unit) {
    const int i = lakiSlot(unit);
    float t;
    if (i < 0 || event->userdata[0] != (EvtWord)sLakiGen) {
        return EVT_RETURN_DONE;
    }
    sLakiFly.frame++;
    t = (float)sLakiFly.frame / (float)sLakiFly.frames;
    sForced[i].ox = sLakiFly.sx + (sLakiFly.tx - sLakiFly.sx) * t;
    sForced[i].oy = sLakiFly.sy + (sLakiFly.ty - sLakiFly.sy) * t + 4.0f * sLakiFly.height * t * (1.0f - t);
    sForced[i].oz = sLakiFly.sz + (sLakiFly.tz - sLakiFly.sz) * t;
    if (sLakiFly.frame >= sLakiFly.frames) {
        if (sLakiFly.hide) {
            sForced[i].overlay = -1;
        }
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* lakilesterSpinyFly(unit, row, x, y, z, frames, height, hide): the spiny (now row; -1 keeps its row) flies from
 * where it is to the stage place (x, y, z) along an arc height high, in PM64 frames (PM64's JumpPartTo: a gravity g
 * over n frames is an arc g*n*n/8 high); hide: gone when it gets there. Blocks. */
static USER_FUNC(lakilesterSpinyFly) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    int i;
    if (unit == NULL || (i = lakiSlot(unit)) < 0) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        lakiFlyStart(event, unit, i, evtGetValue(event, event->args[1]), (float)evtGetValue(event, event->args[2]),
                     (float)evtGetValue(event, event->args[3]), (float)evtGetValue(event, event->args[4]),
                     evtGetValue(event, event->args[5]), (float)evtGetValue(event, event->args[6]),
                     evtGetValue(event, event->args[7]));
    }
    return lakiFlyStep(event, unit);
}

/* lakilesterSpinyToss(unit): Spiny Surge's throw (looks only). PM64: lakilester.c N(ThrowSpinyFX) (fx_throw_spiny
 * from over him, 10-149 and 10-89 at random, 20 frames). The effect itself isn't decompiled: here the spiny
 * (SpinySpin) flies from his hands 1.5 times the first number ahead, down to the floor, along an arc the second number high,
 * in 20 frames, and is gone. Blocks. */
static USER_FUNC(lakilesterSpinyToss) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    int i;
    if (unit == NULL || (i = lakiSlot(unit)) < 0) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        const float face = unit->faceDirection < 0 ? -1.0f : 1.0f;
        const float ahead = (float)(10 + irand(140)) * 1.5f;
        const float high = (float)(10 + irand(80));
        /* (from his hands: 3 ahead, 34 up, 5 in front) */
        sForced[i].ox = face * 3.0f;
        sForced[i].oy = 34.0f;
        sForced[i].oz = 5.0f;
        lakiFlyStart(event, unit, i, 21, unit->position.x + face * ahead, 0.0f, unit->position.z + 5.0f, 20, high, 1);
    }
    return lakiFlyStep(event, unit);
}

/* lakilesterMissPos(x, y): a missed throw's place, about the aim point (x 20 either way, y 10) */
static USER_FUNC(lakilesterMissPos) {
    evtSetValue(event, event->args[0], evtGetValue(event, event->args[0]) + irand(41) - 20);
    evtSetValue(event, event->args[1], evtGetValue(event, event->args[1]) + irand(21) - 10);
    return EVT_RETURN_DONE;
}

/* lakilesterPlain(weapon, keepGale): weapon := a copy with only PM64's effects: no counter touches it (PM64: his
 * spinies are thrown, DAMAGE_TYPE_NO_CONTACT; Spiny Flip's slot weapon is Ground Pound, a stomp) and none of
 * the slot weapon's statuses (Mini-Egg's shrink, Ground Pound's, Gale Force's): PM64's Spiny Flip and Spiny
 * Surge give none; keepGale: Hurricane keeps its blow-away chance, nothing else. Take the copy after the
 * damage is set (lakilesterSurgeDamage) and after pm64StatusWeapon. */
static USER_FUNC(lakilesterPlain) {
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    const int keepGale = evtGetValue(event, event->args[1]);
    u8 gale;
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    gale = sCopy.galeForceChance;
    memset(&sCopy.sleepChance, 0, (size_t)((char*)sCopy.pad_AE - (char*)&sCopy.sleepChance));
    if (keepGale) {
        sCopy.galeForceChance = gale;
    }
    sCopy.counterResistFlags = COUNTER_RESIST_ALL;
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* lakilesterSurgeDamage(weapon, fill 0-100): Spiny Surge's damage. PM64: lakilester.c N(GetSpinySurgeDamage): by
 * the meter, by his rank: Normal 1 (2 full), Super 1 to 70, 2 to 99, 3 full; Ultra 1 to 40, 2 to 70, 3 to 99,
 * 4 full. */
static USER_FUNC(lakilesterSurgeDamage) {
    BattleWeapon* w = (BattleWeapon*)evtGetValue(event, event->args[0]);
    const int fill = evtGetValue(event, event->args[1]);
    int rank, dmg, i;
    if (w == NULL || w->hpDamageFn != weaponGetPowerFromPartyAttackLv) {
        return EVT_RETURN_DONE;
    }
    rank = weaponRank(w);
    if (rank <= 0) {
        dmg = fill <= 99 ? 1 : 2;
    } else if (rank == 1) {
        dmg = fill <= 70 ? 1 : fill <= 99 ? 2 : 3;
    } else {
        dmg = fill <= 40 ? 1 : fill <= 70 ? 2 : fill <= 99 ? 3 : 4;
    }
    for (i = 0; i < 6; i++) {
        w->hpDamageWork[i] = (u32)dmg;
    }
    pm64HoldDamage(event, w); /* (kept over the per-frame re-wear until PM64_END) */
    return EVT_RETURN_DONE;
}

/* lakilesterCloudNine(weapon, turns): weapon := a copy whose Dodgy lasts turns (PM64: lakilester.c
 * N(ApplyCloudNine): as many turns as the command's result, a 50% dodge: TTYD's Dodgy) */
static USER_FUNC(lakilesterCloudNine) {
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    const int turns = evtGetValue(event, event->args[1]);
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    sCopy.dodgyChance = 100;
    sCopy.dodgyTime = (s8)(turns < 1 ? 1 : turns > 9 ? 9 : turns);
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* lakilesterPuff(unit, up): a cloud puff about the unit, up units up (20 either way across, 15 up and down);
 * PM64's EFFECT_FLOATING_CLOUD_PUFF */
static USER_FUNC(lakilesterPuff) {
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    const float up = (float)evtGetValue(event, event->args[1]);
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    effKemuTestEntry(5, unit->position.x + (float)(irand(41) - 20), unit->position.y + up + (float)(irand(31) - 15),
                     unit->position.z + 5.0f, 1.0f);
    return EVT_RETURN_DONE;
}

/* the enemies a Hurricane can blow away (TTYD's Gale Force vulnerability), for their spin */
static int lakiBlowable(const BattleUnit* u) {
    return u != NULL && u->kindParams != NULL && u->kindParams->statusVulnerability != NULL &&
           u->kindParams->statusVulnerability->galeForce > 0;
}

static void lakiSpinTargets(float degrees) {
    BattleWork* wp = _battleWorkPointer;
    int k;
    for (k = 0; k < wp->weaponTargets.numTargets; k++) {
        const BattleWorkTarget* t = &wp->weaponTargets.targets[wp->weaponTargets.targetIndices[k]];
        BattleUnit* u = BattleGetUnitPtr(wp, t->unitIdx);
        if (lakiBlowable(u)) {
            BtlUnit_AddRotate(u, 0.0f, degrees, 0.0f);
        }
    }
}

/* lakilesterHurricane(unit): his breath for 165 frames. PM64: lakilester.c N(ProcessHurricane), re-done: the
 * breath builds while he blows out (HurricaneExhale) until its power passes 5000 and a ceiling (2000 the first
 * time, 1000 more each breath, to 5000), then he breathes in (HurricaneInhale) until it falls as far below 5000;
 * how hard it blows (to 750) steps up through each exhale, and the enemies that can be blown away spin 13 + 4 a
 * frame per 100 of it, set straight again at the end. The game's numbers, step for step, one step a PM64 frame
 * (two TTYD frames); it doesn't hang on the buttons. Not here: PM64's blue breath stream (a puff ahead of his mouth every 6 frames of exhaling instead)
 * and the clouds racing across the stage. Blocks. */
static USER_FUNC(lakilesterHurricane) {
    static struct {
        int frame, phase, breath, ceiling, saved, step;
        int intensity, power;
        float spun;
    } h;
    const BattleUnit* unit = evtUnit(event, event->args[0]);
    const float halfPi = 1.5707963f;
    int i, level;
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        h.frame = 0;
        h.phase = 0;
        h.breath = 0;
        h.ceiling = 2000;
        h.saved = 0;
        h.intensity = 0;
        h.power = 5000;
        h.step = 21; /* (13 + 8 at no intensity) */
        h.spun = 0.0f;
    }
    level = h.intensity / 100;
    if (level > 10) {
        level = 10;
    }
    if (h.frame >= PM64_FRAMES(165)) {
        lakiSpinTargets(-h.spun);
        return EVT_RETURN_DONE;
    }
    /* (every TTYD frame: the spin at half PM64's per-frame step; the breath steps once a PM64 frame) */
    lakiSpinTargets(PM64_PER_FRAME(13.0f + 4.0f * (float)level));
    h.spun += PM64_PER_FRAME(13.0f + 4.0f * (float)level);
    /* his row by the breath (last step's) */
    i = lakiSlot(unit);
    if (i >= 0 && h.breath != 0) {
        sForced[i].anim = (signed char)(h.breath > 0 ? 23 : 22);
    }
    if (h.breath > 0 && h.frame % PM64_FRAMES(6) == 0) {
        const float face = unit->faceDirection < 0 ? -1.0f : 1.0f;
        effKemuTestEntry(5, unit->position.x + face * 12.0f, unit->position.y + 15.0f, unit->position.z + 5.0f, 1.0f);
    }
    h.frame++;
    if (h.frame % PM64_FRAMES(1) != 0) {
        return EVT_RETURN_BLOCK;
    }
    /* the breath's next step */
    for (;;) {
        if (h.phase == 0) {
            /* blowing out: the power builds, faster the further below 5000 it began */
            const float p = h.power > 5000 ? 5000.0f : (float)h.power;
            const float gain = (float)h.step + (float)h.step * (1.0f - sinf(p * halfPi / 5000.0f + halfPi)) * 1.5f;
            h.breath = 1;
            h.intensity = (int)((float)h.intensity + gain);
            h.saved += h.step;
            if (h.power > 9000) {
                h.intensity -= h.intensity / 2;
            }
            if (h.intensity < 0) {
                h.intensity = 0;
            }
            h.intensity = (int)((float)h.intensity + gain);
            if (h.intensity > 750) {
                h.intensity = 750;
            }
            if (h.saved > 1000) {
                h.saved = 1000;
            }
            h.power += h.intensity;
            if (h.power > h.ceiling + 5000) {
                h.power = h.ceiling + 5000;
                h.phase = 1;
                h.intensity = h.saved;
            }
        } else if (h.phase == 1) {
            h.breath = 0;
            h.phase = 10;
        }
        if (h.phase == 10) {
            /* breathing in: the power falls a third more than the intensity (200 at least) a frame */
            int drop = h.intensity + h.intensity / 3;
            if (drop < 200) {
                drop = 200;
            }
            h.breath = -1;
            h.power -= drop;
            if (h.power < 5000 - h.ceiling) {
                h.power = 5000 - h.ceiling;
                h.phase = 11;
                h.ceiling = h.ceiling + 1000 > 5000 ? 5000 : h.ceiling + 1000;
            }
        } else if (h.phase == 11) {
            /* the next breath out, a little weaker to start */
            h.breath = 0;
            h.phase = 0;
            h.intensity = (int)((float)h.intensity - (float)h.intensity / 2.8f);
            h.saved = h.intensity;
            h.step = (int)((1.0f - sinf((float)h.intensity * halfPi / 750.0f)) * 8.0f + 13.0f);
            continue;
        }
        break;
    }
    return EVT_RETURN_BLOCK;
}

/* PM64's mash meters on TTYD's gauge (Bob-ombast's kind 6) with the input and time PM64's command has (frames:
 * TTYD's, PM64_FRAMES of PM64's): input 7 (let go of the stick held left, Shell Slam's), 11 (A and B by turns,
 * Smooch's); gain/drain by difficulty (LW(1)/LW(2): gains for TTYD's press rate over that span), the success mark
 * at full (PM64's "nice" is the full meter) */
#define LAKI_AC_MASH(input, frames, g0, g1, g2, g3, g4, g5, g6)                                              \
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))                                                           \
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))                                                     \
    CALL(btlevtcmd_AcGetDifficulty, LW(0))                                                                    \
    SUBTRACT(LW(0), 3)                                                                                        \
    SWITCH(LW(0))                                                                                             \
        CASE_LESS_OR_EQUAL(-3)                                                                                \
        SET_VALUE(LW(1), g0)                                                                                  \
        SET_VALUE(LW(2), 15)                                                                                  \
        CASE_EQUAL(-2)                                                                                        \
        SET_VALUE(LW(1), g1)                                                                                  \
        SET_VALUE(LW(2), 20)                                                                                  \
        CASE_EQUAL(-1)                                                                                        \
        SET_VALUE(LW(1), g2)                                                                                  \
        SET_VALUE(LW(2), 23)                                                                                  \
        CASE_EQUAL(0)                                                                                         \
        SET_VALUE(LW(1), g3)                                                                                  \
        SET_VALUE(LW(2), 25)                                                                                  \
        CASE_EQUAL(1)                                                                                         \
        SET_VALUE(LW(1), g4)                                                                                  \
        SET_VALUE(LW(2), 26)                                                                                  \
        CASE_EQUAL(2)                                                                                         \
        SET_VALUE(LW(1), g5)                                                                                  \
        SET_VALUE(LW(2), 28)                                                                                  \
        CASE_DEFAULT                                                                                          \
        SET_VALUE(LW(1), g6)                                                                                  \
        SET_VALUE(LW(2), 30)                                                                                  \
    END_SWITCH                                                                                                \
    CALL(btlevtcmd_AcSetParamAll, input, frames, 178, LW(1), LW(2), 20, 100, EVT_NULLPTR)                     \
    CALL(btlevtcmd_AcSetFlag, 64)

/* the way home. PM64: lakilester.c N(EVS_ReturnHome_Success/_Miss): Run to his place in 15 frames, cosine
 * in-out, then Walk (his floating) */
#define LAKI_HOME                                                                                             \
    PM64_CAM_STAGE                                                                                            \
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)                                                                   \
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))                                            \
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(15), 0, 11, 0, -1)           \
    CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)

/* Lakilester's Spiny Flip. PM64: battle/partner/lakilester.c N(EVS_Move_SpinyFlip), N(SpinyFlipActionCommand),
 * N(EVS_ReturnHome_Success/_Miss). He flies (Run) 40 ahead and 20 up in 30 frames, cosine in-out, and idles; a
 * spiny appears 3 ahead of, 10 above and 5 in front of him and he lifts it (LiftSpiny), 6 higher a frame for 4
 * frames; energy gathers 8 ahead of and 40 above him, and he and the spiny tremble a unit side to side every
 * frame while you aim. PM64's aim: a reticle you steer with the stick, 30 frames to start then 80 (A throws
 * early), HoldSpiny with 60 left, StrainHoldSpiny with 30 left; on the enemy if it ends within 12 (by its size).
 * TTYD's: Flurrie's Body Slam sight (kind 8), steered with the stick onto the enemy for PM64's 80 aiming frames
 * after TTYD's own 20-frame slide-in (no early throw on A: the sight's result alone decides, the same on every
 * screen online), HoldSpiny 20 and StrainHoldSpiny 50 PM64 frames into it. He throws (ThrowSpiny), and 3 frames on the spiny
 * (SpinySpin) arcs onto the enemy in 20 frames (PM64's gravity 1.5: 75 high), bounces 50 on and down to the
 * floor in 15 (42 high) and is gone; 3 / 4 / 5 by rank on a hit aim, no contact. Off the enemy (or dodged) it
 * lands about the aim point and bounces, no damage. 15 frames on he flies home (Run, 15 frames) and floats. */
static EVT_BEGIN(sSpinyFlipEvt)
    PM64_BEGIN(PM64_ONE)
    CALL(lakilesterPlain, LW(12), 0)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(evt_btl_camera_set_posoffset, 0, 30, 0, 0)
    PM64_FACE_TARGET
    /* the fly-out */
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
    ADD(LW(1), 20)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    /* the camera takes in him and the enemy (PM64: 416 away over 30 frames) */
    CALL(evt_btl_camera_set_mode, 0, 11)
    CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 400)
    CALL(evt_btl_camera_set_posoffset, 0, 0, 0, 0)
    /* the spiny appears and he lifts it */
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 3)
    ADD(LW(1), 10)
    ADD(LW(2), 5)
    CALL(lakilesterSpinyAt, BTL_UNIT_SELF, 20, LW(0), LW(1), LW(2))
    WAIT_FRAMES(PM64_FRAMES(1))
    CALL(lakilesterAnim, BTL_UNIT_SELF, 15)
    /* (6 a PM64 frame: 3 a TTYD frame) */
    LOOP(PM64_FRAMES(4))
        ADD(LW(1), 3)
        CALL(lakilesterSpinyAt, BTL_UNIT_SELF, 20, LW(0), LW(1), LW(2))
        WAIT_FRAMES(1)
    END_LOOP
    CALL(pm64Fx, 1, BTL_UNIT_SELF, 40)
    /* the aim */
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcGetDifficulty, LW(0))
    CALL(btlevtcmd_AcSetParamAll, LW(3), LW(4), PM64_FRAMES(80), LW(0), EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 8, 1, 0)
    BEGIN_SIBLING_EVENT
        LOOP(45)
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 1, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
            CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, -1, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    END_SIBLING_EVENT
    WAIT_FRAMES(20) /* (TTYD's sight sliding in) */
    BEGIN_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(20))
        CALL(lakilesterAnim, BTL_UNIT_SELF, 16)
        WAIT_FRAMES(PM64_FRAMES(30))
        CALL(lakilesterAnim, BTL_UNIT_SELF, 17)
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_StopAC)
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    /* the throw */
    CALL(lakilesterAnim, BTL_UNIT_SELF, 18)
    WAIT_FRAMES(PM64_FRAMES(3))
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL(evt_snd_sfxon, STRING("SOUND_LAKILESTER_THROW_SPINY_A|SFX_AUDIENCE_THROW_OBJECT1"), 0)
    CALL(btlevtcmd_GetResultAC, LW(0))
    SET_VALUE(LW(15), 0)
    IF_BITS_SET(LW(0), 2)
        IF_INT_EQUAL(LW(5), 1)
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            CALL(lakilesterSpinyFly, BTL_UNIT_SELF, 21, LW(0), LW(1), LW(2), 20, 75, 0)
            BEGIN_SIBLING_EVENT
                CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
                CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 50)
                CALL(lakilesterSpinyFly, BTL_UNIT_SELF, -1, LW(0), 0, LW(2), 15, 42, 1)
            END_SIBLING_EVENT
            CALL_CHILD(sHitCheckedEvt)
            GOTO(90)
        END_IF
    END_IF
    /* off the enemy, or dodged: about the aim point, no damage */
    PM64_AVOID
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(lakilesterMissPos, LW(0), LW(1))
    CALL(lakilesterSpinyFly, BTL_UNIT_SELF, 21, LW(0), LW(1), LW(2), 20, 75, 0)
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 50)
        CALL(lakilesterSpinyFly, BTL_UNIT_SELF, -1, LW(0), 0, LW(2), 15, 42, 1)
    END_SIBLING_EVENT
    LABEL(90)
    IF_INT_EQUAL(LW(15), 0)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    WAIT_FRAMES(PM64_FRAMES(15))
    LAKI_HOME
    PM64_END
EVT_END()

/* Lakilester's Spiny Surge. PM64: battle/partner/lakilester.c N(EVS_Move_SpinySurge), N(ThrowSpinyFX),
 * N(GetSpinySurgeDamage), N(EVS_ReturnHome_Success/_Miss). He flies (Run) to 60 ahead of and 20 above his place
 * in 30 frames (cosine in-out), idles 10; then for 60 frames: tilt the stick left and he lifts a spiny over him
 * (LiftSpiny; it 3 ahead, 34 up, 5 in front), let go and he throws it (ThrowSpinyAlt; it flies off ahead), each
 * throw filling the meter (57 frames). PM64's own command is that tilt-and-let-go; TTYD's: Shell Slam's
 * let-go-of-left gauge (kind 6, input 7), 57 PM64 frames, gains for that span. Ours shows the lift first and a
 * throw each time the gauge rises (the command's own state, the same on every screen online; looks only), lifting
 * again 8 frames after. Then he idles, waits for the last throw (15) and 10 more; with no throw at all (ours: the
 * command's gauge at 0 after its result), 15 more and home. Otherwise every enemy, 6 frames apart, takes the
 * meter's damage by his rank (lakilesterSurgeDamage), no contact; 30 frames; home (Run 15 frames) and floats. */
static EVT_BEGIN(sSpinySurgeEvt)
    PM64_BEGIN(PM64_ALL)
    LAKI_AC_MASH(7, PM64_FRAMES(57), 23, 21, 20, 18, 16, 14, 13)
    CALL(pm64WeaponRank, LW(12), LW(0))
    SWITCH(LW(0))
        CASE_EQUAL(0)
        CALL(btlevtcmd_AcSetGaugeParam, 100, 100, 100, 100)
        CASE_EQUAL(1)
        CALL(btlevtcmd_AcSetGaugeParam, 70, 100, 100, 100)
        CASE_DEFAULT
        CALL(btlevtcmd_AcSetGaugeParam, 40, 70, 100, 100)
    END_SWITCH
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)
    PM64_CAM_STAGE
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 60)
    ADD(LW(1), 20)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(10))
    /* the first spiny lifted */
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(5), 3)
    ADD(LW(6), 34)
    ADD(LW(7), 5)
    CALL(lakilesterAnim, BTL_UNIT_SELF, 15)
    CALL(lakilesterSpinyAt, BTL_UNIT_SELF, 20, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_StartAC, 1)
    /* LW(9): the gauge last frame; LW(11): frames since the last throw (looks only: the command's gauge) */
    SET_VALUE(LW(9), 0)
    SET_VALUE(LW(11), 0)
    LOOP(PM64_FRAMES(60))
        CALL(btlevtcmd_AcGetOutputParam, 1, LW(0))
        IF_INT_GREATER(LW(0), LW(9))
            CALL(evt_snd_sfxon, STRING("SOUND_LAKILESTER_THROW_SPINY_B|SFX_AUDIENCE_THROW_OBJECT1"), 0)
            CALL(lakilesterAnim, BTL_UNIT_SELF, 19)
            BEGIN_SIBLING_EVENT
                CALL(lakilesterSpinyToss, BTL_UNIT_SELF)
            END_SIBLING_EVENT
            SET_VALUE(LW(11), 0)
        ELSE
            ADD(LW(11), 1)
            IF_INT_EQUAL(LW(11), PM64_FRAMES(8))
                CALL(lakilesterAnim, BTL_UNIT_SELF, 15)
            END_IF
        END_IF
        SET_VALUE(LW(9), LW(0))
        WAIT_FRAMES(1)
    END_LOOP
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    CALL(btlevtcmd_StopAC)
    CALL(lakilesterAnim, BTL_UNIT_SELF, 1)
    WAIT_FRAMES(PM64_FRAMES(15))
    WAIT_FRAMES(PM64_FRAMES(10))
    CALL(lakilesterSpinyAt, BTL_UNIT_SELF, -1, 0, 0, 0)
    IF_INT_LESS_OR_EQUAL(LW(8), 0)
        WAIT_FRAMES(PM64_FRAMES(15))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        GOTO(90)
    END_IF
    CALL(lakilesterSurgeDamage, LW(12), LW(8))
    CALL(lakilesterPlain, LW(12), 0)
    SET_VALUE(LW(11), 6)
    SET_VALUE(LW(13), -1)
    SET_VALUE(LW(14), 0)
    CALL_CHILD(sHitAllEvt)
    WAIT_FRAMES(PM64_FRAMES(30))
    LABEL(90)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LAKI_HOME
    PM64_END
EVT_END()

/* Lakilester's Hurricane. PM64: battle/partner/lakilester.c N(EVS_Move_Hurricane), N(InitHurricane),
 * N(ProcessHurricane), N(CanTargetBeBlown), N(BlowTargetAway), N(EVS_ReturnHome_Miss). He flies (Run) to 40 ahead
 * of and 20 above his place in 30 frames (cosine in-out), idles 15; then for 165 frames he breathes out and in
 * (HurricaneExhale / HurricaneInhale, PM64's own rhythm: lakilesterHurricane) and the enemies that can be blown
 * away spin, while you press A and B by turns (PM64's mash; TTYD's: Smooch's A-and-B gauge, kind 6 input 11, 147
 * frames as PM64's). 15 frames after, each enemy is blown away at the meter's chance (TTYD's: Gale Force's
 * rule, the meter as its chance, and its reaction carrying the enemy off), the rest unhurt; 34 frames; home
 * (Run 15 frames) and floats. */
static EVT_BEGIN(sHurricaneEvt)
    PM64_BEGIN(PM64_ALL)
    LAKI_AC_MASH(11, PM64_FRAMES(147), 14, 13, 12, 11, 10, 9, 9)
    CALL(btlevtcmd_AcSetGaugeParam, 100, 100, 100, 100)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 6, 1, 0)
    PM64_CAM_SELF
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 3, -1)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
    ADD(LW(1), 20)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(btlevtcmd_StartAC, 1)
    /* (PM64: the camera draws back to take in the stage over 150 frames) */
    PM64_CAM_STAGE
    CALL(evt_snd_sfxon, STRING("SOUND_LAKILESTER_HURRICANE_EXHALE|SFX_BTL_CLAUD_BREATH1"), 0)
    BEGIN_SIBLING_EVENT
        CALL(lakilesterHurricane, BTL_UNIT_SELF)
    END_SIBLING_EVENT
    WAIT_FRAMES(PM64_FRAMES(2))
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    CALL(btlevtcmd_StopAC)
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(pm64StatusWeapon, LW(12), 2, LW(8))
    CALL(lakilesterPlain, LW(12), 1)
    SET_VALUE(LW(11), 0)
    SET_VALUE(LW(13), -1)
    SET_VALUE(LW(14), 0)
    CALL_CHILD(sHitAllEvt)
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    WAIT_FRAMES(PM64_FRAMES(34))
    LAKI_HOME
    PM64_END
EVT_END()

/* Lakilester's Cloud Nine. PM64: battle/partner/lakilester.c N(EVS_Move_CloudNine), N(cloudNine_normal),
 * N(cloudNine_immobile), N(SpawnCloudNineFX), N(ApplyCloudNine). 10 frames in, he rises 40 (30 frames, cosine
 * in-out) and, 15 frames into that, turns round to Mario (18 degrees a frame for 10 frames); 10 frames; he puffs
 * up (CloudNineBegin, 5 frames, then CloudNineIdle) and 15 frames on Mario hops onto him (12 behind, 13 up, 5
 * back, 18 frames, gravity 1), stands 3 frames. If Mario can't move, Lakilester instead turns, puffs up (20
 * frames), slips under him (12 ahead, 7 down, 5 in front, 20 frames, a 10 dip) and lifts him 40 (20 frames).
 * PM64's command: three chances to press A (97 frames); TTYD's: Dodgy Fog's three signals (kind 18). Each one
 * hit is a turn of Cloud Nine (PM64: the result as turns, a 50% dodge: TTYD's Dodgy for that many turns; none,
 * nothing). He deflates (CloudNineEnd) in a burst of 10 cloud puffs about Mario (one a frame), Mario drops to
 * the floor (10 frames), Lakilester floats (Walk); 7 frames on Mario runs home (speed 8) while Lakilester sinks
 * 40 (30 frames; immobile: flies home, 20) and, 15 frames on, turns back (10 frames). Not here: PM64's cloud
 * around Mario (fx_ending_decals, growing in 5 frames): puffs instead; TTYD's Dodgy shows its own. */
static EVT_BEGIN(sCloudNineEvt)
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
    CALL(evt_btl_camera_set_zoom, 0, 300)
    CALL(btlevtcmd_CommandGetWeaponActionLv, LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcSetParamAll, 3, 0, 40, 0, 1, 1, 2, 8)
    CALL(btlevtcmd_AcSetFlag, 5)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 18, 1, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 1, -1)
    CALL(btlevtcmd_CheckActStatus, LW(3), LW(9))
    WAIT_FRAMES(PM64_FRAMES(10))
    IF_INT_EQUAL(LW(9), 1)
        /* Mario hops on */
        BEGIN_SIBLING_EVENT
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            ADD(LW(1), 40)
            CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
        END_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(15))
        SET_VALUE(LW(0), 0)
        LOOP(PM64_FRAMES(10))
            ADD(LW(0), 9)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
            WAIT_FRAMES(1)
        END_LOOP
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        WAIT_FRAMES(PM64_FRAMES(5))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
        WAIT_FRAMES(PM64_FRAMES(15))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 12)
        ADD(LW(1), 13)
        SUBTRACT(LW(2), 5)
        CALL(btlevtcmd_AnimeChangePose, LW(3), 1, STRING("M_J_1B"))
        CALL(btlevtcmd_SetFallAccel, LW(3), PM64_ACCEL(1.0f))
        CALL(btlevtcmd_JumpPosition, LW(3), LW(0), LW(1), LW(2), PM64_FRAMES(18), -1)
        CALL(btlevtcmd_AnimeChangePoseType, LW(3), 1, BTL_POSE_STAY)
        WAIT_FRAMES(PM64_FRAMES(3))
    ELSE
        /* Mario can't move: Lakilester slips under him and lifts him */
        SET_VALUE(LW(0), 0)
        LOOP(PM64_FRAMES(10))
            ADD(LW(0), 9)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
            WAIT_FRAMES(1)
        END_LOOP
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 5, -1)
        WAIT_FRAMES(PM64_FRAMES(20))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 12)
        SUBTRACT(LW(1), 7)
        ADD(LW(2), 5)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -10, 11, 0, -1)
        BEGIN_SIBLING_EVENT
            CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
            ADD(LW(1), 40)
            CALL(btlevtcmd_DivePosition, LW(3), LW(0), LW(1), LW(2), PM64_FRAMES(20), 0, 11, 0, -1)
        END_SIBLING_EVENT
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(1), 40)
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), 0, 11, 0, -1)
    END_IF
    /* the three chances */
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_AcGetOutputParam, 0, LW(10))
    CALL(btlevtcmd_StopAC)
    WAIT_FRAMES(PM64_FRAMES(3))
    IF_INT_GREATER(LW(10), 0)
        CALL(evt_snd_sfxon, STRING("SFX_BTL_CLAUD_HIDE2"), 0)
        CALL(pm64Fx, 3, LW(3), 0)
        CALL(pm64Fx, 3, LW(3), 20)
        CALL(lakilesterCloudNine, LW(12), LW(10))
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        SET_VALUE(LW(0), LW(10))
        SUBTRACT(LW(0), 1)
        CALL(btlevtcmd_GetResultPrizeLv, BTL_UNIT_SELF, LW(0), LW(6))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    /* down again */
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    BEGIN_SIBLING_EVENT
        LOOP(10)
            CALL(lakilesterPuff, LW(3), 15)
            WAIT_FRAMES(PM64_FRAMES(1))
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetHomePos, LW(3), EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_DivePosition, LW(3), LW(0), LW(1), LW(2), PM64_FRAMES(10), 0, 13, 0, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 2, -1)
    CALL(btlevtcmd_AnimeChangePoseType, LW(3), 1, BTL_POSE_STAY)
    WAIT_FRAMES(PM64_FRAMES(7))
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_AnimeChangePoseType, LW(3), 1, BTL_POSE_RUN)
        CALL(btlevtcmd_SetMoveSpeed, LW(3), PM64_SPEED(8.0f))
        CALL(btlevtcmd_GetHomePos, LW(3), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_MovePosition, LW(3), LW(0), LW(1), LW(2), 0, -1, 0)
        CALL(btlevtcmd_AnimeChangePoseType, LW(3), 1, BTL_POSE_STAY)
    END_SIBLING_EVENT
    IF_INT_EQUAL(LW(9), 1)
        BEGIN_SIBLING_EVENT
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            SUBTRACT(LW(1), 40)
            CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(30), 0, 11, 0, -1)
        END_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(15))
    ELSE
        BEGIN_SIBLING_EVENT
            CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), 0, 11, 0, -1)
        END_SIBLING_EVENT
    END_IF
    SET_VALUE(LW(0), 180)
    LOOP(PM64_FRAMES(10))
        SUBTRACT(LW(0), 9)
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, LW(0), 0)
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    WAIT_FRAMES(PM64_FRAMES(10))
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(99)
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, -1, -1)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()
