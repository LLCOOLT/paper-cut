// Duo moves (Multiplayer tab; port/src/common/duo_pc.c, docs/DUO_MOVES.md):
// Mario and the partner spend both their moves on one team attack, with both
// players' action commands at once. Online, the host's setting is the game's
// (settings.txt goes with the session), as for every setting that changes play.
#include "port/settings.h"

namespace {

struct DuoMovesSettings {
  DuoMovesSettings() {
    portSettingsDeclare("duo_moves", 1, 0, 2);
    static const char* const kChoices[] = {"Off", "Co-op and versus", "Always"};
    portSettingsAddChoice("Multiplayer", "Duo moves",
                          "Team attacks for Mario and the partner, both their moves of the turn at once: at the end "
                          "of Mario's Jump or Hammer list and of the partner's moves. With two players each presses "
                          "{A} for their own; with one (Always, or each side of a versus fight) {A} is Mario's and {B} the "
                          "partner's. A second one for each partner at Ultra rank.",
                          PORT_SETTING_CHANGES_PLAY, "duo_moves", kChoices, 3,
                          "duo team attack combo co-op coop two players versus partner action command");
  }
} sDuoMoves;

} // namespace
