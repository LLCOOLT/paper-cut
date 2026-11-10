#ifndef PORT_PAPER_BOX_H
#define PORT_PAPER_BOX_H

/* The paper captures' box as the GameCube's pixel engine measures it
 * (port/src/common/paper_box_pc.c).
 *
 * A paper form (Mario lying in an inn's bed, the plane, the boat, the
 * flattened and rolled forms, the NPCs' paper turns) is the character drawn
 * offscreen, then copied into a texture sized by the GX bounding box
 * (animPaperPoseDispSub). The hardware box holds the pixels drawn: those
 * that pass the alpha test, so a model's see-through texels are outside it.
 * Aurora's box, and headless's copy of it (gx_bbox_pc.c), is the extent of
 * the vertices instead, which takes in every transparent margin of the
 * cut-out's quads: for Mario's standing pose, 1.7 units of empty texels
 * under his soles and 4.6 beside him. The game sizes and places the paper
 * sheet from that box (its scale, and paperOfsY, the height of the sheet's
 * bottom), so a sheet that lies flat sank by those 1.7 units, under the
 * inns' pillows, and lay half as long again as on the console.
 *
 * Here the capture's own draws are measured again on the CPU, pixel by pixel
 * as the hardware does: each triangle rasterized at the capture's projection
 * and viewport, its texture's alpha read (bilinear, the texture's wrap and
 * animated matrix) against the capture's alpha test (alpha >= 0x80). Draw
 * only: the box sizes the capture and the sheet, nothing the game decides
 * on, and it is the same in every build. */

#include <dolphin/types.h>
#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The texture a draw samples on texture map 0 (hasTex 0: none, its colour's
 * alpha alone), as animSetMaterial_Texture loads it. */
typedef struct PortBoxTex {
    int hasTex;
    const u8* data; /* the GX image (tiled, big-endian) */
    int width, height, format, wrapS, wrapT;
    int linear; /* magnification filter: bilinear (else the nearest texel) */
    const u8* tlut; /* CI formats: the palette (big-endian entries), its format and size */
    int tlutFormat, tlutCount;
    f32 mtx[2][4]; /* GX_TG_MTX2x4 over (s, t, 1, 1) */
} PortBoxTex;

/* Armed between Begin and End (animPaperPoseDispSub around its capture draw). */
extern int gPortPaperBox;
void portPaperBoxBegin(void);
/* One triangle fan as renderProc sends it, under position matrix posMtx
 * (view x model) and the current projection, viewport and scissor: n corners,
 * their positions, texture coordinates (NULL with no texture) and colour
 * alphas. A fan the box can't place (a corner behind the eye, too many
 * corners) leaves the GX box in charge (End returns 0). */
void portPaperBoxFan(MtxPtr posMtx, const PortBoxTex* tex, int n, const Vec* pos, const f32 (*uv)[2],
                     const u8* alpha);
/* A draw the box can't follow (a substituted texture): the GX box stands. */
void portPaperBoxUnknown(void);
/* Disarms; 1 and the box of the pixels that passed (inclusive, as
 * GXReadBoundingBox) when there is one, else 0 (use the GX box). */
int portPaperBoxEnd(u16* left, u16* top, u16* right, u16* bottom);

#ifdef __cplusplus
}
#endif
#endif
