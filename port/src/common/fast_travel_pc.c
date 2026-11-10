/* Fast travel (port/include/port/fast_travel.h): which trips the story allows
 * now, the ways in Mario knows, and the trip itself. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/area_maps.h"
#include "port/fast_travel.h"
#include "port/gameplus.h"
#include "port/platform.h"
#include "port/players.h"
#include "port/replay.h"
#include "port/statesync.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mario/mario_party.h"

#define NEVER 9999

/* The stretches the story keeps Mario in, by its own values (include/evt/evt_story.h): leaving
 * an area (or one room of it: room, a map's start) is refused while the story is in
 * [departFrom, departTo); arriving is let only while it is in [arriveFrom, arriveTo). Every row
 * that matches a room counts. The prologue (before Petal Meadows) and the ending (Grodus beaten,
 * to the credits' return) are refused everywhere (storyOpen). */
static const struct {
    const char* area;
    const char* room;
    int departFrom, departTo;
    int arriveFrom, arriveTo;
} kStory[] = {
    /* the Excess Express: its cars only between stations */
    {"rsh", NULL, 0, NEVER, NEVER, NEVER},
    /* Hooktail Castle's 50 seconds to escape */
    {"gon", NULL, STORY_STG1_13_01_50_SECONDS_TO_ESCAPE, STORY_STG1_13_02_KEY_2, 0, NEVER},
    /* the Great Tree's time bomb */
    {"mri", NULL, STORY_STG2_23_TIME_BOMB_ACTIVATED, STORY_STG2_24_BOSS_BATTLE, 0, NEVER},
    /* Glitzville: no blimp home until the champion's story is over */
    {"tou", NULL, STORY_STG3_00_WELCOME_TO_GLITZVILLE, STORY_STG3_34_RETURN_TO_ROGUEPORT,
     STORY_STG3_34_RETURN_TO_ROGUEPORT, NEVER},
    /* Twilight Town and its trail while Doopliss has Mario's name */
    {"usu", NULL, STORY_STG4_10_02_TRADING_PLACES, STORY_STG4_15_02_BACK_TO_NORMAL, 0, NEVER},
    {"gra", NULL, STORY_STG4_10_02_TRADING_PLACES, STORY_STG4_15_02_BACK_TO_NORMAL, 0, NEVER},
    {"jin", NULL, STORY_STG4_10_02_TRADING_PLACES, STORY_STG4_15_02_BACK_TO_NORMAL, 0, NEVER},
    /* Keelhaul Key: marooned until the pirates sail home; back once it can be sailed to any time */
    {"muj", NULL, STORY_STG5_10_HAUNTED, STORY_STG5_58_FAIR_WINDS, STORY_STG5_63_KEELHAUL_ANYTIME, NEVER},
    {"dou", NULL, STORY_STG5_10_HAUNTED, STORY_STG5_58_FAIR_WINDS, STORY_STG5_63_KEELHAUL_ANYTIME, NEVER},
    /* the Pirate's Grotto's wave-dodging run: only ever crossed as the boat (on foot the room's
     * boat scripts crash: the sweep found it) */
    {"dou", "dou_10", 0, 0, NEVER, NEVER},
    /* Cortez's ship (and its deck and hold): it sails off */
    {"muj", "muj_1", 0, 0, NEVER, NEVER},
    /* Riverside Station and Poshley Heights: the train's chapter; the station is off the line after */
    {"hom", NULL, STORY_STG6_00_START, STORY_STG6_56_ROGUEPORT_STATION, NEVER, NEVER},
    {"eki", NULL, STORY_STG6_00_START, STORY_STG6_56_ROGUEPORT_STATION, NEVER, NEVER},
    {"pik", NULL, STORY_STG6_00_START, STORY_STG6_56_ROGUEPORT_STATION, STORY_STG6_56_ROGUEPORT_STATION,
     NEVER},
    /* the Moon and the X-Naut Fortress: by the cannon, home when chapter 7 is over; the fortress
     * never again (and Peach's rooms there are hers) */
    {"moo", NULL, STORY_STG7_06_TO_THE_MOON, STORY_STG7_50_A_WAITING_PROF_FRANKLY,
     STORY_STG7_50_A_WAITING_PROF_FRANKLY, NEVER},
    {"aji", NULL, STORY_STG7_06_TO_THE_MOON, STORY_STG7_50_A_WAITING_PROF_FRANKLY, NEVER, NEVER},
    /* the Palace of Shadow: no way back from the Shadow Queen's chamber; it and the rooms after
     * never arrived in */
    {"las", NULL, STORY_STG8_14_01_QUEENS_CHAMBER, STORY_STG9_02_RETURN_TO_ROGUEPORT, STORY_STG8_00_START,
     NEVER},
    {"las", "las_28", 0, NEVER, NEVER, NEVER},
    {"las", "las_29", 0, NEVER, NEVER, NEVER},
    {"las", "las_30", 0, NEVER, NEVER, NEVER},
};
#define STORY_ROWS ((int)(sizeof(kStory) / sizeof(kStory[0])))

