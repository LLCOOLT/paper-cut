// Quick resume's switch (System tab): port/src/aurora/resume_aurora.c keeps
// the game as a save of its own while playing, for the launcher's Continue.
#include "port/settings.h"

namespace {

const bool sAdded = [] {
  portSettingsDeclare("quick_resume", 1, 0, 1);
  portSettingsAddToggle("General", "Quick resume",
                        "While you play, the game keeps a save of where you are, written when the app closes or goes "
                        "to the background: the launcher's Continue picks up there. Your own saves are untouched "
                        "until you save.",
                        0, "quick_resume", "continue resume suspend where you left off background close");
  return true;
}();

} // namespace
