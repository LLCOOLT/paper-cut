// Paper Cut's own art in the launcher (launcher_brand.h): the wordmark and its ribbon, and the paper
// look's drawn pieces. Swap the wordmark or the glove here; nothing else draws them.
#include "launcher_brand.h"
#include "launcher_look.h"
#include "launcher_style.h"

#include "port/tr.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

// the shipped icon's 512 px tile (port/assets/icon/papercut-512.png), embedded by CMakeLists.txt
extern "C" {
extern const unsigned char portLogo512[];
extern const unsigned int portLogo512_size;
}

namespace {

float hash01(unsigned i, unsigned k) {
  uint32_t x = i * 747796405u + k * 2891336453u + 977u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}

ImU32 withAlpha(ImU32 c, float a) {
  const unsigned al = static_cast<unsigned>(((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
  return (c & ~IM_COL32_A_MASK) | (al << IM_COL32_A_SHIFT);
}

ImU32 lighter(ImU32 c, float t) {
  const ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(v.x + (1.f - v.x) * t, v.y + (1.f - v.y) * t, v.z + (1.f - v.z) * t, v.w));
}

// ---- the title ----

// an image standing in for the drawn wordmark (launcherBrandWordmarkImage), or none
ImTextureID sWordmark{};
float sWordmarkW = 0.f, sWordmarkH = 0.f;
bool sWordmarkTried = false;

void loadWordmarkOverride() {
  if (sWordmarkTried) {
    return;
  }
  sWordmarkTried = true;
  // $TTYD_BRAND_WORDMARK=<png>: a wordmark picture tried in place of the drawn letters
  const char* path = SDL_getenv("TTYD_BRAND_WORDMARK");
  if (path == nullptr || sWordmark != ImTextureID{}) {
    return;
  }
  SDL_Surface* s = SDL_LoadPNG(path);
  SDL_Surface* rgba = s != nullptr ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(s);
  if (rgba != nullptr && rgba->pitch == rgba->w * 4) {
    launcherBrandWordmarkImage(launcherTexture(rgba->w, rgba->h, rgba->pixels), static_cast<float>(rgba->w),
                               static_cast<float>(rgba->h));
  }
  SDL_DestroySurface(rgba);
}

// the vertices drawn since mark turned by angle (radians) about c
void turn(ImDrawList* dl, int mark, ImVec2 c, float angle) {
  const float cs = std::cos(angle), sn = std::sin(angle);
  for (int i = mark; i < dl->VtxBuffer.Size; i++) {
    ImDrawVert& v = dl->VtxBuffer[i];
    const float x = v.pos.x - c.x, y = v.pos.y - c.y;
    v.pos = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
  }
}

// a letter stamped round a circle of radius r: an outline that thick
void ring(ImDrawList* dl, ImFont* f, float size, ImVec2 p, const char* s, ImU32 col, float r, int n) {
  for (int i = 0; i < n; i++) {
    const float a = i * 6.2831853f / n;
    dl->AddText(f, size, ImVec2(p.x + std::cos(a) * r, p.y + std::sin(a) * r), col, s, s + 1);
  }
}

// a pair of scissors with their pivot at c, h tall, open by `open` (0-1), blades down, turned by tilt
void scissors(ImDrawList* dl, ImVec2 c, float h, float open, float tilt, ImU32 handle = IM_COL32(214, 52, 60, 255)) {
  const int mark = dl->VtxBuffer.Size;
  const ImU32 ink = IM_COL32(48, 34, 30, 255), steel = IM_COL32(222, 226, 234, 255),
              shine = IM_COL32(255, 255, 255, 255);
  const float spread = 0.12f + 0.16f * open;
  for (int side = -1; side <= 1; side += 2) {
    // a blade: from the pivot down to a long thin point, an ink line round it and a glint
    const float a = 1.5708f + side * spread;
    const ImVec2 tip(c.x + std::cos(a) * h * 0.58f, c.y + std::sin(a) * h * 0.58f);
    const ImVec2 n(-std::sin(a) * h * 0.1f, std::cos(a) * h * 0.1f);
    dl->AddTriangleFilled(ImVec2(c.x + n.x * 1.7f, c.y + n.y * 1.7f), ImVec2(tip.x, tip.y + h * 0.02f),
                          ImVec2(c.x - n.x * 1.7f, c.y - n.y * 1.7f), ink);
    dl->AddTriangleFilled(ImVec2(c.x + n.x, c.y + n.y), tip, ImVec2(c.x - n.x, c.y - n.y), steel);
    dl->AddLine(ImVec2(c.x + n.x * 0.3f, c.y + n.y * 0.3f), tip, shine, std::max(1.f, h * 0.02f));
    // its handle: a ring the other way
    const float b = -1.5708f - side * (spread + 0.3f);
    const ImVec2 rc(c.x + std::cos(b) * h * 0.27f, c.y + std::sin(b) * h * 0.27f);
    dl->AddCircleFilled(rc, h * 0.16f, ink);
    dl->AddCircle(rc, h * 0.105f, handle, 0, h * 0.065f);
  }
  dl->AddCircleFilled(c, h * 0.05f, ink);
  dl->AddCircleFilled(c, h * 0.025f, steel);
  turn(dl, mark, c, tilt);
}

// ---- the app icon's pieces ----

// a soft round light: col at c, fading to nothing at radius r
void glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
  const int n = 96;
  const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
  dl->PrimReserve(n * 3, n + 1);
  const unsigned base = dl->_VtxCurrentIdx;
  dl->PrimWriteVtx(c, uv, col);
  for (int i = 0; i < n; i++) {
    const float a = i * 6.2831853f / n;
    dl->PrimWriteVtx(ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r), uv, withAlpha(col, 0.f));
  }
  for (int i = 0; i < n; i++) {
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base));
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % n));
  }
}

