// The launcher's test captures ($TTYD_LAUNCHER_SHOT, launcher_shot.h): the frame's Dear ImGui draw
// data drawn again on the CPU into a PNG. Aurora's picture capture leaves ImGui out, and a window's
// own content can't be read from outside without focusing it, so the launcher's look is checked this
// way: triangles with their texture and colour, each pixel's centre sampled, blended over.
//
// In the game, the same over the picture as shown ($TTYD_SHOT_UI=1, testpass's "ui_shots"): each
// named capture (portShotRequest: an input script's "shot", the menu crawl's) also writes
// <name>_ui.png, the game's picture with the settings page, the map editor's panel and the other
// Dear ImGui windows over it, so they can be checked offscreen.
#include "launcher_shot.h"
#include "port/ui_shot.h"
#include "port/layout.h"

#include <aurora/picture.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
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

// alpha: the layer alone, with its coverage (RGBA; the online wait's notice, composed over the
// picture by the test), else over black (RGB), or over under (RGBA8, uw x uh: the game's picture as
// shown, in the middle of the window as it is presented, scaled to fit when larger)
// dd: the frame's draw data (ImGui's own, or a copy kept from an earlier frame)
// place: where the picture is in the window (x, y, w, h in points; held upright it is at the top, above
// the on-screen controls, port/include/port/layout.h), or nullptr for the middle
void render(const std::string& path, bool alpha = false, const uint8_t* under = nullptr, int uw = 0, int uh = 0,
            ImDrawData* dd = ImGui::GetDrawData(), const float* place = nullptr) {
  if (dd == nullptr || !dd->Valid) {
    return;
  }
  const int W = static_cast<int>(dd->DisplaySize.x), H = static_cast<int>(dd->DisplaySize.y);
  if (W <= 0 || H <= 0) {
    return;
  }
  std::vector<float> img(static_cast<size_t>(W) * H * 3, 0.f), cover(alpha ? static_cast<size_t>(W) * H : 0, 0.f);
  if (under != nullptr && uw > 0 && uh > 0) {
    float kx = std::min({1.f, static_cast<float>(W) / uw, static_cast<float>(H) / uh});
    float ky = kx;
    int pw = static_cast<int>(uw * kx), ph = static_cast<int>(uh * ky);
    int ox = (W - pw) / 2, oy = (H - ph) / 2;
    if (place != nullptr && place[2] >= 1.f && place[3] >= 1.f) {
      ox = std::clamp(static_cast<int>(place[0]), 0, W - 1);
      oy = std::clamp(static_cast<int>(place[1]), 0, H - 1);
      pw = std::min(static_cast<int>(place[2]), W - ox);
      ph = std::min(static_cast<int>(place[3]), H - oy);
      kx = place[2] / static_cast<float>(uw);
      ky = place[3] / static_cast<float>(uh);
    }
    for (int y = 0; y < ph; y++) {
      for (int x = 0; x < pw; x++) {
        const int sx = std::min(uw - 1, static_cast<int>(x / kx)), sy = std::min(uh - 1, static_cast<int>(y / ky));
        const uint8_t* q = &under[(static_cast<size_t>(sy) * uw + sx) * 4];
        float* o = &img[(static_cast<size_t>(y + oy) * W + x + ox) * 3];
        o[0] = q[0] / 255.f;
        o[1] = q[1] / 255.f;
        o[2] = q[2] / 255.f;
      }
    }
  }
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
            if (alpha) {
              float& a = cover[static_cast<size_t>(y) * W + x];
              a = a * (1.f - c[3]) + c[3];
            }
          }
        }
      }
    }
  }
  SDL_Surface* s = SDL_CreateSurface(W, H, alpha ? SDL_PIXELFORMAT_RGBA32 : SDL_PIXELFORMAT_RGB24);
  if (s == nullptr) {
    return;
  }
  for (int y = 0; y < H; y++) {
    uint8_t* row = static_cast<uint8_t*>(s->pixels) + static_cast<size_t>(y) * s->pitch;
    for (int x = 0; x < W; x++) {
      const float a = alpha ? cover[static_cast<size_t>(y) * W + x] : 1.f;
      const int n = alpha ? 4 : 3;
      for (int k = 0; k < 3; k++) { // (the layer's colour: over black, so divided by its coverage)
        const float v = img[(static_cast<size_t>(y) * W + x) * 3 + k] / (a > 0.f ? a : 1.f);
        row[x * n + k] = static_cast<uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
      }
      if (alpha) {
        row[x * n + 3] = static_cast<uint8_t>(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
      }
    }
  }
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
  if (sShots.empty() && !portUiShotOn()) {
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

void launcherShotNamed(const char* name) {
  parse();
  if (!sShots.empty()) {
    sShots.push_back({-1.f, name});
  }
}

void launcherShotFrame(float time) {
  parse();
  bool all = !sShots.empty();
  for (Shot& s : sShots) {
    if (!s.done && time >= s.at) {
      s.done = true;
      render(sDir + "/" + s.name + ".png");
    }
    all = all && (s.done || s.at < 0.f); // (a named one, asked for by a test that drives the launcher: not awaited)
  }
  // $TTYD_LAUNCHER_SHOT_EXIT (tests: a launcher that a program started, Back to the launcher's): it ends once its
  // captures are made, as nothing else would stop it (port/include/port/launcher_back.h)
  static const bool sExit = SDL_getenv("TTYD_LAUNCHER_SHOT_EXIT") != nullptr;
  if (sExit && all) {
    std::printf("[launcher] the captures made: TTYD_LAUNCHER_SHOT_EXIT ends it\n");
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(0);
  }
}

// --- In the game: <name>_ui.png, the Dear ImGui layer over the picture as shown ---

namespace {

std::deque<std::string> sUiWanted; // asked for, not yet under way
std::string sUiName;               // the one whose picture is on its way ("": none)
// the frame's draw data as it was when asked for (the page may have moved on by the picture's arrival)
ImDrawData sUiDraw;
std::vector<ImDrawList*> sUiLists;

void uiKeepDraw() {
  for (ImDrawList* l : sUiLists) {
    IM_DELETE(l);
  }
  sUiLists.clear();
  sUiDraw.Clear();
  ImDrawData* dd = ImGui::GetDrawData();
  if (dd == nullptr || !dd->Valid) {
    return;
  }
  for (int i = 0; i < dd->CmdListsCount; i++) {
    sUiLists.push_back(dd->CmdLists[i]->CloneOutput());
  }
  sUiDraw.Valid = true;
  for (ImDrawList* l : sUiLists) {
    sUiDraw.CmdLists.push_back(l);
  }
  sUiDraw.CmdListsCount = static_cast<int>(sUiLists.size());
  sUiDraw.DisplayPos = dd->DisplayPos;
  sUiDraw.DisplaySize = dd->DisplaySize;
  sUiDraw.FramebufferScale = dd->FramebufferScale;
}
std::mutex sUiMutex;               // the picture's arrival (the GPU's callback thread)
std::vector<uint8_t> sUiPicture;
int sUiW = 0, sUiH = 0, sUiState = 0; // 1 asked for, 2 here, 3 none (over black)
int sUiWaited = 0;
uintptr_t sUiAsk = 0; // the capture asked for now: a late picture from one given up on is dropped

void uiPicture(void* ask, const unsigned char* rgba, unsigned int w, unsigned int h) {
  std::lock_guard lock{sUiMutex};
  if (reinterpret_cast<uintptr_t>(ask) != sUiAsk || sUiState != 1) {
    return; // (an earlier capture's, after its wait ran out)
  }
  if (rgba != nullptr && w > 0 && h > 0) {
    sUiPicture.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
    sUiW = static_cast<int>(w);
    sUiH = static_cast<int>(h);
    sUiState = 2;
  } else {
    sUiState = 3;
  }
}

} // namespace

extern "C" int portUiShotOn(void) {
  static const int on = [] {
    const char* v = SDL_getenv("TTYD_SHOT_UI");
    return v != nullptr && v[0] != '\0' && std::strcmp(v, "0") != 0 ? 1 : 0;
  }();
  return on;
}

extern "C" void portUiShotRequest(const char* name) {
  if (portUiShotOn() && name != nullptr && name[0] != '\0') {
    sUiWanted.push_back(name);
  }
}

extern "C" int portUiShotPending(void) {
  // (without ImGui none is ever made: nothing to wait for)
  return portUiShotOn() && ImGui::GetCurrentContext() != nullptr && (!sUiName.empty() || !sUiWanted.empty()) ? 1
                                                                                                             : 0;
}

extern "C" void portUiShotFrame(void) {
  if (!portUiShotOn() || ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  if (sUiName.empty()) {
    if (sUiWanted.empty()) {
      return;
    }
    // the picture of the next frame shown (the page holds still between two)
    uintptr_t ask = 0;
    {
      std::lock_guard lock{sUiMutex};
      ask = ++sUiAsk; // (asked for before the request: its answer can't come first)
      sUiPicture.clear();
      sUiState = 1;
      sUiWaited = 0;
    }
    if (!aurora_capture_picture(uiPicture, reinterpret_cast<void*>(ask))) {
      std::lock_guard lock{sUiMutex};
      sUiState = 0;
      return; // (another capture waiting: the next frame)
    }
    uiKeepDraw(); // (this frame's: drawn, ImGui::Render done)
    sUiName = sUiWanted.front();
    sUiWanted.pop_front();
    return;
  }
  std::vector<uint8_t> picture;
  int w = 0, h = 0;
  bool none = false;
  {
    std::lock_guard lock{sUiMutex};
    if (sUiState == 1 && ++sUiWaited < 120) {
      return;
    }
    none = sUiState != 2;
    picture.swap(sUiPicture);
    w = sUiW;
    h = sUiH;
    sUiState = 0;
    sUiAsk++; // (a picture still on its way for this one is dropped)
  }
  if (none) {
    // (tests: told apart from a good capture; the page is still drawn, over black)
    std::fprintf(stderr, "[uishot] no picture for %s (%s): drawn over black\n", sUiName.c_str(),
                 sUiWaited >= 120 ? "it never came" : "the capture failed");
  }
  parse(); // (the folder)
  const char* dir = SDL_getenv("TTYD_SHOT_DIR");
  // held upright the picture is at the top, not in the middle (the on-screen controls below it)
  PortLayout pl{};
  const bool upright = portLayoutGet(&pl) != 0 && pl.portrait != 0 && pl.picW > 0.f && pl.picH > 0.f;
  const float place[4] = {pl.picX, pl.picY, pl.picW, pl.picH};
  render(std::string(dir != nullptr ? dir : ".") + "/" + sUiName + "_ui.png", false,
         picture.empty() ? nullptr : picture.data(), w, h, &sUiDraw, upright ? place : nullptr);
  sUiName.clear();
}
