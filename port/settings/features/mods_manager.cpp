// The mod manager's drawing (its logic: port/include/port/mods.h, the list,
// and port/settings/mod_manager.h, installs): the launcher's Mods tab, the
// settings page's Mods tab over the game, and the Start menu's Mods rows in
// the game's windows. Each mod on or off and in order (drag a row, or the
// arrows), what it is and changes, where it and another change the same
// thing, its own settings (mod.json "options"), and what online play asks
// of it; mods dropped on the window or picked are installed; safe mode after
// a start that didn't get going. And texture packs: coverage, collisions,
// dumping and reloading while the game runs. Built from the launcher's own
// widgets (launcher_style.h), nothing of its own look.
#include "../mod_manager.h"
#include "../launcher_look.h"
#include "../launcher_shot.h"
#include "../launcher_style.h"
#include "../settings_ui.h"

#include "port/lua_mods.h"
#include "port/offscreen.h"
#include "port/mods.h"
#include "port/pause_forms.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

extern "C" void portSettingsOpenAt(const char* tab); // settings_ui.cpp: the page open on a tab

using portui::hint;

namespace {

constexpr const char* kTab = "Mods";

void note(const char* english) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr(english));
  ImGui::PopStyleColor();
}

// text as it is (a mod's own words, a path)
void noteRaw(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

void colored(const ImVec4& color, const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

const ImVec4 kRed(0.78f, 0.16f, 0.14f, 1.0f);
const ImVec4 kAmber(0.70f, 0.42f, 0.02f, 1.0f);
const ImVec4 kGreen(0.18f, 0.48f, 0.20f, 1.0f);

std::string tr(const char* english) { return portTr(english); }

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define MODS_OPEN_FOLDERS 1
// a folder opened in the file manager (made first); when the system didn't open it, *said is where it is
// (shown under the buttons, to go there by hand), else cleared
void openFolder(const std::string& dir, std::string* said) {
  SDL_CreateDirectory(dir.c_str());
  char why[1200];
  *said = portOpenFolder(dir.c_str()) ? std::string() : std::string(portOpenError(why, sizeof(why), dir.c_str(), true));
}
std::string sOpenSaid;     // the bar's (the mods folder)
std::string sOpenSaidMod;  // the mod shown in full's (its folder)
std::string sOpenSaidDump; // the textures section's (the textures and dump folders)
#endif

// --- a mod's parts, in words ---

// by kind: singular, plural
constexpr const char* kKinds[PORT_MOD_CHANGE_COUNT][2] = {
    {"file", "files"},           {"texture", "textures"},       {"message", "messages"},
    {"data change", "data changes"}, {"hook", "hooks"},         {"map", "maps"},
};

std::string count(int n, int kind) { return std::to_string(n) + " " + tr(kKinds[kind][n == 1 ? 0 : 1]); }

// "3 files, 120 textures, 1 hook" (only what it has)
std::string summary(int index) {
  std::string text;
  for (int k = 0; k < PORT_MOD_CHANGE_COUNT; k++) {
    if (const int n = portModsChangeCount(index, k); n > 0) {
      text += (text.empty() ? "" : ", ") + count(n, k);
    }
  }
  return text.empty() ? tr("changes nothing the game loads (code only, or empty)") : text;
}

// What it holds, as badges' words: "Textures · Lua"
constexpr std::pair<unsigned, const char*> kBadges[] = {
    {PORT_MOD_KIND_FILES, "Files"},     {PORT_MOD_KIND_TEXTURES, "Textures"}, {PORT_MOD_KIND_TEXT, "Text"},
    {PORT_MOD_KIND_DATA, "Data"},       {PORT_MOD_KIND_SCRIPTS, "Scripts"},   {PORT_MOD_KIND_LUA, "Lua"},
    {PORT_MOD_KIND_CODE, "Code"},       {PORT_MOD_KIND_LANG, "Language pack"}, {PORT_MOD_KIND_MAPS, "Map edits"},
};

std::string badges(const PortModInfo* mod) {
  std::string text;
  for (const auto& [kind, name] : kBadges) {
    if (mod->kinds & kind) {
      text += (text.empty() ? "" : "  ·  ") + tr(name);
    }
  }
  return text;
}

const char* onlineWords(const PortModInfo* mod) {
  switch (mod->online) {
  case PORT_MOD_ONLINE_OWN: return "Online: yours alone (it only changes the look), so it needn't match.";
  case PORT_MOD_ONLINE_LUA: return "Online: a Lua mod: it runs when every player has the same Lua mods.";
  default: return "Online: every player needs it on, or the game won't start together.";
  }
}

// A Lua mod's state (port/include/port/lua_mods.h): running, stopped by an
// error, or held back online.
void luaState(const PortModInfo* mod) {
  if (mod->lua == 0) {
    return;
  }
  const char* error = nullptr;
  switch (portLuaModState(mod->id, &error)) {
  case PORT_LUA_RUNNING: note("Lua: running."); break;
  case PORT_LUA_FAILED:
    colored(kRed, tr("Lua: stopped by an error (the Lua console, {LUA}, has it all):") + " " + (error != nullptr ? error : ""));
    break;
  case PORT_LUA_PAUSED: colored(kAmber, tr("Lua: paused: the other player's Lua mods aren't the same.")); break;
  case PORT_LUA_HUD_ONLY: note("Lua: runs as a display only: the other player's Lua mods aren't the same."); break;
  default:
    if (mod->enabled && mod->error == nullptr) {
      note("Lua: starts with the game.");
    }
    break;
  }
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
    std::string label = tr(kKinds[k][1]);
    if (!label.empty() && label[0] >= 'a' && label[0] <= 'z') {
      label[0] = char(label[0] - 'a' + 'A');
    }
    label += " (" + std::to_string(n) + ")###kind" + std::to_string(k);
    if (ImGui::TreeNode(label.c_str())) {
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
  if (portModsChangeCount(index, PORT_MOD_CHANGE_HOOK) == 0 && (portModsGet(index)->kinds & PORT_MOD_KIND_CODE)) {
    note("Hooks (code that runs in place of or around the game's functions) show once its code runs.");
  }
  ImGui::TreePop();
}

// Where an enabled mod changes the same thing as another one.
void conflicts(int index) {
  for (int c = 0; c < portModsConflictCount(index); c++) {
    const PortModConflict* conflict = portModsGetConflict(index, c);
    char line[1024];
    std::snprintf(line, sizeof(line), portTr("Also changed by %s: %s (%s)."), conflict->other,
                  count(conflict->count, conflict->kind).c_str(), conflict->what);
    std::string text = line;
    text += " ";
    if (conflict->kind == PORT_MOD_CHANGE_HOOK) {
      text += tr(conflict->wins ? "Both run, this one first if it is higher in the list."
                                : "Both replace it: only the higher one's replacement runs.");
    } else if (conflict->wins) {
      text += tr("This mod's are used (it is higher in the list).");
    } else {
      std::snprintf(line, sizeof(line), portTr("Those of %s are used (it is higher in the list)."), conflict->other);
      text += line;
    }
    colored(conflict->wins ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : kAmber, text);
  }
}

// How many of its conflicts it loses (another mod's are used there).
int losing(int index) {
  int n = 0;
  for (int c = 0; c < portModsConflictCount(index); c++) {
    n += portModsGetConflict(index, c)->wins ? 0 : 1;
  }
  return n;
}

// the mods' options that apply from the next start, changed this run (Restart now beside them)
std::set<std::pair<std::string, int>>& changedNextStart() {
  static std::set<std::pair<std::string, int>> sChanged;
  return sChanged;
}

// The mod's own settings (mod.json "options"), read by its code.
void options(int index, const PortModInfo* mod) {
  if (mod->optionCount == 0) {
    return;
  }
  ImGui::SeparatorText(portTr("Its settings"));
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
    const auto key = std::make_pair(std::string(mod->id), o);
    if (edited) {
      portModsSetOption(index, o, value);
      if (opt->nextStart) {
        changedNextStart().insert(key);
      }
    }
    if (opt->nextStart && changedNextStart().count(key) != 0) {
      portRestartButton(); // (port/include/port/restart.h)
    }
    std::string text = opt->note != nullptr ? opt->note : "";
    if (opt->nextStart) {
      text += (text.empty() ? "" : " ") + tr("(From the next start.)");
    }
    if (!text.empty()) {
      noteRaw(text);
    }
    ImGui::PopID();
  }
}

// --- icons: the mod's icon.png (mod.json "icon"), else its initial on a tile ---

struct Icon {
  ImTextureID tex{};
  float w = 0, h = 0;
};
std::map<std::string, Icon> sIcons; // by its path and icon (made once: Aurora keeps them)

const Icon* icon(int index) {
  const PortModInfo* mod = portModsGet(index);
  const std::string key = std::string(portModsPath(index)) + "|" + mod->id;
  if (const auto it = sIcons.find(key); it != sIcons.end()) {
    return it->second.tex ? &it->second : nullptr;
  }
  Icon& out = sIcons[key];
  std::vector<uint8_t> bytes;
  if (!portModsReadIcon(index, bytes)) {
    return nullptr;
  }
  SDL_Surface* s = SDL_LoadPNG_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true);
  if (s == nullptr) {
    return nullptr;
  }
  SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(s);
  if (rgba != nullptr && (rgba->w > 128 || rgba->h > 128)) {
    // an icon is shown small: kept at 128 at most
    const float k = 128.f / static_cast<float>(std::max(rgba->w, rgba->h));
    SDL_Surface* small = SDL_ScaleSurface(rgba, std::max(1, int(rgba->w * k)), std::max(1, int(rgba->h * k)),
                                          SDL_SCALEMODE_LINEAR);
    SDL_DestroySurface(rgba);
    rgba = small;
  }
  if (rgba != nullptr && rgba->pitch == rgba->w * 4) {
    out.tex = aurora_imgui_add_texture(uint32_t(rgba->w), uint32_t(rgba->h), rgba->pixels);
    launcherShotTexture(out.tex, rgba->w, rgba->h, rgba->pixels); // (so the UI captures draw it too)
    out.w = float(rgba->w);
    out.h = float(rgba->h);
  }
  SDL_DestroySurface(rgba);
  return out.tex ? &out : nullptr;
}

// the icon in a size x size square at the cursor (a Dummy takes its place)
void drawIcon(int index, float size) {
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const PortModInfo* mod = portModsGet(index);
  if (const Icon* i = icon(index)) {
    const float k = size / std::max(i->w, i->h);
    const ImVec2 a(at.x + (size - i->w * k) * 0.5f, at.y + (size - i->h * k) * 0.5f);
    dl->AddImage(i->tex, a, ImVec2(a.x + i->w * k, a.y + i->h * k));
  } else {
    // a paper tile in one of the launcher's colours, by the mod's id, with its initial
    static const ImU32 kTiles[] = {kCurtain, IM_COL32(60, 118, 214, 255), IM_COL32(84, 150, 72, 255),
                                   IM_COL32(204, 146, 30, 255), IM_COL32(140, 80, 170, 255)};
    unsigned h = 0;
    for (const char* c = mod->id; *c != '\0'; c++) {
      h = h * 31u + static_cast<unsigned char>(*c);
    }
    dl->AddRectFilled(at, ImVec2(at.x + size, at.y + size), kTiles[h % 5], size * 0.22f);
    char initial[8] = {mod->name[0] != '\0' ? mod->name[0] : '?', '\0'};
    if (static_cast<unsigned char>(initial[0]) >= 0x80) {
      // a UTF-8 letter: its whole sequence
      int n = 1;
      while (n < 4 && (static_cast<unsigned char>(mod->name[n]) & 0xC0) == 0x80) {
        initial[n] = mod->name[n];
        n++;
      }
      initial[n] = '\0';
    } else if (initial[0] >= 'a' && initial[0] <= 'z') {
      initial[0] = char(initial[0] - 'a' + 'A');
    }
    ImFont* font = gLauncherFonts.heading != nullptr ? gLauncherFonts.heading : ImGui::GetFont();
    const float px = size * 0.6f;
    const ImVec2 sz = font->CalcTextSizeA(px, 1e9f, 0.f, initial);
    dl->AddText(font, px, ImVec2(at.x + (size - sz.x) * 0.5f, at.y + (size - sz.y) * 0.5f), IM_COL32(255, 255, 255, 255),
                initial);
  }
  ImGui::Dummy(ImVec2(size, size));
}

// --- the list ---

std::string sSelected; // the mod shown in full, by id
std::string sConfirmRemove;
std::string sRemoveError;

int selectedIndex() {
  for (int i = 0; i < portModsCount(); i++) {
    if (sSelected == portModsGet(i)->id) {
      return i;
    }
  }
  return -1;
}

void setEnabled(int i, bool on) {
  const unsigned kinds = portModsGet(i)->kinds;
  portModsSetEnabled(i, on ? 1 : 0);
  modmgr::listChanged(kinds);
}

void move(int i, int delta) {
  const unsigned kinds = portModsGet(i)->kinds;
  portModsMove(i, delta);
  modmgr::listChanged(kinds);
}

// In the launcher (its Mods tab, what goes over it), the launcher's paper widgets (launcher_look.h);
// in the game, Dear ImGui's in the settings page's style.
bool sInLauncher = false;
float sLauncherScale = 1.f;

bool modButton(const char* label, bool primary = false) {
  if (sInLauncher) {
    return launcherButton(label, ImVec2(0, 0), sLauncherScale, primary ? LauncherButton::Primary : LauncherButton::Normal);
  }
  return primary ? launcherRedButton(label) : ImGui::Button(label);
}

// One mod's row: its switch, icon, name, version, author and what it holds;
// a click shows it in full; dragged, it moves in the order.
void row(int i, float scale) {
  const PortModInfo* mod = portModsGet(i);
  ImGui::PushID(mod->id);
  const float iconSize = 40.f * scale;
  const float height = std::max(iconSize, ImGui::GetTextLineHeightWithSpacing() * 2.f);
  const ImVec2 start = ImGui::GetCursorPos();
  if (sInLauncher) { // the launcher draws the row's strip and glove itself
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
  }
  const bool clicked =
      ImGui::Selectable("##row", sSelected == mod->id, ImGuiSelectableFlags_AllowOverlap, ImVec2(0, height));
  if (sInLauncher) {
    ImGui::PopStyleColor(3);
    launcherRowDecorate(sSelected == mod->id, scale, 0.f);
  }
  if (clicked) {
    sSelected = mod->id;
    sConfirmRemove.clear();
  }
  if (ImGui::IsItemFocused() && sSelected != mod->id && ImGui::GetIO().NavVisible) {
    sSelected = mod->id; // with a controller the row under the focus is the one shown
  }
  // dragged onto another row: moved to its place
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
    // (the mod's id, not its row: a background install can reload the list during the drag)
    ImGui::SetDragDropPayload("PORT_MOD_ROW", mod->id, std::strlen(mod->id) + 1);
    ImGui::TextUnformatted(mod->name);
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PORT_MOD_ROW")) {
      const std::string id(static_cast<const char*>(p->Data), p->DataSize > 0 ? p->DataSize - 1 : 0);
      int from = -1;
      for (int k = 0; k < portModsCount(); k++) {
        if (const PortModInfo* m = portModsGet(k); m != nullptr && id == m->id) {
          from = k;
          break;
        }
      }
      if (from >= 0 && i < portModsCount()) {
        const unsigned kinds = portModsGet(from)->kinds;
        while (from != i) {
          portModsMove(from, from < i ? 1 : -1);
          from += from < i ? 1 : -1;
        }
        modmgr::listChanged(kinds);
      }
    }
    ImGui::EndDragDropTarget();
  }
  ImGui::SetCursorPos(ImVec2(start.x + ImGui::GetStyle().ItemSpacing.x * 0.5f, start.y + (height - ImGui::GetFrameHeight()) * 0.5f));
  bool on = mod->enabled != 0;
  ImGui::BeginDisabled(mod->error != nullptr);
  if (ImGui::Checkbox("##on", &on)) {
    setEnabled(i, on);
    mod = portModsGet(i);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetCursorPosY(start.y + (height - iconSize) * 0.5f);
  drawIcon(i, iconSize);
  ImGui::SameLine();
  ImGui::SetCursorPosY(start.y);
  ImGui::BeginGroup();
  const bool dim = !mod->enabled || mod->error != nullptr || portModsSafeMode();
  if (dim) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  }
  if (mod->version[0] != '\0') {
    ImGui::Text("%s  %s", mod->name, mod->version);
  } else {
    ImGui::TextUnformatted(mod->name);
  }
  if (dim) {
    ImGui::PopStyleColor();
  }
  std::string line;
  if (mod->error != nullptr) {
    ImGui::TextColored(kRed, "%s", portTr("Can't be used: see why."));
  } else {
    if (mod->author[0] != '\0') {
      char by[160];
      std::snprintf(by, sizeof(by), portTr("by %s"), mod->author);
      line = by;
    }
    const std::string kinds = badges(mod);
    line += (line.empty() || kinds.empty() ? "" : "  ·  ") + kinds;
    if (mod->enabled && losing(i) > 0) {
      ImGui::TextColored(kAmber, "%s", (tr("Overlaps another mod") + (line.empty() ? "" : "  ·  " + line)).c_str());
    } else {
      ImGui::TextDisabled("%s", line.c_str());
    }
  }
  ImGui::EndGroup();
  ImGui::SetCursorPos(ImVec2(start.x, start.y + height + ImGui::GetStyle().ItemSpacing.y));
  ImGui::PopID();
}

