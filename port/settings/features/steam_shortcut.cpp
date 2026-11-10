// Add to Steam (Windows and Linux, the Steam Deck's desktop): Paper Cut in the
// Steam library as a non-Steam game, with artwork and a controller set-up.
//
// Steam keeps its non-Steam games in <Steam>/userdata/<account>/config/
// shortcuts.vdf, a binary key-value file (0x00 a map, 0x01 a string, 0x02 an
// int32, 0x07 a u64, 0x08 a map's end). This reads it whole, adds or updates
// the entry whose Exe is this program (or named Paper Cut), and writes it back
// through a temporary file, a dated copy of the old one kept beside it first.
// Steam reads the file at start and writes it at exit, so nothing is written
// while Steam runs: the player is asked to close it.
//
// The entry's appid (the old crc32(exe + name) | 0x80000000 scheme) names the
// artwork in <account>/config/grid: <appid>p.png (600x900 cover), <appid>.png
// (920x430), <appid>_hero.png (1920x620), <appid>_logo.png and _icon.png, all
// made here from Paper Cut's own logo (port/assets/icon, no disc pixels). The
// controller set-up: Steam Input's own "gamepad with mouse trackpad" template
// for the Steam Deck (the right pad works the settings page and the launcher
// like a mouse) and the plain gamepad for other controllers, through
// <Steam>/steamapps/common/Steam Controller Configs/<account>/config/
// configset_<controller>.vdf, unless the player has one for Paper Cut already.
#include "port/settings.h"
#include "port/tr.h"
#include "../file_util.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

extern "C" {
extern const unsigned char portLogo512[];
extern const unsigned int portLogo512_size;
}

#if (defined(_WIN32) || defined(__linux__)) && !defined(__ANDROID__)
#define PORT_ADD_TO_STEAM 1
#endif

#ifdef PORT_ADD_TO_STEAM
namespace {

constexpr const char* kAppName = "Paper Cut";

// --- binary VDF ---

struct Node {
  uint8_t type = 0; // 0 map, 1 string, 2 int32, 7 u64
  std::string key, str;
  uint32_t i32 = 0;
  uint64_t u64 = 0;
  std::vector<Node> kids;
};

struct Reader {
  const std::vector<uint8_t>& b;
  size_t at = 0;
  bool ok = true;
  std::string cstr() {
    std::string s;
    while (at < b.size() && b[at] != 0) {
      s += static_cast<char>(b[at++]);
    }
    if (at >= b.size()) {
      ok = false;
    }
    at++;
    return s;
  }
  // a map's children, up to its 0x08
  void map(Node& into, int depth) {
    while (ok) {
      if (at >= b.size() || depth > 32) {
        ok = false;
        return;
      }
      const uint8_t t = b[at++];
      if (t == 8) {
        return;
      }
      Node n;
      n.type = t;
      n.key = cstr();
      if (t == 0) {
        map(n, depth + 1);
      } else if (t == 1) {
        n.str = cstr();
      } else if (t == 2 && at + 4 <= b.size()) {
        std::memcpy(&n.i32, &b[at], 4);
        at += 4;
      } else if (t == 7 && at + 8 <= b.size()) {
        std::memcpy(&n.u64, &b[at], 8);
        at += 8;
      } else {
        ok = false; // a type this doesn't know: the file is left alone
        return;
      }
      into.kids.push_back(std::move(n));
    }
  }
};

void put(std::vector<uint8_t>& o, const Node& n) {
  o.push_back(n.type);
  o.insert(o.end(), n.key.begin(), n.key.end());
  o.push_back(0);
  if (n.type == 0) {
    for (const Node& k : n.kids) {
      put(o, k);
    }
    o.push_back(8);
  } else if (n.type == 1) {
    o.insert(o.end(), n.str.begin(), n.str.end());
    o.push_back(0);
  } else if (n.type == 2) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&n.i32);
    o.insert(o.end(), p, p + 4);
  } else {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&n.u64);
    o.insert(o.end(), p, p + 8);
  }
}

Node* child(Node& n, const char* key) {
  for (Node& k : n.kids) {
    if (SDL_strcasecmp(k.key.c_str(), key) == 0) {
      return &k;
    }
  }
  return nullptr;
}

void setStr(Node& n, const char* key, const std::string& v) {
  Node* k = child(n, key);
  if (k == nullptr) {
    n.kids.push_back(Node{});
    k = &n.kids.back();
    k->key = key;
  }
  k->type = 1;
  k->str = v;
}

void setInt(Node& n, const char* key, uint32_t v) {
  Node* k = child(n, key);
  if (k == nullptr) {
    n.kids.push_back(Node{});
    k = &n.kids.back();
    k->key = key;
  }
  k->type = 2;
  k->i32 = v;
}

// --- where things are ---

std::string dirOf(const std::string& path);

// what the player starts: the launcher beside this program (Paper Cut.exe,
// papercut), else this program
std::string exePath();
std::string startPath() {
  const std::string self = exePath();
  if (self.empty() || SDL_getenv("APPIMAGE") != nullptr) {
    return self;
  }
#ifdef _WIN32
  const std::string launcher = dirOf(self) + "\\Paper Cut.exe";
#else
  const std::string launcher = dirOf(self) + "/papercut";
#endif
  SDL_PathInfo info;
  return SDL_GetPathInfo(launcher.c_str(), &info) && info.type == SDL_PATHTYPE_FILE ? launcher : self;
}

std::string exePath() {
#ifdef _WIN32
  wchar_t w[MAX_PATH * 2];
  const DWORD n = GetModuleFileNameW(nullptr, w, MAX_PATH * 2);
  char out[MAX_PATH * 6];
  if (n == 0 || WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), out, sizeof(out) - 1, nullptr, nullptr) <= 0) {
    return {};
  }
  out[WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(n), out, sizeof(out) - 1, nullptr, nullptr)] = '\0';
  return out;
#else
  if (const char* appimage = SDL_getenv("APPIMAGE")) {
    return appimage; // the AppImage itself, not its mounted copy
  }
  char out[4096];
  const ssize_t n = readlink("/proc/self/exe", out, sizeof(out) - 1);
  if (n <= 0) {
    return {};
  }
  out[n] = '\0';
  return out;
#endif
}

// tests: $TTYD_STEAM_DIR, a Steam folder of the test's own (userdata/<number>/
// config/...) in place of the installed one: the only one used, and Steam
// itself may run meanwhile (docs/STEAM.md)
const char* testRoot() {
  const char* dir = SDL_getenv("TTYD_STEAM_DIR");
  return dir != nullptr && dir[0] != '\0' ? dir : nullptr;
}

std::vector<std::string> steamRoots() {
  std::vector<std::string> roots;
  if (const char* test = testRoot()) {
    roots.push_back(test);
  } else {
#ifdef _WIN32
  wchar_t w[MAX_PATH * 2];
  DWORD size = sizeof(w);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, w, &size) ==
      ERROR_SUCCESS) {
    char out[MAX_PATH * 6];
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof(out), nullptr, nullptr) > 0) {
      roots.push_back(out);
    }
  }
  roots.push_back("C:/Program Files (x86)/Steam");
#else
  const char* home = SDL_getenv("HOME");
  if (home != nullptr) {
    const std::string h = home;
    for (const char* sub : {"/.steam/steam", "/.local/share/Steam", "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
                            "/.steam/root"}) {
      roots.push_back(h + sub);
    }
  }
#endif
  }
  std::vector<std::string> found;
  for (const std::string& r : roots) {
    SDL_PathInfo info;
    if (SDL_GetPathInfo((r + "/userdata").c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
      // the same Steam reached two ways (~/.steam/steam is a link) counts once
      char* real = nullptr;
#ifndef _WIN32
      real = realpath(r.c_str(), nullptr);
#endif
      std::string key = real != nullptr ? real : r;
      free(real);
#ifdef _WIN32
      // the registry's SteamPath is lower case with forward slashes: the same folder as the fixed
      // "C:/Program Files (x86)/Steam" compared without case, separators or a trailing one
      for (char& c : key) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      while (key.size() > 3 && key.back() == '/') {
        key.pop_back();
      }
#endif
      if (std::find(found.begin(), found.end(), key) == found.end()) {
        found.push_back(key);
      }
    }
  }
  return found;
}

