/* Kooper's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/kooper.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include.
 *
 * Kooper's battle sprite rows (BattleKooper in npc_sprite_names.yaml): Idle 4, Run 6, ShellStill 9,
 * ShellSpinSlowest 10, ShellSpinSlower 11, ShellSpinFaster 12, ShellSpinFastest 13, EnterShell 14, ExitShell 15.
 *
 * (A sibling event ends with the event that started it: the shell's trip runs as a sibling of the script that
 * waits for it to finish.) */

/* Kooper's own state between a move's script and its sibling events (siblings get copies of LW):
 * slot 0 the shell's trip is over (PM64's actor var 0), slot 1 how long the stick has been held.
 * kooperSet(slot, value), kooperGet(slot, out). */
static int sKooperState[2];

static USER_FUNC(kooperSet) {
    const int slot = evtGetValue(event, event->args[0]);
    if (slot >= 0 && slot < 2) {
        sKooperState[slot] = evtGetValue(event, event->args[1]);
    }
    return EVT_RETURN_DONE;
}

static USER_FUNC(kooperGet) {
    const int slot = evtGetValue(event, event->args[0]);
    evtSetValue(event, event->args[1], slot >= 0 && slot < 2 ? sKooperState[slot] : 0);
    return EVT_RETURN_DONE;
}

static BattleUnit* kooperSelf(EventEntry* event) {
    return BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, BTL_UNIT_SELF));
}

/* kooperFrontOf(x): x := x, or his own x when x is behind him (PM64 runs him to 30 past Mario: from the front
 * row, where TTYD can put him, he stays where he is rather than run back) */
static USER_FUNC(kooperFrontOf) {
    const BattleUnit* unit = kooperSelf(event);
    const int x = evtGetValue(event, event->args[0]);
    if (unit != NULL && ((float)x - unit->position.x) * (unit->faceDirection < 0 ? -1.0f : 1.0f) < 0.0f) {
        evtSetValue(event, event->args[0], (int)unit->position.x);
    }
    return EVT_RETURN_DONE;
}

/* kooperPassed(x, out): out := 1 once the shell has reached x going his way (or its trip is over), else 0. PM64:
 * the moves' wait for the shell to pass each target before testing it */
static USER_FUNC(kooperPassed) {
    const BattleUnit* unit = kooperSelf(event);
    const float x = (float)evtGetValue(event, event->args[0]);
    int passed = 1;
    if (unit != NULL && !sKooperState[0]) {
        passed = (unit->position.x - x) * (unit->faceDirection < 0 ? -1.0f : 1.0f) >= 0.0f;
    }
    evtSetValue(event, event->args[1], passed);
    return EVT_RETURN_DONE;
}

/* kooperSlowDown(speed, TTYD's a frame): the shell slides on his way, a third slower each PM64 frame (each of
 * TTYD's: by 1.5's square root), until under 1 a PM64 frame. PM64: kooper.c N(SlowDown) (the speed it went at,
 * divided by 1.5 each frame) */
static float sKooperSlow;
static USER_FUNC(kooperSlowDown) {
    BattleUnit* unit = kooperSelf(event);
    if (unit == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        sKooperSlow = evtGetFloat(event, event->args[0]);
    }
    BtlUnit_SetPos(unit, unit->position.x + sKooperSlow * (unit->faceDirection < 0 ? -1.0f : 1.0f), unit->position.y,
                   unit->position.z);
    sKooperSlow /= 1.2247449f;
    return sKooperSlow < PM64_PER_FRAME(1.0f) ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* kooperDizzyWeapon(weapon, fill 0-100): weapon := a copy of it with Dizzy Shell's chance in it. PM64: kooper.c
 * N(dizzyShell): dizzy 3 turns at the meter's chance, certain (resistance ignored: TTYD's status-vulnerability
 * flag; an immune enemy stays immune) when the meter is full */
