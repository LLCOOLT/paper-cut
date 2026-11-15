/* The online messages' fuzzer (headless build only; docs/TESTING.md, "Online fuzzing").
 *
 * Included at the end of statesync_net_pc.c, so it reaches the connection's own static handlers.
 * $TTYD_NET_FUZZ=1 turns it on in a game that plays online as usual: every message this game takes from
 * the others is kept (a few of each type: the corpus), and from $TTYD_NET_FUZZ_FROM (default 300) to
 * $TTYD_NET_FUZZ_TO (default never) frames after another player first linked, each game frame hands
 * $TTYD_NET_FUZZ_RATE (default 4) mutated or made-up messages to the handlers, as a hostile or broken peer
 * would send them:
 *   truncated, grown (to past the biggest a handler takes), bytes flipped, counts and indexes set to edge
 *   values (0, -1, 255, 65535, INT_MAX), floats to NaN, infinity or huge, the type changed, two messages
 *   spliced, random bytes, old ones replayed, from the wrong player or a number no player has, and to a
 *   connection whose hello hasn't come.
 * $TTYD_NET_FUZZ_LAYERS picks where they go (default "mrdtfs"):
 *   m  ssOnMessage (the unreliable messages: places, the room's NPCs and objects, co-op pads, forwards)
 *   r  ssOnRel (every reliable one: progress, rooms, battle, settings, co-op, scenes, modes, forwards)
 *   d  dispatch (the connection's own: ack, ping, the reliable wrapper, unknown types)
 *   h  hellos (a session's start: these reset sessions, so off by default)
 *   t  the relay's words (roomText: peer, gone, peer N, ticket, framed, junk)
 *   f  the relay's frames (roomFrame/peerFrames: sealed, the old relay's note; not the key's rounds)
 *   k  the key's rounds too (R_PAKE1/2: they restart keys, off by default)
 *   w  by address: raw bytes into the message reader (bad lengths drop the connection)
 *   s  wrong seats: the "from" player 0, 5, 200, or another guest's
 *   p  pre-handshake: to a connection with no hello yet (and an unused one)
 * $TTYD_NET_FUZZ_SEED seeds it (default 1). Each 600 frames it logs "[sync] fuzz: ..." counts; with
 * $TTYD_NET_FUZZ_LAST=<file> each mutant is written there before it's handed on (the one a crash came
 * with, for $TTYD_NET_FUZZ_REPLAY=<file>, which hands that one on at $TTYD_NET_FUZZ_FROM, once, and nothing
 * else). $TTYD_NET_FUZZ_INJECT (tests, fzInject below) hands the messages it spells out on once instead. */

#ifndef TTYD_BACKEND_AURORA

#define FZ_TYPES 256
#define FZ_KEEP 3
#define FZ_MAX (96u << 10) /* a mutant's bytes at most (past FRAME_MAX's slices are the relay's to cut) */

typedef struct FzMsg {
    uint8_t* data;
    size_t n;
} FzMsg;

static struct {
    int read, on, replayDone, inside;
    unsigned long long rng;
    unsigned from, to, rate;
    char layers[24];
    char last[260];
    char replay[260];
    const char* inject; /* $TTYD_NET_FUZZ_INJECT (tests): the messages to hand on once, else NULL */
    int atHere, armed;  /* $TTYD_NET_FUZZ_INJECT_AT=here: at the next linkHere once FROM has passed (armed) */
    FzMsg un[FZ_TYPES][FZ_KEEP]; /* unreliable (and connection) messages, by type */
    FzMsg rel[FZ_TYPES][FZ_KEEP]; /* reliable ones' inner messages, by inner type */
    unsigned unN[FZ_TYPES], relN[FZ_TYPES];
    unsigned long long sent[16];
    unsigned lastLog, base; /* (base: the frame of the first pump with a player linked, +1; 0 none yet) */
    int stopped;
    uint8_t buf[FZ_MAX + 64];
} FZ;

enum { FZ_L_MSG, FZ_L_REL, FZ_L_DISPATCH, FZ_L_HELLO, FZ_L_TEXT, FZ_L_FRAME, FZ_L_KEY, FZ_L_WIRE, FZ_L_N };
static const char kFzLayerName[FZ_L_N][9] = {"msg", "rel", "dispatch", "hello", "text", "frame", "key", "wire"};

static unsigned long long fzRand(void) {
    unsigned long long x = FZ.rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    FZ.rng = x;
    return x * 2685821657736338717ull;
}

