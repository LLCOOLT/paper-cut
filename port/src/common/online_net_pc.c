/* Online co-op's connection (port/include/port/online.h): one TCP connection
 * between the host and the guest, the lobby's handshake on it, then each
 * frame's controller input and, now and then, the state's fingerprint. Only
 * the host's sockets and SDL here, no game headers (online_pc.c is the
 * game's side).
 *
 * Messages: a 4-byte length (little-endian, the type byte and payload), the
 * type, the payload.
 *   HELLO  both, first: the protocol, Paper Cut's version, the disc's ID, the
 *          mods in use, the player's name
 *   PING / PONG  the round trip, for the input delay
 *   SETUP  host: the game clock's start, the input delay, how the game starts
 *          (the title, or quick resume's save and its slot), the host's
 *          settings (settings.txt's lines), the host's memory card files
 *   READY  guest: the setup is in place
 *   START  host: both games start
 *   INPUT  a frame number and that frame's controller state (16 bytes; the
 *          host's: its own, then each seat's, players 3 and 4)
 *   HASH   a frame number and the state's fingerprint at its end
 *   BYE    the reason, when either side leaves
 * TCP with Nagle off: each frame's input goes at once, and the stream keeps
 * them in order with none lost.
 *
 * Seats (players 3 and 4): a seat joins as a watcher does (its connection on
 * the host's port, or the room's "seat" role) and gets what watchers get (the
 * setup, then batches of every player's input as played, each frame or each
 * PORT_ONLINE_BATCH frames while there are seats), and it sends the host:
 *   S_HELLO  first: a random id (to find itself in the setup) and its hello
 *   S_PAD    the controller it had for each frame it played (in order)
 *   S_BYE    it leaves
 * The host takes one of a seat's pads into each frame it sends (its INPUT
 * carries them; a seat's last again when none is there yet, nothing pressed
 * and no controller once it has left), so every game plays the same; a seat
 * is told S_REFUSE when the game has started or the seats are taken. Through a
 * relay a seat's messages are sealed with the watchers' key (they reach only
 * the host, wrapped by the relay as FROM_SEAT with its place in the room). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef select /* port_prefix.h's rename for the game (tik_05.c): this is the host's */
#include "port/online.h"

#if defined(__EMSCRIPTEN__) || !defined(TTYD_BACKEND_AURORA)
/* no online play: the web build, and the headless one (no SDL) */

static PortOnlineHello sNobody;
int portOnlineHost(int port, const PortOnlineHello* me) {
    (void)port;
    (void)me;
    return 0;
}
int portOnlineJoin(const char* address, int port, const PortOnlineHello* me) {
    (void)address;
    (void)port;
    (void)me;
    return 0;
}
void portOnlineLobbyPump(void) {}
int portOnlineLobbyState(void) {
    return PORT_ONLINE_IDLE;
}
const char* portOnlineStatus(void) {
    return "Not in this build";
}
const PortOnlineHello* portOnlinePeer(void) {
    return &sNobody;
}
const PortOnlineHello* portOnlineMe(void) {
    return &sNobody;
}
int portOnlineRoundTrip(void) {
    return -1;
}
int portOnlineIsHost(void) {
    return 0;
}
int portOnlineSendSetup(const void* setup, size_t len) {
    (void)setup;
    (void)len;
    return 0;
}
const void* portOnlineSetup(size_t* len) {
    *len = 0;
    return NULL;
}
void portOnlineGuestReady(void) {}
void portOnlineCancel(const char* why) {
    (void)why;
}
int portOnlineConnected(void) {
    return 0;
}
void portOnlineSendInput(unsigned frame, const unsigned char pad[PORT_ONLINE_PAD_BYTES]) {
    (void)frame;
    (void)pad;
}
int portOnlineWaitInput(unsigned frame, unsigned char pads[PORT_ONLINE_FRAME_BYTES], int timeoutMs) {
    (void)frame;
    (void)pads;
    (void)timeoutMs;
    return 0;
}
int portOnlineSeatPads(unsigned frame, unsigned char* pads) {
    (void)frame;
    (void)pads;
    return 0;
}
int portOnlineSeatRoom(const char* code, const PortOnlineHello* me) {
    (void)code;
    (void)me;
    return 0;
}
int portOnlineSeat(const char* address, int port, const PortOnlineHello* me) {
    (void)address;
    (void)port;
    (void)me;
    return 0;
}
int portOnlineSeating(void) {
    return 0;
}
int portOnlineSeats(void) {
    return 0;
}
int portOnlinePlayers(void) {
    return 2;
}
int portOnlineSelf(void) {
    return -1;
}
const char* portOnlinePlayerName(int player) {
    (void)player;
    return "";
}
void portOnlineSendHash(unsigned frame, unsigned long long hash) {
    (void)frame;
    (void)hash;
}
int portOnlinePeerHash(unsigned frame, unsigned long long* hash) {
    (void)frame;
    (void)hash;
    return 0;
}
void portOnlinePump(void) {}
void portOnlineSendResync(const void* payload, size_t len) {
    (void)payload;
    (void)len;
}
void portOnlineAskResync(unsigned epoch) {
    (void)epoch;
}
int portOnlineResyncArrived(const void** payload, size_t* len) {
    (void)payload;
    (void)len;
    return 0;
}
int portOnlineResyncAsked(void) {
    return -1;
}
int portOnlineLocalAddress(char* out, size_t size) {
    (void)size;
    out[0] = '\0';
    return 0;
}
const char* portOnlineRelay(void) {
    return "";
}
int portOnlineHostRoom(const char* code, const PortOnlineHello* me) {
    (void)code;
    (void)me;
    return 0;
}
int portOnlineJoinRoom(const char* code, const PortOnlineHello* me) {
    (void)code;
    (void)me;
    return 0;
}
const char* portOnlineRoom(void) {
    return "";
}
int portOnlineDelayExtra(void) {
    return 0;
}
const char* portOnlineLinkName(void) {
    return "";
}
int portOnlineWatchRoom(const char* code, const PortOnlineHello* me) {
    (void)code;
    (void)me;
    return 0;
}
int portOnlineWatch(const char* address, int port, const PortOnlineHello* me) {
    (void)address;
    (void)port;
    (void)me;
    return 0;
}
int portOnlineWatching(void) {
    return 0;
}
int portOnlineWatchers(void) {
    return 0;
}
const PortOnlineHello* portOnlineWatchedGuest(void) {
    return &sNobody;
}
void portOnlineWatchRecord(unsigned frame, const unsigned char* pads) {
    (void)frame;
    (void)pads;
}
void portOnlineWatchHash(unsigned frame, unsigned long long hash) {
    (void)frame;
    (void)hash;
}
int portOnlineWatchInput(unsigned frame, unsigned char* pads) {
    (void)frame;
    (void)pads;
    return -1;
}
unsigned portOnlineWatchHave(void) {
    return 0;
}
int portOnlineWatchHostHash(unsigned frame, unsigned long long* hash) {
    (void)frame;
    (void)hash;
    return 0;
}
unsigned portOnlineWatchResync(void) {
    return 0;
}
int portOnlineReconnecting(void) {
    return 0;
}
int portOnlineReconnected(void) {
    return 0;
}
void portOnlineTestDrop(void) {}
const char* portOnlineRelayAt(int i) {
    (void)i;
    return NULL;
}
void portOnlineListRoom(const char* name) {
    (void)name;
}
int portOnlineRejoinExpected(void) {
    return 0;
}

#else

#include <SDL3/SDL.h>
#include "port/online_link.h"
#include "port/online_sock.h"

/* ICE: a line of the direct path's search (online_ice_pc.c), through the
 * relay's room; LINK: host, before SETUP: 1 the session goes direct, 0 the
 * relay carries it */
enum { MSG_HELLO = 1, MSG_PING, MSG_PONG, MSG_SETUP, MSG_READY, MSG_START, MSG_INPUT, MSG_HASH, MSG_BYE,
       MSG_ICE, MSG_LINK,
       /* a room's: the key's two rounds (EC J-PAKE, from the code's secret half),
          then every other message sealed in one of these (online_seal_pc.c) */
       MSG_PAKE1, MSG_PAKE2, MSG_SEALED,
       /* out of step (online_pc.c): host, the way back (its save and a fresh start);
          guest, asking for it (its epoch) */
       MSG_RESYNC, MSG_RESYNC_ASK,
       /* a reconnect: the session's token and the first of the other's frames this side
          still needs (each side then sends what the other is missing) */
       MSG_RESUME,
       /* host to watchers: the setup (with the players' names), a batch of frames (both
          players' input, the host's fingerprints), the end, a recovery; through a relay,
          sealed for them all in a WSEALED (online_seal_pc.c's watchers' channel) */
       MSG_W_SETUP, MSG_W_BATCH, MSG_W_END, MSG_W_RESYNC, MSG_WSEALED,
       /* seats (players 3 and 4): seat to host, and host to the seats (in the watchers' channel) */
       MSG_S_HELLO, MSG_S_PAD, MSG_S_BYE, MSG_S_REFUSE,
       /* the relay's own wrapping of a seat's message to the host: its place in the room, the message */
       MSG_FROM_SEAT = 0xF0 };
#define MAGIC 0x504F4350u /* "PCOP" */
#define PROTOCOL 1
#define RING 512 /* frames of input kept (the delay is a few) */
#define MAX_MESSAGE (16 * 1024 * 1024)
#define SEAT_IN_MAX (1024 * 1024)        /* a seat sends pads: more than this waiting is something else */
#define LIST_REFRESH_MS (10 * 60 * 1000) /* how often a listed room renews its listing */

static Socket sListen = BAD_SOCKET, sSock = BAD_SOCKET;
static int sState = PORT_ONLINE_IDLE, sHost, sConnecting;
/* a room on the relay: the stream goes through its WebSocket (in batches
 * during the session), unless the direct UDP path carries the session */
static int sRelay, sUseIce;
static char sRoom[16];  /* the whole code: the room's half, then the secret one */
static int sPakeSent;   /* this side's first round went */
static int sWrongCodes; /* the host: tries with another code in this room */
static unsigned char* sOpened; /* the last sealed message, opened */
static size_t sOpenedCap;
static unsigned char* sPending; /* the stream's bytes not yet sent to the relay */
static size_t sPendingLen, sPendingCap;
static int sBatched; /* inputs among them */
static Uint64 sGoneAt; /* the relay said the other left (a direct session: confirmed by its silence) */
static char sStatus[192] = "";
/* the room list: this host's room listed under that name (portOnlineListRoom), and when it last said so */
static char sListName[64];
static int sListOn;
static Uint64 sListedAt;
static int sRejoinExpected; /* a guest left to start its game again (PORT_ONLINE_BYE_REJOIN) */
static PortOnlineHello sMe, sPeer;
static Uint64 sPingSent;
static int sRtt = -1; /* milliseconds */
/* what came in, not yet a whole message */
static unsigned char* sIn;
static size_t sInLen, sInCap;
/* the guest's setup, as received */
static unsigned char* sSetup;
static size_t sSetupLen;
/* the session: each side's input by frame, the peer's fingerprints */
static struct {
    int have;
    unsigned frame;
    unsigned char pad[PORT_ONLINE_FRAME_BYTES]; /* the guest's (host); the host's and the seats' (guest) */
} sRemote[RING];
/* a recovery (online_pc.c): the host's, as it came (guest); the guest's ask's epoch (host) */
static unsigned char* sResync;
static size_t sResyncLen;
static int sResyncAsked = -1;
static struct {
    unsigned frame;
    unsigned long long hash;
    int have;
} sRemoteHash[16];

/* --- watching (spectators) ---
 * A watcher plays both players' input from the host, in batches, and sends
 * nothing back; the players never wait for it. Through a relay the host sends
 * each batch once (the relay copies it to every watcher); by address, to each
 * watcher's own connection on the host's port. */
#define WATCH_BATCH 30     /* frames in a batch to the watchers */
#define WATCH_CHUNK 8192   /* frames in one message of the history a late watcher gets */
#define MAX_CONNS 6        /* by address: connections not yet the guest's, and the watchers' */
#define MAX_WATCHERS 4
#define MAX_SEAT_QUEUE 32 /* host: a seat's pads not yet in a frame */
/* a frame's record (the watchers' history and batches): every player's input */
static int sPlayers = 2; /* the session's (fixed when it starts) */
#define PAIR ((size_t)sPlayers * PORT_ONLINE_PAD_BYTES)
typedef struct {
    unsigned char* p;
    size_t len, cap;
} Buf;
enum { CONN_FREE, CONN_PENDING, CONN_WATCHER, CONN_SEAT };
static struct {
    Socket s;
    int kind;
    Buf in, out;
    Uint64 since;
} sConns[MAX_CONNS];
static int sWatch;          /* this side watches */
static int sPeerRole;       /* the peer's hello: 1 a watcher */
static PortOnlineHello sPeer2; /* a watcher's: the guest (sPeer is the host) */
static int sWatchCount;     /* host, a room: watchers there (the relay's "watch N") */
static Buf sHist;           /* host: both players' input, every frame of the session */
static unsigned sWatchSent; /* host: frames already sent to the watchers */
static Buf sHashHist;       /* host: its fingerprints (frame, hash), 12 bytes each */
static size_t sHashSent;    /* host: of them, sent */
static unsigned char* sHostSetup; /* host: the setup as sent, for watchers */
static size_t sHostSetupLen;
static unsigned long long sSetupId;
static Buf sGot;            /* watcher: both players' input, by frame */
static Buf sHostHashes;     /* watcher: the host's fingerprints by frame / PORT_ONLINE_HASH_EVERY (9 bytes) */
static int sHostEnded;      /* watcher: the host said the session is over */
static unsigned sWatchResyncEpoch;
static unsigned long long sGotSetupId;

/* --- seats (players 3 and 4) --- */
static int sSeat;                 /* this side is a seat (sWatch too: it gets what watchers get) */
static int sSelf = -1;            /* this side's player (portOnlineSelf) */
static unsigned long long sSeatId; /* a seat: its own, in its hello */
static int sRefused;              /* a seat: the host said no */
static char sNames[PORT_ONLINE_MAX_PLAYERS][32]; /* every player's name, once the session is set up */
static Buf sSeatOut;              /* a seat: its pads not yet sent (through a relay, a batch at a time) */
static unsigned sSeatOutFirst, sSeatOutCount;
static struct {
    int here;     /* joined: in the lobby, or playing */
    int conn;     /* by address: its connection; a room's: -1 */
    int slot;     /* a room's: its place there (the relay's) */
    int player;   /* 2 or 3 once the game starts, else -1 */
    int gone;     /* left during the session: no controller from the next frame on */
    unsigned long long id;
    char name[32];
    unsigned char q[MAX_SEAT_QUEUE][PORT_ONLINE_PAD_BYTES]; /* its pads not yet in a frame */
    unsigned qHead, qLen;
    unsigned char last[PORT_ONLINE_PAD_BYTES];
} sSeats[PORT_ONLINE_MAX_SEATS];

