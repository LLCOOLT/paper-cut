/* Online co-op's direct UDP path (port/include/port/online_link.h): libjuice's
 * ICE agent finds a way between the two games (the network's addresses, the
 * public IPv4 one Cloudflare's STUN server sees, IPv6; both sides sending at
 * once through their routers), its lines passed through the relay's room.
 * Once it's there, the session's packets go over it:
 *
 *   'I' ack(u32: the first of the sender's frames it still needs from the other)
 *       first(u32) count(u8) size(u8) count x the frame's input (size bytes: one
 *       controller, or the host's with the seats', port/include/port/online.h)
 *       hashes(u8) hashes x frame(u32) fingerprint(u64)
 *   'P' / 'Q' a ping and its answer (8 bytes of time), for the input delay
 *
 * Each packet repeats every input the other side hasn't confirmed (up to 32
 * frames), so a lost one costs nothing: the next brings it. A side waiting
 * for the other sends its last packet again every 30 ms. */
#include "port/online_link.h"

#if defined(__EMSCRIPTEN__) || !defined(TTYD_BACKEND_AURORA)

int portIceStart(int controlling) {
    (void)controlling;
    return 0;
}
int portIceNextSignal(char* out, size_t cap) {
    (void)out;
    (void)cap;
    return 0;
}
void portIceRemoteSignal(const char* line) {
    (void)line;
}
int portIceState(void) {
    return PORT_ICE_OFF;
}
int portIceRoundTrip(void) {
    return -1;
}
void portIceSendInput(unsigned frame, const unsigned char* pad, size_t padBytes) {
    (void)frame;
    (void)pad;
    (void)padBytes;
}
int portIcePeerInput(unsigned frame, unsigned char* pad, size_t padBytes) {
    (void)frame;
    (void)pad;
    (void)padBytes;
    return 0;
}
void portIceSendHash(unsigned frame, unsigned long long hash) {
    (void)frame;
    (void)hash;
}
int portIcePeerHash(unsigned frame, unsigned long long* hash) {
    (void)frame;
    (void)hash;
    return 0;
}
int portIceSilentMs(void) {
    return -1;
}
void portIceSendBye(const char* why) {
    (void)why;
}
const char* portIceBye(void) {
    return NULL;
}
void portIceStop(void) {}
void portIceResume(unsigned peerNeeds, unsigned theirsNext) {
    (void)peerNeeds;
    (void)theirsNext;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <juice/juice.h>
#include "port/online.h"

#define RING 512
#define REPEAT 32      /* inputs at most in one packet */
#define INPUTS_BYTES (REPEAT * PORT_ONLINE_PAD_BYTES) /* fewer inputs when each is the host's with the seats' */
#define PACKET_MAX (1 + 10 + INPUTS_BYTES + 1 + 2 * 12)
#define SIGNALS 64
#define CONNECT_MS 8000 /* after the other side's last line: no path, the relay then */

static SDL_Mutex* sLock;
static juice_agent_t* sAgent;
static int sState = PORT_ICE_OFF, sRemoteDone;
static Uint64 sRemoteDoneAt, sLastHeard, sLastSent, sPingAt;
static int sRtt = -1;
static char* sSignals[SIGNALS];
static int sSignalHead, sSignalCount;

/* the session: this side's inputs, the other's, what each has */
static unsigned char sMine[RING][PORT_ONLINE_FRAME_BYTES];
static size_t sMineBytes = PORT_ONLINE_PAD_BYTES; /* each of this side's inputs */
static unsigned sMineNext;  /* frames 0..sMineNext-1 are known */
static unsigned sPeerNeeds; /* the first of ours the other doesn't have */
static struct {
    unsigned frame;
    int have;
    unsigned char pad[PORT_ONLINE_FRAME_BYTES];
} sTheirs[RING];
static unsigned sTheirsNext; /* the first of theirs we don't have */
static struct {
    unsigned frame;
    unsigned long long hash;
} sMyHash[2];
static struct {
    unsigned frame;
    unsigned long long hash;
    int have;
} sTheirHash[16];
static unsigned char sLastPacket[PACKET_MAX];
static size_t sLastPacketLen;
static char sBye[160]; /* the other's reason for leaving ('B'), when it came */
static int sHaveBye;

static void put32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
static unsigned get32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static void queueSignal(const char* kind, const char* text) {
    const size_t len = strlen(kind) + 1 + strlen(text) + 1;
    char* s = (char*)malloc(len);
    snprintf(s, len, "%s %s", kind, text);
    SDL_LockMutex(sLock);
    if (sSignalCount < SIGNALS) {
        sSignals[(sSignalHead + sSignalCount++) % SIGNALS] = s;
        s = NULL;
    }
    SDL_UnlockMutex(sLock);
    free(s);
}

/* --- libjuice's callbacks (its thread) --- */

static void onState(juice_agent_t* agent, juice_state_t state, void* user) {
    (void)agent;
    (void)user;
    SDL_LockMutex(sLock);
    if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED) {
        sState = PORT_ICE_OK;
    } else if (state == JUICE_STATE_FAILED) {
        sState = PORT_ICE_FAILED;
    }
    SDL_UnlockMutex(sLock);
    SDL_Log("online: direct path %s", juice_state_to_string(state));
}

