// Paper notes (port/include/port/notes.h, docs/PAPER_NOTES.md): the phrases
// and words, the setting, and the relay's side of the game's: an area's
// notes fetched, a note left, one rated, each on a thread of its own so the
// game never waits on the network. What this device left and rated is kept
// in <user>/notes.txt (its notes' ids, and its ratings), to show them as
// such; the relay keeps the count of each address's.
#include "port/file_pc.h"
#include "port/detach.h"
#include "port/http.h"
#include "port/notes.h"
#include "port/online.h"
#include "port/pm64.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" const char* msgSearch(const char* tag); // the game's text (msgdrv.c)

namespace {

// The phrases and words: their numbers are what the relay keeps, so only
// ever add at the end (a game that doesn't know a number shows none of it):
// never reorder, rename or remove one. tools/port/notes_lists.txt is the
// lists as released, and testpass.py lint fails unless these start with it.
// A phrase with no %s takes no word (the page hides it, the note keeps 0).
constexpr const char* kPhrases[] = {
    "%s ahead",       "Try %s",          "Look out for %s", "%s here!",           "Don't forget %s",
    "No %s here",     "If only I had %s...", "Use %s here", "Hidden %s?",         "Praise %s!",
    "Be wary of %s",  "Time for %s",
    // 0.5.0, the second batch
    "Thank you!",     "I did it!",       "Don't give up!",  "Check behind %s",    "Remember %s",
    "Hooray for %s",
};
constexpr const char* kWords[] = {
    "an enemy",      "a strong enemy", "a boss",      "treasure",      "a hidden block", "a Star Piece",
    "a Shine Sprite", "a badge",       "an item",     "coins",         "a secret",       "a shortcut",
    "a dead end",    "a pipe",         "a door",      "a switch",      "a Save Block",   "a Heart Block",
    "the hammer",    "a Spin Jump",    "Paper mode",  "Plane mode",    "Tube mode",      "Boat mode",
    "Goombella",     "Koops",          "Flurrie",     "Yoshi",         "Vivian",         "Bobbery",
    "Ms. Mowz",      "Mario",          "a trap",      "the way up",    "the way down",   "a fight",
    "patience",      "courage",        "a rest",      "a good view",
    // 0.5.0, the second batch (40-71)
    "Hooktail",      "Gloomtail",      "Bonetail",    "Doopliss",      "Smorg",          "the Shadow Sirens",
    "Magnus von Grapple", "Cortez",    "Grodus",      "the Shadow Queen", "Lord Crump",  "Blooper",
    "Macho Grubba",  "a Superguard",   "an action command", "the Super Hammer", "the Ultra Hammer",
    "the Super Boots", "the Ultra Boots", "Bowser",   "Peach",         "Luigi",          "the music",
    "this room",
    // Paper Mario 64's partners: offered with a PM64 ROM linked (shown to anyone: plain names)
    "Goombario",     "Kooper",         "Bombette",    "Parakarry",     "Bow",            "Watt",
    "Sushie",        "Lakilester",
};
constexpr int kPhraseCount = int(sizeof(kPhrases) / sizeof(kPhrases[0]));
constexpr int kWordCount = int(sizeof(kWords) / sizeof(kWords[0]));

// The words the game's own text names, in its language and version (a European disc's text
// language too): the partners' names, and the bosses' as their battles name them. The rest are
// Paper Cut's text, translated with the game's names (the battles shorten some: "Magnus", and the
// Shadow Queen in the European text). None for a word: "".
struct GameName {
  int word;
  const char* tag;
};
constexpr GameName kGameNames[] = {
    {24, "name_party0"},         {25, "name_party1"},   {26, "name_party4"},       {27, "name_party3"},
    {28, "name_party5"},         {29, "name_party2"},   {30, "name_party6"},       {40, "btl_un_gonbaba"},
    {41, "btl_un_bunbaba"},      {42, "btl_un_zonbaba"}, {43, "btl_un_rampell"},   {44, "btl_un_moamoa"},
    {47, "btl_un_cortez"},       {48, "btl_un_batten_leader"}, {50, "btl_un_kanbu1"}, {51, "btl_un_gesso"},
    {52, "btl_un_macho_gance"},
};
// the PM64 partners' words, in kWords' order, as pm64.h numbers them
constexpr int kFirstPm64 = 64;
constexpr int kPm64Partners[] = {PM64_PARTNER_GOOMBARIO, PM64_PARTNER_KOOPER, PM64_PARTNER_BOMBETTE,
                                 PM64_PARTNER_PARAKARRY, PM64_PARTNER_BOW,    PM64_PARTNER_WATT,
                                 PM64_PARTNER_SUSHIE,    PM64_PARTNER_LAKILESTER};
static_assert(kFirstPm64 + int(sizeof(kPm64Partners) / sizeof(kPm64Partners[0])) == kWordCount,
              "the PM64 partners are the last words");

const char* gameTag(int word) {
  for (const GameName& g : kGameNames) {
    if (g.word == word) {
      return g.tag;
    }
  }
  return nullptr;
}

// a word as a note shows it: game, in the game's encoding for its font, else UTF-8 (into buf when it
// has to be converted)
const char* wordText(int word, int game, char* buf, size_t size) {
  if (word >= kFirstPm64 && word < kWordCount) {
    const int p = kPm64Partners[word - kFirstPm64];
    return game ? portPm64PartnerGameName(p) : portPm64PartnerName(p);
  }
  if (const char* tag = gameTag(word)) {
    const char* s = msgSearch(tag);
    if (s != nullptr && s[0] != '\0' && std::strcmp(s, tag) != 0) {
      if (game) {
        return s;
      }
      portTextFromGame(s, buf, size);
      return buf;
    }
  }
  return game ? portTrGame(kWords[word]) : portTr(kWords[word]);
}

// An area's notes kept this long before they're fetched again, and the last few areas' kept too, so
// walking back and forth between areas asks the relay nothing (each fetch is a request of the relay's
// day, which every player shares); the Start menu's Refresh fetches at once.
constexpr Uint64 kKeepMs = 30 * 60 * 1000;
constexpr Uint64 kRetryMs = 10 * 60 * 1000; // a failed fetch tried again after this long
constexpr size_t kKeptAreas = 8;            // the area here and the last 7 before it

struct Area {
  std::string name;     // the map's first three letters
  Uint64 fetchedAt = 0; // SDL_GetTicks
  int state = PORT_NOTES_IDLE;
  std::vector<PortNote> notes;
};

std::mutex sLock;
Area sArea;               // the area here
std::vector<Area> sKept;  // the areas before it with their notes fetched, the latest first (kKeptAreas - 1)
bool sLoading;
const char* sAnswer; // the last add's or rating's answer (English, a literal)
int sAnswerSerial;
bool sWaiting;

// this device's own notes' ids and its ratings (<user>/notes.txt: "mine <id>", "rated <id> <1|-1>")
std::map<std::string, int> sRated;
std::vector<std::string> sMine;
bool sOwnRead;

std::string ownFile() { return std::string(portSettingsUserPath()) + "/notes.txt"; }

void readOwn() {
  if (sOwnRead) {
    return;
  }
  sOwnRead = true;
  if (FILE* f = portFopen(ownFile().c_str(), "r")) {
    char kind[16], id[32];
    int d = 0;
    char line[96];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (std::sscanf(line, "%15s %31s %d", kind, id, &d) >= 2) {
        if (std::strcmp(kind, "mine") == 0) {
          sMine.push_back(std::string(id).substr(0, sizeof(PortNote::id) - 1)); // (as the list's ids are read)
        } else if (std::strcmp(kind, "rated") == 0) {
          sRated[id] = d;
        }
      }
    }
    std::fclose(f);
  }
}