/* The hubs a file's story has passed, each by its first way in, for a record that hasn't them:
 * the story value by which Mario has surely been in it. */
static const struct {
    const char* map;
    int story;
} kSeeds[] = {
    {"gor_00", STORY_STG0_07_GOOMBELLA_JOINS},  {"gor_01", STORY_STG0_07_GOOMBELLA_JOINS},
    {"gor_02", STORY_STG0_08_STOLEN_COINS},     {"tik_01", STORY_STG0_11_3_GOOMBA_GOONS},
    {"hei_00", STORY_STG1_00_01_THE_SHADOW_OF_HOOKTAIL}, {"hei_01", STORY_STG1_01_THE_FLIP_BRIDGE},
    {"nok_00", STORY_STG1_01A_PETALBURG_1ST_TIME}, {"nok_01", STORY_STG1_02_TALKING_WITH_KROOP},
    {"gon_00", STORY_STG1_09_HOOKTAIL_CASTLE},  {"win_00", STORY_STG2_01_A_SIREN_ARGUMENT},
    {"win_01", STORY_STG2_02_TO_THE_GREAT_TREE}, {"mri_00", STORY_STG2_03_THE_GREAT_TREE},
    {"tou_01", STORY_STG3_10_MINORS_5TH_PL_EGG}, {"usu_01", STORY_STG4_01_00_GATEKEEPER_CONVERSATION},
    {"usu_00", STORY_STG4_01_THE_MAYORS_A_PIG}, {"gra_00", STORY_STG4_03_SHOP_KEY},
    {"jin_00", STORY_STG4_09_STEEPLE_DEMO_INT}, {"muj_00", STORY_STG5_11_DESERTED_ISLAND},
    {"muj_01", STORY_STG5_16_FLAVIOS_WINE},     {"pik_00", STORY_STG6_50_THE_SANCTUM_MANAGER},
    {"pik_01", STORY_STG6_50_THE_SANCTUM_MANAGER}, {"bom_02", STORY_STG7_02_MAYOR_BOB},
    {"moo_00", STORY_STG7_06_TO_THE_MOON},      {"las_00", STORY_STG8_01_1000_DRY_BONES},
};

static int story(void) {
    return swByteGet(GSW_STORY_PROGRESS);
}

static int storyOpen(int s) {
    return s >= STORY_STG1_00_PETAL_MEADOWS && !(s >= STORY_STG8_END_AFTER_THE_BATTLE && s < STORY_STG9_02_RETURN_TO_ROGUEPORT);
}

static int rowMatches(int row, int room) {
    const PortAreaRoom* r = &portAreaRooms[room];
    return strcmp(kStory[row].area, r->area) == 0 &&
           (kStory[row].room == NULL || strncmp(r->map, kStory[row].room, strlen(kStory[row].room)) == 0);
}

/* Ways in only ever come through in a vehicle, or from a room that is gone: on foot the room's
 * walk in ends in the water, where Mario is bitten and put back, over and over, and the walk never
 * ends (the travel sweeps found them; $TTYD_TRAVEL_SWEEP=ways tries every way). Never arrived
 * through. */
static const struct {
    const char* room;
    const char* hit;
} kVehicleWays[] = {
    {"dou_11", "w_bero"},   /* the Pirate's Grotto's end, from the wave-dodging run: as the boat */
    /* the grotto's waterways, each only sailed as the boat (the Grotto's water never changes): the
     * channel dou_02 - dou_12 - dou_03 under the side passage, the gated one dou_03 - dou_09, and the
     * lower ways of dou_03, dou_05 and dou_09 (on foot: stuck in the water, or bitten there) */
    {"dou_02", "e_bero_1"},
    {"dou_12", "w_bero_1"},
    {"dou_12", "e_bero_1"},
    {"dou_03", "w_bero_1"},
    {"dou_03", "e_bero_2"},
    {"dou_03", "s_bero"},
    {"dou_09", "n_bero"},
    {"dou_09", "e_bero_1"},
    {"dou_05", "w_bero_2"},
    {"muj_00", "n_bero"},   /* Keelhaul Key's beach from Cortez's ship (muj_10), which has sailed */
};

