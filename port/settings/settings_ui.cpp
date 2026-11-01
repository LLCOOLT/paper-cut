// The settings page (Aurora backend), drawn with Dear ImGui over the game.
//
// Opens with F1, a controller's Back/Select, or a three-finger tap; Escape,
// B or the same again closes it. F11 toggles fullscreen at any time. While it
// is open the game gets no input (PADBlockInput) and keeps running, as in
// Dusklight (the Twilight Princess port on Aurora), whose menu this follows.
// Every change applies at once and is written to settings.txt
// (port/src/aurora/settings_aurora.c).

#include "port/settings.h"
#include "settings_ui.h"
#include "launcher_style.h"
#include "port/speed.h"
#include "port/touch_overlay.h"
#include "port/savestate.h"
#include "port/cloud.h"
#include "port/mods.h"
#include "port/romfont_pc.h"
#include "port/layout.h"
#include "../archipelago/ttyd_ap.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <cfloat>
#include <ctime>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

extern "C" {
void portIosHapticTap(void);    // port/ios/ios_app.m
extern int gPortAcCueState;     // port/src/common/ac_cue_pc.c
extern int gPortAcCueLead;
void portShotRequestUser(void); // port/src/aurora/shot_aurora.c
const char* portShotStatus(void);
// port/src/common/warp_pc.c
void portWarpRequestSave(const char* path);
const char* portWarpStatus(void);
}