static unsigned fzBelow(unsigned n) {
    return n != 0 ? (unsigned)(fzRand() % n) : 0;
}

static int fzLayer(char c) {
    return strchr(FZ.layers, c) != NULL;
}

static void fzRead(void) {
    const char* v;
    if (FZ.read) {
        return;
    }
    FZ.read = 1;
    v = getenv("TTYD_NET_FUZZ");
    FZ.on = v != NULL && v[0] != '\0' && v[0] != '0';
    FZ.inject = getenv("TTYD_NET_FUZZ_INJECT");
    if (FZ.inject != NULL && FZ.inject[0] != '\0') {
        const char* when = getenv("TTYD_NET_FUZZ_INJECT_AT");
        FZ.on = 1; /* (the messages given, once: no fuzzing) */
        FZ.atHere = when != NULL && strcmp(when, "here") == 0;
    } else {
        FZ.inject = NULL;
    }
    if (!FZ.on) {
        return;
    }
    v = getenv("TTYD_NET_FUZZ_SEED");
    FZ.rng = (v != NULL ? strtoull(v, NULL, 0) : 1) * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    v = getenv("TTYD_NET_FUZZ_FROM");
    FZ.from = v != NULL ? (unsigned)strtoul(v, NULL, 10) : 300;
    v = getenv("TTYD_NET_FUZZ_TO");
    FZ.to = v != NULL ? (unsigned)strtoul(v, NULL, 10) : 0xFFFFFFFFu;
    v = getenv("TTYD_NET_FUZZ_RATE");
    FZ.rate = v != NULL ? (unsigned)strtoul(v, NULL, 10) : 4;
    v = getenv("TTYD_NET_FUZZ_LAYERS");
    snprintf(FZ.layers, sizeof(FZ.layers), "%s", v != NULL ? v : "mrdtfs");
    v = getenv("TTYD_NET_FUZZ_LAST");
    snprintf(FZ.last, sizeof(FZ.last), "%s", v != NULL ? v : "");
    v = getenv("TTYD_NET_FUZZ_REPLAY");
    snprintf(FZ.replay, sizeof(FZ.replay), "%s", v != NULL ? v : "");
    ssLog("fuzz: on (layers %s, %u a frame from frame %u%s)", FZ.layers, FZ.rate, FZ.from,
          FZ.replay[0] ? ", replaying one" : "");
}

static void fzKeep(FzMsg* slot, unsigned* count, const uint8_t* p, size_t n) {
    FzMsg* m;
    uint8_t* copy;
    if (n > FZ_MAX) {
        return;
    }
    m = &slot[*count % FZ_KEEP];
    copy = (uint8_t*)realloc(m->data, n > 0 ? n : 1);
    if (copy == NULL) {
        return;
    }
    if (n > 0) {
        memcpy(copy, p, n);
    }
    m->data = copy;
    m->n = n;
    (*count)++;
}

/* dispatch's: every message as it came, kept (a few of each type) */
static void fzSeen(int type, const uint8_t* p, size_t n) {
    if (!FZ.on || FZ.inside || type < 0 || type >= FZ_TYPES) {
        return;
    }
    if (type == SS_MSG_REL && n >= 5) {
        fzKeep(FZ.rel[p[4]], &FZ.relN[p[4]], p + 5, n - 5);
    } else if (fzBelow(4) == 0 || FZ.unN[type] < FZ_KEEP) {
        fzKeep(FZ.un[type], &FZ.unN[type], p, n);
    }
}

/* a kept message of one of the two kinds (a type of one seen, any when none is), or 0 */
static const FzMsg* fzPick(int rel, int* type) {
    FzMsg(*box)[FZ_KEEP] = rel ? FZ.rel : FZ.un;
    unsigned* count = rel ? FZ.relN : FZ.unN;
    int tries;
    for (tries = 0; tries < 64; tries++) {
        const int t = (int)fzBelow(FZ_TYPES);
        if (count[t] > 0) {
            const FzMsg* m = &box[t][fzBelow(count[t] < FZ_KEEP ? count[t] : FZ_KEEP)];
            *type = t;
            return m->data != NULL ? m : NULL;
        }
    }
    return NULL;
}

