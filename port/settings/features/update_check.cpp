// The update check (port/include/port/update_check.h): GitHub's "latest
// release" of kRepository, asked once at start on a thread of its own
// (port/include/port/http.h), on computers and phones alike. A release newer
// than this build's PORT_VERSION shows "Paper Cut X is out" on the General
// tab and the launcher's About tab. No answer, an error or a "dev" build: no
// notice.
//
// Phones (one app per game version, PORT_GAME_ID): Android downloads this
// app's APK from the release to its cache, checks it the same way, and hands
// it to Android's own installer, which asks (port/include/port/android_pc.h;
// never a silent install, and Install updates automatically only downloads
// and checks it there). iOS can't install an app from itself: the notice
// offers the release page and Paper Cut's AltStore / SideStore source
// (kSourceFile, written by tools/port/release.py with each release).
//
// Download and install (docs/UPDATES.md): the release's zip for this
// platform, and nothing else: only Paper Cut's own build, only from
// kRepository's releases (an asset address under its releases/download/),
// never anything made from the game. It's checked against the SHA-256 the
// release lists (its notes' sums or its SHA256SUMS*.txt, tools/port/
// release.py), unpacked beside the install and put in place at the next
// start (port/include/port/update_install.h). The Flatpak and the AppImage
// are updated by their own tools: a note there.
// The limit (docs/UPDATES.md): the sum is the release's own, so it catches a
// damaged download, not a release made by someone holding the account. No
// signing key exists for releases yet; until one does (an offline ed25519
// key, its public half in the build, SHA256SUMS.txt's signature checked
// before staging), automatic installs stay off by default.
//
// Install updates automatically (General, off by default): where Download and
// install is offered, a newer release found at start is downloaded quietly
// in the background, checked and staged the same way, and put in place at
// the next start; never a restart mid-game. Not while a
// replay runs (a download under way stops, and starts again after). The next
// start's "what's new" window says it was updated (whats_new_aurora.c).
//
// Tests: $TTYD_UPDATE_API, a release's JSON on this computer
// (http://127.0.0.1:<port>/..., GitHub's shape) with its zip and sums there
// too, checked even with $TTYD_FAST; $TTYD_UPDATE_INSTALL=1 presses Download
// and install once a release is found.
#include "port/detach.h"
#include "port/offscreen.h"
#include "port/http.h"
#include "port/json.h"
#include "port/lan_http.h"
#include "port/replay.h"
#include "port/pause_forms.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/statesync.h"
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
#ifdef __ANDROID__
#include "port/android_pc.h"
#endif

