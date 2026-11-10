#ifndef PORT_MODE_LEAVE_H
#define PORT_MODE_LEAVE_H

/* Leaving a mode, one way for all of them (port/src/aurora/mode_leave_aurora.c).
 *
 * While a mode runs in the field, the Start menu's Settings tab opens on "Leave <mode>" first (A twice, as
 * Restart now: port/src/aurora/pause_settings_aurora.c); it does the mode's own stop and return (the lent
 * pouch back, Mario back where the mode started, the file's words back). A mode's start refused because
 * another runs asks "Press A again to leave <it> and start <this>" instead: yes leaves the one running, the
 * menu closes, and the new start is asked again once the old one is over (portModeSwitchQueue).
 *
 * The modes it knows: the boss rush, the endless Glitz Pit, a boss refight, the action-command dojo, a battle
 * puzzle, the Paper Tower, a chapter replay, the Pit of 100 Trials in co-op and versus (its ladder too). The
 * raid, the exhibition matches, the quiz and the card duels are a fight or a screen of their own from start
 * to end: the Start menu isn't open during them (fleeing or losing ends them). Master Quest is the file's own
 * rules, not a run.
 *
 * Online: a co-op guest's game shows the host's mode, so a guest can't leave it (the help box says so). */

#ifdef __cplusplus
extern "C" {
#endif

/* The mode running now that can be left: its name (English, a Modes section's: "Boss rush"), or NULL. */
const char* portModeLeaveName(void);
/* Its Start menu row (English, a constant: "Leave the boss rush"), or NULL. */
const char* portModeLeaveLabel(void);
/* Leaves it now (the game's frame, Mario free in the field or in the mode's battle): 1, or 0 and why (English)
   when it can't (an online guest, none running). Never from inside the Start menu: its stop changes the map
   under the menu's closing, and the game stayed black (portModeLeaveAsk waits for the menu to close). */
int portModeLeave(const char** why);
/* Why portModeLeave would refuse now, or NULL (no side effects). */
const char* portModeLeaveRefusal(void);

/* After a leave: act(id, 0) (a Start menu form's act on its start row) asked once the left mode is over and
 * Mario is free in the field, up to about 20 s; its answer is logged ("[modes] switch: ..."). */
void portModeSwitchQueue(const char* (*act)(int id, int dir), int id, const char* name);
/* The leave asked for (any thread: the settings page, the Start menu): done on the first game frame the Start
   menu is closed (or in the mode's battle), then, with act, the start as portModeSwitchQueue; a refusal of the
   leave is logged and kept for portModeLeaveAskRefusal (NULL: none, or not tried yet). */
void portModeLeaveAsk(const char* (*act)(int id, int dir), int id, const char* name);
const char* portModeLeaveAskRefusal(void);
/* Each game frame (modes_aurora.c's frame hook). It also leaves a mode from its battle, where the Start menu
   doesn't open: player 1's Start in a mode's battle asks ("Leave the boss rush: press Start again", over the
   picture), a second Start within 4 s leaves it, the battle ended as run away. */
void portModeSwitchFrame(void);
/* At start (modes_aurora.c): the in-battle ask's drawing. */
void portModeLeaveInit(void);

/* A start's refusal because another mode runs (the texts the modes give): 1 if why is one of them. */
int portModeAnotherRuns(const char* why);

#ifdef __cplusplus
}
#endif

#endif
