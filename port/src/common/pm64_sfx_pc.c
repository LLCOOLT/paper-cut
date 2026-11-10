/* Paper Mario 64's sound effects (port/include/port/pm64_sfx.h): a player of our own for the N64 game's
 * sound effect scripts in the linked ROM's cache (portPm64Segment(PM64_SEG_AUDIO): the SBN archive), and the
 * table that says which of the game's sounds become which of PM64's. What it reads, as the PM64 decomp
 * (pmret/papermario) documents the formats:
 *  - INIT (at SBN 0x24): the bank list (file, bank, bank set) loaded at start, and the extra list whose first
 *    file is the SEF.
 *  - SEF: "SEF ", at 0x0E whether there's an extra section, at 0x10 eight section offsets, at 0x20 the extra
 *    section's. A sound id's low byte - 1 picks an entry of its section (bits 8-9): for entries 0..0xBF a
 *    (script offset, info) pair (info: the player 0-7, polyphony in bits 5-6, priority in bits 8-9); from 0xC0,
 *    and in the extra section (ids 0x2000 | n), a 4-byte script of its own.
 *  - A script: a flags byte (mode: 0 basic, 8 bytes; 1 a sequence of commands; 2 compact, 4 bytes), then the
 *    mode's bytes. The sequence's commands: delays (0x01..0x77; 0x78.. with a second byte), notes (0x80..0xD7:
 *    pitch, then velocity and length), 0xE0..0xF8 (volume, pan, instrument, reverb, envelope, tune, wait,
 *    sweep, loops, ramps, jumps, random pitch and velocity, another sound).
 *  - BK: 16 instrument offsets; an instrument: VADPCM (or 16-bit) samples, loop, codebook, key base, sample
 *    rate, envelope presets.
 * Timing as the N64 engine's: an audio frame of 184 samples at 32 kHz; the effects' scripts step at 312500 /
 * 434782 of the audio frames (about every 8 ms). Paper Cut's own code under its MIT licence: nothing of the
 * ROM is in it. The voices, envelopes and synthesis are those of the soundtrack player (pm64_music_pc.c). */
#include "port/file_pc.h"
#include "port/pm64_sfx.h"
#include "port/audio_mix.h"
#include "port/enemy_variant.h"
#include "port/pm64.h"
#include "port/pm64_sound_ids.h"
#include "port/settings.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "party.h"

#define RATE 32000
#define FRAME 184
#define FRAME_USEC 5750
#define PLAYERS 16
#define SLOTS 40
#define MAX_INSTS 256
#define STEP 312500
#define INTERVAL 434782
#define SILENT_ID 0xFFFF /* the table's "no sound": the game's is silenced and nothing plays */

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

static u32 rd32(const u8* p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u16 rd16(const u8* p) {
    return (u16)(p[0] << 8 | p[1]);
}

/* ---- the ROM's audio: the SBN, its SEF, the banks loaded at start, the instruments decoded ---- */

typedef struct Inst {
    const u8* src; /* its record in the SBN (the cache key) */
    s16* pcm;
    s32 length;
    s32 loopStart, loopEnd, loopCount;
    s32 keyBase;
    float pitchRatio;
    const u8* env;
} Inst;

static struct {
    int ready;           /* 1 loaded, -1 tried and failed (until the link changes) */
    unsigned generation; /* portPm64Generation() it was loaded at */
    const u8* sbn;
    size_t sbnSize;
    const u8* sef;
    u32 sefSize;
    const u8* bank[8][16];
    Inst inst[MAX_INSTS];
    int instUsed;
} sRom;

/* (as offsets, never p + n: a damaged cache's n or offset can't wrap the test) */
static int within(const u8* base, size_t size, const u8* p, size_t n) {
    const uintptr_t at = (uintptr_t)p - (uintptr_t)base;
    return base != NULL && (uintptr_t)p >= (uintptr_t)base && at <= size && n <= size - at;
}

static int inRange(const u8* p, size_t n) {
    return within(sRom.sbn, sRom.sbnSize, p, n);
}

static int inSef(const u8* p, size_t n) {
    return within(sRom.sef, sRom.sefSize, p, n);
}

static const u8* sbnFile(u32 index, int format, u32* size) {
    const u8* t;
    u32 count, off, data;
    if (sRom.sbnSize < 0x40 || memcmp(sRom.sbn, "SBN ", 4) != 0) {
        return NULL;
    }
    count = rd32(sRom.sbn + 0x14);
    t = sRom.sbn + rd32(sRom.sbn + 0x10);
    if (index >= count || !inRange(t + index * 8, 8)) {
        return NULL;
    }
    off = rd32(t + index * 8) & 0xFFFFFF;
    data = rd32(t + index * 8 + 4);
    if ((int)(data >> 24) != format || off == 0 || !inRange(sRom.sbn + off, data & 0xFFFFFF)) {
        return NULL;
    }
    if (size != NULL) {
        *size = data & 0xFFFFFF;
    }
    return sRom.sbn + off;
}

/* VADPCM: 9-byte frames of 16 samples, an order-2 codebook (as pm64_music_pc.c) */
static void vadpcmDecode(const u8* in, s32 frames, const u8* book, int npred, s16* out) {
    s32 f, i, j, h;
    s16 last1 = 0, last2 = 0;
    for (f = 0; f < frames; f++) {
        const u8* fr = in + f * 9;
        const int scale = 1 << (fr[0] >> 4);
        int pred = fr[0] & 15;
        s32 ix[16];
        s16 c[2][8];
        if (pred >= npred) {
            pred = 0;
        }
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 8; j++) {
                c[i][j] = (s16)rd16(book + (pred * 16 + i * 8 + j) * 2);
            }
        }
        for (i = 0; i < 16; i++) {
            int n = (i & 1) ? (fr[1 + i / 2] & 15) : (fr[1 + i / 2] >> 4);
            if (n >= 8) {
                n -= 16;
            }
            ix[i] = n * scale;
        }
        for (h = 0; h < 2; h++) {
            s16* o = out + f * 16 + h * 8;
            const s32* x = ix + h * 8;
            for (i = 0; i < 8; i++) {
                s32 acc = c[0][i] * last2 + c[1][i] * last1 + (x[i] << 11);
                for (j = 0; j < i; j++) {
                    acc += c[1][i - j - 1] * x[j];
                }
                acc >>= 11;
                o[i] = (s16)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
            }
            last2 = o[6];
            last1 = o[7];
        }
    }
}

static Inst* instOf(const u8* bk, int ins) {
    const u8* r;
    u32 off, wav, len, book, env;
    int i, type, codebookSize;
    Inst* in;
    if (bk == NULL || ins < 0 || ins > 15 || !inRange(bk, 0x40)) {
        return NULL;
    }
    off = rd16(bk + 0x12 + ins * 2);
    if (off == 0 || !inRange(bk + off, 0x30)) {
        return NULL;
    }
    r = bk + off;
    for (i = 0; i < sRom.instUsed; i++) {
        if (sRom.inst[i].src == r) {
            return sRom.inst[i].pcm != NULL ? &sRom.inst[i] : NULL;
        }
    }
    if (sRom.instUsed >= MAX_INSTS) {
        return NULL;
    }
    in = &sRom.inst[sRom.instUsed++];
    memset(in, 0, sizeof(*in));
    in->src = r;
    wav = rd32(r);
    len = rd32(r + 4);
    in->loopStart = (s32)rd32(r + 0x0C);
    in->loopEnd = (s32)rd32(r + 0x10);
    in->loopCount = (s32)rd32(r + 0x14);
    book = rd32(r + 0x18);
    codebookSize = rd16(r + 0x1C);
    in->keyBase = rd16(r + 0x1E);
    in->pitchRatio = (float)(s32)rd32(r + 0x20) / (float)RATE;
    type = r[0x24];
    env = rd32(r + 0x2C);
    in->env = env != 0 && inRange(bk + env, 8) ? bk + env : NULL;
    if (len == 0 || !inRange(bk + wav, len)) {
        return NULL;
    }
    if (type == 0) {
        const s32 frames = (s32)(len / 9);
        if (book == 0 || codebookSize < 32 || !inRange(bk + book, (size_t)codebookSize)) {
            return NULL;
        }
        in->length = frames * 16;
        if ((in->pcm = (s16*)malloc((size_t)in->length * sizeof(s16) + 2)) == NULL) {
            return NULL;
        }
        vadpcmDecode(bk + wav, frames, bk + book, codebookSize / 32, in->pcm);
    } else {
        s32 k;
        in->length = (s32)(len / 2);
        if ((in->pcm = (s16*)malloc((size_t)in->length * sizeof(s16) + 2)) == NULL) {
            return NULL;
        }
        for (k = 0; k < in->length; k++) {
            in->pcm[k] = (s16)rd16(bk + wav + k * 2);
        }
    }
    if (in->loopEnd > in->length) {
        in->loopEnd = in->length;
    }
    if (in->loopStart < 0 || (in->loopEnd != 0 && in->loopStart >= in->loopEnd)) {
        in->loopEnd = 0; /* (a damaged cache's loop: plays once rather than reading before the samples) */
    }
    return in;
}

static const u8 kPressDefault[] = {61, 127, 0xFF, 0};
static const u8 kReleaseDefault[] = {52, 0, 0xFF, 0};

/* the engine's instrument lookup: bank byte (bits 4-6 the bank set, bits 0-1 the envelope), patch (bank << 4
 * | instrument) */
static Inst* instrument(int bank, int patch, const u8** press, const u8** release) {
    const int set = (bank & 0x70) >> 4, envIdx = bank & 3;
    Inst* in = instOf(sRom.bank[set][(patch >> 4) & 15], patch & 15);
    *press = kPressDefault;
    *release = kReleaseDefault;
    if (in != NULL && in->env != NULL && envIdx < in->env[0] && inRange(in->env + 4 + envIdx * 4, 4)) {
        const u8* p = in->env + rd16(in->env + 4 + envIdx * 4);
        const u8* q = in->env + rd16(in->env + 6 + envIdx * 4);
        if (inRange(p, 2) && inRange(q, 2)) {
            *press = p;
            *release = q;
        }
    }
    return in;
}

/* the INIT's banks and the SEF; 1 when they're there and held (romReady publishes them). The game thread,
 * outside the lock: the first call reads the whole segment from disk (several MB), and the audio thread
 * doesn't look at sRom until ready is 1 */
static int romLoad(void) {
    size_t size;
    const u8* init;
    u32 io, bankList, bankSize, extra, i;
    sRom.generation = portPm64Generation();
    sRom.ready = -1;
    if ((sRom.sbn = portPm64Segment(PM64_SEG_AUDIO, &size)) == NULL || size < 0x40) {
        sRom.sbn = NULL;
        return 0;
    }
    sRom.sbnSize = size;
    if ((io = rd32(sRom.sbn + 0x24)) == 0 || !inRange(sRom.sbn + io, 0x20)) {
        return 0;
    }
    init = sRom.sbn + io;
    bankList = rd16(init + 8);
    bankSize = rd16(init + 0x0A);
    extra = rd16(init + 0x10);
    for (i = 0; i + 4 <= bankSize && i < 80 * 4; i += 4) {
        const u8* e = init + bankList + i;
        const u8* bk;
        int set;
        if (!inRange(e, 4) || rd16(e) == 0xFFFF) {
            break;
        }
        /* the INIT list's bank sets (1 aux, 2, 3 music, 4..6) to the patches' set indices */
        set = e[3] == 1 ? 0 : e[3] == 2 ? 1 : e[3] >= 3 && e[3] <= 6 ? e[3] : -1;
        if (set >= 0 && e[2] < 16 && (bk = sbnFile(rd16(e), 0x30, NULL)) != NULL) {
            sRom.bank[set][e[2]] = bk;
            if (set == 0) {
                sRom.bank[7][e[2]] = bk;
            }
        }
    }
    if (!inRange(init + extra, 2) || (sRom.sef = sbnFile(rd16(init + extra), 0x20, &sRom.sefSize)) == NULL ||
        sRom.sefSize < 0x22 || memcmp(sRom.sef, "SEF ", 4) != 0) {
        sRom.sef = NULL;
        return 0;
    }
    /* (the segment stays while we read it, an unlink or not) */
    return portPm64AudioHold(sRom.sbn, 1);
}

