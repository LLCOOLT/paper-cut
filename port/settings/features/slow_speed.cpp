// Slow speed (Accessibility): the whole game, or only its battles, at 50-90 %,
// for more time for the action commands; fast-forward's opposite. The game's
// clock and its frames both slow (port/src/aurora/speed_aurora.c), so it plays
// exactly as at full speed, only later. Off online: both games keep the same
// pace.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"100% (off)", "90%", "80%", "70%", "60%", "50%"};

const bool sAdded = [] {
  portSettingsDeclare("slow_speed", 0, 0, 5);
  portSettingsDeclare("slow_speed_battle", 0, 0, 1);
  portSettingsAddChoice("Accessibility", "Game speed",
                        "Slower, for more time for the action commands: the game plays the same, only slower. Not "
                        "while playing online (both games must keep the same pace) or fast-forwarding.",
                        PORT_SETTING_CHANGES_PLAY, "slow_speed", kChoices, 6,
                        "slow speed slower slow motion game speed time action commands timing reaction accessibility");
  portSettingsAddToggle("Accessibility", "Slower only in battle",
                        "The field at full speed, the battles at the speed above.", PORT_SETTING_CHANGES_PLAY,
                        "slow_speed_battle", "slow speed battle only action commands");
  return true;
}();

} // namespace