// the Steam accounts that used this computer: userdata/<number>
std::vector<std::string> accounts(const std::string& root) {
  std::vector<std::string> ids;
  int count = 0;
  char** found = SDL_GlobDirectory((root + "/userdata").c_str(), "*", 0, &count);
  for (int i = 0; found != nullptr && i < count; i++) {
    const std::string id = found[i];
    if (!id.empty() && id != "0" && std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; })) {
      ids.push_back(id);
    }
  }
  SDL_free(found);
  return ids;
}

bool steamRunning() {
#ifdef _WIN32
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return false;
  }
  PROCESSENTRY32W e;
  e.dwSize = sizeof(e);
  bool running = false;
  for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
    if (_wcsicmp(e.szExeFile, L"steam.exe") == 0) {
      running = true;
      break;
    }
  }
  CloseHandle(snap);
  return running;
#else
  int count = 0;
  char** found = SDL_GlobDirectory("/proc", "[0-9]*", 0, &count);
  bool running = false;
  for (int i = 0; found != nullptr && i < count && !running; i++) {
    const std::string path = std::string("/proc/") + found[i] + "/comm";
    size_t size = 0;
    char* comm = static_cast<char*>(SDL_LoadFile(path.c_str(), &size));
    if (comm != nullptr) {
      std::string c(comm, size);
      SDL_free(comm);
      while (!c.empty() && (c.back() == '\n' || c.back() == '\0')) {
        c.pop_back();
      }
      running = c == "steam" || c == "steamwebhelper";
    }
  }
  SDL_free(found);
  return running;
#endif
}

uint32_t crc32(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) {
    c ^= ch;
    for (int k = 0; k < 8; k++) {
      c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
  }
  return ~c;
}

std::string quoted(const std::string& s) { return "\"" + s + "\""; }

std::string dirOf(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

using portfile::readAll;

using portfile::writeAll;

std::string stamp() {
  SDL_Time now = 0;
  SDL_DateTime dt{};
  SDL_GetCurrentTime(&now);
  SDL_TimeToDateTime(now, &dt, true);
  char text[32];
  std::snprintf(text, sizeof(text), "%04d%02d%02d-%02d%02d%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute,
                dt.second);
  return text;
}

// --- the artwork, from the logo ---

using Surface = std::unique_ptr<SDL_Surface, void (*)(SDL_Surface*)>;
Surface surface(SDL_Surface* s) { return Surface(s, SDL_DestroySurface); }

Surface logo() {
  SDL_IOStream* io = SDL_IOFromConstMem(portLogo512, portLogo512_size);
  SDL_Surface* s = io != nullptr ? SDL_LoadPNG_IO(io, true) : nullptr;
  SDL_Surface* rgba = s != nullptr ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(s);
  return surface(rgba);
}

// the logo's theatre colours: a deep curtain red to the stage's night blue,
// top to bottom, with the logo centred at size
Surface art(const SDL_Surface* icon, int w, int h, int size, float cy) {
  Surface s = surface(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32));
  if (!s) {
    return s;
  }
  for (int y = 0; y < h; y++) {
    const float t = static_cast<float>(y) / static_cast<float>(std::max(h - 1, 1));
    const Uint8 r = static_cast<Uint8>(120 + (40 - 120) * t), g = static_cast<Uint8>(14 + (28 - 14) * t),
                b = static_cast<Uint8>(24 + (80 - 24) * t);
    const SDL_Rect row{0, y, w, 1};
    SDL_FillSurfaceRect(s.get(), &row, SDL_MapSurfaceRGBA(s.get(), r, g, b, 255));
  }
  if (icon != nullptr && size > 0) {
    const SDL_Rect to{(w - size) / 2, static_cast<int>(h * cy) - size / 2, size, size};
    SDL_SetSurfaceBlendMode(const_cast<SDL_Surface*>(icon), SDL_BLENDMODE_BLEND);
    SDL_BlitSurfaceScaled(const_cast<SDL_Surface*>(icon), nullptr, s.get(), &to, SDL_SCALEMODE_LINEAR);
  }
  return s;
}

