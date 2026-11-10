// Sending a crash report (port/settings/settings_ui.h):
// after a crash, the next start's notice (and the General tab's Crash reports)
// offer it, and nothing leaves the computer without a click.
//
//   Report on GitHub   a new issue on LLCOOLT/paper-cut opened in the browser,
//                      filled in: the version, the system, the graphics
//                      adapter and the report with its last log lines. The
//                      player reads it there and submits it, or doesn't.
//   Save as a zip...   the same in a zip, in a folder the player picks, with
//                      the settings' number values (settings.txt, no paths).
//
// Both scrub the player's own names from the text first: the user folder's
// path, the home folder's, the account name, and any C:\Users\<name>\,
// /Users/<name>/ or /home/<name>/ (the build's own source paths too).
#include "port/settings.h"
#include "port/offscreen.h"
#include "port/resume.h"
#include "port/tr.h"

#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // ImTextCharFromUtf8
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#elif defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

extern "C" const char* portCrashGpu(void); // crash_pc.c (port/platform.h, C only)

namespace {

constexpr const char* kIssues = "https://github.com/LLCOOLT/paper-cut/issues/new";
// (the recovery screen shows this one as text: where the player reports it)
constexpr const char* kIssuesShown = "https://github.com/LLCOOLT/paper-cut/issues";
constexpr size_t kUrlMax = 7000; // GitHub (and browsers) take about 8 KB of address

// wholeWord: not inside a longer word (an account named "deck" leaves "Steam Deck" alone, case aside)
void replaceAll(std::string& text, const std::string& what, const std::string& with, bool anyCase,
                bool wholeWord = false) {
  if (what.size() < 2) {
    return;
  }
  for (size_t at = 0; at + what.size() <= text.size();) {
    bool same = true;
    for (size_t k = 0; k < what.size() && same; k++) {
      same = anyCase ? std::tolower((unsigned char)text[at + k]) == std::tolower((unsigned char)what[k])
                     : text[at + k] == what[k];
    }
    if (same && wholeWord) {
      const auto letter = [](char c) { return std::isalpha((unsigned char)c) != 0; };
      same = (at == 0 || !letter(text[at - 1])) &&
             (at + what.size() == text.size() || !letter(text[at + what.size()]));
    }
    if (same) {
      text.replace(at, what.size(), with);
      at += with.size();
    } else {
      at++;
    }
  }
}

// <prefix><name><separator>: the name made <user> (prefix any case)
void scrubAfter(std::string& text, const char* prefix) {
  const size_t n = std::strlen(prefix);
  for (size_t at = 0; at + n < text.size(); at++) {
    bool same = true;
    for (size_t k = 0; k < n && same; k++) {
      same = std::tolower((unsigned char)text[at + k]) == std::tolower((unsigned char)prefix[k]);
    }
    if (!same) {
      continue;
    }
    size_t end = at + n;
    while (end < text.size() && text[end] != '\\' && text[end] != '/' && text[end] != '\n' && text[end] != '"' &&
           text[end] != ')' && text[end] != ':') {
      end++;
    }
    if (end > at + n && text.compare(at + n, end - at - n, "<user>") != 0) {
      text.replace(at + n, end - at - n, "<user>");
    }
    at += n;
  }
}

std::string withSlashes(std::string path, char from, char to) {
  for (char& c : path) {
    if (c == from) {
      c = to;
    }
  }
  while (!path.empty() && (path.back() == '/' || path.back() == '\\')) {
    path.pop_back();
  }
  return path;
}

// The next start's notice can be turned off (#27): the report is still kept, under Crash reports
// (settings_aurora.c's portCrashReportPending reads it)
const bool sNoticeAdded = [] {
  portSettingsDeclare("crash_notice", 1, 0, 1);
  portSettingsAddToggle("General", "Crash notice at start",
                        "After a crash, the next start offers to send its report. Off: no notice; the report is "
                        "still kept here under Crash reports.",
                        0, "crash_notice", "crash report bug notice prompt popup");
  return true;
}();

} // namespace

std::string portCrashScrub(std::string text) {
  // the user folder and the home folder, as they are and with the other slashes
  // (the user folder first: it's usually inside the home folder)
  std::vector<std::pair<std::string, const char*>> folders;
  if (const char* user = portSettingsUserPath()) {
    folders.emplace_back(user, "<user folder>");
  }
  if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME)) {
    folders.emplace_back(home, "<home>");
  }
  for (const auto& [f, name] : folders) {
    replaceAll(text, withSlashes(f, '/', '\\'), name, true);
    replaceAll(text, withSlashes(f, '\\', '/'), name, true);
  }
  scrubAfter(text, "\\Users\\");
  scrubAfter(text, "/Users/");
  scrubAfter(text, "/home/");
  scrubAfter(text, "\\home\\");
  // the account's name anywhere else (a name of three letters or more)
  for (const char* var : {"USERNAME", "USER", "LOGNAME"}) {
    const char* name = SDL_getenv(var);
    if (name != nullptr && std::strlen(name) >= 3) {
#ifdef _WIN32
      replaceAll(text, name, "<user>", true, true); // (Windows names: any case)
#else
      replaceAll(text, name, "<user>", false, true);
#endif
    }
  }
  return text;
}

namespace {

std::string osName() {
  char text[160];
#if defined(_WIN32)
  // (RtlGetVersion: GetVersionEx says 8 to programs without a manifest naming 10)
  typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
  OSVERSIONINFOW v = {};
  v.dwOSVersionInfoSize = sizeof(v);
  const auto get = reinterpret_cast<RtlGetVersionFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
  if (get != nullptr && get(&v) == 0) {
    std::snprintf(text, sizeof(text), "Windows %s (%lu.%lu.%lu)", v.dwBuildNumber >= 22000 ? "11" : "10",
                  v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    return text;
  }
  return "Windows";
#elif defined(__APPLE__)
  char version[64] = "";
  size_t size = sizeof(version);
  sysctlbyname("kern.osproductversion", version, &size, nullptr, 0);
  std::snprintf(text, sizeof(text), "%s %s", SDL_GetPlatform(), version);
  return text;
#elif defined(__ANDROID__)
  char version[PROP_VALUE_MAX] = "", model[PROP_VALUE_MAX] = "";
  __system_property_get("ro.build.version.release", version);
  __system_property_get("ro.product.model", model);
  std::snprintf(text, sizeof(text), "Android %s (%s)", version, model);
  return text;
#else
  std::string name = SDL_GetPlatform();
  if (char* release = static_cast<char*>(SDL_LoadFile("/etc/os-release", nullptr))) {
    const char* p = std::strstr(release, "PRETTY_NAME=");
    if (p != nullptr) {
      p += 12;
      std::string pretty(p, std::strcspn(p, "\n"));
      if (pretty.size() >= 2 && pretty.front() == '"') {
        pretty = pretty.substr(1, pretty.size() - 2);
      }
      name += " (" + pretty + ")";
    }
    SDL_free(release);
  }
  if (SDL_getenv("SteamDeck") != nullptr) {
    name += ", Steam Deck";
  }
  return name;
#endif
}

// how this copy was installed, where that matters (Linux: an AppImage runs from a mount, a Flatpak in a sandbox)
std::string packageName() {
#if defined(__linux__) && !defined(__ANDROID__)
  if (SDL_getenv("APPIMAGE") != nullptr) {
    return "AppImage";
  }
  SDL_PathInfo info;
  if (SDL_GetPathInfo("/.flatpak-info", &info)) {
    return "Flatpak";
  }
  return "zip";
#else
  return {};
#endif
}

std::string systemText() {
  const char* gpu = portCrashGpu();
  const std::string package = packageName();
  std::string out = "Paper Cut: " PORT_VERSION + (package.empty() ? std::string() : " (" + package + ")") +
                    "\nGame: " PORT_GAME_ID "\nSystem: " + osName() + "\nGraphics: " +
                    (gpu != nullptr && gpu[0] != '\0' ? gpu : "?") + "\n";
  return out;
}

// The zip's settings.txt: the settings as they are now, their number values only (on/off, choices,
// sliders; no paths, names or codes), so a report shows what was changed from the defaults.
std::string settingsText() {
  std::string out;
  const char* user = portSettingsUserPath();
  if (user == nullptr) {
    return out;
  }
  size_t size = 0;
  char* data = static_cast<char*>(SDL_LoadFile((std::string(user) + "/settings.txt").c_str(), &size));
  if (data == nullptr) {
    return out;
  }
  const std::string text(data, size);
  SDL_free(data);
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= line.size()) {
      continue;
    }
    bool key = true, number = true;
    for (size_t i = 0; i < eq; i++) {
      key = key && (std::islower((unsigned char)line[i]) || std::isdigit((unsigned char)line[i]) || line[i] == '_');
    }
    for (size_t i = eq + 1; i < line.size(); i++) {
      number = number && (std::isdigit((unsigned char)line[i]) || (i == eq + 1 && line[i] == '-'));
    }
    if (key && number && line.size() - eq <= 12) {
      out += line + "\n";
    }
  }
  return out;
}

