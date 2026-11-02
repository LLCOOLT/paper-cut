// On-screen touch controls (TTYD_TOUCH_OVERLAY; on by default on iOS).
//
// Touches become a PADStatus handed to Aurora as port 1's virtual status
// (PADSetVirtualStatus), which Aurora's PADRead merges with whatever else
// drives that port, so the game reads it like a controller (no change to
// Aurora or the game). The controls are drawn over the frame with Aurora's
// Dear ImGui (foreground draw list, window coordinates in points).
//
// - Hidden (and the virtual status cleared) once a gamepad is used (a button,
//   or a stick or trigger past a dead zone); shown again when the screen is
//   touched or the last gamepad goes away. Connected is not enough: the iOS
//   Simulator (and some systems) report a gamepad that never sends input.
// - Left side: a floating analog stick (it centres where the finger lands).
//   Right side: A (large), B, X, Y in the GameCube arrangement; L and R
//   (full presses) in the top corners, Z under R, START at the top, and
//   fast-forward (>>, port/src/aurora/speed_aurora.c) beside it.
// - A button acts on touch-down: the status is updated when the touch event
//   arrives, before the game's PADRead of that frame.
// - Layout follows port/include/port/layout.h: in landscape the controls sit
//   over the safe area, in the margins beside the picture; held upright, in
//   the area below the picture. Each control can be moved (portTouchEditLayout,
//   from the settings page), separately for each orientation, saved in
//   touch_layout.txt in the user folder.
//
// $TTYD_TOUCH_MOUSE=1 (desktop testing): the mouse also produces touches.
// $TTYD_TOUCH_TEST=<frame>:<x>,<y>:<frames>[;...] (tests): a touch at window
// fraction x,y on that drawn frame, held that many frames, sent through SDL's
// event queue like a real one.
#include "port/touch_overlay.h"
#include "port/gameplus.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/pad.h>
#include "port/settings.h"
#include "port/menu_tap.h"
#include "port/speed.h"
#include "port/layout.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
extern "C" void portIosHapticTap(void); // port/ios/ios_app.m
#elif defined(__ANDROID__)
// A light tap for a button press: a short pulse on the device's vibrator (the
// one Aurora rumbles for phone rumble), opened on first use.
static void portAndroidHapticTap() {
  static SDL_Haptic* sHaptic;
  static bool sTried;
  if (!sTried) {
    sTried = true;
    int count = 0;
    SDL_HapticID* ids = SDL_GetHaptics(&count);
    for (int i = 0; ids != nullptr && i < count && sHaptic == nullptr; i++) {
      const char* name = SDL_GetHapticNameForID(ids[i]);
      if (name != nullptr && SDL_strcmp(name, "VIBRATOR_SERVICE") == 0) {
        sHaptic = SDL_OpenHaptic(ids[i]);
      }
    }
    SDL_free(ids);
    if (sHaptic != nullptr && !SDL_InitHapticRumble(sHaptic)) {
      SDL_CloseHaptic(sHaptic);
      sHaptic = nullptr;
    }
  }
  if (sHaptic != nullptr) {
    SDL_PlayHapticRumble(sHaptic, 0.3f, 12);
  }
}
#endif
extern "C" int portSettingsOwnsPoint(float x, float y); // port/settings/settings_ui.cpp
extern "C" void portSettingsToggle(void);                 // port/settings/settings_ui.cpp
#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

enum Control {
  kNone = -1,
  kStick,
  kA,
  kB,
  kX,
  kY,
  kZ,
  kL,
  kR,
  kStart,
  kFast, // fast-forward: not a controller button
  kSwap, // Gameplay+ partner quick-swap (port/include/port/gameplus.h), shown when it is on
  kMenu, // the settings page (port/settings/settings_ui.cpp): not a controller button
  kControlCount,
};

struct ButtonDef {
  const char* label;
  u16 padButton;
};

// Index by Control (kStick has no button).
const ButtonDef kButtons[kControlCount] = {
    {nullptr, 0},
    {"A", PAD_BUTTON_A},
    {"B", PAD_BUTTON_B},
    {"X", PAD_BUTTON_X},
    {"Y", PAD_BUTTON_Y},
    {"Z", PAD_TRIGGER_Z},
    {"L", PAD_TRIGGER_L},
    {"R", PAD_TRIGGER_R},
    {"START", PAD_BUTTON_START},
    {">>", 0},
    {"P", 0},
    {nullptr, 0}, // kMenu: three bars, drawn
};

