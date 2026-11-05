// The launcher's stage (launcher_stage.h): a paper theatre, every piece a layer of cut paper with its
// own shade, a light cut edge and a soft shadow on what is behind it, warm and red and gold like the
// Paper Cut wordmark. Drawn from shapes into Dear ImGui's background list each frame (some ten
// thousand vertices: cheap next to the pages on it, also on phones); no game assets.
//
// Back to front: the night-sky flat (a painted gradient, a warm horizon, twinkling stars), the
// spotlight's beam, two rows of cut-out clouds, the stage boards in perspective with the spotlight's
// pool and footlights along the front edge, a paper moon and stars hanging on strings, the curtains
// (pleats as separate strips, gathered and tied back with gold ropes and tassels, their inner edges
// catching the stage light, swaying gently) and the valance (layered swags with a gold cord and
// fringe, rosettes and tassels between them, on a pleated pelmet with a gold moulding).
#include "launcher_stage.h"
#include "launcher_popup.h"
#include "launcher_style.h"

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr float kPi = 3.14159265f;

// ---- small helpers ----

// a repeatable pseudo-random number in [0, 1) for item i, part k
float hash(int i, int k) {
  uint32_t x = static_cast<uint32_t>(i) * 747796405u + static_cast<uint32_t>(k) * 2891336453u + 12345u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}
float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
float mix(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float e0, float e1, float x) {
  const float t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3.f - 2.f * t);
}
float easeOut(float t) { return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
// a pop-up page's spring: past upright and back
float popUp(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.f, u = t - 1.f;
  return t <= 0.f ? 0.f : t >= 1.f ? 1.f : 1.f + c3 * u * u * u + c1 * u * u;
}

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
int chan(ImU32 c, int shift) { return static_cast<int>((c >> shift) & 0xFF); }
// a colour's alpha times a
ImU32 alpha(ImU32 c, float a) {
  const int al = static_cast<int>(chan(c, IM_COL32_A_SHIFT) * clamp01(a) + 0.5f);
  return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al) << IM_COL32_A_SHIFT);
}
// ... times the fade of what is being drawn (the tie-backs as the pop-up's curtains reach them)
float sFade = 1.f;
ImU32 faded(ImU32 c) { return alpha(c, sFade); }
ImU32 lerpCol(ImU32 a, ImU32 b, float t) {
  t = clamp01(t);
  auto ch = [&](int s) { return static_cast<int>(mix(static_cast<float>(chan(a, s)), static_cast<float>(chan(b, s)), t) + 0.5f); };
  return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), ch(IM_COL32_A_SHIFT));
}
ImU32 shade(ImU32 c, float k) {
  auto ch = [&](int s) { return std::min(255, static_cast<int>(chan(c, s) * k + 0.5f)); };
  return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), chan(c, IM_COL32_A_SHIFT));
}

// one vertex (a position and a colour) on the draw list's white texel
void vtx(ImDrawList* dl, ImVec2 p, ImU32 c) { dl->PrimWriteVtx(p, dl->_Data->TexUvWhitePixel, faded(c)); }

// a quad with a colour at each corner (a, b, c, d in order round it)
void quad(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd) {
  dl->PrimReserve(6, 4);
  const ImDrawIdx i = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
  dl->PrimWriteIdx(i);
  dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 1));
  dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 2));
  dl->PrimWriteIdx(i);
  dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 2));
  dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 3));
  vtx(dl, a, ca);
  vtx(dl, b, cb);
  vtx(dl, c, cc);
  vtx(dl, d, cd);
}

// a mesh of rows x cols vertices (row by row), triangles between them
void grid(ImDrawList* dl, const ImVec2* p, const ImU32* c, int rows, int cols) {
  dl->PrimReserve((rows - 1) * (cols - 1) * 6, rows * cols);
  const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
  for (int r = 0; r + 1 < rows; r++) {
    for (int k = 0; k + 1 < cols; k++) {
      const ImDrawIdx a = static_cast<ImDrawIdx>(base + r * cols + k);
      const ImDrawIdx b = static_cast<ImDrawIdx>(a + 1), d = static_cast<ImDrawIdx>(a + cols),
                      e = static_cast<ImDrawIdx>(a + cols + 1);
      dl->PrimWriteIdx(a);
      dl->PrimWriteIdx(b);
      dl->PrimWriteIdx(e);
      dl->PrimWriteIdx(a);
      dl->PrimWriteIdx(e);
      dl->PrimWriteIdx(d);
    }
  }
  for (int i = 0; i < rows * cols; i++) {
    vtx(dl, p[i], c[i]);
  }
}

// a soft glow: an ellipse from colour in at its centre to out at its rim
void glow(ImDrawList* dl, ImVec2 c, float rx, float ry, ImU32 in, ImU32 out, int segs = 40) {
  dl->PrimReserve(segs * 3, segs + 1);
  const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
  for (int i = 0; i < segs; i++) {
    dl->PrimWriteIdx(base);
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % segs));
  }
  vtx(dl, c, in);
  for (int i = 0; i < segs; i++) {
    const float a = i * 2.f * kPi / segs;
    vtx(dl, ImVec2(c.x + std::cos(a) * rx, c.y + std::sin(a) * ry), out);
  }
}

// a vertical gradient over a rectangle (top colour, bottom colour)
void vgrad(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom) {
  quad(dl, a, ImVec2(b.x, a.y), b, ImVec2(a.x, b.y), top, top, bottom, bottom);
}

