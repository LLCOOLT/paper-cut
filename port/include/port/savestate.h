#ifndef PORT_SAVESTATE_H
#define PORT_SAVESTATE_H

/* Save states (Aurora backend, port/src/aurora/savestate_aurora.c): the
 * whole game kept in memory and put back, for this run of the game. Keys:
 * F5 saves to slot 1, F8 loads it; the settings page has every slot. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SAVE_STATE_SLOTS 4

/* Asks for a save (load 0) or a load into/from slot 0-3; done at the end of
 * the frame, when the game allows it (portSaveStateStatus says the result). */
void portSaveStateRequest(int slot, int load);
/* End of each frame (vi_aurora.c). */
void portSaveStateFrame(void);
/* This build can make save states. */
int portSaveStateSupported(void);
/* A slot's description (when and where), or NULL when empty. */
const char* portSaveStateInfo(int slot);
/* The last result, for a few seconds, or NULL. */
const char* portSaveStateStatus(void);

#ifdef __cplusplus
}
#endif

#endif
