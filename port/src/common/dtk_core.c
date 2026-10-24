/* DVD audio streaming (DTK) and the AI streaming registers, for every backend.
 *
 * ADP format: the disc data is a sequence of 32-byte blocks. Bytes 0 and 1
 * are the left and right channel headers (high nibble: predictor filter 0-3,
 * low nibble: shift); bytes 2 and 3 repeat them. Each of the following 28
 * bytes is one stereo frame: low nibble left, high nibble right, signed.
 * A sample is (nibble << 12) >> shift plus a prediction from the channel's
 * last two outputs with coefficient pairs (0,0) (60,0) (115,-52) (98,-55)
 * over 64. The history is kept with 6 extra fractional bits.
 *
 * Threads: portDTKPump (game thread) reads raw blocks from the source into a
 * ring buffer without holding the lock during the read; portDTKMix (audio
 * thread) decodes from the ring. A generation counter drops reads that were
 * in flight across a prepare/cancel. */
#include <dolphin/types.h>
#include <dolphin/ai.h>
#include <string.h>
#include "port/dtk_pc.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static SRWLOCK sLock = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&sLock)
#define UNLOCK() ReleaseSRWLockExclusive(&sLock)
#else
#include <pthread.h>
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&sLock)
#define UNLOCK() pthread_mutex_unlock(&sLock)
#endif

/* ---- ADP decoder ---- */

static s16 adpSample(s32 nibble, u8 header, s32* hist1, s32* hist2) {
    static const s32 coef[4][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}};
    s32 filter = (header >> 4) & 3;
    s32 shift = header & 0xF;
    s32 pred, cur;

    /* Prediction in 1/64 units: the history is already scaled by 64, so
     * coef/64 * hist keeps that scale. Round to nearest. */
    pred = (*hist1 * coef[filter][0] + *hist2 * coef[filter][1] + 32) >> 6;
    if (pred > 0x1FFFFF) {
        pred = 0x1FFFFF;
    } else if (pred < -0x200000) {
        pred = -0x200000;
    }
    nibble = (nibble ^ 8) - 8; /* sign-extend 4 bits */
    cur = ((nibble * 4096) >> shift) * 64 + pred;
    *hist2 = *hist1;
    *hist1 = cur;
    cur >>= 6;
    if (cur > 32767) {
        cur = 32767;
    } else if (cur < -32768) {
        cur = -32768;
    }
    return (s16)cur;
}

void portADPDecodeBlock(PortADPState* state, const u8* block, s16* out) {
    int i;
    for (i = 0; i < PORT_ADP_BLOCK_FRAMES; i++) {
        u8 b = block[4 + i];
        out[2 * i + 0] = adpSample(b & 0xF, block[0], &state->hist1[0], &state->hist2[0]);
        out[2 * i + 1] = adpSample(b >> 4, block[1], &state->hist1[1], &state->hist2[1]);
    }
}

/* ---- Stream state ---- */

#define RING_SIZE (64 * 1024) /* raw ADP bytes, ~1.2 s at 48 kHz */
#define PUMP_CHUNK (8 * 1024)

typedef struct DTKSource {
    void* handle;
    PortDTKReadFn read;
    u32 offset;
    u32 length;
} DTKSource;

static struct {
    /* drive side (game thread writes, under lock) */
    BOOL active;      /* a stream is prepared */
    BOOL hasNext;
    BOOL stopAtEnd;
    BOOL eof;         /* all data of the last stream is in the ring */
    DTKSource cur;
    DTKSource next;
    u32 readPos;      /* next byte of `cur` to read */
    u32 gen;
    /* ring of raw blocks */
    u8 ring[RING_SIZE];
    u32 ringHead;     /* read index (audio thread) */
    u32 ringFill;
    /* decoder (audio thread) */
    PortADPState adp;
    s16 frames[PORT_ADP_BLOCK_FRAMES * 2];
    u32 frameIdx;     /* next frame in `frames`; PORT_ADP_BLOCK_FRAMES = empty */
    s16 prev[2], curr[2];
    u32 phase;        /* 16.16 position between prev and curr */
    BOOL primed;
    /* AI registers */
    u32 playState;
    u32 sampleRate;
    u8 volL, volR;
    u32 sampleCount;
    u32 trigger;
    AISCallback callback;
} s = {.frameIdx = PORT_ADP_BLOCK_FRAMES, .sampleRate = AI_SAMPLERATE_48KHZ};

