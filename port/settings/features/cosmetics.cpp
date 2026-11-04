// Cosmetics (a tab of its own): Mario's cap and shirt and his overalls, the
// partners' colours and the status bar's banners in colours of the player's
// choosing. Each is the game's own texture recoloured as it is drawn: the
// texels the game loaded (port/include/port/cosmetics.h) decoded, and where
// a pixel has the part's colour (its hue, saturation and lightness near the
// original's), moved to the new colour with its shading kept, then given to
// Aurora as that texture's replacement by its address (as button_prompts.cpp
// does). Nothing the game reads is changed, so a game played together plays
// out the same whatever each player picked; a change shows at once.
//
// Mario's emblem looks (the L and W emblems' own texture files) stay as they
// are, and so do his gloves and shoes: their white and brown are the same as
// his eyes' and his hair's, so no mask tells them apart. The partners: Koops's
// shell and hoodie, Flurrie's hair (her dress is her body's own lavender, her
// face's and hands' too), Bobbery's body (his fuse is a few grey texels, the
// same as his cap's) and Ms. Mowz's mask and cape (one red); Yoshi's colour is
// the game's own choice, changed in the Wardrobe (wardrobe_aurora.c).
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/texture.hpp>

#include "port/cosmetics.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// ------------------------------------------------------------- the palette

struct Named {
  const char* name;
  uint32_t rgb;
};
// 0 the game's own colour; the last, the slot's own colour from the picker
constexpr Named kPalette[] = {
    {"Original", 0},
    {"Red", 0xE62814},
    {"Luigi green", 0x28AA32},
    {"Wario yellow", 0xFAD21E},
    {"Waluigi purple", 0x6E2896},
    {"White", 0xEBEBEB},
    {"Blue", 0x2846E6},
    {"Navy", 0x1E2878},
    {"Sky blue", 0x5AAAF0},
    {"Pink", 0xF56EB4},
    {"Orange", 0xF58214},
    {"Black", 0x282830},
    {"Gold", 0xD7A528},
    {"Brown", 0x824B23},
    {"Custom (the colour picker)", 0},
};
constexpr int kColours = int(sizeof(kPalette) / sizeof(kPalette[0]));
constexpr int kCustom = kColours - 1;
const char* kChoiceNames[kColours];

// ------------------------------------------------------------------ slots

// (the order is the settings' and the Wardrobe's, PORT_COSMETIC_*: new ones at the end)
enum SlotId {
  kCap,
  kOveralls,
  kPartner,
  kHudHp,
  kHudFp,
  kHudSp,
  kKoopsShell,
  kKoopsHoodie,
  kFlurrieHair,
  kBobberyBody,
  kMowzMask,
  kVivian, // (Goombella's and hers were one slot, kPartner: hers starts as it was)
  kSlots
};

struct Slot {
  const char* key;
  const char* rgbKey;
  const char* label;
  const char* note;
  const char* keywords;
  uint32_t defaultCustom;
  int* choice;
  int* rgb;
};
Slot sSlots[kSlots] = {
    {"cosmetic_cap", "cosmetic_cap_rgb", "Mario's cap and shirt", nullptr, "mario hat cap shirt colour color red",
     0xE62814},
    {"cosmetic_overalls", "cosmetic_overalls_rgb", "Mario's overalls", nullptr, "mario overalls colour color blue",
     0x2846E6},
    {"cosmetic_partner", "cosmetic_partner_rgb", "Goombella's hair", nullptr, "partner goombella hair colour color",
     0xFAD21E},
    {"cosmetic_hud_hp", "cosmetic_hud_hp_rgb", "HP banner", "Mario's and the partner's.",
     "hud status bar hp heart banner colour color", 0xFF8C96},
    {"cosmetic_hud_fp", "cosmetic_hud_fp_rgb", "FP banner", nullptr, "hud status bar fp flower banner colour color",
     0xE18C1E},
    {"cosmetic_hud_sp", "cosmetic_hud_sp_rgb", "Star Power banner", nullptr,
     "hud status bar star power sp banner colour color", 0x6EA5D2},
    {"cosmetic_koops_shell", "cosmetic_koops_shell_rgb", "Koops's shell", nullptr,
     "partner koops shell colour color green", 0x14A01E},
    {"cosmetic_koops_hoodie", "cosmetic_koops_hoodie_rgb", "Koops's hoodie", nullptr,
     "partner koops hoodie colour color blue", 0x94B2D1},
    {"cosmetic_flurrie_hair", "cosmetic_flurrie_hair_rgb", "Flurrie's hair", nullptr,
     "partner flurrie hair colour color purple", 0x8B76DA},
    {"cosmetic_bobbery_body", "cosmetic_bobbery_body_rgb", "Bobbery's body", nullptr,
     "partner bobbery body bomb colour color brown", 0x593F22},
    {"cosmetic_mowz_mask", "cosmetic_mowz_mask_rgb", "Ms. Mowz's mask and cape", nullptr,
     "partner ms mowz mask cape colour color red", 0xCC2200},
    {"cosmetic_vivian", "cosmetic_vivian_rgb", "Vivian's body", nullptr, "partner vivian body colour color purple",
     0x8C2D8C},
};

