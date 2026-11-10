/* The Practice tab's "Jump to chapter" (port/include/port/practice_jump.h): the loaded file set to a point
 * of the story, from the game's own story table (src/event.c, as its scenario select reads it), with the
 * late-game test saves' Mario, and taken to the point's first room. */
#include <stdio.h>
#include <string.h>
#include "port/practice.h"
#include "port/practice_jump.h"
#include "port/practice_tools.h"
#include "port/open_world.h"
#include "port/save_words.h"
#include "data/item_data.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_motion.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "event.h"
#include "party.h"
#include "statuswindow.h"

/* --- the game's story table ---------------------------------------------------------------------- */

struct EventData* portEventTableRow(int row, int* stageOut, int* indexOut, int* prevId) {
    int stage, idx, n = 0, prev = 0;
    for (stage = 0; stage < eventStgNum(); stage++) {
        EventStage* st = eventStgDtPtr(stage);
        for (idx = 0; idx < st->count; idx++, n++) {
            if (n == row) {
                *stageOut = stage;
                *indexOut = idx;
                *prevId = prev;
                return &st->data[idx];
            }
            prev = st->data[idx].id;
        }
    }
    return NULL;
}

int portEventTableFind(int stage, const char* textId, int nth) {
    int s, idx, n = 0;
    for (s = 0; s < eventStgNum(); s++) {
        EventStage* st = eventStgDtPtr(s);
        for (idx = 0; idx < st->count; idx++, n++) {
            if (s == stage && strcmp(st->data[idx].textId, textId) == 0 && nth-- == 0) {
                return n;
            }
        }
    }
    return -1;
}

/* Paper Cut's own words (port/include/port/save_words.h, every row of every kind): the file's, not the story's,
 * so kept across the set-up's swInit (the hardcore mark, the Keelhaul switch, Archipelago, PM64, ...) */
enum { KEPT_GSW, KEPT_GSWF, KEPT_LSW, KEPT_LSWF };
static const struct {
    int kind, first, last;
} kKept[] = {
#define KEPT_ROW_(kind, name, first, last, what) {KEPT_##kind, (first), (last)},
    PORT_SAVE_WORDS(KEPT_ROW_)
#undef KEPT_ROW_
};
#define KEPT_COUNT ((int)(sizeof(kKept) / sizeof(kKept[0])))

/* a flag's bit put back from before (the word arrays: gswf or lswf) */
static void keptFlag(u32* now, const u32* before, int i) {
    const u32 bit = 1u << (i % 32);
    now[i / 32] = (now[i / 32] & ~bit) | (before[i / 32] & bit);
}

static void swInitKeepingPorts(void) {
    static u32 sGswf[sizeof(((GlobalWork*)0)->gswf) / sizeof(u32)];
    static u32 sLswf[sizeof(((GlobalWork*)0)->lswf) / sizeof(u32)];
    static u8 sGsw[sizeof(((GlobalWork*)0)->gsw)];
    static u8 sLsw[sizeof(((GlobalWork*)0)->lsw)];
    int r, i;
    memcpy(sGswf, gp->gswf, sizeof(sGswf));
    memcpy(sLswf, gp->lswf, sizeof(sLswf));
    memcpy(sGsw, gp->gsw, sizeof(sGsw));
    memcpy(sLsw, gp->lsw, sizeof(sLsw));
    swInit();
    for (r = 0; r < KEPT_COUNT; r++) {
        for (i = kKept[r].first; i <= kKept[r].last; i++) {
            switch (kKept[r].kind) {
                case KEPT_GSW:
                    gp->gsw[i] = sGsw[i];
                    break;
                case KEPT_LSW:
                    gp->lsw[i] = sLsw[i];
                    break;
                case KEPT_GSWF:
                    keptFlag(gp->gswf, sGswf, i);
                    break;
                default:
                    keptFlag(gp->lswf, sLswf, i);
                    break;
            }
        }
    }
}

void portEventTableSetUp(const struct EventData* row, int stage, int idx, int prevId) {
    int i, j;
    MarioWork* mario;
    stg0_00_init(); /* a new file's pouch (the first row's set-up; seq_load's new game) */
    marioPartyKill();
    swInitKeepingPorts();
    swByteSet(GSW_STORY_PROGRESS, prevId);
    for (i = 0; i < stage; i++) {
        EventStage* st = eventStgDtPtr(i);
        for (j = 0; j < st->count; j++) {
            if (st->data[j].initEvent != NULL) {
                st->data[j].initEvent();
            }
        }
    }
    for (j = 0; j <= idx; j++) {
        EventData* d = &eventStgDtPtr(stage)->data[j];
        if (d->initEvent != NULL) {
            d->initEvent();
        }
    }
    if (row->motionType == 1) {
        marioChgShipMotion();
    } else {
        marioChgStayMotion();
    }
    marioPartyKill();
    mario = marioGetPtr();
    mario->prevPartyMemberId[0] = 0;
    mario->prevPartyMemberId[1] = 0;
    for (i = 0; i < 2; i++) {
        if (row->partners[i]) {
            if (row->partners[i] < 8) {
                partyJoin((PartyId)row->partners[i]); /* (the set-up functions join most; the table's own pick too) */
            }
            marioPartyEntry((PartyId)row->partners[i]);
        }
    }
}

