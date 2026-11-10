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
//   the area below the picture. Each control can be moved, resized, faded or
//   hidden in the layout editor (portTouchEditLayout: Layout editing, below;
//   from the layout button beside START (touch_edit_button; Done while the
//   editor is open), the settings page, the Start menu's Settings tab, or by
//   holding the menu button), separately for each orientation, saved in
//   touch_layout.txt in the user folder.
//
// $TTYD_TOUCH_MOUSE=1 (desktop testing): the mouse also produces touches.
// $TTYD_TOUCH_TEST=<frame>:<x>,<y>:<frames>[><x2>,<y2>][;...] (tests): a touch
// at window fraction x,y on that drawn frame, held that many frames (moving
// in a straight line to x2,y2 meanwhile, if given), sent through SDL's event
// queue like a real one. $TTYD_TOUCH_EDIT=<frame>[-<frame>]: the layout editor
// opens on that frame (and closes with Done on the second);
// $TTYD_TOUCH_EDIT_DO: steps in it (runEditSteps).
#include "port/file_pc.h"
#include "port/touch_overlay.h"
#include "port/gameplus.h"

#include <SDL3/SDL.h>
#include "../settings/file_util.h"
#include <aurora/aurora.h>
#include <dolphin/pad.h>
#include "port/settings.h"
#include "port/menu_tap.h"
#include "port/menu_buttons.h"
#include "port/speed.h"
#include "port/statesync.h"
#include "port/layout.h"
#include "port/practice.h"
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
extern "C" void portFrameAdvanceHold(int hold);           // port/src/common/practice_pc.c: the game held
extern "C" int portFrameAdvanceHeld(void);
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
  kEdit,   // the layout editor open (and, while it is, Done): not a controller button
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
    {nullptr, 0}, // kEdit: a little layout, drawn (editing: Done)
};

u32 sTouchPort = 0; // the game's controller 1 until a seat swap moves the screen's player (portTouchSetPort)
constexpr float kPi = 3.14159265f;

// Settings (Controls), declared below.
int* sDpadMode;    // touch_dpad: 0 while a menu is up, 1 always, 2 never
int* sColours;     // touch_colours: the GameCube controller's colours
int* sPhotoButton; // touch_photo_button: the camera button
int* sEditButton;  // touch_edit_button: the layout editor's button beside START
int* sEditHint;    // touch_edit_hint: the editor's one-line hint seen (it shows by itself only the first time)

bool editButtonOn() { return sEditButton == nullptr || *sEditButton != 0; }

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

// Where the user moved each control from its default place, in size units,
// its size (times), its opacity (times the settings') and whether it is
// hidden, by orientation (0 landscape, 1 portrait). X and Y go with A.
struct Offset {
  float dx, dy, scale, alpha;
  bool hidden;
};
constexpr Offset kPlainOffset = {0.f, 0.f, 1.f, 1.f, false};
Offset sOffset[2][kControlCount];
bool sOffsetsRead;
// touch_layout.txt from before its "v2" line: a move of A (or of the stick)
// also moved the controls placed from it (B, the C-stick; on a tablet R, Z,
// the swap and L); turned into moves of their own once the size unit is known
bool sLegacy[2];
// touch_layout.txt "v2" (before "v3"): the sideways bottom row's defaults then (START, fast-forward, the
// settings and the camera in other places); its moved buttons kept where the player put them
bool sRowV2;
int sCurPortrait; // the orientation the controls were last laid out in
constexpr float kScaleMin = 0.6f, kScaleMax = 1.8f;
constexpr float kAlphaMin = 0.2f;
// the farthest a control's move is read from touch_layout.txt, in size units (past any screen's edge:
// keepInside brings it back on)
constexpr float kOffsetMax = 64.f;

void resetOffsets(int o) {
  for (Offset& off : sOffset[o]) {
    off = kPlainOffset;
  }
}

// a control's own entry (X and Y: A's)
const Offset& offsetOf(int c) { return sOffset[sCurPortrait][c == kX || c == kY ? kA : c]; }

// Layout editing (portTouchEditLayout, below): over the paused game.
bool sEditing;
int sEditSel = -1; // chosen (its panel up); X and Y are chosen as A

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

// The first time the controls show (touch_move_hint): a note for a few
// seconds that they can be moved, which opens the layout editor when tapped.
int* sMoveHint;
unsigned sHintAt;            // the drawn frame it came up on; 0 not yet, 1 gone
constexpr unsigned kHintDelay = 90, kHintShow = 540; // (drawn frames: 1.5 and 9 s at 60 a second)
float sHintRect[4];          // where it is drawn (x0, y0, x1, y1); empty while it isn't

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
  // "v2" first (moves from each control's own default place), then one
  // control a line: <orientation 0|1> <control> <dx> <dy> [<size> [<opacity> <hidden>]]
  const bool v3 = std::strncmp(text, "v3", 2) == 0;
  const bool v2 = v3 || std::strncmp(text, "v2", 2) == 0;
  bool any = false;
  for (char* line = text; line != nullptr && *line != '\0';) {
    int o = 0;
    int c = 0;
    float dx = 0.f;
    float dy = 0.f;
    float s = 1.f;
    float a = 1.f;
    int hidden = 0;
    const int n = std::sscanf(line, "%d %d %f %f %f %f %d", &o, &c, &dx, &dy, &s, &a, &hidden);
    // (a hand-edited or damaged file's nan or inf skipped: keepInside can't bring a control at NaN back on
    // screen, and the Menu button, which can't be hidden, would be out of reach)
    const bool finite = std::isfinite(dx) && std::isfinite(dy) && (n < 5 || std::isfinite(s)) &&
                        (n < 6 || std::isfinite(a));
    if (n >= 4 && !finite) {
      std::printf("[touch] touch_layout.txt: control %d's line skipped (not a number in it)\n", c);
      std::fflush(stdout);
    }
    if (n >= 4 && finite && o >= 0 && o < 2 && c >= 0 && c < kControlCount) {
      dx = std::clamp(dx, -kOffsetMax, kOffsetMax);
      dy = std::clamp(dy, -kOffsetMax, kOffsetMax);
      sOffset[o][c] = {dx, dy, n >= 5 ? std::clamp(s, kScaleMin, kScaleMax) : 1.f,
                       n >= 6 ? std::clamp(a, kAlphaMin, 1.f) : 1.f,
                       n >= 7 && hidden != 0 && c != kMenu && c != kEdit};
      any = true;
    }
    line = std::strchr(line, '\n');
    if (line != nullptr) {
      line++;
    }
  }
  SDL_free(text);
  sLegacy[0] = sLegacy[1] = any && !v2;
  sRowV2 = any && v2 && !v3;
}

