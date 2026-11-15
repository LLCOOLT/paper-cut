/* Special places (port/include/port/special_places.h, GitHub #48): Mario's house and Bowser's places,
 * visited from the Journal's map with fast travel on; the save words kept and put back. */
#include <stdio.h>
#include <string.h>
#include "port/area_maps.h"
#include "port/fast_travel.h"
#include "port/menu_tap.h"
#include "port/replay.h"
#include "port/special_places.h"
#include "port/statesync.h"
#include "port/toggles.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_cmd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "party.h"
#include <dolphin/pad.h>
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"

/* each place: its rooms (the first is where the trip goes, by way in; the rest are rooms its own
 * scripts go on to: the stage's second half, a stage again after a fall), the story value by which
 * the player has seen it, and whether it is Bowser's (its scripts make the player Bowser) */
static const struct {
    const char* name;
    const char* rooms[3];
    const char* way;
    int seen;
    int bowser;
    int listed; /* on the list: 0 for a place a visit can't yet play */
} kPlaces[] = {
    {"Mario's house", {"aaa_00"}, "dokan_1", STORY_STG0_02_MARIOS_HOUSE, 0, 1},
    /* Bowser's places are off the list for 0.5.1: on arrival the player had no control (his hall) or the stage
       didn't play; the code stays for a later fix */
    {"Bowser's Castle", {"kpa_01"}, "", STORY_STG1_19_02_BOWSER_CASTLE_INTERIOR, 1, 0},
    {"Bowser's stage 1", {"kpa_02"}, "", STORY_STG2_25_04_BOWSER_ACTION_STAGE, 1, 0},
    {"Bowser's stage 2", {"kpa_03", "kpa_04"}, "", STORY_STG4_15_07_BOWSER_ACTION_STAGE, 1, 0},
    {"Bowser's stage 3", {"kpa_05", "kpa_06"}, "", STORY_STG6_55_03_BOWSER_ACTION_STAGE, 1, 0},
};
#define PLACES ((int)(sizeof(kPlaces) / sizeof(kPlaces[0])))

static int sVisit = -1;          /* the place visited, -1 none */
static int sAsk = -1;            /* a trip asked for (a place), -1 none */
static int sAskBack;             /* the way back asked for */
static int sGoing;               /* our own map change into a place is under way */
static int sExplicit;            /* the next map change is the player's own trip on (fast travel, a warp) */
static int sCallParty;           /* out of Bowser's place: the partner called back once in the field */
static int sModeBack;            /* out of Bowser's place: Mario again once the next room runs */
static char sRetMap[16], sRetHit[32];
/* the save words when the visit began */
static s32 sGsw0;
static u32 sGswf[sizeof(((GlobalWork*)0)->gswf) / sizeof(u32)];
static u8 sGsw[sizeof(((GlobalWork*)0)->gsw)];

