// Bindings (Controls tab; port/include/port/bindings.h): one page for every
// key and controller button, the game's and Paper Cut's, as other PC games
// have it (local survey: Elden Ring, Cyberpunk 2077, Baldur's Gate 3, the
// Nixxes ports, Ship of Harkinian, Dolphin's hotkeys).
//
//  - A table: each of the game's buttons and stick directions, then each of
//    Paper Cut's actions, with a Keyboard key, a Second key and a Controller
//    button (the chosen player's for the game's buttons; any controller for
//    the actions, which can also be two buttons held together, Back + R).
//  - Choose a cell (click, or A), press the key or button; Escape or 5
//    seconds without one leaves it. Right-click, X or Delete clears it. A key
//    or button already in another row says which, with Swap, Keep both and
//    Cancel. Reset per row, Reset all.
//  - The controller's sticks swapped, each stick's X and Y inverted (Aurora's
//    axis mappings, saved per controller model with its buttons), the dead
//    zones (Aurora's, as the Controllers section has them), and the triggers:
//    analog with a click point (the GameCube's), analog only, or digital.
//
// The game's first keys and controller buttons are Aurora's (PADKeyButtonBinding,
// PADKeyAxisBinding, PADButtonMapping, PADAxisMapping), as before; the second
// keys, Paper Cut's actions and the digital triggers are Paper Cut's settings.
#include "port/bindings.h"
#include "port/button_markup.h"
#include "port/companion_menu.h"
#include "port/menu_buttons.h"
#include "port/menu_typing.h"
#include "port/savestate.h"
#include "port/settings.h"
#include "port/speed.h"
#include "port/tr.h"

#include "../button_glyphs.h"
#include "../launcher_shot.h"
#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include "../file_util.h"
#include <dolphin/pad.h>
#include <imgui.h>
#include <imgui_internal.h> // FocusItem, the nav window (tests)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void portSettingsToggle(void);  // port/settings/settings_ui.cpp: the page
extern "C" void portShotRequestUser(void); // shot_aurora.c (port/platform.h, C only)
extern "C" void portShotRequest(const char* name); // (the same: a test's named capture)
extern "C" int portWarpSettled(void);      // trace_pc.c (the same)
extern "C" int portLauncherTestAddPending(void); // launcher_ui.cpp
extern "C" int portInputHomePort(void);          // input_pc.c (port/platform.h, C only)

namespace {

// the controller port the keyboard plays: player 1's (0) until a seat swap takes its player to another seat
u32 keyboardPort() {
  const int port = portInputHomePort();
  return port >= 0 && port < PAD_CHANMAX ? static_cast<u32>(port) : 0;
}

// the keyboard's keys from one port to another (the other left with none)
void moveKeyboard(u32 from, u32 to) {
  u32 n = 0, m = 0;
  PADKeyButtonBinding* keys = PADGetKeyButtonBindings(from, &n);
  PADKeyAxisBinding* axes = PADGetKeyAxisBindings(from, &m);
  if (from == to || keys == nullptr) {
    return;
  }
  PADKeyButtonBinding k[PAD_BUTTON_COUNT];
  PADKeyAxisBinding a[PAD_AXIS_COUNT];
  std::memcpy(k, keys, sizeof(k[0]) * std::min<u32>(n, PAD_BUTTON_COUNT));
  if (axes != nullptr) {
    std::memcpy(a, axes, sizeof(a[0]) * std::min<u32>(m, PAD_AXIS_COUNT));
  }
  PADClearKeyBindings(from);
  PADSetKeyboardActive(to, TRUE);
  PADSetKeyButtonBindings(to, k);
  if (axes != nullptr) {
    PADSetKeyAxisBindings(to, a);
  }
}

// --- What can be bound ---------------------------------------------------------

// A binding: a key, a controller button, two held together, or a stick or
// trigger direction (a game row's controller cell).
struct Bind {
  enum Type { None, Key, Button, Combo, Axis } type = None;
  int a = 0, b = 0; // Key: scancode, b the modifiers (kMod*); Button: a; Combo: a + b; Axis: a (SDL_GamepadAxis), b (the sign)
  bool operator==(const Bind& o) const {
    if (type != o.type) {
      return false;
    }
    switch (type) {
    case None: return true;
    case Key: return a == o.a && b == o.b;
    case Button: return a == o.a;
    case Combo: return (a == o.a && b == o.b) || (a == o.b && b == o.a);
    case Axis: return a == o.a && b == o.b;
    }
    return false;
  }
};
Bind keyBind(int sc) { return sc > 0 ? Bind{Bind::Key, sc, 0} : Bind{}; }

// An action's key in a setting: the scancode, and the modifiers held with it
// in bits 10-13 (Ctrl + F5). The game's keys have none. Old values (a plain
// scancode) mean the same as before.
enum { kModCtrl = 1, kModShift = 2, kModAlt = 4, kModGui = 8 };
constexpr int kScMask = 0x3FF, kModShiftBits = 10;
constexpr int kKeyMax = (SDL_SCANCODE_COUNT - 1) | (15 << kModShiftBits);
Bind keyValueBind(int v) {
  const int sc = v & kScMask;
  return v > 0 && sc > 0 ? Bind{Bind::Key, sc, (v >> kModShiftBits) & 15} : Bind{};
}
int keyValue(const Bind& b) { return b.type == Bind::Key ? b.a | (b.b << kModShiftBits) : 0; }
int modsOf(SDL_Keymod m) {
  return ((m & SDL_KMOD_CTRL) ? kModCtrl : 0) | ((m & SDL_KMOD_SHIFT) ? kModShift : 0) |
         ((m & SDL_KMOD_ALT) ? kModAlt : 0) | ((m & SDL_KMOD_GUI) ? kModGui : 0);
}
// the modifier a key itself is (0: not one)
int modBit(int sc) {
  switch (sc) {
  case SDL_SCANCODE_LCTRL:
  case SDL_SCANCODE_RCTRL: return kModCtrl;
  case SDL_SCANCODE_LSHIFT:
  case SDL_SCANCODE_RSHIFT: return kModShift;
  case SDL_SCANCODE_LALT:
  case SDL_SCANCODE_RALT: return kModAlt;
  case SDL_SCANCODE_LGUI:
  case SDL_SCANCODE_RGUI: return kModGui;
  default: return 0;
  }
}
int modCount(int m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1); }

// A game row's second controller button in a setting (pad2_<row>_p<n>): -1
// none, a button (0-63), or a stick or trigger direction: 64 + axis * 2 + 1
// for its negative side.
Bind pad2Bind(int v) {
  if (v < 0) {
    return {};
  }
  if (v < 64) {
    return Bind{Bind::Button, v, 0};
  }
  return Bind{Bind::Axis, (v - 64) / 2, (v & 1) ? -1 : 1};
}
int pad2Value(const Bind& b) {
  return b.type == Bind::Button ? b.a : b.type == Bind::Axis ? 64 + b.a * 2 + (b.b < 0 ? 1 : 0) : -1;
}

// A controller binding in a setting: -1 none, a button (0-31), or two (the
// first + 1 in bits 5-9). -2: the old setting's choice (rewind_pad, walk_boost_pad).
constexpr int kPadNone = -1, kPadLegacy = -2;
constexpr int combo(int first, int second) { return ((first + 1) << 5) | second; }
Bind padBind(int v) {
  if (v < 0) {
    return {};
  }
  const int first = (v >> 5) - 1, second = v & 31;
  return first >= 0 ? Bind{Bind::Combo, first, second} : Bind{Bind::Button, second, 0};
}
int padValue(const Bind& b) {
  return b.type == Bind::Button ? b.a : b.type == Bind::Combo ? combo(b.a, b.b) : kPadNone;
}

// The game's buttons and stick directions. Keys: WASD and the arrows move,
// Space is A; clear of Paper Cut's own keys. The D-pad, which the game hardly
// uses, and the C-stick have no keys until the player gives them some.
struct GameRow {
  const char* name;
  bool axis;        // a stick direction (PAD_AXIS_*), else a button (PAD_BUTTON_* / PAD_TRIGGER_*)
  u16 target;
  SDL_Scancode key; // the first key's default (UNKNOWN: none)
  const char* key2; // the second key's setting
  SDL_Scancode key2Default;
};
constexpr GameRow kGameRows[] = {
    {"A", false, PAD_BUTTON_A, SDL_SCANCODE_SPACE, "key2_a", SDL_SCANCODE_UNKNOWN},
    {"B", false, PAD_BUTTON_B, SDL_SCANCODE_E, "key2_b", SDL_SCANCODE_UNKNOWN},
    {"X", false, PAD_BUTTON_X, SDL_SCANCODE_R, "key2_x", SDL_SCANCODE_UNKNOWN},
    {"Y", false, PAD_BUTTON_Y, SDL_SCANCODE_F, "key2_y", SDL_SCANCODE_UNKNOWN},
    {"Z", false, PAD_TRIGGER_Z, SDL_SCANCODE_Q, "key2_z", SDL_SCANCODE_UNKNOWN},
    {"L", false, PAD_TRIGGER_L, SDL_SCANCODE_1, "key2_l", SDL_SCANCODE_UNKNOWN},
    {"R", false, PAD_TRIGGER_R, SDL_SCANCODE_3, "key2_r", SDL_SCANCODE_UNKNOWN},
    {"Start", false, PAD_BUTTON_START, SDL_SCANCODE_RETURN, "key2_start", SDL_SCANCODE_UNKNOWN},
    {"D-pad up", false, PAD_BUTTON_UP, SDL_SCANCODE_UNKNOWN, "key2_dpad_up", SDL_SCANCODE_UNKNOWN},
    {"D-pad down", false, PAD_BUTTON_DOWN, SDL_SCANCODE_UNKNOWN, "key2_dpad_down", SDL_SCANCODE_UNKNOWN},
    {"D-pad left", false, PAD_BUTTON_LEFT, SDL_SCANCODE_UNKNOWN, "key2_dpad_left", SDL_SCANCODE_UNKNOWN},
    {"D-pad right", false, PAD_BUTTON_RIGHT, SDL_SCANCODE_UNKNOWN, "key2_dpad_right", SDL_SCANCODE_UNKNOWN},
    {"Stick up", true, PAD_AXIS_LEFT_Y_POS, SDL_SCANCODE_W, "key2_stick_up", SDL_SCANCODE_UP},
    {"Stick down", true, PAD_AXIS_LEFT_Y_NEG, SDL_SCANCODE_S, "key2_stick_down", SDL_SCANCODE_DOWN},
    {"Stick left", true, PAD_AXIS_LEFT_X_NEG, SDL_SCANCODE_A, "key2_stick_left", SDL_SCANCODE_LEFT},
    {"Stick right", true, PAD_AXIS_LEFT_X_POS, SDL_SCANCODE_D, "key2_stick_right", SDL_SCANCODE_RIGHT},
    {"C-stick up", true, PAD_AXIS_RIGHT_Y_POS, SDL_SCANCODE_UNKNOWN, "key2_cstick_up", SDL_SCANCODE_UNKNOWN},
    {"C-stick down", true, PAD_AXIS_RIGHT_Y_NEG, SDL_SCANCODE_UNKNOWN, "key2_cstick_down", SDL_SCANCODE_UNKNOWN},
    {"C-stick left", true, PAD_AXIS_RIGHT_X_NEG, SDL_SCANCODE_UNKNOWN, "key2_cstick_left", SDL_SCANCODE_UNKNOWN},
    {"C-stick right", true, PAD_AXIS_RIGHT_X_POS, SDL_SCANCODE_UNKNOWN, "key2_cstick_right", SDL_SCANCODE_UNKNOWN},
};
constexpr int kGameRowCount = sizeof(kGameRows) / sizeof(kGameRows[0]);

// Paper Cut's actions. Press: on the press (a combo's first button: on its
// release, when no combo came); Hold: down and up; Polled: read by its feature
// (portBindingsHeld).
enum Kind { kPress, kHold, kPolled };
struct ActionRow {
  const char* id;
  const char* name;
  Kind kind;
  int key;          // the default key (0 none; -1: the old setting's choice)
  int pad;          // the default controller binding (kPadNone, a button, combo(); kPadLegacy: the old one's)
  const char* keySetting = nullptr; // the first key's setting, when not bind_<id>_key
};
const ActionRow kActions[PORT_BIND_COUNT] = {
    {"settings", "Settings (open, close)", kPress, SDL_SCANCODE_F1, SDL_GAMEPAD_BUTTON_BACK},
    {"fast_forward", "Fast-forward", kHold, SDL_SCANCODE_TAB, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"ff_toggle", "Fast-forward on and off", kPress, 0, kPadNone},
    {"rewind", "Rewind (held)", kHold, SDL_SCANCODE_BACKSPACE, kPadLegacy, "rewind_key"},
    {"photo", "Photo mode", kPress, SDL_SCANCODE_P, combo(SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK)},
    {"screenshot", "Screenshot", kPress, SDL_SCANCODE_F12, kPadNone},
    {"save_state", "Save state", kPress, SDL_SCANCODE_F5, kPadNone},
    {"load_state", "Load state", kPress, SDL_SCANCODE_F8, kPadNone},
    {"frame_hold", "Frame advance (on, off)", kPress, SDL_SCANCODE_F6, kPadNone},
    {"frame_step", "Frame advance: one frame", kPress, SDL_SCANCODE_F7, kPadNone},
    {"message_log", "Message log", kPress, SDL_SCANCODE_L, kPadNone},
    {"display_mode", "Window or full screen", kPress, SDL_SCANCODE_F11, kPadNone},
    {"volume_down", "Volume down", kPress, SDL_SCANCODE_F9, kPadNone},
    {"volume_up", "Volume up", kPress, SDL_SCANCODE_F10, kPadNone},
    {"mute", "Mute", kPress, SDL_SCANCODE_F3, kPadNone},
    {"quick_swap", "Partner quick-swap", kPolled, SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"walk_boost", "Walk-speed boost", kPolled, -1, kPadLegacy},
    {"ping", "Co-op ping (hold to aim)", kPolled, SDL_SCANCODE_G, SDL_GAMEPAD_BUTTON_MISC1},
    {"lua_console", "Lua console", kPress, SDL_SCANCODE_F4, kPadNone},
    {"partner_hint", "Partner hint", kPress, SDL_SCANCODE_H, kPadNone},
    {"launcher", "Back to the launcher", kPress, SDL_SCANCODE_F2, kPadNone},
    // (the left shoulder: no game button in the controllers' defaults, the game's L being the left trigger)
    {"notes_read", "Read a paper note", kPolled, SDL_SCANCODE_C, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
};
// the old settings' choices (gameplus2.cpp, rewind.cpp), for -1 and kPadLegacy
constexpr SDL_Scancode kWalkKeys[] = {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LALT, SDL_SCANCODE_RSHIFT};
constexpr int kWalkPads[] = {SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_STICK,
                             SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_MISC1};
constexpr int kRewindPads[] = {kPadNone, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
                               SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1};

PortBindHandler sHandlers[PORT_BIND_COUNT]; // (zero before any static initializer runs)

struct ActionSettings {
  int* key;
  int* key2;
  int* pad;
  int* pad2;
};
ActionSettings sSet[PORT_BIND_COUNT];
int* sKey2[kGameRowCount];
int* sPad2[kGameRowCount][4]; // per player: the game rows' second controller button (pad2Bind)
int* sDigital[4]; // per player: the triggers digital

// a game row's short name in settings and profiles ("a", "dpad_up": its key2 setting's)
const char* rowId(int row) { return kGameRows[row].key2 + 5; }

const bool sDeclared = [] {
  for (int i = 0; i < PORT_BIND_COUNT; i++) {
    const ActionRow& a = kActions[i];
    char name[64];
    if (a.keySetting != nullptr) {
      sSet[i].key = portSettingsDeclare(a.keySetting, a.key, 0, kKeyMax);
    } else {
      std::snprintf(name, sizeof(name), "bind_%s_key", a.id);
      sSet[i].key = portSettingsDeclare(name, a.key, -1, kKeyMax);
    }
    std::snprintf(name, sizeof(name), "bind_%s_key2", a.id);
    sSet[i].key2 = portSettingsDeclare(name, 0, 0, kKeyMax);
    std::snprintf(name, sizeof(name), "bind_%s_pad", a.id);
    sSet[i].pad = portSettingsDeclare(name, a.pad, kPadLegacy, 1023);
    std::snprintf(name, sizeof(name), "bind_%s_pad2", a.id);
    sSet[i].pad2 = portSettingsDeclare(name, kPadNone, kPadNone, 1023);
  }
  for (int i = 0; i < kGameRowCount; i++) {
    sKey2[i] = portSettingsDeclare(kGameRows[i].key2, kGameRows[i].key2Default, PAD_KEY_INVALID, SDL_SCANCODE_COUNT - 1);
    for (int p = 0; p < 4; p++) {
      char name[48];
      std::snprintf(name, sizeof(name), "pad2_%s_p%d", rowId(i), p + 1);
      sPad2[i][p] = portSettingsDeclare(name, -1, -1, 127);
    }
  }
  for (int p = 0; p < 4; p++) {
    char name[32];
    std::snprintf(name, sizeof(name), "bind_digital_triggers_p%d", p + 1);
    sDigital[p] = portSettingsDeclare(name, 0, 0, 1);
  }
  return true;
}();

int legacy(const char* key, int count) {
  const int* v = portSettingsFind(key);
  return v != nullptr && *v >= 0 && *v < count ? *v : 0;
}

// an action's bindings now
// an action's key: the scancode and its modifiers (keyValueBind), or 0
int actionKey(int a, int col) {
  int v = col == 0 ? *sSet[a].key : *sSet[a].key2;
  if (v < 0 && a == PORT_BIND_WALK_BOOST) {
    v = kWalkKeys[legacy("walk_boost_key", 4)];
  }
  return v > 0 && (v & kScMask) > 0 ? v : 0;
}
// an action's controller binding: n 0 the first, 1 the second
Bind actionPad(int a, int n = 0) {
  if (n != 0) {
    return padBind(*sSet[a].pad2);
  }
  int v = *sSet[a].pad;
  if (v == kPadLegacy) {
    v = a == PORT_BIND_REWIND ? kRewindPads[legacy("rewind_pad", 4)] : kWalkPads[legacy("walk_boost_pad", 4)];
  }
  return padBind(v);
}
Bind actionDefault(int a, int col) {
  const ActionRow& r = kActions[a];
  if (col == 2) {
    return padBind(r.pad == kPadLegacy ? (a == PORT_BIND_REWIND ? kPadNone : kWalkPads[0]) : r.pad);
  }
  return col == 0 ? keyBind(r.key < 0 ? kWalkKeys[0] : r.key) : Bind{};
}

// --- The game's bindings (Aurora's) ------------------------------------------------

s32 gameKey(int row) {
  const GameRow& r = kGameRows[row];
  u32 count = 0;
  if (r.axis) {
    const PADKeyAxisBinding* axes = PADGetKeyAxisBindings(keyboardPort(), &count);
    for (u32 i = 0; axes != nullptr && i < count; i++) {
      if (axes[i].padAxis == r.target) {
        return axes[i].scancode;
      }
    }
  } else {
    const PADKeyButtonBinding* buttons = PADGetKeyButtonBindings(keyboardPort(), &count);
    for (u32 i = 0; buttons != nullptr && i < count; i++) {
      if (buttons[i].padButton == r.target) {
        return buttons[i].scancode;
      }
    }
  }
  return PAD_KEY_INVALID;
}

void setGameKey(int row, s32 scancode, bool save = true) {
  const GameRow& r = kGameRows[row];
  if (r.axis) {
    PADSetKeyAxisBinding(keyboardPort(), PADKeyAxisBinding{scancode, r.target, 100});
  } else {
    PADSetKeyButtonBinding(keyboardPort(), PADKeyButtonBinding{scancode, r.target});
  }
  PADSetKeyboardActive(keyboardPort(), TRUE);
  if (save) {
    PADSerializeMappings();
  }
}

PADButtonMapping* buttonMap(u32 port, u16 padButton) {
  u32 count = 0;
  PADButtonMapping* maps = PADGetButtonMappings(port, &count);
  for (u32 i = 0; maps != nullptr && i < count; i++) {
    if (maps[i].padButton == padButton) {
      return &maps[i];
    }
  }
  return nullptr;
}
PADAxisMapping* axisMap(u32 port, u16 padAxis) {
  u32 count = 0;
  PADAxisMapping* axes = PADGetAxisMappings(port, &count);
  for (u32 i = 0; axes != nullptr && i < count; i++) {
    if (axes[i].padAxis == padAxis) {
      return &axes[i];
    }
  }
  return nullptr;
}
bool axisValid(const PADAxisMapping& m) {
  return m.nativeAxis.nativeAxis >= 0 && m.nativeAxis.nativeAxis < SDL_GAMEPAD_AXIS_COUNT;
}
bool buttonValid(s64 b) { return b >= 0 && b < SDL_GAMEPAD_BUTTON_COUNT; }

// L and R read their trigger (an axis) while no button is theirs
u16 triggerAxis(u16 padButton) {
  return padButton == PAD_TRIGGER_L ? PAD_AXIS_TRIGGER_L : padButton == PAD_TRIGGER_R ? PAD_AXIS_TRIGGER_R : 0xFFFF;
}

Bind gamePad(u32 port, int row) {
  const GameRow& r = kGameRows[row];
  if (r.axis) {
    const PADAxisMapping* m = axisMap(port, r.target);
    if (m == nullptr) {
      return {};
    }
    if (axisValid(*m)) {
      return Bind{Bind::Axis, m->nativeAxis.nativeAxis, m->nativeAxis.sign};
    }
    return buttonValid(m->nativeButton) ? Bind{Bind::Button, m->nativeButton, 0} : Bind{};
  }
  const PADButtonMapping* m = buttonMap(port, r.target);
  if (m != nullptr && buttonValid(m->nativeButton)) {
    return Bind{Bind::Button, static_cast<int>(m->nativeButton), 0};
  }
  if (triggerAxis(r.target) != 0xFFFF) {
    const PADAxisMapping* t = axisMap(port, triggerAxis(r.target));
    if (t != nullptr && axisValid(*t)) {
      return Bind{Bind::Axis, t->nativeAxis.nativeAxis, t->nativeAxis.sign};
    }
  }
  return {};
}

void setGamePad(u32 port, int row, const Bind& b, bool save = true) {
  const GameRow& r = kGameRows[row];
  if (r.axis) {
    PADAxisMapping* m = axisMap(port, r.target);
    if (m == nullptr) {
      return;
    }
    PADAxisMapping n = *m;
    n.nativeAxis = {b.type == Bind::Axis ? b.a : -1, b.type == Bind::Axis && b.b < 0 ? AXIS_SIGN_NEGATIVE : AXIS_SIGN_POSITIVE};
    n.nativeButton = b.type == Bind::Button ? b.a : SDL_GAMEPAD_BUTTON_INVALID;
    PADSetAxisMapping(port, n);
  } else {
    PADButtonMapping* m = buttonMap(port, r.target);
    if (m == nullptr) {
      return;
    }
    PADButtonMapping n = *m;
    n.nativeButton = b.type == Bind::Button ? static_cast<u32>(b.a) : PAD_NATIVE_BUTTON_INVALID;
    PADSetButtonMapping(port, n);
    if (triggerAxis(r.target) != 0xFFFF) {
      // L or R: on a button, or on an axis (its analog value and, past the click point, the button)
      if (PADAxisMapping* t = axisMap(port, triggerAxis(r.target))) {
        PADAxisMapping a = *t;
        a.nativeAxis = {b.type == Bind::Axis ? b.a : -1, AXIS_SIGN_POSITIVE};
        a.nativeButton = SDL_GAMEPAD_BUTTON_INVALID;
        PADSetAxisMapping(port, a);
      }
    }
  }
  if (save) {
    PADSerializeMappings();
  }
}

// a row's controller binding as Aurora's defaults have it (all rows worked
// out once a frame: the defaults put in, read, and the player's put back)
Bind gamePadDefault(u32 port, int row) {
  static Bind sDefault[kGameRowCount];
  static int sFrame = -1;
  static u32 sPort;
  const int frame = ImGui::GetCurrentContext() != nullptr ? ImGui::GetFrameCount() : -2;
  if (frame != sFrame || port != sPort || frame < 0) {
    sFrame = frame;
    sPort = port;
    u32 nb = 0, na = 0;
    PADButtonMapping* maps = PADGetButtonMappings(port, &nb);
    PADAxisMapping* axes = PADGetAxisMappings(port, &na);
    if (maps == nullptr || axes == nullptr) {
      std::fill(std::begin(sDefault), std::end(sDefault), Bind{});
      return {};
    }
    std::vector<PADButtonMapping> savedB(maps, maps + nb);
    std::vector<PADAxisMapping> savedA(axes, axes + na);
    PADRestoreDefaultMapping(port);
    for (int i = 0; i < kGameRowCount; i++) {
      sDefault[i] = gamePad(port, i);
    }
    for (const PADButtonMapping& m : savedB) {
      PADSetButtonMapping(port, m);
    }
    for (const PADAxisMapping& m : savedA) {
      PADSetAxisMapping(port, m);
    }
  }
  return sDefault[row];
}

// --- Whose a key or a button is ------------------------------------------------

// one of the game's keys (player 1's keyboard): Paper Cut's actions give way
bool gameKeyIs(int scancode) {
  if (scancode <= 0) {
    return false;
  }
  for (int i = 0; i < kGameRowCount; i++) {
    if (gameKey(i) == scancode || *sKey2[i] == scancode) {
      return true;
    }
  }
  return false;
}

// a controller's button the player gave to one of the game's buttons or
// stick directions (Bindings, one-handed controls)
bool gameButtonIs(SDL_Gamepad* pad, int button) {
  for (u32 port = 0; pad != nullptr && port < 4; port++) {
    const s32 index = PADGetIndexForPort(port);
    if (index < 0 || PADGetSDLGamepadForIndex(static_cast<u32>(index)) != pad) {
      continue;
    }
    u32 count = 0;
    const PADButtonMapping* maps = PADGetButtonMappings(port, &count);
    for (u32 i = 0; maps != nullptr && i < count; i++) {
      if (maps[i].nativeButton == static_cast<u32>(button)) {
        return true;
      }
    }
    const PADAxisMapping* axes = PADGetAxisMappings(port, &count);
    for (u32 i = 0; axes != nullptr && i < count; i++) {
      if (!axisValid(axes[i]) && axes[i].nativeButton == button) {
        return true;
      }
    }
    for (int i = 0; i < kGameRowCount; i++) { // the second buttons
      const Bind b = pad2Bind(*sPad2[i][port]);
      if (b.type == Bind::Button && b.a == button) {
        return true;
      }
    }
  }
  return false;
}

std::vector<SDL_Gamepad*> gamepads() {
  std::vector<SDL_Gamepad*> v;
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for (int i = 0; ids != nullptr && i < count; i++) {
    if (SDL_Gamepad* p = SDL_GetGamepadFromID(ids[i])) {
      v.push_back(p);
    }
  }
  SDL_free(ids);
  return v;
}

bool held(SDL_Gamepad* p, int b) { return p != nullptr && buttonValid(b) && SDL_GetGamepadButton(p, SDL_GamepadButton(b)); }

// --- Names -------------------------------------------------------------------

GlyphStyle styleFor(u32 port) { return glyphStyleOfPort(port); }

const char* axisName(GlyphStyle st, int axis, int sign) {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX: return sign < 0 ? "Left stick left" : "Left stick right";
  case SDL_GAMEPAD_AXIS_LEFTY: return sign < 0 ? "Left stick up" : "Left stick down";
  case SDL_GAMEPAD_AXIS_RIGHTX: return sign < 0 ? "Right stick left" : "Right stick right";
  case SDL_GAMEPAD_AXIS_RIGHTY: return sign < 0 ? "Right stick up" : "Right stick down";
  case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return nativeButtonLabel(st, PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L);
  case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return nativeButtonLabel(st, PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R);
  default: return "None";
  }
}

