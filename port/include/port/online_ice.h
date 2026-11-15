/* Online play's direct connection (port/src/common/online_ice_pc.c): a room's two games (the host and
 * one of its players) reach each other over UDP when their networks let them, found with ICE (libjuice,
 * its STUN at Cloudflare, no TURN), the relay carrying the offer, the answer and the candidates (sealed
 * reliable messages, SS_R_DIRECT) and staying open, quiet, as the way back. Every packet is sealed with the
 * pair's key (online_seal_pc.c's PORT_SEAL_PACKET channel: its own count, replays dropped). The messages
 * (statesync_net_pc.c's [u32 length][u8 type][payload]) ride in packets of up to PORT_ICE_MTU bytes,
 * several to a packet, a long one in pieces (up to PORT_ICE_BIG); a bigger one goes by the relay. The
 * reliable ones stay statesync_net_pc.c's (numbered, acknowledged, sent again after the measured
 * round trip: portIceRtoMs); a late unreliable one (older than one of its type already taken) is dropped.
 * Two seconds with nothing from the other game: back on the relay at once (what wasn't acknowledged
 * goes again there), and tried again later. From 0.6 s quiet the messages go both ways meanwhile.
 *
 * The setting online_direct (each player's own, off by default: the other game sees this one's
 * internet address) must be on in both games. $TTYD_SYNC_DIRECT=force|off (tests) overrides it;
 * $TTYD_SYNC_STUN=<host>:<port>|none another STUN server or none; $TTYD_SYNC_UDP_FAULTS (tests):
 * drop:<percent>, dup:<percent>, delay:<min>-<max> (ms, so packets reorder), blackhole_at:<frame>[-<frame>]
 * (nothing passes either way: the stall), nocand (the other's candidates ignored: ICE fails, as across
 * two strict NATs), comma-separated. Not in the browser (no UDP): every call does nothing there. */
#ifndef PORT_ONLINE_ICE_H
#define PORT_ONLINE_ICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_ICE_LINKS 3   /* online_link.h's PORT_SEAL_LINKS: the host's three players, a player's host */
#define PORT_ICE_MTU 1150  /* a packet's messages at most (sealed: + 8 count + 16 tag + 1 kind) */
#define PORT_ICE_BIG 32768 /* a message longer than this goes by the relay */

typedef struct PortIceHooks {
    /* a reliable SS_R_DIRECT message (offer, answer, candidates...) to that link's game, over the relay */
    void (*signal)(int link, const void* p, size_t n);
    /* a whole message that came direct, as from the relay ([u32 length][u8 type] taken off) */
    void (*deliver)(int link, int type, const uint8_t* p, size_t n);
    /* the direct way went (quiet, or the other side said so): what wasn't acknowledged again, by the relay */
    void (*fellBack)(int link);
    void (*log)(const char* line);
} PortIceHooks;

/* a room's session (host: this game offers; sealed: 0 for the tests' stand-in relay, unsealed) */
void portIceBegin(int host, int sealed, const PortIceHooks* hooks);
void portIceEnd(void);
/* each pump, each link: its player, whether the relay's link to it is up with the hellos in (else
   the direct way stops), the frame (the faults' clock) */
void portIceTick(int link, int player, int linked, unsigned frame, unsigned long long now);
/* an SS_R_DIRECT message from that link's game */
void portIceSignalIn(int link, const uint8_t* p, size_t n);
/* the link's key gone (it's agreed again): the direct way too */
void portIceStop(int link);
int portIceUp(int link);   /* messages to that game go direct */
int portIceDual(int link); /* ...and by the relay as well (the direct way looks shaky) */
/* one message [u32 length][u8 type][payload] direct: 1 taken, 0 not (too big: the relay's) */
int portIceSend(int link, const void* msg, size_t len);
void portIceFlush(void);    /* the messages so far onto the wire (every link's) */
/* that link's messages waiting to go, and onto the wire: at the link's pace, with its relay batch
   (statesync_net_pc.c flushPeer: the same pace either way) */
int portIcePending(int link);
void portIceFlushLink(int link);
int portIceRtoMs(int link); /* a reliable message's wait before it's sent again on the direct way */
/* the setting, or the tests' $TTYD_SYNC_DIRECT: this game takes direct connections */
int portIceWanted(void);
int portIceAvailable(void); /* this build has it (not the browser's) */

/* the reliable signalling message (SS_R_DIRECT): this head, then the text (no terminating 0) */
typedef struct PortIceSig {
    uint8_t op; /* PORT_ICE_* below */
    uint8_t spare[3];
    uint32_t gen; /* the host's try it belongs to (an older try's are let go) */
} PortIceSig;
enum {
    PORT_ICE_OFFER = 1,  /* host: its description (ICE ufrag, password, options) */
    PORT_ICE_ANSWER = 2, /* player: its own */
    PORT_ICE_CAND = 3,   /* either: one candidate line */
    PORT_ICE_DONE = 4,   /* either: no more candidates */
    PORT_ICE_NO = 5,     /* player: not wanted here (its setting off), or no UDP */
    PORT_ICE_DOWN = 6,   /* either: this try is over (failed, or quiet): the relay again */
};

#ifdef __cplusplus
}
#endif

#endif
