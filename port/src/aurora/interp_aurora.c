/* High refresh rates: frames drawn between the game's 60 Hz frames
 * (docs/FRAME_INTERPOLATION.md, steps 1 and 2). $TTYD_FRAMERATE: 120, 144,
 * 240 or "display" (the monitor's rate); unset or 60: off.
 *
 * The game draws (marioStDisp -> camDraw) and then runs its logic, so at the
 * retrace (vi_aurora.c) the port holds the state it just showed (n, kept in
 * a snapshot) and the next one (n+1, live). An in-between frame draws the
 * live scene with, blended between the two:
 * - the render clocks: animation poses are computed from them, not stepped
 *   (animPoseMain), so models are posed at the in-between moment;
 * - every camera's view and projection matrices (and eye/target, which the
 *   billboarded NPCs face);
 * - the positions of Mario, the partners and the NPCs;
 * then puts everything back. Anything that jumped (a camera cut, a teleport,
 * a map change) is drawn as it is now. It repeats camDraw without its
 * letterbox step (which advances the cutscene bars) and without dispReInit
 * (the next real frame draws the same list). The logic never runs for these
 * frames, and the next real frame recomputes every pose from the real clocks:
 * runs, sweeps and audio are the same with the setting on. */
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/npcdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "memory.h"
#include "party.h"
#include "sdk/DEMOInit.h"
#include "system.h"
#include "port/aurora_pc.h"
#include "port/platform.h"
#include "port/widescreen.h"

#undef bool

void portCamEvalNearFar(CameraEntry* camera); /* camdrv.c (camEvalNearFar is static) */
extern s32 entry_n;           /* dispdrv.c: the draw list */
extern DispEntry** pSortWork;
extern DispCallback portExtMakeTexture; /* extdrv.c: its static extMakeTexture */

#define CAM_JUMP 250.0f   /* a camera moving more in one frame cut */
#define BODY_JUMP 150.0f  /* an actor moving more in one frame teleported */
#define MAX_NPCS 128

void portInterpAuditBegin(void); /* interp_audit.c: $TTYD_INTERP_AUDIT */
void portInterpAuditEnd(void);
void animPoseInterpBegin(void); /* animdrv.c */
void animPoseInterpEnd(void);

typedef struct {
    BOOL valid;
    char map[sizeof(gp->currentMapName)];
    BOOL inBattle;
    OSTime clocks[6];
    struct {
        Mtx view;
        Mtx44 projection;
        Vec pos, at;
        f32 fovY;
    } cam[CAMERA_MAX];
    Vec mario;
    PartyEntry* party[2];
    Vec partyPos[2];
    NpcEntry* npcs;
    s32 npcCount;
    BOOL npcLive[MAX_NPCS];
    Vec npcPos[MAX_NPCS];
} Snapshot;

static Snapshot sPrev;
static BOOL sSceneDrawn;
static BOOL sInBetween;
static DispEntry* sSorted[1024]; /* dispdrv.c's DISP_MAX_ENTRIES */

static int sFrames = 1;
static int sRate = 60;

/* The setting (settings_aurora.c: framerate= in settings.txt, or
 * $TTYD_FRAMERATE): 60 (off), 120, 144, 240, ... or "display", the monitor's
 * rate. Shown as the nearest multiple of 60, at most 8 frames per game frame
 * (of 50, in the European release's 50 Hz mode: portInterpFrames). */
void portInterpSetRate(const char* v) {
    int rate = 60, frames;
    if (v != NULL && strcmp(v, "display") == 0) {
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        rate = mode != NULL && mode->refresh_rate > 0 ? (int)(mode->refresh_rate + 0.5f) : 60;
    } else if (v != NULL) {
        rate = atoi(v);
    }
    frames = (rate + 30) / 60;
    frames = frames < 1 ? 1 : frames > 8 ? 8 : frames;
    if (frames != sFrames && frames > 1) {
        printf("[interp] %d frames per game frame (%d Hz)\n", frames, frames * 60);
    }
    sFrames = frames;
    sRate = rate;
}

int portInterpFrames(void) {
    int field = portFieldRate(), frames;
    if (field == 60) {
        return sFrames;
    }
    /* 50 Hz: as many frames per game frame as fit the setting's rate */
    frames = (sRate + field / 2) / field;
    return frames < 1 ? 1 : frames > 8 ? 8 : frames;
}

