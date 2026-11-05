/* The game side of Lua mods (port/include/port/lua_mods.h, docs/LUA_MODDING.md):
 * the host hooks that become their events, and the libraries that read and
 * change the game: player, items, flags, map and battle. The sandbox and the
 * rest of the API are port/mods/lua_host.cpp's.
 *
 * Every event comes from the game's own frame or its code (never the
 * drawing), so every player's mods see the same in online play. A change to
 * the game is made only while portLuaWritesAllowed() (an online session whose
 * players' Lua differs runs display-only mods, with every change ignored).
 *
 * Hook points for what comes later (Master Quest's rules, imported
 * characters): unit_spawn (a unit just made, its stats still free to
 * change), damage / fp_damage (every hit's number), map_load (each room),
 * battle_start / turn_start / battle_end, and rules.set (the challenge
 * rules). A host feature can send its own events with portLuaEvent. */
#include "port/lua_mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/swdrv.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include "lauxlib.h"
#include "lua.h"

#include <stdio.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static int unitId(const BattleUnit* unit) {
    return unit != NULL ? unit->unitId : -1;
}

/* --- events --- */

static char sMap[sizeof(gp->currentMapName)];
static int sTurn = -1;

/* marioStMain, before the game's frame: the host's frame start, the map and
 * the battle's turn as they changed, then "frame" */
static int framePre(void) {
    portLuaFrame();
    if (!portLuaActive()) {
        return 0;
    }
    if (gp != NULL && strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        if (sMap[0] != '\0') {
            portLuaEvent("map_unload", "s", sMap);
        }
        memcpy(sMap, gp->currentMapName, sizeof(sMap));
        sMap[sizeof(sMap) - 1] = '\0';
        if (sMap[0] != '\0') {
            portLuaEvent("map_load", "s", sMap);
        }
    }
    if (_battleWorkPointer != NULL) {
        const int turn = _battleWorkPointer->turnCount;
        if (turn != sTurn && turn > 0) {
            portLuaEvent("turn_start", "i", turn);
        }
        sTurn = turn;
    } else {
        sTurn = -1;
    }
    portLuaEvent("frame", "");
    return 0;
}

static void framePost(void) {
    if (portLuaActive()) {
        portLuaEvent("frame_end", "");
        portLuaFrameEnd(); /* "hud" */
    }
}

/* Btl_UnitSetup(wp): the battle's units are about to be made */
static int battleStartPre(BOOL* result, BattleWork** wp) {
    (void)result;
    (void)wp;
    sTurn = -1;
    portLuaEvent("battle_start", "");
    return 0;
}

static int battleEndPre(void) {
    portLuaEvent("battle_end", "");
    return 0;
}

/* BtlUnit_Entry(setup): a unit made (enemies, Mario, the partner, objects) */
static void unitEntryPost(BattleUnit** result, BattleUnitSetup* setup) {
    (void)setup;
    if (*result != NULL && portLuaListening("unit_spawn")) {
        portLuaEvent("unit_spawn", "uii", unitId(*result), (int)(*result)->currentKind, (int)(*result)->alliance);
    }
}

/* each hit's damage: handlers return the number instead */
static void damagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                       BattleWeapon* weapon, u32* resultFlags, u32 flags) {
    (void)part;
    (void)weapon;
    (void)resultFlags;
    (void)flags;
    if (portLuaListening("damage")) {
        int d = portLuaFilterInt("damage", *result, "uu", unitId(attacker), unitId(target));
        *result = d < 0 ? 0 : d;
    }
}

static void fpDamagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                         BattleWeapon* weapon, u32* resultFlags, u32 flags) {
    (void)part;
    (void)weapon;
    (void)resultFlags;
    (void)flags;
    if (portLuaListening("fp_damage")) {
        int d = portLuaFilterInt("fp_damage", *result, "uu", unitId(attacker), unitId(target));
        *result = d < 0 ? 0 : d;
    }
}

/* items in and out of the pouch (bought, found, used, sold, thrown away) */
static void itemGetPost(BOOL* result, ItemType type) {
    if (*result) {
        portLuaEvent("item_get", "i", (int)type);
    }
}

static void itemRemovePost(BOOL* result, ItemType type) {
    if (*result) {
        portLuaEvent("item_lose", "i", (int)type);
    }
}

static void itemRemoveIndexPost(BOOL* result, ItemType type, s32 index) {
    (void)index;
    if (*result) {
        portLuaEvent("item_lose", "i", (int)type);
    }
}

static int sBadge;
static int badgeIndexPre(BOOL* result, s32* index) {
    (void)result;
    sBadge = *index >= 0 && *index < MAX_BADGES ? pouchGetPtr()->badges[*index] : 0;
    return 0;
}

static void badgeEquipPost(BOOL* result, s32 index) {
    (void)index;
    if (*result && sBadge != 0) {
        portLuaEvent("badge_equip", "ib", sBadge, 1);
    }
}

