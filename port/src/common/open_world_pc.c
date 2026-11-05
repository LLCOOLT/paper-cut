/* Paper Cut's own open world, the game side (port/include/port/open_world.h,
 * docs/RANDOMIZER.md "Paper Cut's open world").
 *
 * Each chapter's story on its own. The game keeps one story value (GSW 0)
 * and every map's scripts branch on it, so in the game a chapter can only
 * start once the one before it has ended. In an open-world file the value a
 * map's scripts read and write is its chapter's own (kept in the file's GSW
 * block, PORT_OPEN_TRACKS): Petal Meadows and Hooktail's Castle read chapter
 * 1's, Boggly Woods and the Great Tree chapter 2's, whatever the others are
 * at. Rogueport and the sewers keep the game's value, which stays where the
 * prologue left it (the Blooper beaten), except for the rooms that lead into
 * a chapter (tik_02's pipe to Petal Meadows, tik_03's to Boggly Woods): they
 * read that chapter's, so the pipe goes to the chapter's title card the first
 * time and straight in after. The title card and the intermissions (Peach,
 * Bowser) read the chapter last played. A chapter not begun reads as the
 * value its first room waits for (21 for chapter 1, 70 for chapter 2), so it
 * starts as the game starts it.
 *
 * The items: a partner's item is one of the game's unused key items, named
 * and drawn as the partner; picking it up joins the partner. A partner's own
 * joining scene (all_party_lecture) gives what was placed there: another
 * partner (the scene as the game plays it, for that partner) or an item
 * (handed over as an NPC hands one). The four Castle Keys are one key: any
 * of them opens any of the castle's doors, as the logic counts them. */
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

/* --- each chapter's own story --- */

/* the maps of a chapter (by name prefix), and the rooms of the hub that lead into one */
typedef struct OpenMap {
    const char* prefix;
    int chapter;
} OpenMap;
static const OpenMap kMaps[] = {
    {"hei_", 1}, {"nok_", 1}, {"gon_", 1}, {"tik_02", 1},
    {"win_", 2}, {"mri_", 2}, {"tik_03", 2},
};
/* the story a chapter not begun reads: what its first room waits for */
static const int kChapterStart[10] = {0, STORY_STG0_19_BLOOPER_BATTLE, STORY_STG1_END_TO_STAGE_2};
/* the chapters this file's stage opens */
static const int kChapterCount = 2;

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

int portOpenTrack(void) {
    const MarioWork* m;
    size_t i;
    if (!portOpenOn() || gp->gsw0 < STORY_STG0_19_BLOOPER_BATTLE) {
        return 0; /* the prologue is the game's own */
    }
    m = marioGetPtr();
    if (m != NULL && m->characterId != 0) {
        return gp->gsw[PORT_OPEN_STICKY]; /* Peach's and Bowser's intermissions: the chapter they follow */
    }
    for (i = 0; i < sizeof(kMaps) / sizeof(kMaps[0]); i++) {
        if (strncmp(gp->currentMapName, kMaps[i].prefix, strlen(kMaps[i].prefix)) == 0) {
            if (kMaps[i].chapter <= kChapterCount) {
                gp->gsw[PORT_OPEN_STICKY] = (u8)kMaps[i].chapter;
                return kMaps[i].chapter;
            }
            return 0;
        }
    }
    if (strncmp(gp->currentMapName, "sys_", 4) == 0) {
        return gp->gsw[PORT_OPEN_STICKY]; /* a chapter's title card */
    }
    return 0;
}

int portOpenStoryGet(int raw) {
    const int t = portOpenTrack();
    return t != 0 ? trackValue(t) : raw;
}

int portOpenStorySet(int value) {
    const int t = portOpenTrack();
    if (t == 0) {
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

/* each partner's item: the game's unused key items 0x53, 0x54, 0x6F, 0x70, 0x71 (Ms. Mowz has none yet) */
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
    if (item >= ITEM_CASTLE_KEY_0x0D && item <= ITEM_CASTLE_KEY_0x0F && portOpenOn()) {
        return ITEM_CASTLE_KEY_0x0C;
    }
    return item;
}

int portOpenGetItem(int item) {
    int partner;
    if (!portOpenOn()) {
        return 0;
    }
    partner = tokenPartner(item);
    if (partner != 0) {
        static const char* const kNames[] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
        char text[64];
        partyJoin(partner);
        snprintf(text, sizeof(text), "%s joins the party!", kNames[partner]);
        portSaveStateNotice(text);
        printf("[open] partner %d joins\n", partner);
        fflush(stdout);
        return 1;
    }
    if (item >= ITEM_CASTLE_KEY_0x0D && item <= ITEM_CASTLE_KEY_0x0F) {
        return pouchGetItem(ITEM_CASTLE_KEY_0x0C); /* (a script's own key: the one key) */
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

/* --- a partner's own joining scene --- */

static void* sLecture; /* the linked area's all_party_lecture */

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
        return script;
    }
    if (reward >= 0x1000) {
        evtSetValue(entry, LW(9), reward - 0x1000); /* (that partner's own lecture, as the game plays it) */
        return script;
    }
    evtSetValue(entry, LW(10), reward);
    return open_lecture_item;
}

/* --- stamping a file --- */

void portOpenStamp(const char* seed, unsigned pools) {
    size_t n = strlen(seed);
    if (gp == NULL) {
        return;
    }
    memset(&gp->gsw[PORT_OPEN_GSW], 0, 0x800 - PORT_OPEN_GSW);
    memcpy(&gp->gsw[PORT_OPEN_MAGIC], kMagic, 4);
    gp->gsw[PORT_OPEN_VERSION] = 1;
    gp->gsw[PORT_OPEN_POOLS] = (u8)pools;
    if (n > PORT_OPEN_SEED_MAX - 1) {
        n = PORT_OPEN_SEED_MAX - 1;
    }
    memcpy(&gp->gsw[PORT_OPEN_SEED], seed, n);
    prepareTokens();
    printf("[open] file stamped: seed '%s', pools %u\n", seed, pools);
    fflush(stdout);
}

void portOpenNewFile(void) {
    char seed[PORT_OPEN_SEED_MAX];
    unsigned pools = 0;
    const char* test = getenv("TTYD_OPEN_SEED");
    if (test != NULL && test[0] != '\0') { /* a scripted run */
        portOpenStamp(test, 0x0F);
        return;
    }
    if (portOpenArmNewFile(seed, sizeof(seed), &pools)) {
        portOpenStamp(seed, pools);
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
    const int raw = gp->gsw0;
    if (!portOpenOn()) {
        selfCheck(0, "the file is stamped");
        return;
    }
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
    /* the Super Boots twice: the Ultra Boots */
    pouchGetItem(ITEM_SUPER_BOOTS);
    pouchGetItem(ITEM_SUPER_BOOTS);
    selfCheck(pouchCheckItem(ITEM_ULTRA_BOOTS) == 1, "a second Super Boots is the Ultra Boots");
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
    strcpy(gp->currentMapName, "gor_01");
    selfCheck(swByteGet(0) == raw, "Rogueport reads the game's own");
    memcpy(gp->currentMapName, map, sizeof(map));
    memset(&gp->gsw[PORT_OPEN_TRACKS], 0, 20);
    pouchRemoveItem(ITEM_ULTRA_BOOTS);
    pouchRemoveItem(ITEM_SUPER_BOOTS);
}
