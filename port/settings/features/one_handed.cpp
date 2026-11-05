// One-handed controls (Controls tab): everything on one half of a controller,
// as a ready-made button mapping (Aurora's, saved per controller model; the
// Bindings section changes it afterwards, its Reset all undoes it), and hold
// instead of mashing (port/src/common/assist_pc.c).
//
//   Left hand    the stick moves; the D-pad is A, B, X and Y where the face
//                buttons are (down A on most controllers), the left bumper Z,
//                the left trigger R, the stick pressed in L, Back/Select Start
//   Right hand   the right stick moves (the left one is the C-stick); the face
//                buttons, the right bumper (Z) and trigger (R) as they are,
//                the right stick pressed in L, Start
// The GameCube D-pad is left without a button either way: the game hardly uses it.
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace {

enum Hand { kLeft, kRight };

// the D-pad direction where a face button is
u32 dpadAt(u32 face) {
  switch (face) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
  case SDL_GAMEPAD_BUTTON_EAST:
    return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
  case SDL_GAMEPAD_BUTTON_WEST:
    return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
  case SDL_GAMEPAD_BUTTON_NORTH:
    return SDL_GAMEPAD_BUTTON_DPAD_UP;
  default:
    return PAD_NATIVE_BUTTON_INVALID;
  }
}