// a four-pointed sparkle at c, r to its tips
void sparkle(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
  const float w = r * 0.22f;
  ImVec2 p[8] = {ImVec2(c.x, c.y - r), ImVec2(c.x + w, c.y - w), ImVec2(c.x + r, c.y), ImVec2(c.x + w, c.y + w),
                 ImVec2(c.x, c.y + r), ImVec2(c.x - w, c.y + w), ImVec2(c.x - r, c.y), ImVec2(c.x - w, c.y - w)};
  dl->AddConcavePolyFilled(p, 8, col);
}

// a dashed line along the points (closed: back to the first)
void dashed(ImDrawList* dl, const std::vector<ImVec2>& pts, bool closed, float dash, float gap, ImU32 col,
            float thick) {
  float phase = 0.f; // along the current dash (or gap, past dash)
  const size_t n = pts.size(), segs = closed ? n : n - 1;
  for (size_t i = 0; i < segs; i++) {
    const ImVec2 a = pts[i], b = pts[(i + 1) % n];
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    float t = 0.f;
    while (t < len) {
      const bool on = phase < dash;
      const float step = std::min(len - t, on ? dash - phase : dash + gap - phase);
      if (on) {
        dl->AddLine(ImVec2(a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len),
                    ImVec2(a.x + (b.x - a.x) * (t + step) / len, a.y + (b.y - a.y) * (t + step) / len), col, thick);
      }
      t += step;
      phase += step;
      if (phase >= dash + gap) {
        phase = 0.f;
      }
    }
  }
}

// One cut-out piece of coloured paper, drawn the wordmark's way: its soft shadow, the dark cut line,
// the white paper round it and its shade, then the face with a line inside its edge and a light
// across its top. draw(off, col) draws the piece's shape moved by off in col.
struct CutPiece {
  std::function<void(ImVec2, ImU32)> draw;
  ImVec2 c;    // turned about this
  float angle; // radians
  ImU32 face, line;
  float lightY;                 // the light on the face above this
  std::function<void()> detail; // drawn on the face (may be empty)
};

void stampPiece(const CutPiece& p, ImVec2 at, float r, int n, ImU32 col) {
  for (int i = 0; i < n; i++) {
    const float a = i * 6.2831853f / n;
    p.draw(ImVec2(at.x + std::cos(a) * r, at.y + std::sin(a) * r), col);
  }
}

