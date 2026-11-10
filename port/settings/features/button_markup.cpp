// Button icons in Paper Cut's own text (port/include/port/button_markup.h):
// {A}, {STICK}, {PING} and the rest as what does them now for player 1, in
// the style the button prompts draw (button_prompts.cpp) and from the
// bindings (bindings.cpp): the page's icons (Dear ImGui, letters of their own
// in its fonts), the game's (its font's <icon> tags) or words.
#include "port/button_markup.h"
#include "port/bindings.h"
#include "port/menu_buttons.h"
#include "port/tr.h"

#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" {
extern const unsigned char portFontBold[];
extern const unsigned int portFontBold_size;
int portIconCStick(void); // icondrv.c: the help box's C-stick (ICON_070, the release's number)
}

namespace {

enum Kind { kButton, kStick, kCStick, kDPad, kAction, kNative, kMenu, kPad, kNote };
struct Token {
  const char* name;
  Kind kind;
  int index;        // the game row (bindings.cpp: 0-7 the buttons, 8 the D-pad's up, 12 the stick's, 16 the C-stick's),
                    // the action, or a controller's own button (SDL_GamepadButton)
  const char* icon; // the game's <icon> name; a controller's own button: the GameCube's that the touch controls call it
};
const Token kTokens[] = {
    {"A", kButton, 0, "PAD_A"},
    {"B", kButton, 1, "PAD_B"},
    {"X", kButton, 2, "PAD_X"},
    {"Y", kButton, 3, "PAD_Y"},
    {"Z", kButton, 4, "PAD_Z_OFF"},
    {"L", kButton, 5, "BUTTON_L"},
    {"R", kButton, 6, "BUTTON_R"},
    {"START", kButton, 7, "PAD_ST_OFF"},
    // a controller's button in text about any controller ("A controller joins with {PADSTART}"): player 1's
    // as {START} while they play on a controller, and a controller's (never a key) while they play on the
    // keyboard: the first one connected's, else the GameCube's own (button_prompts.cpp's buttonPromptsAnyPad)
    {"PADA", kPad, 0, "PAD_A"},
    {"PADB", kPad, 1, "PAD_B"},
    {"PADSTART", kPad, 7, "PAD_ST_OFF"},
    {"STICK", kStick, 12, "STICK"},
    {"CSTICK", kCStick, 16, nullptr},
    {"DPAD", kDPad, 8, nullptr},
    {"SETTINGS", kAction, PORT_BIND_SETTINGS, nullptr},
    {"FF", kAction, PORT_BIND_FAST_FORWARD, nullptr},
    {"REWIND", kAction, PORT_BIND_REWIND, nullptr},
    {"PHOTO", kAction, PORT_BIND_PHOTO, nullptr},
    {"SCREENSHOT", kAction, PORT_BIND_SCREENSHOT, nullptr},
    {"LOG", kAction, PORT_BIND_MESSAGE_LOG, nullptr},
    {"SWAP", kAction, PORT_BIND_QUICK_SWAP, nullptr},
    {"BOOST", kAction, PORT_BIND_WALK_BOOST, nullptr},
    {"PING", kAction, PORT_BIND_PING, nullptr},
    {"SAVESTATE", kAction, PORT_BIND_SAVE_STATE, nullptr},
    {"LOADSTATE", kAction, PORT_BIND_LOAD_STATE, nullptr},
    {"LUA", kAction, PORT_BIND_LUA_CONSOLE, nullptr},
    {"LAUNCHER", kAction, PORT_BIND_LAUNCHER, nullptr},
    // what opens a paper note: its action's binding where it works, else the game's L (putNote)
    {"NOTE", kNote, PORT_BIND_NOTE_READ, nullptr},
    // a controller's own buttons by where they are, for what reads them so (photo mode, the
    // page's controls), each with the key that does the same: {SOUTH:Return}
    {"SOUTH", kNative, SDL_GAMEPAD_BUTTON_SOUTH, "A"},
    {"EAST", kNative, SDL_GAMEPAD_BUTTON_EAST, "B"},
    {"WEST", kNative, SDL_GAMEPAD_BUTTON_WEST, "X"},
    {"NORTH", kNative, SDL_GAMEPAD_BUTTON_NORTH, "Y"},
    {"LB", kNative, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "L"},
    {"RB", kNative, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "R"},
    {"MENU", kNative, SDL_GAMEPAD_BUTTON_START, "START"},
    {"VIEW", kNative, SDL_GAMEPAD_BUTTON_BACK, "Z"}, // (Back, Select, Share, Minus; a GameCube's has none)
    // Paper Cut's menus' confirm, back and third (clear) buttons on the controller in hand
    // (port/include/port/menu_buttons.h), or the page's keys
    {"OK", kMenu, PORT_MENU_CONFIRM, "A"},
    {"BACK", kMenu, PORT_MENU_BACK, "B"},
    {"CLEAR", kMenu, PORT_MENU_THIRD, "X"},
};

// the token at p ('{'): its entry and length, or nullptr; key: a controller's own
// button's key ({SOUTH:Return}: SDL's name for it), or SDL_SCANCODE_UNKNOWN
const Token* tokenAt(const char* p, size_t* len, int* key = nullptr) {
  if (*p != '{') {
    return nullptr;
  }
  size_t n = 1;
  while (n < 13 && std::isupper(static_cast<unsigned char>(p[n]))) {
    n++;
  }
  const size_t name = n - 1;
  int sc = SDL_SCANCODE_UNKNOWN;
  if (p[n] == ':') {
    const size_t from = ++n;
    while (n < from + 16 && p[n] != '\0' && p[n] != '}' && p[n] != '{') {
      n++;
    }
    if (p[n] == '}') {
      sc = SDL_GetScancodeFromName(std::string(p + from, n - from).c_str());
    }
  }
  if (p[n] != '}' || name == 0) {
    return nullptr;
  }
  for (const Token& t : kTokens) {
    if (std::strlen(t.name) == name && std::strncmp(t.name, p + 1, name) == 0 &&
        ((t.kind == kNative || t.kind == kMenu) || p[name + 1] == '}')) {
      *len = n + 1;
      if (key != nullptr) {
        *key = sc;
      }
      return &t;
    }
  }
  return nullptr;
}

bool hasToken(const char* s) {
  for (const char* p = std::strchr(s, '{'); p != nullptr; p = std::strchr(p + 1, '{')) {
    size_t n;
    if (tokenAt(p, &n) != nullptr) {
      return true;
    }
  }
  return false;
}

// --- The page's icons: their code points ---------------------------------------------

// styles: 0 the GameCube's, 1 Xbox, 2 PlayStation, 3 Switch, 4 Steam Deck
constexpr GlyphStyle kStyles[] = {GlyphStyle::GameCube, GlyphStyle::Xbox, GlyphStyle::PlayStation,
                                  GlyphStyle::Switch, GlyphStyle::SteamDeck};
constexpr int kStyleCount = 5;
// a style's icons, 32 code points each: 0-9 the faces South to Back (GlyphFace), 11 the left
// stick, 12 the right, 13 the D-pad, 14 and 15 the sticks pressed in, 16-23 the GameCube's A to Start
constexpr ImWchar kStyleBase = 0xE000;
constexpr int kSlotLeftPress = 14, kSlotRightPress = 15, kSlotGc = 16;
// a key cap: kKeyBase + the scancode, for the scancodes below
constexpr ImWchar kKeyBase = 0xE200;
bool keyCapped(int sc) { return (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_KP_PERIOD) || (sc >= SDL_SCANCODE_LCTRL && sc <= SDL_SCANCODE_RGUI); }

int styleIndex(GlyphStyle s) {
  switch (s) {
  case GlyphStyle::GameCube: return 0;
  case GlyphStyle::PlayStation: return 2;
  case GlyphStyle::Switch: return 3;
  case GlyphStyle::SteamDeck: return 4;
  default: return 1;
  }
}

ImWchar styleGlyph(GlyphStyle s, int slot) { return static_cast<ImWchar>(kStyleBase + styleIndex(s) * 32 + slot); }

// a key cap's name: short, as printed on keys
std::string capLabel(int sc) {
  switch (sc) {
  case SDL_SCANCODE_RETURN: return "Enter";
  case SDL_SCANCODE_ESCAPE: return "Esc";
  case SDL_SCANCODE_BACKSPACE: return "Bksp";
  case SDL_SCANCODE_SPACE: return "Space";
  case SDL_SCANCODE_CAPSLOCK: return "Caps";
  case SDL_SCANCODE_PRINTSCREEN: return "PrtSc";
  case SDL_SCANCODE_SCROLLLOCK: return "ScrLk";
  case SDL_SCANCODE_INSERT: return "Ins";
  case SDL_SCANCODE_DELETE: return "Del";
  case SDL_SCANCODE_PAGEUP: return "PgUp";
  case SDL_SCANCODE_PAGEDOWN: return "PgDn";
  case SDL_SCANCODE_NUMLOCKCLEAR: return "Num";
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL: return "Ctrl";
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT: return "Shift";
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT:
#if defined(__APPLE__)
    return "Opt";
#else
    return "Alt";
#endif
  case SDL_SCANCODE_LGUI:
  case SDL_SCANCODE_RGUI:
#if defined(__APPLE__)
    return "Cmd";
#else
    return "Win";
#endif
  default: break;
  }
  std::string n = SDL_GetScancodeName(static_cast<SDL_Scancode>(sc));
  if (n.rfind("Keypad ", 0) == 0) {
    n = "Num" + n.substr(7);
  }
  if (n.size() > 6) {
    n.resize(6);
  }
  return n.empty() ? "?" : n;
}

// --- What a token is now -------------------------------------------------------------

enum Target { kUi, kGame, kWordsUi, kWordsGame };

// the game's icons in Paper Cut's text at the help box's letters (scale 1): a capital letter's height (0.72, as
// before 0.5.0, was nearly two: too big, the rows crowded and the help broken up)
constexpr float kHelpScale = 0.40f;
float gGameIconScale = kHelpScale;
// the scale the text is drawn at (portButtonsGameTextScale); 0: the icon's own (gGameIconScale / kHelpScale)
float gGameTextScale = 0.f;

struct Out {
  Target target;
  std::string s;

