/* A host pointer as the 32-bit number the game would have seen on the
 * GameCube (port/include/port/platform.h, portGameAddress).
 *
 * Script words hold pointers on PC (EvtWord is 64-bit). Where a script reads
 * one back as a number (a float, through check_float's 32 bits) the host
 * address leaked into play, and with address-space randomization it changes
 * from run to run: the credits' return to Rogueport rolled the ship by a
 * door's data pointer gor_01 had left in GW(6), so two runs of the same input
 * went out of step there, as online games or a replay's playback would. The
 * GameCube's own address was a fixed 0x80xxxxxx; this gives the same kind of
 * number, from where the pointer is in the game's memory (MEM1: the same
 * offset every run), so the game plays as it did there, the same every run. */
#include <dolphin/types.h>
#include <stddef.h>
#include <stdint.h>
#include "port/platform.h"

#if defined(TTYD_BACKEND_AURORA)
#include <dolphin/os.h>
#else
void portHeadlessArena(void** base, size_t* size); /* sdk/os_pc.c */
#endif

#if defined(_WIN32)
extern char __ImageBase; /* the linker's: the executable's start */
#endif

s32 portGameAddress(const void* p) {
    const uintptr_t a = (uintptr_t)p;
    uintptr_t lo;
    size_t size;
#if defined(TTYD_BACKEND_AURORA)
    lo = (uintptr_t)OSBaseAddress;
    size = OSGetPhysicalMemSize();
#else
    {
        void* base;
        portHeadlessArena(&base, &size);
        lo = (uintptr_t)base;
    }
#endif
    if (a >= lo && a - lo < size) {
        return (s32)(0x80000000u + (u32)((a - lo) & 0x7FFFFFFu)); /* MEM1 */
    }
#if defined(_WIN32)
    if (a >= (uintptr_t)&__ImageBase && a - (uintptr_t)&__ImageBase < 0x8000000u) {
        return (s32)(0x88000000u + (u32)(a - (uintptr_t)&__ImageBase)); /* the game's own data */
    }
#endif
    return (s32)0x8F000000u; /* elsewhere: one fixed number */
}