/* ---- the voices (the soundtrack player's, one per script player) ---- */

static s32 envTime(int i) {
    static const float kSec[80] = {
        60, 55, 50, 45, 40, 35, 30, 27.5f, 25, 22.5f, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
        10, 9, 8, 7, 6, 5, 4.5f, 4, 3.5f, 3, 2.75f, 2.5f, 2.25f, 2, 1.9f, 1.8f, 1.7f, 1.6f, 1.5f, 1.4f,
        1.3f, 1.2f, 1.1f, 1, 0.95f, 0.9f, 0.85f, 0.8f, 0.75f, 0.7f, 0.65f, 0.6f, 0.55f, 0.5f, 0.45f, 0.4f,
        0.375f, 0.35f, 0.325f, 0.3f, 0.29f, 0.28f, 0.27f, 0.26f, 0.25f, 0.24f, 0.23f, 0.22f, 0.21f, 0.2f,
        0.19f, 0.18f, 0.17f, 0.16f, 0.15f, 0.14f, 0.13f, 0.12f, 0.11f, 0.1f};
    static const u8 kFrames[14] = {16, 14, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    if (i < 80) {
        return (s32)(kSec[i] * 1000000.0f + 0.5f);
    }
    if (i < 94) {
        return kFrames[i - 80] * FRAME_USEC;
    }
    return 0;
}

/* The script engine's own two envelopes (SetEnvelope): time index, volume pairs, 0xFF ends; press then
 * release. Fast: 290 ms up, held 900 ms, 1 s down to 95, 3 s to silence; released over 1.4 s. Slow: 290 ms
 * up, 1.8 s held, then halving every 1.1 s; released over 450 ms. */
static const u8 kEnvFastPress[] = {60, 127, 45, 127, 43, 95, 29, 0, 0xFF, 0};
static const u8 kEnvFastRelease[] = {39, 0, 0xFF, 0};
static const u8 kEnvSlowPress[] = {60, 127, 35, 127, 60, 63, 42, 31, 42, 15, 42, 7, 42, 3, 54, 0, 0xFF, 0};
static const u8 kEnvSlowRelease[] = {54, 0, 0xFF, 0};

#define VOLMUL(env, client, rel, scale) ((s32)(((long long)(env) * (client) * (rel) >> 14) * (scale) >> 7))

typedef struct Voice {
    int owned; /* the player started it and it hasn't ended */
    Inst* inst;
    float pitch;
    s32 volume;
    u8 pan, reverb;
    s32 delta;
    const u8* press;
    const u8* release;
    const u8* cmd;
    const u8* cmdEnd; /* the SBN's end when cmd is in it (NULL: one of the built-in scripts, terminated) */
    const u8* loopAt;
    u8 loopCount;
    s32 envDuration, envTimeLeft;
    float envDelta;
    s32 envScale;
    s32 envInitial, envTarget;
    int flags; /* 1 releasing, 2 handled volume change, 0x10 key released, 0x20 volume changed */
    s32 envRelStart;
    s32 clientVolume;
    int donePending;
    int sync; /* 1 start, 4 params */
    int playing;
    double pos;
    s32 loopsLeft;
    float gainL, gainR;
    float toL, toR;
    s32 rampLeft;
} Voice;

/* an envelope script's next byte; past the SBN's end (a damaged cache) it reads as the end, 0xFF */
static u8 envPeek(const Voice* v) {
    return v->cmdEnd != NULL && v->cmd >= v->cmdEnd ? 0xFF : *v->cmd;
}

static u8 envByte(Voice* v) {
    const u8 b = envPeek(v);
    if (v->cmdEnd == NULL || v->cmd < v->cmdEnd) {
        v->cmd++;
    }
    return b;
}

static void envAt(Voice* v, const u8* script) {
    v->cmd = script;
    v->cmdEnd = sRom.sbn != NULL && script >= sRom.sbn && script < sRom.sbn + sRom.sbnSize ? sRom.sbn + sRom.sbnSize
                                                                                          : NULL;
}

static int envStep(Voice* v) {
    int guard = 0;
    while (guard++ < 64) {
        const u8 op = envByte(v);
        if (op < 0x80) {
            return op;
        }
        switch (op) {
        case 0xFE: {
            const u8 b = envByte(v);
            v->envScale = b > 0x80 ? 0x80 : b;
            break;
        }
        case 0xFD:
            v->envScale += (s8)envByte(v);
            v->envScale = v->envScale > 0x80 ? 0x80 : v->envScale < 0 ? 0 : v->envScale;
            break;
        case 0xFC:
            v->loopCount = envByte(v);
            v->loopAt = v->cmd;
            break;
        case 0xFB:
            envByte(v);
            if (v->loopAt != NULL && (v->loopCount == 0 || --v->loopCount != 0)) {
                v->cmd = v->loopAt;
            }
            break;
        default:
            envByte(v);
            break;
        }
    }
    return 0x5F;
}

/* the voice's gains from its volume and pan, with the slot's (0..127) and the slot's pan offset */
static void setMix(Voice* v, int slotVolume, int slotPan) {
    const float vol = (float)v->volume * (float)v->volume / 32768.0f / 32768.0f;
    const float sv = (float)slotVolume / 127.0f;
    int pan = (int)v->pan + (slotPan - 64);
    float a;
    pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
    a = (float)pan / 127.0f * 1.5707963f;
    v->toL = vol * sv * sv * cosf(a);
    v->toR = vol * sv * sv * sinf(a);
    v->rampLeft = v->delta > 0 ? v->delta : FRAME;
}

static void voiceStart(Voice* v, int slotVolume, int slotPan) {
    int idx;
    envAt(v, v->press);
    v->envScale = 0x80;
    v->loopAt = NULL;
    idx = envStep(v);
    v->flags = 0;
    v->envInitial = 0;
    v->envTarget = envByte(v) & 0x7F;
    v->envDuration = v->envTimeLeft = envTime(idx);
    v->volume = VOLMUL(v->envTarget, v->clientVolume, 0x80, v->envScale);
    v->delta = (v->envDuration / FRAME_USEC) * FRAME;
    v->envDelta = v->envDuration != 0 ? (float)(v->envTarget - v->envInitial) / (float)v->envDuration : 0.0f;
    v->envRelStart = 0x80;
    v->playing = v->inst != NULL && v->inst->pcm != NULL;
    v->pos = 0.0;
    v->loopsLeft = v->inst != NULL ? v->inst->loopCount : 0;
    v->gainL = v->gainR = 0.0f;
    setMix(v, slotVolume, slotPan);
}

static void envChange(Voice* v, s32 current) {
    v->volume = VOLMUL(current, v->clientVolume, v->envRelStart, v->envScale);
    v->sync |= 4;
}

static void envNext(Voice* v) {
    s32 current;
    if (v->flags & 0x20) {
        v->flags &= ~0x20;
        if (v->envTimeLeft > FRAME_USEC) {
            v->envTimeLeft -= FRAME_USEC;
            v->flags |= 2;
            current = v->envInitial + (s32)(v->envDelta * (float)(v->envDuration - v->envTimeLeft));
        } else {
            current = v->envTarget;
        }
        v->delta = FRAME;
    } else {
        v->delta = (v->envDuration / FRAME_USEC) * FRAME;
        current = v->envTarget;
    }
    envChange(v, current);
}

static void voiceEnvelope(Voice* v) {
    if (v->cmd == NULL) {
        return;
    }
    if (v->flags & 2) {
        v->volume = VOLMUL(v->envTarget, v->clientVolume, v->envRelStart, v->envScale);
        v->delta = (v->envTimeLeft / FRAME_USEC) * FRAME;
        v->flags &= ~2;
        v->sync |= 4;
        return;
    }
    if (v->flags & 0x10) {
        u8 tv;
        v->flags = (v->flags & ~0x10) | 1;
        envAt(v, v->release);
        if (v->envTimeLeft > FRAME_USEC) {
            v->envInitial += (s32)(v->envDelta * (float)(v->envDuration - v->envTimeLeft));
        } else {
            v->envInitial = v->envTarget;
        }
        v->envDuration = v->envTimeLeft = envTime(envByte(v));
        tv = envByte(v);
        if (tv & 0x80) {
            tv &= 0x7F;
            v->envRelStart = v->envInitial;
        }
        v->envTarget = tv;
        envNext(v);
        return;
    }
    if (v->envTimeLeft == -1) {
        if (v->flags & 0x20) {
            v->flags &= ~0x20;
            envChange(v, v->envInitial);
        }
        return;
    }
    v->envTimeLeft -= FRAME_USEC;
    if (v->envTimeLeft <= 0) {
        if (envPeek(v) == 0xFF) {
            if (v->flags & 1) {
                v->flags = 0;
                v->cmd = NULL;
                v->donePending = 1;
            } else {
                v->envTimeLeft = v->envDuration = -1;
                v->delta = FRAME;
                v->envDelta = 0.0f;
                v->envInitial = v->envTarget;
            }
        } else {
            const int idx = envStep(v);
            v->envInitial = v->envTarget;
            v->envTarget = envByte(v) & 0x7F;
            v->envDuration = v->envTimeLeft = envTime(idx);
            v->envDelta = v->envDuration != 0 ? (float)(v->envTarget - v->envInitial) / (float)v->envDuration : 0.0f;
            envNext(v);
        }
    } else if (v->flags & 0x20) {
        v->flags &= ~0x20;
        if (v->envTimeLeft > FRAME_USEC) {
            v->envTimeLeft -= FRAME_USEC;
            v->flags |= 2;
            envChange(v, v->envInitial + (s32)(v->envDelta * (float)(v->envDuration - v->envTimeLeft)));
        } else {
            envChange(v, v->envTarget);
        }
        v->delta = FRAME;
    }
}

/* the voice stops at once (the engine's reset: a short fade over the frame) */
static void voiceReset(Voice* v) {
    v->cmd = NULL;
    v->donePending = 1;
    v->sync = 0;
    v->toL = v->toR = 0.0f;
    v->rampLeft = FRAME;
}

/* ---- the script players ---- */

typedef struct Player {
    const u8* pos;   /* NULL: free */
    const u8* start; /* where Restart goes back to */
    const u8* loopStart;
    int loopCount;
    int mode;   /* 0 basic, 1 sequence */
    u8 flags;   /* the script's flags byte */
    int state;  /* 0 init, 1 continue */
    u16 soundId;
    int priority;
    int owner;  /* the game's effect slot, or -1 */
    unsigned serial;
    int orphan; /* audio frames since its slot ended (0: still live) */
    int cap;    /* how many it may go on for then */
    int fixedVol; /* without a slot: its volume */
    Inst* inst;
    const u8* press;
    const u8* release;
    const u8* customPress;
    s32 sfxVolume;
    s32 delay, playLength;
    s32 coarse, fine;
    u8 pan, reverb, velocity;
    s32 volCur, volStep, volTime; /* 16.16 of the 0..0x7FFF volume */
    s32 volGoal;
    s32 tuneCur, tuneStep, tuneTime, tuneGoal; /* 16.16 cents */
    int randomPitch, randomVelocity;
    int changedVol, changedTune, changedPan;
    u8 oneShot[8];
    Voice v;
} Player;

static struct {
    int vol, pan;      /* the game's for the slot, each frame */
    unsigned serial;   /* the sound armed (serial) */
    u16 sound;         /* its PM64 sound, 0 none */
    int live;          /* the slot was seen this frame */
    int started;       /* its PM64 sound was started (once a slot's sound) */
    int maxFrames;     /* the PM64 sound's longest life after its slot ended (audio frames) */
} sSlot[SLOTS];

static Player sPlayer[PLAYERS];
static s32 sCounter = INTERVAL;
static u32 sRandom, sFrameCounter;
/* a spin lock (as pm64_pc.c's audio lock): the game thread holds it for a table entry's start, the audio thread
 * for one mix call */
static char sLock;
#define LOCK() while (__atomic_test_and_set(&sLock, __ATOMIC_ACQUIRE))
#define UNLOCK() __atomic_clear(&sLock, __ATOMIC_RELEASE)

static float pitchOf(s32 cents) {
    return powf(2.0f, (float)cents / 1200.0f);
}

static s32 randomPitch(s32 seed, s32 amplitude, s32 pitch) {
    const s32 random = ((seed >> 7) & 0xF) + ((seed << 3) & 0xF0);
    return ((seed >> 5) + (seed >> 2)) & 1 ? pitch + ((amplitude * 5 * random) >> 8)
                                           : pitch - ((amplitude * 5 * random) >> 8);
}

static u8 randomVolume(s32 seed, s32 amplitude, s32 volume) {
    const s32 random = ((seed & 0xCC) >> 2) + ((seed & 0x13) << 2);
    return (u8)(volume * (0x8000 - amplitude * random) >> 15);
}

static int slotVolume(const Player* p) {
    return p->owner >= 0 ? sSlot[p->owner].vol : p->fixedVol;
}

static int slotPan(const Player* p) {
    return p->owner >= 0 ? sSlot[p->owner].pan : 64;
}

static void playerFree(Player* p) {
    p->pos = NULL;
    p->soundId = 0;
    p->priority = 0;
    p->owner = -1;
    if (p->v.owned) {
        voiceReset(&p->v);
    }
}

static void setInstrument(Player* p) {
    const int bank = p->pos[0], patch = p->pos[1];
    p->pos += 2;
    p->inst = instrument(bank, patch, &p->press, &p->release);
}

static void setVolume(Player* p) {
    const int v = *p->pos++;
    p->sfxVolume = v != 0 ? (v << 8 | 0xFF) : 0;
    p->changedVol = 1;
}

static s32 clientVolume(const Player* p, int basic) {
    s32 x = (0x8000 * p->sfxVolume) >> 15;
    if (!basic) {
        x = ((x * p->velocity) >> 7) * (p->volCur >> 16) >> 15;
    }
    return x;
}

static void startVoice(Player* p, s32 cents) {
    Voice* v = &p->v;
    if (p->inst == NULL || p->inst->pcm == NULL) {
        if (v->owned) {
            voiceReset(v);
        }
        return;
    }
    v->owned = 1;
    v->inst = p->inst;
    v->press = p->customPress != NULL ? p->customPress : p->press;
    v->release = p->release;
    v->pan = p->pan;
    v->reverb = p->reverb;
    v->pitch = pitchOf(cents - p->inst->keyBase) * p->inst->pitchRatio;
    v->clientVolume = clientVolume(p, p->mode == 0);
    v->donePending = 0;
    v->sync = 1;
}

/* the sequence's commands 0xE0.. */
static void command(Player* p, u8 op) {
    const u8* b = p->pos;
    switch (op - 0xE0) {
    case 0x00:
        setVolume(p);
        break;
    case 0x01:
        p->pan = *p->pos++;
        p->changedPan = 1;
        break;
    case 0x02:
        setInstrument(p);
        break;
    case 0x03:
        p->reverb = *p->pos++;
        p->changedPan = 1;
        break;
    case 0x04: { /* SetEnvelope: the engine's own presets (0 fast, the rest slow) */
        const int preset = *p->pos++ & 0x7F;
        p->press = preset == 0 ? kEnvFastPress : kEnvSlowPress;
        p->release = preset == 0 ? kEnvFastRelease : kEnvSlowRelease;
        break;
    }
    case 0x05:
        p->coarse = (s8)*p->pos++ * 100;
        break;
    case 0x06:
        p->fine = *p->pos++;
        break;
    case 0x07: /* WaitForEnd */
        if (p->v.owned) {
            p->delay = 2;
            p->pos--;
        }
        break;
    case 0x08: { /* PitchSweep */
        s16 time = (s16)(b[1] + (b[0] << 8));
        const s32 goal = (b[2] & 0x7F) * 100;
        p->pos += 3;
        if (time <= 0) {
            time = 1;
        }
        p->tuneTime = time;
        p->tuneGoal = goal;
        p->tuneStep = ((goal << 16) - p->tuneCur) / time;
        break;
    }
    case 0x09:
        p->loopCount = *p->pos++;
        p->loopStart = p->pos;
        break;
    case 0x0A:
        if (p->loopStart != NULL && (p->loopCount == 0 || --p->loopCount != 0)) {
            p->pos = p->loopStart;
        }
        break;
    case 0x0B: /* WaitForRelease */
        if (p->playLength != 0) {
            p->delay = 3;
            p->pos--;
        }
        break;
    case 0x0C: {
        const s32 v = *p->pos++;
        p->volCur = v != 0 ? (s32)((u32)v << 24 | 0xFFFFFF) : 0;
        p->changedVol = 1;
        break;
    }
    case 0x0D: { /* VolumeRamp */
        s16 time = (s16)(b[1] + (b[0] << 8));
        s32 goal = b[2];
        p->pos += 3;
        goal = goal != 0 ? (goal << 8 | 0xFF) : 0;
        if (time <= 0) {
            time = 1;
        }
        p->volTime = time;
        p->volGoal = goal;
        p->volStep = (s32)(((long long)goal << 16) - p->volCur) / time;
        break;
    }
    case 0x0E: /* an alternative sound (for the game's triggers, which we don't send) */
        p->pos += 3;
        break;
    case 0x0F:
        if (p->v.owned) {
            voiceReset(&p->v);
        }
        break;
    case 0x10: { /* Jump */
        const u32 off = (u32)(b[0] << 8) + b[1];
        p->start = b + 2;
        p->pos = inSef(sRom.sef + off, 1) ? sRom.sef + off : NULL;
        break;
    }
    case 0x11:
        p->pos = p->start;
        break;
    case 0x12:
        break;
    case 0x13:
        p->randomPitch = *p->pos++;
        break;
    case 0x14:
        p->randomVelocity = *p->pos++;
        break;
    case 0x15:
        p->pos++;
        break;
    case 0x16: { /* SetEnvelopePress */
        const u32 off = (u32)(b[0] << 8) + b[1];
        p->customPress = off != 0 && inSef(sRom.sef + off, 2) ? sRom.sef + off : NULL;
        p->pos = b + 2;
        break;
    }
    case 0x17: /* PlaySound: another (offset, info) entry, started as its own sound */
        p->pos = b + 4;
        break;
    case 0x18:
        p->pos++;
        break;
    default: /* unknown: the script ends */
        p->pos = NULL;
        break;
    }
}

static void updateSequence(Player* p) {
    int started = 0;
    int guard = 0;
    if (p->state == 0) {
        p->state = 1;
        if (p->v.owned) {
            voiceReset(&p->v);
        }
    }
    p->delay--;
    while (p->delay == 0 && guard++ < 256) {
        u8 op;
        if (p->pos == NULL || !inSef(p->pos, 1)) {
            playerFree(p);
            return;
        }
        op = *p->pos++;
        if (op < 0x80) {
            if (op == 0) {
                playerFree(p);
                return;
            }
            p->delay = op >= 0x78 ? (u8)(*p->pos++) + ((op & 7) << 8) + 0x78 : op;
        } else if (op < 0xD8) {
            u32 len;
            const s32 cents = (op & 0x7F) * 100;
            p->tuneCur = (p->randomPitch != 0 ? randomPitch((s32)sRandom, p->randomPitch, cents) : cents) << 16;
            p->velocity = p->randomVelocity != 0 ? randomVolume((s32)sRandom, p->randomVelocity, p->pos[0] & 0x7F)
                                                 : (u8)(p->pos[0] & 0x7F);
            len = p->pos[1];
            if (len >= 0xC0) {
                len = p->pos[2] + ((len & 0x3F) << 8) + 0xC0;
                p->pos += 3;
            } else {
                p->pos += 2;
            }
            p->playLength = (s32)len;
            startVoice(p, p->coarse + (s16)(p->tuneCur >> 16) + p->fine);
            started = 1;
        } else if (op >= 0xE0 && op <= 0xF8) {
            command(p, op);
            if (p->pos == NULL) {
                playerFree(p);
                return;
            }
        } else {
            playerFree(p);
            return;
        }
    }
    if (p->delay <= 0) {
        playerFree(p); /* a script that never waits */
        return;
    }
    if (p->volTime != 0) {
        if (--p->volTime != 0) {
            p->volCur += p->volStep;
        } else {
            p->volCur = p->volGoal << 16;
        }
        p->changedVol = 1;
    }
    if (!started) {
        if (p->playLength != 0 && --p->playLength == 0 && p->v.owned) {
            p->v.flags |= 0x10;
        }
        if (p->tuneTime != 0) {
            if (--p->tuneTime != 0) {
                p->tuneCur += p->tuneStep;
            } else {
                p->tuneCur = p->tuneGoal << 16;
            }
            p->changedTune = 1;
        }
        if (p->changedPan && p->v.owned) {
            p->v.pan = p->pan;
            p->v.reverb = p->reverb;
            p->v.sync |= 4;
        }
    }
    if (p->changedVol && p->v.owned) {
        p->v.clientVolume = clientVolume(p, 0);
        p->v.flags |= 0x20;
    }
    if (p->changedTune && p->v.owned && p->inst != NULL) {
        p->v.pitch = pitchOf(p->coarse + (s16)(p->tuneCur >> 16) - p->inst->keyBase + p->fine) * p->inst->pitchRatio;
    }
    p->changedVol = p->changedTune = p->changedPan = 0;
}

static void updateBasic(Player* p) {
    if (p->state == 0) {
        s32 tune;
        setInstrument(p);
        setVolume(p);
        p->pan = *p->pos++;
        p->reverb = *p->pos++;
        tune = (*p->pos++ & 0x7F) * 100;
        p->randomPitch = (*p->pos & 0xF) * 8;
        p->tuneCur = (p->randomPitch != 0 ? randomPitch((s32)sRandom, p->randomPitch, tune) : tune) << 16;
        startVoice(p, (s16)(p->tuneCur >> 16));
        p->state = 1;
        if (!p->v.owned) {
            playerFree(p);
        }
        return;
    }
    if (!p->v.owned) {
        playerFree(p);
    }
}

/* starts script at on player p */
static void playerStart(Player* p, const u8* at, u16 soundId, int priority, int owner) {
    if (p->v.owned) {
        voiceReset(&p->v);
    }
    memset(p, 0, offsetof(Player, v));
    p->owner = owner;
    p->serial = owner >= 0 ? sSlot[owner].serial : 0;
    p->cap = owner >= 0 && sSlot[owner].maxFrames > 0 ? sSlot[owner].maxFrames : 2 * RATE / FRAME;
    p->fixedVol = 100;
    p->soundId = soundId;
    p->priority = priority;
    p->pan = 64;
    p->sfxVolume = 0x7FFF;
    p->velocity = 127;
    p->delay = 1;
    p->volCur = 0x7FFFFFFF;
    p->press = kPressDefault;
    p->release = kReleaseDefault;
    p->flags = *at++;
    switch (p->flags & 3) {
    case 1:
        p->mode = 1;
        p->pos = at;
        break;
    case 2: /* compact: bank, patch, volume and random pitch, into a basic script of our own */
        p->mode = 0;
        p->oneShot[0] = at[0];
        p->oneShot[1] = at[1];
        p->oneShot[2] = (u8)((at[2] >> 1) | 3);
        p->oneShot[3] = 64;
        p->oneShot[4] = 0;
        p->oneShot[5] = 0x80 + 48;
        p->oneShot[6] = at[2] & 7;
        p->pos = p->oneShot;
        break;
    default:
        p->mode = 0;
        p->pos = at;
        break;
    }
    p->start = p->pos;
}

static Player* playerFor(u16 soundId, int owner, int priority) {
    int i, best = -1;
    for (i = 0; i < PLAYERS; i++) { /* the same sound from the same slot: it starts again there */
        if (sPlayer[i].pos != NULL && sPlayer[i].soundId == soundId && sPlayer[i].owner == owner) {
            return &sPlayer[i];
        }
    }
    for (i = 0; i < PLAYERS; i++) {
        if (sPlayer[i].pos == NULL && !sPlayer[i].v.owned) {
            return &sPlayer[i];
        }
    }
    for (i = 0; i < PLAYERS; i++) { /* else the orphan longest gone, or the lowest priority */
        if (sPlayer[i].priority <= priority &&
            (best < 0 || sPlayer[i].orphan > sPlayer[best].orphan ||
             (sPlayer[i].orphan == sPlayer[best].orphan && sPlayer[i].priority < sPlayer[best].priority))) {
            best = i;
        }
    }
    return best >= 0 ? &sPlayer[best] : NULL;
}

/* au_sfx_try_sound's lookups: a sound id to its script(s), each started on a player */
static void trySound(u16 soundId, int owner) {
    const u8* sef = sRom.sef;
    const u8* entry;
    Player* p;
    if (sef == NULL || (soundId & 0x23FF) == 0) {
        return;
    }
    if (soundId & 0x2000) {
        const u32 index = (u32)(soundId - 1) & 0x1FF;
        const u32 extra = rd16(sef + 0x20);
        if (sef[0x0E] != 1 || extra == 0 || index >= 0x140 || !inSef(sef + extra + index * 4, 4)) {
            return;
        }
        entry = sef + extra + index * 4;
        if (rd16(entry) != 0 && (p = playerFor(soundId, owner, 0)) != NULL) {
            playerStart(p, entry, soundId, 0, owner);
        }
        return;
    }
    {
        const u32 index = (u32)(soundId - 1) & 0xFF;
        u32 section, table;
        if (index >= 0xC0) {
            section = (u32)(((soundId & 0x3FF) - 1) >> 8) + 4;
        } else {
            section = (soundId >> 8) & 3;
        }
        if (section >= 8 || (table = rd16(sef + 0x10 + section * 2)) == 0) {
            return;
        }
        entry = sef + table + (index >= 0xC0 ? index - 0xC0 : index) * 4;
        if (!inSef(entry, 4) || rd16(entry) == 0) {
            return;
        }
        if (index >= 0xC0) {
            if ((p = playerFor(soundId, owner, 0)) != NULL) {
                playerStart(p, entry, soundId, 0, owner);
            }
            return;
        }
        {
            const u16 info = rd16(entry + 2);
            const int priority = (info >> 8) & 3, poly = (info >> 5) & 3;
            const u8* at = sef + rd16(entry);
            if (!inSef(at, 1)) {
                return;
            }
            if (poly == 0) {
                if ((p = playerFor(soundId, owner, priority)) != NULL) {
                    playerStart(p, at, soundId, priority, owner);
                }
            } else {
                /* polyphonic: a list of (offset, info) pairs, each on a player of its own */
                const int tracks = 2 << (poly - 1);
                int k, i;
                for (i = 0; i < PLAYERS; i++) {
                    if (sPlayer[i].pos != NULL && sPlayer[i].soundId == soundId && sPlayer[i].owner == owner) {
                        playerFree(&sPlayer[i]);
                    }
                }
                for (k = 0; k < tracks && inSef(at + k * 4, 4); k++) {
                    const u8* track = sef + rd16(at + k * 4);
                    if (rd16(at + k * 4) == 0 || !inSef(track, 1)) {
                        continue;
                    }
                    for (i = 0; i < PLAYERS && (sPlayer[i].pos != NULL || sPlayer[i].v.owned); i++) {
                    }
                    if (i < PLAYERS) {
                        playerStart(&sPlayer[i], track, soundId, priority, owner);
                    }
                }
            }
        }
    }
}

/* ---- the audio frame ---- */

static void beginFrame(void) {
    int i;
    for (i = 0; i < PLAYERS; i++) {
        Player* p = &sPlayer[i];
        Voice* v = &p->v;
        if (v->donePending) {
            v->donePending = 0;
            v->cmd = NULL;
            v->owned = 0;
            v->playing = 0;
        }
        if (v->sync & 1) {
            voiceStart(v, slotVolume(p), slotPan(p));
        } else if (v->sync & 4) {
            setMix(v, slotVolume(p), slotPan(p));
        } else if (v->playing) {
            setMix(v, slotVolume(p), slotPan(p)); /* the slot's volume and pan follow the game's */
        }
        v->sync = 0;
    }
}

static void updatePlayers(void) {
    int i;
    sFrameCounter++;
    sRandom = (u16)sRandom + (u16)sFrameCounter;
    for (i = 0; i < PLAYERS; i++) {
        Player* p = &sPlayer[i];
        if (p->pos == NULL) {
            continue;
        }
        if (p->mode == 1) {
            updateSequence(p);
        } else {
            updateBasic(p);
        }
    }
}

static void synthFrame(float* mix) {
    int i, k;
    for (i = 0; i < PLAYERS; i++) {
        Voice* v = &sPlayer[i].v;
        const Inst* in = v->inst;
        float step, dl, dr;
        if (!v->playing || in == NULL) {
            continue;
        }
        step = v->pitch < 1.99996f ? v->pitch : 1.99996f;
        {
            const int n = v->rampLeft > FRAME ? FRAME : v->rampLeft;
            const float r = v->rampLeft > 0 ? (float)n / (float)v->rampLeft : 1.0f;
            dl = (v->gainL + (v->toL - v->gainL) * r - v->gainL) / FRAME;
            dr = (v->gainR + (v->toR - v->gainR) * r - v->gainR) / FRAME;
            v->rampLeft -= n;
        }
        for (k = 0; k < FRAME; k++) {
            const s32 at = (s32)v->pos;
            const float f = (float)(v->pos - at);
            s32 q = at + 1;
            float smp;
            if (in->loopEnd != 0 && v->loopsLeft != 0 && q >= in->loopEnd) {
                q = in->loopStart + (q - in->loopEnd);
            }
            if (at >= in->length) {
                v->playing = 0;
                break;
            }
            smp = (float)in->pcm[at] + ((q < in->length ? (float)in->pcm[q] : 0.0f) - (float)in->pcm[at]) * f;
            mix[k * 2] += smp * v->gainL;
            mix[k * 2 + 1] += smp * v->gainR;
            v->gainL += dl;
            v->gainR += dr;
            v->pos += step;
            if (in->loopEnd != 0 && v->loopsLeft != 0 && v->pos >= in->loopEnd) {
                v->pos -= in->loopEnd - in->loopStart;
                if (v->loopsLeft > 0) {
                    v->loopsLeft--;
                }
            }
        }
        if (!v->playing) {
            v->cmd = NULL;
            v->owned = 0;
        }
    }
}

/* one audio frame into mix (FRAME stereo floats, added) */
static void audioFrame(float* mix) {
    int i;
    beginFrame();
    sCounter -= STEP;
    if (sCounter <= 0) {
        sCounter += INTERVAL;
        updatePlayers();
    }
    for (i = 0; i < PLAYERS; i++) {
        Player* p = &sPlayer[i];
        voiceEnvelope(&p->v);
        if (p->owner < 0 || sSlot[p->owner].serial != p->serial || !sSlot[p->owner].live) {
            /* its slot ended (or it has none): a one-shot finishes, anything longer is let go after its cap */
            if (++p->orphan > p->cap && (p->pos != NULL || p->v.owned)) {
                if (p->v.owned && !(p->v.flags & 1)) {
                    p->v.flags |= 0x10; /* released: its envelope's release */
                }
                p->pos = NULL;
            }
        }
    }
    synthFrame(mix);
}

/* ---- the API ---- */

static int* sSetting;
static int* sGeneral;
static int* sSfxVolume;
static int* sSfxMute;

static void declareSettings(void) {
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("pm64_sfx", 1, 0, 1);
        sGeneral = portSettingsDeclare("pm64_sfx_general", 0, 0, 1);
        sSfxMute = portSettingsDeclare("sfx_mute", 0, 0, 1); /* (for the mixer, here on the game thread) */
        sSfxVolume = portSettingsDeclare("sfx_volume", 100, 0, 100);
    }
}

