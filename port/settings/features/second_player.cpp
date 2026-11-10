// Co-op, up to 4 players (Multiplayer tab, key second_player; port/src/common/second_player_pc.c): player 2
// plays the partner (Luigi in Brothers mode), players 3 and 4 companions, in battle and in the field.
//
// Joining: a controller is player 1's (a handheld's built-in controls, the
// first controller paired: the touch controls hide for it) until another
// controller's Start makes that one the next player, in play or on this page; never
// by itself. The Players section below the switch shows all four players (who each plays, their
// controller; online, a guest's name), starts and stops co-op, picks player 2's, 3's or 4's controller by
// a press (any controller, player 1's too while player 1 plays on the keyboard or the touch screen), and
// lets each go. Aurora keeps which controller is in
// which port (saved with its mappings); a seat is given by Start or a pick.
#include "port/menu_buttons.h"
#include "port/brothers.h"
#include "port/pause_forms.h"
#include "port/together.h"
#include "port/players.h"
#include "port/pm64.h"
#include "port/statesync.h"
#include "port/player_menu.h"
#include "port/settings.h"
#include "port/tr.h"
#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" int portInputHomePort(void); // input_pc.c: the keyboard's and touch controls' port (a seat swap's)

#ifdef PORT_TOUCH_OVERLAY
extern "C" int portTouchPlayerOne(void); // port/touch/touch_overlay.cpp
#else
static int portTouchPlayerOne(void) { return 0; } // (no touch controls in this build)
#endif

namespace {
const char* seatTestSerial(SDL_Gamepad* pad);
}

// the device as itself: its model (GUID), serial and path (a USB socket or a Bluetooth address: two pads of a
// kind without serials, as most are, still differ there unless one replaced the other in the same socket)
extern "C" void portPadIdentity(int port, char* out, int size) {
  const s32 index = port >= 0 ? PADGetIndexForPort(static_cast<u32>(port)) : -1;
  SDL_Gamepad* pad = index >= 0 ? PADGetSDLGamepadForIndex(static_cast<u32>(index)) : nullptr;
  if (size <= 0) {
    return;
  }
  out[0] = '\0';
  if (pad != nullptr) {
    char guid[64];
    const char* serial = SDL_GetGamepadSerial(pad);
    const char* path = SDL_GetGamepadPath(pad);
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(SDL_GetGamepadID(pad)), guid, sizeof(guid));
    if (serial == nullptr || serial[0] == '\0') {
      serial = seatTestSerial(pad);
    }
    std::snprintf(out, static_cast<size_t>(size), "%s/%s/%s", guid, serial != nullptr ? serial : "",
                  path != nullptr ? path : "");
  }
}

namespace {

int* sOn;
int sChoosing; // 0, or the port (1-3: players 2-4) the next A or Start on any controller gives its controller

// Aurora's index of the controller an SDL event came from (-1 none)
s32 indexOf(SDL_JoystickID which) {
  SDL_Gamepad* pad = SDL_GetGamepadFromID(which);
  for (u32 i = 0; pad != nullptr && i < PADCount(); i++) {
    if (PADGetSDLGamepadForIndex(i) == pad) {
      return static_cast<s32>(i);
    }
  }
  return -1;
}

// the controller made player port + 1 (port 1-3), on purpose: it plays now, no Start needed
void makePlayer(s32 index, int port) {
  int was = -1; // (another player's controller chosen: that seat is let go, its controller now this one's)
  for (int p = 1; p < 4; p++) {
    if (p != port && PADGetIndexForPort(static_cast<u32>(p)) == index) {
      was = p;
    }
  }
  PADSetPortForIndex(static_cast<u32>(index), static_cast<u32>(port));
  PADSerializeMappings();
  if (was > 0) {
    portCoopSeatLetGo(was);
  }
  portCoopSeatChosen(port);
  std::printf("[coop] player %d: %s (%s)\n", port + 1, controllerName(static_cast<u32>(index)).c_str(),
              controllerDriverName(static_cast<u32>(index)).c_str());
  std::fflush(stdout);
}

// Whether player 1 last played on port 0's controller (false: on the keyboard or the touch screen, which
// share port 0 with it): only then is that controller player 1's own
bool sPortZeroPadLast = true;

void notePlayerOneDevice(const SDL_Event* e) {
  switch (e->type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_FINGER_DOWN:
    // only while the keyboard and the touch controls are player 1's (a seat swap moves them to player 2),
    // and a key's first press, not its repeats. The mouse never plays: a click (on this page's row, say)
    // says nothing about which player 1 plays with
    if (portInputHomePort() == 0 && (e->type != SDL_EVENT_KEY_DOWN || !e->key.repeat)) {
      sPortZeroPadLast = false;
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
    const s32 index = indexOf(e->gbutton.which);
    if (index >= 0 && index == PADGetIndexForPort(0)) {
      sPortZeroPadLast = true;
    }
    break;
  }
  default:
    break;
  }
}

int choosingEvent(const SDL_Event* e, const void* ev);

int inputEvent(const void* ev) {
  const SDL_Event* e = static_cast<const SDL_Event*>(ev);
  static const bool sLog = SDL_getenv("TTYD_SEATS_TEST") != nullptr;
  if (sLog && e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    std::printf("[seats] controller %d (index %d): %s down%s\n", static_cast<int>(e->gbutton.which),
                static_cast<int>(indexOf(e->gbutton.which)),
                SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(e->gbutton.button)),
                sChoosing ? " (choosing)" : "");
    std::fflush(stdout);
  }
  const int taken = choosingEvent(e, ev);
  notePlayerOneDevice(e); // (after: the press being judged is no say on who player 1 was)
  return taken;
}

