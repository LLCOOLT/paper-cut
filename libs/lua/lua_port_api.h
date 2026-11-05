/* Paper Cut's additions to the vendored Lua (libs/lua/lua_port.c and
 * lua_port_match.c, this project's own files; libs/lua/src is unchanged),
 * for Lua mods (port/mods/lua_host.cpp, docs/LUA_MODDING.md). */
#ifndef PAPER_CUT_LUA_PORT_API_H
#define PAPER_CUT_LUA_PORT_API_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;

/* --- creation numbers (lua_port.h) ---
 * A state's allocator: blocks with a small header holding their number,
 * counted in *counter (the state's own, from 0) as they are made. Lua hashes
 * tables, functions and coroutines by these numbers, so a lua_State made
 * with any other allocator must not be used. size is never 0. */
void* lua_port_realloc(void* block, size_t size, unsigned int* counter);
void lua_port_free(void* block);
/* The creation number of the value at idx: a table, function, coroutine,
 * string or userdata's (1 and on); 0 for any other (a light C function). */
unsigned int lua_port_objid(struct lua_State* L, int idx);

/* --- pattern matching with a budget (lua_port_match.c) ---
 * string.find, match, gmatch and gsub as Lua 5.4.8 has them, counting their
 * steps (a character looked at, a backtrack): every so many, hook(L, steps)
 * is called, which may raise an error (luaL_error) to stop the match. Puts
 * them in the table on the top of the stack (the string library). */
typedef void (*lua_port_matchhook)(struct lua_State* L, unsigned long steps);
void lua_port_openmatch(struct lua_State* L, lua_port_matchhook hook);

#ifdef __cplusplus
}
#endif

#endif
