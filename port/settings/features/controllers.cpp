// Controllers (Controls tab): every kind of controller, not only the ones SDL
// knows.
//  - Mappings: at start, the community SDL_GameControllerDB (about 2,300
//    models, port/assets/controllers) and then the player's own
//    (<user>/gamecontrollerdb.txt), so most "unknown" pads work as gamepads.
//  - The list: each connected controller, its kind and battery, and which
//    player (1-4) it is, changeable; a live test of its buttons and sticks.
//  - Dead zones and trigger points per player (Aurora's, saved with its
//    button mappings).
//  - Set up a controller nothing knows (an old USB pad, an arcade stick):
//    press each button when asked; the mapping is added at once and saved to
//    <user>/gamecontrollerdb.txt for next time.
#include "port/settings.h"
#include "port/pause_forms.h"
#include "port/tr.h"
#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
extern const unsigned char portControllerDb[];
extern const unsigned int portControllerDb_size;
}

namespace {

std::string userDbPath() { return std::string(portSettingsUserPath()) + "/gamecontrollerdb.txt"; }

// the controller at Aurora's index i as an SDL gamepad, and the player it is (-1 none)
SDL_Gamepad* padAt(u32 i) { return PADGetSDLGamepadForIndex(i); }

int playerOf(u32 index) {
  for (u32 p = 0; p < 4; p++) {
    if (PADGetIndexForPort(p) == static_cast<s32>(index)) {
      return static_cast<int>(p);
    }
  }
  return -1;
}

void battery(SDL_Gamepad* pad) {
  int percent = -1;
  const SDL_PowerState st = SDL_GetGamepadPowerInfo(pad, &percent);
  if (st == SDL_POWERSTATE_ON_BATTERY && percent >= 0) {
    ImGui::SameLine();
    ImGui::TextDisabled(portTr("battery %d%%"), percent);
  } else if (st == SDL_POWERSTATE_CHARGING) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("charging"));
  }
}

// a stick as a circle with its dot
void stickView(const char* label, float x, float y, float dead) {
  const float r = ImGui::GetFontSize() * 1.6f;
  ImGui::BeginGroup();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const ImVec2 c(at.x + r, at.y + r);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddCircle(c, r, ImGui::GetColorU32(ImGuiCol_Border), 32, 1.5f);
  if (dead > 0.f) {
    dl->AddCircleFilled(c, r * dead, ImGui::GetColorU32(ImGuiCol_FrameBg), 24);
  }
  dl->AddCircleFilled(ImVec2(c.x + x * r, c.y + y * r), r * 0.15f, ImGui::GetColorU32(ImGuiCol_CheckMark));
  ImGui::Dummy(ImVec2(r * 2, r * 2));
  ImGui::TextDisabled("%s", portTr(label));
  ImGui::EndGroup();
}

void testView(SDL_Gamepad* pad, const PADDeadZones* dz) {
  auto axis = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad, a) / 32767.f; };
  stickView("Stick", axis(SDL_GAMEPAD_AXIS_LEFTX), axis(SDL_GAMEPAD_AXIS_LEFTY),
            dz != nullptr && dz->useDeadzones ? dz->stickDeadZone / 32767.f : 0.f);
  ImGui::SameLine();
  stickView("C-stick", axis(SDL_GAMEPAD_AXIS_RIGHTX), axis(SDL_GAMEPAD_AXIS_RIGHTY),
            dz != nullptr && dz->useDeadzones ? dz->substickDeadZone / 32767.f : 0.f);
  ImGui::SameLine();
  ImGui::BeginGroup();
  const float w = ImGui::GetFontSize() * 6.f;
  const GlyphStyle style = glyphStyleOf(SDL_GetGamepadType(pad), SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad));
  ImGui::ProgressBar(std::max(0.f, axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)), ImVec2(w, 0),
                     portTr(nativeButtonLabel(style, PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_L)));
  ImGui::ProgressBar(std::max(0.f, axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)), ImVec2(w, 0),
                     portTr(nativeButtonLabel(style, PAD_NATIVE_BUTTON_INVALID, PAD_TRIGGER_R)));
  // the held buttons as printed on this controller
  std::string held;
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
    if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b))) {
      held += (held.empty() ? "" : ", ") + std::string(portTr(nativeButtonLabel(style, static_cast<u32>(b), 0)));
    }
  }
  ImGui::TextWrapped(portTr("Held: %s"), held.empty() ? portTr("nothing") : held.c_str());
  ImGui::EndGroup();
}

