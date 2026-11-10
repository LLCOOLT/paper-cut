// Tests: the devices of the device flows (port/src/common/flows_pc.c, an input script's "dev" steps,
// docs/TESTING.md): virtual SDL gamepads plugged in and out and pressed as a player's controller is (the
// same SDL events, so Aurora's controller ports, the join rules and the touch controls' take-over see a
// real controller), keyboard keys pushed through SDL's event queue, and the touch controls' fingers
// (port/touch/touch_overlay.cpp). Nothing here runs without a script asking for it.
#include "port/menu_buttons.h"
#include "port/settings.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif

#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <dolphin/pad.h>

#include <cstdio>
#include <cstring>

extern "C" int portInputHomePort(void); // input_pc.c: the keyboard's and touch controls' port

namespace {

constexpr int kPads = 4;
SDL_Joystick* sJoy[kPads + 1]; // (1-4)

bool named(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

SDL_Gamepad* gamepadOf(int n) {
  if (n < 1 || n > kPads || sJoy[n] == nullptr) {
    return nullptr;
  }
  const SDL_JoystickID id = SDL_GetJoystickID(sJoy[n]);
  SDL_Gamepad* g = SDL_GetGamepadFromID(id);
  return g != nullptr ? g : SDL_OpenGamepad(id);
}

// Aurora's index of test pad n (-1 none yet)
s32 indexOf(int n) {
  SDL_Gamepad* g = gamepadOf(n);
  for (u32 i = 0; g != nullptr && i < PADCount(); i++) {
    if (PADGetSDLGamepadForIndex(i) == g) {
      return static_cast<s32>(i);
    }
  }
  return -1;
}

int portOf(int n) {
  const s32 index = indexOf(n);
  for (u32 p = 0; index >= 0 && p < PAD_CHANMAX; p++) {
    if (PADGetIndexForPort(p) == index) {
      return static_cast<int>(p);
    }
  }
  return -1;
}

// the SDL button a GameCube button name is on this pad (its port's mapping, else the usual layout's)
int nativeOf(int n, const char* key) {
  static const struct {
    const char* name;
    u16 pad;
    int fallback;
  } kKeys[] = {{"a", PAD_BUTTON_A, SDL_GAMEPAD_BUTTON_SOUTH},        {"b", PAD_BUTTON_B, SDL_GAMEPAD_BUTTON_WEST},
               {"x", PAD_BUTTON_X, SDL_GAMEPAD_BUTTON_EAST},         {"y", PAD_BUTTON_Y, SDL_GAMEPAD_BUTTON_NORTH},
               {"start", PAD_BUTTON_START, SDL_GAMEPAD_BUTTON_START}, {"z", PAD_TRIGGER_Z, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
               {"dup", PAD_BUTTON_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},   {"ddown", PAD_BUTTON_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
               {"dleft", PAD_BUTTON_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
               {"dright", PAD_BUTTON_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};
  if (named(key, "back")) {
    return SDL_GAMEPAD_BUTTON_BACK; // (not a GameCube button: the controller's own)
  }
  // the button labelled B on this pad (an Xbox pad's right one, a Switch pad's bottom one): menus' back, the one
  // rule (port/include/port/menu_buttons.h); "confirm" the one labelled A
  if (named(key, "cancel") || named(key, "confirm")) {
    SDL_Gamepad* g = gamepadOf(n);
    return g != nullptr ? portMenuRoleButton(named(key, "cancel") ? PORT_MENU_BACK : PORT_MENU_CONFIRM,
                                             static_cast<unsigned>(SDL_GetGamepadID(g)))
                        : -1;
  }
  // the controller's own face buttons by place (Paper Cut's own pages: an Xbox pad's right one, its B, is back)
  if (named(key, "south") || named(key, "east") || named(key, "west") || named(key, "north")) {
    return named(key, "south") ? SDL_GAMEPAD_BUTTON_SOUTH
           : named(key, "east") ? SDL_GAMEPAD_BUTTON_EAST
           : named(key, "west") ? SDL_GAMEPAD_BUTTON_WEST
                                : SDL_GAMEPAD_BUTTON_NORTH;
  }
  for (const auto& k : kKeys) {
    if (!named(key, k.name)) {
      continue;
    }
    const int port = portOf(n);
    u32 count = 0;
    const PADButtonMapping* maps = port >= 0 ? PADGetButtonMappings(static_cast<u32>(port), &count) : nullptr;
    for (u32 i = 0; maps != nullptr && i < count; i++) {
      if (maps[i].padButton == k.pad && maps[i].nativeButton != PAD_NATIVE_BUTTON_INVALID) {
        return static_cast<int>(maps[i].nativeButton);
      }
    }
    return k.fallback;
  }
  return -1;
}

SDL_WindowID windowId() {
  SDL_Window* w = aurora_get_window();
  return w != nullptr ? SDL_GetWindowID(w) : 0;
}

void pushKey(SDL_Scancode sc, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.windowID = windowId();
  e.key.scancode = sc;
  e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
  e.key.down = down;
  SDL_PushEvent(&e);
}

} // namespace

// test pad n (1-4) plugged in: KIND xbox (the default), ps, switch or deck. 0 when SDL has none to give
extern "C" int portFlowsPadAttach(int n, const char* kind) {
  struct Kind {
    const char* name;
    Uint16 vendor, product;
  };
  static const Kind kKinds[] = {
      {"xbox", 0x045E, 0x02EA}, {"ps", 0x054C, 0x0CE6}, {"switch", 0x057E, 0x2009}, {"deck", 0x28DE, 0x1205}};
  if (n < 1 || n > kPads || sJoy[n] != nullptr) {
    return 0;
  }
  Kind k = kKinds[0];
  for (const Kind& c : kKinds) {
    if (kind != nullptr && named(kind, c.name)) {
      k = c;
    }
  }
  // (an offscreen run's window never has the focus: SDL drops a controller's presses without it)
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  static char names[kPads + 1][32];
  std::snprintf(names[n], sizeof(names[n]), "Paper Cut flow pad %d", n);
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.vendor_id = k.vendor;
  desc.product_id = k.product;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
  desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
  desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
  desc.name = names[n];
  const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  sJoy[n] = id != 0 ? SDL_OpenJoystick(id) : nullptr;
  if (sJoy[n] == nullptr) {
    std::printf("[flows] pad %d: no virtual controller (%s)\n", n, SDL_GetError());
    return 0;
  }
  SDL_SetJoystickVirtualAxis(sJoy[n], SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
  SDL_SetJoystickVirtualAxis(sJoy[n], SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
  return 1;
}

// test pad n unplugged (a controller switched off)
extern "C" void portFlowsPadDetach(int n) {
  if (n < 1 || n > kPads || sJoy[n] == nullptr) {
    return;
  }
  const SDL_JoystickID id = SDL_GetJoystickID(sJoy[n]);
  SDL_CloseJoystick(sJoy[n]);
  sJoy[n] = nullptr;
  SDL_DetachVirtualJoystick(id);
}

// KEYS (GameCube names joined with '+': a b x y z l r start dup ddown dleft dright, back, and left right up
// down for the stick at full tilt) held (down 1) or let go (0) on test pad n. 0 when a key is unknown
extern "C" int portFlowsPadSet(int n, const char* keys, int down) {
  if (n < 1 || n > kPads || sJoy[n] == nullptr) {
    return 0;
  }
  char list[64];
  std::snprintf(list, sizeof(list), "%s", keys);
  int ok = 1;
  for (char* k = std::strtok(list, "+"); k != nullptr; k = std::strtok(nullptr, "+")) {
    const Sint16 full = down ? SDL_JOYSTICK_AXIS_MAX : 0, fullNeg = down ? SDL_JOYSTICK_AXIS_MIN : 0;
    if (named(k, "left") || named(k, "right")) {
      SDL_SetJoystickVirtualAxis(sJoy[n], SDL_GAMEPAD_AXIS_LEFTX, named(k, "left") ? fullNeg : full);
    } else if (named(k, "up") || named(k, "down")) {
      SDL_SetJoystickVirtualAxis(sJoy[n], SDL_GAMEPAD_AXIS_LEFTY, named(k, "up") ? fullNeg : full);
    } else if (named(k, "l") || named(k, "r")) {
      SDL_SetJoystickVirtualAxis(sJoy[n], named(k, "l") ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                                 down ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
    } else if (const int b = nativeOf(n, k); b >= 0) {
      SDL_SetJoystickVirtualButton(sJoy[n], b, down != 0);
    } else {
      ok = 0;
    }
  }
  return ok;
}

// test pad n's controller port now (0-3), -1 none (not plugged in, or no player's)
extern "C" int portFlowsPadPort(int n) { return portOf(n); }

// a keyboard key held (down 1) or let go: a GameCube button's name (a b x y z l r start, left right up down:
// the stick), as the keyboard's port has it bound, or esc, enter, tab, f1. 0 when it has no key
extern "C" int portFlowsKey(const char* key, int down) {
  static const struct {
    const char* name;
    SDL_Scancode sc;
  } kRaw[] = {{"esc", SDL_SCANCODE_ESCAPE}, {"enter", SDL_SCANCODE_RETURN}, {"tab", SDL_SCANCODE_TAB},
              {"f1", SDL_SCANCODE_F1},      {"backspace", SDL_SCANCODE_BACKSPACE}};
  for (const auto& r : kRaw) {
    if (named(key, r.name)) {
      pushKey(r.sc, down != 0);
      return 1;
    }
  }
  static const struct {
    const char* name;
    u16 pad;
  } kButtons[] = {{"a", PAD_BUTTON_A},       {"b", PAD_BUTTON_B},   {"x", PAD_BUTTON_X},    {"y", PAD_BUTTON_Y},
                  {"start", PAD_BUTTON_START}, {"z", PAD_TRIGGER_Z}, {"l", PAD_TRIGGER_L},  {"r", PAD_TRIGGER_R},
                  {"dup", PAD_BUTTON_UP},    {"ddown", PAD_BUTTON_DOWN}};
  static const struct {
    const char* name;
    PADAxis axis;
  } kAxes[] = {{"left", PAD_AXIS_LEFT_X_NEG}, {"right", PAD_AXIS_LEFT_X_POS}, {"up", PAD_AXIS_LEFT_Y_POS},
               {"down", PAD_AXIS_LEFT_Y_NEG}};
  const u32 port = static_cast<u32>(portInputHomePort());
  for (const auto& b : kButtons) {
    if (!named(key, b.name)) {
      continue;
    }
    u32 count = 0;
    const PADKeyButtonBinding* keys = PADGetKeyButtonBindings(port, &count);
    for (u32 i = 0; keys != nullptr && i < count; i++) {
      if (keys[i].padButton == b.pad && keys[i].scancode > 0 && keys[i].scancode < SDL_SCANCODE_COUNT) {
        pushKey(static_cast<SDL_Scancode>(keys[i].scancode), down != 0);
        return 1;
      }
    }
    return 0;
  }
  for (const auto& a : kAxes) {
    if (!named(key, a.name)) {
      continue;
    }
    u32 count = 0;
    const PADKeyAxisBinding* axes = PADGetKeyAxisBindings(port, &count);
    for (u32 i = 0; axes != nullptr && i < count; i++) {
      if (axes[i].padAxis == a.axis && axes[i].scancode > 0 && axes[i].scancode < SDL_SCANCODE_COUNT) {
        pushKey(static_cast<SDL_Scancode>(axes[i].scancode), down != 0);
        return 1;
      }
    }
    return 0;
  }
  return 0;
}

// the touch controls: -1 none in this build, else drawn now (1) or hidden (0)
extern "C" int portFlowsTouchShown(void) {
#ifdef PORT_TOUCH_OVERLAY
  return portTouchTestShown();
#else
  return -1;
#endif
}

extern "C" int portSettingsPageIsOpen(void); // settings_ui.cpp

// the settings page (F1) is open
extern "C" int portFlowsPageOpen(void) { return portSettingsPageIsOpen(); }

// the touch layout editor: open (1), closed (0), -1 none in this build; open asks it open (as its button)
extern "C" int portFlowsEditor(int open) {
#ifdef PORT_TOUCH_OVERLAY
  if (open) {
    portTouchEditRequest();
  }
  return portTouchTestEditing();
#else
  (void)open;
  return -1;
#endif
}

// a finger on touch control NAME (portTouchTestControl), or (NAME null) at window fraction x, y: 0 none
extern "C" int portFlowsTouch(const char* name, float x, float y, int down) {
#ifdef PORT_TOUCH_OVERLAY
  if (name == nullptr) {
    portTouchTestFinger(x, y, down);
    return 1;
  }
  return portTouchTestControl(name, down);
#else
  (void)name;
  (void)x;
  (void)y;
  (void)down;
  return 0;
#endif
}
