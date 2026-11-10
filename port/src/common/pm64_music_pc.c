/* The Paper Mario 64 soundtrack (port/include/port/pm64_music.h): a player of
 * our own for the N64 game's BGM sequences over the SBN archive in the linked
 * ROM's cache. What it reads, as the PM64 decomp (pmret/papermario) documents
 * the formats:
 *  - SBN: "SBN ", then at 0x10 the file table's offset, at 0x14 its count, at
 *    0x24 the INIT file's offset; table entries are (offset, format << 24 |
 *    size), offsets from the SBN's start.
 *  - INIT: offsets/sizes (from INIT's start) of the bank list (file, bank,
 *    bank set; 0xFFFF ended), the song list (BGM file, three BK files) and the
 *    extra list (SEF, PER, PRG files).
 *  - BGM: compositions (u32 commands: phrase, loop labels and ends), phrases
 *    of 16 track words, tracks of byte commands (delays, notes, 0xE0..0xFF
 *    commands), drum and instrument tables.
 *  - BK: 16 instrument offsets; an instrument: VADPCM (or 16-bit) samples,
 *    loop, codebook, key base, sample rate, envelope presets (press and release
 *    scripts of time-index, volume pairs).
 * Timing, voice allocation, envelopes and volume follow the N64 engine: an
 * audio frame of 184 samples at 32 kHz, at most one sequence tick a frame.
 * Paper Cut's own code under its MIT licence. */
#include "port/pm64_music.h"
#include "port/pm64.h"
#include "port/settings.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RATE 32000
#define FRAME 184            /* samples an audio frame */
#define FRAME_USEC 5750      /* its length in microseconds */
#define MFRAMES_PER_MIN 10434782u /* 1000 x audio frames a minute */
#define DEFAULT_STEP 156250u /* the update step before the first tempo */
#define TRACKS 16
#define VOICES 24
#define MAX_INSTS 192

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

/* envelope step times in microseconds, by index (0x00..0x7F; past 0x5F: 0) */
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

static const u8 kPressDefault[] = {61, 127, 0xFF, 0};   /* 280 ms to full */
static const u8 kReleaseDefault[] = {52, 0, 0xFF, 0};   /* 550 ms to silence */
static const s8 kTicksPerBeat[8] = {48, 24, 32, 40, 48, 56, 64, 48};
static const s8 kArgCount[32] = {2, 1, 1, 1, 4, 3, 2, 0, 2, 1, 1, 1, 1, 1, 1, 2,
                                 3, 1, 1, 0, 2, 1, 3, 1, 0, 0, 0, 0, 3, 3, 3, 3};
static const u8 kCustomEnv[40] = {0x5E, 0x5D, 0x5C, 0x5B, 0x5A, 0x58, 0x56, 0x53, 0x51, 0x4F, 0x4A, 0x45, 0x40, 0x3B,
                                  0x37, 0x35, 0x33, 0x31, 0x2F, 0x2D, 0x2B, 0x29, 0x27, 0x26, 0x25, 0x23, 0x21, 0x20,
                                  0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x19, 0x18, 0x17, 0x16, 0x15, 0x14};
static const u8 kPolyphony[8] = {0, 1, 0, 0, 0, 2, 3, 4};

/* ---- instruments: decoded to 16-bit PCM once per song ---- */

typedef struct Inst {
    const u8* src;  /* its 0x30-byte record in the SBN (the cache key) */
    const u8* bk;   /* its BK file */
    s16* pcm;
    s32 length;     /* samples */
    s32 loopStart, loopEnd, loopCount;
    s32 keyBase;    /* cents */
    float pitchRatio; /* its sample rate / 32 kHz */
    const u8* env;  /* its envelope presets, or NULL */
} Inst;

typedef struct Voice {
    int busy;            /* a note owns it (the engine's priority) */
    Inst* inst;
    float pitch;
    s32 volume;          /* envelope x client, 0..0x7FFF */
    u8 pan, reverb;
    s32 delta;           /* samples to reach volume */
    const u8* press;
    const u8* release;
    const u8* cmd;       /* NULL: the envelope's done */
    const u8* cmdEnd;    /* the SBN's end when cmd is in it (NULL: a built-in or the song's own, terminated) */
    const u8* loopAt;
    u8 loopCount;
    s32 envDuration, envTimeLeft;
    float envDelta;
    s32 envScale;
    s32 envInitial, envTarget;
    int flags;           /* 1 releasing, 2 handled volume change, 0x10 key released, 0x20 volume changed */
    int relRelease;
    s32 envRelStart;
    s32 clientVolume;
    int donePending;
    int sync;            /* 1 start, 2 pitch, 4 params, 8 pan */
    /* the synth's side */
    int playing;
    double pos;
    s32 loopsLeft;
    float gainL, gainR, wet;   /* now */
    float toL, toR, toWet;     /* the target */
    s32 rampLeft;
} Voice;

typedef struct Note {
    Inst* ins;
    float pitchRatio;
    s32 detune, randDetune;
    s32 volume;
    u8 velocity;
    s32 length;
    int pendingTick;
    u8 tremoloDepth, tremoloDelay, tremoloPhase;
} Note;

typedef struct Track {
    u32 pos;            /* offset in the BGM file, 0: off */
    u32 saved, prev;
    Inst* inst;
    const u8* press;
    const u8* release;
    s32 insVolume;      /* 8.24 */
    s32 insVolumeStep, insVolumeTarget, insVolumeTicks;
    s32 proxVolume;
    u8 insPan, insReverb;
    s32 coarse, fine, detune;
    u8 volume;
    int pressOverride;
    u8 tremoloDepth, tremoloDelay, tremoloRate;
    u8 randomPan;
    int drum;
    int detourLength;
    s32 delay;
    int firstVoice, lastVoice;
    int muted;
    int changedVolume, changedTune, changedPan;
} Track;

struct Pm64Song {
    const u8* sbn;
    size_t sbnSize;
    const u8* bgm;
    u32 bgmSize;
    /* banks: set index 0..7 -> 16 bank slots -> a BK file */
    const u8* bank[8][16];
    const u8* per;     /* PER drums (72) */
    u32 perCount;
    const u8* prg;     /* PRG instruments (64) */
    u32 prgCount;
    const u8* drums;   /* the BGM's */
    int drumCount;
    const u8* insts;   /* the BGM's instrument table */
    int instCount;
    Inst inst[MAX_INSTS];
    int instUsed;
    /* the player */
    u32 compStart, compPos, phraseStart;
    u32 loopLabel[32];
    u32 loopEnd[5];
    u8 loopCounter[5];
    int loopDepth;
    int state; /* 0 fetch, 1 active, 2 stopped */
    s32 masterVolume, masterVolumeStep, masterVolumeTarget, masterVolumeTicks;
    s32 masterTempo, masterTempoStep, masterTempoTarget, masterTempoTicks;
    s32 masterPitchShift;
    u32 tickInterval, step;
    s32 counter;
    u32 maxTempo;
    u32 rand1, rand2, frameCounter, playing;
    u16 customEnv[3][10];
    u8 customBytes[3][20]; /* as the voices read them (customEnvBytes): this song's own */
    int customWrite[3];
    int writingEnv;
    u8 args[4];
    Track track[TRACKS];
    Note note[VOICES];
    Voice voice[VOICES];
    /* rendering */
    short frame[FRAME * 2];
    int frameAt;
    int ended;
    /* the reverb: four combs, two all-passes */
    float comb[4][1700];
    int combLen[4], combAt[4];
    float combLp[4];
    float ap[2][600];
    int apLen[2], apAt[2];
    /* the output limiter's gain (1 = none; see synthFrame) */
    float limGain;
};

