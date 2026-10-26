#ifndef PORT_MOD_HOST_H
#define PORT_MOD_HOST_H

/* The code-mod host (port/mods/mod_api.cpp), between the mods list
 * (port/mods/mods.cpp), the game (port/src/aurora/mod_game_aurora.c) and the
 * frame loop (vi_aurora.c). Mods themselves see ttyd_mod/ttyd_mod.h. */

#ifdef __cplusplus
extern "C" {
#endif

/* Once at start, before any mod loads: the hook slots' game functions and
 * the host's own hooks (frames, shops, battles). */
void portModHostInit(void);
/* Starts a mod's code: a library (libPath) or, for a mod built into the game,
 * its entry point. NULL on success, else why it didn't start. */
const char* portModHostLoadLibrary(const char* modId, const char* libPath, const char* dataDir);
const char* portModHostStartBuiltin(const char* modId, const char* dataDir);
/* A mod built into the game with this id (TTYD_BUILTIN_MODS). */
int portModHostHasBuiltin(const char* modId);
const char* portModHostBuiltinId(int index); /* NULL past the last */
const char* portModHostBuiltinJson(int index); /* its mod.json */

/* Inside the frame's ImGui pass (vi_aurora.c): the mods' drawing. */
void portModsDraw(void);
/* rel_pc.c: an area's code linked (entered 1) or about to be unlinked (0). */
void portModsAreaEvent(const char* area, int entered);
/* mod_game_aurora.c: a shop being set up, a battle starting. */
void portModsShopEvent(const char* map, void* items, int count);
void portModsBattleEvent(void* battleWork);
/* For mod_game_aurora.c: a host hook (before every mod's). kind 0 pre, 1 post. */
void portModHostAddHook(int kind, int slot, void* fn);

/* port/mods/mods.cpp: a mod's option, a message set by code. */
int portModsOptionInt(const char* modId, const char* key, int fallback);
void portModsSetMessage(const char* tag, const char* text);

/* mod_game_aurora.c (game side): host hooks on the shop and battle slots. */
void portModGameInit(void);

#ifdef __cplusplus
}
#endif

#endif
