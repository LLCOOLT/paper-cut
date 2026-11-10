/* Bombette's battle moves as Paper Mario 64's (pm64_partner_pc.c includes this).
 * Each script: PM64's choreography re-implemented from the decompilation's
 * src/battle/partner/bombette.c (cited per move, never copied), TTYD's action commands and damage rules;
 * its row in pm64_partner_pc.c's sMoves. The shared building blocks (PM64_BEGIN, PM64_AC_*, sHitEvt,
 * sHitAllEvt, pm64SetAnim, pm64Fx, pm64Beam, ...) are defined above the include. */

/* pm64BombDamage(weapon, fill 0-100): Bombette's Bomb, Power Bomb and Mega Bomb. PM64: bombette.c
 * N(GetBombDamage) / N(GetPowerBombDamage) / N(GetMegaBombDamage): the meter's bands (to 35, 60, 80, 99, full) by
 * her rank, the same three rows for all three moves (Power Bomb from Super, Mega Bomb at Ultra). */
static USER_FUNC(pm64BombDamage) {
    static const signed char kBands[3][5] = {{1, 2, 3, 4, 5}, {1, 3, 4, 5, 6}, {1, 3, 5, 6, 7}};
    BattleWeapon* w = (BattleWeapon*)evtGetValue(event, event->args[0]);
    const int fill = evtGetValue(event, event->args[1]);
    const int band = fill <= 35 ? 0 : fill <= 60 ? 1 : fill <= 80 ? 2 : fill <= 99 ? 3 : 4;
    int rank, i;
    if (w == NULL || w->hpDamageFn != weaponGetPowerFromPartyAttackLv) {
        return EVT_RETURN_DONE;
    }
    rank = weaponRank(w);
    for (i = 0; i < 6; i++) {
        w->hpDamageWork[i] = (u32)kBands[rank < 0 ? 0 : rank > 2 ? 2 : rank][band];
    }
    pm64HoldDamage(event, w);
    return EVT_RETURN_DONE;
}

/* Bombette's Bomb, Power Bomb and Mega Bomb (LW(10): 0, 1, 2). PM64: battle/partner/bombette.c
 * N(EVS_Attack_Bomb). The fuse lit, she runs (RunLit) to 20 short of the enemy (Power and Mega Bomb: 40) in 45
 * frames while you mash (TTYD's: Bob-ombast's gauge for PM64's mash meter), stands about to blow
 * (AboutToExplode) and shakes half a unit side to side, 7 / 12 / 20 times 2 frames; she blows up (PM64's
 * explosion, the bigger one when the meter is full; Mega Bomb's a size up), the camera shaking, and the damage
 * is the meter's band by her rank (pm64BombDamage): Bomb one enemy, Power Bomb every enemy at once, Mega Bomb each
 * 5 frames apart; no contact. Then, spinning (33 degrees a frame for 80 frames, Aftermath), she is blown 50 back
 * and 200 up in 10 frames, gone 50 frames, and drops in from 25 ahead of and 200 above her place in 20 frames. */
static EVT_BEGIN(sBombBodyEvt)
    PM64_BEGIN(IF_INT_EQUAL(LW(10), 0) PM64_ONE ELSE PM64_ALL END_IF)
    PM64_AC_MASH
    WAIT_FRAMES(PM64_FRAMES(10))
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK2"), LW(14))
    BEGIN_SIBLING_EVENT
        CALL(pm64SetAnim, BTL_UNIT_SELF, 9, -1)
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        IF_INT_EQUAL(LW(10), 0)
            CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 20)
        ELSE
            CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 40)
        END_IF
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(45), -1, 0)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 26, -1)
    END_SIBLING_EVENT
    CALL(btlevtcmd_StartAC, 1)
    WAIT_FRAMES(PM64_FRAMES(45))
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_StopAC)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 26, -1)
    IF_INT_EQUAL(LW(10), 0)
        SET_VALUE(LW(0), 7)
    ELSE
        IF_INT_EQUAL(LW(10), 1)
            SET_VALUE(LW(0), 12)
        ELSE
            SET_VALUE(LW(0), 20)
        END_IF
    END_IF
    LOOP(LW(0))
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, FLOAT(0.5f), 0, 0)
        WAIT_FRAMES(PM64_FRAMES(1))
        CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, FLOAT(-0.5f), 0, 0)
        WAIT_FRAMES(PM64_FRAMES(1))
    END_LOOP
    CALL(btlevtcmd_SetDispOffset, BTL_UNIT_SELF, 0, 0, 0)
    CALL(evt_snd_sfxoff, LW(14))
    PM64_CAM_STAGE
    CALL(btlevtcmd_AcGetOutputParam, 1, LW(8))
    CALL(pm64BombDamage, LW(12), LW(8))
    /* the blast: 15 small, 16 big (the meter full), Mega Bomb a size up */
    SET_VALUE(LW(0), 15)
    IF_INT_GREATER(LW(8), 99)
        ADD(LW(0), 1)
    END_IF
    IF_INT_EQUAL(LW(10), 2)
        ADD(LW(0), 1)
    END_IF
    CALL(pm64Fx, LW(0), BTL_UNIT_SELF, 20)
    CALL(pm64Fx, 3, BTL_UNIT_SELF, 0) /* (bombette.c PlayExplosionFX: fx_smoke_ring at her feet) */
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_ATTACK3"), 0)
    IF_INT_GREATER(LW(8), 99)
        CALL(evt_btl_camera_shake_h, 0, 6, 0, 40, 13)
    ELSE
        CALL(evt_btl_camera_shake_h, 0, 3, 0, 30, 13)
    END_IF
    CALL(btlevtcmd_StageDispellFog)
    IF_INT_EQUAL(LW(10), 0)
        CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
        SET_VALUE(LW(15), 0)
        IF_INT_EQUAL(LW(5), 1)
            CALL_CHILD(sHitCheckedEvt)
        ELSE
            PM64_AVOID
        END_IF
        IF_INT_EQUAL(LW(15), 0)
            CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
        END_IF
    ELSE
        IF_INT_EQUAL(LW(10), 1)
            SET_VALUE(LW(11), 0)
        ELSE
            SET_VALUE(LW(11), 5)
        END_IF
        SET_VALUE(LW(13), -1)
        SET_VALUE(LW(14), 0)
        CALL_CHILD(sHitAllEvt)
    END_IF
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    /* blown off, spinning */
    BEGIN_SIBLING_EVENT
        SET_VALUE(LW(0), 0)
        LOOP(80) /* (33 a PM64 frame: 16 and 17 a TTYD frame) */
            WAIT_FRAMES(1)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(0))
            ADD(LW(0), 16)
            WAIT_FRAMES(1)
            CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, LW(0))
            ADD(LW(0), 17)
        END_LOOP
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    END_SIBLING_EVENT
    CALL(pm64SetAnim, BTL_UNIT_SELF, 27, -1)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 50)
    ADD(LW(1), 200)
    CALL(btlevtcmd_DivePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(10), 0, 0, 0, -1)
    WAIT_FRAMES(PM64_FRAMES(50))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), -25)
    ADD(LW(1), 200)
    CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_SetFallAccel, BTL_UNIT_SELF, PM64_ACCEL(0.6f))
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(20), -1)
    /* (upright here too: the spin above is a sibling of this event and lasts as long as the flight, so this
       RETURN can delete it a frame before it sets her straight) */
    CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    PM64_END
