/* Area maps (port/include/port/area_maps.h): the rooms a save file has been
 * in and the exits it has taken, kept beside the memory cards as the
 * achievements are, and an area's map built from the generated tables. */
#include "port/file_pc.h"
#include "port/statesync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "port/file_write.h"
#include "port/area_maps.h"
#include "port/companion.h"
#include "port/gameplus.h"
#include "port/practice.h"
#include "port/tr.h"

#define ROOMS_MAX 256
#define EXITS_MAX 1024

static unsigned char sVisited[ROOMS_MAX];
static unsigned char sTaken[EXITS_MAX];
/* the same, as Mario only (not Peach's or Bowser's interludes): fast travel's
 * (port/include/port/fast_travel.h) rooms and ways in, "m <room>" and "w <room> <exit>" */
static unsigned char sMine[ROOMS_MAX];
static unsigned char sMineWay[EXITS_MAX];
static char sPath[1024]; /* the file's record, "" with no save file */
static char sFile[128];
static char sName[32];
static int sLast = -1;   /* the room Mario was last in (this run) */
static int sDirty;

static int roomIndex(const char* map) {
    int i;
    if (map == NULL || map[0] == '\0') {
        return -1;
    }
    for (i = 0; i < portAreaRoomCount && i < ROOMS_MAX; i++) {
        if (strncmp(portAreaRooms[i].map, map, 6) == 0) {
            return i;
        }
    }
    return -1;
}

/* the record as of the save file's last save (file<N>.saved.txt): what a load of the file reads,
   so rooms seen after it (then a Game Over, or a reset) don't count for the file */
static void savedPath(char* out, size_t size, int slot) {
    snprintf(out, size, "%s/AreaMaps/%s/file%d.saved.txt", portPracticeUserDir(), PORT_GAME_ID, slot + 1);
}

static void writeRecord(const char* path) {
    FILE* f;
    int i;
    if (path[0] == '\0') {
        return;
    }
    portPracticeGameDir("AreaMaps");
    f = portFileWriteOpen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "name=%s\n", sName);
    for (i = 0; i < portAreaRoomCount && i < ROOMS_MAX; i++) {
        if (sVisited[i]) {
            fprintf(f, "v %s\n", portAreaRooms[i].map);
        }
    }
    for (i = 0; i < portAreaExitCount && i < EXITS_MAX; i++) {
        if (sTaken[i]) {
            fprintf(f, "e %s %s\n", portAreaRooms[portAreaExits[i].room].map, portAreaExits[i].hit);
        }
    }
    for (i = 0; i < portAreaRoomCount && i < ROOMS_MAX; i++) {
        if (sMine[i]) {
            fprintf(f, "m %s\n", portAreaRooms[i].map);
        }
    }
    for (i = 0; i < portAreaExitCount && i < EXITS_MAX; i++) {
        if (sMineWay[i]) {
            fprintf(f, "w %s %s\n", portAreaRooms[portAreaExits[i].room].map, portAreaExits[i].hit);
        }
    }
    portFileWriteClose(f, path);
}

static void save(void) {
    writeRecord(sPath);
}

void portAreaMapsCardWrite(int slot) {
    char path[1024];
    if (slot < 0 || slot > 3 || sPath[0] == '\0') {
        return;
    }
    savedPath(path, sizeof(path), slot);
    writeRecord(path);
}

void portAreaMapsCardCopy(int from, int to) {
    char a[1024], b[1024], buf[4096];
    FILE *in, *out;
    size_t n;
    if (from < 0 || from > 3 || to < 0 || to > 3 || from == to) {
        return;
    }
    savedPath(a, sizeof(a), from);
    savedPath(b, sizeof(b), to);
    portRemove(b);
    if ((in = portFopen(a, "rb")) == NULL) {
        return;
    }
    if ((out = portFileWriteOpen(b, "wb")) != NULL) {
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            fwrite(buf, 1, n, out);
        }
        portFileWriteClose(out, b);
    }
    fclose(in);
}

void portAreaMapsCardErase(int slot) {
    char path[1024];
    if (slot >= 0 && slot <= 3) {
        savedPath(path, sizeof(path), slot);
        portRemove(path);
    }
}

