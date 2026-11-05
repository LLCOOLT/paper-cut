// Button icons in Paper Cut's own text (port/include/port/button_markup.h):
// {A}, {STICK}, {PING} and the rest as what does them now for player 1, in
// the style the button prompts draw (button_prompts.cpp) and from the
// bindings (bindings.cpp): the page's icons (Dear ImGui, letters of their own
// in its fonts), the game's (its font's <icon> tags) or words.
#include "port/button_markup.h"
#include "port/bindings.h"
#include "port/tr.h"

#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <cctype>
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

enum Kind { kButton, kStick, kCStick, kDPad, kAction, kNative };
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
    // a controller's own buttons by where they are, for what reads them so (photo mode, the
    // page's controls), each with the key that does the same: {SOUTH:Return}
    {"SOUTH", kNative, SDL_GAMEPAD_BUTTON_SOUTH, "A"},
    {"EAST", kNative, SDL_GAMEPAD_BUTTON_EAST, "B"},
    {"WEST", kNative, SDL_GAMEPAD_BUTTON_WEST, "X"},
    {"NORTH", kNative, SDL_GAMEPAD_BUTTON_NORTH, "Y"},
    {"LB", kNative, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "L"},
    {"RB", kNative, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "R"},
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
        (t.kind == kNative || p[name + 1] == '}')) {
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
  // the game's icon (by its <icon> name, or by number), sized to the text's line
  void icon(const char* name, int number = -1) {
    char tag[64];
    if (name != nullptr) {
      std::snprintf(tag, sizeof(tag), "<icon %s 0.72 0 2 2>", name);
    } else {
      std::snprintf(tag, sizeof(tag), "<icon %d 0.72 0 2 2>", number);
    }
    s += tag;
  }
};

struct Mode {
  bool gc;          // the game's own GameCube pictures (the setting, a GameCube controller, touch)
  bool keys;        // the keyboard's
  GlyphStyle style; // otherwise a controller's
};

Mode modeNow() {
  GlyphStyle s = GlyphStyle::GameCube;
  if (!buttonPromptsStyle(&s)) {
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
  buttonPromptsPlan(st, specs, labels);
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
      o.icon(kIcons[b]);
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
    o.icon(t.icon); // the game's icon: the button prompts draw it as the button that is A now
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
    buttonPromptsPlan(m.style, specs, labels);
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

extern "C" int portButtonsGameColumns(const char* p, int* len) {
  size_t n = 0;
  if (p == nullptr || tokenAt(p, &n) == nullptr) {
    return -1;
  }
  *len = static_cast<int>(n);
  const std::string one(p, n);
  const std::string s = expand(one.c_str(), kGame);
  // each icon two columns, the rest a column a byte (Shift-JIS: two bytes, two columns)
  int cols = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '<') {
      const size_t e = s.find('>', i);
      cols += 2;
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
    // about the height of a capital letter and a half, centred on the letters
    const float size = cfg.SizePixels;
    const int h = static_cast<int>(size * 0.92f + 0.5f);
    for (const Icon& i : icons) {
      const int w = static_cast<int>(h * i.width + 0.5f);
      const float gap = size * 0.06f;
      const int r = atlas->AddCustomRectFontGlyph(cfg.DstFont, i.cp, w, h, w + gap * 2.f,
                                                  ImVec2(gap, size * 0.56f - h * 0.5f));
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
