// Photo mode (P, or both sticks pressed in; General > Photo mode; the Start
// menu's Settings tab): the game held where it is (frame advance's hold),
// the camera turned all the way round Mario, up and down, in and out, the HUD
// hidden or not, a filter, a frame and depth of field (port/include/port/picture.h),
// and a picture taken (as F12: the picture as shown, at the window's size, to
// the Pictures folder's Paper Cut; the Journal's Photos show them). Only in the
// field or a battle, and not online (port/include/port/view.h).
//
//   left stick / WASD, arrows   turn round Mario, up and down
//   right stick / R, F          in and out
//   A / Enter                   a picture
//   X / 1                       the next filter
//   LB / 2                      depth of field: off, soft, strong (sharp round Mario)
//   RB / 3                      the next frame
//   Y / H                       the HUD and message windows hidden or shown
//   B / P, Esc                  back to the game
#include <dolphin/pad.h>

#include "port/menu_buttons.h"
#include "port/bindings.h"
#include "port/button_markup.h"
#include "port/picture.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/savestate.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/view.h"

#include "../launcher_style.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" void portShotRequestUser(void);            // shot_aurora.c (port/platform.h, C only)
extern "C" void portShotRequest(const char* name);    // the same, for tests

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kPitchMin = -0.45f, kPitchMax = 1.2f; // radians: a little under, well over
constexpr float kZoomMin = 0.35f, kZoomMax = 3.f;
constexpr float kTurn = 1.6f, kTilt = 1.0f, kZoomRate = 1.2f; // per second at full tilt

constexpr const char* kFilters[PORT_PHOTO_FILTER_COUNT] = {
    "No filter", "Sepia", "Black and white", "Noir", "Vivid", "Warm", "Cool", "Faded", "Cut paper", "Folding light",
};
constexpr const char* kFrames[PORT_PHOTO_FRAME_COUNT] = {"No frame", "Paper border", "Stamp", "Film strip",
                                                         "Storybook"};
constexpr const char* kDof[PORT_PHOTO_DOF_COUNT] = {"Depth of field off", "Soft depth of field",
                                                    "Strong depth of field"};

struct {
  bool on, hideHud;
  int wasHeld; // portFrameAdvanceHeld() before it (given back)
  float yaw, pitch, zoom = 1.f;
  Uint64 last;
} sPhoto;

// the on-screen controls (port/touch/touch_overlay.cpp): their sticks as held
// now, and whether they were used this time (the help then names them)
struct {
  float x, y, z;
  bool used;
} sTouch;

// the look: kept from one use to the next while the game runs
int sFilter, sFrame, sDof;
// the last one changed, said for a moment (the help is hidden with the HUD)
const char* sSaid;
Uint64 sSaidAt;

void apply() {
  portPhotoSet(sPhoto.on, sPhoto.yaw, sPhoto.pitch, sPhoto.zoom, sPhoto.hideHud);
  float fx = 0.5f, fy = 0.5f;
  portPhotoFocus(&fx, &fy);
  portPictureSetPhoto(sPhoto.on ? 1 : 0, sFilter, sFrame, sDof, fx, fy);
}

void say(const char* text) {
  sSaid = text;
  sSaidAt = SDL_GetTicks();
}

void nextFilter() {
  sFilter = (sFilter + 1) % PORT_PHOTO_FILTER_COUNT;
  say(kFilters[sFilter]);
  apply();
}

void nextFrame() {
  sFrame = (sFrame + 1) % PORT_PHOTO_FRAME_COUNT;
  say(kFrames[sFrame]);
  apply();
}

void nextDof() {
  sDof = (sDof + 1) % PORT_PHOTO_DOF_COUNT;
  say(kDof[sDof]);
  apply();
}

void start() {
  if (sPhoto.on || !portPhotoAvailable()) {
    return;
  }
  sPhoto = {};
  sPhoto.on = true;
  sPhoto.zoom = 1.f;
  sPhoto.wasHeld = portFrameAdvanceHeld();
  sPhoto.last = SDL_GetTicksNS();
  sSaid = nullptr;
  sTouch.used = false;
  portFrameAdvanceHold(PORT_FRAME_HOLD_FEATURE);
  apply();
}