int portPm64SfxActive(void) {
    declareSettings();
    return *sSetting != 0 && portPm64Linked() != PM64_NONE;
}

int portPm64SfxGeneralActive(void) {
    declareSettings();
    return *sGeneral != 0 && portPm64Linked() != PM64_NONE;
}

/* the game thread: every player stopped and the ROM's audio let go (the lock held only to take it away from
 * the audio thread; the instruments are freed and the segment released after) */
static void romDrop(void) {
    s16* pcm[MAX_INSTS];
    const u8* sbn;
    int i, n, held;
    LOCK();
    for (i = 0; i < PLAYERS; i++) {
        memset(&sPlayer[i], 0, sizeof(sPlayer[i]));
        sPlayer[i].owner = -1;
    }
    n = sRom.instUsed;
    for (i = 0; i < n; i++) {
        pcm[i] = sRom.inst[i].pcm;
    }
    held = sRom.ready > 0;
    sbn = sRom.sbn;
    memset(&sRom, 0, sizeof(sRom));
    UNLOCK();
    for (i = 0; i < n; i++) {
        free(pcm[i]);
    }
    if (held) {
        portPm64AudioHold(sbn, -1);
    }
}

/* the ROM's audio ready on the game thread (NOT under the lock: the first load reads from disk); 0 if
 * there's none */
