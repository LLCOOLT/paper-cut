// The action-command dojo (port/include/port/dojo.h, docs/DOJO.md): its section
// in the Modes tab (start it, the dummy, the results by command type), and
// Timing assist by command type in the Accessibility tab (each kind's assist,
// or the dojo's suggestions), which "By command type" in Action commands:
// attacks / guards turns on. The Start menu's Settings tab has the dojo too, on its Modes page
// (port/include/port/pause_forms.h).
#include "port/dojo.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
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
PortDojoSetup sSetup = {0, PORT_DOJO_DUMMY_ON_DEFEND, 0, 1}; // the page's and the Start menu's

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
  ImGui::TextWrapped("%s", portTr("Practise any of Mario's or the partners' action commands against a dummy on the Glitz "
                                  "Pit's ring, with each press told in frames: early, Perfect, or late. The guard and "
                                  "Superguard windows show as a bar, and so do the Stylish moves' windows."));
  ImGui::TextDisabled("%s", portTr("Mario's HP, FP and Star Power fill up every turn, and the dummy never falls. "
                                   "Run Away to leave: Mario is then as he was before, items and all."));
  const bool online = portReplayActive() != 0; // (a replay plays what was recorded)
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
  const char* levels[4];
  for (int i = 0; i < 4; i++) {
    levels[i] = portTr(kLevels[i]);
  }
  const int* attack = portSettingsFind("ac_attack_assist");
  const int* guard = portSettingsFind("ac_guard_assist");
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
  if ((attack == nullptr || *attack != 3) && (guard == nullptr || *guard != 3)) {
    ImGui::TextDisabled("%s", portTr("These apply once Action commands: attacks or guards is By command type."));
  }
}

// --- the Start menu's form (port/include/port/pause_forms.h): the page's set-up and start ---

// UTF-8 (the game's names, as the page has them) for the game's font: Latin-1 on the US and EU
// releases, ASCII on the JP one; fallback when nothing of it can be shown
std::string gameText(const char* utf8, const char* fallback) {
  std::string out;
  bool lost = false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8 != nullptr ? utf8 : ""); *p != 0; p++) {
    if (*p < 0x80) {
      out += *p == '<' ? '(' : *p == '>' ? ')' : char(*p);
    } else if (PORT_GAME_ID[3] != 'J' && (*p == 0xC2 || *p == 0xC3) && p[1] != 0) {
      out += char(((*p & 0x03) << 6) | (p[1] & 0x3F));
      p++;
    } else if ((*p & 0xC0) == 0xC0) {
      lost = true;
    }
  }
  return lost || out.empty() ? std::string(fallback) : out;
}

// a command type's name for the game's font: the game's own, else the page's English translated
std::string typeGame(int type) {
  const char* label = portDojoTypeLabel(type);
  const char* english = portTrGame(portDojoTypeName(type));
  return label != nullptr ? gameText(label, english) : english;
}

// the results page: each command type tried, since entering the dojo and in all
int resultLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0;
  for (int session = 1; session >= 0; session--) {
    bool any = false;
    for (int t = 0; t < PORT_DOJO_TYPES && n + 1 < max; t++) {
      PortDojoStats s;
      portDojoGetStats(t, session, &s);
      if (s.attempts == 0) {
        continue;
      }
      if (!any) {
        std::snprintf(out[n++], PORT_PAUSE_LINE, "%s",
                      portTrGame(session ? "Since you entered the dojo" : "All your results"));
        any = true;
      }
      char average[48] = "-";
      if (s.measured > 0) {
        const float a = s.offsetSum / float(s.measured);
        if (a <= -0.5f) {
          std::snprintf(average, sizeof(average), portTrGame("%.1f early"), double(-a));
        } else if (a >= 0.5f) {
          std::snprintf(average, sizeof(average), portTrGame("%.1f late"), double(a));
        } else {
          std::snprintf(average, sizeof(average), "%s", portTrGame("on time"));
        }
      }
      std::snprintf(out[n++], PORT_PAUSE_LINE, "  %-16.16s %4d  %3d %%  %s", typeGame(t).c_str(), s.attempts,
                    s.hits * 100 / s.attempts, average);
    }
    if (!any && session == 0 && n < max) {
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", portTrGame("None yet."));
    }
  }
  return n;
}

const PortPauseInfo kResultsInfo = {"Your dojo results", resultLines, nullptr, nullptr, nullptr, nullptr, 0};

enum { kRowDummy, kRowPartner, kRowEvery, kRowAttacks, kRowEnter, kRowLeave, kRowResults };

