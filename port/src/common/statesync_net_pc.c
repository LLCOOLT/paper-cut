/* "Sync what matters" online play's connections (port/include/port/statesync.h).
 *
 * By address, the host listens and up to three guests connect to it (players
 * 2, 3 and 4; each connects again when its connection drops). A guest has one
 * connection, to the host; everything between two guests goes through the
 * host's game (statesync_pc.c forwards it, tagged with the player it came
 * from). Messages are [u32 length][u8 type][payload]. Two kinds:
 * - unreliable (the players' places, the room's NPCs): the latest wins, a lost
 *   one is replaced by the next;
 * - reliable (story flags, the pouch, defeats, the shared battle's input):
 *   numbered, kept until the other side acknowledges them, sent again when the
 *   acknowledgement doesn't come (go-back-N), taken in order exactly once. A
 *   reconnect resends what the other side is missing, so a drop loses nothing.
 * Each connection (a Peer: the host has one per guest) has its own numbering,
 * log, hello and check (the same build, disc and mods, or refused). Every message but the hello goes through
 * $TTYD_NET_FAULTS (drop, dup, reorder, delay, disconnects;
 * port/online_faults.h), which the reliable layer must come through intact;
 * a disconnect_at drops every connection of that game.
 *
 * Each game has a random session id (its hello's nonce). A hello with an id
 * the other side hasn't seen is a new session: both forget the numbering and
 * the game side merges the whole progress again (ssOnSession(player, 1)).
 * The host gives each guest its player number in its hello's answer, and
 * keeps it for that guest's session id: a guest that comes back is the same
 * player. A fifth game is turned away.
 *
 * Or by a room on the relay (docs/ONLINE_RELAY.md), as 0.4's
 * rooms: a code ROOM-SECRET, the relay told only the room's half. Up to four
 * there too: the relay's guest place (player 2) and its two peer places
 * (players 3 and 4, and watchers, which never take the guest's), each with a
 * key of its own with the host (below, "a room's frames"). The
 * relay's WebSocket (online_ws_pc.c) carries frames [u32 length][u8 kind]
 * [data]: the key's two rounds (EC J-PAKE from the secret half,
 * online_seal_pc.c), then only sealed ones (ChaCha20-Poly1305, the stream's
 * count), each holding one of the messages above. The relay counts messages,
 * so the frames go in batches: every 3 game frames (and whenever this game
 * waits for the other). Its
 * "peer" (both in the room) starts the key again, "gone" ends the session
 * until they're back; a dropped connection takes its place again with the
 * relay's ticket. The reliable messages come through all of it as above.
 * $TTYD_SYNC_TEST_RELAY=<address>:<port> (tests, the headless build, which
 * has no WebSocket or sealing): the same rooms on a local stand-in for the
 * relay over plain TCP (local test tools), nothing sealed. */
#undef select /* port_prefix.h's rename for the game (tik_05.c): this is the host's */
#include "port/statesync.h"
#include "port/online.h"
#include "port/online_faults.h"
#include "port/online_link.h"
#include "port/online_sock.h"
#include "port/pm64.h"
#include "port/versions.h"

#ifdef PORT_MODS
#include "port/mods.h"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define SS_MAGIC 0x32595353u /* "SSY2" (SSY1: before the version check, refused as not a sync game) */
#define SS_DEFAULT_PORT 47700 /* (PORT_ONLINE_DEFAULT_PORT: the menu's port setting) */
#define LOG_CAP 8192         /* reliable messages kept until acknowledged */
#define RX_CAP (1 << 20)
#define RESEND_MS 300
#define PING_MS 500
#define MAX_PEERS (SS_PLAYERS - 1) /* the host's guests */
#define LOBBY 4                    /* the host's connections whose hello hasn't come yet */
#define OUT_CAP (8u << 20)   /* one player's bytes waiting to go out at most: past it, the player is dropped */
#define BATCH_MAX (256u << 10) /* a room's frames in one relay message at most (the relay takes up to 1 MB) */
#define FRAME_MAX ((1u << 20) - 4096) /* one frame at most (a relay message, with the relay's wrapping) */
#define RESUME_PER_PUMP 256  /* kept messages sent again per pump after a resume */
#define RESEND_LOW (256u << 10) /* resends (a resume's, go-back-N) only while less than this is on its way */
#define PLACE_GRACE_MS 60000 /* a dropped player's place kept from a new game this long (its reconnect) */
/* The relay's refusals (no one hosting, the room full, the guest's place taken): asked again after 2 s,
   then 4, 8 and so on up to a minute, never every 2 s without end (each try is a request of the relay's
   day, which every player shares); no one hosting for 2 minutes, any other refusal for 10, and no more
   tries until the player's Try again. ($TTYD_SYNC_GIVE_UP=<seconds>, tests: both sooner.) */
#define RETRY_FIRST_MS 2000ull
#define RETRY_MAX_MS 60000ull
#define NO_HOST_GIVE_UP_MS 120000ull
#define REFUSED_GIVE_UP_MS 600000ull

/* Which build plays (port/include/port/build_id.h): the version and its commit (the headless
 * build's too: port/settings/build_id.cpp in both), $TTYD_ONLINE_VERSION another's (tests). Sent for
 * information (the refusal shows it) and to keep the headless test build apart from the windowed one
 * (its " (headless build)"); what decides is the disc and the protocol (port/include/port/statesync.h's
 * PORT_SYNC_PROTOCOL, $TTYD_ONLINE_PROTOCOL another in tests): no program address ever crosses
 * (statesync_battle_pc.c), so two builds of one protocol play together. */
const char* portBuildVersion(void);

typedef struct Hello {
    uint32_t magic;
    uint32_t host;
    uint64_t nonce;
    uint64_t peerNonce; /* the last session this side had with the other (0 none) */
    uint32_t expect;    /* the next reliable message this side wants */
    uint32_t reset;     /* start the numbering again (the sender couldn't resend what we lack) */
    char build[48];     /* portBuildVersion */
    char game[8];       /* the disc's ID, this executable's version (G8ME01) */
    uint32_t player;    /* the host's: the guest's player number (2..4); a guest's: the one it had (0 none) */
    uint32_t full;      /* the host's: no place left (four players already) */
    /* the mods on that change the game (port/include/port/mods.h's portModsOnlineList, as lockstep
       online's hello): every player needs the same. Last: a hello from before it ends here (none) */
    char mods[256];
    /* PORT_SYNC_PROTOCOL. After the mods: a hello from before it ends there (0, older than any) */
    uint32_t proto;
    uint32_t spare; /* (0: the struct's 8-byte alignment, said) */
} Hello;

/* the shortest hello taken (one from before the mods: its build tells it apart) */
#define HELLO_MIN offsetof(Hello, mods)

typedef struct Kept {
    uint32_t seq;
    uint32_t len; /* inner type + payload */
    uint8_t* data;
} Kept;

/* one other game: the host's guest, or a guest's host */
typedef struct Peer {
    int used;   /* a player known (its place kept while it's away) */
    int player; /* its player number: 1 the host, 2..4 the guests */
    Socket conn;
    int helloIn; /* its hello came on this connection */
    int writeFailed;
    unsigned long long lastRx, lastPing, lastResend, connectedAt, ackAt, lostAt;
    uint64_t peerNonce; /* its session id */
    /* reliable, out */
    uint32_t txNext, txBase; /* the next seq to give; the oldest not acknowledged */
    Kept log[LOG_CAP];
    /* reliable, in */
    uint32_t rxExpect, ackSent;
    uint8_t* rx;
    size_t rxLen;
    /* counts, for the log */
    unsigned sent, got, resent, dupes, gaps, reconnects;
    int rtt; /* the last ping's round trip (ms), -1 none yet (the Multiplayer page's players) */
    /* a room's: this pair's key (its own: the host agrees one with each), the frames for its next
       relay message */
    int keyed, pakeSent;
    uint8_t* out;
    size_t outLen, outCap;
    int refusedLink; /* a room's host: this link's game was removed (no new key for it until the relay says it's back) */
    int forgedSeen;  /* a room's host: a frame of the relay's kinds found inside this player's message (logged once) */
    /* by address: what the socket hasn't taken yet (sent as it takes it, each pump: never waited for) */
    uint8_t* q;
    size_t qHead, qLen, qCap; /* (the bytes waiting are q[qHead..qLen): sent ones skipped, compacted now and then) */
    /* the next kept message to send for the first time, or again after a resume (a few each pump) */
    uint32_t resendNext;
} Peer;

/* a connection to the host whose hello hasn't come yet (it says who it is) */
typedef struct Lobby {
    Socket s;
    uint8_t buf[1024];
    size_t len;
    unsigned long long at;
} Lobby;

static struct {
    int on, host, port;
    int me; /* this game's player number (1 the host; a guest's from the host's hello, 0 before) */
    char address[128];
    unsigned startAt;
    Socket listen;
    Socket pending; /* the guest's connect under way (connectPoll) */
    unsigned long long pendingAt;
    unsigned long long lastTry;
    uint64_t nonce;
    Peer peer[MAX_PEERS];
    Lobby lobby[LOBBY];
    unsigned frame;
    char summary[320];
    char refused[256]; /* the session refused (another protocol or disc, no place): why, English; "" none */
    char refusedBuild[49]; /* refused for another protocol: the other game's build (else "") */
    /* a room on the relay (its one other player: peer[0]) */
    int room;
    char code[PORT_ONLINE_CODE_LENGTH + 1]; /* the room's half, then the secret one */
    int testRelay;                            /* $TTYD_SYNC_TEST_RELAY: the local stand-in, unsealed */
    int wsOpen, wsTried;
    unsigned long long wsTry;
    char ticket[48];
    char relayNote[160]; /* the relay's last word (a refusal: "No one is hosting that room") */
    unsigned long long retryWait;    /* the relay's refusals: the wait before asking again (2 s, doubling to 60) */
    unsigned long long refusedSince; /* the relay's refusals in a row: since when (0 none; in the room again) */
    int asPeer;          /* this joining game's place: a peer's (players 3 and 4, watchers), not the guest's */
    int triedGuest;      /* a watcher on a relay without peers: the guest's place tried instead */
    int everIn;          /* this session has been in the relay's room (the browser's place turns) */
    int noHello;         /* a guest's connections in a row closed before any hello came */
    uint8_t* prx;        /* what the relay sent, not yet taken */
    size_t prxLen;
    unsigned long long pipeRx, listedAt; /* the relay's last word; a listed room's last "list" */
    uint8_t* send;       /* a host's message to a peer: TO_PEER three times, its place, the frames */
    size_t sendCap;
    uint8_t* opened;
    size_t openedCap;
    char failed[160]; /* can't go on (no relay in this build, a bad code): English */
    /* the relay not reached for a while (PORT_SYNC_NO_SERVICE): since the first try after the session
       began or the relay's connection last went (a quiet drop, a close, faults), never the session's start */
    unsigned long long beganAt;
    int triesSince; /* the tries since then (the first sets beganAt: $TTYD_SYNC_AT's later start too) */
    uint64_t banned[8];         /* the host: session ids it removed (ssNetRemove) */
    int bannedN;
    unsigned long long refusedAt; /* the host: when it last turned a game away (its note shown a while) */
    int framed;                   /* a room's host: the relay wraps the guest's messages too (FROM_GUEST) */
    int oldRelay; /* a room's host: the relay has players 3 and 4 but can't frame (it needs an update) */
} S;

/* a room's host on a relay that has players 3 and 4 but no framing (deployed before the clients): the
   two-player game goes on, players 3 and 4 are told why they can't play (R_OLD_RELAY) and so is the host */
#define OLD_RELAY_NOTE "This relay needs an update before players 3 and 4 can join: two can still play."
#define OLD_RELAY_PEER_NOTE "The host's relay needs an update before players 3 and 4 can join."

/* a guest joining the same room or address again keeps its session id (its seat and player number come
   back: the host knows it), across a Leave too; a new place another time */
static struct {
    char where[128];
    uint64_t nonce;
} sRejoin;

#define RP (&S.peer[0]) /* a room's other player */

/* ---- the relay's connection: its WebSocket, or the tests' stand-in ---- */

/* R_OLD_RELAY: a host to player 3 or 4 that its relay can't take them (unsealed, no key yet: it only
   ends that game's try with a note, which a relay could do anyway by closing) */
enum { R_PAKE1 = 1, R_PAKE2 = 2, R_SEALED = 3, R_OLD_RELAY = 4 };

typedef struct Pipe {
    int (*open)(const char* url);
    int (*state)(void);
    const char* (*error)(void);
    void (*send)(const void* data, size_t len);
    void (*sendText)(const char* text);
    size_t (*read)(void* out, size_t cap);
    int (*text)(char* out, size_t cap);
    void (*close)(void);
    size_t (*backlog)(void); /* the bytes given to send that haven't gone yet */
} Pipe;

static const Pipe kWs = {portWsOpen, portWsState, portWsError, portWsSend, portWsSendText, portWsRead, portWsText,
                         portWsClose, portWsBacklog};

static void sleepMs(int ms);

/* The stand-in (tests): TCP to $TTYD_SYNC_TEST_RELAY, frames [u32 length][u8 kind][data]: kind 0
 * a binary message, 1 a text (this side's first: "OPEN <path>"; the relay's: "open" when in the
 * room, then you/ticket/peer/gone), 2 a refusal (its words) and the end, 3 the room list's text
 * (the answer to "OPEN /rooms?..."). */
static struct {
    Socket s;
    int state;
    char error[128];
    uint8_t* raw;
    size_t rawLen;
    uint8_t* bin;
    size_t binLen;
    char texts[16][96];
    int textHead, textCount;
    char list[2048];
    size_t listLen;
} T = {BAD_SOCKET};

static void tCloseWith(const char* why);

/* the 5-byte head and the body through one loop: a frame goes whole or the
 * stand-in connection closes (half a frame would garble the rest of the run) */
static void tFrame(int kind, const void* data, size_t len) {
    uint8_t head[5];
    const uint32_t n = (uint32_t)(len + 1);
    size_t sent = 0;
    const size_t total = len + 5;
    const unsigned long long start = ssNetMs();
    memcpy(head, &n, 4);
    head[4] = (uint8_t)kind;
    if (T.s == BAD_SOCKET) {
        return;
    }
    while (sent < total) {
        const char* p = sent < 5 ? (const char*)head + sent : (const char*)data + (sent - 5);
        const size_t left = sent < 5 ? 5 - sent : total - sent;
        const int k = (int)send(T.s, p, (int)left, SEND_FLAGS);
        if (k > 0) {
            sent += (size_t)k;
        } else if (k < 0 && WOULD_BLOCK() && ssNetMs() - start < 2000) {
            sleepMs(1); /* (the tests' stand-in on this computer: a moment, never forever) */
        } else {
            tCloseWith("The test relay's connection failed while sending.");
            return;
        }
    }
}

static void tCloseWith(const char* why) {
    if (T.s != BAD_SOCKET) {
        closeSocket(T.s);
        T.s = BAD_SOCKET;
    }
    snprintf(T.error, sizeof(T.error), "%s", why);
    T.state = PORT_WS_CLOSED;
    T.rawLen = T.binLen = 0;
}

