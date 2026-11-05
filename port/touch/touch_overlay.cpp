// On-screen touch controls (TTYD_TOUCH_OVERLAY; on by default on iOS).
//
// Touches become a PADStatus handed to Aurora as port 1's virtual status
// (PADSetVirtualStatus), which Aurora's PADRead merges with whatever else
// drives that port, so the game reads it like a controller (no change to
// Aurora or the game): online lockstep, replays and the second player's
// hand-over see it as they see a controller. Nothing here reads the game's
// clock or takes its random numbers (the drawing is Dear ImGui's, the fades
// and taps are timed with SDL's ticks). The controls are drawn over the frame
// with Aurora's Dear ImGui (foreground draw list, window coordinates in
// points).
//
// - Hidden (and the virtual status cleared) once a gamepad is used (a button,
//   or a stick or trigger past a dead zone); shown again when the screen is
//   touched or the last gamepad goes away. Connected is not enough: the iOS
//   Simulator (and some systems) report a gamepad that never sends input.
// - Left side: a floating analog stick (it centres where the finger lands),
//   and above it a D-pad (touch_dpad: while a menu is up, always, or never).
//   Right side: A (large), B, X, Y in the GameCube arrangement, and the
//   yellow C-stick below and left of B (above the buttons when held upright);
//   L and R (full presses) in the top corners, Z under R, START at the top,
//   fast-forward (>>, port/src/aurora/speed_aurora.c), the settings and a
//   camera (photo mode, touch_photo_button) beside it, and the partner
//   quick-swap / partner wheel button under Z in the field.
// - In the GameCube controller's colours (touch_colours) and shapes: a green
//   A, a red B, kidney-bean X and Y, a purple Z, pill-shaped shoulders,
//   octagonal gates for the sticks. A press shrinks the button a little,
//   lights it and sends a ring out; a light haptic tap where the device has
//   one (iOS UIImpactFeedbackGenerator, Android's vibrator).
// - A button acts on touch-down: the status is updated when the touch event
//   arrives, before the game's PADRead of that frame.
// - The C-stick is fixed: the finger's place on it is its tilt (full at its
//   gate's edge). A quick tap on it with no tilt centres the dragged camera.
// - A drag on the picture (or the right side) away from the controls turns
//   the free camera (free_camera, through the mouse camera's path:
//   port/include/port/view.h) or photo mode's; a quick tap there is a co-op
//   ping (port/include/port/ping.h), or a row of a menu that is up.
// - Photo mode (port/settings/features/photo_mode.cpp) takes the controls
//   while it is on: the stick and a drag turn, the C-stick goes in and out,
//   A takes the picture, B goes back, X, L, R and Y as on a controller.
// - Layout follows port/include/port/layout.h: in landscape the controls sit
//   over the safe area, in the margins beside the picture; held upright, in
//   the area below the picture. Each control can be moved and resized
//   (portTouchEditLayout, from the settings page), separately for each
//   orientation, saved in touch_layout.txt in the user folder.
//
// $TTYD_TOUCH_MOUSE=1 (desktop testing): the mouse also produces touches.
// $TTYD_TOUCH_TEST=<frame>:<x>,<y>:<frames>[><x2>,<y2>][;...] (tests): a touch
// at window fraction x,y on that drawn frame, held that many frames (moving
// in a straight line to x2,y2 meanwhile, if given), sent through SDL's event
// queue like a real one. $TTYD_TOUCH_EDIT=<frame>: the layout editor opens
// on that frame (screenshots of it).
#include "port/touch_overlay.h"
#include "port/gameplus.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/pad.h>
#include "port/settings.h"
#include "port/menu_tap.h"
#include "port/speed.h"
#include "port/layout.h"
#include "port/ping.h"
#include "port/tr.h"
#include "port/view.h"
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
extern "C" void portShotRequest(const char* name);        // port/src/aurora/shot_aurora.c (tests)
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// (the numbers are touch_layout.txt's: new controls go at the end)
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
  kFast,   // fast-forward: not a controller button
  kSwap,   // partner quick-swap and the partner wheel (port/include/port/gameplus.h)
  kMenu,   // the settings page (port/settings/settings_ui.cpp): not a controller button
  kCStick, // the C-stick
  kDpad,   // the D-pad
  kPhoto,  // photo mode on and off (port/settings/features/photo_mode.cpp)
  kControlCount,
};
// a finger on the picture away from the controls: a camera drag, or a tap
constexpr int kLook = kControlCount;

struct ButtonDef {
  const char* label;
  u16 padButton;
};

// Index by Control (the sticks and the D-pad have no one button).
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
    {nullptr, 0}, // kFast: two arrows, drawn
    {nullptr, 0}, // kSwap: two turning arrows, drawn
    {nullptr, 0}, // kMenu: three bars, drawn
    {nullptr, 0}, // kCStick
    {nullptr, 0}, // kDpad
    {nullptr, 0}, // kPhoto: a camera, drawn
};

constexpr u32 kPort = 0; // the game's controller 1
constexpr float kPi = 3.14159265f;

// Settings (Controls), declared below.
int* sDpadMode;    // touch_dpad: 0 while a menu is up, 1 always, 2 never
int* sColours;     // touch_colours: the GameCube controller's colours
int* sPhotoButton; // touch_photo_button: the camera button

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

// Where the user moved each control, in size units, and its size (times), by
// orientation (0 landscape, 1 portrait). X and Y go with A.
struct Offset {
  float dx, dy, scale;
};
Offset sOffset[2][kControlCount];
bool sOffsetsRead;
constexpr float kScaleMin = 0.6f, kScaleMax = 1.8f;

void resetOffsets(int o) {
  for (Offset& off : sOffset[o]) {
    off = {0.f, 0.f, 1.f};
  }
}

// Layout editing (portTouchEditLayout): dragging moves controls, pinching or
// - and + resizes the one chosen.
bool sEditing;
int sEditControl = -1; // being dragged
int sEditSel = -1;     // chosen (stays after the finger lifts)
SDL_FingerID sEditFinger;
float sEditX, sEditY;
struct {
  bool on;
  SDL_FingerID id;
  float x, y, d0, s0;
} sPinch;

constexpr int kMaxFingers = 10;
struct Finger {
  SDL_FingerID id;
  int control;   // a Control, or kLook
  bool used;
  bool consumed; // photo mode (or the camera button) took the press: not the game's
  u16 dpad;      // kDpad: the directions held
  float x0, y0, x, y; // kLook, kCStick: where it went down, where it is
  Uint64 at;     // when it went down (ms)
  bool moved;    // kLook: past a tap's reach (a drag); kCStick: tilted
};

