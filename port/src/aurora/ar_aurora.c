/* ARAM on Aurora: Aurora's AR (extern/aurora/lib/dolphin/AR.cpp) keeps ARAM in a
 * host buffer of AuroraConfig.mem2Size bytes (set in main_aurora.c) whose ARAM
 * addresses are offsets; ARGetStorageAddress exposes it. The MusyX PC layer
 * (hw_aramdma.c, hw_pcmixer.c) reads and writes ARAM through portAramPtr. */
#include <dolphin/ar.h>
#include "port/ar_pc.h"

u8* portAramPtr(u32 aramAddr) {
    u8* base = (u8*)ARGetStorageAddress();
    return base != NULL ? base + aramAddr : NULL;
}
