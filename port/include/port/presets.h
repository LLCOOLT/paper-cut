#ifndef PORT_PRESETS_H
#define PORT_PRESETS_H

/* The gameplay presets (port/settings/features/enhancement_presets.cpp) for the code that shows them: the
 * launcher's first-run picker (port/settings/launcher_ui.cpp), the settings page's Presets, and the Start
 * menu's presets page (port/src/aurora/pause_presets_aurora.c). Text is English (translate as shown). */

#ifdef __cplusplus
extern "C" {
#endif

/* The presets in their order; index i's English name, its tagline (a few words, on its tile), its one
 * sentence (the detail line), and the item whose icon is its picture. */
int portPresetCount(void);
const char* portPresetName(int i);
const char* portPresetTagline(int i);
const char* portPresetAbout(int i);
int portPresetItem(int i);
/* Its group (0 Classic, 1 Helpful, 2 Challenge, 3 Special), a group's English name, and the presets as
 * shown: the k-th tile's preset (grouped; -1 past the last). */
int portPresetGroup(int i);
const char* portPresetGroupName(int group);
int portPresetShown(int k);
/* It's one of the first-run picker's (Vanilla, Vanilla+, Remake rules, Enhanced, Story, Challenge,
 * Paper Mario 64). */
int portPresetInPicker(int i);
/* It can be applied now (Paper Mario 64: a ROM linked; none while Console-accurate mode is on). */
int portPresetAvailable(int i);
/* The preset the settings match now, or -1 (Custom): then *nearest is the nearest and *differ how many
 * settings differ from it (either may be NULL). */
int portPresetNow(int* nearest, int* differ);
/* Its settings put in place and saved (nothing if it isn't available). */
void portPresetApply(int i);
/* The Start menu's: put in place and applied, or why not (English: locked, needs a ROM); NULL when done. */
const char* portPresetUse(int i);
/* An item's icon id (port/src/aurora/pause_presets_aurora.c: the item table's; 0 or less: Mario's head) */
int portPresetItemIcon(int item);
/* The Start menu's presets page, over the Settings tab (pause_presets_aurora.c) */
void portPresetsPageOpen(void);

#ifdef __cplusplus
}
#endif

#endif
