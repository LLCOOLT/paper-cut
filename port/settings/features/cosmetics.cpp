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
// his eyes' and his hair's, so no mask tells them apart.
//
// The partners' masks are by colour and by place: a part is a colour range
// in some of their textures, or in a part of a texture (a box across it),
// where another part has the same colour (Flurrie's dress is the lavender of
// her face and hands, Bobbery's fuse the gold of his wheel, Ms. Mowz's cape
// the red of her mask). Their textures are known by content
// (cosmetic_textures.inc), so the same mask holds wherever the game draws
// them: the field, battles, the Party page's own models, the menus' and the
// scenes'. Their faces on the status bar and their pictures in the menus are
// icons, with masks of their own. Koops's shell and hoodie, Goombella's hair
// and helmet, Flurrie's hair and dress, Bobbery's body and fuse, Vivian's
// body, hat and hair, Ms. Mowz's mask and cape; Yoshi's colour is the game's
// own choice, changed in the Wardrobe (wardrobe_aurora.c).
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/texture.hpp>

#include "port/cosmetics.h"
#include "port/luigi.h"
#include "port/postgame_extra.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
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
  kGoombellaHelmet,
  kFlurrieDress,
  kBobberyFuse,
  kVivianHat,
  kVivianHair,
  kMowzCape, // (the mask's colour while it is the game's: they were one slot)
  kHudMenu,  // the Start menu's pages (not a texture: their colours, src/window/win_root.c's winBgGX)
  // the Play as picks' own (docs/PLAY_AS.md): Luigi's, Peach's, Bowser's
  kLuigiCap,
  kLuigiOveralls,
  kPeachDress,
  kPeachCrown,
  kPeachHair,
  kPeachGloves,
  kPeachEarrings,
  kPeachShoes,
  kBowserShell,
  kBowserHair,
  kBowserSkin,
  kBowserSpikes,
  kBowserBracelets,
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
    {"cosmetic_mowz_mask", "cosmetic_mowz_mask_rgb", "Ms. Mowz's mask", nullptr,
     "partner ms mowz mask colour color red", 0xCC2200},
    {"cosmetic_vivian", "cosmetic_vivian_rgb", "Vivian's body", nullptr, "partner vivian body colour color purple",
     0x8C2D8C},
    {"cosmetic_goombella_helmet", "cosmetic_goombella_helmet_rgb", "Goombella's helmet", nullptr,
     "partner goombella helmet hat colour color khaki", 0xBCC29A},
    {"cosmetic_flurrie_dress", "cosmetic_flurrie_dress_rgb", "Flurrie's dress", nullptr,
     "partner flurrie dress body cloud colour color lavender", 0xC4C0EC},
    {"cosmetic_bobbery_fuse", "cosmetic_bobbery_fuse_rgb", "Bobbery's fuse", nullptr,
     "partner bobbery fuse rope colour color gold", 0xD2A532},
    {"cosmetic_vivian_hat", "cosmetic_vivian_hat_rgb", "Vivian's hat", nullptr,
     "partner vivian hat colour color red pink", 0xE0245A},
    {"cosmetic_vivian_hair", "cosmetic_vivian_hair_rgb", "Vivian's hair", nullptr,
     "partner vivian hair colour color pink", 0xF8A8A0},
    {"cosmetic_mowz_cape", "cosmetic_mowz_cape_rgb", "Ms. Mowz's cape", "Its own colour, or the mask's.",
     "partner ms mowz cape colour color red", 0xCC2200},
    {"cosmetic_hud_menu", "cosmetic_hud_menu_rgb", "Menus", "The Start menu's pages, all in one colour.",
     "hud theme menu start pause page window colour color", 0x6E5AB4},
    {"cosmetic_luigi_cap", "cosmetic_luigi_cap_rgb", "Luigi's cap and shirt", nullptr,
     "play as luigi hat cap shirt colour color green", 0x3A9A35},
    {"cosmetic_luigi_overalls", "cosmetic_luigi_overalls_rgb", "Luigi's overalls", nullptr,
     "play as luigi overalls colour color blue", 0x1A50F4},
    {"cosmetic_peach_dress", "cosmetic_peach_dress_rgb", "Peach's dress", nullptr,
     "play as peach dress gown colour color pink", 0xF77CC8},
    {"cosmetic_peach_crown", "cosmetic_peach_crown_rgb", "Peach's crown", nullptr,
     "play as peach crown colour color gold", 0xF0C21A},
    {"cosmetic_peach_hair", "cosmetic_peach_hair_rgb", "Peach's hair", nullptr,
     "play as peach hair colour color blonde yellow", 0xFAD228},
    {"cosmetic_peach_gloves", "cosmetic_peach_gloves_rgb", "Peach's gloves", nullptr,
     "play as peach gloves colour color white", 0xDFE3E8},
    {"cosmetic_peach_earrings", "cosmetic_peach_earrings_rgb", "Peach's earrings", nullptr,
     "play as peach earrings jewels colour color blue", 0x1E7EB8},
    {"cosmetic_peach_shoes", "cosmetic_peach_shoes_rgb", "Peach's shoes", nullptr,
     "play as peach shoes heels colour color pink", 0xE6519B},
    {"cosmetic_bowser_shell", "cosmetic_bowser_shell_rgb", "Bowser's shell", nullptr,
     "play as bowser shell colour color green", 0x158C2E},
    {"cosmetic_bowser_hair", "cosmetic_bowser_hair_rgb", "Bowser's hair", nullptr,
     "play as bowser hair mane colour color red", 0xF2603A},
    {"cosmetic_bowser_skin", "cosmetic_bowser_skin_rgb", "Bowser's skin", nullptr,
     "play as bowser skin scales colour color orange", 0xF08A00},
    {"cosmetic_bowser_spikes", "cosmetic_bowser_spikes_rgb", "Bowser's horns and spikes", nullptr,
     "play as bowser horns spikes colour color yellow", 0xFFD633},
    {"cosmetic_bowser_bracelets", "cosmetic_bowser_bracelets_rgb", "Bowser's bracelets", nullptr,
     "play as bowser bracelets cuffs collar colour color grey gray", 0x808080},
};

// whose each slot is: the Play as pick it dresses (0 Mario .. 3 Bowser), or -1
int slotPick(int s) {
  switch (s) {
  case kCap: case kOveralls: return PORT_PLAY_AS_MARIO;
  case kLuigiCap: case kLuigiOveralls: return PORT_PLAY_AS_LUIGI;
  case kPeachDress: case kPeachCrown: case kPeachHair: case kPeachGloves: case kPeachEarrings: case kPeachShoes:
    return PORT_PLAY_AS_PEACH;
  case kBowserShell: case kBowserHair: case kBowserSkin: case kBowserSpikes: case kBowserBracelets:
    return PORT_PLAY_AS_BOWSER;
  default: return -1;
  }
}

// The status bar's and the menus' themes: the banners' and the Start menu
// pages' colours together (0 the game's own). A part set in its own row
// keeps its own colour; the rest take the theme's. Each is readable on its
// own (light pages for the black text, banners deep enough for the white
// numbers), and all go through the colour-blind filter as any colour does
// (it is the whole picture's, display_aurora.c); High contrast's banners
// differ in lightness as well as hue, for every kind of colour blindness.
struct Theme {
  const char* name;
  uint32_t hp, fp, sp, menu;
};
constexpr Theme kThemes[] = {
    {"Original", 0, 0, 0, 0},
    {"Night", 0xB04A6E, 0xC07A2E, 0x4A6AB0, 0x3C4A82},
    {"Forest", 0xD0584E, 0xD4A23C, 0x4AA070, 0x3E8A4E},
    {"Ocean", 0xE87896, 0xEAA24E, 0x40A8DC, 0x2A7EB8},
    {"Candy", 0xF070B0, 0xF5B041, 0xA27AE6, 0xD866A8},
    {"High contrast", 0xE01828, 0xF0A800, 0x1C64E0, 0x484848},
};
constexpr int kThemeCount = int(sizeof(kThemes) / sizeof(kThemes[0]));
const char* kThemeNames[kThemeCount];
int* sTheme;

// the theme's colour for a slot, or -1 (none, or not one of the theme's)
int64_t themeColour(int s) {
  const int t = sTheme != nullptr ? *sTheme : 0;
  if (t <= 0 || t >= kThemeCount) {
    return -1;
  }
  switch (s) {
  case kHudHp: return kThemes[t].hp;
  case kHudFp: return kThemes[t].fp;
  case kHudSp: return kThemes[t].sp;
  case kHudMenu: return kThemes[t].menu;
  default: return -1;
  }
}