namespace {

bool sOpen;
bool sWasOpen;
// the windows drawn this frame (the settings page, the crash notice): touches
// there are theirs, not the on-screen controls' (portSettingsOwnsPoint)
ImVec2 sWinMin[2], sWinMax[2];
int sWinCount;

void noteWindow() {
  if (sWinCount < 2) {
    sWinMin[sWinCount] = ImGui::GetWindowPos();
    sWinMax[sWinCount] = ImVec2(sWinMin[sWinCount].x + ImGui::GetWindowSize().x,
                                sWinMin[sWinCount].y + ImGui::GetWindowSize().y);
    sWinCount++;
  }
}
int sRebind = -1;     // the controller button mapping waiting for a press
bool sRebindArmed;    // every button was up since (the press that chose it is not taken)
bool sTouchDevice; // a touch seen: bigger text and controls

// a three-finger tap: three fingers down within 180 ms, all up within 550 ms
int sFingers;
Uint64 sFirstDown;
Uint64 sThirdDown;
bool sTap;

void setOpen(bool open) {
  if (open == sOpen) {
    return;
  }
  sOpen = open;
  PADBlockInput(open);
  if (!open) {
    portSettingsSave();
  }
}

void changed() { portSettingsSave(); }

constexpr const char* kRates[] = {"60", "120", "144", "240", "display"};
constexpr const char* kRateNames[] = {"60 Hz (original)", "120 Hz", "144 Hz", "240 Hz", "The display's rate"};
constexpr const char* kCorners[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};

bool combo(const char* label, int* value, const char* const* names, int count) {
  bool change = false;
  if (ImGui::BeginCombo(label, names[*value])) {
    for (int i = 0; i < count; i++) {
      if (ImGui::Selectable(names[i], i == *value)) {
        *value = i;
        change = true;
      }
      if (i == *value) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  return change;
}

bool check(const char* label, int* value) {
  bool v = *value != 0;
  if (ImGui::Checkbox(label, &v)) {
    *value = v ? 1 : 0;
    return true;
  }
  return false;
}

void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool slider(const char* label, int* value, int lo, int hi) {
  if (ImGui::SliderInt(label, value, lo, hi, "%d%%")) {
    portSettingsApply();
  }
  return ImGui::IsItemDeactivatedAfterEdit();
}

// Controller 1's buttons (Aurora's mappings, saved by it per controller model).
void remapSection() {
  u32 count = 0;
  PADButtonMapping* maps = PADGetButtonMappings(0, &count);
  ImGui::Separator();
  ImGui::TextUnformatted("Controller buttons");
  if (maps == nullptr || count == 0) {
    sRebind = -1;
    note("Connect a controller to change which of its buttons does what.");
    return;
  }
  if (sRebind >= 0 && sRebind < static_cast<int>(count)) {
    const s32 pressed = PADGetNativeButtonPressed(0);
    if (pressed < 0) {
      sRebindArmed = true;
    } else if (sRebindArmed) {
      maps[sRebind].nativeButton = static_cast<u32>(pressed);
      PADSetButtonMapping(0, maps[sRebind]);
      PADSerializeMappings();
      sRebind = -1;
    }
  }
  for (u32 i = 0; i < count; i++) {
    ImGui::PushID(static_cast<int>(i));
    const char* native = sRebind == static_cast<int>(i) ? "Press a button..." : PADGetNativeButtonName(maps[i].nativeButton);
    if (ImGui::Button(native != nullptr ? native : "?", ImVec2(ImGui::GetFontSize() * 9.f, 0.f))) {
      sRebind = static_cast<int>(i);
      sRebindArmed = false;
    }
    ImGui::SameLine();
    ImGui::Text("GameCube %s", PADGetButtonName(maps[i].padButton));
    ImGui::PopID();
  }
  if (ImGui::Button("Swap A and B")) {
    PADButtonMapping* a = nullptr;
    PADButtonMapping* b = nullptr;
    for (u32 i = 0; i < count; i++) {
      if (maps[i].padButton == PAD_BUTTON_A) {
        a = &maps[i];
      } else if (maps[i].padButton == PAD_BUTTON_B) {
        b = &maps[i];
      }
    }
    if (a != nullptr && b != nullptr) {
      std::swap(a->nativeButton, b->nativeButton);
      PADSetButtonMapping(0, *a);
      PADSetButtonMapping(0, *b);
      PADSerializeMappings();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Restore defaults")) {
    PADRestoreDefaultMapping(0);
    PADSerializeMappings();
    sRebind = -1;
  }
  note("Choose a button on the left, then press the controller button that should do it.");
}

constexpr const char* kFfSpeeds[] = {"2x", "3x", "4x", "5x", "6x", "7x", "8x"};
constexpr const char* kTextSpeeds[] = {"Original", "Fast (2x)", "Faster (4x)", "Instant"};
constexpr const char* kLanguages[] = {"The computer's", "English", "Deutsch", "Français", "Español", "Italiano"};
constexpr const char* kRunSpeeds[] = {"Original", "125%", "150%"};

// Puts the last crash report on the clipboard; true if there was one.
bool copyCrashReport() {
  const char* path = portCrashReportLast();
  if (path == nullptr) {
    return false;
  }
  char* text = static_cast<char*>(SDL_LoadFile(path, nullptr));
  if (text == nullptr) {
    return false;
  }
  SDL_SetClipboardText(text);
  SDL_free(text);
  return true;
}

void crashSection() {
  ImGui::Separator();
  static bool sCopied;
  if (portCrashReportLast() == nullptr) {
    note("No crash reports.");
    return;
  }
  if (ImGui::Button("Copy the last crash report")) {
    sCopied = copyCrashReport();
  }
  if (sCopied) {
    ImGui::SameLine();
    ImGui::TextUnformatted("Copied");
  }
  note("last_crash.txt in the save folder; paste it to whoever is helping you.");
}

// The last run crashed: offered once at start.
void drawCrashNotice(float scale) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  static bool sCopied;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(display.x - 32.f, 460.f * scale), 0.f), ImGuiCond_Always);
  if (ImGui::Begin("The game crashed last time", nullptr,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::SetWindowFontScale(scale);
    noteWindow();
    ImGui::TextWrapped("A report of where it crashed was saved. Copy it and paste it to whoever is helping you.");
    if (ImGui::Button(sCopied ? "Copied" : "Copy report")) {
      sCopied = copyCrashReport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      portCrashReportDismiss();
    }
  }
  ImGui::End();
}

#if defined(PORT_CLOUD_SYNC)
// iCloud Drive (port/include/port/cloud.h)
void cloudSection(PortSettings* s) {
  if (check("Sync saves with iCloud Drive", &s->cloudSync)) {
    changed();
    if (s->cloudSync) {
      portSettingsCloudPush(0);
    }
  }
#if TARGET_OS_IPHONE
  note("Your saves are kept the same as in a folder in iCloud Drive (pick the TTYD folder the Mac version "
       "uses to share them). The newer copy of each save wins; replaced saves are kept in Backups.");
  if (ImGui::Button(portCloudAvailable() ? "Choose another iCloud Drive folder" : "Choose an iCloud Drive folder")) {
    portCloudPickFolder();
  }
  if (portCloudAvailable()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portCloudFolderName());
  }
#else
  note("Your saves are kept the same as in iCloud Drive's TTYD folder (choose that folder on an iPhone or iPad "
       "to share them). The newer copy of each save wins; replaced saves are kept in Backups.");
#endif
  if (s->cloudSync && portCloudStatus()[0] != '\0') {
    note(portCloudStatus());
  }
  note("Saves are brought from iCloud Drive when the game starts: restart it to get another device's progress.");
}
#endif

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
void openFolder(const std::string& dir) {
  SDL_CreateDirectory(dir.c_str());
  const std::string url = "file:///" + dir;
  SDL_OpenURL(url.c_str());
}
#endif

// Mods (port/include/port/mods.h): the mods folder's mods in order, the
// first winning; on/off and order are written to mods/mods.txt.
void modsTab(PortSettings* s) {
  note("A mod is a folder or a .ttydmod/.zip file with a mod.json, in the mods folder. "
       "The first in the list wins where mods change the same thing. Changes apply to what the game "
       "loads next (the next room or battle); textures change at once.");
  if (ImGui::Button("Reload")) {
    portModsReload();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::SameLine();
  if (ImGui::Button("Open the mods folder")) {
    openFolder(portModsDir());
  }
#endif
  const int count = portModsCount();
  if (count == 0) {
    note("No mods yet.");
  }
  for (int i = 0; i < count; i++) {
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
    ImGui::BeginDisabled(i == count - 1);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
      portModsMove(i, 1);
    }
    ImGui::EndDisabled();
    if (mod->error != nullptr) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
      ImGui::TextWrapped("%s %s", mod->path, mod->error);
      ImGui::PopStyleColor();
    } else {
      char line[256];
      std::snprintf(line, sizeof(line), "%s%s%s%d files, %d textures, %d messages, %d data changes",
                    mod->author[0] ? "by " : "", mod->author, mod->author[0] ? "  ·  " : "", mod->files, mod->textures,
                    mod->messages, mod->patches);
      note(line);
      if (mod->warning != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", mod->warning);
        ImGui::PopStyleColor();
      }
      switch (mod->code) {
      case PORT_MOD_CODE_RUNNING: note("Code: running."); break;
      case PORT_MOD_CODE_STARTS_NEXT_START: note("Code: starts the next time the game starts."); break;
      case PORT_MOD_CODE_STOPS_NEXT_START: note("Code: still running; it stops the next time the game starts."); break;
      case PORT_MOD_CODE_FAILED:
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        ImGui::TextWrapped("Code: %s", mod->codeError);
        ImGui::PopStyleColor();
        break;
      default: break;
      }
      for (int o = 0; o < mod->optionCount; o++) {
        const PortModOption* opt = portModsGetOption(i, o);
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
          edited = combo(opt->label, &value, opt->choices, opt->choiceCount);
        }
        if (edited) {
          portModsSetOption(i, o, value);
        }
        ImGui::PopID();
      }
      if (mod->description[0] != '\0') {
        note(mod->description);
      }
    }
    ImGui::PopID();
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Texture packs");
  note("A Dolphin texture pack works as it comes: put its files in the textures folder, "
       "or in a mod's textures folder.");
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (ImGui::Button("Open the textures folder")) {
    openFolder(std::string(portSettingsUserPath()) + "/textures");
  }
#endif
  if (check("Dump textures (from the next start)", &s->dumpTextures)) {
    changed();
  }
  note("Saves each texture that has no replacement yet, under the name a pack uses, "
       "to make or extend a pack.");
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (s->dumpTextures && ImGui::Button("Open the dump folder")) {
    openFolder(portModsDumpDir());
  }
#endif
}

