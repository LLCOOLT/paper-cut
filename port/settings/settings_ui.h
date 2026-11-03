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
