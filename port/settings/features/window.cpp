// The desktop window (Windows, macOS, Linux): it opens where it was left, at
// its size, maximized or full screen as it was; it never opens larger than
// the screen or where no screen is (a monitor unplugged since); the first
// time, it fills about 80% of the screen at the picture's shape (Aurora's
// fixed 1280x960 was taller than many laptop screens). The Display tab sizes
// it to a multiple of the game's 640x480 picture, or to the picture's shape
// (no bars beside it), and can keep it above other windows. Alt+Enter toggles
// full screen, as F11 does.
//
// The display mode is the fullscreen setting: 0 a window, 1 borderless (a
// window the size of the screen, at the desktop's own mode: switching away is
// instant), 2 full screen (exclusive, at the screen's own resolution and
// refresh rate: the display is the game's alone while it has the focus). F11
// and Alt+Enter switch between the window and the full-screen kind last chosen
// (fullscreen_kind).
//
// Settings: window_x, window_y, window_w, window_h (the last windowed place
// and size, in the desktop's points), window_max, window_remember,
// window_on_top, fullscreen_kind. main_aurora.c passes the saved size and place to Aurora
// before the window is made (portWindowConfig), then portWindowRestore checks
// them against the screens once SDL is up.
#include "port/settings.h"
#include "port/pause_forms.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/vi.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" {
#include "port/widescreen.h"
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define PORT_DESKTOP_WINDOW 1
#endif

namespace {

constexpr int kNoValue = -100000;

struct Saved {
  int *x, *y, *w, *h, *max, *remember, *onTop, *fullKind;
};

Saved& saved() {
  static Saved s{};
  if (s.x == nullptr) {
    s.x = portSettingsDeclare("window_x", kNoValue, kNoValue, 100000);
    s.y = portSettingsDeclare("window_y", kNoValue, kNoValue, 100000);
    s.w = portSettingsDeclare("window_w", 0, 0, 16384);
    s.h = portSettingsDeclare("window_h", 0, 0, 16384);
    s.max = portSettingsDeclare("window_max", 0, 0, 1);
    s.remember = portSettingsDeclare("window_remember", 1, 0, 1);
    s.onTop = portSettingsDeclare("window_on_top", 0, 0, 1);
    s.fullKind = portSettingsDeclare("fullscreen_kind", 1, 1, 2);
  }
  return s;
}

bool sDirty;
Uint64 sDirtyAt;
bool sRestored;

void markDirty() {
  sDirty = true;
  sDirtyAt = SDL_GetTicks();
}

SDL_Window* window() { return aurora_get_window(); }

bool isFullscreen(SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0; }
bool isMaximized(SDL_Window* w) { return (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) != 0; }

// the picture's shape: 4:3, or the widescreen aspect (the window's own: 4:3 for sizing)
float pictureAspect() {
  const float a = portWidescreenAspect();
  return a > 1.f && a < 4.f ? a : 4.f / 3.f;
}

// the usable area (without task bar, dock, menu bar) of the screen the window is on
SDL_Rect usableBounds(SDL_Window* w) {
  SDL_DisplayID d = w != nullptr ? SDL_GetDisplayForWindow(w) : 0;
  if (d == 0) {
    d = SDL_GetPrimaryDisplay();
  }
  SDL_Rect r{0, 0, 1280, 720};
  if (!SDL_GetDisplayUsableBounds(d, &r) || r.w <= 0 || r.h <= 0) {
    SDL_GetDisplayBounds(d, &r);
  }
  return r;
}

// a size of the picture's shape filling `fill` of the area at most
void fitted(const SDL_Rect& area, float fill, float aspect, int* w, int* h) {
  float fw = area.w * fill, fh = area.h * fill;
  if (fw / fh > aspect) {
    fw = fh * aspect;
  } else {
    fh = fw / aspect;
  }
  *w = std::max(640, static_cast<int>(fw));
  *h = std::max(480, static_cast<int>(fh));
}

// some of the window's top (where it's grabbed) is on a screen
bool onSomeScreen(int x, int y, int w) {
  int count = 0;
  SDL_DisplayID* ids = SDL_GetDisplays(&count);
  bool on = false;
  for (int i = 0; i < count && !on; i++) {
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(ids[i], &r)) {
      const SDL_Rect top{x + 32, y, std::max(1, w - 64), 32};
      SDL_Rect cut;
      on = SDL_GetRectIntersection(&top, &r, &cut) && cut.w >= 64;
    }
  }
  SDL_free(ids);
  return on;
}

