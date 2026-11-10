/* Save warps (the settings page, port/settings/settings_ui.cpp): continue the
 * game from a .gci save file, as the file select does, without going back to
 * the title. The file's first valid save is loaded into the card's file 4
 * (cardmgr's slot 3): a save made after the warp is written there, never to
 * files 1-3. Runs between frames (VIWaitForRetrace) on a field map or the
 * title, not in a battle or a map change. */
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/seqdrv.h"
#include "mario/mariost.h"
#include "port/cardsave_pc.h"
#include "port/online_end.h"
#include "port/replay.h"
#include "port/platform.h"
#include "port/practice.h"
#include "port/tr.h"

BOOL portCardLoadSlotImage(s32 slot, const void* image); /* cardmgr.c */
void portContinueGame(s32 slot);                         /* seq_load.c */
void portCardSlotPlace(s32 slot, char* map, char* bero, int size); /* cardmgr.c */
extern char gPortLoadAtEntrance[16];                     /* seq_mapchange.c */
void portSaveStateNotice(const char* text);              /* a notice over the picture (savestate_aurora.c) */

#define GCI_HEADER 0x40
#define WARP_SLOT 3

/* the request: written by the settings page's or launcher's thread, taken by the game's, under sPendingLock
   (a path copied while it was being written was a torn one: "Cannot open") */
static char sPending[1024];
static s32 sPendingSlot = WARP_SLOT;
static int sPendingLock; /* __atomic */
static char sStatus[160];
static s32 sSlot = WARP_SLOT; /* the card slot it loads as (quick resume: the player's own file) */

static void pendingLock(void) {
    while (__atomic_test_and_set(&sPendingLock, __ATOMIC_ACQUIRE)) {
    }
}

static void pendingUnlock(void) {
    __atomic_clear(&sPendingLock, __ATOMIC_RELEASE);
}

/* sStatus (already in the menu language) said over the picture too: the settings page closes on a pick,
   so a warp that can't happen would otherwise say why only on its tab */
static void failed(void) {
    printf("[warp] %s\n", sStatus);
    fflush(stdout);
    portSaveStateNotice(sStatus);
}

static void request(const char* path, s32 slot) {
    pendingLock();
    snprintf(sPending, sizeof(sPending), "%s", path);
    sPendingSlot = slot;
    pendingUnlock();
    snprintf(sStatus, sizeof(sStatus), "%s", portTr("Loading..."));
}

static int requestSave(const char* path, s32 slot) {
    if (portReplayActive()) {
        /* a replay's start save is asked for before it begins (replay.cpp) */
        snprintf(sStatus, sizeof(sStatus), "%s", portTr("Not during a replay."));
        failed();
        return 0;
    }
    request(path, slot);
    return 1;
}

void portWarpRequestSave(const char* path) {
    requestSave(path, WARP_SLOT);
}

/* A save loaded as card slot (0-3) at the next chance: the launcher's
 * Continue (quick resume's save, as the player's own file). */
void portWarpRequestResume(const char* path, int slot) {
    requestSave(path, slot >= 0 && slot <= 3 ? slot : WARP_SLOT);
}

static s32 sOwnSlot = -1; /* a card file to continue from as it is (port/online_end.h) */

void portWarpRequestSlot(int slot) {
    if (slot >= 0 && slot <= 3) {
        sOwnSlot = slot;
    }
}

const char* portWarpStatus(void) {
    return sStatus;
}

/* Quick resume saves from before 0.4.1 could be made inside a house or shop
 * (resume_aurora.c's safe() lets none be made there since): loaded there, the
 * inside starts hidden with its floor off and Mario falls for ever. Their
 * sidecar (resume_<id>.txt) has no "v2" line. */
static int oldResume(const char* path) {
    char sidecar[1024], text[64];
    size_t n = strlen(path);
    FILE* f;
    if (n < 4 || n >= sizeof(sidecar) || strcmp(path + n - 4, ".gci") != 0) {
        return 0;
    }
    memcpy(sidecar, path, n - 4);
    memcpy(sidecar + n - 4, ".txt", 5);
    f = portFopen(sidecar, "r");
    if (f == NULL) {
        return 1;
    }
    n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, "v2") == NULL;
}

/* the field maps with a house or shop the game shows on walking in (evt_door.c's
 * door and house setups, snd_door_in) */
static const char* const kHouseMaps[] = {"aaa_00", "bom_01", "bom_02", "gor_01", "gor_02",
                                         "gor_03", "muj_01", "nok_00", "nok_01", "pik_00",
                                         "pik_04", "tik_00", "tou_01", "usu_00", "usu_01"};

