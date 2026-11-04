#ifndef PORT_ONLINE_H
#define PORT_ONLINE_H

/* Online co-op. Both players run their own copy of the game from the same
 * start, and each frame's controller input crosses the network (lockstep):
 * player 1's from the host, player 2's from the guest, so both games play
 * out the same frames. Neither sends pictures or the game's state; each
 * needs the disc.
 *
 * - The lobby (port/settings/online_lobby.cpp, the launcher's Online tab):
 *   the host waits, the guest joins with its address; the host sends its
 *   settings and memory card, both start the game.
 * - The connection (port/src/common/online_net_pc.c).
 * - The game's side (port/src/common/online_pc.c): the controllers' input
 *   swapped at the pad read, the state's fingerprint compared now and then,
 *   the session ended (each plays on alone) if they ever differ.
 * The game must play out the same on every computer from the same input:
 * the game clock counts frames (os_time_aurora.c), disc reads finish within
 * their frame, audio is rendered per frame (hw_pc.c), the maths is the
 * GameCube's own (port/include/port/fdlibm_pc.h), and what would change one
 * game alone (save states, fast-forward, the Practice tab's edits, warps) is
 * off. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ONLINE_DEFAULT_PORT 47700
#define PORT_ONLINE_PAD_BYTES 16 /* one controller's state for a frame (PADStatus, extButton too) */
#define PORT_ONLINE_HASH_EVERY 60 /* frames between fingerprints */

/* Who a side is, in the handshake: both must match on the version, the
 * disc's ID and the mods. */
typedef struct PortOnlineHello {
    char version[32]; /* Paper Cut's (TTYD_PORT_VERSION) */
    char game[8];     /* G8MJ01, G8ME01 or G8MP01 */
    char mods[256];   /* the mods in use, as a list */
    char name[32];    /* the player's name, shown to the other */
} PortOnlineHello;

enum {
    PORT_ONLINE_IDLE,
    PORT_ONLINE_LISTENING,     /* host: waiting for someone to join */
    PORT_ONLINE_CONNECTING,    /* guest: reaching the host */
    PORT_ONLINE_HANDSHAKE,     /* checking the two games match */
    PORT_ONLINE_PEER_HERE,     /* host: the guest is here, start when ready */
    PORT_ONLINE_WAITING_SETUP, /* guest: waiting for the host to start */
    PORT_ONLINE_GOT_SETUP,     /* guest: the host's setup came: put it in place, then ready */
    PORT_ONLINE_WAITING_READY, /* host: the setup sent */
    PORT_ONLINE_WAITING_START, /* guest: ready */
    PORT_ONLINE_STARTED,       /* both games run */
    PORT_ONLINE_FAILED         /* see portOnlineStatus */
};

/* The lobby (each launcher frame: portOnlineLobbyPump) */
int portOnlineHost(int port, const PortOnlineHello* me);
int portOnlineJoin(const char* address, int port, const PortOnlineHello* me);
void portOnlineLobbyPump(void);
int portOnlineLobbyState(void);
const char* portOnlineStatus(void); /* English, for portTr */
const PortOnlineHello* portOnlinePeer(void);
int portOnlineRoundTrip(void); /* milliseconds, -1 not measured */
int portOnlineIsHost(void);
/* host, once the guest is here: the setup (online_lobby.cpp makes it) */
int portOnlineSendSetup(const void* setup, size_t len);
/* guest: the setup received, then ready once it's in place */
const void* portOnlineSetup(size_t* len);
void portOnlineGuestReady(void);
/* leaves (why: told to the other side, or NULL) */
void portOnlineCancel(const char* why);
/* this computer's address on its network (for the host to share), or 0 */
int portOnlineLocalAddress(char* out, size_t size);

/* Rooms on the relay (docs/ONLINE_RELAY.md): the host opens one by its code,
 * the guest joins with the code, no address or router to set up. A direct
 * UDP path is tried first (port/include/port/online_link.h); the relay
 * carries the game only when there's none. */
#define PORT_ONLINE_CODE_CHARS "23456789ABCDEFGHJKMNPQRSTUVWXYZ" /* nothing to read wrong (0/O, 1/I/L) */
/* ROOM-SECRET, shown with a dash: the relay sees only the room's half (its
 * first character names the relay); the secret half makes the session's key
 * between the two games (port/include/port/online_link.h), never sent */
#define PORT_ONLINE_CODE_LENGTH 8
#define PORT_ONLINE_ROOM_HALF 4
/* the relay's address (the build's TTYD_ONLINE_RELAY, or $TTYD_ONLINE_RELAY),
 * "" when there's none */
const char* portOnlineRelay(void);
int portOnlineHostRoom(const char* code, const PortOnlineHello* me);
int portOnlineJoinRoom(const char* code, const PortOnlineHello* me);
/* the room's code ("" when not in one) */
const char* portOnlineRoom(void);
/* frames the input delay needs on top of the round trip's: the relay's
 * batches (PORT_ONLINE_BATCH - 1), else none */
