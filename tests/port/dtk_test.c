/* DVD audio streaming: ADP decoding against hand-computed samples and a
 * floating-point model of the format, then the ring/mixer path. */
#include <dolphin/types.h>
#include <dolphin/ai.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/dtk_pc.h"

static int sFailures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            sFailures++;                                                                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* Build a block: per-channel header byte and 28 nibbles per channel. */
static void makeBlock(u8* b, u8 hl, u8 hr, const int* l, const int* r) {
    int i;
    b[0] = hl;
    b[1] = hr;
    b[2] = hl;
    b[3] = hr;
    for (i = 0; i < 28; i++) {
        b[4 + i] = (u8)((l[i] & 0xF) | ((r[i] & 0xF) << 4));
    }
}

/* Reference model straight from the format description, in doubles:
 * x = nibble * 2^12 / 2^shift (truncated toward -inf as an integer shift),
 * y = x + (c0 * y1 + c1 * y2) / 64, output = floor(y) clamped. */
typedef struct Ref {
    double y1[2], y2[2];
} Ref;

static int refSample(Ref* m, int ch, int nibble, u8 header) {
    static const double c[4][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}};
    int f = (header >> 4) & 3, sh = header & 15;
    double x = (double)((nibble * 4096) >> sh);
    double y = x + (c[f][0] * m->y1[ch] + c[f][1] * m->y2[ch]) / 64.0;
    int out;
    m->y2[ch] = m->y1[ch];
    m->y1[ch] = y;
    out = (int)floor(y);
    return out > 32767 ? 32767 : out < -32768 ? -32768 : out;
}

static void testHandComputed(void) {
    int l[28] = {1, -1, 7, -8}, r[28] = {1};
    u8 block[32];
    s16 out[56];
    PortADPState st;
    memset(&st, 0, sizeof(st));
    /* left: filter 0, shift 0 -> nibble * 4096; right: filter 1 (60/64), shift 0 */
    makeBlock(block, 0x00, 0x10, l, r);
    CHECK(block[4] == 0x11 && block[5] == 0x0F && block[6] == 0x07 && block[7] == 0x08, "block bytes");
    portADPDecodeBlock(&st, block, out);
    CHECK(out[0] == 4096, "L0 %d", out[0]);
    CHECK(out[2] == -4096, "L1 %d", out[2]);
    CHECK(out[4] == 28672, "L2 %d", out[4]);
    CHECK(out[6] == -32768, "L3 %d", out[6]);
    CHECK(out[8] == 0, "L4 %d", out[8]);
    /* right decays by 60/64 from 4096: 3840, 3600, 3375, 3164.06.. */
    CHECK(out[1] == 4096, "R0 %d", out[1]);
    CHECK(out[3] == 3840, "R1 %d", out[3]);
    CHECK(out[5] == 3600, "R2 %d", out[5]);
    CHECK(out[7] == 3375, "R3 %d", out[7]);
    CHECK(out[9] == 3164, "R4 %d", out[9]);

    /* Saturation: filter 1, nibbles 7, 7 -> 28672, 28672 + 26880 -> 32767. */
    {
        int l2[28] = {7, 7}, r2[28] = {-8, -8};
        memset(&st, 0, sizeof(st));
        makeBlock(block, 0x10, 0x10, l2, r2);
        portADPDecodeBlock(&st, block, out);
        CHECK(out[0] == 28672 && out[2] == 32767, "L sat %d %d", out[0], out[2]);
        CHECK(out[1] == -32768 && out[3] == -32768, "R sat %d %d", out[1], out[3]);
    }
    /* Shift: nibble -3 with shift 4 -> -12288 >> 4 = -768. */
    {
        int l3[28] = {-3}, r3[28] = {5};
        memset(&st, 0, sizeof(st));
        makeBlock(block, 0x04, 0x0C, l3, r3);
        portADPDecodeBlock(&st, block, out);
        CHECK(out[0] == -768, "shift L %d", out[0]);
        CHECK(out[1] == 5, "shift R %d", out[1]);
    }
}

/* All four filters over several blocks with history carried between them,
 * against the double model (the decoder keeps 6 fractional bits, so allow 1). */
static void testAgainstModel(void) {
    static const u8 headers[4][2] = {{0x04, 0x15}, {0x26, 0x37}, {0x35, 0x24}, {0x16, 0x07}};
    PortADPState st;
    Ref ref;
    u32 seed = 12345;
    int blk, i, maxErr = 0;
    memset(&st, 0, sizeof(st));
    memset(&ref, 0, sizeof(ref));
    for (blk = 0; blk < 4; blk++) {
        int l[28], r[28];
        u8 block[32];
        s16 out[56];
        for (i = 0; i < 28; i++) {
            seed = seed * 1103515245u + 12345u;
            l[i] = (int)((seed >> 16) & 15) - 8;
            seed = seed * 1103515245u + 12345u;
            r[i] = (int)((seed >> 16) & 15) - 8;
        }
        makeBlock(block, headers[blk][0], headers[blk][1], l, r);
        portADPDecodeBlock(&st, block, out);
        for (i = 0; i < 28; i++) {
            int el = refSample(&ref, 0, l[i], headers[blk][0]);
            int er = refSample(&ref, 1, r[i], headers[blk][1]);
            int dl = abs(out[2 * i] - el), dr = abs(out[2 * i + 1] - er);
            if (dl > maxErr) {
                maxErr = dl;
            }
            if (dr > maxErr) {
                maxErr = dr;
            }
        }
    }
    CHECK(maxErr <= 1, "model mismatch %d", maxErr);
}

