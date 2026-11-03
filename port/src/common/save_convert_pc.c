/* Saves between releases (port/include/port/save_convert.h). A .gci is the
 * 0x40-byte directory entry, then the file: CardMetadata (0x2000 bytes:
 * names, banner, icon) and eight 0x4000-byte slots (four saves, four
 * backups), big-endian as on the GameCube. A slot is the same in every
 * release up to the Pianta Parlor work (PiantaParlorWork, at 0x1FD8):
 * there, three record tables of [rows][6] s32, four rows in JP, eight in
 * US and PAL; then the rest follows, and the size, version and checksums
 * close the slot at 0x3FF0. */
#include "port/save_convert.h"

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
    if (size < GCI_HEADER + META_SIZE + SLOT_COUNT * SLOT_SIZE || memcmp(gci, "G8M", 3) != 0 ||
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
