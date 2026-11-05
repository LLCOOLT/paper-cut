// The Modes tab's daily seed (port/include/port/party.h, docs/MODES.md): the same randomizer seed and challenge
// rules for everyone each day, from the date (UTC), and a results board on the relay (port/relay/src/index.js,
// "/daily/<YYYYMMDD>").
//
// The day's game: Paper Cut's open world (the built-in randomizer's, port/archipelago/ttyd_rando.h; only its
// settings are written here) with the seed "DAILY-YYYYMMDD", and two of the challenge rules drawn from the
// date, every other rule at its default, so every player's file is the same game. "Play today's seed" keeps
// the player's own settings (<user>/daily_before.txt) and puts them back on "Put my settings back". The goal:
// portDailyGoalStars() Crystal Stars, of any of the open world's chapters (the file's goal set to it, so the
// logic makes that many reachable); the clock is the file's frames (daily_aurora.c). A finished time can be
// sent to the day's board, the best per name kept.
#include "port/companion_menu.h"
#include "port/gameplus.h"
#include "port/http.h"
#include "port/online.h"
#include "port/party.h"
#include "port/pause_forms.h"
#include "port/playmodes.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"
#include "../../archipelago/ttyd_rando.h"
#include "../settings_ui.h"
#include "../relay_util.h"

#include <imgui.h>

#include <atomic>
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

struct Rule {
  const char* key;
  int value;
  const char* label; // English
};

// the rules a day may draw (two of them)
const Rule kDrawn[] = {
    {"remix_enemies", 1, "Remix: each field enemy's fight drawn from its area's"},
    {"remix_badge_draft", 1, "A badge draft at each chapter's title card"},
    {"challenge_no_battle_items", 1, "No items in battle"},
    {"challenge_guard", 1, "Superguards only (no plain Guard)"},
    {"challenge_commands", 1, "Harder attack commands"},
    {"difficulty", 2, "Hard enemies (HP and attacks 150 %)"},
};
constexpr int kDrawnCount = int(sizeof(kDrawn) / sizeof(kDrawn[0]));

// every rule a daily sets (the drawn ones, or their defaults), so all players' games match
const char* const kKeys[] = {"difficulty",        "ac_attack_assist", "ac_guard_assist",           "gameover_retry",
                             "challenge_ohko",    "challenge_guard",  "challenge_commands",        "challenge_no_battle_items",
                             "remix_enemies",     "remix_badge_draft", "remix_seed",               "hardcore_next"};

struct Day {
  int date = 0;
  std::string seed;
  int rule[2] = {0, 1};
  bool shines = false;
};

Day dayOf(int date) {
  Day d;
  d.date = date;
  char seed[32];
  std::snprintf(seed, sizeof(seed), "DAILY-%08d", date);
  d.seed = seed;
  PortPartyRand r;
  r.s = portPartyHash(seed);
  d.rule[0] = portPartyBelow(&r, kDrawnCount);
  d.rule[1] = (d.rule[0] + 1 + portPartyBelow(&r, kDrawnCount - 1)) % kDrawnCount;
  d.shines = portPartyBelow(&r, 2) != 0;
  return d;
}

// the rules' settings declared (the challenge and remix ones are declared where they're first used)
void declareRules() {
  portChallengeDeclare();
  portRemixDeclare();
}

int valueFor(const Day& d, const char* key) {
  for (int k : d.rule) {
    if (std::strcmp(kDrawn[k].key, key) == 0) {
      return kDrawn[k].value;
    }
  }
  if (std::strcmp(key, "remix_seed") == 0) {
    return d.date % 100000;
  }
  int def = 0;
  portSettingsDefault(key, &def);
  return def;
}

std::string beforePath() { return std::string(portSettingsUserPath()) + "/daily_before.txt"; }

bool haveBefore() {
  FILE* f = std::fopen(beforePath().c_str(), "r");
  if (f == nullptr) {
    return false;
  }
  std::fclose(f);
  return true;
}

