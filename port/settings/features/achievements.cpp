// The "Achievements" tab (port/include/port/practice.h): the list, each with
// this file's progress toward it and the day it was earned. The toast when one
// is earned is drawn by practice.cpp's overlay.
#include "port/practice.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace {

constexpr const char* kTab = "Achievements";

void list(int) {
  const char* file = portAchievementFile();
  int earned = 0;
  for (int i = 0; i < portAchievementCount(); i++) {
    earned += portAchievementEarned(i) != nullptr ? 1 : 0;
  }
  if (file[0] != '\0') {
    ImGui::Text(portTr("%s: %d of %d earned"), file, earned, portAchievementCount());
  } else {
    ImGui::TextDisabled("%s", portTr("Load a save file to keep its achievements."));
  }
  if (portPracticeTainted()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", portTr("The Practice tab changed this game: none are given until a file is loaded again."));
    ImGui::PopStyleColor();
  }
  if (!ImGui::BeginTable("achievements", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    return;
  }
  ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 3.f);
  ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthStretch, 2.f);
  ImGui::TableSetupColumn("earned", ImGuiTableColumnFlags_WidthStretch, 1.2f);
  for (int i = 0; i < portAchievementCount(); i++) {
    const char* when = portAchievementEarned(i);
    int goal = 0;
    const int have = portAchievementProgress(i, &goal);
    ImGui::PushID(i);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (when != nullptr) {
      ImGui::TextColored(ImVec4(0.66f, 0.45f, 0.02f, 1.f), "%s", portTr(portAchievementName(i)));
    } else {
      ImGui::TextUnformatted(portTr(portAchievementName(i)));
    }
    ImGui::TextDisabled("%s", portTr(portAchievementNote(i)));
    ImGui::TableNextColumn();
    if (goal > 1) {
      char text[32];
      std::snprintf(text, sizeof(text), "%d / %d", std::min(have, goal), goal);
      ImGui::ProgressBar(static_cast<float>(std::min(have, goal)) / static_cast<float>(goal), ImVec2(-1.f, 0.f),
                         text);
    } else if (goal == 1) {
      ImGui::TextDisabled("%s", have >= 1 ? "done" : "not yet");
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(when != nullptr ? when : "");
    ImGui::PopID();
  }
  ImGui::EndTable();
}

struct Register {
  Register() {
    portPracticeDeclare();
    portSettingsAddToggle(kTab, "Achievements",
                          "Earned from what each save file has done (its flags), kept beside the memory cards "
                          "(not in the save), with a small notice when one is earned.",
                          0, "achievements", "achievements trophies progress completion");
    portSettingsAddSection(kTab, "This file", nullptr, PORT_SETTING_IN_GAME, list,
                           "achievements list progress star pieces recipes badges tattle pit");
  }
} sRegister;

} // namespace
