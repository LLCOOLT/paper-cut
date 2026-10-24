/* Force-included into every translation unit of the PC build (-include).
 * Neutralizes Metrowerks CodeWarrior extensions so the decomp sources
 * compile with GCC/Clang/MSVC. Never included by the matching build. */
#ifndef PORT_PREFIX_H
#define PORT_PREFIX_H

#define TARGET_PC 1

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
#define __fpclassifyf ttyd___fpclassifyf
static inline unsigned int __cntlzw(unsigned int x) {
    return x == 0 ? 32 : (unsigned int)__builtin_clz(x);
}
#endif
#endif
