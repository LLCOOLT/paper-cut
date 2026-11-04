/* Online co-op's sockets, the same on Windows and elsewhere: the connection
 * (online_net_pc.c), the relay's WebSocket (online_ws_pc.c) and the direct
 * UDP path (online_udp_pc.c). The host's headers, no game ones. */
#ifndef PORT_ONLINE_SOCK_H
#define PORT_ONLINE_SOCK_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

/* Winsock started once (elsewhere nothing to do); 0 when there's no network */
static inline int onlineNetStart(void) {
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

static inline void onlineNonBlocking(Socket s, int on) {
#ifdef _WIN32
    u_long v = (u_long)on;
    ioctlsocket(s, FIONBIO, &v);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    }
#endif
#endif
}

/* a socket readable within ms milliseconds */
static inline int onlineReadable(Socket s, int ms) {
    fd_set r;
    struct timeval wait;
    wait.tv_sec = ms / 1000;
    wait.tv_usec = (ms % 1000) * 1000;
    FD_ZERO(&r);
    FD_SET(s, &r);
    return select((int)s + 1, &r, NULL, NULL, &wait) > 0;
}

#endif