static void tPump(void) {
    size_t at = 0;
    int closed = 0;
    if (T.s == BAD_SOCKET) {
        return;
    }
    for (;;) {
        int k;
        if (T.rawLen + 65536 > RX_CAP) {
            break;
        }
        k = (int)recv(T.s, (char*)T.raw + T.rawLen, 65536, 0);
        if (k > 0) {
            T.rawLen += (size_t)k;
            continue;
        }
        if (k == 0 || !WOULD_BLOCK()) {
            closed = 1; /* (what came before it first: a refusal's words come just before the end) */
        }
        break;
    }
    while (T.rawLen - at >= 5) {
        uint32_t n;
        int kind;
        memcpy(&n, T.raw + at, 4);
        if (n == 0 || T.rawLen - at - 4 < n) {
            break;
        }
        kind = T.raw[at + 4];
        if (kind == 0) {
            if (T.binLen + n - 1 > RX_CAP) {
                break; /* (not read yet: the rest waits for the next pump) */
            }
            memcpy(T.bin + T.binLen, T.raw + at + 5, n - 1);
            T.binLen += n - 1;
        } else if (kind == 1) {
            char text[96];
            snprintf(text, sizeof(text), "%.*s", (int)(n - 1), (const char*)T.raw + at + 5);
            if (!strcmp(text, "open")) {
                T.state = PORT_WS_OPEN;
            } else if (T.textCount < 16) {
                snprintf(T.texts[(T.textHead + T.textCount++) % 16], sizeof(T.texts[0]), "%s", text);
            }
        } else if (kind == 3) {
            T.listLen = n - 1 < sizeof(T.list) - 1 ? n - 1 : sizeof(T.list) - 1;
            memcpy(T.list, T.raw + at + 5, T.listLen);
            T.list[T.listLen] = '\0';
        } else {
            char why[128];
            snprintf(why, sizeof(why), "%.*s", (int)(n - 1), (const char*)T.raw + at + 5);
            tCloseWith(why);
            return;
        }
        at += 4 + n;
    }
    memmove(T.raw, T.raw + at, T.rawLen - at);
    T.rawLen -= at;
    if (closed) {
        tCloseWith("The connection to the relay was lost.");
    }
}

static int tOpenPath(const char* path) {
    const char* where = getenv("TTYD_SYNC_TEST_RELAY");
    char host[128], port[16], line[256];
    const char* colon;
    struct addrinfo hints, *res = NULL;
    if (T.raw == NULL) {
        T.raw = (uint8_t*)malloc(RX_CAP);
        T.bin = (uint8_t*)malloc(RX_CAP);
    }
    tCloseWith("");
    if (T.raw == NULL || T.bin == NULL) {
        tCloseWith("no memory for the test relay");
        return 0;
    }
    colon = where != NULL ? strrchr(where, ':') : NULL;
    if (path == NULL || colon == NULL) {
        tCloseWith("bad test relay");
        return 0;
    }
    snprintf(host, sizeof(host), "%.*s", (int)(colon - where), where);
    snprintf(port, sizeof(port), "%s", colon + 1);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        tCloseWith("The relay couldn't be reached.");
        return 0;
    }
    T.s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (T.s == BAD_SOCKET || connect(T.s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        tCloseWith("The relay couldn't be reached.");
        return 0;
    }
    freeaddrinfo(res);
    onlineNonBlocking(T.s, 1);
    T.state = PORT_WS_OPENING;
    T.error[0] = '\0';
    snprintf(line, sizeof(line), "OPEN %s", path);
    tFrame(1, line, strlen(line));
    return 1;
}

static int tOpen(const char* url) {
    return tOpenPath(strstr(url, "/room/"));
}

static int tState(void) {
    tPump();
    return T.state;
}

static const char* tError(void) {
    return T.error;
}

static void tSend(const void* data, size_t len) {
    tFrame(0, data, len);
}

static void tSendText(const char* text) {
    tFrame(1, text, strlen(text));
}

static size_t tRead(void* out, size_t cap) {
    size_t n;
    tPump();
    n = T.binLen < cap ? T.binLen : cap;
    memcpy(out, T.bin, n);
    memmove(T.bin, T.bin + n, T.binLen - n);
    T.binLen -= n;
    return n;
}

static int tText(char* out, size_t cap) {
    tPump();
    if (T.textCount == 0) {
        return 0;
    }
    snprintf(out, cap, "%s", T.texts[T.textHead]);
    T.textHead = (T.textHead + 1) % 16;
    T.textCount--;
    return 1;
}

/* ---- the last reads of the connections this game is done with ----
 * Sending is closed first (the goodbye already out), then what still comes is read and let go until
 * the other side closes too: closed with unread data, a connection is reset, and a reset loses what
 * the other side hadn't read yet (Windows drops it), the host's goodbye among it, so its guests saw a
 * drop and waited 10 seconds. The other side closes once it has read the goodbye, a frame or so but
 * maybe longer than a leave should take, so the reading goes on off the game's thread (a few seconds
 * at most); without threads, here for a moment, as before. */

#define LINGER_MAX (MAX_PEERS + 1)
#define LINGER_MS 4000

typedef struct {
    Socket s[LINGER_MAX];
    int n;
    int ms;
} Linger;

static void lingerRun(Linger* L) {
    const unsigned long long until = ssNetMs() + (unsigned long long)L->ms;
    int open = L->n, i;
    while (open > 0 && ssNetMs() < until) {
        char sink[4096];
        open = 0;
        for (i = 0; i < L->n; i++) {
            if (L->s[i] != BAD_SOCKET) {
                const int k = (int)recv(L->s[i], sink, (int)sizeof(sink), 0);
                if (k == 0 || (k < 0 && !WOULD_BLOCK())) {
                    closeSocket(L->s[i]); /* (the other side closed: nothing unread, no reset) */
                    L->s[i] = BAD_SOCKET;
                } else {
                    open = 1;
                }
            }
        }
        if (open) {
            sleepMs(5);
        }
    }
    for (i = 0; i < L->n; i++) {
        if (L->s[i] != BAD_SOCKET) {
            closeSocket(L->s[i]);
        }
    }
}

#if defined(_WIN32)
static DWORD WINAPI lingerThread(LPVOID arg) {
    lingerRun((Linger*)arg);
    free(arg);
    return 0;
}
#elif !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#include <pthread.h>
static void* lingerThread(void* arg) {
    lingerRun((Linger*)arg);
    free(arg);
    return NULL;
}
#endif

/* sending closed on each (their last words already out), then closed once the other side has closed */
static void lingerClose(const Socket* s, int n, int inPlaceMs) {
    Linger* L = (Linger*)calloc(1, sizeof(Linger));
    int i, started = 0;
    if (L == NULL) {
        for (i = 0; i < n; i++) {
            closeSocket(s[i]);
        }
        return;
    }
    for (i = 0; i < n && L->n < LINGER_MAX; i++) {
        shutdown(s[i], 1 /* SD_SEND, SHUT_WR */);
        L->s[L->n++] = s[i];
    }
    for (; i < n; i++) {
        closeSocket(s[i]);
    }
    L->ms = LINGER_MS;
#if defined(_WIN32)
    {
        HANDLE t = CreateThread(NULL, 0, lingerThread, L, 0, NULL);
        if (t != NULL) {
            CloseHandle(t);
            started = 1;
        }
    }
#elif !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    {
        pthread_t t;
        if (pthread_create(&t, NULL, lingerThread, L) == 0) {
            pthread_detach(t);
            started = 1;
        }
    }
#endif
    if (!started) {
        L->ms = inPlaceMs; /* (no thread: here, a moment at most) */
        lingerRun(L);
        free(L);
    }
}

static void tClose(void) {
    /* (closed here, as the real one's portWsClose leaves it: idle, and no word of the relay's, so a
       connection this game gave up on is asked for again as one that can't be reached, never as a
       refusal; "closed" here made the tests back off as from a refusal and miss the no-answer timing) */
    if (T.s != BAD_SOCKET) {
        /* the last words out first, then closed once the relay has closed too (lingerClose) */
        const Socket s = T.s;
        T.s = BAD_SOCKET;
        lingerClose(&s, 1, 150);
    }
    tCloseWith("");
    T.state = PORT_WS_IDLE;
}

static size_t tBacklog(void) {
    return 0; /* (each frame goes whole before tFrame returns) */
}

static const Pipe kTest = {tOpen, tState, tError, tSend, tSendText, tRead, tText, tClose, tBacklog};

static const Pipe* pipe_(void) {
    return S.testRelay ? &kTest : &kWs;
}

static const char* buildName(void) {
    const char* env = getenv("TTYD_ONLINE_VERSION");
    return env != NULL && *env ? env : portBuildVersion();
}

/* the protocol this game plays (PORT_SYNC_PROTOCOL; $TTYD_ONLINE_PROTOCOL another, tests) */
static uint32_t protoOf(void) {
    const char* env = getenv("TTYD_ONLINE_PROTOCOL");
    return env != NULL && *env ? (uint32_t)strtoul(env, NULL, 10) : (uint32_t)PORT_SYNC_PROTOCOL;
}

/* the headless test build (its version says so: port/settings/build_id.cpp) */
static int headlessBuild(const char* build) {
    return strstr(build, "(headless build)") != NULL;
}

/* the mods every player needs the same of ("" none; the headless build has none); $TTYD_SYNC_MODS
   another list (tests) */
static const char* modsName(void) {
    static char sList[256];
    const char* env = getenv("TTYD_SYNC_MODS");
    const char* mods = "";
    const char* pm64 = portPm64OnlineToken(); /* the Paper Mario 64 partners: every player or none */
#ifdef PORT_MODS
    mods = portModsOnlineList();
#endif
    if (env != NULL) {
        mods = env;
    }
    snprintf(sList, sizeof(sList), "%s%s%s", mods, *mods && *pm64 ? "," : "", pm64);
    return sList;
}

/* a mods list without the Paper Mario 64 partners' token ("pm64", last), into out */
static void modsWithoutPm64(const char* list, char* out, size_t cap) {
    size_t n;
    snprintf(out, cap, "%s", list);
    n = strlen(out);
    if (n >= 4 && !strcmp(out + n - 4, "pm64") && (n == 4 || out[n - 5] == ',')) {
        out[n == 4 ? 0 : n - 5] = 0;
    }
}

unsigned long long ssNetMs(void) {
#ifdef _WIN32
    static LARGE_INTEGER sFreq;
    LARGE_INTEGER now;
    if (sFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&sFreq);
    }
    QueryPerformanceCounter(&now);
    return (unsigned long long)((double)now.QuadPart * 1000.0 / (double)sFreq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
#endif
}

