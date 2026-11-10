// Multiplayer > Camera (port/src/common/coop_camera_pc.c, port/include/port/coop_camera.h): with players
// 2 to 4 in the field, the camera keeps everyone in view (Fit everyone, the default) or stays on Mario as
// before (Follow Mario). Only the picture: each player's own. Online, a guest's Fit everyone follows the
// guest's own character around the host's room.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Fit everyone", "Follow Mario"};

const bool sAdded = [] {
  portSettingsDeclare("coop_camera", 0, 0, 1);
  portSettingsAddChoice("Multiplayer", "Camera",
                        "With players 2 to 4 in the field. Fit everyone: the camera pans and pulls back a little to "
                        "keep everyone in view, inside each room's own camera limits, and a player at the picture's "
                        "edge is held there. Online, your screen follows your own character around the room. Follow "
                        "Mario: the camera stays on Mario, as in one player's game. Scenes and battles keep the game's "
                        "own camera.",
                        0, "coop_camera", kChoices, 2,
                        "camera co-op coop multiplayer fit everyone follow mario zoom frame split players online guest");
  return true;
}();

} // namespace
