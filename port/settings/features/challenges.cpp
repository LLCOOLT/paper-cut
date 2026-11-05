// The Challenges tab: presets that set the difficulty, the action-command
// assists, Retry on Game Over and the challenge rules
// (port/src/common/challenge_pc.c) together with one click, then the rules
// one by one. The tab shows which preset the settings match now, or Custom.
#include "port/settings.h"
#include "port/tr.h"

#include <imgui.h>

#include <cstring>

extern "C" {
#include "port/gameplus.h"
}

namespace {

constexpr const char* kTab = "Challenges";
constexpr unsigned kPlay = PORT_SETTING_CHANGES_PLAY;

struct Value {
  const char* key;
  int value;
};

struct Preset {
  const char* name;
  const char* about;
  Value values[10];
};

// every preset sets every key below, so they don't mix
const Preset kPresets[] = {
    {"Original",
     "The game as it was made: no challenge rules, the original difficulty, no assists.",
     {{"difficulty", 0},
      {"ac_attack_assist", 0},
      {"ac_guard_assist", 0},
      {"gameover_retry", 0},
      {"challenge_ohko", 0},
      {"challenge_guard", 0},
      {"challenge_commands", 0},
      {"challenge_no_battle_items", 0}}},
    {"Hard",
     "Enemies have half again their HP and hit half again as hard.",
     {{"difficulty", 2},
      {"ac_attack_assist", 0},
      {"ac_guard_assist", 0},
      {"gameover_retry", 0},
      {"challenge_ohko", 0},
      {"challenge_guard", 0},
      {"challenge_commands", 0},
      {"challenge_no_battle_items", 0}}},
    {"Expert",
     "Enemies have double HP and hit twice as hard, attack commands are as with two Unsimplifiers, and there's no "
     "retry at a Game Over.",
     {{"difficulty", 3},
      {"difficulty_enemy_hp", 200},
      {"difficulty_enemy_attack", 200},
      {"ac_attack_assist", 0},
      {"ac_guard_assist", 0},
      {"gameover_retry", 0},
      {"challenge_ohko", 0},
      {"challenge_guard", 0},
      {"challenge_commands", 1},
      {"challenge_no_battle_items", 0}}},
    {"Glass cannon",
     "Enemies have half their HP, but hit three times as hard.",
     {{"difficulty", 3},
      {"difficulty_enemy_hp", 50},
      {"difficulty_enemy_attack", 300},
      {"ac_attack_assist", 0},
      {"ac_guard_assist", 0},
      {"gameover_retry", 0},
      {"challenge_ohko", 0},
      {"challenge_guard", 0},
      {"challenge_commands", 0},
      {"challenge_no_battle_items", 0}}},
    {"Superguards only",
     "A Guard does nothing: only a Superguard stops a hit. Items can't be used in battle.",
     {{"difficulty", 0},
      {"ac_attack_assist", 0},
      {"ac_guard_assist", 0},
      {"gameover_retry", 0},
      {"challenge_ohko", 0},
      {"challenge_guard", 1},
      {"challenge_commands", 0},
      {"challenge_no_battle_items", 1}}},
    {"One hit",
     "Any hit that does damage knocks Mario or the partner out. Superguard, dodge, or win first. Retry on Game Over "
     "is on, as a mercy.",
     {{"difficulty", 0},
      {"ac_attack_assist", 0},
      {"ac_guard_assist", 0},
      {"gameover_retry", 1},
      {"challenge_ohko", 1},
      {"challenge_guard", 0},
      {"challenge_commands", 0},
      {"challenge_no_battle_items", 0}}},
};

int* find(const char* key) {
  portGameplusDeclare();
  portChallengeDeclare();
  return portSettingsFind(key);
}

bool matches(const Preset& p) {
  for (const Value& v : p.values) {
    if (v.key == nullptr) {
      break;
    }
    const int* at = find(v.key);
    if (at == nullptr || *at != v.value) {
      return false;
    }
  }
  return true;
}

void apply(const Preset& p) {
  for (const Value& v : p.values) {
    if (v.key == nullptr) {
      break;
    }
    if (int* at = find(v.key)) {
      *at = v.value;
    }
  }
  portSettingsSave();
}

void presetsSection(int) {
  const Preset* current = nullptr;
  for (const Preset& p : kPresets) {
    if (matches(p)) {
      current = &p;
      break;
    }
  }
  ImGui::Text(portTr("Now: %s"), current != nullptr ? current->name : "Custom");
  ImGui::Spacing();
  const float width = ImGui::GetFontSize() * 9.f;
  for (const Preset& p : kPresets) {
    ImGui::PushID(p.name);
    const bool on = &p == current;
    if (on) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::Button(p.name, ImVec2(width, 0))) {
      apply(p);
    }
    if (on) {
      ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", p.about);
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("A preset sets the Accessibility tab's difficulty and assists, Retry on Game Over and the rules "
                     "below. Change any of them after: the preset shows as Custom. They apply from the next "
                     "battle."));
  ImGui::PopStyleColor();
}

struct ChallengeSettings {
  ChallengeSettings() {
    portChallengeDeclare();
    portSettingsAddSection(kTab, "Presets", nullptr, 0, presetsSection,
                           "challenge preset hard expert glass cannon one hit ko superguard difficulty");
    portSettingsAddToggle(kTab, "One-hit KO", "Any damaging enemy hit knocks Mario or the partner out.", kPlay,
                          "challenge_ohko", "challenge ohko one hit death");
    static const char* const kGuard[] = {"Allowed", "Superguards only", "No guarding at all"};
    portSettingsAddChoice(kTab, "Guarding", nullptr, kPlay, "challenge_guard", kGuard, 3,
                          "challenge guard superguard defend");
    static const char* const kCommands[] = {"Original", "Harder (as 2 Unsimplifiers)", "Hardest (as 3)"};
    portSettingsAddChoice(kTab, "Attack commands", "Narrower timing for Mario's and partners' attack commands.",
                          kPlay, "challenge_commands", kCommands, 3, "challenge action command timing unsimplifier");
    portSettingsAddToggle(kTab, "No items in battle", "The Items command can't be chosen in battle.", kPlay,
                          "challenge_no_battle_items", "challenge items battle");
  }
} sChallenges;

} // namespace
