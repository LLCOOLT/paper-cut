// Confirm and back in Paper Cut's own menus (port/include/port/menu_buttons.h): each
// controller's own (its labels, by kind), the same in the launcher, the settings
// page and the Start menu's Paper Cut pages; "Swap confirm and back" for the other
// convention.
#include "port/menu_buttons.h"
#include "port/bindings.h"
#include "port/companion_menu.h"
#include "port/menu_tap.h"
#include "port/settings.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <dolphin/pad.h>
#include <imgui.h>
#include <imgui_internal.h> // SetKeyOwner

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" int portInputActive(void); // port/include/port/platform.h (BOOL): a test's input script drives player 1
extern "C" int portInputHomePort(void); // input_pc.c: the keyboard's port (a seat swap's)

namespace {

int* sSwap = portSettingsDeclare("menu_swap_confirm", 0, 0, 1);
// the player's own confirm and back (Controls > Bindings): -1 as the controller has them, else a face button
// by place (kPlace); never the same one (portMenuSetButton keeps them apart)
int* sConfirmPlace = portSettingsDeclare("menu_confirm_pad", -1, -1, 2);
int* sBackPlace = portSettingsDeclare("menu_back_pad", -1, -1, 2);
constexpr int kPlace[3] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST};
SDL_JoystickID sLast; // the controller last pressed
int sPadInUse = -1;   // the last press a controller's (1), a key's or the mouse's (0); -1 none yet

bool nintendo(SDL_GamepadType t) {
  return t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
         t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR;
}

// a GameCube controller: SDL's kind, the Wii U adapter's, the NSO GameCube controller (Aurora's own table)
bool gameCube(SDL_Gamepad* p) {
  if (SDL_GetGamepadType(p) == SDL_GAMEPAD_TYPE_GAMECUBE) {
    return true;
  }
  const Uint16 v = SDL_GetGamepadVendor(p), pr = SDL_GetGamepadProduct(p);
  return v == 0x057E && (pr == 0x0337 || pr == 0x2073);
}

struct Roles {
  int confirm = SDL_GAMEPAD_BUTTON_SOUTH, back = SDL_GAMEPAD_BUTTON_EAST, third = SDL_GAMEPAD_BUTTON_WEST;
};

// a controller's confirm, back and third buttons: by its labels (A or cross confirms, B or circle
// backs), else by its kind; the bottom and right ones for any other
Roles rolesOf(SDL_Gamepad* p) {
  Roles r;
  if (p != nullptr) {
    if (gameCube(p)) {
      r = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_EAST};
    } else {
      int a = -1, b = -1;
      for (int f : {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST}) {
        const SDL_GamepadButtonLabel l = SDL_GetGamepadButtonLabel(p, SDL_GamepadButton(f));
        if (l == SDL_GAMEPAD_BUTTON_LABEL_A || l == SDL_GAMEPAD_BUTTON_LABEL_CROSS) {
          a = f;
        } else if (l == SDL_GAMEPAD_BUTTON_LABEL_B || l == SDL_GAMEPAD_BUTTON_LABEL_CIRCLE) {
          b = f;
        }
      }
      if (a >= 0 && b >= 0) {
        r.confirm = a;
        r.back = b;
        for (int f : {SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST}) {
          if (f != a && f != b) {
            r.third = f;
            break;
          }
        }
      } else if (nintendo(SDL_GetGamepadType(p))) {
        r = {SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST};
      }
    }
  }
  // the player's own choice, by place (the other of the two as the controller has it, unless that is the chosen
  // one: then the one left); the third is the face button left over
  const int c = *sConfirmPlace, b = *sBackPlace;
  if ((c >= 0 && c < 3) || (b >= 0 && b < 3)) {
    int confirm = c >= 0 && c < 3 ? kPlace[c] : r.confirm, back = b >= 0 && b < 3 ? kPlace[b] : r.back;
    if (confirm == back) {
      back = c >= 0 && c < 3 && b >= 0 && b < 3 ? -1 : back == r.confirm ? r.back : r.confirm;
      if (confirm == back || back < 0) {
        back = confirm == SDL_GAMEPAD_BUTTON_EAST ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
      }
    }
    r.confirm = confirm;
    r.back = back;
    for (int f : kPlace) {
      if (f != confirm && f != back) {
        r.third = f;
        break;
      }
    }
  }
  if (*sSwap != 0) {
    std::swap(r.confirm, r.back);
  }
  return r;
}