// the preset for a player's controller, from its defaults; false without one
bool apply(u32 port, Hand hand) {
  u32 count = 0;
  if (PADGetButtonMappings(port, &count) == nullptr) {
    return false;
  }
  PADRestoreDefaultMapping(port);
  PADButtonMapping* maps = PADGetButtonMappings(port, &count);
  for (u32 i = 0; maps != nullptr && i < count; i++) {
    PADButtonMapping m = maps[i];
    switch (m.padButton) {
    case PAD_BUTTON_A:
    case PAD_BUTTON_B:
    case PAD_BUTTON_X:
    case PAD_BUTTON_Y:
      if (hand == kLeft) {
        const u32 d = dpadAt(m.nativeButton);
        if (d != PAD_NATIVE_BUTTON_INVALID) {
          m.nativeButton = d;
        } else { // a controller whose defaults are elsewhere: the GameCube's own places
          m.nativeButton = m.padButton == PAD_BUTTON_A   ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
                           : m.padButton == PAD_BUTTON_B ? SDL_GAMEPAD_BUTTON_DPAD_LEFT
                           : m.padButton == PAD_BUTTON_X ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT
                                                         : SDL_GAMEPAD_BUTTON_DPAD_UP;
        }
      }
      break;
    case PAD_TRIGGER_Z:
      m.nativeButton = hand == kLeft ? SDL_GAMEPAD_BUTTON_LEFT_SHOULDER : SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
      break;
    case PAD_TRIGGER_L:
      m.nativeButton = hand == kLeft ? SDL_GAMEPAD_BUTTON_LEFT_STICK : SDL_GAMEPAD_BUTTON_RIGHT_STICK;
      break;
    case PAD_TRIGGER_R:
      m.nativeButton = PAD_NATIVE_BUTTON_INVALID; // the trigger on that side (below)
      break;
    case PAD_BUTTON_START:
      m.nativeButton = hand == kLeft ? SDL_GAMEPAD_BUTTON_BACK : SDL_GAMEPAD_BUTTON_START;
      break;
    case PAD_BUTTON_UP:
    case PAD_BUTTON_DOWN:
    case PAD_BUTTON_LEFT:
    case PAD_BUTTON_RIGHT:
      m.nativeButton = PAD_NATIVE_BUTTON_INVALID;
      break;
    default:
      break;
    }
    PADSetButtonMapping(port, m);
  }
  PADAxisMapping* axes = PADGetAxisMappings(port, &count);
  for (u32 i = 0; axes != nullptr && i < count; i++) {
    PADAxisMapping a = axes[i];
    auto native = [&](SDL_GamepadAxis axis) {
      a.nativeAxis.nativeAxis = axis;
      a.nativeButton = SDL_GAMEPAD_BUTTON_INVALID;
    };
    switch (a.padAxis) {
    case PAD_AXIS_TRIGGER_R:
      native(hand == kLeft ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
      break;
    case PAD_AXIS_TRIGGER_L:
      native(SDL_GAMEPAD_AXIS_INVALID); // L is the stick's press, all the way in
      break;
    case PAD_AXIS_LEFT_X_POS:
    case PAD_AXIS_LEFT_X_NEG:
      if (hand == kRight) {
        native(SDL_GAMEPAD_AXIS_RIGHTX);
      }
      break;
    case PAD_AXIS_LEFT_Y_POS:
    case PAD_AXIS_LEFT_Y_NEG:
      if (hand == kRight) {
        native(SDL_GAMEPAD_AXIS_RIGHTY);
      }
      break;
    case PAD_AXIS_RIGHT_X_POS:
    case PAD_AXIS_RIGHT_X_NEG:
      if (hand == kRight) {
        native(SDL_GAMEPAD_AXIS_LEFTX);
      }
      break;
    case PAD_AXIS_RIGHT_Y_POS:
    case PAD_AXIS_RIGHT_Y_NEG:
      if (hand == kRight) {
        native(SDL_GAMEPAD_AXIS_LEFTY);
      }
      break;
    default:
      break;
    }
    PADSetAxisMapping(port, a);
  }
  PADSerializeMappings();
  return true;
}

// Tests: $TTYD_ONE_HANDED_TEST=left|right (presented frames from the start): a
// virtual controller plugged in as player 1, the preset applied, then each of
// its buttons and the stick pressed in turn and what the game reads printed
// ("[one-handed] <native> -> <GameCube buttons, stick>")
void testFrame() {
  static const char* spec = SDL_getenv("TTYD_ONE_HANDED_TEST");
  static int frame;
  static SDL_Joystick* joy;
  struct Step {
    int button; // SDL_GamepadButton, or -1 - axis
    const char* name;
  };
  static const Step kSteps[] = {
      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "D-pad down"}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "D-pad right"},
      {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "D-pad left"}, {SDL_GAMEPAD_BUTTON_DPAD_UP, "D-pad up"},
      {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "left bumper"}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, "left stick in"},
      {SDL_GAMEPAD_BUTTON_BACK, "back"}, {SDL_GAMEPAD_BUTTON_SOUTH, "south"}, {SDL_GAMEPAD_BUTTON_EAST, "east"},
      {SDL_GAMEPAD_BUTTON_WEST, "west"}, {SDL_GAMEPAD_BUTTON_NORTH, "north"},
      {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "right bumper"}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "right stick in"},
      {SDL_GAMEPAD_BUTTON_START, "start"}, {-1 - SDL_GAMEPAD_AXIS_LEFT_TRIGGER, "left trigger"},
      {-1 - SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "right trigger"}, {-1 - SDL_GAMEPAD_AXIS_LEFTX, "left stick right"},
      {-1 - SDL_GAMEPAD_AXIS_RIGHTX, "right stick right"}};
  constexpr int kStart = 60, kApply = 120, kEach = 6;
  if (spec == nullptr) {
    return;
  }
  frame++;
  if (frame == kStart) {
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
    if (joy != nullptr) { // the triggers at rest
      SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
      SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    }
    std::printf("[one-handed] test: virtual controller %s\n", joy != nullptr ? "attached" : SDL_GetError());
  } else if (frame == kApply && joy != nullptr) {
    for (u32 i = 0; i < PADCount(); i++) { // player 1 is the test pad (not a real controller plugged in)
      const char* name = PADGetNameForControllerIndex(i);
      if (name != nullptr && std::strcmp(name, "Paper Cut test pad") == 0) {
        PADSetPortForIndex(i, 0);
      }
    }
    const bool ok = apply(0, std::strcmp(spec, "right") == 0 ? kRight : kLeft);
    std::printf("[one-handed] test: %s preset %s\n", spec, ok ? "applied" : "not applied (no controller as player 1)");
  } else if (frame > kApply && joy != nullptr) {
    const int k = (frame - kApply - 1) / kEach, at = (frame - kApply - 1) % kEach;
    if (k >= int(sizeof(kSteps) / sizeof(kSteps[0]))) {
      return;
    }
    const Step& s = kSteps[k];
    auto set = [&](bool down) {
      if (s.button >= 0) {
        SDL_SetJoystickVirtualButton(joy, s.button, down);
      } else {
        const bool trigger = s.button <= -1 - SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        SDL_SetJoystickVirtualAxis(joy, -1 - s.button,
                                   down ? SDL_JOYSTICK_AXIS_MAX : trigger ? SDL_JOYSTICK_AXIS_MIN : 0);
      }
    };
    if (at == 0) {
      set(true);
    } else if (at == 3) {
      PADStatus st[PAD_CHANMAX];
      PADRead(st);
      const s32 index = PADGetIndexForPort(0);
      SDL_Gamepad* gp = index >= 0 ? PADGetSDLGamepadForIndex(u32(index)) : nullptr;
      std::printf("[one-handed] %-17s -> buttons %04x stick %d,%d c-stick %d,%d L %d R %d (sdl %d err %d)\n", s.name,
                  st[0].button, st[0].stickX, st[0].stickY, st[0].substickX, st[0].substickY, st[0].triggerLeft,
                  st[0].triggerRight,
                  gp != nullptr && s.button >= 0 ? int(SDL_GetGamepadButton(gp, SDL_GamepadButton(s.button))) : -1,
                  int(st[0].err));
    } else if (at == 4) {
      set(false);
    }
  }
  std::fflush(stdout);
}

