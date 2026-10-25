/* Anim model decoding on PC; see port/include/port/anim_pc.h. */
#include "port/anim_pc.h"
#include "port/bedecode.h"
#include "driver/animdrv.h"

#include <stdlib.h>
#include <string.h>

extern const PortType portBE_anim_AnimationPoseData;
extern const PortType portBE_anim_AnimData;

#ifdef PORT_BEDECODE_STANDALONE
#define LOCK() 0
#define UNLOCK(x) ((void)(x))
#else
#include <dolphin/os.h>
#ifdef TTYD_SAVE_STATES
/* the game keeps pointers into these allocations: on the save-state heap */
#define PORT_SNAPHEAP_REDIRECT
#include "port/snapheap.h"
#endif
#define LOCK() OSDisableInterrupts()
#define UNLOCK(x) OSRestoreInterrupts(x)
#endif

#define ANIM_DISC_HEADER 0x1B0u /* sizeof(AnimationPoseData) on disc */

/* Game code that reads the native root/tables at disc offsets (evt_mobj's
 * FLD(f32, data, 0xD0..0xE4), imgdrv's fileName at +4, animSetMaterial_Texture's
 * texId/texType at +4/+8) relies on these staying put. */
_Static_assert(__builtin_offsetof(AnimationPoseData, fileName) == 0x4, "fileName");
_Static_assert(__builtin_offsetof(AnimationPoseData, bboxMin) == 0xD0, "bboxMin");
_Static_assert(__builtin_offsetof(AnimationPoseData, bboxMax) == 0xDC, "bboxMax");
_Static_assert(__builtin_offsetof(AnimTexBind, texId) == 0x4, "texId");
_Static_assert(__builtin_offsetof(AnimTexBind, texType) == 0x8, "texType");
_Static_assert(sizeof(PortAnimHeader) <= ANIM_DISC_HEADER, "marker fits the disc header");

typedef struct AnimLive {
    void* blob;
    PortDecodeArena arena;
} AnimLive;

void (*portAnimErrorHook)(struct PortDecodeCtx* ctx, const char* msg);

static AnimLive* sLive;
static uint32_t sLiveCount, sLiveCap;

static AnimLive* findLive(void* blob) {
    uint32_t i;
    for (i = 0; i < sLiveCount; i++) {
        if (sLive[i].blob == blob) {
            return &sLive[i];
        }
    }
    return NULL;
}

static void dropLive(AnimLive* e) {
    portDecodeArenaFree(&e->arena);
    *e = sLive[--sLiveCount];
}

static void* arenaAlloc(void* user, size_t size, size_t align) {
    return portDecodeArenaAlloc((PortDecodeArena*)user, size, align);
}

/* [p, p + len) must lie in the blob past the 16 bytes we overwrite. */
static int rangeOk(PortDecodeCtx* ctx, const void* p, uint64_t len, const char* what) {
    uint64_t off;
    if (!p) {
        return 1;
    }
    off = (uint64_t)((const uint8_t*)p - ctx->blob);
    if ((const uint8_t*)p < ctx->blob + sizeof(PortAnimHeader) || off > ctx->size || len > ctx->size - off) {
        portDecodeFail(ctx, "%s: 0x%llX bytes at 0x%llX outside the model (0x%X bytes)", what,
                       (unsigned long long)len, (unsigned long long)off, (unsigned)ctx->size);
        return 0;
    }
    return 1;
}

/* A sub-block range [first, first + count) of 'stride'-byte elements at base,
 * inside the AnimData's own blob view. */
static int deltaOk(PortDecodeCtx* sub, const void* base, s32 first, s32 count, uint32_t stride,
                   const char* what) {
    uint64_t off;
    if (count <= 0) {
        return 1;
    }
    if (!base || first < 0) {
        portDecodeFail(sub, "%s: keyframe range %d+%d without a table", what, (int)first, (int)count);
        return 0;
    }
    off = (uint64_t)((const uint8_t*)base - sub->blob) + (uint64_t)first * stride;
    if (off > sub->size || (uint64_t)count * stride > sub->size - off) {
        portDecodeFail(sub, "%s: keyframe range %d+%d runs past the model", what, (int)first, (int)count);
        return 0;
    }
    return 1;
}

/* 32-bit word arrays the CPU reads (Vec and AnimPoseNode floats, loopInfo):
 * native copy, swapped word by word (bit patterns kept, so integer words
 * stored in float slots survive). */