  bool game() const { return target == kGame || target == kWordsGame; }
  bool ui() const { return target == kUi; }
  // an English word in the menu language, for this text
  void word(const char* english) { s += game() ? portTrGame(english) : portTr(english); }
  // a key's name (ASCII, as SDL has it)
  void raw(const std::string& t) { s += t; }
  void glyph(ImWchar c) {
    // UTF-8 (the code points are all three bytes)
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
  // the game's icon (by its <icon> name, or by number), a capital letter's height (portButtonsGameIconScale),
  // centred on the capitals' middle. fontmgr.c draws <icon id k x y advance> with its quad's top at the line's
  // top + y and its middle half the icon lower, the icon k big whatever the text's scale, but x, y and the
  // advance at the text's scale: so the text's scale (kHelpScale for 1, the help box's letters; less for
  // smaller text, as the Bindings page's rows; or portButtonsGameTextScale's, an icon bigger than the
  // letters still centred on them) puts each back where it belongs
  void icon(const char* name, int number = -1) {
    // a button icon's picture a unit of scale, in the help box's pixels (30 at 0.72, measured), and the
    // capitals' middle below the line's top (from the same picture)
    constexpr float kIconPx = 42.f;
    constexpr float kCapMiddle = 11.6f;
    char tag[80];
    const float k = gGameIconScale;
    const float text = gGameTextScale > 0.f ? gGameTextScale : k / kHelpScale; // (the text's scale)
    const float px = kIconPx * k;      // (the icon, on screen)
    const int x = static_cast<int>(std::lround(0.5f * px * (1.f / text - 1.f))); // (not over the letter before)
    const int y = static_cast<int>(std::lround(px * (1.f - 0.5f / text) - kCapMiddle));
    const int advance = static_cast<int>(std::lround((px + 2.f) / text - px)); // (2 pixels after, as before)
    if (name != nullptr) {
      std::snprintf(tag, sizeof(tag), "<icon %s %.2f %d %d %d>", name, k, x, y, advance);
    } else {
      std::snprintf(tag, sizeof(tag), "<icon %d %.2f %d %d %d>", number, k, x, y, advance);
    }
    s += tag;
  }
};

struct Mode {
  bool gc;          // the game's own GameCube pictures (the setting, a GameCube controller, touch)
  bool keys;        // the keyboard's
  GlyphStyle style; // otherwise a controller's
};

// whose buttons the tokens show (portButtonsPlayer): a controller port, 0 player 1's
thread_local int gPort = 0;

// the game's own icon for a GameCube button (row 0-7), or the port's own drawing of it when its buttons
// look unlike player 1's (PORT_ICON_PAD_BASE, button_prompts.cpp)
bool portIcon(Out& o, int row) {
  if (gPort <= 0 || !buttonPromptsOwnIcons(static_cast<unsigned>(gPort))) {
    return false;
  }
  o.icon(nullptr, PORT_ICON_PAD_BASE + gPort * 16 + row * 2);
  return true;
}

void planNow(GlyphStyle st, GlyphSpec specs[8], std::string labels[8]) {
  buttonPromptsPlanFor(static_cast<unsigned>(gPort > 0 ? gPort : 0), st, specs, labels);
}

Mode modeNow() {
  GlyphStyle s = GlyphStyle::GameCube;
  if (!(gPort > 0 ? buttonPromptsStyleFor(static_cast<unsigned>(gPort), &s) : buttonPromptsStyle(&s))) {
    return Mode{true, false, GlyphStyle::GameCube};
  }
  if (s == GlyphStyle::Generic) {
    s = GlyphStyle::Xbox;
  }
  return Mode{false, s == GlyphStyle::Keyboard, s};
}

// a key, with the modifiers held with it (an action's)
void putKey(Out& o, int sc, int mods) {
  static const struct {
    int bit;
    const char* name;
  } kMods[] = {{1, "Ctrl"},
               {2, "Shift"},
#if defined(__APPLE__)
               {4, "Option"},
               {8, "Cmd"}
#elif defined(_WIN32)
               {4, "Alt"},
               {8, "Win"}
#else
               {4, "Alt"},
               {8, "Super"}
#endif
  };
  for (const auto& m : kMods) {
    if (mods & m.bit) {
      o.word(m.name);
      o.raw(" + ");
    }
  }
  if (o.ui() && keyCapped(sc)) {
    o.glyph(static_cast<ImWchar>(kKeyBase + sc));
    return;
  }
  // its name as keyboards print it ("Enter", not SDL's "Return")
  switch (sc) {
  case SDL_SCANCODE_RETURN:
  case SDL_SCANCODE_ESCAPE:
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL:
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT:
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT:
  case SDL_SCANCODE_LGUI:
  case SDL_SCANCODE_RGUI: o.raw(capLabel(sc)); return;
  default: break;
  }
  const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(sc));
  o.raw(n != nullptr && n[0] != '\0' ? n : "?");
}

// the GameCube button (0-7, A B X Y Z L R Start) whose icon the button prompts draw as this
// face of the style now (the game's icons follow the mapping), or -1
int buttonOnFace(GlyphStyle st, GlyphFace face) {
  GlyphSpec specs[8];
  std::string labels[8];
  planNow(st, specs, labels);
  for (int b = 0; b < 8; b++) {
    if (specs[b].face == face) {
      return b;
    }
  }
  return -1;
}

const char* const kIcons[8] = {"PAD_A", "PAD_B", "PAD_X", "PAD_Y", "PAD_Z_OFF", "BUTTON_L", "BUTTON_R", "PAD_ST_OFF"};

// a controller's button (SDL_GamepadButton) in the style; icons: the game's icons are drawn in this
// style now (the button prompts'), so a button one of them shows can be that icon
void putNative(Out& o, GlyphStyle st, int native, bool icons = false) {
  if (o.target == kGame && icons) {
    GlyphFace f = GlyphFace::Other;
    switch (native) {
    case SDL_GAMEPAD_BUTTON_SOUTH: f = GlyphFace::South; break;
    case SDL_GAMEPAD_BUTTON_EAST: f = GlyphFace::East; break;
    case SDL_GAMEPAD_BUTTON_WEST: f = GlyphFace::West; break;
    case SDL_GAMEPAD_BUTTON_NORTH: f = GlyphFace::North; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: f = GlyphFace::LeftShoulder; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: f = GlyphFace::RightShoulder; break;
    case SDL_GAMEPAD_BUTTON_START: f = GlyphFace::Start; break;
    case SDL_GAMEPAD_BUTTON_BACK: f = GlyphFace::Back; break;
    default: break;
    }
    const int b = f != GlyphFace::Other ? buttonOnFace(st, f) : -1;
    if (b >= 0) {
      if (!portIcon(o, b)) {
        o.icon(kIcons[b]);
      }
      return;
    }
  }
  if (o.ui()) {
    int slot = -1;
    switch (native) {
    case SDL_GAMEPAD_BUTTON_SOUTH: slot = static_cast<int>(GlyphFace::South); break;
    case SDL_GAMEPAD_BUTTON_EAST: slot = static_cast<int>(GlyphFace::East); break;
    case SDL_GAMEPAD_BUTTON_WEST: slot = static_cast<int>(GlyphFace::West); break;
    case SDL_GAMEPAD_BUTTON_NORTH: slot = static_cast<int>(GlyphFace::North); break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: slot = static_cast<int>(GlyphFace::LeftShoulder); break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: slot = static_cast<int>(GlyphFace::RightShoulder); break;
    case SDL_GAMEPAD_BUTTON_START: slot = static_cast<int>(GlyphFace::Start); break;
    case SDL_GAMEPAD_BUTTON_BACK: slot = static_cast<int>(GlyphFace::Back); break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: slot = kSlotLeftPress; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: slot = kSlotRightPress; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: break; // (its name says which way)
    default: break;
    }
    if (slot >= 0 && st != GlyphStyle::GameCube) {
      o.glyph(styleGlyph(st, slot));
      return;
    }
  }
  o.word(nativeButtonLabel(st == GlyphStyle::GameCube ? GlyphStyle::Xbox : st, static_cast<uint32_t>(native), 0));
}

// the four keys of a stick or the D-pad (rows up, down, left, right), as W A S D: false when one has none
bool putFourKeys(Out& o, int firstRow) {
  int sc[4];
  static const int kOrder[4] = {0, 2, 1, 3}; // up, left, down, right
  for (int i = 0; i < 4; i++) {
    int a = 0, b = 0;
    if (portBindingsWhat(-1, firstRow + kOrder[i], 0, &a, &b) != 1) {
      return false;
    }
    sc[i] = a;
  }
  for (int i = 0; i < 4; i++) {
    if (i > 0 && !o.ui()) {
      o.raw(o.target == kGame ? "/" : ", ");
    }
    putKey(o, sc[i], 0);
  }
  return true;
}

// the GameCube's name of a button (row 0-7)
const char* kGcNames[8] = {"A", "B", "X", "Y", "Z", "L", "R", "Start"};

void putButton(Out& o, const Token& t, const Mode& m) {
  const int row = t.index;
  if (o.target == kGame) {
    if (!portIcon(o, row)) {
      o.icon(t.icon); // the game's icon: the button prompts draw it as the button that is A now
    }
    return;
  }
  if (m.gc) {
    if (o.ui()) {
      o.glyph(styleGlyph(GlyphStyle::GameCube, kSlotGc + row));
    } else {
      o.word(kGcNames[row]);
    }
    return;
  }
  int a = 0, b = 0;
  if (m.keys) {
    if (portBindingsWhat(-1, row, 0, &a, &b) == 1) {
      putKey(o, a, 0);
    } else {
      o.word(kGcNames[row]);
    }
    return;
  }
  const int what = portBindingsWhat(-1, row, 1, &a, &b);
  if (o.ui()) {
    // as the prompts draw it (a trigger on its axis too)
    GlyphSpec specs[8];
    std::string labels[8];
    planNow(m.style, specs, labels);
    if (specs[row].face != GlyphFace::Other) {
      o.glyph(styleGlyph(m.style, static_cast<int>(specs[row].face)));
      return;
    }
  }
  static const uint16_t kPad[8] = {PAD_BUTTON_A,  PAD_BUTTON_B,  PAD_BUTTON_X,  PAD_BUTTON_Y,
                                   PAD_TRIGGER_Z, PAD_TRIGGER_L, PAD_TRIGGER_R, PAD_BUTTON_START};
  if (what == 2) {
    o.word(nativeButtonLabel(m.style, static_cast<uint32_t>(a), kPad[row]));
  } else if (what == 4 && (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
    o.word(nativeButtonLabel(m.style, PAD_NATIVE_BUTTON_INVALID,
                             a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PAD_TRIGGER_L : PAD_TRIGGER_R));
  } else {
    o.word(kGcNames[row]);
  }
}

void putStick(Out& o, const Token& t, const Mode& m) {
  const bool c = t.kind == kCStick, d = t.kind == kDPad;
  if (m.keys && putFourKeys(o, t.index)) {
    return;
  }
  if (o.ui() && !m.keys) {
    o.glyph(styleGlyph(m.style, d ? 13 : c ? 12 : 11));
    return;
  }
  if (o.target == kGame && !m.keys && !d) {
    if (!c) {
      o.icon("STICK");
      return;
    }
    if (m.gc) {
      o.icon(nullptr, portIconCStick());
      return;
    }
  }
  o.word(d ? "D-pad" : c ? (m.gc || m.keys ? "C-stick" : "right stick") : "stick");
}

// a controller's own button: the key given with it on the keyboard; the GameCube's
// that the touch controls call it; else the controller's
void putNativeToken(Out& o, const Token& t, const Mode& m, int key) {
  if (m.keys && key != SDL_SCANCODE_UNKNOWN) {
    putKey(o, key, 0);
    return;
  }
  if (m.gc) {
    for (const Token& g : kTokens) {
      if (g.kind == kButton && std::strcmp(g.name, t.icon) == 0) {
        putButton(o, g, Mode{true, false, GlyphStyle::GameCube});
        return;
      }
    }
  }
  GlyphStyle st = m.keys ? glyphStyleOfPort(0) : m.style;
  if (st == GlyphStyle::Generic) {
    st = GlyphStyle::Xbox;
  }
  putNative(o, st, t.index, !m.keys && !m.gc);
}

// a menu's confirm, back or third: the controller in hand's button, the page's key, or the GameCube's
void putMenu(Out& o, const Token& t, const Mode& m, int key) {
  if (m.keys && key != SDL_SCANCODE_UNKNOWN) {
    putKey(o, key, 0); // ({OK:Return}: the key that does the same there)
    return;
  }
  if (m.keys) {
    putKey(o, t.index == PORT_MENU_CONFIRM ? SDL_SCANCODE_RETURN
              : t.index == PORT_MENU_BACK  ? SDL_SCANCODE_ESCAPE
                                           : SDL_SCANCODE_DELETE,
           0);
    return;
  }
  if (m.gc) {
    for (const Token& g : kTokens) {
      if (g.kind == kButton && std::strcmp(g.name, t.icon) == 0) {
        putButton(o, g, Mode{true, false, GlyphStyle::GameCube});
        return;
      }
    }
  }
  const unsigned id = portMenuPadId();
  SDL_Gamepad* pad = id != 0 ? SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(id)) : nullptr;
  GlyphStyle st = pad != nullptr ? glyphStyleOf(SDL_GetGamepadType(pad), SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad))
                                 : m.style;
  if (st == GlyphStyle::Generic || st == GlyphStyle::Keyboard) {
    st = GlyphStyle::Xbox;
  }
  putNative(o, st, portMenuRoleButton(t.index, id), true);
}

