/* Paper Cut's own open world, the game side (port/include/port/open_world.h,
 * docs/RANDOMIZER.md "Paper Cut's open world").
 *
 * Each chapter's story on its own. The game keeps one story value (GSW 0)
 * and every map's scripts branch on it, so in the game a chapter can only
 * start once the one before it has ended. In an open-world file the value a
 * map's scripts read and write is its chapter's own (kept in the file's GSW
 * block, PORT_OPEN_TRACKS): Petal Meadows and Hooktail's Castle read chapter
 * 1's, Boggly Woods and the Great Tree chapter 2's, Glitzville chapter 3's,
 * Twilight Town and the steeple chapter 4's, Keelhaul Key and the grotto
 * chapter 5's, the train, Riverside and Poshley Heights chapter 6's, Fahr
 * Outpost, the moon and the X-Naut fortress chapter 7's, the palace chapter
 * 8's, whatever the others are at. The rooms that lead into a chapter read
 * that chapter's too: tik_02's pipe (1), tik_03's (2), tik_08's (4), tik_07's
 * (7), so the way in shows the chapter's title card the first time and goes
 * straight in after. The station gor_04 is two chapters' way in: the train's
 * (6) while Mario boards it or comes off it, the blimp's (3) otherwise. The
 * harbor gor_00 reads chapter 5's once Mario holds the Old Letter (Flavio
 * waits to sail, story 228) or chapter 5 has begun (the ship's return,
 * Cortez's ferry). The Thousand-Year Door's rooms (tik_00, where Frankly
 * waits, and tik_05) read chapter 8's once Mario has the goal's Crystal
 * Stars: the door opens with them. The title cards read the chapter last
 * played. Peach's and Bowser's intermissions, wherever they go, read the
 * chapter they follow while its story is in them (the window from the boss
 * to the chapter's last scene); after the Shadow Queen everything reads
 * chapter 8's (the ending, then the game's own world after it). Mayor Dour's
 * hunt for General White in chapter 7 (story 354) asks in other chapters'
 * rooms (Petalburg, Glitzville, the Great Tree, Twilight Town) and at
 * Goldbob's in Poshley Heights: those rooms read chapter 7's while it is in
 * the hunt. A chapter not begun reads as the value its first room waits for.
 *
 * Rogueport keeps the game's value, and moves it on by itself: once more
 * chapters are done than Rogueport has caught up with, entering a Rogueport
 * room while its story rests sets it to the start of its next stretch (the
 * scenes after a chapter: the Thousand-Year Door's star, Frankly's, Zess T.,
 * Don Pianta...). Stretch n follows the n-th chapter done, whichever chapter
 * it was, so their lines may name another chapter's star. Rogueport's story
 * never goes past the stretch's end: what follows it in the game is the next
 * chapter's way in, which is that chapter's own here.
 *
 * The items: a partner's item is one of the game's unused key items, named
 * and drawn as the partner; picking it up joins the partner. A partner's own
 * joining scene (all_party_lecture) gives what was placed there: another
 * partner (the scene as the game plays it, for that partner) or an item
 * (handed over as an NPC hands one). A script that sends the party away and
 * brings it back (Doopliss, the poisoned cake) brings back only who was
 * there. Keys the game numbers apart but the logic counts as one (the four
 * Castle Keys, the two Steeple Keys, the palace tower's eight, the palace's
 * three others) open any of
 * their locks. The Star Power maximum follows how many stars Mario has, not
 * which one came last. */
#include "port/open_world.h"
#include "port/pm64.h"
#include "port/replay.h"
#include "data/item_data.h"
#include "driver/itemdrv.h"
#include "driver/msgdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_item.h"
#include "evt/evt_mario.h"
#include "evt/evt_npc.h"
#include "evt/evt_story.h"
#include "icon_ids.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void portSaveStateNotice(const char* text); /* a notice over the picture (savestate_aurora.c) */

_Static_assert(PORT_OPEN_END - 1 == PORT_GSW_OPEN_LAST, "the open world's words: port/include/port/save_words.h");

static const unsigned char kMagic[4] = {'P', 'C', 'O', 'W'};

int portOpenOn(void) {
    return gp != NULL && memcmp(&gp->gsw[PORT_OPEN_MAGIC], kMagic, 4) == 0;
}

const char* portOpenFileSeed(void) {
    static char seed[PORT_OPEN_SEED_MAX];
    seed[0] = 0;
    if (portOpenOn()) {
        memcpy(seed, &gp->gsw[PORT_OPEN_SEED], PORT_OPEN_SEED_MAX - 1);
        seed[PORT_OPEN_SEED_MAX - 1] = 0;
    }
    return seed;
}

int portOpenFileGoal(void) {
    return portOpenOn() ? gp->gsw[PORT_OPEN_GOAL] : 0;
}

/* --- each chapter's own story --- */

/* the maps of a chapter (by name prefix), and the rooms of the hub that lead into one */
typedef struct OpenMap {
    const char* prefix;
    int chapter;
} OpenMap;
static const OpenMap kMaps[] = {
    {"hei_", 1}, {"nok_", 1}, {"gon_", 1}, {"tik_02", 1},
    {"win_", 2}, {"mri_", 2}, {"tik_03", 2},
    {"tou_", 3},
    {"usu_", 4}, {"gra_", 4}, {"jin_", 4}, {"tik_08", 4},
    {"muj_", 5}, {"dou_", 5},
    {"rsh_", 6}, {"hom_", 6}, {"eki_", 6}, {"pik_", 6},
    {"bom_", 7}, {"moo_", 7}, {"aji_", 7}, /* (Peach's rooms too: her intermissions read their own chapters') */
    {"las_", 8},
};
/* the rooms that ask about General White while chapter 7's story is in the hunt */
static const char* const kHuntMaps[] = {"nok_00", "nok_01", "tou_01", "mri_01", "usu_00", "pik_00"};
/* the story a chapter not begun reads: what its way in waits for */
static const int kChapterStart[10] = {0, STORY_STG0_19_BLOOPER_BATTLE, STORY_STG1_END_TO_STAGE_2,
                                      STORY_STG2_47_AFTER_GETTING_THE_TICKET, STORY_STG3_52_DARKLY,
                                      STORY_STG4_37_TO_THE_PORT, STORY_STG5_66_BELDAM_FINDS_OUT,
                                      STORY_STG6_END_ULTRA_HAMMER, STORY_STG7_26_THE_HIDEOUT_EXPLODES};
/* a chapter is done from its last scene in its own maps (Peach's mail, the
 * ship home); Rogueport's stretch after it starts there */
static const int kChapterDone[10] = {0, STORY_STG1_21_A_MAIL_FROM_PEACH, STORY_STG2_27_A_MAIL_FROM_PEACH,
                                     STORY_STG3_34_RETURN_TO_ROGUEPORT, STORY_STG4_16_LEAVING_TWILIGHT_TOWN,
                                     STORY_STG5_58_FAIR_WINDS, STORY_STG6_57_A_MAIL_FROM_PEACH,
                                     STORY_STG7_26_THE_HIDEOUT_EXPLODES, STORY_STG8_END_AFTER_THE_BATTLE};
/* the intermissions after a chapter's boss, from the value its own map
 * hands to Peach's (aji_10) to the one Mario takes up again: inside them
 * every map reads that chapter's story, as in the game (chapter 7's Bowser
 * goes from Lord Crump's end, chapter 8's is the ending and all after it) */
