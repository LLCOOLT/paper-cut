#ifndef TTYD_MOD_H
#define TTYD_MOD_H

/* The port's API for code mods (docs/MODDING.md, "Code mods").
 *
 * A code mod is a library (lib/<platform>/ in the mod) built against the
 * game's headers, or source built into the game (TTYD_BUILTIN_MODS, the
 * only way on iOS). It has one entry point, called once at start:
 *
 *     #include "ttyd_mod/ttyd_mod.h"
 *     TTYD_MOD_ENTRY(api) {
 *         api->log("hello");
 *         api->hookPost(TTYD_HOOK_BattleCalculateDamage, (void*)myPost);
 *         return 0;   // nonzero: the mod failed to start
 *     }
 *
 * It can call the game's functions directly (on Windows through the game's
 * import library, ttyd-<jp|us>.lib) and reach its variables through
 * api->find (Windows can't import data). The API only grows: a field added
 * later goes at the end, and version says which ones there are. */

/* C++ mods: the game's headers are C (extern "C"), and two of its structs
 * have a field named delete, which C++ sees as delete_. A C++ mod that
 * includes more game headers puts them between ttyd_mod/game_begin.h and
 * ttyd_mod/game_end.h the same way. */
#include "ttyd_mod/game_begin.h"
#include "battle/battle.h"
#include "battle/battle_audience.h"
#include "battle/battle_database_common.h"
#include "battle/battle_unit.h"
#include "driver/camdrv.h"
#include "driver/itemdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "evt/evt_shop.h"
#include "manager/evtmgr.h"
#include "mario/mario_pouch.h"
#include "win/win_main.h"
#include "ttyd_mod/hooks.h"
#include "ttyd_mod/game_end.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TTYD_MOD_API_VERSION 1

typedef struct TtydModApi {
    int version;          /* TTYD_MOD_API_VERSION of the game running the mod */
    const char* gameId;   /* "G8ME01" (US) or "G8MJ01" (JP) */
    const char* modId;    /* this mod's id (mod.json) */
    const char* dataDir;  /* a folder this mod may write its own files to */

    /* The game's log (and the console), prefixed with the mod's id. */
    void (*log)(const char* fmt, ...);

    /* Hooks on the game's functions (ttyd_mod/hooks.h has each slot's types):
     * pre runs before the function and can change its arguments or skip it,
     * post runs after and can change its result, replace runs instead of it.
     * Several mods can add pre and post hooks to one function; one mod can
     * replace it (the first in the mods' order). 0 on success. */
    int (*hookPre)(int slot, void* fn);
    int (*hookPost)(int slot, void* fn);
    int (*hookReplace)(int slot, void* fn);
    /* The game's own function of a slot (for a replacement to call). */
    void* (*original)(int slot);

    /* Each frame of the game (60 a second, whatever the display's rate):
     * before and after the game's own. */
    void (*onFrame)(void (*fn)(void* user), void* user);
    void (*onFrameEnd)(void (*fn)(void* user), void* user);

    /* Drawing over the game's picture, every frame shown, from fn: in the
     * game's 640x480 coordinates (scaled to the picture), size in those
     * units (the game's text is about 24), color 0xRRGGBBAA. Text is UTF-8. */
    void (*onDraw)(void (*fn)(void* user), void* user);
    void (*drawText)(float x, float y, float size, unsigned int rgba, const char* text);
    void (*drawRect)(float x, float y, float w, float h, unsigned int rgba);

    /* A message by tag (UTF-8), as a messages/ file would: added or replaced
     * for every language; NULL text removes it. */
    void (*setMessage)(const char* tag, const char* text);

    /* Areas: fn(user, area, entered) when an area's code is linked (entered
     * 1, e.g. "gor") and before it is unlinked (0). */
    void (*onArea)(void (*fn)(void* user, const char* area, int entered), void* user);
    /* A shop being set up: its goods (count of them, usually 6) can be
     * changed in place; they are the area's own data, which the game resets
     * the next time the area is entered. map is e.g. "gor_01". */
    void (*onShop)(void (*fn)(void* user, const char* map, ShopItem* items, int count), void* user);
    /* A battle starting, before its units are made: wp->fbatInfo->setup is
     * the encounter (its groupSetup the enemies), which may be pointed at
     * the mod's own. */
    void (*onBattleStart)(void (*fn)(void* user, BattleWork* wp), void* user);

    /* The mod's options (mod.json "options", set on the Mods tab): read them
     * whenever needed; they change while the game runs. */
    int (*optionInt)(const char* key, int fallback);

    /* A game function or variable by its decomp name ("pouchGetPtr",
     * "itemDataTable"), or NULL. */
    void* (*find)(const char* name);
} TtydModApi;

#if defined(TTYD_MOD_BUILTIN)
#define TTYD_MOD_CAT2(a, b) a##b
#define TTYD_MOD_CAT(a, b) TTYD_MOD_CAT2(a, b)
#define TTYD_MOD_ENTRY(api) int TTYD_MOD_CAT(ttyd_mod_init_, TTYD_MOD_BUILTIN)(const TtydModApi* api)
#elif defined(_WIN32)
/* (the game's prefix header turns MWCC's __declspec off: the attribute form) */
#define TTYD_MOD_ENTRY(api) TTYD_MOD_EXTERN_C __attribute__((dllexport)) int ttyd_mod_init(const TtydModApi* api)
#else
#define TTYD_MOD_ENTRY(api) TTYD_MOD_EXTERN_C __attribute__((visibility("default"))) int ttyd_mod_init(const TtydModApi* api)
#endif
#ifdef __cplusplus
#define TTYD_MOD_EXTERN_C extern "C"
#else
#define TTYD_MOD_EXTERN_C
#endif

#ifdef __cplusplus
}
#endif

#endif
