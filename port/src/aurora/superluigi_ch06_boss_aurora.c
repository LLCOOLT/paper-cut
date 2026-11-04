/* Super Luigi, chapter 6's battles (docs/super_luigi/ch6.md, "Battles"):
 *  - the tower's formations, one per room, from jin's own units;
 *  - the Wailer, the area's Atomic Boo tinted grey-blue with Hatesong (a sleep
 *    howl every other turn) and Hayzee's tune;
 *  - the Chestnut King (c_sl_chestnut_king): a spiky husk and a skull out of
 *    reach; a Superguarded or dodged Fang Lunge makes him overreach, the husk
 *    open and the skull low (a hammer on it double); Goo Drool sticks Luigi's
 *    boots, Chestnut Rain, Roar, the hesitation; at 10 HP he kneels and the
 *    last turn is Luigi's, All or Nothing;
 *  - Minister Crepe (c_sl_crepe) in three phases: all-seeing (every attack
 *    misses), seen (the tiara's Future Sight: his next move chosen before
 *    Luigi's turn and shown over his head, a gold ring closing on the guard
 *    moment), and the Nightmare (c_sl_nightmare once the models branch lands;
 *    till then Crepe unfolded, darkened and huge) with its two Page Arms; the
 *    last blow, Smite, is Luigi's. Eclair and the cursed King stand at the
 *    back of the stage, never acting.
 * Future Sight's overlay is drawn here, from the art in
 * port/assets/super_luigi/art/future_sight/. */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/hook_list.h"
#include "port/ac_cue.h"
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_damage.h"
#include "battle/battle_database_common.h"
#include "battle/battle_disp.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "battle/battle_status_effect.h"
#include "data/mapdata.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/windowdrv.h"
#include "manager/fontmgr.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

enum { ARM_NONE, ARM_WAILER, ARM_KING, ARM_CREPE };
static int sArm;      /* the next field battle's boss */
static int sHayzee;   /* Hayzee acted in the Wailer's fight */
static int sTune;     /* Hayzee's lullaby: the next Hatesong is off */

static int inCh6Battle(void) {
    return portSuperLuigiChapter() == 6 && gp != NULL && gp->inBattle;
}

/* the friend in Flurrie's slot is Hayzee, and he's the partner on stage */
static int hayzeeOut(void) {
    const PortSlFriend* f = portSlFriendInSlot(5);
    BattleUnit* p = _battleWorkPointer != NULL ? BattleGetPartyPtr(_battleWorkPointer) : NULL;
    return f != NULL && f->id == PORT_SL_FRIEND_HAYZEE && p != NULL && p->currentKind == UNIT_FLURRIE;
}

/* ======================================================================
 * The tower's formations (ch6.md, "Formations"): jin's units, by room
 * ====================================================================== */

/* the area's own setup of a unit kind (its place, hanging work, drops), found in its battle table */
static BattleUnitSetup* findSetup(int kind) {
    AreaData* area = areaDataPtr("jin");
    NpcSetupBattleInfo* t;
    if (area == NULL || area->btlSetupTbl == NULL) {
        return NULL;
    }
    for (t = (NpcSetupBattleInfo*)area->btlSetupTbl; t->name != NULL; t++) {
        NpcBattleSelect* s;
        for (s = t->select; s != NULL && s->weight != 0; s++) {
            int i;
            if (s->groupSetup == NULL) {
                continue;
            }
            for (i = 0; i < s->groupSetup->numEnemies; i++) {
                BattleUnitSetup* u = &s->groupSetup->enemyData[i];
                if (u->unitKindParams != NULL && (int)u->unitKindParams->unitType == kind) {
                    return u;
                }
            }
        }
    }
    return NULL;
}

static const struct {
    const char* map;
    int kinds[4];
} kFormations[] = {
    {"jin_60", {UNIT_BOO, UNIT_BOO}},
    {"jin_61", {UNIT_SWOOPER, UNIT_SWOOPER}},
    {"jin_62", {UNIT_BOO, UNIT_BUZZY_BEETLE}},
    {"jin_65", {UNIT_SPIKE_TOP, UNIT_SPIKE_TOP}},
    /* (three Boos and a Swooper hit for 15 a round: two of those fights took Luigi from 40 HP to a Game Over) */
    {"jin_66", {UNIT_BOO, UNIT_BOO, UNIT_SWOOPER}},
    {"jin_68", {UNIT_BUZZY_BEETLE, UNIT_BUZZY_BEETLE, UNIT_SPIKE_TOP}},
    {"jin_69", {UNIT_SWOOPER, UNIT_SWOOPER, UNIT_BOO}},
    {"jin_70", {UNIT_SPIKE_TOP, UNIT_BUZZY_BEETLE, UNIT_BUZZY_BEETLE}},
};

static BattleUnitSetup sFormation[4];
static BattleGroupSetup sFormationGroup;

static int formationFor(NpcBattleInfo* info) {
    int f, i, n = 0;
    if (info == NULL || info->groupSetup == NULL) {
        return 0;
    }
    for (f = 0; f < (int)(sizeof(kFormations) / sizeof(kFormations[0])); f++) {
        if (strcmp(kFormations[f].map, gp->currentMapName) != 0) {
            continue;
        }
        for (i = 0; i < 4 && kFormations[f].kinds[i] != 0; i++) {
            BattleUnitSetup* u = findSetup(kFormations[f].kinds[i]);
            if (u == NULL) {
                return 0;
            }
            sFormation[n] = *u;
            if (kFormations[f].kinds[i] == UNIT_SPIKE_TOP || kFormations[f].kinds[i] == UNIT_BUZZY_BEETLE) {
                /* on the floor, where a bomb, a shell or a Quake can flip them: hung from the
                 * ceiling their defense of 4 left a hammer or a bomb nothing to do, and Luigi lost */
                sFormation[n].unitWork[0] = 0;
                sFormation[n].position.y = 0.0f;
            }
            if (kFormations[f].kinds[i] == UNIT_SWOOPER) {
                /* in the air: on the ceiling neither Luigi's jump nor his hammer can reach one */
                sFormation[n].unitWork[0] = 1;
            }
            sFormation[n].position.x = 50.0f + 40.0f * (f32)n;
            sFormation[n].position.z = (n & 1) ? 10.0f : 0.0f;
            n++;
        }
        sFormationGroup = *info->groupSetup;
        sFormationGroup.numEnemies = n;
        sFormationGroup.enemyData = sFormation;
        info->groupSetup = &sFormationGroup;
        return 1;
    }
    return 0;
}

/* ======================================================================
 * Future Sight: the vision, the border, the ring
 * ====================================================================== */

enum {
    MV_FLAMBE = 1,
    MV_FOLD,
    MV_BATTER,
    MV_RESHUFFLE,
    MV_BOLT,
    MV_DOOM,
    MV_HATESONG,
    MV_THOUSAND,
    MV_GRAB,
    MV_DOOM_STRIKE,
    MV_REST,
};
enum { TGT_LUIGI, TGT_PARTNER, TGT_ALL, TGT_NONE };

static int sSight;      /* Future Sight on (phase 2 on) */
static int sVision;     /* the vision is up (Luigi's side's turn) */
static int sNextMove, sNextTarget, sDoomCount;
static struct {
    int unit;   /* the ring's target (unit id), -1 none */
    int frames; /* frames from start to the guard moment */
    int t;      /* frames run */
} sRing = {-1, 0, 0};
static int sSmiteRing; /* the Smite's ring, looping on the Nightmare */
static int sCrepeId = -1, sEclairId = -1;

static const char* moveIcon(int mv) {
    switch (mv) {
        case MV_FLAMBE: return "art/future_sight/icon_flambe.png";
        case MV_FOLD: return "art/future_sight/icon_fold.png";
        case MV_BATTER: return "art/future_sight/icon_batter.png";
        case MV_RESHUFFLE: return "art/future_sight/icon_reshuffle.png";
        case MV_BOLT: return "art/future_sight/icon_compass_bolt_warn.png";
        case MV_DOOM:
        case MV_DOOM_STRIKE: return "art/future_sight/icon_doom_foretold.png";
        case MV_HATESONG: return "art/future_sight/icon_hatesong.png";
        case MV_THOUSAND: return "art/future_sight/icon_thousand_tomorrows.png";
        case MV_GRAB: return "art/future_sight/icon_page_grab.png";
        default: return NULL;
    }
}

static const char* moveName(int mv) {
    static const char* const kTags[] = {"", "sl_c6_mv_flambe", "sl_c6_mv_fold", "sl_c6_mv_batter",
                                        "sl_c6_mv_reshuffle", "sl_c6_mv_bolt", "sl_c6_mv_doom", "sl_c6_mv_hatesong",
                                        "sl_c6_mv_thousand", "sl_c6_mv_grab", "sl_c6_mv_doom", "sl_c6_mv_rest"};
    const char* t = mv > 0 && mv <= MV_REST ? msgSearch(kTags[mv]) : NULL;
    return t != NULL ? t : "?";
}

static void quadSetup(void) {
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetNumTevStages(1);
    GXSetNumIndStages(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
}

/* a texture centred at (x, y) on the 2D camera, w x h, uv flipped by fu/fv, coloured */
static void quad(const char* path, f32 x, f32 y, f32 w, f32 h, int fu, int fv, GXColor c) {
    GXTexObj tex;
    int tw, th;
    f32 u0 = fu ? 1.0f : 0.0f, u1 = fu ? 0.0f : 1.0f, v0 = fv ? 1.0f : 0.0f, v1 = fv ? 0.0f : 1.0f;
    if (path == NULL || !portSlAssetTexture(path, &tex, &tw, &th)) {
        return;
    }
    if (w <= 0.0f) {
        w = (f32)tw;
    }
    if (h <= 0.0f) {
        h = (f32)th;
    }
    GXSetChanMatColor(GX_COLOR0A0, c);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x - w / 2, y + h / 2, 0.0f);
    GXTexCoord2f32(u0, v0);
    GXPosition3f32(x + w / 2, y + h / 2, 0.0f);
    GXTexCoord2f32(u1, v0);
    GXPosition3f32(x + w / 2, y - h / 2, 0.0f);
    GXTexCoord2f32(u1, v1);
    GXPosition3f32(x - w / 2, y - h / 2, 0.0f);
    GXTexCoord2f32(u0, v1);
    GXEnd();
}

/* a unit's place on the 2D camera (centred, +y up), at its height times k */
static int unitScreen(int id, f32 k, f32* sx, f32* sy) {
    BattleUnit* u = _battleWorkPointer != NULL ? BattleGetUnitPtr(_battleWorkPointer, id) : NULL;
    Vec in, out;
    if (u == NULL) {
        return 0;
    }
    in = u->position;
    in.y += (f32)u->height * k;
    btlGetScreenPoint(&in, &out);
    *sx = out.x;
    *sy = out.y;
    return 1;
}