void stop() {
  if (!sPhoto.on) {
    return;
  }
  sPhoto.on = false;
  apply();
  portFrameAdvanceHold(sPhoto.wasHeld);
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

// Tests: $TTYD_PHOTO_TEST=<frame>:<yaw>:<pitch>:<zoom>[:<filter>:<frame>:<dof>] (presented frames, radians): photo
// mode from that frame at that view (and look), a picture (photo_hud), the HUD hidden and another (photo_clean),
// then back to the game; the pictures in $TTYD_SHOT_DIR. $TTYD_PHOTO_USER=1: the second one as the player's (to
// the user folder's Photos with $TTYD_USER_DIR set), read back from the picture as shown.
void testFrame() {
  static const char* spec = SDL_getenv("TTYD_PHOTO_TEST");
  static int frame, at = -1, filter, frameStyle, dof;
  static float yaw, pitch, zoom = 1.f;
  if (spec == nullptr) {
    return;
  }
  if (at < 0) {
    const int n = std::sscanf(spec, "%d:%f:%f:%f:%d:%d:%d", &at, &yaw, &pitch, &zoom, &filter, &frameStyle, &dof);
    if (n < 4) {
      spec = nullptr;
      return;
    }
  }
  frame++;
  if (frame == at) {
    start();
    sPhoto.yaw = yaw;
    sPhoto.pitch = pitch;
    sPhoto.zoom = zoom;
    sFilter = std::clamp(filter, 0, PORT_PHOTO_FILTER_COUNT - 1);
    sFrame = std::clamp(frameStyle, 0, PORT_PHOTO_FRAME_COUNT - 1);
    sDof = std::clamp(dof, 0, PORT_PHOTO_DOF_COUNT - 1);
    apply();
    std::printf("[photo] test: %s\n", sPhoto.on ? "on" : "not available here");
    std::fflush(stdout);
  } else if (frame == at + 40 && sPhoto.on) {
    portShotRequest("photo_hud");
  } else if (frame == at + 80 && sPhoto.on) {
    sPhoto.hideHud = true;
    apply();
  } else if (frame == at + 120 && sPhoto.on) {
    if (SDL_getenv("TTYD_PHOTO_USER") != nullptr) {
      portShotRequestUser();
    } else {
      portShotRequest("photo_clean");
    }
  } else if (frame == at + 200) {
    stop();
  }
}

// a line of text in a dark rounded box, its middle at x, its bottom at bottom
void helpLine(ImDrawList* dl, const char* text, float x, float bottom) {
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 at(std::floor(x - ts.x * 0.5f), bottom - ts.y);
  dl->AddRectFilled(ImVec2(at.x - 12.f, at.y - 6.f), ImVec2(at.x + ts.x + 12.f, at.y + ts.y + 6.f),
                    IM_COL32(20, 16, 30, 190), 8.f);
  dl->AddText(at, kPaper, text);
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
    x += sTouch.x;
    y += sTouch.y;
    z += sTouch.z;
    if (portViewMirrored()) {
      x = -x; // the picture's right
    }
    sPhoto.yaw = std::fmod(sPhoto.yaw + std::clamp(x, -1.f, 1.f) * kTurn * dt + 2.f * kPi, 2.f * kPi);
    sPhoto.pitch = std::clamp(sPhoto.pitch + std::clamp(y, -1.f, 1.f) * kTilt * dt, kPitchMin, kPitchMax);
    sPhoto.zoom = std::clamp(sPhoto.zoom * std::exp(-std::clamp(z, -1.f, 1.f) * kZoomRate * dt), kZoomMin, kZoomMax);
  }
  apply(); // (the focus follows Mario through the turned camera)
  if (pageOpen) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  if (!sPhoto.hideHud) { // (hidden with the HUD: nothing over the picture)
    char look[256];
    // (the buttons as the controller in use has them: port/include/port/button_markup.h)
    std::snprintf(look, sizeof(look),
                  portButtonsUi(sTouch.used ? "{X}: %s   {L}: %s   {R}: %s" : "{CLEAR:1}: %s   {LB:2}: %s   {RB:3}: %s"),
                  portTr(kFilters[sFilter]), portTr(kDof[sDof]), portTr(kFrames[sFrame]));
    const float line = ImGui::GetTextLineHeight() + 16.f;
    helpLine(dl,
             sTouch.used
                 ? portTr("Photo mode   {STICK} or a drag: turn   {CSTICK}: in and out   {A}: picture   {Y}: hide the "
                          "HUD   {B}: back")
                 : portTr("Photo mode   {STICK}: turn   {CSTICK}: in and out   {OK:Return}: picture   {NORTH:H}: "
                          "hide the HUD   {BACK:Escape}: back"),
             size.x * 0.5f, size.y - 18.f - line);
    helpLine(dl, look, size.x * 0.5f, size.y - 18.f);
  } else if (sSaid != nullptr && SDL_GetTicks() - sSaidAt < 1500) {
    helpLine(dl, portTr(sSaid), size.x * 0.5f, size.y - 18.f); // (not in the picture: it isn't the game's)
  }
}

