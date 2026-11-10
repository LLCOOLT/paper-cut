// Leave the room from the settings page (General): the Start menu's Online page has Leave, but the Start
// menu can't open everywhere (a shared battle, a scene), and the page always can; the same leave as the
// Online page's (portStateSyncLeave).
#include "port/settings.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "../settings_ui.h"

#include <imgui.h>

namespace {

void section(int inGame) {
  (void)inGame;
  if (portStateSyncPhase() == PORT_SYNC_OFF) {
    portui::hint("Not in a room.");
    return;
  }
  const char* code = portStateSyncCode();
  if (code != nullptr && code[0] != '\0') {
    ImGui::TextUnformatted(portTrf("Room %s", code));
  }
  if (ImGui::Button(portTrLabel("Leave the room##settings_leave"))) {
    portStateSyncLeave();
  }
}

const bool sAdded = [] {
  portSettingsAddSection("General", "Online room",
                         "Leave the room you play in online, from anywhere (the Start menu's Online page has it too).",
                         PORT_SETTING_IN_GAME, section, "online room leave quit disconnect multiplayer host join");
  return true;
}();

} // namespace
