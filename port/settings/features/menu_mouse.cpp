// The mouse in the game's menus (selection lists such as item lists, key
// items at a door, shops and partners; the battle commands, the file select
// and the pause menu: port/src/common/menu_tap_pc.c), desktop only: the
// choice under the pointer is selected as it moves, and a click on the
// selected one chooses it, as A; a right click (or the back button) is B and
// the wheel moves up and down the list. Nothing happens without a menu up,
// so play is never touched; the click that brings the window back to the
// front is ignored.
#include "port/layout.h"
#include "port/menu_tap.h"
#include "port/settings.h"
#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

extern "C" int portSettingsOwnsPoint(float x, float y); // settings_ui.cpp
extern "C" int portInputHomePort(void);                  // input_pc.c: the keyboard's port

// Esc in the game's menus (the Start menu and its pages, the title's menu, the Journal, the file select,
// lists), and a talk's message or choice: their back (B), as a right click is. Never with neither up (the
// field keeps Esc for nothing), and never when the player put Esc on one of the game's buttons (that button
// is pressed already).
extern "C" int portMenuEscape(void) {
  if (!portMenuBackOpen() ||(ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard)) {
    return 0;
  }
  u32 count = 0;
  const PADKeyButtonBinding* keys = PADGetKeyButtonBindings(static_cast<u32>(portInputHomePort()), &count);
  for (u32 i = 0; keys != nullptr && i < count; i++) {
    if (keys[i].scancode == SDL_SCANCODE_ESCAPE) {
      return 0;
    }
  }
  portMenuBack();
  if (std::getenv("TTYD_INPUT") != nullptr) { // scripted runs (tests)
    std::printf("[menu] Esc: back\n");
    std::fflush(stdout);
  }
  return 1;
}

namespace {

int* sOn;
Uint64 sFocusedAt;

// window point -> 0-1 across the picture, if it is on the picture
bool onPicture(float x, float y, float* nx, float* ny) {
  PortLayout l;
  if (!portLayoutGet(&l) || l.picW <= 0.f || l.picH <= 0.f) {
    return false;
  }
  *nx = (x - l.picX) / l.picW;
  *ny = (y - l.picY) / l.picH;
  return *nx >= 0.f && *nx < 1.f && *ny >= 0.f && *ny < 1.f;
}

bool pointerFree(float x, float y) {
  return portMenuOpen() && !portSettingsOwnsPoint(x, y) &&
         (ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureMouse);
}

struct MenuMouseSettings {
  MenuMouseSettings() {
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
    sOn = portSettingsDeclare("menu_mouse", 1, 0, 1);
    portSettingsAddToggle("Controls", "Mouse in menus",
                          "Lists, battle commands, the file select and the pause menu: the pointer "
                          "picks, a click chooses, a right click goes back, the wheel scrolls.",
                          0, "menu_mouse", "mouse click pointer hover cursor menu wheel battle pause file");
#endif
  }
} sMenuMouse;

} // namespace


extern "C" void portMenuMouseEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  float nx, ny;
  if (sOn == nullptr || *sOn == 0) {
    return;
  }
  switch (e->type) {
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    sFocusedAt = SDL_GetTicks();
    break;
  case SDL_EVENT_MOUSE_MOTION:
    if (e->motion.which != SDL_TOUCH_MOUSEID && pointerFree(e->motion.x, e->motion.y) &&
        onPicture(e->motion.x, e->motion.y, &nx, &ny)) {
      portMenuHover(nx, ny);
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (e->button.which == SDL_TOUCH_MOUSEID || SDL_GetTicks() - sFocusedAt <= 300 ||
        !onPicture(e->button.x, e->button.y, &nx, &ny)) {
      break;
    }
    if (!pointerFree(e->button.x, e->button.y)) {
      // no menu: a right click is still B on a talk's message or choice (portMenuBackOpen), as Esc
      if ((e->button.button == SDL_BUTTON_RIGHT || e->button.button == SDL_BUTTON_X1) &&
          !portSettingsOwnsPoint(e->button.x, e->button.y) &&
          (ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureMouse)) {
        portMenuBack();
      }
      break;
    }
    if (e->button.button == SDL_BUTTON_LEFT) {
      portMenuTap(nx, ny);
    } else if (e->button.button == SDL_BUTTON_RIGHT || e->button.button == SDL_BUTTON_X1) {
      portMenuBack();
    }
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    if (e->wheel.which != SDL_TOUCH_MOUSEID && pointerFree(e->wheel.mouse_x, e->wheel.mouse_y)) {
      static float sNotches; // a touchpad scrolls in fractions of a notch
      sNotches += e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
      const int whole = static_cast<int>(sNotches);
      sNotches -= static_cast<float>(whole);
      if (whole != 0) {
        portMenuWheel(whole);
      }
    }
    break;
  default:
    break;
  }
}