// the vertices drawn since mark scaled vertically about y = base by k (a pop-up layer unfolding)
void fold(ImDrawList* dl, int mark, float base, float k) {
  if (k == 1.f) {
    return;
  }
  for (int i = mark; i < dl->VtxBuffer.Size; i++) {
    ImDrawVert& v = dl->VtxBuffer[i];
    v.pos.y = base + (v.pos.y - base) * k;
  }
}
// ... turned by angle about c
void turn(ImDrawList* dl, int mark, ImVec2 c, float angle) {
  const float cs = std::cos(angle), sn = std::sin(angle);
  for (int i = mark; i < dl->VtxBuffer.Size; i++) {
    ImDrawVert& v = dl->VtxBuffer[i];
    const float x = v.pos.x - c.x, y = v.pos.y - c.y;
    v.pos = ImVec2(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
  }
}

// ---- the stage's measures ----

struct Stage {
  float W, H, s;    // display size, UI scale
  float side;       // the curtains' width at rest (launcher_popup.cpp's too)
  float floorTop;   // where the boards meet the backdrop (launcher_popup.cpp's too)
  float front;      // the boards' front edge (the lip below it)
  float tieY;       // the curtains' tie-backs
  float bandH;      // the pelmet's height
  float drop;       // the swags' sag
  float fringe;     // the fringe's length
  float time;       // seconds, for the sway (frozen when still)
  bool compact;     // a short stage (the wordmark beside the tabs)
};

Stage measure(float time, float scale) {
  Stage st;
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  st.W = size.x;
  st.H = size.y;
  st.s = scale;
  st.side = std::fmax(36.f * scale, size.x * 0.075f);
  st.floorTop = size.y * 0.86f;
  st.front = size.y - (size.y - st.floorTop) * 0.2f;
  st.tieY = size.y * 0.6f;
  st.bandH = std::fmax(22.f * scale, size.y * 0.04f);
  st.drop = std::fmax(24.f * scale, size.y * 0.045f);
  st.fringe = 10.f * scale;
  st.time = launcherStageStill() ? 0.f : time;
  st.compact = false;
  return st;
}

// ---- the backdrop: a painted night sky ----

void sky(ImDrawList* dl, const Stage& st, float twinkle) {
  const float W = st.W, H = st.H, s = st.s;
  // the flat: deep indigo at the top to a plum at the horizon, painted in a few soft strokes
  vgrad(dl, ImVec2(0, 0), ImVec2(W, st.floorTop * 0.55f), rgb(16, 16, 52), rgb(34, 28, 86));
  vgrad(dl, ImVec2(0, st.floorTop * 0.55f), ImVec2(W, st.floorTop + 2.f), rgb(34, 28, 86), rgb(86, 48, 112));
  for (int i = 0; i < 5; i++) {
    const float y = st.floorTop * (0.12f + 0.16f * i + 0.05f * hash(i, 40));
    const float h = st.floorTop * (0.05f + 0.04f * hash(i, 41));
    const ImU32 c = i % 2 == 0 ? rgb(150, 140, 255, 10) : rgb(10, 0, 30, 18);
    vgrad(dl, ImVec2(0, y - h), ImVec2(W, y), alpha(c, 0.f), c);
    vgrad(dl, ImVec2(0, y), ImVec2(W, y + h), c, alpha(c, 0.f));
  }
  // a warm glow low on the flat, as if the stage lights reached it
  vgrad(dl, ImVec2(0, st.floorTop - H * 0.3f), ImVec2(W, st.floorTop + 2.f), rgb(236, 120, 110, 0),
        rgb(244, 140, 104, 60));
  glow(dl, ImVec2(W * 0.5f, st.floorTop * 0.42f), W * 0.42f, st.floorTop * 0.42f, rgb(120, 90, 200, 34),
       rgb(120, 90, 200, 0));
  // stars painted on it, twinkling
  for (int i = 0; i < 70; i++) {
    const ImVec2 p(hash(i, 0) * W, st.bandH + hash(i, 1) * (st.floorTop * 0.78f - st.bandH));
    const float tw = 0.6f + 0.4f * std::sin(twinkle * (0.7f + hash(i, 2) * 1.4f) + hash(i, 3) * 6.283f);
    const float r = (0.8f + hash(i, 4) * 1.5f) * s;
    glow(dl, p, r * 3.2f, r * 3.2f, rgb(255, 236, 190, static_cast<int>(40 * tw)), rgb(255, 236, 190, 0), 12);
    dl->AddCircleFilled(p, r, faded(rgb(255, 246, 214, static_cast<int>(120 + 120 * tw))), 10);
  }
}

// the spotlight's beam from above the valance, a faint warm cone down to the boards
void beam(ImDrawList* dl, const Stage& st, float light) {
  const float cx = st.W * 0.5f;
  const float topW = st.W * 0.07f, botW = st.W * 0.34f;
  const ImVec2 p[6] = {{cx - topW, 0}, {cx, 0}, {cx + topW, 0},
                       {cx - botW, st.floorTop}, {cx, st.floorTop}, {cx + botW, st.floorTop}};
  const ImU32 mid0 = rgb(255, 236, 196, static_cast<int>(26 * light)), mid1 = rgb(255, 236, 196, static_cast<int>(12 * light));
  const ImU32 c[6] = {alpha(mid0, 0.f), mid0, alpha(mid0, 0.f), alpha(mid1, 0.f), mid1, alpha(mid1, 0.f)};
  grid(dl, p, c, 2, 3);
}

// one cloud of puffs: a soft shadow, a light paper edge up and to the left, the face
struct Puff {
  ImVec2 c;
  float r;
};
void cloud(ImDrawList* dl, const Puff* puffs, int n, float baseY, float bottomY, float x0, float x1, ImU32 face,
           ImU32 rim, float s) {
  const ImU32 shadow = rgb(14, 6, 36, 80);
  for (int i = 0; i < n; i++) {
    dl->AddCircleFilled(ImVec2(puffs[i].c.x + 3.f * s, puffs[i].c.y + 4.f * s), puffs[i].r, faded(shadow), 36);
  }
  for (int i = 0; i < n; i++) {
    dl->AddCircleFilled(ImVec2(puffs[i].c.x - 1.2f * s, puffs[i].c.y - 1.8f * s), puffs[i].r, faded(rim), 36);
  }
  dl->AddRectFilled(ImVec2(x0, baseY), ImVec2(x1, bottomY), faded(face));
  for (int i = 0; i < n; i++) {
    dl->AddCircleFilled(puffs[i].c, puffs[i].r, faded(face), 36);
  }
  // a little shade low on each puff, where it turns under
  for (int i = 0; i < n; i++) {
    const ImVec2 c = puffs[i].c;
    const float r = puffs[i].r;
    glow(dl, ImVec2(c.x + r * 0.15f, c.y + r * 0.55f), r * 0.85f, r * 0.45f, rgb(20, 8, 50, 40), rgb(20, 8, 50, 0), 20);
  }
}

void clouds(ImDrawList* dl, const Stage& st, bool frontRow) {
  const float W = st.W, H = st.H, s = st.s;
  Puff puffs[48];
  int n = 0;
  if (!frontRow) {
    // a long bank along the horizon
    const float base = st.floorTop + 4.f * s;
    float x = -40.f * s;
    for (int i = 0; x < W + 60.f * s && n < 48; i++) {
      const float r = std::clamp((0.035f + 0.04f * hash(i, 20)) * H, 18.f * s, 80.f * s);
      puffs[n++] = {ImVec2(x, base - r * 0.3f - hash(i, 21) * 0.03f * H), r};
      x += r * (1.0f + 0.5f * hash(i, 22));
    }
    cloud(dl, puffs, n, base - 0.02f * H, st.floorTop + 8.f * s, -60.f * s, W + 60.f * s, rgb(70, 50, 116),
          rgb(124, 98, 170), s);
  } else {
    // nearer, smaller ones at each side
    for (int side = 0; side < 2; side++) {
      const float base = st.floorTop + 6.f * s;
      float x = side == 0 ? st.side * 0.6f : W - st.side * 0.6f;
      const float dir = side == 0 ? 1.f : -1.f;
      const int first = n;
      for (int i = 0; i < 6 && n < 48; i++) {
        const int k = i + side * 10;
        const float r = std::clamp((0.022f + 0.022f * hash(k, 30)) * H * (1.f - i * 0.1f), 12.f * s, 50.f * s);
        puffs[n++] = {ImVec2(x, base - r * 0.15f - hash(k, 31) * 0.012f * H), r};
        x += dir * r * (0.9f + 0.4f * hash(k, 32));
      }
      const float a = std::fmin(puffs[first].c.x, x), b = std::fmax(puffs[first].c.x, x);
      cloud(dl, puffs + first, n - first, base - 0.008f * H, st.floorTop + 10.f * s, a, b, rgb(98, 72, 142),
            rgb(164, 138, 200), s);
    }
  }
}

// ---- the boards ----

void boards(ImDrawList* dl, const Stage& st, float light) {
  const float W = st.W, H = st.H, s = st.s;
  const float ft = st.floorTop, yf = st.front, floorH = H - ft;
  const float cx = W * 0.5f, vpY = ft - floorH * 3.2f;
  const float k = (ft - vpY) / (yf - vpY); // the back edge's width against the front's
  const float bw = 72.f * s;
  const int m = static_cast<int>(std::ceil(cx / (bw * k))) + 1;
  const ImU32 wood = rgb(186, 120, 64);
  auto backX = [&](float xf) { return cx + (xf - cx) * k; };
  for (int i = -m; i < m; i++) {
    const float xf0 = cx + i * bw, xf1 = xf0 + bw;
    const ImU32 c = shade(wood, 0.88f + 0.2f * hash(i + 100, 50));
    const ImU32 back = lerpCol(shade(c, 0.5f), rgb(70, 36, 50), 0.25f), front = shade(c, 1.02f);
    quad(dl, ImVec2(backX(xf0), ft), ImVec2(backX(xf1), ft), ImVec2(xf1, yf), ImVec2(xf0, yf), back, back, front,
         front);
    // a joint where two planks meet, somewhere along it, and a line of grain
    const float d = 0.25f + 0.6f * hash(i + 100, 51);
    const float y = ft + (yf - ft) * d * d;
    const float t = (y - ft) / (yf - ft);
    const float a = mix(backX(xf0), xf0, t), b = mix(backX(xf1), xf1, t);
    dl->AddLine(ImVec2(a, y), ImVec2(b, y), faded(rgb(64, 32, 16, 150)), std::fmax(1.f, 1.1f * s * (0.5f + t)));
    const float g = 0.3f + 0.4f * hash(i + 100, 52);
    dl->AddLine(ImVec2(mix(backX(xf0), backX(xf1), g), ft), ImVec2(mix(xf0, xf1, g), yf), faded(rgb(255, 214, 160, 22)),
                std::fmax(1.f, 1.f * s));
  }
  for (int i = -m; i <= m; i++) {
    const float xf = cx + i * bw;
    dl->AddLine(ImVec2(backX(xf), ft), ImVec2(xf, yf), faded(rgb(62, 30, 14, 210)), std::fmax(1.f, 1.4f * s));
  }
  // the backdrop's foot in shadow, the spotlight's pool, the footlights' warmth
  vgrad(dl, ImVec2(0, ft), ImVec2(W, ft + floorH * 0.22f), rgb(16, 6, 24, 140), rgb(16, 6, 24, 0));
  glow(dl, ImVec2(cx, ft + floorH * 0.42f), W * 0.36f, floorH * 0.62f, rgb(255, 226, 168, static_cast<int>(70 * light)),
       rgb(255, 226, 168, 0), 48);
  glow(dl, ImVec2(cx, ft + floorH * 0.45f), W * 0.2f, floorH * 0.4f, rgb(255, 240, 200, static_cast<int>(60 * light)),
       rgb(255, 240, 200, 0), 40);
  vgrad(dl, ImVec2(0, yf - floorH * 0.5f), ImVec2(W, yf), rgb(255, 190, 110, 0),
        rgb(255, 190, 110, static_cast<int>(56 * light)));
  const int lamps = std::max(4, static_cast<int>(std::lround((W - 2.f * st.side) / (130.f * s))));
  const float span = W - 2.f * st.side;
  for (int i = 0; i < lamps; i++) {
    const float x = st.side + (i + 0.5f) * span / lamps;
    glow(dl, ImVec2(x, yf), 74.f * s, floorH * 0.62f, rgb(255, 212, 140, static_cast<int>(120 * light)),
         rgb(255, 212, 140, 0), 32);
  }
  // the front edge: the lip's face, its nosing catching the light
  vgrad(dl, ImVec2(0, yf), ImVec2(W, H + 1.f), rgb(108, 60, 30), rgb(48, 24, 14));
  dl->AddLine(ImVec2(0, yf + 1.f * s), ImVec2(W, yf + 1.f * s), faded(rgb(246, 196, 130)), 2.2f * s);
  dl->AddLine(ImVec2(0, yf + 3.2f * s), ImVec2(W, yf + 3.2f * s), faded(rgb(40, 18, 8, 200)), 1.f * s);
  // the footlights: brass shells along the edge, a bulb glowing in each
  for (int i = 0; i < lamps; i++) {
    const float x = st.side + (i + 0.5f) * span / lamps;
    const float hw = 15.f * s, hd = std::fmin(12.f * s, (H - yf) * 0.9f);
    const ImVec2 bulb(x, yf - 2.f * s);
    glow(dl, bulb, 26.f * s, 16.f * s, rgb(255, 226, 160, static_cast<int>(170 * light)), rgb(255, 226, 160, 0), 24);
    dl->AddEllipseFilled(bulb, ImVec2(7.f * s, 4.5f * s),
                         faded(lerpCol(rgb(150, 128, 104), rgb(255, 248, 220), light)), 0.f, 16);
    ImVec2 shell[14];
    for (int k = 0; k < 14; k++) {
      const float a = kPi * k / 13.f;
      shell[k] = ImVec2(x - std::cos(a) * hw, yf + std::sin(a) * hd);
    }
    dl->AddConvexPolyFilled(shell, 14, faded(rgb(136, 92, 34)));
    for (int k = 0; k < 14; k++) {
      shell[k] = ImVec2(x + (shell[k].x - x) * 0.72f, yf + (shell[k].y - yf) * 0.62f);
    }
    dl->AddConvexPolyFilled(shell, 14, faded(rgb(214, 162, 70)));
    dl->AddLine(ImVec2(x - hw, yf), ImVec2(x + hw, yf), faded(rgb(255, 230, 160)), 1.6f * s);
  }
}

// ---- the paper moon and stars on strings ----

// a five-pointed star, its points r out, turned by a
void star(ImDrawList* dl, ImVec2 c, float r, float a, ImU32 face, ImU32 edge) {
  ImVec2 pts[10];
  for (int i = 0; i < 10; i++) {
    const float ang = a - kPi * 0.5f + i * kPi / 5.f;
    const float rr = i % 2 == 0 ? r : r * 0.46f;
    pts[i] = ImVec2(c.x + std::cos(ang) * rr, c.y + std::sin(ang) * rr);
  }
  // a fan from the centre, lighter there, with the points' edges smoothed by a line round them
  dl->PrimReserve(30, 11);
  const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
  for (int i = 0; i < 10; i++) {
    dl->PrimWriteIdx(base);
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % 10));
  }
  vtx(dl, c, lerpCol(face, rgb(255, 250, 220), 0.55f));
  for (int i = 0; i < 10; i++) {
    vtx(dl, pts[i], face);
  }
  dl->AddPolyline(pts, 10, faded(edge), ImDrawFlags_Closed, std::fmax(1.f, r * 0.09f));
}

