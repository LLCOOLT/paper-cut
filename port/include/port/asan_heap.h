#ifndef PORT_ASAN_HEAP_H
#define PORT_ASAN_HEAP_H

/* The game's own heaps under AddressSanitizer (a build with -fsanitize=address; nothing in any other build).
 * The OS heaps (OSAlloc.c), the map heap and its battle area (_mapAlloc) and the smart heap (smartAlloc) carve
 * blocks out of one host allocation, the arena, which ASan sees as one block: a read past a game block, or of
 * one freed, would go unseen. Here each heap's free space is marked unusable (poisoned) and each block usable
 * as it is handed out, so ASan stops at the first touch of memory the game doesn't own. The allocators
 * themselves (their headers live in the poisoned space) are left unchecked: PORT_ASAN_ALLOCATOR_BEGIN/END. */

#include <stddef.h>
#include <string.h>

/* (Clang's: its pragmas and attributes below) */
#if defined(__clang__) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PORT_ASAN_HEAPS 1
#endif
#endif

#ifdef PORT_ASAN_HEAPS
#ifdef __cplusplus
extern "C" {
#endif
void __asan_poison_memory_region(void const volatile* addr, size_t size);
void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
#ifdef __cplusplus
}
#endif
#define PORT_ASAN_POISON(p, n) __asan_poison_memory_region((p), (size_t)(n))
#define PORT_ASAN_UNPOISON(p, n) __asan_unpoison_memory_region((p), (size_t)(n))
#define PORT_ASAN_ALLOCATOR_BEGIN \
    _Pragma("clang attribute push(__attribute__((no_sanitize(\"address\"))), apply_to = function)")
#define PORT_ASAN_ALLOCATOR_END _Pragma("clang attribute pop")
/* a copy of a whole linker section (a REL's data: every global in it, and ASan's red zones between them),
   unchecked: memcpy's checks would stop at the first red zone */
__attribute__((no_sanitize("address"), no_builtin)) static inline void portAsanRawCopy(void* dst, const void* src,
                                                                                         size_t n) {
    volatile unsigned char* d = (volatile unsigned char*)dst;
    const volatile unsigned char* s = (const volatile unsigned char*)src;
    while (n-- > 0) {
        *d++ = *s++;
    }
}
__attribute__((no_sanitize("address"), no_builtin)) static inline void portAsanRawZero(void* dst, size_t n) {
    volatile unsigned char* d = (volatile unsigned char*)dst;
    while (n-- > 0) {
        *d++ = 0;
    }
}
#define PORT_SECTION_COPY(d, s, n) portAsanRawCopy((d), (s), (n))
#define PORT_SECTION_ZERO(d, n) portAsanRawZero((d), (n))
#else
#define PORT_SECTION_COPY(d, s, n) memcpy((d), (s), (n))
#define PORT_SECTION_ZERO(d, n) memset((d), 0, (n))
#define PORT_ASAN_POISON(p, n) ((void)0)
#define PORT_ASAN_UNPOISON(p, n) ((void)0)
#define PORT_ASAN_ALLOCATOR_BEGIN
#define PORT_ASAN_ALLOCATOR_END
#endif

#endif
