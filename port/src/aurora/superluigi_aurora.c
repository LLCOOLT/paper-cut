/* Super Luigi's framework (port/include/port/superluigi.h, docs/SUPER_LUIGI.md):
 * entering and leaving a chapter with Mario's game kept safe, the campaign
 * and its own save file, the chapter card and ending (the game's own: Mario's
 * chapter card map sys_01, and the Crystal Star's "chapter clear" and diary
 * page), the friends in Mario's partner slots, their field points, the
 * shared script functions, and the pauses (quick resume, the retry slot)
 * and Play as Luigi while a chapter runs. The chapters themselves are maps
 * of the mods' host maps (port/mods/mods.cpp), like the modes'
 * (modes_aurora.c), with scripts in the game's script macros, one file each
 * (superluigi_ch01_aurora.c, ...), as the friends are (superluigi_blooey_aurora.c). */
#include "port/superluigi.h"
#include "port/superluigi_evt.h"
#include "port/sl_models.h"
#include "port/map_look.h"
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/settings.h"
#include "port/cardsave_pc.h"
#include "port/checkpoint.h"
#include "port/platform.h"
#include "battle/battle.h"
#include "battle/battle_stage_object.h"
#include "battle/battle_unit.h"
#include "data/mapdata.h"
#include "data/npc_data.h"
#include "data/party_data.h"
#include "driver/animdrv.h"
#include "driver/effdrv.h"
#include "driver/fadedrv.h"
#include "driver/hitdrv.h"
#include "evt/evt_damage.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "eff/eff_stageclear.h"
#include "event.h"
#include "evt/evt_cmd.h"
#include "evt/evt_eff.h"
#include "evt/evt_fade.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "pmario_sound.h"
#include "system.h"
#include "win/win_main.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BOOL portCardSnapshot(void* image);                  /* cardmgr.c */
void portSlBattleRegister(void);                     /* superluigi_battle_aurora.c */
BOOL portCardLoadSlotImage(s32 slot, const void* image);
void* portCardSlotPtr(s32 slot, u32* size);
void portContinueGame(s32 slot);                     /* seq_load.c */
extern BattleUnitSetup entryunit_party[7];
extern WindowPartyDt winPartyDt[7];

#define GCI_HEADER 0x40
#define CAMPAIGN_SLOT 3

/* The campaign's own save words (a campaign file is a game of its own, so
 * these are free; the game's last GSW is 1474, its last GSWF 5666) */
/* the flag bank fits the game's GSWF table and stays clear of the game's own (port/superluigi.h) */
_Static_assert(SL_FLAG_END <= sizeof(((GlobalWork*)0)->gswf) * 8, "Super Luigi's flags past the GSWF table");
_Static_assert(SL_FLAG_BASE > 5666, "Super Luigi's flags over the game's own");
/* GSW bytes: the game's own end at 1474 (GSW_TROUBLE_WORK_LAST) of 0x800 */
#define GSW_SL_CAMPAIGN 2000 /* 1: this file is a Super Luigi campaign */
#define GSW_SL_CHAPTER 2001  /* the chapter it is in */
#define GSW_SL_FRIENDS 2002  /* bit per PORT_SL_FRIEND_*: who has joined */
/* the chapters' story words (SL_WORD) above these and inside the table */
_Static_assert(SL_WORD_BASE > GSW_SL_FRIENDS, "Super Luigi's story words over the framework's");
_Static_assert(SL_WORD_BASE + SL_WORD_PER_CHAPTER * (PORT_SL_CHAPTERS + 1) <= sizeof(((GlobalWork*)0)->gsw),
               "Super Luigi's story words past the GSW table");

/* --- the chapters and friends, each in its own file (these when it isn't built) --- */

__attribute__((weak)) const PortSlChapter* portSlChapter0(void) { return NULL; }
__attribute__((weak)) const PortSlChapter* portSlChapter1(void) { return NULL; }
__attribute__((weak)) const PortSlChapter* portSlChapter2(void) { return NULL; }
__attribute__((weak)) const PortSlChapter* portSlChapter3(void) { return NULL; }
__attribute__((weak)) const PortSlChapter* portSlChapter4(void) { return NULL; }
__attribute__((weak)) const PortSlChapter* portSlChapter5(void) { return NULL; }
__attribute__((weak)) const PortSlChapter* portSlChapter6(void) { return NULL; }
__attribute__((weak)) const PortSlFriend* portSlFriendBlooey(void) { return NULL; }
__attribute__((weak)) const PortSlFriend* portSlFriendJerry(void) { return NULL; }
__attribute__((weak)) const PortSlFriend* portSlFriendTorque(void) { return NULL; }
__attribute__((weak)) const PortSlFriend* portSlFriendHayzee(void) { return NULL; }
__attribute__((weak)) const PortSlFriend* portSlFriendScreamy(void) { return NULL; }

const PortSlChapter* portSlGetChapter(int chapter) {
    static const PortSlChapter* (*const kGet[PORT_SL_CHAPTERS])(void) = {
        portSlChapter0, portSlChapter1, portSlChapter2, portSlChapter3, portSlChapter4, portSlChapter5, portSlChapter6,
    };
    return chapter >= 0 && chapter < PORT_SL_CHAPTERS ? kGet[chapter]() : NULL;
}

const PortSlFriend* portSlGetFriend(int friendId) {
    static const PortSlFriend* (*const kGet[PORT_SL_FRIENDS])(void) = {
        NULL, portSlFriendBlooey, portSlFriendJerry, portSlFriendTorque, portSlFriendHayzee, portSlFriendScreamy,
    };
    return friendId > 0 && friendId < PORT_SL_FRIENDS ? kGet[friendId]() : NULL;
}

/* Every chapter's title and tale, built or not (the Modes tab, the inn) */
static const char* const kTitles[PORT_SL_CHAPTERS] = {
    "The Letter", "Rumblebump Volcano", "Plumpbelly Village", "Circuit Break Island",
    "Jazzafrazz Town", "Rapturous Ruins", "Hatesong Tower",
};
static const char* const kTales[PORT_SL_CHAPTERS] = {
    "gor_luigi_talk_0000", "gor_luigi_talk_0001", "gor_luigi_talk_0002", "gor_luigi_talk_0003",
    "gor_luigi_talk_0004", "gor_luigi_talk_0005", "gor_luigi_talk_0006",
};

/* The inn's unlocks: the story value at which Luigi has told each tale */
static const s32 kToldAt[PORT_SL_CHAPTERS] = {
    STORY_STG1_17_FATHER_AND_SON_REUNITED, STORY_STG2_25_AFTER_THE_BOSS_BATTLE, STORY_STG3_32_BOSS_BATTLE,
    STORY_STG4_15_05_PEACH_X_NAUT_PHD_REPORT, STORY_STG5_43_X_NAUTS_RETREAT, STORY_STG6_54_WELCOME_BACK,
    STORY_STG7_20_LORD_CRUMP_DEFEATED,
};

static int sMode;     /* PORT_SL_* */
static int sChapter = -1;
static int sFriends;  /* PORT_SL_BIT per friend swapped into his slot now */
static uint8_t sReturn[PORT_CARD_SLOT_SIZE]; /* Mario's game, from the inn */
static int sReturnSlot;
static int sLuigiWas = -1; /* Play as Luigi before the chapter */
static int sInGame;        /* the campaign has been in the field since it started */
static int sSaveOnArrival; /* save the campaign once the next chapter's first room is up */

/* asked from the settings page's thread, done on the game's frame */
static int sAskCampaign, sAskChapter = -1, sAskQuit; /* __atomic */

int portSuperLuigiMode(void) {
    return sMode;
}
int portSuperLuigiActive(void) {
    return sMode != PORT_SL_NONE;
}
int portSuperLuigiChapter(void) {
    return sMode != PORT_SL_NONE ? sChapter : -1;
}
const char* portSuperLuigiChapterTitle(int chapter) {
    const PortSlChapter* c = portSlGetChapter(chapter);
    if (c != NULL && c->title != NULL) {
        return c->title;
    }
    return chapter >= 0 && chapter < PORT_SL_CHAPTERS ? kTitles[chapter] : "";
}
int portSuperLuigiChapterBuilt(int chapter) {
    const PortSlChapter* c = portSlGetChapter(chapter);
    return c != NULL && c->firstMap != NULL;
}
int portSuperLuigiChapterTold(int chapter) {
    return chapter >= 0 && chapter < PORT_SL_CHAPTERS && gp != NULL && swByteGet(GSW_STORY_PROGRESS) >= kToldAt[chapter];
}
int portSuperLuigiOwnText(const char* tag) {
    /* sl_..., sl0_..., sl1_...: Super Luigi's own tags */
    return sMode != PORT_SL_NONE && tag != NULL && tag[0] == 's' && tag[1] == 'l' &&
           (tag[2] == '_' || (tag[2] >= '0' && tag[2] <= '9'));
}
int portSuperLuigiNoSetup(const char* map) {
    int c;
    for (c = 0; c < PORT_SL_CHAPTERS; c++) {
        const PortSlChapter* ch = portSlGetChapter(c);
        const PortSlMap* m;
        for (m = ch != NULL ? ch->maps : NULL; m != NULL && m->map != NULL; m++) {
            if (strcmp(m->map, map) == 0) {
                return m->noSetup;
            }
        }
    }
    return 0;
}