static const int kInterlude[10][2] = {
    {0, 0},
    {STORY_STG1_17_FATHER_AND_SON_REUNITED, STORY_STG1_20_KOOPS_LEAVES},
    {STORY_STG2_25_AFTER_THE_BOSS_BATTLE, STORY_STG2_26_PRAISE_FROM_THE_PUNI},
    {STORY_STG3_32_BOSS_BATTLE, STORY_STG3_34_RETURN_TO_ROGUEPORT},
    {STORY_STG4_15_04_SHADOW_SIRENS_AND_DOOPLISS, STORY_STG4_16_LEAVING_TWILIGHT_TOWN},
    {STORY_STG5_43_X_NAUTS_RETREAT, STORY_STG5_58_FAIR_WINDS},
    {STORY_STG6_54_WELCOME_BACK, STORY_STG6_56_ROGUEPORT_STATION},
    {STORY_STG7_19_THE_DOOR_TO_LORD_CRUMP, STORY_STG7_25_THE_LAST_MAIL},
    {STORY_STG8_16_SHADOW_QUEEN, STORY_STG9_END_ROGUEPORT + 1},
};
/* Rogueport's stretches: where each starts (stretch n after the n-th chapter
 * done) and where its story rests until the next */
static const int kStretchStart[] = {STORY_STG0_19_BLOOPER_BATTLE, STORY_STG1_21_A_MAIL_FROM_PEACH,
                                    STORY_STG2_27_A_MAIL_FROM_PEACH, STORY_STG3_34_RETURN_TO_ROGUEPORT,
                                    STORY_STG4_16_LEAVING_TWILIGHT_TOWN, STORY_STG5_59_01_A_MAIL_FROM_PEACH,
                                    STORY_STG6_57_A_MAIL_FROM_PEACH};
/* (the fifth stops at Don Pianta ill: Keelhaul's wedding ring and the Train
 * Ticket after it are chapter 6's way in here, the ticket placed elsewhere;
 * the sixth at Frankly's research, before the Ultra Hammer chest and chapter
 * 7's pipe; the seventh chapter done opens no stretch: what follows it is
 * the Thousand-Year Door, chapter 8's way in) */
static const int kStretchRest[] = {STORY_STG0_19_BLOOPER_BATTLE, STORY_STG1_END_TO_STAGE_2,
                                   STORY_STG2_47_AFTER_GETTING_THE_TICKET, STORY_STG3_52_DARKLY,
                                   STORY_STG4_31_FRANKLYS_HOUSE, STORY_STG5_62_AN_ILL_DON_PIANTA,
                                   STORY_STG6_72_THREE_MINUTES_LATER};
enum { STRETCH_COUNT = sizeof(kStretchStart) / sizeof(kStretchStart[0]) };

/* the chapters this build's stage opens. (The stamp's version is the stage
 * the file was started with; a file of an earlier stage takes this stage's
 * placements from its seed, so it plays this stage's chapters too: the ones
 * it never had are not begun.) */
static int chapterCount(void) {
    return 8;
}

static int bitCount(unsigned v) {
    int n = 0;
    for (; v != 0; v &= v - 1) {
        n++;
    }
    return n;
}

/* the Crystal Stars Mario has (bit 0 of the pouch's is the map's, Sweet Treat) */
static int starsHeld(void) {
    return bitCount(pouchGetPtr()->starPowersObtained & 0xFE);
}

/* the goal's Crystal Stars (0 in the stamp: all of the stage's) */
static int goalStars(void) {
    const int all = chapterCount() < 7 ? chapterCount() : 7;
    const int goal = gp->gsw[PORT_OPEN_GOAL];
    return goal > 0 && goal < all ? goal : all;
}

static int trackValue(int chapter) {
    const u8* p = &gp->gsw[PORT_OPEN_TRACKS + chapter * 2];
    const int v = p[0] | (p[1] << 8);
    return v != 0 ? v : kChapterStart[chapter];
}

static void setTrackValue(int chapter, int value) {
    u8* p = &gp->gsw[PORT_OPEN_TRACKS + chapter * 2];
    p[0] = (u8)value;
    p[1] = (u8)(value >> 8);
}

static int chaptersDone(void) {
    int c, n = 0;
    for (c = 1; c <= chapterCount(); c++) {
        n += trackValue(c) >= kChapterDone[c];
    }
    return n;
}

/* a chapter just done: noted after the ones before it (PORT_OPEN_ORDER) */
static void noteDone(int chapter) {
    int i;
    for (i = 0; i < 8; i++) {
        const int c = gp->gsw[PORT_OPEN_ORDER + i];
        if (c == chapter) {
            return;
        }
        if (c == 0) {
            gp->gsw[PORT_OPEN_ORDER + i] = (u8)chapter;
            return;
        }
    }
}

/* the Rogueport stretch the hub's story is in */
static int stretchOf(int raw) {
    int k = 0;
    while (k + 1 < STRETCH_COUNT && k + 1 < chapterCount() && raw >= kStretchStart[k + 1]) {
        k++;
    }
    return k;
}

static int hubAtRest(void) {
    return gp->gsw0 == kStretchRest[stretchOf(gp->gsw0)];
}

/* more chapters done than Rogueport's stretch k has caught up with */
static int catchUpDue(int k) {
    return k + 1 < STRETCH_COUNT && k + 1 < chapterCount() && chaptersDone() > k;
}

/* Rogueport's story at rest with nothing to catch up on: a room it shares
 * with a chapter's way in can be that chapter's */
static int hubSettled(void) {
    return hubAtRest() && !catchUpDue(stretchOf(gp->gsw0));
}

static int isMap(const char* name) {
    return strncmp(gp->currentMapName, name, strlen(name)) == 0;
}

/* gor_00, the harbor: chapter 5's once it has begun or Mario holds the Old
 * Letter (and Rogueport has no scene of its own going there: Don Pianta's
 * daughter, story 122) */
static int harborIsChapter5(void) {
    if (chapterCount() < 5 || gp->gsw0 == STORY_STG2_44_FINDING_DON_PIANTA) {
        return 0;
    }
    if (trackValue(5) > STORY_STG4_37_TO_THE_PORT) {
        return 1;
    }
    return hubAtRest() && pouchCheckItem(ITEM_OLD_LETTER) > 0;
}

/* a chapter a room's script asked for, for the rest of the visit
 * (portEvtOpenRoomChapter: gor_04's train and blimp attendants) */
static int sRoomChapter;

static int enteredBy(const char* name) {
    return strncmp(gp->beroEnterName, name, sizeof(gp->beroEnterName)) == 0;
}

/* gor_04, the station: the train's (chapter 6) while Mario boards it or has
 * just come off it, the blimp's (chapter 3) otherwise */
static int stationChapter(void) {
    if (chapterCount() >= 6) {
        if (sRoomChapter != 0) {
            return sRoomChapter;
        }
        if (enteredBy("resha_return") || enteredBy("n_bero")) {
            return 6;
        }
    }
    return 3;
}

/* tik_00 and tik_05, the Thousand-Year Door's: chapter 8's once Mario has
 * the goal's Crystal Stars or it has begun, while Rogueport's own story has
 * no scene going there */
static int doorIsChapter8(void) {
    if (chapterCount() < 8 || !hubSettled()) {
        return 0;
    }
    return trackValue(8) > kChapterStart[8] || starsHeld() >= goalStars();
}

static int inHunt(void) {
    size_t i;
    if (chapterCount() < 7 || trackValue(7) != STORY_STG7_02_MAYOR_BOB) {
        return 0;
    }
    for (i = 0; i < sizeof(kHuntMaps) / sizeof(kHuntMaps[0]); i++) {
        if (isMap(kHuntMaps[i])) {
            return 1;
        }
    }
    return 0;
}

/* the chapter last played, noted (title cards and intermissions read it) unless only peeking */
static void noteSticky(int write, int chapter) {
    if (write) {
        gp->gsw[PORT_OPEN_STICKY] = (u8)chapter;
    }
}

