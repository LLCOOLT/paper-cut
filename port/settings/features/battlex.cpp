// Battle extras on the settings page (port/include/port/battlex.h,
// docs/BATTLE_EXTRAS.md): the battle grades, the Paper Cut badge set and
// Zess T.'s new dishes (Enhancements), the badge maker (Enhancements), the
// battle puzzles (Modes) and the Enemy maker (Challenges); the grade after a
// battle and the puzzle's goal over the picture.
//
// Codes, as presets and challenges are shared:
//   PCBADGE1:<base64url of "name|effect,effect,...|icon">  (the game's badge ids)
//   PCFOE1:...                                           (enemy_variant.h)
// The badge maker's effects are settings (the online host sends them); the
// names are kept in <user>/badges.txt, the Enemy maker's list in
// <user>/enemies.txt (a code a line); online, the host's list is the
// session's (portBattlexSessionGet/Put, port/include/port/online_session.h).
#include "port/battlex.h"
#include "port/companion_menu.h"
#include "port/enemy_variant.h"
#include "port/online.h"
#include "port/online_session.h"
#include "port/pause_forms.h"
#include "port/practice_tools.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// the game's ids this page needs (the settings library doesn't see the game's headers):
// mario_pouch.h's badge range and battle_unit.h's kinds
constexpr int BADGE_MIN = 0xF0;               // ITEM_POWER_JUMP
constexpr int BADGE_MAX = 0x153;              // ITEM_SUPER_CHARGE_P + 1
constexpr int ITEM_POWER_PLUS = 0x104;
constexpr int ITEM_UNUSED_DEFEND_BADGE = 0x14F;
constexpr int ITEM_UNUSED_DEFEND_BADGE_P = 0x150;
constexpr int UNIT_GOOMBA = 1;
constexpr int UNIT_MAX = 0xE7;

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

std::string stripped(const char* typed) {
  std::string s;
  for (const char* p = typed; *p; p++) {
    if (*p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
      s += *p;
    }
  }
  return s;
}

std::string readText(const std::string& path) {
  size_t size = 0;
  char* text = static_cast<char*>(SDL_LoadFile(path.c_str(), &size));
  if (text == nullptr) {
    return {};
  }
  std::string all(text, size);
  SDL_free(text);
  return all;
}

std::vector<std::string> lines(const std::string& all) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at < all.size()) {
    size_t end = all.find('\n', at);
    if (end == std::string::npos) {
      end = all.size();
    }
    std::string line = all.substr(at, end - at);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty() && line[0] != '#') {
      out.push_back(line);
    }
    at = end + 1;
  }
  return out;
}

// --- the game's badges, for the choices ---

std::string badgeName(int id) {
  char buf[64];
  portPracticeItemName(id, buf, sizeof(buf));
  if (std::strcmp(buf, "?") == 0) {
    std::snprintf(buf, sizeof(buf), "%s %d", portTr("Badge"), id - BADGE_MIN + 1);
  }
  return buf;
}

// the badges that can be part of a made one: every real badge (not the unused Defend ones)
bool usableBadge(int id) {
  return id >= BADGE_MIN && id < BADGE_MAX && id != ITEM_UNUSED_DEFEND_BADGE && id != ITEM_UNUSED_DEFEND_BADGE_P;
}

// --- the badge maker ---

constexpr const char* kBadgePrefix = "PCBADGE1:";

struct MadeBadge {
  std::string name;
  short grants[PORT_BADGE_GRANTS] = {};
  int icon = 0;
};

std::string badgesPath() { return std::string(portSettingsUserPath()) + "/badges.txt"; }

std::string badgeCode(const MadeBadge& b) {
  std::string body = b.name + "|";
  bool first = true;
  for (short g : b.grants) {
    if (g != 0) {
      body += (first ? "" : ",") + std::to_string(g);
      first = false;
    }
  }
  body += "|" + std::to_string(b.icon);
  return kBadgePrefix + encode64(body);
}

bool parseBadge(const char* typed, MadeBadge& out) {
  const std::string code = stripped(typed);
  const size_t start = code.find(kBadgePrefix);
  std::string body;
  if (start == std::string::npos || !decode64(code.substr(start + std::strlen(kBadgePrefix)), body)) {
    return false;
  }
  const size_t a = body.find('|'), b = body.rfind('|');
  if (a == std::string::npos || b == a) {
    return false;
  }
  out = MadeBadge{};
  out.name = body.substr(0, a).substr(0, 30);
  std::string list = body.substr(a + 1, b - a - 1);
  int n = 0;
  size_t at = 0;
  while (at < list.size() && n < PORT_BADGE_GRANTS) {
    size_t end = list.find(',', at);
    if (end == std::string::npos) {
      end = list.size();
    }
    const int id = std::atoi(list.substr(at, end - at).c_str());
    if (usableBadge(id)) {
      out.grants[n++] = short(id);
    }
    at = end + 1;
  }
  out.icon = std::atoi(body.substr(b + 1).c_str());
  if (!usableBadge(out.icon)) {
    out.icon = n > 0 ? out.grants[0] : ITEM_POWER_PLUS;
  }
  return n > 0;
}