void writeOffsets() {
  // written whole or not at all (port/settings/file_util.h): a phone's OS killing the app half way leaves
  // the old layout, not a cut-short one
  std::string text = "v3\n";
  for (int o = 0; o < 2; o++) {
    for (int c = 0; c < kControlCount; c++) {
      const Offset& off = sOffset[o][c];
      if (off.dx != 0.f || off.dy != 0.f || off.scale != 1.f || off.alpha != 1.f || off.hidden) {
        char line[96];
        std::snprintf(line, sizeof(line), "%d %d %.4f %.4f %.3f %.2f %d\n", o, c, off.dx, off.dy, off.scale,
                      off.alpha, off.hidden ? 1 : 0);
        text += line;
      }
    }
  }
  portfile::writeAll(layoutPath(), text.data(), text.size());
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

// X and Y from A: X a vertical bean hugging A's right side, Y a horizontal
// one over its top, as on the GameCube controller.
void placeBeans(Layout& l) {
  constexpr float kDeg = kPi / 180.f;
  const Circle& a = l.c[kA];
  l.bean[kX] = {a.x, a.y, a.r * 1.78f, a.r * 0.46f, -62.f * kDeg, 30.f * kDeg};
  l.bean[kY] = {a.x, a.y, a.r * 1.78f, a.r * 0.46f, -168.f * kDeg, -100.f * kDeg};
  for (int b = kX; b <= kY; b++) {
    const Bean& n = l.bean[b];
    const float mid = 0.5f * (n.a0 + n.a1);
    l.c[b] = {n.cx + n.rm * std::cos(mid), n.cy + n.rm * std::sin(mid), n.t};
  }
}

// The controls at size unit u, each moved and sized by off (in units).
// The sizes (at PORT_TOUCH_UNIT_MM, 68 mm, a tablet's; a phone's
// PORT_TOUCH_UNIT_PHONE_MM, 61 mm, is 0.9 of each): A 11.6 mm across, B 8.8,
// the stick's gate 24.5, the C-stick 10.9, the D-pad 13.6, L and R 9 mm tall
// and 16.8 wide, Z 6.4 by 11.5, the port's own round buttons 6.8.
// Each control's move is its own: the ones placed from A (or the stick) are
// placed from its default place (legacy: from where it was moved to, as
// touch_layout.txt had it before "v2").
void place(Layout& l, float u, const Offset* off, bool legacy = false) {
  const float sx = l.area.ctrlX;
  const float sy = l.area.ctrlY;
  const float sw = l.area.ctrlW;
  const float sh = l.area.ctrlH;
  l.u = u;
  auto at = [&](int c, float x, float y, float r) {
    return Circle{x + off[c].dx * u, y + off[c].dy * u, r * off[c].scale};
  };
  const float right = sx + sw;
  const float bottom = sy + sh;
  const float mid = sx + sw * 0.5f;
  l.c[kA] = at(kA, right - 0.22f * u, bottom - 0.22f * u, 0.085f * u);
  const Circle a = legacy ? l.c[kA] : Circle{right - 0.22f * u, bottom - 0.22f * u, 0.f};
  l.c[kB] = at(kB, a.x - 0.175f * u, a.y + 0.09f * u, 0.065f * u);
  placeBeans(l);
  l.c[kStick] = at(kStick, sx + 0.25f * u, bottom - 0.25f * u, 0.18f * u);
  const Circle st = legacy ? l.c[kStick] : Circle{sx + 0.25f * u, bottom - 0.25f * u, 0.f};
  // The C-stick: where the GameCube's is, below and left of B, at the
  // thumb's reach from A; held upright there is room above the buttons
  // (clear of the stick's side).
  if (l.portrait) {
    l.c[kCStick] = at(kCStick, a.x - 0.27f * u, a.y - 0.30f * u, 0.08f * u);
  } else {
    l.c[kCStick] = at(kCStick, a.x - 0.37f * u, bottom - 0.11f * u, 0.08f * u);
  }
  // Landscape with little room beside the picture (a tablet's near-4:3
  // screen): the controls are on the picture, so the shoulder buttons go
  // above the thumbs' clusters and START at the bottom, clear of the status
  // bar at the top and of the window's own controls in its corner.
  if (!l.portrait && l.area.picX - sx < 0.30f * u) {
    l.c[kL] = at(kL, st.x, st.y - 0.34f * u, 0.085f * u);
    l.c[kR] = at(kR, a.x + 0.06f * u, a.y - 0.40f * u, 0.085f * u);
    l.c[kZ] = at(kZ, a.x - 0.17f * u, a.y - 0.40f * u, 0.065f * u);
    l.c[kSwap] = at(kSwap, a.x - 0.34f * u, a.y - 0.40f * u, 0.05f * u);
    // the row: the camera, fast-forward, START, the settings, the layout editor's button; five a little
    // smaller and closer than four were, so the row is no wider (between the D-pad and the C-stick: a
    // wider one made a 6 in phone's controls smaller)
    constexpr float kRowR = 0.046f, kRowStep = 0.105f;
    l.c[kStart] = at(kStart, mid, bottom - 0.07f * u, kRowR * u);
    l.c[kFast] = at(kFast, mid - kRowStep * u, bottom - 0.07f * u, kRowR * u);
    l.c[kMenu] = at(kMenu, mid + kRowStep * u, bottom - 0.07f * u, kRowR * u);
    l.c[kPhoto] = at(kPhoto, mid - 2.f * kRowStep * u, bottom - 0.07f * u, kRowR * u);
    l.c[kEdit] = at(kEdit, mid + 2.f * kRowStep * u, bottom - 0.07f * u, kRowR * u);
    // the D-pad right of the stick (its L is above it), as on the GameCube
    l.c[kDpad] = at(kDpad, sx + 0.58f * u, bottom - 0.21f * u, 0.10f * u);
  } else {
    l.c[kL] = at(kL, sx + 0.16f * u, sy + 0.09f * u, 0.085f * u);
    l.c[kR] = at(kR, right - 0.16f * u, sy + 0.09f * u, 0.085f * u);
    l.c[kZ] = at(kZ, right - 0.16f * u, sy + 0.22f * u, 0.065f * u);
    l.c[kSwap] = at(kSwap, right - 0.16f * u, sy + 0.34f * u, 0.05f * u);
    l.c[kStart] = at(kStart, mid, sy + 0.07f * u, 0.05f * u);
    l.c[kFast] = at(kFast, mid - 0.13f * u, sy + 0.07f * u, 0.05f * u);
    l.c[kMenu] = at(kMenu, mid + 0.13f * u, sy + 0.07f * u, 0.05f * u);
    // held upright the top row is narrow (between L and R): the camera under START
    l.c[kPhoto] = l.portrait ? at(kPhoto, mid, sy + 0.19f * u, 0.05f * u)
                             : at(kPhoto, mid - 0.26f * u, sy + 0.07f * u, 0.05f * u);
    // the layout editor's button: after the settings' (upright: under it, beside the camera)
    l.c[kEdit] = l.portrait ? at(kEdit, mid + 0.13f * u, sy + 0.19f * u, 0.05f * u)
                            : at(kEdit, mid + 0.26f * u, sy + 0.07f * u, 0.05f * u);
    // the D-pad above the stick's place, in the left margin (the picture
    // stays clear); moved on its own
    l.c[kDpad] = at(kDpad, sx + 0.25f * u, bottom - 0.62f * u, 0.10f * u);
  }
}

// A control's shape as a few discs (its outline's reach): the round ones one,
// a pill five along its middle, a bean seven along its arc.
struct Disc {
  float x, y, r;
};

int discsOf(const Layout& l, int c, Disc* out) {
  const Circle& b = l.c[c];
  if (isBean(c)) {
    const Bean& n = l.bean[c];
    for (int i = 0; i < 7; i++) {
      const float t = n.a0 + (n.a1 - n.a0) * static_cast<float>(i) / 6.f;
      out[i] = {n.cx + n.rm * std::cos(t), n.cy + n.rm * std::sin(t), n.t};
    }
    return 7;
  }
  if (isPill(c)) {
    float hw = 0.f;
    float hh = 0.f;
    pillSize(c, b, &hw, &hh);
    for (int i = 0; i < 5; i++) {
      out[i] = {b.x + (hw - hh) * (static_cast<float>(i) / 2.f - 1.f), b.y, hh};
    }
    return 5;
  }
  out[0] = {b.x, b.y, c == kStick ? b.r * 1.04f : b.r}; // (the stick's octagon)
  return 1;
}

// Every control inside the area and none touching another (at its default
// place: what the size unit is fitted to).
// report: each clash on stderr ($TTYD_TOUCH_DEVICE tests).
bool fits(const Layout& l, bool report = false) {
  const float m = 0.01f * l.u; // from the edges
  const float g = 0.012f * l.u; // between controls
  Disc d[kControlCount][7];
  int n[kControlCount];
  bool ok = true;
  for (int c = 0; c < kControlCount; c++) {
    n[c] = discsOf(l, c, d[c]);
    for (int i = 0; i < n[c]; i++) {
      const Disc& e = d[c][i];
      if (e.x - e.r < l.area.ctrlX + m || e.x + e.r > l.area.ctrlX + l.area.ctrlW - m ||
          e.y - e.r < l.area.ctrlY + m || e.y + e.r > l.area.ctrlY + l.area.ctrlH - m) {
        if (!report) {
          return false;
        }
        std::fprintf(stderr, "[touch] fit: control %d past an edge\n", c);
        ok = false;
        break;
      }
    }
  }
  for (int c = 0; c < kControlCount; c++) {
    for (int k = c + 1; k < kControlCount; k++) {
      bool clash = false;
      for (int i = 0; i < n[c] && !clash; i++) {
        for (int j = 0; j < n[k] && !clash; j++) {
          const float rr = d[c][i].r + d[k][j].r + g;
          const float dx = d[c][i].x - d[k][j].x;
          const float dy = d[c][i].y - d[k][j].y;
          clash = dx * dx + dy * dy < rr * rr;
        }
      }
      if (clash) {
        if (!report) {
          return false;
        }
        std::fprintf(stderr, "[touch] fit: controls %d and %d touch\n", c, k);
        ok = false;
      }
    }
  }
  return ok;
}

// A control's reach as a box (A with X and Y).
struct Box {
  float x0, y0, x1, y1;
  float cx() const { return 0.5f * (x0 + x1); }
  float cy() const { return 0.5f * (y0 + y1); }
};

Box boxOf(const Layout& l, int c) {
  Box b = {1e9f, 1e9f, -1e9f, -1e9f};
  for (int part = 0; part < 3; part++) {
    const int p = part == 0 ? c : c == kA ? (part == 1 ? kX : kY) : -1;
    if (p < 0) {
      break;
    }
    Disc d[7];
    const int n = discsOf(l, p, d);
    for (int i = 0; i < n; i++) {
      b.x0 = std::fmin(b.x0, d[i].x - d[i].r);
      b.y0 = std::fmin(b.y0, d[i].y - d[i].r);
      b.x1 = std::fmax(b.x1, d[i].x + d[i].r);
      b.y1 = std::fmax(b.y1, d[i].y + d[i].r);
    }
  }
  return b;
}

// Where the controls may go: the controls' area (the safe area: clear of the
// notch and the system bars), a little in from its edges.
Box safeBox(const Layout& l) {
  const float m = 0.01f * l.u;
  return {l.area.ctrlX + m, l.area.ctrlY + m, l.area.ctrlX + l.area.ctrlW - m, l.area.ctrlY + l.area.ctrlH - m};
}

// The user's moves kept on the screen: each control (A with X and Y) pushed
// back inside the area if a move or a size took it past an edge; while the
// layout is edited, the move itself is held there.
void keepInside(Layout& l) {
  Offset* off = sOffset[l.portrait];
  const Box s = safeBox(l);
  for (int c = 0; c < kControlCount; c++) {
    if (isBean(c)) {
      continue; // (with A)
    }
    const Box b = boxOf(l, c);
    const float x0 = b.x0, y0 = b.y0, x1 = b.x1, y1 = b.y1;
    const float lx = s.x0, ly = s.y0, hx = s.x1, hy = s.y1;
    // (larger than the area: kept at its top left)
    const float dx = x0 < lx || x1 - x0 > hx - lx ? lx - x0 : x1 > hx ? hx - x1 : 0.f;
    const float dy = y0 < ly || y1 - y0 > hy - ly ? ly - y0 : y1 > hy ? hy - y1 : 0.f;
    if (dx == 0.f && dy == 0.f) {
      continue;
    }
    l.c[c].x += dx;
    l.c[c].y += dy;
    if (c == kA) {
      placeBeans(l);
    }
    if (sEditing && l.u > 0.f) {
      off[c].dx += dx / l.u;
      off[c].dy += dy / l.u;
    }
  }
}

bool computeLayout(Layout& l) {
  if (!portLayoutGet(&l.area)) {
    return false;
  }
  if (!sOffsetsRead) {
    readOffsets();
  }
  if (l.area.ctrlW <= 0.f || l.area.ctrlH <= 0.f) {
    return false;
  }
  l.portrait = l.area.portrait;
  // The size unit: the screen's real size (PORT_TOUCH_UNIT_MM times the
  // settings' size, port/include/port/layout.h), made smaller until every
  // control fits the area at its default place without touching another
  // (worked out again only when the area or the size changes).
  static PortLayout sLastArea;
  static float sLastWant = -1.f;
  static float sLastU;
  const float want = portLayoutTouchUnit();
  if (want != sLastWant || std::memcmp(&l.area, &sLastArea, sizeof(PortLayout)) != 0) {
    Offset plain[kControlCount];
    for (Offset& e : plain) {
      e = kPlainOffset;
    }
    const Offset* kPlain = plain;
    const bool test = std::getenv("TTYD_TOUCH_DEVICE") != nullptr;
    float u = std::fmax(want, 1.f);
    // (no smaller than about a third: a screen that small gets them overlapping)
    for (int i = 0; i < 35; i++) {
      place(l, u, kPlain);
      if (fits(l, test && i == 0)) {
        break;
      }
      u *= 0.97f;
    }
    sLastArea = l.area;
    sLastWant = want;
    sLastU = u;
    if (test) {
      const float mm = portLayoutPointsPerMm();
      // the smallest round button's reach across (hitTest's: 1.35 of its size), as laid out
      float reach = 1e9f;
      for (int c : {kStart, kFast, kMenu, kPhoto, kEdit, kSwap}) {
        reach = std::fmin(reach, 2.f * 1.35f * l.c[c].r);
      }
      std::fprintf(stderr,
                   "[touch] size unit %.1f mm (asked %.1f mm): A %.1f mm, stick %.1f mm, L %.1f mm tall; B %.1f mm, "
                   "D-pad %.1f mm, L %.1f mm wide, the smallest button's reach %.1f mm\n",
                   u / mm, want / mm, 0.17f * u / mm, 0.36f * u / mm, 2.f * 0.085f * 0.78f * u / mm, 0.13f * u / mm,
                   0.20f * u / mm, 2.f * 0.085f * 1.45f * u / mm, reach / mm);
    }
  }
  sCurPortrait = l.portrait;
  if (sLegacy[l.portrait]) {
    // an older touch_layout.txt: the same places, each control's move its own
    sLegacy[l.portrait] = false;
    Offset* off = sOffset[l.portrait];
    Offset plain[kControlCount];
    for (int c = 0; c < kControlCount; c++) {
      plain[c] = {0.f, 0.f, off[c].scale, off[c].alpha, off[c].hidden};
    }
    Layout was = l;
    Layout base = l;
    place(was, sLastU, off, true);
    place(base, sLastU, plain);
    for (int c = 0; c < kControlCount; c++) {
      if (!isBean(c) && sLastU > 0.f) {
        off[c].dx = (was.c[c].x - base.c[c].x) / sLastU;
        off[c].dy = (was.c[c].y - base.c[c].y) / sLastU;
      }
    }
  }
  if (sRowV2 && !l.portrait && sLastU > 0.f) {
    // a "v2" file: the sideways row's buttons the player moved (on the picture's side, where that row is used)
    // stay where they were, their moves now from today's defaults (START mid + 0.07 then, fast-forward
    // mid - 0.07, the settings mid + 0.21; the camera's place is the same)
    sRowV2 = false;
    if (l.area.picX - l.area.ctrlX < 0.30f * sLastU) {
      constexpr struct {
        int c;
        float was;
      } kRow[] = {{kStart, 0.07f}, {kFast, 0.105f - 0.07f}, {kMenu, 0.21f - 0.105f}};
      for (const auto& r : kRow) {
        Offset& off = sOffset[0][r.c];
        if (off.dx != 0.f || off.dy != 0.f) {
          off.dx += r.was;
        }
      }
    }
  }
  place(l, sLastU, sOffset[l.portrait]);
  keepInside(l);
  const float sx = l.area.ctrlX;
  const float sy = l.area.ctrlY;
  const float sw = l.area.ctrlW;
  const float u = l.u;
  l.stickZoneRight = std::fmax(sx + sw * 0.45f, l.c[kStick].x + l.c[kStick].r * 1.5f);
  l.stickZoneTop = std::fmin(sy + 0.2f * u, l.c[kStick].y - l.c[kStick].r * 1.5f);
  return true;
}

#ifdef __EMSCRIPTEN__
bool sGamepadInUse = true; // the browser: hidden (keyboard or controller) until the screen is first touched
#else
bool sGamepadInUse;
#endif
constexpr Sint16 kGamepadDeadZone = 8000;
SDL_JoystickID sTakeOver; // a controller used since the last draw (0 none): it hides the controls if it is player 1's

// the controller with that id plays the touch controls' port (port 1, player 1, until a seat swap) now
bool playsPortOne(SDL_JoystickID which) {
  SDL_Gamepad* pad = SDL_GetGamepadFromID(which);
  const s32 index = PADGetIndexForPort(sTouchPort);
  return pad != nullptr && index >= 0 && PADGetSDLGamepadForIndex(static_cast<u32>(index)) == pad;
}

// At each draw: a controller used since the last one takes the controls over when it is player 1's (the
// first controller connected, a handheld's built-in controls, or the one chosen for player 1); another
// player's (one that joined with Start, or was chosen as player 2's while player 1 plays on the screen)
// leaves them to player 1. And with no controller on player 1's port at all, the touch controls are player
// 1's: shown.
void decideTakeOver() {
  if (sTakeOver != 0) {
    if (playsPortOne(sTakeOver)) {
      sGamepadInUse = !sEditing;
    }
    sTakeOver = 0;
  }
#ifndef __EMSCRIPTEN__ // (the browser: hidden until the screen is first touched)
  if (sGamepadInUse && !sEditing && PADGetIndexForPort(sTouchPort) < 0) {
    sGamepadInUse = false;
  }
#endif
}

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
  if (c == kEdit && !editButtonOn()) {
    return false; // (touch_edit_button off: not even in the editor)
  }
  if (sEditing || held(c)) {
    return true;
  }
  if (offsetOf(c).hidden) {
    return false; // hidden in the layout editor
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
  case kEdit:
    return !photoOn(); // (photo mode has the controls)
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
    PADSetVirtualStatus(sTouchPort, &status);
    portMenuSetVirtualHeld(sTouchPort, 0);
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
  PADSetVirtualStatus(sTouchPort, &status);
  portMenuSetVirtualHeld(sTouchPort, status.button); // (the page's B is the game's B: portMenuButtons)
  sStatusSet = true;
}

void clearStatus() {
  if (sStatusSet) {
    PADClearVirtualStatus(sTouchPort);
    portMenuSetVirtualHeld(-1, 0);
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
  if (best == kNone && !offsetOf(kStick).hidden && px < l.stickZoneRight && py > l.stickZoneTop) {
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
  } else if (f.control == kMenu && !f.consumed) {
    // on the tap's release, so the page doesn't take this finger (held: the
    // layout editor took it)
    portSettingsToggle();
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
// arrows, the settings' bars, the camera, the layout button's little layout
// (drawn shapes, as the rest: no outside art).
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
  } else if (c == kEdit) {
    // the controls' layout in little: a screen with a stick on the left and two buttons on the right
    const ImVec2 b0(m.x - 0.52f * r, m.y - 0.32f * r);
    const ImVec2 b1(m.x + 0.52f * r, m.y + 0.32f * r);
    dl->AddRect(b0, b1, ink, 0.12f * r, 0, r * 0.10f);
    dl->AddCircle(ImVec2(m.x - 0.26f * r, m.y + 0.04f * r), 0.14f * r, ink, 16, r * 0.09f);
    dl->AddCircleFilled(ImVec2(m.x + 0.30f * r, m.y + 0.10f * r), 0.09f * r, ink, 12);
    dl->AddCircleFilled(ImVec2(m.x + 0.14f * r, m.y - 0.08f * r), 0.07f * r, ink, 12);
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
//
// Over the paused game (frame advance's hold, as photo mode's), the controls
// at their real size, dragged with a finger:
// - The one picked is the one under the finger whose middle is nearest (or,
//   with none under it, the nearest within a few millimetres). It lifts (a
//   little larger, a shadow, its outline) and its group comes with it: the
//   stick and the D-pad; A (with X and Y), B and the C-stick; L, R and Z (L
//   and R mirrored, so they stay at the two sides). The panel's Move alone
//   moves one by itself.
// - It snaps to the area's edges and middle, to the other controls' edges and
//   middles (a guide line shows which) and to a light grid; held still a
//   moment before it moves, it moves freely. It never leaves the controls'
//   area (the safe area: clear of the notch and the system bars).
// - A second finger pinches it to a size. A tap chooses one: a small panel
//   beside it with its size (mm), its opacity, hide or show it, Move alone
//   and its default place.
// - A small toolbar: Undo, Presets (Default, Handheld or Phone upright,
//   Left-handed), Reset, Cancel (everything as it was) and "?" (a one-line
//   hint, shown by itself the first time the editor opens). Done is the
//   layout button's (in the bottom row; in the toolbar only with that button
//   turned off). The toolbar never keeps a control out of reach: it goes where
//   it covers none (the middle of the picture first), hides while one moves,
//   and a finger on a control is the control's even over a toolbar button.
//   Held upright and sideways each have their own layout.

float mmPts() { return std::fmax(portLayoutPointsPerMm(), 1.f); }

bool sGroupMoves = true;            // a control's group moves with it (the panel's Move alone: off)
int sEditWasHeld;                   // frame advance's hold before the editor took it (given back)
Offset sEditOrig[2][kControlCount]; // as it was when the editor opened (Cancel)
bool sPresetsOpen;
unsigned sEditAsked;                // portTouchEditRequest: drawn frames since
unsigned sDrawn;                    // frames drawn (portTouchDraw)
constexpr Uint64 kFreeHoldMs = 400; // held still this long before moving: no snapping

struct UndoStep {
  int o;
  Offset off[kControlCount];
};
std::vector<UndoStep> sUndo;

void pushUndo(int o) {
  if (sUndo.size() >= 64) {
    sUndo.erase(sUndo.begin());
  }
  UndoStep s;
  s.o = o;
  std::memcpy(s.off, sOffset[o], sizeof(s.off));
  sUndo.push_back(s);
}

bool hasGroup(int c) {
  return c == kStick || c == kDpad || c == kA || c == kB || c == kCStick || c == kL || c == kR || c == kZ;
}

// c and the controls that move with it, and which way each goes across (-1:
// mirrored); c first
constexpr int kMaxGroup = 3;
int groupOf(int c, int* m, float* sx) {
  int n = 0;
  auto add = [&](int k, float s) {
    m[n] = k;
    sx[n] = s;
    n++;
  };
  add(c, 1.f);
  if (!sGroupMoves) {
    return n;
  }
  switch (c) {
  case kStick:
    add(kDpad, 1.f);
    break;
  case kDpad:
    add(kStick, 1.f);
    break;
  case kA:
    add(kB, 1.f);
    add(kCStick, 1.f);
    break;
  case kB:
    add(kA, 1.f);
    add(kCStick, 1.f);
    break;
  case kCStick:
    add(kA, 1.f);
    add(kB, 1.f);
    break;
  case kL:
    add(kR, -1.f);
    add(kZ, -1.f);
    break;
  case kR:
    add(kL, -1.f);
    add(kZ, 1.f);
    break;
  case kZ:
    add(kR, 1.f);
    add(kL, -1.f);
    break;
  default:
    break;
  }
  return n;
}

struct Drag {
  bool on;
  SDL_FingerID id;
  int n;
  int m[kMaxGroup];
  float sx[kMaxGroup];
  Offset start[kMaxGroup];
  Box box0[kMaxGroup];
  float fx0, fy0, fx, fy; // where the finger went down, where it is
  Uint64 at;
  unsigned movedAt; // the drawn frame of its last move: the toolbar and the panel stay out of the way only while it moves
  bool moving, free;
  bool onDone; // went down on the layout button (Done meanwhile): lifted without moving, it is Done's tap
  bool gx, gy; // snapped to a line (drawn): across, down
  float guideX, guideY;
} sDrag;

struct {
  bool on;
  SDL_FingerID id;
  float x, y, d0, s0;
} sPinch;

// a held finger's moves are "busy" this long after the last (a thumb left resting on a control after
// moving it, as on a handheld held in both hands, must not keep the bar faded and out of reach)
constexpr unsigned kBusyFrames = 30; // (half a second at 60 frames a second)

// The toolbar's and the panel's parts, as drawn last (the touches find them).
enum UiId {
  kUiNone,
  kUiUndo,
  kUiPresets,
  kUiReset,
  kUiCancel,
  kUiDone, // (the toolbar's only while the layout button, Done meanwhile, is turned off)
  kUiHelp, // "?": the one-line hint shown again or hidden
  kUiPreset0 = 10, // + the preset
  kUiPanel = 20,   // the panel's own background (and everything on it below)
  kUiSize,
  kUiAlpha,
  kUiHide,
  kUiAlone,
  kUiResetOne,
};
bool uiPanelPart(int id) { return id >= kUiPanel; }
bool uiBackground(int id) { return id == kUiPanel; } // (no button: a tap there does nothing)
bool uiSlider(int id) { return id == kUiSize || id == kUiAlpha; }

struct UiRect {
  int id;
  float x0, y0, x1, y1;
  float t0, t1; // a slider's track
  bool enabled;
};
constexpr int kMaxUi = 32;
UiRect sUi[kMaxUi];
int sUiCount;
bool sHintOn; // the one-line hint over the toolbar (the editor's first opening, or "?")
float sPanelBox[4]; // the panel as drawn last (x0, y0, x1, y1; empty while none): the toolbar keeps clear of it
struct {
  bool on;
  SDL_FingerID id;
  int item;
} sUiPress; // a button held (it acts on release over it), or a slider dragged

const UiRect* uiAt(float px, float py) {
  for (int i = sUiCount - 1; i >= 0; i--) { // (the last drawn is on top)
    const UiRect& r = sUi[i];
    if (px >= r.x0 && px < r.x1 && py >= r.y0 && py < r.y1) {
      return &r;
    }
  }
  return nullptr;
}

const UiRect* uiFind(int id) {
  for (int i = 0; i < sUiCount; i++) {
    if (sUi[i].id == id) {
      return &sUi[i];
    }
  }
  return nullptr;
}

UiRect* uiAdd(int id, float x0, float y0, float x1, float y1, bool enabled = true) {
  if (sUiCount >= kMaxUi) {
    return nullptr;
  }
  sUi[sUiCount] = {id, x0, y0, x1, y1, 0.f, 0.f, enabled};
  return &sUi[sUiCount++];
}

// --- Presets ---

enum Preset { kPresetDefault, kPresetHandheld, kPresetPhone, kPresetLeft, kPresetCount };
const char* const kPresetNames[kPresetCount] = {"Default", "Handheld", "Phone upright", "Left-handed"};

// the ones offered: sideways Default, Handheld, Left-handed; upright Default,
// Phone upright, Left-handed
int presetsFor(int portrait, int* out) {
  out[0] = kPresetDefault;
  out[1] = portrait ? kPresetPhone : kPresetHandheld;
  out[2] = kPresetLeft;
  return 3;
}

// A preset's places and sizes for the orientation shown (each control's
// opacity and hiding stay as they are).
// - Handheld (an Odin, a Retroid): everything a little smaller and hugging
//   the edges: the thumbs' clusters in the bottom corners, L and R in the top
//   ones, Z and the swap under R, the top row against its edge.
// - Phone upright: a little smaller, the clusters at the sides and lifted
//   off the bottom (clear of the gesture bar and the palm), L and R just
//   above them, Z and the swap beside R.
// - Left-handed: mirrored: the stick on the right, the buttons on the left.
void applyPreset(const Layout& cur, int preset) {
  Offset* off = sOffset[cur.portrait];
  const float u = cur.u;
  if (u <= 0.f) {
    return;
  }
  Offset next[kControlCount];
  for (int c = 0; c < kControlCount; c++) {
    next[c] = {0.f, 0.f, 1.f, off[c].alpha, off[c].hidden};
    if (preset == kPresetHandheld || preset == kPresetPhone) {
      next[c].scale = preset == kPresetHandheld ? 0.85f : 0.9f;
    }
  }
  Layout t = cur;
  place(t, u, next);
  Box b[kControlCount];
  for (int c = 0; c < kControlCount; c++) {
    b[c] = boxOf(t, c);
  }
  auto move = [&](int c, float dx, float dy) {
    next[c].dx += dx / u;
    next[c].dy += dy / u;
    b[c] = {b[c].x0 + dx, b[c].y0 + dy, b[c].x1 + dx, b[c].y1 + dy};
  };
  auto unite = [&](std::initializer_list<int> cs) {
    Box r = {1e9f, 1e9f, -1e9f, -1e9f};
    for (int c : cs) {
      r = {std::fmin(r.x0, b[c].x0), std::fmin(r.y0, b[c].y0), std::fmax(r.x1, b[c].x1), std::fmax(r.y1, b[c].y1)};
    }
    return r;
  };
  const Box s = safeBox(cur);
  const float mid = s.cx();
  const float gap = 1.5f * mmPts();
  if (preset == kPresetHandheld || preset == kPresetPhone) {
    const float lift = preset == kPresetPhone ? 7.f * mmPts() : 0.f;
    const Box lg = unite({kStick, kDpad});
    for (int c : {kStick, kDpad}) {
      move(c, s.x0 - lg.x0, s.y1 - lift - lg.y1);
    }
    const Box fg = unite({kA, kB, kCStick});
    for (int c : {kA, kB, kCStick}) {
      move(c, s.x1 - fg.x1, s.y1 - lift - fg.y1);
    }
  }
  if (preset == kPresetHandheld) {
    move(kL, s.x0 - b[kL].x0, s.y0 - b[kL].y0);
    move(kR, s.x1 - b[kR].x1, s.y0 - b[kR].y0);
    move(kZ, s.x1 - b[kZ].x1, b[kR].y1 + gap - b[kZ].y0);
    move(kSwap, s.x1 - b[kSwap].x1, b[kZ].y1 + gap - b[kSwap].y0);
    for (int c : {kStart, kFast, kMenu, kPhoto, kEdit}) {
      const bool top = b[c].cy() < s.cy();
      move(c, 0.f, top ? s.y0 - b[c].y0 : s.y1 - b[c].y1);
    }
  } else if (preset == kPresetPhone) {
    const Box lg = unite({kStick, kDpad});
    const Box fg = unite({kA, kB, kCStick});
    move(kL, lg.cx() - b[kL].cx(), lg.y0 - gap - b[kL].y1);
    move(kR, fg.cx() - b[kR].cx(), fg.y0 - gap - b[kR].y1);
    move(kZ, b[kR].x0 - gap - b[kZ].x1, b[kR].cy() - b[kZ].cy());
    move(kSwap, b[kZ].x0 - gap - b[kSwap].x1, b[kZ].cy() - b[kSwap].cy());
  } else if (preset == kPresetLeft) {
    const Box lg = unite({kStick, kDpad});
    const Box fg = unite({kA, kB, kCStick});
    for (int c : {kStick, kDpad}) {
      move(c, 2.f * (mid - lg.cx()), 0.f);
    }
    for (int c : {kA, kB, kCStick}) {
      move(c, 2.f * (mid - fg.cx()), 0.f);
    }
    for (int c : {kL, kR, kZ, kSwap, kStart, kFast, kMenu, kPhoto, kEdit}) {
      move(c, 2.f * (mid - b[c].cx()), 0.f);
    }
  }
  std::memcpy(off, next, sizeof(next));
}

// --- Picking, dragging, snapping ---

// The control a finger at (px, py) takes: of those under it, the one whose
// middle is nearest; with none (and not onlyOn), the nearest within a few
// millimetres. X and Y are A.
int pickControl(const Layout& l, float px, float py, bool onlyOn) {
  const float reach = 6.f * mmPts();
  int best = -1;
  float bestD = 0.f;
  bool bestIn = false;
  for (int c = kStick; c < kControlCount; c++) {
    if (c == kEdit && !editButtonOn()) {
      continue; // (not shown)
    }
    const float d = controlDistance(l, c, px, py);
    float r = l.c[c].r;
    if (isBean(c)) {
      r = l.bean[c].t;
    } else if (isPill(c)) {
      float hw = 0.f;
      pillSize(c, l.c[c], &hw, &r);
    }
    const bool in = d <= 1.f;
    if (!in && (onlyOn || (d - 1.f) * r > reach)) {
      continue;
    }
    const float cd = std::hypot(px - l.c[c].x, py - l.c[c].y);
    if (best < 0 || (in && !bestIn) || (in == bestIn && cd < bestD)) {
      best = c;
      bestD = cd;
      bestIn = in;
    }
  }
  return best < 0 ? -1 : isBean(best) ? kA : best;
}

void beginDrag(const Layout& l, int c, SDL_FingerID id, float px, float py) {
  sDrag = {};
  sDrag.on = true;
  sDrag.id = id;
  sDrag.fx0 = sDrag.fx = px;
  sDrag.fy0 = sDrag.fy = py;
  sDrag.at = SDL_GetTicks();
  sDrag.n = groupOf(c, sDrag.m, sDrag.sx);
  for (int i = 0; i < sDrag.n; i++) {
    sDrag.start[i] = sOffset[l.portrait][sDrag.m[i]];
    sDrag.box0[i] = boxOf(l, sDrag.m[i]);
  }
  // (Done lies over another control: a tap there is Done's, a drag the control's, so neither is ever out of reach)
  sDrag.onDone = editButtonOn() && controlDistance(l, kEdit, px, py) <= 1.f;
  sEditSel = c;
  if (std::getenv("TTYD_TOUCH_DEVICE") != nullptr) {
    // (tests: every grab, and where: a control is never out of reach)
    const Box b = boxOf(l, c);
    std::fprintf(stderr, "[touch] grab: control %d at %.0f,%.0f-%.0f,%.0f (finger %.0f,%.0f)%s\n", c, b.x0, b.y0, b.x1,
                 b.y1, px, py, sDrag.onDone ? " on Done" : "");
  }
  haptic();
}

// One axis's snap for the moving box's low edge, middle and high edge (lo,
// hi): to the area's edges and middle, to the other controls' edges and
// middles (or beside them), else to the grid. The shift, and the guide line
// (*guide; *snapped false for the grid or nothing).
float snapAxis(float lo, float hi, float areaLo, float areaHi, const float* otherLo, const float* otherHi, int others,
               float* guide, bool* snapped) {
  const float mm = mmPts();
  const float reach = 2.f * mm;
  const float gap = 1.2f * mm;
  const float mine[3] = {lo, 0.5f * (lo + hi), hi};
  float best = reach;
  float shift = 0.f;
  *snapped = false;
  auto consider = [&](int e, float target, float line) {
    const float d = target - mine[e];
    if (std::fabs(d) < best) {
      best = std::fabs(d);
      shift = d;
      *guide = line;
      *snapped = true;
    }
  };
  consider(0, areaLo, areaLo);
  consider(2, areaHi, areaHi);
  consider(1, 0.5f * (areaLo + areaHi), 0.5f * (areaLo + areaHi));
  for (int i = 0; i < others; i++) {
    const float a = otherLo[i];
    const float b = otherHi[i];
    consider(0, a, a);
    consider(2, b, b);
    consider(1, 0.5f * (a + b), 0.5f * (a + b));
    consider(0, b + gap, b); // beside it
    consider(2, a - gap, a);
  }
  if (*snapped) {
    return shift;
  }
  // the grid: its lines 4 mm apart from the area's edge; near one, on it
  const float g = 4.f * mm;
  const float c = mine[1];
  const float on = areaLo + std::round((c - areaLo) / g) * g;
  return std::fabs(on - c) < g * 0.22f ? on - c : 0.f;
}

void dragTo(const Layout& l, float px, float py) {
  float dx = px - sDrag.fx0;
  float dy = py - sDrag.fy0;
  if (!sDrag.moving) {
    if (std::hypot(dx, dy) < 1.5f * mmPts()) {
      return; // (still a tap)
    }
    sDrag.moving = true;
    sDrag.free = SDL_GetTicks() - sDrag.at >= kFreeHoldMs;
    sHintOn = false; // (the hint has done its job)
    pushUndo(l.portrait);
  }
  const bool wasX = sDrag.gx;
  const bool wasY = sDrag.gy;
  sDrag.gx = sDrag.gy = false;
  const Box s = safeBox(l);
  if (!sDrag.free) {
    // the other controls' edges (the group's own, moving, are not lines)
    float ox[kControlCount], oxh[kControlCount], oy[kControlCount], oyh[kControlCount];
    int n = 0;
    for (int c = kStick; c < kControlCount; c++) {
      bool member = isBean(c);
      for (int i = 0; i < sDrag.n; i++) {
        member = member || sDrag.m[i] == c;
      }
      if (member) {
        continue;
      }
      const Box b = boxOf(l, c);
      ox[n] = b.x0;
      oxh[n] = b.x1;
      oy[n] = b.y0;
      oyh[n] = b.y1;
      n++;
    }
    const Box& b0 = sDrag.box0[0];
    dx += snapAxis(b0.x0 + dx, b0.x1 + dx, s.x0, s.x1, ox, oxh, n, &sDrag.guideX, &sDrag.gx);
    dy += snapAxis(b0.y0 + dy, b0.y1 + dy, s.y0, s.y1, oy, oyh, n, &sDrag.guideY, &sDrag.gy);
  }
  // every one of the group inside the area
  float xlo = -1e9f, xhi = 1e9f, ylo = -1e9f, yhi = 1e9f;
  for (int i = 0; i < sDrag.n; i++) {
    const Box& b = sDrag.box0[i];
    if (sDrag.sx[i] > 0.f) {
      xlo = std::fmax(xlo, s.x0 - b.x0);
      xhi = std::fmin(xhi, s.x1 - b.x1);
    } else {
      xlo = std::fmax(xlo, b.x1 - s.x1);
      xhi = std::fmin(xhi, b.x0 - s.x0);
    }
    ylo = std::fmax(ylo, s.y0 - b.y0);
    yhi = std::fmin(yhi, s.y1 - b.y1);
  }
  if (xlo <= xhi && (dx < xlo || dx > xhi)) {
    dx = std::clamp(dx, xlo, xhi);
    sDrag.gx = false;
  }
  if (ylo <= yhi && (dy < ylo || dy > yhi)) {
    dy = std::clamp(dy, ylo, yhi);
    sDrag.gy = false;
  }
  if ((sDrag.gx && !wasX) || (sDrag.gy && !wasY)) {
    haptic(); // a notch as it snaps
  }
  sDrag.movedAt = sDrawn;
  for (int i = 0; i < sDrag.n; i++) {
    Offset& o = sOffset[l.portrait][sDrag.m[i]];
    o.dx = sDrag.start[i].dx + sDrag.sx[i] * dx / l.u;
    o.dy = sDrag.start[i].dy + dy / l.u;
  }
}

void setScale(int o, int c, float s) {
  sOffset[o][c].scale = std::clamp(std::round(s * 100.f) / 100.f, kScaleMin, kScaleMax);
}

void pinchTo(int o) {
  if (!sPinch.on || !sDrag.on || sPinch.d0 < 8.f) {
    return;
  }
  setScale(o, sDrag.m[0], sPinch.s0 * std::hypot(sPinch.x - sDrag.fx, sPinch.y - sDrag.fy) / sPinch.d0);
  sDrag.movedAt = sDrawn;
}

// a control held and moving or being pinched just now: the bar faint, the panel away
bool editBusy() {
  return ((sDrag.on && sDrag.moving) || (sDrag.on && sPinch.on)) && sDrawn - sDrag.movedAt < kBusyFrames;
}

// a slider's value from the finger's place on its track
void slide(const Layout& l, const UiRect& r, float px) {
  if (sEditSel < 0) {
    return;
  }
  const float t = std::clamp((px - r.t0) / std::fmax(r.t1 - r.t0, 1.f), 0.f, 1.f);
  Offset& o = sOffset[l.portrait][sEditSel];
  if (r.id == kUiSize) {
    setScale(l.portrait, sEditSel, kScaleMin + t * (kScaleMax - kScaleMin));
  } else {
    o.alpha = std::round((kAlphaMin + t * (1.f - kAlphaMin)) * 20.f) / 20.f;
  }
}

void finishEditing(bool save) {
  if (save && std::getenv("TTYD_TOUCH_DEVICE") != nullptr) {
    // (tests: what the editor changed)
    for (int o = 0; o < 2; o++) {
      for (int c = 0; c < kControlCount; c++) {
        const Offset& a = sEditOrig[o][c];
        const Offset& b = sOffset[o][c];
        if (a.dx != b.dx || a.dy != b.dy || a.scale != b.scale || a.alpha != b.alpha || a.hidden != b.hidden) {
          std::fprintf(stderr, "[touch] saved: control %d (%s) moved %.2f,%.2f size %.2f%s\n", c,
                       o != 0 ? "upright" : "sideways", b.dx - a.dx, b.dy - a.dy, b.scale,
                       b.hidden ? " hidden" : "");
        }
      }
    }
  }
  if (save) {
    writeOffsets();
  } else {
    std::memcpy(sOffset, sEditOrig, sizeof(sOffset));
  }
  sEditing = false;
  sEditSel = -1;
  sDrag.on = false;
  sPinch.on = false;
  sUiPress.on = false;
  sPresetsOpen = false;
  sHintOn = false;
  sUndo.clear();
  PADBlockInput(false);
  portFrameAdvanceHold(sEditWasHeld);
  std::fprintf(stderr, "[touch] layout editor closed (%s)\n", save ? "saved" : "cancelled");
}

void uiAct(const Layout& l, int item) {
  const int o = l.portrait;
  haptic();
  switch (item) {
  case kUiUndo:
    if (!sUndo.empty()) {
      const UndoStep& s = sUndo.back();
      std::memcpy(sOffset[s.o], s.off, sizeof(s.off));
      sUndo.pop_back();
    }
    break;
  case kUiPresets:
    sPresetsOpen = !sPresetsOpen;
    break;
  case kUiHelp:
    sHintOn = !sHintOn;
    break;
  case kUiReset:
    pushUndo(o);
    resetOffsets(o);
    sEditSel = -1;
    break;
  case kUiCancel:
    finishEditing(false);
    break;
  case kUiDone:
    finishEditing(true);
    break;
  case kUiHide:
    if (sEditSel >= 0 && sEditSel != kMenu && sEditSel != kEdit) {
      pushUndo(o);
      sOffset[o][sEditSel].hidden = !sOffset[o][sEditSel].hidden;
    }
    break;
  case kUiAlone:
    sGroupMoves = !sGroupMoves;
    break;
  case kUiResetOne:
    if (sEditSel >= 0) {
      pushUndo(o);
      sOffset[o][sEditSel] = kPlainOffset;
    }
    break;
  default:
    if (item >= kUiPreset0 && item < static_cast<int>(kUiPreset0) + static_cast<int>(kPresetCount)) {
      pushUndo(o);
      applyPreset(l, item - kUiPreset0);
      sEditSel = -1;
    }
    break;
  }
}

void editEvent(const Layout& l, Uint32 type, SDL_FingerID id, float px, float py) {
  const int o = l.portrait;
  if (type == SDL_EVENT_FINGER_DOWN) {
    const UiRect* r = uiAt(px, py);
    // A finger on a control is the control's, a toolbar button under it or not (the toolbar moves out of
    // the way: a control is never trapped under it). The panel's sliders and buttons are the panel's (it
    // is placed clear of the controls, and goes with a tap away); its background is the control's.
    if (r != nullptr && (!uiPanelPart(r->id) || uiBackground(r->id))) {
      if (const int under = pickControl(l, px, py, true); under >= 0) {
        if (std::getenv("TTYD_TOUCH_DEVICE") != nullptr) {
          std::fprintf(stderr, "[touch] ui %d over control %d: the control's\n", r->id, under);
        }
        r = nullptr;
      }
    }
    if (sDrag.on && id != sDrag.id) {
      // A second finger while one holds a control. A handheld is held in both hands, a thumb often left
      // resting on a control: every other touch can't be that control's pinch, or Done and the rest of
      // the bar would be out of reach (faint, and taken for the pinch) for as long as it rests there.
      if (sPinch.on) {
        return; // (a third finger: nothing)
      }
      if (r != nullptr && !uiBackground(r->id)) {
        // on the bar or the panel: its button (one finger there at a time)
        if (r->enabled && !sUiPress.on) {
          sUiPress = {true, id, r->id};
          if (uiSlider(r->id)) {
            pushUndo(o);
            slide(l, *r, px);
          }
        }
        return;
      }
      if (r != nullptr) {
        return; // (the bar's or the panel's background)
      }
      const int c = pickControl(l, px, py, true);
      bool held = false;
      for (int i = 0; i < sDrag.n; i++) {
        held = held || sDrag.m[i] == c;
      }
      if (c >= 0 && !held) {
        // on another control: it moves (the first finger, resting, let go of), unless the first is
        // moving it just now (two at once: the first keeps on)
        if (!editBusy()) {
          sPresetsOpen = false;
          beginDrag(l, c, id, px, py);
        }
        return;
      }
      // on the control held or near it: a pinch, its size
      const Box b = boxOf(l, sDrag.m[0]);
      const float away = std::hypot(std::fmax(std::fmax(b.x0 - px, px - b.x1), 0.f),
                                    std::fmax(std::fmax(b.y0 - py, py - b.y1), 0.f));
      if (away <= 30.f * mmPts()) {
        pushUndo(o);
        sPinch = {true, id, px, py, std::hypot(px - sDrag.fx, py - sDrag.fy), sOffset[o][sDrag.m[0]].scale};
      }
      return;
    }
    if (sUiPress.on) {
      return; // (one finger on the bar or the panel at a time)
    }
    if (r == nullptr) {
      // on a control or near one: it
      const int c = pickControl(l, px, py, false);
      if (c >= 0) {
        sPresetsOpen = false;
        beginDrag(l, c, id, px, py);
        return;
      }
    } else {
      if (uiBackground(r->id) || !r->enabled) {
        return;
      }
      sUiPress = {true, id, r->id};
      if (uiSlider(r->id)) {
        pushUndo(o);
        slide(l, *r, px);
      }
      return;
    }
    sEditSel = -1; // a tap away from everything: nothing chosen
    sPresetsOpen = false;
  } else if (type == SDL_EVENT_FINGER_MOTION) {
    if (sUiPress.on && id == sUiPress.id) {
      const UiRect* r = uiFind(sUiPress.item);
      if (r != nullptr && uiSlider(sUiPress.item)) {
        slide(l, *r, px);
      }
    } else if (sDrag.on && id == sDrag.id) {
      sDrag.fx = px;
      sDrag.fy = py;
      if (sPinch.on) {
        pinchTo(o); // (pinching: the size changes, the place stays)
      } else {
        dragTo(l, px, py);
      }
    } else if (sPinch.on && id == sPinch.id) {
      sPinch.x = px;
      sPinch.y = py;
      pinchTo(o);
    }
  } else if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) {
    if (sUiPress.on && id == sUiPress.id) {
      const int item = sUiPress.item;
      sUiPress.on = false;
      const UiRect* r = uiFind(item);
      if (!uiSlider(item) && type == SDL_EVENT_FINGER_UP && r != nullptr && px >= r->x0 && px < r->x1 &&
          py >= r->y0 && py < r->y1) {
        uiAct(l, item);
      }
    } else if (sDrag.on && id == sDrag.id) {
      // a tap on the layout editor's button (its Done now), a control under it or not: saved and closed
      const bool done = sDrag.onDone && !sDrag.moving && !sPinch.on && type == SDL_EVENT_FINGER_UP;
      if (sDrag.moving && !sPinch.on) {
        sEditSel = -1; // (moved: no panel popping up after it; a tap brings one)
      }
      sDrag.on = false;
      sPinch.on = false;
      if (done) {
        uiAct(l, kUiDone);
      }
    } else if (sPinch.on && id == sPinch.id) {
      sPinch.on = false;
      sDrag.on = false; // (the other finger's moves from here would jump)
    }
  }
}

// --- Drawing the editor ---

constexpr ImU32 kAccent = IM_COL32(110, 205, 255, 255);
constexpr ImU32 kFreeAccent = IM_COL32(255, 170, 60, 255);

ImU32 withAlpha(ImU32 c, float a) {
  const int alpha = std::clamp(static_cast<int>(255.f * a), 0, 255);
  return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

ImVec2 textExt(float size, const char* text, float wrap = 0.f) {
  return ImGui::GetFont()->CalcTextSizeA(size, 1e9f, wrap, text);
}

struct UiMetrics {
  float mm, h, fs, pad, gap;
};

UiMetrics uiMetrics() {
  const float mm = mmPts();
  const float h = std::clamp(6.5f * mm, 30.f, 64.f); // (a fingertip's height)
  return {mm, h, h * 0.42f, h * 0.38f, h * 0.2f};
}

float uiWidth(const UiMetrics& k, const char* text) { return textExt(k.fs, text).x + 2.f * k.pad; }

void uiButton(ImDrawList* dl, const UiMetrics& k, int id, float x, float y, float w, const char* text, bool enabled,
              bool on, float a, bool live) {
  const bool down = sUiPress.on && sUiPress.item == id;
  const ImVec2 p0(x, y);
  const ImVec2 p1(x + w, y + k.h);
  const float round = k.h * 0.3f;
  dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, static_cast<int>(150 * a)), round); // (readable over the picture)
  if (on) {
    dl->AddRectFilled(p0, p1, withAlpha(kAccent, 0.35f * a), round);
  } else if (down) {
    dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(70 * a)), round);
  }
  dl->AddRect(p0, p1, on ? withAlpha(kAccent, 0.9f * a) : IM_COL32(255, 255, 255, static_cast<int>((enabled ? 130 : 50) * a)),
              round, 0, 1.5f);
  const ImVec2 e = textExt(k.fs, text);
  dl->AddText(ImGui::GetFont(), k.fs, ImVec2(x + (w - e.x) * 0.5f, y + (k.h - e.y) * 0.5f),
              IM_COL32(255, 255, 255, static_cast<int>((enabled ? 240 : 105) * a)), text);
  if (live) {
    uiAdd(id, x, y, x + w, y + k.h, enabled);
  }
}