/* --- reconnecting: a session's connection lost, made again ---
 * The game waits (its next frame's input isn't there), both sides try again
 * for up to RECONNECT_MS: a room's relay opened again (the same role, its
 * ticket: the relay replaces a connection it still thinks is there), the key
 * again, the direct path tried again; by address, the guest connects again and
 * the host takes it on its port. Then each says the first of the other's
 * frames it still needs (RESUME) and sends the other what it's missing (its
 * own input is kept, sMineRing): lockstep goes on from the same frame. */
#define RECONNECT_MS 60000
static int sReconnecting, sResumeSent, sResumeGot, sLinkDecided, sReconnects;
static unsigned sPeerNeeds;  /* the other's RESUME: the first of this side's frames it needs */
static Uint64 sReconnectUntil, sReconnectTry, sKeysAt, sConnectAt;
static unsigned long long sToken; /* the session's: a hash of the host's setup */
static unsigned sRemoteNeed;      /* the next of the other's frames this side plays */
static struct {
    unsigned frame;
    int have;
    unsigned char pad[PORT_ONLINE_FRAME_BYTES]; /* the host's: its own, then the seats' */
} sMineRing[RING];
static size_t sMineBytes = PORT_ONLINE_PAD_BYTES; /* each of this side's inputs */
static unsigned sMineNext; /* this side's frames sent: up to here */
static char sJoinHost[256]; /* by address: where the guest joined, to join again */
static int sJoinPort, sHostPort;
static char sTicket[48];    /* a room: the relay's ticket for this side's place */
static char sRoleName[8];

static void bufAdd(Buf* b, const void* data, size_t len) {
    if (b->len + len > b->cap) {
        b->cap = (b->len + len) * 2 + 4096;
        b->p = (unsigned char*)realloc(b->p, b->cap);
    }
    if (len > 0) {
        memcpy(b->p + b->len, data, len);
    }
    b->len += len;
}

static void bufTake(Buf* b, size_t n) {
    memmove(b->p, b->p + n, b->len - n);
    b->len -= n;
}

static unsigned long long fnv64(const unsigned char* p, size_t len) {
    unsigned long long h = 0xCBF29CE484222325ull;
    size_t i;
    for (i = 0; i < len; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

static void status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sStatus, sizeof(sStatus), fmt, ap);
    va_end(ap);
    SDL_Log("online: %s", sStatus);
}

static void connsClose(void);
static void watchSendEnd(const char* why);

static void closeAll(void) {
    connsClose();
    sReconnecting = 0;
    if (sSock != BAD_SOCKET) {
        /* closed gently: what was sent (a goodbye, the last frames' input) still arrives. Input of
           theirs left unread here would make the close a reset, and theirs drop what was on its way */
        char drain[512];
        onlineNonBlocking(sSock, 1);
#ifdef _WIN32
        shutdown(sSock, SD_SEND);
#else
        shutdown(sSock, SHUT_WR);
#endif
        while (recv(sSock, drain, sizeof(drain), 0) > 0) {
        }
        closeSocket(sSock);
    }
    if (sListen != BAD_SOCKET) {
        closeSocket(sListen);
    }
    sSock = sListen = BAD_SOCKET;
    sInLen = 0;
    sConnecting = 0;
    if (sRelay) {
        portWsClose();
        portIceStop();
    }
    sRelay = sUseIce = 0;
    sPakeSent = 0;
    portSealReset();
    sGoneAt = 0;
    sPendingLen = 0;
    sBatched = 0;
}

static void fail(const char* why) {
    if (sHost) {
        watchSendEnd(why); /* the watchers told why, before the connections close */
    }
    closeAll();
    sState = PORT_ONLINE_FAILED;
    status("%s", why);
}

static int netStart(void) {
    return onlineNetStart();
}

static void nonBlocking(Socket s) {
    onlineNonBlocking(s, 1);
    {
        int one = 1; /* each frame's input at once */
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
}

/* the whole buffer out, waiting while the socket is full */
static int sendAll(const void* data, size_t len) {
    const char* p = (const char*)data;
    if (sRelay) { /* to the relay's next message (flushRelay) */
        if (portWsState() == PORT_WS_CLOSED) {
            return 0;
        }
        if (sPendingLen + len > sPendingCap) {
            sPendingCap = (sPendingLen + len) * 2 + 4096;
            sPending = (unsigned char*)realloc(sPending, sPendingCap);
        }
        memcpy(sPending + sPendingLen, data, len);
        sPendingLen += len;
        return 1;
    }
    while (len > 0 && sSock != BAD_SOCKET) {
#ifdef MSG_NOSIGNAL
        const int n = (int)send(sSock, p, (int)len, MSG_NOSIGNAL);
#else
        const int n = (int)send(sSock, p, (int)len, 0);
#endif
        if (n > 0) {
            p += n;
            len -= (size_t)n;
        } else if (n < 0 && WOULD_BLOCK()) {
            fd_set w;
            struct timeval wait = {0, 20000};
            FD_ZERO(&w);
            FD_SET(sSock, &w);
            select((int)sSock + 1, NULL, &w, NULL, &wait);
        } else {
            return 0;
        }
    }
    return len == 0;
}

/* the stream's bytes so far, as one relayed message */
static void flushRelay(void) {
    if (sRelay && sPendingLen > 0) {
        portWsSend(sPending, sPendingLen);
        sPendingLen = 0;
    }
    sBatched = 0;
}

static int sendRaw(int type, const void* payload, size_t len);

/* a room's messages, once the key is there: sealed whole (the type too), so a
 * relay can neither read nor change them */
static int sendMessage(int type, const void* payload, size_t len) {
    if (sRelay && portSealReady() && type != MSG_PAKE1 && type != MSG_PAKE2) {
        unsigned char* inner = (unsigned char*)malloc(len + 1);
        unsigned char* sealed = (unsigned char*)malloc(len + 1 + PORT_SEAL_TAG);
        size_t n;
        int ok;
        inner[0] = (unsigned char)type;
        if (len > 0) {
            memcpy(inner + 1, payload, len);
        }
        n = portSeal(PORT_SEAL_STREAM, inner, len + 1, sealed);
        /* (batched by its inner type: a sealed input waits for its batch) */
        ok = sendRaw(type == MSG_INPUT || type == MSG_HASH ? -type : MSG_SEALED, sealed, n);
        free(inner);
        free(sealed);
        return ok;
    }
    return sendRaw(type, payload, len);
}

static int sendRaw(int type, const void* payload, size_t len) {
    unsigned char head[5];
    const unsigned total = (unsigned)len + 1;
    const int batchAs = type < 0 ? -type : type; /* a sealed input or hash: batched as one */
    type = type < 0 ? MSG_SEALED : type;
    head[0] = (unsigned char)total;
    head[1] = (unsigned char)(total >> 8);
    head[2] = (unsigned char)(total >> 16);
    head[3] = (unsigned char)(total >> 24);
    head[4] = (unsigned char)type;
    if (!sendAll(head, sizeof(head)) || (len > 0 && !sendAll(payload, len))) {
        return 0;
    }
    if (sRelay) {
        /* the relay counts messages: the session's inputs go PORT_ONLINE_BATCH
         * frames at a time (the fingerprints with them), the rest at once */
        if (sState == PORT_ONLINE_STARTED && batchAs == MSG_INPUT) {
            sBatched++;
        }
        if (sState != PORT_ONLINE_STARTED || (batchAs != MSG_INPUT && batchAs != MSG_HASH) ||
            sBatched >= PORT_ONLINE_BATCH) {
            flushRelay();
        }
    }
    return 1;
}

/* --- little-endian packing --- */
static void put32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
static unsigned get32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}
static void put64(unsigned char* p, unsigned long long v) {
    put32(p, (unsigned)v);
    put32(p + 4, (unsigned)(v >> 32));
}
static unsigned long long get64(const unsigned char* p) {
    return get32(p) | ((unsigned long long)get32(p + 4) << 32);
}

#define HELLO_SIZE \
    (4 + 2 + sizeof(sMe.version) + sizeof(sMe.game) + sizeof(sMe.mods) + sizeof(sMe.name) + sizeof(sMe.lua))

/* the hello's bytes: its second byte after the protocol, 1 from a watcher, 2 from
 * a seat (0 from a player, and from versions before watching) */
static void makeHello(unsigned char* m) {
    unsigned char* p = m;
    put32(p, MAGIC);
    p += 4;
    p[0] = PROTOCOL;
    p[1] = (unsigned char)(sSeat ? 2 : sWatch ? 1 : 0);
    p += 2;
    memcpy(p, sMe.version, sizeof(sMe.version));
    p += sizeof(sMe.version);
    memcpy(p, sMe.game, sizeof(sMe.game));
    p += sizeof(sMe.game);
    memcpy(p, sMe.mods, sizeof(sMe.mods));
    p += sizeof(sMe.mods);
    memcpy(p, sMe.name, sizeof(sMe.name));
    p += sizeof(sMe.name);
    memcpy(p, sMe.lua, sizeof(sMe.lua)); /* (last: an older hello just ends before it) */
}

static void sendHello(void) {
    unsigned char m[HELLO_SIZE];
    makeHello(m);
    sendMessage(MSG_HELLO, m, sizeof(m));
}

/* a hello: the same protocol, version, disc and mods, or the reason; into peer */
static const char* parseHello(const unsigned char* p, size_t len, PortOnlineHello* peer, int* role) {
    PortOnlineHello h; /* (copied to peer once it passes) */
    if (len < 6 + sizeof(h.version) + sizeof(h.game) + sizeof(h.mods) + sizeof(h.name) ||
        get32(p) != MAGIC) {
        return "That isn't Paper Cut on the other side.";
    }
    if (p[4] != PROTOCOL) {
        return "The other player's Paper Cut is a different version.";
    }
    *role = p[5] == 1 || p[5] == 2 ? p[5] : 0;
    p += 6;
    memcpy(h.version, p, sizeof(h.version));
    p += sizeof(h.version);
    memcpy(h.game, p, sizeof(h.game));
    p += sizeof(h.game);
    memcpy(h.mods, p, sizeof(h.mods));
    p += sizeof(h.mods);
    memcpy(h.name, p, sizeof(h.name));
    p += sizeof(h.name);
    memset(h.lua, 0, sizeof(h.lua));
    if (len >= HELLO_SIZE) {
        memcpy(h.lua, p, sizeof(h.lua));
    }
    h.lua[sizeof(h.lua) - 1] = '\0';
    h.version[sizeof(h.version) - 1] = h.game[sizeof(h.game) - 1] = '\0';
    h.mods[sizeof(h.mods) - 1] = h.name[sizeof(h.name) - 1] = '\0';
    if (strcmp(h.version, sMe.version) != 0) {
        return "The other player's Paper Cut is a different version.";
    }
    if (strcmp(h.game, sMe.game) != 0) {
        return "The other player has a different release of the game (both need the same: Japanese, US or "
               "European).";
    }
    if (strcmp(h.mods, sMe.mods) != 0) {
        return "The other player has different mods on (both need the same).";
    }
    *peer = h;
    return NULL;
}

static const char* readHello(const unsigned char* p, size_t len) {
    return parseHello(p, len, &sPeer, &sPeerRole);
}

/* one whole message out of the buffer: its type, payload and length, or 0 */
static int takeMessage(unsigned char** payload, size_t* len) {
    unsigned total;
    if (sInLen < 4) {
        return 0;
    }
    total = get32(sIn);
    if (total == 0 || total > MAX_MESSAGE) {
        fail("The connection sent something that isn't Paper Cut's.");
        return 0;
    }
    if (sInLen < 4 + total) {
        return 0;
    }
    *payload = sIn + 5;
    *len = total - 1;
    return sIn[4];
}

static void dropMessage(void) {
    const size_t used = sInLen >= 4 ? 4 + get32(sIn) : 0;
    if (sInLen < used || used == 0) { /* (the buffer emptied meanwhile: the connection closed) */
        sInLen = 0;
        return;
    }
    memmove(sIn, sIn + used, sInLen - used);
    sInLen -= used;
}

/* this side's first round of the room's key (once), from the code's secret half */
static void startPake(void) {
    unsigned char m[512];
    size_t n = 0;
    if (sPakeSent) {
        return;
    }
    sPakeSent = 1;
    if (!portPakeBegin(sHost, sRoom + PORT_ONLINE_ROOM_HALF, m, sizeof(m), &n)) {
        fail("A secure connection couldn't be set up.");
        return;
    }
    sendRaw(MSG_PAKE1, m, n);
}

/* the host, before the game: back to waiting in the room (the one joining left, or
 * had another code) */
static void hostWaitsAgain(const char* why) {
    SDL_Log("online: %s", why);
    portIceStop();
    portSealReset();
    sPakeSent = 0;
    sInLen = 0;
    sPendingLen = 0;
    sState = PORT_ONLINE_LISTENING;
    status("Room %.4s: waiting for the other player to join.", sRoom); /* (the room's half: logs go out with crash reports) */
}

/* --- watching: the host's side --- */

static int watchersHere(void) {
    int i, n = 0;
    if (sRelay) {
        return sWatchCount;
    }
    for (i = 0; i < MAX_CONNS; i++) {
        n += sConns[i].kind == CONN_WATCHER;
    }
    return n;
}

/* host: the seats here (in the lobby; in the session, those that haven't left) */
static int seatsHere(void) {
    int i, n = 0;
    for (i = 0; i < PORT_ONLINE_MAX_SEATS; i++) {
        n += sSeats[i].here && !sSeats[i].gone;
    }
    return n;
}

static void connFree(int i) {
    if (sConns[i].kind != CONN_FREE && sConns[i].s != BAD_SOCKET) {
        closeSocket(sConns[i].s);
    }
    sConns[i].s = BAD_SOCKET;
    sConns[i].kind = CONN_FREE;
    sConns[i].in.len = sConns[i].out.len = 0;
}

static void seatLeft(int i);

/* a watcher's or a seat's connection gone (closed, or let go): a seat's place is told, as when
 * it closes itself (else it would stay, its last pad again each frame) */
static void connLost(int i, const char* why) {
    int k;
    SDL_Log("online: a %s %s", sConns[i].kind == CONN_SEAT ? "seat" : "watcher", why);
    for (k = 0; k < PORT_ONLINE_MAX_SEATS; k++) {
        if (sConns[i].kind == CONN_SEAT && sSeats[k].here && sSeats[k].conn == i) {
            seatLeft(k);
        }
    }
    connFree(i);
}

