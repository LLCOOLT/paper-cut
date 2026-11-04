// The update check (port/include/port/update_check.h): GitHub's "latest
// release" of kRepository, asked once at start on a thread of its own
// (port/include/port/http.h; phones get their updates from their stores, so
// none there). A release newer than this build's PORT_VERSION shows "Paper
// Cut X is out" on the General tab and the launcher's About tab. No answer,
// an error or a "dev" build: no notice.
//
// Download and install (docs/UPDATES.md): the release's zip for this
// platform, and nothing else: only Paper Cut's own build, only from
// kRepository's releases (an asset address under its releases/download/),
// never anything made from the game. It's checked against the SHA-256 the
// release lists (its notes' sums or its SHA256SUMS*.txt, tools/port/
// release.py), unpacked beside the install and put in place at the next
// start (port/include/port/update_install.h). The Flatpak and the AppImage
// are updated by their own tools: a note there.
//
// Tests: $TTYD_UPDATE_API, a release's JSON on this computer
// (http://127.0.0.1:<port>/..., GitHub's shape) with its zip and sums there
// too, checked even with $TTYD_FAST; $TTYD_UPDATE_INSTALL=1 presses Download
// and install once a release is found.
#include "port/http.h"
#include "port/json.h"
#include "port/lan_http.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/update_check.h"
#include "port/update_install.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
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

constexpr uint64_t kMaxZip = 200ull << 20;

int* sOn;
std::atomic<bool> sFound;
std::string sVersion, sUrl;         // set before sFound
std::string sAssetUrl, sSumsBody;   // the zip's address, the notes (sums in them); set before sFound
std::vector<std::string> sSumsUrls; // SHA256SUMS*.txt assets

// --- the download ---
enum State { Idle, Downloading, Checking, Ready, Failed };
std::atomic<int> sState{Idle};
port_http::Progress sProgress;
std::mutex sLock;
std::string sMessage, sDetail, sHash; // (sLock): the message in English (translated when shown), what it was

// A test release on this computer ($TTYD_UPDATE_API): http://127.0.0.1 or localhost only
bool localTest(const std::string& url) {
  std::string host, path;
  int port = 0;
  return SDL_getenv("TTYD_UPDATE_API") != nullptr && port_lan::splitUrl(url, host, port, path) &&
         (host == "127.0.0.1" || host == "localhost");
}

// An address the download may come from: kRepository's release files (GitHub
// sends them on to its storage, https only), or a local test release
bool allowedDownload(const std::string& url) {
  const std::string releases = std::string("https://github.com/") + kRepository + "/releases/download/";
  return url.rfind(releases, 0) == 0 || localTest(url);
}

bool getAny(const std::string& url, const port_http::Sink& sink, uint64_t max, port_http::Progress* progress,
            std::string& why) {
  if (localTest(url)) {
    return port_lan::get(url, sink, max, progress, why);
  }
  return port_http::get(url, sink, max, progress, why);
}

bool fetchAny(const std::string& url, std::vector<uint8_t>& out, uint64_t max, std::string& why) {
  out.clear();
  return getAny(
      url,
      [&](const uint8_t* d, size_t n) {
        out.insert(out.end(), d, d + n);
        return true;
      },
      max, nullptr, why);
}

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
    // tests: a dev build sees any release
    return SDL_getenv("TTYD_UPDATE_REPO") != nullptr || SDL_getenv("TTYD_UPDATE_API") != nullptr;
  }
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) {
      return a[i] > b[i];
    }
  }
  return false;
}

// The sum listed for name in text ("<64 hex>  <name>" lines, sha256sum's), or empty
std::string sumIn(const std::string& text, const std::string& name) {
  size_t at = 0;
  while (at < text.size()) {
    const size_t eol = std::min(text.find('\n', at), text.size());
    std::string line = text.substr(at, eol - at);
    at = eol + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (line.size() < 66 || line.find_first_not_of("0123456789abcdefABCDEF") != 64) {
      continue;
    }
    size_t n = 64;
    while (n < line.size() && (line[n] == ' ' || line[n] == '*')) {
      n++;
    }
    if (line.substr(n) == name) {
      std::string sum = line.substr(0, 64);
      for (char& c : sum) {
        c = char(c >= 'A' && c <= 'F' ? c + 32 : c);
      }
      return sum;
    }
  }
  return std::string();
}