int choosingEvent(const SDL_Event* e, const void* ev) {
  // the game's A (the bottom button), the controller's own confirm (menu_buttons.h: a Switch's A) or Start
  if (e->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
      (e->gbutton.button != SDL_GAMEPAD_BUTTON_SOUTH && portMenuEventRole(ev) != PORT_MENU_CONFIRM &&
       e->gbutton.button != SDL_GAMEPAD_BUTTON_START)) {
    return 0;
  }
  const s32 index = indexOf(e->gbutton.which);
  if (index < 0) {
    return 0;
  }
  if (sChoosing) {
    // player 1's own controller is never taken from Mario: its press goes on to the menu (the Start
    // menu row's A again leaves the choice; the page has Cancel). But the controller on port 0 isn't
    // player 1's when player 1 plays on the keyboard or the touch screen (the first controller always
    // goes on port 0): then choosing it moves it to player 2, as before
    if (index == PADGetIndexForPort(0) && sPortZeroPadLast && !portTouchPlayerOne()) {
      return 0;
    }
    const int port = sChoosing;
    sChoosing = 0;
    makePlayer(index, port);
    return 1; // the press chose; it does nothing else
  }
  // A controller is player 1's (port 0: a handheld's built-in controls, the first controller paired, where
  // the touch controls are; they hide for it) and never moves to another seat by itself. Another player
  // joins only with Start on another controller: in ports 1-3 the game's join rule takes that Start
  // (player_menu_pc.c's joinRule: the next free seat). A controller in no port at all (let go, set to Not
  // playing, past the fourth) is one the game can't read: its Start puts it in the first free port, where
  // the same Start (still held as the game reads the controllers) joins it.
  if (e->gbutton.button != SDL_GAMEPAD_BUTTON_START) {
    return 0;
  }
  for (u32 port = 0; port < 4; port++) {
    if (PADGetIndexForPort(port) == index) {
      return 0; // (in a port: its own seat, or the join rule's)
    }
  }
  for (u32 port = 1; port < 4; port++) {
    // (not into the keyboard's seat: a seat swap's keyboard player there plays it)
    if (PADGetIndexForPort(port) < 0 && portInputHomePort() != static_cast<int>(port)) {
      PADSetPortForIndex(static_cast<u32>(index), port);
      PADSerializeMappings();
      if (gPortTogetherJoinOpen && sOn != nullptr && !*sOn) {
        *sOn = 1; // (the Multiplayer page: joining there turns co-op on, as before)
        portSettingsSave();
      }
      std::printf("[coop] Start on %s (in no port): to port %u to join\n",
                  controllerName(static_cast<u32>(index)).c_str(), port + 1);
      std::fflush(stdout);
      return 0;
    }
  }
  return 0;
}

// a player's controller as players know it (button_glyphs.h), and the driver's name as the line's tooltip
std::string nameOf(u32 port) {
  const s32 index = PADGetIndexForPort(port);
  return index >= 0 ? controllerName(static_cast<u32>(index)) : std::string(portTr("a controller"));
}

// the same for the Start menu's rows, which the game's font draws as they are: its encoding, not UTF-8
std::string nameOfGame(u32 port) {
  const s32 index = PADGetIndexForPort(port);
  return index >= 0 ? controllerNameGame(static_cast<u32>(index)) : std::string(portTrGame("a controller"));
}