int event(const void* e) {
  const SDL_Event* ev = static_cast<const SDL_Event*>(e);
  if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput) {
    return 0; // (typing in a box of the settings page)
  }
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
    } else if (k == SDL_SCANCODE_1) {
      nextFilter();
    } else if (k == SDL_SCANCODE_2) {
      nextDof();
    } else if (k == SDL_SCANCODE_3) {
      nextFrame();
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
    // back, the picture and the filter: the controller's own back, confirm and third (menu_buttons.h)
    const int role = portMenuEventRole(ev);
    if (role == PORT_MENU_BACK) {
      stop();
    } else if (b == SDL_GAMEPAD_BUTTON_NORTH) {
      sPhoto.hideHud = !sPhoto.hideHud;
      apply();
    } else if (role == PORT_MENU_CONFIRM) {
      portShotRequestUser();
    } else if (role == PORT_MENU_THIRD) {
      nextFilter();
    } else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
      nextDof();
    } else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
      nextFrame();
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
    ImGui::TextDisabled("%s", portTr("(in the field or a battle, not online)"));
  }
}

const bool sAdded = [] {
  portSettingsAddSection("General", "Photo mode",
                         "The game held where it is, the camera all the way round Mario, up and down, in and out, the "
                         "HUD hidden or not, a filter, a frame and depth of field, and a picture taken. {PHOTO}; then "
                         "the sticks move the camera, {OK:Return} takes the picture, {CLEAR:1} changes the filter, "
                         "{LB:2} the depth of field, {RB:3} the frame, {NORTH:H} hides the HUD, {BACK:Escape} goes back. The "
                         "pictures go to Pictures, Paper Cut; the Journal's Photos show them.",
                         0, section, "photo mode picture screenshot camera pose hide hud freeze filter frame depth "
                                     "field blur sepia gallery");
  portSettingsAddOverlay(tick, nullptr);
  portSettingsAddEventFirst(event); // (before the bindings: H hides the HUD here, not the partner hint)
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
      // (the title, a scene's change of room, a replay: said, not nothing)
      portSaveStateNotice(portReplayActive() ? "Photo mode: not in a replay."
                                             : "Photo mode: only in the field or a battle.");
      return 0;
    }
    start();
    return 1;
  });
  return true;
}();

} // namespace

// The on-screen controls (port/include/port/view.h).
extern "C" int portPhotoOn(void) {
  return sPhoto.on ? 1 : 0;
}

extern "C" void portPhotoToggle(void) {
  if (sPhoto.on) {
    stop();
  } else {
    start();
    sTouch.used = true;
  }
}

extern "C" void portPhotoTouchAxes(float x, float y, float z) {
  sTouch.x = std::clamp(x, -1.f, 1.f);
  sTouch.y = std::clamp(y, -1.f, 1.f);
  sTouch.z = std::clamp(z, -1.f, 1.f);
  if (sPhoto.on && (x != 0.f || y != 0.f || z != 0.f)) {
    sTouch.used = true;
  }
}

extern "C" void portPhotoTouchDrag(float dx, float dy) {
  if (!sPhoto.on) {
    return;
  }
  sTouch.used = true;
  if (portViewMirrored()) {
    dx = -dx; // the picture's right, as the stick's
  }
  // across the whole picture: half a turn round Mario; up it: the camera higher
  sPhoto.yaw = std::fmod(sPhoto.yaw + dx * kPi + 2.f * kPi, 2.f * kPi);
  sPhoto.pitch = std::clamp(sPhoto.pitch - dy * 1.2f, kPitchMin, kPitchMax);
  apply();
}

extern "C" int portPhotoTouchButton(unsigned int button) {
  if (!sPhoto.on) {
    return 0;
  }
  sTouch.used = true;
  if (button == PAD_BUTTON_B) {
    stop();
  } else if (button == PAD_BUTTON_Y) {
    sPhoto.hideHud = !sPhoto.hideHud;
    apply();
  } else if (button == PAD_BUTTON_A) {
    portShotRequestUser();
  } else if (button == PAD_BUTTON_X) {
    nextFilter();
  } else if (button == PAD_TRIGGER_L) {
    nextDof();
  } else if (button == PAD_TRIGGER_R) {
    nextFrame();
  } else {
    return 0;
  }
  return 1;
}