static void campaignPath(char* out, size_t size) {
    snprintf(out, size, "%s/superluigi_%s.gci", portSettingsUserPath(), PORT_GAME_ID);
}

int portSuperLuigiCampaignSaved(void) {
    char path[1024];
    FILE* f;
    campaignPath(path, sizeof(path));
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

/* --- Play as Luigi --- */

static void forceLuigi(int on) {
    int* v = portSettingsFind("playable_luigi");
    if (v == NULL) {
        return;
    }
    if (on) {
        if (sLuigiWas < 0) {
            sLuigiWas = *v;
        }
        *v = 1;
    } else if (sLuigiWas >= 0) {
        *v = sLuigiWas;
        sLuigiWas = -1;
    }
}

/* --- the friends in Mario's partner slots --- */

/* Each slot's places: its battle unit (entryunit_party), party-menu entry
 * (winPartyDt), and the texts that name it */
static const struct {
    int partyId, entry, menu;
    const char* name, *battleName, *menuText, *changeName, *changeHelp;
} kSlots[] = {
    {1, 0, 0, "name_party0", "btl_un_christine", "msg_menu_party_kuri", "btl_cmd_pt_christine", "btl_hlp_cmd_operation_party1"},
    {2, 1, 1, "name_party1", "btl_un_nokotarou", "msg_menu_party_noko", "btl_cmd_pt_nokotarou", "btl_hlp_cmd_operation_party2"},
    {3, 5, 5, "name_party2", "btl_un_sanders", "msg_menu_party_bom", "btl_cmd_pt_sanders", "btl_hlp_cmd_operation_party6"},
    {5, 3, 2, "name_party4", "btl_un_clauda", "msg_menu_party_cloud", "btl_cmd_pt_clauda", "btl_hlp_cmd_operation_party3"},
    {6, 4, 4, "name_party5", "btl_un_vivian", "msg_menu_party_vivi", "btl_cmd_pt_vivian", "btl_hlp_cmd_operation_party5"},
    {7, 6, 6, "name_party6", "btl_un_chuchurina", "msg_menu_party_chu", "btl_cmd_pt_chuchurina", "btl_hlp_cmd_operation_party7"},
};
#define SLOTS ((int)(sizeof(kSlots) / sizeof(kSlots[0])))

/* what a swap replaced, per slot */
static struct {
    const PortSlFriend* in;
    PartyData field;
    char* poses[15];
    BattleUnitKind* kind;
    WindowPartyDt menu;
} sSlot[SLOTS];

static int slotIndex(int partyId) {
    int i;
    for (i = 0; i < SLOTS; i++) {
        if (kSlots[i].partyId == partyId) {
            return i;
        }
    }
    return -1;
}

const PortSlFriend* portSlFriendInSlot(int partyId) {
    const int i = slotIndex(partyId);
    return i >= 0 ? sSlot[i].in : NULL;
}

static void swapIn(const PortSlFriend* f) {
    const int i = slotIndex(f->partyId);
    BattleUnitKind* kind;
    if (i < 0 || sSlot[i].in != NULL) {
        return;
    }
    sSlot[i].in = f;
    sSlot[i].field = partyDataTbl[f->partyId];
    memcpy(sSlot[i].poses, partyPoseTbl[f->partyId], sizeof(sSlot[i].poses));
    sSlot[i].kind = entryunit_party[kSlots[i].entry].unitKindParams;
    sSlot[i].menu = winPartyDt[kSlots[i].menu];
    partyDataTbl[f->partyId].name = (char*)f->model;
    partyDataTbl[f->partyId].use_func = f->fieldUse;
    partyDataTbl[f->partyId].post_func = f->fieldPost;
    if (f->fieldPoses != NULL) {
        memcpy(partyPoseTbl[f->partyId], f->fieldPoses, sizeof(sSlot[i].poses));
    }
    kind = f->battleUnit != NULL ? f->battleUnit() : NULL;
    if (kind != NULL) {
        entryunit_party[kSlots[i].entry].unitKindParams = kind;
    }
    winPartyDt[kSlots[i].menu].poseName = f->model;
    winPartyDt[kSlots[i].menu].animName = f->menuPose != NULL ? f->menuPose : "S_1";
    if (f->menuMoves != NULL) {
        winPartyDt[kSlots[i].menu].waza = f->menuMoves;
    }
    portModsSetMessage(kSlots[i].name, f->name);
    portModsSetMessage(kSlots[i].battleName, f->name);
    portModsSetMessage(kSlots[i].changeName, f->name);
    if (f->menuText != NULL) {
        portModsSetMessage(kSlots[i].menuText, f->menuText);
        portModsSetMessage(kSlots[i].changeHelp, f->menuText); /* the battle's partner switch */
    }
    if (f->swapped != NULL) {
        f->swapped(1);
    }
    printf("[superluigi] %s in partner slot %d\n", f->name, f->partyId);
    fflush(stdout);
}

static void swapOut(int i) {
    const PortSlFriend* f = sSlot[i].in;
    if (f == NULL) {
        return;
    }
    partyDataTbl[f->partyId] = sSlot[i].field;
    memcpy(partyPoseTbl[f->partyId], sSlot[i].poses, sizeof(sSlot[i].poses));
    entryunit_party[kSlots[i].entry].unitKindParams = sSlot[i].kind;
    winPartyDt[kSlots[i].menu] = sSlot[i].menu;
    portModsSetMessage(kSlots[i].name, NULL);
    portModsSetMessage(kSlots[i].battleName, NULL);
    portModsSetMessage(kSlots[i].changeName, NULL);
    portModsSetMessage(kSlots[i].menuText, NULL);
    portModsSetMessage(kSlots[i].changeHelp, NULL);
    sSlot[i].in = NULL;
    if (f->swapped != NULL) {
        f->swapped(0);
    }
    printf("[superluigi] %s out of partner slot %d\n", f->name, f->partyId);
    fflush(stdout);
}

/* The friends in their slots: exactly those of mask */
static void swapFriends(int mask) {
    int id, i;
    for (i = 0; i < SLOTS; i++) {
        if (sSlot[i].in != NULL && !(mask & PORT_SL_BIT(sSlot[i].in->id))) {
            swapOut(i);
        }
    }
    for (id = 1; id < PORT_SL_FRIENDS; id++) {
        const PortSlFriend* f = portSlGetFriend(id);
        if ((mask & PORT_SL_BIT(id)) && f != NULL) {
            swapIn(f);
        }
    }
    sFriends = mask;
}

/* The friends whose slots are swapped while a chapter runs: those who have
 * joined, those coming in with Luigi, and the chapter's own */
static int wantedFriends(void) {
    const PortSlChapter* c = portSlGetChapter(sChapter);
    if (sMode == PORT_SL_NONE || c == NULL) {
        return 0;
    }
    return (gp != NULL ? swByteGet(GSW_SL_FRIENDS) : 0) | c->party | (c->friendId ? PORT_SL_BIT(c->friendId) : 0);
}

/* A friend in the party: in his slot, with his HP and rank, joined */
static void friendJoin(int id) {
    const PortSlFriend* f = portSlGetFriend(id);
    PouchPartyData* d;
    if (f == NULL) {
        return;
    }
    swByteSet(GSW_SL_FRIENDS, swByteGet(GSW_SL_FRIENDS) | PORT_SL_BIT(id));
    swapFriends(sFriends | PORT_SL_BIT(id));
    d = &pouchGetPtr()->partyData[f->partyId];
    if (!(d->flags & POUCH_PARTY_FLAG_JOINED)) {
        d->baseMaxHP = d->maximumHP = d->currentHP = f->hp > 0 ? f->hp : 10;
        d->attackLevel = f->rank;
        d->HPLevel = 0;
        d->techLevel = f->rank;
        partyJoin((PartyId)f->partyId);
    }
}

/* --- saving --- */

static int writeGci(const char* path, const uint8_t* image) {
    static uint8_t header[GCI_HEADER];
    static uint8_t meta[PORT_CARD_META_SIZE];
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    memset(header, 0, sizeof(header));
    memcpy(header, PORT_GAME_ID, 6);
    fwrite(header, 1, sizeof(header), f);
    fwrite(meta, 1, sizeof(meta), f);
    fwrite(image, 1, PORT_CARD_SLOT_SIZE, f);
    fclose(f);
    return 1;
}

int portSuperLuigiCardWrite(int slot) {
    static uint8_t image[PORT_CARD_SLOT_SIZE];
    char path[1024];
    (void)slot;
    if (sMode == PORT_SL_INN) {
        printf("[superluigi] a save from the inn's chapter: not kept\n");
        fflush(stdout);
        return 1;
    }
    if (sMode != PORT_SL_CAMPAIGN) {
        return 0;
    }
    gp->saveCount++;
    if (portCardSnapshot(image)) {
        campaignPath(path, sizeof(path));
        writeGci(path, image);
        printf("[superluigi] campaign saved (chapter %d, %s)\n", sChapter, gp->currentMapName);
        fflush(stdout);
    }
    return 1;
}

/* A card image loaded as the running game, the way the file select
 * continues one, without leaving it in the card's copy of that slot. */
static void continueFrom(const uint8_t* image, s32 slot) {
    static uint8_t keep[0x8000];
    u32 size = 0;
    void* live = portCardSlotPtr(slot, &size);
    int kept = live != NULL && size <= sizeof(keep);
    if (kept) {
        memcpy(keep, live, size);
    }
    if (portCardLoadSlotImage(slot, image)) {
        portContinueGame(slot);
    }
    if (kept) {
        memcpy(live, keep, size);
    }
}

/* --- the chapter card (sys_01, Mario's) --- */

/* The card's subtitles (sys.c's msg_tbl): the chapter's title while it runs */
static void cardText(int chapter) {
    static const char* const kTags[] = {"msg_stg_seq_prolouge", "msg_stg_seq_stg1", "msg_stg_seq_stg2",
                                        "msg_stg_seq_stg3", "msg_stg_seq_stg4", "msg_stg_seq_stg5",
                                        "msg_stg_seq_stg6"};
    int i;
    for (i = 0; i < PORT_SL_CHAPTERS; i++) {
        portModsSetMessage(kTags[i], chapter == i ? portSuperLuigiChapterTitle(i) : NULL);
    }
}

/* To the chapter card: "Prologue" / "Chapter N" and the title, then on to
 * the chapter's first room (evt_sl_title_next) */
static void toCard(int chapter) {
    static char bero[16];
    cardText(chapter);
    if (chapter == 0) {
        snprintf(bero, sizeof(bero), "prologue");
    } else {
        snprintf(bero, sizeof(bero), "stg_%d", chapter);
    }
    seqSetSeq(SEQ_MAP_CHANGE, "sys_01", bero);
}

/* --- the chapters --- */

/* Luigi as he is when he tells the tale (the inn's way in): his own pouch, and the friends with him */
static void innLoadout(const PortSlChapter* c) {
    PouchData* p;
    const short* it;
    int id;
    pouchInit();
    p = pouchGetPtr();
    p->level = c->level;
    p->baseMaxHP = p->maximumHP = p->currentHP = c->hp;
    p->baseMaxFP = p->maximumFP = p->currentFP = c->fp;
    p->maximumBP = p->availableBP = c->bp;
    p->jumpLevel = (s8)c->boots;
    p->hammerLevel = (s8)c->hammer;
    /* the levels are worked out again from these key items (pouchGetJumpLv, pouchGetHammerLv): without
     * them a chapter's Super Boots dropped away at the first check, and no Spin Jump with them */
    {
        static const short kBoots[] = {0, ITEM_BOOTS, ITEM_SUPER_BOOTS, ITEM_ULTRA_BOOTS};
        static const short kHammers[] = {0, ITEM_HAMMER, ITEM_SUPER_HAMMER, ITEM_ULTRA_HAMMER};
        if (c->boots >= 1 && c->boots <= 3 && !pouchCheckItem(kBoots[c->boots])) {
            pouchGetItem((ItemType)kBoots[c->boots]);
        }
        if (c->hammer >= 1 && c->hammer <= 3 && !pouchCheckItem(kHammers[c->hammer])) {
            pouchGetItem((ItemType)kHammers[c->hammer]);
        }
    }
    p->coins = c->coins;
    p->starPowersObtained = 0;
    p->maximumSP = p->currentSP = 0;
    for (it = c->items; it != NULL && *it != 0; it++) {
        pouchGetItem((ItemType)*it);
    }
    for (it = c->badges; it != NULL && *it != 0; it++) {
        pouchGetItem((ItemType)*it);
    }
    swByteSet(GSW_SL_FRIENDS, 0);
    for (id = 1; id < PORT_SL_FRIENDS; id++) {
        if (c->party & PORT_SL_BIT(id)) {
            friendJoin(id);
        }
    }
}

static void enter(int chapter, int mode, int card) {
    const PortSlChapter* c = portSlGetChapter(chapter);
    sInGame = 0;
    sMode = mode;
    sChapter = chapter;
    forceLuigi(1);
    swByteSet(GSW_SL_CAMPAIGN, mode == PORT_SL_CAMPAIGN);
    swByteSet(GSW_SL_CHAPTER, chapter);
    marioPartyKill();
    {
        MarioWork* m = marioGetPtr();
        m->prevPartyMemberId[0] = 0;
        m->prevPartyMemberId[1] = 0;
    }
    swapFriends(wantedFriends());
    if (seqGetSeq() == SEQ_TITLE) {
        /* from the title: its fades go, the curtain closes, as a file select's continue does */
        s32 i;
        for (i = 0; i < 5; i++) {
            fadeReset(i);
        }
        fadeEntry(FADE_CURTAIN_SIDES_A1, 0, (GXColor){0, 0, 0, 255});
    }
    gp->areaFadeInType = FADE_NONE;
    gp->areaFadeOutType = FADE_CURTAIN_SIDES_A2;
    printf("[superluigi] chapter %d (%s) from %s\n", chapter, c->title, mode == PORT_SL_INN ? "the inn" : "the campaign");
    fflush(stdout);
    if (card) {
        toCard(chapter);
    } else {
        cardText(chapter);
        seqSetSeq(SEQ_MAP_CHANGE, c->firstMap, c->firstBero);
    }
}

/* From Rogueport's inn: Mario's game kept in memory, Luigi's own pouch */
static char sKeptMap[16]; /* where Mario stood as he left for the chapter */
static Vec sKeptPos;
static int sKeptPending;

static int enterFromInn(int chapter, int card) {
    if (!portSuperLuigiChapterBuilt(chapter) || !portCardSnapshot(sReturn)) {
        return 0;
    }
    sReturnSlot = gp->saveSlot >= 0 && gp->saveSlot < 4 ? gp->saveSlot : 0;
    snprintf(sKeptMap, sizeof(sKeptMap), "%s", gp->currentMapName);
    sKeptPos = marioGetPtr()->position;
    sMode = PORT_SL_INN; /* the friends' slots, as the loadout joins them */
    sChapter = chapter;
    innLoadout(portSlGetChapter(chapter));
    enter(chapter, PORT_SL_INN, card);
    return 1;
}

static void leave(void) {
    sInGame = 0;
    printf("[superluigi] leaving chapter %d\n", sChapter);
    fflush(stdout);
    swapFriends(0);
    forceLuigi(0);
    cardText(-1);
    portSlForceCommand(0, -1, NULL, NULL);
    portSlForceCommand(1, -1, NULL, NULL);
    portSlPrompt(NULL, 0);
    sMode = PORT_SL_NONE;
    sChapter = -1;
}

/* Back to Mario's game at the inn, as it was */
/* Back at the inn, Mario exactly where he stood: a continue puts him on the
 * file's spot, then the partner, put out beside him, nudges him a few units
 * (as any load of a file does: Rogueport's inn file saved at x 171.5 loads at
 * 167). Once the room is up he's put back on the spot he left from. */
static void restoreSpot(int seq) {
    static int sFrames;
    if (!sKeptPending || gp == NULL || seq != SEQ_GAME || strcmp(gp->currentMapName, sKeptMap) != 0) {
        sFrames = 0;
        return;
    }
    if (++sFrames < 10) {
        return;
    }
    marioGetPtr()->position = sKeptPos;
    sKeptPending = 0;
    printf("[superluigi] back in %s at %.1f %.1f %.1f\n", sKeptMap, sKeptPos.x, sKeptPos.y, sKeptPos.z);
    fflush(stdout);
}

static void returnToInn(void) {
    leave();
    continueFrom(sReturn, sReturnSlot);
    sKeptPending = 1;
}

static void newCampaign(void) {
    if (sMode == PORT_SL_INN) {
        leave();
    }
    stg0_00_init();
    swInit();
    pouchGetPtr()->jumpLevel = 1;
    pouchGetPtr()->hammerLevel = 1;
    swByteSet(GSW_STORY_PROGRESS, STORY_STG0_00_PROLOGUE);
    strcpy((char*)gp->playerName, "Luigi");
    gp->saveSlot = CAMPAIGN_SLOT;
    enter(0, PORT_SL_CAMPAIGN, 1);
}

/* A new campaign from a later chapter's card, with that chapter's own pouch and
 * party (the inn's loadout); on from there as the campaign goes */
static void campaignAt(int chapter) {
    const PortSlChapter* c = portSlGetChapter(chapter);
    if (chapter <= 0 || c == NULL || !portSuperLuigiChapterBuilt(chapter)) {
        newCampaign();
        return;
    }
    if (sMode == PORT_SL_INN) {
        leave();
    }
    stg0_00_init();
    swInit();
    swByteSet(GSW_STORY_PROGRESS, STORY_STG0_00_PROLOGUE);
    strcpy((char*)gp->playerName, "Luigi");
    gp->saveSlot = CAMPAIGN_SLOT;
    sMode = PORT_SL_CAMPAIGN; /* the friends' slots, as the loadout joins them */
    sChapter = chapter;
    innLoadout(c);
    enter(chapter, PORT_SL_CAMPAIGN, 1);
    sSaveOnArrival = 1;
}

static int continueCampaign(void) {
    static uint8_t image[PORT_CARD_SLOT_SIZE];
    char path[1024];
    FILE* f;
    long size;
    campaignPath(path, sizeof(path));
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, GCI_HEADER + (long)PORT_CARD_META_SIZE, SEEK_SET);
    if (size < GCI_HEADER + (long)PORT_CARD_META_SIZE + (long)PORT_CARD_SLOT_SIZE ||
        fread(image, 1, sizeof(image), f) != sizeof(image)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    if (sMode == PORT_SL_INN) {
        leave();
    }
    sMode = PORT_SL_CAMPAIGN;
    sChapter = 0;
    sInGame = 0;
    forceLuigi(1);
    continueFrom(image, CAMPAIGN_SLOT);
    /* the chapter and friends as the save has them */
    sChapter = swByteGet(GSW_SL_CHAPTER);
    if (!portSuperLuigiChapterBuilt(sChapter)) {
        sChapter = 0;
    }
    cardText(sChapter);
    swapFriends(wantedFriends());
    {
        /* the file select's continue (continueGame) put the saved partner out
         * before the slots were swapped: Goombella's model following Luigi in
         * Blooey's slot. Out again, as the friend now in it */
        MarioWork* m = marioGetPtr();
        marioPartyKill();
        m->prevPartyMemberId[0] = 0;
        m->prevPartyMemberId[1] = 0;
        psndSetFlag(PSND_FLAG_SFX_OFF);
        marioPartyEntry(gp->savePartyId[0]);
        marioPartyEntry(gp->savePartyId[1]);
        psndClearFlag(PSND_FLAG_SFX_OFF);
    }
    printf("[superluigi] campaign continued: chapter %d, %s\n", sChapter, gp->currentMapName);
    fflush(stdout);
    return 1;
}

/* Test checkpoints (port/src/common/checkpoint_pc.c): the mode, the chapter and, from the inn, Mario's game */
void portSlCheckpointGet(PortSlCheckpoint* out) {
    out->mode = sMode;
    out->chapter = sChapter;
    out->haveReturn = sMode == PORT_SL_INN;
    out->returnSlot = sReturnSlot;
    if (out->haveReturn && out->returnImage != NULL) {
        memcpy(out->returnImage, sReturn, sizeof(sReturn));
    }
    snprintf(out->keptMap, sizeof(out->keptMap), "%s", sKeptMap);
    out->keptPos[0] = sKeptPos.x;
    out->keptPos[1] = sKeptPos.y;
    out->keptPos[2] = sKeptPos.z;
}

/* as continueCampaign, from a checkpoint's save: the chapter as it was, its friends in their slots */
void portSlCheckpointResume(const PortSlCheckpoint* ck, const uint8_t* image, int slot) {
    if (sMode != PORT_SL_NONE) {
        leave();
    }
    if (ck->mode == PORT_SL_NONE) {
        continueFrom(image, slot);
        return;
    }
    sMode = ck->mode;
    sChapter = ck->chapter;
    sInGame = 0;
    sSaveOnArrival = 0;
    if (ck->mode == PORT_SL_INN && ck->haveReturn && ck->returnImage != NULL) {
        memcpy(sReturn, ck->returnImage, sizeof(sReturn));
        sReturnSlot = ck->returnSlot;
        snprintf(sKeptMap, sizeof(sKeptMap), "%s", ck->keptMap);
        sKeptPos = (Vec){ck->keptPos[0], ck->keptPos[1], ck->keptPos[2]};
    }
    forceLuigi(1);
    continueFrom(image, slot);
    cardText(sChapter);
    swapFriends(wantedFriends());
    {
        /* the friend now in the saved partner's slot, out (as continueCampaign) */
        MarioWork* m = marioGetPtr();
        marioPartyKill();
        m->prevPartyMemberId[0] = 0;
        m->prevPartyMemberId[1] = 0;
        psndSetFlag(PSND_FLAG_SFX_OFF);
        marioPartyEntry(gp->savePartyId[0]);
        marioPartyEntry(gp->savePartyId[1]);
        psndClearFlag(PSND_FLAG_SFX_OFF);
    }
    printf("[superluigi] checkpoint: chapter %d from %s\n", sChapter, sMode == PORT_SL_INN ? "the inn" : "the campaign");
    fflush(stdout);
}

int portSuperLuigiStartCampaign(int fresh, const char** why) {
    if (gp == NULL || (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_TITLE) || gp->inBattle) {
        *why = "Start it from the title screen or the field.";
        return 0;
    }
    if (!fresh && !portSuperLuigiCampaignSaved()) {
        *why = "There's no campaign saved yet.";
        return 0;
    }
    __atomic_store_n(&sAskCampaign, fresh ? 1 : 2, __ATOMIC_SEQ_CST);
    return 1;
}

int portSuperLuigiStartCampaignAt(int chapter, const char** why) {
    if (gp == NULL || (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_TITLE) || gp->inBattle) {
        *why = "Start it from the title screen or the field.";
        return 0;
    }
    if (!portSuperLuigiChapterBuilt(chapter)) {
        *why = "That chapter isn't built yet.";
        return 0;
    }
    __atomic_store_n(&sAskCampaign, 10 + chapter, __ATOMIC_SEQ_CST);
    return 1;
}

int portSuperLuigiPlayChapter(int chapter, const char** why) {
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Play it in the field, with a file loaded.";
        return 0;
    }
    if (sMode != PORT_SL_NONE) {
        *why = "A chapter is already running.";
        return 0;
    }
    if (!portSuperLuigiChapterBuilt(chapter)) {
        *why = "That chapter isn't built yet.";
        return 0;
    }
    if (!portSuperLuigiChapterTold(chapter)) {
        *why = "Luigi hasn't told that part of his story yet.";
        return 0;
    }
    __atomic_store_n(&sAskChapter, chapter, __ATOMIC_SEQ_CST);
    return 1;
}

