// The raid boss (port/include/port/raid.h, port/src/aurora/raid_aurora.c, docs/RAID.md): the Modes
// tab's section (this week's raid, its start offline, the last result and the week's board on the
// relay), the Start menu's form (Modes: start it, the week's raid on a page of its own), and the
// fight's state over the picture. Everything here only reads the raid; the start from the Start menu
// is in the game's frame (the same in every game online), the page's only off a session.
#include "port/companion_menu.h"
#include "port/http.h"
#include "port/layout.h"
#include "port/mod_host.h"
#include "port/online.h"
#include "port/pause_forms.h"
#include "port/raid.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"
#include "../settings_ui.h"
#include "../relay_util.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using portui::hint;

namespace {

constexpr const char* kTab = "Modes";

// the phases, as the HUD and the page name them (English; translated as shown)
constexpr const char* kPhaseName[PORT_RAID_PHASES] = {"Phase 1: the warm-up", "Phase 2: the shell",
                                                      "Phase 3: the fury", "Phase 4: the last stand"};

std::string weekText(int week) {
  char text[16];
  portRaidWeekText(week, text, sizeof(text));
  return text;
}

std::string clock(unsigned frames) {
  const unsigned s = frames / 60;
  char text[32];
  std::snprintf(text, sizeof(text), "%u:%02u", s / 60, s % 60);
  return text;
}

// --- the relay's board for a week ----------------------------------------------------------

struct Entry {
  int turns = 0;
  unsigned frames = 0;
  int players = 0;
  std::string name;
};

std::mutex sLock;
std::vector<Entry> sBoard;
std::string sBoardError;
int sBoardWeek = -1, sBoardBoss = -1, sBoardClears = 0, sYourRank = 0;
bool sBusy = false;
unsigned sSentSerial = 0;
char sName[32];
bool sNameRead = false;
std::string sMessage;

std::string namePath() { return std::string(portSettingsUserPath()) + "/raid_name.txt"; }

void readName() {
  if (sNameRead) {
    return;
  }
  sNameRead = true;
  if (FILE* f = std::fopen(namePath().c_str(), "r")) {
    if (std::fgets(sName, sizeof(sName), f) != nullptr) {
      sName[std::strcspn(sName, "\r\n")] = '\0';
    }
    std::fclose(f);
  }
}

void saveName() {
  if (FILE* f = std::fopen(namePath().c_str(), "w")) {
    std::fprintf(f, "%s\n", sName);
    std::fclose(f);
  }
}

using portrelay::relayBase;

using portrelay::percent;

using portrelay::unpercent;

// "raid <WEEK> <BOSS> <CLEARS> [<YOUR RANK>]", then "<rank> <turns> <frames> <players> <name> <game>" lines
void fetchBoard(std::string url) {
  std::vector<uint8_t> body;
  std::string error;
  const bool ok = port_http::fetch(url, body, 64 * 1024, error);
  std::lock_guard<std::mutex> lock(sLock);
  sBusy = false;
  if (!ok) {
    sBoardError = error;
    return;
  }
  const std::string text(body.begin(), body.end());
  std::vector<Entry> got;
  int week = -1, boss = -1, clears = 0, rank = 0;
  bool header = false;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    char name[160] = "", game[16] = "";
    int r = 0, turns = 0, players = 0;
    unsigned frames = 0;
    if (std::sscanf(line.c_str(), "raid %d %d %d %d", &week, &boss, &clears, &rank) >= 3) {
      header = true;
    } else if (std::sscanf(line.c_str(), "%d %d %u %d %159s %15s", &r, &turns, &frames, &players, name, game) >= 5) {
      got.push_back({turns, frames, players, unpercent(name)});
    }
  }
  if (!header) {
    sBoardError = "The board's answer wasn't understood.";
    return;
  }
  sBoard = got;
  sBoardWeek = week;
  sBoardBoss = boss;
  sBoardClears = clears;
  sYourRank = rank;
  sBoardError.clear();
}

// the clear's names: the session's players (online), else the name typed here
std::string partyName(const PortRaidResult* r) {
  std::string names;
  for (int i = 0; i < PORT_RAID_NAMES; i++) {
    if (r->names[i][0] != '\0') {
      names += (names.empty() ? "" : " & ") + std::string(r->names[i]);
    }
  }
  return names.empty() ? std::string(sName) : names;
}

