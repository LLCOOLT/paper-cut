/* Hold instead of mashing (port/include/port/assist.h): the mash action
 * commands say which buttons they count presses of (portMashWanted, during
 * the game's frame); at the next read of the controllers (DEMOPad.c), each of
 * those buttons a player holds is pressed for 2 frames and let go for 3: 12
 * presses a second at 60 Hz, a good masher's pace, a little under the most.
 * The game's frames, not the wall clock: the same with slow speed. On the
 * local controllers before online co-op's exchange, so both games see the
 * same presses. $TTYD_SET_hold_to_mash=1 for tests. */
#include "port/assist.h"
#include "port/settings.h"
#include <stdio.h>

#define MASH_PERIOD 5 /* frames */
#define MASH_DOWN 2   /* of them held */
#define MASH_GRACE 3  /* frames a mask stays wanted after the command last said so */
#define MASH_BUTTONS                                                                                                   \
    (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y | PAD_TRIGGER_Z | PAD_TRIGGER_L | PAD_TRIGGER_R)

static unsigned int sWanted; /* said during this frame */
static unsigned int sMask;   /* the buttons mashed for the player now */
static int sGrace;
static int sPresses; /* made for the player in this command */
static int sHeld[PAD_MAX_CONTROLLERS][16]; /* frames each button has been held */

void portMashWanted(unsigned int buttons) {
    sWanted |= buttons & MASH_BUTTONS;
}

void portAssistPad(PADStatus* pads) {
    static int* sOn;
    int i, b;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("hold_to_mash", 0, 0, 1);
    }
    if (sWanted != 0) {
        sMask = sWanted;
        sGrace = MASH_GRACE;
    } else if (sGrace > 0) {
        sGrace--;
    } else {
        if (sMask != 0 && sPresses > 0) { /* the command over: how many presses were made (tests read it) */
            printf("[hold to mash] %d presses made\n", sPresses);
            fflush(stdout);
        }
        sMask = 0;
        sPresses = 0;
    }
    sWanted = 0;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        for (b = 0; b < 16; b++) {
            const unsigned int bit = 1u << b;
            if (!*sOn || !(sMask & bit) || pads[i].err != PAD_ERR_NONE || !(pads[i].button & bit)) {
                sHeld[i][b] = 0;
                continue;
            }
            if (sHeld[i][b] % MASH_PERIOD == 0) {
                sPresses++;
            }
            if (sHeld[i][b]++ % MASH_PERIOD >= MASH_DOWN) {
                pads[i].button &= (unsigned short)~bit; /* let go, between presses */
                if (bit == PAD_TRIGGER_L) {
                    pads[i].triggerLeft = 0;
                } else if (bit == PAD_TRIGGER_R) {
                    pads[i].triggerRight = 0;
                }
            }
        }
    }
}
