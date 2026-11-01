/* LiveSplit's "LiveSplit Server" component (port/include/port/practice.h):
 * the timer's events sent as its text commands ("starttimer\r\n", "split",
 * "pausegametime", "setgametime 0:01:23.45"...) over TCP to 127.0.0.1 at its
 * port (16834 unless changed), so a runner's layouts and splits work as with
 * any autosplitter. LiveSplit listens; the game connects, again every few
 * seconds while it isn't there. Only the host's sockets, no game headers. */
#include <stdio.h>
#include <string.h>
#undef select /* port_prefix.h's rename for the game (tik_05.c): this is the host's */
#include "port/practice.h"

#if defined(__EMSCRIPTEN__)

void portLiveSplitTick(int enabled, int port) {
    (void)enabled;
    (void)port;
}
void portLiveSplitSend(const char* command) {
    (void)command;
}
const char* portLiveSplitStatus(void) {
    return "Not in this build";
}

#else

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Socket;
#define BAD_SOCKET INVALID_SOCKET
#define closeSocket closesocket
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Socket;
#define BAD_SOCKET (-1)
#define closeSocket close
#endif

static Socket sSock = BAD_SOCKET;
static int sEnabled;
static int sConnected; /* else connecting, while sSock is open */
static double sNextTry;
static int sPort;
static char sStatus[64] = "Off";
/* commands waiting for the connection (a start or split made while it was down is kept) */
static char sQueue[1024];
static size_t sQueued;

static void drop(const char* why) {
    if (sSock != BAD_SOCKET) {
        closeSocket(sSock);
    }
    sSock = BAD_SOCKET;
    sConnected = 0;
    snprintf(sStatus, sizeof(sStatus), "%s", why);
}

static void startConnect(int port) {
    struct sockaddr_in addr;
#ifdef _WIN32
    static int sStarted;
    u_long nonBlocking = 1;
    if (!sStarted) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            snprintf(sStatus, sizeof(sStatus), "No network");
            return;
        }
        sStarted = 1;
    }
#endif
    sSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sSock == BAD_SOCKET) {
        snprintf(sStatus, sizeof(sStatus), "No network");
        return;
    }
#ifdef _WIN32
    ioctlsocket(sSock, FIONBIO, &nonBlocking);
#else
    fcntl(sSock, F_SETFL, fcntl(sSock, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(sSock, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
#endif
#endif
    {
        int one = 1; /* each command at once */
        setsockopt(sSock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(sSock, (struct sockaddr*)&addr, sizeof(addr)); /* finishes later (non-blocking) */
    sConnected = 0;
    snprintf(sStatus, sizeof(sStatus), "Connecting to port %d...", port);
}

/* the connection attempt's result, without waiting */
static void checkConnect(void) {
    fd_set w, e;
    struct timeval none = {0, 0};
    int err = 0;
    socklen_t len = sizeof(err);
    FD_ZERO(&w);
    FD_ZERO(&e);
    FD_SET(sSock, &w);
    FD_SET(sSock, &e);
    if (select((int)sSock + 1, NULL, &w, &e, &none) <= 0) {
        return; /* still going */
    }
    getsockopt(sSock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (FD_ISSET(sSock, &e) || err != 0) {
        char why[64];
        snprintf(why, sizeof(why), "LiveSplit Server not found on port %d", sPort);
        drop(why);
        return;
    }
    sConnected = 1;
    snprintf(sStatus, sizeof(sStatus), "Connected (port %d)", sPort);
}

static void flush(void) {
    while (sQueued > 0) {
#ifdef MSG_NOSIGNAL
        const int sent = (int)send(sSock, sQueue, (int)sQueued, MSG_NOSIGNAL);
#else
        const int sent = (int)send(sSock, sQueue, (int)sQueued, 0);
#endif
        if (sent <= 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                return;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
#endif
            drop("LiveSplit closed the connection");
            return;
        }
        memmove(sQueue, sQueue + sent, sQueued - (size_t)sent);
        sQueued -= (size_t)sent;
    }
}

void portLiveSplitTick(int enabled, int port) {
    const double now = portHostSeconds();
    if (!enabled) {
        if (sEnabled) {
            drop("Off");
        }
        sEnabled = 0;
        sQueued = 0;
        return;
    }
    if (!sEnabled) {
        sEnabled = 1;
        sNextTry = 0.0;
    }
    if (port != sPort && sSock != BAD_SOCKET) {
        drop("");
    }
    sPort = port;
    if (sSock == BAD_SOCKET) {
        if (now >= sNextTry) {
            sNextTry = now + 3.0;
            startConnect(port);
        }
        return;
    }
    if (!sConnected) {
        checkConnect();
        if (!sConnected) {
            return;
        }
    }
    {
        /* LiveSplit's replies (to commands this doesn't send): read and dropped */
        char buf[256];
        fd_set r;
        struct timeval none = {0, 0};
        FD_ZERO(&r);
        FD_SET(sSock, &r);
        if (select((int)sSock + 1, &r, NULL, NULL, &none) > 0) {
            const int got = (int)recv(sSock, buf, sizeof(buf), 0);
            if (got == 0) {
                drop("LiveSplit closed the connection");
                return;
            }
        }
    }
    flush();
}

void portLiveSplitSend(const char* command) {
    const size_t n = strlen(command);
    if (!sEnabled) {
        return;
    }
    if (sQueued + n + 2 > sizeof(sQueue)) {
        sQueued = 0; /* a long time unconnected: the old ones don't matter now */
    }
    memcpy(sQueue + sQueued, command, n);
    memcpy(sQueue + sQueued + n, "\r\n", 2);
    sQueued += n + 2;
    if (sConnected) {
        flush();
    }
}

const char* portLiveSplitStatus(void) {
    return sStatus;
}

#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
double portHostSeconds(void) {
    static LARGE_INTEGER sFreq;
    LARGE_INTEGER now;
    if (sFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&sFreq);
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)sFreq.QuadPart;
}
#else
#include <time.h>
double portHostSeconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif
