/* Link your Paper Mario 64 ROM (port/include/port/pm64.h, docs/PM64_LINK.md):
 * the ROM checked, the parts Paper Cut uses read out of it into a cache in the
 * user folder, and that cache read: Yay0, the NPC sprite archive, the message
 * archive, the move table. Formats as the PM64 decomp (pmret/papermario)
 * documents them; this code is Paper Cut's own. Nothing from the ROM is ever
 * written anywhere but <user>/pm64/. */
#include "port/file_pc.h"
#include "port/file_write.h"
#include "port/pm64.h"
#include "port/settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define pm64Mkdir(p) portMkdir(p) /* (UTF-8 paths: port/file_pc.h) */

/* the cache file's first 8 bytes (bytes, not a string: the NUL is one of them) */
static const uint8_t kCacheMagic[8] = {'P', 'C', 'P', 'M', '6', '4', 'C', 0};
#define PM64_CACHE_FORMAT 3u /* 2: the icon archive; 3: the item table (a segment of a cache's no longer read is left there) */

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint16_t be16(const uint8_t* p) {
    return (uint16_t)(p[0] << 8 | p[1]);
}

/* --- SHA-1 (FIPS 180-1), for knowing the ROM */

typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} Sha1;

static uint32_t rol(uint32_t v, int s) {
    return v << s | v >> (32 - s);
}

static void sha1Block(Sha1* c, const uint8_t* p) {
    uint32_t w[80], a, b, d, e, f, k, t, cc;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = be32(p + i * 4);
    }
    for (; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = c->h[0];
    b = c->h[1];
    cc = c->h[2];
    d = c->h[3];
    e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & cc) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ cc ^ d;
            k = 0xCA62C1D6;
        }
        t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = rol(b, 30);
        b = a;
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
}