// the modifiers before a key's name ("Ctrl + Shift + "), translated for the
// page (portTr) or the game's font (portTrGame)
std::string modsName(int m, bool game) {
  auto tr = [game](const char* s) { return game ? portTrGame(s) : portTr(s); };
#if defined(__APPLE__)
  const char *gui = "Cmd", *alt = "Option";
#elif defined(_WIN32)
  const char *gui = "Win", *alt = "Alt";
#else
  const char *gui = "Super", *alt = "Alt";
#endif
  std::string s;
  const struct {
    int bit;
    const char* name;
  } kMods[] = {{kModCtrl, "Ctrl"}, {kModShift, "Shift"}, {kModAlt, alt}, {kModGui, gui}};
  for (const auto& k : kMods) {
    if (m & k.bit) {
      s += tr(k.name);
      s += " + ";
    }
  }
  return s;
}

// a binding's name in the menu language
std::string bindName(const Bind& b, GlyphStyle st, bool game = false) {
  switch (b.type) {
  case Bind::Key: {
    const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(b.a));
    return modsName(b.b, game) + (n != nullptr && n[0] != '\0' ? n : "?");
  }
  case Bind::Button: return portTr(nativeButtonLabel(st, static_cast<u32>(b.a), 0));
  case Bind::Combo:
    return std::string(portTr(nativeButtonLabel(st, static_cast<u32>(b.a), 0))) + " + " +
           portTr(nativeButtonLabel(st, static_cast<u32>(b.b), 0));
  case Bind::Axis: return portTr(axisName(st, b.a, b.b));
  default: return "";
  }
}

// --- Cells: (game row or action, column) -------------------------------------

enum { kGroupGame, kGroupAction };
struct Cell {
  int group = -1, row = 0, col = 0; // col: 0 key, 1 second key, 2 controller, 3 second button
  bool operator==(const Cell& o) const { return group == o.group && row == o.row && col == o.col; }
};
constexpr int kCols = 4;

int sPlayer; // the page's player (0-3): the game rows' controller columns

void profileTouched(); // a binding changed: into the page's player's profile (Profiles, below)

Bind cellGet(const Cell& c) {
  if (c.group == kGroupGame) {
    if (c.col == 0) {
      return keyBind(gameKey(c.row));
    }
    if (c.col == 1) {
      return keyBind(*sKey2[c.row]);
    }
    if (c.col == 3) {
      return pad2Bind(*sPad2[c.row][sPlayer & 3]);
    }
    return gamePad(static_cast<u32>(sPlayer), c.row);
  }
  return c.col >= 2 ? actionPad(c.row, c.col - 2) : keyValueBind(actionKey(c.row, c.col));
}

// what a cell takes: keys for the keyboard's (an action's with modifiers); a
// button (or for L, R and the sticks a stick or trigger direction) for a game
// row's controller, and a button or any stick or trigger direction for its
// second; a button or a combo for an action's
bool cellTakes(const Cell& c, const Bind& b) {
  if (b.type == Bind::None) {
    return true;
  }
  if (c.col < 2) {
    return b.type == Bind::Key && (c.group == kGroupAction || b.b == 0);
  }
  if (c.group == kGroupAction) {
    return b.type == Bind::Button || b.type == Bind::Combo;
  }
  if (c.col == 3) {
    return b.type == Bind::Button || b.type == Bind::Axis;
  }
  const GameRow& r = kGameRows[c.row];
  return b.type == Bind::Button || (b.type == Bind::Axis && (r.axis || triggerAxis(r.target) != 0xFFFF));
}

void cellSet(const Cell& c, const Bind& b) {
  if (!cellTakes(c, b)) {
    return;
  }
  if (c.group == kGroupGame) {
    if (c.col == 0) {
      setGameKey(c.row, b.type == Bind::Key ? b.a : PAD_KEY_INVALID);
    } else if (c.col == 1) {
      *sKey2[c.row] = b.type == Bind::Key ? b.a : PAD_KEY_INVALID;
      portSettingsSave();
    } else if (c.col == 3) {
      *sPad2[c.row][sPlayer & 3] = pad2Value(b);
      portSettingsSave();
    } else {
      setGamePad(static_cast<u32>(sPlayer), c.row, b);
    }
    profileTouched();
    return;
  }
  if (c.col == 2) {
    *sSet[c.row].pad = padValue(b);
  } else if (c.col == 3) {
    *sSet[c.row].pad2 = padValue(b);
  } else {
    *(c.col == 0 ? sSet[c.row].key : sSet[c.row].key2) = keyValue(b);
  }
  portSettingsSave();
  profileTouched();
}

Bind cellDefault(const Cell& c) {
  if (c.group == kGroupGame) {
    if (c.col == 0) {
      return keyBind(kGameRows[c.row].key);
    }
    if (c.col == 1) {
      return keyBind(kGameRows[c.row].key2Default);
    }
    if (c.col == 3) {
      return {};
    }
    return gamePadDefault(static_cast<u32>(sPlayer), c.row);
  }
  return actionDefault(c.row, c.col);
}

// a cell the page's player can't use: the keyboard's game rows for players
// 2-4, a game row's controller cells without a controller
bool haveController(u32 port);
bool cellOff(const Cell& c) {
  return c.group == kGroupGame &&
         (c.col < 2 ? sPlayer != 0 : !haveController(static_cast<u32>(sPlayer)));
}

// the other cell already holding b (or a part of it: a combo's button that is
// a game button), if any
Cell conflictOf(const Cell& self, const Bind& b) {
  if (b.type == Bind::None) {
    return {};
  }
  for (int g = 0; g < 2; g++) {
    const int rows = g == kGroupGame ? kGameRowCount : PORT_BIND_COUNT;
    for (int r = 0; r < rows; r++) {
      for (int col = 0; col < kCols; col++) {
        const Cell c{g, r, col};
        if (c == self || (col < 2) != (self.col < 2)) {
          continue;
        }
        if (cellGet(c) == b) {
          return c;
        }
      }
    }
  }
  return {};
}

const char* cellRowName(const Cell& c) {
  return portTr(c.group == kGroupGame ? kGameRows[c.row].name : kActions[c.row].name);
}

// --- Asking for a key or button ----------------------------------------------

struct Capture {
  Cell cell;
  Uint64 since = 0;
  bool armed = false; // everything was let go since (the press that chose the cell isn't taken)
  int first = -1;     // an action's first button, held (a combo's, if a second comes)
  SDL_JoystickID pad = 0;
  bool menu = false;  // the Start menu's page (player 1's; set at once, a conflict only shown)
  int modKey = 0;     // an action's key: a modifier held, waiting for a key with it (or its own release)
  Uint64 startSince = 0; // Start held since (a second of it leaves the question; a shorter press is Start)
  Uint64 escSince = 0;   // a key's question: Escape held since (let go: it is left; a second held: Escape the key)
  int escMods = 0;       // (an action's: the modifiers held with that Escape)
} sCap;
constexpr Uint64 kCancelHold = 1000; // ms of Start held that leave the question
constexpr Uint64 kEscapeHold = 1000; // ms of Escape held that make Escape the key (a shorter press leaves it)
bool sSettle;        // a binding was just set: the page and the menu wait until everything is let go
int sSettleKey;      // (and the key that was the answer: the Start menu's page reads the game's keys, E its B)
int sLastDrawn = -1; // the frame the section was last drawn (asking ends when it isn't)

struct Pending { // a binding that is already another row's: Swap, Keep both or Cancel
  Cell cell, other;
  Bind want, had;
  bool open = false;
} sPending;
bool sPopupShown; // its question on screen (B and Back answer it, not the page)

void finish(const Cell c, const Bind b) { // (copies: called with sCap.cell, which it clears)
  const bool menu = sCap.menu;
  sCap = {};
  sSettle = true;
  sSettleKey = b.type == Bind::Key ? b.a : 0;
  const Cell other = conflictOf(c, b);
  if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr) {
    std::printf("[padbind] answer: cell %d,%d,%d gets type %d (%d,%d); already %d,%d,%d's\n", c.group, c.row, c.col,
                int(b.type), b.a, b.b, other.group, other.row, other.col);
    std::fflush(stdout);
  }
  if (other.group < 0 || menu) { // (the menu shows a conflict in its help line instead)
    cellSet(c, b);
    return;
  }
  sPending = {c, other, b, cellGet(c), true};
}

void cancelCapture() {
  if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr && sCap.cell.group >= 0) {
    std::printf("[padbind] question left (cell %d,%d,%d; start held since %llu, now %llu)\n", sCap.cell.group,
                sCap.cell.row, sCap.cell.col, static_cast<unsigned long long>(sCap.startSince),
                static_cast<unsigned long long>(SDL_GetTicks()));
    std::fflush(stdout);
  }
  sCap = {};
  sSettle = true;
}

bool anythingHeld() {
  for (SDL_Gamepad* p : gamepads()) {
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
      if (SDL_GetGamepadButton(p, SDL_GamepadButton(b))) {
        return true;
      }
    }
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) {
      if (std::abs(SDL_GetGamepadAxis(p, SDL_GamepadAxis(a))) >= 16384) {
        return true;
      }
    }
  }
  // the key just given (E, the game's B: the Start menu's page would take its press as back)
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  if (sSettleKey > 0 && sSettleKey < count && keys != nullptr && keys[sSettleKey]) {
    return true;
  }
  sSettleKey = 0;
  return false;
}

// the controllers that answer the question: the page's player's for a game row's
// button, any for the rest
std::vector<SDL_Gamepad*> askedPads() {
  if (sCap.cell.group == kGroupGame && sCap.cell.col >= 2) {
    const s32 index = PADGetIndexForPort(static_cast<u32>(sPlayer));
    SDL_Gamepad* p = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
    return p != nullptr ? std::vector<SDL_Gamepad*>{p} : std::vector<SDL_Gamepad*>{};
  }
  return gamepads();
}

// a key's question also names the controller's ways out of it ({BACK}, {MENU} held): with a controller in
// hand only, its prompts a controller's (a keyboard player reads {BACK} as the game's B, the key E)
bool keyQuestionPadToo() { return portMenuPadInUse() != 0 && styleFor(0) != GlyphStyle::Keyboard; }

