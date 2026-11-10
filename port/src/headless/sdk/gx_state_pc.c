/* GX state the game reads back (headless). Nothing is drawn, but the game
 * does its own math with the viewport, scissor and projection it set
 * (GXProject for screen positions: culling, speech bubbles, captures), so
 * they are kept and returned as libs/dolsdk2004/src/gx/GXTransform.c does,
 * and the SDK's GXProject is reproduced. Other read-backs answer with
 * neutral values rather than leaving the caller's outputs unwritten. */
#include <dolphin.h>
#include <string.h>

static f32 sViewport[6] = {0.0f, 0.0f, 640.0f, 480.0f, 0.0f, 1.0f};
static u32 sScissor[4] = {0, 0, 640, 480};
static GXProjectionType sProjType = GX_PERSPECTIVE;
static f32 sProj[6];

/* gx_bbox_pc.c: the bounding box's copy of the transform state */
void portGXBboxProjection(const f32 p[6], int ortho);
void portGXBboxViewport(f32 left, f32 top, f32 width, f32 height, u32 field);
void portGXBboxScissor(u32 left, u32 top, u32 width, u32 height);

static void setViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    sViewport[0] = left;
    sViewport[1] = top;
    sViewport[2] = wd;
    sViewport[3] = ht;
    sViewport[4] = nearz;
    sViewport[5] = farz;
}

void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    setViewport(left, top, wd, ht, nearz, farz);
    portGXBboxViewport(left, top, wd, ht, 1);
}

void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field) {
    setViewport(left, top, wd, ht, nearz, farz);
    portGXBboxViewport(left, top, wd, ht, field);
}

void GXGetViewportv(f32* vp) {
    memcpy(vp, sViewport, sizeof(sViewport));
}

void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht) {
    sScissor[0] = left;
    sScissor[1] = top;
    sScissor[2] = wd;
    sScissor[3] = ht;
    portGXBboxScissor(left, top, wd, ht);
}

void GXGetScissor(u32* left, u32* top, u32* wd, u32* ht) {
    *left = sScissor[0];
    *top = sScissor[1];
    *wd = sScissor[2];
    *ht = sScissor[3];
}

void GXSetProjection(const f32 mtx[4][4], GXProjectionType type) {
    sProjType = type;
    sProj[0] = mtx[0][0];
    sProj[2] = mtx[1][1];
    sProj[4] = mtx[2][2];
    sProj[5] = mtx[2][3];
    if (type == GX_ORTHOGRAPHIC) {
        sProj[1] = mtx[0][3];
        sProj[3] = mtx[1][3];
    } else {
        sProj[1] = mtx[0][2];
        sProj[3] = mtx[1][2];
    }
    portGXBboxProjection(sProj, type == GX_ORTHOGRAPHIC);
}

void GXSetProjectionv(const f32* ptr) {
    sProjType = ptr[0] == 0.0f ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC;
    memcpy(sProj, ptr + 1, sizeof(sProj));
    portGXBboxProjection(sProj, sProjType != GX_PERSPECTIVE);
}

void GXGetProjectionv(f32* ptr) {
    ptr[0] = sProjType != GX_PERSPECTIVE ? 1.0f : 0.0f;
    memcpy(ptr + 1, sProj, sizeof(sProj));
}

void GXProject(f32 x, f32 y, f32 z, const f32 mtx[3][4], const f32* pm, const f32* vp, f32* sx, f32* sy, f32* sz) {
    f32 ex = mtx[0][3] + ((mtx[0][2] * z) + ((mtx[0][0] * x) + (mtx[0][1] * y)));
    f32 ey = mtx[1][3] + ((mtx[1][2] * z) + ((mtx[1][0] * x) + (mtx[1][1] * y)));
    f32 ez = mtx[2][3] + ((mtx[2][2] * z) + ((mtx[2][0] * x) + (mtx[2][1] * y)));
    f32 xc, yc, zc, wc;
    if (pm[0] == 0.0f) {
        xc = (ex * pm[1]) + (ez * pm[2]);
        yc = (ey * pm[3]) + (ez * pm[4]);
        zc = pm[6] + (ez * pm[5]);
        wc = 1.0f / -ez;
    } else {
        xc = pm[2] + (ex * pm[1]);
        yc = pm[4] + (ey * pm[3]);
        zc = pm[6] + (ez * pm[5]);
        wc = 1.0f;
    }
    *sx = (vp[2] / 2.0f) + (vp[0] + (wc * (xc * vp[2] / 2.0f)));
    *sy = (vp[3] / 2.0f) + (vp[1] + (wc * (-yc * vp[3] / 2.0f)));
    *sz = vp[5] + (wc * (zc * (vp[5] - vp[4])));
}

/* Nothing drawn: an EFB peek reads black. */
void GXPeekARGB(u16 x, u16 y, u32* color) {
    (void)x;
    (void)y;
    *color = 0;
}

void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy, u32* clocks) {
    *xf_wait_in = *xf_wait_out = *ras_busy = *clocks = 0;
}

/* GXReadBoundingBox: gx_bbox_pc.c, as Aurora computes it */
