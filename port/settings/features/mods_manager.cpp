// The "Mods" tab: the mod manager (port/include/port/mods.h): each mod on or
// off and in order, what it changes, where it and another mod change the same
// thing, and its own settings (mod.json "options"); and texture packs: their
// coverage against the textures seen in play, name collisions, dumping and
// reloading while the game runs.
#include "port/mods.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <cstdio>
#include <set>
#include <string>
#include <utility>

namespace {

constexpr const char* kTab = "Mods";

void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void colored(const ImVec4& color, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

const ImVec4 kRed(1.0f, 0.45f, 0.4f, 1.0f);
const ImVec4 kAmber(1.0f, 0.75f, 0.3f, 1.0f);

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define MODS_OPEN_FOLDERS 1
void openFolder(const std::string& dir) {
  SDL_CreateDirectory(dir.c_str());
  const std::string url = "file:///" + dir;
  SDL_OpenURL(url.c_str());
}
#endif

// by kind: singular, plural
constexpr const char* kKinds[PORT_MOD_CHANGE_COUNT][2] = {
    {"file", "files"},           {"texture", "textures"},       {"message", "messages"},
    {"data change", "data changes"}, {"hook", "hooks"},         {"map", "maps"},
};

std::string count(int n, int kind) { return std::to_string(n) + " " + kKinds[kind][n == 1 ? 0 : 1]; }

// "3 files, 120 textures, 1 hook" (only what it has)
std::string summary(int index) {
  std::string text;
  for (int k = 0; k < PORT_MOD_CHANGE_COUNT; k++) {
    if (const int n = portModsChangeCount(index, k); n > 0) {
      text += (text.empty() ? "" : ", ") + count(n, k);
    }
  }
  return text.empty() ? "changes nothing the game loads (code only, or empty)" : text;
}

// Everything it changes, by kind, each list folded.
void changes(int index) {
  if (!ImGui::TreeNode(portTrLabel("What it changes"))) {
    return;
  }
  for (int k = 0; k < PORT_MOD_CHANGE_COUNT; k++) {
    const int n = portModsChangeCount(index, k);
    if (n == 0) {
      continue;
    }
    char label[64];
    std::snprintf(label, sizeof(label), "%s (%d)", kKinds[k][1], n);
    label[0] = char(label[0] - 'a' + 'A');
    if (ImGui::TreeNode(label)) {
      ImGuiListClipper clipper;
      clipper.Begin(n);
      while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
          ImGui::BulletText("%s", portModsChange(index, k, i));
        }
      }
      ImGui::TreePop();
    }
  }
  if (portModsChangeCount(index, PORT_MOD_CHANGE_HOOK) == 0) {
    note("Hooks (code that runs in place of or around the game's functions) show once its code runs.");
  }
  ImGui::TreePop();
}

// Where an enabled mod changes the same thing as another one.
void conflicts(int index) {
  for (int c = 0; c < portModsConflictCount(index); c++) {
    const PortModConflict* conflict = portModsGetConflict(index, c);
    std::string line = "Also changed by " + std::string(conflict->other) + ": " +
                       count(conflict->count, conflict->kind) + " (" + conflict->what + "). ";
    if (conflict->kind == PORT_MOD_CHANGE_HOOK) {
      line += conflict->wins ? "Both run, this one first if it is higher in the list."
                             : "Both replace it: only the higher one's replacement runs.";
    } else {
      line += conflict->wins ? "This mod's are used (it is higher in the list)."
                             : "Those of " + std::string(conflict->other) + " are used (it is higher in the list).";
    }
    colored(conflict->wins ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kAmber, line.c_str());
  }
}

// The mod's own settings (mod.json "options"), read by its code.
// the mods' options that apply from the next start, changed this run (Restart now beside them)
std::set<std::pair<int, int>>& changedNextStart() {
  static std::set<std::pair<int, int>> sChanged;
  return sChanged;
}

