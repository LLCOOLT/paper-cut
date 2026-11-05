// The settings page (Aurora backend), drawn with Dear ImGui over the game.
//
// Opens with F1, a controller's Back/Select, or a three-finger tap; Escape,
// B or the same again closes it. F11 toggles fullscreen at any time. While it
// is open the game gets no input (PADBlockInput) and keeps running, as in
// Dusklight (the Twilight Princess port on Aurora), whose menu this follows.
// Every change applies at once and is written to settings.txt
// (port/src/aurora/settings_aurora.c).

#include "port/settings.h"
#include "port/tr.h"
#include "port/resume.h"
#include "port/restart.h"
#include "settings_ui.h"
#include "launcher_style.h"
#include "button_glyphs.h"
#include "port/speed.h"
#include "port/touch_overlay.h"
#include "port/savestate.h"
#include "port/cloud.h"
#include "port/mods.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/romfont_pc.h"
#include "port/layout.h"
#include "port/bindings.h"
#include "port/menu_typing.h"
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
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

extern "C" {
#include "port/widescreen.h"
void portIosHapticTap(void);    // port/ios/ios_app.m
extern int gPortAcCueState;     // port/src/common/ac_cue_pc.c
extern int gPortAcCueLead;
void portShotRequestUser(void); // port/src/aurora/shot_aurora.c
const char* portShotStatus(void);
// port/src/common/warp_pc.c
void portWarpRequestSave(const char* path);
const char* portWarpStatus(void);
}

void portui::hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr(text));
  ImGui::PopStyleColor();
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
bool sTouchDevice; // a touch seen: bigger text and controls

// a three-finger tap: three fingers down within 180 ms, all up within 550 ms
int sFingers;
Uint64 sFirstDown;
Uint64 sThirdDown;
bool sTap;
std::string sOpenAt; // portSettingsOpenAt's tab, shown when the page next draws

void setOpen(bool open) {
  if (open == sOpen) {
    return;
  }
  sOpen = open;
  PADBlockInput(open);
  if (!open) {
    portUiReleaseInput();
    portSettingsSave();
  }
}

void changed() { portSettingsSave(); }

constexpr const char* kRates[] = {"60", "120", "144", "240", "display"};
constexpr const char* kRateNames[] = {"60 Hz (original)", "120 Hz", "144 Hz", "240 Hz", "The display's rate"};
constexpr const char* kCorners[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};

// a label in the menu language (port/include/port/tr.h)
const char* trl(const char* label) { return portTrLabel(label); }

bool combo(const char* label, int* value, const char* const* names, int count) {
  bool change = false;
  if (ImGui::BeginCombo(trl(label), portTr(names[*value]))) {
    for (int i = 0; i < count; i++) {
      ImGui::PushID(i);
      const bool picked = ImGui::Selectable(portTr(names[i]), i == *value);
      ImGui::PopID();
      if (picked) {
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
  if (ImGui::Checkbox(trl(label), &v)) {
    *value = v ? 1 : 0;
    return true;
  }
  return false;
}

void note(const char* text) { portui::hint(text); }

bool slider(const char* label, int* value, int lo, int hi) {
  if (ImGui::SliderInt(trl(label), value, lo, hi, "%d%%")) {
    portSettingsApply();
  }
  return ImGui::IsItemDeactivatedAfterEdit();
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
  if (ImGui::Button(portTrLabel("Copy the last crash report"))) {
    sCopied = copyCrashReport();
  }
  if (sCopied) {
    ImGui::SameLine();
    ImGui::TextUnformatted(portTr("Copied"));
  }
  note("last_crash.txt in the save folder; paste it to whoever is helping you.");
  portCrashReportButtons(); // GitHub, a zip (port/settings/features/crash_report.cpp)
}

// The last run crashed: offered once at start, with sending it (a click each, nothing sent on its own).
void drawCrashNotice(float scale) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  static bool sCopied;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(display.x - 32.f, 460.f * scale), 0.f), ImGuiCond_Always);
  if (ImGui::Begin(portTrLabel("The game crashed last time##crash"), nullptr,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::SetWindowFontScale(scale);
    noteWindow();
    ImGui::TextWrapped("%s", portTr("A report of where it crashed was saved. Send it to Paper Cut on GitHub, save it "
                                    "as a zip, or copy it for whoever is helping you."));
    portCrashReportTest();
    portCrashReportButtons();
    if (ImGui::Button(portTrLabel(sCopied ? "Copied##report" : "Copy report##report"))) {
      sCopied = copyCrashReport();
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Close"))) {
      portCrashReportDismiss();
    }
  }
  ImGui::End();
}

#if defined(PORT_CLOUD_SYNC)
// iCloud Drive on Apple devices, any folder on Windows and Linux (port/include/port/cloud.h)
#if defined(__APPLE__)
#define CLOUD_LABEL "Sync saves with iCloud Drive"
#define CLOUD_RESTART "Saves are brought from iCloud Drive when the game starts: restart it to get another device's progress."
#else
#define CLOUD_LABEL "Sync saves through a folder"
#define CLOUD_RESTART "Saves are brought from the folder when the game starts: restart it to get another computer's progress."
#endif
void cloudSection(PortSettings* s) {
  if (check(CLOUD_LABEL, &s->cloudSync)) {
    changed();
    if (s->cloudSync) {
      portSettingsCloudPush(0);
    }
  }
#if TARGET_OS_IPHONE
  note("Your saves are kept the same as in a folder in iCloud Drive (pick the TTYD folder the Mac version "
       "uses to share them). The newer copy of each save wins; replaced saves are kept in Backups.");
  if (ImGui::Button(trl(portCloudAvailable() ? "Choose another iCloud Drive folder" : "Choose an iCloud Drive folder"))) {
    portCloudPickFolder();
  }
  if (portCloudAvailable()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portCloudFolderName());
  }
#elif defined(__APPLE__)
  note("Your saves are kept the same as in iCloud Drive's TTYD folder (choose that folder on an iPhone or iPad "
       "to share them). The newer copy of each save wins; replaced saves are kept in Backups.");
#else
  // Windows and Linux: any folder a sync service keeps (port/src/aurora/folder_sync_aurora.c)
  note("Your saves are kept the same as in a folder a sync service keeps the same across computers: OneDrive, "
       "Dropbox, Google Drive, Syncthing, or iCloud for Windows' iCloud Drive (its TTYD folder, to share them "
       "with a Mac or an iPhone). The newer copy of each save wins; replaced saves are kept in Backups.");
  if (ImGui::Button(trl(portCloudAvailable() ? "Choose another folder" : "Choose a folder"))) {
    portCloudPickFolder();
  }
  if (portCloudFolderName()[0] != '\0') {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portCloudFolderName());
    ImGui::SameLine();
    if (ImGui::SmallButton(trl("Stop using it"))) {
      portCloudForget();
      s->cloudSync = 0;
      changed();
    }
  }
