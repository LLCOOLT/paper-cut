/* Online play's direct connection between two games of a room (port/include/port/online_ice.h): ICE
 * with libjuice (MPL-2.0), the relay as the way the offer, the answer and the candidates go and as
 * the way back; every packet sealed (online_seal_pc.c, PORT_SEAL_PACKET).
 *
 * A try, per link (the host's guest and players 3 and 4; a player's host):
 * - the host, a few seconds after the link's hellos (and later again, later and later, after a failed
 *   or a lost try), makes an agent (controlling), sends its description (OFFER) and its candidates as
 *   it finds them (CAND, then DONE);
 * - the player, with the setting on, makes its own (controlled) from the offer and answers (ANSWER,
 *   CAND, DONE); with it off, NO (the host asks again after two minutes: the setting may change);
 * - when ICE says connected, each side sends probes (every 100 ms, then every 250 ms: the round trip
 *   and keeping the NATs' mappings); the first sealed packet that opens makes the way up on that side,
 *   and from then on its messages go direct;
 * - no packet from the other game for 0.6 s: everything goes both ways (direct and by the relay) until
 *   one comes; for 2 s: the try is over (DOWN to the other), the messages are the relay's again at
 *   once, what wasn't acknowledged is sent again there (the reliable layer's numbers keep it in order
 *   and once), and the host tries again later;
 * - not up within 15 s, or ICE failed: over too, nothing lost (it was never used).
 * libjuice calls back on a thread of its own: the packets and candidates wait in a locked queue for the
 * game's pump; nothing here is ever called from that thread but the four callbacks. */
#include "port/online_ice.h"
#include "port/online_link.h"
#include "port/settings.h"
#include "port/statesync.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WIRE_SIZE(PortIceSig, 8);

int portIceWanted(void) {
#if defined(PORT_DIRECT)
    static int* sOn;
    const char* env = getenv("TTYD_SYNC_DIRECT");
    if (sOn == NULL) {
        sOn = portSettingsDeclare("online_direct", 0, 0, 1);
    }
    if (env != NULL && *env != '\0' && strcmp(env, "auto") != 0) {
        return strcmp(env, "force") == 0 || strcmp(env, "on") == 0 || strcmp(env, "1") == 0;
    }
    return sOn != NULL && *sOn != 0;
#else
    return 0;
#endif
}

int portIceAvailable(void) {
#if defined(PORT_DIRECT)
    return 1;
#else
    return 0;
#endif
}

#if !defined(PORT_DIRECT)

void portIceBegin(int host, int sealed, const PortIceHooks* hooks) {
    (void)host;
    (void)sealed;
    (void)hooks;
}
void portIceEnd(void) {}
void portIceTick(int link, int player, int linked, unsigned frame, unsigned long long now) {
    (void)link;
    (void)player;
    (void)linked;
    (void)frame;
    (void)now;
}
void portIceSignalIn(int link, const uint8_t* p, size_t n) {
    (void)link;
    (void)p;
    (void)n;
}
void portIceStop(int link) {
    (void)link;
}
int portIceUp(int link) {
    (void)link;
    return 0;
}
int portIceDual(int link) {
    (void)link;
    return 0;
}
int portIceSend(int link, const void* msg, size_t len) {
    (void)link;
    (void)msg;
    (void)len;
    return 0;
}
void portIceFlush(void) {}
int portIcePending(int link) {
    (void)link;
    return 0;
}
void portIceFlushLink(int link) {
    (void)link;
}
int portIceRtoMs(int link) {
    (void)link;
    return 300;
}

#else

#include <juice/juice.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static SRWLOCK sLock = SRWLOCK_INIT;
#define LOCK() AcquireSRWLockExclusive(&sLock)
#define UNLOCK() ReleaseSRWLockExclusive(&sLock)
#else
#include <pthread.h>
static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&sLock)
#define UNLOCK() pthread_mutex_unlock(&sLock)
#endif

#define DGRAM_MAX 1500                   /* a packet taken at most (ours are PORT_ICE_MTU + 25) */
#define RING 128                          /* packets waiting for the game's pump, per link */
#define CANDS 24                          /* candidates found and not yet sent, per link */
#define FRAG_DATA (PORT_ICE_MTU - 9)      /* a piece's bytes: kind, id, index, count in front */
#define FRAG_SLOTS 4                      /* long messages coming in pieces at once, per link */
#define FRAG_MS 3000                      /* a long message's pieces wait this long for the rest */
#define DELAYED 128                       /* the faults' delayed packets, per link */
#define TRY_MS 15000ull                   /* a try not up in this long is over */
#define SHAKY_MS 600ull                   /* nothing direct this long: both ways */
#define QUIET_MS 2000ull                  /* nothing direct this long: back on the relay */
#define FIRST_TRY_MS 3000ull              /* after the hellos, before the first try (the join's burst first) */
#define PROBE_TRYING_MS 100ull
#define PROBE_UP_MS 250ull

enum { K_MSGS = 1, K_FRAG = 2, K_PROBE = 3 };
enum { IDLE, TRYING, UP };

