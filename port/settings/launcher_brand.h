// Paper Cut's own art in the launcher (launcher_brand.cpp), all in one place so a new icon or
// wordmark swaps in here: the title over the stage and its ribbon, and the paper look's own drawn
// pieces (the pointing glove, the torn-paper edges), made from shapes and the Fredoka typeface,
// nothing of the game's. The paper look is the launcher's default; the game look
// (launcher_look.cpp) draws the same widgets with the game's own art from the player's disc.
#pragma once

#include <imgui.h>

#include <vector>

// The title at the top of the stage, the launcher's hero: "Paper Cut" in cut-paper letters (each a
// cut-out with its white paper edge and shadow, a little turned, a pair of scissors cutting down
// between the words) over a paper ribbon, "for Paper Mario: The Thousand-Year Door", in the
// launcher's own type (never the game's logo lettering). centreX, left, top, width, height: the
// stage's. compact: a short stage, the wordmark alone at the left, its room in *indent for the tabs
// beside it. Returns the title's bottom.
float launcherBrandTitle(ImDrawList* dl, float centreX, float left, float top, float width, float height, float scale,
                         float time, bool compact, float* indent);

// The brand's own wordmark picture in place of the drawn letters (a Dear ImGui texture and its size
// in pixels); the ribbon stays. Tests: $TTYD_BRAND_WORDMARK=<png> tries one.
void launcherBrandWordmarkImage(ImTextureID tex, float w, float h);

// Paper Cut's app icon, "snipping a star": a gold folded-paper star in a dashed cut line, the
// title's scissors cutting round it, in the title's cut-paper style over the stage's night blue. Full
// bleed in the square at min, size across (the platforms round or mask it). Every icon file is drawn
// from it (tools/brand_export, then tools/port/make_icons.py packs them).
// px: the size it is seen at in pixels when it is drawn bigger to be scaled down (0: size), which picks
// the detail: under 48 the sparkles and the cut line go and the star grows; under 24 the star is flat
// and the scissors are two handles and a blade. parts: the backdrop, the pieces or both (Android's
// adaptive icon takes them as two layers). pieceScale: the pieces shrunk about the centre (Android's
// safe zone).
enum LauncherBrandIconParts : unsigned { kBrandIconBackdrop = 1u, kBrandIconPieces = 2u, kBrandIconAll = 3u };
void launcherBrandIcon(ImDrawList* dl, ImVec2 min, float size, float time, float px = 0.f,
                       unsigned parts = kBrandIconAll, float pieceScale = 1.f);

// The app icon as the platforms show it (port/assets/icon/papercut-512.png: launcherBrandIcon's
// tile with rounded corners, made by tools/port/make_icons.py), 128 px, for the About page; or none.
ImTextureID launcherBrandIconTexture();

// The paper look's pointing glove: a white glove pointing right, its fingertip at tip, h tall.
void launcherBrandGlove(ImDrawList* dl, ImVec2 tip, float h, float alpha);

// A torn-paper outline round the rectangle a..b: points every step along its edges, each pushed in
// or out by up to rough (repeatable for the same seed), clockwise from the top left.
void launcherBrandTornRect(std::vector<ImVec2>& out, ImVec2 a, ImVec2 b, float step, float rough, unsigned seed);
