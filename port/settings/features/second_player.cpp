// Second player (Controls tab; port/src/common/second_player_pc.c): controller
// 2 plays the partner, in battle and in the field. The section below the
// switch shows which controller is the second, so a player can tell before
// playing whether it's there.
#include "port/settings.h"
#include "port/tr.h"

#include <dolphin/pad.h>
#include <imgui.h>

namespace {

void controllerTwoSection(int) {
  if (PADGetIndexForPort(1) >= 0) {
    const char* name = PADGetName(1);
    ImGui::Text("%s %s", portTr("Controller 2:"), name != nullptr && name[0] != '\0' ? name : portTr("connected"));
  } else {
    ImGui::TextDisabled("%s", portTr("Controller 2: none connected (the first plays the partner too)."));
  }
}

struct SecondPlayerSettings {
  SecondPlayerSettings() {
    portSettingsDeclare("second_player", 0, 0, 1);
    portSettingsAddToggle("Controls", "Second player (co-op)",
                          "Controller 2 plays the partner. In battle: its moves, its action commands and its guards. "
                          "In the field: the stick walks it anywhere, A hops, B talks to or hits what it's next to, X "
                          "is its ability, L or R the next partner. Mario's Y calls it back, and it comes back by "
                          "itself when it gets far away. On a phone or tablet, pair two controllers.",
                          PORT_SETTING_CHANGES_PLAY, "second_player",
                          "second player two players co-op coop multiplayer partner controller 2 p2");
    portSettingsAddSection("Controls", "Controller 2", nullptr, 0, controllerTwoSection, "controller 2 second p2");
  }
} sSecondPlayer;

} // namespace