static void sightDraw(CameraId cameraId, void* param) {
    static const GXColor kWhite = {0xFF, 0xFF, 0xFF, 0xFF};
    CameraEntry* camera = camGetPtr(cameraId);
    const f32 pulse = 0.85f + 0.15f * sinf((f32)gp->retraceCount / 12.0f);
    GXColor gold = {0xFF, 0xF0, 0xC0, (u8)(220 * pulse)};
    f32 x, y;
    int i;
    (void)param;
    quadSetup();
    GXLoadPosMtxImm(camera->view, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    /* the soft gold border: four corners, edges tiled between */
    for (i = -272; i < 272; i += 32) {
        quad("art/future_sight/border_edge.png", (f32)i + 16.0f, 232.0f, 32.0f, 16.0f, 0, 0, gold);
        quad("art/future_sight/border_edge.png", (f32)i + 16.0f, -232.0f, 32.0f, 16.0f, 0, 1, gold);
    }
    for (i = -208; i < 208; i += 32) {
        /* the sides: the edge strip turned (drawn as a narrow tall quad) */
        quad("art/future_sight/border_edge.png", -296.0f, (f32)i + 16.0f, 16.0f, 32.0f, 0, 0, gold);
        quad("art/future_sight/border_edge.png", 296.0f, (f32)i + 16.0f, 16.0f, 32.0f, 1, 0, gold);
    }
    quad("art/future_sight/border_corner.png", -288.0f, 224.0f, 32.0f, 32.0f, 0, 0, kWhite);
    quad("art/future_sight/border_corner.png", 288.0f, 224.0f, 32.0f, 32.0f, 1, 0, kWhite);
    quad("art/future_sight/border_corner.png", -288.0f, -224.0f, 32.0f, 32.0f, 0, 1, kWhite);
    quad("art/future_sight/border_corner.png", 288.0f, -224.0f, 32.0f, 32.0f, 1, 1, kWhite);
    /* the ring: shrinks round the target to the guard moment, flashes as it closes */
    if (sRing.unit >= 0 && unitScreen(sRing.unit, 0.5f, &x, &y)) {
        const int left = sRing.frames - sRing.t;
        if (left > 0) {
            const f32 s = 48.0f + 140.0f * (f32)left / (f32)(sRing.frames > 0 ? sRing.frames : 1);
            quad("art/future_sight/guard_ring.png", x, y, s, s, 0, 0, kWhite);
        } else if (left > -8) {
            quad("art/future_sight/guard_ring_closed.png", x, y, 64.0f, 64.0f, 0, 0, kWhite);
        }
    }
    if (sSmiteRing && sCrepeId >= 0 && unitScreen(sCrepeId, 0.5f, &x, &y)) {
        const int t = (int)(gp->retraceCount % 50);
        if (t < 42) {
            const f32 s = 80.0f + 160.0f * (f32)(42 - t) / 42.0f;
            quad("art/future_sight/guard_ring.png", x, y, s, s, 0, 0, kWhite);
        } else {
            quad("art/future_sight/guard_ring_closed.png", x, y, 96.0f, 96.0f, 0, 0, kWhite);
        }
    }
    /* the vision over Crepe's head: his next move, its icon and who it hits */
    if (sVision && sNextMove != 0 && sCrepeId >= 0 && unitScreen(sCrepeId, 1.0f, &x, &y)) {
        char line[96], who[48];
        /* above and right of the head, kept on screen (clear of Luigi's command bubble) */
        const f32 bx = x + 40.0f > 196.0f ? 196.0f : x + 40.0f;
        const f32 by = y + 86.0f > 164.0f ? 164.0f : y + 86.0f;
        quad("art/future_sight/vision_puff_small.png", x - 6.0f, y + 14.0f, 16.0f, 16.0f, 0, 0, kWhite);
        quad("art/future_sight/vision_puff_big.png", x - 14.0f, y + 32.0f, 24.0f, 24.0f, 0, 0, kWhite);
        quad("art/future_sight/vision_bubble.png", bx, by, 192.0f, 112.0f, 0, 0, kWhite);
        quad(moveIcon(sNextMove), bx - 62.0f, by + 4.0f, 32.0f, 32.0f, 0, 0, kWhite);
        switch (sNextTarget) {
            case TGT_LUIGI: snprintf(who, sizeof(who), "-> Luigi"); break;
            case TGT_PARTNER: snprintf(who, sizeof(who), "-> partner"); break;
            case TGT_ALL: snprintf(who, sizeof(who), "-> everyone"); break;
            default: who[0] = 0; break;
        }
        if (sNextMove == MV_DOOM_STRIKE) {
            snprintf(who + strlen(who), sizeof(who) - strlen(who), "  NOW!");
        } else if (sDoomCount > 0) {
            snprintf(who + strlen(who), sizeof(who) - strlen(who), "  Doom: %d", sDoomCount);
        }
        FontDrawStart();
        FontDrawEdge();
        FontDrawColor((GXColor){0x50, 0x38, 0x10, 0xFF});
        FontDrawScale(0.62f);
        snprintf(line, sizeof(line), "Next:");
        FontDrawString(bx - 38.0f, by + 26.0f, line);
        FontDrawString(bx - 38.0f, by + 6.0f, moveName(sNextMove));
        FontDrawColor(sNextMove == MV_BOLT || sNextMove == MV_DOOM_STRIKE ? (GXColor){0xC0, 0x20, 0x10, 0xFF}
                                                                           : (GXColor){0x50, 0x38, 0x10, 0xFF});
        FontDrawString(bx - 38.0f, by - 14.0f, who);
    }
}

/* seqMain: the overlay each frame of a Future Sight battle */
static void sightFrame(void) {
    if (!inCh6Battle()) {
        sSight = sVision = 0;
        sSmiteRing = 0;
        sRing.unit = -1;
        gPortAcCueHold = 0;
        return;
    }
    if (sRing.unit >= 0 && ++sRing.t > sRing.frames + 8) {
        sRing.unit = -1;
    }
    /* the ring is the guard's moment: the game's Action Command cue shows it too (and the tests'
     * $TTYD_AC_PLAY presses on it), as the cue does for a jump onto Luigi. The guard's window is
     * the 8 frames before the blow, the Superguard's the last 3: the cue aims inside the guard's,
     * and $TTYD_SL6_SGCUE (tests) inside the Superguard's, in every fight, for a real one */
    gPortAcCueHold = 0;
    if ((sSight || getenv("TTYD_SL6_SGCUE") != NULL) && sRing.unit >= 0) {
        const int left = sRing.frames - sRing.t;
        if (left >= -2 && left <= 24) {
            gPortAcCueHold = 1;
            gPortAcCueHoldLead = getenv("TTYD_SL6_SGCUE") != NULL ? left - 1 : left - 4;
        }
    }
    if (sSight) {
        dispEntry(CAMERA_2D, 1, 850.0f, sightDraw, NULL);
    }
}

/* ======================================================================
 * The scripts' C helpers
 * ====================================================================== */

static BattleUnit* unitOf(EventEntry* event, s32 id) {
    return BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, id));
}

/* in: a unit, frames to its guard moment: the ring (only with Future Sight). A move's windup can
 * start the ring early, for a lead a player can follow: its blow then finds it already closing */
static USER_FUNC(sl6_ring) {
    (void)isFirstCall;
    if (sSight || getenv("TTYD_SL6_SGCUE") != NULL) {
        const s32 unit = BattleTransID(event, evtGetValue(event, event->args[0]));
        const s32 frames = evtGetValue(event, event->args[1]);
        const s32 left = sRing.frames - sRing.t;
        if (sRing.unit == unit && left > 0 && left <= frames + 1) {
            return EVT_RETURN_DONE;
        }
        sRing.unit = unit;
        sRing.frames = frames;
        sRing.t = 0;
    }
    return EVT_RETURN_DONE;
}

/* in: 0/1 Future Sight; 0/1 the vision shown; 0/1 the Smite's ring */
static USER_FUNC(sl6_sight) {
    (void)isFirstCall;
    sSight = evtGetValue(event, event->args[0]);
    sVision = evtGetValue(event, event->args[1]);
    sSmiteRing = evtGetValue(event, event->args[2]);
    return EVT_RETURN_DONE;
}

/* in: the next move, its target, the doom count: what the vision shows */
static USER_FUNC(sl6_vision) {
    (void)isFirstCall;
    sNextMove = evtGetValue(event, event->args[0]);
    sNextTarget = evtGetValue(event, event->args[1]);
    sDoomCount = evtGetValue(event, event->args[2]);
    return EVT_RETURN_DONE;
}

/* in: which (0 Crepe, 1 Eclair): this unit is it, for the overlay and speakers */
static USER_FUNC(sl6_set_id) {
    const s32 which = evtGetValue(event, event->args[0]);
    const s32 id = BattleTransID(event, BTL_UNIT_SELF);
    (void)isFirstCall;
    if (which == 0) {
        sCrepeId = id;
    } else {
        sEclairId = id;
    }
    return EVT_RETURN_DONE;
}

/* out: Eclair's unit id */
static USER_FUNC(sl6_eclair_id) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sEclairId);
    return EVT_RETURN_DONE;
}

/* out: Luigi's unit id, the partner's (-1 none) */
static USER_FUNC(sl6_ids) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* m = BattleGetMarioPtr(wp);
    BattleUnit* p = BattleGetPartyPtr(wp);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], m != NULL ? BtlUnit_GetUnitId(m) : -1);
    evtSetValue(event, event->args[1], p != NULL && p->currentHp > 0 ? BtlUnit_GetUnitId(p) : -1);
    return EVT_RETURN_DONE;
}

/* in: a unit, a max HP: its max HP set (and HP to it) */
static USER_FUNC(sl6_set_max_hp) {
    BattleUnit* u = unitOf(event, evtGetValue(event, event->args[0]));
    const s32 hp = evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    if (u != NULL) {
        u->maxHp = u->baseMaxHp = u->currentHp = (s16)hp;
        if (getenv("TTYD_SL6_HP") != NULL) {
            u->currentHp = (s16)atoi(getenv("TTYD_SL6_HP"));
        }
    }
    return EVT_RETURN_DONE;
}

/* out: 1 if Luigi's HP is half or less */
static USER_FUNC(sl6_luigi_low) {
    BattleUnit* m = BattleGetMarioPtr(_battleWorkPointer);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], m != NULL && m->currentHp * 2 <= m->maxHp);
    return EVT_RETURN_DONE;
}

/* Goo Drool: Luigi's Jump off for 3 turns */
static USER_FUNC(sl6_no_jump) {
    (void)event;
    (void)isFirstCall;
    _battleWorkPointer->commandDisabled[0] = 3;
    return EVT_RETURN_DONE;
}

/* out: 1 if Hayzee is the partner on stage */
static USER_FUNC(sl6_hayzee) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], hayzeeOut());
    return EVT_RETURN_DONE;
}

/* the Wailer's tune: out 1 (and spent) if Hayzee sang to it */
static USER_FUNC(sl6_tune_take) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sTune);
    sTune = 0;
    return EVT_RETURN_DONE;
}

/* out: the field's flag: Hayzee was used in the Wailer's fight */
USER_FUNC(evt_sl6_boss_used) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sHayzee);
    return EVT_RETURN_DONE;
}

/* Cabinet Reshuffle: out 1 if another partner who has joined is being loaded (blocks until it is) */
static USER_FUNC(sl6_reshuffle_pick) {
    static const int kKind[8] = {0, UNIT_GOOMBELLA, UNIT_KOOPS, UNIT_BOBBERY, UNIT_YOSHI, UNIT_FLURRIE, UNIT_VIVIAN,
                                 UNIT_MS_MOWZ};
    static int sKind;
    BattleWork* wp = _battleWorkPointer;
    if (isFirstCall) {
        BattleUnit* p = BattleGetPartyPtr(wp);
        PouchData* pouch = pouchGetPtr();
        int cand[8], n = 0, id, hp = 0;
        sKind = 0;
        if (p == NULL) {
            evtSetValue(event, event->args[0], 0);
            return EVT_RETURN_DONE;
        }
        /* prefer friends with HP left, so the move isn't cruel */
        for (hp = 1; hp >= 0 && n == 0; hp--) {
            for (id = 1; id <= 7; id++) {
                if ((pouch->partyData[id].flags & POUCH_PARTY_FLAG_JOINED) && kKind[id] != (int)p->currentKind &&
                    (!hp || pouch->partyData[id].currentHP > 0)) {
                    cand[n++] = kKind[id];
                }
            }
        }
        if (n == 0) {
            evtSetValue(event, event->args[0], 0);
            return EVT_RETURN_DONE;
        }
        sKind = cand[rand() % n];
        wp->commandMenu.partyKind = sKind;
    }
    if (!BattlePartyAnimeLoad(sKind)) {
        return EVT_RETURN_BLOCK;
    }
    evtSetValue(event, event->args[0], 1);
    return EVT_RETURN_DONE;
}

/* in: 1 the partner is held (Page Grab); 0: let go; out: whether he was held */
static int sGrab;
static USER_FUNC(sl6_grab) {
    (void)isFirstCall;
    evtSetValue(event, event->args[1], sGrab);
    sGrab = evtGetValue(event, event->args[0]);
    return EVT_RETURN_DONE;
}

/* in: the model to use for the Nightmare: out 1 if c_sl_nightmare is built (the models branch) */
static USER_FUNC(sl6_nightmare_model) {
    size_t size = 0;
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portModsGetHostFile("/a/c_sl_nightmare", &size) != NULL ||
                                           portModsGetHostFile("a/c_sl_nightmare", &size) != NULL);
    return EVT_RETURN_DONE;
}

/* tests: $TTYD_SL6_SG turns a Guard into a Superguard (with the guard assist on Automatic);
 * $TTYD_SL6_KNEEL has the King kneel on his first turn. in/out: the guard result; out: kneel */
static USER_FUNC(sl6_test) {
    const s32 g = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], g == 4 && getenv("TTYD_SL6_SG") != NULL ? 5 : g);
    evtSetValue(event, event->args[1], getenv("TTYD_SL6_KNEEL") != NULL);
    return EVT_RETURN_DONE;
}

/* in: a tag: the moment to the log under $TTYD_SL_DEBUG (the fights' beats, for test runs) */
static USER_FUNC(sl6_beat) {
    (void)isFirstCall;
    if (getenv("TTYD_SL_DEBUG") != NULL) {
        printf("[sl6] beat: %s\n", (const char*)evtGetValue(event, event->args[0]));
        fflush(stdout);
    }
    return EVT_RETURN_DONE;
}

/* in: damage. A unit's own attack script hurting that unit (Crepe's flame blown back): the hit event
 * it starts deletes the unit's attack event, this script and all, so the command must end the script
 * as btlevtcmd_CheckDamage does (a plain btlevtcmd_DamageDirect on BTL_UNIT_SELF goes back to a wiped
 * event: evtmgrCmd reads from 0). The hit event stands the unit back up; nothing after this call runs. */