void check() {
  // the settings are read from settings.txt once Aurora is up: wait for them
  for (int i = 0; i < 300 && portSettingsUserPath()[0] == '\0'; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::this_thread::sleep_for(std::chrono::seconds(1)); // the folder is known a moment before the file is read
  const char* api = SDL_getenv("TTYD_UPDATE_API"); // tests: a release on this computer
  if (*sOn == 0 || (SDL_getenv("TTYD_FAST") != nullptr && api == nullptr)) { // not in test runs
    return;
  }
  const char* repo = SDL_getenv("TTYD_UPDATE_REPO"); // tests: another repository's releases
  const std::string url = api != nullptr ? std::string(api)
                                         : std::string("https://api.github.com/repos/") +
                                               (repo != nullptr ? repo : kRepository) + "/releases/latest";
  if (api != nullptr && !localTest(url)) {
    return; // (the test's release is on this computer, or nowhere)
  }
  std::vector<uint8_t> body;
  std::string why;
  if (!fetchAny(url, body, 1u << 20, why)) {
    return;
  }
  port_json::Json release;
  std::string error;
  if (!port_json::parseJson(body, release, error) || release.type != port_json::Json::Object) {
    return;
  }
  const std::string tag = release.str("tag_name"), page = release.str("html_url");
  if (!newer(tag, PORT_VERSION) || page.rfind("https://", 0) != 0) {
    return;
  }
  sVersion = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
  sUrl = page;
  sSumsBody = release.str("body");
  // this platform's zip, Paper Cut's own build, from Paper Cut's releases
  // (another repository's, $TTYD_UPDATE_REPO, is a notice only)
  const std::string want = port_update::assetName(sVersion);
  const port_json::Json* assets = release.get("assets");
  if (assets != nullptr && repo == nullptr) {
    for (const port_json::Json& a : assets->items) {
      const std::string name = a.str("name"), link = a.str("browser_download_url");
      if (!allowedDownload(link)) {
        continue;
      }
      if (name == want) {
        sAssetUrl = link;
      } else if (name.rfind("SHA256SUMS", 0) == 0 && name.size() > 4 &&
                 name.compare(name.size() - 4, 4, ".txt") == 0) {
        sSumsUrls.push_back(link);
      }
    }
  }
  std::printf("[update] Paper Cut %s is out%s\n", sVersion.c_str(),
              sAssetUrl.empty() ? "" : (" (" + want + " there)").c_str());
  std::fflush(stdout);
  sFound = true;
}

void setMessage(int state, const std::string& text, const std::string& hash = std::string(),
                const std::string& detail = std::string()) {
  {
    std::lock_guard<std::mutex> hold(sLock);
    sMessage = text;
    sDetail = detail;
    sHash = hash;
  }
  std::printf("[update] %s %s%s%s\n", text.c_str(), detail.c_str(), hash.empty() ? "" : " SHA-256 ", hash.c_str());
  std::fflush(stdout);
  sState = state;
}

void download() {
  const std::string dir = port_update::stageDir(), zip = dir + "/download.zip";
  SDL_CreateDirectory(dir.c_str());
  SDL_IOStream* out = SDL_IOFromFile(zip.c_str(), "wb");
  if (out == nullptr) {
    setMessage(Failed, "Can't write beside the install: download it from the release page.");
    return;
  }
  std::string why;
  sProgress.done = 0;
  sProgress.total = 0;
  sProgress.cancel = false;
  const bool got = getAny(
      sAssetUrl, [&](const uint8_t* d, size_t n) { return SDL_WriteIO(out, d, n) == n; }, kMaxZip, &sProgress, why);
  SDL_CloseIO(out);
  if (!got) {
    SDL_RemovePath(zip.c_str());
    setMessage(Failed, "The download failed:", std::string(), why);
    return;
  }
  sState = Checking;
  const std::string name = port_update::assetName(sVersion), hash = port_update::sha256File(zip);
  // the sum the release lists: in its notes, else in its SHA256SUMS*.txt
  std::string expected = sumIn(sSumsBody, name);
  for (size_t i = 0; expected.empty() && i < sSumsUrls.size(); i++) {
    std::vector<uint8_t> text;
    std::string ignored;
    if (fetchAny(sSumsUrls[i], text, 1u << 20, ignored)) {
      expected = sumIn(std::string(text.begin(), text.end()), name);
    }
  }
  if (expected.empty()) {
    SDL_RemovePath(zip.c_str());
    setMessage(Failed, "The release lists no checksum for it, so it wasn't installed.", hash);
    return;
  }
  if (hash != expected) {
    SDL_RemovePath(zip.c_str());
    setMessage(Failed, "The download doesn't match the release's checksum, so it wasn't installed.", hash);
    return;
  }
  const bool ok = port_update::stage(zip, sVersion, why);
  SDL_RemovePath(zip.c_str());
  if (!ok) {
    setMessage(Failed, "It couldn't be unpacked:", hash, why);
    return;
  }
  setMessage(Ready, "The new version is installed at the next start.", hash);
}

void startDownload() {
  if (sState == Downloading || sState == Checking || sAssetUrl.empty()) {
    return;
  }
  sState = Downloading;
  std::thread(download).detach();
}

void systemSection(int) {
  if (const char* applied = SDL_getenv("TTYD_UPDATE_APPLIED")) {
    ImGui::TextDisabled(portTr("Updated to Paper Cut %s."), applied);
  }
  portUpdateDrawNotice();
  if (!sFound) {
    return;
  }
  const port_update::Mode mode = port_update::mode();
  if (mode == port_update::Mode::Flatpak) {
    ImGui::TextDisabled("%s", portTr("Flathub updates this copy: your software centre, or flatpak update."));
    return;
  }
  if (mode == port_update::Mode::AppImage) {
    ImGui::TextDisabled("%s", portTr("Update the AppImage with AppImageUpdate, or download the new one."));
    return;
  }
  if (mode != port_update::Mode::Install || sAssetUrl.empty()) {
    return; // (the release page's link is all there is)
  }
  const int state = sState;
  if (state == Idle || state == Failed) {
    if (ImGui::SmallButton(portTrLabel("Download and install"))) {
      startDownload();
    }
  } else if (state == Downloading) {
    const uint64_t done = sProgress.done, total = sProgress.total;
    char text[96];
    if (total > 0) {
      std::snprintf(text, sizeof(text), "%.1f / %.1f MB", double(done) / 1048576.0, double(total) / 1048576.0);
    } else {
      std::snprintf(text, sizeof(text), "%.1f MB", double(done) / 1048576.0);
    }
    ImGui::ProgressBar(total > 0 ? float(double(done) / double(total)) : 0.f, ImVec2(240.f, 0.f), text);
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Cancel##update"))) {
      sProgress.cancel = true;
    }
  } else if (state == Checking) {
    ImGui::TextDisabled("%s", portTr("Checking the download..."));
  }
  std::lock_guard<std::mutex> hold(sLock);
  if ((state == Ready || state == Failed) && !sMessage.empty()) {
    ImGui::TextWrapped("%s %s", portTr(sMessage.c_str()), sDetail.c_str());
    if (!sHash.empty()) {
      ImGui::TextDisabled("SHA-256 %s", sHash.c_str());
    }
    if (state == Ready && portRestartSupported() && ImGui::SmallButton(portTrLabel("Restart to update"))) {
      portRestartRequest(1);
    }
  }
}

