// The launcher's look and sounds (port/settings/launcher_look.cpp): on the General tab, beside the
// pop-up launcher.
#include "port/settings.h"

namespace {

const char* const kLooks[] = {"Paper Cut's", "The game's (trial)"};

const bool kRegistered = [] {
  portSettingsDeclare("launcher_style", 0, 0, 1);
  portSettingsDeclare("launcher_sounds", 1, 0, 1);
  portSettingsAddChoice("General", "Launcher look",
                        "Paper Cut's: the launcher's own paper, drawn by Paper Cut. The game's: the Start menu's own "
                        "glove, paper window and labels, made from your disc once it is in the launcher's list.",
                        0, "launcher_style", kLooks, 2, "launcher style look theme game menu glove paper");
  portSettingsAddToggle("General", "Launcher sounds",
                        "Soft paper sounds in the launcher as the glove moves, a choice is made and a page turns.", 0,
                        "launcher_sounds", "launcher sound click page turn");
  return true;
}();

} // namespace
