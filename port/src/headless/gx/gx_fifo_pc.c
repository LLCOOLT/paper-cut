/* GX write-gather pipe and immediate-mode vertex functions for PC.
 * Every store becomes one PPCWGPipe slot in sFifo; the GX backend replays
 * the slots, reading each with the width the vertex format dictates.
 * Also defines the SDK's render mode objects (from GXFrameBuf.c). */
#include <dolphin.h>
#include <dolphin/gx.h>
#include <string.h>
#include "port/gx_pc.h"

#define FIFO_SLOTS (1u << 20)

static PPCWGPipe sFifo[FIFO_SLOTS];
static u32 sFifoCount;
static u32 sOverflowSlot;

volatile PPCWGPipe* __GXPCWGPipeNext(void) {
    if (sFifoCount == FIFO_SLOTS) {
        /* The backend drains per draw; keep going, dropping data. */
        return (volatile PPCWGPipe*)&sFifo[sOverflowSlot];
    }
    return (volatile PPCWGPipe*)&sFifo[sFifoCount++];
}

const PPCWGPipe* portGXFifoSlots(u32* count) {
    *count = sFifoCount;
    return sFifo;
}

void portGXFifoReset(void) {
    sFifoCount = 0;
}

#define FUNC_1PARAM(name, T) \
    void name##1##T(T x) { GXWGFifo.T = x; }
#define FUNC_2PARAM(name, T) \
    void name##2##T(T x, T y) { GXWGFifo.T = x; GXWGFifo.T = y; }
#define FUNC_3PARAM(name, T) \
    void name##3##T(T x, T y, T z) { GXWGFifo.T = x; GXWGFifo.T = y; GXWGFifo.T = z; }
#define FUNC_4PARAM(name, T) \
    void name##4##T(T x, T y, T z, T w) { GXWGFifo.T = x; GXWGFifo.T = y; GXWGFifo.T = z; GXWGFifo.T = w; }
#define FUNC_INDEX8(name) \
    void name##1x8(u8 x) { GXWGFifo.u8 = x; }
#define FUNC_INDEX16(name) \
    void name##1x16(u16 x) { GXWGFifo.u16 = x; }

// GXCmd
FUNC_1PARAM(GXCmd, u8)
FUNC_1PARAM(GXCmd, u16)
FUNC_1PARAM(GXCmd, u32)

// GXParam
FUNC_1PARAM(GXParam, u8)
FUNC_1PARAM(GXParam, u16)
FUNC_1PARAM(GXParam, u32)
FUNC_1PARAM(GXParam, s8)
FUNC_1PARAM(GXParam, s16)
FUNC_1PARAM(GXParam, s32)
FUNC_1PARAM(GXParam, f32)
FUNC_3PARAM(GXParam, f32)
FUNC_4PARAM(GXParam, f32)

// GXPosition
FUNC_3PARAM(GXPosition, f32)
FUNC_3PARAM(GXPosition, u8)
FUNC_3PARAM(GXPosition, s8)
FUNC_3PARAM(GXPosition, u16)
FUNC_3PARAM(GXPosition, s16)
FUNC_2PARAM(GXPosition, f32)
FUNC_2PARAM(GXPosition, u8)
FUNC_2PARAM(GXPosition, s8)
FUNC_2PARAM(GXPosition, u16)
FUNC_2PARAM(GXPosition, s16)
FUNC_INDEX16(GXPosition)
FUNC_INDEX8(GXPosition)

// GXNormal
FUNC_3PARAM(GXNormal, f32)
FUNC_3PARAM(GXNormal, s16)
FUNC_3PARAM(GXNormal, s8)
FUNC_INDEX16(GXNormal)
FUNC_INDEX8(GXNormal)

// GXColor
FUNC_4PARAM(GXColor, u8)
FUNC_1PARAM(GXColor, u32)
FUNC_3PARAM(GXColor, u8)
FUNC_1PARAM(GXColor, u16)
FUNC_INDEX16(GXColor)
FUNC_INDEX8(GXColor)

// GXTexCoord
FUNC_2PARAM(GXTexCoord, f32)
FUNC_2PARAM(GXTexCoord, s16)
FUNC_2PARAM(GXTexCoord, u16)
FUNC_2PARAM(GXTexCoord, s8)
FUNC_2PARAM(GXTexCoord, u8)
FUNC_1PARAM(GXTexCoord, f32)
FUNC_1PARAM(GXTexCoord, s16)
FUNC_1PARAM(GXTexCoord, u16)
FUNC_1PARAM(GXTexCoord, s8)
FUNC_1PARAM(GXTexCoord, u8)
FUNC_INDEX16(GXTexCoord)
FUNC_INDEX8(GXTexCoord)

