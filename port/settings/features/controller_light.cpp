// The controller's light (Controls > Controller light): the light bar of a
// DualSense or DualShock 4, a Switch Pro's or another controller with a
// coloured light, through Aurora's PADSetColor (applied at the next pad read).
// Left as the controller has it, each player's colour (the 1P and 2P tags'),
// Mario's HP (green, then yellow, then red; pulsing when he's nearly out), or
// a colour of one's own. Rumble strength is Controls > Rumble strength.
#include <dolphin/pad.h>

#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" int portLightMarioHp(int* current, int* maximum); // controller_light_aurora.c

namespace {

enum { kOwn, kPlayers, kHp, kCustom, kCount };
const char* kChoices[kCount] = {"The controller's own", "Each player's colour", "Mario's HP", "Custom colour"};

int* sChoice;
int* sRgb;
uint32_t sShown[PAD_CHANMAX] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}; // what each port was given

// the 1P and 2P tags' colours (second_player_pc.c), then two more
constexpr uint32_t kPlayerColour[PAD_CHANMAX] = {0xFF7060, 0x6CC8FF, 0x7CD860, 0xF2C84B};

uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int shift) {
    const float x = float((a >> shift) & 0xFF), y = float((b >> shift) & 0xFF);
    return uint32_t(std::lround(x + (y - x) * t)) << shift;
  };
  return ch(16) | ch(8) | ch(0);
}

// Mario's HP as a colour: full green, half yellow, out red; under a quarter, a slow pulse
uint32_t hpColour() {
  int hp = 0, max = 0;
  if (!portLightMarioHp(&hp, &max)) {
    return kPlayerColour[0];
  }
  const float f = std::clamp(float(hp) / float(max), 0.f, 1.f);
  uint32_t c = f > 0.5f ? mix(0xF2C84B, 0x3CD25A, (f - 0.5f) * 2.f) : mix(0xE8302C, 0xF2C84B, f * 2.f);
  if (f <= 0.25f) {
    const float pulse = 0.55f + 0.45f * std::sin(float(ImGui::GetTime()) * 4.f);
    c = mix(0x200000, c, pulse);
  }
  return c;
}

// each presented frame: the colour each controller should show, given when it changes
void tick(int pageOpen) {
  (void)pageOpen;
  for (uint32_t port = 0; port < PAD_CHANMAX; port++) {
    uint32_t c;
    switch (*sChoice) {
    case kPlayers:
      c = kPlayerColour[port];
      break;
    case kHp:
      c = port == 0 ? hpColour() : kPlayerColour[port]; // Mario's controller; the others their player's
      break;
    case kCustom:
      c = uint32_t(*sRgb) & 0xFFFFFF;
      break;
    default:
      sShown[port] = 0xFFFFFFFF; // left alone (the controller keeps what it shows)
      continue;
    }
    if (c != sShown[port] && PADSetColor(port, u8(c >> 16), u8(c >> 8), u8(c))) {
      sShown[port] = c;
    }
  }
}

void customSection(int inGame) {
  (void)inGame;
  if (*sChoice != kCustom) {
    return;
  }
  float col[3] = {float((*sRgb >> 16) & 0xFF) / 255.f, float((*sRgb >> 8) & 0xFF) / 255.f,
                  float(*sRgb & 0xFF) / 255.f};
  ImGui::PushID("controller_light_rgb");
  if (ImGui::ColorEdit3("##rgb", col, ImGuiColorEditFlags_NoInputs)) {
    *sRgb = (int(std::lround(col[0] * 255.f)) << 16) | (int(std::lround(col[1] * 255.f)) << 8) |
            int(std::lround(col[2] * 255.f));
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    portSettingsSave();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(portTr("The light's colour"));
  ImGui::PopID();
}

// The Start menu's form (port/include/port/pause_forms.h): the colour, picked in the game's windows
unsigned lightRgb() { return unsigned(*sRgb) & 0xFFFFFF; }
void lightSet(unsigned rgb) {
  *sRgb = int(rgb & 0xFFFFFF);
  *sChoice = kCustom; // (a colour picked is the one shown)
  portSettingsSave();
}

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("The light's colour",
                        "A: pick the colour; Controller light becomes Custom colour.", nullptr, 0);
  std::snprintf(out[0].value, sizeof(out[0].value), "#%06X", lightRgb());
  return 1;
}

const char* formAct(int, int dir) {
  if (dir == 0) {
    portPauseColourOpen("The light's colour", lightRgb, lightSet);
  }
  return nullptr;
}

const PortPauseForm kForm = {"Controls", "The light's colour", formRows, formAct, PORT_PAUSE_FORM_OWN};

const bool sAdded = [] {
  sChoice = portSettingsDeclare("controller_light", kOwn, 0, kCount - 1);
  sRgb = portSettingsDeclare("controller_light_rgb", 0xE8452C, 0, 0xFFFFFF);
  portSettingsAddChoice("Controls", "Controller light",
                        "A controller's coloured light (DualSense, DualShock 4 and others with one): each player's "
                        "colour, Mario's HP (green to red, pulsing when he's nearly out), or a colour of your own.",
                        0, "controller_light", kChoices, kCount,
                        "controller light bar led colour color dualsense dualshock playstation hp health");
  portSettingsAddSection("Controls", "The light's colour", nullptr, 0, customSection,
                         "controller light custom colour color led");
  portPauseAddForm(&kForm);
  portSettingsAddOverlay(tick, nullptr);
  return true;
}();

} // namespace
