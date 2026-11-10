// The Mods tab's "Export characters" (docs/CHARACTERS.md): a character's
// model file (a/<name>: its shapes, group tree and animations) and its
// textures (a/<texture>-), as the running game reads them, written to
// exports/<name>/ in the user folder with a manifest.json, in the layout
// tools/port/character_tool.py reads: it adds the editable form (PNGs, JSON,
// a Blender scene) and builds the edited folder into a mod. Registered by
// mods_manager.cpp, after its own sections. Only while a game runs (the disc
// is open then).
#include "port/mods.h"
#include "port/offscreen.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h"
#include <SDL3/SDL.h>
#include <dolphin/dvd.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using portui::hint;

namespace {

constexpr uint32_t kHeaderSize = 0x1B0;   // AnimationPoseData on the disc
constexpr uint32_t kTextureName = 0x44;   // its texture file's name (64 bytes)
constexpr uint32_t kAnimCount = 0x148;    // counts from 0xE8: the 25th, the animations
constexpr uint32_t kAnimTable = 0x1AC;    // offsets from 0x14C: the 25th, the animation table
constexpr uint32_t kAnimRecord = 0x40;    // name[60], offset
constexpr size_t kExportAllLimit = 200;
// the Play as characters' models (port/src/common/luigi_model_pc.c)
constexpr const char* kHostModels[] = {"c_luigi_pc", "c_peach_pc", "c_peach_r_pc", "c_koopa_pc", "c_koopa_r_pc"};

std::vector<std::string> sModels; // the disc's a/ models, sorted
bool sScanned;
char sFilter[64];
std::string sSelected;
std::vector<std::string> sStatus;

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
  return at + 4 <= b.size() ? uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3]
                            : 0;
}

std::string lower(std::string s) {
  for (auto& c : s) {
    c = char(std::tolower(uint8_t(c)));
  }
  return s;
}

// a/'s model files: not the textures (name-) and not the tables (.bin)
void scan() {
  sScanned = true;
  sModels.clear();
  DVDDir d;
  DVDDirEntry e;
  if (!DVDOpenDir("/a", &d)) {
    return;
  }
  while (DVDReadDir(&d, &e)) {
    if (e.isDir || e.name == nullptr) {
      continue;
    }
    const std::string name = e.name;
    if (name.empty() || name.back() == '-' || name.find('.') != std::string::npos) {
      continue;
    }
    sModels.push_back(name);
  }
  // models the port adds as files of its own (a directory listing has only the disc's)
  for (const char* extra : kHostModels) {
    DVDFileInfo info;
    if (std::find(sModels.begin(), sModels.end(), extra) == sModels.end() &&
        DVDOpen((std::string("/a/") + extra).c_str(), &info)) {
      DVDClose(&info);
      sModels.push_back(extra);
    }
  }
  std::sort(sModels.begin(), sModels.end());
}

bool readDisc(const std::string& path, std::vector<uint8_t>& out) {
  DVDFileInfo info;
  if (!DVDOpen(path.c_str(), &info)) {
    return false;
  }
  const s32 length = static_cast<s32>(info.length);
  out.assign((size_t(length) + 31) & ~size_t(31), 0);
  const bool ok = DVDReadPrio(&info, out.data(), static_cast<s32>(out.size()), 0, 2) >= length;
  DVDClose(&info);
  if (ok) {
    out.resize(size_t(length));
  }
  return ok;
}

std::string fixedString(const std::vector<uint8_t>& b, size_t at, size_t n) {
  std::string s;
  for (size_t i = at; i < at + n && i < b.size() && b[i] != 0; i++) {
    s += char(b[i]);
  }
  return s;
}

std::string jsonString(const std::string& s) {
  std::string out = "\"";
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (uint8_t(c) < 0x20 || uint8_t(c) >= 0x7F) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", unsigned(uint8_t(c)));
      out += buf;
    } else {
      out += c;
    }
  }
  return out + "\"";
}

// a file or folder name the game's names can't break out of
bool plainName(const std::string& s) {
  if (s.empty() || s == "." || s == "..") {
    return false;
  }
  for (const char c : s) {
    if (!(std::isalnum(uint8_t(c)) || c == '_' || c == '-' || c == '.' || c == '@')) {
      return false;
    }
  }
  return true;
}