/* the types worth making up (the ones handlers know, their neighbours, and a few nobody does) */
static int fzAnyType(int rel) {
    static const uint8_t kUn[] = {1, 2, 3, 4, 5, 9, 10, 11, 12, 13, 14, 15, 16, 0, 255};
    static const uint8_t kRel[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 19, 20, 21, 22, 23, 24, 29,
                                   30, 31, 32, 35, 36, 37, 38, 40, 41, 50, 51, 60, 61, 62, 63, 64, 65, 66, 69, 70, 255};
    if (fzBelow(8) == 0) {
        return (int)fzBelow(256);
    }
    return rel ? kRel[fzBelow(sizeof(kRel))] : kUn[fzBelow(sizeof(kUn))];
}

static void fzPut32(uint8_t* p, size_t n, size_t at, uint32_t v) {
    if (at + 4 <= n) {
        memcpy(p + at, &v, 4);
    }
}

/* one mutation of buf[0..*n) in place (the buffer holds FZ_MAX) */
static void fzMutateOnce(uint8_t* b, size_t* n) {
    static const uint32_t kEdge32[] = {0,          1,          0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0xFFFFu, 0x10000u,
                                       0xFFu,      0x100u,     0x7FFFu,     0x8000u,     0xFFFF8000u, 16,      64,
                                       0xFFFFFFFEu, 0x3FFFFFFFu, 1000000u};
    static const float kFloats[] = {0.0f, -0.0f, 1e30f, -1e30f, 3.4e38f, 1e-45f, 1e7f, -1e7f};
    const unsigned op = fzBelow(16);
    size_t at;
    switch (op) {
        case 0: /* truncated */
            *n = *n > 0 ? fzBelow((unsigned)*n) : 0;
            break;
        case 1: { /* grown: a little, or a lot */
            const size_t more = fzBelow(4) == 0 ? fzBelow(FZ_MAX - (unsigned)(*n < FZ_MAX ? *n : FZ_MAX)) : fzBelow(64);
            size_t k;
            for (k = 0; k < more && *n < FZ_MAX; k++) {
                b[(*n)++] = (uint8_t)fzRand();
            }
            break;
        }
        case 2: /* bits flipped */
        case 3:
            if (*n > 0) {
                unsigned k, many = 1 + fzBelow(8);
                for (k = 0; k < many; k++) {
                    b[fzBelow((unsigned)*n)] ^= (uint8_t)(1u << fzBelow(8));
                }
            }
            break;
        case 4: /* a byte to an edge value (a count, an index, a bool) */
            if (*n > 0) {
                static const uint8_t kEdge8[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64, 0x7F, 0x80, 0xFE, 0xFF};
                b[fzBelow((unsigned)*n)] = kEdge8[fzBelow(sizeof(kEdge8))];
            }
            break;
        case 5: /* the first bytes (most messages' count or index) to an edge value */
        case 6:
            at = fzBelow(2) ? 0 : (size_t)fzBelow(16);
            fzPut32(b, *n, at, kEdge32[fzBelow(sizeof(kEdge32) / sizeof(kEdge32[0]))]);
            if (at + 2 <= *n && fzBelow(2)) {
                const uint16_t v = (uint16_t)kEdge32[fzBelow(sizeof(kEdge32) / sizeof(kEdge32[0]))];
                memcpy(b + at, &v, 2);
            }
            break;
        case 7: /* a 32-bit word anywhere (aligned) to an edge value */
            if (*n >= 4) {
                at = (size_t)fzBelow((unsigned)(*n / 4)) * 4;
                fzPut32(b, *n, at, kEdge32[fzBelow(sizeof(kEdge32) / sizeof(kEdge32[0]))]);
            }
            break;
        case 8: /* a float anywhere (aligned) to NaN, infinity or huge */
        case 9:
            if (*n >= 4) {
                uint32_t v;
                const unsigned which = fzBelow(4);
                at = (size_t)fzBelow((unsigned)(*n / 4)) * 4;
                if (which == 0) {
                    v = 0x7FC00000u | (uint32_t)fzBelow(0x3FFFFF); /* NaN */
                } else if (which == 1) {
                    v = fzBelow(2) ? 0x7F800000u : 0xFF800000u; /* +-infinity */
                } else {
                    const float f = kFloats[fzBelow(sizeof(kFloats) / sizeof(kFloats[0]))];
                    memcpy(&v, &f, 4);
                }
                fzPut32(b, *n, at, v);
            }
            break;
        case 10: /* a run of bytes random */
            if (*n > 0) {
                size_t k, from = fzBelow((unsigned)*n), len = 1 + fzBelow(32);
                for (k = from; k < from + len && k < *n; k++) {
                    b[k] = (uint8_t)fzRand();
                }
            }
            break;
        case 11: { /* another kept message spliced in */
            int t;
            const FzMsg* o = fzPick(fzBelow(2), &t);
            if (o != NULL && o->n > 0) {
                const size_t cut = *n > 0 ? fzBelow((unsigned)*n + 1) : 0;
                const size_t from = fzBelow((unsigned)o->n);
                size_t k;
                for (k = from; k < o->n && cut + (k - from) < FZ_MAX; k++) {
                    b[cut + (k - from)] = o->data[k];
                }
                *n = cut + (k - from);
            }
            break;
        }
        case 12: /* text fields: no end (a byte run of 'A's) */
            if (*n > 0) {
                size_t k, from = fzBelow((unsigned)*n), len = 8 + fzBelow(64);
                for (k = from; k < from + len && k < *n; k++) {
                    b[k] = 'A' + (uint8_t)fzBelow(3);
                }
            }
            break;
        case 13: { /* a name where a name was (a map, a model, an animation, a door): real ones and ones no game has */
            static const char* const kNames[] = {"gor_01", "gor_99", "zzz_01", "jon_00", "jon_99", "hei_03", "tik_00",
                                                 "", "a_mario", "c_pkr", "a_zzz", "M_S_1", "M_Z_9", "dokan_2",
                                                 "w_bero", "no_bero", "end_00", "dmo_00", "title", "aaa_00"};
            const char* name = kNames[fzBelow(sizeof(kNames) / sizeof(kNames[0]))];
            const size_t len = strlen(name) + 1;
            size_t at;
            /* (at a printable run's start when there's one: where a name field begins) */
            for (at = fzBelow((unsigned)(*n + 1)); at > 0 && at < *n && b[at - 1] >= 'A' && b[at - 1] <= 'z'; at--) {
            }
            if (at + len <= *n) {
                memcpy(b + at, name, len);
            }
            break;
        }
        default: /* all zero or all 0xFF */
            memset(b, fzBelow(2) ? 0xFF : 0, *n);
            break;
    }
}