std::string reportText() {
  std::string text;
  const char* path = portCrashReportLast();
  if (path != nullptr) {
    if (char* data = static_cast<char*>(SDL_LoadFile(path, nullptr))) {
      text = data;
      SDL_free(data);
    }
  }
  return portCrashScrub(text);
}

std::string urlEncode(const std::string& s) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += char(c);
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  return out;
}

std::string issueUrl() {
  std::string report = reportText();
  std::string title = "Crash: ";
  {
    // the report's "*** crash: ..." line names it
    const size_t at = report.find("*** ");
    std::string what = at != std::string::npos ? report.substr(at + 4, report.find('\n', at) - at - 4) : "";
    if (what.rfind("crash: ", 0) == 0) {
      what.erase(0, 7); // ("Crash: crash: ...")
    }
    title += what.empty() ? "the game crashed" : what;
    if (title.size() > 90) {
      title.resize(90);
    }
  }
  const std::string head = "**What were you doing when it crashed?**\n\n\n\n**System**\n```\n" +
                           portCrashScrub(systemText()) + "```\n\n**Crash report**\n```\n";
  const std::string tail = "\n```\n";
  const std::string cut = "\n(cut short: the rest is in the report's zip, Settings > General > Crash reports)";
  std::string body = head + report + tail;
  std::string url = std::string(kIssues) + "?title=" + urlEncode(title) + "&body=" + urlEncode(body);
  // too long for an address: the report's start kept (the header, the crash and the stack's top)
  while (url.size() > kUrlMax && report.size() > 200) {
    report = report.substr(0, report.size() * 3 / 4);
    body = head + report + cut + tail;
    url = std::string(kIssues) + "?title=" + urlEncode(title) + "&body=" + urlEncode(body);
  }
  return url;
}

// a zip of plain stored files (zlib's crc32), enough for two text files
void put16(std::string& z, unsigned v) {
  z += char(v & 0xFF);
  z += char((v >> 8) & 0xFF);
}
void put32(std::string& z, unsigned long v) {
  put16(z, unsigned(v & 0xFFFF));
  put16(z, unsigned((v >> 16) & 0xFFFF));
}