#endif
  if (s->cloudSync && portCloudStatus()[0] != '\0') {
    note(portCloudStatus());
  }
  note(CLOUD_RESTART);
}
#endif

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
void openFolder(const std::string& dir) {
  SDL_CreateDirectory(dir.c_str());
  const std::string url = "file:///" + dir;
  SDL_OpenURL(url.c_str());
}
#endif

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
    ImGui::AlignTextToFramePadding();
    ImGui::Text(portTr("Slot %d"), i + 1);
    ImGui::SameLine(ImGui::GetFontSize() * 4.f);
    char label[32];
    std::snprintf(label, sizeof(label), "%s", portSaveStateLabel(i));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
    if (ImGui::InputTextWithHint("##label", "label", label, sizeof(label))) {
      portSaveStateSetLabel(i, label);
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Save"))) {
      portSaveStateRequest(i, 0);
      setOpen(false);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(info == nullptr);
    if (ImGui::Button(portTrLabel("Load"))) {
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
  text = portTr(text);
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
  if (ImGui::Button(portTrLabel("Refresh"))) {
    readWarps();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Open the warps folder"))) {
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
  ImGui::TextWrapped(portTr("Status: %s"), portApStatus());
  ImGui::Separator();
  ImGui::TextWrapped(portTr("Seed: %s"), portApSeedPath()[0] != '\0' ? portApSeedPath() : "(none)");
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (ImGui::Button(portTrLabel("Choose a seed..."))) {
    static const SDL_DialogFileFilter kFilter[] = {{"Archipelago seed", "apttyd"}};
    SDL_ShowOpenFileDialog(seedChosen, nullptr, SDL_GetKeyboardFocus(), kFilter, 1, nullptr, false);
  }
  ImGui::SameLine();
#endif
  note("A new seed applies at the next start.");
  ImGui::InputText(portTrLabel("Server"), server, sizeof(server));
  ImGui::InputText(portTrLabel("Slot"), slot, sizeof(slot));
  ImGui::InputText(portTrLabel("Password"), password, sizeof(password), ImGuiInputTextFlags_Password);
  if (ImGui::Button(portTrLabel("Connect"))) {
    portApConnect(server, slot, password);
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Disconnect"))) {
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
  int PortSettings::*field = nullptr;   // a next-start setting's value (Restart now)
  int (*shownNow)(void) = nullptr;      // a feature's control: shown only while this says so (portSettingsSetShown)
};

// The tabs in order; a feature's own tab (portSettingsAdd*) comes after these.
// the settings first, grouped and ordered as most games have theirs, then the
// modes and tools (features add their own tabs after these, in this order when listed)
constexpr const char* kTabs[] = {"Gameplay", "Enhancements", "Display",       "Graphics", "Interface", "Audio",
                                 "Controls", "Multiplayer",  "Accessibility", "Language", "General",
                                 "Saves",        "Cosmetics",    "Practice",   "Challenges",   "Modes",
                                 "Achievements", "Mods",         "Randomizer", "Archipelago",  "Streaming"};

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
constexpr const char* kDisplayModes[] = {"Windowed", "Borderless", "Full screen"};
constexpr const char* kOrientations[] = {"Turn with the device", "Landscape", "Portrait"};
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr const char* kCues[] = {"Off", "On screen", "On screen and a tap"};
constexpr int kCueCount = 3;
#else
constexpr const char* kCues[] = {"Off", "On screen"};
constexpr int kCueCount = 2;
#endif

bool widescreenOn(const PortSettings* s) { return s->widescreen != 0 || portWidescreenWide(); }
bool isEurope(const PortSettings*) { return std::strcmp(PORT_GAME_ID, "G8MP01") == 0; }

const Item kItems[] = {
    // Display
    {"Display", "Frame rate",
     "The game runs at 60 frames a second; higher rates draw frames in between (smooth camera and characters).", 0,
     videoRate, "fps hz 120 144 240 smooth interpolation", nullptr},
    {"Display", "VSync", nullptr, 0, TOGGLE(vsync), "tearing", nullptr},
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Display", "Display mode (F11 or Alt+Enter)",
     "Borderless: the whole screen, switching away at once. Full screen: the screen is the game's alone, at its own "
     "resolution and refresh rate.",
     0, CHOICE(fullscreen, kDisplayModes, 3), "fullscreen full screen window windowed borderless exclusive", nullptr},
#endif
    {"Display", "Widescreen (16:9)", "Much of the game was built for 4:3: some rooms and scenes end at the 4:3 edges.",
     0, TOGGLE(widescreen), "aspect ratio 16:9", nullptr},
    {"Interface", "Widescreen HUD",
     "HP and FP at the left edge, Star Points and coins at the right, instead of the 4:3 middle.", 0,
     TOGGLE(wideHud), "status bar", widescreenOn},
    {"Display", "Blurred side bars", "Where the picture doesn't fill the screen, a blurred copy of it instead of black bars.",
     0, TOGGLE(sideBars), "pillarbox black bars", nullptr},
    {"Graphics", "Internal resolution", nullptr, 0, resolution, "resolution scale sharp 4k", nullptr},
    {"Graphics", "Texture filtering", nullptr, 0, CHOICE(texFilter, kFilters, 2), "pixels smooth sharp", nullptr},
    {"Graphics", "Anti-aliasing (4x MSAA)", "Smooths jagged edges.", kNextStart, TOGGLE(msaa), "msaa jaggies edges",
     nullptr, nullptr, &PortSettings::msaa},
    {"Interface", "Overlay corner", "Frame rate, clock and battery in a corner of the screen.", 0, overlay,
     "fps counter clock battery", nullptr},
    {"Graphics", "Never skip graphics that are still loading",
     "Something drawn for the first time waits for its graphics to be ready (a short pause, once) instead of "
     "appearing a moment later.",
     0, TOGGLE(noPopIn), "pop-in stutter shaders", nullptr},
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Display", "Orientation", "Held upright, the picture is at the top and the controls below it.", 0,
     CHOICE(orientation, kOrientations, 3), "portrait landscape rotate", nullptr},
    {"Display", "Keep the picture clear of the notch", nullptr, 0, TOGGLE(safeArea), "safe area island", nullptr},
#endif

    // Audio: port/settings/features/volume.cpp (the mixer, the volume keys)

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
    // the keys and controller buttons: port/settings/features/bindings.cpp

    // Gameplay
    {"Accessibility", "Action Command cue",
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
    {"Language", "Language", "The game's text.", kNextStart, CHOICE(language, kLanguages, 6),
     "english deutsch francais espanol italiano text", isEurope, nullptr, &PortSettings::language},
    {"Display", "Run at 50 Hz (PAL)", "As on a PAL TV: the game's 50 Hz mode, a little slower. 60 Hz otherwise.",
     kNextStart | kChangesPlay, TOGGLE(pal50Hz), "pal 60 hz eurgb60 refresh", isEurope, nullptr,
     &PortSettings::pal50Hz},

    // Practice
    {"Practice", "Save states", nullptr, kInGame, [](PortSettings*, const Item&) { statesTab(); },
     "states slots f5 f8 load", nullptr},
    {"Practice", "Save warps", nullptr, kInGame, [](PortSettings*, const Item&) { warpTab(); },
     "warp gci practice saves", nullptr},

    // Mods: port/settings/features/mods_manager.cpp

    // Archipelago
    {"Archipelago", "Archipelago", nullptr, 0, [](PortSettings*, const Item&) { archipelagoTab(); },
     "randomizer multiworld seed server", nullptr},

    // General and Saves
    {"General", "Skip the launcher (start with the last disc played)", nullptr, 0, TOGGLE(launcherSkip),
     "launcher startup disc", nullptr},
    {"General", "Take a screenshot",
#if defined(__APPLE__) && TARGET_OS_IPHONE
     "Saved to Photos.",
#elif defined(__APPLE__)
     "Saved to Pictures/Paper Cut. Keyboard: F12.",
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
    {"General", "Pause while in the background", nullptr, 0, TOGGLE(pauseUnfocused), "focus", nullptr},
    {"Controls", "Controllers work in the background", nullptr, 0, TOGGLE(backgroundInput), "focus gamepad", nullptr},
    {"Saves", "Open the save folder", "Memory cards (by region) and settings.txt.", 0,
     [](PortSettings*, const Item& it) {
       if (ImGui::Button(it.label)) {
         char url[1200];
         std::snprintf(url, sizeof(url), "file:///%s", portSettingsUserPath());
         SDL_OpenURL(url);
       }
     },
     "saves memory card gci folder", nullptr},
#else
    {"Saves", "Saves", "Saves are in the app's folder (Files app / storage).", 0, nullptr, "memory card gci",
     nullptr},
#endif
#if defined(PORT_CLOUD_SYNC)
    {"Saves", CLOUD_LABEL, nullptr, 0, [](PortSettings* s, const Item&) { cloudSection(s); },
     "icloud cloud sync folder onedrive dropbox google drive syncthing", nullptr},
#endif
    {"General", "Crash reports", nullptr, 0, [](PortSettings*, const Item&) { crashSection(); }, "crash report bug",
     nullptr},
};

// --- Features' own controls (portSettingsAdd*), for settings they declared
// (portSettingsDeclare): kept here and drawn after the fixed ones of their tab.
struct Extra {
  const char* key;
  int kind; // 0 toggle, 1 choice, 2 slider (sections: portSettingsAddSection)
  const char* const* choices;
  int count, min, max;
  const char* format;
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
  int* v = portSettingsFind(e.key);
  if (v == nullptr) {
    // A feature that declares its setting only where it applies (once the
    // game runs) leaves the launcher's Settings tab without it: declared here
    // from the control's range, its lowest value the default, and named in the
    // log so the feature can declare it at start (as display.cpp does).
    const int lo = e.kind == 2 ? e.min : 0;
    const int hi = e.kind == 0 ? 1 : e.kind == 1 ? e.count - 1 : e.max;
    std::fprintf(stderr, "settings: %s declared by the settings page (declare it at start)\n", e.key);
    v = portSettingsDeclare(e.key, lo, lo, hi);
  }
  bool edited = false;
  if (e.kind == 0) {
    edited = check(it.label, v);
  } else if (e.kind == 1) {
    edited = combo(it.label, v, e.choices, e.count);
  } else {
    ImGui::SliderInt(trl(it.label), v, e.min, e.max, e.format != nullptr ? portTr(e.format) : "%d");
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

char sSearch[64];
bool sTabsInGame; // for the features' sections (portSettingsAddSection)

// a next-start setting changed this run: Restart now beside it (a section,
// whose value isn't known here: whenever a game runs)
bool restartWanted(PortSettings* s, const Item& it) {
  if (it.field != nullptr) {
    return portRestartPendingFor(&(s->*it.field)) != 0;
  }
  if (it.draw == drawExtra && it.data != nullptr) {
    return portRestartPendingFor(portSettingsFind(static_cast<const Extra*>(it.data)->key)) != 0;
  }
  return sTabsInGame;
}

// Console-accurate mode (features/console_accurate.cpp): an item it locks
extern "C" int portConsoleAccurateLocked(unsigned flags, const char* key, const char* label);
const char* itemKey(const Item& it) {
  if (it.draw == drawExtra && it.data != nullptr) {
    return static_cast<const Extra*>(it.data)->key;
  }
  return std::strcmp(it.label, "Text speed") == 0 ? "text_speed" : std::strcmp(it.label, "Run speed") == 0 ? "run_speed" : nullptr;
}

void drawItem(PortSettings* s, const Item& it) {
  ImGui::PushID(it.label);
  // online co-op and a replay (recording or playing): this page is one player's alone and its
  // changes no input, so what changes play stays as the run began (the Start menu's forms,
  // which every game of a session runs in lockstep, still change it)
  const bool runLocked = (it.flags & kChangesPlay) != 0 && (portOnlineActive() || portReplayActive());
  const bool locked = runLocked || portConsoleAccurateLocked(it.flags, itemKey(it), it.label) != 0;
  ImGui::BeginDisabled(locked);
  if (it.draw != nullptr) {
    it.draw(s, it);
  } else {
    ImGui::TextUnformatted(portTr(it.label));
  }
  ImGui::EndDisabled();
  // markers after a one-line control (sections draw their own lines)
  if (it.flags & kNextStart) {
    marker(portTr("next start"), IM_COL32(52, 104, 178, 255));
    if (restartWanted(s, it)) {
      portRestartButton();
    }
  }
  if (it.flags & kChangesPlay) {
    marker(portTr("changes play"), IM_COL32(206, 112, 22, 255));
  }
  if (it.flags & kChangesSaves) {
    marker(portTr("changes saves"), IM_COL32(196, 32, 42, 255));
  }
  if (it.note != nullptr) {
    note(it.note);
  }
  ImGui::Spacing();
  ImGui::PopID();
}

bool visible(const PortSettings* s, const Item& it, bool inGame) {
  return (inGame || !(it.flags & kInGame)) && (it.shown == nullptr || it.shown(s)) &&
         (it.shownNow == nullptr || it.shownNow() != 0);
}

// case-insensitive: every word of query is in the item's words
bool matches(const Item& it, const char* query) {
  std::string hay = std::string(it.tab) + " " + it.label + " " + (it.note ? it.note : "") + " " +
                    (it.keywords ? it.keywords : "") + " " + portTr(it.tab) + " " + portTr(it.label) + " " +
                    (it.note ? portTr(it.note) : "");
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

} // namespace

void portSettingsTabs(bool inGame) {
  PortSettings* s = portSettings();
  sTabsInGame = inGame;
  portRestartPending(); // (the values this run began with, before any is changed here)
  static bool sSearchSet;
  if (!sSearchSet) {
    sSearchSet = true;
    if (const char* want = SDL_getenv("TTYD_SETTINGS_SEARCH")) { // tests: a search typed from the start
      std::snprintf(sSearch, sizeof(sSearch), "%s", want);
    }
  }
  ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 18.f));
  ImGui::InputTextWithHint("##search", portTr("Search settings"), sSearch, sizeof(sSearch));
  if (sSearch[0] != '\0') {
    ImGui::SameLine();
    if (ImGui::Button(trl("Clear"))) {
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
          ImGui::SeparatorText(portTr(tab.c_str()));
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
  if (!sOpenAt.empty()) {
    sTab = sOpenAt;
    sOpenAt.clear();
  }
  if (std::find(tabs.begin(), tabs.end(), sTab) == tabs.end()) {
    sTab = tabs.front();
  }
  // a controller's triggers step through the tabs (in the game its shoulder
  // buttons too; in the launcher those switch the launcher's own tabs)
  const int count = static_cast<int>(tabs.size());
  const int at = static_cast<int>(std::find(tabs.begin(), tabs.end(), sTab) - tabs.begin());
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL2, false) || (inGame && ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))) {
    sTab = tabs[(at + count - 1) % count];
  } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR2, false) ||
             (inGame && ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))) {
    sTab = tabs[(at + 1) % count];
  }
  ImGui::BeginChild("tabs", ImVec2(ImGui::GetFontSize() * 8.f, 0));
  for (const std::string& tab : tabs) {
    if (ImGui::Selectable(trl(tab.c_str()), tab == sTab)) {
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

extern "C" void portSettingsSetShown(const char* key, int (*shown)(void)) {
  for (Item& it : extraItems()) {
    if (it.draw == drawExtra && it.data != nullptr && std::strcmp(static_cast<const Extra*>(it.data)->key, key) == 0) {
      it.shownNow = shown;
    }
  }
}

extern "C" void portSettingsSetChoices(const char* key, const char* const* names, int count) {
  for (Extra& e : extras()) {
    if (e.kind == 1 && std::strcmp(e.key, key) == 0) {
      e.choices = names;
      e.count = count;
    }
  }
}

// the sections' tabs and names, for the Start menu (portSettingsSections)
static std::vector<std::pair<const char*, const char*>>& sectionNames() {
  static std::vector<std::pair<const char*, const char*>> list;
  return list;
}

extern "C" void portSettingsAddSection(const char* tab, const char* label, const char* note, unsigned flags,
                                       void (*draw)(int inGame), const char* keywords) {
  Item it{tab, label, note, flags,
          [](PortSettings*, const Item& self) {
            ImGui::SeparatorText(portTr(self.label)); // the section's name over what it draws
            reinterpret_cast<void (*)(int)>(const_cast<void*>(self.data))(sTabsInGame ? 1 : 0);
          },
          keywords, nullptr};
  it.data = reinterpret_cast<const void*>(draw);
  extraItems().push_back(it);
  sectionNames().push_back({tab, label});
}

extern "C" int portSettingsSections(const char** tabs, const char** labels, int max) {
  int n = 0;
  for (const auto& s : sectionNames()) {
    if (n >= max) {
      static bool sTold;
      if (!sTold) { // (the caller's array is too small: the rest lose their Start menu rows)
        sTold = true;
        std::fprintf(stderr, "settings: %d sections, only %d listed\n", int(sectionNames().size()), max);
      }
      break;
    }
    tabs[n] = s.first;
    labels[n++] = s.second;
  }
  return n;
}

namespace {
struct Overlay {
  void (*draw)(int pageOpen);
  int (*event)(const void* sdlEvent);
};
std::vector<Overlay>& overlays() {
  static std::vector<Overlay> list;
  return list;
}
} // namespace

extern "C" void portSettingsAddOverlay(void (*draw)(int pageOpen), int (*event)(const void* sdlEvent)) {
  overlays().push_back(Overlay{draw, event});
}

// A touch at window point (x, y) is on the settings page or the crash notice.
// The keyboard's defaults and second keys (portKeyboardDefaults, portKeyboardApplyPad,
// portKeyboardActivate): port/settings/features/bindings.cpp

void portUiReleaseInput() {
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetWindowFocus(nullptr);
  io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
  io.ClearInputKeys();
}

// the touch controls' settings button (port/touch/touch_overlay.cpp)
extern "C" void portSettingsToggle(void) {
  setOpen(!sOpen);
}

// the Start menu's Wardrobe (port/src/aurora/wardrobe_aurora.c): the page open on one of its tabs
extern "C" void portSettingsOpenAt(const char* tab) {
  sOpenAt = tab != nullptr ? tab : "";
  setOpen(true);
}

extern "C" int portSettingsOwnsPoint(float x, float y) {
  for (int i = 0; i < sWinCount; i++) {
    if (x >= sWinMin[i].x && x < sWinMax[i].x && y >= sWinMin[i].y && y < sWinMax[i].y) {
      return 1;
    }
  }
  return 0;
}

bool gPortLauncherShown;

namespace {

// Touches as ImGui's mouse while the launcher or the settings page is shown:
// Aurora turns SDL's touch-to-mouse events off (the game takes touches as
// its own), so ImGui saw none. One finger: down presses where it lands, up
// releases there (a tap); a drag mostly up or down scrolls the window under
// it instead, its press let go off the item first so nothing is clicked; a
// sideways drag stays a press (sliders).
SDL_FingerID sTouchFinger;
bool sTouchDown;
bool sTouchScroll;
ImVec2 sTouchStart, sTouchLast;

void touchToMouse(const SDL_Event* e) {
  if (ImGui::GetCurrentContext() == nullptr || !(sOpen || gPortLauncherShown)) {
    sTouchDown = false;
    return;
  }
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 p(e->tfinger.x * io.DisplaySize.x, e->tfinger.y * io.DisplaySize.y);
  if (e->type == SDL_EVENT_FINGER_DOWN) {
    if (sTouchDown) {
      return; // a second finger: the first one drives
    }
    sTouchDown = true;
    sTouchScroll = false;
    sTouchFinger = e->tfinger.fingerID;
    sTouchStart = sTouchLast = p;
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(p.x, p.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    return;
  }
  if (!sTouchDown || e->tfinger.fingerID != sTouchFinger) {
    return;
  }
  io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
  if (e->type == SDL_EVENT_FINGER_MOTION) {
    const float dx = p.x - sTouchStart.x, dy = p.y - sTouchStart.y;
    const float slop = 8.f; // points: past a tap's wobble
    if (!sTouchScroll && std::fabs(dy) > slop && std::fabs(dy) > 2.f * std::fabs(dx)) {
      sTouchScroll = true; // let the press go off the item, then point back for the wheel
      io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
      io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
      io.AddMousePosEvent(sTouchStart.x, sTouchStart.y);
    }
    if (sTouchScroll) {
      // a finger's movement in wheel steps (ImGui scrolls about 5 lines a step)
      const float step = ImGui::GetFontSize() * 5.f;
      io.AddMouseWheelEvent(0.f, (p.y - sTouchLast.y) / (step > 1.f ? step : 1.f));
    } else {
      io.AddMousePosEvent(p.x, p.y);
    }
    sTouchLast = p;
    return;
  }
  // up or cancelled: let go, and take the pointer away so nothing stays hovered
  if (!sTouchScroll) {
    io.AddMousePosEvent(p.x, p.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  }
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  sTouchDown = false;
}

} // namespace

extern "C" void portSettingsEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type == SDL_EVENT_FINGER_DOWN || e->type == SDL_EVENT_FINGER_MOTION || e->type == SDL_EVENT_FINGER_UP ||
      e->type == SDL_EVENT_FINGER_CANCELED) {
    touchToMouse(e);
  }
  if (portMenuTypingEvent(sdlEvent)) {
    return; // typed into a Start menu keyboard (menu_typing.cpp)
  }
  if (portBindingsEvent(sdlEvent)) {
    return; // a key or button being chosen (Bindings), or one of Paper Cut's actions
  }
  for (const Overlay& o : overlays()) {
    if (o.event != nullptr && o.event(sdlEvent)) {
      return; // a feature's key
    }
  }
  switch (e->type) {
  case SDL_EVENT_DID_ENTER_BACKGROUND:
  case SDL_EVENT_TERMINATING:
  case SDL_EVENT_QUIT:
    portResumeFlush();        // quick resume: where the player was (resume_aurora.c)
    portSettingsCloudPush(1); // the app may be suspended or gone next
    break;
  case SDL_EVENT_KEY_DOWN:
    if (e->key.repeat) {
      break;
    }
    // F1, Tab, F5, F8, F12, F11 and the controller's Back and right stick:
    // Paper Cut's actions, as bound (portBindingsEvent, above)
    if (e->key.key == SDLK_ESCAPE && sOpen) {
      setOpen(false);
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (e->gbutton.button == SDL_GAMEPAD_BUTTON_EAST && sOpen && !portBindingsCapturing() &&
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
  for (const Overlay& o : overlays()) {
    if (o.draw != nullptr) {
      o.draw(sOpen ? 1 : 0);
    }
  }
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
    ImGui::TextUnformatted(portTr("Settings"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled(portTr("Paper Cut %s"), PORT_VERSION);
    const float closeWidth = ImGui::CalcTextSize(portTr("Close")).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeWidth);
    if (ImGui::Button(trl("Close"))) {
      open = false;
    }
    note("The Start menu's Settings tab, F1, Back/Select, the touch controls' three-bar button or a three-finger tap "
         "opens this page.");
    portSettingsTabs(true);
    ImGui::PopFont();
  }
  ImGui::End();
  launcherStyleEnd();
  if (!open) {
    setOpen(false);
  }
}

// --- The pause menu's Settings tab (port/src/aurora/pause_settings_aurora.c): the
// page's plain controls. The built-in ones kItems draws from a PortSettings
// field (or one of the audio mixer's declared settings) are listed here, the
// frame rate (a string) and the sections left out; features' come from their
// portSettingsAdd* (extras). Each tab's in the page's order.
namespace {

constexpr const char* kHzNames[] = {"Auto", "1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
constexpr const char* kFfNames[] = {"2x", "3x", "4x", "5x", "6x", "7x", "8x"};
constexpr const char* kOnOff[] = {"Off", "On"};

struct PauseField {
  const char* tab;
  const char* label;
  const char* note;
  unsigned flags;
  int kind;
  int PortSettings::*field; // or key
  const char* key;
  const char* const* choices;
  int count, min, max, step;
  const char* format;
  bool (*shown)(const PortSettings*);
};

const PauseField kPauseFields[] = {
    {"Display", "VSync", "Present at the display's refresh: no tearing.", 0, 0, &PortSettings::vsync},
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Display", "Display mode", "A window, the whole screen borderless, or full screen (F11 or Alt+Enter).", 0, 1,
     &PortSettings::fullscreen, nullptr, kDisplayModes, 3},
#endif
    {"Display", "Widescreen", "16:9 instead of the game's own 4:3. Some rooms and scenes end at the 4:3 edges.", 0, 0,
     &PortSettings::widescreen},
    {"Interface", "Widescreen HUD", "HP and FP at the left edge, Star Points and coins at the right.", 0, 0,
     &PortSettings::wideHud, nullptr, nullptr, 0, 0, 0, 0, nullptr, widescreenOn},
    {"Display", "Blurred side bars", "A blurred copy of the picture instead of black bars beside it.", 0, 0,
     &PortSettings::sideBars},
    {"Graphics", "Internal resolution", "Auto: the window's size; or a multiple of 640x480.", 0, 1,
     &PortSettings::resolution, nullptr, kHzNames, 9},
    {"Graphics", "Texture filtering", "Smooth as the game draws them, or sharp pixels.", 0, 1,
     &PortSettings::texFilter, nullptr, kFilters, 2},
    {"Interface", "Overlay corner", "Frame rate, clock and battery in a corner of the screen.", 0, 1, &PortSettings::fps,
     nullptr, kCorners, 5},
    {"Graphics", "Never skip loading graphics", "Something drawn for the first time waits for its graphics.", 0, 0,
     &PortSettings::noPopIn},
    {"Graphics", "Anti-aliasing (4x MSAA)", "Smooths jagged edges.", kNextStart, 0, &PortSettings::msaa},
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Display", "Keep clear of the notch", nullptr, 0, 0, &PortSettings::safeArea},
#endif
    {"Audio", "Volume", "Everything's volume.", 0, 2, &PortSettings::volume, nullptr, nullptr, 0, 0, 100, 5, "%d%%"},
    {"Audio", "Music", "The music's volume.", 0, 2, nullptr, "music_volume", nullptr, 0, 0, 100, 5, "%d%%"},
    {"Audio", "Sound effects", "Everything that isn't music or ambience.", 0, 2, nullptr, "sfx_volume", nullptr, 0, 0,
     100, 5, "%d%%"},
    {"Audio", "Ambience", "Wind, water, crowds and other sounds of a place.", 0, 2, nullptr, "ambience_volume", nullptr,
     0, 0, 100, 5, "%d%%"},
    {"Audio", "Mute in the background", "Silent while another window has the focus.", 0, 0,
     &PortSettings::muteUnfocused},
    {"Controls", "Rumble", "The controller's rumble.", 0, 0, &PortSettings::rumble},
    {"Controls", "Rumble strength", "The game's own Rumble option still applies.", 0, 2, &PortSettings::rumbleStrength,
     nullptr, nullptr, 0, 0, 100, 10, "%d%%"},
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    {"Controls", "Vibrate without a controller", nullptr, 0, 0, &PortSettings::phoneRumble},
    {"Controls", "Haptic tap on button press", nullptr, 0, 0, &PortSettings::touchHaptics},
    {"Controls", "On-screen controls' opacity", nullptr, 0, 2, &PortSettings::touchOpacity, nullptr, nullptr, 0, 20,
     100, 10, "%d%%"},
    {"Controls", "On-screen controls' size", nullptr, 0, 2, &PortSettings::touchSize, nullptr, nullptr, 0, 70, 150, 10,
     "%d%%"},
    {"Controls", "Auto-run", "On-screen stick: past a small tilt, always a full one.", 0, 0, &PortSettings::autoRun},
    {"Controls", "Fixed stick", "The on-screen stick stays in its place.", 0, 0, &PortSettings::stickFixed},
    {"Controls", "Tap the picture to press A", nullptr, 0, 0, &PortSettings::tapA},
#endif
    {"Gameplay", "Text speed", "How fast the game's text appears.", 0, 1, &PortSettings::textSpeed, nullptr,
     kTextSpeeds, 4},
    {"Accessibility", "Action Command cue", "A ring shows when to press A in battle.", 0, 1, &PortSettings::acCue, nullptr,
     kCues, kCueCount},
    {"Gameplay", "Fast-forward speed", "Fast-forward: Tab, the right stick's click, or >> on screen.", 0, 2,
     &PortSettings::ffSpeed, nullptr, nullptr, 0, 2, 8, 1, "%dx"},
    {"Gameplay", "Fast-forward toggles", "On: a press turns it on and off. Off: held.", 0, 0, &PortSettings::ffToggle},
    {"Gameplay", "Run speed", "Faster running can make some jumps longer than intended.", kChangesPlay, 2,
     &PortSettings::runSpeed, nullptr, nullptr, 0, 100, 150, 25, "%d%%"},
    {"Language", "Language", "The game's text.", kNextStart, 1, &PortSettings::language, nullptr, kLanguages, 6, 0, 0,
     0, nullptr, isEurope},
    {"Display", "Run at 50 Hz (PAL)", "As on a PAL TV: the game's 50 Hz mode, a little slower.",
     kNextStart | kChangesPlay, 0, &PortSettings::pal50Hz, nullptr, nullptr, 0, 0, 0, 0, nullptr, isEurope},
    {"General", "Pause in the background", "The game waits while another window has the focus.", 0, 0,
     &PortSettings::pauseUnfocused},
    {"Controls", "Controllers in the background", "Controllers still work while another window has the focus.", 0, 0,
     &PortSettings::backgroundInput},
};

} // namespace

// Console-accurate mode (features/console_accurate.cpp): the keys of the
// settings marked "changes play", the features' and Run speed
extern "C" int portSettingsPlayKeys(const char** keys, int max) {
  int n = 0;
  for (const Item& it : extraItems()) {
    if (n < max && it.draw == drawExtra && it.data != nullptr && (it.flags & kChangesPlay)) {
      keys[n++] = static_cast<const Extra*>(it.data)->key;
    }
  }
  if (n < max) {
    keys[n++] = "run_speed";
  }
  return n;
}

extern "C" int portSettingsControls(PortSettingsControl* out, int max) {
  PortSettings* s = portSettings();
  int n = 0;
  // $TTYD_TR_DUMP (the list to translate, port/include/port/tr.h): every
  // choice's name once, the ones not on screen too
  static bool sDumped;
  if (!sDumped && SDL_getenv("TTYD_TR_DUMP") != nullptr) {
    sDumped = true;
    const auto names = [](const char* const* list, int count) {
      for (int i = 0; list != nullptr && i < count; i++) {
        portTr(list[i]);
      }
    };
    names(kRateNames, 5), names(kCorners, 5), names(kFilters, 2), names(kTextSpeeds, 4), names(kRunSpeeds, 3);
    names(kCues, kCueCount), names(kOnOff, 2);
    for (const Extra& e : extras()) {
      names(e.choices, e.kind == 1 ? e.count : 0);
    }
  }
  for (const std::string& tab : allTabs()) {
    for (const PauseField& f : kPauseFields) {
      if (n >= max || tab != f.tab || (f.shown != nullptr && !f.shown(s))) {
        continue;
      }
      int* v = f.field != nullptr ? &(s->*f.field) : portSettingsFind(f.key);
      if (v == nullptr) {
        continue;
      }
      out[n++] = {f.tab, f.label, f.note, f.flags, f.kind, v, f.kind == 0 ? kOnOff : f.choices,
                  f.kind == 0 ? 2 : f.count, f.min, f.max, f.step > 0 ? f.step : 1, f.format};
    }
    for (const Item& it : extraItems()) {
      // (a section's data is its draw function: only drawExtra's items hold an Extra)
      if (n >= max || tab != it.tab || it.draw != drawExtra || it.data == nullptr ||
          (it.shownNow != nullptr && it.shownNow() == 0)) {
        continue;
      }
      const Extra& e = *static_cast<const Extra*>(it.data);
      int* v = portSettingsFind(e.key);
      if (v == nullptr || e.kind < 0 || e.kind > 2) {
        continue;
      }
      // a slider's step: a twentieth of its range (5 for a percentage), at least 1
      const int step = e.kind == 2 ? std::max(1, (e.max - e.min) / 20) : 1;
      out[n++] = {it.tab, it.label, it.note, it.flags, e.kind, v, e.kind == 0 ? kOnOff : e.choices,
                  e.kind == 0 ? 2 : e.count, e.min, e.max, step, e.format};
    }
  }
  return n;
}
