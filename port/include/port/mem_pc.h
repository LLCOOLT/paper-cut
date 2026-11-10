#ifndef PORT_MEM_PC_H
#define PORT_MEM_PC_H
/* The port keeps per-address records of loaded data: GX vertex-array
 * registrations (disc blobs are big-endian, port/gx_arrays.h), decoded anim
 * models and TPL graphs; and Aurora keeps the textures it made from game
 * memory and the EFB copies made into it. When the game frees a block or
 * clears a whole heap (smartFree, L_smartReInit, memClear: every map change
 * and battle), those records must go too, or new data allocated at the same
 * addresses is taken for the old (a curtain's vertices read as big-endian disc
 * data: the red wedge on the title after the opening demo; a capture drawn in
 * place of a model's texture). src/memory.c calls this. */
void portMemForget(const void* lo, const void* hi);

/* Only Aurora's records (EFB copies made into [lo, hi)) and the TPLs that start
 * there: for the single blocks the default heaps and the map heap give back
 * (__memFree, _mapFree). */
void portMemForgetTextures(const void* lo, const void* hi);

/* size bytes moved from one address to another (smartGarbage compacting the
 * smart heap): Aurora's EFB copies made into them follow, and what it had
 * recorded at either place is dropped. */
void portMemMoved(const void* from, const void* to, unsigned int size);

#endif
