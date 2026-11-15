/* Frame interpolation by replay (port/include/port/interp_replay.h, docs/FRAME_INTERPOLATION.md).
 *
 * The game's draw runs once a game frame, live, as at 60 Hz. Aurora records it (the GX commands it sent, and a list
 * of the frame's events: matrix and projection loads, channel and TEV colours, each draw's direct vertices, each
 * vertex array as its draw read it), each event carrying the identity of the draw-list entry it came from
 * (portInterpEntry: callback, parameter, camera) or of the camera outside its entries.
 *
 * At the retrace the game's picture is kept (interp_hold.cpp) and the in-between frames before it are the frame's
 * recording fed through Aurora again, each event paired with the frame before's by (identity, kind, register,
 * occurrence) and its data blended toward it: position matrices by rotation (slerp), scale and translation;
 * texture, normal and post matrices, projections, colours, direct vertices and posed vertex arrays element by
 * element. An event without a pair, or one that jumped (a camera cut, a teleport, a texture scroll wrapping), is
 * drawn as recorded. Then the kept picture is shown at its time. Stereoscopic 3D: the game draws the right eye; the
 * left is the same recording with the eye's shift applied to the 3D cameras' matrices.
 *
 * A replay runs no game code: draw code with side effects (frees, timers, random numbers) runs once a game frame
 * by construction. EFB copies in it are made again by Aurora into textures of the replay's own; it asks for no
 * readbacks. A frame Aurora can't replay (memory moved under its draws, a port pass drawing outside the FIFO
 * that isn't a hook) shows no in-between frames, logged once per reason. */
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/mtx.h>
#include <dolphin/vi.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/camdrv.h"
#include "mario/mariost.h"
#include "port/aurora_pc.h"
#include "port/interp_replay.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/stereo.h"

#undef bool

void portShotBeforeCopyDisp(u32 frame); /* shot_aurora.c */
void portBloomApply(void);      /* bloom_aurora.c */
void portRtLightingApply(void); /* rt_lighting_aurora.c */

#define CAM_JUMP 250.0f     /* a camera moving more in one frame cut (as the redraw path) */
#define MTX_JUMP 300.0f     /* a matrix's translation moving more: a jump, drawn as recorded */
#define VTX_JUMP 150.0f     /* a vertex or posed array element the same */
#define TEX_JUMP 0.5f       /* a texture coordinate or matrix offset: a scroll wrapping */
#define MAX_SEGS 16
#define TAG_CAMERA(tag) ((int)((tag) & 0xF))
#define TAG_OUTSIDE 0xFFFFFFF0u /* | camera: a camera's commands outside its entries */

#if defined(__ANDROID__) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#define RECORDING_CAP (24u << 20) /* phones: a frame's recording past this replays not (logged) */
#else
#define RECORDING_CAP (128u << 20)
#endif

typedef struct {
    u32 pause, resume; /* stream positions after the pause marker and after the resume marker */
    int id;
} Seg;

typedef struct {
    AuroraRec* rec;
    AuroraRecView v;
    BOOL valid;      /* a drawn game frame, recorded whole */
    BOOL replayable; /* and Aurora can replay it */
    Seg segs[MAX_SEGS];
    int segCount;
    char map[sizeof(gp->currentMapName)];
    BOOL inBattle;
    Vec camPos[CAMERA_MAX];
    BOOL cut[CAMERA_MAX]; /* (against the frame before) */
} Frame;

static Frame sFrames[2];
static int sCur;              /* sFrames[sCur]: the last frame recorded; the other, the one before */
static Frame* sRecording;     /* the frame being recorded */
static int sOpenSeg = -1;     /* a hook begun (its pause position) */
static int sMode = -1;        /* $TTYD_INTERP_MODE: 1 replay, 0 redraw, -1 the setting */
static int* sSetting;
static int sOverCap;          /* recordings in a row past the cap */
static int sCapOff;           /* the cap was passed too often: the redraw path for this run */
static double sLiveProcUs, sReplayProcUs; /* (stats) Aurora's command processor busy: on the game's frame, on replays */
static u64 sBusyAtStart;

/* ---- the mode ---- */

static int replayWanted(void) {
    if (sMode == -1) {
        const char* v = getenv("TTYD_INTERP_MODE");
        sMode = v == NULL ? -2 : strcmp(v, "replay") == 0 ? 1 : 0;
        if (v != NULL) {
            printf("[interp-replay] mode %s ($TTYD_INTERP_MODE)\n", sMode == 1 ? "replay" : "redraw");
            fflush(stdout);
        }
    }
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("interp_replay", 2, 0, 2);
    }
    if (sCapOff) {
        return 0;
    }
    if (sMode >= 0) {
        return sMode;
    }
    if (*sSetting == 2) {
        /* automatic (the default): replay for stereo's second eye at 60 Hz only. A replay's in-between frame costs
         * Aurora's command processor the whole frame again (measured at 120 Hz on an RTX 3090: ~7.7 ms of the 8.33
         * ms slot in Rogueport, against ~2.2 ms for a redraw), so higher
         * frame rates draw their in-between frames by redraw (interp_aurora.c), whose draw code with side effects
         * is kept to once a game frame (portInterpStepsHere, portInterpKeep, the interp audit's scenarios) */
        const int frames = portInterpFrames();
        static int sAudit = -1;
        if (sAudit < 0) {
            const char* v = getenv("TTYD_INTERP_AUDIT"); /* (interp_audit.c audits the redraw path's draws) */
            sAudit = v != NULL && v[0] != '\0' && v[0] != '0';
        }
        if (sAudit) {
            return 0;
        }
        return portStereoMode() != PORT_STEREO_OFF && frames <= 1;
    }
    return *sSetting;
}

/* the redraw path drawing in-between frames or a second eye by choice (the automatic mode's), not a test's: its
 * second draws of entries are its design, not a failure of the draw-once check */
static int redrawByDesign(void) {
    return sMode < 0 && !portInterpReplayOn() && (portInterpFrames() > 1 || portStereoMode() != PORT_STEREO_OFF);
}

int portInterpReplayOn(void) {
    return replayWanted() && (portInterpFrames() > 1 || portStereoMode() != PORT_STEREO_OFF);
}

/* ---- identities and the draw-once check ---- */

static u32 hashPtr(uintptr_t p, u32 h) {
    u64 x = (u64)p ^ ((u64)h << 32 | h);
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return (u32)x;
}

/* entries drawn this game frame: (key, count), open addressing */
#define ONCE_SLOTS 4096
static u32 sOnceKey[ONCE_SLOTS];
static u8 sOnceCount[ONCE_SLOTS];
static u32 sOnceUsed[ONCE_SLOTS]; /* the slots set this frame (to clear them) */
static int sOnceUsedCount;
static unsigned sOnceFails, sOnceFrames, sOnceRedrawn;
static Uint64 sOnceToldAt;
/* the redraw path was on at some point in this game frame: a mode change mid-frame (the graphics watchdog turning
   the in-between frames off: gfx_watchdog_steps) leaves that frame's redraws by design */
static BOOL sOnceFrameByDesign;