void portInterpSceneDrawn(void) {
    sSceneDrawn = TRUE;
}

BOOL portInterpInBetween(void) {
    return sInBetween;
}

/* (address, size, bytes)... kept by portInterpKeep during an in-between draw */
static u8* sKept;
static size_t sKeptUsed, sKeptCap;

void portInterpKeep(void* p, unsigned int n) {
    size_t need;
    if (!sInBetween || p == NULL || n == 0) {
        return;
    }
    need = sizeof(void*) + sizeof(unsigned int) + n;
    if (sKeptUsed + need > sKeptCap) {
        sKeptCap = (sKeptUsed + need) * 2;
        sKept = realloc(sKept, sKeptCap);
    }
    memcpy(sKept + sKeptUsed, &p, sizeof(void*));
    memcpy(sKept + sKeptUsed + sizeof(void*), &n, sizeof(unsigned int));
    memcpy(sKept + sKeptUsed + sizeof(void*) + sizeof(unsigned int), p, n);
    sKeptUsed += need;
}

/* smartAlloc's frame-temporary blocks during an in-between draw (memory.c):
 * a scratch arena, reset for each in-between frame. */
#define SCRATCH_BYTES (16 << 20)
#define SCRATCH_BLOCKS 512
static u8* sScratch;
static size_t sScratchUsed;
static smartEntry sScratchBlocks[SCRATCH_BLOCKS];
static int sScratchCount;

void* portInterpScratchAlloc(u32 size) {
    smartEntry* e;
    size_t n = ((size_t)size + 31) & ~(size_t)31;
    if (sScratch == NULL) {
        sScratch = malloc(SCRATCH_BYTES + 32);
    }
    if (sScratch == NULL || sScratchCount == SCRATCH_BLOCKS || sScratchUsed + n > SCRATCH_BYTES) {
        return NULL; /* the real heap, then */
    }
    e = &sScratchBlocks[sScratchCount++];
    memset(e, 0, sizeof(*e));
    e->address = (u8*)(((uintptr_t)sScratch + 31) & ~(uintptr_t)31) + sScratchUsed;
    e->size = size;
    e->type = 3;
    sScratchUsed += n;
    return e;
}

/* Puts back what was kept, the latest first: a place kept twice ends as it
 * was before the first. */
static void restoreKept(void) {
    while (sKeptUsed > 0) {
        size_t at = 0, last = 0;
        void* p;
        unsigned int n;
        while (at < sKeptUsed) { /* find the last record */
            last = at;
            memcpy(&n, sKept + at + sizeof(void*), sizeof(unsigned int));
            at += sizeof(void*) + sizeof(unsigned int) + n;
        }
        memcpy(&p, sKept + last, sizeof(void*));
        memcpy(&n, sKept + last + sizeof(void*), sizeof(unsigned int));
        memcpy(p, sKept + last + sizeof(void*) + sizeof(unsigned int), n);
        sKeptUsed = last;
    }
}

static OSTime* renderClock(int i) {
    switch (i) {
    case 0: return &gp->renderTime;
    case 1: return &gp->renderFieldTime;
    case 2: return &gp->systemLv0Time;
    case 3: return &gp->systemLv1Time;
    case 4: return &gp->systemLv2Time;
    default: return &gp->systemLv3Time;
    }
}

static PartyEntry* partyMember(int slot) {
    s32 id = slot == 0 ? marioGetPartyId() : marioGetExtraPartyId();
    return id >= 0 ? partyGetPtr(id) : NULL;
}

