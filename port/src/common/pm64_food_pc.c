/* Tayce T.'s recipes (docs/PM64_EXTRAS.md, port/include/port/pm64_media.h): Paper Mario 64's dishes the
 * Thousand-Year Door hasn't got, and the ingredients for them it hasn't got either, as items
 * PORT_PM64_FOOD_MIN + i (port/include/port/battlex.h). Zess T. cooks them from Paper Mario's own recipes
 * (gor_01.c cook_chk/cook_chk2 through portRecipeCook1/portRecipeCook and portPm64RecipeFirst); Rowf sells the
 * ingredients (pm64_badges_aurora.c); the Start menu's recipe book lists them (qol_pc.c).
 *
 * Their names, descriptions and what they restore are the linked ROM's (its item table, PM64_SEG_ITEMS), their
 * icons its icon archive's; the numbers below are Paper Mario's too, for a ROM linked before the item table was
 * read and for a save that holds them with no ROM linked (they stay food: the game's look, the English name).
 *
 * Effects on the game's machinery: a dish restores HP and FP as Zess T.'s dishes do (ItemWeaponData_CookingItem's
 * recovery); Super Soda also cures as a Tasty Tonic; the Egg Missile is thrown as an Egg Bomb; the Kooky Cookie
 * and the Strange Cake electrify, hide or put the eater to sleep as the Love Pudding does (re-picked each turn).
 *
 * Recipes: Paper Mario's tables (Tayce T.'s one-ingredient list, her 29 x 29 two-ingredient grid and her eight
 * dish-with-dish recipes), with its ingredients as the game's own where the game has them (Koopa Leaf as the
 * Turtley Leaf, Dried Pasta as Fresh Pasta, the Egg as the Mystic Egg; both games make Koopa Tea, Spaghetti and
 * the Fried Egg from them). Paper Mario's results the game has (Koopa Tea, Spaghetti, Electro Pop, Fire Pop,
 * Honey Shroom ...) are the game's items. A pair the game has its own recipe for stays the game's, except four
 * pairs whose game dish has other recipes, so that four dishes can be made at all (kFirst). Left out: the
 * recipes that need a berry or Dried Fruit (every dish they make has another way), the Coco Pop and the Jelly
 * Pop (their only recipes are the game's Coco Candy's and Jelly Candy's, much the same dishes), the Mistakes.
 *
 * Sources (the Paper Mario decompilation, papermario on GitHub): kFood's HP, FP, sell price and field use are
 * src/item_table.yaml's potencyA, potencyB, sellValue and ITEM_TYPE_FLAG_WORLD_USABLE for each gItemTable index;
 * the recipes are src/recipes.yaml's (Tayce T.'s tables); in battle src/battle/use_items.c gives the Super Soda
 * its own script (FP and a cure), the Egg Missile a thrown one (potencyA damage), and the Kooky Cookie the Strange
 * Cake's (BTL_ITEM(strange_cake) twice: the same random ailment), the rest the generic food script.
 *
 * Only with a ROM linked and "pm64_recipes" on; online only when every player has the ROM. */
#include "port/file_pc.h"
#include "port/file_write.h"
#include "port/battlex.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/practice.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "pc_call.h"
#include "battle/battle.h"
#include "battle/battle_sub.h"
#include "battle/battle_database_common.h"
#include "battle/battle_item_data.h"
#include "battle/battle_camera.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_status_effect.h"
#include "battle/battle_unit.h"
#include "evt/evt_cmd.h"
#include "evt/evt_snd.h"
#include "data/item_data.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern BattleWeapon ItemWeaponData_CookingItem;
extern EvtWord ItemEvent_Recovery_Core_Effect[]; /* (src/data/battle/battle_item_data.c) */
extern EvtWord _return_home_event[];
extern BattleWeapon ItemWeaponData_FirstLovePuddingBiribiri;
extern BattleWeapon ItemWeaponData_FirstLovePuddingTrans; /* (src/data/battle/battle_item_data.c) */
extern BattleWeapon ItemWeaponData_FirstLovePuddingSleep;

enum { KIND_FOOD, KIND_SODA, KIND_THROWN, KIND_RANDOM };

