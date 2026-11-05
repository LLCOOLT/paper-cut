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
#include "data/item_data.h"
#include "driver/itemdrv.h"
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

int portOpenTrack(void) {
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
            gp->gsw[PORT_OPEN_STICKY] = 5;
            return 5;
        }
        return 0;
    }
    if (isMap("gor_04")) {
        const int c = stationChapter();
        gp->gsw[PORT_OPEN_STICKY] = (u8)c;
        return c;
    }
    if (isMap("tik_07")) {
        /* the pipe to Fahr Outpost; Rogueport's while its story has a scene
         * there (Peach's mail after the fourth chapter) or is due one */
        if (chapterCount() >= 7 && hubSettled()) {
            gp->gsw[PORT_OPEN_STICKY] = 7;
            return 7;
        }
        return 0;
    }
    if (isMap("tik_00") || isMap("tik_05")) {
        if (doorIsChapter8()) {
            gp->gsw[PORT_OPEN_STICKY] = 8;
            return 8;
        }
        return 0;
    }
    for (i = 0; i < sizeof(kMaps) / sizeof(kMaps[0]); i++) {
        if (isMap(kMaps[i].prefix)) {
            if (kMaps[i].chapter <= chapterCount()) {
                gp->gsw[PORT_OPEN_STICKY] = (u8)kMaps[i].chapter;
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

/* a room entered since Rogueport last looked (any room: the way back from a
 * chapter goes through its own) */
static char sLastMap[sizeof(((GlobalWork*)0)->currentMapName)];
static int sRoomEntered;

static void noteRoom(void) {
    if (memcmp(sLastMap, gp->currentMapName, sizeof(sLastMap)) != 0) {
        memcpy(sLastMap, gp->currentMapName, sizeof(sLastMap));
        sRoomEntered = 1;
        sRoomChapter = 0;
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
    if (trackValue(t) != value) {
        printf("[open] chapter %d: story %d -> %d (map '%.*s')\n", t, trackValue(t), value,
               (int)sizeof(gp->currentMapName), gp->currentMapName);
        fflush(stdout);
    }
    setTrackValue(t, value);
    return 1;
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
        static const char* const kNames[] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
        char text[96];
        partyJoin(partner);
        snprintf(text, sizeof(text), portOpenText("%s joins the party!"), kNames[partner]);
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
        return script;
    }
    if (reward >= 0x1000) {
        sLectureParty = reward - 0x1000;
        evtSetValue(entry, LW(9), reward - 0x1000); /* (that partner's own lecture, as the game plays it) */
        return script;
    }
    sLectureParty = 0;
    evtSetValue(entry, LW(10), reward);
    return open_lecture_item;
}

/* --- the party sent away and brought back --- */

int portOpenPartyLeft(int partner) {
    if (!portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE || partner < 1 || partner > 7) {
        return 0;
    }
    if (partyChkJoin((PartyId)partner)) {
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
    if (partyChkJoin((PartyId)partner)) {
        return 0;
    }
    printf("[open] partner %d not brought back: not found yet\n", partner);
    fflush(stdout);
    return 1;
}

/* --- stamping a file --- */

void portOpenStamp(const char* seed, unsigned pools, int goal) {
    size_t n = strlen(seed);
    if (gp == NULL) {
        return;
    }
    memset(&gp->gsw[PORT_OPEN_GSW], 0, 0x800 - PORT_OPEN_GSW);
    memcpy(&gp->gsw[PORT_OPEN_MAGIC], kMagic, 4);
    gp->gsw[PORT_OPEN_VERSION] = PORT_OPEN_STAGE;
    gp->gsw[PORT_OPEN_POOLS] = (u8)pools;
    gp->gsw[PORT_OPEN_GOAL] = (u8)goal;
    if (n > PORT_OPEN_SEED_MAX - 1) {
        n = PORT_OPEN_SEED_MAX - 1;
    }
    memcpy(&gp->gsw[PORT_OPEN_SEED], seed, n);
    prepareTokens();
    printf("[open] file stamped: seed '%s', pools %u, goal %d\n", seed, pools, goal);
    fflush(stdout);
}

void portOpenNewFile(void) {
    char seed[PORT_OPEN_SEED_MAX];
    unsigned pools = 0;
    int goal = 0;
    const char* test = getenv("TTYD_OPEN_SEED");
    if (test != NULL && test[0] != '\0') { /* a scripted run */
        portOpenStamp(test, 0x0F, 0);
        return;
    }
    if (portOpenArmNewFile(seed, sizeof(seed), &pools, &goal)) {
        portOpenStamp(seed, pools, goal);
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
    memcpy(gp->currentMapName, map, sizeof(map));
    memcpy(&gp->gsw[PORT_OPEN_GSW], saved, sizeof(saved));
    pouchRemoveItem(ITEM_ULTRA_BOOTS);
    pouchRemoveItem(ITEM_SUPER_BOOTS);
}
