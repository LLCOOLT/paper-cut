// Button glyphs (button_glyphs.h): every shape is a signed distance, so edges
// are smooth at any size; labels are the launcher's Fredoka Bold through
// stb_truetype (Dear ImGui's copy, compiled static here).
#include "button_glyphs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>

namespace {

const unsigned char* sFont;
stbtt_fontinfo sInfo;
bool sFontReady;

struct Rgba {
  float r, g, b, a;
};

Rgba hex(uint32_t c, float a = 1.f) {
  return Rgba{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a};
}

Rgba shade(Rgba c, float k) { return Rgba{c.r * k, c.g * k, c.b * k, c.a}; }

struct Canvas {
  int w, h;
  std::vector<float> px; // premultiplied RGBA

  Canvas(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_ * 4, 0.f) {}

  void blend(int x, int y, Rgba c, float cover) {
    if (x < 0 || y < 0 || x >= w || y >= h || cover <= 0.f) {
      return;
    }
    const float a = std::min(1.f, cover) * c.a;
    float* p = &px[(static_cast<size_t>(y) * w + x) * 4];
    p[0] = c.r * a + p[0] * (1.f - a);
    p[1] = c.g * a + p[1] * (1.f - a);
    p[2] = c.b * a + p[2] * (1.f - a);
    p[3] = a + p[3] * (1.f - a);
  }

  // paints c where sdf(x, y) < 0, a pixel of soft edge
  template <typename F> void fill(Rgba c, F sdf) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        const float d = sdf(x + 0.5f, y + 0.5f);
        blend(x, y, c, std::clamp(0.5f - d, 0.f, 1.f));
      }
    }
  }

  std::vector<uint8_t> rgba8() const {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
      const float a = px[i * 4 + 3];
      for (int k = 0; k < 3; k++) {
        out[i * 4 + k] = static_cast<uint8_t>(std::clamp(a > 0.f ? px[i * 4 + k] / a : 0.f, 0.f, 1.f) * 255.f + 0.5f);
      }
      out[i * 4 + 3] = static_cast<uint8_t>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
    }
    return out;
  }
};

float circle(float x, float y, float cx, float cy, float r) { return std::hypot(x - cx, y - cy) - r; }

float roundBox(float x, float y, float cx, float cy, float hw, float hh, float r) {
  const float qx = std::fabs(x - cx) - hw + r, qy = std::fabs(y - cy) - hh + r;
  return std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
}

float segment(float x, float y, float ax, float ay, float bx, float by, float r) {
  const float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
  const float t = std::clamp((px * dx + py * dy) / (dx * dx + dy * dy), 0.f, 1.f);
  return std::hypot(px - dx * t, py - dy * t) - r;
}

// a label centred at (cx, cy), its capital letters' height px
void text(Canvas& cv, const char* s, float cx, float cy, float px, Rgba c) {
  if (!sFontReady || s == nullptr || *s == '\0') {
    return;
  }
  // fit: no wider than 80% of the canvas
  float scale = stbtt_ScaleForPixelHeight(&sInfo, px / 0.72f); // Fredoka's caps are about 72% of its height
  auto width = [&](float sc) {
    float wsum = 0.f;
    for (const char* p = s; *p; p++) {
      int adv, lsb;
      stbtt_GetCodepointHMetrics(&sInfo, static_cast<unsigned char>(*p), &adv, &lsb);
      wsum += adv * sc;
      if (p[1]) {
        wsum += stbtt_GetCodepointKernAdvance(&sInfo, static_cast<unsigned char>(*p), static_cast<unsigned char>(p[1])) * sc;
      }
    }
    return wsum;
  };
  float tw = width(scale);
  if (tw > cv.w * 0.8f) {
    scale *= cv.w * 0.8f / tw;
    px *= cv.w * 0.8f / tw;
    tw = width(scale);
  }
  int ascent, descent, gap;
  stbtt_GetFontVMetrics(&sInfo, &ascent, &descent, &gap);
  // the caps' middle on cy: baseline at cy + capHeight/2
  const float baseline = cy + px * 0.5f;
  float x = cx - tw * 0.5f;
  for (const char* p = s; *p; p++) {
    const int cp = static_cast<unsigned char>(*p);
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&sInfo, cp, &adv, &lsb);
    stbtt_GetCodepointBitmapBoxSubpixel(&sInfo, cp, scale, scale, x - std::floor(x), 0.f, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      std::vector<unsigned char> bmp(static_cast<size_t>(gw) * gh);
      stbtt_MakeCodepointBitmapSubpixel(&sInfo, bmp.data(), gw, gh, gw, scale, scale, x - std::floor(x), 0.f, cp);
      const int ox = static_cast<int>(std::floor(x)) + x0, oy = static_cast<int>(std::floor(baseline)) + y0;
      for (int j = 0; j < gh; j++) {
        for (int i = 0; i < gw; i++) {
          cv.blend(ox + i, oy + j, c, bmp[static_cast<size_t>(j) * gw + i] / 255.f);
        }
      }
    }
    x += adv * scale;
    if (p[1]) {
      x += stbtt_GetCodepointKernAdvance(&sInfo, cp, static_cast<unsigned char>(p[1])) * scale;
    }
  }
}