static USER_FUNC(sl6_self_hurt) {
    BattleUnit* u = unitOf(event, BTL_UNIT_SELF);
    (void)isFirstCall;
    BattleDamageDirect(-5, u, NULL, evtGetValue(event, event->args[0]), 0, DAMAGE_FLAG_LAST_HIT | DAMAGE_HIT_FIRE, 0, 1);
    return (event->flags & EVENT_ACTIVE) ? EVT_RETURN_DONE : EVT_RETURN_FINISH;
}

/* out: a random number 0..n-1 (n in) */
static USER_FUNC(sl6_rand) {
    const s32 n = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    evtSetValue(event, event->args[1], n > 0 ? rand() % n : 0);
    return EVT_RETURN_DONE;
}


/* ======================================================================
 * Shared bits of the bosses' scripts
 * ====================================================================== */

#define UW_PHASE 0  /* Crepe: 1, 2, 3; the King: 0 */
#define UW_TURNS 1  /* its turns so far */
#define UW_NEXT 2   /* the move chosen for its next turn */
#define UW_TARGET 3 /* its target (TGT_*) */
#define UW_SKIP 4   /* 1: it loses its next turn */
#define UW_OPEN 5   /* the King: overreached, husk open */
#define UW_STAGGER 6 /* the King kneeling / the Nightmare's Smite: 1 waiting, 2 done */
#define UW_DOOM 7   /* the Nightmare: the count, 0 none */
#define UW_DOOM_T 8 /* its target */
#define UW_LAST 9   /* the King: the last move (no hesitating twice) */

#define SAYB(tag, who) CALL(evt_msg_print, 2, STRING(tag), 0, who)

static PoseTableEntry kKingPoses[] = {
    {1, "K_1"},  {2, "D_1"},  {9, "D_1"},  {3, "D_1"},  {5, "D_1"},  {4, "D_1"},  {27, "K_1"}, {28, "S_1"},
    {29, "D_1"}, {30, "D_1"}, {31, "D_1"}, {39, "D_1"}, {40, "W_1"}, {42, "W_1"}, {56, "S_1"}, {57, "S_1"},
    {BTL_POSE_TALK, "T_3"},   {BTL_POSE_STAY_DEFAULT, "S_1"},
};
static PoseTableEntry kCrepePoses[] = {
    {1, "D_1"},  {2, "D_1"},  {9, "D_1"},  {3, "D_1"},  {5, "D_1"},  {4, "D_1"},  {27, "D_3"}, {28, "S_1"},
    {29, "D_1"}, {30, "D_1"}, {31, "D_1"}, {39, "D_1"}, {40, "W_1"}, {42, "R_1"}, {56, "S_1"}, {57, "S_1"},
    {BTL_POSE_TALK, "T_1"},   {BTL_POSE_STAY_DEFAULT, "S_1"},
};
/* the Nightmare's, by its arms: both, the front lost, the back lost, none (docs/LUIGI_ANIMS.md) */
#define NM_POSES(S, D)                                                                                    \
    {1, S}, {2, S}, {9, S}, {3, "X_1"}, {5, "X_1"}, {4, D}, {27, D}, {28, S}, {29, D}, {30, D}, {31, D},     \
        {39, D}, {40, "W_1"}, {42, "R_1"}, {56, S}, {57, S}, {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, S},
static PoseTableEntry kNightmarePoses[] = {NM_POSES("S_1", "D_1")};
static PoseTableEntry kNightmarePosesF[] = {NM_POSES("S_1F", "D_1F")};
static PoseTableEntry kNightmarePosesB[] = {NM_POSES("S_1B", "D_1B")};
static PoseTableEntry kNightmarePosesN[] = {NM_POSES("S_1N", "D_1N")};
static PoseTableEntry kArmPoses[] = {
    {1, "KRT_S_1"},  {2, "KRT_S_1"},  {9, "KRT_S_1"},  {3, "KRT_D_1"}, {5, "KRT_D_1"}, {4, "KRT_D_1"},
    {27, "KRT_D_1"}, {28, "KRT_S_1"}, {29, "KRT_D_1"}, {39, "KRT_D_1"}, {40, "KRT_S_1"}, {42, "KRT_S_1"},
    {BTL_POSE_TALK, "KRT_S_1"}, {BTL_POSE_STAY_DEFAULT, "KRT_S_1"},
};
static PoseTableEntry kEclairPoses[] = {
    {1, "B_1"},  {2, "B_1"},  {9, "B_1"},  {3, "B_1"},  {5, "B_1"},  {4, "B_1"},  {27, "B_1"}, {28, "B_1"},
    {39, "B_1"}, {40, "W_1"}, {42, "R_1"}, {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, "B_1"},
};
static PoseTableEntry kMarronPoses[] = {
    {1, "K_1"}, {28, "K_1"}, {39, "K_1"}, {40, "K_1"}, {BTL_POSE_TALK, "K_1"}, {BTL_POSE_STAY_DEFAULT, "K_1"},
};

static BattleUnitDefense kDef0 = {{0, 0, 0, 0, 0}};
static BattleUnitDefense kDef1 = {{1, 1, 1, 1, 1}};
static BattleUnitDefense kDef2 = {{2, 2, 2, 2, 2}};
static BattleUnitDefense kDef99 = {{99, 99, 99, 99, 99}};
static BattleUnitDefenseAttr kNoAttr = {{0, 0, 0, 0, 0}};
static StatusVulnerability kBossRegist = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static StatusVulnerability kNoRegist = {0};

#define WEAPON_BASE                                                                                        \
    .baseAccuracy = 100, .superguardsAllowed = 2, .reserved_14 = 1.0f, .stylishCommandMultiplier = 1,       \
    .reserved_19 = 1, .bingoSlotChance = 1, .reserved_1B = 1, .hpDamageFn = weaponGetPowerDefault,          \
    .targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT |                      \
                        TARGET_CLASS_NO_OWN_ALLIANCE | TARGET_CLASS_NO_SELF | TARGET_CLASS_MAIN_PARTS |     \
                        TARGET_CLASS_SINGLE,                                                                \
    .targetPropertyFlags = TARGET_PROP_START_OPPOSING_SIDE, .reserved_6F = 2,                               \
    .special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE,                  \
    .targetWeightFlags = TARGET_WEIGHT_PREFER_FRONT, .stageEffectRates = {0, 0, 0, 100}
#define WEAPON_ALL                                                                                         \
    .targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT |                      \
                        TARGET_CLASS_NO_OWN_ALLIANCE | TARGET_CLASS_NO_SELF | TARGET_CLASS_MAIN_PARTS |     \
                        TARGET_CLASS_MULTIPLE

/* the King's */
static BattleWeapon kLunge = {WEAPON_BASE, .hpDamageWork = {5}};
static BattleWeapon kDrool = {WEAPON_BASE, .hpDamageWork = {3}, .poisonChance = 100, .poisonTime = 3, .poisonStrength = 1};
static BattleWeapon kRain = {WEAPON_BASE, .hpDamageWork = {2}, .targetPropertyFlags = TARGET_PROP_JUMPLIKE | TARGET_PROP_START_OPPOSING_SIDE,
                             .targetWeightFlags = TARGET_WEIGHT_RANDOM};
static BattleWeapon kRoar = {WEAPON_BASE, .hpDamageFn = NULL, .atkChangeChance = 100, .atkChangeTime = 2, .atkChangeStrength = -1,
                             .special = SPECIAL_UNGUARDABLE};
/* Crepe's */
static BattleWeapon kFlambe = {WEAPON_BASE, WEAPON_ALL, .hpDamageWork = {3}, .element = ELEMENT_FIRE, .burnChance = 100, .burnTime = 1};
static BattleWeapon kFold = {WEAPON_BASE, .hpDamageWork = {1}, .stopChance = 100, .stopTime = 1};
/* three pounds, a hit each: 1, 1 and 2, the 4 the one blow used to do */
static BattleWeapon kBatter = {WEAPON_BASE, .hpDamageWork = {1}};
static BattleWeapon kBatterLast = {WEAPON_BASE, .hpDamageWork = {2}};
static BattleWeapon kBolt = {WEAPON_BASE, .hpDamageWork = {6}, .element = ELEMENT_ELECTRIC};
/* the Nightmare's */
static BattleWeapon kDoom = {WEAPON_BASE, .hpDamageWork = {8}, .targetPropertyFlags = TARGET_PROP_JUMPLIKE | TARGET_PROP_START_OPPOSING_SIDE};
static BattleWeapon kHatesong = {WEAPON_BASE, WEAPON_ALL, .hpDamageFn = NULL, .sleepChance = 100, .sleepTime = 2};
static BattleWeapon kTick = {WEAPON_BASE, .hpDamageWork = {1}};
static BattleWeapon kGrab = {WEAPON_BASE, .hpDamageWork = {2}, .stopChance = 100, .stopTime = 9};
/* the Wailer's Hatesong */
static BattleWeapon kWail = {WEAPON_BASE, WEAPON_ALL, .hpDamageFn = NULL, .sleepChance = 100, .sleepTime = 2};

/* One blow at a target: LW(3) the unit, LW(4) its part, LW(12) the weapon,
 * LW(13) frames from here to the guard moment (the ring). Out: LW(6) the
 * guard (5 Superguard, 4 Guard, else not), LW(7) 1 hit, 0 missed. */
static EVT_BEGIN(sl6_blow)
    SET_VALUE(LW(6), 0)
    SET_VALUE(LW(7), 0)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 0, LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(1)
            CALL(sl6_ring, LW(3), LW(13))
            WAIT_FRAMES(LW(13))
            CALL(btlevtcmd_ResultACDefence, LW(3), LW(12))
            CALL(btlevtcmd_GetResultACDefence, LW(6))
            CALL(sl6_test, LW(6), LW(5))
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
            SET_VALUE(LW(7), 1)
        CASE_EQUAL(3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
    END_SWITCH
    RETURN
EVT_END()

/* walks home and stands (LW(9): the stand pose type) */
static EVT_BEGIN(sl6_home)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 40)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -2, 0)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* walks up to LW(3) (to LW(14) units in front of it) */
static EVT_BEGIN(sl6_approach)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, 40)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), LW(14))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 0)
    RETURN
EVT_END()

/* the camera on the attacker and LW(3) */
static EVT_BEGIN(sl6_cam)
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    RETURN
EVT_END()

/* the target LW(3)/LW(4) as chosen: LW(0) TGT_LUIGI or TGT_PARTNER (no partner: Luigi) */
static EVT_BEGIN(sl6_pick_target)
    CALL(sl6_ids, LW(1), LW(2))
    IF_INT_EQUAL(LW(0), TGT_PARTNER)
        IF_INT_NOT_EQUAL(LW(2), -1)
            SET_VALUE(LW(3), LW(2))
        ELSE
            SET_VALUE(LW(3), LW(1))
        END_IF
    ELSE
        SET_VALUE(LW(3), LW(1))
    END_IF
    CALL(btlevtcmd_GetBodyId, LW(3), LW(4))
    RETURN
EVT_END()

/* every target of LW(12) on Luigi's side: LW(8) out, 1 if any Superguarded */
static EVT_BEGIN(sl6_blow_all)
    SET_VALUE(LW(8), 0)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)
    CALL(btlevtcmd_WaitGuardMove)
    LABEL(0)
    CALL_CHILD(sl6_blow)
    IF_INT_EQUAL(LW(6), 5)
        SET_VALUE(LW(8), 1)
    END_IF
    SET_VALUE(LW(13), 14)
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(0)
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * The Wailer: the Atomic Boo's own moves, and Hatesong every other turn
 * ====================================================================== */

static EvtWord* sWailerOwn; /* the Atomic Boo's own attack event */

static USER_FUNC(sl6_wailer_own) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], (EvtWord)(AddrWord)sWailerOwn);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(wailer_attack)
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, 12, 1)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 12, LW(0))
    MODULO(LW(0), 2)
    IF_INT_EQUAL(LW(0), 0)
        CALL(sl6_tune_take, LW(1))
        IF_INT_EQUAL(LW(1), 1)
            CALL(evt_msg_print, 0, STRING("sl_c6_bt_tune"), 0, 0)
            CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
            RETURN
        END_IF
        /* Hatesong: the howl; guard to resist the sleep */
        CALL(evt_msg_print, 0, STRING("sl_c6_bt_hatesong"), 0, 0)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_ATMTLSA_SURPRISED2"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_btl_camera_shake_h, 0, 3, 0, 40, 13)
        SET_VALUE(LW(12), PTR(&kWail))
        SET_VALUE(LW(13), 30)
        CALL_CHILD(sl6_blow_all)
        WAIT_FRAMES(30)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    CALL(sl6_wailer_own, LW(0))
    CALL_CHILD(LW(0))
    RETURN
