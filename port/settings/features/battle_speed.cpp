// Faster battles (Gameplay+ > Faster battles; port/include/port/battle_speed.h):
// a battle's animations at 1.5x or 2x, back to 1x whenever the player acts
// (an action command, a Stylish move, the command menu, a Star Special), and
// the guard and Superguard windows as long in time as at 1x.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Off", "1.5x", "2x"};

const bool sAdded = [] {
  portSettingsDeclare("battle_speed", 0, 0, 2);
  portSettingsAddChoice("Enhancements", "Faster battles",
                        "Battles play faster, but never while you act: action commands, Stylish moves, the menus and "
                        "Star Specials stay at normal speed, and guards and Superguards keep their timing. Not in "
                        "online co-op.",
                        PORT_SETTING_CHANGES_PLAY, "battle_speed", kChoices, 3,
                        "faster battles battle speed quick fast animations enemy turns");
  return true;
}();

} // namespace
