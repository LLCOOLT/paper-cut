#ifndef PORT_SNAPHEAP_H
#define PORT_SNAPHEAP_H

/* The save-state heap (port/src/common/snapheap_pc.c): host memory the
 * game's data points into (decoded animations, textures, maps, camera
 * paths, MusyX's sample directory) comes from one reserved region whose
 * allocator keeps all of its bookkeeping inside it, so a save state copies
 * the used part of the region and a load puts it back whole. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* portSnapMalloc(size_t size);
void* portSnapCalloc(size_t count, size_t size);
void* portSnapRealloc(void* p, size_t size);
void portSnapFree(void* p);
/* The region's used part: [*base, *base + *size). */
void portSnapHeapExtent(void** base, size_t* size);
/* Before a load writes [base, base + size) back: that much usable (committed
 * on Windows, where the region grows as it is used); 0 if it can't be. */
int portSnapHeapCommit(size_t size);
/* After a load put the region back: the part past its (restored) top is free. */
void portSnapHeapRestored(void);

#ifdef __cplusplus
}
#endif

/* In a source file whose allocations the game keeps pointers to: */
#ifdef PORT_SNAPHEAP_REDIRECT
#define malloc portSnapMalloc
#define calloc portSnapCalloc
#define realloc portSnapRealloc
#define free portSnapFree
#endif

#endif
