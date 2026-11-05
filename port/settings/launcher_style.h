// The launcher's stage and type (launcher_style.cpp): a paper theatre in the
// spirit of the game's stage, drawn from shapes and the Fredoka typeface, no game
// assets. Its widgets, in the game's menu style: launcher_look.h.
#pragma once

#include <imgui.h>

struct LauncherFonts {
  ImFont* title = nullptr;   // the game's name
  ImFont* logo = nullptr;    // the wordmark's letters, large (ASCII only; launcher_brand.cpp)
  ImFont* heading = nullptr; // card headings, the Play button
  ImFont* body = nullptr;    // everything else
};
extern LauncherFonts gLauncherFonts;

// colours of the paper cards (ImGui's ABGR)
constexpr ImU32 kInk = IM_COL32(59, 36, 22, 255);       // dark brown text
constexpr ImU32 kPaper = IM_COL32(255, 247, 228, 255);  // cream card face
constexpr ImU32 kCurtain = IM_COL32(196, 32, 42, 255);  // stage red
constexpr ImU32 kGold = IM_COL32(246, 196, 69, 255);    // trim, stars

// The device's UI scale: 1.6 on phones and tablets, else 1.
float launcherFontsScale();
// The UI's scale this frame: the device's (or touch, if larger) times the
// window's size against 1280x800, from 1x up to 2x. Text, spacing and
// controls all follow it (launcherStyleInit), so the pages grow with the window.
float launcherUiScale(float touch = 1.f);
// The fonts, at start (the font atlas is rebuilt before the next frame).
extern "C" void portUiInit(void);

// The paper style for the frame's launcher or settings page at UI scale
// scale (text included), and ImGui's own put back after it (the overlays).
void launcherStyleInit(float scale);
void launcherStyleEnd();
// The paper style's sizes and colours alone, into s (another Dear ImGui
// context's: the second screen, port/settings/features/companion.cpp).
void launcherPaperStyle(ImGuiStyle& s, float scale);

// The stage behind everything: backdrop, stars, spotlight, curtains,
// valance. time in seconds (the stars twinkle). Returns the open stage's
// rectangle (inside the curtains, below the valance) in min/max.
void launcherBackdrop(float time, float scale, ImVec2* min, ImVec2* max);

// One stage curtain from x0 to x1 (its folds, its inner edge's shadow and gold
// tie-back; left: the left one), as the backdrop's (the pop-up's parting ones).
void launcherCurtain(ImDrawList* dl, float x0, float x1, float top, float bottom, bool left, float scale);

// The red button with white lettering, for the main action (Dear ImGui's; the
// launcher's pages use launcherButton, launcher_look.h).
bool launcherRedButton(const char* label, ImVec2 size = ImVec2(0, 0));

// A paper window: a torn sheet with an ink edge (launcherPanelPaper, launcher_look.h).
void launcherPaperCard(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale);

// Text with a cut-out outline and a drop shadow (the title).
void launcherOutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* text, ImU32 fill,
                          ImU32 outline, float thickness);
// The Paper Cut title as the README banner draws it: cream, a red outline, a
// soft shadow.
void launcherTitleText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* text);
// A four-pointed gold sparkle of tip radius r.
void launcherSparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 color);