void writeOwn() {
  if (FILE* f = portFopen(ownFile().c_str(), "w")) {
    // (the newest 400 of each: old notes are long gone from the relay)
    const size_t from = sMine.size() > 400 ? sMine.size() - 400 : 0;
    for (size_t i = from; i < sMine.size(); i++) {
      std::fprintf(f, "mine %s\n", sMine[i].c_str());
    }
    for (const auto& [id, d] : sRated) {
      std::fprintf(f, "rated %s %d\n", id.c_str(), d);
    }
    std::fclose(f);
  }
}

std::string areaOf(const char* map) {
  if (map == nullptr || std::strlen(map) < 4 || map[3] != '_') {
    return {};
  }
  for (int i = 0; i < 3; i++) {
    if (map[i] < 'a' || map[i] > 'z') {
      return {};
    }
  }
  return std::string(map, 3);
}

// the relay's https:// address (the first relay's: all of a release's players share its notes)
std::string relayHttps() {
  const char* relay = portOnlineRelayAt(0);
  if (relay == nullptr || std::strncmp(relay, "wss://", 6) != 0) {
    return {};
  }
  return std::string("https://") + (relay + 6);
}

// "notes <N>", then "<ID> <MAP> <X> <Y> <Z> <PHRASE> <WORD> <SCORE>" lines
bool parse(const std::string& text, std::vector<PortNote>& out) {
  if (text.compare(0, 6, "notes ") != 0) {
    return false;
  }
  size_t at = text.find('\n');
  while (at != std::string::npos && at < text.size()) {
    size_t end = text.find('\n', at + 1);
    const std::string line = text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
    at = end;
    PortNote n{};
    if (std::sscanf(line.c_str(), "%15s %15s %d %d %d %d %d %d", n.id, n.map, &n.x, &n.y, &n.z, &n.phrase, &n.word,
                    &n.score) == 8 &&
        n.phrase >= 0 && n.phrase < kPhraseCount && n.word >= 0 && n.word < kWordCount && out.size() < 512) {
      out.push_back(n);
    }
  }
  return true;
}