// a controller's button (kPad): player 1's own while they play on a controller (or touch: the game's own),
// else a controller's, never a key
void putPad(Out& o, const Token& t, const Mode& m) {
  GlyphStyle st = GlyphStyle::GameCube;
  if (!m.keys || !buttonPromptsAnyPad(&st)) {
    putButton(o, t, m);
    return;
  }
  const int row = t.index;
  if (o.target == kGame) {
    if (buttonPromptsOwnIcons(0)) {
      o.icon(nullptr, PORT_ICON_PAD_BASE + row * 2); // (port 0's set: "a controller", button_prompts.cpp)
    } else {
      o.word(kGcNames[row]); // (its pictures not drawn yet)
    }
    return;
  }
  if (st == GlyphStyle::GameCube) {
    if (o.ui()) {
      o.glyph(styleGlyph(GlyphStyle::GameCube, kSlotGc + row));
    } else {
      o.word(kGcNames[row]);
    }
    return;
  }
  // out of the box: the GameCube's A on the bottom face, B on the left one, Start on Start
  putNative(o, st, row == 0 ? SDL_GAMEPAD_BUTTON_SOUTH : row == 1 ? SDL_GAMEPAD_BUTTON_WEST : SDL_GAMEPAD_BUTTON_START);
}

void putAction(Out& o, const Token& t, const Mode& m) {
  int a = 0, b = 0;
  const int key = portBindingsWhat(t.index, -1, 0, &a, &b);
  const int ka = a, kb = b;
  const int pad = portBindingsWhat(t.index, -1, 1, &a, &b);
  if ((m.keys || m.gc || pad == 0) && key == 1) { // (a GameCube controller or touch: no Back, no stick presses)
    putKey(o, ka, kb);
    return;
  }
  if (pad == 2 || pad == 3) {
    const GlyphStyle st = m.keys ? glyphStyleOfPort(0) : m.style;
    putNative(o, st, a, !m.keys && !m.gc);
    if (pad == 3) {
      o.raw(" + ");
      putNative(o, st, b, !m.keys && !m.gc);
    }
    return;
  }
  if (key == 1) {
    putKey(o, ka, kb);
    return;
  }
  o.word("(none)");
}

