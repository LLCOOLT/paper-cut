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
#include "port/pm64.h"
#include "port/savestate.h"
#include "port/settings.h"
#include "port/tr.h"
#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

extern "C" int portPlayerMenuWheelPick(int id); // port/include/port/player_menu.h (C, no C++ guard)

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
  bool told = false; // this hold's "can't open" said (once a hold)
  int port = 0;      // the player who holds it (0-3): its stick points, its walk is the one stilled
} sWheel;

const char* partnerName(int id) {
  static const char* const kNames[8] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
  if (id > PM64_WHEEL_BASE && id < PM64_WHEEL_BASE + PM64_PARTNER_COUNT) {
    return portPm64PartnerName(id - PM64_WHEEL_BASE); // a Paper Mario 64 partner (port/include/port/pm64.h)
  }
  return id >= 1 && id <= 7 ? portTr(kNames[id]) : "";
}

// a stick of the opener's controller held over the dead zone: its direction (screen axes, y down). Only that
// port's: another player's stick, walking their own character, never points this wheel (before, the first
// stick SDL listed did, player 2's walking one included)
bool stickDirection(int port, float* dx, float* dy) {
  const s32 index = PADGetIndexForPort(static_cast<u32>(port));
  SDL_Gamepad* const pad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
  if (pad == nullptr) {
    return false;
  }
  const SDL_GamepadAxis kAxes[2][2] = {{SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY},
                                        {SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY}};
  for (const auto& a : kAxes) {
    const float x = SDL_GetGamepadAxis(pad, a[0]) / 32767.f, y = SDL_GetGamepadAxis(pad, a[1]) / 32767.f;
    if (x * x + y * y > 0.25f) {
      *dx = x;
      *dy = y;
      return true;
    }
  }
  return false;
}

