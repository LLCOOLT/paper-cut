// On-screen touch controls (TTYD_TOUCH_OVERLAY; on by default on iOS).
//
// Touches become a PADStatus handed to Aurora as port 1's virtual status
// (PADSetVirtualStatus), which Aurora's PADRead merges with whatever else
// drives that port, so the game reads it like a controller (no change to
// Aurora or the game). The controls are drawn over the frame with Aurora's
// Dear ImGui (foreground draw list, window coordinates in points).
//
// - Shown while no physical gamepad is connected; hidden (and the virtual
//   status cleared) while one is.
// - Left side: a floating analog stick (it centres where the finger lands).
//   Right side: A (large), B, X, Y in the GameCube arrangement; L and R
//   (full presses) in the top corners, Z under R, START at the top.
// - A button acts on touch-down: the status is updated when the touch event
//   arrives, before the game's PADRead of that frame.
// - Layout is relative to the window's safe area; on a phone in landscape the
//   controls sit in the pillarbox margins beside the 4:3 picture.
//
// $TTYD_TOUCH_MOUSE=1 (desktop testing): the mouse also produces touches.
// $TTYD_TOUCH_TEST=<frame>:<x>,<y>:<frames>[;...] (tests): a touch at window
// fraction x,y on that drawn frame, held that many frames, sent through SDL's
// event queue like a real one.
#include "port/touch_overlay.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
};

constexpr u32 kPort = 0; // the game's controller 1

struct Circle {
  float x, y, r;
};

struct Layout {
  Circle c[kControlCount]; // kStick: the stick's resting base
  float stickZoneRight;    // touches left of this (and below stickZoneTop) drive the stick
  float stickZoneTop;
};

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

bool computeLayout(Layout& l) {
  SDL_Window* w = window();
  if (w == nullptr) {
    return false;
  }
  SDL_Rect safe{};
  int ww = 0;
  int wh = 0;
  SDL_GetWindowSize(w, &ww, &wh);
  if (!SDL_GetWindowSafeArea(w, &safe) || safe.w <= 0 || safe.h <= 0) {
    safe = {0, 0, ww, wh};
  }
  const float sx = static_cast<float>(safe.x);
  const float sy = static_cast<float>(safe.y);
  const float sw = static_cast<float>(safe.w);
  const float sh = static_cast<float>(safe.h);
  // Sizes follow the shorter side, so a portrait-sized safe area still fits.
  const float u = std::fmin(sh, sw * 0.75f);
  const float right = sx + sw;
  const float bottom = sy + sh;
  const Circle a{right - 0.22f * u, bottom - 0.22f * u, 0.085f * u};
  l.c[kA] = a;
  l.c[kB] = {a.x - 0.15f * u, a.y + 0.07f * u, 0.058f * u};
  l.c[kX] = {a.x + 0.14f * u, a.y - 0.03f * u, 0.055f * u};
  l.c[kY] = {a.x - 0.02f * u, a.y - 0.15f * u, 0.055f * u};
  l.c[kR] = {right - 0.12f * u, sy + 0.10f * u, 0.05f * u};
  l.c[kZ] = {right - 0.12f * u, sy + 0.24f * u, 0.045f * u};
  l.c[kL] = {sx + 0.12f * u, sy + 0.10f * u, 0.05f * u};
  l.c[kStart] = {sx + sw * 0.5f, sy + 0.07f * u, 0.04f * u};
  l.c[kStick] = {sx + 0.22f * u, bottom - 0.24f * u, 0.13f * u};
  l.stickZoneRight = sx + sw * 0.45f;
  l.stickZoneTop = sy + 0.2f * u;
  return true;
}

bool physicalGamepad() {
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
  if (len > 1.0f) {
    dx /= len;
    dy /= len;
  }
  setStick(dx, dy);
}

Control hitTest(const Layout& l, float px, float py) {
  Control best = kNone;
  float bestDist = 0.f;
  for (int c = kA; c < kControlCount; c++) {
    const Circle& b = l.c[c];
    const float d = std::hypot(px - b.x, py - b.y);
    if (d <= b.r * 1.35f && (best == kNone || d < bestDist)) {
      best = static_cast<Control>(c);
      bestDist = d;
    }
  }
  if (best == kNone && px < l.stickZoneRight && py > l.stickZoneTop) {
    return kStick;
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
  }
  f.used = false;
}

void drawCircle(ImDrawList* dl, const Circle& c, const char* label, bool down) {
  const ImU32 fill = down ? IM_COL32(255, 255, 255, 120) : IM_COL32(255, 255, 255, 45);
  dl->AddCircleFilled(ImVec2(c.x, c.y), c.r, fill, 40);
  dl->AddCircle(ImVec2(c.x, c.y), c.r, IM_COL32(255, 255, 255, 140), 40, 2.0f);
  if (label != nullptr) {
    const float size = std::strlen(label) > 1 ? c.r * 0.55f : c.r * 0.95f;
    const ImVec2 ext = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.f, label);
    dl->AddText(ImGui::GetFont(), size, ImVec2(c.x - ext.x * 0.5f, c.y - ext.y * 0.5f),
                IM_COL32(255, 255, 255, 200), label);
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

extern "C" void portTouchEvent(const SDL_Event* event) {
  if (!sMouseInit) {
    sMouseInit = true;
    if (const char* m = std::getenv("TTYD_TOUCH_MOUSE"); m != nullptr && m[0] == '1') {
      SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    }
  }
  switch (event->type) {
  case SDL_EVENT_FINGER_DOWN:
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
    const Control c = hitTest(l, px, py);
    if (c == kNone || (c == kStick && sStickActive)) {
      return;
    }
    *f = {id, c, true};
    if (c == kStick) {
      sStickActive = true;
      sStickOriginX = px;
      sStickOriginY = py;
      setStick(0.f, 0.f);
    } else {
      sDown[c] = true;
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
  const bool visible = !physicalGamepad();
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
    drawCircle(dl, l.c[c], kButtons[c].label, sDown[c]);
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