/* the chapter whose story the current map reads; write: note it as the chapter last played */
static int trackOf(int write) {
    const MarioWork* m;
    size_t i;
    int sticky;
    if (!portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE) {
        return 0; /* the prologue is the game's own */
    }
    sticky = gp->gsw[PORT_OPEN_STICKY];
    if (sticky >= 1 && sticky <= chapterCount()) {
        const int v = trackValue(sticky);
        /* the intermission after its boss: Peach's first room (aji_10, whose
         * init turns Mario into her) from the value the boss's map hands
         * over, everywhere from the next */
        if ((v > kInterlude[sticky][0] || (v == kInterlude[sticky][0] && isMap("aji_"))) &&
            v < kInterlude[sticky][1]) {
            return sticky;
        }
    }
    m = marioGetPtr();
    if (m != NULL && m->characterId != 0) {
        return sticky; /* Peach and Bowser: the chapter they follow */
    }
    if (inHunt()) {
        return 7; /* General White asked about */
    }
    if (isMap("gor_00")) {
        if (harborIsChapter5()) {
            noteSticky(write, 5);
            return 5;
        }
        return 0;
    }
    if (isMap("gor_04")) {
        const int c = stationChapter();
        noteSticky(write, (u8)c);
        return c;
    }
    if (isMap("tik_07")) {
        /* the pipe to Fahr Outpost; Rogueport's while its story has a scene
         * there (Peach's mail after the fourth chapter) or is due one */
        if (chapterCount() >= 7 && hubSettled()) {
            noteSticky(write, 7);
            return 7;
        }
        return 0;
    }
    if (isMap("tik_00") || isMap("tik_05")) {
        if (doorIsChapter8()) {
            noteSticky(write, 8);
            return 8;
        }
        return 0;
    }
    for (i = 0; i < sizeof(kMaps) / sizeof(kMaps[0]); i++) {
        if (isMap(kMaps[i].prefix)) {
            if (kMaps[i].chapter <= chapterCount()) {
                noteSticky(write, (u8)kMaps[i].chapter);
                return kMaps[i].chapter;
            }
            return 0;
        }
    }
    if (isMap("sys_")) {
        return sticky; /* a chapter's title card */
    }
    return 0;
}

int portOpenTrack(void) {
    return trackOf(1);
}

/* a room entered since Rogueport last looked (any room: the way back from a
 * chapter goes through its own) */
static char sLastMap[sizeof(((GlobalWork*)0)->currentMapName)];
static int sRoomEntered;
static void muteScene(EventEntry* entry); /* (a joining scene's lines, below) */

static void noteRoom(void) {
    if (memcmp(sLastMap, gp->currentMapName, sizeof(sLastMap)) != 0) {
        memcpy(sLastMap, gp->currentMapName, sizeof(sLastMap));
        sRoomEntered = 1;
        sRoomChapter = 0;
        muteScene(NULL);
    }
}

int portEvtOpenRoomChapter(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    if (gp != NULL && portOpenOn()) {
        noteRoom(); /* (a room just entered: its own visit) */
        sRoomChapter = (int)evtGetValue(event, event->args[0]);
    }
    return EVT_RETURN_DONE;
}

int portEvtOpenWorld(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portOpenOn());
    return EVT_RETURN_DONE;
}

/* --- the one-way trips --- */

int portOpenHeldCount(int item) {
    return pouchCheckItem((u32)item);
}

int portOpenPartnerIn(int partner) {
    return partyChkJoin((PartyId)partner) && !portPm64SlotStandIn(partner); /* (Watt alone in her slot: pm64.h) */
}

int portOpenStarsIn(void) {
    return starsHeld();
}

int portOpenPlaceFound(int type, int offset, int value) {
    return type == 0 ? swByteGet(offset) >= value : swGet(offset) != 0;
}

void portOpenStampRead(char* seed, int seedMax, unsigned* pools, int* goal, int* logic) {
    snprintf(seed, (size_t)seedMax, "%s", portOpenFileSeed());
    *pools = portOpenOn() ? gp->gsw[PORT_OPEN_POOLS] : 0;
    *goal = portOpenFileGoal();
    *logic = portOpenOn() ? gp->gsw[PORT_OPEN_LOGIC] : 0;
}

void portOpenRulesRead(PortOpenRules* out) {
    const int on = portOpenOn();
    out->level = on ? gp->gsw[PORT_OPEN_RULES] : 0;
    out->tricks = on ? gp->gsw[PORT_OPEN_RULES + 1] : 0;
    out->extra = on ? gp->gsw[PORT_OPEN_RULES + 2] : 0;
    out->entrances = on ? gp->gsw[PORT_OPEN_RULES + 3] : 0;
}

/* The chapter gates (pc_world.h's pcGateRegions, in its order): each sewer pipe room and its pipe, and the
 * chapter's first room and the pipe Mario comes out of there (the rooms' own BeroEntry tables) */
static const struct {
    const char* sewer;
    const char* sewerPipe;
    const char* room;
    const char* roomPipe;
} kGates[4] = {
    {"tik_02", "dokan", "hei_00", "dokan_2"},
    {"tik_03", "dokan", "win_06", "dokan1"},
    {"tik_08", "dokan", "usu_00", "dokan_1"},
    {"tik_07", "dokan__1", "bom_00", "dokan"},
};

/* each gate's chapter, and the title cards' entrances in sys_01 (sys.c's evt_start: a card, then the chapter's
 * first room) */
static const int kGateChapter[4] = {1, 2, 4, 7};
static const char* const kTitleCard[9] = {"prologue", "stg_1", "stg_2", "stg_3", "stg_4",
                                          "stg_5",    "stg_6", "stg_7", "stg_8"};

/* a chapter not begun yet: its way in shows its title card first */
static int notBegun(int chapter) {
    return trackValue(chapter) <= kChapterStart[chapter];
}

static int toCard(const char* map, const char* bero, int chapter) {
    return strcmp(map, "sys_01") == 0 && bero != NULL && strcmp(bero, kTitleCard[chapter]) == 0;
}

static int openGate(const char** map, const char** bero) {
    int g;
    for (g = 0; g < 4; g++) {
        /* into a chapter, from its gate's pipe room (or to its title card the first time): the chapter this gate
         * leads to, through that chapter's own title card while it hasn't begun */
        if (isMap(kGates[g].sewer) && (strcmp(*map, kGates[g].room) == 0 || toCard(*map, *bero, kGateChapter[g]))) {
            const int to = portOpenGateTarget(g);
            if (to >= 0 && to < 4 && to != g) {
                if (notBegun(kGateChapter[to])) {
                    *map = "sys_01";
                    *bero = kTitleCard[kGateChapter[to]];
                } else {
                    *map = kGates[to].room;
                    *bero = kGates[to].roomPipe;
                }
            }
            return 1;
        }
        /* back out of a chapter's first room: to the gate whose pipe leads there */
        if (isMap(kGates[g].room) && strcmp(*map, kGates[g].sewer) == 0) {
            int from;
            for (from = 0; from < 4; from++) {
                if (portOpenGateTarget(from) == g) {
                    break;
                }
            }
            if (from < 4 && from != g) {
                *map = kGates[from].sewer;
                *bero = kGates[from].sewerPipe;
            }
            return 1;
        }
    }
    return 0;
}

/* The rides (pc_doors.h's pcRides, in its order: the blimp, Flavio's ship, the train): a departure is the ride's
 * own map change from its station, to its first stop or its title card */
static int rideLeaving(const char* map, const char* bero) {
    if (isMap("gor_04") && (strcmp(map, "tou_20") == 0 || strcmp(map, "tou_00") == 0)) {
        return 0;
    }
    if (isMap("gor_00") && (strcmp(map, "muj_20") == 0 || toCard(map, bero, 5))) {
        return 1;
    }
    if (isMap("gor_04") &&
        (toCard(map, bero, 6) || (strcmp(map, "rsh_02_a") == 0 && bero != NULL && strcmp(bero, "nidome_start") == 0))) {
        return 2;
    }
    return -1;
}

/* where ride `ride`'s departure goes, by its chapter's story now (its scripts' own choice, made for the ride it
 * stands in for): the first time through its chapter's title card */
