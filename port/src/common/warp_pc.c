/* Save warps (the settings page, port/settings/settings_ui.cpp): continue the
 * game from a .gci save file, as the file select does, without going back to
 * the title. The file's first valid save is loaded into the card's file 4
 * (cardmgr's slot 3): a save made after the warp is written there, never to
 * files 1-3. Runs between frames (VIWaitForRetrace) on a field map or the
 * title, not in a battle or a map change. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/seqdrv.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/online.h"
#include "port/platform.h"
#include "port/practice.h"

BOOL portCardLoadSlotImage(s32 slot, const void* image); /* cardmgr.c */
void portContinueGame(s32 slot);                         /* seq_load.c */

#define GCI_HEADER 0x40
#define WARP_SLOT 3

static char sPending[1024];
static char sStatus[160];
static s32 sSlot = WARP_SLOT; /* the card slot it loads as (quick resume: the player's own file) */

void portWarpRequestSave(const char* path) {
    if (portOnlineActive()) {
        /* online co-op: one game alone can't jump (the session's start is
         * asked for before it begins, online_lobby.cpp) */
        snprintf(sStatus, sizeof(sStatus), "Not during online co-op.");
        return;
    }
    snprintf(sPending, sizeof(sPending), "%s", path);
    sSlot = WARP_SLOT;
    snprintf(sStatus, sizeof(sStatus), "Loading...");
}

/* A save loaded as card slot (0-3) at the next chance: the launcher's
 * Continue (quick resume's save, as the player's own file). */
void portWarpRequestResume(const char* path, int slot) {
    portWarpRequestSave(path);
    if (slot >= 0 && slot <= 3) {
        sSlot = slot;
    }
}

const char* portWarpStatus(void) {
    return sStatus;
}

static void loadSave(const char* path) {
    const char* name = strrchr(path, '/');
    const char* bs = strrchr(path, '\\');
    FILE* f = fopen(path, "rb");
    u8* data;
    long size;
    int i;
    name = bs != NULL && (name == NULL || bs > name) ? bs + 1 : name != NULL ? name + 1 : path;
    if (f == NULL) {
        snprintf(sStatus, sizeof(sStatus), "Cannot open %s", name);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        snprintf(sStatus, sizeof(sStatus), "Cannot read %s", name);
        return;
    }
    fclose(f);
    /* a GCI: the card directory entry (game code first), then the file */
    if (size < GCI_HEADER + (long)PORT_CARD_META_SIZE + (long)PORT_CARD_SLOT_SIZE ||
        memcmp(data, PORT_GAME_ID, 4) != 0) {
        snprintf(sStatus, sizeof(sStatus), "%s: not a %.4s save file (.gci)", name, PORT_GAME_ID);
        free(data);
        return;
    }
    for (i = 0; i < 4; i++) {
        long at = GCI_HEADER + (long)PORT_CARD_META_SIZE + (long)PORT_CARD_SLOT_SIZE * i;
        if (at + (long)PORT_CARD_SLOT_SIZE <= size && portCardLoadSlotImage(sSlot, data + at)) {
            break;
        }
    }
    free(data);
    if (i == 4) {
        snprintf(sStatus, sizeof(sStatus), "%s: no valid save in it", name);
        return;
    }
    printf("[warp] save %s (file slot %d)\n", name, i + 1);
    fflush(stdout);
    portContinueGame(sSlot);
    portPracticeFileLoaded();
    snprintf(sStatus, sizeof(sStatus), "Loaded %s", name);
}

/* Once per retrace: a requested warp, when the game can take it. */
void portWarpFrame(void) {
    static int sEnvRead;
    s32 seq;
    if (!sEnvRead) {
        /* $TTYD_LOAD_SAVE=<file.gci>: a warp requested at start (tests) */
        const char* v = getenv("TTYD_LOAD_SAVE");
        sEnvRead = 1;
        if (v != NULL && v[0] != 0) {
            portWarpRequestSave(v);
            /* $TTYD_LOAD_SAVE_SLOT=0-3: as that save file (the launcher's
             * Continue: quick resume's save, as the player's own file) */
            v = getenv("TTYD_LOAD_SAVE_SLOT");
            if (v != NULL && v[0] >= '0' && v[0] <= '3') {
                sSlot = v[0] - '0';
            }
        }
    }
    if (sPending[0] == '\0' || gp == NULL) {
        return;
    }
    seq = seqGetSeq();
    if (gp->inBattle) {
        snprintf(sStatus, sizeof(sStatus), "Waiting for the battle to end...");
        return;
    }
    if (seq != SEQ_GAME && seq != SEQ_TITLE) {
        return; /* a map change, the file select, ...: shortly */
    }
    {
        char path[sizeof(sPending)];
        memcpy(path, sPending, sizeof(path));
        sPending[0] = '\0';
        loadSave(path);
    }
}
