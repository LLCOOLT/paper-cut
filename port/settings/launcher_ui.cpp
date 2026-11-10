// The launcher: the screen before the game, when no disc was given on the
// command line (port/src/aurora/disc_aurora.c). It keeps a library of the
// player's disc images (library.txt in the user folder), names each one's
// version (port/include/port/versions.h), and plays the chosen one: in this
// executable if it is this build's version, else by starting the executable
// of its version beside this one (ttyd-<short>, which then skips the
// launcher). iOS apps can't start another program, so there each app plays
// its own version and shows the others' discs as not playable here. The
// settings' tabs (settings_ui.cpp) are here too, for what applies at start.
#include "port/achievements.h"
#include "port/art_recipe.h"
#include "port/aurora_pc.h"
#include "port/online.h"
#include "port/online_menu.h"
#include "port/pm64.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/together.h"
#include "port/save_convert.h"
#include "port/save_files.h"
#include "port/cardsave_pc.h"
#include "port/save_transfer.h"
#include "port/tr.h"
#include "port/versions.h"
#include "settings_ui.h"
#include "enhancement_presets.h"
#include "port/update_check.h"
#include "port/resume.h"
#include "port/restart.h"
#include "port/launcher_back.h"
#include "launcher_style.h"
#include "launcher_brand.h"
#include "launcher_look.h"
#include "launcher_popup.h"
#include "launcher_shot.h"
#include "launcher_onboard.h"

#include <SDL3/SDL.h>
#include "file_util.h"
#include <aurora/aurora.h>
#include <imgui.h>
#include <imgui_internal.h> // FocusItem
#include "port/menu_buttons.h"
extern "C" void portBindingsLauncherFrame(void); // features/bindings.cpp
extern "C" int portBindingsAsking(void);        // (the same: a key or button asked for)

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define LAUNCHER_ONE_VERSION 1 // an app can't start another program
#include "port/ios_pc.h" // the Documents folder, remembered paths
#endif
#ifdef __ANDROID__
#define LAUNCHER_ONE_VERSION 1 // each version is an app of its own (port/android/app/build.gradle)
#include "port/android_pc.h"
#endif

extern "C" int portOnlinePeekInvite(char* out, size_t size); // invite.cpp
extern "C" int portOnlineTakeInvite(char* out, size_t size);
// the mod manager (port/settings/features/mods_manager.cpp): its tab, and what goes over the launcher
// (files dragged in, safe mode's offer); true: show the Mods tab
void portModManagerLauncherTab(float scale, bool stacked);
bool portModManagerLauncherFront(float scale);
bool portModManagerLauncherBack(); // the Mods tab's back: a question it asks taken back first (true: it was)
extern "C" void portSettingsShowTab(const char* tab); // settings_ui.cpp: the Settings tab's page shown next
extern "C" int portSettingsLauncherBack(void); // (the same: the Settings tab's back, a search cleared first)
extern "C" int portConsoleAccurateLockedValue(unsigned flags, const int* value); // console_accurate.cpp

namespace {

struct Disc {
  std::string path;
  std::string name; // the file's name, as the list shows it
  const PortVersionInfo* version = nullptr; // nullptr: unreadable or not the game
  bool exists = true;
  // its banner (the picture a GameCube's menu shows for it), read from the disc when it's probed
  std::vector<unsigned char> bannerPixels; // 96x32 RGBA8, until it's a texture
  ImTextureID banner{};
  bool hasBanner = false;
};

struct Launcher {
  std::string libraryFile;
  std::string lastFile;
  std::vector<Disc> discs;
  int selected = -1;
  std::string status;  // the last problem, shown under the list
  std::string result;  // set when a disc of this version is chosen
  bool pad = false;        // a controller in use: its buttons shown, the focus outline kept up
  bool focusList = true;   // put the focus on the selected disc (or the Add button) this frame
  int tab = 0;             // the tab shown (Play, Settings, Mods, About: kTab*)
  int wantTab = -1;        // a tab asked for with the shoulder buttons
  std::string userPath;    // the user folder (the game look's art cache)
  float pm64H = 0.f;       // the Paper Mario 64 entry's height last frame (the disc list leaves it room)
  bool pm64Focus = false;  // a ROM just linked from the entry: the focus to its Settings button
};

enum { kTabPlay, kTabSettings, kTabMods, kTabAbout, kTabCount }; // (no Multiplayer tab: the game's own page)

// The controller in use, for its buttons' names and the confirm button:
// Nintendo controllers confirm with A on the right (east), the others with the
// bottom button (south).
SDL_GamepadType padType() {
  // $TTYD_LAUNCHER_PAD (tests, screenshots): xbox, ps or switch, as if that controller were in use
  static const char* test = SDL_getenv("TTYD_LAUNCHER_PAD");
  if (test != nullptr) {
    return std::strcmp(test, "ps") == 0       ? SDL_GAMEPAD_TYPE_PS5
           : std::strcmp(test, "switch") == 0 ? SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO
                                              : SDL_GAMEPAD_TYPE_XBOXONE;
  }
  // the controller in hand (the last one pressed: menu_buttons.cpp)
  const unsigned id = portMenuPadId();
  return id != 0 ? SDL_GetGamepadTypeForID(static_cast<SDL_JoystickID>(id)) : SDL_GAMEPAD_TYPE_STANDARD;
}

bool nintendoPad(SDL_GamepadType type) {
  return type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
         type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

bool playstationPad(SDL_GamepadType type) {
  return type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
}

// A controller's button pressed: controller mode; the mouse moved or clicked:
// pointer mode. Keyboard keys leave it as it is.
void updateInputMode(Launcher& l) {
  ImGuiIO& io = ImGui::GetIO();
  for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown; k++) {
    if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
      l.pad = true;
      break;
    }
  }
  if (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f || ImGui::IsAnyMouseDown()) {
    l.pad = false;
  }
  if (l.pad) {
    ImGui::SetNavCursorVisible(true);
  }
}

std::string fileName(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string readFile(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  if (data == nullptr) {
    return {};
  }
  std::string text(static_cast<const char*>(data), size);
  SDL_free(data);
  return text;
}

// A disc's path as library.txt and last_disc.txt keep it, and back (file_util.h: iOS's container moves)
std::string storedPath(const std::string& path) { return portfile::storePath(path); }
std::string resolvedPath(const std::string& stored) { return portfile::resolvePath(stored); }

void writeFile(const std::string& path, const std::string& text) {
  if (!portfile::writeAll(path, text.data(), text.size())) {
    std::fprintf(stderr, "launcher: could not write %s: %s\n", path.c_str(), SDL_GetError());
  }
}

void probe(Disc& d) {
  SDL_PathInfo info;
#ifdef __ANDROID__
  // the file picker's content:// URI: no path stat() can see, and often no file name in it
  if (PORT_ANDROID_IS_CONTENT_URI(d.path.c_str())) {
    char name[512];
    d.exists = portAndroidContentInfo(d.path.c_str(), name, sizeof(name), nullptr) != 0;
    d.name = d.exists && name[0] != '\0' ? std::string(name) : fileName(d.path);
  } else
#endif
  {
    d.exists = SDL_GetPathInfo(d.path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
    d.name = fileName(d.path);
  }
  d.version = nullptr;
  d.hasBanner = false;
  if (d.exists) {
    int has = 0;
    d.bannerPixels.assign(96 * 32 * 4, 0);
    d.version = portAuroraProbeDiscBanner(d.path.c_str(), d.bannerPixels.data(), &has);
    d.hasBanner = has != 0;
    if (!d.hasBanner) {
      d.bannerPixels.clear();
    }
  }
}

// the banner's texture, made at its first draw
ImTextureID bannerOf(Disc& d) {
  if (d.hasBanner && d.banner == ImTextureID{} && !d.bannerPixels.empty()) {
    d.banner = launcherTexture(96, 32, d.bannerPixels.data());
    d.bannerPixels.clear();
  }
  return d.hasBanner ? d.banner : ImTextureID{};
}

// a version's region for its tag: JP, US or EU
const char* regionOf(const PortVersionInfo* v) {
  if (v == nullptr) {
    return "?";
  }
  return v->version[3] == 'J' ? "JP" : v->version[3] == 'P' ? "EU" : "US";
}

// The game look's art from disc i (made from it now if it isn't in the art cache yet)
void lookArt(Launcher& l, int i) {
  if (i >= 0 && i < static_cast<int>(l.discs.size()) && l.discs[i].version != nullptr) {
    launcherLookArt(l.userPath.c_str(), l.discs[i].version->id, l.discs[i].path.c_str());
  }
}

void save(const Launcher& l) {
  std::string text;
  for (const Disc& d : l.discs) {
    text += storedPath(d.path) + "\n";
  }
  writeFile(l.libraryFile, text);
}

// index of path in the library, adding it (probed) if it's new
int add(Launcher& l, const std::string& path) {
  for (size_t i = 0; i < l.discs.size(); i++) {
    if (l.discs[i].path == path) {
      return static_cast<int>(i);
    }
  }
  Disc d;
  d.path = path;
  probe(d);
  l.discs.push_back(d);
  save(l);
  return static_cast<int>(l.discs.size()) - 1;
}

// A disc image picked (or opened): in the list, chosen, its art made for the game look
void addDisc(Launcher& l, const char* path) {
  l.selected = add(l, path);
  l.status.clear();
  l.focusList = true;
  lookArt(l, l.selected);
  launcherSound(LauncherSound::Select);
}

// A file picked with Add (or a test's): a Paper Mario 64 ROM is linked (pm64_link.cpp, the settings
// page's code; the entry under the discs says how it went), anything else goes in the list
void takePicked(Launcher& l, const char* path) {
  if (portPm64LinkIsRom(path)) {
    portPm64LinkQueue(path);
    l.status.clear();
    l.pm64Focus = l.pad;
    launcherSound(LauncherSound::Select);
    return;
  }
  addDisc(l, path);
}

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
// Desktops: the discs folder in the user folder. Its disc images are listed (at start, and at Add where no
// file dialog can be used: Steam Deck's game mode, a Linux without one).
#define LAUNCHER_DISCS_FOLDER 1

// a disc image's name (by its extension: .iso, .rvz...)
bool discName(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  return dot != std::string::npos && dot + 1 < path.size() &&
         SDL_strcasestr(".iso.gcm.ciso.gcz.rvz.wia.wbfs.nfs.tgc.", ("." + path.substr(dot + 1) + ".").c_str()) != nullptr;
}

std::string discsFolder(const Launcher& l) { return l.userPath + "/discs"; }

// the discs folder's disc images not in the list yet, added (probed); the last one's index, or -1
int addFromFolder(Launcher& l) {
  const std::string dir = discsFolder(l);
  int count = 0, last = -1;
  char** names = SDL_GlobDirectory(dir.c_str(), nullptr, 0, &count);
  for (int i = 0; names != nullptr && i < count; i++) {
    const std::string path = dir + "/" + names[i];
    bool known = false;
    for (const Disc& d : l.discs) {
      known = known || d.path == path;
    }
    if (!known && discName(path)) {
      last = add(l, path);
    }
  }
  SDL_free(names);
  return last;
}

// Steam Deck's game mode (Steam's own screen, gamescope): a file dialog opened from there is out of a
// controller's reach. $TTYD_LAUNCHER_NO_DIALOG=1 (tests): as there.
bool deckGameMode() {
  static const bool sOn = [] {
    if (SDL_getenv("TTYD_LAUNCHER_NO_DIALOG") != nullptr) {
      return true;
    }
#if defined(__linux__)
    const char* ui = SDL_getenv("SteamGamepadUI");
    const char* desktop = SDL_getenv("XDG_CURRENT_DESKTOP");
    return (ui != nullptr && std::atoi(ui) != 0) || (desktop != nullptr && SDL_strcasecmp(desktop, "gamescope") == 0);
#else
    return false;
#endif
  }();
  return sOn;
}
#endif

// Add: the system's file dialog. A controller's press in Steam Deck's game mode, or a system with no dialog
// (SDL says why): the discs folder instead, looked in again; nothing new there, l.status says where it is.
void pickAdd(Launcher& l, bool fromPad) {
#ifdef LAUNCHER_DISCS_FOLDER
  const std::string dir = discsFolder(l);
  if (fromPad && deckGameMode()) {
    const int at = addFromFolder(l);
    if (at >= 0) {
      addDisc(l, l.discs[at].path.c_str());
    } else {
      SDL_CreateDirectory(dir.c_str());
      l.status = portTrf("Put the disc image in this folder, then press Add again: %s", dir.c_str());
    }
    return;
  }
#else
  (void)fromPad;
#endif
  char path[4096];
  if (portAuroraPickDisc(path, sizeof(path))) {
    takePicked(l, path); // (a Paper Mario 64 ROM: linked)
    return;
  }
  const std::string why = portAuroraPickError();
  if (why.empty()) {
    return; // (cancelled)
  }
#ifdef LAUNCHER_DISCS_FOLDER
  const int at = addFromFolder(l);
  if (at >= 0) {
    addDisc(l, l.discs[at].path.c_str());
    return;
  }
  SDL_CreateDirectory(dir.c_str());
  l.status = portTrf("The file picker didn't open (%s). Put the disc image in this folder, then press Add again: %s",
                     why.c_str(), dir.c_str());
#else
  l.status = portTrf("The file picker didn't open (%s).", why.c_str());
#endif
}

void load(Launcher& l, const char* userPath) {
  const std::string dir = userPath != nullptr ? userPath : ".";
  l.libraryFile = dir + "/library.txt";
  l.lastFile = dir + "/last_disc.txt";
  std::string text = readFile(l.libraryFile);
  if (text.empty()) {
    text = readFile(dir + "/disc.txt"); // the one disc 0.1 remembered
  }
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(at, end - at);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty()) {
      Disc d;
      d.path = resolvedPath(line); // (iOS: under today's container)
      bool known = false;          // (iOS: an old path and a new one can resolve to the same file)
      for (const Disc& k : l.discs) {
        known = known || k.path == d.path;
      }
      if (!known) {
        l.discs.push_back(d);
      }
    }
    at = end + 1;
  }
#if defined(__APPLE__) && TARGET_OS_IPHONE
  // iOS: the disc images in this app's folder (the Files app) are listed too, and left out of backups
  if (const char* docs = portIosDocumentsDir()) {
    int count = 0;
    char** names = SDL_GlobDirectory(docs, nullptr, 0, &count);
    for (int i = 0; names != nullptr && i < count; i++) {
      const std::string path = std::string(docs) + "/" + names[i];
      const std::string ext = path.substr(path.find_last_of('.') + 1);
      if (ext != path && SDL_strcasestr(".iso.gcm.ciso.gcz.rvz.wia.wbfs.nfs.tgc.", ("." + ext + ".").c_str())) {
        portIosExcludeFromBackup(path.c_str());
        bool known = false;
        for (const Disc& d : l.discs) {
          known = known || d.path == path;
        }
        if (!known) {
          Disc d;
          d.path = path;
          l.discs.push_back(d);
        }
      }
    }
    SDL_free(names);
  }
#endif
  for (Disc& d : l.discs) {
    probe(d);
  }
#ifdef LAUNCHER_DISCS_FOLDER
  addFromFolder(l); // (disc images put in the discs folder since the last start)
#endif
  const std::string last = resolvedPath(readFile(l.lastFile));
  for (size_t i = 0; i < l.discs.size(); i++) {
    if (l.discs[i].path == last) {
      l.selected = static_cast<int>(i);
    }
  }
  if (l.selected < 0 && !l.discs.empty()) {
    l.selected = 0;
  }
}