static void rideArrival(int ride, const char** map, const char** bero) {
    switch (ride) {
    case 0: /* gor_04's evt_hikousen */
        if (trackValue(3) < STORY_STG3_00_WELCOME_TO_GLITZVILLE) {
            *map = "tou_20";
            *bero = "start";
        } else {
            *map = "tou_00";
            *bero = NULL;
        }
        break;
    case 1: /* gor_00's departure: chapter 4's last scene hands over to the card (STG4_END), then the sailing */
        if (trackValue(5) < STORY_STG5_00_LOGBOOK) {
            setTrackValue(5, STORY_STG4_END_OFF_TO_THE_KEYS);
            *map = "sys_01";
            *bero = kTitleCard[5];
        } else {
            *map = "muj_20";
            *bero = "";
        }
        break;
    default: /* gor_04's evt_train: what the ride sets before it leaves */
        if (trackValue(6) >= STORY_STG6_56_ROGUEPORT_STATION) {
            swSet(GSWF_RSH_NEW_RIDE_PENDING);
        }
        swClear(GSWF_TRAIN_RETURN_TRIP);
        swSet(GSWF_TRAIN_DEPARTED);
        swByteSet(GSW_TRAIN_LEG, 0);
        swClear(GSWF_TRAIN_ARRIVED_ROGUEPORT);
#if VERSION >= VERSION_JP
        swClear(GSWF_TRAIN_ARRIVED_POSHLEY);
#endif
        swClear(GSWF_TRAIN_AT_ROGUEPORT);
#if VERSION >= VERSION_JP
        swClear(GSWF_TRAIN_AT_POSHLEY);
#endif
        if (trackValue(6) < STORY_STG6_00_START) {
            *map = "sys_01";
            *bero = kTitleCard[6];
        } else {
            *map = "rsh_02_a";
            *bero = "nidome_start";
        }
        break;
    }
}

static int openRide(const char** map, const char** bero) {
    const int ride = rideLeaving(*map, *bero);
    int to;
    if (ride < 0) {
        return 0;
    }
    to = portOpenRideTarget(ride);
    if (to >= 0 && to <= 2 && to != ride) {
        rideArrival(to, map, bero);
        printf("[open] ride %d leads to ride %d's destination: %s\n", ride, to, *map);
        fflush(stdout);
    }
    return 1;
}

/* seqSetSeq's map change, through the file's entrance shuffle: the chapter gates, the rides, the room doors and
 * the ways between areas. Not in the prologue (the game's own) nor for Peach or Bowser. */
void portOpenGate(const char** map, const char** bero) {
    const MarioWork* m;
    unsigned entrances;
    if (gp == NULL || *map == NULL || !portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE) {
        return;
    }
    entrances = gp->gsw[PORT_OPEN_RULES + 3];
    if (entrances == 0) {
        return;
    }
    m = marioGetPtr();
    if (m != NULL && m->characterId != 0) {
        return;
    }
    if ((entrances & PORT_OPEN_ENTRANCE_GATES) && openGate(map, bero)) {
        return;
    }
    if ((entrances & PORT_OPEN_ENTRANCE_TRANSPORTS) && openRide(map, bero)) {
        return;
    }
    if (entrances & (PORT_OPEN_ENTRANCE_ROOMS | PORT_OPEN_ENTRANCE_MIX)) {
        portOpenDoorLead(gp->currentMapName, *map, *bero, map, bero);
    }
}

int portOpenMystery(void) {
    if (gp == NULL) {
        return 0;
    }
    if (portOpenOn()) {
        return (gp->gsw[PORT_OPEN_RULES + 2] & PORT_OPEN_X_MYSTERY) != 0;
    }
    return portRandoRunMystery();
}

void portOpenStampRules(const PortOpenRules* rules) {
    if (gp == NULL || !portOpenOn()) {
        return;
    }
    gp->gsw[PORT_OPEN_RULES] = (u8)rules->level;
    gp->gsw[PORT_OPEN_RULES + 1] = (u8)rules->tricks;
    gp->gsw[PORT_OPEN_RULES + 2] = (u8)rules->extra;
    gp->gsw[PORT_OPEN_RULES + 3] = (u8)rules->entrances;
    printf("[open] file's rules: level %u, tricks %u, extra %u, entrances %u\n", rules->level, rules->tricks,
           rules->extra, rules->entrances);
    fflush(stdout);
}

/* Flavio's ship (gor_00, at story 228), the train attendant (gor_04) and the
 * Fahr Outpost mayor's cannon (bom_01) ask before the trip. Flavio's and the
 * mayor's scenes come only before their trip; the attendant's every ride,
 * and only the first (chapter 6 not begun yet) has no way back. */
int portEvtOpenTripReady(struct EventEntry* event, int isFirstCall) {
    int trip = (int)evtGetValue(event, event->args[0]);
    int ok = 1;
    (void)isFirstCall;
    /* the rides shuffled (pcRides: 0 the blimp, 1 the ship, 2 the train; the blimp's attendant asks as trip 3):
     * the trip is where the ride leads now */
    if (portOpenOn() && trip != 2 && (gp->gsw[PORT_OPEN_RULES + 3] & PORT_OPEN_ENTRANCE_TRANSPORTS)) {
        static const int kRideOfTrip[4] = {1, 2, -1, 0}, kTripOfRide[3] = {-1, 0, 1};
        const int ride = trip >= 0 && trip <= 3 ? kRideOfTrip[trip] : -1;
        const int to = ride >= 0 ? portOpenRideTarget(ride) : -1;
        trip = to >= 0 && to <= 2 ? kTripOfRide[to] : trip;
    }
    if (portOpenOn() && trip >= 0 && trip <= 2 && (trip != 1 || trackValue(6) == kChapterStart[6])) {
        ok = portOpenTripCanEnd(trip);
        printf("[open] trip %d (%s): %s\n", trip, trip == 0 ? "the ship" : trip == 1 ? "the train" : "the moon",
               ok ? "can be finished" : "not yet: something on the way isn't in hand");
        fflush(stdout);
    }
    evtSetValue(event, event->args[1], ok);
    return EVT_RETURN_DONE;
}

/* Rogueport's own story: on entering one of its rooms while it rests, the
 * next stretch if a chapter more is done (once per room: the room's scripts
 * all see one value) */
static void hubCatchUp(void) {
    int k;
    if (!sRoomEntered) {
        return;
    }
    sRoomEntered = 0;
    k = stretchOf(gp->gsw0);
    if (gp->gsw0 == kStretchRest[k] && catchUpDue(k)) {
        printf("[open] Rogueport: %d chapters done, stretch %d (story %d -> %d, map '%.*s')\n", chaptersDone(), k + 1,
               gp->gsw0, kStretchStart[k + 1], (int)sizeof(gp->currentMapName), gp->currentMapName);
        fflush(stdout);
        gp->gsw0 = kStretchStart[k + 1];
    }
}

int portOpenStoryGet(int raw) {
    int t;
    if (gp != NULL) {
        noteRoom();
    }
    t = portOpenTrack();
    if (t != 0) {
        return trackValue(t);
    }
    if (portOpenOn() && raw >= STORY_STG0_19_BLOOPER_BATTLE) {
        hubCatchUp();
        if (isMap("gor_00") && gp->gsw0 >= STORY_STG5_58_FAIR_WINDS) {
            /* the harbor while chapter 5 is not begun: as before Flavio's
             * search, whatever Rogueport's stretch (from chapter 5's end the
             * game puts Cortez's ferry there) */
            return kStretchRest[4];
        }
        return gp->gsw0;
    }
    return raw;
}

int portOpenStoryPeek(int raw) {
    int t;
    if (gp == NULL) {
        return raw;
    }
    t = trackOf(0);
    if (t != 0) {
        return trackValue(t);
    }
    if (portOpenOn() && raw >= STORY_STG0_19_BLOOPER_BATTLE) {
        if (isMap("gor_00") && gp->gsw0 >= STORY_STG5_58_FAIR_WINDS) {
            return kStretchRest[4];
        }
        return gp->gsw0; /* (before a catch-up the next room may make) */
    }
    return raw;
}