static int romReady(void) {
    declareSettings();
    if (sRom.ready != 0 && sRom.generation != portPm64Generation()) {
        romDrop();
    }
    if (sRom.ready == 0 && romLoad()) {
        LOCK();
        sRom.ready = 1; /* (published: the audio thread mixes from here on) */
        UNLOCK();
    }
    return sRom.ready > 0;
}

/* ---- the table: the game's sounds that become PM64's ---- */

enum {
    W_OWN,   /* the name is its host's own: partner arg is on that host */
    W_OUT,   /* a partner's shared name: partner arg is the one out (battle or field) */
    W_ACT,   /* anyone's name: partner arg is the battle's acting unit (in the field: out) */
    W_ENEMY, /* an enemy's name: a PM64 enemy whose host is unit type arg is in the battle */
    W_GEN,   /* anyone's general action (jump, hammer, coin, menu): with "pm64_sfx_general" on */
};

enum { GOOMBARIO = 1, KOOPER, BOMBETTE, PARAKARRY, WATT, SUSHIE, LAKILESTER, BOW };

typedef struct SfxMap {
    const char* name;
    u8 who;
    u16 arg;
    u16 sound;
    u16 cap; /* audio frames it may go on after the game's sound ended (0: 2 s) */
} SfxMap;

/* PM64 sound ids (the decomp's SOUND_ names, typed in as numbers) */
#define S_GOOMBARIO_JUMP 0x200D
#define S_GOOMBARIO_HEADBONK 0x0281
#define S_GOOMBARIO_GATHERING 0x2003
#define S_TATTLE_WINDOW_OPEN 0x0282
#define S_CHARGE_UP 0x208F
#define S_STEP_NORMAL1 0x0141
#define S_STEP_NORMAL2 0x0142
#define S_LAND_SOFTLY 0x0148
#define S_QUICK_PLAYER_JUMP 0x2081
#define S_KOOPER_STEP 0x0100
#define S_KOOPER_SHELL_KICK 0x0283
#define S_KOOPER_SHELL_SWIRL 0x0284
#define S_KOOPER_PICKUP 0x0286
#define S_KOOPER_SHELL_SPINUP 0x200C
#define S_FIRE_SHELL_LAUNCH 0x028F
#define S_DIZZY_SHELL_LAUNCH 0x0290
#define S_BOMBETTE_FUSE 0x0287
#define S_BOMBETTE_BLAST_LV1 0x2016
#define S_BOMBETTE_BLAST_LV2 0x2017
#define S_BOMBETTE_BLAST_LV3 0x2018
#define S_BOMBETTE_BODY_SLAM 0x2020
#define S_PARAKARRY_PREDIVE 0x2004
#define S_PARAKARRY_DIVE 0x2005
#define S_PARAKARRY_SHELL_SHOT 0x2006
#define S_PARAKARRY_FLAP 0x2009
#define S_PARAKARRY_AIR_RAID_1 0x200A
#define S_PARAKARRY_AIR_RAID_2 0x200B
#define S_WATT_CHARGE 0x0289
#define S_WATT_POWER_SHOCK 0x028A
#define S_WATT_TURBO_CHARGE 0x028B
#define S_WATT_MEGA_DISCHARGE 0x028C
#define S_WATT_MEGA_CHARGE_WAVE 0x2024
#define S_WATT_REPEL_DARKNESS 0x2011
#define S_SUSHIE_STEP 0x029A
#define S_SUSHIE_DIVE 0x0294
#define S_SUSHIE_SQUIRT 0x0297
#define S_SUSHIE_FOUNTAIN 0x0298
#define S_SUSHIE_EMBARK 0x2013
#define S_SUSHIE_BELLY_FLOP 0x2034
#define S_TIDAL_WAVE_LEAP_A 0x029B
#define S_TIDAL_WAVE_WATER 0x029D
#define S_CREATE_WATER_BLOCK 0x2035
#define S_FLIGHT 0x0295
#define S_LAKILESTER_THROW_SPINY_A 0x201B
#define S_LAKILESTER_THROW_SPINY_B 0x201C
#define S_LAKILESTER_MAKE_CLOUD_NINE 0x201D
#define S_LAKILESTER_HURRICANE_INHALE 0x201E
#define S_LAKILESTER_HURRICANE_EXHALE 0x201F
#define S_LAKILESTER_HURRICANE_WIND 0x0288
#define S_GOOMBA_STEP 0x20DD
#define S_PARAGOOMBA_FLY 0x20DF
#define S_PARAGOOMBA_DIVE 0x02C1
#define S_KOOPA_LAUNCH_SHELL 0x037C
#define S_BOBOMB_FUSE 0x02A8
#define S_BOMB_BLAST 0x2076
#define S_FUZZY_HOP_A 0x0331
#define S_FUZZY_HOP_B 0x0332
#define S_POKEY_WALK 0x0343
#define S_POKEY_THROW 0x02CE
#define S_DRY_BONES_THROW 0x02D1
#define S_DRY_BONES_COLLAPSE 0x02D2
#define S_DRY_BONES_ARISE 0x02D3
#define S_MAGIKOOPA_WAND_STRIKE 0x02DC
#define S_MAGIKOOPA_VANISH 0x02DB
#define S_HAMMER_BROS_THROW 0x02E9
#define S_LAKITU_THROW_SPINY 0x20D2
#define S_SPINY_SPIN_UP 0x02C7
#define S_BULLET_BILL_FIRE 0x02C8
#define S_BULLET_BILL_EXPLODE_A 0x2078
#define S_SHY_GUY_STEP_A 0x03B6
#define S_SHY_GUY_STEP_B 0x03B7
#define S_STAR_SPIRIT_APPEAR_A 0x2041

