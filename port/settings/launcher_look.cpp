// The launcher's widgets in the game's menu style, in its two looks (launcher_look.h). The paper look
// draws from shapes (launcher_brand.cpp has Paper Cut's own pieces); the game look draws the same
// widgets with four images the launcher makes from the player's disc (art/recipes/launcher_ui.recipe:
// the glove, the woven paper window, the torn strip, the torn label), read from the art cache.
#include "launcher_look.h"
#include "launcher_brand.h"
#include "launcher_shot.h"
#include "launcher_style.h"

#include "port/art_recipe.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <aurora/imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ---- the look and its art ----

struct Art {
  ImTextureID tex{};
  float w = 0.f, h = 0.f;
  bool ok = false;
};
Art sCursor, sPanel, sStrip, sLabel;
std::string sArtId;   // the game id whose art is loaded
std::string sTriedId; // the last one tried (made or read once, not every frame)

int* styleSetting() {
  static int* s = portSettingsDeclare("launcher_style", 0, 0, 1); // 0 paper, 1 the game's (a trial)
  return s;
}

int* soundSetting() {
  static int* s = portSettingsDeclare("launcher_sounds", 1, 0, 1);
  return s;
}

Art loadArt(const std::string& path) {
  Art a;
  SDL_Surface* s = SDL_LoadPNG(path.c_str());
  if (s == nullptr) {
    return a;
  }
  SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(s);
  if (rgba == nullptr) {
    return a;
  }
  if (rgba->pitch == rgba->w * 4) {
    a.tex = launcherTexture(rgba->w, rgba->h, rgba->pixels);
    a.w = static_cast<float>(rgba->w);
    a.h = static_cast<float>(rgba->h);
    a.ok = true;
  }
  SDL_DestroySurface(rgba);
  return a;
}

bool haveArt() { return sCursor.ok && sPanel.ok && sStrip.ok && sLabel.ok; }

// ---- small helpers ----

ImU32 mix(ImU32 a, ImU32 b, float t) {
  const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::ColorConvertFloat4ToU32(
      ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

ImU32 fade(ImU32 c, float a) {
  const unsigned al = static_cast<unsigned>(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.f, 1.f));
  return (c & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
}

unsigned seedOf(float w, float h, unsigned salt = 0) {
  return static_cast<unsigned>(w) * 2654435761u ^ static_cast<unsigned>(h) * 40503u ^ salt * 97u;
}

unsigned seedOf(const char* text) {
  unsigned h = 2166136261u;
  for (; *text; text++) {
    h = (h ^ static_cast<unsigned char>(*text)) * 16777619u;
  }
  return h;
}

float hash01(unsigned i, unsigned k) {
  uint32_t x = i * 747796405u + k * 2891336453u + 4242u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}

void polyFill(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, ImVec2 off = ImVec2(0, 0)) {
  if (off.x == 0.f && off.y == 0.f) {
    dl->AddConcavePolyFilled(pts.data(), static_cast<int>(pts.size()), col);
    return;
  }
  std::vector<ImVec2> moved(pts);
  for (ImVec2& p : moved) {
    p.x += off.x;
    p.y += off.y;
  }
  dl->AddConcavePolyFilled(moved.data(), static_cast<int>(moved.size()), col);
}

// an image cut in nine: corners of c texels drawn d wide, its edges and middle stretched between them
// (as the game stretches its window's texture; repeats would show seams)
void nineSlice(ImDrawList* dl, const Art& art, ImVec2 a, ImVec2 b, float c, float d, ImU32 tint) {
  const float xs[4] = {a.x, a.x + d, b.x - d, b.x};
  const float ys[4] = {a.y, a.y + d, b.y - d, b.y};
  // (half a texel in from the cuts: the filtered middle never takes in the frame's texels)
  const float us[4] = {0.f, (c + 0.5f) / art.w, 1.f - (c + 0.5f) / art.w, 1.f};
  const float vs[4] = {0.f, (c + 0.5f) / art.h, 1.f - (c + 0.5f) / art.h, 1.f};
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < 3; i++) {
      if (xs[i + 1] > xs[i] && ys[j + 1] > ys[j]) {
        dl->AddImage(art.tex, ImVec2(xs[i], ys[j]), ImVec2(xs[i + 1], ys[j + 1]), ImVec2(us[i], vs[j]),
                     ImVec2(us[i + 1], vs[j + 1]), tint);
      }
    }
  }
}

