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
#include "port/platform.h"

BOOL portCardLoadSlotImage(s32 slot, const void* image); /* cardmgr.c */
void portContinueGame(s32 slot);                         /* seq_load.c */

#define GCI_HEADER 0x40
#define WARP_SLOT 3

static char sPending[1024];
static char sStatus[160];

void portWarpRequestSave(const char* path) {
    snprintf(sPending, sizeof(sPending), "%s", path);
    snprintf(sStatus, sizeof(sStatus), "Loading...");
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
        if (at + (long)PORT_CARD_SLOT_SIZE <= size && portCardLoadSlotImage(WARP_SLOT, data + at)) {
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
    portContinueGame(WARP_SLOT);
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
