/* The paper captures' box as the GameCube's pixel engine measures it: the
 * pixels that pass the alpha test, not the vertices (port/paper_box.h).
 * Checked against Dolphin with Mario held on tou_07's bed: the standing sheet
 * (PM_S_3C) and the lying one (PM_S_3A) land where the GameCube draws them,
 * at the same size, half under the pillow. */
#include <stddef.h>
#include <dolphin/types.h>
#include <dolphin/gx.h>
#include <math.h>
#include "port/paper_box.h"

int gPortPaperBox;

static int sUnknown;  /* a fan that couldn't be placed: the GX box stands */
static int sAny;      /* a pixel passed */
static int sLeft, sTop, sRight, sBottom;

#define MAX_FAN 64
#define ALPHA_REF 0x80 /* the capture's GXSetAlphaCompare(GX_GEQUAL, 0x80, ...) */

void portPaperBoxBegin(void) {
    gPortPaperBox = 1;
    sUnknown = 0;
    sAny = 0;
    sLeft = sTop = 0x7FFFFFFF;
    sRight = sBottom = -1;
}

void portPaperBoxUnknown(void) {
    sUnknown = 1;
}

int portPaperBoxEnd(u16* left, u16* top, u16* right, u16* bottom) {
    gPortPaperBox = 0;
    if (sUnknown || !sAny) {
        return 0;
    }
    *left = (u16)sLeft;
    *top = (u16)sTop;
    *right = (u16)sRight;
    *bottom = (u16)sBottom;
    return 1;
}

/* ---- a texel's alpha, from the GX image (tiled, big-endian) ---- */

static u32 be16(const u8* p) {
    return ((u32)p[0] << 8) | p[1];
}

static int a3(u32 v) { /* RGB5A3 */
    if (v & 0x8000) {
        return 255;
    }
    v = (v >> 12) & 7;
    return (int)((v << 5) | (v << 2) | (v >> 1));
}

static int tlutAlpha(const PortBoxTex* t, u32 index) {
    u32 v;
    if (t->tlut == NULL || (int)index >= t->tlutCount) {
        return 255;
    }
    v = be16(t->tlut + 2 * index);
    switch (t->tlutFormat) {
    case GX_TL_IA8:
        return (int)(v >> 8);
    case GX_TL_RGB565:
        return 255;
    default:
        return a3(v);
    }
}

static int texelAlpha(const PortBoxTex* t, int x, int y) {
    int bw, bh, bytes;
    const u8* tile;
    int p;
    switch (t->format) {
    case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: bw = 8; bh = 8; bytes = 32; break;
    case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: bw = 8; bh = 4; bytes = 32; break;
    case GX_TF_RGBA8: bw = 4; bh = 4; bytes = 64; break;
    default: bw = 4; bh = 4; bytes = 32; break;
    }
    tile = t->data + (size_t)((y / bh) * ((t->width + bw - 1) / bw) + x / bw) * bytes;
    p = (y % bh) * bw + x % bw;
    switch (t->format) {
    case GX_TF_I4: {
        int i = (p & 1) ? tile[p >> 1] & 15 : tile[p >> 1] >> 4;
        return i * 17;
    }
    case GX_TF_I8:
        return tile[p];
    case GX_TF_IA4:
        return (tile[p] >> 4) * 17;
    case GX_TF_IA8:
        return tile[2 * p];
    case GX_TF_RGB565:
        return 255;
    case GX_TF_RGB5A3:
        return a3(be16(tile + 2 * p));
    case GX_TF_RGBA8:
        return tile[2 * p];
    case GX_TF_C4:
        return tlutAlpha(t, (p & 1) ? tile[p >> 1] & 15 : tile[p >> 1] >> 4);
    case GX_TF_C8:
        return tlutAlpha(t, tile[p]);
    case GX_TF_C14X2:
        return tlutAlpha(t, be16(tile + 2 * p) & 0x3FFF);
    case GX_TF_CMPR: {
        const u8* blk = tile + 8 * (((y & 7) >> 2) * 2 + ((x & 7) >> 2));
        const u32 c0 = be16(blk), c1 = be16(blk + 2);
        const u32 bits = ((u32)blk[4] << 24) | ((u32)blk[5] << 16) | ((u32)blk[6] << 8) | blk[7];
        const int j = (y & 3) * 4 + (x & 3);
        return (c0 <= c1 && ((bits >> (30 - 2 * j)) & 3) == 3) ? 0 : 255;
    }
    default:
        return 255;
    }
}