struct Btn {
  int id;
  const char* text; // translated
  bool enabled, on;
};

// buttons in rows no wider than maxW, each row centred on cx, from y down;
// the bottom (drawn only with dl); *widest: the widest row's width
float uiFlow(ImDrawList* dl, const UiMetrics& k, const Btn* b, int n, float cx, float y, float maxW, float a, bool live,
             float* widest = nullptr) {
  int i = 0;
  float bottom = y;
  if (widest != nullptr) {
    *widest = 0.f;
  }
  while (i < n) {
    int j = i;
    float w = 0.f;
    while (j < n) {
      const float nw = w + (j > i ? k.gap : 0.f) + uiWidth(k, b[j].text);
      if (j > i && nw > maxW) {
        break;
      }
      w = nw;
      j++;
    }
    if (widest != nullptr) {
      *widest = std::fmax(*widest, w);
    }
    if (dl != nullptr) {
      float x = cx - w * 0.5f;
      for (int t = i; t < j; t++) {
        const float bw = uiWidth(k, b[t].text);
        uiButton(dl, k, b[t].id, x, y, bw, b[t].text, b[t].enabled, b[t].on, a, live);
        x += bw + k.gap;
      }
    }
    bottom = y + k.h;
    y += k.h + k.gap;
    i = j;
  }
  return bottom;
}

