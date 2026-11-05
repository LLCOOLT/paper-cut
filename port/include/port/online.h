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
/* Three or four players: the host (player 1, Mario), the guest (player 2, the
 * partner) and up to two seats (players 3 and 4, the follower and a companion;
 * second_player_pc.c). A seat joins like a watcher (the host's setup, then each
 * frame as played, port/src/common/online_net_pc.c) and sends its controller to
 * the host, which puts it into the frame it sends next (the host's input
 * carries the seats' too): so the players never wait for a seat, a seat's
 * input is a little later than the others', and a seat that leaves is no
 * controller from a frame every game agrees on. */
#define PORT_ONLINE_MAX_PLAYERS 4
#define PORT_ONLINE_MAX_SEATS (PORT_ONLINE_MAX_PLAYERS - 2)
/* one frame's input of every player */
#define PORT_ONLINE_FRAME_BYTES (PORT_ONLINE_MAX_PLAYERS * PORT_ONLINE_PAD_BYTES)

/* Who a side is, in the handshake: both must match on the version, the
 * disc's ID and the mods. */
typedef struct PortOnlineHello {
    char version[32]; /* Paper Cut's (TTYD_PORT_VERSION) */
    char game[8];     /* G8MJ01, G8ME01 or G8MP01 */
    char mods[256];   /* the mods in use, as a list */
    char name[32];    /* the player's name, shown to the other */
    char lua[24];     /* the Lua mods' hash (port/include/port/lua_mods.h), "" without any;
                         after the rest, so an older hello just has none */
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
/* this side's own hello, as sent (Lua mods compare it with the peer's) */
const PortOnlineHello* portOnlineMe(void);
int portOnlineRoundTrip(void); /* milliseconds, -1 not measured: the median of the lobby's last pings */
int portOnlineRoundTripSamples(void); /* the pings it's from */
int portOnlineIsHost(void);
/* A player's window minimized (vi_aurora.c: Aurora's pause): portOnlineAway(1) tells the other
 * player, and portOnlineAwayPump (each moment while it waits, minimized) keeps the connection and
 * says it again each second; portOnlineAway(0) once it's back. portOnlinePeerAway: the other
 * player's window is minimized (the wait for them says so). */
void portOnlineAway(int away);
void portOnlineAwayPump(void);
int portOnlineAwayWanted(void);
int portOnlinePeerAway(void);
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
/* the build's relays: the i-th's address, or NULL past the last */
const char* portOnlineRelayAt(int i);
/* The room list (the Start menu's Online tab, port/include/port/online_menu.h):
 * the next room this host opens is listed on its relay under name (open to
 * anyone, so the relay is told the code's secret half), or not (NULL). Kept
 * until changed; a listed host refreshes its listing while it waits. */
void portOnlineListRoom(const char* name);
/* Joining from a game already running (the Start menu): such a game starts
 * again to begin the session (a session begins with the game). A guest that
 * gets the host's setup there says so with this goodbye; the host waits in its
 * room (or on its port) again, and portOnlineRejoinExpected says it may start
 * the session by itself once that guest is back (1, cleared by the call). */
#define PORT_ONLINE_BYE_REJOIN "rejoin: starting again to play"
int portOnlineRejoinExpected(void);
/* A host's room (or port) stays open when the one joining leaves before the
 * game or can't play with it (another version, other mods): the latest such
 * news (English, for portTr) and a count that goes up with each (0: none yet). */
unsigned portOnlineHostNews(const char** english);

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
/* A seat (player 3 or 4): joins a room or an address to play, not only watch.
 * It must be there before the host starts; it starts from the host's setup as
 * a watcher does. */
int portOnlineSeatRoom(const char* code, const PortOnlineHello* me);
int portOnlineSeat(const char* address, int port, const PortOnlineHello* me);
/* this side is a seat */
int portOnlineSeating(void);
/* host, in the lobby: the seats there now (0..PORT_ONLINE_MAX_SEATS) */
int portOnlineSeats(void);
/* The player count (the raid boss and the rest read this): the session's
 * players, 2 to PORT_ONLINE_MAX_PLAYERS, fixed when it starts (2 when not
 * online); this side's player (0 the host, 1 the guest, 2 or 3 a seat, -1 a
 * watcher or not online); a player's name ("" not known). */
int portOnlinePlayers(void);
int portOnlineSelf(void);
const char* portOnlinePlayerName(int player);
/* a watcher's: the guest (portOnlinePeer is the host) */
const PortOnlineHello* portOnlineWatchedGuest(void);
/* host, each frame: every player's input it played (portOnlinePlayers() pads, player 1's first) */
void portOnlineWatchRecord(unsigned frame, const unsigned char* pads);
/* host: its fingerprint after that many frames */
void portOnlineWatchHash(unsigned frame, unsigned long long hash);
/* watcher or seat: the frame's input, every player's (portOnlinePlayers() pads) (1),
 * waiting for it, the game held; 0 no more come; 2 the players start again (a
 * recovery: portOnlineWatchResync's epoch) */
int portOnlineWatchInput(unsigned frame, unsigned char* pads);
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
/* this side's controller for frame: to the other player (a seat's, to the host);
 * the host's goes with the seats' as it has them now (portOnlineSeatPads) */
void portOnlineSendInput(unsigned frame, const unsigned char pad[PORT_ONLINE_PAD_BYTES]);
/* host: the seats' input it sent with its own for frame (players 3 and 4: the
 * players past 2, PORT_ONLINE_PAD_BYTES each); 0 when that frame's isn't kept */
int portOnlineSeatPads(unsigned frame, unsigned char* pads);
/* the other side's input for frame, waiting up to timeoutMs: the guest's (host),
 * or the host's and then the seats' (guest: portOnlinePlayers() - 1 pads); 0:
 * the session ended */
int portOnlineWaitInput(unsigned frame, unsigned char pads[PORT_ONLINE_FRAME_BYTES], int timeoutMs);
void portOnlineSendHash(unsigned frame, unsigned long long hash);
int portOnlinePeerHash(unsigned frame, unsigned long long* hash);
void portOnlinePump(void);
/* tests ($TTYD_ONLINE_STALL_AT, online_pc.c): this side stops ms milliseconds */
void portOnlineTestStall(int ms);

/* The game's side (online_pc.c) */
/* The session begins as the game starts (the lobby's last step): the input
 * delay in frames. */
void portOnlineBegin(int inputDelay);
/* A session runs (the game plays in lockstep). */
int portOnlineActive(void);
/* The session ended here and now (the Start menu's Leave, which every game of
 * a session runs in the same frame): each plays on alone, as when the other
 * leaves. */
void portOnlineLeave(void);
/* This player alone leaves the session (the settings page: any player, the guest
 * too, whose Start menu is the host's in a session): the others are told the
 * other player left, and each plays on alone. */
void portOnlineQuit(void);
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
