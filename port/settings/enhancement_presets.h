// The gameplay presets (port/settings/features/enhancement_presets.cpp), for the launcher's first-run
// picker (launcher_ui.cpp): "How do you want to play?" on a new install's first start. The list itself
// (names, taglines, groups, what is in place now) is port/include/port/presets.h.
#pragma once

#include "port/presets.h"

#include <imgui.h>

// The picker is due: a new install's first start (no settings.txt found), not shown before
// ($TTYD_PRESET_PICKER=1 shows it anyway: tests).
bool portPresetPickerDue();
// It was answered (a preset or "Choose later"): never shown again.
void portPresetPickerDone();

// A preset's picture, size wide and high from at: its item's icon (the game's own: from icon.tpl in the
// game, from the art cache in the launcher, art/recipes/launcher_presets.recipe; Paper Mario 64's
// Goombario from the linked ROM), or, before there is one, its initial on a paper token. dim: not
// available now.
void portPresetDrawIcon(ImDrawList* dl, int i, ImVec2 at, float size, bool dim = false);
// The launcher's: the folder its art cache keeps the presets' icons in (<user>/art_cache/<game id>/
// art/launcher/presets/), "" for none (no disc yet).
void portPresetIconDir(const char* dir);

// A Paper Mario ROM was just linked (pm64_link.cpp): "Turn on all Paper Mario 64 content?" is asked
// (where the link was made: the launcher, the settings page, the Start menu's form) until answered.
// Yes turns the Everything Paper Mario 64 switch on; No leaves the settings as they are.
void portPresetPm64Linked();
bool portPresetPm64AskPending();
void portPresetPm64Answer(bool yes);
// the question and its note (English)
extern const char* const kPresetPm64Ask;
extern const char* const kPresetPm64AskNote;
