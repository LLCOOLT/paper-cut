// Leaderboards backed by replays (port/include/port/boards.h,
// docs/LEADERBOARDS.md): the Modes tab's Leaderboards section, the relay's
// side, and the checks.
//
// - Record a run: the game starts again recording from where the player is
//   (replay.cpp), armed for the board; the recording is saved as the run ends.
// - Check and upload: the game starts again playing the run's replay; if the
//   run ends on the same frame as it did, its replay (zlib, without the ghost
//   track) goes to the board. A run that doesn't play out the same is not
//   sent.
// - Watch: an entry's replay downloaded to <user>/replays/boards/ and played;
//   as it ends this game tells the relay whether the run took the same frames
//   here (each game once), which the board shows as its checks.
// What this device checked is kept in <user>/replays/checked.txt.
#include "port/boards.h"
#include "port/http.h"
#include "port/online.h"
#include "port/online_menu.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
int portReplayCanRestart(void);
int portReplayCanRecordHere(void);
int portReplayRestartForBoard(int board, const char* name);
int portReplayRestartPlay(const char* path, const char* env, const char* value);
int portReplayPlayingBoard(int* board, unsigned* frames, int* done);
const char* portReplayPlayingPath(void);
}
namespace port_replay {
bool boardRun(const std::string& path, int& board, unsigned& frames, bool& done, std::string& name, std::string& date,
              std::string& game, std::string& version);
bool forUpload(const std::string& path, std::string& bytes);
} // namespace port_replay

namespace {

constexpr int kEntries = 10;
constexpr size_t kMaxReplay = 1 << 20; // the relay's limit (compressed)

enum { IDLE, LOADING, READY, FAILED, OLD_RELAY };

struct Entry {
  std::string id, name, date;
  unsigned frames = 0;
  int checks = 0, fails = 0;
};

struct Board {
  int state = IDLE;
  std::vector<Entry> entries;
};

std::mutex sLock;
Board sBoards[PORT_BOARD_COUNT];
std::string sNews; // the last upload's, download's or check's answer (UTF-8, shown under the board)
bool sBusy;        // an upload or a download under way
int sPicked;       // the board shown
std::string sPlayPath, sPlayEntry; // a downloaded entry to play (started on the main thread: overlay)

// --- where and what ---

std::string gameId() {
  char id[8];
  std::snprintf(id, sizeof(id), "%.6s", portVersionSelf()->id);
  return id;
}

bool fiftyHz() {
  const int* v = portSettingsFind("pal_50hz");
  return PORT_GAME_ID[3] == 'P' && v != nullptr && *v != 0;
}

// the relay's board: its kind, and "-50" for the EU release played at 50 Hz (its frames are longer)
std::string boardKey(int board) { return std::string(portBoardKey(board)) + (fiftyHz() ? "-50" : ""); }

std::string relayHttps() {
  const char* relay = portOnlineRelayAt(0);
  if (relay == nullptr || std::strncmp(relay, "wss://", 6) != 0) {
    return {};
  }
  return std::string("https://") + (relay + 6);
}

std::string query(int board) { return "board=" + boardKey(board) + "&game=" + gameId() + "&v=" + PORT_VERSION; }

std::string percent(const std::string& text) {
  std::string out;
  for (unsigned char c : text) {
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
        c == '.') {
      out += char(c);
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      out += hex;
    }
  }
  return out;
}

std::string unpercent(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); i++) {
    if (text[i] == '%' && i + 2 < text.size()) {
      out += char(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += text[i];
    }
  }
  return out;
}

std::string clock(unsigned frames) {
  const unsigned fps = fiftyHz() ? 50 : 60;
  const unsigned cs = unsigned((unsigned long long)frames * 100 / fps);
  char text[32];
  if (cs >= 360000) {
    std::snprintf(text, sizeof(text), "%u:%02u:%02u.%02u", cs / 360000, cs / 6000 % 60, cs / 100 % 60, cs % 100);
  } else {
    std::snprintf(text, sizeof(text), "%u:%02u.%02u", cs / 6000, cs / 100 % 60, cs % 100);
  }
  return text;
}

std::string replayDir() { return std::string(portSettingsUserPath()) + "/replays"; }

std::string readFile(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  std::string out;
  if (data != nullptr) {
    out.assign(static_cast<const char*>(data), size);
    SDL_free(data);
  }
  return out;
}