// an area's fetched notes put by (sLock held): the latest first, the oldest dropped
void keepArea(Area area) {
  for (size_t i = 0; i < sKept.size(); i++) {
    if (sKept[i].name == area.name) {
      sKept.erase(sKept.begin() + std::ptrdiff_t(i));
      break;
    }
  }
  sKept.insert(sKept.begin(), std::move(area));
  if (sKept.size() > kKeptAreas - 1) {
    sKept.resize(kKeptAreas - 1);
  }
}

void fetchArea(std::string area, std::string url) {
  std::vector<uint8_t> body;
  std::string error;
  std::vector<PortNote> notes;
  int state = PORT_NOTES_READY;
  if (!port_http::fetch(url, body, 256 * 1024, error, "text/plain")) {
    state = error.find("404") != std::string::npos ? PORT_NOTES_OLD_RELAY : PORT_NOTES_FAILED;
    std::printf("[notes] %s: %s\n", area.c_str(), error.c_str());
  } else if (!parse(std::string(body.begin(), body.end()), notes)) {
    state = PORT_NOTES_OLD_RELAY;
  } else {
    std::printf("[notes] %s: %zu notes\n", area.c_str(), notes.size());
  }
  std::fflush(stdout);
  std::lock_guard<std::mutex> lock(sLock);
  sLoading = false;
  if (sArea.name == area) {
    sArea.state = state;
    sArea.notes = std::move(notes);
    sArea.fetchedAt = SDL_GetTicks();
  } else if (state == PORT_NOTES_READY) {
    // (left while it came: kept for coming back)
    Area kept;
    kept.name = area;
    kept.state = state;
    kept.notes = std::move(notes);
    kept.fetchedAt = SDL_GetTicks();
    keepArea(std::move(kept));
  }
}

// an area's notes asked for (sLock held); fresh (&fresh=1: Refresh, a note just left or rated) asks
// past any cache in front of the relay (it keeps none today; the relay ignores it)
void startFetch(const std::string& area, bool fresh = false) {
  const std::string relay = relayHttps();
  if (relay.empty()) {
    sArea.state = PORT_NOTES_FAILED;
    sArea.fetchedAt = SDL_GetTicks();
    return;
  }
  sLoading = true;
  sArea.state = PORT_NOTES_LOADING;
  portDetach(fetchArea, area, relay + "/notes?area=" + area + (fresh ? "&fresh=1" : ""));
}

// the relay's refusal ("<why>: <words>") in the page's English, which is translated
const char* refusal(const std::string& text, int status) {
  const std::string why = text.substr(0, text.find(':'));
  if (why == "mine") {
    return "You have as many notes in this room as can be.";
  }
  if (why == "today") {
    return status == 429 && text.find("rating") != std::string::npos ? "No more ratings today: try tomorrow."
                                                                       : "No more notes in this area today: try tomorrow.";
  }
  if (why == "own") {
    return "That one is yours.";
  }
  if (why == "rated") {
    return "You rated that one already.";
  }
  if (why == "gone") {
    return "That note isn't there any more.";
  }
  if (status == 404) {
    return "Paper notes aren't on this relay yet.";
  }
  return "The relay refused it.";
}

