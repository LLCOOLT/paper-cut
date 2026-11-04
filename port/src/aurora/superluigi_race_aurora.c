/* Super Luigi, chapter 3: the Grand Prix (docs/super_luigi/ch3.md, beats
 * 14-15, "The Lone Lap"), and the chapter's small scene pieces.
 *
 * yuu_l30, the Start Grid (the Plane Game's field, yuu_00): before race day
 * a quiet track entry (practice); on race day the start: eight karts, the
 * lights, Big Green 01 shoots backwards into the wall, everyone else piles
 * up, the scoreboard comes down, and Torque's Tune-up (the big gauge, a
 * 15-second clock) flips her back onto her wheels.
 *
 * yuu_l31, the Circuit (the Tube Game's course, yuu_02): Kart Mode. Luigi
 * is put into the game's tube motion (marioChgRollMotion, marioRollFixOn,
 * as the Tube Game does): its speed, steering and falls are the game's. He
 * is hidden and c_luigi_kart is drawn at his place, turned along the way he
 * goes (never more than 50 degrees off the course's line, so the paper kart
 * never shows its edge). Three laps of the loop (the course's own
 * checkpoints, fukkipoint in yuu_02.c), against the Royal Hourglass (3:00,
 * 3:10 with the Turbo Muffler); a fall puts him back at the last checkpoint.
 * On race day six wrecks lie on the line and from lap 2 Lug and Nut throw
 * scrap across the track: a hit spins him out. The game's start and finish
 * banners (effMiniGameEntry); our own hourglass and lap board. Practice is
 * one lap, no wrecks, a medal clock (gold 0:58, silver 1:10: a clean lap by
 * stick with a wait at the ferries took 1:06; the plan's 1:30 and 1:50 were
 * for two of the course's laps; one lap of practice teaches the line).
 *
 * The Tube Game's minigame code (counter, ranking, coins) lives in the yuu
 * REL and isn't called; everything here is DOL functions and our own. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/superluigi_ch03.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/mods.h"
#include "data/item_data.h"
#include "driver/dispdrv.h"
#include "driver/camdrv.h"
#include "driver/npcdrv.h"
#include "driver/windowdrv.h"
#include "driver/animdrv.h"
#include "eff/eff_minigame.h"
#include "evt/evt_movefloor.h"
#include "manager/fontmgr.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_motion.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mot/mot_roll.h"
#include "party.h"
#include "pmario_sound.h"
#include "system.h"
#include "driver/swdrv.h"
#include "driver/msgdrv.h"
#include <dolphin/pad.h>

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define M(tag, who) CALL(evt_msg_print, 0, STRING(tag), 0, STRING(who))
#define SYS(tag) CALL(evt_msg_print, 0, STRING(tag), 0, 0)

/* ======================================================================
 * Small pieces the scenes use
 * ====================================================================== */

static int sCrown;
static const char* sCaption;

/* in: 1 the crown rides on Luigi's cap (the crown prop, kept on his head), 0 off */
USER_FUNC(sl3_luigi_crown) {
    (void)isFirstCall;
    sCrown = (int)evtGetValue(event, event->args[0]);
    if (sCrown) {
        if (npcNameToPtr_NoAssert("sl_lcrown") == NULL) {
            if (!animGroupBaseAsync("c_sl_crown", 0, NULL)) {
                sCrown = 0;
                return EVT_RETURN_BLOCK;
            }
            npcEntry("sl_lcrown", "c_sl_crown");
        }
    } else if (npcNameToPtr_NoAssert("sl_lcrown") != NULL) {
        npcDelete(npcNameToPtr_NoAssert("sl_lcrown"));
    }
    return EVT_RETURN_DONE;
}

/* The tag's spotlight: a beam of warm light from high above onto a lit pool on the floor, in
 * flat paper bands as the game's stage lights are drawn (no soft falloff): the pool a pale
 * disc with a brighter core and a dark rim line, the beam a pale cone. Drawn on the 3D camera,
 * added to what's under it, so the darkened room (the script's map look) shows through. */
static struct {
    int on;
    f32 x, z;
} sSpot;

#define SPOT_R 46.0f    /* the pool's radius */
#define SPOT_TOP 320.0f /* the beam's source, out of the shot */
#define SPOT_SEG 28

static void spotVtx(f32 x, f32 y, f32 z, GXColor c) {
    GXPosition3f32(x, y, z);
    GXColor4u8(c.r, c.g, c.b, c.a);
}

static void spotDisc(f32 r, f32 y, GXColor c, f32 wobble) {
    int i;
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, SPOT_SEG + 2);
    spotVtx(sSpot.x, y, sSpot.z, c);
    for (i = 0; i <= SPOT_SEG; i++) {
        const f32 a = (f32)(i % SPOT_SEG) * (6.2831853f / SPOT_SEG);
        const f32 rr = r * (1.0f + wobble * sinf(a * 3.0f + 0.7f)); /* hand-cut, not a perfect circle */
        spotVtx(sSpot.x + rr * cosf(a), y, sSpot.z + rr * sinf(a) * 0.92f, c);
    }
    GXEnd();
}

