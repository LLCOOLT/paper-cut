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
/* A host feature's own drawing there, each frame of play (Crowd Control's
 * notices); callable from a static initializer. Text and rectangles in the
 * game's 640x480 coordinates, over the picture. */
void portModHostAddDraw(void (*fn)(void*), void* user);
void portModHostDrawText(float x, float y, float size, unsigned int rgba, const char* text);
void portModHostDrawRect(float x, float y, float w, float h, unsigned int rgba);
/* rel_pc.c: an area's code linked (entered 1) or about to be unlinked (0). */
void portModsAreaEvent(const char* area, int entered);
/* mod_game_aurora.c: a shop being set up, a battle starting. */
void portModsShopEvent(const char* map, void* items, int count);
void portModsBattleEvent(void* battleWork);
/* The hook slots the mods' code took, for the mod manager: the i-th use's mod
 * id (NULL past the last), its slot's function name and kind (0 pre, 1 post,
 * 2 replace). */
const char* portModHostHookUse(int i, const char** slotName, int* kind);
/* For mod_game_aurora.c: a host hook (before every mod's). kind 0 pre, 1 post. */
void portModHostAddHook(int kind, int slot, void* fn);

/* port/mods/mods.cpp: a mod's option, a message set by code. */
int portModsOptionInt(const char* modId, const char* key, int fallback);
/* Paper Cut's own message texts (the badge sets, PM64's moves, bosses...): outside the mods' budget and
 * tag limit, so a mod can never crowd them out. 0 (logged) only for a text past 64 KB. */
int portModsSetMessage(const char* tag, const char* text);
/* A mod's (Lua's ttyd.message, the code API's setMessage): 0 when refused (the first few logged): a text
 * past 64 KB, past the 16 MB of mods' kept texts, or past 16384 tags made by mods. */
int portModsSetModMessage(const char* tag, const char* text);

/* mod_game_aurora.c (game side): host hooks on the shop and battle slots. */
void portModGameInit(void);

#ifdef __cplusplus
}
#endif

#endif
