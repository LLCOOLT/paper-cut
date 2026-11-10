/* Bloom (Graphics > Bloom, docs/PAPER_LOOKS.md): a glow around the bright
 * parts of the 3D scene, drawn with the GameCube's own means between the 3D
 * cameras and the 2D one (camdrv.c camDraw, and interp_aurora.c's in-between
 * frames), so the HUD and the text stay sharp. The frame so far is copied at
 * half size (GXCopyTex with the copy's box filter), then drawn back over
 * itself, added, as a soft disc of taps: the middle and rings around it,
 * each ring's taps close enough together (by distance and around) that the
 * copy's bilinear filter joins them into one smooth glow, each weighed by a
 * Gaussian of its distance. Each tap keeps only what is brighter than the
 * preset's threshold (TEV: the copy minus the threshold, clamped) and weighs
 * it by its share of the preset's strength. Off by default; nothing changes
 * play, replays or online.
 *
 * The copy is of the whole frame as it is (in widescreen Aurora maps the
 * game's 608 units onto the whole widened frame), so it is drawn back in
 * screen space (portScreenSpaceBegin): without it the 3D cameras' widescreen
 * squeeze (and mirror mode's flip), still loaded here, pulled the glow
 * towards the middle, a see-through shifted copy of every bright thing. And a
 * ring of a few far-apart taps (the old 8 at up to 8 pixels) showed as
 * separate copies and streaks, not a glow. */
#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <stdlib.h>
#include "sdk/DEMOInit.h"
#include "port/settings.h"
#include "port/widescreen.h"
#include "wide_x.h"

typedef struct {
    u8 threshold; /* 0-255 */
    u8 strength;  /* 0-255, the sum over the taps */
    f32 radius;   /* in the copy's pixels: the disc's edge (two standard deviations) */
} BloomPreset;

/* Off, Soft, Strong, Dreamy */
static const BloomPreset kPresets[4] = {
    {0, 0, 0.0f},
    {180, 90, 1.5f},
    {150, 150, 2.5f},
    {110, 200, 4.0f},
};

#define MAX_TAPS 64
#define TAP_SPACING 1.5f /* in the copy's pixels: the bilinear filter blends taps this close into one */

typedef struct {
    f32 dx, dy; /* in the copy's pixels */
    u8 k;       /* its weight, of 255 */
} Tap;

static int* sChoice;
static u8 sCopyId; /* GXCopyTex's dest: only an identifier for Aurora */

/* The preset's taps: the middle, then rings out to its radius, every ring's taps evenly around it (each other ring
 * turned half a step, so no line of taps runs outwards), weighed by exp(-2 (d / radius)^2) and together summing to
 * the strength. Returns how many. */
static int bloomTaps(const BloomPreset* p, Tap* taps) {
    f32 w[MAX_TAPS];
    f32 sum = 0.0f, given = 0.0f;
    int n = 0, rings, r, i;
    rings = (int)ceilf(p->radius / TAP_SPACING);
    if (rings < 1) {
        rings = 1;
    }
    taps[n].dx = 0.0f;
    taps[n].dy = 0.0f;
    w[n++] = 1.0f;
    for (r = 1; r <= rings; r++) {
        const f32 d = p->radius * (f32)r / (f32)rings;
        int count = (int)ceilf(6.2831853f * d / TAP_SPACING);
        if (count < 6) {
            count = 6;
        }
        if (n + count > MAX_TAPS) {
            count = MAX_TAPS - n;
        }
        for (i = 0; i < count; i++) {
            const f32 a = 6.2831853f * ((f32)i + ((r & 1) ? 0.5f : 0.0f)) / (f32)count;
            taps[n].dx = cosf(a) * d;
            taps[n].dy = sinf(a) * d;
            w[n++] = expf(-2.0f * (d / p->radius) * (d / p->radius));
        }
    }
    for (i = 0; i < n; i++) {
        sum += w[i];
    }
    /* rounded so that the weights given so far stay as near the strength's share as they can (small weights would
     * all round the same way) */
    for (i = 0; i < n; i++) {
        const f32 want = (f32)p->strength * w[i] / sum;
        const f32 before = given;
        given += want;
        taps[i].k = (u8)((int)floorf(given + 0.5f) - (int)floorf(before + 0.5f));
    }
    return n;
}

/* The weighted centre of the taps, in the copy's pixels (0, 0: centred on what glows), for the test log. */
static void bloomTapsCentre(const Tap* taps, int n, f32* cx, f32* cy) {
    f32 sx = 0.0f, sy = 0.0f, sk = 0.0f;
    int i;
    for (i = 0; i < n; i++) {
        sx += taps[i].dx * (f32)taps[i].k;
        sy += taps[i].dy * (f32)taps[i].k;
        sk += (f32)taps[i].k;
    }
    *cx = sk > 0.0f ? sx / sk : 0.0f;
    *cy = sk > 0.0f ? sy / sk : 0.0f;
}