std::string zip(const std::vector<std::pair<std::string, std::string>>& files) {
  std::string z, dir;
  const std::time_t now = std::time(nullptr);
  const std::tm* t = std::localtime(&now);
  const unsigned dosTime = t != nullptr ? unsigned(t->tm_hour << 11 | t->tm_min << 5 | t->tm_sec / 2) : 0;
  const unsigned dosDate = t != nullptr ? unsigned((t->tm_year - 80) << 9 | (t->tm_mon + 1) << 5 | t->tm_mday) : 0;
  for (const auto& [name, data] : files) {
    const unsigned long crc = crc32(0L, reinterpret_cast<const Bytef*>(data.data()), uInt(data.size()));
    const unsigned long offset = z.size();
    put32(z, 0x04034b50);
    put16(z, 10);
    put16(z, 0x0800); // (UTF-8 names)
    put16(z, 0);      // stored
    put16(z, dosTime);
    put16(z, dosDate);
    put32(z, crc);
    put32(z, data.size());
    put32(z, data.size());
    put16(z, unsigned(name.size()));
    put16(z, 0);
    z += name;
    z += data;
    put32(dir, 0x02014b50);
    put16(dir, 20);
    put16(dir, 10);
    put16(dir, 0x0800);
    put16(dir, 0);
    put16(dir, dosTime);
    put16(dir, dosDate);
    put32(dir, crc);
    put32(dir, data.size());
    put32(dir, data.size());
    put16(dir, unsigned(name.size()));
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, 0);
    put32(dir, 0);
    put32(dir, offset);
    dir += name;
  }
  const unsigned long dirAt = z.size();
  z += dir;
  put32(z, 0x06054b50);
  put16(z, 0);
  put16(z, 0);
  put16(z, unsigned(files.size()));
  put16(z, unsigned(files.size()));
  put32(z, dir.size());
  put32(z, dirAt);
  put16(z, 0);
  return z;
}

std::mutex sLock;
std::string sFolder;
std::atomic<bool> sPending{false};
std::string sMessage;

std::string sFolderError; // the folder dialog couldn't show (portPickerError's text; sLock)

void folderChosen(void*, const char* const* files, int) {
  if (files == nullptr) { // (an error, not a cancel: said by saveWork)
    char why[1024];
    portPickerError(why, sizeof(why), nullptr);
    std::lock_guard<std::mutex> lock(sLock);
    sFolderError = why;
    sPending = true;
  } else if (files[0] != nullptr) {
    std::lock_guard<std::mutex> lock(sLock);
    sFolder = files[0];
    sPending = true;
  }
}

void saveWork() {
  if (!sPending) {
    return;
  }
  std::string folder, error;
  {
    std::lock_guard<std::mutex> lock(sLock);
    folder = sFolder;
    error.swap(sFolderError);
    sPending = false;
  }
  if (!error.empty()) {
    sMessage = error;
    return;
  }
  char name[64];
  const std::time_t now = std::time(nullptr);
  const std::tm* t = std::localtime(&now);
  if (t == nullptr || std::strftime(name, sizeof(name), "papercut-crash-%Y%m%d-%H%M%S.zip", t) == 0) {
    std::snprintf(name, sizeof(name), "papercut-crash.zip");
  }
  const std::string path = folder + (folder.empty() || folder.back() == '/' || folder.back() == '\\' ? "" : "/") + name;
  const std::string data = zip({{"crash_report.txt", reportText()},
                                {"system.txt", portCrashScrub(systemText())},
                                {"settings.txt", settingsText()}});
  sMessage = SDL_SaveFile(path.c_str(), data.data(), data.size()) ? std::string(portTr("Saved: ")) + name
                                                                  : portTr("The zip couldn't be saved there.");
  portReleasePickedFolder(folder.c_str()); // (iOS: the folder's access, kept from the pick until now)
}

} // namespace

#if defined(__ANDROID__)
#define CRASH_NO_FOLDERS 1 // (no folder picker on Android: the GitHub page, or Copy; iOS: the document picker's)
#endif

void portCrashReportButtons() {
  saveWork();
  if (portCrashReportLast() == nullptr) {
    return;
  }
  if (ImGui::Button(portTrLabel("Report on GitHub##crash_issue"))) {
    char why[512];
    sMessage = portOpenUrl(issueUrl().c_str())
                   ? portTr("Opened in your browser: read it, add what you were doing, and submit it there if you like.")
                   : portOpenError(why, sizeof(why), kIssuesShown, false); // (the issues' page, to go to by hand)
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("A new issue on Paper Cut's GitHub, filled in. Nothing is sent until you submit "
                                   "it there."));
  }