typedef struct Pm64Food {
    const char* english;
    short pm64;      /* gItemTable index */
    unsigned icon;   /* the icon archive's offset of its picture (CI4 32x32, its palette after) */
    short looks;     /* the game's item it looks like with no ROM (and sorts after) */
    u8 hp, fp, sell; /* Paper Mario's */
    u8 kind;
    u8 field;        /* usable in the field */
} Pm64Food;

#define F(i) (PORT_PM64_FOOD_MIN + (i))
static const Pm64Food kFood[] = {
    /* the dishes */
    {"Fried Shroom", 0xB6, 0x31A0, ITEM_SHROOM_FRY, 6, 2, 5, KIND_FOOD, 1},
    {"Nutty Cake", 0xD6, 0x79A0, ITEM_CHOCO_CAKE, 0, 10, 6, KIND_FOOD, 1},
    {"Big Cookie", 0xC0, 0x4820, ITEM_ZESS_COOKIE, 0, 20, 10, KIND_FOOD, 1},
    {"Boiled Egg", 0xD8, 0x7E20, ITEM_FRIED_EGG, 8, 8, 10, KIND_FOOD, 1},
    {"Super Soda", 0x9B, 0x24540, ITEM_TASTY_TONIC, 0, 5, 3, KIND_SODA, 1},
    {"Yoshi Cookie", 0xD9, 0x8060, ITEM_ZESS_COOKIE, 15, 15, 20, KIND_FOOD, 1},
    {"Deluxe Feast", 0xBE, 0x43A0, ITEM_ZESS_DELUXE, 40, 40, 60, KIND_FOOD, 1},
    {"Frozen Fries", 0xD4, 0x7520, ITEM_SHROOM_FRY, 15, 0, 15, KIND_FOOD, 1},
    {"Potato Salad", 0xD5, 0x7760, ITEM_HEALTHY_SALAD, 10, 0, 6, KIND_FOOD, 1},
    {"Hot Shroom", 0xB9, 0x3860, ITEM_SHROOM_ROAST, 15, 5, 15, KIND_FOOD, 1},
    {"Bland Meal", 0xBD, 0x3F20, ITEM_ZESS_DINNER, 10, 10, 10, KIND_FOOD, 1},
    {"Yummy Meal", 0xBB, 0x4160, ITEM_ZESS_SPECIAL, 20, 20, 15, KIND_FOOD, 1},
    {"Sweet Shroom", 0xBA, 0x3AA0, ITEM_SHROOM_CAKE, 30, 20, 50, KIND_FOOD, 1},
    {"Egg Missile", 0xC8, 0x5A20, ITEM_EGG_BOMB, 6, 0, 10, KIND_THROWN, 0},
    {"Special Shake", 0xBF, 0x45E0, ITEM_ZESS_FRAPPE, 0, 20, 10, KIND_FOOD, 1},
    {"Healthy Juice", 0xBC, 0x3CE0, ITEM_FRESH_JUICE, 0, 40, 25, KIND_FOOD, 1},
    {"Lime Candy", 0xCE, 0x67A0, ITEM_COCO_CANDY, 0, 20, 15, KIND_FOOD, 1},
    {"Lemon Candy", 0xD0, 0x6C20, ITEM_HONEY_CANDY, 5, 15, 15, KIND_FOOD, 1},
    {"Apple Pie", 0xB1, 0x2660, ITEM_PEACH_TART, 5, 15, 10, KIND_FOOD, 1},
    {"Kooky Cookie", 0xD3, 0x72E0, ITEM_ZESS_COOKIE, 0, 15, 12, KIND_RANDOM, 1},
    {"Strange Cake", 0xD2, 0x70A0, ITEM_LOVE_PUDDING, 0, 0, 10, KIND_RANDOM, 0},
    /* the ingredients (Rowf's) */
    {"Iced Potato", 0xAF, 0x26B80, ITEM_DRIED_SHROOM, 5, 0, 3, KIND_FOOD, 1},
    {"Goomnut", 0xA5, 0x25080, ITEM_COCONUT, 0, 3, 2, KIND_FOOD, 1},
    {"Melon", 0xAD, 0x26700, ITEM_KEEL_MANGO, 15, 0, 7, KIND_FOOD, 1},
    {"Lime", 0x9D, 0x25740, ITEM_KEEL_MANGO, 0, 3, 1, KIND_FOOD, 1},
    {"Lemon", 0x9C, 0x25980, ITEM_PEACHY_PEACH, 1, 2, 1, KIND_FOOD, 1},
    {"Apple", 0x94, 0x276C0, ITEM_PEACHY_PEACH, 5, 0, 2, KIND_FOOD, 1},
    {"Strange Leaf", 0xA9, 0x25E00, ITEM_GOLDEN_LEAF, 0, 5, 2, KIND_FOOD, 1},
    {"Stinky Herb", 0xAE, 0x26940, ITEM_HORSETAIL, 0, 5, 1, KIND_FOOD, 1},
};
#define FOODS (int)(sizeof(kFood) / sizeof(kFood[0]))
#define DISHES 21 /* kFood[0..DISHES): the dishes; the rest Rowf's ingredients */
typedef char kFoodFits[FOODS <= PORT_PM64_FOOD_COUNT ? 1 : -1];