// The piece at unit size u (the edge's width follows it, unless given: edge, the white paper's width;
// line, the dark cut line's beyond it).
void cutPiece(ImDrawList* dl, const CutPiece& piece, float u, float edge = -1.f, float line = -1.f) {
  edge = edge >= 0.f ? edge : 0.085f * u;
  line = line >= 0.f ? line : 0.02f * u;
  const std::function<void(const CutPiece&)> layers[] = {
      [&](const CutPiece& p) { // the shadow: soft, down and to the right
        // (at icon sizes a few copies show as steps: many faint ones, out to a soft edge)
        const ImVec2 s(0.05f * u, 0.08f * u);
        for (int i = 0; i < 6; i++) {
          stampPiece(p, s, edge * (1.7f - 0.25f * i), 40, IM_COL32(10, 6, 24, 9));
        }
      },
      [&](const CutPiece& p) { stampPiece(p, ImVec2(0, 0), edge + line, 64, IM_COL32(70, 48, 40, 255)); },
      [&](const CutPiece& p) { stampPiece(p, ImVec2(0, 0), edge, 64, IM_COL32(255, 250, 238, 255)); },
      [&](const CutPiece& p) { stampPiece(p, ImVec2(0, edge * 0.5f), edge * 0.45f, 32, IM_COL32(228, 214, 188, 255)); },
      [&](const CutPiece& p) {
        stampPiece(p, ImVec2(0, 0), 0.024f * u, 24, p.line);
        p.draw(ImVec2(0, 0), p.face);
        dl->PushClipRect(ImVec2(-1e6f, -1e6f), ImVec2(1e6f, p.lightY), true);
        p.draw(ImVec2(0, 0), lighter(p.face, 0.14f));
        dl->PopClipRect();
        if (p.detail) {
          p.detail();
        }
      },
  };
  for (const auto& layer : layers) {
    const int mark = dl->VtxBuffer.Size;
    layer(piece);
    turn(dl, mark, piece.c, piece.angle);
  }
}

// the stage behind the icon, full bleed: the night-blue backdrop, a warm light, a few stars (not
// when it's small: they'd be specks)
void iconBackdrop(ImDrawList* dl, ImVec2 o, float s, ImVec2 light, bool stars) {
  dl->AddRectFilledMultiColor(o, ImVec2(o.x + s, o.y + s), IM_COL32(30, 36, 92, 255), IM_COL32(30, 36, 92, 255),
                              IM_COL32(78, 48, 122, 255), IM_COL32(78, 48, 122, 255));
  glow(dl, ImVec2(o.x + light.x * s, o.y + light.y * s), 0.62f * s, IM_COL32(255, 226, 170, 70));
  static const float kStars[][3] = {
      {0.12f, 0.14f, 1.f}, {0.86f, 0.12f, 0.8f}, {0.9f, 0.8f, 0.7f}, {0.1f, 0.84f, 0.6f}, {0.24f, 0.3f, 0.5f}};
  for (const auto& st : kStars) {
    if (stars) {
      sparkle(dl, ImVec2(o.x + st[0] * s, o.y + st[1] * s), 0.03f * s * st[2], IM_COL32(255, 244, 200, 150));
    }
  }
}

// the scissors at the icon's smallest sizes: two red handles and one steel blade, each with an ink edge
void tinyScissors(ImDrawList* dl, ImVec2 c, float h, float tilt) {
  const int mark = dl->VtxBuffer.Size;
  const ImU32 ink = IM_COL32(48, 34, 30, 255);
  dl->AddTriangleFilled(ImVec2(c.x - h * 0.17f, c.y), ImVec2(c.x, c.y + h * 0.7f), ImVec2(c.x + h * 0.17f, c.y), ink);
  dl->AddTriangleFilled(ImVec2(c.x - h * 0.1f, c.y), ImVec2(c.x, c.y + h * 0.6f), ImVec2(c.x + h * 0.1f, c.y),
                        IM_COL32(236, 240, 246, 255));
  for (int side = -1; side <= 1; side += 2) {
    const ImVec2 rc(c.x + side * h * 0.17f, c.y - h * 0.24f);
    dl->AddCircleFilled(rc, h * 0.19f, ink);
    dl->AddCircleFilled(rc, h * 0.14f, IM_COL32(222, 56, 64, 255));
  }
  turn(dl, mark, c, tilt);
}