Finger sFingers[kMaxFingers];
bool sStickActive;
float sStickOriginX, sStickOriginY, sStickX01, sStickY01; // origin and knob offset (-1..1)
float sCX01, sCY01;                                       // the C-stick's tilt (-1..1, y down)
bool sCRim;                                               // the C-stick at its gate's edge (a haptic notch)
bool sVisible = true;
bool sStatusSet;
bool sMouseInit;
Uint64 sPressAt[kControlCount]; // the press ring's start (ms)
float sFade[kControlCount];     // the controls that come and go, drawn this much (0-1)
Uint64 sMenuSeen;               // a menu was last up (ms): the D-pad stays a moment
Uint64 sLastDraw;

SDL_Window* window() { return aurora_get_window(); }

std::string layoutPath() { return std::string(portSettingsUserPath()) + "/touch_layout.txt"; }

void readOffsets() {
  sOffsetsRead = true;
  resetOffsets(0);
  resetOffsets(1);
  char* text = static_cast<char*>(SDL_LoadFile(layoutPath().c_str(), nullptr));
  if (text == nullptr) {
    return;
  }
  // one control a line: <orientation 0|1> <control> <dx> <dy> [<size>]
  for (char* line = text; line != nullptr && *line != '\0';) {
    int o = 0;
    int c = 0;
    float dx = 0.f;
    float dy = 0.f;
    float s = 1.f;
    const int n = std::sscanf(line, "%d %d %f %f %f", &o, &c, &dx, &dy, &s);
    if (n >= 4 && o >= 0 && o < 2 && c >= 0 && c < kControlCount) {
      sOffset[o][c] = {dx, dy, n >= 5 ? std::clamp(s, kScaleMin, kScaleMax) : 1.f};
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
      const Offset& off = sOffset[o][c];
      if (off.dx != 0.f || off.dy != 0.f || off.scale != 1.f) {
        SDL_IOprintf(io, "%d %d %.4f %.4f %.3f\n", o, c, off.dx, off.dy, off.scale);
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
    return Circle{x + off[c].dx * u, y + off[c].dy * u, r * off[c].scale};
  };
  const float right = sx + sw;
  const float bottom = sy + sh;
  const Circle a = at(kA, right - 0.22f * u, bottom - 0.22f * u, 0.085f * u);
  l.c[kA] = a;
  l.c[kB] = at(kB, right - 0.22f * u - 0.15f * u, bottom - 0.22f * u + 0.07f * u, 0.058f * u);
  // X: a vertical bean hugging A's right side; Y: a horizontal one over A's
  // top, as on the GameCube controller.
  constexpr float kDeg = kPi / 180.f;
  l.bean[kX] = {a.x, a.y, a.r * 1.78f, a.r * 0.40f, -62.f * kDeg, 30.f * kDeg};
  l.bean[kY] = {a.x, a.y, a.r * 1.78f, a.r * 0.40f, -168.f * kDeg, -100.f * kDeg};
  for (int b = kX; b <= kY; b++) {
    const Bean& n = l.bean[b];
    const float mid = 0.5f * (n.a0 + n.a1);
    l.c[b] = {n.cx + n.rm * std::cos(mid), n.cy + n.rm * std::sin(mid), n.t};
  }
  l.c[kStick] = at(kStick, sx + 0.22f * u, bottom - 0.24f * u, 0.13f * u);
  // The C-stick: where the GameCube's is, below and left of B, at the
  // thumb's reach from A; held upright there is room above the buttons
  // (clear of the stick's side).
  if (l.portrait) {
    l.c[kCStick] = at(kCStick, a.x - 0.27f * u, a.y - 0.30f * u, 0.066f * u);
  } else {
    l.c[kCStick] = at(kCStick, a.x - 0.35f * u, bottom - 0.10f * u, 0.066f * u);
  }
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
    l.c[kPhoto] = at(kPhoto, sx + sw * 0.5f - 0.21f * u, bottom - 0.07f * u, 0.04f * u);
    // the D-pad right of the stick (its L is above it), as on the GameCube
    l.c[kDpad] = at(kDpad, sx + 0.53f * u, bottom - 0.20f * u, 0.085f * u);
  } else {
    l.c[kR] = at(kR, right - 0.12f * u, sy + 0.10f * u, 0.05f * u);
    l.c[kZ] = at(kZ, right - 0.12f * u, sy + 0.24f * u, 0.045f * u);
    l.c[kL] = at(kL, sx + 0.12f * u, sy + 0.10f * u, 0.05f * u);
    l.c[kStart] = at(kStart, sx + sw * 0.5f, sy + 0.07f * u, 0.04f * u);
    l.c[kFast] = at(kFast, sx + sw * 0.5f - 0.13f * u, sy + 0.07f * u, 0.04f * u);
    l.c[kSwap] = at(kSwap, right - 0.12f * u, sy + 0.37f * u, 0.04f * u);
    l.c[kMenu] = at(kMenu, sx + sw * 0.5f + 0.13f * u, sy + 0.07f * u, 0.04f * u);
    l.c[kPhoto] = at(kPhoto, sx + sw * 0.5f - 0.26f * u, sy + 0.07f * u, 0.04f * u);
    // the D-pad above the stick's place, in the left margin (the picture
    // stays clear); moved on its own
    l.c[kDpad] = at(kDpad, sx + 0.24f * u, bottom - 0.58f * u, 0.085f * u);
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

bool photoOn() { return portPhotoOn() != 0; }

void haptic() {
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

bool held(int c) {
  for (const Finger& f : sFingers) {
    if (f.used && f.control == c) {
      return true;
    }
  }
  return false;
}

// The controls that come and go: shown now (and always while held, or while
// the layout is edited).
bool shown(int c) {
  if (sEditing || held(c)) {
    return true;
  }
  switch (c) {
  case kSwap: {
    // with quick-swap or the wheel on, in the field with partners to choose from
    if (!portGameplus("quick_swap") && !portGameplus("partner_wheel")) {
      return false;
    }
    int ids[7];
    int current = 0;
    return portPartnerWheelList(ids, 7, &current) > 1;
  }
  case kDpad: {
    const int mode = sDpadMode != nullptr ? *sDpadMode : 0;
    if (mode == 1) {
      return true;
    }
    if (mode == 2) {
      return false;
    }
    const Uint64 now = SDL_GetTicks();
    if (portMenuOpen()) {
      sMenuSeen = now;
    }
    return sMenuSeen != 0 && now - sMenuSeen < 700; // (a moment more: no flicker between menus)
  }
  case kPhoto:
    return (sPhotoButton == nullptr || *sPhotoButton) && (photoOn() || (portPhotoAvailable() && !portMenuOpen()));
  default:
    return true;
  }
}

// The controls as port 1's virtual status (merged into PADRead by Aurora).
// In photo mode the game is held: its controls are photo mode's, and the
// game gets nothing.
void apply() {
  PADStatus status;
  std::memset(&status, 0, sizeof(status));
  portPhotoTouchAxes(sStickX01, -sStickY01, -sCY01); // (read only while it is on)
  if (photoOn()) {
    PADSetVirtualStatus(kPort, &status);
    sStatusSet = true;
    return;
  }
  for (const Finger& f : sFingers) {
    if (!f.used || f.consumed) {
      continue;
    }
    if (f.control == kDpad) {
      status.button |= f.dpad;
    } else if (f.control >= kA && f.control < kControlCount) {
      status.button |= kButtons[f.control].padButton;
      if (f.control == kL) {
        status.triggerLeft = 255;
      } else if (f.control == kR) {
        status.triggerRight = 255;
      }
    }
  }
  status.stickX = static_cast<s8>(std::lround(sStickX01 * 127.f));
  status.stickY = static_cast<s8>(std::lround(-sStickY01 * 127.f)); // GameCube: up is positive
  status.substickX = static_cast<s8>(std::lround(sCX01 * 127.f));
  status.substickY = static_cast<s8>(std::lround(-sCY01 * 127.f));
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

// The C-stick: the finger's place from its middle, full at the gate's edge.
void moveCStick(const Layout& l, Finger& f, float px, float py) {
  const Circle& c = l.c[kCStick];
  float dx = (px - c.x) / c.r;
  float dy = (py - c.y) / c.r;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1.0f) {
    dx /= len;
    dy /= len;
  }
  sCX01 = dx;
  sCY01 = dy;
  if (len > 0.3f) {
    f.moved = true;
  }
  const bool rim = len >= 0.95f;
  if (rim && !sCRim) {
    haptic(); // a notch at the edge, as the gate's
  }
  sCRim = rim;
}

// The D-pad's directions under the finger: a cardinal one over 60 degrees
// round it, both of a corner over the 30 between; nothing in its middle.
u16 dpadAt(const Circle& c, float px, float py) {
  const float dx = (px - c.x) / c.r;
  const float dy = (py - c.y) / c.r;
  const float ax = std::fabs(dx);
  const float ay = std::fabs(dy);
  if (ax * ax + ay * ay < 0.18f * 0.18f) {
    return 0;
  }
  u16 b = 0;
  constexpr float kTan30 = 0.57735f;
  if (ay <= ax * (1.f / kTan30)) { // not within 30 degrees of vertical
    b |= dx < 0.f ? PAD_BUTTON_LEFT : PAD_BUTTON_RIGHT;
  }
  if (ax <= ay * (1.f / kTan30)) { // not within 30 degrees of horizontal
    b |= dy < 0.f ? PAD_BUTTON_UP : PAD_BUTTON_DOWN;
  }
  return b;
}

bool isBean(int c) {
  return c == kX || c == kY;
}

bool isPill(int c) {
  return c == kL || c == kR || c == kZ;
}

// a pill's half width and half height for its circle
void pillSize(int c, const Circle& b, float* hw, float* hh) {
  *hw = b.r * (c == kZ ? 1.30f : 1.45f);
  *hh = b.r * (c == kZ ? 0.72f : 0.78f);
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

// A control's distance from (px, py) in its own size: <= 1 is on it.
float controlDistance(const Layout& l, int c, float px, float py) {
  const Circle& b = l.c[c];
  if (isBean(c)) {
    return beanDistance(l.bean[c], px, py);
  }
  if (isPill(c)) {
    float hw = 0.f;
    float hh = 0.f;
    pillSize(c, b, &hw, &hh);
    const float ex = std::fmax(std::fabs(px - b.x) - (hw - hh), 0.f);
    return std::hypot(ex, py - b.y) / hh;
  }
  return std::hypot(px - b.x, py - b.y) / b.r;
}

bool onPicture(const Layout& l, float px, float py) {
  return px >= l.area.picX && px < l.area.picX + l.area.picW && py >= l.area.picY && py < l.area.picY + l.area.picH;
}

Control hitTest(const Layout& l, float px, float py) {
  Control best = kNone;
  float bestDist = 0.f;
  for (int c = kA; c < kControlCount; c++) {
    if (!shown(c)) {
      continue;
    }
    // in the controls' own sizes, so a bean and a round button compare fairly;
    // the C-stick's zone a little wider (a thumb lands near it, not on it)
    const float d = controlDistance(l, c, px, py);
    const float reach = c == kCStick ? 1.5f : c == kDpad ? 1.25f : 1.35f;
    if (d <= reach && (best == kNone || d < bestDist)) {
      best = static_cast<Control>(c);
      bestDist = d;
    }
  }
  if (best == kNone && px < l.stickZoneRight && py > l.stickZoneTop) {
    return kStick;
  }
  const bool pic = onPicture(l, px, py);
  // with a selection list up, a tap on the picture is a tap on its rows
  if (best == kNone && pic && portMenuOpen()) {
    portMenuTap((px - l.area.picX) / l.area.picW, (py - l.area.picY) / l.area.picH);
    return kNone;
  }
  // a tap on the picture presses A (settings)
  if (best == kNone && portSettings()->tapA && pic && !photoOn()) {
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
  } else if (f.control == kCStick) {
    sCX01 = sCY01 = 0.f;
    sCRim = false;
  } else if (f.control == kFast && !f.consumed) {
    portSpeedControl(0);
  } else if (f.control == kSwap) {
    gPortQuickSwapTouch = 0;
  } else if (f.control == kMenu) {
    portSettingsToggle(); // on the tap's release, so the page doesn't take this finger
  }
  f.used = false;
}

// --- Drawing ---

struct Tint {
  int r, g, b;
};
constexpr Tint kWhite = {255, 255, 255};

// The GameCube controller's colours (touch_colours), white without.
Tint tint(int c) {
  if (sColours != nullptr && *sColours == 0) {
    return kWhite;
  }
  switch (c) {
  case kA:
    return {40, 200, 150}; // its green-teal
  case kB:
    return {236, 66, 66}; // red
  case kX:
  case kY:
    return {226, 226, 234}; // light grey
  case kZ:
    return {132, 104, 240}; // indigo
  case kL:
  case kR:
  case kStart:
  case kDpad:
  case kStick:
    return {206, 208, 220}; // the body's grey
  case kCStick:
    return {255, 206, 30}; // yellow
  default:
    return kWhite; // the port's own: fast-forward, swap, settings, camera
  }
}

float sAlpha = 1.f; // the control being drawn: its fade

ImU32 rgba(Tint t, int a) {
  const int alpha = static_cast<int>(static_cast<float>(a * portSettings()->touchOpacity / 100) * sAlpha);
  return IM_COL32(t.r, t.g, t.b, std::clamp(alpha, 0, 255));
}

// White at alpha a, scaled by the settings' opacity.
ImU32 white(int a) {
  return rgba(kWhite, a);
}

void label(ImDrawList* dl, float x, float y, float size, const char* text, ImU32 col) {
  const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.f, text);
  const ImVec2 at(x - ext.x * 0.5f, y - ext.y * 0.5f);
  dl->AddText(ImGui::GetFont(), size, ImVec2(at.x + 1.f, at.y + 1.f), rgba({0, 0, 0}, 110), text);
  dl->AddText(ImGui::GetFont(), size, at, col, text);
}

// The press ring: out from the button, fading, for a moment after a press.
void pressRing(ImDrawList* dl, int c, float x, float y, float r, Tint t) {
  if (c < 0 || c >= kControlCount || sPressAt[c] == 0) {
    return;
  }
  const float age = static_cast<float>(SDL_GetTicks() - sPressAt[c]) / 260.f;
  if (age >= 1.f) {
    return;
  }
  dl->AddCircle(ImVec2(x, y), r * (1.f + 0.45f * age), rgba(t, static_cast<int>(200.f * (1.f - age))), 40,
                2.5f * (1.f - age) + 1.f);
}

void drawCircle(ImDrawList* dl, int c, const Circle& b, const char* text, bool down, Tint t = kWhite) {
  const float r = down ? b.r * 0.93f : b.r; // pressed in a little
  dl->AddCircleFilled(ImVec2(b.x, b.y), r, rgba(t, down ? 150 : 58), 40);
  dl->AddCircle(ImVec2(b.x, b.y), r, rgba(t, down ? 240 : 170), 40, 2.0f);
  pressRing(dl, c, b.x, b.y, b.r, t);
  if (text != nullptr) {
    const float size = std::strlen(text) > 1 ? r * 0.55f : r * 0.95f;
    label(dl, b.x, b.y, size, text, white(225));
  }
}

void drawPill(ImDrawList* dl, int c, const Circle& b, const char* text, bool down, Tint t) {
  float hw = 0.f;
  float hh = 0.f;
  pillSize(c, b, &hw, &hh);
  if (down) {
    hw *= 0.94f;
    hh *= 0.93f;
  }
  const ImVec2 p0(b.x - hw, b.y - hh);
  const ImVec2 p1(b.x + hw, b.y + hh);
  dl->AddRectFilled(p0, p1, rgba(t, down ? 150 : 58), hh);
  dl->AddRect(p0, p1, rgba(t, down ? 240 : 170), hh, 0, 2.0f);
  if (c >= 0 && sPressAt[c] != 0) {
    const float age = static_cast<float>(SDL_GetTicks() - sPressAt[c]) / 260.f;
    if (age < 1.f) {
      const float g = 1.f + 0.35f * age;
      dl->AddRect(ImVec2(b.x - hw * g, b.y - hh * g), ImVec2(b.x + hw * g, b.y + hh * g),
                  rgba(t, static_cast<int>(200.f * (1.f - age))), hh * g, 0, 2.5f * (1.f - age) + 1.f);
    }
  }
  label(dl, b.x, b.y, hh * 1.25f, text, white(225));
}

void drawBean(ImDrawList* dl, int c, const Bean& n0, const Circle& labelAt, const char* text, bool down, Tint t) {
  Bean n = n0;
  if (down) {
    n.t *= 0.88f; // pressed in a little
  }
  const ImU32 fill = rgba(t, down ? 150 : 58);
  const ImU32 edge = rgba(t, down ? 240 : 170);
  constexpr int kSegs = 24;
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
  if (c >= 0 && sPressAt[c] != 0 && SDL_GetTicks() - sPressAt[c] < 260) {
    const float age = static_cast<float>(SDL_GetTicks() - sPressAt[c]) / 260.f;
    const float g = n0.t * (1.f + 0.9f * age);
    dl->PathArcTo(ImVec2(n.cx, n.cy), n.rm + g, n.a0, n.a1, kSegs);
    dl->PathStroke(rgba(t, static_cast<int>(200.f * (1.f - age))), 0, 2.5f * (1.f - age) + 1.f);
  }
  if (text != nullptr) {
    label(dl, labelAt.x, labelAt.y, labelAt.r * 1.1f, text, white(225));
  }
}

// a regular octagon (corners at the eight directions, as a GameCube stick's gate)
void octagon(ImDrawList* dl, float cx, float cy, float r, ImU32 fill, ImU32 edge, float thick) {
  ImVec2 p[8];
  for (int i = 0; i < 8; i++) {
    const float a = kPi * 0.25f * static_cast<float>(i);
    p[i] = ImVec2(cx + r * std::cos(a), cy + r * std::sin(a));
  }
  dl->AddConvexPolyFilled(p, 8, fill);
  dl->AddPolyline(p, 8, edge, ImDrawFlags_Closed, thick);
}

bool gcStyle() { return sColours == nullptr || *sColours != 0; }

// The main stick: its gate (octagonal, GameCube style) and its knob.
void drawStick(ImDrawList* dl, const Circle& base, float kx, float ky, bool active) {
  const Tint t = tint(kStick);
  if (gcStyle()) {
    octagon(dl, base.x, base.y, base.r * 1.04f, rgba({0, 0, 0}, 40), rgba(t, 150), 2.f);
    dl->AddCircle(ImVec2(base.x, base.y), base.r * 0.62f, rgba(t, 50), 40, 1.5f); // the gate's well
  } else {
    drawCircle(dl, -1, base, nullptr, false);
  }
  const ImVec2 k(base.x + kx * base.r, base.y + ky * base.r);
  const float kr = base.r * 0.45f;
  dl->AddCircleFilled(ImVec2(k.x + 1.5f, k.y + 2.5f), kr, rgba({0, 0, 0}, 60), 40); // its shadow
  dl->AddCircleFilled(k, kr, rgba(t, active ? 170 : 95), 40);
  dl->AddCircle(k, kr, rgba(t, active ? 245 : 190), 40, 2.f);
  if (gcStyle()) {
    dl->AddCircle(k, kr * 0.62f, rgba(t, active ? 200 : 120), 32, 1.5f); // the ridged top
  }
}

void drawCStick(ImDrawList* dl, const Circle& b, float tx, float ty, bool active) {
  const Tint t = tint(kCStick);
  const Tint deep = gcStyle() ? Tint{150, 104, 0} : Tint{90, 90, 90};
  octagon(dl, b.x, b.y, b.r, rgba({60, 44, 0}, 70), rgba(t, active ? 245 : 200), 2.5f);
  const ImVec2 k(b.x + tx * b.r * 0.55f, b.y + ty * b.r * 0.55f);
  const float kr = b.r * (active ? 0.58f : 0.62f);
  dl->AddCircleFilled(ImVec2(k.x + 1.f, k.y + 2.f), kr, rgba({0, 0, 0}, 60), 32);
  dl->AddCircleFilled(k, kr, rgba(t, active ? 240 : 200), 32);
  dl->AddCircle(k, kr, rgba(deep, 230), 32, 2.f);
  pressRing(dl, kCStick, b.x, b.y, b.r, t);
  label(dl, k.x, k.y, kr * 1.25f, "C", rgba(gcStyle() ? Tint{92, 58, 0} : Tint{40, 40, 40}, 235));
}

void drawDpad(ImDrawList* dl, const Circle& b, u16 lit, bool editLit) {
  const Tint t = tint(kDpad);
  const float r = b.r;
  const float w = r * 0.34f; // an arm's half width
  const float k = w * 0.45f; // its ends' rounding
  const float x = b.x;
  const float y = b.y;
  const ImU32 fill = rgba(t, editLit ? 150 : 62);
  const ImU32 on = rgba(t, 150);
  // the cross in three pieces (it is concave): across, then above and below
  dl->AddRectFilled(ImVec2(x - r, y - w), ImVec2(x + r, y + w), fill, k);
  dl->AddRectFilled(ImVec2(x - w, y - r), ImVec2(x + w, y - w), fill, k, ImDrawFlags_RoundCornersTop);
  dl->AddRectFilled(ImVec2(x - w, y + w), ImVec2(x + w, y + r), fill, k, ImDrawFlags_RoundCornersBottom);
  // the arms held, lit
  if (lit & PAD_BUTTON_UP) {
    dl->AddRectFilled(ImVec2(x - w, y - r), ImVec2(x + w, y - w * 0.2f), on, k, ImDrawFlags_RoundCornersTop);
  }
  if (lit & PAD_BUTTON_DOWN) {
    dl->AddRectFilled(ImVec2(x - w, y + w * 0.2f), ImVec2(x + w, y + r), on, k, ImDrawFlags_RoundCornersBottom);
  }
  if (lit & PAD_BUTTON_LEFT) {
    dl->AddRectFilled(ImVec2(x - r, y - w), ImVec2(x - w * 0.2f, y + w), on, k, ImDrawFlags_RoundCornersLeft);
  }
  if (lit & PAD_BUTTON_RIGHT) {
    dl->AddRectFilled(ImVec2(x + w * 0.2f, y - w), ImVec2(x + r, y + w), on, k, ImDrawFlags_RoundCornersRight);
  }
  // the outline, its outer corners rounded as the fill's
  const float h = kPi * 0.5f;
  dl->PathArcTo(ImVec2(x - w + k, y - r + k), k, 2.f * h, 3.f * h, 4);
  dl->PathArcTo(ImVec2(x + w - k, y - r + k), k, 3.f * h, 4.f * h, 4);
  dl->PathLineTo(ImVec2(x + w, y - w));
  dl->PathArcTo(ImVec2(x + r - k, y - w + k), k, -h, 0.f, 4);
  dl->PathArcTo(ImVec2(x + r - k, y + w - k), k, 0.f, h, 4);
  dl->PathLineTo(ImVec2(x + w, y + w));
  dl->PathArcTo(ImVec2(x + w - k, y + r - k), k, 0.f, h, 4);
  dl->PathArcTo(ImVec2(x - w + k, y + r - k), k, h, 2.f * h, 4);
  dl->PathLineTo(ImVec2(x - w, y + w));
  dl->PathArcTo(ImVec2(x - r + k, y + w - k), k, h, 2.f * h, 4);
  dl->PathArcTo(ImVec2(x - r + k, y - w + k), k, 2.f * h, 3.f * h, 4);
  dl->PathLineTo(ImVec2(x - w, y - w));
  dl->PathStroke(rgba(t, lit != 0 || editLit ? 240 : 170), ImDrawFlags_Closed, 2.0f);
  // an arrow on each arm, pointing out
  const float ar = r * 0.66f;
  const float as = w * 0.55f;
  const struct {
    float dx, dy;
    u16 bit;
  } kArms[4] = {{0.f, -1.f, PAD_BUTTON_UP}, {0.f, 1.f, PAD_BUTTON_DOWN}, {-1.f, 0.f, PAD_BUTTON_LEFT},
                {1.f, 0.f, PAD_BUTTON_RIGHT}};
  for (const auto& arm : kArms) {
    const ImVec2 tip(x + arm.dx * (ar + as * 0.6f), y + arm.dy * (ar + as * 0.6f));
    const ImVec2 b0(x + arm.dx * (ar - as * 0.6f) - arm.dy * as, y + arm.dy * (ar - as * 0.6f) + arm.dx * as);
    const ImVec2 b1(x + arm.dx * (ar - as * 0.6f) + arm.dy * as, y + arm.dy * (ar - as * 0.6f) - arm.dx * as);
    dl->AddTriangleFilled(tip, b0, b1, white((lit & arm.bit) ? 240 : 150));
  }
  dl->AddCircle(ImVec2(x, y), w * 0.55f, rgba(t, 110), 20, 1.5f); // the middle's dimple
}

// The port's own buttons' pictures: fast-forward's arrows, the swap's turning
// arrows, the settings' bars, the camera.
void drawIcon(ImDrawList* dl, int c, const Circle& m) {
  const ImU32 ink = white(220);
  const float r = m.r;
  if (c == kFast) {
    for (int i = 0; i < 2; i++) {
      const float cx = m.x + (i == 0 ? -0.18f : 0.16f) * r;
      dl->AddTriangleFilled(ImVec2(cx - 0.17f * r, m.y - 0.28f * r), ImVec2(cx + 0.19f * r, m.y),
                            ImVec2(cx - 0.17f * r, m.y + 0.28f * r), ink);
    }
  } else if (c == kSwap) {
    const float rr = r * 0.48f;
    const float th = r * 0.12f;
    for (int i = 0; i < 2; i++) {
      const float a0 = i == 0 ? -kPi * 0.92f : kPi * 0.08f;
      const float a1 = a0 + kPi * 0.70f;
      dl->PathArcTo(ImVec2(m.x, m.y), rr, a0, a1, 12);
      dl->PathStroke(ink, 0, th);
      // the arrowhead at the arc's end, along its turn (clockwise)
      const ImVec2 p(m.x + rr * std::cos(a1), m.y + rr * std::sin(a1));
      const ImVec2 tan(-std::sin(a1), std::cos(a1));
      const ImVec2 nor(std::cos(a1), std::sin(a1));
      const float s = r * 0.20f;
      dl->AddTriangleFilled(ImVec2(p.x + tan.x * s, p.y + tan.y * s),
                            ImVec2(p.x + nor.x * s * 0.9f - tan.x * s * 0.3f, p.y + nor.y * s * 0.9f - tan.y * s * 0.3f),
                            ImVec2(p.x - nor.x * s * 0.9f - tan.x * s * 0.3f, p.y - nor.y * s * 0.9f - tan.y * s * 0.3f),
                            ink);
    }
  } else if (c == kMenu) {
    for (int i = -1; i <= 1; i++) {
      const float y = m.y + i * r * 0.32f;
      dl->AddLine(ImVec2(m.x - r * 0.45f, y), ImVec2(m.x + r * 0.45f, y), ink, r * 0.14f);
    }
  } else if (c == kPhoto) {
    const ImVec2 b0(m.x - 0.50f * r, m.y - 0.26f * r);
    const ImVec2 b1(m.x + 0.50f * r, m.y + 0.36f * r);
    dl->AddRectFilled(ImVec2(m.x - 0.20f * r, m.y - 0.40f * r), ImVec2(m.x + 0.14f * r, m.y - 0.20f * r), ink,
                      0.05f * r);
    dl->AddRect(b0, b1, ink, 0.10f * r, 0, r * 0.10f);
    dl->AddCircle(ImVec2(m.x, m.y + 0.05f * r), 0.19f * r, ink, 20, r * 0.10f);
  }
}

// --- Layout editing ---

// Done and Reset in the middle of the picture (no control is there, held
// either way); - and + under them for the control chosen.
struct EditButtons {
  Circle done, reset, minus, plus;
};

EditButtons editButtons(const Layout& l) {
  const bool pic = l.area.picW > 0.f && l.area.picH > 0.f;
  const float cx = pic ? l.area.picX + l.area.picW * 0.5f : l.area.ctrlX + l.area.ctrlW * 0.5f;
  const float cy = pic ? l.area.picY + l.area.picH * 0.5f : l.area.ctrlY + l.area.ctrlH * 0.5f;
  const float r = std::fmax(0.07f * l.u, 28.f);
  EditButtons e;
  e.done = {cx - r * 1.3f, cy, r};
  e.reset = {cx + r * 1.3f, cy, r};
  e.minus = {cx - r * 1.9f, cy + r * 2.5f, r * 0.7f};
  e.plus = {cx + r * 1.9f, cy + r * 2.5f, r * 0.7f};
  return e;
}

void finishEditing() {
  sEditing = false;
  sEditControl = -1;
  sEditSel = -1;
  sPinch.on = false;
  writeOffsets();
  PADBlockInput(false);
}

void setScale(const Layout& l, int c, float s) {
  sOffset[l.portrait][c].scale = std::clamp(s, kScaleMin, kScaleMax);
}

void pinchTo(const Layout& l) {
  if (!sPinch.on || sEditControl < 0 || sPinch.d0 < 8.f) {
    return;
  }
  const float d = std::hypot(sPinch.x - sEditX, sPinch.y - sEditY);
  setScale(l, sEditControl, sPinch.s0 * d / sPinch.d0);
}

void editEvent(const Layout& l, Uint32 type, SDL_FingerID id, float px, float py) {
  if (type == SDL_EVENT_FINGER_DOWN) {
    const EditButtons e = editButtons(l);
    if (std::hypot(px - e.done.x, py - e.done.y) <= e.done.r) {
      finishEditing();
      return;
    }
    if (std::hypot(px - e.reset.x, py - e.reset.y) <= e.reset.r) {
      resetOffsets(l.portrait);
      return;
    }
    if (sEditSel >= 0 && sEditControl < 0) {
      const float s = sOffset[l.portrait][sEditSel].scale;
      if (std::hypot(px - e.minus.x, py - e.minus.y) <= e.minus.r * 1.2f) {
        setScale(l, sEditSel, std::round((s - 0.1f) * 10.f) / 10.f);
        haptic();
        return;
      }
      if (std::hypot(px - e.plus.x, py - e.plus.y) <= e.plus.r * 1.2f) {
        setScale(l, sEditSel, std::round((s + 0.1f) * 10.f) / 10.f);
        haptic();
        return;
      }
    }
    // a second finger while one drags a control: a pinch, its size
    if (sEditControl >= 0 && id != sEditFinger) {
      sPinch = {true, id, px, py, std::hypot(px - sEditX, py - sEditY), sOffset[l.portrait][sEditControl].scale};
      return;
    }
    int best = -1;
    float bestDist = 0.f;
    for (int c = kStick; c < kControlCount; c++) {
      const float d = controlDistance(l, c, px, py);
      if (d <= 1.5f && (best < 0 || d < bestDist)) {
        best = isBean(c) ? kA : c; // X and Y go with A
        bestDist = d;
      }
    }
    sEditSel = best;
    if (best >= 0) {
      sEditControl = best;
      sEditFinger = id;
      sEditX = px;
      sEditY = py;
    }
  } else if (type == SDL_EVENT_FINGER_MOTION) {
    if (sEditControl >= 0 && id == sEditFinger) {
      Offset& o = sOffset[l.portrait][sEditControl];
      if (!sPinch.on) { // (pinching: the size changes, the place stays)
        o.dx += (px - sEditX) / l.u;
        o.dy += (py - sEditY) / l.u;
      }
      sEditX = px;
      sEditY = py;
      pinchTo(l);
    } else if (sPinch.on && id == sPinch.id) {
      sPinch.x = px;
      sPinch.y = py;
      pinchTo(l);
    }
  } else if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) {
    if (id == sEditFinger) {
      sEditControl = -1;
      sPinch.on = false;
    } else if (sPinch.on && id == sPinch.id) {
      sPinch.on = false;
    }
  }
}

void drawEditing(ImDrawList* dl, const Layout& l) {
  const EditButtons e = editButtons(l);
  drawCircle(dl, -1, e.done, portTr("Done"), true);
  drawCircle(dl, -1, e.reset, portTr("Reset"), false);
  const char* hint = portTr("Drag a control to move it. To resize it, pinch it, or tap it and use - and +");
  const float size = e.done.r * 0.45f;
  const float wrap = std::fmax(l.area.ctrlW * 0.8f, 200.f);
  const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, wrap, hint);
  const float cx = 0.5f * (e.done.x + e.reset.x);
  const ImVec2 at(cx - ext.x * 0.5f, e.done.y - e.done.r - ext.y - 8.f);
  dl->AddRectFilled(ImVec2(at.x - 10.f, at.y - 6.f), ImVec2(at.x + ext.x + 10.f, at.y + ext.y + 6.f),
                    IM_COL32(0, 0, 0, 150), 8.f);
  dl->AddText(ImGui::GetFont(), size, at, IM_COL32(255, 255, 255, 235), hint, nullptr, wrap);
  if (sEditSel >= 0) {
    drawCircle(dl, -1, e.minus, "-", false);
    drawCircle(dl, -1, e.plus, "+", false);
    char text[64];
    std::snprintf(text, sizeof(text), "%s %d%%", portTr("Size"),
                  static_cast<int>(std::lround(sOffset[l.portrait][sEditSel].scale * 100.f)));
    const float ts = e.minus.r * 0.75f;
    const ImVec2 te = ImGui::GetFont()->CalcTextSizeA(ts, 1e9f, 0.f, text);
    dl->AddRectFilled(ImVec2(cx - te.x * 0.5f - 8.f, e.minus.y - te.y * 0.5f - 4.f),
                      ImVec2(cx + te.x * 0.5f + 8.f, e.minus.y + te.y * 0.5f + 4.f), IM_COL32(0, 0, 0, 150), 6.f);
    dl->AddText(ImGui::GetFont(), ts, ImVec2(cx - te.x * 0.5f, e.minus.y - te.y * 0.5f), IM_COL32(255, 255, 255, 240),
                text);
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
  static const char* edit = std::getenv("TTYD_TOUCH_EDIT");
  if (edit != nullptr && frame == static_cast<unsigned>(std::atoi(edit))) {
    std::fprintf(stderr, "[touch] test: layout editor (frame %u)\n", frame);
    portTouchEditLayout();
  }
  if (script == nullptr) {
    return;
  }
  SDL_FingerID id = 1000;
  for (const char* p = script; *p != '\0'; id++) {
    unsigned start = 0;
    unsigned hold = 0;
    float x = 0.f;
    float y = 0.f;
    int used = 0;
    if (std::sscanf(p, "%u:%f,%f:%u%n", &start, &x, &y, &hold, &used) != 4) {
      return;
    }
    float x2 = x;
    float y2 = y;
    if (p[used] == '>') {
      std::sscanf(p + used + 1, "%f,%f", &x2, &y2);
    }
    if (frame == start) {
      std::fprintf(stderr, "[touch] test: down at %.3f,%.3f (frame %u)\n", x, y, frame);
      pushFinger(SDL_EVENT_FINGER_DOWN, id, x, y);
    } else if (frame > start && frame < start + hold && (x2 != x || y2 != y)) {
      const float t = static_cast<float>(frame - start) / static_cast<float>(hold);
      pushFinger(SDL_EVENT_FINGER_MOTION, id, x + (x2 - x) * t, y + (y2 - y) * t);
    } else if (frame == start + hold) {
      std::fprintf(stderr, "[touch] test: up (frame %u)\n", frame);
      pushFinger(SDL_EVENT_FINGER_UP, id, x2, y2);
    }
    const char* next = std::strchr(p, ';');
    if (next == nullptr) {
      break;
    }
    p = next + 1;
  }
}

// $TTYD_TOUCH_SHOT=<frame>[,<frame>...] (tests; window captures can't see
// Dear ImGui's drawing): on those drawn frames, the game's picture
// (touch_<frame>.png, portShotRequest) and the foreground draw list as drawn
// so far (touch_<frame>.txt: the window and the picture's place, then each
// vertex and the triangles; touch_atlas.bin the font atlas's alpha), in
// $TTYD_SHOT_DIR; tools/port/touch_shot.py puts them together.
void shotFrame(unsigned frame, ImDrawList* dl) {
  static const char* list = std::getenv("TTYD_TOUCH_SHOT");
  static const char* dir = std::getenv("TTYD_SHOT_DIR");
  if (list == nullptr || dir == nullptr) {
    return;
  }
  bool now = false;
  for (const char* p = list; p != nullptr && *p != '\0';) {
    now = now || static_cast<unsigned>(std::atoi(p)) == frame;
    p = std::strchr(p, ',');
    if (p != nullptr) {
      p++;
    }
  }
  if (!now) {
    return;
  }
  char name[64];
  std::snprintf(name, sizeof(name), "touch_%u", frame);
  portShotRequest(name);
  PortLayout pl{};
  portLayoutGet(&pl);
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(window(), &ww, &wh);
  const std::string path = std::string(dir) + "/" + name + ".txt";
  if (FILE* f = std::fopen(path.c_str(), "w"); f != nullptr) {
    std::fprintf(f, "%d %d %.1f %.1f %.1f %.1f\n%d\n", ww, wh, pl.picX, pl.picY, pl.picW, pl.picH, dl->VtxBuffer.Size);
    for (const ImDrawVert& v : dl->VtxBuffer) {
      std::fprintf(f, "%.2f %.2f %.5f %.5f %u\n", v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col);
    }
    unsigned count = 0;
    for (const ImDrawCmd& cmd : dl->CmdBuffer) {
      count += cmd.ElemCount;
    }
    std::fprintf(f, "%u\n", count);
    for (const ImDrawCmd& cmd : dl->CmdBuffer) {
      for (unsigned i = 0; i < cmd.ElemCount; i++) {
        std::fprintf(f, "%u\n", cmd.VtxOffset + dl->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)]);
      }
    }
    std::fclose(f);
  }
  static bool sAtlas;
  // (the atlas as kept, if it is: never built again here)
  const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  const int aw = atlas->TexWidth;
  const int ah = atlas->TexHeight;
  if (!sAtlas && aw > 0 && ah > 0 && (atlas->TexPixelsAlpha8 != nullptr || atlas->TexPixelsRGBA32 != nullptr)) {
    sAtlas = true;
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    if (FILE* f = std::fopen((std::string(dir) + "/touch_atlas.bin").c_str(), "wb"); f != nullptr) {
      std::fprintf(f, "%d %d %.6f %.6f\n", aw, ah, white.x, white.y);
      for (int i = 0; i < aw * ah; i++) {
        const unsigned char a = atlas->TexPixelsAlpha8 != nullptr ? atlas->TexPixelsAlpha8[i]
                                                                  : static_cast<unsigned char>(atlas->TexPixelsRGBA32[i] >> 24);
        std::fputc(a, f);
      }
      std::fclose(f);
    }
  }
  std::fprintf(stderr, "[touch] test: shot %s\n", name);
}

// a drag across the picture: the free camera turned as the mouse turns it
// (the mouse camera's pixels: its whole reach in about a third of the picture)
constexpr float kDragReach = 0.35f;

void lookMove(const Layout& l, Finger& f, float px, float py) {
  if (!f.moved && std::hypot(px - f.x0, py - f.y0) > 16.f) {
    f.moved = true; // a drag, not a tap
  }
  const float dx = px - f.x;
  const float dy = py - f.y;
  f.x = px;
  f.y = py;
  if (!f.moved || l.area.picW <= 0.f || l.area.picH <= 0.f) {
    return;
  }
  if (photoOn()) {
    portPhotoTouchDrag(dx / l.area.picW, dy / l.area.picH);
  } else if (portViewMouseLook()) {
    const float k = 400.f / (kDragReach * l.area.picW);
    portViewMouse(dx * k, dy * k);
  }
}

const char* const kDpadModes[3] = {"While a menu is up", "Always", "Off"};

const bool sAdded = [] {
  sDpadMode = portSettingsDeclare("touch_dpad", 0, 0, 2);
  sColours = portSettingsDeclare("touch_colours", 1, 0, 1);
  sPhotoButton = portSettingsDeclare("touch_photo_button", 1, 0, 1);
  portSettingsAddChoice("Controls", "On-screen D-pad",
                        "The cross above the stick, for menus and the shortcuts on the D-pad. While a menu is up, "
                        "it shows only then.",
                        0, "touch_dpad", kDpadModes, 3, "touch on-screen dpad d-pad cross arrows");
  portSettingsAddToggle("Controls", "GameCube colours",
                        "The on-screen controls in the GameCube controller's colours: a green A, a red B, a purple "
                        "Z, a yellow C-stick. Off: all white.",
                        0, "touch_colours", "touch on-screen colors colours style");
  portSettingsAddToggle("Controls", "Photo mode button",
                        "A camera at the top of the screen in the field: photo mode on and off.", 0,
                        "touch_photo_button", "touch on-screen photo camera picture");
  return true;
}();

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
  sEditSel = -1;
  sPinch.on = false;
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
    const bool page = portSettingsOwnsPoint(px, py) != 0;
    const Control c = page ? kNone : hitTest(l, px, py);
    if (c == kNone) {
      // away from the controls, on the picture or the right side: a camera
      // drag, or a tap (a ping)
      if (!page && (onPicture(l, px, py) || px >= l.stickZoneRight)) {
        *f = {};
        f->id = id;
        f->control = kLook;
        f->used = true;
        f->x0 = f->x = px;
        f->y0 = f->y = py;
        f->at = SDL_GetTicks();
      }
      return;
    }
    if (c == kStick && sStickActive) {
      return;
    }
    *f = {};
    f->id = id;
    f->control = c;
    f->used = true;
    f->x0 = f->x = px;
    f->y0 = f->y = py;
    f->at = SDL_GetTicks();
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
    } else if (c == kCStick) {
      sCRim = true; // (no notch for landing on the edge: the press's tap is enough)
      moveCStick(l, *f, px, py);
      sPressAt[c] = SDL_GetTicks();
      haptic();
    } else {
      sPressAt[c] = SDL_GetTicks();
      if (c == kDpad) {
        f->dpad = dpadAt(l.c[kDpad], px, py);
      }
      if (c == kPhoto) {
        f->consumed = true; // not a controller button
        portPhotoToggle();
      } else if (photoOn() && c != kMenu) {
        // photo mode's: A a picture, B back, X, Y, L, R (START: back too)
        f->consumed = true;
        if (c == kStart) {
          portPhotoToggle();
        } else if (c >= kA && c < kControlCount) {
          portPhotoTouchButton(kButtons[c].padButton);
        }
      } else if (c == kFast) {
        portSpeedControl(1);
      } else if (c == kSwap) {
        gPortQuickSwapTouch = 1;
      }
      haptic();
    }
  } else if (Finger* f = findFinger(id); f != nullptr) {
    if (event->type == SDL_EVENT_FINGER_MOTION) {
      if (f->control == kStick) {
        moveStick(l, px, py);
      } else if (f->control == kCStick) {
        moveCStick(l, *f, px, py);
      } else if (f->control == kDpad) {
        const u16 d = dpadAt(l.c[kDpad], px, py);
        if (d != f->dpad && d != 0) {
          haptic(); // a new direction
        }
        f->dpad = d;
      } else if (f->control == kSwap) {
        portPartnerWheelPointer(px, py); // held: the partner wheel's choice (partner_wheel.cpp)
      } else if (f->control == kLook) {
        lookMove(l, *f, px, py);
      }
    } else {
      const Uint64 now = SDL_GetTicks();
      if (f->control == kLook && !f->moved && now - f->at < 350 && event->type == SDL_EVENT_FINGER_UP &&
          !portMenuOpen() && !photoOn()) {
        // a quick tap on the picture away from the controls: a co-op ping there
        if (l.area.picW > 0.f && l.area.picH > 0.f) {
          const float nx = (f->x0 - l.area.picX) / l.area.picW;
          const float ny = (f->y0 - l.area.picY) / l.area.picH;
          if (nx >= 0.f && nx < 1.f && ny >= 0.f && ny < 1.f) {
            portPingClick(nx, ny);
          }
        }
      } else if (f->control == kCStick && !f->moved && now - f->at < 300 && event->type == SDL_EVENT_FINGER_UP) {
        portViewMouseCentre(); // a tap on the C-stick: the dragged camera back to the middle
      }
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
  const Uint64 now = SDL_GetTicks();
  const float dt = sLastDraw != 0 ? std::fmin(static_cast<float>(now - sLastDraw) / 1000.f, 0.1f) : 1.f;
  sLastDraw = now;
  // the controls that come and go fade in and out (about a sixth of a second)
  for (int c = 0; c < kControlCount; c++) {
    const float to = shown(c) ? 1.f : 0.f;
    sFade[c] = to > sFade[c] ? std::fmin(sFade[c] + dt * 6.f, 1.f) : std::fmax(sFade[c] - dt * 6.f, 0.f);
  }
  // what is held now, lit
  bool lit[kControlCount] = {};
  u16 dpadLit = 0;
  for (const Finger& f : sFingers) {
    if (f.used && f.control >= 0 && f.control < kControlCount) {
      lit[f.control] = true;
      if (f.control == kDpad) {
        dpadLit |= f.dpad;
      }
    }
  }
  lit[kFast] = lit[kFast] || portSpeedActive();
  lit[kPhoto] = lit[kPhoto] || photoOn();
  lit[kSwap] = lit[kSwap] || gPortPartnerWheelOpen;
  if (sEditing) {
    for (int c = 0; c < kControlCount; c++) {
      // the control being moved or chosen lit (X and Y with A)
      lit[c] = sEditSel == c || (isBean(c) && sEditSel == kA);
    }
    dpadLit = 0;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  for (int c = kA; c < kControlCount; c++) {
    if (sFade[c] <= 0.f) {
      continue;
    }
    sAlpha = sFade[c];
    const Circle& b = l.c[c];
    if (isBean(c)) {
      drawBean(dl, c, l.bean[c], b, kButtons[c].label, lit[c], tint(c));
    } else if (gcStyle() && isPill(c)) {
      drawPill(dl, c, b, kButtons[c].label, lit[c], tint(c));
    } else if (c == kCStick) {
      drawCStick(dl, b, sEditing ? 0.f : sCX01, sEditing ? 0.f : sCY01, lit[c]);
    } else if (c == kDpad) {
      drawDpad(dl, b, dpadLit, lit[c]);
    } else {
      drawCircle(dl, c, b, kButtons[c].label, lit[c], tint(c));
      drawIcon(dl, c, b);
    }
  }
  sAlpha = 1.f;
  if (sEditing) {
    drawStick(dl, l.c[kStick], 0.f, 0.f, sEditSel == kStick);
    drawEditing(dl, l);
  } else {
    Circle base = l.c[kStick];
    if (sStickActive) {
      base.x = sStickOriginX;
      base.y = sStickOriginY;
    }
    drawStick(dl, base, sStickX01, sStickY01, sStickActive);
  }
  shotFrame(sFrame, dl);
}
