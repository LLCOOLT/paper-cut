#ifndef PORT_MEM_PC_H
#define PORT_MEM_PC_H
/* The port keeps per-address records of loaded data: GX vertex-array
 * registrations (disc blobs are big-endian, port/gx_arrays.h), decoded anim
 * models and TPL graphs. When the game frees a block or clears a whole heap
 * (smartFree, L_smartReInit, memClear: every map change and battle), those
 * records must go too, or new data allocated at the same addresses is taken
 * for the old (a curtain's vertices read as big-endian disc data: the red
 * wedge on the title after the opening demo). src/memory.c calls this. */
void portMemForget(const void* lo, const void* hi);

#endif
