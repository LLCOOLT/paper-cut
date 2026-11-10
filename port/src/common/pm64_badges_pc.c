/* Paper Mario 64's badges that the Thousand-Year Door lacks (port/include/port/pm64_media.h): items
 * PORT_PM64_BADGE_MIN.., their names, descriptions and BP from the linked ROM's move table, their icons
 * from its icon archive, sold by Rowf at the Star Haven corner (port/src/aurora/pm64_media_aurora.c).
 *
 * Their effects are our own code on the game's machinery:
 * - most are the game's badges (portBadgeGrants, as the Paper Cut badge set's): Attack FX A-E are TTYD's
 *   five Attack FX; Mega Jump, Mega Smash, Power Quake and Mega Quake are two or three of TTYD's Power
 *   Jump, Power Smash and Quake Hammer (the game's own power levels: each copy one more, at more FP);
 *   Triple Dip two Double Dips; Damage Dodge a Defend Plus;
 * - Dizzy Stomp: TTYD's Sleepy Stomp worn, its weapon making the enemy dizzy instead of asleep, named and
 *   told as Dizzy Stomp (while no real Sleepy Stomp is worn);
 * - Peekaboo: every enemy's HP shown, tattled or not (battle_seq_command.c BattleDrawEnemyHP);
 * - Pay-Off: as many coins as the damage Mario's side took, when the battle's won (battlex_pc.c).
 *
 * Only with a ROM linked and "pm64_badges" on; online only when every player has the ROM (the hello's
 * pm64, portPm64PartnersActive). Their ids are past the game's (port/include/port/battlex.h). */
#include "port/battlex.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "battle/battle.h"
#include "battle/battle_mario.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <string.h>

typedef struct Pm64Badge {
    const char* english;
    int move;          /* gMoveTable index (the decomp's move_table.yaml order) */
    unsigned icon;     /* the icon archive's offset of its picture (a "pair": CI4 32x32, two palettes) */
    short grants[PORT_BADGE_GRANTS];
    short looks;       /* the TTYD badge it sorts beside */
} Pm64Badge;

static const Pm64Badge kBadges[] = {
    {"Attack FX A", 84, 0x18A80, {ITEM_ATTACK_FX_R}, ITEM_ATTACK_FX_R},
    {"Attack FX B", 86, 0x21A80, {ITEM_ATTACK_FX_G}, ITEM_ATTACK_FX_G},
    {"Attack FX C", 88, 0x21F00, {ITEM_ATTACK_FX_B}, ITEM_ATTACK_FX_B},
    {"Attack FX D", 85, 0x21840, {ITEM_ATTACK_FX_Y}, ITEM_ATTACK_FX_Y},
    {"Attack FX E", 87, 0x21CC0, {ITEM_ATTACK_FX_P}, ITEM_ATTACK_FX_P},
    {"Dizzy Stomp", 34, 0x17F40, {ITEM_SLEEPY_STOMP}, ITEM_SLEEPY_STOMP},
    {"Mega Jump", 45, 0x1BE40, {ITEM_POWER_JUMP, ITEM_POWER_JUMP}, ITEM_POWER_JUMP},
    {"Mega Smash", 23, 0x1C500, {ITEM_POWER_SMASH, ITEM_POWER_SMASH}, ITEM_POWER_SMASH},
    {"Power Quake", 20, 0x16440, {ITEM_QUAKE_HAMMER, ITEM_QUAKE_HAMMER}, ITEM_QUAKE_HAMMER},
    {"Mega Quake", 14, 0x16680, {ITEM_QUAKE_HAMMER, ITEM_QUAKE_HAMMER, ITEM_QUAKE_HAMMER}, ITEM_QUAKE_HAMMER},
    {"Triple Dip", 54, 0x1B540, {ITEM_DOUBLE_DIP, ITEM_DOUBLE_DIP}, ITEM_DOUBLE_DIP},
    {"Damage Dodge", 51, 0x1F680, {ITEM_DEFEND_PLUS}, ITEM_DEFEND_PLUS},
    {"Peekaboo", 118, 0x213C0, {0}, ITEM_TIMING_TUTOR},
    {"Pay-Off", 99, 0x19EC0, {0}, ITEM_REFUND},
};
#define BADGES (int)(sizeof(kBadges) / sizeof(kBadges[0]))
typedef char kBadgesFit[BADGES <= PORT_PM64_BADGE_COUNT ? 1 : -1];

