// Paper Cut's brand art drawn offline: the app icon options (launcherBrandIcon) and the README banner
// and social preview with the launcher's own wordmark (launcherBrandTitle), from the same code the
// launcher draws them with (port/settings/launcher_brand.cpp), into PNGs. Dear ImGui's draw lists are
// filled as the launcher fills them, then drawn on the CPU (like launcher_shot.cpp, with alpha),
// supersampled and box-filtered down. No window, no GPU, no game assets.
//
//   brand_export <out_dir> icon <size>...      icon_<size>.png: the icon, full bleed, opaque
//   brand_export <out_dir> android <size>...   android_bg_<size>.png, android_fg_<size>.png: the
//                                              adaptive icon's layers, <size> the full 108 dp
//   brand_export <out_dir> banners             banner.png (1600x400), banner@2x.png (3200x800),
//                                              social-preview.png (1280x640)
// Run from the repo root (it reads the Fredoka font). tools/port/make_icons.py runs it for every icon
// file. Build: tools/brand_export/build.sh.
#include "launcher_brand.h"
#include "launcher_look.h"
#include "launcher_popup.h"
#include "launcher_stage.h"
#include "launcher_style.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <SDL3/SDL.h>
#include <aurora/aurora.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---- what launcher_brand.cpp needs from the rest of the launcher ----
LauncherFonts gLauncherFonts;
extern "C" const char* portTr(const char* english) { return english; }
ImTextureID launcherTexture(int, int, const void*) { return ImTextureID{}; }
extern "C" {
const char* SDLCALL SDL_getenv(const char*) { return nullptr; }
SDL_Surface* SDLCALL SDL_LoadPNG(const char*) { return nullptr; }
SDL_Surface* SDLCALL SDL_ConvertSurface(SDL_Surface*, SDL_PixelFormat) { return nullptr; }
void SDLCALL SDL_DestroySurface(SDL_Surface*) {}
SDL_IOStream* SDLCALL SDL_IOFromConstMem(const void*, size_t) { return nullptr; }
SDL_Surface* SDLCALL SDL_LoadPNG_IO(SDL_IOStream*, bool) { return nullptr; }
extern const unsigned char portLogo512[] = {0};
extern const unsigned int portLogo512_size = 0;
SDL_Window* aurora_get_window() { return nullptr; } // (no window: the stage skips the safe area)
bool SDLCALL SDL_GetWindowSafeArea(SDL_Window*, SDL_Rect*) { return false; }
bool SDLCALL SDL_GetWindowSize(SDL_Window*, int*, int*) { return false; }
}
float launcherPopupLayer(LauncherStageLayer, float) { return 1.f; }

namespace {

// ---- a PNG, uncompressed (stored deflate blocks) ----
uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0) {
  static uint32_t table[256];
  if (table[1] == 0) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t k = i;
      for (int j = 0; j < 8; j++) {
        k = (k & 1) ? 0xEDB88320u ^ (k >> 1) : k >> 1;
      }
      table[i] = k;
    }
  }
  c = ~c;
  for (size_t i = 0; i < n; i++) {
    c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  }
  return ~c;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) {
    v.push_back(static_cast<uint8_t>(x >> s));
  }
}

void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> v;
  be32(v, static_cast<uint32_t>(data.size()));
  v.insert(v.end(), type, type + 4);
  v.insert(v.end(), data.begin(), data.end());
  std::vector<uint8_t> tail;
  be32(tail, crc(v.data() + 4, v.size() - 4));
  fwrite(v.data(), 1, v.size(), f);
  fwrite(tail.data(), 1, 4, f);
}

bool writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    return false;
  }
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk(f, "IHDR", ihdr);
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (w * 4 + 1));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + static_cast<size_t>(y) * w * 4,
               rgba.begin() + static_cast<size_t>(y + 1) * w * 4);
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t at = 0; at < raw.size(); at += 65535) {
    const size_t n = std::min<size_t>(65535, raw.size() - at);
    z.push_back(at + n == raw.size() ? 1 : 0);
    z.push_back(static_cast<uint8_t>(n));
    z.push_back(static_cast<uint8_t>(n >> 8));
    z.push_back(static_cast<uint8_t>(~n));
    z.push_back(static_cast<uint8_t>(~n >> 8));
    z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
  }
  be32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  std::fclose(f);
  return true;
}