// GXMatrixIndex
FUNC_1PARAM(GXMatrixIndex, u8)

/* Render modes, verbatim from libs/dolsdk2004/src/gx/GXFrameBuf.c. */
GXRenderModeObj GXNtsc240Ds = {
    1,
    640, 240, 240,
    40, 0,
    640, 480,
    0,
    0,
    0,
    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc240DsAa = {
    1, 640, 240, 240, 40, 0, 640, 480, 0, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc240Int = {
    0, 640, 240, 240, 40, 0, 640, 480, 0, 1, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc240IntAa = {
    0, 640, 240, 240, 40, 0, 640, 480, 0, 1, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc480IntDf = {
    0, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 8, 8, 10, 12, 10, 8, 8 }
};

GXRenderModeObj GXNtsc480Int = {
    0, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc480IntAa = {
    0, 640, 242, 480, 40, 0, 640, 480, 1, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 4, 8, 12, 16, 12, 8, 4 }
};

GXRenderModeObj GXNtsc480Prog = {
    2, 640, 480, 480, 40, 0, 640, 480, 0, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 }
};

GXRenderModeObj GXNtsc480ProgSoft = {
    2, 640, 480, 480, 40, 0, 640, 480, 0, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 8, 8, 10, 12, 10, 8, 8 }
};

GXRenderModeObj GXNtsc480ProgAa = {
    2, 640, 242, 480, 40, 0, 640, 480, 0, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 4, 8, 12, 16, 12, 8, 4 }
};

GXRenderModeObj GXMpal240Ds = {9, 640, 240, 240, 40, 0, 640, 480, 0, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXMpal240DsAa = {9, 640, 240, 240, 40, 0, 640, 480, 0, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXMpal240Int = {8, 640, 240, 240, 40, 0, 640, 480, 0, 1, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXMpal240IntAa = {8, 640, 240, 240, 40, 0, 640, 480, 0, 1, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXMpal480IntDf = {8, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 8, 8, 10, 12, 10, 8, 8 } };
GXRenderModeObj GXMpal480Int = {8, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXMpal480IntAa = {8, 640, 242, 480, 40, 0, 640, 480, 1, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 4, 8, 12, 16, 12, 8, 4 } };
GXRenderModeObj GXPal264Ds = {5, 640, 264, 264, 40, 11, 640, 528, 0, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXPal264DsAa = {5, 640, 264, 264, 40, 11, 640, 528, 0, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXPal264Int = {4, 640, 264, 264, 40, 23, 640, 528, 0, 1, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXPal264IntAa = {4, 640, 264, 264, 40, 23, 640, 528, 0, 1, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXPal528IntDf = {4, 640, 528, 528, 40, 23, 640, 528, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 8, 8, 10, 12, 10, 8, 8 } };
GXRenderModeObj GXPal528Int = {4, 640, 528, 528, 40, 23, 640, 528, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXPal524IntAa = {4, 640, 264, 524, 40, 23, 640, 524, 1, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 4, 8, 12, 16, 12, 8, 4 } };
GXRenderModeObj GXEurgb60Hz240Ds = {21, 640, 240, 240, 40, 0, 640, 480, 0, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXEurgb60Hz240DsAa = {21, 640, 240, 240, 40, 0, 640, 480, 0, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXEurgb60Hz240Int = {20, 640, 240, 240, 40, 0, 640, 480, 0, 1, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXEurgb60Hz240IntAa = {20, 640, 240, 240, 40, 0, 640, 480, 0, 1, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXEurgb60Hz480IntDf = {20, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 8, 8, 10, 12, 10, 8, 8 } };
GXRenderModeObj GXEurgb60Hz480Int = {20, 640, 480, 480, 40, 0, 640, 480, 1, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };
GXRenderModeObj GXEurgb60Hz480IntAa = {20, 640, 242, 480, 40, 0, 640, 480, 1, 0, 1, { 3, 2, 9, 6, 3, 10, 3, 2, 9, 6, 3, 10, 9, 2, 3, 6, 9, 10, 9, 2, 3, 6, 9, 10 }, { 4, 8, 12, 16, 12, 8, 4 } };
GXRenderModeObj GXRmHW = {1, 320, 240, 240, 40, 0, 640, 480, 0, 0, 0, { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 }, { 0, 0, 21, 22, 21, 0, 0 } };

/* Draw sync: nothing is rendered headless, so a token is reached as soon as it
 * is set (the game spins on GXReadDrawSync until its frame token comes back). */
static u16 sDrawSyncToken;

void GXSetDrawSync(u16 token) {
    sDrawSyncToken = token;
}

u16 GXReadDrawSync(void) {
    return sDrawSyncToken;
}