uint32_t crc32(const std::vector<uint8_t>& b) {
  uint32_t c = 0xFFFFFFFFu;
  for (const uint8_t x : b) {
    c ^= x;
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
  }
  return ~c;
}

std::string exportsDir() { return std::string(portSettingsUserPath()) + "/exports"; }

// The enabled mod that replaces disc file path ("/a/name"), or "".
std::string replacingMod(const std::string& path) {
  const std::string want = lower(path);
  for (int i = 0; i < portModsCount(); i++) {
    const PortModInfo* m = portModsGet(i);
    if (m == nullptr || !m->enabled || m->error != nullptr) {
      continue;
    }
    for (int j = 0; j < portModsChangeCount(i, PORT_MOD_CHANGE_FILE); j++) {
      const char* c = portModsChange(i, PORT_MOD_CHANGE_FILE, j);
      if (c != nullptr && lower(c) == want) {
        return m->name != nullptr && *m->name ? m->name : (m->id != nullptr ? m->id : "a mod");
      }
    }
  }
  return "";
}

// exports/<name>/: disc/<name>, disc/<texture>-, manifest.json. A line for the status.
std::string exportModel(const std::string& name) {
  if (!plainName(name)) {
    return name + ": not a name a folder can have";
  }
  std::vector<uint8_t> model;
  if (!readDisc("/a/" + name, model)) {
    return name + ": not read";
  }
  if (model.size() < kHeaderSize || lower(fixedString(model, 4, 64)) != lower(name)) {
    return name + ": not a model file (its header names another)";
  }
  const std::string texture = fixedString(model, kTextureName, 64);
  std::vector<uint8_t> tpl;
  const bool haveTexture = plainName(texture + "-") && readDisc("/a/" + texture + "-", tpl);
  // the animations' names (the table: 0x40-byte records, name first)
  std::vector<std::string> anims;
  const uint32_t count = be32(model, kAnimCount), table = be32(model, kAnimTable);
  if (table >= kHeaderSize && count < 100000 && uint64_t(table) + uint64_t(count) * kAnimRecord <= model.size()) {
    for (uint32_t i = 0; i < count; i++) {
      anims.push_back(fixedString(model, table + i * kAnimRecord, 60));
    }
  }
  const std::string dir = exportsDir() + "/" + name;
  SDL_CreateDirectory(exportsDir().c_str());
  SDL_CreateDirectory(dir.c_str());
  SDL_CreateDirectory((dir + "/disc").c_str());
  if (!SDL_SaveFile((dir + "/disc/" + name).c_str(), model.data(), model.size()) ||
      (haveTexture && !SDL_SaveFile((dir + "/disc/" + texture + "-").c_str(), tpl.data(), tpl.size()))) {
    return name + ": not written (" + SDL_GetError() + ")";
  }
  const std::string modded = replacingMod("/a/" + name);
  const std::string moddedTexture = haveTexture ? replacingMod("/a/" + texture + "-") : "";
  char crc[16];
  std::string m = "{\n";
  m += " \"format\": 1,\n";
  m += " \"exported_by\": " + jsonString(std::string("Paper Cut ") + PORT_VERSION) + ",\n";
  m += " \"game\": " + jsonString(PORT_GAME_ID) + ",\n";
  m += " \"model\": " + jsonString(name) + ",\n";
  m += " \"texture\": " + jsonString(texture) + ",\n";
  std::snprintf(crc, sizeof(crc), "%08x", crc32(model));
  m += " \"files\": {\n  \"model\": {\"path\": " + jsonString("disc/" + name) +
       ", \"size\": " + std::to_string(model.size()) + ", \"crc32\": \"" + crc + "\"}";
  if (haveTexture) {
    std::snprintf(crc, sizeof(crc), "%08x", crc32(tpl));
    m += ",\n  \"texture\": {\"path\": " + jsonString("disc/" + texture + "-") +
         ", \"size\": " + std::to_string(tpl.size()) + ", \"crc32\": \"" + crc + "\"}";
  }
  m += "\n },\n \"animations\": [";
  for (size_t i = 0; i < anims.size(); i++) {
    m += (i ? ", " : "") + std::string("{\"name\": ") + jsonString(anims[i]) + "}";
  }
  m += "],\n";
  if (!modded.empty() || !moddedTexture.empty()) {
    m += " \"modded\": " + jsonString(!modded.empty() ? modded : moddedTexture) + ",\n";
  }
  m += " \"help\": \"tools/port/character_tool.py export <this folder> makes it editable; docs/CHARACTERS.md\"\n}\n";
  if (!SDL_SaveFile((dir + "/manifest.json").c_str(), m.data(), m.size())) {
    return name + ": manifest.json not written (" + SDL_GetError() + ")";
  }
  std::string line = name + ": " + std::to_string(anims.size()) + " animations" +
                     (haveTexture ? ", textures " + texture + "-" : ", no texture file") + " -> " + dir;
  if (!modded.empty() || !moddedTexture.empty()) {
    line += " (from the mod " + (!modded.empty() ? modded : moddedTexture) + ", not the disc)";
  }
  return line;
}

