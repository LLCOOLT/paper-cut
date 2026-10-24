/* Aurora build: Aurora's <dolphin/gx/GXVert.h>, with GXWGFifo made a compile
 * error. Aurora has no write-gather pipe: its GXWGFifo is the GameCube's
 * hardware address (0xCC008000), so a store through it crashes. Vertex data
 * goes through the GXPosition / GXColor / GXTexCoord calls instead. */
#include_next <dolphin/gx/GXVert.h>

#ifndef PORT_AURORA_OVERLAY_GXVERT_H
#define PORT_AURORA_OVERLAY_GXVERT_H
#undef GXWGFifo
extern volatile PPCWGPipe portAuroraNoWGPipe
    __attribute__((unavailable("no write-gather pipe on Aurora: use the GXPosition/GXColor/GXTexCoord calls")));
#define GXWGFifo portAuroraNoWGPipe
#endif
