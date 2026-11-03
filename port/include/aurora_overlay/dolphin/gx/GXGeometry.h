/* Aurora build: Aurora's GXGeometry.h, with the game's texgen calls checked
 * (port/src/aurora/gx_compat_aurora.c). Aurora aborts on a texgen source it
 * does not know, and caches the pipeline first, so one bad call would also
 * abort every later run at startup; the wrapper reports the caller and
 * passes a valid source instead. */
#include_next <dolphin/gx/GXGeometry.h>

#ifndef PORT_AURORA_OVERLAY_GXGEOMETRY_H
#define PORT_AURORA_OVERLAY_GXGEOMETRY_H
#ifndef __cplusplus
void portGXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx,
                           GXBool normalize, u32 postmtx);
#define GXSetTexCoordGen2 portGXSetTexCoordGen2
#define GXSetTexCoordGen(dst, func, src, mtx) portGXSetTexCoordGen2((dst), (func), (src), (mtx), GX_FALSE, GX_PTIDENTITY)
#endif
#endif