EVT_END()

/* the Wailer, as it enters: tinted, its attack wrapped (BtlUnit_Entry, then its init has run by turn 1) */
static EVT_BEGIN(wailer_phase)
    CALL(btlevtcmd_CheckPhase, LW(0), BATTLE_PHASE_BEFORE_PLAYER)
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 13, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, 13, 1)
        CALL(btlevtcmd_SetRGB, BTL_UNIT_SELF, 1, 150, 175, 225)
        CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(wailer_attack))
    END_IF
    RETURN
EVT_END()

/* ======================================================================
 * The Chestnut King
 * ====================================================================== */

#define HUSK 1
#define SKULL 2

extern EvtWord king_init[], king_attack[], king_damage[], king_dead[], king_wait[];

static BattleUnitKindPart kKingParts[] = {
    {HUSK, "sl_c6_un_husk", "c_sl_chestnut_king", {0.0f, 0.0f, 0.0f}, {0.0f, 40.0f, 0.0f}, {0.0f, 70.0f, 0.0f}, 60, 80,
     255, &kDef0, &kNoAttr, PART_ATTR_MAIN | PART_ATTR_UNTATTLEABLE, COUNTER_ATTR_TOP_SPIKY, kKingPoses},
    {SKULL, "sl_c6_un_skull", "c_sl_chestnut_king", {0.0f, 0.0f, 0.0f}, {-10.0f, 70.0f, 5.0f}, {-10.0f, 90.0f, 5.0f},
     20, 30, 255, &kDef0, &kNoAttr,
     PART_ATTR_HIDDEN | PART_ATTR_NO_SHADOW | PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE, 0, kKingPoses},
};
static BattleDataEntry kKingData[] = {{48, btldefaultevt_Dummy}, {49, king_dead}, {0, NULL}};
static BattleUnitKind kKing = {
    .unitType = 1, .name = "sl_c6_un_king", .maxHp = 40, .dangerHP = 10, .perilHP = 5, .level = 55, .bonusExp = 40,
    .bonusCoin = 10, .bonusCoinRate = 100, .pbSoftCap = 9999, .width = 80, .height = 110, .hitOffset = {0, 70},
    .centerOffset = {0.0f, 55.0f, 0.0f}, .hpGaugeOffset = {0, 0}, .talkTogeBaseOffset = {0.0f, 110.0f, 0.0f},
    .heldItemBaseOffset = {0.0f, 110.0f, 0.0f}, .burnFlameOffset = {0.0f, 110.0f, 0.0f},
    .cutBaseOffset = {0.0f, 55.0f, 0.0f}, .cutWidth = 80.0f, .cutHeight = 110.0f, .turnOrder = 10,
    .starPointDispOffset = {0.0f, 110.0f, 0.0f}, .damageSfxName = "SFX_BTL_SYS_BREAK1",
    .fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1", .iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1",
    .explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1", .attributeFlags = 0, .statusVulnerability = &kBossRegist,
    .numParts = 2, .parts = kKingParts, .initEventCode = king_init, .dataTable = kKingData,
};
static BattleUnitSetup kKingSetup[] = {
    {&kKing, 1, {0}, BATTLE_PHASE_ENEMY, {90.0f, 0.0f, 0.0f}, 0, {0, 0, 0, 0}, NULL},
};
static BattleGroupSetup kKingGroup = {1, kKingSetup, 0, 0, 100, battle_heart_drop_param_default,
                                      battle_flower_drop_param_default, 0};

EVT_BEGIN(king_wait)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("K_1"))
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_OPEN, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("H_2"))
        RETURN
    END_IF
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

EVT_BEGIN(king_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(king_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(king_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(king_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(king_attack))
    CALL(btlevtcmd_SetWalkSound, BTL_UNIT_SELF, STRING("SFX_BTL_GNB_STEP1"), STRING("SFX_BTL_GNB_STEP1"), 0, 20, 20)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_OPEN, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STAGGER, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TURNS, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAST, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* the husk splits, the skull drops within reach */
static EVT_BEGIN(king_open)
    CALL(sl6_beat, STRING("the King overreaches"))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_OPEN, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_1"))
    CALL(btlevtcmd_OffPartsCounterAttribute, BTL_UNIT_SELF, HUSK, COUNTER_ATTR_TOP_SPIKY)
    CALL(btlevtcmd_OffPartsAttribute, BTL_UNIT_SELF, SKULL, PART_ATTR_NOT_TARGETABLE)
    WAIT_FRAMES(40)
    CALL(evt_msg_print, 0, STRING("sl_c6_bt_lunge_miss"), 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(king_close)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_OPEN, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("O_2"))
    CALL(btlevtcmd_OnPartsCounterAttribute, BTL_UNIT_SELF, HUSK, COUNTER_ATTR_TOP_SPIKY)
    CALL(btlevtcmd_OnPartsAttribute, BTL_UNIT_SELF, SKULL, PART_ATTR_NOT_TARGETABLE)
    WAIT_FRAMES(30)
    CALL(evt_msg_print, 0, STRING("sl_c6_bt_close"), 0, 0)
    RETURN
EVT_END()

/* Fang Lunge: a Superguard or a dodge and he overreaches */
static EVT_BEGIN(king_lunge)
    SET_VALUE(LW(12), PTR(&kLunge))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    SET_VALUE(LW(14), 90)
    CALL_CHILD(sl6_approach)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    WAIT_FRAMES(24)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_BITE1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 30)
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(10.0f))
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 0)
    END_SIBLING_EVENT
    SET_VALUE(LW(13), 5)
    CALL_CHILD(sl6_blow)
    IF_INT_EQUAL(LW(6), 5)
        SET_VALUE(LW(7), 0)
    END_IF
    IF_INT_EQUAL(LW(7), 0)
        /* he crashes past, off balance */
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionAdd, BTL_UNIT_SELF, LW(0), 60)
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 0)
        CALL(evt_btl_camera_shake_h, 0, 4, 0, 20, 13)
        CALL_CHILD(king_open)
    END_IF
    WAIT_FRAMES(20)
    CALL_CHILD(sl6_home)
    RETURN
EVT_END()

static EVT_BEGIN(king_drool)
    SET_VALUE(LW(12), PTR(&kDrool))
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    SET_VALUE(LW(14), 70)
    CALL_CHILD(sl6_approach)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    SET_VALUE(LW(13), 12)
    CALL_CHILD(sl6_blow)
    IF_INT_EQUAL(LW(7), 1)
        IF_INT_LESS(LW(6), 4)
            CALL(sl6_ids, LW(0), LW(1))
            IF_INT_EQUAL(LW(3), LW(0))
                CALL(sl6_no_jump)
                CALL(evt_msg_print, 0, STRING("sl_c6_bt_stuck"), 0, 0)
            END_IF
        END_IF
    END_IF
    WAIT_FRAMES(20)
    CALL_CHILD(sl6_home)
    RETURN
EVT_END()

static EVT_BEGIN(king_rain)
    CALL(evt_btl_camera_set_mode, 0, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3"))
    CALL(evt_btl_camera_shake_h, 0, 3, 0, 40, 13)
    WAIT_FRAMES(30)
    LOOP(2)
        SET_VALUE(LW(12), PTR(&kRain))
        CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
        CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
        IF_INT_NOT_EQUAL(LW(3), -1)
            CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
            CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
            ADD(LW(1), 60)
            CALL(evt_eff, STRING(""), STRING("kemuri_test"), 7, LW(0), LW(1), LW(2), FLOAT(0.8f), 0, 0, 0, 0, 0, 0, 0)
            SET_VALUE(LW(13), 14)
            CALL_CHILD(sl6_blow)
            WAIT_FRAMES(16)
        END_IF
    END_LOOP
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(king_roar)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("R_2"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_GNB_ROAR1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(evt_btl_camera_shake_h, 0, 6, 0, 40, 13)
    CALL(evt_msg_print, 2, STRING("sl_c6_bt_roar"), 0, BTL_UNIT_SELF)
    CALL(sl6_ids, LW(3), LW(1))
    CALL(btlevtcmd_GetBodyId, LW(3), LW(4))
    SET_VALUE(LW(12), PTR(&kRoar))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    SET_VALUE(LW(13), 1)
    CALL_CHILD(sl6_blow)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

extern EvtWord king_kneel[];
EVT_BEGIN(king_attack)
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURNS, 1)
    SET_VALUE(LW(0), 0)
    CALL(sl6_test, LW(0), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
        IF_INT_EQUAL(LW(0), 0)
            CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 10)
            CALL_CHILD(king_kneel)
            CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
            RETURN
        END_IF
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        /* kneeling: no move left in him */
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_OPEN, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL_CHILD(king_close)
    END_IF
    /* he hesitates: from the second turn, once in four, never twice in a row */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURNS, LW(0))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_LAST, LW(1))
    IF_INT_GREATER_OR_EQUAL(LW(0), 2)
        IF_INT_NOT_EQUAL(LW(1), 9)
            CALL(sl6_rand, 4, LW(2))
            IF_INT_EQUAL(LW(2), 0)
                CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAST, 9)
                CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("Q_1"))
                CALL(evt_msg_print, 0, STRING("sl_c6_bt_hesitate"), 0, 0)
                CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
                RETURN
            END_IF
        END_IF
    END_IF
    CALL(sl6_rand, 100, LW(2))
    IF_INT_LESS(LW(2), 50)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAST, 1)
        CALL_CHILD(king_lunge)
    ELSE
        IF_INT_LESS(LW(2), 70)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAST, 2)
            CALL_CHILD(king_drool)
        ELSE
            IF_INT_LESS(LW(2), 90)
                CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAST, 3)
                CALL_CHILD(king_rain)
            ELSE
                CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_LAST, 4)
                CALL_CHILD(king_roar)
            END_IF
        END_IF
    END_IF
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* down to his knees: the last turn is Luigi's, All or Nothing */
EVT_BEGIN(king_kneel)
    CALL(sl6_beat, STRING("the King kneels"))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STAGGER, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("K_1"))
    CALL(btlevtcmd_OffPartsCounterAttribute, BTL_UNIT_SELF, HUSK, COUNTER_ATTR_TOP_SPIKY)
    CALL(evt_msg_print, 0, STRING("sl_c6_bt_stagger"), 0, 0)
    CALL(evt_sl_force_command, 0, PORT_SL_ACT_HAMMER, 0, STRING("sl_c6_bt_aon"))
    CALL(evt_sl_force_command, 1, PORT_SL_ACT_TACTICS, 0, 0)
    RETURN
EVT_END()

/* a boss's Star Points and coins, as a defeat stores them (btldefaultevt's): the bosses here
 * end on KillUnit, which doesn't */
static EVT_BEGIN(sl6_reward)
    CALL(btlevtcmd_GetCoin, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_StoreCoin, LW(0))
    CALL(btlevtcmd_GetExp, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_StoreExp, LW(0))
    RETURN
EVT_END()

/* the swing comes down: black, and the battle's won (it never lands) */
static EVT_BEGIN(king_end)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STAGGER, 2)
    CALL(evt_sl_force_command, 0, -1, 0, 0)
    CALL(evt_sl_force_command, 1, -1, 0, 0)
    CALL(evt_fade_entry, FADE_OUT_BLACK, 0, 0, 0, 0)
    CALL_CHILD(sl6_reward)
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
    RETURN
EVT_END()

EVT_BEGIN(king_damage)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL_CHILD(king_end)
        RETURN
    END_IF
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(1))
    IF_INT_EQUAL(LW(1), 0)
        IF_INT_LESS_OR_EQUAL(LW(0), 10)
            IF_INT_GREATER(LW(0), 0)
                CALL_CHILD(king_kneel)
            END_IF
        END_IF
    END_IF
    RETURN
EVT_END()

/* at 0 HP before he's knelt: he kneels instead, on 1 */
EVT_BEGIN(king_dead)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 1)
        CALL_CHILD(king_kneel)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    CALL_CHILD(king_end)
    RETURN
EVT_END()

/* ======================================================================
 * Minister Crepe, the Nightmare, and the two who watch
 * ====================================================================== */

#define BODY 1

extern EvtWord crepe_init[], crepe_attack[], crepe_damage[], crepe_dead[], crepe_wait[], crepe_phase[], crepe_dodge[];
extern EvtWord watcher_init[], arm_init[], arm_damage[], nm_arms_pose[];

