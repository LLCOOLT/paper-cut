// Rogueport's chalk outline without a Japanese disc (chalk_outline.h).
//
// The Japanese release's m/gor_01/d has five things on house 4's floor, under
// the flies, that the US and European maps dropped: a chalk outline (an
// untextured chalk-white strip, pPlane359_q00647, in S_ie_04_kmnC) and two
// stains of the room's own gor_yogosi_01 texture, each drawn twice
// (pPlane136_q00606..9, beside the one the US kept, q00605, in S_i04_kabiC).
//
// With no Japanese disc to read that map from, they are put back into this
// disc's own map, made from this disc:
// - the outline is traced from an art recipe's image (Mario's arms-out pose,
//   his own texture on the disc, filled white: restorations.recipe), its outer
//   edge simplified to a few dozen points and drawn, as the Japanese map draws
//   its own, as a strip of chalk-white vertices on the floor, the size of the
//   Japanese one and where it lies;
// - the stains are the texture this disc still has, at the Japanese map's
//   places, with the material and draw mode of the stain the US kept.
// The new joints, meshes and vertices are appended to the map's data, its
// vertex arrays copied there with the new vertices after the old (so every
// mesh's indices still hold), and the new pointer words added to the
// relocation table: the game loads it as any map file. Nothing of the
// Japanese disc's is used or shipped: only where the marks lie.
#include "chalk_outline.h"

#include "port/art_recipe.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kBody = "art/restorations/chalk_body.png";
constexpr uint32_t kHeader = 0x20; // the map file's header; pointers are offsets into the data after it

// The outline: where the Japanese map's lies in S_ie_04_kmnC (the middle of
// its extent; map units, the game draws maps at 10x), its size (the longer
// side) and its strip's width, the same as the Japanese one's, turned a
// little as that one is; the trace's tolerance in the image's texels.
constexpr float kOutlineX = 47.9f, kOutlineY = 0.1f, kOutlineZ = -18.6f;
constexpr float kOutlineTurn = 12.0f; // degrees about the vertical
constexpr float kOutlineSize = 5.0f;
constexpr float kOutlineHalfWidth = 0.17f;
constexpr double kTraceTolerance = 2.0;
constexpr uint8_t kChalk[4] = {0xCC, 0xCC, 0xCC, 0xFF}; // the Japanese strip's vertex colour

// The stains, in S_i04_kabiC as the Japanese map has them: the joint's place
// (scale 0.6528, turned -90 degrees, as the US stain's), and a quad of the
// texture: its corners (x, z), texture coordinates and vertex colours (RGBA).
struct Corner {
  float x, z, s, t;
  uint32_t rgba;
};
struct Stain {
  float tx, tz;
  Corner c[4];
};
constexpr Stain kStains[2] = {
    {57.701157f, -32.133881f,
     {{2.496f, 21.723f, 1.0f, 0.4296875f, 0x802A2299},
      {-19.289f, 16.594f, 0.0f, 0.4296875f, 0x73261EB2},
      {5.305f, 9.797f, 1.0f, 0.0f, 0x802A2299},
      {-16.484f, 4.668f, 0.0f, 0.0f, 0x73261EB2}}},
    {55.701157f, -28.237671f,
     {{-20.531f, 10.480f, 0.0f, 0.0f, 0x73261EB2},
      {-22.996f, 0.0f, 0.0f, 0.375f, 0x73261EB2},
      {1.258f, 5.352f, 1.0f, 0.0f, 0x73261EB2},
      {0.0f, 0.0f, 1.0f, 0.1875f, 0x73261EB2}}},
};
constexpr float kStainScale = 0.652809f, kStainY = 0.050006f, kStainTurn = -90.0f;