// Vivian's slot, until it has a value of its own (-1): the one she shared
// with Goombella, so a colour picked before keeps showing
void settle() {
  Slot& v = sSlots[kVivian];
  if (v.choice != nullptr && *v.choice < 0) {
    *v.choice = *sSlots[kPartner].choice;
  }
  if (v.rgb != nullptr && *v.rgb < 0) {
    *v.rgb = *sSlots[kPartner].rgb;
  }
}

// the colour a slot asks for now, or -1: the game's own
int64_t slotColour(int s) {
  settle();
  const int c = *sSlots[s].choice;
  if (c <= 0 || c >= kColours) {
    return -1;
  }
  return c == kCustom ? int64_t(*sSlots[s].rgb & 0xFFFFFF) : int64_t(kPalette[c].rgb);
}

// ---------------------------------------------------------------- colour

struct Hsl {
  float h, s, l; // h in degrees
};

Hsl toHsl(float r, float g, float b) {
  const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
  Hsl o{0.f, 0.f, (mx + mn) * 0.5f};
  const float d = mx - mn;
  if (d > 1e-5f) {
    o.s = o.l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r) {
      o.h = (g - b) / d + (g < b ? 6.f : 0.f);
    } else if (mx == g) {
      o.h = (b - r) / d + 2.f;
    } else {
      o.h = (r - g) / d + 4.f;
    }
    o.h *= 60.f;
  }
  return o;
}

float hueToRgb(float p, float q, float t) {
  t -= std::floor(t);
  if (t < 1.f / 6.f) {
    return p + (q - p) * 6.f * t;
  }
  if (t < 0.5f) {
    return q;
  }
  if (t < 2.f / 3.f) {
    return p + (q - p) * (2.f / 3.f - t) * 6.f;
  }
  return p;
}

void toRgb(const Hsl& c, float* rgb) {
  if (c.s <= 1e-5f) {
    rgb[0] = rgb[1] = rgb[2] = c.l;
    return;
  }
  const float q = c.l < 0.5f ? c.l * (1.f + c.s) : c.l + c.s - c.l * c.s;
  const float p = 2.f * c.l - q;
  const float h = c.h / 360.f;
  rgb[0] = hueToRgb(p, q, h + 1.f / 3.f);
  rgb[1] = hueToRgb(p, q, h);
  rgb[2] = hueToRgb(p, q, h - 1.f / 3.f);
}

// a: the signed distance between two hues, -180..180
float hueDelta(float a, float b) {
  float d = std::fmod(a - b, 360.f);
  if (d > 180.f) {
    d -= 360.f;
  } else if (d < -180.f) {
    d += 360.f;
  }
  return d;
}

float ramp(float x, float from, float to) { return std::clamp((x - from) / (to - from), 0.f, 1.f); }