int portPm64BadgesActive(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_badges", 0, 0, 1);
    }
    return *sOn != 0 && portPm64Linked() != PM64_NONE && (!portStateSyncLinked() || portPm64PartnersActive());
}

/* the badges a save holds keep working with the ROM unlinked (their effects are TTYD's own; their names and
 * icons stand in: pm64_badges_aurora.c), with the setting on; online as every player's game has them */
static int badgesWork(void) {
    (void)portPm64BadgesActive(); /* (its setting declared) */
    return *portSettingsFind("pm64_badges") != 0 && (!portStateSyncLinked() || portPm64PartnersActive());
}

int portPm64BadgeCount(void) {
    return BADGES;
}

static int badgeOf(int id) {
    const int i = id - PORT_PM64_BADGE_MIN;
    return i >= 0 && i < BADGES ? i : -1;
}

int portPm64BadgeGrants(int id, const short** grants) {
    const int i = badgeOf(id);
    int n;
    *grants = NULL;
    if (i < 0 || !badgesWork()) {
        return 0;
    }
    for (n = 0; n < PORT_BADGE_GRANTS && kBadges[i].grants[n] != 0; n++) {
    }
    *grants = kBadges[i].grants;
    return n;
}

const char* portPm64BadgeEnglish(int id) {
    const int i = badgeOf(id);
    return i >= 0 ? kBadges[i].english : "";
}

/* the badge's name and description from the ROM (UTF-8); 0 without them */
int portPm64BadgeText(int id, char* name, size_t nameSize, char* desc, size_t descSize) {
    const int i = badgeOf(id);
    Pm64Move m;
    name[0] = desc[0] = '\0';
    if (i < 0 || !portPm64Move(kBadges[i].move, &m)) {
        return 0;
    }
    if (portPm64MessageText(m.nameMsg, 0, name, nameSize) <= 0) {
        snprintf(name, nameSize, "%s", kBadges[i].english);
    }
    portPm64MessageText(m.fullDescMsg, 0, desc, descSize);
    return 1;
}

/* how many of PM64 badge id are worn now (0 while they're off) */
int portPm64BadgeWorn(int id) {
    PouchData* p = pouchGetPtr();
    int i, n = 0;
    if (p == NULL || badgeOf(id) < 0 || !badgesWork()) {
        return 0;
    }
    for (i = 0; i < MAX_BADGES; i++) {
        n += p->equippedBadges[i] == id;
    }
    return n;
}

int portPm64Peekaboo(void) {
    return portPm64BadgeWorn(PORT_PM64_BADGE_PEEKABOO) > 0;
}

int portPm64PayOff(void) {
    return portPm64BadgeWorn(PORT_PM64_BADGE_PAY_OFF);
}

/* --- the items' table entries --- */

static unsigned sFilledGen = ~0u;

void portPm64BadgesFill(void) {
    int i;
    if (sFilledGen == portPm64Generation()) {
        return;
    }
    sFilledGen = portPm64Generation();
    portItemsInit();
    for (i = 0; i < BADGES; i++) {
        ItemData* d = &itemDataTable[PORT_PM64_BADGE_MIN + i];
        const ItemData* look = &itemDataTable[kBadges[i].looks];
        Pm64Move m;
        int bp = look->bp_cost;
        if (portPm64Move(kBadges[i].move, &m) && m.costBP > 0) {
            bp = m.costBP;
        }
        d->bp_cost = (s8)bp;
        d->buy_price = d->discount_price = (u16)portBadgeMakerPrice(bp);
        d->sell_price = (u16)(d->buy_price / 2);
        d->star_piece_price = 0;
        d->type_sort_order = (s16)(look->type_sort_order + 1);
        d->usable_locations = 0;
        d->battle_params = NULL;
        d->icon_id = (u16)(portPm64Linked() != PM64_NONE ? PORT_ICON_PM64_BASE + i : look->icon_id);
    }
}

int portPm64MerchantStock(int* out, int max) {
    int i, n = 0;
    if (portPm64BadgesActive()) {
        portPm64BadgesFill();
        for (i = 0; i < BADGES && n < max; i++) {
            out[n++] = PORT_PM64_BADGE_MIN + i;
        }
    }
    return n + portPm64FoodStock(out + n, max - n); /* and Tayce T.'s ingredients (pm64_food_pc.c) */
}

