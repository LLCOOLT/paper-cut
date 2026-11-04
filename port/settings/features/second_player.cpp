// Second player (Controls tab; port/src/common/second_player_pc.c): player 2
// plays the partner, in battle and in the field.
//
// Joining: with co-op on and nobody as player 2, A (or Start) on any other
// controller makes it player 2, in play or on this page. The Players section
// below the switch shows who player 1 and player 2 are, starts and stops
// co-op, picks player 2's controller by a press (any controller, player 1's
// too: player 1 then plays on the keyboard), and lets player 2 go. Aurora
// keeps which controller is which player (saved with its mappings).
#include "port/settings.h"
#include "port/tr.h"
#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <cstdio>
#include <string>

namespace {

int* sOn;
bool sChoosing; // the next A or Start on any controller makes it player 2

// Aurora's index of the controller an SDL event came from (-1 none)
s32 indexOf(SDL_JoystickID which) {
  SDL_Gamepad* pad = SDL_GetGamepadFromID(which);
  for (u32 i = 0; pad != nullptr && i < PADCount(); i++) {
    if (PADGetSDLGamepadForIndex(i) == pad) {
      return static_cast<s32>(i);
    }
  }
  return -1;
}

void makePlayerTwo(s32 index) {
  PADSetPortForIndex(static_cast<u32>(index), 1);
  PADSerializeMappings();
  std::printf("[coop] player 2: %s (%s)\n", controllerName(static_cast<u32>(index)).c_str(),
              controllerDriverName(static_cast<u32>(index)).c_str());
  std::fflush(stdout);
}

int inputEvent(const void* ev) {
  const SDL_Event* e = static_cast<const SDL_Event*>(ev);
  if (e->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
      (e->gbutton.button != SDL_GAMEPAD_BUTTON_SOUTH && e->gbutton.button != SDL_GAMEPAD_BUTTON_START)) {
    return 0;
  }
  const s32 index = indexOf(e->gbutton.which);
  if (index < 0) {
    return 0;
  }
  if (sChoosing) {
    sChoosing = false;
    makePlayerTwo(index);
    return 1; // the press chose; it does nothing else
  }
  // drop-in: a controller that isn't player 1's, while nobody is player 2
  if (sOn != nullptr && *sOn && PADGetIndexForPort(1) < 0 && PADGetIndexForPort(0) != index) {
    makePlayerTwo(index);
  }
  return 0;
}

// a player's controller as players know it (button_glyphs.h), and the driver's name as the line's tooltip
std::string nameOf(u32 port) {
  const s32 index = PADGetIndexForPort(port);
  return index >= 0 ? controllerName(static_cast<u32>(index)) : std::string(portTr("a controller"));
}

void driverTooltip(u32 port) {
  const s32 index = PADGetIndexForPort(port);
  if (index >= 0 && ImGui::IsItemHovered()) {
    const std::string driver = controllerDriverName(static_cast<u32>(index));
    if (!driver.empty() && driver != controllerName(static_cast<u32>(index))) {
      ImGui::SetTooltip("%s", driver.c_str());
    }
  }
}

void playersSection(int) {
  if (PADGetIndexForPort(0) >= 0) {
    ImGui::Text(portTr("Player 1 (Mario): %s"), nameOf(0).c_str());
    driverTooltip(0);
  } else {
    ImGui::TextUnformatted(portTr("Player 1 (Mario): the keyboard"));
  }
  if (PADGetIndexForPort(1) >= 0) {
    ImGui::Text(portTr("Player 2 (the partner): %s"), nameOf(1).c_str());
    driverTooltip(1);
  } else {
    ImGui::TextDisabled("%s", portTr(*sOn ? "Player 2: nobody yet. Press A on another controller to join."
                                          : "Player 2: nobody (co-op is off)."));
  }
  if (sChoosing) {
    ImGui::TextUnformatted(portTr("Press A on player 2's controller..."));
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Cancel##coop"))) {
      sChoosing = false;
    }
  } else {
    if (ImGui::Button(portTrLabel(*sOn ? "Stop co-op##coop_on" : "Start co-op##coop_on"))) {
      *sOn = !*sOn;
      portSettingsSave();
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Choose player 2's controller"))) {
      sChoosing = true;
    }
    if (PADGetIndexForPort(1) >= 0) {
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Let player 2 go"))) {
        PADClearPort(1);
        PADSerializeMappings();
      }
    }
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Choosing player 1's controller moves it to player 2, and player 1 plays on the "
                                  "keyboard. Each player's buttons are set under Controller buttons."));
  ImGui::PopStyleColor();
}

struct SecondPlayerSettings {
  SecondPlayerSettings() {
    sOn = portSettingsDeclare("second_player", 0, 0, 1);
    portSettingsAddToggle("Multiplayer", "Second player (co-op)",
                          "Player 2 plays the partner: once it's on, A on another controller joins. In battle: its "
                          "moves, its action commands and its guards. In the field: the stick walks it anywhere, A "
                          "hops, B talks to or hits what it's next to, X is its ability, L or R the next partner. "
                          "Mario's Y calls it back, and it comes back by itself when it gets far away. On a phone "
                          "or tablet, pair two controllers.",
                          PORT_SETTING_CHANGES_PLAY, "second_player",
                          "second player two players co-op coop multiplayer partner controller 2 p2 join");
    portSettingsAddSection("Multiplayer", "Players", nullptr, 0, playersSection,
                           "players controller 2 second p2 join co-op coop keyboard");
    portSettingsAddOverlay(nullptr, inputEvent);
  }
} sSecondPlayer;

} // namespace