int portOnlineDelayExtra(void);
/* how the session goes: "direct", "direct (UDP)", "relay" (English) */
const char* portOnlineLinkName(void);
#define PORT_ONLINE_BATCH 3 /* frames of input in one relayed message */

/* Recovery (online_pc.c): out of step, the host tells the guest and both
 * games start again (as Restart now), the session beginning again from the
 * host's last save (online.cpp, portOnlineRestartRejoin); the guest, out of
 * step first, asks. Over the reliable stream (a room's relay, or the TCP
 * connection), sealed in rooms. */
void portOnlineSendResync(const void* payload, size_t len);
void portOnlineAskResync(unsigned epoch);
/* guest: the host's, once it's come (kept until the next one) */
int portOnlineResyncArrived(const void** payload, size_t* len);
/* host: the guest asked (for that epoch), -1 when not */
int portOnlineResyncAsked(void);

/* Watching (docs/ONLINE_RELAY.md): a third (and fourth...) joins only to
 * watch, with the room's code or the host's address. It starts from the
 * host's setup, as a guest does, then plays both players' input as the host
 * sends it, in batches (every PORT_ONLINE_WATCH_BATCH frames, with the host's
 * fingerprints), a little behind; it sends nothing, and the players never
 * wait for it. Through a relay, one message reaches every watcher, sealed
 * with a key from the whole code. */
int portOnlineWatchRoom(const char* code, const PortOnlineHello* me);
int portOnlineWatch(const char* address, int port, const PortOnlineHello* me);
int portOnlineWatching(void);
/* host: how many watch now */
int portOnlineWatchers(void);
/* a watcher's: the guest (portOnlinePeer is the host) */
const PortOnlineHello* portOnlineWatchedGuest(void);
/* host, each frame: the two players' input it played (player 1's, player 2's) */
void portOnlineWatchRecord(unsigned frame, const unsigned char* host, const unsigned char* guest);
/* host: its fingerprint after that many frames */
void portOnlineWatchHash(unsigned frame, unsigned long long hash);
/* watcher: the frame's two inputs (1), waiting for them, the game held; 0 no more
 * come; 2 the players start again (a recovery: portOnlineWatchResync's epoch) */
int portOnlineWatchInput(unsigned frame, unsigned char* host, unsigned char* guest);
unsigned portOnlineWatchHave(void);
int portOnlineWatchHostHash(unsigned frame, unsigned long long* hash);
unsigned portOnlineWatchResync(void);
/* a watcher well behind: its frames unpaced until it's near (vi_aurora.c) */
int portOnlineCatchingUp(void);
#define PORT_ONLINE_WATCH_BATCH 30

/* Reconnecting: a session's connection lost (closed, or quiet), the game
 * waits ("Reconnecting...") and both sides make it again, for up to a minute:
 * a room's relay, its key and the direct path again; by address, the guest
 * connects again. Each then sends the other the input it's missing (both
 * keep theirs), and lockstep goes on from the same frame. */
int portOnlineReconnecting(void);
int portOnlineReconnected(void); /* reconnects so far this session */
/* tests ($TTYD_ONLINE_DROP_AT): this side's connection dropped now */
void portOnlineTestDrop(void);

/* The session's connection */
int portOnlineConnected(void);
void portOnlineSendInput(unsigned frame, const unsigned char pad[PORT_ONLINE_PAD_BYTES]);
/* the other side's input for frame, waiting up to timeoutMs; 0: the session ended */
int portOnlineWaitInput(unsigned frame, unsigned char pad[PORT_ONLINE_PAD_BYTES], int timeoutMs);
void portOnlineSendHash(unsigned frame, unsigned long long hash);
int portOnlinePeerHash(unsigned frame, unsigned long long* hash);
void portOnlinePump(void);

/* The game's side (online_pc.c) */
/* The session begins as the game starts (the lobby's last step): the input
 * delay in frames. */
void portOnlineBegin(int inputDelay);
/* A session runs (the game plays in lockstep). */
int portOnlineActive(void);
/* The pad read (src/sdk/DEMOPad.c): this frame's two players' input. */
struct PADStatus;
void portOnlineApplyPad(struct PADStatus* pads);
/* one controller's state as PORT_ONLINE_PAD_BYTES, and back (replays too) */
void portOnlinePackPad(const struct PADStatus* s, unsigned char* p);
void portOnlineUnpackPad(const unsigned char* p, struct PADStatus* s);
/* A hash of what the game plays out this frame, the same on both computers
 * while they are in step. */
unsigned long long portOnlineFingerprint(void);
/* Each frame, after the game's (vi_aurora.c). */
void portOnlineFrame(void);

#ifdef __cplusplus
}
#endif

#endif
