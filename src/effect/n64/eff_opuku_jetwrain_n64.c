// Paper Mario 64 effect: rain of water (jetwrain) from a water jet.
#include "eff/n64/eff_opuku_jetwrain_n64.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/effdrv.h"
#include "memory.h"
#include "system.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include "eff/eff_n64.h"
#include "libc_decls.h"

typedef struct EffOpukuJetwrainWork {
    s32 type;      // 0x0
    Vec position;  // 0x4
    s32 lifetime;  // 0x10
    s32 frame;     // 0x14
    f32 scale;     // 0x18
    u8 col[3];     // 0x1C
    u8 alpha;      // 0x1F
    u8 col2[3];    // 0x20
    s8 h[19][13];  // 0x23
    s16 v[19][13]; // 0x11A
} EffOpukuJetwrainWork;

/**
 * Four screen tiles (corner, right edge, bottom edge, both edges) whose corners are rewritten from the wave
 * heights before each tile of the copied frame is redrawn, giving the rippling-water warp.
 */
static effN64Vtx opuku_jetwrain_vtx[16] = {
    {-160, -120, -320, 0, 0, 0, 0, 0, 255},
    {-144, -120, -320, 0x200, 0, 0, 0, 0, 255},
    {-160, -104, -320, 0, 0x200, 0, 0, 0, 255},
    {-144, -104, -320, 0x200, 0x200, 0, 0, 0, 255},
    {-160, -120, -320, 0, 0, 0, 0, 0, 255},
    {-144, -120, -320, 0x280, 0, 0, 0, 0, 255},
    {-160, -104, -320, 0, 0x200, 0, 0, 0, 255},
    {-144, -104, -320, 0x280, 0x200, 0, 0, 0, 255},
    {-160, -120, -320, 0, 0, 0, 0, 0, 255},
    {-144, -120, -320, 0x200, 0, 0, 0, 0, 255},
    {-160, -104, -320, 0, 0x280, 0, 0, 0, 255},
    {-144, -104, -320, 0x200, 0x280, 0, 0, 0, 255},
    {-160, -120, -320, 0, 0, 0, 0, 0, 255},
    {-144, -120, -320, 0x280, 0, 0, 0, 0, 255},
    {-160, -104, -320, 0, 0x280, 0, 0, 0, 255},
    {-144, -104, -320, 0x280, 0x280, 0, 0, 0, 255},
};

static void effOpukuJetwrainDisp(CameraId cameraId, EffectEntry* effect);
static void effOpukuJetwrainMain(EffectEntry* effect);

/** Returns the sine of an angle given in degrees. */
inline f32 sind(f32 deg) {
    return sin(6.2831855f * deg / 360.0f);
}

/** Returns whether a < 12 (the loop bound over the wave grid columns). */
inline int inline_fn2(s32 a) {
    return a < 12;
}
/** Returns whether n < 13 (the loop bound over the wave grid width). */
#ifdef TARGET_PC /* file-local: other files define different inline_fn()s (one external copy wins on PC) */
static
#endif
inline int inline_fn(s32 n) {
    return n < 13;
}

/**
 * Spawns a water-ripple screen distortion for lifetime frames (<= 0 means until deleted), scaled by scale;
 * the position is stored but the draw covers the whole screen. Via evt_eff64 "opuku_jetwrain_n64".
 * Soft-deletable.
 */
EffectEntry* effOpukuJetwrainN64Entry(s32 type, f32 x, f32 y, f32 z, f32 scale, s32 lifetime) {
    EffectEntry* effect;
    EffOpukuJetwrainWork* work;
    s32 i;
    s32 j;

    effect = effEntry();
    effect->type = "OpukuJetwrainN64";
    effect->count = 1;
    work = __memAlloc(HEAP_EFFECT, sizeof(EffOpukuJetwrainWork));
    effect->userdata = work;
    effect->callback = effOpukuJetwrainMain;
    effect->flags |= EFFECT_FLAG_SOFT_DELETE;
    work->type = type;
    work->frame = 0;
    if (lifetime <= 0) {
        work->lifetime = 1000;
    } else {
        work->lifetime = lifetime;
    }
    work->alpha = 0;
    work->position.x = x;
    work->position.y = y;
    work->position.z = z;
    work->scale = scale;
    work->col[0] = 10;
    work->col[1] = 110;
    work->col[2] = 255;
    work->col2[0] = 190;
    work->col2[1] = 220;
    work->col2[2] = 255;
    for (i = 0; i < 19; i++) {
        for (j = 0; inline_fn(j); j++) {
            work->h[i][j] = 0;
            work->v[i][j] = 0;
        }
    }
    return effect;
}