// ---- the draw data on the CPU: premultiplied RGBA, each pixel's centre sampled ----
struct Atlas {
  int w = 0, h = 0;
  const uint8_t* px = nullptr;
};

inline float edgeFn(const ImVec2& a, const ImVec2& b, float x, float y) {
  return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

void sample(const Atlas& t, float u, float v, float out[4]) {
  const float x = u * t.w - 0.5f, y = v * t.h - 0.5f;
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const float fx = x - x0, fy = y - y0;
  float acc[4] = {0, 0, 0, 0};
  for (int k = 0; k < 4; k++) {
    const int xi = std::clamp(x0 + (k & 1), 0, t.w - 1), yi = std::clamp(y0 + (k >> 1), 0, t.h - 1);
    const float wgt = ((k & 1) ? fx : 1.f - fx) * ((k >> 1) ? fy : 1.f - fy);
    const uint8_t* p = &t.px[(static_cast<size_t>(yi) * t.w + xi) * 4];
    for (int c = 0; c < 4; c++) {
      acc[c] += p[c] / 255.f * wgt;
    }
  }
  std::memcpy(out, acc, sizeof(acc));
}

std::vector<float> raster(ImDrawData* dd, int W, int H, const Atlas& atlas) {
  std::vector<float> img(static_cast<size_t>(W) * H * 4, 0.f);
  for (int l = 0; l < dd->CmdListsCount; l++) {
    const ImDrawList* list = dd->CmdLists[l];
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr) {
        continue;
      }
      const float cx0 = std::max(0.f, cmd.ClipRect.x), cy0 = std::max(0.f, cmd.ClipRect.y);
      const float cx1 = std::min(static_cast<float>(W), cmd.ClipRect.z),
                  cy1 = std::min(static_cast<float>(H), cmd.ClipRect.w);
      for (unsigned e = 0; e + 2 < cmd.ElemCount; e += 3) {
        const ImDrawVert* v[3];
        ImVec2 p[3];
        for (int k = 0; k < 3; k++) {
          v[k] = &list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + e + k]];
          p[k] = v[k]->pos;
        }
        const float area = edgeFn(p[0], p[1], p[2].x, p[2].y);
        if (std::fabs(area) < 1e-6f) {
          continue;
        }
        const int x0 = static_cast<int>(std::max(cx0, std::floor(std::min({p[0].x, p[1].x, p[2].x}))));
        const int x1 = static_cast<int>(std::min(cx1, std::ceil(std::max({p[0].x, p[1].x, p[2].x}))));
        const int y0 = static_cast<int>(std::max(cy0, std::floor(std::min({p[0].y, p[1].y, p[2].y}))));
        const int y1 = static_cast<int>(std::min(cy1, std::ceil(std::max({p[0].y, p[1].y, p[2].y}))));
        float col[3][4];
        for (int k = 0; k < 3; k++) {
          const ImVec4 c = ImGui::ColorConvertU32ToFloat4(v[k]->col);
          col[k][0] = c.x;
          col[k][1] = c.y;
          col[k][2] = c.z;
          col[k][3] = c.w;
        }
        for (int y = y0; y < y1; y++) {
          for (int x = x0; x < x1; x++) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = edgeFn(p[1], p[2], px, py) / area, w1 = edgeFn(p[2], p[0], px, py) / area;
            const float w2 = 1.f - w0 - w1;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f) {
              continue;
            }
            float t[4];
            sample(atlas, w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x,
                   w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y, t);
            float c[4];
            for (int k = 0; k < 4; k++) {
              c[k] = (w0 * col[0][k] + w1 * col[1][k] + w2 * col[2][k]) * t[k];
            }
            float* o = &img[(static_cast<size_t>(y) * W + x) * 4];
            for (int k = 0; k < 3; k++) {
              o[k] = o[k] * (1.f - c[3]) + c[k] * c[3];
            }
            o[3] = o[3] * (1.f - c[3]) + c[3];
          }
        }
      }
    }
  }
  return img;
}

Atlas gAtlas;

// Android's foreground layer: the pieces shrunk about the centre until the scissors' handles, the
// farthest out, sit inside the 66 dp safe circle of the 108 dp layer
constexpr float kAndroidPieces = 0.48f;