bool sNamesRead;

// <user>/badges.txt: "<slot> <code>" a line (the names; the effects are settings)
void readNames() {
  if (sNamesRead) {
    return;
  }
  sNamesRead = true;
  for (const std::string& line : lines(readText(badgesPath()))) {
    const int slot = std::atoi(line.c_str());
    MadeBadge b;
    const size_t sp = line.find(' ');
    if (sp != std::string::npos && slot >= 1 && slot <= PORT_BADGE_CUSTOM_COUNT && parseBadge(line.c_str() + sp, b)) {
      portBadgeMakerSetName(slot - 1, b.name.c_str());
    }
  }
}

MadeBadge slotBadge(int slot) {
  MadeBadge b;
  portBadgeMakerGet(slot, b.grants, &b.icon);
  b.name = portBadgeMakerName(slot);
  return b;
}

void writeNames() {
  std::string text = "# Paper Cut badge maker: Enhancements > Badge maker (a slot and its code a line)\n";
  for (int i = 0; i < PORT_BADGE_CUSTOM_COUNT; i++) {
    const MadeBadge b = slotBadge(i);
    if (b.grants[0] != 0) {
      text += std::to_string(i + 1) + " " + badgeCode(b) + "\n";
    }
  }
  SDL_SaveFile(badgesPath().c_str(), text.data(), text.size());
}

void putSlot(int slot, const MadeBadge& b) {
  portBadgeMakerSet(slot, b.grants, b.icon);
  portBadgeMakerSetName(slot, b.name.c_str());
  portSettingsSave();
  writeNames();
}

int sEditSlot = -1;
MadeBadge sEdit;
char sBadgeName[31];
char sBadgeCode[512];
std::string sBadgeMessage;