constexpr u32 kPort = 0; // the game's controller 1

struct Circle {
  float x, y, r;
};

// A GameCube X/Y button: a curved "kidney bean" wrapped around A, an arc of
// mid radius rm and half thickness t from angle a0 to a1 (radians, screen
// coordinates: y down) about A's centre, with rounded ends.
struct Bean {
  float cx, cy, rm, t, a0, a1;
};

struct Layout {
  Circle c[kControlCount]; // kStick: the stick's resting base; X/Y: their label spot
  Bean bean[kControlCount]; // X and Y only
  float stickZoneRight;    // touches left of this (and below stickZoneTop) drive the stick
  float stickZoneTop;
  float u;                 // the size unit
  int portrait;
  PortLayout area;         // the picture and the controls' area
};

// Where the user moved each control, in size units, by orientation
// (0 landscape, 1 portrait). X and Y go with A.
struct Offset {
  float dx, dy;
};
Offset sOffset[2][kControlCount];
bool sOffsetsRead;

// Layout editing (portTouchEditLayout): dragging moves controls.
bool sEditing;
int sEditControl = -1;
SDL_FingerID sEditFinger;
float sEditX, sEditY;

constexpr int kMaxFingers = 10;
struct Finger {
  SDL_FingerID id;
  Control control;
  bool used;
};

Finger sFingers[kMaxFingers];
bool sDown[kControlCount];
bool sStickActive;
float sStickOriginX, sStickOriginY, sStickX01, sStickY01; // origin and knob offset (-1..1)
bool sVisible = true;
bool sStatusSet;
bool sMouseInit;

SDL_Window* window() { return aurora_get_window(); }

std::string layoutPath() { return std::string(portSettingsUserPath()) + "/touch_layout.txt"; }

void readOffsets() {
  sOffsetsRead = true;
  std::memset(sOffset, 0, sizeof(sOffset));
  char* text = static_cast<char*>(SDL_LoadFile(layoutPath().c_str(), nullptr));
  if (text == nullptr) {
    return;
  }
  // one control a line: <orientation 0|1> <control> <dx> <dy>
  for (char* line = text; line != nullptr && *line != '\0';) {
    int o = 0;
    int c = 0;
    float dx = 0.f;
    float dy = 0.f;
    if (std::sscanf(line, "%d %d %f %f", &o, &c, &dx, &dy) == 4 && o >= 0 && o < 2 && c >= 0 && c < kControlCount) {
      sOffset[o][c] = {dx, dy};
    }
    line = std::strchr(line, '\n');
    if (line != nullptr) {
      line++;
    }
  }
  SDL_free(text);
}

void writeOffsets() {
  SDL_IOStream* io = SDL_IOFromFile(layoutPath().c_str(), "w");
  if (io == nullptr) {
    return;
  }
  for (int o = 0; o < 2; o++) {
    for (int c = 0; c < kControlCount; c++) {
      if (sOffset[o][c].dx != 0.f || sOffset[o][c].dy != 0.f) {
        SDL_IOprintf(io, "%d %d %.4f %.4f\n", o, c, sOffset[o][c].dx, sOffset[o][c].dy);
      }
    }
  }
  SDL_CloseIO(io);
}