/**
 * Fades the effect in and out and steps a 19x13 height-field wave simulation,
 * driving the middle column with a sine.
 */
static void effOpukuJetwrainMain(EffectEntry* effect) {
    s32 life;
    EffOpukuJetwrainWork* work;
    Vec position;
    Vec tmp = {0.0f, 0.0f, 0.0f};
    s32 i;
    s32 j;
    s32 frame;
    s32 alpha;
    f32 a;

    work = effect->userdata;
    tmp.x = work->position.x;
    tmp.y = work->position.y;
    tmp.z = work->position.z;
    position = tmp;
    if (effect->flags & EFFECT_FLAG_DELETE_REQUESTED) {
        effect->flags &= ~EFFECT_FLAG_DELETE_REQUESTED;
        work->lifetime = 32;
    }
    if (work->lifetime < 1000) {
        work->lifetime--;
    }
    work->frame++;
    life = work->lifetime;
    if (life < 0) {
        effDelete(effect);
        return;
    }
    frame = work->frame;
    alpha = 255;
    if (life < 32) {
        alpha = life * 8;
    }
    if (frame < 16) {
        alpha = frame * 16 + 15;
    }
    work->alpha = alpha;
    a = (f32)alpha / 255.0f;
    for (i = 1; i < 18; i++) {
        work->h[i][6] = a * (-64.0f * sind((f32)((frame - i) * -20)));
    }
    for (i = 1; i < 18; i++) {
        for (j = 1; j < 12; j++) {
            f32 c4 = 4.0f * (f32)work->h[i][j];
            f32 corners = (f32)work->h[i - 1][j - 1] + ((f32)work->h[i + 1][j - 1] + ((f32)work->h[i + 1][j + 1] + (f32)work->h[i - 1][j + 1]));
            f32 edges = (f32)work->h[i][j - 1] + ((f32)work->h[i][j + 1] + ((f32)work->h[i + 1][j] + (f32)work->h[i - 1][j]));
            f32 half = 0.5f * corners;
            work->v[i][j] += (half + (edges - c4)) - 0.7f * c4;
            work->v[i][j] *= 0.98f;
            if (life < 32) {
                work->h[i][j] *= a;
            }
        }
    }
    for (i = 1; i < 18; i++) {
        for (j = 1; inline_fn2(j); j++) {
            work->h[i][j] += 0.02f * work->v[i][j];
        }
    }
#ifdef TARGET_PC
    /* PC port: the last of the 3D camera's translucent draws, so the copy holds the whole stage (units, effects
     * in front) and the ripple covers it all, as PM64's fx_underwater covers its world; the 2D camera (the HUD)
     * draws after, over it */
    (void)position;
    dispEntry(CAMERA_3D, 10000.0f, 2, (DispCallback)effOpukuJetwrainDisp, effect);
#else
    dispEntry(CAMERA_3D, dispCalcZ(&position), 2, (DispCallback)effOpukuJetwrainDisp, effect);
#endif
}

#ifdef TARGET_PC
#include "wide_x.h"