// {NOTE}: what opens a paper note (port/include/port/notes.h) for the player whose buttons show: on the keyboard
// the action's key; on a controller its button where that controller has it free of the game's (an Xbox's LB, a
// PlayStation's L1); else, and on a GameCube's or the touch controls, the game's L, which does it too
void putNote(Out& o, const Mode& m) {
  const Token* l = nullptr;
  for (const Token& t : kTokens) {
    if (t.kind == kButton && std::strcmp(t.name, "L") == 0) {
      l = &t;
    }
  }
  int a = 0, b = 0;
  if (m.keys) {
    if (portBindingsWhat(PORT_BIND_NOTE_READ, -1, 0, &a, &b) == 1) {
      putKey(o, a, b);
      return;
    }
  } else if (!m.gc) {
    const int pad = portBindingsWhat(PORT_BIND_NOTE_READ, -1, 1, &a, &b);
    if ((pad == 2 || pad == 3) && portBindingsPadUsableOn(PORT_BIND_NOTE_READ, gPort > 0 ? gPort : 0)) {
      putNative(o, m.style, a, true);
      if (pad == 3) {
        o.raw(" + ");
        putNative(o, m.style, b, true);
      }
      return;
    }
  }
  putButton(o, *l, m);
}

std::string expand(const char* text, Target target) {
  const Mode m = modeNow();
  Out o{target, {}};
  for (const char* p = text; *p != '\0';) {
    size_t n = 0;
    int key = SDL_SCANCODE_UNKNOWN;
    const Token* t = *p == '{' ? tokenAt(p, &n, &key) : nullptr;
    if (t == nullptr) {
      o.s += *p++;
      continue;
    }
    switch (t->kind) {
    case kButton: putButton(o, *t, m); break;
    case kAction: putAction(o, *t, m); break;
    case kNative: putNativeToken(o, *t, m, key); break;
    case kMenu: putMenu(o, *t, m, key); break;
    case kPad: putPad(o, *t, m); break;
    case kNote: putNote(o, m); break;
    default: putStick(o, *t, m); break;
    }
    p += n;
    // the game's text skips a line break right after a tag: keep it
    if (target == kGame && *p == '\n' && !o.s.empty() && o.s.back() == '>') {
      o.s += ' ';
    }
  }
  return o.s;
}

