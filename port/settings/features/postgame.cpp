// The post-game places and modes on the Modes tab and in the Start menu's
// Settings tab (port/include/port/postgame.h): the trophy room (and its
// plaques over the picture while Mario stands on its pedestal) and the Paper
// Tower (its floor and time over the picture during a climb), and the daily
// shop specials (today's list, and a notice in a shop that has one).
#include "port/postgame.h"
#include "port/area_maps.h"
#include "port/companion_menu.h"
#include "port/date_seed.h"
#include "port/practice_tools.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/pause_forms.h"
#include "port/practice.h"
#include "port/achievements.h"
#include "port/raid.h"
#include "port/settings.h"
#include "port/tr.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace {

constexpr const char* kTab = "Modes";
constexpr const char* kSecondScreen = "Not on the second screen: use the game's Start menu.";
constexpr const char* kFromStartMenu =
    "Online, start it from the Start menu (Settings, Modes), so every game of the session starts it together.";

// --- the trophy room ---

void trophySection(int inGame) {
  static std::string sMessage;
  ImGui::TextWrapped("%s", portTr("A room under Rogueport that fills as you play: the Crystal Stars on their "
                                  "pedestals, the bosses you've beaten as paper cut-outs, and a plaque for each "
                                  "achievement (stand on the pedestal to read them). Its ways out lead into "
                                  "Rogueport's underground."));
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Load a save file to visit it."));
    return;
  }
  if (portTrophyRoomHere()) {
    ImGui::TextDisabled("%s", portTr("Mario is in the trophy room."));
  } else if (portOnlineActive()) {
    // (this page is one game's alone: the Start menu's form goes in every game of the session)
    ImGui::TextDisabled("%s", portTr(kFromStartMenu));
  } else if (ImGui::Button(portTrLabel("Visit the trophy room##trophy"))) {
    const char* why = nullptr;
    sMessage = portTrophyRoomVisit(&why) ? "" : portTr(why);
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

int trophyRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("Visit the trophy room", "The room opens once the menu is closed.", nullptr, 0,
                        portTrophyRoomHere() ? PORT_PAUSE_ROW_DIM : PORT_PAUSE_ROW_CONFIRM);
  return 1;
}

const char* trophyAct(int id, int dir) {
  if (dir != 0 || id != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  const char* why = nullptr;
  return portTrophyRoomVisit(&why) ? "The room opens once the menu is closed." : why;
}

const PortPauseForm kTrophyForm = {kTab, "Trophy room", trophyRows, trophyAct};

// the plaques: a board over the picture while Mario stands on the pedestal
void plaques() {
  if (!portTrophyRoomHere()) {
    return;
  }
  if (!portTrophyRoomOnPedestal()) {
    const char* hint = portTr("Stand on the pedestal to read the plaques.");
    const float w = ImGui::GetFont()->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, hint).x + 16.0f;
    portModHostDrawRect(320.0f - w * 0.5f, 446.0f, w, 20.0f, 0x00000080u);
    portModHostDrawText(328.0f - w * 0.5f, 448.0f, 13.0f, 0xF0D890FFu, hint);
    return;
  }
  const int count = portAchievementCount();
  int earned = 0;
  for (int i = 0; i < count; i++) {
    earned += portAchievementEarned(i) != nullptr ? 1 : 0;
  }
  // a wooden board, a gold plaque per achievement earned (two columns), the rest blank
  const int perColumn = 14;
  const int shown = std::min(count, perColumn * 2);
  const float x0 = 70.0f, y0 = 60.0f, w = 500.0f, rowH = 22.0f;
  const float h = 44.0f + rowH * std::min(perColumn, (shown + 1) / 2) + 12.0f;
  portModHostDrawRect(x0 - 4.0f, y0 - 4.0f, w + 8.0f, h + 8.0f, 0x3A2412E0u);
  portModHostDrawRect(x0, y0, w, h, 0x6B4423E8u);
  char title[96];
  std::snprintf(title, sizeof(title), portTr("Plaques: %d of %d"), earned, count);
  portModHostDrawText(x0 + 12.0f, y0 + 10.0f, 16.0f, 0xF6C445FFu, title);
  if (const char* file = portAchievementFile(); file[0] != '\0') {
    const float fw = ImGui::GetFont()->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, file).x;
    portModHostDrawText(x0 + w - fw - 12.0f, y0 + 13.0f, 12.0f, 0xE8D2ACFFu, file);
  }
  // earned first, in the list's order, then the ones still to earn
  int order[256];
  int n = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < count && n < 256; i++) {
      if ((portAchievementEarned(i) != nullptr) == (pass == 0)) {
        order[n++] = i;
      }
    }
  }
  for (int k = 0; k < shown; k++) {
    const int i = order[k];
    const bool got = portAchievementEarned(i) != nullptr;
    const float x = x0 + 12.0f + (k / perColumn) * (w * 0.5f);
    const float y = y0 + 40.0f + (k % perColumn) * rowH;
    portModHostDrawRect(x, y, w * 0.5f - 24.0f, rowH - 4.0f, got ? 0xD8A830F0u : 0x4A3018C0u);
    portModHostDrawText(x + 6.0f, y + 2.0f, 13.0f, got ? 0x2A1808FFu : 0x9A8060FFu,
                        got ? portTr(portAchievementName(i)) : "?");
  }
  if (count > shown) {
    char more[64];
    std::snprintf(more, sizeof(more), portTr("And %d more on the Achievements tab"), count - shown);
    portModHostDrawText(x0 + 12.0f, y0 + h - 18.0f, 12.0f, 0xE8D2ACFFu, more);
  }
  // the raid plaque (port/include/port/raid.h): the raids won and the bosses beaten, under the board
  if (const int wins = portAchievementCounter("raid_wins"); wins > 0) {
    const int bosses = portAchievementCounter("raid_bosses");
    std::string names;
    for (int b = 0; b < portRaidBossCount(); b++) {
      if ((bosses >> b) & 1) {
        names += (names.empty() ? "" : ", ") + std::string(portTr(portRaidBossName(b)));
      }
    }
    char line[96];
    std::snprintf(line, sizeof(line), portTr("Raid plaque: %d raids won"), wins);
    const float py = std::min(y0 + h + 12.0f, 432.0f);
    portModHostDrawRect(x0 - 4.0f, py - 2.0f, w + 8.0f, 42.0f, 0x3A2412E0u);
    portModHostDrawRect(x0, py, w, 38.0f, 0xB0402AF0u);
    portModHostDrawText(x0 + 12.0f, py + 3.0f, 14.0f, 0xF6C445FFu, line);
    portModHostDrawText(x0 + 12.0f, py + 20.0f, 12.0f, 0xF8E8C8FFu, names.c_str());
  }
}