typedef struct Pm64Recipe {
    short a, b, out; /* b 0: one ingredient */
} Pm64Recipe;

/* Paper Mario's recipes whose pair the game cooks as something else, ahead of the game's: the only ways left to
 * make these four (the game's dish has other recipes: Shroom Fry eight, Shroom Roast thirteen, Shroom Cake
 * four, Egg Bomb two) */
static const Pm64Recipe kFirst[] = {
    {ITEM_MUSHROOM, ITEM_DRIED_SHROOM, F(0)},     /* Fried Shroom */
    {ITEM_SUPER_SHROOM, ITEM_DRIED_SHROOM, F(9)}, /* Hot Shroom */
    {ITEM_LIFE_SHROOM, ITEM_CAKE_MIX, F(12)},     /* Sweet Shroom */
    {ITEM_FIRE_FLOWER, ITEM_MYSTIC_EGG, F(13)},   /* Egg Missile */
};

/* the rest, after the game's own and Paper Cut's (each pair the game has no recipe for) */
static const Pm64Recipe kRecipes[] = {
    {F(24), 0, ITEM_TASTY_TONIC},                  /* Lime */
    {F(25), 0, ITEM_TASTY_TONIC},                  /* Lemon */
    {F(26), 0, F(4)},                              /* Apple: Super Soda */
    {F(22), 0, F(1)},                              /* Goomnut: Nutty Cake */
    {F(27), 0, ITEM_DIZZY_DIAL},                   /* Strange Leaf */
    {F(23), 0, F(14)},                             /* Melon: Special Shake */
    {F(21), 0, F(8)},                              /* Iced Potato: Potato Salad */
    {ITEM_MUSHROOM, F(22), ITEM_VOLT_SHROOM},      /* + Goomnut */
    {ITEM_MUSHROOM, F(27), ITEM_VOLT_SHROOM},      /* + Strange Leaf */
    {ITEM_MUSHROOM, F(21), F(10)},                 /* + Iced Potato: Bland Meal */
    {ITEM_SUPER_SHROOM, F(22), ITEM_LIFE_SHROOM},  /* + Goomnut */
    {ITEM_SUPER_SHROOM, F(27), ITEM_LIFE_SHROOM},  /* + Strange Leaf */
    {ITEM_SUPER_SHROOM, F(21), F(10)},             /* + Iced Potato: Bland Meal */
    {ITEM_ULTRA_SHROOM, F(22), ITEM_LIFE_SHROOM},  /* + Goomnut */
    {ITEM_ULTRA_SHROOM, F(27), ITEM_LIFE_SHROOM},  /* + Strange Leaf */
    {ITEM_ULTRA_SHROOM, F(21), F(11)},             /* + Iced Potato: Yummy Meal */
    {ITEM_LIFE_SHROOM, F(22), ITEM_LIFE_SHROOM},   /* + Goomnut */
    {ITEM_LIFE_SHROOM, F(27), ITEM_LIFE_SHROOM},   /* + Strange Leaf */
    {ITEM_LIFE_SHROOM, F(21), F(10)},              /* + Iced Potato: Bland Meal */
    {ITEM_VOLT_SHROOM, ITEM_FIRE_FLOWER, F(10)},   /* Bland Meal */
    {ITEM_VOLT_SHROOM, F(22), ITEM_LIFE_SHROOM},   /* + Goomnut */
    {ITEM_VOLT_SHROOM, ITEM_TURTLEY_LEAF, ITEM_LIFE_SHROOM},
    {ITEM_VOLT_SHROOM, F(27), ITEM_DIZZY_DIAL},    /* + Strange Leaf */
    {ITEM_VOLT_SHROOM, ITEM_MYSTIC_EGG, F(10)},    /* Bland Meal */
    {ITEM_VOLT_SHROOM, F(21), F(10)},              /* + Iced Potato: Bland Meal */
    {ITEM_FIRE_FLOWER, F(22), F(10)},              /* + Goomnut: Bland Meal */
    {ITEM_FIRE_FLOWER, F(21), F(7)},               /* + Iced Potato: Frozen Fries */
    {ITEM_COCONUT, ITEM_HONEY_SYRUP, ITEM_TASTY_TONIC},
    {ITEM_COCONUT, ITEM_MAPLE_SYRUP, F(4)},        /* Super Soda */
    {ITEM_COCONUT, ITEM_JAMMIN_JELLY, F(14)},      /* Special Shake */
    {ITEM_COCONUT, F(26), F(4)},                   /* + Apple: Super Soda */
    {ITEM_COCONUT, F(23), F(4)},                   /* + Melon: Super Soda */
    {ITEM_COCONUT, F(21), F(10)},                  /* + Iced Potato: Bland Meal */
    {F(24), F(25), F(4)},                          /* Lime + Lemon: Super Soda */
    {F(24), ITEM_HONEY_SYRUP, ITEM_TASTY_TONIC},
    {F(24), ITEM_MAPLE_SYRUP, F(4)},
    {F(24), ITEM_JAMMIN_JELLY, F(14)},
    {F(24), F(26), F(4)},
    {F(24), ITEM_TURTLEY_LEAF, F(4)},
    {F(24), ITEM_CAKE_MIX, F(16)},                 /* Lime Candy */
    {F(24), F(23), F(14)},
    {F(25), ITEM_HONEY_SYRUP, ITEM_TASTY_TONIC},
    {F(25), ITEM_MAPLE_SYRUP, F(4)},
    {F(25), ITEM_JAMMIN_JELLY, F(14)},
    {F(25), F(26), F(4)},
    {F(25), ITEM_TURTLEY_LEAF, F(4)},
    {F(25), ITEM_CAKE_MIX, F(17)},                 /* Lemon Candy */
    {F(25), F(23), F(14)},
    {ITEM_HONEY_SYRUP, F(26), ITEM_TASTY_TONIC},
    {ITEM_HONEY_SYRUP, F(23), F(14)},
    {ITEM_MAPLE_SYRUP, F(26), F(4)},
    {ITEM_MAPLE_SYRUP, F(23), F(14)},
    {ITEM_JAMMIN_JELLY, F(26), F(14)},
    {ITEM_JAMMIN_JELLY, F(23), F(14)},
    {F(26), ITEM_CAKE_MIX, F(18)},                 /* Apple Pie */
    {F(26), F(23), F(14)},
    {F(22), ITEM_TURTLEY_LEAF, F(10)},
    {F(22), ITEM_FRESH_PASTA, F(10)},
    {F(22), F(27), F(10)},
    {F(22), ITEM_CAKE_MIX, F(2)},                  /* Big Cookie */
    {ITEM_TURTLEY_LEAF, ITEM_CAKE_MIX, F(19)},     /* Kooky Cookie */
    {ITEM_TURTLEY_LEAF, F(23), F(14)},
    {ITEM_FRESH_PASTA, F(27), F(10)},
    {ITEM_FRESH_PASTA, F(21), F(10)},
    {F(27), ITEM_CAKE_MIX, F(20)},                 /* Strange Cake */
    {F(27), ITEM_MYSTIC_EGG, F(3)},                /* Boiled Egg */
    {F(27), F(23), F(14)},
    {F(27), F(28), ITEM_DIZZY_DIAL},
    {F(27), F(21), F(11)},                         /* Yummy Meal */
    {ITEM_CAKE_MIX, F(23), F(5)},                  /* Yoshi Cookie */
    {ITEM_CAKE_MIX, F(28), F(19)},                 /* Kooky Cookie */
    {ITEM_CAKE_MIX, F(21), F(10)},
    {ITEM_MYSTIC_EGG, F(28), F(3)},                /* Boiled Egg */
    {ITEM_MYSTIC_EGG, F(21), F(10)},
    /* a dish and something: Tayce T.'s eight */
    {F(14), ITEM_DRIED_SHROOM, F(15)},             /* Special Shake: Healthy Juice */
    {F(14), F(27), F(15)},
    {F(14), F(28), F(15)},
    {F(8), ITEM_SPAGHETTI, F(11)},                 /* Potato Salad: Yummy Meal */
    {F(8), F(0), F(10)},                           /* + Fried Shroom: Bland Meal */
    {F(8), F(9), F(11)},                           /* + Hot Shroom: Yummy Meal */
    {F(8), ITEM_SHROOM_STEAK, F(6)},               /* + Shroom Steak: Deluxe Feast */
    {F(27), ITEM_WHACKA_BUMP, F(6)},               /* Strange Leaf + Whacka Bump: Deluxe Feast */
};