const char* ring(std::string s) {
  static thread_local std::string sRing[8];
  static thread_local int sAt;
  std::string& r = sRing[sAt++ & 7];
  r = std::move(s);
  return r.c_str();
}

} // namespace

extern "C" const char* portButtonsUi(const char* text) {
  if (text == nullptr || std::strchr(text, '{') == nullptr || !hasToken(text)) {
    return text;
  }
  // kept for good (portTr's texts are): each way a text has been shown once
  static std::mutex sMutex;
  static std::unordered_set<std::string> sKept;
  std::string s = expand(text, kUi);
  std::lock_guard<std::mutex> lock(sMutex);
  return sKept.insert(std::move(s)).first->c_str();
}

extern "C" const char* portButtonsGame(const char* text) {
  if (text == nullptr || std::strchr(text, '{') == nullptr || !hasToken(text)) {
    return text;
  }
  return ring(expand(text, kGame));
}

extern "C" const char* portButtonsWords(const char* text, int game) {
  if (text == nullptr || std::strchr(text, '{') == nullptr || !hasToken(text)) {
    return text;
  }
  return ring(expand(text, game ? kWordsGame : kWordsUi));
}

extern "C" int portButtonsPlayer(int port) {
  const int was = gPort;
  gPort = port >= 0 && port < PAD_MAX_CONTROLLERS ? port : 0;
  return was;
}