// the player's own settings kept, then the day's put in place
void playDay(const Day& d) {
  declareRules();
  PortRandoSettings* s = portRandoSettings(portSettingsUserPath());
  if (!haveBefore()) {
    if (FILE* f = std::fopen(beforePath().c_str(), "w")) {
      std::fprintf(f, "rando %d %d %d %d %d %d %d %d\nseed %s\n", s->enabled, s->badges, s->items, s->starPieces,
                   s->shineSprites, s->keyItems, s->spoiler, s->openWorld, s->seed[0] != '\0' ? s->seed : "-");
      std::fprintf(f, "goal %d\n", s->openGoal);
      for (const char* key : kKeys) {
        if (const int* v = portSettingsFind(key)) {
          std::fprintf(f, "set %s %d\n", key, *v);
        }
      }
      std::fclose(f);
    }
  }
  s->enabled = 1;
  s->openWorld = 1;
  s->keyItems = 0;
  s->badges = 1;
  s->items = 1;
  s->starPieces = 1;
  s->shineSprites = d.shines ? 1 : 0;
  s->spoiler = 0;
  s->openGoal = portDailyGoalStars();
  std::snprintf(s->seed, sizeof(s->seed), "%s", d.seed.c_str());
  portRandoSaveSettings();
  for (const char* key : kKeys) {
    if (int* v = portSettingsFind(key)) {
      *v = valueFor(d, key);
    }
  }
  portSettingsSave();
}

bool putBack() {
  FILE* f = std::fopen(beforePath().c_str(), "r");
  if (f == nullptr) {
    return false;
  }
  PortRandoSettings* s = portRandoSettings(portSettingsUserPath());
  char line[256];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char key[64], seed[64];
    int v = 0;
    int a[8];
    if (std::sscanf(line, "rando %d %d %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6],
                    &a[7]) == 8) {
      s->enabled = a[0];
      s->badges = a[1];
      s->items = a[2];
      s->starPieces = a[3];
      s->shineSprites = a[4];
      s->keyItems = a[5];
      s->spoiler = a[6];
      s->openWorld = a[7];
    } else if (std::sscanf(line, "goal %d", &v) == 1) {
      s->openGoal = v;
    } else if (std::sscanf(line, "seed %63s", seed) == 1) {
      std::snprintf(s->seed, sizeof(s->seed), "%s", std::strcmp(seed, "-") == 0 ? "" : seed);
    } else if (std::sscanf(line, "set %63s %d", key, &v) == 2) {
      if (int* p = portSettingsFind(key)) {
        *p = v;
      }
    }
  }
  std::fclose(f);
  std::remove(beforePath().c_str());
  portRandoSaveSettings();
  portSettingsSave();
  return true;
}

bool dayInPlace(const Day& d) {
  declareRules();
  PortRandoSettings* s = portRandoSettings(portSettingsUserPath());
  if (!s->enabled || !s->openWorld || s->openGoal != portDailyGoalStars() || d.seed != s->seed) {
    return false;
  }
  for (const char* key : kKeys) {
    const int* v = portSettingsFind(key);
    if (v != nullptr && *v != valueFor(d, key)) {
      return false;
    }
  }
  return true;
}

std::string clock(unsigned long long ms) {
  char text[32];
  std::snprintf(text, sizeof(text), "%llu:%02llu:%02llu.%01llu", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60,
                ms / 100 % 10);
  return text;
}

unsigned long long msOf(unsigned frames) { return static_cast<unsigned long long>(frames) * 1000ull / 60ull; }

std::string dateText(int date) {
  char text[16];
  std::snprintf(text, sizeof(text), "%04d-%02d-%02d", date / 10000, date / 100 % 100, date % 100);
  return text;
}

// --- the board ------------------------------------------------------------------

struct Entry {
  unsigned long long ms;
  std::string name, game;
};

std::mutex sLock;
std::vector<Entry> sBoard;
int sBoardDate = 0, sBoardCount = 0, sYourRank = 0;
std::string sBoardError;
std::atomic<bool> sBusy{false};
char sName[32] = "";
bool sNameRead = false;
std::string sMessage;

std::string namePath() { return std::string(portSettingsUserPath()) + "/daily_name.txt"; }

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