// the relay's refusal ("<why>: <words>") in English for the page
const char* refusal(const std::string& text, int status) {
  const std::string why = text.substr(0, text.find(':'));
  if (why == "slower") {
    return text.find("your run") != std::string::npos ? "Your run on this board is as fast or faster."
                                                       : "The board's runs are all faster.";
  }
  if (why == "today") {
    return "No more of those today: try tomorrow.";
  }
  if (why == "big") {
    return "That replay is larger than a board keeps.";
  }
  if (why == "gone") {
    return "That run isn't on the board any more.";
  }
  if (status == 404) {
    return "Leaderboards aren't on this relay yet.";
  }
  return "The relay refused it.";
}

void news(const std::string& text) {
  std::lock_guard<std::mutex> lock(sLock);
  sNews = text;
  sBusy = false;
  std::printf("[boards] %s\n", text.c_str());
  std::fflush(stdout);
}

// --- the board ---

void fetchBoard(int board, std::string url) {
  std::vector<uint8_t> body;
  std::string error;
  std::vector<Entry> entries;
  int state = READY;
  if (!port_http::fetch(url, body, 64 * 1024, error, "text/plain")) {
    state = error.find("404") != std::string::npos ? OLD_RELAY : FAILED;
    std::printf("[boards] %s: %s\n", url.c_str(), error.c_str());
    std::fflush(stdout);
  } else {
    const std::string text(body.begin(), body.end());
    if (text.compare(0, 6, "board ") != 0) {
      state = OLD_RELAY;
    }
    size_t at = text.find('\n');
    while (state == READY && at != std::string::npos && at < text.size()) {
      const size_t end = text.find('\n', at + 1);
      const std::string line = text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
      at = end;
      char id[32], date[16], name[160];
      Entry e;
      if (std::sscanf(line.c_str(), "%31s %u %d %d %15s %159s", id, &e.frames, &e.checks, &e.fails, date, name) == 6 &&
          entries.size() < size_t(kEntries)) {
        e.id = id;
        e.date = date;
        e.name = unpercent(name).substr(0, 24);
        entries.push_back(e);
      }
    }
  }
  std::lock_guard<std::mutex> lock(sLock);
  sBoards[board].state = state;
  sBoards[board].entries = std::move(entries);
}

void refreshBoard(int board) {
  const std::string relay = relayHttps();
  std::lock_guard<std::mutex> lock(sLock);
  if (sBoards[board].state == LOADING) {
    return;
  }
  if (relay.empty()) {
    sBoards[board].state = FAILED;
    return;
  }
  sBoards[board].state = LOADING;
  std::thread(fetchBoard, board, relay + "/board?" + query(board)).detach();
}

// --- zlib ---

bool deflateAll(const std::string& in, std::string& out) {
  uLongf size = compressBound(uLong(in.size()));
  out.resize(size);
  if (compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(in.data()), uLong(in.size()),
                9) != Z_OK) {
    return false;
  }
  out.resize(size);
  return true;
}

bool inflateAll(const uint8_t* in, size_t size, std::string& out, size_t limit) {
  z_stream z{};
  if (inflateInit(&z) != Z_OK) {
    return false;
  }
  z.next_in = const_cast<Bytef*>(in);
  z.avail_in = uInt(size);
  char chunk[65536];
  int r;
  do {
    z.next_out = reinterpret_cast<Bytef*>(chunk);
    z.avail_out = sizeof(chunk);
    r = inflate(&z, Z_NO_FLUSH);
    if (r != Z_OK && r != Z_STREAM_END) {
      break;
    }
    out.append(chunk, sizeof(chunk) - z.avail_out);
  } while (r != Z_STREAM_END && out.size() < limit);
  inflateEnd(&z);
  return r == Z_STREAM_END;
}

// --- uploads, downloads, checks (threads of their own) ---

void upload(int board, unsigned frames, std::string path, std::string name) {
  std::string bytes, packed;
  if (!port_replay::forUpload(path, bytes) || !deflateAll(bytes, packed)) {
    news(portTr("The run's replay couldn't be read."));
    return;
  }
  if (packed.size() > kMaxReplay) {
    news(portTr("That replay is larger than a board keeps."));
    return;
  }
  const std::string url = relayHttps() + "/board?" + query(board) + "&frames=" + std::to_string(frames) +
                          "&name=" + percent(name);
  std::vector<uint8_t> out;
  int status = 0;
  std::string error;
  if (!port_http::post(url, packed, "application/octet-stream", out, status, 4096, error)) {
    news(portTr("The relay couldn't be reached."));
    return;
  }
  const std::string text(out.begin(), out.end());
  int rank = 0;
  char id[32];
  if (status == 200 && std::sscanf(text.c_str(), "ok %31s %d", id, &rank) == 2) {
    char line[200];
    std::snprintf(line, sizeof(line), portTr("Uploaded: number %d on the board."), rank);
    news(line);
    refreshBoard(board);
  } else {
    news(portTr(refusal(text, status)));
  }
}

