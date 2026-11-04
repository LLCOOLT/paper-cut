/* The input display (Interface > Input display; port/settings/features/
 * input_display.cpp): a controller as the game last read it, DEMOPadRead's
 * (src/sdk/DEMOPad.c) after the script, the keyboard and the clamp, the
 * state makeKey turns into the game's input. Only read, never changed. */
#include <demo/DEMOPad.h>
#include "port/display.h"

int portInputDisplayPad(int chan, unsigned short* buttons, signed char* sticks, unsigned char* triggers) {
    const PADStatus* p;
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) {
        return 0;
    }
    p = &DemoPad[chan].pst;
    if (p->err != PAD_ERR_NONE && p->err != PAD_ERR_TRANSFER) {
        return 0;
    }
    *buttons = p->button;
    sticks[0] = p->stickX;
    sticks[1] = p->stickY;
    sticks[2] = p->substickX;
    sticks[3] = p->substickY;
    triggers[0] = p->triggerLeft;
    triggers[1] = p->triggerRight;
    return 1;
}
