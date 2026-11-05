// Saved work driver: the event flags and words kept in the save file: the global ones (GSW/GSWF)
// and the area-local ones (LSW/LSWF).
#include "port_hook.h"
#include "driver/swdrv.h"
#include "mario/mariost.h"
#include <string.h>
#ifdef TARGET_PC
#include "port/open_world.h"
#endif
#ifdef PORT_MODS
#include "port/hooks/src_driver_swdrv.h"
#endif

/**
 * Clears all saved work: the global flags (GSWF) and words (GSW, including GSW0)
 * and the area-local ones (LSWF, LSW). Runs at boot, on a new or loaded file (seq_load) and in the E3 demo.
 */
void PORT_HOOKABLE(swInit)(void) {
    memset(gp->gswf, 0, sizeof(gp->gswf));
    memset(gp->gsw, 0, sizeof(gp->gsw));
    memset(gp->lswf, 0, sizeof(gp->lswf));
    memset(gp->lsw, 0, sizeof(gp->lsw));
    gp->gsw0 = 0;
}

/**
 * Clears only the area-local flags (LSWF) and words (LSW);
 * the map-change sequence calls it when Mario enters a different area.
 */
void PORT_HOOKABLE(swReInit)(void) {
    memset(gp->lswf, 0, sizeof(gp->lswf));
    memset(gp->lsw, 0, sizeof(gp->lsw));
}

/** Global Saved Work Flags -------------------------------------- */
void PORT_HOOKABLE(swSet)(s32 index) { // set specific bit
    gp->gswf[index / 32] |= (1 << (index % 32));
}

/** Returns whether global saved flag GSWF(index) is set. */
BOOL PORT_HOOKABLE(swGet)(s32 index) { // get specific bit
    return (gp->gswf[index / 32] & (1 << (index % 32))) != 0;
}

/** Clears global saved flag GSWF(index). */
void PORT_HOOKABLE(swClear)(s32 index) { // clear specific bit
    gp->gswf[index / 32] &= ~(1 << (index % 32));
}

/** Global Saved Work -------------------------------------------- */
void PORT_HOOKABLE(swByteSet)(s32 index, s32 value) {
    if (index == 0) {
#ifdef TARGET_PC
        if (portOpenStorySet(value)) { /* an open-world file: the chapter's own story (port/open_world.h) */
            return;
        }
#endif
        gp->gsw0 = value;
    } else {
        gp->gsw[index] = (u8)value;
    }
}

/** Returns global saved word GSW(index). GSW(0), the story progress, is a full 32-bit value; the others are bytes. */
s32 PORT_HOOKABLE(swByteGet)(s32 index) {
    if (index == 0) {
#ifdef TARGET_PC
        return portOpenStoryGet(gp->gsw0); /* an open-world file: the chapter's own story */
#else
        return gp->gsw0;
#endif
    } else {
        return gp->gsw[index];
    }
}

/** Local Saved Work Flags --------------------------------------- */
void PORT_HOOKABLE(_swSet)(s32 index) { // set specific bit
    gp->lswf[index / 32] |= (1 << (index % 32));
}

/** Returns whether area-local saved flag LSWF(index) is set. */
BOOL PORT_HOOKABLE(_swGet)(s32 index) { // get specific bit
    return (gp->lswf[index / 32] & (1 << (index % 32))) != 0;
}

/** Clears area-local saved flag LSWF(index). */
void PORT_HOOKABLE(_swClear)(s32 index) { // clear specific bit
    gp->lswf[index / 32] &= ~(1 << (index % 32));
}

/** Local Saved Work --------------------------------------------- */
void PORT_HOOKABLE(_swByteSet)(s32 index, u8 value) {
    gp->lsw[index] = value;
}

/** Returns area-local saved byte LSW(index). */
s32 PORT_HOOKABLE(_swByteGet)(s32 index) {
    return gp->lsw[index];
}

#ifdef PORT_MODS
#include "port/hooks/src_driver_swdrv.inc"
#endif
