/* Room past the game's fixed heaps: port/include/port/heap_overflow.h.
 * Blocks come from the save-state heap and the list of them is in the
 * snapshot section (CMakeLists.txt), so a save state restores both. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "port/heap_overflow.h"
#include "port/mem_pc.h"
#include "port/snapheap.h"

typedef struct OverflowBlock {
    struct OverflowBlock* next;
    const void* owner;
    size_t size;
    void* raw; /* what portSnapMalloc returned */
} OverflowBlock;

#define ALIGN 32 /* as OSRoundUp32B and the game's heaps */

static OverflowBlock* sBlocks;
static unsigned sLogged; /* one log line per heap name hash bit */

static void* payload(OverflowBlock* b) {
    return (void*)(((uintptr_t)(b + 1) + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1));
}

void* portHeapOverflowAlloc(const void* owner, size_t size, const char* what) {
    void* raw;
    OverflowBlock* b;
    void* p;
    unsigned bit = 0;
    const char* c;

    if (size == 0) {
        return NULL;
    }
    raw = portSnapMalloc(sizeof(OverflowBlock) + ALIGN + size);
    if (raw == NULL) {
        return NULL;
    }
    b = (OverflowBlock*)(((uintptr_t)raw + 7) & ~(uintptr_t)7);
    b->owner = owner;
    b->size = size;
    b->raw = raw;
    b->next = sBlocks;
    sBlocks = b;
    p = payload(b);
    memset(p, 0, size);
    for (c = what; c != NULL && *c != '\0'; c++) {
        bit = bit * 31 + (unsigned char)*c;
    }
    bit = 1u << (bit & 31);
    if (!(sLogged & bit)) {
        sLogged |= bit;
        printf("[heap] %s is full: %u bytes from host memory instead (8-byte pointers make the game's data larger)\n",
               what != NULL ? what : "a game heap", (unsigned)size);
    }
    return p;
}

static void release(OverflowBlock* b) {
    void* p = payload(b);
    portMemForget(p, (const char*)p + b->size);
    portSnapFree(b->raw);
}

int portHeapOverflowFree(void* p) {
    OverflowBlock** link;
    if (p == NULL) {
        return 0;
    }
    for (link = &sBlocks; *link != NULL; link = &(*link)->next) {
        OverflowBlock* b = *link;
        if (payload(b) == p) {
            *link = b->next;
            release(b);
            return 1;
        }
    }
    return 0;
}

void portHeapOverflowClear(const void* owner) {
    OverflowBlock** link = &sBlocks;
    while (*link != NULL) {
        OverflowBlock* b = *link;
        if (b->owner == owner) {
            *link = b->next;
            release(b);
        } else {
            link = &b->next;
        }
    }
}
