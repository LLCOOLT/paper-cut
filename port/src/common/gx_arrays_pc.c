/* Vertex-array registry; see port/include/port/gx_arrays.h. */
#include "port/gx_arrays.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT_GX_ARRAY_REGISTRY 64

typedef struct {
    const void* data;
    unsigned int size;
    int le;
} ArrayEntry;

typedef struct {
    const uint8_t* base;
    unsigned int size;
} BlobEntry;

/* Arrays: small ring; few are live at once. */
static ArrayEntry sArrays[PORT_GX_ARRAY_REGISTRY];
static unsigned int sArrayNext;
/* Disc blobs: one per live anim model or map file. It held 64 and, full, overwrote slot 0: from then on
 * each newly loaded model lost its slot to the next one, its big-endian vertices were read as native and
 * it drew as nothing but its shadow (chapter 5's ruin, a long run in). The file manager has 1024 entries
 * and the maps two, so this many never fill; lookups stop at the highest slot used. */
#define PORT_GX_BLOB_REGISTRY 1100
static BlobEntry sBlobs[PORT_GX_BLOB_REGISTRY];
static unsigned int sBlobTop; /* slots in use or since freed */
static unsigned int sBlobCap; /* PORT_GX_BLOB_REGISTRY, or TTYD_GX_BLOB_CAP to see the old overflow */
static void portGXForgetArrayRanges(const void* base, unsigned int size);

static void registerArray(const void* data, unsigned int size, int le) {
    unsigned int i;
    if (!data) {
        return;
    }
    for (i = 0; i < PORT_GX_ARRAY_REGISTRY; i++) {
        if (sArrays[i].data == data) {
            sArrays[i].size = size;
            sArrays[i].le = le;
            return;
        }
    }
    sArrays[sArrayNext].data = data;
    sArrays[sArrayNext].size = size;
    sArrays[sArrayNext].le = le;
    sArrayNext = (sArrayNext + 1) % PORT_GX_ARRAY_REGISTRY;
}

void portGXRegisterArray(const void* data, unsigned int size) {
    registerArray(data, size, 0);
}

void portGXRegisterNativeArray(const void* data, unsigned int size) {
    registerArray(data, size, 1);
}

void portGXRegisterDiscBlob(const void* base, unsigned int size) {
    unsigned int i, freeSlot = sBlobTop;
    if (!base) {
        return;
    }
    for (i = 0; i < sBlobTop; i++) {
        if (sBlobs[i].base == base) {
            sBlobs[i].size = size;
            return;
        }
        if (!sBlobs[i].base && freeSlot == sBlobTop) {
            freeSlot = i;
        }
    }
    if (sBlobCap == 0) {
        const char* lim = getenv("TTYD_GX_BLOB_CAP");
        sBlobCap = lim != NULL && atoi(lim) > 0 && atoi(lim) < PORT_GX_BLOB_REGISTRY ? (unsigned int)atoi(lim)
                                                                                     : PORT_GX_BLOB_REGISTRY;
    }
    if (freeSlot == sBlobTop) {
        if (sBlobTop < sBlobCap) {
            if (++sBlobTop == 65) {
                printf("[gx] disc blobs: 65 slots in use (the old registry held 64)\n");
                fflush(stdout);
            }
        } else {
            /* the model in the slot overwritten draws as garbage from now on: say so */
            printf("[gx] disc blob registry full (%u): slot 0 overwritten\n", sBlobCap);
            fflush(stdout);
            freeSlot = 0;
        }
    }
    sBlobs[freeSlot].base = (const uint8_t*)base;
    sBlobs[freeSlot].size = size;
    portGXForgetArrayRanges(base, size);
}

void portGXUnregisterDiscBlob(const void* base) {
    unsigned int i;
    unsigned int size = 0;
    for (i = 0; i < sBlobTop; i++) {
        if (sBlobs[i].base == base) {
            if (sBlobs[i].size > size) {
                size = sBlobs[i].size;
            }
            sBlobs[i].base = NULL;
            sBlobs[i].size = 0;
        }
    }
    /* Arrays registered inside the blob go stale with it. */
    for (i = 0; i < PORT_GX_ARRAY_REGISTRY; i++) {
        const uint8_t* p = (const uint8_t*)sArrays[i].data;
        if (p != NULL && p >= (const uint8_t*)base && (p == base || p < (const uint8_t*)base + size)) {
            sArrays[i].data = NULL;
        }
    }
}