// "daily <DATE> <COUNT> [<YOUR RANK>]", then "<rank> <ms> <name> <game>" lines
void fetchBoard(std::string url, int date) {
  std::vector<uint8_t> body;
  std::string error;
  const bool ok = port_http::fetch(url, body, 64 * 1024, error);
  std::lock_guard<std::mutex> lock(sLock);
  if (!ok) {
    sBoardError = error;
    sBusy = false;
    return;
  }
  const std::string text(body.begin(), body.end());
  std::vector<Entry> got;
  int count = 0, rank = 0, gotDate = 0;
  bool header = false;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    char word[16] = "", name[96] = "", game[16] = "";
    unsigned long long ms = 0;
    int n = 0, r = 0;
    if (std::sscanf(line.c_str(), "daily %d %d %d", &gotDate, &count, &rank) >= 2) {
      header = true;
    } else if (std::sscanf(line.c_str(), "%d %llu %95s %15s", &r, &ms, name, game) >= 3) {
      got.push_back({ms, unpercent(name), game});
    }
    (void)word;
    (void)n;
  }
  if (!header) {
    sBoardError = "The board's answer wasn't understood.";
  } else {
    sBoard = got;
    sBoardDate = gotDate != 0 ? gotDate : date;
    sBoardCount = count;
    sYourRank = rank;
    sBoardError.clear();
  }
  sBusy = false;
}

void askBoard(int date, unsigned long long sendMs) {
  if (sBusy) {
    return;
  }
  const std::string base = relayBase();
  if (base.empty()) {
    std::lock_guard<std::mutex> lock(sLock);
    sBoardError = "This build has no relay.";
    return;
  }
  char path[64];
  std::snprintf(path, sizeof(path), "/daily/%08d", date);
  std::string url = base + path;
  if (sendMs > 0) {
    char game[8];
    std::snprintf(game, sizeof(game), "%.6s", portVersionSelf()->id);
    url += "?name=" + percent(sName) + "&ms=" + std::to_string(sendMs) + "&game=" + game + "&v=" + PORT_VERSION;
  }
  sBusy = true;
  std::thread(fetchBoard, url, date).detach();
}

// --- the page ---------------------------------------------------------------------

void section(int inGame) {
  readName();
  const Day today = dayOf(portDailyToday());
  ImGui::TextWrapped(portTr("The same seed and challenge rules for everyone each day: Paper Cut's open world "
                            "from the day's seed, with two rules drawn from the date. The goal is any %d Crystal "
                            "Stars, from whichever of the open world's chapters; the clock is the file's own, every "
                            "frame played. Send your time to the day's board."),
                     portDailyGoalStars());
  if (!portRandoSupported()) {
    hint(portTr("The randomizer works on the US version only, so the daily seed does too."));
    return;
  }
  ImGui::Text(portTr("Today (%s, UTC): seed %s"), dateText(today.date).c_str(), today.seed.c_str());
  for (int k : today.rule) {
    ImGui::BulletText("%s", portTr(kDrawn[k].label));
  }
  ImGui::BulletText("%s", portTr(today.shines ? "Shine Sprites shuffled too" : "Shine Sprites where the game has them"));
  if (dayInPlace(today)) {
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s",
                       portTr("Today's seed is set: begin a new file from the title screen."));
  } else if (ImGui::Button(portTrLabel("Play today's seed##daily"))) {
    playDay(today);
    sMessage = portTr("Set. Begin a new file from the title screen (Restart now first, if a file is in play).");
  }
  if (haveBefore()) {
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Put my settings back##daily"))) {
      putBack();
      sMessage = portTr("Your own randomizer and challenge settings are back.");
    }
  }
  if (inGame && dayInPlace(today) && portDailyFileDate() == 0 && portRestartSupported()) {
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Restart now##daily"))) {
      portRestartRequest(0);
    }
  }

  const int fileDate = portDailyFileDate();
  if (fileDate != 0) {
    ImGui::Separator();
    const unsigned done = portDailyDoneFrames();
    if (done != 0) {
      ImGui::Text(portTr("This file: the daily of %s, done in %s"), dateText(fileDate).c_str(),
                  clock(msOf(done)).c_str());
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
      if (ImGui::InputTextWithHint(portTrLabel("##dailyname"), portTr("Your name"), sName, sizeof(sName))) {
        saveName();
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(sName[0] == '\0' || sBusy);
      if (ImGui::Button(portTrLabel("Send my time##daily"))) {
        askBoard(fileDate, msOf(done));
      }
      ImGui::EndDisabled();
    } else {
      ImGui::Text(portTr("This file: the daily of %s, %s so far"), dateText(fileDate).c_str(),
                  clock(msOf(portDailyFrames())).c_str());
    }
  }

  ImGui::SeparatorText(portTr("Results board"));
  const int boardDate = fileDate != 0 ? fileDate : today.date;
  ImGui::BeginDisabled(sBusy);
  if (ImGui::Button(portTrLabel("Show the board##daily"))) {
    askBoard(boardDate, 0);
  }
  ImGui::EndDisabled();
  std::lock_guard<std::mutex> lock(sLock);
  if (!sBoardError.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", portTr(sBoardError.c_str()));
  }
  if (sBoardDate != 0) {
    ImGui::Text(portTr("%s: %d finished"), dateText(sBoardDate).c_str(), sBoardCount);
    if (sYourRank > 0) {
      ImGui::SameLine();
      ImGui::Text(portTr("(you: #%d)"), sYourRank);
    }
    for (size_t i = 0; i < sBoard.size(); i++) {
      ImGui::Text("%2d. %s  %s", static_cast<int>(i + 1), clock(sBoard[i].ms).c_str(), sBoard[i].name.c_str());
    }
  }
  if (!sMessage.empty()) {
    ImGui::TextWrapped("%s", sMessage.c_str());
  }
}