static void connsClose(void) {
    int i;
    for (i = 0; i < MAX_CONNS; i++) {
        if (sConns[i].kind != CONN_FREE) {
            connFree(i);
        }
    }
}

/* a watcher's connection: as much of what waits as its socket takes now (never waiting) */
static void connFlush(int i) {
    while (sConns[i].out.len > 0) {
#ifdef MSG_NOSIGNAL
        const int n = (int)send(sConns[i].s, (const char*)sConns[i].out.p, (int)sConns[i].out.len, MSG_NOSIGNAL);
#else
        const int n = (int)send(sConns[i].s, (const char*)sConns[i].out.p, (int)sConns[i].out.len, 0);
#endif
        if (n > 0) {
            bufTake(&sConns[i].out, (size_t)n);
        } else if (n < 0 && WOULD_BLOCK()) {
            return;
        } else {
            connLost(i, "left");
            return;
        }
    }
}

static void connSend(int i, int type, const void* payload, size_t len) {
    unsigned char head[5];
    put32(head, (unsigned)len + 1);
    head[4] = (unsigned char)type;
    bufAdd(&sConns[i].out, head, sizeof(head));
    bufAdd(&sConns[i].out, payload, len);
    if (sConns[i].out.len > 64u * 1024 * 1024) { /* a watcher that can't keep up isn't kept */
        connLost(i, "too far behind: let go");
        return;
    }
    connFlush(i);
}

/* inner: its type, then the payload; to one watcher (by address), or all (only = -1) */
static void watchSendInner(const unsigned char* inner, size_t len, int only) {
    int i;
    if (!sHost || len < 1) {
        return;
    }
    if (sRelay) {
        /* one message for all of them: the relay copies a message that starts with four
           0xFF (no message's length) to the room's watchers, without those */
        unsigned char* m;
        size_t n;
        /* (a refusal goes even with nobody else here: the seat refused waits for it) */
        if ((sWatchCount <= 0 && seatsHere() == 0 && inner[0] != MSG_S_REFUSE) || portWsState() != PORT_WS_OPEN ||
            !portWatchKey(sRoom)) {
            return;
        }
        m = (unsigned char*)malloc(len + PORT_WATCH_OVERHEAD + 9);
        memset(m, 0xFF, 4);
        n = portWatchSeal(inner, len, m + 9);
        put32(m + 4, (unsigned)n + 1);
        m[8] = MSG_WSEALED;
        portWsSend(m, n + 9);
        free(m);
        return;
    }
    for (i = 0; i < MAX_CONNS; i++) {
        if ((sConns[i].kind == CONN_WATCHER || sConns[i].kind == CONN_SEAT) && (only < 0 || only == i)) {
            connSend(i, inner[0], inner + 1, len - 1);
        }
    }
}

static void watchSendEnd(const char* why) {
    unsigned char m[160];
    const size_t n = why != NULL ? (strlen(why) < sizeof(m) - 1 ? strlen(why) : sizeof(m) - 1) : 0;
    int i;
    m[0] = MSG_W_END;
    if (n > 0) {
        memcpy(m + 1, why, n);
    }
    watchSendInner(m, n + 1, -1);
    for (i = 0; i < MAX_CONNS; i++) { /* (a moment for it to go) */
        if (sConns[i].kind == CONN_WATCHER || sConns[i].kind == CONN_SEAT) {
            connFlush(i);
        }
    }
}

/* frames [first, first + count) of the history, and the fingerprints [hashFrom, hashTo),
 * as a batch: runs of the same two inputs */
static void watchBatch(Buf* out, unsigned first, unsigned count, size_t hashFrom, size_t hashTo) {
    unsigned char head[12], run[2];
    unsigned i = 0, runs = 0;
    size_t h;
    out->len = 0;
    head[0] = MSG_W_BATCH;
    bufAdd(out, head, 1);
    put32(head, first);
    put32(head + 4, count);
    put32(head + 8, 0);
    bufAdd(out, head, 12);
    while (i < count) {
        const unsigned char* pair = sHist.p + (size_t)(first + i) * PAIR;
        unsigned rep = 1;
        while (i + rep < count && rep < 65535 && memcmp(pair, pair + (size_t)rep * PAIR, PAIR) == 0) {
            rep++;
        }
        run[0] = (unsigned char)rep;
        run[1] = (unsigned char)(rep >> 8);
        bufAdd(out, run, 2);
        bufAdd(out, pair, PAIR);
        runs++;
        i += rep;
    }
    put32(out->p + 9, runs);
    if (hashTo - hashFrom > 255) {
        hashFrom = hashTo - 255;
    }
    head[0] = (unsigned char)(hashTo - hashFrom);
    bufAdd(out, head, 1);
    for (h = hashFrom; h < hashTo; h++) {
        bufAdd(out, sHashHist.p + h * 12, 12);
    }
}

/* a watcher came (only: its connection; -1 a room's): the setup, then everything so far */
static void watchWelcome(int only) {
    Buf b = {0};
    unsigned at;
    size_t h = 0;
    if (sHostSetup == NULL) {
        return; /* (the game hasn't started: the setup goes to all when it does) */
    }
    {
        /* the setup's id, the players, their names, the seats' ids (each seat finds its place) */
        unsigned char head[1 + 8 + 1 + PORT_ONLINE_MAX_PLAYERS * 32 + PORT_ONLINE_MAX_SEATS * 8];
        int k;
        memset(head, 0, sizeof(head));
        head[0] = MSG_W_SETUP;
        put64(head + 1, sSetupId);
        head[9] = (unsigned char)sPlayers;
        for (k = 0; k < PORT_ONLINE_MAX_PLAYERS; k++) {
            memcpy(head + 10 + k * 32, sNames[k], 32);
        }
        for (k = 0; k < PORT_ONLINE_MAX_SEATS; k++) {
            int j;
            for (j = 0; j < PORT_ONLINE_MAX_SEATS; j++) {
                if (sSeats[j].here && sSeats[j].player == 2 + k) {
                    put64(head + 10 + PORT_ONLINE_MAX_PLAYERS * 32 + k * 8, sSeats[j].id);
                }
            }
        }
        bufAdd(&b, head, sizeof(head));
        bufAdd(&b, sHostSetup, sHostSetupLen);
        watchSendInner(b.p, b.len, only);
    }
    for (at = 0; at < sWatchSent; at += WATCH_CHUNK) {
        const unsigned count = sWatchSent - at < WATCH_CHUNK ? sWatchSent - at : WATCH_CHUNK;
        /* the fingerprints of those frames */
        size_t to = h;
        while (to < sHashSent && get32(sHashHist.p + to * 12) <= at + count) {
            to++;
        }
        watchBatch(&b, at, count, h, to);
        watchSendInner(b.p, b.len, only);
        h = to;
    }
    free(b.p);
    SDL_Log("online: a watcher is here: %u frames so far sent", sWatchSent);
}

/* the frames since the last batch, to the watchers and the seats (none there: only counted) */
static void watchFlush(void) {
    const unsigned frames = (unsigned)(sHist.len / PAIR);
    if (frames <= sWatchSent) {
        return;
    }
    if (watchersHere() > 0 || seatsHere() > 0) { /* (the seats play from these: they aren't watchers) */
        Buf b = {0};
        watchBatch(&b, sWatchSent, frames - sWatchSent, sHashSent, sHashHist.len / 12);
        watchSendInner(b.p, b.len, -1);
        free(b.p);
    }
    sWatchSent = frames;
    sHashSent = sHashHist.len / 12;
}

static void beginReconnect(int clearIn);
static int iceAllowed(void);

/* --- seats: the host's side --- */

/* nothing pressed: a controller there (here: 1), or none (a seat that left) */
static void seatNeutral(unsigned char* pad, int here) {
    memset(pad, 0, PORT_ONLINE_PAD_BYTES);
    pad[10] = here ? 0 : 0xFF; /* the pad's err: PAD_ERR_NONE, PAD_ERR_NO_CONTROLLER */
}

/* a seat refused (conn: its connection by address, or -1 a room's, told through the watchers' channel) */
static void seatRefuse(int conn, unsigned long long id, const char* why) {
    unsigned char m[8 + 160];
    const size_t n = strlen(why) < 160 ? strlen(why) : 160;
    put64(m, id);
    memcpy(m + 8, why, n);
    SDL_Log("online: a seat refused: %s", why);
    if (conn >= 0) {
        connSend(conn, MSG_S_REFUSE, m, 8 + n);
        connFlush(conn);
        connFree(conn);
    } else {
        unsigned char inner[1 + 8 + 160];
        inner[0] = MSG_S_REFUSE;
        memcpy(inner + 1, m, 8 + n);
        watchSendInner(inner, 9 + n, -1);
    }
}

/* a seat's hello (payload: its id, then a hello); conn its connection by address, or -1 and
 * slot its place in a room. 1 when it's taken (by address its connection then a seat's) */
static int seatHello(int conn, int slot, const unsigned char* p, size_t len) {
    PortOnlineHello who;
    int role = 0, i, free_ = -1;
    unsigned long long id;
    const char* why;
    if (len < 8) {
        return 0;
    }
    id = get64(p);
    why = parseHello(p + 8, len - 8, &who, &role);
    for (i = 0; i < PORT_ONLINE_MAX_SEATS; i++) {
        if (sSeats[i].here && sSeats[i].id == id) {
            return 1; /* (said again: already here) */
        }
        if (!sSeats[i].here && free_ < 0) {
            free_ = i;
        }
    }
    if (why == NULL && (sState >= PORT_ONLINE_WAITING_READY || sState == PORT_ONLINE_FAILED)) {
        why = "That game has started: watch it instead.";
    }
    if (why == NULL && free_ < 0) {
        why = "That game has as many players as it can (four).";
    }
    if (why == NULL && strcmp(who.lua, sMe.lua) != 0) {
        /* (the guest may differ: Lua mods then run HUD-only on both; a seat can't tell) */
        why = "The other player has different Lua mods on (a seat needs the same).";
    }
    if (why != NULL) {
        seatRefuse(conn, id, why);
        return 0;
    }
    memset(&sSeats[free_], 0, sizeof(sSeats[free_]));
    sSeats[free_].here = 1;
    sSeats[free_].conn = conn;
    sSeats[free_].slot = slot;
    sSeats[free_].player = -1;
    sSeats[free_].id = id;
    memcpy(sSeats[free_].name, who.name, sizeof(sSeats[free_].name));
    seatNeutral(sSeats[free_].last, 1);
    if (conn >= 0) {
        unsigned char m[HELLO_SIZE];
        sConns[conn].kind = CONN_SEAT;
        makeHello(m);
        connSend(conn, MSG_HELLO, m, sizeof(m));
    }
    SDL_Log("online: a player joined a seat (%d of %d here)", seatsHere(), PORT_ONLINE_MAX_SEATS);
    return 1;
}

/* a seat left (its connection closed, it said so, or the relay said so) */
static void seatLeft(int i) {
    if (!sSeats[i].here) {
        return;
    }
    if (sState == PORT_ONLINE_STARTED && sSeats[i].player >= 0) {
        sSeats[i].gone = 1; /* its place kept: no controller there from the next frame sent */
        SDL_Log("online: player %d left", sSeats[i].player + 1);
    } else {
        sSeats[i].here = 0;
        SDL_Log("online: a seat left before the game");
    }
    sSeats[i].conn = -1;
}

/* a seat's messages (its pads, its goodbye) */
static void seatMessage(int i, int type, const unsigned char* p, size_t len) {
    if (type == MSG_S_PAD && len >= 5 && !sSeats[i].gone) {
        const unsigned count = p[4];
        unsigned k;
        for (k = 0; k < count && 5 + (size_t)(k + 1) * PORT_ONLINE_PAD_BYTES <= len; k++) {
            if (sSeats[i].qLen == MAX_SEAT_QUEUE) { /* far behind: its oldest let go */
                sSeats[i].qHead = (sSeats[i].qHead + 1) % MAX_SEAT_QUEUE;
                sSeats[i].qLen--;
            }
            memcpy(sSeats[i].q[(sSeats[i].qHead + sSeats[i].qLen) % MAX_SEAT_QUEUE],
                   p + 5 + (size_t)k * PORT_ONLINE_PAD_BYTES, PORT_ONLINE_PAD_BYTES);
            sSeats[i].qLen++;
        }
    } else if (type == MSG_S_BYE) {
        seatLeft(i);
    }
}

/* host: a seat's pad for the next frame it sends: the oldest that came, its last again
 * when none has (a few kept for the next frames, the rest skipped if it's far ahead), no
 * controller once it has left */
static void seatTake(int i, unsigned char* pad) {
    if (sSeats[i].gone || !sSeats[i].here) {
        seatNeutral(pad, 0);
        return;
    }
    /* No more kept than the way they come needs (one by address, a relay's batch): a seat
       that hitched and caught up sends a burst, and every pad that stayed queued would be
       a frame more of delay for that player from then on (players 3 and 4 a little later
       than the first two). The skipped pads' buttons go into the next one, so a short
       press isn't lost. (The host decides here and sends what it took: every game plays
       the same.) */
    {
        const unsigned keep = sRelay ? PORT_ONLINE_BATCH + 1 : 2;
        while (sSeats[i].qLen > keep) {
            const unsigned char* skip = sSeats[i].q[sSeats[i].qHead];
            unsigned char* next;
            sSeats[i].qHead = (sSeats[i].qHead + 1) % MAX_SEAT_QUEUE;
            sSeats[i].qLen--;
            next = sSeats[i].q[sSeats[i].qHead];
            next[0] |= skip[0]; /* the buttons; the sticks and triggers the newer's */
            next[1] |= skip[1];
            next[12] |= skip[12];
            next[13] |= skip[13];
            next[14] |= skip[14];
            next[15] |= skip[15];
        }
    }
    if (sSeats[i].qLen > 0) {
        memcpy(sSeats[i].last, sSeats[i].q[sSeats[i].qHead], PORT_ONLINE_PAD_BYTES);
        sSeats[i].qHead = (sSeats[i].qHead + 1) % MAX_SEAT_QUEUE;
        sSeats[i].qLen--;
    }
    memcpy(pad, sSeats[i].last, PORT_ONLINE_PAD_BYTES);
}

/* the seat in a player's place (2 or 3), or -1 */
static int seatOfPlayer(int player) {
    int i;
    for (i = 0; i < PORT_ONLINE_MAX_SEATS; i++) {
        if (sSeats[i].here && sSeats[i].player == player) {
            return i;
        }
    }
    return -1;
}

