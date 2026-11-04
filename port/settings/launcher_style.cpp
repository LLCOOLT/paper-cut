// The launcher's look: a paper theatre like the game's battle stage (red
// curtains with gold trim, a starry backdrop, a spotlight) and paper cut-out
// cards, all drawn here from shapes; text in Fredoka (embedded font).
#include "launcher_style.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

#include <cmath>
#include <cstdint>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

extern "C" {
extern const unsigned char portFontBold[];
extern const unsigned int portFontBold_size;
extern const unsigned char portFontMedium[];
extern const unsigned int portFontMedium_size;
}
// tr.cpp: the system's Japanese merged into the font just added (or nullptr)
ImFont* portTrMergeJapanese(float px);

LauncherFonts gLauncherFonts;

namespace {

ImGuiStyle sSaved;
float sSavedFontScale = 1.f;
constexpr float kFontLoadScale = 2.f; // fonts are loaded at twice their 1x size
bool sStyled = false;

ImFont* addFont(const unsigned char* data, unsigned int size, float px) {
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false; // the embedded array stays ours
  cfg.OversampleH = 2;
  cfg.OversampleV = 2;
  // Latin-1 too: disc file names and the European languages' names
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0};
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), px,
                                                    &cfg, kRanges);
}

ImVec4 rgba(ImU32 c, float alpha = 1.f) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.w *= alpha;
  return v;
}

// a repeatable pseudo-random number in [0, 1) for star i, part k
float hash(int i, int k) {
  uint32_t x = static_cast<uint32_t>(i) * 747796405u + static_cast<uint32_t>(k) * 2891336453u + 12345u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}

// a four-point sparkle, like the game's stars
void sparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
  const float k = r * 0.28f;
  ImVec2 pts[8] = {{c.x, c.y - r}, {c.x + k, c.y - k}, {c.x + r, c.y}, {c.x + k, c.y + k},
                   {c.x, c.y + r}, {c.x - k, c.y + k}, {c.x - r, c.y}, {c.x - k, c.y - k}};
  dl->AddConvexPolyFilled(pts, 8, col);
}

// one curtain: folds as vertical bands, darker where the cloth turns away
void curtain(ImDrawList* dl, float x0, float x1, float top, float bottom, bool left, float scale) {
  const int folds = 5;
  const float w = (x1 - x0) / folds;
  for (int i = 0; i < folds; i++) {
    const float a = x0 + w * i;
    const ImU32 light = IM_COL32(214, 44, 52, 255);
    const ImU32 dark = IM_COL32(120, 14, 26, 255);
    dl->AddRectFilledMultiColor(ImVec2(a, top), ImVec2(a + w * 0.5f, bottom), dark, light, light, dark);
    dl->AddRectFilledMultiColor(ImVec2(a + w * 0.5f, top), ImVec2(a + w, bottom), light, dark, dark, light);
  }
  // the inner edge's shadow on the stage, and its gold rope tie-back
  const float edge = left ? x1 : x0;
  const float dir = left ? 1.f : -1.f;
  dl->AddRectFilledMultiColor(ImVec2(std::fmin(edge, edge + dir * 18.f * scale), top),
                              ImVec2(std::fmax(edge, edge + dir * 18.f * scale), bottom),
                              left ? IM_COL32(0, 0, 0, 110) : IM_COL32(0, 0, 0, 0),
                              left ? IM_COL32(0, 0, 0, 0) : IM_COL32(0, 0, 0, 110),
                              left ? IM_COL32(0, 0, 0, 0) : IM_COL32(0, 0, 0, 110),
                              left ? IM_COL32(0, 0, 0, 110) : IM_COL32(0, 0, 0, 0));
  const float y = top + (bottom - top) * 0.62f;
  dl->AddRectFilled(ImVec2(x0, y - 5.f * scale), ImVec2(x1, y + 5.f * scale), kGold, 5.f * scale);
  dl->AddCircleFilled(ImVec2(edge, y), 9.f * scale, kGold);
  dl->AddCircleFilled(ImVec2(edge, y), 5.f * scale, IM_COL32(204, 146, 30, 255));
}

} // namespace

float launcherFontsScale() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
  return 1.6f; // phones and tablets: bigger text, sized for touch
#else
  return 1.f;
