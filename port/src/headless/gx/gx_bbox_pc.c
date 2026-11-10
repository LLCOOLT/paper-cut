/* The GX bounding box, headless (GXClearBoundingBox / GXReadBoundingBox).
 *
 * The game sizes its offscreen captures with it (paper-peel scenes, the
 * paper forms of NPCs and map objects, shadows) and decides from it whether
 * there was anything to capture: a scene can wait on a capture's animation
 * (evt_img_wait_animend), so the box is game logic. Aurora computes it on the
 * CPU (extern/aurora lib/gx/command_processor.cpp: bbox_add_vertices,
 * bbox_read): the screen extent of the positions of the vertices drawn
 * between a clear and a read, through the position matrix, the projection
 * and the viewport, clamped to the scissor; a vertex that can't be placed
 * (behind the eye, an index outside its array) makes it the whole scissor.
 * This is the same arithmetic on the same state, so headless gets the same
 * boxes and plays the same as Aurora.
 *
 * What it needs is kept here as the game sets it: the vertex descriptors and
 * formats, the position array, the position matrices and the current one,
 * the projection, viewport and scissor (gx_state_pc.c passes those on) as
 * Aurora's command processor has them. Vertices come three ways:
 *  - immediate (GXBegin, then GXPosition... into gx_fifo_pc.c's slots, one
 *    a component): taken at the next GXBegin or state change or read;
 *  - display lists from the disc (the GX command stream, big-endian): read
 *    as Aurora's command processor reads them;
 *  - display lists the game builds (GXBeginDisplayList): their draws kept
 *    here and the list marked with an id, replayed when it is called.
 * Outside a clear-read window only the state is followed (cheap). */
#include <dolphin.h>
#include <dolphin/gx.h>
#include <float.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "port/gx_arrays.h"
#include "port/gx_pc.h"


#define MAX_PNMTX 10
#define NUM_ATTR 21 /* GX_VA_PNMTXIDX .. GX_VA_TEX7 */

typedef struct {
    u8 cnt, type, frac;
} AttrFmt;

static u8 sVtxDesc[NUM_ATTR];
static AttrFmt sVat[GX_MAX_VTXFMT][NUM_ATTR];
static struct {
    const u8* data;
    u32 size, stride;
    int le;
} sPosArray;
static f32 sPnMtx[MAX_PNMTX][3][4];
static u32 sCurrentPnMtx;
static f32 sProj[4][4];
static f32 sVpLeft = 0.0f, sVpTop = 0.0f, sVpWidth = 640.0f, sVpHeight = 480.0f;
static s32 sScX = 0, sScY = 0, sScW = 640, sScH = 480;

/* the box (Aurora's BoundingBox) */
static int sArmed, sUnknown;
static f32 sLeft, sTop, sRight = -1.0f, sBottom = -1.0f;
static u16 sResult[4] = {0x3FF, 0x3FF, 0, 0};

/* --- immediate draws --- */
static int sPending;
static u8 sPendFmt;
static u16 sPendCount;
static u32 sPendStart;

/* --- display lists the game builds --- */
#define DL_MAGIC 0x484C444Cu /* "HLDL" */
typedef struct {
    u8 fmt;
    u16 count;
    u32 start, end; /* slots */
    u8 desc[NUM_ATTR];
    AttrFmt pos;
    u8 mtxDesc;
} DlDraw;
typedef struct {
    PPCWGPipe* slots;
    DlDraw* draws;
    u32 drawCount;
} DlRecord;
static DlRecord* sDls;
static u32 sDlCount, sDlCap;
static int sRecording;
static u32 sRecStart;
static DlDraw* sRecDraws;
static u32 sRecDrawCount, sRecDrawCap;
static u8* sRecBuf;

static u32 typeSize(u32 attr, u32 type) {
    if (attr < GX_VA_POS) {
        return 1;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (type) {
        case GX_RGB565:
        case GX_RGBA4:
            return 2;
        case GX_RGB8:
        case GX_RGBA6:
            return 3;
        default:
            return 4;
        }
    }
    switch (type) {
    case GX_U8:
    case GX_S8:
        return 1;
    case GX_U16:
    case GX_S16:
        return 2;
    case GX_F32:
        return 4;
    default:
        return 0;
    }
}