bool dojoHeld() { return portReplayActive() != 0; }

int dojoRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 6) {
    return 0;
  }
  const bool running = portDojoRunning() != 0;
  const unsigned setup = running || dojoHeld() ? PORT_PAUSE_ROW_DIM : 0;
  const int dummies = portDojoDummyCount();
  sSetup.dummy = dummies > 0 ? (sSetup.dummy % dummies + dummies) % dummies : 0;
  PortPauseRow d = portPauseRow("Dummy", "The Glitz Pit fighter to practise on: it never falls.", nullptr, kRowDummy,
                                setup);
  char fallback[16];
  std::snprintf(fallback, sizeof(fallback), "%d", sSetup.dummy + 1);
  std::snprintf(d.value, sizeof(d.value), "%s", gameText(portDojoDummyName(sSetup.dummy), fallback).c_str());
  out[n++] = d;
  PortPauseRow p = portPauseRow("First partner out", "The partner beside Mario as the fight starts.", nullptr,
                                kRowPartner, setup);
  if (sSetup.partner == 0) {
    p.valueTr = "None";
  } else {
    std::snprintf(fallback, sizeof(fallback), "%d", sSetup.partner);
    std::snprintf(p.value, sizeof(p.value), "%s", gameText(portDojoPartnerName(sSetup.partner), fallback).c_str());
  }
  out[n++] = p;
  out[n++] = portPauseRow("Every move", "Every partner at Ultra Rank, Ultra Boots and Hammer, every Jump and Hammer "
                          "badge, all Star Powers, lent for the dojo.", sSetup.everyMove ? "On" : "Off", kRowEvery,
                          setup);
  out[n++] = portPauseRow("The dummy attacks", "Its attacks are for practising guards and Superguards.",
                          kAttacks[sSetup.attacks % 3], kRowAttacks, dojoHeld() ? PORT_PAUSE_ROW_DIM : 0);
  if (running) {
    out[n++] = portPauseRow("Leave the dojo", "In a fight, Run Away to leave (the dummy always lets you).", nullptr,
                            kRowLeave, PORT_PAUSE_ROW_CONFIRM);
  } else {
    out[n++] = portPauseRow("Enter the dojo", "Practise any of Mario's or the partners' action commands against a "
                            "dummy on the Glitz Pit's ring, with each press told in frames: early, Perfect, or late. "
                            "The guard and Superguard windows show as a bar, and so do the Stylish moves' windows.",
                            nullptr, kRowEnter, dojoHeld() ? PORT_PAUSE_ROW_DIM : PORT_PAUSE_ROW_CONFIRM);
  }
  out[n++] = portPauseRow("Your dojo results", "Kept on this device, not in the save file. The average press is how "
                          "many frames early or late your presses were, a press in time counting as 0.", nullptr,
                          kRowResults);
  return n;
}

const char* dojoAct(int id, int dir) {
  const int step = dir < 0 ? -1 : 1;
  if (id == kRowResults) { // (a page to read: on the second screen and online too)
    if (dir == 0) {
      portPauseInfoOpen(&kResultsInfo);
    }
    return nullptr;
  }
  if (dojoHeld()) {
    return dir == 0 ? "The dojo is offline only: not during online play or a replay." : nullptr;
  }
  const bool running = portDojoRunning() != 0;
  if (id == kRowAttacks) {
    sSetup.attacks = (sSetup.attacks + step + 3) % 3;
    if (running && !gPortCompanionMenu) {
      portDojoSetAttacks(sSetup.attacks); // (changed while it runs: from its next turn)
    }
    return nullptr;
  }
  if (id == kRowDummy || id == kRowPartner || id == kRowEvery) {
    if (running) {
      return dir == 0 ? "Set up the dojo before entering it." : nullptr;
    }
    if (id == kRowDummy) {
      const int count = portDojoDummyCount();
      sSetup.dummy = count > 0 ? (sSetup.dummy + step + count) % count : 0;
    } else if (id == kRowPartner) {
      sSetup.partner = (sSetup.partner + step + 8) % 8;
    } else {
      sSetup.everyMove = !sSetup.everyMove;
    }
    return nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  if (id == kRowLeave) {
    portDojoStop();
    return "In a fight, Run Away to leave (the dummy always lets you).";
  }
  const char* why = nullptr;
  return portDojoStart(&sSetup, &why) ? "The dojo opens once the menu is closed." : why;
}

const PortPauseForm kDojoForm = {"Modes", "Action-command dojo", dojoRows, dojoAct};

struct Register {
  Register() {
    portPauseAddForm(&kDojoForm);
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
