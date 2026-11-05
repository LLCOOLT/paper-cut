/* Rewind (port/include/port/rewind.h): the last seconds of play, a state
 * every EVERY frames, and while the control is held a step back through
 * them every STEP frames, each drawn by the frame the game then runs (at
 * EVERY 6 and STEP 2, back at twice the speed of play). Letting go plays on
 * from the state last put back.
 *
 * The states are save states' (savestate_aurora.c: the same memory, the
 * same rest), kept as differences: a mirror holds the newest state whole,
 * and each state taken compares the game's memory with it page by page,
 * keeps the mirror's old pages that changed (packed, the save states' codec)
 * and copies the new ones in. On Windows only the pages written since the
 * last look are compared (MEM1, ARAM and the heap are write-watched: see
 * visit), so a state costs about a millisecond; elsewhere every page is,
 * which reads the whole state (about 155 MiB, most of it MEM1) each time. So the ring holds, newest first, what turns
 * each state into the one before it; the oldest falls off the end at the
 * length the settings give or at CAP bytes. A step back first makes the
 * game's memory the mirror's again (the frames run since), then each later
 * step unpacks the newest record into the mirror and does the same.
 *
 * Taken only where a save state can be (in the field or a battle, no memory
 * card access), without pausing the clock or the audio device (the audio
 * callback is only held off while the memory is read); put back paused, as a
 * save state is loaded. Silent while stepping back. Off in online co-op and replays
 * (one game alone can't go back) and where save states are off.
 *
 * Tests: an input script's `rewind N` holds it N frames (portRewindTest);
 * $TTYD_REWIND_LOG logs each state's and step's cost. */
#include "port/rewind.h"
#include "port/savestate.h"

#if defined(TTYD_SAVE_STATES)
#include <SDL3/SDL.h>
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/seqdrv.h"
#include "port/lua_mods.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/snapheap.h"

void salPcSetMuted(int muted); /* hw_pc.c */

#define PAGE 4096u
#define EVERY 6   /* frames between states */
#define STEP 2    /* frames between steps back */
#define CAP (64u << 20)
#define MAX_REGIONS 512
#define PACK_BLOCK (1u << 20) /* the old pages packed in blocks of this */

typedef struct Mirror {
    uint8_t* buf;
    size_t size; /* the region's size at the newest state */
    size_t cap;
} Mirror;

/* One record: from a state to the one before it. */
typedef struct Record {
    PortSnapMeta meta; /* the state before's */
    uint32_t pages;
    uint32_t* index;   /* per page: region << 20 | page */
    uint8_t* packed;   /* the old pages, raw size pages * PAGE, in blocks: u32 raw, u32 packed (0: raw), bytes */
    size_t packedSize;
} Record;

static Mirror sMirror[MAX_REGIONS];
static int sRegions;        /* sMirror's count, 0: no state yet */
static PortSnapMeta sHead;  /* the mirror's state's rest */
static Record* sRing;       /* newest at sNewest, going back */
static int sRingCap, sCount, sNewest;
static size_t sRingBytes;
static size_t sHeapMax; /* the save-state heap's largest top yet */
static int sHeld;
static u32 sTestUntil; /* held until this retrace (tests) */
static int sHolding;     /* the control held (with states to go back to) */
static int sActive;      /* steps back taken while held */
static int sSince;       /* frames since the last state or step */
static int sLog = -1;
static uint8_t* sStage;  /* the old pages, raw, before packing */
static size_t sStageCap;
static double sCaptureMs, sCaptureMax;
static int sCaptures;
static int* sOn;
static int* sSeconds;

static int logging(void) {
    if (sLog < 0) {
        sLog = getenv("TTYD_REWIND_LOG") != NULL;
    }
    return sLog;
}

static Record* recordAt(int back) { /* 0: the newest */
    return &sRing[(sNewest - back + sRingCap) % sRingCap];
}

static void freeRecord(Record* r) {
    free(r->index);
    free(r->packed);
    memset(r, 0, sizeof(*r));
}

static void clearAll(void) {
    while (sCount > 0) {
        freeRecord(recordAt(--sCount));
    }
    sRingBytes = 0;
    sRegions = 0;
}