namespace {

// The repository whose releases are Paper Cut's. While it is private the API
// answers 404, which (like any non-200 or a failed curl) means no notice.
constexpr const char* kRepository = "LLCOOLT/paper-cut";
// The AltStore / SideStore source every release carries (tools/port/release.py):
// releases/latest/download/ is always the newest release's
constexpr const char* kSourceFile = "altstore-source.json";

constexpr uint64_t kMaxZip = 200ull << 20;

int* sOn;
int* sAuto; // install updates automatically
std::atomic<bool> sFound;
std::atomic<bool> sAutoStarted{false}, sPaused{false}; // the automatic download: started; stopped for online or a replay
std::string sVersion, sUrl;         // set before sFound
std::string sAssetUrl, sSumsBody;   // the zip's address, the notes (sums in them); set before sFound
std::vector<std::string> sSumsUrls; // SHA256SUMS*.txt assets

// --- the download ---
enum State { Idle, Downloading, Checking, Ready, Failed };
std::atomic<int> sState{Idle};
port_http::Progress sProgress;
std::mutex sLock;
std::string sMessage, sDetail, sHash; // (sLock): the message in English (translated when shown), what it was

// --- phones ---
// Android: the checked APK (sLock), and whether Android's installer has it
std::string sApk;
std::atomic<bool> sInstalling{false};
uint64_t sPolled; // when installResult was last asked (the drawing thread's)

// the messages (messageConstant keeps them by address)
const char* const kNoApkFolder = "Can't save the download: get it from the release page.";
const char* const kApkReady = "Downloaded and checked. Android asks before it installs; the game closes while it "
                              "updates.";
const char* const kApkAsking = "Android is asking: tap Install there. The game closes while it updates.";
const char* const kApkAllow = "Allow Paper Cut in the screen that opened (Install unknown apps), then come back "
                              "and press Install.";
const char* const kApkNoInstaller = "Android's installer couldn't be opened: get it from the release page.";
const char* const kApkOtherKey = "Android can't install it over this copy: they're signed with different keys. "
                                 "See the release page.";
const char* const kApkFailed = "Android couldn't install it:";

// Android's cache folder for the APK, or empty
std::string apkFolder() {
#ifdef __ANDROID__
  const char* cache = SDL_GetAndroidCachePath();
  return cache != nullptr ? std::string(cache) + "/update" : std::string();
#else
  return std::string();
#endif
}

int installApk(const std::string& path) {
#ifdef __ANDROID__
  return portAndroidInstallApk(path.c_str());
#else
  (void)path;
  return -1;
#endif
}

int installResult(std::string& message) {
#ifdef __ANDROID__
  char text[256];
  const int code = portAndroidInstallResult(text, sizeof(text));
  message = text;
  return code;
#else
  message.clear();
  return 0;
#endif
}

// iOS: Paper Cut's source added in AltStore or SideStore ("altstore", "sidestore")
std::string sourceUrl() {
  return std::string("https://github.com/") + kRepository + "/releases/latest/download/" + kSourceFile;
}

std::string sSourceError;  // the store couldn't be opened: the source's address, to add by hand
std::string sReleaseError; // the release page couldn't be opened: its address

// false (and sSourceError) when the store couldn't be opened (not installed: Info.plist.in's
// LSApplicationQueriesSchemes lists the schemes; SDL_OpenURL asks canOpenURL first)
bool openSource(const char* scheme, const char* store) {
  std::string encoded;
  for (const unsigned char c : sourceUrl()) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      encoded += char(c);
    } else {
      char hex[4];
      std::snprintf(hex, sizeof(hex), "%%%02X", c);
      encoded += hex;
    }
  }
  if (portOpenUrl((std::string(scheme) + "://source?url=" + encoded).c_str())) {
    sSourceError.clear();
    return true;
  }
  sSourceError = portTrf("Couldn't open %s (is it installed?). Add this source in it by hand: %s", store,
                         sourceUrl().c_str());
  return false;
}

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
  // Android: the APK in the app's cache (an earlier one there removed); elsewhere the zip beside the install
  const bool android = port_update::mode() == port_update::Mode::Android;
  const std::string dir = android ? apkFolder() : port_update::stageDir();
  const std::string zip = dir + "/" + (android ? port_update::assetName(sVersion) : std::string("download.zip"));
  if (android && !dir.empty()) {
    std::vector<std::string> old;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
          static_cast<std::vector<std::string>*>(user)->push_back(name);
          return SDL_ENUM_CONTINUE;
        },
        &old);
    for (const std::string& name : old) {
      SDL_RemovePath((dir + "/" + name).c_str());
    }
  }
  SDL_CreateDirectory(dir.c_str());
  SDL_IOStream* out = dir.empty() ? nullptr : SDL_IOFromFile(zip.c_str(), "wb");
  if (out == nullptr) {
    setMessage(Failed, android ? kNoApkFolder : "Can't write beside the install: download it from the release page.");
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
    if (sPaused && sProgress.cancel) { // stopped for online play or a replay: again once it's over (autoTick)
      std::printf("[update] the download waits: online play or a replay\n");
      std::fflush(stdout);
      sState = Idle;
      return;
    }
    sPaused = false; // (a failure of its own: shown, not started again unseen)
    setMessage(Failed, "The download failed:", std::string(), why);
    return;
  }
  sPaused = false; // (a pause asked as it finished anyway)
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
  if (android) { // checked: Android's installer asks before it goes in (installNow)
    {
      std::lock_guard<std::mutex> hold(sLock);
      sApk = zip;
    }
    setMessage(Ready, kApkReady, hash);
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
  portDetach(download);
}