void statesTab() {
  if (!portSaveStateSupported()) {
    note("Save states are not available in this build yet.");
    return;
  }
  note("A save state keeps the whole game as it is this moment, in the field or a battle. They last until the "
       "game is closed: use the game's own save blocks to keep your progress.");
  for (int i = 0; i < PORT_SAVE_STATE_SLOTS; i++) {
    ImGui::PushID(i);
    const char* info = portSaveStateInfo(i);
    ImGui::Text("Slot %d", i + 1);
    ImGui::SameLine(ImGui::GetFontSize() * 4.f);
    if (ImGui::Button("Save")) {
      portSaveStateRequest(i, 0);
      setOpen(false);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(info == nullptr);
    if (ImGui::Button("Load")) {
      portSaveStateRequest(i, 1);
      setOpen(false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", info != nullptr ? info : "empty");
    ImGui::PopID();
  }
  note("Keyboard: F5 saves to slot 1, F8 loads it.");
}

// The Action Command cue (port/src/common/ac_cue_pc.c): an A button low in
// the middle, a ring closing on it over the lead-in, lit when to press
void drawAcCue() {
  static int sPrevLead = 99;
  if (gPortAcCueState == 0) {
    sPrevLead = 99;
    return;
  }
  const int lead = gPortAcCueLead;
  if (lead <= 0 && sPrevLead > 0 && portSettings()->acCue == 2) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
    portIosHapticTap();
#endif
  }
  sPrevLead = lead;
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float u = std::min(display.x, display.y) / 16.f; // the button's radius
  const ImVec2 c(display.x * 0.5f, display.y * 0.78f);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const bool now = lead <= 0;
  const ImU32 green = now ? IM_COL32(80, 230, 110, 245) : IM_COL32(40, 170, 80, 200);
  dl->AddCircleFilled(c, u, green, 40);
  dl->AddCircle(c, u, IM_COL32(255, 255, 255, 220), 40, u * 0.08f);
  const char* label = "A";
  ImFont* font = ImGui::GetFont();
  const float size = u * 1.1f;
  const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, label);
  dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), label);
  if (!now) {
    const float t = std::min(1.f, static_cast<float>(lead) / 24.f); // 1 at the lead-in's start
    dl->AddCircle(c, u * (1.f + 2.f * t), IM_COL32(255, 255, 255, static_cast<int>(120 + 120 * (1.f - t))), 48,
                  u * 0.14f);
  } else {
    dl->AddCircle(c, u * 1.25f, IM_COL32(255, 255, 150, 230), 48, u * 0.2f);
  }
}

// The game's ROM-font lines this frame (port/include/port/romfont_pc.h: the
// disc errors and fatal error text, without the console's IPL font), in
// Fredoka where the game put them: romfont.c's units are lines of a
// 480-line picture from its centre, y up, a line's cell 24 high.
float measureRomText(const char* utf8) {
  ImFont* font = gLauncherFonts.body != nullptr ? gLauncherFonts.body : ImGui::GetFont();
  return font->CalcTextSizeA(24.f, FLT_MAX, 0.f, utf8).x;
}

void drawRomFont() {
  gPortRomFontMeasure = measureRomText;
  PortLayout layout;
  if (portRomFontCount() == 0 || !portLayoutGet(&layout) || layout.picH <= 0.f) {
    portRomFontClear();
    return;
  }
  ImFont* font = gLauncherFonts.body != nullptr ? gLauncherFonts.body : ImGui::GetFont();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float u = layout.picH / 480.f; // window points per unit
  const float cx = layout.picX + layout.picW * 0.5f, cy = layout.picY + layout.picH * 0.5f;
  for (int i = 0; i < portRomFontCount(); i++) {
    const PortRomText* t = portRomFontAt(i);
    const float size = 24.f * t->scale * u;
    // (x, y) is the first line's bottom-left; its top is a cell above
    const ImVec2 at(cx + t->x * u, cy - (t->y + 24.f * t->scale) * u);
    dl->AddText(font, size, ImVec2(at.x + size * 0.06f, at.y + size * 0.06f), IM_COL32(0, 0, 0, 160), t->text);
    dl->AddText(font, size, at, IM_COL32(t->rgba[0], t->rgba[1], t->rgba[2], t->rgba[3]), t->text);
  }
  portRomFontClear();
}

void drawStateStatus() {
  const char* text = portSaveStateStatus();
  if (text == nullptr) {
    text = portShotStatus();
  }
  if (text == nullptr) {
    return;
  }
  const ImVec2 size = ImGui::CalcTextSize(text);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  float top = 0.f; // below the notch or Dynamic Island
  SDL_Rect safe{};
  if (SDL_Window* w = aurora_get_window(); w != nullptr && SDL_GetWindowSafeArea(w, &safe) && safe.h > 0) {
    top = static_cast<float>(safe.y);
  }
  const ImVec2 at((display.x - size.x) * 0.5f, top + 12.f);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(ImVec2(at.x - 8, at.y - 4), ImVec2(at.x + size.x + 8, at.y + size.y + 4), IM_COL32(0, 0, 0, 170), 6.f);
  dl->AddText(at, IM_COL32(255, 255, 255, 240), text);
}

// Save warps: the .gci files in the user folder's "warps" folder (and its
// folders), any save of this version (e.g. a practice save pack).
std::vector<std::string> sWarps; // relative paths, sorted
bool sWarpsRead;

std::string warpDir() { return std::string(portSettingsUserPath()) + "/warps"; }

void readWarps() {
  sWarps.clear();
  sWarpsRead = true;
  const std::string dir = warpDir();
  SDL_CreateDirectory(dir.c_str());
  int count = 0;
  char** found = SDL_GlobDirectory(dir.c_str(), "*.gci", SDL_GLOB_CASEINSENSITIVE, &count);
  if (found != nullptr) {
    for (int i = 0; i < count; i++) {
      sWarps.emplace_back(found[i]);
    }
    SDL_free(found);
  }
  for (const char* pattern : {"*/*.gci", "*/*/*.gci"}) {
    found = SDL_GlobDirectory(dir.c_str(), pattern, SDL_GLOB_CASEINSENSITIVE, &count);
    if (found != nullptr) {
      for (int i = 0; i < count; i++) {
        sWarps.emplace_back(found[i]);
      }
      SDL_free(found);
    }
  }
  std::sort(sWarps.begin(), sWarps.end());
  sWarps.erase(std::unique(sWarps.begin(), sWarps.end()), sWarps.end());
}