static int vehicleWay(int exit) {
    int i;
    for (i = 0; i < (int)(sizeof(kVehicleWays) / sizeof(kVehicleWays[0])); i++) {
        if (strcmp(portAreaRooms[portAreaExits[exit].room].map, kVehicleWays[i].room) == 0 &&
            strcmp(portAreaExits[exit].hit, kVehicleWays[i].hit) == 0) {
            return 1;
        }
    }
    return 0;
}

/* the room's first way in (the list's safe spawn), or -1 */
static int firstWay(int room) {
    int i;
    for (i = 0; i < portAreaExitCount; i++) {
        if (portAreaExits[i].room == room && portAreaExits[i].hit != NULL && !vehicleWay(i)) {
            return i;
        }
    }
    return -1;
}

static int seeded(int room) {
    const int s = story();
    int i;
    for (i = 0; i < (int)(sizeof(kSeeds) / sizeof(kSeeds[0])); i++) {
        if (s >= kSeeds[i].story && strcmp(portAreaRooms[room].map, kSeeds[i].map) == 0) {
            return 1;
        }
    }
    return 0;
}

int portTravelOn(void) {
    return portGameplus("fast_travel") != 0;
}

int portTravelRoomKnown(int room) {
    return room >= 0 && room < portAreaRoomCount && (portAreaMineVisited(room) || seeded(room));
}

int portTravelWayKnown(int exit) {
    int room;
    if (exit < 0 || exit >= portAreaExitCount || portAreaExits[exit].hit == NULL || vehicleWay(exit)) {
        return 0;
    }
    room = portAreaExits[exit].room;
    return portAreaMineWay(exit) || (seeded(room) && firstWay(room) == exit);
}

/* someone (not a partner) walking with Mario: an escort, a Trouble's client. Not player 3's companion, which
   co-op puts out in the same party slot: with three or more players on the couch, every mode and trip was
   refused as "someone following Mario". */
static int follower(void) {
    return portCoopStoryFollowerId() >= 0;
}

const char* portTravelDepartRefusal(void) {
    const MarioWork* mp = marioGetPtr();
    const int s = story();
    int room, i;
    if (!portTravelOn()) {
        return "Fast travel is off (Settings, Enhancements).";
    }
    if (gp == NULL || pouchGetPtr() == NULL || mp == NULL || seqGetSeq() != SEQ_GAME) {
        return "Only in the field.";
    }
    if (portReplayActive()) {
        return "Not while a replay records or plays.";
    }
    if (portStateSyncCoopGuest()) {
        return "Playing together: the host travels, and everyone comes along.";
    }
    if (portStateSyncOn() && !portStateSyncCoopOn() && !portStateSyncAllTravel()) {
        /* (playing together the host's trip takes everyone along, as a door does: its own setting is enough) */
        return "Online: every player must turn fast travel on.";
    }
    if (mp->characterId != 0) {
        return "Only Mario can travel.";
    }
    if (!storyOpen(s)) {
        return "Not now: the story keeps Mario here.";
    }
    room = portAreaRoomFind(gp->currentMapName);
    if (room < 0) {
        return "Not from here: walk to a room on the map first.";
    }
    if (follower()) {
        return "Not while someone is following Mario.";
    }
    for (i = 0; i < STORY_ROWS; i++) {
        if (rowMatches(i, room) && s >= kStory[i].departFrom && s < kStory[i].departTo) {
            return "Not now: the story keeps Mario here.";
        }
    }
    return NULL;
}

const char* portTravelStoryKeeps(void) {
    const int s = story();
    int room, i;
    if (gp == NULL || pouchGetPtr() == NULL) {
        return NULL;
    }
    /* (the ending, Grodus beaten to the credits' return; the prologue is let: no stretch of it is left half
       done by a trip, and a mode's tests start on a new game) */
    if (!storyOpen(s) && s >= STORY_STG1_00_PETAL_MEADOWS) {
        return "Not now: the story keeps Mario here.";
    }
    if (follower()) {
        return "Not while someone is following Mario.";
    }
    room = portAreaRoomFind(gp->currentMapName);
    for (i = 0; room >= 0 && i < STORY_ROWS; i++) {
        if (rowMatches(i, room) && s >= kStory[i].departFrom && s < kStory[i].departTo) {
            return "Not now: the story keeps Mario here.";
        }
    }
    return NULL;
}