typedef struct Dgram {
    uint16_t len;
    uint8_t d[DGRAM_MAX];
} Dgram;

typedef struct Frag {
    uint32_t id; /* 0 free */
    uint16_t count;
    uint32_t have; /* pieces in (bit per index) */
    size_t lastLen;
    uint64_t newest; /* the newest packet count among its pieces */
    unsigned long long at;
    uint8_t buf[((PORT_ICE_BIG + 5 + FRAG_DATA - 1) / FRAG_DATA) * FRAG_DATA];
} Frag;

typedef struct Delayed {
    unsigned long long due;
    uint16_t len;
    uint8_t d[PORT_ICE_MTU + 64];
} Delayed;

typedef struct Slot {
    juice_agent_t* agent;
    int phase;
    int player;
    uint32_t gen;  /* this try's (the host counts them) */
    unsigned epoch; /* changes whenever the agent goes (a deliver that ended it is seen) */
    int remoteSet;  /* the other's description taken */
    int doneSent;
    int heard;      /* a packet of this try opened */
    unsigned long long startedAt, upAt, lastRx, lastProbe, nextTry, linkedAt;
    int linked, fails, peerOff;
    /* the round trip, from the probes (RFC 6298's smoothing) */
    int hasRtt;
    double srtt, rttvar;
    uint32_t peerT; /* the other's last probe's clock, and when it came */
    unsigned long long peerTAt;
    uint32_t peerRxAge; /* what the other last said: ms since it heard from this side */
    unsigned long long peerRxAgeAt;
    /* in: packets the callback queued; replays (unsealed: the tests' window), types' newest */
    Dgram* ring;
    int ringHead, ringN;
    unsigned ringDropped;
    uint64_t rxTop, rxSeen; /* (unsealed only: as online_seal_pc.c's window) */
    uint64_t typeNewest[256];
    Frag* frag;
    /* candidates found (the callback's), its state */
    char cand[CANDS][JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    int candN, gathered, jstate;
    /* out: this pump's messages, packed; the unsealed count; the pieces' id */
    uint8_t tx[PORT_ICE_MTU];
    size_t txLen;
    uint64_t txCount;
    uint32_t fragId;
    Delayed* delayed;
    int delayedN;
    /* counts, for the log */
    unsigned tries, ups, fallbacks, pktOut, pktIn, pktBad;
} Slot;

static struct {
    int on, host, sealed;
    PortIceHooks hooks;
    unsigned frame;
    Slot slot[PORT_ICE_LINKS];
    char stunHost[128];
    uint16_t stunPort;
    char bind[64];
    /* the tests' faults ($TTYD_SYNC_UDP_FAULTS) */
    int dropPct, dupPct, delayMin, delayMax, noCand;
    long bhFrom, bhTo;
    uint64_t rng;
} I;

static void say(const char* fmt, ...) {
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (I.hooks.log != NULL) {
        I.hooks.log(line);
    }
}

static Slot* slotAt(int link) {
    return I.on && link >= 0 && link < PORT_ICE_LINKS ? &I.slot[link] : NULL;
}

static uint32_t rnd(void) {
    I.rng ^= I.rng << 13;
    I.rng ^= I.rng >> 7;
    I.rng ^= I.rng << 17;
    return (uint32_t)(I.rng >> 11);
}

static void put64(uint8_t* p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint64_t get64(const uint8_t* p) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }
    return v;
}

static void put32(uint8_t* p, uint32_t v) {
    memcpy(p, &v, 4); /* (little-endian on every platform played on, as the rest of the wire) */
}

static uint32_t get32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* ---- libjuice's thread: these four only, under the lock ---- */

static void onState(juice_agent_t* a, juice_state_t state, void* user) {
    Slot* s = (Slot*)user;
    (void)a;
    LOCK();
    s->jstate = (int)state;
    UNLOCK();
}

static void onCandidate(juice_agent_t* a, const char* sdp, void* user) {
    Slot* s = (Slot*)user;
    (void)a;
    LOCK();
    if (s->candN < CANDS) {
        snprintf(s->cand[s->candN++], sizeof(s->cand[0]), "%s", sdp);
    }
    UNLOCK();
}

static void onGathered(juice_agent_t* a, void* user) {
    Slot* s = (Slot*)user;
    (void)a;
    LOCK();
    s->gathered = 1;
    UNLOCK();
}

static void onRecv(juice_agent_t* a, const char* data, size_t size, void* user) {
    Slot* s = (Slot*)user;
    (void)a;
    if (size == 0 || size > DGRAM_MAX) {
        return;
    }
    LOCK();
    if (s->ring != NULL && s->ringN < RING) {
        Dgram* d = &s->ring[(s->ringHead + s->ringN++) % RING];
        d->len = (uint16_t)size;
        memcpy(d->d, data, size);
    } else {
        s->ringDropped++; /* (the game's pump stopped a while: as lost on the way) */
    }
    UNLOCK();
}

static void juiceLog(juice_log_level_t level, const char* message) {
    (void)level;
    printf("[sync] juice: %s\n", message);
    fflush(stdout);
}

