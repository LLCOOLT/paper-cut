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
    portSettingsDeclare("audience_players", PORT_AUDIENCE_PLAYERS_DEFAULT, 0, 1);
    portSettingsAddToggle("Multiplayer", "Idle players join the audience",
                          "In co-op battles, a player with no character in the fight sits in the crowd: the buttons "
                          "cheer, and a few times a battle {L} throws a Mushroom to your side or {R} a rock at an "
                          "enemy. Online, player 1's setting.",
                          PORT_SETTING_CHANGES_PLAY, "audience_players",
                          "audience crowd idle cheer emote throw rock mushroom player 3 4 co-op coop multiplayer");
  }
} sAudiencePlayers;

} // namespace