void centre(SDL_Window* w, int width, int height) {
  const SDL_Rect area = usableBounds(w);
  SDL_SetWindowSize(w, width, height);
  SDL_SetWindowPosition(w, area.x + (area.w - width) / 2, area.y + (area.h - height) / 2);
}

// Size the window to k times the 640x480 picture (at its shape), or k 0: to
// the picture's shape at its current height. Within the screen.
void sizeTo(float k) {
  SDL_Window* w = window();
  if (w == nullptr) {
    return;
  }
  if (isFullscreen(w)) {
    portSettings()->fullscreen = 0;
    portSettingsApply();
  }
  if (isMaximized(w)) {
    SDL_RestoreWindow(w);
  }
  const float aspect = pictureAspect();
  int width, height;
  if (k <= 0.f) {
    SDL_GetWindowSize(w, &width, &height);
    width = static_cast<int>(std::lround(height * aspect));
  } else {
    height = static_cast<int>(480 * k);
    width = static_cast<int>(std::lround(height * aspect));
  }
  const SDL_Rect area = usableBounds(w);
  if (width > area.w || height > area.h) {
    fitted(area, 1.f, aspect, &width, &height);
  }
  centre(w, width, height);
  markDirty();
}

bool sOnTopRefused; // the system said no (Wayland: a window can't put itself above others)

void applyOnTop() {
  if (SDL_Window* w = window()) {
    sOnTopRefused = !SDL_SetWindowAlwaysOnTop(w, *saved().onTop != 0) && *saved().onTop != 0;
  }
}

void windowSection(int) {
  Saved& s = saved();
  SDL_Window* w = window();
  const SDL_Rect area = usableBounds(w);
  const float aspect = pictureAspect();
  ImGui::TextUnformatted(portTr("Window size"));
  static const float kScales[] = {1.f, 1.5f, 2.f, 3.f, 4.f};
  static const char* kNames[] = {"1x", "1.5x", "2x", "3x", "4x"};
  for (int i = 0; i < 5; i++) {
    const int height = static_cast<int>(480 * kScales[i]);
    const int width = static_cast<int>(std::lround(height * aspect));
    const bool fits = width <= area.w && height <= area.h;
    if (i > 0) {
      ImGui::SameLine();
    }
    ImGui::BeginDisabled(!fits);
    if (ImGui::Button(portTrLabel(kNames[i]))) {
      sizeTo(kScales[i]);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip(fits ? "%dx%d" : portTr("%dx%d: larger than this screen"), width, height);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Fit the picture"))) {
    sizeTo(0.f);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", portTr("The window's width to the picture's shape: no bars beside it."));
  }
  ImGui::SameLine();
  const bool full = w != nullptr && isFullscreen(w);
  ImGui::BeginDisabled(full);
  if (ImGui::Button(portTrLabel("Centre"))) {
    if (w != nullptr && !isFullscreen(w)) {
      int width, height;
      SDL_GetWindowSize(w, &width, &height);
      centre(w, width, height);
      markDirty();
    }
  }
  ImGui::EndDisabled();
  if (full && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", portTr("Not in full screen: F11 leaves it."));
  }
  bool remember = *s.remember != 0;
  if (ImGui::Checkbox(portTrLabel("Remember the window's size and place"), &remember)) {
    *s.remember = remember ? 1 : 0;
    portSettingsSave();
  }
  bool top = *s.onTop != 0;
  if (ImGui::Checkbox(portTrLabel("Keep the window above others"), &top)) {
    *s.onTop = top ? 1 : 0;
    applyOnTop();
    portSettingsSave();
  }
  if (sOnTopRefused && *s.onTop != 0) {
    ImGui::TextDisabled("%s", portTr("This system doesn't let a window keep itself above others."));
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Drag the window's edges to any size; the picture keeps its shape. F11 or Alt+Enter: full "
                     "screen. The window opens where you left it next time."));
  ImGui::PopStyleColor();
}

