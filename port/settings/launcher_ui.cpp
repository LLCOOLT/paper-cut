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
#include "launcher_style.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui.h>

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
};

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

bool touchScreen() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  return true;
#else
  return false;
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

void drawLibrary(Launcher& l, float scale) {
  const PortVersionInfo* self = portVersionSelf();
  const float listWidth = std::max(280.f * scale, ImGui::GetContentRegionAvail().x * 0.42f);
  beginCard("library", ImVec2(listWidth, 0), scale);
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
    if (ImGui::Selectable("##disc", l.selected == static_cast<int>(i), ImGuiSelectableFlags_AllowDoubleClick,
                          ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2))) {
      l.selected = static_cast<int>(i);
      l.status.clear();
      if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        play(l, l.selected);
      }
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
  if (launcherRedButton(l.discs.empty() ? "Add a disc image..." : "Add another...")) {
    char path[4096];
    if (portAuroraPickDisc(path, sizeof(path))) {
      l.selected = add(l, path);
      l.status.clear();
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

void drawDetails(Launcher& l, float scale) {
  beginCard("details", ImVec2(0, 0), scale);
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
  const char* label = d.version != nullptr && d.version != portVersionSelf() ? "Play (starts its build)" : "Play";
  if (bigButton(label, std::min(ImGui::GetContentRegionAvail().x, 300.f * scale), scale)) {
    play(l, l.selected);
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
  if (ImGui::Checkbox("Next time, start with the last disc played", &skip)) {
    s->launcherSkip = skip ? 1 : 0;
    portSettingsSave();
  }
  ImGui::TextDisabled("To see this screen again: Settings > System in the game.");
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
  ImGui::Spacing();
  ImGui::TextDisabled("Typeface: Fredoka (SIL Open Font License).");
  ImGui::EndChild();
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

  // the port's name cut out of paper, centred at the top of the stage, and
  // the game it plays under it
  ImDrawList* fg = ImGui::GetBackgroundDrawList();
  const char* title = "Paper Cut";
  const char* game = "Paper Mario: The Thousand-Year Door";
  const float titleSize = gLauncherFonts.title->FontSize * fontScale * 1.25f;
  const float gameSize = gLauncherFonts.heading->FontSize * fontScale;
  const ImVec2 tsz = gLauncherFonts.title->CalcTextSizeA(titleSize, FLT_MAX, 0.f, title);
  const ImVec2 gsz = gLauncherFonts.heading->CalcTextSizeA(gameSize, FLT_MAX, 0.f, game);
  const float cx = (stageMin.x + stageMax.x) * 0.5f;
  float y = stageMin.y;
  launcherOutlinedText(fg, gLauncherFonts.title, titleSize, ImVec2(cx - tsz.x * 0.5f, y), title, kCurtain,
                       IM_COL32(255, 255, 255, 255), 6.f * scale);
  const float sparkleY = y + tsz.y * 0.5f;
  for (int side = -1; side <= 1; side += 2) {
    const float sx = cx + side * (tsz.x * 0.5f + 28.f * scale);
    const float r = (10.f + 2.f * std::sin(time * 2.f + side)) * scale;
    const ImVec2 c(sx, sparkleY);
    const float k = r * 0.3f;
    ImVec2 pts[8] = {{c.x, c.y - r * 1.6f}, {c.x + k, c.y - k}, {c.x + r * 1.6f, c.y}, {c.x + k, c.y + k},
                     {c.x, c.y + r * 1.6f}, {c.x - k, c.y + k}, {c.x - r * 1.6f, c.y}, {c.x - k, c.y - k}};
    fg->AddConvexPolyFilled(pts, 8, kGold);
  }
  y += tsz.y - 4.f * scale;
  launcherOutlinedText(fg, gLauncherFonts.heading, gameSize, ImVec2(cx - gsz.x * 0.5f, y), game, kGold,
                       IM_COL32(60, 20, 30, 255), 3.f * scale);
  y += gsz.y + 14.f * scale;

  // the panels below the title
  ImGui::SetNextWindowPos(ImVec2(stageMin.x, y), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(stageMax.x - stageMin.x, stageMax.y - y), ImGuiCond_Always);
  ImGui::Begin("launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
  ImGui::PushFont(gLauncherFonts.body);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 247, 228, 255)); // tab labels on the dark stage
  const bool tabs = ImGui::BeginTabBar("launcher");
  ImGui::PopStyleColor();
  if (tabs) {
    if (ImGui::BeginTabItem("Play", nullptr, startTab("Play"))) {
      drawLibrary(l, scale);
      ImGui::SameLine(0, 18.f * scale);
      drawDetails(l, scale);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Settings", nullptr, startTab("Settings"))) {
      beginCard("settings", ImVec2(0, 0), scale);
      portSettingsTabs(false);
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("About", nullptr, startTab("About"))) {
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
  const Uint64 start = SDL_GetTicks();
  while (l.result.empty()) {
    portAuroraBeginFrame(); // exits the program if the window is closed
    draw(l, static_cast<float>(SDL_GetTicks() - start) / 1000.f);
    launcherStyleEnd();
    aurora_end_frame();
  }
  std::snprintf(path, size, "%s", l.result.c_str());
  return 1;
}