bool computeLayout(Layout& l) {
  if (!portLayoutGet(&l.area)) {
    return false;
  }
  if (!sOffsetsRead) {
    readOffsets();
  }
  const float sx = l.area.ctrlX;
  const float sy = l.area.ctrlY;
  const float sw = l.area.ctrlW;
  const float sh = l.area.ctrlH;
  if (sw <= 0.f || sh <= 0.f) {
    return false;
  }
  l.portrait = l.area.portrait;
  // Sizes follow the area's shorter side (the area below the picture when
  // held upright), no bigger than on a large phone (a tablet); the settings'
  // size scales them (and their distances from the corners).
  float u = l.portrait ? std::fmin(sw, sh * 1.1f) : std::fmin(sh, sw * 0.75f);
  u = std::fmin(u, 440.f) * static_cast<float>(portSettings()->touchSize) / 100.f;
  l.u = u;
  const Offset* off = sOffset[l.portrait];
  auto at = [&](int c, float x, float y, float r) {
    return Circle{x + off[c].dx * u, y + off[c].dy * u, r};
  };
  const float right = sx + sw;
  const float bottom = sy + sh;
  const Circle a = at(kA, right - 0.22f * u, bottom - 0.22f * u, 0.085f * u);
  l.c[kA] = a;
  l.c[kB] = at(kB, right - 0.22f * u - 0.15f * u, bottom - 0.22f * u + 0.07f * u, 0.058f * u);
  // X: a vertical bean hugging A's right side; Y: a horizontal one over A's
  // top, as on the GameCube controller.
  constexpr float kDeg = 3.14159265f / 180.f;
  l.bean[kX] = {a.x, a.y, a.r * 1.78f, a.r * 0.40f, -62.f * kDeg, 30.f * kDeg};
  l.bean[kY] = {a.x, a.y, a.r * 1.78f, a.r * 0.40f, -168.f * kDeg, -100.f * kDeg};
  for (int b = kX; b <= kY; b++) {
    const Bean& n = l.bean[b];
    const float mid = 0.5f * (n.a0 + n.a1);
    l.c[b] = {n.cx + n.rm * std::cos(mid), n.cy + n.rm * std::sin(mid), n.t};
  }
  l.c[kStick] = at(kStick, sx + 0.22f * u, bottom - 0.24f * u, 0.13f * u);
  // Landscape with little room beside the picture (a tablet's near-4:3
  // screen): the controls are on the picture, so the shoulder buttons go
  // above the thumbs' clusters and START at the bottom, clear of the status
  // bar at the top and of the window's own controls in its corner.
  if (!l.portrait && l.area.picX - sx < 0.30f * u) {
    const Circle st = l.c[kStick];
    l.c[kL] = at(kL, st.x, st.y - 0.32f * u, 0.05f * u);
    l.c[kR] = at(kR, a.x + 0.03f * u, a.y - 0.36f * u, 0.05f * u);
    l.c[kZ] = at(kZ, a.x - 0.13f * u, a.y - 0.33f * u, 0.045f * u);
    l.c[kStart] = at(kStart, sx + sw * 0.5f + 0.07f * u, bottom - 0.07f * u, 0.04f * u);
    l.c[kFast] = at(kFast, sx + sw * 0.5f - 0.07f * u, bottom - 0.07f * u, 0.04f * u);
    l.c[kSwap] = at(kSwap, a.x - 0.26f * u, a.y - 0.33f * u, 0.04f * u);
    l.c[kMenu] = at(kMenu, sx + sw * 0.5f + 0.21f * u, bottom - 0.07f * u, 0.04f * u);
  } else {
    l.c[kR] = at(kR, right - 0.12f * u, sy + 0.10f * u, 0.05f * u);
    l.c[kZ] = at(kZ, right - 0.12f * u, sy + 0.24f * u, 0.045f * u);
    l.c[kL] = at(kL, sx + 0.12f * u, sy + 0.10f * u, 0.05f * u);
    l.c[kStart] = at(kStart, sx + sw * 0.5f, sy + 0.07f * u, 0.04f * u);
    l.c[kFast] = at(kFast, sx + sw * 0.5f - 0.13f * u, sy + 0.07f * u, 0.04f * u);
    l.c[kSwap] = at(kSwap, right - 0.12f * u, sy + 0.37f * u, 0.04f * u);
    l.c[kMenu] = at(kMenu, sx + sw * 0.5f + 0.13f * u, sy + 0.07f * u, 0.04f * u);
  }
  l.stickZoneRight = std::fmax(sx + sw * 0.45f, l.c[kStick].x + l.c[kStick].r * 1.5f);
  l.stickZoneTop = std::fmin(sy + 0.2f * u, l.c[kStick].y - l.c[kStick].r * 1.5f);
  return true;
}

bool sGamepadInUse;
constexpr Sint16 kGamepadDeadZone = 8000;

bool anyGamepad() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  SDL_free(ids);
  return count > 0;
}

// The controls as port 1's virtual status (merged into PADRead by Aurora).
void apply() {
  PADStatus status;
  std::memset(&status, 0, sizeof(status));
  for (int c = kA; c < kControlCount; c++) {
    if (sDown[c]) {
      status.button |= kButtons[c].padButton;
    }
  }
  if (sDown[kL]) {
    status.triggerLeft = 255;
  }
  if (sDown[kR]) {
    status.triggerRight = 255;
  }
  status.stickX = static_cast<s8>(std::lround(sStickX01 * 127.f));
  status.stickY = static_cast<s8>(std::lround(-sStickY01 * 127.f)); // GameCube: up is positive
  PADSetVirtualStatus(kPort, &status);
  sStatusSet = true;
}

