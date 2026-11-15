// Softer landings after a crash (port/include/port/soft_crash.h): the crash-safe snapshots' setting and what
// the next start offers from them, the record of settings changes and the offer to undo one that crashed the
// game, and the window of its own after a run that stopped on screen when no other window shows.
#include "port/soft_crash.h"
#include "port/flight.h"
#include "port/mem_guard.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/tr.h"

#include "../file_util.h"
#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr long long kUndoWindow = 5 * 60;      // a change this long before the end: "closed after you changed"
constexpr long long kSnapWindow = 2 * 60 * 60; // snapshots this long before the end are offered
constexpr size_t kChangesKept = 24;
// the keys a change of which is never offered: the volume and the mute key are changed in passing, often
// (their own keys and the audio mixer's), and can't close the game
constexpr const char* kNotOffered[] = {"volume", "mute", "music_volume", "sfx_volume", "ambience_volume"};

bool sQuiet;

// Tests ($TTYD_SETTING_CHANGE_TEST=<key>=<value>@<frames>): that setting changed and saved after that many
// frames, as the settings page does, so the change is noted (portSettingsChangesNote)
void changeTest(int) {
  static const char* sTest = SDL_getenv("TTYD_SETTING_CHANGE_TEST");
  static int sFrames;
  if (sTest == nullptr) {
    return;
  }
  const char* eq = std::strchr(sTest, '=');
  const char* at = std::strchr(sTest, '@');
  if (eq == nullptr || ++sFrames < (at != nullptr ? std::atoi(at + 1) : 60)) {
    return;
  }
  const std::string key(sTest, size_t(eq - sTest));
  const std::string value = at != nullptr && at > eq ? std::string(eq + 1, size_t(at - eq - 1)) : std::string(eq + 1);
  sTest = nullptr;
  portSettingsSetKey(key.c_str(), value.c_str());
  portSettingsSave();
  std::printf("[undo] test: %s set to %s and saved\n", key.c_str(), value.c_str());
  std::fflush(stdout);
}

const bool sAdded = [] {
  if (SDL_getenv("TTYD_SETTING_CHANGE_TEST") != nullptr) {
    portSettingsAddOverlay(changeTest, nullptr);
  }
  portSettingsDeclare("crash_safe_resume", 1, 0, 1);
  portSettingsAddToggle("General", "Crash-safe snapshots",
                        "Keeps a copy of where you are every few minutes and at room changes; after a crash, the "
                        "next start offers to continue from it. Not online, in leaderboard runs, the daily seed or "
                        "bingo, or on a Hardcore file.",
                        0, "crash_safe_resume", "crash safe snapshot resume recover continue backup minutes");
  return true;
}();

std::string userFile(const char* name) {
  const char* user = portSettingsUserPath();
  return std::string(user != nullptr ? user : ".") + "/" + name;
}

// key=value lines
std::map<std::string, std::string> parse(const char* text) {
  std::map<std::string, std::string> out;
  for (const char* line = text; line != nullptr && *line != '\0';) {
    const size_t len = std::strcspn(line, "\r\n");
    const char* eq = static_cast<const char*>(std::memchr(line, '=', len));
    if (eq != nullptr && eq > line) {
      out[std::string(line, size_t(eq - line))] = std::string(eq + 1, size_t(line + len - eq - 1));
    }
    line += len;
    line += std::strspn(line, "\r\n");
  }
  return out;
}

// a key's value in settings.txt's text, or its default when the file leaves it out (as every release reads it)
std::string valueIn(const std::map<std::string, std::string>& values, const std::string& key) {
  const auto it = values.find(key);
  if (it != values.end()) {
    return it->second;
  }
  int def = 0;
  return portSettingsDefault(key.c_str(), &def) ? std::to_string(def) : std::string();
}

// a setting's value now, as settings.txt writes it ("" unknown)
std::string valueNow(const std::string& key) {
  if (key == "framerate") {
    return portSettings()->framerate;
  }
  if (const int* field = portSettingsFieldValue(key.c_str())) {
    return std::to_string(*field);
  }
  int v = 0;
  return portSettingsExtraRaw(key.c_str(), &v) ? std::to_string(v) : std::string();
}

bool offered(const std::string& key) {
  for (const char* k : kNotOffered) {
    if (key == k) {
      return false;
    }
  }
  return portSettingsLabelOf(key.c_str()) != nullptr; // (the page's own controls only: never an internal mark)
}

