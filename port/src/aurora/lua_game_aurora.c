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
 * Hook points for Paper Cut's own features: formation / enemy_changes
 * (Master Quest's hooks, a field fight's enemies and an enemy's changes),
 * the looks (partner skins and costumes, in lua_host.cpp), unit_spawn (a
 * unit just made, its stats still free to change), damage / fp_damage
 * (every hit's number), map_load (each room), battle_start / turn_start /
 * battle_end, and rules.set (the challenge rules). A host feature can send
 * its own events with portLuaEvent, or a table to change with
 * portLuaFilterTable. */
#include <limits.h>
#include "port/lua_mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/companion.h"
#include "port/map_edits.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/msgdrv.h"
#include "driver/swdrv.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include "lauxlib.h"
#include "lua.h"

#include <math.h>
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
static void mqHooksUpdate(void);

static int framePre(void) {
    portLuaFrame();
    mqHooksUpdate();
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

/* --- Master Quest's hooks (port/include/port/master_quest.h, docs/MASTER_QUEST.md) ---
 * "formation" (area, map, units): a field fight's enemies before it is set
 * up, a list of unit kinds; "enemy_changes" (area, kind, changes): an
 * enemy's changes as it enters a fight. Handlers change the table they get
 * (or return another). The hooks are set only while a mod listens and may
 * change the game, which is the same in every player's game. Built where
 * Master Quest is (its header). */
#if defined(__has_include)
#if __has_include("port/master_quest.h")
#include "port/master_quest.h"
#define LUA_MASTER_QUEST 1
#endif
#endif

#if defined(LUA_MASTER_QUEST)
static void setInt(lua_State* L, const char* key, lua_Integer v);
static int clampInt(lua_Integer v, int lo, int hi);

typedef struct LuaFormation {
    short* units;
    int count;
} LuaFormation;

static void formationPush(lua_State* L, void* user) {
    const LuaFormation* f = (const LuaFormation*)user;
    int i;
    lua_createtable(L, f->count, 0);
    for (i = 0; i < f->count; i++) {
        lua_pushinteger(L, f->units[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

/* the kinds a table lists (1-5; Master Quest leaves out what the area can't have) */
static void formationRead(lua_State* L, int idx, void* user) {
    LuaFormation* f = (LuaFormation*)user;
    short units[PORT_MQ_UNITS];
    const lua_Integer len = (lua_Integer)lua_rawlen(L, idx);
    lua_Integer i;
    int n = 0;
    for (i = 1; i <= len && n < PORT_MQ_UNITS; i++) {
        int isnum;
        lua_Integer k;
        lua_rawgeti(L, idx, i);
        k = lua_tointegerx(L, -1, &isnum);
        lua_pop(L, 1);
        if (isnum && k > UNIT_NULL && k < UNIT_MAX) {
            units[n++] = (short)k;
        }
    }
    if (n > 0) {
        memcpy(f->units, units, sizeof(short) * (size_t)n);
        f->count = n;
    }
}

static int mqFormation(void* user, const char* area, const char* map, short* units, int count) {
    LuaFormation f;
    (void)user;
    f.units = units;
    f.count = count;
    portLuaFilterTable("formation", formationPush, formationRead, &f, "ss", area, map);
    return f.count;
}

/* the statuses a set can use (master_quest.h): an attack's, a fight's start */
static const signed char kHitStatus[] = {STATUS_SLEEP, STATUS_STOP,        STATUS_DIZZY,        STATUS_POISON,
                                         STATUS_CONFUSE, STATUS_BURN,      STATUS_FREEZE,       STATUS_TINY,
                                         STATUS_ATTACK_DOWN, STATUS_DEFENSE_DOWN, STATUS_SLOW};
static const signed char kStartStatus[] = {STATUS_DODGY, STATUS_HUGE,      STATUS_ATTACK_UP, STATUS_DEFENSE_UP,
                                           STATUS_CHARGE, STATUS_INVISIBLE, STATUS_FAST,      STATUS_PAYBACK,
                                           STATUS_HOLD_FAST, STATUS_HP_REGEN, STATUS_ELECTRIC};

static int statusAllowed(int type, int hit) {
    const signed char* list = hit ? kHitStatus : kStartStatus;
    const int n = hit ? (int)sizeof(kHitStatus) : (int)sizeof(kStartStatus);
    int i;
    for (i = 0; i < n; i++) {
        if (list[i] == type) {
            return 1;
        }
    }
    return 0;
}

static void statusesPush(lua_State* L, const PortMqStatus* s, int hit) {
    int i, k = 0;
    lua_createtable(L, 2, 0);
    for (i = 0; i < 2; i++) {
        if (s[i].type < 0) {
            continue;
        }
        lua_createtable(L, 0, 4);
        setInt(L, "status", s[i].type);
        if (hit) {
            setInt(L, "chance", s[i].chance);
        }
        setInt(L, "turns", s[i].turns);
        setInt(L, "strength", s[i].strength);
        lua_rawseti(L, -2, ++k);
    }
}

/* t[key] as a whole number (no metamethods), or fallback */
static int rawInt(lua_State* L, int t, const char* key, int fallback, int lo, int hi) {
    int isnum;
    lua_Integer v;
    lua_pushstring(L, key);
    lua_rawget(L, t);
    v = lua_tointegerx(L, -1, &isnum);
    lua_pop(L, 1);
    return isnum ? clampInt(v, lo, hi) : fallback;
}

static void statusesRead(lua_State* L, int t, const char* key, PortMqStatus* s, int hit) {
    int i, n = 0;
    lua_pushstring(L, key);
    lua_rawget(L, t);
    if (lua_istable(L, -1)) {
        const int list = lua_gettop(L);
        PortMqStatus out[2];
        memset(out, 0, sizeof(out));
        out[0].type = out[1].type = -1;
        for (i = 1; i <= 2; i++) {
            lua_rawgeti(L, list, i);
            if (lua_istable(L, -1)) {
                const int e = lua_gettop(L);
                const int type = rawInt(L, e, "status", -1, -1, STATUS_INVALID);
                if (statusAllowed(type, hit)) {
                    out[n].type = (signed char)type;
                    out[n].chance = (signed char)(hit ? rawInt(L, e, "chance", 100, 0, 100) : 0);
                    out[n].turns = (signed char)rawInt(L, e, "turns", 1, 0, 99);
                    out[n].strength = (signed char)rawInt(L, e, "strength", 1, -99, 99);
                    n++;
                }
            }
            lua_pop(L, 1);
        }
        memcpy(s, out, sizeof(out));
    }
    lua_pop(L, 1);
}

static void enemyPush(lua_State* L, void* user) {
    const PortMqEnemy* e = (const PortMqEnemy*)user;
    lua_createtable(L, 0, 7);
    setInt(L, "hp_add", e->hpAdd);
    setInt(L, "hp_percent", e->hpPercent);
    setInt(L, "atk_add", e->atkAdd);
    setInt(L, "element", e->element);
    lua_pushboolean(L, e->pierce != 0);
    lua_setfield(L, -2, "pierce");
    statusesPush(L, e->hit, 1);
    lua_setfield(L, -2, "hit");
    statusesPush(L, e->start, 0);
    lua_setfield(L, -2, "start");
}

static void enemyRead(lua_State* L, int t, void* user) {
    PortMqEnemy* e = (PortMqEnemy*)user;
    e->hpAdd = (short)rawInt(L, t, "hp_add", e->hpAdd, -999, 999);
    e->hpPercent = (short)rawInt(L, t, "hp_percent", e->hpPercent, 1, 1000);
    e->atkAdd = (signed char)rawInt(L, t, "atk_add", e->atkAdd, -99, 99);
    e->element = (signed char)rawInt(L, t, "element", e->element, -1, ELEMENT_ELECTRIC);
    lua_pushliteral(L, "pierce");
    lua_rawget(L, t);
    if (lua_isboolean(L, -1)) {
        e->pierce = (unsigned char)lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    statusesRead(L, t, "hit", e->hit, 1);
    statusesRead(L, t, "start", e->start, 0);
}

static void mqEnemy(void* user, const char* area, int unitType, PortMqEnemy* changes) {
    (void)user;
    portLuaFilterTable("enemy_changes", enemyPush, enemyRead, changes, "si", area, unitType);
}

static const PortMasterQuestHooks kMqHooks = {NULL, mqFormation, mqEnemy};
static int sMqHooked;

/* each frame: set while a mod listens and may change the game */
static void mqHooksUpdate(void) {
    const int want = portLuaActive() && portLuaWritesAllowed() &&
                     (portLuaListening("formation") || portLuaListening("enemy_changes"));
    if (want != sMqHooked) {
        portMasterQuestSetHooks(want ? &kMqHooks : NULL);
        sMqHooked = want;
    }
}
#else
static void mqHooksUpdate(void) {
}
#endif

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
    const int v = clampInt(luaL_checkinteger(L, 1), 0, 9999); /* (the pouch caps it: 999, or 9,999 with Coins up to 9,999) */
    if (writes()) {
        pouchSetCoin(v);
    }
    return 0;
}

static int lPlayerAddCoins(lua_State* L) {
    const int v = clampInt(luaL_checkinteger(L, 1), -9999, 9999);
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

/* map.place{...}: a map edit's thing (port/include/port/map_edits.h, its
 * maps/<room>.json fields: type, id, pos or x/y/z, ry, to, entrance, target, model,
 * text, say, price, power, item, flag_gswf, flag_lswf, shop, shows, radius,
 * hidden) placed in the room now; true if it was */
static void tableString(lua_State* L, const char* key, char* out, size_t size) {
    if (lua_getfield(L, 1, key) == LUA_TSTRING) {
        snprintf(out, size, "%s", lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}

/* a number at the stack top: refused (a Lua error) if it is a NaN or infinite,
 * so no cast below sees one */
static lua_Number finiteTop(lua_State* L, const char* key) {
    const lua_Number n = lua_tonumber(L, -1);
    if (!isfinite(n)) {
        luaL_error(L, "map.place: \"%s\" is not a finite number", key);
    }
    return n;
}

static lua_Number tableNumber(lua_State* L, const char* key, lua_Number fallback) {
    lua_Number n = fallback;
    if (lua_getfield(L, 1, key) == LUA_TNUMBER) {
        n = finiteTop(L, key);
    }
    lua_pop(L, 1);
    return n;
}

/* t[key] as a whole number in lo..hi, or fallback (a NaN, an infinity or 1e300 isn't one: no undefined
   double-to-int cast) */
static int tableInt(lua_State* L, const char* key, int fallback, int lo, int hi) {
    int isnum = 0;
    lua_Integer v = 0;
    if (lua_getfield(L, 1, key) == LUA_TNUMBER) {
        v = lua_tointegerx(L, -1, &isnum);
        if (!isnum) {
            const lua_Number d = lua_tonumber(L, -1);
            if (d == d && d > (lua_Number)INT_MIN && d < (lua_Number)INT_MAX) {
                v = (lua_Integer)d; /* (a fraction: cut, as the cast did) */
                isnum = 1;
            }
        }
    }
    lua_pop(L, 1);
    return isnum ? clampInt(v, lo, hi) : fallback;
}

/* a flag field: 0 when absent or negative (no flag); else 1 with *id the
 * number as given, or INT_MAX for one no int holds (NaN, 1e30), both of
 * which portMapThingCheck refuses */
static int tableFlag(lua_State* L, const char* key, int* id) {
    int set = 0;
    if (lua_getfield(L, 1, key) == LUA_TNUMBER) {
        int isnum = 0;
        const lua_Integer v = lua_tointegerx(L, -1, &isnum);
        const lua_Number d = lua_tonumber(L, -1);
        if (isnum ? v >= 0 : !(d < 0)) {
            set = 1;
            *id = isnum && v <= INT_MAX ? (int)v : isnum ? INT_MAX : (d == d && d < (lua_Number)INT_MAX) ? (int)d : INT_MAX;
        }
    }
    lua_pop(L, 1);
    return set;
}

/* a finite number as an int without overflow (out of int's range: a value
 * portMapThingCheck refuses) */
static int toInt(lua_Number n) {
    return n > 2147483647.0 ? 2147483647 : n < -2147483647.0 ? -2147483647 : (int)n;
}

static int lMapPlace(lua_State* L) {
    PortMapThing t;
    int type, i;
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "type");
    type = portMapThingType(lua_tostring(L, -1));
    lua_pop(L, 1);
    if (type < 0) {
        return luaL_error(L, "map.place: no such type");
    }
    portMapThingInit(&t, type);
    tableString(L, "id", t.id, sizeof(t.id));
    tableString(L, "to", t.to, sizeof(t.to));
    tableString(L, "entrance", t.entrance, sizeof(t.entrance));
    tableString(L, "target", t.target, sizeof(t.target));
    tableString(L, "model", t.model, sizeof(t.model));
    tableString(L, "text", t.text, sizeof(t.text));
    tableString(L, "say", t.say, sizeof(t.say));
    if (lua_getfield(L, 1, "pos") == LUA_TTABLE) {
        for (i = 0; i < 3; i++) {
            lua_geti(L, -1, i + 1);
            t.pos[i] = (float)finiteTop(L, "pos");
            lua_pop(L, 1);
        }
    } else {
        t.pos[0] = (float)tableNumber(L, "x", 0);
        t.pos[1] = (float)tableNumber(L, "y", 0);
        t.pos[2] = (float)tableNumber(L, "z", 0);
    }
    lua_pop(L, 1);
    t.ry = (float)tableNumber(L, "ry", 0);
    t.arg = tableInt(L, type == PORT_MT_HEART_BLOCK ? "price" : type == PORT_MT_JUMP_STAND ? "power" : "item",
                     type == PORT_MT_HEART_BLOCK ? 10 : 0, INT_MIN, INT_MAX);
    /* a flag is kept as given, never clamped or dropped: one past the words'
     * end (or a number no int holds) is refused by portMapThingCheck below,
     * so the mod hears of it instead of a thing placed without its flag
     * (a Strange Sack on Mario's spawn once held him in its item window).
     * A negative flag is no flag (the -1 a mod passes for none). */
    if (tableFlag(L, "flag_gswf", &t.flagId)) {
        t.flagKind = PORT_MT_FLAG_GSWF;
    } else if (tableFlag(L, "flag_lswf", &t.flagId)) {
        t.flagKind = PORT_MT_FLAG_LSWF;
    }
    t.radius = (float)tableNumber(L, "radius", 0);
    if (lua_getfield(L, 1, "hidden") == LUA_TBOOLEAN) {
        t.hidden = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 1, "shop") == LUA_TTABLE) {
        for (i = 0; i < PORT_MT_SHOP_MAX; i++) {
            if (lua_geti(L, -1, i + 1) == LUA_TNUMBER) {
                t.shop[t.shopCount++] = toInt(finiteTop(L, "shop"));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 1, "shows") == LUA_TTABLE) {
        for (i = 0; i < PORT_MT_SHOWS_MAX; i++) {
            if (lua_geti(L, -1, i + 1) == LUA_TSTRING) {
                snprintf(t.shows[i], sizeof(t.shows[i]), "%s", lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    {
        /* the same ranges a maps/ file is read with: no flag, item or
         * position past what the game can take */
        char why[128];
        if (!portMapThingCheck(&t, why, (int)sizeof(why))) {
            return luaL_error(L, "map.place: %s", why);
        }
    }
    lua_pushboolean(L, portLuaWritesAllowed() && portMapEditsPlaceNow(&t));
    return 1;
}

/* map.remove(id): a placed thing (a map edit's or map.place's) taken away; true if there was one */
static int lMapRemove(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    lua_pushboolean(L, portLuaWritesAllowed() && portMapEditsRemoveNow(id));
    return 1;
}

/* map.edits(): how many map edits the room has (every mod's and the built-in) */
static int lMapEdits(lua_State* L) {
    lua_pushinteger(L, gp != NULL ? portMapEditsCount(NULL, gp->currentMapName) : 0);
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

/* a kind's names: "UNIT_GOOMBA" (the decomp's, the same everywhere), the
 * game's message tag for it ("btl_un_kuribo") and its text in the player's
 * language, as UTF-8 ("Goomba"; for showing only: it differs between
 * players with other languages) */
static void kindNames(lua_State* L, int kind, const BattleUnitKind* params) {
    const char* name = portLuaUnitKindName(kind);
    if (name != NULL) {
        lua_pushstring(L, name);
        lua_setfield(L, -2, "name");
    }
    if (params != NULL && params->name != NULL) {
        char text[128];
        const char* game = msgSearch(params->name);
        lua_pushstring(L, params->name);
        lua_setfield(L, -2, "tag");
        portCompanionUtf8(game, text, (int)sizeof(text), 0);
        lua_pushstring(L, text);
        lua_setfield(L, -2, "display_name");
    }
}

static void unitNames(lua_State* L, const BattleUnit* u) {
    kindNames(L, u->currentKind, u->kindParams);
}

/* battle.kind_name(kind): "UNIT_GOOMBA", or nil */
static int lBattleKindName(lua_State* L) {
    const char* name = portLuaUnitKindName((int)luaL_checkinteger(L, 1));
    if (name != NULL) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
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
    unitNames(L, u);
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
    static const luaL_Reg map[] = {{"name", lMapName},   {"area", lMapArea},   {"place", lMapPlace},
                                   {"remove", lMapRemove}, {"edits", lMapEdits}, {NULL, NULL}};
    static const luaL_Reg battle[] = {
        {"active", lBattleActive},   {"turn", lBattleTurn},         {"units", lBattleUnits},
        {"unit", lBattleUnit},       {"mario", lBattleMario},       {"partner", lBattlePartner},
        {"set_hp", lBattleSetHp},    {"set_max_hp", lBattleSetMaxHp}, {"set_fp", lBattleSetFp},
        {"kind_name", lBattleKindName}, {NULL, NULL},
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