static BattleUnitKindPart kCrepeParts[] = {
    {BODY, "sl_c6_un_crepe", "c_sl_crepe", {0.0f, 0.0f, 0.0f}, {0.0f, 30.0f, 0.0f}, {0.0f, 50.0f, 0.0f}, 30, 60, 255,
     &kDef0, &kNoAttr, PART_ATTR_MAIN | PART_ATTR_UNTATTLEABLE | PART_ATTR_EVASIVE, 0, kCrepePoses},
};
static BattleDataEntry kCrepeData[] = {{48, btldefaultevt_Dummy}, {49, crepe_dead}, {46, crepe_dodge}, {0, NULL}};
static BattleUnitKind kCrepe = {
    .unitType = 1, .name = "sl_c6_un_crepe", .maxHp = 50, .dangerHP = 10, .perilHP = 5, .level = 70, .bonusExp = 100,
    .bonusCoin = 20, .bonusCoinRate = 100, .pbSoftCap = 9999, .width = 30, .height = 60, .hitOffset = {0, 40},
    .centerOffset = {0.0f, 30.0f, 0.0f}, .talkTogeBaseOffset = {0.0f, 60.0f, 0.0f},
    .heldItemBaseOffset = {0.0f, 60.0f, 0.0f}, .burnFlameOffset = {0.0f, 60.0f, 0.0f},
    .cutBaseOffset = {0.0f, 30.0f, 0.0f}, .cutWidth = 30.0f, .cutHeight = 60.0f, .turnOrder = 10,
    .starPointDispOffset = {0.0f, 60.0f, 0.0f}, .damageSfxName = "SFX_BOSS_GNB_DAMAGED1",
    .fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1", .iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1",
    .explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1", .attributeFlags = UNIT_ATTR_REQUIRED_FOR_CLEAR,
    .statusVulnerability = &kBossRegist, .numParts = 1, .parts = kCrepeParts, .initEventCode = crepe_init,
    .dataTable = kCrepeData,
};
static BattleUnitKindPart kNightmareParts[] = {
    {BODY, "sl_c6_un_nightmare", "c_sl_nightmare", {0.0f, 0.0f, 0.0f}, {0.0f, 60.0f, 0.0f}, {0.0f, 100.0f, 0.0f}, 60, 120,
     255, &kDef0, &kNoAttr, PART_ATTR_MAIN | PART_ATTR_UNTATTLEABLE, 0, kNightmarePoses},
};

static BattleUnitKindPart kWatcherParts[] = {
    {1, "sl_c6_un_eclair", "c_sl_eclair", {0.0f, 0.0f, 0.0f}, {0.0f, 30.0f, 0.0f}, {0.0f, 40.0f, 0.0f}, 30, 60, 255,
     &kDef99, &kNoAttr, PART_ATTR_MAIN | PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE, 0, kEclairPoses},
};
static BattleUnitKindPart kMarronParts[] = {
    {1, "sl_c6_un_king", "c_sl_chestnut_king", {0.0f, 0.0f, 0.0f}, {0.0f, 40.0f, 0.0f}, {0.0f, 60.0f, 0.0f}, 60, 80,
     255, &kDef99, &kNoAttr, PART_ATTR_MAIN | PART_ATTR_NOT_TARGETABLE | PART_ATTR_UNTATTLEABLE, 0, kMarronPoses},
};
static BattleDataEntry kNoData[] = {{48, btldefaultevt_Dummy}, {0, NULL}};
#define WATCHER(var, pts, nm, w, h)                                                                             \
    static BattleUnitKind var = {                                                                                 \
        .unitType = 1, .name = nm, .maxHp = 99, .level = 0, .pbSoftCap = 9999, .width = w, .height = h,           \
        .centerOffset = {0.0f, h / 2, 0.0f}, .talkTogeBaseOffset = {0.0f, h, 0.0f},                              \
        .damageSfxName = "SFX_BOSS_GNB_DAMAGED1", .fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1",                         \
        .iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1", .explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1",        \
        .attributeFlags = UNIT_ATTR_NO_TURN | UNIT_ATTR_IGNORE_FOR_CLEAR | UNIT_ATTR_NOT_SAMPLED_AS_TARGET |       \
                          UNIT_ATTR_NO_HP_GAUGE,                                                                \
        .statusVulnerability = &kNoRegist, .numParts = 1, .parts = pts, .initEventCode = watcher_init,          \
        .dataTable = kNoData,                                                                                     \
    };
WATCHER(kEclair, kWatcherParts, "sl_c6_un_eclair", 30, 60)
WATCHER(kMarron, kMarronParts, "sl_c6_un_king", 70, 100)

/* the Nightmare's Page Arms: 8 HP each, never acting; while one stands the body takes half from its side */
static BattleUnitKindPart kArmParts[] = {
    {1, "sl_c6_un_arm", "c_sl_nightmare", {0.0f, 0.0f, 0.0f}, {0.0f, 10.0f, 0.0f}, {0.0f, 25.0f, 0.0f}, 20, 30, 255,
     &kDef0, &kNoAttr, PART_ATTR_MAIN | PART_ATTR_UNTATTLEABLE | PART_ATTR_HIDDEN | PART_ATTR_NO_SHADOW, 0, kArmPoses},
};
static BattleDataEntry kArmData[] = {{48, btldefaultevt_Dummy}, {0, NULL}};
static BattleUnitKind kArm = {
    .unitType = 1, .name = "sl_c6_un_arm", .maxHp = 4, .level = 0, .pbSoftCap = 9999, .width = 30, .height = 50,
    .centerOffset = {0.0f, 25.0f, 0.0f}, .talkTogeBaseOffset = {0.0f, 50.0f, 0.0f}, .hpGaugeOffset = {0, 0},
    .damageSfxName = "SFX_BOSS_GNB_DAMAGED1", .fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1",
    .iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1", .explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1",
    .attributeFlags = UNIT_ATTR_NO_TURN | UNIT_ATTR_IGNORE_FOR_CLEAR | UNIT_ATTR_FLOATING | UNIT_ATTR_NO_SHADOW,
    .statusVulnerability = &kNoRegist, .numParts = 1, .parts = kArmParts, .initEventCode = arm_init,
    .dataTable = kArmData,
};
/* the left arm (guards the body against the partner) stands to its back, the right in front */
static BattleUnitSetup kArmL = {&kArm, 1, {0}, BATTLE_PHASE_ENEMY, {85.0f, 30.0f, -15.0f}, 0, {0, 0, 0, 0}, NULL};
static BattleUnitSetup kArmR = {&kArm, 1, {0}, BATTLE_PHASE_ENEMY, {10.0f, 30.0f, 15.0f}, 0, {1, 0, 0, 0}, NULL};
static int sArmId[2] = {-1, -1};
static int sArmRegrow[2];

static BattleUnitSetup kCrepeSetup[] = {
    {&kCrepe, 1, {0}, BATTLE_PHASE_ENEMY, {50.0f, 0.0f, 0.0f}, 0, {0, 0, 0, 0}, NULL},
    {&kEclair, 1, {0}, BATTLE_PHASE_ENEMY, {125.0f, 0.0f, -40.0f}, 0, {1, 0, 0, 0}, NULL},
    {&kMarron, 1, {0}, BATTLE_PHASE_ENEMY, {170.0f, 0.0f, -65.0f}, 0, {2, 0, 0, 0}, NULL},
};
static BattleGroupSetup kCrepeGroup = {3, kCrepeSetup, 0, 0, 100, battle_heart_drop_param_default,
                                       battle_flower_drop_param_default, 0};

/* the watchers: Eclair (work 0 = 1) and the King, slumped */
EVT_BEGIN(watcher_init)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 0, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(sl6_set_id, 1)
    END_IF
    CALL(btlevtcmd_SetMaxMoveCount, BTL_UNIT_SELF, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* the arms: which side (0 left, 1 right) in work 0 */
static USER_FUNC(sl6_arm_set) {
    const s32 side = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (side == 0 || side == 1) {
        sArmId[side] = BattleTransID(event, BTL_UNIT_SELF);
    }
    return EVT_RETURN_DONE;
}

EVT_BEGIN(arm_damage)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL(sl6_grab, 0, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_OnOffStatus, BTL_UNIT_PARTNER, STATUS_STOP, 0, 0, 0)
    END_IF
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(0))
    IF_INT_LESS_OR_EQUAL(LW(0), 0)
        /* torn off: the stump shows */
        CALL(sl6_beat, STRING("a Page Arm torn off"))
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 0, LW(1))
        IF_INT_EQUAL(LW(1), 1)
            SET_VALUE(LW(5), PTR("L_1F"))
        ELSE
            SET_VALUE(LW(5), PTR("L_1B"))
        END_IF
        BEGIN_INLINE_EVENT
            WAIT_FRAMES(2)
            CALL_CHILD(nm_arms_pose)
        END_INLINE_EVENT
    END_IF
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