static void snapshot(void) {
    NpcWork* npcs = npcGetWorkPtr();
    int i;
    sPrev.valid = TRUE;
    memcpy(sPrev.map, gp->currentMapName, sizeof(sPrev.map));
    sPrev.inBattle = gp->inBattle;
    for (i = 0; i < 6; i++) {
        sPrev.clocks[i] = *renderClock(i);
    }
    for (i = 0; i < CAMERA_MAX; i++) {
        CameraEntry* c = camGetPtr((CameraId)i);
        MTXCopy(c->view, sPrev.cam[i].view);
        memcpy(sPrev.cam[i].projection, c->projection, sizeof(Mtx44));
        sPrev.cam[i].pos = c->cameraPos;
        sPrev.cam[i].at = c->cameraAt;
        sPrev.cam[i].fovY = c->fovY;
    }
    sPrev.mario = marioGetPtr()->position;
    for (i = 0; i < 2; i++) {
        sPrev.party[i] = partyMember(i);
        if (sPrev.party[i] != NULL) {
            sPrev.partyPos[i] = sPrev.party[i]->position;
        }
    }
    sPrev.npcs = npcs->entries;
    sPrev.npcCount = npcs->total < MAX_NPCS ? npcs->total : MAX_NPCS;
    for (i = 0; i < sPrev.npcCount; i++) {
        sPrev.npcLive[i] = (npcs->entries[i].flags & 1) != 0;
        sPrev.npcPos[i] = npcs->entries[i].position;
    }
}