// The executable of version v beside this one, or "" if there is none.
std::string executableOf(const PortVersionInfo* v) {
  const char* base = SDL_GetBasePath();
  std::string dir = base != nullptr ? base : "";
  // a macOS app bundle: SDL's base path is Contents/Resources, the executables are in Contents/MacOS
  const std::string resources = "Contents/Resources/";
  if (dir.size() >= resources.size() && dir.compare(dir.size() - resources.size(), resources.size(), resources) == 0) {
    dir = dir.substr(0, dir.size() - resources.size()) + "Contents/MacOS/";
  }
#ifdef _WIN32
  const std::string exe = dir + "ttyd-" + v->shortName + ".exe";
#else
  const std::string exe = dir + "ttyd-" + v->shortName;
#endif
  SDL_PathInfo info;
  return SDL_GetPathInfo(exe.c_str(), &info) && info.type == SDL_PATHTYPE_FILE ? exe : std::string();
}

// What Play does with disc d; "" if it can, else why not.
std::string playProblem(const Disc& d) {
  const PortVersionInfo* self = portVersionSelf();
  if (!d.exists) {
#ifdef __ANDROID__
    if (PORT_ANDROID_IS_CONTENT_URI(d.path.c_str())) {
      return portTr("The file can't be read any more: it was moved or deleted, or Android took back Paper Cut's access to "
                    "it. Add it again.");
    }
#endif
    return portTr("The file isn't there any more.");
  }
  if (d.version == nullptr) {
    return portTr("This isn't a disc image of Paper Mario: The Thousand-Year Door that can be read.");
  }
  if (d.version == self) {
    return {};
  }
  if (!d.version->ported) {
    return portTrf("This is %s, which the port doesn't play.", portTr(d.version->name));
  }
#ifdef LAUNCHER_ONE_VERSION
  return portTrf("This is %s. This app plays %s; the app for that version plays this disc.", portTr(d.version->name),
                 portTr(self->name));
#else
  if (executableOf(d.version).empty()) {
    return portTrf("This is %s, and its build (ttyd-%s) isn't beside this one.", portTr(d.version->name),
                   d.version->shortName);
  }
  return {};
#endif
}

// Plays disc i: here, or by starting its version's executable (which exits this one). False when it
// doesn't (l.status says why).
bool play(Launcher& l, int i) {
  const Disc& d = l.discs[i];
  const std::string problem = playProblem(d);
  if (!problem.empty()) {
    l.status = problem;
    return false;
  }
  writeFile(l.lastFile, storedPath(d.path));
  if (d.version == portVersionSelf()) {
    l.result = d.path;
    return true;
  }
  const std::string exe = executableOf(d.version);
  const char* args[] = {exe.c_str(), d.path.c_str(), nullptr};
  SDL_Process* process = SDL_CreateProcess(args, false);
  if (process == nullptr) {
    l.status = portTrf("Could not start %s: %s", fileName(exe).c_str(), SDL_GetError());
    return false;
  }
  // it runs on; this one ends at once: the library and settings are saved, and
  // Aurora's shutdown (its GPU and audio threads) can hang here before a game ever ran
  portEndForStarted(process);
  return true;
}

// The memory card's four files for this build's release (port/include/port/save_files.h): which hold a
// save, the first free one, and a save's own .gci for the start's warp (warp_pc.c loads its first file).
struct CardFiles {
  PortSaveInfo info[4] = {};
  bool any = false;
  int firstFree = -1;
  std::string gci;
};

CardFiles readCard() {
  CardFiles c;
  char name[64];
  portSaveFileName(PORT_GAME_ID[3], name, sizeof(name));
  c.gci = readFile(portSavesCardDir(PORT_GAME_ID[3]) + "/" + name);
  for (int i = 0; i < 4; i++) {
    const size_t at = 0x40 + PORT_CARD_META_SIZE + static_cast<size_t>(i) * PORT_CARD_SLOT_SIZE;
    if (c.gci.size() >= at + PORT_CARD_SLOT_SIZE &&
        portSaveImageInfo(reinterpret_cast<const uint8_t*>(c.gci.data() + at), &c.info[i])) {
      c.any = true;
    } else if (c.firstFree < 0) {
      c.firstFree = i;
    }
  }
  return c;
}

// the card as read at most every two seconds (the launcher draws every frame)
const CardFiles& cardNow() {
  static CardFiles sCard;
  static Uint64 sAt;
  if (sAt == 0 || SDL_GetTicks() - sAt > 2000) {
    sCard = readCard();
    sAt = SDL_GetTicks();
  }
  return sCard;
}

// "Start in Rogueport" (the setting: 0 off, 1 offer, 2 always)
int rogueportMode() {
  const int* v = portSettingsFind("rogueport_start");
  return v != nullptr ? *v : 0;
}

// A new file's choice of start: 1 Rogueport, 0 the beginning, -1 not yet; the setting's (off: the beginning,
// no question; always: Rogueport).
int newFileChoice(float scale) {
  const int mode = rogueportMode();
  if (mode == 2) {
    return 1;
  }
  if (mode == 0) {
    return 0;
  }
  if (launcherButton(portTrLabel("From the beginning##nf_b"), ImVec2(0, 0), scale, LauncherButton::Primary)) {
    return 0;
  }
  ImGui::SameLine();
  if (launcherButton(portTrLabel("Start in Rogueport##nf_r"), ImVec2(0, 0), scale)) {
    return 1;
  }
  // what Rogueport's start gives (gameplus_pc.c's quickStartApply)
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", launcherRogueportNote());
  ImGui::PopStyleColor();
  return -1;
}

std::string saveLabel(int i, const PortSaveInfo& s) {
  char text[160];
  // (a file made with no name entry, as the launcher's New adventure makes it, has none: Mario's, not "File 1: , ")
  std::snprintf(text, sizeof(text), portTr("File %d: %s, chapter %d, level %d"), i + 1, s.name[0] ? s.name : "Mario",
                s.chapter, s.level);
  return text;
}

// Quick resume's save for a disc's version (port/src/aurora/resume_aurora.c):
// its path, when there's one, and when it was written
std::string resumeFile(const Disc& d, SDL_Time* when = nullptr) {
  if (d.version == nullptr) {
    return {};
  }
  char path[1024];
  portResumePath(path, sizeof(path), d.version->id, 0);
  SDL_PathInfo info;
  if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
    return {};
  }
  if (when != nullptr) {
    *when = info.modify_time;
  }
  return path;
}

// Plays disc i from quick resume's save, as the save file it came from: the
// save warp at start ($TTYD_LOAD_SAVE, warp_pc.c), here or in the version's
// executable started (which takes this one's environment)
void continueGame(Launcher& l, int i) {
  const std::string path = resumeFile(l.discs[i]);
  char sidecar[1024];
  portResumePath(sidecar, sizeof(sidecar), l.discs[i].version->id, 1);
  std::string slot = readFile(sidecar);
  slot = slot.empty() || slot[0] < '0' || slot[0] > '3' ? "3" : slot.substr(0, 1);
  // (a disc that can't be played asks for nothing: no warp waits for a later Play)
  const std::string problem = playProblem(l.discs[i]);
  if (!problem.empty()) {
    l.status = problem;
    return;
  }
  const bool self = l.discs[i].version == portVersionSelf();
  if (self) {
    portWarpRequestResume(path.c_str(), slot[0] - '0'); // here: straight to the warp
  } else {
    // another version's executable: through its environment (SDL_CreateProcess passes SDL's)
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LOAD_SAVE", path.c_str(), true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LOAD_SAVE_SLOT", slot.c_str(), true);
  }
  if (!play(l, i) && !self) {
    // not started: a later plain Play of another version from here would start on this save
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LOAD_SAVE");
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LOAD_SAVE_SLOT");
    std::printf("[launcher] Continue: %s not started, its save warp taken back\n",
                l.discs[i].version->id);
    std::fflush(stdout);
  }
}

// a controller's button for the page under the cards (What's new, the multiplayer help): none while one is up,
// or while a pop-up is open (safe mode's question, the recovery screen: Start mustn't play behind them)
bool pageKey(ImGuiKey key) {
  return !launcherOverlayShowing() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(key, false);
}

// The way back on the launcher's pages: a controller's back button and the keyboard's Escape (both come as
// Escape: menu_buttons.cpp; Android's back too: settings_ui.cpp) or the mouse's back button. Once a frame:
// the first part of the page to ask has it (a question before its page, a page before its tab). None while
// a pop-up, a text field or a binding had it last frame (Escape closed those before this frame's drawing),
// or a card is over the stage (it takes its own).
bool sBackBusy;  // at the last frame's end: a pop-up open, or an item (a text field) in use
bool sBackTaken; // this frame's back taken already
bool takeBack() {
  if (sBackTaken || sBackBusy || launcherOverlayShowing() || portBindingsAsking() ||
      !(ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) { // (3: the mouse's back)
    return false;
  }
  sBackTaken = true;
  return true;
}

bool touchScreen() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  return true;
#else
  // $TTYD_LAUNCHER_TOUCH (tests, screenshots): sized as on a phone or tablet
  static const bool test = SDL_getenv("TTYD_LAUNCHER_TOUCH") != nullptr;
  return test;
#endif
}

