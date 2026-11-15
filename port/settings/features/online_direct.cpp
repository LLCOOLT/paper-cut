// Online play's direct connection (port/include/port/online_ice.h, port/src/common/online_ice_pc.c): the
// setting online_direct, each player's own. Off by default: a direct connection shows the other games
// this one's internet address, which the relay never does.
#include "port/online_ice.h"
#include "port/settings.h"

namespace {

int shown() { return portIceAvailable(); }

struct DirectSettings {
  DirectSettings() {
    portIceWanted(); // (declares it)
    portSettingsAddToggle("Multiplayer", "Direct connection",
                          "In an online room, connect straight to the host's game when both networks allow it (both "
                          "players need this on): quicker, and lighter on the online service. The other player's "
                          "game sees your internet address. If it can't connect or drops, the room carries on as "
                          "before by itself.",
                          0, "online_direct", "direct connection udp p2p peer lag latency online relay nat");
    portSettingsSetShown("online_direct", shown);
  }
} sDirect;

} // namespace