uint32_t rd32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}
void wr32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
  b[at] = uint8_t(v >> 24);
  b[at + 1] = uint8_t(v >> 16);
  b[at + 2] = uint8_t(v >> 8);
  b[at + 3] = uint8_t(v);
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
  b.resize(b.size() + 4);
  wr32(b, b.size() - 4, v);
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(uint8_t(v >> 8));
  b.push_back(uint8_t(v));
}
void putF(std::vector<uint8_t>& b, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  put32(b, v);
}
void align(std::vector<uint8_t>& b, size_t to) {
  while (b.size() % to != 0) {
    b.push_back(0);
  }
}

struct P2 {
  double x, y;
};

// ---- the outline, from the recipe's image ----

// The image's opaque texels (alpha 128 or more), the largest 4-connected
// shape's outer edge as the corners of its texels, clockwise from its top
// left corner.
std::vector<P2> traceEdge(const uint8_t* rgba, int w, int h, int pitch) {
  std::vector<int> label(size_t(w) * h, 0);
  auto solid = [&](int x, int y) { return rgba[size_t(y) * pitch + size_t(x) * 4 + 3] >= 128; };
  int best = 0, n = 0;
  size_t bestCount = 0;
  std::vector<std::pair<int, int>> stack;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (!solid(x, y) || label[size_t(y) * w + x] != 0) {
        continue;
      }
      n++;
      size_t count = 0;
      label[size_t(y) * w + x] = n;
      stack.push_back({x, y});
      while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        count++;
        constexpr int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
          const int u = a + kDx[k], v = b + kDy[k];
          if (u >= 0 && u < w && v >= 0 && v < h && solid(u, v) && label[size_t(v) * w + u] == 0) {
            label[size_t(v) * w + u] = n;
            stack.push_back({u, v});
          }
        }
      }
      if (count > bestCount) {
        best = n;
        bestCount = count;
      }
    }
  }
  std::vector<P2> edge;
  if (best == 0) {
    return edge;
  }
  auto in = [&](int x, int y) { return x >= 0 && x < w && y >= 0 && y < h && label[size_t(y) * w + x] == best; };
  int sx = -1, sy = -1;
  for (int y = 0; y < h && sy < 0; y++) {
    for (int x = 0; x < w; x++) {
      if (in(x, y)) {
        sx = x;
        sy = y;
        break;
      }
    }
  }
  // along the texels' edges with the shape on the right: 0 east, 1 south, 2 west, 3 north
  constexpr int kDx[4] = {1, 0, -1, 0}, kDy[4] = {0, 1, 0, -1};
  int cx = sx, cy = sy, d = 0;
  const size_t limit = size_t(w + 1) * (h + 1) * 4;
  do {
    edge.push_back({double(cx), double(cy)});
    int lx, ly, rx, ry; // the texels ahead on the left and on the right
    switch (d) {
    case 0: lx = cx, ly = cy - 1, rx = cx, ry = cy; break;
    case 1: lx = cx, ly = cy, rx = cx - 1, ry = cy; break;
    case 2: lx = cx - 1, ly = cy, rx = cx - 1, ry = cy - 1; break;
    default: lx = cx - 1, ly = cy - 1, rx = cx, ry = cy - 1; break;
    }
    if (in(lx, ly)) {
      d = (d + 3) % 4;
    } else if (!in(rx, ry)) {
      d = (d + 1) % 4;
    }
    cx += kDx[d];
    cy += kDy[d];
  } while (!(cx == sx && cy == sy) && edge.size() < limit);
  return edge;
}

// Douglas-Peucker: the points of p[a..b] (both kept) that keep within tol of it
void simplify(const std::vector<P2>& p, size_t a, size_t b, double tol, std::vector<P2>& out) {
  const double dx = p[b].x - p[a].x, dy = p[b].y - p[a].y, len = std::sqrt(dx * dx + dy * dy);
  double far = -1;
  size_t at = a;
  for (size_t i = a + 1; i < b; i++) {
    const double ex = p[i].x - p[a].x, ey = p[i].y - p[a].y;
    const double dist = len > 0 ? std::fabs(dy * ex - dx * ey) / len : std::sqrt(ex * ex + ey * ey);
    if (dist > far) {
      far = dist;
      at = i;
    }
  }
  if (far <= tol) {
    out.push_back(p[a]);
    return;
  }
  simplify(p, a, at, tol, out);
  simplify(p, at, b, tol, out);
}