// A part of a texture by its colour: the hues within half (fading out over
// soft more), saturated and light enough, and no lighter than lMax (fading
// out over 0.04: a lighter part of the same hue left alone); ref is its usual
// colour, the one a new colour stands in for (the shading around it moves
// with it). only: the textures it is in, by their index in the file (bit i),
// where another part elsewhere in the file has its colour; 0 all of them.
struct Region {
  int slot;
  float hue, half, soft;
  float satMin, lMin;
  Hsl ref;
  float lMax = 1.f;
  uint64_t only = 0;
};

float regionWeight(const Region& r, const Hsl& p) {
  const float dh = std::fabs(hueDelta(p.h, r.hue));
  const float wh = 1.f - ramp(dh, r.half, r.half + r.soft);
  if (wh <= 0.f) {
    return 0.f;
  }
  const float wl = r.lMax < 1.f ? 1.f - ramp(p.l, r.lMax, r.lMax + 0.04f) : 1.f;
  return wh * wl * ramp(p.s, r.satMin - 0.1f, r.satMin) * ramp(p.l, r.lMin - 0.06f, r.lMin);
}

// p moved from r.ref to the colour to: the hue turned by the same amount,
// saturation scaled, lightness offset (its shading's steps kept, a little
// flatter)
Hsl shift(const Region& r, const Hsl& p, const Hsl& to) {
  Hsl o;
  o.h = to.h + hueDelta(p.h, r.ref.h) * 0.5f;
  o.s = std::clamp(to.s * (p.s / r.ref.s), 0.f, 1.f);
  o.l = std::clamp(to.l + (p.l - r.ref.l) * 0.8f, 0.f, 1.f);
  return o;
}

// ---------------------------------------------------------- GX decoding

inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

inline void rgb565(uint16_t v, uint8_t* o) {
  const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  o[0] = uint8_t(r << 3 | r >> 2);
  o[1] = uint8_t(g << 2 | g >> 4);
  o[2] = uint8_t(b << 3 | b >> 2);
  o[3] = 255;
}

inline void rgb5a3(uint16_t v, uint8_t* o) {
  if (v & 0x8000) {
    const int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
    o[0] = uint8_t(r << 3 | r >> 2);
    o[1] = uint8_t(g << 3 | g >> 2);
    o[2] = uint8_t(b << 3 | b >> 2);
    o[3] = 255;
  } else {
    const int a = (v >> 12) & 7;
    o[0] = uint8_t(((v >> 8) & 15) * 17);
    o[1] = uint8_t(((v >> 4) & 15) * 17);
    o[2] = uint8_t((v & 15) * 17);
    o[3] = uint8_t(a << 5 | a << 2 | a >> 1);
  }
}

void paletteEntry(const PortTexInfo& t, unsigned i, uint8_t* o) {
  if (t.tlut == nullptr || i >= unsigned(t.tlutCount)) {
    o[0] = o[1] = o[2] = o[3] = 0;
    return;
  }
  const uint16_t v = be16(static_cast<const uint8_t*>(t.tlut) + i * 2);
  switch (t.tlutFormat) {
  case 0: // IA8
    o[0] = o[1] = o[2] = uint8_t(v & 0xFF);
    o[3] = uint8_t(v >> 8);
    break;
  case 1: rgb565(v, o); break;
  default: rgb5a3(v, o); break;
  }
}