const char* controlName(int c) {
  switch (c) {
  case kStick:
    return "Stick";
  case kA:
    return "A, X and Y";
  case kB:
    return "B";
  case kZ:
    return "Z";
  case kL:
    return "L";
  case kR:
    return "R";
  case kStart:
    return "START";
  case kFast:
    return "Fast-forward";
  case kSwap:
    return "Partner swap";
  case kMenu:
    return "Settings";
  case kCStick:
    return "C-stick";
  case kDpad:
    return "D-pad";
  case kPhoto:
    return "Photo mode";
  case kEdit:
    return "Edit layout";
  default:
    return "";
  }
}

// the control's size as the panel shows it: across (a pill: its height), in mm
float sizeMm(const Layout& l, int c) {
  const Circle& b = l.c[c];
  float d = 2.f * b.r;
  if (isPill(c)) {
    float hw = 0.f;
    float hh = 0.f;
    pillSize(c, b, &hw, &hh);
    d = 2.f * hh;
  } else if (c == kStick) {
    d = 2.f * b.r * 1.04f;
  }
  return d / mmPts();
}

void slider(ImDrawList* dl, const UiMetrics& k, int id, float x0, float x1, float y, float t, bool live) {
  const float h = k.h * 0.8f;
  const float cy = y + h * 0.5f;
  const float knob = k.h * 0.3f;
  const float tx0 = x0 + knob;
  const float tx1 = x1 - knob;
  const float kx = tx0 + (tx1 - tx0) * std::clamp(t, 0.f, 1.f);
  const float th = std::fmax(k.h * 0.1f, 3.f);
  dl->AddRectFilled(ImVec2(tx0, cy - th * 0.5f), ImVec2(tx1, cy + th * 0.5f), IM_COL32(255, 255, 255, 60), th);
  dl->AddRectFilled(ImVec2(tx0, cy - th * 0.5f), ImVec2(kx, cy + th * 0.5f), withAlpha(kAccent, 0.9f), th);
  const bool down = sUiPress.on && sUiPress.item == id;
  dl->AddCircleFilled(ImVec2(kx, cy), knob * (down ? 1.15f : 1.f), IM_COL32(245, 248, 255, 255), 24);
  dl->AddCircle(ImVec2(kx, cy), knob * (down ? 1.15f : 1.f), withAlpha(kAccent, 1.f), 24, 2.f);
  if (live) {
    if (UiRect* r = uiAdd(id, x0, y, x1, y + h); r != nullptr) {
      r->t0 = tx0;
      r->t1 = tx1;
    }
  }
}

