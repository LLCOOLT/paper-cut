// The settings page (Aurora backend), drawn with Dear ImGui over the game.
//
// Opens with F1, a controller's Back/Select, or a three-finger tap; Escape,
// B or the same again closes it. F11 toggles fullscreen at any time. While it
// is open the game gets no input (PADBlockInput) and keeps running, as in
// Dusklight (the Twilight Princess port on Aurora), whose menu this follows.
// Every change applies at once and is written to settings.txt
// (port/src/aurora/settings_aurora.c).

#include "port/settings.h"
#include "port/speed.h"
#include "port/touch_overlay.h"
#include "port/savestate.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

extern "C" {
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

void videoTab(PortSettings* s) {
  int rate = 0;
  for (int i = 0; i < 5; i++) {
    if (std::strcmp(s->framerate, kRates[i]) == 0) {
      rate = i;
    }
  }
  if (combo("Frame rate", &rate, kRateNames, 5)) {
    std::snprintf(s->framerate, sizeof(s->framerate), "%s", kRates[rate]);
    changed();
  }
  note("The game runs at 60 frames a second; higher rates draw frames in between (smooth camera and "
       "characters).");
  if (check("VSync", &s->vsync)) {
    changed();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (check("Fullscreen (F11)", &s->fullscreen)) {
    changed();
  }
#endif
  if (check("Widescreen (16:9)", &s->widescreen)) {
    changed();
  }
  note("Much of the game was built for 4:3: some rooms and scenes end at the 4:3 edges.");
  char names[9][32];
  const char* namep[9];
  std::snprintf(names[0], sizeof(names[0]), "Auto (the window's size)");
  for (int i = 1; i <= 8; i++) {
    std::snprintf(names[i], sizeof(names[i]), "%dx (%dx%d)", i, 640 * i, 480 * i);
  }
  for (int i = 0; i < 9; i++) {
    namep[i] = names[i];
  }
  if (combo("Internal resolution", &s->resolution, namep, 9)) {
    changed();
  }
  if (combo("Frame rate counter", &s->fps, kCorners, 5)) {
    changed();
  }
  if (check("Never skip graphics that are still loading", &s->noPopIn)) {
    changed();
  }
  note("Something drawn for the first time waits for its graphics to be ready (a short pause, once) "
       "instead of appearing a moment later.");
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  static constexpr const char* kOrientations[] = {"Turn with the device", "Landscape", "Portrait"};
  if (combo("Orientation", &s->orientation, kOrientations, 3)) {
    changed();
  }
  note("Held upright, the picture is at the top and the controls below it.");
  if (check("Keep the picture clear of the notch", &s->safeArea)) {
    changed();
  }
#endif
}

void audioTab(PortSettings* s) {
  if (ImGui::SliderInt("Volume", &s->volume, 0, 100, "%d%%")) {
    portSettingsApply();
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    changed();
  }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (check("Mute while in the background", &s->muteUnfocused)) {
    changed();
  }
#endif
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

void controlsTab(PortSettings* s) {
  if (check("Rumble", &s->rumble)) {
    changed();
  }
  if (slider("Rumble strength", &s->rumbleStrength, 0, 100)) {
    changed();
  }
  note("The game's own Rumble option (on the file select) still applies.");
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  if (check("Vibrate the device without a controller", &s->phoneRumble)) {
    changed();
  }
  ImGui::Separator();
  ImGui::TextUnformatted("On-screen controls");
  if (check("Haptic tap on button press", &s->touchHaptics)) {
    changed();
  }
  if (slider("Opacity", &s->touchOpacity, 20, 100)) {
    changed();
  }
  if (slider("Size", &s->touchSize, 70, 150)) {
    changed();
  }
  if (check("Auto-run (stick)", &s->autoRun)) {
    changed();
  }
  if (check("Fixed stick (instead of where the finger lands)", &s->stickFixed)) {
    changed();
  }
  if (check("Tap the picture to press A", &s->tapA)) {
    changed();
  }
  if (ImGui::Button("Move the on-screen controls")) {
    setOpen(false);
    portTouchEditLayout();
  }
  note("Drag them where you like, then Done; held upright and sideways each keep their own.");
  note("They hide while a controller is used and come back when the screen is touched.");
#endif
  remapSection();
}

constexpr const char* kFfSpeeds[] = {"2x", "3x", "4x", "5x", "6x", "7x", "8x"};
constexpr const char* kTextSpeeds[] = {"Original", "Fast (2x)", "Faster (4x)", "Instant"};
constexpr const char* kRunSpeeds[] = {"Original", "125%", "150%"};

void gameplayTab(PortSettings* s) {
  int ff = s->ffSpeed - 2;
  if (combo("Fast-forward speed", &ff, kFfSpeeds, 7)) {
    s->ffSpeed = ff + 2;
    changed();
  }
  if (check("Fast-forward toggles (instead of held)", &s->ffToggle)) {
    changed();
  }
  note("Fast-forward: Tab, a controller's right stick click, or the on-screen >> button.");
  if (combo("Text speed", &s->textSpeed, kTextSpeeds, 4)) {
    changed();
  }
  int run = (s->runSpeed - 100) / 25;
  if (combo("Run speed", &run, kRunSpeeds, 3)) {
    s->runSpeed = 100 + run * 25;
    changed();
  }
  note("Faster running can make some jumps longer than intended.");
  if (check("Skip the intro", &s->skipIntro)) {
    changed();
  }
  note("The logos go by quickly and the story opening is skipped (it still plays from the title "
       "screen if left alone).");
}

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

void systemTab(PortSettings* s) {
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
  if (check("Pause while in the background", &s->pauseUnfocused)) {
    changed();
  }
  if (check("Controllers work in the background", &s->backgroundInput)) {
    changed();
  }
  if (ImGui::Button("Open the save folder")) {
    char url[1200];
    std::snprintf(url, sizeof(url), "file:///%s", portSettingsUserPath());
    SDL_OpenURL(url);
  }
  note("Memory cards (by region) and settings.txt.");
#else
  (void)s;
  note("Saves are in the app's folder (Files app / storage).");
#endif
  crashSection();
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

void drawStateStatus() {
  const char* text = portSaveStateStatus();
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

void drawFps(int corner) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.0f FPS", aurora_get_fps());
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

} // namespace

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
  drawStateStatus();
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
  const float scale = sTouchDevice ? 1.6f : 1.0f;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(display.x - 32.f, 560.f * scale), std::min(display.y - 32.f, 420.f * scale)),
                           ImGuiCond_Always);
  if (!sWasOpen) {
    ImGui::SetNextWindowFocus();
    sWasOpen = true;
  }
  bool open = true;
  if (ImGui::Begin("Settings", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::SetWindowFontScale(scale);
    noteWindow();
    if (ImGui::BeginTabBar("tabs")) {
      if (ImGui::BeginTabItem("Video")) {
        videoTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Audio")) {
        audioTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Gameplay")) {
        gameplayTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controls")) {
        controlsTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("States")) {
        statesTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Warp")) {
        warpTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("System")) {
        systemTab(s);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::Separator();
    note("F1, Back/Select or a three-finger tap opens and closes this page.");
  }
  ImGui::End();
  if (!open) {
    setOpen(false);
  }
}