// t's level 0 as RGBA8 rows; false for a format not read here
bool decode(const PortTexInfo& t, std::vector<uint8_t>& out) {
  const int w = t.width, h = t.height;
  if (t.data == nullptr || w <= 0 || h <= 0 || w > 1024 || h > 1024) {
    return false;
  }
  out.assign(size_t(w) * h * 4, 0);
  const uint8_t* src = static_cast<const uint8_t*>(t.data);
  auto px = [&](int x, int y) -> uint8_t* { return (x < w && y < h) ? &out[(size_t(y) * w + x) * 4] : nullptr; };
  int bw, bh;
  switch (t.format) {
  case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: bw = 8; bh = 8; break;
  case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: bw = 8; bh = 4; break;
  case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_RGBA8: case GX_TF_C14X2: bw = 4; bh = 4; break;
  default: return false;
  }
  for (int by = 0; by < h; by += bh) {
    for (int bx = 0; bx < w; bx += bw) {
      switch (t.format) {
      case GX_TF_CMPR:
        for (int sub = 0; sub < 4; sub++) {
          const uint8_t* b = src;
          src += 8;
          uint8_t pal[4][4];
          const uint16_t c0 = be16(b), c1 = be16(b + 2);
          rgb565(c0, pal[0]);
          rgb565(c1, pal[1]);
          for (int k = 0; k < 3; k++) {
            if (c0 > c1) {
              pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
              pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
            } else {
              pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
              pal[3][k] = 0;
            }
          }
          pal[2][3] = 255;
          pal[3][3] = c0 > c1 ? 255 : 0;
          const int ox = bx + (sub & 1) * 4, oy = by + (sub >> 1) * 4;
          for (int y = 0; y < 4; y++) {
            const uint8_t bits = b[4 + y];
            for (int x = 0; x < 4; x++) {
              if (uint8_t* o = px(ox + x, oy + y)) {
                std::memcpy(o, pal[(bits >> (6 - 2 * x)) & 3], 4);
              }
            }
          }
        }
        break;
      case GX_TF_RGBA8:
        for (int i = 0; i < 16; i++) {
          if (uint8_t* o = px(bx + (i & 3), by + (i >> 2))) {
            o[3] = src[i * 2];
            o[0] = src[i * 2 + 1];
            o[1] = src[32 + i * 2];
            o[2] = src[32 + i * 2 + 1];
          }
        }
        src += 64;
        break;
      default:
        for (int y = 0; y < bh; y++) {
          for (int x = 0; x < bw; x++) {
            uint8_t* o = px(bx + x, by + y);
            uint8_t tmp[4];
            if (o == nullptr) {
              o = tmp;
            }
            switch (t.format) {
            case GX_TF_I4: {
              const uint8_t v = src[(y * bw + x) / 2];
              const uint8_t i = uint8_t(((x & 1) ? v & 15 : v >> 4) * 17);
              o[0] = o[1] = o[2] = o[3] = i;
              break;
            }
            case GX_TF_C4: {
              const uint8_t v = src[(y * bw + x) / 2];
              paletteEntry(t, (x & 1) ? v & 15 : v >> 4, o);
              break;
            }
            case GX_TF_I8: o[0] = o[1] = o[2] = o[3] = src[y * bw + x]; break;
            case GX_TF_IA4: {
              const uint8_t v = src[y * bw + x];
              o[0] = o[1] = o[2] = uint8_t((v & 15) * 17);
              o[3] = uint8_t((v >> 4) * 17);
              break;
            }
            case GX_TF_C8: paletteEntry(t, src[y * bw + x], o); break;
            case GX_TF_IA8: {
              const uint8_t* v = src + (y * bw + x) * 2;
              o[0] = o[1] = o[2] = v[1];
              o[3] = v[0];
              break;
            }
            case GX_TF_RGB565: rgb565(be16(src + (y * bw + x) * 2), o); break;
            case GX_TF_RGB5A3: rgb5a3(be16(src + (y * bw + x) * 2), o); break;
            case GX_TF_C14X2: paletteEntry(t, be16(src + (y * bw + x) * 2) & 0x3FFF, o); break;
            default: break;
            }
          }
        }
        src += 32; // (every block of these is 32 bytes)
        break;
      }
    }
  }
  return true;
}

// ------------------------------------------------------------- the sets

// Where a set's textures come from: an animation texture file, or a status
// bar icon (portHudIconTextures's part)
struct Set {
  const char* file; // nullptr: hud
  int hudPart;
  std::vector<Region> regions;

  struct Tex {
    std::vector<uint8_t> pixels;
    aurora::texture::ReplacementRegistration reg;
  };
  std::vector<Tex> applied;
  std::string key; // the colours and addresses applied ("": none)
};

