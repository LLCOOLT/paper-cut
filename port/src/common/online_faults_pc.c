/* Online co-op's network trouble on purpose, for tests (port/include/port/online_faults.h).
 * Only $TTYD_NET_FAULTS turns it on; without it every call passes by. The
 * host's own headers only, no game ones. */
#include "port/online_faults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned long long release;
    size_t len;
    unsigned char* data;
} Held;

static int sRead, sOn;
static double sDrop, sDup, sReorder; /* percent */
static int sDelayLo, sDelayHi, sQuiet;
static unsigned sEvery;
static unsigned sAt[32];
static unsigned sOffFrom, sOffTo; /* offline:F-G */
static unsigned sOffMs;           /* offline_ms:F-D: from frame F for D milliseconds */
static unsigned sOffStep;         /* offline_battle:S-D: from a shared battle's step S for D milliseconds */
static unsigned long long sOffSince;
static int sAtCount;
static unsigned long long sSeed, sRand;
static int sSide = -1;
static Held* sQueue;
static size_t sQueueLen, sQueueCap;
static unsigned long long sLastRelease;
static Held sHeld; /* a message to go after the next (reorder) */
static int sHaveHeld;
static unsigned long long sHeldAt;
static unsigned sLastDisconnect = ~0u;
static unsigned long long sDropped, sDuplicated, sReordered, sDelayed, sDisconnects, sMessages;

static void printSummary(void) {
    printf("[netfaults] %s\n", portNetFaultsSummary());
    fflush(stdout);
}

static void readRules(void) {
    const char* env;
    char rules[1024];
    char* tok;
    if (sRead) {
        return;
    }
    sRead = 1;
    env = getenv("TTYD_NET_FAULTS");
    if (env == NULL || env[0] == '\0') {
        return;
    }
    sOn = 1;
    atexit(printSummary);
    sSeed = getenv("TTYD_NET_FAULTS_SEED") != NULL ? strtoull(getenv("TTYD_NET_FAULTS_SEED"), NULL, 0) : 1;
    snprintf(rules, sizeof(rules), "%s", env);
    for (tok = strtok(rules, ","); tok != NULL; tok = strtok(NULL, ",")) {
        char* colon = strchr(tok, ':');
        const char* v;
        if (colon == NULL) {
            continue;
        }
        *colon = '\0';
        v = colon + 1;
        if (strcmp(tok, "drop") == 0) {
            sDrop = atof(v);
        } else if (strcmp(tok, "dup") == 0) {
            sDup = atof(v);
        } else if (strcmp(tok, "reorder") == 0) {
            sReorder = atof(v);
        } else if (strcmp(tok, "delay") == 0) {
            const char* dash = strchr(v, '-');
            sDelayLo = atoi(v);
            sDelayHi = dash != NULL ? atoi(dash + 1) : sDelayLo;
            if (sDelayHi < sDelayLo) {
                sDelayHi = sDelayLo;
            }
        } else if (strcmp(tok, "disconnect_at") == 0) {
            const char* p = v;
            while (*p != '\0' && sAtCount < (int)(sizeof(sAt) / sizeof(sAt[0]))) {
                sAt[sAtCount++] = (unsigned)strtoul(p, NULL, 10);
                p = strchr(p, '/');
                if (p == NULL) {
                    break;
                }
                p++;
            }
        } else if (strcmp(tok, "offline") == 0) {
            const char* dash = strchr(v, '-');
            sOffFrom = (unsigned)strtoul(v, NULL, 10);
            sOffTo = dash != NULL ? (unsigned)strtoul(dash + 1, NULL, 10) : sOffFrom;
        } else if (strcmp(tok, "offline_ms") == 0) {
            const char* dash = strchr(v, '-');
            sOffFrom = (unsigned)strtoul(v, NULL, 10);
            sOffMs = dash != NULL ? (unsigned)strtoul(dash + 1, NULL, 10) : 0;
            sOffTo = sOffMs > 0 ? 0xFFFFFFFFu : 0;
        } else if (strcmp(tok, "offline_battle") == 0) {
            const char* dash = strchr(v, '-');
            sOffStep = (unsigned)strtoul(v, NULL, 10);
            sOffMs = dash != NULL ? (unsigned)strtoul(dash + 1, NULL, 10) : 0;
            sOffFrom = 0xFFFFFFFFu; /* (from the battle's step: portNetFaultsBattleStep) */
            sOffTo = sOffMs > 0 && sOffStep > 0 ? 0xFFFFFFFFu : 0;
        } else if (strcmp(tok, "disconnect_every") == 0) {
            sEvery = (unsigned)strtoul(v, NULL, 10);
        } else if (strcmp(tok, "quiet") == 0) {
            sQuiet = atoi(v);
        } else if (strcmp(tok, "seed") == 0) {
            sSeed = strtoull(v, NULL, 0);
        }
    }
    printf("[netfaults] on: drop %.3g%% dup %.3g%% reorder %.3g%% delay %d-%d ms, disconnects at %d frames%s, every %u, "
           "quiet %d ms, seed %llu\n",
           sDrop, sDup, sReorder, sDelayLo, sDelayHi, sAtCount, sAtCount > 0 ? " given" : "", sEvery, sQuiet,
           (unsigned long long)sSeed);
    fflush(stdout);
}

