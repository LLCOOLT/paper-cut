// Co-op pings (port/include/port/ping.h, port/src/common/ping_pc.c): the
// setting, and the mouse's clicks on the picture. A left click pings where
// no game menu is up (the menus' clicks are menu_mouse.cpp's), the middle
// button at any time; a click that dragged (the mouse camera, a window
// moved) doesn't. Taps on a touch screen come from port/touch/touch_overlay.cpp.
#include "port/layout.h"
#include "port/menu_tap.h"
#include "port/ping.h"
#include "port/settings.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <imgui.h>

extern "C" int portSettingsOwnsPoint(float x, float y); // settings_ui.cpp

namespace {

Uint64 sFocusedAt;
struct Press {
  bool down = false;
  Uint8 button = 0;
  float x = 0.f, y = 0.f;
  Uint64 at = 0;
} sPress;

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
  return !portSettingsOwnsPoint(x, y) && (ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureMouse);
}

int mouseEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (!portPingsOn()) {
    return 0;
  }
  switch (e->type) {
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
    sFocusedAt = SDL_GetTicks();
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    // (a touch's mouse is the touch overlay's; the click that brings the window back isn't one)
    if (e->button.which != SDL_TOUCH_MOUSEID && SDL_GetTicks() - sFocusedAt > 300 &&
        (e->button.button == SDL_BUTTON_LEFT || e->button.button == SDL_BUTTON_MIDDLE) &&
        pointerFree(e->button.x, e->button.y)) {
      sPress = {true, e->button.button, e->button.x, e->button.y, SDL_GetTicks()};
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    float nx, ny;
    const Press p = sPress;
    sPress.down = false;
    if (!p.down || e->button.button != p.button || e->button.which == SDL_TOUCH_MOUSEID) {
      break;
    }
    const bool still = std::fabs(e->button.x - p.x) < 6.f && std::fabs(e->button.y - p.y) < 6.f &&
                       SDL_GetTicks() - p.at < 400;
    if (still && onPicture(e->button.x, e->button.y, &nx, &ny) &&
        (p.button == SDL_BUTTON_MIDDLE || !portMenuOpen())) {
      portPingClick(nx, ny);
    }
    break;
  }
  default:
    break;
  }
  return 0; // (the click is still everyone else's)
}

struct PingSettings {
  PingSettings() {
    portPingsOn(); // (declares it)
    portSettingsAddToggle("Multiplayer", "Co-op pings",
                          "Mark a spot or an enemy with a paper arrow both players see (online too): hold {PING} "
                          "and aim with the {CSTICK}, click the picture, or tap it.",
                          0, "coop_pings", "ping mark arrow point marker co-op coop online spot enemy");
    portSettingsAddOverlay(nullptr, mouseEvent);
  }
} sPings;

} // namespace
