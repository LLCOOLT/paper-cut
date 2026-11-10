// Back to the launcher (port/include/port/launcher_back.h): its rows. The Start
// menu's Settings tab, General: a row that opens the question in the game's
// own window (the Start menu's lines page, port/include/port/pause_forms.h),
// A to go, B to stay; the settings page's General tab: a button and the same
// question in the page's own window. Its binding (Controls > Bindings, F2 at
// first) opens the settings page on its question. Each says what the way out
// costs: "Progress since your last save will be lost." when there is some, and
// that online play is left and a mode ends first.
#include "port/bindings.h"
#include "port/companion_menu.h"
#include "port/launcher_back.h"
#include "port/mode_lending.h"
#include "port/pause_forms.h"
#include "port/replay.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "../launcher_style.h" // (gLauncherFonts: the question's heading)
#include "../settings_ui.h"    // (gPortLauncherShown)

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" void portSettingsToggle(void);    // settings_ui.cpp: the page opened or closed
extern "C" int portSettingsPageIsOpen(void); // settings_ui.cpp
extern "C" void portSettingsOpenAt(const char* tab);
extern "C" void portSettingsShowSearch(const char* label); // (nullptr: no search)

namespace {

// what the question says besides itself (English; translated where it is shown)
const char* const kUnsaved = "Progress since your last save will be lost.";
const char* const kResume = "The launcher's Continue can bring back quick resume's save.";
const char* const kSaved = "Everything is saved: the game goes back to its title, then to the launcher.";
const char* const kOnline = "You leave the online room first.";
const char* const kMode = "The mode you are playing ends first, and what it lent is given back.";

// the lines under the question, as they are now (English)
int notes(const char** out) {
  int n = 0;
  if (portLauncherBackUnsaved()) {
    out[n++] = kUnsaved;
    if (portResumeHave()) {
      out[n++] = kResume;
    }
  } else {
    out[n++] = kSaved;
  }
  if (portStateSyncOn()) {
    out[n++] = kOnline;
  }
  if (portModeLending() != 0) {
    out[n++] = kMode;
  }
  return n;
}

void go(const char* from) {
  const char* why = nullptr;
  std::printf("[launcher-back] confirmed (%s)\n", from);
  std::fflush(stdout);
  if (!portLauncherBackRequest(&why) && why != nullptr) {
    std::printf("[launcher-back] refused: %s\n", why);
    std::fflush(stdout);
  }
}

// --- the Start menu: a row and its question, in the game's windows ---

constexpr int kRow = 1;

int confirmLines(char (*out)[PORT_PAUSE_LINE], int max) {
  const char* say[4];
  const int count = notes(say);
  int n = 0;
  char wrapped[640];
  for (int k = 0; k < count && n < max; k++) {
    if (k > 0 && n < max) {
      out[n++][0] = '\0'; // (a line between them)
    }
    portPauseWrapCols(portTrGame(say[k]), wrapped, sizeof(wrapped), 42);
    for (const char* line = wrapped; *line != 0 && n < max;) {
      const char* end = std::strchr(line, '\n');
      const size_t len = end != nullptr ? size_t(end - line) : std::strlen(line);
      std::snprintf(out[n++], PORT_PAUSE_LINE, "%.*s", int(len < PORT_PAUSE_LINE - 1 ? len : PORT_PAUSE_LINE - 1),
                    line);
      line += len + (end != nullptr ? 1 : 0);
    }
  }
  return n;
}

int confirmButton(int button) {
  if (button != 'A') {
    return 0;
  }
  go("the Start menu");
  return -2; // the whole Start menu closed: the game goes to its title from here
}

const char* confirmHelp(void) { return "{A}: back to the launcher. {B}: stay in the game."; }

const PortPauseInfo kConfirm = {"Back to the launcher?", confirmLines, confirmButton, confirmHelp, nullptr, nullptr, 0};

int formRows(PortPauseRow* out, int max) {
  if (max < 1) {
    return 0;
  }
  out[0] = portPauseRow("Back to the launcher",
                        "The launcher's screen, without closing Paper Cut: it asks first, and says if there is "
                        "progress since your last save.",
                        nullptr, kRow, portLauncherBackBusy() ? PORT_PAUSE_ROW_DIM : 0u);
  return 1;
}

const char* formAct(int id, int dir) {
  if (id != kRow || dir != 0) {
    return nullptr;
  }
  if (gPortCompanionMenu) {
    return "Not on the second screen: use the game's Start menu.";
  }
  if (portReplayPlaying()) {
    return "Not during a replay.";
  }
  if (portLauncherBackBusy()) {
    return "On its way to the launcher...";
  }
  std::printf("[launcher-back] the Start menu's question (%s)\n",
              portLauncherBackUnsaved() ? "progress since the last save" : "saved");
  std::fflush(stdout);
  portPauseInfoOpen(&kConfirm);
  return nullptr;
}

// (PORT_PAUSE_FORM_OWN: in a replay, where the Start menu plays back as recorded, a dimmed row instead)
const PortPauseForm kForm = {"General", "Back to the launcher", formRows, formAct, PORT_PAUSE_FORM_OWN};

// --- the settings page: a button and its question, in the page's window ---

bool sAsk; // the question to open at the page's next frame (its button, the binding)

// The binding's (and the test's) way in: the page on its General tab, where the question is drawn, whichever
// tab or search it showed (open already or not), and the question asked there
void askOnPage() {
  portSettingsOpenAt("General");
  portSettingsShowSearch(nullptr); // (a search's results draw only the sections that match)
  sAsk = true;
}

void question() {
  constexpr const char* kId = "###launcher_back";
  if (sAsk) {
    sAsk = false;
    ImGui::OpenPopup(kId);
    std::printf("[launcher-back] the settings page's question (%s)\n",
                portLauncherBackUnsaved() ? "progress since the last save" : "saved");
    std::fflush(stdout);
  }
  const ImVec2 d = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(d.x * 0.5f, d.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(d.x - 40.f < 600.f ? d.x - 40.f : 600.f, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f, 20.f));
  const bool open = ImGui::BeginPopupModal(kId, nullptr,
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoTitleBar);
  ImGui::PopStyleVar();
  if (!open) {
    return;
  }
  ImGui::PushFont(gLauncherFonts.heading);
  ImGui::TextWrapped("%s", portTr("Back to the launcher?"));
  ImGui::PopFont();
  ImGui::Spacing();
  const char* say[4];
  const int count = notes(say);
  for (int k = 0; k < count; k++) {
    if (say[k] == kUnsaved) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.16f, 0.2f, 1.f)); // (the warning stands out: the page's red)
      ImGui::TextWrapped("%s", portTr(say[k]));
      ImGui::PopStyleColor();
    } else {
      ImGui::TextWrapped("%s", portTr(say[k]));
    }
  }
  ImGui::Spacing();
  ImGui::Spacing();
  if (ImGui::Button(portTrLabel("Back to the launcher##launcher_back_go"))) {
    ImGui::CloseCurrentPopup();
    if (portSettingsPageIsOpen()) {
      portSettingsToggle(); // (the page closed: the game goes to its title under it)
    }
    go("the settings page");
  }
  portui::sameLineOrButton(portTr("Stay in the game"));
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere(); // (a controller's A or Return on its own: Stay, the safe answer)
  }
  if (ImGui::Button(portTrLabel("Stay in the game##launcher_back_stay"))) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void section(int inGame) {
  const bool can = inGame && portLauncherBackAvailable() && !portLauncherBackBusy();
  ImGui::BeginDisabled(!can);
  if (ImGui::Button(portTrLabel("Back to the launcher...##launcher_back"))) {
    sAsk = true;
  }
  ImGui::EndDisabled();
  if (portLauncherBackBusy()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("On its way to the launcher..."));
  } else if (inGame && portReplayPlaying()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("Not during a replay."));
  } else if (portLauncherBackUnsaved()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", portTr("(progress since your last save)"));
  }
  question();
}