// the closed outline of the recipe's image, simplified (texel units), or empty
std::vector<P2> outline(std::string& why) {
  const void* png = nullptr;
  unsigned int size = 0;
  if (!portArtAsset(kBody, &png, &size) || png == nullptr) {
    why = "its picture (an art recipe) couldn't be made from this disc";
    return {};
  }
  SDL_IOStream* io = SDL_IOFromConstMem(png, size);
  SDL_Surface* s = io != nullptr ? SDL_LoadPNG_IO(io, true) : nullptr;
  SDL_Surface* rgba = s != nullptr ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32) : nullptr;
  SDL_DestroySurface(s);
  if (rgba == nullptr) {
    why = "its picture couldn't be read";
    return {};
  }
  std::vector<P2> edge =
      traceEdge(static_cast<const uint8_t*>(rgba->pixels), rgba->w, rgba->h, rgba->pitch);
  SDL_DestroySurface(rgba);
  if (edge.size() < 16) {
    why = "its picture has no shape";
    return {};
  }
  // split the loop at the corner farthest from the first, simplify each half
  size_t far = 0;
  double farD = -1;
  for (size_t i = 0; i < edge.size(); i++) {
    const double d = std::hypot(edge[i].x - edge[0].x, edge[i].y - edge[0].y);
    if (d > farD) {
      farD = d;
      far = i;
    }
  }
  edge.push_back(edge[0]);
  std::vector<P2> poly;
  simplify(edge, 0, far, kTraceTolerance, poly);
  simplify(edge, far, edge.size() - 1, kTraceTolerance, poly);
  if (poly.size() < 8) {
    why = "its picture's shape is too small";
    return {};
  }
  return poly;
}

// ---- the map file ----

struct Map {
  const std::vector<uint8_t>& file;
  uint32_t dataSize = 0, relCount = 0, tableEnd = 0;

  explicit Map(const std::vector<uint8_t>& f) : file(f) {}
  bool ok(uint32_t off, uint32_t n) const { return uint64_t(off) + n <= dataSize; }
  uint32_t u32(uint32_t off) const { return ok(off, 4) ? rd32(file, kHeader + off) : 0; }
  std::string str(uint32_t off) const {
    std::string s;
    for (uint32_t i = off; ok(i, 1) && file[kHeader + i] != 0 && s.size() < 64; i++) {
      s += char(file[kHeader + i]);
    }
    return s;
  }
};

struct Found {
  uint32_t kmn = 0, kabi = 0, mark = 0, lambert = 0;
  bool already = false;
};

void findJoints(const Map& m, uint32_t root, Found& f) {
  std::vector<uint32_t> stack{root};
  size_t seen = 0;
  while (!stack.empty() && seen < 100000) {
    uint32_t j = stack.back();
    stack.pop_back();
    for (; j != 0 && m.ok(j, 0x60) && seen < 100000; j = m.u32(j + 0x10)) {
      seen++;
      const std::string name = m.str(m.u32(j));
      if (name == "S_ie_04_kmnC") {
        f.kmn = j;
      } else if (name == "S_i04_kabiC") {
        f.kabi = j;
      } else if (name == "pPlane136_q00605") {
        f.mark = j;
      } else if (name == "pPlane359_q00647" || name == "pc_chalk_outline") {
        f.already = true;
      }
      const uint32_t parts = m.u32(j + 0x5C);
      for (uint32_t p = 0; p < parts && p < 64 && m.ok(j + 0x60 + p * 8, 8); p++) {
        const uint32_t mat = m.u32(j + 0x60 + p * 8);
        if (mat != 0 && f.lambert == 0 && m.str(m.u32(mat)) == "lambert1_v") {
          f.lambert = mat;
        }
      }
      if (m.u32(j + 0xC) != 0) {
        stack.push_back(m.u32(j + 0xC));
      }
    }
  }
}

