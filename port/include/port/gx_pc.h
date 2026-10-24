#ifndef PORT_GX_PC_H
#define PORT_GX_PC_H

#include <dolphin/gx/GXVert.h>

/* Slots written since the last reset (see gx_fifo_pc.c). */
const PPCWGPipe* portGXFifoSlots(u32* count);
void portGXFifoReset(void);

#endif
