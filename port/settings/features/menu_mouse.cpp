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
#include <imgui.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

extern "C" int portSettingsOwnsPoint(float x, float y); // settings_ui.cpp

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
        !pointerFree(e->button.x, e->button.y) || !onPicture(e->button.x, e->button.y, &nx, &ny)) {
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