static USER_FUNC(kooperDizzyWeapon) {
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    const int fill = evtGetValue(event, event->args[1]);
    const int v = fill < 0 ? 0 : fill > 100 ? 100 : fill;
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    sCopy.dizzyChance = (u8)v;
    sCopy.dizzyTime = 3;
    if (v >= 100) {
        sCopy.special |= SPECIAL_IGNORE_STATUS_VULNERABILITY;
    } else {
        sCopy.special &= ~SPECIAL_IGNORE_STATUS_VULNERABILITY;
    }
    if (pm64Log()) {
        printf("[pm64] Dizzy Shell: chance %d, 3 turns\n", v);
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* kooperFireDamage(weapon, fill 0-100, speed out): weapon := a copy of it with Fire Shell's damage by the meter's
 * band in it (to 35, 60, 80, 99, full: 1, 3, 4, 5, 6, every rank; not through defense, as PM64's, where Shell
 * Slam's goes through) and the shell's speed (10, 16 when full). PM64: kooper.c N(GetFireShellSpeedAndDamage)
 * and N(fireShell). (A copy: the worn weapon itself is made again every frame, portPm64BattleCosts, and the hits
 * come later.) */
static USER_FUNC(kooperFireDamage) {
    static const signed char kBands[5] = {1, 3, 4, 5, 6};
    static BattleWeapon sCopy;
    const BattleWeapon* src = (const BattleWeapon*)evtGetValue(event, event->args[0]);
    const int fill = evtGetValue(event, event->args[1]);
    const int band = fill <= 35 ? 0 : fill <= 60 ? 1 : fill <= 80 ? 2 : fill < 100 ? 3 : 4;
    int i;
    evtSetValue(event, event->args[2], band == 4 ? 8 : 5); /* (PM64's 16 and 10 a frame, as TTYD's a frame) */
    if (src == NULL) {
        return EVT_RETURN_DONE;
    }
    if (src != &sCopy) {
        memcpy(&sCopy, src, sizeof(sCopy));
    }
    sCopy.hpDamageFn = weaponGetPowerFromPartyAttackLv;
    for (i = 0; i < 6; i++) {
        sCopy.hpDamageWork[i] = (u32)kBands[band];
    }
    sCopy.special &= ~SPECIAL_PIERCES_DEFENSE;
    if (pm64Log()) {
        printf("[pm64] Fire Shell: meter %d, damage %d\n", fill, kBands[band]);
        fflush(stdout);
    }
    evtSetValue(event, event->args[0], (EvtWord)&sCopy);
    return EVT_RETURN_DONE;
}

/* The start every move of his shares. PM64: kooper.c N(shellToss) / N(powerShell) / N(dizzyShell) /
 * N(fireShell), their first lines: he runs (Run, speed 4) to 30 past Mario, stands (Idle) and tucks into his
 * shell (EnterShell); the move's command is set up there (TTYD's slides in meanwhile), 15 frames, then he
 * spins (ShellSpinSlowest). LW(10): the command (0 hold left, 1 mash). */
static EVT_BEGIN(sKooperIntoShellEvt)
    PM64_CAM_SELF
    CALL(pm64SetAnim, BTL_UNIT_SELF, 6, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_MARIO, LW(0), EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 30)
    CALL(kooperFrontOf, LW(0))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), LW(2))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 14, -1)
    IF_INT_EQUAL(LW(10), 0)
        PM64_AC_HOLD_LEFT
    ELSE
        PM64_AC_MASH
    END_IF
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
    RETURN
EVT_END()

/* Shell Toss's and Power Shell's charge. PM64: kooper.c N(shellToss) / N(powerShell): the hammer command (hold
 * the stick left, let go when the light comes on; TTYD's: Shell Toss's own hold-left gauge), the shell spinning
 * faster the longer it is held (ShellSpinSlowest, at 20 PM64 frames Slower, 30 Faster, 40 Fastest, a puff of smoke
 * at 20, 30, 40 and 50); let go, its speed by how long it was held: 10, 12, 14, 16 (to 20, 30, 40, 50 frames), 8
 * after that. Ours: the spin-up while held is looks only (each screen times it from the command's held flag); the
 * speed and row after it come from the command's result alone (online: synced through ResultAC): done right 16
 * and Fastest (PM64's light comes on in its last band), not 10 and Slowest (PM64's early let-go; its held-too-long
 * 8 is not told apart). Out: LW(10) the speed, TTYD's a frame (PM64's halved). */