// the crescent moon, R across, its horns to the left, hung at its top horn
void moon(ImDrawList* dl, ImVec2 c, float R, ImU32 face, ImU32 edge) {
  // between the outer circle and an inner one offset to the left, from horn to horn
  const int n = 28;
  const float off = R * 0.42f, r2 = R * 0.86f;
  // the horns, where the circles cross: x from the outer centre (negative: to the left)
  const float x = (r2 * r2 - R * R - off * off) / (2.f * off);
  const float hy = std::sqrt(std::fmax(0.f, R * R - x * x));
  const float a0 = std::atan2(-hy, x), a1 = std::atan2(hy, x);             // on the outer circle
  const float b0 = std::atan2(-hy, x + off), b1 = std::atan2(hy, x + off); // on the inner
  ImVec2 p[n * 2];
  ImU32 col[n * 2];
  for (int i = 0; i < n; i++) {
    // both arcs from the top horn round the right to the bottom horn
    const float t = static_cast<float>(i) / (n - 1);
    const float ao = mix(a0, a1, t), ai = mix(b0, b1, t);
    p[i * 2] = ImVec2(c.x + std::cos(ao) * R, c.y + std::sin(ao) * R);
    p[i * 2 + 1] = ImVec2(c.x - off + std::cos(ai) * r2, c.y + std::sin(ai) * r2);
    col[i * 2] = lerpCol(face, rgb(255, 252, 230), 0.45f);
    col[i * 2 + 1] = face;
  }
  grid(dl, p, col, n, 2);
  ImVec2 rim[n * 2];
  for (int i = 0; i < n; i++) {
    rim[i] = p[i * 2];
    rim[n * 2 - 1 - i] = p[i * 2 + 1];
  }
  dl->AddPolyline(rim, n * 2, faded(edge), ImDrawFlags_Closed, std::fmax(1.f, R * 0.06f));
}

