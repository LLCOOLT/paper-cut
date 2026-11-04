/* The music, sound effect and ambience volumes (port/include/port/audio_mix.h;
 * their sliders: port/settings/features/volume.cpp). The master volume is
 * the device's gain (settings_aurora.c); these scale each voice before the
 * mix, read before every 5 ms quantum so a slider is heard as it moves. Each
 * has a mute of its own, and all are on the same audio taper as the master
 * (portVolumeGain). */
#include "port/audio_mix.h"
#include "port/settings.h"

float portVolumeGain(int percent) {
    const float v = (percent < 0 ? 0 : percent > 100 ? 100 : percent) / 100.0f;
    return v * v;
}

void portAudioMixUpdate(void) {
    static int* sVolumes[3];
    static int* sMutes[3];
    static int sLast[3] = {-1, -1, -1};
    int i;
    if (sVolumes[0] == NULL) {
        sVolumes[PORT_MIX_MUSIC] = portSettingsDeclare("music_volume", 100, 0, 100);
        sVolumes[PORT_MIX_SFX] = portSettingsDeclare("sfx_volume", 100, 0, 100);
        sVolumes[PORT_MIX_AMBIENCE] = portSettingsDeclare("ambience_volume", 100, 0, 100);
        sMutes[PORT_MIX_MUSIC] = portSettingsDeclare("music_mute", 0, 0, 1);
        sMutes[PORT_MIX_SFX] = portSettingsDeclare("sfx_mute", 0, 0, 1);
        sMutes[PORT_MIX_AMBIENCE] = portSettingsDeclare("ambience_mute", 0, 0, 1);
    }
    for (i = 0; i < 3; i++) {
        /* a muted category reads as 0; its slider keeps its place for unmuting */
        const int now = *sMutes[i] ? 0 : *sVolumes[i];
        if (now != sLast[i]) {
            sLast[i] = now;
            salPcSetCategoryGain(i, portVolumeGain(now));
        }
    }
}
