// Sharp text (settings: sharp_text, Display tab; port/include/port/display.h):
// the game's font is one bitmap glyph sheet on the disc (GX I4, 4-bit
// coverage, glyphs about 24 pixels high), so at high resolutions text is
// that sheet magnified, soft. With this on, the sheet is redrawn 4x larger
// when the font loads (fontmgr.c, portFontSheet): each glyph's coverage is
// interpolated (Catmull-Rom) as a distance field and given a sharp edge one
// texel wide, then served to Aurora as a replacement of the sheet (keyed by
// its address) with mips for small windows. The glyphs' shapes are the
// game's; only their edges are made crisp. A texture pack can still replace
// the sheet with its own drawing (its file wins over this).
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/texture.hpp>

#include "port/display.h"
#include "port/settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kScale = 4;

const void* sSheet;
int sWidth, sHeight;
std::vector<uint8_t> sPixels; // RGBA8, the mips one after another
uint32_t sMips;
aurora::texture::ReplacementRegistration sRegistration;
int* sOn;

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

void build() {
  const int w = sWidth, h = sHeight;
  const std::vector<float> src = decodeI4(static_cast<const uint8_t*>(sSheet), w, h);
  const auto at = [&](int x, int y) {
    return src[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)];
  };
  // stay inside each glyph's cell: the clamp above is enough at the sheet's
  // edges, and cells have empty borders, so neighbours never bleed in
  const int W = w * kScale, H = h * kScale;
  std::vector<uint8_t> big(static_cast<size_t>(W) * H);
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
      const float v = cubic(row[0], row[1], row[2], row[3], ty);
      // the 0.5 coverage line is the edge; a ramp one big texel wide across it
      // (in source units 1/kScale), keeping thin strokes' faint middles
      const float edge = 0.47f, ramp = 0.5f / kScale * 2.f;
      const float a = std::clamp((v - edge) / ramp + 0.5f, 0.f, 1.f);
      big[static_cast<size_t>(Y) * W + X] = static_cast<uint8_t>(std::lround(a * a * (3.f - 2.f * a) * 255.f));
    }
  }
  // RGBA8 (I4 reads as I in every channel) with mips down to the sheet's own size
  sPixels.clear();
  sMips = 0;
  std::vector<uint8_t> level = std::move(big);
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

void apply() {
  const bool want = sOn != nullptr && *sOn != 0 && sSheet != nullptr;
  if (want == (sRegistration.id != 0)) {
    return;
  }
  if (!want) {
    aurora::texture::unregister_replacement(sRegistration);
    sRegistration = {};
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
          .label = "Sharp text",
      },
      aurora::texture::ReplacementOptions{.priority = -1}); // a pack's own font first
}

const bool sAdded = [] {
  portSettingsDeclare("sharp_text", 0, 0, 1); // at start, for the launcher's Settings tab
  portSettingsAddToggle("Display", "Sharp text",
                        "The game's font redrawn with crisp edges at high resolutions (the same letters).", 0,
                        "sharp_text", "font text letters crisp hd resolution");
  return true;
}();

} // namespace

// fontmgr.c: the font's glyph sheet, once loaded (GX I4)
extern "C" void portFontSheet(const void* texels, int width, int height, int format) {
  if (sRegistration.id != 0) {
    aurora::texture::unregister_replacement(sRegistration);
    sRegistration = {};
  }
  sPixels.clear();
  sSheet = format == GX_TF_I4 && width % 8 == 0 && height % 8 == 0 ? texels : nullptr;
  sWidth = width;
  sHeight = height;
  portSharpTextFrame();
}

// once a frame (display_aurora.c): follows the setting
extern "C" void portSharpTextFrame(void) {
  if (sOn == nullptr) {
    sOn = portSettingsDeclare("sharp_text", 0, 0, 1);
  }
  apply();
}