struct Vtx {
  float x, z;
  int clr;
  int tex; // -1: none
};

} // namespace

bool portChalkOutlineRoom(const std::vector<uint8_t>& room, std::vector<uint8_t>& out, std::string& why) {
  Map m(room);
  if (room.size() < kHeader || rd32(room, 0) != room.size()) {
    why = "this disc's room couldn't be read";
    return false;
  }
  m.dataSize = rd32(room, 4);
  m.relCount = rd32(room, 8);
  const uint32_t chunkCount = rd32(room, 0xC);
  if (uint64_t(kHeader) + m.dataSize + uint64_t(m.relCount) * 4 + uint64_t(chunkCount) * 8 > room.size()) {
    why = "this disc's room isn't a map file";
    return false;
  }
  const uint32_t relAt = kHeader + m.dataSize, tailAt = relAt + m.relCount * 4;
  // the chunks by name (their names: after the chunk and extern tables)
  const uint32_t externCount = rd32(room, 0x10);
  const uint64_t strAt = uint64_t(tailAt) + uint64_t(chunkCount) * 8 + uint64_t(externCount) * 8;
  uint32_t info = UINT32_MAX, vcd = UINT32_MAX;
  for (uint32_t k = 0; k < chunkCount && strAt < room.size(); k++) {
    const uint32_t off = rd32(room, tailAt + k * 8);
    const uint64_t name = strAt + rd32(room, tailAt + k * 8 + 4);
    std::string s;
    for (uint64_t i = name; i < room.size() && room[i] != 0 && s.size() < 32; i++) {
      s += char(room[i]);
    }
    if (s == "information") {
      info = off;
    } else if (s == "vcd_table") {
      vcd = off;
    }
  }
  if (info == UINT32_MAX || vcd == UINT32_MAX || !m.ok(vcd, 0x50)) {
    why = "this disc's room isn't a map file";
    return false;
  }
  Found f;
  findJoints(m, m.u32(info + 4), f);
  if (f.already) {
    why = "this disc's room has it already";
    return false;
  }
  if (f.kmn == 0 || f.kabi == 0 || f.mark == 0 || f.lambert == 0 || m.u32(f.mark + 0x5C) < 1) {
    why = "this disc's room isn't the one the outline is for";
    return false;
  }
  const uint32_t typeName = m.u32(f.mark + 4), stainDraw = m.u32(f.mark + 0x58), stainMat = m.u32(f.mark + 0x60);
  const uint32_t posArr = m.u32(vcd), nrmArr = m.u32(vcd + 4), clrArr = m.u32(vcd + 0xC), texArr = m.u32(vcd + 0x18);
  const int posFrac = int(m.u32(vcd + 0x44)), texFrac = int(m.u32(vcd + 0x48));
  if (posArr == 0 || nrmArr == 0 || clrArr == 0 || texArr == 0 || posFrac < 0 || posFrac > 14 || texFrac < 0 ||
      texFrac > 14 || stainMat == 0 || stainDraw == 0) {
    why = "this disc's room isn't the one the outline is for";
    return false;
  }
  const uint32_t posCount = m.u32(posArr), nrmCount = m.u32(nrmArr), clrCount = m.u32(clrArr), texCount = m.u32(texArr);
  if (!m.ok(posArr + 4, posCount * 6) || !m.ok(nrmArr + 4, nrmCount * 3) || !m.ok(clrArr + 4, clrCount * 4) ||
      !m.ok(texArr + 4, texCount * 4)) {
    why = "this disc's room isn't a map file";
    return false;
  }
  // a normal straight up (64 is 1.0 in the map's S8 normals)
  uint32_t up = UINT32_MAX;
  for (uint32_t i = 0; i < nrmCount && up == UINT32_MAX; i++) {
    const uint8_t* n = &room[kHeader + nrmArr + 4 + i * 3];
    if (n[0] == 0 && n[1] == 64 && n[2] == 0) {
      up = i;
    }
  }
  if (up == UINT32_MAX) {
    why = "this disc's room isn't the one the outline is for";
    return false;
  }

  // ---- the outline's strip, in the joint's own units ----
  const std::vector<P2> poly = outline(why);
  if (poly.empty()) {
    return false;
  }
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const P2& p : poly) {
    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
  }
  const double unit = kOutlineSize / std::max(x1 - x0, y1 - y0), mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
  // the image's down is the floor's +z: toward the camera, so the head lies away from it
  std::vector<P2> pts;
  for (const P2& p : poly) {
    pts.push_back({(p.x - mx) * unit, (p.y - my) * unit});
  }
  const size_t n = pts.size();
  std::vector<Vtx> outlineV;
  for (size_t i = 0; i <= n; i++) {
    const P2& a = pts[(i + n - 1) % n];
    const P2& b = pts[i % n];
    const P2& c = pts[(i + 1) % n];
    auto normal = [](const P2& p, const P2& q) {
      const double dx = q.x - p.x, dy = q.y - p.y, l = std::max(std::hypot(dx, dy), 1e-9);
      return P2{dy / l, -dx / l};
    };
    const P2 n0 = normal(a, b), n1 = normal(b, c);
    P2 mid{n0.x + n1.x, n0.y + n1.y};
    double ml = std::hypot(mid.x, mid.y);
    if (ml < 1e-6) {
      mid = n1;
      ml = 1;
    }
    mid = {mid.x / ml, mid.y / ml};
    // a mitred corner, no longer than twice the strip's half width
    const double k = kOutlineHalfWidth / std::max(mid.x * n1.x + mid.y * n1.y, 0.5);
    outlineV.push_back({float(b.x + mid.x * k), float(b.y + mid.y * k), 0, -1});
    outlineV.push_back({float(b.x - mid.x * k), float(b.y - mid.y * k), 0, -1});
  }

  // ---- the new map data ----
  std::vector<uint8_t> data(room.begin() + kHeader, room.begin() + kHeader + m.dataSize);
  std::vector<uint32_t> rel;
  for (uint32_t k = 0; k < m.relCount; k++) {
    rel.push_back(rd32(room, relAt + k * 4));
  }
  auto ptr = [&](uint32_t at, uint32_t to) {
    wr32(data, at, to);
    if (to != 0) {
      rel.push_back(at);
    }
  };
  const float posScale = float(1 << posFrac), texScale = float(1 << texFrac);
  auto q16 = [](float v, float scale, bool& fits) {
    const long q = std::lround(v * scale);
    fits = fits && q >= -32768 && q <= 32767;
    return uint16_t(int16_t(std::clamp(q, -32768L, 32767L)));
  };
  bool fits = true;

  // the vertex arrays, copied with the new vertices after the old
  std::vector<std::pair<float, float>> newPos; // x, z (y 0: the joints lie on the floor)
  std::vector<uint32_t> newClr{uint32_t(kChalk[0]) << 24 | uint32_t(kChalk[1]) << 16 | uint32_t(kChalk[2]) << 8 | kChalk[3]};
  std::vector<std::pair<float, float>> newTex;
  for (Vtx& v : outlineV) {
    newPos.push_back({v.x, v.z});
  }
  std::vector<std::vector<uint32_t>> stainIdx; // per stain: pos, clr, tex index of each corner
  for (const Stain& s : kStains) {
    std::vector<uint32_t> idx;
    for (const Corner& c : s.c) {
      newPos.push_back({c.x, c.z});
      auto it = std::find(newClr.begin(), newClr.end(), c.rgba);
      if (it == newClr.end()) {
        newClr.push_back(c.rgba);
        it = newClr.end() - 1;
      }
      newTex.push_back({c.s, c.t});
      idx.push_back(posCount + uint32_t(newPos.size() - 1));
      idx.push_back(clrCount + uint32_t(it - newClr.begin()));
      idx.push_back(texCount + uint32_t(newTex.size() - 1));
    }
    stainIdx.push_back(idx);
  }
  if (posCount + newPos.size() > 0xFFFF || clrCount + newClr.size() > 0xFFFF || texCount + newTex.size() > 0xFFFF) {
    why = "this disc's room has no room for it";
    return false;
  }
  align(data, 32);
  const uint32_t newPosArr = uint32_t(data.size());
  put32(data, posCount + uint32_t(newPos.size()));
  data.insert(data.end(), room.begin() + kHeader + posArr + 4, room.begin() + kHeader + posArr + 4 + posCount * 6);
  for (const auto& [x, z] : newPos) {
    put16(data, q16(x, posScale, fits));
    put16(data, 0);
    put16(data, q16(z, posScale, fits));
  }
  align(data, 32);
  const uint32_t newClrArr = uint32_t(data.size());
  put32(data, clrCount + uint32_t(newClr.size()));
  data.insert(data.end(), room.begin() + kHeader + clrArr + 4, room.begin() + kHeader + clrArr + 4 + clrCount * 4);
  for (uint32_t c : newClr) {
    put32(data, c);
  }
  align(data, 32);
  const uint32_t newTexArr = uint32_t(data.size());
  put32(data, texCount + uint32_t(newTex.size()));
  data.insert(data.end(), room.begin() + kHeader + texArr + 4, room.begin() + kHeader + texArr + 4 + texCount * 4);
  for (const auto& [s, t] : newTex) {
    put16(data, q16(s, texScale, fits));
    put16(data, q16(t, texScale, fits));
  }
  if (!fits) {
    why = "its shape doesn't fit the room's vertex format";
    return false;
  }
  ptr(vcd + 0x0, newPosArr);
  ptr(vcd + 0xC, newClrArr);
  ptr(vcd + 0x18, newTexArr);

  // a packed display list (a triangle strip of the map's own vertex format), 32-byte blocks
  auto displayList = [&](const std::vector<std::vector<uint16_t>>& verts, uint32_t& size) {
    align(data, 32);
    const uint32_t at = uint32_t(data.size());
    data.push_back(0x98); // GX_TRIANGLESTRIP, format 0
    put16(data, uint16_t(verts.size()));
    for (const auto& v : verts) {
      for (uint16_t i : v) {
        put16(data, i);
      }
    }
    align(data, 32);
    size = uint32_t(data.size()) - at;
    return at;
  };
  auto mesh = [&](uint32_t mask, uint32_t list, uint32_t size) {
    align(data, 8);
    const uint32_t at = uint32_t(data.size());
    put32(data, 1); // packed
    put32(data, 1); // one display list
    put32(data, mask);
    put32(data, 0);
    put32(data, 0);
    put32(data, size);
    ptr(at + 0xC, vcd);
    ptr(at + 0x10, list);
    return at;
  };
  auto text = [&](const char* s) {
    const uint32_t at = uint32_t(data.size());
    data.insert(data.end(), s, s + std::strlen(s) + 1);
    return at;
  };
  // a joint of one part, after `after` among parent's children (0: the first)
  auto joint = [&](uint32_t name, uint32_t parent, uint32_t after, const float s[3], const float r[3], const float t[3],
                   const std::vector<std::pair<float, float>>& extent, uint32_t draw, uint32_t mat, uint32_t msh) {
    align(data, 8);
    const uint32_t at = uint32_t(data.size());
    data.resize(data.size() + 0x68, 0);
    float lo[3] = {1e9f, 0, 1e9f}, hi[3] = {-1e9f, 0, -1e9f};
    for (const auto& [x, z] : extent) {
      lo[0] = std::min(lo[0], x), hi[0] = std::max(hi[0], x), lo[2] = std::min(lo[2], z), hi[2] = std::max(hi[2], z);
    }
    auto vec = [&](uint32_t off, const float* v) {
      for (int i = 0; i < 3; i++) {
        uint32_t w;
        std::memcpy(&w, &v[i], 4);
        wr32(data, at + off + i * 4, w);
      }
    };
    vec(0x18, s);
    vec(0x24, r);
    vec(0x30, t);
    vec(0x3C, lo);
    vec(0x48, hi);
    ptr(at + 0x0, name);
    ptr(at + 0x4, typeName);
    ptr(at + 0x8, parent);
    ptr(at + 0x58, draw);
    wr32(data, at + 0x5C, 1);
    ptr(at + 0x60, mat);
    ptr(at + 0x64, msh);
    const uint32_t next = after != 0 ? rd32(data, after + 0x10) : rd32(data, parent + 0xC);
    ptr(at + 0x10, next);
    ptr(at + 0x14, after);
    if (after != 0) {
      ptr(after + 0x10, at);
    } else {
      ptr(parent + 0xC, at);
    }
    if (next != 0) {
      ptr(next + 0x14, at);
    }
    return at;
  };

  // the outline: untextured (position, normal, colour), drawn from both sides, opaque
  {
    std::vector<std::vector<uint16_t>> verts;
    std::vector<std::pair<float, float>> extent;
    for (size_t i = 0; i < outlineV.size(); i++) {
      verts.push_back({uint16_t(posCount + i), uint16_t(up), uint16_t(clrCount)});
      extent.push_back({outlineV[i].x, outlineV[i].z});
    }
    uint32_t size = 0;
    const uint32_t list = displayList(verts, size);
    const uint32_t msh = mesh(0x7, list, size);
    align(data, 4);
    const uint32_t draw = uint32_t(data.size());
    data.insert(data.end(), {0, 3, 0, 0}); // blend 0 (opaque), no culling
    put32(data, 0);                        // no lights
    put32(data, 0);                        // no hit attributes
    put32(data, 0);
    const uint32_t name = text("pc_chalk_outline");
    const float s[3] = {1, 1, 1}, r[3] = {0, kOutlineTurn, 0}, t[3] = {kOutlineX, kOutlineY, kOutlineZ};
    joint(name, f.kmn, 0, s, r, t, extent, draw, f.lambert, msh);
  }
  // the stains, each twice (as the Japanese map has them), after the one the US kept
  uint32_t after = f.mark;
  for (int pass = 0; pass < 2; pass++) {
    for (size_t k = 0; k < 2; k++) {
      std::vector<std::vector<uint16_t>> verts;
      std::vector<std::pair<float, float>> extent;
      for (int c = 0; c < 4; c++) {
        const uint32_t* i = &stainIdx[k][c * 3];
        verts.push_back({uint16_t(i[0]), uint16_t(up), uint16_t(i[1]), uint16_t(i[2])});
        extent.push_back({kStains[k].c[c].x, kStains[k].c[c].z});
      }
      uint32_t size = 0;
      const uint32_t list = displayList(verts, size);
      const uint32_t msh = mesh(0x17, list, size);
      char label[32];
      std::snprintf(label, sizeof(label), "pc_chalk_mark_%d", int(pass * 2 + k + 1));
      const uint32_t name = text(label);
      const float s[3] = {kStainScale, kStainScale, kStainScale}, r[3] = {0, kStainTurn, 0},
                  t[3] = {kStains[k].tx, kStainY, kStains[k].tz};
      after = joint(name, f.kabi, after, s, r, t, extent, stainDraw, stainMat, msh);
    }
  }
  align(data, 32);

  // ---- the file: header, data, relocations, then the chunk and extern tables and names as they were ----
  std::sort(rel.begin(), rel.end());
  rel.erase(std::unique(rel.begin(), rel.end()), rel.end());
  out.clear();
  out.insert(out.end(), room.begin(), room.begin() + kHeader);
  out.insert(out.end(), data.begin(), data.end());
  for (uint32_t r : rel) {
    put32(out, r);
  }
  out.insert(out.end(), room.begin() + tailAt, room.end());
  wr32(out, 0, uint32_t(out.size()));
  wr32(out, 4, uint32_t(data.size()));
  wr32(out, 8, uint32_t(rel.size()));
  return true;
}
