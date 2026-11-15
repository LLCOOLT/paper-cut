#ifndef PORT_SPECIAL_PLACES_H
#define PORT_SPECIAL_PLACES_H

/* Special places (GitHub #48; port/src/common/special_places_pc.c): rooms only the story's scenes
 * reach, visited from the Journal's map with fast travel on ({X} there, or the "Special places" tab:
 * port/src/aurora/warp_map_aurora.c), each once its scene has been seen (the story's own value):
 *
 *   Mario's house (aaa_00)        as the player's character, the partner along; the yard's pipe
 *                                 leads back to where the trip began
 *   Bowser's Castle (kpa_01)      as Bowser (the castle's own script makes the player Bowser), the
 *                                 hall to walk and its guards to talk to
 *   Bowser's stages 1, 2 and 3    played as Bowser, with the stage's own controls, timer and lives:
 *   (kpa_02, kpa_03-04, kpa_05-06) a fall or the clock runs the stage again, the goal comes home
 *
 * A visit changes nothing of the file: the save words (GSW, GSWF and the story) are kept as they
 * were when the trip began and put back as it ends, however it ends (the goal, the pipe, Bowser's
 * pause, a fast-travel trip or a practice warp onward), so no scene the room would play then is
 * ever let run (each room's script checks the story itself, and no story value it plays a scene on
 * is one a trip can start from). The way back: the pipe in Mario's yard; the hall's far door (Bowser
 * has no pause in his castle's 3D rooms); {X} on Bowser's pause in a stage (or a click or tap on its
 * line); the Journal's "Back to where you came from"; any fast-travel trip
 * on from Mario's house. Leaving Bowser's places, the player is Mario (or the pick) again and the
 * partner is called back. No save is made there: the rooms have no Save Block, and quick resume
 * makes none in them.
 *
 * Not in Console-accurate mode (fast travel itself is locked off there); never online or in a replay
 * (as the warp map). Left out: Peach's rooms in the X-Naut Fortress (her interludes' scenes run by
 * the story value and her disguise, and the fortress is one-way in the story), and rooms that are
 * a scene only (Bowser's Castle from outside, kpa_00; the Rawk Hawk scene, kpa_07; the opening
 * boat and Rogueport's harbour scene).
 *
 * For now the list has Mario's house only: Bowser's places are kept in the code but off the list (their
 * entry's "listed" in special_places_pc.c), as a visit there left the player without control.
 *
 * Plain C, every build. */

#ifdef __cplusplus
extern "C" {
#endif

struct EventEntry;

/* the places in the list's order */
int portSpecialCount(void);
/* its name (English, for portTrGame) */
const char* portSpecialName(int i);
/* seen in the story (shown in the list) */
int portSpecialSeen(int i);
/* why the trip can't be made now (English), or NULL */
const char* portSpecialRefusal(int i);
/* asks for the trip (made once Mario has the controls in the field): NULL, else why not */
const char* portSpecialRequest(int i);
/* a visit is on (its place's index), else -1 */
int portSpecialVisiting(void);
/* the map is one of the special places' rooms */
int portSpecialRoom(const char* map);
/* back to where the visit began: NULL when asked for, else why not */
const char* portSpecialLeave(void);
/* each frame (fast_travel_pc.c's portTravelFrame): a trip or a way back asked for, the partner
 * called back */
void portSpecialFrame(void);
/* the trip about to be made is the player's own (fast travel, a practice warp): a visit ending on it
 * goes where it goes */
void portSpecialLeaving(void);
/* seqdrv.c seqSetSeq: a map change (where to, rewritable) or another sequence */
void portSpecialMapChange(const char** map, const char** bero);
void portSpecialSeq(int seq);
/* win_main.c, Bowser's pause each frame (this frame's presses): {X} or a click or tap on its line
 * leaves a visit; 1 when it asked to (the pause then closes) */
int portSpecialBowserPause(int buttons);
/* the line Bowser's pause shows on a visit (English), or NULL; drawn centred, its top at this y (the 2D
 * camera's: up from the middle) by portSpecialBowserPauseDisp (warp_map_aurora.c; Aurora only) */
const char* portSpecialBowserPauseLine(void);
#define PORT_SPECIAL_LINE_TOP (-150.0f)
void portSpecialBowserPauseDisp(void);

/* Event commands for the rooms' own scripts (rels/aaa, rels/kpa):
 * CALL(portEvtSpecialVisit, LW(n)): LW(n) = 1 on a visit to this room;
 * CALL(portEvtSpecialPipe, PTR(bero_entry_data)): Mario's yard pipe leads back on a visit (none else). */
int portEvtSpecialVisit(struct EventEntry* event, int isFirstCall);
int portEvtSpecialPipe(struct EventEntry* event, int isFirstCall);
/* CALL(portEvtSpecialBack): a visit's way back now (Bowser's hall's far door) */
int portEvtSpecialBack(struct EventEntry* event, int isFirstCall);

#ifdef __cplusplus
}
#endif

#endif