static void resetDecoder(void) {
    memset(&s.adp, 0, sizeof(s.adp));
    s.frameIdx = PORT_ADP_BLOCK_FRAMES;
    s.prev[0] = s.prev[1] = s.curr[0] = s.curr[1] = 0;
    s.phase = 0;
    s.primed = FALSE;
    s.ringHead = 0;
    s.ringFill = 0;
}

void portDTKPrepare(void* handle, PortDTKReadFn read, u32 offset, u32 length) {
    DTKSource src;
    src.handle = handle;
    src.read = read;
    src.offset = offset;
    src.length = length & ~(u32)(PORT_ADP_BLOCK_SIZE - 1);
    LOCK();
    if (!s.active) {
        s.cur = src;
        s.readPos = 0;
        s.active = TRUE;
        s.hasNext = FALSE;
        s.stopAtEnd = FALSE;
        s.eof = FALSE;
        s.gen++;
        resetDecoder();
    } else {
        s.next = src;
        s.hasNext = TRUE;
        s.stopAtEnd = FALSE;
    }
    UNLOCK();
}

void portDTKCancel(void) {
    LOCK();
    s.active = FALSE;
    s.hasNext = FALSE;
    s.eof = FALSE;
    s.gen++;
    resetDecoder();
    UNLOCK();
}

void portDTKStopAtEnd(void) {
    LOCK();
    s.stopAtEnd = TRUE;
    s.hasNext = FALSE;
    UNLOCK();
}

BOOL portDTKUsesHandle(void* handle) {
    BOOL r;
    LOCK();
    r = (s.active && s.cur.handle == handle) || (s.hasNext && s.next.handle == handle);
    UNLOCK();
    return r;
}

void portDTKPump(void) {
    static u8 buf[PUMP_CHUNK];
    int guard;

    for (guard = 0; guard < RING_SIZE / PUMP_CHUNK; guard++) {
        DTKSource src;
        u32 pos, len, gen, i;
        s32 got;

        LOCK();
        if (!s.active || s.eof || RING_SIZE - s.ringFill < PUMP_CHUNK) {
            UNLOCK();
            return;
        }
        if (s.readPos >= s.cur.length) {
            /* End of the current stream: switch, loop or finish. */
            if (s.stopAtEnd || s.cur.length == 0) {
                s.eof = TRUE;
                UNLOCK();
                return;
            }
            if (s.hasNext) {
                s.cur = s.next;
                s.hasNext = FALSE;
            }
            s.readPos = 0;
            if (s.cur.length == 0) {
                s.eof = TRUE;
                UNLOCK();
                return;
            }
        }
        src = s.cur;
        pos = s.readPos;
        gen = s.gen;
        UNLOCK();

        len = src.length - pos;
        if (len > PUMP_CHUNK) {
            len = PUMP_CHUNK;
        }
        got = src.read(src.handle, buf, len, src.offset + pos);
        if (got <= 0) {
            got = 0;
        }
        got &= ~(PORT_ADP_BLOCK_SIZE - 1);

        LOCK();
        if (gen != s.gen) {
            UNLOCK();
            continue;
        }
        if (got == 0) {
            /* Read error or truncated file: end of the data. */
            s.eof = TRUE;
            UNLOCK();
            return;
        }
        for (i = 0; i < (u32)got; i++) {
            s.ring[(s.ringHead + s.ringFill + i) % RING_SIZE] = buf[i];
        }
        s.ringFill += (u32)got;
        s.readPos += (u32)got;
        UNLOCK();
    }
}

