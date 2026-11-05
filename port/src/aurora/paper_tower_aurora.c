/* The Paper Tower (port/include/port/postgame.h): a post-game climb of 30
 * floors made of the game's own rooms, remixed.
 *
 * Each floor is one of the game's corridor rooms (two road ways on opposite
 * sides, from the rels' BeroEntry tables), read as a map of the host's own
 * (portModsAddHostMap: the room's files and its area's REL, a script of the
 * tower's), so none of the room's people, enemies or story runs: its ground
 * only. Mario comes in by one way and leaves by the other, the game's own
 * ways (BeroEntry), which the tower points at the next floor. Across the way
 * between them stand the floor's challenges, on lines from one way to the
 * other (their places from the two ways' own hit objects, so they fall
 * inside the room whatever its shape):
 *   Paper-mode gaps  paper slats Mario only slips through in Paper mode
 *   Tube-mode gaps   a low wall he only rolls under in Tube mode
 *   wind walls       a gust that blows him back unless he turns side-on (Paper mode)
 * Every fifth floor is a battle, one of the Pit of 100 Trials' own (deeper
 * the higher the floor), fought on a Pit room with a stand-in NPC as the
 * modes' fights are; the 30th is the last. Walking back out of a floor's
 * way in starts the floor again.
 *
 * The rooms, their order, the challenges and the battles come from the
 * climb's seed ("paper_tower_seed"; 0: the day's, port/include/port/date_seed.h).
 * Changes play (Mario moved, a battle, coins): not during online co-op or a
 * replay. Best floor and time: <user>/paper_tower.txt. The gates are drawn
 * on the 2D layer without the game's font (no random numbers drawn). */
#include "port/postgame.h"
#include "port/date_seed.h"
#include "port/modes.h"
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/online.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/view.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/hitdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "evt/evt_snd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "pmario_sound.h"

#include <SDL3/SDL_timer.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- the rooms --------------------------------------------------------------- */

/* Corridor rooms: a way in the west and one in the east ("w_bero",
 * "e_bero"), the room's own. Rooms whose way across needs the story's
 * objects (a bridge raised by a script) are left out. */
static const char* const kRooms[] = {
    "hei_02", "hei_03", "hei_04", "hei_05", "hei_06", "nok_00", "gon_03", "gon_06", "gon_07", "gon_09",
    "win_00", "win_04", "mri_08", "mri_11", "usu_01", "muj_03", "muj_05", "dou_00", "dou_01", "rsh_02",
    "rsh_04", "bom_03", "moo_00", "moo_01", "moo_05", "aji_00", "aji_16", "las_00", "las_01", "las_02",
    "las_04", "gra_00", "gra_03",
};
#define ROOMS ((int)(sizeof(kRooms) / sizeof(kRooms[0])))
#define BATTLE_MAP "jon_tw"
#define BATTLE_FILES "jon_00"

static char sHostNames[ROOMS][8]; /* "hei_w00" ... the host's maps, one per room */

/* --- the climb's plan -------------------------------------------------------- */

#define GATES 3

typedef struct Gate {
    int kind;      /* PORT_TOWER_PAPER, _TUBE or _WIND */
    float at;      /* where on the way across, 0-1 */
} Gate;

typedef struct Floor {
    int room;      /* kRooms index; -1 a battle */
    int reverse;   /* in by the east way, out by the west */
    int challenge; /* PORT_TOWER_* (MIXED: more than one kind) */
    int gateCount;
    Gate gates[GATES];
    int battle;    /* a battle floor's btlsetup_jon_tbl entry */
} Floor;

static Floor sPlan[PORT_TOWER_FLOORS + 1]; /* 1-30 */
static unsigned sSeed;
static int sActive, sFloor, sHelped;
static char sReturnMap[16];
static Uint64 sStartedAt;
static int sStartAsked, sStopAsked, sSkipAsked; /* __atomic */
static int sResultCount, sResultFloor, sResultCleared;
static unsigned long long sResultMs;
static int sBestFloor, sBestRead;
static unsigned long long sBestMs;