static void badgeUnequipPost(BOOL* result, s32 index) {
    (void)index;
    if (*result && sBadge != 0) {
        portLuaEvent("badge_equip", "ib", sBadge, 0);
    }
}

void portLuaGuard(void* unit, int super) {
    if (portLuaActive()) {
        portLuaEvent("guard", "ub", unitId((BattleUnit*)unit), super);
    }
}

/* battle_ac.c's results: 4 a Guard, 5 a Superguard, 2 none; Lua's 1, 2, 0 */
int portLuaGuardResult(int result) {
    int kind, back;
    if (!portLuaActive() || (result != 2 && result != 4 && result != 5) || !portLuaListening("guard_result")) {
        return result;
    }
    kind = result == 4 ? 1 : result == 5 ? 2 : 0;
    back = portLuaFilterInt("guard_result", kind, "");
    return back == 1 ? 4 : back == 2 ? 5 : back == 0 ? 2 : result;
}

int portLuaCommandLevel(int level) {
    int back;
    if (!portLuaActive() || !portLuaListening("command_level")) {
        return level;
    }
    back = portLuaFilterInt("command_level", level, "");
    return back < 0 ? 0 : back > 6 ? 6 : back;
}

void portLuaGameInit(void) {
    portModHostAddHook(0, kSlot_marioStMain, (void*)framePre);
    portModHostAddHook(1, kSlot_marioStMain, (void*)framePost);
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)battleStartPre);
    portModHostAddHook(0, kSlot_BattleEnd, (void*)battleEndPre);
    portModHostAddHook(1, kSlot_BtlUnit_Entry, (void*)unitEntryPost);
    portModHostAddHook(1, kSlot_BattleCalculateDamage, (void*)damagePost);
    portModHostAddHook(1, kSlot_BattleCalculateFpDamage, (void*)fpDamagePost);
    portModHostAddHook(1, kSlot_pouchGetItem, (void*)itemGetPost);
    portModHostAddHook(1, kSlot_pouchRemoveItem, (void*)itemRemovePost);
    portModHostAddHook(1, kSlot_pouchRemoveItemIndex, (void*)itemRemoveIndexPost);
    portModHostAddHook(0, kSlot_pouchEquipBadgeIndex, (void*)badgeIndexPre);
    portModHostAddHook(1, kSlot_pouchEquipBadgeIndex, (void*)badgeEquipPost);
    portModHostAddHook(0, kSlot_pouchUnEquipBadgeIndex, (void*)badgeIndexPre);
    portModHostAddHook(1, kSlot_pouchUnEquipBadgeIndex, (void*)badgeUnequipPost);
}

/* --- the libraries --- */

static void setInt(lua_State* L, const char* key, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, key);
}

static int writes(void) {
    return portLuaWritesAllowed();
}

static int clampInt(lua_Integer v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : (int)v;
}

/* player: Mario's stats and the partner */
static int lPlayerStats(lua_State* L) {
    const PouchData* p = pouchGetPtr();
    lua_newtable(L);
    setInt(L, "hp", p->currentHP);
    setInt(L, "max_hp", p->maximumHP);
    setInt(L, "fp", p->currentFP);
    setInt(L, "max_fp", p->maximumFP);
    setInt(L, "coins", p->coins);
    setInt(L, "sp", p->currentSP);
    setInt(L, "max_sp", p->maximumSP);
    setInt(L, "level", p->level);
    setInt(L, "rank", p->rank);
    setInt(L, "star_points", p->starPoints);
    setInt(L, "bp", p->availableBP);
    setInt(L, "max_bp", p->maximumBP);
    setInt(L, "star_pieces", p->starPieceCount);
    setInt(L, "shine_sprites", p->shineSpriteCount);
    setInt(L, "jump_level", p->jumpLevel);
    setInt(L, "hammer_level", p->hammerLevel);
    return 1;
}

static int lPlayerSetHp(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), 0, 999);
    if (writes()) {
        pouchSetHP((s16)v);
    }
    return 0;
}

static int lPlayerSetFp(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), 0, 999);
    if (writes()) {
        pouchSetFP((s16)v);
    }
    return 0;
}

static int lPlayerSetMaxHp(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), 1, 999);
    if (writes()) {
        pouchSetMaxHP((s16)v);
    }
    return 0;
}

static int lPlayerSetMaxFp(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), 0, 999);
    if (writes()) {
        pouchSetMaxFP((s16)v);
    }
    return 0;
}

static int lPlayerSetCoins(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), 0, 999);
    if (writes()) {
        pouchSetCoin(v);
    }
    return 0;
}

static int lPlayerAddCoins(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), -999, 999);
    if (writes()) {
        pouchAddCoin(v);
    }
    return 0;
}

