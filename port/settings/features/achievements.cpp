// The "Achievements" tab (port/include/port/practice.h): the list, each with
// this file's progress toward it and the day it was earned. The toast when one
// is earned is drawn by practice.cpp's overlay.
//
// 0.5.0: about 170 of them, so grouped: a tab per category (and All, with a
// header per category), each with its earned count; each row with its item's
// icon (the game's own, from icon.tpl), a progress bar for the counted ones,
// and secrets kept as "???" until earned. Paper Mario 64's show only with a ROM.
#include "../launcher_shot.h"
#include "port/cosmetics.h"
#include "port/practice.h"
#include "port/settings.h"
#include "port/tr.h"
#include <aurora/imgui.h>
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

bool portCosmeticsDecode(const PortTexInfo& t, std::vector<uint8_t>& out); // cosmetics.cpp
ImTextureID portAchievementIconTexture(int i);

namespace {

constexpr const char* kTab = "Achievements";
constexpr ImVec4 kGold(0.66f, 0.45f, 0.02f, 1.f);

// the icons made into textures once (by icon number)
std::map<int, ImTextureID> sIcons;

ImTextureID iconTexture(int icon) {
  if (icon < 0) {
    return ImTextureID{};
  }
  if (auto it = sIcons.find(icon); it != sIcons.end()) {
    return it->second;
  }
  PortTexInfo t{};
  std::vector<uint8_t> px;
  if (!portIconTexture(icon, &t) || !portCosmeticsDecode(t, px)) {
    return ImTextureID{}; // (asked again later: the icons may not be in yet)
  }
  const ImTextureID id =
      aurora_imgui_add_texture(static_cast<uint32_t>(t.width), static_cast<uint32_t>(t.height), px.data());
  launcherShotTexture(id, t.width, t.height, px.data()); // (so the UI captures draw it too)
  sIcons[icon] = id;
  return id;
}

struct Counts {
  int earned = 0, shown = 0;
};

Counts countsOf(int category) { // -1: all
  Counts c;
  for (int i = 0; i < portAchievementCount(); i++) {
    if (portAchievementHidden(i) || (category >= 0 && portAchievementCategory(i) != category)) {
      continue;
    }
    c.shown++;
    c.earned += portAchievementEarned(i) != nullptr ? 1 : 0;
  }
  return c;
}

void row(int i) {
  const char* when = portAchievementEarned(i);
  const bool secret = portAchievementSecret(i) && when == nullptr;
  int goal = 0;
  const int have = portAchievementProgress(i, &goal);
  const float size = ImGui::GetFontSize() * 2.f;
  ImGui::PushID(i);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  if (const ImTextureID tex = secret ? ImTextureID{} : iconTexture(portAchievementIcon(i)); tex != ImTextureID{}) {
    // (dimmed until earned)
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddImage(tex, p, ImVec2(p.x + size, p.y + size), ImVec2(0, 0), ImVec2(1, 1),
                                         when != nullptr ? IM_COL32(255, 255, 255, 255) : IM_COL32(140, 140, 140, 150));
  }
  ImGui::Dummy(ImVec2(size, size));
  ImGui::TableNextColumn();
  if (secret) {
    ImGui::TextUnformatted("???");
    ImGui::TextDisabled("%s", portTr("A secret: it shows once earned."));
  } else {
    if (when != nullptr) {
      ImGui::TextColored(kGold, "%s", portTr(portAchievementName(i)));
    } else {
      ImGui::TextUnformatted(portTr(portAchievementName(i)));
    }
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextDisabled("%s", portTr(portAchievementNote(i)));
    ImGui::PopTextWrapPos();
  }
  ImGui::TableNextColumn();
  if (!secret && goal > 1) {
    char text[48];
    std::snprintf(text, sizeof(text), "%d / %d", std::min(have, goal), goal);
    ImGui::ProgressBar(static_cast<float>(std::min(have, goal)) / static_cast<float>(goal), ImVec2(-1.f, 0.f), text);
  } else if (!secret && goal == 1) {
    ImGui::TextDisabled("%s", portTr(have >= 1 ? "done" : "not yet"));
  }
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(when != nullptr ? when : "");
  ImGui::PopID();
}

// the rows of one category (-1: all, under a header each)
void table(int category) {
  if (!ImGui::BeginTable("achievements", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    return;
  }
  ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.2f);
  ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 3.f);
  ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthStretch, 1.6f);
  ImGui::TableSetupColumn("earned", ImGuiTableColumnFlags_WidthStretch, 1.f);
  for (int c = 0; c < portAchievementCategoryCount(); c++) {
    if (category >= 0 && c != category) {
      continue;
    }
    const Counts n = countsOf(c);
    if (n.shown == 0) {
      continue;
    }
    if (category < 0) {
      ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextColored(kGold, "%s  %d / %d", portTr(portAchievementCategoryName(c)), n.earned, n.shown);
    }
    for (int i = 0; i < portAchievementCount(); i++) {
      if (!portAchievementHidden(i) && portAchievementCategory(i) == c) {
        row(i);
      }
    }
  }
  ImGui::EndTable();
}

void list(int) {
  const char* file = portAchievementFile();
  const Counts all = countsOf(-1);
  if (file[0] != '\0') {
    ImGui::Text(portTr("%s: %d of %d earned"), file, all.earned, all.shown);
    ImGui::ProgressBar(all.shown > 0 ? static_cast<float>(all.earned) / static_cast<float>(all.shown) : 0.f,
                       ImVec2(-1.f, ImGui::GetFontSize() * 0.5f), "");
  } else {
    ImGui::TextDisabled("%s", portTr("Load a save file to keep its achievements."));
  }
  if (portPracticeTainted()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", portTr("The Practice tab changed this game: none are given until a file is loaded again."));
    ImGui::PopStyleColor();
  }
  if (!ImGui::BeginTabBar("achievement groups", ImGuiTabBarFlags_FittingPolicyScroll)) {
    return;
  }
  for (int c = -1; c < portAchievementCategoryCount(); c++) {
    const Counts n = countsOf(c);
    if (c >= 0 && n.shown == 0) {
      continue; // (Paper Mario 64's without a ROM)
    }
    char label[96];
    std::snprintf(label, sizeof(label), "%s %d/%d###group%d", portTr(c < 0 ? "All" : portAchievementCategoryName(c)),
                  n.earned, n.shown, c);
    if (ImGui::BeginTabItem(label)) {
      table(c);
      ImGui::EndTabItem();
    }
  }
  ImGui::EndTabBar();
}

struct Register {
  Register() {
    portPracticeDeclare();
    portSettingsAddToggle(kTab, "Achievements",
                          "Earned from what each save file has done (its flags), kept beside the memory cards "
                          "(not in the save), with a small notice when one is earned.",
                          0, "achievements", "achievements trophies progress completion");
    portSettingsAddSection(kTab, "This file", nullptr, PORT_SETTING_IN_GAME, list,
                           "achievements list progress star pieces recipes badges tattle pit battle multiplayer "
                           "co-op paper mario 64 secrets");
  }
} sRegister;

} // namespace

// the toast's picture (practice.cpp's overlay): achievement i's icon, or none (a secret not earned)
ImTextureID portAchievementIconTexture(int i) {
  return portAchievementSecret(i) && portAchievementEarned(i) == nullptr ? ImTextureID{}
                                                                          : iconTexture(portAchievementIcon(i));
}