bool badgeCombo(const char* label, int* id, bool allowNone) {
  bool changed = false;
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  const std::string shown = *id != 0 ? badgeName(*id) : portTr("(none)");
  if (ImGui::BeginCombo(label, shown.c_str())) {
    if (allowNone && ImGui::Selectable(portTr("(none)"), *id == 0)) {
      *id = 0;
      changed = true;
    }
    for (int b = BADGE_MIN; b < BADGE_MAX; b++) {
      if (usableBadge(b) && ImGui::Selectable((badgeName(b) + "##" + std::to_string(b)).c_str(), *id == b)) {
        *id = b;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

std::string badgeSummary(const MadeBadge& b) {
  std::string out;
  for (short g : b.grants) {
    if (g != 0) {
      out += (out.empty() ? "" : " + ") + badgeName(g);
    }
  }
  char cost[96];
  const int bp = portBadgeMakerCost(b.grants);
  std::snprintf(cost, sizeof(cost), portTr(" (%d BP, %d coins)"), bp, portBadgeMakerPrice(bp));
  return out + cost;
}

void badgeMakerSection(int) {
  readNames();
  portBattlexRefresh();
  ImGui::TextWrapped("%s", portTr("Make a badge out of up to four of the game's badges: it does all they do, for the "
                                  "BP they cost together (one less for each past the first). The Paper Cut "
                                  "merchant in Rogueport's plaza sells it."));
  if (sEditSlot >= 0) {
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
    ImGui::InputText(portTrLabel("Name##badgemaker"), sBadgeName, sizeof(sBadgeName));
    for (int i = 0; i < PORT_BADGE_GRANTS; i++) {
      int id = sEdit.grants[i];
      char label[48];
      std::snprintf(label, sizeof(label), "%s %d##grant%d", portTr("Effect"), i + 1, i);
      if (badgeCombo(label, &id, i > 0)) {
        sEdit.grants[i] = short(id);
      }
    }
    // keep them packed
    short packed[PORT_BADGE_GRANTS] = {};
    for (int i = 0, n = 0; i < PORT_BADGE_GRANTS; i++) {
      if (sEdit.grants[i] != 0) {
        packed[n++] = sEdit.grants[i];
      }
    }
    std::memcpy(sEdit.grants, packed, sizeof(packed));
    int icon = sEdit.icon;
    if (badgeCombo(portTrLabel("Looks like##badgemaker"), &icon, false)) {
      sEdit.icon = icon;
    }
    ImGui::TextDisabled("%s", badgeSummary(sEdit).c_str());
    ImGui::BeginDisabled(sEdit.grants[0] == 0);
    if (ImGui::Button(portTrLabel("Save##badgemaker"))) {
      sEdit.name = sBadgeName;
      putSlot(sEditSlot, sEdit);
      sEditSlot = -1;
      sBadgeMessage = portTr("Saved: the merchant has it now.");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Cancel##badgemaker"))) {
      sEditSlot = -1;
    }
    return;
  }
  for (int i = 0; i < PORT_BADGE_CUSTOM_COUNT; i++) {
    const MadeBadge b = slotBadge(i);
    ImGui::PushID(i);
    if (b.grants[0] == 0) {
      ImGui::TextDisabled("%d. %s", i + 1, portTr("(empty)"));
      ImGui::SameLine();
      if (ImGui::SmallButton(portTrLabel("Make##badgemaker"))) {
        sEditSlot = i;
        sEdit = MadeBadge{};
        sEdit.grants[0] = ITEM_POWER_PLUS;
        sEdit.icon = ITEM_POWER_PLUS;
        std::snprintf(sBadgeName, sizeof(sBadgeName), "%s", portTr("My badge"));
      }
    } else {
      ImGui::Text("%d. %s", i + 1, b.name.empty() ? portTr("(no name)") : b.name.c_str());
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s", badgeSummary(b).c_str());
      ImGui::PopStyleColor();
      if (ImGui::SmallButton(portTrLabel("Edit##badgemaker"))) {
        sEditSlot = i;
        sEdit = b;
        std::snprintf(sBadgeName, sizeof(sBadgeName), "%s", b.name.c_str());
      }
      ImGui::SameLine();
      if (ImGui::SmallButton(portTrLabel("Copy the code##badgemaker"))) {
        SDL_SetClipboardText(badgeCode(b).c_str());
        sBadgeMessage = portTr("The code is copied: paste it anywhere to share it.");
      }
      ImGui::SameLine();
      if (ImGui::SmallButton(portTrLabel("Clear##badgemaker"))) {
        putSlot(i, MadeBadge{});
      }
    }
    ImGui::PopID();
  }
  ImGui::Spacing();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  ImGui::InputTextWithHint("##badgecode", "PCBADGE1:...", sBadgeCode, sizeof(sBadgeCode));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Paste##badgemaker"))) {
    if (char* clip = SDL_GetClipboardText()) {
      std::snprintf(sBadgeCode, sizeof(sBadgeCode), "%s", clip);
      SDL_free(clip);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Add from the code##badgemaker"))) {
    MadeBadge b;
    int slot = -1;
    for (int i = 0; i < PORT_BADGE_CUSTOM_COUNT && slot < 0; i++) {
      if (slotBadge(i).grants[0] == 0) {
        slot = i;
      }
    }
    if (!parseBadge(sBadgeCode, b)) {
      sBadgeMessage = portTr("That isn't a badge code.");
    } else if (slot < 0) {
      sBadgeMessage = portTr("Every slot is full: clear one first.");
    } else {
      putSlot(slot, b);
      sBadgeCode[0] = 0;
      sBadgeMessage = portTr("Added: the merchant has it now.");
    }
  }
  if (portOnlineActive()) {
    ImGui::TextDisabled("%s", portTr("Online, made badges are called Custom Badge 1-8 and do what the host's do."));
  }
  if (!sBadgeMessage.empty()) {
    ImGui::TextWrapped("%s", sBadgeMessage.c_str());
  }
}

// --- the Paper Cut badge set and dishes, listed ---

void badgeSetSection(int) {
  portItemsInit();
  if (ImGui::TreeNode(portTrLabel("The badges##pcset"))) {
    for (int i = 0; i < PORT_BADGE_SET_COUNT; i++) {
      const int id = PORT_BADGE_SET_MIN + i;
      const short* g;
      MadeBadge b;
      const int n = portBadgeGrants(id, &g);
      for (int k = 0; k < n; k++) {
        b.grants[k] = g[k];
      }
      ImGui::Text("%s", portTr(portItemName(id)));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s %s", portTr(portItemDescription(id)), badgeSummary(b).c_str());
      ImGui::PopStyleColor();
    }
    ImGui::TreePop();
  }
}

// --- battle puzzles ---

std::string goalText(const PortPuzzleInfo* p) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), p->maxTurns == 1 ? portTr("Win in 1 turn") : portTr("Win in %d turns"),
                p->maxTurns);
  std::string out = buf;
  if (p->flags & PORT_PUZZLE_NO_ITEMS) {
    out += portTr(", without items");
  }
  if (p->flags & PORT_PUZZLE_NO_DAMAGE) {
    out += portTr(", without being hurt");
  }
  if (p->flags & PORT_PUZZLE_NO_JUMP) {
    out += portTr(", without jumping");
  }
  if (p->flags & PORT_PUZZLE_NO_HAMMER) {
    out += portTr(", without the hammer");
  }
  return out + ".";
}

std::string sPuzzleMessage;
unsigned sPuzzleSeen;
bool sShowHints;
constexpr const char* kFromStartMenu =
    "Online, start it from the Start menu (Settings, Modes), so every game of the session starts it together.";

void startPuzzle(int i, int daily) {
  const char* why = nullptr;
  if (portOnlineActive()) {
    sPuzzleMessage = portTr(kFromStartMenu); // (this page is one game's alone)
  } else if (!portPuzzleStart(i, daily, &why)) {
    sPuzzleMessage = portTr(why);
  } else {
    sPuzzleMessage.clear();
  }
}

void puzzlesSection(int inGame) {
  int puzzle, solved, turns;
  const unsigned seen = portPuzzleLastResult(&puzzle, &solved, &turns);
  if (seen != sPuzzleSeen) {
    sPuzzleSeen = seen;
    char buf[160];
    const PortPuzzleInfo* p = portPuzzleInfo(puzzle);
    std::snprintf(buf, sizeof(buf), solved ? portTr("Solved \"%s\" in %d turns!") : portTr("\"%s\" not solved: try again."),
                  p != nullptr ? portTr(p->name) : "", turns);
    sPuzzleMessage = buf;
  }
  ImGui::TextWrapped("%s", portTr("Fixed battles to win under a condition, like chess puzzles: Mario's level, partner, "
                                  "badges and items are set for each. Mario comes back as he was, where he was, "
                                  "won or lost."));
  if (portOnlineActive()) {
    ImGui::TextDisabled("%s", portTr(kFromStartMenu));
  }
  ImGui::BeginDisabled(!inGame || portOnlineActive());
  const int daily = portPuzzleDaily();
  const PortPuzzleInfo* d = portPuzzleInfo(daily);
  if (ImGui::Button(portTrLabel("Today's puzzle##puzzles"))) {
    startPuzzle(daily, 1);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%d. %s: %s", daily + 1, portTr(d->name), goalText(d).c_str());
  ImGui::EndDisabled();
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Load a file to play them."));
  }
  ImGui::Checkbox(portTrLabel("Show the hints##puzzles"), &sShowHints);
  int solvedCount = 0;
  for (int i = 0; i < portPuzzleCount(); i++) {
    solvedCount += portPuzzleSolved(i, nullptr);
  }
  ImGui::Text(portTr("%d of %d solved"), solvedCount, portPuzzleCount());
  if (ImGui::BeginChild("##puzzlelist", ImVec2(0, ImGui::GetFontSize() * 18.f), ImGuiChildFlags_Borders)) {
    for (int i = 0; i < portPuzzleCount(); i++) {
      const PortPuzzleInfo* p = portPuzzleInfo(i);
      int best = 0;
      const bool done = portPuzzleSolved(i, &best);
      ImGui::PushID(i);
      ImGui::BeginDisabled(!inGame || portOnlineActive());
      if (ImGui::SmallButton(portTrLabel("Play##puzzle"))) {
        startPuzzle(i, 0);
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::Text("%d. %s %s%s", i + 1, portTr(p->name), std::string(size_t(p->difficulty), '*').c_str(),
                  done ? portTr(" (solved)") : "");
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s: %s", portTr(p->foes), goalText(p).c_str());
      if (sShowHints) {
        ImGui::TextWrapped("%s", portTr(p->hint));
      }
      ImGui::PopStyleColor();
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  if (portPuzzleActive(nullptr, nullptr, nullptr) >= 0 && ImGui::Button(portTrLabel("Give up##puzzles"))) {
    portPuzzleStop();
  }
  if (!sPuzzleMessage.empty()) {
    ImGui::TextWrapped("%s", sPuzzleMessage.c_str());
  }
}

// the Start menu's form: the same in every game of an online session (lockstep), so a puzzle
// starts there in all of them at once. A row to pick one (left and right) and play it, and today's.
int sPuzzlePick; // (stepped in the Start menu only: in lockstep online)
enum { kRowDaily = 1, kRowPick = 2, kRowGiveUp = 3, kRowAbout = 4 };

std::string puzzleTitle(int i) {
  const PortPuzzleInfo* p = portPuzzleInfo(i);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%d. %s %s", i + 1, p != nullptr ? portTrGame(p->name) : "",
                p != nullptr ? std::string(size_t(p->difficulty), '*').c_str() : "");
  return buf;
}

// the goal in the game's encoding (goalText's, for the game's font)
std::string goalGame(const PortPuzzleInfo* p) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), p->maxTurns == 1 ? portTrGame("Win in 1 turn") : portTrGame("Win in %d turns"),
                p->maxTurns);
  std::string out = buf;
  if (p->flags & PORT_PUZZLE_NO_ITEMS) {
    out += portTrGame(", without items");
  }
  if (p->flags & PORT_PUZZLE_NO_DAMAGE) {
    out += portTrGame(", without being hurt");
  }
  if (p->flags & PORT_PUZZLE_NO_JUMP) {
    out += portTrGame(", without jumping");
  }
  if (p->flags & PORT_PUZZLE_NO_HAMMER) {
    out += portTrGame(", without the hammer");
  }
  return out + ".";
}

