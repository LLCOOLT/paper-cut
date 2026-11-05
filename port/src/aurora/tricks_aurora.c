/* Trick practice (port/include/port/playmodes.h): warps to set-ups, a room
 * with the story where it is for a known trick or fight, and the player's
 * own spots (a room, the story, Mario's place), kept in <user>/tricks.txt:
 *
 *   name|map|entrance|story|x y z     (the entrance and the place may be empty)
 *
 * A warp sets the story (GSW 0), changes map and, for a spot, puts Mario
 * where it was saved once the room is in. It counts as practice: no
 * achievements until a file is loaded (port/include/port/practice.h). Not
 * during a replay or online co-op. */
#include "port/playmodes.h"
#include "port/modes.h"
#include "port/online.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/settings.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Trick {
    char name[64];
    char map[16];
    char bero[16];
    int story;
    int hasPos;
    float x, y, z;
} Trick;

/* built in: rooms with the story at a known set-up (the decomp's story names) */
static const struct {
    const char* map;
    int story;
} kBuiltin[] = {
    {"hei_01", STORY_STG1_01_THE_FLIP_BRIDGE},     {"gon_01", STORY_STG1_09_HOOKTAIL_CASTLE},
    {"gon_04", STORY_STG1_14_PAPER_BRIDGE_RIGHT},  {"gon_11", STORY_STG1_16_HOOKTAIL},
    {"gor_03", STORY_STG2_46_BLIMP_TICKET},        {"tou_03", STORY_STG3_01_ARENA_INTRO},
    {"jin_04", STORY_STG4_10_DOOPLISS_BATTLE},     {"muj_04", STORY_STG5_13_BOBBERY_AND_THE_BRIDGE},
    {"eki_00", STORY_STG6_39_SMORG_1ST_ENCOUNTER}, {"rsh_05_a", STORY_STG6_47_ESCAPING_THE_SMORGS},
    {"las_09", STORY_STG8_03_THE_PALACE_GARDEN},   {"las_26", STORY_STG8_04_GLOOMTAIL},
};
#define BUILTIN ((int)(sizeof(kBuiltin) / sizeof(kBuiltin[0])))
#define TRICK_MAX 128

static Trick sTricks[TRICK_MAX];
static int sCount, sRead;
static Trick sWarp;            /* the warp asked for */
static int sWarpAsked;         /* __atomic */
static int sPlaceWaiting;      /* the room is loading: Mario's place to set once it's in */
static int sPlaceFrames;
static char sSaveName[64];
static int sSaveAsked;         /* __atomic */

static const char* chapterOf(int story) {
    static const char* const kNames[] = {"Prologue",  "Chapter 1", "Chapter 2", "Chapter 3", "Chapter 4",
                                         "Chapter 5", "Chapter 6", "Chapter 7", "Chapter 8"};
    static const int kStarts[] = {0,
                                  STORY_STG1_00_PETAL_MEADOWS,
                                  STORY_STG2_00_BOGGLY_WOODS,
                                  STORY_STG3_00_WELCOME_TO_GLITZVILLE,
                                  STORY_STG4_00_WELCOME_TO_STAGE_4,
                                  STORY_STG5_00_LOGBOOK,
                                  STORY_STG6_00_START,
                                  STORY_STG7_00_START,
                                  STORY_STG8_00_START};
    int i = 8;
    while (i > 0 && story < kStarts[i]) {
        i--;
    }
    return kNames[i];
}

static void path(char* out, size_t size) {
    snprintf(out, size, "%s/tricks.txt", portSettingsUserPath());
}

static void readAll(void) {
    char p[1024], line[256];
    FILE* f;
    int i;
    sCount = 0;
    for (i = 0; i < BUILTIN; i++) {
        Trick* t = &sTricks[sCount++];
        const PortNamedValue* v = portStoryName(kBuiltin[i].story);
        memset(t, 0, sizeof(*t));
        snprintf(t->name, sizeof(t->name), "%s: %s (%s)", chapterOf(kBuiltin[i].story),
                 v != NULL ? v->note : "", kBuiltin[i].map);
        snprintf(t->map, sizeof(t->map), "%s", kBuiltin[i].map);
        t->story = kBuiltin[i].story;
    }
    path(p, sizeof(p));
    f = fopen(p, "r");
    if (f != NULL) {
        while (sCount < TRICK_MAX && fgets(line, sizeof(line), f) != NULL) {
            Trick t;
            char* field[5] = {0};
            char* at = line;
            int n = 0;
            if (line[0] == '#') {
                continue;
            }
            line[strcspn(line, "\r\n")] = '\0';
            while (n < 5) {
                field[n++] = at;
                at = strchr(at, '|');
                if (at == NULL) {
                    break;
                }
                *at++ = '\0';
            }
            if (n < 4 || field[1][0] == '\0') {
                continue;
            }
            memset(&t, 0, sizeof(t));
            snprintf(t.name, sizeof(t.name), "%s", field[0]);
            snprintf(t.map, sizeof(t.map), "%s", field[1]);
            snprintf(t.bero, sizeof(t.bero), "%s", field[2]);
            t.story = atoi(field[3]);
            t.hasPos = n == 5 && sscanf(field[4], "%f %f %f", &t.x, &t.y, &t.z) == 3;
            sTricks[sCount++] = t;
        }
        fclose(f);
    }
    sRead = 1;
}

