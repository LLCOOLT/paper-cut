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
#include "mario/mario.h"
#include "mario/mariost.h"
#include "battle/battle.h"
#include "battle/ac/ac_table.h"
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

/* The hammer. TTYD's is a part of Mario's model, shown in his hammer animations (M_H_<n><step>); PM64 has no
 * hammer of its own to draw beside him: its swings are animations of the player sprites with the hammer in Mario's
 * hands (the decomp's player_sprite_names.yaml), so those are what the look plays, PM64's own steps at PM64's own
 * timing (its battle scripts, HammerSupport.inc.c: a Wait(n) is 2n of our frames, as the sprites' own clock).
 * TTYD's animation says which part of the move it is; how long it has played (frames) says where PM64 would be.
 *   field (mot_hammer.c): a swing M_H_1 / 4 / 7 by level, M_H_2 / 5 / 8 when it strikes something (R: from the
 *     back): MarioW1's Smash<L>_Miss / Smash<L>_Hit (_Back); the Spin Hammer M_H_3 / 6 / 9 raised, A charging,
 *     B released and spinning: PM64 has no field Spin Hammer, so its Spin Smash's (MarioB2 SpinSmash<L>: raised,
 *     held, Hold2 charged, the pre-swing and the swing around)
 *   battle (battle_mario.c): A raised (30 frames), B held for the command, C swung (13 / 14 / 15 Piercing Blow's,
 *     Quake's; 16 Piercing Blow's after): MarioB1's Smash<L>: Begin (the hammer on the ground, 20 frames: the
 *     raise delay with the stick let go), PullBack (Wait 4), Hold1, Hold2 once the command's gauge is full, then
 *     PreSwing (Wait 3) and Swing; for the Hammer, its badges (Power and Mega Smash, Head Rattle, Ice Smash,
 *     Piercing Blow), Quake Hammer and Fire Drive (17 / 18 / 19: TTYD's own, PM64's swing); the Spin Hammer 2 / 6
 *     / 10 (A raised and held, B spun): MarioB2's SpinSmash<L> (Raise, Hold1 / Hold2, PreSwing, Swing); Hammer
 *     Throw 3 / 7 / 11 (raised and held as the Hammer: MarioB2's HammerThrow raise and hold are the same frames):
 *     Throw at once (TTYD lets go of the hammer 4 frames in: PM64's PreThrow first would show it in his hands and
 *     in the air together for 10 frames; the Throw's own swing still holds it 8); the thrown hammer, a part of its own (4 / 8 / 12): MarioB2's Hammer<L>, the sprite
 *     PM64 throws, turned as TTYD turns the part */
#define B1_SMASH(l) (13 + 7 * ((l) - 1)) /* Begin; + 1 PullBack, 2 Hold1, 3 Hold2, 4 PreSwing, 5 Swing */
#define B2_SPIN(l) (6 * ((l) - 1))       /* SpinSmash Begin; + 1 Raise, 2 Hold1, 3 Hold2, 4 PreSwing, 5 Swing */
#define B2_HAMMER(l) (18 + (l) - 1)      /* the thrown hammer */
#define B2_THROW(l) (22 + 6 * ((l) - 1)) /* HammerThrow Begin; + 4 PreThrow, 5 Throw */
#define W1_SMASH(l) (16 + 4 * ((l) - 1)) /* Miss; + 1 Miss_Back, 2 Hit, 3 Hit_Back */
#define PM64_WAIT(n) (2 * (n))           /* (a PM64 script's Wait in our frames) */

/* the hammer's action command (TTYD's stick held left, ac_stick_keep_left.c) held with its gauge full: PM64's
 * Hold2 (HammerSupport: once the action's progress is set) */
static int hammerCharged(int t) {
    static unsigned sFullFrom; /* (the animation it was full in: from then on Hold2, as PM64 never goes back) */
    BattleWork* wp = _battleWorkPointer;
    const unsigned from = gp != NULL ? gp->retraceCount - (unsigned)t + 1u : 0u;
    if (wp != NULL && wp->actionCommands.maincb == battleAcMain_StickKeepLeft && wp->actionCommands.acState == 1001 &&
        wp->actionCommands.params[4] != 0) {
        sFullFrom = from;
    }
    return from != 0u && sFullFrom == from;
}