constexpr ImU32 kIconGold = IM_COL32(250, 196, 58, 255), kIconGoldLine = IM_COL32(146, 86, 14, 255);

// a five-pointed star's ten points, the first at the top, turned by angle
std::vector<ImVec2> starPoints(ImVec2 c, float R, float r, float angle) {
  std::vector<ImVec2> p;
  for (int i = 0; i < 10; i++) {
    const float a = -1.5708f + angle + i * 3.14159265f / 5.f;
    const float rr = (i & 1) ? r : R;
    p.push_back(ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr));
  }
  return p;
}

} // namespace

void launcherBrandWordmarkImage(ImTextureID tex, float w, float h) {
  sWordmark = tex;
  sWordmarkW = w;
  sWordmarkH = h;
}

float launcherBrandTitle(ImDrawList* dl, float cx, float left, float top, float width, float height, float scale,
                         float time, bool compact, float* indent) {
  loadWordmarkOverride();
  ImFont* font = gLauncherFonts.logo != nullptr ? gLauncherFonts.logo : gLauncherFonts.title;
  // the hero's size: from the stage's height (a short stage: a band beside the tabs), at most four
  // fifths of its width (two fifths beside the tabs)
  float size =
      compact ? std::max(40.f * scale, height * 0.2f) : std::clamp(height * 0.18f, 56.f * scale, 240.f * scale);
  const char* words[2] = {"Paper", "Cut"};
  auto wordWidth = [&](const char* w, float s) { return font->CalcTextSizeA(s, FLT_MAX, 0.f, w).x; };
  const float gapK = 0.62f; // the gap between the words, of the size: the scissors cut down it
  const float total = wordWidth(words[0], size) + wordWidth(words[1], size) + gapK * size;
  const float maxW = compact ? width * 0.42f : width * 0.8f;
  if (total > maxW) {
    size *= maxW / total;
  }
  const float wordsW = wordWidth(words[0], size) + wordWidth(words[1], size) + gapK * size;
  const ImVec2 glyph = font->CalcTextSizeA(size, FLT_MAX, 0.f, "P");
  const float x0 = compact ? left + 0.12f * size : cx - wordsW * 0.5f;
  const float y0 = top + 0.04f * size;
  float bottom;

  if (sWordmark != ImTextureID{} && sWordmarkH > 0.f) {
    // the brand's own wordmark picture, as tall as the letters would be
    const float h = glyph.y * 0.95f, w = h * sWordmarkW / sWordmarkH;
    const float x = compact ? left : cx - w * 0.5f;
    dl->AddImage(sWordmark, ImVec2(x, y0), ImVec2(x + w, y0 + h));
    *indent = compact ? w + 24.f * scale : 0.f;
    bottom = y0 + h;
  } else {
    // Each letter a paper cut-out: its shadow on the stage, a dark cut line, the white paper round
    // it, then the letter with a line inside its edge; each a little turned and lifted, as if glued
    // by hand, swaying a touch. A word's letters are cut from one sheet: each layer is drawn for
    // every letter before the next, so their papers join.
    struct Letter {
      char text[2];
      ImVec2 at, c;
      float angle;
      ImU32 face, line;
    };
    std::vector<Letter> letters;
    float x = x0;
    for (int w = 0; w < 2; w++) {
      for (const char* p = words[w]; *p; p++) {
        Letter L{};
        L.text[0] = *p;
        const float adv = font->CalcTextSizeA(size, FLT_MAX, 0.f, L.text, L.text + 1).x;
        const unsigned k = static_cast<unsigned>(letters.size());
        const float lift = (hash01(k, 7) - 0.5f) * 0.08f * size;
        L.at = ImVec2(x, y0 + lift);
        L.c = ImVec2(x + adv * 0.5f, y0 + lift + glyph.y * 0.55f);
        L.angle = (hash01(k, 3) - 0.5f) * 0.18f + 0.012f * std::sin(time * 1.3f + k * 1.7f);
        // Paper: Paper Cut's red; Cut: the stage's gold
        L.face = w == 0 ? IM_COL32(220, 44, 52, 255) : IM_COL32(250, 196, 58, 255);
        L.line = w == 0 ? IM_COL32(120, 12, 24, 255) : IM_COL32(146, 86, 14, 255);
        letters.push_back(L);
        x += adv * 0.98f;
      }
      x += gapK * size;
    }
    const float edge = 0.085f * size; // the white paper round each letter
    auto pass = [&](auto drawOne) {
      for (const Letter& L : letters) {
        const int mark = dl->VtxBuffer.Size;
        drawOne(L);
        turn(dl, mark, L.c, L.angle);
      }
    };
    pass([&](const Letter& L) { // the shadow: soft, down and to the right
      const ImVec2 s(L.at.x + 0.05f * size, L.at.y + 0.08f * size);
      ring(dl, font, size, s, L.text, IM_COL32(10, 6, 24, 30), edge * 1.4f, 16);
      ring(dl, font, size, s, L.text, IM_COL32(10, 6, 24, 40), edge * 0.8f, 12);
    });
    pass([&](const Letter& L) { ring(dl, font, size, L.at, L.text, IM_COL32(70, 48, 40, 255), edge + 0.02f * size, 24); });
    pass([&](const Letter& L) { ring(dl, font, size, L.at, L.text, IM_COL32(255, 250, 238, 255), edge, 24); });
    pass([&](const Letter& L) { // the paper's shade along its lower edge
      ring(dl, font, size, ImVec2(L.at.x, L.at.y + edge * 0.5f), L.text, IM_COL32(228, 214, 188, 255), edge * 0.45f, 12);
    });
    pass([&](const Letter& L) { // the letter: its line, its face, a light across its top
      ring(dl, font, size, L.at, L.text, L.line, 0.024f * size, 12);
      dl->AddText(font, size, L.at, L.face, L.text, L.text + 1);
      dl->PushClipRect(ImVec2(L.at.x - size, L.at.y - size), ImVec2(L.at.x + size * 2.f, L.at.y + glyph.y * 0.43f), true);
      dl->AddText(font, size, L.at, lighter(L.face, 0.14f), L.text, L.text + 1);
      dl->PopClipRect();
    });
    // the scissors' cut between the words: a dashed line down the gap, the scissors at its top
    const float gx = x0 + wordWidth(words[0], size) * 0.98f + gapK * size * 0.5f;
    const float cutTop = y0 + glyph.y * 0.36f, cutBottom = y0 + glyph.y * 0.98f;
    const int dashes = 6;
    for (int i = 0; i < dashes; i++) {
      const float t0 = static_cast<float>(i) / dashes, t1 = t0 + 0.55f / dashes;
      const float sway = 0.035f * size;
      dl->AddLine(ImVec2(gx + std::sin(t0 * 3.f) * sway, cutTop + (cutBottom - cutTop) * t0),
                  ImVec2(gx + std::sin(t1 * 3.f) * sway, cutTop + (cutBottom - cutTop) * t1),
                  IM_COL32(255, 247, 228, 210), std::max(1.5f, 0.024f * size));
    }
    scissors(dl, ImVec2(gx, cutTop - 0.02f * size), 0.72f * size, 0.5f + 0.5f * std::sin(time * 2.5f), -0.2f);
    *indent = compact ? (x0 - left) + wordsW + 0.3f * size : 0.f;
    bottom = y0 + glyph.y * 0.95f;
  }
  if (compact) {
    return bottom; // (a short stage: no ribbon)
  }
  // the ribbon under it: what Paper Cut plays, small and quiet, in the launcher's own type
  const char* sub = portTr("for Paper Mario: The Thousand-Year Door");
  ImFont* small = gLauncherFonts.heading;
  const float base = small->FontSize * ImGui::GetIO().FontGlobalScale;
  float fs = std::clamp(size * 0.17f, base * 0.75f, base * 1.3f);
  ImVec2 ts = small->CalcTextSizeA(fs, FLT_MAX, 0.f, sub);
  if (ts.x > width * 0.7f) {
    fs *= width * 0.7f / ts.x;
    ts = small->CalcTextSizeA(fs, FLT_MAX, 0.f, sub);
  }
  const float bh = ts.y * 1.45f, pad = ts.y * 1.1f;
  const float by = bottom - 0.04f * size; // tucked a little under the letters
  const ImVec2 a(cx - ts.x * 0.5f - pad, by), b(cx + ts.x * 0.5f + pad, by + bh);
  const float tail = bh * 1.1f, drop = bh * 0.3f;
  const ImU32 band = IM_COL32(255, 247, 228, 255), back = IM_COL32(222, 196, 146, 255),
              fold = IM_COL32(160, 122, 76, 255), shadow = IM_COL32(10, 6, 24, 70);
  for (int side = -1; side <= 1; side += 2) {
    // a tail behind each end, lower, with a notch; the fold where it turns under, in shade
    const float ex = side < 0 ? a.x : b.x;
    const float ox = ex + side * tail;
    ImVec2 t[5] = {ImVec2(ex - side * bh * 0.4f, a.y + drop), ImVec2(ox, a.y + drop),
                   ImVec2(ox - side * bh * 0.35f, a.y + drop + bh * 0.5f), ImVec2(ox, b.y + drop),
                   ImVec2(ex - side * bh * 0.4f, b.y + drop)};
    // (in three triangles round the notch, either way round)
    auto tailAt = [&](ImVec2 off, ImU32 col) {
      auto P = [&](int i) { return ImVec2(t[i].x + off.x, t[i].y + off.y); };
      dl->AddTriangleFilled(P(0), P(1), P(2), col);
      dl->AddTriangleFilled(P(0), P(2), P(4), col);
      dl->AddTriangleFilled(P(2), P(3), P(4), col);
    };
    tailAt(ImVec2(bh * 0.1f, bh * 0.16f), shadow);
    tailAt(ImVec2(0, 0), back);
    dl->AddTriangleFilled(ImVec2(ex, b.y), ImVec2(ex, b.y + drop), ImVec2(ex - side * bh * 0.4f, b.y + drop), fold);
  }
  dl->AddRectFilled(ImVec2(a.x + bh * 0.1f, a.y + bh * 0.16f), ImVec2(b.x + bh * 0.1f, b.y + bh * 0.16f), shadow);
  dl->AddRectFilled(a, b, band);
  const float rule = std::max(1.f, bh * 0.04f);
  dl->AddLine(ImVec2(a.x, a.y + bh * 0.13f), ImVec2(b.x, a.y + bh * 0.13f), back, rule);
  dl->AddLine(ImVec2(a.x, b.y - bh * 0.13f), ImVec2(b.x, b.y - bh * 0.13f), back, rule);
  dl->AddText(small, fs, ImVec2(cx - ts.x * 0.5f, a.y + (bh - ts.y) * 0.5f), IM_COL32(96, 58, 38, 255), sub);
  return b.y + drop + 8.f * scale;
}

