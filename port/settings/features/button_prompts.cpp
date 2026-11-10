// Button prompts that match the controller (Controls tab): the game's
// pictures of the GameCube's A, B, X, Y, Z, L, R and Start, in its text
// (<icon PAD_A>), menus and action commands, drawn instead as the buttons
// that do those things now: an Xbox, PlayStation, Switch or Steam Deck controller's (by
// where the button is, so a remapped button shows its new place), or the
// keyboard's keys. Auto follows whatever was used last; a GameCube
// controller through an adapter keeps the game's own pictures.
//
// The game's icon textures (icondrv.c, portButtonIconTextures) are replaced
// by address with Paper Cut's own drawings (port/settings/button_glyphs.cpp),
// four times the icon's size; a texture pack's own icons come first.
#include <dolphin/gx.h> // GXTexObj, for aurora/texture.hpp
#include <aurora/texture.hpp>
#include <dolphin/pad.h>

#include "port/button_markup.h"
#include "port/menu_buttons.h"
#include "port/settings.h"
#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
int portButtonIconTextures(int button, int pressed, const void** data, int* width, int* height, int max);
extern const unsigned char portFontBold[];
extern const unsigned int portFontBold_size;
}

namespace {

enum Choice { kAuto, kOriginal, kXbox, kPlayStation, kSwitch, kKeyboard, kSteamDeck };
constexpr const char* kChoices[] = {"Auto (the controller used last)", "GameCube (the game's own)", "Xbox",
                                    "PlayStation", "Switch", "Keyboard", "Steam Deck"};

constexpr u16 kPadButtons[8] = {PAD_BUTTON_A,  PAD_BUTTON_B,  PAD_BUTTON_X,  PAD_BUTTON_Y,
                                PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_START};

int* sChoice;
// the last input: 0 none yet, 1 the keyboard, 2 a controller (sPadStyle, once one is used)
int sLastInput;
bool sPadSeen;
GlyphStyle sPadStyle = GlyphStyle::Xbox;

struct Image {
  std::vector<uint8_t> pixels;
  aurora::texture::ReplacementRegistration reg;
};
std::vector<Image> sImages;
std::string sApplied; // what's registered now ("" none)

// the glyphs drawn this run, by what they show and their size: a menu opening or closing switches the game's
// A, B, X and Y pictures (the menu layer) without drawing them all again
std::vector<uint8_t> glyphOf(const GlyphSpec& g, int w, int h) {
  static std::unordered_map<std::string, std::vector<uint8_t>> sDrawn;
  const std::string key = std::to_string(static_cast<int>(g.style)) + "/" + std::to_string(static_cast<int>(g.face)) +
                          "/" + (g.label != nullptr ? g.label : "") + "/" + (g.pressed ? "1" : "0") + "/" +
                          std::to_string(g.textScale) + "/" + std::to_string(w) + "x" + std::to_string(h);
  if (auto it = sDrawn.find(key); it != sDrawn.end()) {
    return it->second;
  }
  if (sDrawn.size() > 512) {
    sDrawn.clear();
  }
  return sDrawn.emplace(key, drawButtonGlyph(g, w, h)).first->second;
}

void clear() {
  for (Image& i : sImages) {
    if (i.reg.id != 0) {
      aurora::texture::unregister_replacement(i.reg);
    }
  }
  sImages.clear();
  sApplied.clear();
}

bool havePad() { return PADGetIndexForPort(0) >= 0; }

// a controller made as a GameCube one: through an adapter, Nintendo's own for the Switch (057E:2073), or
// another maker's in its shape (an 8BitDo's, named so): the game's own pictures are its buttons
bool gameCubeLike(unsigned port) {
  if (PADIsGCAdapter(port)) {
    return true;
  }
  const s32 idx = PADGetIndexForPort(port);
  SDL_Gamepad* pad = idx >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(idx)) : nullptr;
  if (pad == nullptr) {
    return false;
  }
  if (SDL_GetGamepadVendor(pad) == 0x057E && SDL_GetGamepadProduct(pad) == 0x2073) {
    return true;
  }
  const char* n = SDL_GetGamepadName(pad);
  return n != nullptr && (SDL_strcasestr(n, "gamecube") != nullptr || SDL_strcasestr(n, "ngc") != nullptr);
}