static void onCandidate(juice_agent_t* agent, const char* sdp, void* user) {
    (void)agent;
    (void)user;
    {
        /* "a=candidate:<f> <c> UDP <prio> <address> <port> typ <kind>": the kind and the
           family only in the log (no addresses: logs go out with crash reports) */
        char address[64] = "", kind[16] = "";
        if (sscanf(sdp, "a=candidate:%*s %*s %*s %*s %63s %*s typ %15s", address, kind) == 2) {
            SDL_Log("online: an %s %s address (%s)", strchr(address, ':') != NULL ? "IPv6" : "IPv4", kind,
                    strcmp(kind, "srflx") == 0 ? "the router's public side, from the STUN server"
                                               : "this device's own");
        }
    }
    queueSignal("c", sdp);
}

static void onGatheringDone(juice_agent_t* agent, void* user) {
    (void)agent;
    (void)user;
    queueSignal("e", "");
}

/* every packet sealed (online_seal_pc.c): none goes unsealed (the path only
 * exists once the room's key does) */
static void sealedSend(juice_agent_t* agent, const unsigned char* data, size_t size) {
    unsigned char sealed[PACKET_MAX + PORT_SEAL_TAG + PORT_SEAL_COUNT + 16];
    size_t n;
    if (!portSealReady() || size + PORT_SEAL_TAG + PORT_SEAL_COUNT > sizeof(sealed)) {
        return;
    }
    n = portSeal(PORT_SEAL_PACKET, data, size, sealed);
    juice_send(agent, (const char*)sealed, n);
}

static void onRecv(juice_agent_t* agent, const char* sealed, size_t sealedSize, void* user) {
    unsigned char opened[PACKET_MAX + 160];
    const unsigned char* p = opened;
    const char* data = (const char*)opened;
    size_t size = 0;
    (void)user;
    /* one that doesn't open (changed, repeated, late, not the other game's) is dropped */
    if (sealedSize > sizeof(opened) + PORT_SEAL_TAG + PORT_SEAL_COUNT ||
        !portOpen(PORT_SEAL_PACKET, (const unsigned char*)sealed, sealedSize, opened, &size) || size < 1) {
        return;
    }
    SDL_LockMutex(sLock);
    sLastHeard = SDL_GetTicks();
    if (p[0] == 'P' && size >= 9) {
        char pong[9];
        memcpy(pong, data, 9);
        pong[0] = 'Q';
        SDL_UnlockMutex(sLock);
        sealedSend(agent, (const unsigned char*)pong, 9);
        return;
    }
    if (p[0] == 'B') {
        const size_t n = size - 1 < sizeof(sBye) - 1 ? size - 1 : sizeof(sBye) - 1;
        memcpy(sBye, p + 1, n);
        sBye[n] = '\0';
        sHaveBye = 1;
    } else if (p[0] == 'Q' && size >= 9) {
        Uint64 sent;
        memcpy(&sent, p + 1, 8);
        sRtt = (int)(SDL_GetTicks() - sent);
    } else if (p[0] == 'I' && size >= 11 && p[10] >= 1 && p[10] <= PORT_ONLINE_FRAME_BYTES) {
        const unsigned ack = get32(p + 1), first = get32(p + 5);
        const unsigned count = p[9];
        const size_t each = p[10];
        size_t at = 11;
        unsigned i;
        if (ack > sPeerNeeds) {
            sPeerNeeds = ack;
        }
        for (i = 0; i < count && at + each <= size; i++, at += each) {
            const unsigned f = first + i;
            if (f >= sTheirsNext && !(sTheirs[f % RING].have && sTheirs[f % RING].frame == f)) {
                sTheirs[f % RING].frame = f;
                sTheirs[f % RING].have = 1;
                memset(sTheirs[f % RING].pad, 0, sizeof(sTheirs[f % RING].pad));
                memcpy(sTheirs[f % RING].pad, p + at, each);
            }
        }
        while (sTheirs[sTheirsNext % RING].have && sTheirs[sTheirsNext % RING].frame == sTheirsNext) {
            sTheirsNext++;
        }
        if (at < size) {
            const unsigned hashes = p[at++];
            for (i = 0; i < hashes && at + 12 <= size; i++, at += 12) {
                const unsigned f = get32(p + at);
                unsigned long long h = get32(p + at + 4) | ((unsigned long long)get32(p + at + 8) << 32);
                const int slot = (f / PORT_ONLINE_HASH_EVERY) % 16;
                if (!(sTheirHash[slot].have && sTheirHash[slot].frame == f)) {
                    sTheirHash[slot].frame = f;
                    sTheirHash[slot].hash = h;
                    sTheirHash[slot].have = 1;
                }
            }
        }
    }
    SDL_UnlockMutex(sLock);
}