static u32 cntCount(u32 attr, u32 cnt) {
    if (attr < GX_VA_POS) {
        return 1;
    }
    if (attr == GX_VA_POS) {
        return cnt == GX_POS_XY ? 2 : 3;
    }
    if (attr == GX_VA_NRM) {
        return cnt == GX_NRM_XYZ ? 3 : 9;
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        return 1;
    }
    return cnt == GX_TEX_S ? 1 : 2;
}

/* the bytes of one vertex in the command stream (Aurora's calc_vtx_size) */
static u32 vtxSize(u32 fmt) {
    u32 size = 0, i;
    for (i = 0; i < NUM_ATTR; i++) {
        const AttrFmt* af = &sVat[fmt][i];
        switch (sVtxDesc[i]) {
        case GX_DIRECT:
            size += typeSize(i, af->type) * cntCount(i, af->cnt);
            break;
        case GX_INDEX8:
            size += i == GX_VA_NRM && af->cnt == GX_NRM_NBT3 ? 3 : 1;
            break;
        case GX_INDEX16:
            size += i == GX_VA_NRM && af->cnt == GX_NRM_NBT3 ? 6 : 2;
            break;
        default:
            break;
        }
    }
    return size;
}

static f32 component(const u8* src, u32 type, u32 c, int le, f32 scale) {
    switch (type) {
    case GX_U8:
        return (f32)src[c] * scale;
    case GX_S8:
        return (f32)(s8)src[c] * scale;
    case GX_U16:
    case GX_S16: {
        u16 v;
        memcpy(&v, src + 2 * c, 2);
        if (!le) {
            v = (u16)(v >> 8 | v << 8);
        }
        return (type == GX_U16 ? (f32)v : (f32)(s16)v) * scale;
    }
    default: {
        u32 v;
        f32 f;
        memcpy(&v, src + 4 * c, 4);
        if (!le) {
            v = v >> 24 | (v >> 8 & 0xFF00) | (v << 8 & 0xFF0000) | v << 24;
        }
        memcpy(&f, &v, 4);
        return f;
    }
    }
}

/* a position from its array (an index): FALSE if it can't be read */
static BOOL arrayPosition(u32 idx, const AttrFmt* af, f32 out[3]) {
    const u32 count = af->cnt == GX_POS_XY ? 2 : 3;
    const f32 scale = 1.0f / (f32)(1u << af->frac);
    const u8* src;
    u32 c;
    if (sPosArray.data == NULL || sPosArray.stride == 0) {
        return FALSE;
    }
    if (sPosArray.size != 0 && (u64)idx * sPosArray.stride + (u64)count * 4 > (u64)sPosArray.size + 4) {
        return FALSE;
    }
    src = sPosArray.data + (u64)idx * sPosArray.stride;
    if (af->type > GX_F32) {
        return FALSE;
    }
    for (c = 0; c < count; c++) {
        out[c] = component(src, af->type, c, sPosArray.le, scale);
    }
    if (count == 2) {
        out[2] = 0.0f;
    }
    return TRUE;
}

/* one placed position (Aurora's bbox_add_vertices, per vertex) */
static void addPosition(const f32 p[3], u32 mtx) {
    const f32(*m)[4];
    f32 v0, v1, v2, cx, cy, cw, sx, sy;
    if (mtx >= MAX_PNMTX) {
        sUnknown = TRUE;
        return;
    }
    m = sPnMtx[mtx];
    v0 = m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3];
    v1 = m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3];
    v2 = m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3];
    cx = sProj[0][0] * v0 + sProj[0][1] * v1 + sProj[0][2] * v2 + sProj[0][3];
    cy = sProj[1][0] * v0 + sProj[1][1] * v1 + sProj[1][2] * v2 + sProj[1][3];
    cw = sProj[3][0] * v0 + sProj[3][1] * v1 + sProj[3][2] * v2 + sProj[3][3];
    if (!(cw > 1e-6f)) {
        sUnknown = TRUE; /* behind the eye: its triangles may reach any edge */
        return;
    }
    sx = sVpLeft + (cx / cw + 1.0f) * 0.5f * sVpWidth;
    sy = sVpTop + (1.0f - cy / cw) * 0.5f * sVpHeight;
    sLeft = sLeft < sx ? sLeft : sx;
    sRight = sRight > sx ? sRight : sx;
    sTop = sTop < sy ? sTop : sy;
    sBottom = sBottom > sy ? sBottom : sy;
}

