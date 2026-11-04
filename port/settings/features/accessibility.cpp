// Colour-blind filters and captions for sound cues (Accessibility tab).
//
// Colour-blind filter: a daltonization correction of the whole picture for
// protanopia, deuteranopia or tritanopia, at a strength; its matrix and where
// it comes from are in port/src/aurora/display_aurora.c, the pass in Aurora's
// present (port/patches/aurora/0020).
//
// Captions for sounds (port/include/port/captions.h): the sounds that tell the
// player something, as short captions low on the screen, at most two at once,
// fading, with an arrow when the sound came from out of sight to one side.
// The game calls portCaptionSfx as each sound starts (pmario_sound.c), and
// for the few cues that have no sound of their own (a hidden block, a guard,
// whose sounds are shared with others); port/src/aurora/captions_aurora.c
// works out where it was heard. The action commands' "Nice!" to "Excellent!"
// are left out: the game shows them already. Drawn over the game with Dear
// ImGui, in the launcher's colours, so never into the second screen's menu.
#include "port/captions.h"
#include "port/settings.h"
#include "port/tr.h"

#include "../launcher_style.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

constexpr const char* kFilters[] = {"Off", "Protanopia (red-blind)", "Deuteranopia (green-blind)",
                                    "Tritanopia (blue-blind)"};

// --- the captions ---

struct Cue {
  const char* sound; // the pssfxlist name, or a prefix of names (prefix), or the port's own cue
  bool prefix;
  const char* text;  // English, through portTr
  float seconds;
  bool offScreenOnly; // shown only when heard out of sight (a door Mario opens himself needs none)
};

// The sounds that carry something the player needs (the game's names, src/pmario_sound.c's pssfxlist)
constexpr Cue kCues[] = {
    // an enemy (or anyone but Mario and his partners) spots Mario: the "!" balloon (eff_fukidashi.c), and the "?" one
    {"SFX_EVT_NPC_BIKKURI_MARK1", false, "! Someone noticed you", 2.0f, false},
    {"SFX_EVT_NPC_QUESTION_MARK1", false, "? Someone is puzzled", 2.0f, false},
    // a hidden block hit (evt_mobj.c: its sound is a plain block's), a Star Piece bouncing out
    {"PORT_HIDDEN_BLOCK", false, "A hidden block appears", 2.5f, false},
    {"SFX_STAR_PIECE_LEAPED1", false, "A Star Piece pops out", 2.5f, false},
    // guards (battle_damage.c: their sounds are the action commands' Nice and Great)
    {"PORT_GUARD", false, "Guarded", 1.2f, false},
    {"PORT_SUPERGUARD", false, "Superguard!", 1.5f, false},
    // doors, switches and locks out of sight
    {"SFX_DOOR_OPEN", true, "A door opens", 2.5f, true},
    {"SFX_GLASS_DOOR1_OPEN1", false, "A door opens", 2.5f, true},
    {"SFX_SLIDE_DOOR1_UP", false, "A door opens", 2.5f, true},
    {"SFX_DOOR_SHUT", true, "A door closes", 2.0f, true},
    {"SFX_GLASS_DOOR1_CLOSE1", false, "A door closes", 2.0f, true},
    {"SFX_SLIDE_DOOR1_DOWN", false, "A door closes", 2.0f, true},
    {"SFX_MOBJ_SWITCH_ON1", false, "A switch clicks", 2.0f, true},
    {"SFX_MOBJ_BIKKURI_SWITCH_ON1", false, "A switch clicks", 2.0f, true},
    {"SFX_MOBJ_RIDE_SWITCH_ON", true, "A switch clicks", 2.0f, true},
    {"SFX_MOBJ_TIMER_SWITCH_HIT1", false, "A switch clicks", 2.0f, true},
    {"SFX_MOBJ_BLOCK_SWITCH1", false, "A switch clicks", 2.0f, true},
    {"SFX_MOBJ_UNLOCK1", false, "A lock opens", 2.5f, true},
};
constexpr int kCueCount = sizeof(kCues) / sizeof(kCues[0]);

int* sCaptions; // captions: on/off

struct Shown {
  int cue = -1;
  int where = PORT_CAPTION_NO_POS;
  double start = 0.0; // ImGui time; < 0: not drawn yet (it starts when first drawn)
};
std::mutex sMutex;
Shown sShown[2]; // [0] the older, [1] the newer

constexpr float kFadeIn = 0.15f, kFadeOut = 0.4f;

void push(int cue, int where) {
  std::lock_guard lock(sMutex);
  // the same caption again: shown afresh where it is
  for (Shown& s : sShown) {
    if (s.cue >= 0 && std::strcmp(kCues[s.cue].text, kCues[cue].text) == 0) {
      s.where = where;
      s.start = -1.0;
      return;
    }
  }
  if (sShown[0].cue < 0) {
    sShown[0] = Shown{cue, where, -1.0};
  } else {
    if (sShown[1].cue >= 0) {
      sShown[0] = sShown[1]; // at most two: the oldest goes
    }
    sShown[1] = Shown{cue, where, -1.0};
  }
}

ImU32 alpha(ImU32 c, float a) {
  const float base = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF);
  return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(base * a, 0.f, 255.f)) << IM_COL32_A_SHIFT);
}

ImFont* font() { return gLauncherFonts.heading != nullptr ? gLauncherFonts.heading : ImGui::GetFont(); }

// a small arrow pointing the way the sound came from
void arrow(ImDrawList* dl, ImVec2 c, float r, bool left, ImU32 col) {
  const float d = left ? -1.f : 1.f;
  dl->AddTriangleFilled(ImVec2(c.x + d * r, c.y), ImVec2(c.x - d * r * 0.6f, c.y - r * 0.85f),
                        ImVec2(c.x - d * r * 0.6f, c.y + r * 0.85f), col);
}

void overlay(int pageOpen) {
  if (sCaptions == nullptr || *sCaptions == 0) {
    std::lock_guard lock(sMutex);
    sShown[0] = sShown[1] = Shown{};
    return;
  }
  Shown shown[2];
  const double now = ImGui::GetTime();
  {
    std::lock_guard lock(sMutex);
    for (Shown& s : sShown) {
      if (s.cue >= 0 && s.start < 0.0) {
        s.start = now;
      }
      if (s.cue >= 0 && now - s.start > kCues[s.cue].seconds) {
        s = Shown{};
      }
    }
    if (sShown[0].cue < 0 && sShown[1].cue >= 0) {
      sShown[0] = sShown[1];
      sShown[1] = Shown{};
    }
    shown[0] = sShown[0];
    shown[1] = sShown[1];
  }
  if (pageOpen) {
    return; // the settings page over the game: kept, not drawn
  }
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float u = std::max(0.75f, std::min(display.x, display.y) / 720.f);
  const float size = 32.f * u, padX = 18.f * u, padY = 8.f * u, gap = 8.f * u;
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  ImFont* f = font();
  // the newer at the bottom, the older above it; low on the screen, clear of the very edge
  float bottom = display.y * 0.86f;
  for (int i = 1; i >= 0; i--) {
    const Shown& s = shown[i];
    if (s.cue < 0) {
      continue;
    }
    const Cue& cue = kCues[s.cue];
    const float t = static_cast<float>(now - s.start);
    const float a = std::clamp(std::min(t / kFadeIn, (cue.seconds - t) / kFadeOut), 0.f, 1.f);
    const char* text = portTr(cue.text);
    const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.f, text);
    const bool left = s.where == PORT_CAPTION_OFF_LEFT, right = s.where == PORT_CAPTION_OFF_RIGHT;
    const float arrowW = (left || right) ? size * 0.9f : 0.f;
    const float w = ts.x + padX * 2.f + arrowW, h = ts.y + padY * 2.f;
    const ImVec2 lo(std::floor((display.x - w) * 0.5f), std::floor(bottom - h));
    const ImVec2 hi(lo.x + w, lo.y + h);
    dl->AddRectFilled(ImVec2(lo.x + 3.f * u, lo.y + 4.f * u), ImVec2(hi.x + 3.f * u, hi.y + 4.f * u),
                      alpha(IM_COL32(0, 0, 0, 90), a), h * 0.3f);
    dl->AddRectFilled(lo, hi, alpha(kInk, a * 0.92f), h * 0.3f);
    dl->AddRect(lo, hi, alpha(kGold, a), h * 0.3f, 0, 1.5f * u);
    float x = lo.x + padX;
    if (left) {
      arrow(dl, ImVec2(x + size * 0.3f, lo.y + h * 0.5f), size * 0.3f, true, alpha(kGold, a));
      x += arrowW;
    }
    dl->AddText(f, size, ImVec2(x, lo.y + padY), alpha(kPaper, a), text);
    if (right) {
      arrow(dl, ImVec2(hi.x - padX - size * 0.3f, lo.y + h * 0.5f), size * 0.3f, false, alpha(kGold, a));
    }
    bottom = lo.y - gap;
  }
}

const bool sAdded = [] {
  // declared here too, at start, so the page has them before the game first draws (display_aurora.c applies them)
  portSettingsDeclare("colorblind", 0, 0, 3);
  portSettingsDeclare("colorblind_strength", 100, 0, 100);
  sCaptions = portSettingsDeclare("captions", 0, 0, 1);
  portSettingsAddChoice("Accessibility", "Colour-blind filter",
                        "Shifts the colours your eyes confuse into ones they can tell apart, over the whole picture "
                        "(a daltonization correction, not a simulation).",
                        0, "colorblind", kFilters, 4,
                        "color colour blind blindness daltonize protanopia deuteranopia tritanopia filter");
  portSettingsAddSlider("Accessibility", "Filter strength", "How far the filter moves the colours.", 0, "colorblind_strength", 0, 100, "%d%%",
                        "color colour blind filter strength daltonize");
  portSettingsAddToggle("Accessibility", "Captions for sounds",
                        "Short captions low on the screen for the sounds that tell you something: an enemy noticing "
                        "you, a hidden block, a Star Piece, a guard, a door or switch out of sight.",
                        0, "captions", "captions subtitles sounds deaf hard of hearing cues audio visual");
  portSettingsAddOverlay(overlay, nullptr);
  return true;
}();

} // namespace

extern "C" int portCaptionLookup(const char* name, int* offScreenOnly) {
  if (sCaptions == nullptr || *sCaptions == 0) {
    return -1;
  }
  for (int i = 0; i < kCueCount; i++) {
    const Cue& c = kCues[i];
    if (c.prefix ? std::strncmp(name, c.sound, std::strlen(c.sound)) == 0 : std::strcmp(name, c.sound) == 0) {
      *offScreenOnly = c.offScreenOnly ? 1 : 0;
      return i;
    }
  }
  return -1;
}

extern "C" void portCaptionPush(int entry, int where) {
  if (entry >= 0 && entry < kCueCount) {
    static const bool log = std::getenv("TTYD_CAPTION_LOG") != nullptr; // tests: each caption as it comes
    if (log) {
      std::printf("[caption] %s (where %d)\n", kCues[entry].text, where);
      std::fflush(stdout);
    }
    push(entry, where);
  }
}