static size_t recordBytes(const Record* r) {
    return r->packedSize + r->pages * sizeof(uint32_t) + sizeof(*r);
}

static int maxRecords(void) {
    const int seconds = sSeconds != NULL ? *sSeconds : 10;
    return seconds * 60 / EVERY;
}

/* the oldest dropped while over the length or the memory cap */
static void trim(void) {
    while (sCount > 0 && (sCount > maxRecords() || sRingBytes > CAP)) {
        Record* r = recordAt(sCount - 1);
        sRingBytes -= recordBytes(r);
        freeRecord(r);
        sCount--;
    }
}

static int grow(Mirror* m, size_t size) {
    if (size > m->cap) {
        /* a page's multiple; a region that grows (the heap) a MiB at a time */
        const size_t step = m->cap != 0 ? (1u << 20) : PAGE;
        const size_t cap = (size + step - 1) & ~(size_t)(step - 1);
        uint8_t* b = (uint8_t*)realloc(m->buf, cap);
        if (b == NULL) {
            return 0;
        }
        memset(b + m->cap, 0, cap - m->cap);
        m->buf = b;
        m->cap = cap;
    }
    return 1;
}

static int stage(size_t need) {
    if (need > sStageCap) {
        size_t cap = sStageCap ? sStageCap : (4u << 20);
        uint8_t* b;
        while (cap < need) {
            cap *= 2;
        }
        b = (uint8_t*)realloc(sStage, cap);
        if (b == NULL) {
            return 0;
        }
        sStage = b;
        sStageCap = cap;
    }
    return 1;
}

static uint32_t pageIndex(int region, size_t page) {
    return (uint32_t)region << 20 | (uint32_t)page;
}

static size_t pageLen(size_t size, size_t page) {
    const size_t off = page * PAGE;
    return size - off < PAGE ? size - off : PAGE;
}

/* Which pages of a region to look at. On Windows MEM1, ARAM (Aurora,
 * port/patches/aurora/0020) and the save-state heap (snapheap_pc.c) are
 * write-watched: only the pages written since the last look, the watch
 * reset. Elsewhere, and for the game's sections, every page. Returns the
 * count in sVisit, or -1 for every page. */
#if defined(_WIN32)
__declspec(dllimport) unsigned int __stdcall GetWriteWatch(unsigned long flags, void* base, size_t size,
                                                           void** addresses, uintptr_t* count,
                                                           unsigned long* granularity);
#define WRITE_WATCH_FLAG_RESET 1
#endif
static void** sVisit;
static size_t sVisitCap;

static long visit(const PortSnapRegion* r) {
#if defined(_WIN32)
    const size_t pages = (r->size + PAGE - 1) / PAGE;
    uintptr_t count;
    unsigned long gran = 0;
    static int sNoWatch = -1;
    if (sNoWatch < 0) {
        sNoWatch = getenv("TTYD_REWIND_NO_WATCH") != NULL; /* tests: every page compared, as elsewhere */
    }
    if (sNoWatch || r->size < (1u << 20) || ((uintptr_t)r->addr & (PAGE - 1)) != 0) {
        return -1; /* (the sections: small, and not in a watched allocation) */
    }
    if (pages > sVisitCap) {
        void** v = (void**)realloc(sVisit, pages * sizeof(void*));
        if (v == NULL) {
            return -1;
        }
        sVisit = v;
        sVisitCap = pages;
    }
    count = pages;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, r->addr, r->size, sVisit, &count, &gran) != 0 || gran != PAGE) {
        return -1;
    }
    return (long)count;
#else
    (void)r;
    return -1;
#endif
}

/* the page visit's ith (all: i itself) */
static size_t visitPage(const PortSnapRegion* r, long count, long i) {
    return count < 0 ? (size_t)i : (size_t)(((uint8_t*)sVisit[i] - (uint8_t*)r->addr) / PAGE);
}

static long visitCount(const PortSnapRegion* r, long count) {
    return count < 0 ? (long)((r->size + PAGE - 1) / PAGE) : count;
}

/* the regions, the heap's at the most it has been (so the pages past its
 * top now still match the mirror, and a heap that grows again is right) */
static int regionsNow(PortSnapRegion* r, size_t heapTop) {
    if (heapTop > sHeapMax) {
        sHeapMax = heapTop;
    }
    return portSnapRegions(r, MAX_REGIONS, sHeapMax);
}

/* A state: the differences from the mirror's into a new record. */
static void capture(void) {
    PortSnapRegion r[MAX_REGIONS];
    const Uint64 t0 = SDL_GetTicksNS();
    void* heapBase;
    size_t heapTop;
    uint32_t* index = NULL;
    size_t pages = 0, indexCap = 0, looked = 0;
    int n, i;
    Record rec;
    portSnapHeapExtent(&heapBase, &heapTop);
    n = regionsNow(r, heapTop);
    if (sRegions != 0 && n != sRegions) {
        clearAll(); /* (can't happen: the regions are fixed but for the heap's size) */
    }
    for (i = 0; i < n; i++) {
        if (!grow(&sMirror[i], r[i].size)) {
            clearAll();
            return;
        }
    }
    if (sRegions == 0) {
        size_t total = 0;
        for (i = 0; i < n; i++) {
            visit(&r[i]); /* the watch from here */
            memcpy(sMirror[i].buf, r[i].addr, r[i].size);
            total += r[i].size;
        }
        if (!portSnapMetaTake(&sHead)) {
            return;
        }
        sRegions = n;
        if (logging()) {
            printf("[rewind] the first state: %d regions, %zu KiB (%.2f ms)\n", n, total >> 10,
                   (double)(SDL_GetTicksNS() - t0) / 1e6);
            fflush(stdout);
        }
        return;
    }
    memset(&rec, 0, sizeof(rec));
    for (i = 0; i < n; i++) {
        const uint8_t* mem = (const uint8_t*)r[i].addr;
        uint8_t* mir = sMirror[i].buf;
        const long v = visit(&r[i]), count = visitCount(&r[i], v);
        long k;
        looked += (size_t)count;
        for (k = 0; k < count; k++) {
            const size_t p = visitPage(&r[i], v, k);
            const size_t off = p * PAGE, len = pageLen(r[i].size, p);
            if (memcmp(mem + off, mir + off, len) == 0) {
                continue;
            }
            if (pages == indexCap) {
                uint32_t* ni;
                indexCap = indexCap ? indexCap * 2 : 1024;
                ni = (uint32_t*)realloc(index, indexCap * sizeof(uint32_t));
                if (ni == NULL || !stage(indexCap * PAGE)) {
                    free(ni != NULL ? ni : index);
                    clearAll();
                    return;
                }
                index = ni;
            }
            index[pages] = pageIndex(i, p);
            memcpy(sStage + pages * PAGE, mir + off, len);
            memcpy(mir + off, mem + off, len);
            pages++;
        }
    }
    rec.meta = sHead;
    if (!portSnapMetaTake(&sHead)) {
        free(index);
        clearAll(); /* too many files open: start over */
        return;
    }
    rec.pages = (uint32_t)pages;
    rec.index = index;
    if (pages > 0) {
        const size_t raw = pages * PAGE;
        size_t used = 0, off;
        rec.packed = (uint8_t*)malloc(raw + (raw / PACK_BLOCK + 1) * 8);
        if (rec.packed == NULL) {
            free(index);
            clearAll();
            return;
        }
        for (off = 0; off < raw; off += PACK_BLOCK) {
            const size_t len = raw - off < PACK_BLOCK ? raw - off : PACK_BLOCK;
            uint32_t hdr[2];
            size_t packed = portSnapPack(rec.packed + used + 8, len, sStage + off, len);
            if (packed == 0 || packed >= len) {
                memcpy(rec.packed + used + 8, sStage + off, len);
                packed = 0;
            }
            hdr[0] = (uint32_t)len;
            hdr[1] = (uint32_t)packed;
            memcpy(rec.packed + used, hdr, 8);
            used += 8 + (packed != 0 ? packed : len);
        }
        {
            uint8_t* shrunk = (uint8_t*)realloc(rec.packed, used);
            if (shrunk != NULL) {
                rec.packed = shrunk;
            }
        }
        rec.packedSize = used;
    }
    if (sCount == sRingCap) {
        const int cap = sRingCap ? sRingCap * 2 : 128;
        Record* nr = (Record*)calloc((size_t)cap, sizeof(Record));
        int k;
        if (nr == NULL) {
            freeRecord(&rec);
            clearAll();
            return;
        }
        for (k = 0; k < sCount; k++) { /* oldest first, so the newest ends at sCount - 1 */
            nr[k] = *recordAt(sCount - 1 - k);
        }
        free(sRing);
        sRing = nr;
        sRingCap = cap;
        sNewest = sCount - 1;
    }
    sNewest = (sNewest + 1) % sRingCap;
    sRing[sNewest] = rec;
    sCount++;
    sRingBytes += recordBytes(&rec);
    trim();
    {
        const double ms = (double)(SDL_GetTicksNS() - t0) / 1e6;
        sCaptureMs += ms;
        if (ms > sCaptureMax) {
            sCaptureMax = ms;
        }
        sCaptures++;
        if (logging()) {
            printf("[rewind] retrace %u: state, %zu pages looked at, %u changed, %zu KiB packed, %.2f ms; %d kept, "
                   "%.1f MiB\n",
                   (unsigned)VIGetRetraceCount(), looked, rec.pages, rec.packedSize >> 10, ms, sCount,
                   (double)sRingBytes / (1 << 20));
            fflush(stdout);
        }
    }
}