/* Next decoded stream frame; FALSE on underrun or end of data. Locked. */
static BOOL nextFrame(s16* lr) {
    if (s.frameIdx >= PORT_ADP_BLOCK_FRAMES) {
        u8 block[PORT_ADP_BLOCK_SIZE];
        u32 i;
        if (s.ringFill < PORT_ADP_BLOCK_SIZE) {
            if (s.eof) {
                s.active = FALSE; /* stopped at the end */
            }
            return FALSE;
        }
        for (i = 0; i < PORT_ADP_BLOCK_SIZE; i++) {
            block[i] = s.ring[(s.ringHead + i) % RING_SIZE];
        }
        s.ringHead = (s.ringHead + PORT_ADP_BLOCK_SIZE) % RING_SIZE;
        s.ringFill -= PORT_ADP_BLOCK_SIZE;
        portADPDecodeBlock(&s.adp, block, s.frames);
        s.frameIdx = 0;
    }
    lr[0] = s.frames[2 * s.frameIdx];
    lr[1] = s.frames[2 * s.frameIdx + 1];
    s.frameIdx++;
    return TRUE;
}

static s16 clamp16(s32 v) {
    return (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

void portDTKMix(s16* out, u32 frames, u32 outRate) {
    u32 step, i, consumed = 0;
    s32 volL, volR;
    AISCallback cb = NULL;
    u32 cbCount = 0;

    LOCK();
    if (!s.active || s.playState != AI_STREAM_START || outRate == 0) {
        UNLOCK();
        return;
    }
    step = (u32)(((u64)(s.sampleRate == AI_SAMPLERATE_32KHZ ? 32000 : 48000) << 16) / outRate);
    volL = s.volL;
    volR = s.volR;
    if (!s.primed) {
        /* prev = first frame, curr = second (same block, so it can't starve) */
        if (!nextFrame(s.prev)) {
            UNLOCK();
            return;
        }
        nextFrame(s.curr);
        s.phase = 0;
        s.primed = TRUE;
    }
    for (i = 0; i < frames; i++) {
        s32 l, r, f;
        BOOL starved = FALSE;
        while (s.phase >= 0x10000) {
            s.prev[0] = s.curr[0];
            s.prev[1] = s.curr[1];
            if (!nextFrame(s.curr)) {
                starved = TRUE;
                break;
            }
            s.phase -= 0x10000;
            consumed++;
        }
        if (starved) {
            break;
        }
        f = (s32)(s.phase & 0xFFFF);
        l = s.prev[0] + (((s32)(s.curr[0] - s.prev[0]) * f) >> 16);
        r = s.prev[1] + (((s32)(s.curr[1] - s.prev[1]) * f) >> 16);
        out[2 * i + 0] = clamp16(out[2 * i + 0] + l * volL / 255);
        out[2 * i + 1] = clamp16(out[2 * i + 1] + r * volR / 255);
        s.phase += step;
    }
    if (consumed != 0) {
        u32 before = s.sampleCount;
        s.sampleCount += consumed;
        if (s.callback != NULL && before < s.trigger && s.sampleCount >= s.trigger) {
            cb = s.callback;
            cbCount = s.sampleCount;
        }
    }
    UNLOCK();
    if (cb != NULL) {
        cb(cbCount);
    }
}

/* ---- AI streaming registers ---- */

AISCallback AIRegisterStreamCallback(AISCallback callback) {
    AISCallback old;
    LOCK();
    old = s.callback;
    s.callback = callback;
    UNLOCK();
    return old;
}

u32 AIGetStreamSampleCount(void) {
    u32 v;
    LOCK();
    v = s.sampleCount;
    UNLOCK();
    return v;
}

void AIResetStreamSampleCount(void) {
    LOCK();
    s.sampleCount = 0;
    UNLOCK();
}

void AISetStreamTrigger(u32 trigger) {
    LOCK();
    s.trigger = trigger;
    UNLOCK();
}

u32 AIGetStreamTrigger(void) {
    return s.trigger;
}

void AISetStreamPlayState(u32 state) {
    LOCK();
    s.playState = state;
    UNLOCK();
}

u32 AIGetStreamPlayState(void) {
    return s.playState;
}

void AISetStreamSampleRate(u32 rate) {
    LOCK();
    s.sampleRate = rate;
    UNLOCK();
}

u32 AIGetStreamSampleRate(void) {
    return s.sampleRate;
}

void AISetStreamVolLeft(u8 vol) {
    s.volL = vol;
}

u8 AIGetStreamVolLeft(void) {
    return s.volL;
}

void AISetStreamVolRight(u8 vol) {
    s.volR = vol;
}

u8 AIGetStreamVolRight(void) {
    return s.volR;
}