int portOpenStorySet(int value) {
    const int t = portOpenTrack();
    if (t == 0) {
        int k;
        if (!portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE) {
            return 0;
        }
        /* tik_03's Puni scenes (66 to 69) are chapter 2's way in: Rogueport's
         * first stretch ends where they would start */
        if (value >= STORY_STG1_32_PUNI_ENCOUNTER_2 && value < STORY_STG1_END_TO_STAGE_2) {
            gp->gsw0 = STORY_STG1_END_TO_STAGE_2;
            return 1;
        }
        if (isMap("gor_00") && gp->gsw0 >= STORY_STG5_58_FAIR_WINDS) {
            return 1; /* (the harbor reads as before Flavio's search: nothing it writes is Rogueport's) */
        }
        k = stretchOf(gp->gsw0);
        if (value > kStretchRest[k] && value > gp->gsw0) {
            printf("[open] Rogueport: story %d kept (not %d: past stretch %d's end)\n", gp->gsw0, value, k);
            fflush(stdout);
            return 1;
        }
        return 0;
    }
    if (t == 4 && trackValue(4) < STORY_STG4_13_VIVIAN_JOINS &&
        (value == STORY_STG4_13_03_BREAKING_THE_FLOOR || value == STORY_STG4_13_04_THE_LETTER_P)) {
        /* jin_11's floor (208) and jin_08's Letter "p" chest (209) are the
         * game's after Vivian's scene (206); here another chapter's Vivian
         * opens the gate to them (jin_10) before it. The story stays: what
         * comes between (Doopliss's first battle, the Superbombomb, Vivian
         * joining) and its places' items still come, and the floor's hole
         * stays open. (The chest itself waits: jin_08's evt_open_wait.) */
        printf("[open] chapter 4: story %d kept (not %d: before Vivian's scene)\n", trackValue(4), value);
        fflush(stdout);
        return 1;
    }
    if (trackValue(t) != value) {
        printf("[open] chapter %d: story %d -> %d (map '%.*s')\n", t, trackValue(t), value,
               (int)sizeof(gp->currentMapName), gp->currentMapName);
        fflush(stdout);
    }
    if (trackValue(t) < kChapterDone[t] && value >= kChapterDone[t]) {
        noteDone(t);
    }
    setTrackValue(t, value);
    return 1;
}

/* the n-th chapter done (1-based), 0 if fewer are: in the order noted, then
 * (a file from before the order was kept) the others done, by number */
static int nthDone(int n) {
    int order[8], count = 0, c, i;
    for (i = 0; i < 8; i++) {
        c = gp->gsw[PORT_OPEN_ORDER + i];
        if (c >= 1 && c <= chapterCount() && trackValue(c) >= kChapterDone[c]) {
            order[count++] = c;
        }
    }
    for (c = 1; c <= chapterCount() && count < 8; c++) {
        for (i = 0; i < count && order[i] != c; i++) {
        }
        if (i == count && trackValue(c) >= kChapterDone[c]) {
            order[count++] = c;
        }
    }
    return n >= 1 && n <= count ? order[n - 1] : 0;
}

int portEvtOpenStretchStar(struct EventEntry* event, int isFirstCall) {
    const int index = (int)evtGetValue(event, event->args[0]);
    int chapter;
    (void)isFirstCall;
    if (gp == NULL || !portOpenOn()) {
        return EVT_RETURN_DONE;
    }
    chapter = nthDone(index + 1);
    if (chapter >= 1 && chapter <= 7) { /* (chapter 8 has no Crystal Star) */
        printf("[open] the pedestal: the star of chapter %d, the %d. done\n", chapter, index + 1);
        fflush(stdout);
        evtSetValue(event, event->args[0], chapter - 1);
    }
    return EVT_RETURN_DONE;
}

/* --- the items --- */

/* each partner's item: the game's unused key items 0x53, 0x54, 0x6F, 0x70,
 * 0x71 (Ms. Mowz has none: her place is a trouble, never shuffled) */
static const struct {
    int party, item;
    const char* name;
    int icon;
} kTokens[] = {
    {2, 0x6F, "name_party1", ICON_156}, /* Koops */
    {5, 0x70, "name_party4", ICON_15F}, /* Flurrie */
    {3, 0x53, "name_party2", ICON_157}, /* Bobbery */
    {4, 0x54, "name_party3", ICON_158}, /* Yoshi */
    {6, 0x71, "name_party5", ICON_160}, /* Vivian */
};

int portOpenPartnerToken(int partner) {
    size_t i;
    for (i = 0; i < sizeof(kTokens) / sizeof(kTokens[0]); i++) {
        if (kTokens[i].party == partner) {
            return kTokens[i].item;
        }
    }
    return 0;
}

static int tokenPartner(int item) {
    size_t i;
    for (i = 0; i < sizeof(kTokens) / sizeof(kTokens[0]); i++) {
        if (kTokens[i].item == item) {
            return kTokens[i].party;
        }
    }
    return 0;
}

/* the partners' items named and drawn as the partners (the entries are unused by the game) */
static void prepareTokens(void) {
    size_t i;
    for (i = 0; i < sizeof(kTokens) / sizeof(kTokens[0]); i++) {
        ItemData* d = &itemDataTable[kTokens[i].item];
        d->name = kTokens[i].name;
        d->description = "pc_open_partner_item";
        d->menu_description = "pc_open_partner_item";
        d->icon_id = (u16)kTokens[i].icon;
    }
    /* the four curses (items 2 to 5), which the game only gives in its black chests' scenes: named for a
     * place's pickup ("You got Paper Mode!", not its unused "Invalid Item"), the Mario tab's help as their
     * description */
    for (i = 0; i < 4; i++) {
        static const char* const kName[4] = {"pc_open_curse_paper", "pc_open_curse_tube", "pc_open_curse_plane",
                                             "pc_open_curse_boat"};
        static const char* const kHelp[4] = {"msg_slit", "msg_roll", "msg_plane", "msg_ship"};
        ItemData* d = &itemDataTable[ITEM_PAPER_CURSE + i];
        d->name = kName[i];
        d->description = kHelp[i];
        d->menu_description = kHelp[i];
    }
}

unsigned portOpenItemAlias(unsigned item) {
    if (item >= ITEM_CASTLE_KEY_0x0C + 1 && item <= ITEM_CASTLE_KEY_0x0C + 3 && portOpenOn()) {
        return ITEM_CASTLE_KEY_0x0C; /* the four Castle Keys */
    }
    if (item == ITEM_STEEPLE_KEY_0x17 && portOpenOn()) {
        return ITEM_STEEPLE_KEY_0x16; /* the steeple's two */
    }
    if (item >= ITEM_PALACE_KEY_0x26 + 1 && item <= ITEM_PALACE_KEY_0x26 + 7 && portOpenOn()) {
        return ITEM_PALACE_KEY_0x26; /* the palace tower's eight */
    }
    if ((item == ITEM_PALACE_KEY_0x2F || item == ITEM_PALACE_KEY_0x30) && portOpenOn()) {
        return ITEM_PALACE_KEY_0x2E; /* the palace's other three */
    }
    return item;
}

