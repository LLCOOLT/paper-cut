/* High refresh rates, step 3 (docs/FRAME_INTERPOLATION.md): everything else
 * that moves, for interp_aurora.c's in-between frames. Regions of memory are
 * kept at each game frame and blended between the two frames, as the
 * cameras and the actors' positions are:
 * - the effects' own data: their particles (smoke, sparkles, stars, the
 *   weather, the battle's dust). There are seventy-odd layouts, so every
 *   4-byte word that reads as an ordinary float in both frames is blended;
 *   a pointer's two halves (8-aligned, its high half a small number once a
 *   tag's top byte is masked, or the whole pointing into MEM1; on a 32-bit
 *   host a word pointing into MEM1) are left alone, and so are ints (their
 *   bits read as tiny floats);
 * - the items (position, scale) and the map objects: blocks, switches,
 *   moving platforms (position, scale, rotation);
 * - the battle's units and each of their parts (position, offsets,
 *   rotations, scales);
 * - the HUD's slide in and out (statuswindow.c), and the windows' fades,
 *   zooms and turns (winmgr.c).
 * A value that changed by more than 150 in one frame (a jump, an angle
 * wrapping from 359 to 0) is drawn as it is now. Each region is blended only
 * while it is still the same thing (its identity bytes as they were: the
 * slot still in use, the same effect in it), and every region is put back
 * after the in-between frame, with whatever its draw wrote: the logic only
 * ever sees its own frames. */
#include <dolphin/os.h>
#include <dolphin/os/OSAlloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/effdrv.h"
#include "driver/itemdrv.h"
#include "driver/mobjdrv.h"
#include "manager/winmgr.h"
#include "mario/mariost.h"
#include "statuswindow.h"

#undef bool

enum { REGION_FLOATS, REGION_INTS };

typedef struct {
    u8* p;
    u32 size, at;          /* at: its frame-n bytes in sPrev */
    u8 kind;
    u8 identLen;
    u8 blended;
    const void* identAddr; /* still the same thing: these bytes as they were */
    u8 ident[16];
} Region;

#define REGION_MAX 1024
#define REGION_BYTES (2u << 20)
#define REGION_EFFECT_MAX 0x8000u /* an effect's data blended up to this size */
#define REGION_JUMP 150.0f        /* a value changing more in one frame: drawn as it is */

static Region sRegions[REGION_MAX];
static int sCount;
static u8* sPrev; /* frame n's bytes */
static u8* sLive; /* frame n+1's, kept while an in-between frame is drawn */
static u32 sUsed;

size_t portHeapOverflowSize(const void* p); /* heap_overflow_pc.c */
EffectWork* portEffWork(void);              /* effdrv.c */
ItemWork* portItemWork(void);               /* itemdrv.c */
MapObjectWork* portMobjWork(void);          /* mobjdrv.c */
StatusWindowWork* portStatusWinWork(void);  /* statuswindow.c */
WinMgrWork* portWinMgrWork(void);           /* winmgr.c */

static void add(void* p, u32 size, int kind, const void* identAddr, u32 identLen) {
    Region* r;
    if (p == NULL || size == 0 || sCount == REGION_MAX || sUsed + size > REGION_BYTES || identLen > 16) {
        return;
    }
    r = &sRegions[sCount++];
    r->p = (u8*)p;
    r->size = size;
    r->at = sUsed;
    r->kind = (u8)kind;
    r->blended = 0;
    r->identAddr = identAddr;
    r->identLen = (u8)identLen;
    if (identLen > 0) {
        memcpy(r->ident, identAddr, identLen);
    }
    memcpy(sPrev + sUsed, p, size);
    sUsed += (size + 7) & ~7u;
}

/* the size of a block from the game's heaps (an effect's data), 0 if unknown */
static u32 heapBlockSize(void* p) {
    size_t n = portHeapOverflowSize(p);
    if (n == 0) {
        n = OSReferentSize(p);
    }
    return (u32)n;
}

void portInterpRegionsSnapshot(void) {
    EffectWork* eff = portEffWork();
    ItemWork* items = portItemWork();
    MapObjectWork* mobjs = portMobjWork();
    StatusWindowWork* status = portStatusWinWork();
    WinMgrWork* windows = portWinMgrWork();
    s32 i;
    if (sPrev == NULL) {
        sPrev = malloc(REGION_BYTES);
        sLive = malloc(REGION_BYTES);
    }
    sCount = 0;
    sUsed = 0;
    if (sPrev == NULL || sLive == NULL || gp == NULL) {
        return;
    }
    if (eff != NULL && eff->entries != NULL) {
        for (i = 0; i < eff->count; i++) {
            EffectEntry* e = &eff->entries[i];
            u32 size;
            if (!(e->flags & EFFECT_FLAG_IN_USE) || e->userdata == NULL) {
                continue;
            }
            size = heapBlockSize(e->userdata);
            if (size > REGION_EFFECT_MAX) {
                size = REGION_EFFECT_MAX;
            }
            /* identity: the data and the effect's callback (the effect's kind) */
            add(e->userdata, size & ~3u, REGION_FLOATS, &e->userdata,
                (u32)((u8*)(&e->callback + 1) - (u8*)&e->userdata));
        }
    }
    if (items != NULL && items->entries != NULL) {
        for (i = 0; i < items->count; i++) {
            ItemEntry* it = &items->entries[i];
            if (it->flags & 1) {
                add(&it->position, sizeof(Vec) + sizeof(f32), REGION_FLOATS, &it->flags,
                    (u32)((u8*)(&it->itemId + 1) - (u8*)&it->flags));
            }
        }
    }
    if (mobjs != NULL && mobjs->entries != NULL) {
        for (i = 0; i < mobjs->count; i++) {
            MapObjectEntry* m = &mobjs->entries[i];
            if (m->flags & 1) {
                add(&m->position, 3 * sizeof(Vec), REGION_FLOATS, &m->flags, sizeof(m->flags));
            }
        }
    }
    if (gp->inBattle && _battleWorkPointer != NULL) {
        for (i = 0; i < BATTLE_UNIT_MAX; i++) {
            BattleUnit** slot = &_battleWorkPointer->units[i];
            BattleUnitPart* part;
            if (*slot == NULL) {
                continue;
            }
            add(&(*slot)->position, (u32)((u8*)(&(*slot)->baseScale + 1) - (u8*)&(*slot)->position), REGION_FLOATS,
                slot, sizeof(*slot));
            for (part = (*slot)->parts; part != NULL; part = part->nextPart) {
                add(&part->position, (u32)((u8*)(&part->baseScale + 1) - (u8*)&part->position), REGION_FLOATS,
                    slot, sizeof(*slot));
            }
        }
    }
    if (status != NULL) {
        add(&status->x, 2 * sizeof(f32), REGION_FLOATS, NULL, 0);
    }
    if (windows != NULL && windows->entries != NULL) {
        for (i = 0; i < windows->numEntries; i++) {
            WinMgrEntry* w = &windows->entries[i];
            if (w->flags & WINMGR_ENTRY_IN_USE) {
                add(&w->alpha, sizeof(s32), REGION_INTS, &w->flags, 2 * sizeof(s32));
                add(&w->scale, 2 * sizeof(f32), REGION_FLOATS, &w->flags, 2 * sizeof(s32));
            }
        }
    }
}