constexpr Hsl kMarioRed{10.f, 0.87f, 0.50f};
constexpr Hsl kMarioBlue{233.f, 1.0f, 0.56f};
constexpr Hsl kGoombellaHair{40.f, 0.85f, 0.50f};
constexpr Hsl kVivianPurple{290.f, 0.60f, 0.35f};
constexpr Hsl kKoopsGreen{125.f, 1.0f, 0.30f};
constexpr Hsl kKoopsBlue{212.f, 0.40f, 0.70f};
constexpr Hsl kFlurriePurple{250.f, 0.58f, 0.66f};
constexpr Hsl kBobberyBrown{33.f, 0.45f, 0.24f};
constexpr Hsl kMowzRed{10.f, 1.0f, 0.40f};

std::vector<Set> makeSets() {
  // Mario: the red of his cap and shirt (not the orange of his skin, the
  // browns of his hair and shoes) and the blue of his overalls (not his
  // gloves' grey shading); field, back views, battle and scenes (b_mario and
  // e_mario use a_mario's textures)
  const Region cap{kCap, 4.f, 12.f, 5.f, 0.45f, 0.28f, kMarioRed};
  // (and the cap's highlights, more orange: as light as no shoe is)
  const Region capLight{kCap, 19.5f, 5.5f, 3.f, 0.6f, 0.47f, kMarioRed};
  const Region overalls{kOveralls, 228.f, 22.f, 8.f, 0.35f, 0.12f, kMarioBlue};
  const Region hair{kPartner, 40.f, 9.f, 5.f, 0.55f, 0.25f, kGoombellaHair};
  const Region vivian{kVivian, 290.f, 22.f, 8.f, 0.30f, 0.10f, kVivianPurple};
  // The partners (each measured over all of its file's textures, the parts
  // around it left as they are): Koops's green shell, and his hoodie's pale
  // blue (not his shorts' deep blue or the sweat drop's cyan)
  const Region koopsShell{kKoopsShell, 125.f, 20.f, 8.f, 0.4f, 0.08f, kKoopsGreen};
  const Region koopsHoodie{kKoopsHoodie, 212.f, 7.f, 5.f, 0.2f, 0.5f, kKoopsBlue};
  // Flurrie's hair: her lavender's hue, but deeper than her body (her face,
  // arms and cloud are lighter and greyer); its highlights more violet
  const Region flurrieHair{kFlurrieHair, 252.f, 6.f, 2.f, 0.42f, 0.45f, kFlurriePurple, 0.70f};
  const Region flurrieShine{kFlurrieHair, 255.f, 6.f, 2.f, 0.58f, 0.66f, kFlurriePurple, 0.76f};
  // Bobbery's body: the bomb's brown, in his face's texture and his body's
  // (4 and 32; the others' wheel and shoe soles are browns of its hue)
  const Region bobberyBody{kBobberyBody, 31.f, 7.f, 2.f, 0.3f, 0.06f, kBobberyBrown, 0.5f,
                           uint64_t(1) << 4 | uint64_t(1) << 32};
  // Ms. Mowz's mask, its eye holes and her cape: the red, not her ears' pink
  const Region mowzMask{kMowzMask, 10.f, 6.f, 3.f, 0.8f, 0.24f, kMowzRed, 0.52f};
  std::vector<Set> sets;
  sets.push_back(Set{"a/a_mario-", -1, {cap, capLight, overalls}, {}, {}});
  sets.push_back(Set{"a/a_mario_r-", -1, {cap, capLight, overalls}, {}, {}});
  sets.push_back(Set{"a/c_pkr-", -1, {hair}, {}, {}});
  sets.push_back(Set{"a/c_vivian-", -1, {vivian}, {}, {}});
  sets.push_back(Set{"a/c_pnk-", -1, {koopsShell, koopsHoodie}, {}, {}});
  sets.push_back(Set{"a/c_windy-", -1, {flurrieHair, flurrieShine}, {}, {}});
  sets.push_back(Set{"a/c_bomt_n-", -1, {bobberyBody}, {}, {}});
  sets.push_back(Set{"a/c_tyutyu-", -1, {mowzMask}, {}, {}});
  // the banners: all of their colour (the outline is grey)
  sets.push_back(Set{nullptr, 0, {{kHudHp, 355.f, 40.f, 15.f, 0.2f, 0.f, Hsl{355.f, 1.0f, 0.78f}}}, {}, {}});
  sets.push_back(Set{nullptr, 1, {{kHudFp, 30.f, 40.f, 15.f, 0.2f, 0.f, Hsl{32.f, 0.9f, 0.50f}}}, {}, {}});
  sets.push_back(Set{nullptr, 2, {{kHudSp, 205.f, 40.f, 15.f, 0.2f, 0.f, Hsl{205.f, 0.5f, 0.63f}}}, {}, {}});
  // the status bar's faces as their models
  sets.push_back(Set{nullptr, 3, {cap, capLight}, {}, {}});
  sets.push_back(Set{nullptr, 4, {hair}, {}, {}});
  return sets;
}