extern "C" float portButtonsGameIconScale(float scale) {
  const float was = gGameIconScale;
  if (scale > 0.f) {
    gGameIconScale = scale;
  }
  return was;
}

extern "C" float portButtonsGameTextScale(float scale) {
  const float was = gGameTextScale;
  gGameTextScale = scale > 0.f ? scale : 0.f;
  return was;
}

extern "C" int portButtonsGameColumns(const char* p, int* len) {
  size_t n = 0;
  if (p == nullptr || tokenAt(p, &n) == nullptr) {
    return -1;
  }
  *len = static_cast<int>(n);
  const std::string one(p, n);
  const std::string s = expand(one.c_str(), kGame);
  // each icon a column (a capital letter's height, about a letter and a third wide: the wrap measures the line
  // in the game's font as well, so a line of wide ones still breaks), the rest a column a byte (Shift-JIS: two
  // bytes, two columns)
  int cols = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '<') {
      const size_t e = s.find('>', i);
      cols += 1;
      i = e == std::string::npos ? s.size() : e;
    } else {
      cols++;
    }
  }
  return cols;
}

// --- The icons in the page's fonts -------------------------------------------------------

namespace {

struct Pending {
  int rect;
  GlyphSpec spec;
  std::string label;
};

// each icon a font has: its code point, what it shows and its width (in heights)
struct Icon {
  ImWchar cp;
  GlyphSpec spec;
  std::string label;
  float width;
};

std::vector<Icon> allIcons() {
  std::vector<Icon> v;
  for (int s = 0; s < kStyleCount; s++) {
    const GlyphStyle st = kStyles[s];
    auto add = [&](int slot, GlyphFace f, float w, std::string label = {}) {
      GlyphSpec g;
      g.style = st;
      g.face = f;
      v.push_back(Icon{styleGlyph(st, slot), g, std::move(label), w});
    };
    if (st == GlyphStyle::GameCube) {
      static const float kW[8] = {1.f, 1.f, 1.f, 1.f, 1.5f, 1.5f, 1.5f, 2.3f};
      for (int i = 0; i < 8; i++) {
        add(kSlotGc + i, static_cast<GlyphFace>(static_cast<int>(GlyphFace::GcA) + i), kW[i]);
      }
    } else {
      for (int f = 0; f <= static_cast<int>(GlyphFace::Back); f++) {
        add(f, static_cast<GlyphFace>(f), f >= static_cast<int>(GlyphFace::LeftShoulder) ? 1.6f : 1.f);
      }
      const bool ps = st == GlyphStyle::PlayStation || st == GlyphStyle::SteamDeck;
      add(kSlotLeftPress, GlyphFace::LeftStick, 1.f, ps ? "L3" : "LS");
      add(kSlotRightPress, GlyphFace::RightStick, 1.f, ps ? "R3" : "RS");
    }
    add(11, GlyphFace::LeftStick, 1.f);
    add(12, GlyphFace::RightStick, 1.f);
    add(13, GlyphFace::DPad, 1.f);
  }
  for (int sc = 0; sc < SDL_SCANCODE_COUNT; sc++) {
    if (!keyCapped(sc) || SDL_GetScancodeName(static_cast<SDL_Scancode>(sc))[0] == '\0') {
      continue;
    }
    GlyphSpec g;
    g.style = GlyphStyle::Keyboard;
    g.face = GlyphFace::Other;
    g.textScale = 0.52f;
    const std::string l = capLabel(sc);
    v.push_back(Icon{static_cast<ImWchar>(kKeyBase + sc), g, l, l.size() <= 1 ? 1.f : 0.3f + 0.5f * l.size()});
  }
  return v;
}

// a font's capitals as Dear ImGui sizes it (the line SizePixels from the hhea ascender to the descender):
// their height and their middle below the line's top, in fractions of SizePixels; false when the data is
// not a plain TrueType/OpenType font with them (ImGui's own: then a usual font's)
bool capMetrics(const ImFontConfig& cfg, float* height, float* middle) {
  const auto* d = static_cast<const unsigned char*>(cfg.FontData);
  const size_t n = cfg.FontDataSize > 0 ? static_cast<size_t>(cfg.FontDataSize) : 0;
  const auto u16 = [&](size_t at) -> unsigned {
    return at + 2 <= n ? (static_cast<unsigned>(d[at]) << 8u) | static_cast<unsigned>(d[at + 1]) : 0u;
  };
  const auto s16 = [&](size_t at) -> int { return static_cast<int16_t>(static_cast<uint16_t>(u16(at))); };
  const auto u32 = [&](size_t at) -> size_t { return (static_cast<size_t>(u16(at)) << 16) | u16(at + 2); };
  if (d == nullptr || n < 12 || cfg.FontNo != 0) {
    return false;
  }
  size_t hhea = 0, os2 = 0, os2Len = 0;
  const unsigned tables = u16(4);
  for (unsigned t = 0; t < tables; t++) {
    const size_t rec = 12 + t * 16;
    if (rec + 16 > n) {
      return false;
    }
    if (std::memcmp(d + rec, "hhea", 4) == 0) {
      hhea = u32(rec + 8);
    } else if (std::memcmp(d + rec, "OS/2", 4) == 0) {
      os2 = u32(rec + 8);
      os2Len = u32(rec + 12);
    }
  }
  if (hhea == 0 || os2 == 0 || os2Len < 90 || u16(os2) < 2) {
    return false;
  }
  const int ascent = s16(hhea + 4), descent = s16(hhea + 6), cap = s16(os2 + 88);
  if (ascent <= 0 || ascent - descent <= 0 || cap <= 0 || cap > ascent) {
    return false;
  }
  const float line = static_cast<float>(ascent - descent);
  *height = cap / line;
  *middle = (ascent - cap * 0.5f) / line;
  return true;
}

} // namespace

