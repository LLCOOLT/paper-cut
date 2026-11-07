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
#include "port/replay.h"
#include "port/platform.h"
#include "port/practice.h"

BOOL portCardLoadSlotImage(s32 slot, const void* image); /* cardmgr.c */
void portContinueGame(s32 slot);                         /* seq_load.c */
void portCardSlotPlace(s32 slot, char* map, char* bero, int size); /* cardmgr.c */
extern char gPortLoadAtEntrance[16];                     /* seq_mapchange.c */

#define GCI_HEADER 0x40
#define WARP_SLOT 3

static char sPending[1024];
static char sStatus[160];
static s32 sSlot = WARP_SLOT; /* the card slot it loads as (quick resume: the player's own file) */

static void request(const char* path) {
    snprintf(sPending, sizeof(sPending), "%s", path);
    sSlot = WARP_SLOT;
    snprintf(sStatus, sizeof(sStatus), "Loading...");
}

void portWarpRequestSave(const char* path) {
    if (portOnlineActive()) {
        /* online co-op: one game alone can't jump (the session's start is
         * asked for before it begins, online_lobby.cpp) */
        snprintf(sStatus, sizeof(sStatus), "Not during online co-op.");
        return;
    }
    if (portReplayActive()) {
        /* a replay's start save is asked for before it begins (replay.cpp) */
        snprintf(sStatus, sizeof(sStatus), "Not during a replay.");
        return;
    }
    request(path);
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
    f = fopen(sidecar, "r");
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
    /* (Continue of quick resume's own file only: never a replay's or an online session's
       start, which must load exactly as the other game or the recording did) */
    if (sSlot != WARP_SLOT && strncmp(name, "resume_", 7) == 0 && !portReplayActive() && !portOnlineActive() &&
        oldResume(path)) {
        atEntranceIfHouseMap();
    }
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
            request(v); /* (a replay's recording starts from it too) */
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