// Vivian's slot, until it has a value of its own (-1): the one she shared
// with Goombella, so a colour picked before keeps showing
void settle() {
  Slot& v = sSlots[kVivian];
  const Slot& p = sSlots[kPartner];
  if (v.choice != nullptr && *v.choice < 0) {
    const int c = p.choice != nullptr ? *p.choice : 0;
    *v.choice = c >= 0 && c < kColours ? c : 0; // (never a -1 or a value past the list, whatever wrote it)
  }
  if (v.rgb != nullptr && *v.rgb < 0) {
    *v.rgb = p.rgb != nullptr && *p.rgb >= 0 ? *p.rgb & 0xFFFFFF : int(v.defaultCustom);
  }
}

// a slot's choice, settled and inside the list (0, the game's own, for
// anything else: an unknown slot, a -1, a value written past the list)
int choiceOf(int s) {
  if (s < 0 || s >= kSlots || sSlots[s].choice == nullptr) {
    return 0;
  }
  settle();
  const int c = *sSlots[s].choice;
  return c >= 0 && c < kColours ? c : 0;
}

// the colour a slot asks for now, or -1: the game's own
int64_t slotColour(int s) {
  const int c = choiceOf(s);
  if (c <= 0 || c >= kColours) {
    return themeColour(s);
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
// with it). areas: where it is, where another part elsewhere has its colour:
// textures by their place in the file (a partner's: in their own file,
// wherever the texture is loaded from) and the part of each (0..1 across and
// down); none, everywhere. fallback: a slot whose colour it takes while its
// own is the game's (a part that had none of its own before).
struct Area {
  int tex;
  float x0 = 0.f, y0 = 0.f, x1 = 1.f, y1 = 1.f;
};

struct Region {
  int slot;
  float hue, half, soft;
  float satMin, lMin;
  Hsl ref;
  float lMax = 1.f;
  std::vector<Area> areas = {};
  int fallback = -1;
  // a part of no colour of its own (white gloves, grey bracelets): no more
  // saturated than satMax, and grey: the new colour's hue and saturation
  // taken whole (a grey's hue is noise), only its shading kept
  float satMax = 1.f;
  bool grey = false;
  // textures of the file it never touches, though they have its colour (Mario's tongue and sweat drops)
  std::vector<int> notTex = {};
  // and boxes of textures it never touches (Goombella's lamp's glass, as gold as her hair)
  std::vector<Area> notAreas = {};
  // no blends taken at its edges (Bowser's skin: the dark lines round it and on his belly are his outline's
  // red-brown, near the orange)
  bool noBlend = false;
};

bool excluded(const Region& r, int index) {
  return std::find(r.notTex.begin(), r.notTex.end(), index) != r.notTex.end();
}

Region without(Region r, std::vector<int> tex, std::vector<Area> boxes = {}) {
  r.notTex = std::move(tex);
  r.notAreas = std::move(boxes);
  return r;
}

Region blendless(Region r) {
  r.noBlend = true;
  return r;
}

// the textures of a list, whole
std::vector<Area> whole(std::initializer_list<int> tex) {
  std::vector<Area> a;
  for (int t : tex) {
    a.push_back(Area{t});
  }
  return a;
}

bool inTexture(const Region& r, int index) {
  if (excluded(r, index)) {
    return false;
  }
  if (r.areas.empty()) {
    return true;
  }
  for (const Area& a : r.areas) {
    if (a.tex == index) {
      return true;
    }
  }
  return false;
}

bool inArea(const Region& r, int index, float u, float v) {
  for (const Area& a : r.notAreas) {
    if (a.tex == index && u >= a.x0 && u < a.x1 && v >= a.y0 && v < a.y1) {
      return false;
    }
  }
  if (r.areas.empty()) {
    return r.notTex.empty() || !excluded(r, index);
  }
  for (const Area& a : r.areas) {
    if (a.tex == index && u >= a.x0 && u < a.x1 && v >= a.y0 && v < a.y1) {
      return true;
    }
  }
  return false;
}

// the colour a region takes now, or -1
int64_t regionColour(const Region& r, const int64_t* colours) {
  return colours[r.slot] >= 0 ? colours[r.slot] : r.fallback >= 0 ? colours[r.fallback] : -1;
}

float regionWeight(const Region& r, const Hsl& p) {
  const float dh = std::fabs(hueDelta(p.h, r.hue));
  const float wh = 1.f - ramp(dh, r.half, r.half + r.soft);
  if (wh <= 0.f) {
    return 0.f;
  }
  const float wl = r.lMax < 1.f ? 1.f - ramp(p.l, r.lMax, r.lMax + 0.04f) : 1.f;
  const float ws = r.satMax < 1.f ? 1.f - ramp(p.s, r.satMax, r.satMax + 0.08f) : 1.f;
  return wh * wl * ws * ramp(p.s, r.satMin - 0.1f, r.satMin) * ramp(p.l, r.lMin - 0.06f, r.lMin);
}

// p moved from r.ref to the colour to: the hue turned by the same amount,
// saturation scaled, lightness offset (its shading's steps kept, a little
// flatter)
// (a dark edge's darkness kept whatever the new colour: a texel as dark as the outline stays as dark,
// shading between that and the part's own lightness eases into the new colour's, so a white or yellow
// part keeps its dark rim against the outline instead of a pale notch in it)
float shiftLightness(const Region& r, const Hsl& p, const Hsl& to) {
  const float moved = std::clamp(to.l + (p.l - r.ref.l) * 0.8f, 0.f, 1.f);
  const float keep = 1.f - ramp(p.l, 0.2f, 0.45f);
  return std::min(moved, moved + (p.l - moved) * keep);
}

Hsl shift(const Region& r, const Hsl& p, const Hsl& to) {
  Hsl o;
  if (r.grey) {
    return Hsl{to.h, to.s, shiftLightness(r, p, to)};
  }
  // (the hue mostly the new colour's, so a shade's or a highlight's hue drift doesn't show the old colour
  // through; the saturation mostly kept, so a shade is the new colour darker, not a muddy grey of it)
  o.h = to.h + hueDelta(p.h, r.ref.h) * 0.25f;
  o.s = std::clamp(to.s * (0.7f + 0.3f * std::min(p.s / r.ref.s, 1.5f)), 0.f, 1.f);
  o.l = shiftLightness(r, p, to);
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

// The partners, whose textures are known by their content wherever they're
// loaded from (cosmetic_textures.inc, tools/port/gen_cosmetic_textures.py):
// their own file, the Party page's, the menus' and the scenes'
enum Owner { kGoombella, kKoops, kBobbery, kFlurrie, kVivianOwner, kMowz, kOwners };

struct Known {
  uint64_t hash;
  int owner;
  int index; // its place in the partner's own file
};
#define kVivian kVivianOwner
constexpr Known kKnown[] = {
#include "cosmetic_textures.inc"
};
#undef kVivian

// Where a set's textures come from: an animation texture file, a status bar
// or menu icon (portHudIconTextures's part), or a partner's textures
// wherever they are (owner)
struct Set {
  const char* file; // nullptr: an icon's or a partner's
  int hudPart;
  std::vector<Region> regions;
  int owner = -1;
  int pick = -1; // the disc's own model of a Play as pick: recoloured only while it's the pick (else it's an NPC's)
  int face = -1; // a Play as pick's faces (portPlayAsFaceTextures)

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

// textures first..last of a file, whole
std::vector<Area> span(std::vector<Area> a, int first, int last) {
  for (int t = first; t <= last; t++) {
    a.push_back(Area{t});
  }
  return a;
}

// a part of no colour of its own: any hue, no more saturated than satMax, as
// light as lMin to lMax (its outline and highlights left)
Region greyPart(int slot, float lMin, float lMax, float satMax, Hsl ref, std::vector<Area> areas) {
  Region r{slot, 0.f, 180.f, 1.f, 0.f, lMin, ref, lMax, std::move(areas)};
  r.satMax = satMax;
  r.grey = true;
  return r;
}

// The Play as picks (docs/PLAY_AS.md), measured over the disc's textures of
// each model by their place in it (ours, c_luigi_pc and the others, keep the
// disc's textures first and add the hammer's after): Luigi's cap and shirt
// (the green of his cap, shirt and sleeves) and overalls (their blue); Peach's
// dress (its pinks, front and back, not her lips' or cheeks'), shoes (the pink
// in her leg's texture), gloves (their white, not the blue-grey outline), hair
// and crown (one gold, told apart by their textures) and earrings (the blue
// of the earring's own texture, not the brooch's gem); Bowser's shell (all of
// his green), hair (the mane's textures), skin (his orange, not his snout's
// or belly's lighter peach), horns and spikes (yellow, not the belly or the
// Star Rod he holds in one scene) and bracelets (their grey: the wrists' and
// collar's, not the claws' or the shell's rim, lighter). The disc's own model
// of each is recoloured only while that one is picked: otherwise it's an NPC.
void playAsSets(std::vector<Set>& sets) {
  const Region luigiCap{kLuigiCap, 117.f, 22.f, 8.f, 0.3f, 0.2f, Hsl{117.f, 0.49f, 0.41f}};
  const Region luigiOveralls{kLuigiOveralls, 232.f, 18.f, 8.f, 0.4f, 0.15f, Hsl{232.f, 0.89f, 0.53f}};
  for (int disc = 0; disc < 2; disc++) {
    sets.push_back(Set{disc ? "a/c_luigi-" : "a/c_luigi_pc-", -1, {luigiCap, luigiOveralls}, -1,
                       disc ? PORT_PLAY_AS_LUIGI : -1});
  }

  const Region dress{kPeachDress, 326.f, 26.f, 8.f, 0.3f, 0.1f, Hsl{326.f, 0.9f, 0.75f}};
  const Region shoes{kPeachShoes, 330.f, 22.f, 8.f, 0.4f, 0.1f, Hsl{330.f, 0.8f, 0.7f}};
  const Region hair{kPeachHair, 47.f, 14.f, 6.f, 0.5f, 0.25f, Hsl{47.f, 0.95f, 0.58f}};
  const Region crown{kPeachCrown, 46.f, 14.f, 6.f, 0.5f, 0.3f, Hsl{48.f, 0.9f, 0.55f}};
  const Region earrings{kPeachEarrings, 205.f, 25.f, 8.f, 0.4f, 0.2f, Hsl{205.f, 0.8f, 0.4f}};
  const Region gloves = greyPart(kPeachGloves, 0.45f, 1.f, 0.3f, Hsl{210.f, 0.08f, 0.88f}, {});
  auto in = [](Region r, std::vector<Area> a) {
    r.areas = std::move(a);
    return r;
  };
  // c_peach (front)
  const std::vector<Region> front{
      in(dress, span(span(span({{0}, {32}, {33}}, 4, 14), 50, 55), 90, 101)),
      in(shoes, {{2}}),
      in(gloves, span(span({}, 15, 28), 34, 46)),
      in(hair, span(span({{61}, {62}}, 56, 59), 84, 86)),
      in(crown, {{87}}),
      in(earrings, {{60}}),
  };
  // c_peach_r (back)
  const std::vector<Region> back{
      in(dress, span({{10}, {17}, {18}, {19}}, 0, 3)),
      in(gloves, span(span({}, 4, 7), 11, 14)),
      in(hair, span({}, 20, 26)),
      in(crown, {{30}}),
      in(earrings, {{29}}),
  };
  for (int disc = 0; disc < 2; disc++) {
    const int pick = disc ? PORT_PLAY_AS_PEACH : -1;
    sets.push_back(Set{disc ? "a/c_peach-" : "a/c_peach_pc-", -1, front, -1, pick});
    sets.push_back(Set{disc ? "a/c_peach_r-" : "a/c_peach_r_pc-", -1, back, -1, pick});
  }

  // c_koopa's 41 textures (30: the Star Rod; ours adds his hammers after them, 41 on, never his)
  const std::vector<Area> notRod = span(span({}, 0, 29), 31, 40);
  // (the horns and spikes: not his belly, whose tan lines are near their yellow: texture 40's left half,
  // and the belly in his whole picture, 0)
  const std::vector<Area> horns = span(span({{40, 0.5f, 0.f, 1.f, 1.f},
                                             {0, 0.f, 0.f, 1.f, 0.7f},
                                             {0, 0.f, 0.7f, 0.37f, 1.f},
                                             {0, 0.6f, 0.7f, 1.f, 1.f}},
                                            1, 29),
                                       31, 39);
  const std::vector<Region> bowser{
      Region{kBowserShell, 135.f, 22.f, 8.f, 0.4f, 0.12f, Hsl{135.f, 0.8f, 0.3f}, 1.f, notRod},
      // (his hair's highlights are as orange as his skin: told apart by where they are, the hair's own
      // textures and the top of his whole picture, 0, not its mouth's red; the skin the rest of it)
      Region{kBowserHair, 12.f, 17.f, 5.f, 0.6f, 0.35f, Hsl{12.f, 0.9f, 0.58f}, 0.66f, // (not his paler snout and cheeks)
             span({{0, 0.f, 0.f, 1.f, 0.36f}}, 16, 22)},
      blendless(Region{kBowserSkin, 32.f, 7.f, 4.f, 0.75f, 0.28f, Hsl{32.f, 1.f, 0.48f}, 0.64f,
                       span(span({{15}, {0, 0.f, 0.36f, 1.f, 1.f}}, 1, 9), 31, 40)}),
      Region{kBowserSpikes, 50.f, 9.f, 5.f, 0.6f, 0.3f, Hsl{50.f, 1.f, 0.6f}, 0.74f, horns},
      // (by place in each texture that has them, his wrists' cuffs and his collar: not his claws and
      // spikes beside them, as grey)
      greyPart(kBowserBracelets, 0.3f, 0.64f, 0.15f, Hsl{0.f, 0.f, 0.48f},
               {{0, 0.34f, 0.4f, 0.64f, 0.78f},
                {15, 0.69f, 0.82f, 0.9f, 1.f},
                {15, 0.37f, 0.55f, 0.53f, 0.72f},
                {31, 0.48f, 0.15f, 0.91f, 0.73f},
                {32, 0.48f, 0.15f, 0.91f, 0.73f},
                {33, 0.48f, 0.15f, 0.91f, 0.73f},
                {34, 0.48f, 0.15f, 0.91f, 0.73f},
                {37, 0.42f, 0.15f, 0.86f, 0.72f},
                {38, 0.42f, 0.15f, 0.86f, 0.72f},
                {39, 0.5f, 0.03f, 0.93f, 0.66f},
                {39, 0.1f, 0.36f, 0.33f, 0.9f},
                {40, 0.f, 0.04f, 0.24f, 0.43f}}),
  };
  for (int disc = 0; disc < 2; disc++) {
    sets.push_back(Set{disc ? "a/c_koopa-" : "a/c_koopa_pc-", -1, bowser, -1, disc ? PORT_PLAY_AS_BOWSER : -1});
  }

  // their faces on the status bar and in the menus, cut from those textures
  // (port/assets/luigi/art/recipes): by colour alone
  auto all = [](std::vector<Region> rs) {
    for (Region& r : rs) {
      r.areas.clear();
    }
    return rs;
  };
  Set luigiFaces{nullptr, -1, {luigiCap, luigiOveralls}};
  luigiFaces.face = PORT_PLAY_AS_LUIGI;
  sets.push_back(luigiFaces);
  Set peachFaces{nullptr, -1, all({hair, earrings, dress})};
  peachFaces.face = PORT_PLAY_AS_PEACH;
  sets.push_back(peachFaces);
  Set bowserFaces{nullptr, -1, all({bowser[0], bowser[1], bowser[2], bowser[3]})};
  bowserFaces.face = PORT_PLAY_AS_BOWSER;
  sets.push_back(bowserFaces);
}

std::vector<Set> makeSets() {
  // Mario: the red of his cap and shirt (not the orange of his skin, the
  // browns of his hair and shoes) and the blue of his overalls (not his
  // gloves' grey shading); field, back views, battle and scenes (b_mario and
  // e_mario use a_mario's textures)
  const Region cap{kCap, 4.f, 12.f, 5.f, 0.62f, 0.28f, kMarioRed}; // (as saturated as no shoe is)
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
  const Region bobberyBody{kBobberyBody, 31.f, 7.f, 2.f, 0.3f, 0.06f, kBobberyBrown, 0.5f, whole({4, 32})};
  // Goombella's helmet: its khaki, in her face pictures (0 and 1) and the
  // head's texture (10) but not her lamp's ring (its grey is of the same hue)
  // or the Tattle book's green
  const Region goombellaHelmet{kGoombellaHelmet, 82.f, 24.f, 8.f, 0.08f, 0.3f, Hsl{70.f, 0.22f, 0.62f}, 1.f,
                               {{0}, {1}, {10, 0.f, 0.f, 1.f, 0.6f}, {10, 0.f, 0.6f, 0.55f, 1.f},
                                {10, 0.72f, 0.75f, 1.f, 1.f}}};
  // Flurrie's dress: her body's lavender, in the textures of her body (44)
  // and her whole picture (0), not her face's, bust's, arms' and hands',
  // the same lavender
  const Region flurrieDress{kFlurrieDress, 243.f, 14.f, 6.f, 0.1f, 0.45f, Hsl{243.f, 0.45f, 0.8f}, 1.f,
                            whole({0, 44})};
  // Bobbery's fuse: the rope's gold, its own texture (16) and where it is
  // in his whole picture (0), not the wheel's knob under it
  const Region bobberyFuse{kBobberyFuse, 45.f, 12.f, 5.f, 0.3f, 0.2f, Hsl{44.f, 0.7f, 0.5f}, 1.f,
                           {{16}, {0, 0.6f, 0.f, 0.72f, 0.24f}, {0, 0.72f, 0.f, 0.85f, 0.14f}}};
  // Vivian's hat: its deep pink (not her cheeks', greyer) and her hair's
  // curls, lighter, in the head's textures (not her lips')
  const Region vivianHat{kVivianHat, 338.f, 14.f, 5.f, 0.72f, 0.22f, Hsl{340.f, 0.85f, 0.47f}, 0.62f};
  const Region vivianHair{kVivianHair, 5.f, 13.f, 6.f, 0.35f, 0.56f, Hsl{3.f, 0.8f, 0.78f}, 1.f,
                          whole({0, 1, 2, 8})};
  // Ms. Mowz's mask and its eye holes, and her cape: one red (not her ears'
  // pink), told apart by where they are: the mask on her face pictures' left
  // (0, 1) and at the atlas's bottom left (2), the cape's ends beside it
  const Region mowzMask{kMowzMask, 10.f, 6.f, 3.f, 0.8f, 0.24f, kMowzRed, 0.52f, [] {
                          std::vector<Area> a{{0, 0.f, 0.f, 0.6f, 1.f}, {1, 0.f, 0.f, 0.6f, 1.f},
                                              {2, 0.f, 0.5f, 0.42f, 1.f}};
                          for (int t = 21; t <= 42; t++) {
                            a.push_back(Area{t});
                          }
                          return a;
                        }()};
  const Region mowzCape{kMowzCape, 10.f, 6.f, 3.f, 0.8f, 0.24f, kMowzRed, 0.52f,
                        {{0, 0.6f, 0.f, 1.f, 1.f}, {1, 0.6f, 0.f, 1.f, 1.f}, {2, 0.42f, 0.3f, 0.7f, 1.f}}, kMowzMask};
  std::vector<Set> sets;
  // (not his mouths' and tongues' red, his shoes' red-brown shading, his sweat drops' blue or the pale
  // lavender of 53 and 90: their colours are near the cap's and the overalls', and were tinted with them)
  std::vector<int> notMario{53, 90, 111, 112}, notMarioBack{14, 51};
  for (int t = 129; t <= 150; t++) {
    notMario.push_back(t);
  }
  for (int t = 116; t <= 140; t++) {
    notMarioBack.push_back(t);
  }
  sets.push_back(Set{"a/a_mario-", -1, {without(cap, notMario), without(capLight, notMario), without(overalls, notMario)}});
  sets.push_back(Set{"a/a_mario_r-", -1,
                     {without(cap, notMarioBack), without(capLight, notMarioBack), without(overalls, notMarioBack)}});
  // (her hair, not her Tattle book's label, 2 to 7 and 9, nor her lamp's glass in her face pictures, 0 and 1,
  // and her head's atlas, 10: their gold and peach are her hair's)
  sets.push_back(Set{nullptr, -1,
                     {without(hair, {2, 3, 4, 5, 6, 7, 9},
                              {{0, 0.24f, 0.16f, 0.45f, 0.38f}, {1, 0.24f, 0.16f, 0.45f, 0.38f},
                               {10, 0.6f, 0.6f, 0.74f, 0.74f}}),
                      goombellaHelmet},
                     kGoombella});
  sets.push_back(Set{nullptr, -1, {vivian, vivianHat, vivianHair}, kVivianOwner});
  sets.push_back(Set{nullptr, -1, {koopsShell, koopsHoodie}, kKoops});
  sets.push_back(Set{nullptr, -1, {flurrieHair, flurrieShine, flurrieDress}, kFlurrie});
  sets.push_back(Set{nullptr, -1, {bobberyBody, bobberyFuse}, kBobbery});
  sets.push_back(Set{nullptr, -1, {mowzMask, mowzCape}, kMowz});
  // the banners: all of their colour (the outline is grey)
  sets.push_back(Set{nullptr, 0, {{kHudHp, 355.f, 40.f, 15.f, 0.2f, 0.f, Hsl{355.f, 1.0f, 0.78f}}}});
  sets.push_back(Set{nullptr, 1, {{kHudFp, 30.f, 40.f, 15.f, 0.2f, 0.f, Hsl{32.f, 0.9f, 0.50f}}}});
  sets.push_back(Set{nullptr, 2, {{kHudSp, 205.f, 40.f, 15.f, 0.2f, 0.f, Hsl{205.f, 0.5f, 0.63f}}}});
  // the status bar's faces (their heart, at the top right, left out) and the
  // menus' pictures, as their models where the picture's colours allow
  const std::vector<Area> face{{0, 0.f, 0.42f, 1.f, 1.f}, {0, 0.f, 0.f, 0.52f, 0.42f}};
  auto in = [](Region r, std::vector<Area> a) {
    r.areas = std::move(a);
    return r;
  };
  sets.push_back(Set{nullptr, 3, {cap, capLight}});
  sets.push_back(Set{nullptr, 4, {in(hair, {{0, 0.f, 0.f, 0.42f, 1.f}}), in(goombellaHelmet, face)}});
  sets.push_back(Set{nullptr, 7, {flurrieHair, flurrieShine}});
  sets.push_back(Set{nullptr, 8, {in(vivian, face), in(vivianHat, face), in(vivianHair, face)}});
  sets.push_back(Set{nullptr, 9, {in(mowzMask, face)}});
  const Region spriteHair{kPartner, 43.f, 7.f, 4.f, 0.82f, 0.3f, Hsl{42.f, 0.9f, 0.5f}, 1.f, {{0, 0.55f, 0.f, 1.f, 0.8f}}};
  sets.push_back(Set{nullptr, 10, {spriteHair, in(goombellaHelmet, {})}});
  sets.push_back(Set{nullptr, 11, {in(koopsShell, {})}});
  sets.push_back(Set{nullptr, 13, {flurrieHair, flurrieShine}});
  sets.push_back(Set{nullptr, 14, {vivian, in(vivianHair, {})}});
  sets.push_back(Set{nullptr, 15, {in(mowzMask, {})}});
  playAsSets(sets);
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

// TTYD_COSMETIC_PROBE=owner:index,index...: those textures of that partner
// (Owner's order) painted in hues around the wheel, the first red, wherever
// they're drawn: which texture is which part, for writing a mask
struct Probe {
  int owner = -1;
  std::vector<int> tex;
};
const Probe& probe() {
  static const Probe p = [] {
    Probe o;
    const char* e = SDL_getenv("TTYD_COSMETIC_PROBE");
    if (e == nullptr || std::strchr(e, ':') == nullptr) {
      return o;
    }
    o.owner = std::atoi(e);
    for (const char* q = std::strchr(e, ':') + 1; *q != '\0';) {
      o.tex.push_back(std::atoi(q));
      while (*q != '\0' && *q != ',') {
        q++;
      }
      if (*q == ',') {
        q++;
      }
    }
    return o;
  }();
  return p;
}

int probeIndex(const Set& s, int index) {
  const Probe& p = probe();
  if (s.owner < 0 || s.owner != p.owner) {
    return -1;
  }
  for (size_t k = 0; k < p.tex.size(); k++) {
    if (p.tex[k] == index) {
      return int(k);
    }
  }
  return -1;
}

// a red part, or a part of one (Mario's cap's highlights: their ref is his red)
bool redPart(const Region& r) {
  return !r.grey && (std::fabs(hueDelta(r.hue, 355.f)) < 16.f || std::fabs(hueDelta(r.ref.h, 355.f)) < 16.f);
}

// The outline round everything, which no part takes: near black, or the figure's own dark brown-red
// outline (Mario's 5/56/14); a part's own dark edge (Mario's overalls' navy, Luigi's cap's dark green)
// is its hue, well saturated, and so the part's. A red part (Mario's cap and shirt) has the outline's
// own hue: there the outline is what is darker than 0.2, or as dark as 0.3 and greyer (the sleeve's
// thick outline, 358/43/22), and the part's dark rim the saturated rest (7/73/24).
bool outlineFor(const Region& r, const Hsl& p) {
  const bool outlineRed = redPart(r);
  if (p.l < 0.07f) {
    return true;
  }
  if (outlineRed) {
    return p.l < 0.2f || (p.l < 0.3f && p.s < 0.5f);
  }
  if (p.l >= 0.2f) {
    return false;
  }
  return r.grey || std::fabs(hueDelta(p.h, r.hue)) > r.half + r.soft || p.s < 0.3f;
}

// Texel p (rgb pc) is shading of part r grown from texel f (rgb fc) beside it: 1 of the part's hue band,
// darker or greyer than its test asks; 2 a blend at its edge, further off in hue (with the outline,
// darker; with the next part, a hue between, as a seam's purple between red and blue), never as light
// as a pale part beside it (skin); 0 neither. Never lighter than its lMax, never the outline, and no
// further in colour from f than a shading's step (a big step is another part's edge). A grey part's:
// as grey.
int shadeOf(const Region& r, const Hsl& p, const uint8_t* pc, const uint8_t* fc) {
  if ((r.lMax < 1.f && p.l > r.lMax + 0.04f) || outlineFor(r, p)) {
    return 0;
  }
  int step = 0;
  for (int k = 0; k < 3; k++) {
    step = std::max(step, std::abs(int(pc[k]) - int(fc[k])));
  }
  if (r.grey) {
    return step <= 80 && p.s <= r.satMax + 0.12f && p.l >= r.lMin - 0.15f ? 1 : 0;
  }
  const float dh = std::fabs(hueDelta(p.h, r.hue));
  if (step <= 140 && dh <= r.half + r.soft && p.s >= std::max(r.satMin * 0.6f, 0.2f)) {
    return 1;
  }
  // (a red part's blends only toward purple: from red toward yellow are the skin's, hair's, shoes' and the
  // buttons' own colours, never a blend of the red)
  const float toward = hueDelta(p.h, r.hue);
  const bool redToYellow = redPart(r) && toward > r.half + r.soft;
  if (!r.noBlend && !redToYellow && step <= 130 && dh <= r.half + r.soft + 40.f && p.s >= 0.25f &&
      p.l <= std::max(0.66f, r.ref.l + 0.1f)) {
    return 2;
  }
  return 0;
}

// TTYD_COSMETIC_DUMP=<folder>: each texture recoloured written there as it was and as it is now
// (<set>_<index>_was.png, _now.png), and a log line of its texels taken whole and in part: to see that
// a part is covered and nothing else is
const char* dumping() {
  static const char* sDir = SDL_getenv("TTYD_COSMETIC_DUMP");
  return sDir != nullptr && sDir[0] != '\0' ? sDir : nullptr;
}
int sDumpStats[3]; // (texels taken whole, in part, and spots: left out beside a part, of its colour)

void dumpPng(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h) {
  SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(rgba.data()), w * 4);
  if (s != nullptr) {
    SDL_SavePNG(s, path.c_str());
    SDL_DestroySurface(s);
  }
}

std::string setName(const Set& s) {
  std::string n = s.file != nullptr ? s.file
                  : s.face >= 0     ? "face" + std::to_string(s.face)
                  : s.owner >= 0    ? "owner" + std::to_string(s.owner)
                                    : "hud" + std::to_string(s.hudPart);
  for (char& c : n) {
    if (c == '/' || c == '-') {
      c = '_';
    }
  }
  return n;
}

// one texture recoloured by the set's regions, or false: nothing of it changes
bool recolour(const Set& s, int index, const PortTexInfo& t, const int64_t* colours, std::vector<uint8_t>& px) {
  const int probed = probeIndex(s, index);
  bool in = probed >= 0;
  for (const Region& r : s.regions) {
    in |= regionColour(r, colours) >= 0 && inTexture(r, index);
  }
  if (!in || !decode(t, px)) {
    return false;
  }
  if (probed >= 0) {
    const float hue = float(probed) * 360.f / float(probe().tex.size());
    for (size_t i = 0; i < px.size(); i += 4) {
      const Hsl c = toHsl(px[i] / 255.f, px[i + 1] / 255.f, px[i + 2] / 255.f);
      float out[3];
      toRgb(Hsl{hue, 1.f, std::clamp(c.l, 0.25f, 0.75f)}, out);
      for (int k = 0; k < 3; k++) {
        px[i + k] = uint8_t(std::lround(out[k] * 255.f));
      }
    }
    return true;
  }
  const size_t n = s.regions.size();
  std::vector<Hsl> to(n);
  for (size_t i = 0; i < n; i++) {
    const int64_t c = regionColour(s.regions[i], colours);
    if (c >= 0) {
      to[i] = toHsl(float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f, float(c & 255) / 255.f);
    }
  }
  // Each texel goes to the region whose colour test it matches best (not the first that matches at
  // all: a cap's highlight in the cap's soft edge and the highlight's middle takes the highlight's),
  // then the parts are completed (1-3 below): what the test only half took or just missed (an edge's
  // anti-aliasing, a fold's dark shade, a highlight's paler one, the dark rim against the outline, the
  // blend of a seam between two parts) is its part's whole. Without that those texels kept the old
  // colour, or half of it, and the parts came out spotty (GitHub discussion #15). The outline, a paler
  // part of the same hue, another part's colours (skin, hair, gold) and stray texels of the colour
  // elsewhere are never taken.
  const int w = t.width, h = t.height;
  const size_t texels = size_t(w) * size_t(h);
  std::vector<int8_t> who(texels, -1);
  std::vector<float> wt(texels, 0.f);
  std::vector<Hsl> hsl(texels);
  const float iw = 1.f / float(w), ih = 1.f / float(h);
  auto uOf = [&](size_t texel) { return (float(texel % size_t(w)) + 0.5f) * iw; };
  auto vOf = [&](size_t texel) { return (float(texel / size_t(w)) + 0.5f) * ih; };
  for (size_t texel = 0; texel < texels; texel++) {
    const uint8_t* p = &px[texel * 4];
    if (p[3] == 0) {
      continue;
    }
    hsl[texel] = toHsl(p[0] / 255.f, p[1] / 255.f, p[2] / 255.f);
    for (size_t ri = 0; ri < n; ri++) {
      const Region& r = s.regions[ri];
      if (regionColour(r, colours) < 0 || !inArea(r, index, uOf(texel), vOf(texel))) {
        continue;
      }
      const float rw = regionWeight(r, hsl[texel]);
      if (rw > wt[texel]) {
        wt[texel] = rw;
        who[texel] = int8_t(ri);
      }
    }
  }
  // (a texel's 8 neighbours, each once, for fn(neighbour); fn says whether to go on from it)
  std::vector<uint32_t> queue;
  std::vector<uint8_t> seen(texels);
  auto grow = [&](auto&& take) -> void {
    for (size_t q = 0; q < queue.size(); q++) {
      const uint32_t at = queue[q];
      const int x = int(at % uint32_t(w)), y = int(at / uint32_t(w));
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          const int nx = x + dx, ny = y + dy;
          if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) {
            continue;
          }
          const size_t m = size_t(ny) * size_t(w) + size_t(nx);
          if (!seen[m] && px[m * 4 + 3] != 0 && take(at, m)) {
            seen[m] = 1;
            queue.push_back(uint32_t(m));
          }
        }
      }
    }
  };
  // 1. Patches: what each part's test took, 8-connected (whole or in part). A patch with fewer than
  // kMinWhole texels taken whole is a few texels of another part near the colour (Vivian's lips by her
  // body's purple, a tan line on Bowser's belly): given back, its own colour kept. In a patch, what was
  // taken in part is the part's whole (a highlight at the edge of the part's hue, an edge's blend), but
  // for one lighter than the part's lightest (a paler part of the same hue beside it).
  constexpr int kMinWhole = 12;
  std::vector<float> full(texels); // (each texel's weight as the test gave it)
  for (size_t texel = 0; texel < texels; texel++) {
    full[texel] = wt[texel];
  }
  // (a small patch within kNear texels of a big one, of any part, is a bit of a seam's dither, as a few
  // red texels in the purple between Mario's shirt and overalls: kept)
  constexpr int kNear = 3;
  std::vector<uint8_t> big(texels, 0);
  std::vector<std::vector<uint32_t>> small;
  for (size_t start = 0; start < texels; start++) {
    if (seen[start] || who[start] < 0) {
      continue;
    }
    const int8_t ri = who[start];
    queue.clear();
    queue.push_back(uint32_t(start));
    seen[start] = 1;
    grow([&](uint32_t, size_t m) { return who[m] == ri; });
    int whole = 0;
    for (uint32_t m : queue) {
      whole += full[m] >= 1.f;
    }
    if (whole >= kMinWhole) {
      for (uint32_t m : queue) {
        big[m] = 1;
      }
    } else {
      small.push_back(queue);
    }
  }
  for (const auto& patch : small) {
    bool near = false;
    for (size_t i = 0; i < patch.size() && !near; i++) {
      const int x = int(patch[i] % uint32_t(w)), y = int(patch[i] / uint32_t(w));
      for (int dy = -kNear; dy <= kNear && !near; dy++) {
        for (int dx = -kNear; dx <= kNear && !near; dx++) {
          const int nx = x + dx, ny = y + dy;
          near = nx >= 0 && ny >= 0 && nx < w && ny < h && big[size_t(ny) * size_t(w) + size_t(nx)];
        }
      }
    }
    for (uint32_t m : patch) {
      if (!near) {
        who[m] = -1;
      } else {
        big[m] = 1;
      }
    }
  }
  for (size_t m = 0; m < texels; m++) {
    if (!big[m] || who[m] < 0) {
      continue;
    }
    const Region& r = s.regions[size_t(who[m])];
    if (outlineFor(r, hsl[m])) {
      who[m] = -1; // (the outline itself, which the test half took)
    } else if (hsl[m].l <= r.lMax) {
      wt[m] = 1.f;
    }
  }
  // 2. Then each part grows over its own shading, a ring of texels at a time from what it has, all the
  // parts at once (so where two meet, each takes its side): a texel goes to the touching part whose hue
  // is nearest it, when it is that part's shading (part of its hue family, darker or greyer, not much
  // further in colour from the texel it grows from than shading steps are) and not the outline; and its
  // edge's blends (shadeOf's 2), at most kBlendReach texels deep. kReach rings at most.
  constexpr int kReach = 12, kBlendReach = 5;
  std::vector<uint8_t> ring(texels, 0), blend(texels, 0);
  std::vector<uint32_t> frontier, next;
  for (size_t texel = 0; texel < texels; texel++) {
    if (who[texel] >= 0) {
      frontier.push_back(uint32_t(texel));
    }
  }
  std::vector<uint8_t> asked(texels, 0);
  // a blend is between the part's texel q and another texel beside it in colour: the part's colour mixed
  // with the outline's or the next part's (a texel as the other side's colour isn't: the next part itself)
  // (a texel as the one beside it already taken: the same band of a blend, a texel further along)
  auto same = [&](size_t m, size_t q) {
    int step = 0;
    for (int k = 0; k < 3; k++) {
      step = std::max(step, std::abs(int(px[m * 4 + k]) - int(px[q * 4 + k])));
    }
    return step <= 16;
  };
  auto between = [&](size_t m, size_t q) {
    const int mx = int(m % size_t(w)), my = int(m / size_t(w));
    const uint8_t* a = &px[q * 4];
    const uint8_t* c = &px[m * 4];
    for (int ey = -1; ey <= 1; ey++) {
      for (int ex = -1; ex <= 1; ex++) {
        const int nx = mx + ex, ny = my + ey;
        if ((ex == 0 && ey == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) {
          continue;
        }
        const size_t o = size_t(ny) * size_t(w) + size_t(nx);
        const uint8_t* b = &px[o * 4];
        if (o == q || b[3] == 0) {
          continue;
        }
        float ab2 = 0.f, t = 0.f;
        for (int k = 0; k < 3; k++) {
          ab2 += float(b[k] - a[k]) * float(b[k] - a[k]);
          t += float(c[k] - a[k]) * float(b[k] - a[k]);
        }
        if (ab2 < 40.f * 40.f) {
          continue; // (the other side as the part's colour: no blend of two)
        }
        t /= ab2;
        float off = 0.f;
        for (int k = 0; k < 3; k++) {
          const float e = float(c[k]) - (float(a[k]) + t * float(b[k] - a[k]));
          off += e * e;
        }
        if (t >= 0.12f && t <= 0.88f && off <= 36.f * 36.f) {
          return true;
        }
      }
    }
    return false;
  };
  for (int d = 1; d <= kReach && !frontier.empty(); d++) {
    next.clear();
    for (uint32_t f : frontier) {
      const int fx = int(f % uint32_t(w)), fy = int(f / uint32_t(w));
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          const int nx = fx + dx, ny = fy + dy;
          if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) {
            continue;
          }
          const size_t m = size_t(ny) * size_t(w) + size_t(nx);
          if (who[m] >= 0 || px[m * 4 + 3] == 0 || asked[m] == d) {
            continue;
          }
          asked[m] = uint8_t(d);
          // (the parts touching it from the last ring or before)
          int best = -1, bestBlend = 0;
          float bestHue = 1e9f;
          for (int ey = -1; ey <= 1; ey++) {
            for (int ex = -1; ex <= 1; ex++) {
              const int qx = nx + ex, qy = ny + ey;
              if ((ex == 0 && ey == 0) || qx < 0 || qy < 0 || qx >= w || qy >= h) {
                continue;
              }
              const size_t q = size_t(qy) * size_t(w) + size_t(qx);
              if (who[q] < 0 || ring[q] >= d) {
                continue;
              }
              const Region& r = s.regions[size_t(who[q])];
              const int kind = inArea(r, index, uOf(m), vOf(m)) ? shadeOf(r, hsl[m], px.data() + m * 4,
                                                                          px.data() + q * 4)
                                                                : 0;
              // (along a band of blend, as the one taken; never far from the part's hue, so a blend at a
              // button's rim doesn't lead over the button)
              const bool along = kind == 2 && same(m, q) &&
                                 (r.grey || std::fabs(hueDelta(hsl[m].h, r.hue)) <= r.half + r.soft + 15.f);
              const int deep = kind == 2 ? blend[q] + 1 : 0;
              if (kind == 0 || deep > kBlendReach || (kind == 2 && !along && !between(m, q))) {
                continue;
              }
              const float dh = r.grey ? 0.f : std::fabs(hueDelta(hsl[m].h, r.hue));
              if (dh < bestHue) {
                bestHue = dh;
                best = who[q];
                bestBlend = deep;
              }
            }
          }
          if (best >= 0) {
            next.push_back(uint32_t(m));
          }
          ring[m] = uint8_t(d);
          who[m] = int8_t(best); // (-1 stays -1; ring keeps it from being asked twice this ring)
          if (best >= 0) {
            wt[m] = 1.f;
            blend[m] = uint8_t(bestBlend);
          } else {
            ring[m] = 0;
          }
        }
      }
    }
    frontier.swap(next);
  }
  // 3. A texel still left out but mostly surrounded by one part (five of its eight neighbours or more: a
  // speck in a seam's blend, a lone texel of an odd colour in the middle) is that part's, when it is
  // of its hue family and not the outline; twice, for specks two texels wide.
  for (int pass = 0; pass < 2; pass++) {
    std::vector<std::pair<uint32_t, int8_t>> fills;
    for (size_t m = 0; m < texels; m++) {
      if (who[m] >= 0 || px[m * 4 + 3] == 0) {
        continue;
      }
      const int mx = int(m % size_t(w)), my = int(m / size_t(w));
      int count[16] = {}, around = 0, most = -1;
      for (int ey = -1; ey <= 1; ey++) {
        for (int ex = -1; ex <= 1; ex++) {
          const int nx = mx + ex, ny = my + ey;
          if ((ex == 0 && ey == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) {
            continue;
          }
          const int8_t o = who[size_t(ny) * size_t(w) + size_t(nx)];
          if (o >= 0 && o < 16) {
            around++;
            count[o]++;
          }
        }
      }
      for (int k = 0; k < 16; k++) {
        if (count[k] > 0 && (most < 0 || count[k] > count[most])) {
          most = k;
        }
      }
      if (around < 5 || most < 0) {
        continue;
      }
      const Region& r = s.regions[size_t(most)];
      // (its colour as shadeOf takes a part's shading or blend, but for the step: it is surrounded)
      if (!inArea(r, index, uOf(m), vOf(m)) || shadeOf(r, hsl[m], px.data() + m * 4, px.data() + m * 4) == 0) {
        continue;
      }
      fills.emplace_back(uint32_t(m), int8_t(most));
    }
    for (const auto& f : fills) {
      who[f.first] = f.second;
      wt[f.first] = 1.f;
    }
  }
  if (dumping()) {
    // (the check: a texel left out that touches a part and is of its colour, as the test would see it:
    // a spot. ~0 but for the outline's own blends)
    for (size_t texel = 0; texel < texels; texel++) {
      if (who[texel] >= 0 || px[texel * 4 + 3] == 0 || hsl[texel].l < 0.2f) {
        continue;
      }
      const int x = int(texel % size_t(w)), y = int(texel / size_t(w));
      bool spot = false;
      for (int dy = -1; dy <= 1 && !spot; dy++) {
        for (int dx = -1; dx <= 1 && !spot; dx++) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h || who[size_t(ny) * size_t(w) + size_t(nx)] < 0) {
            continue;
          }
          const Region& r = s.regions[size_t(who[size_t(ny) * size_t(w) + size_t(nx)])];
          spot = !r.grey && std::fabs(hueDelta(hsl[texel].h, r.hue)) <= r.half && hsl[texel].s >= r.satMin &&
                 hsl[texel].l <= r.lMax && !outlineFor(r, hsl[texel]) && inArea(r, index, uOf(texel), vOf(texel));
        }
      }
      sDumpStats[2] += spot;
    }
  }
  bool any = false;
  for (size_t texel = 0; texel < texels; texel++) {
    if (who[texel] < 0 || wt[texel] <= 0.f) {
      continue;
    }
    uint8_t* p = &px[texel * 4];
    const float rgb[3] = {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f};
    float out[3];
    toRgb(shift(s.regions[size_t(who[texel])], hsl[texel], to[size_t(who[texel])]), out);
    for (int k = 0; k < 3; k++) {
      p[k] = uint8_t(std::lround(std::clamp(rgb[k] + (out[k] - rgb[k]) * wt[texel], 0.f, 1.f) * 255.f));
    }
    if (dumping()) {
      sDumpStats[wt[texel] >= 1.f ? 0 : 1]++;
    }
    any = true;
  }
  return any;
}

