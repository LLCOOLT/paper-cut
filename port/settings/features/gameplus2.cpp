// Enhancements, the second batch (port/include/port/gameplus.h): the settings
// page's controls for badge loadouts, storage at Save Blocks, the walk-speed
// boost and its controls, the coin and EXP multipliers, the Star Piece and
// Shine Sprite tracker (in a corner over the game), faster field text, the
// jukebox and the shops' junk-item helper. All off by default.
#include "port/bindings.h"
#include "port/gameplus.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
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

// --- The Start menu's forms (port/include/port/pause_forms.h) ---
constexpr const char* kSecondScreen = "Not on the second screen: use the game's Start menu.";

// Loadouts: each one's name over its Wear, Store worn and Rename
enum { kWear, kStore, kRename, kLoadoutRows };
char sLoadoutMessage[128]; // portLoadoutWear's answer (English), shown under the loadouts
int sRenaming = -1;

void loadoutRenamed(const char* name) {
  if (sRenaming >= 0 && name[0] != '\0') {
    portLoadoutRename(sRenaming, name);
  }
  sRenaming = -1;
}

int loadoutRows(PortPauseRow* out, int max) {
  int n = 0;
  if (!portGameplus("badge_loadouts")) {
    out[n++] = portPauseRow("Turn on Badge loadouts above to use them.", nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  if (portLoadoutGet(0, nullptr, 0) < 0) {
    out[n++] = portPauseRow("Shown while a game is being played.", nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  for (int i = 0; i < PORT_LOADOUT_COUNT && n + 4 <= max; i++) {
    char name[PORT_LOADOUT_NAME];
    const int count = portLoadoutGet(i, name, sizeof(name));
    PortPauseRow head = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
    if (count > 0) {
      std::snprintf(head.text, sizeof(head.text), portTrGame("%s: %d badges"), name, count);
    } else {
      std::snprintf(head.text, sizeof(head.text), portTrGame("%s: empty"), name);
    }
    out[n++] = head;
    out[n++] = portPauseRow("Wear", "Wears this loadout in place of the badges worn now (in the field, when Mario's BP "
                                    "covers it).",
                            nullptr, i * kLoadoutRows + kWear, count > 0 ? 0 : PORT_PAUSE_ROW_DIM);
    out[n++] = portPauseRow("Store worn", "This loadout becomes the badges worn now.", nullptr,
                            i * kLoadoutRows + kStore, count > 0 ? PORT_PAUSE_ROW_CONFIRM : 0);
    out[n++] = portPauseRow("Rename", "{A} types its name.", nullptr, i * kLoadoutRows + kRename);
  }
  if (sLoadoutMessage[0] != '\0' && n < max) {
    PortPauseRow r = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    std::snprintf(r.text, sizeof(r.text), "%s", sLoadoutMessage);
    out[n++] = r;
  }
  return n;
}

const char* loadoutAct(int id, int dir) {
  if (dir != 0 || id < 0) {
    return nullptr;
  }
  const int i = id / kLoadoutRows;
  switch (id % kLoadoutRows) {
  case kWear:
    if (gPortCompanionMenu) {
      return kSecondScreen;
    }
    if (portLoadoutGet(i, nullptr, 0) <= 0) {
      return "This loadout is empty: Store worn fills it.";
    }
    portLoadoutWear(i, sLoadoutMessage, sizeof(sLoadoutMessage));
    return nullptr;
  case kStore:
    if (gPortCompanionMenu) {
      return kSecondScreen;
    }
    portLoadoutStore(i);
    sLoadoutMessage[0] = '\0';
    return "Stored: the badges worn now.";
  default: {
    char name[PORT_LOADOUT_NAME];
    portLoadoutGet(i, name, sizeof(name));
    sRenaming = i;
    portPauseTextOpen("Rename", name, PORT_LOADOUT_NAME - 1, PORT_PAUSE_CHARS_NAME, loadoutRenamed);
    return nullptr;
  }
  }
}

// Boost key: as Bindings has it
int boostRows(PortPauseRow* out, int max) {
  if (max < 2) {
    return 0;
  }
  const char* key = portBindingsKeyName(PORT_BIND_WALK_BOOST);
  const char* pad = portBindingsPadName(PORT_BIND_WALK_BOOST);
  out[0] = portPauseRow("Boost key", "Change them under Controls, Bindings.", key[0] != '\0' ? nullptr : "(none)", 0);
  std::snprintf(out[0].value, sizeof(out[0].value), "%s", key);
  out[1] = portPauseRow("Boost button", "Change them under Controls, Bindings.", pad[0] != '\0' ? nullptr : "(none)", 1);
  std::snprintf(out[1].value, sizeof(out[1].value), "%s", pad);
  return 2;
}

const char* boostAct(int, int) { return "Change them under Controls, Bindings."; }

// On this map: found of how many
int mapRows(PortPauseRow* out, int max) {
  int p, pt, s, st, n = 0;
  if (max < 2) {
    return 0;
  }
  if (!portCollectiblesHere(&p, &pt, &s, &st) || (pt == 0 && st == 0)) {
    out[n++] = portPauseRow("None on this map (or no game in the field).", nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  if (pt > 0) {
    out[n] = portPauseRow("Star Pieces", "Star Pieces and Shine Sprites found here.", nullptr, 0);
    std::snprintf(out[n++].value, sizeof(out[0].value), "%d/%d", p, pt);
  }
  if (st > 0) {
    out[n] = portPauseRow("Shine Sprites", "Star Pieces and Shine Sprites found here.", nullptr, 1);
    std::snprintf(out[n++].value, sizeof(out[0].value), "%d/%d", s, st);
  }
  return n;
}

const char* mapAct(int, int) { return nullptr; }

// Tracks: one row going through the tracks heard (left and right), A plays it; and the map's music again
int sTrack; // the track shown (0: none chosen yet)

int heardFrom(int from, int dir) {
  const int count = portBgmCount();
  for (int k = 1; k < count; k++) {
    const int i = ((from - 1 + dir * k) % (count - 1) + (count - 1)) % (count - 1) + 1;
    if (portJukeboxKnown(i)) {
      return i;
    }
  }
  return portJukeboxKnown(from) ? from : 0;
}

int trackRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 2) {
    return 0;
  }
  if (!portGameplus("jukebox")) {
    out[n++] = portPauseRow("Turn on Jukebox above: tracks are listed once heard on this file.", nullptr, nullptr,
                            -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  if (sTrack <= 0 || sTrack >= portBgmCount() || !portJukeboxKnown(sTrack)) {
    sTrack = portJukeboxPlaying() > 0 ? portJukeboxPlaying() : heardFrom(0, 1);
  }
  if (sTrack <= 0) {
    out[n++] = portPauseRow("No tracks heard on this file yet.", nullptr, nullptr, -1, PORT_PAUSE_ROW_DIM);
    return n;
  }
  PortPauseRow r = portPauseRow("Track", "Left and right: the tracks heard on this file. {A} plays it, until the next "
                                         "room's music.",
                                nullptr, 0);
  std::snprintf(r.value, sizeof(r.value), "%s%s", portJukeboxPlaying() == sTrack ? "> " : "", portBgmName(sTrack));
  out[n++] = r;
  out[n++] = portPauseRow("Back to the map's music", nullptr, nullptr, 1,
                          portJukeboxPlaying() < 0 ? PORT_PAUSE_ROW_DIM : 0);
  return n;
}

const char* trackAct(int id, int dir) {
  if (id == 0 && dir != 0) {
    sTrack = heardFrom(sTrack, dir);
    return nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  portJukeboxPlay(id == 0 ? sTrack : -1);
  return nullptr;
}

const PortPauseForm kLoadoutForm = {kTab, "Loadouts", loadoutRows, loadoutAct, PORT_PAUSE_FORM_OWN};
const PortPauseForm kBoostForm = {kTab, "Boost key", boostRows, boostAct};
const PortPauseForm kMapForm = {kTab, "On this map", mapRows, mapAct};
const PortPauseForm kTrackForm = {kTab, "Tracks", trackRows, trackAct};

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
    portSettingsAddToggle(kTab, "Walk-speed boost", "Mario moves faster while the Boost key or button is held.",
                          kPlay, "walk_boost", "walk run speed boost sprint hold");
    portSettingsAddSlider(kTab, "Boost speed", nullptr, kPlay, "walk_boost_speed", 110, 200, "%d %%",
                          "walk run speed boost sprint");
    portSettingsAddSection(kTab, "Boost key", nullptr, 0, boostKeys, "walk boost key keyboard button controller gamepad");
    static const char* const kTimes[] = {"x1", "x1.5", "x2", "x2.5", "x3"};
    portSettingsAddChoice(kTab, "Coins from battles",
                          "A battle's coins, after Merlee, Money Money and co.", kPlay,
                          "coin_multiplier", kTimes, 5, "coin multiplier money battle drop");
    portSettingsAddChoice(kTab, "Star Points from battles", "A battle's Star Points (still at most 100, a level).",
                          kPlay, "exp_multiplier", kTimes, 5, "exp star points multiplier level");
    static const char* const kCorners[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};
    portSettingsAddChoice(kTab, "Star Piece and Shine Sprite tracker",
                          "The map's Star Pieces and Shine Sprites, in a corner.", 0,
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
    portPauseAddForm(&kLoadoutForm);
    portPauseAddForm(&kBoostForm);
    portPauseAddForm(&kMapForm);
    portPauseAddForm(&kTrackForm);
  }
} sRegister;

} // namespace
