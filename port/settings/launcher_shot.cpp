// The launcher's test captures ($TTYD_LAUNCHER_SHOT, launcher_shot.h): the frame's Dear ImGui draw
// data drawn again on the CPU into a PNG. Aurora's picture capture leaves ImGui out, and a window's
// own content can't be read from outside without focusing it, so the launcher's look is checked this
// way: triangles with their texture and colour, each pixel's centre sampled, blended over.
#include "launcher_shot.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Tex {
  int w = 0, h = 0;
  std::vector<uint8_t> px; // RGBA8
};
std::map<ImTextureID, Tex>& textures() {
  static std::map<ImTextureID, Tex> m;
  return m;
}

struct Shot {
  float at;
  std::string name;
  bool done = false;
};
std::vector<Shot> sShots;
bool sParsed = false;
std::string sDir = ".";

void parse() {
  if (sParsed) {
    return;
  }
  sParsed = true;
  const char* list = SDL_getenv("TTYD_LAUNCHER_SHOT");
  if (list == nullptr) {
    return;
  }
  if (const char* d = SDL_getenv("TTYD_SHOT_DIR")) {
    sDir = d;
  }
  // "SECONDS:NAME,SECONDS:NAME..."
  std::string s = list;
  size_t at = 0;
  while (at < s.size()) {
    size_t end = s.find(',', at);
    if (end == std::string::npos) {
      end = s.size();
    }
    const std::string item = s.substr(at, end - at);
    const size_t colon = item.find(':');
    if (colon != std::string::npos) {
      sShots.push_back({static_cast<float>(std::atof(item.substr(0, colon).c_str())), item.substr(colon + 1)});
    }
    at = end + 1;
  }
}

inline float edge(const ImVec2& a, const ImVec2& b, float x, float y) {
  return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}

void sample(const Tex* t, float u, float v, float out[4]) {
  if (t == nullptr || t->w == 0) {
    out[0] = out[1] = out[2] = out[3] = 1.f;
    return;
  }
  // bilinear, clamped
  const float x = u * t->w - 0.5f, y = v * t->h - 0.5f;
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const float fx = x - x0, fy = y - y0;
  float acc[4] = {0, 0, 0, 0};
  for (int k = 0; k < 4; k++) {
    const int xi = std::clamp(x0 + (k & 1), 0, t->w - 1), yi = std::clamp(y0 + (k >> 1), 0, t->h - 1);
    const float wgt = ((k & 1) ? fx : 1.f - fx) * ((k >> 1) ? fy : 1.f - fy);
    const uint8_t* p = &t->px[(static_cast<size_t>(yi) * t->w + xi) * 4];
    for (int c = 0; c < 4; c++) {
      acc[c] += p[c] / 255.f * wgt;
    }
  }
  std::memcpy(out, acc, sizeof(acc));
}

void render(const char* name) {
  ImDrawData* dd = ImGui::GetDrawData();
  if (dd == nullptr || !dd->Valid) {
    return;
  }
  const int W = static_cast<int>(dd->DisplaySize.x), H = static_cast<int>(dd->DisplaySize.y);
  if (W <= 0 || H <= 0) {
    return;
  }
  std::vector<float> img(static_cast<size_t>(W) * H * 3, 0.f);
  // the font atlas, as its backend uploaded it
  Tex font;
  {
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    if (px != nullptr) {
      font.w = w;
      font.h = h;
      font.px.assign(px, px + static_cast<size_t>(w) * h * 4);
    }
  }
  const ImTextureID fontId = ImGui::GetIO().Fonts->TexID;
  for (int l = 0; l < dd->CmdListsCount; l++) {
    const ImDrawList* list = dd->CmdLists[l];
    for (const ImDrawCmd& cmd : list->CmdBuffer) {
      if (cmd.UserCallback != nullptr) {
        continue;
      }
      const Tex* tex = nullptr;
      const ImTextureID id = cmd.GetTexID();
      if (id == fontId) {
        tex = &font;
      } else if (auto it = textures().find(id); it != textures().end()) {
        tex = &it->second;
      }
      const float cx0 = std::max(0.f, cmd.ClipRect.x - dd->DisplayPos.x);
      const float cy0 = std::max(0.f, cmd.ClipRect.y - dd->DisplayPos.y);
      const float cx1 = std::min(static_cast<float>(W), cmd.ClipRect.z - dd->DisplayPos.x);
      const float cy1 = std::min(static_cast<float>(H), cmd.ClipRect.w - dd->DisplayPos.y);
      for (unsigned int e = 0; e + 2 < cmd.ElemCount; e += 3) {
        const ImDrawVert* v[3];
        for (int k = 0; k < 3; k++) {
          v[k] = &list->VtxBuffer[cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + e + k]];
        }
        ImVec2 p[3];
        for (int k = 0; k < 3; k++) {
          p[k] = ImVec2(v[k]->pos.x - dd->DisplayPos.x, v[k]->pos.y - dd->DisplayPos.y);
        }
        float area = edge(p[0], p[1], p[2].x, p[2].y);
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
            float w0 = edge(p[1], p[2], px, py) / area;
            float w1 = edge(p[2], p[0], px, py) / area;
            float w2 = 1.f - w0 - w1;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f) {
              continue;
            }
            float t[4];
            sample(tex, w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x,
                   w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y, t);
            float c[4];
            for (int k = 0; k < 4; k++) {
              c[k] = (w0 * col[0][k] + w1 * col[1][k] + w2 * col[2][k]) * t[k];
            }
            float* o = &img[(static_cast<size_t>(y) * W + x) * 3];
            for (int k = 0; k < 3; k++) {
              o[k] = o[k] * (1.f - c[3]) + c[k] * c[3];
            }
          }
        }
      }
    }
  }
  SDL_Surface* s = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGB24);
  if (s == nullptr) {
    return;
  }
  for (int y = 0; y < H; y++) {
    uint8_t* row = static_cast<uint8_t*>(s->pixels) + static_cast<size_t>(y) * s->pitch;
    for (int x = 0; x < W * 3; x++) {
      row[x] = static_cast<uint8_t>(std::clamp(img[static_cast<size_t>(y) * W * 3 + x], 0.f, 1.f) * 255.f + 0.5f);
    }
  }
  const std::string path = sDir + "/" + name + ".png";
  if (!SDL_SavePNG(s, path.c_str())) {
    std::fprintf(stderr, "launcher shot: %s: %s\n", path.c_str(), SDL_GetError());
  } else {
    std::fprintf(stderr, "launcher shot: %s\n", path.c_str());
  }
  SDL_DestroySurface(s);
}

} // namespace

void launcherShotTexture(ImTextureID id, int w, int h, const void* rgba) {
  parse();
  if (sShots.empty()) {
    return;
  }
  Tex& t = textures()[id];
  t.w = w;
  t.h = h;
  t.px.assign(static_cast<const uint8_t*>(rgba), static_cast<const uint8_t*>(rgba) + static_cast<size_t>(w) * h * 4);
}

bool launcherShotActive() {
  parse();
  return !sShots.empty();
}

void launcherShotFrame(float time) {
  parse();
  for (Shot& s : sShots) {
    if (!s.done && time >= s.at) {
      s.done = true;
      render(s.name.c_str());
    }
  }
}
