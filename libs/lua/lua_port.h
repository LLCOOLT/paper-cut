/* Paper Cut's settings for the vendored Lua (libs/lua/src, unchanged 5.4.8),
 * force-included into each of its files (CMakeLists.txt, target lua).
 *
 * Lua mods (port/mods/lua_host.cpp) run in online play's lockstep, where
 * every player's game must do the same thing: Lua's own random seeds (the
 * clock and addresses) are made fixed, so strings hash, tables iterate and
 * table.sort pivots the same on every machine. math.random is seeded by the
 * host.
 *
 * The maths whose results differ between C libraries (math.sin, x ^ y...)
 * are the game's own fdlibm (port/include/port/fdlibm_pc.h, linked into the
 * game), as the game's code has them: the same on every computer. exp and
 * the logarithms in base 2 and 10 are made from fdlibm's pow and log. Exact
 * ones (floor, sqrt, fmod) stay the system's.
 *
 * Tables, functions and coroutines as table keys: Lua hashes them by their
 * address, which differs on every computer (and every run), so pairs()
 * would visit them in a different order on each. Here each is hashed by its
 * creation number instead: lua_port.c's allocator (which every mod's state
 * uses, port/mods/lua_host.cpp) numbers each block it gives out in a small
 * header before it, in the order the state asks for them, which is the
 * same everywhere the same mod runs the same way. Light C functions and
 * light userdata (never made by a mod) all hash alike, and so does the
 * string cache of lua_pushstring (lstring.c: it is a cache, keyed by the
 * text's address; it now hits by text only). ltable.c and lstring.c, the
 * two users of point2uint, are built with LUA_PORT_POINTER_HASH (CMakeLists.txt)
 * so that llimits.h is read here first and its point2uint replaced; the
 * vendored files themselves are unchanged. */
#ifndef PAPER_CUT_LUA_PORT_H
#define PAPER_CUT_LUA_PORT_H

#define luai_makeseed(L) 0x5eedu
#define l_randomizePivot() 0u

#if defined(LUA_PORT_POINTER_HASH)
#define LUA_CORE /* (as ltable.c and lstring.c define it next) */
#include "llimits.h"
struct GCObject;
unsigned int lua_port_gcseq(const struct GCObject* o);
#undef point2uint
#define point2uint(p) \
    _Generic((p), struct GCObject *: lua_port_gcseq((const struct GCObject*)(L_P2I)(p)), default: 0u)
#endif

#include <math.h>

double ttyd_sin(double);
double ttyd_cos(double);
double ttyd_tan(double);
double ttyd_asin(double);
double ttyd_acos(double);
double ttyd_atan2(double, double);
double ttyd_pow(double, double);
double ttyd_log(double);

static inline double lua_port_exp(double x) { return ttyd_pow(2.718281828459045, x); }
static inline double lua_port_log2(double x) { return ttyd_log(x) / 0.6931471805599453; }
static inline double lua_port_log10(double x) { return ttyd_log(x) / 2.302585092994046; }

#undef sin
#undef cos
#undef tan
#undef asin
#undef acos
#undef atan2
#undef pow
#undef log
#undef exp
#undef log2
#undef log10
#define sin ttyd_sin
#define cos ttyd_cos
#define tan ttyd_tan
#define asin ttyd_asin
#define acos ttyd_acos
#define atan2 ttyd_atan2
#define pow ttyd_pow
#define log ttyd_log
#define exp lua_port_exp
#define log2 lua_port_log2
#define log10 lua_port_log10

#endif
