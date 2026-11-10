// Sharp text (settings: sharp_text, Graphics tab; port/include/port/display.h):
// the game's font is one bitmap glyph sheet on the disc (GX I4, 4-bit
// coverage, glyphs about 24 pixels high), so at high resolutions text is that
// sheet magnified, soft. With this on, the sheet is rebuilt as a distance
// field when the font loads (fontmgr.c, portFontSheet): each glyph's coverage
// is interpolated (Catmull-Rom) 4x finer, its 0.5 line is the edge, and every
// texel holds its signed distance to that edge (exact across the edge from the
// interpolated coverage's slope, a distance transform farther out). Aurora's
// paper patch (aurora/paper.h, port/patches/aurora) then thresholds it per
// pixel at the scale the text is drawn, so the letters are sharp at any
// resolution; served to Aurora as a replacement of the sheet (keyed by its
// address), with mips. The glyphs' shapes are the game's. When a texture
// pack replaces the font's sheet, sharp text steps aside for that font by
// itself (looked for again whenever the packs change: port/include/port/
// mods.h), so the pack's letters show as the pack drew them; the setting
// stays as the player left it. Draw-only: no game state, no random numbers.
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/paper.h>
#include <aurora/texture.hpp>
#include <SDL3/SDL_log.h>

#include "port/display.h"
#include "port/mods.h"
#include "port/settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

constexpr int kScale = 4;
// distances kept to this many fine texels either side of the edge (1.5 of the sheet's)
constexpr float kSpread = 6.f;

const void* sSheet;
int sWidth, sHeight;
std::vector<uint8_t> sPixels; // RGBA8, the mips one after another
uint32_t sMips;
aurora::texture::ReplacementRegistration sRegistration;
int* sOn;
// a texture pack replaces this sheet (looked for at sPackGeneration)
bool sPackFont;
int sPackGeneration = -1;

// the sheet's coverage, 0-1, from GX I4 (8x8 texel blocks, two texels a byte, high nibble first)
std::vector<float> decodeI4(const uint8_t* data, int w, int h) {
  std::vector<float> out(static_cast<size_t>(w) * h);
  const int blocksX = w / 8;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const int block = (y / 8) * blocksX + x / 8;
      const int in = (y % 8) * 8 + x % 8;
      const uint8_t b = data[block * 32 + in / 2];
      out[static_cast<size_t>(y) * w + x] = ((in & 1) ? (b & 0xF) : (b >> 4)) / 15.f;
    }
  }
  return out;
}

float cubic(float a, float b, float c, float d, float t) {
  return b + 0.5f * t * (c - a + t * (2.f * a - 5.f * b + 4.f * c - d + t * (3.f * (b - c) + d - a)));
}

// squared distance transform of one row or column (Felzenszwalb and Huttenlocher): f in, d out, n long
void edt1(const float* f, float* d, int n, std::vector<int>& v, std::vector<float>& z) {
  const float kInf = std::numeric_limits<float>::infinity();
  int k = 0;
  v[0] = 0;
  z[0] = -kInf;
  z[1] = kInf;
  for (int q = 1; q < n; q++) {
    float s;
    for (;;) { // z[0] is -infinity, so k stays at 0 or above
      const int p = v[k];
      s = ((f[q] + static_cast<float>(q) * q) - (f[p] + static_cast<float>(p) * p)) / (2.f * (q - p));
      if (s > z[k]) {
        break;
      }
      k--;
    }
    k++;
    v[k] = q;
    z[k] = s;
    z[k + 1] = kInf;
  }
  k = 0;
  for (int q = 0; q < n; q++) {
    while (z[k + 1] < q) {
      k++;
    }
    const int p = v[k];
    d[q] = (q - p) * (q - p) + f[p];
  }
}