int portPm64FoodActive(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_recipes", 0, 0, 1);
    }
    return *sOn != 0 && portPm64Linked() != PM64_NONE && (!portStateSyncLinked() || portPm64PartnersActive());
}

int portPm64FoodCount(void) {
    return FOODS;
}

int portPm64DishCount(void) {
    return DISHES;
}

static int foodOf(int id) {
    const int i = id - PORT_PM64_FOOD_MIN;
    return i >= 0 && i < FOODS ? i : -1;
}

int portPm64FoodIs(int id) {
    return foodOf(id) >= 0;
}

const char* portPm64FoodEnglish(int id) {
    const int i = foodOf(id);
    return i >= 0 ? kFood[i].english : "";
}

/* HP and FP: the ROM's, else ours */
static void restores(int i, int* hp, int* fp) {
    Pm64Item it;
    *hp = kFood[i].hp;
    *fp = kFood[i].fp;
    if (portPm64Item(kFood[i].pm64, &it) && it.potencyA >= 0 && it.potencyB >= 0) {
        *hp = it.potencyA;
        *fp = it.potencyB;
    }
}

int portPm64FoodText(int id, char* name, size_t nameSize, char* desc, size_t descSize) {
    const int i = foodOf(id);
    Pm64Item it;
    name[0] = desc[0] = '\0';
    if (i < 0 || !portPm64Item(kFood[i].pm64, &it)) {
        return 0;
    }
    if (portPm64MessageText(it.nameMsg, 0, name, nameSize) <= 0) {
        snprintf(name, nameSize, "%s", kFood[i].english);
    }
    portPm64MessageText(it.fullDescMsg, 0, desc, descSize);
    return 1;
}

