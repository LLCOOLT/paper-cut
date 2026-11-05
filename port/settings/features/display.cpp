// The v0.2 display options' controls (Display tab): their settings are
// declared where they apply (port/src/aurora/widescreen_aurora.c, hud_aurora.c,
// display_aurora.c; port/include/port/display.h). Off or as before by default.
#include "port/settings.h"
#include "port/tr.h"
#include "port/display.h"
#include "port/pause_forms.h"
#include "port/widescreen.h"
#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr const char* kAspects[] = {"Widescreen switch", "4:3", "16:9", "16:10 (Steam Deck)",
                                    "21:9 (ultrawide)", "32:9 (super ultrawide)", "The window's"};
constexpr const char* kAspectMax[] = {"No limit", "16:9", "21:9"};
constexpr const char* kAniso[] = {"Off (the game's)", "2x", "4x", "8x", "16x"};
constexpr const char* kLodBias[] = {"The game's", "Sharper (-0.5)", "Sharper still (-1)", "Sharpest (-1.5)",
                                    "Softer (+0.5)"};
constexpr const char* kUpscale[] = {"Off", "2x", "4x"};
constexpr const char* kSupersample[] = {"Off", "2x", "3x", "4x"};
constexpr const char* kPost[] = {"Off", "CRT", "Paper grain", "Sharpen", "Cut-paper outline", "Folding light"};
constexpr const char* kPerf[] = {"Off", "Frame-time graph", "Graph and timings"};

// HDR output: the switch and its two sliders, greyed out (with why) where the display or the graphics
// backend cannot show HDR; a switch left on from elsewhere can still be turned off
void hdrSection(int) {
  int* on = portSettingsFind("hdr");
  int* white = portSettingsFind("hdr_paper_white");
  int* boost = portSettingsFind("hdr_boost");
  if (on == nullptr || white == nullptr || boost == nullptr) {
    return;
  }
  const bool supported = portHdrSupported() != 0;
  bool edited = false;
  ImGui::BeginDisabled(!supported && *on == 0);
  bool v = *on != 0;
  if (ImGui::Checkbox(portTrLabel("HDR output"), &v)) {
    *on = v ? 1 : 0;
    edited = true;
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!supported || *on == 0);
  // the value follows the slider as it moves (portDisplayFrame gives it to Aurora); saved when let go
  ImGui::SliderInt(portTrLabel("Paper white"), white, 80, 400, "%d nits");
  edited |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt(portTrLabel("Highlight boost"), boost, 0, 50, "+%d%%");
  edited |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();
  if (!supported) {
    ImGui::TextDisabled("%s", portTr("Not available here."));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", portTr("Needs a display with HDR turned on (on Windows: Settings, System, Display, Use HDR) and "
                       "the Direct3D or Metal graphics backend. Vulkan and OpenGL, as on Linux and Android, show "
                       "SDR only."));
    ImGui::PopStyleColor();
  } else if (portHdrActive()) {
    ImGui::TextDisabled("%s", portTr("Showing in HDR."));
  }
  if (edited) {
    portSettingsSave();
    portHdrApply();
  }
}

// The Start menu's form (port/include/port/pause_forms.h): the switch and its two numbers, dimmed as the page has them
enum { kHdrOn, kHdrWhite, kHdrBoost };

int hdrRows(PortPauseRow* out, int max) {
  int* on = portSettingsFind("hdr");
  int* white = portSettingsFind("hdr_paper_white");
  int* boost = portSettingsFind("hdr_boost");
  if (on == nullptr || white == nullptr || boost == nullptr || max < 3) {
    return 0;
  }
  const bool supported = portHdrSupported() != 0;
  const char* why = supported ? (portHdrActive() ? "Showing in HDR." : "On an HDR display, the picture at the brightness you choose for white.")
                              : "Not available here: needs a display with HDR turned on and the Direct3D or Metal graphics backend.";
  out[0] = portPauseRow("HDR output", why, *on ? "On" : "Off", kHdrOn, !supported && *on == 0 ? PORT_PAUSE_ROW_DIM : 0);
  const unsigned dim = !supported || *on == 0 ? PORT_PAUSE_ROW_DIM : 0;
  out[1] = portPauseRow("Paper white", "How bright white is, in nits: left and right change it.", nullptr, kHdrWhite, dim);
  std::snprintf(out[1].value, sizeof(out[1].value), portTrGame("%d nits"), *white);
  out[2] = portPauseRow("Highlight boost", "The brightest highlights a little brighter still.", nullptr, kHdrBoost, dim);
  std::snprintf(out[2].value, sizeof(out[2].value), "+%d%%", *boost);
  return 3;
}

const char* hdrAct(int id, int dir) {
  int* on = portSettingsFind("hdr");
  int* white = portSettingsFind("hdr_paper_white");
  int* boost = portSettingsFind("hdr_boost");
  if (on == nullptr || white == nullptr || boost == nullptr) {
    return nullptr;
  }
  const bool supported = portHdrSupported() != 0;
  if (id == kHdrOn) {
    if (!supported && *on == 0) {
      return "Not available here.";
    }
    *on = !*on;
  } else if (!supported || *on == 0) {
    return supported ? "Turn on HDR output first." : "Not available here.";
  } else if (id == kHdrWhite) {
    *white = std::clamp(*white + (dir < 0 ? -20 : 20), 80, 400);
  } else {
    *boost = std::clamp(*boost + (dir < 0 ? -5 : 5), 0, 50);
  }
  portSettingsSave();
  portHdrApply();
  return nullptr;
}