void warpTab() {
  if (!sWarpsRead) {
    readWarps();
  }
  note("Continue from any save file (.gci) in the warps folder; folders in it group them. "
       "A save made after a warp goes to file 4.");
  const char* status = portWarpStatus();
  if (status != nullptr && status[0] != '\0') {
    ImGui::TextUnformatted(status);
  }
  if (ImGui::Button("Refresh")) {
    readWarps();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::SameLine();
  if (ImGui::Button("Open the warps folder")) {
    const std::string url = "file:///" + warpDir();
    SDL_OpenURL(url.c_str());
  }
#endif
  if (sWarps.empty()) {
    note("No saves yet: put .gci files in the warps folder (in the save folder).");
    return;
  }
  ImGui::BeginChild("warps", ImVec2(0, 0), ImGuiChildFlags_Borders);
  std::string open; // the folder whose tree node is open, "" at the top
  bool shown = true;
  for (const std::string& rel : sWarps) {
    std::string norm = rel;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    const size_t slash = norm.rfind('/');
    const std::string folder = slash == std::string::npos ? "" : norm.substr(0, slash);
    std::string name = slash == std::string::npos ? norm : norm.substr(slash + 1);
    if (name.size() > 4) {
      name.resize(name.size() - 4); // ".gci"
    }
    if (folder != open) {
      if (!open.empty() && shown) {
        ImGui::TreePop();
      }
      open = folder;
      shown = folder.empty() || ImGui::TreeNode(folder.c_str());
    }
    if (shown && ImGui::Selectable((name + "##" + norm).c_str())) {
      portWarpRequestSave((warpDir() + "/" + rel).c_str());
      setOpen(false);
    }
  }
  if (!open.empty() && shown) {
    ImGui::TreePop();
  }
  ImGui::EndChild();
}

// the time as the system shows it (12/24-hour), e.g. "9:41 PM"
void clockText(char* out, size_t size) {
#if defined(__APPLE__)
  static CFDateFormatterRef sFormatter;
  if (sFormatter == nullptr) {
    CFLocaleRef locale = CFLocaleCopyCurrent();
    sFormatter = CFDateFormatterCreate(nullptr, locale, kCFDateFormatterNoStyle, kCFDateFormatterShortStyle);
    CFRelease(locale);
  }
  CFStringRef str = CFDateFormatterCreateStringWithAbsoluteTime(nullptr, sFormatter, CFAbsoluteTimeGetCurrent());
  if (str != nullptr && CFStringGetCString(str, out, static_cast<CFIndex>(size), kCFStringEncodingUTF8)) {
    CFRelease(str);
    // the formatter's narrow/no-break spaces ("9:41\u202fPM"): plain ones, which the font has
    std::string t(out);
    for (const char* sp : {"\xE2\x80\xAF", "\xC2\xA0"}) {
      for (size_t at; (at = t.find(sp)) != std::string::npos;) {
        t.replace(at, std::strlen(sp), " ");
      }
    }
    std::snprintf(out, size, "%s", t.c_str());
    return;
  }
  if (str != nullptr) {
    CFRelease(str);
  }
#endif
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  std::strftime(out, size, "%H:%M", &tm);
}

void drawFps(int corner) {
  const PortSettings* s = portSettings();
  char text[96] = "";
  size_t len = 0;
  const auto add = [&](const char* part) {
    len += static_cast<size_t>(std::snprintf(text + len, sizeof(text) - len, "%s%s", len != 0 ? "   " : "", part));
    len = len < sizeof(text) ? len : sizeof(text) - 1;
  };
  char part[48];
  if (s->overlayClock) {
    clockText(part, sizeof(part));
    add(part);
  }
  if (s->overlayBattery) {
    int percent = -1;
    const SDL_PowerState state = SDL_GetPowerInfo(nullptr, &percent);
    if (percent >= 0 && state != SDL_POWERSTATE_NO_BATTERY && state != SDL_POWERSTATE_UNKNOWN) {
      std::snprintf(part, sizeof(part), "%d%%%s", percent,
                    state == SDL_POWERSTATE_CHARGING || state == SDL_POWERSTATE_CHARGED ? " +" : "");
      add(part);
    }
  }
  if (s->overlayFps || len == 0) {
    std::snprintf(part, sizeof(part), "%.0f FPS", aurora_get_fps());
    add(part);
  }
  const ImVec2 size = ImGui::CalcTextSize(text);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  // inside the window's safe area (notch, Dynamic Island, home indicator), and
  // clear of a phone's rounded corners
  float left = 0.f, top = 0.f, right = display.x, bottom = display.y;
  SDL_Rect safe{};
  if (SDL_Window* w = aurora_get_window(); w != nullptr && SDL_GetWindowSafeArea(w, &safe) && safe.w > 0 && safe.h > 0) {
    left = static_cast<float>(safe.x);
    top = static_cast<float>(safe.y);
    right = static_cast<float>(safe.x + safe.w);
    bottom = static_cast<float>(safe.y + safe.h);
  }
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  const float pad = 24.f;
#else
  const float pad = 8.f;
#endif
  const ImVec2 at(corner == 1 || corner == 3 ? left + pad : right - size.x - pad,
                  corner <= 2 ? top + pad : bottom - size.y - pad);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(ImVec2(at.x - 4, at.y - 2), ImVec2(at.x + size.x + 4, at.y + size.y + 2), IM_COL32(0, 0, 0, 140), 4.f);
  dl->AddText(at, IM_COL32(255, 255, 255, 230), text);
}


// Archipelago (port/archipelago/ttyd_ap.h): the seed, applied at start,
// and the multiworld server's address, slot and password.
void seedChosen(void*, const char* const* files, int) {
  if (files != nullptr && files[0] != nullptr) {
    portApSetSeed(files[0]);
  }
}

void archipelagoTab() {
  static char server[128], slot[64], password[64];
  static bool filled;
  if (!filled) {
    filled = true;
    snprintf(slot, sizeof(slot), "%s", portApSlotName());
  }
  note("A multiworld randomizer: the TTYD apworld's seed (an .apttyd file) and a server. The seed applies at the "
       "game's start; its randomizer mod, the TTYD AP mod, comes as a code mod.");
  ImGui::TextWrapped("Status: %s", portApStatus());
  ImGui::Separator();
  ImGui::TextWrapped("Seed: %s", portApSeedPath()[0] != '\0' ? portApSeedPath() : "(none)");
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (ImGui::Button("Choose a seed...")) {
    static const SDL_DialogFileFilter kFilter[] = {{"Archipelago seed", "apttyd"}};
    SDL_ShowOpenFileDialog(seedChosen, nullptr, SDL_GetKeyboardFocus(), kFilter, 1, nullptr, false);
  }
  ImGui::SameLine();
#endif
  note("A new seed applies at the next start.");
  ImGui::InputText("Server", server, sizeof(server));
  ImGui::InputText("Slot", slot, sizeof(slot));
  ImGui::InputText("Password", password, sizeof(password), ImGuiInputTextFlags_Password);
  if (ImGui::Button("Connect")) {
    portApConnect(server, slot, password);
  }
  ImGui::SameLine();
  if (ImGui::Button("Disconnect")) {
    portApDisconnect();
  }
}

// --- The settings (v2): each one an item with its tab, label, explanation
// and markers, drawn by the page and found by its search. Sections with
// several controls (button remapping, mods, warps...) are items too.

enum ItemFlags : unsigned {
  kNextStart = 1u,    // takes effect the next time the game starts
  kChangesPlay = 2u,  // changes how the game plays
  kChangesSaves = 4u, // changes what goes in a save
  kInGame = 8u,       // only with a game running (save states, warps)
};

struct Item;
using DrawFn = void (*)(PortSettings* s, const Item& it);

struct Item {
  const char* tab;
  const char* label;    // the control's label, and what search matches first
  const char* note;     // the explanation under it, or nullptr
  unsigned flags;
  DrawFn draw;
  const char* keywords; // more words search finds it by, or nullptr
  bool (*shown)(const PortSettings* s); // nullptr: always
  const void* data = nullptr;           // a feature's control: its Extra (below)
};

// The tabs in order; a feature's own tab (portSettingsAdd*) comes after these.
constexpr const char* kTabs[] = {"Display", "Audio", "Controls", "Gameplay", "Accessibility", "Practice",
                                 "Mods", "Archipelago", "System"};

bool isDesktop() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  return false;
#else
  return true;
#endif
}

