// The map editor (Modes > Map editor, and the Start menu's row; docs/MAP_EDITOR.md):
// things placed into the room Mario is in, from the game's own mobjs and
// models, seen live (the map edits' runtime places them as it does a mod's,
// port/include/port/map_edits.h), and saved as a mod's maps/<room>.json in the
// user's mods folder. Only in the field, never online or in a replay.
//
//   mouse: left click   select a thing, or place the palette's at the point clicked
//          left drag    move the selected thing over the floor
//          right drag   look around
//   WASD, Q / E         fly; Shift faster
//   arrows, PgUp/PgDn   nudge the selected thing; R turns it
//   F                   the selected thing down on the floor
//   Delete              take it away
//   Ctrl+Z, Ctrl+S      undo, save
//   Tab                 test play (Mario back, the editor's camera off) and back
//   Esc                 close (unsaved changes are saved)
// Controller: left stick fly, right stick look, triggers down and up, A place
// at the picture's middle, X take away, LB / RB the previous / next thing,
// Y the next kind, Back test play, B close.
#include "port/menu_buttons.h"
#include "port/map_edits.h"
#include "port/area_maps.h"
#include "port/mods.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "port/view.h"

#include "../file_util.h"
#include "../../mods/evt_asm.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;

struct Editor {
  bool on = false, test = false;
  std::string room;
  std::map<std::string, std::vector<PortMapThing>> rooms; // the working copies, by room
  std::map<std::string, bool> dirty;
  std::vector<std::vector<PortMapThing>> undo;           // the room's, the last on top
  int sel = -1;
  int palette = PORT_MT_PIPE;
  int model = 0;
  float eye[3] = {0, 100, 300};
  float yaw = 0, pitch = -0.3f;
  bool dragging = false, looking = false;
  bool pageOpen = false; // the settings page over it: its keys are the page's
  char mod[40] = "my-maps";
  std::string said;
  Uint64 saidAt = 0, last = 0;
} E;

std::vector<PortMapThing>& things() { return E.rooms[E.room]; }

std::string modId() { return std::string("local.mapedit.") + E.mod; }

void say(const std::string& text) {
  E.said = text;
  E.saidAt = SDL_GetTicks();
}

bool available() { return !portReplayActive() && !portStateSyncOn() && portMapEditsRoomName()[0] != '\0'; }

// the mod's folder name: letters, digits, - and _
void cleanName() {
  for (char* p = E.mod; *p; p++) {
    if (!std::isalnum((unsigned char)*p) && *p != '-' && *p != '_') {
      *p = '-';
    }
  }
  if (E.mod[0] == '\0') {
    std::snprintf(E.mod, sizeof(E.mod), "my-maps");
  }
}

void pushLive() {
  const auto& t = things();
  portMapEditsSetLive(modId().c_str(), E.room.c_str(), t.data(), int(t.size()));
}

// the room's things from the mod (the store's, as last loaded), once a session
void loadRoom() {
  if (E.rooms.count(E.room) == 0) {
    std::vector<PortMapThing> list;
    const int n = portMapEditsCount(modId().c_str(), E.room.c_str());
    for (int i = 0; i < n; i++) {
      if (const PortMapThing* t = portMapEditsGet(modId().c_str(), E.room.c_str(), i)) {
        list.push_back(*t);
      }
    }
    E.rooms[E.room] = list;
  }
  E.undo.clear();
  E.sel = -1;
  pushLive();
}

void remember() {
  E.undo.push_back(things());
  if (E.undo.size() > 100) {
    E.undo.erase(E.undo.begin());
  }
  E.dirty[E.room] = true;
}

void changed() { pushLive(); }

// the look's direction from yaw and pitch
void forward(float out[3]) {
  out[0] = std::sin(E.yaw) * std::cos(E.pitch);
  out[1] = std::sin(E.pitch);
  out[2] = -std::cos(E.yaw) * std::cos(E.pitch);
}

void applyCamera() {
  float f[3], at[3];
  forward(f);
  for (int i = 0; i < 3; i++) {
    at[i] = E.eye[i] + f[i] * 100.f;
  }
  portViewEditorSet(E.on && !E.test ? 1 : 0, E.eye, at);
}

// the "id" of a mod.json's text ("" when it has none)
std::string manifestId(const std::string& json) {
  const size_t key = json.find("\"id\"");
  if (key == std::string::npos) {
    return {};
  }
  const size_t open = json.find('"', json.find(':', key + 4));
  if (open == std::string::npos) {
    return {};
  }
  const size_t close = json.find('"', open + 1);
  return close == std::string::npos ? std::string() : json.substr(open + 1, close - open - 1);
}

bool anyDirty() {
  for (const auto& [room, d] : E.dirty) {
    if (d) {
      return true;
    }
  }
  return false;
}

bool save() {
  cleanName();
  const std::string dir = portfile::join(portModsDir(), E.mod);
  const std::string manifest = dir + "/mod.json";
  const std::string existing = portfile::readFile(manifest);
  // a folder of another mod's (one installed, not made here): its own maps/*.json would be
  // replaced by this editor's list, which never read them
  if (!existing.empty() && manifestId(existing) != modId()) {
    say(portTr("That folder is another mod's: choose another name to save to"));
    return false;
  }
  SDL_CreateDirectory((dir + "/maps").c_str());
  if (existing.empty()) {
    std::string json = "{\n  \"id\": \"" + modId() + "\",\n  \"name\": \"" + E.mod +
                       "\",\n  \"version\": \"1.0\",\n  \"description\": \"Made with the map editor.\"\n}\n";
    if (!portfile::writeAll(manifest, json.data(), json.size())) {
      say(portTr("Couldn't write the mod's folder"));
      return false;
    }
  }
  // the mod's files by the room they hold ("room": "gor_01" in maps/town.json too), and whether the editor read
  // all of each: a thing with an error isn't in its list, so writing over that file would drop it
  std::map<std::string, std::vector<std::string>> filesOf;
  std::map<std::string, bool> unreadable;
  {
    int n = 0;
    char** names = SDL_GlobDirectory((dir + "/maps").c_str(), "*.json", 0, &n);
    for (int i = 0; names != nullptr && i < n; i++) {
      const std::string file = std::string("maps/") + names[i];
      std::string room;
      std::vector<PortMapThing> got;
      std::vector<std::string> errors;
      const bool ok = portMapEditsParse(portfile::readFile(dir + "/" + file), file, room, got, errors);
      if (!room.empty()) {
        filesOf[room].push_back(file);
        unreadable[room] = unreadable[room] || !ok || !errors.empty();
      }
    }
    SDL_free(names);
  }
  int rooms = 0;
  for (auto& [room, list] : E.rooms) {
    if (!E.dirty[room]) {
      continue;
    }
    if (unreadable[room]) {
      say(portTr("The room's file has things the editor can't read: fix them by hand first"));
      std::printf("[mapeditor] %s not saved: its file has things with errors\n", room.c_str());
      return false;
    }
    // (written back to the file the things came from; another file of the same room's goes, its things here)
    const std::vector<std::string>& from = filesOf[room];
    const std::string path = dir + "/" + (from.empty() ? "maps/" + room + ".json" : from[0]);
    if (list.empty()) {
      SDL_RemovePath(path.c_str());
    } else {
      const std::string text = portMapEditsWrite(room, list);
      if (!portfile::writeAll(path, text.data(), text.size())) {
        say(portTr("Couldn't write the room's file"));
        return false;
      }
    }
    for (size_t i = 1; i < from.size(); i++) {
      SDL_RemovePath((dir + "/" + from[i]).c_str()); // (once the room's things are all in the one)
    }
    E.dirty[room] = false;
    rooms++;
  }
  portModsReload(); // (the mod manager lists it; the store has its things)
  say(rooms > 0 ? portTr("Saved to the mods folder") : portTr("Nothing to save"));
  return true;
}

void start() {
  if (!available()) {
    return;
  }
  cleanName();
  E.on = true;
  E.test = false;
  E.room = portMapEditsRoomName();
  float at[3];
  if (portViewEditorGame(E.eye, at)) {
    const float dx = at[0] - E.eye[0], dy = at[1] - E.eye[1], dz = at[2] - E.eye[2];
    E.yaw = std::atan2(dx, -dz);
    E.pitch = std::atan2(dy, std::sqrt(dx * dx + dz * dz));
  }
  E.last = SDL_GetTicksNS();
  loadRoom();
  portMapEditsHold(1);
  applyCamera();
}

void stop() {
  // unsaved edits are saved; when they can't be (a replay or online play starting, which
  // closes the editor, or a failed save) they are kept for the next time it opens
  bool keep = false;
  if (anyDirty()) {
    keep = !available() || !save();
    if (keep && !available()) {
      say(portTr("Unsaved changes kept: they are saved when the editor next closes"));
    }
  }
  E.on = false;
  E.dragging = E.looking = false;
  portViewEditorSet(0, E.eye, E.eye);
  portMapEditsHold(0);
  portMapEditsSetLive(nullptr, nullptr, nullptr, 0); // (from the mod's files from now on)
  if (!keep) {
    E.rooms.clear();
    E.dirty.clear();
  }
  E.undo.clear();
}

// "Mod to save to" changed (to): the edits so far saved to the mod they were made for first
// (refused while they can't be), then the new mod's things read for this room, its undo fresh
bool switchMod(const char* to) {
  char name[sizeof(E.mod)];
  std::snprintf(name, sizeof(name), "%s", to);
  if (std::strcmp(name, E.mod) == 0) {
    return true;
  }
  if (anyDirty() && (!available() || !save())) {
    say(portTr("Save this mod's changes first (or discard them)"));
    return false;
  }
  std::snprintf(E.mod, sizeof(E.mod), "%s", name);
  cleanName();
  E.rooms.clear();
  E.dirty.clear();
  E.undo.clear();
  if (E.on) {
    loadRoom(); // (that mod's things)
  }
  return true;
}

void discard() {
  E.rooms.erase(E.room);
  E.dirty[E.room] = false;
  loadRoom();
  say(portTr("Changes to this room discarded"));
}

void toggleTest() {
  E.test = !E.test;
  E.dragging = E.looking = false;
  portMapEditsHold(E.test ? 0 : 1);
  applyCamera();
  say(E.test ? portTr("Test play: Tab or Back to edit again") : portTr("Editing"));
}

// a new thing of the palette's kind at p
void placeAt(const float p[3]) {
  PortMapThing t;
  portMapThingInit(&t, E.palette);
  std::copy(p, p + 3, t.pos);
  switch (E.palette) {
    case PORT_MT_SAVE_BLOCK:
    case PORT_MT_HEART_BLOCK:
    case PORT_MT_BLOCK:
    case PORT_MT_BADGE_BLOCK:
    case PORT_MT_BRICK:
    case PORT_MT_HIDDEN_BLOCK: t.pos[1] += 60.f; break; // (blocks float, as the game's)
    default: break;
  }
  if (E.palette == PORT_MT_HEART_BLOCK) {
    t.arg = 10;
  } else if (E.palette == PORT_MT_JUMP_STAND) {
    t.arg = 130;
  } else if (E.palette == PORT_MT_BLOCK || E.palette == PORT_MT_ITEM) {
    long long item = 0;
    port_evt::constantValue("ITEM_MUSHROOM", item);
    t.arg = int(item);
  } else if (E.palette == PORT_MT_BADGE_BLOCK) {
    long long item = 0;
    port_evt::constantValue("ITEM_POWER_JUMP", item);
    t.arg = int(item);
  } else if (E.palette == PORT_MT_NPC) {
    const char* m = portMapEditsModel(E.model);
    std::snprintf(t.model, sizeof(t.model), "%s", m != nullptr ? m : "c_kinopio");
    std::snprintf(t.say, sizeof(t.say), "%s", "Hello!");
  } else if (E.palette == PORT_MT_SIGN) {
    std::snprintf(t.say, sizeof(t.say), "%s", "A sign.");
  } else if (E.palette == PORT_MT_ZONE) {
    t.radius = 40.f;
  }
  if (E.palette == PORT_MT_PIPE || E.palette == PORT_MT_ZONE) {
    // (somewhere to go: the room's own first entrance, so it works at once)
    std::snprintf(t.to, sizeof(t.to), "%s", E.room.c_str());
    if (const char* e = portMapEditsEntrance(0)) {
      std::snprintf(t.entrance, sizeof(t.entrance), "%s", e);
    }
  }
  remember();
  things().push_back(t);
  E.sel = int(things().size()) - 1;
  changed();
}