// A page's paper window filling size (0: the rest of the row or column), as a child window with the
// window drawn behind it (launcherPanel). Pair with ImGui::EndChild().
void beginCard(const char* id, ImVec2 size, float scale) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (size.x <= 0) {
    size.x = avail.x;
  }
  if (size.y <= 0) {
    size.y = avail.y - 8.f * scale; // room for the window's shadow
  }
  const ImVec2 at = ImGui::GetCursorScreenPos();
  launcherPanel(ImGui::GetWindowDrawList(), at, ImVec2(at.x + size.x, at.y + size.y), scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f * scale, 20.f * scale));
  ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
}

// text shortened with "..." to fit width (UTF-8 kept whole)
std::string fit(const std::string& text, float width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= width) {
    return text;
  }
  std::string s = text;
  while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > width) {
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
      s.pop_back();
    }
  }
  return s + "...";
}

// muted text, wrapped
void note(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// a disc's banner (or a blank card where it has none) from at, w wide (its shape 3:1), framed
void drawBanner(ImDrawList* dl, Disc& d, ImVec2 at, float w, float scale) {
  const float h = w / 3.f;
  const float line = std::max(1.5f, 2.f * scale);
  dl->AddRectFilled(ImVec2(at.x + 2.f * scale, at.y + 3.f * scale), ImVec2(at.x + w + 2.f * scale, at.y + h + 3.f * scale),
                    IM_COL32(20, 10, 30, 80), 3.f * scale);
  dl->AddRectFilled(ImVec2(at.x - line, at.y - line), ImVec2(at.x + w + line, at.y + h + line),
                    IM_COL32(86, 66, 52, 255), 3.f * scale);
  if (const ImTextureID tex = bannerOf(d); tex != ImTextureID{}) {
    dl->AddImage(tex, at, ImVec2(at.x + w, at.y + h));
  } else {
    // no banner: a blank card with the disc's mark
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(236, 222, 196, 255));
    const ImVec2 c(at.x + w * 0.5f, at.y + h * 0.5f);
    dl->AddCircle(c, h * 0.36f, IM_COL32(190, 160, 120, 255), 0, line);
    dl->AddCircleFilled(c, h * 0.08f, IM_COL32(190, 160, 120, 255));
  }
}

// The Paper Mario 64 entry, under the discs (docs/PM64_LINK.md; pm64_link.cpp does the linking): one quiet
// row, its name and state at the left and its one button at the right (under them where it's narrow):
// Settings (its Paper Mario 64 page) once linked; else Link... (the file picker), or Link <file> for a ROM
// found in the user folder or Downloads (a controller alone does it). Nothing linked and the card not
// hidden: the row says "Have Paper Mario 64? Link it" and What it adds opens what linking unlocks under it
// (with Settings and Don't show again). Turn on all Paper Mario 64 content?, once a ROM is linked, is a card
// over the stage (launcher_onboard.h). Its height into l.pm64H for the list above.
void drawPm64(Launcher& l, float scale) {
  const float top = ImGui::GetCursorScreenPos().y;
  const char* said = portPm64LinkPoll(); // (a ROM picked or dropped is linked here)
  const int v = portPm64Linked();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float left = ImGui::GetCursorScreenPos().x;
  const float width = ImGui::GetContentRegionAvail().x;
  static bool sMore = false; // What it adds, open
  ImGui::Dummy(ImVec2(0, 4.f * scale));
  const float lineY = ImGui::GetCursorScreenPos().y;
  dl->AddLine(ImVec2(left, lineY), ImVec2(left + width, lineY), IM_COL32(86, 66, 52, 90), std::max(1.f, 1.5f * scale));
  ImGui::Dummy(ImVec2(0, 6.f * scale));
  const bool card = v == PM64_NONE && launcherPm64CardWanted();
  if (!card) {
    sMore = false;
  }
  // the row's buttons, right to left: the main one, then (the card) What it adds / Hide
  const char* found = v == PM64_NONE ? portPm64LinkFound(0) : nullptr;
  std::string mainId;
  if (v != PM64_NONE) {
    mainId = portTrLabel("Settings##pm64_settings");
  } else if (found != nullptr) {
    char label[160];
    std::snprintf(label, sizeof(label), portTr("Link %s"),
                  fit(fileName(found), std::max(80.f * scale, width * 0.45f)).c_str());
    mainId = std::string(label) + "##pm64_found";
  } else {
    mainId = portTrLabel("Link...##pm64_pick");
  }
  const char* moreId = sMore ? portTrLabel("Hide##pm64_more") : portTrLabel("What it adds##pm64_more");
  auto buttonW = [&](const char* id) {
    return ImGui::CalcTextSize(id, ImGui::FindRenderedTextEnd(id)).x + 44.f * scale;
  };
  const float gap = 10.f * scale;
  const float buttonsW = buttonW(mainId.c_str()) + (card ? gap + buttonW(moreId) : 0.f);
  // the name (and the state) at the left, its middle on the buttons' row
  char state[128] = "";
  if (v != PM64_NONE) {
    std::snprintf(state, sizeof(state), portTr("Linked (%s)"), portTr(portPm64VersionName(v)));
  } else if (!card) {
    std::snprintf(state, sizeof(state), "%s", portTr("Not linked"));
  }
  const char* name = card ? portTr("Have Paper Mario 64? Link it") : portTr("Paper Mario 64");
  const float nameW = ImGui::CalcTextSize(name).x + (state[0] ? 12.f * scale + ImGui::CalcTextSize(state).x : 0.f) +
                      (card ? 20.f * scale : 0.f);
  const bool oneRow = nameW + 16.f * scale + buttonsW <= width;
  const float rowTop = ImGui::GetCursorScreenPos().y;
  const float frameH = ImGui::GetFrameHeight();
  const float textY = oneRow ? rowTop + (frameH - ImGui::GetTextLineHeight()) * 0.5f : rowTop;
  float x = left;
  if (card) {
    launcherSparkle(dl, ImVec2(x + 6.f * scale, textY + ImGui::GetTextLineHeight() * 0.5f), 5.f * scale, kGold);
    x += 20.f * scale;
  }
  ImGui::SetCursorScreenPos(ImVec2(x, textY));
  ImGui::TextUnformatted(name);
  if (state[0]) {
    ImGui::SameLine(0, 12.f * scale);
    if (v != PM64_NONE) {
      ImGui::TextColored(ImVec4(0.22f, 0.50f, 0.25f, 1.f), "%s", state);
    } else {
      ImGui::TextDisabled("%s", state);
    }
  }
  ImGui::SetCursorScreenPos(oneRow ? ImVec2(left + width - buttonsW, rowTop)
                                   : ImVec2(left, ImGui::GetItemRectMax().y + 6.f * scale));
  if (launcherButton(mainId.c_str(), ImVec2(0, 0), scale)) {
    if (v != PM64_NONE) {
      portSettingsShowTab("Paper Mario 64"); // Settings > Enhancements > Paper Mario 64
      l.wantTab = kTabSettings;
      launcherSound(LauncherSound::Select);
    } else if (found != nullptr) {
      portPm64LinkQueue(found);
      l.pm64Focus = l.pad;
      launcherSound(LauncherSound::Select);
    } else {
      portPm64LinkPick();
      l.pm64Focus = l.pad;
    }
  }
  if (l.pm64Focus && v != PM64_NONE) {
    ImGui::FocusItem(); // (the Link button it was on is now Settings)
    l.pm64Focus = false;
  }
  if (card) {
    ImGui::SameLine(0, gap);
    if (launcherButton(moreId, ImVec2(0, 0), scale)) {
      sMore = !sMore;
      launcherSound(sMore ? LauncherSound::Turn : LauncherSound::Back);
    }
  }
  if (card && sMore) {
    // what linking unlocks, on a gold-tinted sheet, then Settings and Don't show again
    const float pad = 4.f * scale;
    ImGui::Dummy(ImVec2(0, 4.f * scale));
    const float cardTop = ImGui::GetCursorScreenPos().y;
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::Indent(10.f * scale);
    ImGui::Dummy(ImVec2(0, 2.f * scale));
    launcherPm64CardBody(scale, false, false);
    ImGui::Dummy(ImVec2(0, 2.f * scale));
    if (launcherButton(portTrLabel("Settings##pm64_settings"), ImVec2(0, 0), scale)) {
      portSettingsShowTab("Paper Mario 64");
      l.wantTab = kTabSettings;
      launcherSound(LauncherSound::Select);
    }
    const char* hide = portTrLabel("Don't show again##pm64_card_hide");
    if (ImGui::GetItemRectMax().x + gap + buttonW(hide) <= left + width) {
      ImGui::SameLine(0, gap);
    }
    if (launcherButton(hide, ImVec2(0, 0), scale)) {
      launcherPm64CardHide();
      sMore = false;
      launcherSound(LauncherSound::Back);
    }
    ImGui::Dummy(ImVec2(0, 4.f * scale));
    ImGui::Unindent(10.f * scale);
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(ImVec2(left - pad, cardTop), ImVec2(left + width + pad, ImGui::GetCursorScreenPos().y),
                      IM_COL32(246, 196, 69, 46), 8.f * scale);
    dl->AddRect(ImVec2(left - pad, cardTop), ImVec2(left + width + pad, ImGui::GetCursorScreenPos().y),
                IM_COL32(190, 140, 40, 140), 8.f * scale, 0, std::max(1.f, 1.5f * scale));
    dl->ChannelsMerge();
  }
  // how the last try went (once linked, the row's Linked says it); linked with the partners off: where to
  // turn them on
  std::string text = v == PM64_NONE ? said : "";
  const int* partners = portSettingsFind("pm64_partners");
  if (!portPresetPm64AskPending() && v != PM64_NONE && (partners == nullptr || *partners == 0)) {
    text += text.empty() ? "" : " ";
    text += portTr("Turn on Paper Mario 64 partners in Settings to meet them.");
  }
  if (!text.empty()) {
    note(text.c_str());
  }
  l.pm64H = ImGui::GetCursorScreenPos().y - top;
}