void portSuperLuigiQuit(void) {
    __atomic_store_n(&sAskQuit, 1, __ATOMIC_SEQ_CST);
}

/* The chapter's end: from the inn, back to the table; in the campaign, on to
 * the next chapter's card (saved as it arrives), or the title after the last one built. */
static void chapterDone(void) {
    /* "Chapter Clear!" stays up until it's deleted: Mario's chapters go on to
     * a map change, which clears it, but the campaign's last chapter goes to
     * the title, where it stayed over the title and the file select */
    EffectEntry* clear = effNameToPtr("sl_clear");
    if (clear != NULL) {
        effDelete(clear);
    }
    if (sMode == PORT_SL_INN) {
        returnToInn();
        return;
    }
    if (sMode == PORT_SL_CAMPAIGN) {
        int next = sChapter + 1;
        if (next < PORT_SL_CHAPTERS && portSuperLuigiChapterBuilt(next)) {
            sChapter = next;
            swByteSet(GSW_SL_CHAPTER, next);
            swapFriends(wantedFriends());
            gp->areaFadeInType = FADE_NONE;
            gp->areaFadeOutType = FADE_CURTAIN_SIDES_A2;
            toCard(next);
            sSaveOnArrival = 1; /* saved there, so a continue starts the new chapter */
        } else {
            portSuperLuigiCardWrite(CAMPAIGN_SLOT);
            leave();
            seqSetSeq(SEQ_TITLE, NULL, NULL);
        }
    }
}