// --- the parts around the list ---

void installBar(bool launcher) {
  if (modmgr::canPickFiles()) {
    if (modButton(portTrLabel("Add a mod..."), true)) {
      modmgr::pickFiles();
    }
    ImGui::SameLine();
  }
  if (modmgr::canPickFolder()) {
    if (modButton(portTrLabel("Add a folder..."))) {
      modmgr::pickFolder();
    }
    ImGui::SameLine();
  }
#ifdef MODS_OPEN_FOLDERS
  if (modButton(portTrLabel("Open the mods folder"))) {
    openFolder(portModsDir(), &sOpenSaid);
  }
  ImGui::SameLine();
#endif
  if (modButton(portTrLabel("Reload"))) {
    portModsReload();
    modmgr::listChanged(PORT_MOD_KIND_LANG);
  }
#ifdef MODS_OPEN_FOLDERS
  if (!sOpenSaid.empty()) {
    colored(kRed, sOpenSaid); // (a folder the system didn't open: where it is)
  }
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
  note("To add a mod: put it in the Files app's Paper Cut folder, Saves > mods, then press Reload.");
#elif defined(__ANDROID__)
  note("Add a mod picks a .ttydmod or .zip file from your phone and installs it.");
#else
  note(launcher ? "Drop mods on this window to install them: a .ttydmod or .zip, a mod's folder, a texture pack or "
                  "a .lua file. Only your own files: Paper Cut never downloads mods."
                : "Drop mods on the game's window to install them: a .ttydmod or .zip, a mod's folder, a texture "
                  "pack or a .lua file.");
#endif
  const modmgr::Progress p = modmgr::progress();
  if (p.busy) {
    char text[300];
    std::snprintf(text, sizeof(text), portTr("Installing %s..."), p.item.c_str());
    ImGui::TextUnformatted(text);
    const float f = p.total > 0 ? float(double(p.done) / double(p.total)) : -1.f * float(ImGui::GetTime());
    ImGui::ProgressBar(f, ImVec2(-1, 0), p.waiting > 0 ? (std::to_string(p.waiting) + " " + tr("more waiting")).c_str()
                                                       : nullptr);
  }
  const std::vector<modmgr::Result> results = modmgr::results();
  for (auto it = results.rbegin(); it != results.rend() && it - results.rbegin() < 6; ++it) {
    colored(it->ok ? kGreen : kRed, it->text);
  }
  if (!results.empty() && !p.busy) {
    if (ImGui::SmallButton(portTrLabel("Clear##results"))) {
      modmgr::clearResults();
    }
  }
}

