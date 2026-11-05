// The title menu's switch (General tab): port/src/aurora/title_menu_aurora.c, port/include/port/title_menu.h.
#include "port/settings.h"

namespace {

const bool sAdded = [] {
  portSettingsDeclare("title_menu", 1, 0, 1);
  portSettingsAddToggle("General", "Title menu",
                        "After Press Start, a menu before the files: Play, Modes (party games and battles on a file "
                        "that is never saved), Online and Settings. Off: straight to the files, as the original.",
                        0, "title_menu", "title screen press start main menu modes online settings files");
  return true;
}();

} // namespace
