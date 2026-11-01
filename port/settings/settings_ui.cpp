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
#include "port/cloud.h"
#include "port/mods.h"
#include "../archipelago/ttyd_ap.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
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
  if (s->widescreen && check("Widescreen HUD", &s->wideHud)) {
    changed();
  }
  if (s->widescreen) {
    note("HP and FP at the left edge, Star Points and coins at the right, instead of the 4:3 middle.");
  }
  if (check("Blurred side bars", &s->sideBars)) {
    changed();
  }
  note("Where the picture doesn't fill the screen, a blurred copy of it instead of black bars.");
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
  static constexpr const char* kFilters[] = {"Original (smooth)", "Sharp (pixels)"};
  if (combo("Texture filtering", &s->texFilter, kFilters, 2)) {
    changed();
  }
  {
    static const int sStartMsaa = s->msaa; /* what this run started with */
    if (check("Anti-aliasing (4x MSAA)", &s->msaa)) {
      changed();
    }
    note(s->msaa != sStartMsaa ? "Takes effect the next time the game starts." : "Smooths jagged edges.");
  }
  if (combo("Overlay corner", &s->fps, kCorners, 5)) {
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
constexpr const char* kLanguages[] = {"The computer's", "English", "Deutsch", "Français", "Español", "Italiano"};
constexpr const char* kRunSpeeds[] = {"Original", "125%", "150%"};

void gameplayTab(PortSettings* s) {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  static constexpr const char* kCues[] = {"Off", "On screen", "On screen and a tap"};
  const int cueCount = 3;
#else
  static constexpr const char* kCues[] = {"Off", "On screen"};
  const int cueCount = 2;
  s->acCue = s->acCue > 1 ? 1 : s->acCue;
#endif
  if (combo("Action Command cue", &s->acCue, kCues, cueCount)) {
    changed();
  }
  note("In battle, a ring closes on an A button when to press A for a Jump (and partners' stomps) and to "
       "guard an enemy jumping at you. Other commands show their own timing.");
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
  if (strcmp(PORT_GAME_ID, "G8MP01") == 0) {
    if (combo("Language", &s->language, kLanguages, 6)) {
      changed();
    }
    note("The game's text, from the next start.");
    if (check("Run at 50 Hz (PAL)", &s->pal50Hz)) {
      changed();
    }
    note("As on a PAL TV: the game's 50 Hz mode, a little slower. From the next start; 60 Hz otherwise.");
  }
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
  note("TTYD port " PORT_VERSION " (" PORT_GAME_ID ")");
  if (ImGui::Button("Take a screenshot")) {
    setOpen(false);
    portShotRequestUser(); // the next frame, without this page
  }
#if defined(__APPLE__) && TARGET_OS_IPHONE
  note("Saved to Photos.");
#elif defined(__APPLE__)
  note("Saved to Pictures/TTYD. Keyboard: F12.");
#else
  note("Saved to the Screenshots folder by the saves. Keyboard: F12.");
#endif
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
#if defined(PORT_CLOUD_SYNC)
  ImGui::Separator();
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
#endif
  crashSection();
}

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

// TTYD_SETTINGS_TAB (tests, with TTYD_SETTINGS_OPEN): the tab of that name
// selected when the page first opens
ImGuiTabItemFlags tabFlags(const char* name) {
  static const char* want = SDL_getenv("TTYD_SETTINGS_TAB");
  if (want != nullptr && strcmp(want, name) == 0) {
    want = nullptr;
    return ImGuiTabItemFlags_SetSelected;
  }
  return 0;
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
      if (ImGui::BeginTabItem("Video", nullptr, tabFlags("Video"))) {
        videoTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Audio", nullptr, tabFlags("Audio"))) {
        audioTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Gameplay", nullptr, tabFlags("Gameplay"))) {
        gameplayTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controls", nullptr, tabFlags("Controls"))) {
        controlsTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("States", nullptr, tabFlags("States"))) {
        statesTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Warp", nullptr, tabFlags("Warp"))) {
        warpTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Mods", nullptr, tabFlags("Mods"))) {
        modsTab(s);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Archipelago", nullptr, tabFlags("Archipelago"))) {
        archipelagoTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("System", nullptr, tabFlags("System"))) {
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