static uint32_t* decodeWords(PortDecodeCtx* ctx, uint32_t off, uint32_t count, const char* what) {
    uint32_t* out;
    uint32_t i;
    if (off == 0 || count == 0) {
        return NULL;
    }
    if (!portDecodeCheck(ctx, off, 4, what) || (uint64_t)count * 4 > ctx->size - off) {
        portDecodeFail(ctx, "%s: %u words at 0x%X run past the model", what, (unsigned)count, (unsigned)off);
        return NULL;
    }
    /* AnimData contexts allocate from the model's arena through ctx->alloc. */
    out = (uint32_t*)(ctx->alloc ? ctx->alloc(ctx->allocUser, (size_t)count * sizeof(uint32_t), _Alignof(uint32_t))
                                 : portDecodeArenaAlloc(&ctx->arena, (size_t)count * sizeof(uint32_t),
                                                        _Alignof(uint32_t)));
    if (!out) {
        portDecodeFail(ctx, "out of memory (%s)", what);
        return NULL;
    }
    for (i = 0; i < count; i++) {
        out[i] = portBE32(ctx->blob + off + i * 4);
    }
    return out;
}

static AnimData* decodeAnim(PortDecodeCtx* ctx, uint32_t off) {
    PortDecodeCtx sub;
    AnimData* a;
    s32 i;

    if (off < ANIM_DISC_HEADER || off >= ctx->size) {
        portDecodeFail(ctx, "anim data at 0x%X outside the model", (unsigned)off);
        return NULL;
    }
    /* AnimData pointers are relative to the AnimData itself. */
    portDecodeBegin(&sub, ctx->blob + off, ctx->size - off, ctx->name);
    sub.alloc = arenaAlloc;
    sub.allocUser = &ctx->arena;
    sub.onError = ctx->onError;
    a = (AnimData*)portDecode(&sub, &portBE_anim_AnimData, 0);
    for (i = 0; a && i < a->keyframeCount; i++) {
        const AnimKeyframe* k = &a->keyframes[i];
        if (!deltaOk(&sub, a->vtxPosDeltas, k->vtxPosStart, k->vtxPosCount, 4, "vtxPosDeltas") ||
            !deltaOk(&sub, a->vtxNrmDeltas, k->vtxNrmStart, k->vtxNrmCount, 4, "vtxNrmDeltas") ||
            !deltaOk(&sub, a->visDeltas, k->visStart, k->visCount, 2, "visDeltas") ||
            !deltaOk(&sub, a->nodeDeltas, k->nodeStart, k->nodeCount, 4, "nodeDeltas")) {
            a = NULL;
            break;
        }
        if (k->texCount > 0 && (k->texStart < 0 || k->texStart + k->texCount > a->texDeltaCount)) {
            portDecodeFail(&sub, "texDeltas: keyframe range %d+%d past %d entries", (int)k->texStart,
                           (int)k->texCount, (int)a->texDeltaCount);
            a = NULL;
            break;
        }
    }
    /* loopInfo: [0] loop flag (read as s32), [1] start, [2] end frame. */
    if (a && !sub.failed) {
        a->loopInfo = (f32*)decodeWords(&sub, (uint32_t)(uintptr_t)a->loopInfo, (a->loopInfoCount ? a->loopInfoCount : 1) * 3,
                                        "loopInfo");
        if (!a->loopInfo && !sub.failed) {
            portDecodeFail(&sub, "anim data without loopInfo");
        }
    }
    if (sub.failed) {
        ctx->failed = 1;
        a = NULL;
    }
    portDecodeEnd(&sub);
    return a;
}

/* Retail shape entries are 0xA8 bytes (named). a/OFF_hei_01b (built
 * 2003/02/26, never loaded by the game) is a pre-release model with unnamed
 * 0x68-byte shapes and 0x18-byte groups: read with the retail layout its
 * shapes overlap the draw-call table and every shape/group field is garbage.
 * Name that instead of reporting whichever garbage index trips first. */
static int layoutOk(PortDecodeCtx* ctx, const AnimationPoseData* r) {
    uint32_t shapes = portBE32(ctx->blob + 0x14C), drawCalls = portBE32(ctx->blob + 0x150);
    if (r->shapeCount > 0 && drawCalls > shapes &&
        (uint64_t)(drawCalls - shapes) < (uint64_t)r->shapeCount * sizeof(AnimPoseShape)) {
        portDecodeFail(ctx, "%d shapes in 0x%X bytes before the draw calls (0x%X each in the retail layout): "
                       "pre-release model format", (int)r->shapeCount, (unsigned)(drawCalls - shapes),
                       (unsigned)sizeof(AnimPoseShape));
        return 0;
    }
    return 1;
}