long long fileTime(const char* path) {
  SDL_PathInfo info;
  return path != nullptr && SDL_GetPathInfo(path, &info) ? static_cast<long long>(info.modify_time / SDL_NS_PER_SECOND)
                                                          : -1;
}

} // namespace

extern "C" void portSettingsChangesQuiet(int quiet) {
  sQuiet = quiet != 0;
}

// settings_aurora.c's portSettingsSave, after each write: what it changed, noted with the time
extern "C" void portSettingsChangesNote(const char* before, const char* after) {
  if (sQuiet || before == nullptr || after == nullptr || std::strcmp(before, after) == 0) {
    return;
  }
  const auto a = parse(before), b = parse(after);
  std::set<std::string> keys;
  for (const auto& kv : a) {
    keys.insert(kv.first);
  }
  for (const auto& kv : b) {
    keys.insert(kv.first);
  }
  const long long now = static_cast<long long>(std::time(nullptr));
  std::string add;
  for (const std::string& key : keys) {
    const std::string was = valueIn(a, key), is = valueIn(b, key);
    // (a default of -1 is "not settled yet": its first value is the game's choice, not the player's, e.g.
    // Vivian's colour settled on first use: cosmetics.cpp)
    int def = 0;
    const bool settling = was == "-1" && portSettingsDefault(key.c_str(), &def) && def == -1;
    if (was == is || was.empty() || is.empty() || settling || !offered(key) || key.find_first_of(" \t") != std::string::npos ||
        was.find_first_of(" \t") != std::string::npos || is.find_first_of(" \t") != std::string::npos) {
      continue;
    }
    add += std::to_string(now) + " " + key + " " + was + " " + is + "\n";
    std::printf("[undo] noted: %s %s -> %s\n", key.c_str(), was.c_str(), is.c_str());
  }
  if (add.empty()) {
    return;
  }
  std::fflush(stdout);
  // the last kChangesKept lines, written whole (a crash just after leaves the old file, never half of one)
  const std::string path = userFile("settings_changes.txt");
  std::string text = portfile::readFile(path) + add;
  std::vector<size_t> starts{0};
  for (size_t i = 0; i + 1 < text.size(); i++) {
    if (text[i] == '\n') {
      starts.push_back(i + 1);
    }
  }
  if (starts.size() > kChangesKept) {
    text.erase(0, starts[starts.size() - kChangesKept]);
  }
  portfile::writeAll(path, text.data(), text.size(), portfile::kNoSync);
}

// --- when the last run ended, and how ---

bool portLastRunStopped() {
  return !portCrashReportFound() && (portFlightStoppedOnScreen() || portMemGuardClosedOnScreen());
}

long long portLastRunEndedAt() {
  if (portCrashReportFound()) {
    return fileTime(portCrashReportLast()); // (written as it crashed; kept as last_crash.txt by a rename)
  }
  if (portLastRunStopped()) {
    const long long t = portFlightEndedAt();
    return t > 0 ? t : -1;
  }
  return -1;
}

// --- the crash-safe snapshots (resume_aurora.c writes them) ---

std::vector<PortRecoverySnap> portCrashSafeSnaps(const char* gameId, long long endedAt) {
  std::vector<PortRecoverySnap> out;
  if (gameId == nullptr || gameId[0] == '\0') {
    return out;
  }
  for (int i = 0; i < PORT_CRASH_SAFE_KEPT; i++) {
    char gci[1024], txt[1024];
    portCrashSafePath(gci, sizeof(gci), gameId, i, 0);
    portCrashSafePath(txt, sizeof(txt), gameId, i, 1);
    SDL_PathInfo info;
    const std::string side = portfile::readFile(txt);
    // (a .gci with no .txt: its write was cut short, never offered; the .txt says v2 as quick resume's does)
    if (side.empty() || !SDL_GetPathInfo(gci, &info) || info.type != SDL_PATHTYPE_FILE ||
        side.find("\nv2\n") == std::string::npos || side[0] < '0' || side[0] > '3') {
      continue;
    }
    PortRecoverySnap s;
    s.path = gci;
    s.slot = side[0] - '0';
    const auto values = parse(side.c_str());
    if (const auto t = values.find("time"); t != values.end()) {
      s.made = std::atoll(t->second.c_str());
    }
    if (const auto m = values.find("map"); m != values.end()) {
      s.map = m->second;
    }
    if (const auto a = values.find("area"); a != values.end()) {
      s.area = a->second;
    }
    if (s.made <= 0) {
      continue;
    }
    if (endedAt > 0) {
      // made in the run that ended (or just before it): never one from long ago, never one from after it
      if (s.made > endedAt + 5 || endedAt - s.made > kSnapWindow) {
        continue;
      }
      s.before = std::max(0LL, endedAt - s.made);
    }
    out.push_back(s);
  }
  std::sort(out.begin(), out.end(), [](const PortRecoverySnap& x, const PortRecoverySnap& y) { return x.made > y.made; });
  return out;
}