// --- the Start menu: the page's day, its start and this file's time (port/include/port/pause_forms.h).
// This device's own (its settings, its relay, its name), so online and in replays one dimmed row ---

// UTF-8 (a name typed on the page) for the game's font: Latin-1 on the US and EU releases, ASCII on JP
std::string gameText(const std::string& utf8) {
  std::string out;
  for (size_t i = 0; i < utf8.size(); i++) {
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    if (c < 0x80) {
      out += c == '<' ? '(' : c == '>' ? ')' : char(c);
    } else if (PORT_GAME_ID[3] != 'J' && (c == 0xC2 || c == 0xC3) && i + 1 < utf8.size()) {
      out += char(((c & 0x03) << 6) | (static_cast<unsigned char>(utf8[++i]) & 0x3F));
    } else if ((c & 0xC0) == 0xC0) {
      out += '?';
    }
  }
  return out;
}

// today's page: the date, the seed and the day's rules
int dayLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0;
  const Day today = dayOf(portDailyToday());
  if (max < 6) {
    return 0;
  }
  std::snprintf(out[n++], PORT_PAUSE_LINE, "%s  %s", dateText(today.date).c_str(), today.seed.c_str());
  for (int k : today.rule) {
    std::snprintf(out[n++], PORT_PAUSE_LINE, "- %s", portTrGame(kDrawn[k].label));
  }
  std::snprintf(out[n++], PORT_PAUSE_LINE, "- %s",
                portTrGame(today.shines ? "Shine Sprites shuffled too" : "Shine Sprites where the game has them"));
  return n;
}

const PortPauseInfo kDayInfo = {"Daily seed", dayLines, nullptr, nullptr, nullptr, nullptr, 0};

// the results board (fetched as it opens; A fetches it again)
int sShownDate;

int boardLines(char (*out)[PORT_PAUSE_LINE], int max) {
  int n = 0;
  std::lock_guard<std::mutex> lock(sLock);
  if (sBusy) {
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", portTrGame("Fetching the board..."));
    return n;
  }
  if (!sBoardError.empty()) {
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", gameText(sBoardError).c_str());
  }
  if (sBoardDate != 0 && n < max) {
    char line[PORT_PAUSE_LINE];
    std::snprintf(line, sizeof(line), portTrGame("%s: %d finished"), dateText(sBoardDate).c_str(), sBoardCount);
    std::snprintf(out[n++], PORT_PAUSE_LINE, "%s", line);
    for (size_t i = 0; i < sBoard.size() && n < max; i++) {
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%2d. %s  %s", static_cast<int>(i + 1), clock(sBoard[i].ms).c_str(),
                    gameText(sBoard[i].name).c_str());
    }
  }
  return n;
}

int boardButton(int button) {
  if (button == 'A') {
    askBoard(sShownDate, 0);
  }
  return 0;
}

const char* boardHelp() { return "{A} fetches the board again."; }

const PortPauseInfo kBoardInfo = {"Results board", boardLines, boardButton, boardHelp, nullptr, nullptr, 0};

void nameTyped(const char* text) {
  std::snprintf(sName, sizeof(sName), "%s", text);
  saveName();
}