void launcherBrandGlove(ImDrawList* dl, ImVec2 tip, float h, float alpha) {
  // drawn on a 32-unit square, the fingertip at (31, 14); a cuff, the fist with its knuckle
  // creases, the thumb up and the pointing finger, each outlined in ink, then a shadow under all
  const float u = h / 32.f;
  const ImVec2 o(tip.x - 31.f * u, tip.y - 14.f * u);
  auto P = [&](float x, float y) { return ImVec2(o.x + x * u, o.y + y * u); };
  const ImU32 ink = withAlpha(IM_COL32(48, 34, 30, 255), alpha);
  const ImU32 white = withAlpha(IM_COL32(250, 250, 246, 255), alpha);
  const ImU32 shade = withAlpha(IM_COL32(196, 196, 204, 255), alpha);
  const ImU32 shadow = withAlpha(IM_COL32(20, 10, 30, 90), alpha);
  struct Part {
    float x0, y0, x1, y1, r;
  };
  const Part parts[] = {
      {3.f, 9.f, 8.f, 27.f, 1.5f},   // the cuff
      {7.f, 10.f, 21.f, 28.f, 5.f},  // the fist
      {12.f, 2.f, 17.f, 14.f, 2.5f}, // the thumb, up
      {17.f, 11.f, 31.f, 17.f, 3.f}, // the pointing finger
  };
  const float line = 1.6f * u;
  for (const Part& p : parts) {
    dl->AddRectFilled(P(p.x0 + 1.5f, p.y0 + 2.f), P(p.x1 + 1.5f, p.y1 + 2.f), shadow, p.r * u);
  }
  for (const Part& p : parts) {
    dl->AddRectFilled(ImVec2(P(p.x0, p.y0).x - line, P(p.x0, p.y0).y - line),
                      ImVec2(P(p.x1, p.y1).x + line, P(p.x1, p.y1).y + line), ink, p.r * u + line);
  }
  for (const Part& p : parts) {
    dl->AddRectFilled(P(p.x0, p.y0), P(p.x1, p.y1), white, p.r * u);
  }
  // the cuff's band and the knuckles' creases
  dl->AddRectFilled(P(3.f, 9.f), P(5.f, 27.f), shade, 1.f * u);
  for (int i = 0; i < 3; i++) {
    const float y = 19.f + i * 3.f;
    dl->AddLine(P(11.f, y), P(19.f, y), shade, 1.2f * u);
  }
  dl->AddLine(P(17.f, 14.f), P(21.f, 14.f), shade, 1.f * u);
}