int portOpenGetItem(int item) {
    int partner;
    unsigned alias;
    if (!portOpenOn()) {
        return 0;
    }
    partner = tokenPartner(item);
    if (partner != 0) {
        /* the partner's name in the game's own text (name_party0 Goombella ... name_party6 Ms. Mowz) */
        static const char* const kNames[] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
        char text[96], tag[16];
        const char* name;
        snprintf(tag, sizeof(tag), "name_party%d", partner - 1);
        name = msgSearch(tag);
        if (name == NULL || name[0] == '\0') {
            name = kNames[partner];
        }
        partyJoin(partner);
        snprintf(text, sizeof(text), portOpenText("%s joins the party!"), name);
        portSaveStateNotice(text);
        printf("[open] partner %d joins\n", partner);
        fflush(stdout);
        return 1;
    }
    alias = portOpenItemAlias((unsigned)item);
    if (alias != (unsigned)item) {
        return pouchGetItem((ItemType)alias); /* (a script's own key: the one key) */
    }
    /* the boots and hammers are progressive: a second Super Boots is the Ultra Boots */
    if (item == ITEM_SUPER_BOOTS && pouchCheckItem(ITEM_SUPER_BOOTS)) {
        return pouchGetItem(ITEM_ULTRA_BOOTS);
    }
    if (item == ITEM_SUPER_HAMMER && pouchCheckItem(ITEM_SUPER_HAMMER)) {
        return pouchGetItem(ITEM_ULTRA_HAMMER);
    }
    return 0;
}

/* --- the Crystal Stars --- */

void portOpenStarStone(void) {
    PouchData* pouch;
    int stars, goal;
    if (!portOpenOn()) {
        return;
    }
    pouch = pouchGetPtr();
    /* 100 per star Mario has (the game: 100 per the last one's number) */
    pouch->maximumSP = (s16)(100 * bitCount(pouch->starPowersObtained));
    if (pouch->maximumSP > 800) {
        pouch->maximumSP = 800;
    }
    stars = starsHeld();
    goal = goalStars();
    if (stars >= goal && !(gp->gsw[PORT_OPEN_FLAGS] & PORT_OPEN_FLAG_GOAL)) {
        char text[96];
        gp->gsw[PORT_OPEN_FLAGS] |= PORT_OPEN_FLAG_GOAL;
        snprintf(text, sizeof(text), portOpenText("The randomizer's goal is reached: %d Crystal Stars!"), stars);
        portSaveStateNotice(text);
        printf("[open] goal: %d Crystal Stars\n", stars);
        fflush(stdout);
    }
}

int portOpenStarShown(int star) {
    if (!portOpenOn() || star < 1 || star > 7) {
        return 1;
    }
    return (pouchGetPtr()->starPowersObtained & (1 << star)) != 0;
}

/* --- a partner's own joining scene --- */

static void* sLecture;     /* the linked area's all_party_lecture */
static int sLectureParty;  /* the partner its scene is joining */

void portOpenSetLecture(void* script) {
    sLecture = script;
    prepareTokens();
}

/* An item instead of the partner: the partner's NPC steps away, as in the
 * scene, and Mario is handed the item (LW(10)). */
static EVT_BEGIN(open_lecture_item)
    CALL(evt_mario_normalize)
    CALL(evt_npc_set_position, LW(8), 0, -1000, 0)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_item_entry, STRING("pc_open_item"), LW(10), LW(0), LW(1), LW(2), ITEM_ENTRY_FIXED, -1, 0)
    CALL(evt_item_flag_on, STRING("pc_open_item"), ITEM_FLAG_NO_TOUCH)
    CALL(evt_item_get_item, STRING("pc_open_item"))
    RETURN
EVT_END()

/* The scene whose joining lecture gave something other than its partner:
 * the event that ran it and the ones above it (each by its pointer and id,
 * so a later event in the same slot isn't taken for it). Their lines spoken
 * by the party are left out: they are the joining partner's words. */
enum { MUTE_DEPTH = 8 };
static struct {
    EventEntry* entry;
    s32 id;
} sMuted[MUTE_DEPTH];

static void muteScene(EventEntry* entry) {
    int i;
    memset(sMuted, 0, sizeof(sMuted));
    for (i = 0; i < MUTE_DEPTH && entry != NULL; i++, entry = entry->waitingEvent) {
        sMuted[i].entry = entry;
        sMuted[i].id = entry->eventId;
    }
}

int portOpenMuteLine(struct EventEntry* event, const char* speaker) {
    int i;
    if (event == NULL || sMuted[0].entry == NULL || !portOpenOn()) {
        return 0;
    }
    for (i = 0; i < MUTE_DEPTH; i++) {
        if (sMuted[i].entry == event && sMuted[i].id == event->eventId) {
            if (speaker == NULL || strcmp(speaker, "party") != 0) {
                return 0;
            }
            printf("[open] a line of the joining scene left out (the partner out would say it)\n");
            fflush(stdout);
            return 1;
        }
    }
    return 0;
}

void* portOpenChildScript(struct EventEntry* entry, void* script) {
    int partner, reward;
    if (script == NULL || script != sLecture || !portOpenOn()) {
        return script;
    }
    partner = (int)evtGetValue(entry, LW(9));
    reward = portOpenPartnerReward(partner);
    printf("[open] partner %d's scene: %s %d\n", partner, reward >= 0x1000 ? "partner" : "item",
           reward >= 0x1000 ? reward - 0x1000 : reward);
    fflush(stdout);
    if (reward == 0) {
        sLectureParty = partner;
        muteScene(NULL);
        return script;
    }
    if (reward >= 0x1000) {
        sLectureParty = reward - 0x1000;
        evtSetValue(entry, LW(9), reward - 0x1000); /* (that partner's own lecture, as the game plays it) */
        if (reward - 0x1000 != partner) {
            muteScene(entry);
        }
        return script;
    }
    sLectureParty = 0;
    evtSetValue(entry, LW(10), reward);
    muteScene(entry);
    return open_lecture_item;
}

/* --- the black chests --- */

int portEvtOpenChest(struct EventEntry* event, int isFirstCall) {
    const int curse = (int)evtGetValue(event, event->args[0]);
    int reward = 0;
    (void)isFirstCall;
    if (gp != NULL && portOpenOn()) {
        reward = portOpenChestReward(curse);
        printf("[open] black chest of curse %d: %s %d\n", curse, reward != 0 ? "gives item" : "its own curse", reward);
        fflush(stdout);
    }
    evtSetValue(event, event->args[1], reward);
    return EVT_RETURN_DONE;
}

/* --- the party sent away and brought back --- */

/* the TTYD partner herself joined (Ms. Mowz's slot held by Paper Mario 64's Watt alone isn't her: pm64.h) */
static int partnerJoined(int partner) {
    return partyChkJoin((PartyId)partner) && !portPm64SlotStandIn(partner);
}

int portOpenPartyLeft(int partner) {
    if (!portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE || partner < 1 || partner > 7) {
        return 0;
    }
    if (partnerJoined(partner)) {
        gp->gsw[PORT_OPEN_AWAY] |= (u8)(1 << partner);
    }
    return 0;
}

int portOpenPartyJoin(int partner) {
    u8* away;
    if (!portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE || partner < 1 || partner > 7) {
        return 0;
    }
    if (partner == sLectureParty) {
        sLectureParty = 0; /* the joining scene's own */
        return 0;
    }
    away = &gp->gsw[PORT_OPEN_AWAY];
    if (*away & (1 << partner)) {
        *away &= (u8) ~(1 << partner);
        return 0; /* back from where a scene sent them */
    }
    if (partnerJoined(partner)) {
        return 0;
    }
    printf("[open] partner %d not brought back: not found yet\n", partner);
    fflush(stdout);
    return 1;
}

/* --- stamping a file --- */

/* $TTYD_OPEN_SEED, a scripted run's seed: never online or in a replay, where
 * the randomizer's settings are the session's (the host's or the recording's) */
const char* portOpenTestSeed(void) {
    const char* v = getenv("TTYD_OPEN_SEED");
    if (v == NULL || v[0] == '\0' || portReplayActive()) {
        return NULL;
    }
    return v;
}

