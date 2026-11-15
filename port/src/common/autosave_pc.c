/* Autosave at room changes (Gameplay > Autosave, off by default;
 * port/settings/features/autosave.cpp): after Mario goes into another room,
 * once the player has the controls there, the game is saved into the file
 * being played, as a Save Block saves it, so a crash, a Game Over or a closed
 * window costs no more than the room.
 *
 * A room change is any map change the game makes (SEQ_MAP_CHANGE), the same
 * map again too: the Pit of 100 Trials' floors are a few maps over and over
 * (jon_00 for floor after floor), and by the map's name alone none of them
 * was ever a change, so the Pit never saved (#71). Its floor is the file's
 * (GSW_JON_FLOOR), so a save there loads on that floor.
 *
 * The save is quick resume's (resume_aurora.c, portCardSnapshot): made in
 * memory without touching the running game, then put in the card's slot
 * (portCardLoadSlotImage) and written out by the game's own card manager as
 * the file select's Copy writes a slot (cardCopy onto itself: both copies on
 * the card, the other one first). It loads where Mario stood as he came in.
 *
 * Not while the card is busy (a Save Block's own save), in a scene, a battle,
 * a menu or a partner's action (quick resume's "safe"), inside a house or
 * shop shown on walking in, in an exhibition's ring, while any mode lends
 * (port/include/port/mode_lending.h: the boss rush, the endless Glitz Pit,
 * refights, the raid, the dojo, puzzles, versus, the Paper Tower's floors:
 * the pouch or the map isn't the file's, and such a save would load on a map
 * with no way out), or after the practice tools changed the game. The choice
 * of how often: every room, or at a room change once 2, 5 or 10 minutes have
 * gone since the last save.
 *
 * The modes that keep progress of their own (Aurora) keep it at each floor or
 * round instead, whatever the minutes: the Paper Tower's and the Pit in
 * co-op's deepest floor (paper_tower.txt, pit_coop.txt), the endless Glitz
 * Pit's most rounds (modes.txt, port/settings/features/modes.cpp).
 *
 * Online, the host saves its own file as at home. Not a co-op guest's game
 * (its world is the host's: port/statesync.h), nor during a replay: the save
 * puts a slot into the game's card data and keeps the card manager busy for a
 * few frames on this game alone and on this machine's clock, which the
 * recording doesn't have. Nor the title menu's file for the modes (never
 * saved). Headless too (tests): the same, without the modes. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "manager/cardmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/statesync.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/resume.h"
#include "port/settings.h"
#include "pmario_sound.h"
#ifdef TTYD_BACKEND_AURORA
#include <SDL3/SDL_timer.h>
#include "port/mode_lending.h"
#include "port/title_menu.h"
#include "port/postgame.h"
#include "port/postgame_extra.h"
#include "port/playmodes.h"
#endif

BOOL portCardSnapshot(void* image);                     /* cardmgr.c */
BOOL portCardLoadSlotImage(s32 slot, const void* image); /* cardmgr.c */

static uint8_t sImage[PORT_CARD_SLOT_SIZE];
static char sRoom[sizeof(((GlobalWork*)0)->currentMapName)];
static int sPending, sWriting, sChanged;
static unsigned long long sLastSave;
static int* sMode;

/* when the last autosave finished writing (SDL ms, 0: none yet), and whether it failed */
unsigned long long gPortAutosaveAt;
int gPortAutosaveFailed;

/* this machine's clock in ms (the mark's: SDL's on Aurora) */
static unsigned long long nowMs(void) {
#ifdef TTYD_BACKEND_AURORA
    return SDL_GetTicks();
#else
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (unsigned long long)ts.tv_sec * 1000u + (unsigned long long)(ts.tv_nsec / 1000000);
#endif
}

int portAutosaveOn(void) {
    if (sMode == NULL) {
        sMode = portSettingsDeclare("autosave", 0, 0, 4);
    }
    return *sMode != 0;
}