// the chosen puzzle's page (a page of lines: the same for every game, only what's drawn): its name,
// foes, goal, whether this computer solved it, and its hint once A is pressed
bool sHintShown;

int puzzleLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0, best = 0;
  const PortPuzzleInfo* p = portPuzzleInfo(sPuzzlePick);
  char text[512], wrapped[640];
  const auto add = [&](const char* game) {
    portPauseWrapCols(game, wrapped, sizeof(wrapped), 42);
    for (const char* line = wrapped; *line != 0 && n < max;) {
      const char* end = std::strchr(line, '\n');
      const size_t len = end != nullptr ? size_t(end - line) : std::strlen(line);
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%.*s", int(len < PORT_PAUSE_LINE - 1 ? len : PORT_PAUSE_LINE - 1), line);
      line += len + (end != nullptr ? 1 : 0);
    }
  };
  if (p == nullptr) {
    return 0;
  }
  add(puzzleTitle(sPuzzlePick).c_str());
  std::snprintf(text, sizeof(text), "%s: %s", portTrGame(p->foes), goalGame(p).c_str());
  add(text);
  if (portPuzzleSolved(sPuzzlePick, &best)) {
    std::snprintf(text, sizeof(text), portTrGame("Solved \"%s\" in %d turns!"), portTrGame(p->name), best);
    add(text);
  }
  if (sHintShown) {
    add(portTrGame(p->hint));
  }
  return n;
}