// a room change of the palette's kind (no place in the room)
void addChange() {
  PortMapThing t;
  portMapThingInit(&t, E.palette);
  if (E.palette == PORT_MT_EXIT) {
    if (const char* e = portMapEditsEntrance(0)) {
      std::snprintf(t.target, sizeof(t.target), "%s", e);
    }
    std::snprintf(t.to, sizeof(t.to), "%s", E.room.c_str());
  }
  remember();
  things().push_back(t);
  E.sel = int(things().size()) - 1;
  changed();
}

// the picture's point (window pixels) as the camera's 0-1, and back
bool toPicture(ImVec2 m, float& sx, float& sy) {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  float r[4];
  portViewEditorPicture(size.x, size.y, r);
  if (r[2] <= 0.f || r[3] <= 0.f) {
    return false;
  }
  sx = (m.x - r[0]) / r[2];
  sy = (m.y - r[1]) / r[3];
  return sx >= 0.f && sx <= 1.f && sy >= 0.f && sy <= 1.f;
}

bool toWindow(const float p[3], ImVec2& out) {
  float sx, sy, r[4];
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (!portViewEditorProject(p, &sx, &sy)) {
    return false;
  }
  portViewEditorPicture(size.x, size.y, r);
  out = ImVec2(r[0] + sx * r[2], r[1] + sy * r[3]);
  return true;
}

// where the picture's point hits the room's collision
bool hitAt(float sx, float sy, float out[3]) {
  float o[3], d[3];
  return portViewEditorRay(sx, sy, o, d) && portMapEditsCast(o, d, 5000.f, out);
}

