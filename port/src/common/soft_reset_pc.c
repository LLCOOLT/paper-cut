/* The GameCube's reset, in the same process (port/include/port/soft_reset.h).
 *
 * On the console, L_gcResetCheck (mariost.c) blanks the picture, stops the
 * sound driver and the disc manager and restarts the machine
 * (OSResetSystem), then spins until it goes. There is no machine to restart
 * on PC: OSResetSystem is a stub on both backends, so the game froze in that
 * spin. Here the game goes back to its title by its own way instead:
 *
 * - the request (gp->resetType) waits for the memory card to be idle (as the
 *   US and PAL games wait) and for any sequence change under way (a map
 *   change, a battle starting or ending) to finish;
 * - then a map change to "title", as a Game Over's way back or the title
 *   menu's Modes file's: from the field, a battle (its exit restores the
 *   field first), a Game Over or the file select (both the game's new-game
 *   path). The map change's _unload re-initialises Mario, the map, the NPCs,
 *   the events, the windows and the pause menu, the status window and the
 *   rest, and the title starts as after the logos;
 * - the sound is stopped as the console's restart would (every voice faded,
 *   the reverb and the mix flags cleared); the sound driver and the disc
 *   manager are never torn down, so nothing has to come back up;
 * - over the logos or the title it does nothing: the title is already there.
 *
 * In a battle shared online the request is dropped: the other games play it frame for frame
 * with this one, and a reset would leave them waiting for it.
 *
 * Everything here reads only the game's own state, so a replay or an input script resets on
 * the same frame every time. */
#include "port/soft_reset.h"
#include "port/statesync.h"
#include <stdio.h>
#include "driver/camdrv.h"
#include "driver/fadedrv.h"
#include "driver/seqdrv.h"
#include "manager/cardmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "statuswindow.h"
#include "system.h"

enum { S_IDLE, S_GOING };

static int sState = S_IDLE;
static int sFrames;     /* frames since the map change to the title was asked for */
static int sSoundDone;  /* the sound stopped once the map change began (after a battle's exit restarted it) */
static int sRefusedLog; /* the shared battle's refusal logged (once a press) */

static void soundOff(void) {
    psndStopAllFadeOut();
    psndSFXAllOff();
    psndSetReverb(0);
    psndClearFlag(PSND_FLAG_BGM_OFF | PSND_FLAG_ENV_OFF | PSND_FLAG_SFX_OFF | PSND_FLAG_SFX_WITH_BGM |
                  PSND_FLAG_IDLE_MIX | PSND_FLAG_IDLE_MIX_OFF | PSND_FLAG_MUTE_SFX | PSND_FLAG_MUTE_DOOR_SFX);
}

int portSoftResetBusy(void) {
    return sState != S_IDLE || (gp != NULL && gp->resetType != 0);
}

void portSoftResetCheck(void) {
    SequenceType seq;
    int i;

    if (gp == NULL) {
        return;
    }
    seq = seqGetSeq();
    if (sState == S_GOING) {
        gp->resetType = 0; /* (one already on its way) */
        sFrames++;
        if (seq == SEQ_MAP_CHANGE && !sSoundDone) {
            soundOff();
            sSoundDone = 1;
        }
        if (seq == SEQ_TITLE && !seqCheckSeq()) {
            /* what a Game Over's way back puts right, and the title expects */
            statusWinDispOn();
            camGetPtr(CAMERA_2D)->flags &= ~CAM_FLAG_LETTER_BOX_DISABLE;
            sState = S_IDLE;
            printf("[reset] at the title (%d frames)\n", sFrames);
            fflush(stdout);
        } else if (sFrames > 1800) { /* (30 s: never seen; don't hold later resets off for good) */
            sState = S_IDLE;
            printf("[reset] the title never came (sequence %d): given up\n", (int)seq);
            fflush(stdout);
        }
        return;
    }
    if (gp->resetType == 0) {
        sRefusedLog = 0;
        return;
    }
    if (portStateSyncSharedBattle()) {
        if (!sRefusedLog) {
            printf("[reset] refused: a shared battle stays in step\n");
            fflush(stdout);
            sRefusedLog = 1;
        }
        gp->resetType = 0;
        return;
    }
    if (seq == SEQ_UNDEFINED || seq == SEQ_LOGO || seq == SEQ_TITLE || seq == SEQ_E3) {
        gp->resetType = 0; /* the title is there, or on its way */
        return;
    }
    if (cardIsExec() || seqCheckSeq() || seq == SEQ_MAP_CHANGE) {
        return; /* (kept: once the card is idle and the sequence settled) */
    }
    printf("[reset] type %d from sequence %d: back to the title\n", (int)gp->resetType, (int)seq);
    fflush(stdout);
    gp->resetType = 0;
    if (gp->systemLevelFlags != 0) {
        marioStSystemLevel(0); /* (the pause menu, an item get: the events run again, then go with the map) */
    }
    soundOff();
    for (i = 0; i < 4; i++) {
        padRumbleHardOff(i);
    }
    gp->mapFadeInType = FADE_OUT_BLACK;
    gp->mapFadeInDuration = 250;
    gp->areaFadeInType = FADE_OUT_BLACK;
    gp->areaFadeInDuration = 250;
    seqSetSeq(SEQ_MAP_CHANGE, "title", NULL);
    sState = S_GOING;
    sFrames = 0;
    sSoundDone = 0;
}
