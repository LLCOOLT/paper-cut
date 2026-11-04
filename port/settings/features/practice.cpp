// The "Practice" tab's speedrun and practice tools (port/include/port/practice.h,
// port/include/port/practice_tools.h): the timer, its splits and LiveSplit;
// frame advance; the RNG; stats, items and the saved flags; a memory watch.
// The timer and the watch draw in a corner over the game, frame advance has
// F6 (hold / go on) and F7 (one frame).
#include "port/bindings.h"
#include "port/practice.h"
#include "port/practice_tools.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kTab = "Practice";
constexpr unsigned kInGame = PORT_SETTING_IN_GAME;

int* sTimer;
int* sFrameKeys;
int* sWatchCorner;
int* sWatchSet[2]; // the watched fields, a bit each
int* sLiveSplitPort;
int* sRngSeed;

int setting(const char* key) {
  const int* v = portSettingsFind(key);
  return v != nullptr ? *v : 0;
}

bool watched(int i) { return i < 62 && (*sWatchSet[i / 31] >> (i % 31) & 1) != 0; }

void setWatched(int i, bool on) {
  if (i >= 62) {
    return;
  }
  if (on) {
    *sWatchSet[i / 31] |= 1 << (i % 31);
  } else {
    *sWatchSet[i / 31] &= ~(1 << (i % 31));
  }
  portSettingsSave();
}

// a grey note, wrapped to the page
void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool contains(const char* hay, const char* needle) {
  if (needle[0] == '\0') {
    return true;
  }
  std::string h = hay, n = needle;
  std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
  return h.find(n) != std::string::npos;
}

// --- over the game --------------------------------------------------------------

// a corner's box (1-4: TL TR BL BR) for text of size, inside the window's safe area
ImVec2 cornerAt(int corner, ImVec2 size, float lift) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  float left = 0.f, top = 0.f, right = display.x, bottom = display.y;
  SDL_Rect safe{};
  if (SDL_Window* w = aurora_get_window(); w != nullptr && SDL_GetWindowSafeArea(w, &safe) && safe.w > 0) {
    left = static_cast<float>(safe.x);
    top = static_cast<float>(safe.y);
    right = static_cast<float>(safe.x + safe.w);
    bottom = static_cast<float>(safe.y + safe.h);
  }
  const float pad = 10.f;
  return ImVec2(corner == 1 || corner == 3 ? left + pad : right - size.x - pad,
                corner <= 2 ? top + pad + lift : bottom - size.y - pad - lift);
}

void box(ImDrawList* dl, ImVec2 at, ImVec2 size) {
  dl->AddRectFilled(ImVec2(at.x - 6, at.y - 4), ImVec2(at.x + size.x + 6, at.y + size.y + 4), IM_COL32(0, 0, 0, 150),
                    5.f);
}