// the thing whose marker is under the window point, -1 none
int markerAt(ImVec2 m) {
  int best = -1;
  float bestD = 18.f * 18.f;
  for (int i = 0; i < int(things().size()); i++) {
    ImVec2 w;
    if (!portMapThingPlaced(things()[i].type) || !toWindow(things()[i].pos, w)) {
      continue;
    }
    const float d = (w.x - m.x) * (w.x - m.x) + (w.y - m.y) * (w.y - m.y);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

void deleteSelected() {
  if (E.sel < 0 || E.sel >= int(things().size())) {
    return;
  }
  remember();
  things().erase(things().begin() + E.sel);
  E.sel = std::min(E.sel, int(things().size()) - 1);
  changed();
}

void undo() {
  if (E.undo.empty()) {
    return;
  }
  things() = E.undo.back();
  E.undo.pop_back();
  E.dirty[E.room] = true;
  E.sel = std::min(E.sel, int(things().size()) - 1);
  changed();
}

void nudge(float dx, float dy, float dz, float dry) {
  if (E.sel < 0 || E.sel >= int(things().size())) {
    return;
  }
  remember();
  PortMapThing& t = things()[E.sel];
  // (x and z along the camera's look, as the screen shows them)
  const float c = std::cos(E.yaw), s = std::sin(E.yaw);
  t.pos[0] += dx * c - dz * s;
  t.pos[2] += dx * s + dz * c;
  t.pos[1] += dy;
  t.ry = std::fmod(t.ry + dry + 360.f, 360.f);
  changed();
}

void snapSelected() {
  if (E.sel < 0 || E.sel >= int(things().size())) {
    return;
  }
  remember();
  PortMapThing& t = things()[E.sel];
  const bool block = t.type >= PORT_MT_SAVE_BLOCK && t.type <= PORT_MT_HIDDEN_BLOCK && t.type != PORT_MT_JUMP_STAND;
  // (looked for from below its own top: a block from under its bottom, the rest from just above their
  // feet; from higher up the thing's own collision was the floor found, and it rose with each press)
  float p[3] = {t.pos[0], t.pos[1] - (block ? 60.f : 35.f), t.pos[2]};
  if (portMapEditsFloor(p)) {
    t.pos[1] = p[1] + (block ? 60.f : 0.f);
  }
  changed();
}

SDL_Gamepad* pad() { return SDL_GetGamepadFromPlayerIndex(0); }

float stick(SDL_Gamepad* p, SDL_GamepadAxis a) {
  if (p == nullptr) {
    return 0.f;
  }
  const float v = float(SDL_GetGamepadAxis(p, a)) / 32767.f;
  return std::fabs(v) < 0.18f ? 0.f : v;
}

// --- the panel ---

// a room of portAreaRooms by name, -1 none
int roomIndex(const char* map) {
  for (int i = 0; i < portAreaRoomCount; i++) {
    if (std::strcmp(portAreaRooms[i].map, map) == 0) {
      return i;
    }
  }
  return -1;
}

// a link's room and entrance: pickers from the area maps' rooms and their exits
void linkFields(PortMapThing& t, bool& edit) {
  ImGui::SetNextItemWidth(160.f);
  if (ImGui::BeginCombo(portTrLabel("Room##to"), t.to[0] ? t.to : "-")) {
    for (int i = 0; i < portAreaRoomCount; i++) {
      const char* m = portAreaRooms[i].map;
      if (ImGui::Selectable(m, std::strcmp(m, t.to) == 0)) {
        std::snprintf(t.to, sizeof(t.to), "%s", m);
        t.entrance[0] = '\0';
        edit = true;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.f);
  const int room = roomIndex(t.to);
  if (ImGui::BeginCombo(portTrLabel("Entrance##to"), t.entrance[0] ? t.entrance : "-")) {
    for (int i = 0; i < portAreaExitCount; i++) {
      if (portAreaExits[i].room == room && room >= 0 &&
          ImGui::Selectable(portAreaExits[i].hit, std::strcmp(portAreaExits[i].hit, t.entrance) == 0)) {
        std::snprintf(t.entrance, sizeof(t.entrance), "%s", portAreaExits[i].hit);
        edit = true;
      }
    }
    // (this room's own, for a link inside it)
    if (std::strcmp(t.to, E.room.c_str()) == 0) {
      for (int i = 0; const char* e = portMapEditsEntrance(i); i++) {
        if (ImGui::Selectable(e, std::strcmp(e, t.entrance) == 0)) {
          std::snprintf(t.entrance, sizeof(t.entrance), "%s", e);
          edit = true;
        }
      }
    }
    ImGui::EndCombo();
  }
}

// an item by its header name or number
bool itemField(const char* label, int& item) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%d", item);
  ImGui::SetNextItemWidth(200.f);
  if (ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
    long long v = 0;
    if (port_evt::constantValue(buf, v) || (v = std::atoll(buf)) > 0) {
      item = int(v);
      return true;
    }
    say(portTr("No such item (a number, or a name such as ITEM_MUSHROOM)"));
  }
  return false;
}

void thingPanel(PortMapThing& t) {
  bool edit = false;
  ImGui::SeparatorText(portMapThingTypeName(t.type));
  if (portMapThingPlaced(t.type)) {
    ImGui::SetNextItemWidth(260.f);
    if (ImGui::DragFloat3(portTrLabel("Place##pos"), t.pos, 1.f, -20000.f, 20000.f, "%.0f")) {
      edit = true;
    }
    if (ImGui::IsItemActivated()) {
      remember();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("On the floor"))) {
      snapSelected();
    }
  }
  if (t.type == PORT_MT_NPC) {
    ImGui::SetNextItemWidth(260.f);
    edit |= ImGui::SliderFloat(portTrLabel("Facing"), &t.ry, 0.f, 359.f, "%.0f");
    ImGui::SetNextItemWidth(260.f);
    if (ImGui::BeginCombo(portTrLabel("Model"), t.model)) {
      for (int i = 0; const char* m = portMapEditsModel(i); i++) {
        if (ImGui::Selectable(m, std::strcmp(m, t.model) == 0)) {
          std::snprintf(t.model, sizeof(t.model), "%s", m);
          edit = true;
        }
      }
      ImGui::EndCombo();
    }
  }
  if (t.type == PORT_MT_PIPE || t.type == PORT_MT_ZONE || t.type == PORT_MT_JUMP_STAND || t.type == PORT_MT_EXIT) {
    ImGui::TextUnformatted(portTr(t.type == PORT_MT_JUMP_STAND ? "Throws Mario to (or none):" : "Leads to:"));
    linkFields(t, edit);
  }
  if (t.type == PORT_MT_HEART_BLOCK) {
    ImGui::SetNextItemWidth(200.f);
    edit |= ImGui::InputInt(portTrLabel("Price"), &t.arg);
    t.arg = std::clamp(t.arg, 0, 999);
  }
  if (t.type == PORT_MT_JUMP_STAND) {
    ImGui::SetNextItemWidth(200.f);
    edit |= ImGui::SliderInt(portTrLabel("Power"), &t.arg, 20, 600);
  }
  if (t.type == PORT_MT_ZONE) {
    ImGui::SetNextItemWidth(200.f);
    edit |= ImGui::SliderFloat(portTrLabel("Size"), &t.radius, 10.f, 300.f, "%.0f");
  }
  if (t.type >= PORT_MT_BLOCK && t.type <= PORT_MT_HIDDEN_BLOCK && t.type != PORT_MT_SWITCH && t.type != PORT_MT_SIGN) {
    edit |= itemField(portTrLabel("Item##thing"), t.arg);
  }
  if (t.type == PORT_MT_ITEM) {
    edit |= itemField(portTrLabel("Item##thing"), t.arg);
  }
  if (t.type == PORT_MT_SIGN || t.type == PORT_MT_NPC) {
    ImGui::SetNextItemWidth(320.f);
    edit |= ImGui::InputTextMultiline(portTrLabel("Words"), t.say, sizeof(t.say), ImVec2(320.f, 60.f));
    ImGui::TextDisabled("%s", portTr("The game's tags work: <p> a new box, <k> wait for A."));
  }
  if (t.type == PORT_MT_NPC) {
    char shop[128] = "";
    size_t n = 0;
    for (int k = 0; k < t.shopCount; k++) {
      n += std::snprintf(shop + n, sizeof(shop) - n, k ? ",%d" : "%d", t.shop[k]);
    }
    ImGui::SetNextItemWidth(320.f);
    if (ImGui::InputText(portTrLabel("Shop's items"), shop, sizeof(shop), ImGuiInputTextFlags_EnterReturnsTrue)) {
      t.shopCount = 0;
      for (char* p = std::strtok(shop, ", "); p != nullptr && t.shopCount < PORT_MT_SHOP_MAX; p = std::strtok(nullptr, ", ")) {
        long long v = 0;
        if (port_evt::constantValue(p, v) || (v = std::atoll(p)) > 0) {
          t.shop[t.shopCount++] = int(v);
        }
      }
      edit = true;
    }
  }
  if (t.type == PORT_MT_SWITCH) {
    ImGui::SetNextItemWidth(200.f);
    edit |= ImGui::InputText(portTrLabel("Shows (an id)"), t.shows[0], sizeof(t.shows[0]));
  }
  if (t.type >= PORT_MT_HIDE_COLLISION) {
    ImGui::SetNextItemWidth(260.f);
    if (t.type == PORT_MT_EXIT) {
      if (ImGui::BeginCombo(portTrLabel("This room's entrance"), t.target)) {
        for (int i = 0; const char* e = portMapEditsEntrance(i); i++) {
          if (ImGui::Selectable(e, std::strcmp(e, t.target) == 0)) {
            std::snprintf(t.target, sizeof(t.target), "%s", e);
            edit = true;
          }
        }
        ImGui::EndCombo();
      }
    } else {
      edit |= ImGui::InputText(portTrLabel("The room's own (by name)"), t.target, sizeof(t.target));
    }
  }
  ImGui::SetNextItemWidth(200.f);
  edit |= ImGui::InputText(portTrLabel("Id (for a switch)"), t.id, sizeof(t.id));
  {
    bool hidden = t.hidden != 0;
    if (ImGui::Checkbox(portTrLabel("Hidden until shown##hidden"), &hidden)) {
      t.hidden = hidden;
      edit = true;
    }
  }
  ImGui::SetNextItemWidth(200.f);
  edit |= ImGui::InputInt(portTrLabel("From the story's step"), &t.story);
  t.story = std::max(0, t.story);
  if (edit) {
    E.dirty[E.room] = true;
    changed();
  }
}

void panel() {
  ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(portTrLabel("Map editor##window"), nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGui::Text("%s: %s", portTr("Room"), E.room.c_str());
  ImGui::SetNextItemWidth(200.f);
  // typed into a copy: E.mod changes only on Enter (a save meanwhile goes to the mod the edits are of)
  static char sTyped[sizeof(E.mod)];
  static bool sTyping;
  if (!sTyping) {
    std::snprintf(sTyped, sizeof(sTyped), "%s", E.mod);
  }
  if (ImGui::InputText(portTrLabel("Mod"), sTyped, sizeof(sTyped), ImGuiInputTextFlags_EnterReturnsTrue)) {
    switchMod(sTyped);
  }
  sTyping = ImGui::IsItemActive();
  ImGui::SetNextItemWidth(200.f);
  if (ImGui::BeginCombo(portTrLabel("Place"), portMapThingTypeName(E.palette))) {
    for (int i = 0; i < PORT_MT_COUNT; i++) {
      if (ImGui::Selectable(portMapThingTypeName(i), i == E.palette)) {
        E.palette = i;
      }
    }
    ImGui::EndCombo();
  }
  if (E.palette == PORT_MT_NPC) {
    ImGui::SetNextItemWidth(200.f);
    const char* m = portMapEditsModel(E.model);
    if (ImGui::BeginCombo(portTrLabel("Model##palette"), m != nullptr ? m : "-")) {
      for (int i = 0; const char* name = portMapEditsModel(i); i++) {
        if (ImGui::Selectable(name, i == E.model)) {
          E.model = i;
        }
      }
      ImGui::EndCombo();
    }
  }
  if (portMapThingPlaced(E.palette)) {
    ImGui::TextDisabled("%s", portTr("Click the room to place it there."));
    if (ImGui::Button(portTrLabel("At Mario's feet"))) {
      float p[3];
      if (portMapEditsMario(p)) {
        placeAt(p);
      }
    }
  } else if (ImGui::Button(portTrLabel("Add the change"))) {
    addChange();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(E.undo.empty());
  if (ImGui::Button(portTrLabel("Undo"))) {
    undo();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel(E.test ? "Edit##test" : "Test play##test"))) {
    toggleTest();
  }
  if (ImGui::Button(portTrLabel("Save"))) {
    save();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Discard this room's changes"))) {
    discard();
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Close"))) {
    stop();
    ImGui::End();
    return;
  }
  if (portMapEditsLastError()[0] != '\0') {
    ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f), "%s", portMapEditsLastError());
  }
  ImGui::SeparatorText(portTr("In this room"));
  if (ImGui::BeginListBox("##things", ImVec2(-1.f, 130.f))) {
    for (int i = 0; i < int(things().size()); i++) {
      const PortMapThing& t = things()[i];
      char line[96];
      std::snprintf(line, sizeof(line), "%d  %s%s%s##%d", i + 1, portMapThingTypeName(t.type), t.id[0] ? "  " : "",
                    t.id, i);
      if (ImGui::Selectable(line, i == E.sel)) {
        E.sel = i;
      }
    }
    ImGui::EndListBox();
  }
  if (E.sel >= 0 && E.sel < int(things().size())) {
    thingPanel(things()[E.sel]);
    if (ImGui::Button(portTrLabel("Take it away"))) {
      deleteSelected();
    }
  }
  ImGui::End();
}

void testStep();

// each presented frame
void tick(int pageOpen) {
  E.pageOpen = pageOpen != 0;
  testStep();
  if (!E.on && portMapEditorTakeRequest()) { // (the Start menu's row, the menu now closed)
    start();
  }
  if (!E.on) {
    return;
  }
  if (!available()) {
    stop();
    return;
  }
  if (E.room != portMapEditsRoomName()) { // (a pipe or door in test play: the next room's)
    E.room = portMapEditsRoomName();
    loadRoom();
  }
  const Uint64 now = SDL_GetTicksNS();
  const float dt = std::min(float(now - E.last) / 1e9f, 0.1f);
  E.last = now;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (!E.said.empty() && SDL_GetTicks() - E.saidAt < 2500) {
    const ImVec2 ts = ImGui::CalcTextSize(E.said.c_str());
    const ImVec2 at(size.x * 0.5f - ts.x * 0.5f, size.y - 40.f);
    dl->AddRectFilled(ImVec2(at.x - 10, at.y - 6), ImVec2(at.x + ts.x + 10, at.y + ts.y + 6), IM_COL32(20, 16, 30, 200), 8.f);
    dl->AddText(at, IM_COL32(255, 247, 228, 255), E.said.c_str());
  }
  if (E.test || pageOpen) {
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  // flying: the keys (unless a text field has them) and the sticks
  SDL_Gamepad* p = pad();
  float mx = stick(p, SDL_GAMEPAD_AXIS_LEFTX), mz = -stick(p, SDL_GAMEPAD_AXIS_LEFTY);
  float my = (float(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) - float(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER))) / 32767.f;
  if (p == nullptr) {
    my = 0.f;
  }
  float fast = 1.f;
  if (!io.WantCaptureKeyboard) {
    const bool* k = SDL_GetKeyboardState(nullptr);
    mx += float(k[SDL_SCANCODE_D]) - float(k[SDL_SCANCODE_A]);
    mz += float(k[SDL_SCANCODE_W]) - float(k[SDL_SCANCODE_S]);
    my += float(k[SDL_SCANCODE_E]) - float(k[SDL_SCANCODE_Q]);
    fast = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] ? 3.f : 1.f;
  }
  E.yaw += stick(p, SDL_GAMEPAD_AXIS_RIGHTX) * 1.8f * dt;
  E.pitch = std::clamp(E.pitch - stick(p, SDL_GAMEPAD_AXIS_RIGHTY) * 1.2f * dt, -1.5f, 1.5f);
  {
    const float speed = 350.f * fast * dt;
    float f[3];
    forward(f);
    const float rx = std::cos(E.yaw), rz = std::sin(E.yaw);
    E.eye[0] += (f[0] * mz + rx * mx) * speed;
    E.eye[1] += (f[1] * mz + my) * speed;
    E.eye[2] += (f[2] * mz + rz * mx) * speed;
  }
  // the mouse over the room (not over the panel)
  if (!io.WantCaptureMouse) {
    const ImVec2 m = io.MousePos;
    float sx, sy;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && toPicture(m, sx, sy)) {
      const int hit = markerAt(m);
      float at[3];
      if (hit >= 0) {
        E.sel = hit;
        E.dragging = true;
        remember();
      } else if (portMapThingPlaced(E.palette) && hitAt(sx, sy, at)) {
        placeAt(at);
      } else if (!portMapThingPlaced(E.palette)) {
        say(portTr("That kind changes the room's own: Add the change in the panel"));
      }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      E.looking = true;
    }
  }
  if (E.dragging) {
    float sx, sy, at[3];
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      E.dragging = false;
    } else if (E.sel >= 0 && E.sel < int(things().size()) && toPicture(io.MousePos, sx, sy) && hitAt(sx, sy, at) &&
               (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f)) {
      PortMapThing& t = things()[E.sel];
      const bool block = t.type >= PORT_MT_SAVE_BLOCK && t.type <= PORT_MT_HIDDEN_BLOCK && t.type != PORT_MT_JUMP_STAND;
      t.pos[0] = at[0];
      t.pos[1] = at[1] + (block ? 60.f : 0.f);
      t.pos[2] = at[2];
      changed();
    }
  }
  if (E.looking) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      E.looking = false;
    } else {
      E.yaw += io.MouseDelta.x * 0.005f;
      E.pitch = std::clamp(E.pitch - io.MouseDelta.y * 0.005f, -1.5f, 1.5f);
    }
  }
  applyCamera();
  // the things' markers
  for (int i = 0; i < int(things().size()); i++) {
    const PortMapThing& t = things()[i];
    ImVec2 w;
    if (!portMapThingPlaced(t.type) || !toWindow(t.pos, w)) {
      continue;
    }
    const ImU32 c = i == E.sel ? IM_COL32(255, 210, 60, 255) : IM_COL32(120, 200, 255, 220);
    dl->AddCircle(w, 9.f, c, 0, 2.5f);
    if (t.type == PORT_MT_ZONE) {
      float edge[3] = {t.pos[0] + (t.radius > 0.f ? t.radius : 40.f), t.pos[1], t.pos[2]};
      ImVec2 e;
      if (toWindow(edge, e)) {
        dl->AddCircle(w, std::hypot(e.x - w.x, e.y - w.y), IM_COL32(120, 255, 160, 160), 0, 1.5f);
      }
    }
    dl->AddText(ImVec2(w.x + 12.f, w.y - 8.f), c, portMapThingTypeName(t.type));
  }
  // the picture's middle: where the controller's A places
  dl->AddCircle(ImVec2(size.x * 0.5f, size.y * 0.5f), 4.f, IM_COL32(255, 255, 255, 160));
  panel();
}