// $TTYD_PAD_TYPE=C:TYPE[,...] (tests): controller C (1-4) taken as that kind (xbox, ps, switch, deck, gc,
// keyboard), there or not. 1 (*out set), 0 the game's own pictures, -1 not given
int testType(unsigned port, GlyphStyle* out) {
  static const char* sList = std::getenv("TTYD_PAD_TYPE");
  for (const char* p = sList; p != nullptr && *p != '\0';) {
    unsigned c = 0;
    char type[16] = {};
    if (std::sscanf(p, "%u:%15[a-z]", &c, type) == 2 && c == port + 1) {
      const std::string t = type;
      if (t == "gc") {
        return 0;
      }
      *out = t == "ps"         ? GlyphStyle::PlayStation
             : t == "switch"   ? GlyphStyle::Switch
             : t == "deck"     ? GlyphStyle::SteamDeck
             : t == "keyboard" ? GlyphStyle::Keyboard
                               : GlyphStyle::Xbox;
      return 1;
    }
    p = std::strchr(p, ',');
    p = p != nullptr ? p + 1 : nullptr;
  }
  return -1;
}

// the style to draw now, or false: the game's own pictures
bool wantedStyle(GlyphStyle* out) {
  int c = *sChoice;
  if (c == kAuto) {
    if (const int t = testType(0, out); t >= 0) {
      return t == 1;
    }
    if (havePad() && gameCubeLike(0)) {
      return false; // a GameCube controller: its own buttons
    }
    if (sLastInput == 2 || (sLastInput == 0 && havePad())) {
      *out = sPadSeen ? sPadStyle : glyphStyleOfPort(0);
      return true;
    }
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    if (sLastInput != 1) {
      return false; // touch: the on-screen buttons are the GameCube's
    }
#endif
    *out = GlyphStyle::Keyboard;
    return true;
  }
  switch (c) {
  case kXbox: *out = GlyphStyle::Xbox; return true;
  case kPlayStation: *out = GlyphStyle::PlayStation; return true;
  case kSwitch: *out = GlyphStyle::Switch; return true;
  case kKeyboard: *out = GlyphStyle::Keyboard; return true;
  case kSteamDeck: *out = GlyphStyle::SteamDeck; return true;
  default: return false;
  }
}

// a key's name, short enough for a key cap
std::string keyLabel(s32 scancode) {
  if (scancode < 0) {
    return "?";
  }
  std::string n = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
  static const struct {
    const char* from;
    const char* to;
  } kShort[] = {{"Space", "SPC"},     {"Return", "ENT"},     {"Enter", "ENT"},       {"Escape", "ESC"},
                {"Backspace", "BKS"}, {"Left Shift", "SHF"}, {"Right Shift", "SHF"}, {"Left Ctrl", "CTL"},
                {"Right Ctrl", "CTL"}, {"Left Alt", "ALT"},  {"Right Alt", "ALT"},   {"Tab", "TAB"},
                {"Up", "\x5E"},       {"CapsLock", "CAP"}};
  for (const auto& k : kShort) {
    if (n == k.from) {
      return k.to;
    }
  }
  if (n.rfind("Keypad ", 0) == 0) {
    n = n.substr(7);
  }
  if (n.size() > 3) {
    n.resize(3);
  }
  for (char& ch : n) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return n.empty() ? "?" : n;
}

GlyphFace faceOf(u32 native, int button) {
  switch (native) {
  case SDL_GAMEPAD_BUTTON_SOUTH: return GlyphFace::South;
  case SDL_GAMEPAD_BUTTON_EAST: return GlyphFace::East;
  case SDL_GAMEPAD_BUTTON_WEST: return GlyphFace::West;
  case SDL_GAMEPAD_BUTTON_NORTH: return GlyphFace::North;
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return GlyphFace::LeftShoulder;
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GlyphFace::RightShoulder;
  case SDL_GAMEPAD_BUTTON_START: return GlyphFace::Start;
  case SDL_GAMEPAD_BUTTON_BACK: return GlyphFace::Back;
  case PAD_NATIVE_BUTTON_INVALID:
    // on an axis: the triggers
    return button == 5 ? GlyphFace::LeftTrigger : button == 6 ? GlyphFace::RightTrigger : GlyphFace::Other;
  default: return GlyphFace::Other;
  }
}