/* The newest record into the mirror and the game's memory: the mirror's
 * state becomes the one before. */
static void popRecord(const PortSnapRegion* r, int n) {
    Record* rec = recordAt(0);
    const uint8_t* p = rec->packed;
    size_t off = 0, raw = (size_t)rec->pages * PAGE;
    uint32_t k;
    if (!stage(raw)) {
        return;
    }
    while (off < raw) {
        uint32_t hdr[2];
        memcpy(hdr, p, 8);
        p += 8;
        if (hdr[1] == 0) {
            memcpy(sStage + off, p, hdr[0]);
            p += hdr[0];
        } else {
            portSnapUnpack(sStage + off, hdr[0], p, hdr[1]);
            p += hdr[1];
        }
        off += hdr[0];
    }
    for (k = 0; k < rec->pages; k++) {
        const int region = (int)(rec->index[k] >> 20);
        const size_t page = rec->index[k] & 0xFFFFFu;
        const size_t at = page * PAGE;
        if (region < n && at < r[region].size) {
            const size_t len = pageLen(r[region].size, page);
            memcpy(sMirror[region].buf + at, sStage + (size_t)k * PAGE, len);
            memcpy((uint8_t*)r[region].addr + at, sStage + (size_t)k * PAGE, len);
        }
    }
    sHead = rec->meta;
    sRingBytes -= recordBytes(rec);
    freeRecord(rec);
    sNewest = (sNewest - 1 + sRingCap) % sRingCap;
    sCount--;
}

/* The game's memory made the mirror's (the pages written since the last
 * look that differ), and the rest of the state put back. */
static int sync(const PortSnapRegion* r, int n) {
    int i;
    for (i = 0; i < n; i++) {
        uint8_t* mem = (uint8_t*)r[i].addr;
        const uint8_t* mir = sMirror[i].buf;
        const long v = visit(&r[i]), count = visitCount(&r[i], v);
        long k;
        if (r[i].size > sMirror[i].cap) {
            return 0;
        }
        for (k = 0; k < count; k++) {
            const size_t p = visitPage(&r[i], v, k);
            const size_t off = p * PAGE, len = pageLen(r[i].size, p);
            if (memcmp(mem + off, mir + off, len) != 0) {
                memcpy(mem + off, mir + off, len);
            }
        }
    }
    portSnapMetaPut(&sHead);
    portLuaSnapPut(sHead.lua, PORT_LUA_STATE_REWIND); /* the Lua mods' ttyd.keep, at the next frame */
    return 1;
}

