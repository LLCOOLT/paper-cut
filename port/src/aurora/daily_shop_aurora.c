/* Daily shop specials (port/include/port/postgame.h): with "daily_specials"
 * on, five shops each put one rare item on a shelf at 30% off, a different
 * one each day, the same for everyone that day (port/include/port/date_seed.h,
 * UTC). The special takes one shelf's place (by the day's draw) for the day;
 * the shelf's own item is back tomorrow, or with the setting off.
 *
 * The shops' goods lists are the rooms' own (each REL's ShopItem table, the
 * second argument of evt_shop_setup): a host hook before the game's
 * evt_shop_setup puts the special in, and puts the shelf back first if this
 * table had it from an earlier visit (the REL stays loaded within the area).
 *
 * Changes what a shop sells: off during online co-op and replays (a replay
 * played back on another day would draw another special). */
#include "port/postgame.h"
#include "port/date_seed.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "data/item_data.h"
#include "driver/seqdrv.h"
#include "evt/evt_shop.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* the shops' rooms (Rogueport, Petalburg, Glitzville, Twilight Town, Fahr Outpost) */
static const struct {
    const char* map;
} kShops[] = {
    {"gor_01"}, {"nok_00"}, {"tou_01"}, {"usu_01"}, {"bom_02"},
};
#define SHOPS ((int)(sizeof(kShops) / sizeof(kShops[0])))
#define SHELVES 6

/* the rare items a special is drawn from: the late game's */
static const s32 kRare[] = {
    ITEM_ULTRA_SHROOM, ITEM_LIFE_SHROOM, ITEM_JAMMIN_JELLY, ITEM_MAPLE_SYRUP, ITEM_SHOOTING_STAR,
    ITEM_THUNDER_RAGE, ITEM_EARTH_QUAKE, ITEM_REPEL_CAPE,   ITEM_BOOS_SHEET,  ITEM_STOPWATCH,
    ITEM_POWER_PUNCH,  ITEM_COURAGE_SHELL, ITEM_MR_SOFTENER, ITEM_POINT_SWAP, ITEM_FRIGHT_MASK,
};
#define RARE ((int)(sizeof(kRare) / sizeof(kRare[0])))

static int* sOn;

/* what each shop's table has had put in (to put it back) */
static struct {
    ShopItem* items;
    int slot;
    ShopItem shelf;   /* the shelf's own */
    ShopItem special; /* what went there */
} sApplied[SHOPS];

static int sBought;

static int on(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("daily_specials", 0, 0, 1);
    }
    return *sOn && !portOnlineActive() && !portReplayActive();
}

static int shopOf(const char* map) {
    int i;
    for (i = 0; map != NULL && i < SHOPS; i++) {
        if (strncmp(map, kShops[i].map, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int usualPrice(s32 item) {
    const ItemData* d = &itemDataTable[item];
    return d->buy_price > 0 ? d->buy_price : d->sell_price > 0 ? d->sell_price * 2 : 10;
}

/* today's special for shop i: the item, the shelf it takes and its price (the
 * same draw on the page as in the shop) */
static void draw(int shop, s32* item, int* slot, int* price) {
    PortSeedRand r;
    portSeedRandInit(&r, portDateSeed() + 7919u * (unsigned)(shop + 1));
    *slot = (int)portSeedRandNext(&r, SHELVES);
    *item = kRare[portSeedRandNext(&r, RARE)];
    *price = usualPrice(*item) * 7 / 10;
    if (*price < 1) {
        *price = 1;
    }
}

/* evt_shop_setup(event, isFirstCall): its goods list (6) before the game reads it */
static int shopPre(s32* result, EventEntry** event, BOOL* isFirstCall) {
    ShopItem* items;
    int shop;
    (void)result;
    if (!*isFirstCall || gp == NULL) {
        return 0;
    }
    shop = shopOf(gp->currentMapName);
    items = (ShopItem*)evtGetValue(*event, (*event)->args[1]);
    if (shop < 0 || items == NULL) {
        return 0;
    }
    /* the table had a special from an earlier visit: its shelf back first */
    if (sApplied[shop].items == items) {
        ShopItem* s = &items[sApplied[shop].slot];
        if (s->id == sApplied[shop].special.id && s->price == sApplied[shop].special.price) {
            *s = sApplied[shop].shelf;
        }
        sApplied[shop].items = NULL;
    }
    if (on()) {
        s32 item;
        int slot, price;
        draw(shop, &item, &slot, &price);
        sApplied[shop].items = items;
        sApplied[shop].slot = slot;
        sApplied[shop].shelf = items[slot];
        sApplied[shop].special.id = item;
        sApplied[shop].special.price = price;
        items[slot].id = item;
        items[slot].price = price;
        printf("[daily] %s: item %d for %d coins on shelf %d\n", kShops[shop].map, (int)item, price, slot);
        fflush(stdout);
    }
    return 0;
}

/* --- for the page ------------------------------------------------------------ */

int portDailyShopCount(void) {
    return SHOPS;
}

const char* portDailyShopName(int i) {
    return i >= 0 && i < SHOPS ? kShops[i].map : "";
}

int portDailyShopSpecial(int i, int* item, int* price, int* usual) {
    s32 it;
    int slot;
    if (i < 0 || i >= SHOPS || sOn == NULL || !*sOn) {
        return 0;
    }
    draw(i, &it, &slot, price);
    *item = (int)it;
    *usual = usualPrice(it);
    return 1;
}

int portDailyShopHere(void) {
    const int shop = gp != NULL && seqGetSeq() == SEQ_GAME ? shopOf(gp->currentMapName) : -1;
    return shop >= 0 && sApplied[shop].items != NULL ? shop : -1;
}

int portDailySpecialsBought(void) {
    return sBought;
}

/* --- the game's frame: a special bought ------------------------------------- */

static void frameHook(void) {
    static int sShop = -1, sCoins = -1, sHave = -1, sPaidFrames;
    PouchData* p = pouchGetPtr();
    const int shop = gp != NULL && seqGetSeq() == SEQ_GAME ? shopOf(gp->currentMapName) : -1;
    if (shop < 0 || p == NULL || sApplied[shop].items == NULL) {
        sShop = -1;
        return;
    }
    {
        const s32 item = sApplied[shop].special.id;
        const int have = pouchCheckItem((u32)item);
        if (shop != sShop) {
            sShop = shop;
            sCoins = p->coins;
            sHave = have;
            sPaidFrames = 0;
            return;
        }
        /* the coins go and the item comes, a few frames apart: one bought */
        if (sCoins - p->coins == sApplied[shop].special.price) {
            sPaidFrames = 180;
        }
        if (sPaidFrames > 0 && have == sHave + 1) {
            sBought++;
            sPaidFrames = 0;
            printf("[daily] special bought (%d this run)\n", sBought);
            fflush(stdout);
        }
        if (sPaidFrames > 0) {
            sPaidFrames--;
        }
        sCoins = p->coins;
        sHave = have;
    }
}

void portDailyShopInit(void) {
    on();
    portModHostAddHook(0, kSlot_evt_shop_setup, (void*)shopPre);
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