/* vertices from the command stream's bytes (big-endian), as Aurora reads them */
static void addStreamVertices(const u8* data, u32 count, u32 size, u32 fmt) {
    const AttrFmt* af = &sVat[fmt][GX_VA_POS];
    u32 v;
    if (size == 0) {
        return;
    }
    for (v = 0; v < count && !sUnknown; v++) {
        const u8* vtx = data + v * size;
        u32 off = 0, mtx = sCurrentPnMtx, i;
        f32 p[3];
        for (i = GX_VA_PNMTXIDX; i < GX_VA_POS; i++) {
            if (sVtxDesc[i] == GX_DIRECT) {
                if (i == GX_VA_PNMTXIDX) {
                    mtx = vtx[off] / 3;
                }
                off += 1;
            } else if (sVtxDesc[i] == GX_INDEX8) {
                off += 1;
            } else if (sVtxDesc[i] == GX_INDEX16) {
                off += 2;
            }
        }
        if (sVtxDesc[GX_VA_POS] == GX_DIRECT) {
            const u32 n = af->cnt == GX_POS_XY ? 2 : 3;
            const f32 scale = 1.0f / (f32)(1u << af->frac);
            u32 c;
            if (af->type > GX_F32) {
                sUnknown = TRUE;
                return;
            }
            for (c = 0; c < n; c++) {
                p[c] = component(vtx + off, af->type, c, 0, scale);
            }
            if (n == 2) {
                p[2] = 0.0f;
            }
        } else if (sVtxDesc[GX_VA_POS] == GX_INDEX8 || sVtxDesc[GX_VA_POS] == GX_INDEX16) {
            const u32 idx = sVtxDesc[GX_VA_POS] == GX_INDEX8 ? vtx[off] : ((u32)vtx[off] << 8 | vtx[off + 1]);
            if (!arrayPosition(idx, af, p)) {
                sUnknown = TRUE;
                return;
            }
        } else {
            sUnknown = TRUE;
            return;
        }
        addPosition(p, mtx);
    }
}

/* vertices from the immediate slots (a component a slot): the same reads */
static void addSlotVertices(const PPCWGPipe* slots, u32 nslots, u32 count, const u8* desc, const AttrFmt* af) {
    u32 per, v;
    if (count == 0) {
        return;
    }
    per = nslots / count;
    if (per == 0) {
        sUnknown = TRUE;
        return;
    }
    for (v = 0; v < count && !sUnknown; v++) {
        const PPCWGPipe* s = slots + v * per;
        u32 off = 0, mtx = sCurrentPnMtx, i;
        f32 p[3];
        for (i = GX_VA_PNMTXIDX; i < GX_VA_POS; i++) {
            if (desc[i] != GX_NONE) {
                if (i == GX_VA_PNMTXIDX && desc[i] == GX_DIRECT) {
                    mtx = s[off].u8 / 3;
                }
                off++;
            }
        }
        if (off >= per) {
            sUnknown = TRUE;
            return;
        }
        if (desc[GX_VA_POS] == GX_DIRECT) {
            const u32 n = af->cnt == GX_POS_XY ? 2 : 3;
            const f32 scale = 1.0f / (f32)(1u << af->frac);
            u32 c;
            for (c = 0; c < n; c++) {
                const PPCWGPipe* q = &s[off + c];
                switch (af->type) {
                case GX_U8:
                    p[c] = (f32)q->u8 * scale;
                    break;
                case GX_S8:
                    p[c] = (f32)q->s8 * scale;
                    break;
                case GX_U16:
                    p[c] = (f32)q->u16 * scale;
                    break;
                case GX_S16:
                    p[c] = (f32)q->s16 * scale;
                    break;
                case GX_F32:
                    p[c] = q->f32;
                    break;
                default:
                    sUnknown = TRUE;
                    return;
                }
            }
            if (n == 2) {
                p[2] = 0.0f;
            }
        } else if (desc[GX_VA_POS] == GX_INDEX8 || desc[GX_VA_POS] == GX_INDEX16) {
            const u32 idx = desc[GX_VA_POS] == GX_INDEX8 ? s[off].u8 : s[off].u16;
            if (!arrayPosition(idx, af, p)) {
                sUnknown = TRUE;
                return;
            }
        } else {
            sUnknown = TRUE;
            return;
        }
        addPosition(p, mtx);
    }
}

/* the immediate draw begun last, done: its vertices into the box, or into
 * the display list being built */