#endif
}

float launcherUiScale(float touch) {
  const ImVec2 d = ImGui::GetIO().DisplaySize;
  if (touch > 1.f || launcherFontsScale() > 1.f) {
    // phones and tablets (sizes in points): from the screen's short side, so
    // a phone either way up (about 400) gets about 0.9 (16 pt text, 43 pt
    // buttons) and a tablet the full touch size
    const float shortSide = std::fmin(d.x, d.y);
    return std::fmin(std::fmax(touch, launcherFontsScale()), std::fmax(0.85f, shortSide / 440.f));
  }
  // the window's size against a 1280x800 one, 1x to 2x
  const float fit = std::fmin(d.x / 1280.f, d.y / 800.f);
  return std::fmin(2.f, std::fmax(1.f, fit));
}

extern "C" void portUiInit(void) {
  if (gLauncherFonts.title != nullptr || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  // at the largest scale the UI grows to, drawn smaller (FontGlobalScale), so text stays crisp
  const float load = kFontLoadScale * launcherFontsScale();
  // the first font is ImGui's default (the overlays'): keep it ImGui's own
  if (ImGui::GetIO().Fonts->Fonts.empty()) {
    ImGui::GetIO().Fonts->AddFontDefault();
  }
  // each with the system's Japanese merged in (the menus in Japanese, port/settings/tr.cpp)
  gLauncherFonts.title = addFont(portFontBold, portFontBold_size, 42.f * load);
  portTrMergeJapanese(42.f * load);
  gLauncherFonts.heading = addFont(portFontBold, portFontBold_size, 23.f * load);
  portTrMergeJapanese(23.f * load);
  gLauncherFonts.body = addFont(portFontMedium, portFontMedium_size, 18.f * load);
  portTrMergeJapanese(18.f * load);
}

void launcherStyleInit(float scale) {
  if (sStyled) {
    return;
  }
  sStyled = true;
  portUiInit();
  sSavedFontScale = ImGui::GetIO().FontGlobalScale;
  ImGui::GetIO().FontGlobalScale = scale / (kFontLoadScale * launcherFontsScale());
  ImGuiStyle& s = ImGui::GetStyle();
  sSaved = s;
  s.WindowRounding = 14.f * scale;
  s.ChildRounding = 14.f * scale;
  s.FrameRounding = 10.f * scale;
  s.PopupRounding = 10.f * scale;
  s.GrabRounding = 10.f * scale;
  s.TabRounding = 10.f * scale;
  s.ScrollbarRounding = 10.f * scale;
  s.WindowPadding = ImVec2(18.f * scale, 16.f * scale);
  s.FramePadding = ImVec2(12.f * scale, 7.f * scale);
  s.ItemSpacing = ImVec2(10.f * scale, 9.f * scale);
  s.WindowBorderSize = 0.f;
  s.ChildBorderSize = 0.f;
  s.FrameBorderSize = 1.f;
  s.TabBorderSize = 0.f;
  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = rgba(kInk);
  c[ImGuiCol_TextDisabled] = rgba(IM_COL32(128, 98, 74, 255));
  c[ImGuiCol_WindowBg] = rgba(kPaper, 0.f);
  c[ImGuiCol_ChildBg] = rgba(kPaper, 0.f);
  c[ImGuiCol_PopupBg] = rgba(kPaper);
  c[ImGuiCol_Border] = rgba(IM_COL32(190, 156, 116, 255));
  c[ImGuiCol_FrameBg] = rgba(IM_COL32(232, 210, 172, 255));
  c[ImGuiCol_FrameBgHovered] = rgba(IM_COL32(248, 234, 204, 255));
  c[ImGuiCol_FrameBgActive] = rgba(IM_COL32(252, 222, 150, 255));
  // ordinary buttons are paper; the main action is red (launcherRedButton)
  c[ImGuiCol_Button] = rgba(IM_COL32(236, 214, 176, 255));
  c[ImGuiCol_ButtonHovered] = rgba(IM_COL32(252, 222, 150, 255));
  c[ImGuiCol_ButtonActive] = rgba(kGold);
  c[ImGuiCol_Header] = rgba(kGold, 0.55f);
  c[ImGuiCol_HeaderHovered] = rgba(kGold, 0.35f);
  c[ImGuiCol_HeaderActive] = rgba(kGold, 0.75f);
  c[ImGuiCol_Tab] = rgba(IM_COL32(236, 214, 176, 255));
  c[ImGuiCol_TabHovered] = rgba(IM_COL32(252, 222, 150, 255));
  c[ImGuiCol_TabSelected] = rgba(kPaper);
  c[ImGuiCol_TabSelectedOverline] = rgba(kCurtain);
  c[ImGuiCol_CheckMark] = rgba(kCurtain);
  c[ImGuiCol_SliderGrab] = rgba(kCurtain);
  c[ImGuiCol_SliderGrabActive] = rgba(IM_COL32(160, 20, 32, 255));
  c[ImGuiCol_Separator] = rgba(IM_COL32(222, 196, 160, 255));
  c[ImGuiCol_ScrollbarBg] = rgba(kPaper, 0.f);
  c[ImGuiCol_ScrollbarGrab] = rgba(IM_COL32(214, 186, 146, 255));
  c[ImGuiCol_NavCursor] = rgba(kInk); // the controller's focus: dark, to show on the cream cards and the red buttons
}

bool launcherRedButton(const char* label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, rgba(kCurtain));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgba(IM_COL32(226, 62, 64, 255)));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgba(IM_COL32(160, 20, 32, 255)));
  ImGui::PushStyleColor(ImGuiCol_Text, rgba(IM_COL32(255, 255, 255, 255)));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return pressed;
}

