// Rows on the Start menu's Modes hub (port/include/port/modes_hub.h) for modes whose setting lives on
// another tab: each is that setting itself, changed here as on its own tab's page (the same value,
// the same console-accurate lock). Their place on the hub is a layout row in the Modes tab
// (port/settings/settings_layout.cpp) named as the row.
//
// - Dungeon master (Multiplayer > Co-op; port/settings/features/dungeon_master.cpp): under Party games.
#include "port/pause_forms.h"
#include "port/settings.h"

extern "C" int portConsoleAccurateLockedValue(unsigned flags, const int* value); // console_accurate.cpp

namespace {

const char* const kDungeonMaster[] = {"Off", "Player 2", "Player 3", "Player 4"};

int dmRows(PortPauseRow* out, int max) {
  const int* v = portSettingsFind("dungeon_master");
  if (max < 1 || v == nullptr) {
    return 0;
  }
  out[0] = portPauseRow("Dungeon master",
                        "Another player plays the enemies in the story's battles: on each enemy's turn they pick one "
                        "of its own moves and its target, and can hold its wind-up before the strike. Mario's side "
                        "plays as ever. Needs that player's controller; online, the guest is player 2.",
                        kDungeonMaster[*v >= 0 && *v < 4 ? *v : 0], 0);
  return 1;
}

const char* dmAct(int id, int dir) {
  int* v = portSettingsFind("dungeon_master");
  if (id != 0 || v == nullptr || portConsoleAccurateLockedValue(PORT_SETTING_CHANGES_PLAY, v)) {
    return nullptr;
  }
  *v = (*v + (dir < 0 ? 3 : 1)) % 4;
  portSettingsApply();
  portSettingsSave();
  return nullptr;
}

const PortPauseForm kDmForm = {"Modes", "Dungeon master", dmRows, dmAct};

const bool sAdded = [] {
  portPauseAddForm(&kDmForm);
  return true;
}();

} // namespace