void download(int board, std::string id) {
  const std::string url = relayHttps() + "/replay?" + query(board) + "&id=" + id;
  std::vector<uint8_t> packed;
  std::string error, bytes;
  if (!port_http::fetch(url, packed, kMaxReplay, error, "application/octet-stream")) {
    news(error.find("404") != std::string::npos ? portTr("That run isn't on the board any more.")
                                                 : portTr("The replay couldn't be downloaded."));
    return;
  }
  if (!inflateAll(packed.data(), packed.size(), bytes, 64u << 20)) {
    news(portTr("The replay couldn't be downloaded."));
    return;
  }
  const std::string dir = replayDir() + "/boards";
  SDL_CreateDirectory(dir.c_str());
  const std::string path = dir + "/" + boardKey(board) + "-" + id + ".pcreplay";
  if (!SDL_SaveFile(path.c_str(), bytes.data(), bytes.size())) {
    news(portTr("The replay couldn't be saved."));
    return;
  }
  std::lock_guard<std::mutex> lock(sLock);
  sBusy = false;
  sNews.clear();
  sPlayPath = path;
  sPlayEntry = std::to_string(board) + ":" + id;
}

void check(int board, std::string id, unsigned frames) {
  const std::string url = relayHttps() + "/check?" + query(board) + "&id=" + id;
  std::vector<uint8_t> out;
  int status = 0;
  std::string error;
  if (port_http::post(url, std::to_string(frames > 0 ? frames : 1), "text/plain", out, status, 1024, error) &&
      status == 200) {
    std::printf("[boards] check of %s told the relay: %.*s", id.c_str(), int(out.size()), out.data());
    std::fflush(stdout);
  }
}

// --- this device's runs, and its checks ---

struct Run {
  std::string path, name, date;
  int board = 0;
  unsigned frames = 0;
  bool done = false;
  std::string checked; // "ok", "fail" or ""
};
std::vector<Run> sRuns;
bool sRunsListed;

std::string checkedFile() { return replayDir() + "/checked.txt"; }

// path -> "ok" / "fail" (the last check's)
std::string checkedOf(const std::string& path) {
  const std::string text = readFile(checkedFile());
  std::string result;
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    const size_t tab = line.find('\t');
    if (tab != std::string::npos && line.substr(tab + 1) == path) {
      result = line.substr(0, tab);
    }
  }
  return result;
}

void markChecked(const std::string& path, bool ok) {
  if (FILE* f = std::fopen(checkedFile().c_str(), "a")) {
    std::fprintf(f, "%s\t%s\n", ok ? "ok" : "fail", path.c_str());
    std::fclose(f);
  }
}

void listRuns() {
  sRunsListed = true;
  sRuns.clear();
  const std::string dir = replayDir();
  int count = 0;
  if (char** found = SDL_GlobDirectory(dir.c_str(), "*.pcreplay", 0, &count)) {
    for (int i = 0; i < count; i++) {
      Run r;
      bool done = false;
      std::string game, version;
      r.path = dir + "/" + found[i];
      if (port_replay::boardRun(r.path, r.board, r.frames, done, r.name, r.date, game, version) && done &&
          game == gameId() && version == PORT_VERSION) {
        r.done = done;
        r.checked = checkedOf(r.path);
        sRuns.push_back(r);
      }
    }
    SDL_free(found);
  }
  std::sort(sRuns.begin(), sRuns.end(), [](const Run& a, const Run& b) { return a.date > b.date; });
}

// --- the playback's check (each presented frame) ---