static void drawOnceFrame(void) {
    int i;
    for (i = 0; i < sOnceUsedCount; i++) {
        sOnceKey[sOnceUsed[i]] = 0;
        sOnceCount[sOnceUsed[i]] = 0;
    }
    sOnceUsedCount = 0;
    sOnceFrames++;
    sOnceFrameByDesign = redrawByDesign();
    if (sOnceRedrawn != 0 && sOnceFails == 0) {
        static int sTold;
        if (!sTold) {
            sTold = 1;
            printf("[drawonce] the redraw path draws the game's entries again for its in-between frames or second "
                   "eye (the automatic mode at this rate; not checked)\n");
            fflush(stdout);
        }
    }
    if (sOnceFails != 0) {
        const Uint64 now = SDL_GetTicksNS();
        if (sOnceToldAt == 0 || now - sOnceToldAt > 10000000000ull) {
            printf("[drawonce] FAIL: %u draw-list entries drawn more than once in a game frame (%u frames)\n",
                   sOnceFails, sOnceFrames);
            fflush(stdout);
            sOnceToldAt = now;
        }
    }
}

void portInterpDrawOnceHold(void) {
    int i;
    for (i = 0; i < sOnceUsedCount; i++) {
        sOnceKey[sOnceUsed[i]] = 0;
        sOnceCount[sOnceUsed[i]] = 0;
    }
    sOnceUsedCount = 0;
}

static void drawOnceCount(u32 key, void* callback, void* param, int camera) {
    u32 i = key & (ONCE_SLOTS - 1);
    int probes;
    if (key == 0) {
        key = 1;
    }
    for (probes = 0; probes < ONCE_SLOTS; probes++, i = (i + 1) & (ONCE_SLOTS - 1)) {
        if (sOnceKey[i] == key) {
            if (sOnceCount[i] < 255) {
                sOnceCount[i]++;
            }
            if (redrawByDesign()) {
                sOnceFrameByDesign = TRUE;
            }
            if (sOnceCount[i] == 2 && sOnceFrameByDesign) {
                sOnceRedrawn++; /* (the redraw path's own second draw: counted, not a failure) */
            } else if (sOnceCount[i] == 2) {
                static int sTold;
                const char* strict = getenv("TTYD_DRAWONCE_STRICT");
                sOnceFails++;
                if (sTold < 5) {
                    sTold++;
                    printf("[drawonce] FAIL: entry %p (param %p, camera %d) drawn twice in game frame %u\n", callback,
                           param, camera, (unsigned)VIGetRetraceCount());
                    fflush(stdout);
                }
                if (strict != NULL && strict[0] != '\0' && strict[0] != '0') {
                    printf("[drawonce] strict: stopping\n");
                    fflush(stdout);
                    _Exit(86);
                }
            }
            return;
        }
        if (sOnceKey[i] == 0) {
            sOnceKey[i] = key;
            sOnceCount[i] = 1;
            if (sOnceUsedCount < ONCE_SLOTS) {
                sOnceUsed[sOnceUsedCount++] = i;
            }
            return;
        }
    }
}

void portInterpEntry(int cameraId, const void* entry, void* callback, void* param) {
    const u32 h = hashPtr((uintptr_t)callback, hashPtr((uintptr_t)param, (u32)cameraId));
    drawOnceCount(hashPtr((uintptr_t)entry, 0x5157u), callback, param, cameraId); /* (an entry: its slot this frame) */
    AuroraRecordTag((h & ~0xFu) | ((u32)cameraId & 0xF));
}

void portInterpCamera(int cameraId) {
    AuroraRecordTag(TAG_OUTSIDE | ((u32)cameraId & 0xF));
}

/* ---- hooks ---- */

void portInterpHookBegin(void) {
    if (sRecording == NULL) {
        return;
    }
    sOpenSeg = (int)AuroraRecordPause(GX_TRUE);
}

void portInterpHookEnd(int id) {
    u32 resume;
    if (sRecording == NULL || sOpenSeg < 0) {
        return;
    }
    resume = AuroraRecordPause(GX_FALSE);
    if (sRecording->segCount < MAX_SEGS) {
        Seg* s = &sRecording->segs[sRecording->segCount++];
        s->pause = (u32)sOpenSeg;
        s->resume = resume;
        s->id = id;
    } else {
        sRecording->replayable = FALSE; /* (never: one hook a frame) */
    }
    sOpenSeg = -1;
}

static void runHook(int id) {
    switch (id) {
    case PORT_INTERP_HOOK_LIGHT_BLOOM:
        portRtLightingApply();
        portBloomApply();
        break;
    default:
        break;
    }
}

/* ---- recording ---- */

static void forgetFrame(Frame* f) {
    if (f->rec != NULL) {
        AuroraRecordFree(f->rec);
    }
    memset(f, 0, sizeof(*f));
}

void portInterpRecordStart(void) {
    Frame* f;
    if (!portInterpReplayOn() || sRecording != NULL) {
        return;
    }
    f = &sFrames[sCur ^ 1]; /* the one before the last: its place */
    forgetFrame(f);
    f->rec = AuroraRecordBegin();
    if (f->rec == NULL) {
        return;
    }
    sBusyAtStart = AuroraProcessorBusyNs();
    f->replayable = TRUE;
    sRecording = f;
    sOpenSeg = -1;
}

static void logOnce(const char* reason) {
    static const char* sTold[16];
    int i;
    for (i = 0; i < 16 && sTold[i] != NULL; i++) {
        if (sTold[i] == reason) {
            return;
        }
    }
    if (i < 16) {
        sTold[i] = reason;
    }
    printf("[interp-replay] a frame not replayed (no in-between frames for it): %s\n", reason);
    fflush(stdout);
}

/* what a frame's recording holds of its own: its stream, the arrays it copied (not shared with the frame
 * before) and its event list */
static double sPaired, sPairedOf; /* events paired with the frame before's, of those in frames paired */
static double sRecEndUs, sSubstUs; /* time in AuroraRecordEnd (the frame's processing awaited), in buildSubst */

static size_t frameBytes(const Frame* f) {
    return (size_t)f->v.streamSize + f->v.arrayBytesNew + (size_t)f->v.eventCount * sizeof(AuroraRecEvent);
}

static void statsLine(const Frame* f, double buildUs, double replayUs, int replays) {
    static int sOn = -1;
    static Uint64 sAt;
    static double sStream, sArena, sArrays, sEvents, sBuild, sReplay, sMax;
    static unsigned sN, sReplays;
    const Uint64 now = SDL_GetTicksNS();
    if (sOn < 0) {
        const char* v = getenv("TTYD_INTERP_STATS");
        sOn = v != NULL && v[0] != '0';
    }
    if (!sOn) {
        return;
    }
    sN++;
    sStream += f->v.streamSize;
    sArena += f->v.arrayBytesNew;
    sEvents += f->v.eventCount;
    sBuild += buildUs;
    sReplay += replayUs;
    sReplays += (unsigned)replays;
    if ((double)frameBytes(f) > sMax) {
        sMax = (double)frameBytes(f);
    }
    sArrays += f->v.arrayBytes;
    if (sAt == 0) {
        sAt = now;
    }
    if (now - sAt >= 5000000000ull && sN > 0) {
        printf("[interp-replay] stats over %u game frames: recording %.0f KB new a frame (stream %.0f KB, arrays %.0f "
               "KB copied of %.0f KB read; peak %.0f KB), %.0f events (%.1f%% paired); recording's end %.0f us and "
               "pairing %.0f us a frame, %.0f us a replay (blending %.0f us); Aurora's processor %.0f us on the "
               "game's frame, %.0f us on a replay\n",
               sN, (sStream + sArena) / sN / 1024.0, sStream / sN / 1024.0, sArena / sN / 1024.0,
               sArrays / sN / 1024.0, sMax / 1024.0, sEvents / sN, sPairedOf > 0 ? 100.0 * sPaired / sPairedOf : 0.0,
               sRecEndUs / sN, sBuild / sN, sReplays ? sReplay / sReplays : 0.0,
               sReplays ? sSubstUs / sReplays : 0.0, sLiveProcUs / sN, sReplays ? sReplayProcUs / sReplays : 0.0);
        fflush(stdout);
        sAt = now;
        sN = sReplays = 0;
        sPaired = sPairedOf = sRecEndUs = sSubstUs = sLiveProcUs = sReplayProcUs = 0;
        sStream = sArena = sArrays = sEvents = sBuild = sReplay = sMax = 0;
    }
}