// an image cut in three across (ends of c texels drawn d wide), the middle stretched
void threeSlice(ImDrawList* dl, const Art& art, ImVec2 a, ImVec2 b, float c, float d, ImU32 tint) {
  const float u = c / art.w;
  d = std::min(d, (b.x - a.x) * 0.5f);
  dl->AddImage(art.tex, a, ImVec2(a.x + d, b.y), ImVec2(0, 0), ImVec2(u, 1), tint);
  dl->AddImage(art.tex, ImVec2(a.x + d, a.y), ImVec2(b.x - d, b.y), ImVec2(u, 0), ImVec2(1 - u, 1), tint);
  dl->AddImage(art.tex, ImVec2(b.x - d, a.y), b, ImVec2(1 - u, 0), ImVec2(1, 1), tint);
}

bool navVisible() {
  ImGuiContext& g = *GImGui;
  return g.NavCursorVisible && g.NavHighlightItemUnderNav;
}

// ---- the cursor ----

struct Target {
  ImVec2 min, max;
  bool set = false;
};
Target sHover, sFallback;
bool sNoGlove = false; // a tab is under the mouse or the focus: no glove this frame
int sTabFocused = -1;  // the tab the focus was on last frame
ImVec2 sGlove(0, 0);
float sGloveAlpha = 0.f;
ImVec2 sLastTarget(-1, -1);
float sLastTime = 0.f, sLastMoveSound = 0.f;

// ---- sounds ----

SDL_AudioStream* sAudio = nullptr;
bool sAudioTried = false;
constexpr int kRate = 44100;

void audioOpen() {
  if (sAudioTried) {
    return;
  }
  sAudioTried = true;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    return;
  }
  const SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, kRate};
  sAudio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (sAudio != nullptr) {
    SDL_ResumeAudioStreamDevice(sAudio);
  }
}

// ---- the page turn ----

int sPage = -1;
float sTurnStart = -10.f;
int sTurnDir = 1;
constexpr float kTurn = 0.34f;

// the tab colours (frame, face), from the Start menu's tabs
const ImU32 kTabFrame[] = {IM_COL32(170, 25, 0, 255), IM_COL32(176, 44, 106, 255), IM_COL32(184, 100, 0, 255),
                           IM_COL32(74, 132, 16, 255), IM_COL32(21, 74, 156, 255)};
const ImU32 kTabFace[] = {IM_COL32(206, 85, 66, 255), IM_COL32(232, 103, 159, 255), IM_COL32(235, 154, 36, 255),
                          IM_COL32(140, 198, 58, 255), IM_COL32(63, 127, 214, 255)};

} // namespace

// ---------------------------------------------------------------------------------------------

bool launcherLookGameWanted() {
  static const char* env = SDL_getenv("TTYD_LAUNCHER_STYLE");
  if (env != nullptr) {
    return std::strcmp(env, "game") == 0;
  }
  return *styleSetting() == 1;
}

LauncherLook launcherLook() { return launcherLookGameWanted() && haveArt() ? LauncherLook::Game : LauncherLook::Paper; }

void launcherLookArt(const char* userPath, const char* gameId, const char* discPath) {
  soundSetting(); // declared before the settings page lists its keys
  if (!launcherLookGameWanted() || userPath == nullptr || gameId == nullptr || gameId[0] == '\0') {
    return;
  }
  if ((sArtId == gameId && haveArt()) || sTriedId == gameId) {
    return;
  }
  sTriedId = gameId;
  if (discPath != nullptr) {
    portArtMakeFolder(userPath, discPath, "art/launcher/"); // from the cache, or made from the disc now
  }
  const std::string dir = std::string(userPath) + "/art_cache/" + gameId + "/art/launcher/ui/";
  Art cursor = loadArt(dir + "cursor.png"), panel = loadArt(dir + "panel.png"), strip = loadArt(dir + "strip.png"),
      label = loadArt(dir + "label.png");
  if (cursor.ok && panel.ok && strip.ok && label.ok) {
    sCursor = cursor;
    sPanel = panel;
    sStrip = strip;
    sLabel = label;
    sArtId = gameId;
  }
}

ImTextureID launcherTexture(int w, int h, const void* rgba) {
  const ImTextureID id = aurora_imgui_add_texture(static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgba);
  launcherShotTexture(id, w, h, rgba);
  return id;
}

// ---- frames and paper ----

void launcherPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale) {
  if (b.x - a.x < 8.f || b.y - a.y < 8.f) {
    return;
  }
  if (launcherLook() == LauncherLook::Game) {
    // the Start menu's window: its texture has 4 clear texels round a dark wavy line; warmed to the
    // menu's cream
    const float d = 34.f * scale; // the game's texels a little larger, as on a big screen
    nineSlice(dl, sPanel, ImVec2(a.x + 5.f * scale, a.y + 7.f * scale), ImVec2(b.x + 5.f * scale, b.y + 7.f * scale),
              24.f, d, IM_COL32(20, 10, 30, 110));
    nineSlice(dl, sPanel, a, b, 24.f, d, IM_COL32(255, 247, 226, 255));
    return;
  }
  launcherPanelPaper(dl, a, b, scale);
}

void launcherPanelPaper(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale) {
  if (b.x - a.x < 8.f || b.y - a.y < 8.f) {
    return;
  }
  // a torn sheet with an ink edge, a cream face and its fibres
  const unsigned seed = seedOf(b.x - a.x, b.y - a.y);
  std::vector<ImVec2> outer, inner;
  launcherBrandTornRect(outer, a, b, 26.f * scale, 2.4f * scale, seed);
  const float edge = 3.f * scale;
  launcherBrandTornRect(inner, ImVec2(a.x + edge, a.y + edge), ImVec2(b.x - edge, b.y - edge), 26.f * scale,
                        2.f * scale, seed + 1);
  polyFill(dl, outer, IM_COL32(20, 10, 30, 110), ImVec2(5.f * scale, 7.f * scale));
  polyFill(dl, outer, IM_COL32(86, 66, 52, 255));
  polyFill(dl, inner, kPaper);
  // the paper's fibres: short faint strokes across the face
  dl->PushClipRect(ImVec2(a.x + edge * 3.f, a.y + edge * 3.f), ImVec2(b.x - edge * 3.f, b.y - edge * 3.f), true);
  const float area = (b.x - a.x) * (b.y - a.y);
  const int count = std::min(400, static_cast<int>(area / (900.f * scale * scale)));
  for (int i = 0; i < count; i++) {
    const ImVec2 p(a.x + hash01(seed, i * 4) * (b.x - a.x), a.y + hash01(seed, i * 4 + 1) * (b.y - a.y));
    const float len = (6.f + hash01(seed, i * 4 + 2) * 10.f) * scale;
    const float ang = 0.7f + hash01(seed, i * 4 + 3) * 0.5f;
    dl->AddLine(p, ImVec2(p.x + std::cos(ang) * len, p.y + std::sin(ang) * len),
                i % 3 == 0 ? IM_COL32(214, 190, 150, 70) : IM_COL32(255, 255, 255, 110), 1.2f * scale);
  }
  dl->PopClipRect();
}

void launcherStrip(ImDrawList* dl, ImVec2 a, ImVec2 b, float scale, ImU32 tint) {
  if (launcherLook() == LauncherLook::Game) {
    threeSlice(dl, sStrip, a, b, 24.f, 24.f * scale, tint);
    return;
  }
  // a cream band, torn tan fibres along its top and bottom (as the game's strip)
  const ImU32 face = mix(IM_COL32(240, 234, 214, 255), tint, (tint & 0x00FFFFFF) == 0x00FFFFFF ? 0.f : 0.55f);
  const ImU32 fibre = mix(IM_COL32(212, 184, 140, 255), tint, (tint & 0x00FFFFFF) == 0x00FFFFFF ? 0.f : 0.3f);
  dl->AddRectFilled(a, b, fade(face, ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.f), 3.f * scale);
  const unsigned seed = seedOf(b.x - a.x, b.y - a.y, 7);
  const float step = 5.f * scale;
  const float alpha = ((tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.f;
  int i = 0;
  for (float x = a.x + 6.f * scale; x < b.x - 6.f * scale; x += step, i++) {
    const float top = (0.6f + hash01(seed, i * 2) * 2.4f) * scale;
    const float bot = (0.6f + hash01(seed, i * 2 + 1) * 2.4f) * scale;
    const float w = std::min(step * (0.6f + hash01(seed, i * 3) * 0.8f), b.x - 6.f * scale - x);
    dl->AddRectFilled(ImVec2(x, a.y + 2.f * scale), ImVec2(x + w, a.y + 2.f * scale + top), fade(fibre, alpha));
    dl->AddRectFilled(ImVec2(x, b.y - 2.f * scale - bot), ImVec2(x + w, b.y - 2.f * scale), fade(fibre, alpha));
  }
}

void launcherHeading(const char* text, float scale) {
  ImGui::PushFont(gLauncherFonts.heading);
  const ImVec2 sz = ImGui::CalcTextSize(text, nullptr, true);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 pad(16.f * scale, 5.f * scale);
  const float w = std::min(sz.x + pad.x * 2.f, ImGui::GetContentRegionAvail().x);
  launcherStrip(ImGui::GetWindowDrawList(), at, ImVec2(at.x + w, at.y + sz.y + pad.y * 2.f), scale);
  ImGui::SetCursorScreenPos(ImVec2(at.x + pad.x, at.y + pad.y));
  ImGui::TextUnformatted(text, ImGui::FindRenderedTextEnd(text));
  ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + sz.y + pad.y * 2.f));
  ImGui::Dummy(ImVec2(0, 2.f * scale));
  ImGui::PopFont();
}

