// The Challenges tab's editor: your own rule sets (the difficulty, the assists,
// Retry on Game Over, the challenge rules and the Modes tab's remix modes),
// named, kept in <user>/challenges.txt and shared as codes, as settings
// presets are (presets.cpp):
//
//   PCRULE1:<base64url of "name|key=value;key=value...">
//
// A code holds the rules that differ from their defaults; putting one in
// place sets every rule below, the others back to their defaults.
#include "port/master_quest.h"
#include "port/playmodes.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "port/gameplus.h"
}

namespace {

constexpr const char* kTab = "Challenges";
constexpr const char* kPrefix = "PCRULE1:";

enum Kind { kToggle, kChoice, kSlider };

struct Rule {
  const char* key;
  const char* label;
  Kind kind;
  const char* const* choices;
  int count; // choices, or the slider's min..max below
  int min, max;
};

const char* const kDifficulty[] = {"Original", "Easy (enemy HP 75 %, attacks 50 %)",
                                   "Hard (enemy HP 150 %, attacks 150 %)", "Custom"};
const char* const kAssist[] = {"Off", "Wider windows", "Automatic"};
const char* const kGuard[] = {"Allowed", "Superguards only", "No guarding at all"};
const char* const kCommands[] = {"Original", "Harder (as 2 Unsimplifiers)", "Hardest (as 3)"};

const Rule kRules[] = {
    {"difficulty", "Difficulty", kChoice, kDifficulty, 4, 0, 0},
    {"difficulty_enemy_hp", "Enemy HP (custom)", kSlider, nullptr, 0, 25, 300},
    {"difficulty_enemy_attack", "Enemy attack (custom)", kSlider, nullptr, 0, 25, 300},
    {"ac_attack_assist", "Action commands: attacks", kChoice, kAssist, 3, 0, 0},
    {"ac_guard_assist", "Action commands: guards", kChoice, kAssist, 3, 0, 0},
    {"gameover_retry", "Retry on Game Over", kToggle, nullptr, 0, 0, 0},
    {"challenge_ohko", "One-hit KO", kToggle, nullptr, 0, 0, 0},
    {"challenge_guard", "Guarding", kChoice, kGuard, 3, 0, 0},
    {"challenge_commands", "Attack commands", kChoice, kCommands, 3, 0, 0},
    {"challenge_no_battle_items", "No items in battle", kToggle, nullptr, 0, 0, 0},
    {"remix_enemies", "Remix: enemies", kToggle, nullptr, 0, 0, 0},
    {"remix_badge_draft", "Remix: badge draft", kToggle, nullptr, 0, 0, 0},
    {"remix_seed", "Remix seed", kSlider, nullptr, 0, 0, 99999},
    {"hardcore_next", "The next new file is a hardcore file", kToggle, nullptr, 0, 0, 0},
    {"master_quest", "Master Quest", kToggle, nullptr, 0, 0, 0},
};
constexpr int kRuleCount = int(sizeof(kRules) / sizeof(kRules[0]));

struct RuleSet {
  std::string name;
  std::map<std::string, int> values; // only those not at their defaults
};

void declare() {
  portGameplusDeclare();
  portChallengeDeclare();
  portRemixDeclare();
  portMasterQuestDeclare();
}

int defaultOf(const char* key) {
  int def = 0;
  portSettingsDefault(key, &def);
  return def;
}

// --- codes ---

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encode64(const std::string& in) {
  std::string out;
  unsigned v = 0;
  int bits = 0;
  for (unsigned char c : in) {
    v = (v << 8) | c;
    bits += 8;
    while (bits >= 6) {
      out += kB64[(v >> (bits - 6)) & 63];
      bits -= 6;
    }
  }
  if (bits > 0) {
    out += kB64[(v << (6 - bits)) & 63];
  }
  return out;
}

bool decode64(const std::string& in, std::string& out) {
  unsigned v = 0;
  int bits = 0;
  out.clear();
  for (char c : in) {
    const char* at = c != 0 ? std::strchr(kB64, c) : nullptr;
    if (at == nullptr) {
      return false;
    }
    v = (v << 6) | unsigned(at - kB64);
    bits += 6;
    if (bits >= 8) {
      out += char((v >> (bits - 8)) & 0xFF);
      bits -= 8;
    }
  }
  return true;
}

const Rule* ruleOf(const std::string& key) {
  for (const Rule& r : kRules) {
    if (key == r.key) {
      return &r;
    }
  }
  return nullptr;
}

std::string codeOf(const RuleSet& s) {
  std::string body = s.name + "|";
  bool first = true;
  for (const auto& [key, value] : s.values) {
    body += (first ? "" : ";") + key + "=" + std::to_string(value);
    first = false;
  }
  return std::string(kPrefix) + encode64(body);
}

// false: not a rule set's code
bool parse(const std::string& typed, RuleSet& out) {
  std::string code;
  for (char c : typed) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      code += c;
    }
  }
  const size_t start = code.find(kPrefix);
  std::string body;
  if (start == std::string::npos || !decode64(code.substr(start + std::strlen(kPrefix)), body)) {
    return false;
  }
  const size_t bar = body.find('|');
  if (bar == std::string::npos) {
    return false;
  }
  out.name = body.substr(0, bar).substr(0, 48);
  out.values.clear();
  size_t at = bar + 1;
  while (at < body.size()) {
    size_t end = body.find(';', at);
    if (end == std::string::npos) {
      end = body.size();
    }
    const std::string pair = body.substr(at, end - at);
    at = end + 1;
    const size_t eq = pair.find('=');
    if (eq == std::string::npos) {
      return false;
    }
    const std::string key = pair.substr(0, eq), value = pair.substr(eq + 1);
    if (value.empty() || value.find_first_not_of("-0123456789") != std::string::npos) {
      return false;
    }
    if (ruleOf(key) != nullptr) { // a rule this version doesn't know is left out
      out.values[key] = std::atoi(value.c_str());
    }
  }
  return true;
}