/* The game's GXCopyDisp, while its frame is recorded, left its capture (and stereo's composing) for when its picture
 * is whole and next to be shown: stereo's left eye is a replay after it, and the in-between frames are shown before
 * it (a capture of the window, as a burst's, takes the next picture shown) */
static int sCopyDispDeferred;

int portInterpReplayDeferCopyDisp(void) {
    if (sRecording == NULL || (portStereoMode() == PORT_STEREO_OFF && portInterpFrames() <= 1)) {
        return 0;
    }
    sCopyDispDeferred = 1;
    return 1;
}

static void copyDispDone(void) {
    if (sCopyDispDeferred) {
        sCopyDispDeferred = 0;
        portStereoCompose(); /* (once a frame: a second call composes nothing) */
        portShotBeforeCopyDisp(VIGetRetraceCount());
    }
}

void portInterpRecordStop(int drawn) {
    Frame* f = sRecording;
    int i;
    drawOnceFrame(); /* (every retrace: the check runs on either path) */
    if (f == NULL) {
        return;
    }
    sRecording = NULL;
    {
        const Uint64 from = SDL_GetTicksNS();
        AuroraRecordEnd(f->rec);
        sRecEndUs += (double)(SDL_GetTicksNS() - from) / 1000.0;
        sLiveProcUs += (double)(AuroraProcessorBusyNs() - sBusyAtStart) / 1000.0;
    }
    AuroraRecordView(f->rec, &f->v);
    if (sOpenSeg >= 0) {
        AuroraRecordPause(GX_FALSE); /* (a hook never ended) */
        f->replayable = FALSE;
        sOpenSeg = -1;
    }
    if (!drawn || gp == NULL) {
        forgetFrame(f); /* an undrawn frame: the last one stays the last */
        copyDispDone();
        return;
    }
    f->valid = TRUE;
    if (f->v.unreplayable != NULL) {
        f->replayable = FALSE;
        logOnce(f->v.unreplayable);
    }
    if (frameBytes(f) > RECORDING_CAP) {
        f->replayable = FALSE;
        logOnce("its recording is past the memory cap");
        if (++sOverCap >= 60) {
            sCapOff = 1;
            printf("[interp-replay] recordings past the cap for a second: the redraw path for this run\n");
            fflush(stdout);
        }
    } else {
        sOverCap = 0;
    }
    memcpy(f->map, gp->currentMapName, sizeof(f->map));
    f->inBattle = gp->inBattle;
    for (i = 0; i < CAMERA_MAX; i++) {
        CameraEntry* c = camGetPtr((CameraId)i);
        f->camPos[i] = c->cameraPos;
    }
    sCur ^= 1;
}

void portInterpReplayForget(void) {
    if (sRecording != NULL) {
        AuroraRecordEnd(sRecording->rec);
        sRecording = NULL;
    }
    forgetFrame(&sFrames[0]);
    forgetFrame(&sFrames[1]);
}

/* ---- pairing ---- */

/* the frame before's events by (tag, kind, register, occurrence) */
typedef struct {
    u32 tag, kindReg, nth, index;
} PairKey;

static PairKey* sTable;
static u32 sTableMask;
static u32* sNth; /* (counting occurrences) keys of (tag, kind, register) -> count */
static u32* sNthKey;
static u32* sNthTag;
static u32 sNthMask;

static u32 keyHash(u32 tag, u32 kindReg, u32 nth) {
    u32 h = tag * 0x9E3779B1u ^ kindReg * 0x85EBCA77u ^ nth * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

static int growTables(u32 events) {
    u32 size = 1024;
    while (size < events * 2) {
        size <<= 1;
    }
    if (size - 1 != sTableMask) {
        free(sTable);
        free(sNth);
        free(sNthKey);
        free(sNthTag);
        sTable = malloc(size * sizeof(PairKey));
        sNth = malloc(size * sizeof(u32));
        sNthKey = malloc(size * sizeof(u32));
        sNthTag = malloc(size * sizeof(u32));
        if (sTable == NULL || sNth == NULL || sNthKey == NULL || sNthTag == NULL) {
            sTableMask = 0;
            return 0;
        }
        sTableMask = size - 1;
        sNthMask = size - 1;
    }
    return 1;
}

/* the occurrence of (tag, kindReg) so far this pass */
static u32 nthOf(u32 tag, u32 kindReg) {
    u32 i = keyHash(tag, kindReg, 0) & sNthMask;
    for (;;) {
        if (sNth[i] == 0) {
            sNth[i] = 1;
            sNthKey[i] = kindReg;
            sNthTag[i] = tag;
            return 0;
        }
        if (sNthKey[i] == kindReg && sNthTag[i] == tag) {
            return sNth[i]++;
        }
        i = (i + 1) & sNthMask;
    }
}

static u32 kindReg(const AuroraRecEvent* e) {
    return (u32)e->kind << 16 | e->reg;
}

static void indexFrame(const Frame* f) {
    u32 i;
    memset(sTable, 0xFF, (sTableMask + 1) * sizeof(PairKey));
    memset(sNth, 0, (sNthMask + 1) * sizeof(u32));
    for (i = 0; i < f->v.eventCount; i++) {
        const AuroraRecEvent* e = &f->v.events[i];
        const u32 kr = kindReg(e), nth = nthOf(e->tag, kr);
        u32 j = keyHash(e->tag, kr, nth) & sTableMask;
        while (sTable[j].index != 0xFFFFFFFFu) {
            j = (j + 1) & sTableMask;
        }
        sTable[j].tag = e->tag;
        sTable[j].kindReg = kr;
        sTable[j].nth = nth;
        sTable[j].index = i;
    }
}

static u32 findPair(u32 tag, u32 kr, u32 nth) {
    u32 j = keyHash(tag, kr, nth) & sTableMask;
    while (sTable[j].index != 0xFFFFFFFFu) {
        if (sTable[j].tag == tag && sTable[j].kindReg == kr && sTable[j].nth == nth) {
            return sTable[j].index;
        }
        j = (j + 1) & sTableMask;
    }
    return 0xFFFFFFFFu;
}

/* the current frame's events' pairs in the frame before (0xFFFFFFFF: none), made once a game frame */
static u32* sPairs;
static u32 sPairsCap;
#define PAIR_NONE 0xFFFFFFFFu
#define PAIR_TODO 0xFFFFFFFEu

/* Counts that differ: an entry drawing a varying number of things (an effect's particles, one dying or born; a
 * list scrolling). Paired by occurrence, everything after the change would blend toward its neighbour's place; so
 * such a group's position matrices and direct draws are aligned by where they are (a greedy walk through both
 * frames' occurrences that skips one that died or was born when its neighbour is much closer), and its other
 * events, which say nothing of where, are drawn as recorded for this frame. */
static u32* sPrevCount; /* (copies of the frame before's counting table) */
static u32* sPrevKey;
static u32* sPrevTag;
static u32* sSlotLast; /* per counting slot: its last occurrence in the current frame so far */
static u32* sEvSlot;   /* per current event: its counting slot */
static u32* sEvNext;   /* per current event: the next occurrence of its (tag, kind, register) */
static u32 sEvCap;

static f32 beF(const u8* p);
static int finiteF(f32 f);
static f32 compGet(const u8* p, int type, int frac);
static int compSize(int type);

/* where an event puts things (a position matrix's translation, a direct draw's first vertex) */
static int eventWhere(const Frame* f, const AuroraRecEvent* e, f32 out[3]) {
    const u8* d = f->v.stream + e->offset;
    if (e->kind == AURORA_REC_XF && e->reg < 0x78 && e->count == 12) {
        out[0] = beF(d + 12);
        out[1] = beF(d + 28);
        out[2] = beF(d + 44);
        return finiteF(out[0]) && finiteF(out[1]) && finiteF(out[2]);
    }
    if (e->kind == AURORA_REC_DRAW && e->posOff != 0xFF && e->posCnt >= 2 && e->count > 0 &&
        (u32)e->posOff + (u32)e->posCnt * (u32)compSize(e->posType) <= e->vtxSize) {
        const int cs = compSize(e->posType);
        out[0] = compGet(d + e->posOff, e->posType, e->posFrac);
        out[1] = compGet(d + e->posOff + cs, e->posType, e->posFrac);
        out[2] = e->posCnt >= 3 ? compGet(d + e->posOff + 2 * cs, e->posType, e->posFrac) : 0.0f;
        return finiteF(out[0]) && finiteF(out[1]) && finiteF(out[2]);
    }
    return 0;
}

static f32 whereDist(const Frame* fa, const AuroraRecEvent* a, const Frame* fb, const AuroraRecEvent* b) {
    f32 pa[3], pb[3];
    if (!eventWhere(fa, a, pa) || !eventWhere(fb, b, pb) || a->size != b->size) {
        return 1e30f;
    }
    return (pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2]);
}

