/* Display options of v0.2 (port/include/port/display.h): applied once a frame
 * from their settings (port/settings/features/display.cpp), and the frame
 * timing the performance overlay draws. */
#include <stdio.h>
#include <stdlib.h>
#include <aurora/aurora.h>
#include <aurora/color.h>
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
static int* sDalton;    /* colorblind: 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia (Accessibility) */
static int* sDaltonStrength; /* colorblind_strength: 0-100 % of the correction */

/* what Aurora was last given (-1: nothing yet) */
static int sAnisoSet = -1, sLodSet = -1, sSuperSet = -1, sPostSet = -1, sVrrSet = -1;
static int sHdrSet = -1, sHdrWhiteSet = -1, sHdrBoostSet = -1;
static int sDaltonSet = -1, sDaltonStrengthSet = -1;

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
    sDalton = portSettingsDeclare("colorblind", 0, 0, 3);
    sDaltonStrength = portSettingsDeclare("colorblind_strength", 100, 0, 100);
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

/* Colour-blind filters: a daltonization correction, not a simulation. The colours a viewer of that type
 * confuses are found by simulating the type (the picture as they see it) and the difference, what they
 * lose, is shifted into channels they can tell apart: Fidaner, Lin and Ozguven, "Analysis of Color
 * Blindness" (Stanford, 1999), as daltonize.js and Vischeck have it. All in gamma-encoded RGB (0-1), the
 * same three steps for each pixel make one 3x3 matrix:
 *   simulated = LMS2RGB * TYPE * RGB2LMS * rgb
 *   error     = rgb - simulated
 *   corrected = rgb + SHIFT * error * strength
 * so corrected = (I + strength * SHIFT * (I - LMS2RGB * TYPE * RGB2LMS)) * rgb, applied to the whole
 * frame by Aurora's present pass (aurora_set_color_matrix, port/patches/aurora/0020). */
static const float kRgbToLms[3][3] = {
    {17.8824f, 43.5161f, 4.11935f},
    {3.45565f, 27.1554f, 3.86714f},
    {0.0299566f, 0.184309f, 1.46709f},
};
static const float kLmsToRgb[3][3] = {
    {0.0809444479f, -0.130504409f, 0.116721066f},
    {-0.0102485335f, 0.0540193266f, -0.113614708f},
    {-0.000365296938f, -0.00412161469f, 0.693511405f},
};
/* each type's missing cone rebuilt from the other two (Fidaner et al.) */
static const float kDaltonType[3][3][3] = {
    {{0.0f, 2.02344f, -2.52581f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},     /* protanopia: no L (red) cones */
    {{1.0f, 0.0f, 0.0f}, {0.494207f, 0.0f, 1.24827f}, {0.0f, 0.0f, 1.0f}},     /* deuteranopia: no M (green) */
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {-0.395913f, 0.801109f, 0.0f}},   /* tritanopia: no S (blue) */
};
/* the error shifted: red's into green and blue, green's and blue's kept */
static const float kDaltonShift[3][3] = {
    {0.0f, 0.0f, 0.0f},
    {0.7f, 1.0f, 0.0f},
    {0.7f, 0.0f, 1.0f},
};

static void mul3(const float a[3][3], const float b[3][3], float out[3][3]) {
    int i, j, k;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            float s = 0.0f;
            for (k = 0; k < 3; k++) {
                s += a[i][k] * b[k][j];
            }
            out[i][j] = s;
        }
    }
}

static void portDaltonMatrix(int type, float strength, float out[9]) {
    float t[3][3], sim[3][3], err[3][3], shifted[3][3];
    int i, j;
    mul3(kDaltonType[type - 1], kRgbToLms, t);
    mul3(kLmsToRgb, t, sim);
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            err[i][j] = (i == j ? 1.0f : 0.0f) - sim[i][j];
        }
    }
    mul3(kDaltonShift, err, shifted);
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out[i * 3 + j] = (i == j ? 1.0f : 0.0f) + strength * shifted[i][j];
        }
    }
}

static void daltonApply(void) {
    float m[9];
    if (*sDalton == sDaltonSet && *sDaltonStrength == sDaltonStrengthSet) {
        return;
    }
    sDaltonSet = *sDalton;
    sDaltonStrengthSet = *sDaltonStrength;
    if (sDaltonSet <= 0 || sDaltonSet > 3 || sDaltonStrengthSet <= 0) {
        aurora_set_color_matrix(NULL);
        return;
    }
    portDaltonMatrix(sDaltonSet, (float)sDaltonStrengthSet / 100.0f, m);
    aurora_set_color_matrix(m);
    if (getenv("TTYD_DALTON_LOG") != NULL) {
        printf("[colorblind] type %d at %d%%: %.4f %.4f %.4f / %.4f %.4f %.4f / %.4f %.4f %.4f\n", sDaltonSet,
               sDaltonStrengthSet, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        fflush(stdout);
    }
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
    daltonApply();
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
