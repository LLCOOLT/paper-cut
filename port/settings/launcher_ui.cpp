// The launcher: the screen before the game, when no disc was given on the
// command line (port/src/aurora/disc_aurora.c). It keeps a library of the
// player's disc images (library.txt in the user folder), names each one's
// version (port/include/port/versions.h), and plays the chosen one: in this
// executable if it is this build's version, else by starting the executable
// of its version beside this one (ttyd-<short>, which then skips the
// launcher). iOS apps can't start another program, so there each app plays
// its own version and shows the others' discs as not playable here. The
// settings' tabs (settings_ui.cpp) are here too, for what applies at start.
#include "port/aurora_pc.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/versions.h"
#include "settings_ui.h"
#include "port/update_check.h"
#include "port/resume.h"
#include "launcher_style.h"
#include "launcher_brand.h"
#include "launcher_look.h"
#include "launcher_popup.h"
#include "launcher_shot.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>
#include <imgui_internal.h> // FocusItem

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
extern "C" const char* portIosDocumentsDir(void);
#endif

bool portOnlineLobbyDraw(float scale); // online co-op's lobby (port/settings/features/online.cpp)
extern "C" int portOnlinePeekInvite(char* out, size_t size); // invite.cpp
// the mod manager (port/settings/features/mods_manager.cpp): its tab, and what goes over the launcher
// (files dragged in, safe mode's offer); true: show the Mods tab
void portModManagerLauncherTab(float scale, bool stacked);
bool portModManagerLauncherFront(float scale);