void draw(int) { testFrame(); }

void section(int) {
  static int sPort;
  static const char* sDone;
  const char* kPorts[] = {portTr("Player 1"), portTr("Player 2"), portTr("Player 3"), portTr("Player 4")};
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
  ImGui::Combo("##one_handed_player", &sPort, kPorts, 4);
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Left hand##one_handed"))) {
    sDone = apply(u32(sPort), kLeft) ? "Set for the left hand." : "No controller is this player.";
  }
  ImGui::SameLine();
  if (ImGui::Button(portTrLabel("Right hand##one_handed"))) {
    sDone = apply(u32(sPort), kRight) ? "Set for the right hand." : "No controller is this player.";
  }
  if (sDone != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr(sDone));
  }
}

// The Start menu's form (port/include/port/pause_forms.h): the player, then each hand
enum { kFormPlayer, kFormLeft, kFormRight };
int sFormPort;
constexpr const char* kFormPorts[] = {"Player 1", "Player 2", "Player 3", "Player 4"};

int formRows(PortPauseRow* out, int max) {
  if (max < 3) {
    return 0;
  }
  out[0] = portPauseRow("Player", "Whose controller is set: left and right change it.", kFormPorts[sFormPort], kFormPlayer);
  out[1] = portPauseRow("Left hand", "The stick moves, the D-pad is ABXY, LB is Z and LT is R.",
                        nullptr, kFormLeft, PORT_PAUSE_ROW_CONFIRM);
  out[2] = portPauseRow("Right hand", "The right stick moves; face buttons, RB and RT unchanged.",
                        nullptr, kFormRight, PORT_PAUSE_ROW_CONFIRM);
  return 3;
}

const char* formAct(int id, int dir) {
  if (id == kFormPlayer) {
    sFormPort = (sFormPort + (dir == 0 ? 1 : dir) + 4) % 4;
    return nullptr;
  }
  if (dir != 0) {
    return nullptr;
  }
  const bool left = id == kFormLeft;
  return apply(u32(sFormPort), left ? kLeft : kRight) ? (left ? "Set for the left hand." : "Set for the right hand.")
                                                     : "No controller is this player.";
}

const PortPauseForm kForm = {"Controls", "One-handed controls", formRows, formAct, PORT_PAUSE_FORM_OWN};

const bool sAdded = [] {
  portSettingsDeclare("hold_to_mash", 0, 0, 1);
  portPauseAddForm(&kForm);
  portSettingsAddSection(
      "Controls", "One-handed controls",
      "Everything on one half of the controller. Left hand: the stick moves, the D-pad is A, B, X and Y, the left "
      "bumper Z, the left trigger R, the stick pressed in L, Back Start. Right hand: the right stick moves, the face "
      "buttons, right bumper and trigger as they are, the right stick pressed in L. Change any button afterwards under "
      "Bindings; Reset all there undoes it. Back and the stick presses then play instead of opening "
      "Settings or fast-forwarding (the Start menu's Settings tab, F1 and Tab still do).",
      0, section, "one handed one-handed single hand left right accessibility disability layout preset remap");
  portSettingsAddToggle("Controls", "Hold instead of mashing",
                        "In the action commands that ask you to press a button again and again, holding it counts: "
                        "it presses about 12 times a second for you.",
                        PORT_SETTING_CHANGES_PLAY, "hold_to_mash",
                        "hold to mash mashing rapid fire turbo auto press action command accessibility one handed");
  portSettingsAddOverlay(draw, nullptr);
  return true;
}();

} // namespace