void hanging(ImDrawList* dl, const Stage& st, ImVec2 min, ImVec2 max) {
  const float s = st.s, t = st.time;
  const float stageW = max.x - min.x;
  const float skyH = st.floorTop - min.y;
  // a short stage (the wordmark beside the tabs) or a narrow one (the title wide across it): they hang
  // just below the valance, clear of the tabs
  const float lenK = st.compact ? 0.2f : stageW < 640.f * s ? 0.3f : 1.f;
  const float m = std::fmin(st.W, st.H);
  const ImU32 string = rgb(232, 210, 170, 150);
  const ImU32 shadow = rgb(8, 2, 24, 90);
  // each: where it hangs (of the stage's width; < 0 counts from the right), how far down (of the sky),
  // its size (of the screen's short side)
  struct Hang {
    float fx, fy, r;
    bool isMoon;
  };
  const Hang items[] = {{0.035f, 0.07f, 0.019f, false}, {0.11f, 0.17f, 0.024f, false}, {0.19f, 0.035f, 0.013f, false},
                        {-0.13f, 0.045f, 0.015f, false}, {-0.03f, 0.1f, 0.034f, true}};
  for (int i = 0; i < 5; i++) {
    const Hang& h = items[i];
    const float r = h.r * m * (st.compact ? 0.8f : 1.f);
    const float x = h.fx >= 0.f ? min.x + r + h.fx * stageW : max.x - r + h.fx * stageW;
    const float y = min.y + r + h.fy * skyH * lenK;
    const float top = st.bandH * 0.5f;
    // a slow swing on its string
    const float a = 0.05f * std::sin(t * (0.8f + 0.25f * hash(i, 60)) + hash(i, 61) * 6.283f);
    const ImVec2 pivot(x, top);
    const int mark = dl->VtxBuffer.Size;
    const ImVec2 hook(x, y - r * (h.isMoon ? 0.92f : 0.98f));
    dl->AddLine(pivot, hook, faded(string), std::fmax(1.f, 1.f * s));
    if (h.isMoon) {
      moon(dl, ImVec2(x + 4.f * s, y + 5.f * s), r, shadow, alpha(shadow, 0.f));
      moon(dl, ImVec2(x - 1.f * s, y - 1.f * s), r, rgb(255, 250, 226), rgb(255, 250, 226, 0));
      moon(dl, ImVec2(x, y), r, rgb(250, 218, 120), rgb(170, 120, 40, 200));
    } else {
      const float spin = 0.12f * std::sin(t * (0.6f + 0.3f * hash(i, 62)) + hash(i, 63) * 6.283f);
      star(dl, ImVec2(x + 3.f * s, y + 4.f * s), r, spin, shadow, alpha(shadow, 0.f));
      star(dl, ImVec2(x - 0.8f * s, y - 1.f * s), r, spin, rgb(255, 248, 220), rgb(255, 248, 220, 0));
      star(dl, ImVec2(x, y), r, spin, rgb(246, 190, 60), rgb(176, 112, 24, 220));
    }
    turn(dl, mark, pivot, a);
  }
}

// ---- the curtains ----

constexpr int kFolds = 6;       // pleats (strips of paper) in each curtain
constexpr int kRows = 40;       // down each strip

// how far in from its side a curtain reaches at height y, open by `open`
float innerEdge(const Stage& st, float y, float open) {
  float f;
  if (y < st.tieY) {
    const float e = 0.5f + 0.5f * std::cos(kPi * clamp01(y / st.tieY));
    f = 0.52f + (1.3f - 0.52f) * std::pow(std::fmax(0.f, e), 1.15f);
  } else {
    const float u = clamp01((y - st.tieY) / (st.H - st.tieY));
    f = 0.52f + (1.08f - 0.52f) * (1.f - (1.f - u) * (1.f - u));
  }
  const float gather = smooth(0.5f, 1.f, open);
  const float rest = st.side * (1.f + (f - 1.f) * gather);
  const float closed = st.W * 0.5f + 2.f;
  return closed + (rest - closed) * open;
}

// the curtain's red from its light (0 dark ... 1.1 brightest)
ImU32 velvet(float l) {
  static const float stops[][4] = {{0.25f, 58, 6, 22},   {0.55f, 128, 14, 32}, {0.8f, 190, 30, 42},
                                   {1.0f, 226, 54, 58},  {1.12f, 246, 96, 82}};
  if (l <= stops[0][0]) {
    return rgb(58, 6, 22);
  }
  for (int i = 1; i < 5; i++) {
    if (l <= stops[i][0]) {
      const float t = (l - stops[i - 1][0]) / (stops[i][0] - stops[i - 1][0]);
      return rgb(static_cast<int>(mix(stops[i - 1][1], stops[i][1], t)), static_cast<int>(mix(stops[i - 1][2], stops[i][2], t)),
                 static_cast<int>(mix(stops[i - 1][3], stops[i][3], t)));
    }
  }
  return rgb(246, 96, 82);
}

// a tassel hanging from top, len long, swinging by angle
void tassel(ImDrawList* dl, ImVec2 top, float len, float angle, float s) {
  const int mark = dl->VtxBuffer.Size;
  const ImU32 dark = rgb(150, 92, 22), gold = rgb(240, 184, 60), light = rgb(255, 230, 150);
  const float cord = len * 0.18f, headR = len * 0.13f;
  const ImVec2 head(top.x, top.y + cord + headR);
  dl->AddLine(top, ImVec2(head.x, head.y - headR), faded(gold), std::fmax(1.f, 1.6f * s));
  // its skirt of threads
  const float y0 = head.y + headR * 0.6f, y1 = top.y + len;
  const float w0 = headR * 0.9f, w1 = headR * 1.7f;
  const ImVec2 skirt[4] = {{head.x - w0, y0}, {head.x + w0, y0}, {head.x + w1, y1}, {head.x - w1, y1}};
  quad(dl, skirt[0], skirt[1], skirt[2], skirt[3], gold, gold, dark, dark);
  const int threads = 9;
  for (int i = 0; i < threads; i++) {
    const float t = (i + 0.5f) / threads;
    const ImVec2 a(mix(head.x - w0, head.x + w0, t), y0), b(mix(head.x - w1, head.x + w1, t), y1 + (i % 2) * 1.5f * s);
    dl->AddLine(a, b, faded(i % 2 == 0 ? light : rgb(214, 150, 40)), std::fmax(1.f, 1.2f * s));
  }
  // the head and the band round its neck
  dl->AddCircleFilled(head, headR, faded(dark), 18);
  dl->AddCircleFilled(ImVec2(head.x - headR * 0.12f, head.y - headR * 0.12f), headR * 0.82f, faded(gold), 18);
  dl->AddCircleFilled(ImVec2(head.x - headR * 0.35f, head.y - headR * 0.35f), headR * 0.28f, faded(light), 10);
  dl->AddRectFilled(ImVec2(head.x - w0 * 1.1f, y0 - 1.2f * s), ImVec2(head.x + w0 * 1.1f, y0 + 1.6f * s), faded(dark),
                    1.f * s);
  turn(dl, mark, top, angle);
}