static EVT_BEGIN(sKooperChargeEvt)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK1"), LW(13))
    BEGIN_SIBLING_EVENT_GET_ID(LW(11))
        LABEL(1)
        CALL(btlevtcmd_AcGetOutputParam, 0, LW(0))
        IF_INT_EQUAL(LW(0), 0)
            WAIT_FRAMES(1)
            GOTO(1)
        END_IF
        SET_VALUE(LW(6), 0)
        LOOP(0)
            WAIT_FRAMES(1)
            IF_INT_EQUAL(LW(6), PM64_FRAMES(20))
                CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
            END_IF
            IF_INT_EQUAL(LW(6), PM64_FRAMES(30))
                CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
            END_IF
            IF_INT_EQUAL(LW(6), PM64_FRAMES(40))
                CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
            END_IF
            IF_INT_EQUAL(LW(6), PM64_FRAMES(50))
                CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
            END_IF
            IF_INT_LESS(LW(6), PM64_FRAMES(20))
                CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
            ELSE
                IF_INT_LESS(LW(6), PM64_FRAMES(30))
                    CALL(pm64SetAnim, BTL_UNIT_SELF, 11, -1)
                ELSE
                    IF_INT_LESS(LW(6), PM64_FRAMES(40))
                        CALL(pm64SetAnim, BTL_UNIT_SELF, 12, -1)
                    ELSE
                        CALL(pm64SetAnim, BTL_UNIT_SELF, 13, -1)
                    END_IF
                END_IF
            END_IF
            ADD(LW(6), 1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    DELETE_EVENT(LW(11))
    CALL(evt_snd_sfxoff, LW(13))
    CALL(btlevtcmd_GetResultAC, LW(0))
    IF_BITS_SET(LW(0), 2)
        SET_VALUE(LW(10), 8) /* (PM64's 16) */
        CALL(pm64SetAnim, BTL_UNIT_SELF, 13, -1)
    ELSE
        SET_VALUE(LW(10), 5) /* (PM64's 10) */
        CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
    END_IF
    RETURN
EVT_END()

/* Dizzy Shell's and Fire Shell's charge: TTYD's mash gauge (Bob-ombast's) for PM64's stick-left mash meter (its
 * 72 frames: TTYD's own length), the shell spinning faster as the meter fills. LW(7): 0 Dizzy Shell (the
 * whirlwind round the shell), 1 Fire Shell (flames on it). Out: LW(8) the meter (0-100). */
static EVT_BEGIN(sKooperMashEvt)
    IF_INT_EQUAL(LW(7), 0)
        CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK1"), LW(13))
        CALL(pm64Fx, 9, BTL_UNIT_SELF, 0)
    ELSE
        CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_IPPATSU1"), LW(13))
    END_IF
    BEGIN_SIBLING_EVENT_GET_ID(LW(11))
        SET_VALUE(LW(6), 0)
        LOOP(0)
            CALL(btlevtcmd_AcGetOutputParam, 1, LW(0))
            IF_INT_EQUAL(LW(7), 0)
                /* N(dizzyShell): to 20 Slowest, 40 Slower, 60 Faster, then Fastest */
                IF_INT_LESS(LW(0), 20)
                    CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
                ELSE
                    IF_INT_LESS(LW(0), 40)
                        CALL(pm64SetAnim, BTL_UNIT_SELF, 11, -1)
                    ELSE
                        IF_INT_LESS(LW(0), 60)
                            CALL(pm64SetAnim, BTL_UNIT_SELF, 12, -1)
                        ELSE
                            CALL(pm64SetAnim, BTL_UNIT_SELF, 13, -1)
                        END_IF
                    END_IF
                END_IF
                /* (the whirlwind lasts 60 PM64 frames: another while it spins) */
                SET_VALUE(LW(1), LW(6))
                MODULO(LW(1), PM64_FRAMES(55))
                IF_INT_EQUAL(LW(1), PM64_FRAMES(55) - 1)
                    CALL(pm64Fx, 9, BTL_UNIT_SELF, 0)
                END_IF
            ELSE
                /* N(fireShell): from 80 Fastest, 60 Faster, 40 Slower, else Slowest; the flames on it */
                IF_INT_GREATER_OR_EQUAL(LW(0), 80)
                    CALL(pm64SetAnim, BTL_UNIT_SELF, 13, -1)
                ELSE
                    IF_INT_GREATER_OR_EQUAL(LW(0), 60)
                        CALL(pm64SetAnim, BTL_UNIT_SELF, 12, -1)
                    ELSE
                        IF_INT_GREATER_OR_EQUAL(LW(0), 40)
                            CALL(pm64SetAnim, BTL_UNIT_SELF, 11, -1)
                        ELSE
                            CALL(pm64SetAnim, BTL_UNIT_SELF, 10, -1)
                        END_IF
                    END_IF
                END_IF
                SET_VALUE(LW(1), LW(6))
                MODULO(LW(1), PM64_FRAMES(4))
                IF_INT_EQUAL(LW(1), 0)
                    CALL(pm64Fx, 10, BTL_UNIT_SELF, 8)
                END_IF
            END_IF
            /* (kooper.c dizzyShell / fireShell: EFFECT_SMOKE_IMPACT puffs off the spinning shell 15, 35, 55 and 65
               frames into the mash) */
            SWITCH(LW(6))
                CASE_OR_EQUAL(PM64_FRAMES(15))
                CASE_OR_EQUAL(PM64_FRAMES(35))
                CASE_OR_EQUAL(PM64_FRAMES(55))
                CASE_OR_EQUAL(PM64_FRAMES(65))
                    CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
                CASE_END
            END_SWITCH
            ADD(LW(6), 1)
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    DELETE_EVENT(LW(11))
    CALL(evt_snd_sfxoff, LW(13))
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    CALL(btlevtcmd_StopAC)
    RETURN
EVT_END()

/* Back off the enemy he hit, still spinning. PM64: kooper.c N(EVS_ReturnHome_Success) (LW(7) 0: 30 back in 10
 * frames at jump gravity 1.1, 20 in 8, 10 in 6, a 4-frame hop in place) and N(EVS_ReturnHome_Miss) (LW(7) 1: 60
 * in 20 frames at gravity 1.4, 30 in 8, 20 in 6, a 4-frame hop); then out of his shell (ExitShell), 4 frames, and
 * he runs home (Run, speed 7). */
static EVT_BEGIN(sKooperBackEvt)
    PM64_CAM_STAGE
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), EVT_NULLPTR, LW(2))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
    IF_INT_EQUAL(LW(7), 0)
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.1f))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 30)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), -1)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(8), -1)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 10)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(6), -1)
    ELSE
        CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(1.4f))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 60)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 30)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(8), -1)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(6), -1)
    END_IF
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(4), -1)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 15, -1)
    WAIT_FRAMES(PM64_FRAMES(4))
    PM64_HOME(6, 7.0f)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    RETURN
