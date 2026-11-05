#ifndef PORT_PLAYERS_H
#define PORT_PLAYERS_H

/* How many play (docs/PLAYERS.md): Mario's player and up to three more, on
 * one computer (controllers 2 to 4, with Second player on) or online
 * (port/include/port/online.h: the guest and up to two seats). Player 1 is
 * Mario; player 2 the partner (in Brothers mode, Luigi: then nobody walks
 * the partner and its turns are player 1's); player 3 the follower (the
 * story's, or a companion: another partner, as a follower); player 4 a
 * companion of their own. In battle each plays their own character's turns
 * (Luigi's are player 2's; the partner's are player 3's or 4's when it's
 * their companion's partner).
 *
 * Both come from the frame's controllers as played (online: the same in every
 * game of the session), so what changes play may follow them; read them in
 * the game's frame, not in drawing. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_PLAYERS_MAX 4

/* The port's own party slots (src/party.c's partyPtrTbl), past the game's
 * partner (0) and follower (1), one for each mode so they run together: player
 * 4's companion and Brothers mode's Luigi (port/include/port/brothers.h). The
 * game's code never hands them out, and no script, save or menu sees them. */
#define PORT_PARTY_SLOT_COMPANION 2
#define PORT_PARTY_SLOT_LUIGI 3
#define PORT_PARTY_SLOTS 4

/* the players now: 1 (Mario's alone) to PORT_PLAYERS_MAX; online, at least
 * the session's (portOnlinePlayers) */
int portCoopPlayers(void);
/* player (0 to 3) plays now: player 0 always; the others with their
 * controller there (online: the guest, a seat that hasn't left) */
int portCoopPlayerHere(int player);

#ifdef __cplusplus
}
#endif

#endif
