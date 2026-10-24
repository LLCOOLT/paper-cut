/* Vertex-array registry; see port/include/port/gx_arrays.h. */
#include "port/gx_arrays.h"

#include <stddef.h>

#define PORT_GX_ARRAY_REGISTRY 64
#define PORT_GX_BLOB_REGISTRY 64

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
static BlobEntry sBlobs[PORT_GX_BLOB_REGISTRY];
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
    unsigned int i, freeSlot = PORT_GX_BLOB_REGISTRY;
    if (!base) {
        return;
    }
    for (i = 0; i < PORT_GX_BLOB_REGISTRY; i++) {
        if (sBlobs[i].base == base) {
            sBlobs[i].size = size;
            return;
        }
        if (!sBlobs[i].base && freeSlot == PORT_GX_BLOB_REGISTRY) {
            freeSlot = i;
        }
    }
    if (freeSlot == PORT_GX_BLOB_REGISTRY) {
        freeSlot = 0; /* full: overwrite the oldest slot rather than fail */
    }
    sBlobs[freeSlot].base = (const uint8_t*)base;
    sBlobs[freeSlot].size = size;
    portGXForgetArrayRanges(base, size);
}

void portGXUnregisterDiscBlob(const void* base) {
    unsigned int i;
    unsigned int size = 0;
    for (i = 0; i < PORT_GX_BLOB_REGISTRY; i++) {
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
    for (i = 0; i < PORT_GX_BLOB_REGISTRY; i++) {
        if (sBlobs[i].base && p >= sBlobs[i].base && p < sBlobs[i].base + sBlobs[i].size) {
            *size = (unsigned int)(sBlobs[i].base + sBlobs[i].size - p);
            *le = 0;
            return 1;
        }
    }
    return 0;
}
