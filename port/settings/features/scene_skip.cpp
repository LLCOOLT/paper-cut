// Skip any scene and Hold B through text (port/include/port/scene_skip.h): their rows
// on the Gameplay tab, beside Text speed, and the ring drawn over the game while
// START is held in a scene (it fills) and while the scene is skipped (it turns).
#include "port/button_markup.h"
#include "port/scene_skip.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

// the ring at the bottom right: its track, the part held so far (or a turning
// arc while skipping) and the words beside it
void drawRing(int pageOpen) {
  float fill = 0.f;
  const int state = portSceneSkipRing(&fill);
  if (state == 0 || pageOpen) {
    return;
  }
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float u = std::max(1.f, std::min(display.x, display.y) / 480.f); // scale with the window
  const float r = 14.f * u, th = 4.f * u;
  const ImVec2 c(display.x - 28.f * u, display.y - 28.f * u);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (state == 3) {
    // the first skippable scene's hint (once per install): the empty ring and its words, fading in and out,
    // a sweep round the ring now and then to say "hold"
    const float frames = fill;
    const float a = std::clamp(std::min(frames / 20.f, (300.f - frames) / 30.f), 0.f, 1.f);
    const int al = static_cast<int>(a * 255.f);
    dl->AddCircleFilled(c, r + th, IM_COL32(20, 20, 24, al * 170 / 255), 32);
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, al * 90 / 255), 32, th);
    const float sweep = std::fmod(frames, 90.f) / 45.f; // (0..1 fills, then a rest)
    if (sweep < 1.f) {
      const float a0 = -kPi * 0.5f;
      dl->PathArcTo(c, r, a0, a0 + 2.f * kPi * sweep, 32);
      dl->PathStroke(IM_COL32(255, 255, 255, al * 200 / 255), 0, th);
    }
    const char* hint = portButtonsUi(portTr("Hold {START} to skip, {B} to stop"));
    ImFont* hintFont = ImGui::GetFont();
    const float hfs = 15.f * u;
    const ImVec2 hs = hintFont->CalcTextSizeA(hfs, FLT_MAX, 0.f, hint);
    const ImVec2 hat(c.x - r - th - 8.f * u - hs.x, c.y - hs.y * 0.5f);
    dl->AddRectFilled(ImVec2(hat.x - 6.f * u, hat.y - 3.f * u), ImVec2(hat.x + hs.x + 6.f * u, hat.y + hs.y + 3.f * u),
                      IM_COL32(20, 20, 24, al * 170 / 255), 6.f * u);
    dl->AddText(hintFont, hfs, hat, IM_COL32(255, 255, 255, al * 235 / 255), hint);
    return;
  }
  dl->AddCircleFilled(c, r + th, IM_COL32(20, 20, 24, 170), 32);
  dl->AddCircle(c, r, IM_COL32(255, 255, 255, 60), 32, th);
  const ImU32 fg = IM_COL32(255, 255, 255, 235);
  if (state == 1) {
    const float a0 = -kPi * 0.5f;
    dl->PathArcTo(c, r, a0, a0 + 2.f * kPi * std::clamp(fill, 0.f, 1.f), 32);
    dl->PathStroke(fg, 0, th);
  } else {
    const float a0 = static_cast<float>(SDL_GetTicks() % 1000) / 1000.f * 2.f * kPi;
    dl->PathArcTo(c, r, a0, a0 + kPi * 0.6f, 12);
    dl->PathStroke(fg, 0, th);
  }
  // (B stops a skip under way: scene_skip_pc.c)
  const char* text = portButtonsUi(state == 1 ? portTr("Hold {START} to skip, {B} to stop")
                                              : portTr("Skipping... {B} to stop"));
  ImFont* font = ImGui::GetFont();
  const float fs = 15.f * u;
  const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
  const ImVec2 at(c.x - r - th - 8.f * u - ts.x, c.y - ts.y * 0.5f);
  dl->AddRectFilled(ImVec2(at.x - 6.f * u, at.y - 3.f * u), ImVec2(at.x + ts.x + 6.f * u, at.y + ts.y + 3.f * u),
                    IM_COL32(20, 20, 24, 170), 6.f * u);
  dl->AddText(font, fs, at, fg, text);
}

struct SceneSkipSettings {
  SceneSkipSettings() {
    portSettingsDeclare("skip_any_scene", 1, 0, 1); // (on: it acts only while Start is held in a scene)
    portSettingsDeclare("text_hold_skip", 0, 0, 1);
    portSettingsAddToggle("Gameplay", "Skip any scene",
                          "While a scene holds Mario, hold {START} until the ring fills: the scene runs on to its "
                          "end at once, unseen and unheard, so whatever it gives or changes still happens. {B} stops it. It "
                          "stops at a choice, a menu or a battle (they're yours), and never runs during play, "
                          "minigames or online.",
                          0, "skip_any_scene", "skip cutscene scene hold start any fast story");
    portSettingsAddToggle("Gameplay", "Fly through text",
                          "In a conversation, hold {B}: its pages turn as fast as they print, up to the next "
                          "choice. A tap of {B} is the game's own.",
                          0, "text_hold_skip", "text dialogue message skip hold b fast conversation");
    portSettingsAddOverlay(drawRing, nullptr);
  }
} sSceneSkip;

} // namespace