// --- the Paper Tower ---

std::string clock(unsigned long long ms) {
  char text[32];
  const unsigned long long s = ms / 1000;
  if (s >= 3600) {
    std::snprintf(text, sizeof(text), "%llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
  } else {
    std::snprintf(text, sizeof(text), "%llu:%02llu", s / 60, s % 60);
  }
  return text;
}

const char* challengeName(int c) {
  switch (c) {
  case PORT_TOWER_PAPER:
    return "Paper-mode gaps";
  case PORT_TOWER_TUBE:
    return "Tube-mode gaps";
  case PORT_TOWER_WIND:
    return "Wind walls";
  case PORT_TOWER_MIXED:
    return "Mixed gates";
  case PORT_TOWER_BATTLE:
    return "Battle";
  default:
    return "Walk";
  }
}

void towerSection(int inGame) {
  static std::string sMessage;
  static int sSeenResult;
  int floor = 0, cleared = 0;
  unsigned long long ms = 0;
  const int results = portTowerResult(&floor, &ms, &cleared);
  if (results != sSeenResult) {
    sSeenResult = results;
    char text[128];
    std::snprintf(text, sizeof(text), cleared ? portTr("Climbed all %d floors in %s.") : portTr("The climb ended on floor %d after %s."),
                  cleared ? PORT_TOWER_FLOORS : floor, clock(ms).c_str());
    sMessage = text;
  }
  ImGui::TextWrapped("%s", portTr("A climb of 30 floors made of the game's own rooms, remixed: Paper-mode gaps you "
                                  "slip through sideways, low walls you roll under in Tube mode, and wind walls that "
                                  "blow you back unless you turn side-on. Every fifth floor is a battle from the Pit "
                                  "of 100 Trials. Walk back out of a floor's way in to start it again. Once the game "
                                  "is finished."));
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Load a save file to climb it."));
    return;
  }
  if (portOnlineActive()) {
    ImGui::TextDisabled("%s", portTr(kFromStartMenu)); // (the climb, Leave and Skip: the Start menu's form)
  }
  if (portTowerActive()) {
    ImGui::Text(portTr("Floor %d of %d, %s"), portTowerFloor(), PORT_TOWER_FLOORS, clock(portTowerElapsed()).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", portTr(challengeName(portTowerChallenge())));
    if (ImGui::Button(portTrLabel("Leave the tower##tower"))) {
      portTowerStop();
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Skip this floor##tower"))) {
      const char* why = nullptr;
      if (!portTowerSkip(&why)) {
        sMessage = portTr(why);
      }
    }
    ImGui::TextDisabled("%s", portTr("A skipped floor counts as help: the climb sets no best and earns no achievements."));
  } else {
    if (ImGui::Button(portTrLabel("Climb the Paper Tower##tower"))) {
      const char* why = nullptr;
      sMessage = portTowerStart(&why) ? "" : portTr(why);
    }
  }
  if (portTowerBest(&floor, &ms)) {
    if (ms > 0) {
      ImGui::Text(portTr("Best: the top in %s"), clock(ms).c_str());
    } else {
      ImGui::Text(portTr("Best: floor %d"), floor);
    }
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

int towerRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 3) {
    return 0;
  }
  if (!portTowerActive()) {
    out[n++] = portPauseRow("Climb the Paper Tower", "30 floors of the game's rooms, remixed, after the credits.",
                            nullptr, 0, portTowerUnlocked() ? PORT_PAUSE_ROW_CONFIRM : PORT_PAUSE_ROW_DIM);
    return n;
  }
  PortPauseRow r = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
  std::snprintf(r.text, sizeof(r.text), "%d / %d  %s", portTowerFloor(), PORT_TOWER_FLOORS,
                clock(portTowerElapsed()).c_str());
  out[n++] = r;
  out[n++] = portPauseRow("Leave the tower", "Mario goes back to the room he started from.", nullptr, 1,
                          PORT_PAUSE_ROW_CONFIRM);
  out[n++] = portPauseRow("Skip this floor", "On to the next floor; the climb counts as helped.", nullptr, 2,
                          PORT_PAUSE_ROW_CONFIRM);
  return n;
}

const char* towerAct(int id, int dir) {
  const char* why = nullptr;
  if (dir != 0 || id < 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return kSecondScreen;
  }
  if (id == 0) {
    return portTowerStart(&why) ? "The climb starts once the menu is closed." : why;
  }
  if (id == 1) {
    portTowerStop();
    return "The climb ends once the menu is closed.";
  }
  return portTowerSkip(&why) ? "On to the next floor once the menu is closed." : why;
}

const PortPauseForm kTowerForm = {kTab, "The Paper Tower", towerRows, towerAct};

void towerHud() {
  if (!portTowerActive()) {
    return;
  }
  char line[128];
  std::snprintf(line, sizeof(line), portTr("Paper Tower: floor %d/%d   %s"), portTowerFloor(), PORT_TOWER_FLOORS,
                clock(portTowerElapsed()).c_str());
  const char* kind = portTr(challengeName(portTowerChallenge()));
  const float w = std::max(ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line).x,
                           ImGui::GetFont()->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, kind).x) +
                  16.0f;
  portModHostDrawRect(632.0f - w, 8.0f, w, 38.0f, 0x00000090u);
  portModHostDrawText(638.0f - w, 10.0f, 14.0f, 0xFFFFFFFFu, line);
  portModHostDrawText(638.0f - w, 28.0f, 12.0f, portTowerHelped() ? 0xB0B0B0FFu : 0xF6C445FFu, kind);
}