int roleOf(const Roles& r, int button) {
  return button == r.confirm ? PORT_MENU_CONFIRM
         : button == r.back  ? PORT_MENU_BACK
         : button == r.third ? PORT_MENU_THIRD
                             : PORT_MENU_NONE;
}

SDL_Gamepad* portPad(int port) {
  const s32 index = PADGetIndexForPort(static_cast<u32>(port));
  return index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
}

// the controller in hand: the last one pressed, else player 1's, else the first
SDL_Gamepad* inHand() {
  if (sLast != 0) {
    if (SDL_Gamepad* p = SDL_GetGamepadFromID(sLast)) {
      return p;
    }
  }
  if (SDL_Gamepad* p = portPad(0)) {
    return p;
  }
  int n = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&n);
  SDL_Gamepad* p = n > 0 ? SDL_GetGamepadFromID(ids[0]) : nullptr;
  SDL_free(ids);
  return p;
}

// --- The Start menu's pages: the game's A, B, X and Y as the controller's own ------------

constexpr u32 kFace[4] = {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y};

int faceIndex(u32 padButton) {
  for (int i = 0; i < 4; i++) {
    if (kFace[i] == padButton) {
      return i;
    }
  }
  return -1;
}

// to[i]: the page's button (0-3: A B X Y) that the game's button i is on the player's controller;
// false: as they are (no controller, or confirm and back not on two of the game's face buttons)
// (script: a test's input script's presses may be in the buttons mapped: they are the game's own, not a
// controller's, so as they are, else a controller left plugged in on the test machine turns the script's B
// into X; the menu layer moves only the controller's own presses, so it maps with a script running)
bool menuMap(int port, int to[4], bool script = true) {
  SDL_Gamepad* p = portPad(port);
  if (p == nullptr || (script && port == 0 && portInputActive())) {
    return false;
  }
  const Roles r = rolesOf(p);
  u32 count = 0;
  const PADButtonMapping* maps = PADGetButtonMappings(static_cast<u32>(port), &count);
  auto gameOf = [&](int native) {
    for (u32 i = 0; maps != nullptr && i < count; i++) {
      if (maps[i].nativeButton == static_cast<u32>(native) && faceIndex(maps[i].padButton) >= 0) {
        return faceIndex(maps[i].padButton);
      }
    }
    return -1;
  };
  const int c = gameOf(r.confirm), b = gameOf(r.back), t = gameOf(r.third);
  if (c < 0 || b < 0 || c == b) {
    return false;
  }
  bool srcUsed[4] = {}, dstUsed[4] = {};
  auto put = [&](int src, int dst) {
    to[src] = dst;
    srcUsed[src] = dstUsed[dst] = true;
  };
  put(c, 0);
  put(b, 1);
  if (t >= 0 && !srcUsed[t]) {
    put(t, 2);
  }
  for (int i = 0; i < 4; i++) { // the rest where they are, if they can be
    if (!srcUsed[i] && !dstUsed[i]) {
      put(i, i);
    }
  }
  for (int i = 0; i < 4; i++) { // then in turn
    for (int j = 0; j < 4 && !srcUsed[i]; j++) {
      if (!dstUsed[j]) {
        put(i, j);
      }
    }
  }
  for (int i = 0; i < 4; i++) {
    if (to[i] != i) {
      return true;
    }
  }
  return false;
}

// --- Dear ImGui ------------------------------------------------------------------------

constexpr ImGuiID kOwner = 0x50434D42; // ("PCMB": the controller's face buttons are Paper Cut's here)
bool sDown[2];                         // Space, Escape: put down by a controller
SDL_JoystickID sDownBy[2];             // its controller
int sDownButton[2] = {-1, -1};         // and its button
struct Deferred {
  int key; // 0 Space, 1 Escape
  bool down;
};
std::vector<Deferred> sDeferred; // the left face button's: given to Dear ImGui a frame late (portMenuUiEvent)

} // namespace