std::vector<Set> sSets;

void clearSet(Set& s) {
  for (auto& t : s.applied) {
    if (t.reg.id != 0) {
      aurora::texture::unregister_replacement(t.reg);
    }
  }
  s.applied.clear();
  s.key.clear();
}

// one texture recoloured by the set's regions, or false: nothing of it changes
bool recolour(const Set& s, int index, const PortTexInfo& t, const int64_t* colours, std::vector<uint8_t>& px) {
  bool in = false;
  for (const Region& r : s.regions) {
    in |= colours[r.slot] >= 0 && (r.only == 0 || (index < 64 && (r.only >> index & 1) != 0));
  }
  if (!in || !decode(t, px)) {
    return false;
  }
  Hsl to[kSlots];
  for (int i = 0; i < kSlots; i++) {
    if (colours[i] >= 0) {
      const uint32_t c = uint32_t(colours[i]);
      to[i] = toHsl(float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f, float(c & 255) / 255.f);
    }
  }
  bool any = false;
  for (size_t i = 0; i < px.size(); i += 4) {
    uint8_t* p = &px[i];
    if (p[3] == 0) {
      continue;
    }
    const float rgb[3] = {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f};
    const Hsl c = toHsl(rgb[0], rgb[1], rgb[2]);
    for (const Region& r : s.regions) {
      if (colours[r.slot] < 0 || (r.only != 0 && (index >= 64 || (r.only >> index & 1) == 0))) {
        continue;
      }
      const float w = regionWeight(r, c);
      if (w <= 0.f) {
        continue;
      }
      float out[3];
      toRgb(shift(r, c, to[r.slot]), out);
      for (int k = 0; k < 3; k++) {
        p[k] = uint8_t(std::lround(std::clamp(rgb[k] + (out[k] - rgb[k]) * w, 0.f, 1.f) * 255.f));
      }
      any = true;
      break;
    }
  }
  return any;
}

constexpr int kMaxTextures = 1024;
PortTexInfo sInfo[kMaxTextures];

void updateSet(Set& s, const int64_t* colours) {
  bool wanted = false;
  std::string key;
  for (const Region& r : s.regions) {
    if (colours[r.slot] >= 0) {
      wanted = true;
    }
    key += std::to_string(colours[r.slot]) + ",";
  }
  const int n = wanted ? (s.file != nullptr ? portAnimTextures(s.file, sInfo, kMaxTextures)
                                            : portHudIconTextures(s.hudPart, sInfo, kMaxTextures))
                       : 0;
  if (n == 0) {
    if (!s.key.empty()) {
      clearSet(s);
    }
    return;
  }
  // the addresses are part of it: the file loaded again, or moved
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < n; i++) {
    h = (h ^ uint64_t(reinterpret_cast<uintptr_t>(sInfo[i].data))) * 1099511628211ull;
  }
  key += std::to_string(n) + "@" + std::to_string(h);
  if (key == s.key) {
    return;
  }
  clearSet(s);
  s.applied.reserve(size_t(n)); // the pixels stay where Aurora was told they are
  for (int i = 0; i < n; i++) {
    Set::Tex tex;
    if (!recolour(s, i, sInfo[i], colours, tex.pixels)) {
      continue;
    }
    s.applied.push_back(std::move(tex));
    Set::Tex& a = s.applied.back();
    a.reg = aurora::texture::register_replacement(
        aurora::texture::TexturePointerKey{sInfo[i].data},
        aurora::texture::RawTextureReplacement{
            .bytes = a.pixels,
            .width = static_cast<uint32_t>(sInfo[i].width),
            .height = static_cast<uint32_t>(sInfo[i].height),
            .mipCount = 1,
            .gxFormat = GX_TF_RGBA8_PC,
            .label = "Cosmetics",
        },
        aurora::texture::ReplacementOptions{.priority = -1});
  }
  s.key = key;
}

