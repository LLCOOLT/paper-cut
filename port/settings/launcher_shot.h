// The launcher's test captures (launcher_shot.cpp): $TTYD_LAUNCHER_SHOT="SECONDS:NAME,..." writes the
// launcher as drawn, SECONDS after it opened, to $TTYD_SHOT_DIR/NAME.png (tests, screenshots).
#pragma once

#include <imgui.h>

// A texture given to Dear ImGui, kept for the captures (only while some are asked for).
void launcherShotTexture(ImTextureID id, int w, int h, const void* rgba);
// Captures are asked for.
bool launcherShotActive();
// After the frame was drawn (its draw data still there): the captures due at time (seconds).
void launcherShotFrame(float time);
// A capture named so at the next frame's end (tests that drive the launcher: TTYD_PADBIND_TEST), while
// $TTYD_LAUNCHER_SHOT asks for any (so the textures are kept).
void launcherShotNamed(const char* name);
