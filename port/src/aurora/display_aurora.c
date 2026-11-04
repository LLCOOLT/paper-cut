/* Display options of v0.2 (port/include/port/display.h): applied once a frame
 * from their settings (port/settings/features/display.cpp), and the frame
 * timing the performance overlay draws. */
#include <stdio.h>
#include <stdlib.h>
#include <aurora/aurora.h>
#include <aurora/display.h>
#include <aurora/hdr.h>
#include <SDL3/SDL_timer.h>
#include "port/display.h"
#include "port/settings.h"
#include "port/widescreen.h"

static int* sAniso;     /* aniso: 0 the game's, 1-4 2x 4x 8x 16x */
static int* sLodBias;   /* lod_bias: PORT_LOD_* */
static int* sSuper;     /* supersample: 0 off, 1-3 2x 3x 4x */
static int* sPost;      /* post_filter: 0 off, 1 CRT, 2 paper grain, 3 sharpen */
static int* sVrr;       /* vrr */
static int* sPerf;      /* perf_overlay: 0 off, 1 the frame-time graph, 2 with numbers */
static int* sHdr;       /* hdr: HDR output where supported */
static int* sHdrWhite;  /* hdr_paper_white: the SDR picture's white, in nits */
static int* sHdrBoost;  /* hdr_boost: highlights raised by up to this percent of paper white */

/* what Aurora was last given (-1: nothing yet) */
static int sAnisoSet = -1, sLodSet = -1, sSuperSet = -1, sPostSet = -1, sVrrSet = -1;
static int sHdrSet = -1, sHdrWhiteSet = -1, sHdrBoostSet = -1;

/* frame timing: the time between presents, and the part of it spent
 * computing and drawing (the rest waits: pacing, vsync) */
static float sInterval[PORT_PERF_SAMPLES];
static float sWork[PORT_PERF_SAMPLES];
static int sNext, sCount;
static Uint64 sLastEnd, sStart, sWorkEnd;
static FILE* sPacingLog; /* $TTYD_PACING_LOG: one line a frame, interval and work in ms */

static void declare(void) {
    if (sAniso != NULL) {
        return;
    }
    sAniso = portSettingsDeclare("aniso", 0, 0, 4);
    sLodBias = portSettingsDeclare("lod_bias", PORT_LOD_GAME, 0, PORT_LOD_COUNT - 1);
    sSuper = portSettingsDeclare("supersample", 0, 0, 3);
    sPost = portSettingsDeclare("post_filter", 0, 0, 3);
    sVrr = portSettingsDeclare("vrr", 0, 0, 1);
    sPerf = portSettingsDeclare("perf_overlay", 0, 0, 2);
    sHdr = portSettingsDeclare("hdr", 0, 0, 1);
    sHdrWhite = portSettingsDeclare("hdr_paper_white", 200, 80, 400);
    sHdrBoost = portSettingsDeclare("hdr_boost", 0, 0, 50);
    portTextureUpscaleStart(); /* once, before the game's first textures load */
    {
        const char* path = getenv("TTYD_PACING_LOG");
        if (path != NULL && path[0] != '\0') {
            sPacingLog = fopen(path, "w");
            if (sPacingLog != NULL) {
                fprintf(sPacingLog, "interval_ms,work_ms\n");
            }
        }
    }
}

static float lodBias(int choice) {
    static const float kBias[PORT_LOD_COUNT] = {0.0f, -0.5f, -1.0f, -1.5f, 0.5f};
    return kBias[choice];
}

void portDisplayInit(void) {
    declare();
}

void portHdrApply(void) {
    declare();
    if (*sHdr == sHdrSet && *sHdrWhite == sHdrWhiteSet && *sHdrBoost == sHdrBoostSet) {
        return;
    }
    sHdrSet = *sHdr;
    sHdrWhiteSet = *sHdrWhite;
    sHdrBoostSet = *sHdrBoost;
    /* off: Aurora is left as it is (SDR) unless HDR was on */
    aurora_set_hdr(sHdrSet != 0, (float)sHdrWhiteSet, (float)sHdrBoostSet / 100.0f);
}

int portHdrSupported(void) {
    return aurora_hdr_supported() ? 1 : 0;
}

int portHdrActive(void) {
    return aurora_hdr_active() ? 1 : 0;
}

void portDisplayFrameStart(void) {
    sStart = SDL_GetTicksNS();
    sWorkEnd = 0;
}

void portDisplayWorkDone(void) {
    sWorkEnd = SDL_GetTicksNS();
}

void portDisplayFrame(void) {
    const Uint64 now = SDL_GetTicksNS();
    declare();
    portWidescreenFrame();
    portSharpTextFrame();
    if (*sAniso != sAnisoSet) {
        sAnisoSet = *sAniso;
        AuroraSetTextureAnisotropy((u8)(sAnisoSet == 0 ? 1 : 1 << sAnisoSet));
    }
    if (*sLodBias != sLodSet) {
        sLodSet = *sLodBias;
        AuroraSetTextureLodBias(lodBias(sLodSet));
    }
    if (*sSuper != sSuperSet) {
        sSuperSet = *sSuper;
        aurora_set_supersample(sSuperSet + 1);
    }
    if (*sPost != sPostSet) {
        sPostSet = *sPost;
        aurora_set_post_filter(sPostSet);
    }
    if (*sVrr != sVrrSet) {
        sVrrSet = *sVrr;
        aurora_set_vrr(sVrrSet != 0);
    }
    portHdrApply();
    if (sLastEnd != 0 && sStart != 0) {
        const float interval = (float)(now - sLastEnd) / 1e6f;
        const Uint64 from = sStart > sLastEnd ? sStart : sLastEnd;
        const Uint64 to = sWorkEnd > from ? sWorkEnd : now;
        const float work = (float)(to - from) / 1e6f;
        sInterval[sNext] = interval;
        sWork[sNext] = work;
        sNext = (sNext + 1) % PORT_PERF_SAMPLES;
        sCount += sCount < PORT_PERF_SAMPLES;
        if (sPacingLog != NULL) {
            fprintf(sPacingLog, "%.3f,%.3f\n", interval, work);
        }
    }
    sLastEnd = now;
}

int portPerfOverlay(void) {
    declare();
    return *sPerf;
}

int portPerfSamples(float* interval, float* work, int max) {
    int i, n = sCount < max ? sCount : max;
    for (i = 0; i < n; i++) {
        const int at = (sNext - n + i + PORT_PERF_SAMPLES) % PORT_PERF_SAMPLES;
        interval[i] = sInterval[at];
        work[i] = sWork[at];
    }
    return n;
}