// a gold rope from a to b (a gentle sag), thick t, twisted
void rope(ImDrawList* dl, ImVec2 a, ImVec2 b, float t, float sag, float s) {
  const int n = 16;
  ImVec2 p[n * 3];
  ImU32 c[n * 3];
  ImVec2 sp[n * 2];
  ImU32 sc[n * 2];
  for (int i = 0; i < n; i++) {
    const float u = static_cast<float>(i) / (n - 1);
    const ImVec2 m(mix(a.x, b.x, u), mix(a.y, b.y, u) + sag * std::sin(kPi * u));
    p[i * 3] = ImVec2(m.x, m.y - t * 0.5f);
    p[i * 3 + 1] = ImVec2(m.x, m.y - t * 0.1f);
    p[i * 3 + 2] = ImVec2(m.x, m.y + t * 0.5f);
    c[i * 3] = rgb(255, 230, 140);
    c[i * 3 + 1] = rgb(236, 176, 54);
    c[i * 3 + 2] = rgb(140, 86, 18);
    sp[i * 2] = ImVec2(m.x, m.y + t * 0.4f);
    sp[i * 2 + 1] = ImVec2(m.x, m.y + t * 0.4f + 7.f * s);
    sc[i * 2] = rgb(20, 0, 10, 140);
    sc[i * 2 + 1] = rgb(20, 0, 10, 0);
  }
  grid(dl, sp, sc, n, 2);
  grid(dl, p, c, n, 3);
  // its outline, smoothing the edges
  ImVec2 top[n], bottom[n];
  for (int i = 0; i < n; i++) {
    top[i] = p[i * 3];
    bottom[i] = p[i * 3 + 2];
  }
  dl->AddPolyline(top, n, faded(rgb(150, 96, 24, 200)), 0, std::fmax(1.f, 0.9f * s));
  dl->AddPolyline(bottom, n, faded(rgb(96, 56, 12, 220)), 0, std::fmax(1.f, 1.1f * s));
  // the twist
  const float len = std::fabs(b.x - a.x);
  const int twists = static_cast<int>(len / (5.f * s));
  for (int i = 0; i < twists; i++) {
    const float u = (i + 0.5f) / twists;
    const ImVec2 m(mix(a.x, b.x, u), mix(a.y, b.y, u) + sag * std::sin(kPi * u));
    dl->AddLine(ImVec2(m.x - t * 0.3f, m.y - t * 0.45f), ImVec2(m.x + t * 0.3f, m.y + t * 0.45f),
                faded(rgb(150, 92, 22, 170)), std::fmax(1.f, 1.1f * s));
  }
}

void curtain(ImDrawList* dl, const Stage& st, bool left, float open) {
  const float s = st.s, W = st.W, H = st.H, t = st.time;
  auto X = [&](float x) { return left ? x : W - x; }; // from the curtain's side to the screen
  const float outer = -10.f * s;
  // the pleats' edges across the curtain (of its width), uneven
  float b[kFolds + 1];
  {
    float sum = 0.f;
    float w[kFolds];
    for (int j = 0; j < kFolds; j++) {
      w[j] = 0.8f + 0.45f * hash(j + (left ? 0 : 20), 70);
      sum += w[j];
    }
    b[0] = 0.f;
    for (int j = 0; j < kFolds; j++) {
      b[j + 1] = b[j] + w[j] / sum;
    }
  }
  // each edge down the curtain, swaying: the cloth hangs from the rail, is held at the tie-back
  // and swings freest at its foot
  static float ex[kFolds + 1][kRows];
  float ys[kRows];
  for (int r = 0; r < kRows; r++) {
    const float v = static_cast<float>(r) / (kRows - 1);
    ys[r] = -2.f + (H + 4.f) * v;
  }
  for (int r = 0; r < kRows; r++) {
    const float y = ys[r];
    const float in = innerEdge(st, y, open);
    const float held = y < st.tieY ? 0.45f * std::sin(kPi * y / st.tieY)
                                   : std::pow(clamp01((y - st.tieY) / (H - st.tieY)), 1.2f);
    for (int j = 0; j <= kFolds; j++) {
      const float wob = j == 0 ? 0.f : 1.5f * s * std::sin(y * 0.011f / s + j * 1.7f + (left ? 0.f : 2.f));
      const float sway = 2.6f * s * held * std::sin(t * (0.85f + 0.05f * j) + j * 0.45f + y * 0.002f + (left ? 0.f : 1.3f));
      ex[j][r] = outer + (in - outer) * b[j] + (j == 0 ? 0.f : wob + sway);
    }
  }
  // the light on the curtain: brighter towards the stage, darker under the valance and by the tie
  auto light = [&](int j, float y) {
    const float lj = 0.76f + 0.24f * j / (kFolds - 1);
    const float lv = 0.7f + 0.3f * smooth(0.f, H * 0.32f, y) - 0.08f * smooth(H * 0.8f, H, y);
    const float d = (y - st.tieY) / (22.f * s);
    const float lp = 1.f - 0.3f * open * std::exp(-d * d);
    return lj * lv * lp;
  };
  ImVec2 p[kRows * 4];
  ImU32 c[kRows * 4];
  // its shadow on the stage
  for (int r = 0; r < kRows; r++) {
    const float x = ex[kFolds][r];
    p[r * 2] = ImVec2(X(x - 4.f * s), ys[r]);
    p[r * 2 + 1] = ImVec2(X(x + 34.f * s), ys[r] + 6.f * s);
    c[r * 2] = rgb(10, 2, 22, 150);
    c[r * 2 + 1] = rgb(10, 2, 22, 0);
  }
  grid(dl, p, c, kRows, 2);
  static const float kAcross[4] = {0.f, 0.28f, 0.66f, 1.f};
  static const float kFold[4] = {0.4f, 0.74f, 1.06f, 0.8f};
  for (int j = 0; j < kFolds; j++) {
    const float tint = 0.94f + 0.12f * hash(j + (left ? 0 : 20), 71);
    if (j > 0) {
      // the shadow this pleat casts on the one behind it
      for (int r = 0; r < kRows; r++) {
        const float x = ex[j][r];
        p[r * 2] = ImVec2(X(x + 2.f * s), ys[r]);
        p[r * 2 + 1] = ImVec2(X(x - 12.f * s), ys[r] + 2.f * s);
        c[r * 2] = rgb(30, 0, 12, 190);
        c[r * 2 + 1] = rgb(30, 0, 12, 0);
      }
      grid(dl, p, c, kRows, 2);
    }
    // the pleat: round, so darker where it turns away, lit where it faces the stage
    for (int r = 0; r < kRows; r++) {
      const float x0 = ex[j][r], x1 = ex[j + 1][r] + (j + 1 < kFolds ? 3.f * s : 0.f);
      const float l = light(j, ys[r]);
      for (int k = 0; k < 4; k++) {
        p[r * 4 + k] = ImVec2(X(mix(x0, x1, kAcross[k])), ys[r]);
        c[r * 4 + k] = shade(velvet(l * kFold[k]), tint);
      }
    }
    grid(dl, p, c, kRows, 4);
    if (j > 0) {
      // its cut edge, light against the shadow
      ImVec2 e[kRows];
      for (int r = 0; r < kRows; r++) {
        e[r] = ImVec2(X(ex[j][r] + 0.6f * s), ys[r]);
      }
      dl->AddPolyline(e, kRows, faded(rgb(255, 160, 140, 95)), 0, std::fmax(1.f, 1.f * s));
    }
  }
  // the inner edge: warm stage light on it, and a light paper edge
  for (int r = 0; r < kRows; r++) {
    const float x = ex[kFolds][r];
    const float l = 0.55f + 0.45f * smooth(st.bandH, H * 0.45f, ys[r]);
    p[r * 2] = ImVec2(X(x), ys[r]);
    p[r * 2 + 1] = ImVec2(X(x - 20.f * s), ys[r]);
    c[r * 2] = rgb(255, 168, 96, static_cast<int>(130 * l));
    c[r * 2 + 1] = rgb(255, 168, 96, 0);
  }
  grid(dl, p, c, kRows, 2);
  {
    ImVec2 e[kRows];
    for (int r = 0; r < kRows; r++) {
      e[r] = ImVec2(X(ex[kFolds][r] - 0.8f * s), ys[r]);
    }
    dl->AddPolyline(e, kRows, faded(rgb(255, 220, 176, 235)), 0, std::fmax(1.f, 2.f * s));
  }
  // the tie-back: a gold rope round the gathered cloth, a tassel at its knot
  const float tieShow = smooth(0.8f, 1.f, open);
  if (tieShow > 0.f) {
    const float saved = sFade;
    sFade *= tieShow;
    const int r = static_cast<int>(std::lround((st.tieY + 2.f) / (H + 4.f) * (kRows - 1)));
    const float in = ex[kFolds][std::clamp(r, 0, kRows - 1)];
    const float rt = 10.f * s;
    // the cloth bunched above and below it
    vgrad(dl, ImVec2(X(outer), st.tieY - 16.f * s), ImVec2(X(in), st.tieY), rgb(30, 0, 12, 0), rgb(30, 0, 12, 90));
    vgrad(dl, ImVec2(X(outer), st.tieY), ImVec2(X(in), st.tieY + 16.f * s), rgb(30, 0, 12, 90), rgb(30, 0, 12, 0));
    rope(dl, ImVec2(X(outer), st.tieY - 2.f * s), ImVec2(X(in + 2.f * s), st.tieY), rt, 3.f * s, s);
    const ImVec2 knot(X(in + 1.f * s), st.tieY + 1.f * s);
    const float swing = 0.07f * std::sin(t * 1.1f + (left ? 0.f : 1.9f));
    tassel(dl, ImVec2(knot.x, knot.y + 4.f * s), 50.f * s, swing, s);
    dl->AddCircleFilled(ImVec2(knot.x + 1.5f * s, knot.y + 2.f * s), 9.f * s, faded(rgb(20, 0, 10, 100)), 20);
    dl->AddCircleFilled(knot, 9.f * s, faded(rgb(150, 92, 22)), 20);
    dl->AddCircleFilled(ImVec2(knot.x - 1.f * s, knot.y - 1.f * s), 7.2f * s, faded(rgb(240, 186, 64)), 20);
    dl->AddCircleFilled(ImVec2(knot.x - 2.8f * s, knot.y - 2.8f * s), 2.4f * s, faded(rgb(255, 238, 180)), 10);
    sFade = saved;
  }
}