void send(std::string url, std::string body, std::string area, std::string rated, int d) {
  std::vector<uint8_t> out;
  int status = 0;
  std::string error;
  const char* answer;
  std::string id;
  if (!port_http::post(url, body, "text/plain", out, status, 4096, error)) {
    std::printf("[notes] %s: %s\n", body.c_str(), error.c_str());
    answer = "The relay couldn't be reached.";
  } else {
    const std::string text(out.begin(), out.end());
    if (status == 200 && text.compare(0, 3, "ok ") == 0) {
      answer = rated.empty() ? "Your note is left here." : "Rated: thank you.";
      id = text.substr(3, text.find_first_of(" \t\r\n", 3) - 3);
      // kept as the notes' list reads ids (PortNote::id: %15s), or it would never match
      id = id.substr(0, sizeof(PortNote::id) - 1);
    } else {
      answer = refusal(text, status);
    }
    std::printf("[notes] %s: %d %s", body.c_str(), status, text.c_str());
  }
  std::fflush(stdout);
  std::lock_guard<std::mutex> lock(sLock);
  if (status == 200 && rated.empty() && !id.empty()) {
    sMine.push_back(id);
    writeOwn();
  } else if (!rated.empty() && (status == 200 || (status == 409 && std::string(answer).find("already") != std::string::npos))) {
    sRated[rated] = d;
    writeOwn();
  }
  sAnswer = answer;
  sAnswerSerial++;
  sWaiting = false;
  // the area again, with the note or the rating in it
  if (sArea.name == area && status == 200 && !sLoading) {
    startFetch(area, true);
  } else if (sArea.name != area && status == 200) {
    // (left before the answer: its kept copy lacks the note or the rating, fetched again on the way back)
    for (size_t i = 0; i < sKept.size(); i++) {
      if (sKept[i].name == area) {
        sKept.erase(sKept.begin() + std::ptrdiff_t(i));
        break;
      }
    }
  }
}

// Tests ($TTYD_NOTES_TEST, docs/PAPER_NOTES.md): the room's notes from the variable, no relay, placed
// round Mario's first place in the field ("<dx> <dy> <dz> <phrase> <word> <score>[ m]" lines, ';' between,
// m: this device's). Leaving and rating are answered here, with the relay's rules (3 of yours a room, one
// rating each, gone at -3), so the page and the drawing are tested with no network.
const char* testSpec() {
  static const char* sSpec = SDL_getenv("TTYD_NOTES_TEST");
  return sSpec != nullptr && sSpec[0] != '\0' ? sSpec : nullptr;
}
bool sTestPlaced;
int sTestNext;

// a test's add or rating (sLock held): the relay's answer, as send() takes it
void testPost(const std::string& body, const std::string& rated, int d) {
  char kind[8] = {}, map[16] = {}, id[16] = {};
  int x = 0, y = 0, z = 0, phrase = 0, word = 0;
  const char* answer = "The relay refused it.";
  if (std::sscanf(body.c_str(), "add %15s %d %d %d %d %d", map, &x, &y, &z, &phrase, &word) == 6) {
    int mine = 0;
    for (const PortNote& n : sArea.notes) {
      for (const std::string& m : sMine) {
        mine += std::strcmp(n.map, map) == 0 && m == n.id ? 1 : 0;
      }
    }
    if (mine >= 3) {
      answer = refusal("mine: three notes in this room", 429);
    } else {
      PortNote n{};
      std::snprintf(n.id, sizeof(n.id), "t%d", sTestNext++);
      std::snprintf(n.map, sizeof(n.map), "%s", map);
      n.x = x, n.y = y, n.z = z, n.phrase = phrase, n.word = word;
      sArea.notes.push_back(n);
      sMine.push_back(n.id);
      answer = "Your note is left here.";
    }
  } else if (std::sscanf(body.c_str(), "%7s %15s %15s", kind, map, id) == 3 && std::strcmp(kind, "rate") == 0) {
    answer = refusal("gone: no such note", 404);
    for (size_t i = 0; i < sArea.notes.size(); i++) {
      if (std::strcmp(sArea.notes[i].id, id) != 0) {
        continue;
      }
      if (sRated.count(rated) != 0) {
        answer = refusal("rated: once each", 409);
      } else {
        sRated[rated] = d;
        sArea.notes[i].score += d;
        answer = "Rated: thank you.";
        if (sArea.notes[i].score <= -3) {
          sArea.notes.erase(sArea.notes.begin() + std::ptrdiff_t(i));
        }
      }
      break;
    }
  }
  std::printf("[notes] test: %s: %s\n", body.c_str(), answer);
  std::fflush(stdout);
  sAnswer = answer;
  sAnswerSerial++;
}