// --- daily shop specials ---

void dailySection(int inGame) {
  char date[16];
  portDateText(portDateDay(), date, sizeof(date));
  ImGui::Text(portTr("Today's specials (%s, UTC):"), date);
  for (int i = 0; i < portDailyShopCount(); i++) {
    int item = 0, price = 0, usual = 0;
    if (!portDailyShopSpecial(i, &item, &price, &usual)) {
      ImGui::TextDisabled("%s", portTr("Turn on Daily shop specials above to see them."));
      return;
    }
    char name[64];
    if (inGame) {
      portPracticeItemName(item, name, sizeof(name));
    } else {
      std::snprintf(name, sizeof(name), "%s", portTr("a rare item"));
    }
    // the shop's town by the game's own name for its area (the Journal's), or the area's code
    const char* area = portAreaName(portDailyShopName(i));
    char town[64];
    std::snprintf(town, sizeof(town), "%s", area != nullptr ? area : portDailyShopName(i));
    ImGui::BulletText(portTr("%s: %s for %d coins (usually %d)"), town, name, price, usual);
  }
  if (!inGame) {
    ImGui::TextDisabled("%s", portTr("Load a save file to see the items' names."));
  }
}

extern "C" const char* msgSearch(const char* tag); // the game's text (msgdrv.c)

// an item's name in the game's encoding, for the game's font: the JP release's text by the item's key
// (portPracticeItemName's there), the others' UTF-8 name made Latin-1 again
std::string gameItemName(int item) {
  char name[64];
  std::string out;
  portPracticeItemName(item, name, sizeof(name));
  if (PORT_GAME_ID[3] == 'J') {
    const char* text = msgSearch(name);
    for (const char* p = text != nullptr ? text : name; *p != 0; p++) {
      if (*p == '<') { // (a control tag)
        while (*p != 0 && *p != '>') {
          p++;
        }
        if (*p == 0) {
          break;
        }
      } else if (*p != '\n') {
        out += *p;
      }
    }
    return out;
  }
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p != 0; p++) {
    if (*p < 0x80) {
      out += char(*p);
    } else if ((*p == 0xC2 || *p == 0xC3) && p[1] != 0) {
      out += char(((*p & 0x03) << 6) | (p[1] & 0x3F));
      p++;
    }
  }
  return out;
}