EVT_END()
static EVT_BEGIN(sBombEvt)
    SET_VALUE(LW(10), 0)
    CALL_CHILD(sBombBodyEvt)
    RETURN
EVT_END()
static EVT_BEGIN(sPowerBombEvt)
    SET_VALUE(LW(10), 1)
    CALL_CHILD(sBombBodyEvt)
    RETURN
EVT_END()
static EVT_BEGIN(sMegaBombEvt)
    SET_VALUE(LW(10), 2)
    CALL_CHILD(sBombBodyEvt)
    RETURN
EVT_END()

/* Bombette's Body Slam. PM64: battle/partner/bombette.c N(EVS_Attack_BodySlam), N(EVS_ReturnHome_Success/_Miss).
 * She runs (Run) to 40 short of the enemy, braces (Brace, jittering) while you hold A (TTYD's: Bobbery's
 * hold-and-let-go, PM64's own kind), and on letting go rams the last 30 (BodySlam, 3 frames); 1 / 2 / 3, or 2 / 3 / 5
 * done right. She is thrown back (Backfire2): 60 in 25 frames, then 30, 20 and a hop, idles 8 and runs home (Run,
 * speed 5). No blast: that is her Bomb. */
static EVT_BEGIN(sBodySlamEvt)
    PM64_BEGIN(PM64_ONE)
    PM64_CAM_TARGET
    PM64_FACE_TARGET
    CALL(pm64SetAnim, BTL_UNIT_SELF, 8, -1)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 40)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, PM64_SPEED(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    PM64_AC_HOLD_A
    CALL(pm64SetAnim, BTL_UNIT_SELF, 15, -1)
    WAIT_FRAMES(22)
    CALL(btlevtcmd_CommandPreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), DAMAGE_FLAG_LAST_HIT, LW(5))
    CALL(btlevtcmd_StartAC, 1)
    CALL(btlevtcmd_ResultAC)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 16, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SOUND_BOMBETTE_BODY_SLAM|SFX_PARTY_BATTLE_JUMP1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 10)
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(3), -1, 0)
    IF_INT_NOT_EQUAL(LW(5), 1)
        PM64_AVOID
        CALL(btlevtcmd_StopAC)
        CALL(pm64SetAnim, BTL_UNIT_SELF, 18, -1)
        WAIT_FRAMES(PM64_FRAMES(30))
        GOTO(95)
    END_IF
    SET_VALUE(LW(15), 0)
    CALL(evt_btl_camera_shake_h, 0, 2, 0, 6, 13)
    CALL_CHILD(sHitCheckedEvt)
    PM64_ONE_DONE
    PM64_BOUNCE(17, 60, 25)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 14, -1)
    WAIT_FRAMES(PM64_FRAMES(1))
    PM64_BOUNCE(17, 30, 8)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 14, -1)
    WAIT_FRAMES(PM64_FRAMES(1))
    PM64_BOUNCE(17, 20, 6)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 14, -1)
    WAIT_FRAMES(PM64_FRAMES(1))
    PM64_BOUNCE(17, 0, 4)
    CALL(pm64SetAnim, BTL_UNIT_SELF, 4, -1)
    WAIT_FRAMES(PM64_FRAMES(8))
    LABEL(95)
    PM64_HOME(8, 5.0f)
    PM64_END
EVT_END()
