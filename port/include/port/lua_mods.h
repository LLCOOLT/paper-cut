#ifndef PORT_LUA_MODS_H
#define PORT_LUA_MODS_H

/* Lua mods (docs/LUA_MODDING.md): a mod's lua/main.lua, run in a sandbox of
 * its own (port/mods/lua_host.cpp) with the game's events and an API to read
 * and change the game (port/src/aurora/lua_game_aurora.c).
 *
 * Each mod gets its own Lua state: the safe standard libraries only (no io,
 * os, package or debug, no file or network access), a memory cap and an
 * instruction budget per call. An error stops that mod (its message in the
 * Lua console, F4, and on the Mods tab); the game goes on.
 *
 * Online play: Lua mods run only if both players (and a watcher's two) run
 * the same ones, by a hash of their files sent in the hello; otherwise only
 * mods that say they are a display only (mod.json "lua": {"online": "hud"})
 * run, and their changes to the game are ignored. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- the host (lua_host.cpp) --- */

/* Lua mods are loaded (any state open): the game side's fast check. */
int portLuaActive(void);
/* Each game frame (lua_game_aurora.c, marioStMain's pre and post hooks):
 * the frame's start (a new set of mods put in place, hot reload; the game
 * side then sends "frame"), and its end (after the game side's "frame_end":
 * "hud", the HUD's drawing for this frame). */
void portLuaFrame(void);
void portLuaFrameEnd(void);

/* An event to every mod listening, with arguments by fmt: 'i' an int, 'n' a
 * double, 's' a string (or NULL: nil), 'b' a bool (int), 'u' a battle unit
 * (its id, an int). Handlers run in the mods' order. */
void portLuaEvent(const char* event, const char* fmt, ...);
/* The same, and each handler that returns a number replaces value (the next
 * handler gets the new one); the result. Only while mods may change the
 * game (not in an online session whose mods differ). */
int portLuaFilterInt(const char* event, int value, const char* fmt, ...);
/* Anyone listening to event (skip the work of making its arguments). */
int portLuaListening(const char* event);

/* Mods may change the game now (writes and filters count): no online session,
 * or one whose players run the same Lua mods. */
int portLuaWritesAllowed(void);
/* A challenge rule a Lua mod set (rules.set): 1 and *value, or 0. */
int portLuaRule(const char* key, int* value);

/* The Mods tab and the console: a mod's Lua state, by mod id. */
enum {
    PORT_LUA_NONE,     /* no Lua (or not loaded yet) */
    PORT_LUA_RUNNING,
    PORT_LUA_FAILED,   /* stopped by an error: *error says */
    PORT_LUA_PAUSED,   /* online, and the other player's Lua mods differ */
    PORT_LUA_HUD_ONLY, /* the same, but it runs as a display only */
};
int portLuaModState(const char* modId, const char** error);
/* The console's lines (prints, errors), oldest first; NULL past the end.
 * error: the line is an error. */
int portLuaConsoleCount(void);
const char* portLuaConsoleLine(int i, int* error);
void portLuaConsoleClear(void);
/* The newest error and when (SDL ticks), for a notice; NULL when none. */
const char* portLuaLastError(unsigned long long* ticks);
/* Every Lua mod started again from its files (the console's Reload). */
void portLuaReloadAll(void);
/* The set's hash ("" without Lua mods): what the online hello carries. */
const char* portLuaHash(void);

/* Inside the frame's ImGui pass (mod_api.cpp): the HUD the mods drew. */
void portLuaDraw(void);

/* --- the game side (lua_game_aurora.c; headless.c stubs it) --- */

struct lua_State;
/* Each new state: the game's libraries (player, items, flags, map, battle). */
void portLuaGameOpen(struct lua_State* L);
/* Once at start, from portModGameInit: the host hooks behind the events. */
void portLuaGameInit(void);
/* From the game's code (Enhancements' guard popup, the challenge rules): a
 * Guard or Superguard (super), and the guard's result to filter (battle_ac.c:
 * 4 a Guard, 5 a Superguard, 2 none). */
void portLuaGuard(void* unit, int super);
int portLuaGuardResult(int result);
int portLuaCommandLevel(int level);

#ifdef __cplusplus
}

#include <map>
#include <string>
#include <vector>

/* One mod's Lua, as mods.cpp read it: lua/'s files by path ("lua/main.lua"),
 * and the folder to watch for hot reload ("" for a zip). */
struct PortLuaModSpec {
    std::string id, name;
    std::string folder;
    std::map<std::string, std::string> files;
    bool hudOnline = false; /* mod.json "lua": {"online": "hud"} */
    std::string options;    /* its options' values ("key=value\n"...), for the hash */
};
/* The set of Lua mods in use (mods.cpp apply()), put in place at the next
 * frame's start; the old states closed. */
void portLuaSetMods(std::vector<PortLuaModSpec> mods);
/* The hash of such a set (also without a running game: the launcher's). */
std::string portLuaHashOf(const std::vector<PortLuaModSpec>& mods);
#endif

#endif