int portPm64FoodRestores(int id, int* hp, int* fp) {
    const int i = foodOf(id);
    *hp = *fp = 0;
    if (i < 0) {
        return -1;
    }
    restores(i, hp, fp);
    return kFood[i].kind;
}

/* --- the items' table entries --- */

/* Super Soda in battle: the recovery items' event (ItemEvent_Recovery: the FP), then the Tasty Tonic's cure on
 * the same ally (ItemEvent_Support_RefreshEffect's hit: the weapon is the Tasty Tonic's, every ailment at 100%) */
static EVT_BEGIN(pm64_super_soda)
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(8))
    CALL(btlevtcmd_WeaponAftereffect, LW(8))
    CALL(btlevtcmd_CheckCommandUnit, BTL_UNIT_SELF, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_GetSelectEnemy, LW(10), LW(11))
    ELSE
        CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(8))
        CALL(btlevtcmd_ChoiceSamplingEnemy, LW(8), LW(10), LW(11))
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    WAIT_MS(1000)
    CALL(btlevtcmd_GetFriendBelong, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_GetBodyId, BTL_UNIT_SELF, LW(9))
        CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, LW(9), 40)
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(3.0f))
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, -60, LW(1), LW(2), 0, -1, 0)
    END_IF
    CALL(btlevtcmd_GetBodyId, BTL_UNIT_SELF, LW(9))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, LW(9), 52)
    CALL(btlevtcmd_CommandFlyItem, BTL_UNIT_SELF, 60)
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, LW(9), BTL_POSE_STAY_DEFAULT)
    CALL(btlevtcmd_ConsumeItem, BTL_UNIT_SELF)
    CALL(btlevtcmd_CommandGetWeaponItemId, LW(9), LW(8))
    IF_INT_EQUAL(LW(9), 0)
        CALL(btlevtcmd_GetConsumeItem, LW(9))
    END_IF
    CALL_CHILD(ItemEvent_Recovery_Core_Effect)
    CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_GetHitPos, LW(10), LW(11), LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_REST_CONDITION1"), LW(0), LW(1), LW(2), 0)
    CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(10), LW(11), LW(12), 256, LW(5))
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    WAIT_FRAMES(60)
    CALL_CHILD(_return_home_event)
    CALL(btlevtcmd_StartWaitEvent, LW(10))
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static BattleWeapon sWeapon[PORT_PM64_FOOD_COUNT];
static BattleWeapon sRandom[2][3]; /* the Kooky Cookie's and the Strange Cake's three: electric, hidden, asleep */
static unsigned sFilledGen = ~0u;