static void sleepMs(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#elif defined(__EMSCRIPTEN__)
    emscripten_sleep((unsigned)ms); /* the browser: its WebSocket's messages come in meanwhile (one thread) */
#else
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

void ssLog(const char* fmt, ...) {
    va_list ap;
    printf("[sync] f%u ", S.frame);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

/* the way to that game is up (its socket, or the room's key agreed) */
static int linkUp(const Peer* P) {
    return S.room ? S.wsOpen && P->keyed : P->conn != BAD_SOCKET;
}

static int peerUp(const Peer* P) {
    return P->used && linkUp(P) && P->helloIn;
}

/* the connection to a player: the host's to its guests (2..4), a guest's to the host (1) */
static Peer* peerOf(int player) {
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        if (S.peer[i].used && S.peer[i].player == player) {
            return &S.peer[i];
        }
    }
    return NULL;
}

int ssNetConnected(void) {
    int i;
    if (!S.on) {
        return 0;
    }
    for (i = 0; i < MAX_PEERS; i++) {
        if (peerUp(&S.peer[i])) {
            return 1;
        }
    }
    return 0;
}

int ssNetLinked(int player) {
    const Peer* P = S.on ? peerOf(player) : NULL;
    return P != NULL && peerUp(P);
}

int ssNetMe(void) {
    return S.host ? 1 : S.me;
}

int ssNetIsHost(void) {
    return S.host;
}

/* ---- a room's frames: the key, then everything sealed ----
 * A room's host has a pair of keys with each of its players: the guest's (link 0, the relay's
 * stream as it is) and players 3 and 4's (links 1 and 2: the relay's "peers", their messages to the
 * host wrapped as FROM_PEER with their place, the host's to them sent with TO_PEER three times and
 * the place in front). A joining game has only the host (link 0), whichever place it took. */

#define FROM_PEER 0xF1
#define FROM_GUEST 0xF2 /* the guest's message, framed by the relay (a host that asked: &framed=1) */
#define TO_PEER 0xFE
#define RELAY_PEERS 2 /* the relay's peers' places: players 3 and 4 */

static int linkOf(const Peer* P) {
    return (int)(P - S.peer);
}

/* room for a frame of len bytes in that player's batch (1), or 0: past what the relay carries in one
   message, a backlog this big for one player, or no memory. Asked before a frame is sealed: a sealed
   frame that never goes would put the stream's count out of step (every later one fails to open) */
static int outRoom(Peer* P, size_t len) {
    if (len + 5 > FRAME_MAX || P->outLen + len + 5 > OUT_CAP) {
        ssLog("net: a frame of %u bytes for player %d not sent (%u waiting)", (unsigned)len, P->player,
              (unsigned)P->outLen);
        return 0;
    }
    if (P->outLen + len + 5 > P->outCap) {
        const size_t cap = (P->outLen + len + 5) * 2 + 4096;
        uint8_t* grown = (uint8_t*)realloc(P->out, cap);
        if (grown == NULL) {
            return 0;
        }
        P->out = grown;
        P->outCap = cap;
    }
    return 1;
}

static void outFrame(Peer* P, int kind, const void* data, size_t len) {
    const uint32_t n = (uint32_t)(len + 1);
    if (!outRoom(P, len)) {
        return; /* (lost, as a dropped message: only the key's unsealed rounds come here unchecked) */
    }
    memcpy(P->out + P->outLen, &n, 4);
    P->out[P->outLen + 4] = (uint8_t)kind;
    if (len > 0) {
        memcpy(P->out + P->outLen + 5, data, len);
    }
    P->outLen += len + 5;
}

/* when bytes last went onto the wire (a socket by address, the relay's connection in a room): the delay
   log's "out" (port/statesync.h ssNetOutMs) */
static unsigned long long sOutMs;

unsigned long long ssNetOutMs(void) {
    return sOutMs;
}

/* one relay message of frames to one player (n bytes of whole frames) */
static void sendBatch(int i, const uint8_t* frames, size_t n) {
    if (S.host && i > 0) {
        if (S.sendCap < n + 4) {
            const size_t cap = n * 2 + 4096;
            uint8_t* grown = (uint8_t*)realloc(S.send, cap);
            if (grown == NULL) {
                /* (no memory: these sealed frames lost, so the link's stream is out of step: its key
                   again, at once, rather than every later frame failing to open) */
                S.peer[i].writeFailed = 1;
                return;
            }
            S.send = grown;
            S.sendCap = cap;
        }
        S.send[0] = S.send[1] = S.send[2] = TO_PEER;
        S.send[3] = (uint8_t)(i - 1);
        memcpy(S.send + 4, frames, n);
        pipe_()->send(S.send, n + 4);
    } else {
        pipe_()->send(frames, n);
    }
    sOutMs = ssNetMs();
}

/* the frames so far, as the relay's messages for each player: whole frames, BATCH_MAX bytes at most a
   message (the relay takes up to 1 MB: a resume's backlog of snapshots and save images goes in several) */
static void flushOut(void) {
    int i;
    if (!S.room) {
        return;
    }
    for (i = 0; i < MAX_PEERS; i++) {
        Peer* P = &S.peer[i];
        if (P->outLen == 0) {
            continue;
        }
        if (S.wsOpen && pipe_()->state() == PORT_WS_OPEN) {
            size_t at = 0;
            while (at < P->outLen) {
                size_t end = at;
                while (end < P->outLen) {
                    uint32_t n;
                    memcpy(&n, P->out + end, 4);
                    if (end > at && end + 4 + n - at > BATCH_MAX) {
                        break; /* (the next message starts with this frame) */
                    }
                    end += 4 + n;
                }
                sendBatch(i, P->out + at, end - at);
                at = end;
            }
        }
        P->outLen = 0;
    }
}

/* the key's rounds and the seal: Mbed TLS's (online_seal_pc.c); the tests' stand-in passes them
   through as they are (the headless build has no sealing) */
static int pakeBegin(Peer* P, uint8_t* out, size_t cap, size_t* n) {
    if (S.testRelay) {
        memcpy(out, "pake1", 5); // NOLINT(bugprone-not-null-terminated-result): a 5-byte token, its length sent
        *n = 5;
        return 1;
    }
    return portPakeBeginAt(linkOf(P), S.host, S.code + PORT_ONLINE_ROOM_HALF, out, cap, n);
}

static int pakeOne(Peer* P, const uint8_t* in, size_t len, uint8_t* out, size_t cap, size_t* n) {
    if (S.testRelay) {
        memcpy(out, "pake2", 5); // NOLINT(bugprone-not-null-terminated-result): a 5-byte token, its length sent
        *n = 5;
        return len == 5 && !memcmp(in, "pake1", 5);
    }
    return portPakeRoundOneAt(linkOf(P), in, len, out, cap, n);
}

static int pakeTwo(Peer* P, const uint8_t* in, size_t len) {
    if (S.testRelay) {
        return len == 5 && !memcmp(in, "pake2", 5);
    }
    return portPakeRoundTwoAt(linkOf(P), in, len);
}

static void keyReset(Peer* P) {
    if (!S.testRelay) {
        portSealResetAt(linkOf(P));
    }
    P->keyed = P->pakeSent = 0;
}

/* this side's first round (once a session) */
static void startPake(Peer* P) {
    uint8_t m[512];
    size_t n = 0;
    if (P->pakeSent) {
        return;
    }
    P->pakeSent = 1;
    if (!pakeBegin(P, m, sizeof(m), &n)) {
        snprintf(S.failed, sizeof(S.failed), "A secure connection couldn't be set up.");
        ssLog("net: the room's key couldn't start");
        return;
    }
    outFrame(P, R_PAKE1, m, n);
    flushOut();
}

/* one whole message sealed into the next relay message */
static int sealOut(Peer* P, const void* data, size_t len) {
    uint8_t* sealed;
    size_t n;
    if (!P->keyed) {
        return 0;
    }
    /* (checked before the seal counts it: one that can't go fails here, and the put drops the link) */
    if (!outRoom(P, len + (S.testRelay ? 0 : PORT_SEAL_TAG))) {
        return 0;
    }
    if (S.testRelay) {
        outFrame(P, R_SEALED, data, len);
        return 1;
    }
    sealed = (uint8_t*)malloc(len + PORT_SEAL_TAG);
    if (sealed == NULL) {
        return 0;
    }
    n = portSealAt(linkOf(P), PORT_SEAL_STREAM, (const unsigned char*)data, len, sealed);
    outFrame(P, R_SEALED, sealed, n);
    free(sealed);
    return 1;
}

/* ---- the sockets ---- */

/* a room: one player's link lost (its key again once it's back: the relay says so) */
static void linkLost(Peer* P, const char* why) {
    if (P->helloIn) {
        ssLog("net: connection %s (%s)", S.host ? "to a player lost" : "lost", why);
        P->reconnects++;
    }
    keyReset(P);
    P->helloIn = 0;
    P->outLen = 0;
}

/* a room: the relay's connection gone, however it went (closed by it, a read failure, or given up on
   here when it went quiet: pipeDrop): a fresh window to reach it again, measured from now, the tries
   backing off as when it was first asked, and only 15 s of them unanswered is "can't reach" (from the
   session's start instead, a drop late in a session was said at once, before any try) */
static void relayGone(unsigned long long now) {
    S.beganAt = now;
    S.triesSince = 0;}

/* a room: the relay's connection closed (and opened again: its ticket keeps this game's place) */
static void pipeDrop(void) {
    int i;
    if (S.wsOpen) {
        pipe_()->close();
        S.wsOpen = 0;
        S.wsTry = ssNetMs();
        relayGone(S.wsTry);
    }
    for (i = 0; i < MAX_PEERS; i++) {
        linkLost(&S.peer[i], "the relay's connection closed");
    }
    S.prxLen = 0;
    portNetFaultsReset();
}

static void dropConn(Peer* P, const char* why) {
    if (S.room) {
        if (S.host) {
            /* one player's: its key again at once (theirs starts over when our first round comes);
               the relay's connection only when nothing at all has come from it for a while */
            linkLost(P, why);
            portNetFaultsReset();
            if (S.wsOpen && ssNetMs() - S.pipeRx > (unsigned long long)portNetFaultsQuietMs(5000)) {
                ssLog("net: nothing from the relay: its connection again");
                pipeDrop();
            } else if (S.wsOpen && P->used && !P->refusedLink) {
                startPake(P); /* (a removed player's: not, until the relay says it's back) */
            }
            return;
        }
        if (P->helloIn) {
            ssLog("net: connection lost (%s)", why);
            P->reconnects++;
            P->helloIn = 0;
        }
        pipeDrop();
        return;
    }
    if (P->conn != BAD_SOCKET) {
        closeSocket(P->conn);
        P->conn = BAD_SOCKET;
        if (P->helloIn) {
            ssLog("net: connection to player %d lost (%s)", P->player, why);
            P->reconnects++;
            S.noHello = 0;
        } else if (!S.host && ++S.noHello >= 3 && S.refused[0] == '\0') {
            /* a guest: the game there answers no hello of this kind, time after time (0.4's online play,
               which closes on this game's; another program): refused, not tried forever */
            snprintf(S.refused, sizeof(S.refused), "%s",
                     "The game there isn't this version's online play (an older Paper Cut, or another program). "
                     "Both need the same build.");
            ssLog("net: refused: %s", S.refused);
        }
    }
    P->helloIn = 0;
    P->writeFailed = 0;
    P->rxLen = 0;
    P->qHead = P->qLen = 0; /* (what was waiting to go: the reliable ones come again with the resume) */
    P->lostAt = ssNetMs();
    portNetFaultsReset(); /* (every connection's held messages: the reliable ones come again) */
}

/* as much as the socket takes now (never waited for): the bytes taken, -1 on a failure */
static long sendSome(Socket s, const uint8_t* p, size_t len) {
    size_t at = 0;
    while (at < len) {
        const size_t chunk = len - at < (1u << 20) ? len - at : (1u << 20);
        const int n = (int)send(s, (const char*)p + at, (int)chunk, SEND_FLAGS);
        if (n > 0) {
            at += (size_t)n;
            sOutMs = ssNetMs();
            continue;
        }
        if (n < 0 && WOULD_BLOCK()) {
            break; /* (full: the rest later) */
        }
        return -1;
    }
    return (long)at;
}

/* by address: what's waiting for that player's socket, as much as it takes now; 0 on a failure */
static size_t qWaiting(const Peer* P) {
    return P->qLen - P->qHead;
}

/* the bytes to that player still on their way out: by address its queue; in a room its batch so far
   plus what the relay's connection hasn't written yet (every player's: one connection) */
static size_t peerBacklog(const Peer* P) {
    return S.room ? P->outLen + pipe_()->backlog() : qWaiting(P);
}

static int flushQueue(Peer* P) {
    long n;
    if (qWaiting(P) == 0 || P->conn == BAD_SOCKET) {
        return 1;
    }
    n = sendSome(P->conn, P->q + P->qHead, qWaiting(P));
    if (n < 0) {
        return 0;
    }
    /* (the sent bytes skipped, never the whole queue copied on each message while the socket's full) */
    P->qHead += (size_t)n;
    if (P->qHead == P->qLen) {
        P->qHead = P->qLen = 0;
    }
    return 1;
}

/* by address: the bytes out to that player, the socket's now and the rest queued (sent each pump, so a
   player whose game stops reading never holds up this one or the others); 0 on a failure, or too much
   waiting (OUT_CAP: that player is dropped) */
static int sendQueued(Peer* P, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    if (!flushQueue(P)) {
        return 0;
    }
    if (qWaiting(P) == 0) {
        const long n = sendSome(P->conn, p, len);
        if (n < 0) {
            return 0;
        }
        p += n;
        len -= (size_t)n;
    }
    if (len == 0) {
        return 1;
    }
    if (qWaiting(P) + len > OUT_CAP) {
        ssLog("net: player %d's game takes nothing (%u bytes waiting): dropped", P->player, (unsigned)qWaiting(P));
        return 0;
    }
    if (P->qHead > 0 && (P->qLen + len > P->qCap || P->qHead > P->qLen / 2)) {
        /* (compacted only when the sent part is most of it, or room's needed: each byte moves about once) */
        memmove(P->q, P->q + P->qHead, qWaiting(P));
        P->qLen -= P->qHead;
        P->qHead = 0;
    }
    if (P->qLen + len > P->qCap) {
        const size_t cap = (P->qLen + len) * 2 + 65536;
        uint8_t* grown = (uint8_t*)realloc(P->q, cap);
        if (grown == NULL) {
            return 0;
        }
        P->q = grown;
        P->qCap = cap;
    }
    memcpy(P->q + P->qLen, p, len);
    P->qLen += len;
    return 1;
}

/* the whole buffer on its way (sealed into the room's batch, or onto the socket and its queue); 0 on a
   failure */
static int writeAll(Peer* P, const void* data, size_t len) {
    if (S.room) {
        return sealOut(P, data, len);
    }
    if (P->conn == BAD_SOCKET) {
        return 0;
    }
    return sendQueued(P, data, len);
}

/* the faults' messages carry their connection's index first */
static int faultsWrite(const void* data, size_t len) {
    const uint8_t* m = (const uint8_t*)data;
    Peer* P;
    if (len < 1 || m[0] >= MAX_PEERS) {
        return 1;
    }
    P = &S.peer[m[0]];
    if (!P->used || !linkUp(P)) {
        return 1; /* (gone meanwhile: lost with it) */
    }
    if (!writeAll(P, m + 1, len - 1)) {
        P->writeFailed = 1; /* (dropped after the faults' pass, never during it) */
    }
    return 1;
}

static void dropFailed(void) {
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        if (S.peer[i].writeFailed) {
            S.peer[i].writeFailed = 0;
            dropConn(&S.peer[i], "write failed");
        }
    }
}

/* $TTYD_WIRE_CHECK=1 (tests): every message out is searched for what looks like an address of this
 * program (an 8-byte word inside its image: its code and its data, where the game's tables are; the heap's
 * addresses are too like a run of small numbers to tell apart): the games playing
 * together are other programs (other platforms, other builds), so nothing that came from a pointer may
 * cross. Each find is logged "WIRE: ..." (the test pass fails on it). Windows only (the test machine's). */
#ifdef _WIN32
extern char __ImageBase;
#endif

static void wireCheck(int type, const uint8_t* p, size_t n) {
    static int sOn = -1;
    static uintptr_t sLo[2], sHi[2];
    size_t i;
    if (sOn < 0) {
        const char* v = getenv("TTYD_WIRE_CHECK");
        sOn = v != NULL && *v != '\0' && *v != '0';
#ifdef _WIN32
        if (sOn) {
            const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)&__ImageBase;
            const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const char*)dos + dos->e_lfanew);
            sLo[0] = (uintptr_t)&__ImageBase;
            sHi[0] = sLo[0] + nt->OptionalHeader.SizeOfImage;
        }
#endif
    }
    if (!sOn) {
        return;
    }
    for (i = 0; i + 8 <= n; i += 4) {
        uint64_t w;
        int r;
        memcpy(&w, p + i, 8);
        for (r = 0; r < 2; r++) {
            if (sHi[r] > sLo[r] && w >= sLo[r] && w < sHi[r]) {
                ssLog("WIRE: message type %d (%u bytes) carries an address of this program at byte %u", type,
                      (unsigned)n, (unsigned)i);
                return;
            }
        }
    }
}

/* one message out to one game: through the faults (but the hello) */
static void put(Peer* P, int type, const void* p, size_t n, int faults) {
    uint8_t buf[6 + 65536];
    uint8_t* m = buf;
    const uint32_t len = (uint32_t)(n + 1);
    if (!linkUp(P)) {
        return;
    }
    if (n > 0) {
        wireCheck(type, (const uint8_t*)p, n);
    }
    if (n + 6 > sizeof(buf)) {
        m = (uint8_t*)malloc(n + 6);
        if (m == NULL) {
            return;
        }
    }
    m[0] = (uint8_t)(P - S.peer);
    memcpy(m + 1, &len, 4);
    m[5] = (uint8_t)type;
    if (n > 0) {
        memcpy(m + 6, p, n);
    }
    if (!faults || !portNetFaultsSend(m, n + 6, 1, ssNetMs(), faultsWrite)) {
        if (!writeAll(P, m + 1, n + 5)) {
            P->writeFailed = 1;
        }
    }
    if (m != buf) {
        free(m);
    }
    P->sent++;
    dropFailed();
}

static void fillHello(Hello* h, const Peer* P, int reset, int full) {
    memset(h, 0, sizeof(*h));
    h->magic = SS_MAGIC;
    h->host = (uint32_t)S.host;
    h->nonce = S.nonce;
    h->peerNonce = P != NULL ? P->peerNonce : 0;
    h->expect = P != NULL ? P->rxExpect : 0;
    h->reset = (uint32_t)reset;
    h->player = (uint32_t)(S.host ? (P != NULL ? P->player : 0) : S.me);
    h->full = (uint32_t)full;
    snprintf(h->build, sizeof(h->build), "%s", buildName());
    snprintf(h->game, sizeof(h->game), "%.6s", portVersionSelf()->id);
    snprintf(h->mods, sizeof(h->mods), "%s", modsName());
    h->proto = protoOf();
}

/* a hello as it came (n bytes): one from before the mods' list has none; 0 too short */
static int readHello(Hello* h, const uint8_t* p, size_t n) {
    if (n < HELLO_MIN) {
        return 0;
    }
    memset(h, 0, sizeof(*h));
    memcpy(h, p, n < sizeof(*h) ? n : sizeof(*h));
    return 1;
}

static void sendHello(Peer* P, int reset) {
    Hello h;
    fillHello(&h, P, reset, 0);
    put(P, SS_MSG_HELLO, &h, sizeof(h), 0);
}