void options(int index, const PortModInfo* mod) {
  if (mod->optionCount == 0) {
    return;
  }
  ImGui::Indent();
  ImGui::TextUnformatted(portTr("Its settings"));
  for (int o = 0; o < mod->optionCount; o++) {
    const PortModOption* opt = portModsGetOption(index, o);
    int value = opt->value;
    ImGui::PushID(o);
    bool edited = false;
    if (opt->type == PORT_MOD_OPTION_BOOL) {
      bool v = value != 0;
      edited = ImGui::Checkbox(opt->label, &v);
      value = v ? 1 : 0;
    } else if (opt->type == PORT_MOD_OPTION_INT) {
      edited = ImGui::SliderInt(opt->label, &value, opt->min, opt->max);
    } else if (opt->choiceCount > 0) {
      if (ImGui::BeginCombo(opt->label, opt->choices[value])) {
        for (int i = 0; i < opt->choiceCount; i++) {
          if (ImGui::Selectable(opt->choices[i], i == value)) {
            value = i;
            edited = true;
          }
        }
        ImGui::EndCombo();
      }
    }
    if (value != opt->fallback) {
      ImGui::SameLine();
      if (ImGui::SmallButton(portTrLabel("Default"))) {
        value = opt->fallback;
        edited = true;
      }
    }
    if (edited) {
      portModsSetOption(index, o, value);
      if (opt->nextStart) {
        changedNextStart().insert(std::make_pair(index, o));
      }
    }
    if (opt->nextStart && changedNextStart().count(std::make_pair(index, o)) != 0) {
      portRestartButton(); // (port/include/port/restart.h)
    }
    std::string text = opt->note != nullptr ? opt->note : "";
    if (opt->nextStart) {
      text += text.empty() ? "From the next start." : " (From the next start.)";
    }
    if (!text.empty()) {
      note(text.c_str());
    }
    ImGui::PopID();
  }
  ImGui::Unindent();
}

void modsSection(int inGame) {
  if (!inGame) {
    portModsPreview(portSettingsUserPath()); // the launcher: list them without applying
  }
  note("A mod is a folder or a .ttydmod/.zip file with a mod.json, in the mods folder. Where two mods change "
       "the same thing, the one higher in the list wins. Changes apply to what the game loads next (the next "
       "room or battle); textures change at once; a mod's code starts with the game.");
  if (ImGui::Button(portTrLabel("Reload"))) {
    portModsReload();
  }
#ifdef MODS_OPEN_FOLDERS
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Open the mods folder"))) {
    openFolder(portModsDir());
  }
#endif
  const int n = portModsCount();
  if (n == 0) {
    note("No mods yet.");
  }
  for (int i = 0; i < n; i++) {
    const PortModInfo* mod = portModsGet(i);
    ImGui::PushID(i);
    ImGui::Separator();
    bool on = mod->enabled != 0;
    ImGui::BeginDisabled(mod->error != nullptr);
    if (ImGui::Checkbox("##on", &on)) {
      portModsSetEnabled(i, on ? 1 : 0);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (mod->version[0] != '\0') {
      ImGui::Text("%s  %s", mod->name, mod->version);
    } else {
      ImGui::TextUnformatted(mod->name);
    }
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorStartPos().x - ImGui::GetFrameHeight() * 2.4f);
    ImGui::BeginDisabled(i == 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
      portModsMove(i, -1);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(i == n - 1);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
      portModsMove(i, 1);
    }
    ImGui::EndDisabled();
    if (mod->error != nullptr) {
      colored(kRed, (std::string(mod->path) + " " + mod->error).c_str());
      ImGui::PopID();
      continue;
    }
    const std::string by = mod->author[0] != '\0' ? "by " + std::string(mod->author) + "  ·  " : "";
    note((by + summary(i)).c_str());
    if (mod->description[0] != '\0') {
      note(mod->description);
    }
    if (mod->warning != nullptr) {
      colored(kAmber, mod->warning);
    }
    switch (mod->code) {
    case PORT_MOD_CODE_RUNNING: note("Code: running."); break;
    case PORT_MOD_CODE_STARTS_NEXT_START: note("Code: starts the next time the game starts."); break;
    case PORT_MOD_CODE_STOPS_NEXT_START: note("Code: still running; it stops the next time the game starts."); break;
    case PORT_MOD_CODE_FAILED: colored(kRed, (std::string("Code: ") + mod->codeError).c_str()); break;
    default: break;
    }
    if (mod->enabled) {
      conflicts(i);
    }
    options(i, mod);
    changes(i);
    ImGui::PopID();
  }
}