void clearStatus() {
  if (sStatusSet) {
    PADClearVirtualStatus(kPort);
    sStatusSet = false;
  }
}

void setStick(float x, float y) {
  sStickX01 = x;
  sStickY01 = y;
}

void moveStick(const Layout& l, float px, float py) {
  const float r = l.c[kStick].r;
  float dx = (px - sStickOriginX) / r;
  float dy = (py - sStickOriginY) / r;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1.0f || (portSettings()->autoRun && len > 0.3f)) {
    // auto-run: past a small tilt, a full one
    dx /= len;
    dy /= len;
  }
  setStick(dx, dy);
}

bool isBean(int c) {
  return c == kX || c == kY;
}

// the quick-swap button only while its setting is on
bool shown(int c) {
  return c != kSwap || portGameplus("quick_swap");
}

// Distance from (px, py) to a bean's centre line (its arc and end points),
// in units of its half thickness: <= 1 is on the button.
float beanDistance(const Bean& n, float px, float py) {
  const float dx = px - n.cx;
  const float dy = py - n.cy;
  float ang = std::atan2(dy, dx);
  float best;
  if (ang >= n.a0 && ang <= n.a1) {
    best = std::fabs(std::hypot(dx, dy) - n.rm);
  } else {
    const float e0 = std::hypot(px - (n.cx + n.rm * std::cos(n.a0)), py - (n.cy + n.rm * std::sin(n.a0)));
    const float e1 = std::hypot(px - (n.cx + n.rm * std::cos(n.a1)), py - (n.cy + n.rm * std::sin(n.a1)));
    best = std::fmin(e0, e1);
  }
  return best / n.t;
}

Control hitTest(const Layout& l, float px, float py) {
  Control best = kNone;
  float bestDist = 0.f;
  for (int c = kA; c < kControlCount; c++) {
    if (!shown(c)) {
      continue;
    }
    const Circle& b = l.c[c];
    // in button radii, so a bean and a round button compare fairly
    const float d = isBean(c) ? beanDistance(l.bean[c], px, py) : std::hypot(px - b.x, py - b.y) / b.r;
    if (d <= 1.35f && (best == kNone || d < bestDist)) {
      best = static_cast<Control>(c);
      bestDist = d;
    }
  }
  if (best == kNone && px < l.stickZoneRight && py > l.stickZoneTop) {
    return kStick;
  }
  const bool onPicture = px >= l.area.picX && px < l.area.picX + l.area.picW && py >= l.area.picY &&
                         py < l.area.picY + l.area.picH;
  // with a selection list up, a tap on the picture is a tap on its rows
  if (best == kNone && onPicture && portMenuOpen()) {
    portMenuTap((px - l.area.picX) / l.area.picW, (py - l.area.picY) / l.area.picH);
    return kNone;
  }
  // a tap on the picture presses A (settings)
  if (best == kNone && portSettings()->tapA && onPicture) {
    return kA;
  }
  return best;
}

Finger* findFinger(SDL_FingerID id) {
  for (Finger& f : sFingers) {
    if (f.used && f.id == id) {
      return &f;
    }
  }
  return nullptr;
}

void release(Finger& f) {
  if (f.control == kStick) {
    sStickActive = false;
    setStick(0.f, 0.f);
  } else if (f.control != kNone) {
    sDown[f.control] = false;
    if (f.control == kFast) {
      portSpeedControl(0);
    } else if (f.control == kSwap) {
      gPortQuickSwapTouch = 0;
    } else if (f.control == kMenu) {
      portSettingsToggle(); // on the tap's release, so the page doesn't take this finger
    }
  }
  f.used = false;
}

// White at alpha a, scaled by the settings' opacity.
ImU32 white(int a) {
  return IM_COL32(255, 255, 255, a * portSettings()->touchOpacity / 100);
}

void drawCircle(ImDrawList* dl, const Circle& c, const char* label, bool down) {
  const ImU32 fill = down ? white(120) : white(45);
  dl->AddCircleFilled(ImVec2(c.x, c.y), c.r, fill, 40);
  dl->AddCircle(ImVec2(c.x, c.y), c.r, white(140), 40, 2.0f);
  if (label != nullptr) {
    const float size = std::strlen(label) > 1 ? c.r * 0.55f : c.r * 0.95f;
    const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.f, label);
    dl->AddText(ImGui::GetFont(), size, ImVec2(c.x - ext.x * 0.5f, c.y - ext.y * 0.5f),
                white(200), label);
  }
}