// each texel's distance to the nearest texel where mask is set (0 there), in texels
std::vector<float> distanceTo(const std::vector<uint8_t>& mask, int W, int H) {
  constexpr float kInf = 1e20f;
  std::vector<float> g(mask.size());
  for (size_t i = 0; i < mask.size(); i++) {
    g[i] = mask[i] ? 0.f : kInf;
  }
  const int n = std::max(W, H);
  std::vector<float> f(n), d(n), z(n + 1);
  std::vector<int> v(n);
  for (int x = 0; x < W; x++) {
    for (int y = 0; y < H; y++) {
      f[y] = g[static_cast<size_t>(y) * W + x];
    }
    edt1(f.data(), d.data(), H, v, z);
    for (int y = 0; y < H; y++) {
      g[static_cast<size_t>(y) * W + x] = d[y];
    }
  }
  for (int y = 0; y < H; y++) {
    float* row = &g[static_cast<size_t>(y) * W];
    std::copy(row, row + W, f.begin());
    edt1(f.data(), d.data(), W, v, z);
    for (int x = 0; x < W; x++) {
      row[x] = std::sqrt(d[x]);
    }
  }
  return g;
}

void build() {
  const int w = sWidth, h = sHeight;
  const std::vector<float> src = decodeI4(static_cast<const uint8_t*>(sSheet), w, h);
  const auto at = [&](int x, int y) {
    return src[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)];
  };
  // the coverage 4x finer (cells have empty borders, so neighbours never bleed in)
  const int W = w * kScale, H = h * kScale;
  std::vector<float> fine(static_cast<size_t>(W) * H);
  for (int Y = 0; Y < H; Y++) {
    const float fy = (Y + 0.5f) / kScale - 0.5f;
    const int y0 = static_cast<int>(std::floor(fy));
    const float ty = fy - y0;
    for (int X = 0; X < W; X++) {
      const float fx = (X + 0.5f) / kScale - 0.5f;
      const int x0 = static_cast<int>(std::floor(fx));
      const float tx = fx - x0;
      float row[4];
      for (int j = 0; j < 4; j++) {
        row[j] = cubic(at(x0 - 1, y0 - 1 + j), at(x0, y0 - 1 + j), at(x0 + 1, y0 - 1 + j), at(x0 + 2, y0 - 1 + j),
                       tx);
      }
      fine[static_cast<size_t>(Y) * W + X] = cubic(row[0], row[1], row[2], row[3], ty);
    }
  }
  // the edge: a little under half coverage, keeping thin strokes' faint middles
  constexpr float kEdge = 0.47f;
  std::vector<uint8_t> inside(fine.size()), outside(fine.size());
  for (size_t i = 0; i < fine.size(); i++) {
    inside[i] = fine[i] >= kEdge;
    outside[i] = !inside[i];
  }
  const std::vector<float> toInside = distanceTo(inside, W, H);
  const std::vector<float> toOutside = distanceTo(outside, W, H);
  std::vector<uint8_t> field(fine.size());
  for (int Y = 0; Y < H; Y++) {
    for (int X = 0; X < W; X++) {
      const size_t i = static_cast<size_t>(Y) * W + X;
      // texel centres to the other side's centres, less half a texel: the edge between them
      float sd = inside[i] ? toOutside[i] - 0.5f : 0.5f - toInside[i];
      if (std::fabs(sd) < 1.5f) {
        // near the edge: the interpolated coverage's own crossing, from its slope (sub-texel)
        const auto c = [&](int x, int y) {
          return fine[static_cast<size_t>(std::clamp(y, 0, H - 1)) * W + std::clamp(x, 0, W - 1)];
        };
        const float gx = 0.5f * (c(X + 1, Y) - c(X - 1, Y));
        const float gy = 0.5f * (c(X, Y + 1) - c(X, Y - 1));
        const float slope = std::sqrt(gx * gx + gy * gy);
        if (slope > 1e-3f) {
          const float lin = (fine[i] - kEdge) / slope;
          if (std::fabs(lin) < 1.5f && (lin >= 0.f) == (inside[i] != 0)) {
            sd = lin;
          }
        }
      }
      const float d = std::clamp(0.5f + sd / (2.f * kSpread), 0.f, 1.f);
      field[i] = static_cast<uint8_t>(std::lround(d * 255.f));
    }
  }
  // RGBA8 (I4 reads as I in every channel) with mips down to the sheet's own size: averaged distances
  sPixels.clear();
  sMips = 0;
  std::vector<uint8_t> level = std::move(field);
  int lw = W, lh = H;
  for (;;) {
    for (const uint8_t v : level) {
      sPixels.insert(sPixels.end(), {v, v, v, v});
    }
    sMips++;
    if (lw <= w || lw < 2 || lh < 2) {
      break;
    }
    std::vector<uint8_t> next(static_cast<size_t>(lw / 2) * (lh / 2));
    for (int y = 0; y < lh / 2; y++) {
      for (int x = 0; x < lw / 2; x++) {
        const size_t i = static_cast<size_t>(y) * 2 * lw + x * 2;
        next[static_cast<size_t>(y) * (lw / 2) + x] =
            static_cast<uint8_t>((level[i] + level[i + 1] + level[i + lw] + level[i + lw + 1] + 2) / 4);
      }
    }
    level = std::move(next);
    lw /= 2;
    lh /= 2;
  }
}