void driverTooltip(u32 port) {
  const s32 index = PADGetIndexForPort(port);
  if (index >= 0 && ImGui::IsItemHovered()) {
    const std::string driver = controllerDriverName(static_cast<u32>(index));
    if (!driver.empty() && driver != controllerName(static_cast<u32>(index))) {
      ImGui::SetTooltip("%s", driver.c_str());
    }
  }
}

// --- the four players (seat s: 1-4; port s - 1) ---

// online, the seat's player is another game's (a guest's, or the host's on a guest's game): 1 and their name
bool onlineSeat(int seat, char* name, size_t size) {
  const int player = portStateSyncOn() ? portStateSyncSeatPlayer(seat) : 0;
  if (player <= 0 || player == ssNetMe()) {
    return false;
  }
  portStateSyncPlayerNameShown(player, name, size);
  if (name[0] == '\0') {
    std::snprintf(name, size, "%d", player);
  }
  return true;
}

// what seat plays, in English (translated by the caller): Mario, Luigi, the partner's or a companion's name
const char* partOf(int seat, bool game = false) {
  static const char* const kNames[8] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
  if (seat == 1) {
    return "Mario";
  }
  const int character = portCoopCharacterOf(seat - 1);
  if (character < 0) {
    return "Luigi";
  }
  const int pm64 = portCoopPm64Of(seat - 1);
  if (pm64 > 0 && (game ? portPm64PartnerGameName(pm64) : portPm64PartnerName(pm64)) != nullptr) {
    return game ? portPm64PartnerGameName(pm64) : portPm64PartnerName(pm64);
  }
  if (character >= 1 && character <= 7) {
    return kNames[character];
  }
  if (seat == 2) {
    return portBrothersSetting() ? "Luigi" : "the partner";
  }
  return "a companion";
}

// the seat's controller is there and plays (a controller in a port that hasn't joined is nobody yet: SDL put
// it there, its Start makes it a player)
bool seated(int seat) {
  return seat == 1 ? PADGetIndexForPort(0) >= 0
                   : PADGetIndexForPort(static_cast<u32>(seat - 1)) >= 0 && !portCoopUnseated(seat - 1);
}

constexpr const char* kChooseLabel[4] = {"", "Choose player 2's controller", "Choose player 3's controller",
                                         "Choose player 4's controller"};
constexpr const char* kChoosingLabel[4] = {"", "Press {A} on player 2's controller...",
                                           "Press {A} on player 3's controller...",
                                           "Press {A} on player 4's controller..."};
constexpr const char* kLetGoLabel[4] = {"", "Let player 2 go", "Let player 3 go", "Let player 4 go"};

void letGo(int port) {
  PADClearPort(static_cast<u32>(port));
  PADSerializeMappings();
  portCoopSeatLetGo(port);
  std::printf("[coop] player %d let go\n", port + 1);
  std::fflush(stdout);
}

void playersSection(int) {
  char online[64];
  if (ImGui::Button(portTrLabel(*sOn ? "Stop co-op##coop_on" : "Start co-op##coop_on"))) {
    *sOn = !*sOn;
    portSettingsSave();
  }
  for (int seat = 1; seat <= 4; seat++) {
    const int port = seat - 1;
    const std::string who = portTr(partOf(seat));
    ImGui::PushID(seat);
    ImGui::Spacing();
    if (onlineSeat(seat, online, sizeof(online))) {
      ImGui::Text(portTr("Player %d (%s): online: %s"), seat, who.c_str(), online);
    } else if (seated(seat)) {
      ImGui::Text(portTr("Player %d (%s): %s"), seat, who.c_str(), nameOf(static_cast<u32>(port)).c_str());
      driverTooltip(static_cast<u32>(port));
    } else if (portInputHomePort() == port) {
      // (player 1 on the keyboard or the touch screen; or a seat swap took the keyboard to this seat)
      ImGui::Text(portTr("Player %d (%s): %s"), seat, who.c_str(), portTr("Keyboard"));
    } else if (!*sOn) {
      ImGui::TextDisabled(portTr("Player %d: nobody (co-op is off)."), seat);
    } else {
      ImGui::TextDisabled(portTr("Player %d: nobody yet. Press {START} on another controller to join."), seat);
    }
    if (seat >= 2 && !onlineSeat(seat, online, sizeof(online))) {
      ImGui::Indent();
      if (sChoosing == port) {
        ImGui::TextUnformatted(portTr(kChoosingLabel[port]));
        ImGui::SameLine();
        if (ImGui::SmallButton(portTrLabel("Cancel##coop"))) {
          sChoosing = 0;
        }
      } else {
        if (ImGui::SmallButton(portTr(kChooseLabel[port]))) {
          sChoosing = port;
        }
        if (PADGetIndexForPort(static_cast<u32>(port)) >= 0) {
          ImGui::SameLine();
          if (ImGui::SmallButton(portTr(kLetGoLabel[port]))) {
            letGo(port);
          }
        }
      }
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Players 2 to 4 each play on a controller of their own: Mario keeps his. Each "
                                  "player's buttons are set under Bindings."));
  ImGui::PopStyleColor();
}

