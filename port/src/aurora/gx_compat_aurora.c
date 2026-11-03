/* Adapters from 2004-SDK call shapes to Aurora's API (see port/aurora_compat.h). */
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/aurora_compat.h"
#include "port/platform.h"

/* Bytes Aurora copies for an array the game passed without a size, and its
 * endianness (port/gx_arrays.h: disc arrays big-endian, code-built native).
 * Aurora uploads [data, data + size) and bounds-checks every index against
 * it, so the size must cover the largest index used and must not run off
 * mapped memory. Order: a registered size (array, or the rest of its disc
 * blob, capped at 65536 entries); else, inside emulated MEM1, up to 65536
 * entries clamped to MEM1's end; else (static data in the executable) a
 * 64 KiB window, which covers the game's built-in vertex tables. */
void portGXSetArraySized(int attr, const void* data, unsigned char stride, unsigned int arrayBytes) {
    uintptr_t p = (uintptr_t)data;
    uintptr_t mem1Lo = OSBaseAddress;
    uintptr_t mem1Hi = mem1Lo + OSGetPhysicalMemSize();
    u32 want = (u32)stride * 0x10000u;
    unsigned int size = 0;
    int le = 1;

    portGXArrayInfo(data, &size, &le);
    if (size == 0 && arrayBytes != 0) {
        size = arrayBytes; /* a static array: its exact size (aurora_compat.h) */
    }
    if (size > want && want != 0) {
        size = want;
    }
    if (size == 0 && data != NULL) {
        if (mem1Lo != 0 && p >= mem1Lo && p < mem1Hi) {
            size = (mem1Hi - p) < want ? (u32)(mem1Hi - p) : want;
        } else {
            size = want < 0x10000u ? want : 0x10000u;
        }
        if (getenv("TTYD_GXARRAY_TRACE") != NULL) {
            char name[160];
            printf("[gxarray] unsized attr %d stride %u -> %u bytes from %s\n", attr, (unsigned)stride, size,
                   portSymbolName(__builtin_return_address(0), name, sizeof(name)));
        }
    }
    {
        const char* v = getenv("TTYD_GXARRAY_TRACE");
        static const void* last[32];
        static unsigned int lastSize[32];
        if (v != NULL && v[0] == '2' && attr >= 0 && attr < 32 && (last[attr] != data || lastSize[attr] != size)) {
            char name[160];
            last[attr] = data;
            lastSize[attr] = size;
            printf("[gxarray] attr %d %u bytes from %s (frame %u)\n", attr, size,
                   portSymbolName(__builtin_return_address(0), name, sizeof(name)), (unsigned)VIGetRetraceCount());
        }
    }
    GXSetArray((GXAttr)attr, data, size, stride, le != 0);
}

/* EFB color peek over Aurora's asynchronous readback: returns the value the
 * previous request produced (a frame or two old), then requests a new one. */
void GXPeekARGB(unsigned short x, unsigned short y, unsigned int* color) {
    static AuroraReadbackStream sStream;
    static u32 sLast;
    AuroraReadbackDest dest = { 0, &sLast, sizeof(u32), GX_FALSE };
    AuroraReadbackPeekRequest req = { 0, AURORA_READBACK_PEEK_ARGB, x, y };

    if (sStream == 0) {
        sStream = AuroraReadbackCreateStream();
    }
    if (sStream != 0) {
        AuroraReadbackGet(sStream, &dest, 1);
        AuroraReadbackCapturePeeks(sStream, &req, 1);
    }
    *color = sLast;
}

/* The 2004 SDK's EURGB60 mode (GXFrameBuf.c); Aurora defines the others. */
GXRenderModeObj GXEurgb60Hz480IntDf = {
    (VITVMode)20, 640, 480, 480, 40, 0, 640, 480, VI_XFBMODE_DF, 0, 0,
    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 8, 8, 10, 12, 10, 8, 8 },
};

/* Logic ops Aurora lacks, as blends that agree on the game's 0/255 masks:
 * OR as an additive blend (shadowdrv.c accumulates shadow masks with it),
 * AND as a multiply. */
#undef GXSetBlendMode
void portGXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op) {
    if (type == GX_BM_LOGIC) {
        switch (op) {
        case GX_LO_OR:
            GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_COPY);
            return;
        case GX_LO_AND:
            GXSetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_COPY);
            return;
        default:
            break;
        }
    }
    GXSetBlendMode(type, src_factor, dst_factor, op);
}

/* Texgen calls with a source GX does not have (port aurora_overlay
 * GXGeometry.h): report the caller once per site, use GX_TG_TEX0. */
#undef GXSetTexCoordGen2
void portGXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx,
                           GXBool normalize, u32 postmtx) {
    if ((u32)src_param >= GX_MAX_TEXGENSRC) {
        static const void* seen[16];
        const void* caller = __builtin_return_address(0);
        int i;
        for (i = 0; i < 16 && seen[i] != NULL && seen[i] != caller; i++) {
        }
        if (i < 16 && seen[i] == NULL) {
            char name[160];
            seen[i] = caller;
            printf("[gx] invalid texgen source %d (coord %d, func %d, mtx %u) from %s\n", (int)src_param,
                   (int)dst_coord, (int)func, (unsigned)mtx, portSymbolName(caller, name, sizeof(name)));
            fflush(stdout);
        }
        src_param = GX_TG_TEX0;
    }
    GXSetTexCoordGen2(dst_coord, func, src_param, mtx, normalize, postmtx);
}