/* a mutant of a kept message (or a made-up one) in FZ.buf: its length; *type its type */
static size_t fzMake(int rel, int* type) {
    int t = 0;
    const FzMsg* m = fzBelow(10) != 0 ? fzPick(rel, &t) : NULL;
    size_t n;
    unsigned k, ops;
    if (m != NULL) {
        n = m->n;
        memcpy(FZ.buf, m->data, n);
        *type = t;
        if (fzBelow(12) == 0) {
            return n; /* (replayed as it was: an old one again) */
        }
        if (fzBelow(8) == 0) {
            *type = fzAnyType(rel); /* (as another type) */
        }
    } else {
        /* made up: random bytes, any length up to a few kilobytes (now and then far more) */
        n = fzBelow(8) == 0 ? fzBelow(FZ_MAX) : fzBelow(fzBelow(2) ? 64 : 4096);
        for (k = 0; k < n; k++) {
            FZ.buf[k] = (uint8_t)fzRand();
        }
        *type = fzAnyType(rel);
    }
    ops = 1 + fzBelow(fzBelow(4) == 0 ? 8 : 2);
    for (k = 0; k < ops; k++) {
        fzMutateOnce(FZ.buf, &n);
    }
    return n;
}

/* the player a mutant says it's from: the connection's own mostly, now and then a wrong one */
static int fzFrom(const Peer* P) {
    if (fzLayer('s') && fzBelow(6) == 0) {
        static const int kWrong[] = {0, 1, 2, 3, 4, 5, -1, 200};
        return kWrong[fzBelow(sizeof(kWrong) / sizeof(kWrong[0]))];
    }
    return P != NULL && P->player >= 1 ? P->player : (S.host ? 2 : 1);
}

static void fzLast(int layer, int from, int type, int peer, const uint8_t* p, size_t n) {
    FILE* f;
    uint32_t head[5];
    if (FZ.last[0] == '\0' || (f = fopen(FZ.last, "wb")) == NULL) {
        return;
    }
    head[0] = 0x5A465353u; /* "SSFZ" */
    head[1] = (uint32_t)layer;
    head[2] = (uint32_t)from;
    head[3] = ((uint32_t)type & 0xFFFFu) | ((uint32_t)peer << 16);
    head[4] = (uint32_t)n;
    fwrite(head, sizeof(head), 1, f);
    fwrite(p, 1, n, f);
    fclose(f);
}