// ---- the valance ----

// a gold fringe hanging from the curve y = top(u) for u 0..1 across x0..x1, len long: a dark gold
// backing (so no sky shows between the strands and nothing shimmers when scaled) under fewer, wider
// strands, each round (light down its middle) and tapering to its tip
template <typename Top> void fringe(ImDrawList* dl, float x0, float x1, Top top, float len, float s, int seed) {
  constexpr int kCols = 24;
  ImVec2 p[kCols * 2];
  ImU32 c[kCols * 2];
  for (int q = 0; q < kCols; q++) {
    const float u = static_cast<float>(q) / (kCols - 1);
    const float y = top(u);
    p[q * 2] = ImVec2(mix(x0, x1, u), y);
    p[q * 2 + 1] = ImVec2(mix(x0, x1, u), y + len * 0.8f);
    c[q * 2] = rgb(132, 80, 16);
    c[q * 2 + 1] = rgb(104, 60, 12);
  }
  grid(dl, p, c, kCols, 2);
  const float pitch = std::fmax(4.f, 5.2f * s);
  const int count = std::max(1, static_cast<int>((x1 - x0) / pitch));
  const float step = (x1 - x0) / count, hw = step * 0.42f;
  for (int i = 0; i < count; i++) {
    const float x = x0 + (i + 0.5f) * step;
    const float u = (x - x0) / (x1 - x0);
    const float y0 = top(u) - 1.f * s;
    const float l = len * (0.94f + 0.12f * hash(i + seed * 997, 80));
    const float tip = 0.55f;
    const ImVec2 q[6] = {{x - hw, y0}, {x, y0}, {x + hw, y0}, {x - hw * tip, y0 + l}, {x, y0 + l + hw * 0.4f},
                         {x + hw * tip, y0 + l}};
    const ImU32 edge = rgb(178, 116, 26), mid = rgb(255, 222, 124), edgeTip = rgb(200, 138, 40),
                midTip = rgb(255, 236, 170);
    const ImU32 qc[6] = {edge, mid, edge, edgeTip, midTip, edgeTip};
    grid(dl, q, qc, 2, 3);
  }
}

// a gold cord along the curve y = mid(u) across x0..x1, thick t
template <typename Mid> void cord(ImDrawList* dl, float x0, float x1, Mid mid, float t, float s) {
  constexpr int kCols = 24;
  ImVec2 p[kCols * 3];
  ImU32 c[kCols * 3];
  ImVec2 lo[kCols];
  for (int q = 0; q < kCols; q++) {
    const float u = static_cast<float>(q) / (kCols - 1);
    const float x = mix(x0, x1, u), y = mid(u);
    p[q * 3] = ImVec2(x, y - t * 0.5f);
    p[q * 3 + 1] = ImVec2(x, y - t * 0.1f);
    p[q * 3 + 2] = ImVec2(x, y + t * 0.5f);
    c[q * 3] = rgb(255, 232, 150);
    c[q * 3 + 1] = rgb(238, 178, 56);
    c[q * 3 + 2] = rgb(146, 90, 20);
    lo[q] = p[q * 3 + 2];
  }
  grid(dl, p, c, kCols, 3);
  dl->AddPolyline(lo, kCols, faded(rgb(96, 56, 12, 200)), 0, std::fmax(1.f, 1.f * s));
}