static void plan(unsigned seed) {
    PortSeedRand r;
    int order[ROOMS], i, k = 0;
    portSeedRandInit(&r, seed);
    for (i = 0; i < ROOMS; i++) {
        order[i] = i;
    }
    for (i = ROOMS - 1; i > 0; i--) {
        const int j = (int)portSeedRandNext(&r, (unsigned)(i + 1));
        const int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    memset(sPlan, 0, sizeof(sPlan));
    for (i = 1; i <= PORT_TOWER_FLOORS; i++) {
        Floor* f = &sPlan[i];
        if (i % 5 == 0) {
            /* the Pit's sets of five (BTLNO_JON_x1..x5 at 1 + 5x): a deeper set each time */
            static const int kSet[6] = {2, 4, 5, 6, 8, 9};
            f->room = -1;
            f->challenge = PORT_TOWER_BATTLE;
            f->battle = 1 + 5 * kSet[i / 5 - 1] + (int)portSeedRandNext(&r, 5);
            continue;
        }
        f->room = order[k++ % ROOMS];
        f->reverse = (int)portSeedRandNext(&r, 2);
        /* the first floors one gate, then two, then three; the kinds mixed from the tenth */
        f->gateCount = i < 5 ? 1 : i < 15 ? 2 : 3;
        {
            const int base = PORT_TOWER_PAPER + (int)portSeedRandNext(&r, 3);
            int g, mixed = 0;
            for (g = 0; g < f->gateCount; g++) {
                f->gates[g].kind = i < 10 ? base : PORT_TOWER_PAPER + (int)portSeedRandNext(&r, 3);
                f->gates[g].at = (float)(g + 1) / (float)(f->gateCount + 1);
                mixed |= f->gates[g].kind != f->gates[0].kind;
            }
            f->challenge = mixed ? PORT_TOWER_MIXED : f->gates[0].kind;
        }
    }
}

static const char* floorMap(int floor) {
    if (floor < 1 || floor > PORT_TOWER_FLOORS) {
        return NULL;
    }
    return sPlan[floor].room < 0 ? BATTLE_MAP : sHostNames[sPlan[floor].room];
}

/* the way Mario comes into a floor by (a battle floor: none) */
static const char* floorEntrance(int floor) {
    if (floor < 1 || floor > PORT_TOWER_FLOORS || sPlan[floor].room < 0) {
        return NULL;
    }
    return sPlan[floor].reverse ? "e_bero" : "w_bero";
}

/* --- the best ---------------------------------------------------------------- */

static void bestPath(char* out, size_t size) {
    snprintf(out, size, "%s/paper_tower.txt", portSettingsUserPath());
}

static void readBest(void) {
    char p[1024];
    FILE* f;
    if (sBestRead) {
        return;
    }
    sBestRead = 1;
    bestPath(p, sizeof(p));
    f = fopen(p, "r");
    if (f != NULL) {
        if (fscanf(f, "floor %d ms %llu", &sBestFloor, &sBestMs) != 2) {
            sBestFloor = 0;
            sBestMs = 0;
        }
        fclose(f);
    }
}

static void writeBest(void) {
    char p[1024];
    FILE* f;
    bestPath(p, sizeof(p));
    f = fopen(p, "w");
    if (f != NULL) {
        fprintf(f, "floor %d ms %llu\n", sBestFloor, sBestMs);
        fclose(f);
    }
}

/* --- the floors' scripts ----------------------------------------------------- */

USER_FUNC(evt_tower_floor);
USER_FUNC(evt_tower_battle);
USER_FUNC(evt_tower_battle_end);

/* the floor's two ways, pointed by evt_tower_floor */
static BeroEntry sBeros[3];
static char sNextMap[16];

static EVT_BEGIN(tower_floor_init)
    CALL(evt_tower_floor, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(evt_bero_get_info)
    CALL_CHILD(evt_bero_info_run)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG0_100DN1"))
    RETURN
EVT_END()

static EVT_BEGIN(tower_battle_evt)
    WAIT_FRAMES(20)
    CALL(evt_tower_battle, LW(0))
    IF_INT_EQUAL(LW(0), -1)
        RETURN
    END_IF
    CALL(evt_npc_entry, STRING("tower_foe"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("tower_foe"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("tower_foe"), 0, -1000, 0)
    CALL(evt_npc_set_battle_info, STRING("tower_foe"), LW(0))
    CALL(evt_npc_battle_start, STRING("tower_foe"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_tower_battle_end, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(tower_battle_init)
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_STG0_100DN1"))
    SPAWN_EVENT(tower_battle_evt)
    RETURN
EVT_END()

/* The floor this map is: the next one on the plan that uses it, or this one
 * again (Mario walked back out of its way in). */
static int floorOfMap(const char* map) {
    int f;
    if (sFloor >= 1 && strcmp(floorMap(sFloor), map) == 0) {
        return sFloor;
    }
    for (f = sFloor + 1; f <= PORT_TOWER_FLOORS; f++) {
        if (strcmp(floorMap(f), map) == 0) {
            return f;
        }
    }
    return 0;
}

static void beroSet(BeroEntry* b, const char* hit, const char* map, const char* bero) {
    memset(b, 0, sizeof(*b));
    b->hitName = hit;
    b->directionId = BERO_DIR_AUTO;
    b->centerPosition = BERO_POS_AUTO;
    b->length = -1;
    b->caseType = -1;
    b->nextMapName = map;
    b->nextBeroName = bero;
    b->enterAnimeType = 1;
    b->exitAnimeType = 1;
}

/* Gates' places: the two ways' hit objects (the map is in) */
static Vec sFrom, sTo;
static int sGatesLive;
static Vec sPrev;     /* Mario's place last frame (the gates' crossing test) */
static int sPrevLive; /* 0: none yet in this room */

/* out: the floor's ways (0: not a climb's floor) */
USER_FUNC(evt_tower_floor) {
    const int floor = sActive ? floorOfMap(gp->currentMapName) : 0;
    (void)isFirstCall;
    if (floor == 0 || sPlan[floor].room < 0) {
        evtSetValue(event, event->args[0], 0);
        return EVT_RETURN_DONE;
    }
    sFloor = floor;
    {
        const Floor* f = &sPlan[floor];
        const char* in = f->reverse ? "e_bero" : "w_bero";
        const char* out = f->reverse ? "w_bero" : "e_bero";
        const char* next = floor < PORT_TOWER_FLOORS ? floorMap(floor + 1) : NULL;
        snprintf(sNextMap, sizeof(sNextMap), "%s", next != NULL ? next : BATTLE_MAP);
        /* the way in: this floor again; the way out: the next floor, by its way in */
        beroSet(&sBeros[0], in, sHostNames[f->room], in);
        beroSet(&sBeros[1], out, sNextMap, floorEntrance(floor + 1) != NULL ? floorEntrance(floor + 1) : "");
        memset(&sBeros[2], 0, sizeof(sBeros[2]));
        hitObjGetPos(in, &sFrom);
        hitObjGetPos(out, &sTo);
        sGatesLive = !(sFrom.x == sTo.x && sFrom.z == sTo.z);
        sPrevLive = 0; /* a new room: no crossing from the last one's place */
    }
    printf("[tower] floor %d: %s (%s), %d gates\n", floor, kRooms[sPlan[floor].room], gp->currentMapName,
           sPlan[floor].gateCount);
    fflush(stdout);
    evtSetValue(event, event->args[0], (EvtWord)sBeros);
    return EVT_RETURN_DONE;
}

/* out: the battle (-1: not a climb's battle floor) */
USER_FUNC(evt_tower_battle) {
    const int floor = sActive ? floorOfMap(gp->currentMapName) : 0;
    (void)isFirstCall;
    sGatesLive = 0;
    if (floor == 0 || sPlan[floor].room >= 0) {
        evtSetValue(event, event->args[0], -1);
        return EVT_RETURN_DONE;
    }
    sFloor = floor;
    printf("[tower] floor %d: battle %d\n", floor, sPlan[floor].battle);
    fflush(stdout);
    evtSetValue(event, event->args[0], sPlan[floor].battle);
    return EVT_RETURN_DONE;
}

static void endClimb(int cleared, int goHome);

static void goToFloor(int floor) {
    const char* bero = floorEntrance(floor);
    seqSetSeq(SEQ_MAP_CHANGE, floorMap(floor), bero);
}

/* in: the battle's result (1 won, 3 lost, 4 fled) */
USER_FUNC(evt_tower_battle_end) {
    const s32 result = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (!sActive) {
        return EVT_RETURN_DONE;
    }
    if (result == 4) {
        goToFloor(sFloor); /* fled: the same battle again */
    } else if (result == 1) {
        pouchAddCoin((s16)(5 + sFloor)); /* a little for each battle floor */
        if (sFloor >= PORT_TOWER_FLOORS) {
            endClimb(1, 1);
        } else {
            goToFloor(sFloor + 1);
        }
    }
    /* lost: the game's Game Over, which ends the climb (portTowerFrame) */
    return EVT_RETURN_DONE;
}

/* --- the climb --------------------------------------------------------------- */

int portTowerUnlocked(void) {
    return gp != NULL && pouchGetPtr() != NULL && portChapterSelectUnlocked();
}

int portTowerActive(void) {
    return __atomic_load_n(&sActive, __ATOMIC_SEQ_CST);
}

int portTowerFloor(void) {
    return sActive ? (sFloor < 1 ? 1 : sFloor) : 0;
}

int portTowerChallenge(void) {
    const int f = portTowerFloor();
    return f >= 1 ? sPlan[f].challenge : PORT_TOWER_WALK;
}

unsigned portTowerSeed(void) {
    return sSeed;
}

unsigned long long portTowerElapsed(void) {
    return sActive ? SDL_GetTicks() - sStartedAt : 0;
}

int portTowerHelped(void) {
    return sHelped;
}

int portTowerResult(int* floor, unsigned long long* ms, int* cleared) {
    *floor = sResultFloor;
    *ms = sResultMs;
    *cleared = sResultCleared;
    return sResultCount;
}

int portTowerBest(int* floor, unsigned long long* ms) {
    readBest();
    *floor = sBestFloor;
    *ms = sBestMs;
    return sBestFloor > 0;
}

int portTowerStart(const char** why) {
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (portOnlineActive() || portReplayActive()) {
        *why = "Not during online co-op or a replay.";
        return 0;
    }
    if (!portTowerUnlocked()) {
        *why = "The Paper Tower opens once this file has finished the game.";
        return 0;
    }
    if (portTowerActive() || portPitCoopActive() || portChapterActive() ||
        (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished)) {
        *why = "Not while another mode runs.";
        return 0;
    }
    __atomic_store_n(&sStartAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

void portTowerStop(void) {
    __atomic_store_n(&sStopAsked, 1, __ATOMIC_SEQ_CST);
}

int portTowerSkip(const char** why) {
    if (!portTowerActive()) {
        *why = "No climb is under way.";
        return 0;
    }
    __atomic_store_n(&sSkipAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

static void endClimb(int cleared, int goHome) {
    sResultFloor = sFloor < 1 ? 1 : sFloor;
    sResultMs = SDL_GetTicks() - sStartedAt;
    sResultCleared = cleared;
    sResultCount++;
    readBest();
    if (!sHelped) {
        if (sResultFloor > sBestFloor) {
            sBestFloor = sResultFloor;
        }
        if (cleared && (sBestMs == 0 || sResultMs < sBestMs)) {
            sBestMs = sResultMs;
        }
        writeBest();
    }
    printf("[tower] climb over at floor %d, %llu ms%s%s\n", sResultFloor, sResultMs, cleared ? ", cleared" : "",
           sHelped ? " (helped)" : "");
    fflush(stdout);
    __atomic_store_n(&sActive, 0, __ATOMIC_SEQ_CST);
    sGatesLive = 0;
    if (goHome && gp != NULL) {
        seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    }
}

static int towerMap(const char* map) {
    int i;
    if (strcmp(map, BATTLE_MAP) == 0) {
        return 1;
    }
    for (i = 0; i < ROOMS; i++) {
        if (strcmp(map, sHostNames[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* --- the gates --------------------------------------------------------------- */

#define GATE_REACH 420.0f /* each side of the way across */
#define WIND_DEPTH 70.0f  /* each side of a wind wall's line */
#define WIND_PUSH 3.5f    /* units a frame */

/* gate g's line: its centre, and the way across (unit, x z) */
static void gateLine(const Gate* g, float* cx, float* cz, float* ux, float* uz) {
    const float dx = sTo.x - sFrom.x, dz = sTo.z - sFrom.z;
    const float len = sqrtf(dx * dx + dz * dz);
    *cx = sFrom.x + dx * g->at;
    *cz = sFrom.z + dz * g->at;
    *ux = len > 0.0f ? dx / len : 1.0f;
    *uz = len > 0.0f ? dz / len : 0.0f;
}

static void gatesFrame(void) {
    MarioWork* m = marioGetPtr();
    const Floor* f;
    int g;
    if (!sGatesLive || m == NULL || sFloor < 1 || sPlan[sFloor].room < 0 || seqGetSeq() != SEQ_GAME) {
        sPrevLive = 0;
        return;
    }
    f = &sPlan[sFloor];
    if (!sPrevLive) {
        sPrev = m->position;
        sPrevLive = 1;
        return;
    }
    for (g = 0; g < f->gateCount; g++) {
        float cx, cz, ux, uz;
        float before, after, across;
        gateLine(&f->gates[g], &cx, &cz, &ux, &uz);
        /* along the way across (signed distance from the line), and along the line */
        before = (sPrev.x - cx) * ux + (sPrev.z - cz) * uz;
        after = (m->position.x - cx) * ux + (m->position.z - cz) * uz;
        across = fabsf((m->position.x - cx) * -uz + (m->position.z - cz) * ux);
        if (across > GATE_REACH) {
            continue;
        }
        if (f->gates[g].kind == PORT_TOWER_WIND) {
            /* in the gust: blown back towards the way in, unless side-on */
            if (fabsf(after) < WIND_DEPTH && !marioChkSts(4)) {
                m->position.x -= ux * WIND_PUSH;
                m->position.z -= uz * WIND_PUSH;
            }
            continue;
        }
        if ((before < 0.0f) != (after < 0.0f)) {
            const int through = f->gates[g].kind == PORT_TOWER_PAPER ? marioChkSts(4) : marioChkSts(0x10);
            if (!through) {
                m->position.x = sPrev.x; /* stopped at the gate */
                m->position.z = sPrev.z;
            }
        }
    }
    sPrev = m->position;
}

/* the 2D layer, as the co-op pings draw */
static void flatStart(void) {
    Mtx mtx;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXCopy(camGetPtr(CAMERA_2D)->view, mtx);
    GXLoadPosMtxImm(mtx, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

static void color(GXColor c) {
    GXSetTevColor(GX_TEVREG0, c);
}

/* an upright quad in the world: from a to b along the ground, from y0 to y1 */
static void wallQuad(float ax, float az, float bx, float bz, float y0, float y1) {
    Vec p[4], q[4];
    int i;
    const float m = portViewMirrored() ? -1.0f : 1.0f;
    p[0] = (Vec){ax, y0, az};
    p[1] = (Vec){bx, y0, bz};
    p[2] = (Vec){bx, y1, bz};
    p[3] = (Vec){ax, y1, az};
    for (i = 0; i < 4; i++) {
        getScreenPoint(&p[i], &q[i]);
    }
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    for (i = 0; i < 4; i++) {
        GXPosition3f32(q[i].x * m, q[i].y, 0.0f);
    }
}

static void gatesDisp(CameraId camId, void* param) {
    static unsigned sTick;
    const Floor* f = &sPlan[sFloor];
    const float y = (sFrom.y + sTo.y) * 0.5f;
    int g;
    (void)camId;
    (void)param;
    sTick++;
    flatStart();
    for (g = 0; g < f->gateCount; g++) {
        float cx, cz, ux, uz, s;
        int k;
        gateLine(&f->gates[g], &cx, &cz, &ux, &uz);
        /* along the line: (-uz, ux) */
        if (f->gates[g].kind == PORT_TOWER_PAPER) {
            /* paper slats with thin gaps: only a sheet of paper slips between */
            color((GXColor){0xFF, 0xFA, 0xEC, 0xB4});
            for (k = -12; k < 12; k++) {
                s = (float)k * (GATE_REACH / 12.0f);
                wallQuad(cx - uz * s, cz + ux * s, cx - uz * (s + GATE_REACH / 12.0f - 3.0f),
                         cz + ux * (s + GATE_REACH / 12.0f - 3.0f), y, y + 110.0f);
            }
        } else if (f->gates[g].kind == PORT_TOWER_TUBE) {
            /* a low paper wall: a rolling tube fits under it, Mario doesn't */
            color((GXColor){0xE8, 0xD2, 0xAC, 0xC8});
            wallQuad(cx + uz * GATE_REACH, cz - ux * GATE_REACH, cx - uz * GATE_REACH, cz + ux * GATE_REACH, y + 24.0f,
                     y + 120.0f);
            color((GXColor){0x78, 0x54, 0x32, 0xC8});
            wallQuad(cx + uz * GATE_REACH, cz - ux * GATE_REACH, cx - uz * GATE_REACH, cz + ux * GATE_REACH, y + 22.0f,
                     y + 26.0f);
        } else {
            /* the gust: streaks drifting back towards the way in */
            color((GXColor){0xC8, 0xE6, 0xFF, 0x70});
            for (k = 0; k < 10; k++) {
                const float t = fmodf((float)(sTick * 3 + k * 37), WIND_DEPTH * 2.0f) - WIND_DEPTH;
                const float along = ((float)(k * 53 % 21) - 10.0f) * (GATE_REACH / 10.0f);
                const float h = y + 15.0f + (float)(k * 29 % 80);
                const float px = cx - ux * t - uz * along, pz = cz - uz * t + ux * along;
                wallQuad(px, pz, px - ux * 40.0f, pz - uz * 40.0f, h, h + 3.0f);
            }
        }
    }
}

/* --- each frame -------------------------------------------------------------- */

static void startNow(void) {
    int* seed = portSettingsFind("paper_tower_seed");
    sSeed = seed != NULL && *seed != 0 ? (unsigned)*seed : portDateSeed();
    plan(sSeed);
    snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
    sFloor = 0;
    sHelped = 0;
    sGatesLive = 0;
    sStartedAt = SDL_GetTicks();
    __atomic_store_n(&sActive, 1, __ATOMIC_SEQ_CST);
    printf("[tower] climb from %s, seed %u\n", sReturnMap, sSeed);
    fflush(stdout);
    goToFloor(1);
}

void portTowerFrame(void) {
    const SequenceType seq = seqGetSeq();
    if (!sActive) {
        __atomic_store_n(&sStopAsked, 0, __ATOMIC_SEQ_CST);
        __atomic_store_n(&sSkipAsked, 0, __ATOMIC_SEQ_CST);
        /* after a scene or a menu (the Start menu's form) */
        if (seq == SEQ_GAME && gp != NULL && !gp->inBattle && !seqCheckSeq() && marioStGetSystemLevel() == 0 &&
            __atomic_exchange_n(&sStartAsked, 0, __ATOMIC_SEQ_CST)) {
            startNow();
        }
        return;
    }
    if (seq == SEQ_GAME_OVER || seq == SEQ_TITLE) {
        endClimb(0, 0);
        return;
    }
    if (seq != SEQ_GAME || gp == NULL || gp->inBattle) {
        return;
    }
    if (__atomic_exchange_n(&sStopAsked, 0, __ATOMIC_SEQ_CST)) {
        endClimb(0, 1);
        return;
    }
    if (!towerMap(gp->currentMapName)) {
        endClimb(0, 0); /* out of the tower some other way (a warp) */
        return;
    }
    if (__atomic_exchange_n(&sSkipAsked, 0, __ATOMIC_SEQ_CST)) {
        sHelped = 1;
        if (sFloor >= PORT_TOWER_FLOORS) {
            endClimb(1, 1);
        } else {
            goToFloor(sFloor + 1);
        }
        return;
    }
    gatesFrame();
    if (sGatesLive && sFloor >= 1 && sPlan[sFloor].room >= 0) {
        dispEntry(CAMERA_2D, 902.0f, 1, gatesDisp, NULL);
    }
}

void portTowerRegisterMaps(void) {
    int i;
    for (i = 0; i < ROOMS; i++) {
        snprintf(sHostNames[i], sizeof(sHostNames[i]), "%.3s_w%02d", kRooms[i], i);
        portModsAddHostMap(sHostNames[i], tower_floor_init, kRooms[i]);
    }
    portModsAddHostMap(BATTLE_MAP, tower_battle_init, BATTLE_FILES);
    portSettingsDeclare("paper_tower_seed", 0, 0, 99999);
}