#if !defined(CRASH_NO_FOLDERS)
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Save as a zip...##crash_zip"))) {
    portShowOpenFolderDialog(folderChosen, nullptr, SDL_GetKeyboardFocus(), nullptr, false);
  }
#endif
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", sMessage.empty() ? portTr("It holds the version, the system, the graphics card and where "
                                                     "it crashed; your name is taken out of folder paths.")
                                            : sMessage.c_str());
  ImGui::PopStyleColor();
}

// Tests ($TTYD_CRASH_REPORT_TEST=<file>): the issue's address, the scrubbed report and the zip's settings.txt
// written there
// (main_aurora.c doesn't call it: a test build's crash notice calls it, settings_ui.cpp)
void portCrashReportTest() {
  const char* out = SDL_getenv("TTYD_CRASH_REPORT_TEST");
  static bool sDone;
  if (out == nullptr || sDone || portCrashReportLast() == nullptr) {
    return;
  }
  sDone = true;
  const std::string text = issueUrl() + "\n\n" + reportText() + "\n" + portCrashScrub(systemText()) +
                           "\n--- settings.txt (the zip's)\n" + settingsText();
  SDL_SaveFile(out, text.data(), text.size());
}

// --- The recovery screen (settings_ui.h) ---
//
// "Paper Cut closed unexpectedly", once at the next start after a crash: Continue from quick resume's
// save (where it was made and how long before the crash), the report itself (exactly the text Copy
// puts on the clipboard, the player's name taken out of paths as above: nothing in it they haven't
// seen), Copy the report, Open its folder (desktops), the issues' address as text (a click opens a
// new issue there, filled in; nothing is sent without submitting it), and Dismiss.
// Tests: $TTYD_RECOVERY_TEST=continue:<frames> or dismiss:<frames> presses that button after that many
// frames of the screen; "[recovery] ..." lines say what it shows and does.

std::string portCrashReportGameId() {
  const char* path = portCrashReportLast();
  std::string id;
  if (path == nullptr) {
    return id;
  }
  if (char* data = static_cast<char*>(SDL_LoadFile(path, nullptr))) {
    // "version: US (G8ME01), built ..."
    const char* v = std::strstr(data, "\nversion: ");
    const char* eol = v != nullptr ? std::strchr(v + 1, '\n') : nullptr;
    const char* open = v != nullptr ? std::strchr(v, '(') : nullptr;
    if (open != nullptr && (eol == nullptr || open < eol) && std::strlen(open) > 7 && open[7] == ')') {
      id.assign(open + 1, 6);
    }
    SDL_free(data);
  }
  return id;
}

PortRecoveryResume portRecoveryResume(const char* gameId) {
  PortRecoveryResume r;
  char path[1024];
  SDL_PathInfo info;
  if (gameId == nullptr || gameId[0] == '\0') {
    return r;
  }
  portResumePath(path, sizeof(path), gameId, 0);
  if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
    return r;
  }
  r.have = true;
  r.gameId = gameId;
  r.path = path;
  long long made = static_cast<long long>(info.modify_time / SDL_NS_PER_SECOND);
  portResumePath(path, sizeof(path), gameId, 1);
  size_t size = 0;
  if (char* data = static_cast<char*>(SDL_LoadFile(path, &size))) {
    const std::string text(data, size);
    SDL_free(data);
    if (!text.empty() && text[0] >= '0' && text[0] <= '3') {
      r.slot = text[0] - '0';
    }
    for (size_t at = 0; at < text.size();) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) {
        end = text.size();
      }
      std::string line = text.substr(at, end - at);
      at = end + 1;
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.rfind("time=", 0) == 0) {
        made = std::atoll(line.c_str() + 5);
      } else if (line.rfind("map=", 0) == 0) {
        r.map = line.substr(4);
      } else if (line.rfind("area=", 0) == 0) {
        r.area = line.substr(5);
      }
    }
  }
  if (const char* report = portCrashReportLast(); report != nullptr && SDL_GetPathInfo(report, &info)) {
    const long long crashed = static_cast<long long>(info.modify_time / SDL_NS_PER_SECOND);
    r.before = made > 0 && crashed >= made ? crashed - made : -1;
  }
  return r;
}

