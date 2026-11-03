#ifndef PORT_GX_ARRAYS_H
#define PORT_GX_ARRAYS_H
/* Vertex-array endianness and size for GXSetArray on PC.
 *
 * Aurora's GXSetArray takes (attr, data, size, stride, little_endian). The
 * game calls the 2004 three-argument form; port/aurora_compat.h routes it to
 * portGXSetArraySized (aurora_compat.h), which looks the pointer up here:
 *
 *   RULE: an array that lives in data loaded from disc is big-endian
 *         (le = false); an array the game built or filled in code (static
 *         tables in the executable, heap buffers written by the CPU, skinned
 *         vertices) is native (le = true).
 *
 * Loaders make disc arrays known in one of two ways:
 *   - portGXRegisterDiscBlob(base, size): every array inside [base, base+size)
 *     is big-endian, and its upload size is clamped to the blob's end. Done
 *     for whole files that carry vertex data (map 'd' files, anim models).
 *     Call portGXUnregisterDiscBlob before the buffer is freed or reused.
 *   - portGXRegisterArray(data, size): one big-endian array of known size.
 * portGXRegisterNativeArray(data, size) records a native array's size.
 * Anything not registered is native. Lookups are exact-pointer first, then
 * blob ranges. Thread-safe enough for the game's use (registration happens on
 * the main/DVD threads at load time, lookups at draw time). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void portGXRegisterArray(const void* data, unsigned int size);
void portGXRegisterNativeArray(const void* data, unsigned int size);
void portGXRegisterDiscBlob(const void* base, unsigned int size);
void portGXUnregisterDiscBlob(const void* base);
/* A buffer of known extent and endianness (arrays inside it are sized to its
 * end); kept in a small ring, so register it again before each draw. */
void portGXRegisterArrayRange(const void* base, unsigned int size, int le);
/* Looks up data: returns 1 and fills *size (0 if unknown) / *le if the
 * pointer is registered or inside a disc blob; returns 0 (native, size
 * unknown) otherwise. */
int portGXArrayInfo(const void* data, unsigned int* size, int* le);
/* Drops every registration (blob, array, range) starting in [lo, hi): the
 * memory was freed or its heap cleared (portMemForget, port/mem_pc.h). */
void portGXForgetRange(const void* lo, const void* hi);
/* 1 if data is disc data (big-endian): inside a registered disc blob, or a
 * registered big-endian array. Draw-time ranges do not count, so a buffer's
 * endianness never comes from a stale range over freed memory. */
int portGXIsDiscData(const void* data);

#ifdef __cplusplus
}
#endif
#endif
