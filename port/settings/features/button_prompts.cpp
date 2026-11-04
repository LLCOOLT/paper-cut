// Button prompts that match the controller (Controls tab): the game's
// pictures of the GameCube's A, B, X, Y, Z, L, R and Start, in its text
// (<icon PAD_A>), menus and action commands, drawn instead as the buttons
// that do those things now: an Xbox, PlayStation or Switch controller's (by
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
#include <string>
#include <vector>

extern "C" {
int portButtonIconTextures(int button, int pressed, const void** data, int* width, int* height, int max);
extern const unsigned char portFontBold[];
extern const unsigned int portFontBold_size;
}

namespace {

enum Choice { kAuto, kOriginal, kXbox, kPlayStation, kSwitch, kKeyboard };
constexpr const char* kChoices[] = {"Auto (the controller used last)", "GameCube (the game's own)", "Xbox",
                                    "PlayStation", "Switch", "Keyboard"};

constexpr u16 kPadButtons[8] = {PAD_BUTTON_A,  PAD_BUTTON_B,  PAD_BUTTON_X,  PAD_BUTTON_Y,
                                PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_START};

int* sChoice;
// the last input: 0 none yet, 1 the keyboard, 2 a controller (sPadType)
int sLastInput;
SDL_GamepadType sPadType = SDL_GAMEPAD_TYPE_UNKNOWN;

struct Image {
  std::vector<uint8_t> pixels;
  aurora::texture::ReplacementRegistration reg;
};
std::vector<Image> sImages;
std::string sApplied; // what's registered now ("" none)

void clear() {
  for (Image& i : sImages) {
    if (i.reg.id != 0) {
      aurora::texture::unregister_replacement(i.reg);
    }
  }
  sImages.clear();
  sApplied.clear();
}

GlyphStyle styleOf(SDL_GamepadType t) {
  switch (t) {
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return GlyphStyle::PlayStation;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return GlyphStyle::Switch;
  default:
    return GlyphStyle::Xbox;
  }
}

SDL_GamepadType portOneType() {
  const s32 idx = PADGetIndexForPort(0);
  if (idx < 0) {
    return SDL_GAMEPAD_TYPE_UNKNOWN;
  }
  SDL_Gamepad* pad = PADGetSDLGamepadForIndex(static_cast<u32>(idx));
  return pad != nullptr ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_UNKNOWN;
}

bool havePad() { return PADGetIndexForPort(0) >= 0; }

// the style to draw now, or false: the game's own pictures
bool wantedStyle(GlyphStyle* out) {
  int c = *sChoice;
  if (c == kAuto) {
    if (havePad() && PADIsGCAdapter(0)) {
      return false; // a real GameCube controller: its own buttons
    }
    if (sLastInput == 2 || (sLastInput == 0 && havePad())) {
      const SDL_GamepadType t = sPadType != SDL_GAMEPAD_TYPE_UNKNOWN ? sPadType : portOneType();
      *out = styleOf(t);
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
std::string plan(GlyphStyle style, GlyphSpec specs[8], std::string labels[8]) {
  std::string key = std::to_string(static_cast<int>(style));
  u32 count = 0;
  if (style == GlyphStyle::Keyboard) {
    PADKeyButtonBinding* keys = PADGetKeyButtonBindings(0, &count);
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
    PADButtonMapping* maps = PADGetButtonMappings(0, &count);
    static const u32 kDefaults[8] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
                                     SDL_GAMEPAD_BUTTON_EAST,  SDL_GAMEPAD_BUTTON_NORTH,
                                     SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_NATIVE_BUTTON_INVALID,
                                     PAD_NATIVE_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_START};
    for (int b = 0; b < 8; b++) {
      u32 native = maps != nullptr ? PAD_NATIVE_BUTTON_INVALID : kDefaults[b];
      for (u32 i = 0; maps != nullptr && i < count; i++) {
        if (maps[i].padButton == kPadButtons[b]) {
          native = maps[i].nativeButton;
        }
      }
      specs[b].face = faceOf(native, b);
      if (specs[b].face == GlyphFace::Other) {
        const char* n = PADGetNativeButtonName(native);
        labels[b] = n != nullptr ? std::string(n).substr(0, 3) : "?";
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

void update() {
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
        sImages.push_back(Image{drawButtonGlyph(g, W, H), {}});
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
  const Uint64 now = SDL_GetTicks();
  if (now - sLast < 250) {
    return;
  }
  sLast = now;
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
    sLastInput = 2;
    if (SDL_Gamepad* pad = SDL_GetGamepadFromID(e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? e->gbutton.which
                                                                                         : e->gaxis.which)) {
      sPadType = SDL_GetGamepadType(pad);
    }
    break;
  default:
    break;
  }
  return 0; // only watching
}

struct ButtonPromptSettings {
  ButtonPromptSettings() {
    sChoice = portSettingsDeclare("button_prompts", kAuto, kAuto, kKeyboard);
    portSettingsAddChoice("Controls", "Button prompts",
                          "The buttons the game shows (in its text, menus and action commands) drawn as the ones "
                          "that do those things on your controller or keyboard, where you've mapped them.",
                          0, "button_prompts", kChoices, 6,
                          "button prompts icons glyphs xbox playstation ps4 ps5 switch keyboard controller");
    portSettingsAddOverlay(tick, inputEvent);
  }
} sButtonPrompts;

} // namespace