namespace {

// every letter of text in the current font (an area's name in Japanese, with the launcher's font
// holding only its own text's kanji, would be boxes)
bool fontHas(const std::string& text) {
  ImFont* font = ImGui::GetFont();
  for (const char* p = text.c_str(); *p != '\0';) {
    unsigned int c = 0;
    const int n = ImTextCharFromUtf8(&c, p, nullptr);
    if (n <= 0) {
      return false;
    }
    p += n;
    if (c >= 0x80 && (c > 0xFFFF || font->FindGlyphNoFallback(static_cast<ImWchar>(c)) == nullptr)) {
      return false;
    }
  }
  return true;
}

std::string beforeText(long long s) {
  char text[120];
  if (s < 60) {
    return portTr("less than a minute before it closed");
  }
  if (s < 120) {
    return portTr("1 minute before it closed");
  }
  if (s < 3600) {
    std::snprintf(text, sizeof(text), portTr("%d minutes before it closed"), int(s / 60));
    return text;
  }
  if (s < 7200) {
    return portTr("1 hour before it closed");
  }
  if (s < 2 * 86400) {
    std::snprintf(text, sizeof(text), portTr("%d hours before it closed"), int(s / 3600));
    return text;
  }
  std::snprintf(text, sizeof(text), portTr("%d days before it closed"), int(s / 86400));
  return text;
}

// the report's "*** crash: ..." line, for the log
std::string crashLine(const std::string& report) {
  const size_t at = report.find("*** ");
  return at == std::string::npos ? "?" : report.substr(at + 4, report.find('\n', at) - at - 4);
}

} // namespace

int portRecoveryDraw(const PortRecoveryResume& r, float scale, bool modal, void (*inside)()) {
  portCrashReportTest(); // ($TTYD_CRASH_REPORT_TEST, once)
  static std::string sReport;
  static bool sLoaded, sCopied, sLogged, sFieldActive;
  static int sFrames, sTestAt = -1, sTestPress;
  static std::string sOpenSaid; // a folder or link the system didn't open: where it is
  if (!sLoaded) {
    sLoaded = true;
    sReport = reportText();
    if (const char* test = SDL_getenv("TTYD_RECOVERY_TEST")) {
      const char* colon = std::strchr(test, ':');
      sTestPress = std::strncmp(test, "continue", 8) == 0 ? 1 : std::strncmp(test, "dismiss", 7) == 0 ? 2 : 0;
      sTestAt = colon != nullptr ? std::atoi(colon + 1) : 60;
    }
  }
  const std::string where = r.area.empty() || !fontHas(r.area) ? r.map : r.area;
  if (!sLogged) {
    sLogged = true;
    if (r.have) {
      std::printf("[recovery] shown: %s; continue: %s (map %s, file %d, %lld s before it closed)\n",
                  crashLine(sReport).c_str(), r.area.empty() ? "?" : r.area.c_str(),
                  r.map.empty() ? "?" : r.map.c_str(), r.slot + 1, r.before);
    } else {
      std::printf("[recovery] shown: %s; continue: none\n", crashLine(sReport).c_str());
    }
    std::fflush(stdout);
  }
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const std::string id = std::string(portTr("Paper Cut closed unexpectedly")) + "##recovery";
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2((std::min)(display.x - 32.f, 600.f * scale), 0.f), ImGuiCond_Always);
  bool open;
  if (modal) {
    if (!ImGui::IsPopupOpen(id.c_str())) {
      ImGui::OpenPopup(id.c_str());
    }
    open = ImGui::BeginPopupModal(id.c_str(), nullptr, flags);
  } else {
    open = ImGui::Begin(id.c_str(), nullptr, flags);
  }
  int result = 0;
  if (open) {
    if (!modal) {
      ImGui::SetWindowFontScale(scale);
    }
    if (inside != nullptr) {
      inside();
    }
    if (r.have && !where.empty() && r.before >= 0) {
      ImGui::TextWrapped(portTr("Continue where you were: %s, %s."), where.c_str(), beforeText(r.before).c_str());
    } else if (r.have && !where.empty()) {
      ImGui::TextWrapped(portTr("Continue where you were: %s."), where.c_str());
    } else if (r.have) {
      ImGui::TextWrapped("%s", portTr("Continue where you were, from quick resume's save."));
    } else {
      ImGui::TextWrapped("%s", portTr("There is no quick resume save to continue from."));
    }
    if (r.have) {
      if (ImGui::Button(portTrLabel("Continue"))) {
        result = 1;
      }
      if (sFrames == 0) {
        ImGui::SetItemDefaultFocus();
      }
      ImGui::SameLine();
    }
    if (ImGui::Button(portTrLabel("Dismiss##recovery"))) {
      result = 2;
    }
    if (!r.have && sFrames == 0) {
      ImGui::SetItemDefaultFocus(); // (a controller's and the keyboard's first place: no Continue)
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", portTr("The crash report, exactly as Copy gives it: the version, the graphics card, "
                                    "where the game was and where it crashed. Your name is taken out of folder "
                                    "paths."));
    ImGui::PopStyleColor();
    const bool fieldWas = sFieldActive; // (last frame's: Escape there leaves the report's text, not the screen)
    ImGui::InputTextMultiline("##recovery_report", const_cast<char*>(sReport.c_str()), sReport.size() + 1,
                              ImVec2(-1.f, ImGui::GetTextLineHeight() * 8.f), ImGuiInputTextFlags_ReadOnly);
    sFieldActive = ImGui::IsItemActive();
    if (ImGui::Button(portTrLabel(sCopied ? "Copied##recovery_copy" : "Copy the report##recovery_copy"))) {
      sCopied = SDL_SetClipboardText(sReport.c_str());
    }
    char why[1200];
