// Idle players join the audience (Multiplayer tab; port/include/port/crowd.h,
// port/src/common/crowd_seats_pc.c, docs/PLAYERS.md): in co-op, a player with
// no character in the battle cheers and throws from a seat in the crowd.
#include "port/crowd.h"
#include "port/settings.h"

namespace {

struct AudiencePlayersSettings {
  AudiencePlayersSettings() {
    // (declared as crowd_seats_pc.c does: nothing of the game's state here, before main; portCrowdSeatsOn asks
    // whether Brothers mode's Luigi is out, which reads Mario's work: a crash at start with Brothers mode on)
    portSettingsDeclare("audience_players", 1, 0, 1);
    portSettingsAddToggle("Multiplayer", "Idle players join the audience",
                          "In co-op battles, a player with no character in the fight gets a seat at the front of "
                          "the crowd, tagged in their colour: the {STICK} moves it, {A} {B} {X} {Y} and the {DPAD} "
                          "cheer, boo, clap, jump, wave and laugh. Once a turn, three times a battle, while a "
                          "command is chosen: hold {L} and aim to throw a Mushroom (or Honey Syrup) to Mario or "
                          "the partner, or {R} to throw a rock at an enemy (1 damage, never the last HP). Online "
                          "too, decided by player 1's game.",
                          PORT_SETTING_CHANGES_PLAY, "audience_players",
                          "audience crowd idle cheer emote throw rock mushroom player 3 4 co-op coop multiplayer");
  }
} sAudiencePlayers;

} // namespace