int puzzleButton(int button) {
  if (button == 'A') {
    sHintShown = true;
  }
  return 0;
}

const char* puzzleHelp() { return sHintShown ? nullptr : "{A} shows the puzzle's hint."; }

const PortPauseInfo kPuzzleInfo = {"Battle puzzles", puzzleLines, puzzleButton, puzzleHelp, nullptr, nullptr, 0};

int puzzleRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 4) {
    return 0;
  }
  int daily = 0, turn = 0, maxTurns = 0;
  const int active = portPuzzleActive(&daily, &turn, &maxTurns);
  if (active >= 0) {
    PortPauseRow h = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
    std::snprintf(h.text, sizeof(h.text), "%s", puzzleTitle(active).c_str());
    out[n++] = h;
    out[n++] = portPauseRow("Give up", "Mario comes back as he was, where he was.", nullptr, kRowGiveUp,
                            PORT_PAUSE_ROW_CONFIRM);
    return n;
  }
  PortPauseRow d = portPauseRow("Today's puzzle", "The same puzzle for everyone today: it begins once the menu is "
                                "closed.", nullptr, kRowDaily, PORT_PAUSE_ROW_CONFIRM);
  std::snprintf(d.value, sizeof(d.value), "%d", portPuzzleDaily() + 1);
  out[n++] = d;
  sPuzzlePick = (sPuzzlePick % portPuzzleCount() + portPuzzleCount()) % portPuzzleCount();
  PortPauseRow p = portPauseRow(nullptr, "Left and right: another puzzle. {A}: play it, once the menu is closed.", nullptr,
                                kRowPick, PORT_PAUSE_ROW_CONFIRM);
  std::snprintf(p.text, sizeof(p.text), "%s", puzzleTitle(sPuzzlePick).c_str());
  if (portPuzzleSolved(sPuzzlePick, nullptr)) {
    p.valueTr = "Solved";
  }
  out[n++] = p;
  out[n++] = portPauseRow("About this puzzle", "Who's fought, the goal, and a hint.", nullptr, kRowAbout);
  return n;
}