void portOpenStamp(const char* seed, unsigned pools, int goal) {
    size_t n = strlen(seed);
    if (gp == NULL) {
        return;
    }
    memset(&gp->gsw[PORT_OPEN_GSW], 0, PORT_OPEN_END - PORT_OPEN_GSW); /* only the open world's own bytes: the rest of 1920..2047 is other features' (PM64 2011..2019, Keelhaul switch 2030, hardcore 2040..2042) */
    memcpy(&gp->gsw[PORT_OPEN_MAGIC], kMagic, 4);
    gp->gsw[PORT_OPEN_VERSION] = PORT_OPEN_STAGE;
    gp->gsw[PORT_OPEN_LOGIC] = PORT_OPEN_LOGIC_VERSION;
    gp->gsw[PORT_OPEN_POOLS] = (u8)pools;
    gp->gsw[PORT_OPEN_GOAL] = (u8)goal;
    if (n > PORT_OPEN_SEED_MAX - 1) {
        n = PORT_OPEN_SEED_MAX - 1;
    }
    memcpy(&gp->gsw[PORT_OPEN_SEED], seed, n);
    prepareTokens();
    if (portOpenTestSeed() != NULL) {
        /* a scripted run's start: chapters' stories ($TTYD_OPEN_TRACKS,
         * "chapter:story,...") and items in hand ($TTYD_OPEN_ITEMS, item ids) */
        const char* v = getenv("TTYD_OPEN_TRACKS");
        while (v != NULL && *v != '\0') {
            char* end;
            long chapter, story;
            chapter = strtol(v, &end, 0);
            if (*end != ':') {
                break;
            }
            story = strtol(end + 1, &end, 0);
            if (chapter >= 1 && chapter <= 8) {
                setTrackValue((int)chapter, (int)story);
                if (story >= kChapterDone[chapter]) {
                    noteDone((int)chapter);
                }
                printf("[open] test: chapter %ld's story %ld\n", chapter, story);
            }
            v = *end == ',' ? end + 1 : NULL;
        }
        v = getenv("TTYD_OPEN_ITEMS");
        while (v != NULL && *v != '\0') {
            char* end;
            const long item = strtol(v, &end, 0);
            if (end == v) {
                break;
            }
            pouchGetItem((ItemType)item);
            printf("[open] test: item 0x%lX in hand\n", item);
            v = *end == ',' ? end + 1 : NULL;
        }
        /* flags set ($TTYD_OPEN_FLAGS, GSWF numbers) */
        v = getenv("TTYD_OPEN_FLAGS");
        while (v != NULL && *v != '\0') {
            char* end;
            const long flag = strtol(v, &end, 0);
            if (end == v) {
                break;
            }
            swSet((s32)flag);
            v = *end == ',' ? end + 1 : NULL;
        }
        /* the goal ($TTYD_OPEN_GOAL) and Crystal Stars had ($TTYD_OPEN_STARS, 1 to 7) */
        if ((v = getenv("TTYD_OPEN_GOAL")) != NULL) {
            gp->gsw[PORT_OPEN_GOAL] = (u8)atoi(v);
        }
        /* a file as an earlier version stamped it: its pools ($TTYD_OPEN_POOLS, the stamp's bits: 31 with the
         * key items' bit, as 0.4.x's) and its logic's version ($TTYD_OPEN_LOGIC), so its placements are as then */
        if ((v = getenv("TTYD_OPEN_POOLS")) != NULL) {
            gp->gsw[PORT_OPEN_POOLS] = (u8)strtol(v, NULL, 0);
        }
        if ((v = getenv("TTYD_OPEN_LOGIC")) != NULL) {
            gp->gsw[PORT_OPEN_LOGIC] = (u8)atoi(v);
        }
        v = getenv("TTYD_OPEN_STARS");
        while (v != NULL && *v != '\0') {
            char* end;
            const long star = strtol(v, &end, 0);
            if (end == v) {
                break;
            }
            if (star >= 1 && star <= 7) {
                pouchGetStarStone((u32)star);
            }
            v = *end == ',' ? end + 1 : NULL;
        }
    }
    /* (the stamp as written, after a scripted run's $TTYD_OPEN_GOAL, _POOLS and _LOGIC: logged before them, a
       test's goal read 0 here while the file's was 3) */
    printf("[open] file stamped: seed '%s', pools %u, goal %d, logic %d\n", seed, (unsigned)gp->gsw[PORT_OPEN_POOLS],
           (int)gp->gsw[PORT_OPEN_GOAL], (int)gp->gsw[PORT_OPEN_LOGIC]);
    fflush(stdout);
}

void portOpenNewFile(void) {
    char seed[PORT_OPEN_SEED_MAX];
    unsigned pools = 0;
    int goal = 0, armed;
    const char* test = portOpenTestSeed();
    if (test != NULL) { /* a scripted run */
        portOpenStamp(test, 0x0F, 0);
        return;
    }
    /* (online and in replays the randomizer's settings are the session's: the
     * host's, sent with the setup, or the recording's, kept in the replay; a
     * replay from before that stamps nothing: ttyd_rando.cpp) */
    armed = portOpenArmNewFile(seed, sizeof(seed), &pools, &goal);
    if (armed > 0) {
        PortOpenRules rules = {0, 0, 0, 0};
        portOpenStamp(seed, pools, goal);
        portOpenArmRules(&rules);
        portOpenStampRules(&rules);
    } else if (armed < 0) {
        /* (the reason is in the log and the Randomizer tab's "This run") */
        portSaveStateNotice(portOpenText("The open world's seed can't be made: this file is the game's own story."));
        printf("[open] the seed can't be made: a file of the game's own\n");
        fflush(stdout);
    }
}

/* $TTYD_OPEN_SELFTEST (with $TTYD_OPEN_SEED at a warp start): the items given
 * in an odd order and the chapters' stories, checked at once; PASS/FAIL lines */
static void selfCheck(int ok, const char* what) {
    printf("[open] selftest %s: %s\n", ok ? "PASS" : "FAIL", what);
    fflush(stdout);
}