void drawBean(ImDrawList* dl, const Bean& n, const Circle& labelAt, const char* label, bool down) {
  const ImU32 fill = down ? white(120) : white(45);
  const ImU32 edge = white(140);
  constexpr int kSegs = 24;
  constexpr float kPi = 3.14159265f;
  const float ro = n.rm + n.t;
  const float ri = n.rm - n.t;
  // Fill: the band as convex quads, the ends as discs (the shape is concave,
  // so it cannot be one convex fill).
  for (int i = 0; i < kSegs; i++) {
    const float t0 = n.a0 + (n.a1 - n.a0) * i / kSegs;
    const float t1 = n.a0 + (n.a1 - n.a0) * (i + 1) / kSegs;
    const ImVec2 q[4] = {
        ImVec2(n.cx + ro * std::cos(t0), n.cy + ro * std::sin(t0)),
        ImVec2(n.cx + ro * std::cos(t1), n.cy + ro * std::sin(t1)),
        ImVec2(n.cx + ri * std::cos(t1), n.cy + ri * std::sin(t1)),
        ImVec2(n.cx + ri * std::cos(t0), n.cy + ri * std::sin(t0)),
    };
    dl->AddConvexPolyFilled(q, 4, fill);
  }
  const ImVec2 e0(n.cx + n.rm * std::cos(n.a0), n.cy + n.rm * std::sin(n.a0));
  const ImVec2 e1(n.cx + n.rm * std::cos(n.a1), n.cy + n.rm * std::sin(n.a1));
  // the caps' halves outside the band, so no part of the fill is drawn twice
  dl->PathArcTo(e1, n.t, n.a1, n.a1 + kPi, 12);
  dl->PathFillConvex(fill);
  dl->PathArcTo(e0, n.t, n.a0 + kPi, n.a0 + 2.f * kPi, 12);
  dl->PathFillConvex(fill);
  // Outline: outer arc, end cap, inner arc back, start cap.
  dl->PathArcTo(ImVec2(n.cx, n.cy), ro, n.a0, n.a1, kSegs);
  dl->PathArcTo(e1, n.t, n.a1, n.a1 + kPi, 12);
  dl->PathArcTo(ImVec2(n.cx, n.cy), ri, n.a1, n.a0, kSegs);
  dl->PathArcTo(e0, n.t, n.a0 + kPi, n.a0 + 2.f * kPi, 12);
  dl->PathStroke(edge, ImDrawFlags_Closed, 2.0f);
  if (label != nullptr) {
    const float size = labelAt.r * 1.1f;
    const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.f, label);
    dl->AddText(ImGui::GetFont(), size, ImVec2(labelAt.x - ext.x * 0.5f, labelAt.y - ext.y * 0.5f),
                white(200), label);
  }
}

// Layout editing: Done and Reset in the middle of the controls' area.
void editButtons(const Layout& l, Circle& done, Circle& reset) {
  const float cx = l.area.ctrlX + l.area.ctrlW * 0.5f;
  const float cy = l.area.ctrlY + l.area.ctrlH * 0.5f;
  const float r = std::fmax(0.07f * l.u, 28.f);
  done = {cx - r * 1.3f, cy, r};
  reset = {cx + r * 1.3f, cy, r};
}

void finishEditing() {
  sEditing = false;
  sEditControl = -1;
  writeOffsets();
  PADBlockInput(false);
}