void launcherStyleEnd() {
  if (sStyled) {
    ImGui::GetStyle() = sSaved;
    ImGui::GetIO().FontGlobalScale = sSavedFontScale;
    sStyled = false;
  }
}

void launcherBackdrop(float time, float scale, ImVec2* min, ImVec2* max) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  // the night-sky backdrop of the stage
  dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(24, 28, 74, 255), IM_COL32(24, 28, 74, 255),
                              IM_COL32(70, 44, 110, 255), IM_COL32(70, 44, 110, 255));
  for (int i = 0; i < 90; i++) {
    const ImVec2 p(hash(i, 0) * size.x, hash(i, 1) * size.y * 0.8f);
    const float tw = 0.55f + 0.45f * std::sin(time * (0.8f + hash(i, 2) * 1.6f) + hash(i, 3) * 6.283f);
    const float r = (1.2f + hash(i, 4) * 2.2f) * scale;
    const ImU32 col = IM_COL32(255, 244, 200, static_cast<int>(200 * tw));
    if (hash(i, 5) < 0.12f) {
      sparkle(dl, p, r * 3.f, col);
    } else {
      dl->AddCircleFilled(p, r, col);
    }
  }
  // the stage floor, and the spotlight on it
  const float floorTop = size.y * 0.86f;
  dl->AddRectFilledMultiColor(ImVec2(0, floorTop), size, IM_COL32(150, 96, 52, 255), IM_COL32(150, 96, 52, 255),
                              IM_COL32(92, 56, 28, 255), IM_COL32(92, 56, 28, 255));
  for (int i = 1; i < 4; i++) {
    const float y = floorTop + (size.y - floorTop) * i / 4.f;
    dl->AddLine(ImVec2(0, y), ImVec2(size.x, y), IM_COL32(80, 46, 22, 160), 2.f * scale);
  }
  for (int i = 6; i >= 1; i--) {
    dl->AddEllipseFilled(ImVec2(size.x * 0.5f, floorTop), ImVec2(size.x * 0.06f * i, size.y * 0.03f * i),
                         IM_COL32(255, 240, 190, 12));
  }
  // curtains at the sides, the valance across the top with its gold trim
  const float side = std::fmax(36.f * scale, size.x * 0.075f);
  const float valance = std::fmax(30.f * scale, size.y * 0.075f);
  curtain(dl, 0, side, 0, size.y, true, scale);
  curtain(dl, size.x - side, size.x, 0, size.y, false, scale);
  dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, valance), IM_COL32(150, 18, 30, 255),
                              IM_COL32(150, 18, 30, 255), kCurtain, kCurtain);
  const float scallop = 34.f * scale;
  for (float x = scallop * 0.5f; x < size.x + scallop; x += scallop) {
    dl->AddCircleFilled(ImVec2(x, valance), scallop * 0.5f, kCurtain);
  }
  dl->AddRectFilled(ImVec2(0, valance - 4.f * scale), ImVec2(size.x, valance + 1.f * scale), kGold);
  for (float x = scallop * 0.5f; x < size.x + scallop; x += scallop) {
    dl->AddCircleFilled(ImVec2(x, valance + scallop * 0.5f), 4.f * scale, kGold);
  }
  *min = ImVec2(side + 12.f * scale, valance + scallop * 0.5f + 10.f * scale);
  *max = ImVec2(size.x - side - 12.f * scale, floorTop + (size.y - floorTop) * 0.35f); // standing on the stage
  // and clear of a phone's notch, Dynamic Island and home indicator (the
  // backdrop runs under them)
  SDL_Rect safe;
  int ww = 0, wh = 0;
  SDL_Window* w = aurora_get_window();
  if (w != nullptr && SDL_GetWindowSafeArea(w, &safe) && SDL_GetWindowSize(w, &ww, &wh) && ww > 0 && wh > 0) {
    const float kx = size.x / ww, ky = size.y / wh; // window units to ImGui's
    min->x = std::fmax(min->x, safe.x * kx + 8.f * scale);
    min->y = std::fmax(min->y, safe.y * ky + 4.f * scale);
    max->x = std::fmin(max->x, (safe.x + safe.w) * kx - 8.f * scale);
    max->y = std::fmin(max->y, (safe.y + safe.h) * ky - 4.f * scale);
  }
}

