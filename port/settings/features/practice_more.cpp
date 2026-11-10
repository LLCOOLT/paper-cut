// More Practice powers (port/include/port/practice_more.h): a badge given
// and worn at once, in battle too (GitHub #13); the partners' HP, max HP and
// rank (#3); a warp to any room, searchable, with a safe spawn (#4), on the
// Practice tab and, outside it, in Enhancements; or on the warp map, a Start menu page in the game's
// windows (port/include/port/warp_map.h): the full page's List / Map choice, and the Start menu's
// Settings tab, Enhancements, Field, "Warp map".
#include "port/practice_more.h"
#include "port/pause_forms.h"
#include "port/warp_map.h"
#include "port/practice_tools.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" void portSettingsToggle(void); // settings_ui.cpp: the full page opened or closed
extern "C" int portSettingsPageIsOpen(void);
extern "C" void portSettingsOpenAt(const char* tab);

namespace {

constexpr unsigned kInGame = PORT_SETTING_IN_GAME;

// #13: any badge (the enemies' and bosses' too), given and worn now
void badgeSection(int) {
  if (const char* why = portPracticeEditRefusal()) { // online, a replay: no edits (port/include/port/practice_tools.h)
    ImGui::TextDisabled("%s", portTr(why));
    return;
  }
  static int sBadge = -1;
  static const char* sMessage = "";
  char name[96];
  const int first = portPracticeItemFirst(1), end = portPracticeItemEnd(1);
  if (sBadge < first || sBadge >= end || !portPracticeItemListed(sBadge)) {
    sBadge = first;
    while (sBadge < end - 1 && !portPracticeItemListed(sBadge)) {
      sBadge++; // (the table's unused "Invalid Item" entries are left out, as in Items)
    }
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  if (ImGui::BeginCombo("##badge", portPracticeItemName(sBadge, name, sizeof(name)))) {
    for (int id = first; id < end; id++) {
      if (!portPracticeItemListed(id)) {
        continue;
      }
      ImGui::PushID(id);
      if (ImGui::Selectable(portPracticeItemName(id, name, sizeof(name)), id == sBadge)) {
        sBadge = id;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Give and wear"))) {
    sMessage = portPracticeGiveBadgeWorn(sBadge) ? "Worn now." : "No room";
  }
  if (sMessage[0] != '\0') {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr(sMessage));
  }
  ImGui::TextDisabled("%s", portTr("In a battle it counts at once. Items: the Items section's Give works in battle too."));
}

// #3: each partner in the party
void partySection(int) {
  if (const char* why = portPracticeEditRefusal()) { // online, a replay: no edits (port/include/port/practice_tools.h)
    ImGui::TextDisabled("%s", portTr(why));
    return;
  }
  static const char* const kFields[PORT_PARTY_FIELDS] = {"HP", "Max HP", "Rank"};
  bool any = false;
  for (int p = 1; p <= 7; p++) {
    if (portPracticeParty(p, PORT_PARTY_HP) < 0) {
      continue;
    }
    any = true;
    char name[64];
    ImGui::PushID(p);
    ImGui::TextUnformatted(portPracticePartyName(p, name, sizeof(name)));
    for (int f = 0; f < PORT_PARTY_FIELDS; f++) {
      int v = portPracticeParty(p, f);
      ImGui::SameLine(ImGui::GetFontSize() * (7.f + f * 8.f));
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
      ImGui::PushID(f);
      if (f == PORT_PARTY_RANK) {
        static const char* const kRanks[] = {"Normal", "Super", "Ultra"};
        const char* names[3] = {portTr(kRanks[0]), portTr(kRanks[1]), portTr(kRanks[2])};
        if (ImGui::Combo(portTr(kFields[f]), &v, names, 3)) {
          portPracticeSetParty(p, f, v);
        }
      } else if (ImGui::InputInt(portTr(kFields[f]), &v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) ||
                 ImGui::IsItemDeactivatedAfterEdit()) {
        portPracticeSetParty(p, f, v);
      }
      ImGui::PopID();
    }
    ImGui::PopID();
  }
  if (!any) {
    ImGui::TextDisabled("%s", portTr("No partner has joined yet."));
  }
}

// #4: the warp map, in the Start menu: the full page closes and the menu opens on it
void warpMapChoice() {
  ImGui::TextWrapped("%s", portTr("Every room on the Journal's map of the world: pick an area, then a room, then "
                                  "a way in. It opens in the Start menu, in the game's own windows."));
  const char* why = portWarpMapRefusal();
  ImGui::BeginDisabled(why != nullptr);
  if (ImGui::Button(portTrLabel("Open the warp map"))) {
    portWarpMapRequest();
    if (portSettingsPageIsOpen()) {
      portSettingsToggle(); // (closed: the menu opens on the map, or the Start menu under it shows it)
    }
  }
  ImGui::EndDisabled();
  if (why != nullptr) {
    ImGui::TextDisabled("%s", portTr(why));
  }
}

// #4: every room, found by its name, its area's or its id; or the map
void warpSection(int) {
  static int sMode; // 0 the list, 1 the map
  if (ImGui::RadioButton(portTrLabel("List##warpmode"), sMode == 0)) {
    sMode = 0;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton(portTrLabel("Map##warpmode"), sMode == 1)) {
    sMode = 1;
  }
  if (sMode == 1) {
    warpMapChoice();
    return;
  }
  static char sFilter[48];
  static int sRoom = -1;
  static int sEntrance; // 0: the safe spawn
  static const char* sMessage = "";
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::InputTextWithHint("##find", portTr("Search: area or room id"), sFilter, sizeof(sFilter));
  std::string f(sFilter);
  std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  if (ImGui::BeginChild("##rooms", ImVec2(0.f, ImGui::GetFontSize() * 9.f), ImGuiChildFlags_Borders)) {
    for (int i = 0; i < portRoomWarpCount(); i++) {
      char label[128];
      std::snprintf(label, sizeof(label), "%s  %s", portRoomWarpMap(i), portRoomWarpArea(i));
      std::string low(label);
      std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      if (!f.empty() && low.find(f) == std::string::npos) {
        continue;
      }
      ImGui::PushID(i);
      if (ImGui::Selectable(label, i == sRoom)) {
        sRoom = i;
        sEntrance = 0;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  if (sRoom < 0 || sRoom >= portRoomWarpCount()) {
    ImGui::TextDisabled("%s", portTr("Pick a room."));
    return;
  }
  const char* entrances[32];
  const int n = portRoomWarpEntrances(portRoomWarpMap(sRoom), entrances, 32);
  const char* names[33];
  names[0] = portTr("Safe spawn (the room's start)");
  for (int i = 0; i < n; i++) {
    names[i + 1] = entrances[i];
  }
  if (sEntrance > n) {
    sEntrance = 0;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::Combo("##entrance", &sEntrance, names, n + 1);
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Warp"))) {
    sMessage = portRoomWarpRequest(portRoomWarpMap(sRoom), sEntrance > 0 ? entrances[sEntrance - 1] : nullptr)
                   ? ""
               : portPracticeModeRefusal() != nullptr ? portPracticeModeRefusal() // (a mode's by name)
                                                      : "Only in the field, and not online or in a replay.";
  }
  if (sMessage[0] != '\0') {
    ImGui::TextDisabled("%s", portPracticeRefusalShown(sMessage));
  }
  ImGui::TextDisabled("%s", portTr("A warp counts as practice: no achievements until a file is loaded."));
}

// the Start menu's Settings tab, Enhancements: the warp map, and the list on the full page
int warpFormRows(PortPauseRow* out, int max) {
  if (max < 2) {
    return 0;
  }
  // (the same rows everywhere: the Start menu runs in step online; there the map's row says why not)
  out[0] = portPauseRow("Warp map", "Every room on a map of the world: pick an area, a room and a way in.", nullptr,
                        0, portWarpMapAvailable() ? 0u : (unsigned)PORT_PAUSE_ROW_DIM);
  out[1] = portPauseRow("Room list", "Every room in the game, by area or room id.", nullptr, 1);
  return 2;
}

const char* warpFormAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (id == 0) {
    if (const char* why = portWarpMapRefusal()) {
      return why;
    }
    portWarpMapOpen();
  } else if (id == 1) {
    portSettingsOpenAt("Enhancements");
  }
  return nullptr;
}

const PortPauseForm kWarpForm = {"Enhancements", "Warp to any room", warpFormRows, warpFormAct};

struct Register {
  Register() {
    portPauseAddForm(&kWarpForm);
    portSettingsAddSection("Practice", "Badges in battle", "Any badge, the bosses' too, given and worn at once.",
                           kInGame, badgeSection, "badge give wear battle boss enemy cheat practice");
    portSettingsAddSection("Practice", "Party", "Each partner's HP, max HP and rank.", kInGame, partySection,
                           "partner party hp max rank edit cheat practice");
    portSettingsAddSection("Practice", "Warp to a room", nullptr, kInGame, warpSection,
                           "warp map room teleport any area practice");
    portSettingsAddSection("Enhancements", "Warp to any room", "Every room in the game, by area or room id.", kInGame,
                           warpSection, "warp map room teleport any area travel");
  }
} sRegister;

} // namespace
