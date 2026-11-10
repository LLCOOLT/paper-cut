/* Battle extras' game side (port/include/port/battlex.h): the port's own
 * badges and dishes in the item table, the badges' effects, the new
 * recipes, the badge maker's slots, and the battle grades. The grades'
 * counters are battle state: this file's data is in the save states
 * (CMakeLists.txt's snapshot list). */
#include "port/file_pc.h"
#include "port/file_write.h"
#include "port/achievements.h"
#include "port/battlex.h"
#include "port/postgame_extra.h"
#include "port/enemy_variant.h"
#include "port/pm64_media.h"
#include "port/settings.h"
#include "port/stats.h"
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "data/item_data.h"
#include "mario/mario_pouch.h"

#include <stdio.h>
#include <string.h>

extern BattleWeapon ItemWeaponData_CookingItem;

/* --- the Paper Cut badge set --- */

typedef struct PcBadge {
    const char* name;
    const char* description;
    short grants[PORT_BADGE_GRANTS];
    short icon; /* the badge whose icon it wears */
} PcBadge;

static const PcBadge kBadgeSet[PORT_BADGE_SET_COUNT] = {
    {"Glass Cannon", "Mario's attack goes up by 2, but he takes double damage.",
     {ITEM_POWER_PLUS, ITEM_POWER_PLUS, ITEM_DOUBLE_PAIN}, ITEM_POWER_PLUS},
    {"Paper Armor", "Mario's defence goes up by 2, but his attack drops by 1.",
     {ITEM_DEFEND_PLUS, ITEM_P_DOWN_D_UP}, ITEM_DEFEND_PLUS},
    {"Second Wind", "Now and then, Mario gets back 1 HP and 1 FP at the start of a turn.",
     {ITEM_HAPPY_HEART, ITEM_HAPPY_FLOWER}, ITEM_HAPPY_HEART},
    {"Charmed Life", "Enemies miss Mario more often, far more when he's in Danger.",
     {ITEM_PRETTY_LUCKY, ITEM_CLOSE_CALL}, ITEM_PRETTY_LUCKY},
    {"Leech Fold", "Mario's attacks heal him 1 HP and 1 FP, but hit 2 weaker.",
     {ITEM_HP_DRAIN_BADGE, ITEM_FP_DRAIN}, ITEM_HP_DRAIN_BADGE},
    {"Showstopper", "Appeals fill far more Star Power, and items used give coins back.",
     {ITEM_SUPER_APPEAL, ITEM_SUPER_APPEAL, ITEM_REFUND}, ITEM_SUPER_APPEAL},
    {"Rush Hour", "Mario's attack goes up by 2 in Danger and by 5 in Peril.",
     {ITEM_POWER_RUSH, ITEM_MEGA_RUSH}, ITEM_MEGA_RUSH},
    {"Charge Kit", "Charge and Super Charge in Mario's Tactics menu.",
     {ITEM_CHARGE, ITEM_SUPER_CHARGE}, ITEM_CHARGE},
    {"Stomp Kit", "Power Jump and Multibounce, one badge.",
     {ITEM_POWER_JUMP, ITEM_MULTIBOUNCE}, ITEM_MULTIBOUNCE},
    {"Hammer Kit", "Power Smash and Hammer Throw, one badge.",
     {ITEM_POWER_SMASH, ITEM_HAMMER_THROW}, ITEM_POWER_SMASH},
    {"Treasure Nose", "More hearts, flowers and items from enemies Mario beats.",
     {ITEM_HEART_FINDER, ITEM_FLOWER_FINDER, ITEM_ITEM_HOG}, ITEM_ITEM_HOG},
    {"Paper Shield P", "The partner's defence goes up by 1; enemies miss them more in Danger.",
     {ITEM_DEFEND_PLUS_P, ITEM_CLOSE_CALL_P}, ITEM_DEFEND_PLUS_P},
};

/* --- Zess T.'s new dishes --- */