static int lPlayerPartner(lua_State* L) {
    lua_pushinteger(L, marioGetPartyId());
    return 1;
}

static int lPlayerPartnerStats(lua_State* L) {
    const int id = clampInt(luaL_optinteger(L, 1, marioGetPartyId()), 0, 7);
    const PouchPartyData* d = &pouchGetPtr()->partyData[id];
    lua_newtable(L);
    setInt(L, "id", id);
    setInt(L, "joined", (d->flags & POUCH_PARTY_FLAG_JOINED) != 0);
    setInt(L, "hp", d->currentHP);
    setInt(L, "max_hp", d->maximumHP);
    setInt(L, "level", d->attackLevel);
    return 1;
}

static int lPlayerSetPartnerHp(lua_State* L) {
    const int id = clampInt(luaL_checkinteger(L, 1), 1, 7);
    const int v = clampInt(luaL_checkinteger(L, 2), 0, 999);
    if (writes()) {
        pouchSetPartyHP(id, v);
    }
    return 0;
}

/* items: by id (ttyd.const("ITEM_MUSHROOM")) */
static int lItemsCount(lua_State* L) {
    lua_pushinteger(L, pouchCheckItem((u32)luaL_checkinteger(L, 1)));
    return 1;
}

static int lItemsGive(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (!writes() || id <= 0 || id >= ITEM_TYPE_MAX) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, pouchGetItem((ItemType)id) != 0);
    return 1;
}

static int lItemsTake(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (!writes() || id <= 0 || id >= ITEM_TYPE_MAX) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, pouchRemoveItem((ItemType)id) != 0);
    return 1;
}

