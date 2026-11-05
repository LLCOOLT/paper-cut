// Master Quest (port/include/port/master_quest.h, docs/MASTER_QUEST.md): on the
// Modes tab, the toggle and which set; on the Challenges tab, the set editor:
// your own sets, written in the sets' text, checked as you type, kept in
// <user>/master_quest_sets.txt and shared as PCMQ1: codes.
#include "port/master_quest.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using portui::hint;

namespace {

// one parse for the page (a set is about 30 KB)
PortMqSet sParsed;

std::string countsOf(const char* text, bool* ok) {
  char err[200];
  *ok = portMasterQuestParse(text, &sParsed, err, sizeof(err)) != 0;
  if (!*ok) {
    return err;
  }
  char line[160];
  std::snprintf(line, sizeof(line), portTr("%s: %d fight lines, %d enemy lines."), sParsed.name,
                sParsed.fightCount, sParsed.enemyCount);
  return line;
}

int* setting() {
  portMasterQuestDeclare();
  return portSettingsFind("master_quest_set");
}

// --- Modes: which set ---

void setSection(int) {
  int* set = setting();
  if (set == nullptr) {
    return;
  }
  const char* current = portTr("The curated set");
  const int custom = portMasterQuestCustomCount();
  for (int i = 0; i < custom; i++) {
    if (portMasterQuestHash(portMasterQuestCustomText(i)) == static_cast<unsigned>(*set)) {
      current = portMasterQuestCustomName(i);
    }
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.f);
  if (ImGui::BeginCombo(portTrLabel("Master Quest set##mq"), current)) {
    if (ImGui::Selectable(portTr("The curated set"), *set == 0)) {
      *set = 0;
      portSettingsSave();
    }
    for (int i = 0; i < custom; i++) {
      const unsigned h = portMasterQuestHash(portMasterQuestCustomText(i));
      ImGui::PushID(i);
      if (ImGui::Selectable(portMasterQuestCustomName(i), static_cast<unsigned>(*set) == h)) {
        *set = static_cast<int>(h);
        portSettingsSave();
        portMasterQuestReload();
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  hint(portTr("The curated set remixes the whole story, the prologue to chapter 8, and the Pit of 100 Trials. Make "
              "more on the Challenges tab. Online, the host's set is played."));
}

// --- the Start menu's form (port/include/port/pause_forms.h): the set, as the page's choice. The sets
// are this device's own files, so online and in replays it is one dimmed row ---

int setRows(PortPauseRow* out, int max) {
  int* set = setting();
  if (max < 1 || set == nullptr) {
    return 0;
  }
  PortPauseRow r = portPauseRow("Master Quest set", "The curated set remixes the whole story, the prologue to chapter "
                                "8, and the Pit of 100 Trials. Make more on the Challenges tab. Online, the host's set "
                                "is played.", "The curated set", 0);
  for (int i = 0; i < portMasterQuestCustomCount(); i++) {
    if (portMasterQuestHash(portMasterQuestCustomText(i)) == static_cast<unsigned>(*set)) {
      // a set's own name, for the game's font: Latin-1 on the US and EU releases, ASCII on the JP one
      size_t n = 0;
      for (const unsigned char* p = reinterpret_cast<const unsigned char*>(portMasterQuestCustomName(i));
           *p != 0 && n + 1 < sizeof(r.value); p++) {
        if (*p < 0x80) {
          r.value[n++] = *p == '<' ? '(' : *p == '>' ? ')' : char(*p);
        } else if (PORT_GAME_ID[3] != 'J' && (*p == 0xC2 || *p == 0xC3) && p[1] != 0) {
          r.value[n++] = char(((*p & 0x03) << 6) | (p[1] & 0x3F));
          p++;
        } else if ((*p & 0xC0) == 0xC0) {
          r.value[n++] = '?';
        }
      }
      r.value[n] = 0;
      r.valueTr = nullptr;
    }
  }
  out[0] = r;
  return 1;
}

const char* setAct(int id, int dir) {
  int* set = setting();
  if (id != 0 || set == nullptr) {
    return nullptr;
  }
  // the curated set (0), then each of yours: the next or the one before
  const int count = portMasterQuestCustomCount() + 1;
  int at = 0;
  for (int i = 0; i < count - 1; i++) {
    if (portMasterQuestHash(portMasterQuestCustomText(i)) == static_cast<unsigned>(*set)) {
      at = i + 1;
    }
  }
  at = (at + (dir < 0 ? count - 1 : 1)) % count;
  *set = at == 0 ? 0 : static_cast<int>(portMasterQuestHash(portMasterQuestCustomText(at - 1)));
  portSettingsSave();
  if (at != 0) {
    portMasterQuestReload();
  }
  return nullptr;
}

const PortPauseForm kSetForm = {"Modes", "Master Quest set", setRows, setAct, PORT_PAUSE_FORM_OWN};

// --- Challenges: the editor ---

constexpr size_t kTextSize = PORT_MQ_TEXT_MAX;
std::vector<char> sText(kTextSize, 0);
bool sEditing;
std::string sStatus;
bool sStatusOk;
char sCode[4096];
std::string sMessage;

void edit(const char* text) {
  std::snprintf(sText.data(), kTextSize, "%s", text);
  sStatus = countsOf(sText.data(), &sStatusOk);
  sEditing = true;
}

void formatHelp() {
  if (!ImGui::TreeNode(portTrLabel("How a set is written##mq"))) {
    return;
  }
  hint(portTr("A line each; # starts a note.\n"
              "name <the set's name>\n"
              "fight <area> <lead> = <enemy> <enemy> ...: in that area (hei, gon, win...; jon is the Pit), a fight "
              "led by <lead> becomes one of its lines (1-5 enemies, only the area's own).\n"
              "enemy <area or *> <enemy> <changes>: hp+N, hp*P (percent), atk+N, "
              "hit=<status>:<chance>:<turns>[:<strength>] (sleep stop dizzy poison confuse burn freeze tiny "
              "attack_down defense_down slow), start=<status>:<turns>[:<strength>] (dodgy huge attack_up defense_up "
              "charge invisible fast payback hold_fast hp_regen electric), element=fire|ice|explosion|electric, "
              "pierce."));
  hint(portTr("variant=PCFOE1:...: an enemy variant's code from the Enemy maker, for that enemy (its stats, colour, "
              "size, borrowed move, coins, name and Tattle), with the line's other changes."));
  if (ImGui::TreeNode(portTrLabel("The enemies a set can use##mq"))) {
    std::string names;
    for (int i = 0; i < portMasterQuestUnitCount(); i++) {
      names += (i > 0 ? " " : "");
      names += portMasterQuestUnitNameAt(i, nullptr);
    }
    hint(names.c_str());
    ImGui::TreePop();
  }
  ImGui::TreePop();
}

void editor() {
  if (ImGui::InputTextMultiline("##mqtext", sText.data(), kTextSize,
                                ImVec2(-1.0f, ImGui::GetTextLineHeight() * 18.0f), ImGuiInputTextFlags_AllowTabInput)) {
    sStatus = countsOf(sText.data(), &sStatusOk);
  }
  ImGui::PushStyleColor(ImGuiCol_Text, sStatusOk ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                                                 : ImVec4(0.95f, 0.45f, 0.35f, 1.0f));
  ImGui::TextWrapped("%s", sStatus.c_str());
  ImGui::PopStyleColor();
  ImGui::BeginDisabled(!sStatusOk);
  if (ImGui::Button(portTrLabel("Save to your sets##mq"))) {
    char err[200] = {0};
    if (portMasterQuestCustomSave(sText.data(), err, sizeof(err))) {
      portMasterQuestReload();
      sEditing = false;
      sMessage = portTr("Saved. Choose it on the Modes tab (Master Quest set).");
    } else {
      sMessage = err;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Cancel##mq"))) {
    sEditing = false;
  }
  formatHelp();
}

void editorSection(int) {
  ImGui::TextWrapped("%s", portTr("Master Quest sets of your own: which enemies each area's fights have and how they "
                                  "attack, written as text, checked as you type, and shared as codes."));
  if (sEditing) {
    editor();
  } else {
    if (ImGui::Button(portTrLabel("Start from the curated set##mq"))) {
      std::string text = portMasterQuestCuratedText();
      const size_t at = text.find("name Master Quest");
      if (at != std::string::npos) {
        text.replace(at, std::strlen("name Master Quest"), std::string("name ") + portTr("My Master Quest"));
      }
      edit(text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Start an empty set##mq"))) {
      edit((std::string("name ") + portTr("My Master Quest") + "\n").c_str());
    }
  }
  ImGui::Spacing();
  int remove = -1;
  int* set = setting();
  for (int i = 0; i < portMasterQuestCustomCount(); i++) {
    const char* text = portMasterQuestCustomText(i);
    const unsigned h = portMasterQuestHash(text);
    ImGui::PushID(i);
    const bool inUse = set != nullptr && static_cast<unsigned>(*set) == h;
    ImGui::Text("%s%s", portMasterQuestCustomName(i), inUse ? portTr(" (in play)") : "");
    if (ImGui::SmallButton(portTrLabel("Play this set##mq"))) {
      if (set != nullptr) {
        *set = static_cast<int>(h);
        if (int* on = portSettingsFind("master_quest")) {
          *on = 1;
        }
        portSettingsSave();
        portMasterQuestReload();
        sMessage = portTr("Master Quest is on with this set, from the next room.");
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Copy the code##mq"))) {
      if (char* code = portMasterQuestCode(text)) {
        SDL_SetClipboardText(code);
        std::free(code);
        sMessage = portTr("The code is copied: paste it anywhere to share it.");
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Edit##mq"))) {
      edit(text);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Remove##mq"))) {
      remove = i;
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    if (set != nullptr && static_cast<unsigned>(*set) == portMasterQuestHash(portMasterQuestCustomText(remove))) {
      *set = 0;
      portSettingsSave();
    }
    portMasterQuestCustomRemove(remove);
    portMasterQuestReload();
  }
  ImGui::Spacing();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  ImGui::InputTextWithHint("##mqcode", "PCMQ1:...", sCode, sizeof(sCode));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Paste##mq"))) {
    if (char* clip = SDL_GetClipboardText()) {
      std::snprintf(sCode, sizeof(sCode), "%s", clip);
      SDL_free(clip);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Add from the code##mq"))) {
    char* text = portMasterQuestFromCode(sCode);
    char err[200] = {0};
    if (text == nullptr) {
      sMessage = portTr("That isn't a Master Quest code.");
    } else if (portMasterQuestCustomSave(text, err, sizeof(err))) {
      portMasterQuestReload();
      sCode[0] = 0;
      sMessage = portTr("Added to your sets.");
    } else {
      sMessage = err;
    }
    std::free(text);
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

struct Register {
  Register() {
    portMasterQuestDeclare();
    portSettingsAddToggle("Modes", "Master Quest",
                          "The story's fights remixed: more enemies, mixed from each area's own, and attacks with the "
                          "game's own statuses, harder chapter by chapter. Bosses stay as they are. From the next room.",
                          PORT_SETTING_CHANGES_PLAY, "master_quest", "master quest remix harder enemies formations hard mode");
    portPauseAddForm(&kSetForm);
    portSettingsAddSection("Modes", "Master Quest set", nullptr, PORT_SETTING_CHANGES_PLAY, setSection,
                           "master quest set curated custom");
    portSettingsAddSection("Challenges", "Master Quest sets", nullptr, PORT_SETTING_CHANGES_PLAY, editorSection,
                           "master quest editor sets fights enemies code share make");
  }
} sRegister;

} // namespace