static int wrapCoord(int c, int size, int mode) {
    if (mode == GX_REPEAT) {
        c %= size;
        return c < 0 ? c + size : c;
    }
    if (mode == GX_MIRROR) {
        const int period = 2 * size;
        c %= period;
        if (c < 0) {
            c += period;
        }
        return c < size ? c : period - 1 - c;
    }
    return c < 0 ? 0 : (c >= size ? size - 1 : c);
}

static int sampleAlpha(const PortBoxTex* t, f32 s, f32 tt) {
    f32 u, v, fu, fv;
    int x0, y0, x1, y1;
    f32 a;
    if (t->linear) {
        u = s * (f32)t->width - 0.5f;
        v = tt * (f32)t->height - 0.5f;
        x0 = (int)floorf(u);
        y0 = (int)floorf(v);
        fu = u - (f32)x0;
        fv = v - (f32)y0;
        x1 = wrapCoord(x0 + 1, t->width, t->wrapS);
        y1 = wrapCoord(y0 + 1, t->height, t->wrapT);
        x0 = wrapCoord(x0, t->width, t->wrapS);
        y0 = wrapCoord(y0, t->height, t->wrapT);
        a = (1.0f - fv) * ((1.0f - fu) * (f32)texelAlpha(t, x0, y0) + fu * (f32)texelAlpha(t, x1, y0)) +
            fv * ((1.0f - fu) * (f32)texelAlpha(t, x0, y1) + fu * (f32)texelAlpha(t, x1, y1));
        return (int)(a + 0.5f);
    }
    x0 = wrapCoord((int)floorf(s * (f32)t->width), t->width, t->wrapS);
    y0 = wrapCoord((int)floorf(tt * (f32)t->height), t->height, t->wrapT);
    return texelAlpha(t, x0, y0);
}

/* ---- the fan, rasterized ---- */

typedef struct {
    f32 x, y;     /* screen */
    f32 iw;       /* 1/w */
    f32 s, t, a;  /* over w */
} Corner;

static void addPixel(int x, int y) {
    sAny = 1;
    if (x < sLeft) sLeft = x;
    if (x > sRight) sRight = x;
    if (y < sTop) sTop = y;
    if (y > sBottom) sBottom = y;
}

