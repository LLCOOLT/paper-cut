// Sending a crash report (port/settings/settings_ui.h):
// after a crash, the next start's notice (and the General tab's Crash reports)
// offer it, and nothing leaves the computer without a click.
//
//   Report on GitHub   a new issue on LLCOOLT/paper-cut opened in the browser,
//                      filled in: the version, the system, the graphics
//                      adapter and the report with its last log lines. The
//                      player reads it there and submits it, or doesn't.
//   Save as a zip...   the same in a zip, in a folder the player picks.
//
// Both scrub the player's own names from the text first: the user folder's
// path, the home folder's, the account name, and any C:\Users\<name>\,
// /Users/<name>/ or /home/<name>/ (the build's own source paths too).
#include "port/settings.h"
#include "port/tr.h"

#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <zlib.h>

#include <atomic>
#include <cctype>
#include <cstdio>
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
constexpr size_t kUrlMax = 7000; // GitHub (and browsers) take about 8 KB of address

void replaceAll(std::string& text, const std::string& what, const std::string& with, bool anyCase) {
  if (what.size() < 2) {
    return;
  }
  for (size_t at = 0; at + what.size() <= text.size();) {
    bool same = true;
    for (size_t k = 0; k < what.size() && same; k++) {
      same = anyCase ? std::tolower((unsigned char)text[at + k]) == std::tolower((unsigned char)what[k])
                     : text[at + k] == what[k];
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
      replaceAll(text, name, "<user>", true);
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

std::string systemText() {
  const char* gpu = portCrashGpu();
  std::string out = "Paper Cut: " PORT_VERSION "\nGame: " PORT_GAME_ID "\nSystem: " + osName() + "\nGraphics: " +
                    (gpu != nullptr && gpu[0] != '\0' ? gpu : "?") + "\n";
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

void folderChosen(void*, const char* const* files, int) {
  if (files != nullptr && files[0] != nullptr) {
    std::lock_guard<std::mutex> lock(sLock);
    sFolder = files[0];
    sPending = true;
  }
}

void saveWork() {
  if (!sPending) {
    return;
  }
  std::string folder;
  {
    std::lock_guard<std::mutex> lock(sLock);
    folder = sFolder;
    sPending = false;
  }
  char name[64];
  const std::time_t now = std::time(nullptr);
  const std::tm* t = std::localtime(&now);
  if (t == nullptr || std::strftime(name, sizeof(name), "papercut-crash-%Y%m%d-%H%M%S.zip", t) == 0) {
    std::snprintf(name, sizeof(name), "papercut-crash.zip");
  }
  const std::string path = folder + (folder.empty() || folder.back() == '/' || folder.back() == '\\' ? "" : "/") + name;
  const std::string data = zip({{"crash_report.txt", reportText()}, {"system.txt", portCrashScrub(systemText())}});
  sMessage = SDL_SaveFile(path.c_str(), data.data(), data.size()) ? std::string(portTr("Saved: ")) + name
                                                                  : portTr("The zip couldn't be saved there.");
}

} // namespace

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define CRASH_NO_FOLDERS 1 // (no folder picker on phones: the GitHub page, or Copy)
#endif

void portCrashReportButtons() {
  saveWork();
  if (portCrashReportLast() == nullptr) {
    return;
  }
  if (ImGui::Button(portTrLabel("Report on GitHub##crash_issue"))) {
    SDL_OpenURL(issueUrl().c_str());
    sMessage = portTr("Opened in your browser: read it, add what you were doing, and submit it there if you like.");
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("A new issue on Paper Cut's GitHub, filled in. Nothing is sent until you submit "
                                   "it there."));
  }
#if !defined(CRASH_NO_FOLDERS)
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Save as a zip...##crash_zip"))) {
    SDL_ShowOpenFolderDialog(folderChosen, nullptr, SDL_GetKeyboardFocus(), nullptr, false);
  }
#endif
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", sMessage.empty() ? portTr("It holds the version, the system, the graphics card and where "
                                                     "it crashed; your name is taken out of folder paths.")
                                            : sMessage.c_str());
  ImGui::PopStyleColor();
}

// Tests ($TTYD_CRASH_REPORT_TEST=<file>): the issue's address and the scrubbed report written there
// (main_aurora.c doesn't call it: a test build's crash notice calls it, settings_ui.cpp)
void portCrashReportTest() {
  const char* out = SDL_getenv("TTYD_CRASH_REPORT_TEST");
  static bool sDone;
  if (out == nullptr || sDone || portCrashReportLast() == nullptr) {
    return;
  }
  sDone = true;
  const std::string text = issueUrl() + "\n\n" + reportText() + "\n" + portCrashScrub(systemText());
  SDL_SaveFile(out, text.data(), text.size());
}