// Safe mode on now: what it means, and the way back.
void safeBanner(bool launcher) {
  if (!portModsSafeMode()) {
    return;
  }
  colored(kAmber, tr("Safe mode: every mod is off for this start. They stay as they are, and come back at the next "
                     "start."));
  if (launcher) {
    if (modButton(portTrLabel("Start with mods after all"))) {
      portModsSetSafeMode(0);
    }
  } else if (portRestartSupported()) {
    if (modButton(portTrLabel("Restart with mods"))) {
      portRestartRequest(1);
    }
  }
}

// Online play and the Lua mods: whether they run in the session now.
void onlineLine() {
  int lua = 0, shared = 0;
  for (int i = 0; i < portModsCount(); i++) {
    const PortModInfo* m = portModsGet(i);
    if (m->enabled && m->error == nullptr) {
      lua += (m->kinds & PORT_MOD_KIND_LUA) ? 1 : 0;
      shared += m->online == PORT_MOD_ONLINE_SHARED ? 1 : 0;
    }
  }
  if (portStateSyncLinked()) { // each in their own game: the others' sets, from their games' messages
    const bool same = portStateSyncLuaSame() != 0;
    if (lua == 0 && same) {
      return;
    }
    colored(same ? kGreen : kAmber,
            tr(same ? "Online: the other player has the same Lua mods, so they run."
                    : "Online: the other player's Lua mods aren't the same: they're paused (display-only ones still "
                      "draw)."));
    return;
  }
  if (shared > 0) {
    note("Online, every player needs the same mods on (all but texture packs, language packs and Lua mods).");
  } else if (lua > 0) {
    note("Online, Lua mods run only when every player has the same ones.");
  }
}