static const SfxMap kMap[] = {
    /* Goombario (Goombella's place): his jump, Headbonk, Multibonk, Tattle, Charge */
    {"SFX_PARTY_BATTLE_JUMP1", W_OUT, GOOMBARIO, S_GOOMBARIO_JUMP, 0},
    {"SFX_PARTY_JUMP1", W_OUT, GOOMBARIO, S_GOOMBARIO_JUMP, 0},
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, GOOMBARIO, S_STEP_NORMAL1, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, GOOMBARIO, S_STEP_NORMAL2, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, GOOMBARIO, S_STEP_NORMAL1, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, GOOMBARIO, S_STEP_NORMAL2, 0},
    {"SFX_PARTY_MOVE1L", W_OUT, GOOMBARIO, S_STEP_NORMAL1, 0},
    {"SFX_PARTY_MOVE1R", W_OUT, GOOMBARIO, S_STEP_NORMAL2, 0},
    {"SFX_PARTY_BATTLE_LANDING1", W_OUT, GOOMBARIO, S_LAND_SOFTLY, 0},
    {"SFX_BTL_KURI_ATTACK1", W_OWN, GOOMBARIO, S_GOOMBARIO_HEADBONK, 0},
    {"SFX_BTL_KURI_ATTACK2", W_OWN, GOOMBARIO, S_GOOMBARIO_HEADBONK, 0},
    {"SFX_BTL_KURI_LEARNED1", W_OWN, GOOMBARIO, S_TATTLE_WINDOW_OPEN, 0},
    {"SFX_BTL_KURI_LEARNED2", W_OWN, GOOMBARIO, S_TATTLE_WINDOW_OPEN, 0},
    {"SFX_CONDITION_CHARGE1", W_ACT, GOOMBARIO, S_GOOMBARIO_GATHERING, 0},

    /* Kooper (Koops' place): his steps, the shell's spin-up, kick and swirl, Power Shell, Fire and Dizzy Shell */
    {"SFX_PARTY_BATTLE_JUMP1", W_OUT, KOOPER, S_QUICK_PLAYER_JUMP, 0},
    {"SFX_PARTY_JUMP1", W_OUT, KOOPER, S_QUICK_PLAYER_JUMP, 0},
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, KOOPER, S_KOOPER_STEP, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, KOOPER, S_KOOPER_STEP, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, KOOPER, S_KOOPER_STEP, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, KOOPER, S_KOOPER_STEP, 0},
    {"SFX_PARTY_MOVE1L", W_OUT, KOOPER, S_KOOPER_STEP, 0},
    {"SFX_PARTY_MOVE1R", W_OUT, KOOPER, S_KOOPER_STEP, 0},
    {"SFX_BTL_NOKO_ATTACK1", W_OWN, KOOPER, S_KOOPER_SHELL_SPINUP, 0},
    {"SFX_BTL_NOKO_ATTACK2", W_OWN, KOOPER, S_KOOPER_SHELL_KICK, 0},
    {"SFX_BTL_NOKO_SHUBIBIN1", W_OWN, KOOPER, S_KOOPER_SHELL_SPINUP, 0},
    {"SFX_BTL_NOKO_SHUBIBIN2", W_OWN, KOOPER, S_KOOPER_SHELL_SWIRL, 0},
    {"SFX_BTL_NOKO_IPPATSU1", W_OWN, KOOPER, S_KOOPER_SHELL_SPINUP, 0},
    {"SFX_BTL_NOKO_IPPATSU2", W_OWN, KOOPER, S_FIRE_SHELL_LAUNCH, 0},
    {"SFX_PARTY_PNK_KOURA1", W_OUT, KOOPER, S_KOOPER_SHELL_KICK, 0},
    {"SFX_PARTY_PNK_KOURA_BACK1", W_OUT, KOOPER, S_KOOPER_SHELL_SWIRL, 0},
    {"SFX_PARTY_PNK_HOLD1", W_OUT, KOOPER, S_KOOPER_PICKUP, 0},
    {"SFX_AUDIENCE_THROW_OBJECT1", W_ACT, KOOPER, S_DIZZY_SHELL_LAUNCH, 0},

    /* Bombette (Bobbery's place): the fuse, Body Slam, Bomb, Power Bomb, Mega Bomb's blasts */
    {"SFX_PARTY_BATTLE_JUMP1", W_OUT, BOMBETTE, S_QUICK_PLAYER_JUMP, 0},
    {"SFX_PARTY_JUMP1", W_OUT, BOMBETTE, S_QUICK_PLAYER_JUMP, 0},
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, BOMBETTE, S_STEP_NORMAL1, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, BOMBETTE, S_STEP_NORMAL2, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, BOMBETTE, S_STEP_NORMAL1, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, BOMBETTE, S_STEP_NORMAL2, 0},
    {"SFX_PARTY_MOVE1L", W_OUT, BOMBETTE, S_STEP_NORMAL1, 0},
    {"SFX_PARTY_MOVE1R", W_OUT, BOMBETTE, S_STEP_NORMAL2, 0},
    {"SFX_BTL_THUNDERS_ATTACK1", W_OWN, BOMBETTE, S_BOMBETTE_FUSE, 180},
    {"SFX_BTL_THUNDERS_ATTACK2", W_OWN, BOMBETTE, S_BOMBETTE_BODY_SLAM, 0},
    {"SFX_BTL_THUNDERS_ATTACK3", W_OWN, BOMBETTE, S_BOMBETTE_BLAST_LV1, 0},
    {"SFX_BTL_THUNDERS_ATTACK4", W_OWN, BOMBETTE, S_BOMBETTE_BLAST_LV3, 0},
    {"SFX_BTL_THUNDERS_BOMB1", W_OWN, BOMBETTE, S_BOMBETTE_BLAST_LV2, 0},
    {"SFX_BTL_THUNDERS_BOMB2", W_OWN, BOMBETTE, S_BOMBETTE_BLAST_LV2, 0},
    {"SFX_PARTY_PBM_FUSE1", W_OUT, BOMBETTE, S_BOMBETTE_FUSE, 180},
    {"SFX_PARTY_PBM_FUSE2", W_OUT, BOMBETTE, S_BOMBETTE_FUSE, 180},
    {"SFX_PARTY_PBM_BOMB1", W_OUT, BOMBETTE, S_BOMBETTE_BLAST_LV1, 0},

    /* Parakarry (Yoshi's place, flying): wing flaps for steps and jumps, Sky Dive, Shell Shot, Air Lift, Air Raid */
    {"SFX_PARTY_BATTLE_JUMP1", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_PARTY_JUMP1", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, PARAKARRY, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, PARAKARRY, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_LANDING1", W_OUT, PARAKARRY, SILENT_ID, 0},
    {"SFX_PARTY_YOSH_MOVE1", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_PARTY_YOSH_JUMP1", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_PARTY_YOSH_RIDE1", W_OUT, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_BTL_YOSHI_ATTACK1", W_OWN, PARAKARRY, S_PARAKARRY_DIVE, 0},
    {"SFX_BTL_YOSHI_EGG1", W_OWN, PARAKARRY, S_PARAKARRY_PREDIVE, 0},
    {"SFX_BTL_YOSHI_EGG2", W_OWN, PARAKARRY, S_PARAKARRY_SHELL_SHOT, 0},
    {"SFX_BTL_YOSHI_EGG3", W_OWN, PARAKARRY, S_PARAKARRY_SHELL_SHOT, 0},
    {"SFX_BTL_YOSHI_SWALLOW1", W_OWN, PARAKARRY, S_PARAKARRY_FLAP, 0},
    {"SFX_BTL_YOSHI_SWALLOW5", W_OWN, PARAKARRY, S_PARAKARRY_AIR_RAID_1, 0},
    {"SFX_BTL_YOSHI_TAIGUN1", W_OWN, PARAKARRY, S_PARAKARRY_AIR_RAID_2, 0},

    /* Watt (Ms. Mowz's place): no steps (he floats), Electro Dash, Power Shock, Turbo Charge, Mega Shock */
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_LANDING1", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_PARTY_MOVE1L", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_PARTY_MOVE1R", W_OUT, WATT, SILENT_ID, 0},
    {"SFX_BTL_CHUCHURINA_ATTACK1", W_OWN, WATT, S_WATT_CHARGE, 0},
    {"SFX_BTL_CHUCHURINA_ATTACK2", W_OWN, WATT, S_WATT_POWER_SHOCK, 0},
    {"SFX_BTL_CHUCHURINA_ATTACK3", W_OWN, WATT, S_WATT_POWER_SHOCK, 0},
    {"SFX_BTL_CHURINA_CONFUSE1", W_OWN, WATT, S_WATT_MEGA_CHARGE_WAVE, 0},
    {"SFX_BTL_CHURINA_CONFUSE1_2", W_OWN, WATT, S_WATT_MEGA_DISCHARGE, 0},
    {"SFX_BTL_CHURINA_CONFUSE_MOVE1L", W_OWN, WATT, SILENT_ID, 0},
    {"SFX_BTL_CHURINA_CONFUSE_MOVE1R", W_OWN, WATT, SILENT_ID, 0},
    {"SFX_BTL_DAMAGE_BIRIBIRI1", W_ACT, WATT, S_WATT_POWER_SHOCK, 0},
    {"SFX_CONDITION_CHARGE1", W_ACT, WATT, S_WATT_TURBO_CHARGE, 0},
    {"SFX_PARTY_CHURINA_SMELL1", W_OUT, WATT, S_WATT_REPEL_DARKNESS, 0},

    /* Sushie (Vivian's place): her steps, diving under, Belly Flop, Squirt, Water Block, Tidal Wave */
    {"SFX_PARTY_BATTLE_JUMP1", W_OUT, SUSHIE, S_TIDAL_WAVE_LEAP_A, 0},
    {"SFX_PARTY_JUMP1", W_OUT, SUSHIE, S_QUICK_PLAYER_JUMP, 0},
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, SUSHIE, S_SUSHIE_STEP, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, SUSHIE, S_SUSHIE_STEP, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, SUSHIE, S_SUSHIE_STEP, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, SUSHIE, S_SUSHIE_STEP, 0},
    {"SFX_PARTY_MOVE1L", W_OUT, SUSHIE, S_SUSHIE_STEP, 0},
    {"SFX_PARTY_MOVE1R", W_OUT, SUSHIE, S_SUSHIE_STEP, 0},
    {"SFX_BTL_VIVIAN_ATTACK1", W_OWN, SUSHIE, S_TIDAL_WAVE_LEAP_A, 0},
    {"SFX_BTL_VIVIAN_ATTACK2", W_OWN, SUSHIE, S_SUSHIE_BELLY_FLOP, 0},
    {"SFX_BTL_VIVIAN_ATTACK3", W_OWN, SUSHIE, S_SUSHIE_BELLY_FLOP, 0},
    {"SFX_BTL_VIVIAN_ATTACK4", W_OWN, SUSHIE, S_SUSHIE_SQUIRT, 0},
    {"SFX_BTL_VIVIAN_SHADOW1_1", W_OWN, SUSHIE, S_SUSHIE_DIVE, 0},
    {"SFX_BTL_VIVIAN_SHADOW1_2", W_OWN, SUSHIE, S_SUSHIE_EMBARK, 0},
    {"SFX_BTL_VIVIAN_MAGIC1", W_OWN, SUSHIE, S_TIDAL_WAVE_WATER, 0},
    {"SFX_BTL_VIVIAN_MEROMERO1", W_OWN, SUSHIE, S_CREATE_WATER_BLOCK, 0},
    {"SFX_PARTY_VIVIAN_SHADOW1", W_OUT, SUSHIE, S_SUSHIE_DIVE, 0},
    {"SFX_PARTY_VIVIAN_SHADOW1_1", W_OUT, SUSHIE, S_SUSHIE_DIVE, 0},
    {"SFX_PARTY_VIVIAN_SHADOW1_2", W_OUT, SUSHIE, S_SUSHIE_DIVE, 0},
    {"SFX_PARTY_VIVIAN_SHADOW2", W_OUT, SUSHIE, S_SUSHIE_EMBARK, 0},

    /* Lakilester (Flurrie's place, on his cloud): the cloud's flight; Gale Force in the field his Hurricane's wind
     * (his battle moves name their PM64 sounds in pm64_partner_pc.c's scripts: Spiny Flip, Spiny Surge, Cloud
     * Nine, Hurricane's breath in and out) */
    {"SFX_PARTY_BATTLE_JUMP1", W_OUT, LAKILESTER, S_FLIGHT, 0},
    {"SFX_PARTY_JUMP1", W_OUT, LAKILESTER, S_FLIGHT, 0},
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, LAKILESTER, S_FLIGHT, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, LAKILESTER, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, LAKILESTER, S_FLIGHT, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, LAKILESTER, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_LANDING1", W_OUT, LAKILESTER, SILENT_ID, 0},
    {"SFX_PARTY_CLAUD_WIND1", W_OWN, LAKILESTER, S_LAKILESTER_HURRICANE_WIND, 0},
    {"SFX_CONDITION_CHARGE1", W_ACT, LAKILESTER, S_LAKILESTER_MAKE_CLOUD_NINE, 0},

    /* Bow (Vivian's place): she floats without a sound; Outta Sight in the field (her battle moves name their
     * PM64 sounds in pm64_partner_pc.c's scripts) */
    {"SFX_PARTY_BATTLE_MOVE1L", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_MOVE1R", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1L", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_SUBMOVE1R", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_BATTLE_LANDING1", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_MOVE1L", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_MOVE1R", W_OUT, BOW, SILENT_ID, 0},
    {"SFX_PARTY_VIVIAN_SHADOW1", W_OUT, BOW, PM64_SOUND_BOW_VANISH, 0},
    {"SFX_PARTY_VIVIAN_SHADOW1_1", W_OUT, BOW, PM64_SOUND_BOW_VANISH, 0},
    {"SFX_PARTY_VIVIAN_SHADOW1_2", W_OUT, BOW, PM64_SOUND_BOW_VANISH, 0},
    {"SFX_PARTY_VIVIAN_SHADOW2", W_OUT, BOW, PM64_SOUND_BOW_APPEAR, 0},

    /* the PM64 enemies (by the TTYD enemy each fights as) */
    {"SFX_ENM_KURI_MOVE1", W_ENEMY, UNIT_GOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE2", W_ENEMY, UNIT_GOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE1", W_ENEMY, UNIT_HYPER_GOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE2", W_ENEMY, UNIT_HYPER_GOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE1", W_ENEMY, UNIT_GLOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE2", W_ENEMY, UNIT_GLOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE1", W_ENEMY, UNIT_SPIKY_GOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE2", W_ENEMY, UNIT_SPIKY_GOOMBA, S_GOOMBA_STEP, 0},
    {"SFX_ENM_KURI_MOVE1", W_ENEMY, UNIT_GOOMBA_GLITZVILLE, S_SHY_GUY_STEP_A, 0},
    {"SFX_ENM_KURI_MOVE2", W_ENEMY, UNIT_GOOMBA_GLITZVILLE, S_SHY_GUY_STEP_B, 0},
    {"SFX_ENM_PATAKURI_MOVE1", W_ENEMY, UNIT_PARAGOOMBA, S_PARAGOOMBA_FLY, 0},
    {"SFX_ENM_PATAKURI_MOVE2", W_ENEMY, UNIT_PARAGOOMBA, S_PARAGOOMBA_FLY, 0},
    {"SFX_ENM_PATAKURI_MOVE3", W_ENEMY, UNIT_PARAGOOMBA, S_PARAGOOMBA_FLY, 0},
    {"SFX_ENM_KURI_ATTACK1", W_ENEMY, UNIT_PARAGOOMBA, S_PARAGOOMBA_DIVE, 0},
    {"SFX_ENM_NOKO_SHELL1", W_ENEMY, UNIT_KOOPA_TROOPA, S_KOOPA_LAUNCH_SHELL, 0},
    {"SFX_ENM_NOKO_SHELL1", W_ENEMY, UNIT_PARATROOPA, S_KOOPA_LAUNCH_SHELL, 0},
    {"SFX_ENM_NOKO_SHELL1", W_ENEMY, UNIT_DARK_KOOPA, S_KOOPA_LAUNCH_SHELL, 0},
    {"SFX_ENM_NOKO_MOVE1", W_ENEMY, UNIT_KOOPA_TROOPA, S_KOOPER_STEP, 0},
    {"SFX_ENM_NOKO_MOVE2", W_ENEMY, UNIT_KOOPA_TROOPA, S_KOOPER_STEP, 0},
    {"SFX_ENM_BOMB_SPARK1", W_ENEMY, UNIT_BOB_OMB, S_BOBOMB_FUSE, 120},
    {"SFX_BTL_ENM_BOMB_ATTACK2", W_ENEMY, UNIT_BOB_OMB, S_BOMB_BLAST, 0},
    {"SFX_ENM_CHORO1_MOVE1", W_ENEMY, UNIT_FUZZY, S_FUZZY_HOP_A, 0},
    {"SFX_ENM_CHORO1_LANDING1", W_ENEMY, UNIT_FUZZY, S_FUZZY_HOP_B, 0},
    {"SFX_ENM_CHORO1_MOVE1", W_ENEMY, UNIT_GREEN_FUZZY, S_FUZZY_HOP_A, 0},
    {"SFX_ENM_CHORO1_LANDING1", W_ENEMY, UNIT_GREEN_FUZZY, S_FUZZY_HOP_B, 0},
    {"SFX_ENM_CHORO1_MOVE1", W_ENEMY, UNIT_FLOWER_FUZZY, S_FUZZY_HOP_A, 0},
    {"SFX_ENM_CHORO1_LANDING1", W_ENEMY, UNIT_FLOWER_FUZZY, S_FUZZY_HOP_B, 0},
    {"SFX_ENM_SAMBO_MOVE1", W_ENEMY, UNIT_POKEY, S_POKEY_WALK, 0},
    {"SFX_ENM_SAMBO_MOVE2", W_ENEMY, UNIT_POKEY, S_POKEY_WALK, 0},
    {"SFX_ENM_SAMBO_ATTACK1", W_ENEMY, UNIT_POKEY, S_POKEY_THROW, 0},
    {"SFX_ENM_SAMBO_MOVE1", W_ENEMY, UNIT_POISON_POKEY, S_POKEY_WALK, 0},
    {"SFX_ENM_SAMBO_MOVE2", W_ENEMY, UNIT_POISON_POKEY, S_POKEY_WALK, 0},
    {"SFX_ENM_SAMBO_ATTACK1", W_ENEMY, UNIT_POISON_POKEY, S_POKEY_THROW, 0},
    {"SFX_ENM_HONENOKO_BONE_THROW2", W_ENEMY, UNIT_DRY_BONES, S_DRY_BONES_THROW, 0},
    {"SFX_ENM_HONENOKO_BONE_THROW3", W_ENEMY, UNIT_DRY_BONES, S_DRY_BONES_THROW, 0},
    {"SFX_ENM_HONENOKO_CRUMBLE1", W_ENEMY, UNIT_DRY_BONES, S_DRY_BONES_COLLAPSE, 0},
    {"SFX_ENM_HONENOKO_UNITE1", W_ENEMY, UNIT_DRY_BONES, S_DRY_BONES_ARISE, 0},
    {"SFX_ENM_KAMEKU_MAGIC1", W_ENEMY, UNIT_MAGIKOOPA, S_MAGIKOOPA_WAND_STRIKE, 0},
    {"SFX_ENM_KAMEKU_OFFSHOOT1", W_ENEMY, UNIT_MAGIKOOPA, S_MAGIKOOPA_VANISH, 0},
    {"SFX_ENM_HUMMER_ATTACK1", W_ENEMY, UNIT_HAMMER_BRO, S_HAMMER_BROS_THROW, 0},
    {"SFX_ENM_JUGEM_THROW1", W_ENEMY, UNIT_LAKITU, S_LAKITU_THROW_SPINY, 0},
    {"SFX_ENM_TOGEZO_ROLL1", W_ENEMY, UNIT_SPINY, S_SPINY_SPIN_UP, 0},
    {"SFX_ENM_KILLER_ATTACK1", W_ENEMY, UNIT_BULLET_BILL, S_BULLET_BILL_FIRE, 0},
    {"SFX_BTL_ENEMY_DIE1_2", W_ENEMY, UNIT_BULLET_BILL, S_BULLET_BILL_EXPLODE_A, 0},

    /* general actions ("pm64_sfx_general", off by default): Mario's jump and hammer, the pick-ups, the menus.
     * Last, so a partner's or enemy's own entry above wins. */
    {"SFX_MARIO_JUMP1", W_GEN, 0, PM64_SOUND_QUICK_PLAYER_JUMP, 0},
    {"SFX_MARIO_BATTLE_JUMP1", W_GEN, 0, PM64_SOUND_QUICK_PLAYER_JUMP, 0},
    {"SFX_BTL_JUMP_CONTINUE1", W_GEN, 0, PM64_SOUND_JUMP_COMBO_1, 0},
    {"SFX_BTL_JUMP_CONTINUE2", W_GEN, 0, PM64_SOUND_JUMP_COMBO_2, 0},
    {"SFX_BTL_JUMP_CONTINUE3", W_GEN, 0, PM64_SOUND_JUMP_COMBO_3, 0},
    {"SFX_BTL_JUMP_CONTINUE4", W_GEN, 0, PM64_SOUND_JUMP_COMBO_4, 0},
    {"SFX_BTL_JUMP_CONTINUE5", W_GEN, 0, PM64_SOUND_JUMP_COMBO_5, 0},
    {"SFX_BTL_JUMP_CONTINUE6", W_GEN, 0, PM64_SOUND_JUMP_COMBO_6, 0},
    {"SFX_BTL_JUMP_CONTINUE7", W_GEN, 0, PM64_SOUND_JUMP_COMBO_7, 0},
    {"SFX_BTL_JUMP_CONTINUE8", W_GEN, 0, PM64_SOUND_JUMP_COMBO_8, 0},
    {"SFX_MARIO_HAMMER_HOLD1", W_GEN, 0, PM64_SOUND_HAMMER_WINDUP, 0},
    {"SFX_MARIO_HAMMER_HOLD2", W_GEN, 0, PM64_SOUND_HAMMER_WINDUP, 0},
    {"SFX_MARIO_HAMMER_HOLD3", W_GEN, 0, PM64_SOUND_HAMMER_WINDUP, 0},
    {"SFX_MARIO_HAMMER_FURU1", W_GEN, 0, PM64_SOUND_HAMMER_SWING_1, 0},
    {"SFX_MARIO_HAMMER_FURU2", W_GEN, 0, PM64_SOUND_HAMMER_SWING_2, 0},
    {"SFX_MARIO_HAMMER_FURU3", W_GEN, 0, PM64_SOUND_HAMMER_SWING_3, 0},
    {"SFX_MARIO_HAMMER_SWING1", W_GEN, 0, PM64_SOUND_HAMMER_SWING_1, 0},
    {"SFX_MARIO_HAMMER_SWING2", W_GEN, 0, PM64_SOUND_HAMMER_SWING_2, 0},
    {"SFX_MARIO_HAMMER_SWING3", W_GEN, 0, PM64_SOUND_HAMMER_SWING_3, 0},
    {"SFX_MARIO_HAMMER_DON1", W_GEN, 0, PM64_SOUND_HAMMER_STRIKE_1, 0},
    {"SFX_MARIO_HAMMER_DON2", W_GEN, 0, PM64_SOUND_HAMMER_STRIKE_2, 0},
    {"SFX_MARIO_HAMMER_DON3", W_GEN, 0, PM64_SOUND_HAMMER_STRIKE_3, 0},
    {"SFX_MARIO_HAMMER_HIT_GROUND1", W_GEN, 0, PM64_SOUND_HAMMER_STRIKE_1, 0},
    {"SFX_MARIO_HAMMER_HIT_GROUND2", W_GEN, 0, PM64_SOUND_HAMMER_STRIKE_2, 0},
    {"SFX_MARIO_HAMMER_HIT_GROUND3", W_GEN, 0, PM64_SOUND_HAMMER_STRIKE_3, 0},
    {"SFX_COIN_GET1", W_GEN, 0, PM64_SOUND_COIN_PICKUP, 0},
    {"SFX_COIN_LEAPED1", W_GEN, 0, PM64_SOUND_COIN_BOUNCE, 0},
    {"SFX_HEART_GET1", W_GEN, 0, PM64_SOUND_HEART_PICKUP, 0},
    {"SFX_HEART_LEAPED1", W_GEN, 0, PM64_SOUND_HEART_BOUNCE, 0},
    {"SFX_FLOWER_GET1", W_GEN, 0, PM64_SOUND_FLOWER_PICKUP, 0},
    {"SFX_STAR_POINT_GET1", W_GEN, 0, PM64_SOUND_STAR_POINT_PICKUP, 0},
    {"SFX_ITEM_LEAPED1", W_GEN, 0, PM64_SOUND_ITEM_BOUNCE, 0},
    {"SFX_BADGE_LEAPED1", W_GEN, 0, PM64_SOUND_BADGE_BOUNCE, 0},
    {"SFX_STAR_PIECE_LEAPED1", W_GEN, 0, PM64_SOUND_STAR_PIECE_BOUNCE, 0},
    {"SFX_ITEM_MARIO_USE1", W_GEN, 0, PM64_SOUND_USE_ITEM, 0},
    {"SFX_ITEM_PARTY_USE1", W_GEN, 0, PM64_SOUND_USE_ITEM, 0},
    {"SFX_MESSAGE_SAVE_OK1", W_GEN, 0, PM64_SOUND_SAVE_CONFIRM, 0},
    {"SFX_CURSOR_MOVE1", W_GEN, 0, PM64_SOUND_MENU_CHANGE_SELECTION, 0},
    {"SFX_CURSOR_MOVE2", W_GEN, 0, PM64_SOUND_MENU_CHANGE_SELECTION, 0},
    {"SFX_CURSOR_OK1", W_GEN, 0, PM64_SOUND_MENU_NEXT, 0},
    {"SFX_CURSOR_CANCEL1", W_GEN, 0, PM64_SOUND_MENU_BACK, 0},
    {"SFX_CURSOR_NOT_CHOICE1", W_GEN, 0, PM64_SOUND_MENU_ERROR, 0},
    {"SFX_BTL_CURSOR_MOVE1", W_GEN, 0, PM64_SOUND_MENU_CHANGE_SELECTION, 0},
    {"SFX_BTL_CURSOR_MOVE2", W_GEN, 0, PM64_SOUND_MENU_CHANGE_SELECTION, 0},
    {"SFX_BTL_CURSOR_OK1", W_GEN, 0, PM64_SOUND_MENU_NEXT, 0},
    {"SFX_BTL_CURSOR_CANCEL1", W_GEN, 0, PM64_SOUND_MENU_BACK, 0},
    {"SFX_FILE_CURSOR_MOVE1", W_GEN, 0, PM64_SOUND_FILE_MENU_MOVE_CURSOR, 0},
    {"SFX_FILE_CURSOR_MOVE2", W_GEN, 0, PM64_SOUND_FILE_MENU_MOVE_CURSOR, 0},
    {"SFX_BADGE_PUT1", W_GEN, 0, PM64_SOUND_MENU_BADGE_EQUIP, 0},
    {"SFX_BADGE_REMOVE1", W_GEN, 0, PM64_SOUND_MENU_BADGE_UNEQUIP, 0},
};
#define MAPS (int)(sizeof(kMap) / sizeof(kMap[0]))