/* Cross-table indices animdrv follows without checks (renderProc, dispProc,
 * evalProc). Also guards the count fields the decode sizes come from.
 * A start index with a zero count is never dereferenced (renderProc and
 * materialProc only form &draws[start] / &drawCalls[first] and loop 0 times). */
static int indicesOk(PortDecodeCtx* ctx, const AnimationPoseData* r) {
    s32 i, j;
    for (i = 0; i < r->shapeCount; i++) {
        const AnimPoseShape* sh = &r->shapes[i];
        if (sh->shapeDrawCount < 0 ||
            (sh->shapeDrawCount > 0 &&
             (sh->shapeDrawStart < 0 || (s64)sh->shapeDrawStart + sh->shapeDrawCount > r->drawCount))) {
            portDecodeFail(ctx, "shape %d: draws %d+%d past %d", (int)i, (int)sh->shapeDrawStart,
                           (int)sh->shapeDrawCount, (int)r->drawCount);
            return 0;
        }
    }
    for (i = 0; i < r->drawCount; i++) {
        const AnimPoseShapeDraw* d = &r->draws[i];
        if (d->drawCallCount < 0 ||
            (d->drawCallCount > 0 &&
             (d->firstDrawCall < 0 || (s64)d->firstDrawCall + d->drawCallCount > r->drawCallCount)) ||
            d->texCount < 0 || d->texCount > 8) {
            portDecodeFail(ctx, "draw %d: draw calls %d+%d past %d (texCount %d)", (int)i, (int)d->firstDrawCall,
                           (int)d->drawCallCount, (int)r->drawCallCount, (int)d->texCount);
            return 0;
        }
        /* texDataIds[j] may name no texture entry: c_kamek_bb/c_kamek_be draw 34
         * (shape pPlaneShape4, group pPlane4) has -1, on a plane whose group is
         * hidden in every animation, so the game never draws it. If it were drawn
         * the GameCube would read the words before texData / the pose's tex
         * matrices and its bindId range check would fall back to bind 0;
         * animdrv's ANIM_TEX_ID_OK takes that fallback without the stray reads.
         * texArrayIds pick texIndex[]/texVtxIndices[] directly: those must fit. */
        for (j = 0; j < d->texCount; j++) {
            if (d->texArrayIds[j] >= 8) {
                portDecodeFail(ctx, "draw %d: texture %d uses vertex array %d", (int)i, (int)j,
                               (int)d->texArrayIds[j]);
                return 0;
            }
        }
    }
    for (i = 0; i < r->groupCount; i++) {
        const AnimPoseGroup* g = &r->groups[i];
        if (g->shapeId < -1 || g->shapeId >= r->shapeCount || g->childGroupId < -1 || g->childGroupId >= r->groupCount ||
            g->brotherGroupId < -1 || g->brotherGroupId >= r->groupCount || g->visibilityId < 0 ||
            (u32)g->visibilityId >= r->mBufferVisibilityNum || g->nodeId < 0 ||
            (u64)g->nodeId + sizeof(AnimPoseNode) / sizeof(f32) > r->mBufferNodeNum) {
            portDecodeFail(ctx, "group %d (%s): an index is out of range", (int)i, g->name);
            return 0;
        }
    }
    return 1;
}