// ---- items ----

bool launcherButton(const char* label, ImVec2 size, float scale, LauncherButton kind) {
  const bool primary = kind == LauncherButton::Primary;
  if (primary) {
    ImGui::PushFont(gLauncherFonts.heading);
  }
  const char* end = ImGui::FindRenderedTextEnd(label);
  const ImVec2 text = ImGui::CalcTextSize(label, end);
  const ImVec2 pad(22.f * scale, (primary ? 12.f : 8.f) * scale);
  if (size.x <= 0.f) {
    size.x = text.x + pad.x * 2.f;
  }
  if (size.y <= 0.f) {
    size.y = text.y + pad.y * 2.f;
  }
  const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
  const bool pressed = ImGui::InvisibleButton(label, size, ImGuiButtonFlags_EnableNav);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
  const bool focused = ImGui::IsItemFocused() && navVisible();
  ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  if ((hovered || focused) && !disabled) {
    launcherPoint(a, b);
  }
  if (held) {
    a.y += 2.f * scale;
    b.y += 2.f * scale;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const bool lit = (hovered || focused) && !disabled;
  ImU32 face = primary ? (lit ? IM_COL32(226, 62, 64, 255) : kCurtain)
                       : (lit ? IM_COL32(254, 228, 160, 255) : IM_COL32(248, 238, 214, 255));
  ImU32 ink = IM_COL32(86, 40, 30, 255);
  if (disabled) {
    face = mix(face, IM_COL32(200, 190, 176, 255), 0.6f);
    ink = mix(ink, IM_COL32(150, 136, 120, 255), 0.5f);
  }
  const float line = 2.5f * scale;
  if (launcherLook() == LauncherLook::Game) {
    // the game's torn label: an ink copy a little larger behind for its line, a shadow under it
    dl->AddImage(sLabel.tex, ImVec2(a.x + 2.f * scale, a.y + 4.f * scale), ImVec2(b.x + 2.f * scale, b.y + 4.f * scale),
                 ImVec2(0, 0), ImVec2(1, 1), IM_COL32(20, 10, 30, 90));
    dl->AddImage(sLabel.tex, ImVec2(a.x - line, a.y - line), ImVec2(b.x + line, b.y + line), ImVec2(0, 0),
                 ImVec2(1, 1), ink);
    dl->AddImage(sLabel.tex, a, b, ImVec2(0, 0), ImVec2(1, 1), face);
  } else {
    std::vector<ImVec2> outer, inner;
    const unsigned seed = seedOf(label);
    launcherBrandTornRect(outer, ImVec2(a.x - line, a.y - line), ImVec2(b.x + line, b.y + line), 11.f * scale,
                          2.4f * scale, seed);
    launcherBrandTornRect(inner, a, b, 11.f * scale, 2.f * scale, seed);
    polyFill(dl, outer, IM_COL32(20, 10, 30, 90), ImVec2(2.f * scale, 4.f * scale));
    polyFill(dl, outer, ink);
    polyFill(dl, inner, face);
    // a light along the top, as of a paper's raised edge
    dl->AddLine(ImVec2(a.x + 6.f * scale, a.y + 3.f * scale), ImVec2(b.x - 6.f * scale, a.y + 3.f * scale),
                fade(IM_COL32(255, 255, 255, 255), primary ? 0.35f : 0.8f), 1.5f * scale);
  }
  ImFont* font = ImGui::GetFont();
  const float fs = ImGui::GetFontSize();
  const ImVec2 at(a.x + (b.x - a.x - text.x) * 0.5f, a.y + (b.y - a.y - text.y) * 0.5f);
  dl->PushClipRect(ImVec2(a.x - line, a.y - line), ImVec2(b.x + line, b.y + line), true);
  if (primary) {
    // white lettering with a dark red line, as the game's tab lettering
    const ImU32 out = disabled ? IM_COL32(120, 100, 96, 255) : IM_COL32(96, 10, 16, 255);
    for (int i = 0; i < 8; i++) {
      const float ang = i * 6.283185f / 8.f;
      dl->AddText(font, fs, ImVec2(at.x + std::cos(ang) * 1.8f * scale, at.y + std::sin(ang) * 1.8f * scale), out, label,
                  end);
    }
    dl->AddText(font, fs, at, disabled ? IM_COL32(236, 226, 220, 255) : IM_COL32(255, 250, 240, 255), label, end);
  } else {
    dl->AddText(font, fs, at, ink, label, end);
  }
  dl->PopClipRect();
  if (primary) {
    ImGui::PopFont();
  }
  if (pressed) {
    launcherSound(LauncherSound::Select);
  }
  return pressed;
}

bool launcherRow(const char* id, bool selected, float h, float scale) {
  ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
  const float x0 = ImGui::GetCursorPosX();
  const bool clicked =
      ImGui::Selectable(id, selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(0, h));
  ImGui::PopStyleColor(3);
  const float room = 36.f * scale; // the glove's, at the row's left
  launcherRowDecorate(selected, scale, room);
  if (clicked) {
    launcherSound(LauncherSound::Select);
  }
  ImGui::SameLine(x0 + room + 6.f * scale);
  return clicked;
}

void launcherRowDecorate(bool selected, float scale, float room) {
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  const bool hovered = ImGui::IsItemHovered();
  const bool focused = ImGui::IsItemFocused() && navVisible();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (selected) {
    launcherStrip(dl, ImVec2(a.x + room * 0.5f, a.y), b, scale, IM_COL32(255, 226, 150, 255));
  } else if (hovered || focused) {
    launcherStrip(dl, ImVec2(a.x + room * 0.5f, a.y), b, scale, IM_COL32(255, 255, 255, 130));
  }
  const ImVec2 point(a.x + room, a.y);
  if (hovered || focused) {
    launcherPoint(point, b);
  } else if (selected) {
    launcherPoint(point, b, true);
  }
}

void launcherPoint(ImVec2 min, ImVec2 max, bool fallback) {
  Target& t = fallback ? sFallback : sHover;
  t.min = min;
  t.max = max;
  t.set = true;
}

void launcherCursorFrame(float time, float scale) {
  ImGuiContext& g = *GImGui;
  Target t;
  if (sNoGlove) {
    sNoGlove = false;
  } else if (navVisible() && g.NavWindow != nullptr && g.NavId != 0 && g.NavIdIsAlive) {
    // what the keyboard or controller is on, anywhere (the settings' rows too)
    const ImRect r = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
    t.min = r.Min;
    t.max = r.Max;
    t.set = true;
    if (sHover.set && sHover.max.y > r.Min.y - 1.f && sHover.min.y < r.Max.y + 1.f &&
        std::fabs(sHover.max.x - r.Max.x) < 2.f) {
      t.min.x = std::max(t.min.x, sHover.min.x); // a row's own room for the glove
    }
  } else if (sHover.set) {
    t = sHover;
  } else if (sFallback.set) {
    t = sFallback;
  }
  sHover.set = sFallback.set = false;
  const float dt = std::clamp(time - sLastTime, 0.f, 0.1f);
  sLastTime = time;
  const float h = 30.f * scale;
  if (t.set) {
    const ImVec2 want(t.min.x - 2.f * scale, (t.min.y + t.max.y) * 0.5f);
    if (sGloveAlpha <= 0.01f) {
      sGlove = want;
    }
    const float k = 1.f - std::exp(-dt * 20.f);
    sGlove.x += (want.x - sGlove.x) * k;
    sGlove.y += (want.y - sGlove.y) * k;
    if (std::fabs(want.x - sLastTarget.x) > 2.f || std::fabs(want.y - sLastTarget.y) > 2.f) {
      if (sLastTarget.x >= 0.f && time - sLastMoveSound > 0.06f) {
        launcherSound(LauncherSound::Move);
        sLastMoveSound = time;
      }
      sLastTarget = want;
    }
    sGloveAlpha = std::min(1.f, sGloveAlpha + dt * 8.f);
  } else {
    sGloveAlpha = std::max(0.f, sGloveAlpha - dt * 8.f);
    sLastTarget = ImVec2(-1, -1);
  }
  if (sGloveAlpha <= 0.f) {
    return;
  }
  // the game's glove nudges back and forth as it points
  const float bob = (1.f + std::sin(time * 7.f)) * 2.5f * scale;
  const ImVec2 tip(sGlove.x - bob, sGlove.y);
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (launcherLook() == LauncherLook::Game) {
    // its fingertip at texel (30, 15) of 32
    const float k = h / 32.f;
    const ImVec2 o(tip.x - 30.f * k, tip.y - 15.f * k);
    dl->AddImage(sCursor.tex, ImVec2(o.x + 2.f * k, o.y + 3.f * k), ImVec2(o.x + 34.f * k, o.y + 35.f * k), ImVec2(0, 0),
                 ImVec2(1, 1), fade(IM_COL32(20, 10, 30, 110), sGloveAlpha));
    dl->AddImage(sCursor.tex, o, ImVec2(o.x + h, o.y + h), ImVec2(0, 0), ImVec2(1, 1),
                 fade(IM_COL32(255, 255, 255, 255), sGloveAlpha));
  } else {
    launcherBrandGlove(dl, tip, h, sGloveAlpha);
  }
}

// ---- tabs and pages ----

int launcherTabs(const LauncherTab* tabs, int count, int current, float scale) {
  ImFont* font = gLauncherFonts.heading;
  float fs = font->FontSize * ImGui::GetIO().FontGlobalScale;
  float pad = 34.f * scale, gap = 6.f * scale;
  {
    // all in the row's width: smaller where they wouldn't fit (a phone, five tabs beside the wordmark)
    float total = gap * static_cast<float>(count - 1);
    for (int i = 0; i < count; i++) {
      total += font->CalcTextSizeA(fs, FLT_MAX, 0.f, tabs[i].label, ImGui::FindRenderedTextEnd(tabs[i].label)).x + pad;
    }
    const float avail = ImGui::GetContentRegionAvail().x - 4.f * scale;
    if (total > avail && avail > 0.f) {
      const float k = std::max(0.55f, avail / total);
      fs *= k;
      pad *= k;
      gap *= k;
    }
  }
  const float h = fs + 16.f * scale;
  int chosen = current, focused = -1;
  std::vector<ImRect> rects(count);
  ImGui::PushFont(font);
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      ImGui::SameLine(0, gap);
    }
    const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, tabs[i].label, ImGui::FindRenderedTextEnd(tabs[i].label));
    ImGui::PushID(i);
    const ImVec2 size(sz.x + pad, h);
    if (ImGui::InvisibleButton("##tab", size, ImGuiButtonFlags_EnableNav) && i != current) {
      chosen = i;
    }
    // the keyboard or a controller moving onto a tab turns to it (the Start menu's way)
    if (ImGui::IsItemFocused() && navVisible()) {
      focused = i;
      if (i != sTabFocused && i != current) {
        chosen = i; // only as the focus arrives (the shoulder buttons may have turned past it since)
      }
    }
    rects[i] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (ImGui::IsItemHovered() || focused == i) {
      sNoGlove = true; // the tab itself shows it, raised
    }
    ImGui::PopID();
  }
  ImGui::PopFont();
  sTabFocused = focused;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto drawTab = [&](int i, bool on) {
    const int c = std::clamp(static_cast<int>(tabs[i].colour), 0, 4);
    ImVec2 a = rects[i].Min, b = rects[i].Max;
    if (!on) {
      a.y += 5.f * scale; // behind: lower and a little dimmer
    }
    const ImU32 frame = on ? kTabFrame[c] : mix(kTabFrame[c], IM_COL32(40, 30, 40, 255), 0.25f);
    const ImU32 face = on ? kTabFace[c] : mix(kTabFace[c], IM_COL32(60, 50, 60, 255), 0.25f);
    const float r = 9.f * scale;
    dl->AddRectFilled(ImVec2(a.x + 3.f * scale, a.y + 4.f * scale), ImVec2(b.x + 3.f * scale, b.y + 4.f * scale),
                      IM_COL32(20, 10, 30, 100), r);
    dl->AddRectFilled(a, b, frame, r);
    const float in = 3.f * scale;
    dl->AddRectFilled(ImVec2(a.x + in, a.y + in), ImVec2(b.x - in, b.y - in), face, r - in);
    dl->AddRectFilledMultiColor(ImVec2(a.x + in + r * 0.5f, a.y + in), ImVec2(b.x - in - r * 0.5f, (a.y + b.y) * 0.5f),
                                IM_COL32(255, 255, 255, 70), IM_COL32(255, 255, 255, 70), IM_COL32(255, 255, 255, 0),
                                IM_COL32(255, 255, 255, 0));
    // the lettering: pale, with a dark line of the tab's colour
    const ImU32 fill = mix(kTabFace[c], IM_COL32(255, 255, 255, 255), on ? 0.84f : 0.7f);
    const ImU32 line = mix(kTabFrame[c], IM_COL32(0, 0, 0, 255), 0.55f);
    const char* end = ImGui::FindRenderedTextEnd(tabs[i].label);
    const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, tabs[i].label, end);
    const ImVec2 at(a.x + (b.x - a.x - sz.x) * 0.5f, a.y + (b.y - a.y - sz.y) * 0.5f);
    for (int k = 0; k < 12; k++) {
      const float ang = k * 6.283185f / 12.f;
      dl->AddText(font, fs, ImVec2(at.x + std::cos(ang) * 2.2f * scale, at.y + std::sin(ang) * 2.2f * scale), line,
                  tabs[i].label, end);
    }
    dl->AddText(font, fs, at, fill, tabs[i].label, end);
  };
  for (int i = 0; i < count; i++) {
    if (i != current) {
      drawTab(i, false);
    }
  }
  if (current >= 0 && current < count) {
    drawTab(current, true);
  }
  return chosen;
}