// whether a texture pack has its own sheet for the font: asked of Aurora with
// this one's own registration off (it answers for the address first)
bool packHasFont() {
  if (sSheet == nullptr) {
    return false;
  }
  GXTexObj obj;
  GXInitTexObj(&obj, const_cast<void*>(sSheet), static_cast<u16>(sWidth), static_cast<u16>(sHeight), GX_TF_I4,
               GX_CLAMP, GX_CLAMP, GX_FALSE);
  return aurora::texture::has_replacement(&obj);
}

void apply() {
  const int generation = portModsTexturesGeneration();
  if (generation != sPackGeneration) {
    sPackGeneration = generation;
    if (sRegistration.id != 0) {
      aurora::texture::unregister_replacement(sRegistration);
      sRegistration = {};
      aurora_set_paper_distance_field(nullptr);
    }
    const bool pack = packHasFont();
    if (pack != sPackFont) {
      SDL_Log("sharp text: %s", pack ? "a texture pack has its own font: drawn as the pack's" : "the game's font");
    }
    sPackFont = pack;
  }
  const bool want = sOn != nullptr && *sOn != 0 && sSheet != nullptr && !sPackFont;
  if (want == (sRegistration.id != 0)) {
    return;
  }
  if (!want) {
    aurora::texture::unregister_replacement(sRegistration);
    sRegistration = {};
    aurora_set_paper_distance_field(nullptr);
    return;
  }
  if (sPixels.empty()) {
    build();
  }
  sRegistration = aurora::texture::register_replacement(
      aurora::texture::TexturePointerKey{sSheet},
      aurora::texture::RawTextureReplacement{
          .bytes = sPixels,
          .width = static_cast<uint32_t>(sWidth * kScale),
          .height = static_cast<uint32_t>(sHeight * kScale),
          .mipCount = sMips,
          .gxFormat = GX_TF_RGBA8_PC,
          .label = "Sharp text (distance field)",
      },
      // ahead of a pack's own font: Aurora reads this sheet as a distance field
      aurora::texture::ReplacementOptions{.priority = 100});
  aurora_set_paper_distance_field(sSheet);
  SDL_Log("sharp text: on (the font as a distance field)");
}

const bool sAdded = [] {
  portSettingsDeclare("sharp_text", 0, 0, 1); // at start, for the launcher's Settings tab
  portSettingsAddToggle("Graphics", "Sharp text",
                        "The game's font rebuilt as a distance field, so its letters stay sharp at any resolution "
                        "(the same letters; a texture pack's own font is drawn as the pack's).",
                        0, "sharp_text", "font text letters crisp hd resolution distance field");
  return true;
}();

} // namespace

// fontmgr.c: the font's glyph sheet, once loaded (GX I4)
extern "C" void portFontSheet(const void* texels, int width, int height, int format) {
  if (sRegistration.id != 0) {
    aurora::texture::unregister_replacement(sRegistration);
    sRegistration = {};
    aurora_set_paper_distance_field(nullptr);
  }
  sPixels.clear();
  sSheet = format == GX_TF_I4 && width % 8 == 0 && height % 8 == 0 ? texels : nullptr;
  sWidth = width;
  sHeight = height;
  sPackGeneration = -1; // (a pack's font looked for again: the sheet is new)
  SDL_Log("sharp text: the font's sheet, %dx%d", width, height);
  portSharpTextFrame();
}

// once a frame (display_aurora.c): follows the setting
extern "C" void portSharpTextFrame(void) {
  if (sOn == nullptr) {
    sOn = portSettingsDeclare("sharp_text", 0, 0, 1);
  }
  apply();
}