float overlapArea(const Box& a, const Box& b) {
  const float w = std::fmin(a.x1, b.x1) - std::fmax(a.x0, b.x0);
  const float h = std::fmin(a.y1, b.y1) - std::fmax(a.y0, b.y0);
  return w > 0.f && h > 0.f ? w * h : 0.f;
}

// The panel for the control chosen: beside it toward the middle, else on its
// other side, above or below it, or below (or above) the toolbar; kept on the
// screen, never over the control itself or the layout button (Done), clear of
// the toolbar where it can be (else the toolbar moves away from it), and over
// the fewest controls it can.
void drawPanel(ImDrawList* dl, const Layout& l, const UiMetrics& k, const Box& s, float top, const Box& bar) {
  const int c = sEditSel;
  const Offset& off = sOffset[l.portrait][c];
  char sizeText[96];
  char alphaText[96];
  std::snprintf(sizeText, sizeof(sizeText), "%s   %.1f mm", portTr("Size"), sizeMm(l, c));
  std::snprintf(alphaText, sizeof(alphaText), "%s   %d%%", portTr("Opacity"), static_cast<int>(std::lround(off.alpha * 100.f)));
  Btn btns[3];
  int nb = 0;
  if (c != kMenu && c != kEdit) { // (the settings stay reachable; the editor's button: its setting)
    btns[nb++] = {kUiHide, portTr(off.hidden ? "Show" : "Hide"), true, off.hidden};
  }
  if (hasGroup(c)) {
    btns[nb++] = {kUiAlone, portTr("Move alone"), true, !sGroupMoves};
  }
  btns[nb++] = {kUiResetOne, portTr("Reset"), true, false};
  const float titleH = k.fs * 1.5f;
  const float labelH = k.fs * 1.35f;
  const float sliderH = k.h * 0.8f;
  const Box cb = boxOf(l, c);
  const float space = 2.f * k.gap;
  const bool leftHalf = cb.cx() < s.cx();
  // Its shape: narrow (the sliders one over the other), or, where that fits nowhere clear of the bar (a
  // short screen, the bar at the top), wide and short (the sliders side by side, the buttons in a row).
  const float narrowW = std::fmin(std::clamp(58.f * k.mm, 230.f, 380.f), s.x1 - s.x0);
  const float wideW = std::fmin(1.5f * narrowW, s.x1 - s.x0);
  // the layout editor's own button says Done while it is open: never under the panel either
  const bool editShown = editButtonOn() && c != kEdit;
  const Box eb = editShown ? boxOf(l, kEdit) : Box{0.f, 0.f, 0.f, 0.f};
  // A place's cost: over the control chosen or Done (1e6 and up: the least such), over the toolbar (1000:
  // it moves out of the panel's way), over another control (10 each), moved to stay on the screen (1). The
  // first of the least.
  auto costOf = [&](const Box& pb, bool clamped) {
    const float forbidden = overlapArea(pb, cb) + (editShown ? overlapArea(pb, eb) : 0.f);
    float cost = forbidden > 0.f ? 1e6f + forbidden : 0.f;
    cost += overlapArea(pb, bar) > 0.f ? 1000.f : 0.f;
    for (int o = kStick; o < kControlCount; o++) {
      if (o != c && o != kEdit && !isBean(o) && overlapArea(pb, boxOf(l, o)) > 0.f) {
        cost += 10.f;
      }
    }
    return cost + (clamped ? 1.f : 0.f);
  };
  float pw = narrowW;
  float ph = 0.f;
  float x0 = 0.f;
  float y0 = 0.f;
  bool wide = false;
  float best = 1e30f;
  for (int shape = 0; shape < 2 && best >= 1000.f; shape++) { // (wide only where narrow can't help it)
    const bool w2 = shape == 1;
    const float w = w2 ? wideW : narrowW;
    const float btnsH = uiFlow(nullptr, k, btns, nb, 0.f, 0.f, w - 2.f * k.pad, 1.f, false);
    const float h = w2 ? k.pad + titleH + labelH + sliderH + k.gap + btnsH + k.pad
                       : k.pad + titleH + 2.f * (labelH + sliderH) + k.gap * 2.f + btnsH + k.pad;
    const float toward = leftHalf ? cb.x1 + space : cb.x0 - space - w; // beside it, toward the middle
    const float away = leftHalf ? cb.x0 - space - w : cb.x1 + space;   // beside it, on its other side
    const float mid = cb.cx() - w * 0.5f;
    const float beside = cb.cy() - h * 0.5f;
    // where it can go, best first (each kept on the screen)
    const ImVec2 places[] = {{toward, beside},
                             {away, beside},
                             {mid, cb.y0 - space - h},
                             {mid, cb.y1 + space},
                             {mid, bar.y1 + space},
                             {mid, bar.y0 - space - h},
                             {toward, bar.y1 + space},
                             {away, bar.y1 + space},
                             {bar.x0 - space - w, beside},
                             {bar.x1 + space, beside}};
    for (const ImVec2& p : places) {
      const float x = std::clamp(p.x, s.x0, std::fmax(s.x0, s.x1 - w));
      const float y = std::clamp(p.y, top, std::fmax(top, s.y1 - h));
      const float cost = costOf({x, y, x + w, y + h}, std::fabs(x - p.x) > 0.5f || std::fabs(y - p.y) > 0.5f);
      if (cost < best) {
        best = cost;
        x0 = x;
        y0 = y;
        pw = w;
        ph = h;
        wide = w2;
      }
      if (cost == 0.f) {
        break;
      }
    }
  }
  const float inner = pw - 2.f * k.pad;
  if (std::getenv("TTYD_TOUCH_DEVICE") != nullptr) {
    // (tests: where the panel went, logged when it moves)
    static float sLogged[5];
    const float now[5] = {static_cast<float>(c), x0, y0, bar.x0, bar.y0};
    if (std::memcmp(now, sLogged, sizeof(now)) != 0) {
      std::memcpy(sLogged, now, sizeof(now));
      const Box pb = {x0, y0, x0 + pw, y0 + ph};
      std::fprintf(stderr,
                   "[touch] panel: control %d at %.0f,%.0f-%.0f,%.0f%s; bar %.0f,%.0f-%.0f,%.0f; over the bar: %s; "
                   "over the layout button: %s; over the control: %s\n",
                   c, pb.x0, pb.y0, pb.x1, pb.y1, wide ? " (wide)" : "", bar.x0, bar.y0, bar.x1, bar.y1,
                   overlapArea(pb, bar) > 0.f ? "yes" : "no", editShown && overlapArea(pb, eb) > 0.f ? "yes" : "no",
                   overlapArea(pb, cb) > 0.f ? "yes" : "no");
    }
  }
  const ImVec2 p0(x0, y0);
  const ImVec2 p1(x0 + pw, y0 + ph);
  sPanelBox[0] = p0.x;
  sPanelBox[1] = p0.y;
  sPanelBox[2] = p1.x;
  sPanelBox[3] = p1.y;
  dl->AddRectFilled(ImVec2(p0.x + 2.f, p0.y + 4.f), ImVec2(p1.x + 2.f, p1.y + 4.f), IM_COL32(0, 0, 0, 90), k.h * 0.35f);
  dl->AddRectFilled(p0, p1, IM_COL32(20, 22, 32, 236), k.h * 0.35f);
  dl->AddRect(p0, p1, withAlpha(kAccent, 0.55f), k.h * 0.35f, 0, 1.5f);
  uiAdd(kUiPanel, p0.x, p0.y, p1.x, p1.y);
  float y = y0 + k.pad;
  const float tfs = k.fs * 1.1f;
  dl->AddText(ImGui::GetFont(), tfs, ImVec2(x0 + k.pad, y + (titleH - tfs) * 0.4f), IM_COL32(255, 255, 255, 250),
              portTr(controlName(c)));
  y += titleH;
  // the size's slider, then the opacity's: one over the other, or (wide) side by side
  const float colW = wide ? (inner - space) * 0.5f : inner;
  const float ax = wide ? x0 + k.pad + colW + space : x0 + k.pad;
  dl->AddText(ImGui::GetFont(), k.fs, ImVec2(x0 + k.pad, y + (labelH - k.fs) * 0.5f), IM_COL32(225, 230, 240, 235),
              sizeText);
  y += labelH;
  slider(dl, k, kUiSize, x0 + k.pad, x0 + k.pad + colW, y, (off.scale - kScaleMin) / (kScaleMax - kScaleMin), true);
  if (!wide) {
    y += sliderH + k.gap;
  }
  const float ay = wide ? y - labelH : y;
  dl->AddText(ImGui::GetFont(), k.fs, ImVec2(ax, ay + (labelH - k.fs) * 0.5f), IM_COL32(225, 230, 240, 235),
              alphaText);
  slider(dl, k, kUiAlpha, ax, ax + colW, ay + labelH, (off.alpha - kAlphaMin) / (1.f - kAlphaMin), true);
  y = ay + labelH + sliderH + k.gap;
  uiFlow(dl, k, btns, nb, x0 + pw * 0.5f, y, inner, 1.f, true);
}

