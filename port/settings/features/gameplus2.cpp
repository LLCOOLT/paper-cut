// Gameplay+, the second batch (port/include/port/gameplus.h): the settings
// page's controls for badge loadouts, storage at Save Blocks, the walk-speed
// boost and its controls, the coin and EXP multipliers, the Star Piece and
// Shine Sprite tracker (in a corner over the game), faster field text, the
// jukebox and the shops' junk-item helper. All off by default.
#include "port/bindings.h"
#include "port/gameplus.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <cstdio>
#include <cstring>

namespace {

constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;
constexpr const char* kTab = "Enhancements";

// the walk-speed boost's key and controller button (Controls > Bindings; at
// first the old Boost key and Boost button choices)
int walkBoostHeld() { return portBindingsHeld(PORT_BIND_WALK_BOOST); }

// what they are now
void boostKeys(int) {
  const char* key = portBindingsKeyName(PORT_BIND_WALK_BOOST);
  const char* pad = portBindingsPadName(PORT_BIND_WALK_BOOST);
  ImGui::Text("%s  /  %s", key[0] != 0 ? key : portTr("(none)"), pad[0] != 0 ? pad : portTr("(none)"));
  ImGui::SameLine();
  ImGui::TextDisabled("%s", portTr("Change them under Controls > Bindings."));
}

// Badge loadouts: a row per loadout, its name editable
void loadouts(int) {
  static char sNames[PORT_LOADOUT_COUNT][PORT_LOADOUT_NAME];
  static char sMessage[128];
  if (!portGameplus("badge_loadouts")) {
    ImGui::TextDisabled("%s", portTr("Turn on Badge loadouts above to use them."));
    return;
  }
  if (portLoadoutGet(0, nullptr, 0) < 0) {
    ImGui::TextDisabled("%s", portTr("Shown while a game is being played."));
    return;
  }
  for (int i = 0; i < PORT_LOADOUT_COUNT; i++) {
    ImGui::PushID(i);
    char name[PORT_LOADOUT_NAME];
    const int count = portLoadoutGet(i, name, sizeof(name));
    static bool sEditing[PORT_LOADOUT_COUNT];
    if (!sEditing[i]) { // the file's name, unless it is being typed
      std::snprintf(sNames[i], sizeof(sNames[i]), "%s", name);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
    if (ImGui::InputTextWithHint("##name", "Name", sNames[i], sizeof(sNames[i]))) {
      portLoadoutRename(i, sNames[i]);
    }
    sEditing[i] = ImGui::IsItemActive();
    ImGui::SameLine();
    if (count > 0) {
      ImGui::Text(portTr("%2d badges"), count);
    } else {
      ImGui::TextDisabled("%s", portTr("  empty  "));
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Store worn"))) {
      const int n = portLoadoutStore(i);
      std::snprintf(sMessage, sizeof(sMessage), "Loadout %d: the %d badges worn now.", i + 1, n);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(count <= 0);
    if (ImGui::Button(portTrLabel("Wear"))) {
      portLoadoutWear(i, sMessage, sizeof(sMessage));
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (sMessage[0] != '\0') {
    ImGui::TextWrapped("%s", sMessage);
  }
}

// the Star Pieces and Shine Sprites found here, as "Star Pieces 1/3  Shine Sprites 0/1"
bool collectiblesText(char* out, size_t size) {
  int p, pt, s, st;
  if (!portCollectiblesHere(&p, &pt, &s, &st)) {
    return false;
  }
  int n = 0;
  if (pt > 0) {
    n += std::snprintf(out + n, size - n, "Star Pieces %d/%d", p, pt);
  }
  if (st > 0) {
    std::snprintf(out + n, size - n, "%sShine Sprites %d/%d", n ? "   " : "", s, st);
  }
  return true;
}

void collectibles(int) {
  char text[96];
  if (collectiblesText(text, sizeof(text))) {
    ImGui::TextUnformatted(text);
  } else {
    ImGui::TextDisabled("%s", portTr("None on this map (or no game in the field)."));
  }
}

// Jukebox: the tracks heard on this file
void jukebox(int) {
  if (!portGameplus("jukebox")) {
    ImGui::TextDisabled("%s", portTr("Turn on Jukebox above: tracks are listed once heard on this file."));
    return;
  }
  int heard = 0;
  for (int i = 1; i < portBgmCount(); i++) {
    heard += portJukeboxKnown(i);
  }
  ImGui::Text(portTr(heard == 1 ? "%d track heard on this file" : "%d tracks heard on this file"), heard);
  ImGui::SameLine();
  ImGui::BeginDisabled(portJukeboxPlaying() < 0);
  if (ImGui::Button(portTrLabel("Back to the map's music"))) {
    portJukeboxPlay(-1);
  }
  ImGui::EndDisabled();
  if (heard == 0) {
    return;
  }
  if (ImGui::BeginChild("##tracks", ImVec2(0.f, ImGui::GetFontSize() * 12.f), ImGuiChildFlags_Borders)) {
    for (int i = 1; i < portBgmCount(); i++) {
      if (portJukeboxKnown(i) && ImGui::Selectable(portBgmName(i), portJukeboxPlaying() == i)) {
        portJukeboxPlay(i);
      }
    }
  }
  ImGui::EndChild();
  ImGui::TextDisabled("%s", portTr("A track plays until the next room's music, or the button above."));
}

// over the game: the tracker in its corner
void overlay(int pageOpen) {
  const int corner = portGameplus("collectible_tracker");
  char text[96];
  if (corner == 0 || pageOpen || !collectiblesText(text, sizeof(text))) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImVec2 size = ImGui::CalcTextSize(text);
  const float pad = 12.f;
  const ImVec2 at(corner == 1 || corner == 3 ? pad : display.x - size.x - pad,
                  corner <= 2 ? pad : display.y - size.y - pad);
  dl->AddRectFilled(ImVec2(at.x - 6, at.y - 4), ImVec2(at.x + size.x + 6, at.y + size.y + 4), IM_COL32(0, 0, 0, 150),
                    5.f);
  dl->AddText(at, IM_COL32(255, 235, 140, 240), text);
}

struct Register {
  Register() {
    portGameplusDeclare();
    gPortWalkBoostHeld = walkBoostHeld;
    portSettingsAddToggle(kTab, "Badge loadouts",
                          "Store the badges worn as one of four named sets and wear a set again from the settings "
                          "page (All settings, Loadouts), in the field (only when Mario's BP covers it). Kept "
                          "beside the save, not in it.",
                          kPlay, "badge_loadouts", "badge loadout set preset swap bp equip");
    portSettingsAddSection(kTab, "Loadouts", nullptr, PORT_SETTING_IN_GAME, loadouts, "badge loadout set preset");
    portSettingsAddToggle(kTab, "Storage at every Save Block",
                          "A Save Block also offers to store items and take them out, as the Rogueport shop's "
                          "counter does, before saving.",
                          kPlay, "storage_anywhere", "item storage save block keep stored items");
    portSettingsAddToggle(kTab, "Walk-speed boost", "Mario walks and runs faster while the Boost key or Boost "
                                                    "button is held.",
                          kPlay, "walk_boost", "walk run speed boost sprint hold");
    portSettingsAddSlider(kTab, "Boost speed", nullptr, kPlay, "walk_boost_speed", 110, 200, "%d %%",
                          "walk run speed boost sprint");
    portSettingsAddSection(kTab, "Boost key", nullptr, 0, boostKeys, "walk boost key keyboard button controller gamepad");
    static const char* const kTimes[] = {"x1", "x1.5", "x2", "x2.5", "x3"};
    portSettingsAddChoice(kTab, "Coins from battles",
                          "The coins a won battle drops, after Merlee, Money Money and a First Strike.", kPlay,
                          "coin_multiplier", kTimes, 5, "coin multiplier money battle drop");
    portSettingsAddChoice(kTab, "Star Points from battles", "A battle's Star Points (still at most 100, a level).",
                          kPlay, "exp_multiplier", kTimes, 5, "exp star points multiplier level");
    static const char* const kCorners[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};
    portSettingsAddChoice(kTab, "Star Piece and Shine Sprite tracker",
                          "How many of the current map's Star Pieces and Shine Sprites are found, in a corner.", 0,
                          "collectible_tracker", kCorners, 5, "star piece shine sprite tracker collectible missing");
    portSettingsAddSection(kTab, "On this map", "Star Pieces and Shine Sprites found here.", PORT_SETTING_IN_GAME,
                           collectibles, "star piece shine sprite map collectible");
    portSettingsAddToggle(kTab, "Faster text outside battle",
                          "Messages in the field print three times as fast, and their windows open, turn pages and "
                          "close faster (on top of Text speed).",
                          0, "fast_field_text", "text speed fast field dialogue message");
    portSettingsAddToggle(kTab, "Jukebox", "Lists the music heard on this file on the settings page (All settings, "
                                           "Tracks); pick a track to play it.", 0,
                          "jukebox", "jukebox sound test music bgm track");
    portSettingsAddSection(kTab, "Tracks", nullptr, PORT_SETTING_IN_GAME, jukebox, "jukebox sound test music bgm");
    portSettingsAddToggle(kTab, "Junk-item helper in shops",
                          "Selling, an item or badge Mario has more than one of shows how many (a worn badge is "
                          "never marked).",
                          0, "sell_helper", "sell shop junk spare duplicate item badge");
    portSettingsAddOverlay(overlay, nullptr);
  }
} sRegister;

} // namespace
