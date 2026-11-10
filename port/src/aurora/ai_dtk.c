/* DVD audio streaming on Aurora. The stream state, ADP decoder and the AI
 * streaming registers are in port/src/common/dtk_core.c; this file opens the
 * stream's source and hands the decoded audio to the mixer.
 *
 * Aurora owns DVDPrepareStream(Async)/DVDCancelStream/DVDStopStreamAtEnd
 * (no-op shims in extern/aurora/lib/dolphin/dvd/dvd.cpp) and they do not call
 * back into the port. TTYD never prepares a stream (its SoundDVD channels are
 * only ever stopped or faded), so nothing is lost; code that wants a stream
 * calls portDTKPrepareFile, and AISetStreamPlayState(0), which the game issues
 * before every DVDCancelStream, silences it. */
#include <dolphin/dvd.h>
#include <string.h>
#include <dolphin/ai.h>
#include "port/ai_pc.h"
#include "port/dtk_pc.h"
#include "port/music.h"
#include "port/pm64_sfx.h"

/* The mixer's output rate (MusyX runs at 32 kHz). */
#define PORT_AUDIO_RATE 32000

/* Own handles, so the game closing its DVDFileInfo can't pull the file away
 * from under a read in flight. Two: the current and the queued stream. */
static DVDFileInfo sFiles[2];
static BOOL sOpen[2];

static s32 dtkRead(void* handle, void* dst, u32 length, u32 offset) {
    return DVDReadPrio((DVDFileInfo*)handle, dst, (s32)length, (s32)offset, 2);
}

BOOL portDTKPrepareFile(const char* path, u32 length, u32 offset) {
    int i;
    for (i = 0; i < 2; i++) {
        if (sOpen[i] && !portDTKUsesHandle(&sFiles[i])) {
            DVDClose(&sFiles[i]);
            sOpen[i] = FALSE;
        }
    }
    for (i = 0; i < 2 && sOpen[i]; i++) {
    }
    if (i == 2 || !DVDOpen(path, &sFiles[i])) {
        return FALSE;
    }
    sOpen[i] = TRUE;
    if (length == 0 && offset <= sFiles[i].length) {
        length = sFiles[i].length - offset;
    }
    portDTKPrepare(&sFiles[i], dtkRead, offset, length);
    return TRUE;
}

/* Called by the MusyX PC mixer (hw_pc) after it has produced `frames`
 * interleaved stereo frames at 32 kHz: add the disc stream on top, and the
 * player's own music (port/include/port/music.h). */
void portAudioMixExtra(s16* out, u32 frames) {
    portDTKMix(out, frames, PORT_AUDIO_RATE);
    portMusicMix(out, frames);
    portPm64SfxMix(out, frames); /* Paper Mario 64's sound effects (port/include/port/pm64_sfx.h) */
}

/* ---- AI DMA registers ----
 * Aurora has no AI. MusyX's PC hardware layer mixes straight into SDL, so
 * DMA here is bookkeeping: the mixer reports each block it outputs with
 * AIInitDMA(pointer, length) (or portAISetDMABuffer) + portAIRunDMACallback. */

static AIDCallback sDmaCallback;
static u32 sDmaStart, sDmaLength;
static BOOL sDmaEnabled, sInit;
static u32 sDspRate = AI_SAMPLERATE_32KHZ;

void AIInit(u8* stack) {
    sInit = TRUE;
}

void AIReset(void) {
    sInit = FALSE;
}

BOOL AICheckInit(void) {
    return sInit;
}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    AIDCallback old = sDmaCallback;
    sDmaCallback = callback;
    return old;
}

void portAIRunDMACallback(void) {
    if (sDmaEnabled && sDmaCallback != NULL) {
        sDmaCallback();
    }
}

/* Aurora's AIInitDMA takes a host pointer. AIGetDMAStartAddr can only return
 * its low 32 bits; use portAIGetDMAStartPtr for the pointer. */
void AIInitDMA(uintptr_t start_addr, u32 length) {
    sDmaStart = (u32)start_addr;
    sDmaLength = length;
    portAISetDMABuffer((void*)start_addr, length);
}

BOOL AIGetDMAEnableFlag(void) {
    return sDmaEnabled;
}

void AIStartDMA(void) {
    sDmaEnabled = TRUE;
}

void AIStopDMA(void) {
    sDmaEnabled = FALSE;
}

u32 AIGetDMABytesLeft(void) {
    return 0;
}

u32 AIGetDMAStartAddr(void) {
    return sDmaStart;
}

u32 AIGetDMALength(void) {
    u32 len = portAIGetDMALengthPC();
    return len != 0 ? len : sDmaLength;
}

void AISetDSPSampleRate(u32 rate) {
    sDspRate = rate;
}

u32 AIGetDSPSampleRate(void) {
    return sDspRate;
}
