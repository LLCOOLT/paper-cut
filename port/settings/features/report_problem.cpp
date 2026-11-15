// Report a problem (port/include/port/soft_crash.h): one button (Settings > General > Crash reports, the
// launcher's About, the recovery screen) that bundles what helps a report into one zip in the user folder:
// the last crash report, the graphics flight record, the memory record, the online diagnostics, the settings'
// number values and their recent changes, the build, system and graphics adapter, and the last log lines. What
// goes in is listed first, with each file's size; nothing is made until the player says so. Then GitHub's
// new-issue page opens with the bug form, its version and system filled in, and the zip's folder (on a phone or
// tablet: "Save a copy...", the system's own sheet for where it goes). Nothing is sent anywhere: the player
// attaches the zip to the issue themselves, or doesn't.
//
// Everything in it is scrubbed first (portReportScrub): the account's name and folder paths (portCrashScrub),
// the online names (this player's and the session's), the secret half of room codes, and IP addresses.
// Tests: $TTYD_REPORT_PROBLEM_TEST=<frames> opens the list and makes the zip after that many frames of the
// button drawn, as papercut-report-test.zip; "[report] ..." lines say what is in it.
#include "port/build_id.h"
#include "port/flight.h"
#include "port/offscreen.h"
#include "port/online.h"
#include "port/online_status.h"
#include "port/safe_graphics.h"
#include "port/settings.h"
#include "port/soft_crash.h"
#include "port/statesync.h"
#include "port/tr.h"

#include "../file_export.h"
#include "../file_util.h"
#include "../online_prefs.h"
#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define REPORT_PHONE 1 // (no file manager for a folder: the system's sheet to save or share the zip)
#endif

extern "C" void portCrashLogLine(const char* text); // crash_pc.c (port/platform.h, C only)

namespace {

constexpr const char* kIssues = "https://github.com/LLCOOLT/paper-cut/issues/new";

// --- the scrub ---

bool codeChar(char c) {
  return c != '\0' && std::strchr(PORT_ONLINE_CODE_CHARS, c) != nullptr;
}
bool wordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// a name (two letters or more) wherever it stands alone, any case
void scrubName(std::string& text, const std::string& name, const char* with) {
  if (name.size() < 2) {
    return;
  }
  for (size_t at = 0; at + name.size() <= text.size();) {
    bool same = true;
    for (size_t k = 0; k < name.size() && same; k++) {
      same = std::tolower(static_cast<unsigned char>(text[at + k])) == std::tolower(static_cast<unsigned char>(name[k]));
    }
    if (same && (at == 0 || !wordChar(text[at - 1])) &&
        (at + name.size() == text.size() || !wordChar(text[at + name.size()]))) {
      text.replace(at, name.size(), with);
      at += std::strlen(with);
    } else {
      at++;
    }
  }
}

// ROOM-SECRET (and ROOMSECRET) codes: the room's half kept, the secret's starred (it makes the session's key)
void scrubCodes(std::string& text) {
  const size_t half = PORT_ONLINE_ROOM_HALF, len = PORT_ONLINE_CODE_LENGTH;
  for (size_t at = 0; at + len <= text.size(); at++) {
    if (at > 0 && wordChar(text[at - 1])) {
      continue;
    }
    size_t k = 0;
    while (k < half && codeChar(text[at + k])) {
      k++;
    }
    if (k < half) {
      continue;
    }
    const size_t secret = at + half + (text[at + half] == '-' ? 1 : 0);
    if (secret + (len - half) > text.size()) {
      continue;
    }
    size_t m = 0;
    while (m < len - half && codeChar(text[secret + m])) {
      m++;
    }
    const size_t end = secret + m;
    if (m == len - half && (end == text.size() || !wordChar(text[end]))) {
      text.replace(secret, m, std::string(m, '*'));
    }
  }
}

// IPv4 (four numbers to 255) and IPv6 (hex and colons: a "::", or four colons or more) addresses
void scrubAddresses(std::string& text) {
  for (size_t at = 0; at < text.size(); at++) {
    if (at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '.' ||
                   text[at - 1] == ':')) {
      continue;
    }
    // IPv4
    size_t p = at;
    int parts = 0;
    bool ok = true;
    while (ok && parts < 4) {
      size_t digits = 0;
      int value = 0;
      while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p])) && digits < 4) {
        value = value * 10 + (text[p] - '0');
        p++;
        digits++;
      }
      ok = digits >= 1 && digits <= 3 && value <= 255;
      parts++;
      if (ok && parts < 4) {
        ok = p < text.size() && text[p] == '.';
        p++;
      }
    }
    if (ok && parts == 4 && (p == text.size() || (!std::isalnum(static_cast<unsigned char>(text[p])) &&
                                                   !(text[p] == '.' && p + 1 < text.size() &&
                                                     std::isdigit(static_cast<unsigned char>(text[p + 1])))))) {
      text.replace(at, p - at, "<ip>");
      continue;
    }
    // IPv6
    p = at;
    int colons = 0;
    bool doubled = false, hex = false;
    while (p < text.size() && (std::isxdigit(static_cast<unsigned char>(text[p])) || text[p] == ':')) {
      if (text[p] == ':') {
        colons++;
        doubled = doubled || (p + 1 < text.size() && text[p + 1] == ':');
      } else if (!std::isdigit(static_cast<unsigned char>(text[p]))) {
        hex = true;
      }
      p++;
    }
    if (p < text.size() && std::isalnum(static_cast<unsigned char>(text[p]))) {
      continue; // (part of a longer word)
    }
    if ((doubled && colons >= 2 && p - at >= 3) || (colons >= 4 && (hex || colons >= 5))) {
      text.replace(at, p - at, "<ip>");
    }
  }
}

} // namespace