void launcherBrandTornRect(std::vector<ImVec2>& out, ImVec2 a, ImVec2 b, float step, float rough, unsigned seed) {
  out.clear();
  const ImVec2 corners[4] = {a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y)};
  const ImVec2 normals[4] = {ImVec2(0, -1), ImVec2(1, 0), ImVec2(0, 1), ImVec2(-1, 0)};
  unsigned n = 0;
  for (int side = 0; side < 4; side++) {
    const ImVec2 p = corners[side], q = corners[(side + 1) & 3];
    const float len = std::hypot(q.x - p.x, q.y - p.y);
    const int steps = std::max(1, static_cast<int>(len / std::max(step, 1.f)));
    for (int i = 0; i < steps; i++) {
      const float t = static_cast<float>(i) / steps;
      // the corners stay put; the edges wander in and out
      const float k = i == 0 ? 0.25f : (hash01(seed, n) * 2.f - 1.f);
      n++;
      out.push_back(ImVec2(p.x + (q.x - p.x) * t + normals[side].x * k * rough,
                           p.y + (q.y - p.y) * t + normals[side].y * k * rough));
    }
  }
}

void launcherBrandIcon(ImDrawList* dl, ImVec2 o, float s, float time, float px, unsigned parts, float pieceScale) {
  auto P = [&](float x, float y) { return ImVec2(o.x + x * s, o.y + y * s); };
  // the detail for the size it's seen at: full; small (no sparkles or cut line, a bigger star and
  // scissors); tiny (a flat star, the scissors as two handles and a blade)
  const float seen = px > 0.f ? px : s;
  const bool small = seen < 48.f, tiny = seen < 24.f;
  if ((parts & kBrandIconBackdrop) != 0u) {
    iconBackdrop(dl, o, s, ImVec2(0.46f, 0.5f), !small);
  }
  if ((parts & kBrandIconPieces) == 0u) {
    return;
  }
  const int mark = dl->VtxBuffer.Size;
  const float snip = 0.5f + 0.5f * std::sin(time * 2.5f); // the scissors' blades, as in the title
  // a gold folded-paper star inside a dashed cut line, the title's scissors cutting round it
  const ImVec2 c = small ? P(0.43f, 0.58f) : P(0.45f, 0.56f);
  const float R = (tiny ? 0.34f : small ? 0.33f : 0.3f) * s, r = R * 0.5f, tilt = -0.12f;
  const std::vector<ImVec2> pts = starPoints(c, R, r, tilt);
  CutPiece star;
  star.draw = [dl, pts](ImVec2 off, ImU32 col) {
    ImVec2 q[10];
    for (int i = 0; i < 10; i++) {
      q[i] = ImVec2(pts[i].x + off.x, pts[i].y + off.y);
    }
    dl->AddConcavePolyFilled(q, 10, col);
  };
  star.c = c;
  star.angle = 0.f;
  star.face = kIconGold;
  star.line = kIconGoldLine;
  star.lightY = c.y - R * 2.f; // (the folds light it instead)
  if (!tiny) {
    star.detail = [dl, pts, c]() { // folded: each point's left half in light
      for (int i = 0; i < 10; i += 2) {
        dl->AddTriangleFilled(c, pts[(i + 9) % 10], pts[i], IM_COL32(255, 222, 120, 255));
      }
    };
  }
  if (!small) {
    const std::vector<ImVec2> guide = starPoints(c, R * 1.3f, r * 1.42f, tilt);
    dashed(dl, guide, true, 0.045f * s, 0.03f * s, IM_COL32(255, 247, 228, 200), std::max(1.f, 0.016f * s));
  }
  // small: the white paper's edge and the cut line at least about a pixel each, so the star stays cut
  // out; tiny: a thin edge and a firm line, crisp before cut-out
  const float u = 0.42f * s, onePx = s / seen;
  if (tiny) {
    cutPiece(dl, star, u, 0.6f * onePx, 0.9f * onePx);
    tinyScissors(dl, P(0.69f, 0.33f), 0.46f * s, 0.72f);
  } else if (small) {
    cutPiece(dl, star, u, std::max(0.085f * u, onePx), std::max(0.02f * u, 0.7f * onePx));
    scissors(dl, P(0.71f, 0.32f), 0.47f * s, snip, 0.72f);
  } else {
    cutPiece(dl, star, u);
    scissors(dl, P(0.72f, 0.31f), 0.44f * s, snip, 0.72f);
  }
  if (pieceScale != 1.f) {
    const ImVec2 mid = P(0.5f, 0.5f);
    for (int i = mark; i < dl->VtxBuffer.Size; i++) {
      ImDrawVert& v = dl->VtxBuffer[i];
      v.pos = ImVec2(mid.x + (v.pos.x - mid.x) * pieceScale, mid.y + (v.pos.y - mid.y) * pieceScale);
    }
  }
}