// Android: the checked APK to Android's installer, which asks the player
void installNow() {
  std::string apk, hash;
  {
    std::lock_guard<std::mutex> hold(sLock);
    apk = sApk;
    hash = sHash;
  }
  if (sState != Ready || apk.empty()) {
    return;
  }
  const int result = installApk(apk);
  sInstalling = result == 0;
  setMessage(Ready, result == 0 ? kApkAsking : result == 1 ? kApkAllow : kApkNoInstaller, hash);
}

// how Android's install ended, while it's under way (a few times a second, from the page or the Start menu)
void pollInstall() {
  if (!sInstalling || SDL_GetTicks() - sPolled < 500) {
    return;
  }
  sPolled = SDL_GetTicks();
  std::string why, hash;
  const int code = installResult(why);
  if (code == 0) {
    return;
  }
  sInstalling = false;
  {
    std::lock_guard<std::mutex> hold(sLock);
    hash = sHash;
  }
  setMessage(Ready, code == 2 ? kApkOtherKey : code == 3 ? kApkReady : kApkFailed, hash,
             code == 4 ? why : std::string());
}

void sourceButtons() {
  ImGui::TextDisabled("%s", portTr("AltStore and SideStore update Paper Cut from its source: add it once."));
  if (ImGui::SmallButton(portTrLabel("Add to AltStore"))) {
    openSource("altstore", "AltStore");
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(portTrLabel("Add to SideStore"))) {
    openSource("sidestore", "SideStore");
  }
  if (!sSourceError.empty()) {
    ImGui::TextWrapped("%s", sSourceError.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel("Copy##source_url"))) {
      SDL_SetClipboardText(sourceUrl().c_str());
    }
  }
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
  const bool android = mode == port_update::Mode::Android;
  if ((mode != port_update::Mode::Install && !android) || sAssetUrl.empty()) {
    return; // (the release page's link is all there is; on iOS the source's buttons with it)
  }
  pollInstall();
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
  bool install = false;
  {
    std::lock_guard<std::mutex> hold(sLock);
    if ((state == Ready || state == Failed) && !sMessage.empty()) {
      ImGui::TextWrapped("%s %s", portTr(sMessage.c_str()), sDetail.c_str());
      if (!sHash.empty()) {
        ImGui::TextDisabled("SHA-256 %s", sHash.c_str());
      }
      if (state == Ready && android) {
        install = ImGui::SmallButton(portTrLabel("Install##update")); // (again after a closed dialog)
      } else if (state == Ready && portRestartSupported() && ImGui::SmallButton(portTrLabel("Restart to update"))) {
        portRestartRequest(1);
      }
    }
  }
  if (install) {
    installNow(); // (outside the lock: it sets the message)
  }
}

// Install updates automatically: each presented frame, the download started
// once a release is found (where the button would be), stopped while online
// co-op or a replay runs, and started again after; quiet unless it fails
// (Android: downloaded and checked only; Android's installer asks once Install is pressed)
bool autoAllowed() {
  const port_update::Mode mode = port_update::mode();
  return mode == port_update::Mode::Install || mode == port_update::Mode::Android;
}

int autoShown() {
  return autoAllowed() ? 1 : 0;
}

void autoTick(int) {
  if (sAuto == nullptr || !*sAuto || !sFound || sAssetUrl.empty() || !autoAllowed()) {
    return;
  }
  const bool busy = portReplayActive() || portStateSyncOn(); // (online: the relay connection first, as in 0.4.1)
  const int state = sState;
  if (busy) {
    if (state == Downloading && sAutoStarted) {
      sPaused = true;
      sProgress.cancel = true;
    }
    return;
  }
  if (state == Idle && (!sAutoStarted || sPaused)) {
    sPaused = false;
    sAutoStarted = true;
    std::printf("[update] installing Paper Cut %s automatically: downloading\n", sVersion.c_str());
    std::fflush(stdout);
    startDownload();
  }
}

// The Start menu's form (port/include/port/pause_forms.h): the new version, and its download as on the page
enum { kNotice = 1, kRelease, kInstall, kCancelDownload, kRestartNow, kMessage, kApkInstall, kAltStore, kSideStore };