// Draws with draw(dl, K) at K times w x h, box-filters it down and writes it. corner: round the
// corners by that many pixels (transparent outside).
template <class F> void render(const std::string& path, int w, int h, int K, float corner, F draw) {
  const int W = w * K, H = h * K;
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(W), static_cast<float>(H));
  io.DeltaTime = 1.f / 60.f;
  ImGui::NewFrame();
  draw(ImGui::GetBackgroundDrawList(), static_cast<float>(K));
  ImGui::Render();
  const std::vector<float> img = raster(ImGui::GetDrawData(), W, H, gAtlas);
  std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float acc[4] = {0, 0, 0, 0};
      for (int j = 0; j < K; j++) {
        for (int i = 0; i < K; i++) {
          const float* p = &img[(static_cast<size_t>(y * K + j) * W + x * K + i) * 4];
          for (int c = 0; c < 4; c++) {
            acc[c] += p[c];
          }
        }
      }
      float cov = 1.f;
      if (corner > 0.f) {
        // the distance into the rounded rectangle, anti-aliased over a pixel
        const float cx = std::clamp(x + 0.5f, corner, w - corner), cy = std::clamp(y + 0.5f, corner, h - corner);
        const float d = corner - std::hypot(x + 0.5f - cx, y + 0.5f - cy);
        cov = std::clamp(d + 0.5f, 0.f, 1.f);
      }
      const float a = acc[3] / (K * K) * cov;
      uint8_t* o = &out[(static_cast<size_t>(y) * w + x) * 4];
      // an ordered dither so the stage's soft light doesn't band (a repeating pattern, which the
      // PNGs' deflate packs far tighter than noise)
      static const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
      const float dither = (kBayer[y & 3][x & 3] + 0.5f) / 16.f - 0.5f;
      for (int c = 0; c < 3; c++) {
        const float v = acc[3] > 0.f ? acc[c] / acc[3] : 0.f; // un-premultiplied
        o[c] = static_cast<uint8_t>(std::clamp(v * 255.f + 0.5f + dither, 0.f, 255.f));
      }
      o[3] = static_cast<uint8_t>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
    }
  }
  std::fprintf(stderr, "%s %s\n", path.c_str(), writePng(path, w, h, out) ? "" : "FAILED");
}

// ---- the README's stage: the launcher's own (launcher_stage.cpp), at rest ----

// The launcher's stage filling W x H at UI scale `scale`, the curtains tied back, nothing swaying;
// returns the open stage's top and the boards' top (where the wordmark goes between).
void stage(float W, float H, float scale, float* top, float* bottom) {
  ImGui::GetIO().DisplaySize = ImVec2(W, H);
  ImVec2 min, max;
  launcherBackdrop(0.f, scale, &min, &max, false);
  *top = min.y;
  *bottom = H * 0.86f; // the boards meet the backdrop (launcher_stage.cpp's measure)
}

void fonts(const char* ttf) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->TexDesiredWidth = 4096;
  ImFontConfig cfg;
  cfg.OversampleH = 1;
  cfg.OversampleV = 1;
  ImFontGlyphRangesBuilder b;
  b.AddText("PaperCut ");
  static ImVector<ImWchar> logoRanges;
  b.BuildRanges(&logoRanges);
  gLauncherFonts.logo = io.Fonts->AddFontFromFileTTF(ttf, 480.f, &cfg, logoRanges.Data);
  ImFontConfig cfg2;
  gLauncherFonts.heading = io.Fonts->AddFontFromFileTTF(ttf, 96.f, &cfg2, io.Fonts->GetGlyphRangesDefault());
  gLauncherFonts.title = gLauncherFonts.heading;
  gLauncherFonts.body = gLauncherFonts.heading;
  if (gLauncherFonts.logo == nullptr || gLauncherFonts.heading == nullptr) {
    std::fprintf(stderr, "can't load %s\n", ttf);
    std::exit(1);
  }
  unsigned char* px = nullptr;
  int w = 0, h = 0;
  io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
  io.Fonts->SetTexID(static_cast<ImTextureID>(1));
  gAtlas = {w, h, px};
}