// Records the window's place and size as it changes; Alt+Enter.
int windowEvent(const void* ev) {
  const SDL_Event* e = static_cast<const SDL_Event*>(ev);
  SDL_Window* w = window();
  if (w == nullptr) {
    return 0;
  }
  Saved& s = saved();
  switch (e->type) {
  case SDL_EVENT_KEY_DOWN:
    if ((e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) && (e->key.mod & SDL_KMOD_ALT) && !e->key.repeat) {
      portWindowToggleFullscreen();
      return 1;
    }
    return 0;
  case SDL_EVENT_WINDOW_MOVED:
  case SDL_EVENT_WINDOW_RESIZED:
    if (sRestored && !isFullscreen(w) && !isMaximized(w) && !(SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED)) {
      int x, y, width, height;
      SDL_GetWindowPosition(w, &x, &y);
      SDL_GetWindowSize(w, &width, &height);
      if (x != *s.x || y != *s.y || width != *s.w || height != *s.h) {
        *s.x = x;
        *s.y = y;
        *s.w = width;
        *s.h = height;
        markDirty();
      }
    }
    return 0;
  case SDL_EVENT_WINDOW_MAXIMIZED:
  case SDL_EVENT_WINDOW_RESTORED:
    if (sRestored && !isFullscreen(w)) {
      const int max = e->type == SDL_EVENT_WINDOW_MAXIMIZED ? 1 : 0;
      if (*s.max != max && !(SDL_GetWindowFlags(w) & SDL_WINDOW_MINIMIZED)) {
        *s.max = max;
        markDirty();
      }
    }
    return 0;
  case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
  case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
    // full screen from the system (macOS's green button, a window manager's
    // key): the setting follows, so the page and the next start agree
    const bool full = e->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
    if ((portSettings()->fullscreen != 0) != full) {
      portSettings()->fullscreen = full ? *s.fullKind : 0;
      markDirty();
    }
    return 0;
  }
  default:
    return 0;
  }
}

// the place and size saved a moment after the window stops moving
void windowTick(int) {
  if (sDirty && SDL_GetTicks() - sDirtyAt > 600) {
    sDirty = false;
    portSettingsSave();
  }
}

// The Start menu's form (port/include/port/pause_forms.h): the size, kept on top, remembered
constexpr float kFormScales[] = {1.f, 1.5f, 2.f, 3.f, 4.f};
constexpr const char* kFormScaleNames[] = {"1x", "1.5x", "2x", "3x", "4x"};

int formScale() { // the size nearest the window's now
  int width = 0, height = 0;
  if (SDL_Window* w = window()) {
    SDL_GetWindowSize(w, &width, &height);
  }
  int best = 0;
  for (int i = 1; i < 5; i++) {
    if (std::fabs(480 * kFormScales[i] - height) < std::fabs(480 * kFormScales[best] - height)) {
      best = i;
    }
  }
  return best;
}

int formRows(PortPauseRow* out, int max) {
  if (max < 3) {
    return 0;
  }
  Saved& s = saved();
  out[0] = PortPauseRow{"Window size", "The window this many times the game's picture; left and right change it.",
                        {}, {}, kFormScaleNames[formScale()], 0, 0};
  out[1] = PortPauseRow{"Keep the window above others", nullptr, {}, {}, *s.onTop ? "On" : "Off", 1, 0};
  out[2] = PortPauseRow{"Remember the window's size and place", nullptr, {}, {}, *s.remember ? "On" : "Off", 2, 0};
  return 3;
}

const char* formAct(int id, int dir) {
  Saved& s = saved();
  if (id == 0) {
    SDL_Window* w = window();
    const SDL_Rect area = usableBounds(w);
    int to = formScale() + (dir == 0 ? 1 : dir);
    to = (to + 5) % 5;
    const int height = static_cast<int>(480 * kFormScales[to]);
    if (height > area.h || static_cast<int>(std::lround(height * pictureAspect())) > area.w) {
      return "That size is larger than this screen.";
    }
    sizeTo(kFormScales[to]);
  } else if (id == 1) {
    *s.onTop = !*s.onTop;
    applyOnTop();
    portSettingsSave();
  } else if (id == 2) {
    *s.remember = !*s.remember;
    portSettingsSave();
  }
  return nullptr;
}

const PortPauseForm kForm = {"Display", "Window", formRows, formAct, PORT_PAUSE_FORM_OWN};

struct WindowSettings {
  WindowSettings() {
    saved();
#if defined(PORT_DESKTOP_WINDOW)
    portSettingsAddSection("Display", "Window", nullptr, 0, windowSection,
                           "windowed size scale 1x 2x 3x resize centre center fit bars maximize always on top "
                           "remember position alt enter");
    portSettingsAddOverlay(windowTick, windowEvent);
    portPauseAddForm(&kForm);
#endif
#if defined(__ANDROID__)
    // Android: the game's window is SDL's full screen unless the player wants
    // the status and navigation bars (portSettingsApply and portWindowConfig
    // read the key; TtydActivity.java keeps the bars hidden while it's off).
    portSettingsDeclare("system_bars", 0, 0, 1);
    portSettingsAddToggle("Display", "Show the system bars",
                          "Android's status bar and navigation bar stay on the screen, beside the picture. Off: "
                          "the game has the whole screen; swipe in from an edge to see them for a moment.",
                          0, "system_bars", "status bar navigation bar full screen fullscreen immersive notch");
#endif
  }
} sWindow;

} // namespace