static void step(void) {
    const Uint64 t0 = SDL_GetTicksNS();
    PortSnapRegion r[MAX_REGIONS];
    const char* why;
    int n, ok;
    if (!portSnapAllowed(1, &why) || portSnapDiscBusy()) {
        return; /* (the next frame) */
    }
    portSnapQuiesce(1);
    n = regionsNow(r, 0);
    ok = n == sRegions && portSnapHeapCommit(sHeapMax);
    if (ok && sActive++ > 0 && sCount > 0) {
        popRecord(r, n); /* the first step puts back the newest state itself */
    }
    ok = ok && sync(r, n);
    if (!ok) {
        clearAll();
    }
    portSnapResume(1);
    if (logging()) {
        printf("[rewind] retrace %u: step back, %d kept, %.2f ms\n", (unsigned)VIGetRetraceCount(), sCount,
               (double)(SDL_GetTicksNS() - t0) / 1e6);
        fflush(stdout);
    }
}

static int enabled(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("rewind", 0, 0, 1);
        sSeconds = portSettingsDeclare("rewind_seconds", 10, 5, 60);
    }
    return *sOn && !portOnlineActive() && !portReplayActive(); /* (a replay's input runs on: one game alone can't go back) */
}

void portRewindHold(int down) {
    sHeld = down;
}

void portRewindTest(int frames) {
    sTestUntil = VIGetRetraceCount() + (u32)frames;
}

void portRewindFrame(void) {
    const s32 seq = (s32)seqGetSeq();
    int held = sHeld || (s32)(sTestUntil - VIGetRetraceCount()) > 0;
    if (!enabled() || seq == SEQ_LOGO || seq == SEQ_TITLE || seq == SEQ_LOAD) {
        if (sRegions != 0) {
            clearAll(); /* off, online, or back at the title: another play */
        }
        held = 0;
    }
    if (held && sRegions != 0) {
        if (!sHolding) {
            sHolding = 1;
            salPcSetMuted(1);
            sSince = STEP;
        }
        if (++sSince >= STEP) {
            sSince = 0;
            step();
        }
        return;
    }
    if (sHolding) {
        sHolding = 0;
        sActive = 0;
        salPcSetMuted(0);
        sSince = 0; /* the next state EVERY frames on */
        if (logging()) {
            printf("[rewind] retrace %u: let go, %d kept\n", (unsigned)VIGetRetraceCount(), sCount);
            fflush(stdout);
        }
    }
    if (!enabled() || ++sSince < EVERY) {
        return;
    }
    {
        const char* why;
        if (!portSnapAllowed(0, &why) || portSnapDiscBusy()) {
            return; /* (a map change, the card, a read: the next frame it can) */
        }
    }
    sSince = 0;
    portSnapQuiesce(0);
    capture();
    portSnapResume(0);
}

int portRewindActive(void) {
    return sActive;
}

float portRewindSeconds(void) {
    return sRegions != 0 ? (float)(sCount * EVERY) / 60.0f : 0.0f;
}

int portRewindSupported(void) {
    return !portOnlineActive() && !portReplayActive();
}

void portRewindCosts(float* statesMiB, float* mirrorMiB, float* avgMs, float* maxMs) {
    size_t mirror = 0;
    int i;
    for (i = 0; i < sRegions; i++) {
        mirror += sMirror[i].cap;
    }
    *statesMiB = (float)sRingBytes / (1 << 20);
    *mirrorMiB = (float)mirror / (1 << 20);
    *avgMs = sCaptures > 0 ? (float)(sCaptureMs / sCaptures) : 0.0f;
    *maxMs = (float)sCaptureMax;
}

#else

void portRewindHold(int down) {
    (void)down;
}

void portRewindTest(int frames) {
    (void)frames;
}

void portRewindFrame(void) {}

int portRewindActive(void) {
    return 0;
}

float portRewindSeconds(void) {
    return 0.0f;
}

int portRewindSupported(void) {
    return 0;
}

void portRewindCosts(float* statesMiB, float* mirrorMiB, float* avgMs, float* maxMs) {
    *statesMiB = *mirrorMiB = *avgMs = *maxMs = 0.0f;
}

#endif