/* a connection to aim at: a linked one mostly (p: now and then one with no hello yet, or unused) */
static Peer* fzPeer(void) {
    int i, tries;
    if (fzLayer('p') && fzBelow(16) == 0) {
        return &S.peer[fzBelow(MAX_PEERS)];
    }
    for (tries = 0; tries < 8; tries++) {
        Peer* P = &S.peer[fzBelow(MAX_PEERS)];
        if (P->used && P->helloIn) {
            return P;
        }
    }
    for (i = 0; i < MAX_PEERS; i++) {
        if (S.peer[i].used && S.peer[i].helloIn) {
            return &S.peer[i];
        }
    }
    return NULL;
}

/* the relay's words, mostly real ones with odd numbers, some junk */
static void fzText(char* t, size_t cap) {
    static const char* const kWords[] = {"peer", "gone", "framed", "you 127.0.0.1:1", "you ::1", "open", "ticket ",
                                         "peer ", "peergone ", "list", "rooms 3", "caps split", "busy", "watch 2"};
    static const char* const kNums[] = {"0", "1", "2", "-1", "3", "99999999999", "", " ", "1x", "-2147483648", "0x1"};
    const char* w = kWords[fzBelow(sizeof(kWords) / sizeof(kWords[0]))];
    if (!fzLayer('k') && (!strcmp(w, "peer") || !strcmp(w, "gone"))) {
        w = "peer "; /* (the relay's own word that the link came or went restarts its key: layer k's, as the key's
                        rounds are; "peer N" with a place no relay has instead. A ticket, framed or caps after
                        the relay's welcome are left out, so they're this layer's: 0.5.1) */
    }
    if (fzBelow(8) == 0) {
        size_t k, n = fzBelow((unsigned)cap - 1);
        for (k = 0; k < n; k++) {
            t[k] = (char)(1 + fzBelow(255));
        }
        t[n] = '\0';
        return;
    }
    if (strcmp(w, "ticket ") == 0) {
        /* (letters, or one shaped as a relay's: lowercase hex, 16 or 32 digits) */
        const int hex = fzBelow(2);
        size_t k, n = hex ? (fzBelow(2) ? 16u : 32u) : fzBelow((unsigned)cap - 8);
        snprintf(t, cap, "%s", w);
        for (k = 0; k < n; k++) {
            t[7 + k] = hex ? "0123456789abcdef"[fzBelow(16)] : (char)('a' + fzBelow(26));
        }
        t[7 + n] = '\0';
        return;
    }
    {
        static const char* const kOff[] = {"2", "3", "99999999999", "", " ", "x", "-2147483648", "+2"};
        const char* num = fzLayer('k') ? kNums[fzBelow(sizeof(kNums) / sizeof(kNums[0]))]
                                       : kOff[fzBelow(sizeof(kOff) / sizeof(kOff[0]))];
        snprintf(t, cap, "%s%s", w, w[strlen(w) - 1] == ' ' ? num : "");
    }
}

