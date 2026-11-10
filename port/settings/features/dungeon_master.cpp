// Dungeon master (a party game: the Multiplayer page's Party; port/include/port/dungeon_master.h,
// docs/DUNGEON_MASTER.md): another player plays the enemies' side in the story's
// battles, picking each enemy's move and target from its own and holding its
// wind-up. The player who does: 2, 3 or 4 (online, the guest is player 2).
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Off", "Player 2", "Player 3", "Player 4"};

const bool sAdded = [] {
  portSettingsDeclare("dungeon_master", 0, 0, 3);
  portSettingsAddChoice("Multiplayer", "Dungeon master",
                        "Another player plays the enemies in the story's battles: on each enemy's turn they pick one "
                        "of its own moves and its target, and can hold its wind-up before the strike. Mario's side "
                        "plays as ever. Needs that player's controller. As a party game: the Multiplayer page, Party.",
                        PORT_SETTING_CHANGES_PLAY, "dungeon_master", kChoices, 4,
                        "dungeon master dm enemies enemy side player 2 versus control the enemies multiplayer");
  // A party game now: the Multiplayer page's Party is the way in (together_aurora.c sets the key for its
  // boss rush and puts it back after; the Start menu's Modes hub keeps its row, modes_hub_links.cpp). The
  // row under Co-op is hidden; the key and its console-accurate lock stay, and online's settings exchange
  // still carries it.
  portSettingsSetShown("dungeon_master", [] { return 0; });
  return true;
}();

} // namespace