// one end of the valance: a jabot, a pleated tail falling over the curtain's head from the pelmet to
// its last rosette at x = inner, longest at the outside (left: the left one)
void jabot(ImDrawList* dl, const Stage& st, bool left, float inner) {
  const float s = st.s, W = st.W;
  auto X = [&](float x) { return left ? x : W - x; };
  const float top = st.bandH - 10.f * s;
  const float outerY = st.bandH + st.drop * 2.1f, innerY = st.bandH + st.drop * 0.55f;
  const float x0 = -6.f * s, x1 = inner;
  auto bottom = [&](float x) { return mix(outerY, innerY, clamp01((x - x0) / (x1 - x0))); };
  // its shadow on the curtain, along its inner edge and under it
  {
    constexpr int kN = 12;
    ImVec2 p[kN * 2];
    ImU32 c[kN * 2];
    for (int q = 0; q < kN; q++) {
      const float x = mix(x0, x1, static_cast<float>(q) / (kN - 1));
      const float y = bottom(x);
      p[q * 2] = ImVec2(X(x), y - 4.f * s);
      p[q * 2 + 1] = ImVec2(X(x + 3.f * s), y + 16.f * s);
      c[q * 2] = rgb(10, 2, 22, 140);
      c[q * 2 + 1] = rgb(10, 2, 22, 0);
    }
    grid(dl, p, c, kN, 2);
  }
  // three pleats, the inner one in front
  constexpr int kPleats = 3;
  static const float kAcross[4] = {0.f, 0.28f, 0.66f, 1.f};
  static const float kFold[4] = {0.42f, 0.76f, 1.06f, 0.82f};
  for (int j = 0; j < kPleats; j++) {
    const float a = mix(x0, x1, static_cast<float>(j) / kPleats);
    const float b = mix(x0, x1, static_cast<float>(j + 1) / kPleats) + (j + 1 < kPleats ? 3.f * s : 0.f);
    if (j > 0) {
      quad(dl, ImVec2(X(a + 2.f * s), top), ImVec2(X(a - 10.f * s), top), ImVec2(X(a - 10.f * s), bottom(a)),
           ImVec2(X(a + 2.f * s), bottom(a)), rgb(30, 0, 12, 170), rgb(30, 0, 12, 0), rgb(30, 0, 12, 0),
           rgb(30, 0, 12, 170));
    }
    ImVec2 p[3 * 4];
    ImU32 c[3 * 4];
    const float light = 0.8f + 0.2f * j / (kPleats - 1);
    for (int k = 0; k < 4; k++) {
      const float x = mix(a, b, kAcross[k]);
      const float yb = bottom(x);
      p[k] = ImVec2(X(x), top);
      p[4 + k] = ImVec2(X(x), mix(top, yb, 0.5f));
      p[8 + k] = ImVec2(X(x), yb);
      c[k] = velvet(0.62f * kFold[k] * light);
      c[4 + k] = velvet(0.92f * kFold[k] * light);
      c[8 + k] = velvet(1.02f * kFold[k] * light);
    }
    grid(dl, p, c, 3, 4);
    if (j > 0) {
      const ImVec2 e[2] = {ImVec2(X(a + 0.6f * s), top), ImVec2(X(a + 0.6f * s), bottom(a))};
      dl->AddPolyline(e, 2, faded(rgb(255, 160, 140, 95)), 0, std::fmax(1.f, 1.f * s));
    }
  }
  // the fringe and gold cord along its slanting hem
  const float fa = X(x0), fb = X(x1);
  const float lo = std::fmin(fa, fb), hi = std::fmax(fa, fb);
  auto hem = [&](float u) { return bottom(left ? mix(x0, x1, u) : mix(x1, x0, u)); };
  fringe(dl, lo, hi, [&](float u) { return hem(u) + 1.f * s; }, st.fringe * 1.1f, s, left ? 101 : 102);
  cord(dl, lo, hi, hem, 4.6f * s, s);
}

void valance(ImDrawList* dl, const Stage& st) {
  const float s = st.s, W = st.W, t = st.time;
  const float bandH = st.bandH, drop = st.drop;
  // a whole number of swags between the two end rosettes, which sit over the curtains' heads and
  // fully inside the window; an odd number, so one hangs in the middle, deepest, in front
  const float rr = 11.f * s;
  const float e0 = std::fmax(rr + 6.f * s, st.side * 0.8f), e1 = W - e0;
  int n = std::max(3, static_cast<int>(std::lround((e1 - e0) / (290.f * s))));
  n |= 1;
  const float sw = (e1 - e0) / n, ov = 4.f * s;
  auto sag = [](float u) { return std::pow(std::fmax(0.f, std::sin(kPi * clamp01(u))), 0.85f); }; // (sin(pi) < 0)
  auto depth = [&](int i) { return drop * (i == n / 2 ? 1.f : 0.9f - 0.04f * std::abs(i - n / 2) / std::max(1, n / 2)); };
  constexpr int kCols = 30;
  static const float kDrop[3] = {0.4f, 0.7f, 1.f};
  auto bottomOf = [&](int i, int k, float u) { return bandH - 2.f * s + depth(i) * kDrop[k] * sag(u); };
  ImVec2 p[kCols * 3];
  ImU32 c[kCols * 3];
  // the fringe's shadow on what is behind
  for (int i = 0; i < n; i++) {
    const float x0 = e0 + i * sw - ov, x1 = e0 + (i + 1) * sw + ov;
    for (int k = 0; k < kCols; k++) {
      const float u = static_cast<float>(k) / (kCols - 1);
      const float y = bottomOf(i, 2, u) + st.fringe * 0.7f;
      p[k * 2] = ImVec2(mix(x0, x1, u), y - 4.f * s);
      p[k * 2 + 1] = ImVec2(mix(x0, x1, u) + 3.f * s, y + 14.f * s);
      c[k * 2] = rgb(10, 2, 22, 120);
      c[k * 2 + 1] = rgb(10, 2, 22, 0);
    }
    grid(dl, p, c, kCols, 2);
  }
  // the pelmet: deep red, softly pleated
  vgrad(dl, ImVec2(0, 0), ImVec2(W, bandH), rgb(104, 10, 26), rgb(160, 22, 36));
  const float pleat = 22.f * s;
  for (float x = 0.f; x < W; x += pleat) {
    quad(dl, ImVec2(x, 0), ImVec2(x + pleat * 0.5f, 0), ImVec2(x + pleat * 0.5f, bandH), ImVec2(x, bandH),
         rgb(255, 120, 110, 0), rgb(255, 120, 110, 18), rgb(255, 120, 110, 18), rgb(255, 120, 110, 0));
    quad(dl, ImVec2(x + pleat * 0.5f, 0), ImVec2(x + pleat, 0), ImVec2(x + pleat, bandH), ImVec2(x + pleat * 0.5f, bandH),
         rgb(30, 0, 10, 0), rgb(30, 0, 10, 30), rgb(30, 0, 10, 30), rgb(30, 0, 10, 0));
  }
  // the swags, from the ends in, each of three layered folds with a gold cord and fringe
  for (int o = 0; o < n; o++) {
    const int i = (o % 2 == 0) ? o / 2 : n - 1 - o / 2; // ends first, the middle last
    const float x0 = e0 + i * sw - ov, x1 = e0 + (i + 1) * sw + ov;
    for (int k = 0; k < 3; k++) {
      if (k > 0) {
        // this fold's shadow up onto the one behind
        for (int q = 0; q < kCols; q++) {
          const float u = static_cast<float>(q) / (kCols - 1);
          const float y = bottomOf(i, k - 1, u) - 3.f * s - depth(i) * 0.16f * sag(u);
          p[q * 2] = ImVec2(mix(x0, x1, u), y + 2.f * s);
          p[q * 2 + 1] = ImVec2(mix(x0, x1, u), y - 6.f * s);
          c[q * 2] = rgb(30, 0, 12, 150);
          c[q * 2 + 1] = rgb(30, 0, 12, 0);
        }
        grid(dl, p, c, kCols, 2);
      }
      for (int q = 0; q < kCols; q++) {
        const float u = static_cast<float>(q) / (kCols - 1);
        const float x = mix(x0, x1, u);
        const float yb = bottomOf(i, k, u);
        const float yt = k == 0 ? bandH - 10.f * s : bottomOf(i, k - 1, u) - 3.f * s - depth(i) * 0.16f * sag(u);
        const float ends = 0.72f + 0.28f * smooth(0.f, 0.22f, std::fmin(u, 1.f - u)); // gathered at the ends
        const float lift = 0.9f + 0.1f * k;
        p[q * 3] = ImVec2(x, yt);
        p[q * 3 + 1] = ImVec2(x, mix(yt, yb, 0.55f));
        p[q * 3 + 2] = ImVec2(x, yb);
        c[q * 3] = velvet(0.5f * ends * lift);
        c[q * 3 + 1] = velvet(0.86f * ends * lift);
        c[q * 3 + 2] = velvet(1.04f * ends * lift);
      }
      grid(dl, p, c, kCols, 3);
      if (k < 2) {
        ImVec2 e[kCols];
        for (int q = 0; q < kCols; q++) {
          const float u = static_cast<float>(q) / (kCols - 1);
          e[q] = ImVec2(mix(x0, x1, u), bottomOf(i, k, u) - 0.8f * s);
        }
        dl->AddPolyline(e, kCols, faded(rgb(255, 170, 150, 130)), 0, std::fmax(1.f, 1.2f * s));
      }
    }
    fringe(dl, x0, x1, [&](float u) { return bottomOf(i, 2, u) + 1.f * s; }, st.fringe, s, i);
    cord(dl, x0, x1, [&](float u) { return bottomOf(i, 2, u); }, 4.6f * s, s);
  }
  // the ends: jabots over the curtains' heads
  jabot(dl, st, true, e0 + 2.f * s);
  jabot(dl, st, false, e0 + 2.f * s);
  // the gold rail along the pelmet's foot: a round rod
  const float rail = 8.f * s;
  vgrad(dl, ImVec2(0, bandH - rail), ImVec2(W, bandH - rail * 0.55f), rgb(255, 234, 156), rgb(246, 196, 72));
  vgrad(dl, ImVec2(0, bandH - rail * 0.55f), ImVec2(W, bandH), rgb(246, 196, 72), rgb(134, 80, 16));
  dl->AddLine(ImVec2(0, bandH - rail), ImVec2(W, bandH - rail), faded(rgb(120, 70, 14, 200)), std::fmax(1.f, 1.f * s));
  dl->AddLine(ImVec2(0, bandH), ImVec2(W, bandH), faded(rgb(90, 50, 10, 220)), std::fmax(1.f, 1.f * s));
  vgrad(dl, ImVec2(0, bandH), ImVec2(W, bandH + 6.f * s), rgb(20, 0, 10, 100), rgb(20, 0, 10, 0));
  // rosettes where the swags meet (and at the ends), a tassel hanging from each
  for (int i = 0; i <= n; i++) {
    const float x = e0 + i * sw;
    const ImVec2 c0(x, bandH - rail * 0.5f);
    const float swing = 0.06f * std::sin(t * 1.2f + i * 0.9f);
    tassel(dl, ImVec2(c0.x, c0.y + 6.f * s), drop * 1.05f, swing, s);
    dl->AddCircleFilled(ImVec2(c0.x + 1.5f * s, c0.y + 2.5f * s), rr, faded(rgb(20, 0, 10, 100)), 24);
    dl->AddCircleFilled(c0, rr, faded(rgb(146, 90, 20)), 24);
    for (int k = 0; k < 8; k++) {
      const float a = k * kPi / 4.f + kPi / 8.f;
      dl->AddCircleFilled(ImVec2(c0.x + std::cos(a) * rr * 0.6f, c0.y + std::sin(a) * rr * 0.6f), rr * 0.36f,
                          faded(k % 2 == 0 ? rgb(250, 206, 96) : rgb(232, 172, 52)), 12);
    }
    dl->AddCircle(c0, rr, faded(rgb(110, 64, 12, 220)), 24, std::fmax(1.f, 1.f * s));
    dl->AddCircleFilled(c0, rr * 0.36f, faded(rgb(196, 32, 42)), 14);
    dl->AddCircleFilled(ImVec2(c0.x - rr * 0.1f, c0.y - rr * 0.1f), rr * 0.13f, faded(rgb(255, 170, 160)), 8);
  }
}