void askBoard(int week, const PortRaidResult* send) {
  std::lock_guard<std::mutex> lock(sLock);
  if (sBusy) {
    return;
  }
  const std::string base = relayBase();
  if (base.empty()) {
    sBoardError = "This build has no relay.";
    return;
  }
  std::string url = base + "/raid/" + std::to_string(week);
  if (send != nullptr) {
    char game[8];
    std::snprintf(game, sizeof(game), "%.6s", portVersionSelf()->id);
    url += "?name=" + percent(partyName(send)) + "&boss=" + std::to_string(send->boss) +
           "&turns=" + std::to_string(send->turns) + "&frames=" + std::to_string(send->frames) +
           "&players=" + std::to_string(send->players) + "&game=" + game + "&v=" + PORT_VERSION;
    sSentSerial = send->serial;
  }
  sBusy = true;
  std::thread(fetchBoard, url).detach();
}

// --- the page ----------------------------------------------------------------------------------

void weekLines(int week) {
  int a = 0, b = 0;
  const int boss = portRaidOfWeek(week, &a, &b);
  ImGui::Text(portTr("This week (from %s, UTC): %s"), weekText(week).c_str(), portTr(portRaidBossName(boss)));
  ImGui::BulletText(portTr("HP: %d with two players, %d with three, %d with four"), portRaidHp(boss, 2),
                    portRaidHp(boss, 3), portRaidHp(boss, 4));
  ImGui::BulletText(portTr("The shell's attacks borrow %s"), portTr(portRaidBorrowName(a)));
  ImGui::BulletText(portTr("The last stand's borrow %s and %s"), portTr(portRaidBorrowName(a)),
                    portTr(portRaidBorrowName(b)));
  int na = 0, nb = 0;
  const int next = portRaidOfWeek(week + 1, &na, &nb);
  ImGui::TextDisabled(portTr("Next week: %s"), portTr(portRaidBossName(next)));
}

void section(int inGame) {
  readName();
  ImGui::TextWrapped("%s", portTr("A boss of the game's own, much stronger, for a full party: two to four players "
                                  "online, with controllers on this computer, or with Brothers mode's Luigi. Four "
                                  "phases: a duo move breaks its shell, everyone's Superguards stop its fury, and "
                                  "its attacks borrow other bosses'. A new boss every Monday (UTC), the same for "
                                  "everyone."));
  const int week = portRaidWeek();
  weekLines(week);
  hint(portTr("Mario's side is lent the boss rush's late-game loadout for the fight (level 30, every partner "
              "upgraded); your badges stay as they are. Win or lose, Mario is back where he was."));
  const bool online = portOnlineActive() != 0;
  const PortRaidStatus* st = portRaidStatus();
  ImGui::BeginDisabled(!inGame || online || st->active);
  if (ImGui::Button(portTrLabel("Start this week's raid##raid"))) {
    portRaidAsk();
    sMessage = portTr("The raid begins once this page is closed (with two players or more).");
  }
  ImGui::EndDisabled();
  if (online) {
    hint(portTr("Online, start it from the Start menu (Settings, Modes; or the Online tab), so it starts in every "
                "game at once."));
  } else if (!inGame) {
    hint(portTr("Start it while a file is in play."));
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }

  const PortRaidResult* last = portRaidLast();
  if (last->result != 0) {
    ImGui::Separator();
    if (last->result == 1) {
      ImGui::Text(portTr("Last raid: %s beaten in %d turns (%s), %d players"), portTr(portRaidBossName(last->boss)),
                  last->turns, clock(last->frames).c_str(), last->players);
      if (online && last->names[0][0] != '\0') {
        ImGui::TextDisabled("%s", partyName(last).c_str());
      } else {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::InputTextWithHint(portTrLabel("##raidname"), portTr("Your party's name"), sName, sizeof(sName))) {
          saveName();
        }
        ImGui::SameLine();
      }
      bool busy;
      {
        std::lock_guard<std::mutex> lock(sLock);
        busy = sBusy;
      }
      ImGui::BeginDisabled(busy || sSentSerial == last->serial || partyName(last).empty() ||
                           last->week < week - 1);
      if (ImGui::Button(portTrLabel("Send our clear##raid"))) {
        askBoard(last->week, last);
      }
      ImGui::EndDisabled();
    } else {
      ImGui::Text(portTr("Last raid: %s, not beaten (it ended in phase %d)"), portTr(portRaidBossName(last->boss)),
                  last->phase);
    }
  }

  ImGui::SeparatorText(portTr("This week's board"));
  bool busy;
  {
    std::lock_guard<std::mutex> lock(sLock);
    busy = sBusy;
  }
  ImGui::BeginDisabled(busy);
  if (ImGui::Button(portTrLabel("Show the board##raid"))) {
    askBoard(week, nullptr);
  }
  ImGui::EndDisabled();
  std::lock_guard<std::mutex> lock(sLock);
  if (!sBoardError.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", portTr(sBoardError.c_str()));
  }
  if (sBoardWeek >= 0) {
    ImGui::Text(portTr("Week of %s: %d parties cleared it"), weekText(sBoardWeek).c_str(), sBoardClears);
    if (sYourRank > 0) {
      ImGui::SameLine();
      ImGui::Text(portTr("(you: #%d)"), sYourRank);
    }
    if (sBoardWeek == week && sBoardBoss >= 0 && sBoardBoss != portRaidOfWeek(week, nullptr, nullptr)) {
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
                         portTr("The relay has another boss this week: is this computer's date right?"));
    }
    for (size_t i = 0; i < sBoard.size(); i++) {
      ImGui::Text(portTr("%2d. %d turns  %s  %dP  %s"), static_cast<int>(i + 1), sBoard[i].turns,
                  clock(sBoard[i].frames).c_str(), sBoard[i].players, sBoard[i].name.c_str());
    }
  }
}