/* the frame before's count of (tag, kindReg) */
static u32 prevCountOf(u32 tag, u32 kr) {
    u32 i = keyHash(tag, kr, 0) & sNthMask;
    while (sPrevCount[i] != 0) {
        if (sPrevKey[i] == kr && sPrevTag[i] == tag) {
            return sPrevCount[i];
        }
        i = (i + 1) & sNthMask;
    }
    return 0;
}

/* the group whose first current occurrence is first, counts differing: aligned by place (or left unpaired) */
static void alignGroup(const Frame* prev, const Frame* cur, u32 first, u32 prevCount, u32 curCount) {
    const AuroraRecEvent* e0 = &cur->v.events[first];
    const u32 tag = e0->tag, kr = kindReg(e0);
    f32 where[3];
    u32 i = first, j = 0, skips = (prevCount > curCount ? prevCount - curCount : curCount - prevCount) + 2;
    const int placed = eventWhere(cur, e0, where);
    while (i != PAIR_NONE) {
        const u32 next = sEvNext[i];
        u32 p = PAIR_NONE;
        if (placed && j < prevCount) {
            const u32 pj = findPair(tag, kr, j);
            const f32 d0 = pj != PAIR_NONE ? whereDist(cur, &cur->v.events[i], prev, &prev->v.events[pj]) : 1e30f;
            if (skips > 0 && d0 > 0.0f) {
                const u32 pj1 = j + 1 < prevCount ? findPair(tag, kr, j + 1) : PAIR_NONE;
                const f32 d1 = pj1 != PAIR_NONE ? whereDist(cur, &cur->v.events[i], prev, &prev->v.events[pj1]) : 1e30f;
                const f32 d2 = next != PAIR_NONE && pj != PAIR_NONE
                                   ? whereDist(cur, &cur->v.events[next], prev, &prev->v.events[pj])
                                   : 1e30f;
                if (d1 < d0 * 0.25f && d1 <= d2) { /* the frame before's j is gone: i is its follower's */
                    j++;
                    skips--;
                    continue;
                }
                if (d2 < d0 * 0.25f) { /* i is new: j is the next one's */
                    sPairs[i] = PAIR_NONE;
                    i = next;
                    skips--;
                    continue;
                }
            }
            p = pj;
            j++;
        }
        sPairs[i] = p;
        i = next;
    }
}

/* two events of the same layout (sizes, vertex formats) */
static int sameShape(const AuroraRecEvent* q, const AuroraRecEvent* e) {
    return q->size == e->size && q->count == e->count && q->vtxSize == e->vtxSize && q->posOff == e->posOff &&
           q->posType == e->posType && q->posCnt == e->posCnt && q->posFrac == e->posFrac && q->clrOff == e->clrOff &&
           q->clrType == e->clrType && q->texOff == e->texOff && q->texType == e->texType && q->texCnt == e->texCnt &&
           q->texFrac == e->texFrac && q->le == e->le;
}