extern "C" int portMenuEventRole(const void* sdlEvent) {
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  if (e->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && e->type != SDL_EVENT_GAMEPAD_BUTTON_UP) {
    return PORT_MENU_NONE;
  }
  if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    sLast = e->gbutton.which;
  }
  SDL_Gamepad* p = SDL_GetGamepadFromID(e->gbutton.which);
  return roleOf(rolesOf(p), e->gbutton.button);
}

extern "C" int portMenuRoleButton(int role, unsigned id) {
  SDL_Gamepad* p = id != 0 ? SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(id)) : inHand();
  const Roles r = rolesOf(p);
  return role == PORT_MENU_CONFIRM ? r.confirm : role == PORT_MENU_BACK ? r.back : r.third;
}

extern "C" unsigned portMenuPadId(void) {
  SDL_Gamepad* p = inHand();
  return p != nullptr ? static_cast<unsigned>(SDL_GetGamepadID(p)) : 0u;
}

extern "C" int portMenuSwapped(void) { return *sSwap; }

extern "C" void portMenuSetSwapped(int on) {
  *sSwap = on != 0;
  portSettingsSave();
}

extern "C" int portMenuChosenPlace(int role) {
  return role == PORT_MENU_CONFIRM ? *sConfirmPlace : role == PORT_MENU_BACK ? *sBackPlace : -1;
}

extern "C" void portMenuSetPlace(int role, int place) {
  if (role != PORT_MENU_CONFIRM && role != PORT_MENU_BACK) {
    return;
  }
  int* mine = role == PORT_MENU_CONFIRM ? sConfirmPlace : sBackPlace;
  int* other = role == PORT_MENU_CONFIRM ? sBackPlace : sConfirmPlace;
  place = place >= 0 && place < 3 ? place : -1;
  if (place >= 0 && *other == place) {
    *other = *mine; // (the two change places; -1: the other as the controller has it, kept apart in rolesOf)
  }
  *mine = place;
  portSettingsSave();
}

extern "C" void portMenuResetButtons(void) {
  *sConfirmPlace = *sBackPlace = -1;
  *sSwap = 0;
  portSettingsSave();
}

extern "C" unsigned portMenuButtonsAsOwn(int port, unsigned buttons) {
  int to[4];
  // (the second screen's menu: its presenter's buttons are the game's own, whatever is in hand here; the
  // menu layer on: the pads' buttons are the controller's own already)
  if ((buttons & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y)) == 0 || gPortCompanionMenu != 0 ||
      portMenuLayerOn(port) || !menuMap(port, to)) {
    return buttons;
  }
  const unsigned virt = portMenuVirtualHeld(port);
  unsigned out = buttons & ~(PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y);
  for (int i = 0; i < 4; i++) {
    if ((buttons & kFace[i]) == 0) {
      continue;
    }
    // a key held for it: the keyboard's, as it is (the keyboard's player's keys: player 1's until a seat
    // swap moves them); the touch controls', the mouse's and Esc's B as they are too
    out |= (port == portInputHomePort() && portBindingsGameKeyHeld(i)) || (virt & kFace[i]) != 0 ? kFace[i]
                                                                                                 : kFace[to[i]];
  }
  return out;
}

// a menu reading its buttons: marked up (the layer from the next frame), and its buttons as the controller's
// own now (this frame's by hand the first frame, the layer's after)
extern "C" unsigned portMenuButtons(int port, unsigned buttons) {
  if (port >= 0 && port < 4) {
    portMenuMark(1u << port);
  }
  return portMenuButtonsAsOwn(port, buttons);
}

extern "C" int portMenuLayerMap(int port, int to[4]) {
  if (!portMenuLayerOn(port) || gPortCompanionMenu != 0) {
    return 0;
  }
  return menuMap(port, to, false) ? 1 : 0;
}

namespace {
unsigned sLatch[4];   // the controller's face buttons held as the layer turned on or off: nothing until let go
unsigned sChanged[4]; // the game's face buttons the layer moves now (0: off, or nothing moved)
} // namespace