/* ---- the faults (tests) ---- */

static void readFaults(void) {
    const char* v = getenv("TTYD_SYNC_UDP_FAULTS");
    const char* p = v;
    I.dropPct = I.dupPct = I.delayMin = I.delayMax = I.noCand = 0;
    I.bhFrom = I.bhTo = -1;
    while (p != NULL && *p != '\0') {
        if (strncmp(p, "drop:", 5) == 0) {
            I.dropPct = atoi(p + 5);
        } else if (strncmp(p, "dup:", 4) == 0) {
            I.dupPct = atoi(p + 4);
        } else if (strncmp(p, "delay:", 6) == 0) {
            const char* dash = strchr(p + 6, '-');
            I.delayMin = atoi(p + 6);
            I.delayMax = dash != NULL && (strchr(p + 6, ',') == NULL || dash < strchr(p + 6, ',')) ? atoi(dash + 1)
                                                                                                   : I.delayMin;
        } else if (strncmp(p, "blackhole_at:", 13) == 0) {
            const char* dash = strchr(p + 13, '-');
            I.bhFrom = atol(p + 13);
            I.bhTo = dash != NULL && (strchr(p + 13, ',') == NULL || dash < strchr(p + 13, ',')) ? atol(dash + 1)
                                                                                                 : 0x7FFFFFFFL;
        } else if (strncmp(p, "nocand", 6) == 0) {
            I.noCand = 1;
        }
        p = strchr(p, ',');
        if (p != NULL) {
            p++;
        }
    }
    if (v != NULL && *v != '\0') {
        say("net: direct: test faults: %s", v);
    }
}

static int blackholed(void) {
    return I.bhFrom >= 0 && (long)I.frame >= I.bhFrom && (long)I.frame < I.bhTo;
}

static void juiceOut(Slot* s, const uint8_t* p, size_t n) {
    if (s->agent != NULL && juice_send(s->agent, (const char*)p, n) == JUICE_ERR_SUCCESS) {
        s->pktOut++;
    }
}

/* a sealed packet onto the wire, through the faults */
static void faultsOut(Slot* s, const uint8_t* p, size_t n, unsigned long long now) {
    int copies = 1, c;
    if (blackholed() || (I.dropPct > 0 && (int)(rnd() % 100) < I.dropPct)) {
        return;
    }
    if (I.dupPct > 0 && (int)(rnd() % 100) < I.dupPct) {
        copies = 2;
    }
    for (c = 0; c < copies; c++) {
        if (I.delayMax > 0 && s->delayed != NULL && s->delayedN < DELAYED && n <= sizeof(s->delayed[0].d)) {
            Delayed* d = &s->delayed[s->delayedN++];
            const int span = I.delayMax - I.delayMin;
            d->due = now + (unsigned long long)(I.delayMin + (span > 0 ? (int)(rnd() % (unsigned)(span + 1)) : 0));
            d->len = (uint16_t)n;
            memcpy(d->d, p, n);
        } else {
            juiceOut(s, p, n);
        }
    }
}

static void delayedOut(Slot* s, unsigned long long now) {
    int i = 0;
    while (i < s->delayedN) {
        if (s->delayed[i].due <= now) {
            juiceOut(s, s->delayed[i].d, s->delayed[i].len);
            s->delayed[i] = s->delayed[--s->delayedN]; /* (any order: they reorder anyway) */
        } else {
            i++;
        }
    }
}

/* ---- packets ---- */

static int linkOfSlot(const Slot* s) {
    return (int)(s - I.slot);
}

/* one packet's plain bytes sealed and sent */
static void wire(Slot* s, const uint8_t* plain, size_t n) {
    uint8_t pkt[PORT_ICE_MTU + PORT_SEAL_COUNT + PORT_SEAL_TAG + 16];
    size_t m;
    if (s->agent == NULL || n == 0 || n > PORT_ICE_MTU) {
        return;
    }
    if (I.sealed) {
        m = portSealAt(linkOfSlot(s), PORT_SEAL_PACKET, plain, n, pkt);
        if (m == 0) {
            return;
        }
    } else {
        put64(pkt, s->txCount++); /* (the stand-in relay's tests: unsealed, counted as sealed ones are) */
        memcpy(pkt + 8, plain, n);
        m = n + 8;
    }
    faultsOut(s, pkt, m, ssNetMs());
}

static void txFlush(Slot* s) {
    if (s->txLen > 1) {
        wire(s, s->tx, s->txLen);
    }
    s->txLen = 0;
}

static void probe(Slot* s, unsigned long long now) {
    uint8_t b[17];
    const uint32_t t = (uint32_t)now | 1u;
    b[0] = K_PROBE;
    put32(b + 1, t);
    put32(b + 5, s->peerT);
    put32(b + 9, s->peerT != 0 ? (uint32_t)(now - s->peerTAt) : 0);
    put32(b + 13, s->heard ? (uint32_t)(now - s->lastRx < 60000 ? now - s->lastRx : 60000) : 60000u);
    s->lastProbe = now;
    wire(s, b, sizeof(b));
}