EVT_BEGIN(arm_init)
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(arm_damage))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, 0, LW(0))
    CALL(sl6_arm_set, LW(0))
    CALL(btlevtcmd_SetMaxMoveCount, BTL_UNIT_SELF, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* out: 1 if arm side (in) stands; regrows it after 3 turns down */
static USER_FUNC(sl6_arm_alive) {
    const s32 side = evtGetValue(event, event->args[0]);
    BattleUnit* u = sArmId[side] >= 0 ? BattleGetUnitPtr(_battleWorkPointer, sArmId[side]) : NULL;
    (void)isFirstCall;
    evtSetValue(event, event->args[1], u != NULL && u->currentHp > 0 && u->kindParams == &kArm);
    return EVT_RETURN_DONE;
}

/* the arms at the start of each Nightmare turn: out LW the side to regrow (-1 none) */
static USER_FUNC(sl6_arm_tick) {
    int side, regrow = -1;
    (void)isFirstCall;
    for (side = 0; side < 2; side++) {
        BattleUnit* u = sArmId[side] >= 0 ? BattleGetUnitPtr(_battleWorkPointer, sArmId[side]) : NULL;
        const int alive = u != NULL && u->currentHp > 0 && u->kindParams == &kArm;
        if (alive) {
            sArmRegrow[side] = 0;
        } else if (++sArmRegrow[side] >= 3 && regrow < 0) {
            sArmRegrow[side] = 0;
            regrow = side;
        }
    }
    evtSetValue(event, event->args[0], regrow);
    return EVT_RETURN_DONE;
}

/* out: the Nightmare's pose table for its arms now, and its unit */
static USER_FUNC(sl6_nm_table) {
    int side, alive[2];
    PoseTableEntry* t;
    (void)isFirstCall;
    for (side = 0; side < 2; side++) {
        BattleUnit* u = sArmId[side] >= 0 ? BattleGetUnitPtr(_battleWorkPointer, sArmId[side]) : NULL;
        alive[side] = u != NULL && u->currentHp > 0 && u->kindParams == &kArm;
    }
    /* side 1 the front (right) arm, 0 the back */
    t = alive[1] && alive[0] ? kNightmarePoses : alive[0] ? kNightmarePosesF : alive[1] ? kNightmarePosesB : kNightmarePosesN;
    evtSetValue(event, event->args[0], (EvtWord)(AddrWord)t);
    evtSetValue(event, event->args[1], sCrepeId);
    return EVT_RETURN_DONE;
}

/* in: a unit, a part, a pose table (the game's AnimeSetPoseTable reads its table raw, not from a variable) */
static USER_FUNC(sl6_pose_table) {
    BattleUnit* u = unitOf(event, evtGetValue(event, event->args[0]));
    BattleUnitPart* part = u != NULL ? BtlUnit_GetPartsPtr(u, evtGetValue(event, event->args[1])) : NULL;
    (void)isFirstCall;
    if (part != NULL) {
        part->poseTable = (PoseTableEntry*)evtGetValue(event, event->args[2]);
    }
    return EVT_RETURN_DONE;
}

/* the Nightmare's poses follow its arms; LW(5) a pose to play first (0 none) */
EVT_BEGIN(nm_arms_pose)
    CALL(sl6_nm_table, LW(6), LW(7))
    IF_INT_EQUAL(LW(7), -1)
        RETURN
    END_IF
    IF_INT_NOT_EQUAL(LW(5), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(7), BODY, LW(5))
        WAIT_FRAMES(26)
    END_IF
    CALL(sl6_pose_table, LW(7), BODY, LW(6))
    CALL(btlevtcmd_AnimeChangePoseFromTable, LW(7), BODY)
    RETURN
EVT_END()

/* (tests: $TTYD_SL6_KNEEL leaves the arms out, for the body and the Smite) */
static EVT_BEGIN(arms_spawn_l)
    SET_VALUE(LW(0), 0)
    CALL(sl6_test, LW(0), LW(1))
    IF_INT_EQUAL(LW(1), 0)
        CALL(btlevtcmd_SpawnUnit, LW(0), PTR(&kArmL), 0)
    END_IF
    RETURN
EVT_END()
static EVT_BEGIN(arms_spawn_r)
    SET_VALUE(LW(0), 0)
    CALL(sl6_test, LW(0), LW(1))
    IF_INT_EQUAL(LW(1), 0)
        CALL(btlevtcmd_SpawnUnit, LW(0), PTR(&kArmR), 0)
    END_IF
    RETURN
EVT_END()

/* in: a side; out: that arm's unit id if it stands, else -1 */
static USER_FUNC(sl6_arm_id) {
    const s32 side = evtGetValue(event, event->args[0]);
    BattleUnit* u = sArmId[side] >= 0 ? BattleGetUnitPtr(_battleWorkPointer, sArmId[side]) : NULL;
    (void)isFirstCall;
    evtSetValue(event, event->args[1], u != NULL && u->kindParams == &kArm && u->currentHp > 0 ? sArmId[side] : -1);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(sl6_arms_kill)
    CALL(sl6_arm_id, 0, LW(0))
    IF_INT_NOT_EQUAL(LW(0), -1)
        CALL(btlevtcmd_GetPos, LW(0), LW(1), LW(2), LW(3))
        CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, LW(1), LW(2), LW(3), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_KillUnit, LW(0), 0)
    END_IF
    CALL(sl6_arm_id, 1, LW(0))
    IF_INT_NOT_EQUAL(LW(0), -1)
        CALL(btlevtcmd_GetPos, LW(0), LW(1), LW(2), LW(3))
        CALL(evt_eff, STRING(""), STRING("kemuri_test"), 13, LW(1), LW(2), LW(3), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_KillUnit, LW(0), 0)
    END_IF
    RETURN
EVT_END()

EVT_BEGIN(crepe_wait)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

/* Foresight: every blow misses, a gold flash and a sidestep */
EVT_BEGIN(crepe_dodge)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("E_1"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_MARIO_AVOID1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 40)
    CALL(evt_eff64, STRING(""), STRING("stardust_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_sl_prompt, STRING("sl_c6_bt_foresight"), 1500)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

EVT_BEGIN(crepe_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(crepe_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(crepe_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(crepe_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(crepe_attack))
    CALL(btlevtcmd_SetEventPhase, BTL_UNIT_SELF, PTR(crepe_phase))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PHASE, 1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TURNS, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_NEXT, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SKIP, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STAGGER, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DOOM, 0)
    CALL(sl6_set_id, 0)
    CALL(sl6_sight, 0, 0, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* --- Crepe's moves --- */

static EVT_BEGIN(crepe_flambe)
    CALL(evt_btl_camera_set_mode, 0, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1A"))
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_1B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_DAMAGE_FIRE1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    SUBTRACT(LW(0), 30)
    ADD(LW(1), 30)
    CALL(evt_eff, STRING(""), STRING("fire"), 3, LW(0), LW(1), LW(2), FLOAT(1.5f), 60, 0, 0, 0, 0, 0, 0)
    SET_VALUE(LW(12), PTR(&kFlambe))
    SET_VALUE(LW(13), 24)
    CALL_CHILD(sl6_blow_all)
    IF_INT_EQUAL(LW(8), 1)
        /* Superguarded: the flame blows back */
        CALL(sl6_beat, STRING("the flame blows back"))
        CALL(evt_msg_print, 0, STRING("sl_c6_bt_flame_back"), 0, 0)
        CALL(btlevtcmd_OnOffStatus, BTL_UNIT_SELF, STATUS_BURN, 1, 1, 1)
        CALL(evt_btl_camera_set_mode, 0, 0)
        CALL(sl6_self_hurt, 3)
        RETURN
    END_IF
    WAIT_FRAMES(20)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

/* LW(0): the target */
static EVT_BEGIN(crepe_fold)
    SET_VALUE(LW(0), TGT_LUIGI)
    CALL_CHILD(sl6_pick_target)
    SET_VALUE(LW(12), PTR(&kFold))
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    SET_VALUE(LW(14), 40)
    CALL_CHILD(sl6_approach)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2A"))
    CALL(sl6_ring, LW(3), 22)
    WAIT_FRAMES(16)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_2B"))
    SET_VALUE(LW(13), 6)
    CALL_CHILD(sl6_blow)
    IF_INT_EQUAL(LW(6), 5)
        CALL(evt_msg_print, 0, STRING("sl_c6_bt_fold_back"), 0, 0)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SKIP, 1)
    END_IF
    WAIT_FRAMES(20)
    CALL_CHILD(sl6_home)
    RETURN
EVT_END()

static EVT_BEGIN(crepe_batter)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TARGET, LW(0))
    CALL_CHILD(sl6_pick_target)
    SET_VALUE(LW(12), PTR(&kBatter))
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    SET_VALUE(LW(14), 24) /* in reach: his fists come down on the target */
    CALL_CHILD(sl6_approach)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3A"))
    WAIT_FRAMES(14)
    /* A_3B pounds at its frames 4, 10 and 16: a blow (and a guard) at each */
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_3B"))
    SET_VALUE(LW(13), 4)
    CALL_CHILD(sl6_blow)
    CALL(btlevtcmd_GetHp, LW(3), LW(0))
    IF_INT_GREATER(LW(0), 0)
        SET_VALUE(LW(13), 6)
        CALL_CHILD(sl6_blow)
        CALL(btlevtcmd_GetHp, LW(3), LW(0))
        IF_INT_GREATER(LW(0), 0)
            SET_VALUE(LW(12), PTR(&kBatterLast))
            SET_VALUE(LW(13), 6)
            CALL_CHILD(sl6_blow)
        END_IF
    END_IF
    WAIT_FRAMES(20)
    CALL_CHILD(sl6_home)
    RETURN
EVT_END()

static EVT_BEGIN(crepe_bolt)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TARGET, LW(0))
    CALL_CHILD(sl6_pick_target)
    SET_VALUE(LW(12), PTR(&kBolt))
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_5A"))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 50)
    CALL(evt_eff64, STRING("sl_bolt_c"), STRING("akari_charge_n64"), 1, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
    CALL(sl6_ring, LW(3), 38)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_5B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_QWEN_THUNDER1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(evt_eff64, STRING("sl_bolt"), STRING("thunder_n64"), 0, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
    SET_VALUE(LW(13), 8)
    CALL_CHILD(sl6_blow)
    WAIT_FRAMES(30)
    /* these effects never end by themselves */
    CALL(evt_eff_delete, STRING("sl_bolt_c"))
    CALL(evt_eff_delete, STRING("sl_bolt"))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

/* the game's partner switch, forced (btldefaultevt_ChangeParty's, from the enemy's side) */
static EVT_BEGIN(crepe_reshuffle)
    CALL(sl6_beat, STRING("Cabinet Reshuffle"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("A_4"))
    CALL(evt_msg_print, 0, STRING("sl_c6_bt_reshuffle"), 0, 0)
    WAIT_FRAMES(30)
    CALL(sl6_ids, LW(8), LW(3))
    IF_INT_EQUAL(LW(3), -1)
        GOTO(99)
    END_IF
    CALL(sl6_reshuffle_pick, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_SetFallAccel, LW(3), FLOAT(0.69921875f))
    CALL(btlevtcmd_GetPos, LW(8), LW(5), LW(6), LW(7))
    BEGIN_SIBLING_EVENT
        LOOP(18)
            CALL(btlevtcmd_AddScale, LW(3), FLOAT(-0.049804688f), FLOAT(-0.049804688f), FLOAT(-0.049804688f))
            WAIT_FRAMES(1)
        END_LOOP
    END_SIBLING_EVENT
    CALL(evt_snd_sfxon, STRING("SFX_BTL_PARTY_IN1"), 0)
    CALL(btlevtcmd_JumpPosition, LW(3), LW(5), LW(6), LW(7), 20, 0)
    CALL(btlevtcmd_ChangeParty, LW(9))
    CALL(btlevtcmd_OffUnitFlag, LW(9), UNIT_FLAG_FLOAT_ACTIVE)
    CALL(btlevtcmd_GetBodyId, LW(9), LW(0))
    CALL(btlevtcmd_AnimeChangePoseFromTable, LW(9), LW(0))
    CALL(btlevtcmd_SetPos, LW(9), LW(5), LW(6), LW(7))
    CALL(btlevtcmd_SetFallAccel, LW(9), FLOAT(0.69921875f))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_PARTY_CHANGE1"), 0)
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_SetScale, LW(9), FLOAT(0.099609375f), FLOAT(0.099609375f), FLOAT(0.099609375f))
        LOOP(18)
            CALL(btlevtcmd_AddScale, LW(9), FLOAT(0.049804688f), FLOAT(0.049804688f), FLOAT(0.049804688f))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetScale, LW(9), FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
    END_SIBLING_EVENT
    CALL(btlevtcmd_GetHomePos, LW(9), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon, STRING("SFX_PARTY_OUT1"), 0)
    CALL(btlevtcmd_JumpPosition, LW(9), LW(0), LW(1), LW(2), 20, 0)
    CALL(btlevtcmd_OnUnitFlag, LW(9), UNIT_FLAG_FLOAT_ACTIVE)
    CALL(btlevtcmd_KillUnit, LW(3), 0)
    CALL(btlevtcmd_SortPhaseMoveTable)
    CALL(btlevtcmd_StartWaitEvent, LW(9))
    LABEL(99)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    RETURN
EVT_END()

/* --- the Nightmare's --- */

static EVT_BEGIN(nm_doom_lock)
    /* the needle points and stops */
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TARGET, LW(0))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DOOM_T, LW(0))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DOOM, 3)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_1A"))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_1C"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(evt_btl_camera_shake_h, 0, 2, 0, 20, 13)
    CALL(sl6_eclair_id, LW(1))
    IF_INT_EQUAL(LW(0), TGT_PARTNER)
        CALL(evt_msg_print, 2, STRING("sl_c6_warn_needle_p"), 0, LW(1))
    ELSE
        CALL(evt_msg_print, 2, STRING("sl_c6_warn_needle"), 0, LW(1))
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(nm_doom_strike)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DOOM, 0)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_DOOM_T, LW(0))
    CALL_CHILD(sl6_pick_target)
    SET_VALUE(LW(12), PTR(&kDoom))
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 150)
    CALL(evt_eff64, STRING("sl_nm_c"), STRING("akari_charge_n64"), 1, LW(0), LW(1), LW(2), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_QWEN_THUNDER1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_1B"))
    SET_VALUE(LW(13), 30)
    CALL_CHILD(sl6_blow)
    IF_INT_EQUAL(LW(6), 5)
        CALL(evt_msg_print, 0, STRING("sl_c6_bt_stun"), 0, 0)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SKIP, 1)
    END_IF
    CALL(evt_btl_camera_shake_h, 0, 8, 0, 20, 13)
    WAIT_FRAMES(30)
    CALL(evt_eff_delete, STRING("sl_nm_c"))
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(nm_hatesong)
    CALL(evt_msg_print, 0, STRING("sl_c6_bt_hatesong"), 0, 0)
    CALL(sl6_hayzee, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_msg_print, 2, STRING("sl_c6_bt_not_tonight"), 0, BTL_UNIT_PARTNER)
        RETURN
    END_IF
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_2A"))
    WAIT_FRAMES(24)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_2B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_ATMTLSA_SURPRISED2"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(evt_btl_camera_shake_h, 0, 4, 0, 40, 13)
    SET_VALUE(LW(12), PTR(&kHatesong))
    SET_VALUE(LW(13), 30)
    CALL_CHILD(sl6_blow_all)
    WAIT_FRAMES(20)
    RETURN
EVT_END()

/* five strikes of 1, in a rhythm a player can press to (a tenth of a second apart they could not
 * be guarded by hand): all five Superguarded and it's stunned */
static EVT_BEGIN(nm_thousand)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TARGET, LW(0))
    CALL_CHILD(sl6_pick_target)
    SET_VALUE(LW(12), PTR(&kTick))
    CALL_CHILD(sl6_cam)
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    SET_VALUE(LW(14), 50)
    CALL_CHILD(sl6_approach)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_3"))
    SET_VALUE(LW(15), 0)
    SET_VALUE(LW(13), 24)
    LOOP(5)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_GNB_DAMAGED1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL_CHILD(sl6_blow)
        IF_INT_EQUAL(LW(6), 5)
            ADD(LW(15), 1)
        END_IF
        SET_VALUE(LW(13), 16)
    END_LOOP
    IF_INT_EQUAL(LW(15), 5)
        CALL(evt_msg_print, 0, STRING("sl_c6_bt_stun"), 0, 0)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SKIP, 1)
    END_IF
    WAIT_FRAMES(20)
    CALL_CHILD(sl6_home)
    RETURN
EVT_END()

/* an arm grabs the partner and holds him until an arm is hurt */
static EVT_BEGIN(nm_grab)
    SET_VALUE(LW(0), TGT_PARTNER)
    CALL_CHILD(sl6_pick_target)
    SET_VALUE(LW(12), PTR(&kGrab))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_4A"))
    SET_VALUE(LW(13), 20)
    CALL_CHILD(sl6_blow)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_4B"))
    WAIT_FRAMES(30)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("A_4C"))
    WAIT_FRAMES(20)
    IF_INT_EQUAL(LW(7), 1)
        IF_INT_LESS(LW(6), 4)
            CALL(sl6_grab, 1, LW(0))
            CALL(sl6_eclair_id, LW(1))
            CALL(evt_msg_print, 2, STRING("sl_c6_warn_arm"), 0, LW(1))
        END_IF
    END_IF
    RETURN