/* ======================================================================
 * The scripts' functions
 * ====================================================================== */

/* in: the tale's message (gor.c, after it's told); out: its chapter, or -1 */
USER_FUNC(evt_sl_tale_chapter) {
    EvtWord* args = event->args;
    const char* tale = (const char*)evtGetValue(event, args[0]);
    s32 i, chapter = -1;
    (void)isFirstCall;
    for (i = 0; i < PORT_SL_CHAPTERS && tale != NULL; i++) {
        if (strcmp(kTales[i], tale) == 0 && portSuperLuigiChapterBuilt(i)) {
            chapter = i;
        }
    }
    evtSetValue(event, args[1], chapter);
    return EVT_RETURN_DONE;
}

/* in: the chapter: played from the inn */
USER_FUNC(evt_sl_enter) {
    (void)isFirstCall;
    enterFromInn(evtGetValue(event, event->args[0]), 1);
    return EVT_RETURN_DONE;
}

/* sys.c: out: the map and entrance the chapter card goes on to, or 0 for the game's own */
USER_FUNC(evt_sl_title_next) {
    const PortSlChapter* c = portSlGetChapter(sChapter);
    (void)isFirstCall;
    if (sMode == PORT_SL_NONE || c == NULL || c->firstMap == NULL) {
        evtSetValue(event, event->args[0], 0);
        return EVT_RETURN_DONE;
    }
    evtSetValue(event, event->args[0], (EvtWord)(AddrWord)c->firstMap);
    evtSetValue(event, event->args[1], (EvtWord)(AddrWord)c->firstBero);
    return EVT_RETURN_DONE;
}