// On the same line as the last button when the next one (its English label) fits, else under it.
void sameLineFor(const char* label) {
  const ImGuiStyle& st = ImGui::GetStyle();
  // (the launcher's paper buttons are wider: launcherButton's padding and edge)
  const float w = ImGui::CalcTextSize(portTr(label)).x + (sInLauncher ? 50.f * sLauncherScale : st.FramePadding.x * 2.f);
  const float right = ImGui::GetItemRectMax().x + st.ItemSpacing.x + w;
  if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
    ImGui::SameLine();
  }
}

// The mod shown in full.
void details(int i, bool inGame, float scale) {
  const PortModInfo* mod = portModsGet(i);
  ImGui::PushID(mod->id);
  drawIcon(i, 64.f * scale);
  ImGui::SameLine();
  ImGui::BeginGroup();
  if (sInLauncher) {
    launcherHeading(mod->name, scale);
  } else {
    ImGui::PushFont(gLauncherFonts.heading);
    ImGui::TextWrapped("%s", mod->name);
    ImGui::PopFont();
  }
  std::string line = mod->version[0] != '\0' ? tr("Version") + " " + mod->version : std::string();
  if (mod->author[0] != '\0') {
    char by[160];
    std::snprintf(by, sizeof(by), portTr("by %s"), mod->author);
    line += (line.empty() ? "" : "  ·  ") + std::string(by);
  }
  if (!line.empty()) {
    noteRaw(line);
  }
  ImGui::EndGroup();
  if (mod->description[0] != '\0') {
    ImGui::TextWrapped("%s", mod->pack ? portTr(mod->description) : mod->description);
  }
  if (mod->error != nullptr) {
    colored(kRed, std::string(mod->path) + " " + mod->error);
  } else {
    noteRaw(badges(mod) + (badges(mod).empty() ? "" : "  ·  ") + summary(i));
    if (mod->warning != nullptr) {
      colored(kAmber, mod->warning);
    }
    switch (mod->code) {
    case PORT_MOD_CODE_RUNNING: note("Code: running."); break;
    case PORT_MOD_CODE_STARTS_NEXT_START: note("Code: starts the next time the game starts."); break;
    case PORT_MOD_CODE_STOPS_NEXT_START: note("Code: still running; it stops the next time the game starts."); break;
    case PORT_MOD_CODE_FAILED: colored(kRed, tr("Code:") + " " + mod->codeError); break;
    default:
      if ((mod->kinds & PORT_MOD_KIND_CODE) && !inGame && mod->enabled) {
        note("Code: starts with the game.");
      }
      break;
    }
    luaState(mod);
    note(onlineWords(mod));
    if (mod->pack) {
      note("Turned off, the menus leave its language; Language > Menu language chooses it.");
    }
    if (mod->enabled) {
      conflicts(i);
    }
  }
  // its place and the buttons
  const int n = portModsCount();
  ImGui::BeginDisabled(i == 0);
  if (modButton(portTrLabel("Move up"))) {
    move(i, -1);
  }
  ImGui::EndDisabled();
  sameLineFor("Move down");
  ImGui::BeginDisabled(i == n - 1);
  if (modButton(portTrLabel("Move down"))) {
    move(i, 1);
  }
  ImGui::EndDisabled();
  const std::string path = portModsPath(i);
  if (!path.empty()) {
#ifdef MODS_OPEN_FOLDERS
    sameLineFor("Show its folder");
    if (modButton(portTrLabel("Show its folder"))) {
      // a folder's own; a file's, the mods folder
      SDL_PathInfo info;
      openFolder(SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY ? path
                                                                                             : std::string(portModsDir()),
                 &sOpenSaidMod);
    }
#endif
    sameLineFor(sConfirmRemove == mod->id ? "Remove: sure?" : "Remove");
    if (sConfirmRemove == mod->id) {
      if (modButton(portTrLabel("Remove: sure?"), true)) {
        const unsigned kinds = mod->kinds;
        const char* why = portModsRemove(i);
        sRemoveError = why != nullptr ? why : "";
        sConfirmRemove.clear();
        modmgr::listChanged(kinds);
        ImGui::PopID();
        return; // (the list changed)
      }
    } else if (modButton(portTrLabel("Remove"))) {
      sConfirmRemove = mod->id;
      sRemoveError.clear();
    }
    if (sConfirmRemove == mod->id) {
      note("Moves it out of the mods folder into mods_old beside it: nothing is deleted.");
    }
    if (!sRemoveError.empty()) {
      colored(kRed, sRemoveError);
    }
#ifdef MODS_OPEN_FOLDERS
    if (!sOpenSaidMod.empty()) {
      colored(kRed, sOpenSaidMod); // (its folder the system didn't open: where it is)
    }
#endif
  }
  if (mod->error == nullptr) {
    options(i, mod);
    changes(i);
  }
  ImGui::PopID();
}