ImTextureID launcherBrandIconTexture() {
  static ImTextureID tex{};
  static bool tried = false;
  if (tried) {
    return tex;
  }
  tried = true;
  // the shipped 512 px tile, box-filtered to 128 px on the CPU (the GPU would alias it, scaled down
  // that far without mipmaps), colours weighted by their alpha
  SDL_IOStream* io = SDL_IOFromConstMem(portLogo512, portLogo512_size);
  SDL_Surface* s = io != nullptr ? SDL_LoadPNG_IO(io, true) : nullptr;
  SDL_Surface* rgba = s != nullptr ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(s);
  if (rgba != nullptr && rgba->w == 512 && rgba->h == 512) {
    constexpr int kOut = 128, kStep = 4;
    std::vector<uint8_t> px(static_cast<size_t>(kOut) * kOut * 4);
    for (int y = 0; y < kOut; y++) {
      for (int x = 0; x < kOut; x++) {
        float acc[4] = {0, 0, 0, 0};
        for (int j = 0; j < kStep; j++) {
          const uint8_t* row =
              static_cast<const uint8_t*>(rgba->pixels) + static_cast<size_t>(y * kStep + j) * rgba->pitch;
          for (int i = 0; i < kStep; i++) {
            const uint8_t* p = row + (x * kStep + i) * 4;
            const float a = p[3] / 255.f;
            acc[0] += p[0] * a;
            acc[1] += p[1] * a;
            acc[2] += p[2] * a;
            acc[3] += a;
          }
        }
        uint8_t* o = &px[(static_cast<size_t>(y) * kOut + x) * 4];
        for (int c = 0; c < 3; c++) {
          o[c] = static_cast<uint8_t>(acc[3] > 0.f ? std::min(255.f, acc[c] / acc[3] + 0.5f) : 0.f);
        }
        o[3] = static_cast<uint8_t>(acc[3] / (kStep * kStep) * 255.f + 0.5f);
      }
    }
    tex = launcherTexture(kOut, kOut, px.data());
  }
  SDL_DestroySurface(rgba);
  return tex;
}