// PlayStation's symbols, drawn in a box of half-size s around (cx, cy), stroke t
void psSymbol(Canvas& cv, GlyphFace f, float cx, float cy, float s, float t, Rgba c) {
  switch (f) {
  case GlyphFace::South: // a cross
    cv.fill(c, [&](float x, float y) {
      return std::min(segment(x, y, cx - s, cy - s, cx + s, cy + s, t), segment(x, y, cx - s, cy + s, cx + s, cy - s, t));
    });
    break;
  case GlyphFace::East: // a circle
    cv.fill(c, [&](float x, float y) { return std::fabs(circle(x, y, cx, cy, s)) - t; });
    break;
  case GlyphFace::West: // a square
    cv.fill(c, [&](float x, float y) { return std::fabs(roundBox(x, y, cx, cy, s * 0.9f, s * 0.9f, t * 0.5f)) - t; });
    break;
  case GlyphFace::North: { // a triangle
    const float ax = cx, ay = cy - s, bx = cx - s * 1.05f, by = cy + s * 0.75f, ex = cx + s * 1.05f, ey = by;
    cv.fill(c, [&](float x, float y) {
      return std::min({segment(x, y, ax, ay, bx, by, t), segment(x, y, bx, by, ex, ey, t), segment(x, y, ex, ey, ax, ay, t)});
    });
    break;
  }
  default:
    break;
  }
}

// three lines (a menu button)
void menuLines(Canvas& cv, float cx, float cy, float s, float t, Rgba c) {
  cv.fill(c, [&](float x, float y) {
    float d = 1e9f;
    for (int i = -1; i <= 1; i++) {
      d = std::min(d, segment(x, y, cx - s, cy + i * s * 0.75f, cx + s, cy + i * s * 0.75f, t));
    }
    return d;
  });
}

const char* faceLetter(GlyphStyle st, GlyphFace f) {
  // the letters printed on the face buttons, by position
  if (st == GlyphStyle::Switch) {
    switch (f) {
    case GlyphFace::South: return "B";
    case GlyphFace::East: return "A";
    case GlyphFace::West: return "Y";
    case GlyphFace::North: return "X";
    default: return "";
    }
  }
  switch (f) {
  case GlyphFace::South: return "A";
  case GlyphFace::East: return "B";
  case GlyphFace::West: return "X";
  case GlyphFace::North: return "Y";
  default: return "";
  }
}

const char* shoulderText(GlyphStyle st, GlyphFace f) {
  const bool left = f == GlyphFace::LeftShoulder || f == GlyphFace::LeftTrigger;
  const bool trig = f == GlyphFace::LeftTrigger || f == GlyphFace::RightTrigger;
  switch (st) {
  case GlyphStyle::Xbox: return trig ? (left ? "LT" : "RT") : (left ? "LB" : "RB");
  case GlyphStyle::PlayStation: return trig ? (left ? "L2" : "R2") : (left ? "L1" : "R1");
  case GlyphStyle::Switch: return trig ? (left ? "ZL" : "ZR") : (left ? "L" : "R");
  default: return trig ? (left ? "LT" : "RT") : (left ? "L" : "R");
  }
}

} // namespace

void setGlyphFont(const unsigned char* ttf, unsigned) {
  if (ttf == nullptr || sFont == ttf) {
    return;
  }
  sFont = ttf;
  sFontReady = stbtt_InitFont(&sInfo, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)) != 0;
}