// each frame: a controller button (or stick) for the cell being asked about.
// The press that chose the cell is let go first (so that button can be the
// answer too); Start held for a second leaves the question with a controller
// alone (a shorter press of Start is the answer Start); a key's question is
// also left with the controller's back button.
void pollCapture() {
  if (sSettle && !anythingHeld()) {
    sSettle = false;
  }
  if (sCap.cell.group < 0) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  if (sCap.escSince != 0) {
    // Escape held for a second: Escape itself is the key (let go before that, it left the question: keyCaptureUp)
    if (now - sCap.escSince >= kEscapeHold) {
      finish(sCap.cell, Bind{Bind::Key, SDL_SCANCODE_ESCAPE, sCap.escMods});
    }
    return;
  }
  if (now - sCap.since > 5000 && sCap.first < 0 && sCap.modKey == 0 && sCap.startSince == 0) {
    cancelCapture();
    return;
  }
  const bool padCell = sCap.cell.col >= 2;
  if (!sCap.armed) {
    if (!anythingHeld()) {
      sCap.armed = true;
      sCap.since = now;
    }
    return;
  }
  const std::vector<SDL_Gamepad*> pads = askedPads();
  bool start = false;
  for (SDL_Gamepad* p : pads) {
    start = start || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_START);
  }
  if (start && sCap.first < 0 && padCell) {
    // another button with Start: an action's combo Start + it; a game row's, that button
    for (SDL_Gamepad* p : pads) {
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        if (b == SDL_GAMEPAD_BUTTON_START || !SDL_GetGamepadButton(p, SDL_GamepadButton(b))) {
          continue;
        }
        const Bind got = sCap.cell.group == kGroupAction ? Bind{Bind::Combo, SDL_GAMEPAD_BUTTON_START, b}
                                                         : Bind{Bind::Button, b, 0};
        if (cellTakes(sCap.cell, got)) {
          finish(sCap.cell, got);
          return;
        }
      }
    }
  }
  if (start && sCap.first < 0) {
    if (sCap.startSince == 0) {
      sCap.startSince = now;
    } else if (now - sCap.startSince >= kCancelHold) {
      cancelCapture(); // (sSettle: nothing more until Start is let go)
    }
    return;
  }
  if (sCap.startSince != 0 && sCap.first < 0) {
    sCap.startSince = 0; // let go before the second: Start is the answer
    const Bind st{Bind::Button, SDL_GAMEPAD_BUTTON_START, 0};
    if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr) {
      std::printf("[padbind] Start let go: cell %d,%d,%d takes it %d\n", sCap.cell.group, sCap.cell.row, sCap.cell.col,
                  int(cellTakes(sCap.cell, st)));
      std::fflush(stdout);
    }
    if (padCell && cellTakes(sCap.cell, st)) {
      finish(sCap.cell, st);
    }
    return;
  }
  if (!padCell) {
    // a key (portBindingsEvent); the controller's back button leaves it
    for (SDL_Gamepad* p : pads) {
      if (SDL_GetGamepadButton(p, SDL_GamepadButton(portMenuRoleButton(PORT_MENU_BACK, SDL_GetGamepadID(p))))) {
        cancelCapture();
        return;
      }
    }
    return;
  }
  if (sCap.cell.group == kGroupGame) {
    const u32 port = static_cast<u32>(sPlayer);
    const int row = sCap.cell.row;
    const s32 b = PADGetNativeButtonPressed(port);
    Bind got;
    if (b == SDL_GAMEPAD_BUTTON_START) {
      return; // (Start: above)
    }
    if (b >= 0) {
      got = Bind{Bind::Button, b, 0};
    } else {
      const PADSignedNativeAxis ax = PADGetNativeAxisPulled(port);
      if (ax.nativeAxis >= 0) {
        got = Bind{Bind::Axis, ax.nativeAxis, ax.sign};
      }
    }
    const Cell c{kGroupGame, row, sCap.cell.col};
    if (got.type == Bind::None || !cellTakes(c, got)) {
      return;
    }
    finish(c, got);
    return;
  }
  // an action: one button, or a second while the first is held
  if (sCap.first < 0) {
    for (SDL_Gamepad* p : pads) {
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        if (b != SDL_GAMEPAD_BUTTON_START && SDL_GetGamepadButton(p, SDL_GamepadButton(b))) {
          sCap.first = b;
          sCap.pad = SDL_GetGamepadID(p);
          return;
        }
      }
    }
    return;
  }
  SDL_Gamepad* p = SDL_GetGamepadFromID(sCap.pad);
  if (p == nullptr) {
    cancelCapture();
    return;
  }
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
    if (b != sCap.first && SDL_GetGamepadButton(p, SDL_GamepadButton(b))) {
      finish(sCap.cell, Bind{Bind::Combo, sCap.first, b});
      return;
    }
  }
  if (!SDL_GetGamepadButton(p, SDL_GamepadButton(sCap.first))) {
    finish(sCap.cell, Bind{Bind::Button, sCap.first, 0});
  }
}

void ask(const Cell c) { // (a copy: sCap is cleared first)
  sCap = {};
  sCap.cell = c;
  sCap.since = SDL_GetTicks();
}

// --- Doing the actions ---------------------------------------------------------

bool sPageOpen;
int sKeyHeld[PORT_BIND_COUNT]; // the key holding a Hold action down (0 none)

bool call(int a, int down, int repeat) {
  if (sHandlers[a] == nullptr || (kActions[a].kind == kPress && !down)) {
    return false;
  }
  static const bool sTest = SDL_getenv("TTYD_BINDINGS_TEST") != nullptr;
  if (sTest) {
    std::printf("[bindings] action %s %s\n", kActions[a].id, down ? "down" : "up");
    std::fflush(stdout);
  }
  return sHandlers[a](down, repeat) != 0;
}

// a controller's state for combos and Hold actions
struct PadState {
  SDL_JoystickID id = 0;
  int holding[SDL_GAMEPAD_BUTTON_COUNT];  // the Hold action this button pressed (-1 none)
  int deferred[SDL_GAMEPAD_BUTTON_COUNT]; // the Press action waiting for this button's release (-1)
  bool comboUsed[SDL_GAMEPAD_BUTTON_COUNT];
};
std::vector<PadState> sPads;
struct ActiveCombo {
  SDL_JoystickID id;
  int action, a, b;
};
std::vector<ActiveCombo> sCombos;

PadState& padState(SDL_JoystickID id) {
  for (PadState& s : sPads) {
    if (s.id == id) {
      return s;
    }
  }
  PadState s;
  s.id = id;
  std::fill(std::begin(s.holding), std::end(s.holding), -1);
  std::fill(std::begin(s.deferred), std::end(s.deferred), -1);
  std::fill(std::begin(s.comboUsed), std::end(s.comboUsed), false);
  sPads.push_back(s);
  return sPads.back();
}

// b is one of an action's combos (so alone it waits for its release)
bool inCombo(int b) {
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    for (int n = 0; n < 2; n++) {
      const Bind p = actionPad(a, n);
      if (p.type == Bind::Combo && (p.a == b || p.b == b)) {
        return true;
      }
    }
  }
  return false;
}

// an action allowed now: on the page only Settings (the page closes with it)
bool allowed(int a) { return (!sPageOpen && !gPortLauncherShown) || a == PORT_BIND_SETTINGS; }

void releaseAll() {
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    if (sKeyHeld[a] != 0) {
      sKeyHeld[a] = 0;
      call(a, 0, 0);
    }
  }
  for (PadState& s : sPads) {
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
      if (s.holding[b] >= 0) {
        call(s.holding[b], 0, 0);
      }
      s.holding[b] = s.deferred[b] = -1;
      s.comboUsed[b] = false;
    }
  }
  for (const ActiveCombo& c : sCombos) {
    call(c.action, 0, 0);
  }
  sCombos.clear();
}

int buttonDown(SDL_JoystickID id, int b) {
  SDL_Gamepad* gp = SDL_GetGamepadFromID(id);
  PadState& st = padState(id);
  if (!buttonValid(b)) {
    return 0;
  }
  bool used = false;
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    for (int n = 0; n < 2; n++) { // (its first binding or its second: once)
      const Bind p = actionPad(a, n);
      if (p.type != Bind::Combo || !allowed(a) || kActions[a].kind == kPolled) {
        continue;
      }
      const int other = p.a == b ? p.b : p.b == b ? p.a : -1;
      if (other < 0 || !held(gp, other) || (!sPageOpen && gameButtonIs(gp, p.a) && gameButtonIs(gp, p.b))) {
        continue;
      }
      for (int x : {p.a, p.b}) {
        st.comboUsed[x] = true;
        st.deferred[x] = -1;
        if (st.holding[x] >= 0) { // the first button's own Hold action gives way to the combo
          call(st.holding[x], 0, 0);
          st.holding[x] = -1;
        }
      }
      call(a, 1, 0);
      if (kActions[a].kind == kHold) {
        sCombos.push_back({id, a, p.a, p.b});
      }
      used = true;
      break;
    }
  }
  if (used) {
    return 1;
  }
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    const Bind p0 = actionPad(a, 0), p1 = actionPad(a, 1);
    const Bind p = p0.type == Bind::Button && p0.a == b ? p0 : p1;
    if (p.type != Bind::Button || p.a != b || !allowed(a) || kActions[a].kind == kPolled) {
      continue;
    }
    if (!sPageOpen && gameButtonIs(gp, b)) {
      continue; // the player gave it to one of the game's buttons: the game's
    }
    if (kActions[a].kind == kPress && inCombo(b)) {
      st.deferred[b] = a;
      used = true;
      continue;
    }
    used = call(a, 1, 0) || used;
    if (kActions[a].kind == kHold) {
      st.holding[b] = a;
      used = true;
    }
  }
  return used ? 1 : 0;
}

int buttonUp(SDL_JoystickID id, int b) {
  if (!buttonValid(b)) {
    return 0;
  }
  PadState& st = padState(id);
  bool used = false;
  for (size_t i = 0; i < sCombos.size();) {
    if (sCombos[i].id == id && (sCombos[i].a == b || sCombos[i].b == b)) {
      call(sCombos[i].action, 0, 0);
      sCombos.erase(sCombos.begin() + static_cast<long>(i));
      used = true;
    } else {
      i++;
    }
  }
  if (st.holding[b] >= 0) {
    call(st.holding[b], 0, 0);
    st.holding[b] = -1;
    used = true;
  }
  if (st.deferred[b] >= 0) {
    const int a = st.deferred[b];
    st.deferred[b] = -1;
    if (!st.comboUsed[b] && allowed(a)) {
      call(a, 1, 0);
    }
    used = true;
  }
  st.comboUsed[b] = false;
  return used ? 1 : 0;
}

// a key pressed, with these modifiers held: the actions on it whose
// modifiers are all held, those with the most of them (Ctrl + F5 over F5);
// one with none gives way to the game's own key
int keyDown(int sc, bool repeat, int modsHeld) {
  if (sc <= 0) {
    return 0;
  }
  const int held = modsHeld & ~modBit(sc);
  int fits[PORT_BIND_COUNT], best = -1;
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    fits[a] = -1;
    for (int col = 0; col < 2 && kActions[a].kind != kPolled; col++) {
      const Bind k = keyValueBind(actionKey(a, col));
      if (k.type == Bind::Key && k.a == sc && (k.b & ~held) == 0) {
        fits[a] = std::max(fits[a], modCount(k.b));
      }
    }
    best = std::max(best, fits[a]);
  }
  if (best < 0 || (best == 0 && gameKeyIs(sc))) {
    return 0;
  }
  bool used = false;
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    if (fits[a] != best) {
      continue;
    }
    if (kActions[a].kind == kHold) {
      if (!repeat && sKeyHeld[a] == 0) {
        sKeyHeld[a] = sc;
        used = call(a, 1, 0) || used;
      } else {
        used = true; // (a held key's repeats are its own)
      }
      continue;
    }
    used = call(a, 1, repeat ? 1 : 0) || used;
  }
  return used ? 1 : 0;
}

int keyUp(int sc) {
  bool used = false;
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    if (sKeyHeld[a] == sc && sc != 0) {
      sKeyHeld[a] = 0;
      call(a, 0, 0);
      used = true;
    }
  }
  return used ? 1 : 0;
}

// the actions Paper Cut does itself (the features set theirs)
int settingsAction(int, int) {
  portSettingsToggle();
  return 1;
}
int ffAction(int down, int) {
  portSpeedControl(down);
  return 1;
}
int ffToggleAction(int, int) {
  portSpeedToggle();
  return 1;
}
int shotAction(int, int repeat) {
  if (!repeat) {
    portShotRequestUser();
  }
  return 1;
}
int saveAction(int, int repeat) {
  if (!repeat) {
    portSaveStateRequest(0, 0);
  }
  return 1;
}
int loadAction(int, int repeat) {
  if (!repeat) {
    portSaveStateRequest(0, 1);
  }
  return 1;
}
int displayAction(int, int repeat) {
  if (!repeat) {
    portWindowToggleFullscreen();
  }
  return 1;
}

const bool sCore = [] {
  portBindingsSetHandler(PORT_BIND_SETTINGS, [](int down, int repeat) { return repeat ? 1 : settingsAction(down, 0); });
  portBindingsSetHandler(PORT_BIND_FAST_FORWARD, ffAction);
  portBindingsSetHandler(PORT_BIND_FF_TOGGLE, [](int, int repeat) { return repeat ? 1 : ffToggleAction(1, 0); });
  portBindingsSetHandler(PORT_BIND_SCREENSHOT, shotAction);
  portBindingsSetHandler(PORT_BIND_SAVE_STATE, saveAction);
  portBindingsSetHandler(PORT_BIND_LOAD_STATE, loadAction);
  portBindingsSetHandler(PORT_BIND_DISPLAY_MODE, displayAction);
  return true;
}();

// --- The page ---------------------------------------------------------------------

const char* playerName(int p) {
  static const char* kNames[] = {"Player 1", "Player 2", "Player 3", "Player 4"};
  return portTr(kNames[p & 3]);
}

bool haveController(u32 port) {
  u32 count = 0;
  return PADGetButtonMappings(port, &count) != nullptr && count > 0;
}

Cell sTestFocus; // tests ($TTYD_PADBIND_TEST): the cell the controller's cursor is put on, once
void padTestFrame();

// a binding's name, with its icon in front for a face button, a shoulder or Start while a controller's
// prompts are shown (as the controller's prompts draw it)
// (button_markup.h's token for a controller button that has an icon, while the prompts are a controller's)
const char* iconToken(const Bind& b) {
  GlyphStyle prompts;
  if (b.type != Bind::Button || !buttonPromptsStyle(&prompts) || prompts == GlyphStyle::Keyboard) {
    return nullptr;
  }
  return b.a == SDL_GAMEPAD_BUTTON_SOUTH            ? "{SOUTH}"
         : b.a == SDL_GAMEPAD_BUTTON_EAST           ? "{EAST}"
         : b.a == SDL_GAMEPAD_BUTTON_WEST           ? "{WEST}"
         : b.a == SDL_GAMEPAD_BUTTON_NORTH          ? "{NORTH}"
         : b.a == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER  ? "{LB}"
         : b.a == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER ? "{RB}"
         : b.a == SDL_GAMEPAD_BUTTON_START          ? "{MENU}"
                                                    : nullptr;
}

std::string bindLabel(const Bind& b, GlyphStyle st) {
  const char* token = iconToken(b);
  return token != nullptr ? std::string(portButtonsUi(token)) + " " + bindName(b, st) : bindName(b, st);
}

// one cell's button: its binding, or "Press a key..." while it is asked about
void drawCell(const Cell& c, float width) {
  ImGui::PushID(c.group * 1000 + c.row * kCols + c.col);
  const bool asking = sCap.cell == c;
  const bool padCell = c.col >= 2;
  const bool noPad = padCell && cellOff(c);
  const bool noKeys = !padCell && cellOff(c);
  std::string label;
  if (asking) {
    label = portTr(padCell ? (c.group == kGroupAction ? "Press a button (or two)..." : "Press a button...")
                   : sCap.modKey != 0 ? "Now the key (or let go)..."
                   : c.group == kGroupAction ? "Press a key (or a combination)..."
                                             : "Press a key...");
  } else if (noPad || noKeys) {
    label = "-";
  } else {
    const Bind b = cellGet(c);
    label = b.type == Bind::None ? std::string(portTr("(none)"))
                                 : bindLabel(b, styleFor(c.group == kGroupGame ? static_cast<u32>(sPlayer) : 0));
  }
  const bool isDefault = !asking && !noPad && !noKeys && cellGet(c) == cellDefault(c);
  ImGui::BeginDisabled(noPad || noKeys);
  if (!isDefault && !asking) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.20f, 0.05f, 1.f)); // changed: a warmer ink
  }
  if (asking) {
    // waiting for the answer: the cell pulses in the warmer ink, and says how to leave it
    const float k = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.62f + 0.18f * k, 0.30f + 0.2f * k, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.62f + 0.18f * k, 0.30f + 0.2f * k, 1.f));
  }
  // (the id without the label: the controller's cursor stays on the cell as its text changes)
  const bool clicked = ImGui::Button((label + "###cell").c_str(), ImVec2(width, 0.f));
  if (asking) {
    ImGui::PopStyleColor(2); // (how to leave it: beside the Controller and Keyboard choice, section())
  }
  if (clicked) {
    if (asking) {
      cancelCapture();
    } else {
      ask(c);
    }
  }
  if (!isDefault && !asking) {
    ImGui::PopStyleColor();
  }
  if (sTestFocus == c) {
    ImGui::FocusItem(); // (as a controller's moves would have put the cursor here)
    ImGui::SetNavCursorVisible(true);
    sTestFocus = {};
  }
  // clear: right-click, the controller's third button (X on an Xbox's, a GameCube's X), Delete
  if (!asking && (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
                  (ImGui::IsItemFocused() && (portMenuUiPressed(PORT_MENU_THIRD) ||
                                              ImGui::IsKeyPressed(ImGuiKey_Delete, false))))) {
    cellSet(c, Bind{});
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !asking) {
    ImGui::SetTooltip("%s", portTr("Choose, then press the key or button. Right-click, {CLEAR} or Delete: none."));
  }
  ImGui::EndDisabled();
  ImGui::PopID();
}

int sView = -1; // the table's two cells: 0 the keyboard's, 1 the controller's (-1: chosen when it opens)

void drawRow(int group, int row, float cellWidth) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  const char* name = group == kGroupGame ? kGameRows[row].name : kActions[row].name;
  ImGui::TextUnformatted(portTr(name));
  for (int i = 0; i < 2; i++) {
    ImGui::TableSetColumnIndex(i + 1);
    drawCell(Cell{group, row, (sView == 1 ? 2 : 0) + i}, cellWidth);
  }
  ImGui::TableSetColumnIndex(3);
  ImGui::PushID(group * 1000 + row);
  bool changed = false;
  for (int col = 0; col < kCols && !changed; col++) {
    const Cell c{group, row, col};
    if (!cellOff(c)) {
      changed = !(cellGet(c) == cellDefault(c));
    }
  }
  ImGui::BeginDisabled(!changed);
  if (ImGui::SmallButton(portTrLabel("Reset##row"))) {
    for (int col = 0; col < kCols; col++) {
      const Cell c{group, row, col};
      if (!cellOff(c)) {
        cellSet(c, cellDefault(c));
      }
    }
  }
  ImGui::EndDisabled();
  ImGui::PopID();
}

void pendingPopup() {
  if (sPending.open) {
    ImGui::OpenPopup("##bind_conflict");
    sPending.open = false;
  }
  // (no title bar: an empty band with nothing to say)
  sPopupShown = ImGui::BeginPopupModal("##bind_conflict", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);
  if (!sPopupShown) {
    return;
  }
  const GlyphStyle st = styleFor(sPending.cell.group == kGroupGame || sPending.other.group == kGroupGame
                                     ? static_cast<u32>(sPlayer)
                                     : 0);
  // a game row's: "the game's A button", not "A is already A."
  ImGui::Text(portTr(sPending.other.group == kGroupGame ? "%s is already the game's %s button." : "%s is already %s."),
              bindLabel(sPending.want, st).c_str(), cellRowName(sPending.other));
  if (sPending.other.group == kGroupGame && sPending.cell.group == kGroupAction) {
    ImGui::TextDisabled("%s", portTr("Kept for both, the game's button wins and the action does nothing."));
  }
  const bool canSwap = cellTakes(sPending.other, sPending.had);
  ImGui::BeginDisabled(!canSwap);
  if (ImGui::Button(portTrLabel("Swap"))) {
    cellSet(sPending.cell, sPending.want);
    cellSet(sPending.other, sPending.had);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip(portTr("%s gets %s."), cellRowName(sPending.other),
                      sPending.had.type == Bind::None ? portTr("(none)") : bindName(sPending.had, st).c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Keep both"))) {
    cellSet(sPending.cell, sPending.want);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  // Cancel, or back (a modal isn't left by Dear ImGui's own: the controller's back is Escape here)
  if (ImGui::Button(portTrLabel("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void resetAll() {
  const u32 port = static_cast<u32>(sPlayer);
  portMenuResetButtons(); // (the menus' confirm and back too: a way out of a pick that doesn't suit)
  if (haveController(port)) {
    PADRestoreDefaultMapping(port);
    PADSerializeMappings();
  }
  if (sPlayer == 0) {
    PADRestoreDefaultKeyBindings(0); // (port 0's are the defaults; a seat swap's keyboard elsewhere gets them)
    PADSetKeyboardActive(0, TRUE);
    moveKeyboard(0, keyboardPort());
    PADSerializeMappings();
    for (int i = 0; i < kGameRowCount; i++) {
      *sKey2[i] = kGameRows[i].key2Default;
    }
  }
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    for (int col = 0; col < kCols; col++) {
      cellSet(Cell{kGroupAction, a, col}, actionDefault(a, col));
    }
  }
  if (sPlayer >= 0 && sPlayer < 4) {
    *sDigital[sPlayer] = 0;
    for (int i = 0; i < kGameRowCount; i++) {
      *sPad2[i][sPlayer] = -1;
    }
  }
  portSettingsSave();
  profileTouched();
}

// the sticks and triggers of the player's controller (Aurora's mappings and dead zones)
void swapNative(PADAxisMapping* x, PADAxisMapping* y) {
  std::swap(x->nativeAxis, y->nativeAxis);
  std::swap(x->nativeButton, y->nativeButton);
}

bool stickSwapped(u32 port) {
  const PADAxisMapping* m = axisMap(port, PAD_AXIS_LEFT_X_POS);
  return m != nullptr && axisValid(*m) &&
         (m->nativeAxis.nativeAxis == SDL_GAMEPAD_AXIS_RIGHTX || m->nativeAxis.nativeAxis == SDL_GAMEPAD_AXIS_RIGHTY);
}

// a stick's X (or Y) the other way: its positive direction on what was its negative's
bool inverted(u32 port, u16 posAxis) {
  const PADAxisMapping* m = axisMap(port, posAxis);
  if (m == nullptr || !axisValid(*m)) {
    return false;
  }
  const int a = m->nativeAxis.nativeAxis;
  const bool yAxis = a == SDL_GAMEPAD_AXIS_LEFTY || a == SDL_GAMEPAD_AXIS_RIGHTY;
  const bool yRow = posAxis == PAD_AXIS_LEFT_Y_POS || posAxis == PAD_AXIS_RIGHT_Y_POS;
  (void)yRow;
  // SDL's Y is down: the GameCube's up is its negative
  return yAxis ? m->nativeAxis.sign == AXIS_SIGN_POSITIVE : m->nativeAxis.sign == AXIS_SIGN_NEGATIVE;
}

void controllerExtras() {
  const u32 port = static_cast<u32>(sPlayer);
  if (!haveController(port)) {
    return;
  }
  ImGui::SeparatorText(portTr("Sticks and triggers"));
  bool changed = false;
  bool swapped = stickSwapped(port);
  if (ImGui::Checkbox(portTrLabel("Swap the sticks"), &swapped)) {
    for (u16 i = 0; i < 4; i++) {
      PADAxisMapping* l = axisMap(port, i);
      PADAxisMapping* r = axisMap(port, static_cast<u16>(i + 4));
      if (l != nullptr && r != nullptr) {
        swapNative(l, r);
      }
    }
    changed = true;
  }
  struct Inv {
    const char* label;
    u16 pos, neg;
  };
  static const Inv kInv[] = {{"Invert the stick's X", PAD_AXIS_LEFT_X_POS, PAD_AXIS_LEFT_X_NEG},
                             {"Invert the stick's Y", PAD_AXIS_LEFT_Y_POS, PAD_AXIS_LEFT_Y_NEG},
                             {"Invert the C-stick's X", PAD_AXIS_RIGHT_X_POS, PAD_AXIS_RIGHT_X_NEG},
                             {"Invert the C-stick's Y", PAD_AXIS_RIGHT_Y_POS, PAD_AXIS_RIGHT_Y_NEG}};
  for (int i = 0; i < 4; i++) {
    bool v = inverted(port, kInv[i].pos);
    if (i % 2 == 1) {
      ImGui::SameLine();
    }
    if (ImGui::Checkbox(portTrLabel(kInv[i].label), &v)) {
      PADAxisMapping* p = axisMap(port, kInv[i].pos);
      PADAxisMapping* n = axisMap(port, kInv[i].neg);
      if (p != nullptr && n != nullptr) {
        swapNative(p, n);
        changed = true;
      }
    }
  }
  if (PADDeadZones* dz = PADGetDeadZones(port)) {
    bool use = dz->useDeadzones;
    if (ImGui::Checkbox(portTrLabel("Dead zones"), &use)) {
      dz->useDeadzones = use;
      changed = true;
    }
    ImGui::BeginDisabled(!dz->useDeadzones);
    auto pct = [&](const char* label, u16* v, int lo, int hi) {
      int p = static_cast<int>(std::lround(*v * 100.0 / 32767.0));
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
      if (ImGui::SliderInt(portTrLabel(label), &p, lo, hi, "%d%%")) {
        *v = static_cast<u16>(p * 32767 / 100);
      }
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    };
    ImGui::SameLine();
    pct("Stick", &dz->stickDeadZone, 0, 50);
    ImGui::SameLine();
    pct("C-stick", &dz->substickDeadZone, 0, 50);
    ImGui::EndDisabled();
    static const char* kModes[] = {"Analog, clicking at a point (as the GameCube's)", "Analog only",
                                   "Digital (past the point: all the way)"};
    int mode = *sDigital[port] ? 2 : dz->emulateTriggers ? 0 : 1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.f);
    if (ImGui::BeginCombo(portTrLabel("Triggers"), portTr(kModes[mode]))) {
      for (int i = 0; i < 3; i++) {
        if (ImGui::Selectable(portTr(kModes[i]), i == mode)) {
          mode = i;
          dz->emulateTriggers = mode != 1;
          *sDigital[port] = mode == 2;
          portSettingsSave();
          changed = true;
        }
      }
      ImGui::EndCombo();
    }
    if (dz->emulateTriggers) {
      pct("L clicks at", &dz->leftTriggerActivationZone, 5, 100);
      ImGui::SameLine();
      pct("R clicks at", &dz->rightTriggerActivationZone, 5, 100);
    }
  }
  if (ImGui::Button(portTrLabel("Swap A and B"))) {
    PADButtonMapping* a = buttonMap(port, PAD_BUTTON_A);
    PADButtonMapping* b = buttonMap(port, PAD_BUTTON_B);
    if (a != nullptr && b != nullptr) {
      std::swap(a->nativeButton, b->nativeButton);
      PADSetButtonMapping(port, *a);
      PADSetButtonMapping(port, *b);
      changed = true;
    }
  }
  if (changed) {
    PADSerializeMappings();
    profileTouched();
  }
}

// --- Profiles ------------------------------------------------------------------
//
// <user>/bindings/<file>.txt: a whole set of bindings under a name, as lines
// of key=value (name=, then key_<row>, key2_<row>, bind_<action>_key/_key2/
// _pad/_pad2, pad_<row>, pad2_<row>, the dead zones and the triggers). The
// keyboard's part and the actions' are everyone's; the controller's part is
// loaded onto the chosen player. Each player has an active profile, which
// their changes are saved into; a profile can be the default for a kind of
// controller (SDL's name for its type), loaded onto a player whenever one of
// that kind becomes theirs. Both are in <user>/bindings/defaults.ini.

struct Profile {
  std::string file, name;
};
std::vector<Profile> sProfiles;
std::string sActive[4];                                   // each player's profile (its file), or ""
std::vector<std::pair<std::string, std::string>> sDefaults; // a controller type, its profile's file
bool sProfilesRead;
bool sLoading; // a profile being put in: nothing saved back meanwhile

std::string profileDir() { return std::string(portSettingsUserPath()) + "/bindings"; }
std::string profilePath(const std::string& file) { return profileDir() + "/" + file + ".txt"; }
std::string defaultsPath() { return profileDir() + "/defaults.ini"; }

// the lines of a file, as key and value
std::vector<std::pair<std::string, std::string>> readPairs(const std::string& path) {
  std::vector<std::pair<std::string, std::string>> out;
  size_t size = 0;
  char* data = static_cast<char*>(SDL_LoadFile(path.c_str(), &size));
  if (data == nullptr) {
    return out;
  }
  std::string text(data, size);
  SDL_free(data);
  size_t at = 0;
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) {
      continue;
    }
    out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
  }
  return out;
}

bool writeText(const std::string& path, const std::string& text) {
  SDL_CreateDirectory(profileDir().c_str());
  return portfile::writeAll(path, text.data(), text.size());
}

// a name the menus can show (the Start menu's font: plain letters), at most 40
std::string cleanName(const char* in) {
  std::string s;
  for (const char* p = in; p != nullptr && *p != '\0' && s.size() < 40; p++) {
    const char c = *p;
    if (c >= 0x20 && c < 0x7F && std::strchr("<>\\`=", c) == nullptr) {
      s += c;
    }
  }
  while (!s.empty() && s.back() == ' ') {
    s.pop_back();
  }
  while (!s.empty() && s.front() == ' ') {
    s.erase(s.begin());
  }
  return s;
}

int profileIndex(const std::string& file) {
  for (size_t i = 0; i < sProfiles.size(); i++) {
    if (sProfiles[i].file == file) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// a file for a new profile's name: its letters, numbered when taken
std::string newFile(const std::string& name) {
  std::string base;
  for (char c : name) {
    base += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ' ? c : '_';
  }
  if (base.empty() || SDL_strcasecmp(base.c_str(), "defaults") == 0) {
    base = "profile";
  }
  std::string file = base;
  for (int n = 2; SDL_GetPathInfo(profilePath(file).c_str(), nullptr); n++) {
    file = base + " " + std::to_string(n);
  }
  return file;
}

void writeDefaults() {
  std::string text = "# Paper Cut: each player's bindings profile, and the default per kind of controller\n";
  for (int p = 0; p < 4; p++) {
    if (!sActive[p].empty()) {
      text += "active_p" + std::to_string(p + 1) + "=" + sActive[p] + "\n";
    }
  }
  for (const auto& d : sDefaults) {
    text += "type:" + d.first + "=" + d.second + "\n";
  }
  writeText(defaultsPath(), text);
}

void readProfiles() {
  sProfiles.clear();
  int n = 0;
  char** found = SDL_GlobDirectory(profileDir().c_str(), "*.txt", 0, &n);
  for (int i = 0; found != nullptr && i < n; i++) {
    std::string file = found[i];
    if (file.find('/') != std::string::npos || file.find('\\') != std::string::npos || file.size() < 5) {
      continue;
    }
    file.resize(file.size() - 4);
    Profile pr{file, file};
    for (const auto& kv : readPairs(profilePath(file))) {
      if (kv.first == "name" && !cleanName(kv.second.c_str()).empty()) {
        pr.name = cleanName(kv.second.c_str());
      }
    }
    sProfiles.push_back(pr);
  }
  SDL_free(found);
  std::sort(sProfiles.begin(), sProfiles.end(),
            [](const Profile& a, const Profile& b) { return SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
  for (std::string& a : sActive) {
    a.clear();
  }
  sDefaults.clear();
  for (const auto& kv : readPairs(defaultsPath())) {
    if (kv.first.size() == 9 && kv.first.compare(0, 8, "active_p") == 0 && kv.first[8] >= '1' && kv.first[8] <= '4' &&
        profileIndex(kv.second) >= 0) {
      sActive[kv.first[8] - '1'] = kv.second;
    } else if (kv.first.compare(0, 5, "type:") == 0 && profileIndex(kv.second) >= 0) {
      sDefaults.emplace_back(kv.first.substr(5), kv.second);
    }
  }
  sProfilesRead = true;
}

void ensureProfiles() {
  if (!sProfilesRead) {
    readProfiles();
  }
}

// the kind of controller a player has (SDL's name for its type: "xboxone", "ps5"), or ""
std::string controllerType(int port) {
  const s32 index = PADGetIndexForPort(static_cast<u32>(port));
  SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
  const char* t = gp != nullptr ? SDL_GetGamepadStringForType(SDL_GetGamepadType(gp)) : nullptr;
  return t != nullptr ? t : "";
}

// the bindings now, for a player's controller, as a profile's text
std::string profileText(const std::string& name, int port) {
  std::string t = "# Paper Cut bindings profile\nname=" + name + "\n";
  auto put = [&t](const std::string& k, int v) { t += k + "=" + std::to_string(v) + "\n"; };
  for (int i = 0; i < kGameRowCount; i++) {
    put(std::string("key_") + rowId(i), gameKey(i));
    put(std::string("key2_") + rowId(i), *sKey2[i]);
  }
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    const std::string id = std::string("bind_") + kActions[a].id;
    put(id + "_key", actionKey(a, 0));
    put(id + "_key2", actionKey(a, 1));
    put(id + "_pad", padValue(actionPad(a, 0)));
    put(id + "_pad2", *sSet[a].pad2);
  }
  if (haveController(static_cast<u32>(port))) {
    for (int i = 0; i < kGameRowCount; i++) {
      put(std::string("pad_") + rowId(i), pad2Value(gamePad(static_cast<u32>(port), i)));
    }
    if (const PADDeadZones* dz = PADGetDeadZones(static_cast<u32>(port))) {
      put("dead_zones", dz->useDeadzones ? 1 : 0);
      put("stick_dead_zone", dz->stickDeadZone);
      put("cstick_dead_zone", dz->substickDeadZone);
      put("trigger_click", dz->emulateTriggers ? 1 : 0);
      put("l_click_at", dz->leftTriggerActivationZone);
      put("r_click_at", dz->rightTriggerActivationZone);
    }
  }
  for (int i = 0; i < kGameRowCount; i++) {
    put(std::string("pad2_") + rowId(i), *sPad2[i][port]);
  }
  put("digital_triggers", *sDigital[port]);
  return t;
}

void saveProfile(const std::string& file, int port) {
  const int i = profileIndex(file);
  writeText(profilePath(file), profileText(i >= 0 ? sProfiles[i].name : file, port));
}

// a profile put in: the controller's part onto port's controller, and unless
// controllerOnly the keyboard's and the actions' too; it becomes port's
bool loadProfile(const std::string& file, int port, bool controllerOnly) {
  const auto pairs = readPairs(profilePath(file));
  if (pairs.empty()) {
    return false;
  }
  auto find = [&pairs](const std::string& k, int* out) {
    for (const auto& kv : pairs) {
      if (kv.first == k) {
        *out = std::atoi(kv.second.c_str());
        return true;
      }
    }
    return false;
  };
  sLoading = true;
  const u32 up = static_cast<u32>(port);
  int v = 0;
  if (!controllerOnly) {
    for (int i = 0; i < kGameRowCount; i++) {
      if (find(std::string("key_") + rowId(i), &v)) {
        setGameKey(i, v > 0 && v < SDL_SCANCODE_COUNT ? v : PAD_KEY_INVALID, false);
      }
      if (find(std::string("key2_") + rowId(i), &v)) {
        *sKey2[i] = v > 0 && v < SDL_SCANCODE_COUNT ? v : PAD_KEY_INVALID;
      }
    }
    for (int a = 0; a < PORT_BIND_COUNT; a++) {
      const std::string id = std::string("bind_") + kActions[a].id;
      if (find(id + "_key", &v)) {
        *sSet[a].key = std::clamp(v, 0, kKeyMax);
      }
      if (find(id + "_key2", &v)) {
        *sSet[a].key2 = std::clamp(v, 0, kKeyMax);
      }
      if (find(id + "_pad", &v)) {
        *sSet[a].pad = std::clamp(v, kPadNone, 1023);
      }
      if (find(id + "_pad2", &v)) {
        *sSet[a].pad2 = std::clamp(v, kPadNone, 1023);
      }
    }
  }
  if (haveController(up)) {
    for (int i = 0; i < kGameRowCount; i++) {
      if (find(std::string("pad_") + rowId(i), &v)) {
        const Bind b = pad2Bind(std::clamp(v, -1, 127));
        const Cell c{kGroupGame, i, 2};
        const int was = sPlayer;
        sPlayer = port;
        if (cellTakes(c, b)) {
          setGamePad(up, i, b, false);
        }
        sPlayer = was;
      }
    }
    if (PADDeadZones* dz = PADGetDeadZones(up)) {
      if (find("dead_zones", &v)) {
        dz->useDeadzones = v != 0;
      }
      if (find("stick_dead_zone", &v)) {
        dz->stickDeadZone = static_cast<u16>(std::clamp(v, 0, 32767));
      }
      if (find("cstick_dead_zone", &v)) {
        dz->substickDeadZone = static_cast<u16>(std::clamp(v, 0, 32767));
      }
      if (find("trigger_click", &v)) {
        dz->emulateTriggers = v != 0;
      }
      if (find("l_click_at", &v)) {
        dz->leftTriggerActivationZone = static_cast<u16>(std::clamp(v, 0, 32767));
      }
      if (find("r_click_at", &v)) {
        dz->rightTriggerActivationZone = static_cast<u16>(std::clamp(v, 0, 32767));
      }
    }
  }
  for (int i = 0; i < kGameRowCount; i++) {
    if (find(std::string("pad2_") + rowId(i), &v)) {
      *sPad2[i][port] = std::clamp(v, -1, 127);
    }
  }
  if (find("digital_triggers", &v)) {
    *sDigital[port] = v != 0;
  }
  PADSerializeMappings();
  portSettingsSave();
  sLoading = false;
  sActive[port] = file;
  writeDefaults();
  return true;
}

void profileTouched() {
  const int p = sPlayer & 3;
  if (!sLoading && sProfilesRead && !sActive[p].empty() && profileIndex(sActive[p]) >= 0) {
    saveProfile(sActive[p], p);
  }
}

// the bindings now under a name, for port's controller: an existing profile
// of that name is written over; it becomes port's. Its index, or -1.
int saveProfileAs(const char* rawName, int port) {
  ensureProfiles();
  const std::string name = cleanName(rawName);
  if (name.empty()) {
    return -1;
  }
  std::string file;
  for (const Profile& pr : sProfiles) {
    if (SDL_strcasecmp(pr.name.c_str(), name.c_str()) == 0) {
      file = pr.file;
    }
  }
  if (file.empty()) {
    file = newFile(name);
  }
  if (!writeText(profilePath(file), profileText(name, port))) {
    return -1;
  }
  sActive[port] = file;
  writeDefaults();
  const std::string keep[4] = {sActive[0], sActive[1], sActive[2], sActive[3]};
  readProfiles();
  for (int p = 0; p < 4; p++) {
    sActive[p] = keep[p];
  }
  return profileIndex(file);
}

bool renameProfile(int i, const char* rawName) {
  const std::string name = cleanName(rawName);
  if (i < 0 || i >= static_cast<int>(sProfiles.size()) || name.empty()) {
    return false;
  }
  const std::string old = sProfiles[i].file;
  std::string text = "name=" + name + "\n";
  for (const auto& kv : readPairs(profilePath(old))) {
    if (kv.first != "name") {
      text += kv.first + "=" + kv.second + "\n";
    }
  }
  const std::string file = newFile(name);
  if (!writeText(profilePath(file), "# Paper Cut bindings profile\n" + text)) {
    return false;
  }
  SDL_RemovePath(profilePath(old).c_str());
  for (std::string& a : sActive) {
    a = a == old ? file : a;
  }
  for (auto& d : sDefaults) {
    d.second = d.second == old ? file : d.second;
  }
  writeDefaults();
  readProfiles();
  return true;
}

bool deleteProfile(int i) {
  if (i < 0 || i >= static_cast<int>(sProfiles.size())) {
    return false;
  }
  SDL_RemovePath(profilePath(sProfiles[i].file).c_str());
  readProfiles(); // (dropping what named it)
  writeDefaults();
  return true;
}

// whether profile i is the default for port's kind of controller (-1: no controller)
int profileDefault(int i, int port) {
  const std::string t = controllerType(port);
  if (t.empty() || i < 0 || i >= static_cast<int>(sProfiles.size())) {
    return t.empty() ? -1 : 0;
  }
  for (const auto& d : sDefaults) {
    if (d.first == t && d.second == sProfiles[i].file) {
      return 1;
    }
  }
  return 0;
}

void setProfileDefault(int i, int port, bool on) {
  const std::string t = controllerType(port);
  if (t.empty() || i < 0 || i >= static_cast<int>(sProfiles.size())) {
    return;
  }
  sDefaults.erase(std::remove_if(sDefaults.begin(), sDefaults.end(), [&t](const auto& d) { return d.first == t; }),
                  sDefaults.end());
  if (on) {
    sDefaults.emplace_back(t, sProfiles[i].file);
  }
  writeDefaults();
}

// each frame: a controller that has just become a player's gets its kind's default profile
void watchControllers() {
  static SDL_JoystickID sSeen[4];
  for (int port = 0; port < 4; port++) {
    const s32 index = PADGetIndexForPort(static_cast<u32>(port));
    SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
    const SDL_JoystickID id = gp != nullptr ? SDL_GetGamepadID(gp) : 0;
    if (id == sSeen[port]) {
      continue;
    }
    sSeen[port] = id;
    if (id == 0 || !haveController(static_cast<u32>(port))) {
      continue;
    }
    ensureProfiles();
    const std::string t = controllerType(port);
    for (const auto& d : sDefaults) {
      if (d.first == t && profileIndex(d.second) >= 0) {
        loadProfile(d.second, port, true);
        break;
      }
    }
  }
}

// the page's profile row: choose, Load, Save as, Rename, Delete, the default for this kind of controller
int sPageChosen = -1;
char sNameBuf[48];
int sNameMode; // 1 save as, 2 rename
const char* sProfileSaid; // how the last Load, Save as, Rename or Delete went (English, the Start menu's words)

void profilesUi() {
  const int active = profileIndex(sActive[sPlayer & 3]);
  if (sPageChosen >= static_cast<int>(sProfiles.size()) || sPageChosen < 0) {
    sPageChosen = active;
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(portTr("Profile"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
  if (ImGui::BeginCombo("##bind_profile", sPageChosen >= 0 ? sProfiles[sPageChosen].name.c_str() : portTr("(none)"))) {
    for (int i = 0; i < static_cast<int>(sProfiles.size()); i++) {
      ImGui::PushID(i);
      if (ImGui::Selectable(sProfiles[i].name.c_str(), i == sPageChosen)) {
        sPageChosen = i;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  const bool chosen = sPageChosen >= 0;
  ImGui::SameLine();
  ImGui::BeginDisabled(!chosen);
  if (ImGui::Button(portTrLabel("Load##profile")) && chosen) {
    sProfileSaid = loadProfile(sProfiles[sPageChosen].file, sPlayer & 3, false) ? "Loaded."
                                                                                 : "That profile couldn't be read.";
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  bool openName = false;
  if (ImGui::Button(portTrLabel("Save as..."))) {
    sNameMode = 1;
    std::snprintf(sNameBuf, sizeof(sNameBuf), "%s", chosen ? sProfiles[sPageChosen].name.c_str() : "");
    openName = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!chosen);
  if (ImGui::Button(portTrLabel("Rename...")) && chosen) {
    sNameMode = 2;
    std::snprintf(sNameBuf, sizeof(sNameBuf), "%s", sProfiles[sPageChosen].name.c_str());
    openName = true;
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Delete##profile")) && chosen) {
    ImGui::OpenPopup("##bind_delete");
  }
  const int def = profileDefault(sPageChosen, sPlayer & 3);
  if (def >= 0) {
    // (on a line of its own: a long label in some languages)
    bool on = def == 1;
    if (ImGui::Checkbox(portTrLabel("Default for this kind of controller"), &on)) {
      setProfileDefault(sPageChosen, sPlayer & 3, on);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip(portTr("Loaded onto a player whenever a controller of this kind (%s) becomes theirs."),
                        controllerType(sPlayer & 3).c_str());
    }
  }
  ImGui::EndDisabled();
  if (active >= 0) {
    ImGui::TextDisabled(portTr("Changes are saved into %s."), sProfiles[active].name.c_str());
  }
  if (sProfileSaid != nullptr) {
    ImGui::TextDisabled("%s", portTr(sProfileSaid));
  }
  if (openName) {
    ImGui::OpenPopup("##bind_name");
  }
  if (ImGui::BeginPopup("##bind_name")) {
    ImGui::TextUnformatted(portTr(sNameMode == 1 ? "Save the bindings as" : "Rename the profile"));
    if (ImGui::IsWindowAppearing()) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.f);
    const bool enter = ImGui::InputText("##bind_name_text", sNameBuf, 41, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool ok = !cleanName(sNameBuf).empty();
    ImGui::BeginDisabled(!ok);
    if ((ImGui::Button(portTrLabel("OK##profile")) || enter) && ok) {
      if (sNameMode == 1) {
        sPageChosen = saveProfileAs(sNameBuf, sPlayer & 3);
        sProfileSaid = sPageChosen >= 0 ? "Saved: changes now go into this profile." : "That name can't be used.";
      } else {
        sProfileSaid = renameProfile(sPageChosen, sNameBuf) ? "Renamed." : "That name can't be used.";
        sPageChosen = -1;
        for (int i = 0; i < static_cast<int>(sProfiles.size()); i++) {
          if (sProfiles[i].name == cleanName(sNameBuf)) {
            sPageChosen = i;
          }
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Cancel##profile"))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopup("##bind_delete")) {
    if (sPageChosen >= 0) {
      ImGui::Text(portTr("Delete the profile %s?"), sProfiles[sPageChosen].name.c_str());
      if (ImGui::Button(portTrLabel("Delete##profile_yes"))) {
        sProfileSaid = deleteProfile(sPageChosen) ? "Deleted." : "That profile couldn't be deleted.";
        sPageChosen = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
    }
    if (ImGui::Button(portTrLabel("Cancel##profile_no"))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

// the Settings action works on this controller in play: a button of its own (not one of the game's, which it gives
// way to), or a combo not both the game's
bool settingsOnPad(SDL_Gamepad* gp) {
  for (int n = 0; n < 2; n++) {
    const Bind p = actionPad(PORT_BIND_SETTINGS, n);
    if (p.type == Bind::Button && SDL_GamepadHasButton(gp, SDL_GamepadButton(p.a)) && !gameButtonIs(gp, p.a)) {
      return true;
    }
    if (p.type == Bind::Combo && SDL_GamepadHasButton(gp, SDL_GamepadButton(p.a)) &&
        SDL_GamepadHasButton(gp, SDL_GamepadButton(p.b)) && !(gameButtonIs(gp, p.a) && gameButtonIs(gp, p.b))) {
      return true;
    }
  }
  return false;
}

// every controller connected has it (none connected: nothing to warn of)
bool settingsOnPad() {
  for (SDL_Gamepad* gp : gamepads()) {
    if (!settingsOnPad(gp)) {
      return false;
    }
  }
  return true;
}

bool hasView(SDL_Gamepad* gp) { return gp != nullptr && SDL_GamepadHasButton(gp, SDL_GAMEPAD_BUTTON_BACK); }

// the controller in hand has a Back button (the hint's chord)
bool inHandHasView() {
  const unsigned id = portMenuPadId();
  return id == 0 || hasView(SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(id)));
}

void settingsSafety(int pageOpen) {
  struct Hold {
    SDL_JoystickID id;
    Uint64 since; // 0: not held
    bool done;    // opened the page: not again until let go
  };
  static std::vector<Hold> sHolds;
  const std::vector<SDL_Gamepad*> pads = gamepads();
  for (SDL_Gamepad* gp : pads) {
    const SDL_JoystickID id = SDL_GetGamepadID(gp);
    Hold* h = nullptr;
    for (Hold& x : sHolds) {
      if (x.id == id) {
        h = &x;
      }
    }
    if (h == nullptr) {
      sHolds.push_back({id, 0, false});
      h = &sHolds.back();
    }
    const bool chord = held(gp, SDL_GAMEPAD_BUTTON_START) && (!hasView(gp) || held(gp, SDL_GAMEPAD_BUTTON_BACK));
    if (!chord || pageOpen || gPortLauncherShown || settingsOnPad(gp) || sCap.cell.group >= 0) {
      h->since = 0;
      h->done = h->done && chord;
      continue;
    }
    const Uint64 now = SDL_GetTicks();
    if (h->since == 0) {
      h->since = now;
    }
    if (!h->done && now - h->since >= 3000) {
      h->done = true;
      std::printf("[bindings] no controller button opens the settings page: Start%s held 3 s on %s, the page opened\n",
                  hasView(gp) ? " and Back" : "", SDL_GetGamepadName(gp) != nullptr ? SDL_GetGamepadName(gp) : "a controller");
      std::fflush(stdout);
      portSettingsToggle();
    }
  }
}

// Paper Cut's menus' confirm and back on the face buttons the player picks (port/include/port/menu_buttons.h):
// as the controller has them, or the bottom, right or left one (the top one switches this page's view); picking
// the other's button swaps the two
void menuButtonChoices() {
  static const char* const kPlaces[3] = {"{SOUTH}", "{EAST}", "{WEST}"};
  for (int role : {PORT_MENU_CONFIRM, PORT_MENU_BACK}) {
    const int chosen = portMenuChosenPlace(role);
    std::string preview = chosen >= 0 ? portTr(kPlaces[chosen]) : portTr("As the controller has it");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.f);
    if (ImGui::BeginCombo(portTrLabel(role == PORT_MENU_CONFIRM ? "Menu confirm##menu_confirm" : "Menu back##menu_back"),
                          preview.c_str())) {
      for (int place = -1; place < 3; place++) {
        std::string item = place >= 0 ? portTr(kPlaces[place]) : portTr("As the controller has it");
        item += "##place" + std::to_string(place);
        if (ImGui::Selectable(item.c_str(), chosen == place)) {
          portMenuSetPlace(role, place);
          if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr) {
            std::printf("[padbind] menu %s chosen: place %d (confirm %d, back %d)\n",
                        role == PORT_MENU_CONFIRM ? "confirm" : "back", place, portMenuRoleButton(PORT_MENU_CONFIRM, 0),
                        portMenuRoleButton(PORT_MENU_BACK, 0));
            std::fflush(stdout);
          }
        }
        if (chosen == place) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    if (role == PORT_MENU_CONFIRM) {
      ImGui::SameLine();
    }
  }
}

void section(int) {
  // (opened: also the first time, which can be the first frames of the run)
  if (sView < 0 || ImGui::GetFrameCount() - sLastDrawn > 2) {
    readProfiles(); // (opened: the folder as it is now)
    sView = portMenuPadInUse() ? 1 : 0; // (a controller in hand: its buttons)
  }
  sLastDrawn = ImGui::GetFrameCount();
  if (gPortLauncherShown) {
    pollCapture(); // (the launcher's Settings tab: no overlays' tick there)
  }
  if (sCap.cell.group >= 0 || sSettle) {
    // asking: the controller and the arrows are the answer, not the page's navigation
    ImGui::GetIO().ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
  }
  {
    const char* kPorts[] = {playerName(0), playerName(1), playerName(2), playerName(3)};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
    if (ImGui::Combo("##bind_player", &sPlayer, kPorts, 4)) {
      sCap = {};
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Reset all"))) {
    resetAll();
  }
  ImGui::SameLine();
  if (haveController(static_cast<u32>(sPlayer))) {
    if (const s32 index = PADGetIndexForPort(static_cast<u32>(sPlayer)); index >= 0) {
      ImGui::TextDisabled("%s", controllerName(static_cast<unsigned>(index)).c_str());
    }
  } else {
    ImGui::TextDisabled("%s", portTr("No controller is this player: choose one under Controllers."));
  }
  {
    bool swap = portMenuSwapped() != 0;
    if (ImGui::Checkbox(portTrLabel("Swap confirm and back"), &swap)) {
      portMenuSetSwapped(swap);
    }
    ImGui::SameLine();
    menuButtonChoices();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", portTr("Paper Cut's menus: {OK} confirms, {BACK} goes back. The game's own menus keep "
                                    "the game's buttons."));
    ImGui::PopStyleColor();
    if (!settingsOnPad()) {
      // (the way back to this page with a controller alone: settingsSafety)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.20f, 0.05f, 1.f));
      ImGui::TextWrapped("%s", portTr(inHandHasView()
                                          ? "No controller button opens this page in play now: hold {MENU} and {VIEW} "
                                            "together for 3 seconds to open it."
                                          : "No controller button opens this page in play now: hold {MENU} for 3 "
                                            "seconds to open it."));
      ImGui::PopStyleColor();
    }
  }
  profilesUi();
  // the controller's buttons or the keyboard's keys, the controller's top button switches (portBindingsEvent)
  // (marked as the tab list's chosen tab is)
  for (int v : {1, 0}) {
    const char* label = portTrLabel(v == 1 ? "Controller##view" : "Keyboard##view");
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 4.f;
    // (not the top button's own press on it: that switches already, portBindingsPageEvent)
    if (ImGui::Selectable(label, sView == v, 0, ImVec2(w, 0.f)) && sView != v &&
        !ImGui::IsKeyDown(ImGuiKey_GamepadFaceUp)) {
      if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr) {
        std::printf("[padbind] view chosen on the page: %d (nav %08x, mouse %d)\n", v, ImGui::GetCurrentContext()->NavId,
                    int(ImGui::IsMouseClicked(ImGuiMouseButton_Left)));
        std::fflush(stdout);
      }
      sView = v; // (chosen with the mouse or the cursor)
      sCap = {};
    }
    ImGui::SameLine();
  }
  // beside them: the question and the way out of it with a controller alone, while asking (in place of the
  // hint, so nothing below moves and the cell asked about stays where it was)
  ImGui::PushStyleColor(ImGuiCol_Text, sCap.cell.group >= 0 ? ImVec4(0.55f, 0.20f, 0.05f, 1.f)
                                                            : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  // (a key's: Escape, the one key that leaves it, never the game's B or Start keys a controller's {BACK} and
  // {MENU} look like to a keyboard player; those two named only with a controller in hand)
  ImGui::TextWrapped("%s", portTr(sCap.cell.group < 0 ? "{NORTH}: keyboard or controller."
                                  : sCap.cell.col >= 2 ? "Press the new button. Hold {MENU} for a second to leave it as it is."
                                  : keyQuestionPadToo()
                                      ? "Press the new key. Escape, {BACK} or {MENU} held: leave it as it is. Escape held "
                                        "for a second: Escape is the new key."
                                      : "Press the new key. Escape: leave it as it is. Escape held for a second: Escape is "
                                        "the new key."));
  ImGui::PopStyleColor();
  const float cell = ImGui::GetFontSize() * 7.5f;
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
  // rows tall enough for their buttons, clear of the header and of each other
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetFontSize() * 0.3f, ImGui::GetFontSize() * 0.25f));
  if (ImGui::BeginTable("##bindings", 4, flags)) {
    ImGui::TableSetupColumn(portTr("The game's buttons"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(portTr(sView == 1 ? "Controller" : "Keyboard"), ImGuiTableColumnFlags_WidthFixed, cell * 1.3f);
    ImGui::TableSetupColumn(portTr(sView == 1 ? "Second button" : "Second key"), ImGuiTableColumnFlags_WidthFixed, cell * 1.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int i = 0; i < kGameRowCount; i++) {
      drawRow(kGroupGame, i, -FLT_MIN);
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  if (sPlayer != 0) {
    ImGui::TextDisabled("%s", portTr("The keyboard plays as player 1."));
  }
  controllerExtras();
  ImGui::SeparatorText(portTr("Paper Cut's actions"));
  // rows tall enough for their buttons, clear of the header and of each other
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetFontSize() * 0.3f, ImGui::GetFontSize() * 0.25f));
  if (ImGui::BeginTable("##actions", 4, flags)) {
    ImGui::TableSetupColumn(portTr("Action"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(portTr(sView == 1 ? "Controller" : "Keyboard"), ImGuiTableColumnFlags_WidthFixed, cell * 1.3f);
    ImGui::TableSetupColumn(portTr(sView == 1 ? "Second button" : "Second key"), ImGuiTableColumnFlags_WidthFixed, cell * 1.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int a = 0; a < PORT_BIND_COUNT; a++) {
      drawRow(kGroupAction, a, -FLT_MIN);
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Choose a cell, then press the key or button (Escape, or {MENU} held for a second: "
                                  "leave it). An action's controller binding can be two buttons: hold the first, "
                                  "press the second. Any controller does the actions; a key or button that is also "
                                  "one of the game's does only the game's. Changed bindings are in a warmer ink."));
  ImGui::TextWrapped("%s", portTr("An action's key can have Ctrl, Shift or Alt held with it: hold them, then press "
                                  "the key (a modifier alone: press it and let go). With Ctrl + F5 and F5 both "
                                  "bound, Ctrl + F5 does only its own. A second button on a game row can be any "
                                  "button, stick or trigger direction: a trigger as A, the right stick as the D-pad."));
  ImGui::PopStyleColor();
  pendingPopup();
}

void testFrame(); // tests: $TTYD_BINDINGS_TEST (below)
void rebindTestFrame(); // tests: $TTYD_REBIND_TEST (below)

// The way back to the settings page with a controller alone. In play the page opens with the Settings action;
// its button can be cleared, put on a combo of the game's buttons or kept on one of the game's buttons (it then
// gives way to the game's). While no controller connected has a binding of it that works, Start and Back (View,
// Select, Share, Minus) held together for 3 seconds open the page; on a controller with no Back, Start alone.
void settingsSafety(int pageOpen);

// each presented frame: whether the page is open; asking ends with the page
void tick(int pageOpen) {
  settingsSafety(pageOpen);
  if (sPageOpen != (pageOpen != 0)) {
    releaseAll(); // the page took over, or gave the controls back
  }
  sPageOpen = pageOpen != 0;
  if (sCap.cell.group >= 0 && !sCap.menu && (!pageOpen || ImGui::GetFrameCount() - sLastDrawn > 2)) {
    if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr) {
      std::printf("[padbind] question dropped: page %d, last drawn %d frames ago\n", pageOpen,
                  ImGui::GetFrameCount() - sLastDrawn);
      std::fflush(stdout);
    }
    sCap = {}; // closed, or another tab
  }
  if (!pageOpen || ImGui::GetFrameCount() - sLastDrawn > 2) {
    sPopupShown = false;
  }
  pollCapture();
  watchControllers();
  testFrame();
  padTestFrame();
  rebindTestFrame();
}

// a key for the cell asked about: an action's can have modifiers (held
// first: a modifier pressed waits for the key, or for its own release).
// Escape is the way out, never the answer by a press: pressed and let go it
// leaves the question (keyCaptureUp); held for a second it is the answer
// (pollCapture), so Escape itself can still be bound. (The page's back key
// on the game's side, the game's B, is an ordinary answer: the hints name
// Escape alone.)
int keyCapture(const SDL_Event* e) {
  if (sCap.cell.group < 0 || sCap.cell.col >= 2) {
    if (sCap.cell.group >= 0 && e->key.scancode == SDL_SCANCODE_ESCAPE) {
      cancelCapture();
      return 1;
    }
    return 0;
  }
  const int sc = e->key.scancode;
  if (sc == SDL_SCANCODE_ESCAPE) {
    if (sCap.escSince == 0) {
      sCap.escSince = SDL_GetTicks();
      sCap.escMods = sCap.cell.group == kGroupAction ? modsOf(e->key.mod) : 0;
    }
  } else if (sCap.cell.group == kGroupAction && modBit(sc) != 0) {
    sCap.modKey = sc;
  } else {
    const int mods = sCap.cell.group == kGroupAction ? modsOf(e->key.mod) & ~modBit(sc) : 0;
    finish(sCap.cell, Bind{Bind::Key, sc, mods});
  }
  return 1;
}

// a modifier let go while it waited: the modifier itself (with any others still held)
int keyCaptureUp(const SDL_Event* e) {
  const int sc = e->key.scancode;
  if (sCap.cell.group >= 0 && sCap.escSince != 0) {
    if (sc == SDL_SCANCODE_ESCAPE) {
      cancelCapture(); // (let go before the second: left as it was; a second held made it the key already)
    }
    return 1; // (a modifier let go meanwhile: still Escape's question)
  }
  if (sCap.cell.group >= 0 && sCap.cell.col < 2 && sCap.modKey != 0 && modBit(sc) != 0) {
    finish(sCap.cell, Bind{Bind::Key, sc, modsOf(e->key.mod) & ~modBit(sc)});
  }
  return 1;
}

} // namespace

// every event first (settings_ui.cpp): the Bindings page shown, the controller's top button switches
// its controller's and keyboard's buttons
extern "C" void portBindingsPageEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->gbutton.button != SDL_GAMEPAD_BUTTON_NORTH ||
      ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  const bool shown = ImGui::GetFrameCount() - sLastDrawn <= 2;
  if (SDL_getenv("TTYD_PADBIND_TEST") != nullptr) {
    std::printf("[padbind] top button: page shown %d (frame %d, drawn %d), asking %d, question %d\n", int(shown),
                ImGui::GetFrameCount(), sLastDrawn, sCap.cell.group, int(sPopupShown));
    std::fflush(stdout);
  }
  if (shown && sCap.cell.group < 0 && !sPopupShown) {
    sView = sView == 1 ? 0 : 1;
  }
}

// the launcher's frames (launcher_ui.cpp): tests ($TTYD_PADBIND_TEST), whatever its tab shows
extern "C" void portBindingsLauncherFrame(void) { padTestFrame(); }

extern "C" void portBindingsSetHandler(int action, PortBindHandler handler) {
  if (action >= 0 && action < PORT_BIND_COUNT) {
    sHandlers[action] = handler;
  }
}

extern "C" int portBindingsAsking(void) { return sCap.cell.group >= 0; }

extern "C" int portBindingsCapturing(void) {
  return sCap.cell.group >= 0 || sSettle || sPopupShown;
}

extern "C" int portBindingsEvent(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  switch (e->type) {
  case SDL_EVENT_KEY_DOWN:
    if (sCap.cell.group >= 0) {
      if (!e->key.repeat) {
        return keyCapture(e);
      }
      return 1;
    }
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput) {
      return 0; // typing in a box
    }
    return keyDown(e->key.scancode, e->key.repeat, modsOf(e->key.mod));
  case SDL_EVENT_KEY_UP:
    if (sCap.cell.group >= 0) {
      keyUp(e->key.scancode);
      return keyCaptureUp(e);
    }
    return keyUp(e->key.scancode);
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (portBindingsCapturing()) {
      return 1;
    }
    return buttonDown(e->gbutton.which, e->gbutton.button);
  case SDL_EVENT_GAMEPAD_BUTTON_UP:
    if (sCap.cell.group >= 0) {
      return 1;
    }
    return buttonUp(e->gbutton.which, e->gbutton.button);
  case SDL_EVENT_GAMEPAD_REMOVED:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    releaseAll();
    return 0;
  default:
    return 0;
  }
}

namespace {
// an action's key held now: its scancode, and all of its modifiers (one with
// none: unless it is also one of the game's keys)
bool actionKeyHeld(int action) {
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  const bool typing =
      (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput) || portMenuTyping();
  for (int col = 0; col < 2 && keys != nullptr && !typing; col++) {
    const Bind k = keyValueBind(actionKey(action, col));
    if (k.type != Bind::Key || k.a >= count || !keys[k.a]) {
      continue;
    }
    if (k.b == 0 ? !gameKeyIs(k.a) : (modsOf(SDL_GetModState()) & k.b) == k.b) {
      return true;
    }
  }
  return false;
}

// an action's controller bindings held on a controller
bool actionPadHeld(int action, SDL_Gamepad* gp) {
  for (int n = 0; n < 2 && gp != nullptr; n++) {
    const Bind p = actionPad(action, n);
    if (p.type == Bind::Button && held(gp, p.a) && !gameButtonIs(gp, p.a)) {
      return true;
    }
    if (p.type == Bind::Combo && held(gp, p.a) && held(gp, p.b)) {
      return true;
    }
  }
  return false;
}
} // namespace

extern "C" int portBindingsHeld(int action) {
  if (action < 0 || action >= PORT_BIND_COUNT) {
    return 0;
  }
  if (actionKeyHeld(action)) {
    return 1;
  }
  for (SDL_Gamepad* gp : gamepads()) {
    if (actionPadHeld(action, gp)) {
      return 1;
    }
  }
  return 0;
}

extern "C" int portBindingsHeldOn(int action, int port) {
  if (action < 0 || action >= PORT_BIND_COUNT || port < 0 || port > 3) {
    return 0;
  }
  if (static_cast<u32>(port) == keyboardPort() && actionKeyHeld(action)) { // (the keyboard's player: a seat swap moves it)
    return 1;
  }
  const s32 index = PADGetIndexForPort(static_cast<u32>(port));
  SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
  return actionPadHeld(action, gp) ? 1 : 0;
}

extern "C" int portBindingsPadUsableOn(int action, int port) {
  if (action < 0 || action >= PORT_BIND_COUNT || port < 0 || port > 3) {
    return 0;
  }
  const s32 index = PADGetIndexForPort(static_cast<u32>(port));
  SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
  const Bind p = actionPad(action, 0);
  if (gp == nullptr) {
    return 0;
  }
  return p.type == Bind::Combo || (p.type == Bind::Button && !gameButtonIs(gp, p.a)) ? 1 : 0;
}

extern "C" const char* portBindingsKeyName(int action) {
  static std::string sName[PORT_BIND_COUNT];
  if (action < 0 || action >= PORT_BIND_COUNT) {
    return "";
  }
  const Bind k = keyValueBind(actionKey(action, 0));
  sName[action] = k.type == Bind::Key ? bindName(k, styleFor(0)) : "";
  return sName[action].c_str();
}

extern "C" const char* portBindingsPadName(int action) {
  static std::string sName[PORT_BIND_COUNT];
  if (action < 0 || action >= PORT_BIND_COUNT) {
    return "";
  }
  sName[action] = bindName(actionPad(action), styleFor(0));
  return sName[action].c_str();
}

extern "C" int portBindingsWhat(int action, int row, int pad, int* a, int* b) {
  Bind k;
  if (action >= 0 && action < PORT_BIND_COUNT) {
    k = pad ? actionPad(action) : keyValueBind(actionKey(action, 0));
  } else if (action < 0 && row >= 0 && row < kGameRowCount) {
    k = pad ? gamePad(0, row) : keyBind(gameKey(row));
  }
  *a = k.a;
  *b = k.b;
  switch (k.type) {
  case Bind::Key: return 1;
  case Bind::Button: return 2;
  case Bind::Combo: return 3;
  case Bind::Axis: return 4;
  default: return 0;
  }
}

// --- The Start menu's page (port/src/aurora/pause_bindings_aurora.c): player 1's -----

namespace {
// a menu row and column as a cell (the columns as the page's: 0 key, 1 second key, 2 controller, 3 second button)
Cell menuCell(int row, int col) {
  col = std::clamp(col, 0, kCols - 1);
  return row < kGameRowCount ? Cell{kGroupGame, row, col} : Cell{kGroupAction, row - kGameRowCount, col};
}
bool menuRowOk(int row) { return row >= 0 && row < kGameRowCount + PORT_BIND_COUNT; }
} // namespace

extern "C" int portBindingsGameRows(void) { return kGameRowCount; }

extern "C" int portBindingsGameKeyHeld(int row) {
  if (row < 0 || row >= kGameRowCount || ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().WantCaptureKeyboard) {
    return 0;
  }
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  const s32 k1 = gameKey(row);
  const int k2 = *sKey2[row];
  return (k1 > 0 && k1 < count && keys[k1]) || (k2 > 0 && k2 < count && keys[k2]) ? 1 : 0;
}

extern "C" void portBindingsClear(int row, int col) {
  if (!menuRowOk(row)) {
    return;
  }
  sPlayer = 0;
  const Cell c = menuCell(row, col);
  if (!cellOff(c)) {
    cellSet(c, Bind{});
  }
}
extern "C" int portBindingsRowCount(void) { return kGameRowCount + PORT_BIND_COUNT; }

extern "C" const char* portBindingsRowName(int row) {
  return !menuRowOk(row) ? "" : row < kGameRowCount ? kGameRows[row].name : kActions[row - kGameRowCount].name;
}

extern "C" const char* portBindingsCellText(int row, int col) {
  static std::string sText;
  if (!menuRowOk(row)) {
    return "";
  }
  const Cell c = menuCell(row, col);
  if (sCap.cell == c && sCap.menu) {
    return portTrGame(c.col >= 2 ? (c.group == kGroupAction ? "Press a button (or two)..." : "Press a button...")
                      : sCap.modKey != 0 ? "Now the key (or let go)..."
                      : c.group == kGroupAction ? "Press a key (or a combination)..."
                                                : "Press a key...");
  }
  if (c.group == kGroupGame && c.col >= 2 && !haveController(0)) {
    return portTrGame("No controller");
  }
  sPlayer = 0;
  const Bind b = cellGet(c);
  // in the game's encoding: the names translated for its font (portTrGame), a key's as SDL has it
  const GlyphStyle st = styleFor(0);
  switch (b.type) {
  case Bind::Key: sText = bindName(b, st, true); break;
  case Bind::Button:
    // (its icon too, drawn by the game's font from the token: button_markup.h)
    sText = std::string(iconToken(b) != nullptr ? iconToken(b) : "") + (iconToken(b) != nullptr ? " " : "") +
            portTrGame(nativeButtonLabel(st, static_cast<u32>(b.a), 0));
    break;
  case Bind::Combo:
    sText = std::string(portTrGame(nativeButtonLabel(st, static_cast<u32>(b.a), 0))) + " + " +
            portTrGame(nativeButtonLabel(st, static_cast<u32>(b.b), 0));
    break;
  case Bind::Axis: sText = portTrGame(axisName(st, b.a, b.b)); break;
  default: sText = portTrGame("(none)"); break;
  }
  return sText.c_str();
}

// The player menu's Your controls (port/src/common/player_menu_pc.c): a game row's binding on one player's
// controller (port 0-3), read-only, in the game's encoding; port 0 without a controller: its key
extern "C" const char* portBindingsPlayerText(int port, int row) {
  static std::string sText;
  if (row < 0 || row >= kGameRowCount || port < 0 || port > 3) {
    return "";
  }
  if (!haveController(static_cast<u32>(port))) {
    return port == 0 ? portBindingsCellText(row, 0) : portTrGame("No controller");
  }
  const int was = sPlayer;
  sPlayer = port;
  const Bind b = cellGet(Cell{kGroupGame, row, 2});
  sPlayer = was;
  const GlyphStyle st = styleFor(static_cast<u32>(port));
  switch (b.type) {
  case Bind::Button:
    sText = std::string(iconToken(b) != nullptr ? iconToken(b) : "") + (iconToken(b) != nullptr ? " " : "") +
            portTrGame(nativeButtonLabel(st, static_cast<u32>(b.a), 0));
    break;
  case Bind::Combo:
    sText = std::string(portTrGame(nativeButtonLabel(st, static_cast<u32>(b.a), 0))) + " + " +
            portTrGame(nativeButtonLabel(st, static_cast<u32>(b.b), 0));
    break;
  case Bind::Axis: sText = portTrGame(axisName(st, b.a, b.b)); break;
  default: sText = portTrGame("(none)"); break;
  }
  return sText.c_str();
}

bool sAskSecond; // the menu's question was the second screen's (port/companion_menu.h): the game plays on meanwhile

extern "C" int portBindingsAskNeedsPad(int row, int col) {
  if (!menuRowOk(row)) {
    return 0;
  }
  const Cell c = menuCell(row, col);
  return c.group == kGroupGame && c.col >= 2 && !haveController(0) ? 1 : 0;
}

extern "C" void portBindingsAsk(int row, int col) {
  if (!menuRowOk(row) || portBindingsAskNeedsPad(row, col)) {
    return;
  }
  const Cell c = menuCell(row, col);
  sPlayer = 0;
  ask(c);
  sCap.menu = true;
  sAskSecond = gPortCompanionMenu != 0;
}

extern "C" int portBindingsAskedFromSecondScreen(void) { return sAskSecond && (sCap.cell.group >= 0 || sSettle); }

extern "C" void portBindingsCancel(void) {
  if (sCap.cell.group >= 0) {
    cancelCapture();
  }
}

extern "C" int portBindingsKeyQuestionPadToo(void) { return keyQuestionPadToo() ? 1 : 0; }

extern "C" int portBindingsBusy(void) {
  if (!sPageOpen) {
    pollCapture(); // (the overlays' tick does it too; twice a frame is harmless)
  }
  return sCap.cell.group >= 0 || sSettle;
}

extern "C" const char* portBindingsConflict(int row, int col) {
  if (!menuRowOk(row)) {
    return nullptr;
  }
  sPlayer = 0;
  const Cell c = menuCell(row, col);
  const Cell other = conflictOf(c, cellGet(c));
  return other.group < 0 ? nullptr
         : other.group == kGroupGame ? kGameRows[other.row].name
                                     : kActions[other.row].name;
}

extern "C" void portBindingsResetRow(int row) {
  if (!menuRowOk(row)) {
    return;
  }
  sPlayer = 0;
  for (int col = 0; col < kCols; col++) {
    const Cell c = menuCell(row, col);
    if (!cellOff(c)) {
      cellSet(c, cellDefault(c));
    }
  }
}

extern "C" void portBindingsResetAll(void) {
  sPlayer = 0;
  resetAll();
}

// --- Profiles, for the Start menu's page (and any other) ---------------------------

extern "C" int portBindingsProfileCount(void) {
  ensureProfiles();
  return static_cast<int>(sProfiles.size());
}

extern "C" const char* portBindingsProfileName(int i) {
  ensureProfiles();
  return i >= 0 && i < static_cast<int>(sProfiles.size()) ? sProfiles[i].name.c_str() : "";
}

extern "C" int portBindingsProfileActive(int player) {
  ensureProfiles();
  return profileIndex(sActive[player & 3]);
}

extern "C" int portBindingsProfileLoad(int i, int player) {
  ensureProfiles();
  return i >= 0 && i < static_cast<int>(sProfiles.size()) && loadProfile(sProfiles[i].file, player & 3, false) ? 1 : 0;
}

extern "C" int portBindingsProfileSaveAs(const char* name, int player) { return saveProfileAs(name, player & 3); }

extern "C" int portBindingsProfileRename(int i, const char* name) {
  ensureProfiles();
  if (!renameProfile(i, name)) {
    return -1;
  }
  const std::string want = cleanName(name);
  for (int k = 0; k < static_cast<int>(sProfiles.size()); k++) {
    if (sProfiles[k].name == want) {
      return k;
    }
  }
  return -1;
}

extern "C" int portBindingsProfileDelete(int i) {
  ensureProfiles();
  return deleteProfile(i) ? 1 : 0;
}

extern "C" int portBindingsProfileIsDefault(int i, int player) {
  ensureProfiles();
  return profileDefault(i, player & 3);
}

extern "C" void portBindingsProfileSetDefault(int i, int player, int on) {
  ensureProfiles();
  setProfileDefault(i, player & 3, on != 0);
}

extern "C" void portBindingsProfilesRefresh(void) { readProfiles(); }

// --- The keyboard's part of the game's controls -------------------------------

// Before PADInit (main_aurora.c): the default keys (kGameRows; the C-stick,
// the D-pad and the analog triggers unbound). Aurora has none, so the
// keyboard did nothing.
extern "C" void portKeyboardDefaults(void) {
  PADDefaultKeyBindings d;
  const u16 buttons[PAD_BUTTON_COUNT] = {PAD_BUTTON_A,    PAD_BUTTON_B,    PAD_BUTTON_X,    PAD_BUTTON_Y,
                                         PAD_BUTTON_START, PAD_TRIGGER_Z,   PAD_TRIGGER_L,   PAD_TRIGGER_R,
                                         PAD_BUTTON_UP,   PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT};
  for (int i = 0; i < PAD_BUTTON_COUNT; i++) {
    d.buttons[i] = PADKeyButtonBinding{PAD_KEY_INVALID, buttons[i]};
  }
  for (int i = 0; i < PAD_AXIS_COUNT; i++) {
    d.axes[i] = PADKeyAxisBinding{PAD_KEY_INVALID, static_cast<PADAxis>(i), 100};
  }
  for (const GameRow& row : kGameRows) {
    const s32 key = row.key != SDL_SCANCODE_UNKNOWN ? row.key : PAD_KEY_INVALID;
    if (row.axis) {
      d.axes[row.target].scancode = key;
    } else {
      for (PADKeyButtonBinding& b : d.buttons) {
        if (b.padButton == row.target) {
          b.scancode = key;
        }
      }
    }
  }
  PADSetDefaultKeyBindings(0, &d);
}

// After PADInit (DEMOPadInit): the keyboard on with the defaults, unless the
// player's own saved bindings already turned it on
extern "C" void portKeyboardActivate(void) {
  u32 count = 0;
  for (u32 port = 1; port < PAD_CHANMAX; port++) {
    // (a seat swap's keyboard in another port, saved with the controllers': a new start is player 1's again)
    if (PADGetKeyButtonBindings(port, &count) != nullptr) {
      if (PADGetKeyButtonBindings(0, &count) == nullptr) {
        moveKeyboard(port, 0);
      } else {
        PADClearKeyBindings(port);
      }
    }
  }
  if (PADGetKeyButtonBindings(0, &count) == nullptr) {
    PADSetKeyboardActive(0, TRUE);
  }
}

// After each PADRead (DEMOPadRead):
//  - the second keys held (when nothing else has the keyboard: the settings
//    page, a text box): their buttons pressed, and a stick not already pushed
//    pushed their way;
//  - each player's second controller buttons held: their buttons pressed (L
//    and R analog from a trigger or a stick), a stick not already pushed
//    pushed their way (as far as a stick or trigger is);
//  - a combo of Paper Cut's held on a controller: its buttons not the game's;
//  - a player's triggers set digital: pressed past the click point, all the way.
namespace {
void clampStick(s8* x, s8* y, float dx, float dy) {
  if ((dx == 0.f && dy == 0.f) || std::abs(*x) > 16 || std::abs(*y) > 16) {
    return;
  }
  const float len = std::sqrt(dx * dx + dy * dy);
  const float k = len > 1.f ? 1.f / len : 1.f; // (no faster diagonally)
  *x = static_cast<s8>(std::lround(dx * k * 100.f));
  *y = static_cast<s8>(std::lround(dy * k * 100.f));
}

void applySecondButtons(u32 port, SDL_Gamepad* gp, PADStatus& st) {
  float sx = 0.f, sy = 0.f, cx = 0.f, cy = 0.f;
  for (int i = 0; i < kGameRowCount; i++) {
    const Bind b = pad2Bind(*sPad2[i][port]);
    float v = 0.f;
    if (b.type == Bind::Button) {
      v = held(gp, b.a) ? 1.f : 0.f;
    } else if (b.type == Bind::Axis && b.a >= 0 && b.a < SDL_GAMEPAD_AXIS_COUNT) {
      v = SDL_GetGamepadAxis(gp, SDL_GamepadAxis(b.a)) * static_cast<float>(b.b) / 32767.f;
      v = v < 0.25f ? 0.f : std::min(v, 1.f); // (a dead zone of its own)
    }
    if (v <= 0.f) {
      continue;
    }
    const GameRow& r = kGameRows[i];
    if (!r.axis) {
      if (v >= 0.5f) {
        st.button |= r.target;
      }
      const u8 t = static_cast<u8>(std::lround(v * 255.f));
      if (r.target == PAD_TRIGGER_L) {
        st.triggerLeft = std::max(st.triggerLeft, t);
      } else if (r.target == PAD_TRIGGER_R) {
        st.triggerRight = std::max(st.triggerRight, t);
      }
      continue;
    }
    switch (r.target) {
    case PAD_AXIS_LEFT_X_POS: sx += v; break;
    case PAD_AXIS_LEFT_X_NEG: sx -= v; break;
    case PAD_AXIS_LEFT_Y_POS: sy += v; break;
    case PAD_AXIS_LEFT_Y_NEG: sy -= v; break;
    case PAD_AXIS_RIGHT_X_POS: cx += v; break;
    case PAD_AXIS_RIGHT_X_NEG: cx -= v; break;
    case PAD_AXIS_RIGHT_Y_POS: cy += v; break;
    case PAD_AXIS_RIGHT_Y_NEG: cy -= v; break;
    default: break;
    }
  }
  clampStick(&st.stickX, &st.stickY, sx, sy);
  clampStick(&st.substickX, &st.substickY, cx, cy);
}
} // namespace

extern "C" void portKeyboardApplyPad(PADStatus* pad) {
  // second buttons, then combos' buttons
  for (u32 port = 0; port < 4; port++) {
    const s32 index = PADGetIndexForPort(port);
    SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
    if (gp == nullptr) {
      continue;
    }
    if (pad[port].err == PAD_ERR_NONE) {
      applySecondButtons(port, gp, pad[port]);
    }
    for (int a = 0; a < PORT_BIND_COUNT; a++) {
      for (int n = 0; n < 2; n++) {
        const Bind p = actionPad(a, n);
        if (p.type != Bind::Combo || !held(gp, p.a) || !held(gp, p.b)) {
          continue;
        }
        u32 count = 0;
        const PADButtonMapping* maps = PADGetButtonMappings(port, &count);
        for (u32 i = 0; maps != nullptr && i < count; i++) {
          if (maps[i].nativeButton == static_cast<u32>(p.a) || maps[i].nativeButton == static_cast<u32>(p.b)) {
            pad[port].button &= static_cast<u16>(~maps[i].padButton);
            if (maps[i].padButton == PAD_TRIGGER_L) {
              pad[port].triggerLeft = 0;
            } else if (maps[i].padButton == PAD_TRIGGER_R) {
              pad[port].triggerRight = 0;
            }
          }
        }
        for (int i = 0; i < kGameRowCount; i++) { // (a second button that is one of the combo's)
          const Bind b = pad2Bind(*sPad2[i][port]);
          if (b.type == Bind::Button && (b.a == p.a || b.a == p.b) && !kGameRows[i].axis) {
            pad[port].button &= static_cast<u16>(~kGameRows[i].target);
          }
        }
      }
    }
  }
  for (u32 port = 0; port < 4; port++) {
    if (*sDigital[port] && pad[port].err == PAD_ERR_NONE) {
      pad[port].triggerLeft = (pad[port].button & PAD_TRIGGER_L) ? 255 : 0;
      pad[port].triggerRight = (pad[port].button & PAD_TRIGGER_R) ? 255 : 0;
    }
  }
  if (sPageOpen || gPortLauncherShown || ImGui::GetCurrentContext() == nullptr || ImGui::GetIO().WantCaptureKeyboard) {
    return;
  }
  const u32 kb = keyboardPort(); // (the keyboard's player: port 1 until a seat swap moves them)
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  auto down = [&](int row) { return *sKey2[row] > 0 && *sKey2[row] < count && keys[*sKey2[row]]; };
  int sx = 0, sy = 0, cx = 0, cy = 0;
  bool any = false;
  for (int i = 0; i < kGameRowCount; i++) {
    if (!down(i)) {
      continue;
    }
    any = true;
    const GameRow& r = kGameRows[i];
    if (!r.axis) {
      pad[kb].button |= r.target;
      continue;
    }
    switch (r.target) {
    case PAD_AXIS_LEFT_X_POS: sx++; break;
    case PAD_AXIS_LEFT_X_NEG: sx--; break;
    case PAD_AXIS_LEFT_Y_POS: sy++; break;
    case PAD_AXIS_LEFT_Y_NEG: sy--; break;
    case PAD_AXIS_RIGHT_X_POS: cx++; break;
    case PAD_AXIS_RIGHT_X_NEG: cx--; break;
    case PAD_AXIS_RIGHT_Y_POS: cy++; break;
    case PAD_AXIS_RIGHT_Y_NEG: cy--; break;
    default: break;
    }
  }
  if (!any) {
    return;
  }
  pad[kb].err = PAD_ERR_NONE;
  auto push = [](s8* x, s8* y, int dx, int dy) {
    if ((dx == 0 && dy == 0) || std::abs(*x) > 16 || std::abs(*y) > 16) {
      return;
    }
    const float k = dx != 0 && dy != 0 ? 0.7071f : 1.f; // the same speed diagonally
    *x = static_cast<s8>(dx * 100 * k);
    *y = static_cast<s8>(dy * 100 * k);
  };
  push(&pad[kb].stickX, &pad[kb].stickY, sx, sy);
  push(&pad[kb].substickX, &pad[kb].substickY, cx, cy);
}

namespace {

// Tests: $TTYD_BINDINGS_TEST=1 (presented frames from the start): a virtual
// controller plugged in as player 1, then: Back + R set for Save state; Back
// held and R pressed (the game must not see R; "[bindings] save state" from
// the action); Back alone (Settings on its release); the right stick (fast-
// forward held); a remap of A to the north button through the page's own
// path; the stick swapped and the stick's Y inverted. Each step prints what
// the game reads ("[bindings] <step>: buttons ... stick ...").
void testFrame() {
  static const char* spec = SDL_getenv("TTYD_BINDINGS_TEST");
  static int frame;
  static SDL_Joystick* joy;
  if (spec == nullptr) {
    return;
  }
  frame++;
  auto btn = [&](int b, bool down) { SDL_SetJoystickVirtualButton(joy, b, down); };
  if (frame == 60) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.name = "Paper Cut test pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    joy = id != 0 ? SDL_OpenJoystick(id) : nullptr;
    if (joy != nullptr) {
      SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
      SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    }
    std::printf("[bindings] test: virtual controller %s\n", joy != nullptr ? "attached" : SDL_GetError());
    std::fflush(stdout);
    return;
  }
  if (joy == nullptr) {
    return;
  }
  // the steps, 10 frames apart
  const int k = (frame - 120) / 10, at = (frame - 120) % 10;
  if (frame < 120 || k > 12) {
    return;
  }
  auto read = [](const char* step) {
    u32 count = 0;
    PADStatus st[PAD_CHANMAX];
    PADRead(st);
    // (what DEMOPadRead would hand the game, the combos' buttons taken out)
    const s32 index = PADGetIndexForPort(0);
    SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
    for (int a = 0; a < PORT_BIND_COUNT && gp != nullptr; a++) {
      const Bind p = actionPad(a);
      if (p.type == Bind::Combo && held(gp, p.a) && held(gp, p.b)) {
        const PADButtonMapping* maps = PADGetButtonMappings(0, &count);
        for (u32 i = 0; maps != nullptr && i < count; i++) {
          if (maps[i].nativeButton == static_cast<u32>(p.a) || maps[i].nativeButton == static_cast<u32>(p.b)) {
            st[0].button &= static_cast<u16>(~maps[i].padButton);
          }
        }
      }
    }
    std::printf("[bindings] %-30s buttons %04x stick %d,%d c-stick %d,%d ff %d page %d\n", step, st[0].button,
                st[0].stickX, st[0].stickY, st[0].substickX, st[0].substickY, portSpeedActive(), int(sPageOpen));
    std::fflush(stdout);
  };
  switch (k) {
  case 0:
    if (at == 0) {
      // player 1 is the test pad (not a real controller plugged in): found by its SDL gamepad
      SDL_Gamepad* mine = SDL_GetGamepadFromID(SDL_GetJoystickID(joy));
      if (mine == nullptr) {
        mine = SDL_OpenGamepad(SDL_GetJoystickID(joy));
      }
      for (u32 i = 0; i < PADCount(); i++) {
        const char* name = PADGetNameForControllerIndex(i);
        const bool it = PADGetSDLGamepadForIndex(i) == mine || (name != nullptr && std::strcmp(name, "Paper Cut test pad") == 0);
        std::printf("[bindings] test: controller %u \"%s\"%s\n", i, name != nullptr ? name : "?", it ? " (the test pad)" : "");
        if (it) {
          PADSetPortForIndex(i, 0);
        }
      }
      std::printf("[bindings] test: player 1 is index %d, gamepad %s\n", int(PADGetIndexForPort(0)),
                  mine != nullptr ? "open" : SDL_GetError());
      *sSet[PORT_BIND_SAVE_STATE].pad = combo(SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
      std::printf("[bindings] save state: %s\n", portBindingsPadName(PORT_BIND_SAVE_STATE));
    }
    break;
  case 1: // Back held, then R (the game's R): the combo, not the game's
    if (at == 0) {
      btn(SDL_GAMEPAD_BUTTON_BACK, true);
    } else if (at == 2) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    } else if (at == 5) {
      read("Back + R held");
    } else if (at == 7) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
      btn(SDL_GAMEPAD_BUTTON_BACK, false);
    }
    break;
  case 2:
    if (at == 5) {
      read("after the combo (page shut)");
    }
    break;
  case 3: // R alone: the game's
    if (at == 0) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    } else if (at == 5) {
      read("R alone");
    } else if (at == 7) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    }
    break;
  case 4: // Back alone: Settings, on its release
    if (at == 0) {
      btn(SDL_GAMEPAD_BUTTON_BACK, true);
    } else if (at == 3) {
      read("Back held (page still shut)");
    } else if (at == 4) {
      btn(SDL_GAMEPAD_BUTTON_BACK, false);
    } else if (at == 8) {
      read("Back let go (page open)");
    }
    break;
  case 5: // close it again
    if (at == 0) {
      btn(SDL_GAMEPAD_BUTTON_BACK, true);
    } else if (at == 2) {
      btn(SDL_GAMEPAD_BUTTON_BACK, false);
    } else if (at == 8) {
      read("Back again (page shut)");
    }
    break;
  case 6: // the right stick: fast-forward while held
    if (at == 0) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    } else if (at == 5) {
      portSpeedFrame();
      read("right stick held");
    } else if (at == 6) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    } else if (at == 9) {
      portSpeedFrame();
      read("right stick let go");
    }
    break;
  case 7: // A remapped to the north button, through the page's own path
    if (at == 0) {
      const int was = sPlayer;
      sPlayer = 0;
      const Cell c{kGroupGame, 0, 2};
      const Bind b{Bind::Button, SDL_GAMEPAD_BUTTON_NORTH, 0};
      const Cell other = conflictOf(c, b);
      std::printf("[bindings] A -> north: conflict with %s\n", other.group < 0 ? "nothing" : cellRowName(other));
      const Bind had = cellGet(c);
      cellSet(c, b);
      if (other.group >= 0) {
        cellSet(other, had); // Swap
      }
      sPlayer = was;
    } else if (at == 1) {
      btn(SDL_GAMEPAD_BUTTON_NORTH, true);
    } else if (at == 5) {
      read("north (A after the swap)");
    } else if (at == 6) {
      btn(SDL_GAMEPAD_BUTTON_NORTH, false);
      btn(SDL_GAMEPAD_BUTTON_SOUTH, true);
    } else if (at == 9) {
      read("south (Y after the swap)");
      btn(SDL_GAMEPAD_BUTTON_SOUTH, false);
    }
    break;
  case 8: // the sticks swapped, the stick's Y inverted
    if (at == 0) {
      for (u16 i = 0; i < 4; i++) {
        PADAxisMapping* l = axisMap(0, i);
        PADAxisMapping* r = axisMap(0, static_cast<u16>(i + 4));
        if (l != nullptr && r != nullptr) {
          swapNative(l, r);
        }
      }
      swapNative(axisMap(0, PAD_AXIS_LEFT_Y_POS), axisMap(0, PAD_AXIS_LEFT_Y_NEG));
      std::printf("[bindings] swapped %d, Y inverted %d\n", int(stickSwapped(0)), int(inverted(0, PAD_AXIS_LEFT_Y_POS)));
    } else if (at == 1) {
      SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHTY, SDL_JOYSTICK_AXIS_MIN); // the right stick up
    } else if (at == 5) {
      read("right stick up (swapped, inverted)");
    } else if (at == 6) {
      SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHTY, 0);
    }
    break;
  case 9: // the photo combo, both sticks in, from the defaults
    if (at == 0) {
      btn(SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
    } else if (at == 1) {
      btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    } else if (at == 4) {
      portSpeedFrame();
      read("both sticks in (photo, not ff)");
    } else if (at == 5) {
      btn(SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
      btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    }
    break;
  case 10: // everything back as it was (only ever in a test user dir)
    if (at == 0) {
      PADRestoreDefaultMapping(0);
      *sSet[PORT_BIND_SAVE_STATE].pad = kPadNone;
      std::printf("[bindings] test: done\n");
      std::fflush(stdout);
    }
    break;
  default:
    break;
  }
}

// Tests: $TTYD_PADBIND_TEST=xbox, ps, switch, gc or deck (<kind>:attach: only plugged in as player 1), with the settings page open on Controls
// (TTYD_SETTINGS_OPEN=1 TTYD_SETTINGS_TAB=Controls) or the launcher's Settings tab
// (TTYD_LAUNCHER_TAB=Settings TTYD_SETTINGS_TAB=Controls): a virtual controller of that kind as
// player 1, then with it alone, through the page's cursor and the controller's own events
// ("[padbind] ..." lines): its confirm, back and third buttons and the Start menu's mapping;
// B's controller button chosen with confirm and given the confirm button itself (A's: Swap, in
// the question); B's button cleared with the third; Start held for a second leaving a question;
// Start as an answer (the question then left with back); a trigger and a stick direction as
// second buttons (Keep both); back out of the page. Everything put back after (a test user dir).
void padTestFrame() {
  static const char* kind = SDL_getenv("TTYD_PADBIND_TEST");
  if (kind == nullptr) {
    return;
  }
  static int step, frames;
  static Uint64 next;
  static SDL_Joystick* joy;
  static int ok, back, third, sAnswer;
  static ImGuiID sStickFrom, sStickTo; // (xbox:stick)
  static int sStickMoves;
  static Uint64 sStickHeldSince;
  // (where the cursor is: the ids, and its window's scroll)
  auto stickWhere = [] {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    char b[160];
    std::snprintf(b, sizeof(b), " (from %08x to %08x, now %08x; scroll %.0f of %.0f)", sStickFrom, sStickTo, g.NavId,
                  g.NavWindow != nullptr ? g.NavWindow->Scroll.y : -1.f,
                  g.NavWindow != nullptr ? g.NavWindow->ScrollMax.y : -1.f);
    return std::string(b);
  };
  // (in the game: once $TTYD_MAP's warp is done, as a map's loading doesn't read the controller)
  if (++frames < 30 || SDL_GetTicks() < next || (!gPortLauncherShown && !portWarpSettled()) ||
      (gPortLauncherShown && portLauncherTestAddPending())) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  auto after = [&](Uint64 ms) {
    next = now + ms;
    step++;
  };
  auto btn = [&](int b, bool down) {
    if (!SDL_SetJoystickVirtualButton(joy, b, down)) {
      std::printf("[padbind] press of %d lost: %s (connected %d)\n", b, SDL_GetError(), int(SDL_JoystickConnected(joy)));
      std::fflush(stdout);
    }
  };
  auto name = [](const Bind& b) { return b.type == Bind::None ? std::string("(none)") : bindName(b, styleFor(0)); };
  auto say = [](const std::string& s) {
    std::printf("[padbind] %s\n", s.c_str());
    std::fflush(stdout);
  };
  const Cell aPad{kGroupGame, 0, 2}, aSecond{kGroupGame, 0, 3}, bPad{kGroupGame, 1, 2}, bSecond{kGroupGame, 1, 3};
  // the question's default button: Swap when it can, else Keep both; dpad right to Keep both
  auto keepBoth = [&](int at) {
    const bool canSwap = cellTakes(sPending.other, sPending.had);
    if (at == 0 && canSwap) {
      btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
    } else if (at == 1 && canSwap) {
      btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false);
    } else if (at == 2) {
      btn(ok, true);
    } else if (at == 3) {
      btn(ok, false);
    }
  };
  // the question for a cell up (the launcher's frames can lag behind the presses): a few tries
  auto waitAsking = [&](const Cell& c) {
    static int sTries;
    if (!(sCap.cell == c) && ++sTries < 40) {
      next = now + 100;
      return true;
    }
    sTries = 0;
    return false;
  };
  switch (step) {
  case 0: {
    struct Kind {
      const char* name;
      Uint16 vendor, product;
    };
    static const Kind kKinds[] = {{"xbox", 0x045E, 0x02EA}, {"ps", 0x054C, 0x0CE6}, {"switch", 0x057E, 0x2009},
                                  {"gc", 0x057E, 0x0337},   {"deck", 0x28DE, 0x1205}};
    Kind k = kKinds[0];
    for (const Kind& c : kKinds) {
      if (std::strncmp(kind, c.name, std::strlen(c.name)) == 0) {
        k = c;
      }
    }
    // (an offscreen run's window never has the focus: SDL drops a controller's presses without it)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = k.vendor;
    desc.product_id = k.product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.name = "Paper Cut test pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    joy = id != 0 ? SDL_OpenJoystick(id) : nullptr;
    if (joy == nullptr) {
      say(std::string("no virtual controller: ") + SDL_GetError());
      kind = nullptr;
      return;
    }
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    after(600);
    break;
  }
  case 1: {
    const SDL_JoystickID id = SDL_GetJoystickID(joy);
    SDL_Gamepad* mine = SDL_GetGamepadFromID(id) != nullptr ? SDL_GetGamepadFromID(id) : SDL_OpenGamepad(id);
    for (u32 i = 0; i < PADCount(); i++) {
      if (PADGetSDLGamepadForIndex(i) == mine) {
        PADSetPortForIndex(i, 0);
      }
    }
    sPlayer = 0;
    ok = portMenuRoleButton(PORT_MENU_CONFIRM, id);
    back = portMenuRoleButton(PORT_MENU_BACK, id);
    third = portMenuRoleButton(PORT_MENU_THIRD, id);
    const GlyphStyle st = styleFor(0);
    char line[512];
    std::snprintf(line, sizeof(line), "%s: SDL type %d, player 1 %s; confirm %s, back %s, third %s", kind,
                  int(SDL_GetGamepadType(mine)), PADGetIndexForPort(0) >= 0 ? "yes" : "no",
                  nativeButtonLabel(st, u32(ok), 0), nativeButtonLabel(st, u32(back), 0), nativeButtonLabel(st, u32(third), 0));
    say(line);
    std::snprintf(line, sizeof(line), "Start menu pages: the game's A->%04x B->%04x X->%04x Y->%04x; help \"%s\"",
                  portMenuButtonsAsOwn(0, PAD_BUTTON_A), portMenuButtonsAsOwn(0, PAD_BUTTON_B), portMenuButtonsAsOwn(0, PAD_BUTTON_X),
                  portMenuButtonsAsOwn(0, PAD_BUTTON_Y), portMenuText("{A}: change. {B}: back. {X}: reset."));
    say(line);
    say("before: A " + name(cellGet(aPad)) + ", B " + name(cellGet(bPad)));
    if (std::strstr(kind, ":attach") != nullptr) {
      say("attached only"); // (xbox:attach: the controller plugged in for another test, the menu crawl)
      kind = nullptr;
      return;
    }
    if (std::strstr(kind, ":stick") != nullptr) {
      step = 100; // (xbox:stick: the cursor moved with the left stick alone, below)
      next = now + 300;
      break;
    }
    if (std::strstr(kind, ":menu") != nullptr) {
      step = 200; // (xbox:menu: the menus' own confirm and back, Reset all, the way back to the page, below)
      next = now + 300;
      break;
    }
    say(std::string("the page shows ") + (sView == 1 ? "the controller's" : "the keyboard's") + " buttons");
    if (sView != 1) {
      btn(SDL_GAMEPAD_BUTTON_NORTH, true); // the top button: the other view
    }
    after(300);
    break;
  }
  case 2:
    btn(SDL_GAMEPAD_BUTTON_NORTH, false);
    say(std::string("now ") + (sView == 1 ? "the controller's" : "the keyboard's") + " buttons");
    sTestFocus = bPad; // the cursor on B's controller button
    after(400);
    break;
  case 3:
    if (gPortLauncherShown) {
      launcherShotNamed("padbind_controller_view"); // (the page as drawn)
    }
    btn(ok, true); // chosen with confirm
    after(150);
    break;
  case 4:
    btn(ok, false);
    say(std::string("B's button chosen with confirm: ") + (sCap.cell == bPad ? "asking" : "NOT asking"));
    {
      ImGuiContext& g = *ImGui::GetCurrentContext();
      char d[256];
      std::snprintf(d, sizeof(d), "(the page's cursor: id %08x in %s, shown %d, nav flags %x, space %d, pad keys %d)",
                    g.NavId, g.NavWindow != nullptr ? g.NavWindow->Name : "-", int(g.NavCursorVisible),
                    unsigned(g.IO.ConfigFlags & 3), int(ImGui::IsKeyDown(ImGuiKey_Space)),
                    int((g.IO.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0));
      say(d);
    }
    after(300);
    break;
  case 5: {
    // (once the question shows: its picture first; the launcher's frames lag while it draws them)
    static int sWaits;
    static bool sShot;
    if (!sShot) {
      if (!(sCap.cell == bPad) && ++sWaits < 40) {
        next = now + 100;
        break;
      }
      say(std::string("B's button: ") + (sCap.cell == bPad ? "asking" : "NOT asking") + " before the answer");
      if (gPortLauncherShown) {
        launcherShotNamed("padbind_asking"); // (the page as drawn)
      }
      sShot = true;
      next = now + 600;
      break;
    }
    // the answer: A's button (the confirm button on most controllers; a Switch's confirm is already B's)
    sAnswer = cellGet(aPad).type == Bind::Button ? cellGet(aPad).a : ok;
    btn(sAnswer, true);
    after(150);
    break;
  }
  case 6: btn(sAnswer, false); after(400); break;
  case 7:
    say(std::string("A's button as B's: ") + (sPopupShown ? "the question (already A's)" : "no question") +
        ", B " + name(cellGet(bPad)));
    if (gPortLauncherShown) {
      launcherShotNamed("padbind_question"); // (the page as drawn)
    }
    btn(ok, true); // Swap (the question's first button)
    after(150);
    break;
  case 8: btn(ok, false); after(400); break;
  case 9:
    say("after Swap: A " + name(cellGet(aPad)) + ", B " + name(cellGet(bPad)) + (sPopupShown ? " (question still up)" : ""));
    sTestFocus = bPad;
    after(250);
    break;
  case 10: btn(third, true); after(150); break; // the third button clears
  case 11: {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    char d[256];
    std::snprintf(d, sizeof(d), "third held: nav %08x layer %d window %s, windowing %s toggle %d, owner %08x",
                  g.NavId, int(g.NavLayer), g.NavWindow != nullptr ? g.NavWindow->Name : "-",
                  g.NavWindowingTarget != nullptr ? g.NavWindowingTarget->Name : "-", int(g.NavWindowingToggleLayer),
                  ImGui::GetKeyOwner(ImGuiKey_GamepadFaceLeft));
    say(d);
    btn(third, false);
    after(300);
    break;
  }
  case 12:
    say("third on B's button: B " + name(cellGet(bPad)));
    if (gPortLauncherShown) {
      launcherShotNamed("padbind_cleared");
    }
    sTestFocus = aPad;
    after(250);
    break;
  case 13: btn(ok, true); after(150); break;
  case 14: btn(ok, false); after(300); break;
  case 15:
    if (waitAsking(aPad)) {
      break;
    }
    say(std::string("A's button chosen: ") + (sCap.cell == aPad ? "asking" : "NOT asking") + " (cell " +
        std::to_string(sCap.cell.group) + "," + std::to_string(sCap.cell.row) + "," + std::to_string(sCap.cell.col) +
        ", nav " + std::to_string(ImGui::GetCurrentContext()->NavId) + ")");
    btn(SDL_GAMEPAD_BUTTON_START, true); // held a second and more
    after(1400);
    break;
  case 16:
    say(std::string("Start held 1.4 s: ") + (sCap.cell.group < 0 ? "left the question" : "STILL asking") + ", A " +
        name(cellGet(aPad)));
    btn(SDL_GAMEPAD_BUTTON_START, false);
    after(400);
    break;
  case 17: sTestFocus = aSecond; after(250); break;
  case 18: btn(ok, true); after(150); break;
  case 19: btn(ok, false); after(300); break;
  case 20:
    if (waitAsking(aSecond) || (sCap.cell == aSecond && !sCap.armed)) {
      break; // (armed: the press that chose it let go)
    }
    say(std::string("A's second chosen: ") + (sCap.cell == aSecond ? "asking" : "NOT asking") +
        (sCap.armed ? "" : " (not armed yet)"));
    btn(SDL_GAMEPAD_BUTTON_START, true); // a short press: Start is the answer
    after(200);
    break;
  case 21:
    say(std::string("Start down: ") + (sCap.cell == aSecond ? "asking" : "NOT asking") +
        (sCap.startSince != 0 ? ", held" : ", not seen held"));
    btn(SDL_GAMEPAD_BUTTON_START, false);
    after(400);
    break;
  case 22:
    say(std::string("Start tapped as A's second button: ") +
        (sPopupShown ? "the question for " + name(sPending.want) + " (" + cellRowName(sPending.other) + "'s)"
                     : "no question, A's second " + name(cellGet(aSecond))));
    say("(the last question was for " + name(sPending.want) + ", asking " + std::to_string(sCap.cell.group) +
        ", settle " + std::to_string(int(sSettle)) + ", page " + std::to_string(int(sPageOpen)) + ")");
    btn(back, true); // back: the question left
    after(150);
    break;
  case 23: btn(back, false); after(400); break;
  case 24:
    say(std::string("back in the question: ") + (sPopupShown ? "STILL up" : "left") + ", A's second " +
        name(cellGet(aSecond)));
    sTestFocus = aSecond;
    after(250);
    break;
  case 25: btn(ok, true); after(150); break;
  case 26: btn(ok, false); after(300); break;
  case 27: SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX); after(200); break;
  case 28: SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN); after(400); break;
  case 29: keepBoth(0); after(150); break;
  case 30: keepBoth(1); after(250); break;
  case 31: keepBoth(2); after(150); break;
  case 32: keepBoth(3); after(400); break;
  case 33:
    say("the right trigger as A's second button: A's second " + name(cellGet(aSecond)) +
        (sPopupShown ? " (question still up)" : "") + (sSettle ? " (settling" : " (") +
        (anythingHeld() ? ", something held)" : ")"));
    sTestFocus = bSecond;
    after(250);
    break;
  case 34: btn(ok, true); after(150); break;
  case 35: btn(ok, false); after(300); break;
  case 36: SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN); after(200); break; // up
  case 37: SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0); after(400); break;
  case 38: keepBoth(0); after(150); break;
  case 39: keepBoth(1); after(250); break;
  case 40: keepBoth(2); after(150); break;
  case 41: keepBoth(3); after(400); break;
  case 42:
    say("the stick up as B's second button: B's second " + name(cellGet(bSecond)) +
        (sPopupShown ? " (question still up)" : ""));
    btn(back, true); // back out of the page
    after(150);
    break;
  case 43: btn(back, false); after(400); break;
  case 44: {
    if (gPortLauncherShown) {
      ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
      say(std::string("back on the launcher's Settings tab: the cursor ") +
          (ImGui::GetCurrentContext()->NavId == 0 ? "left the list" : "still in it") + " (" +
          (nav != nullptr ? nav->Name : "no window") + ")");
    } else {
      say(std::string("back on the page: ") + (sPageOpen ? "STILL open" : "closed"));
    }
    PADRestoreDefaultMapping(0);
    for (int i = 0; i < kGameRowCount; i++) {
      *sPad2[i][0] = -1;
    }
    portSettingsSave();
    say("done");
    after(100);
    std::fflush(stdout);
    std::_Exit(0); // (no exit handlers from inside a frame)
  }
  // xbox:stick: the page's cursor moved with the left stick alone (menu_buttons.cpp's stickAsDpad): a push
  // down moves it, a push up brings it back, the D-pad with the stick moves once, a held push repeats
  case 100: sView = 1; sTestFocus = aPad; after(400); break; // (the controller's cells)
  // (each push held until the cursor moves, up to two seconds, then let go: a busy machine's slow frames
  // would miss a push shorter than a frame)
  case 101:
    ImGui::GetIO().KeyRepeatDelay = 5.f; // (a slow frame on a busy machine is no repeat: one push, one move)
    sStickFrom = ImGui::GetCurrentContext()->NavId;
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MAX);
    sStickHeldSince = now;
    after(0);
    break;
  case 102:
    if (ImGui::GetCurrentContext()->NavId == sStickFrom && now - sStickHeldSince < 2000) {
      break;
    }
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0);
    after(400);
    break;
  case 103:
    sStickTo = ImGui::GetCurrentContext()->NavId;
    say(std::string("stick down: ") + (sStickTo != sStickFrom && sStickFrom != 0 ? "moved" : "NOT moved") + stickWhere());
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MIN);
    sStickHeldSince = now;
    after(0);
    break;
  case 104:
    if (ImGui::GetCurrentContext()->NavId == sStickTo && now - sStickHeldSince < 2000) {
      break;
    }
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0);
    after(400);
    break;
  case 105:
    say(std::string("stick up: ") + (ImGui::GetCurrentContext()->NavId == sStickFrom ? "back" : "NOT back") + stickWhere());
    btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MAX);
    sStickHeldSince = now;
    after(0);
    break;
  case 106:
    if (ImGui::GetCurrentContext()->NavId == sStickFrom && now - sStickHeldSince < 2000) {
      break;
    }
    btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0);
    after(400);
    break;
  case 107: {
    const ImGuiID at = ImGui::GetCurrentContext()->NavId;
    ImGui::GetIO().KeyRepeatDelay = 0.275f; // (Dear ImGui's own)
    say(std::string("D-pad and stick together: ") + (at == sStickTo ? "one move" : at == sStickFrom ? "NO move" : "TWO moves"));
    sTestFocus = aPad;
    after(400);
    break;
  }
  case 108:
    sStickMoves = 0;
    sStickFrom = ImGui::GetCurrentContext()->NavId;
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MAX);
    sStickHeldSince = now;
    after(0);
    break;
  case 109: // (each frame while held: the cursor's moves counted)
    if (ImGui::GetCurrentContext()->NavId != sStickFrom) {
      sStickFrom = ImGui::GetCurrentContext()->NavId;
      sStickMoves++;
    }
    if (now - sStickHeldSince < 1000) {
      break;
    }
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, 0);
    say("stick held a second: " + std::to_string(sStickMoves) + (sStickMoves >= 2 ? " moves (repeats)" : " moves, NO repeat"));
    if (gPortLauncherShown) {
      launcherShotNamed("padbind_stick");
    }
    after(300);
    break;
  case 110:
    say("done");
    std::fflush(stdout);
    std::_Exit(0);
  // xbox:menu: the menus' confirm put on the left button, used, back put on it too (the two swap), Reset all,
  // then no controller button for the Settings action: Start and Back held bring the page back
  case 200: {
    auto roles = [&](const char* what) {
      const GlyphStyle st = styleFor(0);
      char line[256];
      std::snprintf(line, sizeof(line), "%s: confirm %s, back %s, third %s (chosen %d, %d)", what,
                    nativeButtonLabel(st, u32(portMenuRoleButton(PORT_MENU_CONFIRM, 0)), 0),
                    nativeButtonLabel(st, u32(portMenuRoleButton(PORT_MENU_BACK, 0)), 0),
                    nativeButtonLabel(st, u32(portMenuRoleButton(PORT_MENU_THIRD, 0)), 0),
                    portMenuChosenPlace(PORT_MENU_CONFIRM), portMenuChosenPlace(PORT_MENU_BACK));
      say(line);
    };
    portMenuSetPlace(PORT_MENU_CONFIRM, 2);
    roles("menu confirm on the left button");
    portMenuSetPlace(PORT_MENU_BACK, 2);
    roles("menu back on the left button too");
    portMenuSetPlace(PORT_MENU_BACK, -1);
    portMenuSetPlace(PORT_MENU_CONFIRM, 2);
    roles("menu confirm on the left button again");
    sView = 1;
    btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true); // (a first press: the page's cursor shown)
    after(150);
    break;
  }
  case 201: btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false); after(300); break;
  case 202: sTestFocus = bPad; after(400); break;
  case 203: btn(SDL_GAMEPAD_BUTTON_WEST, true); after(150); break;
  case 204: {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    char d[256];
    std::snprintf(d, sizeof(d),
                  "(left held: nav %08x layer %d window %s, windowing %s toggle %d, space %d owner %08x, active %08x, "
                  "cursor %d, source %d, asking %d)",
                  g.NavId, int(g.NavLayer), g.NavWindow != nullptr ? g.NavWindow->Name : "-",
                  g.NavWindowingTarget != nullptr ? g.NavWindowingTarget->Name : "-", int(g.NavWindowingToggleLayer),
                  int(ImGui::IsKeyDown(ImGuiKey_Space)), ImGui::GetKeyOwner(ImGuiKey_Space), g.ActiveId,
                  int(g.NavCursorVisible), int(g.NavInputSource), sCap.cell.group);
    say(d);
    btn(SDL_GAMEPAD_BUTTON_WEST, false);
    after(300);
    break;
  }
  case 205:
    if (waitAsking(bPad)) {
      break;
    }
    say(std::string("the left button as confirm: B's button ") + (sCap.cell == bPad ? "asking" : "NOT asking"));
    btn(SDL_GAMEPAD_BUTTON_START, true); // held: the question left as it is
    after(1400);
    break;
  case 206:
    btn(SDL_GAMEPAD_BUTTON_START, false);
    say(std::string("Start held: ") + (sCap.cell.group < 0 ? "left the question" : "STILL asking"));
    after(400);
    break;
  case 207: {
    resetAll();
    char line[160];
    std::snprintf(line, sizeof(line), "after Reset all: confirm %d, back %d (chosen %d, %d)",
                  portMenuRoleButton(PORT_MENU_CONFIRM, 0), portMenuRoleButton(PORT_MENU_BACK, 0),
                  portMenuChosenPlace(PORT_MENU_CONFIRM), portMenuChosenPlace(PORT_MENU_BACK));
    say(line);
    back = portMenuRoleButton(PORT_MENU_BACK, 0);
    cellSet(Cell{kGroupAction, PORT_BIND_SETTINGS, 2}, Bind{});
    cellSet(Cell{kGroupAction, PORT_BIND_SETTINGS, 3}, Bind{});
    say(std::string("the Settings action's buttons cleared: ") +
        (settingsOnPad() ? "a button STILL opens the page" : "no button opens the page"));
    btn(back, true); // the page closed
    after(150);
    break;
  }
  case 208: btn(back, false); after(600); break;
  case 209:
    say(std::string("back on the page: ") + (sPageOpen ? "STILL open" : "closed"));
    btn(SDL_GAMEPAD_BUTTON_START, true);
    btn(SDL_GAMEPAD_BUTTON_BACK, true);
    sStickHeldSince = now;
    after(0);
    break;
  case 210:
    if (!sPageOpen && now - sStickHeldSince < 5000) {
      break;
    }
    btn(SDL_GAMEPAD_BUTTON_START, false);
    btn(SDL_GAMEPAD_BUTTON_BACK, false);
    say(sPageOpen ? "Start and Back held: the page opened after " + std::to_string(now - sStickHeldSince) + " ms"
                  : std::string("Start and Back held 5 s: the page NOT opened"));
    after(400);
    break;
  case 211:
    resetAll();
    portSettingsSave();
    say("done");
    std::fflush(stdout);
    std::_Exit(0);
  default: break;
  }
}

