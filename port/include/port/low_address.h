/* Host addresses under 2 GiB (port/src/common/low_address_pc.c).
 *
 * Some words hold either a pointer or a small number, and the GameCube told
 * them apart by the address bit 0x80000000: a sound name or its id
 * (include/sfx_name_word.h), an NPC's name or its number (evt_nannpc.c). On a
 * 64-bit host the image, the heaps and the stacks are normally far above
 * that, so a word under the number range's top is a number. Not always:
 * with Windows' "Randomize memory allocations (Bottom-up ASLR)" or
 * "High-entropy ASLR" off (Exploit Protection), the C heap, the stacks and
 * VirtualAlloc's memory start near the bottom of the address space. The game
 * then would not start (a check aborted) and, past it, a name in such memory
 * would be read as a number.
 *
 * So the start-up check notes such memory instead (portCheckHostAddress sets
 * gPortLowAddresses), and while it is noted a word in the number range that
 * points at readable text is a pointer (portWordIsHostText). Off, nothing
 * changes: the plain range test, as before.
 *
 * $TTYD_LOW_ADDRESSES=1 (tests, Windows): before main the program starts
 * itself again with bottom-up and high-entropy ASLR off, as those settings
 * do, so the heap and the stacks lie under 2 GiB; if it cannot, the free
 * address space from 4 GiB up is reserved instead (slow). */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* set when some host memory the game uses lies under 2 GiB */
extern int gPortLowAddresses;

/* w (0x10000 up, under 2 GiB) points at readable text: two or more bytes, no control bytes, a NUL within 128 */
int portLowWordIsText(uintptr_t w);

/* p lies in committed memory (a pointer's word, for the in-between frames' blending) */
int portLowAddressMapped(uintptr_t p);

/* The free space $TTYD_LOW_ADDRESSES reserved, in MiB (0: the option is off or not on this host). */
unsigned long long portLowAddressesReservedMiB(void);

/* $TTYD_LOW_ADDRESSES started this process with bottom-up and high-entropy ASLR off */
int portLowAddressesAslrOff(void);

/* w is a pointer, not a number in 0..numMax: above numMax; else, with low host memory noted, text at w.
 * Addresses under 64 KiB are never mapped on Windows, Linux or macOS, so such a word is a number. */
static inline int portWordIsHostText(uintptr_t w, uintptr_t numMax) {
    if (w > numMax) {
        return 1;
    }
    if (w < 0x10000u || !gPortLowAddresses) {
        return 0;
    }
    return portLowWordIsText(w);
}

#ifdef __cplusplus
}
#endif