static unsigned wordsSum(void) {
    unsigned h = 2166136261u;
    size_t i;
    const u8* p = (const u8*)gp->gswf;
    for (i = 0; i < sizeof(gp->gswf); i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    for (i = 0; i < sizeof(gp->gsw); i++) {
        h = (h ^ gp->gsw[i]) * 16777619u;
    }
    return (h ^ (unsigned)gp->gsw0) * 16777619u;
}

static unsigned sSum; /* wordsSum() when the visit began */

static int placeOf(const char* map) {
    int i, k;
    for (i = 0; map != NULL && i < PLACES; i++) {
        for (k = 0; k < 3 && kPlaces[i].rooms[k] != NULL; k++) {
            if (strcmp(map, kPlaces[i].rooms[k]) == 0) {
                return i;
            }
        }
    }
    return -1;
}

int portSpecialCount(void) {
    return PLACES;
}

const char* portSpecialName(int i) {
    return i >= 0 && i < PLACES ? kPlaces[i].name : "";
}

int portSpecialSeen(int i) {
    return i >= 0 && i < PLACES && kPlaces[i].listed && gp != NULL && swByteGet(GSW_STORY_PROGRESS) >= kPlaces[i].seen;
}

int portSpecialVisiting(void) {
    return sVisit;
}

int portSpecialRoom(const char* map) {
    return placeOf(map) >= 0;
}

const char* portSpecialRefusal(int i) {
    const char* why;
    if (i < 0 || i >= PLACES) {
        return "Mario hasn't been here yet.";
    }
    if (portConsoleAccurate()) {
        return "Off in Console-accurate mode.";
    }
    if (!portTravelOn()) {
        return "Fast travel is off (Settings, Enhancements).";
    }
    if (portStateSyncOn() || portReplayActive()) {
        return "Only in the field, and not online or in a replay."; /* (as the warp map) */
    }
    if (!portSpecialSeen(i)) {
        return "Not seen in the story yet.";
    }
    why = portTravelDepartRefusal();
    if (why != NULL) {
        return why;
    }
    if (gp != NULL && placeOf(gp->currentMapName) == i) {
        return "Mario is here.";
    }
    return NULL;
}

const char* portSpecialRequest(int i) {
    const char* why = portSpecialRefusal(i);
    if (why != NULL) {
        printf("[special] refused: %s: %s\n", portSpecialName(i), why);
        fflush(stdout);
        return why;
    }
    sAsk = i;
    sAskBack = 0;
    printf("[special] asked: %s\n", kPlaces[i].name);
    fflush(stdout);
    return NULL;
}

const char* portSpecialLeave(void) {
    if (sVisit < 0) {
        return "Mario hasn't been here yet.";
    }
    sAskBack = 1;
    sAsk = -1;
    printf("[special] back asked: %s %s\n", sRetMap, sRetHit);
    fflush(stdout);
    return NULL;
}

void portSpecialLeaving(void) {
    sExplicit = 1;
}

static int fieldFree(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && marioStGetSystemLevel() == 0 && marioChkKey() &&
           marioChkCtrl();
}

/* the visit over: the save words as they were, the player Mario (or the pick) again */
static void visitEnd(int fieldNow) {
    const unsigned now = wordsSum();
    printf("[special] visit over: %s; save words %s\n", kPlaces[sVisit].name,
           now == sSum ? "unchanged" : "changed during the visit, put back");
    gp->gsw0 = sGsw0;
    memcpy(gp->gswf, sGswf, sizeof(gp->gswf));
    memcpy(gp->gsw, sGsw, sizeof(gp->gsw));
    if (kPlaces[sVisit].bowser && fieldNow) {
        /* Mario (or the pick) again once the next room is up: as the story's own rooms after a stage do (gor_00
           after Bowser's second), never in the stage itself, whose scripts run until it is gone */
        sModeBack = 1;
        sCallParty = 1;
    }
    printf("[special] home: save words %s\n", wordsSum() == sSum ? "as before the visit" : "DIFFER");
    fflush(stdout);
    sVisit = -1;
}

void portSpecialMapChange(const char** map, const char** bero) {
    const int to = map != NULL ? placeOf(*map) : -1;
    if (sGoing) {
        sGoing = 0;
        sExplicit = 0;
        return;
    }
    if (sVisit < 0 || (to == sVisit && *map != NULL)) {
        sExplicit = 0;
        return; /* (no visit, or a room of the same place: the stage's second half, the stage again) */
    }
    if (!sExplicit && gp != NULL && strncmp(gp->currentMapName, "kpa_", 4) == 0) {
        /* Bowser's own way out (the goal's, or the castle's scene's): back where the trip began */
        printf("[special] %s's way out (%s) goes back: %s %s\n", gp->currentMapName, *map != NULL ? *map : "",
               sRetMap, sRetHit);
        *map = sRetMap;
        *bero = sRetHit[0] != '\0' ? sRetHit : NULL;
    }
    sExplicit = 0;
    visitEnd(1);
}

void portSpecialSeq(int seq) {
    if (sVisit >= 0 && (seq == SEQ_TITLE || seq == SEQ_LOAD)) {
        visitEnd(0); /* (a reset or a load: the file's own words come with it anyway) */
    }
    if (seq == SEQ_TITLE || seq == SEQ_LOAD) {
        sAsk = -1;
        sAskBack = 0;
        sModeBack = 0;
        sCallParty = 0;
    }
}

static void goBack(void) {
    if (sVisit < 0) {
        return;
    }
    printf("[special] back: %s %s\n", sRetMap, sRetHit);
    fflush(stdout);
    portAreaMapsJumped();
    seqSetSeq(SEQ_MAP_CHANGE, sRetMap, sRetHit[0] != '\0' ? sRetHit : NULL);
}

void portSpecialFrame(void) {
    if (sModeBack && gp != NULL && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !portSpecialRoom(gp->currentMapName)) {
        MarioWork* mp = marioGetPtr();
        sModeBack = 0;
        if (mp != NULL && mp->characterId == 2) {
            printf("[special] Mario again\n");
            fflush(stdout);
            marioSetCharMode(0);
        }
    }
    if (sCallParty && !sModeBack && fieldFree() && !portSpecialRoom(gp->currentMapName)) {
        MarioWork* mp = marioGetPtr();
        sCallParty = 0;
        if (mp != NULL && mp->characterId == 0 && mp->currPartySlotId[0] < 0 && mp->prevPartyMemberId[0] > 0) {
            printf("[special] partner %d called back\n", mp->prevPartyMemberId[0]);
            fflush(stdout);
            marioPartyEntry((PartyId)mp->prevPartyMemberId[0]);
        }
    }
    if (sAsk < 0 && !sAskBack) {
        return;
    }
    if (gp == NULL || (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_MAP_CHANGE && seqGetSeq() != SEQ_BATTLE)) {
        sAsk = -1; /* the title, a Game Over: dropped */
        sAskBack = 0;
        return;
    }
    if (!fieldFree()) {
        return; /* a scene, a menu, the stage's start: it waits */
    }
    if (sAskBack) {
        sAskBack = 0;
        goBack();
        return;
    }
    {
        const int i = sAsk;
        const char* why = portSpecialRefusal(i);
        sAsk = -1;
        if (why != NULL) {
            printf("[special] dropped: %s\n", why);
            fflush(stdout);
            return;
        }
        if (sVisit < 0) {
            /* where the trip began, by the room's first way in (a room on the area maps: fast travel's
               departure refusal let none other) */
            const int room = portAreaRoomFind(gp->currentMapName);
            const int way = portTravelFirstWay(room);
            snprintf(sRetMap, sizeof(sRetMap), "%s", gp->currentMapName);
            snprintf(sRetHit, sizeof(sRetHit), "%s", way >= 0 ? portAreaExits[way].hit : "");
            sGsw0 = gp->gsw0;
            memcpy(sGswf, gp->gswf, sizeof(sGswf));
            memcpy(sGsw, gp->gsw, sizeof(sGsw));
            sSum = wordsSum();
        }
        sVisit = i;
        sGoing = 1;
        printf("[special] trip: %s (%s %s), back to %s %s\n", kPlaces[i].name, kPlaces[i].rooms[0], kPlaces[i].way,
               sRetMap, sRetHit);
        fflush(stdout);
        portAreaMapsJumped();
        seqSetSeq(SEQ_MAP_CHANGE, kPlaces[i].rooms[0], kPlaces[i].way[0] != '\0' ? kPlaces[i].way : NULL);
    }
}

/* --- Bowser's pause --- */

const char* portSpecialBowserPauseLine(void) {
    return sVisit >= 0 && kPlaces[sVisit].bowser ? "{X}: leave, back to where Mario came from." : NULL;
}

int portSpecialBowserPause(int buttons) {
    float gx, gy;
    int tapped;
    if (portSpecialBowserPauseLine() == NULL) {
        return 0;
    }
    /* the mouse or a tap on the line (warp_map_aurora.c draws it there, the middle of the picture's foot) */
    tapped = portMenuPoint(&gx, &gy) == 1 && gx >= -260.0f && gx <= 260.0f && gy <= PORT_SPECIAL_LINE_TOP + 8.0f &&
             gy >= PORT_SPECIAL_LINE_TOP - 36.0f;
    if (!(buttons & PAD_BUTTON_X) && !tapped) {
        return 0;
    }
    return portSpecialLeave() == NULL;
}

/* --- the rooms' scripts --- */

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture (keeps the pointer) */

int portEvtSpecialVisit(struct EventEntry* event, int isFirstCall) {
    const int on = sVisit >= 0 && gp != NULL && placeOf(gp->currentMapName) == sVisit;
    (void)isFirstCall;
    evtSetValue(event, event->args[0], on);
    if (on && strcmp(gp->currentMapName, "kpa_01") == 0) {
        /* (Bowser has no pause in his hall: its far door is the way back, as in the story) */
        portCoopNotice("Bowser's Castle: the far door leads back.");
    }
    return EVT_RETURN_DONE;
}

int portEvtSpecialBack(struct EventEntry* event, int isFirstCall) {
    (void)event;
    (void)isFirstCall;
    goBack();
    return EVT_RETURN_DONE;
}

int portEvtSpecialPipe(struct EventEntry* event, int isFirstCall) {
    BeroEntry* bero = (BeroEntry*)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (bero == NULL) {
        return EVT_RETURN_DONE;
    }
    /* (the table is the room's static data: set back each time, so the story's scenes find it as it was) */
    if (sVisit >= 0 && sRetMap[0] != '\0') {
        bero->nextMapName = sRetMap;
        bero->nextBeroName = sRetHit[0] != '\0' ? sRetHit : NULL;
    } else {
        bero->nextMapName = NULL;
        bero->nextBeroName = bero->hitName;
    }
    return EVT_RETURN_DONE;
}