/* a room's: a seat's message through the relay (its place, then the message: sealed for the host) */
static void seatFromRelay(const unsigned char* p, size_t len) {
    int slot, i, seat = -1;
    size_t n = 0, inner;
    unsigned char* opened;
    if (len < 6 || !portWatchKey(sRoom)) {
        return;
    }
    slot = p[0];
    inner = get32(p + 1);
    if (inner < 1 || 5 + (size_t)inner > len || p[5] != MSG_WSEALED) {
        return;
    }
    opened = (unsigned char*)malloc(inner + 1);
    if (opened == NULL) {
        return;
    }
    if (!portWatchOpen(p + 6, inner - 1, opened, &n) || n < 1) {
        free(opened);
        return; /* (not the code's: not one of this room's players) */
    }
    for (i = 0; i < PORT_ONLINE_MAX_SEATS; i++) {
        if (sSeats[i].here && sSeats[i].conn < 0 && sSeats[i].slot == slot) {
            seat = i;
        }
    }
    if (opened[0] == MSG_S_HELLO) {
        seatHello(-1, slot, opened + 1, n - 1);
    } else if (seat >= 0) {
        seatMessage(seat, opened[0], opened + 1, n - 1);
    }
    free(opened);
}

/* by address: what's come on the host's port besides the guest's connection (a
 * watcher, or the guest connecting again after a drop), and the watchers' sends */
static void connsPump(void) {
    int i;
    if (!sHost || sRelay) {
        return;
    }
    if (sListen != BAD_SOCKET && sState != PORT_ONLINE_LISTENING) {
        Socket s = accept(sListen, NULL, NULL);
        if (s != BAD_SOCKET) {
            for (i = 0; i < MAX_CONNS && sConns[i].kind != CONN_FREE; i++) {
            }
            if (i == MAX_CONNS) {
                closeSocket(s);
            } else {
                nonBlocking(s);
                sConns[i].s = s;
                sConns[i].kind = CONN_PENDING;
                sConns[i].in.len = sConns[i].out.len = 0;
                sConns[i].since = SDL_GetTicks();
            }
        }
    }
    for (i = 0; i < MAX_CONNS; i++) {
        unsigned char tmp[4096];
        int n;
        if (sConns[i].kind == CONN_FREE) {
            continue;
        }
        connFlush(i);
        if (sConns[i].kind == CONN_FREE) {
            continue;
        }
        while ((n = (int)recv(sConns[i].s, (char*)tmp, sizeof(tmp), 0)) > 0) {
            if (sConns[i].kind == CONN_PENDING || sConns[i].kind == CONN_SEAT) {
                bufAdd(&sConns[i].in, tmp, (size_t)n);
            }
        }
        if (sConns[i].kind == CONN_SEAT) {
            /* a seat's messages: its pads, its goodbye */
            int k, seat = -1;
            for (k = 0; k < PORT_ONLINE_MAX_SEATS; k++) {
                if (sSeats[k].here && sSeats[k].conn == i) {
                    seat = k;
                }
            }
            while (seat >= 0 && sConns[i].in.len >= 5 && sConns[i].in.len >= 4 + (size_t)get32(sConns[i].in.p)) {
                const size_t used = 4 + get32(sConns[i].in.p);
                if (used < 5) {
                    break;
                }
                seatMessage(seat, sConns[i].in.p[4], sConns[i].in.p + 5, used - 5);
                bufTake(&sConns[i].in, used);
            }
            if (seat < 0 || sConns[i].in.len > SEAT_IN_MAX) {
                n = 0; /* (not a seat any more, or not sending what a seat sends) */
            }
        }
        if (n == 0 || (n < 0 && !WOULD_BLOCK())) {
            int k;
            if (sConns[i].kind == CONN_WATCHER) {
                SDL_Log("online: a watcher left");
            }
            for (k = 0; k < PORT_ONLINE_MAX_SEATS; k++) {
                if (sConns[i].kind == CONN_SEAT && sSeats[k].here && sSeats[k].conn == i) {
                    seatLeft(k);
                }
            }
            connFree(i);
            continue;
        }
        if (sConns[i].kind != CONN_PENDING) {
            continue;
        }
        if (SDL_GetTicks() - sConns[i].since > 10000 || sConns[i].in.len > 65536) {
            connFree(i);
            continue;
        }
        if (sConns[i].in.len >= 4 && (get32(sConns[i].in.p) == 0 || get32(sConns[i].in.p) > MAX_MESSAGE)) {
            connFree(i); /* (not a message: as takeMessage refuses it) */
            continue;
        }
        if (sConns[i].in.len >= 5 && sConns[i].in.len >= 4 + (size_t)get32(sConns[i].in.p)) {
            const unsigned char* p = sConns[i].in.p + 5;
            const size_t len = get32(sConns[i].in.p) - 1;
            const int type = sConns[i].in.p[4];
            if (type == MSG_S_HELLO) {
                const size_t used = 4 + get32(sConns[i].in.p);
                if (seatHello(i, -1, p, len)) {
                    bufTake(&sConns[i].in, used); /* (what came after it: the seat's own, next time) */
                }
            } else if (type == MSG_HELLO) {
                PortOnlineHello who;
                int role = 0;
                const char* why = parseHello(p, len, &who, &role);
                if (why == NULL && !role) {
                    why = "That game already has two players.";
                }
                if (why == NULL && watchersHere() >= MAX_WATCHERS) {
                    why = "As many are watching that game as can.";
                }
                if (why != NULL) {
                    connSend(i, MSG_BYE, why, strlen(why));
                    connFree(i);
                    continue;
                }
                bufTake(&sConns[i].in, 4 + get32(sConns[i].in.p));
                sConns[i].kind = CONN_WATCHER;
                {
                    unsigned char m[HELLO_SIZE];
                    makeHello(m);
                    connSend(i, MSG_HELLO, m, sizeof(m));
                }
                if (sConns[i].kind == CONN_WATCHER) {
                    watchWelcome(i);
                }
            } else if (type == MSG_RESUME && sState == PORT_ONLINE_STARTED && len >= 12 && get64(p) == sToken) {
                /* the guest again, after a drop: its connection is the session's now */
                SDL_Log("online: the other player connected again");
                if (sSock != BAD_SOCKET) {
                    closeSocket(sSock);
                }
                if (!sReconnecting) { /* (this side hadn't noticed yet) */
                    beginReconnect(1);
                }
                sSock = sConns[i].s;
                if (sInCap < sConns[i].in.len) {
                    sInCap = sConns[i].in.len + 65536;
                    sIn = (unsigned char*)realloc(sIn, sInCap);
                }
                memcpy(sIn, sConns[i].in.p, sConns[i].in.len);
                sInLen = sConns[i].in.len;
                sConns[i].kind = CONN_FREE;
                sConns[i].s = BAD_SOCKET;
                sConns[i].in.len = sConns[i].out.len = 0;
            } else {
                connFree(i);
            }
        }
    }
}

void portOnlineWatchRecord(unsigned frame, const unsigned char* pads) {
    unsigned every;
    if (!sHost || sWatch || sState != PORT_ONLINE_STARTED || frame != sHist.len / PAIR) {
        return;
    }
    bufAdd(&sHist, pads, PAIR);
    /* the seats play from these: each frame (by address) or a relay's batch; the watchers'
       alone, every WATCH_BATCH */
    every = seatsHere() > 0 ? (sRelay ? PORT_ONLINE_BATCH : 1) : WATCH_BATCH;
    if (sHist.len / PAIR - sWatchSent >= every) {
        watchFlush();
    }
}

void portOnlineWatchHash(unsigned frame, unsigned long long hash) {
    unsigned char m[12];
    if (!sHost || sState != PORT_ONLINE_STARTED) {
        return;
    }
    put32(m, frame);
    put64(m + 4, hash);
    bufAdd(&sHashHist, m, sizeof(m));
}

int portOnlineWatchers(void) {
    return sHost ? watchersHere() : 0;
}

/* --- watching: the watcher's side --- */

#define W_SETUP_HEAD (8 + 1 + PORT_ONLINE_MAX_PLAYERS * 32 + PORT_ONLINE_MAX_SEATS * 8)

static void watchHandle(int type, const unsigned char* p, size_t len) {
    if (type == MSG_W_SETUP && len >= W_SETUP_HEAD && p[8] >= 2 && p[8] <= PORT_ONLINE_MAX_PLAYERS) {
        const unsigned long long id = get64(p);
        int k;
        if (sState >= PORT_ONLINE_GOT_SETUP || id == sGotSetupId) {
            return; /* (one this watcher has: sent again for a later one) */
        }
        sGotSetupId = id;
        sPlayers = p[8];
        for (k = 0; k < PORT_ONLINE_MAX_PLAYERS; k++) {
            memcpy(sNames[k], p + 9 + k * 32, 32);
            sNames[k][31] = '\0';
        }
        memcpy(sPeer.name, sNames[0], 32);
        memcpy(sPeer2.name, sNames[1], 32);
        sSelf = -1;
        for (k = 0; k < PORT_ONLINE_MAX_SEATS; k++) {
            if (sSeat && k + 2 < sPlayers && get64(p + 9 + PORT_ONLINE_MAX_PLAYERS * 32 + k * 8) == sSeatId) {
                sSelf = 2 + k; /* this seat's place */
            }
        }
        if (sSeat && sSelf < 0) {
            fail("The game started without this player: watch it instead.");
            return;
        }
        free(sSetup);
        sSetupLen = len - W_SETUP_HEAD;
        sSetup = (unsigned char*)malloc(sSetupLen > 0 ? sSetupLen : 1);
        memcpy(sSetup, p + W_SETUP_HEAD, sSetupLen);
        sGot.len = 0;
        sState = PORT_ONLINE_GOT_SETUP;
        status("Getting ready...");
    } else if (type == MSG_W_BATCH && len >= 12 && sGotSetupId != 0) {
        const unsigned first = get32(p), runs = get32(p + 8);
        unsigned r, f = first;
        size_t at = 12;
        for (r = 0; r < runs && at + 2 + PAIR <= len; r++, at += 2 + PAIR) {
            const unsigned rep = p[at] | (p[at + 1] << 8);
            unsigned k;
            for (k = 0; k < rep; k++, f++) {
                if (f == sGot.len / PAIR) {
                    bufAdd(&sGot, p + at + 2, PAIR);
                }
            }
        }
        if (at < len) {
            const unsigned hashes = p[at++];
            unsigned i;
            for (i = 0; i < hashes && at + 12 <= len; i++, at += 12) {
                const unsigned frame = get32(p + at);
                const size_t slot = frame / PORT_ONLINE_HASH_EVERY;
                if (frame % PORT_ONLINE_HASH_EVERY != 0 || slot > (1u << 22)) {
                    continue;
                }
                while (sHostHashes.len < (slot + 1) * 9) {
                    const unsigned char none[9] = {0};
                    bufAdd(&sHostHashes, none, sizeof(none));
                }
                memcpy(sHostHashes.p + slot * 9, p + at + 4, 8);
                sHostHashes.p[slot * 9 + 8] = 1;
            }
        }
    } else if (type == MSG_W_END) {
        char why[160];
        snprintf(why, sizeof(why), "%.*s", (int)(len < sizeof(why) - 1 ? len : sizeof(why) - 1), (const char*)p);
        sHostEnded = 1;
        SDL_Log("online: the players' game ended (%s)", why);
    } else if (type == MSG_W_RESYNC && len >= 8) {
        sWatchResyncEpoch = get32(p + 4);
    } else if (type == MSG_S_REFUSE && sSeat && len >= 8 && get64(p) == sSeatId) {
        char why[160];
        snprintf(why, sizeof(why), "%.*s", (int)(len - 8 < sizeof(why) - 1 ? len - 8 : sizeof(why) - 1),
                 (const char*)p + 8);
        sRefused = 1;
        fail(why[0] != '\0' ? why : "That game has as many players as it can.");
    }
}

int portOnlineWatching(void) {
    return sWatch && !sSeat;
}

int portOnlineSeating(void) {
    return sSeat;
}

int portOnlineSeats(void) {
    return sHost ? seatsHere() : 0;
}

int portOnlinePlayers(void) {
    return sState == PORT_ONLINE_STARTED || sState == PORT_ONLINE_WAITING_START || sState == PORT_ONLINE_GOT_SETUP
               ? sPlayers
               : 2;
}

int portOnlineSelf(void) {
    return sState == PORT_ONLINE_STARTED ? sSelf : -1;
}

const char* portOnlinePlayerName(int player) {
    return player >= 0 && player < PORT_ONLINE_MAX_PLAYERS ? sNames[player] : "";
}

const PortOnlineHello* portOnlineWatchedGuest(void) {
    return &sPeer2;
}

unsigned portOnlineWatchHave(void) {
    return (unsigned)(sGot.len / PAIR);
}

int portOnlineWatchHostHash(unsigned frame, unsigned long long* hash) {
    const size_t slot = frame / PORT_ONLINE_HASH_EVERY;
    if (!sWatch || frame % PORT_ONLINE_HASH_EVERY != 0 || (slot + 1) * 9 > sHostHashes.len ||
        !sHostHashes.p[slot * 9 + 8]) {
        return 0;
    }
    *hash = get64(sHostHashes.p + slot * 9);
    return 1;
}

unsigned portOnlineWatchResync(void) {
    return sWatchResyncEpoch;
}

/* --- reconnecting --- */

static int reconnectAllowed(void) {
    return !sWatch && getenv("TTYD_ONLINE_NO_RECONNECT") == NULL;
}

/* the state of a reconnect's start (the connection itself is the caller's) */
static void beginReconnect(int clearIn) {
    sReconnecting = 1;
    sReconnects++;
    sReconnectUntil = SDL_GetTicks() + RECONNECT_MS;
    sReconnectTry = 0;
    sResumeSent = sResumeGot = 0;
    sLinkDecided = !sRelay;
    sKeysAt = 0;
    if (clearIn) {
        sInLen = 0;
    }
    sPendingLen = 0;
    sBatched = 0;
    memset(sRemote, 0, sizeof(sRemote));
    status("Reconnecting...");
}

/* the session's connection lost (why: the log's); reopen: this side's own connection
 * closed too, to be made again (else it waits for the other side's) */
static void startReconnect(const char* why, int reopen) {
    if (sState != PORT_ONLINE_STARTED || !reconnectAllowed()) {
        fail(sWatch ? "The connection to the host was lost." : "The other player left.");
        return;
    }
    SDL_Log("online: the connection dropped (%s): %s", why, sReconnecting ? "trying again" : "reconnecting");
    if (!sReconnecting) {
        beginReconnect(1);
    } else {
        sResumeSent = sResumeGot = 0;
        sLinkDecided = !sRelay;
        sKeysAt = 0;
        sInLen = 0;
        sPendingLen = 0;
    }
    if (sRelay) {
        portIceStop();
        sUseIce = 0;
        sGoneAt = 0;
        portSealReset();
        sPakeSent = 0;
        if (reopen) {
            portWsClose();
            /* (both sides at once would meet each other's closes: a little apart) */
            sReconnectTry = SDL_GetTicks() + 300 + SDL_rand(900);
        }
    } else if (!sHost || reopen) {
        if (sSock != BAD_SOCKET) {
            closeSocket(sSock);
        }
        sSock = BAD_SOCKET;
        sConnecting = 0;
    }
}