static int pairFrames(const Frame* prev, const Frame* cur) {
    u32 i;
    const u32 n = cur->v.eventCount;
    const u32 oldMask = sNthMask;
    if (!growTables(prev->v.eventCount > n ? prev->v.eventCount : n)) {
        return 0;
    }
    if (sPrevCount == NULL || oldMask != sNthMask) {
        free(sPrevCount);
        free(sPrevKey);
        free(sPrevTag);
        free(sSlotLast);
        sPrevCount = malloc((sNthMask + 1) * sizeof(u32));
        sPrevKey = malloc((sNthMask + 1) * sizeof(u32));
        sPrevTag = malloc((sNthMask + 1) * sizeof(u32));
        sSlotLast = malloc((sNthMask + 1) * sizeof(u32));
        if (sPrevCount == NULL || sPrevKey == NULL || sPrevTag == NULL || sSlotLast == NULL) {
            free(sPrevCount);
            sPrevCount = NULL;
            return 0;
        }
    }
    if (sPairsCap < n + 1) {
        free(sPairs);
        sPairsCap = n + 1024;
        sPairs = malloc(sPairsCap * sizeof(u32));
        if (sPairs == NULL) {
            sPairsCap = 0;
            return 0;
        }
    }
    if (sEvCap < n + 1) {
        free(sEvSlot);
        free(sEvNext);
        sEvCap = n + 1024;
        sEvSlot = malloc(sEvCap * sizeof(u32));
        sEvNext = malloc(sEvCap * sizeof(u32));
        if (sEvSlot == NULL || sEvNext == NULL) {
            free(sEvSlot);
            free(sEvNext);
            sEvSlot = sEvNext = NULL;
            sEvCap = 0;
            return 0;
        }
    }
    if (prev->v.eventCount == n) {
        /* the same events in the same order as the frame before (a scene drawing the same things): each one's
         * occurrence is its place, so it pairs with the event at its place (no tables to build) */
        for (i = 0; i < n; i++) {
            const AuroraRecEvent* e = &cur->v.events[i];
            const AuroraRecEvent* q = &prev->v.events[i];
            if (e->tag != q->tag || e->kind != q->kind || e->reg != q->reg) {
                break;
            }
        }
        if (i == n) {
            sPairedOf += n;
            for (i = 0; i < n; i++) {
                sPairs[i] = sameShape(&prev->v.events[i], &cur->v.events[i]) ? i : PAIR_NONE;
                sPaired += sPairs[i] != PAIR_NONE;
            }
            return 1;
        }
    }
    indexFrame(prev);
    memcpy(sPrevCount, sNth, (sNthMask + 1) * sizeof(u32));
    memcpy(sPrevKey, sNthKey, (sNthMask + 1) * sizeof(u32));
    memcpy(sPrevTag, sNthTag, (sNthMask + 1) * sizeof(u32));
    /* the current frame's counts, and each group's occurrences in order */
    memset(sNth, 0, (sNthMask + 1) * sizeof(u32));
    memset(sSlotLast, 0xFF, (sNthMask + 1) * sizeof(u32));
    for (i = 0; i < n; i++) {
        const AuroraRecEvent* e = &cur->v.events[i];
        const u32 kr = kindReg(e);
        u32 s = keyHash(e->tag, kr, 0) & sNthMask;
        while (sNth[s] != 0 && !(sNthKey[s] == kr && sNthTag[s] == e->tag)) {
            s = (s + 1) & sNthMask;
        }
        if (sNth[s] == 0) {
            sNthKey[s] = kr;
            sNthTag[s] = e->tag;
        }
        sNth[s]++;
        sEvSlot[i] = s;
        sEvNext[i] = PAIR_NONE;
        if (sSlotLast[s] != PAIR_NONE) {
            sEvNext[sSlotLast[s]] = i;
        }
        sSlotLast[s] = i;
        sPairs[i] = PAIR_TODO;
    }
    sPairedOf += n;
    /* (sSlotLast reused: each group's next occurrence number) */
    memset(sSlotLast, 0, (sNthMask + 1) * sizeof(u32));
    for (i = 0; i < n; i++) {
        const AuroraRecEvent* e = &cur->v.events[i];
        const u32 kr = kindReg(e);
        const u32 s = sEvSlot[i];
        u32 p;
        if (sPairs[i] == PAIR_TODO) {
            const u32 pc = prevCountOf(e->tag, kr);
            if (pc != sNth[s] && pc != 0) {
                alignGroup(prev, cur, i, pc, sNth[s]); /* (i: the group's first) */
            }
        }
        if (sPairs[i] == PAIR_TODO) {
            p = findPair(e->tag, kr, sSlotLast[s]);
        } else {
            p = sPairs[i];
        }
        sSlotLast[s]++;
        if (p != PAIR_NONE && !sameShape(&prev->v.events[p], e)) {
            p = PAIR_NONE; /* not the same shape: not the same thing */
        }
        sPairs[i] = p;
        sPaired += p != 0xFFFFFFFFu;
    }
    return 1;
}

/* ---- blending ---- */

static u8* sOut; /* the replacements, one replay's */
static size_t sOutUsed, sOutCap;
static u32* sSubstOff; /* per event: its replacement's offset in sOut (or none) */
static const void** sSubst;
static u32 sSubstCap;

static u8* outTake(size_t n) {
    size_t at = (sOutUsed + 15) & ~(size_t)15;
    if (at + n > sOutCap) {
        size_t cap = sOutCap ? sOutCap : (1 << 20);
        u8* grown;
        while (at + n > cap) {
            cap *= 2;
        }
        grown = realloc(sOut, cap);
        if (grown == NULL) {
            return NULL;
        }
        sOut = grown;
        sOutCap = cap;
    }
    sOutUsed = at + n;
    return sOut + at; /* (an offset is kept, not this pointer: sOut may move) */
}