AnimationPoseData* portAnimDecode(void* blob, uint32_t size, const char* name) {
    PortDecodeCtx ctx;
    AnimationPoseData* root;
    PortAnimHeader hdr;
    AnimLive* e;
    s32 i, j;
    int lvl;

    portDecodeBegin(&ctx, blob, size, name ? name : "anim");
    ctx.onError = portAnimErrorHook;
    if (!blob || size < ANIM_DISC_HEADER || ((uintptr_t)blob & (_Alignof(PortAnimHeader) - 1))) {
        portDecodeFail(&ctx, "anim blob %p (0x%X bytes) is too small or misaligned", blob, (unsigned)size);
        return NULL;
    }
    if (((PortAnimHeader*)blob)->magic == PORT_ANIM_MAGIC) {
        lvl = LOCK();
        e = findLive(blob);
        UNLOCK(lvl);
        if (e) {
            return ((PortAnimHeader*)blob)->root; /* already decoded */
        }
    }

    root = (AnimationPoseData*)portDecode(&ctx, &portBE_anim_AnimationPoseData, 0);
    if (root) {
        root->bufferVtxPos = (Vec*)decodeWords(&ctx, (uint32_t)(uintptr_t)root->bufferVtxPos, root->mBufferPosNum * 3,
                                               "bufferVtxPos");
        root->nrmVtxBuffer = (Vec*)decodeWords(&ctx, (uint32_t)(uintptr_t)root->nrmVtxBuffer, root->mBufferNrmNum * 3,
                                               "nrmVtxBuffer");
        /* nodeData: mBufferNodeNum floats (groups index it by float). */
        root->nodeData = (AnimPoseNode*)decodeWords(&ctx, (uint32_t)(uintptr_t)root->nodeData, root->mBufferNodeNum,
                                                    "nodeData");
    }
    for (i = 0; root && !ctx.failed && i < root->mAnimCount; i++) {
        uint32_t off = (uint32_t)(uintptr_t)root->mpAnims[i].data;
        AnimData* a = NULL;
        if (off) {
            /* entries may share an AnimData: reuse one decoded earlier (the
             * disc table is still intact; 0x1AC = mpAnims, 0x3C = .data) */
            uint32_t table = portBE32(ctx.blob + 0x1AC);
            for (j = 0; j < i; j++) {
                uint32_t prev = portBE32(ctx.blob + table + j * 0x40 + 0x3C);
                if (prev == off) {
                    a = root->mpAnims[j].data;
                    break;
                }
            }
            if (!a) {
                a = decodeAnim(&ctx, off);
            }
        }
        root->mpAnims[i].data = a;
    }
    if (root && !ctx.failed && layoutOk(&ctx, root) && indicesOk(&ctx, root)) {
        /* Payloads stay in the blob: none may sit in the bytes we overwrite. */
        if (rangeOk(&ctx, root->posIndex, 4, "posIndex") && rangeOk(&ctx, root->nrmIndex, 4, "nrmIndex") &&
            rangeOk(&ctx, root->clr0VtxBuffer, 4, "clr0VtxBuffer") && rangeOk(&ctx, root->clr0Index, 4, "clr0Index") &&
            rangeOk(&ctx, root->texVtxBuffer, 8, "texVtxBuffer") &&
            rangeOk(&ctx, root->groupVisibility, root->mBufferVisibilityNum, "groupVisibility")) {
            for (i = 0; i < 8; i++) {
                if (!rangeOk(&ctx, root->texIndex[i], 4, "texIndex")) {
                    break;
                }
            }
        }
    }
    portDecodeEnd(&ctx);
    if (!root || ctx.failed) {
        portDecodeArenaFree(&ctx.arena);
        return NULL;
    }

    lvl = LOCK();
    e = findLive(blob);
    if (e) {
        dropLive(e); /* an earlier model lived at this address */
    }
    if (sLiveCount == sLiveCap) {
        uint32_t cap = sLiveCap ? sLiveCap * 2 : 64;
        AnimLive* n = (AnimLive*)realloc(sLive, cap * sizeof(AnimLive));
        if (!n) {
            UNLOCK(lvl);
            ctx.failed = 0;
            portDecodeFail(&ctx, "out of memory (anim registry)");
            portDecodeArenaFree(&ctx.arena);
            return NULL;
        }
        sLive = n;
        sLiveCap = cap;
    }
    sLive[sLiveCount].blob = blob;
    sLive[sLiveCount].arena = ctx.arena;
    sLiveCount++;
    hdr.magic = PORT_ANIM_MAGIC;
    hdr.discSize = root->mSize;
    hdr.root = root;
    memcpy(blob, &hdr, sizeof(hdr));
    UNLOCK(lvl);
    return root;
}

void portAnimRelease(void* blob) {
    int lvl = LOCK();
    AnimLive* e = findLive(blob);
    if (e) {
        dropLive(e);
    }
    UNLOCK(lvl);
}

void portAnimForgetRange(const void* lo, const void* hi) {
    uint32_t i;
    int lvl = LOCK();
    for (i = 0; i < sLiveCount;) {
        if ((const uint8_t*)sLive[i].blob >= (const uint8_t*)lo && (const uint8_t*)sLive[i].blob < (const uint8_t*)hi) {
            dropLive(&sLive[i]); /* moves the last entry here */
        } else {
            i++;
        }
    }
    UNLOCK(lvl);
}

uint32_t portAnimLiveCount(void) {
    return sLiveCount;
}