// The whole manager: the bar, the list, the one shown in full. twoColumns:
// the list on the left, the details on the right (else under it).
void manager(bool inGame, bool twoColumns, float scale) {
  modmgr::poll(); // an install done: the list read again
  if (selectedIndex() < 0 && portModsCount() > 0) {
    sSelected = portModsGet(0)->id;
  }
  installBar(!inGame);
  safeBanner(!inGame);
  onlineLine();
  ImGui::Spacing();
  const int n = portModsCount();
  if (n == 0) {
    ImGui::Separator();
    note("No mods yet. A mod is a folder or a .ttydmod/.zip file with a mod.json; Add a mod or drop one here.");
    return;
  }
  const float listWidth = twoColumns ? std::max(280.f * scale, ImGui::GetContentRegionAvail().x * 0.45f) : 0.f;
  const float listHeight = twoColumns ? 0.f : std::min(ImGui::GetContentRegionAvail().y * 0.5f,
                                                       (std::max(40.f * scale, ImGui::GetTextLineHeightWithSpacing() * 2.f) +
                                                        ImGui::GetStyle().ItemSpacing.y) * float(n) + 8.f * scale);
  ImGui::BeginChild("modlist", ImVec2(listWidth, twoColumns ? 0.f : std::max(listHeight, 120.f * scale)),
                    sInLauncher ? ImGuiChildFlags_None : ImGuiChildFlags_Borders); // (the launcher: rows on the paper)
  int on = 0;
  for (int i = 0; i < n; i++) {
    row(i, scale);
    on += portModsGet(i) != nullptr && portModsGet(i)->enabled ? 1 : 0;
  }
  ImGui::EndChild();
  if (twoColumns) {
    ImGui::SameLine();
  } else {
    char counts[96];
    std::snprintf(counts, sizeof(counts), portTr("%d mods, %d on. The higher one wins where two change the same thing."),
                  portModsCount(), on);
    noteRaw(counts);
  }
  if (twoColumns) {
    ImGui::BeginChild("moddetails", ImVec2(0, 0));
  } else {
    ImGui::Separator();
  }
  if (const int i = selectedIndex(); i >= 0) {
    details(i, inGame, scale);
  }
  if (twoColumns) {
    ImGui::EndChild();
  }
}