void portOpenSelfTest(void) {
    char map[sizeof(gp->currentMapName)];
    u8 saved[0x800 - PORT_OPEN_GSW];
    const int raw = gp->gsw0;
    if (!portOpenOn()) {
        selfCheck(0, "the file is stamped");
        return;
    }
    memcpy(saved, &gp->gsw[PORT_OPEN_GSW], sizeof(saved));
    /* Flurrie before Koops, both from their items */
    pouchGetItem(0x70);
    selfCheck(partyChkJoin(PARTNER_FLURRIE), "Flurrie's item joins Flurrie");
    pouchGetItem(0x6F);
    selfCheck(partyChkJoin(PARTNER_KOOPS), "Koops's item joins Koops");
    selfCheck(pouchCheckItem(0x6F) == 0, "a partner's item is not kept");
    /* the third Castle Key first: it opens the first door's lock */
    pouchGetItem(ITEM_CASTLE_KEY_0x0E);
    selfCheck(pouchCheckItem(ITEM_CASTLE_KEY_0x0D) == 1 && pouchCheckItem(ITEM_CASTLE_KEY_0x0F) == 1,
              "any Castle Key is any Castle Key");
    pouchRemoveItem(ITEM_CASTLE_KEY_0x0D);
    selfCheck(pouchCheckItem(ITEM_CASTLE_KEY_0x0C) == 0, "a lock takes the one key");
    pouchGetItem(ITEM_STEEPLE_KEY_0x16);
    selfCheck(pouchCheckItem(ITEM_STEEPLE_KEY_0x17) == 1, "either Steeple Key opens the parrot's door");
    pouchRemoveItem(ITEM_STEEPLE_KEY_0x17);
    /* the Super Boots twice: the Ultra Boots */
    pouchGetItem(ITEM_SUPER_BOOTS);
    pouchGetItem(ITEM_SUPER_BOOTS);
    selfCheck(pouchCheckItem(ITEM_ULTRA_BOOTS) == 1, "a second Super Boots is the Ultra Boots");
    /* a party sent away comes back as it was: Koops yes, Yoshi (not found) no */
    portOpenPartyLeft(PARTNER_KOOPS);
    portOpenPartyLeft(PARTNER_YOSHI);
    selfCheck(portOpenPartyJoin(PARTNER_KOOPS) == 0 && portOpenPartyJoin(PARTNER_YOSHI) == 1,
              "a scene brings back only who was there");
    /* the stories: chapter 2 played first, chapter 1 untouched, Rogueport the game's */
    memcpy(map, gp->currentMapName, sizeof(map));
    strcpy(gp->currentMapName, "win_04");
    selfCheck(swByteGet(0) == STORY_STG1_END_TO_STAGE_2, "chapter 2 not begun reads as its start");
    swByteSet(0, STORY_STG2_09_FLURRIE_JOINS);
    selfCheck(swByteGet(0) == STORY_STG2_09_FLURRIE_JOINS && gp->gsw0 == raw, "chapter 2's story is its own");
    strcpy(gp->currentMapName, "hei_01");
    selfCheck(swByteGet(0) == (raw < STORY_STG0_19_BLOOPER_BATTLE ? raw : STORY_STG0_19_BLOOPER_BATTLE),
              "chapter 1 is where it starts");
    strcpy(gp->currentMapName, "sys_01");
    selfCheck(raw < STORY_STG0_19_BLOOPER_BATTLE || swByteGet(0) == STORY_STG0_19_BLOOPER_BATTLE,
              "a title card reads the chapter last played");
    strcpy(gp->currentMapName, "tou_01");
    selfCheck(raw < STORY_STG0_19_BLOOPER_BATTLE || swByteGet(0) == STORY_STG2_47_AFTER_GETTING_THE_TICKET,
              "chapter 3 not begun reads as the blimp's");
    strcpy(gp->currentMapName, "gor_00");
    selfCheck(raw < STORY_STG0_19_BLOOPER_BATTLE || pouchCheckItem(ITEM_OLD_LETTER) || swByteGet(0) == gp->gsw0,
              "the harbor is Rogueport's without the Old Letter");
    if (raw >= STORY_STG0_19_BLOOPER_BATTLE && gp->gsw0 == STORY_STG0_19_BLOOPER_BATTLE) {
        /* chapter 1 done: Rogueport's first stretch on the next room */
        setTrackValue(1, STORY_STG1_21_A_MAIL_FROM_PEACH);
        strcpy(gp->currentMapName, "gor_02");
        selfCheck(swByteGet(0) == STORY_STG1_21_A_MAIL_FROM_PEACH, "a chapter done moves Rogueport on");
        swByteSet(0, STORY_STG1_32_PUNI_ENCOUNTER_2);
        selfCheck(gp->gsw0 == STORY_STG1_END_TO_STAGE_2, "the Puni scenes are chapter 2's");
        swByteSet(0, STORY_STG2_00_BOGGLY_WOODS);
        selfCheck(gp->gsw0 == STORY_STG1_END_TO_STAGE_2, "Rogueport stops at its stretch's end");
        memcpy(&gp->gsw[PORT_OPEN_GSW], saved, sizeof(saved)); /* (chapter 1 not done again) */
        gp->gsw0 = raw;
    }
    if (raw >= STORY_STG0_19_BLOOPER_BATTLE) {
        /* the steeple: jin_11's floor and the Letter "p" don't move chapter 4 on before Vivian's scene */
        const int ch4 = trackValue(4);
        strcpy(gp->currentMapName, "jin_11");
        setTrackValue(4, STORY_STG4_09_02_BOO_BOX);
        swByteSet(0, STORY_STG4_13_03_BREAKING_THE_FLOOR);
        selfCheck(swByteGet(0) == STORY_STG4_09_02_BOO_BOX, "the steeple's floor waits for Vivian's scene");
        strcpy(gp->currentMapName, "jin_08");
        swByteSet(0, STORY_STG4_13_04_THE_LETTER_P);
        selfCheck(swByteGet(0) == STORY_STG4_09_02_BOO_BOX, "the Letter \"p\" waits for Vivian's scene");
        setTrackValue(4, STORY_STG4_13_VIVIAN_JOINS);
        swByteSet(0, STORY_STG4_13_04_THE_LETTER_P);
        selfCheck(swByteGet(0) == STORY_STG4_13_04_THE_LETTER_P, "and comes after it");
        setTrackValue(4, ch4);
    }
    strcpy(gp->currentMapName, "gor_01");
    selfCheck(swByteGet(0) == gp->gsw0, "Rogueport reads the game's own");
    /* the palace's keys: any of the three regular ones opens any lock */
    pouchGetItem(ITEM_PALACE_KEY_0x30);
    selfCheck(pouchCheckItem(ITEM_PALACE_KEY_0x2F) == 1, "any Palace Key is any Palace Key");
    pouchRemoveItem(ITEM_PALACE_KEY_0x2E);
    if (raw >= STORY_STG0_19_BLOOPER_BATTLE && chapterCount() >= 8) {
        char enter[sizeof(gp->beroEnterName)];
        memcpy(enter, gp->beroEnterName, sizeof(enter));
        /* the station: the blimp's by default, the train's off the train or at its attendant */
        strcpy(gp->currentMapName, "gor_04");
        strcpy(gp->beroEnterName, "");
        selfCheck(swByteGet(0) == trackValue(3), "the station is the blimp's");
        strcpy(gp->beroEnterName, "resha_return");
        selfCheck(portOpenTrack() == 6 && swByteGet(0) == trackValue(6), "the station is the train's off the train");
        strcpy(gp->beroEnterName, "");
        sRoomChapter = 6;
        selfCheck(portOpenTrack() == 6, "the station is the train's at its attendant");
        sRoomChapter = 3;
        selfCheck(portOpenTrack() == 3, "and the blimp's at the blimp's");
        sRoomChapter = 0;
        memcpy(gp->beroEnterName, enter, sizeof(enter));
        /* chapter 7's hunt: Petalburg reads chapter 7's, then its own again */
        setTrackValue(7, STORY_STG7_02_MAYOR_BOB);
        strcpy(gp->currentMapName, "nok_00");
        selfCheck(portOpenTrack() == 7, "General White's hunt asks in Petalburg");
        setTrackValue(7, STORY_STG7_03_WHERES_WHITE);
        selfCheck(portOpenTrack() == 1, "after the hunt Petalburg is chapter 1's");
        /* the door: Rogueport's without the goal's stars */
        strcpy(gp->currentMapName, "tik_05");
        selfCheck(starsHeld() >= goalStars() || portOpenTrack() != 8, "the door stays shut without the goal's stars");
        /* the harbor in a later stretch, chapter 5 not begun: no ferry yet */
        if (trackValue(5) <= STORY_STG4_37_TO_THE_PORT && pouchCheckItem(ITEM_OLD_LETTER) == 0) {
            gp->gsw0 = STORY_STG5_62_AN_ILL_DON_PIANTA;
            strcpy(gp->currentMapName, "gor_00");
            selfCheck(swByteGet(0) == STORY_STG4_31_FRANKLYS_HOUSE, "the harbor has no Cortez before chapter 5");
            gp->gsw0 = raw;
        }
    }
    /* the door's stars: only the ones Mario has; the pedestal's: the chapters in the order done */
    selfCheck(portOpenStarShown(7) == ((pouchGetPtr()->starPowersObtained >> 7) & 1),
              "the door shows only the stars Mario has");
    setTrackValue(3, kChapterDone[3]);
    noteDone(3);
    setTrackValue(1, kChapterDone[1]);
    noteDone(1);
    selfCheck(nthDone(1) == 3 && nthDone(2) == 1, "the pedestal's stars go in the order the chapters were done");
    memcpy(gp->currentMapName, map, sizeof(map));
    memcpy(&gp->gsw[PORT_OPEN_GSW], saved, sizeof(saved));
    pouchRemoveItem(ITEM_ULTRA_BOOTS);
    pouchRemoveItem(ITEM_SUPER_BOOTS);
}