// each button's glyph as it would be drawn now, and a key naming them all
// (defaults: a controller's buttons where they are out of the box, not port's mapping)
std::string plan(GlyphStyle style, GlyphSpec specs[8], std::string labels[8], unsigned port = 0,
                 bool defaults = false) {
  std::string key = std::to_string(static_cast<int>(style));
  u32 count = 0;
  if (style == GlyphStyle::Keyboard && !defaults) {
    PADKeyButtonBinding* keys = PADGetKeyButtonBindings(port, &count);
    for (int b = 0; b < 8; b++) {
      s32 sc = -1;
      for (u32 i = 0; keys != nullptr && i < count; i++) {
        if (keys[i].padButton == kPadButtons[b]) {
          sc = keys[i].scancode;
        }
      }
      labels[b] = keyLabel(sc);
      specs[b].face = GlyphFace::Other;
      key += "|" + labels[b];
    }
  } else {
    PADButtonMapping* maps = defaults ? nullptr : PADGetButtonMappings(port, &count);
    static const u32 kDefaults[8] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
                                     SDL_GAMEPAD_BUTTON_EAST,  SDL_GAMEPAD_BUTTON_NORTH,
                                     SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_NATIVE_BUTTON_INVALID,
                                     PAD_NATIVE_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_START};
    u32 natives[8];
    for (int b = 0; b < 8; b++) {
      natives[b] = maps != nullptr ? PAD_NATIVE_BUTTON_INVALID : kDefaults[b];
      for (u32 i = 0; maps != nullptr && i < count; i++) {
        if (maps[i].padButton == kPadButtons[b]) {
          natives[b] = maps[i].nativeButton;
        }
      }
    }
    // a menu up (the menu layer, port/include/port/menu_buttons.h): the game's A, B, X and Y are the
    // controller's own confirm, back and third there, so their pictures are those buttons
    int to[4];
    if (!defaults && portMenuLayerPictures(static_cast<int>(port)) && portMenuLayerMap(static_cast<int>(port), to)) {
      const u32 was[4] = {natives[0], natives[1], natives[2], natives[3]};
      for (int i = 0; i < 4; i++) {
        natives[to[i]] = was[i];
      }
      key += "|menu";
    }
    for (int b = 0; b < 8; b++) {
      const u32 native = natives[b];
      specs[b].face = faceOf(native, b);
      if (native == PAD_NATIVE_BUTTON_INVALID && maps != nullptr && (b == 5 || b == 6)) {
        // L or R on an axis: the trigger it is now (Controls > Bindings can put L on the right one)
        u32 na = 0;
        const PADAxisMapping* axes = PADGetAxisMappings(port, &na);
        const u16 want = b == 5 ? PAD_AXIS_TRIGGER_L : PAD_AXIS_TRIGGER_R;
        for (u32 i = 0; axes != nullptr && i < na; i++) {
          if (axes[i].padAxis == want) {
            const int ax = axes[i].nativeAxis.nativeAxis;
            if (ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || ax == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
              specs[b].face = ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? GlyphFace::LeftTrigger : GlyphFace::RightTrigger;
            }
            key += "|a" + std::to_string(ax);
          }
        }
      }
      if (specs[b].face == GlyphFace::Other) {
        // a short name on the pill: L3, R4, DU (the d-pad's up), Sha(re)
        std::string n = nativeButtonLabel(style, native, kPadButtons[b]);
        if (n.rfind("D-pad ", 0) == 0) {
          n = std::string("D") + static_cast<char>(std::toupper(static_cast<unsigned char>(n[6])));
        } else if (n.rfind("Paddle ", 0) == 0) {
          n = "P" + n.substr(7);
        }
        labels[b] = n.substr(0, 3);
      }
      key += "|" + std::to_string(native);
    }
  }
  for (int b = 0; b < 8; b++) {
    specs[b].style = style;
    specs[b].label = labels[b].empty() ? nullptr : labels[b].c_str();
  }
  return key;
}

// --- players 2-4's own button icons (PORT_ICON_PAD_BASE, port/include/port/button_markup.h) ---

// a port's style (0: player 1's, wantedStyle), or false: the game's own pictures. Players 2-4 follow
// their own controller (Auto, and with a fixed style too: that one is player 1's); GameCube keeps
// everyone's the game's own; no controller there (a test's scripted one): the game's own
bool styleFor(unsigned port, GlyphStyle* out) {
  if (port == 0) {
    return wantedStyle(out);
  }
  if (*sChoice == kOriginal) {
    return false;
  }
  if (const int t = testType(port, out); t >= 0) {
    return t == 1;
  }
  if (PADGetIndexForPort(port) < 0 || gameCubeLike(port)) {
    return false;
  }
  *out = glyphStyleOfPort(port);
  if (*out == GlyphStyle::Generic) {
    *out = GlyphStyle::Xbox;
  }
  return true;
}