void videoRate(PortSettings* s, const Item& it) {
  int rate = 0;
  for (int i = 0; i < 5; i++) {
    if (std::strcmp(s->framerate, kRates[i]) == 0) {
      rate = i;
    }
  }
  if (combo(it.label, &rate, kRateNames, 5)) {
    std::snprintf(s->framerate, sizeof(s->framerate), "%s", kRates[rate]);
    changed();
  }
}

void resolution(PortSettings* s, const Item& it) {
  char names[9][32];
  const char* namep[9];
  std::snprintf(names[0], sizeof(names[0]), "Auto (the window's size)");
  for (int i = 1; i <= 8; i++) {
    std::snprintf(names[i], sizeof(names[i]), "%dx (%dx%d)", i, 640 * i, 480 * i);
  }
  for (int i = 0; i < 9; i++) {
    namep[i] = names[i];
  }
  if (combo(it.label, &s->resolution, namep, 9)) {
    changed();
  }
}

void overlay(PortSettings* s, const Item& it) {
  if (combo(it.label, &s->fps, kCorners, 5)) {
    changed();
  }
  if (s->fps != 0) {
    if (check("Frame rate", &s->overlayFps)) {
      changed();
    }
    ImGui::SameLine();
    if (check("Clock", &s->overlayClock)) {
      changed();
    }
    ImGui::SameLine();
    if (check("Battery", &s->overlayBattery)) {
      changed();
    }
  }
}

// a toggle bound to a PortSettings field, by offset
#define TOGGLE(field)                                                                                              \
  [](PortSettings* s, const Item& it) {                                                                            \
    if (check(it.label, &s->field)) {                                                                              \
      changed();                                                                                                   \
    }                                                                                                              \
  }
#define CHOICE(field, names, count)                                                                                \
  [](PortSettings* s, const Item& it) {                                                                            \
    if (combo(it.label, &s->field, names, count)) {                                                                \
      changed();                                                                                                   \
    }                                                                                                              \
  }
#define PERCENT(field, lo, hi)                                                                                     \
  [](PortSettings* s, const Item& it) {                                                                            \
    if (slider(it.label, &s->field, lo, hi)) {                                                                     \
      changed();                                                                                                   \
    }                                                                                                              \
  }

constexpr const char* kFilters[] = {"Original (smooth)", "Sharp (pixels)"};
constexpr const char* kOrientations[] = {"Turn with the device", "Landscape", "Portrait"};
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr const char* kCues[] = {"Off", "On screen", "On screen and a tap"};
constexpr int kCueCount = 3;
#else
constexpr const char* kCues[] = {"Off", "On screen"};
constexpr int kCueCount = 2;
#endif

bool widescreenOn(const PortSettings* s) { return s->widescreen != 0; }
bool isEurope(const PortSettings*) { return std::strcmp(PORT_GAME_ID, "G8MP01") == 0; }

const Item kItems[] = {
    // Display
    {"Display", "Frame rate",
     "The game runs at 60 frames a second; higher rates draw frames in between (smooth camera and characters).", 0,
     videoRate, "fps hz 120 144 240 smooth interpolation", nullptr},
    {"Display", "VSync", nullptr, 0, TOGGLE(vsync), "tearing", nullptr},
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Display", "Fullscreen (F11)", nullptr, 0, TOGGLE(fullscreen), "window", nullptr},
#endif
    {"Display", "Widescreen (16:9)", "Much of the game was built for 4:3: some rooms and scenes end at the 4:3 edges.",
     0, TOGGLE(widescreen), "aspect ratio 16:9", nullptr},
    {"Display", "Widescreen HUD",
     "HP and FP at the left edge, Star Points and coins at the right, instead of the 4:3 middle.", 0,
     TOGGLE(wideHud), "status bar", widescreenOn},
    {"Display", "Blurred side bars", "Where the picture doesn't fill the screen, a blurred copy of it instead of black bars.",
     0, TOGGLE(sideBars), "pillarbox black bars", nullptr},
    {"Display", "Internal resolution", nullptr, 0, resolution, "resolution scale sharp 4k", nullptr},
    {"Display", "Texture filtering", nullptr, 0, CHOICE(texFilter, kFilters, 2), "pixels smooth sharp", nullptr},
    {"Display", "Anti-aliasing (4x MSAA)", "Smooths jagged edges.", kNextStart, TOGGLE(msaa), "msaa jaggies edges",
     nullptr},
    {"Display", "Overlay corner", "Frame rate, clock and battery in a corner of the screen.", 0, overlay,
     "fps counter clock battery", nullptr},
    {"Display", "Never skip graphics that are still loading",
     "Something drawn for the first time waits for its graphics to be ready (a short pause, once) instead of "
     "appearing a moment later.",
     0, TOGGLE(noPopIn), "pop-in stutter shaders", nullptr},
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Display", "Orientation", "Held upright, the picture is at the top and the controls below it.", 0,
     CHOICE(orientation, kOrientations, 3), "portrait landscape rotate", nullptr},
    {"Display", "Keep the picture clear of the notch", nullptr, 0, TOGGLE(safeArea), "safe area island", nullptr},
#endif

    // Audio
    {"Audio", "Volume", nullptr, 0,
     [](PortSettings* s, const Item& it) {
       if (ImGui::SliderInt(it.label, &s->volume, 0, 100, "%d%%")) {
         portSettingsApply();
       }
       if (ImGui::IsItemDeactivatedAfterEdit()) {
         changed();
       }
     },
     "sound music loud", nullptr},
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Audio", "Mute while in the background", nullptr, 0, TOGGLE(muteUnfocused), "sound focus", nullptr},
#endif

    // Controls
    {"Controls", "Rumble", nullptr, 0, TOGGLE(rumble), "vibration", nullptr},
    {"Controls", "Rumble strength", "The game's own Rumble option (on the file select) still applies.", 0,
     PERCENT(rumbleStrength, 0, 100), "vibration", nullptr},
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Controls", "Vibrate the device without a controller", nullptr, 0, TOGGLE(phoneRumble), "haptics", nullptr},
    {"Controls", "Haptic tap on button press", nullptr, 0, TOGGLE(touchHaptics), "touch on-screen", nullptr},
    {"Controls", "Opacity", nullptr, 0, PERCENT(touchOpacity, 20, 100), "touch on-screen controls", nullptr},
    {"Controls", "Size", nullptr, 0, PERCENT(touchSize, 70, 150), "touch on-screen controls", nullptr},
    {"Controls", "Auto-run (stick)", nullptr, 0, TOGGLE(autoRun), "touch stick", nullptr},
    {"Controls", "Fixed stick (instead of where the finger lands)", nullptr, 0, TOGGLE(stickFixed), "touch", nullptr},
    {"Controls", "Tap the picture to press A", nullptr, 0, TOGGLE(tapA), "touch", nullptr},
    {"Controls", "Move the on-screen controls",
     "Drag them where you like, then Done; held upright and sideways each keep their own. They hide while a "
     "controller is used and come back when the screen is touched.",
     kInGame,
     [](PortSettings*, const Item& it) {
       if (ImGui::Button(it.label)) {
         setOpen(false);
         portTouchEditLayout();
       }
     },
     "touch layout position", nullptr},
#endif
    {"Controls", "Controller buttons", nullptr, 0, [](PortSettings*, const Item&) { remapSection(); },
     "remap rebind buttons swap a b mapping gamepad", nullptr},

    // Gameplay
    {"Gameplay", "Action Command cue",
     "In battle, a ring closes on an A button when to press A for a Jump (and partners' stomps) and to guard an "
     "enemy jumping at you. Other commands show their own timing.",
     0,
     [](PortSettings* s, const Item& it) {
       s->acCue = s->acCue >= kCueCount ? kCueCount - 1 : s->acCue;
       if (combo(it.label, &s->acCue, kCues, kCueCount)) {
         changed();
       }
     },
     "timing guard superguard battle", nullptr},
    {"Gameplay", "Fast-forward speed", "Fast-forward: Tab, a controller's right stick click, or the on-screen >> button.",
     0,
     [](PortSettings* s, const Item& it) {
       int ff = s->ffSpeed - 2;
       if (combo(it.label, &ff, kFfSpeeds, 7)) {
         s->ffSpeed = ff + 2;
         changed();
       }
     },
     "speed turbo", nullptr},
    {"Gameplay", "Fast-forward toggles (instead of held)", nullptr, 0, TOGGLE(ffToggle), "turbo", nullptr},
    {"Gameplay", "Text speed", nullptr, 0, CHOICE(textSpeed, kTextSpeeds, 4), "dialogue messages", nullptr},
    {"Gameplay", "Run speed", "Faster running can make some jumps longer than intended.", kChangesPlay,
     [](PortSettings* s, const Item& it) {
       int run = (s->runSpeed - 100) / 25;
       if (combo(it.label, &run, kRunSpeeds, 3)) {
         s->runSpeed = 100 + run * 25;
         changed();
       }
     },
     "walk move", nullptr},
    {"Gameplay", "Skip the intro",
     "The logos go by quickly and the story opening is skipped (it still plays from the title screen if left "
     "alone).",
     0, TOGGLE(skipIntro), "logos opening", nullptr},
    {"Gameplay", "Language", "The game's text.", kNextStart, CHOICE(language, kLanguages, 6),
     "english deutsch francais espanol italiano text", isEurope},
    {"Gameplay", "Run at 50 Hz (PAL)", "As on a PAL TV: the game's 50 Hz mode, a little slower. 60 Hz otherwise.",
     kNextStart | kChangesPlay, TOGGLE(pal50Hz), "pal 60 hz eurgb60 refresh", isEurope},

    // Practice
    {"Practice", "Save states", nullptr, kInGame, [](PortSettings*, const Item&) { statesTab(); },
     "states slots f5 f8 load", nullptr},
    {"Practice", "Save warps", nullptr, kInGame, [](PortSettings*, const Item&) { warpTab(); },
     "warp gci practice saves", nullptr},

    // Mods
    {"Mods", "Mods and texture packs", nullptr, 0, [](PortSettings* s, const Item&) { modsTab(s); },
     "mods textures packs dump reload folder", nullptr},

    // Archipelago
    {"Archipelago", "Archipelago", nullptr, 0, [](PortSettings*, const Item&) { archipelagoTab(); },
     "randomizer multiworld seed server", nullptr},

    // System
    {"System", "Skip the launcher (start with the last disc played)", nullptr, 0, TOGGLE(launcherSkip),
     "launcher startup disc", nullptr},
    {"System", "Take a screenshot",
#if defined(__APPLE__) && TARGET_OS_IPHONE
     "Saved to Photos.",
#elif defined(__APPLE__)
     "Saved to Pictures/TTYD. Keyboard: F12.",
#else
     "Saved to the Screenshots folder by the saves. Keyboard: F12.",
#endif
     kInGame,
     [](PortSettings*, const Item& it) {
       if (ImGui::Button(it.label)) {
         setOpen(false);
         portShotRequestUser(); // the next frame, without this page
       }
     },
     "screenshot picture f12 capture", nullptr},
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    {"System", "Pause while in the background", nullptr, 0, TOGGLE(pauseUnfocused), "focus", nullptr},
    {"System", "Controllers work in the background", nullptr, 0, TOGGLE(backgroundInput), "focus gamepad", nullptr},
    {"System", "Open the save folder", "Memory cards (by region) and settings.txt.", 0,
     [](PortSettings*, const Item& it) {
       if (ImGui::Button(it.label)) {
         char url[1200];
         std::snprintf(url, sizeof(url), "file:///%s", portSettingsUserPath());
         SDL_OpenURL(url);
       }
     },
     "saves memory card gci folder", nullptr},
#else
    {"System", "Saves", "Saves are in the app's folder (Files app / storage).", 0, nullptr, "memory card gci",
     nullptr},
#endif
#if defined(PORT_CLOUD_SYNC)
    {"System", "Sync saves with iCloud Drive", nullptr, 0, [](PortSettings* s, const Item&) { cloudSection(s); },
     "icloud cloud sync", nullptr},
#endif
    {"System", "Crash reports", nullptr, 0, [](PortSettings*, const Item&) { crashSection(); }, "crash report bug",
     nullptr},
};

// --- Features' own controls (portSettingsAdd*), for settings they declared
// (portSettingsDeclare): kept here and drawn after the fixed ones of their tab.
struct Extra {
  const char* key;
  int kind; // 0 toggle, 1 choice, 2 slider, 3 a section the feature draws
  const char* const* choices;
  int count, min, max;
  const char* format;
  void (*draw)(void) = nullptr; // a section's
};