static void rttSample(Slot* s, double r) {
    if (!s->hasRtt) {
        s->srtt = r;
        s->rttvar = r / 2;
        s->hasRtt = 1;
        return;
    }
    s->rttvar = 0.75 * s->rttvar + 0.25 * (s->srtt > r ? s->srtt - r : r - s->srtt);
    s->srtt = 0.875 * s->srtt + 0.125 * r;
}

/* unreliable messages: the latest of each type wins (a late one, older than one taken, is let go: UDP
   reorders); the reliable layer's own and the pings always pass (its numbers order them) */
static int lateUnreliable(Slot* s, int type, uint64_t count) {
    if (type == SS_MSG_REL || type == SS_MSG_ACK || type == SS_MSG_HELLO || type == SS_MSG_PING) {
        return 0;
    }
    if (count < s->typeNewest[type & 255]) {
        return 1;
    }
    s->typeNewest[type & 255] = count;
    return 0;
}

/* a whole message in (its [u32 length][u8 type] first); 0 when the slot ended meanwhile */
static int message(Slot* s, const uint8_t* m, size_t n, uint64_t count) {
    const unsigned epoch = s->epoch;
    uint32_t len;
    if (n < 5) {
        return 1;
    }
    len = get32(m);
    if (len == 0 || len != n - 4) {
        s->pktBad++;
        return 1;
    }
    if (lateUnreliable(s, m[4], count)) {
        return 1;
    }
    if (I.hooks.deliver != NULL) {
        I.hooks.deliver(linkOfSlot(s), m[4], m + 5, len - 1);
    }
    return s->epoch == epoch;
}

static void fragIn(Slot* s, const uint8_t* p, size_t n, uint64_t count, unsigned long long now) {
    uint32_t id;
    uint16_t idx, cnt;
    Frag* f = NULL;
    int i, oldest = 0;
    if (n < 9 || s->frag == NULL) {
        return;
    }
    id = get32(p + 1);
    memcpy(&idx, p + 5, 2);
    memcpy(&cnt, p + 7, 2);
    p += 9;
    n -= 9;
    if (id == 0 || cnt < 2 || cnt > 32 || idx >= cnt || n > FRAG_DATA || (idx + 1 < cnt && n != FRAG_DATA) ||
        (size_t)cnt * FRAG_DATA > sizeof(s->frag[0].buf)) {
        s->pktBad++;
        return;
    }
    for (i = 0; i < FRAG_SLOTS; i++) {
        Frag* g = &s->frag[i];
        if (g->id != 0 && now - g->at > FRAG_MS) {
            g->id = 0; /* (its rest lost: the reliable layer sends it again, an unreliable one is newer by now) */
        }
        if (g->id == id) {
            f = g;
        }
    }
    if (f == NULL) {
        /* a free place, or the one waiting the longest */
        for (i = 0; i < FRAG_SLOTS; i++) {
            if (s->frag[i].id == 0) {
                oldest = i;
                break;
            }
            if (s->frag[i].at < s->frag[oldest].at) {
                oldest = i;
            }
        }
        f = &s->frag[oldest];
        f->id = id;
        f->count = cnt;
        f->have = 0;
        f->lastLen = 0;
        f->newest = 0;
        f->at = now;
    }
    if (f->count != cnt || (f->have >> idx & 1u)) {
        return;
    }
    memcpy(f->buf + (size_t)idx * FRAG_DATA, p, n);
    f->have |= 1u << idx;
    if (idx + 1 == cnt) {
        f->lastLen = n;
    }
    if (count > f->newest) {
        f->newest = count;
    }
    if (f->have == (cnt == 32 ? 0xFFFFFFFFu : (1u << cnt) - 1u)) {
        const size_t total = (size_t)(cnt - 1) * FRAG_DATA + f->lastLen;
        f->id = 0;
        message(s, f->buf, total, f->newest);
    }
}

/* the tests' stand-in (unsealed): online_seal_pc.c's window, here */
static int unsealedFresh(Slot* s, uint64_t count) {
    if (count < s->rxTop) {
        const uint64_t back = s->rxTop - 1 - count;
        if (back >= PORT_SEAL_WINDOW || (s->rxSeen >> back & 1ull)) {
            return 0;
        }
        s->rxSeen |= 1ull << back;
        return 1;
    }
    {
        const uint64_t up = count + 1 - s->rxTop;
        s->rxSeen = (up >= PORT_SEAL_WINDOW ? 0ull : s->rxSeen << up) | 1ull;
        s->rxTop = count + 1;
    }
    return 1;
}