static void fillWeapon(int i) {
    const int id = PORT_PM64_FOOD_MIN + i;
    BattleWeapon* w = &sWeapon[i];
    const BattleWeapon* look = itemDataTable[kFood[i].looks].battle_params;
    switch (kFood[i].kind) {
    case KIND_SODA: /* the Tasty Tonic's cures, after the dishes' recovery (its FP) */
        *w = look != NULL ? *look : ItemWeaponData_CookingItem;
        w->attackEvtScript = pm64_super_soda;
        break;
    case KIND_THROWN:
        *w = look != NULL ? *look : ItemWeaponData_CookingItem;
        break;
    case KIND_RANDOM: {
        const int r = i == DISHES - 1 ? 1 : 0;
        int k;
        sRandom[r][0] = ItemWeaponData_FirstLovePuddingBiribiri;
        sRandom[r][1] = ItemWeaponData_FirstLovePuddingTrans;
        sRandom[r][2] = ItemWeaponData_FirstLovePuddingSleep;
        for (k = 0; k < 3; k++) {
            sRandom[r][k].itemId = id;
        }
        *w = sRandom[r][0];
        break;
    }
    default:
        *w = ItemWeaponData_CookingItem;
        break;
    }
    w->itemId = id;
}

void portPm64FoodFill(void) {
    int i;
    if (sFilledGen == portPm64Generation()) {
        return;
    }
    sFilledGen = portPm64Generation();
    portItemsInit();
    for (i = 0; i < FOODS; i++) {
        ItemData* d = &itemDataTable[PORT_PM64_FOOD_MIN + i];
        const ItemData* look = &itemDataTable[kFood[i].looks];
        Pm64Item it;
        int hp, fp, sell = kFood[i].sell;
        restores(i, &hp, &fp);
        if (portPm64Item(kFood[i].pm64, &it) && it.sellValue > 0) {
            sell = it.sellValue;
        }
        d->usable_locations = kFood[i].field ? itemDataTable[ITEM_MUSHROOM].usable_locations : look->usable_locations;
        d->type_sort_order = (s16)(itemDataTable[ITEM_FRESH_JUICE].type_sort_order + 1 + PORT_DISH_COUNT + i);
        d->hp_restored = (u8)(kFood[i].kind == KIND_THROWN ? 0 : hp);
        d->fp_restored = (u8)fp;
        d->sp_restored = 0;
        d->bp_cost = 0;
        d->sell_price = (u16)sell;
        /* Rowf's price for an ingredient: three times what Paper Mario's shops pay, at least 4 */
        d->buy_price = d->discount_price = (u16)(sell * 3 < 4 ? 4 : sell * 3);
        d->star_piece_price = 0;
        d->icon_id = (u16)(portPm64Linked() != PM64_NONE ? PORT_ICON_PM64_FOOD_BASE + i : look->icon_id);
        fillWeapon(i);
        d->battle_params = &sWeapon[i];
    }
}

