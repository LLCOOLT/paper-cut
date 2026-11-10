/* Headless audio: no device. MusyX (libs/musyx/src/musyx/runtime/hw_pc.c) is run
 * from VIWaitForRetrace in real-time-sized chunks into a null sink (or a WAV file
 * named by $TTYD_AUDIO_WAV); the DVD stream is mixed on top like on Aurora. */
#include <dolphin/types.h>
#include "port/dtk_pc.h"
#include "port/music.h"
#include "port/pm64_sfx.h"

void portAudioMixExtra(s16* out, u32 frames) {
    portDTKMix(out, frames, 32000);
    portMusicMix(out, frames); /* the player's own music (port/include/port/music.h) */
    portPm64SfxMix(out, frames); /* Paper Mario 64's sound effects (port/include/port/pm64_sfx.h) */
}
