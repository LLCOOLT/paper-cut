// The input display (Interface > Input display; port/include/port/display.h):
// the GameCube controller as the game reads it, in a corner over the game, for
// streams and practice. The stick and C-stick in their octagonal gates (a dot
// where each points, its trail kept short), A B X Y in their GameCube places,
// Z, Start, the D-pad, and L and R as bars that fill with the analog press
// (the label lit at the click). The second player's controller beside it while
// Second player is on and one is connected. In the launcher's colours: paper
// cream on dark ink, gold and stage red for what is pressed.
#include "port/display.h"
#include "port/settings.h"

#include "../launcher_style.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// PAD_BUTTON_* (dolphin/pad.h)
constexpr unsigned kLeft = 0x1, kRight = 0x2, kDown = 0x4, kUp = 0x8, kZ = 0x10, kR = 0x20, kL = 0x40, kA = 0x100,
                   kB = 0x200, kX = 0x400, kY = 0x800, kStart = 0x1000;

constexpr float kW = 228.f, kH = 124.f; // a controller's panel, in units

ImU32 alpha(ImU32 c, int a) {
  return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

ImFont* font() {
  return gLauncherFonts.heading != nullptr ? gLauncherFonts.heading : ImGui::GetFont();
}

void label(ImDrawList* dl, ImVec2 c, float size, const char* text, ImU32 col) {
  ImFont* f = font();
  const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.f, text);
  dl->AddText(f, size, ImVec2(std::floor(c.x - ts.x * 0.5f), std::floor(c.y - ts.y * 0.5f)), col, text);
}

// a round button: lit when held, else an outline
void button(ImDrawList* dl, ImVec2 c, float r, const char* text, bool held, ImU32 lit, float u) {
  if (held) {
    dl->AddCircleFilled(c, r, lit, 24);
  } else {
    dl->AddCircleFilled(c, r, alpha(kPaper, 28), 24);
    dl->AddCircle(c, r, alpha(kPaper, 150), 24, 1.5f * u);
  }
  label(dl, c, r * 1.15f, text, held ? kInk : alpha(kPaper, 220));
}

// a stick: its octagonal gate, a line from the centre and the dot where it points
void stick(ImDrawList* dl, ImVec2 c, float r, int x, int y, ImU32 tint, float u, const char* text) {
  ImVec2 gate[8];
  for (int i = 0; i < 8; i++) {
    const float a = static_cast<float>(i) * 3.14159265f / 4.f;
    gate[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
  }
  dl->AddConvexPolyFilled(gate, 8, alpha(kPaper, 22));
  dl->AddPolyline(gate, 8, alpha(tint, 140), ImDrawFlags_Closed, 1.5f * u);
  // the stick's range is about +-100 at the gate's corners (the game clamps it so)
  const float k = r / 100.f;
  const ImVec2 at(c.x + std::clamp(static_cast<float>(x), -110.f, 110.f) * k,
                  c.y - std::clamp(static_cast<float>(y), -110.f, 110.f) * k);
  const bool moved = std::abs(x) > 8 || std::abs(y) > 8;
  dl->AddCircleFilled(c, 2.f * u, alpha(tint, 120), 12);
  if (moved) {
    dl->AddLine(c, at, alpha(tint, 170), 2.f * u);
  }
  dl->AddCircleFilled(at, r * 0.36f, moved ? tint : alpha(tint, 200), 20);
  if (text != nullptr) {
    label(dl, at, r * 0.36f * 1.2f, text, kInk);
  }
}

// L or R: a bar filling with the analog press, its letter lit at the click
void trigger(ImDrawList* dl, ImVec2 a, ImVec2 b, int value, bool click, const char* text, bool right, float u) {
  const float round = (b.y - a.y) * 0.5f;
  dl->AddRectFilled(a, b, alpha(kPaper, 28), round);
  const float t = std::clamp(static_cast<float>(value) / 255.f, 0.f, 1.f);
  if (t > 0.02f) {
    const float w = (b.x - a.x) * t;
    if (right) {
      dl->AddRectFilled(ImVec2(b.x - w, a.y), b, click ? kCurtain : kGold, round);
    } else {
      dl->AddRectFilled(a, ImVec2(a.x + w, b.y), click ? kCurtain : kGold, round);
    }
  }
  dl->AddRect(a, b, alpha(kPaper, click ? 230 : 120), round, 0, 1.2f * u);
  label(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), (b.y - a.y) * 0.95f, text,
        t > 0.5f || click ? kInk : alpha(kPaper, 230));
}

// the D-pad: a cross, each arm lit while held
void dpad(ImDrawList* dl, ImVec2 c, float arm, float th, unsigned held, float u) {
  struct Arm {
    unsigned bit;
    float dx, dy;
  } const arms[] = {{kUp, 0, -1}, {kDown, 0, 1}, {kLeft, -1, 0}, {kRight, 1, 0}};
  dl->AddRectFilled(ImVec2(c.x - th, c.y - th), ImVec2(c.x + th, c.y + th), alpha(kPaper, 60));
  for (const Arm& a : arms) {
    const ImVec2 lo(c.x + (a.dx != 0 ? (a.dx < 0 ? -arm : th) : -th), c.y + (a.dy != 0 ? (a.dy < 0 ? -arm : th) : -th));
    const ImVec2 hi(c.x + (a.dx != 0 ? (a.dx < 0 ? -th : arm) : th), c.y + (a.dy != 0 ? (a.dy < 0 ? -th : arm) : th));
    if (held & a.bit) {
      dl->AddRectFilled(lo, hi, kGold, 2.f * u);
    } else {
      dl->AddRectFilled(lo, hi, alpha(kPaper, 60), 2.f * u);
      dl->AddRect(lo, hi, alpha(kPaper, 130), 2.f * u, 0, 1.f * u);
    }
  }
}

void controller(ImDrawList* dl, ImVec2 o, float u, int chan, unsigned buttons, const signed char* sticks,
                const unsigned char* triggers) {
  const auto P = [&](float x, float y) { return ImVec2(o.x + x * u, o.y + y * u); };
  dl->AddRectFilled(o, P(kW, kH), alpha(kInk, 215), 12.f * u);
  dl->AddRect(o, P(kW, kH), alpha(kGold, 200), 12.f * u, 0, 1.5f * u);
  trigger(dl, P(12, 9), P(78, 23), triggers[0], (buttons & kL) != 0, "L", false, u);
  trigger(dl, P(150, 9), P(216, 23), triggers[1], (buttons & kR) != 0, "R", true, u);
  char who[4];
  std::snprintf(who, sizeof(who), "P%d", chan + 1);
  label(dl, P(114, 16), 13.f * u, who, alpha(kGold, 230));
  // Z, under R, as on the controller
  const bool z = (buttons & kZ) != 0;
  dl->AddRectFilled(P(160, 28), P(206, 38), z ? kGold : alpha(kPaper, 28), 5.f * u);
  if (!z) {
    dl->AddRect(P(160, 28), P(206, 38), alpha(kPaper, 130), 5.f * u, 0, 1.2f * u);
  }
  label(dl, P(183, 33), 10.f * u, "Z", z ? kInk : alpha(kPaper, 220));
  stick(dl, P(42, 64), 24.f * u, sticks[0], sticks[1], kPaper, u, nullptr);
  dpad(dl, P(76, 102), 13.f * u, 4.f * u, buttons, u);
  stick(dl, P(124, 104), 14.f * u, sticks[2], sticks[3], kGold, u, "C");
  // Start between the sticks
  button(dl, P(108, 58), 6.f * u, "", (buttons & kStart) != 0, kPaper, u);
  label(dl, P(108, 72), 9.f * u, "START", alpha(kPaper, 170));
  // A big, B low left of it, X right, Y above
  button(dl, P(184, 76), 14.f * u, "A", (buttons & kA) != 0, kGold, u);
  button(dl, P(159, 92), 8.5f * u, "B", (buttons & kB) != 0, kCurtain, u);
  button(dl, P(208, 66), 8.5f * u, "X", (buttons & kX) != 0, kPaper, u);
  button(dl, P(181, 52), 8.5f * u, "Y", (buttons & kY) != 0, kPaper, u);
}

void overlay(int pageOpen) {
  static int* sCorner = portSettingsDeclare("input_display", 0, 0, 4);
  const int corner = *sCorner;
  if (corner == 0 || pageOpen) {
    return;
  }
  struct Pad {
    int chan;
    unsigned short buttons;
    signed char sticks[4];
    unsigned char triggers[2];
  } pads[2];
  int count = 0;
  const int* second = portSettingsFind("second_player");
  for (int chan = 0; chan < 2; chan++) {
    Pad& p = pads[count];
    p.chan = chan;
    if (chan == 1 && (second == nullptr || *second == 0)) {
      break;
    }
    if (portInputDisplayPad(chan, &p.buttons, p.sticks, p.triggers)) {
      count++;
    } else if (chan == 0) {
      // no controller 1 (the keyboard plays as it): an idle one, so the corner still shows
      p.buttons = 0;
      p.sticks[0] = p.sticks[1] = p.sticks[2] = p.sticks[3] = 0;
      p.triggers[0] = p.triggers[1] = 0;
      count++;
    }
  }
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float u = std::max(0.75f, std::min(display.x, display.y) / 720.f) * 1.1f;
  const float pad = 14.f, gap = 8.f * u;
  const bool right = corner == 2 || corner == 4, bottom = corner >= 3;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  for (int i = 0; i < count; i++) {
    // side by side, the first player's in the corner
    const float x = right ? display.x - pad - (kW * u) * (i + 1) - gap * i : pad + (kW * u + gap) * i;
    const float y = bottom ? display.y - pad - kH * u : pad;
    controller(dl, ImVec2(std::floor(x), std::floor(y)), u, pads[i].chan, pads[i].buttons, pads[i].sticks,
               pads[i].triggers);
  }
}

const bool kRegistered = [] {
  static const char* const kCorners[] = {"Off", "Top left", "Top right", "Bottom left", "Bottom right"};
  portSettingsDeclare("input_display", 0, 0, 4);
  portSettingsAddChoice("Interface", "Input display",
                        "The controller as the game reads it, in a corner: sticks, buttons, D-pad and triggers "
                        "(with Second player on, the second controller beside it).",
                        0, "input_display", kCorners, 5,
                        "input display controller buttons stick overlay stream practice viewer pad");
  portSettingsAddOverlay(overlay, nullptr);
  return true;
}();

} // namespace