// The settings page's Mods section, over the game (the launcher has its own tab).
void modsSection(int inGame) {
  manager(inGame != 0, false, ImGui::GetFontSize() / 18.f);
}

// --- texture packs: counted on demand (reads the folders) ---

PortTextureStats sStats;
bool sCounted;

void texturesSection(int inGame) {
  PortSettings* s = portSettings();
  note("A Dolphin texture pack works as it comes: put its folder in the textures folder, or in a mod's "
       "textures folder; or drop it on the window and it becomes a mod of its own.");
#ifdef MODS_OPEN_FOLDERS
  if (ImGui::Button(portTrLabel("Open the textures folder"))) {
    openFolder(std::string(portSettingsUserPath()) + "/textures", &sOpenSaidDump);
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
    hint("Reads the packs again while you play: edit a texture, reload, see it.");
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
  hint("Saves each texture the game draws that no pack replaces yet, under the name a pack uses, to make or "
       "extend a pack. Leave it off to play: it slows the game a little.");
#ifdef MODS_OPEN_FOLDERS
  if (inGame && portModsDumpDir()[0] != '\0' && ImGui::Button(portTrLabel("Open the dump folder"))) {
    openFolder(portModsDumpDir(), &sOpenSaidDump);
  }
  if (!sOpenSaidDump.empty()) {
    colored(kRed, sOpenSaidDump); // (a folder the system didn't open: where it is)
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
    hint("The coverage counts what the dump folder holds: each texture the game drew while dumping was on and "
         "no pack replaced it then. Play with dumping on to see more of the game.");
  } else {
    hint("Coverage: turn on dumping and play a while; the textures the game draws are then counted against "
         "the packs.");
  }
  if (sStats.collisions > 0) {
    char label[160];
    std::snprintf(label, sizeof(label), "%s: %d###collisions", portTr("Names given by more than one file"),
                  sStats.collisions);
    if (ImGui::TreeNode(label)) {
      hint("A pack names each texture by its size, format and a hash of its pixels and palette, as Dolphin "
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
        hint("(the first 200)");
      }
      ImGui::TreePop();
    }
  }
}

// --- the launcher ---

std::string sOfferNames; // safe mode's offer: the last start's mods, a name a line
bool sOfferChecked, sOfferOpen;

// Safe mode's question, as a message box (no launcher, or it skipped to the
// disc): not under $TTYD_FAST (tests run unattended).
int askBox(const char* names) {
  if (SDL_getenv("TTYD_FAST") != nullptr) {
    return 0;
  }
  std::string text = tr("Paper Cut closed while starting last time, with these mods on:") + "\n\n";
  for (const char* p = names; *p != '\0';) {
    const char* eol = std::strchr(p, '\n');
    const std::string name = eol != nullptr ? std::string(p, eol) : std::string(p);
    text += "  - " + name + "\n";
    p = eol != nullptr ? eol + 1 : p + name.size();
  }
  text += "\n" + tr("Start with every mod off this time? They stay as they are, and come back at the next start.");
  static std::string sOff, sOn, sTitle;
  sOff = tr("Start with mods off");
  sOn = tr("Start with mods on");
  sTitle = tr("Safe mode");
  const SDL_MessageBoxButtonData buttons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, sOff.c_str()},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, sOn.c_str()},
  };
  const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, nullptr, sTitle.c_str(), text.c_str(), 2, buttons, nullptr};
  int chosen = 0;
  return portShowMessageBox(&box, &chosen) && chosen == 1 ? 1 : 0;
}

} // namespace