static void flush(void) {
    u32 count;
    const PPCWGPipe* slots;
    if (!sPending) {
        return;
    }
    sPending = 0;
    slots = portGXFifoSlots(&count);
    if (sRecording) {
        DlDraw* d;
        if (sRecDrawCount == sRecDrawCap) {
            sRecDrawCap = sRecDrawCap ? sRecDrawCap * 2 : 16;
            DlDraw* grown = (DlDraw*)realloc(sRecDraws, sRecDrawCap * sizeof(DlDraw));
            if (grown == NULL) {
                abort(); /* out of memory */
            }
            sRecDraws = grown;
        }
        d = &sRecDraws[sRecDrawCount++];
        d->fmt = sPendFmt;
        d->count = sPendCount;
        d->start = sPendStart - sRecStart;
        d->end = count - sRecStart;
        memcpy(d->desc, sVtxDesc, sizeof(d->desc));
        d->pos = sVat[sPendFmt][GX_VA_POS];
        return;
    }
    if (sArmed && count > sPendStart) {
        addSlotVertices(slots + sPendStart, count - sPendStart, sPendCount, sVtxDesc, &sVat[sPendFmt][GX_VA_POS]);
    }
}

void portGXBboxStateChanging(void) {
    flush();
}

void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    u32 count;
    (void)type;
    flush();
    if (!sRecording) {
        portGXFifoReset(); /* (nothing else reads the slots) */
    }
    portGXFifoSlots(&count);
    sPending = 1;
    sPendFmt = (u8)vtxfmt;
    sPendCount = nverts;
    sPendStart = count;
}

/* --- state --- */

void GXClearVtxDesc(void) {
    flush();
    memset(sVtxDesc, 0, sizeof(sVtxDesc));
    sVtxDesc[GX_VA_POS] = GX_DIRECT;
}

void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
    flush();
    if (attr == GX_VA_NBT) {
        attr = GX_VA_NRM;
    }
    if ((u32)attr < NUM_ATTR) {
        sVtxDesc[attr] = (u8)type;
    }
}

void GXSetVtxDescv(const GXVtxDescList* list) {
    flush();
    for (; list->attr != GX_VA_NULL; list++) {
        GXSetVtxDesc(list->attr, list->type);
    }
}

void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    flush();
    if (attr == GX_VA_NBT) {
        attr = GX_VA_NRM;
        cnt = GX_NRM_NBT;
    }
    if ((u32)vtxfmt < GX_MAX_VTXFMT && (u32)attr < NUM_ATTR) {
        sVat[vtxfmt][attr].cnt = (u8)cnt;
        sVat[vtxfmt][attr].type = (u8)type;
        sVat[vtxfmt][attr].frac = frac;
    }
}

void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list) {
    for (; list->attr != GX_VA_NULL; list++) {
        GXSetVtxAttrFmt(vtxfmt, list->attr, list->cnt, list->type, list->frac);
    }
}

/* the array as Aurora gets it (gx_compat_aurora.c's portGXSetArraySized): its
 * size and endianness from the registrations, else unsized and native */
void GXSetArray(GXAttr attr, void* base_ptr, u8 stride) {
    unsigned int size = 0;
    int le = 1;
    if (attr != GX_VA_POS) {
        return;
    }
    flush();
    portGXArrayInfo(base_ptr, &size, &le);
    if (size == 0 && base_ptr != NULL) {
        size = (u32)stride * 0x10000u;
    }
    sPosArray.data = (const u8*)base_ptr;
    sPosArray.size = size;
    sPosArray.stride = stride;
    sPosArray.le = le;
}

void GXLoadPosMtxImm(const f32 mtx[3][4], u32 id) {
    flush();
    if (id / 3 < MAX_PNMTX) {
        memcpy(sPnMtx[id / 3], mtx, sizeof(sPnMtx[0]));
    }
}

void GXSetCurrentMtx(u32 id) {
    flush();
    sCurrentPnMtx = (id & 0x3F) / 3;
}

/* gx_state_pc.c: the projection's six values and type, as Aurora's XF load builds its matrix */
void portGXBboxProjection(const f32 p[6], int ortho) {
    flush();
    memset(sProj, 0, sizeof(sProj));
    sProj[0][0] = p[0];
    sProj[1][1] = p[2];
    sProj[2][2] = p[4];
    sProj[2][3] = p[5];
    if (ortho) {
        sProj[0][3] = p[1];
        sProj[1][3] = p[3];
        sProj[3][3] = 1.0f;
    } else {
        sProj[0][2] = p[1];
        sProj[1][2] = p[3];
        sProj[3][2] = -1.0f;
    }
}

