// Photo mode (P, or both sticks pressed in; System > Photo mode; the Start
// menu's Settings tab): the game held where it is (frame advance's hold),
// the camera turned all the way round Mario, up and down, in and out, the HUD
// hidden or not, and a picture taken (as F12). Only in the field and not
// online (port/include/port/view.h).
//
//   left stick / WASD, arrows   turn round Mario, up and down
//   right stick / R, F          in and out
//   A / Enter                   a picture
//   Y / H                       the HUD and message windows hidden or shown
//   B / P, Esc                  back to the game
#include <dolphin/pad.h>

#include "port/bindings.h"
#include "port/practice.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/view.h"

#include "../launcher_style.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

extern "C" void portShotRequestUser(void);            // shot_aurora.c (port/platform.h, C only)
extern "C" void portShotRequest(const char* name);    // the same, for tests

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kPitchMin = -0.45f, kPitchMax = 1.2f; // radians: a little under, well over
constexpr float kZoomMin = 0.35f, kZoomMax = 3.f;
constexpr float kTurn = 1.6f, kTilt = 1.0f, kZoomRate = 1.2f; // per second at full tilt

struct {
  bool on, hideHud, wasHeld;
  float yaw, pitch, zoom = 1.f;
  Uint64 last;
} sPhoto;

void apply() {
  portPhotoSet(sPhoto.on, sPhoto.yaw, sPhoto.pitch, sPhoto.zoom, sPhoto.hideHud);
}

void start() {
  if (sPhoto.on || !portPhotoAvailable()) {
    return;
  }
  sPhoto = {};
  sPhoto.on = true;
  sPhoto.zoom = 1.f;
  sPhoto.wasHeld = portFrameAdvanceHeld() != 0;
  sPhoto.last = SDL_GetTicksNS();
  portFrameAdvanceHold(1);
  apply();
}

void stop() {
  if (!sPhoto.on) {
    return;
  }
  sPhoto.on = false;
  apply();
  portFrameAdvanceHold(sPhoto.wasHeld ? 1 : 0);
}

// Mario's controller (player 1's)
SDL_Gamepad* pad() {
  return SDL_GetGamepadFromPlayerIndex(0);
}

float stick(SDL_Gamepad* p, SDL_GamepadAxis a) {
  if (p == nullptr) {
    return 0.f;
  }
  const float v = float(SDL_GetGamepadAxis(p, a)) / 32767.f;
  return std::fabs(v) < 0.18f ? 0.f : v;
}

// Tests: $TTYD_PHOTO_TEST=<frame>:<yaw>:<pitch>:<zoom> (presented frames, radians): photo
// mode from that frame at that view, a picture (photo_hud), the HUD hidden and another
// (photo_clean), then back to the game; the pictures in $TTYD_SHOT_DIR
void testFrame() {
  static const char* spec = SDL_getenv("TTYD_PHOTO_TEST");
  static int frame, at = -1;
  static float yaw, pitch, zoom = 1.f;
  if (spec == nullptr) {
    return;
  }
  if (at < 0 && std::sscanf(spec, "%d:%f:%f:%f", &at, &yaw, &pitch, &zoom) != 4) {
    spec = nullptr;
    return;
  }
  frame++;
  if (frame == at) {
    start();
    sPhoto.yaw = yaw;
    sPhoto.pitch = pitch;
    sPhoto.zoom = zoom;
    apply();
    std::printf("[photo] test: %s\n", sPhoto.on ? "on" : "not available here");
    std::fflush(stdout);
  } else if (frame == at + 40 && sPhoto.on) {
    portShotRequest("photo_hud");
  } else if (frame == at + 80 && sPhoto.on) {
    sPhoto.hideHud = true;
    apply();
  } else if (frame == at + 120 && sPhoto.on) {
    portShotRequest("photo_clean");
  } else if (frame == at + 160) {
    stop();
  }
}