/* a room: the other side is in the room again (the relay's "peer"): the key again */
static void reconnectPeer(void) {
    if (!sReconnecting) {
        SDL_Log("online: the other player came back to the room");
        beginReconnect(1);
    }
    portIceStop();
    sUseIce = 0;
    portSealReset();
    sPakeSent = 0;
    sResumeSent = sResumeGot = 0;
    sLinkDecided = 0;
    sKeysAt = 0;
    sInLen = 0;
    startPake();
    if (iceAllowed()) {
        portIceStart(sHost);
    }
}

static void sendInputNow(unsigned frame, const unsigned char* pad);

/* this side's RESUME; once both have and the way is chosen, what the other is missing */
static void sendResume(void) {
    unsigned char m[12];
    put64(m, sToken);
    put32(m + 8, sRemoteNeed);
    sendMessage(MSG_RESUME, m, sizeof(m));
    flushRelay();
    sResumeSent = 1;
}

static void maybeResumed(void) {
    unsigned f, sent = 0;
    if (!sReconnecting || !sResumeGot || !sResumeSent || !sLinkDecided) {
        return;
    }
    if (sMineNext > RING && sPeerNeeds < sMineNext - RING) {
        fail("The connection was lost for too long.");
        return;
    }
    sReconnecting = 0;
    if (sUseIce) {
        portIceResume(sPeerNeeds, sRemoteNeed);
    }
    for (f = sPeerNeeds; f < sMineNext; f++) {
        if (sMineRing[f % RING].have && sMineRing[f % RING].frame == f) {
            sendInputNow(f, sMineRing[f % RING].pad);
            sent++;
        }
    }
    flushRelay();
    SDL_Log("online: reconnected (%s): %u of this side's frames sent again, from %u", portOnlineLinkName(), sent,
            sPeerNeeds);
    status("Playing online with %s.", sPeer.name[0] ? sPeer.name : "the other player");
}

static void handleResume(const unsigned char* p, size_t len) {
    if (len < 12 || get64(p) != sToken) {
        fail("The connection was changed on its way: the session ended.");
        return;
    }
    if (!sReconnecting) {
        beginReconnect(0); /* (the other side noticed first; mid-stream: what's come stays) */
        sLinkDecided = !sRelay;
    }
    sPeerNeeds = get32(p + 8);
    sResumeGot = 1;
    if (!sResumeSent && !sRelay) {
        sendResume(); /* by address: the host answers the guest's */
    }
    maybeResumed();
}

/* the relay's news: its text messages */
static void relayNews(void) {
    char text[96];
    while (portWsText(text, sizeof(text))) {
        if (strncmp(text, "you ", 4) == 0) {
            SDL_Log("online: in the room (the relay sees an %s address)", strchr(text + 4, ':') ? "IPv6" : "IPv4");
        } else if (strncmp(text, "ticket ", 7) == 0) {
            snprintf(sTicket, sizeof(sTicket), "%s", text + 7); /* this side's place, to take again after a drop */
        } else if (strncmp(text, "seatgone ", 9) == 0 && sHost) {
            /* a seat's connection to the room closed */
            const int slot = atoi(text + 9);
            int i;
            for (i = 0; i < PORT_ONLINE_MAX_SEATS; i++) {
                if (sSeats[i].here && sSeats[i].conn < 0 && sSeats[i].slot == slot) {
                    seatLeft(i);
                }
            }
        } else if (strncmp(text, "watch ", 6) == 0) {
            const int n = atoi(text + 6);
            if (sHost && n > sWatchCount) {
                sWatchCount = n;
                watchWelcome(-1); /* (everyone gets it: those that have it already pass it by) */
            }
            sWatchCount = n;
        } else if (sWatch) {
            /* a watcher: the players' comings and goings are only the host's messages' */
        } else if (strcmp(text, "peer") == 0 && sState == PORT_ONLINE_STARTED) {
            reconnectPeer(); /* the other side back in the room after a drop */
        } else if (strcmp(text, "gone") == 0 && sState == PORT_ONLINE_STARTED && !sUseIce) {
            startReconnect("the other side left the room", 0); /* (its goodbye would have come first) */
            if (sState == PORT_ONLINE_FAILED) {
                return;
            }
        } else if (strcmp(text, "peer") == 0 && sHost && sState == PORT_ONLINE_LISTENING) {
            sState = PORT_ONLINE_HANDSHAKE; /* the key, then the guest's hello */
            status("Someone is joining...");
            startPake();
            if (getenv("TTYD_ONLINE_NO_ICE") == NULL) { /* (iceAllowed, below) */
                portIceStart(1);
            }
        } else if (strcmp(text, "gone") == 0 && sHost && sState < PORT_ONLINE_WAITING_READY) {
            hostWaitsAgain("the one joining left before the game");
        } else if (strcmp(text, "gone") == 0 && !sUseIce) {
            fail("The other player left.");
            return;
        } else if (strcmp(text, "gone") == 0) {
            sGoneAt = SDL_GetTicks(); /* the direct path decides (iceGone) */
        }
    }
}

/* what the socket has now, into the buffer; 0 when the connection closed */
static int receive(void) {
    if (sRelay) {
        size_t n;
        relayNews();
        if (sState == PORT_ONLINE_FAILED) {
            return 0;
        }
        do {
            if (sInCap - sInLen < 65536) {
                sInCap = sInCap * 2 + 65536;
                sIn = (unsigned char*)realloc(sIn, sInCap);
            }
            n = portWsRead(sIn + sInLen, sInCap - sInLen);
            sInLen += n;
        } while (n > 0);
        return portWsState() != PORT_WS_CLOSED || sUseIce;
    }
    {
        int got = 0;
        for (;;) {
            int n;
            if (sInCap - sInLen < 65536) {
                sInCap = sInCap * 2 + 65536;
                sIn = (unsigned char*)realloc(sIn, sInCap);
            }
            n = (int)recv(sSock, (char*)sIn + sInLen, (int)(sInCap - sInLen), 0);
            if (n > 0) {
                sInLen += (size_t)n;
                got = 1;
            } else if (n < 0 && WOULD_BLOCK()) {
                return 1;
            } else {
                /* closed: what came before it is read first (the last frames' input, a goodbye);
                   the next read finds the close */
                return got;
            }
        }
    }
}

static void handleSession(int type, const unsigned char* p, size_t len) {
    if (type == MSG_INPUT && len >= 4 + PORT_ONLINE_PAD_BYTES) {
        const unsigned frame = get32(p);
        const size_t n = len - 4 < PORT_ONLINE_FRAME_BYTES ? len - 4 : PORT_ONLINE_FRAME_BYTES;
        sRemote[frame % RING].have = 1;
        sRemote[frame % RING].frame = frame; /* (a frame from before a recovery isn't the new one's) */
        memset(sRemote[frame % RING].pad, 0, sizeof(sRemote[frame % RING].pad));
        memcpy(sRemote[frame % RING].pad, p + 4, n); /* (the host's: its own, then the seats') */
    } else if (type == MSG_RESYNC && !sHost) {
        free(sResync);
        sResync = (unsigned char*)malloc(len > 0 ? len : 1);
        memcpy(sResync, p, len);
        sResyncLen = len;
    } else if (type == MSG_RESYNC_ASK && sHost && len >= 4) {
        sResyncAsked = (int)get32(p);
    } else if (type == MSG_HASH && len >= 12) {
        const unsigned frame = get32(p);
        sRemoteHash[(frame / PORT_ONLINE_HASH_EVERY) % 16].frame = frame;
        sRemoteHash[(frame / PORT_ONLINE_HASH_EVERY) % 16].hash = get64(p + 4);
        sRemoteHash[(frame / PORT_ONLINE_HASH_EVERY) % 16].have = 1;
    } else if (type == MSG_PING) {
        sendMessage(MSG_PONG, p, len);
    } else if (type == MSG_RESUME) {
        handleResume(p, len);
    } else if (type == MSG_BYE) {
        char why[160];
        snprintf(why, sizeof(why), "%.*s", (int)(len < sizeof(why) - 1 ? len : sizeof(why) - 1), (const char*)p);
        fail(why[0] != '\0' ? why : "The other player left.");
    }
}

/* the connection is there: the TCP one, or the relay's (or the direct path, which
 * carries the session without it) */
static int linkUp(void) {
    return sRelay ? sUseIce || portWsState() != PORT_WS_CLOSED : sSock != BAD_SOCKET;
}

/* the direct path's own lines, to the other side through the room */
static void sendIceSignals(void) {
    char line[2048];
    /* (sealed only: this side's addresses aren't the relay's business) */
    while (sRelay && portSealReady() && portIceNextSignal(line, sizeof(line))) {
        sendMessage(MSG_ICE, line, strlen(line));
    }
}