// the download's message as the constant it was made from (translations are kept by address)
const char* messageConstant() {
  static const char* const kKnown[] = {
      "Can't write beside the install: download it from the release page.",
      "The download failed:",
      "The release lists no checksum for it, so it wasn't installed.",
      "The download doesn't match the release's checksum, so it wasn't installed.",
      "It couldn't be unpacked:",
      "The new version is installed at the next start.",
      kNoApkFolder, kApkReady, kApkAsking, kApkAllow, kApkNoInstaller, kApkOtherKey, kApkFailed,
  };
  std::lock_guard<std::mutex> hold(sLock);
  for (const char* k : kKnown) {
    if (sMessage == k) {
      return k;
    }
  }
  return nullptr;
}

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (const char* applied = SDL_getenv("TTYD_UPDATE_APPLIED"); applied != nullptr && n < max) {
    PortPauseRow r = portPauseRow("Updated to Paper Cut", nullptr, nullptr, kNotice, PORT_PAUSE_ROW_DIM);
    std::snprintf(r.value, sizeof(r.value), "%s", applied);
    out[n++] = r;
  }
  if (!sFound) {
    if (n < max) {
      out[n++] = portPauseRow("No new version found", "Checked at start, when Check for updates at start is on.",
                              nullptr, kNotice, PORT_PAUSE_ROW_DIM);
    }
    return n;
  }
  PortPauseRow out1 = portPauseRow("New version", "A newer Paper Cut is out. {A}: open its release page.",
                                   nullptr, kRelease);
  std::snprintf(out1.value, sizeof(out1.value), "Paper Cut %s", sVersion.c_str());
  out[n++] = out1;
  const port_update::Mode mode = port_update::mode();
  if (mode == port_update::Mode::Flatpak) {
    out[n++] = portPauseRow("Flathub updates this copy", "Your software centre, or flatpak update.", nullptr, kNotice,
                            PORT_PAUSE_ROW_DIM);
    return n;
  }
  if (mode == port_update::Mode::AppImage) {
    out[n++] = portPauseRow("Update the AppImage", "With AppImageUpdate, or download the new one.", nullptr, kNotice,
                            PORT_PAUSE_ROW_DIM);
    return n;
  }
  if (mode == port_update::Mode::AltStore) {
    if (n < max - 1) {
      out[n++] = portPauseRow("Add to AltStore", "{A}: add Paper Cut's source, so AltStore offers each new version.",
                              nullptr, kAltStore);
      out[n++] = portPauseRow("Add to SideStore", "{A}: add Paper Cut's source, so SideStore offers each new version.",
                              nullptr, kSideStore);
    }
    return n;
  }
  const bool android = mode == port_update::Mode::Android;
  if ((mode != port_update::Mode::Install && !android) || sAssetUrl.empty()) {
    return n;
  }
  pollInstall();
  const int state = sState;
  if (state == Idle || state == Failed) {
    out[n++] = android ? portPauseRow("Download and install",
                                      "Only Paper Cut's own app for this version, from its releases, checked against "
                                      "the release's checksum; then Android asks before it installs.",
                                      nullptr, kInstall, PORT_PAUSE_ROW_CONFIRM)
                       : portPauseRow("Download and install",
                                      "Only Paper Cut's own build, from its releases, checked against the release's "
                                      "checksum; put in place at the next start.",
                                      nullptr, kInstall, PORT_PAUSE_ROW_CONFIRM);
  } else if (state == Downloading) {
    PortPauseRow r = portPauseRow("Downloading", "{A}: stop the download.", nullptr, kCancelDownload);
    const uint64_t done = sProgress.done, total = sProgress.total;
    if (total > 0) {
      std::snprintf(r.value, sizeof(r.value), "%.1f / %.1f MB", double(done) / 1048576.0, double(total) / 1048576.0);
    } else {
      std::snprintf(r.value, sizeof(r.value), "%.1f MB", double(done) / 1048576.0);
    }
    out[n++] = r;
  } else if (state == Checking) {
    out[n++] = portPauseRow("Checking the download...", nullptr, nullptr, kNotice, PORT_PAUSE_ROW_DIM);
  }
  if ((state == Ready || state == Failed) && n < max - 1) {
    if (const char* k = messageConstant(); k != nullptr) {
      out[n++] = portPauseRow(state != Ready ? "Not installed" : android ? "Ready to install" : "Installed", k,
                              nullptr, kMessage, PORT_PAUSE_ROW_DIM);
    }
    if (state == Ready && android) { // (again after a closed dialog, or one that never came)
      out[n++] = portPauseRow("Install", "{A}: Android asks before it installs. The game closes while it updates.",
                              nullptr, kApkInstall);
    } else if (state == Ready && portRestartSupported()) {
      out[n++] = portPauseRow("Restart to update", "Press {A} twice: the game starts again on the new version.", nullptr,
                              kRestartNow, PORT_PAUSE_ROW_CONFIRM);
    }
  }
  return n;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  switch (id) {
  case kRelease:
    return portOpenUrl(sUrl.c_str()) ? nullptr : "Couldn't open the release page: it is on Paper Cut's GitHub.";
  case kInstall:
    startDownload();
    return nullptr;
  case kCancelDownload:
    sProgress.cancel = true;
    return nullptr;
  case kRestartNow:
    portRestartRequest(1);
    return nullptr;
  case kMessage:
    return messageConstant();
  case kApkInstall:
    installNow();
    return nullptr;
  case kAltStore: // (the source's address: on the settings page's General tab, with Copy)
    return openSource("altstore", "AltStore") ? nullptr : "Couldn't open AltStore: is it installed?";
  case kSideStore:
    return openSource("sidestore", "SideStore") ? nullptr : "Couldn't open SideStore: is it installed?";
  default:
    return nullptr;
  }
}