// the timer: game time large, the next split (or the last one's time) under it
void drawTimer(ImDrawList* dl) {
  char time[32], line[96];
  const int state = portTimerState();
  portTimerFormat(portTimerGameSeconds(), time, sizeof(time));
  const int next = portSplitNext();
  if (state == PORT_TIMER_IDLE) {
    std::snprintf(line, sizeof(line), "%s - ready", portSplitCategoryName(setting("split_category")));
  } else if (next < portSplitCount() && state == PORT_TIMER_RUNNING) {
    std::snprintf(line, sizeof(line), "%d/%d  %s", next + 1, portSplitCount(), portSplitName(next));
  } else if (next > 0) {
    char at[32];
    portTimerFormat(portSplitTime(next - 1), at, sizeof(at));
    std::snprintf(line, sizeof(line), "%s  %s", portSplitName(next - 1), at);
  } else {
    line[0] = '\0';
  }
  char real[48], rt[32];
  portTimerFormat(portTimerRealSeconds(), rt, sizeof(rt));
  std::snprintf(real, sizeof(real), "RTA %s%s", rt, portTimerLoading() && setting("timer_load_removal") ? "  (load)" : "");
  ImFont* font = ImGui::GetFont();
  const float big = ImGui::GetFontSize() * 2.6f, small = ImGui::GetFontSize() * 1.3f;
  const ImVec2 t = font->CalcTextSizeA(big, FLT_MAX, 0.f, time);
  const ImVec2 l = font->CalcTextSizeA(small, FLT_MAX, 0.f, line);
  const ImVec2 r = font->CalcTextSizeA(small, FLT_MAX, 0.f, real);
  const ImVec2 size(std::max({t.x, l.x, r.x}), t.y + l.y + r.y);
  const ImVec2 at = cornerAt(*sTimer, size, 0.f);
  box(dl, at, size);
  const ImU32 color = state == PORT_TIMER_DONE ? IM_COL32(120, 220, 255, 255)
                      : state == PORT_TIMER_RUNNING ? IM_COL32(140, 240, 140, 255)
                                                    : IM_COL32(230, 230, 230, 255);
  dl->AddText(font, big, ImVec2(at.x + size.x - t.x, at.y), color, time);
  dl->AddText(font, small, ImVec2(at.x + size.x - r.x, at.y + t.y), IM_COL32(200, 200, 200, 230), real);
  dl->AddText(font, small, ImVec2(at.x + size.x - l.x, at.y + t.y + r.y), IM_COL32(255, 255, 255, 230), line);
}

void drawWatch(ImDrawList* dl) {
  std::string text;
  char value[96];
  for (int i = 0; i < portWatchCount(); i++) {
    if (watched(i)) {
      portWatchRead(i, value, sizeof(value));
      text += std::string(portWatchName(i)) + "  " + value + "\n";
    }
  }
  if (text.empty()) {
    return;
  }
  text.pop_back();
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize() * 1.2f;
  const ImVec2 size = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text.c_str());
  // below (or above) the timer when they share the corner
  const float lift = *sTimer == *sWatchCorner ? ImGui::GetFontSize() * 5.8f : 0.f;
  const ImVec2 at = cornerAt(*sWatchCorner, size, lift);
  box(dl, at, size);
  dl->AddText(font, fs, at, IM_COL32(255, 255, 255, 235), text.c_str());
}

void drawToast(ImDrawList* dl) {
  double age = 0.0;
  const int i = portAchievementToast(&age);
  if (i < 0) {
    return;
  }
  // in from the top over a quarter second, out over the last half
  const float in = std::min(1.f, static_cast<float>(age) * 4.f);
  const float out = std::min(1.f, static_cast<float>(5.0 - age) * 2.f);
  const float alpha = std::max(0.f, std::min(in, out));
  const char* title = "Achievement";
  const char* name = portAchievementName(i);
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize() * 1.3f;
  const ImVec2 a = font->CalcTextSizeA(fs, FLT_MAX, 0.f, title);
  const ImVec2 b = font->CalcTextSizeA(fs * 1.3f, FLT_MAX, 0.f, name);
  const ImVec2 size(std::max(a.x, b.x) + fs * 2.f, a.y + b.y + fs);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImVec2 at(display.x * 0.5f - size.x * 0.5f, fs * (0.5f + in));
  const int al = static_cast<int>(alpha * 255);
  dl->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(30, 25, 10, al * 220 / 255), 8.f);
  dl->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(240, 200, 60, al), 8.f, 0, 2.f);
  dl->AddText(font, fs, ImVec2(at.x + (size.x - a.x) * 0.5f, at.y + fs * 0.4f), IM_COL32(240, 200, 60, al), title);
  dl->AddText(font, fs * 1.3f, ImVec2(at.x + (size.x - b.x) * 0.5f, at.y + fs * 0.4f + a.y),
              IM_COL32(255, 255, 255, al), name);
}