/* everything waiting, handled; 0 when the connection is gone */
static int pumpSocket(void) {
    unsigned char* p;
    size_t len;
    int type;
    if (!linkUp()) {
        return 0;
    }
    if (!receive()) {
        if (sState == PORT_ONLINE_STARTED && reconnectAllowed()) {
            startReconnect(sRelay ? "the relay's connection closed" : "the connection closed", 1);
        } else if (sState != PORT_ONLINE_FAILED) {
            fail(sRelay && portWsError()[0] != '\0' ? portWsError()
                 : sWatch                       ? "The connection to the host was lost."
                                                : "The other player left.");
        }
        return 0;
    }
    if (sState != PORT_ONLINE_STARTED || sReconnecting) {
        sendIceSignals();
    }
    while ((type = takeMessage(&p, &len)) != 0) {
        if (type == MSG_FROM_SEAT && sHost && sRelay) {
            seatFromRelay(p, len); /* a seat's, through the room (sealed for the host) */
            dropMessage();
            continue;
        }
        if (sWatch && sRelay) {
            /* a watcher's room: only the host's messages for its watchers, sealed for them */
            if (type == MSG_WSEALED) {
                size_t n = 0;
                if (sOpenedCap < len) {
                    sOpenedCap = len;
                    sOpened = (unsigned char*)realloc(sOpened, sOpenedCap);
                }
                if (portWatchOpen(p, len, sOpened, &n) && n >= 1) {
                    watchHandle(sOpened[0], sOpened + 1, n - 1);
                } else if (sGotSetupId == 0 && sState < PORT_ONLINE_GOT_SETUP) {
                    dropMessage();
                    fail("That code isn't right: check its second half.");
                    return 0;
                }
            }
            dropMessage();
            continue;
        }
        if (sRelay) {
            /* a room's: the key's rounds, then only sealed messages (a relay can't add any) */
            if (type == MSG_PAKE1) {
                unsigned char m[512];
                size_t n = 0;
                startPake();
                if (sState == PORT_ONLINE_FAILED || !portPakeRoundOne(p, len, m, sizeof(m), &n)) {
                    dropMessage();
                    if (sState == PORT_ONLINE_STARTED) {
                        startReconnect("the key's first round didn't take", 1);
                    } else {
                        fail("A secure connection couldn't be set up.");
                    }
                    return 0;
                }
                sendRaw(MSG_PAKE2, m, n);
                dropMessage();
                continue;
            }
            if (type == MSG_PAKE2) {
                if (!portPakeRoundTwo(p, len)) {
                    dropMessage();
                    if (sState == PORT_ONLINE_STARTED) {
                        startReconnect("the key's second round didn't take", 1);
                    } else {
                        fail("A secure connection couldn't be set up.");
                    }
                    return 0;
                }
                dropMessage();
                if (sState == PORT_ONLINE_STARTED) {
                    /* a reconnect: the key again; the guest says where it is (the host,
                       once it has chosen the way: reconnectPump) */
                    sKeysAt = SDL_GetTicks();
                    if (!sHost) {
                        sendResume();
                    }
                } else if (!sHost) { /* the key is there: the handshake, sealed */
                    sendHello();
                }
                sendIceSignals();
                continue;
            }
            if (type == MSG_BYE && !sHost && sState < PORT_ONLINE_WAITING_SETUP) {
                /* the host couldn't open this side's hello: another code. (Unsealed, so
                   it could be anyone's: our own words, not its; and it can only end the try.) */
                dropMessage();
                fail("That code isn't right: check its second half.");
                return 0;
            }
            if (type != MSG_SEALED) {
                dropMessage();
                continue;
            }
            if (sOpenedCap < len) {
                sOpenedCap = len;
                sOpened = (unsigned char*)realloc(sOpened, sOpenedCap);
            }
            {
                size_t n = 0;
                if (!portOpen(PORT_SEAL_STREAM, p, len, sOpened, &n) || n < 1) {
                    /* a different secret (or a meddling relay): the keys don't match */
                    dropMessage();
                    if (sHost && sState < PORT_ONLINE_WAITING_READY) {
                        sendRaw(MSG_BYE, "code", 4); /* unsealed: there's no key between them */
                        flushRelay();
                        /* each try is one guess at the secret half: a few, then a new room */
                        if (++sWrongCodes >= 5) {
                            fail("Too many tries with the wrong code: host again for a new room code.");
                            return 0;
                        }
                        hostWaitsAgain("someone tried to join with a different code");
                        return 1;
                    }
                    if (sReconnecting) { /* (one from before the key again: start the key over) */
                        startReconnect("a message didn't open", 1);
                        return 0;
                    }
                    fail(sHost ? "The connection was changed on its way: the session ended."
                               : "That code isn't right: check its second half.");
                    return 0;
                }
                type = sOpened[0];
                p = sOpened + 1;
                len = n - 1;
            }
        }
        if (type == MSG_ICE) {
            char line[2048];
            snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) - 1 ? len : sizeof(line) - 1),
                     (const char*)p);
            portIceRemoteSignal(line);
        } else if (type == MSG_LINK && !sHost && len >= 1) {
            /* the host's word, as it is: both sides must send the same way (a path
               the host has works both ways, even before this side's agent says so) */
            sUseIce = p[0] == 1;
            if (sReconnecting) {
                sLinkDecided = 1;
                maybeResumed();
            }
        } else if (sWatch && ((type >= MSG_W_SETUP && type <= MSG_W_RESYNC) || type == MSG_S_REFUSE)) {
            watchHandle(type, p, len); /* a watcher by address */
        } else if (sState == PORT_ONLINE_STARTED) {
            handleSession(type, p, len);
        } else if (type == MSG_S_HELLO && sHost && !sRelay) {
            /* by address, a seat before the guest: kept as one, the port still waits */
            int i;
            for (i = 0; i < MAX_CONNS && sConns[i].kind != CONN_FREE; i++) {
            }
            if (i == MAX_CONNS) {
                closeSocket(sSock);
            } else {
                sConns[i].s = sSock;
                sConns[i].kind = CONN_PENDING;
                sConns[i].in.len = sConns[i].out.len = 0;
                sConns[i].since = SDL_GetTicks();
                (void)seatHello(i, -1, p, len); /* (refused: its connection closed) */
            }
            dropMessage();
            if (i < MAX_CONNS && sConns[i].kind == CONN_SEAT && sInLen > 0) {
                bufAdd(&sConns[i].in, sIn, sInLen); /* (what came after its hello: its own) */
            }
            sSock = BAD_SOCKET;
            sInLen = 0;
            sState = PORT_ONLINE_LISTENING;
            status("Waiting for the other player to join (port %d)...", sHostPort);
            return 0;
        } else if (type == MSG_HELLO) {
            PortOnlineHello who;
            int role = 0;
            const char* why = parseHello(p, len, &who, &role);
            if (sHost && role) {
                /* by address, a watcher before the guest: kept as one, the port still waits */
                int i;
                dropMessage();
                for (i = 0; i < MAX_CONNS && sConns[i].kind != CONN_FREE; i++) {
                }
                if (why == NULL && i == MAX_CONNS) {
                    why = "As many are watching that game as can.";
                }
                if (why != NULL) {
                    sendMessage(MSG_BYE, why, strlen(why));
                    closeSocket(sSock);
                } else {
                    unsigned char m[HELLO_SIZE];
                    sConns[i].s = sSock;
                    sConns[i].kind = CONN_WATCHER;
                    sConns[i].in.len = sConns[i].out.len = 0;
                    makeHello(m);
                    connSend(i, MSG_HELLO, m, sizeof(m));
                    SDL_Log("online: a watcher is here, before the other player");
                }
                sSock = BAD_SOCKET;
                sInLen = 0;
                sState = PORT_ONLINE_LISTENING;
                status("Waiting for the other player to join (port %d)...", sHostPort);
                return 0;
            }
            if (why == NULL) {
                sPeer = who;
                sPeerRole = role;
            }
            if (why != NULL) {
                sendMessage(MSG_BYE, why, strlen(why));
                dropMessage();
                fail(why);
                return 0;
            }
            if (sHost) {
                sendHello();
                sPingSent = SDL_GetTicks();
                sendMessage(MSG_PING, "p", 1);
                sState = PORT_ONLINE_PEER_HERE;
                status("%s joined.", sPeer.name[0] != '\0' ? sPeer.name : "The other player");
            } else if (sWatch) {
                sState = PORT_ONLINE_WAITING_SETUP;
                status("Connected: waiting for the players to start.");
            } else {
                sState = PORT_ONLINE_WAITING_SETUP;
                status("Connected to %s: waiting for them to start.", sPeer.name[0] ? sPeer.name : "the host");
            }
        } else if (type == MSG_PING) {
            sendMessage(MSG_PONG, p, len);
        } else if (type == MSG_PONG) {
            sRtt = (int)(SDL_GetTicks() - sPingSent);
        } else if (type == MSG_SETUP && !sHost) {
            free(sSetup);
            sSetup = (unsigned char*)malloc(len);
            memcpy(sSetup, p, len);
            sSetupLen = len;
            sToken = fnv64(p, len); /* (the session's, for a reconnect) */
            sState = PORT_ONLINE_GOT_SETUP;
            status("Getting ready...");
        } else if (type == MSG_READY && sHost) {
            /* the players (more than two: the seats' names with it; two: nothing, as before) */
            unsigned char m[1 + PORT_ONLINE_MAX_SEATS * 32];
            memset(m, 0, sizeof(m));
            m[0] = (unsigned char)sPlayers;
            memcpy(m + 1, sNames[2], 32);
            memcpy(m + 33, sNames[3], 32);
            sendMessage(MSG_START, m, sPlayers > 2 ? sizeof(m) : 0);
            sState = PORT_ONLINE_STARTED;
            status("Playing online with %s.", sPeer.name[0] ? sPeer.name : "the other player");
        } else if (type == MSG_START && !sHost) {
            sPlayers = len >= 1 + PORT_ONLINE_MAX_SEATS * 32 && p[0] > 2 && p[0] <= PORT_ONLINE_MAX_PLAYERS ? p[0] : 2;
            if (sPlayers > 2) {
                memcpy(sNames[2], p + 1, 32);
                memcpy(sNames[3], p + 33, 32);
                sNames[2][31] = sNames[3][31] = '\0';
            }
            memcpy(sNames[0], sPeer.name, 32);
            memcpy(sNames[1], sMe.name, 32);
            sState = PORT_ONLINE_STARTED;
            status("Playing online with %s.", sPeer.name[0] ? sPeer.name : "the host");
        } else if (type == MSG_BYE) {
            char why[160];
            snprintf(why, sizeof(why), "%.*s", (int)(len < sizeof(why) - 1 ? len : sizeof(why) - 1),
                     (const char*)p);
            dropMessage();
            if (sHost && strcmp(why, PORT_ONLINE_BYE_REJOIN) == 0 && (sRelay || sListen != BAD_SOCKET) &&
                (sState == PORT_ONLINE_PEER_HERE || sState == PORT_ONLINE_WAITING_READY)) {
                /* the guest joined from its running game: it starts again and comes back */
                sRejoinExpected = 1;
                if (!sRelay && sSock != BAD_SOCKET) { /* by address: the port takes it again */
                    closeSocket(sSock);
                    sSock = BAD_SOCKET;
                }
                hostWaitsAgain("the guest starts its game again to play");
                return 0;
            }
            fail(why[0] != '\0' ? why : "The other player left.");
            return 0;
        }
        dropMessage();
        if (!linkUp() || sState == PORT_ONLINE_FAILED) {
            return 0;
        }
    }
    return linkUp();
}

int portOnlineHost(int port, const PortOnlineHello* me) {
    struct sockaddr_in6 addr;
    int no = 0, yes = 1;
    portOnlineCancel(NULL);
    sMe = *me;
    sHost = 1;
    if (!netStart()) {
        fail("No network.");
        return 0;
    }
    /* IPv6 and IPv4 both (dual stack) */
    sListen = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (sListen == BAD_SOCKET) {
        fail("No network.");
        return 0;
    }
    setsockopt(sListen, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&no, sizeof(no));
    setsockopt(sListen, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons((unsigned short)port);
    /* tests ($TTYD_ONLINE_HOST): this computer only, no firewall's question,
       unless $TTYD_ONLINE_LAN (a test between two computers) */
    addr.sin6_addr = getenv("TTYD_ONLINE_HOST") != NULL && getenv("TTYD_ONLINE_LAN") == NULL ? in6addr_loopback
                                                                                             : in6addr_any;
    if (bind(sListen, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(sListen, 1) != 0) {
        fail("That port is in use on this computer: choose another.");
        return 0;
    }
    nonBlocking(sListen);
    sHostPort = port;
    sState = PORT_ONLINE_LISTENING;
    status("Waiting for the other player to join (port %d)...", port);
    return 1;
}

/* a connection to address:port begun (sSock, not waiting); 0: not found (1), unreachable (2) */
static int tcpConnect(const char* address, int port) {
    struct addrinfo hints, *found = NULL, *a;
    char service[16];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(address, service, &hints, &found) != 0 || found == NULL) {
        return 1;
    }
    for (a = found; a != NULL; a = a->ai_next) {
        sSock = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (sSock == BAD_SOCKET) {
            continue;
        }
        nonBlocking(sSock);
        if (connect(sSock, a->ai_addr, (int)a->ai_addrlen) == 0 || WOULD_BLOCK()) {
            break;
        }
        closeSocket(sSock);
        sSock = BAD_SOCKET;
    }
    freeaddrinfo(found);
    if (sSock == BAD_SOCKET) {
        return 2;
    }
    sConnecting = 1;
    sConnectAt = SDL_GetTicks();
    return 0;
}

int portOnlineJoin(const char* address, int port, const PortOnlineHello* me) {
    int why;
    portOnlineCancel(NULL);
    sMe = *me;
    sHost = 0;
    if (!netStart()) {
        fail("No network.");
        return 0;
    }
    snprintf(sJoinHost, sizeof(sJoinHost), "%s", address);
    sJoinPort = port;
    why = tcpConnect(address, port);
    if (why != 0) {
        fail(why == 1 ? "That address wasn't found." : "Couldn't reach that address.");
        return 0;
    }
    sPingSent = SDL_GetTicks();
    sState = PORT_ONLINE_CONNECTING;
    status("Connecting to %s...", address);
    return 1;
}

int portOnlineWatch(const char* address, int port, const PortOnlineHello* me) {
    if (!portOnlineJoin(address, port, me)) {
        return 0;
    }
    sWatch = 1; /* (its hello says so) */
    return 1;
}

/* a seat's message to the host: through a room sealed with the watchers' key (the relay
 * gives it to the host only), by address as it is */
static void seatSend(int type, const void* payload, size_t len) {
    unsigned char *inner, *sealed;
    size_t n;
    if (!sRelay) {
        sendRaw(type, payload, len);
        return;
    }
    inner = (unsigned char*)malloc(len + 1);
    sealed = (unsigned char*)malloc(len + 1 + PORT_WATCH_OVERHEAD);
    if (inner != NULL && sealed != NULL) {
        inner[0] = (unsigned char)type;
        if (len > 0) {
            memcpy(inner + 1, payload, len);
        }
        n = portWatchSeal(inner, len + 1, sealed);
        if (n > 0) {
            sendRaw(MSG_WSEALED, sealed, n);
            flushRelay();
        }
    }
    free(sealed);
    free(inner);
}

/* a seat's hello: its id (to find itself in the setup), then the hello */
static void seatSendHello(void) {
    unsigned char m[8 + HELLO_SIZE];
    put64(m, sSeatId);
    makeHello(m + 8);
    seatSend(MSG_S_HELLO, m, sizeof(m));
}

/* a seat's pads so far, to the host */
static void seatFlush(void) {
    unsigned char head[5];
    Buf m = {0};
    if (sSeatOutCount == 0) {
        return;
    }
    put32(head, sSeatOutFirst);
    head[4] = (unsigned char)sSeatOutCount;
    bufAdd(&m, head, sizeof(head));
    bufAdd(&m, sSeatOut.p, sSeatOut.len);
    seatSend(MSG_S_PAD, m.p, m.len);
    free(m.p);
    sSeatOut.len = 0;
    sSeatOutCount = 0;
}

static void seatBegin(const PortOnlineHello* me) {
    sSeat = 1;
    sWatch = 1; /* (it gets what watchers get) */
    sMe = *me;
    sSeatId = ((unsigned long long)SDL_rand_bits() << 32) | SDL_rand_bits() | 1;
}

int portOnlineSeat(const char* address, int port, const PortOnlineHello* me) {
    if (!portOnlineJoin(address, port, me)) {
        return 0;
    }
    seatBegin(me); /* (its hello, a seat's, once connected) */
    return 1;
}

/* --- rooms on the relay --- */

/* The relays, in order (the build's TTYD_ONLINE_RELAY, or $TTYD_ONLINE_RELAY:
 * addresses separated by spaces). Each is a free Cloudflare account's, with
 * its own daily allowance: a host whose relay is full for the day takes the
 * next. A room's code says which: its first character, the relay's place in
 * PORT_ONLINE_CODE_CHARS (both games have the same list: the same version). */
#define MAX_RELAYS 8
static char sRelays[MAX_RELAYS][256];
static int sRelayCount = -1, sRelayAt;

static void loadRelays(void) {
    const char* env = getenv("TTYD_ONLINE_RELAY");
#ifdef PORT_ONLINE_RELAY_URL
    const char* p = env != NULL ? env : PORT_ONLINE_RELAY_URL;
#else
    const char* p = env != NULL ? env : "";
#endif
    if (sRelayCount >= 0) {
        return;
    }
    sRelayCount = 0;
    while (*p != '\0' && sRelayCount < MAX_RELAYS) {
        size_t n = 0;
        while (*p == ' ') {
            p++;
        }
        while (p[n] != '\0' && p[n] != ' ' && n < sizeof(sRelays[0]) - 1) {
            n++;
        }
        if (n > 0) {
            memcpy(sRelays[sRelayCount], p, n);
            while (n > 0 && sRelays[sRelayCount][n - 1] == '/') {
                n--;
            }
            sRelays[sRelayCount++][n] = '\0';
        }
        p += n;
    }
}

const char* portOnlineRelay(void) {
    loadRelays();
    return sRelayCount > 0 ? sRelays[0] : "";
}

const char* portOnlineRelayAt(int i) {
    loadRelays();
    return i >= 0 && i < sRelayCount ? sRelays[i] : NULL;
}

void portOnlineListRoom(const char* name) {
    sListOn = name != NULL;
    snprintf(sListName, sizeof(sListName), "%s", name != NULL ? name : "");
}

int portOnlineRejoinExpected(void) {
    const int was = sRejoinExpected;
    sRejoinExpected = 0;
    return was;
}

/* the relay a code names, or -1 */
static int relayOf(const char* code) {
    const char* at = strchr(PORT_ONLINE_CODE_CHARS, code[0]);
    loadRelays();
    return at != NULL && code[0] != '\0' && at - PORT_ONLINE_CODE_CHARS < sRelayCount
               ? (int)(at - PORT_ONLINE_CODE_CHARS)
               : -1;
}

/* the direct path off ($TTYD_ONLINE_NO_ICE): tests of the relay itself */
static int iceAllowed(void) {
    return getenv("TTYD_ONLINE_NO_ICE") == NULL;
}

static int openRoom(const char* code, const char* role, const PortOnlineHello* me) {
    char url[700];
    int relay;
    portOnlineCancel(NULL);
    sMe = *me;
    if (portOnlineRelay()[0] == '\0') {
        fail("There's no relay in this build: connect by address.");
        return 0;
    }
    snprintf(sRoom, sizeof(sRoom), "%s", code);
    if (strcmp(role, "host") == 0) { /* the code names the relay the host is on */
        sRoom[0] = PORT_ONLINE_CODE_CHARS[sRelayAt];
    }
    relay = relayOf(sRoom);
    if (relay < 0 || strlen(sRoom) != PORT_ONLINE_CODE_LENGTH) {
        fail("That room code isn't from this version of Paper Cut: check it.");
        return 0;
    }
    /* the room's half only: the secret one stays between the two games */
    snprintf(url, sizeof(url), "%s/room/%.*s?role=%s", sRelays[relay], PORT_ONLINE_ROOM_HALF, sRoom, role);
    if (sListOn && strcmp(role, "host") == 0) {
        /* listed (the room list): open to anyone, so the relay has the whole code, with the
           host's name and what it plays (only the same release and version can join) */
        char name[3 * sizeof(sListName)];
        const char* hex = "0123456789ABCDEF";
        size_t n = 0, len;
        const unsigned char* c;
        for (c = (const unsigned char*)sListName; *c != '\0' && n + 4 < sizeof(name); c++) {
            if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')) {
                name[n++] = (char)*c;
            } else {
                name[n++] = '%';
                name[n++] = hex[*c >> 4];
                name[n++] = hex[*c & 15];
            }
        }
        name[n] = '\0';
        len = strlen(url);
        /* (max: how many may play, for the list's "1/4"; a relay before three or four players passes it by) */
        snprintf(url + len, sizeof(url) - len, "&list=%s&name=%s&game=%.6s&v=%s&max=%d", sRoom + PORT_ONLINE_ROOM_HALF,
                 name, me->game, me->version, PORT_ONLINE_MAX_PLAYERS);
        sListedAt = SDL_GetTicks();
    }
    snprintf(sRoleName, sizeof(sRoleName), "%s", role);
    sRelay = 1;
    if (!portWsOpen(url)) {
        fail(portWsError()[0] != '\0' ? portWsError() : "The relay couldn't be reached.");
        return 0;
    }
    sPingSent = SDL_GetTicks();
    return 1;
}

