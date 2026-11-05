// The pop-up launcher (launcher_popup.cpp; setting launcher_popup): as the
// launcher opens, its stage builds like a pop-up book: the curtains part, the
// stage boards fold up, the clouds pop up in two rows, the moon and stars drop
// on their strings, the footlights come up, the title unfolds upright, the
// cards rise onto the stage and Mario and his partners spring up along its
// front edge. About two seconds; any key, click or button skips it.
#pragma once

#include "launcher_stage.h"

#include <imgui.h>

// At the launcher's start: the user folder and the game id of the disc whose
// art cache holds the cast (art/recipes/launcher_cast.recipe), or nullptr.
void launcherPopupStart(const char* userPath, const char* gameId);
// Before launcherBackdrop: a key, click or button skips the opening.
void launcherPopupStage(float time, float scale);
// How far a stage layer has popped up (0 not yet, 1 up: also with the pop-up off or skipped).
float launcherPopupLayer(LauncherStageLayer layer, float time);
// The title's unfolding: mark the draw list before drawing it, then fold what
// was drawn since about baseline y.
int launcherPopupMark(ImDrawList* dl);
void launcherPopupFold(ImDrawList* dl, int mark, float baseline, float time);
// How far below its place the cards are now (0 once risen), of a stage height.
float launcherPopupRise(float height, float time);
// Last: the curtains and valance over everything while the curtains part, and the cast.
void launcherPopupFront(float time, float scale);