void overlay(int pageOpen) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (*sTimer != 0) {
    drawTimer(dl);
  }
  if (*sWatchCorner != 0 && portPracticeInGame()) {
    drawWatch(dl);
  }
  if (portFrameAdvanceHeld() && !pageOpen) {
    char text[96]; // the keys as bound (Controls > Bindings)
    std::snprintf(text, sizeof(text), "Frame advance: %s one frame, %s go on", portBindingsKeyName(PORT_BIND_FRAME_STEP),
                  portBindingsKeyName(PORT_BIND_FRAME_HOLD));
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 at(ImGui::GetIO().DisplaySize.x * 0.5f - size.x * 0.5f,
                    ImGui::GetIO().DisplaySize.y - size.y - 14.f);
    box(dl, at, size);
    dl->AddText(at, IM_COL32(255, 230, 120, 255), text);
  }
  drawToast(dl);
}

// frame advance's bindings (Controls > Bindings; F6 and F7), with it on
int frameHold(int, int repeat) {
  if (sFrameKeys == nullptr || !*sFrameKeys || !portPracticeInGame()) {
    return 0;
  }
  if (!repeat) {
    portFrameAdvanceHold(!portFrameAdvanceHeld());
  }
  return 1;
}

int frameStep(int, int) { // held: a frame per key repeat
  if (sFrameKeys == nullptr || !*sFrameKeys || !portPracticeInGame()) {
    return 0;
  }
  portFrameAdvanceStep();
  return 1;
}

// --- the page's sections --------------------------------------------------------