extern "C" void portMenuPadLayer(PADStatus* pad) {
  constexpr unsigned kFaces = PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y;
  for (int port = 0; port < 4; port++) {
    int to[4] = {0, 1, 2, 3};
    const bool on = portMenuLayerMap(port, to) != 0;
    unsigned changed = 0;
    for (int i = 0; on && i < 4; i++) {
      if (to[i] != i) {
        changed |= kFace[i];
      }
    }
    // the controller's own presses: the game's face buttons whose controller button is held on it now (not a
    // key's, the touch controls', the mouse's and Esc's B, or a test script's)
    unsigned own = 0;
    if (SDL_Gamepad* p = portPad(port)) {
      u32 count = 0;
      const PADButtonMapping* maps = PADGetButtonMappings(static_cast<u32>(port), &count);
      for (u32 m = 0; maps != nullptr && m < count; m++) {
        const int i = faceIndex(maps[m].padButton);
        if (i >= 0 && maps[m].nativeButton < SDL_GAMEPAD_BUTTON_COUNT &&
            SDL_GetGamepadButton(p, static_cast<SDL_GamepadButton>(maps[m].nativeButton))) {
          own |= kFace[i];
        }
      }
    }
    own &= pad[port].button & kFaces;
    const unsigned virt = portMenuVirtualHeld(port);
    for (int i = 0; i < 4; i++) {
      if ((port == portInputHomePort() && portBindingsGameKeyHeld(i)) || (virt & kFace[i]) != 0) {
        own &= ~kFace[i];
      }
    }
    if (changed != sChanged[port]) {
      sLatch[port] |= own & (changed | sChanged[port]);
      sChanged[port] = changed;
    }
    sLatch[port] &= own;
    if (changed == 0 && sLatch[port] == 0) {
      continue;
    }
    unsigned out = pad[port].button & ~own;
    for (int i = 0; i < 4; i++) {
      if ((own & kFace[i]) != 0 && (sLatch[port] & kFace[i]) == 0) {
        out |= kFace[to[i]];
      }
    }
    pad[port].button = static_cast<u16>(out);
  }
}

extern "C" const char* portMenuText(const char* text) {
  int to[4];
  // (the menu layer on: the game's {A} to {Y} pictures are the controller's own buttons there already)
  if (text == nullptr || std::strchr(text, '{') == nullptr || portMenuLayerPictures(0) || !menuMap(0, to)) {
    return text;
  }
  static const char kNames[4] = {'A', 'B', 'X', 'Y'};
  int from[4]; // the page's button i is the game's from[i] (its icon shows the controller's button)
  for (int i = 0; i < 4; i++) {
    from[to[i]] = i;
  }
  std::string s(text);
  bool changed = false;
  for (size_t at = s.find('{'); at != std::string::npos && at + 2 < s.size(); at = s.find('{', at + 1)) {
    if (s[at + 2] != '}') {
      continue;
    }
    for (int i = 0; i < 4; i++) {
      if (s[at + 1] == kNames[i] && from[i] != i) {
        s[at + 1] = kNames[from[i]];
        changed = true;
        break;
      }
    }
  }
  if (!changed) {
    return text;
  }
  // kept for the run: the help texts are few (a node's c_str stays put as the map grows)
  static std::unordered_map<std::string, std::string> sMade;
  if (sMade.size() > 512) {
    sMade.clear();
  }
  std::string key = std::string(1, char('0' + from[0])) + char('0' + from[1]) + char('0' + from[2]) + text;
  return sMade.emplace(std::move(key), std::move(s)).first->second.c_str();
}

