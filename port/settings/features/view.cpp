// The free camera and mirror mode (both on the Gameplay tab): port/include/port/view.h,
// port/src/common/view_pc.c. Both change only how the game is seen, not what it plays out.
//
// The mouse camera (desktop): while the free camera can turn, the cursor is
// captured (the input router's relative pointer mode, through a layer of our
// own under Dear ImGui's, so the settings page still gets its pointer) and the
// mouse's moves turn the camera; anywhere else (menus, scenes, battles, the
// page) the cursor is the player's again.
#include "port/settings.h"
#include "port/view.h"

#include <SDL3/SDL.h>
#include <aurora/input.hpp>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace {

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define MOUSE_CAMERA 1

aurora::input::LayerId sLayer = aurora::input::kInvalidLayerId;

aurora::input::PointerMode pointerMode(void*) {
  return portViewMouseLook() ? aurora::input::PointerMode::Relative : aurora::input::PointerMode::None;
}

aurora::input::EventResult layerEvent(const aurora::input::InputEvent&, void*) {
  return aurora::input::EventResult::Pass;
}

// each presented frame: our layer, once the router is up
void tick(int pageOpen) {
  (void)pageOpen;
  if (sLayer == aurora::input::kInvalidLayerId) {
    aurora::input::LayerDescriptor d;
    d.label = "paper-cut.mouse-camera";
    d.priority = aurora::input::kGameLayerPriority + 50;
    d.onEvent = layerEvent;
    d.pointerMode = pointerMode;
    sLayer = aurora::input::register_layer(d);
  }
}

int event(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  switch (e->type) {
  case SDL_EVENT_MOUSE_MOTION: {
    SDL_Window* w = SDL_GetWindowFromID(e->motion.windowID);
    // only while captured: a free pointer's moves are the menus' and the page's
    if (e->motion.which != SDL_TOUCH_MOUSEID && w != nullptr && SDL_GetWindowRelativeMouseMode(w)) {
      portViewMouse(e->motion.xrel, e->motion.yrel);
    }
    break;
  }
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (e->button.button == SDL_BUTTON_MIDDLE && portViewMouseLook()) {
      portViewMouseCentre();
    }
    break;
  default:
    break;
  }
  return 0;
}
#endif

const bool sAdded = [] {
  portSettingsDeclare("free_camera", 0, 0, 1);
  portSettingsDeclare("mirror_mode", 0, 0, 1);
  portSettingsAddToggle("Gameplay", "Free camera",
                        "In the field, the right stick turns the camera a little around Mario and moves it in or "
                        "out. Let go and it eases back.",
                        0, "free_camera", "camera c-stick right stick orbit rotate zoom look around view angle");
#ifdef MOUSE_CAMERA
  portSettingsDeclare("mouse_camera", 1, 0, 1);
  portSettingsDeclare("mouse_camera_speed", 5, 1, 10);
  portSettingsDeclare("mouse_camera_invert", 0, 0, 1);
  portSettingsAddToggle("Gameplay", "Mouse camera",
                        "With the free camera on, the mouse turns it too while Mario walks, and the window holds "
                        "the cursor then; menus and scenes give it back. A middle click centres the camera.",
                        0, "mouse_camera", "mouse look mouselook camera cursor capture free camera");
  portSettingsAddSlider("Gameplay", "Mouse camera speed", "How far the camera turns for a move of the mouse.", 0,
                        "mouse_camera_speed", 1, 10, "%d", "mouse sensitivity speed camera look");
  portSettingsAddToggle("Gameplay", "Invert mouse camera", "Moving the mouse up moves the camera out and higher.", 0,
                        "mouse_camera_invert", "mouse invert inverted y axis camera look");
  portSettingsAddOverlay(tick, event);
#endif
  portSettingsAddToggle("Gameplay", "Mirror mode",
                        "The world flipped left to right, with the stick and D-pad turned around to match while "
                        "Mario walks. Menus and messages stay readable.",
                        0, "mirror_mode", "mirror flip flipped reverse left right horizontal");
  return true;
}();

} // namespace