static void readRecord(FILE* f, int anyName);

void portAreaMapsFileLoaded(void) {
    char saved[1024];
    FILE* f;
    memset(sVisited, 0, sizeof(sVisited));
    memset(sTaken, 0, sizeof(sTaken));
    memset(sMine, 0, sizeof(sMine));
    memset(sMineWay, 0, sizeof(sMineWay));
    sLast = -1;
    if (gp == NULL || gp->lastSlot < 0 || gp->lastSlot > 3) {
        sPath[0] = '\0';
        return;
    }
    snprintf(sName, sizeof(sName), "%.16s", (const char*)gp->playerName);
    snprintf(sPath, sizeof(sPath), "%s/AreaMaps/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID,
             (int)gp->lastSlot + 1);
    snprintf(sFile, sizeof(sFile), portTr("File %d (%s)"), (int)gp->lastSlot + 1, sName);
    if (swByteGet(GSW_STORY_PROGRESS) == 0) {
        /* a new game in the file (story 0: the file select's new game, as the speedrun timer's):
           its map starts again, whatever the file's last game had seen under the same name */
        sDirty = 1;
        return;
    }
    savedPath(saved, sizeof(saved), (int)gp->lastSlot);
    f = portFopen(saved, "r");
    if (f == NULL) {
        f = portFopen(sPath, "r"); /* (a record from before the saved copies: the file's whole record) */
    }
    if (f != NULL) {
        readRecord(f, 0);
    }
}

/* the record in f into the tables (cleared before); anyName: whatever its name (tests) */
static void readRecord(FILE* f, int anyName) {
    char line[128];
    int sameName = anyName, mine = 0, i;
    while (fgets(line, sizeof(line), f) != NULL) {
        char a[32], b[32];
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "name=", 5) == 0) {
            sameName = anyName || strcmp(line + 5, sName) == 0;
            continue;
        }
        if (!sameName) {
            continue; /* another game begun in the file: its map starts again */
        }
        if ((line[0] == 'v' || line[0] == 'm') && sscanf(line + 1, " %31s", a) == 1) {
            const int r = roomIndex(a);
            if (r >= 0) {
                (line[0] == 'v' ? sVisited : sMine)[r] = 1;
            }
            mine |= line[0] == 'm';
        } else if ((line[0] == 'e' || line[0] == 'w') && sscanf(line + 1, " %31s %31s", a, b) == 2) {
            const int r = roomIndex(a);
            for (i = 0; r >= 0 && i < portAreaExitCount && i < EXITS_MAX; i++) {
                if (portAreaExits[i].room == r && strcmp(portAreaExits[i].hit, b) == 0) {
                    (line[0] == 'e' ? sTaken : sMineWay)[i] = 1;
                }
            }
            mine |= line[0] == 'w';
        }
    }
    fclose(f);
    if (!mine) {
        /* a record from before Mario's own (0.4): its rooms and ways are his, but for the
         * X-Naut Fortress, where only Peach goes before chapter 7 */
        for (i = 0; i < portAreaRoomCount && i < ROOMS_MAX; i++) {
            sMine[i] = sVisited[i] && strcmp(portAreaRooms[i].area, "aji") != 0;
        }
        for (i = 0; i < portAreaExitCount && i < EXITS_MAX; i++) {
            sMineWay[i] = sTaken[i] && sMine[portAreaExits[i].room];
        }
    }
}

/* The areas' names as the game's Journal has them (menu_monosiri_<area>), in
 * UTF-8, read on the game's frame as each room comes in (the language may
 * have changed); "" where the game has none. */
#define AREAS_MAX 32
static char sAreaNames[AREAS_MAX][48];

static void readAreaNames(void) {
    int i;
    for (i = 0; i < AREAS_MAX && i < portAreaCount(); i++) {
        char tag[32];
        const char* text;
        snprintf(tag, sizeof(tag), "menu_monosiri_%s", portAreaAt(i));
        text = portMsgHas(tag) ? msgSearch(tag) : NULL; /* (the areas with no name: not a missing message) */
        if (text == NULL) {
            sAreaNames[i][0] = '\0';
        } else {
            portCompanionUtf8(text, sAreaNames[i], sizeof(sAreaNames[i]), 0);
        }
    }
}

const char* portAreaName(const char* area) {
    int i;
    for (i = 0; area != NULL && i < AREAS_MAX && i < portAreaCount(); i++) {
        if (strncmp(portAreaAt(i), area, 3) == 0) {
            return sAreaNames[i][0] != '\0' ? sAreaNames[i] : NULL;
        }
    }
    return NULL;
}

/* Mario went from room a to room b: every exit between them taken, both ways
 * (mine: as Mario himself). */
static void walked(int a, int b, int mine) {
    int i;
    for (i = 0; i < portAreaExitCount && i < EXITS_MAX; i++) {
        const PortAreaExit* e = &portAreaExits[i];
        const int to = e->toRoom >= 0 ? e->toRoom : roomIndex(e->to);
        if ((e->room == a && to == b) || (e->room == b && to == a)) {
            if (!sTaken[i] || (mine && !sMineWay[i])) {
                sTaken[i] = 1;
                sMineWay[i] |= (unsigned char)mine;
                sDirty = 1;
            }
        }
    }
}

int portAreaRoomFind(const char* map) {
    return roomIndex(map);
}

int portAreaMineVisited(int room) {
    return room >= 0 && room < portAreaRoomCount && room < ROOMS_MAX && sMine[room];
}

int portAreaMineWay(int exit) {
    return exit >= 0 && exit < portAreaExitCount && exit < EXITS_MAX && sMineWay[exit];
}

void portAreaMapsJumped(void) {
    sLast = -1; /* (the next room isn't reached through an exit of this one) */
}

void portAreaMapsMarkAll(void) {
    memset(sMine, 1, sizeof(sMine));
    memset(sMineWay, 1, sizeof(sMineWay));
}

void portAreaMapsFrame(void) {
    static unsigned int sTick;
    static int sTestRecord;
    int r;
    if (gp == NULL || seqGetSeq() != SEQ_GAME || pouchGetPtr() == NULL) {
        return;
    }
    if (portStateSyncCopyHeld()) {
        return; /* (a co-op guest in the host's world: its rooms are the host's adventure, not this file's) */
    }
    if (!sTestRecord && sPath[0] == '\0' && getenv("TTYD_AREA_RECORD") != NULL) {
        /* tests: a record (as the user folder keeps one) for a game begun without a save file */
        FILE* f = portFopen(getenv("TTYD_AREA_RECORD"), "r");
        sTestRecord = 1;
        if (f != NULL) {
            readRecord(f, 1);
            printf("[areamaps] record %s read\n", getenv("TTYD_AREA_RECORD"));
            fflush(stdout);
        }
    }
    r = roomIndex(gp->currentMapName);
    if (r >= 0 && r != sLast) {
        const MarioWork* mp = marioGetPtr();
        const int mine = mp != NULL && mp->characterId == 0; /* (not Peach, not Bowser) */
        if (!sVisited[r] || (mine && !sMine[r])) {
            sVisited[r] = 1;
            sMine[r] |= (unsigned char)mine;
            sDirty = 1;
        }
        if (sLast >= 0) {
            walked(sLast, r, mine);
        }
        sLast = r;
        readAreaNames();
    } else if (r < 0) {
        sLast = -1; /* a room off the tables (a mode's, a cutscene's): no exit taken from it */
    }
    if (sDirty && ++sTick % 60 == 0) {
        sDirty = 0;
        save();
    }
}

/* --- the areas ----------------------------------------------------------------- */

int portAreaCount(void) {
    int i, n = 0;
    for (i = 0; i < portAreaRoomCount; i++) {
        n += i == 0 || strcmp(portAreaRooms[i].area, portAreaRooms[i - 1].area) != 0;
    }
    return n;
}

const char* portAreaAt(int i) {
    int k, n = -1;
    for (k = 0; k < portAreaRoomCount; k++) {
        if (k == 0 || strcmp(portAreaRooms[k].area, portAreaRooms[k - 1].area) != 0) {
            if (++n == i) {
                return portAreaRooms[k].area;
            }
        }
    }
    return "";
}

