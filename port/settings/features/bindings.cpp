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
#include "port/companion_menu.h"
#include "port/savestate.h"
#include "port/settings.h"
#include "port/speed.h"
#include "port/tr.h"

#include "../button_glyphs.h"
#include "../settings_ui.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void portSettingsToggle(void);  // port/settings/settings_ui.cpp: the page
extern "C" void portShotRequestUser(void); // shot_aurora.c (port/platform.h, C only)

namespace {

// --- What can be bound ---------------------------------------------------------

// A binding: a key, a controller button, two held together, or a stick or
// trigger direction (a game row's controller cell).
struct Bind {
  enum Type { None, Key, Button, Combo, Axis } type = None;
  int a = 0, b = 0; // Key: scancode; Button: a; Combo: a + b; Axis: a (SDL_GamepadAxis), b (the sign)
  bool operator==(const Bind& o) const {
    if (type != o.type) {
      return false;
    }
    switch (type) {
    case None: return true;
    case Key:
    case Button: return a == o.a;
    case Combo: return (a == o.a && b == o.b) || (a == o.b && b == o.a);
    case Axis: return a == o.a && b == o.b;
    }
    return false;
  }
};
Bind keyBind(int sc) { return sc > 0 ? Bind{Bind::Key, sc, 0} : Bind{}; }

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
};
ActionSettings sSet[PORT_BIND_COUNT];
int* sKey2[kGameRowCount];
int* sDigital[4]; // per player: the triggers digital