/* On such a map, an old quick resume save puts Mario at the entrance he last
 * came in by (outside every house) instead of where it was made: the map's
 * progress is the same, only his place (seq_mapchange.c's _load). */
static void atEntranceIfHouseMap(void) {
    char map[32], bero[32];
    size_t i;
    portCardSlotPlace(sSlot, map, bero, (int)sizeof(map));
    for (i = 0; i < sizeof(kHouseMaps) / sizeof(kHouseMaps[0]); i++) {
        if (strcmp(map, kHouseMaps[i]) == 0) {
            /* "*": none named (a save loaded since clears it): the map's nearest entrance */
            snprintf(gPortLoadAtEntrance, sizeof(gPortLoadAtEntrance), "%s", bero[0] != '\0' ? bero : "*");
            printf("[warp] quick resume save from before 0.4.1 on %s: Mario at the entrance '%s'\n", map, bero);
            fflush(stdout);
            return;
        }
    }
}

static void loadSave(const char* path) {
    const char* name = strrchr(path, '/');
    const char* bs = strrchr(path, '\\');
    FILE* f = portFopen(path, "rb");
    u8* data;
    long size;
    int i;
    name = bs != NULL && (name == NULL || bs > name) ? bs + 1 : name != NULL ? name + 1 : path;
    if (f == NULL) {
        snprintf(sStatus, sizeof(sStatus), portTr("Cannot open %s"), name);
        failed();
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(size > 0 ? (size_t)size : 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        snprintf(sStatus, sizeof(sStatus), portTr("Cannot read %s"), name);
        failed();
        return;
    }
    fclose(f);
    /* a GCI: the card directory entry (game code first), then the file */
    if (size < GCI_HEADER + (long)PORT_CARD_META_SIZE + (long)PORT_CARD_SLOT_SIZE ||
        memcmp(data, PORT_GAME_ID, 4) != 0) {
        snprintf(sStatus, sizeof(sStatus), portTr("%s: not a %.4s save file (.gci)"), name, PORT_GAME_ID);
        free(data);
        failed();
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
        snprintf(sStatus, sizeof(sStatus), portTr("%s: no valid save in it"), name);
        failed();
        return;
    }
    printf("[warp] save %s (file slot %d)\n", name, i + 1);
    fflush(stdout);
    /* (Continue of quick resume's own file only: never a replay's start, which must load exactly
       as the recording did) */
    if (sSlot != WARP_SLOT && strncmp(name, "resume_", 7) == 0 && !portReplayActive() &&
        oldResume(path)) {
        atEntranceIfHouseMap();
    }
    portContinueGame(sSlot);
    portPracticeFileLoaded();
    snprintf(sStatus, sizeof(sStatus), portTr("Loaded %s"), name);
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
            /* $TTYD_LOAD_SAVE_SLOT=0-3: as that save file (the launcher's
             * Continue: quick resume's save, as the player's own file) */
            const char* s = getenv("TTYD_LOAD_SAVE_SLOT");
            /* (a replay's recording starts from it too) */
            request(v, s != NULL && s[0] >= '0' && s[0] <= '3' ? s[0] - '0' : WARP_SLOT);
        }
    }
    if (sOwnSlot >= 0 && gp != NULL && !gp->inBattle &&
        (seqGetSeq() == SEQ_GAME || seqGetSeq() == SEQ_TITLE)) {
        /* (online: the player's own file again, as it is on the card: online_end_pc.c) */
        const s32 slot = sOwnSlot;
        sOwnSlot = -1;
        printf("[warp] the player's own file %d again\n", (int)slot + 1);
        fflush(stdout);
        portContinueGame(slot);
        portPracticeFileLoaded(); /* (its records, and the host's world gone from memory: statesync.h) */
        return;
    }
    if (__atomic_load_n(&sPending[0], __ATOMIC_ACQUIRE) == '\0' || gp == NULL) {
        return; /* (a first look without the lock: taken under it below) */
    }
    seq = seqGetSeq();
    if (gp->inBattle) {
        if (strcmp(sStatus, portTr("Waiting for the battle to end...")) != 0) {
            snprintf(sStatus, sizeof(sStatus), "%s", portTr("Waiting for the battle to end..."));
            portSaveStateNotice(sStatus); /* (once: the page has closed) */
        }
        return;
    }
    if (seq != SEQ_GAME && seq != SEQ_TITLE) {
        return; /* a map change, the file select, ...: shortly */
    }
    {
        char path[sizeof(sPending)];
        pendingLock();
        memcpy(path, sPending, sizeof(path));
        sSlot = sPendingSlot;
        sPending[0] = '\0';
        pendingUnlock();
        if (path[0] != '\0') {
            loadSave(path);
        }
    }
}