static f32 beF(const u8* p) {
    u32 v;
    f32 f;
    memcpy(&v, p, 4);
    v = (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    memcpy(&f, &v, 4);
    return f;
}

static void putBeF(u8* p, f32 f) {
    u32 v;
    memcpy(&v, &f, 4);
    v = (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    memcpy(p, &v, 4);
}

static u32 beU32(const u8* p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void putBeU32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static f32 lerpf(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

static int finiteF(f32 f) {
    return f - f == 0.0f;
}

/* a 3x4 position matrix t of the way from a to b: rotation by slerp, scale and translation linearly; element by
 * element when either isn't a rotation and scale. 0: it jumped (drawn as b) */
static int blendPosMtx(const f32 a[12], const f32 b[12], f32 t, f32 out[12]) {
    f32 sa[3], sb[3], ra[9], rb[9], qa[4], qb[4], q[4], d;
    int r, c, k;
    for (r = 0; r < 3; r++) {
        f32 dt = b[r * 4 + 3] - a[r * 4 + 3];
        if (!(fabsf(dt) < MTX_JUMP)) {
            return 0;
        }
    }
    for (k = 0; k < 2; k++) {
        const f32* m = k == 0 ? a : b;
        f32* s = k == 0 ? sa : sb;
        f32* rm = k == 0 ? ra : rb;
        f32 det;
        for (c = 0; c < 3; c++) {
            s[c] = sqrtf(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]);
            if (!(s[c] > 1e-6f)) {
                goto elementwise;
            }
            for (r = 0; r < 3; r++) {
                rm[r * 3 + c] = m[r * 4 + c] / s[c];
            }
        }
        det = rm[0] * (rm[4] * rm[8] - rm[5] * rm[7]) - rm[1] * (rm[3] * rm[8] - rm[5] * rm[6]) +
              rm[2] * (rm[3] * rm[7] - rm[4] * rm[6]);
        if (det < 0.0f) { /* a mirror: the x axis's scale negative */
            s[0] = -s[0];
            for (r = 0; r < 3; r++) {
                rm[r * 3] = -rm[r * 3];
            }
            det = -det;
        }
        /* a rotation: orthonormal columns */
        if (fabsf(det - 1.0f) > 0.02f || fabsf(rm[0] * rm[1] + rm[3] * rm[4] + rm[6] * rm[7]) > 0.02f ||
            fabsf(rm[0] * rm[2] + rm[3] * rm[5] + rm[6] * rm[8]) > 0.02f ||
            fabsf(rm[1] * rm[2] + rm[4] * rm[5] + rm[7] * rm[8]) > 0.02f) {
            goto elementwise;
        }
        {
            f32* qq = k == 0 ? qa : qb;
            const f32 tr = rm[0] + rm[4] + rm[8];
            if (tr > 0.0f) {
                f32 s4 = sqrtf(tr + 1.0f) * 2.0f;
                qq[3] = 0.25f * s4;
                qq[0] = (rm[7] - rm[5]) / s4;
                qq[1] = (rm[2] - rm[6]) / s4;
                qq[2] = (rm[3] - rm[1]) / s4;
            } else if (rm[0] > rm[4] && rm[0] > rm[8]) {
                f32 s4 = sqrtf(1.0f + rm[0] - rm[4] - rm[8]) * 2.0f;
                qq[3] = (rm[7] - rm[5]) / s4;
                qq[0] = 0.25f * s4;
                qq[1] = (rm[1] + rm[3]) / s4;
                qq[2] = (rm[2] + rm[6]) / s4;
            } else if (rm[4] > rm[8]) {
                f32 s4 = sqrtf(1.0f + rm[4] - rm[0] - rm[8]) * 2.0f;
                qq[3] = (rm[2] - rm[6]) / s4;
                qq[0] = (rm[1] + rm[3]) / s4;
                qq[1] = 0.25f * s4;
                qq[2] = (rm[5] + rm[7]) / s4;
            } else {
                f32 s4 = sqrtf(1.0f + rm[8] - rm[0] - rm[4]) * 2.0f;
                qq[3] = (rm[3] - rm[1]) / s4;
                qq[0] = (rm[2] + rm[6]) / s4;
                qq[1] = (rm[5] + rm[7]) / s4;
                qq[2] = 0.25f * s4;
            }
        }
    }
    if ((sa[0] < 0.0f) != (sb[0] < 0.0f)) {
        goto elementwise; /* one mirrored, one not: through the flat middle, as the game's own scale goes */
    }
    d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (d < 0.0f) {
        d = -d;
        for (k = 0; k < 4; k++) {
            qb[k] = -qb[k];
        }
    }
    if (d < 0.7071f) {
        return 0; /* turned more than 90 degrees in a frame: a jump */
    }
    if (d > 0.9995f) {
        for (k = 0; k < 4; k++) {
            q[k] = lerpf(qa[k], qb[k], t);
        }
    } else {
        const f32 th = acosf(d), sn = sinf(th);
        const f32 wa = sinf((1.0f - t) * th) / sn, wb = sinf(t * th) / sn;
        for (k = 0; k < 4; k++) {
            q[k] = qa[k] * wa + qb[k] * wb;
        }
    }
    d = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (k = 0; k < 4; k++) {
        q[k] /= d;
    }
    {
        const f32 x = q[0], y = q[1], z = q[2], w = q[3];
        const f32 rot[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
        for (c = 0; c < 3; c++) {
            const f32 s = lerpf(sa[c], sb[c], t);
            for (r = 0; r < 3; r++) {
                out[r * 4 + c] = rot[r * 3 + c] * s;
            }
        }
        for (r = 0; r < 3; r++) {
            out[r * 4 + 3] = lerpf(a[r * 4 + 3], b[r * 4 + 3], t);
        }
    }
    return 1;
elementwise:
    for (k = 0; k < 12; k++) {
        out[k] = lerpf(a[k], b[k], t);
    }
    return 1;
}

static u8 lerpByte(u8 a, u8 b, f32 t) {
    const f32 v = lerpf((f32)a, (f32)b, t) + 0.5f;
    return (u8)(v < 0.0f ? 0 : v > 255.0f ? 255 : v);
}

static s32 signExt11(u32 v) {
    return (s32)(v << 21) >> 21;
}

/* the replacement for event i (paired with p in the frame before) at t, with the eye's shift; 0: none */
typedef struct {
    int on;
    f32 viewDx[CAMERA_MAX], projK[CAMERA_MAX], orthoShift[CAMERA_MAX];
    BOOL shifted[CAMERA_MAX];
} Eye;

static int blendXf(const Frame* prev, const Frame* cur, const AuroraRecEvent* e, const AuroraRecEvent* q, f32 t,
                   const Eye* eye, u8* out) {
    const u8* b = cur->v.stream + e->offset;
    const u8* a = q != NULL ? prev->v.stream + q->offset : NULL;
    const u16 addr = e->reg;
    const int cam = TAG_CAMERA(e->tag);
    const int shift = eye != NULL && eye->on && cam < CAMERA_MAX && eye->shifted[cam];
    int i, changed = 0;
    memcpy(out, b, e->size);
    if (addr < 0x78 && e->count == 12) { /* a position matrix */
        f32 mb[12], m[12];
        for (i = 0; i < 12; i++) {
            mb[i] = beF(b + i * 4);
            m[i] = mb[i];
        }
        if (a != NULL) {
            f32 ma[12];
            for (i = 0; i < 12; i++) {
                ma[i] = beF(a + i * 4);
            }
            changed = blendPosMtx(ma, mb, t, m);
        }
        if (shift) {
            m[3] += eye->viewDx[cam];
            changed = 1;
        }
        if (changed) {
            for (i = 0; i < 12; i++) {
                putBeF(out + i * 4, m[i]);
            }
        }
        return changed;
    }
    if (addr == 0x1020 && e->count == 7) { /* the projection: six values and the type */
        f32 p[6];
        const u32 type = beU32(b + 24);
        for (i = 0; i < 6; i++) {
            p[i] = beF(b + i * 4);
        }
        if (a != NULL && beU32(a + 24) == type) {
            f32 pa[6];
            int ok = 1;
            for (i = 0; i < 6; i++) {
                pa[i] = beF(a + i * 4);
                if (!(fabsf(p[i] - pa[i]) <= 0.5f * fabsf(p[i]) + 1e-3f)) {
                    ok = 0; /* a projection changed outright (another camera's, a zoom cut) */
                }
            }
            if (ok) {
                for (i = 0; i < 6; i++) {
                    p[i] = lerpf(pa[i], p[i], t);
                }
                changed = 1;
            }
        }
        if (shift) {
            if (type == GX_PERSPECTIVE) {
                p[1] += p[0] * eye->projK[cam];
            } else {
                p[1] += eye->orthoShift[cam];
            }
            changed = 1;
        }
        if (changed) {
            for (i = 0; i < 6; i++) {
                putBeF(out + i * 4, p[i]);
            }
        }
        return changed;
    }
    if (a == NULL) {
        return 0;
    }
    if (addr >= 0x100A && addr <= 0x100F) { /* channel colours: RGBA bytes a word */
        for (i = 0; i < (int)e->size; i++) {
            out[i] = lerpByte(a[i], b[i], t);
        }
        return 1;
    }
    /* texture (0x78-0xFF), normal (0x400-) and post-transform (0x500-) matrices: element by element */
    {
        const int n = (int)e->count;
        for (i = 0; i < n; i++) {
            const f32 fa = beF(a + i * 4), fb = beF(b + i * 4);
            if (!finiteF(fa) || !finiteF(fb)) {
                return 0;
            }
            if ((addr < 0x100 || addr >= 0x500) && (i & 3) == 3 && !(fabsf(fb - fa) < TEX_JUMP)) {
                return 0; /* a scroll wrapping */
            }
        }
        for (i = 0; i < n; i++) {
            putBeF(out + i * 4, lerpf(beF(a + i * 4), beF(b + i * 4), t));
        }
        return 1;
    }
}

static int blendBp(const u8* a, const u8* b, f32 t, u8* out) {
    const u32 va = beU32(a), vb = beU32(b);
    u32 v;
    if ((va & 0xFF800000u) != (vb & 0xFF800000u)) {
        return 0; /* a colour and a konst colour: not the same register use */
    }
    if (vb & 0x800000u) { /* konst: 8-bit fields */
        const u32 lo = lerpByte((u8)(va & 0xFF), (u8)(vb & 0xFF), t);
        const u32 hi = lerpByte((u8)((va >> 12) & 0xFF), (u8)((vb >> 12) & 0xFF), t);
        v = (vb & ~0xFF0FFu) | lo | hi << 12;
    } else { /* 11-bit signed fields */
        const s32 la = signExt11(va & 0x7FF), lb = signExt11(vb & 0x7FF);
        const s32 ha = signExt11((va >> 12) & 0x7FF), hb = signExt11((vb >> 12) & 0x7FF);
        const s32 lo = (s32)floorf(lerpf((f32)la, (f32)lb, t) + 0.5f);
        const s32 hi = (s32)floorf(lerpf((f32)ha, (f32)hb, t) + 0.5f);
        v = (vb & ~0x7FF7FFu) | ((u32)lo & 0x7FF) | ((u32)hi & 0x7FF) << 12;
    }
    putBeU32(out, v);
    return 1;
}

/* a component of a direct vertex attribute (type: GX_U8..GX_F32), as a float, and back */
static f32 compGet(const u8* p, int type, int frac) {
    const f32 scale = 1.0f / (f32)(1 << frac);
    switch (type) {
    case GX_U8: return (f32)p[0] * scale;
    case GX_S8: return (f32)(s8)p[0] * scale;
    case GX_U16: return (f32)(u16)(p[0] << 8 | p[1]) * scale;
    case GX_S16: return (f32)(s16)(p[0] << 8 | p[1]) * scale;
    default: return beF(p);
    }
}

static void compPut(u8* p, int type, int frac, f32 v) {
    const f32 x = floorf(v * (f32)(1 << frac) + 0.5f);
    switch (type) {
    case GX_U8: p[0] = (u8)(x < 0 ? 0 : x > 255 ? 255 : x); break;
    case GX_S8: p[0] = (u8)(s8)(x < -128 ? -128 : x > 127 ? 127 : x); break;
    case GX_U16: {
        const u16 u = (u16)(x < 0 ? 0 : x > 65535 ? 65535 : x);
        p[0] = (u8)(u >> 8);
        p[1] = (u8)u;
        break;
    }
    case GX_S16: {
        const s16 u = (s16)(x < -32768 ? -32768 : x > 32767 ? 32767 : x);
        p[0] = (u8)((u16)u >> 8);
        p[1] = (u8)u;
        break;
    }
    default: putBeF(p, v); break;
    }
}

static int compSize(int type) {
    return type == GX_U8 || type == GX_S8 ? 1 : type == GX_U16 || type == GX_S16 ? 2 : 4;
}

static int blendDraw(const AuroraRecEvent* e, const u8* a, const u8* b, f32 t, u8* out) {
    const int n = e->count, size = e->vtxSize;
    int v, c;
    if (size == 0 || (u32)n * (u32)size != e->size || (e->posOff == 0xFF && e->clrOff == 0xFF && e->texOff == 0xFF)) {
        return 0;
    }
    if (memcmp(a, b, e->size) == 0) {
        return 0; /* (still) */
    }
    /* a jump: drawn as recorded */
    if (e->posOff != 0xFF) {
        const int cs = compSize(e->posType);
        for (v = 0; v < n; v++) {
            for (c = 0; c < e->posCnt; c++) {
                const int o = v * size + e->posOff + c * cs;
                const f32 fa = compGet(a + o, e->posType, e->posFrac), fb = compGet(b + o, e->posType, e->posFrac);
                if (!finiteF(fa) || !finiteF(fb) || !(fabsf(fb - fa) < VTX_JUMP)) {
                    return 0;
                }
            }
        }
    }
    memcpy(out, b, e->size);
    for (v = 0; v < n; v++) {
        const u8* va = a + v * size;
        const u8* vb = b + v * size;
        u8* vo = out + v * size;
        if (e->posOff != 0xFF) {
            const int cs = compSize(e->posType);
            for (c = 0; c < e->posCnt; c++) {
                const int o = e->posOff + c * cs;
                compPut(vo + o, e->posType, e->posFrac,
                        lerpf(compGet(va + o, e->posType, e->posFrac), compGet(vb + o, e->posType, e->posFrac), t));
            }
        }
        if (e->clrOff != 0xFF && (e->clrType == GX_RGB8 || e->clrType == GX_RGBX8 || e->clrType == GX_RGBA8)) {
            const int bytes = e->clrType == GX_RGB8 ? 3 : 4;
            for (c = 0; c < bytes; c++) {
                vo[e->clrOff + c] = lerpByte(va[e->clrOff + c], vb[e->clrOff + c], t);
            }
        }
        if (e->texOff != 0xFF) {
            const int cs = compSize(e->texType);
            for (c = 0; c < e->texCnt; c++) {
                const int o = e->texOff + c * cs;
                const f32 fa = compGet(va + o, e->texType, e->texFrac), fb = compGet(vb + o, e->texType, e->texFrac);
                if (finiteF(fa) && finiteF(fb) && fabsf(fb - fa) < TEX_JUMP) {
                    compPut(vo + o, e->texType, e->texFrac, lerpf(fa, fb, t));
                }
            }
        }
    }
    return 1;
}

static int ordinaryF(f32 f) {
    const f32 m = fabsf(f);
    return f == 0.0f || (m > 1e-30f && m < 1e7f);
}

/* a posed vertex array (positions, normals: floats) */
static int blendArray(const AuroraRecEvent* e, const u8* a, const u8* b, f32 t, u8* out) {
    const u32 n = e->size / 4;
    u32 i;
    if ((e->reg != GX_VA_POS && e->reg != GX_VA_NRM) || (e->size & 3) != 0 || !e->le) {
        return 0;
    }
    if (memcmp(a, b, e->size) == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        f32 fa, fb;
        memcpy(&fa, a + i * 4, 4);
        memcpy(&fb, b + i * 4, 4);
        if (!ordinaryF(fa) || !ordinaryF(fb) || !(fabsf(fb - fa) < VTX_JUMP)) {
            return 0; /* not floats, or a jump (another pose) */
        }
    }
    for (i = 0; i < n; i++) {
        f32 fa, fb, f;
        memcpy(&fa, a + i * 4, 4);
        memcpy(&fb, b + i * 4, 4);
        f = lerpf(fa, fb, t);
        memcpy(out + i * 4, &f, 4);
    }
    return 1;
}

/* The replacements for a replay of cur at t (prev NULL: none blended) for an eye; the table Aurora reads. */
static const void* const* buildSubst(const Frame* prev, const Frame* cur, f32 t, const Eye* eye, u32* count) {
    u32 i;
    const u32 n = cur->v.eventCount;
    if (sSubstCap < n + 1) {
        free(sSubstOff);
        free(sSubst);
        sSubstCap = n + 1024;
        sSubstOff = malloc(sSubstCap * sizeof(u32));
        sSubst = malloc(sSubstCap * sizeof(void*));
        if (sSubstOff == NULL || sSubst == NULL) {
            sSubstCap = 0;
            *count = 0;
            return NULL;
        }
    }
    sOutUsed = 0;
    for (i = 0; i < n; i++) {
        const AuroraRecEvent* e = &cur->v.events[i];
        const u32 p = prev != NULL ? sPairs[i] : 0xFFFFFFFFu;
        const AuroraRecEvent* q = p != 0xFFFFFFFFu ? &prev->v.events[p] : NULL;
        const int cam = TAG_CAMERA(e->tag);
        u8* out;
        int done = 0;
        sSubstOff[i] = 0xFFFFFFFFu;
        if (q != NULL && cam < CAMERA_MAX && cur->cut[cam]) {
            q = NULL; /* a camera cut: this camera's draws as recorded */
        }
        if (q == NULL && !(eye != NULL && eye->on && e->kind == AURORA_REC_XF)) {
            continue;
        }
        out = outTake(e->size);
        if (out == NULL) {
            break;
        }
        switch (e->kind) {
        case AURORA_REC_XF:
            done = blendXf(prev, cur, e, q, t, eye, out);
            break;
        case AURORA_REC_BP:
            done = blendBp(prev->v.stream + q->offset, cur->v.stream + e->offset, t, out);
            break;
        case AURORA_REC_PROJ_FULL: {
            int k;
            for (k = 0; k < 16; k++) {
                putBeF(out + k * 4, lerpf(beF(prev->v.stream + q->offset + k * 4),
                                          beF(cur->v.stream + e->offset + k * 4), t));
            }
            done = 1;
            break;
        }
        case AURORA_REC_DRAW:
            done = blendDraw(e, prev->v.stream + q->offset, cur->v.stream + e->offset, t, out);
            break;
        case AURORA_REC_ARRAY:
            done = prev->v.arrays[q->offset] != cur->v.arrays[e->offset] && /* (shared: unchanged) */
                   blendArray(e, prev->v.arrays[q->offset], cur->v.arrays[e->offset], t, out);
            break;
        default:
            break;
        }
        if (done) {
            sSubstOff[i] = (u32)(out - sOut);
        } else {
            sOutUsed = (size_t)(out - sOut); /* (given back) */
        }
    }
    for (i = 0; i < n; i++) {
        sSubst[i] = sSubstOff[i] != 0xFFFFFFFFu ? sOut + sSubstOff[i] : NULL;
    }
    *count = n;
    return sSubst;
}

/* ---- replaying ---- */

static void feed(const Frame* f) {
    u32 at = 0;
    int s;
    for (s = 0; s < f->segCount; s++) {
        const Seg* g = &f->segs[s];
        if (g->pause < at || g->resume < 4 || g->resume - 4 < g->pause || g->resume > f->v.streamSize) {
            continue; /* (never) */
        }
        AuroraReplayWrite(f->v.stream + at, g->pause - at); /* up to and with the pause */
        runHook(g->id);                                      /* the port pass, at its place */
        at = g->resume - 4;                                  /* from the resume marker on */
    }
    AuroraReplayWrite(f->v.stream + at, f->v.streamSize - at);
}

static double sReplayUs;
static int sReplays;

static void replay(const Frame* prev, const Frame* cur, f32 t, int leftEye) {
    Eye eye;
    u32 count = 0;
    const void* const* subst;
    const Uint64 from = SDL_GetTicksNS();
    int i;
    memset(&eye, 0, sizeof(eye));
    if (leftEye) {
        eye.on = 1;
        for (i = 0; i < CAMERA_MAX; i++) {
            eye.shifted[i] = portStereoReplayShift(i, &eye.viewDx[i], &eye.projK[i], &eye.orthoShift[i]);
        }
    }
    subst = buildSubst(prev, cur, t, &eye, &count);
    sSubstUs += (double)(SDL_GetTicksNS() - from) / 1000.0;
    {
        const u64 busy = AuroraProcessorBusyNs();
        AuroraReplayBegin(cur->rec, subst, count);
        feed(cur);
        AuroraReplayEnd();
        sReplayProcUs += (double)(AuroraProcessorBusyNs() - busy) / 1000.0;
    }
    sReplayUs += (double)(SDL_GetTicksNS() - from) / 1000.0;
    sReplays++;
}

static int sameScene(const Frame* a, const Frame* b) {
    return a->valid && b->valid && a->inBattle == b->inBattle && memcmp(a->map, b->map, sizeof(a->map)) == 0;
}

int portInterpReplayFrames(int frames, PortInterpPresent present, void (*beginFrame)(void)) {
    Frame* cur = &sFrames[sCur];
    Frame* prev = &sFrames[sCur ^ 1];
    const int stereo = portStereoMode() != PORT_STEREO_OFF;
    const int canReplay = cur->valid && cur->replayable;
    int pair, k, open = 1, shown = 0, i;
    double buildUs = 0.0;
    Uint64 from;
    sReplayUs = 0.0;
    sReplays = 0;
    if (!cur->valid) {
        copyDispDone();
        return 0;
    }
    if (stereo) {
        /* the game drew the right eye: the left is its replay, then the two composed */
        if (canReplay && portStereoGpuRightDone()) {
            replay(NULL, cur, 1.0f, 1);
        }
        portStereoCompose();
    }
    pair = canReplay && frames > 1 && prev->valid && prev->replayable && sameScene(prev, cur);
    if (pair) {
        from = SDL_GetTicksNS();
        for (i = 0; i < CAMERA_MAX; i++) {
            const Vec* a = &prev->camPos[i];
            const Vec* b = &cur->camPos[i];
            const f32 dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;
            cur->cut[i] = !(dx * dx + dy * dy + dz * dz < CAM_JUMP * CAM_JUMP);
        }
        pair = pairFrames(prev, cur);
        buildUs = (double)(SDL_GetTicksNS() - from) / 1000.0;
    }
    if (!pair || !portInterpGpuHold()) {
        copyDispDone(); /* (the game frame's capture, of both eyes in stereo) */
        statsLine(cur, buildUs, 0.0, 0);
        return 0;
    }
    for (k = 1; k < frames; k++) {
        const f32 t = (f32)k / (f32)frames;
        if (!present(k, -frames)) {
            continue; /* too late to show (keep game speed) */
        }
        if (!open) {
            beginFrame();
        }
        open = 1;
        if (stereo) {
            portStereoGpuNewPicture();
            replay(prev, cur, t, 0);
            if (portStereoGpuRightDone()) {
                replay(prev, cur, t, 1);
            }
        } else {
            replay(prev, cur, t, 0);
        }
        present(k, frames);
        open = 0;
        shown++;
    }
    if (!open) {
        beginFrame();
    }
    if (!portInterpGpuShowHeld()) {
        /* (never: kept above) the game's frame replayed as it was */
        replay(NULL, cur, 1.0f, 0);
    }
    if (stereo) {
        portStereoGpuSkipCompose();
    }
    copyDispDone(); /* (the game frame's capture: of its picture, now in the frame it is shown in) */
    statsLine(cur, buildUs, sReplays ? sReplayUs : 0.0, sReplays);
    return shown;
}

int portInterpReplayRedraw(void) {
    const Frame* cur = &sFrames[sCur];
    if (!cur->valid || !cur->replayable) {
        return 0;
    }
    if (portStereoMode() != PORT_STEREO_OFF) {
        portStereoGpuNewPicture();
        replay(NULL, cur, 1.0f, 0);
        if (portStereoGpuRightDone()) {
            replay(NULL, cur, 1.0f, 1);
        }
    } else {
        replay(NULL, cur, 1.0f, 0);
    }
    return 1;
}