// the wordmark and its ribbon centred in the stage's opening (top..bottom), its letters size px
void wordmark(ImDrawList* dl, float W, float top, float bottom, float size, float u, float time) {
  // the title sizes its letters from the stage's height: ask for size, then centre what it drew
  ImGui::GetIO().FontGlobalScale = 23.f * u / gLauncherFonts.heading->FontSize;
  const float height = size / 0.18f;
  float indent = 0.f;
  ImDrawList probe(ImGui::GetDrawListSharedData());
  probe._ResetForNewFrame();
  probe.PushClipRectFullScreen();
  probe.PushTextureID(ImGui::GetIO().Fonts->TexID);
  const float tall = launcherBrandTitle(&probe, W * 0.5f, 0.f, 0.f, W, height, u, time, false, &indent);
  launcherBrandTitle(dl, W * 0.5f, 0.f, top + (bottom - top - tall) * 0.5f, W, height, u, time, false, &indent);
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: brand_export <out_dir> icon|android <size>...\n"
                         "       brand_export <out_dir> banners\n");
    return 2;
  }
  const std::string out = argv[1];
  const std::string what = argv[2];
  std::vector<int> sizes;
  for (int i = 3; i < argc; i++) {
    sizes.push_back(std::atoi(argv[i]));
  }
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  fonts("port/assets/fonts/Fredoka-Bold.ttf");
  const float time = 0.35f; // the scissors half open, the letters' sway at rest
  // supersampling: more for the small sizes, where every pixel shows
  auto ss = [](int size) { return size >= 1024 ? 3 : size >= 256 ? 4 : size >= 64 ? 8 : 16; };

  if (what == "icon") {
    // the icon, full bleed and opaque, drawn at each size (the detail follows the size)
    for (int size : sizes) {
      render(out + "/icon_" + std::to_string(size) + ".png", size, size, ss(size), 0.f, [&](ImDrawList* dl, float k) {
        launcherBrandIcon(dl, ImVec2(0, 0), size * k, time, static_cast<float>(size));
      });
    }
  } else if (what == "android") {
    // Android's adaptive icon, 108 dp a side, of which the launcher shows the middle 72 dp (cut to a
    // circle, a squircle or a rounded square) and moves the layers apart a little: the backdrop
    // alone, full bleed; the star and scissors alone on a clear canvas, inside the 66 dp safe circle.
    // The detail follows the shown 72 dp.
    for (int size : sizes) {
      const float shown = size * 72.f / 108.f;
      render(out + "/android_bg_" + std::to_string(size) + ".png", size, size, ss(size), 0.f,
             [&](ImDrawList* dl, float k) {
               launcherBrandIcon(dl, ImVec2(0, 0), size * k, time, shown, kBrandIconBackdrop);
             });
      render(out + "/android_fg_" + std::to_string(size) + ".png", size, size, ss(size), 0.f,
             [&](ImDrawList* dl, float k) {
               launcherBrandIcon(dl, ImVec2(0, 0), size * k, time, shown, kBrandIconPieces, kAndroidPieces);
             });
    }
  } else if (what == "banners") {
    // the README banner, 1600x400 and 2x: rounded, transparent outside, like the one it replaces
    for (int m : {1, 2}) {
      const char* name = m == 1 ? "/banner.png" : "/banner@2x.png";
      render(out + name, 1600 * m, 400 * m, m == 1 ? 3 : 2, 24.f * m, [&](ImDrawList* dl, float k) {
        const float u = k * m, W = 1600.f * u, H = 400.f * u;
        float top = 0.f, bottom = 0.f;
        stage(W, H, 0.7f * u, &top, &bottom);
        wordmark(dl, W, top, bottom, 150.f * u, u, time);
      });
    }
    // GitHub's social preview, 1280x640, square-cornered (GitHub rounds it), text well inside
    render(out + "/social-preview.png", 1280, 640, 3, 0.f, [&](ImDrawList* dl, float u) {
      const float W = 1280.f * u, H = 640.f * u;
      float top = 0.f, bottom = 0.f;
      stage(W, H, 1.f * u, &top, &bottom);
      wordmark(dl, W, top, bottom, 190.f * u, u, time);
    });
  } else {
    std::fprintf(stderr, "brand_export: unknown %s\n", what.c_str());
    return 2;
  }
  ImGui::DestroyContext();
  return 0;
}
