#ifndef PORT_AURORA_COMPAT_H
#define PORT_AURORA_COMPAT_H
/* Source-compatibility layer between the game (written against the 2004
 * Dolphin SDK) and Aurora's SDK headers. Force-included through
 * port_prefix.h when TTYD_BACKEND_AURORA is defined; Aurora's API stays
 * authoritative, these only adapt call shapes. */

/* GXSetArray: the SDK takes (attr, base, stride); Aurora needs the array
 * size too, (attr, base, size, stride, little_endian). Three-argument calls
 * go to portGXSetArray3, which picks a size (see gx_compat_aurora.c).
 * A five-argument use (Aurora's own prototype) expands to itself. */
#define PORT_GXSETARRAY_PICK(_1, _2, _3, _4, _5, NAME, ...) NAME
#define GXSetArray(...) \
    PORT_GXSETARRAY_PICK(__VA_ARGS__, GXSetArray, _4, portGXSetArray3, _2, _1)(__VA_ARGS__)
/* An array argument (not a pointer) has its exact size known here: pass it,
 * so static vertex tables upload what they hold, not a guessed window. */
#define PORT_ARRAY_BYTES(x) \
    __builtin_choose_expr(__builtin_types_compatible_p(__typeof__(x), __typeof__(&*(x))), 0u, (unsigned)sizeof(x))
#define portGXSetArray3(attr, data, stride) portGXSetArraySized(attr, data, stride, PORT_ARRAY_BYTES(data))
/* attr is a GXAttr; declared with int so this header needs no SDK types.
 * arrayBytes: the array's size, or 0 when the game passed a pointer. */
void portGXSetArraySized(int attr, const void* data, unsigned char stride, unsigned int arrayBytes);
/* Endianness and size of game arrays: see port/gx_arrays.h (disc data is
 * big-endian, arrays built in code are native). */
#include "port/gx_arrays.h"

/* 2004-SDK constants Aurora's headers do not define. */
#define GX_VIEWPORT_SZ 6
#define VI_DISPLAY_PIX_SZ 2

/* Aurora has no synchronous EFB peek; gx_compat_aurora.c answers with the
 * result of the previous frame's request (0 until one arrives). x/y are
 * u16, color a u32* holding 0xAARRGGBB. */
void GXPeekARGB(unsigned short x, unsigned short y, unsigned int* color);

#endif
