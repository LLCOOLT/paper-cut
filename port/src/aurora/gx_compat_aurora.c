/* Adapters from 2004-SDK call shapes to Aurora's API (see port/aurora_compat.h). */
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>
#include "port/aurora_compat.h"
#include "port/platform.h"

static void portNanArray(const void* data, unsigned int size, void* caller);

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
    int sized;

    /* $TTYD_GXARRAY_TRACE read once: a getenv here, on every array the game sets (thousands a frame), was a
     * quarter of the game thread's time in a busy room (the CRT walks the whole environment each time) */
    static const char* sTrace = (const char*)1;
    if (sTrace == (const char*)1) {
        sTrace = getenv("TTYD_GXARRAY_TRACE");
    }
    portGXArrayInfo(data, &size, &le);
    sized = size != 0 || arrayBytes != 0;
    if (size == 0 && arrayBytes != 0) {
        size = arrayBytes; /* a static array: its exact size (aurora_compat.h) */
    }
    if (size > want && want != 0) {
        size = want;
    }
    if (size != 0 && mem1Lo != 0 && p >= mem1Lo && p < mem1Hi && size > mem1Hi - p) {
        size = (u32)(mem1Hi - p); /* (a caller's window never runs off game memory) */
    }
    if (size == 0 && data != NULL) {
        if (mem1Lo != 0 && p >= mem1Lo && p < mem1Hi) {
            size = (mem1Hi - p) < want ? (u32)(mem1Hi - p) : want;
        } else {
            size = want < 0x10000u ? want : 0x10000u;
        }
        if (sTrace != NULL) {
            char name[160];
            printf("[gxarray] unsized attr %d stride %u -> %u bytes from %s\n", attr, (unsigned)stride, size,
                   portSymbolName(__builtin_return_address(0), name, sizeof(name)));
        }
    }
    {
        const char* v = sTrace;
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
    if (portNanCheckOn && sized && attr == GX_VA_POS && le && stride == 12 && data != NULL) {
        /* $TTYD_NAN_CHECK: a position array the game built (skinned vertices, effects), of a known size */
        portNanArray(data, size, __builtin_return_address(0));
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

/* GXProject with a finite answer (aurora_overlay GXTransform.h): a point on the
 * camera's plane (w = 0) or a NaN in the inputs gave infinities or NaN, which
 * the game draws with; RADV draws a NaN quad over half the screen where other
 * GPUs drop it. Far off screen, on the side the point was, and NaN as 0. */
#undef GXProject
static f32 finiteScreen(f32 v) {
    const f32 limit = 100000.0f;
    if (v != v) {
        return 0.0f;
    }
    return v > limit ? limit : v < -limit ? -limit : v;
}

/* Non-finite values on their way to the GPU, reported as "[nan] ..." (tests:
 * tests/port/testpass/bug_white_half_*.json fail on one). GXProject's are
 * always checked (once per call); with $TTYD_NAN_CHECK=1 also every vertex
 * position, normal and texture coordinate the game sends one by one
 * (aurora_overlay GXVert.h) and every projection and position matrix
 * (GXTransform.h). A few lines per place in the code, then a count. */
int portNanCheckOn;

__attribute__((constructor)) static void portNanCheckEarly(void) {
    const char* v = getenv("TTYD_NAN_CHECK");
    portNanCheckOn = v != NULL && v[0] != '\0' && v[0] != '0';
    if (portNanCheckOn) {
        printf("[nan] check on: vertices, matrices and GXProject\n");
    }
}

static int nanBad(f32 v) {
    union { f32 f; u32 u; } b;
    b.f = v;
    return (b.u & 0x7F800000u) == 0x7F800000u;
}

/* FALSE once this place has been reported enough (its first 3, 400 in all). Only the reports shown count
 * towards the 400: a place muted after its 3 (a far GXProject note every frame, which is no NaN) doesn't
 * bring on the "[nan] more than 400" line, which a check for NaN takes for one */
static BOOL nanReportHere(const char* file, int line) {
    static struct { const char* file; int line, n; } sites[128];
    static int nSites, total;
    int i;
    for (i = 0; i < nSites; i++) {
        if (sites[i].line == line && sites[i].file == file) {
            break;
        }
    }
    if (i < nSites && sites[i].n >= 3) {
        return FALSE;
    }
    if (++total > 400) {
        if (total == 401) {
            printf("[nan] more than 400 reports: no more\n");
        }
        return FALSE;
    }
    if (i < nSites) {
        sites[i].n++;
        return TRUE;
    }
    if (nSites < 128) {
        sites[nSites].file = file;
        sites[nSites].line = line;
        sites[nSites++].n = 1;
    }
    return TRUE;
}

static const char* nanWhen(void) {
    return portInterpInBetween() ? " (in-between frame)" : "";
}

void portNanVertex(const char* what, f32 x, f32 y, f32 z, const char* file, int line) {
    if (nanReportHere(file, line)) {
        printf("[nan] %s (%g, %g, %g) from %s:%d%s\n", what, x, y, z, file != NULL ? file : "?", line, nanWhen());
        fflush(stdout);
    }
}

void portNanMatrix(const char* what, const void* m, int rows, const char* file, int line) {
    const f32* f = (const f32*)m;
    int i;
    if (f == NULL) {
        return;
    }
    for (i = 0; i < rows * 4; i++) {
        if (nanBad(f[i])) {
            if (nanReportHere(file, line)) {
                printf("[nan] %s [%d][%d] = %g from %s:%d%s\n", what, i / 4, i % 4, f[i], file != NULL ? file : "?",
                       line, nanWhen());
                fflush(stdout);
            }
            return;
        }
    }
}

static void portNanArray(const void* data, unsigned int size, void* caller) {
    const f32* f = (const f32*)data;
    unsigned int i, n = size / 4;
    for (i = 0; i < n; i++) {
        if (nanBad(f[i])) {
            char name[160];
            portSymbolName(caller, name, sizeof(name));
            if (nanReportHere((const char*)caller, 0)) {
                printf("[nan] position array %p [%u] = %g (vertex %u of %u) from %s%s\n", data, i, f[i], i / 3,
                       n / 3, name, nanWhen());
                fflush(stdout);
            }
            return;
        }
    }
}

void portGXProjectAt(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx, f32* sy,
                     f32* sz, const char* file, int line) {
    GXProject(x, y, z, mtx, pm, vp, sx, sy, sz);
    if (nanBad(*sx) || nanBad(*sy) || nanBad(*sz)) {
        if (nanReportHere(file, line)) {
            int bad = nanBad(x) || nanBad(y) || nanBad(z), i;
            for (i = 0; i < 12; i++) {
                bad |= nanBad(mtx[i / 4][i % 4]) << 1;
            }
            for (i = 0; i < GX_PROJECTION_SZ; i++) {
                bad |= nanBad(pm[i]) << 2;
            }
            printf("[nan] GXProject (%g, %g, %g) -> (%g, %g, %g)%s%s%s from %s:%d%s\n", x, y, z, *sx, *sy, *sz,
                   bad & 1 ? " point" : "", bad & 2 ? " view" : "", bad & 4 ? " projection" : "",
                   file != NULL ? file : "?", line, nanWhen());
            fflush(stdout);
        }
    } else if (portNanCheckOn && (__builtin_fabsf(*sx) > 100000.0f || __builtin_fabsf(*sy) > 100000.0f) &&
               nanReportHere(file, line)) {
        /* finite but far off (a point near the camera's plane): clamped below; a note, not a failure */
        printf("[gxproject] far (%g, %g, %g) -> (%g, %g) from %s:%d%s\n", x, y, z, *sx, *sy, file != NULL ? file : "?",
               line, nanWhen());
        fflush(stdout);
    }
    *sx = finiteScreen(*sx);
    *sy = finiteScreen(*sy);
    *sz = finiteScreen(*sz);
}

void portGXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx, f32* sy,
                   f32* sz) {
    portGXProjectAt(x, y, z, mtx, pm, vp, sx, sy, sz, NULL, 0);
}

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
