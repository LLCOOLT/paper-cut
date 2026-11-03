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
#include "port/versions.h"
#include "settings_ui.h"
#include "port/update_check.h"
#include "port/resume.h"
#include "launcher_style.h"

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

namespace {

struct Disc {
  std::string path;
  const PortVersionInfo* version = nullptr; // nullptr: unreadable or not the game
  bool exists = true;
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
  int tab = 0;             // the tab shown (Play, Settings, About)
  int wantTab = -1;        // a tab asked for with the shoulder buttons
};

constexpr int kTabCount = 3;

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
  d.version = d.exists ? portAuroraProbeDisc(d.path.c_str()) : nullptr;
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

// A paper card filling size (0: the rest of the row or column), as a child
// window with the card drawn behind it. Pair with ImGui::EndChild().
void beginCard(const char* id, ImVec2 size, float scale) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (size.x <= 0) {
    size.x = avail.x;
  }
  if (size.y <= 0) {
    size.y = avail.y - 8.f * scale; // room for the card's shadow
  }
  const ImVec2 at = ImGui::GetCursorScreenPos();
  launcherPaperCard(ImGui::GetWindowDrawList(), at, ImVec2(at.x + size.x, at.y + size.y), scale);
  ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding);
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

void heading(const char* text) {
  ImGui::PushFont(gLauncherFonts.heading);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

// a big red button with white lettering
bool bigButton(const char* label, float width, float scale) {
  ImGui::PushFont(gLauncherFonts.heading);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 22.f * scale);
  const bool pressed = launcherRedButton(label, ImVec2(width, 48.f * scale));
  ImGui::PopStyleVar();
  ImGui::PopFont();
  return pressed;
}

// stacked (a narrow stage): under the details, in the rest of the space;
// else the left of the row
void drawLibrary(Launcher& l, float scale, bool stacked) {
  const PortVersionInfo* self = portVersionSelf();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 size = stacked ? ImVec2(0, 0) : ImVec2(std::max(280.f * scale, avail.x * 0.42f), 0);
  beginCard("library", size, scale);
  heading("Your discs");
  ImGui::Spacing();
  if (l.discs.empty()) {
    ImGui::TextWrapped("None yet. Add the disc image of your copy of the game (.iso, .rvz, .gcm...).");
  }
  for (size_t i = 0; i < l.discs.size(); i++) {
    const Disc& d = l.discs[i];
    const char* label = d.version != nullptr ? d.version->label : (d.exists ? "Not the game" : "Missing");
    ImGui::PushID(static_cast<int>(i));
    const bool playable = d.version == self || (d.version != nullptr && d.version->ported);
    const bool chosen = ImGui::Selectable("##disc", l.selected == static_cast<int>(i),
                                          ImGuiSelectableFlags_AllowDoubleClick,
                                          ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2));
    if (l.focusList && l.selected == static_cast<int>(i)) {
      ImGui::FocusItem(); // not SetKeyboardFocusHere, which would also press it
      l.focusList = false;
    }
    if (chosen) {
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
    ImGui::SameLine(ImGui::GetStyle().ItemSpacing.x * 2);
    ImGui::BeginGroup();
    if (playable) {
      ImGui::TextUnformatted(label);
    } else {
      ImGui::TextDisabled("%s", label);
    }
    ImGui::TextDisabled("%s", fit(fileName(d.path), ImGui::GetContentRegionAvail().x).c_str());
    ImGui::EndGroup();
    ImGui::PopID();
  }
  ImGui::Spacing();
  const bool addPressed = launcherRedButton(l.discs.empty() ? "Add a disc image..." : "Add another...");
  if (l.focusList && l.selected < 0) {
    ImGui::FocusItem();
    l.focusList = false;
  }
  if (addPressed || (l.pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false))) {
    char path[4096];
    if (portAuroraPickDisc(path, sizeof(path))) {
      l.selected = add(l, path);
      l.status.clear();
      l.focusList = true;
    }
  }
  if (l.selected >= 0) {
    ImGui::SameLine();
    if (ImGui::Button("Remove from list")) {
      l.discs.erase(l.discs.begin() + l.selected);
      l.selected = std::min(l.selected, static_cast<int>(l.discs.size()) - 1);
      save(l);
    }
  }
  ImGui::EndChild();
}