// stacked (a narrow stage): under the details, in the rest of the space;
// else the left of the row
void drawLibrary(Launcher& l, float scale, bool stacked) {
  const PortVersionInfo* self = portVersionSelf();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 size = stacked ? ImVec2(0, 0) : ImVec2(std::max(300.f * scale, avail.x * 0.44f), 0);
  beginCard("library", size, scale);
  launcherHeading(portTr("Your discs"), scale);
  ImGui::Spacing();
  if (l.discs.empty()) {
    ImGui::TextWrapped("%s", portTr("None yet. Add the disc image of your copy of the game (.iso, .rvz, .gcm...)."));
    ImGui::Spacing();
  }
  // the list scrolls between the heading and the buttons
  const float buttonsH = ImGui::GetFrameHeight() + 26.f * scale + l.pm64H; // (and the Paper Mario 64 entry)
  const bool listed = !l.discs.empty();
  if (listed) {
    ImGui::BeginChild("discs", ImVec2(0, std::max(ImGui::GetTextLineHeight() * 3.f,
                                                  ImGui::GetContentRegionAvail().y - buttonsH)));
  }
  const float rowH = ImGui::GetTextLineHeightWithSpacing() * 2.f + 8.f * scale;
  for (size_t i = 0; i < l.discs.size(); i++) {
    Disc& d = l.discs[i];
    const char* label = d.version != nullptr ? portTr(d.version->label) : (d.exists ? portTr("Not the game") : portTr("Missing"));
    ImGui::PushID(static_cast<int>(i));
    const bool playable = d.version == self || (d.version != nullptr && d.version->ported);
    const bool chosen = launcherRow("##disc", l.selected == static_cast<int>(i), rowH, scale);
    if (l.focusList && l.selected == static_cast<int>(i)) {
      ImGui::FocusItem(); // not SetKeyboardFocusHere, which would also press it
      l.focusList = false;
    }
    const ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
    if (chosen) {
      if (l.selected != static_cast<int>(i)) {
        lookArt(l, static_cast<int>(i));
      }
      l.selected = static_cast<int>(i);
      l.status.clear();
      // a double click, or the controller's confirm button, plays it
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) || l.pad) {
        play(l, l.selected);
      }
    }
    if (l.pad && ImGui::IsItemFocused() && l.selected != static_cast<int>(i)) {
      l.selected = static_cast<int>(i); // with a controller the disc under the focus is the chosen one
      l.status.clear();
    }
    // the banner, the region's tag on its corner, then the version and the file's name
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // (at most two fifths of the row: the names keep their room on a phone)
    const float bannerW = std::min((rowMax.y - rowMin.y - 10.f * scale) * 3.f,
                                   (rowMax.x - ImGui::GetCursorScreenPos().x) * 0.4f);
    const float bannerTop = rowMin.y + (rowMax.y - rowMin.y - bannerW / 3.f) * 0.5f;
    const ImVec2 bannerAt(ImGui::GetCursorScreenPos().x, bannerTop);
    drawBanner(dl, d, bannerAt, bannerW, scale);
    if (d.version != nullptr) {
      launcherRegionTag(dl, ImVec2(bannerAt.x + bannerW - 16.f * scale, bannerAt.y + bannerW / 3.f - 14.f * scale),
                        regionOf(d.version), 18.f * scale);
    }
    ImGui::SetCursorScreenPos(ImVec2(bannerAt.x + bannerW + 22.f * scale,
                                     (rowMin.y + rowMax.y) * 0.5f - ImGui::GetTextLineHeightWithSpacing() +
                                         ImGui::GetStyle().ItemSpacing.y * 0.5f));
    ImGui::BeginGroup();
    if (playable) {
      ImGui::TextUnformatted(label);
    } else {
      ImGui::TextDisabled("%s", label);
    }
    ImGui::TextDisabled("%s", fit(d.name, rowMax.x - ImGui::GetCursorScreenPos().x - 6.f * scale).c_str());
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y));
    ImGui::Dummy(ImVec2(0, 2.f * scale));
    ImGui::PopID();
  }
  if (listed) {
    ImGui::EndChild();
  }
  ImGui::Spacing();
  const bool addPressed =
      launcherButton(l.discs.empty() ? portTr("Add a disc image...") : portTr("Add another..."), ImVec2(0, 0), scale,
                     l.discs.empty() ? LauncherButton::Primary : LauncherButton::Normal);
  if (l.discs.empty()) {
    launcherPoint(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), true); // the glove: start here
  }
  if (l.focusList && l.selected < 0) {
    ImGui::FocusItem();
    l.focusList = false;
  }
  if (addPressed || (l.pad && pageKey(ImGuiKey_GamepadFaceUp))) {
    pickAdd(l, !addPressed || l.pad); // (a controller's: not the dialog in Steam Deck's game mode)
  }
  if (l.selected >= 0) {
    // beside Add, or under it where the window is narrow
    const float removeW = ImGui::CalcTextSize(portTr("Remove from list")).x + 52.f * scale;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (right - ImGui::GetItemRectMax().x - 14.f * scale > removeW) {
      ImGui::SameLine(0, 14.f * scale);
    } else {
      ImGui::Dummy(ImVec2(0, 2.f * scale));
    }
    if (launcherButton(portTrLabel("Remove from list"), ImVec2(0, 0), scale)) {
      l.discs.erase(l.discs.begin() + l.selected);
      l.selected = std::min(l.selected, static_cast<int>(l.discs.size()) - 1);
      save(l);
      launcherSound(LauncherSound::Back);
    }
  }
  drawPm64(l, scale);
  ImGui::EndChild();
}

// The guest's co-op save copy (port/include/port/together.h): a co-op guest's copy of the host's adventure
// not taken yet, offered before the game (the game's file menu offers it too). A free file takes it at once;
// a file in use only after a second press (its save stays in Save files). Not now: the next launch asks
// again; Don't ask again: let go. Without a memory card yet, only the game's file menu offers it.
void drawCoopCopy(float buttonWidth, float indent, float scale) {
  static Uint64 sAt;
  static bool sPending;
  static std::vector<unsigned char> sImage(PORT_CARD_SLOT_SIZE);
  static char sHost[96];
  static int sConfirm = -1;
  static std::string sDone;
  if (!sDone.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", sDone.c_str());
  }
  if (portCoopCopyLaterAsked()) {
    return;
  }
  if (sAt == 0 || SDL_GetTicks() - sAt > 2000) {
    sAt = SDL_GetTicks();
    sPending = portCoopCopyPending(sHost, sizeof(sHost), sImage.data()) != 0;
  }
  const CardFiles& card = cardNow();
  if (!sPending || card.gci.empty()) {
    return;
  }
  char text[256];
  ImGui::Spacing();
  ImGui::Spacing();
  std::snprintf(text, sizeof(text), portTr("Keep your co-op save with %s?"), sHost);
  launcherHeading(text, scale);
  note(portTr("Your adventure together, as the host's game last kept it: put it into one of your files to play on "
              "alone."));
  for (int i = 0; i < 4; i++) {
    std::string label;
    if (!card.info[i].valid) {
      std::snprintf(text, sizeof(text), portTr("File %d: empty"), i + 1);
      label = text;
    } else if (sConfirm == i) {
      std::snprintf(text, sizeof(text), portTr("Replace File %d: press again"), i + 1);
      label = text;
    } else {
      label = saveLabel(i, card.info[i]);
    }
    label += "##coopcopy" + std::to_string(i);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    if (launcherButton(label.c_str(), ImVec2(buttonWidth - indent, 0), scale,
                       !card.info[i].valid ? LauncherButton::Primary : LauncherButton::Normal)) {
      if (card.info[i].valid && sConfirm != i) {
        sConfirm = i; // (never over a save without asking)
        continue;
      }
      char why[192];
      sConfirm = -1;
      if (portSaveFilesPutImage(sImage.data(), i, why, sizeof(why))) {
        portAchievementsCoopCopyKept(i); // (its achievements go with it)
        portCoopCopyDone();
        sPending = false;
        std::snprintf(text, sizeof(text), portTr("Kept in File %d."), i + 1);
        sDone = text;
        sAt = SDL_GetTicks();
      } else {
        sDone = portTr(why);
      }
    }
  }
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
  if (launcherButton(portTrLabel("Not now##coopcopy"), ImVec2(0, 0), scale)) {
    portCoopCopyLater();
    sConfirm = -1;
  }
  ImGui::SameLine();
  if (launcherButton(portTrLabel("Don't ask again##coopcopy"), ImVec2(0, 0), scale)) {
    portCoopCopyDone();
    sPending = false;
    sConfirm = -1;
  }
}

// An invite link (port/include/port/together.h): online play is the game's Multiplayer page's alone (hosting a
// room, joining by code or by address, the room list, invites, the room's settings); the launcher only starts
// the game on that page. Disc i: this build's, with the Multiplayer page asked for (portTitleOpenMultiplayer);
// another version's, its build ($TTYD_OPEN_MULTIPLAYER, and an invite link's room in $TTYD_ONLINE_INVITE).
// An invite's room is offered on the page ("Join the invite's room"), never joined by itself.
bool startOnMultiplayer(Launcher& l, int i) {
  const Disc& d = l.discs[i];
  std::printf("[launcher] an invite: the game starts on its Multiplayer page\n");
  std::fflush(stdout);
  if (d.version == portVersionSelf()) {
    portTitleOpenMultiplayer();
    return play(l, i);
  }
  SDL_Environment* env = SDL_GetEnvironment();
  SDL_SetEnvironmentVariable(env, "TTYD_OPEN_MULTIPLAYER", "1", true);
  char invite[16];
  const bool setInvite = portOnlinePeekInvite(invite, sizeof(invite)) != 0;
  if (setInvite) {
    SDL_SetEnvironmentVariable(env, "TTYD_ONLINE_INVITE", invite, true);
  }
  if (play(l, i)) {
    return true;
  }
  // not started: a later Play of another version from here mustn't open on the page, or offer this room
  SDL_UnsetEnvironmentVariable(env, "TTYD_OPEN_MULTIPLAYER");
  if (setInvite) {
    SDL_UnsetEnvironmentVariable(env, "TTYD_ONLINE_INVITE");
  }
  return false;
}

// An invite link (invite.cpp) while the launcher is up: the chosen disc starts on the Multiplayer page, its
// room offered there. No disc that plays yet: the Play tab says so, and once one plays it starts there.
void inviteFrame(Launcher& l) {
  static std::string sDone;
  char invite[16];
  if (!portOnlinePeekInvite(invite, sizeof(invite)) || sDone == invite || portCrashReportPending()) {
    return;
  }
  if (l.selected < 0 || l.selected >= static_cast<int>(l.discs.size()) || !playProblem(l.discs[l.selected]).empty()) {
    return;
  }
  sDone = invite;
  std::printf("[launcher] an invite link: room %.4s-%s\n", invite, invite + 4);
  std::fflush(stdout);
  startOnMultiplayer(l, l.selected);
}

bool sNewAsk = SDL_getenv("TTYD_LAUNCHER_NEW") != nullptr; // the Play card's New adventure (no save yet): where
                                                           // does it start? ($TTYD_LAUNCHER_NEW: asked at once)