void editEvent(const Layout& l, Uint32 type, SDL_FingerID id, float px, float py) {
  if (type == SDL_EVENT_FINGER_DOWN) {
    Circle done;
    Circle reset;
    editButtons(l, done, reset);
    if (std::hypot(px - done.x, py - done.y) <= done.r) {
      finishEditing();
      return;
    }
    if (std::hypot(px - reset.x, py - reset.y) <= reset.r) {
      std::memset(sOffset[l.portrait], 0, sizeof(sOffset[l.portrait]));
      return;
    }
    int best = -1;
    float bestDist = 0.f;
    for (int c = kStick; c < kControlCount; c++) {
      if (!shown(c)) {
        continue;
      }
      const float d = isBean(c) ? beanDistance(l.bean[c], px, py)
                                : std::hypot(px - l.c[c].x, py - l.c[c].y) / l.c[c].r;
      if (d <= 1.5f && (best < 0 || d < bestDist)) {
        best = isBean(c) ? kA : c; // X and Y go with A
        bestDist = d;
      }
    }
    if (best >= 0) {
      sEditControl = best;
      sEditFinger = id;
      sEditX = px;
      sEditY = py;
    }
  } else if (type == SDL_EVENT_FINGER_MOTION && sEditControl >= 0 && id == sEditFinger) {
    Offset& o = sOffset[l.portrait][sEditControl];
    o.dx += (px - sEditX) / l.u;
    o.dy += (py - sEditY) / l.u;
    sEditX = px;
    sEditY = py;
  } else if ((type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) && id == sEditFinger) {
    sEditControl = -1;
  }
}

// $TTYD_TOUCH_TEST: scripted touches, pushed as SDL finger events.
void pushFinger(SDL_EventType type, SDL_FingerID id, float x, float y) {
  SDL_Event e;
  SDL_zero(e);
  e.type = type;
  e.tfinger.touchID = 1;
  e.tfinger.fingerID = id;
  e.tfinger.x = x;
  e.tfinger.y = y;
  e.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.f : 1.f;
  e.tfinger.windowID = SDL_GetWindowID(window());
  SDL_PushEvent(&e);
}

void runTouchTest(unsigned frame) {
  static const char* script = std::getenv("TTYD_TOUCH_TEST");
  if (script == nullptr) {
    return;
  }
  SDL_FingerID id = 1000;
  for (const char* p = script; *p != '\0'; id++) {
    unsigned start = 0;
    unsigned hold = 0;
    float x = 0.f;
    float y = 0.f;
    if (std::sscanf(p, "%u:%f,%f:%u", &start, &x, &y, &hold) != 4) {
      return;
    }
    if (frame == start) {
      std::fprintf(stderr, "[touch] test: down at %.3f,%.3f (frame %u)\n", x, y, frame);
      pushFinger(SDL_EVENT_FINGER_DOWN, id, x, y);
    } else if (frame == start + hold) {
      std::fprintf(stderr, "[touch] test: up (frame %u)\n", frame);
      pushFinger(SDL_EVENT_FINGER_UP, id, x, y);
    }
    const char* next = std::strchr(p, ';');
    if (next == nullptr) {
      break;
    }
    p = next + 1;
  }
}

} // namespace

extern "C" void portTouchReleaseAll(void) {
  for (Finger& f : sFingers) {
    if (f.used) {
      release(f);
    }
  }
  if (sStatusSet) {
    apply();
  }
}

extern "C" void portTouchEditLayout(void) {
  portTouchReleaseAll();
  clearStatus();
  sGamepadInUse = false;
  sEditing = true;
  sEditControl = -1;
  PADBlockInput(true);
}

