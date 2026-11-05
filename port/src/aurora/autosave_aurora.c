/* Autosave at room changes (Gameplay > Autosave, off by default;
 * port/settings/features/autosave.cpp): after Mario goes into another room,
 * once the player has the controls there, the game is saved into the file
 * being played, as a Save Block saves it, so a crash, a Game Over or a closed
 * window costs no more than the room.
 *
 * The save is quick resume's (resume_aurora.c, portCardSnapshot): made in
 * memory without touching the running game, then put in the card's slot
 * (portCardLoadSlotImage) and written out by the game's own card manager as
 * the file select's Copy writes a slot (cardCopy onto itself: both copies on
 * the card, the other one first). It loads where Mario stood as he came in.
 *
 * Not while the card is busy (a Save Block's own save), in a scene, a battle,
 * a menu or a partner's action (quick resume's "safe"), during a boss rush or
 * endless Glitz Pit, or after the practice tools changed the game. The choice
 * of how often: every room, or at a room change once 2, 5 or 10 minutes have
 * gone since the last save.
 *
 * Not during online co-op or a replay either: the save puts a slot into the
 * game's card data and keeps the card manager busy for a few frames, on this
 * game alone and on this machine's clock (the minutes between saves), which
 * the other game, or the recording, doesn't have. */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_timer.h>
#include "driver/seqdrv.h"
#include "manager/cardmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/modes.h"
#include "port/online.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/raid.h"
#include "port/dojo.h"

BOOL portCardSnapshot(void* image);                     /* cardmgr.c */
BOOL portCardLoadSlotImage(s32 slot, const void* image); /* cardmgr.c */

static uint8_t sImage[PORT_CARD_SLOT_SIZE];
static char sRoom[sizeof(((GlobalWork*)0)->currentMapName)];
static int sPending, sWriting;
static Uint64 sLastSave;
static int* sMode;

/* when the last autosave finished writing (SDL ms, 0: none yet), and whether it failed */
unsigned long long gPortAutosaveAt;
int gPortAutosaveFailed;

static int safe(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && marioChkKey() && marioChkCtrl() &&
           gp->saveSlot >= 0 && gp->saveSlot < 4;
}

/* once a retrace (vi_aurora.c), after quick resume's */
void portAutosaveFrame(void) {
    static const Uint64 kGap[] = {0, 0, 2 * 60000, 5 * 60000, 10 * 60000};
    const Uint64 now = SDL_GetTicks();
    if (sMode == NULL) {
        sMode = portSettingsDeclare("autosave", 0, 0, 4);
    }
    if (sWriting) {
        /* the game's card manager writes it over the next frames */
        if (!cardIsExec()) {
            sWriting = 0;
            gPortAutosaveFailed = cardGetCode() != CARD_RESULT_READY;
            gPortAutosaveAt = now != 0 ? now : 1;
        }
        return;
    }
    if (*sMode == 0 || gp == NULL || seqGetSeq() != SEQ_GAME) {
        /* the title, a file loading, a Game Over: the first room after isn't a change (a room
         * change itself goes through SEQ_MAP_CHANGE, and a battle returns to its room) */
        if (*sMode == 0 || gp == NULL || (seqGetSeq() != SEQ_MAP_CHANGE && seqGetSeq() != SEQ_BATTLE)) {
            sRoom[0] = '\0';
            sPending = 0;
        }
        return;
    }
    if (strncmp(sRoom, gp->currentMapName, sizeof(sRoom)) != 0) {
        if (sRoom[0] != '\0') {
            sPending = 1;
        }
        memcpy(sRoom, gp->currentMapName, sizeof(sRoom));
        sRoom[sizeof(sRoom) - 1] = '\0';
    }
    if (!sPending || !safe() || cardIsExec()) {
        return;
    }
    if ((portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished) || portPracticeTainted() ||
        portRaidActive() || /* (a raid's loadout is lent: never saved) */
        portDojoRunning() || /* (nor the dojo's, port/include/port/dojo.h) */
        portOnlineActive() || portReplayActive()) { /* (this game alone: see the top) */
        sPending = 0;
        return;
    }
    if (sLastSave != 0 && now - sLastSave < kGap[*sMode]) {
        sPending = 0; /* too soon: the next room change tries again */
        return;
    }
    sPending = 0;
    if (portCardSnapshot(sImage) && portCardLoadSlotImage(gp->saveSlot, sImage)) {
        cardCopy(gp->saveSlot, gp->saveSlot);
        sWriting = 1;
        sLastSave = now;
        printf("[autosave] file %d saved in %s\n", gp->saveSlot + 1, sRoom);
    } else {
        printf("[autosave] file %d: no snapshot in %s\n", gp->saveSlot + 1, sRoom);
    }
    fflush(stdout);
}
