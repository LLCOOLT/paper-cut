#ifndef PORT_MODES_HUB_H
#define PORT_MODES_HUB_H

/* The Start menu's Modes hub (port/src/aurora/modes_hub_aurora.c): the
 * Settings tab's Modes page opens on a picture button for each group of the
 * Modes tab (port/settings/settings_layout.cpp, its GROUP lines), and a
 * button opens that group's list (port/src/aurora/pause_settings_aurora.c).
 *
 * A group with no entry in the hub's table (modes_hub_aurora.c, kButtons)
 * still gets its button, named as its group, with the Modes page's picture:
 * a new mode's group needs only its line in settings_layout.cpp, and a line
 * in kButtons (and a picture in port/assets/luigi/art/recipes/modes_hub.recipe)
 * when it wants a picture or a shorter name of its own. */

#ifdef __cplusplus
extern "C" {
#endif

/* The hub button a group of the Modes tab is shown under: its own, or
 * another group's (a small group folded into a bigger one). English. */
const char* portModesHubButton(const char* group);
/* The button's name on the hub (English): a shorter one than its group's, or the group's. */
const char* portModesHubName(const char* button);
/* The button's picture, made from the disc by recipe at Preparing graphics:
 * 1 and *texObj (a GXTexObj) set, its size in w and h; 0 when there is none
 * (then *icon: one of the game's icons to draw instead, or -1). */
int portModesHubPicture(const char* button, void* texObj, float* w, float* h, int* icon);

#ifdef __cplusplus
}
#endif

#endif
