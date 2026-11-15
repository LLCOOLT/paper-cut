// Brothers mode (Multiplayer tab; port/include/port/brothers.h,
// docs/BROTHERS.md): Luigi joins Mario as a second hero, played by the
// second controller, in the field and in battle.
#include "port/brothers.h"
#include "port/settings.h"

namespace {

struct BrothersSettings {
  BrothersSettings() {
    portSettingsDeclare("brothers_mode", 0, 0, 1);
    portSettingsAddToggle("Multiplayer", "Brothers mode",
                          "Luigi joins Mario as a second hero with his own turn, HP and FP in battle, played by "
                          "player 2 ({START} on another controller) or following Mario when alone. Turns Play as "
                          "off. Saves are not changed.",
                          PORT_SETTING_CHANGES_PLAY, "brothers_mode",
                          "brothers luigi mario second hero player 2 p2 co-op coop multiplayer");
  }
} sBrothers;

} // namespace
