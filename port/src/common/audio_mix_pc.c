/* The music, sound effect and ambience volumes (port/include/port/audio_mix.h;
 * their sliders: port/settings/features/audio_mix.cpp). The master volume is
 * the device's gain (settings_aurora.c); these scale each voice before the
 * mix, read before every 5 ms quantum so a slider is heard as it moves. */
#include "port/audio_mix.h"
#include "port/settings.h"

void portAudioMixUpdate(void) {
    static int* sVolumes[3];
    static int sLast[3] = {-1, -1, -1};
    int i;
    if (sVolumes[0] == NULL) {
        sVolumes[PORT_MIX_MUSIC] = portSettingsDeclare("music_volume", 100, 0, 100);
        sVolumes[PORT_MIX_SFX] = portSettingsDeclare("sfx_volume", 100, 0, 100);
        sVolumes[PORT_MIX_AMBIENCE] = portSettingsDeclare("ambience_volume", 100, 0, 100);
    }
    for (i = 0; i < 3; i++) {
        if (*sVolumes[i] != sLast[i]) {
            sLast[i] = *sVolumes[i];
            salPcSetCategoryGain(i, sLast[i] / 100.0f);
        }
    }
}