std::vector<uint8_t> drawButtonGlyph(const GlyphSpec& g, int w, int h) {
  Canvas cv(w, h);
  const float m = static_cast<float>(std::min(w, h));
  const float press = g.pressed ? m * 0.05f : 0.f; // pressed: lower, darker, its shadow gone
  const float dim = g.pressed ? 0.78f : 1.f;
  const float cx = w * 0.5f, cy = h * 0.5f - m * 0.03f + press;
  const Rgba white = hex(0xFFFFFF), ink = hex(0x24232A);
  const bool face = g.face == GlyphFace::South || g.face == GlyphFace::East || g.face == GlyphFace::West ||
                    g.face == GlyphFace::North;

  if (g.style == GlyphStyle::Keyboard) {
    // a key cap: a rounded square (wider for a long name), its front edge below
    const std::string label = g.label != nullptr ? g.label : "?";
    const float hw = std::min(w * 0.47f, label.size() <= 1 ? m * 0.4f : w * 0.47f), hh = m * 0.38f;
    const float r = m * 0.12f;
    if (!g.pressed) {
      cv.fill(hex(0x8B8A96), [&](float x, float y) { return roundBox(x, y, cx, cy + m * 0.07f, hw, hh, r); });
    }
    cv.fill(hex(0x46454F), [&](float x, float y) { return roundBox(x, y, cx, cy, hw, hh, r); });
    cv.fill(shade(hex(0xF4F2EC), dim), [&](float x, float y) { return roundBox(x, y, cx, cy, hw - m * 0.04f, hh - m * 0.04f, r * 0.8f); });
    text(cv, label.c_str(), cx, cy, label.size() <= 1 ? m * 0.4f : m * 0.3f, ink);
    return cv.rgba8();
  }

  if (face) {
    const float r = m * 0.44f;
    Rgba fillc = hex(0x5A5966), mark = white;
    switch (g.style) {
    case GlyphStyle::Xbox: {
      static const uint32_t kXbox[] = {0x3FA33A, 0xD63A33, 0x2D73D2, 0xE8B415}; // A B X Y, by position
      fillc = hex(kXbox[static_cast<int>(g.face) - static_cast<int>(GlyphFace::South)]);
      break;
    }
    case GlyphStyle::PlayStation: {
      static const uint32_t kPs[] = {0x7FA4E8, 0xF0595F, 0xE58AC5, 0x3EC59B}; // cross circle square triangle
      fillc = hex(0x2B2A33);
      mark = hex(kPs[static_cast<int>(g.face) - static_cast<int>(GlyphFace::South)]);
      break;
    }
    case GlyphStyle::Switch:
      fillc = hex(0x34333B);
      break;
    default:
      break;
    }
    if (!g.pressed) {
      cv.fill(Rgba{0.f, 0.f, 0.f, 0.35f}, [&](float x, float y) { return circle(x, y, cx, cy + m * 0.06f, r); });
    }
    cv.fill(shade(fillc, 0.62f * dim), [&](float x, float y) { return circle(x, y, cx, cy, r); });
    cv.fill(shade(fillc, dim), [&](float x, float y) { return circle(x, y, cx, cy - m * 0.012f, r - m * 0.045f); });
    // a gloss on the upper half, as the game's own buttons have
    cv.fill(Rgba{1.f, 1.f, 1.f, 0.16f}, [&](float x, float y) {
      return std::max(circle(x, y, cx, cy - m * 0.012f, r - m * 0.08f), y - (cy - r * 0.1f));
    });
    if (g.style == GlyphStyle::PlayStation) {
      psSymbol(cv, g.face, cx, cy, r * 0.42f, m * 0.045f, shade(mark, dim));
    } else {
      text(cv, faceLetter(g.style, g.face), cx, cy, m * 0.42f, shade(mark, dim));
    }
    return cv.rgba8();
  }

  // shoulders, triggers, Start and anything else: a rounded pill with its name
  const float hw = w * 0.46f, hh = std::min(h * 0.36f, w * 0.3f), r = hh * 0.8f;
  const Rgba fillc = hex(0x3A3942);
  if (!g.pressed) {
    cv.fill(Rgba{0.f, 0.f, 0.f, 0.35f}, [&](float x, float y) { return roundBox(x, y, cx, cy + m * 0.06f, hw, hh, r); });
  }
  cv.fill(shade(fillc, 0.62f * dim), [&](float x, float y) { return roundBox(x, y, cx, cy, hw, hh, r); });
  cv.fill(shade(fillc, dim), [&](float x, float y) { return roundBox(x, y, cx, cy - m * 0.01f, hw - m * 0.04f, hh - m * 0.04f, r * 0.85f); });
  const Rgba c = shade(white, dim);
  if (g.face == GlyphFace::Start || g.face == GlyphFace::Back) {
    if (g.style == GlyphStyle::Switch) {
      const float s = hh * 0.55f, t = m * 0.05f;
      const bool plus = g.face == GlyphFace::Start;
      cv.fill(c, [&](float x, float y) {
        const float hz = segment(x, y, cx - s, cy, cx + s, cy, t);
        return plus ? std::min(hz, segment(x, y, cx, cy - s, cx, cy + s, t)) : hz;
      });
    } else {
      menuLines(cv, cx, cy, hh * 0.55f, m * 0.022f, c);
    }
  } else if (g.face == GlyphFace::Other) {
    text(cv, g.label != nullptr ? g.label : "?", cx, cy, hh * 0.9f, c);
  } else {
    text(cv, shoulderText(g.style, g.face), cx, cy, hh * 0.9f, c);
  }
  return cv.rgba8();
}