/* gx_state_pc.c: the viewport, through Aurora's XF registers and back */
void portGXBboxViewport(f32 left, f32 top, f32 width, f32 height, u32 field) {
    f32 sx, sy, ox, oy, w, h;
    flush();
    if (field == 0) {
        top -= 0.5f;
    }
    sx = width / 2.0f;
    sy = -height / 2.0f;
    ox = 340.0f + (left + width / 2.0f);
    oy = 340.0f + (top + height / 2.0f);
    w = sx * 2.0f;
    h = -sy * 2.0f;
    sVpLeft = ox - 340.0f - w / 2.0f;
    sVpTop = oy - 340.0f - h / 2.0f;
    sVpWidth = w;
    sVpHeight = h;
}

void portGXBboxScissor(u32 left, u32 top, u32 width, u32 height) {
    flush();
    sScX = (s32)(left & 0x7FF);
    sScY = (s32)(top & 0x7FF);
    sScW = (s32)width;
    sScH = (s32)height;
}

/* --- the box --- */

void GXClearBoundingBox(void) {
    flush();
    sArmed = TRUE;
    sUnknown = FALSE;
    sLeft = sTop = FLT_MAX; /* (Aurora's reset: an empty extent) */
    sRight = sBottom = -FLT_MAX;
}

static f32 clampf(f32 v, f32 lo, f32 hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void GXReadBoundingBox(u16* left, u16* top, u16* right, u16* bottom) {
    flush();
    if (sArmed) {
        const f32 x0 = (f32)sScX, y0 = (f32)sScY;
        const f32 x1 = (f32)(sScX + sScW - 1), y1 = (f32)(sScY + sScH - 1);
        if (sUnknown) {
            sResult[0] = (u16)(x0 > 0.0f ? x0 : 0.0f);
            sResult[1] = (u16)(y0 > 0.0f ? y0 : 0.0f);
            sResult[2] = (u16)(x1 > 0.0f ? x1 : 0.0f);
            sResult[3] = (u16)(y1 > 0.0f ? y1 : 0.0f);
        } else if (sRight < sLeft || sRight < x0 || sLeft > x1 || sBottom < y0 || sTop > y1) {
            sResult[0] = 0x3FF; /* nothing drawn (or all of it outside): the hardware's empty box */
            sResult[1] = 0x3FF;
            sResult[2] = 0;
            sResult[3] = 0;
        } else {
            sResult[0] = (u16)floorf(clampf(sLeft, x0, x1));
            sResult[1] = (u16)floorf(clampf(sTop, y0, y1));
            sResult[2] = (u16)ceilf(clampf(sRight, x0, x1));
            sResult[3] = (u16)ceilf(clampf(sBottom, y0, y1));
        }
        sArmed = FALSE;
    }
    *left = sResult[0];
    *top = sResult[1];
    *right = sResult[2];
    *bottom = sResult[3];
}

/* --- display lists --- */

void GXBeginDisplayList(void* list, u32 size) {
    u32 count;
    flush();
    sRecording = 1;
    sRecBuf = size >= 8 ? (u8*)list : NULL;
    portGXFifoReset();
    portGXFifoSlots(&count);
    sRecStart = count;
    sRecDrawCount = 0;
}

u32 GXEndDisplayList(void) {
    u32 count, id;
    const PPCWGPipe* slots;
    DlRecord* r;
    flush();
    sRecording = 0;
    if (sRecBuf == NULL) {
        return 0;
    }
    slots = portGXFifoSlots(&count);
    if (sDlCount == sDlCap) {
        sDlCap = sDlCap ? sDlCap * 2 : 64;
        DlRecord* grown = (DlRecord*)realloc(sDls, sDlCap * sizeof(DlRecord));
        if (grown == NULL) {
            abort(); /* out of memory */
        }
        sDls = grown;
    }
    id = sDlCount++;
    r = &sDls[id];
    r->drawCount = sRecDrawCount;
    r->draws = NULL;
    r->slots = NULL;
    if (sRecDrawCount != 0) {
        r->draws = (DlDraw*)malloc(sRecDrawCount * sizeof(DlDraw));
        memcpy(r->draws, sRecDraws, sRecDrawCount * sizeof(DlDraw));
        r->slots = (PPCWGPipe*)malloc((count - sRecStart) * sizeof(PPCWGPipe));
        memcpy(r->slots, slots + sRecStart, (count - sRecStart) * sizeof(PPCWGPipe));
    }
    portGXFifoReset();
    {
        const u32 magic = DL_MAGIC;
        memcpy(sRecBuf, &magic, 4);
        memcpy(sRecBuf + 4, &id, 4);
    }
    sRecBuf = NULL;
    return 32; /* (a list's size is a multiple of 32) */
}

/* a disc list's commands; TRUE if one of them changed the state kept here */
static BOOL callStream(const u8* p, u32 n) {
    const u8* end = p + n;
    BOOL state = FALSE;
    while (p < end) {
        const u8 cmd = *p++;
        const u8 op = cmd & 0xF8;
        if (cmd == 0x00 || cmd == 0x48) {
            continue; /* nop, invalidate the vertex cache */
        }
        if (cmd == 0x08) { /* CP register: address, value */
            p += 5;
        } else if (cmd == 0x10) { /* XF registers */
            u32 header, count, addr;
            if (end - p < 4) {
                return TRUE;
            }
            header = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
            p += 4;
            count = ((header >> 16) & 0xFFFF) + 1;
            addr = header & 0xFFFF;
            if (addr < 0x78 && addr % 12 == 0 && count == 12 && end - p >= 48) {
                u32 i;
                f32* flat = &sPnMtx[addr / 12][0][0];
                for (i = 0; i < 12; i++) {
                    flat[i] = component(p, GX_F32, i, 0, 1.0f);
                }
                state = TRUE;
            } else if (addr == 0x1018 && end - p >= 4) {
                sCurrentPnMtx = (p[3] & 0x3F) / 3;
                state = TRUE;
            }
            p += count * 4;
        } else if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
            p += 4; /* indexed XF loads (not used by the game's lists) */
        } else if (cmd == 0x40) {
            p += 8;
        } else if (cmd == 0x61) {
            p += 4;
        } else if (cmd >= 0x80 && cmd < 0xC0) {
            const u32 fmt = cmd & 7;
            u32 count, size;
            if (end - p < 2) {
                return TRUE;
            }
            count = (u32)p[0] << 8 | p[1];
            p += 2;
            size = vtxSize(fmt);
            if ((u64)count * size > (u64)(end - p)) {
                return TRUE;
            }
            if (sArmed && !sUnknown) {
                addStreamVertices(p, count, size, fmt);
            }
            p += count * size;
        } else {
            return TRUE; /* (an opcode this doesn't know: the rest of the list unread) */
        }
    }
    return state;
}

