/* Big-endian MusyX data loaded from the disc, converted in place for a
 * little-endian host (see port/include/port/musyx_swap_pc.h).
 *
 * Layouts are the GameCube ones (libs/musyx/include/musyx/synthdata.h,
 * seq.h, hardware.h, stream.h), addressed by explicit offsets so this file
 * doesn't depend on host struct layout. A bitmap of swapped 16-bit units
 * makes every field swap exactly once, so lists and patterns shared between
 * groups or tracks are safe. Reads before a swap use the big-endian value;
 * all offsets are checked against the buffer length. */
#include <dolphin/types.h>
#include <stdlib.h>
#include <string.h>
#include <musyx/synthdata.h>
#include "port/musyx_swap_pc.h"

typedef struct Swapper {
    u8* p;
    u32 len;
    u8* done; /* one bit per 2-byte unit */
} Swapper;

static BOOL swInit(Swapper* sw, void* data, u32 len) {
    sw->p = (u8*)data;
    sw->len = len;
    sw->done = (u8*)calloc(len / 16 + 1, 1);
    return sw->done != NULL;
}

static void swFree(Swapper* sw) {
    free(sw->done);
}

static BOOL inRange(const Swapper* sw, u32 off, u32 size) {
    return off <= sw->len && size <= sw->len - off;
}

static u16 rd16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

static u32 rd32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static BOOL isDone(const Swapper* sw, u32 off) {
    return (sw->done[off >> 4] >> ((off >> 1) & 7)) & 1;
}

static void markDone(Swapper* sw, u32 off) {
    sw->done[off >> 4] |= (u8)(1 << ((off >> 1) & 7));
}

/* Big-endian value at `off`, whether or not it has been swapped yet. */
static u16 get16(const Swapper* sw, u32 off) {
    u16 v;
    if (!inRange(sw, off, 2)) {
        return 0xFFFF;
    }
    if (isDone(sw, off)) {
        memcpy(&v, sw->p + off, 2);
        return v;
    }
    return rd16(sw->p + off);
}

static u32 get32(const Swapper* sw, u32 off) {
    u32 v;
    if (!inRange(sw, off, 4)) {
        return 0xFFFFFFFF;
    }
    if (isDone(sw, off)) {
        memcpy(&v, sw->p + off, 4);
        return v;
    }
    return rd32(sw->p + off);
}

static void sw16(Swapper* sw, u32 off) {
    u16 v;
    if ((off & 1) || !inRange(sw, off, 2) || isDone(sw, off)) {
        return;
    }
    v = rd16(sw->p + off);
    memcpy(sw->p + off, &v, 2);
    markDone(sw, off);
}

static void sw32(Swapper* sw, u32 off) {
    u32 v;
    if ((off & 1) || !inRange(sw, off, 4) || isDone(sw, off)) {
        return;
    }
    v = rd32(sw->p + off);
    memcpy(sw->p + off, &v, 4);
    markDone(sw, off);
    markDone(sw, off + 2);
}

/* ---- .proj ---- */

/* u16 ID list terminated by 0xFFFF (ranges are two entries). */
static void swapIdList(Swapper* sw, u32 off) {
    for (; inRange(sw, off, 2); off += 2) {
        u16 id = get16(sw, off);
        sw16(sw, off);
        if (id == 0xFFFF) {
            break;
        }
    }
}

/* PAGE[]: u16 macro, u8 prio, maxVoices, index, reserved; ends at index 0xFF. */
static void swapPages(Swapper* sw, u32 off) {
    for (; inRange(sw, off, 6); off += 6) {
        sw16(sw, off);
        if (sw->p[off + 4] == 0xFF) {
            break;
        }
    }
}

/* MIDISETUP[] (0x54 bytes): u16 songId, u16 reserved, 16 x 5 bytes. The table
 * is not always 0xFFFF-terminated (pmario.proj's runs into the next group's
 * header), so it also stops at 'end', the next group. */
static void swapMidiSetups(Swapper* sw, u32 off, u32 end) {
    for (; inRange(sw, off, 2) && off + 0x54 <= end; off += 0x54) {
        u16 id = get16(sw, off);
        sw16(sw, off);
        if (id == 0xFFFF) {
            break;
        }
        sw16(sw, off + 2);
    }
}