extern "C" void portButtonGlyphsInit(float maxPx) {
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  if (atlas == nullptr || atlas->IsBuilt()) {
    return;
  }
  const std::vector<Icon> icons = allIcons();
  std::vector<Pending> pending;
  for (const ImFontConfig& cfg : atlas->Sources) {
    if (cfg.MergeMode || cfg.DstFont == nullptr || cfg.SizePixels > maxPx) {
      continue;
    }
    // a capital letter's height, centred on the capitals' middle, from the font's own measures
    // (bigger crowded the rows); a little over, as a round icon looks smaller than a letter
    float cap = 0.7f, middle = 0.45f;
    capMetrics(cfg, &cap, &middle);
    const float size = cfg.SizePixels;
    const int h = std::max(6, static_cast<int>(size * cap * 1.06f + 0.5f));
    for (const Icon& i : icons) {
      const int w = static_cast<int>(h * i.width + 0.5f);
      const float gap = size * 0.06f;
      const int r = atlas->AddCustomRectFontGlyph(cfg.DstFont, i.cp, w, h, w + gap * 2.f,
                                                  ImVec2(gap, cfg.GlyphOffset.y + size * middle - h * 0.5f));
      atlas->GetCustomRectByIndex(r)->GlyphColored = 1; // its own colours, not the text's
      pending.push_back(Pending{r, i.spec, i.label});
    }
  }
  if (!atlas->Build()) {
    return;
  }
  unsigned char* px = nullptr;
  int tw = 0, th = 0;
  atlas->GetTexDataAsRGBA32(&px, &tw, &th);
  if (px == nullptr) {
    return;
  }
  setGlyphFont(portFontBold, portFontBold_size);
  for (Pending& p : pending) {
    const ImFontAtlasCustomRect* r = atlas->GetCustomRectByIndex(p.rect);
    if (!r->IsPacked()) {
      continue;
    }
    p.spec.label = p.label.empty() ? nullptr : p.label.c_str();
    const std::vector<uint8_t> img = drawButtonGlyph(p.spec, r->Width, r->Height);
    for (int y = 0; y < r->Height; y++) {
      std::memcpy(px + (static_cast<size_t>(r->Y + y) * tw + r->X) * 4, img.data() + static_cast<size_t>(y) * r->Width * 4,
                  static_cast<size_t>(r->Width) * 4);
    }
  }
}
