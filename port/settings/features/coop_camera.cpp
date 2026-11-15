// Multiplayer > Camera (port/src/common/coop_camera_pc.c, port/include/port/coop_camera.h): with players
// 2 to 4 in the field, the camera keeps everyone in view (Fit everyone, the default) or stays on Mario as
// before (Follow Mario). Only the picture: each player's own. Online, a guest's Fit everyone follows the
// guest's own character around the host's room.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Fit everyone", "Follow Mario"};

const bool sAdded = [] {
  portSettingsDeclare("coop_camera", 0, 0, 1);
  portSettingsAddChoice("Multiplayer", "Co-op camera",
                        "With players 2 to 4 in the field. Fit everyone: the camera pulls back to keep all players "
                        "in view (online, it follows your own character). Follow Mario: it stays on Mario, as in a "
                        "one-player game.",
                        0, "coop_camera", kChoices, 2,
                        "camera co-op coop multiplayer fit everyone follow mario zoom frame split players online guest");
  return true;
}();

} // namespace