void deadZones(u32 player) {
  PADDeadZones* dz = PADGetDeadZones(player);
  if (dz == nullptr) {
    return;
  }
  bool changed = false;
  bool use = dz->useDeadzones;
  if (ImGui::Checkbox(portTrLabel("Dead zones"), &use)) {
    dz->useDeadzones = use;
    changed = true;
  }
  ImGui::BeginDisabled(!dz->useDeadzones);
  auto pct = [&](const char* label, u16* v) {
    int p = static_cast<int>(std::lround(*v * 100.0 / 32767.0));
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
    if (ImGui::SliderInt(portTrLabel(label), &p, 0, 50, "%d%%")) {
      *v = static_cast<u16>(p * 32767 / 100);
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
  };
  pct("Stick", &dz->stickDeadZone);
  ImGui::SameLine();
  pct("C-stick", &dz->substickDeadZone);
  ImGui::EndDisabled();
  bool emulate = dz->emulateTriggers;
  if (ImGui::Checkbox(portTrLabel("Triggers click at a point (as the GameCube's L and R)"), &emulate)) {
    dz->emulateTriggers = emulate;
    changed = true;
  }
  if (dz->emulateTriggers) {
    auto trig = [&](const char* label, u16* v) {
      int p = static_cast<int>(std::lround(*v * 100.0 / 32767.0));
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.f);
      if (ImGui::SliderInt(portTrLabel(label), &p, 5, 100, "%d%%")) {
        *v = static_cast<u16>(p * 32767 / 100);
      }
      changed |= ImGui::IsItemDeactivatedAfterEdit();
    };
    trig("L clicks at", &dz->leftTriggerActivationZone);
    ImGui::SameLine();
    trig("R clicks at", &dz->rightTriggerActivationZone);
  }
  if (changed) {
    PADSerializeMappings();
  }
}

void listSection(int) {
  const u32 count = PADCount();
  if (count == 0) {
    ImGui::TextDisabled("%s", portTr("No controller connected. Any Xbox, PlayStation, Switch, GameCube (through an adapter), MFi "
                        "or other USB or Bluetooth controller works; one this page doesn't list can be set up "
                        "below."));
    return;
  }
  static int sTest = -1;
  const char* kPlayers[] = {portTr("Not playing"), portTr("Player 1"), portTr("Player 2"), portTr("Player 3"),
                            portTr("Player 4")};
  for (u32 i = 0; i < count; i++) {
    SDL_Gamepad* pad = padAt(i);
    if (pad == nullptr) {
      continue;
    }
    ImGui::PushID(static_cast<int>(i));
    const int player = playerOf(i);
    int choice = player + 1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.f);
    if (ImGui::Combo("##player", &choice, kPlayers, 5)) {
      if (choice == 0) {
        if (player >= 0) {
          PADClearPort(static_cast<u32>(player));
        }
      } else {
        PADSetPortForIndex(i, static_cast<u32>(choice - 1));
      }
      PADSerializeMappings();
    }
    ImGui::SameLine();
    // its name as players know it (button_glyphs.h), the driver's as the tooltip
    const std::string name = controllerName(i), driver = controllerDriverName(i);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());
    if (!driver.empty() && driver != name && ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", driver.c_str());
    }
    if (player >= 0 && PADIsGCAdapter(static_cast<u32>(player))) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%s)", portTr("through an adapter"));
    }
    battery(pad);
    ImGui::SameLine();
    if (ImGui::SmallButton(portTrLabel(sTest == static_cast<int>(i) ? "Hide test##test" : "Test##test"))) {
      sTest = sTest == static_cast<int>(i) ? -1 : static_cast<int>(i);
    }
    if (sTest == static_cast<int>(i)) {
      ImGui::Indent();
      testView(pad, player >= 0 ? PADGetDeadZones(static_cast<u32>(player)) : nullptr);
      if (player >= 0) {
        deadZones(static_cast<u32>(player));
      } else {
        ImGui::TextDisabled("%s", portTr("Make it a player to set its dead zones."));
      }
      ImGui::Unindent();
    }
    ImGui::PopID();
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("Player 1 plays; player 2 can play the partner in battle and in the field (Second "
                                  "player, below). The buttons of each player are set under Controller buttons."));
  ImGui::PopStyleColor();
}

// --- Setting up a controller nothing knows ---

struct Step {
  const char* field; // SDL's mapping name
  const char* ask;
  bool axis;         // a stick: the axis it moves, whole
};

constexpr Step kSteps[] = {
    {"a", "the bottom face button (A on the GameCube)", false},
    {"b", "the right face button", false},
    {"x", "the left face button", false},
    {"y", "the top face button", false},
    {"start", "Start", false},
    {"back", "Select / Back (or skip)", false},
    {"leftshoulder", "the left shoulder button", false},
    {"rightshoulder", "the right shoulder button (Z)", false},
    {"lefttrigger", "the left trigger (L)", false},
    {"righttrigger", "the right trigger (R)", false},
    {"dpup", "the d-pad up", false},
    {"dpdown", "the d-pad down", false},
    {"dpleft", "the d-pad left", false},
    {"dpright", "the d-pad right", false},
    {"leftx", "the left stick to the right", true},
    {"lefty", "the left stick down", true},
    {"rightx", "the right stick to the right (or skip)", true},
    {"righty", "the right stick down (or skip)", true},
};
constexpr int kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

struct Wizard {
  SDL_JoystickID id = 0;
  SDL_Joystick* joy = nullptr;
  int step = -1;
  std::vector<Sint16> rest;    // each axis at rest
  std::vector<bool> buttonsDown; // held when the step began (not taken)
  std::vector<Uint8> hats;
  std::string fields[kStepCount];
  Uint64 armedAt = 0;
  std::string done; // the result shown after
} sWiz;

void wizardBegin(SDL_JoystickID id) {
  sWiz = Wizard{};
  sWiz.id = id;
  sWiz.joy = SDL_OpenJoystick(id);
  if (sWiz.joy == nullptr) {
    sWiz.done = std::string(portTr("Couldn't open it: ")) + SDL_GetError();
    return;
  }
  sWiz.step = 0;
  for (int a = 0; a < SDL_GetNumJoystickAxes(sWiz.joy); a++) {
    sWiz.rest.push_back(SDL_GetJoystickAxis(sWiz.joy, a));
  }
}

// the step's input begins: what's held now isn't the answer
void arm() {
  sWiz.buttonsDown.assign(static_cast<size_t>(SDL_GetNumJoystickButtons(sWiz.joy)), false);
  for (int b = 0; b < static_cast<int>(sWiz.buttonsDown.size()); b++) {
    sWiz.buttonsDown[b] = SDL_GetJoystickButton(sWiz.joy, b);
  }
  sWiz.hats.assign(static_cast<size_t>(SDL_GetNumJoystickHats(sWiz.joy)), 0);
  for (int h = 0; h < static_cast<int>(sWiz.hats.size()); h++) {
    sWiz.hats[h] = SDL_GetJoystickHat(sWiz.joy, h);
  }
  sWiz.armedAt = SDL_GetTicks();
}

std::string detect(const Step& s) {
  if (SDL_GetTicks() - sWiz.armedAt < 250) {
    return ""; // the last answer's press still settling
  }
  for (int b = 0; b < static_cast<int>(sWiz.buttonsDown.size()); b++) {
    const bool down = SDL_GetJoystickButton(sWiz.joy, b);
    if (down && !sWiz.buttonsDown[b]) {
      return "b" + std::to_string(b);
    }
    sWiz.buttonsDown[b] = down && sWiz.buttonsDown[b];
  }
  for (int h = 0; h < static_cast<int>(sWiz.hats.size()); h++) {
    const Uint8 v = SDL_GetJoystickHat(sWiz.joy, h);
    if (v != 0 && v != sWiz.hats[h] && (v == 1 || v == 2 || v == 4 || v == 8)) {
      return "h" + std::to_string(h) + "." + std::to_string(v);
    }
    sWiz.hats[h] = v == 0 ? 0 : sWiz.hats[h];
  }
  for (int a = 0; a < static_cast<int>(sWiz.rest.size()); a++) {
    const int v = SDL_GetJoystickAxis(sWiz.joy, a), from = sWiz.rest[a];
    if (std::abs(v - from) > 16000) {
      if (s.axis) {
        // the direction asked for is positive (right, down): invert an axis that went the other way
        return std::string("a") + std::to_string(a) + (v < from ? "~" : "");
      }
      // a trigger resting at -32768 is a whole axis; one at 0, a half
      return (from < -16000 ? "a" : v > from ? "+a" : "-a") + std::to_string(a);
    }
  }
  return "";
}

// a translated message with the controller's name in its %s
std::string withName(const char* message, const std::string& name) {
  std::string out = message;
  const size_t at = out.find("%s");
  return at == std::string::npos ? out : out.replace(at, 2, name);
}

void wizardSave() {
  SDL_GUID guid = SDL_GetJoystickGUID(sWiz.joy);
  char g[64];
  SDL_GUIDToString(guid, g, sizeof(g));
  std::string name = SDL_GetJoystickName(sWiz.joy) != nullptr ? SDL_GetJoystickName(sWiz.joy) : "Controller";
  std::replace(name.begin(), name.end(), ',', ' ');
  std::string mapping = std::string(g) + "," + name + ",";
  for (int i = 0; i < kStepCount; i++) {
    if (!sWiz.fields[i].empty()) {
      mapping += std::string(kSteps[i].field) + ":" + sWiz.fields[i] + ",";
    }
  }
  mapping += std::string("platform:") + SDL_GetPlatform() + ",";
  SDL_CloseJoystick(sWiz.joy);
  sWiz.joy = nullptr;
  if (SDL_AddGamepadMapping(mapping.c_str()) < 0) {
    sWiz.done = std::string(portTr("SDL didn't take the mapping: ")) + SDL_GetError();
  } else if (FILE* f = std::fopen(userDbPath().c_str(), "a")) {
    std::fprintf(f, "%s\n", mapping.c_str());
    std::fclose(f);
    sWiz.done = withName(portTr("%s is set up. It's in the list above now, and next time."), name);
  } else {
    sWiz.done = withName(portTr("%s is set up for now; it couldn't be saved for next time."), name);
  }
  sWiz.step = -1;
}

void setupSection(int) {
  if (sWiz.step >= 0) {
    if (sWiz.joy == nullptr || !SDL_JoystickConnected(sWiz.joy)) {
      sWiz.step = -1;
      sWiz.done = portTr("The controller went away.");
      return;
    }
    const Step& s = kSteps[sWiz.step];
    ImGui::Text(portTr("Step %d of %d: press %s"), sWiz.step + 1, kStepCount, portTr(s.ask));
    ImGui::ProgressBar(static_cast<float>(sWiz.step) / kStepCount, ImVec2(ImGui::GetFontSize() * 14.f, 0), "");
    if (sWiz.armedAt == 0) {
      arm();
    }
    std::string got = detect(s);
    bool next = !got.empty();
    if (ImGui::Button(portTrLabel("Skip"))) {
      next = true;
      got.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Cancel"))) {
      SDL_CloseJoystick(sWiz.joy);
      sWiz = Wizard{};
      return;
    }
    if (next) {
      sWiz.fields[sWiz.step] = got;
      sWiz.armedAt = 0;
      if (++sWiz.step == kStepCount) {
        wizardSave();
      }
    }
    return;
  }
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  int unknown = 0;
  for (int i = 0; i < count; i++) {
    if (SDL_IsGamepad(ids[i])) {
      continue;
    }
    unknown++;
    ImGui::PushID(static_cast<int>(ids[i]));
    const char* n = SDL_GetJoystickNameForID(ids[i]);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", n != nullptr ? n : "A controller");
    ImGui::SameLine();
    if (ImGui::Button(portTrLabel("Set it up"))) {
      wizardBegin(ids[i]);
    }
    ImGui::PopID();
  }
  SDL_free(ids);
  if (unknown == 0) {
    ImGui::TextDisabled("%s", portTr("Every connected controller is recognised."));
  }
  if (!sWiz.done.empty()) {
    ImGui::TextWrapped("%s", sWiz.done.c_str());
  }
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", portTr("A controller listed here works once you press each of its buttons when asked. Mappings "
                     "you make are kept in gamecontrollerdb.txt in the save folder."));
  ImGui::PopStyleColor();
}

// The Start menu's form (port/include/port/pause_forms.h): each connected controller and its player
constexpr const char* kFormPlayers[] = {"Not playing", "Player 1", "Player 2", "Player 3", "Player 4"};

int formRows(PortPauseRow* out, int max) {
  const u32 count = PADCount();
  int n = 0;
  if (count == 0) {
    out[n++] = PortPauseRow{"No controller connected", "Any Xbox, PlayStation, Switch or GameCube controller works.",
                            {}, {}, nullptr, -1, PORT_PAUSE_ROW_DIM};
    return n;
  }
  for (u32 i = 0; i < count && n < max; i++) {
    if (padAt(i) == nullptr) {
      continue;
    }
    PortPauseRow r{nullptr, "Which player it plays as; left and right change it.", {}, {},
                   kFormPlayers[playerOf(i) + 1], static_cast<int>(i), 0};
    std::snprintf(r.text, sizeof(r.text), "%s", controllerName(i).c_str());
    out[n++] = r;
  }
  return n;
}

const char* formAct(int id, int dir) {
  if (id < 0 || static_cast<u32>(id) >= PADCount()) {
    return nullptr;
  }
  const int player = playerOf(static_cast<u32>(id));
  const int choice = ((player + 1 + (dir == 0 ? 1 : dir)) % 5 + 5) % 5;
  if (choice == 0) {
    if (player >= 0) {
      PADClearPort(static_cast<u32>(player));
    }
  } else {
    PADSetPortForIndex(static_cast<u32>(id), static_cast<u32>(choice - 1));
  }
  PADSerializeMappings();
  return nullptr;
}

const PortPauseForm kForm = {"Controls", "Controllers", formRows, formAct};

struct ControllerSettings {
  ControllerSettings() {
    portSettingsAddSection("Controls", "Controllers", nullptr, 0, listSection,
                           "controllers gamepad player 1 2 3 4 battery test dead zone deadzone drift trigger "
                           "xbox playstation dualsense dualshock switch joy-con gamecube adapter 8bitdo");
    portSettingsAddSection("Controls", "Set up a controller", nullptr, 0, setupSection,
                           "unknown unrecognised controller joystick arcade stick usb mapping wizard");
    portPauseAddForm(&kForm);
  }
} sControllers;

} // namespace

// After aurora_initialize (main_aurora.c), before the controllers are opened:
// the community mappings, then the player's own (which win).
extern "C" void portControllersInit(const char* userPath) {
  SDL_IOStream* io = SDL_IOFromConstMem(portControllerDb, portControllerDb_size);
  const int added = io != nullptr ? SDL_AddGamepadMappingsFromIO(io, true) : -1;
  std::printf("[controllers] %d community mappings\n", added);
  if (userPath != nullptr) {
    const std::string mine = std::string(userPath) + "/gamecontrollerdb.txt";
    if (SDL_GetPathInfo(mine.c_str(), nullptr)) {
      std::printf("[controllers] %d of your own\n", SDL_AddGamepadMappingsFromFile(mine.c_str()));
    }
  }
  std::fflush(stdout);
}