int portTravelStoryLets(const char* map) {
    const int s = story();
    const int room = map != NULL ? portAreaRoomFind(map) : -1;
    int i;
    for (i = 0; room >= 0 && i < STORY_ROWS; i++) {
        if (rowMatches(i, room) && (s < kStory[i].arriveFrom || s >= kStory[i].arriveTo)) {
            return 0;
        }
    }
    return 1;
}

const char* portTravelArriveRefusal(int room) {
    const int s = story();
    int i, ways = 0;
    if (room < 0 || room >= portAreaRoomCount || !portTravelRoomKnown(room)) {
        return "Mario hasn't been here yet.";
    }
    if (!storyOpen(s)) {
        return "Not now: the story keeps Mario here.";
    }
    for (i = 0; i < STORY_ROWS; i++) {
        if (rowMatches(i, room) && (s < kStory[i].arriveFrom || s >= kStory[i].arriveTo)) {
            return "The story doesn't let Mario go there now.";
        }
    }
    for (i = 0; i < portAreaExitCount && !ways; i++) {
        ways = portAreaExits[i].room == room && portTravelWayKnown(i);
    }
    return ways ? NULL : "No way in known yet: walk through one first.";
}

/* --- the trip --- */

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture (keeps the pointer) */

static char sMap[16], sHit[32];
static int sAsked;

const char* portTravelRequest(int exit) {
    const char* why = portTravelDepartRefusal();
    if (why == NULL) {
        why = exit >= 0 && exit < portAreaExitCount ? portTravelArriveRefusal(portAreaExits[exit].room)
                                                    : "Mario hasn't been here yet.";
    }
    if (why == NULL && !portTravelWayKnown(exit)) {
        why = "No way in known yet: walk through one first.";
    }
    if (why != NULL) {
        printf("[travel] refused: %s\n", why);
        fflush(stdout);
        return why;
    }
    snprintf(sMap, sizeof(sMap), "%s", portAreaRooms[portAreaExits[exit].room].map);
    if (strncmp(sMap, "rsh_", 4) == 0 && strlen(sMap) == 6) {
        snprintf(sMap + 6, sizeof(sMap) - 6, "_a"); /* (never: the train is refused) */
    }
    snprintf(sHit, sizeof(sHit), "%s", portAreaExits[exit].hit);
    sAsked = 1;
    printf("[travel] asked: %s %s\n", sMap, sHit);
    fflush(stdout);
    return NULL;
}

static void sweepFrame(void);

void portTravelFrame(void) {
    sweepFrame();
    if (!sAsked) {
        return;
    }
    if (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_BATTLE && seqGetSeq() != SEQ_MAP_CHANGE) {
        sAsked = 0; /* the title, a Game Over: dropped */
        return;
    }
    if (seqGetSeq() != SEQ_GAME || seqCheckSeq() || marioStGetSystemLevel() != 0 || !marioChkKey() ||
        !marioChkCtrl()) {
        return; /* a battle, a scene, a menu or Mario without his controls (online: a shared one): it waits */
    }
    sAsked = 0;
    {
        /* (a battle or a scene meanwhile may have changed the story, or another player's setting) */
        const char* why = portTravelDepartRefusal();
        if (why == NULL) {
            why = portTravelArriveRefusal(portAreaRoomFind(sMap));
        }
        if (why != NULL) {
            printf("[travel] dropped: %s\n", why);
            fflush(stdout);
            portCoopNotice(why); /* (a string literal: the refusals' own; asked for, so said on screen) */
            return;
        }
    }
    printf("[travel] trip: %s %s\n", sMap, sHit);
    fflush(stdout);
    portAreaMapsJumped();
    seqSetSeq(SEQ_MAP_CHANGE, sMap, sHit);
}

/* --- $TTYD_TRAVEL_SWEEP: from where the game starts, every room asked for in turn (every room
 * and way counted as Mario's, so the table alone decides), each through its first way in (=ways:
 * through every way in) and waited on until Mario has the controls for half a second, then the
 * next from there, and home at the end. A trip that sticks is noted and the sweep goes on from
 * home. Logs "[travel] sweep <room> <way>: refused (why) / ok / STUCK / STRANDED (why)" and
 * "[travel] sweep done: ...", then the game ends (1 on a stuck or stranded trip). --- */

