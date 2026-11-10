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

/* PADClamp as Aurora's (extern/aurora lib/dolphin/pad/pad.cpp, the SDK's
 * clamp regions): the dead zone taken off and the stick scaled into the
 * octagon, so the game sees the same stick values from the same input on
 * both backends (a scripted stick of 127 is 72 to the game). */
static void clampTrigger(u8* trigger, u8 min, u8 max) {
    if (*trigger <= min) {
        *trigger = 0;
    } else {
        if (*trigger > max) {
            *trigger = max;
        }
        *trigger -= min;
    }
}

static void clampStick(s8* px, s8* py, s32 max, s32 xy, s32 min) {
    s32 x = *px, y = *py, signX = 1, signY = 1;
    if (x < 0) {
        signX = -1;
        x = -x;
    }
    if (y < 0) {
        signY = -1;
        y = -y;
    }
    x = x <= min ? 0 : x - min;
    y = y <= min ? 0 : y - min;
    if (x == 0 && y == 0) {
        *px = *py = 0;
        return;
    }
    x = x * max / (127 - min);
    y = y * max / (127 - min);
    if (xy * y <= xy * x) {
        const s32 d = xy * x + (max - xy) * y;
        if (xy * max < d) {
            x = xy * max * x / d;
            y = xy * max * y / d;
        }
    } else {
        const s32 d = xy * y + (max - xy) * x;
        if (xy * max < d) {
            x = xy * max * x / d;
            y = xy * max * y / d;
        }
    }
    *px = (s8)(signX * x);
    *py = (s8)(signY * y);
}

void PADClamp(PADStatus* status) {
    s32 i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (status[i].err != PAD_ERR_NONE) {
            continue;
        }
        clampStick(&status[i].stickX, &status[i].stickY, 72, 40, 15);
        clampStick(&status[i].substickX, &status[i].substickY, 59, 31, 15);
        clampTrigger(&status[i].triggerLeft, 30, 180);
        clampTrigger(&status[i].triggerRight, 30, 180);
    }
}

u32 SIProbe(s32 chan) {
    return chan == 0 ? SI_GC_CONTROLLER : SI_ERROR_NO_RESPONSE;
}