// The toolbar: one row of short buttons (Undo, Presets, Reset, Cancel, "?";
// Done too only with the layout button turned off), the presets under it
// while open, and (the first opening, or "?") a one-line hint over it. It
// goes where it covers no control, the middle of the picture first, and stays
// there while that spot is still clear; while a control moves it is hidden.
// Its box (empty while hidden).
Box drawToolbar(ImDrawList* dl, const Layout& l, const UiMetrics& k, const Box& safe, bool busy) {
  if (busy) {
    return {0.f, 0.f, 0.f, 0.f}; // (out of the way while a control moves)
  }
  const bool pic = l.area.picW > 0.f && l.area.picH > 0.f;
  // (where it may go: the controls' area and the picture above it, upright the picture's being clear)
  const Box s = {safe.x0, pic ? std::fmin(safe.y0, l.area.picY) : safe.y0, safe.x1, safe.y1};
  const float cx = pic ? l.area.picX + l.area.picW * 0.5f : s.cx();
  const float cy = pic ? l.area.picY + l.area.picH * 0.5f : s.cy();
  const float maxW = std::fmax(s.x1 - s.x0 - 2.f * k.gap, 100.f);
  Btn row[6];
  int nr = 0;
  row[nr++] = {kUiUndo, portTr("Undo"), !sUndo.empty(), false};
  row[nr++] = {kUiPresets, portTr("Presets"), true, sPresetsOpen};
  row[nr++] = {kUiReset, portTr("Reset"), true, false};
  row[nr++] = {kUiCancel, portTr("Cancel"), true, false};
  if (!editButtonOn()) {
    row[nr++] = {kUiDone, portTr("Done"), true, true}; // (no layout button to say Done)
  }
  row[nr++] = {kUiHelp, "?", true, sHintOn};
  int ids[3];
  const int np = presetsFor(l.portrait, ids);
  Btn presets[3];
  for (int i = 0; i < np; i++) {
    presets[i] = {kUiPreset0 + ids[i], portTr(kPresetNames[ids[i]]), true, false};
  }
  const char* hint = portTr("Drag to move, pinch to resize");
  const float hfs = k.fs * 0.9f;
  const ImVec2 he = textExt(hfs, hint);
  const float hintH = sHintOn ? he.y + k.pad * 0.6f + k.gap : 0.f;
  const float hintW = sHintOn ? he.x + 2.f * k.pad : 0.f;
  float rowW = 0.f;
  float presetsW = 0.f;
  const float rowH = uiFlow(nullptr, k, row, nr, 0.f, 0.f, maxW, 1.f, false, &rowW);
  const float presetsH =
      sPresetsOpen ? k.gap + uiFlow(nullptr, k, presets, np, 0.f, 0.f, maxW, 1.f, false, &presetsW) : 0.f;
  const float w = std::fmin(std::fmax(rowW, std::fmax(presetsW, hintW)), s.x1 - s.x0);
  const float h = std::fmin(hintH + rowH + presetsH, s.y1 - s.y0);
  // A spot's cost: the controls' area it covers (each a little larger: a finger's slack), and the panel's.
  const float slack = 1.5f * k.mm;
  Box cover[kControlCount + 1];
  int nc = 0;
  for (int c = kStick; c < kControlCount; c++) {
    if (isBean(c) || (c == kEdit && !editButtonOn())) {
      continue; // (X and Y: A's box has them)
    }
    const Box b = boxOf(l, c);
    cover[nc++] = {b.x0 - slack, b.y0 - slack, b.x1 + slack, b.y1 + slack};
  }
  if (sPanelBox[2] > sPanelBox[0]) {
    cover[nc++] = {sPanelBox[0] - k.gap, sPanelBox[1] - k.gap, sPanelBox[2] + k.gap, sPanelBox[3] + k.gap};
  }
  const int panelSlot = sPanelBox[2] > sPanelBox[0] ? nc - 1 : -1;
  auto covered = [&](float x, float y) {
    const Box tb = {x, y, x + w, y + h};
    float area = 0.f;
    for (int i = 0; i < nc; i++) {
      // (under the panel its buttons can't be reached at all; over a control they lose only to it)
      area += overlapArea(tb, cover[i]) * (i == panelSlot ? 8.f : 1.f);
    }
    return area;
  };
  // (the controls and the area as they were when it was placed: nothing changed, it stays put)
  float sig = s.x0 + 3.f * s.y0 + 5.f * s.x1 + 7.f * s.y1;
  for (int i = 0; i < nc; i++) {
    sig += static_cast<float>(i + 1) * (cover[i].x0 + 2.f * cover[i].y0 + 3.f * cover[i].x1 + 4.f * cover[i].y1);
  }
  static float sX = -1.f;
  static float sY = -1.f;
  static float sW = 0.f;
  static float sH = 0.f;
  static float sSig = 0.f;
  const float xHi = std::fmax(s.x0, s.x1 - w);
  const float yHi = std::fmax(s.y0, s.y1 - h);
  const bool kept = sX >= s.x0 - 0.5f && sX <= xHi + 0.5f && sY >= s.y0 - 0.5f && sY <= yHi + 0.5f &&
                    std::fabs(sW - w) < 0.5f && std::fabs(sH - h) < 0.5f && (sig == sSig || covered(sX, sY) <= 0.f);
  sSig = sig;
  if (!kept) {
    // the clear spot nearest the middle of the picture (none clear: the one covering least)
    const float step = std::fmax(k.h * 0.5f, 8.f);
    float bestCost = 1e30f;
    float bestD = 1e30f;
    float bx = std::clamp(cx - w * 0.5f, s.x0, xHi);
    float by = std::clamp(cy - h * 0.5f, s.y0, yHi);
    // (the grid's first line through the middle, so the middle itself is tried)
    const float gx0 = bx - std::floor((bx - s.x0) / step) * step;
    const float gy0 = by - std::floor((by - s.y0) / step) * step;
    for (float y = gy0; y <= yHi + 0.5f; y += step) {
      for (float x = gx0; x <= xHi + 0.5f; x += step) {
        const float cost = covered(x, y);
        const float d = std::hypot(x + w * 0.5f - cx, y + h * 0.5f - cy);
        if (cost < bestCost - 1.f || (cost <= bestCost + 1.f && d < bestD)) {
          bestCost = std::fmin(cost, bestCost);
          bestD = d;
          bx = x;
          by = y;
        }
      }
    }
    sX = bx;
    sY = by;
    sW = w;
    sH = h;
    if (std::getenv("TTYD_TOUCH_DEVICE") != nullptr) {
      // (tests: where it went, the controls it covers (none, wherever a clear spot is), its longest text)
      int over = 0;
      for (int c = kStick; c < kControlCount; c++) {
        if (!isBean(c) && !(c == kEdit && !editButtonOn()) && overlapArea({bx, by, bx + w, by + h}, boxOf(l, c)) > 0.f) {
          over++;
        }
      }
      size_t longest = sHintOn ? std::strlen(hint) : 0;
      for (int i = 0; i < nr; i++) {
        longest = std::max(longest, std::strlen(row[i].text));
      }
      std::fprintf(stderr, "[touch] toolbar: at %.0f,%.0f-%.0f,%.0f; over controls: %d; longest text: %d chars\n", bx,
                   by, bx + w, by + h, over, static_cast<int>(longest));
    }
  }
  const float x0 = sX;
  float y = sY;
  if (sHintOn) {
    const float hx = x0 + (w - hintW) * 0.5f;
    dl->AddRectFilled(ImVec2(hx, y), ImVec2(hx + hintW, y + he.y + k.pad * 0.6f), IM_COL32(0, 0, 0, 160), k.h * 0.25f);
    dl->AddText(ImGui::GetFont(), hfs, ImVec2(hx + k.pad, y + k.pad * 0.3f), IM_COL32(255, 255, 255, 235), hint);
    y += hintH; // (the hint is only drawn: a finger there is a control's, or nothing)
  }
  y = uiFlow(dl, k, row, nr, x0 + w * 0.5f, y, maxW, 1.f, true);
  if (sPresetsOpen) {
    uiFlow(dl, k, presets, np, x0 + w * 0.5f, y + k.gap, maxW, 1.f, true);
  }
  return {x0, sY, x0 + w, sY + h};
}

// Under the controls: the screen dimmed (the game is paused), the grid while
// a control moves, the lifted ones' shadows.
void drawEditUnder(ImDrawList* dl, const Layout& l, float ww, float wh) {
  dl->AddRectFilled(ImVec2(0.f, 0.f), ImVec2(ww, wh), IM_COL32(0, 0, 0, 80));
  const Box s = safeBox(l);
  if (editBusy() && sDrag.moving && !sDrag.free) {
    const float g = 4.f * mmPts();
    for (float x = s.x0; x <= s.x1 + 0.5f; x += g) {
      dl->AddLine(ImVec2(x, s.y0), ImVec2(x, s.y1), IM_COL32(255, 255, 255, 22), 1.f);
    }
    for (float y = s.y0; y <= s.y1 + 0.5f; y += g) {
      dl->AddLine(ImVec2(s.x0, y), ImVec2(s.x1, y), IM_COL32(255, 255, 255, 22), 1.f);
    }
  }
  // the area the controls stay in
  dl->AddRect(ImVec2(s.x0, s.y0), ImVec2(s.x1, s.y1), IM_COL32(255, 255, 255, 40), 6.f, 0, 1.f);
  if (sDrag.on && (sDrag.moving || sPinch.on)) {
    const float lift = 1.2f * mmPts();
    for (int i = 0; i < sDrag.n; i++) {
      const Box b = boxOf(l, sDrag.m[i]);
      const float r = std::fmin(b.x1 - b.x0, b.y1 - b.y0) * 0.5f;
      dl->AddRectFilled(ImVec2(b.x0 + lift * 0.5f, b.y0 + lift), ImVec2(b.x1 + lift * 0.5f, b.y1 + lift),
                        IM_COL32(0, 0, 0, 80), r);
    }
  }
}