void playbackFrame() {
  static bool sDone;
  int board = -1, done = 0;
  unsigned frames = 0;
  if (sDone || !portReplayPlayingBoard(&board, &frames, &done)) {
    return;
  }
  const int state = portBoardRunState();
  if (state != PORT_BOARD_RUN_DONE && state != PORT_BOARD_RUN_FAILED && !portReplayFinished()) {
    return;
  }
  sDone = true;
  const unsigned took = portBoardRunFrames();
  const bool ok = done && state == PORT_BOARD_RUN_DONE && took == frames;
  const std::string path = portReplayPlayingPath();
  markChecked(path, ok);
  std::printf("[boards] check: %s (%u frames, the replay says %u)\n", ok ? "same" : "differs", took, frames);
  std::fflush(stdout);
  char line[240];
  if (ok) {
    std::snprintf(line, sizeof(line), portTr("Checked: the run took the same %s here."), clock(took).c_str());
  } else {
    std::snprintf(line, sizeof(line), "%s", portTr("Checked: the run didn't play out the same here."));
  }
  {
    std::lock_guard<std::mutex> lock(sLock);
    sNews = line;
  }
  if (const char* entry = SDL_getenv("TTYD_BOARD_ENTRY"); entry != nullptr && std::strchr(entry, ':') != nullptr) {
    const int b = std::atoi(entry);
    const std::string id = std::strchr(entry, ':') + 1;
    if (b >= 0 && b < PORT_BOARD_COUNT && id.size() < 32) {
      std::thread(check, b, id, state == PORT_BOARD_RUN_DONE ? took : 0).detach();
    }
  } else if (SDL_getenv("TTYD_BOARD_UPLOAD") != nullptr && ok) {
    {
      std::lock_guard<std::mutex> lock(sLock);
      sNews = std::string(line) + " " + portTr("Uploading...");
      sBusy = true;
    }
    std::thread(upload, board, took, path, std::string(portOnlineMenuName())).detach();
  }
}

void overlay(int pageOpen) {
  (void)pageOpen;
  playbackFrame();
  {
    std::string path, entry;
    {
      std::lock_guard<std::mutex> lock(sLock);
      path.swap(sPlayPath);
      entry.swap(sPlayEntry);
    }
    if (!path.empty() && !portReplayRestartPlay(path.c_str(), "TTYD_BOARD_ENTRY", entry.c_str())) {
      news(portTr("The game can't start again here to play it."));
    }
  }
  int board = -1, done = 0;
  unsigned frames = 0;
  std::string text;
  if (portReplayPlayingBoard(&board, &frames, &done)) {
    std::lock_guard<std::mutex> lock(sLock);
    text = sNews;
    if (text.empty()) {
      char line[200];
      std::snprintf(line, sizeof(line), portTr("%s: %s so far (the run: %s)"), portTr(portBoardTitle(board)),
                    clock(portBoardRunFrames()).c_str(), clock(frames).c_str());
      text = line;
    }
  } else if (portReplayRecording() && portBoardRunBoard() >= 0) {
    const int state = portBoardRunState();
    char line[200];
    std::snprintf(line, sizeof(line), "%s: %s", portTr(portBoardTitle(portBoardRunBoard())),
                  state == PORT_BOARD_RUN_ARMED
                      ? portTr(portBoardRunBoard() == PORT_BOARD_PIT ? "the clock starts on floor 1"
                                                                     : "starting in the field")
                      : clock(portBoardRunFrames()).c_str());
    text = line;
  } else {
    return;
  }
  // under the replay's label (replay.cpp's, at the top left)
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  const float size = ImGui::GetFontSize();
  const ImVec2 at(size * 0.6f, size * 3.4f);
  const ImVec2 box = ImGui::CalcTextSize(text.c_str());
  draw->AddRectFilled(ImVec2(at.x - size * 0.3f, at.y - size * 0.2f),
                      ImVec2(at.x + box.x + size * 0.3f, at.y + box.y + size * 0.2f), IM_COL32(0, 0, 0, 150),
                      size * 0.3f);
  draw->AddText(at, IM_COL32(255, 230, 150, 255), text.c_str());
}

// --- the section ---

void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void section(int inGame) {
  (void)inGame;
  static bool sOpened[PORT_BOARD_COUNT];
  if (relayHttps().empty()) {
    hint(portTr("Leaderboards need the online relay, which this build has none of."));
    return;
  }
  // the board
  const char* names[PORT_BOARD_COUNT];
  for (int i = 0; i < PORT_BOARD_COUNT; i++) {
    names[i] = portTr(portBoardTitle(i));
  }
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
  ImGui::Combo(portTrLabel("Board##boards"), &sPicked, names, PORT_BOARD_COUNT);
  sPicked = std::clamp(sPicked, 0, PORT_BOARD_COUNT - 1);
  if (!sOpened[sPicked]) {
    sOpened[sPicked] = true;
    refreshBoard(sPicked); // (as it's first shown, and on Refresh: never polled)
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Refresh##boards"))) {
    refreshBoard(sPicked);
  }
  Board board;
  std::string newsText;
  bool busy;
  {
    std::lock_guard<std::mutex> lock(sLock);
    board = sBoards[sPicked];
    newsText = sNews;
    busy = sBusy;
  }
  if (board.state == LOADING) {
    hint(portTr("Fetching the board..."));
  } else if (board.state == OLD_RELAY) {
    hint(portTr("Leaderboards aren't on this relay yet."));
  } else if (board.state == FAILED) {
    hint(portTr("The board couldn't be fetched."));
  } else if (board.entries.empty()) {
    hint(portTr("No runs on this board yet: be the first."));
  } else if (ImGui::BeginTable("##board", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    for (int i = 0; i < int(board.entries.size()); i++) {
      const Entry& e = board.entries[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d.", i + 1);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(e.name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(clock(e.frames).c_str());
      ImGui::TableNextColumn();
      if (e.fails > 0) {
        ImGui::Text(portTr("checked by %d, differed for %d"), e.checks, e.fails);
      } else {
        ImGui::Text(portTr("checked by %d"), e.checks);
      }
      ImGui::TableNextColumn();
      ImGui::PushID(i);
      ImGui::BeginDisabled(busy || !portReplayCanRestart());
      if (ImGui::SmallButton(portTrLabel("Watch##board_watch"))) {
        {
          std::lock_guard<std::mutex> lock(sLock);
          sBusy = true;
          sNews = portTr("Downloading the replay...");
        }
        std::thread(download, sPicked, e.id).detach();
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndTable();
    hint(portTr("Watch plays the run's replay (the game starts again); as it ends, this game tells the board whether "
                "it played out the same here."));
  }
  if (!newsText.empty()) {
    ImGui::TextWrapped("%s", newsText.c_str());
  }

  // a run of one's own
  ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
  ImGui::TextUnformatted(portTr("Record a run"));
  ImGui::BeginDisabled(!portReplayCanRecordHere());
  if (ImGui::Button(portTrLabel("Restart and record a run##board_record"))) {
    portReplayRestartForBoard(sPicked, portBoardTitle(sPicked));
  }
  ImGui::EndDisabled();
  hint(portTr(sPicked == PORT_BOARD_PIT
                  ? "The game starts again from where you are, recording; the clock starts as you enter the Pit's "
                    "floor 1 and stops on floor 100. Then check and upload it below."
                  : "The game starts again from where you are, recording, and the boss rush starts in the field with "
                    "this board's loadout; the clock stops as the last boss falls. Then check and upload it below."));
  if (!portReplayCanRecordHere()) {
    hint(portTr(portOnlineActive() ? "Not during online co-op."
                                   : "Recording needs a file in play (quick resume's save) and a game that can start "
                                     "itself again (not on phones and tablets)."));
  }

  ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
  ImGui::TextUnformatted(portTr("Your runs"));
  if (!sRunsListed) {
    listRuns();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(portTrLabel("Refresh##board_runs"))) {
    listRuns();
  }
  int shown = 0;
  for (int i = 0; i < int(sRuns.size()); i++) {
    const Run& r = sRuns[i];
    if (r.board != sPicked) {
      continue;
    }
    shown++;
    ImGui::PushID(i);
    ImGui::Text("%s  %s  (%s)", clock(r.frames).c_str(), r.date.c_str(),
                r.checked == "ok" ? portTr("checked") : r.checked == "fail" ? portTr("didn't play out the same")
                                                                             : portTr("not checked yet"));
    ImGui::SameLine();
    ImGui::BeginDisabled(busy || !portReplayCanRestart());
    if (ImGui::SmallButton(portTrLabel("Check and upload##board_upload"))) {
      portReplayRestartPlay(r.path.c_str(), "TTYD_BOARD_UPLOAD", "1");
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  if (shown == 0) {
    hint(portTr("None for this board yet (finished runs of this version are kept in the replays folder)."));
  } else {
    hint(portTr("Check and upload plays the run back (the game starts again): only a run that plays out the same "
                "goes on the board, with your name from the Online tab."));
  }
}

struct Register {
  Register() {
    portSettingsAddSection("Modes", "Leaderboards", nullptr, 0, section,
                           "leaderboard leaderboards board times ranking speedrun replay boss rush pit 100 trials");
    portSettingsAddOverlay(overlay, nullptr);
  }
} sRegister;

} // namespace