EVT_END()

/* Kooper's Shell Toss. PM64: battle/partner/kooper.c N(shellToss), N(getShellTossMoveTime), N(SlowDown),
 * N(EVS_ReturnHome_Success/_Miss). Into his shell 30 past Mario (sKooperIntoShellEvt), the hammer command's charge
 * (sKooperChargeEvt), then the shell shoots along the ground into the front enemy at its speed (10-16), a smoke
 * trail behind it. 1 / 2 / 3, or 2 / 3 / 5 done right (the row). Hit: done right he bounces back the long way
 * (N(EVS_ReturnHome_Miss), PM64's own pairing), else the short one; out of the shell, runs home. Missed: the shell
 * slides on, slowing a third a frame (N(SlowDown)); 4 frames on it stops (ShellStill), tips back 30 then 60
 * degrees (2 frames, about 8 to its side), 5 frames, a puff of smoke, rocks back level in 5 (12 a frame), wobbles
 * 2 / -2 / 0 (2 frames each), 10 frames, out of the shell (ExitShell), 10 frames, runs home (Run, speed 7). */
static EVT_BEGIN(sShellTossEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_FACE_TARGET
    SET_VALUE(LW(10), 0)
    CALL_CHILD(sKooperIntoShellEvt)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL_CHILD(sKooperChargeEvt)
    PM64_CAM_TARGET
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK2"), 0)
    BEGIN_SIBLING_EVENT_GET_ID(LW(11))
        LOOP(0)
            CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
            WAIT_FRAMES(PM64_FRAMES(3))
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), EVT_NULLPTR, LW(2))
    ADD(LW(2), 5)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, LW(10))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        BEGIN_SIBLING_EVENT
            CALL(kooperSlowDown, LW(10))
            DELETE_EVENT(LW(11))
        END_SIBLING_EVENT
        WAIT_FRAMES(PM64_FRAMES(4))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 9, -1)
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 8, 0, 0)
        SET_VALUE(LW(0), 0)
        LOOP(PM64_FRAMES(2)) /* (-30 a PM64 frame: -15 a TTYD frame) */
            ADD(LW(0), -15)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(0))
            WAIT_FRAMES(1)
        END_LOOP
        WAIT_FRAMES(PM64_FRAMES(5))
        CALL(pm64Fx, 3, BTL_UNIT_SELF, 0)
        LOOP(PM64_FRAMES(5)) /* (12 a PM64 frame: 6 a TTYD frame) */
            ADD(LW(0), 6)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(0))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetRotateOffset, BTL_UNIT_SELF, 0, 0, 0)
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 2)
        WAIT_FRAMES(PM64_FRAMES(2))
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, -2)
        WAIT_FRAMES(PM64_FRAMES(2))
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(pm64SetAnim, BTL_UNIT_SELF, 15, -1)
        WAIT_FRAMES(PM64_FRAMES(10))
        CALL(evt_audience_ap_recovery)
        CALL(btlevtcmd_InviteApInfoReport)
        PM64_HOME(6, 7.0f)
        GOTO(99)
    END_IF
    DELETE_EVENT(LW(11))
    SET_VALUE(LW(15), 0)
    CALL_CHILD(sHitCheckedEvt)
    SET_VALUE(LW(7), 0)
    IF_INT_GREATER(LW(15), 0)
        SET_VALUE(LW(7), 1)
    END_IF
    PM64_ONE_DONE
    CALL_CHILD(sKooperBackEvt)
    PM64_END