/* Array ranges known at draw time (skinned vertex buffers, a model's
 * arrays): a small ring, refreshed before each draw that uses one. Looked up
 * before disc blobs, so they bound an array more tightly than its file. */
#define PORT_GX_RANGE_RING 64
static ArrayEntry sRanges[PORT_GX_RANGE_RING];
static unsigned int sRangeNext;

void portGXRegisterArrayRange(const void* base, unsigned int size, int le) {
    unsigned int i;
    if (!base) {
        return;
    }
    /* Older ranges over this memory describe freed buffers (lookups take the
     * first match, so a stale big-endian range would win): drop them. */
    for (i = 0; i < PORT_GX_RANGE_RING; i++) {
        const uint8_t* r = (const uint8_t*)sRanges[i].data;
        if (r && r != (const uint8_t*)base && r < (const uint8_t*)base + size &&
            (const uint8_t*)base < r + sRanges[i].size) {
            sRanges[i].data = NULL;
        }
    }
    for (i = 0; i < PORT_GX_RANGE_RING; i++) {
        if (sRanges[i].data == base) {
            sRanges[i].size = size;
            sRanges[i].le = le;
            return;
        }
    }
    sRanges[sRangeNext].data = base;
    sRanges[sRangeNext].size = size;
    sRanges[sRangeNext].le = le;
    sRangeNext = (sRangeNext + 1) % PORT_GX_RANGE_RING;
}

/* A new disc blob reuses memory: ranges registered over it are stale. */
static void portGXForgetArrayRanges(const void* base, unsigned int size) {
    const uint8_t* lo = (const uint8_t*)base;
    unsigned int i;
    for (i = 0; i < PORT_GX_RANGE_RING; i++) {
        const uint8_t* r = (const uint8_t*)sRanges[i].data;
        if (r && r < lo + size && lo < r + sRanges[i].size) {
            sRanges[i].data = NULL;
        }
    }
}

void portGXForgetRange(const void* lo_, const void* hi_) {
    const uint8_t* lo = (const uint8_t*)lo_;
    const uint8_t* hi = (const uint8_t*)hi_;
    unsigned int i;
    for (i = 0; i < sBlobTop; i++) {
        if (sBlobs[i].base && sBlobs[i].base >= lo && sBlobs[i].base < hi) {
            sBlobs[i].base = NULL;
            sBlobs[i].size = 0;
        }
    }
    for (i = 0; i < PORT_GX_ARRAY_REGISTRY; i++) {
        const uint8_t* p = (const uint8_t*)sArrays[i].data;
        if (p && p >= lo && p < hi) {
            sArrays[i].data = NULL;
        }
    }
    for (i = 0; i < PORT_GX_RANGE_RING; i++) {
        const uint8_t* p = (const uint8_t*)sRanges[i].data;
        if (p && p >= lo && p < hi) {
            sRanges[i].data = NULL;
        }
    }
}

int portGXIsDiscData(const void* data) {
    const uint8_t* p = (const uint8_t*)data;
    unsigned int i;
    if (!data) {
        return 0;
    }
    for (i = 0; i < PORT_GX_ARRAY_REGISTRY; i++) {
        if (sArrays[i].data == data) {
            return !sArrays[i].le;
        }
    }
    for (i = 0; i < sBlobTop; i++) {
        if (sBlobs[i].base && p >= sBlobs[i].base && p < sBlobs[i].base + sBlobs[i].size) {
            return 1;
        }
    }
    return 0;
}

int portGXArrayInfo(const void* data, unsigned int* size, int* le) {
    const uint8_t* p = (const uint8_t*)data;
    unsigned int i;
    *size = 0;
    *le = 1;
    if (!data) {
        return 0;
    }
    for (i = 0; i < PORT_GX_ARRAY_REGISTRY; i++) {
        if (sArrays[i].data == data) {
            *size = sArrays[i].size;
            *le = sArrays[i].le;
            return 1;
        }
    }
    for (i = 0; i < PORT_GX_RANGE_RING; i++) {
        const uint8_t* r = (const uint8_t*)sRanges[i].data;
        if (r && p >= r && p < r + sRanges[i].size) {
            *size = (unsigned int)(r + sRanges[i].size - p);
            *le = sRanges[i].le;
            return 1;
        }
    }
    for (i = 0; i < sBlobTop; i++) {
        if (sBlobs[i].base && p >= sBlobs[i].base && p < sBlobs[i].base + sBlobs[i].size) {
            *size = (unsigned int)(sBlobs[i].base + sBlobs[i].size - p);
            *le = 0;
            return 1;
        }
    }
    return 0;
}
