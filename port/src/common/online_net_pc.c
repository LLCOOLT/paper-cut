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
 *   INPUT  a frame number and that frame's controller state (16 bytes)
 *   HASH   a frame number and the state's fingerprint at its end
 *   BYE    the reason, when either side leaves
 * TCP with Nagle off: each frame's input goes at once, and the stream keeps
 * them in order with none lost. */
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
int portOnlineWaitInput(unsigned frame, unsigned char pad[PORT_ONLINE_PAD_BYTES], int timeoutMs) {
    (void)frame;
    (void)pad;
    (void)timeoutMs;
    return 0;
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
       MSG_RESYNC, MSG_RESYNC_ASK };
#define MAGIC 0x504F4350u /* "PCOP" */
#define PROTOCOL 1
#define RING 512 /* frames of input kept (the delay is a few) */
#define MAX_MESSAGE (16 * 1024 * 1024)

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
    unsigned char pad[PORT_ONLINE_PAD_BYTES];
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

static void status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sStatus, sizeof(sStatus), fmt, ap);
    va_end(ap);
    SDL_Log("online: %s", sStatus);
}

static void closeAll(void) {
    if (sSock != BAD_SOCKET) {
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

static void sendHello(void) {
    unsigned char m[4 + 2 + sizeof(sMe.version) + sizeof(sMe.game) + sizeof(sMe.mods) + sizeof(sMe.name)];
    unsigned char* p = m;
    put32(p, MAGIC);
    p += 4;
    p[0] = PROTOCOL;
    p[1] = 0;
    p += 2;
    memcpy(p, sMe.version, sizeof(sMe.version));
    p += sizeof(sMe.version);
    memcpy(p, sMe.game, sizeof(sMe.game));
    p += sizeof(sMe.game);
    memcpy(p, sMe.mods, sizeof(sMe.mods));
    p += sizeof(sMe.mods);
    memcpy(p, sMe.name, sizeof(sMe.name));
    sendMessage(MSG_HELLO, m, sizeof(m));
}

/* the peer's hello: the same protocol, version, disc and mods, or the reason */
static const char* readHello(const unsigned char* p, size_t len) {
    if (len < 6 + sizeof(sPeer.version) + sizeof(sPeer.game) + sizeof(sPeer.mods) + sizeof(sPeer.name) ||
        get32(p) != MAGIC) {
        return "That isn't Paper Cut on the other side.";
    }
    if (p[4] != PROTOCOL) {
        return "The other player's Paper Cut is a different version.";
    }
    p += 6;
    memcpy(sPeer.version, p, sizeof(sPeer.version));
    p += sizeof(sPeer.version);
    memcpy(sPeer.game, p, sizeof(sPeer.game));
    p += sizeof(sPeer.game);
    memcpy(sPeer.mods, p, sizeof(sPeer.mods));
    p += sizeof(sPeer.mods);
    memcpy(sPeer.name, p, sizeof(sPeer.name));
    sPeer.version[sizeof(sPeer.version) - 1] = sPeer.game[sizeof(sPeer.game) - 1] = '\0';
    sPeer.mods[sizeof(sPeer.mods) - 1] = sPeer.name[sizeof(sPeer.name) - 1] = '\0';
    if (strcmp(sPeer.version, sMe.version) != 0) {
        return "The other player's Paper Cut is a different version.";
    }
    if (strcmp(sPeer.game, sMe.game) != 0) {
        return "The other player has a different release of the game (both need the same: Japanese, US or "
               "European).";
    }
    if (strcmp(sPeer.mods, sMe.mods) != 0) {
        return "The other player has different mods on (both need the same).";
    }
    return NULL;
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
    const size_t used = 4 + get32(sIn);
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

/* the relay's news: its text messages */
static void relayNews(void) {
    char text[96];
    while (portWsText(text, sizeof(text))) {
        if (strncmp(text, "you ", 4) == 0) {
            SDL_Log("online: in the room (the relay sees an %s address)", strchr(text + 4, ':') ? "IPv6" : "IPv4");
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
    for (;;) {
        int n;
        if (sInCap - sInLen < 65536) {
            sInCap = sInCap * 2 + 65536;
            sIn = (unsigned char*)realloc(sIn, sInCap);
        }
        n = (int)recv(sSock, (char*)sIn + sInLen, (int)(sInCap - sInLen), 0);
        if (n > 0) {
            sInLen += (size_t)n;
        } else if (n < 0 && WOULD_BLOCK()) {
            return 1;
        } else {
            return 0;
        }
    }
}

static void handleSession(int type, const unsigned char* p, size_t len) {
    if (type == MSG_INPUT && len >= 4 + PORT_ONLINE_PAD_BYTES) {
        const unsigned frame = get32(p);
        sRemote[frame % RING].have = 1;
        sRemote[frame % RING].frame = frame; /* (a frame from before a recovery isn't the new one's) */
        memcpy(sRemote[frame % RING].pad, p + 4, PORT_ONLINE_PAD_BYTES);
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
        if (sState != PORT_ONLINE_FAILED) {
            fail(sRelay && portWsError()[0] != '\0' ? portWsError() : "The other player left.");
        }
        return 0;
    }
    if (sState != PORT_ONLINE_STARTED) {
        sendIceSignals();
    }
    while ((type = takeMessage(&p, &len)) != 0) {
        if (sRelay) {
            /* a room's: the key's rounds, then only sealed messages (a relay can't add any) */
            if (type == MSG_PAKE1) {
                unsigned char m[512];
                size_t n = 0;
                startPake();
                if (sState == PORT_ONLINE_FAILED || !portPakeRoundOne(p, len, m, sizeof(m), &n)) {
                    dropMessage();
                    fail("A secure connection couldn't be set up.");
                    return 0;
                }
                sendRaw(MSG_PAKE2, m, n);
                dropMessage();
                continue;
            }
            if (type == MSG_PAKE2) {
                if (!portPakeRoundTwo(p, len)) {
                    dropMessage();
                    fail("A secure connection couldn't be set up.");
                    return 0;
                }
                dropMessage();
                if (!sHost) { /* the key is there: the handshake, sealed */
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
        } else if (sState == PORT_ONLINE_STARTED) {
            handleSession(type, p, len);
        } else if (type == MSG_HELLO) {
            const char* why = readHello(p, len);
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
            sState = PORT_ONLINE_GOT_SETUP;
            status("Getting ready...");
        } else if (type == MSG_READY && sHost) {
            sendMessage(MSG_START, NULL, 0);
            sState = PORT_ONLINE_STARTED;
            status("Playing online with %s.", sPeer.name[0] ? sPeer.name : "the other player");
        } else if (type == MSG_START && !sHost) {
            sState = PORT_ONLINE_STARTED;
            status("Playing online with %s.", sPeer.name[0] ? sPeer.name : "the host");
        } else if (type == MSG_BYE) {
            char why[160];
            snprintf(why, sizeof(why), "%.*s", (int)(len < sizeof(why) - 1 ? len : sizeof(why) - 1),
                     (const char*)p);
            dropMessage();
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
    sState = PORT_ONLINE_LISTENING;
    status("Waiting for the other player to join (port %d)...", port);
    return 1;
}

int portOnlineJoin(const char* address, int port, const PortOnlineHello* me) {
    struct addrinfo hints, *found = NULL, *a;
    char service[16];
    portOnlineCancel(NULL);
    sMe = *me;
    sHost = 0;
    if (!netStart()) {
        fail("No network.");
        return 0;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", port);
    if (getaddrinfo(address, service, &hints, &found) != 0 || found == NULL) {
        fail("That address wasn't found.");
        return 0;
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
        fail("Couldn't reach that address.");
        return 0;
    }
    sConnecting = 1;
    sPingSent = SDL_GetTicks();
    sState = PORT_ONLINE_CONNECTING;
    status("Connecting to %s...", address);
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
    pumpSocket();
}

void portOnlineLobbyPump(void) {
    if (sRelay) {
        relayLobbyPump();
        return;
    }
    if (sState == PORT_ONLINE_LISTENING && sListen != BAD_SOCKET) {
        Socket s = accept(sListen, NULL, NULL);
        if (s != BAD_SOCKET) {
            sSock = s;
            nonBlocking(sSock);
            closeSocket(sListen);
            sListen = BAD_SOCKET;
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
            sendHello();
            status("Connected: checking the game matches...");
        } else if (SDL_GetTicks() - sPingSent > 10000) {
            fail("Couldn't reach the host: check the address and that they are waiting.");
        }
        return;
    }
    if (sState != PORT_ONLINE_IDLE && sState != PORT_ONLINE_FAILED) {
        pumpSocket();
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
    sState = PORT_ONLINE_WAITING_READY;
    status("Starting...");
    return 1;
}

const void* portOnlineSetup(size_t* len) {
    *len = sSetupLen;
    return sSetup;
}

void portOnlineGuestReady(void) {
    if (!sHost && sState == PORT_ONLINE_GOT_SETUP && sendMessage(MSG_READY, NULL, 0)) {
        sState = PORT_ONLINE_WAITING_START;
    }
}

void portOnlineCancel(const char* why) {
    if (why != NULL && sUseIce) {
        portIceSendBye(why);
    }
    if (why != NULL && (sSock != BAD_SOCKET || (sRelay && portWsState() == PORT_WS_OPEN))) {
        sendMessage(MSG_BYE, why, strlen(why)); /* (the relay's goes out before its socket closes) */
    }
    closeAll();
    memset(sRemote, 0, sizeof(sRemote));
    memset(sRemoteHash, 0, sizeof(sRemoteHash));
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
    return sState == PORT_ONLINE_STARTED && linkUp();
}

/* the direct path's news: the other side's goodbye */
static int iceGone(void) {
    const char* why = sUseIce ? portIceBye() : NULL;
    if (sUseIce && why == NULL) {
        /* closed without a goodbye: the relay saw its connection go, and the path is quiet */
        relayNews();
        if (sGoneAt != 0 && portIceSilentMs() > 1000) {
            why = "The other player left.";
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

void portOnlineSendInput(unsigned frame, const unsigned char pad[PORT_ONLINE_PAD_BYTES]) {
    unsigned char m[4 + PORT_ONLINE_PAD_BYTES];
    if (sUseIce) {
        if (portOnlineConnected()) {
            portIceSendInput(frame, pad, PORT_ONLINE_PAD_BYTES);
        }
        return;
    }
    put32(m, frame);
    memcpy(m + 4, pad, PORT_ONLINE_PAD_BYTES);
    if (portOnlineConnected() && !sendMessage(MSG_INPUT, m, sizeof(m))) {
        fail("The other player left.");
    }
}

static int haveInput(unsigned frame, unsigned char pad[PORT_ONLINE_PAD_BYTES]) {
    if (sUseIce) {
        return portIcePeerInput(frame, pad, PORT_ONLINE_PAD_BYTES);
    }
    if (sRemote[frame % RING].have && sRemote[frame % RING].frame == frame) {
        memcpy(pad, sRemote[frame % RING].pad, PORT_ONLINE_PAD_BYTES);
        sRemote[frame % RING].have = 0;
        return 1;
    }
    return 0;
}

/* 2: a recovery came (or was asked for) while waiting: online_pc.c takes it */
static int resyncWaiting(void) {
    return (!sHost && sResync != NULL) || (sHost && sResyncAsked >= 0);
}

int portOnlineWaitInput(unsigned frame, unsigned char pad[PORT_ONLINE_PAD_BYTES], int timeoutMs) {
    const Uint64 start = SDL_GetTicks();
    Uint64 lastPump = start, lastStream = 0;
    for (;;) {
        if (!portOnlineConnected() || iceGone()) {
            return 0;
        }
        if (!sUseIce) {
            pumpSocket();
        } else if (SDL_GetTicks() - lastStream >= 10) {
            pumpSocket(); /* the direct path carries the input; the stream a recovery */
            lastStream = SDL_GetTicks();
        }
        if (resyncWaiting()) {
            return 2;
        }
        if (haveInput(frame, pad)) {
            return 1;
        }
        /* waiting: this side's batch goes now, or both would wait on each other */
        flushRelay();
        if (SDL_GetTicks() - start > (Uint64)timeoutMs) {
            portOnlineCancel("The connection was too slow.");
            sState = PORT_ONLINE_FAILED;
            status("The other player stopped answering.");
            return 0;
        }
        if (SDL_GetTicks() - lastPump > 100) {
            SDL_PumpEvents(); /* the window stays responsive while waiting */
            lastPump = SDL_GetTicks();
        }
        if (sRelay) {
            SDL_DelayNS(500000); /* the relay's thread and libjuice's fill the buffers */
        } else {
            onlineReadable(sSock, 1);
        }
    }
}

void portOnlineSendHash(unsigned frame, unsigned long long hash) {
    unsigned char m[12];
    if (sUseIce) {
        portIceSendHash(frame, hash);
        return;
    }
    put32(m, frame);
    put64(m + 4, hash);
    if (portOnlineConnected() && !sendMessage(MSG_HASH, m, sizeof(m))) {
        fail("The other player left.");
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
    if (portOnlineConnected() && !iceGone()) {
        pumpSocket();
    }
}

/* --- recovery (online_pc.c) --- */

void portOnlineSendResync(const void* payload, size_t len) {
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