/* the host's hello on a connection with no place (refused, or full): straight on its socket */
static void lobbyHello(Socket s, int full) {
    uint8_t m[5 + sizeof(Hello)];
    const uint32_t len = (uint32_t)(sizeof(Hello) + 1);
    Hello h;
    fillHello(&h, NULL, 0, full);
    memcpy(m, &len, 4);
    m[4] = SS_MSG_HELLO;
    memcpy(m + 5, &h, sizeof(h));
    (void)sendSome(s, m, sizeof(m)); /* (a new connection's empty buffer takes it whole; never waited for) */
}

static void socketOptions(Socket s) {
    int one = 1;
    onlineNonBlocking(s, 1);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

/* a guest: connected to the host */
static void connected(Socket s) {
    Peer* P = RP;
    if (P->conn != BAD_SOCKET) {
        dropConn(P, "replaced by a new connection");
    }
    P->conn = s;
    socketOptions(s);
    P->lastRx = P->lastPing = P->connectedAt = ssNetMs();
    P->rxLen = 0;
    P->helloIn = 0;
    portNetFaultsReset();
    ssLog("net: connected to the host");
    sendHello(P, 0);
}

static int splitAddress(const char* in, char* host, size_t hostSize, int* port) {
    const char* colon;
    *port = SS_DEFAULT_PORT;
    if (in[0] == '[') {
        const char* end = strchr(in, ']');
        if (end == NULL) {
            return 0;
        }
        snprintf(host, hostSize, "%.*s", (int)(end - in - 1), in + 1);
        if (end[1] == ':') {
            *port = (int)strtol(end + 2, NULL, 10);
        }
        return 1;
    }
    colon = strrchr(in, ':');
    if (colon != NULL && strchr(in, ':') == colon) {
        snprintf(host, hostSize, "%.*s", (int)(colon - in), in);
        *port = (int)strtol(colon + 1, NULL, 10);
    } else {
        snprintf(host, hostSize, "%s", in);
    }
    return 1;
}

/* ---- the host's address looked up off the game's thread (a name's lookup can take seconds) ---- */

#define RESOLVED_MAX 4
#define RESOLVE_AGAIN_MS 30000 /* a name looked up again this often (its address may change) */

typedef struct Lookup {
    char host[128], port[16];
    struct sockaddr_storage addr[RESOLVED_MAX];
    int addrLen[RESOLVED_MAX];
    int n;
    volatile long state; /* 0 under way, 1 done, 2 let go by the game (the lookup frees itself) */
} Lookup;

static struct {
    Lookup* busy;    /* the lookup under way, NULL none */
    Lookup* done;    /* the last one's answer */
    unsigned long long doneAt;
} sLook;

static long lookCas(volatile long* v, long from, long to) {
#ifdef _WIN32
    return InterlockedCompareExchange(v, to, from);
#else
    long seen = from;
    __atomic_compare_exchange_n(v, &seen, to, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    return seen;
#endif
}

static void lookRun(Lookup* L) {
    struct addrinfo hints, *res = NULL, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(L->host, L->port, &hints, &res) == 0) {
        for (ai = res; ai != NULL && L->n < RESOLVED_MAX; ai = ai->ai_next) {
            if (ai->ai_addrlen <= sizeof(L->addr[0])) {
                memcpy(&L->addr[L->n], ai->ai_addr, ai->ai_addrlen);
                L->addrLen[L->n++] = (int)ai->ai_addrlen;
            }
        }
        freeaddrinfo(res);
    }
}

/* the lookup's end: done, or freed here when the game let it go meanwhile */
static void lookFinish(Lookup* L) {
    if (lookCas(&L->state, 0, 1) == 2) {
        free(L);
    }
}

#if defined(_WIN32)
static DWORD WINAPI lookThread(LPVOID arg) {
    lookRun((Lookup*)arg);
    lookFinish((Lookup*)arg);
    return 0;
}
#elif !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#include <pthread.h>
static void* lookThread(void* arg) {
    lookRun((Lookup*)arg);
    lookFinish((Lookup*)arg);
    return NULL;
}
#endif

static void lookStart(const char* host, const char* port) {
    Lookup* L = (Lookup*)calloc(1, sizeof(Lookup));
    int started = 0;
    if (L == NULL) {
        return;
    }
    snprintf(L->host, sizeof(L->host), "%s", host);
    snprintf(L->port, sizeof(L->port), "%s", port);
#if defined(_WIN32)
    {
        HANDLE t = CreateThread(NULL, 0, lookThread, L, 0, NULL);
        if (t != NULL) {
            CloseHandle(t);
            started = 1;
        }
    }
#elif !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    {
        pthread_t t;
        if (pthread_create(&t, NULL, lookThread, L) == 0) {
            pthread_detach(t);
            started = 1;
        }
    }
#endif
    if (!started) {
        lookRun(L); /* (no thread: here, as before) */
        lookFinish(L);
    }
    sLook.busy = L;
}

/* the lookups let go (the session's end): one under way frees itself when it's done */
static void lookStop(void) {
    if (sLook.busy != NULL && lookCas(&sLook.busy->state, 0, 2) != 0) {
        free(sLook.busy); /* (it was done already) */
    }
    sLook.busy = NULL;
    free(sLook.done);
    sLook.done = NULL;
}

/* the host's addresses: the last lookup's while it's fresh, else a new one started (NULL until done) */
/* the lookup under way finished: its answer taken as the last one */
static void lookTake(unsigned long long now) {
    if (sLook.busy != NULL && lookCas(&sLook.busy->state, 1, 1) == 1) { /* (an acquire: its answer seen whole) */
        free(sLook.done);
        sLook.done = sLook.busy;
        sLook.busy = NULL;
        sLook.doneAt = now;
    }
}

static const Lookup* lookAddresses(const char* host, const char* port, unsigned long long now) {
    lookTake(now);
    if (sLook.done != NULL && (strcmp(sLook.done->host, host) != 0 || strcmp(sLook.done->port, port) != 0)) {
        free(sLook.done);
        sLook.done = NULL;
    }
    /* (looked up again now and then; no answer, again after a few seconds) */
    if (sLook.busy == NULL &&
        (sLook.done == NULL || now - sLook.doneAt > (sLook.done->n > 0 ? RESOLVE_AGAIN_MS : 3000ull))) {
        lookStart(host, port);
        lookTake(now); /* (no thread: answered at once) */
    }
    return sLook.done != NULL && sLook.done->n > 0 ? sLook.done : NULL; /* (the last answer meanwhile) */
}

static void tryConnect(void) {
    char host[128], port[16];
    int p, i;
    const Lookup* L;
    if (!splitAddress(S.address, host, sizeof(host), &p)) {
        return;
    }
    snprintf(port, sizeof(port), "%d", p);
    if ((L = lookAddresses(host, port, ssNetMs())) == NULL) {
        return;
    }
    for (i = 0; i < L->n; i++) {
        const struct sockaddr* a = (const struct sockaddr*)&L->addr[i];
        Socket s = socket(a->sa_family, SOCK_STREAM, IPPROTO_TCP);
        if (s == BAD_SOCKET) {
            continue;
        }
        /* never waited for: the game goes on (a refused connect takes Windows about 2 seconds);
           connectPoll finishes it */
        onlineNonBlocking(s, 1);
        if (connect(s, a, L->addrLen[i]) == 0) {
            connected(s);
            return;
        }
        if (WOULD_BLOCK()) {
            S.pending = s;
            S.pendingAt = ssNetMs();
            break;
        }
        closeSocket(s);
    }
}

/* a connect that failed (the socket's error; -1 refused, -2 too slow): logged when the reason changes */
static void connectNote(int why) {
    static int sLast;
    if (why != sLast) {
        sLast = why;
        ssLog("net: can't reach the host at %s yet (%s %d): trying again", S.address,
              why == -2 ? "no answer in 3 s" : "error", why);
    }
}

/* the guest's connect under way: done (connected), failed or too slow (closed: tried again) */
static void connectPoll(void) {
    fd_set w, e;
    struct timeval none = {0, 0};
    int err = 0;
    socklen_t len = sizeof(err);
    if (S.pending == BAD_SOCKET) {
        return;
    }
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET(S.pending, &w);
    FD_SET(S.pending, &e);
    if (select((int)S.pending + 1, NULL, &w, &e, &none) > 0) {
        Socket s = S.pending;
        S.pending = BAD_SOCKET;
        if (FD_ISSET(s, &w) && !FD_ISSET(s, &e) &&
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len) == 0 && err == 0) {
            connected(s);
        } else {
            connectNote(err != 0 ? err : -1);
            closeSocket(s);
        }
        return;
    }
    if (ssNetMs() - S.pendingAt > 3000) {
        connectNote(-2);
        closeSocket(S.pending);
        S.pending = BAD_SOCKET;
    }
}

static void openListen(void) {
    struct sockaddr_in6 a6;
    int off = 0, one = 1;
    Socket s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD_SOCKET) {
        ssLog("net: no socket");
        return;
    }
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof(off)); /* IPv4 too */
#ifdef _WIN32
    /* (Windows' SO_REUSEADDR lets another program bind the same port as well and take connections
       meant for this one, silently: a second game hosting, another program. The port is this game's.) */
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&one, sizeof(one));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
#endif
    memset(&a6, 0, sizeof(a6));
    a6.sin6_family = AF_INET6;
    a6.sin6_port = htons((unsigned short)S.port);
    a6.sin6_addr = in6addr_any;
    if (bind(s, (struct sockaddr*)&a6, sizeof(a6)) != 0 || listen(s, 4) != 0) {
        ssLog("net: can't listen on port %d", S.port);
        closeSocket(s);
        return;
    }
    onlineNonBlocking(s, 1);
    S.listen = s;
    ssLog("net: listening on port %d", S.port);
}

/* ---- reliable messages ---- */

static void freeKept(Kept* k) {
    free(k->data);
    k->data = NULL;
}

static void resetReliable(Peer* P) {
    uint32_t s;
    for (s = P->txBase; s != P->txNext; s++) {
        freeKept(&P->log[s % LOG_CAP]);
    }
    P->txBase = P->txNext = P->resendNext = 0;
    P->rxExpect = 0;
    P->ackSent = 0xFFFFFFFFu;
}

static void sendKept(Peer* P, const Kept* k) {
    uint8_t* m;
    if (k->data == NULL || k->len == 0) {
        return; /* (let go: acknowledged, or no memory for it) */
    }
    m = (uint8_t*)malloc(k->len + 4);
    if (m == NULL) {
        return;
    }
    memcpy(m, &k->seq, 4);
    memcpy(m + 4, k->data, k->len);
    put(P, SS_MSG_REL, m, k->len + 4, 1);
    free(m);
}

static void keepRel(Peer* P, int type, const void* p, size_t n) {
    Kept* k;
    if (P->txNext - P->txBase >= LOG_CAP) {
        /* the other side away too long: the oldest let go; a new session's merge
           covers it (the hello's expect falls behind txBase) */
        freeKept(&P->log[P->txBase % LOG_CAP]);
        P->txBase++;
    }
    k = &P->log[P->txNext % LOG_CAP];
    k->seq = P->txNext++;
    k->len = (uint32_t)(n + 1);
    k->data = (uint8_t*)malloc(n + 1);
    if (k->data == NULL) {
        k->len = 0;
        return;
    }
    k->data[0] = (uint8_t)type;
    if (n > 0) {
        memcpy(k->data + 1, p, n);
    }
    if (P->resendNext == k->seq) {
        /* (sent now: nothing older still to go; after a resume the older ones go first, a few each pump) */
        P->resendNext = P->txNext;
        if (peerUp(P)) {
            sendKept(P, k);
        }
    }
}

/* to one player (0: every one), kept for a player away (its connection's resume sends it) */
void ssNetSendRelTo(int player, int type, const void* p, size_t n) {
    int i;
    if (!S.on) {
        return;
    }
    for (i = 0; i < MAX_PEERS; i++) {
        Peer* P = &S.peer[i];
        if (P->used && (player == SS_ALL || P->player == player)) {
            keepRel(P, type, p, n);
        }
    }
}

void ssNetSendRel(int type, const void* p, size_t n) {
    ssNetSendRelTo(SS_ALL, type, p, n);
}

void ssNetSendTo(int player, int type, const void* p, size_t n) {
    int i;
    if (!S.on) {
        return;
    }
    for (i = 0; i < MAX_PEERS; i++) {
        Peer* P = &S.peer[i];
        if (peerUp(P) && (player == SS_ALL || P->player == player)) {
            put(P, type, p, n, 1);
        }
    }
}

void ssNetSend(int type, const void* p, size_t n) {
    ssNetSendTo(SS_ALL, type, p, n);
}

static void acked(Peer* P, uint32_t expect) {
    /* (an ack can't be for more than was sent) */
    if ((int32_t)(expect - P->txNext) > 0 || (int32_t)(expect - P->txBase) <= 0) {
        return;
    }
    while (P->txBase != expect) {
        freeKept(&P->log[P->txBase % LOG_CAP]);
        P->txBase++;
    }
    P->lastResend = ssNetMs();
}

/* the kept messages from one on (within what's kept: txBase..txNext), max of them; the next one not sent */
static uint32_t resendFrom(Peer* P, uint32_t from, int max) {
    uint32_t s;
    int n = 0;
    if ((int32_t)(from - P->txBase) < 0 || (int32_t)(from - P->txNext) > 0) {
        from = P->txBase;
    }
    /* (only while little is still on its way: a resend never fills the queue past what the link drains,
       which would drop the player at OUT_CAP, or pile up in the relay's connection) */
    for (s = from; s != P->txNext && n < max && peerUp(P) && peerBacklog(P) < RESEND_LOW; s++, n++) {
        sendKept(P, &P->log[s % LOG_CAP]);
        P->resent++;
    }
    return s;
}

/* ---- in ---- */

/* the other game can't play with this one: another disc version or build (why, English); 0 none */
static int isBanned(uint64_t nonce) {
    int i;
    for (i = 0; i < S.bannedN; i++) {
        if (S.banned[i] == nonce && nonce != 0) {
            return 1;
        }
    }
    return 0;
}

/* another protocol's refusal, as the newer game says it (English: the menus translate it) */
static const char kFriendNewer[] =
    "Your friend has a newer Paper Cut: update yours from Settings, then Updates (or the store), and join again.";
static const char kFriendOlder[] =
    "Your friend has an older Paper Cut: they need to update from Settings, then Updates (or the store), and join "
    "again.";