static void sha1(const uint8_t* data, size_t size, uint8_t out[20]) {
    Sha1 c = {{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, 0, {0}, 0};
    size_t i;
    uint64_t bits = (uint64_t)size * 8;
    for (i = 0; i + 64 <= size; i += 64) {
        sha1Block(&c, data + i);
    }
    memcpy(c.buf, data + i, size - i);
    c.n = size - i;
    c.buf[c.n++] = 0x80;
    if (c.n > 56) {
        memset(c.buf + c.n, 0, 64 - c.n);
        sha1Block(&c, c.buf);
        c.n = 0;
    }
    memset(c.buf + c.n, 0, 56 - c.n);
    for (i = 0; i < 8; i++) {
        c.buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha1Block(&c, c.buf);
    for (i = 0; i < 5; i++) {
        out[i * 4] = (uint8_t)(c.h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c.h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c.h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)c.h[i];
    }
}

/* --- the versions: their SHA-1 (as the decomp's ver/<v>/checksum.sha1) and
 * where each segment is */

typedef struct {
    int version;
    const char* sha1;
    uint32_t sprites;          /* the sprite section (its size is in its header) */
    uint32_t msg[4][2];        /* message archives: English (or the only one), German, French, Spanish */
    uint32_t audio[2];         /* the SBN */
} RomLayout;

static const RomLayout kLayouts[] = {
    {PM64_US, "3837f44cda784b466c9a2d99df70d77c322b97a0", 0x1943000,
     {{0x1B83000, 0x1C84D30}, {0, 0}, {0, 0}, {0, 0}}, {0xF00000, 0x1942C40}},
    {PM64_JP, "b9cca3ff260b9ff427d981626b82f96de73586d3", 0x1A40000,
     {{0x1D40000, 0x1DDBE60}, {0, 0}, {0, 0}, {0, 0}}, {0xFC0000, 0x1A01AF0}},
    {PM64_PAL, "2111d39265a317414d359e35a7d971c4dfa5f9e1", 0x1DF0000,
     {{0x2030000, 0x21B0000}, {0x21B0000, 0x2330000}, {0x2330000, 0x24B0000}, {0x24B0000, 0x2600000}},
     {0x13A0000, 0x1DE2C40}},
    {PM64_IQUE, "5c724685085eba796537573dd6f84aaddedc8582", 0x1943000,
     {{0x1B83000, 0x1C3CDA0}, {0, 0}, {0, 0}, {0, 0}}, {0xF00000, 0x1942C40}},
};

/* the item icon archive (the decomp's pm_icons segment): its start per version (kLayouts' order), the
 * same size in all four */
static const uint32_t kIconArchive[4] = {0x1CC310, 0x1D4720, 0x1DA1E0, 0x215390};
#define ICON_ARCHIVE_SIZE 0x31EA0
static const uint32_t kMsgTags[4] = {PM64_SEG_MESSAGES, PM64_SEG_MESSAGES_DE, PM64_SEG_MESSAGES_FR,
                                     PM64_SEG_MESSAGES_ES};

const char* portPm64VersionName(int version) {
    switch (version) {
    case PM64_US:
        return "US";
    case PM64_JP:
        return "Japan";
    case PM64_PAL:
        return "Europe";
    case PM64_IQUE:
        return "iQue";
    default:
        return "";
    }
}

/* --- the cache: "PCPM64C\0", format, version, segment count, then per
 * segment tag, offset, size (little-endian u32s), then the segments */

#define MAX_SEGS 12
static struct {
    int read; /* the index was looked for */
    int version;
    unsigned generation;
    int count;
    struct {
        uint32_t tag, offset, size;
        uint8_t* data;
        int failed; /* couldn't be read: not tried again (until the link changes) */
    } seg[MAX_SEGS];
} sCache;

static void cachePath(char* out, size_t size, int dir) {
    snprintf(out, size, dir ? "%s/pm64" : "%s/pm64/rom_cache.bin", portSettingsUserPath());
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void forget(void);

/* Tests (docs/PC_PORT.md): TTYD_PM64_ROM=<file> links that ROM into the user
 * folder at the first look at the cache, as the settings page's picker would;
 * TTYD_PM64_ROM=unlink unlinks. */
static void envLink(void) {
    static int sDone;
    const char* rom;
    char err[256];
    int v;
    if (sDone) {
        return;
    }
    sDone = 1;
    if ((rom = getenv("TTYD_PM64_ROM")) == NULL || rom[0] == '\0') {
        return;
    }
    if (!strcmp(rom, "unlink")) {
        portPm64Unlink();
        printf("[pm64] TTYD_PM64_ROM: unlinked\n");
    } else if ((v = portPm64LinkFile(rom, err, sizeof(err))) != PM64_NONE) {
        printf("[pm64] TTYD_PM64_ROM: linked %s\n", portPm64VersionName(v));
    } else {
        printf("[pm64] TTYD_PM64_ROM: refused: %s\n", err);
    }
    fflush(stdout);
}

static void readIndex(void) {
    char path[1024];
    uint8_t head[20];
    FILE* f;
    long long fileSize;
    int i;
    envLink();
    if (sCache.read) {
        return;
    }
    sCache.read = 1;
    cachePath(path, sizeof(path), 0);
    if ((f = portFopen(path, "rb")) == NULL) {
        return;
    }
    fileSize = portFileSize(path);
    if (fread(head, 1, 20, f) == 20 && !memcmp(head, kCacheMagic, sizeof(kCacheMagic)) && get32(head + 8) == PM64_CACHE_FORMAT &&
        get32(head + 16) <= MAX_SEGS) {
        sCache.version = (int)get32(head + 12);
        sCache.count = (int)get32(head + 16);
        for (i = 0; i < sCache.count; i++) {
            uint8_t e[12];
            if (fread(e, 1, 12, f) != 12) {
                sCache.version = PM64_NONE;
                sCache.count = 0;
                break;
            }
            sCache.seg[i].tag = get32(e);
            sCache.seg[i].offset = get32(e + 4);
            sCache.seg[i].size = get32(e + 8);
            sCache.seg[i].data = NULL;
            sCache.seg[i].failed = 0;
            /* a truncated or damaged cache (a full disk, a hand edit): every part inside the file, past the
             * index, or it isn't taken as linked (a part past the file's end would ask for gigabytes at each
             * look) */
            if ((long long)sCache.seg[i].offset < 20 + 12 * (long long)sCache.count ||
                (long long)sCache.seg[i].offset + (long long)sCache.seg[i].size > fileSize) {
                printf("[pm64] cache: part %d lies outside the file (%lld bytes): link the ROM again\n", i,
                       fileSize);
                fflush(stdout);
                sCache.version = PM64_NONE;
                sCache.count = 0;
                break;
            }
        }
    }
    fclose(f);
    if (sCache.version < PM64_US || sCache.version > PM64_IQUE) {
        sCache.version = PM64_NONE;
        sCache.count = 0;
    } else {
        printf("[pm64] cache: Paper Mario (%s), %d parts\n", portPm64VersionName(sCache.version), sCache.count);
        fflush(stdout);
    }
}

int portPm64Linked(void) {
    readIndex();
    return sCache.version;
}

unsigned portPm64Generation(void) {
    return sCache.generation;
}

const uint8_t* portPm64Segment(uint32_t tag, size_t* size) {
    int i;
    readIndex();
    for (i = 0; i < sCache.count; i++) {
        if (sCache.seg[i].tag != tag) {
            continue;
        }
        if (sCache.seg[i].data == NULL && sCache.seg[i].size > 0 && !sCache.seg[i].failed) {
            char path[1024];
            FILE* f;
            uint8_t* d = (uint8_t*)malloc(sCache.seg[i].size);
            cachePath(path, sizeof(path), 0);
            if (d != NULL && (f = portFopen(path, "rb")) != NULL) {
                if (fseek(f, (long)sCache.seg[i].offset, SEEK_SET) == 0 &&
                    fread(d, 1, sCache.seg[i].size, f) == sCache.seg[i].size) {
                    sCache.seg[i].data = d;
                    d = NULL;
                }
                fclose(f);
            }
            if (d != NULL) {
                sCache.seg[i].failed = 1; /* (not opened and read again on every call) */
                printf("[pm64] cache: part %d couldn't be read\n", i);
                fflush(stdout);
            }
            free(d);
        }
        if (size != NULL) {
            *size = sCache.seg[i].data != NULL ? sCache.seg[i].size : 0;
        }
        return sCache.seg[i].data;
    }
    if (size != NULL) {
        *size = 0;
    }
    return NULL;
}

/* --- linking */

/* any byte order to big-endian (.z64) in place; 0 if it isn't an N64 ROM */
static int toBigEndian(uint8_t* rom, size_t size) {
    size_t i;
    if (size < 0x1000 || (size & 3) != 0) {
        return 0;
    }
    if (rom[0] == 0x80 && rom[1] == 0x37 && rom[2] == 0x12 && rom[3] == 0x40) {
        return 1;
    }
    if (rom[0] == 0x37 && rom[1] == 0x80 && rom[2] == 0x40 && rom[3] == 0x12) { /* .v64: 16-bit swapped */
        for (i = 0; i < size; i += 2) {
            const uint8_t t = rom[i];
            rom[i] = rom[i + 1];
            rom[i + 1] = t;
        }
        return 1;
    }
    if (rom[0] == 0x40 && rom[1] == 0x12 && rom[2] == 0x37 && rom[3] == 0x80) { /* .n64: little-endian words */
        for (i = 0; i < size; i += 4) {
            uint8_t t = rom[i];
            rom[i] = rom[i + 3];
            rom[i + 3] = t;
            t = rom[i + 1];
            rom[i + 1] = rom[i + 2];
            rom[i + 2] = t;
        }
        return 1;
    }
    /* the iQue's image has no N64 boot header word: known by its hash alone */
    return 2;
}

/* gMoveTable: found by its Headbonk 1 entry (name, flags, short description
 * PartnerDesc_Goombario_Battle_Headbonk 0x280002, full PartnerDesc_Goombario_
 * Pause_Headbonk1 0x280007), entry 0x83 of 0xB9, 0x14 bytes each */
#define MOVE_SIZE 0x14
#define MOVE_COUNT 0xB9
#define MOVE_HEADBONK1 0x83
static long findMoves(const uint8_t* rom, size_t size) {
    size_t at;
    for (at = 8; at + 16 <= size; at += 4) {
        if (be32(rom + at) == 0x00280002 && be32(rom + at + 4) == 0x00280007 &&
            (be32(rom + at - 8) >> 16) == 0x2B) {
            const size_t entry = at - 8;
            if (entry >= (size_t)MOVE_HEADBONK1 * MOVE_SIZE &&
                entry - (size_t)MOVE_HEADBONK1 * MOVE_SIZE + (size_t)MOVE_COUNT * MOVE_SIZE <= size) {
                return (long)(entry - (size_t)MOVE_HEADBONK1 * MOVE_SIZE);
            }
        }
    }
    return -1;
}

/* gItemTable: 0x16D entries of 0x20 bytes, found by what a few of Tayce T.'s dishes restore (potencyA/B at
 * +0x1B/+0x1C: Mushroom 5 HP, Deluxe Feast 40/40, Yummy Meal 20/20, Koopa Tea 7 FP, Jelly Pop 64 FP) and their
 * names being in one message section */
#define ITEM_SIZE 0x20
#define ITEM_COUNT 0x16D
static long findItems(const uint8_t* rom, size_t size) {
    static const struct {
        int index, a, b;
    } kSig[] = {{0x8A, 5, 0}, {0xBE, 40, 40}, {0xBB, 20, 20}, {0xC3, 0, 7}, {0xD1, 0, 64}};
    size_t at;
    for (at = 0; at + ITEM_COUNT * ITEM_SIZE <= size; at += 4) {
        size_t k;
        const uint32_t sec = be32(rom + at + 0xC3 * ITEM_SIZE) >> 16;
        if (sec == 0) {
            continue;
        }
        for (k = 0; k < sizeof(kSig) / sizeof(kSig[0]); k++) {
            const uint8_t* e = rom + at + (size_t)kSig[k].index * ITEM_SIZE;
            if ((int8_t)e[0x1B] != kSig[k].a || (int8_t)e[0x1C] != kSig[k].b || (be32(e) >> 16) != sec) {
                break;
            }
        }
        if (k == sizeof(kSig) / sizeof(kSig[0])) {
            return (long)at;
        }
    }
    return -1;
}

int portPm64LinkRom(void* romData, size_t size, char* err, size_t errSize) {
    uint8_t* rom = (uint8_t*)romData;
    const RomLayout* L = NULL;
    uint8_t digest[20];
    char hex[41];
    char path[1024], tmp[1100];
    struct {
        uint32_t tag, from, size;
    } segs[MAX_SEGS];
    int n = 0, i;
    uint32_t at;
    FILE* f;
    uint8_t head[20];
    long moves;

    if (err != NULL && errSize > 0) {
        err[0] = '\0';
    }
    if (rom == NULL || !toBigEndian(rom, size)) {
        snprintf(err, errSize, "That file isn't an N64 ROM.");
        return PM64_NONE;
    }
    sha1(rom, size, digest);
    for (i = 0; i < 20; i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    for (i = 0; i < (int)(sizeof(kLayouts) / sizeof(kLayouts[0])); i++) {
        if (!strcmp(hex, kLayouts[i].sha1)) {
            L = &kLayouts[i];
        }
    }
    if (L == NULL) {
        snprintf(err, errSize,
                 "That isn't a Paper Mario ROM Paper Cut knows (US, Japan, Europe or iQue, unmodified). SHA-1 %s",
                 hex);
        return PM64_NONE;
    }
    /* the sprite section: its header's last offset is its size */
    if ((size_t)L->sprites + 0x20 > size) {
        snprintf(err, errSize, "The ROM is too short.");
        return PM64_NONE;
    }
    segs[n].tag = PM64_SEG_SPRITES;
    segs[n].from = L->sprites;
    segs[n].size = be32(rom + L->sprites + 0x1C) + 0x10;
    n++;
    for (i = 0; i < 4; i++) {
        if (L->msg[i][1] > L->msg[i][0]) {
            segs[n].tag = kMsgTags[i];
            segs[n].from = L->msg[i][0];
            segs[n].size = L->msg[i][1] - L->msg[i][0];
            n++;
        }
    }
    segs[n].tag = PM64_SEG_AUDIO;
    segs[n].from = L->audio[0];
    segs[n].size = L->audio[1] - L->audio[0];
    n++;
    if ((moves = findMoves(rom, size)) >= 0) {
        segs[n].tag = PM64_SEG_MOVES;
        segs[n].from = (uint32_t)moves;
        segs[n].size = MOVE_COUNT * MOVE_SIZE;
        n++;
    }
    if ((moves = findItems(rom, size)) >= 0) {
        segs[n].tag = PM64_SEG_ITEMS;
        segs[n].from = (uint32_t)moves;
        segs[n].size = ITEM_COUNT * ITEM_SIZE;
        n++;
    }
    if (L->version >= PM64_US && L->version <= PM64_IQUE) {
        segs[n].tag = PM64_SEG_ICONS;
        segs[n].from = kIconArchive[L->version - PM64_US];
        segs[n].size = ICON_ARCHIVE_SIZE;
        n++;
    }
    for (i = 0; i < n; i++) {
        if ((size_t)segs[i].from + segs[i].size > size) {
            snprintf(err, errSize, "The ROM is too short.");
            return PM64_NONE;
        }
    }

    /* written beside, then put in place */
    cachePath(path, sizeof(path), 1);
    pm64Mkdir(path);
    cachePath(path, sizeof(path), 0);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path); /* (portFileWriteOpen's) */
    if ((f = portFileWriteOpen(path, "wb")) == NULL) {
        snprintf(err, errSize, "The cache couldn't be written in the user folder.");
        return PM64_NONE;
    }
    memcpy(head, kCacheMagic, sizeof(kCacheMagic));
    put32(head + 8, PM64_CACHE_FORMAT);
    put32(head + 12, (uint32_t)L->version);
    put32(head + 16, (uint32_t)n);
    fwrite(head, 1, 20, f);
    at = 20 + 12 * (uint32_t)n;
    for (i = 0; i < n; i++) {
        uint8_t e[12];
        put32(e, segs[i].tag);
        put32(e + 4, at);
        put32(e + 8, segs[i].size);
        fwrite(e, 1, 12, f);
        at += segs[i].size;
    }
    for (i = 0; i < n; i++) {
        fwrite(rom + segs[i].from, 1, segs[i].size, f);
    }
    forget();
    /* (flushed, synced, the close checked, then renamed over the last one: a crash never leaves the link
       half gone) */
    if (!portFileWriteClose(f, path)) {
        portRemove(tmp);
        snprintf(err, errSize, "The cache couldn't be written in the user folder (is it full?).");
        sCache.read = 0;
        return PM64_NONE;
    }
    sCache.read = 0;
    return portPm64Linked();
}

int portPm64LinkFile(const char* path, char* err, size_t errSize) {
    FILE* f = portFopen(path, "rb");
    long size;
    uint8_t* rom;
    int v = PM64_NONE;
    if (f == NULL) {
        snprintf(err, errSize, "That file couldn't be opened.");
        return PM64_NONE;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 64L * 1024 * 1024 || /* (the largest Paper Mario ROM: 40 MB) */ (rom = (uint8_t*)malloc((size_t)size)) == NULL) {
        fclose(f);
        snprintf(err, errSize, "That file isn't an N64 ROM.");
        return PM64_NONE;
    }
    if (fread(rom, 1, (size_t)size, f) == (size_t)size) {
        v = portPm64LinkRom(rom, (size_t)size, err, errSize);
    } else {
        snprintf(err, errSize, "That file couldn't be read.");
    }
    fclose(f);
    free(rom);
    return v;
}

static void spritesForget(void);

/* The audio segments under the songs playing (port/include/port/pm64.h, portPm64AudioHold), counted per
 * segment: an unlink or a new link while one plays (the audio thread renders it from the segment) leaves its
 * segment here, freed once the last song or player holding that segment lets go (the current segment's holds
 * never keep an old one). A spin lock: held for a few instructions, on the game and audio threads. */
#define AUDIO_BUFS 8
static char sAudioLock;
static struct {
    const void* data;
    int holds;
    int retired; /* forgotten: freed at its last hold's end */
} sAudioBuf[AUDIO_BUFS];

static void audioLock(void) {
    while (__atomic_test_and_set(&sAudioLock, __ATOMIC_ACQUIRE)) {
    }
}

static void audioUnlock(void) {
    __atomic_clear(&sAudioLock, __ATOMIC_RELEASE);
}

static void audioRetire(void* data) {
    int i;
    if (data == NULL) {
        return;
    }
    audioLock();
    for (i = 0; i < AUDIO_BUFS; i++) {
        if (sAudioBuf[i].data == data && sAudioBuf[i].holds > 0) {
            sAudioBuf[i].retired = 1;
            data = NULL; /* (freed when its holds end) */
            break;
        }
    }
    audioUnlock();
    free(data);
}

int portPm64AudioHold(const void* segment, int delta) {
    void* done = NULL;
    int i, at = -1;
    if (segment == NULL) {
        return 0;
    }
    audioLock();
    for (i = 0; i < AUDIO_BUFS; i++) {
        if (sAudioBuf[i].data == segment) {
            at = i;
            break;
        }
        if (at < 0 && sAudioBuf[i].data == NULL) {
            at = i;
        }
    }
    if (at >= 0 && sAudioBuf[at].data != segment && delta > 0) {
        sAudioBuf[at].data = segment; /* a free entry */
        sAudioBuf[at].holds = 0;
        sAudioBuf[at].retired = 0;
    }
    if (at < 0 || sAudioBuf[at].data != segment) {
        audioUnlock();
        return 0; /* (no entry free: refused, so nothing is ever freed under a hold) */
    }
    sAudioBuf[at].holds += delta;
    if (sAudioBuf[at].holds <= 0) {
        if (sAudioBuf[at].retired) {
            done = (void*)sAudioBuf[at].data;
        }
        memset(&sAudioBuf[at], 0, sizeof(sAudioBuf[at]));
    }
    audioUnlock();
    free(done);
    return 1;
}

static void forget(void) {
    int i;
    spritesForget();
    for (i = 0; i < sCache.count; i++) {
        if (sCache.seg[i].tag == PM64_SEG_AUDIO) {
            audioRetire(sCache.seg[i].data);
            continue;
        }
        free(sCache.seg[i].data);
    }
    memset(sCache.seg, 0, sizeof(sCache.seg));
    sCache.count = 0;
    sCache.version = PM64_NONE;
    sCache.read = 0;
    sCache.generation++;
}

void portPm64Unlink(void) {
    char path[1024];
    forget();
    cachePath(path, sizeof(path), 0);
    portRemove(path);
    sCache.read = 1; /* (nothing to read now) */
}

/* --- Yay0: "Yay0", decoded size, link table offset, chunk offset; then mask
 * words from 0x10: each bit (high first) 1 copies a chunk byte, 0 a back
 * reference from the link table (12-bit distance - 1; 4-bit length - 2, or
 * 0 and a chunk byte + 0x12) */

size_t portPm64Yay0Size(const void* src, size_t srcSize) {
    const uint8_t* s = (const uint8_t*)src;
    if (srcSize < 16 || memcmp(s, "Yay0", 4) != 0) {
        return 0;
    }
    return be32(s + 4);
}

int portPm64Yay0Decode(const void* src, size_t srcSize, void* dst, size_t dstSize) {
    const uint8_t* s = (const uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    size_t out = 0, mp = 16, link, chunk, total;
    uint32_t mask = 0;
    int bits = 0;
    if ((total = portPm64Yay0Size(src, srcSize)) == 0 || total > dstSize) {
        return 0;
    }
    link = be32(s + 8);
    chunk = be32(s + 12);
    while (out < total) {
        if (bits == 0) {
            if (mp + 4 > srcSize) {
                return 0;
            }
            mask = be32(s + mp);
            mp += 4;
            bits = 32;
        }
        if (mask & 0x80000000u) {
            if (chunk >= srcSize) {
                return 0;
            }
            d[out++] = s[chunk++];
        } else {
            size_t dist, len;
            uint16_t v;
            if (link + 2 > srcSize) {
                return 0;
            }
            v = be16(s + link);
            link += 2;
            dist = (size_t)(v & 0xFFF) + 1;
            len = v >> 12;
            if (len == 0) {
                if (chunk >= srcSize) {
                    return 0;
                }
                len = (size_t)s[chunk++] + 0x12;
            } else {
                len += 2;
            }
            if (dist > out) {
                return 0;
            }
            for (; len > 0 && out < total; len--, out++) {
                d[out] = d[out - dist];
            }
        }
        mask <<= 1;
        bits--;
    }
    /* the rest of a bigger dst zeroed: all dstSize bytes are defined (the
     * analyzer saw a sprite's header read from past a short decode) */
    memset(d + total, 0, dstSize - total);
    return 1;
}

/* --- NPC sprites: the sprite section's header (a build date, then offsets
 * from 0x10 to the player rasters, the player sprites, the NPC sprites and
 * the end); the NPC part is a table of offsets (n + 1 of them) to each
 * sprite's Yay0. A sprite decoded: offsets to its raster list and palette
 * list, its most components, its palette groups, then its animations' list
 * (offsets, -1 ended); a raster: data offset, width, height, palette, 0xFF
 * (CI4, high nibble first); a palette: 16 RGBA5551; an animation: a list of
 * its components (-1 ended); a component: its commands' offset, their size
 * in bytes, its x, y, z. */

#define MAX_SPRITES 0x100
#define MAX_RASTERS 256
#define MAX_PALETTES 64

struct Pm64Sprite {
    uint8_t* data;
    size_t size;
    int rasterCount, paletteCount, animCount;
    uint32_t raster[MAX_RASTERS];
    uint32_t palette[MAX_PALETTES];
    uint32_t anim[256];
    void** tex; /* rasterCount x (paletteCount + 1): made textures */
};

static Pm64Sprite* sSprites[MAX_SPRITES];

/* Cool Mode's eyes on a sprite's raster (the glasses go over them): blobs of near black or of white inside
 * the outline (pupils, eye slits, eyeballs); the pair side by side are the eyes, else the one with a glint
 * (a face in profile). Kept per raster: a raster's pixels never change. */
#define EYE_CACHE 512
typedef struct EyeEntry {
    const Pm64Sprite* s;
    int raster, found;
    float x, y, width, score;
} EyeEntry;
static EyeEntry sEyes[EYE_CACHE];

typedef struct EyeBlob {
    float x, y;
    int n, w, h, dark, glint;
} EyeBlob;

static int eyeLum(const uint8_t* p) {
    return (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
}

static int eyeInside(const uint8_t* px, int w, int h, int x, int y) {
    int dx, dy;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            if (x + dx < 0 || x + dx >= w || y + dy < 0 || y + dy >= h ||
                px[((size_t)(y + dy) * w + x + dx) * 4 + 3] < 128) {
                return 0;
            }
        }
    }
    return 1;
}

static void findEyes(const uint8_t* px, int w, int h, EyeEntry* e) {
    uint8_t* kind = calloc((size_t)w * h, 1); /* 1 dark, 2 white (inside the outline); +2 once seen */
    int* stack = malloc(sizeof(int) * (size_t)w * h);
    EyeBlob blob[32];
    int nb = 0, total = 0, x, y, i, j, top = h, bottom = -1;
    float best = 0.0f, hh;
    if (kind == NULL || stack == NULL) {
        free(kind);
        free(stack);
        return;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const uint8_t* p = px + ((size_t)y * w + x) * 4;
            int l, mx, mn;
            if (p[3] < 128) {
                continue;
            }
            total++;
            top = y < top ? y : top;
            bottom = y > bottom ? y : bottom;
            if (!eyeInside(px, w, h, x, y)) {
                continue;
            }
            l = eyeLum(p);
            mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
            mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
            if (l <= 56) {
                kind[y * w + x] = 1;
            } else if (l >= 200 && mx - mn < 60) {
                kind[y * w + x] = 2;
            }
        }
    }
    for (y = 0; y < h && nb < 32; y++) {
        for (x = 0; x < w && nb < 32; x++) {
            const int k = kind[y * w + x];
            int sp = 0, n = 0, x0 = x, x1 = x, y0 = y, y1 = y, bw, bh;
            float sx = 0.0f, sy = 0.0f;
            if (k == 0 || k > 2) {
                continue;
            }
            stack[sp++] = y * w + x;
            kind[y * w + x] = (uint8_t)(k + 2);
            while (sp > 0) {
                const int at = stack[--sp], ax = at % w, ay = at / w;
                const int nx[4] = {ax + 1, ax - 1, ax, ax}, ny[4] = {ay, ay, ay + 1, ay - 1};
                n++;
                sx += (float)ax;
                sy += (float)ay;
                x0 = ax < x0 ? ax : x0;
                x1 = ax > x1 ? ax : x1;
                y0 = ay < y0 ? ay : y0;
                y1 = ay > y1 ? ay : y1;
                for (i = 0; i < 4; i++) {
                    if (nx[i] >= 0 && nx[i] < w && ny[i] >= 0 && ny[i] < h && kind[ny[i] * w + nx[i]] == k) {
                        kind[ny[i] * w + nx[i]] = (uint8_t)(k + 2);
                        stack[sp++] = ny[i] * w + nx[i];
                    }
                }
            }
            bw = x1 - x0 + 1;
            bh = y1 - y0 + 1;
            /* not a speck, not a line (a mouth, a shell's seam), not a big patch (a shell, a belly) */
            if (n < 2 || n * 100 < 35 * bw * bh || bw > 3 * bh || bh > 4 * bw || n * 100 > 8 * total ||
                bh * 10 > 4 * (bottom - top + 1)) {
                continue;
            }
            blob[nb].x = sx / (float)n + 0.5f;
            blob[nb].y = sy / (float)n + 0.5f;
            blob[nb].n = n;
            blob[nb].w = bw;
            blob[nb].h = bh;
            blob[nb].dark = k == 1;
            blob[nb].glint = 0;
            nb++;
        }
    }
    /* an eye: a dark blob with a white one in or against it (a glint, or the eyeball round a pupil) */
    for (i = 0; i < nb; i++) {
        for (j = 0; j < nb; j++) {
            if (i != j && blob[i].dark != blob[j].dark &&
                fabsf(blob[i].x - blob[j].x) <= (float)(blob[i].w + blob[j].w) * 0.5f + 1.0f &&
                fabsf(blob[i].y - blob[j].y) <= (float)(blob[i].h + blob[j].h) * 0.5f + 1.0f) {
                blob[i].glint = 1;
            }
        }
    }
    hh = (float)(bottom - top + 1);
    for (i = 0; i < nb; i++) {
        const EyeBlob* a = &blob[i];
        const float up = 1.6f - (a->y - (float)top) / hh; /* the eyes are above the mouth */
        float s;
        if (!a->dark || a->y > (float)top + 0.75f * hh) {
            continue;
        }
        s = (float)a->n * up * (a->glint ? 2.5f : 0.6f); /* one eye: a face in profile */
        if (s > best) {
            best = s;
            e->found = 1;
            e->x = a->x;
            e->y = a->y;
            e->width = (float)a->w * 3.0f;
        }
        for (j = 0; j < nb; j++) { /* a pair side by side */
            const EyeBlob* b = &blob[j];
            const float dx = b->x - a->x, dy = b->y - a->y;
            float span;
            if (j == i || !b->dark || dx <= 0.0f || dx > 0.6f * (float)w || fabsf(dy) > 0.5f * dx ||
                dx < 0.8f * (float)(a->w + b->w) * 0.5f || a->n > 3 * b->n || b->n > 3 * a->n) {
                continue;
            }
            s = sqrtf((float)(a->n * b->n)) * up * 2.0f * (float)(1 + a->glint + b->glint);
            if (s > best) {
                best = s;
                span = (dx + (float)(a->w + b->w) * 0.5f) * 1.2f;
                e->found = 1;
                e->x = (a->x + b->x) * 0.5f;
                e->y = (a->y + b->y) * 0.5f;
                e->width = dx * 2.0f > span ? dx * 2.0f : span;
            }
        }
    }
    e->score = best;
    free(kind);
    free(stack);
}

int portPm64SpriteEyes(const Pm64Sprite* s, int raster, float* x, float* y, float* width, float* score) {
    EyeEntry* e;
    int w, h;
    if (s == NULL || !portPm64SpriteRaster(s, raster, &w, &h, NULL) || w <= 0 || h <= 0) {
        return 0;
    }
    e = &sEyes[(((size_t)s >> 4) * 31u + (size_t)raster) % EYE_CACHE];
    if (e->s != s || e->raster != raster) {
        uint8_t* px = malloc((size_t)w * h * 4);
        memset(e, 0, sizeof(*e));
        e->s = s;
        e->raster = raster;
        if (px != NULL && portPm64SpriteRGBA(s, raster, -1, px)) {
            findEyes(px, w, h, e);
        }
        free(px);
    }
    if (!e->found) {
        return 0;
    }
    *x = e->x / (float)w;
    *y = e->y / (float)h;
    *width = e->width / (float)w;
    *score = e->score;
    return 1;
}

static void spritesForget(void) {
    int i, j;
    for (i = 0; i < MAX_SPRITES; i++) {
        Pm64Sprite* s = sSprites[i];
        if (s == NULL) {
            continue;
        }
        /* The textures may still be in a queued draw: kept (a link or unlink
         * is rare, and a few hundred KB). */
        (void)j;
        free(s->data);
        free(s);
        sSprites[i] = NULL;
    }
    memset(sEyes, 0, sizeof(sEyes)); /* a freed sprite's address may come again */
}

static const uint8_t* npcTable(uint32_t* count, size_t* tableSize) {
    size_t size;
    const uint8_t* spr = portPm64Segment(PM64_SEG_SPRITES, &size);
    size_t npc, end, first;
    if (spr == NULL || size < 0x20) {
        return NULL;
    }
    npc = (size_t)be32(spr + 0x18) + 0x10; /* (size_t: a damaged cache's offsets can't wrap) */
    end = (size_t)be32(spr + 0x1C) + 0x10;
    if (npc + 4 > size || end > size || end <= npc) {
        return NULL;
    }
    *tableSize = end - npc;
    first = be32(spr + npc); /* the first offset is the table's own size */
    if (first < 8 || first > *tableSize) {
        return NULL;
    }
    *count = (uint32_t)(first / 4 - 1); /* (each entry and the next inside the table) */
    return spr + npc;
}

int portPm64NpcSpriteCount(void) {
    uint32_t n;
    size_t t;
    return npcTable(&n, &t) != NULL ? (int)(n < MAX_SPRITES ? n : MAX_SPRITES) : 0;
}

static int readList(const Pm64Sprite* s, uint32_t at, uint32_t* out, int max) {
    int n = 0;
    while ((size_t)at + 4 <= s->size && n < max) {
        const uint32_t v = be32(s->data + at);
        if (v == 0xFFFFFFFFu) {
            break;
        }
        out[n++] = v;
        at += 4;
    }
    return n;
}

/* s's lists read from its decoded data; 0 (nothing kept) if they don't fit */
static int parseSprite(Pm64Sprite* s) {
    const size_t dsize = s->size;
    int i, ok = dsize >= 0x14;
    if (ok) {
        s->rasterCount = readList(s, be32(s->data), s->raster, MAX_RASTERS);
        s->paletteCount = readList(s, be32(s->data + 4), s->palette, MAX_PALETTES);
        s->animCount = readList(s, 0x10, s->anim, 256);
        for (i = 0; i < s->rasterCount; i++) {
            if ((size_t)s->raster[i] + 8 > dsize) {
                ok = 0;
            }
        }
        for (i = 0; i < s->paletteCount; i++) {
            if ((size_t)s->palette[i] + 32 > dsize) {
                ok = 0;
            }
        }
        s->tex = (void**)calloc((size_t)s->rasterCount * (size_t)(s->paletteCount + 1) + 1, sizeof(void*));
        ok = ok && s->tex != NULL;
    }
    return ok;
}

/* A sprite of the same layout made elsewhere (the player's, port/src/common/pm64_player_pc.c): data
 * (malloc'd, decoded, its rasters' pixel offsets into itself) taken and kept for good; NULL (data freed)
 * if it doesn't parse. */
const Pm64Sprite* portPm64SpriteAdopt(uint8_t* data, size_t size) {
    Pm64Sprite* s = (Pm64Sprite*)calloc(1, sizeof(Pm64Sprite));
    if (s == NULL || data == NULL) {
        free(s);
        free(data);
        return NULL;
    }
    s->data = data;
    s->size = size;
    if (!parseSprite(s)) {
        free(s->tex);
        free(s->data);
        free(s);
        return NULL;
    }
    return s;
}

const Pm64Sprite* portPm64NpcSprite(int index) {
    uint32_t n, a, b;
    size_t tableSize, dsize;
    const uint8_t* t;
    Pm64Sprite* s;
    int ok = 1;
    if (index < 0 || index >= MAX_SPRITES) {
        return NULL;
    }
    if (sSprites[index] != NULL) {
        return sSprites[index];
    }
    if ((t = npcTable(&n, &tableSize)) == NULL || (uint32_t)index >= n) {
        return NULL;
    }
    a = be32(t + index * 4);
    b = be32(t + index * 4 + 4);
    if (b <= a || b > tableSize || (dsize = portPm64Yay0Size(t + a, b - a)) == 0 || dsize > 4u * 1024 * 1024) {
        return NULL;
    }
    if ((s = (Pm64Sprite*)calloc(1, sizeof(Pm64Sprite))) == NULL || (s->data = (uint8_t*)malloc(dsize)) == NULL) {
        free(s);
        return NULL;
    }
    s->size = dsize;
    if (!portPm64Yay0Decode(t + a, b - a, s->data, dsize) || dsize < 0x14) {
        ok = 0;
    }
    ok = ok && parseSprite(s);
    if (!ok) {
        free(s->tex);
        free(s->data);
        free(s);
        return NULL;
    }
    sSprites[index] = s;
    return s;
}

int portPm64SpriteAnimCount(const Pm64Sprite* s) {
    return s != NULL ? s->animCount : 0;
}

int portPm64SpriteRasterCount(const Pm64Sprite* s) {
    return s != NULL ? s->rasterCount : 0;
}

int portPm64SpritePaletteCount(const Pm64Sprite* s) {
    return s != NULL ? s->paletteCount : 0;
}

int portPm64SpriteRaster(const Pm64Sprite* s, int i, int* width, int* height, int* palette) {
    const uint8_t* r;
    if (s == NULL || i < 0 || i >= s->rasterCount) {
        return 0;
    }
    r = s->data + s->raster[i];
    if (width != NULL) {
        *width = r[4];
    }
    if (height != NULL) {
        *height = r[5];
    }
    if (palette != NULL) {
        *palette = r[6];
    }
    return 1;
}

int portPm64SpriteRGBA(const Pm64Sprite* s, int i, int pal, uint8_t* out) {
    const uint8_t* r;
    const uint8_t* pixels;
    const uint8_t* p;
    uint8_t rgba[16][4];
    int w, h, k, x, y;
    if (!portPm64SpriteRaster(s, i, &w, &h, NULL)) {
        return 0;
    }
    r = s->data + s->raster[i];
    if (pal < 0 || pal >= s->paletteCount) {
        pal = r[6];
    }
    if (pal >= s->paletteCount || be32(r) + (size_t)(w * h + 1) / 2 > s->size) {
        return 0;
    }
    p = s->data + s->palette[pal];
    for (k = 0; k < 16; k++) {
        const unsigned v = be16(p + k * 2);
        rgba[k][0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
        rgba[k][1] = (uint8_t)(((v >> 6) & 31) * 255 / 31);
        rgba[k][2] = (uint8_t)(((v >> 1) & 31) * 255 / 31);
        rgba[k][3] = (v & 1) ? 255 : 0;
    }
    pixels = s->data + be32(r);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const int at = y * w + x;
            const int c = (at & 1) ? (pixels[at >> 1] & 15) : (pixels[at >> 1] >> 4);
            memcpy(out + (size_t)at * 4, rgba[c], 4);
        }
    }
    return 1;
}

const void* portPm64RasterTexture(const Pm64Sprite* cs, int i, int pal, int* texW, int* texH) {
    Pm64Sprite* s = (Pm64Sprite*)cs;
    int w, h, tw, th, slot, tx, ty, x, y;
    uint8_t* rgba;
    uint8_t* out;
    if (!portPm64SpriteRaster(s, i, &w, &h, NULL) || w == 0 || h == 0) {
        return NULL;
    }
    if (pal < 0 || pal >= s->paletteCount) {
        pal = -1;
    }
    tw = (w + 3) / 4;
    th = (h + 3) / 4;
    if (texW != NULL) {
        *texW = tw * 4;
    }
    if (texH != NULL) {
        *texH = th * 4;
    }
    slot = i * (s->paletteCount + 1) + (pal + 1);
    if (s->tex[slot] != NULL) {
        return s->tex[slot];
    }
    rgba = (uint8_t*)malloc((size_t)w * h * 4);
    out = (uint8_t*)calloc((size_t)tw * th, 64);
    if (rgba == NULL || out == NULL || !portPm64SpriteRGBA(s, i, pal, rgba)) {
        free(rgba);
        free(out);
        return NULL;
    }
    /* GX RGBA8: each 4x4 tile is 32 bytes of alpha-red pairs, then 32 of green-blue */
    for (ty = 0; ty < th; ty++) {
        for (tx = 0; tx < tw; tx++) {
            uint8_t* tile = out + ((size_t)ty * tw + tx) * 64;
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    const int px = tx * 4 + x, py = ty * 4 + y, k = y * 4 + x;
                    if (px < w && py < h) {
                        const uint8_t* p = rgba + ((size_t)py * w + px) * 4;
                        tile[k * 2] = p[3];
                        tile[k * 2 + 1] = p[0];
                        tile[32 + k * 2] = p[1];
                        tile[32 + k * 2 + 1] = p[2];
                    }
                }
            }
        }
    }
    free(rgba);
    s->tex[slot] = out;
    return out;
}

