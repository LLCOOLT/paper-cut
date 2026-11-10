// Shared by the settings page and the launcher (port/settings/*.cpp).
#pragma once

// The settings' tabs, inside the current ImGui window. inGame: the running
// game's own tabs too (save states, save warps).
void portSettingsTabs(bool inGame);

// The launcher is on screen (launcher_ui.cpp): touches drive its ImGui
// windows, as they do the settings page's (settings_ui.cpp, touchToMouse).
extern bool gPortLauncherShown;

// The launcher or the settings page gone: ImGui lets go of the keyboard and
// controller (no focused window, keyboard/controller navigation off), or
// Aurora keeps giving it the keys and the game gets none.
void portUiReleaseInput();

namespace portui {
// A note in the page's grey, wrapped to the window: English (translated here)
// or text already translated.
void hint(const char* text);
// SameLine() for the next control when it still fits the row (width: its
// width), else a new row: buttons chained on one line stay on the page at
// phone width and with long translations. label: a button's text (its width
// worked out with the frame padding), or use the width form.
void sameLineOr(float width);
void sameLineOrButton(const char* label);
void sameLineOrSmallButton(const char* label);
} // namespace portui

// The last crash report sent with a click (port/settings/features/crash_report.cpp):
// Report on GitHub (a filled-in issue opened in the browser) and Save as a zip...,
// in the current ImGui window; the text with the player's name taken out of paths.
// Nothing is sent without a click. Test: portCrashReportTest ($TTYD_CRASH_REPORT_TEST).
#include <string>
void portCrashReportButtons();
void portCrashReportTest();
std::string portCrashScrub(std::string text);

// The recovery screen at the next start after a crash (crash_report.cpp), once per crash
// (portCrashReportPending): the launcher's (launcher_ui.cpp, every system), or the game's own over
// its title when the launcher was left out (settings_ui.cpp: a disc given at start, $TTYD_NO_LAUNCHER).
// What its Continue loads: quick resume's save of the build that crashed.
struct PortRecoveryResume {
  bool have = false;
  std::string gameId;    // "G8ME01"
  std::string path;      // resume_<id>.gci
  int slot = 3;          // the save file it came from (0-3)
  std::string map;       // where it was made ("gor_01"), "" unknown
  std::string area;      // the area's name there (the game's text, UTF-8), "" unknown
  long long before = -1; // seconds between it and the crash, -1 unknown
};
// The crashed build's game ID, from the report's "version: US (G8ME01)" line ("" unknown).
std::string portCrashReportGameId();
// gameId's quick resume save, timed against the last crash's report.
PortRecoveryResume portRecoveryResume(const char* gameId);
// The screen, centred over the display (modal: a popup nothing under it takes input from; else a
// window, its text scaled by scale; inside, if given, is called in it). 0 while it's up, 1 Continue,
// 2 Dismiss.
int portRecoveryDraw(const PortRecoveryResume& resume, float scale, bool modal, void (*inside)() = nullptr);