// (asked before Paper Cut's own keys and buttons, portSettingsAddEventFirst: Tab,
// Backspace and a controller's Back are the editor's while it is open, not
// fast-forward, rewind and the settings page)
int event(const void* ev) {
  const SDL_Event* e = static_cast<const SDL_Event*>(ev);
  if (!E.on || E.pageOpen) {
    return 0;
  }
  if (e->type == SDL_EVENT_KEY_DOWN) {
    const SDL_Scancode k = e->key.scancode;
    const bool ctrl = (e->key.mod & SDL_KMOD_CTRL) != 0;
    if (k == SDL_SCANCODE_TAB && !e->key.repeat && !ImGui::GetIO().WantTextInput) { // (not while typing a name)
      toggleTest();
      return 1;
    }
    if (E.test || ImGui::GetIO().WantCaptureKeyboard) {
      return 0;
    }
    if (k == SDL_SCANCODE_ESCAPE) {
      stop();
    } else if (k == SDL_SCANCODE_DELETE || k == SDL_SCANCODE_BACKSPACE) {
      deleteSelected();
    } else if (ctrl && k == SDL_SCANCODE_Z) {
      undo();
    } else if (ctrl && k == SDL_SCANCODE_S) {
      save();
    } else if (k == SDL_SCANCODE_F) {
      snapSelected();
    } else if (k == SDL_SCANCODE_R) {
      nudge(0, 0, 0, 45.f);
    } else if (k == SDL_SCANCODE_LEFT || k == SDL_SCANCODE_RIGHT || k == SDL_SCANCODE_UP || k == SDL_SCANCODE_DOWN) {
      const float step = (e->key.mod & SDL_KMOD_SHIFT) ? 1.f : 5.f;
      nudge(k == SDL_SCANCODE_LEFT ? -step : k == SDL_SCANCODE_RIGHT ? step : 0.f, 0.f,
            k == SDL_SCANCODE_UP ? -step : k == SDL_SCANCODE_DOWN ? step : 0.f, 0.f);
    } else if (k == SDL_SCANCODE_PAGEUP || k == SDL_SCANCODE_PAGEDOWN) {
      nudge(0.f, k == SDL_SCANCODE_PAGEUP ? 5.f : -5.f, 0.f, 0.f);
    } else {
      return 0;
    }
    return 1;
  }
  if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    const int b = e->gbutton.button;
    if (b == SDL_GAMEPAD_BUTTON_BACK) {
      toggleTest();
      return 1;
    }
    if (E.test) {
      return 0;
    }
    const int role = portMenuEventRole(e); // the controller's own back and confirm (menu_buttons.h)
    if (role == PORT_MENU_BACK) {
      stop();
    } else if (role == PORT_MENU_CONFIRM) {
      float at[3];
      if (portMapThingPlaced(E.palette) && hitAt(0.5f, 0.5f, at)) {
        placeAt(at);
      } else if (!portMapThingPlaced(E.palette)) {
        addChange();
      }
    } else if (role == PORT_MENU_THIRD) { // X, wherever this controller has it (a GameCube pad's is EAST)
      deleteSelected();
    } else if (b == SDL_GAMEPAD_BUTTON_NORTH) {
      E.palette = (E.palette + 1) % PORT_MT_COUNT;
      say(portMapThingTypeName(E.palette));
    } else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
      const int n = int(things().size());
      if (n > 0) {
        E.sel = ((E.sel < 0 ? 0 : E.sel) + (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER ? 1 : n - 1)) % n;
      }
    } else if (b >= SDL_GAMEPAD_BUTTON_DPAD_UP && b <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
      nudge(b == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? -5.f : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 5.f : 0.f, 0.f,
            b == SDL_GAMEPAD_BUTTON_DPAD_UP ? -5.f : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? 5.f : 0.f, 0.f);
    } else {
      return 0;
    }
    return 1;
  }
  return 0;
}

