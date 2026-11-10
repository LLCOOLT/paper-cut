#ifndef PORT_WARP_MAP_H
#define PORT_WARP_MAP_H

/* The warp map (Practice #4's warp to any room, on a map): a Start menu page
 * in the game's windows and font (port/src/aurora/warp_map_aurora.c).
 *
 * - The world: the Journal's world map (the disc's own w/<lang>/map7.tpl, read
 *   as the Journal reads it), a dot of the Journal's on each area where the
 *   Journal puts it (src/window/win_log.c's tubuDt), the area's name in the
 *   game's text.
 * - An area: its rooms as the game's paper cut-outs on the area's grid, joined
 *   the way their exits join them (port/include/port/area_maps.h, from the
 *   rels' BeroEntry tables), the ways out of the area as stubs.
 * - A room's ways in: the safe spawn first, then each exit by its direction.
 *   A warps there (portRoomWarpRequest: the list's own rules, refused online,
 *   in a replay or out of the field; the run counts as practice).
 * The stick or the D-pad move the cursor to the nearest dot or room that way,
 * the mouse and touch pick (a click on the picked one is A), B goes back a
 * level, X opens the room list (the full settings page).
 *
 * Reached from the Start menu's Settings tab, Enhancements, Field, "Warp map", and
 * from the full settings page's warp (its List / Map choice).
 *
 * Tests: $TTYD_WARPMAP_AREA=<area> opens on that area's dot; $TTYD_WARPMAP_TOUR=1
 * shows every area in turn once the page opens, a picture of each
 * (warpmap_world_<area>, warpmap_area_<area>), then "[warpmap] tour done". */

#ifdef TTYD_BACKEND_AURORA
#include <dolphin/gx.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* the page can open now: in the field, not online, in a replay or on the second screen */
int portWarpMapAvailable(void);
/* why it can't (English, for the help box or the page), or NULL */
const char* portWarpMapRefusal(void);
/* opens it over the Start menu's Settings tab (the menu is open there) */
void portWarpMapOpen(void);
/* the full settings page's Map: the Start menu opens on the map once the page closes
 * (port/src/common/practice_more_pc.c keeps the request) */
void portWarpMapRequest(void);
int portWarpMapRequested(void); /* 1 once, and clears it (a request older than ten seconds is dropped) */
/* winMain (src/window/win_main.c), the Start menu closed: 1 when it should open on the Settings tab
 * for the map (the request taken) */
int portWarpMapPoll(void);
/* the Settings tab was entered straight (win_root.c) or is shown (pause_settings_aurora.c): 1 when it
 * opened the map */
int portWarpMapOpenIfRequested(void);

/* Fast travel's pages (port/include/port/fast_travel.h), the same map in its travel mode: in the
 * Journal's map (src/window/win_log.c), {A} on a place opens its area with only the rooms Mario
 * has been in (one he can't go to now shaded), then a room's ways in he knows; {A} travels (the
 * menu closes, the trip once Mario has the controls), {B} back to the Journal's map.
 * $TTYD_TRAVEL_AREA=<area> opens that area instead of the cursor's (tests). */
struct WindowWork;
/* 1 when it opened (the place has a room Mario has been in; else the buzzer) */
int portTravelJournalOpen(struct WindowWork* wp, const char* area);
int portTravelJournalMain(struct WindowWork* wp); /* 0 stay, -1 back to the map, -2 the menu closed */
void portTravelJournalDisp(struct WindowWork* wp, float x, float y);
const char* portTravelJournalMsg(const char* tag); /* the help box's ("pc_menu_travel...") */
#ifdef TTYD_BACKEND_AURORA
/* a place's dot on the Journal's map: dark while fast travel is on and Mario has been in none of its rooms */
GXColor portTravelJournalDot(const char* area, GXColor c);
#endif

#ifdef __cplusplus
}
#endif

#endif
