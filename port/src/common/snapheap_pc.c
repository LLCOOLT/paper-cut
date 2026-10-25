/* The save-state heap (port/include/port/snapheap.h). One region reserved
 * at start (pages are committed as they are touched); blocks carry boundary
 * tags (their size and the previous block's) and free blocks are on a list
 * kept inside them, so everything the allocator knows is in the region and a
 * copy of [base, top) is a complete picture of it. First fit, split, and
 * coalesce on free; a free block at the end lowers the top. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/snapheap.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#endif

#define REGION_SIZE ((size_t)1 << 30) /* reserved; only what is used is committed */
#define ALIGN 16
#define HDR 16       /* the block header: its size (bit 0: in use) and the previous block's */
#define MIN_BLOCK 48 /* a free block holds its header and the list links */

typedef struct Block {
    size_t size;     /* this block's, header included; bit 0: in use */
    size_t prevSize; /* the block before it (0: the first) */
    struct Block* next; /* free blocks only */
    struct Block* prev;
} Block;

typedef struct Header {
    uint64_t magic;
    size_t top;      /* offset of the end of the last block */
    size_t lastSize; /* size of the last block (the one ending at top) */
    Block* freeList;
    uint8_t pad[32];
} Header;

#define FIRST_BLOCK sizeof(Header)

static uint8_t* sBase;
#if defined(_WIN32)
static CRITICAL_SECTION sLock;
static INIT_ONCE sLockOnce = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK initLock(PINIT_ONCE once, PVOID param, PVOID* ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&sLock);
    return TRUE;
}
static void lock(void) {
    InitOnceExecuteOnce(&sLockOnce, initLock, NULL, NULL);
    EnterCriticalSection(&sLock);
}
static void unlock(void) {
    LeaveCriticalSection(&sLock);
}
#else
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) {
    pthread_mutex_lock(&sLock);
}
static void unlock(void) {
    pthread_mutex_unlock(&sLock);
}
#endif

static Header* header(void) {
    return (Header*)sBase;
}

static int init(void) {
    if (sBase != NULL) {
        return 1;
    }
#if defined(_WIN32)
    sBase = (uint8_t*)VirtualAlloc(NULL, REGION_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    {
        void* p = mmap(NULL, REGION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        sBase = p == MAP_FAILED ? NULL : (uint8_t*)p;
    }
#endif
    if (sBase == NULL) {
        fprintf(stderr, "snapheap: cannot reserve %zu MiB\n", REGION_SIZE >> 20);
        abort();
    }
    header()->magic = 0x5041454850414e53ull; /* "SNAPHEAP" */
    header()->top = FIRST_BLOCK;
    header()->lastSize = 0;
    header()->freeList = NULL;
    return 1;
}

static size_t blockSize(const Block* b) {
    return b->size & ~(size_t)1;
}

static int inUse(const Block* b) {
    return (int)(b->size & 1);
}

static Block* nextBlock(Block* b) {
    return (Block*)((uint8_t*)b + blockSize(b));
}

static int isEnd(const Block* b) {
    return (const uint8_t*)b >= sBase + header()->top;
}

static void listRemove(Block* b) {
    if (b->prev != NULL) {
        b->prev->next = b->next;
    } else {
        header()->freeList = b->next;
    }
    if (b->next != NULL) {
        b->next->prev = b->prev;
    }
}

static void listPush(Block* b) {
    b->prev = NULL;
    b->next = header()->freeList;
    if (b->next != NULL) {
        b->next->prev = b;
    }
    header()->freeList = b;
}

/* the block after b (if any) learns b's size */
static void linkNext(Block* b) {
    Block* n = nextBlock(b);
    if (!isEnd(n)) {
        n->prevSize = blockSize(b);
    } else {
        header()->lastSize = blockSize(b);
    }
}

static void* allocLocked(size_t size) {
    size_t need = (size + HDR + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    Block* b;
    if (need < MIN_BLOCK) {
        need = MIN_BLOCK;
    }
    for (b = header()->freeList; b != NULL; b = b->next) {
        if (blockSize(b) >= need) {
            break;
        }
    }
    if (b != NULL) {
        listRemove(b);
        if (blockSize(b) - need >= MIN_BLOCK) {
            Block* rest = (Block*)((uint8_t*)b + need);
            rest->size = blockSize(b) - need;
            rest->prevSize = need;
            b->size = need;
            linkNext(rest);
            listPush(rest);
        }
        b->size |= 1;
        return (uint8_t*)b + HDR;
    }
    if (header()->top + need > REGION_SIZE) {
        return NULL;
    }
    b = (Block*)(sBase + header()->top);
    b->size = need | 1;
    b->prevSize = header()->lastSize;
    header()->top += need;
    header()->lastSize = need;
    return (uint8_t*)b + HDR;
}

static void freeLocked(void* p) {
    Block* b = (Block*)((uint8_t*)p - HDR);
    Block* n;
    b->size &= ~(size_t)1;
    n = nextBlock(b);
    if (!isEnd(n) && !inUse(n)) {
        listRemove(n);
        b->size += blockSize(n);
    }
    if (b->prevSize != 0) {
        Block* prev = (Block*)((uint8_t*)b - b->prevSize);
        if (!inUse(prev)) {
            listRemove(prev);
            prev->size += blockSize(b);
            b = prev;
        }
    }
    if (isEnd(nextBlock(b))) {
        /* the last block: the top comes down */
        header()->top = (size_t)((uint8_t*)b - sBase);
        header()->lastSize = b->prevSize;
        return;
    }
    linkNext(b);
    listPush(b);
}

void* portSnapMalloc(size_t size) {
    void* p;
    lock();
    init();
    p = allocLocked(size);
    unlock();
    return p;
}

void* portSnapCalloc(size_t count, size_t size) {
    void* p;
    if (size != 0 && count > (size_t)-1 / size) {
        return NULL;
    }
    p = portSnapMalloc(count * size);
    if (p != NULL) {
        memset(p, 0, count * size);
    }
    return p;
}

void* portSnapRealloc(void* p, size_t size) {
    void* q;
    size_t have;
    if (p == NULL) {
        return portSnapMalloc(size);
    }
    if (size == 0) {
        portSnapFree(p);
        return NULL;
    }
    have = blockSize((Block*)((uint8_t*)p - HDR)) - HDR;
    if (size <= have) {
        return p;
    }
    q = portSnapMalloc(size);
    if (q != NULL) {
        memcpy(q, p, have);
        portSnapFree(p);
    }
    return q;
}

void portSnapFree(void* p) {
    if (p == NULL) {
        return;
    }
    lock();
    freeLocked(p);
    unlock();
}

void portSnapHeapExtent(void** base, size_t* size) {
    lock();
    init();
    *base = sBase;
    *size = header()->top;
    unlock();
}

void portSnapHeapRestored(void) {
    /* nothing past the restored top is reachable: the allocator's own data
     * (the header, the free list, the tags) came back with the region */
}