std::deque<Extra>& extras() {
  static std::deque<Extra> sExtras; // stable addresses: the items point at them
  return sExtras;
}

std::vector<Item>& extraItems() {
  static std::vector<Item> sItems;
  return sItems;
}

void drawExtra(PortSettings*, const Item& it) {
  const Extra& e = *static_cast<const Extra*>(it.data);
  if (e.kind == 3) {
    ImGui::TextUnformatted(it.label);
    e.draw();
    return;
  }
  int* v = portSettingsFind(e.key);
  if (v == nullptr) {
    ImGui::TextDisabled("%s (not declared: %s)", it.label, e.key);
    return;
  }
  bool edited = false;
  if (e.kind == 0) {
    edited = check(it.label, v);
  } else if (e.kind == 1) {
    edited = combo(it.label, v, e.choices, e.count);
  } else {
    ImGui::SliderInt(it.label, v, e.min, e.max, e.format != nullptr ? e.format : "%d");
    edited = ImGui::IsItemDeactivatedAfterEdit();
  }
  if (edited) {
    changed();
  }
}

void addExtra(const char* tab, const char* label, const char* note, unsigned flags, const char* keywords,
              const Extra& e) {
  extras().push_back(e);
  Item it{tab, label, note, flags, drawExtra, keywords, nullptr};
  it.data = &extras().back();
  extraItems().push_back(it);
}

// every item: the fixed ones, then the features'
std::vector<const Item*> allItems() {
  std::vector<const Item*> v;
  for (const Item& it : kItems) {
    v.push_back(&it);
  }
  for (const Item& it : extraItems()) {
    v.push_back(&it);
  }
  return v;
}

// the tabs: the fixed ones, then any a feature named
std::vector<std::string> allTabs() {
  std::vector<std::string> v(std::begin(kTabs), std::end(kTabs));
  for (const Item& it : extraItems()) {
    if (std::find(v.begin(), v.end(), it.tab) == v.end()) {
      v.push_back(it.tab);
    }
  }
  return v;
}

// a coloured marker after a setting (next start, changes play, changes saves)
void marker(const char* text, ImU32 bg) {
  ImGui::SameLine();
  const ImVec2 size = ImGui::CalcTextSize(text);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float padX = ImGui::GetFontSize() * 0.4f, padY = ImGui::GetFontSize() * 0.12f;
  const float y = at.y + (ImGui::GetFrameHeight() - size.y) * 0.5f;
  ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, y - padY), ImVec2(at.x + size.x + padX * 2, y + size.y + padY),
                                            bg, size.y);
  ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + padX, y), IM_COL32(255, 255, 255, 255), text);
  ImGui::Dummy(ImVec2(size.x + padX * 2, ImGui::GetFrameHeight()));
}

void drawItem(PortSettings* s, const Item& it) {
  ImGui::PushID(it.label);
  if (it.draw != nullptr) {
    it.draw(s, it);
  } else {
    ImGui::TextUnformatted(it.label);
  }
  // markers after a one-line control (sections draw their own lines)
  if (it.flags & kNextStart) {
    marker("next start", IM_COL32(52, 104, 178, 255));
  }
  if (it.flags & kChangesPlay) {
    marker("changes play", IM_COL32(206, 112, 22, 255));
  }
  if (it.flags & kChangesSaves) {
    marker("changes saves", IM_COL32(196, 32, 42, 255));
  }
  if (it.note != nullptr) {
    note(it.note);
  }
  ImGui::Spacing();
  ImGui::PopID();
}

bool visible(const PortSettings* s, const Item& it, bool inGame) {
  return (inGame || !(it.flags & kInGame)) && (it.shown == nullptr || it.shown(s));
}

// case-insensitive: every word of query is in the item's words
bool matches(const Item& it, const char* query) {
  std::string hay = std::string(it.tab) + " " + it.label + " " + (it.note ? it.note : "") + " " +
                    (it.keywords ? it.keywords : "");
  std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return std::tolower(c); });
  std::string q = query;
  std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });
  size_t at = 0;
  while (at < q.size()) {
    while (at < q.size() && q[at] == ' ') {
      at++;
    }
    const size_t end = std::min(q.find(' ', at), q.size());
    if (end > at && hay.find(q.substr(at, end - at)) == std::string::npos) {
      return false;
    }
    at = end;
  }
  return true;
}

char sSearch[64];

} // namespace

