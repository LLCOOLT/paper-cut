#ifndef TTYD_MOD_COMPAT_H
#define TTYD_MOD_COMPAT_H

/* For porting GameCube mods (docs/MODDING.md, "Porting a GameCube mod"),
 * C++: ttyd-tools' patch::hookFunction on the port's hook slots.
 *
 *     g_marioStMain_trampoline = TTYD_HOOK_FUNCTION(marioStMain, runOncePerFrame);
 *
 * replaces the game's function by dest and returns the game's own (what the
 * GameCube trampoline was), found by the function's name (fn is only named,
 * so a GameCube name the decomp lacks is fine as long as a slot has it): a hook slot must
 * exist for it (port/mods/hooks.spec). A function's address can't be
 * compared across the game and a mod's library on Windows (the library sees
 * an import thunk), hence the name. */

#include "ttyd_mod/vars.h"

#ifdef __cplusplus
#include <cstring>

inline int ttydHookSlot(const char* name) {
    static const char* const names[] = TTYD_HOOK_NAMES;
    // a qualified name (ttyd::animdrv::animPoseMain): the function's own
    for (const char* p = name; *p != '\0'; p++) {
        if (p[0] == ':' && p[1] == ':') {
            name = p + 2;
        }
    }
    for (int i = 0; i < TTYD_HOOK_COUNT; i++) {
        if (std::strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

/* The game's own function, as whatever function pointer type the mod's
 * trampoline has: a GameCube mod often declares a hooked function with its
 * own types (bool for the game's BOOL, its own struct names), as ttyd-tools'
 * hookFunction allowed. */
struct TtydOriginal {
    void* fn;
    template <typename T>
    operator T*() const {
        return reinterpret_cast<T*>(fn);
    }
};

template <typename D>
TtydOriginal ttydHookFunction(const char* name, D dest) {
    const int slot = ttydHookSlot(name);
    if (slot < 0) {
        ttyd_mod_api->log("no hook slot for %s: it stays as the game has it", name);
        return {nullptr};
    }
    if (ttyd_mod_api->hookReplace(slot, reinterpret_cast<void*>(dest)) != 0) {
        return {nullptr};
    }
    return {ttyd_mod_api->original(slot)};
}

#define TTYD_HOOK_FUNCTION(fn, dest) ttydHookFunction(#fn, dest)
#endif

#endif