std::string portCrashSafePaused(const char* gameId, long long endedAt) {
  char path[1024];
  if (gameId == nullptr || gameId[0] == '\0') {
    return {};
  }
  portCrashSafeStatusPath(path, sizeof(path), gameId);
  const std::string text = portfile::readFile(path);
  if (text.empty()) {
    return {};
  }
  const auto values = parse(text.c_str());
  const auto why = values.find("paused"), since = values.find("since"), until = values.find("until");
  if (why == values.end() || since == values.end() || until == values.end()) {
    return {};
  }
  // still paused when the run ended: started before the end and not over
  const long long from = std::atoll(since->second.c_str()), to = std::atoll(until->second.c_str());
  if (to != 0 || (endedAt > 0 && (from > endedAt + 5 || endedAt - from > kSnapWindow * 12))) {
    return {};
  }
  return why->second;
}

// --- the undo question ---

namespace {

struct Change {
  std::string key, was, is;
  long long at = 0;
};

struct Undo {
  bool looked = false, asking = false;
  std::vector<Change> changes; // newest first
  std::string said;            // after Undo: what it did
  const char* test = nullptr;  // $TTYD_UNDO_TEST
  int testAt = 0, frames = 0;
};
Undo sUndo;

void undoLook() {
  Undo& u = sUndo;
  u.looked = true;
  if (const char* test = SDL_getenv("TTYD_UNDO_TEST")) {
    u.test = test;
    const char* colon = std::strchr(test, ':');
    u.testAt = colon != nullptr ? std::atoi(colon + 1) : 30;
  }
  if (!portCrashReportFound() && !portLastRunStopped()) {
    return;
  }
  const long long ended = portLastRunEndedAt();
  if (ended <= 0) {
    return;
  }
  const std::string text = portfile::readFile(userFile("settings_changes.txt"));
  std::map<std::string, Change> byKey; // the first old value and the last new one within the window
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    char key[96] = "", was[48] = "", is[48] = "";
    long long t = 0;
    if (std::sscanf(text.substr(at, end - at).c_str(), "%lld %95s %47s %47s", &t, key, was, is) == 4 &&
        t <= ended + 5 && ended - t <= kUndoWindow) {
      auto found = byKey.find(key);
      if (found == byKey.end()) {
        byKey[key] = Change{key, was, is, t};
      } else {
        found->second.is = is;
        found->second.at = t;
      }
    }
    at = end + 1;
  }
  for (auto& kv : byKey) {
    Change& c = kv.second;
    const std::string now = valueNow(c.key);
    // (changed again since, by hand: theirs; or back where it was: nothing to undo)
    if (now != c.is || now == c.was || !offered(c.key)) {
      continue;
    }
    u.changes.push_back(c);
  }
  std::sort(u.changes.begin(), u.changes.end(), [](const Change& x, const Change& y) { return x.at > y.at; });
  if (!u.changes.empty()) {
    u.asking = true;
    const Change& c = u.changes.front();
    std::printf("[undo] offered: %s %s -> %s, %lld s before it closed, %d other%s\n", c.key.c_str(), c.was.c_str(),
                c.is.c_str(), ended - c.at, int(u.changes.size()) - 1, u.changes.size() == 2 ? "" : "s");
    std::fflush(stdout);
  }
}

