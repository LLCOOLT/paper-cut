/* Headless platform backend: no window, no input. Lets the game run (and
 * be tested) before a windowed backend exists. */
#include <stdlib.h>
#include "port/platform.h"

void portPlatformInit(void) {}

BOOL portPlatformPoll(void) {
    return TRUE;
}

void portPlatformPresent(void) {}

BOOL portPlatformReadPad(s32 chan, PADStatus* status) {
    return chan == 0; /* a controller in port 1 with nothing pressed */
}

/* Frame captures: nothing is drawn headless. */
void portShotRequest(const char* name) {
    (void)name;
}

BOOL portShotPending(void) {
    return FALSE;
}

void portShotFrame(void) {}

void portInterpSceneDrawn(void) {}

BOOL portInterpInBetween(void) {
    return FALSE;
}

void* portInterpScratchAlloc(u32 size) {
    (void)size;
    return NULL;
}

void portInterpKeep(void* p, unsigned int n) {
    (void)p;
    (void)n;
}