static void fzOne(unsigned frame) {
    int layer, tries, type = 0, from;
    Peer* P;
    size_t n;
    char opts[FZ_L_N];
    int nOpts = 0;
    (void)frame;
    if (fzLayer('m')) {
        opts[nOpts++] = FZ_L_MSG;
    }
    if (fzLayer('r')) {
        opts[nOpts++] = FZ_L_REL;
    }
    if (fzLayer('d')) {
        opts[nOpts++] = FZ_L_DISPATCH;
    }
    if (fzLayer('h')) {
        opts[nOpts++] = FZ_L_HELLO;
    }
    if (fzLayer('t') && S.room) {
        opts[nOpts++] = FZ_L_TEXT;
    }
    if (fzLayer('f') && S.room) {
        opts[nOpts++] = FZ_L_FRAME;
    }
    if (fzLayer('k') && S.room) {
        opts[nOpts++] = FZ_L_KEY;
    }
    if (fzLayer('w') && !S.room) {
        opts[nOpts++] = FZ_L_WIRE;
    }
    if (nOpts == 0) {
        return;
    }
    (void)tries;
    layer = opts[fzBelow((unsigned)nOpts)];
    P = fzPeer();
    if (P == NULL) {
        return; /* (no one linked yet: nothing a peer could send would reach a handler) */
    }
    from = fzFrom(P);
    FZ.sent[layer]++;
    switch (layer) {
        case FZ_L_MSG:
            n = fzMake(0, &type);
            fzLast(layer, from, type, (int)(P - S.peer), FZ.buf, n);
            ssOnMessage(from, type, FZ.buf, n);
            break;
        case FZ_L_REL:
            n = fzMake(1, &type);
            fzLast(layer, from, type, (int)(P - S.peer), FZ.buf, n);
            ssOnRel(from, type, FZ.buf, n);
            break;
        case FZ_L_DISPATCH: {
            static const uint8_t kConn[] = {SS_MSG_ACK, SS_MSG_PING, SS_MSG_REL, 0, 5, 6, 9, 16, 99, 255};
            n = fzMake(0, &type);
            type = fzBelow(2) ? kConn[fzBelow(sizeof(kConn))] : type;
            if (type == SS_MSG_HELLO) {
                type = SS_MSG_PING;
            }
            if (type == SS_MSG_REL && n >= 4 && fzBelow(2)) {
                /* (a seq near the expected one, never the very one: that would take the real next's place) */
                const uint32_t seqNear = P->rxExpect + (uint32_t)(fzBelow(2) ? -1 - (int)fzBelow(3) : 1 + (int)fzBelow(3));
                memcpy(FZ.buf, &seqNear, 4);
            } else if (type == SS_MSG_REL && n >= 4) {
                uint32_t s;
                memcpy(&s, FZ.buf, 4);
                if (s == P->rxExpect) {
                    s += 7;
                    memcpy(FZ.buf, &s, 4);
                }
            }
            fzLast(layer, from, type, (int)(P - S.peer), FZ.buf, n);
            dispatch(P, type, FZ.buf, n);
            break;
        }
        case FZ_L_HELLO: {
            Hello h;
            fillHello(&h, P, 0, 0);
            h.host = (uint32_t)!S.host;
            memcpy(FZ.buf, &h, sizeof(h));
            n = sizeof(h);
            if (fzBelow(4) != 0) {
                fzMutateOnce(FZ.buf, &n);
            }
            fzLast(layer, from, SS_MSG_HELLO, (int)(P - S.peer), FZ.buf, n);
            dispatch(P, SS_MSG_HELLO, FZ.buf, n);
            break;
        }
        case FZ_L_TEXT: {
            char t[128];
            fzText(t, sizeof(t));
            fzLast(layer, 0, 0, 0, (const uint8_t*)t, strlen(t));
            roomText(t);
            break;
        }
        case FZ_L_FRAME: {
            /* a sealed frame (the test relay's: a message as it is) with a mutant inside, or a mutated frame
               list (a peer's, FROM_PEER's inside) */
            int kind = fzBelow(8) == 0 ? (int)fzBelow(256) : fzBelow(8) == 0 ? R_OLD_RELAY : R_SEALED;
            if (kind == R_PAKE1 || kind == R_PAKE2) {
                kind = R_SEALED; /* (the key's rounds restart a session: layer k's alone) */
            }
            size_t inner = fzMake(0, &type);
            if (inner + 5 > FZ_MAX) {
                inner = FZ_MAX - 5;
            }
            memmove(FZ.buf + 5, FZ.buf, inner);
            {
                uint32_t len = (uint32_t)(inner + 1);
                if (fzBelow(6) == 0) {
                    len = (uint32_t)fzRand(); /* (a length that lies) */
                }
                memcpy(FZ.buf, &len, 4);
            }
            FZ.buf[4] = (uint8_t)(type == SS_MSG_HELLO || type == SS_MSG_REL ? SS_MSG_PING : type);
            n = inner + 5;
            if (kind == R_OLD_RELAY && fzBelow(2)) {
                n = 0; /* (the old relay's note as the host sends it: the kind alone; or garbled, with bytes) */
            }
            fzLast(layer, kind, type, (int)(P - S.peer), FZ.buf, n);
            if (S.host && fzBelow(3) == 0) {
                /* a list of frames as FROM_PEER's insides: this one wrapped as a room frame of that kind */
                memmove(FZ.buf + 5, FZ.buf, n < FZ_MAX - 5 ? n : FZ_MAX - 5);
                {
                    const uint32_t len = (uint32_t)(n + 1);
                    memcpy(FZ.buf, &len, 4);
                }
                FZ.buf[4] = (uint8_t)(fzBelow(8) == 0 ? 0xF1 : kind);
                peerFrames(P, FZ.buf, n + 5 - (fzBelow(4) == 0 ? fzBelow(6) : 0));
            } else {
                roomFrame(P, kind, FZ.buf, n);
            }
            break;
        }
        case FZ_L_KEY:
            n = fzMake(0, &type);
            if (n > 600) {
                n = fzBelow(600);
            }
            fzLast(layer, R_PAKE1, 0, (int)(P - S.peer), FZ.buf, n);
            roomFrame(P, fzBelow(2) ? R_PAKE1 : R_PAKE2, FZ.buf, n);
            break;
        case FZ_L_WIRE: {
            /* the reader's own: a length, then a type and bytes, the length often a lie */
            size_t body = fzMake(0, &type);
            uint32_t len = (uint32_t)(body + 1);
            if (body + 5 > FZ_MAX) {
                body = FZ_MAX - 5;
            }
            memmove(FZ.buf + 5, FZ.buf, body);
            if (fzBelow(3) == 0) {
                static const uint32_t kLens[] = {0, 1, 2, 0xFFFFFFFFu, RX_CAP - 8, RX_CAP - 7, RX_CAP, 0x80000000u};
                len = kLens[fzBelow(sizeof(kLens) / sizeof(kLens[0]))];
            }
            memcpy(FZ.buf, &len, 4);
            FZ.buf[4] = (uint8_t)(type == SS_MSG_HELLO ? SS_MSG_PING : type);
            n = body + 5;
            if (P->conn == BAD_SOCKET) {
                break;
            }
            fzLast(layer, from, type, (int)(P - S.peer), FZ.buf, n);
            (void)takeMessages(P, FZ.buf, n);
            break;
        }
        default:
            break;
    }
}

