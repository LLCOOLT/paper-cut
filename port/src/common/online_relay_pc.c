/* The relays and this computer's address (port/include/port/online.h): what
 * sync what matters, the boards, notes and the save transfer use. Only the
 * host's sockets here, no game headers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef select /* port_prefix.h's rename for the game (tik_05.c): this is the host's */
#include "port/online.h"

#if defined(__EMSCRIPTEN__) && !defined(PORT_WEB_ONLINE)
/* no relay or address: a web build without online play */

int portOnlineLocalAddress(char* out, size_t size) {
    (void)size;
    out[0] = '\0';
    return 0;
}
const char* portOnlineRelay(void) {
    return "";
}
const char* portOnlineRelayAt(int i) {
    (void)i;
    return NULL;
}

#else

#ifndef __EMSCRIPTEN__
#include "port/online_sock.h"
#endif

/* The relays, in order (the build's TTYD_ONLINE_RELAY, or $TTYD_ONLINE_RELAY:
 * addresses separated by spaces). Each is a free Cloudflare account's, with
 * its own daily allowance: a host whose relay is full for the day takes the
 * next. A room's code says which: its first character, the relay's place in
 * PORT_ONLINE_CODE_CHARS (both games have the same list: the same version). */
#define MAX_RELAYS 8
static char sRelays[MAX_RELAYS][256];
static int sRelayCount = -1;

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

#ifdef __EMSCRIPTEN__
/* the browser: no address to give (it takes no connections: rooms only) */
int portOnlineLocalAddress(char* out, size_t size) {
    (void)size;
    out[0] = '\0';
    return 0;
}
#else
/* this computer's address on its network, for the host to tell the guest: the
 * one a connection outwards would use (a UDP socket connected, nothing sent) */
int portOnlineLocalAddress(char* out, size_t size) {
    struct sockaddr_in to, me;
    socklen_t len = sizeof(me);
    Socket s;
    out[0] = '\0';
    if (!onlineNetStart() || (s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == BAD_SOCKET) {
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
#endif /* __EMSCRIPTEN__ */

#endif