static void packetIn(Slot* s, const uint8_t* d, size_t n, unsigned long long now) {
    uint8_t plain[DGRAM_MAX];
    size_t m = 0, at;
    uint64_t count;
    if (blackholed() || n < 8 + 1) {
        return;
    }
    count = get64(d);
    if (I.sealed) {
        if (n > sizeof(plain) || !portOpenAt(linkOfSlot(s), PORT_SEAL_PACKET, d, n, plain, &m) || m == 0) {
            s->pktBad++; /* (changed on the way, a repeat, another key: never taken) */
            return;
        }
    } else {
        if (!unsealedFresh(s, count)) {
            return;
        }
        m = n - 8;
        memcpy(plain, d + 8, m);
    }
    s->pktIn++;
    s->heard = 1;
    s->lastRx = now;
    switch (plain[0]) {
        case K_MSGS:
            at = 1;
            while (m - at >= 5) {
                const uint32_t len = get32(plain + at);
                if (len == 0 || len > m - at - 4) {
                    s->pktBad++;
                    break;
                }
                if (!message(s, plain + at, len + 4, count)) {
                    return;
                }
                at += 4 + len;
            }
            break;
        case K_FRAG:
            fragIn(s, plain, m, count, now);
            break;
        case K_PROBE:
            if (m >= 17) {
                const uint32_t t = get32(plain + 1), echo = get32(plain + 5), held = get32(plain + 9);
                s->peerT = t;
                s->peerTAt = now;
                s->peerRxAge = get32(plain + 13);
                s->peerRxAgeAt = now;
                if (echo != 0) {
                    const uint32_t r = (uint32_t)now - echo - held;
                    if (r < 10000u) {
                        rttSample(s, (double)r);
                    }
                }
            }
            break;
        default:
            s->pktBad++;
            break;
    }
}

/* ---- tries ---- */

static void sig(int link, int op, uint32_t gen, const char* text) {
    uint8_t buf[sizeof(PortIceSig) + JUICE_MAX_SDP_STRING_LEN];
    PortIceSig h;
    const size_t n = text != NULL ? strlen(text) : 0;
    if (I.hooks.signal == NULL || n > JUICE_MAX_SDP_STRING_LEN) {
        return;
    }
    memset(&h, 0, sizeof(h));
    h.op = (uint8_t)op;
    h.gen = gen;
    memcpy(buf, &h, sizeof(h));
    if (n > 0) {
        memcpy(buf + sizeof(h), text, n);
    }
    I.hooks.signal(link, buf, sizeof(h) + n);
}

/* the agent let go (never under the lock: libjuice waits for its thread, which may be waiting for it) */
static void dropAgent(Slot* s) {
    juice_agent_t* a = s->agent;
    s->agent = NULL;
    s->epoch++;
    if (a != NULL) {
        juice_destroy(a);
    }
    LOCK();
    s->ringN = s->ringHead = 0;
    s->candN = s->gathered = s->jstate = 0;
    UNLOCK();
    s->phase = IDLE;
    s->remoteSet = s->doneSent = s->heard = 0;
    s->txLen = 0;
    s->delayedN = 0;
    if (s->frag != NULL) {
        int i;
        for (i = 0; i < FRAG_SLOTS; i++) {
            s->frag[i].id = 0;
        }
    }
}

static int makeAgent(Slot* s) {
    juice_config_t c;
    if (s->ring == NULL) {
        s->ring = (Dgram*)malloc(sizeof(Dgram) * RING);
        s->frag = (Frag*)calloc(FRAG_SLOTS, sizeof(Frag));
        s->delayed = (Delayed*)malloc(sizeof(Delayed) * DELAYED);
        if (s->ring == NULL || s->frag == NULL || s->delayed == NULL) {
            free(s->ring);
            free(s->frag);
            free(s->delayed);
            s->ring = NULL;
            s->frag = NULL;
            s->delayed = NULL;
            return 0;
        }
    }
    memset(&c, 0, sizeof(c));
    c.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;
    c.stun_server_host = I.stunHost[0] != '\0' ? I.stunHost : NULL;
    c.stun_server_port = I.stunPort;
    c.bind_address = I.bind[0] != '\0' ? I.bind : NULL;
    c.cb_state_changed = onState;
    c.cb_candidate = onCandidate;
    c.cb_gathering_done = onGathered;
    c.cb_recv = onRecv;
    c.user_ptr = s;
    LOCK();
    s->ringN = s->ringHead = 0;
    s->candN = s->gathered = s->jstate = 0;
    UNLOCK();
    s->agent = juice_create(&c);
    s->epoch++;
    s->heard = s->remoteSet = s->doneSent = 0;
    s->hasRtt = 0;
    s->peerT = 0;
    s->peerRxAge = 0;
    s->peerRxAgeAt = 0;
    return s->agent != NULL;
}

static unsigned long long backoff(int fails) {
    return fails <= 1 ? 20000ull : fails == 2 ? 60000ull : fails == 3 ? 120000ull : 300000ull;
}

/* this try over: the relay's again (what wasn't acknowledged sent there, when the way was in use) */
static void endTry(Slot* s, const char* why, int tell, unsigned long long now) {
    const int link = linkOfSlot(s);
    const int wasUp = s->phase == UP;
    const uint32_t gen = s->gen;
    if (s->phase == IDLE) {
        return;
    }
    dropAgent(s); /* (first: what goes from here on goes by the relay, the DOWN too) */
    if (wasUp) {
        s->fallbacks++;
        if (now - s->upAt > 60000ull) {
            s->fails = 0; /* (it worked a good while: the next try soon) */
        }
        say("net: the direct connection to player %d went (%s): back on the relay, nothing lost", s->player, why);
        if (I.hooks.fellBack != NULL) {
            I.hooks.fellBack(link);
        }
    } else {
        say("net: no direct connection to player %d (%s): the relay carries the game", s->player, why);
    }
    if (tell) {
        sig(link, PORT_ICE_DOWN, gen, NULL);
    }
    s->fails++;
    s->nextTry = now + backoff(s->fails);
}