EVT_END()

/* The shell's run through the enemies on the ground (LW(14): 0 Power Shell, 1 Dizzy Shell, 2 Fire Shell; LW(10)
 * its speed). The trip, a sibling: it shoots on (Power Shell 260, the others 350) with its trail (smoke; Fire
 * Shell's fire); Dizzy and Fire Shell's then go off the stage and, 20 frames on, come back in from the far side
 * (x -200) to 40 ahead of his place (speed 12, quartic out); then slot 0 is set. Meanwhile each target is hit as
 * the shell reaches it (kooperPassed); Dizzy Shell's each spin round from then till he is back (N(SetTargetsYaw):
 * 33 degrees a frame). Returns when the trip is over.
 * (The trip is a script of its own, sKooperTripEvt, called from the sibling: its trail is a sibling of its own, and
 * TTYD's sibling block ends at the first END_SIBLING_EVENT after it, nesting or not. Written inline, the trail's
 * END closed the trip's block: the sweep made the trip itself, then reached the trip's END_SIBLING_EVENT, which
 * deleted it before any hit; the shell passed through every enemy unharmed.) */
static EVT_BEGIN(sKooperTripEvt)
    BEGIN_SIBLING_EVENT_GET_ID(LW(11))
        LOOP(0)
            IF_INT_EQUAL(LW(14), 2)
                CALL(pm64Fx, 10, BTL_UNIT_SELF, 6)
                WAIT_FRAMES(PM64_FRAMES(2))
            ELSE
                CALL(pm64Fx, 3, BTL_UNIT_SELF, 4)
                WAIT_FRAMES(PM64_FRAMES(3))
            END_IF
        END_LOOP
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    IF_INT_EQUAL(LW(14), 0)
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 260)
    ELSE
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 350)
    END_IF
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, LW(10))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    DELETE_EVENT(LW(11))
    IF_INT_NOT_EQUAL(LW(14), 0)
        WAIT_FRAMES(PM64_FRAMES(20))
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        SET_VALUE(LW(6), 0)
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(6), 200)
        CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(6), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 40)
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(12.0f))
        CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, 0, 6, 0, -1)
    END_IF
    CALL(kooperSet, 0, 1)
    RETURN
