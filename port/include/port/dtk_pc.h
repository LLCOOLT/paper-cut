#ifndef PORT_DTK_PC_H
#define PORT_DTK_PC_H

/* DVD audio streaming (DTK): the drive decodes ADP from the disc and the AI
 * mixes it with the DSP output. Here a ring buffer of raw ADP blocks is
 * filled from the game thread (portDTKPump) and decoded, resampled and mixed
 * on the audio thread (portDTKMix). port/src/common/dtk_core.c. */
#include <dolphin/types.h>

#define PORT_ADP_BLOCK_SIZE 32
#define PORT_ADP_BLOCK_FRAMES 28

/* Per-channel decoder history, in 1/64 sample units. */
typedef struct PortADPState {
    s32 hist1[2];
    s32 hist2[2];
} PortADPState;

/* Decode one 32-byte ADP block into 28 interleaved stereo frames (L, R). */
void portADPDecodeBlock(PortADPState* state, const u8* block, s16* out);

/* Reads `length` bytes at `offset` of the stream's source; returns the count read. */
typedef s32 (*PortDTKReadFn)(void* handle, void* dst, u32 length, u32 offset);

/* DVDPrepareStream: when no stream is active this becomes the current one;
 * otherwise it plays when the current one reaches its end. */
void portDTKPrepare(void* handle, PortDTKReadFn read, u32 offset, u32 length);
/* DVDCancelStream: stop at once and drop buffered data. */
void portDTKCancel(void);
/* DVDStopStreamAtEnd: finish the current stream instead of looping it. */
void portDTKStopAtEnd(void);
/* Does `handle` belong to the current or the queued stream? */
BOOL portDTKUsesHandle(void* handle);

/* Game thread (once per frame): refill the ring buffer from the disc. */
void portDTKPump(void);
/* Audio thread: add `frames` stream frames, resampled to `outRate`, into an
 * interleaved stereo s16 buffer (saturating). */
void portDTKMix(s16* out, u32 frames, u32 outRate);

/* Aurora backend: prepare a stream from a disc file (port/src/aurora/ai_dtk.c). */
BOOL portDTKPrepareFile(const char* path, u32 length, u32 offset);
/* Mixer hook: add extra stereo audio (32 kHz) after the MusyX output. */
void portAudioMixExtra(s16* out, u32 frames);

#endif
