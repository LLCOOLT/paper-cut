/* Windows (COFF): put the data and bss that follow in named sections, for the
 * save states' sections (port_prefix.h, snapshot_bounds_pc.c), the second
 * screen's menu blocks (companion_section.h) and each REL's data and bss
 * (CMakeLists.txt's generated rel_<area>_sections.h and rel_<area>_bounds.c).
 *
 *   PORT_COFF_SECTIONS(".data$name", ".bss$name")
 *   PORT_COFF_SECTIONS_END()      (back to .data and .bss)
 *
 * '#pragma clang section' is the way, but older clang (VS 2022's own clang
 * 18; LLVM 19, as in Apple clang 17; 23 is fine) drops its names for COFF:
 * every variable under it goes to one unnamed section, flagged bss when the
 * object's first one is. The linker then zero-fills the RELs' event scripts
 * and unit data (a crash entering the first REL area or starting a battle),
 * and no marker bounds the RELs' or the save states' data. CMakeLists.txt
 * tries the pragma on the compiler at configure time and defines
 * TTYD_COFF_MS_SECTIONS where it is broken: the Microsoft pragmas instead,
 * which every clang names correctly. They put a variable with an explicit
 * zero, and the bss itself, in sections that take room in the executable
 * (the zeros are written out), so they are only the fallback. And a
 * tentative definition ('char x;') takes the bss section in effect at the
 * end of the file, not where it stands: a bss marker in a file that moves
 * between sections names its own, PORT_COFF_BSS(".bss$name") char x;. */
#ifndef PORT_COFF_SECTION_H
#define PORT_COFF_SECTION_H

#define PORT_COFF_PRAGMA(x) _Pragma(#x)
#ifdef TTYD_COFF_MS_SECTIONS
#define PORT_COFF_SECTIONS(d, b) PORT_COFF_PRAGMA(data_seg(d)) PORT_COFF_PRAGMA(bss_seg(b))
#define PORT_COFF_SECTIONS_END() PORT_COFF_PRAGMA(data_seg()) PORT_COFF_PRAGMA(bss_seg())
#define PORT_COFF_BSS(b) __attribute__((section(b)))
#else
#define PORT_COFF_BSS(b)
#define PORT_COFF_SECTIONS(d, b) PORT_COFF_PRAGMA(clang section data = d bss = b)
#define PORT_COFF_SECTIONS_END() PORT_COFF_PRAGMA(clang section data = "" bss = "")
#endif

#endif