static void fzReplay(void) {
    FILE* f = fopen(FZ.replay, "rb");
    uint32_t head[5];
    size_t n;
    Peer* P;
    FZ.replayDone = 1;
    if (f == NULL) {
        ssLog("fuzz: no replay file %s", FZ.replay);
        return;
    }
    if (fread(head, sizeof(head), 1, f) != 1 || head[0] != 0x5A465353u || head[4] > FZ_MAX) {
        fclose(f);
        ssLog("fuzz: %s isn't a fuzz mutant", FZ.replay);
        return;
    }
    n = fread(FZ.buf, 1, head[4], f);
    fclose(f);
    P = &S.peer[(head[3] >> 16) % MAX_PEERS];
    ssLog("fuzz: replaying a %s mutant: from %d, type %u, %u bytes", head[1] < FZ_L_N ? kFzLayerName[head[1]] : "?",
          (int)head[2], head[3] & 0xFFFFu, (unsigned)n);
    switch (head[1]) {
        case FZ_L_MSG:
            ssOnMessage((int)head[2], (int)(head[3] & 0xFFFFu), FZ.buf, n);
            break;
        case FZ_L_REL:
            ssOnRel((int)head[2], (int)(head[3] & 0xFFFFu), FZ.buf, n);
            break;
        case FZ_L_DISPATCH:
        case FZ_L_HELLO:
            dispatch(P, (int)(head[3] & 0xFFFFu), FZ.buf, n);
            break;
        case FZ_L_TEXT:
            FZ.buf[n < FZ_MAX ? n : FZ_MAX - 1] = 0;
            roomText((const char*)FZ.buf);
            break;
        case FZ_L_FRAME:
        case FZ_L_KEY:
            roomFrame(P, (int)head[2], FZ.buf, n);
            break;
        case FZ_L_WIRE:
            if (P->conn != BAD_SOCKET) {
                (void)takeMessages(P, FZ.buf, n);
            }
            break;
        default:
            break;
    }
    ssLog("fuzz: the replayed mutant was taken (no crash)");
}

static int fzHex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* $TTYD_NET_FUZZ_INJECT (tests: a message no real game sends, to show a handler leaves it out): messages split
   by ';', each LAYER:FROM:TYPE:SIZE then @OFFSET=HEX ... (SIZE bytes, zero but for the HEX written at each
   OFFSET); LAYER m (ssOnMessage), r (ssOnRel) or f (roomFrame: TYPE the room frame's kind, on the link to
   player FROM); or t:WORD, a relay word as it is (roomText). Handed on once, at $TTYD_NET_FUZZ_FROM frames
   after the first link */
