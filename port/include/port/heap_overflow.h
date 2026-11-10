#ifndef PORT_HEAP_OVERFLOW_H
#define PORT_HEAP_OVERFLOW_H
/* Room past the game's fixed heaps (port/src/common/heap_overflow_pc.c).
 *
 * The game's heaps have byte sizes chosen for the GameCube's 4-byte pointers.
 * On PC the same structs are larger (8-byte pointers and their alignment), so
 * a heap the GameCube filled to 90% can run out: the game then gets NULL and
 * crashes on it. src/memory.c doubles the fixed heaps, but the US/PAL battle
 * area of the map heap (0x64000 bytes, N_battleMapAlloc) is carved at a fixed
 * size, and a busy scene can still go past any of them. When a heap is full,
 * the allocation comes from host memory instead (the save-state heap, so save
 * states keep it), tied to an owner: the heap or the map area it should have
 * come from. Freeing a block that came from here returns it; clearing the
 * owner (memClear, a new map heap, the battle area's end) returns them all.
 * Nothing changes while the heaps have room. */
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* size bytes, zeroed, 32-byte aligned; what names the heap in the one-time log line */
void* portHeapOverflowAlloc(const void* owner, size_t size, const char* what);
/* p came from portHeapOverflowAlloc (any owner): returned, and 1; else 0 */
int portHeapOverflowFree(void* p);
/* p's size when it came from portHeapOverflowAlloc, else 0 */
size_t portHeapOverflowSize(const void* p);
/* every block of owner returned */
void portHeapOverflowClear(const void* owner);

#ifdef __cplusplus
}
#endif

#endif
