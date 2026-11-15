// Duo moves (Multiplayer tab; port/src/common/duo_pc.c, docs/DUO_MOVES.md):
// Mario and the partner spend both their moves on one team attack, with both
// players' action commands at once. Online, the host's setting is the game's
// (settings.txt goes with the session), as for every setting that changes play.
#include "port/duo.h"
#include "port/settings.h"

namespace {

struct DuoMovesSettings {
  DuoMovesSettings() {
    portSettingsDeclare("duo_moves", PORT_DUO_MOVES_DEFAULT, 0, 2);
    static const char* const kChoices[] = {"Off", "Co-op and versus", "Always"};
    portSettingsAddChoice("Multiplayer", "Duo moves",
                          "Team attacks where Mario and the partner strike together, at the end of their move lists. "
                          "With two players each presses {A}; alone, {A} is Mario's and {B} the partner's.",
                          PORT_SETTING_CHANGES_PLAY, "duo_moves", kChoices, 3,
                          "duo team attack combo co-op coop two players versus partner action command");
  }
} sDuoMoves;

} // namespace