/* The chapter's title card: the game's own chapter card now plays as the
 * chapter is entered (sys_01, before its first room), so this does nothing;
 * kept for the scripts that call it */
USER_FUNC(evt_sl_title_card) {
    (void)event;
    (void)isFirstCall;
    return EVT_RETURN_DONE;
}

static int sEnding; /* the ending's script is running */

static USER_FUNC(sl_end_clear_wait) {
    (void)event;
    (void)isFirstCall;
    return effStageClearEndChk(effNameToPtr("sl_clear")) ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* out: the chapter's diary page (a tag), or 0 */
static USER_FUNC(sl_end_diary) {
    const PortSlChapter* c = portSlGetChapter(sChapter);
    (void)isFirstCall;
    evtSetValue(event, event->args[0], c != NULL && c->diary != NULL ? (EvtWord)(AddrWord)c->diary : 0);
    return EVT_RETURN_DONE;
}

static USER_FUNC(sl_end_done) {
    (void)event;
    (void)isFirstCall;
    sEnding = 0;
    return EVT_RETURN_DONE;
}

/* The chapter's end, as Mario's chapters end when the Crystal Star is won
 * (evt_sub.c, evt_sub_starstone): the stage-clear music, the curtain comes
 * down behind Luigi, "Chapter Clear!" and his cheer, the curtain closes and
 * the chapter's diary page sums it up */
static EVT_BEGIN(sl_chapter_end_evt)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_party_stop, 0)
    /* a beat after the last line, the music going, and the camera closes in on him as it does on Mario
     * with the star: he's centre stage when the curtain comes down, and the room's edges are out of sight */
    CALL(evt_snd_bgmoff_f, PSND_FADE_OUT, 1000)
    /* a chapter that ends on someone else's scene parks Luigi under the map (chapter 3's tag: Torque in
     * his spotlight, Luigi at y -1000): there the scene's own camera stays, and the curtains are the screen's
     * (LW(3) 1), since the 3D ones come down where Luigi is, under the floor, and the screen only went dark */
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    SET_VALUE(LW(3), 1)
    IF_INT_GREATER(LW(1), -500)
        SET_VALUE(LW(3), 0)
        CALL(evt_mario_adjust_dir)
        CALL(evt_cam3d_evt_set_rel, 0, 50, 397, 0, 70, 25, 1000, 11)
    END_IF
    WAIT_MS(1200)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_HAPPY1"), LW(0), LW(1), LW(2), 0)
    CALL(evt_mario_set_pose, STRING("M_P_2"))
    CALL(evt_snd_bgmon, PSND_FADE_OUT, STRING("BGM_EVT_STAGE_CLEAR1"))
    CALL(evt_snd_envoff, PSND_FADE_OUT)
    IF_INT_EQUAL(LW(3), 0)
        CALL(evt_fade_entry, FADE_CURTAIN_ENTER_3D, 0, 0, 0, 0)
    ELSE
        CALL(evt_fade_entry, FADE_CURTAIN_ENTER, 0, 0, 0, 0)
    END_IF
    CALL(evt_fade_end_wait)
    WAIT_MS(1000)
    CALL(evt_eff, STRING("sl_clear"), STRING("stageclear"), 10, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    CALL(sl_end_clear_wait)
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_VOICE_MARIO_HAPPY2"), LW(0), LW(1), LW(2), 0)
    WAIT_MS(2000)
    IF_INT_EQUAL(LW(3), 0)
        CALL(evt_fade_entry, FADE_CURTAIN_CENTER_A2_3D, 0, 0, 0, 0)
    ELSE
        CALL(evt_fade_entry, FADE_CURTAIN_CENTER_A2, 0, 0, 0, 0)
    END_IF
    CALL(evt_fade_end_wait)
    WAIT_MS(1000)
    CALL(sl_end_diary, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_msg_print, 0, LW(0), 0, 0)
    END_IF
    WAIT_MS(500)
    CALL(sl_end_done)
    RETURN
EVT_END()

/* The chapter's ending, then on (chapterDone): blocks while it plays */
USER_FUNC(evt_sl_chapter_end) {
    (void)event;
    if (isFirstCall) {
        sEnding = 1;
        printf("[superluigi] chapter %d complete\n", sChapter);
        fflush(stdout);
        /* of the caller's own type: from a chest's event (a map object's, run with the
         * field held at system level 1) a plain evtEntry's script would stay stopped */
        evtEntryType(sl_chapter_end_evt, event->priority, 0, event->type);
    }
    if (sEnding) {
        return EVT_RETURN_BLOCK;
    }
    chapterDone();
    return EVT_RETURN_DONE;
}

/* out: whether Super Luigi runs from the inn (1) or the campaign (2) */
USER_FUNC(evt_sl_mode) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], sMode);
    return EVT_RETURN_DONE;
}

/* in: a friend (PORT_SL_FRIEND_*) joins: in his slot, in the party, from now on */
USER_FUNC(evt_sl_friend_join) {
    (void)isFirstCall;
    friendJoin((int)evtGetValue(event, event->args[0]));
    return EVT_RETURN_DONE;
}

/* in: a friend; out: the partner slot he takes (for evt_mario_set_party_pos and the like) */
USER_FUNC(evt_sl_friend_slot) {
    const PortSlFriend* f = portSlGetFriend((int)evtGetValue(event, event->args[0]));
    (void)isFirstCall;
    evtSetValue(event, event->args[1], f != NULL ? f->partyId : 0);
    return EVT_RETURN_DONE;
}

/* in: a label: Luigi's position and LW(0)-LW(2) to the log (building the rooms) */
USER_FUNC(evt_sl_where) {
    MarioWork* m = marioGetPtr();
    (void)isFirstCall;
    printf("[superluigi] %s: %s luigi %.0f %.0f %.0f lw %d %d %d\n", (const char*)evtGetValue(event, event->args[0]),
           gp->currentMapName, m->position.x, m->position.y, m->position.z, (int)event->lwData[0],
           (int)event->lwData[1], (int)event->lwData[2]);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* in: an NPC, a point (x, y, z) and a time (ms): it flies there in a smooth
 * arc-less glide, whatever its kind (the game's glide wants a tribe's NPC) */
USER_FUNC(evt_sl_npc_fly) {
    EvtWord* a = event->args;
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, a[0]));
    static Vec from; /* one flight at a time */
    static s32 frames;
    f32 t;
    if (npc == NULL) {
        return EVT_RETURN_DONE;
    }
    if (isFirstCall) {
        frames = 0;
        from = npc->position;
    }
    frames++;
    t = (f32)frames * (1000.0f / 60.0f) / (f32)evtGetValue(event, a[4]);
    if (t > 1.0f) {
        t = 1.0f;
    }
    t = t * t * (3.0f - 2.0f * t); /* ease in and out */
    npc->position.x = from.x + ((f32)evtGetValue(event, a[1]) - from.x) * t;
    npc->position.y = from.y + ((f32)evtGetValue(event, a[2]) - from.y) * t;
    npc->position.z = from.z + ((f32)evtGetValue(event, a[3]) - from.z) * t;
    return t >= 1.0f ? EVT_RETURN_DONE : EVT_RETURN_BLOCK;
}

/* A field enemy, as a map's setup file makes one (database.c's setupDataBase),
 * for rooms on borrowed geometry without the borrowed map's own (noSetup):
 * in: its name, tribe (npc_data.c's npcTribe, the game's names), AI type
 * (npc_ai_type_table), position (x, y, z), how far it roams (x and z, a square
 * around its place), and its battle (the area's BTLNO_* name, its battle table
 * entry). It wanders, sees Luigi, chases, and First Strikes go both ways. */
