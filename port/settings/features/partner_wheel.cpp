// The partner wheel (port/include/port/gameplus.h): hold the quick-swap
// control (Q, the left stick pressed in, or the on-screen swap button) and a
// ring of the partners who have joined opens in the field; point at one (a
// stick, the movement keys, the mouse, or the finger dragged from the button)
// and let go to switch to them. A quick tap is still the quick swap.
//
// The choice goes into the controller's input as the second screen's partner
// tap (port/include/port/companion.h): online lockstep and replays carry it
// like any input, and the field takes it where the Start menu could open, as
// it takes every partner switch. While the wheel is open the stick chooses
// instead of walking (portPartnerWheelPad, before online's exchange). The
// ring is drawn with Dear ImGui (no game font: no random numbers drawn).
#include "port/bindings.h"
#include "port/companion.h"
#include "port/gameplus.h"
#include "port/layout.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace {

constexpr Uint64 kHoldMs = 250; // held this long: the wheel, not a quick swap
constexpr float kPi = 3.14159265f;

int* sOn; // partner_wheel

struct Wheel {
  bool down = false;    // the control is held
  bool open = false;    // the ring is up
  Uint64 since = 0;     // held since
  int choice = 0;       // the partner pointed at (0 none)
  float mouseX = 0.f, mouseY = 0.f; // where the mouse was as it opened
  bool touchSet = false;
  float touchX0 = 0.f, touchY0 = 0.f, touchX = 0.f, touchY = 0.f; // the finger's drag
} sWheel;

const char* partnerName(int id) {
  static const char* const kNames[8] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
  return id >= 1 && id <= 7 ? portTr(kNames[id]) : "";
}

// a stick held over the dead zone: its direction (screen axes, y down)
bool stickDirection(float* dx, float* dy) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  bool found = false;
  for (int i = 0; ids != nullptr && i < count && !found; i++) {
    SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
    if (pad == nullptr) {
      continue;
    }
    const SDL_GamepadAxis kAxes[2][2] = {{SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY},
                                          {SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY}};
    for (const auto& a : kAxes) {
      const float x = SDL_GetGamepadAxis(pad, a[0]) / 32767.f, y = SDL_GetGamepadAxis(pad, a[1]) / 32767.f;
      if (x * x + y * y > 0.25f) {
        *dx = x;
        *dy = y;
        found = true;
        break;
      }
    }
  }
  SDL_free(ids);
  return found;
}

// the movement keys (WASD, the arrows)
bool keyDirection(float* dx, float* dy) {
  int count = 0;
  const bool* k = SDL_GetKeyboardState(&count);
  if (k == nullptr) {
    return false;
  }
  const float x = (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT] ? 1.f : 0.f) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT] ? 1.f : 0.f);
  const float y = (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN] ? 1.f : 0.f) - (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP] ? 1.f : 0.f);
  *dx = x;
  *dy = y;
  return x != 0.f || y != 0.f;
}

void close() {
  sWheel.open = false;
  sWheel.choice = 0;
  sWheel.touchSet = false;
  gPortPartnerWheelOpen = 0;
}