// each frame: the textures can move or load again at any room change; a
// colour being picked is recoloured a few times a second
void tick(int) {
  if (sSets.empty()) {
    sSets = makeSets();
  }
  int64_t colours[kSlots];
  for (int i = 0; i < kSlots; i++) {
    colours[i] = slotColour(i);
  }
  static int64_t sLastColours[kSlots];
  static Uint64 sLastRecolour;
  const Uint64 now = SDL_GetTicks();
  if (std::memcmp(colours, sLastColours, sizeof(colours)) != 0) {
    if (now - sLastRecolour < 120) {
      std::memcpy(colours, sLastColours, sizeof(colours)); // (the next one soon)
    } else {
      std::memcpy(sLastColours, colours, sizeof(colours));
      sLastRecolour = now;
    }
  }
  for (Set& s : sSets) {
    updateSet(s, colours);
  }
}

// ---------------------------------------------------------------- the page

void setAll(const int* choices) {
  for (int i = 0; i < kSlots; i++) {
    *sSlots[i].choice = choices[i];
  }
  portSettingsSave();
}

int paletteIndex(const char* name) {
  for (int i = 0; i < kColours; i++) {
    if (std::strcmp(kPalette[i].name, name) == 0) {
      return i;
    }
  }
  return 0;
}

// the looks of the brothers and the others, in one press (the partner and the
// banners as they are); the first, the game's own (the Start menu's Wardrobe)
struct Preset {
  const char* label;
  const char* cap;
  const char* overalls;
};
constexpr Preset kPresets[] = {{"Original", "Original", "Original"},
                               {"Luigi", "Luigi green", "Navy"},
                               {"Wario", "Wario yellow", "Waluigi purple"},
                               {"Waluigi", "Waluigi purple", "Black"},
                               {"Fire Mario", "White", "Red"}};
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

