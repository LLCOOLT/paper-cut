// Speakers (Audio > Speakers): stereo, or 5.1 / 7.1 surround from the game's
// own surround mix (libs/musyx/src/musyx/runtime/hw_pc.c): MusyX's surround
// bus and Pro Logic II's rear pair to the rear speakers instead of matrixed
// into the front two, the front pair as in stereo, the LFE a low pass of it.
// The game's Stereo (its file select's option) is then its Surround; Mono
// stays mono. A device with fewer speakers gets SDL's fold-down.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Stereo", "5.1 surround", "7.1 surround"};

const bool sAdded = [] {
  portSettingsDeclare("speakers", 0, 0, 2);
  portSettingsAddChoice("Audio", "Speakers",
                        "Surround: the game's own surround sound to the rear speakers (it was made for Dolby Pro "
                        "Logic II) instead of folded into the front two. Your device's output must be set to 5.1 or "
                        "7.1 too.",
                        PORT_SETTING_NEXT_START, "speakers", kChoices, 3,
                        "speakers surround 5.1 7.1 dolby pro logic rear channels home theatre theater");
  return true;
}();

} // namespace