// --- the Start menu: start it, and the week's raid on a page ------------------------------------

int infoLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0, a = 0, b = 0;
  const int week = portRaidWeek();
  const int boss = portRaidOfWeek(week, &a, &b);
  auto line = [&](const std::string& text) {
    if (n < max) {
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", text.c_str());
    }
  };
  char text[PORT_PAUSE_LINE];
  std::snprintf(text, sizeof(text), portTrGame("From %s: %s"), weekText(week).c_str(),
                portTrGame(portRaidBossName(boss)));
  line(text);
  std::snprintf(text, sizeof(text), portTrGame("HP: %d (two players) to %d (four)"), portRaidHp(boss, 2),
                portRaidHp(boss, 4));
  line(text);
  line(portTrGame("1: the boss as it fights, stronger."));
  std::snprintf(text, sizeof(text), portTrGame("2: a shell (1 damage) a duo move breaks; %s."),
                portTrGame(portRaidBorrowName(a)));
  line(text);
  line(portTrGame("3: fury: piercing hits; half damage until"));
  line(portTrGame("   everyone has Superguarded once."));
  std::snprintf(text, sizeof(text), portTrGame("4: %s and %s; duo moves x2."), portTrGame(portRaidBorrowName(a)),
                portTrGame(portRaidBorrowName(b)));
  line(text);
  return n;
}

const PortPauseInfo kInfo = {"This week's raid", infoLines, nullptr, nullptr, nullptr, nullptr, 0};

enum { kRowStart = 0, kRowInfo = 1 };

