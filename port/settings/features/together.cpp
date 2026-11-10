// Team bonus (Multiplayer > Co-op; port/include/port/together.h, port/src/common/together_pc.c):
// with two players or more, when everyone who acts in a turn of Mario's side lands their action
// command (Nice or better), a "Team bonus!" pop-up and a quarter of a Star Power bar. Online it is
// the host's setting (it goes with the host's settings that change play) and the host's world gives it.
#include "port/settings.h"

namespace {

const bool sAdded = [] {
  portSettingsDeclare("team_bonus", 1, 0, 1);
  portSettingsAddToggle("Multiplayer", "Team bonus",
                        "With two players or more: when everyone who acts in a turn lands their action command, a "
                        "\"Team bonus!\" and a little extra Star Power. Online, the host's setting.",
                        PORT_SETTING_CHANGES_PLAY, "team_bonus",
                        "team bonus co-op coop star power action command together nice multiplayer");
  return true;
}();

} // namespace
