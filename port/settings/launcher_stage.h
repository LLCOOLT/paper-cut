// The launcher's stage (launcher_stage.cpp): a paper theatre cut from layers of paper like the Paper
// Cut wordmark, drawn from shapes (no game assets). Behind everything: a painted night-sky flat with
// cut-out clouds, a paper moon and stars on strings, stage boards with footlights and a spotlight
// pool, pleated curtains tied back with gold tasselled ropes, and a valance of swags with a gold
// fringe. launcherBackdrop (launcher_style.h) draws it; the pop-up opening (launcher_popup.h) builds
// it layer by layer.
#pragma once

#include <imgui.h>

// The stage's layers that pop up in turn as the pop-up launcher opens (launcherPopupLayer).
enum class LauncherStageLayer { Floor, CloudsBack, CloudsFront, Hanging, Lights };

// The curtains open by `open` (0 closed across the stage, 1 tied back as on the stage at rest) and
// the valance over them, on the foreground draw list (the pop-up's parting curtains). At 1 it is
// exactly the resting stage's.
void launcherStageFront(float open, float time, float scale);

// The stage keeps still (no sway, no twinkle): the system asks for less motion.
bool launcherStageStill();