static int sSweep = -1, sNext, sWait, sSettle, sHome = -1, sFrom;
static int sTrips, sRefused;
static int sWays;                /* $TTYD_TRAVEL_SWEEP=ways: every way into every room, not only the first */
static int sStuck, sRescue, sFought, sLost; /* trips that stuck (each noted, then on from home), a rescue on
                                              * its way, a fight on this trip, a Game Over after one */

static int fieldFree(void) {
    return seqGetSeq() == SEQ_GAME && !seqCheckSeq() && marioStGetSystemLevel() == 0 && marioChkKey() &&
           marioChkCtrl();
}

static void sweepEnd(int code) {
    if (sStuck != 0) {
        printf("[travel] sweep: %d trips stuck\n", sStuck);
    }
    printf("[travel] sweep done: %d trips, %d refused%s\n", sTrips, sRefused, code != 0 ? ", FAILED" : "");
    fflush(stdout);
    sSweep = 0;
    portTestExit(code);
}

/* the story value by which Mario has first come into each area (include/evt/evt_story.h's first
 * value there): a room of an area not reached yet can't have been visited, so isn't tried */
static const struct {
    const char* area;
    int story;
} kReached[] = {
    {"gor", STORY_STG0_03_TO_ROGUEPORT},    {"tik", STORY_STG0_11_3_GOOMBA_GOONS},
    {"hei", STORY_STG1_00_01_THE_SHADOW_OF_HOOKTAIL}, {"nok", STORY_STG1_01A_PETALBURG_1ST_TIME},
    {"gon", STORY_STG1_09_HOOKTAIL_CASTLE}, {"win", STORY_STG2_01_A_SIREN_ARGUMENT},
    {"mri", STORY_STG2_03_THE_GREAT_TREE},  {"tou", STORY_STG3_00_WELCOME_TO_GLITZVILLE},
    {"usu", STORY_STG4_01_00_GATEKEEPER_CONVERSATION}, {"gra", STORY_STG4_03_SHOP_KEY},
    {"jin", STORY_STG4_09_STEEPLE_DEMO_INT}, {"muj", STORY_STG5_10_HAUNTED},
    {"dou", STORY_STG5_19_01_ENTERING_THE_GROTTO}, {"rsh", STORY_STG6_01_THE_CONDUCTOR},
    {"hom", STORY_STG6_27_RIVERSIDE_STATION}, {"eki", STORY_STG6_30_ORIGAMI_STAIRS},
    {"pik", STORY_STG6_50_THE_SANCTUM_MANAGER}, {"bom", STORY_STG7_01_FAHR_OUTPOST_1ST_VISIT},
    {"moo", STORY_STG7_06_TO_THE_MOON},     {"aji", STORY_STG7_07_01_INSIDE_THE_BUILDING},
    {"las", STORY_STG8_01_1000_DRY_BONES},
};

static int reached(int room) {
    int i;
    for (i = 0; i < (int)(sizeof(kReached) / sizeof(kReached[0])); i++) {
        if (strcmp(kReached[i].area, portAreaRooms[room].area) == 0) {
            return story() >= kReached[i].story;
        }
    }
    return 0;
}

/* asks for the trip to room through way (its first way in when -1); 1 when on its way */
static int sweepAsk(int room, int way) {
    const char* why = reached(room) ? portTravelArriveRefusal(room) : "not reached yet";
    if (way < 0) {
        way = firstWay(room);
    }
    if (why == NULL) {
        why = way >= 0 ? portTravelRequest(way) : "no way in";
    }
    if (why != NULL) {
        printf("[travel] sweep %s: refused (%s)\n", portAreaRooms[room].map, why);
        sRefused++;
        return 0;
    }
    sTrips++;
    sFought = 0;
    sFrom = story();
    if (pouchGetPtr() != NULL) {
        /* (each trip at full HP, so a bite on one never adds up to a Game Over on another) */
        pouchGetPtr()->currentHP = pouchGetPtr()->maximumHP;
    }
    sWait = 1800;
    sSettle = 0;
    return 1;
}