void undoApply(bool undo) {
  Undo& u = sUndo;
  u.asking = false;
  if (!undo) {
    std::printf("[undo] kept\n");
    std::fflush(stdout);
    return;
  }
  portSettingsChangesQuiet(1); // (the undo itself is no change to offer back)
  for (const Change& c : u.changes) {
    portSettingsSetKey(c.key.c_str(), c.was.c_str());
    std::printf("[undo] undone: %s=%s\n", c.key.c_str(), c.was.c_str());
  }
  portSettingsSave();
  portSettingsChangesQuiet(0);
  std::fflush(stdout);
  u.said = portTr("Undone: it is back as it was. A setting used at start takes effect from the next start.");
}

} // namespace

bool portSettingsUndoRows() {
  Undo& u = sUndo;
  if (!u.looked) {
    undoLook();
  }
  if (!u.asking) {
    if (!u.said.empty()) {
      ImGui::TextWrapped("%s", u.said.c_str());
    }
    return false;
  }
  const Change& c = u.changes.front();
  const char* label = portSettingsLabelOf(c.key.c_str());
  const char* name = label != nullptr ? portTr(label) : c.key.c_str();
  char text[512];
  if (u.changes.size() == 1) {
    std::snprintf(text, sizeof(text), portTr("Paper Cut closed after you changed %s. Undo it?"), name);
  } else {
    std::snprintf(text, sizeof(text), portTr("Paper Cut closed after you changed %s and %d other settings. Undo them?"),
                  name, int(u.changes.size()) - 1);
  }
  ImGui::TextWrapped("%s", text);
  ImGui::PushID("undo_setting");
  int press = 0;
  if (ImGui::Button(portTrLabel("Undo##undo"))) {
    press = 1;
  }
  if (u.frames == 0) {
    ImGui::SetItemDefaultFocus(); // (the first place for a controller and the keyboard: the question first)
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Keep##undo"))) {
    press = 2;
  }
  ImGui::PopID();
  ImGui::Separator();
  u.frames++;
  if (press == 0 && u.test != nullptr && u.frames >= u.testAt) {
    press = std::strncmp(u.test, "undo", 4) == 0 ? 1 : 2;
    u.test = nullptr;
  }
  if (press != 0) {
    undoApply(press == 1);
  }
  return true;
}

// --- Continue, asked for by a window that isn't the recovery screen ---

namespace {
struct ContinueAsk {
  bool pending = false;
  std::string gameId, path;
  int slot = 3;
};
ContinueAsk sContinue;
} // namespace

void portSoftCrashRequestContinue(const std::string& gameId, const std::string& path, int slot) {
  sContinue = ContinueAsk{true, gameId, path, slot};
  const size_t slash = path.find_last_of("/\\");
  std::printf("[crashsafe] continue from %s as file %d\n", path.substr(slash == std::string::npos ? 0 : slash + 1).c_str(),
              slot + 1);
  std::fflush(stdout);
}

bool portSoftCrashTakeContinue(std::string* gameId, std::string* path, int* slot) {
  if (!sContinue.pending) {
    return false;
  }
  sContinue.pending = false;
  *gameId = sContinue.gameId;
  *path = sContinue.path;
  *slot = sContinue.slot;
  return true;
}

// --- after a run that stopped on screen ---

namespace {

// the snapshots of the version that stopped: the launcher doesn't know which it was, so the newest of any
// version's (a game over its own title: its own)
PortRecoveryResume stoppedResume(const char* gameId) {
  PortRecoveryResume r;
  const long long ended = portLastRunEndedAt();
  static const char* const kIds[] = {"G8ME01", "G8MJ01", "G8MP01"};
  for (const char* id : kIds) {
    if (gameId != nullptr && std::strcmp(gameId, id) != 0) {
      continue;
    }
    auto snaps = portCrashSafeSnaps(id, ended);
    if (!snaps.empty() && (r.snaps.empty() || snaps.front().made > r.snaps.front().made)) {
      r.snaps = snaps;
      r.gameId = id;
      r.paused = portCrashSafePaused(id, ended);
    }
  }
  if (r.snaps.empty() && gameId != nullptr) {
    r.gameId = gameId;
    r.paused = portCrashSafePaused(gameId, ended);
  }
  return r;
}

int sWindow = -1; // -1 not looked at, 0 not shown, 1 up, 2 done

} // namespace

