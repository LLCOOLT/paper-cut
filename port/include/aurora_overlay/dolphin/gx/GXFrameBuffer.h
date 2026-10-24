/* Aurora build: Aurora's GXFrameBuffer.h, with the game's GXCopyDisp routed
 * through portGXCopyDisp, which first takes any requested frame capture
 * (port/src/aurora/shot_aurora.c; GXCopyDisp clears the EFB it reads). */
#include_next <dolphin/gx/GXFrameBuffer.h>

#ifndef PORT_AURORA_OVERLAY_GXFRAMEBUFFER_H
#define PORT_AURORA_OVERLAY_GXFRAMEBUFFER_H
#ifndef __cplusplus
void portGXCopyDisp(void* dest, GXBool clear);
#define GXCopyDisp portGXCopyDisp
#endif
#endif
