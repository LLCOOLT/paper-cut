/* The save states' sections (port_prefix.h's pragma, which this file is
 * compiled under): their bounds, read by port/src/aurora/savestate_aurora.c
 * as portSnapData{Start,End} and portSnapBss{Start,End}.
 * - Mach-O: the linker's section$start/section$end; one byte in each section
 *   keeps it present.
 * - COFF: grouped sections, which the linker sorts by their suffix: markers in
 *   .data$ttyd_snap_a/_z and .bss$ttyd_snap_a/_z around the game's _m.
 * - ELF: the linker's __start_/__stop_ around sections named like C
 *   identifiers; one byte in each keeps it present.
 * The second screen's menu's blocks (port/companion_section.h) likewise: on
 * Windows inside the save states' sections (_k0.._k2, _n0.._n2), elsewhere
 * sections of their own. */
#include "port/companion_section.h"
#if defined(TTYD_SAVE_STATES) && defined(__APPLE__)
__attribute__((used)) char portSnapDataPad = 1;
__attribute__((used)) char portSnapBssPad;
#ifdef PORT_COMPANION_MENU_SECTIONS
#pragma clang section data = "__DATA,__ttyd_menud" bss = "__DATA,__ttyd_menub"
__attribute__((used)) char portMenuDataPad = 1;
__attribute__((used)) char portMenuBssPad;
#pragma clang section data = "__DATA,__ttyd_keepd" bss = "__DATA,__ttyd_keepb"
__attribute__((used)) char portKeepDataPad = 1;
__attribute__((used)) char portKeepBssPad;
#pragma clang section data = "" bss = ""
#endif
#elif defined(TTYD_SAVE_STATES) && defined(_WIN32)
#pragma clang section data = ".data$ttyd_snap_a" bss = ".bss$ttyd_snap_a"
__attribute__((used)) char portSnapDataStart = 1;
__attribute__((used)) char portSnapBssStart;
#ifdef PORT_COMPANION_MENU_SECTIONS
#pragma clang section data = ".data$ttyd_snap_k0" bss = ".bss$ttyd_snap_k0"
__attribute__((used)) char portKeepDataStart = 1;
__attribute__((used)) char portKeepBssStart;
#pragma clang section data = ".data$ttyd_snap_k2" bss = ".bss$ttyd_snap_k2"
__attribute__((used)) char portKeepDataEnd = 1;
__attribute__((used)) char portKeepBssEnd;
#pragma clang section data = ".data$ttyd_snap_n0" bss = ".bss$ttyd_snap_n0"
__attribute__((used)) char portMenuDataStart = 1;
__attribute__((used)) char portMenuBssStart;
#pragma clang section data = ".data$ttyd_snap_n2" bss = ".bss$ttyd_snap_n2"
__attribute__((used)) char portMenuDataEnd = 1;
__attribute__((used)) char portMenuBssEnd;
#endif
#pragma clang section data = ".data$ttyd_snap_z" bss = ".bss$ttyd_snap_z"
__attribute__((used)) char portSnapDataEnd = 1;
__attribute__((used)) char portSnapBssEnd;
#pragma clang section data = "" bss = ""
#elif defined(TTYD_SAVE_STATES)
__attribute__((used)) char portSnapDataPad = 1;
__attribute__((used)) char portSnapBssPad;
#ifdef PORT_COMPANION_MENU_SECTIONS
#pragma clang section data = "ttyd_menud" bss = "ttyd_menub"
__attribute__((used)) char portMenuDataPad = 1;
__attribute__((used)) char portMenuBssPad;
#pragma clang section data = "ttyd_keepd" bss = "ttyd_keepb"
__attribute__((used)) char portKeepDataPad = 1;
__attribute__((used)) char portKeepBssPad;
#pragma clang section data = "" bss = ""
#endif
#endif

#include <stddef.h>
/* The second screen's menu's blocks (port/companion_section.h): which 0 the
 * menu's data, 1 its bss, 2 the decoders' data, 3 their bss. Their size, 0
 * without them; *start where they begin. */