/* ---- VADPCM: 9-byte frames of 16 samples (a scale/predictor byte, 8 bytes
 * of nibbles); order-2 codebook, 8 x 2 coefficients a predictor ---- */

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

/* (as offsets, never p + n: a damaged cache's n or offset can't wrap the test) */
static int inRange(const Pm64Song* s, const u8* p, size_t n) {
    const uintptr_t at = (uintptr_t)p - (uintptr_t)s->sbn;
    return (uintptr_t)p >= (uintptr_t)s->sbn && at <= s->sbnSize && n <= s->sbnSize - at;
}

/* instrument ins (0..15) of BK file bk: decoded on first use */
static Inst* instOf(Pm64Song* s, const u8* bk, int ins) {
    const u8* r;
    u32 off, wav, len, book, env;
    int i, type, codebookSize;
    Inst* in;
    if (bk == NULL || ins < 0 || ins > 15 || !inRange(s, bk, 0x40)) {
        return NULL;
    }
    off = rd16(bk + 0x12 + ins * 2);
    if (off == 0 || !inRange(s, bk + off, 0x30)) {
        return NULL;
    }
    r = bk + off;
    for (i = 0; i < s->instUsed; i++) {
        if (s->inst[i].src == r) {
            return s->inst[i].pcm != NULL ? &s->inst[i] : NULL;
        }
    }
    if (s->instUsed >= MAX_INSTS) {
        return NULL;
    }
    in = &s->inst[s->instUsed++];
    memset(in, 0, sizeof(*in));
    in->src = r;
    in->bk = bk;
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
    in->env = env != 0 && inRange(s, bk + env, 8) ? bk + env : NULL;
    if (len == 0 || !inRange(s, bk + wav, len)) {
        return NULL;
    }
    if (type == 0) { /* VADPCM */
        const s32 frames = (s32)(len / 9);
        if (book == 0 || codebookSize < 32 || !inRange(s, bk + book, (size_t)codebookSize)) {
            return NULL;
        }
        in->length = frames * 16;
        in->pcm = (s16*)malloc((size_t)in->length * sizeof(s16) + 2);
        if (in->pcm == NULL) {
            return NULL;
        }
        vadpcmDecode(bk + wav, frames, bk + book, codebookSize / 32, in->pcm);
    } else { /* 16-bit big-endian */
        s32 k;
        in->length = (s32)(len / 2);
        in->pcm = (s16*)malloc((size_t)in->length * sizeof(s16) + 2);
        if (in->pcm == NULL) {
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

/* the engine's instrument lookup: bank byte (bits 4-6 the bank set, bits
 * 0-1 the envelope preset), patch (bank << 4 | instrument) */
static Inst* instrument(Pm64Song* s, int bank, int patch, const u8** press, const u8** release) {
    const int set = (bank & 0x70) >> 4, envIdx = bank & 3;
    Inst* in = instOf(s, s->bank[set][(patch >> 4) & 15], patch & 15);
    *press = kPressDefault;
    *release = kReleaseDefault;
    if (in != NULL && in->env != NULL && envIdx < in->env[0] && inRange(s, in->env + 4 + envIdx * 4, 4)) {
        const u8* p = in->env + rd16(in->env + 4 + envIdx * 4);
        const u8* q = in->env + rd16(in->env + 6 + envIdx * 4);
        if (inRange(s, p, 2) && inRange(s, q, 2)) {
            *press = p;
            *release = q;
        }
    }
    return in;
}

/* ---- the SBN ---- */

static const u8* sbnFile(const Pm64Song* s, u32 index, int format, u32* size) {
    const u8* t;
    u32 count, off, data;
    if (s->sbnSize < 0x40 || memcmp(s->sbn, "SBN ", 4) != 0) {
        return NULL;
    }
    count = rd32(s->sbn + 0x14);
    t = s->sbn + rd32(s->sbn + 0x10);
    if (index >= count || !inRange(s, t + index * 8, 8)) {
        return NULL;
    }
    off = rd32(t + index * 8) & 0xFFFFFF;
    data = rd32(t + index * 8 + 4);
    if ((int)(data >> 24) != format || off == 0 || !inRange(s, s->sbn + off, data & 0xFFFFFF)) {
        return NULL;
    }
    if (size != NULL) {
        *size = data & 0xFFFFFF;
    }
    return s->sbn + off;
}

static int setOf(int bankSet) {
    /* the INIT list's bank sets (1 aux .. 6) to the patch's set indices */
    switch (bankSet) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 3:
        return 3;
    case 4:
        return 4;
    case 5:
        return 5;
    case 6:
        return 6;
    default:
        return -1;
    }
}

static int loadInit(Pm64Song* s, int songId) {
    const u8* init;
    u32 io, bankList, bankSize, songList, songSize, extra, i, size;
    const u8* song;
    if (s->sbnSize < 0x40 || (io = rd32(s->sbn + 0x24)) == 0 || !inRange(s, s->sbn + io, 0x20)) {
        return 0;
    }
    init = s->sbn + io;
    bankList = rd16(init + 8);
    bankSize = rd16(init + 0x0A);
    songList = rd16(init + 0x0C);
    songSize = rd16(init + 0x0E);
    extra = rd16(init + 0x10);
    if (songId < 0 || (u32)(songId + 1) * 8 > songSize || !inRange(s, init + songList + songId * 8, 8)) {
        return 0;
    }
    /* banks loaded at start */
    for (i = 0; i + 4 <= bankSize && i < 80 * 4; i += 4) {
        const u8* e = init + bankList + i;
        const u8* bk;
        int set;
        if (!inRange(s, e, 4) || rd16(e) == 0xFFFF) {
            break;
        }
        set = setOf(e[3]);
        if (set >= 0 && e[2] < 16 && (bk = sbnFile(s, rd16(e), 0x30, NULL)) != NULL) {
            s->bank[set][e[2]] = bk;
            if (set == 0) {
                s->bank[7][e[2]] = bk;
            }
        }
    }
    song = init + songList + songId * 8;
    if (rd16(song) == 0xFFFF || (s->bgm = sbnFile(s, rd16(song), 0x10, &s->bgmSize)) == NULL) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        const u8* bk = rd16(song + 2 + i * 2) != 0 ? sbnFile(s, rd16(song + 2 + i * 2), 0x30, NULL) : NULL;
        if (bk != NULL) {
            s->bank[0][i] = bk;
            s->bank[7][i] = bk;
        }
    }
    /* PER and PRG: the extra list's second and third */
    if (inRange(s, init + extra, 6)) {
        const u8* f;
        if ((f = sbnFile(s, rd16(init + extra + 2), 0x40, &size)) != NULL && size > 0x10) {
            s->per = f + 0x10;
            s->perCount = (rd32(f + 4) > 0x10 ? rd32(f + 4) - 0x10 : 0) / 12;
            if (s->perCount > 72) {
                s->perCount = 72;
            }
        }
        if ((f = sbnFile(s, rd16(init + extra + 4), 0x40, &size)) != NULL && size > 0x10) {
            s->prg = f + 0x10;
            s->prgCount = (rd32(f + 4) > 0x10 ? rd32(f + 4) - 0x10 : 0) / 8;
            if (s->prgCount > 64) {
                s->prgCount = 64;
            }
        }
    }
    return 1;
}

/* ---- the player ---- */

static const u8* at(const Pm64Song* s, u32 off, u32 n) {
    return off != 0 && off + n <= s->bgmSize ? s->bgm + off : NULL;
}

static void setTicks(Pm64Song* s, u32 tpb) {
    u32 interval = MFRAMES_PER_MIN / (tpb != 0 ? tpb : 48);
    u32 step = DEFAULT_STEP;
    if (interval > 500000) {
        interval = 500000;
    } else if (interval < 80000) {
        interval = 80000;
    }
    if (interval < step) {
        step = interval;
    }
    s->step = step;
    s->tickInterval = interval;
    s->counter = (s32)interval;
    s->maxTempo = interval / 1000;
}

static s32 bpmToTempo(const Pm64Song* s, u32 bpm) {
    if (bpm > s->maxTempo) {
        bpm = s->maxTempo;
    } else if (bpm == 0) {
        bpm = 1;
    }
    return (s32)bpm * 100;
}

static void resetVoice(Voice* v) {
    v->cmd = NULL;
    v->donePending = 1;
    v->sync = 0;
    v->toL = v->toR = v->toWet = 0.0f;
    v->rampLeft = FRAME;
}

static void initPlayer(Pm64Song* s) {
    int i;
    u32 p;
    for (i = 0; i < TRACKS; i++) {
        Track* t = &s->track[i];
        memset(t, 0, sizeof(*t));
        t->insVolume = 0x7FFF << 16;
        t->insPan = 0x40;
        t->volume = 0x7F;
        t->proxVolume = 0x7FFF << 16;
        t->press = kPressDefault;
        t->release = kReleaseDefault;
    }
    for (i = 0; i < VOICES; i++) {
        memset(&s->note[i], 0, sizeof(Note));
        s->note[i].pitchRatio = 2.0f;
    }
    s->masterTempo = 156 * 100;
    s->masterVolume = 0x7F << 24;
    s->masterPitchShift = 0;
    s->masterTempoTicks = s->masterVolumeTicks = 0;
    s->loopDepth = 0;
    memset(s->loopEnd, 0, sizeof(s->loopEnd));
    memset(s->loopCounter, 0, sizeof(s->loopCounter));
    for (i = 0; i < 3; i++) {
        int k;
        for (k = 0; k < 9; k++) {
            s->customEnv[i][k] = 0xFF00;
        }
        s->customWrite[i] = 0;
    }
    for (i = 0; i < 32; i++) {
        s->loopLabel[i] = s->compStart;
    }
    for (p = s->compStart; at(s, p, 4) != NULL; p += 4) {
        const u32 cmd = rd32(s->bgm + p);
        if (cmd == 0) {
            break;
        }
        if ((cmd >> 28) == 3) {
            s->loopLabel[cmd & 0x1F] = p + 4;
        }
    }
    s->compPos = s->compStart;
    s->state = 0;
}

static void endCompLoop(Pm64Song* s, u32 cmd) {
    const int label = cmd & 0x1F, count = (cmd >> 5) & 0x7F;
    int d = s->loopDepth;
    if (s->loopEnd[d] != 0) {
        if (s->loopEnd[d] == s->compPos) {
            if (s->loopCounter[d] != 0) {
                if (--s->loopCounter[d] == 0) {
                    s->loopEnd[d] = 0;
                    if (d > 0) {
                        d--;
                    }
                } else {
                    s->compPos = s->loopLabel[label];
                }
            } else {
                s->compPos = s->loopLabel[label];
            }
        } else if (d < 4) {
            d++;
            s->loopEnd[d] = s->compPos;
            s->loopCounter[d] = (u8)count;
            s->compPos = s->loopLabel[label];
        }
    } else {
        s->loopEnd[d] = s->compPos;
        s->loopCounter[d] = (u8)count;
        s->compPos = s->loopLabel[label];
    }
    s->loopDepth = d;
}

static void loadPhrase(Pm64Song* s, u32 cmd) {
    int i, cur = 0;
    s->phraseStart = s->compStart + ((cmd & 0xFFFF) << 2);
    for (i = 0; i < TRACKS; i++) {
        Track* t = &s->track[i];
        const u8* w = at(s, s->phraseStart + i * 4, 4);
        const u32 info = w != NULL ? rd32(w) : 0;
        t->pos = info >> 16;
        if (t->pos == 0) {
            continue;
        }
        if (info & 0x100) {
            t->pos = 0;
            continue;
        }
        t->drum = (info >> 7) & 1;
        if (((info >> 9) & 0xF) != 0) {
            /* a linked track (the proximity mix's other half): stays silent */
            t->pos = 0;
            continue;
        }
        t->firstVoice = cur;
        cur += kPolyphony[(info >> 13) & 7];
        if (cur > VOICES) {
            cur = VOICES;
        }
        t->lastVoice = cur;
        t->pos += s->phraseStart;
        t->delay = 1;
    }
}

static int readComposition(Pm64Song* s) {
    int guard = 0;
    s->masterTempoStep = 0;
    s->masterTempoTicks = 0;
    while (guard++ < 256) {
        const u8* w = at(s, s->compPos, 4);
        u32 cmd;
        if (w == NULL) {
            s->state = 2;
            return 0;
        }
        cmd = rd32(w);
        s->compPos += 4;
        if (cmd == 0) {
            s->state = 2;
            return 0;
        }
        switch (cmd >> 28) {
        case 1:
            loadPhrase(s, cmd);
            s->state = 1;
            return 1;
        case 5:
            endCompLoop(s, cmd);
            break;
        case 6: /* conditional loops: their condition is never set */
            endCompLoop(s, cmd);
            break;
        default:
            break;
        }
    }
    s->state = 2;
    return 0;
}

static u8 readByte(Pm64Song* s, Track* t) {
    const u8* p = at(s, t->pos, 1);
    const u8 b = p != NULL ? *p : 0;
    t->pos++;
    if (t->detourLength != 0 && --t->detourLength == 0) {
        t->pos = t->saved;
    }
    return b;
}

static float pitchOf(s32 cents) {
    return (float)pow(2.0, cents / 1200.0);
}

static s32 clientVolume(const Pm64Song* s, const Track* t, s32 mul, int shift) {
    const long long a = ((long long)(s->masterVolume >> 21) * (t->insVolume >> 21) * (t->proxVolume >> 21)) >> 20;
    long long v = (a * t->volume * mul) >> shift;
    return (s32)(v > 0x7FFF ? 0x7FFF : v < 0 ? 0 : v);
}

static u8 randomPan(const Pm64Song* s, u8 pan, u8 amp) {
    const s32 seed = (s32)s->rand1;
    const s32 r = ((seed >> 8) & 0x3F) + ((seed << 4) & 0xC0);
    s32 p = ((seed >> 7) + (seed >> 2)) & 1 ? pan + ((amp * r) >> 8) : pan - ((amp * r) >> 8);
    return (u8)(p < 0 ? 0 : p > 127 ? 127 : p);
}

static void command(Pm64Song* s, Track* t, u8 op) {
    const u8* a = s->args;
    switch (op) {
    case 0xE0: /* tempo */
        s->masterTempo = bpmToTempo(s, rd16(a));
        s->step = (u32)s->masterTempo * 10;
        s->masterTempoTicks = 0;
        break;
    case 0xE1: /* master volume */
        s->masterVolume = (a[0] & 0x7F) << 24;
        s->masterVolumeTicks = 0;
        t->changedVolume = 1;
        break;
    case 0xE2:
        s->masterPitchShift = (s8)a[0] * 100;
        break;
    case 0xE4: { /* tempo fade */
        s32 time = rd16(a);
        const s32 tempo = bpmToTempo(s, rd16(a + 2));
        if (time <= 0) {
            time = 1;
        }
        s->masterTempoTicks = time;
        s->masterTempoTarget = tempo;
        s->masterTempoStep = (tempo - s->masterTempo) / time;
        break;
    }
    case 0xE5: { /* master volume fade */
        s32 time = rd16(a);
        const s32 vol = (a[2] & 0x7F) << 24;
        if (time <= 0) {
            time = 1;
        }
        s->masterVolumeTicks = time;
        s->masterVolumeTarget = vol;
        s->masterVolumeStep = (vol - s->masterVolume) / time;
        break;
    }
    case 0xE8: /* override patch: bank, patch */
        t->inst = instrument(s, a[0], a[1], &t->press, &t->release);
        break;
    case 0xE9:
        t->insVolume = (a[0] & 0x7F) << 24;
        t->changedVolume = 1;
        break;
    case 0xEA:
        t->insPan = a[0] & 0x7F;
        t->randomPan = 0;
        t->changedPan = 1;
        break;
    case 0xEB:
        t->insReverb = a[0] & 0x7F;
        t->changedPan = 1;
        break;
    case 0xEC:
        t->volume = a[0] & 0x7F;
        t->changedVolume = 1;
        break;
    case 0xED:
        t->coarse = (s8)a[0] * 100;
        break;
    case 0xEE:
        t->fine = (s8)a[0];
        break;
    case 0xEF:
        t->detune = (s16)rd16(a);
        t->changedTune = 1;
        break;
    case 0xF0:
        t->tremoloDelay = a[0];
        t->tremoloRate = a[1];
        t->tremoloDepth = a[2];
        break;
    case 0xF1:
        t->tremoloRate = a[0];
        break;
    case 0xF2:
        t->tremoloDepth = a[0];
        break;
    case 0xF3:
        t->tremoloDepth = 0;
        break;
    case 0xF4:
        t->insPan = a[0] & 0x7F;
        t->randomPan = a[1] & 0x7F;
        break;
    case 0xF5: { /* use instrument: the BGM's (< 0x80) or the PRG's */
        const u8* info = NULL;
        u32 idx = a[0];
        s32 vol;
        if (idx < 0x80) {
            if ((int)idx < s->instCount) {
                info = s->insts + idx * 8;
            }
        } else if (idx - 0x80 < s->prgCount) {
            info = s->prg + (idx - 0x80) * 8;
        }
        if (info == NULL || !inRange(s, info, 8)) {
            t->inst = NULL;
            break;
        }
        t->inst = instrument(s, info[0], info[1], &t->press, &t->release);
        vol = info[2] & 0x7F;
        t->insVolume = vol << 24;
        t->insPan = info[3] & 0x7F;
        t->insReverb = info[4] & 0x7F;
        t->coarse = (s8)info[5] * 100;
        t->fine = (s8)info[6];
        t->changedVolume = t->changedPan = 1;
        break;
    }
    case 0xF6: { /* instrument volume fade */
        s32 time = rd16(a);
        const s32 vol = (a[2] & 0x7F) << 24;
        if (vol != t->insVolume) {
            if (time <= 0) {
                time = 1;
            }
            t->insVolumeTicks = time;
            t->insVolumeTarget = vol;
            t->insVolumeStep = (vol - t->insVolume) / time;
        }
        break;
    }
    case 0xFC: { /* branch: the first entry of its table */
        const u8* e = at(s, rd16(a), 3);
        if (e != NULL) {
            t->prev = t->pos;
            t->pos = (u32)(e[0] << 8 | e[1]);
            t->drum = e[2];
        }
        t->coarse = t->fine = t->detune = 0;
        t->pressOverride = 0;
        t->tremoloDepth = 0;
        t->insVolumeTicks = 0;
        t->randomPan = 0;
        break;
    }
    case 0xFE: /* detour: offset, length */
        t->detourLength = a[2];
        t->saved = t->pos;
        t->pos = rd16(a);
        break;
    case 0xFF: { /* specials: the custom press envelopes */
        const u8 type = a[0], arg1 = a[1], arg2 = a[2];
        if (type == 2) {
            if (arg1 >= 1 && arg1 <= 3) {
                int k;
                s->writingEnv = arg1;
                s->customWrite[arg1 - 1] = 0;
                for (k = 0; k < 9; k++) {
                    s->customEnv[arg1 - 1][k] = 0xFF00;
                }
            } else {
                s->writingEnv = 0;
            }
        } else if (type == 3) {
            const int e = s->writingEnv - 1;
            if (e >= 0 && e < 3 && s->customWrite[e] < 9) {
                s->customEnv[e][s->customWrite[e]++] =
                    (u16)(((arg1 < 40 ? kCustomEnv[arg1] : arg1) << 8) + arg2);
            }
        } else if (type == 4) {
            t->pressOverride = arg1 <= 3 ? arg1 : 0;
        }
        break;
    }
    default: /* reverb types, events, effects */
        break;
    }
}

/* custom press envelopes are u16 (time << 8 | volume): as bytes, big-endian */
static const u8* customEnvBytes(Pm64Song* s, int which) {
    int k;
    for (k = 0; k < 10; k++) {
        const u16 v = k < 9 ? s->customEnv[which][k] : 0xFF00;
        s->customBytes[which][k * 2] = (u8)(v >> 8);
        s->customBytes[which][k * 2 + 1] = (u8)v;
    }
    return s->customBytes[which];
}

static void playNote(Pm64Song* s, Track* t, int pitch, u8 velocity, s32 length, int* nextFree) {
    int vi, got = -1;
    Voice* v;
    Note* n;
    if (t->muted) {
        return;
    }
    for (vi = *nextFree; vi < t->lastVoice; vi++) {
        (*nextFree)++;
        if (!s->voice[vi].busy) {
            got = vi;
            break;
        }
    }
    if (got < 0) {
        if (t->lastVoice - t->firstVoice >= 2) {
            s32 shortest = 0xFFFF;
            for (vi = t->firstVoice; vi < t->lastVoice; vi++) {
                if (s->note[vi].length == 0) {
                    got = vi;
                    break;
                }
            }
            if (got < 0) {
                for (vi = t->firstVoice; vi < t->lastVoice; vi++) {
                    if (!s->note[vi].pendingTick && s->note[vi].length < shortest) {
                        shortest = s->note[vi].length;
                        got = vi;
                    }
                }
            }
        } else if (t->lastVoice > t->firstVoice) {
            got = t->firstVoice;
        }
        if (got < 0) {
            return;
        }
        s->note[got].length = 0;
        resetVoice(&s->voice[got]);
    }
    v = &s->voice[got];
    n = &s->note[got];
    n->tremoloDepth = 0;
    n->velocity = velocity > 0 ? (u8)(velocity + 1) : 0;
    n->length = length;
    if (t->drum) {
        const u8* d = NULL;
        s32 tune;
        if (pitch < 72) {
            if ((u32)pitch < s->perCount) {
                d = s->per + pitch * 12;
            }
        } else if (s->drumCount > 0) {
            const int k = pitch - 72 < s->drumCount ? pitch - 72 : 0;
            d = s->drums + k * 12;
        }
        if (d == NULL || !inRange(s, d, 12)) {
            return;
        }
        n->ins = instrument(s, d[0], d[1], &v->press, &v->release);
        if (n->ins == NULL) {
            return;
        }
        n->volume = n->velocity * d[4];
        v->clientVolume = clientVolume(s, t, n->volume, 16);
        n->detune = (s32)rd16(d + 2) + t->coarse + t->fine - n->ins->keyBase;
        tune = n->detune + t->detune;
        n->randDetune = 0;
        if (d[7] != 0) {
            const s32 seed = (s32)s->rand1;
            const s32 r = ((seed >> 6) & 0xF) + ((seed << 2) & 0xF0);
            n->randDetune = (((seed >> 4) + (seed >> 1)) & 1) ? (d[7] * 5 * r) >> 8 : -((d[7] * 5 * r) >> 8);
            tune += n->randDetune;
        }
        n->pitchRatio = pitchOf(tune) * n->ins->pitchRatio;
        v->pan = d[9] != 0 ? randomPan(s, (u8)d[5], d[9]) : (u8)(d[5] & 0x7F);
        v->reverb = d[6];
    } else {
        if (t->inst == NULL) {
            return;
        }
        n->ins = t->inst;
        n->volume = clientVolume(s, t, n->velocity, 9);
        v->clientVolume = n->volume;
        n->detune = pitch * 100 + t->coarse + s->masterPitchShift + t->fine - n->ins->keyBase;
        n->randDetune = 0;
        n->pitchRatio = pitchOf(n->detune + t->detune) * n->ins->pitchRatio;
        v->pan = t->randomPan != 0 ? randomPan(s, t->insPan, t->randomPan) : t->insPan;
        v->reverb = t->insReverb;
        v->press = t->pressOverride != 0 ? customEnvBytes(s, t->pressOverride - 1) : t->press;
        v->release = t->release;
    }
    v->inst = n->ins;
    v->pitch = n->pitchRatio;
    if (n->length > 1) {
        n->pendingTick = 1;
        n->tremoloDepth = t->tremoloDepth;
        n->tremoloPhase = 0;
        n->tremoloDelay = t->tremoloDelay;
        v->sync = 1;
        v->busy = 1;
    }
}

static void updatePlaying(Pm64Song* s) {
    int i, vi, finished = 0, fading = 0;
    if (s->masterTempoTicks != 0) {
        if (--s->masterTempoTicks == 0) {
            s->masterTempo = s->masterTempoTarget;
        } else {
            s->masterTempo += s->masterTempoStep;
        }
        s->step = (u32)s->masterTempo * 10;
    }
    if (s->masterVolumeTicks != 0) {
        if (--s->masterVolumeTicks == 0) {
            s->masterVolume = s->masterVolumeTarget;
        } else {
            s->masterVolume += s->masterVolumeStep;
        }
        fading = 1;
    }
    for (i = 0; i < TRACKS; i++) {
        Track* t = &s->track[i];
        if (t->pos == 0) {
            continue;
        }
        t->changedVolume = fading;
        t->changedTune = t->changedPan = 0;
        if (t->insVolumeTicks != 0) {
            if (--t->insVolumeTicks == 0) {
                t->insVolume = t->insVolumeTarget;
            } else {
                t->insVolume += t->insVolumeStep;
            }
            t->changedVolume = 1;
        }
        if (--t->delay <= 0) {
            int nextFree = t->firstVoice, guard = 0;
            while (t->delay == 0 && guard++ < 4096) {
                const u8 op = readByte(s, t);
                if (op < 0x80) {
                    if (op == 0) {
                        if (t->prev != 0) {
                            t->pos = t->prev;
                            t->prev = 0;
                        } else {
                            finished = 1;
                            break;
                        }
                    } else if (op >= 0x78) {
                        t->delay = ((op & 7) << 8) + readByte(s, t) + 0x78;
                    } else {
                        t->delay = op;
                    }
                } else if (op < 0xD4) {
                    const u8 velocity = readByte(s, t);
                    s32 length = readByte(s, t);
                    if (length >= 0xC0) {
                        length = ((length & ~0xC0) << 8) + readByte(s, t) + 0xC0;
                    }
                    playNote(s, t, op & 0x7F, velocity, length, &nextFree);
                } else if (op >= 0xE0) {
                    const int n = kArgCount[op - 0xE0];
                    int k;
                    for (k = 0; k < n; k++) {
                        s->args[k] = readByte(s, t);
                    }
                    command(s, t, op);
                }
            }
            if (t->delay < 0) {
                t->delay = 0;
            }
        }
        if (t->muted) {
            continue;
        }
        for (vi = t->firstVoice; vi < t->lastVoice; vi++) {
            Voice* v = &s->voice[vi];
            Note* n = &s->note[vi];
            if (!v->busy) {
                continue;
            }
            if (!n->pendingTick && n->ins != NULL) {
                if (n->length > 0 && --n->length == 0) {
                    v->flags |= 0x10;
                }
                if (t->drum) {
                    if (t->changedTune) {
                        v->pitch = pitchOf(n->detune + n->randDetune + t->detune) * n->ins->pitchRatio;
                        v->sync |= 2;
                    }
                    if (t->changedVolume) {
                        v->clientVolume = clientVolume(s, t, n->volume, 16);
                        v->flags |= 0x20;
                    }
                } else {
                    if (n->tremoloDepth != 0) {
                        if (n->tremoloDelay != 0) {
                            n->tremoloDelay--;
                        } else {
                            u8 q, w;
                            s32 tr;
                            n->tremoloPhase = (u8)(n->tremoloPhase + t->tremoloRate);
                            w = (u8)((n->tremoloPhase << 2) + 3);
                            q = n->tremoloPhase >> 6;
                            tr = (q == 1 || q == 3) ? (u8)(~w) + 1 : w;
                            tr = (tr * t->tremoloDepth) >> 8;
                            if (q >= 2) {
                                tr = -tr;
                            }
                            v->pitch = pitchOf(tr + n->detune + t->detune) * n->ins->pitchRatio;
                            v->sync |= 2;
                        }
                    } else if (t->changedTune) {
                        v->pitch = pitchOf(n->detune + t->detune) * n->ins->pitchRatio;
                        v->sync |= 2;
                    }
                    if (t->changedVolume) {
                        n->volume = clientVolume(s, t, n->velocity, 9);
                        v->clientVolume = n->volume;
                        v->flags |= 0x20;
                        v->pan = t->insPan;
                        v->reverb = t->insReverb;
                    } else if (t->changedPan) {
                        v->pan = t->insPan;
                        v->reverb = t->insReverb;
                        v->sync |= 8;
                    }
                }
            }
            n->pendingTick = 0;
        }
    }
    if (finished) {
        s->state = 0;
    }
}

static void tick(Pm64Song* s) {
    int guard = 0;
    s->rand1 = (s->rand1 & 0xFFFF) + (s->playing & 0xFFFF) + (s->frameCounter & 0xFFFF);
    s->rand2 = (s->rand2 & 0xFFFF) + ((s->playing << 4) & 0xFFFF) + ((s->frameCounter >> 4) & 0xFFFF);
    while (guard++ < 8) {
        if (s->state == 0) {
            if (!readComposition(s)) {
                return;
            }
        }
        if (s->state != 1) {
            return;
        }
        updatePlaying(s);
        if (s->state == 1) {
            return;
        }
    }
}

/* ---- voices: envelopes (the N64 engine's), then the synth ---- */

#define VOLMUL(env, client, rel, scale) ((s32)(((long long)(env) * (client) * (rel) >> 14) * (scale) >> 7))

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

static void envAt(const Pm64Song* s, Voice* v, const u8* script) {
    v->cmd = script;
    v->cmdEnd = script >= s->sbn && script < s->sbn + s->sbnSize ? s->sbn + s->sbnSize : NULL;
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

static void setMix(Voice* v) {
    const float vol = (float)v->volume * (float)v->volume / 32768.0f / 32768.0f; /* squared, as the N64's */
    const float a = (float)(v->pan > 127 ? 127 : v->pan) / 127.0f * 1.5707963f;
    v->toL = vol * cosf(a);
    v->toR = vol * sinf(a);
    v->toWet = vol * (float)(v->reverb > 127 ? 127 : v->reverb) / 127.0f;
    v->rampLeft = v->delta > 0 ? v->delta : FRAME;
}

static void voiceStart(const Pm64Song* s, Voice* v) {
    int idx;
    v->busy = 1; /* (a stolen voice's reset freed it this frame: it's the new note's again) */
    envAt(s, v, v->press);
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
    v->relRelease = 0;
    v->envRelStart = 0x80;
    /* the synth: from the sample's start, from silence */
    v->playing = v->inst != NULL && v->inst->pcm != NULL;
    v->pos = 0.0;
    v->loopsLeft = v->inst != NULL ? v->inst->loopCount : 0;
    v->gainL = v->gainR = v->wet = 0.0f;
    setMix(v);
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

static void updateVoices(Pm64Song* s) {
    int i;
    for (i = 0; i < VOICES; i++) {
        Voice* v = &s->voice[i];
        if (v->cmd == NULL) {
            continue;
        }
        if (v->flags & 2) {
            v->volume = VOLMUL(v->envTarget, v->clientVolume, v->envRelStart, v->envScale);
            v->delta = (v->envTimeLeft / FRAME_USEC) * FRAME;
            v->flags &= ~2;
            v->sync |= 4;
            continue;
        }
        if (v->flags & 0x10) {
            u8 tv;
            v->flags = (v->flags & ~0x10) | 1;
            envAt(s, v, v->release);
            if (v->envTimeLeft > FRAME_USEC) {
                v->envInitial += (s32)(v->envDelta * (float)(v->envDuration - v->envTimeLeft));
            } else {
                v->envInitial = v->envTarget;
            }
            v->envDuration = v->envTimeLeft = envTime(envByte(v));
            tv = envByte(v);
            if (tv & 0x80) {
                tv &= 0x7F;
                v->relRelease = 1;
                v->envRelStart = v->envInitial;
            }
            v->envTarget = tv;
            envNext(v);
            continue;
        }
        if (v->envTimeLeft == -1) {
            if (v->flags & 0x20) {
                v->flags &= ~0x20;
                envChange(v, v->envInitial);
            }
            continue;
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
}

static void beginFrame(Pm64Song* s) {
    int i;
    for (i = 0; i < VOICES; i++) {
        Voice* v = &s->voice[i];
        if (v->donePending) {
            v->donePending = 0;
            v->cmd = NULL;
            v->busy = 0;
            v->playing = 0;
        }
        if (v->sync & 1) {
            voiceStart(s, v);
        } else if (v->sync & (4 | 8)) {
            setMix(v);
        }
        v->sync = 0;
    }
}

static float reverbSample(Pm64Song* s, float in) {
    float out = 0.0f;
    int k;
    for (k = 0; k < 4; k++) {
        float* b = &s->comb[k][s->combAt[k]];
        const float y = *b;
        s->combLp[k] = y * 0.6f + s->combLp[k] * 0.4f;
        *b = in + s->combLp[k] * 0.78f;
        if (++s->combAt[k] >= s->combLen[k]) {
            s->combAt[k] = 0;
        }
        out += y;
    }
    out *= 0.25f;
    for (k = 0; k < 2; k++) {
        float* b = &s->ap[k][s->apAt[k]];
        const float y = *b;
        *b = out + y * 0.5f;
        out = y - out * 0.5f;
        if (++s->apAt[k] >= s->apLen[k]) {
            s->apAt[k] = 0;
        }
    }
    return out;
}

static void synthFrame(Pm64Song* s) {
    float mix[FRAME * 2];
    float wet[FRAME];
    int i, k;
    memset(mix, 0, sizeof(mix));
    memset(wet, 0, sizeof(wet));
    for (i = 0; i < VOICES; i++) {
        Voice* v = &s->voice[i];
        const Inst* in = v->inst;
        float step, dl, dr, dw;
        if (!v->playing || in == NULL) {
            continue;
        }
        step = v->pitch < 1.99996f ? v->pitch : 1.99996f;
        {
            const int n = v->rampLeft > FRAME ? FRAME : v->rampLeft;
            const float r = v->rampLeft > 0 ? (float)n / (float)v->rampLeft : 1.0f;
            const float eL = v->gainL + (v->toL - v->gainL) * r;
            const float eR = v->gainR + (v->toR - v->gainR) * r;
            const float eW = v->wet + (v->toWet - v->wet) * r;
            dl = (eL - v->gainL) / FRAME;
            dr = (eR - v->gainR) / FRAME;
            dw = (eW - v->wet) / FRAME;
            v->rampLeft -= n;
        }
        for (k = 0; k < FRAME; k++) {
            const s32 p = (s32)v->pos;
            const float f = (float)(v->pos - p);
            s32 q = p + 1;
            float smp;
            if (in->loopEnd != 0 && v->loopsLeft != 0 && q >= in->loopEnd) {
                q = in->loopStart + (q - in->loopEnd);
            }
            if (p >= in->length) {
                v->playing = 0;
                break;
            }
            smp = (float)in->pcm[p] + ((q < in->length ? (float)in->pcm[q] : 0.0f) - (float)in->pcm[p]) * f;
            mix[k * 2] += smp * v->gainL;
            mix[k * 2 + 1] += smp * v->gainR;
            wet[k] += smp * v->wet;
            v->gainL += dl;
            v->gainR += dr;
            v->wet += dw;
            v->pos += step;
            if (in->loopEnd != 0 && v->loopsLeft != 0 && v->pos >= in->loopEnd) {
                v->pos -= in->loopEnd - in->loopStart;
                if (v->loopsLeft > 0) {
                    v->loopsLeft--;
                }
            }
        }
        if (!v->playing) {
            /* the sample ran out: the voice is free (the N64's AL_STOPPED) */
            v->cmd = NULL;
            v->busy = 0;
        }
    }
    /* The output: brought to the game's own music's level (Toad Town measured about 6 dB under TTYD's Rogueport
     * theme on the same channel at the same volume, so x1.95 over the old 0.8), then a peak limiter: past
     * kLimKnee the gain drops at once to keep the peak at the knee and comes back over about 80 ms, and a soft
     * knee above it keeps whatever slips through (the first sample of a sudden peak) under full scale. */
    for (k = 0; k < FRAME; k++) {
        static const float kGain = 1.8f, kLimKnee = 26000.0f, kRelease = 0.0004f;
        const float r = reverbSample(s, wet[k] * 0.5f);
        float o[2], peak;
        o[0] = (mix[k * 2] + r * 0.7f) * kGain;
        o[1] = (mix[k * 2 + 1] + r * 0.7f) * kGain;
        peak = fabsf(o[0]) > fabsf(o[1]) ? fabsf(o[0]) : fabsf(o[1]);
        s->limGain += (1.0f - s->limGain) * kRelease;
        if (peak * s->limGain > kLimKnee) {
            s->limGain = kLimKnee / peak;
        }
        for (i = 0; i < 2; i++) {
            float x = o[i] * s->limGain;
            const float ax = fabsf(x);
            if (ax > kLimKnee) {
                /* soft knee: kLimKnee .. +inf onto kLimKnee .. 32767 */
                const float room = 32767.0f - kLimKnee;
                const float y = kLimKnee + room * tanhf((ax - kLimKnee) / room);
                x = x < 0.0f ? -y : y;
            }
            s->frame[k * 2 + i] = (short)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)x);
        }
    }
}

static void audioFrame(Pm64Song* s) {
    beginFrame(s);
    s->frameCounter++;
    s->playing++;
    s->counter -= (s32)s->step;
    if (s->counter <= 0) {
        s->counter += (s32)s->tickInterval;
        tick(s);
    }
    updateVoices(s);
    synthFrame(s);
    if (s->state == 2) {
        int i, any = 0;
        for (i = 0; i < VOICES; i++) {
            any |= s->voice[i].playing;
        }
        if (!any) {
            s->ended = 1;
        }
    }
}

/* ---- the API ---- */

Pm64Song* portPm64SongOpen(int songId) {
    size_t size;
    const u8* sbn = portPm64Linked() != PM64_NONE ? portPm64Segment(PM64_SEG_AUDIO, &size) : NULL;
    Pm64Song* s;
    const u8* h;
    u32 comp;
    int k;
    static const int kComb[4] = {1116, 1188, 1277, 1356};
    static const int kAp[2] = {556, 441};
    if (sbn == NULL || size < 0x40) {
        return NULL;
    }
    if ((s = (Pm64Song*)calloc(1, sizeof(Pm64Song))) == NULL) {
        return NULL;
    }
    s->sbn = sbn;
    s->sbnSize = size;
    if (!portPm64AudioHold(sbn, 1)) { /* (the segment stays while the song plays, an unlink or not) */
        free(s);
        return NULL;
    }
    if (!loadInit(s, songId) || s->bgmSize < 0x24 || memcmp(s->bgm, "BGM ", 4) != 0) {
        portPm64SongClose(s);
        return NULL;
    }
    h = s->bgm + 0x10;
    comp = (u32)rd16(h + 4) << 2;
    if (comp == 0 || comp >= s->bgmSize) {
        portPm64SongClose(s);
        return NULL;
    }
    s->compStart = comp;
    if (rd16(h + 0x0C) != 0) {
        s->drums = s->bgm + ((u32)rd16(h + 0x0C) << 2);
        s->drumCount = rd16(h + 0x0E);
    }
    if (rd16(h + 0x10) != 0) {
        s->insts = s->bgm + ((u32)rd16(h + 0x10) << 2);
        s->instCount = rd16(h + 0x12);
    }
    setTicks(s, (u32)kTicksPerBeat[h[0] & 7]);
    for (k = 0; k < 4; k++) {
        s->combLen[k] = kComb[k];
    }
    for (k = 0; k < 2; k++) {
        s->apLen[k] = kAp[k];
    }
    s->limGain = 1.0f;
    initPlayer(s);
    s->frameAt = FRAME;
    return s;
}

int portPm64SongRender(Pm64Song* s, short* out, int frames) {
    int n = 0;
    if (s == NULL) {
        return 0;
    }
    while (n < frames) {
        int take;
        if (s->frameAt >= FRAME) {
            if (s->ended) {
                break;
            }
            audioFrame(s);
            s->frameAt = 0;
        }
        take = FRAME - s->frameAt;
        if (take > frames - n) {
            take = frames - n;
        }
        memcpy(out + n * 2, s->frame + s->frameAt * 2, (size_t)take * 2 * sizeof(short));
        s->frameAt += take;
        n += take;
    }
    return n;
}

void portPm64SongClose(Pm64Song* s) {
    int i;
    if (s == NULL) {
        return;
    }
    for (i = 0; i < s->instUsed; i++) {
        free(s->inst[i].pcm);
    }
    portPm64AudioHold(s->sbn, -1);
    free(s);
}

/* ---- TTYD tracks to PM64 songs (docs/PM64_EXTRAS.md lists them) ---- */

typedef struct {
    const char* track; /* without "BGM_" and the "M_" of the muffled copies */
    int song;
} TrackSong;

static const TrackSong kTrackSongs[] = {
    {"TITLE1", 0x6A},           {"FILE_MENU1", 0x69},        {"OPENING1", 0x95},
    {"ENDING1", 0x3F},          {"STAFF_ROLL1", 0x6E},
    {"STG0_GOR1", 0x00},        {"STG0_TIK1", 0x4B},         {"STG0_TIK2", 0x19},
    {"STG0_CSN1", 0x5D},        {"STG0_100DN1", 0x65},
    {"STG1_HEI1", 0x11},        {"STG1_NOK1", 0x13},         {"STG1_TORIDE1", 0x18},
    {"STG1_GON1", 0x14},
    {"STG2_WIN1", 0x1A},        {"STG2_MRI1", 0x25},         {"STG2_MRI2", 0x26},
    {"STG3_URN1", 0x20},        {"STG3_TOU1", 0x21},         {"STG3_TOU2", 0x85},
    {"STG4_USU1", 0x1D},        {"STG4_GRA1", 0x1A},         {"STG4_JIN1", 0x1B},
    {"STG5_MUJ1", 0x24},        {"STG5_DOU1", 0x18},
    {"STG6_RSH1", 0x80},        {"STG6_RSH2", 0x79},         {"STG6_RSH3", 0x39},
    {"STG6_HOM1", 0x15},        {"STG6_EKI1", 0x3C},         {"STG6_PIK1", 0x31},
    {"STG6_PIK2", 0x35},
    {"STG7_BOM1", 0x38},        {"STG7_MON1", 0x61},         {"STG7_DUN1", 0x37},
    {"STG7_DUN2", 0x62},
    {"STG8_LST1", 0x64},        {"STG8_LST2", 0x65},         {"STG8_LST3", 0x5F},
    {"STGK_CASTLE1", 0x66},
    {"ZAKO_BATTLE1", 0x02},     {"KOBOSS_BATTLE1", 0x03},    {"CHUBOSS_BATTLE1", 0x5B},
    {"BOSS_STG1_GONBABA1", 0x0D}, {"BOSS_STG3_3RDLEADER1", 0x0C}, {"BOSS_STG3_3WOMEN1", 0x0A},
    {"BOSS_STG3_CHAMP1", 0x08}, {"BOSS_STG3_GANCE1", 0x07},  {"BOSS_STG4_RUNPELL1", 0x0B},
    {"BOSS_STG4_FAKEMARIO1", 0x09}, {"BOSS_STG5_KORTESS1", 0x0F}, {"BOSS_STG5_SHIP1", 0x04},
    {"BOSS_STG6_GREAT_MOAMOA1", 0x0E}, {"BOSS_STG8_3RDHEAD1", 0x0D}, {"BOSS_STG8_WITCH", 0x2C},
    {"BOSS_STG8_KOOPA", 0x67},  {"BOSS_STG8_LAST_BOSS1", 0x05}, {"BOSS_STG8_LAST_BOSS2", 0x05},
    {"BATTLE_WIN1", 0x8B},      {"BATTLE_WIN2", 0x8B},       {"BATTLE_WIN3", 0x8B},
    {"BATTLE_LOSE1", 0x87},
    {"EVT_DANGER1", 0x6B},      {"EVT_DANGER2", 0x27},       {"EVT_REST1", 0x88},
    {"EVT_PEACH1", 0x73},       {"EVT_MARIO_HOUSE1", 0x94},  {"EVT_MARIO_HOUSE2", 0x94},
    {"EVT_KUPPA1", 0x66},       {"EVT_KUPPA2", 0x66},        {"EVT_MINIGAME1", 0x8D},
    {"EVT_COOKING1", 0x71},     {"EVT_HAPPY1", 0x7F},        {"EVT_STAGE_CLEAR1", 0x40},
    {"EVT_SAILING1", 0x81},     {"EVT_STG4_TRAIN1", 0x80},   {"EVT_PEACH_WALTZ1", 0x68},
    {"EVT_WITCH1", 0x46},       {"EVT_QUIZ1", 0x77},
    {"FF_GET_PARTY1", 0x82},    {"FF_GET_STARSTONE1", 0x40}, {"FF_STG_START1", 0x41},
    {"FF_POWERUP1", 0x8E},      {"FF_GO_SLEEPING1", 0x88},
};

static const struct {
    int song;
    const char* name;
} kSongNames[] = {
    {0x00, "Toad Town"}, {0x02, "Battle"}, {0x03, "Special Battle"}, {0x04, "Jr. Troopa Battle"},
    {0x05, "Final Bowser Battle"}, {0x07, "Goomba King Battle"}, {0x08, "Koopa Bros. Battle"},
    {0x09, "Fake Bowser Battle"}, {0x0A, "Tutankoopa Battle"}, {0x0B, "Tubba Blubba Battle"},
    {0x0C, "General Guy Battle"}, {0x0D, "Lava Piranha Battle"}, {0x0E, "Huff N. Puff Battle"},
    {0x0F, "Crystal King Battle"}, {0x11, "Pleasant Path"}, {0x13, "Koopa Village"},
    {0x14, "Koopa Bros. Fortress"}, {0x15, "Dry Dry Outpost"}, {0x18, "Dry Dry Ruins"},
    {0x19, "Ruins Basement"}, {0x1A, "Forever Forest"}, {0x1B, "Boo's Mansion"}, {0x1D, "Gusty Gulch"},
    {0x20, "Shy Guy's Toy Box"}, {0x21, "Toy Box Train"}, {0x24, "Jade Jungle"}, {0x25, "Deep Jungle"},
    {0x26, "Yoshi's Village"}, {0x27, "Yoshi's Panic"}, {0x2C, "Master Battle"}, {0x31, "Flower Fields"},
    {0x35, "Sun Tower"}, {0x37, "Crystal Palace"}, {0x38, "Shiver City"}, {0x39, "Penguin Mystery"},
    {0x3C, "Starborn Valley"}, {0x3F, "Peach's Castle Party"}, {0x40, "Chapter End"},
    {0x41, "Chapter Start"}, {0x46, "Kammy Koopa"}, {0x4B, "Toad Town Tunnels"}, {0x5B, "Miniboss Battle"},
    {0x5D, "Club 64"}, {0x5F, "Bowser's Castle Falls"}, {0x61, "Shooting Star Summit"},
    {0x62, "Starship"}, {0x64, "Bowser's Castle"}, {0x65, "Bowser's Castle Caves"},
    {0x66, "Bowser's Theme"}, {0x67, "Bowser Battle"}, {0x68, "Peach's Wishes"}, {0x69, "File Select"},
    {0x6A, "Main Theme"}, {0x6B, "Bowser Attacks"}, {0x6E, "The End"}, {0x71, "Stirring the Cake"},
    {0x73, "Prisoner Peach"}, {0x77, "Peach's Quiz"}, {0x79, "Whodunit"}, {0x7F, "Sunshine Returns"},
    {0x80, "Riding the Rails"}, {0x81, "Riding the Whale"}, {0x82, "New Partner"}, {0x85, "Playroom"},
    {0x87, "Game Over"}, {0x88, "Taking a Rest"}, {0x8B, "Battle Won"}, {0x8D, "Boo Minigame"},
    {0x8E, "Level Up"}, {0x94, "Mario Bros.' House"}, {0x95, "Story"},
};

const char* portPm64SongName(int songId) {
    size_t i;
    for (i = 0; i < sizeof(kSongNames) / sizeof(kSongNames[0]); i++) {
        if (kSongNames[i].song == songId) {
            return kSongNames[i].name;
        }
    }
    return "";
}

int portPm64SongForTrack(const char* track) {
    size_t i;
    if (track == NULL || strncmp(track, "BGM_", 4) != 0) {
        return -1;
    }
    track += 4;
    if (strncmp(track, "M_", 2) == 0) {
        track += 2;
    }
    for (i = 0; i < sizeof(kTrackSongs) / sizeof(kTrackSongs[0]); i++) {
        if (strcmp(kTrackSongs[i].track, track) == 0) {
            return kTrackSongs[i].song;
        }
    }
    return -1;
}

/* Mixed by area: these areas (and the battles fought there) keep the game's
 * own music, the rest play PM64's: about half each, the same every time. */
static int areaKeepsTtyd(const char* map) {
    static const char* const kKeep[] = {"gor", "tik", "hei", "gon", "win", "tou", "usu", "jin",
                                        "dou", "rsh", "pik", "moo", "las", "aji", "bom", "jon"};
    size_t i;
    if (map == NULL || map[0] == '\0') {
        return 1;
    }
    for (i = 0; i < sizeof(kKeep) / sizeof(kKeep[0]); i++) {
        if (strncmp(map, kKeep[i], 3) == 0) {
            return 1;
        }
    }
    return 0;
}

/* a Paper Mario 64 boss fight's own battle song (portPm64MusicBossSong): armed as the fight is set up, played in
 * place of its battle track, let go at the first other track after it played */
static int sBossSong = -1, sBossPlayed;

void portPm64MusicBossSong(int songId) {
    sBossSong = songId;
    sBossPlayed = 0;
}

void portPm64MusicBattleStarted(void) {
    if (!sBossPlayed) {
        sBossSong = -1;
    }
}

int portPm64MusicPick(const char* track, const char* mapName) {
    static int* sMode;
    static int* sBossMusic;
    if (sMode == NULL) {
        sMode = portSettingsDeclare("pm64_music", 0, 0, 2);
        sBossMusic = portSettingsDeclare("pm64_boss_music", 1, 0, 1);
    }
    if (sBossSong >= 0) {
        if (track != NULL && strcmp(track, PM64_BOSS_TRACK) == 0) {
            if (*sBossMusic != 0 && portPm64Linked() != PM64_NONE) {
                sBossPlayed = 1;
                return sBossSong;
            }
        } else if (sBossPlayed) {
            sBossSong = -1; /* (the fight is over: its win jingle, the field's music) */
        }
    }
    if (*sMode == 0 || portPm64Linked() == PM64_NONE) {
        return -1;
    }
    if (*sMode == 2 && areaKeepsTtyd(mapName)) {
        return -1;
    }
    return portPm64SongForTrack(track);
}