constexpr int kMaxTextures = 1024;
PortTexInfo sInfo[kMaxTextures];

// --- the partners' textures, wherever they are loaded

constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull, kFnvPrime = 0x100000001B3ull; // FNV-1a 64

uint64_t fnv(const void* data, size_t n, uint64_t h = kFnvOffset) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < n; i++) {
    h = (h ^ p[i]) * kFnvPrime;
  }
  return h;
}

// the bytes a GX texture's level 0 takes (gen_cosmetic_textures.py's texel_bytes)
size_t texelBytes(const PortTexInfo& t) {
  int bw, bh, bb = 32;
  switch (t.format) {
  case GX_TF_I4: case GX_TF_C4: case GX_TF_CMPR: bw = 8; bh = 8; break;
  case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: bw = 8; bh = 4; break;
  case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_C14X2: bw = 4; bh = 4; break;
  case GX_TF_RGBA8: bw = 4; bh = 4; bb = 64; break;
  default: return 0;
  }
  return size_t((t.width + bw - 1) / bw) * size_t((t.height + bh - 1) / bh) * size_t(bb);
}

// a texture's content: its size and format, texels and palette (as the
// generator hashes the disc's)
uint64_t textureHash(const PortTexInfo& t) {
  const uint8_t head[3] = {uint8_t(t.width), uint8_t(t.height), uint8_t(t.format)};
  uint64_t h = fnv(head, 3);
  h = fnv(t.data, texelBytes(t), h);
  if (t.tlut != nullptr && (t.format == GX_TF_C4 || t.format == GX_TF_C8 || t.format == GX_TF_C14X2)) {
    h = fnv(t.tlut, size_t(t.tlutCount) * 2, h);
  }
  return h;
}