/* the battle's partner unit's host (TTYD PartyId), 0 none */
static int battleHost(const BattleUnit* unit) {
    static const u8 kHost[7] = {1, 2, 4, 5, 6, 3, 7}; /* UNIT_GOOMBELLA .. UNIT_MS_MOWZ */
    if (unit == NULL || unit->currentKind < UNIT_GOOMBELLA || unit->currentKind > UNIT_MS_MOWZ) {
        return 0;
    }
    return kHost[unit->currentKind - UNIT_GOOMBELLA];
}

/* the PM64 partner out now: in battle on the partner unit (acting: only while it's the acting unit) */
static int partnerNow(int acting) {
    if (gp == NULL) {
        return 0;
    }
    if (gp->inBattle) {
        BattleWork* wp = _battleWorkPointer;
        const BattleUnit* party;
        if (wp == NULL || (party = BattleGetPartyPtr(wp)) == NULL) {
            return 0;
        }
        if (acting && BattleGetUnitPtr(wp, wp->activeUnitId) != party) {
            return 0;
        }
        return portPm64PartnerOn(battleHost(party));
    }
    return portPm64PartnerOut();
}

/* a PM64 enemy fighting as unit type host is in the battle */
static int enemyIn(int host) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (gp == NULL || !gp->inBattle || wp == NULL) {
        return 0;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* unit = BattleGetUnitPtr(wp, i);
        const PortEnemyVariant* v;
        if (unit != NULL && unit->alliance == 1 && (int)unit->currentKind == host &&
            (v = portEnemyVariantOf(unit)) != NULL && v->pm64 > 0) {
            return 1;
        }
    }
    return 0;
}