int portOnlineHostRoom(const char* code, const PortOnlineHello* me) {
    sHost = 1;
    sRelayAt = 0;
    sWrongCodes = 0;
    if (!openRoom(code, "host", me)) {
        return 0;
    }
    /* the watchers' key now, in the lobby (PBKDF2 takes a moment: never during the game) */
    portWatchKey(sRoom);
    sState = PORT_ONLINE_LISTENING;
    status("Opening a room...");
    return 1;
}

/* the host's relay full for today: the next one's, the same code but its first character */
static int nextRelay(void) {
    char code[16];
    if (!sHost || sState != PORT_ONLINE_LISTENING || strstr(portWsError(), "full") == NULL ||
        sRelayAt + 1 >= sRelayCount) {
        return 0;
    }
    snprintf(code, sizeof(code), "%s", sRoom);
    SDL_Log("online: relay %d is full for today: the next", sRelayAt + 1);
    sRelayAt++;
    {
        const int at = sRelayAt; /* (openRoom's cancel keeps it) */
        if (!openRoom(code, "host", &sMe)) {
            return 0;
        }
        sRelayAt = at;
    }
    portWatchKey(sRoom);
    sState = PORT_ONLINE_LISTENING;
    status("Opening a room...");
    return 1;
}

int portOnlineJoinRoom(const char* code, const PortOnlineHello* me) {
    sHost = 0;
    if (!openRoom(code, "guest", me)) {
        return 0;
    }
    sState = PORT_ONLINE_CONNECTING;
    {
        char half[PORT_ONLINE_ROOM_HALF + 1]; /* (the room's half: logs go out with crash reports) */
        snprintf(half, sizeof(half), "%s", code);
        status("Joining room %s...", half);
    }
    return 1;
}

int portOnlineSeatRoom(const char* code, const PortOnlineHello* me) {
    sHost = 0;
    if (!openRoom(code, "seat", me)) {
        return 0;
    }
    seatBegin(me);
    if (!portWatchKey(sRoom)) {
        fail("A secure connection couldn't be set up.");
        return 0;
    }
    sState = PORT_ONLINE_CONNECTING;
    {
        char half[PORT_ONLINE_ROOM_HALF + 1]; /* (the room's half: logs go out with crash reports) */
        snprintf(half, sizeof(half), "%s", code);
        status("Joining room %s to play...", half);
    }
    return 1;
}

int portOnlineWatchRoom(const char* code, const PortOnlineHello* me) {
    sHost = 0;
    if (!openRoom(code, "watch", me)) {
        return 0;
    }
    sWatch = 1;
    if (!portWatchKey(sRoom)) {
        fail("A secure connection couldn't be set up.");
        return 0;
    }
    sState = PORT_ONLINE_CONNECTING;
    {
        char half[PORT_ONLINE_ROOM_HALF + 1]; /* (the room's half: logs go out with crash reports) */
        snprintf(half, sizeof(half), "%s", code);
        status("Joining room %s to watch...", half);
    }
    return 1;
}

const char* portOnlineRoom(void) {
    /* (shown once the room is open: until then the relay, and so the code, may change) */
    return sRelay && portWsState() == PORT_WS_OPEN ? sRoom : "";
}

static int iceCarries(void) {
    return sRelay && iceAllowed() && portIceState() == PORT_ICE_OK;
}

int portOnlineDelayExtra(void) {
    return sRelay && !(sState == PORT_ONLINE_STARTED ? sUseIce : iceCarries()) ? PORT_ONLINE_BATCH - 1 : 0;
}

const char* portOnlineLinkName(void) {
    if (!sRelay) {
        return "direct";
    }
    return (sState == PORT_ONLINE_STARTED ? sUseIce : iceCarries()) ? "direct (UDP)" : "relay";
}

static void relayLobbyPump(void) {
    const int ws = portWsState();
    if (ws == PORT_WS_CLOSED) {
        if (nextRelay()) {
            return;
        }
        fail(portWsError()[0] != '\0' ? portWsError() : "The connection to the relay was lost.");
        return;
    }
    if (ws != PORT_WS_OPEN) {
        return;
    }
    if (sState == PORT_ONLINE_CONNECTING && sWatch) { /* a watcher, in the room: the host's setup next */
        sState = PORT_ONLINE_WAITING_SETUP;
        status("Connected: waiting for the players to start.");
        if (sSeat) {
            seatSendHello(); /* (the host takes it, or says no) */
        }
        return;
    }
    if (sState == PORT_ONLINE_CONNECTING) { /* the guest, in the room: the key, then the handshake */
        sState = PORT_ONLINE_HANDSHAKE;
        startPake();
        if (iceAllowed()) {
            portIceStart(0);
        }
        status("Connected: checking the game matches...");
        return;
    }
    if (sState == PORT_ONLINE_LISTENING && strncmp(sStatus, "Opening", 7) == 0) {
        status("Room %.4s: waiting for the other player to join.", sRoom); /* (the room's half: logs go out with crash reports) */
    }
    if (sHost && sListOn && sState == PORT_ONLINE_LISTENING && SDL_GetTicks() - sListedAt > LIST_REFRESH_MS) {
        sListedAt = SDL_GetTicks(); /* the listing kept (a relay without the room list passes it by) */
        portWsSendText("list");
    }
    pumpSocket();
}

void portOnlineLobbyPump(void) {
    if (sRelay) {
        relayLobbyPump();
        return;
    }
    if (sState == PORT_ONLINE_LISTENING && sListen != BAD_SOCKET) {
        Socket s;
        connsPump(); /* (the seats and watchers here before the guest) */
        s = accept(sListen, NULL, NULL);
        if (s != BAD_SOCKET) {
            /* (the port stays open: watchers, and the guest again after a drop) */
            sSock = s;
            nonBlocking(sSock);
            sState = PORT_ONLINE_HANDSHAKE;
            status("Someone is joining...");
        }
        return;
    }
    if (sConnecting) {
        fd_set w, e;
        struct timeval none = {0, 0};
        FD_ZERO(&w);
        FD_ZERO(&e);
        FD_SET(sSock, &w);
        FD_SET(sSock, &e);
        if (select((int)sSock + 1, NULL, &w, &e, &none) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(sSock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            if (err != 0 || FD_ISSET(sSock, &e)) {
                fail("Couldn't reach the host: check the address and that they are waiting.");
                return;
            }
            sConnecting = 0;
            sState = PORT_ONLINE_HANDSHAKE;
            if (sSeat) {
                seatSendHello();
            } else {
                sendHello();
            }
            status("Connected: checking the game matches...");
        } else if (SDL_GetTicks() - sPingSent > 10000) {
            fail("Couldn't reach the host: check the address and that they are waiting.");
        }
        return;
    }
    if (sState != PORT_ONLINE_IDLE && sState != PORT_ONLINE_FAILED) {
        connsPump();
        if (sState != PORT_ONLINE_LISTENING) {
            pumpSocket();
        }
    }
}

int portOnlineLobbyState(void) {
    return sState;
}

const char* portOnlineStatus(void) {
    return sStatus;
}

const PortOnlineHello* portOnlinePeer(void) {
    return &sPeer;
}

const PortOnlineHello* portOnlineMe(void) {
    return &sMe;
}

int portOnlineRoundTrip(void) {
    if (iceCarries() && portIceRoundTrip() >= 0) {
        return portIceRoundTrip(); /* the session will go that way */
    }
    return sRtt;
}

int portOnlineIsHost(void) {
    return sHost;
}

int portOnlineSendSetup(const void* setup, size_t len) {
    if (!sHost || sState != PORT_ONLINE_PEER_HERE) {
        return 0;
    }
    if (sRelay) { /* how the session goes: direct when the path is there, the relay otherwise */
        const unsigned char direct = (unsigned char)iceCarries();
        sUseIce = direct;
        sendMessage(MSG_LINK, &direct, 1);
        SDL_Log("online: the session goes %s", direct ? "direct (UDP)" : "through the relay");
    }
    if (!sendMessage(MSG_SETUP, setup, len)) {
        fail("The other player left.");
        return 0;
    }
    free(sHostSetup);
    sHostSetup = (unsigned char*)malloc(len > 0 ? len : 1);
    memcpy(sHostSetup, setup, len);
    sHostSetupLen = len;
    {
        /* the players, fixed now: the host, the guest, the seats here in the order they came */
        int i;
        memset(sNames, 0, sizeof(sNames));
        memcpy(sNames[0], sMe.name, 32);
        memcpy(sNames[1], sPeer.name, 32);
        sPlayers = 2;
        for (i = 0; i < PORT_ONLINE_MAX_SEATS; i++) {
            if (sSeats[i].here) {
                sSeats[i].player = sPlayers;
                sSeats[i].gone = 0;
                sSeats[i].qLen = 0;
                seatNeutral(sSeats[i].last, 1);
                memcpy(sNames[sPlayers], sSeats[i].name, 32);
                sPlayers++;
            }
        }
        sMineBytes = (size_t)(sPlayers - 1) * PORT_ONLINE_PAD_BYTES; /* its own, then the seats' */
        sSelf = 0;
        SDL_Log("online: the session's players: %d", sPlayers);
    }
    sToken = fnv64(sHostSetup, len);
    sSetupId = ((unsigned long long)SDL_rand_bits() << 32) | SDL_rand_bits();
    if (sSetupId == 0) {
        sSetupId = 1;
    }
    sWatchSent = 0;
    sHist.len = sHashHist.len = 0;
    sHashSent = 0;
    watchWelcome(-1); /* the watchers already here start with it too */
    sState = PORT_ONLINE_WAITING_READY;
    status("Starting...");
    return 1;
}

const void* portOnlineSetup(size_t* len) {
    *len = sSetupLen;
    return sSetup;
}

void portOnlineGuestReady(void) {
    if (sWatch && sState == PORT_ONLINE_GOT_SETUP) { /* a watcher starts at once: the input will come */
        sState = PORT_ONLINE_STARTED;
        if (sSeat) {
            status("Playing online as player %d with %s.", sSelf + 1, sPeer.name[0] ? sPeer.name : "the host");
        } else {
            status("Watching %s and %s.", sPeer.name[0] ? sPeer.name : "the host",
                   sPeer2.name[0] ? sPeer2.name : "the other player");
        }
        return;
    }
    if (!sHost && sState == PORT_ONLINE_GOT_SETUP && sendMessage(MSG_READY, NULL, 0)) {
        sState = PORT_ONLINE_WAITING_START;
        sSelf = 1;
        sMineBytes = PORT_ONLINE_PAD_BYTES;
    }
}

void portOnlineCancel(const char* why) {
    if (why != NULL && sHost) {
        watchSendEnd(why);
    }
    if (why != NULL && sSeat && !sRefused && linkUp()) {
        seatFlush();
        seatSend(MSG_S_BYE, why, strlen(why)); /* (its place freed at once) */
    }
    if (why != NULL && sUseIce) {
        portIceSendBye(why);
    }
    if (why != NULL && (sSock != BAD_SOCKET || (sRelay && portWsState() == PORT_WS_OPEN))) {
        sendMessage(MSG_BYE, why, strlen(why)); /* (the relay's goes out before its socket closes) */
    }
    closeAll();
    memset(sRemote, 0, sizeof(sRemote));
    memset(sRemoteHash, 0, sizeof(sRemoteHash));
    /* watching's and reconnecting's own */
    sWatch = sPeerRole = sWatchCount = 0;
    sHist.len = sHashHist.len = sGot.len = sHostHashes.len = 0;
    sWatchSent = 0;
    sHashSent = 0;
    free(sHostSetup);
    sHostSetup = NULL;
    sHostSetupLen = 0;
    sSetupId = sGotSetupId = 0;
    sHostEnded = 0;
    sWatchResyncEpoch = 0;
    memset(&sPeer2, 0, sizeof(sPeer2));
    portWatchReset();
    /* the seats' and the players' */
    sSeat = sRefused = 0;
    sSelf = -1;
    sSeatId = 0;
    sPlayers = 2;
    memset(sNames, 0, sizeof(sNames));
    memset(sSeats, 0, sizeof(sSeats));
    sSeatOut.len = 0;
    sSeatOutCount = sSeatOutFirst = 0;
    sMineBytes = PORT_ONLINE_PAD_BYTES;
    sReconnecting = sResumeSent = sResumeGot = 0;
    memset(sMineRing, 0, sizeof(sMineRing));
    sMineNext = sRemoteNeed = sPeerNeeds = 0;
    sToken = 0;
    sTicket[0] = '\0';
    sState = PORT_ONLINE_IDLE;
    sRtt = -1;
    sStatus[0] = '\0';
}

/* this computer's address on its network, for the host to tell the guest: the
 * one a connection outwards would use (a UDP socket connected, nothing sent) */
int portOnlineLocalAddress(char* out, size_t size) {
    struct sockaddr_in to, me;
    socklen_t len = sizeof(me);
    Socket s;
    out[0] = '\0';
    if (!netStart() || (s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == BAD_SOCKET) {
        return 0;
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "192.0.2.1", &to.sin_addr); /* any routable address (a documentation one) */
    if (connect(s, (struct sockaddr*)&to, sizeof(to)) == 0 && getsockname(s, (struct sockaddr*)&me, &len) == 0) {
        inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)size);
    }
    closeSocket(s);
    return out[0] != '\0';
}

/* --- the session --- */

int portOnlineConnected(void) {
    return sState == PORT_ONLINE_STARTED && (linkUp() || sReconnecting);
}