/* --- animations */

static int sext12(unsigned v) {
    v &= 0xFFF;
    return (v & 0x800) ? (int)v - 0x1000 : (int)v;
}

static int sext16(unsigned v) {
    v &= 0xFFFF;
    return (v & 0x8000) ? (int)v - 0x10000 : (int)v;
}

/* component c of the animation: its commands and their count */
static const uint8_t* compCommands(const Pm64Sprite* s, int anim, int c, int* count, int* bx, int* by, int* bz) {
    uint32_t comps[PM64_MAX_PARTS];
    int n;
    const uint8_t* e;
    uint32_t at, bytes;
    if (s == NULL || anim < 0 || anim >= s->animCount) {
        return NULL;
    }
    n = readList(s, s->anim[anim], comps, PM64_MAX_PARTS);
    if (c < 0 || c >= n || (size_t)comps[c] + 12 > s->size) {
        return NULL;
    }
    e = s->data + comps[c];
    at = be32(e);
    bytes = be16(e + 4);
    if ((size_t)at + bytes > s->size) {
        return NULL;
    }
    *count = (int)(bytes / 2);
    *bx = sext16(be16(e + 6));
    *by = sext16(be16(e + 8));
    *bz = sext16(be16(e + 10));
    return s->data + at;
}

void portPm64AnimStart(Pm64Anim* a, const Pm64Sprite* s, int anim) {
    uint32_t comps[PM64_MAX_PARTS];
    int i;
    memset(a, 0, sizeof(*a));
    a->sprite = s;
    a->anim = anim;
    if (s == NULL || anim < 0 || anim >= s->animCount) {
        return;
    }
    a->count = readList(s, s->anim[anim], comps, PM64_MAX_PARTS);
    for (i = 0; i < a->count; i++) {
        Pm64Part* p = &a->part[i];
        int n, bx = 0, by = 0, bz = 0;
        compCommands(s, anim, i, &n, &bx, &by, &bz);
        p->raster = -1;
        p->palette = -1;
        p->parent = -1;
        p->sx = p->sy = p->sz = 1.0f;
        p->bx = (float)bx;
        p->by = (float)by;
        p->bz = (float)bz;
    }
    portPm64AnimStep(a, 0.0f);
}