EVT_END()

static EVT_BEGIN(sKooperSweepEvt)
    CALL(kooperSet, 0, 0)
    BEGIN_SIBLING_EVENT
        CALL_CHILD(sKooperTripEvt)
    END_SIBLING_EVENT
    SET_VALUE(LW(15), 0)
    LABEL(2)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(9), EVT_NULLPTR, EVT_NULLPTR)
    LABEL(3)
    WAIT_FRAMES(1)
    CALL(kooperPassed, LW(9), LW(6))
    IF_INT_EQUAL(LW(6), 0)
        GOTO(3)
    END_IF
    IF_INT_EQUAL(LW(14), 1)
        BEGIN_SIBLING_EVENT
            /* (33 a PM64 frame: 16 and 17 a TTYD frame) */
            SET_VALUE(LW(1), 16)
            LABEL(4)
            CALL(kooperGet, 0, LW(0))
            IF_INT_EQUAL(LW(0), 0)
                CALL(btlevtcmd_AddRotate, LW(3), 0, LW(1), 0)
                SUBTRACT(LW(1), 33)
                MULTIPLY(LW(1), -1)
                WAIT_FRAMES(1)
                GOTO(4)
            END_IF
            CALL(btlevtcmd_SetRotate, LW(3), 0, 0, 0)
        END_SIBLING_EVENT
    END_IF
    CALL_CHILD(sHitEvt)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(2)
    END_IF
    IF_INT_EQUAL(LW(15), 0)
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    LABEL(5)
    CALL(kooperGet, 0, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        WAIT_FRAMES(1)
        GOTO(5)
    END_IF
    /* (two frames for the spinning ones to stand still again before this, their starter, ends) */
    WAIT_FRAMES(2)
    RETURN
EVT_END()

/* Kooper's Power Shell. PM64: battle/partner/kooper.c N(powerShell), N(EVS_ReturnHome_Success/_Miss). Into his
 * shell 30 past Mario, the hammer command's charge as Shell Toss's (sKooperChargeEvt; PM64's command 47 frames),
 * then the shell shoots 260 on along the ground at its speed (10-16), a smoke trail behind, through every enemy
 * on the ground as it reaches each: 1, or 2 / 3 / 4 by rank done right (the row). Then back the short way
 * (N(EVS_ReturnHome_Success)) when done right, the long way (_Miss) when not; out of the shell, runs home. */
static EVT_BEGIN(sPowerShellEvt)
    PM64_BEGIN(PM64_ALL)
    SET_VALUE(LW(10), 0)
    CALL_CHILD(sKooperIntoShellEvt)
    CALL_CHILD(sKooperChargeEvt)
    CALL(btlevtcmd_StopAC)
    PM64_CAM_STAGE
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_SHUBIBIN2"), 0)
    SET_VALUE(LW(14), 0)
    CALL_CHILD(sKooperSweepEvt)
    CALL(btlevtcmd_GetResultAC, LW(0))
    SET_VALUE(LW(7), 1)
    IF_BITS_SET(LW(0), 2)
        SET_VALUE(LW(7), 0)
    END_IF
    CALL_CHILD(sKooperBackEvt)
    PM64_END
EVT_END()

/* The end of Dizzy Shell and Fire Shell. PM64: N(dizzyShell) / N(fireShell), last lines: 15 frames, out of the
 * shell (ExitShell), 15, stands (Idle) and runs home (Run, speed 7). */
static EVT_BEGIN(sKooperOutEvt)
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 15, -1)
    WAIT_FRAMES(PM64_FRAMES(15))
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    PM64_HOME(6, 7.0f)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    RETURN
EVT_END()