std::string portReportScrub(std::string text) {
  text = portCrashScrub(text); // the folders, the home folder, the account's name
  portonline::loadPrefs();
  scrubName(text, portonline::gPrefs.name, "<player>");
  for (int p = 1; p <= 4; p++) {
    if (const char* name = portStateSyncPlayerName(p)) {
      scrubName(text, name, "<player>");
    }
  }
  scrubCodes(text);
  portNotesScrubCode(text); // (before the addresses: a code's digits could read as one)
  scrubAddresses(text);
  return text;
}

namespace {

struct Part {
  std::string name;  // in the zip
  const char* what;  // English, for portTr
  std::string data;  // scrubbed
};

std::string fileText(const std::string& path) {
  return path.empty() ? std::string() : portfile::readFile(path);
}

std::string userFile(const char* name) {
  const char* user = portSettingsUserPath();
  return std::string(user != nullptr ? user : ".") + "/" + name;
}

const char* backendName() {
  switch (aurora_get_backend()) {
  case BACKEND_D3D12:
    return "Direct3D 12";
  case BACKEND_D3D11:
    return "Direct3D 11";
  case BACKEND_METAL:
    return "Metal";
  case BACKEND_VULKAN:
    return "Vulkan";
  case BACKEND_OPENGL:
    return "OpenGL";
  case BACKEND_OPENGLES:
    return "OpenGL ES";
  default:
    return "?";
  }
}

std::string systemPart() {
  std::string text = portCrashSystemText();
  text += std::string("Build: ") + portBuildVersion() + "\nPlatform: " + SDL_GetPlatform() + "\nGraphics API: " +
          backendName() + "\nSafe graphics at start: " + (portSafeGraphicsAtStart() ? "on" : "off") +
          "\nLast run: " +
          (portCrashReportFound() ? "crashed" : portLastRunStopped() ? "stopped on screen" : "no crash this start") +
          "\n";
  return text;
}

std::vector<Part> gather() {
  std::vector<Part> parts;
  const auto add = [&parts](const char* name, const char* what, const std::string& raw) {
    if (!raw.empty()) {
      parts.push_back(Part{name, what, portReportScrub(raw)});
    }
  };
  add("system.txt", "the version, the build, the system and the graphics card", systemPart());
  add("crash_report.txt", "the last crash report", portCrashReportText());
  {
    const char* flight = portFlightLastPath();
    add("flight_record_last.txt", "what the graphics were doing as the last run ended",
        fileText(flight != nullptr ? flight : ""));
  }
  add("memory_record_last.txt", "the memory record of the last run (phones and tablets)",
      fileText(userFile("memory_record_last.txt")));
  {
    std::string diag(12288, '\0');
    diag.resize(portOnlineDiagnostics(diag.data(), diag.size()));
    add("online_diagnostics.txt", "the online diagnostics: the connection, no names or codes", diag);
  }
  add("settings.txt", "your settings' values (numbers only)", portCrashSettingsText());
  add("settings_changes.txt", "the settings you changed lately, and when", fileText(userFile("settings_changes.txt")));
  {
    std::string log(16384, '\0');
    log.resize(portCrashLogTail(log.data(), log.size()));
    add("log.txt", "the last lines of this run's log", log);
  }
  return parts;
}

std::string sizeText(size_t bytes) {
  char text[32];
  if (bytes < 1024) {
    std::snprintf(text, sizeof(text), portTr("%d bytes"), int(bytes));
  } else {
    std::snprintf(text, sizeof(text), portTr("%d KB"), int((bytes + 1023) / 1024));
  }
  return text;
}

// the issue form's own options (.github/ISSUE_TEMPLATE/1-bug.yml): platform and the game's version
const char* platformOption() {
#if defined(__ANDROID__)
  return "Android";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  return "iPhone or iPad";
#elif defined(__APPLE__)
  return "macOS";
#elif defined(_WIN32)
  return "Windows";
#else
  return SDL_getenv("SteamDeck") != nullptr ? "Steam Deck" : "Linux";
#endif
}

const char* discOption() {
  const char* id = PORT_GAME_ID;
  return std::strcmp(id, "G8MJ01") == 0 ? "Japan (G8MJ01)" : std::strcmp(id, "G8MP01") == 0 ? "Europe (G8MP01)" : "US (G8ME01)";
}

// a line's value from system.txt's "Name: value" lines
std::string lineOf(const std::string& text, const char* name) {
  const size_t at = text.find(std::string(name) + ": ");
  if (at == std::string::npos) {
    return {};
  }
  const size_t from = at + std::strlen(name) + 2;
  return text.substr(from, text.find('\n', from) - from);
}

// the bug form with what the game knows filled in (GitHub fills a form's fields by their ids)
std::string issueUrl(const std::string& zipName) {
  const std::string sys = portReportScrub(portCrashSystemText());
  std::string system = lineOf(sys, "System");
  const std::string gpu = lineOf(sys, "Graphics");
  if (!gpu.empty() && gpu != "?") {
    system += ", " + gpu;
  }
  const std::string media = "Attach " + zipName + " here (made by Report a problem; it is in Paper Cut's save folder).";
  return std::string(kIssues) + "?template=1-bug.yml&version=" + portUrlEncode(PORT_VERSION) + "&platform=" +
         portUrlEncode(platformOption()) + "&disc=" + portUrlEncode(discOption()) + "&system=" + portUrlEncode(system) +
         "&media=" + portUrlEncode(media);
}

struct State {
  bool open = false;
  std::string where;         // the place whose panel is open
  std::vector<Part> parts;   // what goes in, as listed
  std::string zipPath, zipName;
  std::mutex lock;           // (a phone's save sheet answers on another thread)
  std::string message;       // what happened, shown under the buttons
  int frames = 0;
  int testAt = -1;
};
State sState;

void setMessage(const std::string& text) {
  std::lock_guard<std::mutex> lock(sState.lock);
  sState.message = text;
}

void make(bool test) {
  State& s = sState;
  char name[64];
  const std::time_t now = std::time(nullptr);
  const std::tm* t = std::localtime(&now);
  if (test) {
    std::snprintf(name, sizeof(name), "papercut-report-test.zip");
  } else if (t == nullptr || std::strftime(name, sizeof(name), "papercut-report-%Y%m%d-%H%M%S.zip", t) == 0) {
    std::snprintf(name, sizeof(name), "papercut-report.zip");
  }
  std::vector<std::pair<std::string, std::string>> files;
  for (const Part& p : s.parts) {
    files.emplace_back(p.name, p.data);
  }
  const std::string data = portZipStored(files);
  s.zipName = name;
  s.zipPath = userFile(name);
  if (!portfile::writeAll(s.zipPath, data.data(), data.size())) {
    setMessage(portTr("The zip couldn't be made in the save folder."));
    std::printf("[report] zip not made: %s\n", SDL_GetError());
    std::fflush(stdout);
    s.zipPath.clear();
    return;
  }
  std::printf("[report] zip: %s, %d files, %zu bytes:", name, int(s.parts.size()), data.size());
  for (const Part& p : s.parts) {
    std::printf(" %s", p.name.c_str());
  }
  std::printf("\n");
  std::fflush(stdout);
  char text[512];
  std::snprintf(text, sizeof(text),
                portTr("Made %s in the save folder. Add what happened on the GitHub page and attach the zip there."),
                name);
  std::string said = text;
  const std::string url = issueUrl(name);
  std::printf("[report] issue: %s\n", url.c_str());
  std::fflush(stdout);
  char why[1200];
  if (!portOpenUrl(url.c_str())) {
    said += std::string("\n") + portOpenError(why, sizeof(why), "https://github.com/LLCOOLT/paper-cut/issues", false);
  }
#if !defined(REPORT_PHONE)
  {
    const char* user = portSettingsUserPath();
    if (!portOpenFolder(user != nullptr ? user : ".")) {
      said += std::string("\n") + portOpenError(why, sizeof(why), user != nullptr ? user : ".", true);
    }
  }
#endif
  setMessage(said);
}

#if defined(REPORT_PHONE)
// a phone or tablet: the zip where the player picks (the system's sheet: Files, Drive, a share target...)
void saveCopy() {
  State& s = sState;
  const std::vector<uint8_t> bytes = portfile::readAll(s.zipPath);
  if (bytes.empty()) {
    setMessage(portTr("The zip couldn't be made in the save folder."));
    return;
  }
  static const SDL_DialogFileFilter kZip[] = {{"Zip", "zip"}};
  portfile::exportFile(
      [](void*, const char* const* files, int) {
        if (files == nullptr) {
          setMessage(portTr("The zip couldn't be saved there."));
        } else if (files[0] != nullptr) {
          setMessage(portTr("Saved a copy of the zip."));
        }
      },
      nullptr, kZip, 1, s.zipName.c_str(), bytes);
}
#endif

} // namespace