// $TTYD_UPDATE_INSTALL: the General tab's button pressed without the page (tests)
void testOverlay(int) {
  static bool sStarted;
  if (!sStarted && sFound) {
    sStarted = true;
    std::string why;
    if (port_update::mode(&why) != port_update::Mode::Install) {
      std::printf("[update] not installed from here: %s\n", why.c_str());
    } else if (sAssetUrl.empty()) {
      std::printf("[update] the release has no %s\n", port_update::assetName(sVersion).c_str());
    } else {
      startDownload();
    }
    std::fflush(stdout);
  }
}

struct UpdateCheckSettings {
  UpdateCheckSettings() {
    sOn = portSettingsDeclare("update_check", 1, 0, 1);
    if (!kCanCheck) {
      return;
    }
    portSettingsAddToggle("General", "Check for updates at start", "Asks GitHub for the latest release of Paper Cut.",
                          0, "update_check", "update version release new github");
    portSettingsAddSection("General", "New version", nullptr, 0, systemSection,
                           "update version release new download install");
    if (SDL_getenv("TTYD_UPDATE_INSTALL") != nullptr) {
      portSettingsAddOverlay(testOverlay, nullptr);
    }
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
  if (ImGui::SmallButton(portTrLabel("Open the release page"))) {
    SDL_OpenURL(sUrl.c_str());
  }
}