extern "C" void portTouchEvent(const SDL_Event* event) {
  if (!sMouseInit) {
    sMouseInit = true;
    if (const char* m = std::getenv("TTYD_TOUCH_MOUSE"); m != nullptr && m[0] == '1') {
      SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    }
  }
  switch (event->type) {
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    sGamepadInUse = !sEditing;
    return;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (!sEditing && (event->gaxis.value > kGamepadDeadZone || event->gaxis.value < -kGamepadDeadZone)) {
      sGamepadInUse = true;
    }
    return;
  case SDL_EVENT_GAMEPAD_REMOVED:
    if (!anyGamepad()) {
      sGamepadInUse = false;
    }
    return;
  case SDL_EVENT_FINGER_DOWN:
    if (sGamepadInUse) {
      // the screen was touched: the controls come back and this touch counts
      sGamepadInUse = false;
      sVisible = true;
    }
    break;
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    break;
  case SDL_EVENT_WINDOW_FOCUS_LOST:
  case SDL_EVENT_WINDOW_MINIMIZED:
  case SDL_EVENT_WILL_ENTER_BACKGROUND:
    portTouchReleaseAll();
    return;
  default:
    return;
  }
  if (!sVisible) {
    return;
  }
  Layout l;
  if (!computeLayout(l)) {
    return;
  }
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(window(), &ww, &wh);
  const float px = event->tfinger.x * static_cast<float>(ww);
  const float py = event->tfinger.y * static_cast<float>(wh);
  const SDL_FingerID id = event->tfinger.fingerID;

  if (sEditing) {
    editEvent(l, event->type, id, px, py);
    return;
  }
  if (event->type == SDL_EVENT_FINGER_DOWN) {
    Finger* f = findFinger(id);
    if (f != nullptr) {
      release(*f);
    }
    f = nullptr;
    for (Finger& slot : sFingers) {
      if (!slot.used) {
        f = &slot;
        break;
      }
    }
    if (f == nullptr) {
      return;
    }
    // on the settings page or the crash notice: theirs (Dear ImGui's)
    const Control c = portSettingsOwnsPoint(px, py) ? kNone : hitTest(l, px, py);
    if (c == kNone || (c == kStick && sStickActive)) {
      return;
    }
    *f = {id, c, true};
    if (c == kStick) {
      sStickActive = true;
      if (portSettings()->stickFixed) {
        // fixed: the stick stays at its place, the finger tilts it from there
        sStickOriginX = l.c[kStick].x;
        sStickOriginY = l.c[kStick].y;
        moveStick(l, px, py);
      } else {
        sStickOriginX = px;
        sStickOriginY = py;
        setStick(0.f, 0.f);
      }
    } else {
      sDown[c] = true;
      if (c == kFast) {
        portSpeedControl(1);
      } else if (c == kSwap) {
        gPortQuickSwapTouch = 1;
      }
#if defined(__APPLE__) && TARGET_OS_IPHONE
      if (portSettings()->touchHaptics) {
        portIosHapticTap();
      }
#elif defined(__ANDROID__)
      if (portSettings()->touchHaptics) {
        portAndroidHapticTap();
      }
#endif
    }
  } else if (Finger* f = findFinger(id); f != nullptr) {
    if (event->type == SDL_EVENT_FINGER_MOTION) {
      if (f->control == kStick) {
        moveStick(l, px, py);
      }
    } else {
      release(*f);
    }
  }
  apply();
}

extern "C" void portTouchDraw(void) {
  static unsigned sFrame;
  runTouchTest(++sFrame);
  const bool visible = !sGamepadInUse || sEditing;
  if (!visible && sVisible) {
    // A controller took over: let go of everything and leave port 1 to it.
    portTouchReleaseAll();
    clearStatus();
  }
  sVisible = visible;
  if (!sVisible || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  Layout l;
  if (!computeLayout(l)) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  for (int c = kA; c < kControlCount; c++) {
    if (!shown(c)) {
      continue;
    }
    // editing: the control being moved lit (X and Y with A)
    const bool lit = sEditing ? (sEditControl == c || (isBean(c) && sEditControl == kA))
                              : sDown[c] || (c == kFast && portSpeedActive());
    if (isBean(c)) {
      drawBean(dl, l.bean[c], l.c[c], kButtons[c].label, lit);
    } else {
      drawCircle(dl, l.c[c], kButtons[c].label, lit);
    }
    if (c == kMenu) { // three bars: the settings
      const Circle& m = l.c[c];
      for (int i = -1; i <= 1; i++) {
        const float y = m.y + i * m.r * 0.32f;
        dl->AddLine(ImVec2(m.x - m.r * 0.45f, y), ImVec2(m.x + m.r * 0.45f, y), white(200), m.r * 0.14f);
      }
    }
  }
  if (sEditing) {
    Circle done;
    Circle reset;
    editButtons(l, done, reset);
    drawCircle(dl, done, "Done", true);
    drawCircle(dl, reset, "Reset", false);
    const char* hint = "Drag the controls to move them";
    const float size = done.r * 0.5f;
    const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.f, hint);
    dl->AddText(ImGui::GetFont(), size, ImVec2(done.x + done.r * 1.3f - ext.x * 0.5f, done.y - done.r - ext.y - 6.f),
                IM_COL32(255, 255, 255, 230), hint);
    drawCircle(dl, l.c[kStick], nullptr, sEditControl == kStick);
    return;
  }
  Circle base = l.c[kStick];
  if (sStickActive) {
    base.x = sStickOriginX;
    base.y = sStickOriginY;
  }
  drawCircle(dl, base, nullptr, false);
  const Circle knob{base.x + sStickX01 * base.r, base.y + sStickY01 * base.r, base.r * 0.45f};
  drawCircle(dl, knob, nullptr, sStickActive);
}