/* name's PM64 sound now (index into kMap, or -1); forPartner >= 0: as if that partner were out and acting */
static int pick(const char* name, int forPartner) {
    int i, out = -1, act = -1;
    int partners = forPartner >= 0 || portPm64SfxActive(), general = forPartner >= 0 || portPm64SfxGeneralActive();
    if (name == NULL || strncmp(name, "SFX_", 4) != 0) {
        return -1;
    }
    for (i = 0; i < MAPS; i++) {
        const SfxMap* m = &kMap[i];
        int yes = 0;
        if (strcmp(m->name, name) != 0 || !(m->who == W_GEN ? general : partners)) {
            continue;
        }
        switch (m->who) {
        case W_GEN:
            yes = 1;
            break;
        case W_OWN:
            yes = forPartner >= 0 ? forPartner == m->arg
                                  : portPm64PartnerOn(portPm64PartnerHost(m->arg)) == m->arg;
            break;
        case W_OUT:
            if (out < 0) {
                out = forPartner >= 0 ? forPartner : partnerNow(0);
            }
            yes = out == m->arg;
            break;
        case W_ACT:
            if (act < 0) {
                act = forPartner >= 0 ? forPartner : partnerNow(1);
            }
            yes = act == m->arg;
            break;
        case W_ENEMY:
            yes = forPartner < 0 && enemyIn(m->arg);
            break;
        }
        if (yes) {
            return i;
        }
    }
    return -1;
}

unsigned portPm64SfxLookup(const char* name, int pm64Partner) {
    const int i = pick(name, pm64Partner);
    return i >= 0 ? kMap[i].sound : 0;
}

/* ---- by name: the decomp's SOUND_ names (port/include/port/pm64_sound_ids.h) ---- */

typedef struct SoundName {
    const char* name; /* without "SOUND_" */
    u16 id;
} SoundName;

static const SoundName kNames[] = {
#define PM64_SOUND_ROW(n, v) {#n, v},
    PM64_SOUND_LIST(PM64_SOUND_ROW)
#undef PM64_SOUND_ROW
};
#define NAMES (int)(sizeof(kNames) / sizeof(kNames[0]))