// Over the controls: the hidden ones struck through, the chosen one's (and
// its group's) outline, the snap's guide lines, the size while pinched, the
// bar and the panel.
void drawEditOver(ImDrawList* dl, const Layout& l) {
  sUiCount = 0;
  const UiMetrics k = uiMetrics();
  const Box s = safeBox(l);
  const Offset* off = sOffset[l.portrait];
  for (int c = kStick; c < kControlCount; c++) {
    if (!isBean(c) && off[c].hidden) {
      const Box b = boxOf(l, c);
      dl->AddLine(ImVec2(b.x0, b.y1), ImVec2(b.x1, b.y0), IM_COL32(255, 120, 120, 220), 3.f);
    }
  }
  if (sEditSel >= 0) {
    int m[kMaxGroup];
    float sx[kMaxGroup];
    const int n = sDrag.on ? sDrag.n : groupOf(sEditSel, m, sx);
    const bool free = sDrag.on && (sDrag.moving ? sDrag.free : SDL_GetTicks() - sDrag.at >= kFreeHoldMs);
    const ImU32 accent = free ? kFreeAccent : kAccent;
    for (int i = 0; i < n; i++) {
      const int c = sDrag.on ? sDrag.m[i] : m[i];
      const Box b = boxOf(l, c);
      const float p = 0.8f * k.mm;
      dl->AddRect(ImVec2(b.x0 - p, b.y0 - p), ImVec2(b.x1 + p, b.y1 + p), withAlpha(accent, i == 0 ? 0.95f : 0.45f),
                  6.f, 0, i == 0 ? 2.5f : 1.5f);
    }
  }
  const bool busy = editBusy();
  if (busy && sDrag.moving) { // (the snap's lines while it moves, not while a thumb rests on it)
    if (sDrag.gx) {
      dl->AddLine(ImVec2(sDrag.guideX, s.y0), ImVec2(sDrag.guideX, s.y1), withAlpha(kAccent, 0.8f), 1.5f);
    }
    if (sDrag.gy) {
      dl->AddLine(ImVec2(s.x0, sDrag.guideY), ImVec2(s.x1, sDrag.guideY), withAlpha(kAccent, 0.8f), 1.5f);
    }
  }
  if (sDrag.on && (sPinch.on || sDrag.moving)) {
    // the size (pinched) or nothing (moved) over the control held
    if (sPinch.on) {
      char text[32];
      std::snprintf(text, sizeof(text), "%.1f mm", sizeMm(l, sDrag.m[0]));
      const Box b = boxOf(l, sDrag.m[0]);
      const ImVec2 e = textExt(k.fs, text);
      const ImVec2 at(b.cx() - e.x * 0.5f, std::fmax(b.y0 - e.y - k.gap * 2.f, 0.f));
      dl->AddRectFilled(ImVec2(at.x - 6.f, at.y - 3.f), ImVec2(at.x + e.x + 6.f, at.y + e.y + 3.f), IM_COL32(0, 0, 0, 170), 6.f);
      dl->AddText(ImGui::GetFont(), k.fs, at, IM_COL32(255, 255, 255, 245), text);
    }
  }
  const bool pic = l.area.picW > 0.f && l.area.picH > 0.f;
  const float top = pic ? std::fmin(s.y0, l.area.picY) : s.y0;
  const Box bar = drawToolbar(dl, l, k, s, busy);
  if (sEditSel >= 0 && !busy && !sDrag.on) { // (once the tap is over: not under a finger that may yet drag)
    drawPanel(dl, l, k, s, top, bar);
  } else if (!busy) {
    sPanelBox[0] = sPanelBox[2] = 0.f; // (none: while a control moves, the toolbar's spot is kept as it was)
  }
}

// The first time's note (touch_move_hint): a moment after the controls first
// show, at the top of the picture for a few seconds, fading out; a tap on it
// opens the layout editor. Seen once (saved as soon as it shows).
void drawMoveHint(ImDrawList* dl, const Layout& l, unsigned frame) {
  sHintRect[0] = sHintRect[2] = 0.f;
  if (sMoveHint == nullptr || sHintAt == 1 || (sHintAt == 0 && *sMoveHint != 0)) {
    return;
  }
  const unsigned now = frame;
  static unsigned sFirstDrawn;
  if (sFirstDrawn == 0) {
    sFirstDrawn = now;
  }
  if (sHintAt == 0) {
    if (now - sFirstDrawn < kHintDelay) {
      return;
    }
    sHintAt = now;
    *sMoveHint = 1;
    portSettingsSave();
    std::fprintf(stderr, "[touch] the note about moving the controls\n");
  }
  const unsigned age = now - sHintAt;
  if (age >= kHintShow) {
    sHintAt = 1;
    return;
  }
  // (fading in over a quarter second, out over the last second)
  const float a = std::fmin(std::fmin(static_cast<float>(age) / 15.f, 1.f), static_cast<float>(kHintShow - age) / 60.f);
  const char* text = portTr("Tap here, or hold the menu button, to move and resize the on-screen controls");
  const bool pic = l.area.picW > 0.f && l.area.picH > 0.f;
  const float cx = pic ? l.area.picX + l.area.picW * 0.5f : l.area.ctrlX + l.area.ctrlW * 0.5f;
  const float top = pic ? l.area.picY + l.area.picH * 0.22f : l.area.ctrlY + l.area.ctrlH * 0.3f;
  const float size = std::fmax(0.045f * l.u, 15.f);
  const float wrap = std::fmax((pic ? l.area.picW : l.area.ctrlW) * 0.6f, 200.f);
  const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, wrap, text);
  const float padX = size * 0.8f;
  const float padY = size * 0.5f;
  const ImVec2 p0(cx - ext.x * 0.5f - padX, top);
  const ImVec2 p1(cx + ext.x * 0.5f + padX, top + ext.y + 2.f * padY);
  dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, static_cast<int>(170.f * a)), size * 0.5f);
  dl->AddRect(p0, p1, IM_COL32(255, 255, 255, static_cast<int>(150.f * a)), size * 0.5f, 0, 2.f);
  dl->AddText(ImGui::GetFont(), size, ImVec2(cx - ext.x * 0.5f, top + padY),
              IM_COL32(255, 255, 255, static_cast<int>(240.f * a)), text, nullptr, wrap);
  sHintRect[0] = p0.x;
  sHintRect[1] = p0.y;
  sHintRect[2] = p1.x;
  sHintRect[3] = p1.y;
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

// $TTYD_TOUCH_EDIT_DO=<frame>:<step>[;...] (tests, with the editor open): a
// step on that drawn frame: select=<control>, preset=<n> (0 Default, 1
// Handheld, 2 Phone upright, 3 Left-handed), presets (its row), undo, reset, hide, alone, done,
// cancel, or drag=<control>,<dx mm>,<dy mm>,<frames> (a finger from the
// control's middle, moved that far over that many frames, then lifted), or
// drag=<control>,done,<frames> (to the middle of the layout button, Done).
struct {
  bool on;
  unsigned start, frames;
  float x0, y0, x1, y1;
} sTestDrag;

struct TestFinger {
  bool on;
  SDL_FingerID id;
  unsigned at;
  float x, y;   // (window fractions)
  float x1, y1; // rest=: where it creeps to (over kCreepFrames), then stays
};
constexpr unsigned kCreepFrames = 8;
TestFinger sTestUps[4]; // taps' releases due
TestFinger sTestRest;   // a finger left resting (rest=, lift)

void runEditSteps(unsigned frame) {
  static const char* steps = std::getenv("TTYD_TOUCH_EDIT_DO");
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(window(), &ww, &wh);
  constexpr SDL_FingerID kTestDragId = 2000;
  if (sTestDrag.on && ww > 0 && wh > 0 && frame > sTestDrag.start) {
    const unsigned t = frame - sTestDrag.start;
    const float f = std::fmin(static_cast<float>(t) / static_cast<float>(sTestDrag.frames), 1.f);
    const float x = sTestDrag.x0 + (sTestDrag.x1 - sTestDrag.x0) * f;
    const float y = sTestDrag.y0 + (sTestDrag.y1 - sTestDrag.y0) * f;
    // (the last move to the very end, then up there: a lift alone doesn't move a control)
    pushFinger(SDL_EVENT_FINGER_MOTION, kTestDragId, x / static_cast<float>(ww), y / static_cast<float>(wh));
    if (t >= sTestDrag.frames) {
      pushFinger(SDL_EVENT_FINGER_UP, kTestDragId, x / static_cast<float>(ww), y / static_cast<float>(wh));
    }
    sTestDrag.on = t < sTestDrag.frames;
  }
  // the taps' releases, the frame after
  for (auto& u : sTestUps) {
    if (u.on && frame > u.at) {
      u.on = false;
      pushFinger(SDL_EVENT_FINGER_UP, u.id, u.x, u.y);
    }
  }
  // the resting finger's creep
  if (sTestRest.on && frame > sTestRest.at && frame <= sTestRest.at + kCreepFrames &&
      (sTestRest.x1 != sTestRest.x || sTestRest.y1 != sTestRest.y)) {
    const float t = static_cast<float>(frame - sTestRest.at) / static_cast<float>(kCreepFrames);
    pushFinger(SDL_EVENT_FINGER_MOTION, sTestRest.id, sTestRest.x + (sTestRest.x1 - sTestRest.x) * t,
               sTestRest.y + (sTestRest.y1 - sTestRest.y) * t);
  }
  if (steps == nullptr) {
    return;
  }
  auto tapAt = [&](SDL_FingerID id, float x, float y, bool up) {
    if (ww <= 0 || wh <= 0) {
      return;
    }
    const float nx = x / static_cast<float>(ww);
    const float ny = y / static_cast<float>(wh);
    pushFinger(SDL_EVENT_FINGER_DOWN, id, nx, ny);
    for (auto& u : sTestUps) {
      if (up && !u.on) {
        u = {true, id, frame, nx, ny};
        break;
      }
    }
  };
  static const struct {
    const char* word;
    int item;
  } kWords[] = {{"undo", kUiUndo},   {"reset", kUiReset}, {"hide", kUiHide},       {"alone", kUiAlone},
                {"done", kUiDone},   {"cancel", kUiCancel}, {"presets", kUiPresets}, {"help", kUiHelp}};
  for (const char* p = steps; p != nullptr && *p != '\0';) {
    unsigned at = 0;
    char word[64] = {};
    Layout l;
    if (std::sscanf(p, "%u:%63[^;]", &at, word) == 2 && at == frame && computeLayout(l)) {
      int c = 0;
      char name[32] = {};
      // touches, in the editor or not: tap=<control> (a finger down on its middle, up the frame after),
      // rest=<control> (a finger left down there: a thumb resting on it) and lift (that finger up),
      // ui=<undo|reset|hide|alone|done|cancel|presets|help> (a tap on that button of the toolbar or the panel;
      // done: the layout button's, while it is shown)
      if (std::sscanf(word, "tap=%d", &c) == 1 && c >= 0 && c < kControlCount) {
        std::fprintf(stderr, "[touch] test: %s (frame %u)\n", word, frame);
        tapAt(2003, l.c[c].x, l.c[c].y, true);
      } else if (std::strcmp(word, "key=esc") == 0 || std::strcmp(word, "key=enter") == 0 ||
                 std::strcmp(word, "pad=b") == 0 || std::strcmp(word, "pad=start") == 0) {
        // no touch: a key or a controller's button, as SDL's event (the editor holds the game, so the device
        // flows' frames stop: these go by drawn frame). A controller with no SDL pad behind it: an Xbox-style
        // pad's places (back is the right face button)
        std::fprintf(stderr, "[touch] test: %s (frame %u)\n", word, frame);
        SDL_Event e{};
        if (word[0] == 'k') {
          e.type = SDL_EVENT_KEY_DOWN;
          e.key.key = word[4] == 'e' && word[5] == 's' ? SDLK_ESCAPE : SDLK_RETURN;
          e.key.down = true;
        } else {
          e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
          e.gbutton.button = word[4] == 'b' ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_START;
          e.gbutton.down = true;
        }
        SDL_PushEvent(&e);
      } else if (float cx = 0.f, cy = 0.f; std::sscanf(word, "rest=%d,%f,%f", &c, &cx, &cy) >= 1 && c >= 0 &&
                                           c < kControlCount && ww > 0 && wh > 0) {
        // (rest=<control>[,<dx mm>,<dy mm>]: crept that far over a few frames, then still)
        std::fprintf(stderr, "[touch] test: %s (frame %u)\n", word, frame);
        const float mm = mmPts();
        const float fw = static_cast<float>(ww);
        const float fh = static_cast<float>(wh);
        sTestRest = {true, 2001, frame, l.c[c].x / fw, l.c[c].y / fh, (l.c[c].x + cx * mm) / fw,
                     (l.c[c].y + cy * mm) / fh};
        tapAt(2001, l.c[c].x, l.c[c].y, false);
      } else if (std::strcmp(word, "lift") == 0 && sTestRest.on) {
        std::fprintf(stderr, "[touch] test: lift (frame %u)\n", frame);
        sTestRest.on = false;
        pushFinger(SDL_EVENT_FINGER_UP, sTestRest.id, sTestRest.x1, sTestRest.y1);
      } else if (std::sscanf(word, "ui=%31s", name) == 1) {
        const UiRect* r = nullptr;
        for (const auto& w : kWords) {
          r = r == nullptr && std::strcmp(name, w.word) == 0 ? uiFind(w.item) : r;
        }
        const bool editDone = r == nullptr && sEditing && std::strcmp(name, "done") == 0 && editButtonOn();
        std::fprintf(stderr, "[touch] test: %s%s (frame %u)%s\n", word, !editDone && r == nullptr ? " (not shown)" : "",
                     frame, editDone ? ": the layout button" : "");
        if (editDone) {
          tapAt(2002, l.c[kEdit].x, l.c[kEdit].y, true);
        } else if (r != nullptr) {
          tapAt(2002, 0.5f * (r->x0 + r->x1), 0.5f * (r->y0 + r->y1), true);
        }
      } else if (sEditing) {
        std::fprintf(stderr, "[touch] test: editor %s (frame %u)\n", word, frame);
        int n = 0;
        float dx = 0.f;
        float dy = 0.f;
        if (std::sscanf(word, "select=%d", &c) == 1 && c >= 0 && c < kControlCount) {
          sEditSel = isBean(c) ? kA : c;
        } else if (std::sscanf(word, "preset=%d", &n) == 1 && n >= 0 && n < kPresetCount) {
          uiAct(l, kUiPreset0 + n);
        } else if (std::sscanf(word, "drag=%d,done,%d", &c, &n) == 2 && c >= 0 && c < kControlCount && n > 0 &&
                   ww > 0 && wh > 0) {
          // (drag=<control>,done,<frames>: its middle to the layout button's, Done's)
          sTestDrag = {true, frame, static_cast<unsigned>(n), l.c[c].x, l.c[c].y, l.c[kEdit].x, l.c[kEdit].y};
          pushFinger(SDL_EVENT_FINGER_DOWN, kTestDragId, l.c[c].x / static_cast<float>(ww),
                     l.c[c].y / static_cast<float>(wh));
        } else if (std::sscanf(word, "drag=%d,%f,%f,%d", &c, &dx, &dy, &n) == 4 && c >= 0 && c < kControlCount &&
                   n > 0 && ww > 0 && wh > 0) {
          const float mm = mmPts();
          sTestDrag = {true, frame, static_cast<unsigned>(n), l.c[c].x, l.c[c].y, l.c[c].x + dx * mm,
                       l.c[c].y + dy * mm};
          pushFinger(SDL_EVENT_FINGER_DOWN, kTestDragId, l.c[c].x / static_cast<float>(ww),
                     l.c[c].y / static_cast<float>(wh));
        } else {
          for (const auto& w : kWords) {
            if (std::strcmp(word, w.word) == 0) {
              uiAct(l, w.item);
            }
          }
        }
      }
    }
    p = std::strchr(p, ';');
    if (p != nullptr) {
      p++;
    }
  }
}