bool writeArt(const std::string& grid, uint32_t appid) {
  Surface icon = logo();
  if (!icon) {
    return false;
  }
  SDL_CreateDirectory(grid.c_str());
  const std::string id = std::to_string(appid);
  bool ok = true;
  Surface cover = art(icon.get(), 600, 900, 520, 0.42f);
  Surface wide = art(icon.get(), 920, 430, 380, 0.5f);
  Surface hero = art(icon.get(), 1920, 620, 0, 0.5f); // the logo goes over it, from _logo
  Surface small = surface(SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_RGBA32));
  if (small) {
    SDL_SetSurfaceBlendMode(icon.get(), SDL_BLENDMODE_NONE);
    SDL_BlitSurfaceScaled(icon.get(), nullptr, small.get(), nullptr, SDL_SCALEMODE_LINEAR);
  }
  ok &= cover && SDL_SavePNG(cover.get(), (grid + "/" + id + "p.png").c_str());
  ok &= wide && SDL_SavePNG(wide.get(), (grid + "/" + id + ".png").c_str());
  ok &= hero && SDL_SavePNG(hero.get(), (grid + "/" + id + "_hero.png").c_str());
  ok &= SDL_SavePNG(icon.get(), (grid + "/" + id + "_logo.png").c_str());
  ok &= small && SDL_SavePNG(small.get(), (grid + "/" + id + "_icon.png").c_str());
  return ok;
}

// --- the controller set-up ---

// configset_<controller>.vdf: Paper Cut's entry with a Steam template, unless
// the file names Paper Cut already (the player's own set-up stays)
void controllerConfig(const std::string& root, const std::string& account) {
  const std::string dir = root + "/steamapps/common/Steam Controller Configs/" + account + "/config";
  struct Set {
    const char* controller;
    const char* templ;
  };
  const Set kSets[] = {
      {"controller_neptune", "controller_neptune_gamepad+mouse_trackpad.vdf"},
      {"controller_ps5", "controller_ps5_gamepad_joystick.vdf"},
      {"controller_ps4", "controller_ps4_gamepad_joystick.vdf"},
      {"controller_xboxone", "controller_xboxone_gamepad_joystick.vdf"},
      {"controller_switch_pro", "controller_switch_pro_gamepad_joystick.vdf"},
  };
  SDL_CreateDirectory(dir.c_str());
  std::string name = kAppName;
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(SDL_tolower(c)); });
  for (const Set& set : kSets) {
    const std::string path = dir + "/configset_" + set.controller + ".vdf";
    const std::vector<uint8_t> bytes = readAll(path);
    std::string text(bytes.begin(), bytes.end());
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(SDL_tolower(c)); });
    if (lower.find("\"" + name + "\"") != std::string::npos) {
      continue;
    }
    const std::string entry = "\t\"" + name + "\"\n\t{\n\t\t\"template\"\t\t\"" + set.templ + "\"\n\t}\n";
    const size_t end = text.rfind('}');
    if (text.empty() || end == std::string::npos) {
      text = "\"controller_config\"\n{\n" + entry + "}\n";
    } else {
      text.insert(end, entry);
    }
    writeAll(path, text.data(), text.size());
  }
}

// --- adding it ---

std::string sStatus;