// what each texture seen is, by its address; checked against its first
// bytes, as another texture can be loaded where one was
struct Seen {
  int width, height, format;
  const void* tlut;
  uint64_t quick;
  int owner, index; // owner -1: none of theirs
};
std::unordered_map<const void*, Seen> sSeen;

struct Found {
  PortTexInfo info;
  int index;
};
std::vector<Found> sFound[kOwners];

void identify(const PortTexInfo& t) {
  const size_t bytes = texelBytes(t);
  if (t.data == nullptr || bytes == 0) {
    return;
  }
  const uint64_t quick = fnv(t.data, std::min<size_t>(bytes, 64));
  auto it = sSeen.find(t.data);
  if (it == sSeen.end() || it->second.width != t.width || it->second.height != t.height ||
      it->second.format != t.format || it->second.tlut != t.tlut || it->second.quick != quick) {
    if (sSeen.size() > 16384) {
      sSeen.clear();
    }
    Seen seen{t.width, t.height, t.format, t.tlut, quick, -1, -1};
    const uint64_t h = textureHash(t);
    const Known* k = std::lower_bound(std::begin(kKnown), std::end(kKnown), h,
                                      [](const Known& a, uint64_t v) { return a.hash < v; });
    if (k != std::end(kKnown) && k->hash == h) {
      seen.owner = k->owner;
      seen.index = k->index;
    }
    it = sSeen.insert_or_assign(t.data, seen).first;
  }
  if (it->second.owner >= 0) {
    sFound[it->second.owner].push_back(Found{t, it->second.index});
  }
}

