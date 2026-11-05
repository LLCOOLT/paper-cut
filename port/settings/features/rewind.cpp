// Rewind (Gameplay > Rewind; port/include/port/rewind.h): hold Backspace or a
// controller button (Controls > Bindings, port/include/port/bindings.h), and
// the game steps back through its last seconds; let go and it plays on from
// there. Off by default; the length 5 to 60 seconds. Save states have no
// controller button, so neither has this until one is chosen.
#include "port/bindings.h"
#include "port/rewind.h"
#include "port/pause_forms.h"
#include "port/settings.h"
#include "port/tr.h"

#include "../launcher_style.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>

namespace {

int* sOn;
bool sPageOpen;
bool sDown;

void update() { portRewindHold(sDown); }

// the "<< Rewind" badge while stepping back
void tick(int pageOpen) {
  sPageOpen = pageOpen != 0;
  if (sPageOpen && sDown) {
    sDown = false; // the page took over
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

// the Rewind binding held (Bindings): down, then up
int hold(int down, int) {
  if (down && (!*sOn || sPageOpen)) {
    return 0;
  }
  if ((down != 0) != sDown) {
    sDown = down != 0;
    update();
  }
  return 1;
}

// its key and button (changed under Controls > Bindings), and what the states cost now
void section(int inGame) {
  const char* key = portBindingsKeyName(PORT_BIND_REWIND);
  const char* pad = portBindingsPadName(PORT_BIND_REWIND);
  ImGui::Text("%s%s%s", key[0] != '\0' ? key : portTr("(none)"), pad[0] != '\0' ? "  /  " : "", pad);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", portTr("Change them under Controls > Bindings."));
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

// The Start menu's form (port/include/port/pause_forms.h): its key and button, as Bindings has them
int formRows(PortPauseRow* out, int max) {
  if (max < 2) {
    return 0;
  }
  const char* key = portBindingsKeyName(PORT_BIND_REWIND);
  const char* pad = portBindingsPadName(PORT_BIND_REWIND);
  const char* note = portRewindSupported() ? "Change them under Controls, Bindings." : "Not in online co-op.";
  out[0] = portPauseRow("Rewind key", note, key[0] != '\0' ? nullptr : "(none)", 0);
  std::snprintf(out[0].value, sizeof(out[0].value), "%s", key);
  out[1] = portPauseRow("Rewind button", note, pad[0] != '\0' ? nullptr : "(none)", 1);
  std::snprintf(out[1].value, sizeof(out[1].value), "%s", pad);
  return 2;
}

const char* formAct(int, int) { return "Change them under Controls, Bindings."; }

const PortPauseForm kForm = {"Gameplay", "Rewind key", formRows, formAct};

const bool sAdded = [] {
  sOn = portSettingsDeclare("rewind", 0, 0, 1);
  portPauseAddForm(&kForm);
  portSettingsDeclare("rewind_seconds", 10, 5, 60);
  portSettingsDeclare("rewind_key", SDL_SCANCODE_BACKSPACE, 0, (SDL_SCANCODE_COUNT - 1) | (15 << 10)); // (Bindings' Rewind key)
  portSettingsDeclare("rewind_pad", 0, 0, 3); // the old choice of button, until Bindings sets one
  portSettingsAddToggle("Gameplay", "Rewind",
                        "Hold the Rewind key (Backspace) or button and the game steps back through its last "
                        "seconds; let go to play on from there. Uses some memory and a little time every few "
                        "frames. Not in online co-op.",
                        0, "rewind", "rewind back undo time reverse backspace");
  portSettingsAddSlider("Gameplay", "Rewind length", "How far back Rewind can go.", 0, "rewind_seconds", 5, 60, "%d s",
                        "rewind length seconds memory");
  portSettingsAddSection("Gameplay", "Rewind key", nullptr, 0, section, "rewind key keyboard backspace");
  portSettingsAddOverlay(tick, nullptr);
  portBindingsSetHandler(PORT_BIND_REWIND, hold);
  return true;
}();

} // namespace