/* ---- ring + mixer ---- */

static u8 sDisc[64 * 4];

static s32 memRead(void* handle, void* dst, u32 length, u32 offset) {
    u32 size = (u32)(uintptr_t)handle;
    if (offset >= size) {
        return 0;
    }
    if (length > size - offset) {
        length = size - offset;
    }
    memcpy(dst, sDisc + offset, length);
    return (s32)length;
}

static void testMixer(void) {
    int l[28], r[28], i;
    s16 expect[56 * 2], out[200 * 2];
    PortADPState st;

    for (i = 0; i < 28; i++) {
        l[i] = (i % 15) - 7;
        r[i] = 7 - (i % 15);
    }
    makeBlock(sDisc, 0x01, 0x11, l, r);
    makeBlock(sDisc + 32, 0x21, 0x31, r, l);
    memset(&st, 0, sizeof(st));
    portADPDecodeBlock(&st, sDisc, expect);
    portADPDecodeBlock(&st, sDisc + 32, expect + 56);

    /* 32 kHz stream into 32 kHz output: no resampling; stop at the end. */
    portDTKCancel();
    portDTKPrepare((void*)(uintptr_t)64, memRead, 0, 64);
    portDTKStopAtEnd();
    AISetStreamSampleRate(AI_SAMPLERATE_32KHZ);
    AISetStreamVolLeft(255);
    AISetStreamVolRight(255);
    AIResetStreamSampleCount();

    memset(out, 0, sizeof(out));
    portDTKMix(out, 56, 32000);
    CHECK(out[0] == 0, "plays before AISetStreamPlayState(1)");
    AISetStreamPlayState(AI_STREAM_START);
    portDTKMix(out, 56, 32000);
    CHECK(out[0] == 0, "plays before the pump filled the ring");
    portDTKPump();
    portDTKMix(out, 56, 32000);
    /* The last frame needs the one after it for interpolation, so 55 come out. */
    for (i = 0; i < 55 * 2; i++) {
        CHECK(out[i] == expect[i], "mix sample %d: %d != %d", i, out[i], expect[i]);
        if (out[i] != expect[i]) {
            break;
        }
    }
    CHECK(AIGetStreamSampleCount() >= 54 && AIGetStreamSampleCount() <= 56, "sample count %u",
          AIGetStreamSampleCount());

    /* Data exhausted: the stream ends (stop at end), output stays silent. */
    memset(out, 0, sizeof(out));
    portDTKPump();
    portDTKMix(out, 100, 32000);
    portDTKMix(out, 100, 32000);
    CHECK(!portDTKUsesHandle((void*)(uintptr_t)64), "stream still active after its end");

    /* Volume and mixing on top of existing samples, with saturation. */
    portDTKPrepare((void*)(uintptr_t)64, memRead, 0, 64);
    AISetStreamVolLeft(0);
    AISetStreamVolRight(128);
    portDTKPump();
    for (i = 0; i < 56; i++) {
        out[2 * i] = 1000;
        out[2 * i + 1] = 32000;
    }
    portDTKMix(out, 56, 32000);
    CHECK(out[0] == 1000, "left muted: %d", out[0]);
    CHECK(out[1] == (s16)(32000 + expect[1] * 128 / 255 > 32767 ? 32767 : 32000 + expect[1] * 128 / 255),
          "right half volume: %d", out[1]);

    /* Default: loop. 48 kHz -> 32 kHz consumes 1.5 frames per output frame. */
    portDTKCancel();
    portDTKPrepare((void*)(uintptr_t)64, memRead, 0, 64);
    AISetStreamSampleRate(AI_SAMPLERATE_48KHZ);
    AISetStreamVolLeft(255);
    AISetStreamVolRight(255);
    AIResetStreamSampleCount();
    portDTKPump();
    memset(out, 0, sizeof(out));
    portDTKMix(out, 200, 32000);
    CHECK(AIGetStreamSampleCount() >= 298 && AIGetStreamSampleCount() <= 300,
          "48k looped sample count %u", AIGetStreamSampleCount());
    CHECK(out[2] == (s16)((expect[2] + expect[4]) / 2) || abs(out[2] - (expect[2] + expect[4]) / 2) <= 1,
          "interpolated frame 1: %d (between %d and %d)", out[2], expect[2], expect[4]);
    CHECK(portDTKUsesHandle((void*)(uintptr_t)64), "looping stream ended");
    portDTKCancel();
    CHECK(!portDTKUsesHandle((void*)(uintptr_t)64), "cancel");
}

int main(void) {
    testHandComputed();
    testAgainstModel();
    testMixer();
    if (sFailures == 0) {
        printf("dtk_test: OK\n");
    }
    return sFailures != 0;
}