// --- the list, <user>/challenges.txt: a code a line ---

std::vector<RuleSet> sSets;
bool sRead;

std::string listPath() { return std::string(portSettingsUserPath()) + "/challenges.txt"; }

void readList() {
  if (sRead) {
    return;
  }
  sRead = true;
  size_t size = 0;
  char* text = static_cast<char*>(SDL_LoadFile(listPath().c_str(), &size));
  if (text == nullptr) {
    return;
  }
  const std::string all(text, size);
  SDL_free(text);
  size_t at = 0;
  while (at < all.size()) {
    size_t end = all.find('\n', at);
    if (end == std::string::npos) {
      end = all.size();
    }
    RuleSet s;
    if (parse(all.substr(at, end - at), s)) {
      sSets.push_back(s);
    }
    at = end + 1;
  }
}

void writeList() {
  std::string text = "# Paper Cut challenges: Challenges > Your challenges (a code a line)\n";
  for (const RuleSet& s : sSets) {
    text += codeOf(s) + "\n";
  }
  SDL_SaveFile(listPath().c_str(), text.data(), text.size());
}

// --- in place ---

RuleSet fromSettings(const std::string& name) {
  RuleSet s;
  s.name = name;
  for (const Rule& r : kRules) {
    const int* v = portSettingsFind(r.key);
    if (v != nullptr && *v != defaultOf(r.key)) {
      s.values[r.key] = *v;
    }
  }
  return s;
}

void putInPlace(const RuleSet& s) {
  for (const Rule& r : kRules) {
    if (int* v = portSettingsFind(r.key)) {
      const auto at = s.values.find(r.key);
      int want = at != s.values.end() ? at->second : defaultOf(r.key);
      const int lo = r.kind == kSlider ? r.min : 0;
      const int hi = r.kind == kSlider ? r.max : r.kind == kChoice ? r.count - 1 : 1;
      *v = want < lo ? lo : want > hi ? hi : want;
    }
  }
  portSettingsSave();
}

bool matches(const RuleSet& s) {
  for (const Rule& r : kRules) {
    const int* v = portSettingsFind(r.key);
    const auto at = s.values.find(r.key);
    if (v != nullptr && *v != (at != s.values.end() ? at->second : defaultOf(r.key))) {
      return false;
    }
  }
  return true;
}

// --- the page ---

RuleSet sEdit;
bool sEditing;
char sName[49] = "My challenge";
char sCode[1024];
std::string sMessage;

std::string summary(const RuleSet& s) {
  std::string out;
  for (const auto& [key, value] : s.values) {
    const Rule* r = ruleOf(key);
    if (r == nullptr) {
      continue;
    }
    out += out.empty() ? "" : ", ";
    out += portTr(r->label);
    if (r->kind == kChoice && value >= 0 && value < r->count) {
      out += std::string(": ") + portTr(r->choices[value]);
    } else if (r->kind == kSlider) {
      out += ": " + std::to_string(value);
    }
  }
  return out.empty() ? portTr("The game as it was made.") : out;
}

