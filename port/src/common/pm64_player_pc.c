/* Mario's Paper Mario 64 look (port/include/port/pm64_media.h): the player's sprites from the linked ROM's
 * sprite section, and Mario's poses drawn as them while the Wardrobe's "Paper Mario 64" is on.
 *
 * The player's sprites aren't laid out as the NPCs': each is Yay0 in a table of its own (the section
 * header's 0x14), and their rasters are apart from them, shared, in the player raster section (the
 * header's 0x10: its index ranges, one start per sprite, into a table of descriptors, each a raster's size
 * / 16 in its top 12 bits and its offset in the section in its low 20). Each sprite is made here into the
 * NPCs' layout (its rasters' pixels appended, their offsets pointed at them) and parsed by pm64_pc.c
 * (portPm64SpriteAdopt), so it draws and animates as the NPC sprites do. As the decomp's tools/splat_ext/
 * pm_sprites.py documents the section; the code is Paper Cut's own. */
#include "port/costume.h"
#include "port/luigi.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/settings.h"
#include "driver/animdrv.h"
#include "mario/mariost.h"
#include <stdlib.h>
#include <string.h>

#define PLAYER_SPRITES 13
#define SPECIAL_RASTER 0x1F880 /* (a descriptor of no raster of its own) */

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

#define MAX_PLAYER_RASTERS 256 /* (Mario's sprites have under 100: a damaged count can't ask for gigabytes) */

static const Pm64Sprite* sPlayer[PLAYER_SPRITES];
/* the link generation (+ 1) each sprite was made in, or tried and failed in: a cache whose sprite doesn't
   parse isn't decoded again on every draw */
static unsigned sGen[PLAYER_SPRITES];

/* Every offset and size here comes from the cache file (a damaged one too): each is a size_t checked as
   "it fits in what is left" (never a sum that can wrap, on 32-bit targets either), and every table index is
   bounded by the table the section actually holds. */
const Pm64Sprite* portPm64PlayerSprite(int index) {
    size_t size, dsize, total, rasAt, yayAt, rasLen, a, b, start, list, rangesAt, descAt, descCount;
    const uint8_t* spr = portPm64Segment(PM64_SEG_SPRITES, &size);
    const uint8_t *ras, *yay, *ranges, *desc;
    uint8_t* d;
    int count, i;
    if (index < 0 || index >= PLAYER_SPRITES || spr == NULL || size < 0x20) {
        return NULL;
    }
    if (sGen[index] == portPm64Generation() + 1) {
        return sPlayer[index];
    }
    sPlayer[index] = NULL; /* (an older link's: kept, a queued draw may hold its textures) */
    sGen[index] = portPm64Generation() + 1;
    rasAt = (size_t)be32(spr + 0x10) + 0x10;
    yayAt = (size_t)be32(spr + 0x14) + 0x10;
    if (rasAt > size || size - rasAt < 12 || yayAt > size || size - yayAt < 4 * (PLAYER_SPRITES + 1)) {
        return NULL;
    }
    ras = spr + rasAt;
    rasLen = size - rasAt;
    yay = spr + yayAt;
    rangesAt = be32(ras);
    descAt = be32(ras + 4);
    if (rangesAt > rasLen || rasLen - rangesAt < 4 * (PLAYER_SPRITES + 1) || descAt > rasLen) {
        return NULL;
    }
    ranges = ras + rangesAt;
    desc = ras + descAt;
    descCount = (rasLen - descAt) / 4; /* the descriptors the section holds */
    start = be32(ranges + 4 * index);
    a = be32(yay + 4 * index);
    b = be32(yay + 4 * index + 4);
    if (start > descCount || b <= a || b > size - yayAt || (dsize = portPm64Yay0Size(yay + a, b - a)) < 4 ||
        dsize > 1024 * 1024) {
        return NULL;
    }
    /* the sprite, then room for its rasters' pixels (the descriptors give their sizes) */
    {
        uint8_t* tmp = (uint8_t*)malloc(dsize);
        if (tmp == NULL || !portPm64Yay0Decode(yay + a, b - a, tmp, dsize)) {
            free(tmp);
            return NULL;
        }
        list = be32(tmp);
        total = dsize;
        for (count = 0; count < MAX_PLAYER_RASTERS && list <= dsize && (dsize - list) / 4 > (size_t)count &&
                        (size_t)count < descCount - start && be32(tmp + list + 4 * (size_t)count) != 0xFFFFFFFFu;
             count++) {
            const uint32_t di = be32(desc + 4 * (start + (size_t)count));
            if ((di & 0xFFFFF) != SPECIAL_RASTER) {
                total += (size_t)(di >> 20) << 4;
            }
        }
        if ((d = (uint8_t*)realloc(tmp, total)) == NULL) {
            free(tmp);
            return NULL;
        }
    }
    total = dsize;
    for (i = 0; i < count; i++) {
        const size_t meta = be32(d + list + 4 * (size_t)i);
        const uint32_t di = be32(desc + 4 * (start + (size_t)i));
        const size_t from = di & 0xFFFFF, n = (size_t)(di >> 20) << 4; /* (from: in the raster section) */
        if (meta > dsize || dsize - meta < 8) {
            continue;
        }
        if ((di & 0xFFFFF) == SPECIAL_RASTER || from > rasLen || n > rasLen - from) {
            d[meta + 4] = 0; /* (no pixels: drawn as nothing) */
            d[meta + 5] = 0;
            continue;
        }
        memcpy(d + total, ras + from, n);
        put32(d + meta, (uint32_t)total);
        total += n;
    }
    sPlayer[index] = portPm64SpriteAdopt(d, total);
    return sPlayer[index];
}

/* --- Mario drawn as it --- */

int portPm64MarioLookOn(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_mario_look", 0, 0, 1);
    }
    return *sOn != 0 && portPm64Linked() != PM64_NONE && portPlayAs() == PORT_PLAY_AS_MARIO;
}

void portPm64MarioLookSet(int on) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_mario_look", 0, 0, 1);
    }
    *sOn = on != 0;
    portSettingsSave();
}

/* TTYD Mario's animation (by its name: M_S_1 standing, M_W_1 walking, M_R_1 running, M_J_1 jumping...) as
 * the PM64 sprite Mario1's: Idle 2, Walk 4, Run 5, Jump 7, Fall 8, Land 9, Hurt 23 */
int portPm64MarioAnim(const char* ttyd) {
    char c;
    if (ttyd == NULL) {
        return 2;
    }
    c = strncmp(ttyd, "M_", 2) == 0 ? ttyd[2] : ttyd[0];
    switch (c) {
    case 'W':
        return 4;
    case 'R':
        return 5;
    case 'J':
        return 7;
    case 'F':
        return 8;
    case 'D':
        return 23;
    default:
        return 2;
    }
}