/* the Kooky Cookie's and the Strange Cake's effect, picked again each turn as the Love Pudding's (40% electric,
 * 40% hidden, 20% asleep), with the game's random numbers */
void portPm64FoodBattleFrame(void) {
    static int sTurn = -1, sPoisoned;
    BattleWork* wp = _battleWorkPointer;
    int r;
    if (gp == NULL || !gp->inBattle || wp == NULL) {
        sTurn = -1;
        sPoisoned = 0;
        return;
    }
    if (wp->turnCount == sTurn) {
        return;
    }
    if (getenv("TTYD_PM64_POISON") != NULL && wp->turnCount >= 1 && BattleGetMarioPtr(wp) != NULL) {
        /* test switch: Mario poisoned as the battle's first turn starts (the Super Soda's cure shows), his poison
         * logged each turn */
        BattleUnit* mario = BattleGetMarioPtr(wp);
        s8 turns = 0, strength = 0;
        if (!sPoisoned) {
            sPoisoned = 1;
            BtlUnit_SetStatus(mario, STATUS_POISON, 9, 1);
        }
        BtlUnit_GetStatus(mario, STATUS_POISON, &turns, &strength);
        printf("[pm64] turn %d: Mario poisoned %d turns\n", (int)wp->turnCount, (int)turns);
        fflush(stdout);
    }
    sTurn = wp->turnCount;
    /* (only with one in the bag: otherwise the game's random numbers are left as they were) */
    if (pouchCheckItem(PORT_PM64_FOOD_MIN + DISHES - 2) == 0 && pouchCheckItem(PORT_PM64_FOOD_MIN + DISHES - 1) == 0) {
        return;
    }
    portPm64FoodFill();
    for (r = 0; r < 2; r++) {
        const int roll = irand(100);
        const int k = roll < 40 ? 0 : roll < 80 ? 1 : 2;
        const int i = r == 0 ? DISHES - 2 : DISHES - 1;
        sWeapon[i] = sRandom[r][k];
        sWeapon[i].itemId = PORT_PM64_FOOD_MIN + i;
    }
}

/* --- cooking --- */

static int cooked(int a, int b, int out, const char* how) {
    printf("[pm64] cook (%s): 0x%x + 0x%x -> 0x%x %s\n", how, (unsigned)a, (unsigned)b, (unsigned)out,
           portPm64FoodIs(out) ? portPm64FoodEnglish(out) : "(the game's)");
    fflush(stdout);
    portPm64FoodFill();
    return out;
}

static int pairIs(const Pm64Recipe* r, int a, int b) {
    return (r->a == a && r->b == b) || (r->a == b && r->b == a);
}

int portPm64RecipeFirst(int a, int b) {
    size_t i;
    if (!portPm64FoodActive()) {
        return 0;
    }
    for (i = 0; i < sizeof(kFirst) / sizeof(kFirst[0]); i++) {
        if (pairIs(&kFirst[i], a, b)) {
            return cooked(a, b, kFirst[i].out, "first");
        }
    }
    return 0;
}

int portPm64RecipeCook(int a, int b) {
    size_t i;
    if (!portPm64FoodActive()) {
        return 0;
    }
    for (i = 0; i < sizeof(kRecipes) / sizeof(kRecipes[0]); i++) {
        if (kRecipes[i].b != 0 && pairIs(&kRecipes[i], a, b)) {
            return cooked(a, b, kRecipes[i].out, "pair");
        }
    }
    return 0;
}

int portPm64RecipeCook1(int a) {
    size_t i;
    if (!portPm64FoodActive()) {
        return 0;
    }
    for (i = 0; i < sizeof(kRecipes) / sizeof(kRecipes[0]); i++) {
        if (kRecipes[i].b == 0 && kRecipes[i].a == a) {
            return cooked(a, 0, kRecipes[i].out, "alone");
        }
    }
    return 0;
}