// size: the card's (0: the rest of the row or column)
void drawDetails(Launcher& l, float scale, ImVec2 size = ImVec2(0, 0), float time = 0.f) {
  beginCard("details", size, scale);
  if (l.selected < 0 || l.selected >= static_cast<int>(l.discs.size())) {
    launcherHeading(portTr("Welcome!"), scale);
    ImGui::Spacing();
    ImGui::TextWrapped("%s", portTr("Add the disc image of your copy of the game to play. The list remembers it, and each "
                       "version (Japanese, US, European) plays with its own build of the port."));
    char invite[16];
    if (portOnlinePeekInvite(invite, sizeof(invite))) {
      ImGui::Spacing();
      ImGui::TextWrapped("%s", portTr("An invite link came: once your disc is added, the game opens on its "
                                      "Multiplayer page, the room offered there."));
    }
    if (!l.status.empty()) {
      ImGui::Spacing();
      ImGui::TextWrapped("%s", l.status.c_str()); // (Add's: no file dialog, the discs folder instead)
    }
    ImGui::EndChild();
    return;
  }
  Disc& d = l.discs[l.selected];
  // the disc's banner, large, its region's tag on the corner
  if (d.version != nullptr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // (a quarter of the window's height at most: Play stays in sight on a phone)
    // (smaller in a short window, a phone held sideways: Play stays in sight)
    const bool shortCard = ImGui::GetWindowHeight() < 440.f * scale;
    const float w = std::min({ImGui::GetContentRegionAvail().x, 288.f * scale,
                              ImGui::GetWindowHeight() * (shortCard ? 0.45f : 0.75f)});
    const ImVec2 at = ImGui::GetCursorScreenPos();
    drawBanner(dl, d, at, w, scale);
    launcherRegionTag(dl, ImVec2(at.x + w - 22.f * scale, at.y + w / 3.f - 18.f * scale), regionOf(d.version),
                      26.f * scale);
    ImGui::Dummy(ImVec2(w, w / 3.f + 10.f * scale));
    std::string name = portTr(d.version->name);
    if (std::islower(static_cast<unsigned char>(name[0]))) {
      name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    launcherHeading(name.c_str(), scale);
    ImGui::TextDisabled(portTr("Disc ID %s"), d.version->id);
  } else {
    launcherHeading(d.exists ? portTr("Unknown disc image") : portTr("Missing file"), scale);
  }
  ImGui::Spacing();
  if (ImGui::GetWindowHeight() >= 440.f * scale) { // (a short window: the file's path only in the list)
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    // (Android's content:// URI says nothing to the player: the file's name instead)
    ImGui::TextWrapped("%s", d.name != fileName(d.path) ? d.name.c_str() : d.path.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }
  ImGui::Spacing();
  const std::string problem = playProblem(d);
  ImGui::BeginDisabled(!problem.empty());
  SDL_Time resumed = 0;
  const bool canContinue = problem.empty() && !resumeFile(d, &resumed).empty();
  const float buttonWidth = std::min(ImGui::GetContentRegionAvail().x - 8.f * scale, 300.f * scale);
  // the glove's room at the buttons' left
  const float indent = 34.f * scale;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
  if (canContinue) {
    // quick resume's save: Continue first (Start too), the title below it
    if (launcherButton(portTrLabel("Continue"), ImVec2(buttonWidth - indent, 0), scale, LauncherButton::Primary) ||
        pageKey(ImGuiKey_GamepadStart)) {
      continueGame(l, l.selected);
    }
    SDL_DateTime t;
    if (SDL_TimeToDateTime(resumed, &t, true)) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
      ImGui::TextDisabled(portTr("Where you left off, %04d-%02d-%02d %02d:%02d"), t.year, t.month, t.day, t.hour, t.minute);
    }
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    if (launcherButton(portTrLabel("Start from the title"), ImVec2(buttonWidth - indent, 0), scale)) {
      play(l, l.selected);
    }
  } else if (problem.empty() && d.version == portVersionSelf() && !cardNow().any && cardNow().firstFree >= 0) {
    // no save yet: a new adventure straight away (its file the card's first, no file menu), or the title
    bool& sAsk = sNewAsk;
    if (!sAsk) {
      if (launcherButton(portTrLabel("New adventure"), ImVec2(buttonWidth - indent, 0), scale, LauncherButton::Primary) ||
          pageKey(ImGuiKey_GamepadStart)) {
        sAsk = rogueportMode() == 1;
        if (!sAsk) {
          portBootRequestNew(cardNow().firstFree, rogueportMode() == 2);
          play(l, l.selected);
        }
      }
    } else {
      const int pick = newFileChoice(scale);
      if (pick >= 0) {
        sAsk = false;
        portBootRequestNew(cardNow().firstFree, pick);
        play(l, l.selected);
      }
      // the question taken back: its Back, or the way back (B, Escape, Android's back)
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
      if (sAsk && (launcherButton(portTrLabel("Back##new_ask"), ImVec2(0, 0), scale) || takeBack())) {
        sAsk = false;
        launcherSound(LauncherSound::Back);
      }
    }
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    if (launcherButton(portTrLabel("Start from the title"), ImVec2(buttonWidth - indent, 0), scale)) {
      play(l, l.selected);
    }
  } else {
    const char* label = d.version != nullptr && d.version != portVersionSelf() ? portTrLabel("Play (starts its build)")
                                                                               : portTrLabel("Play");
    if (launcherButton(label, ImVec2(buttonWidth - indent, 0), scale, LauncherButton::Primary) ||
        (problem.empty() && pageKey(ImGuiKey_GamepadStart))) {
      play(l, l.selected);
    }
  }
  ImGui::EndDisabled();
  if (!problem.empty()) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", problem.c_str());
  }
  if (!l.status.empty() && l.status != problem) {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", l.status.c_str());
  }
  if (problem.empty() && d.version == portVersionSelf()) {
    drawCoopCopy(buttonWidth, indent, scale); // (a co-op guest's copy of the host's save, offered)
  }
  ImGui::Spacing();
  ImGui::Spacing();
  PortSettings* s = portSettings();
  bool skip = s->launcherSkip != 0;
  if (ImGui::Checkbox(portTrLabel("Skip to this disc next time"), &skip)) {
    s->launcherSkip = skip ? 1 : 0;
    portSettingsSave();
  }
  note(portTr("To see this screen again: Settings > General in the game."));
  ImGui::EndChild();
}

void drawAbout(Launcher& l, float scale) {
  (void)l;
  const PortVersionInfo* self = portVersionSelf();
  beginCard("about", ImVec2(0, 0), scale);
  launcherHeading(portTr("About"), scale);
  ImGui::Spacing();
  ImGui::TextWrapped("%s", portTr("Paper Cut plays Paper Mario: The Thousand-Year Door as a native program, built from the "
                     "game's C decompilation, with no emulator. You need your own copy of the game: a disc image "
                     "of the Japanese, US or European release."));
  ImGui::Spacing();
  // the app's icon beside its version
  if (const ImTextureID icon = launcherBrandIconTexture(); icon != ImTextureID{}) {
    const float side = 64.f * scale;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddImage(icon, at, ImVec2(at.x + side, at.y + side));
    ImGui::Dummy(ImVec2(side, side));
    ImGui::SameLine(0.f, 14.f * scale);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (side - ImGui::GetTextLineHeightWithSpacing() * 2.f) * 0.5f);
  }
  ImGui::BeginGroup();
  ImGui::Text(portTr("Paper Cut %s"), PORT_VERSION);
  ImGui::Text(portTr("This build: %s (%s)"), portTr(self->name), self->version);
  ImGui::EndGroup();
  portUpdateDrawNotice(); // a newer release, when the update check found one
  ImGui::Spacing();
  if (launcherButton(portTrLabel("What's new##about_news"), ImVec2(0, 0), scale)) {
    launcherNewsOpen(); // the card the first launch after an update shows (launcher_onboard.h)
  }
  ImGui::SameLine(0, 12.f * scale);
  if (launcherButton(portTrLabel("How multiplayer works##about_help"), ImVec2(0, 0), scale)) {
    launcherHelpOpen();
  }
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
  // Quit: the app closed, as its window's close does (a controller alone, Steam Deck's game mode, has no other)
  ImGui::Spacing();
  if (launcherButton(portTrLabel("Quit##about_quit"), ImVec2(0, 0), scale)) {
    std::printf("[launcher] Quit\n");
    std::fflush(stdout);
    portSettingsSave();
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit); // (Aurora's exit: portAuroraBeginFrame ends the program)
  }
#endif
  ImGui::Spacing();
  launcherHeading(portTr("Licences"), scale);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Paper Cut is under the MIT licence. It is built with: Aurora (MIT), SDL 3 (zlib), Dawn "
                     "(BSD 3-Clause), nod (MIT), Dear ImGui (MIT), FreeType (FreeType License), libpng (libpng "
                     "License), zlib (zlib), Zstandard (BSD 3-Clause), xxHash (BSD 2-Clause), {fmt} (MIT), Abseil "
                     "(Apache 2.0), Tracy (BSD 3-Clause), SQLite (public domain), MusyX (MIT), SGI's GLU "
                     "tessellator (SGI Free Software License B 2.0), and the Fredoka typeface (SIL Open Font "
                     "License 1.1)."));
  ImGui::TextWrapped("%s", portTr("Your music is read with stb_vorbis (public domain), dr_mp3 and dr_flac (public "
                                  "domain or MIT No Attribution)."));
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::TextWrapped("%s", portTr("The licence texts are in THIRD_PARTY.txt of Paper Cut's desktop packages and in its "
                     "source (docs/THIRD_PARTY.md)."));
#else
  ImGui::TextWrapped("%s", portTr("The licence texts are in THIRD_PARTY.txt and OFL.txt beside Paper Cut."));
#endif
  ImGui::PopStyleColor();
  ImGui::EndChild();
}

enum PadButton { kConfirm, kBack, kTop, kStart, kShoulders, kTriggers };

// One button's glyph with its top left at `at`, in the controller's own
// markings; returns its width (dl nullptr: only measured).
float padGlyph(ImDrawList* dl, PadButton b, SDL_GamepadType type, ImVec2 at, float h) {
  const float r = h * 0.5f;
  const ImVec2 c(at.x + r, at.y + r);
  ImFont* font = gLauncherFonts.heading;
  const float size = h * 0.72f;
  auto letter = [&](const char* text, ImU32 bg) {
    if (dl == nullptr) {
      return h;
    }
    dl->AddCircleFilled(c, r, bg);
    const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    dl->AddText(font, size, ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), IM_COL32(255, 255, 255, 255), text);
    return h;
  };
  auto pill = [&](const char* text) {
    const ImVec2 sz = font->CalcTextSizeA(size * 0.8f, FLT_MAX, 0.f, text);
    const float w = std::max(h * 1.3f, sz.x + h * 0.6f);
    if (dl == nullptr) {
      return w;
    }
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(255, 247, 228, 255), r);
    dl->AddText(font, size * 0.8f, ImVec2(at.x + (w - sz.x) * 0.5f, c.y - sz.y * 0.5f), kInk, text);
    return w;
  };
  if (b == kShoulders || b == kTriggers) {
    const bool n = nintendoPad(type), p = playstationPad(type);
    const char* left = b == kShoulders ? (n ? "L" : p ? "L1" : "LB") : (n ? "ZL" : p ? "L2" : "LT");
    const char* right = b == kShoulders ? (n ? "R" : p ? "R1" : "RB") : (n ? "ZR" : p ? "R2" : "RT");
    const float w = pill(left) + h * 0.2f;
    at.x += w;
    return w + pill(right);
  }
  if (b == kStart) {
    return pill(nintendoPad(type) ? "+" : playstationPad(type) ? "Options" : "Menu");
  }
  // confirm and back: the controller's own (its labels; a GameCube's A and B; the swap setting)
  // ($TTYD_LAUNCHER_PAD with none plugged in: the kind's, Nintendo's A on the right)
  SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_NORTH;
  if (b == kConfirm || b == kBack) {
    const bool confirm = (b == kConfirm) != (portMenuPadId() == 0 && portMenuSwapped() != 0);
    if (portMenuPadId() != 0) {
      button = static_cast<SDL_GamepadButton>(portMenuRoleButton(b == kConfirm ? PORT_MENU_CONFIRM : PORT_MENU_BACK, 0));
    } else {
      button = (confirm == nintendoPad(type)) ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
    }
  }
  if (dl == nullptr) {
    return h; // measuring
  }
  const ImU32 dark = IM_COL32(52, 40, 34, 255);
  const float k = r * 0.45f;
  switch (SDL_GetGamepadButtonLabelForType(type, button)) {
  case SDL_GAMEPAD_BUTTON_LABEL_A:
    return letter("A", IM_COL32(84, 170, 72, 255));
  case SDL_GAMEPAD_BUTTON_LABEL_B:
    return letter("B", IM_COL32(214, 64, 56, 255));
  case SDL_GAMEPAD_BUTTON_LABEL_X:
    return letter("X", IM_COL32(60, 118, 214, 255));
  case SDL_GAMEPAD_BUTTON_LABEL_Y:
    return letter("Y", IM_COL32(226, 176, 34, 255));
  case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
    dl->AddCircleFilled(c, r, dark);
    dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), IM_COL32(124, 166, 232, 255), h * 0.1f);
    dl->AddLine(ImVec2(c.x + k, c.y - k), ImVec2(c.x - k, c.y + k), IM_COL32(124, 166, 232, 255), h * 0.1f);
    return h;
  case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
    dl->AddCircleFilled(c, r, dark);
    dl->AddCircle(c, k, IM_COL32(232, 96, 100, 255), 0, h * 0.1f);
    return h;
  case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
    dl->AddCircleFilled(c, r, dark);
    dl->AddRect(ImVec2(c.x - k * 0.85f, c.y - k * 0.85f), ImVec2(c.x + k * 0.85f, c.y + k * 0.85f),
                IM_COL32(222, 130, 196, 255), 0.f, 0, h * 0.1f);
    return h;
  case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
    dl->AddCircleFilled(c, r, dark);
    dl->AddTriangle(ImVec2(c.x, c.y - k), ImVec2(c.x + k, c.y + k * 0.75f), ImVec2(c.x - k, c.y + k * 0.75f),
                    IM_COL32(96, 200, 160, 255), h * 0.1f);
    return h;
  default:
    return letter(b == kConfirm ? "A" : b == kBack ? "B" : "Y", dark);
  }
}