/* --- the icons: CI4 with an RGBA5551 palette, made GX RGBA8 (4x4 tiles: AR then GB) --- */

#define ICON_W 32
#define ICON_H 32
#define OUT_WH 64      /* the texture made: the picture cropped to what it shows and drawn over it, finer */
#define OUT_FILL 0.88f /* how much of the frame the picture's larger side fills: the game's own badges' and items' */
#define ICONS (PORT_PM64_BADGE_COUNT + PORT_PM64_FOOD_COUNT) /* the badges', then Tayce T.'s recipes' */
static unsigned char sIcon[ICONS][OUT_WH * OUT_WH * 4] __attribute__((aligned(32)));
static unsigned sIconGen[ICONS];

/* A PM64 icon's 32x32 has a wide empty margin round its picture; the game's icons fill their frame (an icon
 * borrows a game icon's frame, icondrv.c, and is drawn at its size). So the picture is cut to what it shows,
 * squared about its middle, and stretched over OUT_FILL of the texture (bilinear: as the game's own soft
 * icons), into GX RGBA8 tiles. px: the ROM's 32x32 as RGBA. */
static void iconFit(const unsigned char px[ICON_H][ICON_W][4], unsigned char* o) {
    int x, y, x0 = ICON_W, y0 = ICON_H, x1 = -1, y1 = -1, tx, ty;
    float cx, cy, side;
    for (y = 0; y < ICON_H; y++) {
        for (x = 0; x < ICON_W; x++) {
            if (px[y][x][3] != 0) {
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
    }
    if (x1 < 0) {
        x0 = y0 = 0;
        x1 = ICON_W - 1;
        y1 = ICON_H - 1;
    }
    cx = (x0 + x1 + 1) * 0.5f;
    cy = (y0 + y1 + 1) * 0.5f;
    side = (float)((x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 1) / OUT_FILL;
    if (side > (float)ICON_W) {
        side = (float)ICON_W; /* (never smaller than the ROM draws it) */
    }
    for (ty = 0; ty < OUT_WH; ty += 4) {
        for (tx = 0; tx < OUT_WH; tx += 4) {
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    /* the source point under this texel's middle, and its four neighbours' mix */
                    const float sx = cx - side * 0.5f + ((float)(tx + x) + 0.5f) * side / OUT_WH - 0.5f;
                    const float sy = cy - side * 0.5f + ((float)(ty + y) + 0.5f) * side / OUT_WH - 0.5f;
                    const int ix = (int)(sx + 1.0f) - 1, iy = (int)(sy + 1.0f) - 1;
                    const float fx = sx - (float)ix, fy = sy - (float)iy;
                    float acc[4] = {0, 0, 0, 0}, wsum = 0.f;
                    int c, k = (y * 4 + x) * 2, dx, dy;
                    for (dy = 0; dy < 2; dy++) {
                        for (dx = 0; dx < 2; dx++) {
                            const int qx = ix + dx, qy = iy + dy;
                            const float wgt = (dx ? fx : 1.f - fx) * (dy ? fy : 1.f - fy);
                            float a;
                            if (qx < 0 || qy < 0 || qx >= ICON_W || qy >= ICON_H) {
                                continue; /* (outside: clear) */
                            }
                            a = px[qy][qx][3] / 255.f;
                            for (c = 0; c < 3; c++) {
                                acc[c] += px[qy][qx][c] * a * wgt; /* (colour weighted by alpha: no dark fringe) */
                            }
                            acc[3] += a * wgt;
                            wsum += wgt;
                        }
                    }
                    (void)wsum;
                    o[k] = (unsigned char)(acc[3] * 255.f + 0.5f);
                    for (c = 0; c < 3; c++) {
                        acc[c] = acc[3] > 0.f ? acc[c] / acc[3] : 0.f;
                    }
                    o[k + 1] = (unsigned char)(acc[0] + 0.5f);
                    o[32 + k] = (unsigned char)(acc[1] + 0.5f);
                    o[32 + k + 1] = (unsigned char)(acc[2] + 0.5f);
                }
            }
            o += 64;
        }
    }
}

const void* portPm64BadgeIcon(int iconId, int* w, int* h) {
    const int i = iconId - PORT_ICON_PM64_BASE;
    const uint8_t* arc;
    size_t size;
    unsigned at;
    int looks;
    if (i >= 0 && i < BADGES) {
        at = kBadges[i].icon;
    } else if (!portPm64FoodIcon(iconId, &at, &looks)) {
        return NULL;
    }
    if (i >= ICONS || (arc = portPm64Segment(PM64_SEG_ICONS, &size)) == NULL || at + ICON_W * ICON_H / 2 + 32 > size) {
        return NULL;
    }
    *w = OUT_WH;
    *h = OUT_WH;
    if (sIconGen[i] != portPm64Generation() + 1) {
        const uint8_t* px = arc + at;
        const uint8_t* pal = px + ICON_W * ICON_H / 2;
        static unsigned char rgba[ICON_H][ICON_W][4];
        int x, y;
        for (y = 0; y < ICON_H; y++) {
            for (x = 0; x < ICON_W; x++) {
                const int c = (px[(y * ICON_W + x) / 2] >> ((x & 1) ? 0 : 4)) & 0xF;
                const unsigned v = (unsigned)pal[c * 2] << 8 | pal[c * 2 + 1];
                rgba[y][x][0] = (unsigned char)(((v >> 11) & 0x1F) * 255 / 31);
                rgba[y][x][1] = (unsigned char)(((v >> 6) & 0x1F) * 255 / 31);
                rgba[y][x][2] = (unsigned char)(((v >> 1) & 0x1F) * 255 / 31);
                rgba[y][x][3] = (v & 1) ? 0xFF : 0x00;
            }
        }
        iconFit((const unsigned char(*)[ICON_W][4])rgba, sIcon[i]);
        sIconGen[i] = portPm64Generation() + 1;
    }
    return sIcon[i];
}

unsigned portPm64IconBinId(unsigned iconId) {
    if (iconId >= PORT_ICON_PM64_BASE && iconId < PORT_ICON_PM64_BASE + PORT_PM64_BADGE_COUNT) {
        const int i = (int)(iconId - PORT_ICON_PM64_BASE);
        return itemDataTable[i < BADGES ? kBadges[i].looks : ITEM_POWER_JUMP].icon_id;
    }
    {
        unsigned at;
        int looks;
        if (portPm64FoodIcon((int)iconId, &at, &looks)) {
            return itemDataTable[looks].icon_id; /* (a dish's: the game's dish's frame) */
        }
    }
    return iconId;
}

/* --- Dizzy Stomp: Sleepy Stomp's weapon as it, while Dizzy Stomp is worn and no Sleepy Stomp --- */

static BattleWeapon sSleepy;
static const char* sSleepyDesc; /* the badge's own description (the Jump list's help shows it; its icon stays Sleepy Stomp's: the battle menu draws icons from icon.bin only) */
static int sSwapped;

void portPm64BadgesBattleFrame(void) {
    PouchData* p = pouchGetPtr();
    int want = 0, i, real = 0;
    if (gp != NULL && gp->inBattle && p != NULL && portPm64BadgeWorn(PORT_PM64_BADGE_DIZZY_STOMP) > 0) {
        for (i = 0; i < MAX_BADGES; i++) {
            real += p->equippedBadges[i] == ITEM_SLEEPY_STOMP;
        }
        want = real == 0;
    }
    if (want && !sSwapped) {
        BattleWeapon* w = &badgeWeapon_NemuraseFumi;
        sSleepy = *w;
        w->name = "pc_pm64_dizzy_stomp";
        w->description = "pc_pm64_dizzy_stomp_desc";
        w->dizzyChance = w->sleepChance > 0 ? w->sleepChance : 100;
        w->dizzyTime = w->sleepTime > 0 ? w->sleepTime : 2;
        w->sleepChance = 0;
        w->sleepTime = 0;
        sSleepyDesc = itemDataTable[ITEM_SLEEPY_STOMP].description;
        itemDataTable[ITEM_SLEEPY_STOMP].description = "pc_pm64_dizzy_stomp_desc";
        sSwapped = 1;
    } else if (!want && sSwapped) {
        badgeWeapon_NemuraseFumi = sSleepy;
        itemDataTable[ITEM_SLEEPY_STOMP].description = sSleepyDesc;
        sSwapped = 0;
    }
}
