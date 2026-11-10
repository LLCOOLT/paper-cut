// Speakers (Audio > Speakers): stereo, or 5.1 / 7.1 surround from the game's
// own surround mix (libs/musyx/src/musyx/runtime/hw_pc.c): MusyX's surround
// bus and Pro Logic II's rear pair to the rear speakers instead of matrixed
// into the front two, the front pair as in stereo, the LFE a low pass of it.
// The game's Stereo (its file select's option) is then its Surround; Mono
// stays mono. A device with fewer speakers gets SDL's fold-down.
// Headphones (3D): the same surround mix heard as speakers around the head,
// through a built-in head model (port/src/common/hrtf_pc.c), on two channels.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Stereo", "5.1 surround", "7.1 surround", "Headphones (3D)"};
const char* kRates[] = {"32 kHz (the GameCube's)", "48 kHz"};

const bool sAdded = [] {
  portSettingsDeclare("speakers", 0, 0, 3);
  portSettingsAddChoice("Audio", "Speakers",
                        "Surround: the game's own surround sound to the rear speakers (it was made for Dolby Pro "
                        "Logic II) instead of folded into the front two. Your device's output must be set to 5.1 or "
                        "7.1 too. Headphones (3D): the same surround heard around your head, in front and behind, "
                        "on any headphones.",
                        PORT_SETTING_NEXT_START, "speakers", kChoices, 4,
                        "speakers surround 5.1 7.1 dolby pro logic rear channels home theatre theater headphones "
                        "3d hrtf binaural spatial virtual");
  // the device's rate: the 32 kHz mix resampled to 48 kHz by the game (hw_pc.c resample48), not the system
  portSettingsDeclare("audio_48k", 1, 0, 1);
  portSettingsAddChoice("Audio", "Output rate",
                        "48 kHz: the game's 32 kHz sound resampled by a high-quality filter before it reaches the "
                        "system, which then plays it as it is (most devices run at 48 kHz). 32 kHz: as the "
                        "GameCube, the system resamples it.",
                        PORT_SETTING_NEXT_START, "audio_48k", kRates, 2,
                        "audio sample rate 48khz 48000 32khz resampler quality output hz");
  return true;
}();

} // namespace