static void tap(f32 x0, f32 y0, f32 x1, f32 y1) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x0, y0, -1.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(x1, y0, -1.0f);
    GXTexCoord2f32(1.0f, 0.0f);
    GXPosition3f32(x1, y1, -1.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(x0, y1, -1.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXEnd();
}

void portBloomApply(void) {
    static Tap sTaps[MAX_TAPS];
    static int sTapCount, sTapsFor;
    GXRenderModeObj* rmode;
    const BloomPreset* p;
    GXTexObj tex;
    Mtx44 proj;
    Mtx ident;
    u16 w, h, cw, ch;
    int i;
    if (sChoice == NULL) {
        sChoice = portSettingsDeclare("bloom", 0, 0, 3);
    }
    if (*sChoice <= 0 || *sChoice > 3) {
        return;
    }
    p = &kPresets[*sChoice];
    if (sTapsFor != *sChoice) {
        sTapsFor = *sChoice;
        sTapCount = bloomTaps(p, sTaps);
    }
    {
        static int sTold; /* each preset, once */
        if (sTold != *sChoice) {
            f32 cx, cy;
            sTold = *sChoice;
            printf("[bloom] preset %d\n", *sChoice);
            bloomTapsCentre(sTaps, sTapCount, &cx, &cy);
            /* the draw's x scale is always 1 (screen space); the widescreen squeeze it undoes, for the log */
            printf("[bloom] %d taps, radius %.2f, centre %.3f %.3f, drawn at x scale 1 (cameras' %.4f)\n", sTapCount,
                   p->radius, cx, cy, portWidescreenScaleX());
        }
    }
    rmode = DEMOGetRenderModeObj();
    w = rmode->fbWidth;
    h = rmode->efbHeight;

    /* the 3D scene so far, at the game's own half size in real pixels: Aurora sizes a copy by the internal
     * resolution (a 4x frame's half copy is 4x the texels), so it is asked for that much smaller; the glow is then
     * the same at every internal resolution and supersampling, its taps never farther apart than the copy's
     * bilinear filter joins (at 4x they were 4 texels apart: separate copies of each bright thing) */
    {
        u32 rw = 0, rh = 0;
        f32 s;
        AuroraGetRenderSize(&rw, &rh);
        s = rh > 0 && h > 0 ? (f32)rh / (f32)h : 1.0f;
        if (!(s >= 1.0f)) {
            s = 1.0f;
        }
        cw = (u16)(w / 2.0f / s + 0.5f);
        ch = (u16)(h / 2.0f / s + 0.5f);
        cw = cw < 1 ? 1 : cw;
        ch = ch < 1 ? 1 : ch;
    }
    GXSetTexCopySrc(0, 0, w, h);
    GXSetTexCopyDst(cw, ch, GX_TF_RGBA8, GX_TRUE);
    GXCopyTex(&sCopyId, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&tex, &sCopyId, cw, ch, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&tex, GX_TEXMAP0);

    /* drawn over the whole frame, in its pixels: the frame as it is, not squeezed or flipped as the cameras' */
    portScreenSpaceBegin();
    GXSetViewport(0.0f, 0.0f, (f32)w, (f32)h, 0.0f, 1.0f);
    GXSetScissor(0, 0, w, h);
    MTXOrtho(proj, 0.0f, (f32)h, 0.0f, (f32)w, 0.1f, 10.0f);
    GXSetProjection(proj, GX_ORTHOGRAPHIC);
    MTXIdentity(ident);
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumChans(0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(2);
    /* stage 0: the copy minus the threshold, clamped, doubled */
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, (GXColor){p->threshold, p->threshold, p->threshold, 0});
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    /* stage 1: weighed by the tap's share of the strength (C1, set per tap) */
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_C1, GX_CC_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0, 0, 0, 0});
    for (i = 0; i < sTapCount; i++) {
        /* in the frame's pixels: twice the copy's */
        const f32 dx = sTaps[i].dx * 2.0f, dy = sTaps[i].dy * 2.0f;
        if (sTaps[i].k == 0) {
            continue;
        }
        GXSetTevColor(GX_TEVREG1, (GXColor){sTaps[i].k, sTaps[i].k, sTaps[i].k, 0});
        tap(dx, dy, (f32)w + dx, (f32)h + dy);
    }
    portScreenSpaceEnd();
    /* as the game's draws expect to find it */
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetNumChans(1);
}
