/* Save states (port/include/port/savestate.h): the whole game, as it is at a
 * frame boundary, kept in memory (compressed) and put back on load.
 *
 * What makes up the game:
 * - MEM1 (its heaps), ARAM (MusyX's samples, allocated once at start);
 * - the game's data and bss: its own sections, __ttyd_snapd/__ttyd_snapb
 *   (port_prefix.h), MusyX's and the port's decoders' state with them;
 *   each REL's sections, and which REL is linked;
 * - the save-state heap (port/src/common/snapheap_pc.c): what the decoders
 *   made, which the game's data points into;
 * - the game clock, its random numbers' state; the disc files open (their handles are Aurora's, made
 *   again on load: aurora_dvd_reopen_files).
 * Taken at the end of a frame (VIWaitForRetrace), on the main thread, in the
 * field or a battle, with no card access going on; the game is paused, the
 * audio callback held off and the disc reads and ARAM transfers finished
 * first. A load also drops Aurora's texture caches and the in-between frames'
 * last picture.
 *
 * States hold host addresses (the image, the heaps), so they are good only
 * for the run that made them. */
#include "port/savestate.h"
#include "port/lua_mods.h"
#include "port/online.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "port/battlex.h"
#include "port/postgame.h"
#include "port/versus.h"
#include <stdio.h>
#include <string.h>

static char sLabels[PORT_SAVE_STATE_SLOTS][32];

const char* portSaveStateLabel(int slot) {
    return slot >= 0 && slot < PORT_SAVE_STATE_SLOTS ? sLabels[slot] : "";
}

void portSaveStateSetLabel(int slot, const char* label) {
    if (slot >= 0 && slot < PORT_SAVE_STATE_SLOTS) {
        snprintf(sLabels[slot], sizeof(sLabels[slot]), "%s", label);
    }
}

#if defined(TTYD_SAVE_STATES)
#include <SDL3/SDL.h>
#include <aurora/dvd.h>
#if defined(__APPLE__)
#include <compression.h>
#endif
#include <dolphin/ar.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <stdlib.h>
#include <time.h>
#include "port/platform.h"
#include "port/rel_pc.h"
#include "port/snapheap.h"
#include "driver/seqdrv.h"
#include "manager/cardmgr.h"
#include "manager/dvdmgr.h"
#include "mario/mariost.h"