// The Start menu's form (port/include/port/pause_forms.h): who plays, and players 2-4's controllers. Ids: the
// seat's row 0-3, its Choose 4 + port, its Let go 8 + port
enum { kSeat = 0, kChoose = 4, kLetGo = 8 };

int formRows(PortPauseRow* out, int max) {
  int n = 0;
  if (max < 12) {
    return 0;
  }
  for (int seat = 1; seat <= 4; seat++) {
    const int port = seat - 1;
    PortPauseRow row = portPauseRow(nullptr, nullptr, nullptr, kSeat + port);
    std::snprintf(row.text, sizeof(row.text), portTrGame("Player %d: %s"), seat, portTrGame(partOf(seat, true)));
    if (seated(seat)) {
      row.note = seat == 1 ? "Player 1's controller, or the keyboard." : "This player's controller.";
      std::snprintf(row.value, sizeof(row.value), "%s", nameOfGame(static_cast<u32>(port)).c_str());
    } else if (portInputHomePort() == port) {
      row.note = seat == 1 ? "Player 1's controller, or the keyboard." : "This player's controller.";
      row.valueTr = "The keyboard"; // (player 1's; or a seat swap took the keyboard there)
    } else {
      row.note = *sOn ? "Nobody yet: press {START} on another controller to join." : "Co-op is off.";
      row.valueTr = "Nobody";
    }
    out[n++] = row;
    if (seat == 1) {
      continue;
    }
    out[n++] = portPauseRow(sChoosing == port ? kChoosingLabel[port] : kChooseLabel[port],
                            "Press {A} on another controller than player 1's. {A} on player 1's leaves this.",
                            nullptr, kChoose + port);
    if (PADGetIndexForPort(static_cast<u32>(port)) >= 0) {
      out[n++] = portPauseRow(kLetGoLabel[port], "This player's controller plays as nobody until it joins again.",
                              nullptr, kLetGo + port, PORT_PAUSE_ROW_CONFIRM);
    }
  }
  return n;
}

const char* formAct(int id, int dir) {
  if (dir != 0) {
    return nullptr;
  }
  if (id > kChoose && id < kChoose + 4) {
    const int port = id - kChoose;
    int pads = 0;
    SDL_free(SDL_GetGamepads(&pads));
    if (sChoosing != port && pads == 0) {
      return "Connect a controller first."; // (none: nothing could answer)
    }
    sChoosing = sChoosing == port ? 0 : port; // (A again leaves it)
  } else if (id > kLetGo && id < kLetGo + 4) {
    letGo(id - kLetGo);
  }
  return nullptr;
}

const PortPauseForm kForm = {"Multiplayer", "Players", formRows, formAct, PORT_PAUSE_FORM_OWN};

// Tests (TTYD_SEATS_TEST, docs/PC_PORT.md): virtual controllers plugged in and pressed through SDL, as real
// ones are (their events come through inputEvent and the touch controls'), and who plays printed. Steps,
// comma-separated, each at a game frame: "N:attach@F" plugs in virtual controller N (1-4), "N:BUTTON@F+H"
// holds its BUTTON (SDL's name: a, b, x, y, start, back, dpup...) from frame F for H frames (default 6),
// "N:detach@F" unplugs it, "report@F" prints "[seats] F: ..." (each port's controller, whether player 2 plays,
// how many play, and whether player 1 plays on the touch screen; a second line ports 3 and 4), "choose@F" (or
// "choose3@F", "choose4@F") presses Players' Choose player N's controller, "letgo2@F" to "letgo4@F" its Let
// player N go. Pads 2-4 are one model (one GUID, as two pads of a kind are), each with a serial of its own
// (portPadIdentity's "seat pad N").
struct SeatStep {
  int pad, frame, hold;
  int button; // -1 attach, -2 report, -3 choose, -4 detach
};
constexpr int kSeatPads = 4;
SDL_Joystick* sSeatJoy[kSeatPads];

// a test's virtual controller N: "seat pad N", the serial SDL's virtual pads can't have (nullptr: not one)
const char* seatTestSerial(SDL_Gamepad* pad) {
  static char out[kSeatPads][16];
  for (int k = 0; k < kSeatPads; k++) {
    if (sSeatJoy[k] != nullptr && pad != nullptr && SDL_GetGamepadFromID(SDL_GetJoystickID(sSeatJoy[k])) == pad) {
      std::snprintf(out[k], sizeof(out[k]), "seat pad %d", k + 1);
      return out[k];
    }
  }
  return nullptr;
}

const char* seatPadOf(u32 port) {
  static char out[4][32];
  const s32 index = PADGetIndexForPort(port);
  if (index < 0) {
    return "none";
  }
  SDL_Gamepad* gp = PADGetSDLGamepadForIndex(static_cast<u32>(index));
  for (int k = 0; k < kSeatPads; k++) {
    if (sSeatJoy[k] != nullptr && gp != nullptr &&
        SDL_GetGamepadFromID(SDL_GetJoystickID(sSeatJoy[k])) == gp) {
      std::snprintf(out[port], sizeof(out[port]), "pad %d", k + 1);
      return out[port];
    }
  }
  return "another controller";
}

void seatsTestFrame() {
  static const char* spec = SDL_getenv("TTYD_SEATS_TEST");
  static std::vector<SeatStep> steps;
  static bool parsed;
  static int frame;
  if (spec == nullptr) {
    return;
  }
  if (!parsed) {
    parsed = true;
    std::string all(spec);
    size_t at = 0;
    while (at <= all.size()) {
      const size_t end = all.find(',', at) == std::string::npos ? all.size() : all.find(',', at);
      const std::string one = all.substr(at, end - at);
      at = end + 1;
      SeatStep s{0, 0, 6, -2};
      const size_t colon = one.find(':'), amp = one.find('@');
      if (amp == std::string::npos) {
        continue;
      }
      std::string what = one.substr(0, amp);
      const std::string when = one.substr(amp + 1);
      s.frame = std::atoi(when.c_str());
      if (when.find('+') != std::string::npos) {
        s.hold = std::atoi(when.c_str() + when.find('+') + 1);
      }
      if (colon != std::string::npos && colon < amp) {
        s.pad = std::atoi(what.substr(0, colon).c_str());
        what = what.substr(colon + 1);
        if (s.pad < 1 || s.pad > kSeatPads) {
          continue;
        }
        if (what == "attach") {
          s.button = -1;
        } else if (what == "detach") {
          s.button = -4;
        } else {
          s.button = SDL_GetGamepadButtonFromString(what.c_str());
          if (s.button == SDL_GAMEPAD_BUTTON_INVALID) {
            std::printf("[seats] test: no button \"%s\"\n", what.c_str());
            continue;
          }
        }
      } else if (what == "choose" || what == "choose2" || what == "choose3" || what == "choose4") {
        s.button = -3;
        s.pad = what.size() > 6 ? what[6] - '1' : 1; // (the port whose controller is chosen: players 2-4)
      } else if (what == "letgo2" || what == "letgo3" || what == "letgo4") {
        s.button = -5;
        s.pad = what[5] - '1';
      } else if (what != "report") {
        continue;
      }
      steps.push_back(s);
    }
    std::printf("[seats] test: %d steps\n", static_cast<int>(steps.size()));
    std::fflush(stdout);
  }
  frame++;
  for (const SeatStep& s : steps) {
    if (s.button == -3 && frame == s.frame) {
      sChoosing = s.pad; // (as the Players section's Choose player N's controller)
      std::printf("[seats] %d: choosing player %d's controller\n", frame, s.pad + 1);
      std::fflush(stdout);
    } else if (s.button == -5 && frame == s.frame) {
      std::printf("[seats] %d: letting player %d go\n", frame, s.pad + 1); // (as its Let player N go)
      std::fflush(stdout);
      letGo(s.pad);
    } else if (s.button == -2 && frame == s.frame) {
      std::printf("[seats] %d: port 1 %s, port 2 %s; player 2 %s; %d playing; player 1 on the touch screen %d; "
                  "co-op setting %d\n",
                  frame, seatPadOf(0), seatPadOf(1), portCoopPlayerHere(1) ? "plays" : "nobody", portCoopPlayers(),
                  portTouchPlayerOne(), sOn != nullptr ? *sOn : -1);
      std::printf("[seats] %d: port 3 %s, port 4 %s; player 3 %s; player 4 %s\n", frame, seatPadOf(2), seatPadOf(3),
                  portCoopPlayerHere(2) ? "plays" : "nobody", portCoopPlayerHere(3) ? "plays" : "nobody");
      std::fflush(stdout);
    } else if (s.button == -1 && frame == s.frame && sSeatJoy[s.pad - 1] == nullptr) {
      // (an offscreen run's window never has the focus: SDL drops a controller's presses without it)
      SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
      SDL_VirtualJoystickDesc desc;
      SDL_INIT_INTERFACE(&desc);
      desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
      desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
      desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
      desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
      desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
      desc.name = s.pad == 1 ? "Paper Cut seat pad 1" : "Paper Cut seat pad";
      const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
      SDL_Joystick* joy = id != 0 ? SDL_OpenJoystick(id) : nullptr;
      if (joy != nullptr) {
        SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
        SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
      }
      sSeatJoy[s.pad - 1] = joy;
      std::printf("[seats] %d: pad %d %s\n", frame, s.pad, joy != nullptr ? "plugged in" : SDL_GetError());
      std::fflush(stdout);
    } else if (s.button == -4 && frame == s.frame && sSeatJoy[s.pad - 1] != nullptr) {
      const SDL_JoystickID id = SDL_GetJoystickID(sSeatJoy[s.pad - 1]);
      SDL_CloseJoystick(sSeatJoy[s.pad - 1]);
      sSeatJoy[s.pad - 1] = nullptr;
      std::printf("[seats] %d: pad %d %s\n", frame, s.pad, SDL_DetachVirtualJoystick(id) ? "unplugged" : SDL_GetError());
      std::fflush(stdout);
    } else if (s.button >= 0 && sSeatJoy[s.pad - 1] != nullptr && (frame == s.frame || frame == s.frame + s.hold)) {
      SDL_SetJoystickVirtualButton(sSeatJoy[s.pad - 1], s.button, frame == s.frame);
      if (frame == s.frame) {
        std::printf("[seats] %d: pad %d presses %s\n", frame, s.pad,
                    SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(s.button)));
        std::fflush(stdout);
      }
    }
  }
}

void overlayFrame(int) { seatsTestFrame(); }

struct SecondPlayerSettings {
  SecondPlayerSettings() {
    sOn = portSettingsDeclare("second_player", 1, 0, 1); // (on: nothing changes until a second controller joins)
    portSettingsAddToggle("Multiplayer", "Co-op (up to 4 players)",
                          "Players 2 to 4 join with {START} on their own controller. Player 2 plays the partner (Luigi "
                          "with Brothers mode on); players 3 and 4 play companions. In battle: their moves, action "
                          "commands and guards. In the field: the stick walks them, {A} hops, {B} talks or hits, {X} is "
                          "the partner's ability, {L} or {R} the next partner; Mario's {Y} calls them back. On a "
                          "handheld its own controls are player 1, so one paired controller is enough for two; on a "
                          "phone or tablet the first paired controller is player 1.",
                          PORT_SETTING_CHANGES_PLAY, "second_player",
                          "second player two players three four 3 4 up to 4 co-op coop multiplayer partner companion "
                          "controller 2 p2 p3 p4 join");
    portSettingsAddSection("Multiplayer", "Players", nullptr, 0, playersSection,
                           "players controller 2 second p2 join co-op coop keyboard");
    // the players' colours (second_player_pc.c's portPlayerColour reads the key)
    portSettingsDeclare("player_colours", 0, 0, 1);
    static const char* const kColours[] = {"Standard", "High contrast"};
    portSettingsAddChoice("Multiplayer", "Player colours",
                          "Each player's colour on their card, their tag and a borrowed partner's tint. High contrast: "
                          "colours that stay apart for red-green and blue-yellow colour blindness. Each also carries "
                          "the player's number (2P, 3P, 4P).",
                          0, "player_colours", kColours, 2,
                          "player colours colors colour blind color blind deuteranopia protanopia tritanopia "
                          "contrast accessibility 2p 3p 4p tint");
    portPauseAddForm(&kForm);
    portSettingsAddOverlay(overlayFrame, inputEvent);
  }
} sSecondPlayer;

} // namespace