static void rasterTri(const Corner* c0, const Corner* c1, const Corner* c2, const PortBoxTex* tex, int scL, int scT,
                      int scR, int scB) {
    const f32 d = (c1->y - c2->y) * (c0->x - c2->x) + (c2->x - c1->x) * (c0->y - c2->y);
    int x0, x1, y0, y1, px, py;
    if (fabsf(d) < 1e-9f) {
        return;
    }
    x0 = (int)floorf(fminf(c0->x, fminf(c1->x, c2->x)));
    x1 = (int)ceilf(fmaxf(c0->x, fmaxf(c1->x, c2->x)));
    y0 = (int)floorf(fminf(c0->y, fminf(c1->y, c2->y)));
    y1 = (int)ceilf(fmaxf(c0->y, fmaxf(c1->y, c2->y)));
    if (x0 < scL) x0 = scL;
    if (y0 < scT) y0 = scT;
    if (x1 > scR) x1 = scR;
    if (y1 > scB) y1 = scB;
    for (py = y0; py <= y1; py++) {
        const f32 cy = (f32)py + 0.5f;
        const int inRows = sAny && py >= sTop && py <= sBottom;
        for (px = x0; px <= x1; px++) {
            const f32 cx = (f32)px + 0.5f;
            f32 l0, l1, l2, iw, a;
            if (inRows && px >= sLeft && px <= sRight) {
                px = sRight; /* inside the box already: nothing to learn here */
                continue;
            }
            l0 = ((c1->y - c2->y) * (cx - c2->x) + (c2->x - c1->x) * (cy - c2->y)) / d;
            l1 = ((c2->y - c0->y) * (cx - c2->x) + (c0->x - c2->x) * (cy - c2->y)) / d;
            l2 = 1.0f - l0 - l1;
            if (l0 < 0.0f || l1 < 0.0f || l2 < 0.0f) {
                continue;
            }
            iw = l0 * c0->iw + l1 * c1->iw + l2 * c2->iw;
            if (!(iw > 0.0f)) {
                continue;
            }
            a = (l0 * c0->a + l1 * c1->a + l2 * c2->a) / iw;
            if (tex != NULL && tex->hasTex) {
                const f32 s = (l0 * c0->s + l1 * c1->s + l2 * c2->s) / iw;
                const f32 t = (l0 * c0->t + l1 * c1->t + l2 * c2->t) / iw;
                a = a * (f32)sampleAlpha(tex, s, t) / 255.0f;
            }
            if (a + 0.5f >= (f32)ALPHA_REF) {
                addPixel(px, py);
            }
        }
    }
}

void portPaperBoxFan(MtxPtr m, const PortBoxTex* tex, int n, const Vec* pos, const f32 (*uv)[2], const u8* alpha) {
    Corner c[MAX_FAN];
    f32 proj[7], vp[6];
    u32 scL, scT, scW, scH;
    int i;
    if (!gPortPaperBox || sUnknown || n < 3) {
        return;
    }
    if (n > MAX_FAN) {
        sUnknown = 1;
        return;
    }
    GXGetProjectionv(proj);
    GXGetViewportv(vp);
    GXGetScissor(&scL, &scT, &scW, &scH);
    for (i = 0; i < n; i++) {
        const f32 x = m[0][0] * pos[i].x + m[0][1] * pos[i].y + m[0][2] * pos[i].z + m[0][3];
        const f32 y = m[1][0] * pos[i].x + m[1][1] * pos[i].y + m[1][2] * pos[i].z + m[1][3];
        const f32 z = m[2][0] * pos[i].x + m[2][1] * pos[i].y + m[2][2] * pos[i].z + m[2][3];
        f32 cx, cy, cw, s = 0.0f, t = 0.0f;
        if (proj[0] == (f32)GX_PERSPECTIVE) {
            cx = proj[1] * x + proj[2] * z;
            cy = proj[3] * y + proj[4] * z;
            cw = -z;
        } else {
            cx = proj[1] * x + proj[2];
            cy = proj[3] * y + proj[4];
            cw = 1.0f;
        }
        if (!(cw > 1e-6f)) {
            sUnknown = 1; /* behind the eye */
            return;
        }
        if (tex != NULL && tex->hasTex && uv != NULL) {
            s = tex->mtx[0][0] * uv[i][0] + tex->mtx[0][1] * uv[i][1] + tex->mtx[0][2] + tex->mtx[0][3];
            t = tex->mtx[1][0] * uv[i][0] + tex->mtx[1][1] * uv[i][1] + tex->mtx[1][2] + tex->mtx[1][3];
        }
        c[i].x = vp[0] + (cx / cw + 1.0f) * 0.5f * vp[2];
        c[i].y = vp[1] + (1.0f - cy / cw) * 0.5f * vp[3];
        c[i].iw = 1.0f / cw;
        c[i].s = s * c[i].iw;
        c[i].t = t * c[i].iw;
        c[i].a = (f32)(alpha != NULL ? alpha[i] : 255) * c[i].iw;
    }
    for (i = 1; i + 1 < n; i++) {
        rasterTri(&c[0], &c[i], &c[i + 1], tex, (int)scL, (int)scT, (int)(scL + scW) - 1, (int)(scT + scH) - 1);
    }
}
