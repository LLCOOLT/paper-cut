#ifndef TTYD_MOD_VARS_H
#define TTYD_MOD_VARS_H

/* The game's variables in a C++ mod. A Windows DLL can't link to data in the
 * executable, so each is a reference bound by name when the mod's library
 * loads (the executable exports every game symbol):
 *     inline GlobalWork*& ttyd_var_gp = ttydGameVar<GlobalWork*>("gp");
 * tools/port/mod_headers.py writes these for a ported mod's declarations,
 * with `#define gp ttyd_var_gp`: a function parameter of the same name is
 * renamed with it, so it still shadows the variable. A name the game doesn't
 * export is listed by ttydGameVarsMissing(), its reference bound to zeroes. */

#include "ttyd_mod/ttyd_mod.h"

#ifdef __cplusplus
extern "C" const TtydModApi* ttyd_mod_api;

#ifdef _WIN32
extern "C" {
struct HINSTANCE__;
__attribute__((dllimport)) HINSTANCE__* GetModuleHandleW(const wchar_t* name);
__attribute__((dllimport)) long long (*GetProcAddress(HINSTANCE__* module, const char* name))();
}
#else
#include <dlfcn.h>
#endif

inline void* ttydGameFind(const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(nullptr), name));
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}

inline const char* ttyd_vars_missing[64];
inline int ttyd_vars_missing_count = 0;

/* Where a missing variable's reference points: zeroes, so a read finds
 * nothing and a write lands nowhere that matters (never null). */
alignas(16) inline unsigned char ttyd_vars_nowhere[65536];

/* A game function's GameCube instructions, as a mod patched them (its
 * `extern int32_t fn[]`, a `(uint32_t*)fn`): the PC's are native code, with
 * nothing of the GameCube's to patch, so the words a mod reads and writes are
 * these (tools/port/mod_headers.py points them here). */
alignas(16) inline unsigned char ttyd_code_nowhere[65536];
#define TTYD_GC_CODE(type) reinterpret_cast<type>(ttyd_code_nowhere)

template <typename T>
T& ttydGameVar(const char* name) {
    void* p = ttydGameFind(name);
    // a file's static: the build exports a pointer to it (port_static__...)
    if (p != nullptr && __builtin_strstr(name, "port_static__") != nullptr) {
        p = *static_cast<void* const*>(p);
    }
    if (p == nullptr) {
        if (ttyd_vars_missing_count < 64) {
            ttyd_vars_missing[ttyd_vars_missing_count++] = name;
        }
        p = ttyd_vars_nowhere;
    }
    return *static_cast<T*>(p);
}

/* A GameCube low-memory address (0x80000000 up to 0x80005000, where mods kept
 * settings and mailboxes) in the port's stand-in for it (portGcLowMem):
 * tools/port/mod_headers.py --gc-lowmem writes these. */
inline uintptr_t ttydGcLowMem() {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(ttydGameFind("portGcLowMem"));
    return base;
}
#define TTYD_GC_ADDR(address) (ttydGcLowMem() + static_cast<uintptr_t>((address) - 0x80000000u))

/* A GameCube address of the game's data (0x803DB890: GSW 1792 in marioSt), as
 * the same place on the PC: the symbol it is in, by the name the game exports
 * it under (through its pointer for a file's static), plus the PC offset of
 * the same field. tools/port/mod_headers.py --gc-data writes these, from
 * tools/port/gc_offsets.py. */
inline uintptr_t ttydGcDataBase(const char* name, bool viaPointer) {
    void* p = ttydGameFind(name);
    if (p != nullptr && viaPointer) {
        p = *static_cast<void* const*>(p);
    }
    if (p == nullptr && ttyd_vars_missing_count < 64) {
        ttyd_vars_missing[ttyd_vars_missing_count++] = name;
    }
    return p != nullptr ? reinterpret_cast<uintptr_t>(p) : reinterpret_cast<uintptr_t>(ttyd_vars_nowhere);
}
#define TTYD_GC_DATA(name, offset, viaPointer)                                                                  \
    ([]() -> uintptr_t {                                                                                       \
        static const uintptr_t base = ttydGcDataBase(name, viaPointer);                                       \
        return base;                                                                                           \
    }() + static_cast<uintptr_t>(offset))

/* A file's static function (or variable) by the pointer the build exports
 * for it (port_static__...), or null. */
inline void* ttydGameStatic(const char* name) {
    void* p = ttydGameFind(name);
    if (p == nullptr) {
        if (ttyd_vars_missing_count < 64) {
            ttyd_vars_missing[ttyd_vars_missing_count++] = name;
        }
        return nullptr;
    }
    return *static_cast<void* const*>(p);
}

/* Logs the variables the mod declares that the game doesn't export (each
 * is unusable: its reference is null). The ported mod's entry calls it. */
inline void ttydGameVarsMissing(void) {
    for (int i = 0; i < ttyd_vars_missing_count; i++) {
        ttyd_mod_api->log("the game has no variable %s: what uses it won't work", ttyd_vars_missing[i]);
    }
}
#endif

#endif