static void stepPart(Pm64Anim* a, int i, float frames) {
    Pm64Part* p = &a->part[i];
    int n, bx, by, bz, gotoAt = -1, guard = 0;
    float px = 0, py = 0, pz = 0, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1;
    int changed = 0;
    const uint8_t* cmd = compCommands(a->sprite, a->anim, i, &n, &bx, &by, &bz);
    if (cmd == NULL || n <= 0) {
        return;
    }
    p->wait -= frames;
    while (p->wait <= 0.0f && guard++ < 1024) {
        unsigned v;
        if (p->pc < 0 || p->pc >= n) {
            p->pc = 0;
            break;
        }
        v = be16(cmd + p->pc * 2);
        switch (v & 0xF000) {
        case 0x0000: /* wait */
            p->pc++;
            p->wait = (float)(v & 0xFFF);
            if (p->wait == 0.0f) {
                p->wait = 4095.0f;
            }
            p->ox = p->oy = p->oz = 0.0f;
            p->rx = p->ry = p->rz = 0.0f;
            p->sx = p->sy = p->sz = 1.0f;
            break;
        case 0x1000: /* the raster (0xFFF: none) */
            p->pc++;
            p->raster = (v & 0xFFF) == 0xFFF ? -1 : (int)(v & 0xFFF);
            p->palette = -1;
            break;
        case 0x2000: /* go to */
            p->pc = sext12(v);
            if (p->pc == gotoAt) {
                p->pc = 0;
                p->wait = 1.0f;
            }
            gotoAt = p->pc;
            break;
        case 0x3000: /* position */
            p->pc++;
            if ((v & 0xF) <= 1 && p->pc + 3 <= n) {
                px = (float)sext16(be16(cmd + p->pc * 2));
                py = (float)sext16(be16(cmd + p->pc * 2 + 2));
                pz = (float)sext16(be16(cmd + p->pc * 2 + 4));
                p->pc += 3;
                changed |= 1;
            }
            break;
        case 0x4000: /* rotation */
            if (p->pc + 3 > n) {
                p->pc = n;
                break;
            }
            rx = (float)sext12(v);
            ry = (float)sext16(be16(cmd + p->pc * 2 + 2));
            rz = (float)sext16(be16(cmd + p->pc * 2 + 4));
            p->pc += 3;
            changed |= 2;
            break;
        case 0x5000: /* scale, percent */
            p->pc++;
            if (p->pc < n) {
                const float k = be16(cmd + p->pc * 2) / 100.0f;
                switch (v & 0xF) {
                case 0:
                    sx = sy = sz = k;
                    break;
                case 1:
                    sx = k;
                    break;
                case 2:
                    sy = k;
                    break;
                case 3:
                    sz = k;
                    break;
                }
                p->pc++;
                changed |= 4;
            }
            break;
        case 0x6000: /* palette (0xFFF: the raster's) */
            p->pc++;
            p->palette = (v & 0xFFF) == 0xFFF ? -1 : (int)(v & 0xFFF);
            break;
        case 0x7000: /* loop: count, back to */
            if (p->loops != 0) {
                if (--p->loops == 0) {
                    p->pc += 2;
                    break;
                }
            } else if (p->pc + 1 < n) {
                p->loops = be16(cmd + p->pc * 2 + 2);
            }
            p->pc = sext12(v);
            break;
        case 0x8000: /* properties: 0x81XX parent XX, 0x82XX a notify value */
            p->pc++;
            if ((v & 0xF00) == 0x100) {
                p->parent = (int)(v & 0xFF);
            }
            break;
        default:
            p->pc = 0;
            p->wait = 1.0f;
            break;
        }
    }
    if (changed & 1) {
        p->ox = px;
        p->oy = py;
        p->oz = pz;
    }
    if (changed & 2) {
        p->rx = rx;
        p->ry = ry;
        p->rz = rz;
    }
    if (changed & 4) {
        p->sx = sx;
        p->sy = sy;
        p->sz = sz;
    }
}