void post(const char* map, const std::string& body, const std::string& rated, int d) {
  const std::string area = areaOf(map);
  const std::string relay = relayHttps();
  std::lock_guard<std::mutex> lock(sLock);
  readOwn();
  if (testSpec() != nullptr) {
    testPost(body, rated, d);
    return;
  }
  if (area.empty() || relay.empty() || sWaiting) {
    sAnswer = area.empty() ? "Notes can't be left here." : relay.empty() ? "There's no relay to keep notes on." : sAnswer;
    sAnswerSerial++;
    return;
  }
  sWaiting = true;
  portDetach(send, relay + "/notes?area=" + area, body, area, rated, d);
}

struct Register {
  Register() {
    portSettingsDeclare("paper_notes", 1, 0, 1);
    portSettingsAddToggle("Multiplayer", "Paper notes",
                          "Notes other players left on the ground, from set phrases, and yours for them: the Start "
                          "menu's Online tab leaves and rates them. Off, none are fetched or shown.",
                          0, "paper_notes", "notes messages hints paper online community rate");
  }
} sRegister;

} // namespace

extern "C" int portNotePhraseCount(void) { return kPhraseCount; }
extern "C" const char* portNotePhrase(int i) { return i >= 0 && i < kPhraseCount ? kPhrases[i] : ""; }
extern "C" int portNoteWordCount(void) { return kWordCount; }
extern "C" const char* portNoteWord(int i) { return i >= 0 && i < kWordCount ? kWords[i] : ""; }

extern "C" int portNotePhraseTakesWord(int phrase) {
  return phrase >= 0 && phrase < kPhraseCount && std::strstr(kPhrases[phrase], "%s") != nullptr ? 1 : 0;
}

extern "C" int portNoteWordOffered(int word) {
  if (word < 0 || word >= kWordCount) {
    return 0;
  }
  return word < kFirstPm64 || portPm64Linked() ? 1 : 0;
}

extern "C" const char* portNoteWordShown(int word) {
  static char sBuf[64];
  return word >= 0 && word < kWordCount ? wordText(word, 1, sBuf, sizeof(sBuf)) : "";
}

extern "C" void portNoteText(int phrase, int word, int game, char* out, int size) {
  if (phrase < 0 || phrase >= kPhraseCount || word < 0 || word >= kWordCount) {
    std::snprintf(out, size_t(size), "?");
    return;
  }
  char buf[64];
  const char* p = game ? portTrGame(kPhrases[phrase]) : portTr(kPhrases[phrase]);
  const char* w = wordText(word, game, buf, sizeof(buf));
  // the phrase's one %s (a translation's too: anything else in it printed as it is)
  const char* s = std::strstr(p, "%s");
  if (s == nullptr) {
    std::snprintf(out, size_t(size), "%s", p);
  } else {
    std::snprintf(out, size_t(size), "%.*s%s%s", int(s - p), p, w, s + 2);
  }
  // a phrase that starts with the word: its first letter a capital (Latin letters)
  if (s == p && out[0] >= 'a' && out[0] <= 'z') {
    out[0] = char(out[0] - 'a' + 'A');
  }
}

extern "C" int portNotesOn(void) {
  static int* sOn = portSettingsDeclare("paper_notes", 1, 0, 1);
  // (online co-op too: each game draws the notes itself and nothing of the game is touched, so the
  // players' games stay the same; a replay's recording and playback show none, as the docs say)
  if (sOn == nullptr || *sOn == 0 || portReplayActive()) {
    return 0;
  }
  if (testSpec() != nullptr) {
    return 1;
  }
  return portOnlineRelayAt(0) != nullptr && std::strncmp(portOnlineRelayAt(0), "wss://", 6) == 0 ? 1 : 0;
}