static int refusal(const Hello* h, char* why, size_t cap) {
    char build[sizeof(h->build) + 1], game[sizeof(h->game) + 1];
    snprintf(build, sizeof(build), "%.*s", (int)sizeof(h->build), h->build);
    snprintf(game, sizeof(game), "%.*s", (int)sizeof(h->game), h->game);
    why[0] = '\0';
    if (strncmp(game, portVersionSelf()->id, 6) != 0) {
        /* (said plainly: which release each has, US, JP or EU by the disc's ID, G8ME01; its revision too
           when only that differs) */
        const char* mine = portVersionSelf()->id;
        const char* const kRegion[] = {"US", "JP", "EU", "?"};
        const int theirsAt = game[3] == 'E' ? 0 : game[3] == 'J' ? 1 : game[3] == 'P' ? 2 : 3;
        const int mineAt = mine[3] == 'E' ? 0 : mine[3] == 'J' ? 1 : mine[3] == 'P' ? 2 : 3;
        if (theirsAt != mineAt) {
            snprintf(why, cap, "Different disc release (yours %s, your friend's %s): both need the same release.",
                     kRegion[mineAt], kRegion[theirsAt]);
        } else {
            snprintf(why, cap,
                     "Different disc release (yours %s revision %.2s, your friend's %.2s): both need the same "
                     "release.",
                     kRegion[mineAt], mine + 4, game + 4);
        }
    } else if (h->proto != protoOf()) {
        /* another protocol (port/include/port/statesync.h): the older game must update. Fixed words (the
           menus translate them); the builds go to the log, and under the note (portStateSyncRefusedBuilds).
           (0: a build from before the protocol, which wants the very same build: older) */
        snprintf(why, cap, "%s", h->proto > protoOf() ? kFriendNewer : kFriendOlder);
        snprintf(S.refusedBuild, sizeof(S.refusedBuild), "%s", build);
        ssLog("net: another protocol: theirs %u (Paper Cut %s), this game's %u (Paper Cut %s)", (unsigned)h->proto,
              build, (unsigned)protoOf(), buildName());
    } else if (headlessBuild(build) != headlessBuild(buildName())) {
        /* (the headless test build plays only with its own kind: $TTYD_MIX_BACKENDS lets them mix) */
        snprintf(why, cap, "%s",
                 "One of you plays the test build (headless): it plays only with other test builds.");
        ssLog("net: their build: %s, this game's: %s", build, buildName());
    } else {
        char mods[sizeof(h->mods) + 1];
        if (strcmp(build, buildName()) != 0) {
            /* (the same protocol: they play together; noted for the logs) */
            ssLog("net: their build %s, this game's %s: the same protocol (%u), played together", build,
                  buildName(), (unsigned)protoOf());
        }
        snprintf(mods, sizeof(mods), "%.*s", (int)sizeof(h->mods), h->mods);
        if (strcmp(mods, modsName()) != 0) {
            char theirs[sizeof(mods)], ours[sizeof(mods)];
            modsWithoutPm64(mods, theirs, sizeof(theirs));
            modsWithoutPm64(modsName(), ours, sizeof(ours));
            if (!strcmp(theirs, ours)) {
                /* only the Paper Mario 64 partners differ (port/include/port/pm64.h) */
                snprintf(why, cap, "%s",
                         "The Paper Mario 64 partners are on in one game only: every player needs their own "
                         "Paper Mario ROM linked and the partners on, or every player off.");
            } else {
                /* (0.4's words, translated already) */
                snprintf(why, cap, "%s", "The other player has different mods on (both need the same).");
            }
            ssLog("net: their mods: \"%s\", this game's: \"%s\"", mods, modsName());
        }
    }
    return why[0] != '\0';
}

static void gotHello(Peer* P, const Hello* h) {
    int fresh;
    /* by address the host answers each guest's hello (with the guest's player number); a room's two
       games each send theirs when the key's agreed */
    const int answer = S.host && !S.room;
    if (h->magic != SS_MAGIC) {
        dropConn(P, "not a sync game");
        return;
    }
    if (h->host == (uint32_t)S.host) {
        dropConn(P, S.host ? "two hosts" : "two guests");
        return;
    }
    /* the same build on the same disc, or no session (the guest stops trying; the host waits
       for another) */
    if (refusal(h, S.refused, sizeof(S.refused))) {
        ssLog("net: refused: %s", S.refused);
        S.refusedAt = ssNetMs();
        dropConn(P, "another version");
        return;
    }
    if (S.host && isBanned(h->nonce)) {
        /* (a removed player's game, back: told so, never let in again this session) */
        Hello no;
        fillHello(&no, P, 0, 2);
        put(P, SS_MSG_HELLO, &no, sizeof(no), 0);
        ssLog("net: a removed player's game came back: turned away");
        if (S.room) {
            /* (the answer out before the link's dropped, which empties its batch; no new key for that
               link until the relay says a game is there again: that game stops, told why) */
            flushOut();
            P->refusedLink = 1;
            linkLost(P, "removed");
            return;
        }
        flushQueue(P);
        dropConn(P, "removed");
        return;
    }
    if (S.host) {
        S.refused[0] = '\0'; /* (a game let in: an earlier refusal's note is old news) */
    }
    if (!S.host) {
        if (h->full) {
            /* (2: this game was removed from the room, ssNetRemove) */
            snprintf(S.refused, sizeof(S.refused), "%s",
                     h->full == 2 ? "The host removed you from the room." : "The host's game has four players already.");
            ssLog("net: refused: %s", S.refused);
            dropConn(P, "no place");
            return;
        }
        if (h->player >= 2 && h->player <= SS_PLAYERS && (int)h->player != S.me) {
            S.me = (int)h->player;
            ssLog("net: this game is player %d", S.me);
        }
    }
    P->helloIn = 1;
    /* a new session: their game is new to us, or ours to them, or they've lost what we'd resend */
    fresh = h->nonce != P->peerNonce || h->peerNonce != S.nonce || h->reset;
    P->peerNonce = h->nonce;
    if (!fresh && ((int32_t)(h->expect - P->txBase) < 0 || (int32_t)(h->expect - P->txNext) > 0)) {
        /* what they lack is gone from our log, or they want what was never sent (not this session's
           numbering): both start the numbering again */
        fresh = 1;
        if (!answer) {
            sendHello(P, 1);
        }
    }
    if (fresh) {
        resetReliable(P);
    }
    if (answer) {
        sendHello(P, fresh); /* (their numbering starts again with ours) */
    }
    if (fresh) {
        ssLog("net: session with player %d (%s, new)", P->player, S.host ? "they joined" : "joined");
        ssOnSession(P->player, 1);
    } else {
        ssLog("net: session resumed with player %d (they want #%u, we sent up to #%u)", P->player, h->expect,
              P->txNext);
        /* (what they lack, a few each pump from now on: keepUp; never the whole backlog at once) */
        P->resendNext = resendFrom(P, h->expect, RESUME_PER_PUMP);
        ssOnSession(P->player, 0);
    }
}

static void gotRel(Peer* P, const uint8_t* p, size_t n) {
    uint32_t seq;
    if (n < 5) {
        return;
    }
    memcpy(&seq, p, 4);
    if (seq != P->rxExpect) {
        if ((int32_t)(seq - P->rxExpect) < 0) {
            P->dupes++;
        } else {
            P->gaps++; /* one before it was lost: it comes again (the sender's resend) */
        }
        P->ackAt = 0; /* say where we are soon */
        return;
    }
    P->rxExpect++;
    ssOnRel(P->player, p[4], p + 5, n - 5);
}

static void gotHelloBytes(Peer* P, const uint8_t* p, size_t n) {
    Hello h;
    if (readHello(&h, p, n)) {
        gotHello(P, &h);
    }
}

static void dispatch(Peer* P, int type, const uint8_t* p, size_t n) {
    P->got++;
    switch (type) {
        case SS_MSG_HELLO:
            gotHelloBytes(P, p, n);
            break;
        case SS_MSG_ACK:
            if (n >= 4 && P->helloIn) {
                uint32_t e;
                memcpy(&e, p, 4);
                acked(P, e);
            }
            break;
        case SS_MSG_PING:
            /* u32 the sender's clock, u32 0 asked / 1 the answer: the round trip (one from before carries
               nothing) */
            if (n >= 8) {
                uint32_t t[2];
                memcpy(t, p, 8);
                if (t[1] == 0) {
                    t[1] = 1;
                    put(P, SS_MSG_PING, t, 8, 1);
                } else {
                    P->rtt = (int)((uint32_t)ssNetMs() - t[0]);
                }
            }
            break;
        case SS_MSG_REL:
            if (P->helloIn) {
                gotRel(P, p, n);
            }
            break;
        default:
            if (P->helloIn) {
                ssOnMessage(P->player, type, p, n);
            }
            break;
    }
}

/* the messages whole in a buffer handed on; the bytes used (-1: the connection dropped) */
static long takeMessages(Peer* P, const uint8_t* buf, size_t len) {
    size_t at = 0;
    while (len - at >= 5) {
        uint32_t n;
        memcpy(&n, buf + at, 4);
        if (n == 0 || n > RX_CAP - 8) {
            dropConn(P, "bad message");
            return -1;
        }
        if (len - at - 4 < n) {
            break;
        }
        dispatch(P, buf[at + 4], buf + at + 5, n - 1);
        if (P->conn == BAD_SOCKET) {
            return -1;
        }
        at += 4 + n;
    }
    return (long)at;
}

static void readSome(Peer* P) {
    for (;;) {
        int n;
        long at;
        if (P->conn == BAD_SOCKET) {
            return;
        }
        if (P->rxLen >= RX_CAP) {
            dropConn(P, "message too big");
            return;
        }
        n = (int)recv(P->conn, (char*)P->rx + P->rxLen, (int)(RX_CAP - P->rxLen), 0);
        if (n == 0) {
            dropConn(P, "closed by the other side");
            return;
        }
        if (n < 0) {
            if (!WOULD_BLOCK()) {
                /* (the socket's error said: a reset, an abort, a time-out read apart in a test's log) */
                char why[40];
#ifdef _WIN32
                snprintf(why, sizeof(why), "read failed, error %d", (int)WSAGetLastError());
#else
                snprintf(why, sizeof(why), "read failed, error %d", errno);
#endif
                dropConn(P, why);
            }
            return;
        }
        P->rxLen += (size_t)n;
        P->lastRx = ssNetMs();
        at = takeMessages(P, P->rx, P->rxLen);
        if (at < 0) {
            return;
        }
        if (at > 0) {
            memmove(P->rx, P->rx + at, P->rxLen - (size_t)at);
            P->rxLen -= (size_t)at;
        }
    }
}

/* ---- the host's new connections: who they are (their hello), then their place ---- */

static void lobbyClose(Lobby* L) {
    if (L->s != BAD_SOCKET) {
        closeSocket(L->s);
    }
    L->s = BAD_SOCKET;
    L->len = 0;
}

static void lobbyAccept(Socket s) {
    int i;
    for (i = 0; i < LOBBY; i++) {
        Lobby* L = &S.lobby[i];
        if (L->s == BAD_SOCKET) {
            L->s = s;
            L->len = 0;
            L->at = ssNetMs();
            socketOptions(s);
            return;
        }
    }
    closeSocket(s); /* (too many at once: it connects again) */
}

/* the place for a guest's session id: its own again (a reconnect), a free one, or the place of
   the player away the longest; NULL when four are playing */
static Peer* placeFor(uint64_t nonce) {
    Peer* best = NULL;
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        if (S.peer[i].used && S.peer[i].peerNonce == nonce) {
            return &S.peer[i];
        }
    }
    for (i = 0; i < MAX_PEERS; i++) {
        if (!S.peer[i].used) {
            return &S.peer[i];
        }
    }
    for (i = 0; i < MAX_PEERS; i++) {
        Peer* P = &S.peer[i];
        /* (only a place away a while: one dropped a moment ago is kept for its reconnect) */
        if (P->conn == BAD_SOCKET && ssNetMs() - P->lostAt > PLACE_GRACE_MS &&
            (best == NULL || P->lostAt < best->lostAt)) {
            best = P;
        }
    }
    return best;
}

static void lobbyHelloIn(Lobby* L, const Hello* h) {
    char why[256];
    Peer* P;
    if (h->magic != SS_MAGIC || h->host) {
        ssLog("net: a connection that isn't a guest's sync game: closed");
        lobbyClose(L);
        return;
    }
    if (refusal(h, why, sizeof(why))) {
        snprintf(S.refused, sizeof(S.refused), "%s", why);
        S.refusedAt = ssNetMs();
        ssLog("net: refused: %s", why);
        lobbyHello(L->s, 0); /* (they see why from it) */
        lobbyClose(L);
        return;
    }
    if (isBanned(h->nonce)) {
        ssLog("net: a removed player's game came back: turned away");
        lobbyHello(L->s, 2);
        lobbyClose(L);
        return;
    }
    P = placeFor(h->nonce);
    if (P == NULL) {
        ssLog("net: another player wants in: four are playing already (turned away)");
        lobbyHello(L->s, 1);
        lobbyClose(L);
        return;
    }
    if (!P->used || P->peerNonce != h->nonce) {
        if (P->used) {
            ssLog("net: player %d's place goes to a new player (theirs was away the longest)", P->player);
        }
        resetReliable(P);
        P->used = 1;
        P->peerNonce = 0;
    }
    if (P->conn != BAD_SOCKET) {
        dropConn(P, "replaced by a new connection");
    }
    P->conn = L->s;
    L->s = BAD_SOCKET;
    P->rxLen = 0;
    P->helloIn = 0;
    P->lastRx = P->lastPing = P->connectedAt = ssNetMs();
    portNetFaultsReset();
    ssLog("net: player %d connected", P->player);
    gotHello(P, h);
}

static void lobbyPump(unsigned long long now) {
    int i;
    for (i = 0; i < LOBBY; i++) {
        Lobby* L = &S.lobby[i];
        int n;
        uint32_t len;
        if (L->s == BAD_SOCKET) {
            continue;
        }
        if (now > L->at && now - L->at > 5000) { /* (accepted after this pump's now: younger) */
            ssLog("net: a connection sent no hello: closed");
            lobbyClose(L);
            continue;
        }
        n = (int)recv(L->s, (char*)L->buf + L->len, (int)(sizeof(L->buf) - L->len), 0);
        if (n == 0 || (n < 0 && !WOULD_BLOCK())) {
            lobbyClose(L);
            continue;
        }
        if (n > 0) {
            L->len += (size_t)n;
        }
        if (L->len < 5) {
            continue;
        }
        memcpy(&len, L->buf, 4);
        if (L->buf[4] != SS_MSG_HELLO || len < HELLO_MIN + 1 || len > sizeof(L->buf) - 4) {
            ssLog("net: a connection that isn't a sync game: closed");
            lobbyClose(L);
            continue;
        }
        if (L->len >= 4 + len) {
            Hello h;
            const size_t rest = L->len - 4 - len;
            uint8_t more[sizeof(L->buf)];
            int k;
            readHello(&h, L->buf + 5, len - 1);
            memcpy(more, L->buf + 4 + len, rest);
            {
                const Socket s = L->s;
                lobbyHelloIn(L, &h);
                /* what came after the hello: the place's connection's */
                for (k = 0; k < MAX_PEERS && rest > 0; k++) {
                    Peer* P = &S.peer[k];
                    if (P->conn == s && P->conn != BAD_SOCKET) {
                        memcpy(P->rx, more, rest);
                        P->rxLen = rest;
                    }
                }
            }
        }
    }
}

/* ---- a room on the relay ---- */

/* the relay a code names (its first character's place), -1 none */
static int relayOfCode(const char* code) {
    const char* at = code[0] != '\0' ? strchr(PORT_ONLINE_CODE_CHARS, code[0]) : NULL;
    return at != NULL ? (int)(at - PORT_ONLINE_CODE_CHARS) : -1;
}

/* the room list: the next room hosted is listed under this name ("" not listed) */
static char sListName[32];

