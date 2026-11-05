// The Modes tab's daily seed (port/include/port/party.h, docs/MODES.md): the same randomizer seed and challenge
// rules for everyone each day, from the date (UTC), and a results board on the relay (port/relay/src/index.js,
// "/daily/<YYYYMMDD>").
//
// The day's game: Paper Cut's open world (the built-in randomizer's, port/archipelago/ttyd_rando.h; only its
// settings are written here) with the seed "DAILY-YYYYMMDD", and two of the challenge rules drawn from the
// date, every other rule at its default, so every player's file is the same game. "Play today's seed" keeps
// the player's own settings (<user>/daily_before.txt) and puts them back on "Put my settings back". The goal:
// both of the first two Crystal Stars; the clock is the file's frames (daily_aurora.c). A finished time can be
// sent to the day's board, the best per name kept.
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

#include <imgui.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
  if (!s->enabled || !s->openWorld || d.seed != s->seed) {
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

std::string relayBase() {
  for (int i = 0; portOnlineRelayAt(i) != nullptr; i++) {
    const std::string relay = portOnlineRelayAt(i);
    if (relay.compare(0, 6, "wss://") == 0) {
      return "https://" + relay.substr(6);
    }
  }
  return "";
}

std::string percent(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
        c == '.') {
      out += static_cast<char>(c);
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

std::string unpercent(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

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

void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void section(int inGame) {
  readName();
  const Day today = dayOf(portDailyToday());
  ImGui::TextWrapped("%s", portTr("The same seed and challenge rules for everyone each day: Paper Cut's open world "
                                  "from the day's seed, with two rules drawn from the date. The goal is the first two "
                                  "Crystal Stars, in either order; the clock is the file's own, every frame played. "
                                  "Send your time to the day's board."));
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

// --- the Start menu: the day and this file's clock ----------------------------------------------

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  PortPauseRow r = portPauseRow("Daily seed", "Today's seed and rules, and the board, are on the full settings "
                                "page's Modes tab.", nullptr, 0);
  const int date = portDailyFileDate();
  if (date != 0) {
    const unsigned done = portDailyDoneFrames();
    std::snprintf(r.value, sizeof(r.value), "%s%s", done != 0 ? "* " : "",
                  clock(msOf(done != 0 ? done : portDailyFrames())).c_str());
  } else {
    std::snprintf(r.value, sizeof(r.value), "%s", dateText(portDailyToday()).c_str());
  }
  out[0] = r;
  return 1;
}

const char* formAct(int, int) { return nullptr; }

const PortPauseForm kForm = {kTab, "Daily seed", formRows, formAct};

struct Register {
  Register() {
    portSettingsAddSection(kTab, "Daily seed", nullptr, PORT_SETTING_CHANGES_PLAY, section,
                           "daily seed challenge randomizer race leaderboard results board today");
    portPauseAddForm(&kForm);
  }
} sRegister;

} // namespace
