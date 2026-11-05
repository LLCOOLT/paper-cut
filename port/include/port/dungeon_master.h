#ifndef PORT_DUNGEON_MASTER_H
#define PORT_DUNGEON_MASTER_H

/* Dungeon master (port/src/aurora/dungeon_master_aurora.c, docs/DUNGEON_MASTER.md):
 * in the story's battles another player plays the enemies' side. On each
 * enemy's turn they pick which of its own moves it uses and on whom, from a
 * menu in the game's look, and hold its wind-up before the strike; Mario's
 * side plays as ever.
 *
 * The moves come from the enemy's own attack script: before it runs, a look
 * ahead (a copy of the event's state, the game's own handlers for the flow,
 * the battle's getters for what it reads) follows each way its random
 * numbers could go to the move it ends on. The player's pick then stands in
 * for those random numbers, in the order the script asks for them, and for
 * the target the AI would weigh (btlevtcmd_ChoiceSamplingEnemy); what the
 * script decides alone (a boss's phases, its story turns) stays its own.
 *
 * Off by default ("dungeon_master": 0 off, 1..3 the player, 2 to 4, who plays
 * the enemies). Online it is the host's setting, and its player's controller
 * the session's (the guest is player 2), so every game picks the same. */

#ifdef __cplusplus
extern "C" {
#endif

struct EventEntry;

/* A battle with a dungeon master is on: evtmgr_cmd.c's two hooks look (one
 * load and a branch otherwise). */
extern int gPortDmLive;

/* evt_user_func (evtmgr_cmd.c), before a user function runs (first: its
 * first call): 1 when the dungeon master handled it (*result set), 0 for the
 * game's own. */
int portDmUserFunc(struct EventEntry* entry, int first, int* result);
/* A WAIT command about to start (evtmgrCmd): 1 to hold it this frame (the
 * enemy's wind-up, while its player holds A). */
int portDmHold(struct EventEntry* entry);

/* evtmgr_cmd.c: one command of a look-ahead entry (above); -1 when run, else
 * its opcode for the caller. */
int portEvtLookStep(struct EventEntry* entry);

/* The setting's player (1..3, the controller's index; 0 off) */
int portDmPlayer(void);
/* second_player_pc.c: the dungeon master is that player (0..3) in this battle,
 * so a guard on Mario's side stays the first controller's (no player guards
 * their own enemies' hits). */
int portDmPlaysEnemies(int player);

/* mods.cpp: the frame's and the battle's hooks, once */
void portDmRegister(void);

#ifdef __cplusplus
}
#endif

#endif
