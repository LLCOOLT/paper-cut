/* The second screen's Start menu (port/include/port/companion_menu.h) runs
 * the game's own menu code a second time, with statics of its own. Included
 * first in a file, this puts the file's data and bss in a block of their own:
 *
 * - PORT_SECTION_MENU (src/window/win_*.c): the Start menu's statics. The
 *   second screen's menu swaps the block for its own copy around each of its
 *   frames (companion_menu_aurora.c).
 * - PORT_SECTION_KEEP (the decoders, anim_pc.c, tpl_pc.c, gx_arrays_pc.c):
 *   what the decoders know of the files they decoded. A frame of the second
 *   screen's menu puts the game's data and bss back as they were before it,
 *   all but this block: the files it loaded for itself stay known.
 *
 * Both stay in the save states (snapshot_bounds_pc.c, savestate_aurora.c).
 * On Windows they are grouped sections inside the save states' (the linker
 * sorts them by suffix: _k0/_k2 and _n0/_n2 hold the markers); on Apple and
 * ELF sections of their own. Only with the save states' sections (Aurora,
 * the RELs linked in): elsewhere the file is left as it is and the second
 * screen's menu is off (PORT_COMPANION_MENU_SECTIONS undefined). */
#ifndef PORT_COMPANION_SECTION_H
#define PORT_COMPANION_SECTION_H
/* (not in the browser: there's no second screen there, and clang ignores the pragma for wasm) */
#if defined(TTYD_BACKEND_AURORA) && defined(TTYD_SAVE_STATES) && defined(TTYD_SNAPSHOT_SECTION) && \
    !defined(TTYD_NO_SNAPSHOT) && !defined(__EMSCRIPTEN__)
#define PORT_COMPANION_MENU_SECTIONS 1
#endif
#endif

#ifdef PORT_COMPANION_MENU_SECTIONS
#if defined(PORT_SECTION_MENU)
#if defined(__APPLE__)
#pragma clang section data = "__DATA,__ttyd_menud" bss = "__DATA,__ttyd_menub"
#elif defined(_WIN32)
#include "port/coff_section.h"
PORT_COFF_SECTIONS(".data$ttyd_snap_n1", ".bss$ttyd_snap_n1")
#else
#pragma clang section data = "ttyd_menud" bss = "ttyd_menub"
#endif
#elif defined(PORT_SECTION_KEEP)
#if defined(__APPLE__)
#pragma clang section data = "__DATA,__ttyd_keepd" bss = "__DATA,__ttyd_keepb"
#elif defined(_WIN32)
#include "port/coff_section.h"
PORT_COFF_SECTIONS(".data$ttyd_snap_k1", ".bss$ttyd_snap_k1")
#else
#pragma clang section data = "ttyd_keepd" bss = "ttyd_keepb"
#endif
#endif
#endif
