// The v0.2 display options' controls (Display tab): their settings are
// declared where they apply (port/src/aurora/widescreen_aurora.c, hud_aurora.c,
// display_aurora.c; port/include/port/display.h). Off or as before by default.
#include "port/settings.h"

namespace {

constexpr const char* kAspects[] = {"Widescreen switch", "4:3", "16:9", "16:10 (Steam Deck)",
                                    "21:9 (ultrawide)", "32:9 (super ultrawide)", "The window's"};
constexpr const char* kAspectMax[] = {"No limit", "16:9", "21:9"};
constexpr const char* kAniso[] = {"Off (the game's)", "2x", "4x", "8x", "16x"};
constexpr const char* kLodBias[] = {"The game's", "Sharper (-0.5)", "Sharper still (-1)", "Sharpest (-1.5)",
                                    "Softer (+0.5)"};
constexpr const char* kSupersample[] = {"Off", "2x", "3x", "4x"};
constexpr const char* kPost[] = {"Off", "CRT", "Paper grain", "Sharpen"};
constexpr const char* kPerf[] = {"Off", "Frame-time graph", "Graph and timings"};

const bool sAdded = [] {
  portSettingsAddChoice("Display", "Aspect ratio",
                        "Wider shows more of the sides of each scene; much of the game was built for 4:3, so rooms "
                        "and scenes can end before the screen's edges.",
                        0, "aspect_ratio", kAspects, 7, "ultrawide 21:9 32:9 16:10 steam deck widescreen");
  portSettingsAddChoice("Display", "Widest picture",
                        "Past this the rest of the screen is bars (blurred with Blurred side bars).", 0, "aspect_max",
                        kAspectMax, 3, "ultrawide limit clamp pillarbox");
  portSettingsAddSlider("Display", "HUD size",
                        "The status bar, message windows and battle menus, larger or smaller than the game's.", 0,
                        "hud_scale", 50, 150, "%d%%", "hud scale ui interface status bar text size");
  portSettingsAddChoice("Display", "Anisotropic filtering", "Textures seen at an angle (floors, paths) stay sharp.", 0,
                        "aniso", kAniso, 5, "aniso texture filtering af");
  portSettingsAddChoice("Display", "Texture detail", "Which of a texture's detail levels is used at a distance.", 0,
                        "lod_bias", kLodBias, 5, "lod bias mipmap texture sharp");
  portSettingsAddChoice("Display", "Supersampling",
                        "Draws the picture several times larger than the window and shrinks it: smoother edges, "
                        "much more work for the graphics card.",
                        0, "supersample", kSupersample, 4, "ssaa anti-aliasing downsample resolution");
  portSettingsAddChoice("Display", "Picture filter", nullptr, 0, "post_filter", kPost, 4,
                        "crt scanlines paper grain sharpen post-processing shader");
  portSettingsAddToggle("Display", "Variable refresh rate (G-Sync, FreeSync)",
                        "Each frame shown as soon as it is ready, without waiting for the screen. For VRR "
                        "displays (others may tear); goes well with Frame rate: the display's.",
                        0, "vrr", "vrr gsync freesync adaptive sync tearing pacing");
  portSettingsAddChoice("Display", "Performance overlay",
                        "How long each frame takes: the time between frames, and the part spent working.", 0,
                        "perf_overlay", kPerf, 3, "frame time graph pacing stutter performance fps");
  return true;
}();

} // namespace
