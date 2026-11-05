// The action-command dojo (port/include/port/dojo.h, docs/DOJO.md): its section
// in the Modes tab (start it, the dummy, the results by command type), and
// Timing assist by command type in the Accessibility tab (each kind's assist,
// or the dojo's suggestions), which "By command type" in Action commands:
// attacks / guards turns on.
#include "port/dojo.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace {

constexpr const char* kAttacks[] = {"Every turn", "When you choose Defend", "Never"};
constexpr const char* kLevels[] = {"Off", "A little wider", "Wider", "Much wider"};

std::string sMessage;

// a command type's name: the game's own (the battle menu's, the partners'), else the page's
const char* typeName(int type) {
  const char* label = portDojoTypeLabel(type);
  return label != nullptr ? label : portTr(portDojoTypeName(type));
}

std::string averageText(const PortDojoStats& s) {
  if (s.measured == 0) {
    return "-";
  }
  const float a = s.offsetSum / float(s.measured);
  char text[64];
  if (a <= -0.5f) {
    std::snprintf(text, sizeof(text), portTr("%.1f early"), double(-a));
  } else if (a >= 0.5f) {
    std::snprintf(text, sizeof(text), portTr("%.1f late"), double(a));
  } else {
    std::snprintf(text, sizeof(text), "%s", portTr("on time"));
  }
  return text;
}

