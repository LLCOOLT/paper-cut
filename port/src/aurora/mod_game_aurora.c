/* The game side of the code-mod host (port/include/port/mod_host.h): the
 * host's hooks on the game functions behind the mods' shop and battle
 * services, which read the game's own structures. */
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "evt/evt_shop.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mariost.h"

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* evt_shop_setup(event, isFirstCall): its second argument is the goods
 * list (src/event/evt_shop.c), 6 of them. */
static int shopPre(s32* result, EventEntry** event, BOOL* isFirstCall) {
    (void)result;
    if (*isFirstCall) {
        ShopItem* items = (ShopItem*)evtGetValue(*event, (*event)->args[1]);
        if (items != NULL) {
            portModsShopEvent(gp->currentMapName, items, 6);
        }
    }
    return 0;
}

/* Btl_UnitSetup(wp): the battle's units are made from wp->fbatInfo->setup. */
static int battlePre(BOOL* result, BattleWork** wp) {
    (void)result;
    portModsBattleEvent(*wp);
    return 0;
}

void portModGameInit(void) {
    portModHostAddHook(0, kSlot_evt_shop_setup, (void*)shopPre);
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)battlePre);
}
