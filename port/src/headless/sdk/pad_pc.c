/* Controllers via the platform backend. */
#include <dolphin.h>
#include <string.h>
#include "port/platform.h"

BOOL PADInit(void) {
    return TRUE;
}

int PADReset(u32 mask) {
    return TRUE;
}

u32 PADRead(PADStatus* status) {
    s32 chan;
    for (chan = 0; chan < PAD_MAX_CONTROLLERS; chan++) {
        memset(&status[chan], 0, sizeof(PADStatus));
        status[chan].err = portPlatformReadPad(chan, &status[chan]) ? PAD_ERR_NONE : PAD_ERR_NO_CONTROLLER;
    }
    return 0;
}

void PADControlMotor(s32 chan, u32 command) {}

void PADClamp(PADStatus* status) {}

u32 SIProbe(s32 chan) {
    return chan == 0 ? SI_GC_CONTROLLER : SI_ERROR_NO_RESPONSE;
}