// what a port's buttons look like now, as a key: "gc" (the game's own), else its style and mapping
std::string lookOf(unsigned port, GlyphStyle* style, GlyphSpec specs[8], std::string labels[8]) {
  if (!styleFor(port, style)) {
    return "gc";
  }
  return plan(*style, specs, labels, port);
}

struct PortIcons {
  std::string key;  // its look drawn ("" none yet)
  bool own = false; // drawn as its own (its look isn't player 1's)
  uint8_t* tex[16] = {};
  int w[16] = {}, h[16] = {};
  // the buffers tex points into: this look's, and the one before (a frame may still draw
  // one of those); older ones freed as a new look is drawn, so a controller plugged in and
  // out, or a pad sleeping and waking, all session long doesn't keep every set it ever had
  std::vector<std::unique_ptr<uint8_t[]>> pixels, before;
};
PortIcons sPorts[PAD_MAX_CONTROLLERS];

// RGBA8 rows to the GameCube's RGBA8 (4x4 tiles: their AR pairs, then their GB pairs), 32-byte aligned
uint8_t* tiled(std::vector<std::unique_ptr<uint8_t[]>>& keep, const std::vector<uint8_t>& rgba, int w, int h) {
  keep.push_back(std::make_unique<uint8_t[]>(static_cast<size_t>(w) * h * 4 + 32));
  uint8_t* base = keep.back().get();
  uint8_t* o = base + ((32 - (reinterpret_cast<uintptr_t>(base) & 31)) & 31);
  uint8_t* t = o;
  for (int by = 0; by < h; by += 4) {
    for (int bx = 0; bx < w; bx += 4) {
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          const uint8_t* px = &rgba[(static_cast<size_t>(by + y) * w + bx + x) * 4];
          const int k = (y * 4 + x) * 2;
          t[k] = px[3];
          t[k + 1] = px[0];
          t[32 + k] = px[1];
          t[32 + k + 1] = px[2];
        }
      }
      t += 64;
    }
  }
  return o;
}

// "a controller" (the text's {PADSTART} & co., button_markup.cpp) while player 1 plays on the keyboard: the
// first controller connected, its buttons where they are out of the box; none (or GameCube chosen): the
// GameCube's own. False: player 1's own look is a controller's or the game's (the plain tokens show it).
bool anyPadLook(GlyphStyle* style, bool* gc) {
  GlyphStyle one = GlyphStyle::GameCube;
  if (sChoice == nullptr || !wantedStyle(&one) || one != GlyphStyle::Keyboard) {
    return false;
  }
  *gc = true;
  *style = GlyphStyle::GameCube;
  if (*sChoice == kKeyboard) {
    return true; // (the keyboard's chosen for the prompts: a controller is a GameCube one)
  }
  int n = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
    if (n > 0) {
      *style = glyphStyleOf(SDL_GetGamepadTypeForID(ids[0]), SDL_GetGamepadVendorForID(ids[0]),
                            SDL_GetGamepadProductForID(ids[0]));
      *style = *style == GlyphStyle::Generic ? GlyphStyle::Xbox : *style;
      *gc = *style == GlyphStyle::GameCube;
    }
    SDL_free(ids);
  }
  return true;
}

void drawSet(PortIcons& pi, const std::string& key, const GlyphSpec specs[8], unsigned port);

void updatePorts(const std::string& playerOne) {
  {
    // port 0's set: "a controller", for player 1 on the keyboard
    PortIcons& pi = sPorts[0];
    GlyphStyle style = GlyphStyle::GameCube;
    bool gc = true;
    pi.own = anyPadLook(&style, &gc);
    if (pi.own) {
      GlyphSpec specs[8];
      std::string labels[8];
      const std::string key = gc ? std::string("gc") : plan(style, specs, labels, 0, true);
      if (key != pi.key) {
        drawSet(pi, key, specs, 0);
      }
    }
  }
  for (unsigned port = 1; port < PAD_MAX_CONTROLLERS; port++) {
    PortIcons& pi = sPorts[port];
    GlyphStyle style = GlyphStyle::GameCube;
    GlyphSpec specs[8];
    std::string labels[8];
    const std::string key = lookOf(port, &style, specs, labels);
    pi.own = key != playerOne;
    if (!pi.own || key == pi.key) {
      continue;
    }
    drawSet(pi, key, specs, port);
  }
}