EVT_END()

/* --- choosing the next move (before Luigi's turn, so it can be seen) --- */

static USER_FUNC(sl6_choose) {
    /* in: phase, HP, doom count, partner present, an arm standing; out: move, target */
    const s32 phase = evtGetValue(event, event->args[0]);
    const s32 hp = evtGetValue(event, event->args[1]);
    const s32 doom = evtGetValue(event, event->args[2]);
    const s32 partner = evtGetValue(event, event->args[3]);
    const s32 arm = evtGetValue(event, event->args[4]);
    int mv, tgt, r = rand() % 100;
    (void)isFirstCall;
    if (getenv("TTYD_SL6_MOVE") != NULL && phase < 3) {
        mv = atoi(getenv("TTYD_SL6_MOVE")); /* tests: Crepe's move, every turn */
    } else if (phase < 3) {
        if (phase == 2 && hp < 25 && r < 30) {
            mv = MV_BOLT;
        } else {
            r = rand() % 100;
            mv = r < 30 ? MV_FLAMBE : r < 55 ? MV_FOLD : r < 70 ? MV_RESHUFFLE : MV_BATTER;
        }
        if (mv == MV_RESHUFFLE && !partner) {
            mv = MV_BATTER;
        }
    } else if (doom == 1) {
        mv = MV_DOOM_STRIKE;
    } else {
        r = rand() % 100;
        mv = (doom == 0 && r < 30) ? MV_DOOM : r < 50 ? MV_HATESONG : r < 75 ? MV_THOUSAND : MV_GRAB;
        if (mv == MV_GRAB && (!partner || !arm)) {
            mv = MV_THOUSAND;
        }
    }
    switch (mv) {
        case MV_FLAMBE:
        case MV_HATESONG: tgt = TGT_ALL; break;
        case MV_FOLD: tgt = TGT_LUIGI; break;
        case MV_RESHUFFLE: tgt = TGT_NONE; break;
        case MV_GRAB: tgt = TGT_PARTNER; break;
        case MV_DOOM_STRIKE: tgt = -1; break;
        default: tgt = partner && (rand() & 1) ? TGT_PARTNER : TGT_LUIGI; break;
    }
    evtSetValue(event, event->args[5], mv);
    evtSetValue(event, event->args[6], tgt);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(crepe_choose)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(0))
    CALL(btlevtcmd_GetHp, BTL_UNIT_SELF, LW(1))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_DOOM, LW(2))
    CALL(sl6_ids, LW(5), LW(3))
    IF_INT_NOT_EQUAL(LW(3), -1)
        SET_VALUE(LW(3), 1)
    ELSE
        SET_VALUE(LW(3), 0)
    END_IF
    SET_VALUE(LW(4), 0)
    CALL(sl6_arm_alive, 0, LW(6))
    CALL(sl6_arm_alive, 1, LW(7))
    IF_INT_EQUAL(LW(6), 1)
        SET_VALUE(LW(4), 1)
    END_IF
    IF_INT_EQUAL(LW(7), 1)
        SET_VALUE(LW(4), 1)
    END_IF
    CALL(sl6_choose, LW(0), LW(1), LW(2), LW(3), LW(4), LW(5), LW(6))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_NEXT, LW(5))
    IF_INT_EQUAL(LW(5), MV_DOOM_STRIKE)
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_DOOM_T, LW(6))
    END_IF
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TARGET, LW(6))
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_SKIP, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(sl6_vision, MV_REST, TGT_NONE, LW(2))
    ELSE
        CALL(sl6_vision, LW(5), LW(6), LW(2))
    END_IF
    RETURN
EVT_END()

/* --- the tiara (beat 8) --- */

static EVT_BEGIN(crepe_tiara)
    CALL(sl6_beat, STRING("the tiara: Future Sight"))
    CALL(btlevtcmd_PhaseEventStartDeclare, BTL_UNIT_SELF)
    CALL(btlevtcmd_StatusWindowOnOff, 0)
    CALL(evt_btl_camera_set_mode, 0, 1)
    SAYB("sl_c6_sight_00", BTL_UNIT_SELF)
    CALL(sl6_eclair_id, LW(9))
    CALL(btlevtcmd_AnimeChangePose, LW(9), 1, STRING("Y_1"))
    SAYB("sl_c6_sight_01", LW(9))
    CALL(btlevtcmd_AnimeChangePose, LW(9), 1, STRING("TI_1"))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePose, LW(9), 1, STRING("TI_2"))
    CALL(btlevtcmd_GetPos, LW(9), LW(0), LW(1), LW(2))
    ADD(LW(1), 70)
    CALL(evt_eff64, STRING("sl_tiara"), STRING("akari_charge_n64"), 1, LW(0), LW(1), LW(2), FLOAT(3.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
    SAYB("sl_c6_sight_02", LW(9))
    /* light pours into Luigi */
    CALL(sl6_ids, LW(3), LW(4))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 30)
    CALL(evt_eff64, STRING(""), STRING("stardust_n64"), 2, LW(0), LW(1), LW(2), FLOAT(20.0f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_snd_sfxon, STRING("SFX_BOSS_KAMEBABA_MAGIC1"), 0)
    CALL(btlevtcmd_SetRGB, LW(3), 1, 255, 230, 120)
    WAIT_FRAMES(30)
    CALL(btlevtcmd_SetRGB, LW(3), 1, 255, 255, 255)
    CALL(evt_eff_delete, STRING("sl_tiara"))
    CALL(btlevtcmd_AnimeChangePose, LW(9), 1, STRING("B_1"))
    CALL(sl6_sight, 1, 0, 0)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("D_1"))
    SAYB("sl_c6_sight_03", BTL_UNIT_SELF)
    SAYB("sl_c6_sight_04", BTL_UNIT_MARIO)
    SAYB("sl_c6_sight_05", BTL_UNIT_MARIO)
    /* phase 2: seen; the bar goes down now */
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PHASE, 2)
    CALL(btlevtcmd_OffPartsAttribute, BTL_UNIT_SELF, BODY, PART_ATTR_EVASIVE)
    CALL(sl6_set_max_hp, BTL_UNIT_SELF, 30)
    CALL(evt_snd_bgmoff_f, PSND_FADE_OUT | PSND_CH(1), 800)
    CALL(evt_snd_bgmon, PSND_FADE_OUT | PSND_CH(1), STRING("BGM_BOSS_STG8_3RDHEAD1"))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StatusWindowOnOff, 1)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

EVT_BEGIN(crepe_phase)
    CALL(btlevtcmd_CheckPhase, LW(0), BATTLE_PHASE_BEFORE_PLAYER)
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_TURNS, LW(1))
        CALL(sl6_luigi_low, LW(2))
        IF_INT_GREATER_OR_EQUAL(LW(1), 2)
            SET_VALUE(LW(2), 1)
        END_IF
        /* tests: $TTYD_SL6_KNEEL rushes the tiara too */
        SET_VALUE(LW(3), 0)
        CALL(sl6_test, LW(3), LW(4))
        IF_INT_EQUAL(LW(4), 1)
            SET_VALUE(LW(2), 1)
        END_IF
        IF_INT_EQUAL(LW(2), 0)
            RETURN
        END_IF
        CALL_CHILD(crepe_tiara)
        CALL_CHILD(crepe_choose)
        CALL(sl6_sight, 1, 1, 0)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    /* Future Sight: the next move, before Luigi's turn */
    CALL_CHILD(crepe_choose)
    CALL(sl6_sight, 1, 1, 0)
    RETURN
EVT_END()

EVT_BEGIN(crepe_attack)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(10))
    IF_INT_GREATER_OR_EQUAL(LW(10), 2)
        CALL(sl6_sight, 1, 0, 0)
    ELSE
        CALL(sl6_sight, 0, 0, 0)
    END_IF
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURNS, 1)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    IF_INT_EQUAL(LW(10), 3)
        /* the arms regrow after 3 turns; the doom counts down */
        CALL(sl6_arm_tick, LW(0))
        IF_INT_NOT_EQUAL(LW(0), -1)
            CALL(sl6_beat, STRING("a Page Arm regrows"))
        END_IF
        IF_INT_EQUAL(LW(0), 0)
            CALL_CHILD(arms_spawn_l)
            SET_VALUE(LW(5), PTR("G_1B"))
            CALL_CHILD(nm_arms_pose)
        END_IF
        IF_INT_EQUAL(LW(0), 1)
            CALL_CHILD(arms_spawn_r)
            SET_VALUE(LW(5), PTR("G_1F"))
            CALL_CHILD(nm_arms_pose)
        END_IF
        CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_DOOM, LW(0))
        IF_INT_GREATER(LW(0), 1)
            SUBTRACT(LW(0), 1)
            CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DOOM, LW(0))
        END_IF
        CALL(sl6_rand, 4, LW(1))
        SWITCH(LW(1))
            CASE_EQUAL(0)
                CALL(evt_msg_print, 0, STRING("sl_c6_nm_word_00"), 0, 0)
            CASE_EQUAL(1)
                CALL(evt_msg_print, 0, STRING("sl_c6_nm_word_01"), 0, 0)
            CASE_EQUAL(2)
                CALL(evt_msg_print, 0, STRING("sl_c6_nm_word_02"), 0, 0)
            CASE_DEFAULT
                CALL(evt_msg_print, 0, STRING("sl_c6_nm_word_03"), 0, 0)
        END_SWITCH
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_SKIP, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SKIP, 0)
        IF_INT_EQUAL(LW(10), 3)
            CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("X_1"))
            CALL(evt_msg_print, 0, STRING("sl_c6_bt_stun"), 0, 0)
        ELSE
            CALL(evt_msg_print, 0, STRING("sl_c6_bt_skip"), 0, 0)
        END_IF
        CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
        RETURN
    END_IF
    /* phase 1: chosen now (unseen); later, as the vision showed */
    IF_INT_EQUAL(LW(10), 1)
        CALL_CHILD(crepe_choose)
        CALL(sl6_rand, 3, LW(1))
        SWITCH(LW(1))
            CASE_EQUAL(0)
                SAYB("sl_c6_taunt_00", BTL_UNIT_SELF)
            CASE_EQUAL(1)
                SAYB("sl_c6_taunt_01", BTL_UNIT_SELF)
            CASE_DEFAULT
                SAYB("sl_c6_taunt_02", BTL_UNIT_SELF)
        END_SWITCH
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_NEXT, LW(0))
    SWITCH(LW(0))
        CASE_EQUAL(MV_FLAMBE)
            CALL_CHILD(crepe_flambe)
        CASE_EQUAL(MV_FOLD)
            CALL_CHILD(crepe_fold)
        CASE_EQUAL(MV_RESHUFFLE)
            CALL_CHILD(crepe_reshuffle)
        CASE_EQUAL(MV_BOLT)
            CALL_CHILD(crepe_bolt)
        CASE_EQUAL(MV_DOOM)
            CALL_CHILD(nm_doom_lock)
        CASE_EQUAL(MV_DOOM_STRIKE)
            CALL_CHILD(nm_doom_strike)
        CASE_EQUAL(MV_HATESONG)
            CALL_CHILD(nm_hatesong)
        CASE_EQUAL(MV_THOUSAND)
            CALL_CHILD(nm_thousand)
        CASE_EQUAL(MV_GRAB)
            CALL_CHILD(nm_grab)
        CASE_DEFAULT
            CALL_CHILD(crepe_batter)
    END_SWITCH
    CALL(sl6_eclair_id, LW(9))
    CALL(sl6_rand, 4, LW(1))
    IF_INT_EQUAL(LW(1), 0)
        CALL(btlevtcmd_AnimeChangePose, LW(9), 1, STRING("Y_1"))
        SAYB("sl_c6_cheer", LW(9))
        CALL(btlevtcmd_AnimeChangePose, LW(9), 1, STRING("B_1"))
    END_IF
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* 9. The Nightmare: the six pieces spin into him and he unfolds */
static EVT_BEGIN(crepe_unfold)
    CALL(sl6_beat, STRING("Crepe unfolds into the Nightmare"))
    CALL(sl6_sight, 1, 0, 0)
    CALL(btlevtcmd_StatusWindowOnOff, 0)
    CALL(evt_btl_camera_set_mode, 0, 1)
    CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("D_1"))
    SAYB("sl_c6_nightmare_00", BTL_UNIT_SELF)
    CALL(evt_snd_bgmoff_f, PSND_FADE_OUT | PSND_CH(1), 800)
    CALL(evt_snd_bgmon, PSND_FADE_OUT | PSND_CH(1), STRING("BGM_BOSS_STG8_LAST_BOSS1"))
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("D_2"))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 40)
    LOOP(6)
        CALL(evt_eff64, STRING(""), STRING("stardust_n64"), 2, LW(0), LW(1), LW(2), 0, 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_snd_sfxon, STRING("SFX_STAR_POINT_GET1"), 0)
        WAIT_FRAMES(10)
    END_LOOP
    CALL(evt_btl_camera_shake_h, 0, 6, 0, 60, 13)
    CALL(sl6_nightmare_model, LW(5))
    IF_INT_EQUAL(LW(5), 1)
        WAIT_FRAMES(40)
        CALL(btlevtcmd_AnimeChangeGroup, BTL_UNIT_SELF, BODY, STRING("c_sl_nightmare"))
        CALL(btlevtcmd_AnimeSetPoseTable, BTL_UNIT_SELF, BODY, PTR(kNightmarePoses))
        /* the cut model is its own size (58 units to its spikes, 2.4x Luigi): at 1.5 its head left the screen */
        CALL(btlevtcmd_SetBaseScale, BTL_UNIT_SELF, FLOAT(1.0f), FLOAT(1.0f), FLOAT(1.0f))
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("E_1"))
        WAIT_FRAMES(50)
    ELSE
        /* till the Nightmare's model lands: Crepe unfolded, huge and dark */
        SET_VALUE(LW(6), FLOAT(1.0f))
        LOOP(30)
            ADD_FLOAT(LW(6), FLOAT(0.05f))
            CALL(btlevtcmd_SetBaseScale, BTL_UNIT_SELF, LW(6), LW(6), LW(6))
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetRGB, BTL_UNIT_SELF, BODY, 90, 60, 140)
    END_IF
    CALL(btlevtcmd_ReplaceParts, BTL_UNIT_SELF, BODY, PTR(kNightmareParts), BODY)
    CALL(sl6_set_max_hp, BTL_UNIT_SELF, 30)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_PHASE, 3)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_DOOM, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_SKIP, 0)
    CALL_CHILD(arms_spawn_l)
    CALL_CHILD(arms_spawn_r)
    WAIT_FRAMES(20)
    SAYB("sl_c6_nightmare_01", BTL_UNIT_SELF)
    /* no friend out: no line (the unit lookup would crash) */
    CALL(btlevtcmd_GetUnitId, BTL_UNIT_PARTNER, LW(0))
    IF_INT_NOT_EQUAL(LW(0), -1)
        SAYB("sl_c6_nightmare_02", BTL_UNIT_PARTNER)
    END_IF
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_StatusWindowOnOff, 1)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL_CHILD(crepe_choose)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* 10. Smite: the last action is Luigi's */
static EVT_BEGIN(nm_smite_ready)
    CALL(sl6_beat, STRING("the Smite"))
    CALL(btlevtcmd_SetHp, BTL_UNIT_SELF, 1)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STAGGER, 1)
    CALL(sl6_vision, 0, TGT_NONE, 0)
    CALL(sl6_sight, 1, 0, 1)
    CALL(evt_sl_force_command, 0, PORT_SL_ACT_HAMMER, 0, STRING("sl_c6_bt_smite"))
    CALL(evt_sl_force_command, 1, PORT_SL_ACT_TACTICS, 0, 0)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static EVT_BEGIN(nm_shatter)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_STAGGER, 2)
    CALL(sl6_sight, 1, 0, 0)
    CALL(evt_sl_force_command, 0, -1, 0, 0)
    CALL(evt_sl_force_command, 1, -1, 0, 0)
    CALL(btlevtcmd_StatusWindowOnOff, 0)
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 50)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("C_1"))
    WAIT_FRAMES(40)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, BODY, STRING("C_2"))
    CALL(evt_snd_sfxon, STRING("SFX_BTL_SYS_BREAK1"), 0)
    CALL(evt_eff64, STRING(""), STRING("glass_n64"), 0, LW(0), LW(1), LW(2), FLOAT(1.0f), 30, 0, 0, 0, 0, 0, 0)
    CALL(evt_msg_print, 0, STRING("sl_c6_smite_00"), 0, 0)
    LOOP(6)
        CALL(evt_eff64, STRING(""), STRING("stardust_n64"), 2, LW(0), LW(1), LW(2), FLOAT(20.0f), 0, 0, 0, 0, 0, 0, 0)
        WAIT_FRAMES(8)
    END_LOOP
    CALL_CHILD(sl6_arms_kill)
    WAIT_FRAMES(40)
    CALL_CHILD(sl6_reward)
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
    RETURN