// the Start menu's page of today's specials: a shop and its item a line
int dailyLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0;
  for (int i = 0; i < portDailyShopCount() && n + 1 < max; i++) {
    int item = 0, price = 0, usual = 0;
    if (!portDailyShopSpecial(i, &item, &price, &usual)) {
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", portTrGame("Turn on Daily shop specials above to see them."));
      return n;
    }
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%s:", portTrGame(portDailyShopName(i)));
    char line[PORT_PAUSE_LINE];
    std::snprintf(line, sizeof(line), portTrGame("%s for %d coins (usually %d)"), gameItemName(item).c_str(), price,
                  usual);
    std::snprintf(out[n++], PORT_PAUSE_LINE, "  %s", line);
  }
  return n;
}

const PortPauseInfo kDailyInfo = {"Today's specials", dailyLines, nullptr, nullptr, nullptr, nullptr, 0};

int dailyRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  // (the date by this computer's clock: only what's drawn differs between games)
  PortPauseRow r = portPauseRow("Today's specials", "Five shops each put one rare item on a shelf at 30% off, a "
                                "different one each day and the same for everyone that day (by the UTC date).",
                                nullptr, 0);
  portDateText(portDateDay(), r.value, sizeof(r.value));
  out[0] = r;
  return 1;
}

const char* dailyAct(int id, int dir) {
  if (id == 0 && dir == 0) {
    portPauseInfoOpen(&kDailyInfo);
  }
  return nullptr;
}

const PortPauseForm kDailyForm = {kTab, "Today's specials", dailyRows, dailyAct};

void dailyHud() {
  static int sShop = -1;
  static unsigned sShownAt;
  const int shop = portDailyShopHere();
  if (shop != sShop) {
    sShop = shop;
    sShownAt = static_cast<unsigned>(ImGui::GetTime() * 1000.0);
  }
  if (shop < 0 || static_cast<unsigned>(ImGui::GetTime() * 1000.0) - sShownAt > 7000u) {
    return;
  }
  int item = 0, price = 0, usual = 0;
  if (!portDailyShopSpecial(shop, &item, &price, &usual)) {
    return;
  }
  char name[64], line[160];
  portPracticeItemName(item, name, sizeof(name));
  std::snprintf(line, sizeof(line), portTr("Today's special: %s, %d coins (usually %d)"), name, price, usual);
  const float w = ImGui::GetFont()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, line).x + 16.0f;
  portModHostDrawRect(320.0f - w * 0.5f, 52.0f, w, 22.0f, 0x6B4423E0u);
  portModHostDrawText(328.0f - w * 0.5f, 54.0f, 14.0f, 0xF6C445FFu, line);
}

void hud(void*) {
  plaques();
  towerHud();
  dailyHud();
}

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Trophy room", nullptr, 0, trophySection,
                           "trophy room crystal stars bosses cut-outs plaques achievements post game");
    portPauseAddForm(&kTrophyForm);
    portSettingsDeclare("paper_tower_seed", 0, 0, 99999);
    portSettingsAddSection(kTab, "The Paper Tower", nullptr, PORT_SETTING_CHANGES_PLAY, towerSection,
                           "paper tower climb floors post game challenge paper mode tube mode wind");
    portSettingsAddSlider(kTab, "Paper Tower seed",
                          "The rooms, their gates and the battles of a climb: 0 for the day's (the same for everyone "
                          "that day), any other number for that climb every time.",
                          PORT_SETTING_CHANGES_PLAY, "paper_tower_seed", 0, 99999, "%d", "paper tower seed daily");
    portPauseAddForm(&kTowerForm);
    portSettingsDeclare("daily_specials", 0, 0, 1);
    portSettingsAddToggle(kTab, "Daily shop specials",
                          "Five shops each put one rare item on a shelf at 30% off, a different one each day and the "
                          "same for everyone that day (by the UTC date). It takes one shelf's place for the day. Not "
                          "during online co-op or a replay.",
                          PORT_SETTING_CHANGES_PLAY, "daily_specials", "daily shop specials discount sale rare items");
    portSettingsAddSection(kTab, "Today's specials", nullptr, 0, dailySection, "daily shop specials today");
    portPauseAddForm(&kDailyForm);
    portModHostAddDraw(hud, nullptr);
  }
} sRegister;

} // namespace
