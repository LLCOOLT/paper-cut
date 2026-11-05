/* Creation numbers for Lua mods' states (libs/lua/lua_port.h,
 * lua_port_api.h): each block a state allocates carries its number in a
 * header before it, and tables, functions and coroutines hash by it instead
 * of by their address, so pairs() visits them in the same order on every
 * computer. This project's own file; libs/lua/src is unchanged. */
#define LUA_CORE

#include "lprefix.h"

#include <stdlib.h>

#include "lua.h"

#include "lobject.h"
#include "lstate.h"

#include "lua_port_api.h"

/* 16 bytes, so the block after it keeps malloc's alignment */
typedef struct PortHeader {
    unsigned int seq;
    unsigned int pad[3];
} PortHeader;

/* a coroutine's GCObject is the lua_State inside lstate.c's LX (and the
 * main thread's inside LG, which starts with an LX) */
typedef struct PortLX {
    lu_byte extra_[LUA_EXTRASPACE];
    lua_State l;
} PortLX;

void* lua_port_realloc(void* block, size_t size, unsigned int* counter) {
    PortHeader* h;
    if (block == NULL) {
        h = (PortHeader*)malloc(sizeof(PortHeader) + size);
        if (h == NULL) {
            return NULL;
        }
        h->seq = ++*counter;
        return h + 1;
    }
    h = (PortHeader*)realloc((PortHeader*)block - 1, sizeof(PortHeader) + size);
    return h != NULL ? h + 1 : NULL; /* (the number moves with it) */
}

void lua_port_free(void* block) {
    if (block != NULL) {
        free((PortHeader*)block - 1);
    }
}

unsigned int lua_port_gcseq(const struct GCObject* o) {
    const char* start = (const char*)o;
    if (o->tt == LUA_VTHREAD) {
        start -= offsetof(PortLX, l);
    }
    return ((const PortHeader*)start - 1)->seq;
}

unsigned int lua_port_objid(lua_State* L, int idx) {
    unsigned int id = 0;
    const TValue* o;
    lua_pushvalue(L, idx);
    o = s2v(L->top.p - 1);
    if (iscollectable(o)) {
        id = lua_port_gcseq(gcvalue(o));
    }
    lua_pop(L, 1);
    return id;
}
