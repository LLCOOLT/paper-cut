#ifndef PORT_PLAYERS_H
#define PORT_PLAYERS_H

/* How many play (docs/PLAYERS.md): Mario's player and up to three more, on
 * one computer (controllers 2 to 4, with Second player on), or in a shared
 * battle online (port/include/port/statesync.h). Player 1 is
 * Mario; player 2 the partner (in Brothers mode, Luigi: then nobody walks
 * the partner and its turns are player 1's); player 3 the follower (the
 * story's, or a companion: another partner, as a follower); player 4 a
 * companion of their own. In battle each plays their own character's turns
 * (Luigi's are player 2's; the partner's are player 3's or 4's when it's
 * their companion's partner).
 *
 * Both come from the frame's controllers as played (a shared battle's: the
 * same in every game of it), so what changes play may follow them; read them in
 * the game's frame, not in drawing. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_PLAYERS_MAX 4

/* The port's own party slots (src/party.c's partyPtrTbl), past the game's
 * partner (0) and follower (1), one for each mode so they run together: player
 * 4's companion, Brothers mode's Luigi (port/include/port/brothers.h) and
 * player 2's lent partner while Mario has none yet (docs/PLAYERS.md). The
 * game's code never hands them out, and no script, save or menu sees them. */
#define PORT_PARTY_SLOT_COMPANION 2
#define PORT_PARTY_SLOT_LUIGI 3
#define PORT_PARTY_SLOT_LENT 4
#define PORT_PARTY_SLOTS 5

/* Enemies notice every player's character in the field, not only Mario
 * (docs/PLAYERS.md): the partner while player 2 walks it, players 3 and 4's
 * companions and lent partners, Brothers mode's Luigi. Between Begin and End
 * (npcdrv.c's find checks, evtmgr.c's run of an enemy's events) an enemy sees
 * Mario where the nearest of them stands, as online play's
 * portStateSyncNpcTargetBegin does; fbatEncountCheck asks portCoopFieldTouch
 * after its own checks found nothing: an enemy touching one of them starts the
 * battle as a plain contact (no first strike either way, no Bump Attack:
 * portCoopTouchedBy). Single player: nothing changes. */
struct NpcEntry;
void portCoopNpcTargetBegin(struct NpcEntry* npc);
void portCoopNpcTargetEnd(void);
struct NpcEntry* portCoopFieldTouch(struct NpcEntry* found, void* kind); /* kind: the hit's u32 */
int portCoopTouchedBy(const struct NpcEntry* npc);

/* the players now: 1 (Mario's alone) to PORT_PLAYERS_MAX */
int portCoopPlayers(void);
/* player (0 to 3) plays now: player 0 always; the others with their
 * controller there */
int portCoopPlayerHere(int player);
/* the story's follower's party id (an escort, a Trouble's client), -1 none: marioGetExtraPartyId(), except
 * that player 3's companion, out in the follower's slot, is no follower (second_player_pc.c) */
int portCoopStoryFollowerId(void);

/* A seated controller disconnecting (a battery, a cable), on one computer: the seat is kept for 60 s,
 * their character waiting at Mario's side, a note at their place; the controller coming back (or any
 * unseated one's Start) takes it again, and after that time the seat frees. Frames left of port's
 * (1-3) kept seat, 0 none. Never online; a replay keeps its own controllers' seats the same way. */
int portCoopSeatHeld(int port);
/* Player 1's controller disconnected (Aurora, one computer): the game holds after this frame
 * (vi_aurora.c, as frame advance holds it) with "1P's controller disconnected" until a Start on any
 * controller or the keyboard. portCoopPadLost: 1 from the frame it went; portCoopPadLostWait (the
 * hold's loop): reads the controllers, 0 once a Start came (that controller is then player 1's). */
int portCoopPadLost(void);
int portCoopPadLostWait(void);

/* Tests (an input script's "where coop"; tools/port/testpass.py's mp_ scenarios): one "[mp] where:" line with
 * the map, story, sequence, battle, control, menus, the players and each one's character (place, motion, a
 * hop or fall in progress), ending "settled" or "NOT SETTLED: <why>" (someone mid-move, a menu up, no
 * control). Online every game prints its own; testpass compares them (the online check "same_where").
 * 1 when settled; print 0: only that, no line (where coop MAX waiting for it). */
int portCoopWhere(int print);

/* The players' colours (the cards, the tags over heads, the lent partners' tints, the controller's
 * light): player 0-3's as 0xRRGGBBAA, by the Player colours setting (Standard; High contrast: hues
 * that stay further apart for every common colour blindness). Every tinted thing also carries its
 * player's number ("2P" & co.), so no colour has to be told by its hue alone. */
unsigned portPlayerColour(int player);

#ifdef __cplusplus
}
#endif

#endif