extern "C" void portNotesTestAnchor(const char* map, float x, float y, float z) {
  const char* spec = testSpec();
  const std::string area = areaOf(map);
  if (spec == nullptr || sTestPlaced || area.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(sLock);
  readOwn();
  sTestPlaced = true;
  sArea = Area{};
  sArea.name = area;
  sArea.state = PORT_NOTES_READY;
  sArea.fetchedAt = SDL_GetTicks();
  for (const char* at = spec; at != nullptr && *at != '\0';) {
    const char* end = std::strchr(at, ';');
    const std::string line(at, end != nullptr ? size_t(end - at) : std::strlen(at));
    at = end != nullptr ? end + 1 : nullptr;
    float dx = 0.f, dy = 0.f, dz = 0.f;
    char flag[4] = {};
    PortNote n{};
    if (std::sscanf(line.c_str(), "%f %f %f %d %d %d %3s", &dx, &dy, &dz, &n.phrase, &n.word, &n.score, flag) < 6 ||
        n.phrase < 0 || n.phrase >= kPhraseCount || n.word < 0 || n.word >= kWordCount) {
      continue;
    }
    std::snprintf(n.id, sizeof(n.id), "t%d", sTestNext++);
    std::snprintf(n.map, sizeof(n.map), "%s", map);
    n.x = int(std::floor(x + dx + 0.5f));
    n.y = int(std::floor(y + dy + 0.5f));
    n.z = int(std::floor(z + dz + 0.5f));
    if (flag[0] == 'm') {
      sMine.push_back(n.id);
    }
    std::printf("[notes] test: %s at %d %d %d, phrase %d word %d score %+d%s\n", n.id, n.x, n.y, n.z, n.phrase, n.word,
                n.score, flag[0] == 'm' ? " (mine)" : "");
    sArea.notes.push_back(n);
  }
  std::printf("[notes] test: %zu notes placed in %s\n", sArea.notes.size(), map);
  std::fflush(stdout);
}

extern "C" void portNotesWant(const char* map) {
  const std::string area = areaOf(map);
  if (area.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(sLock);
  readOwn();
  if (testSpec() != nullptr) {
    return; // (the test's notes: no relay asked)
  }
  if (sArea.name != area) {
    if (sArea.state == PORT_NOTES_READY) {
      keepArea(std::move(sArea));
    }
    sArea = Area{};
    sArea.name = area;
    for (size_t i = 0; i < sKept.size(); i++) {
      if (sKept[i].name == area) {
        sArea = std::move(sKept[i]); // (back in an area of the last few: its notes as they were)
        sKept.erase(sKept.begin() + std::ptrdiff_t(i));
        break;
      }
    }
  }
  const bool stale = sArea.fetchedAt == 0 ||
                     SDL_GetTicks() - sArea.fetchedAt > (sArea.state == PORT_NOTES_READY ? kKeepMs : kRetryMs);
  // (a failed fetch too is tried again only after kRetryMs: fetchedAt is its end's)
  if (!sLoading && stale) {
    startFetch(area);
  }
}

extern "C" void portNotesRefresh(void) {
  std::lock_guard<std::mutex> lock(sLock);
  if (!sArea.name.empty() && !sLoading && testSpec() == nullptr) {
    startFetch(sArea.name, true);
  }
}

extern "C" int portNotesState(void) {
  std::lock_guard<std::mutex> lock(sLock);
  return sArea.state;
}

extern "C" int portNotesCount(const char* map) {
  std::lock_guard<std::mutex> lock(sLock);
  int n = 0;
  for (const PortNote& note : sArea.notes) {
    n += std::strcmp(note.map, map) == 0 ? 1 : 0;
  }
  return n;
}

extern "C" int portNotesGet(const char* map, int i, PortNote* out) {
  std::lock_guard<std::mutex> lock(sLock);
  for (const PortNote& note : sArea.notes) {
    if (std::strcmp(note.map, map) == 0 && i-- == 0) {
      *out = note;
      out->mine = 0;
      for (const std::string& id : sMine) {
        out->mine |= id == note.id ? 1 : 0;
      }
      const auto r = sRated.find(note.id);
      out->rated = r != sRated.end() ? r->second : 0;
      return 1;
    }
  }
  return 0;
}

extern "C" void portNotesAdd(const char* map, int x, int y, int z, int phrase, int word) {
  if (phrase < 0 || phrase >= kPhraseCount || word < 0 || word >= kWordCount) {
    return;
  }
  if (!portNotePhraseTakesWord(phrase)) {
    word = 0; // (a phrase with no word: the note keeps 0)
  }
  char body[96];
  std::snprintf(body, sizeof(body), "add %s %d %d %d %d %d", map, x, y, z, phrase, word);
  post(map, body, {}, 0);
}

extern "C" void portNotesRate(const char* map, const char* id, int d) {
  char body[96];
  std::snprintf(body, sizeof(body), "rate %s %s %d", map, id, d > 0 ? 1 : -1);
  post(map, body, id, d > 0 ? 1 : -1);
}

extern "C" const char* portNotesAnswer(int* serial) {
  std::lock_guard<std::mutex> lock(sLock);
  *serial = sAnswerSerial;
  return sWaiting ? nullptr : sAnswer;
}