int formRows(PortPauseRow* out, int max) {
  if (max < 2) {
    return 0;
  }
  const PortRaidStatus* st = portRaidStatus();
  PortPauseRow r = portPauseRow("Raid boss", "This week's raid for two to four players: it begins once the menu "
                                "is closed. Win or lose, Mario is back here after.",
                                nullptr, kRowStart, st->active ? PORT_PAUSE_ROW_DIM : 0);
  std::snprintf(r.value, sizeof(r.value), "%s", portTrGame(portRaidBossName(portRaidOfWeek(portRaidWeek(), nullptr,
                                                                                           nullptr))));
  out[0] = r;
  out[1] = portPauseRow("This week's raid", "The boss, its phases and what they borrow.", nullptr, kRowInfo);
  return 2;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (id == kRowInfo) {
    portPauseInfoOpen(&kInfo);
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  const char* why = nullptr;
  return portRaidStart(&why) ? "The raid begins once the menu is closed." : why;
}

const PortPauseForm kForm = {kTab, "Raid boss", formRows, formAct};

// --- over the picture, during a raid -------------------------------------------------------------

float textWidth(float size, const char* text) { return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x; }

void frame(void*) {
  const PortRaidStatus* st = portRaidStatus();
  static int sShownPhase;
  static unsigned sBannerUntil;
  static unsigned sTick;
  sTick++;
  if (!st->active || !st->inFight || st->maxHp <= 0) {
    sShownPhase = 0;
    return;
  }
  if (st->phase != sShownPhase) {
    sShownPhase = st->phase;
    sBannerUntil = sTick + 180;
  }
  // a bar across the top: the boss's HP with the phases' lines, its name and phase, what to do now
  const float x0 = 150.0f, w = 340.0f, y0 = 64.0f; // under the battle HUD
  char title[128];
  std::snprintf(title, sizeof(title), portTr("Raid: %s"), portTr(portRaidBossName(st->boss)));
  portModHostDrawRect(x0 - 6.0f, y0 - 4.0f, w + 12.0f, 50.0f, 0x000000A0u);
  portModHostDrawText(x0, y0, 13.0f, 0xFFD860FFu, title);
  char hp[32];
  std::snprintf(hp, sizeof(hp), "%d / %d", st->hp, st->maxHp);
  portModHostDrawText(x0 + w - textWidth(12.0f, hp), y0 + 1.0f, 12.0f, 0xFFFFFFFFu, hp);
  const float barY = y0 + 18.0f;
  portModHostDrawRect(x0, barY, w, 8.0f, 0x402020FFu);
  const float f = std::max(0.0f, std::min(1.0f, float(st->hp) / float(st->maxHp)));
  portModHostDrawRect(x0, barY, w * f, 8.0f, st->gateOpen ? 0xE04040FFu : 0xA0A0C0FFu);
  for (int k = 1; k < PORT_RAID_PHASES; k++) {
    portModHostDrawRect(x0 + w * float(k) / PORT_RAID_PHASES - 1.0f, barY - 2.0f, 2.0f, 12.0f, 0xFFFFFFFFu);
  }
  std::string now = portTr(kPhaseName[st->phase - 1]);
  char what[160] = "";
  switch (st->phase) {
    case 2:
      if (!st->gateOpen) {
        std::snprintf(what, sizeof(what), "%s", portTr("Its shell takes 1 damage a hit: break it with a duo move."));
      }
      break;
    case 3:
      if (!st->gateOpen) {
        std::snprintf(what, sizeof(what), portTr("Superguard its fury, everyone: %d of %d"), st->guards,
                      st->guardsNeeded);
      } else {
        std::snprintf(what, sizeof(what), "%s", portTr("Staggered: hit it hard!"));
      }
      break;
    case 4:
      std::snprintf(what, sizeof(what), "%s", portTr("Duo moves do double damage."));
      break;
  }
  portModHostDrawText(x0, barY + 11.0f, 12.0f, 0xFFFFFFFFu, now.c_str());
  if (what[0] != '\0') {
    portModHostDrawText(x0 + w - textWidth(12.0f, what), barY + 11.0f, 12.0f, 0xFFE080FFu, what);
  }
  // a new phase: its name large in the middle for three seconds
  if (sTick < sBannerUntil && st->phase > 1) {
    const char* big = portTr(kPhaseName[st->phase - 1]);
    const float bw = textWidth(24.0f, big);
    portModHostDrawRect(320.0f - bw * 0.5f - 12.0f, 150.0f, bw + 24.0f, 36.0f, 0x000000B0u);
    portModHostDrawText(320.0f - bw * 0.5f, 155.0f, 24.0f, 0xFFD860FFu, big);
  }
}

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Raid boss", nullptr, PORT_SETTING_CHANGES_PLAY, section,
                           "raid boss weekly rotation party four players superguard duo moves phases board");
    portPauseAddForm(&kForm);
    portModHostAddDraw(frame, nullptr);
  }
} sRegister;

} // namespace