// a port's sixteen icons (eight buttons, up and pressed) drawn in its look
void drawSet(PortIcons& pi, const std::string& key, const GlyphSpec specs[8], unsigned port) {
  {
    setGlyphFont(portFontBold, portFontBold_size);
    pi.before = std::move(pi.pixels); // (the set before that one freed here)
    pi.pixels.clear();
    int made = 0;
    for (int b = 0; b < 8; b++) {
      for (int pressed = 0; pressed < 2; pressed++) {
        const void* data[1] = {nullptr};
        int w[1] = {0}, h[1] = {0};
        if (portButtonIconTextures(b, pressed, data, w, h, 1) == 0 || w[0] <= 0 || h[0] <= 0) {
          continue;
        }
        GlyphSpec g;
        if (key == "gc") {
          g.style = GlyphStyle::GameCube;
          g.face = static_cast<GlyphFace>(static_cast<int>(GlyphFace::GcA) + b);
        } else {
          g = specs[b];
        }
        g.pressed = pressed != 0;
        const int W = std::min(w[0] * 4, 256) & ~3, H = std::min(h[0] * 4, 256) & ~3;
        const int i = b * 2 + pressed;
        pi.tex[i] = tiled(pi.pixels, glyphOf(g, W, H), W, H);
        pi.w[i] = W;
        pi.h[i] = H;
        made++;
      }
    }
    if (made == 16) {
      pi.key = key;
      if (port == 0) {
        std::printf("[prompts] a controller's buttons, player 1 on the keyboard (%s)\n",
                    key == "gc" ? "GameCube" : key.c_str());
      } else {
        std::printf("[prompts] controller %u's buttons: its own (%s)\n", port + 1, key == "gc" ? "GameCube" : key.c_str());
      }
      std::fflush(stdout);
    } else {
      pi.own = false; // (the icon sheet not in yet: at the next look)
    }
  }
}

void update() {
  {
    GlyphStyle one = GlyphStyle::GameCube;
    GlyphSpec specs[8];
    std::string labels[8];
    updatePorts(lookOf(0, &one, specs, labels));
  }
  GlyphStyle style;
  if (!wantedStyle(&style)) {
    if (!sApplied.empty()) {
      clear();
    }
    return;
  }
  GlyphSpec specs[8];
  std::string labels[8];
  std::string key = plan(style, specs, labels);
  // the textures' addresses are part of it: the icon sheet loaded again
  const void* first[1] = {nullptr};
  int fw[1], fh[1];
  if (portButtonIconTextures(0, 0, first, fw, fh, 1) == 0) {
    return; // not loaded yet
  }
  char at[32];
  std::snprintf(at, sizeof(at), "@%p", first[0]);
  key += at;
  if (key == sApplied) {
    return;
  }
  clear();
  sImages.reserve(64); // the pixels stay where Aurora was told they are
  setGlyphFont(portFontBold, portFontBold_size);
  for (int b = 0; b < 8; b++) {
    for (int pressed = 0; pressed < 2; pressed++) {
      const void* data[8];
      int w[8], h[8];
      const int n = portButtonIconTextures(b, pressed, data, w, h, 8);
      for (int i = 0; i < n; i++) {
        if (data[i] == nullptr || w[i] <= 0 || h[i] <= 0) {
          continue;
        }
        GlyphSpec g = specs[b];
        g.pressed = pressed != 0;
        const int W = std::min(w[i] * 4, 256), H = std::min(h[i] * 4, 256);
        sImages.push_back(Image{glyphOf(g, W, H), {}});
        Image& img = sImages.back();
        img.reg = aurora::texture::register_replacement(
            aurora::texture::TexturePointerKey{data[i]},
            aurora::texture::RawTextureReplacement{
                .bytes = img.pixels,
                .width = static_cast<uint32_t>(W),
                .height = static_cast<uint32_t>(H),
                .mipCount = 1,
                .gxFormat = GX_TF_RGBA8_PC,
                .label = "Button prompts",
            },
            aurora::texture::ReplacementOptions{.priority = -1}); // a texture pack's icons first
      }
    }
  }
  sApplied = key;
}