// one account's shortcuts.vdf: Paper Cut added or brought up to date
bool addTo(const std::string& root, const std::string& account, const std::string& exe, std::string& why) {
  const std::string config = root + "/userdata/" + account + "/config";
  const std::string path = config + "/shortcuts.vdf";
  std::vector<uint8_t> bytes = readAll(path);
  Node top;
  top.type = 0;
  top.key = "shortcuts";
  if (!bytes.empty()) {
    Reader r{bytes};
    Node file;
    r.map(file, 0);
    Node* list = r.ok ? child(file, "shortcuts") : nullptr;
    if (list == nullptr || list->type != 0) {
      why = "Steam's list of non-Steam games couldn't be read; it was left as it is.";
      return false;
    }
    top = *list;
  }
  // this program's entry, or one named Paper Cut
  Node* entry = nullptr;
  for (Node& n : top.kids) {
    Node* e = child(n, "Exe");
    Node* name = child(n, "AppName");
    if ((e != nullptr && e->str == quoted(exe)) || (name != nullptr && name->str == kAppName)) {
      entry = &n;
      break;
    }
  }
  if (entry == nullptr) {
    top.kids.push_back(Node{});
    entry = &top.kids.back();
    entry->type = 0;
    entry->key = std::to_string(top.kids.size() - 1);
  }
  const uint32_t appid = crc32(quoted(exe) + kAppName) | 0x80000000u;
  setInt(*entry, "appid", appid);
  setStr(*entry, "AppName", kAppName);
  setStr(*entry, "Exe", quoted(exe));
  setStr(*entry, "StartDir", quoted(dirOf(exe)));
  setStr(*entry, "icon", config + "/grid/" + std::to_string(appid) + "_icon.png");
  if (child(*entry, "ShortcutPath") == nullptr) {
    setStr(*entry, "ShortcutPath", "");
    setStr(*entry, "LaunchOptions", "");
    setInt(*entry, "IsHidden", 0);
    setInt(*entry, "AllowDesktopConfig", 1);
    setInt(*entry, "AllowOverlay", 1);
    setInt(*entry, "OpenVR", 0);
    setInt(*entry, "Devkit", 0);
    setStr(*entry, "DevkitGameID", "");
    setInt(*entry, "DevkitOverrideAppID", 0);
    setInt(*entry, "LastPlayTime", 0);
    setStr(*entry, "FlatpakAppID", "");
    Node tags;
    tags.type = 0;
    tags.key = "tags";
    entry->kids.push_back(tags);
  }
  std::vector<uint8_t> out;
  put(out, top);
  out.push_back(8);
  SDL_CreateDirectory(config.c_str());
  if (!bytes.empty() && !writeAll(path + ".papercut-" + stamp() + ".bak", bytes.data(), bytes.size())) {
    why = "No copy of Steam's list could be kept, so it wasn't changed.";
    return false;
  }
  if (!writeAll(path, out.data(), out.size())) {
    why = std::string("Steam's list couldn't be written: ") + SDL_GetError();
    return false;
  }
  if (!writeArt(config + "/grid", appid)) {
    why = "Added, but the artwork couldn't be written.";
  }
  controllerConfig(root, account);
  return true;
}

void addToSteam() {
  if (testRoot() == nullptr && steamRunning()) {
    sStatus = "Steam is running: close it first (on a Steam Deck, in Desktop Mode: Steam, then Exit), then press "
              "again.";
    return;
  }
  const std::string exe = startPath();
  const std::vector<std::string> roots = steamRoots();
  if (exe.empty()) {
    sStatus = "Paper Cut couldn't tell where it is installed.";
    return;
  }
  if (roots.empty()) {
    sStatus = "Steam isn't installed here (or has never been signed in to).";
    return;
  }
  int added = 0;
  std::string why;
  for (const std::string& root : roots) {
    for (const std::string& account : accounts(root)) {
      added += addTo(root, account, exe, why) ? 1 : 0;
    }
  }
  if (added > 0) {
    sStatus = why.empty() ? "Added. Start Steam: Paper Cut is in the library, with its artwork." : why;
  } else {
    sStatus = why.empty() ? "No Steam account has used this computer yet: sign in to Steam once, close it, then "
                            "press again."
                          : why;
  }
}

void section(int inGame) {
  (void)inGame;
  if (ImGui::Button(portTrLabel("Add Paper Cut to Steam"))) {
    addToSteam();
  }
  if (!sStatus.empty()) {
    ImGui::TextWrapped("%s", portTr(sStatus.c_str()));
  }
}

// tests: $TTYD_STEAM_TEST=1 (with $TTYD_STEAM_DIR only) presses the button once
// the first frame is up and prints what it says ("[steam] ...")
void testDraw(int pageOpen) {
  static bool sDone;
  (void)pageOpen;
  if (sDone || SDL_getenv("TTYD_STEAM_TEST") == nullptr) {
    return;
  }
  sDone = true;
  if (testRoot() == nullptr) {
    std::printf("[steam] TTYD_STEAM_TEST needs TTYD_STEAM_DIR (a test folder, never the installed Steam)\n");
  } else {
    addToSteam();
    std::printf("[steam] %s: %s\n", testRoot(), sStatus.c_str());
  }
  std::fflush(stdout);
}

struct SteamShortcut {
  SteamShortcut() {
    portSettingsAddOverlay(testDraw, nullptr);
    portSettingsAddSection("General", "Add to Steam",
                           "Puts Paper Cut in the Steam library as a non-Steam game, with its own artwork and a "
                           "controller set-up (the Steam Deck's right pad as a mouse). Steam must be closed; a copy of "
                           "its list is kept.",
                           0, section, "steam deck library shortcut non-steam artwork big picture");
  }
} sSteamShortcut;

} // namespace
#endif