void resultsTable(bool session) {
  if (!ImGui::BeginTable(session ? "##dojo_session" : "##dojo_results", 5,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH)) {
    return;
  }
  ImGui::TableSetupColumn(portTr("Command"));
  ImGui::TableSetupColumn(portTr("Tries"));
  ImGui::TableSetupColumn(portTr("In time"));
  ImGui::TableSetupColumn(portTr("Average press (frames)"));
  ImGui::TableSetupColumn(portTr("Suggested assist"));
  ImGui::TableHeadersRow();
  for (int t = 0; t < PORT_DOJO_TYPES; t++) {
    PortDojoStats s;
    portDojoGetStats(t, session ? 1 : 0, &s);
    if (s.attempts == 0) {
      continue;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(typeName(t));
    ImGui::TableNextColumn();
    ImGui::Text("%d", s.attempts);
    ImGui::TableNextColumn();
    ImGui::Text("%d %%", s.hits * 100 / s.attempts);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(averageText(s).c_str());
    ImGui::TableNextColumn();
    const int suggest = portDojoSuggest(t);
    ImGui::TextUnformatted(suggest < 0 ? portTr("(more tries)") : portTr(kLevels[suggest]));
  }
  ImGui::EndTable();
}

bool anyResults(bool session) {
  for (int t = 0; t < PORT_DOJO_TYPES; t++) {
    PortDojoStats s;
    portDojoGetStats(t, session ? 1 : 0, &s);
    if (s.attempts > 0) {
      return true;
    }
  }
  return false;
}

void dojoSection(int inGame) {
  static PortDojoSetup sSetup = {0, PORT_DOJO_DUMMY_ON_DEFEND, 0, 1};
  ImGui::TextWrapped("%s", portTr("Practise any of Mario's or the partners' action commands against a dummy on the Glitz "
                                  "Pit's ring, with each press told in frames: early, Perfect, or late. The guard and "
                                  "Superguard windows show as a bar, and so do the Stylish moves' windows."));
  ImGui::TextDisabled("%s", portTr("Mario's HP, FP and Star Power fill up every turn, and the dummy never falls. "
                                   "Run Away to leave: Mario is then as he was before, items and all."));
  const bool online = portOnlineActive() != 0 || portReplayActive() != 0;
  const bool running = portDojoRunning() != 0;
  ImGui::BeginDisabled(running);
  {
    const char* names[64];
    const int n = portDojoDummyCount() < 64 ? portDojoDummyCount() : 64;
    for (int i = 0; i < n; i++) {
      names[i] = portDojoDummyName(i);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::Combo(portTrLabel("Dummy"), &sSetup.dummy, names, n);
    const char* partners[8];
    partners[0] = portTr("None");
    for (int i = 1; i <= 7; i++) {
      partners[i] = portDojoPartnerName(i);
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::Combo(portTrLabel("First partner out"), &sSetup.partner, partners, 8);
    bool every = sSetup.everyMove != 0;
    if (ImGui::Checkbox(portTrLabel("Every move"), &every)) {
      sSetup.everyMove = every ? 1 : 0;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("(every partner at Ultra Rank, Ultra Boots and Hammer, every Jump and Hammer badge, "
                                     "all Star Powers, lent for the dojo)"));
  }
  ImGui::EndDisabled();
  const char* attacks[3];
  for (int i = 0; i < 3; i++) {
    attacks[i] = portTr(kAttacks[i]);
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  if (ImGui::Combo(portTrLabel("The dummy attacks"), &sSetup.attacks, attacks, 3) && running) {
    portDojoSetAttacks(sSetup.attacks); // (changed while it runs: from its next turn)
  }
  ImGui::BeginDisabled(!inGame || online);
  if (!running) {
    if (ImGui::Button(portTrLabel("Enter the dojo"))) {
      const char* why = nullptr;
      sMessage = portDojoStart(&sSetup, &why) ? "" : portTr(why);
    }
  } else if (ImGui::Button(portTrLabel("Leave the dojo"))) {
    portDojoStop();
    sMessage = portTr("In a fight, Run Away to leave (the dummy always lets you).");
  }
  ImGui::EndDisabled();
  if (online) {
    ImGui::TextDisabled("%s", portTr("The dojo is offline only: not during online play or a replay."));
  } else if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Enter it while a file is in play."));
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
  if (anyResults(true)) {
    ImGui::SeparatorText(portTr("Since you entered the dojo"));
    resultsTable(true);
  }
  ImGui::SeparatorText(portTr("All your results"));
  if (anyResults(false)) {
    resultsTable(false);
    ImGui::TextDisabled("%s", portTr("Kept on this device, not in the save file. The average press is how many frames "
                                     "early or late your presses were, a press in time counting as 0."));
    if (ImGui::Button(portTrLabel("Reset the results"))) {
      portDojoResetStats();
    }
  } else {
    ImGui::TextDisabled("%s", portTr("None yet."));
  }
}

void assistSection(int inGame) {
  (void)inGame;
  portDojoDeclare();
  ImGui::TextWrapped("%s", portTr("With Action commands: attacks or guards set to By command type, only the kinds of "
                                  "command you choose here have wider windows, as much as you choose. Wider is as "
                                  "Simplifier badges make it: one badge a step for attacks."));
  const bool online = portOnlineActive() != 0;
  if (online) {
    ImGui::TextDisabled("%s", portTr("Online, the host's timing assist plays for everyone."));
  }
  const char* levels[4];
  for (int i = 0; i < 4; i++) {
    levels[i] = portTr(kLevels[i]);
  }
  const int* attack = portSettingsFind("ac_attack_assist");
  const int* guard = portSettingsFind("ac_guard_assist");
  ImGui::BeginDisabled(online);
  for (int t = 0; t < PORT_DOJO_TYPES; t++) {
    char key[48];
    std::snprintf(key, sizeof(key), "ac_assist_%s", portDojoTypeKey(t));
    int* v = portSettingsFind(key);
    if (v == nullptr) {
      continue;
    }
    const int* group = portDojoTypeGroup(t) ? guard : attack;
    const bool used = group != nullptr && *group == 3;
    int value = *v;
    ImGui::PushID(t);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    if (ImGui::Combo("##level", &value, levels, 4)) {
      char text[8];
      std::snprintf(text, sizeof(text), "%d", value);
      portSettingsSetKey(key, text);
      portSettingsSave();
    }
    ImGui::SameLine();
    if (used) {
      ImGui::TextUnformatted(typeName(t));
    } else {
      ImGui::TextDisabled("%s", typeName(t));
    }
    const int suggest = portDojoSuggest(t);
    if (suggest >= 0 && suggest != value) {
      ImGui::SameLine();
      ImGui::TextDisabled(portTr("(the dojo suggests: %s)"), portTr(kLevels[suggest]));
    }
    ImGui::PopID();
  }
  if (ImGui::Button(portTrLabel("Use the dojo's suggestions"))) {
    const int changed = portDojoApplySuggestions();
    char text[160];
    std::snprintf(text, sizeof(text), portTr("%d changed; attacks and guards are now By command type."), changed);
    sMessage = text;
  }
  ImGui::EndDisabled();
  if ((attack == nullptr || *attack != 3) && (guard == nullptr || *guard != 3)) {
    ImGui::TextDisabled("%s", portTr("These apply once Action commands: attacks or guards is By command type."));
  }
}

struct Register {
  Register() {
    portDojoDeclare();
    portSettingsAddSection("Modes", "Action-command dojo", nullptr, PORT_SETTING_CHANGES_PLAY, dojoSection,
                           "dojo practice action command timing frames superguard guard stylish training modes");
    portSettingsAddSection("Accessibility", "Timing assist by command type", nullptr, PORT_SETTING_CHANGES_PLAY,
                           assistSection, "timing assist action command wider window jump hammer guard superguard "
                                          "stylish partner dojo");
    portSettingsAddToggle("Accessibility", "Action command timing in every battle",
                          "The dojo's window after each action command, guard and Stylish move, in every battle: "
                          "how many frames early or late.",
                          0, "dojo_feedback", "dojo timing frames feedback action command guard superguard");
  }
} sRegister;

} // namespace