// the player whose quick-swap control is held (-1 none): ports 0-3, each its own controller (the keyboard its
// seat's); one held on no seated controller is player 1's, as before
int heldPort() {
  for (int p = 0; p < 4; p++) {
    if (portBindingsHeldOn(PORT_BIND_QUICK_SWAP, p)) {
      return p;
    }
  }
  return portBindingsHeld(PORT_BIND_QUICK_SWAP) ? 0 : -1;
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
  // tests (docs/PC_PORT.md): TTYD_WHEEL_TEST=<id>[,<frames>] holds the wheel open that many drawn frames
  // (default 90) once the field is free, pointing at partner id (a Paper Mario 64 partner: 8 + 1..7, PM64_WHEEL_BASE), then
  // lets go; the list is logged as it opens
  static int sTestId = -1, sTestFrames = 90, sTestHeld = 0;
  if (sTestId < 0) {
    const char* t = SDL_getenv("TTYD_WHEEL_TEST");
    sTestId = 0;
    if (t != nullptr && std::sscanf(t, "%d,%d", &sTestId, &sTestFrames) < 1) {
      sTestId = 0;
    }
  }
  // (a scene that closes it while it's held: it opens again once the field is free, below)
  bool testHeld = false;
  if (sTestId > 0 && sTestHeld >= 0 && (sTestHeld > 0 || portPartnerWheelFieldFree())) {
    testHeld = ++sTestHeld <= sTestFrames;
    if (sTestHeld == 1) {
      sWheel.since = SDL_GetTicks() - kHoldMs; // (opens at once)
      sWheel.down = true;
    } else if (!testHeld) {
      sTestHeld = -1; // once
    }
  }
  // held: by whoever took it first, and from then on by that player's control only (their stick points it,
  // their walk is stilled: gPortPartnerWheelPort, portPartnerWheelPad). Before, any controller's control
  // opened player 1's wheel, and player 1's stick was the one zeroed
  bool held;
  if (testHeld) {
    held = true;
  } else if (pageOpen) {
    held = false;
  } else if (!sWheel.down) {
    const int p = gPortQuickSwapTouch ? 0 : heldPort();
    held = p >= 0;
    if (held) {
      sWheel.port = p;
    }
  } else {
    held = portBindingsHeldOn(PORT_BIND_QUICK_SWAP, sWheel.port) ||
           (sWheel.port == 0 && (gPortQuickSwapTouch || heldPort() == 0));
  }
  if (testHeld && !sWheel.down) {
    sWheel.port = 0;
  }
  gPortPartnerWheelPort = sWheel.port;
  const Uint64 now = SDL_GetTicks();
  if (held && !sWheel.down) {
    sWheel.down = true;
    sWheel.told = false;
    sWheel.since = now;
    SDL_GetMouseState(&sWheel.mouseX, &sWheel.mouseY);
    sWheel.touchSet = false;
    if (sWheel.port != 0) {
      std::printf("[wheel] held by player %d\n", sWheel.port + 1);
      std::fflush(stdout);
    }
  }
  if (!held && sWheel.down) {
    sWheel.down = false;
    if (sWheel.open) {
      if (sWheel.choice > 0 && sWheel.port != 0) {
        // (another player's wheel: the partner they walk, as their own pick does (player 2's: the party's
        // partner, a partner of this game's seven); players 3 and 4 pick theirs with L and R)
        if (sWheel.port == 1 && sWheel.choice <= 7) {
          portQuickSwapPickCoop(sWheel.choice);
        }
        std::printf("[wheel] player %d's partner %d chosen\n", sWheel.port + 1, sWheel.choice);
        std::fflush(stdout);
      } else if (sWheel.choice > 0) {
        // (an online co-op guest playing the partner: its pick, sent as the Character strip's is; the host's
        // game has the party. Anywhere else the field takes the tap)
        if (!portPlayerMenuWheelPick(sWheel.choice)) {
          portCompanionTap(PORT_COMPANION_TAP_PARTNER, sWheel.choice, 0);
        }
        std::printf("[wheel] partner %d chosen\n", sWheel.choice);
        std::fflush(stdout);
      }
    } else if (now - sWheel.since < kHoldMs && !pageOpen) {
      // a tap: the quick swap, as without the wheel (player 2's: co-op's, the partner they walk)
      if (sWheel.port == 0) {
        portQuickSwapPress();
      } else if (sWheel.port == 1) {
        portQuickSwapPressCoop();
      }
    }
    close();
    return;
  }
  int ids[7 + PM64_PARTNER_COUNT], current = 0;
  const int n = portPartnerWheelList(ids, 7 + PM64_PARTNER_COUNT - 1, &current);
  if (sWheel.down && !sWheel.open && now - sWheel.since >= kHoldMs && n > 0 && portPartnerWheelFieldFree()) {
    sWheel.open = true;
    sWheel.choice = 0;
    if (testHeld) {
      std::printf("[wheel] test: %d entries:", n);
      for (int i = 0; i < n; i++) {
        std::printf(" %d", ids[i]);
      }
      std::printf("\n");
      std::fflush(stdout);
    }
  }
  if (sWheel.down && !sWheel.open && !sWheel.told && !testHeld && now - sWheel.since >= kHoldMs) {
    // held for the wheel, and it can't open: why, once a hold (a battle, Peach or Bowser, a scene, no partner)
    sWheel.told = true;
    portSaveStateNotice(portPartnerWheelWhyNot());
  }
  if (!sWheel.open) {
    return;
  }
  if (n == 0 || !portPartnerWheelFieldFree()) {
    close(); // a scene began: the wheel goes, and letting go does nothing
    if (testHeld) {
      sTestHeld = 0; // (the test's hold: again once the field is free)
      sWheel.down = false;
    }
    return;
  }
  gPortPartnerWheelOpen = 1;
  PortLayout l;
  if (!portLayoutGet(&l) || l.picW <= 0.f || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  const ImVec2 c(l.picX + l.picW * 0.5f, l.picY + l.picH * 0.5f);
  const float radius = std::min(l.picW, l.picH) * 0.3f;

  // where it points: its player's stick, then (player 1's) the keys, the finger's drag, the mouse
  float dx = 0.f, dy = 0.f;
  const bool p1 = sWheel.port == 0;
  bool pointing = stickDirection(sWheel.port, &dx, &dy) || (p1 && keyDirection(&dx, &dy));
  if (!p1) {
    // (no keys, finger or mouse for another player's wheel: they're player 1's)
  } else if (!pointing && sWheel.touchSet) {
    dx = sWheel.touchX - sWheel.touchX0;
    dy = sWheel.touchY - sWheel.touchY0;
    pointing = dx * dx + dy * dy > 24.f * 24.f;
  }
  if (!pointing && p1) {
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
  if (testHeld) {
    sWheel.choice = sTestId;
  }

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const float fs = ImGui::GetFontSize() * 1.15f;
  const ImU32 paper = IM_COL32(250, 244, 226, 235), ink = IM_COL32(52, 40, 30, 255), gold = IM_COL32(255, 206, 64, 255);
  if (n > 8) {
    // more than the seven (the Paper Mario 64 partners too: up to 14): a wider ring of paper labels, each
    // as wide as the gap to the next allows, the names made smaller together where one wouldn't fit
    const float ring = std::min(l.picW, l.picH) * 0.38f;
    const float gap = 2.f * ring * std::sin(kPi / n) - 6.f;
    float fit = 1.f, widest = 0.f;
    for (int i = 0; i < n; i++) {
      const float w = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, partnerName(ids[i])).x;
      widest = std::max(widest, w);
      fit = std::min(fit, (gap - 12.f) / std::max(w, 1.f));
    }
    const float lfs = fs * std::max(fit, 0.5f);
    if (testHeld && sTestHeld == 2) {
      std::printf("[wheel] test: ring %.0f, label gap %.0f, widest name %.0f at %.0f%% (%s)\n", ring, gap, widest,
                  100.f * std::max(fit, 0.5f), widest * std::max(fit, 0.5f) + 12.f <= gap ? "fits" : "TOO WIDE");
      std::fflush(stdout);
    }
    dl->AddCircleFilled(c, ring * 1.22f, IM_COL32(0, 0, 0, 110), 64);
    dl->AddCircle(c, ring, IM_COL32(255, 255, 255, 90), 64, 3.f);
    for (int i = 0; i < n; i++) {
      const float a = -kPi * 0.5f + 2.f * kPi * i / n;
      const ImVec2 at(c.x + std::cos(a) * ring, c.y + std::sin(a) * ring);
      const bool chosen = ids[i] == sWheel.choice;
      const char* name = partnerName(ids[i]);
      const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(lfs, FLT_MAX, 0.f, name);
      const float hw = std::min(size.x * 0.5f + 6.f, gap * 0.5f) + (chosen ? 4.f : 0.f), hh = size.y * 0.5f + (chosen ? 8.f : 5.f);
      const ImVec2 p0(at.x - hw, at.y - hh), p1(at.x + hw, at.y + hh);
      dl->AddRectFilled(ImVec2(p0.x + 3.f, p0.y + 4.f), ImVec2(p1.x + 3.f, p1.y + 4.f), IM_COL32(0, 0, 0, 90), hh);
      dl->AddRectFilled(p0, p1, chosen ? gold : paper, hh);
      dl->AddRect(p0, p1, ink, hh, 0, chosen ? 3.f : 2.f);
      if (ids[i] == current) {
        dl->AddRect(ImVec2(p0.x - 4.f, p0.y - 4.f), ImVec2(p1.x + 4.f, p1.y + 4.f), IM_COL32(255, 255, 255, 200), hh + 4.f, 0,
                    2.f); // the one out now
      }
      dl->AddText(ImGui::GetFont(), lfs, ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), ink, name);
    }
  }
  if (n <= 8) {
    dl->AddCircleFilled(c, radius * 1.32f, IM_COL32(0, 0, 0, 110), 64);
    dl->AddCircle(c, radius, IM_COL32(255, 255, 255, 90), 64, 3.f);
  }
  for (int i = 0; i < n && n <= 8; i++) {
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
