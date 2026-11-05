// Paper looks' settings (port/include/port/paper.h, port/src/common/paper_pc.c,
// docs/PAPER_LOOKS.md): paper shadows, paper physics and smooth paper edges,
// on the Graphics tab, and the pop-up launcher (port/settings/launcher_popup.cpp)
// on the General tab. All draw-only, each player's own (online keeps them
// local: online.cpp's list).
#include "port/settings.h"

namespace {

const char* const kPhysics[] = {"Off", "Subtle", "Lively"};

const bool sAdded = [] {
  portSettingsDeclare("paper_shadows", 0, 0, 1);
  portSettingsDeclare("paper_physics", 0, 0, 2);
  portSettingsDeclare("paper_edges", 0, 0, 1);
  portSettingsAddToggle("Graphics", "Paper shadows",
                        "Characters cast shadows shaped like their paper cut-outs, stretching away from the nearest "
                        "lamp, instead of the round ones.",
                        0, "paper_shadows", "shadow shadows cut-out silhouette lamp light");
  portSettingsAddChoice("Graphics", "Paper physics",
                        "Characters bend like paper: they lean into fast moves, flutter on quick turns and squash on "
                        "hard landings. Only the look changes.",
                        0, "paper_physics", kPhysics, 3, "bend flutter squash wobble lean paper");
  portSettingsAddToggle("Graphics", "Smooth paper edges",
                        "Clean outlines on the paper cut-outs at high resolutions instead of jagged steps; smoothest "
                        "with anti-aliasing (MSAA) on.",
                        0, "paper_edges", "jagged edges outline aliasing cut-out smooth");
  // the launcher's (launcher_popup.cpp)
  portSettingsDeclare("launcher_popup", 1, 0, 1);
  portSettingsAddToggle("General", "Pop-up launcher",
                        "The launcher's stage builds like a pop-up book as it opens: the curtains part, the title "
                        "unfolds and Mario and his partners spring up (once the game has run on that disc). Any "
                        "button skips it.",
                        0, "launcher_popup", "launcher animation pop-up book curtains intro");
  return true;
}();

} // namespace