// The controller's buttons for this screen, along the bottom of the stage.
void drawPrompts(const Launcher& l, ImVec2 stageMin, ImVec2 stageMax, float scale) {
  struct Prompt {
    PadButton button;
    const char* text;
  };
  std::vector<Prompt> prompts;
  if (portBindingsAsking()) {
    // a binding being asked for (Settings, Bindings): the answer, and the way out with a controller
    prompts.push_back({kStart, portTr("Hold: leave it as it is")});
  } else if (l.tab == 0 && sNewAsk) {
    prompts.push_back({kConfirm, portTr("Choose")}); // (a question on the Play card: its way back)
    prompts.push_back({kBack, portTr("Back")});
  } else if (l.tab == 0) {
    if (!l.discs.empty()) {
      prompts.push_back({kConfirm, portTr("Play")});
    }
    prompts.push_back({kTop, portTr("Add a disc")});
  } else {
    prompts.push_back({kConfirm, portTr("Choose")});
    prompts.push_back({kBack, portTr("Back")});
  }
  if (l.tab == kTabSettings && !portBindingsAsking()) { // Settings
    prompts.push_back({kTriggers, portTr("Sections")});
  }
  if (!portBindingsAsking()) {
    prompts.push_back({kShoulders, portTr("Tabs")});
  }
  const SDL_GamepadType type = padType();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = gLauncherFonts.body;
  float h = 26.f * scale;
  float textSize = font->FontSize * ImGui::GetIO().FontGlobalScale;
  float gap = 8.f * scale, between = 26.f * scale;
  const auto rowWidth = [&]() {
    float w = 0.f;
    for (const Prompt& p : prompts) {
      w += padGlyph(nullptr, p.button, type, ImVec2(0.f, 0.f), h) + gap +
           font->CalcTextSizeA(textSize, FLT_MAX, 0.f, p.text).x + between;
    }
    return w - between;
  };
  // the row fits the stage (a phone held upright, a long translation): made
  // smaller, down to two thirds, then the last prompts left out
  const float avail = stageMax.x - stageMin.x;
  float width = rowWidth();
  if (width > avail && width > 0.f) {
    const float fit = std::max(2.f / 3.f, avail / width);
    h *= fit;
    textSize *= fit;
    gap *= fit;
    between *= fit;
    width = rowWidth();
    while (width > avail && prompts.size() > 1) {
      prompts.pop_back();
      width = rowWidth();
    }
  }
  ImVec2 at((stageMin.x + stageMax.x - width) * 0.5f, stageMax.y - h);
  for (const Prompt& p : prompts) {
    at.x += padGlyph(dl, p.button, type, at, h) + gap;
    const ImVec2 sz = font->CalcTextSizeA(textSize, FLT_MAX, 0.f, p.text);
    dl->AddText(font, textSize, ImVec2(at.x, at.y + (h - sz.y) * 0.5f), IM_COL32(255, 247, 228, 255), p.text);
    at.x += sz.x + between;
  }
}

// $TTYD_LAUNCHER_TAB (tests, screenshots): the tab of that name at start, or "NAME@SECONDS" then
int startTab(float time) {
  static const char* want = SDL_getenv("TTYD_LAUNCHER_TAB");
  static const char* const kNames[kTabCount] = {"Play", "Settings", "Mods", "About"};
  if (want == nullptr) {
    return -1;
  }
  const char* at = std::strchr(want, '@');
  if (at != nullptr && time < static_cast<float>(std::atof(at + 1))) {
    return -1;
  }
  const size_t n = at != nullptr ? static_cast<size_t>(at - want) : std::strlen(want);
  // ("Online", the Multiplayer tab before online play moved into the game: Play)
  int found = n == 6 && std::strncmp(want, "Online", 6) == 0 ? kTabPlay : -1;
  for (int i = 0; i < kTabCount; i++) {
    if (std::strlen(kNames[i]) == n && std::strncmp(want, kNames[i], n) == 0) {
      found = i;
    }
  }
  want = nullptr;
  return found;
}

// $TTYD_LAUNCHER_ADD="SECONDS:PATH" (tests, screenshots): that disc image added then, as if picked
// (a Paper Mario 64 ROM: linked, as Add links one)
const char* sTestAdd = SDL_getenv("TTYD_LAUNCHER_ADD"); // (until it is added)
void testAdd(Launcher& l, float time) {
  const char*& add = sTestAdd;
  if (add == nullptr) {
    return;
  }
  const char* colon = std::strchr(add, ':');
  if (colon != nullptr && time >= static_cast<float>(std::atof(add))) {
    takePicked(l, colon + 1);
    add = nullptr;
  }
}

} // namespace

// tests that drive the launcher ($TTYD_PADBIND_TEST): $TTYD_LAUNCHER_ADD's disc not added yet (adding it shows Play)
extern "C" int portLauncherTestAddPending(void) { return sTestAdd != nullptr; }