void section(int inGame) {
  const bool can = inGame && available();
  ImGui::BeginDisabled(!can);
  if (ImGui::Button(portTrLabel(E.on ? "Close the map editor" : "Open the map editor"))) {
    E.on ? stop() : start();
  }
  ImGui::EndDisabled();
  if (!can) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("(in the field, not online or in a replay)"));
  }
  // (the editor's own panel changes it while the editor is open; here it takes effect on Enter,
  // after the edits so far are saved to the mod they were made for)
  static char sTyped[sizeof(E.mod)];
  static bool sTyping;
  if (!sTyping) {
    std::snprintf(sTyped, sizeof(sTyped), "%s", E.mod);
  }
  ImGui::BeginDisabled(E.on);
  ImGui::SetNextItemWidth(200.f);
  if (ImGui::InputText(portTrLabel("Mod to save to"), sTyped, sizeof(sTyped), ImGuiInputTextFlags_EnterReturnsTrue)) {
    switchMod(sTyped);
  }
  sTyping = ImGui::IsItemActive();
  ImGui::EndDisabled();
  if (!E.said.empty() && !E.on && SDL_GetTicks() - E.saidAt < 6000) {
    ImGui::TextDisabled("%s", E.said.c_str());
  }
}

const bool sAdded = [] {
  portSettingsAddSection("Modes", "Map editor",
                         "Place pipes, blocks, switches, signs, items, NPCs and loading zones in the room Mario is in, "
                         "from the game's own models; move them, test them, and save them as a mod in your mods "
                         "folder (maps/<room>.json), which the mod manager then lists. Mouse: click to place or pick, "
                         "drag to move, right-drag to look; WASD and Q / E fly; Tab test play; Esc closes and saves.",
                         0, section, "map editor level maker room place pipe block npc door mod create build");
  portSettingsAddOverlay(tick, nullptr);
  portSettingsAddEventFirst(event);
  return true;
}();

