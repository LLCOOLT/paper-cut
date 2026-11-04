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
/* Ended by now (the reason shown in the game), or NULL. */
const char* portOnlineEnded(void);
/* The pad read (src/sdk/DEMOPad.c): this frame's two players' input. */
struct PADStatus;
void portOnlineApplyPad(struct PADStatus* pads);
/* A hash of what the game plays out this frame, the same on both computers
 * while they are in step. */
unsigned long long portOnlineFingerprint(void);
/* Each frame, after the game's (vi_aurora.c). */
void portOnlineFrame(void);

#ifdef __cplusplus
}
#endif

#endif