// The display mode in place (portSettingsApply, on a change): mode 2's
// exclusive mode is the screen's own (its desktop resolution and refresh
// rate), 1's none (borderless); then full screen on or off.
extern "C" void portWindowSetDisplayMode(int mode) {
#if defined(PORT_DESKTOP_WINDOW)
  SDL_Window* w = window();
  if (w != nullptr && mode != 0) {
    const SDL_DisplayMode* desktop = mode == 2 ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(w)) : nullptr;
    if (!SDL_SetWindowFullscreenMode(w, desktop)) {
      std::fprintf(stderr, "window: the full-screen mode wasn't taken: %s\n", SDL_GetError());
    }
    *saved().fullKind = mode;
  }
#else
  (void)mode;
#endif
  VISetWindowFullscreen(mode != 0);
}

// F11, Alt+Enter: the window, or the full-screen kind last chosen
extern "C" void portWindowToggleFullscreen(void) {
  PortSettings* s = portSettings();
  s->fullscreen = s->fullscreen != 0 ? 0 : *saved().fullKind;
  portSettingsSave(); // applies it
}

// Before aurora_initialize: the saved size and place for the window it makes
// (0 / unchanged when there are none), and full screen from the start.
extern "C" void portWindowConfig(const char* userPath, int* x, int* y, unsigned* w, unsigned* h, int* fullscreen) {
#if defined(PORT_DESKTOP_WINDOW)
  // (a Steam Deck's first start: full screen, as its first-start settings say, settings_aurora.c)
  *fullscreen = portSettingsPeek(userPath, "fullscreen", portSteamDeck()) != 0 ? 1 : 0;
  if (portSettingsPeek(userPath, "window_remember", 1) == 0) {
    return;
  }
  const int sw = portSettingsPeek(userPath, "window_w", 0), sh = portSettingsPeek(userPath, "window_h", 0);
  if (sw >= 640 && sh >= 480) {
    *w = static_cast<unsigned>(sw);
    *h = static_cast<unsigned>(sh);
  }
  const int sx = portSettingsPeek(userPath, "window_x", kNoValue), sy = portSettingsPeek(userPath, "window_y", kNoValue);
  if (sx != kNoValue && sy != kNoValue && *x < 0 && *y < 0) {
    // Aurora takes a negative place as "anywhere": a window left on a screen
    // left of or above the main one is put back by portWindowRestore instead
    if (sx >= 0 && sy >= 0) {
      *x = sx;
      *y = sy;
    }
  }
#elif defined(__ANDROID__)
  // Android: full screen (the bars hidden) from the start unless system_bars
  // is on; settings_aurora.c's portSettingsApply follows later changes.
  (void)x, (void)y, (void)w, (void)h;
  *fullscreen = portSettingsPeek(userPath, "system_bars", 0) == 0 ? 1 : 0;
#else
  (void)userPath, (void)x, (void)y, (void)w, (void)h, (void)fullscreen;
#endif
}

// After the settings are loaded: the window checked against the screens
// (inside one, no larger than it), maximized as it was, or on the first run
// sized to the screen. keepPlace: $TTYD_WINDOW_POS put it somewhere on purpose.
extern "C" void portWindowRestore(int keepPlace) {
#if defined(PORT_DESKTOP_WINDOW)
  SDL_Window* w = window();
  Saved& s = saved();
  if (w == nullptr) {
    return;
  }
  applyOnTop();
  if (!keepPlace && !isFullscreen(w)) {
    const SDL_Rect area = usableBounds(w);
    const bool remember = *s.remember != 0;
    int width, height;
    if (remember && *s.w >= 640 && *s.h >= 480) {
      width = std::min(*s.w, std::max(640, area.w));
      height = std::min(*s.h, std::max(480, area.h));
    } else {
      fitted(area, 0.8f, pictureAspect(), &width, &height);
    }
    if (remember && *s.x != kNoValue && *s.y != kNoValue && onSomeScreen(*s.x, *s.y, width)) {
      SDL_SetWindowSize(w, width, height);
      SDL_SetWindowPosition(w, *s.x, *s.y);
    } else {
      centre(w, width, height);
    }
    if (remember && *s.max) {
      SDL_MaximizeWindow(w);
    }
  }
  sRestored = true;
#else
  (void)keepPlace;
#endif
}