#ifdef PORT_COMPANION_MENU_SECTIONS
#if defined(__APPLE__)
extern char sMenuDataStart __asm("section$start$__DATA$__ttyd_menud");
extern char sMenuDataEnd __asm("section$end$__DATA$__ttyd_menud");
extern char sMenuBssStart __asm("section$start$__DATA$__ttyd_menub");
extern char sMenuBssEnd __asm("section$end$__DATA$__ttyd_menub");
extern char sKeepDataStart __asm("section$start$__DATA$__ttyd_keepd");
extern char sKeepDataEnd __asm("section$end$__DATA$__ttyd_keepd");
extern char sKeepBssStart __asm("section$start$__DATA$__ttyd_keepb");
extern char sKeepBssEnd __asm("section$end$__DATA$__ttyd_keepb");
#elif defined(_WIN32)
#define sMenuDataStart portMenuDataStart
#define sMenuDataEnd portMenuDataEnd
#define sMenuBssStart portMenuBssStart
#define sMenuBssEnd portMenuBssEnd
#define sKeepDataStart portKeepDataStart
#define sKeepDataEnd portKeepDataEnd
#define sKeepBssStart portKeepBssStart
#define sKeepBssEnd portKeepBssEnd
#else
extern char __start_ttyd_menud[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_menud[] __attribute__((visibility("hidden")));
extern char __start_ttyd_menub[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_menub[] __attribute__((visibility("hidden")));
extern char __start_ttyd_keepd[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_keepd[] __attribute__((visibility("hidden")));
extern char __start_ttyd_keepb[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_keepb[] __attribute__((visibility("hidden")));
#define sMenuDataStart (*__start_ttyd_menud)
#define sMenuDataEnd (*__stop_ttyd_menud)
#define sMenuBssStart (*__start_ttyd_menub)
#define sMenuBssEnd (*__stop_ttyd_menub)
#define sKeepDataStart (*__start_ttyd_keepd)
#define sKeepDataEnd (*__stop_ttyd_keepd)
#define sKeepBssStart (*__start_ttyd_keepb)
#define sKeepBssEnd (*__stop_ttyd_keepb)
#endif

#if defined(__APPLE__)
extern char sSnapDataStart __asm("section$start$__DATA$__ttyd_snapd");
extern char sSnapDataEnd __asm("section$end$__DATA$__ttyd_snapd");
extern char sSnapBssStart __asm("section$start$__DATA$__ttyd_snapb");
extern char sSnapBssEnd __asm("section$end$__DATA$__ttyd_snapb");
#elif defined(_WIN32)
#define sSnapDataStart portSnapDataStart
#define sSnapDataEnd portSnapDataEnd
#define sSnapBssStart portSnapBssStart
#define sSnapBssEnd portSnapBssEnd
#else
extern char __start_ttyd_snapd[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_snapd[] __attribute__((visibility("hidden")));
extern char __start_ttyd_snapb[] __attribute__((visibility("hidden")));
extern char __stop_ttyd_snapb[] __attribute__((visibility("hidden")));
#define sSnapDataStart (*__start_ttyd_snapd)
#define sSnapDataEnd (*__stop_ttyd_snapd)
#define sSnapBssStart (*__start_ttyd_snapb)
#define sSnapBssEnd (*__stop_ttyd_snapb)
#endif

/* The save states' sections: which 0 data, 1 bss. */
size_t portSnapSection(int which, char** start) {
    char* lo = which == 0 ? &sSnapDataStart : &sSnapBssStart;
    char* hi = which == 0 ? &sSnapDataEnd : &sSnapBssEnd;
    *start = lo;
    return (size_t)(hi - lo);
}

size_t portCompanionBlock(int which, char** start) {
    char* lo;
    char* hi;
    switch (which) {
    case 0: lo = &sMenuDataStart; hi = &sMenuDataEnd; break;
    case 1: lo = &sMenuBssStart; hi = &sMenuBssEnd; break;
    case 2: lo = &sKeepDataStart; hi = &sKeepDataEnd; break;
    default: lo = &sKeepBssStart; hi = &sKeepBssEnd; break;
    }
    *start = lo;
    return (size_t)(hi - lo);
}
#else
size_t portSnapSection(int which, char** start) {
    (void)which;
    *start = NULL;
    return 0;
}

size_t portCompanionBlock(int which, char** start) {
    (void)which;
    *start = NULL;
    return 0;
}
#endif