/* PC port: the draw below (the GameCube's own, never used by TTYD) puts its tiles at 4 x PM64's screen pixels
 * on a 608x480 ortho seen from 304,240: 64-unit tiles from x -256, a grid more than twice the frame, and samples
 * the copy at (pixel / 256) of its 304 texels: what reaches the screen is a magnified, offset piece of the frame
 * in 64-pixel tiles under the blue tint (Sushie's Tidal Wave in the PM64 partners: a blue tiled smear over the
 * stage). It is PM64's fx_underwater: the frame copied and redrawn as an 18 x 12 grid of 16-pixel tiles over
 * PM64's viewport (12..308, 20..220 of its 320x240), the grid's corners moved down by the wave's heights (in
 * quarter pixels), the copy's own colour plus the tint (col x alpha / 2). Here: the same grid over the whole
 * frame (PM64's viewport is all of its picture), each corner sampling where it would be unmoved, so the frame
 * ripples in place. The copy is of the frame as Aurora has it (its real size, widened or not), and the draw
 * covers it the same way (portScreenSpaceBegin: no widescreen squeeze, no mirror flip). */
#define JETWRAIN_COLS 19 /* grid corners across: PM64's 12, 32, 48 .. 288, 308 */
#define JETWRAIN_ROWS 13 /* and down: 20, 40, 56 .. 200, 220 */

static f32 jetwrainCornerX(s32 j) {
    return j == 0 ? 12.0f : j == JETWRAIN_COLS - 1 ? 308.0f : (f32)(j * 16 + 16);
}

static f32 jetwrainCornerY(s32 i) {
    return i == 0 ? 20.0f : i == JETWRAIN_ROWS - 1 ? 220.0f : (f32)(i * 16 + 24);
}

static void effOpukuJetwrainDispPC(EffOpukuJetwrainWork* work) {
    static const GXColor kNoFog = {0, 0, 0, 0};
    Mtx44 ortho;
    Mtx ident;
    GXTexObj texObj;
    f32 proj[7];
    void* buf;
    s32 i, j, k;

    GXGetProjectionv(proj);
    portScreenSpaceBegin();
    C_MTXOrtho(ortho, 0.0f, 480.0f, 0.0f, 608.0f, 1.0f, 32768.0f);
    GXSetProjection(ortho, GX_ORTHOGRAPHIC);
    PSMTXIdentity(ident);
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, kNoFog);

    buf = smartAlloc(GXGetTexBufferSize(304, 240, GX_TF_RGB565, GX_FALSE, 0), 3)->address;
    GXSetTexCopySrc(0, 0, 608, 480);
    GXSetTexCopyDst(304, 240, GX_TF_RGB565, GX_TRUE);
    GXCopyTex(buf, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&texObj, buf, 304, 240, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&texObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&texObj, GX_TEXMAP0);

    GXSetNumChans(0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_C0, GX_CC_A0, GX_CC_TEXC);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevColor(GX_TEVREG0, (GXColor){work->col[0], work->col[1], work->col[2], (u8)(work->alpha >> 1)});
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    for (i = 0; i < JETWRAIN_ROWS - 1; i++) {
        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, JETWRAIN_COLS * 2);
        for (j = 0; j < JETWRAIN_COLS; j++) {
            const f32 x = jetwrainCornerX(j);
            for (k = i; k <= i + 1; k++) {
                const f32 y = jetwrainCornerY(k);
                const f32 u = (x - 12.0f) / 296.0f;
                const f32 v = (y - 20.0f) / 200.0f;
                const f32 dy = (f32)work->h[j][k] / 4.0f / 200.0f; /* quarter pixels of PM64's 200 rows */
                GXPosition3f32(u * 608.0f, (v + dy) * 480.0f, -100.0f);
                GXTexCoord2f32(u, v);
            }
        }
        GXEnd();
    }
    portScreenSpaceEnd();
    GXSetProjectionv(proj);
}
#endif

/**
 * Copies the frame to a texture and redraws it as 16x16 tiles whose corners are displaced by the wave heights,
 * waiting for draw sync after every tile.
 */