EVT_END()

EVT_BEGIN(crepe_damage)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL_CHILD(nm_shatter)
        RETURN
    END_IF
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    RETURN
EVT_END()

/* at 0 HP: phase 2 unfolds into the Nightmare; the Nightmare waits for the Smite */
EVT_BEGIN(crepe_dead)
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_PHASE, LW(0))
    IF_INT_LESS(LW(0), 3)
        CALL_CHILD(crepe_unfold)
        RETURN
    END_IF
    CALL(btlevtcmd_GetUnitWork, BTL_UNIT_SELF, UW_STAGGER, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL_CHILD(nm_smite_ready)
        RETURN
    END_IF
    CALL_CHILD(nm_shatter)
    RETURN
EVT_END()

/* ======================================================================
 * Hooks
 * ====================================================================== */

/* in: the next field battle's boss (ARM_*), 0 off */
USER_FUNC(evt_sl6_boss_arm) {
    (void)isFirstCall;
    sArm = evtGetValue(event, event->args[0]);
    if (sArm == ARM_WAILER) {
        sHayzee = sTune = 0;
    }
    return EVT_RETURN_DONE;
}

/* Btl_UnitSetup: the boss's group, or the room's formation */
static int unitSetupPre(BOOL* result, BattleWork** wp) {
    NpcBattleInfo* info = (*wp)->fbatInfo != NULL ? (*wp)->fbatInfo->setup : NULL;
    (void)result;
    if (portSuperLuigiChapter() != 6 || info == NULL) {
        return 0;
    }
    switch (sArm) {
        case ARM_WAILER:
            info->bgmName = "BGM_CHUBOSS_BATTLE1";
            info->battleFlags |= NPC_BATTLE_NO_ESCAPE;
            break;
        case ARM_KING:
            info->groupSetup = &kKingGroup;
            info->bgmName = "BGM_BOSS_STG4_RUNPELL1";
            info->battleFlags |= NPC_BATTLE_NO_ESCAPE;
            break;
        case ARM_CREPE:
            info->groupSetup = &kCrepeGroup;
            info->bgmName = "BGM_BOSS_STG3_GANCE1";
            info->battleFlags |= NPC_BATTLE_NO_ESCAPE;
            sCrepeId = sEclairId = -1;
            sArmId[0] = sArmId[1] = -1;
            sArmRegrow[0] = sArmRegrow[1] = 0;
            sGrab = 0;
            break;
        default:
            if (formationFor(info)) {
                printf("[superluigi] chapter 6: %s's formation\n", gp->currentMapName);
                fflush(stdout);
            }
            return 0;
    }
    printf("[superluigi] chapter 6: boss %d\n", sArm);
    fflush(stdout);
    return 0;
}

/* BtlUnit_Entry: the Wailer gets its phase event (tint, Hatesong) */
static void unitEntryPost(BattleUnit** result, BattleUnitSetup* setup) {
    BattleUnit* u = *result;
    (void)setup;
    if (u == NULL || portSuperLuigiChapter() != 6) {
        return;
    }
    /* tests: $TTYD_SL6_HP starts the bosses on that much HP */
    if (getenv("TTYD_SL6_HP") != NULL && (u->kindParams == &kKing || u->kindParams == &kCrepe ||
                                          u->currentKind == UNIT_ATOMIC_BOO)) {
        u->currentHp = (s16)atoi(getenv("TTYD_SL6_HP"));
    }
    if (sArm != ARM_WAILER || u->currentKind != UNIT_ATOMIC_BOO) {
        return;
    }
    u->phaseEventCode = wailer_phase;
}

/* BattleCalculateDamage: the Skull's double, the arms' guard, Hayzee's tune; the Wailer's own attack noted */
static void calculateDamagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                                BattleWeapon* weapon, u32* out, u32 flags) {
    (void)out, (void)flags;
    if (!inCh6Battle() || attacker == NULL || target == NULL) {
        return;
    }
    if (getenv("TTYD_SL_DEBUG") != NULL) {
        /* balance: every blow of the chapter's fights (before this hook's own changes) */
        printf("[sl6] %d hits %d (HP %d/%d) for %d\n", (int)attacker->currentKind, (int)target->currentKind,
               (int)target->currentHp, (int)target->maxHp, (int)*result);
        fflush(stdout);
    }
    if (sArm == ARM_WAILER && target->currentKind == UNIT_ATOMIC_BOO) {
        if (attacker->currentKind == UNIT_FLURRIE && hayzeeOut()) {
            sHayzee = 1;
            if (weapon != NULL && weapon->sleepChance > 0) {
                /* no sleep: it listens (statusDamagePre keeps the song's sleep off it) */
                if (!sTune) {
                    portSlPrompt("sl_c6_bt_listen", 1800);
                }
                sTune = 1;
                *result = 0;
            }
        }
        if (sTune && *result > 0) {
            *result += 1;
        }
    }
    if (target->kindParams == &kKing && part != NULL && part->kindPartParams != NULL &&
        part->kindPartParams->partNum == SKULL && *result > 0) {
        *result *= 2;
        portSlPrompt("sl_c6_bt_skull", 1200);
    }
    if (target->kindParams == &kCrepe && part != NULL && part->kindPartParams == &kNightmareParts[0] && *result > 1) {
        BattleUnit* m = BattleGetMarioPtr(_battleWorkPointer);
        const int fromLuigi = attacker == m;
        const int side = fromLuigi ? 1 : 0;
        BattleUnit* arm = sArmId[side] >= 0 ? BattleGetUnitPtr(_battleWorkPointer, sArmId[side]) : NULL;
        if (arm != NULL && arm->kindParams == &kArm && arm->currentHp > 0) {
            *result /= 2;
        }
    }
}

/* While chapter 6 runs, the game's names it borrows are ours: the Steeple Key the parrot drops is
 * the Crown Key (its name and description), the Atomic Boo is the Wailer (its battle name and
 * tattle). Put back as the chapter ends. */
static void namesFrame(void) {
    static const char* const kSwap[][2] = {
        {"in_key_daimu", "sl_c6_crown_key"},        {"msg_key_daimu", "sl_c6_crown_key_h"},
        {"btl_un_atmic_teresa", "sl_c6_wailer_name"}, {"btl_hlp_atomic_teresa", "sl_c6_wailer_tattle"},
    };
    static int sOn;
    const int on = portSuperLuigiChapter() == 6;
    int i;
    if (on == sOn) {
        return;
    }
    sOn = on;
    for (i = 0; i < (int)(sizeof(kSwap) / sizeof(kSwap[0])); i++) {
        portModsSetMessage(kSwap[i][0], on ? msgSearch(kSwap[i][1]) : NULL);
    }
}

/* BattleSetStatusDamageFromWeapon: Hayzee's Lullaby gives the Wailer no sleep (it's a tune to it) */
static int statusDamagePre(s32* result, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                           BattleWeapon** weapon, u32* flags) {
    (void)part, (void)flags;
    if (!inCh6Battle() || sArm != ARM_WAILER || *attacker == NULL || *target == NULL || *weapon == NULL) {
        return 0;
    }
    if ((*target)->currentKind == UNIT_ATOMIC_BOO && (*attacker)->currentKind == UNIT_FLURRIE && hayzeeOut() &&
        (*weapon)->sleepChance > 0) {
        *result = 0;
        return 1;
    }
    return 0;
}

/* the Wailer's own attack, kept as its init sets it (before the phase event swaps it) */
static void seqMainPost(void) {
    BattleWork* wp;
    int i;
    namesFrame();
    sightFrame();
    if (!inCh6Battle() || sArm != ARM_WAILER || (wp = _battleWorkPointer) == NULL) {
        return;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u != NULL && u->currentKind == UNIT_ATOMIC_BOO && u->attackEventCode != NULL &&
            u->attackEventCode != (void*)wailer_attack) {
            sWailerOwn = u->attackEventCode;
        }
    }
}

static void battleEndPost(void) {
    sSight = sVision = sSmiteRing = 0;
    sRing.unit = -1;
    sNextMove = 0;
    sWailerOwn = NULL;
}

void portSlCh6BossRegister(void) {
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    portModHostAddHook(1, kSlot_BtlUnit_Entry, (void*)unitEntryPost);
    portModHostAddHook(1, kSlot_BattleCalculateDamage, (void*)calculateDamagePost);
    portModHostAddHook(0, kSlot_BattleSetStatusDamageFromWeapon, (void*)statusDamagePre);
    portModHostAddHook(1, kSlot_seqMain, (void*)seqMainPost);
    portModHostAddHook(1, kSlot_BattleEnd, (void*)battleEndPost);
}