/* Kooper's Dizzy Shell. PM64: battle/partner/kooper.c N(dizzyShell), N(AverageTargetDizzyChance) (the command's
 * difficulty only), N(SetTargetsYaw). Into his shell 30 past Mario; the mash meter (72 frames; TTYD's: Bob-ombast's
 * gauge) while the shell spins in a whirlwind, faster as it fills (to 20 Slowest, 40 Slower, 60 Faster, then
 * Fastest), its speed by the same bands: 10, 12, 14, 16. It shoots 350 on and off the stage (a smoke trail),
 * every enemy on the ground it passes spinning round; no damage: dizzy 3 turns at the meter's chance, certain when
 * full (kooperDizzyWeapon; PM64's failed command: never, ours an empty meter). It comes back in from the far
 * side to 40 ahead of his place; out of the shell, home. */
static EVT_BEGIN(sDizzyShellEvt)
    PM64_BEGIN(PM64_ALL)
    SET_VALUE(LW(10), 1)
    CALL_CHILD(sKooperIntoShellEvt)
    SET_VALUE(LW(7), 0)
    CALL_CHILD(sKooperMashEvt)
    CALL(kooperDizzyWeapon, LW(12), LW(8))
    /* (PM64's 10, 12, 14, 16 a frame, as TTYD's a frame) */
    IF_INT_LESS(LW(8), 20)
        SET_VALUE(LW(10), 5)
    ELSE
        IF_INT_LESS(LW(8), 40)
            SET_VALUE(LW(10), 6)
        ELSE
            IF_INT_LESS(LW(8), 60)
                SET_VALUE(LW(10), 7)
            ELSE
                SET_VALUE(LW(10), 8)
            END_IF
        END_IF
    END_IF
    PM64_CAM_STAGE
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_ATTACK2"), 0)
    SET_VALUE(LW(14), 1)
    CALL_CHILD(sKooperSweepEvt)
    CALL_CHILD(sKooperOutEvt)
    PM64_END
EVT_END()

/* Kooper's Fire Shell. PM64: battle/partner/kooper.c N(fireShell), N(GetFireShellSpeedAndDamage). Into his
 * shell 30 past Mario; the mash meter (72 frames; TTYD's: Bob-ombast's gauge), flames on the shell (PM64's red
 * flames growing and his palette reddening by the meter: ours the flame effect, no palette), spinning faster as
 * it fills (from 40 Slower, 60 Faster, 80 Fastest). Damage by the meter's band, every rank: to 35 1, 60 3, 80 4,
 * 99 5, full 6 (kooperFireDamage); the shell's speed 10, 16 full. It shoots 350 on and off the stage trailing fire,
 * a fire hit on every enemy on the ground it reaches; back in from the far side to 40 ahead of his place; out of
 * the shell, home. */
static EVT_BEGIN(sFireShellEvt)
    PM64_BEGIN(PM64_ALL)
    SET_VALUE(LW(10), 1)
    CALL_CHILD(sKooperIntoShellEvt)
    SET_VALUE(LW(7), 1)
    CALL_CHILD(sKooperMashEvt)
    CALL(kooperFireDamage, LW(12), LW(8), LW(10))
    PM64_CAM_STAGE
    CALL(evt_snd_sfxon, STRING("SFX_BTL_NOKO_IPPATSU2"), 0)
    SET_VALUE(LW(14), 2)
    CALL_CHILD(sKooperSweepEvt)
    CALL_CHILD(sKooperOutEvt)
    PM64_END
EVT_END()