// the open stage's rectangle: inside the curtains, below the valance's fringe
void openStage(const Stage& st, ImVec2* min, ImVec2* max) {
  *min = ImVec2(st.side + 12.f * st.s, st.bandH - 2.f * st.s + st.drop + st.fringe + 6.f * st.s);
  *max = ImVec2(st.W - st.side - 12.f * st.s, st.floorTop + (st.H - st.floorTop) * 0.35f); // standing on the stage
}

bool sStillKnown = false, sStill = false;

} // namespace

bool launcherStageStill() {
  if (!sStillKnown) {
    sStillKnown = true;
    if (const char* e = SDL_getenv("TTYD_LAUNCHER_STILL")) {
      sStill = e[0] != '\0' && e[0] != '0';
    } else {
#ifdef _WIN32
      // Settings > Accessibility > Visual effects > Animation effects
      BOOL animate = TRUE;
      if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0)) {
        sStill = !animate;
      }
#endif
    }
  }
  return sStill;
}

void launcherBackdrop(float time, float scale, ImVec2* min, ImVec2* max, bool popup) {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  Stage st = measure(time, scale);
  openStage(st, min, max);
  st.compact = max->y - min->y < 560.f * scale; // launcher_ui.cpp's: the wordmark beside the tabs
  auto layer = [&](LauncherStageLayer l) { return popup ? launcherPopupLayer(l, time) : 1.f; };
  const float lights = smooth(0.f, 1.f, layer(LauncherStageLayer::Lights));

  sky(dl, st, st.time);
  beam(dl, st, lights);
  int mark = dl->VtxBuffer.Size;
  clouds(dl, st, false);
  fold(dl, mark, st.floorTop, popUp(layer(LauncherStageLayer::CloudsBack)));
  mark = dl->VtxBuffer.Size;
  clouds(dl, st, true);
  fold(dl, mark, st.floorTop, popUp(layer(LauncherStageLayer::CloudsFront)));
  mark = dl->VtxBuffer.Size;
  boards(dl, st, lights);
  fold(dl, mark, st.H, easeOut(layer(LauncherStageLayer::Floor)));
  mark = dl->VtxBuffer.Size;
  hanging(dl, st, *min, *max);
  fold(dl, mark, st.bandH * 0.5f, popUp(layer(LauncherStageLayer::Hanging)));
  curtain(dl, st, true, 1.f);
  curtain(dl, st, false, 1.f);
  valance(dl, st);

  // and clear of a phone's notch, Dynamic Island and home indicator (the
  // backdrop runs under them)
  SDL_Rect safe;
  int ww = 0, wh = 0;
  SDL_Window* w = aurora_get_window();
  const ImVec2 size(st.W, st.H);
  if (w != nullptr && SDL_GetWindowSafeArea(w, &safe) && SDL_GetWindowSize(w, &ww, &wh) && ww > 0 && wh > 0) {
    const float kx = size.x / ww, ky = size.y / wh; // window units to ImGui's
    min->x = std::fmax(min->x, safe.x * kx + 8.f * scale);
    min->y = std::fmax(min->y, safe.y * ky + 4.f * scale);
    max->x = std::fmin(max->x, (safe.x + safe.w) * kx - 8.f * scale);
    max->y = std::fmin(max->y, (safe.y + safe.h) * ky - 4.f * scale);
  }
}

void launcherStageFront(float open, float time, float scale) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  const Stage st = measure(time, scale);
  curtain(dl, st, true, open);
  curtain(dl, st, false, open);
  valance(dl, st);
}