// The launcher's Mods tab (launcher_ui.cpp): the manager on paper cards,
// the list on the left and the mod shown in full on the right (stacked on a
// narrow stage).
void portModManagerLauncherTab(float scale, bool stacked) {
  portModsPreview(portSettingsUserPath()); // listed without applying
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 size(avail.x, avail.y - 8.f * scale);
  launcherPanel(ImGui::GetWindowDrawList(), at, ImVec2(at.x + size.x, at.y + size.y), scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f * scale, 20.f * scale));
  ImGui::BeginChild("mods", size, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  sInLauncher = true;
  sLauncherScale = scale;
  manager(false, !stacked, scale);
  sInLauncher = false;
  ImGui::EndChild();
}

// The way back on the launcher's Mods tab (launcher_ui.cpp): a Remove: sure? taken back; false when none
// was asked (the launcher goes back to Play)
bool portModManagerLauncherBack() {
  if (sConfirmRemove.empty()) {
    return false;
  }
  sConfirmRemove.clear();
  return true;
}

// Over the launcher (after its window): files dragged over it, and safe
// mode's offer at the first frame when the last start didn't get going.
// True when the launcher should show its Mods tab (a drop came, or the offer
// asked to see the mods).
bool portModManagerLauncherFront(float scale) {
  bool show = false;
  sInLauncher = true;
  sLauncherScale = scale;
  ImGui::PushFont(gLauncherFonts.body);
  static bool sTestDropped;
  if (!sTestDropped) {
    // $TTYD_MODS_DROP (tests): paths separated by '|', dropped on the window as SDL drop events
    sTestDropped = true;
    static std::vector<std::string> sPaths;
    if (const char* list = SDL_getenv("TTYD_MODS_DROP")) {
      for (const char* p = list; *p != '\0';) {
        const char* bar = std::strchr(p, '|');
        sPaths.emplace_back(bar != nullptr ? std::string(p, bar) : std::string(p));
        p = bar != nullptr ? bar + 1 : p + std::strlen(p);
      }
      for (const std::string& path : sPaths) {
        SDL_Event e{};
        e.type = SDL_EVENT_DROP_FILE;
        e.drop.data = path.c_str();
        SDL_PushEvent(&e);
      }
    }
  }
  static uint64_t sSeenDrop;
  if (modmgr::droppedAt() != sSeenDrop) {
    sSeenDrop = modmgr::droppedAt();
    show = true;
  }
  if (modmgr::dragging()) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    const float m = 60.f * scale;
    dl->AddRectFilled(ImVec2(0, 0), d, IM_COL32(20, 10, 30, 150));
    launcherPanel(dl, ImVec2(m, d.y * 0.32f), ImVec2(d.x - m, d.y * 0.68f), scale);
    ImFont* font = gLauncherFonts.heading;
    const float px = font->FontSize * ImGui::GetIO().FontGlobalScale * 1.2f;
    const char* text = portTr("Drop to install the mod");
    const ImVec2 sz = font->CalcTextSizeA(px, 1e9f, 0.f, text);
    dl->AddText(font, px, ImVec2((d.x - sz.x) * 0.5f, (d.y - sz.y) * 0.5f), kInk, text);
  }
  if (!sOfferChecked) {
    sOfferChecked = true;
    if (const char* names = portModsLastStartFailed(portSettingsUserPath())) {
      sOfferNames = names;
      sOfferOpen = true;
    }
  }
  if (sOfferOpen) {
    ImGui::OpenPopup("###safemode");
    const ImVec2 d = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(d.x * 0.5f, d.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(d.x - 40.f * scale, 620.f * scale), 0));
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f * scale, 20.f * scale));
  if (ImGui::BeginPopupModal((tr("Safe mode") + "###safemode").c_str(), nullptr,
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar)) {
    ImGui::PushFont(gLauncherFonts.heading);
    ImGui::TextWrapped("%s", portTr("Paper Cut closed while starting last time"));
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::TextWrapped("%s", portTr("These mods were on:"));
    for (size_t at = 0; at < sOfferNames.size();) {
      size_t eol = sOfferNames.find('\n', at);
      eol = eol == std::string::npos ? sOfferNames.size() : eol;
      ImGui::BulletText("%s", sOfferNames.substr(at, eol - at).c_str());
      at = eol + 1;
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", portTr("Start with every mod off this time? They stay as they are, and come back at the "
                                    "next start."));
    ImGui::Spacing();
    if (modButton(portTrLabel("Start with mods off"), true)) {
      portModsSetSafeMode(1);
      sOfferOpen = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (modButton(portTrLabel("Keep them on"))) {
      portModsSetSafeMode(0);
      sOfferOpen = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (modButton(portTrLabel("See the mods"))) {
      portModsSetSafeMode(1);
      sOfferOpen = false;
      show = true;
      ImGui::CloseCurrentPopup();
    }
    // the way back (B, Escape, Android's back, the mouse's back): the safe answer, mods off this time
    if (sOfferOpen && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
      portModsSetSafeMode(1);
      sOfferOpen = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
  ImGui::PopFont();
  sInLauncher = false;
  return show;
}

namespace {

// --- the Start menu's Mods rows (port/include/port/pause_forms.h) ---

enum { kRowManager = 1000, kRowReload, kRowFolder, kRowRestart, kRowSafe, kRowNone };

// a mod's name for the game's font: ASCII as it is, Latin-1 letters on the
// US and EU releases (the font's own), else '?'
void gameText(const char* utf8, char* out, size_t size) {
  const bool latin = std::strcmp(PORT_GAME_ID, "G8MJ01") != 0;
  size_t n = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8); *p != '\0' && n + 1 < size;) {
    if (*p < 0x80) {
      out[n++] = (*p == '<' || *p == '>' || *p == '\\' || *p == '`') ? '?' : char(*p);
      p++;
    } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      const unsigned c = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
      out[n++] = latin && c >= 0xA0 && c <= 0xFF ? char(c) : '?';
      p += 2;
    } else {
      out[n++] = '?';
      p++;
      while ((*p & 0xC0) == 0x80) {
        p++;
      }
    }
  }
  out[n] = '\0';
}

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (portModsSafeMode()) {
    out[n++] = portPauseRow("Safe mode: mods off for this start",
                            "Every mod is off for this start: they stay as they are and come back at the next start.",
                            nullptr, kRowSafe, PORT_PAUSE_ROW_DIM);
    if (portRestartSupported()) {
      out[n++] = portPauseRow("Restart with mods", "Starts the game again with the mods on; quick resume brings you back.",
                              nullptr, kRowRestart, PORT_PAUSE_ROW_CONFIRM);
    }
  }
  const int count = portModsCount();
  if (count == 0) {
    out[n++] = portPauseRow("No mods yet.",
                            "A mod is a folder or a .ttydmod or .zip file in the mods folder; drop one on the window.",
                            nullptr, kRowNone, PORT_PAUSE_ROW_DIM);
  }
  for (int i = 0; i < count && n < max - 3; i++) {
    const PortModInfo* mod = portModsGet(i);
    const char* noteText = mod->error != nullptr ? "This mod can't be used: the full page (Mod manager) says why."
                           : mod->online == PORT_MOD_ONLINE_SHARED
                               ? "A mod: {A} turns it on or off; files change from the next room. Online, every player "
                                 "needs it on."
                           : mod->online == PORT_MOD_ONLINE_LUA
                               ? "A Lua mod: {A} turns it on or off. Online, it runs when every player has the same Lua "
                                 "mods."
                               : "A mod that changes the look: {A} turns it on or off; it shows at once.";
    PortPauseRow r = portPauseRow(nullptr, noteText, mod->error != nullptr ? "Off" : mod->enabled ? "On" : "Off", i,
                                  mod->error != nullptr ? PORT_PAUSE_ROW_DIM : 0);
    gameText(mod->name, r.text, sizeof(r.text));
    out[n++] = r;
  }
  out[n++] = portPauseRow("Mod manager...",
                          "Paper Cut's full settings page on Mods: the order, what each mod changes, its settings and "
                          "installing.",
                          nullptr, kRowManager);
  out[n++] = portPauseRow("Reload mods", "Reads the mods folder again: a mod added or changed.", nullptr, kRowReload);
