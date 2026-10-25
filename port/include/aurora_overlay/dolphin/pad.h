/* Aurora build: Aurora's <dolphin/pad.h>, with the game's rumble calls routed
 * through the port's settings (rumble on/off, strength, the phone's own
 * vibration: portPADControlMotor, port/src/aurora/settings_aurora.c). */
#include_next <dolphin/pad.h>

#ifndef PORT_AURORA_OVERLAY_PAD_H
#define PORT_AURORA_OVERLAY_PAD_H
#ifndef __cplusplus
void portPADControlMotor(unsigned int chan, unsigned int cmd);
#define PADControlMotor portPADControlMotor
#endif
#endif