void portSettingsTabs(bool inGame) {
  PortSettings* s = portSettings();
  ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 18.f));
  ImGui::InputTextWithHint("##search", "Search settings", sSearch, sizeof(sSearch));
  if (sSearch[0] != '\0') {
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      sSearch[0] = '\0';
    }
  }
  ImGui::Spacing();
  if (sSearch[0] != '\0') {
    // the results, under the names of their tabs
    ImGui::BeginChild("results", ImVec2(0, 0));
    ImGui::PushItemWidth(ImGui::GetFontSize() * 16.f);
    int found = 0;
    const std::vector<const Item*> items = allItems();
    for (const std::string& tab : allTabs()) {
      bool header = false;
      for (const Item* it : items) {
        if (tab != it->tab || !visible(s, *it, inGame) || !matches(*it, sSearch)) {
          continue;
        }
        if (!header) {
          ImGui::SeparatorText(tab.c_str());
          header = true;
        }
        drawItem(s, *it);
        found++;
      }
    }
    if (found == 0) {
      note("No setting matches.");
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    return;
  }
  // the tabs down the side (there are many), the chosen one's settings beside them
  const std::vector<const Item*> items = allItems();
  std::vector<std::string> tabs;
  for (const std::string& tab : allTabs()) {
    for (const Item* it : items) {
      if (tab == it->tab && visible(s, *it, inGame)) {
        tabs.push_back(tab); // e.g. not Practice before a game runs
        break;
      }
    }
  }
  static std::string sTab;
  if (sTab.empty()) {
    const char* want = SDL_getenv("TTYD_SETTINGS_TAB"); // tests: the tab shown first
    sTab = want != nullptr ? want : "Display";
  }
  if (tabs.empty()) {
    return;
  }
  if (std::find(tabs.begin(), tabs.end(), sTab) == tabs.end()) {
    sTab = tabs.front();
  }
  ImGui::BeginChild("tabs", ImVec2(ImGui::GetFontSize() * 8.f, 0));
  for (const std::string& tab : tabs) {
    if (ImGui::Selectable(tab.c_str(), tab == sTab)) {
      sTab = tab;
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("tab", ImVec2(0, 0));
  ImGui::PushItemWidth(ImGui::GetFontSize() * 14.f); // controls a readable width, labels beside them
  for (const Item* it : items) {
    if (sTab == it->tab && visible(s, *it, inGame)) {
      drawItem(s, *it);
    }
  }
  ImGui::PopItemWidth();
  ImGui::EndChild();
}

extern "C" void portSettingsAddToggle(const char* tab, const char* label, const char* note, unsigned flags,
                                      const char* key, const char* keywords) {
  addExtra(tab, label, note, flags, keywords, Extra{key, 0, nullptr, 0, 0, 0, nullptr});
}

extern "C" void portSettingsAddChoice(const char* tab, const char* label, const char* note, unsigned flags,
                                      const char* key, const char* const* choices, int count, const char* keywords) {
  addExtra(tab, label, note, flags, keywords, Extra{key, 1, choices, count, 0, 0, nullptr});
}

extern "C" void portSettingsAddSlider(const char* tab, const char* label, const char* note, unsigned flags,
                                      const char* key, int min, int max, const char* format, const char* keywords) {
  addExtra(tab, label, note, flags, keywords, Extra{key, 2, nullptr, 0, min, max, format});
}

extern "C" void portSettingsAddSection(const char* tab, const char* label, const char* note, unsigned flags,
                                       void (*draw)(void), const char* keywords) {
  addExtra(tab, label, note, flags, keywords, Extra{nullptr, 3, nullptr, 0, 0, 0, nullptr, draw});
}


// A touch at window point (x, y) is on the settings page or the crash notice.
extern "C" int portSettingsOwnsPoint(float x, float y) {
  for (int i = 0; i < sWinCount; i++) {
    if (x >= sWinMin[i].x && x < sWinMax[i].x && y >= sWinMin[i].y && y < sWinMax[i].y) {
      return 1;
    }
  }
  return 0;
}

extern "C" void portSettingsEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  PortSettings* s = portSettings();
  switch (e->type) {
  case SDL_EVENT_DID_ENTER_BACKGROUND:
  case SDL_EVENT_TERMINATING:
  case SDL_EVENT_QUIT:
    portSettingsCloudPush(1); // the app may be suspended or gone next
    break;
  case SDL_EVENT_KEY_DOWN:
    if (e->key.repeat) {
      break;
    }
    if (e->key.key == SDLK_TAB) {
      portSpeedControl(1);
    } else if (e->key.key == SDLK_F5) {
      portSaveStateRequest(0, 0);
    } else if (e->key.key == SDLK_F8) {
      portSaveStateRequest(0, 1);
    } else if (e->key.key == SDLK_F12) {
      portShotRequestUser();
    } else if (e->key.key == SDLK_F1) {
      setOpen(!sOpen);
    } else if (e->key.key == SDLK_ESCAPE && sOpen) {
      setOpen(false);
    } else if (e->key.key == SDLK_F11) {
      s->fullscreen = !s->fullscreen;
      portSettingsSave();
    }
    break;
  case SDL_EVENT_KEY_UP:
    if (e->key.key == SDLK_TAB) {
      portSpeedControl(0);
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
      portSpeedControl(0);
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
      portSpeedControl(1);
    } else if (e->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
      setOpen(!sOpen);
    } else if (e->gbutton.button == SDL_GAMEPAD_BUTTON_EAST && sOpen && sRebind < 0 &&
               !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
      setOpen(false); // B: back out of the page (ImGui closes an open list itself)
    }
    break;
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    portSettingsFocus(1);
    break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    portSettingsFocus(0);
    break;
  case SDL_EVENT_FINGER_DOWN:
    sTouchDevice = true;
    if (sFingers == 0) {
      sFirstDown = SDL_GetTicks();
      sTap = true;
    }
    sFingers++;
    if (sFingers == 3) {
      sThirdDown = SDL_GetTicks();
      sTap = sTap && sThirdDown - sFirstDown <= 180;
    } else if (sFingers > 3) {
      sTap = false;
    }
    break;
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    if (sFingers > 0 && --sFingers == 0) {
      if (sTap && sThirdDown != 0 && SDL_GetTicks() - sFirstDown <= 550 && e->type == SDL_EVENT_FINGER_UP) {
        setOpen(!sOpen);
      }
      sTap = false;
      sThirdDown = 0;
    }
    break;
  default:
    break;
  }
}

extern "C" void portSettingsDraw(void) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  PortSettings* s = portSettings();
  static bool sChecked;
  sWinCount = 0;
  if (!sChecked) {
    sChecked = true;
    if (SDL_getenv("TTYD_SETTINGS_OPEN") != nullptr) { // tests: the page open from the start
      setOpen(true);
    }
  }
  if (s->fps != 0) {
    drawFps(s->fps);
  }
  drawRomFont();
  drawStateStatus();
  drawAcCue();
  portSettingsCloudTick();
  if (portCrashReportPending() && !sOpen) {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    drawCrashNotice(1.6f);
#else
    drawCrashNotice(sTouchDevice ? 1.6f : 1.0f);
#endif
  }
  if (!sOpen) {
    sWasOpen = false;
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  const ImVec2 display = io.DisplaySize;
  // the page is a paper card in the launcher's style (launcher_style.cpp), growing with the window
  const float scale = launcherUiScale(sTouchDevice ? 1.6f : 1.f);
  launcherStyleInit(scale);
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  // most of the window: 85% of it, at least 640x480 (scaled), inside its edges
  ImGui::SetNextWindowSize(ImVec2(std::min(display.x - 32.f, std::max(display.x * 0.85f, 640.f * scale)),
                                  std::min(display.y - 32.f, std::max(display.y * 0.85f, 480.f * scale))),
                           ImGuiCond_Always);
  if (!sWasOpen) {
    ImGui::SetNextWindowFocus();
    sWasOpen = true;
  }
  bool open = true;
  if (ImGui::Begin("Settings", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground)) {
    const ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    launcherPaperCard(ImGui::GetBackgroundDrawList(), at, ImVec2(at.x + size.x, at.y + size.y), scale);
    ImGui::PushFont(gLauncherFonts.body);
    noteWindow();
    ImGui::PushFont(gLauncherFonts.heading);
    ImGui::TextUnformatted("Settings");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("Paper Cut %s", PORT_VERSION);
    const float closeWidth = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth);
    if (ImGui::Button("Close")) {
      open = false;
    }
    note("F1, Back/Select or a three-finger tap opens and closes this page.");
    portSettingsTabs(true);
    ImGui::PopFont();
  }
  ImGui::End();
  launcherStyleEnd();
  if (!open) {
    setOpen(false);
  }
}
