/* Quick resume (System > Quick resume, on by default): the game as it is,
 * kept as a save of its own that the launcher's Continue loads, so a phone
 * app closed in the background (iOS ends them as it likes) or a window shut
 * without saving loses nothing.
 *
 * Save states can't do this: they hold this run's memory, pointers and all
 * (savestate_aurora.c). This is a real save instead, made as a Save Block
 * makes one (portCardSnapshot, cardmgr.c) but never written to the card:
 * about every half second while the player has control in the field (not in
 * a scene, a battle, a menu or a partner's action, where a save would catch a
 * script halfway), it's made in memory; it's written out as
 * <user>/resume_<game ID>.gci when the app goes to the background or quits,
 * and once a minute meanwhile. Beside it, resume_<game ID>.txt names the
 * save file it came from, so that Continue loads it as that file and a later
 * Save Block save goes to the player's own file. */
#include <stdio.h>
#include <string.h>
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/platform.h"
#include "port/resume.h"
#include "port/settings.h"

BOOL portCardSnapshot(void* image); /* cardmgr.c */

#define GCI_HEADER 0x40
#define SNAP_EVERY 30      /* frames between snapshots */
#define WRITE_EVERY 3600   /* frames between writes of a changed snapshot */

static uint8_t sImage[PORT_CARD_SLOT_SIZE];
static int sHave, sDirty, sSlot;
static unsigned sFrame, sLastWrite;
static int* sOn;

static int on(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("quick_resume", 1, 0, 1);
    }
    return *sOn != 0;
}

void portResumePath(char* out, size_t size, const char* gameId, int sidecar) {
    snprintf(out, size, "%s/resume_%s.%s", portSettingsUserPath(), gameId, sidecar ? "txt" : "gci");
}

/* the player has the controls in the field: a save now is a clean one */
static int safe(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && marioChkKey() && marioChkCtrl() &&
           gp->saveSlot >= 0 && gp->saveSlot < 4;
}

void portResumeFlush(void) {
    char path[1024];
    FILE* f;
    static uint8_t header[GCI_HEADER];
    static uint8_t meta[PORT_CARD_META_SIZE];
    if (!sHave || !sDirty) {
        return;
    }
    /* a GCI as the save warps read one (warp_pc.c): the game ID first, then
     * the card's file header (unused there), then the slot */
    memset(header, 0, sizeof(header));
    memcpy(header, PORT_GAME_ID, 6);
    portResumePath(path, sizeof(path), PORT_GAME_ID, 0);
    f = fopen(path, "wb");
    if (f == NULL) {
        return;
    }
    fwrite(header, 1, sizeof(header), f);
    fwrite(meta, 1, sizeof(meta), f);
    fwrite(sImage, 1, sizeof(sImage), f);
    fclose(f);
    portResumePath(path, sizeof(path), PORT_GAME_ID, 1);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%d\n", sSlot);
        fclose(f);
    }
    sDirty = 0;
    sLastWrite = sFrame;
}

/* once a retrace (vi_aurora.c) */
void portResumeFrame(void) {
    sFrame++;
    if (sFrame % SNAP_EVERY != 0 || !on()) {
        return;
    }
    if (safe() && portCardSnapshot(sImage)) {
        sHave = 1;
        sDirty = 1;
        sSlot = gp->saveSlot;
    }
    if (sDirty && sFrame - sLastWrite >= WRITE_EVERY) {
        portResumeFlush();
    }
}
