// Graphics > Bloom (port/src/aurora/bloom_aurora.c, docs/PAPER_LOOKS.md): a glow around the bright parts of the
// scene, in three strengths. Only the look: each player's own.
#include "port/settings.h"

namespace {

const char* kChoices[] = {"Off", "Soft", "Strong", "Dreamy"};

const bool sAdded = [] {
  portSettingsDeclare("bloom", 0, 0, 3);
  portSettingsAddChoice("Graphics", "Bloom",
                        "A glow around the bright parts of the scene: lamps, sparkles, the sky. Soft barely shows; "
                        "Dreamy blooms like an old storybook film. The HUD and text stay sharp.",
                        0, "bloom", kChoices, 4, "bloom glow light bright soft dreamy post processing effect");
  return true;
}();

} // namespace
