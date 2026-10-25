/* The save states' sections (port_prefix.h's pragma, which this file is
 * compiled under): their bounds, read by port/src/aurora/savestate_aurora.c
 * as portSnapData{Start,End} and portSnapBss{Start,End}.
 * - Mach-O: the linker's section$start/section$end; one byte in each section
 *   keeps it present.
 * - COFF: grouped sections, which the linker sorts by their suffix: markers in
 *   .data$ttyd_snap_a/_z and .bss$ttyd_snap_a/_z around the game's _m.
 * - ELF: the linker's __start_/__stop_ around sections named like C
 *   identifiers; one byte in each keeps it present. */
#if defined(TTYD_SAVE_STATES) && defined(__APPLE__)
__attribute__((used)) char portSnapDataPad = 1;
__attribute__((used)) char portSnapBssPad;
#elif defined(TTYD_SAVE_STATES) && defined(_WIN32)
#pragma clang section data = ".data$ttyd_snap_a" bss = ".bss$ttyd_snap_a"
__attribute__((used)) char portSnapDataStart = 1;
__attribute__((used)) char portSnapBssStart;
#pragma clang section data = ".data$ttyd_snap_z" bss = ".bss$ttyd_snap_z"
__attribute__((used)) char portSnapDataEnd = 1;
__attribute__((used)) char portSnapBssEnd;
#pragma clang section data = "" bss = ""
#elif defined(TTYD_SAVE_STATES)
__attribute__((used)) char portSnapDataPad = 1;
__attribute__((used)) char portSnapBssPad;
#else
typedef int portSnapshotBoundsUnused;
#endif