static void spotDisp(CameraId cameraId, void* param) {
    CameraEntry* cam = camGetPtr(CAMERA_3D);
    const GXColor beam = {0xFF, 0xF2, 0xC8, 0x30}, pool = {0xFF, 0xEC, 0xB4, 0x58}, core = {0xFF, 0xF6, 0xDC, 0x48};
    const GXColor rim = {0x40, 0x34, 0x20, 0x70};
    int i;
    (void)cameraId, (void)param;
    if (!sSpot.on || cam == NULL) {
        return;
    }
    GXLoadPosMtxImm(cam->view, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetNumIndStages(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    /* the rim line first (darkening), then the pool, its core and the beam (adding light) */
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    spotDisc(SPOT_R + 3.0f, 0.4f, rim, 0.03f);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
    spotDisc(SPOT_R, 0.6f, pool, 0.03f);
    spotDisc(SPOT_R * 0.55f, 0.8f, core, 0.05f);
    GXBegin(GX_QUADS, GX_VTXFMT0, SPOT_SEG * 4);
    for (i = 0; i < SPOT_SEG; i++) {
        const f32 a0 = (f32)i * (6.2831853f / SPOT_SEG), a1 = (f32)(i + 1) * (6.2831853f / SPOT_SEG);
        const f32 rt = 7.0f;
        spotVtx(sSpot.x + rt * cosf(a0), SPOT_TOP, sSpot.z + rt * sinf(a0), beam);
        spotVtx(sSpot.x + rt * cosf(a1), SPOT_TOP, sSpot.z + rt * sinf(a1), beam);
        spotVtx(sSpot.x + SPOT_R * cosf(a1), 0.6f, sSpot.z + SPOT_R * sinf(a1) * 0.92f, beam);
        spotVtx(sSpot.x + SPOT_R * cosf(a0), 0.6f, sSpot.z + SPOT_R * sinf(a0) * 0.92f, beam);
    }
    GXEnd();
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

/* in: on, x, z: the tag's spotlight */
USER_FUNC(sl3_spotlight) {
    (void)isFirstCall;
    sSpot.on = (int)evtGetValue(event, event->args[0]);
    sSpot.x = (f32)evtGetValue(event, event->args[1]);
    sSpot.z = (f32)evtGetValue(event, event->args[2]);
    return EVT_RETURN_DONE;
}

static void captionDisp(CameraId cameraId, void* param) {
    const char* text = msgSearch((const char*)param);
    u16 lines = 0;
    u16 width;
    (void)cameraId;
    if (text == NULL) {
        return;
    }
    width = FontGetMessageWidthLine(text, &lines);
    windowDispGX_Waku_col(0, (GXColor){0x20, 0x20, 0x28, 0xC0}, -290.0f, 186.0f, (f32)(width * 7 / 10 + 24), 32.0f, 8.0f);
    FontDrawStart();
    FontDrawScale(0.7f);
    FontDrawColor((GXColor){0xFF, 0xF0, 0xC0, 0xFF});
    FontDrawMessage(-278, 178, text);
}

/* in: a message tag, 0 off: a small caption top left */
USER_FUNC(sl3_caption) {
    (void)isFirstCall;
    sCaption = (const char*)evtGetValue(event, event->args[0]);
    return EVT_RETURN_DONE;
}

/* (the held stills are the scene's own props) */
USER_FUNC(sl3_still) {
    (void)event, (void)isFirstCall;
    return EVT_RETURN_DONE;
}

/* in: x, z, radius; out: 1 if Luigi is landing a Spin Jump (the game's hip drop) there */
USER_FUNC(sl3_spin_landed) {
    EvtWord* a = event->args;
    MarioWork* m = marioGetPtr();
    const f32 dx = m->position.x - (f32)evtGetValue(event, a[0]);
    const f32 dz = m->position.z - (f32)evtGetValue(event, a[1]);
    const f32 r = (f32)evtGetValue(event, a[2]);
    (void)isFirstCall;
    evtSetValue(event, a[3], (m->currMotionId == MARIO_MOTION_HIP || m->currMotionId == MARIO_MOTION_HIP_2) &&
                                 dx * dx + dz * dz < r * r);
    return EVT_RETURN_DONE;
}

void portSl3PosterSets(void) {
    /* King Revvo on the HQ's fight posters (the race ends the chapter on the plaza, so the
     * "after" set, Big Green 01's poster, has no room left to show in) */
    portSlMapTextures("tou_l32", "textures/ch3_posters_revvo/");
}

/* ======================================================================
 * Kart Mode and the Lone Lap
 * ====================================================================== */

/* the course's checkpoints (yuu_02.c's fukkipoint, US and later) */
static const Vec kCheck[15] = {
    {300.0f, 0.0f, 300.0f},   {750.0f, 0.0f, 300.0f},   {1150.0f, 0.0f, 300.0f}, {1500.0f, 0.0f, 300.0f},
    {1550.0f, 0.0f, -300.0f}, {925.0f, 200.0f, -300.0f}, {250.0f, 200.0f, -300.0f}, {-250.0f, 200.0f, -300.0f},
    {-950.0f, 200.0f, -300.0f}, {-1450.0f, 0.0f, -300.0f}, {-1550.0f, 0.0f, 300.0f}, {-950.0f, 0.0f, 300.0f},
    {-250.0f, 0.0f, 300.0f},  {250.0f, 0.0f, 300.0f},   {250.0f, 0.0f, 300.0f},
};
/* the wrecks on race day */
static const Vec kWreck[6] = {
    {650.0f, 0.0f, 300.0f},  {1300.0f, 0.0f, 290.0f}, {700.0f, 200.0f, -300.0f},
    {-100.0f, 200.0f, -310.0f}, {-1200.0f, 0.0f, 300.0f}, {-550.0f, 0.0f, 290.0f},
};
/* Lug and Nut on the infield, and where their scrap crosses the front straight */
static const Vec kThrower[2] = {{900.0f, 0.0f, 60.0f}, {-800.0f, 0.0f, 60.0f}};

enum { R_OFF, R_RUN, R_DONE, R_FAIL };
static struct {
    int state, mode;          /* mode: 1 race day, 0 practice */
    int laps, lap, index;
    int frames, limit;
    Vec prev;
    int spin;                 /* spin-out frames left */
    int hitCool;
    int said;                 /* Lakky's lines said, by bit */
    int throwT[2];            /* frames into each thrower's cycle */
    Vec throwFrom[2];
    int result;
} sRace;

static NpcEntry* npcOf(const char* name) {
    return npcNameToPtr_NoAssert(name);
}

static void npcAnim(NpcEntry* npc, const char* anim) {
    if (npc != NULL && strcmp(npc->currentAnim, anim) != 0) {
        strcpy(npc->currentAnim, anim);
        animPoseSetAnim(npc->poseId, anim, TRUE);
    }
}

static void lakky(const char* tag) {
    portSlPrompt(tag, 2600);
}

static void respawn(int index) {
    MarioWork* m = marioGetPtr();
    m->position = kCheck[index];
    m->position.x += kCheck[index].z >= 0.0f ? 5.0f : -5.0f;
    m->position.y += 20.0f;
    marioResetRollSpd();
    clrRollEvtFlag();
    sRace.prev = m->position;
}

static void spinOut(void) {
    if (sRace.hitCool > 0) {
        return;
    }
    sRace.spin = 40;
    sRace.hitCool = 90;
    marioResetRollSpd();
    psndSFXOn("SFX_ENM_3RD2_CRASH1");
    npcAnim(npcOf("sl_kartmode"), "D_1");
}

static void raceStep(void) {
    MarioWork* m = marioGetPtr();
    NpcEntry* kart = npcOf("sl_kartmode");
    Vec p = m->position;
    int i;
    sRace.frames++;
    /* the tube's own size (mot_roll.c's 14.8): Play as Luigi's paper changes run marioSetSpec,
     * which puts back the standing 20 x 37, and the course's overhead bars (40 up) then stop
     * the kart dead at x 1100 and -1200, as they never stop the Tube Game's tube */
    if (m->currMotionId == MARIO_MOTION_ROLL) {
        m->wPlayerCollisionBox.x = 14.8f;
        m->wPlayerCollisionBox.y = 14.8f;
    }
    if (sRace.hitCool > 0) {
        sRace.hitCool--;
    }
    /* the kart at Luigi's place, along his way */
    if (kart != NULL) {
        f32 vx = p.x - sRace.prev.x, vz = p.z - sRace.prev.z;
        kart->position = p;
        if (sRace.spin == 0 && (vx > 0.3f || vx < -0.3f)) {
            /* the paper kart faces the way he rolls along the course, a little turned
             * into the bends (never edge-on) */
            f32 lean = vz * 4.0f;
            lean = lean > 25.0f ? 25.0f : lean < -25.0f ? -25.0f : lean;
            kart->direction = vx > 0.0f ? 180.0f : 0.0f;
            kart->rotation.y = vx > 0.0f ? -lean : lean;
        }
        if (sRace.spin > 0) {
            if (--sRace.spin == 0) {
                npcAnim(kart, "W_1");
            }
            marioResetRollSpd();
        } else {
            npcAnim(kart, vx * vx + vz * vz > 16.0f ? "R_1" : "W_1");
        }
    }
    /* fell off: back at the last checkpoint */
    if (p.y <= -50.0f) {
        respawn(sRace.index);
        return;
    }
    /* the checkpoints (yuu_02.c's rule), the laps */
    {
        const Vec* next = &kCheck[sRace.index + 1];
        if (p.z * next->z >= 0.0f && p.y >= next->y - 1.0f) {
            if ((p.z >= 0.0f && sRace.prev.x < next->x && p.x >= next->x) ||
                (p.z < 0.0f && sRace.prev.x > next->x && p.x <= next->x)) {
                sRace.index++;
                if (sRace.index == 6 && sRace.lap == 1 && !(sRace.said & 2) && sRace.mode) {
                    sRace.said |= 2;
                    lakky("sl_c3_lap_02");
                }
            }
        }
        if (sRace.index >= 13) {
            sRace.index = 0;
            if (sRace.lap >= sRace.laps) {
                sRace.state = R_DONE;
                return;
            }
            sRace.lap++;
            psndSFXOn("SFX_STG3_WIN_AUDIENCE1");
            if (sRace.mode && sRace.lap == 2) {
                lakky("sl_c3_lap_03");
            } else if (sRace.mode && sRace.lap == 3) {
                lakky("sl_c3_lap_04");
            }
        }
    }
    sRace.prev = p;
    if (sRace.mode) {
        /* the wrecks */
        for (i = 0; i < 6; i++) {
            if (fabsf(p.x - kWreck[i].x) < 22.0f && fabsf(p.z - kWreck[i].z) < 22.0f && fabsf(p.y - kWreck[i].y) < 30.0f) {
                spinOut();
            }
        }
        /* laps 2 and 3: the scrap, one throw each 2 seconds, a wind-up first */
        for (i = 0; i < 2 && sRace.lap >= 2; i++) {
            static const char* const kProj[2] = {"sl_scrap1", "sl_scrap2"};
            NpcEntry* s = npcOf(kProj[i]);
            const int t = (sRace.throwT[i] = (sRace.throwT[i] + 1) % 120);
            if (s == NULL) {
                continue;
            }
            if (t == 20) {
                psndSFXOn("SFX_ENM_TOGENOKO_MOVE1");
            }
            if (t >= 30 && t < 90) {
                const f32 k = (f32)(t - 30) / 60.0f;
                s->position.x = kThrower[i].x + (i ? 60.0f : -60.0f) * k;
                s->position.y = 10.0f + 80.0f * k * (1.0f - k) * 4.0f / 4.0f + 20.0f;
                s->position.z = kThrower[i].z + (420.0f - kThrower[i].z) * k;
                s->rotation.z += 24.0f;
                if (fabsf(p.x - s->position.x) < 20.0f && fabsf(p.z - s->position.z) < 20.0f && p.y < 40.0f) {
                    spinOut();
                }
            } else {
                s->position = (Vec){kThrower[i].x, -1000.0f, kThrower[i].z};
            }
        }
        if (sRace.frames >= sRace.limit) {
            sRace.state = R_FAIL;
        }
    }
}

static void raceDisp(CameraId cameraId, void* param) {
    char buf[64];
    int s;
    (void)cameraId, (void)param;
    if (sRace.state != R_RUN) {
        return;
    }
    s = sRace.mode ? (sRace.limit - sRace.frames + 59) / 60 : sRace.frames / 60;
    if (s < 0) {
        s = 0;
    }
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xE0}, 60.0f, 214.0f, 230.0f, 84.0f, 10.0f);
    FontDrawStart();
    FontDrawScale(0.7f);
    FontDrawColor((GXColor){0x30, 0x20, 0x10, 0xFF});
    FontDrawMessage(72, 206, msgSearch(sRace.mode ? "sl_c3_hud_hourglass" : "sl_c3_hud_clock"));
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    FontDrawScale(1.0f);
    FontDrawMessage(200, 184, buf);
    FontDrawScale(0.7f);
    snprintf(buf, sizeof(buf), "%s %d/%d", msgSearch("sl_c3_hud_lap"), sRace.lap, sRace.laps);
    FontDrawMessage(72, 160, buf);
}

static void frameHook(void) {
    static int sTick;
    NpcEntry* c;
    if (gp == NULL) {
        return;
    }
    if (getenv("TTYD_SL3_POS") != NULL && ++sTick % 30 == 0) {
        /* building the rooms: where Luigi is */
        MarioWork* m = marioGetPtr();
        printf("[sl3] %s pos %.0f %.0f %.0f\n", gp->currentMapName, m->position.x, m->position.y, m->position.z);
        fflush(stdout);
    }
    if (sSpot.on) {
        dispEntry(CAMERA_3D, 500.0f, 2, spotDisp, NULL);
    }
    if (sCaption != NULL) {
        dispEntry(CAMERA_2D, 905.0f, 1, captionDisp, (void*)sCaption);
    }
    if (sCrown && (c = npcOf("sl_lcrown")) != NULL) {
        MarioWork* m = marioGetPtr();
        c->position = m->position;
        c->position.y += 42.0f;
        c->position.z += 1.0f;
    }
    if (sRace.state == R_RUN && strncmp(gp->currentMapName, "yuu_l31", 7) == 0 && !gp->inBattle) {
        raceStep();
        dispEntry(CAMERA_2D, 900.0f, 1, raceDisp, NULL);
    } else if (sRace.state == R_RUN) {
        sRace.state = R_OFF;
    }
}

/* in: race day (1) or practice (0), laps; the run starts at the start line */
static USER_FUNC(sl3_race_start) {
    MarioWork* m = marioGetPtr();
    (void)isFirstCall;
    memset(&sRace, 0, sizeof(sRace));
    sRace.mode = (int)evtGetValue(event, event->args[0]);
    sRace.laps = (int)evtGetValue(event, event->args[1]);
    sRace.lap = 1;
    /* three laps by stick are about 3:20, with a wait at the ferries each lap, and a spin-out or two */
    sRace.limit = (240 + (swGet(SL3_FLAG_BASE + F_MUFFLER) ? 15 : 0)) * 60;
    sRace.prev = m->position;
    sRace.state = R_RUN;
    sRace.said = 1;
    if (sRace.mode) {
        lakky("sl_c3_lap_01");
    }
    return EVT_RETURN_DONE;
}

/* blocks while the run goes; out: 1 finished, 2 the Hourglass ran out; and the time (frames) */
static USER_FUNC(sl3_race_wait) {
    (void)isFirstCall;
    if (sRace.state == R_RUN) {
        return EVT_RETURN_BLOCK;
    }
    evtSetValue(event, event->args[0], sRace.state == R_DONE ? 1 : 2);
    evtSetValue(event, event->args[1], sRace.frames);
    sRace.state = R_OFF;
    return EVT_RETURN_DONE;
}

/* Luigi into the tube motion, held in it (as yuuminigame_init_3roll and roll_fix_on) */
static USER_FUNC(sl3_kart_mode) {
    (void)event, (void)isFirstCall;
    /* (marioChgMot sets up the roll's work as it starts; marioChgRollMotion
     * alone assumes the Tube Game's own entry did) */
    if (marioGetPtr()->currMotionId != MARIO_MOTION_ROLL) {
        marioChgMot(MARIO_MOTION_ROLL);
    }
    marioRollFixOn();
    return EVT_RETURN_DONE;
}

/* out of Kart Mode for good: Luigi unrolled (the tube motion otherwise follows him off the course,
 * a tiny tube in the plaza, his partner held riding and every partner move after it waiting) */
static USER_FUNC(sl3_kart_off) {
    (void)event, (void)isFirstCall;
    marioRollForceCancel();
    return EVT_RETURN_DONE;
}

static USER_FUNC(sl3_roll_reset) {
    (void)event, (void)isFirstCall;
    marioResetRollSpd();
    clrRollEvtFlag();
    return EVT_RETURN_DONE;
}

/* in: 0 start, 1 finish, 2 out of time: the game's minigame banner */
static USER_FUNC(sl3_banner) {
    (void)isFirstCall;
    effMiniGameEntry((s32)evtGetValue(event, event->args[0]), 0.0f, 0.0f, 500.0f);
    return EVT_RETURN_DONE;
}

/* in: the run's frames, race day or practice; the result board's text; out: the practice's medal
 * (0 none, 1 silver, 2 gold) */
static USER_FUNC(sl3_result) {
    char buf[200];
    const int f = (int)evtGetValue(event, event->args[0]);
    const int race = (int)evtGetValue(event, event->args[1]);
    const int s = f / 60, cs = (f % 60) * 100 / 60;
    int medal = 0;
    (void)isFirstCall;
    if (!race) {
        medal = f <= 58 * 60 ? 2 : f <= 70 * 60 ? 1 : 0;
    }
    snprintf(buf, sizeof(buf), "<system>\n%s\n%s %d:%02d.%02d\n<k>", msgSearch(race ? "sl_c3_result_race" : "sl_c3_result_practice"),
             msgSearch("sl_c3_result_time"), s / 60, s % 60, cs);
    portModsSetMessage("sl_c3_result_dyn", buf);
    evtSetValue(event, event->args[2], medal);
    return EVT_RETURN_DONE;
}

/* out: Torque's next retry line */
static USER_FUNC(sl3_retry_line) {
    static int n;
    static const char* const kLines[] = {"sl_c3_lap_retry_01", "sl_c3_lap_retry_02", "sl_c3_lap_retry_03",
                                         "sl_c3_lap_retry_04"};
    (void)isFirstCall;
    evtSetValue(event, event->args[0], (EvtWord)(AddrWord)kLines[n++ % 4]);
    return EVT_RETURN_DONE;
}

/* practice finishes paid out: the first five, 10 coins each (chapter 3's story word 0) */
static USER_FUNC(sl3_practice_pay) {
    const int n = swByteGet(SL_WORD(3, 0));
    (void)isFirstCall;
    evtSetValue(event, event->args[0], n < 5);
    if (n < 5) {
        swByteSet(SL_WORD(3, 0), n + 1);
    }
    return EVT_RETURN_DONE;
}

/* in: a medal's prize, straight into the pouch (the box said so) */
static USER_FUNC(sl3_medal_prize) {
    (void)isFirstCall;
    pouchGetItem((ItemType)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* the course's two moving floors (minigame03_start's) */
static EVT_BEGIN(course_floors)
    SET_VALUE(LW(0), STRING("S_ugoku"))
    SET_VALUE(LW(1), STRING("ugo1"))
    SET_VALUE(LW(2), 0)
    SET_VALUE(LW(3), -200)
    SET_VALUE(LW(4), 2400)
    SET_VALUE(LW(5), 600)
    SET_VALUE(LW(6), 0)
    SET_VALUE(LW(7), 0)
    SET_VALUE(LW(8), 0)
    SET_VALUE(LW(9), 0)
    SET_VALUE(LW(10), 0)
    SET_VALUE(LW(11), 0)
    SET_VALUE(LW(12), 0)
    SPAWN_EVENT(moving_floor)
    SET_VALUE(LW(0), STRING("S_ugoku1"))
    SET_VALUE(LW(1), STRING("ugo2"))
    SET_VALUE(LW(3), 200)
    SPAWN_EVENT(moving_floor)
    RETURN
EVT_END()

static EVT_BEGIN(s_en1_turn)
    SET_FLOAT_VALUE(LW(0), FLOAT(0.0f))
    LOOP(0)
        ADD_FLOAT(LW(0), FLOAT(0.75f))
        IF_INT_GREATER_OR_EQUAL(LW(0), FLOAT(360.0f))
            SUBTRACT_FLOAT(LW(0), FLOAT(360.0f))
        END_IF
        CALL(evt_mapobj_rotate, 1, STRING("S_en1"), FLOAT(0.0f), LW(0), FLOAT(0.0f))
        WAIT_FRAMES(1)
    END_LOOP
    RETURN
EVT_END()

/* the run: LW(10) 1 race day / 0 practice */
static EVT_BEGIN(circuit_main)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_mario_set_pos, 200, 0, 300)
    CALL(evt_mario_set_dir, 90, 0, 0)
    CALL(evt_npc_entry, STRING("sl_kartmode"), STRING("c_luigi_kart"))
    CALL(evt_npc_flag_onoff, 1, STRING("sl_kartmode"), NPC_FLAG_NO_MARIO_SIDE_HIT)
    CALL(evt_npc_set_position, STRING("sl_kartmode"), 200, 0, 300)
    CALL(evt_npc_set_ry, STRING("sl_kartmode"), 180)
    CALL(evt_npc_set_anim, STRING("sl_kartmode"), STRING("S_2"))
    CALL(evt_mario_dispflag_onoff, 1, 1)
    IF_INT_EQUAL(LW(10), 1)
        CALL(evt_npc_entry, STRING("sl_w1"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_w1"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_position, STRING("sl_w1"), 650, 0, 300)
        CALL(evt_npc_entry, STRING("sl_w2"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_w2"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_position, STRING("sl_w2"), 1300, 0, 290)
        CALL(evt_npc_entry, STRING("sl_w3"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_w3"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_position, STRING("sl_w3"), 700, 200, -300)
        CALL(evt_npc_entry, STRING("sl_w4"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_w4"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_position, STRING("sl_w4"), -100, 200, -310)
        CALL(evt_npc_entry, STRING("sl_w5"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_w5"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_position, STRING("sl_w5"), -1200, 0, 300)
        CALL(evt_npc_entry, STRING("sl_w6"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_w6"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_position, STRING("sl_w6"), -550, 0, 290)
        CALL(evt_npc_set_anim, STRING("sl_w1"), STRING("S_1"))
        CALL(evt_npc_set_anim, STRING("sl_w4"), STRING("S_1"))
        CALL(evt_npc_entry, STRING("sl_lug"), STRING("c_burosu_h"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_lug"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_tribe, STRING("sl_lug"), STRING(SJ3_HAMMER))
        CALL(evt_npc_set_position, STRING("sl_lug"), 900, 0, 60)
        CALL(evt_npc_entry, STRING("sl_nut"), STRING("c_burosu_b"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_nut"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_tribe, STRING("sl_nut"), STRING(SJ3_BOOMERANG))
        CALL(evt_npc_set_position, STRING("sl_nut"), -800, 0, 60)
        CALL(evt_npc_entry, STRING("sl_scrap1"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_scrap1"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_scale, STRING("sl_scrap1"), FLOAT(0.35f), FLOAT(0.35f), FLOAT(0.35f))
        CALL(evt_npc_set_position, STRING("sl_scrap1"), 0, -1000, 0)
        CALL(evt_npc_entry, STRING("sl_scrap2"), STRING("c_lp_kart_wreck"))
        CALL(evt_npc_flag_onoff, 1, STRING("sl_scrap2"), NPC_FLAG_NO_MARIO_SIDE_HIT)
        CALL(evt_npc_set_scale, STRING("sl_scrap2"), FLOAT(0.35f), FLOAT(0.35f), FLOAT(0.35f))
        CALL(evt_npc_set_position, STRING("sl_scrap2"), 0, -1000, 0)
    END_IF
    CALL(evt_snd_sfxon, STRING("SFX_EVT_GAME_AUDIENCE1"), GW(10))
    CALL(evt_seq_wait, 2)
    /* the title, then the course from end to end */
    CALL(evt_cam3d_evt_set, FLOAT(-1600.0f), FLOAT(573.0f), FLOAT(1333.0f), FLOAT(-1600.0f), FLOAT(12.0f), FLOAT(-89.0f), 0, 11)
    WAIT_FRAMES(1)
    CALL(evt_cam3d_evt_set, FLOAT(1600.0f), FLOAT(573.0f), FLOAT(1333.0f), FLOAT(1600.0f), FLOAT(12.0f), FLOAT(-89.0f), 4000, 0)
    CALL(evt_sl_prompt, STRING("sl_c3_lap_title"), 3000)
    WAIT_MS(4000)
    CALL(evt_cam3d_evt_off, 0, 11)
    WAIT_FRAMES(1)
    CALL(sl3_kart_mode)
    LABEL(10)
    CALL(evt_mario_set_pos, 200, 0, 300)
    CALL(sl3_roll_reset)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_MINIGAME1"))
    CALL(sl3_banner, 0)
    WAIT_MS(3000)
    IF_INT_EQUAL(LW(10), 1)
        CALL(sl3_race_start, 1, 3)
    ELSE
        CALL(sl3_race_start, 0, 1)
    END_IF
    CALL(evt_mario_key_onoff, 1)
    CALL(sl3_race_wait, LW(0), LW(1))
    CALL(evt_mario_key_onoff, 0)
    CALL(sl3_roll_reset)
    IF_INT_EQUAL(LW(0), 2)
        /* the Hourglass ran out: Torque, on the radio, and again */
        CALL(sl3_banner, 2)
        WAIT_MS(3000)
        CALL(evt_snd_sfxon, STRING("SFX_PEACH_MACHINE_SIREN1"), 0)
        CALL(sl3_retry_line, LW(2))
        CALL(evt_msg_print, 0, LW(2), 0, 0)
        GOTO(10)
    END_IF
    /* the line: he did it */
    CALL(sl3_banner, 1)
    CALL(evt_snd_sfxon, STRING("SFX_STG3_WIN_AUDIENCE2"), 0)
    CALL(evt_npc_set_anim, STRING("sl_kartmode"), STRING("S_2"))
    WAIT_MS(3000)
    CALL(evt_snd_bgmoff, PSND_FADE_OUT)
    CALL(evt_snd_sfxoff, GW(10))
    CALL(sl3_result, LW(1), LW(10), LW(3))
    IF_INT_EQUAL(LW(10), 1)
        CALL(evt_msg_print, 0, STRING("sl_c3_lap_05"), 0, 0)
        CALL(evt_msg_print, 0, STRING("sl_c3_result_dyn"), 0, 0)
        SET_VALUE(SL3F(F_RACE_WON), 1)
        CALL(sl3_kart_off)
        CALL(evt_mario_dispflag_onoff, 0, 1)
        CALL(evt_party_run, 0)
        CALL(evt_bero_mapchange, STRING("tou_l31"), STRING("a_door_mon"))
        RETURN
    END_IF
    CALL(evt_msg_print, 0, STRING("sl_c3_result_dyn"), 0, 0)
    IF_INT_EQUAL(LW(3), 2)
        IF_INT_EQUAL(SL3F(F_GOLD), 0)
            SET_VALUE(SL3F(F_GOLD), 1)
            SYS("sl_c3_medal_gold")
            CALL(sl3_medal_prize, ITEM_SHINE_SPRITE) /* (dropped by him, a prize waited for a touch that couldn't come: his key is off and the room's about to change) */
        END_IF
    END_IF
    IF_INT_GREATER_OR_EQUAL(LW(3), 1)
        IF_INT_EQUAL(SL3F(F_SILVER), 0)
            SET_VALUE(SL3F(F_SILVER), 1)
            SYS("sl_c3_medal_silver")
            CALL(sl3_medal_prize, ITEM_STAR_PIECE)
        END_IF
    END_IF
    CALL(sl3_kart_off)
    CALL(sl3_practice_pay, LW(4))
    IF_INT_EQUAL(LW(4), 1)
        CALL(evt_pouch_add_coin, 10)
        CALL(evt_snd_sfxon, STRING("SFX_COIN_GET1"), 0)
        SYS("sl_c3_practice_coins")
    END_IF
    CALL(evt_mario_dispflag_onoff, 0, 1)
    CALL(evt_party_run, 0)
    CALL(evt_bero_mapchange, STRING("yuu_l30"), STRING("sl_back"))
    RETURN
EVT_END()

EVT_BEGIN(sl3_circuit_init)
    CALL(evt_map_playanim, STRING("den1"), 1, 0)
    CALL(evt_map_playanim, STRING("den2"), 1, 0)
    CALL(evt_map_playanim, STRING("kumo"), 1, 0)
    CALL(evt_map_playanim, STRING("wakka"), 1, 0)
    SPAWN_EVENT(s_en1_turn)
    CALL_CHILD(course_floors)
    /* the signs: the language's own (yuu_02's), the Circuit Break sign over the English one */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("J_mon"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("D_mon"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("F_mon"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("Sp_mon"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("I_mon"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("S_den_mont"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("D_den_mont"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("F_den_mont"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("I_den_mont"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("Sp_den_mont"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("J_kan"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("D_kan"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("F_kan"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("Sp_kan"), MAPOBJ_FLAG_DISABLE)
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("I_kan"), MAPOBJ_FLAG_DISABLE)
    /* the Tube Game's start posts: the near one stands between the kart's camera and the kart, filling the screen's right third */
    CALL(evt_mapobj_flag_onoff, MAPOBJ_GROUP, 1, STRING("den_sta"), MAPOBJ_FLAG_DISABLE)
    SET_VALUE(LW(10), 0)
    IF_INT_EQUAL(SL3F(F_PITSTOP), 1)
        IF_INT_EQUAL(SL3F(F_RACE_WON), 0)
            SET_VALUE(LW(10), 1)
        END_IF
    END_IF
    SPAWN_EVENT(circuit_main)
    RETURN
EVT_END()

/* ======================================================================
 * yuu_l30, the Start Grid
 * ====================================================================== */

/* the rivals' karts on the grid, by colour, front to back (Big Green 01 last, by the wall) */
#define GRID_KART(n, model, x, z)                               \
    CALL(evt_npc_entry, STRING(n), STRING(model))               \
    CALL(evt_npc_set_position, STRING(n), x, 0, z)              \
    CALL(evt_npc_set_anim, STRING(n), STRING("S_2"))

static EVT_BEGIN(grid_karts)
    GRID_KART("sl_k1", "c_lp_kart_red", -260, 30)
    GRID_KART("sl_k2", "c_lp_kart_blue", -200, 90)
    GRID_KART("sl_k3", "c_lp_kart_yellow", -140, 30)
    GRID_KART("sl_k4", "c_lp_kart_purple", -80, 90)
    GRID_KART("sl_k5", "c_lp_kart_orange", -20, 30)
    GRID_KART("sl_k6", "c_lp_kart_white", 40, 90)
    GRID_KART("sl_k7", "c_lp_kart_black", 100, 30)
    CALL(evt_npc_entry, STRING("sl_revvo"), STRING("c_togenoko_b"))
    CALL(evt_npc_set_tribe, STRING("sl_revvo"), STRING(SJ3_KOOPATROL))
    CALL(evt_npc_set_position, STRING("sl_revvo"), -255, 14, 31)
    CALL(evt_npc_set_scale, STRING("sl_revvo"), FLOAT(0.8f), FLOAT(0.8f), FLOAT(0.8f))
    CALL(evt_npc_entry, STRING("sl_board"), STRING("c_sl_scoreboard"))
    CALL(evt_npc_set_position, STRING("sl_board"), -330, 0, -40)
    RETURN
EVT_END()

/* a kart crashes into the pile (LW(0): its name) */
static EVT_BEGIN(kart_pile)
    CALL(evt_npc_set_anim, LW(0), STRING("C_1"))
    CALL(evt_snd_sfxon, STRING("SFX_ENM_3RD2_CRASH1"), 0)
    CALL(evt_npc_get_position, LW(0), LW(1), LW(2), LW(3))
    CALL(evt_eff, 0, STRING("kemuri_test"), 0, LW(1), LW(2), LW(3), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(2.0f), 200)
    RETURN
EVT_END()
#define PILE(n)                     \
    SET_VALUE(LW(0), STRING(n))     \
    CALL_CHILD(kart_pile)           \
    WAIT_MS(220)

/* the wait for A at the lights: whatever the press, she goes backwards */
static USER_FUNC(sl3_wait_gas) {
    static int t;
    if (isFirstCall) {
        t = 0;
    }
    return (keyGetButtonTrg(0) & PAD_BUTTON_A) || ++t > 70 ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* the pit stop: she's over */
static EVT_BEGIN(pitstop_done)
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("F_1"))
    WAIT_FRAMES(20)
    CALL(evt_snd_sfxon, STRING("SFX_BTL_THUNDERS_BOMB1"), 0)
    CALL(evt_npc_get_position, STRING("sl_bg"), LW(1), LW(2), LW(3))
    CALL(evt_eff, 0, STRING("kemuri_test"), 0, LW(1), LW(2), LW(3), FLOAT(1.2f), 0, 0, 0, 0, 0, 0, 0)
    WAIT_FRAMES(25)
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("S_2"))
    SET_VALUE(SL3F(F_PITSTOP), 1)
    M("sl_c3_crash_16", "party")
    CALL(evt_bero_mapchange, STRING("yuu_l31"), STRING("sl_race"))
    RETURN
EVT_END()

/* beat 14 */
static EVT_BEGIN(race_start_scene)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STG3_TOUGI1"))
    CALL_CHILD(grid_karts)
    /* Big Green 01 at the very back, an inch from the grandstand wall */
    CALL(evt_mario_set_pos, 160, 0, 60)
    CALL(evt_mario_dispflag_onoff, 1, 1)
    CALL(evt_party_set_pos, 0, 0, -1000, 0)
    CALL(evt_npc_entry, STRING("sl_bg"), STRING("c_luigi_kart"))
    CALL(evt_npc_set_position, STRING("sl_bg"), 160, 0, 60)
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("S_2"))
    CALL(evt_npc_entry, STRING("sl_lakky"), STRING("c_jugemu"))
    CALL(evt_npc_set_tribe, STRING("sl_lakky"), STRING(SJ3_LAKITU))
    CALL(evt_npc_set_position, STRING("sl_lakky"), -80, 130, -20)
    CALL(evt_npc_entry, STRING("sl_lights"), STRING("c_sl_lights"))
    CALL(evt_npc_set_position, STRING("sl_lights"), -98, 108, -18)
    CALL(evt_npc_set_anim, STRING("sl_lights"), STRING("L_0"))
    CALL(evt_cam3d_evt_set, -40, 140, 560, -40, 30, 40, 0, 11)
    CALL(evt_seq_wait, 2)
    WAIT_MS(500)
    M("sl_c3_grid_01", "sl_lakky")
    CALL(evt_cam3d_evt_set, -230, 70, 260, -250, 30, 30, 800, 11)
    WAIT_MS(800)
    M("sl_c3_grid_02", "sl_revvo")
    CALL(evt_cam3d_evt_set, 160, 60, 240, 160, 25, 60, 800, 11)
    WAIT_MS(800)
    CALL(evt_snd_sfxon, STRING("SFX_PEACH_MACHINE_SIREN1"), 0)
    SYS("sl_c3_grid_03")
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("T_1"))
    M("sl_c3_grid_04", "sl_bg")
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("S_2"))
    /* the lights */
    CALL(evt_cam3d_evt_set, -40, 140, 560, -40, 30, 40, 600, 11)
    WAIT_MS(600)
    CALL(evt_npc_set_anim, STRING("sl_lights"), STRING("C_1"))
    WAIT_FRAMES(20)
    CALL(evt_snd_sfxon, STRING("SFX_AC_BUTTON_PUSH1"), 0)
    WAIT_FRAMES(40)
    CALL(evt_snd_sfxon, STRING("SFX_AC_BUTTON_PUSH1"), 0)
    WAIT_FRAMES(40)
    CALL(evt_snd_sfxon, STRING("SFX_AC_BUTTON_PUSH1"), 0)
    WAIT_FRAMES(40)
    CALL(evt_sl_prompt, STRING("sl_c3_grid_gas"), 1300)
    CALL(sl3_wait_gas)
    CALL(evt_sl_prompt, 0, 0)
    /* BACKWARDS */
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("B_1"))
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_MBTN_MOTOR_ACTIVE1"), 0)
    CALL(evt_cam3d_evt_set, 160, 60, 240, 220, 25, 60, 500, 11)
    BEGIN_INLINE_EVENT
        WAIT_MS(250)
        M("sl_c3_crash_01", "sl_bg")
    END_INLINE_EVENT
    CALL(evt_sl_npc_fly, STRING("sl_bg"), 245, 0, 60, 500)
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("C_1"))
    CALL(evt_snd_bgmoff, PSND_FADE_HOLD)
    CALL(evt_snd_sfxon, STRING("SFX_STG0_GESO_CRASH1"), 0)
    CALL(evt_eff, 0, STRING("bomb"), 0, 260, 10, 60, FLOAT(1.2f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(5.0f), 600)
    WAIT_MS(1200)
    /* the grid turns round to stare... */
    CALL(evt_cam3d_evt_set, -80, 110, 520, -80, 30, 40, 900, 11)
    CALL(evt_npc_set_ry, STRING("sl_revvo"), 180)
    WAIT_MS(1200)
    /* ...still accelerating, all looking backwards */
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_MBTN_MOTOR_ACTIVE1"), 0)
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_k7"), 40, 0, 40, 500)
    END_INLINE_EVENT
    BEGIN_INLINE_EVENT
        CALL(evt_sl_npc_fly, STRING("sl_revvo"), -250, 14, 40, 600)
    END_INLINE_EVENT
    WAIT_MS(450)
    PILE("sl_k7")
    PILE("sl_k6")
    PILE("sl_k5")
    PILE("sl_k4")
    PILE("sl_k3")
    PILE("sl_k2")
    PILE("sl_k1")
    /* the scoreboard comes down on the pile */
    CALL(evt_cam3d_evt_set, -280, 100, 330, -280, 30, 0, 500, 11)
    CALL(evt_npc_set_anim, STRING("sl_board"), STRING("F_1"))
    WAIT_FRAMES(40)
    CALL(evt_snd_sfxon, STRING("SFX_STG2_BOSS_EXPLOSION1"), 0)
    CALL(evt_cam_shake, 4, FLOAT(0.0f), FLOAT(4.0f), 500)
    CALL(evt_npc_set_position, STRING("sl_revvo"), 0, -1000, 0)
    WAIT_MS(1200)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_SURPRISED1"))
    M("sl_c3_crash_02", "sl_lakky")
    M("sl_c3_crash_03", "sl_lakky")
    /* Big Green 01, upside down and smoking; Luigi crawls out */
    CALL(evt_cam3d_evt_set, 230, 60, 240, 230, 20, 60, 800, 11)
    CALL(evt_npc_delete, STRING("sl_bg"))
    CALL(evt_npc_entry, STRING("sl_bg"), STRING("c_lp_kart"))
    CALL(evt_npc_set_position, STRING("sl_bg"), 245, 0, 60)
    CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("C_2"))
    CALL(evt_mario_set_pos, 215, 0, 75)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_mario_dispflag_onoff, 0, 1)
    CALL(evt_luigi_story_pose, STRING("SL_WOBBLE"))
    WAIT_MS(900)
    M("sl_c3_crash_04", "mario")
    CALL(evt_luigi_story_pose, 0)
    M("sl_c3_crash_05", "sl_board")
    /* Torque, running up */
    CALL(evt_party_set_pos, 0, 60, 0, 120)
    CALL(evt_party_run, 0)
    CALL(evt_party_move_pos, 0, 190, 90, 0)
    CALL(evt_party_run, 0)
    M("sl_c3_crash_06", "party")
    M("sl_c3_crash_07", "mario")
    WAIT_MS(1000)
    M("sl_c3_crash_08", "party")
    M("sl_c3_crash_09", "party")
    M("sl_c3_crash_10", "sl_lakky")
    M("sl_c3_crash_11", "sl_board")
    CALL(evt_cam3d_evt_off, 600, 11)
    M("sl_c3_crash_12", "mario")
    M("sl_c3_crash_13", "party")
    SET_VALUE(SL3F(F_RACE_START), 1)
    M("sl_c3_crash_14", "party")
    CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 245, 0, 60, 90, 1, PTR(pitstop_done))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

/* after the crash, back on the grid: the pile and the kart stay */
static EVT_BEGIN(grid_after)
    CALL_CHILD(grid_karts)
    CALL(evt_npc_set_anim, STRING("sl_k1"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_k2"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_k3"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_k4"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_k5"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_k6"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_k7"), STRING("C_2"))
    CALL(evt_npc_set_anim, STRING("sl_board"), STRING("F_2"))
    CALL(evt_npc_set_position, STRING("sl_revvo"), 0, -1000, 0)
    CALL(evt_npc_entry, STRING("sl_bg"), STRING("c_lp_kart"))
    CALL(evt_npc_set_position, STRING("sl_bg"), 245, 0, 60)
    IF_INT_EQUAL(SL3F(F_PITSTOP), 0)
        CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("C_2"))
        CALL(evt_sl_point, PORT_SL_ABILITY_TUNE, 0, 245, 0, 60, 90, 1, PTR(pitstop_done))
    ELSE
        CALL(evt_npc_set_anim, STRING("sl_bg"), STRING("S_2"))
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(grid_official)
    CALL(evt_mario_key_onoff, 0)
    IF_INT_EQUAL(SL3F(F_PITSTOP), 1)
        IF_INT_EQUAL(SL3F(F_RACE_WON), 0)
            M("sl_c3_grid_onward", "sl_official")
            CALL(evt_bero_mapchange, STRING("yuu_l31"), STRING("sl_race"))
            RETURN
        END_IF
    END_IF
    M("sl_c3_grid_official", "sl_official")
    CALL(evt_msg_select, 0, STRING("sl_c3_grid_choice"))
    CALL(evt_msg_continue)
    SWITCH(LW(0))
        CASE_EQUAL(0)
            CALL(evt_snd_bgmoff, PSND_FADE_OUT)
            CALL(evt_bero_mapchange, STRING("yuu_l31"), STRING("sl_practice"))
            RETURN
        CASE_EQUAL(1)
            CALL(evt_snd_bgmoff, PSND_FADE_OUT)
            CALL(evt_bero_mapchange, STRING("tou_l33"), STRING("e_bero"))
            RETURN
    END_SWITCH
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

static EVT_BEGIN(toolbox_read)
    SYS("sl_c3_grid_sign")
    RETURN
EVT_END()

EVT_BEGIN(sl3_grid_init)
    CALL(evt_snd_envon, PSND_FADE_RESTART | PSND_FADE_IN, STRING("ENV_STG3_TOU3"))
    CALL(evt_mario_set_pos, 0, 0, 150)
    CALL(evt_mario_set_dir, 270, 0, 0)
    CALL(evt_npc_entry, STRING("sl_official"), STRING("c_kinopio_g"))
    CALL(evt_npc_set_tribe, STRING("sl_official"), STRING(SJ3_TOAD_G))
    CALL(evt_npc_set_position, STRING("sl_official"), -60, 0, 170)
    CALL(evt_npc_set_ry, STRING("sl_official"), 90)
    CALL(evt_npc_change_interrupt, STRING("sl_official"), 6, PTR(grid_official))
    IF_INT_EQUAL(SL3F(F_NIGHT), 1)
        /* race day: Torque is the one in Luigi's pit */
        CALL(evt_sl_friend_slot, PORT_SL_FRIEND_TORQUE, LW(13))
        CALL(evt_mario_kill_party, 0) /* whoever came along: set_party_pos does nothing with one out, and the pit stop wants Tune-up */
        CALL(evt_mario_set_party_pos, 0, LW(13), -40, 0, 150)
        IF_INT_EQUAL(SL3F(F_RACE_START), 0)
            SPAWN_EVENT(race_start_scene)
            RETURN
        END_IF
        IF_INT_EQUAL(SL3F(F_RACE_WON), 0)
            CALL_CHILD(grid_after)
        END_IF
    ELSE
        CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_FF_ENV_TOU1"))
        CALL(evt_npc_entry, STRING("sl_toolbox"), STRING("c_lp_kart"))
        CALL(evt_npc_set_position, STRING("sl_toolbox"), 60, 0, 130)
        CALL(evt_npc_set_tribe, STRING("sl_toolbox"), STRING(SJ3_TOAD))
        CALL(evt_npc_change_interrupt, STRING("sl_toolbox"), 6, PTR(toolbox_read))
    END_IF
    RETURN
EVT_END()

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portSl3RaceRegister(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