void editor() {
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::InputText(portTrLabel("Name##rules"), sName, sizeof(sName));
  for (const Rule& r : kRules) {
    ImGui::PushID(r.key);
    const auto at = sEdit.values.find(r.key);
    int v = at != sEdit.values.end() ? at->second : defaultOf(r.key);
    bool changed = false;
    if (r.kind == kToggle) {
      bool on = v != 0;
      changed = ImGui::Checkbox(portTrLabel(r.label), &on);
      v = on ? 1 : 0;
    } else if (r.kind == kChoice) {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
      if (ImGui::BeginCombo(portTrLabel(r.label), portTr(r.choices[v >= 0 && v < r.count ? v : 0]))) {
        for (int i = 0; i < r.count; i++) {
          if (ImGui::Selectable(portTr(r.choices[i]), i == v)) {
            v = i;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
    } else {
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
      changed = ImGui::SliderInt(portTrLabel(r.label), &v, r.min, r.max);
    }
    if (changed) {
      if (v == defaultOf(r.key)) {
        sEdit.values.erase(r.key);
      } else {
        sEdit.values[r.key] = v;
      }
    }
    ImGui::PopID();
  }
  sEdit.name = sName;
  if (ImGui::Button(portTrLabel("Save to your challenges##rules"))) {
    readList();
    bool replaced = false;
    for (RuleSet& s : sSets) {
      if (s.name == sEdit.name) {
        s = sEdit;
        replaced = true;
      }
    }
    if (!replaced) {
      sSets.push_back(sEdit);
    }
    writeList();
    sEditing = false;
    sMessage = portTr("Saved.");
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Cancel##rules"))) {
    sEditing = false;
  }
}

void section(int) {
  declare();
  readList();
  ImGui::TextWrapped("%s", portTr("Your own rule sets: the difficulty, the assists, the challenge rules and the remix "
                                  "modes together, named, and shared as codes."));
  if (sEditing) {
    editor();
  } else {
    if (ImGui::Button(portTrLabel("New, from the settings now##rules"))) {
      sEdit = fromSettings("");
      std::snprintf(sName, sizeof(sName), "%s", portTr("My challenge"));
      sEditing = true;
    }
  }
  ImGui::Spacing();
  int remove = -1;
  for (int i = 0; i < int(sSets.size()); i++) {
    const RuleSet& s = sSets[i];
    ImGui::PushID(i);
    const bool on = matches(s);
    ImGui::Text("%s%s", s.name.c_str(), on ? portTr(" (in place)") : "");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", summary(s).c_str());
    ImGui::PopStyleColor();
    if (ImGui::SmallButton(portTrLabel("Put in place##rules"))) {
      putInPlace(s);
      sMessage = portTr("Put in place: it applies from the next battle.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Copy the code##rules"))) {
      SDL_SetClipboardText(codeOf(s).c_str());
      sMessage = portTr("The code is copied: paste it anywhere to share it.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Edit##rules"))) {
      sEdit = s;
      std::snprintf(sName, sizeof(sName), "%s", s.name.c_str());
      sEditing = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Remove##rules"))) {
      remove = i;
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    sSets.erase(sSets.begin() + remove);
    writeList();
  }
  ImGui::Spacing();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  ImGui::InputTextWithHint(portTrLabel("##rulecode"), "PCRULE1:...", sCode, sizeof(sCode));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Paste##rules"))) {
    if (char* clip = SDL_GetClipboardText()) {
      std::snprintf(sCode, sizeof(sCode), "%s", clip);
      SDL_free(clip);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Add from the code##rules"))) {
    RuleSet s;
    if (parse(sCode, s)) {
      if (s.name.empty()) {
        s.name = portTr("Shared challenge");
      }
      sSets.push_back(s);
      writeList();
      sCode[0] = 0;
      sMessage = portTr("Added to your challenges.");
    } else {
      sMessage = portTr("That isn't a challenge code.");
    }
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

struct Register {
  Register() {
    declare();
    portSettingsAddSection(kTab, "Your challenges", nullptr, PORT_SETTING_CHANGES_PLAY, section,
                           "challenge editor rules code share custom rule set");
  }
} sRegister;

} // namespace