USER_FUNC(evt_sl_enemy) {
    EvtWord* a = event->args;
    const char* name = (const char*)evtGetValue(event, a[0]);
    const char* tribeName = (const char*)evtGetValue(event, a[1]);
    const char* aiType = (const char*)evtGetValue(event, a[2]);
    const char* btlSetup = (const char*)evtGetValue(event, a[8]);
    NpcTribeInfo* tribe = npcGetTribe(tribeName);
    NpcAiInfo* ai;
    NpcEntry* npc;
    AreaData* area;
    s32 btl = -1;
    if (getenv("TTYD_SL_NO_ENEMIES") != NULL) {
        return EVT_RETURN_DONE; /* tests: walking a room's route */
    }
    if (tribe == NULL) {
        return EVT_RETURN_DONE;
    }
    if (!animGroupBaseAsync(tribe->modelName, 0, NULL)) {
        return EVT_RETURN_BLOCK; /* its model loads first, as evt_npc_entry waits */
    }
    npcEntry(name, (const char*)tribe->modelName);
    npc = npcNameToPtr(name);
    npc->position.x = (f32)evtGetValue(event, a[3]);
    npc->position.y = (f32)evtGetValue(event, a[4]);
    npc->position.z = (f32)evtGetValue(event, a[5]);
    npc->direction = 270.0f;
    npc->tribe = tribe;
    npc->width = tribe->width;
    npc->height = tribe->height;
    npc->flags &= ~NPC_FLAG_ACTIVE;
    for (ai = npc_ai_type_table; ai->description != NULL; ai++) {
        if (strcmp(ai->description, aiType) == 0) {
            npc->flags |= ai->flags;
            npc->initEvent = ai->initEvent;
            npc->moveEvent = ai->moveEvent;
            npc->deadEvent = ai->deadEvent;
            npc->findEvent = ai->findEvent;
            npc->lostEvent = ai->lostEvent;
            npc->returnEvent = ai->returnEvent;
            npc->blowEvent = ai->blowEvent;
            break;
        }
    }
    npc->territoryType = dbGetDefData(npc_define_territory_type, "NPC_TERRITORY_TYPE_SQUARE");
    npc->territoryBase = npc->position;
    npc->territoryLoiter.x = (f32)evtGetValue(event, a[6]);
    npc->territoryLoiter.y = 100.0f;
    npc->territoryLoiter.z = (f32)evtGetValue(event, a[7]);
    npc->territoryHoming.x = 200.0f;
    npc->territoryHoming.y = 300.0f;
    npc->territoryHoming.z = 0.0f;
    npc->searchRange = 200.0f;
    npc->searchAngle = 300.0f;
    npc->homingRange = 1000.0f;
    npc->homingAngle = 360.0f;
    npc->footstepSfxA = tribe->footstepSfxA;
    npc->footstepSfxB = tribe->footstepSfxB;
    npc->jumpSfx = tribe->jumpSfx;
    npc->landSfx = tribe->landSfx;
    npc->footstepStride = tribe->footstepStride;
    npc->footstepToggle = 1;
    npc->footstepAccum = 0.0f;
    area = areaDataPtr(gp->currentAreaName);
    if (area != NULL && area->btlSetupNoTbl != NULL && btlSetup != NULL) {
        btl = dbGetDefData((NpcTerritoryInfo*)area->btlSetupNoTbl, btlSetup);
    }
    npcSetBattleInfo(npc, btl);
    if (tribe->stayAnim) {
        strcpy(npc->stayAnim, tribe->stayAnim);
    }
    if (tribe->talkAnim) {
        strcpy(npc->talkAnim, tribe->talkAnim);
    }
    if (tribe->initialAnim) {
        strcpy(npc->currentAnim, tribe->initialAnim);
        animPoseSetAnim(npc->poseId, npc->currentAnim, 1);
    }
    return EVT_RETURN_DONE;
}

/* out: the buttons pressed this frame (PAD_BUTTON_*) */
USER_FUNC(evt_sl_buttons) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], (s32)keyGetButtonTrg(0));
    return EVT_RETURN_DONE;
}

/* --- a room's look (port/include/port/map_look.h) --- */

/* in: a map object or group (0 or "" for the whole map), mul and add (0xRRGGBB):
 * its colour becomes colour * mul + add (a tint, a glow) */
USER_FUNC(evt_sl_map_look) {
    EvtWord* a = event->args;
    const char* name = (const char*)evtGetValue(event, a[0]);
    (void)isFirstCall;
    portMapLookSet(name, (unsigned)evtGetValue(event, a[1]), (unsigned)evtGetValue(event, a[2]));
    return EVT_RETURN_DONE;
}

/* in: a placed map object's name (a mobj: a block, a box), mul and add (0xRRGGBB), as evt_sl_map_look:
 * its model's colour becomes colour * mul + add (mul per channel: a red block can be made stone) */
USER_FUNC(evt_sl_mobj_look) {
    EvtWord* a = event->args;
    (void)isFirstCall;
    portMapLookSetMobj((const char*)evtGetValue(event, a[0]), (unsigned)evtGetValue(event, a[1]),
                       (unsigned)evtGetValue(event, a[2]));
    return EVT_RETURN_DONE;
}

/* in: a map object or group (0 or "" for the whole map), a colour (0xRRGGBB) and how far (0-100):
 * washed toward that colour (100 white: a white room) */
USER_FUNC(evt_sl_map_wash) {
    EvtWord* a = event->args;
    const char* name = (const char*)evtGetValue(event, a[0]);
    const unsigned c = (unsigned)evtGetValue(event, a[1]);
    s32 t = evtGetValue(event, a[2]);
    unsigned mul, add, k;
    (void)isFirstCall;
    t = t < 0 ? 0 : t > 100 ? 100 : t;
    k = (unsigned)(255 - t * 255 / 100);
    mul = k << 16 | k << 8 | k;
    add = ((c >> 16 & 0xFF) * t / 100) << 16 | ((c >> 8 & 0xFF) * t / 100) << 8 | (c & 0xFF) * t / 100;
    portMapLookSet(name, mul, add);
    return EVT_RETURN_DONE;
}

/* --- the friends' field abilities --- */

static PortSlPoint sPoints[PORT_SL_ABILITIES][PORT_SL_POINTS];

PortSlPoint* portSlPointGet(int ability, int id) {
    if (ability < 0 || ability >= PORT_SL_ABILITIES || id < 0 || id >= PORT_SL_POINTS) {
        return NULL;
    }
    return &sPoints[ability][id];
}

PortSlPoint* portSlPointNear(int ability, float x, float z, int* end) {
    PortSlPoint* best = NULL;
    float bestD = 1e9f;
    int i;
    if (ability < 0 || ability >= PORT_SL_ABILITIES) {
        return NULL;
    }
    for (i = 0; i < PORT_SL_POINTS; i++) {
        PortSlPoint* p = &sPoints[ability][i];
        float d1, d2;
        if (!p->on) {
            continue;
        }
        d1 = hypotf(x - p->x, z - p->z);
        d2 = hypotf(x - p->x2, z - p->z2);
        if (d1 <= p->radius && d1 < bestD) {
            best = p, bestD = d1;
            if (end != NULL) {
                *end = 0;
            }
        }
        if (d2 <= p->radius && d2 < bestD) {
            best = p, bestD = d2;
            if (end != NULL) {
                *end = 1;
            }
        }
    }
    return best;
}

void* portSlPointRun(int ability, PortSlPoint* p) {
    EventEntry* e;
    if (p == NULL || p->script == NULL) {
        return NULL;
    }
    p->used = 1;
    e = evtEntry((EvtWord*)p->script, 0, 0);
    if (e != NULL) {
        e->lwData[0] = (EvtWord)(p - sPoints[ability]);
    }
    return e;
}

static void pointsClear(void) {
    memset(sPoints, 0, sizeof(sPoints));
}

/* in: ability (PORT_SL_ABILITY_*), id (0-7), where (x, y, z), radius, value, script (or 0):
 * a point of a field ability in this room (its other end, if any, by evt_sl_point_end) */
USER_FUNC(evt_sl_point) {
    EvtWord* a = event->args;
    PortSlPoint* p = portSlPointGet((int)evtGetValue(event, a[0]), (int)evtGetValue(event, a[1]));
    (void)isFirstCall;
    if (p != NULL) {
        p->on = 1;
        p->used = 0;
        p->x = p->x2 = (f32)evtGetValue(event, a[2]);
        p->y = p->y2 = (f32)evtGetValue(event, a[3]);
        p->z = p->z2 = (f32)evtGetValue(event, a[4]);
        p->radius = (f32)evtGetValue(event, a[5]);
        p->value = (long long)evtGetValue(event, a[6]);
        p->script = (void*)evtGetValue(event, a[7]);
    }
    return EVT_RETURN_DONE;
}

/* in: ability, id, the point's other end (x, y, z) */
USER_FUNC(evt_sl_point_end) {
    EvtWord* a = event->args;
    PortSlPoint* p = portSlPointGet((int)evtGetValue(event, a[0]), (int)evtGetValue(event, a[1]));
    (void)isFirstCall;
    if (p != NULL) {
        p->x2 = (f32)evtGetValue(event, a[2]);
        p->y2 = (f32)evtGetValue(event, a[3]);
        p->z2 = (f32)evtGetValue(event, a[4]);
    }
    return EVT_RETURN_DONE;
}

/* in: ability, id: the point off (a wall blown, a machine fixed) */
USER_FUNC(evt_sl_point_off) {
    PortSlPoint* p = portSlPointGet((int)evtGetValue(event, event->args[0]), (int)evtGetValue(event, event->args[1]));
    (void)isFirstCall;
    if (p != NULL) {
        p->on = 0;
    }
    return EVT_RETURN_DONE;
}

