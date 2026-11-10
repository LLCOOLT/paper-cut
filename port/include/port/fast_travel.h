#ifndef PORT_FAST_TRAVEL_H
#define PORT_FAST_TRAVEL_H

/* Fast travel ("fast_travel", Enhancements, off by default): from the
 * Journal's map, to a room Mario has been in, by a way in he has come or gone
 * through (port/src/common/fast_travel_pc.c; the pages: the warp map's
 * travel mode, port/src/aurora/warp_map_aurora.c). Not practice: no
 * achievement is held back.
 *
 * What Mario has seen is the area maps' record (port/include/port/area_maps.h:
 * the user folder, beside the memory cards; nothing in the save, so the card
 * stays the GameCube's and Dolphin's), as Mario himself: it is kept whether
 * fast travel is on or off, as the area maps always were. A file whose record
 * has none of a hub (a file begun elsewhere, or before the record) has the
 * hubs its story has passed: Rogueport's plaza, Petalburg, Boggly Woods ...
 * each by its first way in.
 *
 * A trip is one Mario could make on foot now:
 * - leaving: in the field, a room on the area maps, as Mario (not Peach or
 *   Bowser), nobody following him (a follower isn't left behind), once the
 *   prologue is over, and not inside a stretch the story keeps him in (the
 *   table in fast_travel_pc.c, by the story's own values: the Excess Express,
 *   Glitzville in chapter 3, Keelhaul Key in chapter 5, Riverside and Poshley
 *   in chapter 6, the Moon and the X-Naut Fortress in chapter 7, the Palace of
 *   Shadow from the Shadow Queen's chamber on, Twilight Town while Doopliss has
 *   Mario's name, Hooktail Castle's escape, the Great Tree's time bomb). The
 *   Pit of 100 Trials, the Glitz Pit's ring and the Bowser stages are off the
 *   area maps, so never left from.
 * - arriving: a room Mario has been in, through a way in he knows, where the
 *   story lets him walk to now (the same table: the train never, Keelhaul Key
 *   once it can be sailed to any time, the Moon after chapter 7, never the
 *   X-Naut Fortress, Riverside Station or Cortez's ship).
 * The trip waits for a scene or a battle to end (online: a shared one); it is
 * dropped at the title or a Game Over. In a replay (recorded or played) it is
 * off, so a playback stays as it was recorded. Online, fast_travel stays each
 * player's own setting and a trip is let only while every player playing has
 * it on (portStateSyncAllTravel: each game's PLAYER messages carry it); each
 * game then travels on its own as it walks. A trip asked for waits until Mario
 * has his controls in the field, and both refusals are checked again then. A
 * new game in a file starts its record afresh (area_maps_pc.c), so only rooms
 * of this game count.
 *
 * Plain C, every build. */

#ifdef __cplusplus
extern "C" {
#endif

/* the setting */
int portTravelOn(void);
/* Why Mario can't leave now (English), or NULL. */
const char* portTravelDepartRefusal(void);
/* Why Mario can't arrive in room (portAreaRooms' index) now, or NULL (leaving aside). */
const char* portTravelArriveRefusal(int room);
/* The story's part of the departure refusal alone (the stretches the story keeps Mario in, the ending,
 * someone following him), whatever the setting says and wherever he is: the modes, which leave the
 * room for a map of their own and come back to it (port/include/port/modes.h). English, or NULL. */
const char* portTravelStoryKeeps(void);
/* The story lets Mario arrive in map now (its rows; a room on no area map: yes). The remake's hub pipe. */
int portTravelStoryLets(const char* map);
/* Mario has been in the room (or its story says so) / knows the way in (portAreaExits' index). */
int portTravelRoomKnown(int room);
int portTravelWayKnown(int exit);
/* Asks for the trip (both refusals checked); NULL when it's on its way, else why not. */
const char* portTravelRequest(int exit);
/* gameplus_pc.c, each frame: a trip asked for, made once the field is free. */
void portTravelFrame(void);

#ifdef __cplusplus
}
#endif

#endif