namespace {
// The left stick moves the cursor as the D-pad does (Dear ImGui's gamepad navigation moves only with the
// D-pad; its stick only scrolls): pushed past a dead zone, the stick's main direction is an arrow key held,
// so the first move comes at once and Dear ImGui repeats it while held. Not while the same controller's D-pad
// is held (no double moves), nor while the page doesn't have the controls. The right stick still scrolls.
void stickAsDpad(ImGuiIO& io) {
  static const ImGuiKey kArrows[4] = {ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow};
  static bool sHeld[4];
  constexpr float kPress = 0.5f, kLetGo = 0.35f; // (apart: a stick resting near the edge doesn't flicker)
  bool want[4] = {};
  if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0 && !io.WantTextInput && !portBindingsCapturing()) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    float bx = 0.f, by = 0.f;
    for (int i = 0; ids != nullptr && i < count; i++) {
      SDL_Gamepad* p = SDL_GetGamepadFromID(ids[i]);
      if (p == nullptr || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
          SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
          SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
        continue;
      }
      const float x = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f;
      const float y = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
      if (x * x + y * y > bx * bx + by * by) {
        bx = x;
        by = y;
      }
    }
    SDL_free(ids);
    // the main direction only (a diagonal push moves one way, not two)
    const bool horizontal = std::fabs(bx) >= std::fabs(by);
    const float v = horizontal ? bx : by;
    const int dir = horizontal ? (v < 0.f ? 0 : 1) : (v < 0.f ? 2 : 3);
    const float need = sHeld[dir] ? kLetGo : kPress;
    if (std::fabs(v) >= need) {
      want[dir] = true;
    }
  }
  for (int d = 0; d < 4; d++) {
    if (want[d] != sHeld[d]) {
      sHeld[d] = want[d];
      io.AddKeyEvent(kArrows[d], want[d]);
    }
  }
}

// Dear ImGui also scrolls the window with the left stick (its NavUpdate, this frame's, already done): with
// the stick moving the cursor that scroll fights it, overriding the scroll that brings the next row in view,
// so the page runs off ahead of the cursor (a busy machine's long frames more so) and a row moved off can't
// be moved back to. Undone where the page has rows to move between, the cursor's row kept in view instead.
void stickNoScroll(ImGuiIO& io) {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  ImGuiWindow* w = g.NavWindow;
  if (w == nullptr || (w->Flags & ImGuiWindowFlags_NoNavInputs) || g.NavWindowingTarget != nullptr ||
      (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0 || (io.BackendFlags & ImGuiBackendFlags_HasGamepad) == 0 ||
      w->DC.NavLayersActiveMask == 0) { // (no rows: the stick's scroll is the only one)
    return;
  }
  const ImVec2 d = ImGui::GetKeyMagnitude2d(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
                                            ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown);
  if (d.x == 0.f && d.y == 0.f) {
    return;
  }
  if (d.y != 0.f) {
    w->ScrollTarget.y = FLT_MAX;
  }
  if (d.x != 0.f && w->ScrollbarX) {
    w->ScrollTarget.x = FLT_MAX;
  }
  if (g.NavId != 0 && g.NavLayer == ImGuiNavLayer_Main) {
    ImGui::ScrollToRectEx(w, ImGui::WindowRectRelToAbs(w, w->NavRectRel[ImGuiNavLayer_Main]),
                          ImGuiScrollFlags_KeepVisibleEdgeX | ImGuiScrollFlags_KeepVisibleEdgeY);
  }
}
} // namespace

extern "C" void portMenuUiFrame(void) {
  ImGuiContext* ctx = ImGui::GetCurrentContext();
  if (ctx == nullptr) {
    return;
  }
  static ImGuiContext* sAll;
  if (sAll != ctx && ImGui::GetIO().BackendPlatformUserData != nullptr) {
    // every controller moves the page (Dear ImGui's default: the first SDL lists, which on Linux can
    // be one not in hand, as Steam's own)
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoAll);
    sAll = ctx;
  }
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigNavSwapGamepadButtons = false;
  // confirm and back come as Space and Escape (portMenuUiEvent): the face buttons by place are
  // kept from the navigation, and from its window menu (the left button held)
  for (ImGuiKey k : {ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft}) {
    ImGui::SetKeyOwner(k, kOwner);
  }
  // and its window menu started anyway (a frame drawn without this one, as the launcher's captures make):
  // left, else its release puts the cursor on the launcher's tabs (the third button there clears a binding)
  ImGuiContext& g = *ctx;
  if (g.NavWindowingTarget != nullptr && g.NavInputSource == ImGuiInputSource_Gamepad) {
    g.NavWindowingTarget = nullptr;
    g.NavWindowingToggleLayer = false;
  }
  for (const Deferred& d : sDeferred) { // (the left face button's confirm or back, now that it is undone)
    if (d.down) {
      ImGui::SetNavCursorVisible(true);
    }
    io.AddKeyEvent(d.key == 0 ? ImGuiKey_Space : ImGuiKey_Escape, d.down);
  }
  sDeferred.clear();
  stickNoScroll(io);
  stickAsDpad(io);
}