/* the first way to make dish d: its ingredients (b 0 alone); 0 none */
int portPm64RecipeHint(int dish, int* a, int* b) {
    size_t i;
    for (i = 0; i < sizeof(kFirst) / sizeof(kFirst[0]); i++) {
        if (kFirst[i].out == dish) {
            *a = kFirst[i].a;
            *b = kFirst[i].b;
            return 1;
        }
    }
    for (i = 0; i < sizeof(kRecipes) / sizeof(kRecipes[0]); i++) {
        if (kRecipes[i].out == dish) {
            *a = kRecipes[i].a;
            *b = kRecipes[i].b;
            return 1;
        }
    }
    return 0;
}

/* --- the dishes this save file has cooked, as the game's own recipe book keeps its own: beside the memory
 * cards as the area maps and the stats are (Recipes/<game id>/file<n>.txt, "name=<player>" first, then one
 * English name a line; another game begun in the slot starts its own). Nothing goes in the card file, so the
 * save stays the GameCube's and Dolphin's. --- */

static unsigned sCooked; /* bit i: kFood[i] */
static char sCookedPath[1024]; /* this file's record, "" with no save file */
static char sCookedName[20];

static void cookedSave(void) {
    FILE* f;
    int i;
    if (sCookedPath[0] == '\0') {
        return;
    }
    portPracticeGameDir("Recipes");
    if ((f = portFileWriteOpen(sCookedPath, "w")) == NULL) {
        return;
    }
    fprintf(f, "name=%s\n", sCookedName);
    for (i = 0; i < FOODS; i++) {
        if (sCooked >> i & 1u) {
            fprintf(f, "%s\n", kFood[i].english);
        }
    }
    portFileWriteClose(f, sCookedPath);
}

void portPm64FoodFileLoaded(void) {
    char line[64];
    FILE* f;
    int sameName = 0;
    sCooked = 0;
    sCookedPath[0] = '\0';
    if (gp == NULL || gp->lastSlot < 0 || gp->lastSlot > 3) {
        return;
    }
    snprintf(sCookedName, sizeof(sCookedName), "%.16s", (const char*)gp->playerName);
    snprintf(sCookedPath, sizeof(sCookedPath), "%s/Recipes/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID,
             (int)gp->lastSlot + 1);
    if (swByteGet(GSW_STORY_PROGRESS) == 0 || (f = portFopen(sCookedPath, "r")) == NULL) {
        return; /* a new game: a book of its own */
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int i;
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "name=", 5) == 0) {
            sameName = strcmp(line + 5, sCookedName) == 0;
            continue;
        }
        for (i = 0; sameName && i < FOODS; i++) {
            if (strcmp(line, kFood[i].english) == 0) {
                sCooked |= 1u << i;
            }
        }
    }
    fclose(f);
}

int portPm64FoodCooked(int id) {
    const int i = foodOf(id);
    return i >= 0 && (sCooked >> i & 1u) != 0;
}

void portPm64FoodMarkCooked(int id) {
    const int i = foodOf(id);
    if (i < 0 || portPm64FoodCooked(id) || portStateSyncCopyHeld()) {
        return; /* (a co-op guest's dish in the host's world: not this file's) */
    }
    sCooked |= 1u << i;
    cookedSave();
}

/* --- Rowf's ingredients --- */

int portPm64FoodStock(int* out, int max) {
    int i, n = 0;
    if (!portPm64FoodActive()) {
        return 0;
    }
    portPm64FoodFill();
    for (i = DISHES; i < FOODS && n < max; i++) {
        out[n++] = PORT_PM64_FOOD_MIN + i;
    }
    return n;
}

/* --- the icons (pm64_badges_pc.c draws them) --- */

int portPm64FoodIcon(int iconId, unsigned* offset, int* looks) {
    const int i = iconId - PORT_ICON_PM64_FOOD_BASE;
    if (i < 0 || i >= FOODS) {
        return 0;
    }
    *offset = kFood[i].icon;
    *looks = kFood[i].looks;
    return 1;
}