int portNetFaultsOn(void) {
    readRules();
    return sOn;
}

void portNetFaultsSide(int host) {
    readRules();
    if (sSide != (host ? 1 : 0)) {
        sSide = host ? 1 : 0;
        sRand = (sSeed * 2 + (unsigned long long)sSide) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
        if (sRand == 0) {
            sRand = 1;
        }
    }
}

/* splitmix64: the decisions' own random numbers */
static unsigned long long next(void) {
    unsigned long long z;
    if (sSide < 0) {
        portNetFaultsSide(0);
    }
    z = (sRand += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* true P percent of the time */
static int chance(double percent) {
    if (percent <= 0) {
        return 0;
    }
    return (double)(next() >> 11) * (100.0 / 9007199254740992.0) < percent;
}

static void queue(const void* data, size_t len, unsigned long long release) {
    Held* h;
    if (sQueueLen == sQueueCap) {
        sQueueCap = sQueueCap * 2 + 64;
        Held* grown = (Held*)realloc(sQueue, sQueueCap * sizeof(Held));
        if (grown == NULL) {
            abort(); /* out of memory */
        }
        sQueue = grown;
    }
    h = &sQueue[sQueueLen++];
    h->release = release;
    h->len = len;
    h->data = (unsigned char*)malloc(len > 0 ? len : 1);
    memcpy(h->data, data, len);
}

/* in order behind what's held already: a stream's bytes never pass each other */
static void enqueue(const void* data, size_t len, unsigned long long nowMs) {
    unsigned long long release = nowMs;
    if (sDelayHi > 0) {
        const unsigned span = (unsigned)(sDelayHi - sDelayLo) + 1u;
        release += (unsigned long long)sDelayLo + next() % span;
        sDelayed++;
    }
    if (release < sLastRelease) {
        release = sLastRelease;
    }
    sLastRelease = release;
    queue(data, len, release);
}

int portNetFaultsPoll(unsigned long long nowMs, PortNetFaultsWrite write) {
    size_t i = 0, k;
    int ok = 1;
    if (!sOn) {
        return 1;
    }
    if (sHaveHeld && nowMs - sHeldAt > 40) { /* nothing came after it for a while: it goes */
        sHaveHeld = 0;
        enqueue(sHeld.data, sHeld.len, nowMs);
        free(sHeld.data);
    }
    while (i < sQueueLen && sQueue[i].release <= nowMs) {
        if (ok && !write(sQueue[i].data, sQueue[i].len)) {
            ok = 0;
        }
        free(sQueue[i].data);
        i++;
    }
    if (i > 0) {
        for (k = i; k < sQueueLen; k++) {
            sQueue[k - i] = sQueue[k];
        }
        sQueueLen -= i;
    }
    return ok;
}

int portNetFaultsSend(const void* message, size_t len, int droppable, unsigned long long nowMs,
                      PortNetFaultsWrite write) {
    if (!portNetFaultsOn()) {
        return 0;
    }
    sMessages++;
    if (droppable) {
        if (chance(sDrop)) {
            sDropped++;
            return 1;
        }
        if (!sHaveHeld && chance(sReorder)) {
            sHaveHeld = 1;
            sHeldAt = nowMs;
            sHeld.len = len;
            sHeld.data = (unsigned char*)malloc(len > 0 ? len : 1);
            memcpy(sHeld.data, message, len);
            sReordered++;
            return 1;
        }
        enqueue(message, len, nowMs);
        if (chance(sDup)) {
            sDuplicated++;
            enqueue(message, len, nowMs);
        }
        if (sHaveHeld) { /* the one held goes after this one */
            sHaveHeld = 0;
            enqueue(sHeld.data, sHeld.len, nowMs);
            free(sHeld.data);
        }
    } else {
        enqueue(message, len, nowMs);
    }
    portNetFaultsPoll(nowMs, write);
    return 1;
}

void portNetFaultsReset(void) {
    size_t i;
    if (!sOn) {
        return;
    }
    for (i = 0; i < sQueueLen; i++) {
        free(sQueue[i].data);
    }
    sQueueLen = 0;
    sLastRelease = 0;
    if (sHaveHeld) {
        sHaveHeld = 0;
        free(sHeld.data);
    }
}

int portNetFaultsDisconnectAt(unsigned frame) {
    int i, hit = 0;
    if (!portNetFaultsOn() || frame == sLastDisconnect || frame == 0) {
        return 0;
    }
    for (i = 0; i < sAtCount; i++) {
        if (sAt[i] == frame) {
            hit = 1;
        }
    }
    if (sEvery > 0 && frame % sEvery == 0) {
        hit = 1;
    }
    if (hit) {
        sLastDisconnect = frame;
        sDisconnects++;
        printf("[netfaults] the connection dropped at frame %u\n", frame);
        fflush(stdout);
    }
    return hit;
}

int portNetFaultsQuietMs(int usual) {
    return portNetFaultsOn() && sQuiet > 0 ? sQuiet : usual;
}

const char* portNetFaultsSummary(void) {
    static char s[256];
    snprintf(s, sizeof(s), "messages %llu, dropped %llu, duplicated %llu, reordered %llu, delayed %llu, disconnects %llu",
             sMessages, sDropped, sDuplicated, sReordered, sDelayed, sDisconnects);
    return s;
}

/* offline:F-G: from session frame F to G the connections stay down (none made again meanwhile);
   offline_ms:F-D: from frame F for D milliseconds (a fast test run's frames aren't time) */
void portNetFaultsBattleStep(unsigned step) {
    if (sOffStep > 0 && step >= sOffStep && sOffFrom == 0xFFFFFFFFu) {
        sOffFrom = 0; /* (offline from now, for its milliseconds) */
    }
}

int portNetFaultsOffline(unsigned frame, unsigned long long nowMs) {
    static int sSaid;
    int off = portNetFaultsOn() && sOffTo > 0 && frame >= sOffFrom && frame < sOffTo;
    if (off && sOffMs > 0) {
        if (sOffSince == 0) {
            sOffSince = nowMs != 0 ? nowMs : 1;
        } else if (nowMs - sOffSince >= sOffMs) {
            sOffTo = 0; /* (once) */
            off = 0;
        }
    }
    if (off != sSaid) {
        sSaid = off;
        printf("[netfaults] %s at frame %u\n", off ? "offline" : "online again", frame);
        fflush(stdout);
    }
    return off;
}
