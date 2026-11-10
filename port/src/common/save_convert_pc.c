/* Saves between releases (port/include/port/save_convert.h). A .gci is the
 * 0x40-byte directory entry, then the file: CardMetadata (0x2000 bytes:
 * names, banner, icon) and eight 0x4000-byte slots (four saves, four
 * backups), big-endian as on the GameCube. A slot is the same in every
 * release up to the Pianta Parlor work (PiantaParlorWork, at 0x1FD8):
 * there, three record tables of [rows][6] s32, four rows in JP, eight in
 * US and PAL; then the rest follows, and the size, version and checksums
 * close the slot at 0x3FF0. */
#include "port/save_convert.h"
#include "port/battlex.h"
#include "data/item_data.h"
#include "mario/mario_pouch.h"

#include <stdio.h>
#include <string.h>

enum {
    GCI_HEADER = 0x40,
    GCI_FILE_NAME = 0x08,
    META_SIZE = 0x2000,
    SLOT_SIZE = 0x4000,
    SLOT_COUNT = 8,
    PARLOR = 0x1FD8,
    PARLOR_HEAD = 0x14, /* flags, keptTokens, tokens, dispTick, dispTokens */
    PARLOR_ROW = 6 * 4,
    PARLOR_AFTER = 0x2B + 5, /* InformantUnknown, then the tail */
    SLOT_VERSION = 0x3FF0,
    SLOT_SIZE_FIELD = 0x3FF4,
    SLOT_CK1 = 0x3FF8,
    SLOT_CK2 = 0x3FFC,
};

typedef struct Layout {
    int rows;          /* Pianta Parlor record rows */
    uint32_t dataSize; /* CardSaveSlot.data */
    char version[4];   /* cardmgr.c's version string */
} Layout;

static const Layout kJapan = {4, 0x2140, "009"};
static const Layout kWest = {8, 0x2260, "010"};

static const Layout* layoutOf(char release) {
    return release == 'J' ? &kJapan : &kWest;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t byteSum(const uint8_t* p, uint32_t n) {
    uint32_t s = 0, i;
    for (i = 0; i < n; i++) {
        s += p[i];
    }
    return s;
}

static int slotValid(const uint8_t* s, const Layout* l) {
    const uint32_t ck1 = be32(s + SLOT_CK1);
    return be32(s + SLOT_SIZE_FIELD) == l->dataSize && be32(s + SLOT_CK2) == ~ck1 &&
           ck1 == byteSum(s, l->dataSize) && memcmp(s + SLOT_VERSION, l->version, 4) == 0;
}

static int slotEmpty(const uint8_t* s) {
    int i;
    for (i = 0; i < SLOT_SIZE; i++) {
        if (s[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/* Returns 1 when the slot verified and was converted. */
static int convertSlot(const uint8_t* src, const Layout* from, uint8_t* dst, const Layout* to) {
    const uint32_t fromAfter = PARLOR + PARLOR_HEAD + 3 * from->rows * PARLOR_ROW + 4;
    const uint32_t toAfter = PARLOR + PARLOR_HEAD + 3 * to->rows * PARLOR_ROW + 4;
    const int valid = slotValid(src, from);
    int table, row;

    memset(dst, 0, SLOT_SIZE);
    if (slotEmpty(src)) {
        return 0;
    }
    memcpy(dst, src, PARLOR + PARLOR_HEAD);
    for (table = 0; table < 3; table++) {
        for (row = 0; row < to->rows; row++) {
            /* JP to US/PAL: the second four rows (the same four minigames) start as the first four */
            const int fromRow = row % from->rows;
            memcpy(dst + PARLOR + PARLOR_HEAD + (table * to->rows + row) * PARLOR_ROW,
                   src + PARLOR + PARLOR_HEAD + (table * from->rows + fromRow) * PARLOR_ROW, PARLOR_ROW);
        }
    }
    memcpy(dst + toAfter - 4, src + fromAfter - 4, 4 + PARLOR_AFTER); /* pad_134, then the rest */
    memcpy(dst + SLOT_VERSION, to->version, 4);
    put32(dst + SLOT_SIZE_FIELD, to->dataSize);
    if (valid) {
        const uint32_t ck = byteSum(dst, to->dataSize);
        put32(dst + SLOT_CK1, ck);
        put32(dst + SLOT_CK2, ~ck);
    } /* else 0 and 0: still fails the game's check */
    return valid;
}

char portSaveRelease(const uint8_t* gci, size_t size) {
    /* the game's file is always this size (17 blocks: cardmgr.c's CARDCreateAsync); anything else, a
     * damaged or made-up file, is refused here, so no card writer or card image ever takes one */
    if (size != GCI_HEADER + META_SIZE + SLOT_COUNT * SLOT_SIZE || memcmp(gci, "G8M", 3) != 0 ||
        memcmp(gci + GCI_FILE_NAME, "mariost_save_file", 18) != 0) {
        return 0;
    }
    return gci[3] == 'J' || gci[3] == 'E' || gci[3] == 'P' ? (char)gci[3] : 0;
}

int portSaveConvert(const uint8_t* gci, size_t size, char to, uint8_t* out) {
    const char from = portSaveRelease(gci, size);
    int i, converted = 0;
    if (from == 0 || (to != 'J' && to != 'E' && to != 'P')) {
        return -1;
    }
    memcpy(out, gci, size); /* the entry, the names and pictures, anything after the slots */
    out[3] = (uint8_t)to;
    for (i = 0; i < SLOT_COUNT; i++) {
        const size_t at = GCI_HEADER + META_SIZE + (size_t)i * SLOT_SIZE;
        converted += convertSlot(gci + at, layoutOf(from), out + at, layoutOf(to));
    }
    return converted;
}

void portSaveFileName(char release, char* out, size_t cap) {
    snprintf(out, cap, "01-G8M%c-mariost_save_file.gci", release);
}

/* --- a copy for Dolphin or a GameCube (portSaveDolphinSafe) --- */

enum {
    POUCH = 0x13E0, /* PouchData in the slot (the same in every release) */
    POUCH_PARTY = 0x0,
    PARTY_SIZE = 0xE,
    PARTY_MAX_HP = 0x2,
    PARTY_BASE_MAX_HP = 0x4,
    PARTY_HP = 0x6,
    POUCH_HP = 0x70,
    POUCH_MAX_HP = 0x72,
    POUCH_FP = 0x74,
    POUCH_MAX_FP = 0x76,
    POUCH_COINS = 0x78,
    POUCH_BASE_MAX_HP = 0x8E,
    POUCH_BASE_MAX_FP = 0x90,
    POUCH_AVAILABLE_BP = 0x92,
    POUCH_MAX_BP = 0x94,
    POUCH_HELD = 0x192,
    POUCH_STORED = 0x1BA,
    POUCH_BADGES = 0x1FA,
    POUCH_EQUIPPED = 0x38A,
    COIN_MAX = 999,
};

static int be16(const uint8_t* p) {
    return (int16_t)(((unsigned)p[0] << 8) | p[1]);
}

static void put16(uint8_t* p, int v) {
    p[0] = (uint8_t)((unsigned)v >> 8);
    p[1] = (uint8_t)v;
}

static int portItem(int id) {
    return id >= PORT_ITEM_MIN && id < PORT_ITEM_END;
}

/* A list of item ids (n s16) without the port's items, the rest kept in order; each
 * one taken out paid for. Returns how many were. */
static int sellFrom(uint8_t* list, int n, int* coins) {
    int i, kept = 0, sold = 0;
    for (i = 0; i < n; i++) {
        const int id = be16(list + i * 2);
        if (portItem(id)) {
            *coins += itemDataTable[id].sell_price;
            sold++;
        } else {
            put16(list + kept * 2, id);
            kept++;
        }
    }
    for (; kept < n; kept++) {
        put16(list + kept * 2, 0);
    }
    return sold;
}

static int clampTo(int v, int max) {
    return v > max ? max : v;
}

/* One slot: the port's items sold, the stats worked out again as the game does
 * (pouchReviseMarioParam, pouchRevisePartyParam) when one was worn. */
static int sellSlot(uint8_t* s, const Layout* l, int* paid) {
    uint8_t* p = s + POUCH;
    int coins = 0, sold, worn = 0, i, kept = 0;
    if (!slotValid(s, l)) {
        return 0;
    }
    sold = sellFrom(p + POUCH_HELD, MAX_HELD_ITEMS, &coins);
    sold += sellFrom(p + POUCH_STORED, MAX_STORED_ITEMS, &coins);
    /* the badges, and the worn ones beside them (the same place in both lists) */
    for (i = 0; i < MAX_BADGES; i++) {
        const int id = be16(p + POUCH_BADGES + i * 2), on = be16(p + POUCH_EQUIPPED + i * 2);
        if (portItem(id) || portItem(on)) {
            coins += portItem(id) ? itemDataTable[id].sell_price : 0;
            sold += portItem(id);
            worn |= portItem(on);
            continue;
        }
        put16(p + POUCH_BADGES + kept * 2, id);
        put16(p + POUCH_EQUIPPED + kept * 2, on);
        kept++;
    }
    for (; kept < MAX_BADGES; kept++) {
        put16(p + POUCH_BADGES + kept * 2, 0);
        put16(p + POUCH_EQUIPPED + kept * 2, 0);
    }
    /* (coins over 999, Coins up to 9,999 (qol_pc.c): the GameCube's cap) */
    if (sold == 0 && !worn && be16(p + POUCH_COINS) <= COIN_MAX) {
        return 0;
    }
    put16(p + POUCH_COINS, clampTo(be16(p + POUCH_COINS) + coins, COIN_MAX));
    if (worn) {
        int hp = be16(p + POUCH_BASE_MAX_HP), fp = be16(p + POUCH_BASE_MAX_FP), bp = be16(p + POUCH_MAX_BP);
        int partyPlus = 0;
        for (i = 0; i < MAX_BADGES; i++) {
            const int on = be16(p + POUCH_EQUIPPED + i * 2);
            if (on <= 0 || on >= ITEM_TYPE_MAX) {
                continue;
            }
            hp += on == ITEM_HP_PLUS ? 5 : 0;
            fp += on == ITEM_FP_PLUS ? 5 : 0;
            partyPlus += on == ITEM_HP_PLUS_P;
            bp -= itemDataTable[on].bp_cost;
        }
        put16(p + POUCH_MAX_HP, hp);
        put16(p + POUCH_MAX_FP, fp);
        put16(p + POUCH_HP, clampTo(be16(p + POUCH_HP), hp));
        put16(p + POUCH_FP, clampTo(be16(p + POUCH_FP), fp));
        put16(p + POUCH_AVAILABLE_BP, bp);
        for (i = 1; i < 8; i++) {
            uint8_t* party = p + POUCH_PARTY + i * PARTY_SIZE;
            const int max = be16(party + PARTY_BASE_MAX_HP) + partyPlus * 5;
            put16(party + PARTY_MAX_HP, max);
            put16(party + PARTY_HP, clampTo(be16(party + PARTY_HP), max));
        }
    }
    {
        const uint32_t ck = byteSum(s, l->dataSize);
        put32(s + SLOT_CK1, ck);
        put32(s + SLOT_CK2, ~ck);
    }
    *paid += coins;
    return sold;
}

int portSaveDolphinSafe(uint8_t* gci, size_t size, int* coins) {
    const char release = portSaveRelease(gci, size);
    int i, sold = 0, paid = 0;
    if (release == 0) {
        return -1;
    }
    portItemsInit(); /* (their prices) */
    for (i = 0; i < SLOT_COUNT; i++) {
        int backupPaid = 0;
        /* (slots 4-7 are 0-3's backups: sold the same, counted once) */
        const int n = sellSlot(gci + GCI_HEADER + META_SIZE + (size_t)i * SLOT_SIZE, layoutOf(release),
                               i < SLOT_COUNT / 2 ? &paid : &backupPaid);
        sold += i < SLOT_COUNT / 2 ? n : 0;
    }
    if (coins != NULL) {
        *coins = paid;
    }
    return sold;
}

int portSaveHasPortItems(const uint8_t* gci, size_t size) {
    const char release = portSaveRelease(gci, size);
    int i, k;
    if (release == 0) {
        return 0;
    }
    for (i = 0; i < SLOT_COUNT; i++) {
        const uint8_t* p = gci + GCI_HEADER + META_SIZE + (size_t)i * SLOT_SIZE + POUCH;
        for (k = 0; k < MAX_HELD_ITEMS + MAX_STORED_ITEMS; k++) { /* (held, then stored: one after the other) */
            if (portItem(be16(p + POUCH_HELD + k * 2))) {
                return 1;
            }
        }
        for (k = 0; k < 2 * MAX_BADGES; k++) { /* (the badges, then the worn ones) */
            if (portItem(be16(p + POUCH_BADGES + k * 2))) {
                return 1;
            }
        }
    }
    return 0;
}