/* --- the points ----------------------------------------------------------------------------------- */

/* Each point: its row of the table (the stage, the text id and which of that id), the room Mario goes to
 * (NULL: the row's own map and entrance, a chapter's title card on to its first map; an entrance NULL:
 * the map's start, as $TTYD_MAP), then Mario as the late-game test saves have him there (level, HP, FP,
 * BP; the partners' rank 0 Normal, 1 Super, 2 Ultra; coins) and a few recovery items. */
typedef struct Jump {
    const char* name;
    int stage;
    const char* textId;
    int nth;
    const char* map;
    const char* entrance;
    s16 level, hp, fp, bp, rank, coins;
    s16 items[6];
} Jump;

#define EARLY {ITEM_MUSHROOM, ITEM_MUSHROOM, ITEM_FIRE_FLOWER}
#define MID {ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM, ITEM_MAPLE_SYRUP, ITEM_LIFE_SHROOM}
#define LATE {ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM, ITEM_MAPLE_SYRUP, ITEM_LIFE_SHROOM, ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY}

static const Jump kJumps[PORT_JUMP_COUNT] = {
    /* the story as chapter 1's first row starts it (the Blooper beaten, STORY_STG0_END_TO_STAGE_1), in the plaza */
    {"End of the Prologue (Rogueport)", 1, "STG1_00", 0, "gor_01", NULL, 1, 10, 5, 3, 0, 100, EARLY},
    {"Start of Chapter %d", 1, "STG1_00", 0, NULL, NULL, 1, 10, 5, 3, 0, 100, EARLY},
    {"Start of Chapter %d", 2, "STG2_00", 0, NULL, NULL, 3, 15, 15, 6, 0, 150, {ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM, ITEM_HONEY_SYRUP, ITEM_FIRE_FLOWER}},
    {"Start of Chapter %d", 3, "STG3_00", 0, NULL, NULL, 6, 20, 20, 9, 0, 200, MID},
    {"Start of Chapter %d", 4, "STG4_00", 0, NULL, NULL, 9, 25, 20, 15, 1, 200, MID},
    {"Start of Chapter %d", 5, "STG5_00", 0, NULL, NULL, 12, 25, 25, 21, 1, 250, MID},
    {"Start of Chapter %d", 6, "STG6_00", 0, NULL, NULL, 15, 30, 30, 24, 1, 300, MID},
    {"Start of Chapter %d", 7, "STG7_00", 0, NULL, NULL, 18, 35, 30, 30, 2, 300, LATE},
    {"Start of Chapter %d", 8, "STG8_00", 0, NULL, NULL, 21, 40, 35, 33, 2, 400, LATE},
    /* the story as "The Return of the Queen" starts it (Grodus and Bowser beaten), in the throne room before it */
    {"Just before the Shadow Queen", 8, "STG8_16", 0, "las_28", NULL, 23, 40, 40, 36, 2, 400, LATE},
    /* "Rogueport" after the credits (the first return's scene over: STORY_STG9_02_RETURN_TO_ROGUEPORT) */
    {"Postgame (after the credits)", 9, "STG9_END", 0, NULL, NULL, 25, 45, 40, 39, 2, 999, LATE},
};

const char* portPracticeJumpName(int point) {
    return point >= 0 && point < PORT_JUMP_COUNT ? kJumps[point].name : "";
}

int portPracticeJumpChapter(int point) {
    return point >= PORT_JUMP_CHAPTER1 && point <= PORT_JUMP_CHAPTER8 ? point - PORT_JUMP_CHAPTER1 + 1 : 0;
}

const char* portPracticeJumpRefusal(void) {
    if (portPracticeEditRefusal() != NULL) {
        return portPracticeEditRefusal(); /* online, a replay */
    }
    if (!portPracticeCanEdit() || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        return "Start it in the field, with a file loaded.";
    }
    if (portPracticeModeRefusal() != NULL) {
        /* (any mode, the field's too: a Tower climb or the Pit in co-op ends on the jump's new room) */
        return portPracticeModeRefusal();
    }
    if (portOpenOn()) {
        return "Not on an open-world randomizer file.";
    }
    return NULL;
}

