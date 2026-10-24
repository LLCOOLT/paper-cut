/* Host-pointer side of AI DMA; see port/include/port/ai_pc.h. */
#include <dolphin/types.h>
#include "port/ai_pc.h"

static u8* sDmaBuffer;
static u32 sDmaLength;

void portAISetDMABuffer(void* buffer, u32 length) {
    sDmaBuffer = (u8*)buffer;
    sDmaLength = length;
}

u8* portAIGetDMAStartPtr(void) {
    return sDmaBuffer;
}

u32 portAIGetDMALengthPC(void) {
    return sDmaLength;
}