void launcherPageTurn(int page, ImVec2 a, ImVec2 b, float scale, float time) {
  if (page != sPage) {
    if (sPage >= 0) {
      sTurnStart = time;
      sTurnDir = page > sPage ? 1 : -1;
      launcherSound(LauncherSound::Turn);
    }
    sPage = page;
  }
  const float t = (time - sTurnStart) / kTurn;
  if (t < 0.f || t >= 1.f) {
    return;
  }
  // a sheet turning on the panel's spine (left going forward, right going back): its face over the
  // new page while it lifts, its back past the spine as it lands; the new page in its shadow
  const float e = t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
  const float c = std::cos(e * 3.14159265f); // 1 flat on the page, -1 over
  const float w = b.x - a.x;
  const float spine = sTurnDir > 0 ? a.x : b.x;
  const float dir = sTurnDir > 0 ? 1.f : -1.f;
  const float edge = spine + dir * w * c; // the sheet's free edge
  const float lift = std::sin(e * 3.14159265f) * (b.y - a.y) * 0.05f; // the free edge nearer: taller
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  // the shadow it casts on the new page
  const float shadowW = 60.f * scale * std::sin(e * 3.14159265f) + 1.f;
  if (c > 0.f) {
    const float x0 = std::min(edge, edge + dir * shadowW), x1 = std::max(edge, edge + dir * shadowW);
    const ImU32 dark = IM_COL32(20, 10, 30, 90), clear = IM_COL32(20, 10, 30, 0);
    dl->AddRectFilledMultiColor(ImVec2(x0, a.y), ImVec2(x1, b.y), dir > 0 ? dark : clear, dir > 0 ? clear : dark,
                                dir > 0 ? clear : dark, dir > 0 ? dark : clear);
  }
  const ImVec2 p1(spine, a.y), p2(edge, a.y - lift), p3(edge, b.y + lift), p4(spine, b.y);
  const bool front = c > 0.f;
  const float shade = front ? 1.f - 0.25f * (1.f - c) : 0.72f + 0.2f * (-c);
  if (launcherLook() == LauncherLook::Game) {
    // the window's own paper on the sheet
    const float u0 = dir > 0 ? 0.f : 1.f, u1 = dir > 0 ? 1.f : 0.f;
    dl->AddImageQuad(sPanel.tex, p1, p2, p3, p4, ImVec2(u0, 0), ImVec2(u1, 0), ImVec2(u1, 1), ImVec2(u0, 1),
                     mix(IM_COL32(255, 247, 226, 255), IM_COL32(0, 0, 0, 255), 1.f - shade));
  } else {
    const ImVec2 quad[4] = {p1, p2, p3, p4};
    dl->AddConvexPolyFilled(quad, 4, mix(kPaper, IM_COL32(0, 0, 0, 255), 1.f - shade));
    dl->AddPolyline(quad, 4, IM_COL32(86, 66, 52, 255), ImDrawFlags_Closed, 2.5f * scale);
  }
}