extern "C" int portMenuPadInUse(void) { return sPadInUse >= 0 ? sPadInUse : SDL_HasGamepad() ? 1 : 0; }

extern "C" void portMenuUiEvent(const void* sdlEvent) {
  const SDL_Event* ev = static_cast<const SDL_Event*>(sdlEvent);
  if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_FINGER_DOWN) {
    sPadInUse = 0;
  } else if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
             (ev->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && (ev->gaxis.value > 16000 || ev->gaxis.value < -16000))) {
    sPadInUse = 1;
  }
  const int role = portMenuEventRole(sdlEvent);
  static const bool sTrace = SDL_getenv("TTYD_PADBIND_TEST") != nullptr;
  if (sTrace && (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev->type == SDL_EVENT_GAMEPAD_BUTTON_UP)) {
    std::printf("[padbind] event: button %d %s, role %d, page nav %d\n", int(ev->gbutton.button),
                ev->gbutton.down ? "down" : "up", role,
                ImGui::GetCurrentContext() != nullptr && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0);
    std::fflush(stdout);
  }
  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }
  // a button let go that put Space or Escape down: up again, whatever it is now (its press changed confirm and
  // back: Swap confirm and back, Menu confirm), else the key stays held and the next press of it is lost
  if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
    for (int k = 0; k < 2; k++) {
      if (sDown[k] && sDownBy[k] == ev->gbutton.which && sDownButton[k] == ev->gbutton.button &&
          role != (k == 0 ? PORT_MENU_CONFIRM : PORT_MENU_BACK)) {
        sDown[k] = false;
        ImGui::GetIO().AddKeyEvent(k == 0 ? ImGuiKey_Space : ImGuiKey_Escape, false);
      }
    }
  }
  if (role != PORT_MENU_CONFIRM && role != PORT_MENU_BACK) {
    return;
  }
  const SDL_Event* e = static_cast<const SDL_Event*>(sdlEvent);
  const bool down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  const int i = role == PORT_MENU_CONFIRM ? 0 : 1;
  ImGuiIO& io = ImGui::GetIO();
  if (down) {
    sDownBy[i] = e->gbutton.which;
    sDownButton[i] = e->gbutton.button;
  }
  if (down && (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) {
    return; // the page doesn't have the controls (the game does, or a binding is being asked for)
  }
  if (!down && !sDown[i]) {
    return;
  }
  sDown[i] = down;
  if (e->gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
    // the left face button is Dear ImGui's window menu: its press starts the window switching whatever owns it
    // (NavUpdateWindowing asks for any owner), and while that is on nothing is activated, so a confirm there
    // would be lost in its first frame. Its Space or Escape comes a frame later (portMenuUiFrame), after the
    // switching is undone
    sDeferred.push_back({i, down});
    return;
  }
  if (down) {
    // the cursor shown, as any controller press shows it (Space alone wouldn't, and a hidden one does nothing:
    // a question just opened after a binding was asked for)
    ImGui::SetNavCursorVisible(true);
  }
  io.AddKeyEvent(i == 0 ? ImGuiKey_Space : ImGuiKey_Escape, down);
}

extern "C" int portMenuUiPressed(int role) {
  const int b = portMenuRoleButton(role, 0);
  const ImGuiKey k = b == SDL_GAMEPAD_BUTTON_SOUTH  ? ImGuiKey_GamepadFaceDown
                     : b == SDL_GAMEPAD_BUTTON_EAST ? ImGuiKey_GamepadFaceRight
                     : b == SDL_GAMEPAD_BUTTON_WEST ? ImGuiKey_GamepadFaceLeft
                                                    : ImGuiKey_GamepadFaceUp;
  return ImGui::IsKeyPressed(k, false) ? 1 : 0;
}
