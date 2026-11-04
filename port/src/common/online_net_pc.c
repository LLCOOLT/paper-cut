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
int portOnlineLocalAddress(char* out, size_t size) {
    (void)size;
    out[0] = '\0';
    return 0;
}

#else

#include <SDL3/SDL.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Socket;
#define BAD_SOCKET INVALID_SOCKET
#define closeSocket closesocket
#define WOULD_BLOCK() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Socket;
#define BAD_SOCKET (-1)
#define closeSocket close
#define WOULD_BLOCK() (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS)
#endif

enum { MSG_HELLO = 1, MSG_PING, MSG_PONG, MSG_SETUP, MSG_READY, MSG_START, MSG_INPUT, MSG_HASH, MSG_BYE };
#define MAGIC 0x504F4350u /* "PCOP" */
#define PROTOCOL 1
#define RING 512 /* frames of input kept (the delay is a few) */
#define MAX_MESSAGE (16 * 1024 * 1024)

static Socket sListen = BAD_SOCKET, sSock = BAD_SOCKET;
static int sState = PORT_ONLINE_IDLE, sHost, sConnecting;
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
    unsigned char pad[PORT_ONLINE_PAD_BYTES];
} sRemote[RING];
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
}

static void fail(const char* why) {
    closeAll();
    sState = PORT_ONLINE_FAILED;
    status("%s", why);
}

static int netStart(void) {
#ifdef _WIN32
    static int sStarted;
    if (!sStarted) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return 0;
        }
        sStarted = 1;
    }
#endif
    return 1;
}

static void nonBlocking(Socket s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
#endif
#endif
    {
        int one = 1; /* each frame's input at once */
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
}

/* the whole buffer out, waiting while the socket is full */
static int sendAll(const void* data, size_t len) {
    const char* p = (const char*)data;
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

static int sendMessage(int type, const void* payload, size_t len) {
    unsigned char head[5];
    const unsigned total = (unsigned)len + 1;
    head[0] = (unsigned char)total;
    head[1] = (unsigned char)(total >> 8);
    head[2] = (unsigned char)(total >> 16);
    head[3] = (unsigned char)(total >> 24);
    head[4] = (unsigned char)type;
    if (!sendAll(head, sizeof(head)) || (len > 0 && !sendAll(payload, len))) {
        return 0;
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

/* what the socket has now, into the buffer; 0 when the connection closed */
static int receive(void) {
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
        memcpy(sRemote[frame % RING].pad, p + 4, PORT_ONLINE_PAD_BYTES);
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

/* everything waiting, handled; 0 when the connection is gone */
static int pumpSocket(void) {
    unsigned char* p;
    size_t len;
    int type;
    if (sSock == BAD_SOCKET) {
        return 0;
    }
    if (!receive()) {
        fail("The other player left.");
        return 0;
    }
    while ((type = takeMessage(&p, &len)) != 0) {
        if (sState == PORT_ONLINE_STARTED) {
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
        if (sSock == BAD_SOCKET) {
            return 0;
        }
    }
    return sSock != BAD_SOCKET;
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
    /* tests ($TTYD_ONLINE_HOST): this computer only, no firewall's question */
    addr.sin6_addr = getenv("TTYD_ONLINE_HOST") != NULL ? in6addr_loopback : in6addr_any;
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

void portOnlineLobbyPump(void) {
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
    return sRtt;
}

int portOnlineIsHost(void) {
    return sHost;
}

int portOnlineSendSetup(const void* setup, size_t len) {
    if (!sHost || sState != PORT_ONLINE_PEER_HERE) {
        return 0;
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
    if (sSock != BAD_SOCKET && why != NULL) {
        sendMessage(MSG_BYE, why, strlen(why));
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
    return sState == PORT_ONLINE_STARTED && sSock != BAD_SOCKET;
}

void portOnlineSendInput(unsigned frame, const unsigned char pad[PORT_ONLINE_PAD_BYTES]) {
    unsigned char m[4 + PORT_ONLINE_PAD_BYTES];
    put32(m, frame);
    memcpy(m + 4, pad, PORT_ONLINE_PAD_BYTES);
    if (portOnlineConnected() && !sendMessage(MSG_INPUT, m, sizeof(m))) {
        fail("The other player left.");
    }
}

int portOnlineWaitInput(unsigned frame, unsigned char pad[PORT_ONLINE_PAD_BYTES], int timeoutMs) {
    const Uint64 start = SDL_GetTicks();
    Uint64 lastPump = start;
    for (;;) {
        if (!portOnlineConnected()) {
            return 0;
        }
        pumpSocket();
        if (sRemote[frame % RING].have) {
            memcpy(pad, sRemote[frame % RING].pad, PORT_ONLINE_PAD_BYTES);
            sRemote[frame % RING].have = 0;
            return 1;
        }
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
        {
            fd_set r;
            struct timeval wait = {0, 1000};
            FD_ZERO(&r);
            FD_SET(sSock, &r);
            select((int)sSock + 1, &r, NULL, NULL, &wait);
        }
    }
}

void portOnlineSendHash(unsigned frame, unsigned long long hash) {
    unsigned char m[12];
    put32(m, frame);
    put64(m + 4, hash);
    if (portOnlineConnected() && !sendMessage(MSG_HASH, m, sizeof(m))) {
        fail("The other player left.");
    }
}

int portOnlinePeerHash(unsigned frame, unsigned long long* hash) {
    const int i = (frame / PORT_ONLINE_HASH_EVERY) % 16;
    if (sRemoteHash[i].have && sRemoteHash[i].frame == frame) {
        *hash = sRemoteHash[i].hash;
        sRemoteHash[i].have = 0;
        return 1;
    }
    return 0;
}

void portOnlinePump(void) {
    if (portOnlineConnected()) {
        pumpSocket();
    }
}

#endif