static int sAbilityDone;

/* the friend's ability script is over: the partner goes back to following */
USER_FUNC(evt_sl_ability_done) {
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    (void)event;
    (void)isFirstCall;
    sAbilityDone = 1;
    /* the field and the friend back here and now: an ability that moves the partner (Swim's
     * jump and swim) takes him out of his use state, and his use function, which would do
     * this on its next call, isn't called again; the field stayed at system level 1, with
     * every exit, chest and sign dead */
    marioStSystemLevel(0);
    if (party != NULL && (party->flags & PARTY_FLAG_IS_BEING_USED)) {
        partyChgRunMode(party, 2);
        partyChgMoveMode(party, 0);
        partyChgMot(party, 0);
        partyChgPoseId(party, 1);
        party->flags &= ~PARTY_FLAG_IS_BEING_USED;
        party->useMotionId = 11;
        party->moveFrames = 0;
    }
    return EVT_RETURN_DONE;
}

void portSlFieldUse(PartyEntry* party, void* script) {
    if (party->trigFlags & PARTY_TRIG_FLAG_CHG_RUN_MODE) {
        party->trigFlags &= ~PARTY_TRIG_FLAG_CHG_RUN_MODE;
        partyChgMoveMode(party, 0);
        partyChgMot(party, 0);
        partyChgPoseId(party, 1);
        party->moveFrames = 100;
        party->useMotionId = 0;
    }
    switch (party->useMotionId) {
        case 0:
            sAbilityDone = 0;
            evtEntryType((EvtWord*)script, 0, 0, 26);
            marioStSystemLevel(1);
            party->useMotionId = 1;
            break;
        case 1:
            if (!sAbilityDone) {
                break;
            }
            marioStSystemLevel(0);
            party->moveFrames = 5;
            party->useMotionId = 11;
            /* fall through */
        case 11:
            if (--party->moveFrames > 0) {
                break;
            }
            partyChgRunMode(party, 2);
            partyChgMoveMode(party, 0);
            partyChgMot(party, 0);
            partyChgPoseId(party, 1);
            party->flags &= ~PARTY_FLAG_IS_BEING_USED;
            break;
    }
    partyChkGnd(party);
}

/* --- a chapter's textures --- */

void portSlMapTextures(const char* pattern, const char* folder) {
    const char* path;
    int i;
    for (i = 0; (path = portSlAssetIn(folder, i)) != NULL; i++) {
        unsigned int size = 0;
        const void* data = portSlAsset(path, &size);
        if (strstr(path, "tex1_") != NULL) {
            portModsAddMapTexture(pattern, path, data, size);
        }
    }
}

/* ======================================================================
 * Each frame
 * ====================================================================== */

/* the title closed its curtain and no card opens it: campaign@ opens it once the room is up (tests) */
static int sTestCurtain;

/* $TTYD_SL_TEST: "campaign" (a new one, once the title is up), "continue",
 * or "chapter:N" (from the loaded save's field, as "Show me" does) (tests) */
static void testStart(int seq) {
    static int sFrames, sDone;
    const char* v;
    if (sDone || (v = getenv("TTYD_SL_TEST")) == NULL || portCheckpointPending(NULL) != NULL) { /* (a checkpoint run: its own) */
        return;
    }
    if (seq != SEQ_GAME && seq != SEQ_TITLE) {
        sFrames = 0;
        return;
    }
    if (++sFrames < 240) {
        return;
    }
    sDone = 1;
    if (strncmp(v, "modes:", 6) == 0) {
        /* modes:campaign | modes:continue | modes:chapter:N | modes:quit: what the Modes tab's
         * buttons call (portSuperLuigi*), from the settings page's side */
        const char* why = "";
        int ok = 1;
        if (strcmp(v + 6, "campaign") == 0 || strcmp(v + 6, "continue") == 0) {
            ok = portSuperLuigiStartCampaign(v[6] == 'c' && v[7] == 'a', &why);
        } else if (strncmp(v + 6, "at:", 3) == 0) {
            ok = portSuperLuigiStartCampaignAt(atoi(v + 9), &why);
        } else if (strncmp(v + 6, "chapter:", 8) == 0) {
            ok = portSuperLuigiPlayChapter(atoi(v + 14), &why);
        } else if (strcmp(v + 6, "quit") == 0) {
            portSuperLuigiQuit();
        }
        printf("[superluigi] test %s: %s %s\n", v, ok ? "asked" : "refused:", why);
        fflush(stdout);
        return;
    }
    if (strcmp(v, "campaign") == 0) {
        newCampaign();
    } else if (strncmp(v, "campaign@", 9) == 0) {
        /* campaign@map[@bero]: a new campaign, straight to one of the prologue's rooms with no card */
        static char map[32];
        char* bero;
        const char* f = getenv("TTYD_SL_FLAGS");
        snprintf(map, sizeof(map), "%s", v + 9);
        bero = strchr(map, '@');
        if (bero != NULL) {
            *bero++ = 0;
        }
        newCampaign();
        {
            /* $TTYD_SL_CHAPTER=N: in that chapter's room, with its pouch and friend, as the card would leave it */
            const char* ch = getenv("TTYD_SL_CHAPTER");
            if (ch != NULL && portSuperLuigiChapterBuilt(atoi(ch))) {
                const PortSlChapter* c = portSlGetChapter(atoi(ch));
                sMode = PORT_SL_INN;
                sChapter = atoi(ch);
                innLoadout(c);
                enter(sChapter, PORT_SL_CAMPAIGN, 0);
                if (c->friendId != 0) {
                    const PortSlFriend* fr = portSlGetFriend(c->friendId);
                    friendJoin(c->friendId);
                    if (fr != NULL) {
                        /* $TTYD_SL_RANK=N: the friend at that rank (1 Super, 2 Ultra), to test his later moves */
                        const char* rk = getenv("TTYD_SL_RANK");
                        if (rk != NULL) {
                            pouchGetPtr()->partyData[fr->partyId].attackLevel = (s16)atoi(rk);
                            pouchGetPtr()->partyData[fr->partyId].techLevel = (s16)atoi(rk);
                        }
                        gp->savePartyId[0] = fr->partyId;
                        marioGetPtr()->prevPartyMemberId[0] = fr->partyId;
                    }
                }
            }
        }
        cardText(sChapter);
        sTestCurtain = 1;
        for (; f != NULL && *f; f = strchr(f, ',') != NULL ? strchr(f, ',') + 1 : NULL) {
            swSet(atoi(f));
        }
        seqSetSeq(SEQ_MAP_CHANGE, map, bero);
    } else if (strcmp(v, "continue") == 0) {
        continueCampaign();
    } else if (strncmp(v, "chapter:", 8) == 0 && seq == SEQ_GAME) {
        /* chapter:N@map[@bero]: straight to one of its rooms, the friend joined and the openings seen */
        const char* at = strchr(v, '@');
        const int chapter = atoi(v + 8);
        if (at == NULL) {
            enterFromInn(chapter, 1);
        } else if (enterFromInn(chapter, 0)) {
            static char map[32];
            const PortSlChapter* c = portSlGetChapter(chapter);
            char* bero;
            snprintf(map, sizeof(map), "%s", at + 1);
            bero = strchr(map, '@');
            if (bero != NULL) {
                *bero++ = 0;
            }
            if (c->friendId != 0) {
                const PortSlFriend* f = portSlGetFriend(c->friendId);
                friendJoin(c->friendId);
                if (f != NULL) {
                    gp->savePartyId[0] = f->partyId;
                    marioGetPtr()->prevPartyMemberId[0] = f->partyId;
                }
            }
            if (chapter == 1) {
                swSet(SL_FLAG(1, 2)), swSet(SL_FLAG(1, 3)); /* chapter 1's opening seen, Blooey joined */
            }
            {
                /* $TTYD_SL_FLAGS=7115,7117: switches set before the room comes up (a room's later state) */
                const char* f = getenv("TTYD_SL_FLAGS");
                while (f != NULL && *f) {
                    swSet(atoi(f));
                    f = strchr(f, ',');
                    f = f != NULL ? f + 1 : NULL;
                }
            }
            seqSetSeq(SEQ_MAP_CHANGE, map, bero);
        }
    } else {
        sDone = 0;
    }
}

/* Where Luigi comes back up after falling in a borrowed room's water (the game's mot_damage: the Gazigazi bites
 * and he's set down at the point of the last floor he stood on that has one). A room on borrowed geometry runs
 * our init, not the map's own, so the map's evt_hit_damage_return_set never ran, and with no point the game
 * puts him back at the map's origin. In the Maple Wood (hei_01) that's the river's middle, under the bridge
 * while it's down: he fell in, was dropped back in, for ever (a softlock, seen). The game's own points
 * there are (0, -1000, 0), which never lands either. Ours are the bank he jumped from. Each room gets its
 * points as it comes up. Chapter 2's rooms set their own; the other borrowed maps whose own inits set points
 * (nok_00, muj_02, pik_00/01/04, tik_02/15) give (0, -1000, 0) or the origin there, so they gain nothing */