const char* portAreaHere(void) {
    const int r = gp != NULL ? roomIndex(gp->currentMapName) : -1;
    return r >= 0 ? portAreaRooms[r].area : "";
}

const char* portAreaMapsFile(void) {
    return sPath[0] != '\0' ? sFile : "";
}

int portAreaMapsVisited(int* total) {
    int i, n = 0;
    *total = portAreaRoomCount;
    for (i = 0; i < portAreaRoomCount && i < ROOMS_MAX; i++) {
        n += sVisited[i];
    }
    return n;
}

int portAreaMapBuild(const char* area, PortAreaView* out) {
    int i, here, inGame;
    int index[ROOMS_MAX];
    memset(out, 0, sizeof(*out));
    if (area == NULL || area[0] == '\0') {
        area = portAreaHere();
    }
    if (area[0] == '\0') {
        return 0;
    }
    snprintf(out->area, sizeof(out->area), "%.3s", area);
    inGame = gp != NULL && pouchGetPtr() != NULL &&
             (seqGetSeq() == SEQ_GAME || seqGetSeq() == SEQ_BATTLE || seqGetSeq() == SEQ_MAP_CHANGE);
    here = inGame ? roomIndex(gp->currentMapName) : -1;
    for (i = 0; i < portAreaRoomCount && i < ROOMS_MAX; i++) {
        const PortAreaRoom* r = &portAreaRooms[i];
        index[i] = -1;
        if (strcmp(r->area, out->area) != 0 || out->roomCount >= PORT_AREA_VIEW_ROOMS) {
            continue;
        }
        {
            PortAreaViewRoom* v = &out->rooms[out->roomCount];
            int pieces = 0, piecesTotal = 0, sprites = 0, spritesTotal = 0;
            index[i] = out->roomCount++;
            snprintf(v->map, sizeof(v->map), "%s", r->map);
            v->x = r->x;
            v->y = r->y;
            v->visited = sVisited[i];
            v->here = i == here;
            if (inGame) {
                portCollectiblesForMap(r->map, &pieces, &piecesTotal, &sprites, &spritesTotal);
            }
            v->pieces = piecesTotal - pieces;
            v->sprites = spritesTotal - sprites;
            out->pieces += v->pieces;
            out->sprites += v->sprites;
            out->visited += v->visited;
            out->width = r->x + 1 > out->width ? r->x + 1 : out->width;
            out->height = r->y + 1 > out->height ? r->y + 1 : out->height;
        }
    }
    for (i = 0; i < portAreaExitCount && i < EXITS_MAX && out->linkCount < PORT_AREA_VIEW_LINKS; i++) {
        const PortAreaExit* e = &portAreaExits[i];
        const int a = e->room < ROOMS_MAX ? index[e->room] : -1;
        const int toRoom = e->toRoom >= 0 ? e->toRoom : roomIndex(e->to);
        int b, k, dup = -1;
        if (a < 0) {
            continue;
        }
        if (!sTaken[i]) {
            out->rooms[a].waysUntaken++;
        }
        b = toRoom >= 0 && toRoom < ROOMS_MAX ? index[toRoom] : -1;
        if (b == a) {
            continue;
        }
        /* one line per pair of rooms (the two ends' exits are one way through) */
        for (k = 0; k < out->linkCount; k++) {
            const PortAreaViewLink* l = &out->links[k];
            if (b >= 0 && ((l->a == a && l->b == b) || (l->a == b && l->b == a))) {
                dup = k;
                break;
            }
            if (b < 0 && l->b < 0 && l->a == a && strncmp(l->to, e->to, 6) == 0) {
                dup = k;
                break;
            }
        }
        if (dup >= 0) {
            out->links[dup].taken |= sTaken[i];
            continue;
        }
        {
            PortAreaViewLink* l = &out->links[out->linkCount++];
            l->a = a;
            l->b = b;
            l->dir = e->dir;
            l->taken = sTaken[i];
            snprintf(l->to, sizeof(l->to), "%.6s", e->to);
        }
    }
    return 1;
}