static int hammerLook(int n, const char* step, int battle, int t, int* sprite) {
    int level;
    const char s = step[0];
    if (!battle) {
        if (n < 1 || n > 9) {
            return -1;
        }
        level = (n + 2) / 3;
        if (n % 3 == 0) {
            *sprite = PM64_PLAYER_MARIOB2;
            if (s == 'B') {
                return B2_SPIN(level) + (t < PM64_WAIT(3) ? 4 : 5);
            }
            /* (raised as the stick is turned, A from the first charge on: one Raise played on, then held; Hold2
               once fully charged, mot_hammer's charge 3) */
            return B2_SPIN(level) + (marioGetPtr()->hammerChargeLv >= 3 ? 3 : s == 'A' && t >= 24 ? 2 : 1);
        }
        *sprite = PM64_PLAYER_MARIOW1;
        return W1_SMASH(level) + (n % 3 == 2 ? 2 : 0) + (s == 'R' ? 1 : 0);
    }
    switch (n) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 13:
    case 16:
    case 17:
        level = 1;
        break;
    case 5:
    case 6:
    case 7:
    case 8:
    case 14:
    case 18:
        level = 2;
        break;
    case 9:
    case 10:
    case 11:
    case 12:
    case 15:
    case 19:
        level = 3;
        break;
    default:
        return -1;
    }
    if (n == 4 || n == 8 || n == 12) {
        *sprite = PM64_PLAYER_MARIOB2;
        return B2_HAMMER(level);
    }
    if (n == 3 || n == 7 || n == 11) {
        *sprite = PM64_PLAYER_MARIOB2;
        return B2_THROW(level) + 5;
    }
    if (n == 2 || n == 6 || n == 10) {
        *sprite = PM64_PLAYER_MARIOB2;
        if (s == 'B') {
            return B2_SPIN(level) + (t < PM64_WAIT(3) ? 4 : 5);
        }
        return B2_SPIN(level) + (t < 24 ? 1 : hammerCharged(t) ? 3 : 2);
    }
    *sprite = PM64_PLAYER_MARIOB1;
    if (n == 16) {
        return B1_SMASH(level) + 5; /* (Piercing Blow's after: the swing's end, played on) */
    }
    if (s == 'A' && n >= 17) {
        /* (Fire Drive's raise is 20 frames: PM64's raise delay shortened, as with the stick held early) */
        return B1_SMASH(level) + (t < PM64_WAIT(6) ? 0 : t < PM64_WAIT(10) ? 1 : 2);
    }
    if (s == 'A') {
        return B1_SMASH(level) + (t < PM64_WAIT(10) ? 0 : t < PM64_WAIT(14) ? 1 : hammerCharged(t) ? 3 : 2);
    }
    if (s == 'B') {
        return B1_SMASH(level) + (hammerCharged(t) ? 3 : 2);
    }
    return B1_SMASH(level) + (t < PM64_WAIT(3) ? 4 : 5);
}

int portPm64MarioLookAnim(const char* ttyd, int battle, int frames, int* sprite) {
    *sprite = PM64_PLAYER_MARIO1;
    if (ttyd != NULL && strncmp(ttyd, "M_", 2) == 0 && ttyd[2] != '\0' && ttyd[3] == '_') {
        const char c = ttyd[2];
        const char* p = ttyd + 4;
        int n = 0, anim, spr = PM64_PLAYER_MARIO1;
        while (*p >= '0' && *p <= '9' && n < 100) {
            n = n * 10 + (*p++ - '0');
        }
        if (c == 'H' && (anim = hammerLook(n, p, battle, frames, &spr)) >= 0 && portPm64PlayerSprite(spr) != NULL) {
            *sprite = spr;
            return anim;
        }
        if (!battle && c == 'A') {
            /* the Spin Jump (mot_hip.c: A_1A up and turning, A_1B the drop): Mario1's SpinJump, SpinFall */
            return p[0] == 'B' ? 12 : 11;
        }
        if (!battle && c == 'Y' && portPm64PlayerSprite(PM64_PLAYER_MARIOW2) != NULL) {
            /* on Yoshi's back: MarioW2's RideLaki (seated, as on Lakilester's cloud) */
            *sprite = PM64_PLAYER_MARIOW2;
            return 14;
        }
    }
    return portPm64MarioAnim(ttyd);
}