void section(int) {
  settle();
  for (int i = 1; i < kPresetCount; i++) {
    if (i > 1) {
      ImGui::SameLine();
    }
    if (ImGui::Button(portTrLabel(kPresets[i].label))) {
      *sSlots[kCap].choice = paletteIndex(kPresets[i].cap);
      *sSlots[kOveralls].choice = paletteIndex(kPresets[i].overalls);
      portSettingsSave();
    }
  }
  if (ImGui::Button(portTrLabel("Random"))) {
    static std::mt19937 sRng{std::random_device{}()};
    std::uniform_int_distribution<int> pick(1, kCustom - 1);
    int c[kSlots];
    for (int i = 0; i < kSlots; i++) {
      c[i] = pick(sRng);
    }
    setAll(c);
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Reset all"))) {
    const int c[kSlots] = {};
    setAll(c);
  }
  // each part's own colour (Custom): picking one chooses it
  for (int i = 0; i < kSlots; i++) {
    ImGui::PushID(i);
    const uint32_t v = uint32_t(*sSlots[i].rgb);
    float col[3] = {float((v >> 16) & 255) / 255.f, float((v >> 8) & 255) / 255.f, float(v & 255) / 255.f};
    if (ImGui::ColorEdit3("##rgb", col, ImGuiColorEditFlags_NoInputs)) {
      *sSlots[i].rgb = int(std::lround(col[0] * 255.f)) << 16 | int(std::lround(col[1] * 255.f)) << 8 |
                       int(std::lround(col[2] * 255.f));
      *sSlots[i].choice = kCustom;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      portSettingsSave();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(portTr(sSlots[i].label));
    ImGui::PopID();
  }
}

const bool sAdded = [] {
  for (int i = 0; i < kColours; i++) {
    kChoiceNames[i] = kPalette[i].name;
  }
  for (Slot& s : sSlots) {
    const bool unset = &s == &sSlots[kVivian]; // (settle)
    s.choice = portSettingsDeclare(s.key, unset ? -1 : 0, unset ? -1 : 0, kColours - 1);
    s.rgb = portSettingsDeclare(s.rgbKey, unset ? -1 : int(s.defaultCustom), unset ? -1 : 0, 0xFFFFFF);
    portSettingsAddChoice("Cosmetics", s.label, s.note, 0, s.key, kChoiceNames, kColours, s.keywords);
  }
  portSettingsAddSection("Cosmetics", "Looks and colour picker",
                         "Mario as his brother or his rivals in one press, every colour at random or back to the "
                         "game's own, and each part's Custom colour. Only how things look: each player sees their own.",
                         0, section, "cosmetics skin outfit palette random reset luigi wario waluigi fire colour color");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace

// --- The Start menu's Wardrobe (port/include/port/cosmetics.h)

extern "C" int portCosmeticsSlotCount(void) { return kSlots; }
extern "C" const char* portCosmeticsSlotLabel(int s) { return s >= 0 && s < kSlots ? sSlots[s].label : ""; }
extern "C" const char* portCosmeticsSlotNote(int s) { return s >= 0 && s < kSlots ? sSlots[s].note : nullptr; }
extern "C" int portCosmeticsChoice(int s) {
  settle();
  return s >= 0 && s < kSlots && sSlots[s].choice ? *sSlots[s].choice : 0;
}

extern "C" void portCosmeticsStep(int s, int dir) {
  if (s < 0 || s >= kSlots || sSlots[s].choice == nullptr) {
    return;
  }
  settle();
  *sSlots[s].choice = ((*sSlots[s].choice + dir) % kColours + kColours) % kColours;
  portSettingsSave();
}

extern "C" const char* portCosmeticsColourName(int c) {
  return c == kCustom ? "Custom" : c >= 0 && c < kColours ? kPalette[c].name : "?";
}

extern "C" unsigned portCosmeticsSlotRgb(int s) {
  if (s < 0 || s >= kSlots) {
    return 0;
  }
  const int64_t c = slotColour(s);
  return c >= 0 ? unsigned(c) : sSlots[s].defaultCustom; // (the custom colour starts as the game's own)
}

extern "C" void portCosmeticsSetRgb(int s, unsigned rgb) {
  if (s < 0 || s >= kSlots || sSlots[s].choice == nullptr || sSlots[s].rgb == nullptr) {
    return;
  }
  *sSlots[s].rgb = int(rgb & 0xFFFFFF);
  *sSlots[s].choice = kCustom;
  portSettingsSave();
}

extern "C" int portCosmeticsLookCount(void) { return kPresetCount; }
extern "C" const char* portCosmeticsLookName(int i) { return i >= 0 && i < kPresetCount ? kPresets[i].label : "?"; }

extern "C" int portCosmeticsLook(void) {
  for (int i = 0; i < kPresetCount; i++) {
    if (portCosmeticsChoice(kCap) == paletteIndex(kPresets[i].cap) &&
        portCosmeticsChoice(kOveralls) == paletteIndex(kPresets[i].overalls)) {
      return i;
    }
  }
  return -1;
}

extern "C" void portCosmeticsSetLook(int i) {
  if (i < 0 || i >= kPresetCount || sSlots[kCap].choice == nullptr) {
    return;
  }
  *sSlots[kCap].choice = paletteIndex(kPresets[i].cap);
  *sSlots[kOveralls].choice = paletteIndex(kPresets[i].overalls);
  portSettingsSave();
}