/* FX_DATA: u16 num, u16 reserved, FX_TAB[num] (u16 id, u16 macro, 6 x u8). */
static void swapFxData(Swapper* sw, u32 off) {
    u32 i, num = get16(sw, off);
    sw16(sw, off);
    sw16(sw, off + 2);
    for (i = 0; i < num && inRange(sw, off + 4 + i * 10, 10); i++) {
        sw16(sw, off + 4 + i * 10);
        sw16(sw, off + 4 + i * 10 + 2);
    }
}

void portMusyxSwapProj(void* data, u32 len) {
    Swapper sw;
    u32 off = 0;
    if (!swInit(&sw, data, len)) {
        return;
    }
    /* GROUP_DATA chain; nextOff is relative to the project start. */
    while (inRange(&sw, off, 8)) {
        u32 next = get32(&sw, off), i;
        u16 type;
        sw32(&sw, off);
        if (next == 0xFFFFFFFF || !inRange(&sw, off, 0x20)) {
            break;
        }
        sw16(&sw, off + 4);
        type = get16(&sw, off + 6);
        sw16(&sw, off + 6);
        for (i = 0; i < 5; i++) {
            sw32(&sw, off + 8 + i * 4);
        }
        swapIdList(&sw, get32(&sw, off + 0x08)); /* macros */
        swapIdList(&sw, get32(&sw, off + 0x0C)); /* samples */
        swapIdList(&sw, get32(&sw, off + 0x10)); /* curves */
        swapIdList(&sw, get32(&sw, off + 0x14)); /* keymaps */
        swapIdList(&sw, get32(&sw, off + 0x18)); /* layers */
        if (type == 1) {
            sw32(&sw, off + 0x1C);
            swapFxData(&sw, get32(&sw, off + 0x1C));
        } else {
            sw32(&sw, off + 0x1C);
            sw32(&sw, off + 0x20);
            sw32(&sw, off + 0x24);
            swapPages(&sw, get32(&sw, off + 0x1C));
            swapPages(&sw, get32(&sw, off + 0x20));
            swapMidiSetups(&sw, get32(&sw, off + 0x24), next == 0xFFFFFFFF ? len : next);
        }
        if (next <= off) {
            break; /* malformed: never loop */
        }
        off = next;
    }
    swFree(&sw);
}

/* ---- .pool ---- */

typedef enum PoolKind { POOL_MACRO, POOL_CURVE, POOL_KEYMAP, POOL_LAYER } PoolKind;

static void swapPoolChain(Swapper* sw, u32 off, PoolKind kind) {
    /* MEM_DATA chain: u32 nextOff (relative to this entry), u16 id, u16 reserved, data. */
    while (inRange(sw, off, 4)) {
        u32 next = get32(sw, off), end, p;
        sw32(sw, off);
        if (next == 0xFFFFFFFF || next < 8 || !inRange(sw, off, next)) {
            break;
        }
        sw16(sw, off + 4);
        sw16(sw, off + 6);
        end = off + next;
        switch (kind) {
        case POOL_MACRO: /* MSTEP: two u32 words each */
            for (p = off + 8; p + 4 <= end; p += 4) {
                sw32(sw, p);
            }
            break;
        case POOL_CURVE: /* u8 tables / little-endian ADSR: leave */
            break;
        case POOL_KEYMAP: /* KEYMAP: u16 id, s8, u8, s16 prioOffset, 2 bytes */
            for (p = off + 8; p + 8 <= end; p += 8) {
                sw16(sw, p);
                sw16(sw, p + 4);
            }
            break;
        case POOL_LAYER: { /* u32 num, LAYER[num]: u16 id, 4 x u8, s16 prioOffset, 4 bytes */
            u32 num = get32(sw, off + 8), i;
            sw32(sw, off + 8);
            for (i = 0, p = off + 12; i < num && p + 12 <= end; i++, p += 12) {
                sw16(sw, p);
                sw16(sw, p + 6);
            }
        } break;
        }
        off = end;
    }
}

void portMusyxSwapPool(void* data, u32 len) {
    Swapper sw;
    u32 i, offs[4];
    if (len < 16 || !swInit(&sw, data, len)) {
        return;
    }
    for (i = 0; i < 4; i++) {
        offs[i] = get32(&sw, i * 4);
        sw32(&sw, i * 4);
    }
    swapPoolChain(&sw, offs[0], POOL_MACRO);
    swapPoolChain(&sw, offs[1], POOL_CURVE);
    swapPoolChain(&sw, offs[2], POOL_KEYMAP);
    swapPoolChain(&sw, offs[3], POOL_LAYER);
    swFree(&sw);
}