// a few times a second: the controller, its mapping or the choice may have changed
void tick(int) {
  static Uint64 sLast;
  static unsigned sLayers = 0;
  const Uint64 now = SDL_GetTicks();
  // (a menu opened or closed: its pictures at once, the menu layer's, port/include/port/menu_buttons.h)
  unsigned layers = 0;
  for (int p = 0; p < PAD_MAX_CONTROLLERS; p++) {
    int to[4];
    layers |= portMenuLayerPictures(p) && portMenuLayerMap(p, to) ? 1u << p : 0u;
  }
  if (now - sLast < 250 && layers == sLayers) {
    return;
  }
  sLast = now;
  sLayers = layers;
  update();
}

int inputEvent(const void* ev) {
  const SDL_Event* e = static_cast<const SDL_Event*>(ev);
  switch (e->type) {
  case SDL_EVENT_KEY_DOWN:
    sLastInput = 1;
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(e->gaxis.value) < 16000) {
      break;
    }
    if (SDL_Gamepad* other = SDL_GetGamepadFromID(e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? e->gbutton.which
                                                                                           : e->gaxis.which);
        other != nullptr && SDL_GetGamepadPlayerIndex(other) > 0) {
      break; // (players 2-4's controllers show their own buttons, styleFor: player 1's prompts stay theirs)
    }
    sLastInput = 2;
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? e->gbutton.which
                                                                                         : e->gaxis.which)) {
      sPadStyle = glyphStyleOf(SDL_GetGamepadType(pad), SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad));
      sPadSeen = true;
    }
    break;
  default:
    break;
  }
  return 0; // only watching
}

struct ButtonPromptSettings {
  ButtonPromptSettings() {
    sChoice = portSettingsDeclare("button_prompts", kAuto, kAuto, kSteamDeck);
    portSettingsAddChoice("Controls", "Button prompts",
                          "The buttons the game shows (in its text, menus and action commands) drawn as the ones "
                          "that do those things on your controller or keyboard, where you've mapped them.",
                          0, "button_prompts", kChoices, 7,
                          "button prompts icons glyphs xbox playstation ps4 ps5 switch steam deck keyboard controller");
    portSettingsAddOverlay(tick, inputEvent);
  }
} sButtonPrompts;

} // namespace

// the text's button icons (port/settings/features/button_markup.cpp) draw what the prompts do
bool buttonPromptsStyle(GlyphStyle* out) { return sChoice != nullptr && wantedStyle(out); }

void buttonPromptsPlan(GlyphStyle style, GlyphSpec specs[8], std::string labels[8]) { plan(style, specs, labels); }

bool buttonPromptsStyleFor(unsigned port, GlyphStyle* out) { return sChoice != nullptr && styleFor(port, out); }

void buttonPromptsPlanFor(unsigned port, GlyphStyle style, GlyphSpec specs[8], std::string labels[8]) {
  plan(style, specs, labels, port);
}

bool buttonPromptsOwnIcons(unsigned port) {
  return port < PAD_MAX_CONTROLLERS && sPorts[port].own && !sPorts[port].key.empty();
}

bool buttonPromptsAnyPad(GlyphStyle* style) {
  bool gc = true;
  if (!anyPadLook(style, &gc)) {
    return false;
  }
  if (gc) {
    *style = GlyphStyle::GameCube;
  }
  return true;
}

extern "C" int portPadIconTex(unsigned iconId, void* texObj) {
  if (iconId < PORT_ICON_PAD_BASE || iconId >= PORT_ICON_PAD_BASE + PORT_ICON_PAD_COUNT) {
    return 0;
  }
  const unsigned port = (iconId - PORT_ICON_PAD_BASE) >> 4, i = (iconId - PORT_ICON_PAD_BASE) & 15u;
  if (!buttonPromptsOwnIcons(port) || sPorts[port].tex[i] == nullptr) {
    return 0;
  }
  GXTexObj* obj = static_cast<GXTexObj*>(texObj);
  GXInitTexObj(obj, sPorts[port].tex[i], static_cast<u16>(sPorts[port].w[i]), static_cast<u16>(sPorts[port].h[i]),
               GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GXInitTexObjLOD(obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
  return 1;
}