void portReportProblemSection(const char* where) {
  State& s = sState;
  if (s.testAt < 0) {
    const char* test = SDL_getenv("TTYD_REPORT_PROBLEM_TEST");
    s.testAt = test != nullptr ? std::max(1, std::atoi(test)) : 0;
    if (s.testAt > 0) {
      // (a line with an address, a room code and a name in the log: none of it may reach the zip)
      portCrashLogLine("[report] test: from 203.0.113.7 and fe80::1:2 in room 7KQ2-9XWZ");
      portNotesCodeTestLine(); // (and the paper notes' device code, when there is one)
    }
  }
  ImGui::PushID("report_problem");
  ImGui::PushID(where);
  const bool here = s.open && s.where == where;
  bool press = ImGui::Button(portTrLabel("Report a problem...##report"));
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("One zip of what helps a bug report, shown before it is made. Nothing is sent: you "
                                   "attach it on GitHub yourself."));
  }
  if (s.testAt > 0 && !s.open && ++s.frames == s.testAt) {
    press = true;
  }
  if (press && !here) {
    s.open = true;
    s.where = where;
    s.parts = gather();
    s.zipPath.clear();
    setMessage("");
    std::printf("[report] the list shown (%s): %d files\n", where, int(s.parts.size()));
    std::fflush(stdout);
  } else if (press) {
    s.open = false; // (pressed again: closed)
  }
  if (s.open && s.where == where) {
    ImGui::Indent();
    if (s.zipPath.empty()) {
      ImGui::TextWrapped("%s", portTr("The zip will hold:"));
      for (const Part& p : s.parts) {
        ImGui::BulletText("%s (%s): %s", p.name.c_str(), sizeText(p.data.size()).c_str(), portTr(p.what));
      }
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("%s", portTr("Taken out first: your name in folder paths, online names, the secret half of "
                                      "room codes and IP addresses. Nothing is sent anywhere: you attach the zip to "
                                      "the GitHub issue yourself."));
      ImGui::PopStyleColor();
      bool makeIt = ImGui::Button(portTrLabel("Make the zip and open GitHub##report_make"));
      if (s.testAt > 0 && s.frames >= s.testAt) {
        makeIt = true;
        s.testAt = 0; // (once)
      }
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Cancel##report_cancel"))) {
        s.open = false;
      }
      if (makeIt) {
        make(SDL_getenv("TTYD_REPORT_PROBLEM_TEST") != nullptr);
      }
    } else {
#if defined(REPORT_PHONE)
      if (ImGui::Button(portTrLabel("Save a copy of the zip...##report_copy"))) {
        saveCopy();
      }
#else
      if (ImGui::Button(portTrLabel("Open its folder##report_folder"))) {
        const char* user = portSettingsUserPath();
        char why[1200];
        if (!portOpenFolder(user != nullptr ? user : ".")) {
          setMessage(portOpenError(why, sizeof(why), user != nullptr ? user : ".", true));
        }
      }
#endif
      ImGui::SameLine();
      if (ImGui::Button(portTrLabel("Done##report_done"))) {
        s.open = false;
      }
    }
    std::string message;
    {
      std::lock_guard<std::mutex> lock(s.lock);
      message = s.message;
    }
    if (!message.empty()) {
      ImGui::TextWrapped("%s", message.c_str());
    }
    if (!s.zipPath.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped(portTr("It is here: %s"), s.zipPath.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Unindent();
  }
  ImGui::PopID();
  ImGui::PopID();
}