namespace {

struct Disc {
  std::string path;
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
  int tab = 0;             // the tab shown (Play, Online, Settings, Mods, About)
  int wantTab = -1;        // a tab asked for with the shoulder buttons
  std::string userPath;    // the user folder (the game look's art cache)
};

constexpr int kTabCount = 5;

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
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  const SDL_GamepadType type = count > 0 ? SDL_GetGamepadTypeForID(ids[0]) : SDL_GAMEPAD_TYPE_STANDARD;
  SDL_free(ids);
  return type;
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
  io.ConfigNavSwapGamepadButtons = nintendoPad(padType());
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

void writeFile(const std::string& path, const std::string& text) {
  if (!SDL_SaveFile(path.c_str(), text.data(), text.size())) {
    std::fprintf(stderr, "launcher: could not write %s: %s\n", path.c_str(), SDL_GetError());
  }
}

void probe(Disc& d) {
  SDL_PathInfo info;
  d.exists = SDL_GetPathInfo(d.path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
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
    text += d.path + "\n";
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
      d.path = line;
      l.discs.push_back(d);
    }
    at = end + 1;
  }
#ifdef LAUNCHER_ONE_VERSION
  // iOS: the disc images in this app's folder (the Files app) are listed too
  if (const char* docs = portIosDocumentsDir()) {
    int count = 0;
    char** names = SDL_GlobDirectory(docs, nullptr, 0, &count);
    for (int i = 0; names != nullptr && i < count; i++) {
      const std::string path = std::string(docs) + "/" + names[i];
      const std::string ext = path.substr(path.find_last_of('.') + 1);
      if (ext != path && SDL_strcasestr(".iso.gcm.ciso.gcz.rvz.wia.wbfs.nfs.tgc.", ("." + ext + ".").c_str())) {
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
  const std::string last = readFile(l.lastFile);
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
    return "The file isn't there any more.";
  }
  if (d.version == nullptr) {
    return "This isn't a disc image of Paper Mario: The Thousand-Year Door that can be read.";
  }
  if (d.version == self) {
    return {};
  }
  if (!d.version->ported) {
    return std::string("This is ") + d.version->name + ", which the port doesn't play.";
  }
#ifdef LAUNCHER_ONE_VERSION
  return std::string("This is ") + d.version->name + ". This app plays " + self->name +
         "; the app for that version plays this disc.";
#else
  if (executableOf(d.version).empty()) {
    return std::string("This is ") + d.version->name + ", and its build (ttyd-" + d.version->shortName +
           ") isn't beside this one.";
  }
  return {};
#endif
}

// Plays disc i: here, or by starting its version's executable (which exits this one).
void play(Launcher& l, int i) {
  const Disc& d = l.discs[i];
  const std::string problem = playProblem(d);
  if (!problem.empty()) {
    l.status = problem;
    return;
  }
  writeFile(l.lastFile, d.path);
  if (d.version == portVersionSelf()) {
    l.result = d.path;
    return;
  }
  const std::string exe = executableOf(d.version);
  const char* args[] = {exe.c_str(), d.path.c_str(), nullptr};
  SDL_Process* process = SDL_CreateProcess(args, false);
  if (process == nullptr) {
    l.status = "Could not start " + fileName(exe) + ": " + SDL_GetError();
    return;
  }
  SDL_DestroyProcess(process); // it runs on; this one ends
  // at once: the library and settings are saved, and Aurora's shutdown (its
  // GPU and audio threads) can hang here before a game ever ran
  if (SDL_Window* w = aurora_get_window()) {
    SDL_HideWindow(w);
  }
  std::_Exit(0);
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
  if (l.discs[i].version == portVersionSelf()) {
    portWarpRequestResume(path.c_str(), slot[0] - '0'); // here: straight to the warp
  } else {
    // another version's executable: through its environment (SDL_CreateProcess passes SDL's)
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LOAD_SAVE", path.c_str(), true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LOAD_SAVE_SLOT", slot.c_str(), true);
  }
  play(l, i);
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
  const float buttonsH = ImGui::GetFrameHeight() + 26.f * scale;
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
    ImGui::TextDisabled("%s", fit(fileName(d.path), rowMax.x - ImGui::GetCursorScreenPos().x - 6.f * scale).c_str());
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
  if (addPressed || (l.pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false))) {
    char path[4096];
    if (portAuroraPickDisc(path, sizeof(path))) {
      addDisc(l, path);
    }
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
  ImGui::EndChild();
}

// size: the card's (0: the rest of the row or column)
void drawDetails(Launcher& l, float scale, ImVec2 size = ImVec2(0, 0)) {
  beginCard("details", size, scale);
  if (l.selected < 0 || l.selected >= static_cast<int>(l.discs.size())) {
    launcherHeading(portTr("Welcome!"), scale);
    ImGui::Spacing();
    ImGui::TextWrapped("%s", portTr("Add the disc image of your copy of the game to play. The list remembers it, and each "
                       "version (Japanese, US, European) plays with its own build of the port."));
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
    ImGui::TextWrapped("%s", d.path.c_str());
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
        ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
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
  } else {
    const char* label = d.version != nullptr && d.version != portVersionSelf() ? portTrLabel("Play (starts its build)")
                                                                               : portTrLabel("Play");
    if (launcherButton(label, ImVec2(buttonWidth - indent, 0), scale, LauncherButton::Primary) ||
        (problem.empty() && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))) {
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

// Online co-op (port/include/port/online.h): the lobby for the disc chosen in
// Play, in its version's build; both games start from here.
void drawOnline(Launcher& l, float scale) {
  beginCard("online", ImVec2(0, 0), scale);
  const Disc* d = l.selected >= 0 && l.selected < static_cast<int>(l.discs.size()) ? &l.discs[l.selected] : nullptr;
  if (d == nullptr || d->version == nullptr || !playProblem(*d).empty()) {
    launcherHeading(portTr("Online"), scale);
    ImGui::TextWrapped("%s", portTr("Choose your disc in Play first: online play uses it."));
  } else if (d->version != portVersionSelf()) {
    launcherHeading(portTr("Online"), scale);
    ImGui::TextWrapped(portTr("This is %s: online play runs in its own build."), portTr(d->version->name));
#ifndef LAUNCHER_ONE_VERSION
    ImGui::Spacing();
    if (launcherButton(portTrLabel("Open its build"), ImVec2(0, 0), scale, LauncherButton::Primary)) {
      // its launcher, on this tab (SDL_CreateProcess passes SDL's environment)
      const std::string exe = executableOf(d->version);
      SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LAUNCHER_TAB", "Online", true);
      char invite[16];
      if (portOnlinePeekInvite(invite, sizeof(invite))) { // an invite link's room, asked about there (invite.cpp)
        SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_ONLINE_INVITE", invite, true);
      }
      const char* args[] = {exe.c_str(), nullptr};
      if (SDL_Process* process = SDL_CreateProcess(args, false)) {
        SDL_DestroyProcess(process);
        if (SDL_Window* w = aurora_get_window()) {
          SDL_HideWindow(w);
        }
        std::_Exit(0);
      }
      l.status = "Could not start " + fileName(exe) + ": " + SDL_GetError();
    }
#endif
  } else if (portOnlineLobbyDraw(scale)) {
    writeFile(l.lastFile, d->path);
    l.result = d->path; // both games start
  }
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
  // the face buttons by position: confirm and back swap places on Nintendo controllers
  SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_NORTH;
  if (b == kConfirm) {
    button = nintendoPad(type) ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
  } else if (b == kBack) {
    button = nintendoPad(type) ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
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
  if (l.tab == 0) {
    if (!l.discs.empty()) {
      prompts.push_back({kConfirm, portTr("Play")});
    }
    prompts.push_back({kTop, portTr("Add a disc")});
  } else {
    prompts.push_back({kConfirm, portTr("Choose")});
    prompts.push_back({kBack, portTr("Back")});
  }
  if (l.tab == 2) { // Settings
    prompts.push_back({kTriggers, portTr("Sections")});
  }
  prompts.push_back({kShoulders, portTr("Tabs")});
  const SDL_GamepadType type = padType();
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* font = gLauncherFonts.body;
  const float h = 26.f * scale;
  const float textSize = font->FontSize * ImGui::GetIO().FontGlobalScale;
  const float gap = 8.f * scale, between = 26.f * scale;
  float width = 0.f;
  for (const Prompt& p : prompts) {
    width += padGlyph(nullptr, p.button, type, ImVec2(0.f, 0.f), h) + gap +
             font->CalcTextSizeA(textSize, FLT_MAX, 0.f, p.text).x + between;
  }
  width -= between;
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
  static const char* const kNames[kTabCount] = {"Play", "Online", "Settings", "Mods", "About"};
  if (want == nullptr) {
    return -1;
  }
  const char* at = std::strchr(want, '@');
  if (at != nullptr && time < static_cast<float>(std::atof(at + 1))) {
    return -1;
  }
  const size_t n = at != nullptr ? static_cast<size_t>(at - want) : std::strlen(want);
  int found = -1;
  for (int i = 0; i < kTabCount; i++) {
    if (std::strlen(kNames[i]) == n && std::strncmp(want, kNames[i], n) == 0) {
      found = i;
    }
  }
  want = nullptr;
  return found;
}

// $TTYD_LAUNCHER_ADD="SECONDS:PATH" (tests, screenshots): that disc image added then, as if picked
void testAdd(Launcher& l, float time) {
  static const char* add = SDL_getenv("TTYD_LAUNCHER_ADD");
  if (add == nullptr) {
    return;
  }
  const char* colon = std::strchr(add, ':');
  if (colon != nullptr && time >= static_cast<float>(std::atof(add))) {
    addDisc(l, colon + 1);
    add = nullptr;
  }
}

void draw(Launcher& l, float time) {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  const float scale = launcherUiScale(touchScreen() ? 1.6f : 1.f);
  launcherStyleInit(scale); // this frame's, at the window's size
  ImVec2 stageMin, stageMax;
  launcherPopupStage(time, scale);                     // the pop-up launcher: skipped by any press
  launcherBackdrop(time, scale, &stageMin, &stageMax, true); // its layers popping up in turn
  testAdd(l, time);
  if (launcherLookGameWanted()) {
    lookArt(l, l.selected); // (the look chosen on the Settings tab just now: once per disc)
  }

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
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageUp, false))) {
    l.wantTab = (l.tab + kTabCount - 1) % kTabCount;
  } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageDown, false))) {
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
  if (tabIndent > 0.f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tabIndent);
  }
  // the Start menu's tabs, in its colours
  const LauncherTab tabs[kTabCount] = {
      {portTr("Play"), LauncherTabColour::Red},
      {portTr("Online"), LauncherTabColour::Pink},
      {portTr("Settings"), LauncherTabColour::Orange},
      {portTr("Mods"), LauncherTabColour::Green},
      {portTr("About"), LauncherTabColour::Blue},
  };
  if (l.wantTab >= 0) {
    l.tab = l.wantTab;
    l.wantTab = -1;
    if (l.tab == 0) {
      l.focusList = true;
    }
  }
  l.tab = launcherTabs(tabs, kTabCount, l.tab, scale);
  ImGui::Dummy(ImVec2(0, 2.f * scale));
  const ImVec2 pageMin = ImGui::GetCursorScreenPos();
  const ImVec2 pageMax(pageMin.x + ImGui::GetContentRegionAvail().x, pageMin.y + ImGui::GetContentRegionAvail().y);
  switch (l.tab) {
  case 0:
    if (stacked) {
      // a narrow stage (a phone held upright): Play first, the discs (set up once) under it
      drawDetails(l, scale, ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f));
      ImGui::Dummy(ImVec2(0, 4.f * scale));
      drawLibrary(l, scale, true);
    } else {
      drawLibrary(l, scale, false);
      ImGui::SameLine(0, 18.f * scale);
      drawDetails(l, scale);
    }
    break;
  case 1:
    drawOnline(l, scale);
    break;
  case 2:
    beginCard("settings", ImVec2(0, 0), scale);
    portSettingsTabs(false);
    ImGui::EndChild();
    break;
  case 3:
    portModManagerLauncherTab(scale, stacked); // the mod manager (mods_manager.cpp)
    break;
  default:
    drawAbout(l, scale);
    break;
  }
  ImGui::PopFont();
  ImGui::End();
  if (portModManagerLauncherFront(scale)) {
    l.wantTab = 3; // mods dropped on the window: the Mods tab shows them come in
  }
  launcherPageTurn(l.tab, pageMin, ImVec2(pageMax.x, pageMax.y - 8.f * scale), scale, time);
  launcherCursorFrame(time, scale); // the glove, at what is chosen
  launcherPopupFront(time, scale);  // the pop-up launcher: the curtains parting, the cast
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
             l.discs[l.selected].path == readFile(l.lastFile)) {
    play(l, l.selected);
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
    launcherStyleEnd();
    aurora_end_frame();
    launcherShotFrame(time); // tests: captures
  }
  gPortLauncherShown = false;
  launcherSoundEnd();
  portUiReleaseInput(); // the game gets the keyboard and controller from here
  std::snprintf(path, size, "%s", l.result.c_str());
  return 1;
}