// size: the card's (0: the rest of the row or column)
void drawDetails(Launcher& l, float scale, ImVec2 size = ImVec2(0, 0)) {
  beginCard("details", size, scale);
  if (l.selected < 0 || l.selected >= static_cast<int>(l.discs.size())) {
    heading("Welcome!");
    ImGui::Spacing();
    ImGui::TextWrapped("Add the disc image of your copy of the game to play. The list remembers it, and each "
                       "version (Japanese, US, European) plays with its own build of the port.");
    ImGui::EndChild();
    return;
  }
  const Disc& d = l.discs[l.selected];
  if (d.version != nullptr) {
    std::string name = d.version->name;
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    heading(name.c_str());
    ImGui::TextDisabled("Disc ID %s", d.version->id);
  } else {
    heading(d.exists ? "Unknown disc image" : "Missing file");
  }
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", d.path.c_str());
  ImGui::PopStyleColor();
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  const std::string problem = playProblem(d);
  ImGui::BeginDisabled(!problem.empty());
  SDL_Time resumed = 0;
  const bool canContinue = problem.empty() && !resumeFile(d, &resumed).empty();
  const float buttonWidth = std::min(ImGui::GetContentRegionAvail().x, 300.f * scale);
  if (canContinue) {
    // quick resume's save: Continue first (Start too), the title below it
    if (bigButton("Continue", buttonWidth, scale) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
      continueGame(l, l.selected);
    }
    SDL_DateTime t;
    if (SDL_TimeToDateTime(resumed, &t, true)) {
      ImGui::TextDisabled("Where you left off, %04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
    }
    ImGui::Spacing();
    if (ImGui::Button("Start from the title", ImVec2(buttonWidth, 0))) {
      play(l, l.selected);
    }
  } else {
    const char* label = d.version != nullptr && d.version != portVersionSelf() ? "Play (starts its build)" : "Play";
    if (bigButton(label, buttonWidth, scale) || (problem.empty() && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))) {
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
  if (ImGui::Checkbox("Skip to this disc next time", &skip)) {
    s->launcherSkip = skip ? 1 : 0;
    portSettingsSave();
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("To see this screen again: Settings > System in the game.");
  ImGui::PopStyleColor();
  ImGui::EndChild();
}

void drawAbout(Launcher& l, float scale) {
  (void)l;
  const PortVersionInfo* self = portVersionSelf();
  beginCard("about", ImVec2(0, 0), scale);
  heading("About");
  ImGui::Spacing();
  ImGui::TextWrapped("Paper Cut plays Paper Mario: The Thousand-Year Door as a native program, built from the "
                     "game's C decompilation, with no emulator. You need your own copy of the game: a disc image "
                     "of the Japanese, US or European release.");
  ImGui::Spacing();
  ImGui::Text("Paper Cut %s", PORT_VERSION);
  ImGui::Text("This build: %s (%s)", self->name, self->version);
  portUpdateDrawNotice(); // a newer release, when the update check found one
  ImGui::Spacing();
  heading("Licences");
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("Paper Cut is under the MIT licence. It is built with: Aurora (MIT), SDL 3 (zlib), Dawn "
                     "(BSD 3-Clause), nod (MIT), Dear ImGui (MIT), FreeType (FreeType License), libpng (libpng "
                     "License), zlib (zlib), Zstandard (BSD 3-Clause), xxHash (BSD 2-Clause), {fmt} (MIT), Abseil "
                     "(Apache 2.0), Tracy (BSD 3-Clause), SQLite (public domain), MusyX (MIT), SGI's GLU "
                     "tessellator (SGI Free Software License B 2.0), and the Fredoka typeface (SIL Open Font "
                     "License 1.1).");
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  ImGui::TextWrapped("The licence texts are in THIRD_PARTY.txt of Paper Cut's desktop packages and in its "
                     "source (docs/THIRD_PARTY.md).");
#else
  ImGui::TextWrapped("The licence texts are in THIRD_PARTY.txt and OFL.txt beside Paper Cut.");
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
      prompts.push_back({kConfirm, "Play"});
    }
    prompts.push_back({kTop, "Add a disc"});
  } else {
    prompts.push_back({kConfirm, "Choose"});
    prompts.push_back({kBack, "Back"});
  }
  if (l.tab == 1) {
    prompts.push_back({kTriggers, "Sections"});
  }
  prompts.push_back({kShoulders, "Tabs"});
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

// $TTYD_LAUNCHER_TAB (tests, screenshots): the tab of that name selected at start
ImGuiTabItemFlags startTab(const char* name) {
  static const char* want = SDL_getenv("TTYD_LAUNCHER_TAB");
  if (want != nullptr && std::strcmp(want, name) == 0) {
    want = nullptr;
    return ImGuiTabItemFlags_SetSelected;
  }
  return 0;
}

void draw(Launcher& l, float time) {
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  const float scale = launcherUiScale(touchScreen() ? 1.6f : 1.f);
  launcherStyleInit(scale); // this frame's, at the window's size
  const float fontScale = io.FontGlobalScale;
  ImVec2 stageMin, stageMax;
  launcherBackdrop(time, scale, &stageMin, &stageMax);

  // A short stage (a phone held sideways, a small window): the title shrinks
  // to the tab row's left, without the game's name, so the cards keep the
  // height. A narrow one (a phone held upright): the Play tab's cards stack.
  const float stageW = stageMax.x - stageMin.x;
  const bool compact = stageMax.y - stageMin.y < 560.f * scale;
  const bool stacked = stageW < 640.f * scale;

  // the port's name cut out of paper, centred at the top of the stage, and
  // the game it plays under it (each shrunk to fit the stage's width)
  ImDrawList* fg = ImGui::GetBackgroundDrawList();
  const char* title = "Paper Cut";
  const char* game = "Paper Mario: The Thousand-Year Door";
  auto fitWidth = [&](ImFont* font, float size, const char* text, float width) {
    const float w = font->CalcTextSizeA(size, FLT_MAX, 0.f, text).x;
    return w > width ? size * width / w : size;
  };
  const float titleSize =
      compact ? gLauncherFonts.heading->FontSize * fontScale * 1.3f
              : fitWidth(gLauncherFonts.title, gLauncherFonts.title->FontSize * fontScale * 1.25f, title,
                         stageW - 80.f * scale);
  const float gameSize = fitWidth(gLauncherFonts.heading, gLauncherFonts.heading->FontSize * fontScale, game, stageW);
  ImFont* titleFont = compact ? gLauncherFonts.heading : gLauncherFonts.title;
  const ImVec2 tsz = titleFont->CalcTextSizeA(titleSize, FLT_MAX, 0.f, title);
  const ImVec2 gsz = gLauncherFonts.heading->CalcTextSizeA(gameSize, FLT_MAX, 0.f, game);
  const float cx = (stageMin.x + stageMax.x) * 0.5f;
  float y = stageMin.y;
  float tabIndent = 0.f; // compact: the tabs start right of the title
  if (compact) {
    launcherTitleText(fg, titleFont, titleSize, ImVec2(stageMin.x + 4.f * scale, y + 2.f * scale), title);
    tabIndent = tsz.x + 20.f * scale;
  } else {
    launcherTitleText(fg, titleFont, titleSize, ImVec2(cx - tsz.x * 0.5f, y), title);
    // the banner's sparkles: one each side at the middle of the capitals, a
    // small one further out and higher (sizes from the font's em S)
    const float em = 0.826f * titleSize;
    const float sparkleY = y + 0.629f * em;
    for (int side = -1; side <= 1; side += 2) {
      const float sx = cx + side * (tsz.x * 0.5f + 0.42f * em);
      const float pulse = 1.f + 0.06f * std::sin(time * 2.f + side); // a slow twinkle
      launcherSparkle(fg, ImVec2(sx, sparkleY), 0.177f * em * pulse, kGold);
      launcherSparkle(fg, ImVec2(sx + side * 0.21f * em, sparkleY - 0.21f * em), 0.073f * em,
                      (kGold & 0x00FFFFFF) | (230u << 24));
    }
    y += tsz.y - 4.f * scale;
    launcherOutlinedText(fg, gLauncherFonts.heading, gameSize, ImVec2(cx - gsz.x * 0.5f, y), game, kGold,
                         IM_COL32(60, 20, 30, 255), 3.f * scale);
    y += gsz.y + 14.f * scale;
  }

  // a controller: the shoulder buttons step through the tabs, and its
  // buttons are named along the bottom
  updateInputMode(l);
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
    l.wantTab = (l.tab + kTabCount - 1) % kTabCount;
  } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
    l.wantTab = (l.tab + 1) % kTabCount;
  }
  const float promptsHeight = l.pad ? 40.f * scale : 0.f;
  if (l.pad) {
    drawPrompts(l, stageMin, stageMax, scale);
  }
  auto tabFlags = [&](int index, const char* name) {
    if (l.wantTab == index) {
      l.wantTab = -1;
      if (index == 0) {
        l.focusList = true;
      }
      return static_cast<ImGuiTabItemFlags>(ImGuiTabItemFlags_SetSelected);
    }
    return startTab(name);
  };

  // the panels below the title
  ImGui::SetNextWindowPos(ImVec2(stageMin.x, y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(stageMax.x - stageMin.x, stageMax.y - y - promptsHeight), ImGuiCond_Always);
  ImGui::Begin("launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
  ImGui::PushFont(gLauncherFonts.body);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 247, 228, 255)); // tab labels on the dark stage
  if (tabIndent > 0.f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + tabIndent);
  }
  const bool tabs = ImGui::BeginTabBar("launcher");
  ImGui::PopStyleColor();
  if (tabs) {
    if (ImGui::BeginTabItem("Play", nullptr, tabFlags(0, "Play"))) {
      l.tab = 0;
      if (stacked) {
        // a narrow stage (a phone held upright): Play first, the discs (set
        // up once) under it
        drawDetails(l, scale, ImVec2(0, ImGui::GetContentRegionAvail().y * 0.48f));
        ImGui::Dummy(ImVec2(0, 4.f * scale));
        drawLibrary(l, scale, true);
      } else {
        drawLibrary(l, scale, false);
        ImGui::SameLine(0, 18.f * scale);
        drawDetails(l, scale);
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings", nullptr, tabFlags(1, "Settings"))) {
      l.tab = 1;
      beginCard("settings", ImVec2(0, 0), scale);
      portSettingsTabs(false);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("About", nullptr, tabFlags(2, "About"))) {
      l.tab = 2;
      drawAbout(l, scale);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::PopFont();
  ImGui::End();
}

} // namespace

extern "C" int portLauncherRun(const char* userPath, const char* finder, char* path, size_t size) {
  Launcher l;
  load(l, userPath);
  if (finder != nullptr && finder[0] != '\0') {
    // macOS: a disc image opened with the app in Finder plays straight away
    l.selected = add(l, finder);
    play(l, l.selected);
  } else if (portSettings()->launcherSkip && l.selected >= 0 && l.discs[l.selected].path == readFile(l.lastFile)) {
    play(l, l.selected);
  }
  gPortLauncherShown = true; // touches drive the launcher (settings_ui.cpp)
  // a controller plugged in: its buttons shown from the start
  l.pad = SDL_HasGamepad() || SDL_getenv("TTYD_LAUNCHER_PAD") != nullptr;
  const Uint64 start = SDL_GetTicks();
  while (l.result.empty()) {
    portAuroraBeginFrame(); // exits the program if the window is closed
    draw(l, static_cast<float>(SDL_GetTicks() - start) / 1000.f);
    launcherStyleEnd();
    aurora_end_frame();
  }
  gPortLauncherShown = false;
  portUiReleaseInput(); // the game gets the keyboard and controller from here
  std::snprintf(path, size, "%s", l.result.c_str());
  return 1;
}