void portPm64AnimStep(Pm64Anim* a, float frames) {
    int i;
    float cx[PM64_MAX_PARTS], cy[PM64_MAX_PARTS], cz[PM64_MAX_PARTS];
    if (a->sprite == NULL) {
        return;
    }
    for (i = 0; i < a->count; i++) {
        stepPart(a, i, frames);
    }
    /* where each is: its offset, its parent's added (a parent comes first) */
    for (i = 0; i < a->count; i++) {
        Pm64Part* p = &a->part[i];
        cx[i] = p->ox;
        cy[i] = p->oy;
        cz[i] = p->oz;
        if (p->parent >= 0 && p->parent < i) {
            cx[i] += cx[p->parent];
            cy[i] += cy[p->parent];
            cz[i] += cz[p->parent];
        }
        p->x = cx[i] + p->bx;
        p->y = cy[i] + p->by;
        p->z = cz[i] + p->bz;
    }
}

/* --- messages: a table of section offsets (0 ended); a section is a table
 * of message offsets ending where the section starts (its first offset is
 * its own); a message ends at 0xFD. Offsets are from the archive's start. */

const uint8_t* portPm64MessageRaw(uint32_t id, int lang, size_t* len) {
    size_t size, end;
    const uint8_t* m;
    const uint32_t sec = id >> 16, idx = id & 0xFFFF;
    uint32_t i, so, mo;
    if (lang < 0 || lang > 3 || (m = portPm64Segment(kMsgTags[lang], &size)) == NULL) {
        m = portPm64Segment(PM64_SEG_MESSAGES, &size);
    }
    if (m == NULL) {
        return NULL;
    }
    for (i = 0; i <= sec; i++) {
        if ((size_t)i * 4 + 4 > size || be32(m + i * 4) == 0) {
            return NULL;
        }
    }
    so = be32(m + sec * 4);
    if ((size_t)so + idx * 4 + 4 > size) {
        return NULL;
    }
    for (i = 0; i <= idx; i++) {
        if (be32(m + so + i * 4) == so) {
            return NULL; /* past the section's last */
        }
    }
    mo = be32(m + so + idx * 4);
    for (end = mo; end < size && m[end] != 0xFD; end++) {
    }
    if (end >= size) {
        return NULL;
    }
    if (len != NULL) {
        *len = end - mo;
    }
    return m + mo;
}