// every loaded texture file's textures, the partners' among them found
void scanOwners() {
  for (auto& f : sFound) {
    f.clear();
  }
  for (int slot = 0;; slot++) {
    const int n = portAnimTexturesAt(slot, sInfo, kMaxTextures);
    if (n < 0) {
      break;
    }
    for (int i = 0; i < n; i++) {
      identify(sInfo[i]);
    }
  }
}

void updateSet(Set& s, const int64_t* colours) {
  bool wanted = probe().owner >= 0 && probe().owner == s.owner;
  std::string key;
  for (const Region& r : s.regions) {
    const int64_t c = regionColour(r, colours);
    wanted |= c >= 0;
    key += std::to_string(c) + ",";
  }
  // the textures, each with its place (in the partner's own file, for theirs)
  static std::vector<Found> sList;
  sList.clear();
  if (s.pick >= 0 && portPlayAs() != s.pick) {
    wanted = false; // (the disc's model of someone not picked: an NPC's)
  }
  if (wanted) {
    if (s.owner >= 0) {
      sList = sFound[s.owner];
    } else {
      int n = s.face >= 0       ? portPlayAsFaceTextures(s.face, sInfo, kMaxTextures)
              : s.file != nullptr ? portAnimTextures(s.file, sInfo, kMaxTextures)
                                  : portHudIconTextures(s.hudPart, sInfo, kMaxTextures);
      if (n == 0 && s.file != nullptr) {
        n = portAnimTextures((std::string("/") + s.file).c_str(), sInfo, kMaxTextures); // (ours, made as /a/...)
      }
      for (int i = 0; i < n; i++) {
        sList.push_back(Found{sInfo[i], i});
      }
    }
  }
  if (sList.empty()) {
    if (!s.key.empty()) {
      clearSet(s);
    }
    return;
  }
  // the addresses are part of it: the file loaded again, or moved
  uint64_t h = kFnvOffset;
  for (const Found& f : sList) {
    h = (h ^ uint64_t(reinterpret_cast<uintptr_t>(f.info.data))) * kFnvPrime;
  }
  key += std::to_string(sList.size()) + "@" + std::to_string(h);
  if (key == s.key) {
    return;
  }
  clearSet(s);
  s.applied.reserve(sList.size()); // the pixels stay where Aurora was told they are
  for (const Found& f : sList) {
    Set::Tex tex;
    sDumpStats[0] = sDumpStats[1] = sDumpStats[2] = 0;
    if (!recolour(s, f.index, f.info, colours, tex.pixels)) {
      continue;
    }
    if (const char* dir = dumping()) {
      std::vector<uint8_t> was;
      const std::string base = std::string(dir) + "/" + setName(s) + "_" + std::to_string(f.index);
      if (decode(f.info, was)) {
        dumpPng(base + "_was.png", was, f.info.width, f.info.height);
      }
      dumpPng(base + "_now.png", tex.pixels, f.info.width, f.info.height);
      std::printf("[cosmetics] dump %s #%d: %d texels whole, %d in part, %d spots\n", setName(s).c_str(), f.index,
                  sDumpStats[0], sDumpStats[1], sDumpStats[2]);
      std::fflush(stdout);
    }
    s.applied.push_back(std::move(tex));
    Set::Tex& a = s.applied.back();
    a.reg = aurora::texture::register_replacement(
        aurora::texture::TexturePointerKey{f.info.data},
        aurora::texture::RawTextureReplacement{
            .bytes = a.pixels,
            .width = static_cast<uint32_t>(f.info.width),
            .height = static_cast<uint32_t>(f.info.height),
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
  // the partners' textures looked for only while one of theirs is coloured
  bool owners = probe().owner >= 0;
  for (const Set& s : sSets) {
    for (const Region& r : s.regions) {
      owners |= s.owner >= 0 && regionColour(r, colours) >= 0;
    }
  }
  if (owners) {
    scanOwners();
  } else {
    for (auto& f : sFound) {
      f.clear();
    }
  }
  for (Set& s : sSets) {
    updateSet(s, colours);
  }
}

// ---------------------------------------------------------------- the page

void setAll(const int* choices) {
  for (int i = 0; i < kSlots; i++) {
    if (sSlots[i].choice != nullptr) {
      *sSlots[i].choice = choices[i] >= 0 && choices[i] < kColours ? choices[i] : 0;
    }
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
                               {"Fire Mario", "White", "Red"},
                               // the Gold Medal's (postgame_extra.h): listed once the file has it
                               {"Gold Mario", "Gold", "Gold"}};
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

void section(int) {
  settle();
  // the Play as pick's looks, as the Wardrobe's (Luigi's own colours while he is picked: Mario's then would
  // change nothing seen); the gold look with the Gold Medal (portCosmeticsLookCount)
  for (int i = 1; i < portCosmeticsLookCount(); i++) {
    if (i > 1) {
      ImGui::SameLine();
    }
    if (ImGui::Button(portTrLabel(portCosmeticsLookName(i)))) {
      portCosmeticsSetLook(i);
    }
  }
  if (portCosmeticsLookCount() == 0) {
    ImGui::TextDisabled("%s", portTr("Looks are for Mario and Luigi: change the parts below."));
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
    *sTheme = 0;
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
  // Not settled here: settings.txt isn't read yet, and Vivian would take the
  // partner's default instead of the colour a player picked before her slot
  // existed (then saved as hers). Settled where it's used (each frame's tick,
  // the Wardrobe), and as the page shows her row, so it never shows a -1.
  portSettingsSetShown(sSlots[kVivian].key, [] {
    settle();
    return 1;
  });
  for (int i = 0; i < kThemeCount; i++) {
    kThemeNames[i] = kThemes[i].name;
  }
  sTheme = portSettingsDeclare("hud_theme", 0, 0, kThemeCount - 1);
  portSettingsAddChoice("Cosmetics", "Theme",
                        "The status bar's banners and the menus' pages in colours that go together. A part with "
                        "a colour of its own keeps it.",
                        0, "hud_theme", kThemeNames, kThemeCount,
                        "hud theme status bar menu banner skin colour color night forest ocean candy contrast");
  portSettingsAddSection("Cosmetics", "Looks and colour picker",
                         "Mario as his brother or his rivals in one press, every colour at random or back to the "
                         "game's own, and each part's Custom colour. Only how things look: each player sees their own.",
                         0, section, "cosmetics skin outfit palette random reset luigi wario waluigi fire colour color");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace

// the achievements' icons (achievements.cpp): a game texture as RGBA8 rows, false for a format not read here
bool portCosmeticsDecode(const PortTexInfo& t, std::vector<uint8_t>& out) { return decode(t, out); }

// --- The Start menu's Wardrobe (port/include/port/cosmetics.h)

extern "C" int portCosmeticsSlotCount(void) { return kSlots; }
extern "C" int portCosmeticsSlotPick(int s) { return slotPick(s); }
extern "C" const char* portCosmeticsSlotLabel(int s) { return s >= 0 && s < kSlots ? sSlots[s].label : ""; }
extern "C" const char* portCosmeticsSlotNote(int s) { return s >= 0 && s < kSlots ? sSlots[s].note : nullptr; }
extern "C" int portCosmeticsChoice(int s) { return choiceOf(s); }

extern "C" void portCosmeticsStep(int s, int dir) {
  if (s < 0 || s >= kSlots || sSlots[s].choice == nullptr) {
    return;
  }
  *sSlots[s].choice = ((choiceOf(s) + dir) % kColours + kColours) % kColours;
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

// The Wardrobe's looks are the Play as pick's: Mario's above, or Luigi's (his
// own, then as his brother, the rivals and Fire Luigi); Peach and Bowser have
// none (their parts are their own rows)
constexpr Preset kLuigiPresets[] = {{"Original", "Original", "Original"},
                                    {"Mario", "Red", "Blue"},
                                    {"Wario", "Wario yellow", "Waluigi purple"},
                                    {"Waluigi", "Waluigi purple", "Black"},
                                    {"Fire Luigi", "White", "Luigi green"},
                                    {"Gold Luigi", "Gold", "Gold"}};
struct Looks {
  const Preset* presets;
  int count;
  int cap, overalls;
};

Looks looksNow() {
  switch (portPlayAs()) {
  // (the last, the gold look, only with the Gold Medal: every Tattle, badge and recipe)
  case PORT_PLAY_AS_MARIO: return Looks{kPresets, kPresetCount - (portGoldMedal() ? 0 : 1), kCap, kOveralls};
  case PORT_PLAY_AS_LUIGI:
    return Looks{kLuigiPresets, int(sizeof(kLuigiPresets) / sizeof(kLuigiPresets[0])) - (portGoldMedal() ? 0 : 1),
                 kLuigiCap, kLuigiOveralls};
  default: return Looks{nullptr, 0, kCap, kOveralls};
  }
}

extern "C" int portCosmeticsLookCount(void) { return looksNow().count; }
extern "C" const char* portCosmeticsLookName(int i) {
  const Looks l = looksNow();
  return i >= 0 && i < l.count ? l.presets[i].label : "?";
}

extern "C" int portCosmeticsLook(void) {
  const Looks l = looksNow();
  for (int i = 0; i < l.count; i++) {
    if (portCosmeticsChoice(l.cap) == paletteIndex(l.presets[i].cap) &&
        portCosmeticsChoice(l.overalls) == paletteIndex(l.presets[i].overalls)) {
      return i;
    }
  }
  return -1;
}

extern "C" void portCosmeticsSetLook(int i) {
  const Looks l = looksNow();
  if (i < 0 || i >= l.count || sSlots[l.cap].choice == nullptr || sSlots[l.overalls].choice == nullptr) {
    return;
  }
  *sSlots[l.cap].choice = paletteIndex(l.presets[i].cap);
  *sSlots[l.overalls].choice = paletteIndex(l.presets[i].overalls);
  // (the tests' record: whose slots the look wrote)
  std::printf("[cosmetics] look %s: %s %s, %s %s\n", l.presets[i].label, sSlots[l.cap].label,
              kPalette[choiceOf(l.cap)].name, sSlots[l.overalls].label, kPalette[choiceOf(l.overalls)].name);
  std::fflush(stdout);
  portSettingsSave();
}

extern "C" int portCosmeticsThemeCount(void) { return kThemeCount; }
extern "C" const char* portCosmeticsThemeName(int t) { return t >= 0 && t < kThemeCount ? kThemes[t].name : "?"; }
extern "C" int portCosmeticsTheme(void) {
  return sTheme != nullptr && *sTheme >= 0 && *sTheme < kThemeCount ? *sTheme : 0;
}

extern "C" void portCosmeticsStepTheme(int dir) {
  if (sTheme == nullptr) {
    return;
  }
  *sTheme = ((portCosmeticsTheme() + dir) % kThemeCount + kThemeCount) % kThemeCount;
  portSettingsSave();
}

extern "C" int portCosmeticsSlotThemed(int s) {
  return s >= 0 && s < kSlots && sSlots[s].choice != nullptr && choiceOf(s) == 0 && themeColour(s) >= 0;
}

// The Start menu's page colours (win_root.c's winBgGX): the menus' colour as
// the game's three shades of a page, deep, middle and light; the light ones
// light whatever the colour, for the black text on them
extern "C" int portCosmeticsMenuShades(unsigned* rgb) {
  const int64_t c = slotColour(kHudMenu);
  if (c < 0) {
    return 0;
  }
  const Hsl base = toHsl(float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f, float(c & 255) / 255.f);
  const Hsl shades[3] = {
      {base.h, std::min(base.s, 0.7f), std::clamp(base.l, 0.3f, 0.55f)},
      {base.h, std::min(base.s, 0.7f) * 0.8f, 0.73f},
      {base.h, std::min(base.s, 0.7f) * 0.6f, 0.87f},
  };
  for (int i = 0; i < 3; i++) {
    float o[3];
    toRgb(shades[i], o);
    rgb[i] = unsigned(std::lround(o[0] * 255.f)) << 16 | unsigned(std::lround(o[1] * 255.f)) << 8 |
             unsigned(std::lround(o[2] * 255.f));
  }
  return 1;
}