int portOnlineReconnecting(void) {
    return sReconnecting;
}

int portOnlineReconnected(void) {
    return sReconnects;
}

/* the direct path's news: the other side's goodbye */
static int iceGone(void) {
    const char* why = sUseIce ? portIceBye() : NULL;
    if (sUseIce && why == NULL) {
        /* closed without a goodbye: the relay saw its connection go, and the path is quiet */
        relayNews();
        if (sGoneAt != 0 && portIceSilentMs() > 1000) {
            startReconnect("the other side left the room, the direct path quiet", 0);
            return sState != PORT_ONLINE_STARTED;
        }
    }
    if (why != NULL) {
        char copy[160];
        snprintf(copy, sizeof(copy), "%s", why);
        fail(copy);
        return 1;
    }
    return 0;
}

static void sendInputNow(unsigned frame, const unsigned char* pad) {
    unsigned char m[4 + PORT_ONLINE_FRAME_BYTES];
    if (sUseIce) {
        portIceSendInput(frame, pad, sMineBytes);
        return;
    }
    put32(m, frame);
    memcpy(m + 4, pad, sMineBytes);
    if (!sendMessage(MSG_INPUT, m, 4 + sMineBytes)) {
        startReconnect("a send didn't go", 1);
    }
}

void portOnlineSendInput(unsigned frame, const unsigned char pad[PORT_ONLINE_PAD_BYTES]) {
    if (sSeat) {
        /* a seat's: to the host (each frame by address; through a room, a batch at a time) */
        if (sState != PORT_ONLINE_STARTED || !linkUp()) {
            return;
        }
        if (sSeatOutCount == 0) {
            sSeatOutFirst = frame;
        }
        bufAdd(&sSeatOut, pad, PORT_ONLINE_PAD_BYTES);
        sSeatOutCount++;
        if (!sRelay || sSeatOutCount >= PORT_ONLINE_BATCH) {
            seatFlush();
        }
        return;
    }
    if (sWatch) {
        return;
    }
    /* kept, for the other side to have again after a reconnect */
    sMineRing[frame % RING].frame = frame;
    sMineRing[frame % RING].have = 1;
    memcpy(sMineRing[frame % RING].pad, pad, PORT_ONLINE_PAD_BYTES);
    if (sHost && sPlayers > 2) {
        /* the host's input carries the seats' (each one's next, as it has them now) */
        int p;
        for (p = 2; p < sPlayers; p++) {
            const int seat = seatOfPlayer(p);
            unsigned char* to = sMineRing[frame % RING].pad + (size_t)(p - 1) * PORT_ONLINE_PAD_BYTES;
            if (seat >= 0) {
                seatTake(seat, to);
            } else {
                seatNeutral(to, 0);
            }
        }
    }
    pad = sMineRing[frame % RING].pad;
    if (frame + 1 > sMineNext) {
        sMineNext = frame + 1;
    }
    if (portOnlineConnected() && !sReconnecting) {
        sendInputNow(frame, pad);
    }
}

int portOnlineSeatPads(unsigned frame, unsigned char* pads) {
    if (!sHost || sPlayers <= 2 || !sMineRing[frame % RING].have || sMineRing[frame % RING].frame != frame) {
        return 0;
    }
    memcpy(pads, sMineRing[frame % RING].pad + PORT_ONLINE_PAD_BYTES, (size_t)(sPlayers - 2) * PORT_ONLINE_PAD_BYTES);
    return 1;
}

static int haveInput(unsigned frame, unsigned char pad[PORT_ONLINE_FRAME_BYTES]) {
    if (sUseIce) {
        return portIcePeerInput(frame, pad, PORT_ONLINE_FRAME_BYTES);
    }
    if (sRemote[frame % RING].have && sRemote[frame % RING].frame == frame) {
        memcpy(pad, sRemote[frame % RING].pad, PORT_ONLINE_FRAME_BYTES);
        sRemote[frame % RING].have = 0;
        return 1;
    }
    return 0;
}

/* 2: a recovery came (or was asked for) while waiting: online_pc.c takes it */
static int resyncWaiting(void) {
    return (!sHost && sResync != NULL) || (sHost && sResyncAsked >= 0);
}

static unsigned sWatchAt; /* the watcher's frame */

/* the game held while reconnecting (vi_aurora.c): a frame with the words over it */
void portOnlineHoldFrame(const char* english);

/* a reconnect's steps, while the game waits */
static void reconnectPump(void) {
    const Uint64 now = SDL_GetTicks();
    connsPump();
    if (now > sReconnectUntil) {
        fail("The connection was lost and couldn't be made again.");
        return;
    }
    if (sRelay) {
        const int ws = portWsState();
        if (ws == PORT_WS_CLOSED || ws == PORT_WS_IDLE) {
            if (now >= sReconnectTry) { /* into the room again, in this side's place */
                char url[800];
                const int relay = relayOf(sRoom);
                if (relay < 0) {
                    fail("The connection was lost and couldn't be made again.");
                    return;
                }
                snprintf(url, sizeof(url), "%s/room/%.*s?role=%s%s%s", sRelays[relay], PORT_ONLINE_ROOM_HALF, sRoom,
                         sRoleName, sTicket[0] != '\0' ? "&ticket=" : "", sTicket);
                SDL_Log("online: into the room again");
                portSealReset();
                sPakeSent = 0;
                sInLen = 0;
                portWsOpen(url);
                sReconnectTry = now + 2000;
            }
            return;
        }
        if (ws != PORT_WS_OPEN) {
            return;
        }
        if (sHost && portSealReady() && sKeysAt != 0 && !sLinkDecided) {
            /* the way, as in the lobby: direct when the path is there again (a few seconds' try) */
            const int ice = iceAllowed() ? portIceState() : PORT_ICE_OFF;
            if (ice != PORT_ICE_TRYING || now - sKeysAt > 6000) {
                const unsigned char direct = (unsigned char)iceCarries();
                sUseIce = direct;
                sendMessage(MSG_LINK, &direct, 1);
                sLinkDecided = 1;
                sendResume();
                maybeResumed();
            }
        }
        pumpSocket();
        return;
    }
    if (sHost) { /* by address: the guest comes back on the port (connsPump) */
        if (sSock != BAD_SOCKET) {
            pumpSocket();
        }
        return;
    }
    if (sSock == BAD_SOCKET) { /* the guest: to the host again */
        if (now >= sReconnectTry) {
            sReconnectTry = now + 1000;
            if (tcpConnect(sJoinHost, sJoinPort) == 0) {
                SDL_Log("online: connecting to the host again");
            }
        }
        return;
    }
    if (sConnecting) {
        fd_set w, e;
        struct timeval none = {0, 0};
        FD_ZERO(&w);
        FD_ZERO(&e);
        FD_SET(sSock, &w);
        FD_SET(sSock, &e);
        if (select((int)sSock + 1, NULL, &w, &e, &none) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(sSock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            if (err != 0 || FD_ISSET(sSock, &e)) {
                closeSocket(sSock);
                sSock = BAD_SOCKET;
                sConnecting = 0;
                return;
            }
            sConnecting = 0;
            sendResume();
        } else if (now - sConnectAt > 3000) {
            closeSocket(sSock);
            sSock = BAD_SOCKET;
            sConnecting = 0;
        }
        return;
    }
    pumpSocket();
}

int portOnlineWaitInput(unsigned frame, unsigned char pad[PORT_ONLINE_FRAME_BYTES], int timeoutMs) {
    Uint64 start = SDL_GetTicks();
    Uint64 lastPump = start, lastStream = 0, lastHold = 0;
    for (;;) {
        if (sState != PORT_ONLINE_STARTED) {
            /* ended (their goodbye): the frames they played before it still come, so both games stop
               on the same frame (a Leave from the Start menu: each game's own) */
            return sState == PORT_ONLINE_FAILED && haveInput(frame, pad) ? 1 : 0;
        }
        if (sReconnecting) {
            reconnectPump();
            if (!sReconnecting) {
                start = SDL_GetTicks(); /* back: the input's wait begins again */
            }
        } else if (!linkUp()) {
            startReconnect("the connection closed", 1);
        } else if (iceGone()) {
            return 0;
        } else if (!sUseIce) {
            pumpSocket();
        } else if (SDL_GetTicks() - lastStream >= 10) {
            pumpSocket(); /* the direct path carries the input; the stream a recovery */
            lastStream = SDL_GetTicks();
        }
        connsPump();
        if (sState != PORT_ONLINE_STARTED) {
            return sState == PORT_ONLINE_FAILED && haveInput(frame, pad) ? 1 : 0; /* (as above) */
        }
        if (resyncWaiting()) {
            return 2;
        }
        if (!sReconnecting && haveInput(frame, pad)) {
            sRemoteNeed = frame + 1;
            return 1;
        }
        if (!sReconnecting) {
            /* waiting: this side's batch goes now, or both would wait on each other */
            flushRelay();
            if (SDL_GetTicks() - start > (Uint64)timeoutMs) {
                /* quiet this long: the connection made again (as when it closes) */
                startReconnect("the other side went quiet", 1);
            }
        }
        if (sReconnecting && SDL_GetTicks() - lastHold >= 33) {
            portOnlineHoldFrame("Reconnecting..."); /* (its events too) */
            lastHold = lastPump = SDL_GetTicks();
        } else if (SDL_GetTicks() - lastPump > 100) {
            SDL_PumpEvents(); /* the window stays responsive while waiting */
            lastPump = SDL_GetTicks();
        }
        if (sRelay || sReconnecting || sSock == BAD_SOCKET) {
            SDL_DelayNS(500000); /* the relay's thread and libjuice's fill the buffers */
        } else {
            onlineReadable(sSock, 1);
        }
    }
}

/* the watcher's next frame: both players' input (1); 0 no more is coming; 2 the players
 * start again (a recovery). It waits, the game held, for a batch to come; once it has
 * had to, until a second's input is there, so the game doesn't stop at each batch. */
int portOnlineWatchInput(unsigned frame, unsigned char* pads) {
    Uint64 start = SDL_GetTicks(), lastHold = 0, lastPump = start;
    int waited = 0;
    unsigned had = (unsigned)(sGot.len / PAIR);
    if (!sWatch) {
        return 0;
    }
    sWatchAt = frame;
    for (;;) {
        unsigned have;
        if (sState == PORT_ONLINE_STARTED && linkUp()) {
            pumpSocket();
        }
        have = (unsigned)(sGot.len / PAIR);
        if (have != had) {
            had = have;
            start = SDL_GetTicks(); /* (it's coming) */
        }
        if (sWatchResyncEpoch != 0) {
            return 2;
        }
        /* (a seat plays each frame as soon as it's there: its own input is in them) */
        if (frame < have && (sSeat || !waited || have >= frame + 60 || sHostEnded || sState != PORT_ONLINE_STARTED)) {
            memcpy(pads, sGot.p + (size_t)frame * PAIR, PAIR);
            return 1;
        }
        if (frame >= have && (sHostEnded || sState != PORT_ONLINE_STARTED)) {
            return 0;
        }
        if (SDL_GetTicks() - start > 120000) {
            fail("The players' game stopped coming.");
            return 0;
        }
        waited = 1;
        if (SDL_GetTicks() - start > 400 && SDL_GetTicks() - lastHold >= 33) {
            portOnlineHoldFrame("Waiting for the players...");
            lastHold = lastPump = SDL_GetTicks();
        } else if (SDL_GetTicks() - lastPump > 100) {
            SDL_PumpEvents();
            lastPump = SDL_GetTicks();
        }
        if (sSeat && sState == PORT_ONLINE_STARTED) {
            seatFlush(); /* (waiting: what it has goes now) */
        }
        SDL_DelayNS(sSeat ? 250000 : 1000000);
    }
}

/* a watcher well behind (joined late, or held): its frames as fast as they go */
int portOnlineCatchingUp(void) {
    return sWatch && sState == PORT_ONLINE_STARTED && sGot.len / PAIR > (size_t)sWatchAt + 120;
}

void portOnlineSendHash(unsigned frame, unsigned long long hash) {
    unsigned char m[12];
    if (sReconnecting || sWatch) {
        return; /* (a fingerprint lost to a reconnect is only not compared) */
    }
    if (sUseIce) {
        portIceSendHash(frame, hash);
        return;
    }
    put32(m, frame);
    put64(m + 4, hash);
    if (portOnlineConnected() && !sendMessage(MSG_HASH, m, sizeof(m))) {
        startReconnect("a send didn't go", 1);
    }
}

int portOnlinePeerHash(unsigned frame, unsigned long long* hash) {
    const int i = (frame / PORT_ONLINE_HASH_EVERY) % 16;
    if (sUseIce) {
        return portIcePeerHash(frame, hash);
    }
    if (sRemoteHash[i].have && sRemoteHash[i].frame == frame) {
        *hash = sRemoteHash[i].hash;
        sRemoteHash[i].have = 0;
        return 1;
    }
    return 0;
}

void portOnlinePump(void) {
    connsPump();
    if (sWatch) {
        if (sState == PORT_ONLINE_STARTED && linkUp()) {
            pumpSocket();
        }
        return;
    }
    if (sState != PORT_ONLINE_STARTED) {
        return;
    }
    if (sReconnecting) {
        reconnectPump();
    } else if (!linkUp()) {
        startReconnect("the connection closed", 1);
    } else if (!iceGone()) {
        pumpSocket();
    }
}

void portOnlineTestDrop(void) {
    if (sState != PORT_ONLINE_STARTED || sWatch) {
        return;
    }
    SDL_Log("online: test: the connection dropped on purpose");
    startReconnect("a test", 1);
}

/* --- recovery (online_pc.c) --- */

void portOnlineSendResync(const void* payload, size_t len) {
    if (sHost) { /* the watchers start again with them */
        unsigned char* m = (unsigned char*)malloc(len + 1);
        m[0] = MSG_W_RESYNC;
        memcpy(m + 1, payload, len);
        watchSendInner(m, len + 1, -1);
        free(m);
    }
    if (sHost && portOnlineConnected() && !sendMessage(MSG_RESYNC, payload, len)) {
        fail("The other player left.");
    }
    flushRelay();
}

void portOnlineAskResync(unsigned epoch) {
    unsigned char m[4];
    put32(m, epoch);
    if (!sHost && portOnlineConnected() && !sendMessage(MSG_RESYNC_ASK, m, sizeof(m))) {
        fail("The other player left.");
    }
    flushRelay();
}

int portOnlineResyncArrived(const void** payload, size_t* len) {
    if (sHost || sResync == NULL) {
        return 0;
    }
    *payload = sResync;
    *len = sResyncLen;
    return 1;
}

int portOnlineResyncAsked(void) {
    const int epoch = sResyncAsked;
    sResyncAsked = -1;
    return epoch;
}

#endif