/* --- the lobby --- */

int portIceStart(int controlling) {
    juice_config_t config;
    char description[JUICE_MAX_SDP_STRING_LEN];
    (void)controlling; /* libjuice settles the roles itself */
    portIceStop();
    if (sLock == NULL) {
        sLock = SDL_CreateMutex();
    }
    memset(&config, 0, sizeof(config));
    config.stun_server_host = "stun.cloudflare.com";
    config.stun_server_port = 3478;
    config.cb_state_changed = onState;
    config.cb_candidate = onCandidate;
    config.cb_gathering_done = onGatheringDone;
    config.cb_recv = onRecv;
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);
    sAgent = juice_create(&config);
    if (sAgent == NULL) {
        sState = PORT_ICE_FAILED;
        return 0;
    }
    sState = PORT_ICE_TRYING;
    sRemoteDone = 0;
    sRtt = -1;
    sLastHeard = sLastSent = sPingAt = 0;
    sMineNext = sPeerNeeds = sTheirsNext = 0;
    sMineBytes = PORT_ONLINE_PAD_BYTES;
    memset(sTheirs, 0, sizeof(sTheirs));
    memset(sTheirHash, 0, sizeof(sTheirHash));
    memset(sMyHash, 0, sizeof(sMyHash));
    sLastPacketLen = 0;
    sHaveBye = 0;
    if (juice_get_local_description(sAgent, description, sizeof(description)) == 0) {
        queueSignal("d", description);
    }
    juice_gather_candidates(sAgent);
    return 1;
}

int portIceNextSignal(char* out, size_t cap) {
    char* s = NULL;
    if (sLock == NULL) {
        return 0;
    }
    SDL_LockMutex(sLock);
    if (sSignalCount > 0) {
        s = sSignals[sSignalHead];
        sSignalHead = (sSignalHead + 1) % SIGNALS;
        sSignalCount--;
    }
    SDL_UnlockMutex(sLock);
    if (s == NULL) {
        return 0;
    }
    snprintf(out, cap, "%s", s);
    free(s);
    return 1;
}

void portIceRemoteSignal(const char* line) {
    if (sAgent == NULL || line[0] == '\0' || line[1] != ' ') {
        return;
    }
    if (line[0] == 'd') {
        juice_set_remote_description(sAgent, line + 2);
    } else if (line[0] == 'c') {
        juice_add_remote_candidate(sAgent, line + 2);
    } else if (line[0] == 'e') {
        juice_set_remote_gathering_done(sAgent);
        sRemoteDone = 1;
        sRemoteDoneAt = SDL_GetTicks();
    }
}

int portIceState(void) {
    int s;
    if (sLock == NULL) {
        return PORT_ICE_OFF;
    }
    SDL_LockMutex(sLock);
    s = sState;
    if (s == PORT_ICE_TRYING && sRemoteDone && SDL_GetTicks() - sRemoteDoneAt > CONNECT_MS) {
        s = sState = PORT_ICE_FAILED; /* no way through: the relay carries it */
    }
    SDL_UnlockMutex(sLock);
    if (s == PORT_ICE_OK && SDL_GetTicks() - sPingAt > 500) { /* the round trip, for the delay */
        unsigned char ping[9];
        const Uint64 now = SDL_GetTicks();
        ping[0] = 'P';
        memcpy(ping + 1, &now, 8);
        sPingAt = now;
        sealedSend(sAgent, ping, sizeof(ping));
    }
    return s;
}

int portIceRoundTrip(void) {
    return sRtt;
}

/* --- the session --- */

