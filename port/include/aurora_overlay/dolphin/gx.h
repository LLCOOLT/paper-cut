/* Aurora build: Aurora's <dolphin/gx.h>, plus what the game gets from the
 * 2004 SDK's gx.h: <dolphin/os.h> (reached there through GXVert.h; OSTime,
 * OSRoundUp32B, ...) and the GX functions Aurora does not declare.
 * port/src/aurora/sdk_aurora.c defines them. */
#include_next <dolphin/gx.h>

#ifndef PORT_AURORA_OVERLAY_GX_H
#define PORT_AURORA_OVERLAY_GX_H
#include <dolphin/os.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXSetDrawSync(u16 token);
u16 GXReadDrawSync(void);
void GXClearBoundingBox(void);
void GXReadBoundingBox(u16* left, u16* top, u16* right, u16* bottom);
void GXSetZScaleOffset(f32 scale, f32 offset);
void __GXSetIndirectMask(u32 mask);
void GXResetWriteGatherPipe(void);
u32 GXGetOverflowCount(void);
u32 GXResetOverflowCount(void);
void GXSetGPMetric(GXPerf0 perf0, GXPerf1 perf1);
void GXClearGPMetric(void);
void GXInitTexPreLoadRegion(GXTexRegion* region, u32 tmem_even, u32 size_even, u32 tmem_odd, u32 size_odd);
void GXPreLoadEntireTexture(GXTexObj* tex_obj, GXTexRegion* region);
void GXLoadTexObjPreLoaded(GXTexObj* obj, GXTexRegion* region, GXTexMapID id);

#ifdef __cplusplus
}
#endif
#endif