static void fzInject(const char* spec) {
    const char* at = spec;
    while (*at != '\0') {
        const char layer = *at;
        int from = 0, type = 0;
        unsigned size = 0;
        char* end;
        if (layer == 't' && at[1] == ':') {
            /* a relay word, as it is (to the next ';') */
            char word[128];
            size_t k = 0;
            at += 2;
            while (at[k] != '\0' && at[k] != ';' && k < sizeof(word) - 1) {
                word[k] = at[k];
                k++;
            }
            word[k] = '\0';
            ssLog("fuzz: inject: the relay's word \"%s\"", word);
            roomText(word);
            ssLog("fuzz: inject: taken (no crash)");
            at += k;
            at += *at == ';' ? 1 : 0;
            continue;
        }
        if ((layer != 'm' && layer != 'r' && layer != 'f') || at[1] != ':') {
            ssLog("fuzz: inject: can't read \"%s\"", at);
            return;
        }
        from = (int)strtol(at + 2, &end, 10);
        type = *end == ':' ? (int)strtol(end + 1, &end, 10) : 0;
        size = *end == ':' ? (unsigned)strtoul(end + 1, &end, 10) : 0;
        if (size > FZ_MAX) {
            size = FZ_MAX;
        }
        memset(FZ.buf, 0, size);
        while (*end == '@') {
            unsigned off = (unsigned)strtoul(end + 1, &end, 10);
            if (*end != '=') {
                break;
            }
            end++;
            while (fzHex(end[0]) >= 0 && fzHex(end[1]) >= 0) {
                if (off < size) {
                    FZ.buf[off] = (uint8_t)(fzHex(end[0]) * 16 + fzHex(end[1]));
                }
                off++;
                end += 2;
            }
        }
        ssLog("fuzz: inject: %s from player %d, type %d, %u bytes",
              layer == 'm' ? "a message" : layer == 'r' ? "a reliable one" : "a room frame", from, type, size);
        if (layer == 'm') {
            ssOnMessage(from, type, FZ.buf, size);
        } else if (layer == 'r') {
            ssOnRel(from, type, FZ.buf, size);
        } else {
            /* (a frame of the relay's room, TYPE its kind, on the link to player FROM: a joining game's one
               link is the host's) */
            Peer* P = S.host ? NULL : RP;
            int i;
            for (i = 0; P == NULL && i < MAX_PEERS; i++) {
                if (S.peer[i].used && S.peer[i].player == from) {
                    P = &S.peer[i];
                }
            }
            if (P != NULL && S.room) {
                roomFrame(P, type, FZ.buf, size);
            } else {
                ssLog("fuzz: inject: no link to player %d in a room", from);
            }
        }
        ssLog("fuzz: inject: taken (no crash)");
        at = *end == ';' ? end + 1 : end + strlen(end);
    }
}

/* linkHere's (the relay says the other's in the room: this side's key round just went): with
   $TTYD_NET_FUZZ_INJECT_AT=here, the inject once armed (above) is handed on now, before that key agrees */
static void fzLinkHere(void) {
    fzRead();
    if (FZ.inject != NULL && FZ.armed && !FZ.replayDone) {
        FZ.replayDone = 1;
        fzInject(FZ.inject);
    }
}

/* ssNetPump's, each game frame */
static void fzPump(unsigned frame) {
    unsigned k;
    fzRead();
    if (!FZ.on || frame == 0xFFFFFFFFu || !S.on) {
        return;
    }
    if (FZ.base == 0) {
        FZ.base = frame + 1;
    }
    frame -= FZ.base - 1; /* (from here on: frames since the first link) */
    if (frame > FZ.to && !FZ.stopped) {
        FZ.stopped = 1;
        ssLog("fuzz: stopped (%u frames after the first link)", frame);
    }
    if (frame < FZ.from || frame > FZ.to) {
        return;
    }
    if (FZ.inject != NULL) {
        if (!FZ.replayDone && fzPeer() != NULL) {
            if (!FZ.atHere) {
                FZ.replayDone = 1;
                fzInject(FZ.inject);
            } else if (!FZ.armed) {
                FZ.armed = 1;
                ssLog("fuzz: inject: at the next time the other's in the room (before its key)");
            }
        }
        return;
    }
    if (FZ.replay[0] != '\0') {
        if (!FZ.replayDone && fzPeer() != NULL) {
            fzReplay();
        }
        return;
    }
    FZ.inside = 1; /* (what the fuzzer hands on isn't kept as a peer's) */
    for (k = 0; k < FZ.rate; k++) {
        fzOne(frame);
    }
    FZ.inside = 0;
    if (frame - FZ.lastLog >= 600) {
        int i;
        char line[200];
        size_t at = 0;
        FZ.lastLog = frame;
        for (i = 0; i < FZ_L_N; i++) {
            if (FZ.sent[i]) {
                at += (size_t)snprintf(line + at, sizeof(line) - at, " %s %llu", kFzLayerName[i], FZ.sent[i]);
            }
        }
        line[at] = '\0';
        ssLog("fuzz: link frame %u, handed on:%s", frame, at ? line : " none yet (no one linked)");
    }
}

#endif