static void sweepFrame(void) {
    if (sSweep < 0) {
        if (getenv("TTYD_TRAVEL_SWEEP") == NULL || gp == NULL || !fieldFree()) {
            return;
        }
        portAreaMapsMarkAll();
        sWays = strcmp(getenv("TTYD_TRAVEL_SWEEP"), "ways") == 0;
        sSweep = 1;
        sNext = 0;
        sWait = 0;
        {
            /* $TTYD_TRAVEL_SWEEP_FROM=<map>: from that room on (a rerun of a sweep's tail) */
            const char* from = getenv("TTYD_TRAVEL_SWEEP_FROM");
            const int room = from != NULL ? portAreaRoomFind(from) : -1;
            if (room >= 0) {
                while (sNext < (sWays ? portAreaExitCount : room) && (!sWays || portAreaExits[sNext].room != room)) {
                    sNext++;
                }
            }
        }
        sHome = portAreaRoomFind(gp->currentMapName);
        printf("[travel] sweep from %s, story %d: %s\n", gp->currentMapName, story(),
               portTravelDepartRefusal() != NULL ? portTravelDepartRefusal() : "may leave");
        fflush(stdout);
        if (portTravelDepartRefusal() != NULL) {
            sweepEnd(0); /* (a story point that keeps Mario here: nothing to try) */
        }
        return;
    }
    if (sSweep != 1) {
        return;
    }
    if (sWait > 0) { /* a trip on its way */
        if (sAsked || !fieldFree()) {
            sSettle = 0;
            if (seqGetSeq() == SEQ_BATTLE) {
                sFought = 1;
                return; /* a battle where Mario arrived (a boss's room): as on foot, $TTYD_AUTO_BATTLE plays it */
            }
            if (seqGetSeq() == SEQ_GAME_OVER && sFought && !sRescue) {
                sLost = 1;
            }
            if (--sWait == 0) {
                if (sLost) {
                    /* (a test file's Mario is at his first level at any story point: beaten by the
                     * room's enemies, not the way in's fault; a Game Over without a fight, bitten in
                     * the water to the end, is a stuck way) */
                    printf("[travel] sweep %s %s: a fight there lost (Game Over), on from home\n", sMap, sHit);
                } else {
                    printf("[travel] sweep %s %s: STUCK (no controls 30 s after the trip)\n", sMap, sHit);
                    sStuck++;
                }
                if (sRescue || sHome < 0 || firstWay(sHome) < 0) {
                    sweepEnd(1);
                    return;
                }
                /* noted, and on from home by a plain map change, which ends a stuck scene */
                sLost = sFought = 0;
                sRescue = 1;
                sWait = 1800;
                fflush(stdout);
                seqSetSeq(SEQ_MAP_CHANGE, portAreaRooms[sHome].map, portAreaExits[firstWay(sHome)].hit);
            }
            return;
        }
        if (++sSettle < 30) {
            return;
        }
        sWait = 0;
        if (sRescue) {
            sRescue = 0;
        } else if (!sFought && pouchGetPtr() != NULL && pouchGetPtr()->currentHP < pouchGetPtr()->maximumHP) {
            /* the walk in hurt him with no fight: it ends in the water, where he is bitten and put
             * back. Noted, not failed (a test file's room flags may leave a moving floor or a
             * channel as no save at that story point would): a way to look at for the list above */
            printf("[travel] sweep %s %s: ok, but bitten walking in (%d/%d HP): a way in to the water?\n", sMap,
                   sHit, (int)pouchGetPtr()->currentHP, (int)pouchGetPtr()->maximumHP);
        } else {
            printf("[travel] sweep %s %s: ok (story %d -> %d)\n", gp->currentMapName, sHit, sFrom, story());
        }
        if (portTravelDepartRefusal() != NULL) {
            printf("[travel] sweep %s: STRANDED (%s)\n", gp->currentMapName, portTravelDepartRefusal());
            sweepEnd(1);
            return;
        }
        fflush(stdout);
    }
    if (sWays) { /* every way in, each its own trip */
        while (sNext < portAreaExitCount) {
            const int way = sNext++;
            if (portAreaExits[way].hit != NULL && !vehicleWay(way) && sweepAsk(portAreaExits[way].room, way)) {
                return;
            }
        }
    } else {
        while (sNext < portAreaRoomCount) {
            if (sweepAsk(sNext++, -1)) {
                return;
            }
        }
    }
    if (sNext == (sWays ? portAreaExitCount : portAreaRoomCount) && sHome >= 0) {
        sNext++;
        printf("[travel] sweep home:\n");
        if (sweepAsk(sHome, -1)) {
            return;
        }
    }
    sweepEnd(sStuck != 0);
}

