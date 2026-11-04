// Autosave at room changes (port/src/aurora/autosave_aurora.c): the setting
// on the Gameplay tab, and a small "Saved" mark in the corner for a moment
// after each autosave (or "Autosave failed" when the card couldn't be written).
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>

extern "C" {
#include "port/resume.h"
}

namespace {

constexpr const char* kChoices[] = {"Off", "Every room", "At a room change, every 2 minutes",
                                    "At a room change, every 5 minutes", "At a room change, every 10 minutes"};

void drawMark(int pageOpen) {
  const unsigned long long at = gPortAutosaveAt;
  if (at == 0 || pageOpen) {
    return;
  }
  const Uint64 age = SDL_GetTicks() - at;
  constexpr Uint64 kShow = 1500, kFade = 400;
  if (age >= kShow + kFade) {
    return;
  }
  const float alpha = age < kShow ? 1.f : 1.f - static_cast<float>(age - kShow) / kFade;
  const char* text = gPortAutosaveFailed ? "Autosave failed" : "Saved";
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float u = std::max(1.f, std::min(display.x, display.y) / 480.f);
  ImFont* font = ImGui::GetFont();
  const float fs = 14.f * u;
  const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
  const float pad = 8.f * u, icon = fs;
  const ImVec2 size(ts.x + icon + pad * 3, ts.y + pad * 2);
  const ImVec2 p(display.x - size.x - 16.f * u, display.y - size.y - 16.f * u);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const auto a = [&](int v) { return static_cast<int>(v * alpha); };
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(20, 20, 24, a(190)), 8.f * u);
  const ImU32 fg = gPortAutosaveFailed ? IM_COL32(255, 120, 110, a(240)) : IM_COL32(255, 255, 255, a(235));
  // a memory card: a rounded block with a notch of contacts
  const ImVec2 c0(p.x + pad, p.y + (size.y - icon) * 0.5f), c1(c0.x + icon * 0.8f, c0.y + icon);
  dl->AddRectFilled(c0, c1, fg, 2.f * u);
  dl->AddRectFilled(ImVec2(c0.x + icon * 0.15f, c0.y + icon * 0.12f), ImVec2(c1.x - icon * 0.15f, c0.y + icon * 0.35f),
                    IM_COL32(20, 20, 24, a(190)));
  dl->AddText(font, fs, ImVec2(p.x + pad * 2 + icon, p.y + pad), fg, text);
}

struct AutosaveSettings {
  AutosaveSettings() {
    portSettingsDeclare("autosave", 0, 0, 4);
    portSettingsAddChoice("Gameplay", "Autosave",
                          "Saves into the file you're playing when you go into another room, once you have the "
                          "controls there, as a Save Block would: a crash or a closed window costs no more than "
                          "the room. Not during the boss rush or endless Glitz Pit, or after the practice tools "
                          "changed the game.",
                          PORT_SETTING_CHANGES_SAVES, "autosave", kChoices, 5,
                          "auto save room door map memory card save block");
    portSettingsAddOverlay(drawMark, nullptr);
  }
} sAutosave;

} // namespace