void portStateSyncListAs(const char* name) {
    snprintf(sListName, sizeof(sListName), "%s", name != NULL ? name : "");
}

/* the rooms listed by protocol, not build: every game that can join one finds it ("sync-p1"; the headless
   test build's "sync-p1-h", kept apart as its hello is). Never a release's number: 0.4.x lists under its
   release ("0.4.1"), and 0.5.0's first builds under "sync-" and their whole build, so neither mixes in.
   (The relay takes letters, digits and . + -, up to 24.) */
const char* portStateSyncListVersion(void) {
    static char v[32];
    snprintf(v, sizeof(v), "sync-p%u%s", (unsigned)protoOf(), headlessBuild(buildName()) ? "-h" : "");
    return v;
}

/* text into a query: letters and digits as they are, the rest %XX */
static void percentEncode(const char* in, char* out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    const unsigned char* c;
    for (c = (const unsigned char*)in; *c != '\0' && n + 4 < cap; c++) {
        if ((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '.' ||
            *c == '-') {
            out[n++] = (char)*c;
        } else {
            out[n++] = '%';
            out[n++] = hex[*c >> 4];
            out[n++] = hex[*c & 15];
        }
    }
    out[n] = '\0';
}

static void openRoom(void) {
    char url[700];
    const char* relay = S.testRelay ? "test://" : portOnlineRelayAt(relayOfCode(S.code));
    size_t len;
    S.wsTry = ssNetMs();
    S.wsTried = 1;
    if (S.triesSince++ == 0) {
        S.beganAt = S.wsTry; /* (the window from the first try: not the session's start, nor a drop's) */
    }
    if (relay == NULL || relay[0] == '\0') {
        snprintf(S.failed, sizeof(S.failed), "%s",
                 portOnlineRelay()[0] == '\0' ? "There's no relay in this build: connect by address."
                                              : "That room code isn't from this version of Paper Cut: check it.");
        ssLog("net: %s", S.failed);
        return;
    }
    /* the room's half only: the secret one stays between the games (but for a listed room) */
    /* (a host asks for every message to it framed by the relay, the guest's too: "framed" says it does) */
    snprintf(url, sizeof(url), "%s/room/%.*s?role=%s%s%s%s", relay, PORT_ONLINE_ROOM_HALF, S.code,
             S.host ? "host" : S.asPeer ? "peer" : "guest", S.ticket[0] != '\0' ? "&ticket=" : "", S.ticket,
             S.host ? "&framed=1" : "");
    S.framed = 0;
    S.oldRelay = 0;
    if (S.host && sListName[0] != '\0') {
        /* listed: open to anyone, so the relay has the whole code, with the host's name and what it
           plays (the room list shows a room only to the same disc and build); up to four play */
        char name[100], v[80];
        percentEncode(sListName, name, sizeof(name));
        percentEncode(portStateSyncListVersion(), v, sizeof(v));
        len = strlen(url);
        snprintf(url + len, sizeof(url) - len, "&list=%s&name=%s&game=%.6s&v=%s&max=%d",
                 S.code + PORT_ONLINE_ROOM_HALF, name, portVersionSelf()->id, v, SS_PLAYERS);
        S.listedAt = ssNetMs();
    }
    pipe_()->open(url);
}

/* the relay's link for a place it names: the guest's (or, joining, the host's), a peer's */
static Peer* placeLink(int peerPlace) {
    if (peerPlace < 0) {
        return RP;
    }
    if (!S.host || peerPlace >= RELAY_PEERS) {
        return NULL;
    }
    return &S.peer[1 + peerPlace];
}

/* both in the room (again): the key, then the hellos */
static void linkHere(Peer* P) {
    if (P->helloIn) {
        ssLog("net: connection lost (player %d came back to the room)", P->player);
        P->reconnects++;
    }
    if (!P->used) {
        P->used = 1; /* (a peer's place: player 3 or 4, kept while it's away) */
    }
    ssLog("net: player %d is in the room: the key", S.host ? P->player : 1);
    P->refusedLink = 0; /* (a game there again: its hello says whether it's a removed one) */
    keyReset(P);
    P->helloIn = 0;
    P->lastRx = ssNetMs();
    portNetFaultsReset();
    startPake(P);
}

static void linkGone(Peer* P) {
    if (P->helloIn) {
        ssLog("net: connection lost (player %d left the room)", S.host ? P->player : 1);
        P->reconnects++;
    }
    keyReset(P);
    P->helloIn = 0;
}

/* the relay's own words */
static void roomText(const char* t) {
    Peer* P;
    if (strncmp(t, "you ", 4) == 0) {
        ssLog("net: in the room (the relay sees an %s address)", strchr(t + 4, ':') != NULL ? "IPv6" : "IPv4");
    } else if (strncmp(t, "ticket ", 7) == 0) {
        snprintf(S.ticket, sizeof(S.ticket), "%s", t + 7); /* this side's place, after a drop */
    } else if (strcmp(t, "framed") == 0) {
        S.framed = S.host; /* (the relay wraps the guest's messages to this host too: FROM_GUEST) */
    } else if (strcmp(t, "peer") == 0) {
        linkHere(RP);
    } else if (strcmp(t, "gone") == 0) {
        linkGone(RP);
    } else if (strncmp(t, "peer ", 5) == 0 && (P = placeLink((int)strtol(t + 5, NULL, 10))) != NULL) {
        if (!S.framed) {
            /* (a relay with players 3 and 4 but from before the framing: "framed" comes before any "peer N"
               on one that frames. Without it a guest's frame could pass for a peer's, so no peer is keyed:
               the peer's told why, the host too, and the two-player game goes on) */
            if (!S.oldRelay) {
                S.oldRelay = 1;
                ssLog("net: the relay has players 3 and 4 but doesn't frame the guest's messages (an older "
                      "relay): two players only until it's updated");
            }
            snprintf(S.relayNote, sizeof(S.relayNote), "%s", OLD_RELAY_NOTE);
            outFrame(P, R_OLD_RELAY, NULL, 0);
            flushOut();
            return;
        }
        linkHere(P);
    } else if (strncmp(t, "peergone ", 9) == 0 && (P = placeLink((int)strtol(t + 9, NULL, 10))) != NULL) {
        linkGone(P);
    }
}

/* the code's second half differs (the keys don't match): the joining game stops, the host
   waits for another try */
static void wrongCode(Peer* P) {
    ssLog("net: the room's key didn't match: another code's second half");
    keyReset(P);
    P->helloIn = 0;
    if (!S.host) {
        snprintf(S.failed, sizeof(S.failed), "That code isn't right: check its second half.");
        pipe_()->close();
        S.wsOpen = 0;
    }
}

static void roomFrame(Peer* P, int kind, const uint8_t* p, size_t n) {
    P->lastRx = ssNetMs();
    if (kind == R_OLD_RELAY) {
        if (!S.host && S.asPeer && !P->keyed && S.failed[0] == '\0') {
            /* (player 3 or 4: the host's relay can't take them yet; said, not waited on for ever) */
            snprintf(S.failed, sizeof(S.failed), "%s", OLD_RELAY_PEER_NOTE);
            ssLog("net: %s", S.failed);
            pipe_()->close();
            S.wsOpen = 0;
        }
        return;
    }
    if (kind == R_PAKE1) {
        uint8_t m[512];
        size_t k = 0;
        if (P->keyed) {
            keyReset(P); /* they started again (a drop this side didn't see) */
            P->helloIn = 0;
        }
        startPake(P);
        if (!pakeOne(P, p, n, m, sizeof(m), &k)) {
            wrongCode(P);
            return;
        }
        outFrame(P, R_PAKE2, m, k);
        flushOut();
    } else if (kind == R_PAKE2) {
        if (!pakeTwo(P, p, n)) {
            wrongCode(P);
            return;
        }
        P->keyed = 1;
        P->helloIn = 0;
        P->connectedAt = P->lastPing = ssNetMs();
        portNetFaultsReset();
        ssLog("net: the room's key agreed with player %d%s", S.host ? P->player : 1,
              S.testRelay ? " (the test relay: not sealed)" : "");
        sendHello(P, 0);
        flushOut();
    } else if (kind == R_SEALED && P->keyed) {
        const uint8_t* q = p;
        size_t m = n;
        uint32_t len;
        if (!S.testRelay) {
            if (S.openedCap < n) {
                uint8_t* grown = (uint8_t*)realloc(S.opened, n);
                if (grown == NULL) {
                    return; /* (no memory: the message dropped) */
                }
                S.opened = grown;
                S.openedCap = n;
            }
            if (!portOpenAt(linkOf(P), PORT_SEAL_STREAM, p, n, S.opened, &m)) {
                if (!P->helloIn) {
                    wrongCode(P);
                } else {
                    dropConn(P, "a sealed message didn't open");
                }
                return;
            }
            q = S.opened;
        }
        if (m >= 5) {
            memcpy(&len, q, 4);
            if (len == m - 4) {
                dispatch(P, q[4], q + 5, len - 1);
            }
        }
    }
}

/* a peer's message, wrapped by the relay (FROM_PEER): its own frames */
static void peerFrames(Peer* P, const uint8_t* p, size_t n) {
    size_t at = 0;
    while (n - at >= 5) {
        uint32_t len;
        memcpy(&len, p + at, 4);
        if (len == 0 || n - at - 4 < len) {
            return;
        }
        if (p[at + 4] >= 0xF0) {
            /* (a frame of the relay's own kinds can only be this player's, passing for another's) */
            if (!P->forgedSeen) {
                P->forgedSeen = 1;
                ssLog("net: player %d's message carried a frame of the relay's kind %d: left out", P->player,
                      p[at + 4]);
            }
            at += 4 + len;
            continue;
        }
        roomFrame(P, p[at + 4], p + at + 5, len - 1);
        if (!S.wsOpen) {
            return;
        }
        at += 4 + len;
    }
}

/* the relay turned this game away: another place to ask for, or why it can't (English) */
static int roomRefused(const char* why) {
    /* (the relay's words, or online_ws_pc.c's for them) */
    const int guestTaken = strstr(why, "room is full") != NULL || strstr(why, "second player") != NULL;
    const int noPeers = strncmp(why, "role:", 5) == 0 || strstr(why, "can't do that yet") != NULL;
    if (S.host) {
        return 0;
    }
    if (!S.asPeer && guestTaken && !S.triedGuest) {
        ssLog("net: the room's second player is there: this game asks for player 3 or 4's place");
        S.asPeer = 1;
        S.ticket[0] = '\0';
        return 1;
    }
    if (S.asPeer && noPeers) {
        if (ssWatchChosen() && !S.triedGuest) {
            /* a relay from before players 3 and 4 in these rooms: a watcher takes the guest's place */
            ssLog("net: this relay has no places for players 3 and 4: watching from the guest's place");
            S.asPeer = 0;
            S.triedGuest = 1;
            S.ticket[0] = '\0';
            return 1;
        }
        snprintf(S.failed, sizeof(S.failed),
                 "That room has its second player, and this relay can't take a third yet (it needs an update).");
        ssLog("net: %s", S.failed);
    }
    return 0;
}

static int roomBinary(const Pipe* P, unsigned long long now);

static void roomPump(unsigned long long now) {
    const Pipe* P = pipe_();
    const int st = S.wsTried ? P->state() : PORT_WS_IDLE;
    char text[128];
    int i;
    if (st != PORT_WS_OPEN && st != PORT_WS_OPENING) {
        int refusal = 0; /* the relay answered, and said no (not one it can't be reached for) */
        if (S.wsOpen) {
            relayGone(now); /* (since when the relay's gone: PORT_SYNC_NO_SERVICE below) */
            S.wsOpen = 0;
            for (i = 0; i < MAX_PEERS; i++) {
                linkLost(&S.peer[i], "the relay's connection closed");
            }
            S.prxLen = 0;
        }
        if (S.wsTried && P->error()[0] != '\0' && strcmp(S.relayNote, P->error()) != 0) {
            snprintf(S.relayNote, sizeof(S.relayNote), "%s", P->error());
            ssLog("net: the relay: %s", S.relayNote);
            if (roomRefused(S.relayNote)) {
                S.relayNote[0] = '\0';
                openRoom(); /* (the other place at once) */
                return;
            }
        }
        if (S.failed[0] != '\0' || (!S.host && S.refused[0] != '\0')) {
            return;
        }
        {
            /* the online service not there: never reached in 6 seconds, or gone 15 (the relay's own
               answers, a full room or a code no one hosts, are its words instead): said plainly, and no
               more tries until the player's Try again (portStateSyncRetry) */
            const char* e = S.relayNote;
            const int unreachable = e[0] == '\0' || strstr(e, "reached") != NULL || strstr(e, "found") != NULL ||
                                    strstr(e, "answer") != NULL || strstr(e, "secure") != NULL ||
                                    strstr(e, "was lost") != NULL;
            if (unreachable && S.triesSince > 0 && now - S.beganAt > (S.everIn ? 15000ull : 6000ull)) {
                snprintf(S.failed, sizeof(S.failed), "%s", PORT_SYNC_NO_SERVICE);
                ssLog("net: %s (the relay: %s)", S.failed, e[0] != '\0' ? e : "no answer");
                return;
            }
            refusal = !unreachable;
        }
        if (refusal) {
            /* the relay answered no: asked again later and later (RETRY_FIRST_MS), and not for ever */
            const char* e = S.relayNote;
            const char* test = getenv("TTYD_SYNC_GIVE_UP");
            const int testSeconds = test != NULL ? atoi(test) : 0;
            const unsigned long long giveUp = testSeconds > 0                ? (unsigned long long)testSeconds * 1000ull
                                              : strstr(e, "hosting") != NULL ? NO_HOST_GIVE_UP_MS
                                                                             : REFUSED_GIVE_UP_MS;
            if (S.refusedSince == 0) {
                S.refusedSince = now;
            }
            if (strstr(e, "full for today") != NULL) {
                /* the relay's day is used up (a 429): every try now only keeps it so; the player's
                   Try again asks once more */
                snprintf(S.failed, sizeof(S.failed), "%s", e);
                ssLog("net: the relay is full for today: no more tries until Try again");
                return;
            }
            if (now - S.refusedSince > giveUp) {
                snprintf(S.failed, sizeof(S.failed), "%s", e);
                ssLog("net: the relay said no for %llu s (%s): no more tries until Try again",
                      (now - S.refusedSince) / 1000ull, e);
                return;
            }
        }
        if (!S.wsTried || now - S.wsTry > (refusal ? S.retryWait : 2000ull)) {
            if (S.wsTried && refusal) {
                S.retryWait = S.retryWait * 2 < RETRY_MAX_MS ? S.retryWait * 2 : RETRY_MAX_MS;
                ssLog("net: asking the relay again (it said: %s); the next try waits %llu s", S.relayNote,
                      S.retryWait / 1000ull);
            }
#ifdef __EMSCRIPTEN__
            /* the browser is never told why the relay said no (its answer comes before the WebSocket:
               online_ws_pc.c), so a joining game never in the room yet asks for the other place in
               turn: the guest's taken, player 3 or 4's (roomRefused's turn on the desktop) */
            if (S.wsTried && !S.host && !S.everIn && S.ticket[0] == '\0') {
                S.asPeer = !S.asPeer;
                ssLog("net: the relay said no (not why, in the browser): asking for %s place next",
                      S.asPeer ? "player 3 or 4's" : "the guest's");
            }
#endif
            openRoom();
        }
        return;
    }
    if (st == PORT_WS_OPENING) {
        return;
    }
    if (!S.wsOpen) {
        S.everIn = 1;
        S.wsOpen = 1;
        S.retryWait = RETRY_FIRST_MS;
        S.refusedSince = 0;
        S.pipeRx = now;
        RP->lastRx = now;
        S.relayNote[0] = '\0';
        ssLog("net: in the relay's room %.4s as the %s", S.code,
              S.host ? "host" : S.asPeer ? "player 3 or 4 (a peer)" : "guest");
    }
    /* a listed room: the relay told again now and then, or the listing goes */
    if (S.host && sListName[0] != '\0' && now - S.listedAt > 10 * 60 * 1000) {
        S.listedAt = now;
        P->sendText("list");
    }
    while (P->text(text, sizeof(text))) {
        S.pipeRx = now;
        if ((strcmp(text, "gone") == 0 || strncmp(text, "peergone ", 9) == 0) && !roomBinary(P, now)) {
            /* (the relay's words and its messages come apart (two queues): what came before the gone is
               read first, under the key it came with. Read after, the host's goodbye that came just
               before it was left out, the key let go, and its guests waited 10 seconds for it) */
            return;
        }
        roomText(text);
    }
    roomBinary(P, now);
}

/* the relay's messages so far, each to its player's connection: 0 when the connection closed meanwhile */
static int roomBinary(const Pipe* P, unsigned long long now) {
    size_t at = 0;
    for (;;) {
        const size_t n = S.prxLen < RX_CAP ? P->read(S.prx + S.prxLen, RX_CAP - S.prxLen) : 0;
        if (n == 0) {
            break;
        }
        S.prxLen += n;
        S.pipeRx = now;
    }
    while (S.prxLen - at >= 5) {
        uint32_t len;
        memcpy(&len, S.prx + at, 4);
        if (len == 0 || len > RX_CAP - 8) {
            ssLog("net: a bad message from the relay");
            pipeDrop();
            return 0;
        }
        if (S.prxLen - at - 4 < len) {
            break;
        }
        if (S.host && S.framed) {
            /* every message here is framed by the relay (its length, FROM_GUEST or FROM_PEER and the
               place): the players' own frames are read inside their message alone, so none can pass for
               another player's, nor run on into the next message */
            const int kind = S.prx[at + 4];
            if (kind == FROM_PEER && len >= 2) {
                Peer* Q = placeLink(S.prx[at + 5]);
                if (Q != NULL) {
                    peerFrames(Q, S.prx + at + 6, len - 2);
                }
            } else if (kind == FROM_GUEST) {
                peerFrames(RP, S.prx + at + 5, len - 1);
            } else {
                ssLog("net: a message from the relay not framed by it (kind %d): left out", kind);
            }
        } else if (S.host && S.prx[at + 4] >= 0xF0) {
            /* (a relay from before the framing: no players 3 and 4 on it, so the guest's stream is the
               only one; a frame of the relay's kinds can only be the guest's own, passing for another's) */
            ssLog("net: the guest's message of the relay's kind %d: left out", S.prx[at + 4]);
        } else {
            roomFrame(RP, S.prx[at + 4], S.prx + at + 5, len - 1);
        }
        if (!S.wsOpen) {
            return 0; /* (closed meanwhile: its buffer emptied) */
        }
        at += 4 + len;
    }
    if (at > 0) {
        memmove(S.prx, S.prx + at, S.prxLen - at);
        S.prxLen -= at;
    }
    return 1;
}


/* tests ($TTYD_SYNC_JOIN_ROOM=listed): the stand-in relay's room list ("OPEN /rooms?..." answered
   with one frame of kind 3, the list's text): its first room's code; 0 none */
static int testListFirst(char* code, size_t cap) {
    char path[200], v[80];
    const unsigned long long until = ssNetMs() + 3000;
    code[0] = '\0';
    percentEncode(portStateSyncListVersion(), v, sizeof(v));
    snprintf(path, sizeof(path), "/rooms?game=%.6s&v=%s", portVersionSelf()->id, v);
    T.listLen = 0;
    if (!onlineNetStart() || !tOpenPath(path)) {
        return 0;
    }
    while (ssNetMs() < until && T.listLen == 0 && T.state != PORT_WS_CLOSED) {
        tPump();
        if (T.listLen == 0) {
            sleepMs(10);
        }
    }
    if (T.listLen > 0) {
        const char* nl = strchr(T.list, '\n');
        const int online = strncmp(T.list, "rooms ", 6) == 0 ? (int)strtol(T.list + 6, NULL, 10) : -1;
        if (nl != NULL && nl[1] != '\0') {
            const char* sp = strchr(nl + 1, ' ');
            if (sp != NULL && (size_t)(sp - nl - 1) < cap) {
                snprintf(code, cap, "%.*s", (int)(sp - nl - 1), nl + 1);
            }
        }
        ssLog("net: test: the room list: %d online, its first room %s", online, code[0] != '\0' ? "taken" : "none");
    }
    tCloseWith("");
    return code[0] != '\0';
}

/* a room's code typed (with or without its dash, any case) as the code: 0 when it isn't one */
static int takeCode(const char* typed, char* out) {
    int n = 0;
    for (; *typed != '\0'; typed++) {
        char c = *typed;
        if (c == '-' || c == ' ') {
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (n >= PORT_ONLINE_CODE_LENGTH || strchr(PORT_ONLINE_CODE_CHARS, c) == NULL) {
            return 0;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return n == PORT_ONLINE_CODE_LENGTH;
}

/* n bytes of the system's randomness (the secret half of a room's code is the key's only secret); 0 none */
static int osRandom(void* out, size_t n) {
#if defined(_WIN32)
    /* (RtlGenRandom: advapi32, in every program's libraries) */
    extern BOOLEAN NTAPI SystemFunction036(PVOID buffer, ULONG length);
    return SystemFunction036(out, (ULONG)n) != 0;
#elif defined(__APPLE__) || defined(__ANDROID__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(out, n);
    return 1;
#else
    FILE* f = fopen("/dev/urandom", "rb");
    size_t got = 0;
    if (f != NULL) {
        got = fread(out, 1, n, f);
        fclose(f);
    }
    return got == n;
#endif
}

/* a new room's code: the first relay's (its first character), the rest the system's randomness (each
   character unbiased: bytes past the last whole run of the characters are drawn again) */
static void newCode(char* out) {
    uint64_t x = S.nonce ^ ((uint64_t)ssNetMs() << 17) ^ 0x9E3779B97F4A7C15ull;
    const int chars = (int)strlen(PORT_ONLINE_CODE_CHARS);
    const int top = 256 - 256 % chars;
    int i;
    out[0] = PORT_ONLINE_CODE_CHARS[0];
    for (i = 1; i < PORT_ONLINE_CODE_LENGTH; i++) {
        unsigned char b = 0;
        int ok = 0, tries;
        for (tries = 0; tries < 64 && !ok; tries++) {
            if (!osRandom(&b, 1)) {
                break;
            }
            ok = b < top;
        }
        if (ok) {
            out[i] = PORT_ONLINE_CODE_CHARS[b % chars];
            continue;
        }
        /* (no system randomness: the old mix) */
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out[i] = PORT_ONLINE_CODE_CHARS[x % (uint64_t)chars];
    }
    out[PORT_ONLINE_CODE_LENGTH] = '\0';
}

/* ---- the frame ---- */

static void clearSockets(void) {
    int i;
    S.listen = S.pending = BAD_SOCKET;
    for (i = 0; i < MAX_PEERS; i++) {
        S.peer[i].conn = BAD_SOCKET;
        S.peer[i].player = i + 2;
        S.peer[i].rtt = -1;
    }
    for (i = 0; i < LOBBY; i++) {
        S.lobby[i].s = BAD_SOCKET;
    }
}

int ssNetBegin(int host, int room, const char* where, int port) {
    const char* at = getenv("TTYD_SYNC_AT");
    const char* test = getenv("TTYD_SYNC_TEST_RELAY");
    int i;
    if (S.on) {
        return 1;
    }
    S.refused[0] = S.failed[0] = S.relayNote[0] = S.ticket[0] = '\0';
    if (!onlineNetStart()) {
        snprintf(S.failed, sizeof(S.failed), "The network couldn't be started.");
        return 0;
    }
    clearSockets();
    S.host = host;
    S.me = host ? 1 : 0;
    S.room = room;
    S.testRelay = room && test != NULL && *test != '\0';
    S.port = port > 0 ? port : SS_DEFAULT_PORT;
    if (!osRandom(&S.nonce, sizeof(S.nonce)) || S.nonce == 0) {
        /* (no system randomness: as before) */
        S.nonce = ((uint64_t)ssNetMs() << 20) ^ (uint64_t)(uintptr_t)&S ^ ((uint64_t)rand() << 40) ^ 0x5EED5EEDull;
#ifdef _WIN32
        S.nonce ^= (uint64_t)GetCurrentProcessId() << 32;
#else
        S.nonce ^= (uint64_t)getpid() << 32;
#endif
    }
    if (room) {
        if (where != NULL && *where != '\0' && strcmp(where, "new") != 0) {
            if (!takeCode(where, S.code)) {
                snprintf(S.failed, sizeof(S.failed), "That isn't a room code: 8 letters and numbers, like K7P2-QX9M.");
                return 0;
            }
        } else if (host) {
            newCode(S.code);
        } else {
            return 0;
        }
    } else if (!host) {
        if (where == NULL || *where == '\0') {
            return 0;
        }
        snprintf(S.address, sizeof(S.address), "%s", where);
    }
    if (!host && sRejoin.nonce != 0) {
        char again[sizeof(sRejoin.where)];
        snprintf(again, sizeof(again), "%s%s", room ? "room:" : "addr:", room ? S.code : S.address);
        if (strcmp(again, sRejoin.where) == 0) {
            S.nonce = sRejoin.nonce;
            ssLog("net: the same room as last time: this game's session id again (its place comes back)");
        }
    }
    S.beganAt = ssNetMs();
    /* a guest's one other game is the host; a room's host has its one guest */
    if (!host || room) {
        RP->used = 1;
        RP->player = host ? 2 : 1;
    }
    /* a room's watcher asks for a peer's place (players 3 and 4's), never taking the guest's from
       a player; a player asks for the guest's, then a peer's when it's taken (roomRefused) */
    S.asPeer = room && !host && ssWatchChosen();
    S.everIn = S.triedGuest = S.noHello = 0;
    S.retryWait = RETRY_FIRST_MS;
    S.refusedSince = 0;
    S.on = 1;
    S.startAt = at != NULL ? (unsigned)(int)strtol(at, NULL, 10) : 0;
    for (i = 0; i < MAX_PEERS; i++) {
        S.peer[i].rx = (uint8_t*)malloc(RX_CAP);
        S.peer[i].ackSent = 0xFFFFFFFFu;
        if (S.peer[i].rx == NULL) {
            ssNetStop();
            snprintf(S.failed, sizeof(S.failed), "The network couldn't be started.");
            return 0;
        }
    }
    if (room) {
        S.prx = (uint8_t*)malloc(RX_CAP);
        if (S.prx == NULL) {
            ssNetStop();
            snprintf(S.failed, sizeof(S.failed), "The network couldn't be started.");
            return 0;
        }
    }
    portNetFaultsSide(S.host);
    if (room) {
        ssLog("on: %s room %.4s-%s%s%s%s", S.host ? "host of" : "guest in", S.code, "****",
              S.host && sListName[0] != '\0' ? " (listed)" : S.asPeer ? " (a watcher: a peer's place)" : "",
              S.testRelay ? " (the test relay)" : "", portNetFaultsOn() ? " (network faults on)" : "");
    } else {
        if (S.host) {
            ssLog("on: host, port %d (up to %d players)%s", S.port, SS_PLAYERS,
                  portNetFaultsOn() ? " (network faults on)" : "");
        } else {
            ssLog("on: guest of %s%s", S.address, portNetFaultsOn() ? " (network faults on)" : "");
        }
    }
    return 1;
}

/* the environment's (tests) */
int ssNetStart(void) {
    const char* host = getenv("TTYD_SYNC_HOST");
    const char* join = getenv("TTYD_SYNC_JOIN");
    const char* hostRoom = getenv("TTYD_SYNC_HOST_ROOM");
    const char* joinRoom = getenv("TTYD_SYNC_JOIN_ROOM");
    if (S.on) {
        return 1;
    }
    if (hostRoom != NULL && *hostRoom) {
        portStateSyncListAs(getenv("TTYD_SYNC_LIST")); /* (tests: listed under that name) */
        return ssNetBegin(1, 1, hostRoom, 0);
    }
    if (joinRoom != NULL && strcmp(joinRoom, "listed") == 0) {
        /* tests: the first room in the stand-in relay's list (the host may not have listed it yet) */
        char code[32];
        int tries;
        for (tries = 0; tries < 100; tries++) {
            if (testListFirst(code, sizeof(code))) {
                return ssNetBegin(0, 1, code, 0);
            }
            sleepMs(200);
        }
        ssLog("net: test: no room in the list");
        return 0;
    }
    if (joinRoom != NULL && *joinRoom) {
        return ssNetBegin(0, 1, joinRoom, 0);
    }
    if (host != NULL && *host) {
        return ssNetBegin(1, 0, NULL, (int)strtol(host, NULL, 10));
    }
    if (join != NULL && *join) {
        return ssNetBegin(0, 0, join, 0);
    }
    return 0;
}

void ssNetStop(void) {
    int i;
    if (!S.on) {
        return;
    }
    if (!S.host) {
        /* (the same room or address again: the same session id, so the same place and seat) */
        snprintf(sRejoin.where, sizeof(sRejoin.where), "%s%s", S.room ? "room:" : "addr:", S.room ? S.code : S.address);
        sRejoin.nonce = S.nonce;
    }
    for (i = 0; i < MAX_PEERS; i++) {
        /* the last words that hadn't gone yet (a resume's backlog: the host's goodbye kept behind it,
           keepRel) out now, in order after what has gone; closed with them still here, a host's Leave
           reached its guests as a drop and they waited 10 seconds for it */
        Peer* P = &S.peer[i];
        int n = 0;
        while (peerUp(P) && P->resendNext != P->txNext && n++ < 64) {
            sendKept(P, &P->log[P->resendNext % LOG_CAP]);
            P->resendNext++;
        }
    }
    if (S.room && S.wsOpen) {
        flushOut(); /* (the last words, the host's goodbye among them, before the relay's connection closes) */
    }
    if (S.room) {
        if (S.wsTried) {
            pipe_()->close();
        }
    } else {
        /* the last words out first (the host's goodbye), then sending closed and each connection closed
           once the other side has read them and closed too (lingerClose: closed with unread data, the
           connection is reset and the last words lost on the way) */
        unsigned long long until = ssNetMs() + 250;
        Socket done[MAX_PEERS];
        int n = 0, waiting = 1;
        while (waiting && ssNetMs() < until) {
            /* (what's still queued for each, a moment at most for all of them together) */
            waiting = 0;
            for (i = 0; i < MAX_PEERS; i++) {
                if (S.peer[i].conn != BAD_SOCKET && S.peer[i].qLen > S.peer[i].qHead) {
                    waiting |= flushQueue(&S.peer[i]) && S.peer[i].qLen > S.peer[i].qHead;
                }
            }
            if (waiting) {
                sleepMs(2);
            }
        }
        for (i = 0; i < MAX_PEERS; i++) {
            if (S.peer[i].conn != BAD_SOCKET) {
                if (S.peer[i].helloIn) {
                    done[n++] = S.peer[i].conn;
                } else {
                    closeSocket(S.peer[i].conn);
                }
                S.peer[i].conn = BAD_SOCKET;
            }
        }
        if (n > 0) {
            lingerClose(done, n, 250);
        }
        for (i = 0; i < LOBBY; i++) {
            lobbyClose(&S.lobby[i]);
        }
        if (S.listen != BAD_SOCKET) {
            closeSocket(S.listen);
        }
        if (S.pending != BAD_SOCKET) {
            closeSocket(S.pending);
        }
    }
    ssLog("net: off (this player left)");
    for (i = 0; i < MAX_PEERS; i++) {
        resetReliable(&S.peer[i]);
        free(S.peer[i].rx);
        if (S.room) {
            keyReset(&S.peer[i]);
        }
        free(S.peer[i].out);
        free(S.peer[i].q);
    }
    lookStop();
    portNetFaultsReset();
    free(S.prx);
    free(S.send);
    free(S.opened);
    memset(&S, 0, sizeof(S));
    clearSockets();
}

int ssNetRoom(void) {
    return S.on && S.room;
}

const char* ssNetCode(void) {
    static char shown[PORT_ONLINE_CODE_LENGTH + 2];
    if (!S.on || !S.room || S.code[0] == '\0') {
        return "";
    }
    snprintf(shown, sizeof(shown), "%.4s-%s", S.code, S.code + PORT_ONLINE_ROOM_HALF);
    return shown;
}

int ssNetPort(void) {
    return S.on && !S.room ? S.port : 0;
}

int ssNetRttMs(int player) {
    const Peer* P = S.on ? peerOf(player) : NULL;
    return P != NULL && peerUp(P) ? P->rtt : -1;
}

/* the host: that player's game out of the room (the co-op side told it first), and turned away if it comes
   back this session (its hello's id) */
void ssNetRemove(int player) {
    Peer* P = S.on && S.host ? peerOf(player) : NULL;
    if (P == NULL) {
        return;
    }
    if (P->peerNonce != 0 && S.bannedN < (int)(sizeof(S.banned) / sizeof(S.banned[0])) && !isBanned(P->peerNonce)) {
        S.banned[S.bannedN++] = P->peerNonce;
    }
    ssLog("net: player %d removed from the room", player);
    if (S.room) {
        linkLost(P, "removed"); /* (its game leaves by itself; a hello again is turned away) */
        return;
    }
    dropConn(P, "removed");
    resetReliable(P);
    P->used = 0;
    P->peerNonce = 0;
}

/* it can't go on: a failure, or the joining game refused (a host waits for another) */
int ssNetFailed(void) {
    return S.failed[0] != '\0' || (!S.host && S.refused[0] != '\0');
}

/* why it can't go on (English): refused, failed, or the relay's last refusal; "" none */
const char* ssNetNote(void) {
    /* (the host's refusal of one game is shown a while, and goes when a game's let in: the host plays on) */
    const int refused = S.refused[0] != '\0' && (!S.host || ssNetMs() - S.refusedAt < 20000);
    return refused ? S.refused : S.failed[0] ? S.failed : S.relayNote;
}

int portStateSyncRefusedBuilds(const char** theirs, const char** mine) {
    const char* note = ssNetNote();
    if (note != S.refused || S.refusedBuild[0] == '\0' ||
        (strcmp(note, kFriendNewer) != 0 && strcmp(note, kFriendOlder) != 0)) {
        return 0;
    }
    *theirs = S.refusedBuild;
    *mine = buildName();
    return 1;
}

/* one connection's upkeep: quiet too long, no hello, the ping, the acknowledgement, resends */
static void keepUp(Peer* P, unsigned long long now) {
    if (now - P->lastRx > (unsigned long long)portNetFaultsQuietMs(5000)) {
        dropConn(P, "quiet");
        return;
    }
    if (!P->helloIn) {
        if (now - P->connectedAt > 5000) {
            dropConn(P, "no hello");
        }
        return;
    }
    if (now - P->lastPing > PING_MS) {
        const uint32_t t[2] = {(uint32_t)now, 0};
        P->lastPing = now;
        put(P, SS_MSG_PING, t, sizeof(t), 1);
    }
    /* the acknowledgement: when it changed, and again now and then (it may be lost) */
    if (peerUp(P) && (P->rxExpect != P->ackSent || now - P->ackAt > 200)) {
        P->ackSent = P->rxExpect;
        P->ackAt = now;
        put(P, SS_MSG_ACK, &P->rxExpect, 4, 1);
    }
    /* a resume's backlog: a few more each pump */
    if (peerUp(P) && P->resendNext != P->txNext) {
        P->resendNext = resendFrom(P, P->resendNext, RESUME_PER_PUMP);
    }
    /* go-back-N: nothing acknowledged for a while, everything not yet acknowledged again (not while a
       resume's backlog still goes: those are what's missing; resendFrom stops while much is on its way) */
    if (peerUp(P) && P->txBase != P->txNext && now - P->lastResend > RESEND_MS && P->resendNext == P->txNext) {
        P->lastResend = now;
        resendFrom(P, P->txBase, 256);
    }
}

static int sPolling;        /* ssNetPoll's pump: no batch flushed */
static unsigned sFlushFrame; /* the frame a room's batch last went (ssNetFrameEnd) */

void ssNetPump(unsigned frame) {
    unsigned long long now = ssNetMs();
    int i, any = 0;
    if (frame != 0xFFFFFFFFu) {
        S.frame = frame; /* (a battle's wait pumps with no frame) */
    }
    if (!S.on || (frame != 0xFFFFFFFFu && frame < S.startAt)) {
        return;
    }
    {
        /* (tests: $TTYD_SYNC_STALL_AT=<frame>:<ms>: this game stands still that long there, nothing read
           or sent, as a game hung or held in a debugger; $TTYD_SYNC_HITCH_MS=<ms>: a pump that long after
           the one before it is logged, as this game frozen meanwhile: another's stall must never do that) */
        static int sRead, sStallAt = -1, sStallMs, sHitchMs;
        static unsigned long long sLast;
        if (!sRead) {
            const char* v = getenv("TTYD_SYNC_STALL_AT");
            sRead = 1;
            if (v != NULL && strchr(v, ':') != NULL) {
                sStallAt = (int)strtol(v, NULL, 10);
                sStallMs = (int)strtol(strchr(v, ':') + 1, NULL, 10);
            }
            v = getenv("TTYD_SYNC_HITCH_MS");
            sHitchMs = v != NULL ? (int)strtol(v, NULL, 10) : 0;
        }
        if (sHitchMs > 0 && sLast != 0 && now - sLast > (unsigned long long)sHitchMs) {
            ssLog("test: a frame took %llu ms", now - sLast);
        }
        if (frame != 0xFFFFFFFFu && (int)frame == sStallAt && sStallMs > 0) {
            ssLog("test: this game stands still %d ms", sStallMs);
            sleepMs(sStallMs);
            now = ssNetMs();
        }
        sLast = now;
    }
    {
        /* (tests: offline a while, the connections kept down; a wait's pumps keep the last frame's word) */
        static int sOffline;
        if (frame != 0xFFFFFFFFu) {
            sOffline = portNetFaultsOffline(frame, ssNetMs());
        }
        if (sOffline) {
            if (S.room) {
                pipeDrop();
            } else {
                for (i = 0; i < MAX_PEERS; i++) {
                    if (S.peer[i].used && linkUp(&S.peer[i])) {
                        dropConn(&S.peer[i], "faults");
                    }
                }
            }
            return;
        }
    }
    if (S.room) {
        roomPump(now);
    } else if (S.host) {
        if (S.listen == BAD_SOCKET) {
            static unsigned long long sLastListen;
            if (sLastListen == 0 || now - sLastListen > 2000) {
                sLastListen = now;
                openListen();
            }
        }
        if (S.listen != BAD_SOCKET) {
            Socket s;
            while ((s = accept(S.listen, NULL, NULL)) != BAD_SOCKET) {
                lobbyAccept(s);
            }
        }
        lobbyPump(now);
    } else {
        connectPoll();
        if (RP->conn != BAD_SOCKET && !RP->helloIn && now > RP->connectedAt && now - RP->connectedAt > 10000) {
            /* connected, and no hello in 10 s: not a game of this kind there (0.4's waits silently) */
            dropConn(RP, "no hello");
        }
        if (RP->conn == BAD_SOCKET && S.pending == BAD_SOCKET && S.refused[0] == '\0' &&
            (S.lastTry == 0 || now - S.lastTry > 1000)) {
            S.lastTry = now;
            tryConnect();
            if (sLook.busy != NULL) {
                S.lastTry = 0; /* (the address being looked up: tried again next pump, not a second later) */
            }
        }
    }
    for (i = 0; i < MAX_PEERS; i++) {
        any |= S.peer[i].used && linkUp(&S.peer[i]);
    }
    if (!any) {
        return;
    }
    if (frame != 0xFFFFFFFFu && portNetFaultsDisconnectAt(frame)) { /* (a wait's pumps have no frame) */
        ssLog("net: test: the connection%s dropped on purpose", S.host && !S.room ? "s" : "");
        if (S.room) {
            pipeDrop(); /* (a room's: the relay's one connection) */
            return;
        }
        for (i = 0; i < MAX_PEERS; i++) {
            if (S.peer[i].used && linkUp(&S.peer[i])) {
                dropConn(&S.peer[i], "faults");
            }
        }
        return;
    }
    portNetFaultsPoll(now, faultsWrite);
    dropFailed();
    for (i = 0; i < MAX_PEERS; i++) {
        Peer* P = &S.peer[i];
        if (!P->used || !linkUp(P)) {
            continue;
        }
        if (!S.room) {
            if (!flushQueue(P)) { /* (what the socket didn't take last time first) */
                dropConn(P, "write failed");
                continue;
            }
            readSome(P);
        }
        if (!linkUp(P)) {
            continue;
        }
        keepUp(P, ssNetMs()); /* (after the connection's times: never before them) */
    }
    /* a room's batch: every 3 frames (the relay counts messages) at the frame's end, after the frame's own
       messages (ssNetFrameEnd; here only when no frame's end came lately), and at once while this game waits
       for the other (not the host's poll before its controllers are read: ssNetPoll) */
    if (S.room && ((frame == 0xFFFFFFFFu && !sPolling) || (frame != 0xFFFFFFFFu && frame - sFlushFrame >= 6))) {
        flushOut();
        sFlushFrame = S.frame;
    }
}

/* the host, as the controllers are read: what came since the frame's pump (a guest's controller frames, played
   this frame rather than the next), nothing flushed */
void ssNetPoll(void) {
    sPolling = 1;
    ssNetPump(0xFFFFFFFFu);
    sPolling = 0;
}

/* the frame's end, after its messages: a room's batch every 3 frames */
void ssNetFrameEnd(void) {
    if (S.on && S.room && S.frame % 3 == 0) {
        flushOut();
        sFlushFrame = S.frame;
    }
}

/* the tests' pace: a plain sleep */
void ssNetSleep(int ms) {
    sleepMs(ms);
}

void ssNetWait(int ms) {
    fd_set r;
    struct timeval wait;
    Socket top = 0;
    int i, n = 0;
    if (S.room) {
        flushOut();
        sleepMs(ms);
        return;
    }
    FD_ZERO(&r);
    for (i = 0; i < MAX_PEERS; i++) {
        if (S.peer[i].conn != BAD_SOCKET) {
            FD_SET(S.peer[i].conn, &r);
            top = S.peer[i].conn > top ? S.peer[i].conn : top;
            n++;
        }
    }
    if (n == 0) {
        sleepMs(ms);
        return;
    }
    wait.tv_sec = ms / 1000;
    wait.tv_usec = (ms % 1000) * 1000;
    select((int)top + 1, &r, NULL, NULL, &wait);
}

/* the reliable messages not acknowledged yet, over every connection */
static unsigned unacked(void) {
    unsigned n = 0;
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        if (S.peer[i].used) {
            n += S.peer[i].txNext - S.peer[i].txBase;
        }
    }
    return n;
}

/* A game ending (a test's last frame): its reliable messages out before it goes. A change made in the last
 * moments of a session, a drop or two just before the end, could otherwise be lost with the program: the
 * reliable layer resends until acknowledged, but only while the program runs. Pumps (receiving, acknowledging,
 * resending, reconnecting) until every message is acknowledged and nothing has come for a short while, at
 * most ms. */
void ssNetDrain(int ms) {
    const unsigned long long start = ssNetMs();
    unsigned long long quietSince = start;
    unsigned gotBefore = 0;
    int i;
    if (!S.on) {
        return;
    }
    while (ssNetMs() - start < (unsigned long long)ms) {
        unsigned got = 0;
        ssNetPump(0xFFFFFFFFu);
        for (i = 0; i < MAX_PEERS; i++) {
            got += S.peer[i].rxExpect; /* (reliable ones taken: pings and places don't count) */
        }
        if (got != gotBefore) {
            gotBefore = got;
            quietSince = ssNetMs();
        }
        /* (everything of ours acknowledged, and the others quiet for half a second: theirs are in too) */
        if (unacked() == 0 && ssNetMs() - quietSince > 500 && ssNetMs() - start > 600) {
            break;
        }
        ssNetWait(5);
    }
    ssLog("net: at the end, %u reliable messages not acknowledged after %llu ms", unacked(), ssNetMs() - start);
}

const char* portStateSyncRefused(void) {
    return S.refused;
}

const char* ssNetSummary(void) {
    unsigned sent = 0, got = 0, resent = 0, dupes = 0, gaps = 0, reconnects = 0, unacked = 0;
    char players[32] = "";
    size_t k = 0;
    int i;
    for (i = 0; i < MAX_PEERS; i++) {
        const Peer* P = &S.peer[i];
        if (!P->used) {
            continue;
        }
        sent += P->sent;
        got += P->got;
        resent += P->resent;
        dupes += P->dupes;
        gaps += P->gaps;
        reconnects += P->reconnects;
        unacked += P->txNext - P->txBase;
        if (peerUp(P) && k + 3 < sizeof(players)) {
            k += (size_t)snprintf(players + k, sizeof(players) - k, "%s%d", k ? "," : "", P->player);
        }
    }
    snprintf(S.summary, sizeof(S.summary),
             "me %d with %s | sent %u got %u resent %u dupes %u gaps %u reconnects %u unacked %u faults: %s",
             ssNetMe(), k ? players : "-", sent, got, resent, dupes, gaps, reconnects, unacked,
             portNetFaultsOn() ? portNetFaultsSummary() : "off");
    return S.summary;
}

/* every message's size, the same in every build (port/statesync.h WIRE_SIZE) */
WIRE_SIZE(Hello, 360);