/* The game's data and bss (port_prefix.h, snapshot_bounds_pc.c) */
#if defined(__APPLE__)
extern char sSnapDataStart __asm("section$start$__DATA$__ttyd_snapd");
extern char sSnapDataEnd __asm("section$end$__DATA$__ttyd_snapd");
extern char sSnapBssStart __asm("section$start$__DATA$__ttyd_snapb");
extern char sSnapBssEnd __asm("section$end$__DATA$__ttyd_snapb");
#elif defined(_WIN32)
extern char portSnapDataStart, portSnapDataEnd, portSnapBssStart, portSnapBssEnd;
#define sSnapDataStart portSnapDataStart
#define sSnapDataEnd portSnapDataEnd
#define sSnapBssStart portSnapBssStart
#define sSnapBssEnd portSnapBssEnd
#else
extern char __start_ttyd_snapd[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_snapd[] __attribute__((visibility("hidden")));
extern char __start_ttyd_snapb[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_snapb[] __attribute__((visibility("hidden")));
#define sSnapDataStart (*__start_ttyd_snapd)
#define sSnapDataEnd (*__stop_ttyd_snapd)
#define sSnapBssStart (*__start_ttyd_snapb)
#define sSnapBssEnd (*__stop_ttyd_snapb)
#endif

const void* portRelCurrent(void);      /* rel_pc.c */
size_t portCompanionBlock(int which, char** start); /* snapshot_bounds_pc.c */
void portRelSetCurrent(const void* rel);
void portInterpInvalidate(void);       /* interp_aurora.c */

#define CHUNK (1u << 20)

typedef PortSnapRegion Region;

typedef struct Slot {
    int used;
    uint8_t* blob; /* per chunk: u32 raw size, u32 stored size (0: stored raw), bytes */
    size_t blobSize;
    size_t rawSize;
    PortSnapMeta meta;
    char info[96];
} Slot;

static Slot sSlots[PORT_SAVE_STATE_SLOTS + 1]; /* the player's, then the retry slot */
static int sQuiet; /* the retry slot's saves: no message on screen */
static int sRequestSlot = -1;
static int sRequestLoad;
static char sStatus[128];
static Uint64 sStatusTime;

#if defined(__APPLE__)
static uint8_t* sScratch; /* the compressor's work buffer */

/* Apple's LZ4: 0 if the chunk does not get smaller */
static size_t packChunk(uint8_t* dst, size_t cap, const uint8_t* src, size_t n) {
    if (sScratch == NULL) {
        sScratch = (uint8_t*)malloc(compression_encode_scratch_buffer_size(COMPRESSION_LZ4));
    }
    return compression_encode_buffer(dst, cap, src, n, sScratch, COMPRESSION_LZ4);
}

static void unpackChunk(uint8_t* dst, size_t n, const uint8_t* src, size_t packed) {
    compression_decode_buffer(dst, n, src, packed, NULL, COMPRESSION_LZ4);
}
#else
/* Elsewhere, a small LZ77 codec in LZ4's block style (game memory is mostly
 * zero runs and repeats): sequences of a token (literal count in its high
 * nibble, match length - 4 in its low one; 15 continues in bytes of up to
 * 255), the literals, then a 2-byte offset and the match. The last sequence
 * is literals only. Returns 0 if the chunk does not fit in cap. */
#define LZ_HASH_BITS 14

static int lzLength(uint8_t* dst, size_t cap, size_t* op, size_t len) {
    while (len >= 255) {
        if (*op >= cap) {
            return 0;
        }
        dst[(*op)++] = 255;
        len -= 255;
    }
    if (*op >= cap) {
        return 0;
    }
    dst[(*op)++] = (uint8_t)len;
    return 1;
}

/* a sequence: lit literals at src, then (if ml != 0) a match of ml at offset */
static int lzSequence(uint8_t* dst, size_t cap, size_t* op, const uint8_t* lits, size_t lit, size_t ml, size_t offset) {
    const size_t mcode = ml != 0 ? ml - 4 : 0;
    if (*op >= cap) {
        return 0;
    }
    dst[(*op)++] = (uint8_t)((lit < 15 ? lit : 15) << 4 | (mcode < 15 ? mcode : 15));
    if (lit >= 15 && !lzLength(dst, cap, op, lit - 15)) {
        return 0;
    }
    if (*op + lit > cap) {
        return 0;
    }
    memcpy(dst + *op, lits, lit);
    *op += lit;
    if (ml == 0) {
        return 1;
    }
    if (*op + 2 > cap) {
        return 0;
    }
    dst[(*op)++] = (uint8_t)offset;
    dst[(*op)++] = (uint8_t)(offset >> 8);
    return mcode < 15 || lzLength(dst, cap, op, mcode - 15);
}

static size_t packChunk(uint8_t* dst, size_t cap, const uint8_t* src, size_t n) {
    static uint32_t table[1u << LZ_HASH_BITS];
    size_t ip = 0, anchor = 0, op = 0;
    memset(table, 0xFF, sizeof(table));
    while (ip + 8 <= n) {
        uint32_t seq, h, ref;
        memcpy(&seq, src + ip, 4);
        h = (seq * 2654435761u) >> (32 - LZ_HASH_BITS);
        ref = table[h];
        table[h] = (uint32_t)ip;
        if (ref != 0xFFFFFFFFu && ip - ref <= 0xFFFF && memcmp(src + ref, src + ip, 4) == 0) {
            size_t ml = 4;
            while (ip + ml < n && src[ref + ml] == src[ip + ml]) {
                ml++;
            }
            if (!lzSequence(dst, cap, &op, src + anchor, ip - anchor, ml, ip - ref)) {
                return 0;
            }
            ip += ml;
            anchor = ip;
        } else {
            ip++;
        }
    }
    if (!lzSequence(dst, cap, &op, src + anchor, n - anchor, 0, 0)) {
        return 0;
    }
    return op;
}

static void unpackChunk(uint8_t* dst, size_t n, const uint8_t* src, size_t packed) {
    size_t ip = 0, op = 0;
    while (ip < packed) {
        const uint8_t t = src[ip++];
        size_t lit = t >> 4, ml = (size_t)(t & 15) + 4, offset;
        uint8_t b;
        if (lit == 15) {
            do {
                b = src[ip++];
                lit += b;
            } while (b == 255 && ip < packed);
        }
        if (op + lit > n || ip + lit > packed) {
            return; /* corrupt: stop */
        }
        memcpy(dst + op, src + ip, lit);
        op += lit;
        ip += lit;
        if (ip >= packed) {
            break; /* the last sequence */
        }
        offset = (size_t)src[ip] | (size_t)src[ip + 1] << 8;
        ip += 2;
        if ((t & 15) == 15) {
            do {
                b = src[ip++];
                ml += b;
            } while (b == 255 && ip < packed);
        }
        if (offset == 0 || offset > op || op + ml > n) {
            return;
        }
        for (; ml != 0; ml--, op++) { /* may overlap: byte by byte */
            dst[op] = dst[op - offset];
        }
    }
}
#endif

static void status(const char* text) {
    fprintf(stderr, "[savestate] %s\n", text);
    if (sQuiet) {
        return;
    }
    snprintf(sStatus, sizeof(sStatus), "%s", text);
    sStatusTime = SDL_GetTicks();
}

/* the regions in a fixed order; heapSize: the save-state heap's used part
 * to take (save: now; load: as saved) */
static int regions(Region* out, int max, size_t heapSize) {
    int n = 0, i;
    void* heap;
    size_t heapNow;
    out[n++] = (Region){&sSnapDataStart, (size_t)(&sSnapDataEnd - &sSnapDataStart)};
    out[n++] = (Region){&sSnapBssStart, (size_t)(&sSnapBssEnd - &sSnapBssStart)};
#if !defined(_WIN32)
    /* the second screen's menu's blocks, sections of their own here (on
     * Windows inside the ones above): port/companion_section.h */
    for (i = 0; i < 4; i++) {
        char* lo;
        const size_t size = portCompanionBlock(i, &lo);
        if (size > 0) {
            out[n++] = (Region){lo, size};
        }
    }
#endif
    for (i = 0; i < portRelStaticCount && n + 2 <= max - 3; i++) {
        const PortRelStatic* r = &portRelStaticTable[i];
        out[n++] = (Region){r->dataStart, (size_t)(r->dataEnd - r->dataStart)};
        out[n++] = (Region){r->bssStart, (size_t)(r->bssEnd - r->bssStart)};
    }
    out[n++] = (Region){(void*)OSBaseAddress, OSGetPhysicalMemSize()};
    out[n++] = (Region){ARGetStorageAddress(), ARGetSize()};
    portSnapHeapExtent(&heap, &heapNow);
    out[n++] = (Region){heap, heapSize};
    return n;
}

extern DVDEntry* dvdq; /* dvdmgr.c: its queue */

int portSnapDiscBusy(void) {
    int i;
    if (dvdq == NULL) {
        return 0;
    }
    for (i = 0; i < DVDEntryCount; i++) {
        if ((dvdq[i].status & DVDMGR_INUSE) && (dvdq[i].status & DVDMGR_READING) &&
            !(dvdq[i].status & DVDMGR_UNK_ERROR2)) {
            return 1;
        }
    }
    return 0;
}

int portSnapRegions(PortSnapRegion* out, int max, size_t heapSize) {
    return regions(out, max, heapSize);
}

size_t portSnapPack(uint8_t* dst, size_t cap, const uint8_t* src, size_t n) {
    return packChunk(dst, cap, src, n);
}

void portSnapUnpack(uint8_t* dst, size_t n, const uint8_t* src, size_t packed) {
    unpackChunk(dst, n, src, packed);
}

int portSnapMetaTake(PortSnapMeta* m) {
    void* heap;
    portSnapHeapExtent(&heap, &m->heapTop);
    m->gameTime = (OSTime)portGameClockPeek(); /* (not moving it: a snapshot is this game's alone) */
    m->fastClock = portFastClockGet();
    m->rand = portRandState();
    m->rel = portRelCurrent();
    m->fileCount = aurora_dvd_open_files(m->files, PORT_SNAP_MAX_FILES);
    m->lua = portLuaSnapTake(); /* the Lua mods' ttyd.keep (put back by the callers: load, rewind's sync) */
    return m->fileCount <= PORT_SNAP_MAX_FILES;
}

void portSnapMetaPut(const PortSnapMeta* m) {
    portSnapHeapRestored();
    portClockSetGame(m->gameTime);
    portFastClockSet(m->fastClock);
    portRandSetState(m->rand);
    portRelSetCurrent(m->rel);
    aurora_dvd_reopen_files(m->files, m->fileCount);
    AuroraFlushTextureCaches();
    portInterpInvalidate();
}

static int allowed(const char** why, int retryLoad) {
    const s32 seq = (s32)seqGetSeq();
    if (!portThreadIsMain()) {
        *why = "not on the main thread";
        return 0;
    }
    if (seq != SEQ_GAME && seq != SEQ_BATTLE && !(retryLoad && seq == SEQ_GAME_OVER)) {
        *why = "only in the field or a battle";
        return 0;
    }
    if (cardIsExec()) {
        *why = "not while the memory card is in use";
        return 0;
    }
    /* the modes whose own state lives outside the save-state sections
     * (port/src/aurora: the chapter's GSW to put back, the tower's floors,
     * the puzzle's and versus fight's pouches, the draft's choices): a state
     * taken or loaded meanwhile would leave theirs behind. Rewind alike. */
    if (portChapterActive()) {
        *why = "not while a chapter from chapter select is played";
        return 0;
    }
    if (portTowerActive()) {
        *why = "not in the Paper Tower";
        return 0;
    }
    if (portPitCoopActive()) {
        *why = "not during a Pit co-op run";
        return 0;
    }
    if (portPuzzleActive(NULL, NULL, NULL) >= 0) {
        *why = "not during a battle puzzle";
        return 0;
    }
    if (portVersusBusy()) {
        *why = "not during a versus fight";
        return 0;
    }
    if (portRemixDrafting()) {
        *why = "not while the badge draft is up";
        return 0;
    }
    return 1;
}

/* No other thread touches the game: paused, audio held, disc reads and
 * ARAM transfers (and their callbacks, which may queue more) finished.
 * Without pause (rewind's states, taken often): the clock and the audio
 * device run on, only the audio callback held off while the memory is read. */
void portSnapQuiesce(int pause) {
    int i;
    if (pause) {
        portPause();
    }
    salPcLock();
    for (i = 0; i < 1000; i++) {
        aurora_dvd_wait_idle();
        portARQPoll();
        if (portARQPendingCount() == 0) {
            aurora_dvd_wait_idle();
            break;
        }
        SDL_Delay(1);
    }
    if (pause) {
        GXDrawDone(); /* the GPU done reading what a load writes over */
    }
}

void portSnapResume(int pause) {
    salPcUnlock();
    if (pause) {
        portResume();
    }
}

int portSnapAllowed(int restore, const char** why) {
    return allowed(why, restore);
}

static void save(Slot* s) {
    Region r[512];
    void* heap;
    size_t heapSize, cap = 0, used = 0, raw = 0;
    int n, i;
    uint8_t* blob = NULL;
    const Uint64 t0 = SDL_GetTicks();
    portSnapHeapExtent(&heap, &heapSize);
    n = regions(r, 512, heapSize);
    for (i = 0; i < n; i++) {
        raw += r[i].size;
    }
    for (i = 0; i < n; i++) {
        size_t off;
        for (off = 0; off < r[i].size; off += CHUNK) {
            const size_t len = r[i].size - off < CHUNK ? r[i].size - off : CHUNK;
            size_t packed;
            uint32_t hdr[2];
            if (used + 8 + len > cap) {
                uint8_t* nb;
                cap = (used + 8 + len) * 2 > cap + (64u << 20) ? (used + 8 + len) * 2 : cap + (64u << 20);
                nb = (uint8_t*)realloc(blob, cap);
                if (nb == NULL) {
                    free(blob);
                    status("Not enough memory for a save state");
                    return;
                }
                blob = nb;
            }
            packed = packChunk(blob + used + 8, len, (const uint8_t*)r[i].addr + off, len);
            if (packed == 0 || packed >= len) {
                memcpy(blob + used + 8, (const uint8_t*)r[i].addr + off, len);
                packed = 0;
            }
            hdr[0] = (uint32_t)len;
            hdr[1] = (uint32_t)packed;
            memcpy(blob + used, hdr, 8);
            used += 8 + (packed != 0 ? packed : len);
        }
    }
    free(s->blob);
    s->blob = (uint8_t*)realloc(blob, used); /* the unused tail back */
    if (s->blob == NULL) {
        s->blob = blob;
    }
    s->blobSize = used;
    s->rawSize = raw;
    if (!portSnapMetaTake(&s->meta)) {
        s->used = 0;
        status("Too many open files for a save state");
        return;
    }
    portLuaSnapPin((int)(s - sSlots), s->meta.lua); /* kept while the slot holds it */
    {
        time_t now = time(NULL);
        struct tm tm;
        char when[16];
#if defined(_WIN32)
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        strftime(when, sizeof(when), "%H:%M:%S", &tm);
        snprintf(s->info, sizeof(s->info), "%s  %s%.16s", when, seqGetSeq() == SEQ_BATTLE ? "battle, " : "",
                 gp != NULL ? gp->currentMapName : "?");
    }
    s->used = 1;
    {
        char text[128];
        snprintf(text, sizeof(text), "Saved state %d (%zu MiB, %llu ms)", (int)(s - sSlots) + 1, used >> 20,
                 (unsigned long long)(SDL_GetTicks() - t0));
        status(text);
    }
}

static void load(Slot* s) {
    Region r[512];
    int n, i;
    const uint8_t* p = s->blob;
    const Uint64 t0 = SDL_GetTicks();
    if (!portSnapHeapCommit(s->meta.heapTop)) { /* before anything is written back */
        status("Not enough memory to load the state");
        return;
    }
    n = regions(r, 512, s->meta.heapTop);
    for (i = 0; i < n; i++) {
        size_t off;
        for (off = 0; off < r[i].size;) {
            uint32_t hdr[2];
            memcpy(hdr, p, 8);
            p += 8;
            if (hdr[1] == 0) {
                memcpy((uint8_t*)r[i].addr + off, p, hdr[0]);
                p += hdr[0];
            } else {
                unpackChunk((uint8_t*)r[i].addr + off, hdr[0], p, hdr[1]);
                p += hdr[1];
            }
            off += hdr[0];
        }
    }
    portSnapMetaPut(&s->meta);
    portLuaSnapPut(s->meta.lua, PORT_LUA_STATE_SLOT);
    {
        char text[128];
        snprintf(text, sizeof(text), "Loaded state %d (%llu ms)", (int)(s - sSlots) + 1,
                 (unsigned long long)(SDL_GetTicks() - t0));
        status(text);
    }
}

void portSaveStateRequest(int slot, int load) {
    if (slot < 0 || slot > PORT_SAVE_STATE_RETRY || portOnlineActive() || portReplayActive()) {
        return; /* online co-op, replays: one game alone can't jump (port/include/port/online.h) */
    }
    if (load && portHardcoreActive()) {
        status("Not on a hardcore file"); /* one life (port/include/port/playmodes.h) */
        return;
    }
    if (load && !sSlots[slot].used) {
        status("That slot is empty");
        return;
    }
    sRequestSlot = slot;
    sRequestLoad = load;
}

void portSaveStateFrame(void) {
    const char* why = "";
    char text[128];
    int slot;
    {
        /* $TTYD_STATE_TEST=<save frame>:<load frame> (tests): slot 1 saved,
         * then loaded, at those retrace counts */
        static int sTestSave = -2, sTestLoad = -1;
        u32 frame = VIGetRetraceCount();
        if (sTestSave == -2) {
            const char* v = getenv("TTYD_STATE_TEST");
            sTestSave = -1;
            if (v != NULL) {
                sscanf(v, "%d:%d", &sTestSave, &sTestLoad);
            }
        }
        if ((int)frame == sTestSave) {
            portSaveStateRequest(0, 0);
        } else if ((int)frame == sTestLoad) {
            portSaveStateRequest(0, 1);
        }
    }
    slot = sRequestSlot;
    if (slot < 0) {
        return;
    }
    {
        /* the game's disc thread mid-read (a room's files coming in): its
         * place in the read isn't in memory, so wait for it, up to 5 s */
        static int sWaited;
        if (portSnapDiscBusy() && sWaited++ < 300) {
            return;
        }
        sWaited = 0;
    }
    sRequestSlot = -1;
    sQuiet = slot == PORT_SAVE_STATE_RETRY && !sRequestLoad;
    if (!allowed(&why, slot == PORT_SAVE_STATE_RETRY && sRequestLoad)) {
        snprintf(text, sizeof(text), "Save states: %s", why);
        status(text);
        sQuiet = 0;
        return;
    }
    portSnapQuiesce(1);
    if (sRequestLoad) {
        load(&sSlots[slot]);
    } else {
        save(&sSlots[slot]);
    }
    portSnapResume(1);
    sQuiet = 0;
}

int portSaveStateHas(int slot) {
    return slot >= 0 && slot <= PORT_SAVE_STATE_RETRY && sSlots[slot].used;
}

void portSaveStateNotice(const char* text) {
    snprintf(sStatus, sizeof(sStatus), "%s", text);
    sStatusTime = SDL_GetTicks();
}

int portSaveStateSupported(void) {
    return !portOnlineActive() && !portReplayActive();
}

const char* portSaveStateInfo(int slot) {
    if (slot < 0 || slot >= PORT_SAVE_STATE_SLOTS || !sSlots[slot].used) {
        return NULL;
    }
    return sSlots[slot].info;
}

const char* portSaveStateStatus(void) {
    return sStatus[0] != '\0' && SDL_GetTicks() - sStatusTime < 3000 ? sStatus : NULL;
}

#else

void portSaveStateRequest(int slot, int load) {
    (void)slot;
    (void)load;
}

void portSaveStateFrame(void) {}

int portSaveStateSupported(void) {
    return 0;
}

const char* portSaveStateInfo(int slot) {
    (void)slot;
    return NULL;
}

const char* portSaveStateStatus(void) {
    return NULL;
}

int portSaveStateHas(int slot) {
    (void)slot;
    return 0;
}

void portSaveStateNotice(const char* text) {
    (void)text;
}

#endif