static int sAsked = -1; /* the point asked for, -1 none (__atomic: the page asks) */

int portPracticeJumpRequest(int point, const char** why) {
    const char* no = point >= 0 && point < PORT_JUMP_COUNT ? portPracticeJumpRefusal() : "No such chapter.";
    if (no != NULL) {
        *why = no;
        return 0;
    }
    __atomic_store_n(&sAsked, point, __ATOMIC_SEQ_CST);
    return 1;
}

/* Mario as the test saves have him at the point (the story's own badges and items kept) */
static void giveMario(const Jump* j) {
    PouchData* p = pouchGetPtr();
    s32 i;
    p->level = j->level;
    p->baseMaxHP = j->hp;
    p->baseMaxFP = j->fp;
    p->maximumBP = j->bp;
    p->availableBP = j->bp;
    for (i = 1; i < 8; i++) {
        p->partyData[i].HPLevel = j->rank;
        p->partyData[i].attackLevel = j->rank;
        p->partyData[i].techLevel = j->rank;
    }
    pouchSetCoin(j->coins);
    for (i = 0; i < 6 && j->items[i] != 0; i++) {
        pouchGetItem((ItemType)j->items[i]);
    }
    /* the story's badges (Hooktail's, ...) worn as far as the BP goes, as a player would */
    pouchReviseMarioParam();
    for (i = 0; i < MAX_BADGES; i++) {
        const s16 b = p->badges[i];
        if (b != ITEM_NULL && p->equippedBadges[i] == ITEM_NULL && p->availableBP >= itemDataTable[b].bp_cost) {
            p->equippedBadges[i] = b; /* (slot for slot with the list, as pouchEquipBadgeID) */
            pouchReviseMarioParam();
        }
    }
    pouchRevisePartyParam();
    p->currentHP = p->maximumHP;
    p->currentFP = p->maximumFP;
    p->currentSP = p->maximumSP;
    for (i = 1; i < 8; i++) {
        p->partyData[i].currentHP = p->partyData[i].maximumHP;
    }
}

static void jump(int point) {
    const Jump* j = &kJumps[point];
    const int row = portEventTableFind(j->stage, j->textId, j->nth);
    const OSTime played = gp->retraceLocalTime; /* (stg0_00_init zeroes the play clock: the file's is kept) */
    int stage, idx, prevId, i, party = 0;
    const char* map;
    const char* entrance;
    EventData* d = row >= 0 ? portEventTableRow(row, &stage, &idx, &prevId) : NULL;
    if (d == NULL) {
        printf("[practice] jump %d: no row %s in the story table\n", point, j->textId);
        fflush(stdout);
        return;
    }
    portEventTableSetUp(d, stage, idx, prevId);
    gp->retraceLocalTime = played;
    giveMario(j);
    statusWinForceUpdate();
    portPracticeTaint();
    map = j->map != NULL ? j->map : d->mapName;
    entrance = j->map != NULL ? j->entrance : d->beroName != NULL && d->beroName[0] != '\0' ? d->beroName : NULL;
    for (i = 1; i < 8; i++) {
        party |= (pouchGetPtr()->partyData[i].flags & POUCH_PARTY_FLAG_JOINED) << i;
    }
    printf("[practice] jump: point %d (row %d, %s): story %d, to %s %s; level %d, HP %d, FP %d, BP %d, boots %d, "
           "hammer %d, party 0x%02X, star powers 0x%02X\n",
           point, row, j->textId, (int)swByteGet(GSW_STORY_PROGRESS), map, entrance != NULL ? entrance : "(start)",
           pouchGetPtr()->level, pouchGetPtr()->maximumHP, pouchGetPtr()->maximumFP, pouchGetPtr()->maximumBP,
           (int)pouchGetJumpLv(), (int)pouchGetHammerLv(), party, pouchGetPtr()->starPowersObtained);
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, map, entrance);
}

void portPracticeJumpFrame(void) {
#ifdef TTYD_BACKEND_AURORA
    portPracticeJumpAskFrame();
#endif
    if (__atomic_load_n(&sAsked, __ATOMIC_SEQ_CST) < 0) {
        return;
    }
    if (portPracticeJumpRefusal() != NULL && seqGetSeq() != SEQ_MAP_CHANGE) {
        __atomic_store_n(&sAsked, -1, __ATOMIC_SEQ_CST); /* a battle, the title, online: dropped */
        return;
    }
    if (seqGetSeq() != SEQ_GAME || seqCheckSeq() || marioStGetSystemLevel() != 0) {
        return; /* a scene or a menu: wait for it */
    }
    jump(__atomic_exchange_n(&sAsked, -1, __ATOMIC_SEQ_CST));
}
