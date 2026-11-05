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
} // namespace portui

// The last crash report sent with a click (port/settings/features/crash_report.cpp):
// Report on GitHub (a filled-in issue opened in the browser) and Save as a zip...,
// in the current ImGui window; the text with the player's name taken out of paths.
// Nothing is sent without a click. Test: portCrashReportTest ($TTYD_CRASH_REPORT_TEST).
#include <string>
void portCrashReportButtons();
void portCrashReportTest();
std::string portCrashScrub(std::string text);
