#ifndef PORT_AI_PC_H
#define PORT_AI_PC_H

/* AI DMA with host pointers. AIInitDMA takes a u32 physical address on the
 * GameCube, which can't hold a 64-bit pointer: the PC mixer reports the
 * buffer it just played with portAISetDMABuffer (port/src/common/ai_pc.c),
 * and readers (sound.c's SoundAIDMACallback) use portAIGetDMAStartPtr.
 * The buffer holds native-endian interleaved stereo s16. */
#include <dolphin/types.h>

void portAISetDMABuffer(void* buffer, u32 length);
u8* portAIGetDMAStartPtr(void);
u32 portAIGetDMALengthPC(void);
/* Aurora backend (port/src/aurora/ai_dtk.c): the mixer calls this after each
 * block it outputs (after portAISetDMABuffer) to run the callback registered
 * with AIRegisterDMACallback, as the AI DMA interrupt would. */
void portAIRunDMACallback(void);

#endif