static void effOpukuJetwrainDisp(CameraId cameraId, EffectEntry* effect) {
    s32 px;
    EffOpukuJetwrainWork* work;
    s32 alpha;
    Mtx trans;
    Mtx texMtx;
    Mtx44 ortho;
    Mtx view;
    GXTexObj texObj;
    f32 proj[7];
    void* buf;
    s32 j, i;
    s32 edgeY;
    s32 x1;
    s32 y0;
    s32 y1;
    s32 by, bx;
    s32 x0;
    s32 k;
    s32 edgeX;

    camGetPtr(cameraId);
    work = effect->userdata;
#ifdef TARGET_PC
    effOpukuJetwrainDispPC(work); /* (above) */
    return;
#endif
    alpha = work->alpha;
    GXGetProjectionv(proj);
    C_MTXOrtho(ortho, 0.0f, 480.0f, 0.0f, 608.0f, 1.0f, 32768.0f);
    GXSetProjection(ortho, GX_ORTHOGRAPHIC);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    buf = smartAlloc(GXGetTexBufferSize(304, 240, GX_TF_RGB565, GX_FALSE, 0), 3)->address;
    GXSetTexCopySrc(0, 0, 608, 480);
    GXSetTexCopyDst(304, 240, GX_TF_RGB565, GX_TRUE);
    GXCopyTex(buf, GX_FALSE);
    GXPixModeSync();
    GXInitTexObj(&texObj, buf, 304, 240, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&texObj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&texObj, GX_TEXMAP0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_C0, GX_CC_A0, GX_CC_TEXC);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GXSetTevColor(GX_TEVREG0, (GXColor){work->col[0], work->col[1], work->col[2], alpha >> 1});
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    GXSetCullMode(GX_CULL_NONE);
    effSetVtxDescN64(opuku_jetwrain_vtx);
    {
        Vec up = {0.0f, 1.0f, 0.0f};
        Vec target = {304.0f, 240.0f, 0.0f};
        Vec cam = {304.0f, 240.0f, 100.0f};
        C_MTXLookAt(view, &cam, &up, &target);
    }
    GXLoadPosMtxImm(view, 0);
    GXSetCurrentMtx(0);
    for (i = 0; i < 12; i++) {
        by = i * 16 + 24;
        edgeY = 0;
        if (i == 0) {
            y0 = -4;
            edgeY = 1;
        } else {
            y0 = 0;
        }
        if (i == 11) {
            y1 = 4;
            edgeY = 1;
        } else {
            y1 = 0;
        }
        for (j = 0; j < 18; j++) {
            bx = j * 16 + 16;
            edgeX = 0;
            if (j == 0) {
                x0 = -4;
                edgeX = 1;
            } else {
                x0 = 0;
            }
            if (j == 17) {
                x1 = 4;
                edgeX = 1;
            } else {
                x1 = 0;
            }
            PSMTXScale(texMtx, 0.00390625f, 0.00390625f, 1.0f);
            px = bx + x0;
            PSMTXTrans(trans, px, by + y0, 0.0f);
            PSMTXConcat(texMtx, trans, texMtx);
            GXLoadTexMtxImm(texMtx, GX_TEXMTX0, GX_MTX2x4);
            if (edgeX != 0 && edgeY != 0) {
                k = 12;
            } else if (edgeX != 0) {
                k = 4;
            } else if (edgeY) {
                k = 8;
            } else {
                k = 0;
            }
            opuku_jetwrain_vtx[k].x = px * 4;
            opuku_jetwrain_vtx[k].y = work->h[j][i] + (by + y0) * 4;
            opuku_jetwrain_vtx[k + 1].x = (bx + x1 + 16) * 4;
            opuku_jetwrain_vtx[k + 1].y = work->h[j + 1][i] + (by + y0) * 4;
            opuku_jetwrain_vtx[k + 2].x = px * 4;
            opuku_jetwrain_vtx[k + 2].y = work->h[j][i + 1] + (by + y1 + 16) * 4;
            opuku_jetwrain_vtx[k + 3].x = (bx + x1 + 16) * 4;
            opuku_jetwrain_vtx[k + 3].y = work->h[j + 1][i + 1] + (by + y1 + 16) * 4;
            DCFlushRange(opuku_jetwrain_vtx, sizeof(opuku_jetwrain_vtx));
            GXInvalidateVtxCache();
            GXBegin(GX_TRIANGLES, GX_VTXFMT0, 6);
            tri2(k, k + 3, k + 1, 0, k, k + 2, k + 3, 0);
            sysWaitDrawSync();
        }
    }
    GXSetProjectionv(proj);
}