enum { kRowToday, kRowPlay, kRowBack, kRowRestart, kRowName, kRowSend, kRowBoard };

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 8) {
    return 0;
  }
  readName();
  const Day today = dayOf(portDailyToday());
  if (!portRandoSupported()) {
    out[n++] = portPauseRow("Today's seed", "The randomizer works on the US version only, so the daily seed does too.",
                            nullptr, kRowToday, PORT_PAUSE_ROW_DIM);
    return n;
  }
  PortPauseRow d = portPauseRow("Today's seed", "The day's seed and its two rules.", nullptr, kRowToday);
  std::snprintf(d.value, sizeof(d.value), "%s", dateText(today.date).c_str());
  out[n++] = d;
  const bool set = dayInPlace(today);
  out[n++] = portPauseRow("Play today's seed",
                          set ? "Today's seed is set: begin a new file from the title screen."
                              : "Set. Begin a new file from the title screen (Restart now first, if a file is in play).",
                          set ? "Set" : nullptr, kRowPlay, set ? PORT_PAUSE_ROW_DIM : PORT_PAUSE_ROW_CONFIRM);
  if (haveBefore()) {
    out[n++] = portPauseRow("Put my settings back", "Your own randomizer and challenge settings are back.", nullptr,
                            kRowBack, PORT_PAUSE_ROW_CONFIRM);
  }
  const int fileDate = portDailyFileDate();
  if (set && fileDate == 0 && portRestartSupported()) {
    out[n++] = portPauseRow("Restart now", "Press {A} twice to restart; quick resume brings you back.", nullptr,
                            kRowRestart, PORT_PAUSE_ROW_CONFIRM);
  }
  if (fileDate != 0) {
    const unsigned done = portDailyDoneFrames();
    PortPauseRow h = portPauseRow(nullptr, nullptr, nullptr, -1, PORT_PAUSE_ROW_HEADING);
    std::snprintf(h.text, sizeof(h.text), "%s  %s%s", dateText(fileDate).c_str(), done != 0 ? "* " : "",
                  clock(msOf(done != 0 ? done : portDailyFrames())).c_str());
    out[n++] = h;
    if (done != 0) {
      PortPauseRow name = portPauseRow("Your name", "The name your time goes to the board with.", nullptr, kRowName);
      std::snprintf(name.value, sizeof(name.value), "%s", gameText(sName).c_str());
      out[n++] = name;
      out[n++] = portPauseRow("Send my time", "This file's time to the day's board.", nullptr, kRowSend,
                              sName[0] == 0 || sBusy ? PORT_PAUSE_ROW_DIM : 0);
    }
  }
  out[n++] = portPauseRow("Show the board", "The day's results board.", nullptr, kRowBoard);
  return n;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  const Day today = dayOf(portDailyToday());
  const int fileDate = portDailyFileDate();
  switch (id) {
  case kRowToday:
    if (portRandoSupported()) {
      portPauseInfoOpen(&kDayInfo);
    }
    return nullptr;
  case kRowPlay:
    if (dayInPlace(today)) {
      return "Today's seed is set: begin a new file from the title screen.";
    }
    if (gPortCompanionMenu) {
      return "Not on the second screen: use the game's Start menu.";
    }
    playDay(today);
    return "Set. Begin a new file from the title screen (Restart now first, if a file is in play).";
  case kRowBack:
    if (gPortCompanionMenu) {
      return "Not on the second screen: use the game's Start menu.";
    }
    putBack();
    return "Your own randomizer and challenge settings are back.";
  case kRowRestart:
    if (gPortCompanionMenu) {
      return "Not on the second screen: use the game's Start menu.";
    }
    portRestartRequest(0);
    return nullptr;
  case kRowName:
    portPauseTextOpen("Your name", gameText(sName).c_str(), int(sizeof(sName)) - 1, PORT_PAUSE_CHARS_NAME, nameTyped);
    return nullptr;
  case kRowSend:
    if (sName[0] == 0) {
      return "The name your time goes to the board with.";
    }
    askBoard(fileDate, msOf(portDailyDoneFrames()));
    sShownDate = fileDate;
    portPauseInfoOpen(&kBoardInfo);
    return nullptr;
  case kRowBoard:
    sShownDate = fileDate != 0 ? fileDate : today.date;
    askBoard(sShownDate, 0);
    portPauseInfoOpen(&kBoardInfo);
    return nullptr;
  }
  return nullptr;
}

const PortPauseForm kForm = {kTab, "Daily seed", formRows, formAct, PORT_PAUSE_FORM_OWN};

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Daily seed", nullptr, PORT_SETTING_CHANGES_PLAY, section,
                           "daily seed challenge randomizer race leaderboard results board today");
    portPauseAddForm(&kForm);
  }
} sRegister;

} // namespace