static void sendPacket(void) {
    unsigned char* p = sLastPacket;
    unsigned first, count, i, repeat;
    size_t at;
    SDL_LockMutex(sLock);
    repeat = (unsigned)(INPUTS_BYTES / sMineBytes); /* (the packet's size kept the same) */
    first = sPeerNeeds;
    if (sMineNext > repeat && first < sMineNext - repeat) {
        first = sMineNext - repeat;
    }
    count = sMineNext > first ? sMineNext - first : 0;
    p[0] = 'I';
    put32(p + 1, sTheirsNext);
    put32(p + 5, first);
    p[9] = (unsigned char)count;
    p[10] = (unsigned char)sMineBytes;
    at = 11;
    for (i = 0; i < count; i++, at += sMineBytes) {
        memcpy(p + at, sMine[(first + i) % RING], sMineBytes);
    }
    p[at++] = 2;
    for (i = 0; i < 2; i++, at += 12) {
        put32(p + at, sMyHash[i].frame);
        put32(p + at + 4, (unsigned)sMyHash[i].hash);
        put32(p + at + 8, (unsigned)(sMyHash[i].hash >> 32));
    }
    sLastPacketLen = at;
    sLastSent = SDL_GetTicks();
    SDL_UnlockMutex(sLock);
    sealedSend(sAgent, sLastPacket, sLastPacketLen);
}

void portIceSendInput(unsigned frame, const unsigned char* pad, size_t padBytes) {
    if (sAgent == NULL || padBytes < 1 || padBytes > PORT_ONLINE_FRAME_BYTES) {
        return;
    }
    SDL_LockMutex(sLock);
    sMineBytes = padBytes; /* (the same all session) */
    memcpy(sMine[frame % RING], pad, padBytes);
    if (frame + 1 > sMineNext) {
        sMineNext = frame + 1;
    }
    SDL_UnlockMutex(sLock);
    sendPacket();
}

int portIcePeerInput(unsigned frame, unsigned char* pad, size_t padBytes) {
    int have;
    if (sAgent == NULL) {
        return 0;
    }
    SDL_LockMutex(sLock);
    have = sTheirs[frame % RING].have && sTheirs[frame % RING].frame == frame;
    if (have) {
        memcpy(pad, sTheirs[frame % RING].pad, padBytes < PORT_ONLINE_FRAME_BYTES ? padBytes : PORT_ONLINE_FRAME_BYTES);
    }
    SDL_UnlockMutex(sLock);
    if (!have && sLastPacketLen > 0 && SDL_GetTicks() - sLastSent > 30) {
        sendPacket(); /* waiting: ours again, in case the last was lost */
    }
    return have;
}

void portIceSendHash(unsigned frame, unsigned long long hash) {
    if (sAgent == NULL) {
        return;
    }
    SDL_LockMutex(sLock); /* the two latest go in every packet */
    sMyHash[0] = sMyHash[1];
    sMyHash[1].frame = frame;
    sMyHash[1].hash = hash;
    SDL_UnlockMutex(sLock);
}

int portIcePeerHash(unsigned frame, unsigned long long* hash) {
    const int slot = (frame / PORT_ONLINE_HASH_EVERY) % 16;
    int have;
    if (sLock == NULL) {
        return 0;
    }
    SDL_LockMutex(sLock);
    have = sTheirHash[slot].have && sTheirHash[slot].frame == frame;
    if (have) {
        *hash = sTheirHash[slot].hash;
        sTheirHash[slot].have = 0;
    }
    SDL_UnlockMutex(sLock);
    return have;
}

int portIceSilentMs(void) {
    return sLastHeard == 0 ? -1 : (int)(SDL_GetTicks() - sLastHeard);
}

void portIceSendBye(const char* why) {
    char m[160];
    int i;
    size_t n;
    if (sAgent == NULL) {
        return;
    }
    m[0] = 'B';
    n = strlen(why) < sizeof(m) - 1 ? strlen(why) : sizeof(m) - 1;
    memcpy(m + 1, why, n);
    for (i = 0; i < 3; i++) { /* UDP: a few times, so it gets there */
        sealedSend(sAgent, (const unsigned char*)m, n + 1);
    }
}

const char* portIceBye(void) {
    const char* why = NULL;
    if (sLock == NULL) {
        return NULL;
    }
    SDL_LockMutex(sLock);
    if (sHaveBye) {
        why = sBye[0] != '\0' ? sBye : "The other player left.";
    }
    SDL_UnlockMutex(sLock);
    return why;
}

void portIceResume(unsigned peerNeeds, unsigned theirsNext) {
    if (sLock == NULL) {
        return;
    }
    SDL_LockMutex(sLock);
    sPeerNeeds = peerNeeds;
    if (theirsNext > sTheirsNext) {
        sTheirsNext = theirsNext;
    }
    SDL_UnlockMutex(sLock);
}

void portIceStop(void) {
    if (sAgent != NULL) {
        juice_destroy(sAgent); /* waits for its thread: no callback after */
        sAgent = NULL;
    }
    if (sLock != NULL) {
        SDL_LockMutex(sLock);
        while (sSignalCount > 0) {
            free(sSignals[sSignalHead]);
            sSignalHead = (sSignalHead + 1) % SIGNALS;
            sSignalCount--;
        }
        sState = PORT_ICE_OFF;
        SDL_UnlockMutex(sLock);
    }
}

#endif