// Tests: $TTYD_REBIND_TEST, the keyboard's way out of a key's question, through SDL's own key events
// ("[rebind] ..." lines; a test user dir):
//  - page: the settings page open on Controls (TTYD_SETTINGS_OPEN=1 TTYD_SETTINGS_TAB=Controls), no
//    controller: B's key asked for and answered in turn with Escape pressed and let go (left as it was), K
//    (B's key K), Escape held for 1.3 s (B's key Escape) and E, the game's B (the key E: no way out); a
//    capture of the question (rebind_page_question); B's key put back.
//  - menu: the Start menu's Bindings page, opened and asked by the scenario's input script (A on A's key,
//    then wait_bindings): each question answered in turn with Escape pressed and let go, K, Escape held
//    for 1.3 s and E; A's key put back after.
void rebindTestFrame() {
  static const char* mode = SDL_getenv("TTYD_REBIND_TEST");
  if (mode == nullptr) {
    return;
  }
  static int step, frames, answer;
  static Uint64 next;
  static Bind was;
  static bool over; // this answer's question is over (its result said)
  if (++frames < 30 || !portWarpSettled()) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  const bool menu = std::strcmp(mode, "menu") == 0;
  const Cell c = menu ? Cell{kGroupGame, 0, 0} : Cell{kGroupGame, 1, 0};
  const char* row = menu ? "A" : "B";
  auto after = [&](Uint64 ms) {
    next = now + ms;
    step++;
  };
  auto key = [](int sc, bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = static_cast<SDL_Scancode>(sc);
    e.key.key = SDL_GetKeyFromScancode(e.key.scancode, SDL_KMOD_NONE, false);
    e.key.down = down;
    SDL_PushEvent(&e);
  };
  auto keyOf = [](const Cell& at) {
    sPlayer = 0;
    const Bind b = cellGet(at);
    return b.type == Bind::Key ? std::string(SDL_GetScancodeName(static_cast<SDL_Scancode>(b.a))) : std::string("(none)");
  };
  auto say = [](const std::string& s) {
    std::printf("[rebind] %s\n", s.c_str());
    std::fflush(stdout);
  };
  // the answers in turn: the key, and how long it is held (ms)
  static const struct {
    int sc;
    Uint64 hold;
    const char* what;
  } kAnswers[] = {{SDL_SCANCODE_ESCAPE, 100, "Escape pressed and let go"},
                  {SDL_SCANCODE_K, 100, "K pressed"},
                  {SDL_SCANCODE_ESCAPE, 1300, "Escape held 1.3 s"},
                  {SDL_SCANCODE_E, 100, "E (the game's B) pressed"}};
  constexpr int kAnswerCount = sizeof(kAnswers) / sizeof(kAnswers[0]);
  const std::string where = menu ? "menu: " : "page: ";
  // the result the frame the question is over (a script's next A can come soon after: the menu's
  // wait_bindings), whatever the step
  if (step >= 3 && step <= 5 && !over && !(sCap.cell == c)) {
    over = true;
    say(where + kAnswers[answer].what + ": " + row + "'s key " + keyOf(c) + " (was " +
        (was.type == Bind::Key ? SDL_GetScancodeName(static_cast<SDL_Scancode>(was.a)) : "(none)") + ")");
  }
  if (SDL_GetTicks() < next) {
    return;
  }
  const bool held = kAnswers[answer].hold > 100; // (else pressed and let go at once)
  switch (step) {
  case 0: // the page shown (menu: a question asked by the script)
    if (menu ? !(sCap.cell == c && sCap.menu)
             : ImGui::GetCurrentContext() == nullptr || ImGui::GetFrameCount() - sLastDrawn > 2) {
      next = now + 50;
      break;
    }
    if (answer == 0) {
      was = cellGet(c);
      say(where + row + "'s key " + keyOf(c) + (menu ? " (the question up)" : ""));
    }
    if (!menu) {
      sPlayer = 0;
      ask(c);
    }
    after(menu ? 600 : 400);
    break;
  case 1:
    say(where + "asking " + std::to_string(int(sCap.cell == c)) + ", the hint for a key: " +
        (keyQuestionPadToo() ? "with the controller's" : "Escape alone"));
    if (answer == 0 && !menu) {
      portShotRequest("rebind_page_question");
    }
    after(menu ? 100 : 500);
    break;
  case 2:
    over = false;
    key(kAnswers[answer].sc, true);
    if (!held) {
      key(kAnswers[answer].sc, false);
    }
    after(held ? kAnswers[answer].hold / 2 : 0);
    break;
  case 3:
    if (held) {
      say(where + kAnswers[answer].what + ": halfway, asking " + std::to_string(int(sCap.cell == c)));
    }
    after(held ? kAnswers[answer].hold - kAnswers[answer].hold / 2 : 0);
    break;
  case 4:
    if (held) {
      key(kAnswers[answer].sc, false);
    }
    after(0);
    break;
  case 5: // (the question over: Escape's release left it, or the key was the answer; said above)
    if (!over) {
      if (now - next > 5000) {
        say(where + kAnswers[answer].what + ": the question STILL up");
        over = true;
      }
      break;
    }
    if (++answer < kAnswerCount) {
      step = 0;
      next = now + 300;
      break;
    }
    sPlayer = 0;
    cellSet(c, was);
    say(where + row + "'s key put back: " + keyOf(c) + "; done");
    if (menu) {
      mode = nullptr; // (the script goes on: back out of the page)
      break;
    }
    after(500); // (the capture written)
    break;
  case 6:
    std::fflush(stdout);
    std::_Exit(0);
  default: break;
  }
}

const bool sAdded = [] {
  portSettingsAddSection("Controls", "Bindings",
                         "Every key and button: the game's and Paper Cut's own.", 0, section,
                         "bindings keybinds keys keyboard controller buttons remap rebind mapping gamepad shortcuts "
                         "hotkeys swap sticks invert dead zone triggers combo");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace
