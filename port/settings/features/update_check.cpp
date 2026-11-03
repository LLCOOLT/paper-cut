// The update check (port/include/port/update_check.h): GitHub's "latest
// release" of kRepository, asked once at start on a thread of its own
// (WinHTTP on Windows; curl on macOS and Linux; phones get their updates
// from their stores, so none there). A release newer than this build's
// PORT_VERSION shows "Paper Cut X is out" on the System tab and the
// launcher's About tab. No answer, an error or a "dev" build: no notice.
#include "port/json.h"
#include "port/settings.h"
#include "port/update_check.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace {

// The repository whose releases are Paper Cut's. While it is private the API
// answers 404, which (like any non-200 or a failed curl) means no notice.
constexpr const char* kRepository = "LLCOOLT/paper-cut";

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
constexpr bool kCanCheck = false;
#else
constexpr bool kCanCheck = true;
#endif

int* sOn;
std::atomic<bool> sFound;
std::string sVersion, sUrl; // set before sFound

// "v1.2.3" / "1.2.3" -> its numbers; false for anything else ("dev")
bool parseVersion(const std::string& text, int out[3]) {
  const char* p = text.c_str();
  if (*p == 'v' || *p == 'V') {
    p++;
  }
  for (int i = 0; i < 3; i++) {
    out[i] = 0;
  }
  for (int i = 0; i < 3; i++) {
    if (*p < '0' || *p > '9') {
      return i > 0;
    }
    out[i] = std::atoi(p);
    while (*p >= '0' && *p <= '9') {
      p++;
    }
    if (*p != '.') {
      return *p == '\0' || *p == '-';
    }
    p++;
  }
  return true;
}

bool newer(const std::string& latest, const char* current) {
  int a[3], b[3];
  if (!parseVersion(latest, a)) {
    return false;
  }
  if (!parseVersion(current, b)) {
    return SDL_getenv("TTYD_UPDATE_REPO") != nullptr; // tests: a dev build sees any release
  }
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) {
      return a[i] > b[i];
    }
  }
  return false;
}

#ifdef _WIN32
bool fetch(const std::string& path, std::vector<uint8_t>& body) {
  bool ok = false;
  HINTERNET session = WinHttpOpen(L"PaperCut", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET connect = session ? WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : nullptr;
  const std::wstring wpath(path.begin(), path.end());
  HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                              : nullptr;
  if (request != nullptr) {
    WinHttpSetTimeouts(request, 10000, 10000, 10000, 10000);
    DWORD status = 0, size = sizeof(status);
    if (WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA,
                           0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
      DWORD got = 0;
      uint8_t chunk[4096];
      while (WinHttpReadData(request, chunk, sizeof(chunk), &got) && got > 0 && body.size() < (1u << 20)) {
        body.insert(body.end(), chunk, chunk + got);
      }
      ok = !body.empty();
    }
  }
  if (request != nullptr) {
    WinHttpCloseHandle(request);
  }
  if (connect != nullptr) {
    WinHttpCloseHandle(connect);
  }
  if (session != nullptr) {
    WinHttpCloseHandle(session);
  }
  return ok;
}
#else
bool fetch(const std::string& path, std::vector<uint8_t>& body) {
  const std::string command = "curl -fsSL --max-time 10 -H 'Accept: application/vnd.github+json' "
                              "'https://api.github.com" + path + "' 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return false;
  }
  uint8_t chunk[4096];
  size_t got;
  while ((got = std::fread(chunk, 1, sizeof(chunk), pipe)) > 0 && body.size() < (1u << 20)) {
    body.insert(body.end(), chunk, chunk + got);
  }
  return pclose(pipe) == 0 && !body.empty();
}
#endif

void check() {
  // the settings are read from settings.txt once Aurora is up: wait for them
  for (int i = 0; i < 300 && portSettingsUserPath()[0] == '\0'; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::seconds(1)); // the folder is known a moment before the file is read
  if (*sOn == 0 || SDL_getenv("TTYD_FAST") != nullptr) { // not in test runs
    return;
  }
  const char* repo = SDL_getenv("TTYD_UPDATE_REPO"); // tests: another repository's releases
  std::vector<uint8_t> body;
  if (!fetch(std::string("/repos/") + (repo != nullptr ? repo : kRepository) + "/releases/latest", body)) {
    return;
  }
  port_json::Json release;
  std::string error;
  if (!port_json::parseJson(body, release, error) || release.type != port_json::Json::Object) {
    return;
  }
  const std::string tag = release.str("tag_name"), url = release.str("html_url");
  if (!newer(tag, PORT_VERSION) || url.rfind("https://", 0) != 0) {
    return;
  }
  sVersion = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
  sUrl = url;
  sFound = true;
}

void systemSection(int) { portUpdateDrawNotice(); }

struct UpdateCheckSettings {
  UpdateCheckSettings() {
    sOn = portSettingsDeclare("update_check", 1, 0, 1);
    if (!kCanCheck) {
      return;
    }
    portSettingsAddToggle("System", "Check for updates at start", "Asks GitHub for the latest release of Paper Cut.",
                          0, "update_check", "update version release new github");
    portSettingsAddSection("System", "New version", nullptr, 0, systemSection, "update version release new");
    std::thread(check).detach();
  }
} sUpdateCheck;

} // namespace

extern "C" void portUpdateDrawNotice(void) {
  if (!sFound) {
    return;
  }
  ImGui::TextColored(ImVec4(0.81f, 0.44f, 0.09f, 1.f), "Paper Cut %s is out", sVersion.c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Open the release page")) {
    SDL_OpenURL(sUrl.c_str());
  }
}