const char* puzzleAct(int id, int dir) {
  if (id == kRowAbout) { // (a page to read, on the second screen too)
    if (dir == 0) {
      sHintShown = false;
      portPauseInfoOpen(&kPuzzleInfo);
    }
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  if (id == kRowPick && dir != 0) {
    sPuzzlePick += dir;
    return nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  if (id == kRowGiveUp) {
    portPuzzleGiveUp();
    return "Mario comes back as he was, where he was.";
  }
  const char* why = nullptr;
  const int i = id == kRowDaily ? portPuzzleDaily() : sPuzzlePick;
  return portPuzzleStart(i, id == kRowDaily, &why) ? "The puzzle begins once the menu is closed." : why;
}

const PortPauseForm kPuzzleForm = {"Modes", "Battle puzzles", puzzleRows, puzzleAct};

// --- the Enemy maker ---

std::vector<PortEnemyVariant> sFoes;
bool sFoesRead;
int sFoeEdit = -1; // -2: a new one
PortEnemyVariant sFoe;
char sFoeCode[1024];
std::string sFoeMessage;

std::string foesPath() { return std::string(portSettingsUserPath()) + "/enemies.txt"; }

void armUserFoes() { portBattlexSetUserVariants(sFoes.data(), int(sFoes.size())); }

void readFoes() {
  if (sFoesRead) {
    return;
  }
  sFoesRead = true;
  for (const std::string& line : lines(readText(foesPath()))) {
    PortEnemyVariant v;
    if (portEnemyVariantParse(line.c_str(), &v) && sFoes.size() < PORT_VARIANT_ARMED_MAX) {
      sFoes.push_back(v);
    }
  }
  armUserFoes();
}

std::string foeCode(const PortEnemyVariant& v) {
  char buf[2048];
  return portEnemyVariantCode(&v, buf, sizeof(buf)) > 0 ? buf : "";
}

void writeFoes() {
  std::string text = "# Paper Cut enemy variants: Challenges > Enemy maker (a code a line)\n";
  for (const PortEnemyVariant& v : sFoes) {
    text += foeCode(v) + "\n";
  }
  SDL_SaveFile(foesPath().c_str(), text.data(), text.size());
  armUserFoes();
}

std::string enemyName(int kind) {
  char buf[64];
  portBattlexEnemyName(kind, buf, sizeof(buf));
  if (buf[0] == 0) {
    std::snprintf(buf, sizeof(buf), "%s %d", portTr("Enemy"), kind);
  }
  return buf;
}

std::string foeTitle(const PortEnemyVariant& v) {
  return v.name[0] != 0 ? std::string(v.name) + " (" + enemyName(v.baseKind) + ")" : enemyName(v.baseKind);
}

std::string foeSummary(const PortEnemyVariant& v) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), portTr("HP %d%% %+d, attack %+d, defence %+d, size %d%%"), v.hpPercent, v.hpAdd,
                v.attack, v.defense, v.sizePercent);
  std::string out = buf;
  if (v.move != PORT_VARIANT_MOVE_NONE) {
    out += std::string(", ") + portTr(portEnemyVariantMoveName(v.move));
  }
  if (v.coins > 0) {
    std::snprintf(buf, sizeof(buf), portTr(", %d more coins"), v.coins);
    out += buf;
  }
  return out;
}