/* the regions can't be trusted (a save state, a map change) */
void portInterpRegionsForget(void) {
    sCount = 0;
    sUsed = 0;
}

/* an ordinary float: zero, or about 2^-31 to 2^31 in size (no small int read as a float, no NaN) */
static int plainFloat(u32 bits) {
    const u32 exp = (bits >> 23) & 0xFF;
    return (bits & 0x7FFFFFFF) == 0 || (exp >= 0x60 && exp <= 0x9E);
}

/* A pointer's high half on a 64-bit host: a small number (user space is under 2^48), under a top byte that is
 * a pointer tag's or none: 0, an MTE tag (0x01-0x0F) or Android's heap tag 0xB4 (arm64's top-byte-ignore).
 * Not any top byte: an ordinary float's bits 0xXX00YYYY (0.5-0.504, 2.0-2.016, 128-129...) would read as one,
 * and that pair would be left unblended on in-between frames. */
static int pointerHigh(u32 high) {
    const u32 top = high >> 24;
    const u32 h = high & 0x00FFFFFFu;
    return (top <= 0x0Fu || top == 0xB4u) && h != 0 && h <= 0xFFFFu;
}

/* p in the game's memory (MEM1): a pointer whatever its high half (a game heap mapped under 4 GiB, high half 0),
 * or on a 32-bit host */
static int gameMemory(uintptr_t p) {
    const uintptr_t lo = (uintptr_t)OSBaseAddress;
    return p >= lo && p - lo < (uintptr_t)OSGetPhysicalMemSize();
}

void portInterpRegionsBlend(f32 t) {
    int k;
    for (k = 0; k < sCount; k++) {
        Region* r = &sRegions[k];
        const u8* prev = sPrev + r->at;
        u32 i;
        r->blended = 0;
        if (r->identLen > 0 && memcmp(r->identAddr, r->ident, r->identLen) != 0) {
            continue; /* gone, or something else in its place */
        }
        r->blended = 1;
        memcpy(sLive + r->at, r->p, r->size);
        for (i = 0; i + 4 <= r->size; i += 4) {
            u32 a, b;
            memcpy(&a, prev + i, 4);
            memcpy(&b, r->p + i, 4);
            if (a == b) {
                continue;
            }
            if (r->kind == REGION_INTS) {
                const s32 x = (s32)a, y = (s32)b;
                if (x - y <= 255 && y - x <= 255) {
                    const s32 v = x + (s32)((f32)(y - x) * t + (y > x ? 0.5f : -0.5f));
                    memcpy(r->p + i, &v, 4);
                }
                continue;
            }
#if UINTPTR_MAX > 0xFFFFFFFFu
            if ((i & 7) == 0 && i + 8 <= r->size) {
                u32 ha, hb;
                memcpy(&ha, prev + i + 4, 4);
                memcpy(&hb, r->p + i + 4, 4);
                if (pointerHigh(ha) || pointerHigh(hb) || gameMemory(((uintptr_t)ha << 32) | a) ||
                    gameMemory(((uintptr_t)hb << 32) | b)) {
                    i += 4; /* a pointer (or NULL then one): both halves as they are */
                    continue;
                }
            }
#else
            if (gameMemory(a) || gameMemory(b)) {
                continue; /* a 32-bit host's pointer */
            }
#endif
            if (plainFloat(a) && plainFloat(b)) {
                f32 fa, fb, v;
                memcpy(&fa, &a, 4);
                memcpy(&fb, &b, 4);
                if (fa - fb <= REGION_JUMP && fb - fa <= REGION_JUMP) {
                    v = fa + (fb - fa) * t;
                    memcpy(r->p + i, &v, 4);
                }
            }
        }
    }
}

void portInterpRegionsRestore(void) {
    int k;
    for (k = 0; k < sCount; k++) {
        if (sRegions[k].blended) {
            memcpy(sRegions[k].p, sLive + sRegions[k].at, sRegions[k].size);
            sRegions[k].blended = 0;
        }
    }
}