int portTrickCount(void) {
    if (!sRead) {
        readAll();
    }
    return sCount;
}

const char* portTrickName(int i) {
    return i >= 0 && i < portTrickCount() ? sTricks[i].name : "";
}

int portTrickBuiltin(int i) {
    return i >= 0 && i < BUILTIN;
}

void portTricksReload(void) {
    sRead = 0;
}

static int canWarp(const char** why) {
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        *why = "Warp in the field, with a file loaded.";
        return 0;
    }
    if (portReplayActive() || portOnlineActive()) {
        *why = "Not during a replay or online co-op.";
        return 0;
    }
    if (portChapterActive() || portPitCoopActive() ||
        (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished)) {
        *why = "Not while a mode runs.";
        return 0;
    }
    return 1;
}

int portTrickWarp(int i, const char** why) {
    if (i < 0 || i >= portTrickCount()) {
        *why = "No such set-up.";
        return 0;
    }
    if (!canWarp(why)) {
        return 0;
    }
    sWarp = sTricks[i];
    __atomic_store_n(&sWarpAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

int portTrickSaveHere(const char* name, const char** why) {
    if (!canWarp(why)) {
        return 0;
    }
    snprintf(sSaveName, sizeof(sSaveName), "%s", name != NULL && name[0] != '\0' ? name : "My spot");
    __atomic_store_n(&sSaveAsked, 1, __ATOMIC_SEQ_CST);
    return 1;
}

void portTricksFrame(void) {
    const SequenceType seq = seqGetSeq();
    if (__atomic_exchange_n(&sSaveAsked, 0, __ATOMIC_SEQ_CST) && seq == SEQ_GAME && gp != NULL) {
        char p[1024];
        FILE* f;
        const MarioWork* mw = marioGetPtr();
        char* bar;
        while ((bar = strchr(sSaveName, '|')) != NULL) {
            *bar = '/';
        }
        path(p, sizeof(p));
        f = fopen(p, "a");
        if (f != NULL) {
            fprintf(f, "%s|%s|%s|%d|%.2f %.2f %.2f\n", sSaveName, gp->currentMapName, "",
                    (int)swByteGet(GSW_STORY_PROGRESS), mw->position.x, mw->position.y, mw->position.z);
            fclose(f);
        }
        sRead = 0;
    }
    if (__atomic_exchange_n(&sWarpAsked, 0, __ATOMIC_SEQ_CST) && seq == SEQ_GAME && gp != NULL && !gp->inBattle) {
        printf("[tricks] %s: %s, story %d\n", sWarp.name, sWarp.map, sWarp.story);
        fflush(stdout);
        swByteSet(GSW_STORY_PROGRESS, sWarp.story);
        portPracticeTaint();
        sPlaceWaiting = sWarp.hasPos ? 2 : 0;
        seqSetSeq(SEQ_MAP_CHANGE, sWarp.map, sWarp.bero[0] != '\0' ? sWarp.bero : NULL);
        return;
    }
    /* the room is in: Mario where the spot was saved */
    if (sPlaceWaiting == 2 && seq == SEQ_MAP_CHANGE) {
        sPlaceWaiting = 1;
    } else if (sPlaceWaiting == 1 && seq == SEQ_GAME && gp != NULL && strcmp(gp->currentMapName, sWarp.map) == 0 &&
               ++sPlaceFrames >= 20) { /* past the entrance's walk in */
        MarioWork* mw = marioGetPtr();
        sPlaceFrames = 0;
        mw->position.x = sWarp.x;
        mw->position.y = sWarp.y;
        mw->position.z = sWarp.z;
        sPlaceWaiting = 0;
    }
}