/* name's id: "SOUND_X", "PM64_SOUND_X" or "X", up to len characters (len < 0: all); 0 unknown */
static unsigned idOf(const char* name, int len) {
    int lo = 0, hi = NAMES - 1;
    if (name == NULL) {
        return 0;
    }
    if (len < 0) {
        len = (int)strlen(name);
    }
    if (len > 5 && strncmp(name, "PM64_", 5) == 0) {
        name += 5, len -= 5;
    }
    if (len > 6 && strncmp(name, "SOUND_", 6) == 0) {
        name += 6, len -= 6;
    }
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        int c = strncmp(kNames[mid].name, name, (size_t)len);
        if (c == 0 && kNames[mid].name[len] != '\0') {
            c = 1;
        }
        if (c == 0) {
            return kNames[mid].id;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}

unsigned portPm64SfxId(const char* name) {
    return idOf(name, -1);
}

const char* portPm64SfxName(unsigned id) {
    int i;
    for (i = 0; i < NAMES; i++) {
        if (kNames[i].id == id && strncmp(kNames[i].name, "LOOP_", 5) != 0) {
            return kNames[i].name;
        }
    }
    return NULL;
}

/* The game thread's "SOUND_X|SFX_Y" name: the PM64 sound for the TTYD sound list's entry it found, until the
 * next name looked up (a packed numeric id keeps it: evt_npc.c looks the name up, then plays the number). */
static int sNamedIndex = -1;
static u16 sNamedSound;

const char* portPm64SfxSplit(const char* name, unsigned* pm64) {
    const char* bar;
    *pm64 = 0;
    if (name == NULL || name[0] != 'S' || strncmp(name, "SOUND_", 6) != 0 || (bar = strchr(name, '|')) == NULL) {
        return name;
    }
    *pm64 = idOf(name, (int)(bar - name));
    return bar + 1;
}

void portPm64SfxNamed(int index, unsigned pm64) {
    sNamedIndex = pm64 != 0 ? index : -1;
    sNamedSound = (u16)pm64;
}

static int sLog = -1;

void portPm64SfxArm(int slot, unsigned serial, const char* name, int index) {
    int i = -1, ready;
    u16 sound = 0, cap = 0;
    if (slot < 0 || slot >= SLOTS || portPm64Linked() == PM64_NONE) {
        return;
    }
    if (index >= 0 && index == sNamedIndex && portPm64SfxActive()) { /* a writer's "SOUND_X|SFX_Y" */
        sound = sNamedSound;
    } else if ((i = pick(name, -1)) >= 0) {
        sound = kMap[i].sound;
        cap = kMap[i].cap;
    }
    sNamedIndex = -1;
    ready = sound != 0 && romReady(); /* (before the lock: it may read the segment from disk) */
    LOCK();
    sSlot[slot].serial = serial;
    sSlot[slot].sound = 0;
    sSlot[slot].started = 0;
    if (ready) {
        sSlot[slot].sound = sound;
        sSlot[slot].maxFrames = cap != 0 ? cap : 2 * RATE / FRAME;
    }
    UNLOCK();
    if (sLog < 0) {
        const char* e = getenv("TTYD_PM64_SFX_LOG");
        sLog = e != NULL && e[0] == '1';
    }
    if (sLog && sound != 0) {
        const char* pm = portPm64SfxName(sound);
        printf("[pm64sfx] %s -> 0x%04X %s (slot %d)\n", name, sound, pm != NULL ? pm : "-", slot);
        fflush(stdout);
    }
}

int portPm64SfxCategory(int slot, unsigned serial, int cat) {
    int armed;
    if (slot < 0 || slot >= SLOTS || sRom.ready <= 0) {
        return cat;
    }
    LOCK();
    armed = sSlot[slot].serial == serial && sSlot[slot].sound != 0;
    UNLOCK();
    return armed ? PORT_MIX_SILENT : cat;
}

void portPm64SfxSlot(int slot, unsigned serial, int volume, int pan) {
    if (slot < 0 || slot >= SLOTS || sRom.ready <= 0) {
        return;
    }
    LOCK();
    if (sSlot[slot].serial == serial && sSlot[slot].sound != 0) {
        const u16 sound = sSlot[slot].sound;
        sSlot[slot].vol = volume < 0 ? 0 : volume > 127 ? 127 : volume;
        sSlot[slot].pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
        sSlot[slot].live = 2;
        if (sound != SILENT_ID && !sSlot[slot].started) {
            sSlot[slot].started = 1;
            trySound(sound, slot);
        }
    }
    UNLOCK();
}

void portPm64SfxOff(int slot) {
    int i;
    if (slot < 0 || slot >= SLOTS || sRom.ready <= 0) {
        return;
    }
    LOCK();
    for (i = 0; i < PLAYERS; i++) {
        Player* p = &sPlayer[i];
        if (p->owner == slot && p->serial == sSlot[slot].serial) {
            if (p->v.owned) {
                p->v.flags |= 0x10;
            }
            p->pos = NULL;
            p->owner = -1;
        }
    }
    sSlot[slot].sound = 0;
    sSlot[slot].live = 0;
    UNLOCK();
}

/* Tests ($TTYD_PM64_SFX_DUMP="<folder>|NAME,NAME,..."): once, each named PM64 sound rendered alone (centred, volume
 * 100, until it ends or 4 s) to <folder>/<NAME>.wav, its length logged. The live players are put back after. */
static void writeWav(const char* path, const s16* pcm, u32 frames) {
    FILE* f = portFopen(path, "wb");
    u8 h[44];
    const u32 bytes = frames * 4;
    if (f == NULL) {
        return;
    }
    memcpy(h, "RIFF", 4);
    h[4] = (u8)(bytes + 36), h[5] = (u8)((bytes + 36) >> 8), h[6] = (u8)((bytes + 36) >> 16), h[7] = (u8)((bytes + 36) >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16, h[17] = h[18] = h[19] = 0;
    h[20] = 1, h[21] = 0, h[22] = 2, h[23] = 0;                            /* PCM, stereo */
    h[24] = (u8)RATE, h[25] = (u8)(RATE >> 8), h[26] = h[27] = 0;          /* 32000 Hz */
    h[28] = (u8)(RATE * 4), h[29] = (u8)((RATE * 4) >> 8), h[30] = (u8)((RATE * 4) >> 16), h[31] = 0;
    h[32] = 4, h[33] = 0, h[34] = 16, h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (u8)bytes, h[41] = (u8)(bytes >> 8), h[42] = (u8)(bytes >> 16), h[43] = (u8)(bytes >> 24);
    fwrite(h, 1, 44, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
}

static void dumpSounds(void) {
    static int sDone;
    static Player sSave[PLAYERS];
    static s16 sPcm[4 * RATE * 2];
    const char* e = getenv("TTYD_PM64_SFX_DUMP");
    const char* bar;
    char dir[512], name[96], path[640];
    const char* at;
    if (sDone || e == NULL || (bar = strchr(e, '|')) == NULL || (size_t)(bar - e) >= sizeof(dir) || sRom.ready <= 0) {
        return;
    }
    sDone = 1;
    memcpy(dir, e, (size_t)(bar - e));
    dir[bar - e] = '\0';
    memcpy(sSave, sPlayer, sizeof(sPlayer));
    for (at = bar + 1; *at != '\0';) {
        const char* end = strchr(at, ',');
        const size_t n = end != NULL ? (size_t)(end - at) : strlen(at);
        unsigned id;
        u32 frames = 0, last = 0;
        int i, k, peak = 0, crossings = 0;
        if (n > 0 && n < sizeof(name)) {
            memcpy(name, at, n);
            name[n] = '\0';
            id = portPm64SfxId(name);
            for (i = 0; i < PLAYERS; i++) {
                memset(&sPlayer[i], 0, sizeof(sPlayer[i]));
                sPlayer[i].owner = -1;
            }
            if (id != 0) {
                trySound((u16)id, -1);
                while (frames + FRAME <= 4 * RATE) {
                    float mix[FRAME * 2];
                    int any = 0;
                    for (i = 0; i < PLAYERS; i++) {
                        any |= sPlayer[i].pos != NULL || sPlayer[i].v.owned;
                    }
                    if (!any) {
                        break;
                    }
                    memset(mix, 0, sizeof(mix));
                    audioFrame(mix);
                    for (k = 0; k < FRAME * 2; k++) { /* the gain portPm64SfxMix gives it at full volume */
                        const float y = mix[k] * 1.6f;
                        const s16 s = (s16)(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : (int)y);
                        const u32 t = frames + (u32)k / 2;
                        const int a = s < 0 ? -s : s;
                        sPcm[t * 2 + (k & 1)] = s;
                        if (k & 1) {
                            continue;
                        }
                        if (a > peak) {
                            peak = a;
                        }
                        if (a > 64) {
                            last = t + 1;
                        }
                        if (t > 0 && (s < 0) != (sPcm[(t - 1) * 2] < 0)) {
                            crossings++;
                        }
                    }
                    frames += FRAME;
                }
                snprintf(path, sizeof(path), "%s/%s.wav", dir, name);
                writeWav(path, sPcm, frames);
            }
            printf("[pm64sfx] dump %s 0x%04X: %u frames (%.3f s, sound for %.3f s), peak %d, ~%d Hz\n", name, id, frames,
                   (double)frames / RATE, (double)last / RATE, peak, frames > 0 ? (int)((long long)crossings * RATE / 2 / frames) : 0);
            fflush(stdout);
        }
        if (end == NULL) {
            break;
        }
        at = end + 1;
    }
    memcpy(sPlayer, sSave, sizeof(sPlayer));
}

void portPm64SfxFrame(void) {
    int i;
    if (portPm64Linked() != PM64_NONE && getenv("TTYD_PM64_SFX_DUMP") != NULL && romReady()) {
        LOCK();
        dumpSounds();
        UNLOCK();
    }
    if (sRom.ready == 0) {
        return;
    }
    if (sRom.generation != portPm64Generation() || portPm64Linked() == PM64_NONE) {
        romDrop();
        LOCK();
        memset(sSlot, 0, sizeof(sSlot));
    } else {
        LOCK();
        for (i = 0; i < SLOTS; i++) { /* live: seen this frame (2), then 1 until the next */
            if (sSlot[i].live > 0) {
                sSlot[i].live--;
            }
        }
    }
    UNLOCK();
}

void portPm64SfxPlay(unsigned soundId, int volume) {
    int i;
    if (!portPm64SfxActive() || !romReady()) { /* (before the lock: it may read the segment from disk) */
        return;
    }
    LOCK();
    trySound((u16)soundId, -1);
    for (i = 0; i < PLAYERS; i++) {
        if (sPlayer[i].owner < 0 && sPlayer[i].soundId == (u16)soundId && sPlayer[i].pos != NULL) {
            sPlayer[i].fixedVol = volume < 0 ? 0 : volume > 127 ? 127 : volume;
        }
    }
    UNLOCK();
}

int portPm64SfxPlayName(const char* name, int volume) {
    const unsigned id = portPm64SfxId(name);
    int ok;
    if (id == 0 || !portPm64SfxActive()) {
        return 0;
    }
    ok = romReady();
    if (ok) {
        portPm64SfxPlay(id, volume);
    }
    return ok;
}

void portPm64SfxGreet(int p) {
    /* Goombario's jump, Kooper's shell, Bombette's fuse, Parakarry's wings, Watt's charge, Sushie's splash,
     * Lakilester's cloud, Bow appearing */
    static const u16 kGreet[9] = {0, S_GOOMBARIO_JUMP, S_KOOPER_SHELL_KICK, S_BOMBETTE_FUSE, S_PARAKARRY_FLAP,
                                  S_WATT_CHARGE, S_SUSHIE_EMBARK, S_LAKILESTER_MAKE_CLOUD_NINE, PM64_SOUND_BOW_APPEAR};
    if (p > 0 && p < 9) {
        portPm64SfxPlay(kGreet[p], 100);
    }
}

void portPm64SfxStarSpirit(void) {
    portPm64SfxPlay(S_STAR_SPIRIT_APPEAR_A, 100);
}

void portPm64SfxMix(short* out, unsigned frames) {
    static float sMix[FRAME * 2];
    static int sAt = FRAME;
    static float sLim = 1.0f;
    float gain;
    unsigned n;
    int i;
    if (sRom.ready <= 0 || sSfxVolume == NULL || sSfxMute == NULL) {
        return; /* (the settings are declared on the game thread before anything loads) */
    }
    /* The level: PM64's effects brought to the game's own (measured: the captures in docs/PM64_LINK.md's
     * notes), times the settings page's sound effect volume. */
    gain = 1.6f * portVolumeGain(*sSfxMute ? 0 : *sSfxVolume);
    LOCK();
    for (n = 0; n < frames; n++) {
        if (sAt >= FRAME) {
            int any = 0;
            for (i = 0; i < PLAYERS; i++) {
                any |= sPlayer[i].pos != NULL || sPlayer[i].v.owned;
            }
            memset(sMix, 0, sizeof(sMix));
            if (any) {
                audioFrame(sMix);
            }
            sAt = 0;
        }
        for (i = 0; i < 2; i++) {
            /* added to the game's mix under a peak limiter (fast down, about 80 ms back up), never clipping */
            const float add = sMix[sAt * 2 + i] * gain;
            const float sum = (float)out[n * 2 + i] + add * sLim;
            const float a = sum < 0.0f ? -sum : sum;
            if (a > 32000.0f && add != 0.0f) {
                const float room = 32000.0f - ((float)out[n * 2 + i] < 0.0f ? -(float)out[n * 2 + i] : (float)out[n * 2 + i]);
                const float add_a = add < 0.0f ? -add : add;
                const float want = room > 0.0f ? room / add_a : 0.0f;
                if (want < sLim) {
                    sLim = want;
                }
            }
            {
                const float y = (float)out[n * 2 + i] + add * sLim;
                out[n * 2 + i] = (short)(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : (int)y);
            }
        }
        sLim += (1.0f - sLim) * 0.0004f;
        sAt++;
    }
    UNLOCK();
}