/* ---- .sdir ---- */

#define SDIR_DISC_ENTRY 0x20

static u32 sdirCount(const u8* p, u32 len) {
    u32 n = 0;
    while ((n + 1) * SDIR_DISC_ENTRY <= len && rd16(p + n * SDIR_DISC_ENTRY) != 0xFFFF) {
        n++;
    }
    return n;
}

static u32 sdirTableSize(u32 n) {
    u32 size = (n + 1) * (u32)sizeof(SDIR_DATA);
    return (size + 31) & ~31u;
}

u32 portMusyxSdirNativeSize(const void* data, u32 len) {
    return sdirTableSize(sdirCount((const u8*)data, len)) + len;
}

void portMusyxSdirConvert(void* dst, const void* src, u32 len) {
    const u8* in = (const u8*)src;
    u32 n = sdirCount(in, len), table = sdirTableSize(n), i;
    SDIR_DATA* out = (SDIR_DATA*)dst;
    Swapper sw;

    memset(dst, 0, table);
    memcpy((u8*)dst + table, src, len);
    for (i = 0; i < n; i++) {
        const u8* e = in + i * SDIR_DISC_ENTRY;
        out[i].id = rd16(e + 0x00);
        out[i].ref_cnt = rd16(e + 0x02);
        out[i].offset = rd32(e + 0x04);
        out[i].addr = (void*)(uintptr_t)rd32(e + 0x08);
        out[i].header.info = rd32(e + 0x0C);
        out[i].header.length = rd32(e + 0x10);
        out[i].header.loopOffset = rd32(e + 0x14);
        out[i].header.loopLength = rd32(e + 0x18);
        out[i].extraData = rd32(e + 0x1C);
        if (out[i].extraData != 0) {
            out[i].extraData += table;
        }
    }
    out[n].id = 0xFFFF;

    /* ADPCM info records in the copy (offsets relative to the directory). */
    if (!swInit(&sw, (u8*)dst + table, len)) {
        return;
    }
    for (i = 0; i < n; i++) {
        u32 x = out[i].extraData, k, nblk;
        u8 comp = (u8)(out[i].header.length >> 24);
        if (x == 0) {
            continue;
        }
        x -= table;
        if (comp != 0 && comp != 1 && comp != 4 && comp != 5) {
            continue; /* PCM: no ADPCM record */
        }
        if (!inRange(&sw, x, 0x28)) {
            continue;
        }
        /* u16 numCoef, u8 initialPS, u8 loopPS, s16 loopY0, loopY1, s16 coefTab[16] */
        sw16(&sw, x);
        for (k = 4; k < 0x28; k += 2) {
            sw16(&sw, x + k);
        }
        if (comp == 1) {
            /* DSPADPCMplusInfo: one {s16 Y0, Y1; u8 PS, reserved} per 14-sample frame */
            nblk = ((out[i].header.length & 0xFFFFFF) + 13) / 14;
            for (k = 0; k < nblk && inRange(&sw, x + 0x28 + k * 6, 6); k++) {
                sw16(&sw, x + 0x28 + k * 6);
                sw16(&sw, x + 0x28 + k * 6 + 2);
            }
        }
    }
    swFree(&sw);
}

/* ---- songs (.slib) ---- */