static HitDamageReturn kWater_hei_01[] = {{"mod_00", {-160.0f, 0.0f, 0.0f}}, {"mod_01", {110.0f, 0.0f, 0.0f}}, {0}};
static const struct {
    const char* files;
    HitDamageReturn* water;
} kWater[] = {
    {"hei_01", kWater_hei_01},
};

static void roomWater(void) {
    int c, i;
    for (c = 0; c < PORT_SL_CHAPTERS; c++) {
        const PortSlChapter* ch = portSlGetChapter(c);
        const PortSlMap* m;
        for (m = ch != NULL ? ch->maps : NULL; m != NULL && m->map != NULL; m++) {
            if (strcmp(m->map, gp->currentMapName) != 0 || m->files == NULL) {
                continue;
            }
            for (i = 0; i < (int)(sizeof(kWater) / sizeof(kWater[0])); i++) {
                HitDamageReturn* w;
                if (strcmp(kWater[i].files, m->files) != 0) {
                    continue;
                }
                for (w = kWater[i].water; w->mapobjName != NULL; w++) {
                    hitGrpDamageReturnSet(w->mapobjName, w);
                }
                if (npcNameToPtr_NoAssert("\x83\x4B\x83\x57\x83\x4B\x83\x57") == NULL) { /* the Gazigazi */
                    evtEntry(evt_gazigazi_entry, 0, 0);
                }
                printf("[superluigi] %s: %s's water\n", m->map, m->files);
                fflush(stdout);
                return;
            }
            return;
        }
    }
}

static void frameHook(void) {
    static char sMap[16];
    const int seq = seqGetSeq();
    int ask;
    testStart(seq);
    if (sTestCurtain && seq == SEQ_GAME) {
        sTestCurtain = 0;
        fadeReset(1);
    }
    if (seq == SEQ_BATTLE && sMode != PORT_SL_NONE && gp != NULL && gp->inBattle) {
        /* Keelhaul's jungle stage (stg_05_2): its grass at the front of the stage hides a Fuzzy to the
         * eyes, so that it reads as nobody there. Sunk below the stage's lip; the grass behind stays */
        BattleStageObject* grass = BattleSearchObjectPtr("kusa_mae");
        if (grass != NULL) {
            grass->positionOffset.y = -30.0f;
        }
        /* Chapter 4's hills' stage (the Choir's own stage hides no one): its front grass band
         * (kusamura_mae, z 72) stands in front of the party at about Luigi's knees, which is Hayzee up to
         * his face, so his leaves and most of each pose were hidden. 8 lower its top is at their feet:
         * both seen whole, the band still in front (more sinks it out of sight); the ferns stay */
        grass = BattleSearchObjectPtr("kusamura_mae");
        if (grass != NULL) {
            grass->positionOffset.y = -8.0f;
        }
    }
    portSlGalleryFrame(seq == SEQ_GAME);
    portSlGallery2dFrame(seq == SEQ_GAME);
    restoreSpot(seq);
    if (gp != NULL && strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        /* a new room: its init sets its own points */
        snprintf(sMap, sizeof(sMap), "%s", gp->currentMapName);
        pointsClear();
    }
    {
        static char sWaterMap[16];
        if (seq != SEQ_GAME && seq != SEQ_BATTLE) {
            sWaterMap[0] = '\0'; /* a map change, the title: the next room comes up fresh */
        }
        if (gp != NULL && sMode != PORT_SL_NONE && seq == SEQ_GAME &&
            strncmp(sWaterMap, gp->currentMapName, sizeof(sWaterMap)) != 0) {
            snprintf(sWaterMap, sizeof(sWaterMap), "%s", gp->currentMapName);
            roomWater();
        }
    }
    {
        /* $TTYD_SL_WASH=RRGGBB,percent: every room washed so (trying a look out) */
        const char* w = getenv("TTYD_SL_WASH");
        static char sWashed[16];
        if (w != NULL && gp != NULL && seq == SEQ_GAME && strcmp(sWashed, gp->currentMapName) != 0) {
            const unsigned c = (unsigned)strtoul(w, NULL, 16);
            const char* comma = strchr(w, ',');
            const unsigned t = comma != NULL ? (unsigned)atoi(comma + 1) : 100;
            const unsigned k = 255 - t * 255 / 100;
            snprintf(sWashed, sizeof(sWashed), "%s", gp->currentMapName);
            portMapLookSet(NULL, k << 16 | k << 8 | k,
                           ((c >> 16 & 0xFF) * t / 100) << 16 | ((c >> 8 & 0xFF) * t / 100) << 8 | (c & 0xFF) * t / 100);
        }
    }
    if (__atomic_exchange_n(&sAskQuit, 0, __ATOMIC_SEQ_CST) && sMode != PORT_SL_NONE) {
        if (sMode == PORT_SL_INN) {
            returnToInn();
        } else {
            leave();
            seqSetSeq(SEQ_TITLE, NULL, NULL);
        }
        return;
    }
    if ((seq == SEQ_GAME || seq == SEQ_TITLE) && !gp->inBattle) {
        ask = __atomic_exchange_n(&sAskCampaign, 0, __ATOMIC_SEQ_CST);
        if (ask == 1) {
            newCampaign();
            return;
        }
        if (ask == 2) {
            continueCampaign();
            return;
        }
        if (ask >= 10) {
            campaignAt(ask - 10);
            return;
        }
        ask = __atomic_exchange_n(&sAskChapter, -1, __ATOMIC_SEQ_CST);
        if (ask >= 0 && seq == SEQ_GAME) {
            enterFromInn(ask, 1);
            return;
        }
    }
    if (sMode == PORT_SL_INN && seq == SEQ_TITLE) {
        /* a Game Over in the inn's chapter: back to the table, not the title */
        returnToInn();
        return;
    }
    if (sMode == PORT_SL_CAMPAIGN && seq == SEQ_GAME) {
        const PortSlChapter* c = portSlGetChapter(sChapter);
        sInGame = 1;
        if (sSaveOnArrival && c != NULL && strcmp(gp->currentMapName, c->firstMap) == 0) {
            sSaveOnArrival = 0;
            portSuperLuigiCardWrite(CAMPAIGN_SLOT);
        }
    }
    if (sMode == PORT_SL_CAMPAIGN && seq == SEQ_TITLE && sInGame) {
        leave(); /* the campaign ended at the title (a Game Over, a reset) */
    }
    if (sMode != PORT_SL_NONE) {
        /* $TTYD_SL_QUIT_AT=N: the Modes tab's "leave the chapter", N frames into one (tests) */
        static int sChapterFrames;
        const char* quitAt = getenv("TTYD_SL_QUIT_AT");
        if (quitAt != NULL && seq == SEQ_GAME && ++sChapterFrames == atoi(quitAt)) {
            printf("[superluigi] test: leaving the chapter as the Modes tab does\n");
            fflush(stdout);
            portSuperLuigiQuit();
        }
        forceLuigi(1);
    }
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void addText(const char* const* t) {
    for (; t != NULL && t[0] != NULL; t += 2) {
        portModsSetMessage(t[0], t[1]);
    }
}

void portSuperLuigiRegister(void) {
    int c, id;
    for (c = 0; c < PORT_SL_CHAPTERS; c++) {
        const PortSlChapter* ch = portSlGetChapter(c);
        const PortSlMap* m;
        if (ch == NULL) {
            continue;
        }
        for (m = ch->maps; m != NULL && m->map != NULL; m++) {
            portModsAddHostMap(m->map, m->init, m->files);
        }
        addText(ch->text);
        if (ch->setup != NULL) {
            ch->setup();
        }
    }
    for (id = 1; id < PORT_SL_FRIENDS; id++) {
        const PortSlFriend* f = portSlGetFriend(id);
        if (f != NULL) {
            addText(f->text);
        }
    }
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portSlBattleRegister();
    /* the chapter card's coin: Luigi's face in place of Mario's, on Super Luigi's cards only
     * (mod_game_aurora.c names sys_01 "sys_01@sl" to the map-scoped sets while Luigi is the player) */
    portSlMapTextures("sys_01@sl", "art/titles/medallion/"); /* made from the disc (art/recipes/luigi_medallion.recipe) */
    portSlMapTextures("sys_01@sl", "art/titles/medallion_jp/"); /* the same, from the Japanese disc's folder */
    /* while Luigi is the player (here, and in Play as Luigi): the game's own pictures of Mario
     * in its menus as Luigi, in every map (mod_game_aurora.c flags the map "#luigi"): the
     * pause menu's first tab (made from the disc's own tabs, art/recipes/luigi_tabs.recipe) and
     * level-up's BP panel (drawn, art/tools/luigi_icons.py) */
    portSlMapTextures("*#luigi", "art/icons/luigi/ui/");
    portSlMapTextures("*#luigi", "art/icons/luigi/ui_jp/"); /* the Japanese tab (the same recipes) */
}
