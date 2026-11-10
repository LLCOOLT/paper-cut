#ifndef PORT_ANIM_PC_H
#define PORT_ANIM_PC_H
/* Anim model loading on PC ('a/<name>' files, filemgr type 5, animdrv.c).
 * See docs/PC_PORT.md, "Loader design".
 *
 * portAnimDecode decodes the big-endian model with the generated descriptors
 * (port/bedecode/anim.spec) into a per-blob arena:
 *   - the AnimationPoseData root and every CPU-read table (shapes, draws,
 *     groups, textures, anim table, each AnimData with its keyframes) become
 *     native structs; vertex position/normal/node base values become native
 *     floats (the CPU copies and animates them into the pose's own buffers);
 *   - GX arrays (colors, texcoords) stay big-endian in the blob, which filemgr
 *     registers with portGXRegisterDiscBlob;
 *   - index tables (posIndex & co., a u32 per index) and the byte-only delta
 *     tables stay in the blob: animdrv reads indices with portBE16/portBE32.
 * The native root does not fit in the disc header (25 pointers), so it lives
 * in the arena and the blob's first 16 bytes are overwritten with a marker
 * and the root pointer: portAnimRoot(blob) is what animdrv uses wherever the
 * GameCube code casts *handle->data to AnimationPoseData*.
 *
 * Idempotent (a decoded blob is recognised by its marker and the registry);
 * decoding a new model at an address that held an earlier one frees the old
 * arena. The blob must not move afterwards (filemgr pins typed files). */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ANIM_MAGIC 0x4D494E41u /* "ANIM" read little-endian; never a disc mSize (BE) here */

struct AnimationPoseData;
typedef struct PortAnimHeader {
    uint32_t magic;                 /* PORT_ANIM_MAGIC */
    uint32_t discSize;              /* the file's mSize */
    struct AnimationPoseData* root; /* native root (arena) */
} PortAnimHeader;

/* size: blob size in bytes, or PORT_DECODE_SIZE_UNKNOWN. name: for errors.
 * Returns the native root, or NULL on error (after the error hook). */
struct AnimationPoseData* portAnimDecode(void* blob, uint32_t size, const char* name);
/* Frees the decoded graph of a blob that is about to be freed. No-op if none. */
void portAnimRelease(void* blob);
/* Releases every decoded model whose blob starts in [lo, hi) (portMemForget). */
void portAnimForgetRange(const void* lo, const void* hi);
/* A pose the port draws as something else: its paper turn stepped as its own draw would (once a frame),
 * without drawing it; *flip -1 when it is drawn mirrored. The angle it faces (src/driver/animdrv.c). */
float portAnimPoseStepTurn(int poseId, float rotY, float* flip);
/* Native root of a decoded blob (NULL for NULL). */
static inline struct AnimationPoseData* portAnimRoot(void* blob) {
    return blob ? ((PortAnimHeader*)blob)->root : (struct AnimationPoseData*)0;
}
/* Error handler for tests (NULL: OSPanic). */
struct PortDecodeCtx;
extern void (*portAnimErrorHook)(struct PortDecodeCtx* ctx, const char* msg);
/* Number of live decoded models (tests, diagnostics). */
uint32_t portAnimLiveCount(void);

#ifdef __cplusplus
}
#endif
#endif
