/* Online play's network trouble on purpose (tests only; online_faults_pc.c,
 * used by statesync_net_pc.c's connections to the other players).
 *
 * $TTYD_NET_FAULTS=<rule>,<rule>,... (none: nothing changes):
 *   drop:P          P percent of the session's droppable messages never
 *                   go: the other side's wait goes quiet and the
 *                   connection is made again, which must keep the games
 *                   identical (each side sends what the other is missing)
 *   dup:P           P percent of them go twice
 *   reorder:P       P percent of them go after the next one
 *   delay:A-B       every message held A to B milliseconds (in order, as a
 *                   slow line holds a stream: TCP's bytes never pass each other)
 *   disconnect_at:F[/F...]  the connection dropped at those session frames
 *   disconnect_every:N      ... and every N frames
 *   offline:F-G     no connection from session frame F to G (none made again meanwhile)
 *   offline_battle:S-D  no connection for D milliseconds from a shared battle's step S on (this game's)
 *   quiet:MS        the other side quiet this long: reconnect (8000 otherwise),
 *                   so a dropped message costs a short wait in a soak
 *   seed:N          the decisions' random numbers' seed (or $TTYD_NET_FAULTS_SEED;
 *                   each side's own from it, host and guest apart)
 * P may have decimals (drop:0.2). The decisions come from the seed and the
 * messages' order only, never the game's random numbers. */
#ifndef PORT_ONLINE_FAULTS_H
#define PORT_ONLINE_FAULTS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* writes bytes to the connection; 0 when it closed */
typedef int (*PortNetFaultsWrite)(const void* data, size_t len);

/* the rules read (once); 1 when any is on */
int portNetFaultsOn(void);
/* this side: the host or not (the seed's half), once known */
void portNetFaultsSide(int host);
/* one whole message (its length, type and payload) to send: 1 it was taken
 * (sent now, later, twice or never), 0 send it as usual. droppable: the
 * session's input-like messages */
int portNetFaultsSend(const void* message, size_t len, int droppable, unsigned long long nowMs,
                      PortNetFaultsWrite write);
/* the held messages whose time came, out (call it often); 0 when a write failed */
int portNetFaultsPoll(unsigned long long nowMs, PortNetFaultsWrite write);
/* the connection closed or made again: what was on its way is lost */
void portNetFaultsReset(void);
/* the connection to be dropped at this session frame (once per frame) */
int portNetFaultsDisconnectAt(unsigned frame);
/* offline:F-G (frames) or offline_ms:F-D (from frame F, D milliseconds): 1 while the connections stay down
   (tests: a player gone a while, then back) */
int portNetFaultsOffline(unsigned frame, unsigned long long nowMs);
/* offline_battle:S-D: a shared battle reached step S (statesync_battle_pc.c): offline from now for D ms */
void portNetFaultsBattleStep(unsigned step);
/* the wait for the other's input before reconnecting, milliseconds */
int portNetFaultsQuietMs(int usual);
/* what was done so far, for the log: "dropped 12, duplicated 3, ..." */
const char* portNetFaultsSummary(void);

#ifdef __cplusplus
}
#endif

#endif