/* bytes after an 0xFF command byte, by command */
static int ffArgs(const uint8_t* p, size_t left) {
    const uint8_t c = left > 0 ? p[0] : 0;
    switch (c) {
    case 0x00: case 0x05: case 0x0B: case 0x0C: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
    case 0x19: case 0x1C: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x28: case 0x29: case 0x2E:
    case 0x2F: case 0x27:
        return 1;
    case 0x0D: case 0x0F: case 0x10: case 0x17: case 0x1B:
        return 2;
    case 0x16: case 0x1A:
        return 3;
    case 0x18:
        return 7;
    case 0x2C:
        return 8;
    case 0x26: /* an effect; Static, Blur and DitherFade take one more */
        return left > 1 && (p[1] == 0x03 || p[1] == 0x05 || p[1] == 0x07) ? 2 : 1;
    default:
        return 0;
    }
}

static int putUtf8(char* out, size_t size, size_t* n, unsigned cp) {
    char b[4];
    int k = 0, i;
    if (cp < 0x80) {
        b[k++] = (char)cp;
    } else if (cp < 0x800) {
        b[k++] = (char)(0xC0 | cp >> 6);
        b[k++] = (char)(0x80 | (cp & 0x3F));
    } else {
        b[k++] = (char)(0xE0 | cp >> 12);
        b[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[k++] = (char)(0x80 | (cp & 0x3F));
    }
    if (*n + (size_t)k + 1 > size) {
        return 0;
    }
    for (i = 0; i < k; i++) {
        out[(*n)++] = b[i];
    }
    return 1;
}

int portPm64MessageText(uint32_t id, int lang, char* out, size_t outSize) {
    /* the standard font's 0x5F..0x8F, then 0xA2..0xA5 */
    static const unsigned short kLatin[] = {
        0xB0, 0xC0, 0xC1, 0xC2, 0xC4, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD1, 0xD2, 0xD3,
        0xD4, 0xD6, 0xD9, 0xDA, 0xDB, 0xDC, 0xDF, 0xE0, 0xE1, 0xE2, 0xE4, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC,
        0xED, 0xEE, 0xEF, 0xF1, 0xF2, 0xF3, 0xF4, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC, 0xA1, 0xBF, 0xAA};
    static const unsigned short kQuotes[] = {0x201C, 0x201D, 0x2018, 0x2019};
    size_t len, i, n = 0;
    const uint8_t* m;
    const int v = portPm64Linked();
    if (outSize == 0 || v == PM64_JP || v == PM64_IQUE || (m = portPm64MessageRaw(id, lang, &len)) == NULL) {
        if (outSize > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    for (i = 0; i < len; i++) {
        const uint8_t c = m[i];
        unsigned cp = 0;
        if (c >= 0x01 && c <= 0x5E) {
            cp = c == 0x3C ? 0xA5 : (unsigned)c + 0x20;
        } else if (c >= 0x5F && c <= 0x8F) {
            cp = kLatin[c - 0x5F];
        } else if (c >= 0xA2 && c <= 0xA5) {
            cp = kQuotes[c - 0xA2];
        } else if (c == 0xF7 || c == 0xF8) {
            cp = ' ';
        } else if (c == 0xF0 || c == 0xFB) {
            cp = '\n';
        } else if (c == 0xF2) {
            i += 1; /* a pause */
        } else if (c == 0xFC) { /* a style: some take their place and size */
            if (i + 1 < len && (m[i + 1] == 0x05 || m[i + 1] == 0x0C)) {
                i += 5;
            } else if (i + 1 < len && (m[i + 1] == 0x08 || m[i + 1] == 0x09)) {
                i += 2;
            } else {
                i += 1;
            }
        } else if (c == 0xFF) {
            i += 1 + (size_t)ffArgs(m + i + 1, len - i - 1);
        }
        if (cp != 0 && !putUtf8(out, outSize, &n, cp)) {
            break;
        }
    }
    /* no line breaks at the ends */
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' ')) {
        n--;
    }
    out[n] = '\0';
    return (int)n;
}

/* --- moves */

int portPm64MoveCount(void) {
    size_t size;
    return portPm64Segment(PM64_SEG_MOVES, &size) != NULL ? (int)(size / MOVE_SIZE) : 0;
}

int portPm64Move(int index, Pm64Move* out) {
    size_t size;
    const uint8_t* t = portPm64Segment(PM64_SEG_MOVES, &size);
    const uint8_t* e;
    if (t == NULL || index < 0 || (size_t)(index + 1) * MOVE_SIZE > size) {
        return 0;
    }
    e = t + index * MOVE_SIZE;
    if (out != NULL) {
        out->nameMsg = be32(e);
        out->flags = be32(e + 4);
        out->shortDescMsg = be32(e + 8);
        out->fullDescMsg = be32(e + 12);
        out->category = (signed char)e[16];
        out->costFP = (signed char)e[17];
        out->costBP = (signed char)e[18];
        out->actionTip = e[19];
    }
    return 1;
}

int portPm64MoveByDesc(uint32_t fullDescMsg, Pm64Move* out) {
    const int n = portPm64MoveCount();
    int i;
    Pm64Move m;
    for (i = 0; i < n; i++) {
        if (portPm64Move(i, &m) && m.fullDescMsg == fullDescMsg) {
            if (out != NULL) {
                *out = m;
            }
            return i;
        }
    }
    return -1;
}

/* --- items */

int portPm64Item(int index, Pm64Item* out) {
    size_t size;
    const uint8_t* t = portPm64Segment(PM64_SEG_ITEMS, &size);
    const uint8_t* e;
    if (t == NULL || index < 0 || (size_t)(index + 1) * ITEM_SIZE > size) {
        return 0;
    }
    e = t + index * ITEM_SIZE;
    if (out != NULL) {
        out->nameMsg = be32(e);
        out->sellValue = (int16_t)be16(e + 0xC);
        out->fullDescMsg = be32(e + 0x10);
        out->shortDescMsg = be32(e + 0x14);
        out->typeFlags = (int16_t)be16(e + 0x18);
        out->potencyA = (int8_t)e[0x1B];
        out->potencyB = (int8_t)e[0x1C];
    }
    return 1;
}
