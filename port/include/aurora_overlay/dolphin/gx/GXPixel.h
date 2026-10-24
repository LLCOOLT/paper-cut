/* Aurora build: Aurora's <dolphin/gx/GXPixel.h>. Aurora implements only the
 * CLEAR/COPY/NOOP logic ops of GXSetBlendMode (others are fatal);
 * port/src/aurora/gx_compat_aurora.c maps the ones the game uses to blends. */
#include_next <dolphin/gx/GXPixel.h>

#ifndef PORT_AURORA_OVERLAY_GXPIXEL_H
#define PORT_AURORA_OVERLAY_GXPIXEL_H
#ifdef __cplusplus
extern "C" {
#endif
void portGXSetBlendMode(GXBlendMode type, GXBlendFactor src_factor, GXBlendFactor dst_factor, GXLogicOp op);
#ifdef __cplusplus
}
#endif
#define GXSetBlendMode portGXSetBlendMode
#endif