bool matches(const std::string& name) {
  return sFilter[0] == '\0' || lower(name).find(lower(sFilter)) != std::string::npos;
}

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define CHARACTERS_OPEN_FOLDER 1
#endif

} // namespace

// The section (mods_manager.cpp registers it).
void portCharacterExportSection(int inGame) {
  if (!inGame) {
    return;
  }
  hint("A character's model and animations (the paper cut-out's model file) and its textures, as the game "
       "reads them, to exports/ in the user folder. tools/port/character_tool.py turns the export into PNGs and "
       "files Blender opens, and the edited files into a mod that replaces the character (docs/CHARACTERS.md).");
  if (!sScanned) {
    scan();
  }
  if (sModels.empty()) {
    hint("No models found on the disc.");
    return;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
  ImGui::InputTextWithHint("##filter", "Filter (mario, c_luigi, kino...)", sFilter, sizeof(sFilter));
  std::vector<const std::string*> shown;
  for (const auto& n : sModels) {
    if (matches(n)) {
      shown.push_back(&n);
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled(portTr("%zu of %zu"), shown.size(), sModels.size());
  if (ImGui::BeginListBox("##models", ImVec2(-1, ImGui::GetTextLineHeightWithSpacing() * 8))) {
    ImGuiListClipper clipper;
    clipper.Begin(int(shown.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const std::string& n = *shown[size_t(i)];
        if (ImGui::Selectable(n.c_str(), n == sSelected)) {
          sSelected = n;
        }
      }
    }
    ImGui::EndListBox();
  }
  // a name typed whole that the list doesn't have (a file a mod adds) is tried as it is
  const bool typed = shown.empty() && plainName(sFilter);
  const std::string pick = typed ? std::string(sFilter) : sSelected;
  ImGui::BeginDisabled(pick.empty());
  if (ImGui::Button(pick.empty() ? portTrLabel("Export##export")
                                 : (std::string(portTr("Export")) + " " + pick + "###export").c_str())) {
    sStatus = {exportModel(pick)};
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(shown.empty() || shown.size() > kExportAllLimit || sFilter[0] == '\0');
  char all[128];
  std::snprintf(all, sizeof(all), "%s###exportshown", portTrf("Export the %zu shown", shown.size()));
  if (ImGui::Button(all)) {
    sStatus.clear();
    for (const auto* n : shown) {
      sStatus.push_back(exportModel(*n));
    }
  }
  ImGui::EndDisabled();
#ifdef CHARACTERS_OPEN_FOLDER
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Open the exports folder"))) {
    SDL_CreateDirectory(exportsDir().c_str());
    if (!portOpenFolder(exportsDir().c_str())) {
      char why[1200];
      sStatus = {portOpenError(why, sizeof(why), exportsDir().c_str(), true)};
    }
  }
#endif
  for (const auto& line : sStatus) {
    ImGui::BulletText("%s", line.c_str());
  }
  hint("Mario is a_mario (field; _r his back), e_mario (story scenes) and b_mario (battle), with variants; "
       "a partner or NPC is usually c_<name>. Filter \"mario\" and export them all for the whole set.");
}