const PortPauseForm kForm = {"General", "New version", formRows, formAct, PORT_PAUSE_FORM_OWN};

// $TTYD_UPDATE_INSTALL: the General tab's button pressed without the page (tests)
void testOverlay(int) {
  static bool sStarted;
  if (!sStarted && sFound) {
    sStarted = true;
    std::string why;
    const port_update::Mode mode = port_update::mode(&why);
    if (mode != port_update::Mode::Install && mode != port_update::Mode::Android) {
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
    portSettingsAddToggle("General", "Check for updates at start", "Asks GitHub for the latest release of Paper Cut.",
                          0, "update_check", "update version release new github");
    sAuto = portSettingsDeclare("update_auto", 0, 0, 1);
#ifdef __ANDROID__
    // (Android: downloaded and checked; installing is the player's, in Android's own dialog)
    portSettingsAddToggle("General", "Install updates automatically",
                          "A new version found at start is downloaded in the background and checked against the "
                          "release's checksum; then Android asks before it installs. Never during online play or a "
                          "replay.",
                          0, "update_auto", "update automatic auto install download background");
#else
    portSettingsAddToggle("General", "Install updates automatically",
                          "A new version found at start is downloaded in the background, checked against the "
                          "release's checksum, and put in place the next time the game starts. Never during online "
                          "play or a replay.",
                          0, "update_auto", "update automatic auto install download background");
#endif
    portSettingsSetShown("update_auto", autoShown); // (where Download and install is: not iOS, Flatpak, AppImage)
    portSettingsAddOverlay(autoTick, nullptr);
    portSettingsAddSection("General", "New version", nullptr, 0, systemSection,
                           "update version release new download install");
    portPauseAddForm(&kForm);
    if (SDL_getenv("TTYD_UPDATE_INSTALL") != nullptr) {
      portSettingsAddOverlay(testOverlay, nullptr);
    }
#ifndef __EMSCRIPTEN__ // (the browser: no threads, and the page is always the latest)
    portDetach(check);
#endif
  }
} sUpdateCheck;

} // namespace

extern "C" void portUpdateDrawNotice(void) {
  if (!sFound) {
    return;
  }
  ImGui::TextColored(ImVec4(0.81f, 0.44f, 0.09f, 1.f), "%s", portTrf("Paper Cut %s is out", sVersion.c_str()));
  ImGui::SameLine();
  if (ImGui::SmallButton(portTrLabel("Open the release page"))) {
    char why[1024];
    sReleaseError = portOpenUrl(sUrl.c_str()) ? "" : portOpenError(why, sizeof(why), sUrl.c_str(), false);
  }
  if (!sReleaseError.empty()) {
    ImGui::TextWrapped("%s", sReleaseError.c_str());
  }
  if (port_update::mode() == port_update::Mode::AltStore) {
    sourceButtons(); // iOS: AltStore or SideStore installs it
  }
}
