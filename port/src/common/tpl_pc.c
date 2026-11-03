/* TPL header decoding on PC; see port/include/port/tpl_pc.h. */
#include "port/tpl_pc.h"
#include "port/bedecode.h"
#include "sdk/texPalette.h"

#include <stdlib.h>
#include <string.h>

extern const PortType portBE_tpl_TEXPalette;

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

typedef struct TplLive {
    void* blob;
    PortDecodeArena arena;
    uint32_t size;
} TplLive;

void (*portTplErrorHook)(struct PortDecodeCtx* ctx, const char* msg);

static TplLive* sLive;
static uint32_t sLiveCount, sLiveCap;

static TplLive* findLive(void* blob) {
    uint32_t i;
    for (i = 0; i < sLiveCount; i++) {
        if (sLive[i].blob == blob) {
            return &sLive[i];
        }
    }
    return NULL;
}

static void dropLive(TplLive* e) {
    portDecodeArenaFree(&e->arena);
    *e = sLive[--sLiveCount];
}

static int payloadOk(PortDecodeCtx* ctx, const void* p, const char* what) {
    if (p && (const uint8_t*)p < ctx->blob + sizeof(TEXPalette)) {
        portDecodeFail(ctx, "%s data at 0x%X overlaps the in-place native root", what,
                       (unsigned)((const uint8_t*)p - ctx->blob));
        return 0;
    }
    return 1;
}

TEXPalette* portTplDecode(void* blob, uint32_t size, const char* name) {
    PortDecodeCtx ctx;
    TEXPalette* root;
    TplLive* e;
    uint32_t i;
    int lvl;

    portDecodeBegin(&ctx, blob, size, name ? name : "tpl");
    ctx.onError = portTplErrorHook;
    if (!blob || size < sizeof(TEXPalette) || ((uintptr_t)blob & (_Alignof(TEXPalette) - 1))) {
        portDecodeFail(&ctx, "TPL blob %p (0x%X bytes) is too small or misaligned", blob, (unsigned)size);
        return NULL;
    }
    if (((TEXPalette*)blob)->versionNumber == PORT_TPL_VERSION) {
        return (TEXPalette*)blob; /* already native */
    }
    if (portBE32(blob) != PORT_TPL_VERSION) {
        portDecodeFail(&ctx, "invalid version number for texture palette (0x%08X)", (unsigned)portBE32(blob));
        return NULL;
    }
    root = (TEXPalette*)portDecode(&ctx, &portBE_tpl_TEXPalette, 0);
    portDecodeEnd(&ctx);
    for (i = 0; root && i < root->numDescriptors; i++) {
        TEXDescriptor* d = &root->descriptorArray[i];
        if ((d->textureHeader && !payloadOk(&ctx, d->textureHeader->data, "texel")) ||
            (d->CLUTHeader && !payloadOk(&ctx, d->CLUTHeader->data, "CLUT"))) {
            root = NULL;
        }
    }
    if (!root) {
        portDecodeArenaFree(&ctx.arena);
        return NULL;
    }

    lvl = LOCK();
    e = findLive(blob);
    if (e) {
        dropLive(e); /* an earlier TPL lived at this address */
    }
    if (sLiveCount == sLiveCap) {
        uint32_t cap = sLiveCap ? sLiveCap * 2 : 64;
        TplLive* n = (TplLive*)realloc(sLive, cap * sizeof(TplLive));
        if (!n) {
            UNLOCK(lvl);
            portDecodeFail(&ctx, "out of memory (TPL registry)");
            portDecodeArenaFree(&ctx.arena);
            return NULL;
        }
        sLive = n;
        sLiveCap = cap;
    }
    sLive[sLiveCount].blob = blob;
    sLive[sLiveCount].arena = ctx.arena;
    sLive[sLiveCount].size = size;
    sLiveCount++;
    memcpy(blob, root, sizeof(TEXPalette)); /* the root lives in place */
    UNLOCK(lvl);
    return (TEXPalette*)blob;
}

void portTplRelease(void* blob) {
    int lvl = LOCK();
    TplLive* e = findLive(blob);
    if (e) {
        dropLive(e);
    }
    UNLOCK(lvl);
}

void portTplForgetRange(const void* lo, const void* hi) {
    uint32_t i;
    int lvl = LOCK();
    for (i = 0; i < sLiveCount;) {
        if ((const uint8_t*)sLive[i].blob >= (const uint8_t*)lo && (const uint8_t*)sLive[i].blob < (const uint8_t*)hi) {
            dropLive(&sLive[i]);
        } else {
            i++;
        }
    }
    UNLOCK(lvl);
}

void* portTplDetach(void* blob) {
    TplLive* h = NULL;
    int lvl = LOCK();
    TplLive* e = findLive(blob);
    if (e) {
        h = (TplLive*)malloc(sizeof(TplLive));
        if (h) {
            *h = *e;
            *e = sLive[--sLiveCount]; /* unregister, keep the arena */
        }
    }
    UNLOCK(lvl);
    return h;
}

/* Repoints one data pointer from the old blob into the new one. */
static void* moveData(void* p, const TplLive* h, uint8_t* newBlob) {
    uint8_t* old = (uint8_t*)h->blob;
    if (p && (uint8_t*)p >= old && (uint8_t*)p < old + h->size) {
        return newBlob + ((uint8_t*)p - old);
    }
    return p;
}

void portTplAttach(void* handle, void* newBlob) {
    TplLive* h = (TplLive*)handle;
    TEXPalette* root = (TEXPalette*)newBlob;
    uint32_t i, j;
    int lvl;
    if (!h) {
        return;
    }
    if (!newBlob || root->versionNumber != PORT_TPL_VERSION) {
        portDecodeArenaFree(&h->arena); /* not the copied native root: drop the graph */
        free(h);
        return;
    }
    if (newBlob != h->blob) {
        /* Headers may be shared between descriptors: move each one once. */
        for (i = 0; i < root->numDescriptors; i++) {
            TEXDescriptor* d = &root->descriptorArray[i];
            int seenTex = 0, seenClut = 0;
            for (j = 0; j < i; j++) {
                seenTex |= d->textureHeader && root->descriptorArray[j].textureHeader == d->textureHeader;
                seenClut |= d->CLUTHeader && root->descriptorArray[j].CLUTHeader == d->CLUTHeader;
            }
            if (d->textureHeader && !seenTex) {
                d->textureHeader->data = moveData(d->textureHeader->data, h, (uint8_t*)newBlob);
            }
            if (d->CLUTHeader && !seenClut) {
                d->CLUTHeader->data = moveData(d->CLUTHeader->data, h, (uint8_t*)newBlob);
            }
        }
    }
    lvl = LOCK();
    {
        TplLive* e = findLive(newBlob);
        if (e) {
            dropLive(e);
        }
        if (sLiveCount == sLiveCap) {
            uint32_t cap = sLiveCap ? sLiveCap * 2 : 64;
            TplLive* n = (TplLive*)realloc(sLive, cap * sizeof(TplLive));
            if (!n) {
                UNLOCK(lvl);
                portDecodeArenaFree(&h->arena);
                free(h);
                return;
            }
            sLive = n;
            sLiveCap = cap;
        }
        h->blob = newBlob;
        sLive[sLiveCount++] = *h;
    }
    UNLOCK(lvl);
    free(h);
}

uint32_t portTplLiveCount(void) {
    return sLiveCount;
}