void foeEditor() {
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  if (ImGui::BeginCombo(portTrLabel("Enemy##foe"), enemyName(sFoe.baseKind).c_str())) {
    for (int k = 1; k < UNIT_MAX; k++) {
      char buf[64];
      if (portBattlexEnemyName(k, buf, sizeof(buf))[0] == 0) {
        continue; // no Tattle entry: not an enemy to fight
      }
      if (ImGui::Selectable((std::string(buf) + "##" + std::to_string(k)).c_str(), sFoe.baseKind == k)) {
        sFoe.baseKind = k;
      }
    }
    ImGui::EndCombo();
  }
  const float w = ImGui::GetFontSize() * 14.f;
  ImGui::SetNextItemWidth(w);
  ImGui::InputText(portTrLabel("Name (empty: its own)##foe"), sFoe.name, sizeof(sFoe.name));
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(portTrLabel("HP##foe"), &sFoe.hpPercent, 10, 1000, "%d%%");
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(portTrLabel("HP added##foe"), &sFoe.hpAdd, -99, 999);
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(portTrLabel("Attack##foe"), &sFoe.attack, -9, 20, "%+d");
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(portTrLabel("Defence##foe"), &sFoe.defense, -9, 20, "%+d");
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(portTrLabel("Size##foe"), &sFoe.sizePercent, 25, 300, "%d%%");
  float c[4] = {float((sFoe.color >> 24) & 0xFF) / 255.f, float((sFoe.color >> 16) & 0xFF) / 255.f,
                float((sFoe.color >> 8) & 0xFF) / 255.f, float(sFoe.color & 0xFF) / 255.f};
  if (ImGui::ColorEdit4(portTrLabel("Colour##foe"), c, ImGuiColorEditFlags_NoInputs)) {
    sFoe.color = (unsigned(c[0] * 255.f + 0.5f) << 24) | (unsigned(c[1] * 255.f + 0.5f) << 16) |
                 (unsigned(c[2] * 255.f + 0.5f) << 8) | unsigned(c[3] * 255.f + 0.5f);
  }
  ImGui::SetNextItemWidth(w);
  if (ImGui::BeginCombo(portTrLabel("Move##foe"), portTr(portEnemyVariantMoveName(sFoe.move)))) {
    for (int m = 0; m < PORT_VARIANT_MOVE_COUNT; m++) {
      if (ImGui::Selectable(portTr(portEnemyVariantMoveName(m)), sFoe.move == m)) {
        sFoe.move = m;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt(portTrLabel("Coins when beaten##foe"), &sFoe.coins, 0, 99);
  ImGui::InputTextMultiline(portTrLabel("Tattle (empty: its own)##foe"), sFoe.tattle, sizeof(sFoe.tattle),
                            ImVec2(ImGui::GetFontSize() * 22.f, ImGui::GetFontSize() * 4.f));
  if (ImGui::Button(portTrLabel("Save##foe"))) {
    if (sFoeEdit >= 0 && sFoeEdit < int(sFoes.size())) {
      sFoes[size_t(sFoeEdit)] = sFoe;
    } else {
      sFoes.push_back(sFoe);
    }
    writeFoes();
    sFoeEdit = -1;
    sFoeMessage = portTr("Saved.");
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Cancel##foe"))) {
    sFoeEdit = -1;
  }
}

void enemyMakerSection(int) {
  readFoes();
  ImGui::TextWrapped("%s", portTr("Your own versions of the game's enemies: stronger, tougher, bigger, another colour, "
                                  "with a move taken from another enemy and your own Tattle. With the switch below "
                                  "on, every enemy of a kind you changed is your version, in the story, challenges "
                                  "and battle puzzles (not online)."));
  if (sFoeEdit != -1) {
    foeEditor();
    return;
  }
  ImGui::BeginDisabled(sFoes.size() >= PORT_VARIANT_ARMED_MAX);
  if (ImGui::Button(portTrLabel("New enemy##foe"))) {
    portEnemyVariantInit(&sFoe, UNIT_GOOMBA);
    sFoeEdit = -2;
  }
  ImGui::EndDisabled();
  int remove = -1;
  for (int i = 0; i < int(sFoes.size()); i++) {
    const PortEnemyVariant& v = sFoes[size_t(i)];
    ImGui::PushID(i);
    ImGui::Text("%s", foeTitle(v).c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", foeSummary(v).c_str());
    ImGui::PopStyleColor();
    if (ImGui::SmallButton(portTrLabel("Edit##foe"))) {
      sFoe = v;
      sFoeEdit = i;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Copy the code##foe"))) {
      SDL_SetClipboardText(foeCode(v).c_str());
      sFoeMessage = portTr("The code is copied: paste it anywhere to share it.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Remove##foe"))) {
      remove = i;
    }
    ImGui::PopID();
  }
  if (remove >= 0) {
    sFoes.erase(sFoes.begin() + remove);
    writeFoes();
  }
  ImGui::Spacing();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.f);
  ImGui::InputTextWithHint("##foecode", "PCFOE1:...", sFoeCode, sizeof(sFoeCode));
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Paste##foe"))) {
    if (char* clip = SDL_GetClipboardText()) {
      std::snprintf(sFoeCode, sizeof(sFoeCode), "%s", clip);
      SDL_free(clip);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Add from the code##foe"))) {
    PortEnemyVariant v;
    if (!portEnemyVariantParse(sFoeCode, &v)) {
      sFoeMessage = portTr("That isn't an enemy code.");
    } else if (sFoes.size() >= PORT_VARIANT_ARMED_MAX) {
      sFoeMessage = portTr("Eight is the most: remove one first.");
    } else {
      sFoes.push_back(v);
      writeFoes();
      sFoeCode[0] = 0;
      sFoeMessage = portTr("Added.");
    }
  }
  if (!sFoeMessage.empty()) {
    ImGui::TextWrapped("%s", sFoeMessage.c_str());
  }
}

// --- over the picture: the last grade, the puzzle's goal ---

int* sGrades;

void box(ImDrawList* dl, ImVec2 at, const std::string& a, const std::string& b) {
  ImFont* f = ImGui::GetFont();
  const float size = ImGui::GetFontSize() * 1.1f;
  const ImVec2 ta = f->CalcTextSizeA(size, FLT_MAX, 0.f, a.c_str());
  const ImVec2 tb = f->CalcTextSizeA(size * 0.85f, FLT_MAX, 0.f, b.c_str());
  const float w = (ta.x > tb.x ? ta.x : tb.x) + size * 1.2f;
  const float h = ta.y + tb.y + size * 0.9f;
  const ImVec2 lo(at.x - w * 0.5f, at.y), hi(at.x + w * 0.5f, at.y + h);
  dl->AddRectFilled(lo, hi, IM_COL32(20, 16, 10, 200), size * 0.4f);
  dl->AddRect(lo, hi, IM_COL32(230, 190, 90, 220), size * 0.4f, 0, 2.f);
  dl->AddText(f, size, ImVec2(at.x - ta.x * 0.5f, lo.y + size * 0.35f), IM_COL32(255, 230, 150, 255), a.c_str());
  dl->AddText(f, size * 0.85f, ImVec2(at.x - tb.x * 0.5f, lo.y + size * 0.45f + ta.y), IM_COL32(240, 240, 240, 255),
              b.c_str());
}

void overlay(int pageOpen) {
  readFoes(); // (once: "Use my enemies" armed from the first frame, not only once the Enemy maker was shown)
  if (pageOpen) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 view = ImGui::GetMainViewport()->Size;
  int daily = 0, turn = 0, maxTurns = 0;
  const int puzzle = portPuzzleActive(&daily, &turn, &maxTurns);
  float y = view.y * 0.06f;
  if (puzzle >= 0) {
    const PortPuzzleInfo* p = portPuzzleInfo(puzzle);
    char a[160], b[200];
    std::snprintf(a, sizeof(a), "%s%d. %s", daily ? portTr("Today's puzzle: ") : "", puzzle + 1, portTr(p->name));
    std::snprintf(b, sizeof(b), portTr("%s  Turn %d of %d"), goalText(p).c_str(), turn < 1 ? 1 : turn, maxTurns);
    box(dl, ImVec2(view.x * 0.5f, y), a, b);
    y += ImGui::GetFontSize() * 3.2f;
  }
  // (the last grade is on the battle recap's card, in the game's own look: port/src/aurora/recap_aurora.c)
}

struct Register {
  Register() {
    sGrades = portSettingsDeclare("battle_grades", 0, 0, 1);
    portSettingsDeclare("pc_badges", 0, 0, 1);
    portSettingsDeclare("pc_recipes", 0, 0, 1);
    portSettingsDeclare("enemy_variants", 0, 0, 1);
    portSettingsAddToggle("Enhancements", "Battle grades",
                          "Each battle won is graded D to S for its Stylish moves, Superguards, damage taken and "
                          "turns, shown for a moment after it; an S pays a few more coins, an A one more.",
                          PORT_SETTING_CHANGES_PLAY, "battle_grades",
                          "battle grade rank score stylish superguard s rank coins");
    {
      // the battle recap (port/src/aurora/recap_aurora.c): draw-only, each player's own
      static const char* const kRecap[] = {"Off", "Short", "Full"};
      portSettingsDeclare("battle_recap", 1, 0, 2); // Short by default: one line, never holds the game
      portSettingsAddChoice("Enhancements", "Battle recap",
                            "After each battle won, a card with its numbers on the grade's: damage dealt and taken, "
                            "the best hit, Superguards, guards, Stylish moves, turns, FP and items used. Short is "
                            "one line; {A} or {B} puts it away. The Journal's Records keep them all.",
                            0, "battle_recap", kRecap, 3, "battle recap summary results stats damage card after");
    }
    portSettingsAddToggle("Enhancements", "Paper Cut badge set",
                          "Twelve new badges, each folded from two or three of the game's, sold by a Toad merchant "
                          "in Rogueport's plaza once the prologue is done. A file holding them isn't for the "
                          "GameCube game any more.",
                          PORT_SETTING_CHANGES_PLAY | PORT_SETTING_CHANGES_SAVES, "pc_badges",
                          "badge set new badges merchant shop paper cut");
    portSettingsAddSection("Enhancements", "Paper Cut badges", nullptr, PORT_SETTING_CHANGES_PLAY, badgeSetSection,
                           "badge set list new badges");
    portSettingsAddToggle("Enhancements", "Zess T.'s new dishes",
                          "Sixteen new dishes from pairs of ingredients with no recipe of their own (they made a "
                          "Mistake before), to be found by trying: attack, defence, regeneration, dodging, "
                          "moving twice. They aren't in the Recipe Log.",
                          PORT_SETTING_CHANGES_PLAY | PORT_SETTING_CHANGES_SAVES, "pc_recipes",
                          "recipes cooking zess dishes food new");
    portSettingsAddSection("Enhancements", "Badge maker", nullptr,
                           PORT_SETTING_CHANGES_PLAY | PORT_SETTING_CHANGES_SAVES, badgeMakerSection,
                           "badge maker custom badge make design code share bp");
    portSettingsAddSection("Modes", "Battle puzzles", nullptr, PORT_SETTING_CHANGES_PLAY, puzzlesSection,
                           "battle puzzles puzzle daily chess turn challenge");
    portPauseAddForm(&kPuzzleForm);
    portSettingsAddToggle("Challenges", "Use my enemies",
                          "Every enemy of a kind changed in the Enemy maker below is your version, in every battle "
                          "(not online).",
                          PORT_SETTING_CHANGES_PLAY, "enemy_variants", "enemy maker variants custom enemies");
    portSettingsAddSection("Challenges", "Enemy maker", nullptr, PORT_SETTING_CHANGES_PLAY, enemyMakerSection,
                           "enemy maker variant custom enemy code share tattle");
    portSettingsAddOverlay(overlay, nullptr);
  }
} sRegister;

} // namespace

// --- online: the host's enemies for the session (port/include/port/online_session.h) ---

extern "C" int portBattlexSessionGet(char* out, int size) {
  const std::string text = readText(foesPath());
  if (static_cast<int>(text.size()) > size) {
    return -1;
  }
  std::memcpy(out, text.data(), text.size());
  return static_cast<int>(text.size());
}

extern "C" void portBattlexSessionPut(const char* data, int len) {
  if (data == nullptr) {
    portBattlexSetSessionVariants(nullptr, 0, 0);
    return;
  }
  std::vector<PortEnemyVariant> list;
  for (const std::string& line : lines(std::string(data, static_cast<size_t>(len)))) {
    PortEnemyVariant v;
    if (portEnemyVariantParse(line.c_str(), &v) && list.size() < PORT_VARIANT_ARMED_MAX) {
      list.push_back(v);
    }
  }
  portBattlexSetSessionVariants(list.data(), static_cast<int>(list.size()), 1);
}