void launcherPaperCard(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale) {
  const float r = 14.f * scale;
  dl->AddRectFilled(ImVec2(a.x + 6.f * scale, a.y + 7.f * scale), ImVec2(b.x + 6.f * scale, b.y + 7.f * scale),
                    IM_COL32(20, 10, 30, 120), r);
  dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255), r);
  dl->AddRectFilled(ImVec2(a.x + 4.f * scale, a.y + 4.f * scale), ImVec2(b.x - 4.f * scale, b.y - 4.f * scale), kPaper,
                    r - 3.f * scale);
}

void launcherOutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* text, ImU32 fill,
                          ImU32 outline, float thickness) {
  dl->AddText(font, size, ImVec2(pos.x + thickness * 1.6f, pos.y + thickness * 2.f), IM_COL32(20, 10, 30, 150), text);
  for (int i = 0; i < 16; i++) {
    const float a = i * 6.283185f / 16.f;
    dl->AddText(font, size, ImVec2(pos.x + std::cos(a) * thickness, pos.y + std::sin(a) * thickness), outline, text);
  }
  dl->AddText(font, size, pos, fill, text);
}

void launcherTitleText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, const char* text) {
  // the README banner's title (tools/port/make_readme_art.py), from the font's
  // em S = 0.826 x ImGui's size: outline 0.0565 S, the outlined shape's shadow
  // at (1.6, 2.0) x the outline, softened by stamps around it
  const float t = 0.0467f * size;
  const ImVec2 off(pos.x + 1.6f * t, pos.y + 2.f * t);
  for (int i = 0; i < 12; i++) {
    const float a = i * 6.283185f / 12.f;
    const float r = t * (i % 2 == 0 ? 1.4f : 0.8f);
    dl->AddText(font, size, ImVec2(off.x + std::cos(a) * r, off.y + std::sin(a) * r), IM_COL32(20, 10, 30, 34), text);
  }
  for (int i = 0; i < 16; i++) {
    const float a = i * 6.283185f / 16.f;
    dl->AddText(font, size, ImVec2(pos.x + std::cos(a) * t, pos.y + std::sin(a) * t), kCurtain, text);
  }
  dl->AddText(font, size, pos, kPaper, text);
}

void launcherSparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 color) {
  // four points, the waist at 0.1875 r (the banner's sparkles)
  const float k = r * 0.1875f;
  const ImVec2 pts[8] = {{c.x, c.y - r}, {c.x + k, c.y - k}, {c.x + r, c.y}, {c.x + k, c.y + k},
                         {c.x, c.y + r}, {c.x - k, c.y + k}, {c.x - r, c.y}, {c.x - k, c.y - k}};
  dl->AddConvexPolyFilled(pts, 8, color);
}