/* the candidates libjuice found since the last pump, to the other game */
static void sendFound(Slot* s, int link) {
    char found[CANDS][JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    int n, i, done;
    LOCK();
    n = s->candN;
    memcpy(found, s->cand, sizeof(found[0]) * (size_t)n);
    s->candN = 0;
    done = s->gathered && !s->doneSent;
    UNLOCK();
    for (i = 0; i < n; i++) {
        sig(link, PORT_ICE_CAND, s->gen, found[i]);
    }
    if (done) {
        s->doneSent = 1;
        sig(link, PORT_ICE_DONE, s->gen, NULL);
    }
}

/* a candidate line's type ("host", "srflx", "prflx", "relay"), for the log (never the address) */
static void typeOf(const char* line, char* out, size_t cap) {
    const char* t = strstr(line, " typ ");
    size_t n = 0;
    if (t == NULL) {
        snprintf(out, cap, "?");
        return;
    }
    t += 5;
    while (t[n] != '\0' && t[n] != ' ' && t[n] != '\r' && t[n] != '\n' && n + 1 < cap) {
        n++;
    }
    snprintf(out, cap, "%.*s", (int)n, t);
}

static void startTry(Slot* s, int link, unsigned long long now) {
    char sdp[JUICE_MAX_SDP_STRING_LEN];
    if (!makeAgent(s)) {
        s->fails++;
        s->nextTry = now + backoff(s->fails);
        return;
    }
    s->gen++;
    if (s->gen == 0) {
        s->gen = 1;
    }
    s->tries++;
    if (juice_get_local_description(s->agent, sdp, sizeof(sdp)) != JUICE_ERR_SUCCESS) {
        dropAgent(s);
        s->fails++;
        s->nextTry = now + backoff(s->fails);
        return;
    }
    s->phase = TRYING;
    s->startedAt = now;
    say("net: trying a direct connection to player %d (try %u)", s->player, s->tries);
    sig(link, PORT_ICE_OFFER, s->gen, sdp);
    juice_gather_candidates(s->agent);
}

void portIceBegin(int host, int sealed, const PortIceHooks* hooks) {
    const char* stun = getenv("TTYD_SYNC_STUN");
    const char* bind = getenv("TTYD_SYNC_UDP_BIND");
    const char* seed = getenv("TTYD_SYNC_UDP_SEED");
    int i;
    if (I.on) {
        portIceEnd();
    }
    memset(&I, 0, sizeof(I));
    I.on = 1;
    I.host = host;
    I.sealed = sealed;
    if (hooks != NULL) {
        I.hooks = *hooks;
    }
    /* Cloudflare's public STUN (free and unlimited, by its terms: its Realtime TURN FAQ); no TURN, the relay
       is the way otherwise */
    snprintf(I.stunHost, sizeof(I.stunHost), "%s", "stun.cloudflare.com");
    I.stunPort = 3478;
    if (stun != NULL && *stun != '\0') {
        const char* colon = strrchr(stun, ':');
        if (strcmp(stun, "none") == 0) {
            I.stunHost[0] = '\0';
        } else if (colon != NULL) {
            snprintf(I.stunHost, sizeof(I.stunHost), "%.*s", (int)(colon - stun), stun);
            I.stunPort = (uint16_t)atoi(colon + 1);
        } else {
            snprintf(I.stunHost, sizeof(I.stunHost), "%s", stun);
        }
    }
    snprintf(I.bind, sizeof(I.bind), "%s", bind != NULL ? bind : "");
    I.rng = (seed != NULL ? strtoull(seed, NULL, 10) : 0x9E3779B97F4A7C15ull) ^ (host ? 0x51ull : 0xA7ull);
    if (I.rng == 0) {
        I.rng = 1;
    }
    for (i = 0; i < PORT_ICE_LINKS; i++) {
        I.slot[i].player = i + 2;
    }
    readFaults();
    juice_set_log_handler(juiceLog);
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);
    if (portIceWanted()) {
        say("net: direct connections: on (%s; STUN %s)", host ? "offered to each player" : "taken when offered",
            I.stunHost[0] != '\0' ? I.stunHost : "none");
    }
}

void portIceEnd(void) {
    int i;
    if (!I.on) {
        return;
    }
    for (i = 0; i < PORT_ICE_LINKS; i++) {
        Slot* s = &I.slot[i];
        if (s->tries > 0 || s->ups > 0 || s->pktIn > 0) {
            say("net: direct with player %d: %u tries, up %u times, %u fallbacks, packets %u out %u in %u bad "
                "%u dropped, round trip %d ms",
                s->player, s->tries, s->ups, s->fallbacks, s->pktOut, s->pktIn, s->pktBad, s->ringDropped,
                s->hasRtt ? (int)s->srtt : -1);
        }
        dropAgent(s);
        free(s->ring);
        free(s->frag);
        free(s->delayed);
        s->ring = NULL;
        s->frag = NULL;
        s->delayed = NULL;
    }
    I.on = 0;
}