// the Safe graphics offer's part (safe_graphics.cpp): the undo question and the snapshots inside it; true when
// a snapshot was picked (the offer closes: the launcher continues from it)
bool portSoftCrashOfferRows(const char* gameId) {
  static bool sLooked;
  static PortRecoveryResume sR;
  if (!sLooked) {
    sLooked = true;
    sR = stoppedResume(gameId);
    sWindow = 2; // (the offer is the one window: no other after it)
  }
  portSettingsUndoRows();
  const int pick = portCrashSafeRows(sR, false);
  if (pick >= 0 && pick < int(sR.snaps.size())) {
    portSoftCrashRequestContinue(sR.gameId, sR.snaps[size_t(pick)].path, sR.snaps[size_t(pick)].slot);
    return true;
  }
  return false;
}

int portSoftCrashWindowDraw(float scale, const char* gameId, bool modal, void (*inside)()) {
  static PortRecoveryResume sR;
  static int sFrames;
  if (sWindow == 0 || sWindow == 2) {
    return -1;
  }
  if (sWindow < 0) {
    if (!sUndo.looked) {
      undoLook();
    }
    sR = stoppedResume(gameId);
    sWindow = portLastRunStopped() && !portCrashReportPending() && (sUndo.asking || !sR.snaps.empty()) ? 1 : 0;
    if (sWindow == 0) {
      return -1;
    }
    std::printf("[crashsafe] stopped-on-screen window shown: %d snapshot%s, undo %s\n", int(sR.snaps.size()),
                sR.snaps.size() == 1 ? "" : "s", sUndo.asking ? "asked" : "none");
    std::fflush(stdout);
  }
  const std::string id = std::string(portTr("Paper Cut closed unexpectedly")) + "##softcrash";
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2((std::min)(display.x - 32.f, 600.f * scale), 0.f), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(display.x, (std::max)(120.f, display.y - 32.f)));
  bool open;
  if (modal) {
    if (!ImGui::IsPopupOpen(id.c_str())) {
      ImGui::OpenPopup(id.c_str());
    }
    open = ImGui::BeginPopupModal(id.c_str(), nullptr, flags);
  } else {
    if (sFrames == 0) {
      ImGui::SetNextWindowFocus();
    }
    open = ImGui::Begin(id.c_str(), nullptr, flags);
  }
  bool close = false;
  if (open) {
    if (!modal) {
      ImGui::SetWindowFontScale(scale);
    }
    if (inside != nullptr) {
      inside(); // (the game's: its place, so touches go to it)
    }
    ImGui::TextWrapped("%s", portTr("Last time the game stopped with no crash report: the system or the graphics "
                                    "driver ended it."));
    portSettingsUndoRows();
    int pick = portCrashSafeRows(sR, !sUndo.asking && sFrames == 0);
    {
      // tests: $TTYD_RECOVERY_TEST=snap:<frames> picks the newest here too (dismiss:<frames> closes)
      static const char* sTest = SDL_getenv("TTYD_RECOVERY_TEST");
      const char* colon = sTest != nullptr ? std::strchr(sTest, ':') : nullptr;
      if (colon != nullptr && sFrames >= std::atoi(colon + 1)) {
        if (std::strncmp(sTest, "snap", 4) == 0 && !sR.snaps.empty()) {
          pick = 0;
        } else if (std::strncmp(sTest, "dismiss", 7) == 0) {
          close = true;
        }
        sTest = nullptr;
      }
    }
    if (pick >= 0 && pick < int(sR.snaps.size())) {
      portSoftCrashRequestContinue(sR.gameId, sR.snaps[size_t(pick)].path, sR.snaps[size_t(pick)].slot);
      close = true;
    }
    if (ImGui::Button(portTrLabel("Load your save as usual##softcrash"))) {
      close = true;
    }
    if (!sUndo.asking && sR.snaps.empty() && sFrames == 0) {
      ImGui::SetItemDefaultFocus();
    }
    portReportProblemSection("softcrash");
    sFrames++;
    // the way back (B, Escape, Android's back, the mouse's back): as Load your save as usual
    if (!close && sFrames > 1 && !ImGui::IsAnyItemActive() &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
      close = true;
    }
    if (close && modal) {
      ImGui::CloseCurrentPopup();
    }
    if (modal) {
      ImGui::EndPopup();
    }
  }
  if (!modal) {
    ImGui::End();
  }
  if (close) {
    sWindow = 2;
    std::printf("[crashsafe] stopped-on-screen window closed\n");
    std::fflush(stdout);
    return 1;
  }
  return 0;
}
