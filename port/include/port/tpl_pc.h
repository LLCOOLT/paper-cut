#ifndef PORT_TPL_PC_H
#define PORT_TPL_PC_H
/* TPL (texture palette) loading on PC. See docs/PC_PORT.md, "Loader design".
 *
 * portTplDecode decodes the big-endian header graph of a TPL blob with the
 * generated descriptors (port/bedecode/tpl.spec) and writes the native
 * TEXPalette root over the first bytes of the blob, so every caller that
 * holds the blob pointer (filemgr entries, UnpackTexPalette callers) sees a
 * native TEXPalette at the same address, exactly like the in-place fixup on
 * the GameCube. Descriptors/headers live in a per-blob arena; texel and CLUT
 * data pointers point into the blob, which stays big-endian (Aurora decodes
 * GameCube texture and TLUT formats from big-endian data).
 *
 * Idempotent: a blob whose root is already native is returned as is. Decoding
 * a new TPL at an address that held an earlier one frees the earlier arena.
 * The blob must not move afterwards (filemgr pins decoded files). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_TPL_VERSION 0x0020AF30u

struct TEXPalette;
/* size: blob size in bytes, or PORT_DECODE_SIZE_UNKNOWN. name: for errors. */
struct TEXPalette* portTplDecode(void* blob, uint32_t size, const char* name);
/* Frees the decoded graph of a blob that is about to be freed. No-op if none. */
void portTplRelease(void* blob);
/* Releases every decoded TPL whose blob starts in [lo, hi) (portMemForget);
 * detached graphs (portTplDetach) are not affected. */
void portTplForgetRange(const void* lo, const void* hi);
/* Copy round trip (mapdrv bmapLoad/bmapUnLoad park the map TPL in ARAM, free
 * it, and copy it back later, possibly to another address). portTplDetach
 * unregisters the decoded graph of blob (so a TPL decoded at that address in
 * the meantime does not free it) and returns a handle (NULL if none).
 * portTplAttach(handle, newBlob) takes the copied blob, whose first bytes are
 * still the native root: it repoints texel/CLUT data into newBlob and
 * registers the graph there. newBlob == NULL frees the graph. */
void* portTplDetach(void* blob);
void portTplAttach(void* handle, void* newBlob);
/* Error handler for tests (NULL: OSPanic). Declared as the decoder's
 * PortDecodeErrorFn; include port/bedecode.h to use it. */
struct PortDecodeCtx;
extern void (*portTplErrorHook)(struct PortDecodeCtx* ctx, const char* msg);
/* Number of live decoded TPLs (tests, diagnostics). */
uint32_t portTplLiveCount(void);
/* Nonzero when pal is the root of a TPL decoded and not yet freed (released, forgotten with its
 * heap, or detached). The game's texture lookups (src/sdk/texPalette.c) check it, so a draw from a
 * TPL that isn't loaded or was freed is skipped with a "[tex] draw skipped" line instead of reading
 * freed memory. A cached lookup: cheap enough for every texture fetch. */
int portTplIsLive(const void* pal);

#ifdef __cplusplus
}
#endif
#endif
