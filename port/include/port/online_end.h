/* Where now, when a room is over for this player (port/src/common/online_end_pc.c; docs/STATE_SYNC.md
 * "Leaving and coming back"). Never a silent time-out: a co-op guest whose host left (its Leave, or gone
 * more than 10 seconds), who was removed by the host, or who left, sees why in the game's own window, with
 * "Back to your own adventure" (the guest's own last save, as it was) and "Title screen"; their copy of the
 * host's adventure is kept (coop_save_pc.c). When the online service can't be reached (the relay, before
 * or while playing), "Try again" and "Back". */
#ifndef PORT_ONLINE_END_H
#define PORT_ONLINE_END_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_END_NONE,
    PORT_END_HOST_LEFT, /* the host's Leave, its game closed, or no word from it for 10 seconds */
    PORT_END_REMOVED,   /* the host removed this player */
    PORT_END_LEFT,      /* this player's Leave the room */
    PORT_END_SERVICE,   /* the relay gone while playing together */
    PORT_END_NO_SERVICE /* the relay never reached (Host, Join by code, the launcher's, an invite link) */
};
/* the engine (statesync_coop_pc.inc): this game's room is over; host: the host's name (UTF-8); ownSlot:
   the card file this player played before joining (-1: none, the newest save on the card is theirs) */
void portOnlineEnd(int why, const char* host, int ownSlot);
int portOnlineEndUp(void); /* asking now (or about to, at the next calm moment) */
int portOnlineEndWhy(void);
/* the window taken down unasked (Back to the launcher: the player chose where to go; launcher_back.h) */
void portOnlineEndDrop(void);
/* the window taken down because this game is in a room again (a Host or Join: statesync_pc.c) */
void portOnlineEndStale(void);
/* each game frame (statesync_pc.c portStateSyncGameFrame), on or off line */
void portOnlineEndFrame(void);
/* warp_pc.c: the card's own file loaded at the next chance (the field or the title), as Continue does */
void portWarpRequestSlot(int slot);

#ifdef __cplusplus
}
#endif

#endif