namespace {

// The presets' pictures for the launcher (enhancement_presets.h): made from the chosen disc into its art
// cache once per version (art/recipes/launcher_presets.recipe), then read from there
void presetIcons(Launcher& l) {
  static std::string sMadeFor;
  if (l.selected < 0 || l.selected >= static_cast<int>(l.discs.size()) || l.discs[l.selected].version == nullptr) {
    return;
  }
  const Disc& d = l.discs[l.selected];
  if (sMadeFor == d.version->id) {
    return;
  }
  sMadeFor = d.version->id;
  portArtMakeFolder(l.userPath.c_str(), d.path.c_str(), "art/launcher/presets/");
  const bool slash = !l.userPath.empty() && (l.userPath.back() == '/' || l.userPath.back() == '\\');
  portPresetIconDir((l.userPath + (slash ? "" : "/") + "art_cache/" + d.version->id + "/art/launcher/presets/").c_str());
}

// text in font at size, centred across width w from at.x, wrapped there (each line centred); its height
float centredWrapped(ImDrawList* dl, ImFont* font, float size, ImVec2 at, float w, ImU32 col, const char* text,
                     bool draw = true) {
  const char* end = text + std::strlen(text);
  float y = at.y;
  for (const char* s = text; s < end;) {
    const char* cut = font->CalcWordWrapPositionA(size / font->FontSize, s, end, w);
    if (cut == s) {
      cut = s + 1; // (a word wider than the line: at least a letter a line)
    }
    const char* lineEnd = cut;
    while (lineEnd > s && (lineEnd[-1] == ' ' || lineEnd[-1] == '\n')) {
      lineEnd--;
    }
    if (draw) {
      const float lw = font->CalcTextSizeA(size, FLT_MAX, 0.f, s, lineEnd).x;
      dl->AddText(font, size, ImVec2(at.x + (w - lw) * 0.5f, y), col, s, lineEnd);
    }
    y += size * 1.15f;
    s = cut;
    while (s < end && (*s == ' ' || *s == '\n')) {
      s++;
    }
  }
  return y - at.y;
}

// A card's layout: the picture over the text, or (across: a short wide card) beside it, the text at k
// times its full size. Its picture's size, the text's width and height; true when it fits (the name on
// one line, the tagline on two at most, all inside the card).
struct CardText {
  float icon = 0.f, textW = 0.f, textH = 0.f, hs = 0.f, bs = 0.f;
};
bool cardLayout(int p, ImVec2 size, float scale, bool across, float k, CardText* out) {
  ImGuiIO& io = ImGui::GetIO();
  ImFont* head = gLauncherFonts.heading;
  ImFont* body = gLauncherFonts.body;
  const float pad = 10.f * scale;
  out->hs = head->FontSize * io.FontGlobalScale * k;
  out->bs = body->FontSize * io.FontGlobalScale * 0.92f * k;
  out->icon = across ? std::min({size.y - pad * 2.f, size.x * 0.3f, 72.f * scale})
                     : std::min({size.x * 0.42f, 72.f * scale, size.y * 0.4f});
  out->textW = across ? size.x - out->icon - pad * 3.f : size.x - pad * 2.f;
  const float nameW = head->CalcTextSizeA(out->hs, FLT_MAX, 0.f, portTr(portPresetName(p))).x;
  const float tagH = centredWrapped(nullptr, body, out->bs, ImVec2(0, 0), out->textW, 0, portTr(portPresetTagline(p)),
                                    false);
  out->textH = out->hs * 1.1f + 4.f * scale + tagH;
  const float room = across ? size.y - pad * 2.f : size.y - pad * 2.f - out->icon - 6.f * scale;
  return nameW <= out->textW && tagH <= out->bs * 1.15f * 2.f + 0.5f && out->textH <= room;
}

// the largest text size (of 1, 0.96 ... 0.44) at which every one of the cards fits in that layout
float cardsFit(const std::vector<int>& cards, ImVec2 size, float scale, bool across) {
  CardText t;
  for (float k = 1.f; k > 0.42f; k -= 0.04f) {
    bool all = true;
    for (int p : cards) {
      all = all && cardLayout(p, size, scale, across, k, &t);
    }
    if (all) {
      return k;
    }
  }
  return 0.42f;
}

// One preset's card in the first-run picker: its picture, its name and its tagline, laid out as the picker
// chose for all of them (the same size of text on every card). Returns true when chosen; *over set to p
// while the pointer or the focus is on it.
bool presetCard(int p, ImVec2 size, float scale, bool across, float k, int* over) {
  ImGui::PushID(p);
  const bool can = portPresetAvailable(p);
  ImGui::BeginDisabled(!can);
  const bool pressed = ImGui::InvisibleButton("##card", size, ImGuiButtonFlags_EnableNav);
  ImGui::EndDisabled();
  const bool lit = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused();
  ImGui::PopID();
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  if (lit) {
    *over = p;
    if (can) {
      launcherPoint(a, b);
    }
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = 12.f * scale, line = 2.5f * scale;
  dl->AddRectFilled(ImVec2(a.x + 3.f * scale, a.y + 5.f * scale), ImVec2(b.x + 3.f * scale, b.y + 5.f * scale),
                    IM_COL32(20, 10, 30, 70), r);
  dl->AddRectFilled(ImVec2(a.x - line, a.y - line), ImVec2(b.x + line, b.y + line),
                    can ? IM_COL32(86, 40, 30, 255) : IM_COL32(150, 136, 120, 255), r + line);
  dl->AddRectFilled(a, b, !can ? IM_COL32(226, 218, 204, 255) : lit ? IM_COL32(254, 228, 160, 255)
                                                                     : IM_COL32(250, 242, 222, 255), r);
  dl->AddLine(ImVec2(a.x + r, a.y + 3.f * scale), ImVec2(b.x - r, a.y + 3.f * scale), IM_COL32(255, 255, 255, 170),
              1.5f * scale);

  ImFont* head = gLauncherFonts.heading;
  ImFont* body = gLauncherFonts.body;
  const char* name = portTr(portPresetName(p));
  const char* tag = portTr(portPresetTagline(p));
  const float pad = 10.f * scale;
  const ImU32 ink = can ? IM_COL32(59, 36, 22, 255) : IM_COL32(140, 126, 112, 255);
  const ImU32 soft = can ? IM_COL32(110, 84, 62, 255) : IM_COL32(150, 136, 120, 255);
  CardText t;
  cardLayout(p, size, scale, across, k, &t);
  if (across) {
    portPresetDrawIcon(dl, p, ImVec2(a.x + pad, a.y + (size.y - t.icon) * 0.5f), t.icon, !can);
    const float x = a.x + pad * 2.f + t.icon, y = a.y + (size.y - t.textH) * 0.5f;
    centredWrapped(dl, head, t.hs, ImVec2(x, y), t.textW, ink, name);
    centredWrapped(dl, body, t.bs, ImVec2(x, y + t.hs * 1.1f + 4.f * scale), t.textW, soft, tag);
  } else {
    // the picture and the text together, in the card's middle
    const float total = t.icon + 6.f * scale + t.textH;
    const float y = a.y + (size.y - total) * 0.5f;
    portPresetDrawIcon(dl, p, ImVec2(a.x + (size.x - t.icon) * 0.5f, y), t.icon, !can);
    const float ty = y + t.icon + 6.f * scale;
    centredWrapped(dl, head, t.hs, ImVec2(a.x + pad, ty), t.textW, ink, name);
    centredWrapped(dl, body, t.bs, ImVec2(a.x + pad, ty + t.hs * 1.1f + 4.f * scale), t.textW, soft, tag);
  }
  return pressed && can;
}

// how many across, and each card's size, for n cards in room: the largest cards of about the shape
// shape (width over height), the fewest empty places among equals (only: that many across, if it fits)
struct CardGrid {
  int cols = 1;
  ImVec2 card;
};
CardGrid fitCards(int n, ImVec2 room, float gap, float shape, float most, int only = 0) {
  CardGrid best;
  float bestH = -1.f;
  int bestEmpty = 1 << 30;
  for (int c = only > 0 ? only : 1; c <= (only > 0 ? only : n); c++) {
    const int rows = (n + c - 1) / c;
    const float w = (room.x - gap * float(c - 1)) / float(c);
    const float h = (room.y - gap * float(rows - 1)) / float(rows);
    if (w <= 0.f || h <= 0.f) {
      continue;
    }
    const float ch = std::min({h, w / shape, most});
    const int empty = rows * c - n;
    if (ch > bestH + 1.f || (std::fabs(ch - bestH) <= 1.f && empty < bestEmpty)) {
      bestH = ch;
      bestEmpty = empty;
      best.cols = c;
      best.card = ImVec2(std::min(w, ch * 3.2f), ch); // (wide cards set the picture beside the text)
    }
  }
  return best;
}

// A new install's first start: "How do you want to play?" in place of the tabs. The main presets as cards
// (Vanilla, Vanilla+, Remake rules, Enhanced, Story, Challenge, and Paper Mario 64 with a ROM linked: 4 and
// 3 across), each with its picture, name and tagline; the card under the pointer or the focus says its one
// sentence below them. Then More presets (all thirteen as cards) and Choose later. All of it on the one
// page: the cards shrink to fit. Asked once; the presets are under Settings, Gameplay, Presets after.
// Tests: $TTYD_PRESET_PICKER_CHOOSE="SECONDS:NAME" picks one then (a preset's English name, or "later");
// $TTYD_PRESET_PICKER_MORE=1 opens on all of them; $TTYD_PRESET_PICKER_PM64=1 shows Paper Mario 64's card
// without a ROM (dimmed: the layout with seven).
void drawPresetPicker(Launcher& l, float time, float scale, bool headingDone) {
  static bool sMore = SDL_getenv("TTYD_PRESET_PICKER_MORE") != nullptr;
  static const bool sPm64Card = SDL_getenv("TTYD_PRESET_PICKER_PM64") != nullptr;
  static bool sFocus = true; // the first card focused (at the start, and after More)
  static int sDetail = -2;   // the card last under the pointer or the focus (-1 Choose later, -3 More)
  beginCard("preset_picker", ImVec2(0, 0), scale);
  ImFont* body = gLauncherFonts.body;
  const float bodySize = body->FontSize * ImGui::GetIO().FontGlobalScale;
  // a short page (a phone held sideways): the intro beside the heading, the detail beside the buttons
  const bool compact = ImGui::GetContentRegionAvail().y < bodySize * 16.f;
  const char* intro = portTr("Pick a starting point. You can change anything later.");
  if (!headingDone) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const char* heading = portTr("How do you want to play?");
    launcherHeading(heading, scale);
    if (compact) {
      ImFont* head = gLauncherFonts.heading;
      const float headW = head->CalcTextSizeA(head->FontSize * ImGui::GetIO().FontGlobalScale, FLT_MAX, 0.f, heading).x +
                          32.f * scale + 16.f * scale;
      const float introW = ImGui::CalcTextSize(intro).x;
      const float h = ImGui::GetCursorScreenPos().y - at.y;
      if (headW + introW <= ImGui::GetContentRegionAvail().x) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + headW, at.y + (h - bodySize) * 0.5f - 2.f * scale),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), intro);
      }
    } else {
      note(intro);
    }
  }
  // the cards: the picker's own (in the list's order), or all of them as the presets page groups them
  std::vector<int> cards;
  if (sMore) {
    for (int k = 0; portPresetShown(k) >= 0; k++) {
      cards.push_back(portPresetShown(k));
    }
  } else {
    for (int p = 0; p < portPresetCount(); p++) {
      if (portPresetInPicker(p) && (portPresetAvailable(p) || sPm64Card)) {
        cards.push_back(p);
      }
    }
  }
  // the room: what's left, less the detail's two lines and the buttons' row (compact: one row for both)
  const float gap = (compact ? 10.f : 16.f) * scale;
  const float buttonsH = bodySize + 16.f * scale;
  const float detailH = compact ? buttonsH : bodySize * 1.15f * 2.f;
  ImGui::Dummy(ImVec2(0, 2.f * scale));
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 room(avail.x - 8.f * scale,
                    avail.y - (compact ? buttonsH + gap : detailH + buttonsH + gap * 2.f) - 10.f * scale);
  // the main ones in two rows (4 and 3, or 3 and 3), all of them as fits
  const int n = static_cast<int>(cards.size());
  const CardGrid grid = fitCards(n, room, gap, sMore ? 1.5f : 1.2f, 200.f * scale, sMore || n <= 3 ? 0 : (n + 1) / 2);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const int rows = (static_cast<int>(cards.size()) + grid.cols - 1) / grid.cols;
  const float gridH = rows * grid.card.y + (rows - 1) * gap;
  // one layout and one size of text for every card: whichever layout lets that size be the larger
  const float kUp = cardsFit(cards, grid.card, scale, false), kAcross = cardsFit(cards, grid.card, scale, true);
  const bool across = kAcross > kUp + 0.01f;
  const float k = across ? kAcross : kUp;
  int chosen = -2; // -1: later
  int over = -2;
  for (size_t i = 0; i < cards.size(); i++) {
    const int row = static_cast<int>(i) / grid.cols, col = static_cast<int>(i) % grid.cols;
    // each row centred (the last may be shorter)
    const int inRow = std::min(grid.cols, static_cast<int>(cards.size()) - row * grid.cols);
    const float rowW = inRow * grid.card.x + (inRow - 1) * gap;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (avail.x - rowW) * 0.5f + col * (grid.card.x + gap),
                                     origin.y + std::max(0.f, (room.y - gridH) * 0.5f) + row * (grid.card.y + gap)));
    if (presetCard(cards[i], grid.card, scale, across, k, &over)) {
      chosen = cards[i];
    }
    if (i == 0 && sFocus) {
      ImGui::FocusItem(); // the first card, for the keyboard and a controller
      sFocus = false;
    }
  }
  // the detail: the card's one sentence (or the button's), under the cards
  if (sDetail == -2 && !cards.empty()) {
    sDetail = cards[0];
  }
  // the buttons: More presets (or Fewer) and Choose later, side by side in the middle (compact: at the
  // right, the detail at their left)
  const char* moreLabel = sMore ? portTr("Fewer presets") : portTr("More presets...");
  const char* laterLabel = portTr("Choose later");
  const float padX = 22.f * scale;
  const float w1 = ImGui::CalcTextSize(moreLabel, nullptr, true).x + padX * 2.f;
  const float w2 = ImGui::CalcTextSize(laterLabel, nullptr, true).x + padX * 2.f;
  const float bottom = origin.y + room.y + gap;
  const ImVec2 detailAt(origin.x, bottom);
  const float detailW = compact ? avail.x - w1 - w2 - gap * 3.f : avail.x;
  ImGui::SetCursorScreenPos(compact ? ImVec2(origin.x + avail.x - w1 - w2 - gap, bottom)
                                    : ImVec2(origin.x + (avail.x - w1 - w2 - gap) * 0.5f, bottom + detailH + gap * 0.5f));
  if (launcherButton(moreLabel, ImVec2(0, 0), scale)) {
    sMore = !sMore;
    sFocus = true;
    sDetail = -2;
  }
  if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) {
    over = -3;
  }
  ImGui::SameLine(0.f, gap);
  if (launcherButton(laterLabel, ImVec2(0, 0), scale)) {
    chosen = -1;
  }
  if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) {
    over = -1;
  }
  if (over != -2) {
    sDetail = over;
  }
  {
    const char* detail = "";
    if (sDetail == -1) {
      detail = portTr("Keep the game as on the console. The presets stay under Settings, Gameplay.");
    } else if (sDetail == -3) {
      detail = sMore ? portTr("Back to the main presets.") : portTr("Every preset, thirteen in all.");
    } else if (sDetail >= 0) {
      detail = portPresetAvailable(sDetail) ? portTr(portPresetAbout(sDetail))
                                            : portTr("Link your Paper Mario ROM first: Enhancements, Paper Mario "
                                                     "64, Link Paper Mario 64.");
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // as large as fits in its room (two lines, or the buttons' height)
    float size = bodySize;
    while (size > bodySize * 0.5f &&
           centredWrapped(dl, body, size, detailAt, detailW, 0, detail, false) > detailH + 0.5f) {
      size -= bodySize * 0.05f;
    }
    const float h = centredWrapped(dl, body, size, detailAt, detailW, 0, detail, false);
    centredWrapped(dl, body, size, ImVec2(detailAt.x, detailAt.y + (detailH - h) * 0.5f), detailW,
                   IM_COL32(86, 66, 52, 255), detail);
  }
  // the way back (B, Escape, Android's back): from all of them to the main ones, else Choose later
  if (chosen == -2 && takeBack()) {
    if (sMore) {
      sMore = false;
      sFocus = true;
      sDetail = -2;
    } else {
      chosen = -1;
    }
    launcherSound(LauncherSound::Back);
  }
  // tests: a pick at a time
  static const char* sTest = SDL_getenv("TTYD_PRESET_PICKER_CHOOSE");
  if (sTest != nullptr) {
    const char* colon = std::strchr(sTest, ':');
    if (colon != nullptr && time >= static_cast<float>(std::atof(sTest))) {
      chosen = -1;
      for (int i = 0; i < portPresetCount(); i++) {
        if (std::strcmp(colon + 1, portPresetName(i)) == 0) {
          chosen = i;
        }
      }
      sTest = nullptr;
    }
  }
  ImGui::EndChild();
  if (chosen >= -1) {
    if (chosen >= 0) {
      portPresetApply(chosen);
    }
    std::printf("[presets] first run: %s\n", chosen >= 0 ? portPresetName(chosen) : "later");
    std::fflush(stdout);
    portPresetPickerDone();
    l.focusList = true;
  }
}

