// The update check (port/include/port/update_check.h): GitHub's "latest
// release" of kRepository, asked once at start on a thread of its own
// (port/include/port/http.h; phones get their updates from their stores, so
// none there). A release newer than this build's PORT_VERSION shows "Paper
// Cut X is out" on the System tab and the launcher's About tab. No answer,
// an error or a "dev" build: no notice.
#include "port/http.h"
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
  std::string why;
  if (!port_http::fetch(std::string("https://api.github.com/repos/") + (repo != nullptr ? repo : kRepository) +
                            "/releases/latest",
                        body, 1u << 20, why, "application/vnd.github+json")) {
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