// ---- discs ----

float launcherRegionTag(ImDrawList* dl, ImVec2 at, const char* region, float h) {
  ImU32 bg = IM_COL32(110, 90, 80, 255), fg = IM_COL32(255, 255, 255, 255);
  if (std::strcmp(region, "JP") == 0) {
    bg = IM_COL32(255, 252, 245, 255);
    fg = IM_COL32(200, 16, 46, 255);
  } else if (std::strcmp(region, "US") == 0) {
    bg = IM_COL32(32, 64, 150, 255);
  } else if (std::strcmp(region, "EU") == 0) {
    bg = IM_COL32(0, 51, 153, 255);
    fg = IM_COL32(255, 204, 0, 255);
  }
  ImFont* font = gLauncherFonts.heading;
  const float fs = h * 0.66f;
  const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, region);
  const float w = sz.x + h * 0.6f;
  const float line = std::max(1.f, h * 0.07f);
  dl->AddRectFilled(ImVec2(at.x + line, at.y + line * 1.5f), ImVec2(at.x + w + line, at.y + h + line * 1.5f),
                    IM_COL32(20, 10, 30, 70), h * 0.22f);
  dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), IM_COL32(86, 66, 52, 255), h * 0.22f);
  dl->AddRectFilled(ImVec2(at.x + line, at.y + line), ImVec2(at.x + w - line, at.y + h - line), bg,
                    h * 0.22f - line);
  if (std::strcmp(region, "JP") == 0) {
    dl->AddCircleFilled(ImVec2(at.x + w * 0.5f, at.y + h * 0.5f), h * 0.3f, IM_COL32(200, 16, 46, 60));
  }
  dl->AddText(font, fs, ImVec2(at.x + (w - sz.x) * 0.5f, at.y + (h - sz.y) * 0.5f), fg, region);
  return w;
}