#ifdef MODS_OPEN_FOLDERS
  out[n++] = portPauseRow("Open the mods folder", "Shows the mods folder on the desktop.", nullptr, kRowFolder);
#endif
  return n;
}

const char* formAct(int id, int dir) {
  if (id >= 0 && id < portModsCount()) {
    const PortModInfo* mod = portModsGet(id);
    if (mod->error != nullptr) {
      return dir == 0 ? "This mod can't be used: the full page (Mod manager) says why." : nullptr;
    }
    const bool on = dir == 0 ? !mod->enabled : dir > 0;
    if (on != (mod->enabled != 0)) {
      setEnabled(id, on);
    }
    return (portModsGet(id)->kinds & PORT_MOD_KIND_CODE) ? "Its code follows at the next start." : nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  switch (id) {
  case kRowManager: portSettingsOpenAt(kTab); return nullptr;
  case kRowReload:
    portModsReload();
    modmgr::listChanged(PORT_MOD_KIND_LANG);
    return "The mods folder read again.";
#ifdef MODS_OPEN_FOLDERS
  case kRowFolder:
    openFolder(portModsDir(), &sOpenSaid); // (the full page shows its path too)
    return sOpenSaid.empty() ? nullptr : "The mods folder didn't open: it is the mods folder in the save folder.";
#endif
  case kRowRestart: portRestartRequest(1); return nullptr;
  case kRowSafe: return "Every mod is off for this start: they stay as they are and come back at the next start.";
  default: return nullptr;
  }
}

const PortPauseForm kForm = {kTab, "Mods", formRows, formAct, PORT_PAUSE_FORM_OWN};

} // namespace

// character_export.cpp
void portCharacterExportSection(int inGame);

namespace {

struct Register {
  Register() {
    portModsSetSafeAsk(askBox);
    portPauseAddForm(&kForm);
    // over the game (the launcher has its Mods tab)
    portSettingsAddSection(kTab, "Mods", nullptr, PORT_SETTING_IN_GAME, modsSection,
                           "mods manager install drop conflicts hooks options order enable disable reload folder safe "
                           "mode icon lua online");
    portSettingsAddSection(kTab, "Texture packs", nullptr, 0, texturesSection,
                           "textures packs hd dump coverage collisions reload dolphin");
    portSettingsAddSection(kTab, "Export characters", nullptr, PORT_SETTING_IN_GAME, portCharacterExportSection,
                           "character model animation anim export blender modding mario luigi partner npc");
  }
} sRegister;

} // namespace