static int list(lua_State* L, const s16* ids, int count) {
    int i, n = 0;
    lua_newtable(L);
    for (i = 0; i < count; i++) {
        if (ids[i] != 0) {
            lua_pushinteger(L, ids[i]);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

static int lItemsHeld(lua_State* L) {
    return list(L, pouchGetPtr()->heldItems, MAX_HELD_ITEMS);
}
static int lItemsStored(lua_State* L) {
    return list(L, pouchGetPtr()->storedItems, MAX_STORED_ITEMS);
}
static int lItemsKey(lua_State* L) {
    return list(L, pouchGetPtr()->keyItems, MAX_KEY_ITEMS);
}
static int lItemsBadges(lua_State* L) {
    return list(L, pouchGetPtr()->badges, MAX_BADGES);
}
static int lItemsEquipped(lua_State* L) {
    return list(L, pouchGetPtr()->equippedBadges, MAX_BADGES);
}

/* flags: the story's saved words (GSW, 0-2047) and flags (GSWF, 0-8191) */
static int lFlagsGsw(lua_State* L) {
    lua_pushinteger(L, swByteGet(clampInt(luaL_checkinteger(L, 1), 0, 0x7FF)));
    return 1;
}

static int lFlagsSetGsw(lua_State* L) {
    const int i = clampInt(luaL_checkinteger(L, 1), 0, 0x7FF);
    const lua_Integer v = luaL_checkinteger(L, 2);
    if (writes()) {
        swByteSet(i, (s32)v);
    }
    return 0;
}

static int lFlagsGswf(lua_State* L) {
    lua_pushboolean(L, swGet(clampInt(luaL_checkinteger(L, 1), 0, 0x1FFF)) != 0);
    return 1;
}

static int lFlagsSetGswf(lua_State* L) {
    const int i = clampInt(luaL_checkinteger(L, 1), 0, 0x1FFF);
    const int on = lua_isnoneornil(L, 2) || lua_toboolean(L, 2);
    if (writes()) {
        if (on) {
            swSet(i);
        } else {
            swClear(i);
        }
    }
    return 0;
}

/* map */
static int lMapName(lua_State* L) {
    lua_pushstring(L, gp != NULL ? gp->currentMapName : "");
    return 1;
}

static int lMapArea(lua_State* L) {
    lua_pushstring(L, gp != NULL ? gp->currentAreaName : "");
    return 1;
}

/* battle: units by id */
static BattleUnit* unitArg(lua_State* L, int index) {
    const lua_Integer id = luaL_checkinteger(L, index);
    if (_battleWorkPointer == NULL || id < 0 || id >= BATTLE_UNIT_MAX) {
        return NULL;
    }
    return _battleWorkPointer->units[id];
}

static int lBattleActive(lua_State* L) {
    lua_pushboolean(L, _battleWorkPointer != NULL);
    return 1;
}

static int lBattleTurn(lua_State* L) {
    lua_pushinteger(L, _battleWorkPointer != NULL ? _battleWorkPointer->turnCount : 0);
    return 1;
}

static int lBattleUnits(lua_State* L) {
    int i, n = 0;
    lua_newtable(L);
    for (i = 0; _battleWorkPointer != NULL && i < BATTLE_UNIT_MAX; i++) {
        if (_battleWorkPointer->units[i] != NULL) {
            lua_pushinteger(L, _battleWorkPointer->units[i]->unitId);
            lua_rawseti(L, -2, ++n);
        }
    }
    return 1;
}

static int lBattleUnit(lua_State* L) {
    const BattleUnit* u = unitArg(L, 1);
    if (u == NULL) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    setInt(L, "id", u->unitId);
    setInt(L, "kind", u->currentKind);
    setInt(L, "alliance", u->alliance);
    setInt(L, "level", u->level);
    setInt(L, "hp", u->currentHp);
    setInt(L, "max_hp", u->maxHp);
    setInt(L, "fp", u->currentFp);
    setInt(L, "max_fp", u->maxFp);
    lua_pushnumber(L, u->position.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, u->position.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, u->position.z);
    lua_setfield(L, -2, "z");
    return 1;
}

static int lBattleMario(lua_State* L) {
    lua_pushinteger(L, _battleWorkPointer != NULL ? unitId(BattleGetMarioPtr(_battleWorkPointer)) : -1);
    return 1;
}

static int lBattlePartner(lua_State* L) {
    lua_pushinteger(L, _battleWorkPointer != NULL ? unitId(BattleGetPartyPtr(_battleWorkPointer)) : -1);
    return 1;
}

static int lBattleSetHp(lua_State* L) {
    BattleUnit* u = unitArg(L, 1);
    const int v = clampInt(luaL_checkinteger(L, 2), 0, 9999);
    if (u != NULL && writes()) {
        u->currentHp = (s16)(v > u->maxHp ? u->maxHp : v);
    }
    return 0;
}

static int lBattleSetMaxHp(lua_State* L) {
    BattleUnit* u = unitArg(L, 1);
    const int v = clampInt(luaL_checkinteger(L, 2), 1, 9999);
    if (u != NULL && writes()) {
        u->maxHp = u->baseMaxHp = (s16)v;
        if (u->currentHp > u->maxHp) {
            u->currentHp = u->maxHp;
        }
    }
    return 0;
}

static int lBattleSetFp(lua_State* L) {
    BattleUnit* u = unitArg(L, 1);
    const int v = clampInt(luaL_checkinteger(L, 2), 0, 999);
    if (u != NULL && writes()) {
        u->currentFp = (s16)(v > u->maxFp ? u->maxFp : v);
    }
    return 0;
}

static void lib(lua_State* L, const char* name, const luaL_Reg* funcs) {
    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_setglobal(L, name);
}

void portLuaGameOpen(lua_State* L) {
    static const luaL_Reg player[] = {
        {"stats", lPlayerStats},
        {"set_hp", lPlayerSetHp},
        {"set_fp", lPlayerSetFp},
        {"set_max_hp", lPlayerSetMaxHp},
        {"set_max_fp", lPlayerSetMaxFp},
        {"set_coins", lPlayerSetCoins},
        {"add_coins", lPlayerAddCoins},
        {"partner", lPlayerPartner},
        {"partner_stats", lPlayerPartnerStats},
        {"set_partner_hp", lPlayerSetPartnerHp},
        {NULL, NULL},
    };
    static const luaL_Reg items[] = {
        {"count", lItemsCount},   {"give", lItemsGive},       {"take", lItemsTake},
        {"held", lItemsHeld},     {"stored", lItemsStored},   {"key_items", lItemsKey},
        {"badges", lItemsBadges}, {"equipped", lItemsEquipped}, {NULL, NULL},
    };
    static const luaL_Reg flags[] = {
        {"gsw", lFlagsGsw}, {"set_gsw", lFlagsSetGsw}, {"gswf", lFlagsGswf}, {"set_gswf", lFlagsSetGswf}, {NULL, NULL},
    };
    static const luaL_Reg map[] = {{"name", lMapName}, {"area", lMapArea}, {NULL, NULL}};
    static const luaL_Reg battle[] = {
        {"active", lBattleActive},   {"turn", lBattleTurn},         {"units", lBattleUnits},
        {"unit", lBattleUnit},       {"mario", lBattleMario},       {"partner", lBattlePartner},
        {"set_hp", lBattleSetHp},    {"set_max_hp", lBattleSetMaxHp}, {"set_fp", lBattleSetFp},
        {NULL, NULL},
    };
    lib(L, "player", player);
    lib(L, "items", items);
    lib(L, "flags", flags);
    lib(L, "map", map);
    lib(L, "battle", battle);
    lua_getglobal(L, "battle");
    setInt(L, "GUARD_NONE", 0);
    setInt(L, "GUARD", 1);
    setInt(L, "SUPERGUARD", 2);
    setInt(L, "MARIO_SIDE", 0);
    setInt(L, "ENEMY_SIDE", 1);
    lua_pop(L, 1);
}
