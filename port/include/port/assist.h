#ifndef PORT_ASSIST_H
#define PORT_ASSIST_H

/* Hold instead of mashing (Controls > One-handed controls; port/src/common/
 * assist_pc.c): while a mash action command runs, a held button it counts
 * presses of is pressed and let go for the player, about 12 times a second
 * (a good masher's pace, a little under the most). Done to the local
 * controllers before online co-op sends them, so both games see the same. */

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The mash action commands (src/battle/ac/ac_repeatedly*.c, the Supernova and
 * Clock Out special moves): this frame they count presses of buttons. */
void portMashWanted(unsigned int buttons);
/* DEMOPad.c, before online co-op's exchange: the held mash buttons pressed and
 * let go in turn, when the setting is on. */
void portAssistPad(PADStatus* pads);

#ifdef __cplusplus
}
#endif

#endif
