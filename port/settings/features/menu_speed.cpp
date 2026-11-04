// Menu speed (Interface > Menu speed; port/include/port/menu_speed.h): the
// Start menu's pages slide in over 16 frames and from tab to tab over 24, and
// the next tab takes no input until they settle. Fast halves every slide,
// Instant leaves a flicker of it. The setting travels with an online session
// as a gameplay setting, so both games' menus keep the same frames.
#include "port/menu_speed.h"
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Original", "Fast", "Instant"};

int* sChoice;

const bool sAdded = [] {
  sChoice = portSettingsDeclare("menu_speed", 1, 0, 2);
  portSettingsAddChoice("Interface", "Menu speed",
                        "How fast the Start menu's pages slide in and from tab to tab. Original is the game's own; "
                        "Fast takes half the time; Instant leaves only a flicker of it.",
                        0, "menu_speed", kChoices, 3, "menu speed start pause tabs slide animation fast snappy quick");
  return true;
}();

} // namespace

extern "C" int portMenuFrames(int frames) {
  if (frames <= 0) {
    return frames; // (the first tab's stagger: none)
  }
  const int choice = sChoice != nullptr ? *sChoice : 0;
  const int divisor = choice == 2 ? 8 : choice == 1 ? 2 : 1;
  const int n = frames / divisor;
  return n < 1 ? 1 : n;
}