void draw(int pageOpen) {
  if (sOn == nullptr) {
    return;
  }
  gPortPartnerWheelOwns = *sOn != 0;
  if (*sOn == 0) {
    sWheel.down = false;
    close();
    return;
  }
  const bool held = !pageOpen && (portBindingsHeld(PORT_BIND_QUICK_SWAP) || gPortQuickSwapTouch);
  const Uint64 now = SDL_GetTicks();
  if (held && !sWheel.down) {
    sWheel.down = true;
    sWheel.since = now;
    SDL_GetMouseState(&sWheel.mouseX, &sWheel.mouseY);
    sWheel.touchSet = false;
  }
  if (!held && sWheel.down) {
    sWheel.down = false;
    if (sWheel.open) {
      if (sWheel.choice > 0) {
        portCompanionTap(PORT_COMPANION_TAP_PARTNER, sWheel.choice, 0);
        std::printf("[wheel] partner %d chosen\n", sWheel.choice);
        std::fflush(stdout);
      }
    } else if (now - sWheel.since < kHoldMs && !pageOpen) {
      portQuickSwapPress(); // a tap: the quick swap, as without the wheel
    }
    close();
    return;
  }
  int ids[7], current = 0;
  const int n = portPartnerWheelList(ids, 7, &current);
  if (sWheel.down && !sWheel.open && now - sWheel.since >= kHoldMs && n > 0 && portPartnerWheelFieldFree()) {
    sWheel.open = true;
    sWheel.choice = 0;
  }
  if (!sWheel.open) {
    return;
  }
  if (n == 0 || !portPartnerWheelFieldFree()) {
    close(); // a scene began: the wheel goes, and letting go does nothing
    return;
  }
  gPortPartnerWheelOpen = 1;
  PortLayout l;
  if (!portLayoutGet(&l) || l.picW <= 0.f || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  const ImVec2 c(l.picX + l.picW * 0.5f, l.picY + l.picH * 0.5f);
  const float radius = std::min(l.picW, l.picH) * 0.3f;

  // where it points: a stick, the keys, the finger's drag, then the mouse
  float dx = 0.f, dy = 0.f;
  bool pointing = stickDirection(&dx, &dy) || keyDirection(&dx, &dy);
  if (!pointing && sWheel.touchSet) {
    dx = sWheel.touchX - sWheel.touchX0;
    dy = sWheel.touchY - sWheel.touchY0;
    pointing = dx * dx + dy * dy > 24.f * 24.f;
  }
  if (!pointing) {
    float mx = 0.f, my = 0.f;
    SDL_GetMouseState(&mx, &my);
    if (std::fabs(mx - sWheel.mouseX) + std::fabs(my - sWheel.mouseY) > 20.f) {
      dx = mx - c.x;
      dy = my - c.y;
      pointing = dx * dx + dy * dy > radius * radius * 0.09f;
    }
  }
  if (pointing) {
    // the slot nearest the direction: slot 0 at the top, clockwise
    float a = std::atan2(dy, dx) + kPi * 0.5f;
    while (a < 0.f) {
      a += 2.f * kPi;
    }
    const int slot = static_cast<int>(std::floor(a / (2.f * kPi / n) + 0.5f)) % n;
    sWheel.choice = ids[slot];
  }

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float fs = ImGui::GetFontSize() * 1.15f;
  const ImU32 paper = IM_COL32(250, 244, 226, 235), ink = IM_COL32(52, 40, 30, 255), gold = IM_COL32(255, 206, 64, 255);
  dl->AddCircleFilled(c, radius * 1.32f, IM_COL32(0, 0, 0, 110), 64);
  dl->AddCircle(c, radius, IM_COL32(255, 255, 255, 90), 64, 3.f);
  for (int i = 0; i < n; i++) {
    const float a = -kPi * 0.5f + 2.f * kPi * i / n;
    const ImVec2 at(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius);
    const bool chosen = ids[i] == sWheel.choice;
    const float r = radius * (chosen ? 0.30f : 0.24f);
    dl->AddCircleFilled(ImVec2(at.x + 3.f, at.y + 4.f), r, IM_COL32(0, 0, 0, 90), 40); // its shadow: a paper disc
    dl->AddCircleFilled(at, r, chosen ? gold : paper, 40);
    dl->AddCircle(at, r, ink, 40, chosen ? 3.f : 2.f);
    if (ids[i] == current) {
      dl->AddCircle(at, r + 5.f, IM_COL32(255, 255, 255, 200), 40, 2.f); // the one out now
    }
    const char* name = partnerName(ids[i]);
    const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, name);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), ink, name);
  }
  const char* middle = sWheel.choice > 0 ? partnerName(sWheel.choice) : portTr("Point at a partner, then let go");
  const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, middle);
  dl->AddRectFilled(ImVec2(c.x - size.x * 0.5f - 8.f, c.y - size.y * 0.5f - 4.f),
                    ImVec2(c.x + size.x * 0.5f + 8.f, c.y + size.y * 0.5f + 4.f), IM_COL32(0, 0, 0, 160), 6.f);
  dl->AddText(ImGui::GetFont(), fs, ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), IM_COL32(255, 255, 255, 255),
              middle);
}

const bool sAdded = [] {
  sOn = portSettingsDeclare("partner_wheel", 1, 0, 1);
  portSettingsAddToggle("Controls", "Partner wheel",
                        "Hold Partner quick-swap ({SWAP}, or the on-screen swap button) for a "
                        "ring of your partners; point at one with a stick, the movement keys, the mouse or your "
                        "finger, and let go. A quick tap still swaps to the next one (with Partner quick-swap on).",
                        0, "partner_wheel", "partner wheel ring radial swap switch quick select");
  portSettingsAddOverlay(draw, nullptr);
  return true;
}();

} // namespace

extern "C" void portPartnerWheelPointer(float x, float y) {
  if (!sWheel.touchSet) {
    sWheel.touchSet = true;
    sWheel.touchX0 = x;
    sWheel.touchY0 = y;
  }
  sWheel.touchX = x;
  sWheel.touchY = y;
}