void portAutosaveMark(int ok) {
    const unsigned long long now = nowMs();
    gPortAutosaveFailed = !ok;
    gPortAutosaveAt = now != 0 ? now : 1;
}

/* as quick resume's (resume_aurora.c): not inside a house or shop shown on walking in (a save there loads
   with the floor gone and Mario falling for ever), nor in an exhibition's room (no way out but its fight).
   Not safe now keeps the save pending, for when it is */
static int safe(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && marioChkKey() &&
           marioChkCtrl() && !(psndGetFlag() & PSND_FLAG_MUTE_DOOR_SFX) &&
#ifdef TTYD_BACKEND_AURORA
           !portExhibitionRoom() &&
#endif
           gp->saveSlot >= 0 && gp->saveSlot < 4;
}

/* the modes' own progress, kept as it is made (Aurora: their files; each writes only a new deepest floor, by
   its own rules: not a helped climb) */
static void modesFrame(void) {
#ifdef TTYD_BACKEND_AURORA
    if (portReplayActive()) {
        return;
    }
    if (portTowerAutosave() | portPitCoopAutosave()) {
        portAutosaveMark(1);
    }
#endif
}

/* once a retrace (vi_aurora.c, the headless vi_pc.c), after quick resume's */
void portAutosaveFrame(void) {
    static const unsigned long long kGap[] = {0, 0, 2 * 60000, 5 * 60000, 10 * 60000};
    const unsigned long long now = nowMs();
    const int on = portAutosaveOn();
    if (sWriting) {
        /* the game's card manager writes it over the next frames */
        if (!cardIsExec()) {
            sWriting = 0;
            portAutosaveMark(cardGetCode() == CARD_RESULT_READY);
        }
        return;
    }
    if (on && gp != NULL) {
        modesFrame();
    }
    if (!on || gp == NULL || seqGetSeq() != SEQ_GAME) {
        /* the title, a file loading, a Game Over: the first room after isn't a change (a room
         * change itself goes through SEQ_MAP_CHANGE, and a battle returns to its room) */
        if (!on || gp == NULL || (seqGetSeq() != SEQ_MAP_CHANGE && seqGetSeq() != SEQ_BATTLE)) {
            sRoom[0] = '\0';
            sPending = 0;
            sChanged = 0;
        } else if (seqGetSeq() == SEQ_MAP_CHANGE && sRoom[0] != '\0') {
            sChanged = 1; /* (from a room played: the same map again counts too, a Pit floor) */
        }
        return;
    }
    if (sChanged || strncmp(sRoom, gp->currentMapName, sizeof(sRoom)) != 0) {
        if (sRoom[0] != '\0') {
            sPending = 1;
        }
        sChanged = 0;
        memcpy(sRoom, gp->currentMapName, sizeof(sRoom));
        sRoom[sizeof(sRoom) - 1] = '\0';
    }
    if (!sPending || !safe() || cardIsExec()) {
        return;
    }
    if (
#ifdef TTYD_BACKEND_AURORA
        portModeLending() || /* (a mode lends: a loadout, its map, versus's words; port/include/port/mode_lending.h) */
        portTitleTempFile() || /* (nor the title menu's file for the modes, never saved: title_menu.h) */
#endif
        portPracticeTainted() ||
        portStateSyncCopyHeld() || /* (nor online the host's world, a co-op guest's: port/statesync.h) */
        portReplayActive()) { /* (this game alone: see the top) */
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
        sLastSave = now != 0 ? now : 1;
        if (strncmp(sRoom, "jon_", 4) == 0) {
            printf("[autosave] file %d saved in %s, Pit floor %d\n", gp->saveSlot + 1, sRoom,
                   (int)swByteGet(GSW_JON_FLOOR) + 1);
        } else {
            printf("[autosave] file %d saved in %s\n", gp->saveSlot + 1, sRoom);
        }
    } else {
        printf("[autosave] file %d: no snapshot in %s\n", gp->saveSlot + 1, sRoom);
    }
    fflush(stdout);
}