void runSection(int) {
  const int state = portTimerState();
  if (ImGui::Button(portTrLabel(state == PORT_TIMER_IDLE ? "Start##timer" : "Restart##timer"))) {
    portTimerStart();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(state != PORT_TIMER_RUNNING);
  if (ImGui::Button(portTrLabel("Split"))) {
    portTimerSplit();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(state == PORT_TIMER_IDLE);
  if (ImGui::Button(portTrLabel("Reset"))) {
    portTimerReset();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  char game[32], real[32];
  portTimerFormat(portTimerGameSeconds(), game, sizeof(game));
  portTimerFormat(portTimerRealSeconds(), real, sizeof(real));
  ImGui::Text(portTr("Game %s   Real %s"), game, real);
  // a scrolled list past 10 splits
  const bool scroll = portSplitCount() > 10;
  if (ImGui::BeginTable("splits", 2, ImGuiTableFlags_RowBg | (scroll ? ImGuiTableFlags_ScrollY : 0),
                        ImVec2(0.f, scroll ? ImGui::GetFrameHeightWithSpacing() * 10 : 0.f))) {
    for (int i = 0; i < portSplitCount(); i++) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (i == portSplitNext() && state == PORT_TIMER_RUNNING) {
        ImGui::TextColored(ImVec4(0.55f, 0.95f, 0.55f, 1.f), "%s", portSplitName(i));
      } else {
        ImGui::TextUnformatted(portSplitName(i));
      }
      ImGui::TableNextColumn();
      if (portSplitTime(i) >= 0.0) {
        char at[32];
        portTimerFormat(portSplitTime(i), at, sizeof(at));
        ImGui::TextUnformatted(at);
      } else {
        ImGui::TextDisabled("-");
      }
    }
    ImGui::EndTable();
  }
  if (portSplitCount() == 0) {
    hint(setting("split_category") == 2
                            ? "No splits.txt in the user folder yet (a line each: a story value or STORY_ name, "
                              "then the split's name)."
                            : "Splits are added as the story moves.");
  }
  if (setting("split_category") == 2 && ImGui::Button(portTrLabel("Read splits.txt again"))) {
    portSplitsReload();
  }
}

void liveSplitSection(int) {
  ImGui::TextUnformatted(portLiveSplitStatus());
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
  if (ImGui::InputInt(portTrLabel("Port"), sLiveSplitPort, 0, 0)) {
    *sLiveSplitPort = std::clamp(*sLiveSplitPort, 1, 65535);
    portSettingsSave();
  }
  hint("In LiveSplit: Edit Layout, add Control > LiveSplit Server, then right-click > Control > "
                      "Start Server. Compare against Game Time for load removal.");
}

void frameSection(int) {
  if (!portFrameAdvanceHeld()) {
    if (ImGui::Button(portTrLabel("Hold the game"))) {
      portFrameAdvanceHold(1);
    }
  } else {
    if (ImGui::Button(portTrLabel("One frame"))) {
      portFrameAdvanceStep();
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Ten frames"))) {
      for (int i = 0; i < 10; i++) {
        portFrameAdvanceStep();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Go on"))) {
      portFrameAdvanceHold(0);
    }
  }
}

void rngSection(int) {
  ImGui::Text(portTr("rand() state: %u (0x%08X)"), portPracticeRand(), portPracticeRand());
  static int sSet = 1;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
  ImGui::InputInt("##seed", &sSet, 0, 0);
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Set now"))) {
    portPracticeSetRand(static_cast<unsigned>(sSet));
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
  if (ImGui::InputInt(portTrLabel("Fixed seed"), sRngSeed, 0, 0)) {
    *sRngSeed = std::max(*sRngSeed, 0);
    portSettingsSave();
  }
}

void statsSection(int) {
  static const char* const kNames[PORT_STAT_COUNT] = {"Coins", "HP", "Max HP (base)", "FP", "Max FP (base)",
                                                      "BP", "Star Power", "Max Star Power", "Level",
                                                      "Star Points", "Star Pieces", "Shine Sprites"};
  for (int i = 0; i < PORT_STAT_COUNT; i++) {
    int v = portPracticeStat(i);
    ImGui::PushID(i);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
    if (ImGui::InputInt(kNames[i], &v, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue) ||
        (ImGui::IsItemDeactivatedAfterEdit())) {
      portPracticeSetStat(i, std::clamp(v, 0, 9999));
    }
    ImGui::PopID();
    if (i % 2 == 0 && i + 1 < PORT_STAT_COUNT) {
      ImGui::SameLine(ImGui::GetFontSize() * 16.f);
    }
  }
}

void itemsSection(int) {
  static int sKind;
  static int sItem = -1;
  const char* const kKinds[] = {portTr("Items"), portTr("Badges"), portTr("Key items")};
  char name[96];
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
  if (ImGui::Combo("##kind", &sKind, kKinds, 3)) {
    sItem = -1;
  }
  ImGui::SameLine();
  const int first = portPracticeItemFirst(sKind), end = portPracticeItemEnd(sKind);
  if (sItem < first || sItem >= end) {
    sItem = first;
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.f);
  if (ImGui::BeginCombo("##item", portPracticeItemName(sItem, name, sizeof(name)))) {
    for (int id = first; id < end; id++) {
      ImGui::PushID(id);
      if (ImGui::Selectable(portPracticeItemName(id, name, sizeof(name)), id == sItem)) {
        sItem = id;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  static bool sFull;
  if (ImGui::Button(portTrLabel("Give"))) {
    sFull = !portPracticeGiveItem(sItem);
  }
  if (sFull) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("No room"));
  }
  ImGui::TextDisabled("%s", portTr("Held items (click to drop):"));
  for (int i = 0; i < 20; i++) {
    const int id = portPracticeHeldItem(i);
    if (id == 0) {
      continue;
    }
    ImGui::PushID(100 + i);
    if (ImGui::SmallButton(portPracticeItemName(id, name, sizeof(name)))) {
      portPracticeRemoveItem(id);
    }
    ImGui::PopID();
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 8.f) {
      ImGui::NewLine();
    }
  }
  ImGui::NewLine();
}

void flagsSection(int) {
  static char sFilter[64];
  static int sWhich; // 0 GSWF, 1 GSW, 2 story
  ImGui::RadioButton(portTrLabel("Flags (GSWF)"), &sWhich, 0);
  ImGui::SameLine();
  ImGui::RadioButton(portTrLabel("Words (GSW)"), &sWhich, 1);
  ImGui::SameLine();
  ImGui::RadioButton(portTrLabel("Story (GSW 0)"), &sWhich, 2);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::InputTextWithHint("##filter", "name, comment or number", sFilter, sizeof(sFilter));
  static int sIndex;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
  ImGui::InputInt("##index", &sIndex, 0, 0);
  ImGui::SameLine();
  // any index, named or not
  if (sWhich == 0) {
    bool on = portPracticeGswf(sIndex) != 0;
    if (ImGui::Checkbox(portTrLabel("by number"), &on)) {
      portPracticeSetGswf(sIndex, on);
    }
  } else {
    int v = sWhich == 2 ? portPracticeGsw(0) : portPracticeGsw(sIndex);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.f);
    if (ImGui::InputInt(portTrLabel("by number"), &v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
      portPracticeSetGsw(sWhich == 2 ? 0 : sIndex, v);
    }
  }
  const PortNamedValue* rows = sWhich == 0 ? portGswfNames : sWhich == 1 ? portGswNames : portStoryNames;
  const int count = sWhich == 0 ? portGswfNamesCount : sWhich == 1 ? portGswNamesCount : portStoryNamesCount;
  static int sMatches[2048];
  int n = 0;
  for (int i = 0; i < count && n < 2048; i++) {
    char num[16];
    std::snprintf(num, sizeof(num), "%d", rows[i].value);
    if (contains(rows[i].name, sFilter) || contains(rows[i].note, sFilter) || std::strcmp(num, sFilter) == 0) {
      sMatches[n++] = i;
    }
  }
  const int story = portPracticeGsw(0);
  if (sWhich == 2) {
    const PortNamedValue* now = portStoryName(story);
    ImGui::Text(portTr("Now %d: %s"), story, now != nullptr ? now->name : "");
  }
  if (ImGui::BeginTable("flags", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                        ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 14))) {
    ImGuiListClipper clip;
    clip.Begin(n);
    while (clip.Step()) {
      for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
        const PortNamedValue& r = rows[sMatches[k]];
        ImGui::PushID(k);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (sWhich == 0) {
          bool on = portPracticeGswf(r.value) != 0;
          if (ImGui::Checkbox("##on", &on)) {
            portPracticeSetGswf(r.value, on);
          }
        } else if (sWhich == 1) {
          int v = portPracticeGsw(r.value);
          ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.f);
          if (ImGui::InputInt("##v", &v, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) {
            portPracticeSetGsw(r.value, v);
          }
        } else if (ImGui::SmallButton(portTrLabel(r.value == story ? "now##set" : "set##set"))) {
          portPracticeSetGsw(0, r.value);
        }
        ImGui::TableNextColumn();
        ImGui::Text("%d", r.value);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.name);
        if (r.note[0] != '\0' && ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s", r.note);
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  hint("Names from the decomp (include/evt/evt_story.h); hover one for its comment.");
}

void watchSection(int) {
  static char sFilter[48];
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::InputTextWithHint("##watchfilter", "mario.position, pouch.coins...", sFilter, sizeof(sFilter));
  if (ImGui::BeginTable("watch", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                        ImVec2(0.f, ImGui::GetTextLineHeightWithSpacing() * 10))) {
    char value[96];
    for (int i = 0; i < portWatchCount(); i++) {
      if (!contains(portWatchName(i), sFilter)) {
        continue;
      }
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      bool on = watched(i);
      if (ImGui::Checkbox(portWatchName(i), &on)) {
        setWatched(i, on);
      }
      ImGui::TableNextColumn();
      portWatchRead(i, value, sizeof(value));
      ImGui::TextUnformatted(value);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (*sWatchCorner == 0) {
    hint("Choose a corner above to keep the ticked ones on screen.");
  }
}

struct Register {
  Register() {
    portPracticeDeclare();
    sTimer = portSettingsFind("speedrun_timer");
    sFrameKeys = portSettingsFind("frame_advance");
    sWatchCorner = portSettingsFind("memory_watch");
    sLiveSplitPort = portSettingsFind("livesplit_port");
    sRngSeed = portSettingsFind("rng_seed");
    sWatchSet[0] = portSettingsDeclare("memory_watch_set", 0, 0, 0x7FFFFFFF);
    sWatchSet[1] = portSettingsDeclare("memory_watch_set2", 0, 0, 0x7FFFFFFF);
    portSettingsAddOverlay(overlay, nullptr);
    portBindingsSetHandler(PORT_BIND_FRAME_HOLD, frameHold);
    portBindingsSetHandler(PORT_BIND_FRAME_STEP, frameStep);

    static const char* const kCorners[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};
    portSettingsAddChoice(kTab, "Speedrun timer",
                          "Game time in a corner, counted in the game's own frames, with real time under it.", 0,
                          "speedrun_timer", kCorners, 5, "speedrun timer igt rta splits clock");
    portSettingsAddToggle(kTab, "Load removal",
                          "Game time stops while a map or a battle loads (the timer's and LiveSplit's).", 0,
                          "timer_load_removal", "speedrun timer load removal igt loadless");
    portSettingsAddToggle(kTab, "Start with a new file", "The timer starts when a new file begins, and resets at "
                          "the title screen.", 0, "timer_autostart", "speedrun timer autostart start reset");
    static const char* sCategories[8];
    for (int i = 0; i < portSplitCategoryCount() && i < 8; i++) {
      sCategories[i] = portSplitCategoryName(i);
    }
    portSettingsAddChoice(kTab, "Splits",
                          "When to split: Any% at the end of each chapter; every step of the story; or the user "
                          "folder's splits.txt.",
                          0, "split_category", sCategories, portSplitCategoryCount(),
                          "speedrun splits category any% autosplit");
    portSettingsAddSection(kTab, "Run", nullptr, 0, runSection, "speedrun timer start split reset");
    portSettingsAddToggle(kTab, "LiveSplit",
                          "Sends the timer's start, splits, resets and game time to LiveSplit Server on this "
                          "computer.",
                          0, "livesplit", "livesplit server autosplitter speedrun");
    portSettingsAddSection(kTab, "LiveSplit connection", nullptr, 0, liveSplitSection, "livesplit server port");
    portSettingsAddToggle(kTab, "Frame advance keys", "F6 holds the game (and lets it go on), F7 moves one frame.",
                          kInGame, "frame_advance", "frame advance step pause tas");
    portSettingsAddSection(kTab, "Frame advance", nullptr, kInGame, frameSection, "frame advance step pause");
    portSettingsAddToggle(kTab, "Fixed RNG", "The game's random numbers start from a fixed seed (All settings, "
                                             "RNG) at each room and battle, so a room plays out the same way each "
                                             "try.",
                          PORT_SETTING_CHANGES_PLAY | kInGame, "rng_fixed", "rng random seed luck");
    portSettingsAddSection(kTab, "RNG", nullptr, kInGame, rngSection, "rng random seed rand luck");
    portSettingsAddSection(kTab, "Stats", "Changes count as practice: no achievements until a file is loaded.",
                           kInGame, statsSection, "coins hp fp bp level star points pieces shine edit cheat");
    portSettingsAddSection(kTab, "Items", nullptr, kInGame, itemsSection, "items badges key items give edit cheat");
    portSettingsAddSection(kTab, "Story and flags", "GSW and GSWF: the story's progress and what has happened.",
                           kInGame, flagsSection, "gsw gswf flags story progress edit sequence");
    portSettingsAddChoice(kTab, "Memory watch corner", "The fields ticked in Memory watch (All settings), kept on "
                                                       "screen.", kInGame,
                          "memory_watch", kCorners, 5, "memory watch ram display position speed");
    portSettingsAddSection(kTab, "Memory watch", nullptr, kInGame, watchSection,
                           "memory watch ram mario party pouch position speed");
  }
} sRegister;

} // namespace
