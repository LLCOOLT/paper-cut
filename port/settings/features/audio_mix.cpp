// The music, sound effect and ambience volumes (port/src/common/audio_mix_pc.c):
// three sliders under the master volume.
#include "port/settings.h"

namespace {

struct AudioMixSettings {
  AudioMixSettings() {
    // declared here too, so the launcher's settings have them before the game starts
    portSettingsDeclare("music_volume", 100, 0, 100);
    portSettingsDeclare("sfx_volume", 100, 0, 100);
    portSettingsDeclare("ambience_volume", 100, 0, 100);
    portSettingsAddSlider("Audio", "Music", nullptr, 0, "music_volume", 0, 100, "%d%%", "bgm songs volume");
    portSettingsAddSlider("Audio", "Sound effects", nullptr, 0, "sfx_volume", 0, 100, "%d%%", "sfx sounds volume");
    portSettingsAddSlider("Audio", "Ambience", "Wind, water, crowds and other sounds of a place.", 0,
                          "ambience_volume", 0, 100, "%d%%", "environment background volume");
  }
} sAudioMix;

} // namespace