/* Outside a box's window a list is not read at all: the game's disc lists
 * (the map's packed lists, a thousand a frame) hold draws only, and reading
 * them (cold memory) cost a quarter of a run. $TTYD_BBOX_CHECK=1 reads every
 * one anyway and says so if one ever changes the state kept here. */
void GXCallDisplayList(void* list, u32 nbytes) {
    static int sCheck = -1;
    u32 magic, id, i;
    flush();
    if (list == NULL || nbytes == 0) {
        return;
    }
    if (sCheck < 0) {
        sCheck = getenv("TTYD_BBOX_CHECK") != NULL;
    }
    if (!sArmed && !sCheck) {
        return;
    }
    memcpy(&magic, list, 4);
    if (magic == DL_MAGIC && nbytes == 32) {
        memcpy(&id, (const u8*)list + 4, 4);
        if (id < sDlCount && sArmed) {
            const DlRecord* r = &sDls[id];
            for (i = 0; i < r->drawCount && !sUnknown; i++) {
                const DlDraw* d = &r->draws[i];
                /* (the state when it is called, as Aurora reads a list's vertices) */
                addSlotVertices(r->slots + d->start, d->end - d->start, d->count, sVtxDesc, &sVat[d->fmt][GX_VA_POS]);
            }
        }
        return;
    }
    if (callStream((const u8*)list, nbytes) && !sArmed) {
        static int sSaid;
        if (!sSaid) {
            sSaid = 1;
            fprintf(stderr, "[bbox] a display list (%u bytes) changes the transform state outside a box: "
                            "headless reads it only inside one (gx_bbox_pc.c)\n", (unsigned)nbytes);
        }
    }
}