void draw(Launcher& l, float time) {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  portMenuUiFrame(); // every controller, each with its own confirm and back (menu_buttons.cpp)
  portBindingsLauncherFrame(); // (tests: $TTYD_PADBIND_TEST)
  sBackTaken = false; // (this frame's way back: takeBack)
  const float scale = launcherUiScale(touchScreen() ? 1.6f : 1.f);
  launcherStyleInit(scale); // this frame's, at the window's size
  ImVec2 stageMin, stageMax;
  launcherPopupStage(time, scale);                     // the pop-up launcher: skipped by any press
  launcherBackdrop(time, scale, &stageMin, &stageMax, true); // its layers popping up in turn
  testAdd(l, time);
  inviteFrame(l); // an invite link: the chosen disc starts on the Multiplayer page
  (void)portPm64LinkPoll(); // a Paper Mario 64 ROM dropped on the window: linked (on any tab)
  if (launcherLookGameWanted()) {
    lookArt(l, l.selected); // (the look chosen on the Settings tab just now: once per disc)
  }
  presetIcons(l); // (the presets' pictures: once per disc's version)

  // A short stage (a phone held sideways, a small window): the wordmark goes
  // to the tab row's left, without its ribbon, so the cards keep the height. A narrow one (a phone held upright): the Play tab's cards stack.
  const float stageW = stageMax.x - stageMin.x;
  const bool compact = stageMax.y - stageMin.y < 560.f * scale;
  const bool stacked = stageW < 640.f * scale;

  // Paper Cut's title over the stage (launcher_brand.cpp), unfolding as the pop-up opens
  ImDrawList* bg = ImGui::GetBackgroundDrawList();
  const int titleMark = launcherPopupMark(bg);
  float tabIndent = 0.f;
  const float titleBottom = launcherBrandTitle(bg, (stageMin.x + stageMax.x) * 0.5f, stageMin.x, stageMin.y, stageW,
                                               stageMax.y - stageMin.y, scale, time, compact, &tabIndent);
  launcherPopupFold(bg, titleMark, titleBottom, time);
  // compact: the tabs beside the wordmark, along its foot
  const float tabRow = gLauncherFonts.heading->FontSize * io.FontGlobalScale + 24.f * scale;
  const float y = compact ? std::max(stageMin.y, titleBottom - tabRow) : titleBottom;

  // a controller: the shoulder buttons step through the tabs (Ctrl+Page Up/Down on a keyboard), and
  // its buttons are named along the bottom
  updateInputMode(l);
  const bool ctrl = io.KeyCtrl;
  if (launcherOverlayShowing() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
    // (a card over the stage, launcher_onboard.h, or a question: the tabs stay as they are under it)
  } else if (pageKey(ImGuiKey_GamepadL1) || (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageUp, false))) {
    l.wantTab = (l.tab + kTabCount - 1) % kTabCount;
  } else if (pageKey(ImGuiKey_GamepadR1) || (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageDown, false))) {
    l.wantTab = (l.tab + 1) % kTabCount;
  }
  if (const int t = startTab(time); t >= 0) {
    l.wantTab = t;
  }
  const float promptsHeight = l.pad ? 40.f * scale : 0.f;
  if (l.pad) {
    drawPrompts(l, stageMin, stageMax, scale);
  }

  // the tabs and the page below the title
  ImGui::SetNextWindowPos(ImVec2(stageMin.x, y + launcherPopupRise(stageMax.y - y, time)), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(stageMax.x - stageMin.x, stageMax.y - y - promptsHeight), ImGuiCond_Always);
  ImGui::Begin("launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
  ImGui::PushFont(gLauncherFonts.body);
  if (portPresetPickerDue()) {
    // a new install's first start: the presets first, in place of the tabs (a short stage: below the
    // wordmark; its heading and intro where the tabs would sit beside it, so the cards keep the height)
    bool headingDone = false;
    float used = 0.f;
    if (compact && tabIndent > 0.f && titleBottom > y) {
      const float top = ImGui::GetCursorScreenPos().y;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tabIndent);
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const char* heading = portTr("How do you want to play?");
      launcherHeading(heading, scale);
      used = ImGui::GetCursorScreenPos().y - top;
      // the intro beside it, in the stage's cream, where it fits
      ImFont* head = gLauncherFonts.heading;
      const float headW = head->CalcTextSizeA(head->FontSize * io.FontGlobalScale, FLT_MAX, 0.f, heading).x + 48.f * scale;
      const char* intro = portTr("Pick a starting point. You can change anything later.");
      const ImVec2 introSize = ImGui::CalcTextSize(intro);
      if (at.x + headW + introSize.x <= stageMax.x - 8.f * scale) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + headW, at.y + (used - introSize.y) * 0.5f - 2.f * scale),
                                            IM_COL32(255, 247, 228, 255), intro);
      }
      headingDone = true;
    }
    if (titleBottom > y + used) {
      ImGui::Dummy(ImVec2(0, titleBottom - y - used));
    }
    drawPresetPicker(l, time, scale, headingDone);
    ImGui::PopFont();
    ImGui::End();
    launcherCursorFrame(time, scale);
    launcherPopupFront(time, scale);
    return;
  }
  if (tabIndent > 0.f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tabIndent);
  }
  // the Start menu's tabs, in its colours
  const LauncherTab tabs[kTabCount] = {
      {portTr("Play"), LauncherTabColour::Red},
      {portTr("Settings"), LauncherTabColour::Orange},
      {portTr("Mods"), LauncherTabColour::Green},
      {portTr("About"), LauncherTabColour::Blue},
  };
  static const bool sTabTrace = SDL_getenv("TTYD_PADBIND_TEST") != nullptr;
  const int tabBefore = l.tab;
  if (sTabTrace && l.wantTab >= 0 && l.wantTab != l.tab) {
    std::printf("[padbind] launcher tab asked for: %d (L1 %d, R1 %d)\n", l.wantTab,
                int(ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)), int(ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)));
    std::fflush(stdout);
  }
  if (l.wantTab >= 0) {
    l.tab = l.wantTab;
    l.wantTab = -1;
    if (l.tab == 0) {
      l.focusList = true;
    }
  }
  l.tab = launcherTabs(tabs, kTabCount, l.tab, scale);
  if (sTabTrace && l.tab != tabBefore) {
    std::printf("[padbind] launcher tab now %d (nav %08x, window %s)\n", l.tab, ImGui::GetCurrentContext()->NavId,
                ImGui::GetCurrentContext()->NavWindow != nullptr ? ImGui::GetCurrentContext()->NavWindow->Name : "-");
    std::fflush(stdout);
  }
  ImGui::Dummy(ImVec2(0, 2.f * scale));
  const ImVec2 pageMin = ImGui::GetCursorScreenPos();
  const ImVec2 pageMax(pageMin.x + ImGui::GetContentRegionAvail().x, pageMin.y + ImGui::GetContentRegionAvail().y);
  switch (l.tab) {
  case 0:
    if (stacked) {
      // a narrow stage (a phone held upright): Play first, the discs (set up once) under it
      drawDetails(l, scale, ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), time);
      ImGui::Dummy(ImVec2(0, 4.f * scale));
      drawLibrary(l, scale, true);
    } else {
      drawLibrary(l, scale, false);
      ImGui::SameLine(0, 18.f * scale);
      drawDetails(l, scale, ImVec2(0, 0), time);
    }
    break;
  case kTabSettings:
    beginCard("settings", ImVec2(0, 0), scale);
    portSettingsTabs(false);
    ImGui::EndChild();
    break;
  case kTabMods:
    portModManagerLauncherTab(scale, stacked); // the mod manager (mods_manager.cpp)
    break;
  default:
    drawAbout(l, scale);
    break;
  }
  // the way back on a tab (B, Escape, Android's back, the mouse's back): what the page has open first (the
  // Settings tab's search, the Mods tab's Remove: sure?), else back to Play
  if (l.tab != kTabPlay && takeBack()) {
    const bool page = l.tab == kTabSettings ? portSettingsLauncherBack() != 0
                      : l.tab == kTabMods   ? portModManagerLauncherBack()
                                            : false;
    if (!page) {
      l.wantTab = kTabPlay;
    }
    launcherSound(LauncherSound::Back);
  }
  ImGui::PopFont();
  ImGui::End();
  if (portModManagerLauncherFront(scale)) {
    l.wantTab = kTabMods; // mods dropped on the window: the Mods tab shows them come in
  }
  launcherPageTurn(l.tab, pageMin, ImVec2(pageMax.x, pageMax.y - 8.f * scale), scale, time);
  {
    // What's new, the multiplayer help (launcher_onboard.h): over the stage, under the controller's buttons
    ImGui::PushFont(gLauncherFonts.body);
    const int go = launcherOverlayDraw(scale, stageMin, ImVec2(stageMax.x, stageMax.y - promptsHeight),
                                       time > 2.2f); // (the pop-up opening over: a key skips it, not a card)
    ImGui::PopFont();
    if (go >= 0) {
      l.wantTab = go;
    }
  }
  launcherCursorFrame(time, scale); // the glove, at what is chosen
  launcherPopupFront(time, scale);  // the pop-up launcher: the curtains parting, the cast
}

// The next start after a crash (crash_report.cpp, settings_ui.h): the recovery screen over the launcher,
// once. Its Continue is the Play card's, for the disc of the version that crashed (quick resume's save
// as the save file it came from); none without that disc in the library or its save.
void drawRecovery(Launcher& l) {
  static bool sLooked;
  static int sDisc = -1;
  static PortRecoveryResume sResume;
  if (!portCrashReportPending() || (!sLooked && sTestAdd != nullptr)) {
    return; // (tests: $TTYD_LAUNCHER_ADD's disc in the library first)
  }
  if (!sLooked) {
    sLooked = true;
    const std::string crashed = portCrashReportGameId();
    for (size_t i = 0; i < l.discs.size(); i++) {
      const Disc& d = l.discs[i];
      const bool same = d.version != nullptr && (crashed.empty() ? int(i) == l.selected : crashed == d.version->id);
      if (same && playProblem(d).empty() && !resumeFile(d).empty()) {
        sDisc = int(i);
        sResume = portRecoveryResume(d.version->id);
        break;
      }
    }
  }
  const float scale = launcherUiScale(touchScreen() ? 1.6f : 1.f);
  ImGui::PushFont(gLauncherFonts.body);
  const int pick = portRecoveryDraw(sResume, scale, true);
  ImGui::PopFont();
  if (pick != 0) {
    portCrashReportDismiss(); // (once: the game's own over its title doesn't come back with it)
  }
  if (pick == 1 && sDisc >= 0 && sDisc < int(l.discs.size())) {
    l.selected = sDisc;
    continueGame(l, sDisc);
  }
}

} // namespace

extern "C" int portLauncherRun(const char* userPath, const char* finder, char* path, size_t size) {
  Launcher l;
  l.userPath = userPath != nullptr ? userPath : ".";
  load(l, userPath);
  lookArt(l, l.selected); // the game look's art, when it's asked for (launcher_look.h)
  {
    // the pop-up launcher's cast: from the art cache of the last disc chosen (else the first known)
    const char* id = nullptr;
    if (l.selected >= 0 && l.discs[l.selected].version != nullptr) {
      id = l.discs[l.selected].version->id;
    }
    for (size_t i = 0; id == nullptr && i < l.discs.size(); i++) {
      if (l.discs[i].version != nullptr) {
        id = l.discs[i].version->id;
      }
    }
    launcherPopupStart(userPath != nullptr ? userPath : ".", id);
  }
  if (finder != nullptr && finder[0] != '\0') {
    // macOS: a disc image opened with the app in Finder plays straight away
    l.selected = add(l, finder);
    play(l, l.selected);
  } else if (portSettings()->launcherSkip && SDL_getenv("TTYD_LAUNCHER_TAB") == nullptr && l.selected >= 0 &&
             l.discs[l.selected].path == resolvedPath(readFile(l.lastFile)) && !portCrashReportPending() &&
             !portLauncherBackForced()) {
    // (after a crash, the launcher all the same: its recovery screen first; and when the player went Back to
    // the launcher from the game: port/include/port/launcher_back.h)
    play(l, l.selected);
  }
  if (portLauncherBackForced()) {
    std::printf("[launcher] shown: Back to the launcher\n");
    std::fflush(stdout);
  }
  gPortLauncherShown = true; // touches drive the launcher (settings_ui.cpp)
  // a controller plugged in: its buttons shown from the start
  l.pad = SDL_HasGamepad() || SDL_getenv("TTYD_LAUNCHER_PAD") != nullptr;
  const Uint64 start = SDL_GetTicks();
  // tests' captures: a clock of 60 frames a second, so each capture shows the moment asked for
  const bool shots = launcherShotActive();
  for (Uint64 frame = 0; l.result.empty(); frame++) {
    const float time = shots ? static_cast<float>(frame) / 60.f : static_cast<float>(SDL_GetTicks() - start) / 1000.f;
    portAuroraBeginFrame(); // exits the program if the window is closed
    draw(l, time);
    drawRecovery(l); // after a crash: once, over it
    // (the next frame's way back isn't the page's while a pop-up or a text field has it: takeBack)
    sBackBusy = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) || ImGui::IsAnyItemActive();
    launcherStyleEnd();
    aurora_end_frame();
    launcherShotFrame(time); // tests: captures
    // $TTYD_LAUNCHER_PLAY_AT=<seconds> (tests): Play on the disc chosen then, as its button does
    static const char* sPlayAt = SDL_getenv("TTYD_LAUNCHER_PLAY_AT");
    if (sPlayAt != nullptr && time >= static_cast<float>(std::atof(sPlayAt)) && l.selected >= 0 &&
        l.selected < static_cast<int>(l.discs.size()) && l.result.empty()) {
      std::printf("[launcher] TTYD_LAUNCHER_PLAY_AT: Play\n");
      std::fflush(stdout);
      if (!play(l, l.selected)) {
        sPlayAt = nullptr; // (said why in l.status: not again every frame)
      }
    }
  }
  gPortLauncherShown = false;
  launcherSoundEnd();
  portUiReleaseInput(); // the game gets the keyboard and controller from here
  std::snprintf(path, size, "%s", l.result.c_str());
  return 1;
}