// each presented frame: the camera from the sticks and keys, the help line
void tick(int pageOpen) {
  testFrame();
  static int sSince = -1; // presented frames since the Start menu's row started it (tests)
  if (!sPhoto.on && portPhotoTakeRequest()) { // the Start menu's row, the menu now closed
    start();
    sSince = 0;
  }
  if (!sPhoto.on) {
    sSince = -1;
    return;
  }
  if (sSince >= 0 && ++sSince == 30 && SDL_getenv("TTYD_PHOTO_MENU_SHOT") != nullptr) {
    portShotRequest("photo_from_menu"); // ($TTYD_PHOTO_MENU_SHOT: a picture once it's on)
  }
  if (!portPhotoAvailable()) { // the game moved on (a map change, online)
    stop();
    return;
  }
  const Uint64 now = SDL_GetTicksNS();
  const float dt = std::min(float(now - sPhoto.last) / 1e9f, 0.1f);
  sPhoto.last = now;
  if (!pageOpen) {
    SDL_Gamepad* p = pad();
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float x = stick(p, SDL_GAMEPAD_AXIS_LEFTX), y = -stick(p, SDL_GAMEPAD_AXIS_LEFTY),
          z = -stick(p, SDL_GAMEPAD_AXIS_RIGHTY);
    x += float(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) - float(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
    y += float(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) - float(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]);
    z += float(keys[SDL_SCANCODE_R]) - float(keys[SDL_SCANCODE_F]);
    if (portViewMirrored()) {
      x = -x; // the picture's right
    }
    sPhoto.yaw = std::fmod(sPhoto.yaw + std::clamp(x, -1.f, 1.f) * kTurn * dt + 2.f * kPi, 2.f * kPi);
    sPhoto.pitch = std::clamp(sPhoto.pitch + std::clamp(y, -1.f, 1.f) * kTilt * dt, kPitchMin, kPitchMax);
    sPhoto.zoom = std::clamp(sPhoto.zoom * std::exp(-std::clamp(z, -1.f, 1.f) * kZoomRate * dt), kZoomMin, kZoomMax);
    apply();
  }
  if (!sPhoto.hideHud && !pageOpen) { // (hidden with the HUD: nothing over the picture)
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const char* help = portTr("Photo mode   Stick: turn   Right stick: in and out   A: picture   Y: hide the HUD   B: "
                              "back");
    const ImVec2 ts = ImGui::CalcTextSize(help);
    const ImVec2 at(std::floor((size.x - ts.x) * 0.5f), size.y - ts.y - 18.f);
    dl->AddRectFilled(ImVec2(at.x - 12.f, at.y - 6.f), ImVec2(at.x + ts.x + 12.f, at.y + ts.y + 6.f),
                      IM_COL32(20, 16, 30, 190), 8.f);
    dl->AddText(at, kPaper, help);
  }
}

int event(const void* e) {
  const SDL_Event* ev = static_cast<const SDL_Event*>(e);
  // (P and both sticks pressed in, on and off: the Photo mode binding, below)
  if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
    const SDL_Scancode k = ev->key.scancode;
    if (!sPhoto.on) {
      return 0;
    }
    if (k == SDL_SCANCODE_ESCAPE) {
      stop();
    } else if (k == SDL_SCANCODE_H) {
      sPhoto.hideHud = !sPhoto.hideHud;
      apply();
    } else if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER) {
      portShotRequestUser();
    } else {
      return 0;
    }
    return 1;
  }
  if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    const int b = ev->gbutton.button;
    if (!sPhoto.on) {
      return 0;
    }
    if (b == SDL_GAMEPAD_BUTTON_EAST) { // B (Xbox's B, PlayStation's circle)
      stop();
    } else if (b == SDL_GAMEPAD_BUTTON_NORTH) {
      sPhoto.hideHud = !sPhoto.hideHud;
      apply();
    } else if (b == SDL_GAMEPAD_BUTTON_SOUTH) {
      portShotRequestUser();
    } else {
      return 0;
    }
    return 1;
  }
  return 0;
}

void section(int inGame) {
  ImGui::BeginDisabled(!inGame || !portPhotoAvailable());
  if (ImGui::Button(portTrLabel(sPhoto.on ? "Back to the game##photo" : "Photo mode##photo"))) {
    sPhoto.on ? stop() : start();
  }
  ImGui::EndDisabled();
  if (!inGame || !portPhotoAvailable()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("(in the field, not online)"));
  }
}

const bool sAdded = [] {
  portSettingsAddSection("General", "Photo mode",
                         "The game held where it is, the camera all the way round Mario, up and down, in and out, the "
                         "HUD hidden or not, and a picture taken. P on the keyboard or both sticks pressed in; then the "
                         "sticks move the camera, A takes the picture, Y hides the HUD, B goes back.",
                         0, section, "photo mode picture screenshot camera pose hide hud freeze");
  portSettingsAddOverlay(tick, event);
  // the Photo mode binding (Controls > Bindings): on, and off again
  portBindingsSetHandler(PORT_BIND_PHOTO, [](int, int repeat) {
    if (repeat) {
      return 1;
    }
    if (sPhoto.on) {
      stop();
      return 1;
    }
    if (!portPhotoAvailable()) {
      return 0;
    }
    start();
    return 1;
  });
  return true;
}();

} // namespace

// The Start menu's Settings tab and tests: photo mode on or off
extern "C" void portPhotoToggle(void) {
  sPhoto.on ? stop() : start();
}
