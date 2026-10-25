/* 2004-SDK functions the game calls that Aurora leaves to the application
 * (its OSReport.cpp is compiled out) or does not have: reporting, interrupt
 * masking, the IPL font, reset, and GX calls with no WebGPU counterpart
 * (performance metrics, bounding box, texture preloading, draw sync).
 * Behavior matches the headless backend (port/src/headless/sdk/os_pc.c,
 * gx_fifo_pc.c, stubs_generated.c) unless noted. */
#include <dolphin/os.h>
#include <dolphin/gx.h>
#include <dolphin/exi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

GXRenderModeObj* DEMOGetRenderModeObj(void); /* sdk/DEMOInit.c: the game's render mode */

#define STUB_ONCE(name)                                 \
    do {                                                \
        static int sLogged;                             \
        if (!sLogged) {                                 \
            sLogged = 1;                                \
            fprintf(stderr, "STUB " #name "\n");        \
        }                                               \
    } while (0)

/* ---- reporting */

void OSVReport(const char* msg, va_list list) {
    vfprintf(stderr, msg, list);
}

void OSReport(const char* msg, ...) {
    va_list list;
    va_start(list, msg);
    OSVReport(msg, list);
    va_end(list);
}

void OSPanic(const char* file, int line, const char* msg, ...) {
    va_list list;
    fprintf(stderr, "OSPanic %s:%d: ", file, line);
    va_start(list, msg);
    vfprintf(stderr, msg, list);
    va_end(list);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

/* ---- interrupts: the game loop is single-threaded; MusyX's audio thread
 * has its own lock (hw_pc.c). */

BOOL OSDisableInterrupts(void) { return TRUE; }
BOOL OSEnableInterrupts(void) { return TRUE; }
BOOL OSRestoreInterrupts(BOOL level) { return level; }

/* ---- errors, reset, settings. Crashes go to port/src/common/crash_pc.c. */

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    (void)error;
    (void)handler;
    return NULL;
}

void OSFillFPUContext(OSContext* context) { (void)context; }

/* A cold boot: no restart code, the reset button never held. */
u32 OSGetResetCode(void) { return 0; }
BOOL OSGetResetButtonState(void) { return FALSE; }

void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) {
    (void)reset;
    (void)resetCode;
    (void)forceMenu;
    STUB_ONCE(OSResetSystem);
}

/* SRAM settings: mono sound, interlaced video, as a fresh GameCube. */
static u32 sSoundMode;
static u32 sProgressiveMode;
u32 OSGetSoundMode(void) { return sSoundMode; }
void OSSetSoundMode(u32 mode) { sSoundMode = mode; }
u32 OSGetProgressiveMode(void) { return sProgressiveMode; }
void OSSetProgressiveMode(u32 on) { sProgressiveMode = on; }

/* ---- IPL ROM font: none on PC (as headless). */

BOOL OSInitFont(OSFontHeader* fontData) {
    (void)fontData;
    STUB_ONCE(OSInitFont);
    return FALSE;
}

u32 OSLoadFont(OSFontHeader* fontData, void* tmp) {
    (void)fontData;
    (void)tmp;
    STUB_ONCE(OSLoadFont);
    return 0;
}

char* OSGetFontTexel(const char* string, void* image, s32 pos, s32 stride, s32* width) {
    (void)string;
    (void)image;
    (void)pos;
    (void)stride;
    (void)width;
    STUB_ONCE(OSGetFontTexel);
    return NULL;
}

char* OSGetFontTexture(const char* string, void** image, s32* x, s32* y, s32* width) {
    (void)string;
    (void)image;
    (void)x;
    (void)y;
    (void)width;
    STUB_ONCE(OSGetFontTexture);
    return NULL;
}

/* ---- EXI: nothing attached. */

s32 EXIGetType(s32 chan, u32 dev, u32* type) {
    (void)chan;
    (void)dev;
    (void)type;
    return 0;
}

/* ---- DEMO statistics (GX performance counters): none. */

void DEMOUpdateStats(GXBool inc) { (void)inc; }
void DEMOPrintStats(void) {}

/* ---- GX */

/* Draw sync: the game spins on GXReadDrawSync until its token comes back;
 * Aurora's draws are queued in order, so the token is back at once. */
static u16 sDrawSyncToken;
void GXSetDrawSync(u16 token) { sDrawSyncToken = token; }
u16 GXReadDrawSync(void) { return sDrawSyncToken; }

/* The CPU never sees the GP FIFO on Aurora. */
void* GXGetFifoBase(const GXFifoObj* fifo) {
    (void)fifo;
    return NULL;
}
u32 GXGetFifoSize(const GXFifoObj* fifo) {
    (void)fifo;
    return 0;
}
u32 GXGetOverflowCount(void) { return 0; }
u32 GXResetOverflowCount(void) { return 0; }
void GXResetWriteGatherPipe(void) {}
void GXAbortFrame(void) {}

void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1) {
    (void)perf0;
    (void)perf1;
}
void GXClearGPMetric(void) {}
void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy, u32* clocks) {
    *xf_wait_in = 0;
    *xf_wait_out = 0;
    *ras_busy = 0;
    *clocks = 0;
}

/* Bounding box (pixels drawn since the clear): not tracked. Reports an
 * empty box (left > right), as the hardware does when nothing was drawn. */

/* Depth scale/offset for the next draws (used by depth-of-field and fades).
 * Aurora has no counterpart yet. */
void GXSetZScaleOffset(f32 scale, f32 offset) {
    (void)scale;
    (void)offset;
    STUB_ONCE(GXSetZScaleOffset);
}

void __GXSetIndirectMask(u32 mask) { (void)mask; }

/* TMEM preloading: Aurora manages texture memory itself, so a preloaded
 * texture is simply loaded when bound. */
void GXInitTexPreLoadRegion(GXTexRegion* region, u32 tmem_even, u32 size_even, u32 tmem_odd, u32 size_odd) {
    (void)region;
    (void)tmem_even;
    (void)size_even;
    (void)tmem_odd;
    (void)size_odd;
}

void GXPreLoadEntireTexture(GXTexObj* tex_obj, GXTexRegion* region) {
    (void)tex_obj;
    (void)region;
}

void GXLoadTexObjPreLoaded(GXTexObj* obj, GXTexRegion* region, GXTexMapID id) {
    (void)region;
    GXLoadTexObj(obj, id);
}