// $TTYD_MAPEDIT_TEST (tests/port/testpass/modes_mapedit_editor.json): once the field is up,
// the editor opened as the Start menu's row opens it and driven by SDL events pushed
// as a player's would come (a click on the room, a controller's buttons, keys), each
// step's result logged as "[mapedit test] ..."; the input script presses Start meanwhile
int sTestFrame = -1;

void testLog(const char* what) {
  const auto& t = things();
  const PortMapThing* s = E.sel >= 0 && E.sel < int(t.size()) ? &t[size_t(E.sel)] : nullptr;
  std::printf("[mapedit test] %s: on %d, test %d, room %s, things %d, palette %s", what, E.on ? 1 : 0, E.test ? 1 : 0,
              E.room.c_str(), int(t.size()), portMapThingTypeName(E.palette));
  if (s != nullptr) {
    std::printf(", picked %s at %.0f %.0f %.0f turned %.0f", portMapThingTypeName(s->type), s->pos[0], s->pos[1],
                s->pos[2], s->ry);
  }
  std::printf("\n");
  std::fflush(stdout);
}

SDL_WindowID testWindow() {
  int n = 0;
  SDL_Window** list = SDL_GetWindows(&n);
  const SDL_WindowID id = list != nullptr && n > 0 ? SDL_GetWindowID(list[0]) : 0;
  SDL_free(list);
  return id;
}

