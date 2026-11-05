/* The game side of the code-mod host (port/include/port/mod_host.h): the
 * host's hooks on the game functions behind the mods' shop and battle
 * services, which read the game's own structures, and the maps mods' scripts
 * give (port/include/port/mods.h). */
#include "port/lua_mods.h"
#include "port/mod_host.h"
#include "port/mods.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "data/mapdata.h"
#include "evt/evt_cmd.h"
#include "evt/evt_shop.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mariost.h"
#include "port/luigi.h"

#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <string.h>

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

/* evt_mod_log(text): a line in the log, "[evt] text", for mods' scripts (the
 * game's DEBUG_PUT_MSG does nothing in a retail build). */
USER_FUNC(evt_mod_log) {
    (void)isFirstCall;
    SDL_Log("[evt] %s", (const char*)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* mapDataPtr(name): a map a mod gives an init script (a new map, or one of
 * the game's), as a MapData of the host's: the game reads its eventAddr when
 * the map is entered (src/sequence/seq_mapchange.c). */
#define MOD_MAPS 64
static MapData sModMaps[MOD_MAPS];
static char sModMapNames[MOD_MAPS][16];

static void mapDataPost(MapData** result, const char* name) {
    EvtWord* script = (EvtWord*)portModsMapScript(name);
    int i;
    if (script == NULL) {
        return;
    }
    for (i = 0; i < MOD_MAPS && sModMapNames[i][0] != '\0'; i++) {
        if (strcmp(sModMapNames[i], name) == 0) {
            break;
        }
    }
    if (i == MOD_MAPS) {
        return;
    }
    if (sModMapNames[i][0] == '\0') {
        snprintf(sModMapNames[i], sizeof(sModMapNames[i]), "%s", name);
        sModMaps[i].mapName = sModMapNames[i];
    }
    sModMaps[i].eventAddr = script;
    *result = &sModMaps[i];
}

/* seqMain, each frame: the map-scoped texture sets for the map now (mods.cpp) */
static void seqMainPost(void) {
    if (gp != NULL && gp->currentMapName[0] != 0) {
        /* with Luigi as the player (Play as Luigi): every map flagged "#luigi",
         * for the sets of Luigi's faces in the game's own textures
         * (luigi_faces_aurora.c, mods.cpp's mapMatches) */
        char map[48];
        const int luigi = portLuigiOn();
        snprintf(map, sizeof(map), "%s%s", gp->currentMapName, luigi ? "#luigi" : "");
        portModsMapTextures(map);
    }
}

void portModGameInit(void) {
    portModHostAddHook(1, kSlot_seqMain, (void*)seqMainPost);
    portModHostAddHook(0, kSlot_evt_shop_setup, (void*)shopPre);
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)battlePre);
    portModHostAddHook(1, kSlot_mapDataPtr, (void*)mapDataPost);
    portLuaGameInit(); /* Lua mods' events (lua_game_aurora.c) */
}