#if !defined(CRASH_NO_FOLDERS)
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Open its folder##recovery_folder"))) {
      // (a click only: the user folder, where last_crash.txt is)
      const char* user = portSettingsUserPath();
      user = user != nullptr ? user : ".";
      sOpenSaid = portOpenFolder(user) ? std::string() : std::string(portOpenError(why, sizeof(why), user, true));
    }
#endif
    ImGui::TextUnformatted(portTr("Report it on GitHub:"));
    if (ImGui::TextLink(kIssuesShown)) {
      // (a click only: a new issue there, filled in with the report; else its page's address to go to)
      sOpenSaid = portOpenUrl(issueUrl().c_str()) ? std::string()
                                                  : std::string(portOpenError(why, sizeof(why), kIssuesShown, false));
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", portTr("A new issue on Paper Cut's GitHub, filled in. Nothing is sent until you submit "
                                     "it there."));
    }
    if (!sOpenSaid.empty()) {
      ImGui::TextWrapped("%s", sOpenSaid.c_str()); // (a folder or link the system didn't open: where it is)
    }
    sFrames++;
    if (sTestPress != 0 && sFrames >= sTestAt && result == 0) {
      result = sTestPress == 1 && r.have ? 1 : 2;
      sTestPress = 0;
    }
    // the way back (B, Escape, Android's back, the mouse's back): the safe answer, Dismiss
    if (result == 0 && !fieldWas && sFrames > 1 &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
      result = 2;
    }
    if (result != 0 && modal) {
      ImGui::CloseCurrentPopup();
    }
    if (modal) {
      ImGui::EndPopup();
    }
  }
  if (!modal) {
    ImGui::End();
  }
  if (result == 1) {
    const size_t slash = r.path.find_last_of("/\\");
    std::printf("[recovery] continue: %s as file %d\n",
                r.path.substr(slash == std::string::npos ? 0 : slash + 1).c_str(), r.slot + 1);
    std::fflush(stdout);
  } else if (result == 2) {
    std::printf("[recovery] dismissed\n");
    std::fflush(stdout);
  }
  return result;
}