static void swapArr(Swapper* sw) {
    u32 i, tTab, pTab, mTrack, numPatterns = 0;

    if (!inRange(sw, 0, 0x58)) {
        return;
    }
    tTab = get32(sw, 0x00);
    pTab = get32(sw, 0x04);
    mTrack = get32(sw, 0x0C);
    for (i = 0; i < 0x58; i += 4) { /* tTab, pTab, tmTab, mTrack, info, loopPoint[16], tsTab */
        sw32(sw, i);
    }

    /* 64 track offsets; each track is TENTRY[] (12 bytes) ending in pattern
     * 0xFFFF (stop) or 0xFFFE (loop: jump to the entry index at +10, read as
     * a u16). A looping track has no 0xFFFF after its 0xFFFE: the next bytes
     * are the next track or the pattern table, so the walk follows the jump
     * like GenerateNextTrackEvent does and stops once it goes backwards. */
    for (i = 0; i < 64 && inRange(sw, tTab + i * 4, 4); i++) {
        u32 t = get32(sw, tTab + i * 4), k = 0;
        sw32(sw, tTab + i * 4);
        if (t == 0) {
            continue;
        }
        while (inRange(sw, t + k * 12, 12)) {
            u32 e = t + k * 12;
            u16 pattern = get16(sw, e + 8);
            sw32(sw, e);     /* time */
            sw16(sw, e + 8); /* pattern */
            if (pattern == 0xFFFF) {
                break;
            }
            if (pattern == 0xFFFE) {
                u32 target = get16(sw, e + 10);
                sw16(sw, e + 10);
                if (target <= k) {
                    break;
                }
                k = target;
                continue;
            }
            if (pattern + 1u > numPatterns) {
                numPatterns = pattern + 1u;
            }
            k++;
        }
    }

    /* Patterns: u32 headerLen, pitchBend, modulation offsets, then NOTE_DATA
     * from offset 0xC: u16 time, u8 key, u8 velocity, u16 length. Controller
     * events (key & 0x80) and time-only padding (key|velocity == 0) are 4
     * bytes; key == velocity == 0xFF ends the pattern. Pitch bend and
     * modulation are byte streams. */
    for (i = 0; i < numPatterns && inRange(sw, pTab + i * 4, 4); i++) {
        u32 p = get32(sw, pTab + i * 4);
        sw32(sw, pTab + i * 4);
        if (p == 0 || !inRange(sw, p, 12)) {
            continue;
        }
        sw32(sw, p);
        sw32(sw, p + 4);
        sw32(sw, p + 8);
        for (p += 12; inRange(sw, p, 4);) {
            u8 key = sw->p[p + 2], vel = sw->p[p + 3];
            sw16(sw, p);
            if (key == 0xFF && vel == 0xFF) {
                break;
            }
            if ((key & 0x80) || (key | vel) == 0) {
                p += 4;
            } else {
                sw16(sw, p + 4);
                p += 6;
            }
        }
    }

    /* Tempo track: {u32 time, u32 bpm} until time == 0xFFFFFFFF. */
    if (mTrack != 0) {
        for (; inRange(sw, mTrack, 4); mTrack += 8) {
            u32 time = get32(sw, mTrack);
            sw32(sw, mTrack);
            if (time == 0xFFFFFFFF) {
                break;
            }
            sw32(sw, mTrack + 4);
        }
    }
    /* tmTab and tsTab are u8 tables. */
}

void portMusyxSwapArr(void* arr, u32 len) {
    Swapper sw;
    if (!swInit(&sw, arr, len)) {
        return;
    }
    swapArr(&sw);
    swFree(&sw);
}

void portMusyxSwapSlib(void* data, u32 len) {
    u8* p = (u8*)data;
    u32 off = 0;
    /* Chunks: u32 size (including the 0x20-byte header), song at +0x20;
     * a size of 0xFFFFFFFF ends the library. */
    while (off + 4 <= len) {
        u32 size = rd32(p + off);
        memcpy(p + off, &size, 4);
        if (size == 0xFFFFFFFF || size < 0x20 || size > len - off) {
            break;
        }
        portMusyxSwapArr(p + off + 0x20, size - 0x20);
        off += size;
    }
}

/* ---- .stm stream header ---- */

void portMusyxSwapStreamHeader(void* header) {
    u8* h = (u8*)header;
    u32 i;
    u16 v16;
    u32 v32;
    /* u8 type, u16 freq @2, u32 count..loopStart1 @4-0x1C, u16 hist @0x20-0x26 */
    v16 = rd16(h + 2);
    memcpy(h + 2, &v16, 2);
    for (i = 4; i < 0x20; i += 4) {
        v32 = rd32(h + i);
        memcpy(h + i, &v32, 4);
    }
    for (i = 0x20; i < 0x28; i += 2) {
        v16 = rd16(h + i);
        memcpy(h + i, &v16, 2);
    }
    if (h[0] == 2) {
        /* SND_ADPCMSTREAM_INFO (16 x s16) per channel */
        for (i = 0; i < 32; i += 2) {
            v16 = rd16(h + 0x5C + i);
            memcpy(h + 0x5C + i, &v16, 2);
            v16 = rd16(h + 0xBC + i);
            memcpy(h + 0xBC + i, &v16, 2);
        }
    }
}