// ---- sounds ----

void launcherSound(LauncherSound s) {
  if (*soundSetting() == 0 || launcherShotActive()) {
    return;
  }
  audioOpen();
  if (sAudio == nullptr) {
    return;
  }
  const float volume = std::clamp(portSettings()->volume / 100.f, 0.f, 1.f) * 0.5f;
  std::vector<float> buf;
  auto tone = [&](float f0, float f1, float len, float start, float amp) {
    const int n0 = static_cast<int>(start * kRate), n = static_cast<int>(len * kRate);
    if (static_cast<int>(buf.size()) < n0 + n) {
      buf.resize(n0 + n, 0.f);
    }
    float ph = 0.f;
    for (int i = 0; i < n; i++) {
      const float t = static_cast<float>(i) / n;
      const float f = f0 + (f1 - f0) * t;
      ph += 6.2831853f * f / kRate;
      const float env = std::min(1.f, t * 40.f) * std::exp(-t * 5.f);
      buf[n0 + i] += amp * env * (std::sin(ph) + 0.25f * std::sin(ph * 2.f));
    }
  };
  auto noise = [&](float len, float start, float amp, float lowpass, bool swell) {
    const int n0 = static_cast<int>(start * kRate), n = static_cast<int>(len * kRate);
    if (static_cast<int>(buf.size()) < n0 + n) {
      buf.resize(n0 + n, 0.f);
    }
    uint32_t r = 22222u;
    float y = 0.f, prev = 0.f;
    for (int i = 0; i < n; i++) {
      r = r * 1664525u + 1013904223u;
      const float x = (static_cast<float>(r >> 8) / 8388608.f) - 1.f;
      y += (x - y) * lowpass; // softened
      const float hp = y - prev; // and the rumble taken out
      prev = y;
      const float t = static_cast<float>(i) / n;
      const float env = swell ? std::sin(t * 3.14159265f) : std::exp(-t * 9.f);
      buf[n0 + i] += amp * env * hp * 4.f;
    }
  };
  switch (s) {
  case LauncherSound::Move: // a paper tick
    noise(0.03f, 0.f, 0.5f, 0.6f, false);
    tone(2100.f, 1900.f, 0.025f, 0.f, 0.08f);
    break;
  case LauncherSound::Select: // two bright notes up
    tone(988.f, 988.f, 0.07f, 0.f, 0.22f);
    tone(1480.f, 1480.f, 0.12f, 0.055f, 0.22f);
    break;
  case LauncherSound::Back: // two notes down
    tone(784.f, 784.f, 0.07f, 0.f, 0.2f);
    tone(523.f, 523.f, 0.11f, 0.055f, 0.2f);
    break;
  case LauncherSound::Turn: // a page's swish
    noise(0.2f, 0.f, 0.45f, 0.25f, true);
    break;
  }
  for (float& v : buf) {
    v *= volume;
  }
  SDL_PutAudioStreamData(sAudio, buf.data(), static_cast<int>(buf.size() * sizeof(float)));
}

void launcherSoundEnd() {
  if (sAudio != nullptr) {
    SDL_DestroyAudioStream(sAudio);
    sAudio = nullptr;
  }
}