void portIceStop(int link) {
    Slot* s = slotAt(link);
    if (s == NULL) {
        return;
    }
    if (s->phase != IDLE) {
        say("net: the direct connection to player %d closed with its relay link (the key again)", s->player);
        dropAgent(s);
    }
    /* (a new key: the packets' counts start again, both ways) */
    s->txCount = s->rxTop = s->rxSeen = 0;
    memset(s->typeNewest, 0, sizeof(s->typeNewest));
}

void portIceTick(int link, int player, int linked, unsigned frame, unsigned long long now) {
    Slot* s = slotAt(link);
    int jstate;
    if (s == NULL) {
        return;
    }
    I.frame = frame;
    s->player = player;
    if (linked && !s->linked) {
        s->linkedAt = now;
        if (s->nextTry < now + FIRST_TRY_MS) {
            s->nextTry = now + FIRST_TRY_MS;
        }
    }
    s->linked = linked;
    if (!linked) {
        if (s->phase != IDLE) {
            dropAgent(s);
        }
        return;
    }
    /* the packets in */
    for (;;) {
        Dgram d;
        int got = 0;
        const unsigned epoch = s->epoch;
        LOCK();
        if (s->ringN > 0 && s->ring != NULL) {
            const Dgram* q = &s->ring[s->ringHead];
            d.len = q->len;
            memcpy(d.d, q->d, q->len);
            s->ringHead = (s->ringHead + 1) % RING;
            s->ringN--;
            got = 1;
        }
        UNLOCK();
        if (!got) {
            break;
        }
        packetIn(s, d.d, d.len, now);
        if (s->epoch != epoch) {
            return; /* (a message ended this link meanwhile) */
        }
    }
    if (s->delayed != NULL) {
        delayedOut(s, now);
    }
    /* the host's try */
    if (I.host && s->phase == IDLE && portIceWanted() && now >= s->nextTry) {
        s->peerOff = 0;
        startTry(s, link, now);
    }
    if (s->phase == IDLE) {
        return;
    }
    sendFound(s, link);
    LOCK();
    jstate = s->jstate;
    UNLOCK();
    if (jstate == JUICE_STATE_FAILED) {
        endTry(s, s->phase == UP ? "ICE lost the way" : "ICE found no way through", 1, now);
        return;
    }
    if (jstate == JUICE_STATE_CONNECTED || jstate == JUICE_STATE_COMPLETED) {
        if (now - s->lastProbe >= (s->phase == UP ? PROBE_UP_MS : PROBE_TRYING_MS)) {
            probe(s, now); /* (on its own: the messages wait for their batch's pace) */
        }
        if (s->phase == TRYING && s->heard) {
            char local[JUICE_MAX_CANDIDATE_SDP_STRING_LEN], remote[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
            char lt[16] = "?", rt[16] = "?";
            s->phase = UP;
            s->upAt = now;
            s->ups++;
            if (juice_get_selected_candidates(s->agent, local, sizeof(local), remote, sizeof(remote)) ==
                JUICE_ERR_SUCCESS) {
                typeOf(local, lt, sizeof(lt));
                typeOf(remote, rt, sizeof(rt));
            }
            say("net: direct connection to player %d up (this side %s, theirs %s, after %llu ms)", s->player, lt, rt,
                now - s->startedAt);
        }
    }
    if (s->phase == TRYING && now - s->startedAt > TRY_MS) {
        endTry(s, jstate == JUICE_STATE_CONNECTED || jstate == JUICE_STATE_COMPLETED
                      ? "ICE connected, but nothing came through"
                      : "no way through in 15 s",
               1, now);
        return;
    }
    if (s->phase == UP && now - s->lastRx > QUIET_MS) {
        endTry(s, "nothing came for 2 s", 1, now);
    }
}

void portIceSignalIn(int link, const uint8_t* p, size_t n) {
    Slot* s = slotAt(link);
    PortIceSig h;
    char text[JUICE_MAX_SDP_STRING_LEN + 1];
    const unsigned long long now = ssNetMs();
    size_t len;
    if (s == NULL || n < sizeof(h)) {
        return;
    }
    memcpy(&h, p, sizeof(h));
    len = n - sizeof(h) < JUICE_MAX_SDP_STRING_LEN ? n - sizeof(h) : JUICE_MAX_SDP_STRING_LEN;
    memcpy(text, p + sizeof(h), len);
    text[len] = '\0';
    if (!I.host) {
        if (h.op == PORT_ICE_OFFER) {
            char sdp[JUICE_MAX_SDP_STRING_LEN];
            if (!portIceWanted()) {
                say("net: the host offers a direct connection: Direct connection is off here (the relay it is)");
                sig(link, PORT_ICE_NO, h.gen, NULL);
                return;
            }
            if (s->phase != IDLE) {
                dropAgent(s); /* (a newer try: the old one's over) */
            }
            s->gen = h.gen;
            s->tries++;
            if (!makeAgent(s) || juice_set_remote_description(s->agent, text) != JUICE_ERR_SUCCESS ||
                juice_get_local_description(s->agent, sdp, sizeof(sdp)) != JUICE_ERR_SUCCESS) {
                dropAgent(s);
                sig(link, PORT_ICE_DOWN, h.gen, NULL);
                return;
            }
            s->remoteSet = 1;
            s->phase = TRYING;
            s->startedAt = now;
            say("net: trying the direct connection the host offers (try %u)", s->tries);
            sig(link, PORT_ICE_ANSWER, h.gen, sdp);
            juice_gather_candidates(s->agent);
            return;
        }
    } else if (h.op == PORT_ICE_NO) {
        if (h.gen == s->gen && s->phase != IDLE) {
            dropAgent(s);
        }
        if (!s->peerOff) {
            say("net: player %d has Direct connection off: the relay carries the game", s->player);
        }
        s->peerOff = 1;
        s->nextTry = now + 120000ull; /* (asked again later: the setting may change) */
        return;
    } else if (h.op == PORT_ICE_ANSWER) {
        if (h.gen == s->gen && s->phase == TRYING && !s->remoteSet && s->agent != NULL) {
            if (juice_set_remote_description(s->agent, text) == JUICE_ERR_SUCCESS) {
                s->remoteSet = 1;
            } else {
                endTry(s, "the player's answer didn't read", 1, now);
            }
        }
        return;
    }
    if (h.gen != s->gen || s->phase == IDLE || s->agent == NULL) {
        return; /* (an older try's) */
    }
    if (h.op == PORT_ICE_CAND) {
        if (s->remoteSet && !I.noCand) {
            juice_add_remote_candidate(s->agent, text);
        }
    } else if (h.op == PORT_ICE_DONE) {
        if (s->remoteSet) {
            juice_set_remote_gathering_done(s->agent);
        }
    } else if (h.op == PORT_ICE_DOWN) {
        endTry(s, "the other game ended it", 0, now);
    }
}

int portIceUp(int link) {
    const Slot* s = slotAt(link);
    return s != NULL && s->phase == UP && s->agent != NULL;
}

int portIceDual(int link) {
    const Slot* s = slotAt(link);
    unsigned long long now;
    if (s == NULL || s->phase != UP) {
        return 0;
    }
    now = ssNetMs();
    return now - s->lastRx > SHAKY_MS ||
           (s->peerRxAge > SHAKY_MS && s->peerRxAgeAt != 0 && now - s->peerRxAgeAt < 1000ull);
}

int portIceSend(int link, const void* msg, size_t len) {
    Slot* s = slotAt(link);
    const uint8_t* m = (const uint8_t*)msg;
    uint32_t id;
    uint16_t cnt, idx;
    if (s == NULL || s->phase != UP || s->agent == NULL || len < 5 || len > PORT_ICE_BIG) {
        return 0;
    }
    if (len <= PORT_ICE_MTU - 1) {
        if (s->txLen + len > PORT_ICE_MTU) {
            txFlush(s);
        }
        if (s->txLen == 0) {
            s->tx[0] = K_MSGS;
            s->txLen = 1;
        }
        memcpy(s->tx + s->txLen, m, len);
        s->txLen += len;
        return 1;
    }
    /* in pieces, after what's packed so far (in order) */
    txFlush(s);
    id = ++s->fragId;
    if (id == 0) {
        id = s->fragId = 1;
    }
    cnt = (uint16_t)((len + FRAG_DATA - 1) / FRAG_DATA);
    for (idx = 0; idx < cnt; idx++) {
        uint8_t b[PORT_ICE_MTU];
        const size_t at = (size_t)idx * FRAG_DATA;
        const size_t k = len - at < FRAG_DATA ? len - at : FRAG_DATA;
        b[0] = K_FRAG;
        put32(b + 1, id);
        memcpy(b + 5, &idx, 2);
        memcpy(b + 7, &cnt, 2);
        memcpy(b + 9, m + at, k);
        wire(s, b, 9 + k);
    }
    return 1;
}

void portIceFlush(void) {
    int i;
    if (!I.on) {
        return;
    }
    for (i = 0; i < PORT_ICE_LINKS; i++) {
        txFlush(&I.slot[i]);
    }
}

int portIcePending(int link) {
    const Slot* s = slotAt(link);
    return s != NULL && s->txLen > 1;
}

void portIceFlushLink(int link) {
    Slot* s = slotAt(link);
    if (s != NULL) {
        txFlush(s);
    }
}

int portIceRtoMs(int link) {
    const Slot* s = slotAt(link);
    double rto;
    if (s == NULL || !s->hasRtt) {
        return 1000; /* (RFC 6298's first wait, before a round trip is known) */
    }
    rto = 1.25 * s->srtt + 4.0 * s->rttvar;
    return rto < 200.0 ? 200 : rto > 2000.0 ? 2000 : (int)rto;
}

#endif