void testKey(SDL_Scancode k, SDL_Keymod mod = SDL_KMOD_NONE) {
  for (const Uint32 type : {Uint32(SDL_EVENT_KEY_DOWN), Uint32(SDL_EVENT_KEY_UP)}) {
    SDL_Event e{};
    e.type = type;
    e.key.windowID = testWindow();
    e.key.scancode = k;
    e.key.key = SDL_GetKeyFromScancode(k, mod, false);
    e.key.mod = mod;
    e.key.down = type == SDL_EVENT_KEY_DOWN;
    SDL_PushEvent(&e);
  }
}

void testButton(SDL_GamepadButton b) {
  for (const Uint32 type : {Uint32(SDL_EVENT_GAMEPAD_BUTTON_DOWN), Uint32(SDL_EVENT_GAMEPAD_BUTTON_UP)}) {
    SDL_Event e{};
    e.type = type;
    e.gbutton.button = Uint8(b);
    e.gbutton.down = type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    SDL_PushEvent(&e);
  }
}

// a left click at the window's point (x, y as parts of its size)
void testClick(float fx, float fy, Uint32 type) {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  SDL_Event e{};
  e.type = type;
  if (type == SDL_EVENT_MOUSE_MOTION) {
    e.motion.windowID = testWindow();
    e.motion.x = size.x * fx;
    e.motion.y = size.y * fy;
  } else {
    e.button.windowID = testWindow();
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.clicks = 1;
    e.button.x = size.x * fx;
    e.button.y = size.y * fy;
  }
  SDL_PushEvent(&e);
}

void testStep() {
  static const bool on = SDL_getenv("TTYD_MAPEDIT_TEST") != nullptr;
  if (!on) {
    return;
  }
  if (sTestFrame < 0) {
    if (!available() || portMapEditsRoomName()[0] == '\0') {
      return;
    }
    sTestFrame = 0;
  }
  const int f = ++sTestFrame;
  switch (f) {
  case 60: portMapEditorRequest(); break; // (the Start menu's row)
  case 100: testLog("opened"); break;
  case 110: testClick(0.82f, 0.62f, SDL_EVENT_MOUSE_MOTION); break;
  case 112: testClick(0.82f, 0.62f, SDL_EVENT_MOUSE_BUTTON_DOWN); break;
  case 114: testClick(0.82f, 0.62f, SDL_EVENT_MOUSE_BUTTON_UP); break;
  case 120: testLog("clicked the room (a pipe placed where the ray hit)"); break;
  case 130: testButton(SDL_GAMEPAD_BUTTON_NORTH); break; // the next kind: Save Block
  case 135: testButton(SDL_GAMEPAD_BUTTON_SOUTH); break; // placed at the picture's middle
  case 140: testLog("Y then A (a Save Block at the middle)"); break;
  case 150: testKey(SDL_SCANCODE_PAGEUP); break;
  case 152: testKey(SDL_SCANCODE_PAGEUP); break;
  case 155: testLog("Page Up twice (up 10)"); break;
  case 160: testKey(SDL_SCANCODE_F); break;
  case 165: testLog("F (down on the floor, a block 60 above it)"); break;
  case 170: testKey(SDL_SCANCODE_R); break;
  case 175: testLog("R (turned 45)"); break;
  case 180: testKey(SDL_SCANCODE_RIGHT); break;
  case 185: testLog("Right (nudged 5)"); break;
  case 190: testButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER); break;
  case 195: testLog("LB (the previous thing picked)"); break;
  case 200: testKey(SDL_SCANCODE_DELETE); break;
  case 205: testLog("Delete (taken away)"); break;
  case 210: testKey(SDL_SCANCODE_Z, SDL_KMOD_LCTRL); break;
  case 215: testLog("Ctrl+Z (back)"); break;
  case 220: testKey(SDL_SCANCODE_BACKSPACE); break;
  case 225: testLog("Backspace (taken away; not rewind)"); break;
  case 230: testKey(SDL_SCANCODE_Z, SDL_KMOD_LCTRL); break;
  case 235: testLog("Ctrl+Z (back again)"); break;
  case 240: testKey(SDL_SCANCODE_TAB); break;
  case 250: testLog("Tab (test play; not fast-forward)"); break;
  case 300: testKey(SDL_SCANCODE_TAB); break;
  case 310: testLog("Tab (editing again)"); break;
  case 320: testButton(SDL_GAMEPAD_BUTTON_BACK); break;
  case 330: testLog("Back (test play; not the settings page)"); break;
  case 340: testButton(SDL_GAMEPAD_BUTTON_BACK); break;
  case 350: testLog("Back (editing again)"); break;
  case 360: testKey(SDL_SCANCODE_S, SDL_KMOD_LCTRL); break;
  case 370: {
    const std::string path = std::string(portModsDir()) + "/" + E.mod + "/maps/" + E.room + ".json";
    const std::string text = portfile::readFile(path);
    std::printf("[mapedit test] Ctrl+S: %s, %d bytes, the store has %d of its things\n", path.c_str(),
                int(text.size()), portMapEditsCount(modId().c_str(), E.room.c_str()));
    std::fflush(stdout);
    break;
  }
  case 600: testKey(SDL_SCANCODE_ESCAPE); break;
  case 610: testLog("Esc (closed)"); break;
  default: break;
  }
}

} // namespace