typedef struct PcDish {
    const char* name;
    const char* description;
    short a, b;  /* the ingredients (either order) */
    short looks; /* the dish whose icon it has */
    u8 hp, fp;
    s8 atk, atkTurns, def, defTurns;
    s8 regenHp, regenFp, regenTurns;
    s8 charge, dodgyTurns, fastTurns, invisibleTurns, paybackTurns;
} PcDish;

static const PcDish kDishes[PORT_DISH_COUNT] = {
    {"Glass Cannon Cake", "Attack +2 for 3 turns, but defence -1. Restores 5 HP.", ITEM_POWER_PUNCH, ITEM_CAKE_MIX,
     ITEM_MOUSSE_CAKE, 5, 0, 2, 3, -1, 3},
    {"Paper Armor Stew", "Defence +2 for 3 turns. Restores 5 HP.", ITEM_COURAGE_SHELL, ITEM_DRIED_SHROOM,
     ITEM_SHROOM_BROTH, 5, 0, 0, 0, 2, 3},
    {"Second Wind Tea", "Restores 2 HP and 2 FP each turn for 5 turns.", ITEM_TURTLEY_LEAF, ITEM_HONEY_SYRUP,
     ITEM_KOOPA_TEA, 0, 0, 0, 0, 0, 0, 2, 2, 5},
    {"Charmed Crepe", "Makes you hard to hit for 3 turns. Restores 5 HP.", ITEM_CAKE_MIX, ITEM_DIZZY_DIAL,
     ITEM_SHROOM_CREPE, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3},
    {"Leech Linguine", "Restores 10 HP and 5 FP, then 1 HP a turn for 4 turns.", ITEM_FRESH_PASTA,
     ITEM_HP_DRAIN_ITEM, ITEM_SPAGHETTI, 10, 5, 0, 0, 0, 0, 1, 0, 4},
    {"Encore Parfait", "Lets you move twice a turn for 2 turns.", ITEM_PEACHY_PEACH, ITEM_STOPWATCH,
     ITEM_FRUIT_PARFAIT, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
    {"Rush Hour Curry", "Attack +3 for 2 turns. Restores 3 HP.", ITEM_HOT_SAUCE, ITEM_POWER_PUNCH, ITEM_SPICY_SOUP,
     3, 0, 3, 2},
    {"Charge Cookies", "Charges your next attack by 3. Restores 5 FP.", ITEM_CAKE_MIX, ITEM_VOLT_SHROOM,
     ITEM_ZESS_COOKIE, 0, 5, 0, 0, 0, 0, 0, 0, 0, 3},
    {"Stomp Stew", "Attack +1 for 4 turns. Restores 10 HP.", ITEM_DRIED_SHROOM, ITEM_POWER_PUNCH, ITEM_SHROOM_ROAST,
     10, 0, 1, 4},
    {"Thorny Hash", "Enemies that hit you take the damage back for 3 turns.", ITEM_COCONUT, ITEM_POWER_PUNCH,
     ITEM_COCONUT_BOMB, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3},
    {"Treasure Tart", "Restores 15 HP and 15 FP.", ITEM_GOLDEN_LEAF, ITEM_CAKE_MIX, ITEM_PEACH_TART, 15, 15},
    {"Ghost Gelato", "Makes you invisible for 2 turns. Restores 5 FP.", ITEM_ICE_STORM, ITEM_BOOS_SHEET,
     ITEM_ICICLE_POP, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
    {"Iron Omelette", "Defence +3 for 2 turns. Restores 5 HP.", ITEM_MYSTIC_EGG, ITEM_COURAGE_SHELL,
     ITEM_OMELETTE_MEAL, 5, 0, 0, 0, 3, 2},
    {"Paper Plane Pie", "Makes you hard to hit for 3 turns. Restores 5 FP.", ITEM_REPEL_CAPE, ITEM_CAKE_MIX,
     ITEM_HEARTFUL_CAKE, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 3},
    {"Heartbeat Jelly", "Restores 20 HP and 20 FP, then 3 HP a turn for 3 turns.", ITEM_JAMMIN_JELLY,
     ITEM_LIFE_SHROOM, ITEM_JELLY_ULTRA, 20, 20, 0, 0, 0, 0, 3, 0, 3},
    {"Slow Simmer", "Restores 3 HP and 3 FP each turn for 5 turns.", ITEM_SLOW_SHROOM, ITEM_GRADUAL_SYRUP,
     ITEM_HONEY_CANDY, 0, 0, 0, 0, 0, 0, 3, 3, 5},
    /* 0.5.0 (port/include/port/postgame_extra.h) */
    {"Hottest Dog", "So spicy it burns! Restores 15 HP and 5 FP. Attack +2 for 3 turns.", ITEM_HOT_DOG,
     ITEM_FIRE_FLOWER, ITEM_HOT_DOG, 15, 5, 2, 3},
};

static BattleWeapon sDishWeapons[PORT_DISH_COUNT];

/* --- names and the table --- */

static char sTags[PORT_ITEM_END - PORT_ITEM_MIN][2][16]; /* the message tags: name, description */
static char sIds[PORT_ITEM_END - PORT_ITEM_MIN][16];
static int sInit;

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

int portItemIsBadge(int id) {
    return id >= PORT_BADGE_SET_MIN && id < PORT_BADGE_SET_MIN + PORT_BADGE_SET_COUNT ||
           id >= PORT_BADGE_CUSTOM_MIN && id < PORT_BADGE_CUSTOM_MIN + PORT_BADGE_CUSTOM_COUNT ||
           id >= PORT_PM64_BADGE_MIN && id < PORT_PM64_BADGE_MIN + portPm64BadgeCount();
}

int portItemIsHeld(int id) {
    return (id >= PORT_DISH_MIN && id < PORT_DISH_MIN + PORT_DISH_COUNT) ||
           portPm64FoodIs(id); /* (Tayce T.'s recipes: held with or without a ROM, so a save keeps them) */
}

/* the badge maker's slots, read from the settings each frame (portBattlexFrame) */
static short sCustom[PORT_BADGE_CUSTOM_COUNT][PORT_BADGE_GRANTS];
static int sCustomIcon[PORT_BADGE_CUSTOM_COUNT];
static char sCustomName[PORT_BADGE_CUSTOM_COUNT][32] PORT_HOST_DATA; /* (badges.txt's: not game state) */

int portBadgeMakerCost(const short grants[PORT_BADGE_GRANTS]) {
    int i, n = 0, sum = 0;
    for (i = 0; i < PORT_BADGE_GRANTS; i++) {
        if (grants[i] >= BADGE_MIN && grants[i] < BADGE_MAX) {
            sum += itemDataTable[grants[i]].bp_cost;
            n++;
        }
    }
    return n == 0 ? 0 : clampi(sum - (n - 1), 1, 99);
}

int portBadgeMakerPrice(int bp) {
    return 20 + 15 * bp;
}

static void fillBadge(int id, const short* grants, int icon) {
    ItemData* d = &itemDataTable[id];
    const int bp = portBadgeMakerCost(grants);
    const ItemData* look = &itemDataTable[icon >= BADGE_MIN && icon < BADGE_MAX ? icon : ITEM_POWER_PLUS];
    d->bp_cost = (s8)bp;
    d->buy_price = d->discount_price = (u16)portBadgeMakerPrice(bp);
    d->sell_price = (u16)(d->buy_price / 2);
    d->star_piece_price = 0;
    d->icon_id = look->icon_id;
    d->type_sort_order = look->type_sort_order;
    d->usable_locations = 0;
    d->battle_params = NULL;
}

void portItemsInit(void) {
    int i;
    if (sInit) {
        return;
    }
    sInit = 1;
    for (i = PORT_ITEM_MIN; i < PORT_ITEM_END; i++) {
        ItemData* d = &itemDataTable[i];
        const int k = i - PORT_ITEM_MIN;
        snprintf(sTags[k][0], sizeof(sTags[k][0]), "pc_in_%03x", i);
        snprintf(sTags[k][1], sizeof(sTags[k][1]), "pc_msg_%03x", i);
        snprintf(sIds[k], sizeof(sIds[k]), "pc_item_%03x", i);
        memset(d, 0, sizeof(*d));
        d->id = sIds[k];
        d->name = sTags[k][0];
        d->description = d->menu_description = sTags[k][1];
        d->icon_id = itemDataTable[ITEM_MYSTERY].icon_id;
    }
    for (i = 0; i < PORT_BADGE_SET_COUNT; i++) {
        fillBadge(PORT_BADGE_SET_MIN + i, kBadgeSet[i].grants, kBadgeSet[i].icon);
    }
    for (i = 0; i < PORT_BADGE_CUSTOM_COUNT; i++) {
        fillBadge(PORT_BADGE_CUSTOM_MIN + i, sCustom[i], sCustomIcon[i]);
    }
    for (i = 0; i < PORT_DISH_COUNT; i++) {
        const PcDish* s = &kDishes[i];
        const int id = PORT_DISH_MIN + i;
        ItemData* d = &itemDataTable[id];
        const ItemData* look = &itemDataTable[s->looks];
        BattleWeapon* w = &sDishWeapons[i];
        d->usable_locations = look->usable_locations;
        d->type_sort_order = (s16)(itemDataTable[ITEM_FRESH_JUICE].type_sort_order + 1 + i);
        d->hp_restored = s->hp;
        d->fp_restored = s->fp;
        d->icon_id = look->icon_id;
        d->buy_price = d->discount_price = (u16)(10 + (s->hp + s->fp) / 2 + 5 * (s->atk + s->def + s->charge > 0));
        d->sell_price = (u16)(d->buy_price / 2 + 2);
        *w = ItemWeaponData_CookingItem;
        w->itemId = id;
        if (s->atk != 0) {
            w->atkChangeChance = 100;
            w->atkChangeTime = s->atkTurns;
            w->atkChangeStrength = s->atk;
        }
        if (s->def != 0) {
            w->defChangeChance = 100;
            w->defChangeTime = s->defTurns;
            w->defChangeStrength = s->def;
        }
        if (s->regenTurns > 0) {
            w->hpRegenTime = s->regenHp > 0 ? s->regenTurns : 0;
            w->hpRegenStrength = s->regenHp;
            w->fpRegenTime = s->regenFp > 0 ? s->regenTurns : 0;
            w->fpRegenStrength = s->regenFp;
        }
        w->chargeStrength = s->charge;
        if (s->dodgyTurns > 0) {
            w->dodgyChance = 100;
            w->dodgyTime = s->dodgyTurns;
        }
        if (s->fastTurns > 0) {
            w->fastChance = 100;
            w->fastTime = s->fastTurns;
        }
        if (s->invisibleTurns > 0) {
            w->invisibleChance = 100;
            w->invisibleTime = s->invisibleTurns;
        }
        w->paybackTime = s->paybackTurns;
        d->battle_params = w;
    }
    portPm64FoodFill(); /* Paper Mario 64's dishes (pm64_food_pc.c) */
}

const char* portItemName(int id) {
    if (id >= PORT_BADGE_SET_MIN && id < PORT_BADGE_SET_MIN + PORT_BADGE_SET_COUNT) {
        return kBadgeSet[id - PORT_BADGE_SET_MIN].name;
    }
    if (id >= PORT_BADGE_CUSTOM_MIN && id < PORT_BADGE_CUSTOM_MIN + PORT_BADGE_CUSTOM_COUNT) {
        return sCustomName[id - PORT_BADGE_CUSTOM_MIN];
    }
    if (id >= PORT_DISH_MIN && id < PORT_DISH_MIN + PORT_DISH_COUNT) { /* (Paper Mario's dishes: pm64_badges_aurora.c) */
        return kDishes[id - PORT_DISH_MIN].name;
    }
    return "";
}

const char* portItemDescription(int id) {
    if (id >= PORT_BADGE_SET_MIN && id < PORT_BADGE_SET_MIN + PORT_BADGE_SET_COUNT) {
        return kBadgeSet[id - PORT_BADGE_SET_MIN].description;
    }
    if (id >= PORT_DISH_MIN && id < PORT_DISH_MIN + PORT_DISH_COUNT) { /* (Paper Mario's dishes: pm64_badges_aurora.c) */
        return kDishes[id - PORT_DISH_MIN].description;
    }
    return "";
}

/* --- the badges' effects --- */

int portBadgeGrants(int id, const short** grants) {
    const short* g;
    int n;
    if (id >= PORT_BADGE_SET_MIN && id < PORT_BADGE_SET_MIN + PORT_BADGE_SET_COUNT) {
        g = kBadgeSet[id - PORT_BADGE_SET_MIN].grants;
    } else if (id >= PORT_BADGE_CUSTOM_MIN && id < PORT_BADGE_CUSTOM_MIN + PORT_BADGE_CUSTOM_COUNT) {
        g = sCustom[id - PORT_BADGE_CUSTOM_MIN];
    } else if (id >= PORT_PM64_BADGE_MIN && id < PORT_PM64_BADGE_MIN + PORT_PM64_BADGE_COUNT) {
        return portPm64BadgeGrants(id, grants); /* Paper Mario 64's (pm64_badges_pc.c) */
    } else {
        *grants = NULL;
        return 0;
    }
    for (n = 0; n < PORT_BADGE_GRANTS && g[n] != 0; n++) {
    }
    *grants = g;
    return n;
}

int portBadgeEquippedGrants(int baseId) {
    PouchData* p = pouchGetPtr();
    int i, k, count = 0;
    if (p == NULL) {
        return 0;
    }
    for (i = 0; i < MAX_BADGES; i++) {
        const int id = p->equippedBadges[i];
        const short* g;
        int n;
        if (id < PORT_ITEM_MIN || (n = portBadgeGrants(id, &g)) == 0) {
            continue;
        }
        for (k = 0; k < n; k++) {
            count += g[k] == baseId;
        }
    }
    return count;
}

/* --- recipes --- */

static int settingOn(const char* key) {
    int* v = portSettingsDeclare(key, 0, 0, 1);
    return v != NULL && *v != 0;
}

int portRecipeCook(int a, int b) {
    int i;
    const int jp = portPgRecipeCook(a, b); /* the Japanese release's recipes (postgame_extra.h) */
    if (jp != 0) {
        return jp;
    }
    if (!settingOn("pc_recipes")) {
        return portPm64RecipeCook(a, b); /* Tayce T.'s (pm64_food_pc.c) */
    }
    for (i = 0; i < PORT_DISH_COUNT; i++) {
        if ((kDishes[i].a == a && kDishes[i].b == b) || (kDishes[i].a == b && kDishes[i].b == a)) {
            return PORT_DISH_MIN + i;
        }
    }
    return portPm64RecipeCook(a, b);
}

int portRecipeCook1(int a) {
    return portPm64RecipeCook1(a); /* Tayce T.'s (pm64_food_pc.c) */
}

/* --- the badge maker --- */

/* (declared once each: asked for every frame; the settings' table never moves) */
static int* sMakerKeys[PORT_BADGE_CUSTOM_COUNT][2] PORT_HOST_DATA;

/* a made badge's effects packed in one setting: 7 bits each (a badge's index + 1) */
#define MAKER_BITS 7
#define MAKER_MASK ((1 << MAKER_BITS) - 1)

static int* makerKey(int slot, const char* what) {
    const int which = what[0] == 'f' ? 0 : 1; /* "fx", "icon" */
    if (sMakerKeys[slot][which] == NULL) {
        char key[40];
        snprintf(key, sizeof(key), "badge_maker_%d_%s", slot, what);
        sMakerKeys[slot][which] = portSettingsDeclare(key, 0, 0, (1 << (MAKER_BITS * PORT_BADGE_GRANTS)) - 1);
    }
    return sMakerKeys[slot][which];
}

int portBadgeMakerGet(int slot, short grants[PORT_BADGE_GRANTS], int* iconBadge) {
    const int* fx;
    const int* icon;
    int i, n = 0;
    if (slot < 0 || slot >= PORT_BADGE_CUSTOM_COUNT) {
        return 0;
    }
    fx = makerKey(slot, "fx");
    icon = makerKey(slot, "icon");
    for (i = 0; i < PORT_BADGE_GRANTS; i++) {
        const int v = fx != NULL ? (*fx >> (MAKER_BITS * i)) & MAKER_MASK : 0;
        grants[i] = (short)(v > 0 && v <= BADGE_COUNT ? BADGE_MIN + v - 1 : 0);
    }
    /* packed: no gaps */
    for (i = 0; i < PORT_BADGE_GRANTS; i++) {
        if (grants[i] != 0) {
            grants[n++] = grants[i];
        }
    }
    for (i = n; i < PORT_BADGE_GRANTS; i++) {
        grants[i] = 0;
    }
    *iconBadge = icon != NULL && *icon > 0 && *icon <= BADGE_COUNT ? BADGE_MIN + *icon - 1 : 0;
    return n;
}

void portBadgeMakerSet(int slot, const short grants[PORT_BADGE_GRANTS], int iconBadge) {
    int* fx;
    int* icon;
    int i, v = 0, n = 0;
    if (slot < 0 || slot >= PORT_BADGE_CUSTOM_COUNT) {
        return;
    }
    fx = makerKey(slot, "fx");
    icon = makerKey(slot, "icon");
    for (i = 0; i < PORT_BADGE_GRANTS; i++) {
        if (grants[i] >= BADGE_MIN && grants[i] < BADGE_MAX) {
            v |= (grants[i] - BADGE_MIN + 1) << (MAKER_BITS * n++);
        }
    }
    if (fx != NULL) {
        *fx = v;
    }
    if (icon != NULL) {
        *icon = iconBadge >= BADGE_MIN && iconBadge < BADGE_MAX ? iconBadge - BADGE_MIN + 1 : 0;
    }
    portBattlexRefresh();
}

void portBadgeMakerSetName(int slot, const char* name) {
    if (slot >= 0 && slot < PORT_BADGE_CUSTOM_COUNT) {
        snprintf(sCustomName[slot], sizeof(sCustomName[slot]), "%s", name != NULL ? name : "");
    }
}

const char* portBadgeMakerName(int slot) {
    return slot >= 0 && slot < PORT_BADGE_CUSTOM_COUNT ? sCustomName[slot] : "";
}

unsigned portBattlexRefresh(void) {
    static unsigned sChanges PORT_HOST_DATA; /* (only ever counts up, save states or not) */
    int i;
    portItemsInit();
    for (i = 0; i < PORT_BADGE_CUSTOM_COUNT; i++) {
        short g[PORT_BADGE_GRANTS];
        int icon;
        portBadgeMakerGet(i, g, &icon);
        if (memcmp(g, sCustom[i], sizeof(g)) != 0 || icon != sCustomIcon[i]) {
            memcpy(sCustom[i], g, sizeof(g));
            sCustomIcon[i] = icon;
            fillBadge(PORT_BADGE_CUSTOM_MIN + i, sCustom[i], icon);
            sChanges++;
        }
    }
    return sChanges;
}

int (*gPortMerchantStockHook)(int* out, int max);

int portMerchantStock(int* out, int max) {
    int i, n = 0;
    /* (the merchant's own, always: Rowf's list reads gPortMerchantStockHook itself, portMerchantListStock. When
     * this answered with Rowf's stock while his list was up, the merchant's frame (battlex_aurora.c) found stock
     * with pc_badges off and set himself up mid-talk; evt_npc_setup put the field back in its walking mode, and
     * Rowf's talk never gave Mario back his keys: the game frozen once the talk was left) */
    portItemsInit();
    if (settingOn("pc_badges")) {
        for (i = 0; i < PORT_BADGE_SET_COUNT && n < max; i++) {
            out[n++] = PORT_BADGE_SET_MIN + i;
        }
    }
    for (i = 0; i < PORT_BADGE_CUSTOM_COUNT && n < max; i++) {
        if (sCustom[i][0] != 0) {
            out[n++] = PORT_BADGE_CUSTOM_MIN + i;
        }
    }
    return n;
}

int portMerchantListStock(int* out, int max) {
    return gPortMerchantStockHook != NULL ? gPortMerchantStockHook(out, max) : portMerchantStock(out, max);
}

/* --- battle grades --- */

static int sStylish, sSuperguards, sEnemies;
/* the recap's (port/include/port/stats.h): guards, damage dealt, the best hit, enemies defeated */
static int sGuards, sDealt, sBestHit, sDefeated;
static PortBattleRecap sRecap;
static unsigned sRecapSerial;
static PortBattleGrade sLast;
static unsigned sSerial;
/* the grades' totals, battle_grades.txt's: kept out of save states (a state loaded would count them back) */
static int sCounts[PORT_GRADE_COUNT] PORT_HOST_DATA;
static int sCountsRead PORT_HOST_DATA, sCountsDirty PORT_HOST_DATA;
void (*gPortBattlexOverHook)(struct BattleWork* wp, int result);

const char* portBattleGradeLetter(int grade) {
    static const char* const kLetters[PORT_GRADE_COUNT] = {"D", "C", "B", "A", "S"};
    return grade >= 0 && grade < PORT_GRADE_COUNT ? kLetters[grade] : "";
}

static void countsPath(char* out, size_t size) {
    snprintf(out, size, "%s/battle_grades.txt", portSettingsUserPath());
}

static void readCounts(void) {
    char path[1024];
    FILE* f;
    if (sCountsRead) {
        return;
    }
    sCountsRead = 1;
    countsPath(path, sizeof(path));
    if ((f = portFopen(path, "r")) != NULL) {
        char line[64];
        while (fgets(line, sizeof(line), f) != NULL) {
            char letter;
            int n;
            if (sscanf(line, "%c %d", &letter, &n) == 2) {
                int g;
                for (g = 0; g < PORT_GRADE_COUNT; g++) {
                    if (letter == portBattleGradeLetter(g)[0]) {
                        sCounts[g] += n;
                    }
                }
            }
        }
        fclose(f);
    }
}

void portBattleGradesSave(void) {
    char path[1024];
    FILE* f;
    int g;
    if (!sCountsDirty) {
        return;
    }
    sCountsDirty = 0;
    countsPath(path, sizeof(path));
    if ((f = portFileWriteOpen(path, "w")) != NULL) {
        fprintf(f, "# Paper Cut battle grades: battles won with each grade\n");
        for (g = PORT_GRADE_COUNT - 1; g >= 0; g--) {
            fprintf(f, "%s %d\n", portBattleGradeLetter(g), sCounts[g]);
        }
        portFileWriteClose(f, path);
    }
}

unsigned portBattleGradeLast(PortBattleGrade* out) {
    if (out != NULL) {
        *out = sLast;
    }
    return sLast.serial;
}

int portBattleGradeCount(int grade) {
    readCounts();
    return grade >= 0 && grade < PORT_GRADE_COUNT ? sCounts[grade] : 0;
}

void portBattlexBattleStart(int numEnemies) {
    sStylish = 0;
    sSuperguards = 0;
    sEnemies = numEnemies;
    sGuards = 0;
    sDealt = 0;
    sBestHit = 0;
    sDefeated = 0;
}

void portBattlexStylish(void) {
    sStylish++;
}

void portBattlexSuperguard(void) {
    sSuperguards++;
}

void portBattlexGuard(void) {
    sGuards++;
}

void portBattlexHit(int attackerAlliance, int targetAlliance, int damage, int heal) {
    /* Mario's side's hits on the enemies (or no one's: a Superguard's counter, poison) */
    if (damage > 0 && !heal && targetAlliance == 1 && (attackerAlliance == 0 || attackerAlliance < 0)) {
        sDealt += damage;
        if (damage > sBestHit) {
            sBestHit = damage;
        }
    }
}

void portBattlexDefeated(int kind) {
    sDefeated++;
    portStatsEnemyDefeated(kind);
}

unsigned portBattleRecapLast(PortBattleRecap* out) {
    if (out != NULL) {
        *out = sRecap;
    }
    return sRecap.serial;
}

int portBattlexBattleWon(BattleWork* wp) {
    PortBattleGrade g;
    const BattleActRecord* r = &wp->actRecord;
    /* (Paper Mario 64's Pay-Off: a coin for each point of damage Mario took, per badge worn) */
    int coins = portEnemyVariantCoins() + r->marioDamageTaken * portPm64PayOff();
    /* the recap's numbers (the card, the lifetime stats): counted whether it's shown or not */
    memset(&sRecap, 0, sizeof(sRecap));
    sRecap.damageDealt = sDealt;
    sRecap.damageTaken = r->marioDamageTaken + r->partnerDamageTaken;
    sRecap.bestHit = sBestHit;
    sRecap.superguards = sSuperguards;
    sRecap.guards = sGuards;
    sRecap.stylish = sStylish;
    sRecap.turns = r->turnCount;
    sRecap.fpUsed = r->marioFpSpent + r->partnerFpSpent;
    sRecap.itemsUsed = r->marioAttackItems + r->marioSupportItems + r->partnerAttackItems + r->partnerSupportItems;
    sRecap.enemies = sDefeated;
    sRecap.starPoints = wp->stockExp <= 0 ? 1 : wp->stockExp >= 100 ? 100 : wp->stockExp; /* (BattleGetStockExp's) */
    sRecap.grade = -1;
    sRecap.serial = ++sRecapSerial;
    if (!settingOn("battle_grades")) {
        return coins;
    }
    memset(&g, 0, sizeof(g));
    g.stylish = sStylish;
    g.superguards = sSuperguards;
    g.damageTaken = wp->actRecord.marioDamageTaken + wp->actRecord.partnerDamageTaken;
    g.turns = wp->actRecord.turnCount;
    g.par = 1 + sEnemies / 2;
    g.score = 70 + (sStylish * 5 < 25 ? sStylish * 5 : 25) + (sSuperguards * 10 < 30 ? sSuperguards * 10 : 30) -
              (g.damageTaken * 3 < 60 ? g.damageTaken * 3 : 60) - (g.turns > g.par ? (g.turns - g.par) * 10 : 0) +
              (g.damageTaken == 0 ? 10 : 0);
    g.grade = g.score >= 100 ? PORT_GRADE_S
              : g.score >= 85 ? PORT_GRADE_A
              : g.score >= 70 ? PORT_GRADE_B
              : g.score >= 50 ? PORT_GRADE_C
                              : PORT_GRADE_D;
    g.bonusCoins = g.grade == PORT_GRADE_S ? 2 + sEnemies : g.grade == PORT_GRADE_A ? 1 : 0;
    g.serial = ++sSerial;
    sLast = g;
    readCounts();
    sCounts[g.grade]++;
    sCountsDirty = 1;
    sRecap.grade = g.grade;
    sRecap.bonusCoins = g.bonusCoins;
    return coins + g.bonusCoins;
}

void portBattlexBattleOver(BattleWork* wp, int result) {
    const BattleActRecord* r = &wp->actRecord;
    portStatsBattleOver(result, result == 1 ? &sRecap : NULL, r->marioJumpMoves, r->marioHammerMoves,
                        r->marioSpecialAttacks + r->marioSpecialSupports,
                        r->partnerAttackMoves + r->partnerSupportMoves,
                        r->marioAttackItems + r->marioSupportItems + r->partnerAttackItems + r->partnerSupportItems,
                        r->marioFpSpent + r->partnerFpSpent, r->marioAppeals + r->partnerAppeals, r->turnCount,
                        r->marioDamageTaken + r->partnerDamageTaken);
    portAchievementsBattleOver(wp, result); /* the battle feats (port/include/port/achievements.h) */
    if (gPortBattlexOverHook != NULL) {
        gPortBattlexOverHook(wp, result);
    }
    portEnemyVariantBattleEnd();
}