void runTouchTest(unsigned frame) {
  static const char* script = std::getenv("TTYD_TOUCH_TEST");
  // $TTYD_TOUCH_EDIT=<frame>[-<frame>]: the layout editor opened (and closed
  // with Done) on those drawn frames
  static const char* edit = std::getenv("TTYD_TOUCH_EDIT");
  if (edit != nullptr && frame == static_cast<unsigned>(std::atoi(edit))) {
    std::fprintf(stderr, "[touch] test: layout editor (frame %u)\n", frame);
    portTouchEditLayout();
  }
  if (const char* close = edit != nullptr ? std::strchr(edit, '-') : nullptr;
      close != nullptr && sEditing && frame == static_cast<unsigned>(std::atoi(close + 1))) {
    Layout l;
    if (computeLayout(l)) {
      uiAct(l, kUiDone);
    }
  }
  runEditSteps(frame);
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
  if (FILE* f = portFopen(path.c_str(), "w"); f != nullptr) {
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
    if (FILE* f = portFopen((std::string(dir) + "/touch_atlas.bin").c_str(), "wb"); f != nullptr) {
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
  sMoveHint = portSettingsDeclare("touch_move_hint", 0, 0, 1);
  sEditButton = portSettingsDeclare("touch_edit_button", 1, 0, 1);
  sEditHint = portSettingsDeclare("touch_edit_hint", 0, 0, 1);
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
  portSettingsAddToggle("Controls", "Layout button",
                        "A button beside START that moves and resizes the on-screen controls, over the paused "
                        "game; tap it again (it says Done meanwhile) to keep the new layout.",
                        0, "touch_edit_button", "touch on-screen layout edit move resize controls");
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
  if (sEditing) {
    return;
  }
  portTouchReleaseAll();
  clearStatus();
  sGamepadInUse = false;
  if (!sOffsetsRead) {
    readOffsets();
  }
  Layout l;
  computeLayout(l); // (an older touch_layout.txt read and turned into this one's now, so Cancel has it so)
  std::memcpy(sEditOrig, sOffset, sizeof(sOffset));
  sEditing = true;
  sEditSel = -1;
  sDrag = {};
  sPinch.on = false;
  sUiPress.on = false;
  sPresetsOpen = false;
  sUndo.clear();
  // the one-line hint: by itself only the first time ("?" brings it back)
  sHintOn = sEditHint == nullptr || *sEditHint == 0;
  if (sEditHint != nullptr && *sEditHint == 0) {
    *sEditHint = 1;
    portSettingsSave();
  }
  sEditWasHeld = portFrameAdvanceHeld();
  // the game paused under the editor (as photo mode's: no frame advance note); online it plays on (a held game
  // would hold the other players' session), the controls only kept from it
  if (!portStateSyncOn()) {
    portFrameAdvanceHold(PORT_FRAME_HOLD_FEATURE);
  }
  PADBlockInput(true);
  std::fprintf(stderr, "[touch] layout editor open\n");
}

extern "C" void portTouchEditRequest(void) {
  sEditAsked = 1;
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
    // (decided at the next draw: only player 1's controller takes the controls over, a handheld's built-in
    // controls or the first controller paired; another player's, one that joined with Start, leaves them)
    if (!sEditing) {
      sTakeOver = event->gbutton.which;
    } else if (portMenuEventRole(event) == PORT_MENU_BACK) {
      finishEditing(false); // the editor with a controller (no touchscreen): back is Cancel, Start is Done
    } else if (event->gbutton.button == SDL_GAMEPAD_BUTTON_START) {
      finishEditing(true);
    }
    return;
  case SDL_EVENT_KEY_DOWN:
    // and with the keyboard: Esc (or Android's back) is Cancel, Enter is Done
    if (sEditing && !event->key.repeat) {
      if (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK) {
        finishEditing(false);
      } else if (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER) {
        finishEditing(true);
      }
    }
    return;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (!sEditing && (event->gaxis.value > kGamepadDeadZone || event->gaxis.value < -kGamepadDeadZone)) {
      sTakeOver = event->gaxis.which;
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
    // the first time's note: a tap on it opens the layout editor
    if (sHintRect[2] > sHintRect[0] && px >= sHintRect[0] && px < sHintRect[2] && py >= sHintRect[1] &&
        py < sHintRect[3]) {
      sHintRect[0] = sHintRect[2] = 0.f;
      sHintAt = 1; // (gone)
      portTouchEditLayout();
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
      // the layout editor's button: the editor, on the tap's release (so this finger isn't the editor's)
      const bool edit = f->control == kEdit && !f->consumed && event->type == SDL_EVENT_FINGER_UP;
      release(*f);
      if (edit) {
        apply();
        std::fprintf(stderr, "[touch] the layout editor's button\n");
        portTouchEditLayout();
        return;
      }
    }
  }
  apply();
}

extern "C" void portTouchDraw(void) {
  unsigned& sFrame = sDrawn;
  runTouchTest(++sFrame);
  // the Start menu's row (portTouchEditRequest): once its menu has closed
  if (sEditAsked != 0 && ++sEditAsked > 20) {
    sEditAsked = 0;
    portTouchEditLayout();
  }
  decideTakeOver();
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
  // the menu button held: the layout editor (a ring fills meanwhile)
  constexpr Uint64 kMenuHoldMs = 700;
  float menuHold = 0.f;
  for (Finger& f : sFingers) {
    if (f.used && f.control == kMenu && !f.consumed && !sEditing) {
      const Uint64 held = now - f.at;
      if (held >= kMenuHoldMs) {
        f.consumed = true; // (its release opens nothing)
        haptic();
        portTouchEditLayout();
        if (!computeLayout(l)) {
          return;
        }
        break;
      }
      menuHold = static_cast<float>(held) / static_cast<float>(kMenuHoldMs);
    }
  }
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
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (sEditing) {
    // nothing pressed; the ones held lifted (a little larger) over their shadows
    for (bool& b : lit) {
      b = false;
    }
    dpadLit = 0;
    int ww = 0;
    int wh = 0;
    SDL_GetWindowSize(window(), &ww, &wh);
    drawEditUnder(dl, l, static_cast<float>(ww), static_cast<float>(wh));
    if (sDrag.on && (sDrag.moving || sPinch.on)) {
      for (int i = 0; i < sDrag.n; i++) {
        l.c[sDrag.m[i]].r *= 1.06f;
      }
      placeBeans(l);
    }
  }
  for (int c = kA; c < kControlCount; c++) {
    if (sFade[c] <= 0.f) {
      continue;
    }
    const Offset& off = offsetOf(c);
    sAlpha = sFade[c] * (sEditing && off.hidden ? 0.35f : off.alpha);
    const Circle& b = l.c[c];
    if (isBean(c)) {
      drawBean(dl, c, l.bean[c], b, kButtons[c].label, lit[c], tint(c));
    } else if (gcStyle() && isPill(c)) {
      drawPill(dl, c, b, kButtons[c].label, lit[c], tint(c));
    } else if (c == kCStick) {
      drawCStick(dl, b, sEditing ? 0.f : sCX01, sEditing ? 0.f : sCY01, lit[c]);
    } else if (c == kDpad) {
      drawDpad(dl, b, dpadLit, lit[c]);
    } else if (c == kEdit && sEditing) {
      // the editor's Done, in the same spot (a tap: saved and closed; a drag still moves it)
      const bool down = sDrag.on && sDrag.onDone && !sDrag.moving && !sPinch.on;
      drawCircle(dl, c, b, nullptr, down, Tint{110, 205, 255});
      const char* done = portTr("Done");
      const float w = std::fmax(textExt(1.f, done).x, 0.01f);
      label(dl, b.x, b.y, std::fmin(b.r * 0.55f, 1.6f * b.r / w), done, white(240));
    } else {
      drawCircle(dl, c, b, kButtons[c].label, lit[c], tint(c));
      drawIcon(dl, c, b);
    }
    if (c == kMenu && menuHold > 0.15f) {
      const float t = (menuHold - 0.15f) / 0.85f;
      dl->PathArcTo(ImVec2(b.x, b.y), b.r * 1.18f, -kPi * 0.5f, -kPi * 0.5f + 2.f * kPi * t, 32);
      dl->PathStroke(white(230), 0, 3.f);
    }
  }
  const Offset& stickOff = offsetOf(kStick);
  sAlpha = sEditing ? (stickOff.hidden ? 0.35f : stickOff.alpha) : stickOff.hidden ? 0.f : stickOff.alpha;
  if (sEditing) {
    drawStick(dl, l.c[kStick], 0.f, 0.f, false);
    sAlpha = 1.f;
    drawEditOver(dl, l);
  } else {
    Circle base = l.c[kStick];
    if (sStickActive) {
      base.x = sStickOriginX;
      base.y = sStickOriginY;
    }
    if (sAlpha > 0.f) {
      drawStick(dl, base, sStickX01, sStickY01, sStickActive);
    }
    sAlpha = 1.f;
    drawMoveHint(dl, l, sFrame);
  }
  shotFrame(sFrame, dl);
}

// player 1 plays on the screen now (no controller of theirs took the controls over): second_player.cpp's
// Choose player 2's controller may then pick port 0's controller for player 2. Nothing moves a controller to
// another seat by itself: a controller pressed is player 1's (the controls hide for it)
extern "C" int portTouchPlayerOne(void) {
  return sTouchPort == 0 && !sGamepadInUse && !sEditing ? 1 : 0;
}

// a seat swap took the screen's player to another seat: the touches are that port's now
extern "C" void portTouchSetPort(int port) {
  if (port < 0 || port >= PAD_CHANMAX || static_cast<u32>(port) == sTouchPort) {
    return;
  }
  clearStatus(); // (nothing left held on the old port)
  sTouchPort = static_cast<u32>(port);
  sGamepadInUse = PADGetIndexForPort(sTouchPort) >= 0; // (a controller there already: it plays, hidden)
}

// --- tests: the device flows (port/src/common/flows_pc.c's "dev touch") ---

// the controls are drawn now (no controller took them over, or the layout editor is up)
extern "C" int portTouchTestShown(void) { return !sGamepadInUse || sEditing ? 1 : 0; }

extern "C" int portTouchTestEditing(void) { return sEditing ? 1 : 0; }

// a finger on control NAME (a b x y z l r start menu swap fast photo dpad cstick, or stick-left, stick-right,
// stick-up, stick-down: on the stick, then pushed that way) through SDL's event queue, as a real touch:
// down 1 puts it there (the stick's: moved off its middle too), down 0 lifts it. 0 when there's no such control.
extern "C" int portTouchTestControl(const char* name, int down) {
  static const struct {
    const char* name;
    int control;
    float dx, dy;
  } kNames[] = {{"a", kA, 0, 0},          {"b", kB, 0, 0},          {"x", kX, 0, 0},
                {"y", kY, 0, 0},          {"z", kZ, 0, 0},          {"l", kL, 0, 0},
                {"r", kR, 0, 0},          {"start", kStart, 0, 0},  {"menu", kMenu, 0, 0},
                {"swap", kSwap, 0, 0},    {"fast", kFast, 0, 0},    {"photo", kPhoto, 0, 0},
                {"dpad", kDpad, 0, 0},    {"cstick", kCStick, 0, 0}, {"stick-left", kStick, -1, 0},
                {"stick-right", kStick, 1, 0}, {"stick-up", kStick, 0, -1}, {"stick-down", kStick, 0, 1}};
  constexpr SDL_FingerID kId = 3000;
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(window(), &ww, &wh);
  Layout l;
  if (ww <= 0 || wh <= 0 || !computeLayout(l)) {
    return 0;
  }
  for (const auto& n : kNames) {
    if (std::strcmp(n.name, name) != 0) {
      continue;
    }
    const Circle& c = l.c[n.control];
    const float x = c.x / static_cast<float>(ww), y = c.y / static_cast<float>(wh);
    const float mx = x + n.dx * c.r / static_cast<float>(ww), my = y + n.dy * c.r / static_cast<float>(wh);
    if (!down) {
      pushFinger(SDL_EVENT_FINGER_UP, kId, mx, my);
      return 1;
    }
    pushFinger(SDL_EVENT_FINGER_DOWN, kId, x, y);
    if (n.dx != 0.f || n.dy != 0.f) {
      pushFinger(SDL_EVENT_FINGER_MOTION, kId, mx, my);
    }
    return 1;
  }
  return 0;
}

// a finger at window fraction x, y (a tap on a menu's row, on the picture): down 1, or up 0
extern "C" void portTouchTestFinger(float x, float y, int down) {
  pushFinger(down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP, 3001, x, y);
}
