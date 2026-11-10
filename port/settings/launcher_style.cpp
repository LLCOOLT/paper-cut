// The launcher's type and style: text in Fredoka (embedded font); Dear ImGui's colours and sizes.
// The stage behind it is launcher_stage.cpp's; the widgets on it (paper windows, the glove, tabs,
// buttons) are launcher_look.cpp's.
#include "launcher_style.h"
#include "launcher_look.h"
#include "port/button_markup.h"

#include <SDL3/SDL.h>

#include <cmath>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

extern "C" {
extern const unsigned char portFontBold[];
extern const unsigned int portFontBold_size;
extern const unsigned char portFontMedium[];
extern const unsigned int portFontMedium_size;
}
// tr.cpp: the system's Japanese and the language packs' letters merged into the font just added
void portTrMergeFonts(const unsigned char* data, unsigned int size, float px);

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
  // Latin-1 too: disc file names and the European languages' names; Latin Extended-A (French
  // oe, and other languages' letters in names), and General Punctuation's dashes, quotes,
  // ellipsis and single guillemets (German low and high quotes), what the built-in
  // translations write, and the euro sign. Only those parts of the blocks: each glyph is atlas
  // memory on a phone, and the rest of General Punctuation (spaces, invisible marks) is never drawn.
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x2027,
                                    0x2030, 0x203A, 0x20AC, 0x20AC, 0};
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), px,
                                                    &cfg, kRanges);
}

ImVec4 rgba(ImU32 c, float alpha = 1.f) {
  ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  v.w *= alpha;
  return v;
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
  // each with the system's Japanese and the language packs' letters merged in (port/settings/tr.cpp)
  gLauncherFonts.title = addFont(portFontBold, portFontBold_size, 42.f * load);
  {
    // the wordmark's (launcher_brand.cpp): large, so the hero title stays crisp; ASCII only, nothing
    // merged, to keep the atlas small
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    static const ImWchar kAscii[] = {0x0020, 0x007E, 0};
    gLauncherFonts.logo = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(portFontBold), static_cast<int>(portFontBold_size), 64.f * load, &cfg, kAscii);
  }
  portTrMergeFonts(portFontBold, portFontBold_size, 42.f * load);
  gLauncherFonts.heading = addFont(portFontBold, portFontBold_size, 23.f * load);
  portTrMergeFonts(portFontBold, portFontBold_size, 23.f * load);
  gLauncherFonts.body = addFont(portFontMedium, portFontMedium_size, 18.f * load);
  portTrMergeFonts(portFontMedium, portFontMedium_size, 18.f * load);
  // the text's button icons, in the text's fonts (not the title's or the wordmark's: port/include/port/button_markup.h)
  portButtonGlyphsInit(30.f * load);
}

void launcherStyleInit(float scale) {
  if (sStyled) {
    return;
  }
  sStyled = true;
  portUiInit();
  sSavedFontScale = ImGui::GetIO().FontGlobalScale;
  ImGui::GetIO().FontGlobalScale = scale / (kFontLoadScale * launcherFontsScale());
  sSaved = ImGui::GetStyle();
  launcherPaperStyle(ImGui::GetStyle(), scale);
}

void launcherPaperStyle(ImGuiStyle& s, float scale) {
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
  c[ImGuiCol_TextDisabled] = rgba(IM_COL32(108, 80, 58, 255)); // (the help under each setting: readable on the cream)
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
  // the controller's focus: the launcher's glove points at it (launcher_look.cpp); a gold line too, for
  // the settings page in the game (no glove there)
  c[ImGuiCol_NavCursor] = rgba(IM_COL32(214, 150, 30, 255));
  // tables (Bindings, the mixer, practice's lists...): a light tan header and lines on the cream card,
  // not Dear ImGui's dark defaults under brown text
  c[ImGuiCol_TableHeaderBg] = rgba(IM_COL32(232, 210, 172, 255));
  c[ImGuiCol_TableBorderStrong] = rgba(IM_COL32(190, 156, 116, 255));
  c[ImGuiCol_TableBorderLight] = rgba(IM_COL32(222, 196, 160, 255));
  c[ImGuiCol_TableRowBg] = rgba(kPaper, 0.f);
  c[ImGuiCol_TableRowBgAlt] = rgba(IM_COL32(236, 214, 176, 255), 0.35f);
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

void launcherPaperCard(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale) {
  launcherPanelPaper(dl, a, b, scale); // the launcher's paper window (launcher_look.cpp)
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