// $TTYD_LAUNCHER_BACK_ASK=<frame> (tests): the settings page opened on the question at that presented frame,
// as the binding opens it
void tick(int pageOpen) {
  static const char* sAt = SDL_getenv("TTYD_LAUNCHER_BACK_ASK");
  static long sFrame;
  if (sAt != nullptr && ++sFrame == std::atol(sAt)) {
    std::printf("[launcher-back] TTYD_LAUNCHER_BACK_ASK: the question\n");
    std::fflush(stdout);
    askOnPage();
  } else if (!pageOpen) {
    sAsk = false; // (the page closed before it drew the question: not opened later, unasked)
  }
}

const bool sAdded = [] {
  portSettingsAddSection("General", "Back to the launcher",
                         "The launcher's screen, without closing Paper Cut. It asks first, and says if there is "
                         "progress since your last save; online play is left and a mode ends first. {LAUNCHER} asks "
                         "from anywhere in the game.",
                         PORT_SETTING_IN_GAME, section,
                         "back to the launcher return launcher quit exit leave main menu disc another version "
                         "close game");
  portPauseAddForm(&kForm);
  portSettingsAddOverlay(tick, nullptr);
  portBindingsSetHandler(PORT_BIND_LAUNCHER, [](int down, int repeat) {
    if (!down || repeat || gPortLauncherShown || !portLauncherBackAvailable() || portLauncherBackBusy()) {
      return 0;
    }
    askOnPage();
    return 1;
  });
  return true;
}();

} // namespace
