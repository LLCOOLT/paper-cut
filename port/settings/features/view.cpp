// The free camera and mirror mode (both on the Gameplay tab): port/include/port/view.h,
// port/src/common/view_pc.c. Both change only how the game is seen, not what it plays out.
#include "port/settings.h"

namespace {

const bool sAdded = [] {
  portSettingsDeclare("free_camera", 0, 0, 1);
  portSettingsDeclare("mirror_mode", 0, 0, 1);
  portSettingsAddToggle("Gameplay", "Free camera",
                        "In the field, the right stick turns the camera a little around Mario and moves it in or "
                        "out. Let go and it eases back.",
                        0, "free_camera", "camera c-stick right stick orbit rotate zoom look around view angle");
  portSettingsAddToggle("Gameplay", "Mirror mode",
                        "The world flipped left to right, with the stick and D-pad turned around to match while "
                        "Mario walks. Menus and messages stay readable.",
                        0, "mirror_mode", "mirror flip flipped reverse left right horizontal");
  return true;
}();

} // namespace
