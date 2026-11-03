/* ARAM (16 MiB audio RAM) as a host buffer; ARAM addresses are offsets.
 * ARQ transfers are plain copies that complete immediately. */
#undef ARQPostRequest /* this is the backend's own (port_prefix.h routes the game's calls) */
#include <dolphin.h>
#include <dolphin/ar.h>
#include <dolphin/arq.h>
#include <stdlib.h>
#include <string.h>
#include "port/ar_pc.h"

#define ARAM_SIZE (16u * 1024 * 1024)
/* The first 16 KiB of ARAM are reserved for the OS, as on hardware. */
#define ARAM_RESERVED 0x4000u

static u8* sAram;
static u32 sAllocTop;
static u32* sStack;
static u32 sStackMax;
static u32 sStackCount;

u8* portAramPtr(u32 aramAddr) {
    return sAram + aramAddr;
}

u32 ARInit(u32* stack_index_addr, u32 num_entries) {
    if (sAram == NULL) {
        sAram = calloc(1, ARAM_SIZE);
    }
    sAllocTop = ARAM_RESERVED;
    sStack = stack_index_addr;
    sStackMax = num_entries;
    sStackCount = 0;
    return sAllocTop;
}

BOOL ARCheckInit(void) {
    return sAram != NULL;
}

u32 ARGetSize(void) {
    return ARAM_SIZE;
}

u32 ARAlloc(u32 length) {
    u32 addr = sAllocTop;
    if (addr + length > ARAM_SIZE || sStackCount == sStackMax) {
        OSPanic(__FILE__, __LINE__, "ARAlloc: out of ARAM (0x%x)", length);
    }
    sStack[sStackCount++] = length;
    sAllocTop += length;
    return addr;
}

u32 ARFree(u32* length) {
    u32 len;
    if (sStackCount == 0) {
        return sAllocTop;
    }
    len = sStack[--sStackCount];
    sAllocTop -= len;
    if (length != NULL) {
        *length = len;
    }
    return sAllocTop;
}

void ARQInit(void) {}

void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, ARQAddr source, ARQAddr dest,
                    u32 length, ARQCallback callback) {
    request->owner = owner;
    request->type = type;
    request->priority = priority;
    request->source = source;
    request->dest = dest;
    request->length = length;
    request->callback = callback;
    if (type == ARQ_TYPE_MRAM_TO_ARAM) {
        memcpy(sAram + dest, (void*)(uintptr_t)source, length);
    } else {
        memcpy((void*)(uintptr_t)dest, sAram + source, length);
    }
    if (callback != NULL) {
        callback((ARQAddr)request);
    }
}
