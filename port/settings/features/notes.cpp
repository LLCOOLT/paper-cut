// Paper notes (port/include/port/notes.h, docs/PAPER_NOTES.md): the phrases
// and words, the setting, and the relay's side of the game's: an area's
// notes fetched, a note left, one rated, each on a thread of its own so the
// game never waits on the network. What this device left and rated is kept
// in <user>/notes.txt (its notes' ids, and its ratings), to show them as
// such; the relay keeps the count of each address's.
#include "port/http.h"
#include "port/notes.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>

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
// ever add at the end (a game that doesn't know a number shows none of it).
constexpr const char* kPhrases[] = {
    "%s ahead",       "Try %s",          "Look out for %s", "%s here!",           "Don't forget %s",
    "No %s here",     "If only I had %s...", "Use %s here", "Hidden %s?",         "Praise %s!",
    "Be wary of %s",  "Time for %s",
};
constexpr const char* kWords[] = {
    "an enemy",      "a strong enemy", "a boss",      "treasure",      "a hidden block", "a Star Piece",
    "a Shine Sprite", "a badge",       "an item",     "coins",         "a secret",       "a shortcut",
    "a dead end",    "a pipe",         "a door",      "a switch",      "a Save Block",   "a Heart Block",
    "the hammer",    "a Spin Jump",    "Paper mode",  "Plane mode",    "Tube mode",      "Boat mode",
    "Goombella",     "Koops",          "Flurrie",     "Yoshi",         "Vivian",         "Bobbery",
    "Ms. Mowz",      "Mario",          "a trap",      "the way up",    "the way down",   "a fight",
    "patience",      "courage",        "a rest",      "a good view",
};
// the partners' words (24-30): their names as the game's own text says them, in its language
constexpr int kFirstPartner = 24;
constexpr const char* kPartnerTags[] = {"name_party0", "name_party1", "name_party4", "name_party3",
                                        "name_party5", "name_party2", "name_party6"};
constexpr int kPhraseCount = int(sizeof(kPhrases) / sizeof(kPhrases[0]));
constexpr int kWordCount = int(sizeof(kWords) / sizeof(kWords[0]));

constexpr Uint64 kKeepMs = 10 * 60 * 1000; // an area's notes kept this long before they're fetched again

struct Area {
  std::string name;     // the map's first three letters
  Uint64 fetchedAt = 0; // SDL_GetTicks
  int state = PORT_NOTES_IDLE;
  std::vector<PortNote> notes;
};

std::mutex sLock;
Area sArea;
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
  if (FILE* f = std::fopen(ownFile().c_str(), "r")) {
    char kind[16], id[32];
    int d = 0;
    char line[96];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (std::sscanf(line, "%15s %31s %d", kind, id, &d) >= 2) {
        if (std::strcmp(kind, "mine") == 0) {
          sMine.push_back(id);
        } else if (std::strcmp(kind, "rated") == 0) {
          sRated[id] = d;
        }
      }
    }
    std::fclose(f);
  }
}

void writeOwn() {
  if (FILE* f = std::fopen(ownFile().c_str(), "w")) {
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
  }
}

void startFetch(const std::string& area) { // (sLock held)
  const std::string relay = relayHttps();
  if (relay.empty()) {
    sArea.state = PORT_NOTES_FAILED;
    sArea.fetchedAt = SDL_GetTicks();
    return;
  }
  sLoading = true;
  sArea.state = PORT_NOTES_LOADING;
  std::thread(fetchArea, area, relay + "/notes?area=" + area).detach();
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
      id = text.substr(3, text.find_first_of("\r\n") - 3);
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
    startFetch(area);
  }
}

void post(const char* map, const std::string& body, const std::string& rated, int d) {
  const std::string area = areaOf(map);
  const std::string relay = relayHttps();
  std::lock_guard<std::mutex> lock(sLock);
  readOwn();
  if (area.empty() || relay.empty() || sWaiting) {
    sAnswer = area.empty() ? "Notes can't be left here." : relay.empty() ? "There's no relay to keep notes on." : sAnswer;
    sAnswerSerial++;
    return;
  }
  sWaiting = true;
  std::thread(send, relay + "/notes?area=" + area, body, area, rated, d).detach();
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

extern "C" void portNoteText(int phrase, int word, int game, char* out, int size) {
  if (phrase < 0 || phrase >= kPhraseCount || word < 0 || word >= kWordCount) {
    std::snprintf(out, size_t(size), "?");
    return;
  }
  const char* p = game ? portTrGame(kPhrases[phrase]) : portTr(kPhrases[phrase]);
  const char* w = game ? portTrGame(kWords[word]) : portTr(kWords[word]);
  if (game && word >= kFirstPartner && word < kFirstPartner + 7) {
    w = msgSearch(kPartnerTags[word - kFirstPartner]);
  }
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
  return sOn != nullptr && *sOn && !portOnlineActive() && !portReplayActive() && portOnlineRelayAt(0) != nullptr &&
                 std::strncmp(portOnlineRelayAt(0), "wss://", 6) == 0
             ? 1
             : 0;
}

extern "C" void portNotesWant(const char* map) {
  const std::string area = areaOf(map);
  if (area.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(sLock);
  readOwn();
  if (sArea.name != area) {
    sArea = Area{};
    sArea.name = area;
  }
  const bool stale = sArea.fetchedAt == 0 || SDL_GetTicks() - sArea.fetchedAt > kKeepMs;
  // (a failed fetch too is tried again only after kKeepMs: fetchedAt is its end's)
  if (!sLoading && stale) {
    startFetch(area);
  }
}

extern "C" void portNotesRefresh(void) {
  std::lock_guard<std::mutex> lock(sLock);
  if (!sArea.name.empty() && !sLoading) {
    startFetch(sArea.name);
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
