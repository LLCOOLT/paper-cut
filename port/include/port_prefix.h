/* Force-included into every translation unit of the PC build (-include).
 * Neutralizes Metrowerks CodeWarrior extensions so the decomp sources
 * compile with GCC/Clang/MSVC. Never included by the matching build. */
#ifndef PORT_PREFIX_H
#define PORT_PREFIX_H

#define TARGET_PC 1

/* Save states (port/src/aurora/savestate_aurora.c): the game's data and bss
 * (and the port's state the game's data depends on) in their own sections,
 * copied as a whole. A REL's own sections header, included after this one,
 * puts its data in its REL's sections instead. Bounds
 * (snapshot_bounds_pc.c): Mach-O section$start/end; COFF grouped sections
 * .data$ttyd_snap_{a,m,z} with markers in _a and _z; ELF __start_/__stop_.
 * PORT_HOST_DATA: a variable among them that holds host scratch, not game
 * state, kept out. */
#if defined(TTYD_SAVE_STATES) && defined(TTYD_SNAPSHOT_SECTION) && !defined(TTYD_NO_SNAPSHOT)
#if defined(__APPLE__)
#pragma clang section data = "__DATA,__ttyd_snapd" bss = "__DATA,__ttyd_snapb"
#define PORT_HOST_DATA __attribute__((section("__DATA,__data")))
#elif defined(_WIN32)
#include "port/coff_section.h" /* (the pragma, or the fallback where clang drops its names) */
PORT_COFF_SECTIONS(".data$ttyd_snap_m", ".bss$ttyd_snap_m")
#define PORT_HOST_DATA __attribute__((section(".data")))
#elif defined(__EMSCRIPTEN__)
/* (clang ignores the pragma for wasm: tools/web/snap_cc.py moves the object's
 * .data.* and .bss.* to ttyd_snapd and ttyd_snapb once it is compiled) */
#define PORT_HOST_DATA __attribute__((section("ttyd_hostd")))
#else
#pragma clang section data = "ttyd_snapd" bss = "ttyd_snapb"
#define PORT_HOST_DATA __attribute__((section(".data")))
#endif
#else
#define PORT_HOST_DATA
#endif

#ifdef TTYD_BACKEND_AURORA
#include "port/aurora_compat.h"
#endif

#ifdef _WIN32
/* Before __declspec is erased below: the UCRT's <wchar.h> (reached through
 * <windows.h> and SDL) defines _Avx2WmemEnabledWeakValue __declspec(selectany);
 * erased, every C file including it would define it strongly, clashing with
 * the C++ objects (Aurora) that define it too. */
#include <wchar.h>
#endif

/* __declspec(section ".data"), __declspec(weak): section placement is
 * irrelevant on PC and weak linkage is not needed once the SDK is shimmed. */
#define __declspec(...)

/* MWCC sizes enums under '#pragma enumsalwaysint off' to their values. */
#define PORT_ENUM_BASE(type) : type

#ifndef PORT_PREFIX_INTRINSICS
#define PORT_PREFIX_INTRINSICS
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* MusyX configuration (matches cflags_static in configure.py). */
#define MUSY_VERSION_MAJOR 2
#define MUSY_VERSION_MINOR 0
#define MUSY_VERSION_PATCH 3
#define MUSY_TARGET 0 /* MUSY_TARGET_PC */

/* Metrowerks PowerPC intrinsics. */
/* Macros, not functions: glibc already declares __fabs/__fabsf. */
#define __fabs(x)  fabs(x)
#define __fabsf(x) fabsf(x)
static inline int port_abs(int x) { return x < 0 ? -x : x; }
#define __abs(x)   port_abs(x)
/* frsqrte is an estimate; callers refine with Newton steps, so exact is fine. */
static inline double __frsqrte(double x) { return 1.0 / sqrt(x); }
/* ARQ callbacks run later, as the DMA interrupt does (port/src/common/arq_defer_pc.c). */
#define ARQPostRequest portARQPostRequest

/* The game's sincosf(f32* sin, f32* cos, f32 degrees) is not the C library's. */
#define sincosf ttyd_sincosf
/* Game names a host C library also declares: the system's declarations come
 * first (the headers above, include-guarded), then the game's are renamed.
 * select: tik_05.c's variable vs POSIX select(); __fpclassifyf: the n64
 * effects' MWCC math.h copy vs glibc's and Apple's. */
#ifndef _WIN32
#include <sys/select.h>
#endif
#define select ttyd_select
/* rand: the GameCube's (MSL's LCG, port/src/common/runtime_pc.c), not the host's */
#define rand ttyd_rand
#define srand ttyd_srand
int ttyd_rand(void);
void ttyd_srand(unsigned int seed);
#define __fpclassifyf ttyd___fpclassifyf
#ifndef __cplusplus
/* The maths functions whose results differ between C libraries: the
 * GameCube's own (MSL's fdlibm, port/include/port/fdlibm_pc.h), the same on
 * every computer for online co-op's lockstep. Exact ones (sqrt, floor, fabs)
 * stay the system's. C only: C++ (the port's settings) keeps <cmath>. */
double ttyd_sin(double);
double ttyd_cos(double);
double ttyd_tan(double);
double ttyd_atan(double);
double ttyd_atan2(double, double);
double ttyd_asin(double);
double ttyd_acos(double);
double ttyd_pow(double, double);
double ttyd_log(double);
double ttyd_hypot(double, double);
#define sin ttyd_sin
#define cos ttyd_cos
#define tan ttyd_tan
#define atan ttyd_atan
#define atan2 ttyd_atan2
#define asin ttyd_asin
#define acos ttyd_acos
#define pow ttyd_pow
#define log ttyd_log
#define hypot ttyd_hypot
/* the float ones as MSL has them: the double function, rounded (atan2f is
 * the Glitz Pit's own, rels/tou2, on atan2) */
static inline float ttyd_sinf(float x) { return (float)ttyd_sin(x); }
static inline float ttyd_cosf(float x) { return (float)ttyd_cos(x); }
static inline float ttyd_tanf(float x) { return (float)ttyd_tan(x); }
static inline float ttyd_atanf(float x) { return (float)ttyd_atan(x); }
static inline float ttyd_asinf(float x) { return (float)ttyd_asin(x); }
static inline float ttyd_acosf(float x) { return (float)ttyd_acos(x); }
static inline float ttyd_powf(float x, float y) { return (float)ttyd_pow(x, y); }
static inline float ttyd_logf(float x) { return (float)ttyd_log(x); }
/* (not MSL's, but the port's C uses it: co-op's partner, second_player_pc.c) */
static inline float ttyd_hypotf(float x, float y) { return (float)ttyd_hypot(x, y); }
#undef sinf
#undef cosf
#undef tanf
#undef atanf
#undef asinf
#undef acosf
#undef powf
#undef logf
#undef hypotf
#define sinf ttyd_sinf
#define cosf ttyd_cosf
#define tanf ttyd_tanf
#define atanf ttyd_atanf
#define asinf ttyd_asinf
#define acosf ttyd_acosf
#define powf ttyd_powf
#define logf ttyd_logf
#define hypotf ttyd_hypotf
#endif
static inline unsigned int __cntlzw(unsigned int x) {
    return x == 0 ? 32 : (unsigned int)__builtin_clz(x);
}
#endif
#endif