const PortPauseForm kHdrForm = {"Display", "HDR output", hdrRows, hdrAct, PORT_PAUSE_FORM_OWN};

const bool sAdded = [] {
  // declared here too, at start, with their defaults, so the launcher's Settings tab has them before
  // the game first draws (where they apply declares them again: one value)
  portSettingsDeclare("aspect_ratio", PORT_ASPECT_SWITCH, 0, PORT_ASPECT_WINDOW);
  portSettingsDeclare("aspect_max", 0, 0, 2);
  portSettingsDeclare("aspect_scene_limits", 1, 0, 1);
  portSettingsDeclare("hud_scale", 100, 50, 150);
  portSettingsDeclare("text_scale", 100, 100, 150);
  portSettingsDeclare("aniso", 0, 0, 4);
  portSettingsDeclare("lod_bias", PORT_LOD_GAME, 0, PORT_LOD_COUNT - 1);
  portSettingsDeclare("texture_upscale", 0, 0, 2);
  portSettingsDeclare("supersample", 0, 0, 3);
  portSettingsDeclare("post_filter", 0, 0, 5);
  portSettingsDeclare("vrr", 0, 0, 1);
  portSettingsDeclare("perf_overlay", 0, 0, 2);
  portSettingsDeclare("hdr", 0, 0, 1);
  portSettingsDeclare("hdr_paper_white", 200, 80, 400);
  portSettingsDeclare("hdr_boost", 0, 0, 50);
  portSettingsAddChoice("Display", "Aspect ratio",
                        "Wider shows more of the sides of each scene; much of the game was built for 4:3, so rooms "
                        "and scenes can end before the screen's edges.",
                        0, "aspect_ratio", kAspects, 7, "ultrawide 21:9 32:9 16:10 steam deck widescreen");
  portSettingsAddChoice("Display", "Widest picture",
                        "Past this the rest of the screen is bars (blurred with Blurred side bars).", 0, "aspect_max",
                        kAspectMax, 3, "ultrawide limit clamp pillarbox");
  // port/src/aurora/widescreen_aurora.c
  portSettingsAddToggle("Display", "Bars where a scene ends",
                        "In the rooms whose sets end before a wide screen's edges, and in battles past 16:9, bars "
                        "beside the picture instead of the empty space past the set.",
                        0, "aspect_scene_limits", "ultrawide room void black edges pillarbox battle");
  portSettingsAddSlider("Interface", "HUD size",
                        "The status bar, message windows and battle menus, larger or smaller than the game's.", 0,
                        "hud_scale", 50, 150, "%d%%", "hud scale ui interface status bar text size");
  // port/src/aurora/hud_aurora.c
  portSettingsAddSlider("Accessibility", "Message size",
                        "The message and choice windows larger, on top of HUD size. A window grows only while it fits "
                        "the screen: a little in 4:3, up to about 145% in widescreen.",
                        0, "text_scale", 100, 150, "%d%%", "bigger text font message dialogue window large read");
  portSettingsAddChoice("Graphics", "Anisotropic filtering", "Textures seen at an angle (floors, paths) stay sharp.", 0,
                        "aniso", kAniso, 5, "aniso texture filtering af");
  portSettingsAddChoice("Graphics", "Texture detail", "Which of a texture's detail levels is used at a distance.", 0,
                        "lod_bias", kLodBias, 5, "lod bias mipmap texture sharp");
  // port/src/aurora/upscale_aurora.c
  portSettingsAddChoice("Graphics", "Texture upscaling",
                        "The game's textures made larger by an edge-following filter as they load: smoother lines "
                        "on paper and signs. More video memory. From the next start.",
                        PORT_SETTING_NEXT_START, "texture_upscale", kUpscale, 3,
                        "upscale texture hd enhance resolution filter smooth");
  portSettingsAddChoice("Graphics", "Supersampling",
                        "Draws the picture several times larger than the window and shrinks it: smoother edges, "
                        "much more work for the graphics card.",
                        0, "supersample", kSupersample, 4, "ssaa anti-aliasing downsample resolution");
  portSettingsAddChoice("Graphics", "Picture filter",
                        "Over the whole picture. Cut-paper outline: an ink line and a pale cut edge round each "
                        "shape, as pieces of paper laid on each other. Folding light: the picture on a folded sheet, "
                        "a slow light moving across its panels.",
                        0, "post_filter", kPost, 6,
                        "crt scanlines paper grain sharpen post-processing shader cut paper outline ink fold "
                        "folding light crease");
  portSettingsAddToggle("Display", "Variable refresh rate (G-Sync, FreeSync)",
                        "Each frame shown as soon as it is ready, without waiting for the screen. For VRR "
                        "displays (others may tear); goes well with Frame rate: the display's.",
                        0, "vrr", "vrr gsync freesync adaptive sync tearing pacing");
  portSettingsAddSection("Display", "HDR output",
                         "On an HDR display, the picture at the brightness you choose for white (paper white), "
                         "with the brightest highlights optionally a little brighter. The game itself is SDR; this "
                         "does not add detail it does not have.",
                         0, hdrSection, "hdr high dynamic range nits brightness paper white edr scrgb hdr10");
  portPauseAddForm(&kHdrForm);
  portSettingsAddChoice("Interface", "Performance overlay",
                        "How long each frame takes: the time between frames, and the part spent working.", 0,
                        "perf_overlay", kPerf, 3, "frame time graph pacing stutter performance fps");
  return true;
}();

} // namespace
