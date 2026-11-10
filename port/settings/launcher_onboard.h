// The launcher's onboarding (port/settings/features/launcher_onboard.cpp): the "What's new" card on the
// first launch after an update (again from About), the "Have Paper Mario 64? Link it" card on the Play tab
// while no ROM is linked, and the "How multiplayer works" page (the "?" on the Multiplayer tab). Each in
// the launcher's paper cards and torn buttons, for a controller, the keyboard, the mouse and touch.
#pragma once

#include <imgui.h>

// The What's new card is due (the first launch after an update; $TTYD_LAUNCHER_NEWS=1 always, =0 never).
bool launcherNewsDue();
void launcherNewsOpen();
// The help page, from the "?" (launcherHelpButton: the button, true when pressed).
void launcherHelpOpen();
bool launcherHelpButton(float scale);
// A card or page over the launcher is up: the tabs and the page under it take no input.
bool launcherOverlayShowing();
// Last in the frame, over the stage: the card or page that is up. Returns the launcher tab to go to
// (a line's button: Play 0, Multiplayer 1, Settings 2), or -1.
// ready: the launcher has opened (the pop-up stage is up): What's new comes at the first such frame.
int launcherOverlayDraw(float scale, ImVec2 stageMin, ImVec2 stageMax, bool ready);

// The Paper Mario 64 card is wanted (no ROM linked, not hidden with "Don't show again").
bool launcherPm64CardWanted();
// Its heading and lines (what linking unlocks), under the Play tab's Paper Mario 64 row when its What it adds
// is open; shortCard: one line only; heading: with its heading (the row says it already: false). Settings
// and "Don't show again" (launcherPm64CardHide) follow it.
void launcherPm64CardBody(float scale, bool shortCard, bool heading = true);
void launcherPm64CardHide();

// A new file's Start in Rogueport, said under its buttons: what it gives.
const char* launcherRogueportNote();