static f32 lerpf(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

static f32 dist(const Vec* a, const Vec* b) {
    f32 x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;
    return x * x + y * y + z * z;
}

/* b moved to its blend with a, unless it jumped. */
static void lerpPos(Vec* b, const Vec* a, f32 t, f32 jump) {
    if (dist(a, b) < jump * jump) {
        b->x = lerpf(a->x, b->x, t);
        b->y = lerpf(a->y, b->y, t);
        b->z = lerpf(a->z, b->z, t);
    }
}

/* The live scene, drawn as camDraw does, minus the letterbox step and the
 * draw list's reset, between DEMOBeforeRender's and DEMODoneRender's state. */
static void drawScene(void) {
    GXRenderModeObj* rmode = DEMOGetRenderModeObj();
    s32 count = entry_n;
    int id;
    memcpy(sSorted, pSortWork, count * sizeof(DispEntry*));
    sInBetween = TRUE;
    GXSetViewport(0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight, 0.0f, 1.0f);
    GXInvalidateVtxCache();
    GXInvalidateTexAll();
    dispSort();
    for (id = 0; id < CAMERA_MAX; id++) {
        CameraEntry* camera = camGetPtr((CameraId)id);
        if (camera->flags & 1) {
            continue;
        }
        camSetCurNo((CameraId)id);
        GXSetViewport(camera->viewport[0], camera->viewport[1], camera->viewport[2], camera->viewport[3],
                      camera->viewport[4], camera->viewport[5]);
        GXSetScissor(camera->scissor[0], camera->scissor[1], camera->scissor[2], camera->scissor[3]);
        if (camera->flags & 0x40 && camera->projType == GX_PERSPECTIVE) {
            portCamEvalNearFar(camera);
            MTXPerspective(camera->projection, camera->fovY, camera->aspect, camera->near, camera->far);
        }
        portWidescreenCamera((CameraId)id); /* as camDraw */
        GXSetProjection(camera->projection, camera->projType);
        dispDraw((CameraId)id);
        sysDummyDraw(camera->view);
    }
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    GXSetAlphaUpdate(GX_TRUE);
    sInBetween = FALSE;
    /* the list as the game left it: its own frame sorts it (qqsort is not
     * stable) and draws it, as without these frames */
    entry_n = count;
    memcpy(pSortWork, sSorted, count * sizeof(DispEntry*));
}

int portInterpReady(void) {
    s32 i;
    /* the game drew its scene this frame, on the map and side it drew last */
    if (!sSceneDrawn || !sPrev.valid || gp == NULL || gp->inBattle != sPrev.inBattle ||
        memcmp(gp->currentMapName, sPrev.map, sizeof(sPrev.map)) != 0) {
        return FALSE;
    }
    /* extdrv's textures are made (smartAlloc'd) by the next draw, once, and
     * the draws after it use them: that frame is the game's own */
    for (i = 0; i < entry_n; i++) {
        if (pSortWork[i]->callback == portExtMakeTexture) {
            return FALSE;
        }
    }
    return TRUE;
}

int portInterpDraw(float t) {
    static CameraEntry sCams[CAMERA_MAX];
    static Vec sNpcPos[MAX_NPCS];
    NpcWork* npcs;
    OSTime clocks[6];
    Vec mario, partyPos[2];
    CameraId curCam;
    u32 seed;
    int i;
    if (!portInterpReady()) {
        return FALSE; /* nothing drawn this frame, or a map or battle change */
    }
    npcs = npcGetWorkPtr();
    if (npcs->entries != sPrev.npcs || npcs->total < sPrev.npcCount) {
        sPrev.npcCount = 0; /* the NPCs were set up again */
    }
    curCam = camGetCurNo();
    /* keep the live state */
    for (i = 0; i < 6; i++) {
        clocks[i] = *renderClock(i);
    }
    for (i = 0; i < CAMERA_MAX; i++) {
        sCams[i] = *camGetPtr((CameraId)i);
    }
    mario = marioGetPtr()->position;
    for (i = 0; i < 2; i++) {
        PartyEntry* p = partyMember(i);
        partyPos[i] = p != NULL ? p->position : (Vec){0.0f, 0.0f, 0.0f};
    }
    for (i = 0; i < sPrev.npcCount; i++) {
        sNpcPos[i] = npcs->entries[i].position;
    }

    /* blend toward the previous frame */
    for (i = 0; i < 6; i++) {
        *renderClock(i) = sPrev.clocks[i] + (OSTime)((f64)(clocks[i] - sPrev.clocks[i]) * t);
    }
    for (i = 0; i < CAMERA_MAX; i++) {
        CameraEntry* c = camGetPtr((CameraId)i);
        int r, k;
        if (dist(&sPrev.cam[i].pos, &c->cameraPos) >= CAM_JUMP * CAM_JUMP) {
            continue; /* a cut */
        }
        for (r = 0; r < 3; r++) {
            for (k = 0; k < 4; k++) {
                c->view[r][k] = lerpf(sPrev.cam[i].view[r][k], c->view[r][k], t);
            }
        }
        for (r = 0; r < 4; r++) {
            for (k = 0; k < 4; k++) {
                c->projection[r][k] = lerpf(sPrev.cam[i].projection[r][k], c->projection[r][k], t);
            }
        }
        lerpPos(&c->cameraPos, &sPrev.cam[i].pos, t, CAM_JUMP);
        lerpPos(&c->cameraAt, &sPrev.cam[i].at, t, CAM_JUMP);
        c->fovY = lerpf(sPrev.cam[i].fovY, c->fovY, t);
    }
    lerpPos(&marioGetPtr()->position, &sPrev.mario, t, BODY_JUMP);
    for (i = 0; i < 2; i++) {
        PartyEntry* p = partyMember(i);
        if (p != NULL && p == sPrev.party[i]) {
            lerpPos(&p->position, &sPrev.partyPos[i], t, BODY_JUMP);
        }
    }
    for (i = 0; i < sPrev.npcCount; i++) {
        if ((npcs->entries[i].flags & 1) && sPrev.npcLive[i]) {
            lerpPos(&npcs->entries[i].position, &sPrev.npcPos[i], t, BODY_JUMP);
        }
    }

    seed = portRandState(); /* text shakes its glyphs with irand while drawing */
    sScratchUsed = 0;
    sScratchCount = 0;
    animPoseInterpBegin();
    portInterpAuditBegin();
    drawScene();
    portInterpAuditEnd();
    animPoseInterpEnd(); /* the poses as the game left them */
    portRandSetState(seed);
    restoreKept(); /* what draw code kept (portInterpKeep) */

    /* and back */
    for (i = 0; i < 6; i++) {
        *renderClock(i) = clocks[i];
    }
    for (i = 0; i < CAMERA_MAX; i++) {
        *camGetPtr((CameraId)i) = sCams[i];
    }
    camSetCurNo(curCam);
    marioGetPtr()->position = mario;
    for (i = 0; i < 2; i++) {
        PartyEntry* p = partyMember(i);
        if (p != NULL) {
            p->position = partyPos[i];
        }
    }
    for (i = 0; i < sPrev.npcCount; i++) {
        npcs->entries[i].position = sNpcPos[i];
    }
    return TRUE;
}

/* Save states: the scene changed under it, nothing to blend from. */
void portInterpInvalidate(void) {
    sPrev.valid = FALSE;
    sSceneDrawn = FALSE;
}

void portInterpFrameDone(void) {
    if (sSceneDrawn) { /* the game is running (its cameras exist) */
        snapshot();
    } else {
        sPrev.valid = FALSE;
    }
    sSceneDrawn = FALSE;
}