const bool sDeclared = [] {
  for (int i = 0; i < PORT_BIND_COUNT; i++) {
    const ActionRow& a = kActions[i];
    char name[64];
    if (a.keySetting != nullptr) {
      sSet[i].key = portSettingsDeclare(a.keySetting, a.key, 0, SDL_SCANCODE_COUNT - 1);
    } else {
      std::snprintf(name, sizeof(name), "bind_%s_key", a.id);
      sSet[i].key = portSettingsDeclare(name, a.key, -1, SDL_SCANCODE_COUNT - 1);
    }
    std::snprintf(name, sizeof(name), "bind_%s_key2", a.id);
    sSet[i].key2 = portSettingsDeclare(name, 0, 0, SDL_SCANCODE_COUNT - 1);
    std::snprintf(name, sizeof(name), "bind_%s_pad", a.id);
    sSet[i].pad = portSettingsDeclare(name, a.pad, kPadLegacy, 1023);
  }
  for (int i = 0; i < kGameRowCount; i++) {
    sKey2[i] = portSettingsDeclare(kGameRows[i].key2, kGameRows[i].key2Default, PAD_KEY_INVALID, SDL_SCANCODE_COUNT - 1);
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
int actionKey(int a, int col) {
  int v = col == 0 ? *sSet[a].key : *sSet[a].key2;
  if (v < 0 && a == PORT_BIND_WALK_BOOST) {
    v = kWalkKeys[legacy("walk_boost_key", 4)];
  }
  return v > 0 ? v : 0;
}
Bind actionPad(int a) {
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
    const PADKeyAxisBinding* axes = PADGetKeyAxisBindings(0, &count);
    for (u32 i = 0; axes != nullptr && i < count; i++) {
      if (axes[i].padAxis == r.target) {
        return axes[i].scancode;
      }
    }
  } else {
    const PADKeyButtonBinding* buttons = PADGetKeyButtonBindings(0, &count);
    for (u32 i = 0; buttons != nullptr && i < count; i++) {
      if (buttons[i].padButton == r.target) {
        return buttons[i].scancode;
      }
    }
  }
  return PAD_KEY_INVALID;
}

void setGameKey(int row, s32 scancode) {
  const GameRow& r = kGameRows[row];
  if (r.axis) {
    PADSetKeyAxisBinding(0, PADKeyAxisBinding{scancode, r.target, 100});
  } else {
    PADSetKeyButtonBinding(0, PADKeyButtonBinding{scancode, r.target});
  }
  PADSetKeyboardActive(0, TRUE);
  PADSerializeMappings();
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

void setGamePad(u32 port, int row, const Bind& b) {
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
  PADSerializeMappings();
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

// a binding's name in the menu language
std::string bindName(const Bind& b, GlyphStyle st) {
  switch (b.type) {
  case Bind::Key: {
    const char* n = SDL_GetScancodeName(static_cast<SDL_Scancode>(b.a));
    return n != nullptr && n[0] != '\0' ? n : "?";
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
  int group = -1, row = 0, col = 0; // col: 0 key, 1 second key, 2 controller
  bool operator==(const Cell& o) const { return group == o.group && row == o.row && col == o.col; }
};

int sPlayer; // the page's player (0-3): the game rows' controller column

Bind cellGet(const Cell& c) {
  if (c.group == kGroupGame) {
    if (c.col == 0) {
      return keyBind(gameKey(c.row));
    }
    if (c.col == 1) {
      return keyBind(*sKey2[c.row]);
    }
    return gamePad(static_cast<u32>(sPlayer), c.row);
  }
  return c.col == 2 ? actionPad(c.row) : keyBind(actionKey(c.row, c.col));
}

// what a cell takes: keys for the keyboard's; a button (or for L, R and the
// sticks a stick or trigger direction) for a game row's controller; a button
// or a combo for an action's
bool cellTakes(const Cell& c, const Bind& b) {
  if (b.type == Bind::None) {
    return true;
  }
  if (c.col < 2) {
    return b.type == Bind::Key;
  }
  if (c.group == kGroupAction) {
    return b.type == Bind::Button || b.type == Bind::Combo;
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
    } else {
      setGamePad(static_cast<u32>(sPlayer), c.row, b);
    }
    return;
  }
  if (c.col == 2) {
    *sSet[c.row].pad = padValue(b);
  } else {
    *(c.col == 0 ? sSet[c.row].key : sSet[c.row].key2) = b.type == Bind::Key ? b.a : 0;
  }
  portSettingsSave();
}

Bind cellDefault(const Cell& c) {
  if (c.group == kGroupGame) {
    if (c.col == 0) {
      return keyBind(kGameRows[c.row].key);
    }
    if (c.col == 1) {
      return keyBind(kGameRows[c.row].key2Default);
    }
    return gamePadDefault(static_cast<u32>(sPlayer), c.row);
  }
  return actionDefault(c.row, c.col);
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
      for (int col = 0; col < 3; col++) {
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
} sCap;
bool sSettle;        // a binding was just set: the page and the menu wait until everything is let go
int sLastDrawn = -1; // the frame the section was last drawn (asking ends when it isn't)

struct Pending { // a binding that is already another row's: Swap, Keep both or Cancel
  Cell cell, other;
  Bind want, had;
  bool open = false;
} sPending;
bool sPopupShown; // its question on screen (B and Back answer it, not the page)

void finish(const Cell& c, const Bind& b) {
  const bool menu = sCap.menu;
  sCap = {};
  sSettle = true;
  const Cell other = conflictOf(c, b);
  if (other.group < 0 || menu) { // (the menu shows a conflict in its help line instead)
    cellSet(c, b);
    return;
  }
  sPending = {c, other, b, cellGet(c), true};
}

void cancelCapture() {
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
  return false;
}

// each frame: a controller button (or stick) for the cell being asked about
void pollCapture() {
  if (sSettle && !anythingHeld()) {
    sSettle = false;
  }
  if (sCap.cell.group < 0) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  if (now - sCap.since > 5000 && sCap.first < 0) {
    cancelCapture();
    return;
  }
  const bool padCell = sCap.cell.col == 2;
  if (!padCell) {
    return; // a key: portBindingsEvent
  }
  if (!sCap.armed) {
    if (!anythingHeld()) {
      sCap.armed = true;
      sCap.since = now;
    }
    return;
  }
  if (sCap.cell.group == kGroupGame) {
    const u32 port = static_cast<u32>(sPlayer);
    const int row = sCap.cell.row;
    const s32 b = PADGetNativeButtonPressed(port);
    Bind got;
    if (b >= 0) {
      got = Bind{Bind::Button, b, 0};
    } else {
      const PADSignedNativeAxis ax = PADGetNativeAxisPulled(port);
      if (ax.nativeAxis >= 0) {
        got = Bind{Bind::Axis, ax.nativeAxis, ax.sign};
      }
    }
    const Cell c{kGroupGame, row, 2};
    if (got.type == Bind::None || !cellTakes(c, got)) {
      return;
    }
    finish(c, got);
    return;
  }
  // an action: one button, or a second while the first is held
  if (sCap.first < 0) {
    for (SDL_Gamepad* p : gamepads()) {
      for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
        if (SDL_GetGamepadButton(p, SDL_GamepadButton(b))) {
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

void ask(const Cell& c) {
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
    const Bind p = actionPad(a);
    if (p.type == Bind::Combo && (p.a == b || p.b == b)) {
      return true;
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
    const Bind p = actionPad(a);
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
  }
  if (used) {
    return 1;
  }
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    const Bind p = actionPad(a);
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

int keyDown(int sc, bool repeat) {
  if (sc <= 0 || gameKeyIs(sc)) {
    return 0;
  }
  bool used = false;
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    if (kActions[a].kind == kPolled || (actionKey(a, 0) != sc && actionKey(a, 1) != sc)) {
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

// one cell's button: its binding, or "Press a key..." while it is asked about
void drawCell(const Cell& c, float width) {
  ImGui::PushID(c.group * 1000 + c.row * 4 + c.col);
  const bool asking = sCap.cell == c;
  const bool padCell = c.col == 2;
  const bool noPad = padCell && c.group == kGroupGame && !haveController(static_cast<u32>(sPlayer));
  const bool noKeys = !padCell && c.group == kGroupGame && sPlayer != 0;
  std::string label;
  if (asking) {
    label = portTr(padCell ? (c.group == kGroupAction ? "Press a button (or two)..." : "Press a button...")
                           : "Press a key...");
  } else if (noPad || noKeys) {
    label = "-";
  } else {
    const Bind b = cellGet(c);
    label = b.type == Bind::None ? std::string(portTr("(none)"))
                                 : bindName(b, styleFor(c.group == kGroupGame ? static_cast<u32>(sPlayer) : 0));
  }
  const bool isDefault = !asking && !noPad && !noKeys && cellGet(c) == cellDefault(c);
  ImGui::BeginDisabled(noPad || noKeys);
  if (!isDefault && !asking) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.20f, 0.05f, 1.f)); // changed: a warmer ink
  }
  if (ImGui::Button((label + "##cell").c_str(), ImVec2(width, 0.f))) {
    if (asking) {
      cancelCapture();
    } else {
      ask(c);
    }
  }
  if (!isDefault && !asking) {
    ImGui::PopStyleColor();
  }
  // clear: right-click, X on a controller, Delete
  if (!asking && (ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
                  (ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) ||
                                              ImGui::IsKeyPressed(ImGuiKey_Delete, false))))) {
    cellSet(c, Bind{});
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !asking) {
    ImGui::SetTooltip("%s", portTr("Choose, then press the key or button. Right-click, X or Delete: none."));
  }
  ImGui::EndDisabled();
  ImGui::PopID();
}

void drawRow(int group, int row, float cellWidth) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  const char* name = group == kGroupGame ? kGameRows[row].name : kActions[row].name;
  ImGui::TextUnformatted(portTr(name));
  for (int col = 0; col < 3; col++) {
    ImGui::TableSetColumnIndex(col + 1);
    drawCell(Cell{group, row, col}, cellWidth);
  }
  ImGui::TableSetColumnIndex(4);
  ImGui::PushID(group * 1000 + row);
  bool changed = false;
  for (int col = 0; col < 3 && !changed; col++) {
    const Cell c{group, row, col};
    if ((col < 2 && group == kGroupGame && sPlayer != 0) ||
        (col == 2 && group == kGroupGame && !haveController(static_cast<u32>(sPlayer)))) {
      continue;
    }
    changed = !(cellGet(c) == cellDefault(c));
  }
  ImGui::BeginDisabled(!changed);
  if (ImGui::SmallButton(portTrLabel("Reset##row"))) {
    for (int col = 0; col < 3; col++) {
      const Cell c{group, row, col};
      if ((col < 2 && group == kGroupGame && sPlayer != 0) ||
          (col == 2 && group == kGroupGame && !haveController(static_cast<u32>(sPlayer)))) {
        continue;
      }
      cellSet(c, cellDefault(c));
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
  sPopupShown = ImGui::BeginPopupModal("##bind_conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  if (!sPopupShown) {
    return;
  }
  const GlyphStyle st = styleFor(sPending.cell.group == kGroupGame || sPending.other.group == kGroupGame
                                     ? static_cast<u32>(sPlayer)
                                     : 0);
  ImGui::Text(portTr("%s is already %s."), bindName(sPending.want, st).c_str(), cellRowName(sPending.other));
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
  if (ImGui::Button(portTrLabel("Cancel"))) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void resetAll() {
  const u32 port = static_cast<u32>(sPlayer);
  if (haveController(port)) {
    PADRestoreDefaultMapping(port);
    PADSerializeMappings();
  }
  if (sPlayer == 0) {
    PADRestoreDefaultKeyBindings(0);
    PADSetKeyboardActive(0, TRUE);
    PADSerializeMappings();
    for (int i = 0; i < kGameRowCount; i++) {
      *sKey2[i] = kGameRows[i].key2Default;
    }
  }
  for (int a = 0; a < PORT_BIND_COUNT; a++) {
    for (int col = 0; col < 3; col++) {
      cellSet(Cell{kGroupAction, a, col}, actionDefault(a, col));
    }
  }
  if (sPlayer >= 0 && sPlayer < 4) {
    *sDigital[sPlayer] = 0;
  }
  portSettingsSave();
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
  }
}

void section(int) {
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
  const float cell = ImGui::GetFontSize() * 7.5f;
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
  // rows tall enough for their buttons, clear of the header and of each other
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetFontSize() * 0.3f, ImGui::GetFontSize() * 0.25f));
  if (ImGui::BeginTable("##bindings", 5, flags)) {
    ImGui::TableSetupColumn(portTr("The game's buttons"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(portTr("Keyboard"), ImGuiTableColumnFlags_WidthFixed, cell);
    ImGui::TableSetupColumn(portTr("Second key"), ImGuiTableColumnFlags_WidthFixed, cell);
    ImGui::TableSetupColumn(portTr("Controller"), ImGuiTableColumnFlags_WidthFixed, cell * 1.3f);
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
  if (ImGui::BeginTable("##actions", 5, flags)) {
    ImGui::TableSetupColumn(portTr("Action"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(portTr("Keyboard"), ImGuiTableColumnFlags_WidthFixed, cell);
    ImGui::TableSetupColumn(portTr("Second key"), ImGuiTableColumnFlags_WidthFixed, cell);
    ImGui::TableSetupColumn(portTr("Controller"), ImGuiTableColumnFlags_WidthFixed, cell * 1.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (int a = 0; a < PORT_BIND_COUNT; a++) {
      drawRow(kGroupAction, a, -FLT_MIN);
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Choose a cell, then press the key or button (Escape: leave it). An action's "
                                  "controller binding can be two buttons: hold the first, press the second. Any "
                                  "controller does the actions; a key or button that is also one of the game's "
                                  "does only the game's. Changed bindings are in a warmer ink."));
  ImGui::PopStyleColor();
  pendingPopup();
}

void testFrame(); // tests: $TTYD_BINDINGS_TEST (below)

// each presented frame: whether the page is open; asking ends with the page
void tick(int pageOpen) {
  if (sPageOpen != (pageOpen != 0)) {
    releaseAll(); // the page took over, or gave the controls back
  }
  sPageOpen = pageOpen != 0;
  if (sCap.cell.group >= 0 && !sCap.menu && (!pageOpen || ImGui::GetFrameCount() - sLastDrawn > 2)) {
    sCap = {}; // closed, or another tab
  }
  if (!pageOpen || ImGui::GetFrameCount() - sLastDrawn > 2) {
    sPopupShown = false;
  }
  pollCapture();
  testFrame();
}

int keyCapture(const SDL_Event* e) {
  if (sCap.cell.group < 0 || sCap.cell.col == 2) {
    if (sCap.cell.group >= 0 && e->key.scancode == SDL_SCANCODE_ESCAPE) {
      cancelCapture();
      return 1;
    }
    return 0;
  }
  if (e->key.scancode == SDL_SCANCODE_ESCAPE) {
    cancelCapture();
  } else {
    finish(sCap.cell, keyBind(e->key.scancode));
  }
  return 1;
}

} // namespace

extern "C" void portBindingsSetHandler(int action, PortBindHandler handler) {
  if (action >= 0 && action < PORT_BIND_COUNT) {
    sHandlers[action] = handler;
  }
}

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
    return keyDown(e->key.scancode, e->key.repeat);
  case SDL_EVENT_KEY_UP:
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

extern "C" int portBindingsHeld(int action) {
  if (action < 0 || action >= PORT_BIND_COUNT) {
    return 0;
  }
  int count = 0;
  const bool* keys = SDL_GetKeyboardState(&count);
  const bool typing = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantTextInput;
  for (int col = 0; col < 2 && keys != nullptr && !typing; col++) {
    const int k = actionKey(action, col);
    if (k > 0 && k < count && keys[k] && !gameKeyIs(k)) {
      return 1;
    }
  }
  const Bind p = actionPad(action);
  for (SDL_Gamepad* gp : gamepads()) {
    if (p.type == Bind::Button && held(gp, p.a) && !gameButtonIs(gp, p.a)) {
      return 1;
    }
    if (p.type == Bind::Combo && held(gp, p.a) && held(gp, p.b)) {
      return 1;
    }
  }
  return 0;
}

extern "C" const char* portBindingsKeyName(int action) {
  static std::string sName[PORT_BIND_COUNT];
  if (action < 0 || action >= PORT_BIND_COUNT) {
    return "";
  }
  const int k = actionKey(action, 0);
  sName[action] = k > 0 ? SDL_GetScancodeName(static_cast<SDL_Scancode>(k)) : "";
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

// --- The Start menu's page (port/src/aurora/pause_bindings_aurora.c): player 1's -----

namespace {
// a menu row and column as a cell: 0 the key, 1 the controller
Cell menuCell(int row, int col) {
  return row < kGameRowCount ? Cell{kGroupGame, row, col == 0 ? 0 : 2}
                             : Cell{kGroupAction, row - kGameRowCount, col == 0 ? 0 : 2};
}
bool menuRowOk(int row) { return row >= 0 && row < kGameRowCount + PORT_BIND_COUNT; }
} // namespace

extern "C" int portBindingsGameRows(void) { return kGameRowCount; }
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
    return portTrGame(c.col == 2 ? (c.group == kGroupAction ? "Press a button (or two)..." : "Press a button...")
                                 : "Press a key...");
  }
  if (c.group == kGroupGame && c.col == 2 && !haveController(0)) {
    return portTrGame("No controller");
  }
  sPlayer = 0;
  const Bind b = cellGet(c);
  // in the game's encoding: the names translated for its font (portTrGame), a key's as SDL has it
  const GlyphStyle st = styleFor(0);
  switch (b.type) {
  case Bind::Key: sText = bindName(b, st); break;
  case Bind::Button: sText = portTrGame(nativeButtonLabel(st, static_cast<u32>(b.a), 0)); break;
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

extern "C" void portBindingsAsk(int row, int col) {
  if (!menuRowOk(row)) {
    return;
  }
  const Cell c = menuCell(row, col);
  if (c.group == kGroupGame && c.col == 2 && !haveController(0)) {
    return;
  }
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
  for (int col = 0; col < 3; col++) {
    const Cell c = row < kGameRowCount ? Cell{kGroupGame, row, col} : Cell{kGroupAction, row - kGameRowCount, col};
    if (c.group == kGroupGame && c.col == 2 && !haveController(0)) {
      continue;
    }
    cellSet(c, cellDefault(c));
  }
}

extern "C" void portBindingsResetAll(void) {
  sPlayer = 0;
  resetAll();
}

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
  if (PADGetKeyButtonBindings(0, &count) == nullptr) {
    PADSetKeyboardActive(0, TRUE);
  }
}

// After each PADRead (DEMOPadRead):
//  - the second keys held (when nothing else has the keyboard: the settings
//    page, a text box): their buttons pressed, and a stick not already pushed
//    pushed their way;
//  - a combo of Paper Cut's held on a controller: its buttons not the game's;
//  - a player's triggers set digital: pressed past the click point, all the way.
extern "C" void portKeyboardApplyPad(PADStatus* pad) {
  // combos' buttons
  for (u32 port = 0; port < 4; port++) {
    const s32 index = PADGetIndexForPort(port);
    SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
    if (gp == nullptr) {
      continue;
    }
    for (int a = 0; a < PORT_BIND_COUNT; a++) {
      const Bind p = actionPad(a);
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
      pad[0].button |= r.target;
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
  pad[0].err = PAD_ERR_NONE;
  auto push = [](s8* x, s8* y, int dx, int dy) {
    if ((dx == 0 && dy == 0) || std::abs(*x) > 16 || std::abs(*y) > 16) {
      return;
    }
    const float k = dx != 0 && dy != 0 ? 0.7071f : 1.f; // the same speed diagonally
    *x = static_cast<s8>(dx * 100 * k);
    *y = static_cast<s8>(dy * 100 * k);
  };
  push(&pad[0].stickX, &pad[0].stickY, sx, sy);
  push(&pad[0].substickX, &pad[0].substickY, cx, cy);
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

const bool sAdded = [] {
  portSettingsAddSection("Controls", "Bindings",
                         "Every key and controller button: the game's, and Paper Cut's own shortcuts.", 0, section,
                         "bindings keybinds keys keyboard controller buttons remap rebind mapping gamepad shortcuts "
                         "hotkeys swap sticks invert dead zone triggers combo");
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace
