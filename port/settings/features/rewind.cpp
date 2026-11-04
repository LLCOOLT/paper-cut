// Rewind (System > Rewind; port/include/port/rewind.h): hold Backspace (the
// key changeable here) or the chosen controller button, and the game steps
// back through its last seconds; let go and it plays on from there. Off by
// default; the length 5 to 60 seconds. Save states have no controller
// button, so neither has this until one is chosen.
#include "port/rewind.h"
#include "port/settings.h"
#include "port/tr.h"

#include "../launcher_style.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>

namespace {

const char* kPadChoices[] = {"None", "Share / capture button", "Left back paddle", "Right back paddle"};
constexpr SDL_GamepadButton kPadButtons[] = {SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_MISC1,
                                             SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1};

int* sOn;
int* sKey;
int* sPad;
bool sRebinding;
bool sPageOpen;
bool sKeyDown, sPadDown;

void update() {
  portRewindHold(sKeyDown || sPadDown);
}

// the "<< Rewind" badge while stepping back
void tick(int pageOpen) {
  sPageOpen = pageOpen != 0;
  if (sPageOpen && (sKeyDown || sPadDown)) {
    sKeyDown = sPadDown = false; // the page took over
    update();
  }
  if (!portRewindActive() || pageOpen) {
    return;
  }
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  char text[64];
  std::snprintf(text, sizeof(text), portTr("<< Rewind  %.1f s"), portRewindSeconds());
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 at(18.f, 18.f);
  dl->AddRectFilled(ImVec2(at.x - 10.f, at.y - 6.f), ImVec2(at.x + ts.x + 10.f, at.y + ts.y + 6.f),
                    IM_COL32(20, 16, 30, 190), 8.f);
  dl->AddText(at, kPaper, text);
}

int event(const void* e) {
  const SDL_Event* ev = static_cast<const SDL_Event*>(e);
  switch (ev->type) {
  case SDL_EVENT_KEY_DOWN:
    if (sRebinding) {
      if (ev->key.scancode != SDL_SCANCODE_ESCAPE) {
        *sKey = ev->key.scancode;
        portSettingsSave();
      }
      sRebinding = false;
      return 1;
    }
    if (!*sOn || sPageOpen || ev->key.scancode != *sKey || ImGui::GetIO().WantTextInput) {
      return 0;
    }
    if (!ev->key.repeat) {
      sKeyDown = true;
      update();
    }
    return 1;
  case SDL_EVENT_KEY_UP:
    if (ev->key.scancode == *sKey && sKeyDown) {
      sKeyDown = false;
      update();
      return 1;
    }
    return 0;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP: {
    const int p = *sPad;
    if (p <= 0 || p > 3 || ev->gbutton.button != kPadButtons[p]) {
      return 0;
    }
    const bool down = ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && *sOn && !sPageOpen;
    if (down == sPadDown) {
      return 0;
    }
    sPadDown = down;
    update();
    return 1;
  }
  case SDL_EVENT_WINDOW_FOCUS_LOST:
    if (sKeyDown || sPadDown) {
      sKeyDown = sPadDown = false;
      update();
    }
    return 0;
  default:
    return 0;
  }
}

// the key, changeable, and what the states cost now
void section(int inGame) {
  const char* name = sRebinding ? portTr("Press a key...") : SDL_GetScancodeName(static_cast<SDL_Scancode>(*sKey));
  if (ImGui::Button(name[0] != '\0' ? name : "?", ImVec2(ImGui::GetFontSize() * 9.f, 0.f))) {
    sRebinding = true;
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(portTr("Rewind key"));
  if (!portRewindSupported()) {
    ImGui::TextDisabled("%s", portTr("Not in online co-op."));
  } else if (inGame && *sOn) {
    float states, mirror, avg, max;
    portRewindCosts(&states, &mirror, &avg, &max);
    ImGui::TextDisabled(portTr("Kept now: %.1f s, %.0f MiB (and %.0f MiB for the newest whole); %.1f ms a state, "
                               "every 6 frames"),
                        portRewindSeconds(), states, mirror, avg);
  }
}

const bool sAdded = [] {
  sOn = portSettingsDeclare("rewind", 0, 0, 1);
  portSettingsDeclare("rewind_seconds", 10, 5, 60);
  sKey = portSettingsDeclare("rewind_key", SDL_SCANCODE_BACKSPACE, 0, SDL_SCANCODE_COUNT - 1);
  sPad = portSettingsDeclare("rewind_pad", 0, 0, 3);
  portSettingsAddToggle("System", "Rewind",
                        "Hold the Rewind key (Backspace) or button and the game steps back through its last "
                        "seconds; let go to play on from there. Uses some memory and a little time every few "
                        "frames. Not in online co-op.",
                        0, "rewind", "rewind back undo time reverse backspace");
  portSettingsAddSlider("System", "Rewind length", nullptr, 0, "rewind_seconds", 5, 60, "%d s",
                        "rewind length seconds memory");
  portSettingsAddChoice("System", "Rewind button", "A controller button that rewinds while held.", 0, "rewind_pad",
                        kPadChoices, 4, "rewind button controller gamepad paddle share");
  portSettingsAddSection("System", "Rewind key", nullptr, 0, section, "rewind key keyboard backspace");
  portSettingsAddOverlay(tick, event);
  return true;
}();

} // namespace