// Texture packs: counted on demand (reads the folders).
PortTextureStats sStats;
bool sCounted;

void texturesSection(int inGame) {
  PortSettings* s = portSettings();
  note("A Dolphin texture pack works as it comes: put its folder in the textures folder, or in a mod's "
       "textures folder.");
#ifdef MODS_OPEN_FOLDERS
  if (ImGui::Button(portTrLabel("Open the textures folder"))) {
    openFolder(std::string(portSettingsUserPath()) + "/textures");
  }
  ImGui::SameLine();
#endif
  ImGui::BeginDisabled(!inGame);
  if (ImGui::Button(portTrLabel("Reload the packs"))) {
    portModsReloadTextures();
    sCounted = false;
  }
  ImGui::EndDisabled();
  if (inGame) {
    note("Reads the packs again while you play: edit a texture, reload, see it.");
  }

  bool dump = inGame ? sStats.dumping != 0 || s->dumpTextures != 0 : s->dumpTextures != 0;
  if (ImGui::Checkbox(portTrLabel(inGame ? "Dump textures##dump" : "Dump textures (when the game runs)##dump"), &dump)) {
    s->dumpTextures = dump ? 1 : 0;
    portSettingsSave();
    if (inGame) {
      portModsSetTextureDumps(s->dumpTextures);
      sCounted = false;
    }
  }
  note("Saves each texture the game draws that no pack replaces yet, under the name a pack uses, to make or "
       "extend a pack. Leave it off to play: it slows the game a little.");
#ifdef MODS_OPEN_FOLDERS
  if (inGame && portModsDumpDir()[0] != '\0' && ImGui::Button(portTrLabel("Open the dump folder"))) {
    openFolder(portModsDumpDir());
  }
#endif

  // counted again on demand, and every 10 s while dumping adds files
  static Uint64 sCountedAt;
  if (!sCounted || ImGui::Button(portTrLabel("Count again")) || (sStats.dumping && SDL_GetTicks() - sCountedAt > 10000)) {
    portModsTextureStats(&sStats);
    sCounted = true;
    sCountedAt = SDL_GetTicks();
  }
  ImGui::Text(portTr("Replaced: %d textures (%d files)."), sStats.replacements, sStats.files);
  if (sStats.dumped > 0) {
    ImGui::Text(portTr("Seen in play: %d textures, %d of them replaced now (%d%%)."), sStats.dumped, sStats.dumpedCovered,
                sStats.dumpedCovered * 100 / sStats.dumped);
    note("The coverage counts what the dump folder holds: each texture the game drew while dumping was on and "
         "no pack replaced it then. Play with dumping on to see more of the game.");
  } else {
    note("Coverage: turn on dumping and play a while; the textures the game draws are then counted against "
         "the packs.");
  }
  if (sStats.collisions > 0) {
    char label[96];
    std::snprintf(label, sizeof(label), "Names given by more than one file: %d###collisions", sStats.collisions);
    if (ImGui::TreeNode(label)) {
      note("A pack names each texture by its size, format and a hash of its pixels and palette, as Dolphin "
           "does. Two files with one name replace the same texture, and only one is used: between mods, the "
           "one higher in the list; the mods before the textures folder; inside one folder, either, so keep "
           "one. Textures with the same pixels share a name, so one file replaces them all; a name with $ for "
           "a hash covers every texture it stands for.");
      for (int i = 0; i < sStats.collisions && i < 200; i++) {
        const char* packs = "";
        const char* name = portModsTextureCollision(i, &packs);
        ImGui::BulletText("%s: %s", name, packs);
      }
      if (sStats.collisions > 200) {
        note("(the first 200)");
      }
      ImGui::TreePop();
    }
  }
}

} // namespace

// character_export.cpp
void portCharacterExportSection(int inGame);

namespace {

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Mods", nullptr, 0, modsSection,
                           "mods manager conflicts hooks options order enable disable reload folder");
    portSettingsAddSection(kTab, "Texture packs", nullptr, 0, texturesSection,
                           "textures packs hd dump coverage collisions reload dolphin");
    portSettingsAddSection(kTab, "Export characters", nullptr, PORT_SETTING_IN_GAME, portCharacterExportSection,
                           "character model animation anim export blender modding mario luigi partner npc");
  }
} sRegister;

} // namespace
