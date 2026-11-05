/* Chapter select (port/include/port/playmodes.h): a finished file plays any
 * chapter's story again with its own Mario.
 *
 * The start: the file's story state is kept (the saved words and flags, the
 * area-local ones, and the parts of the pouch the story gives: key items,
 * partners, Star Powers and the Star Power gauge), the story (GSW 0) is set to
 * the end of the chapter before, the chapter's own areas' flags and words are
 * cleared, and Mario goes to the chapter's title card (sys_01, "stg_N"), as
 * the pipe to it does; the card takes him on to the chapter's first map.
 *
 * The end: when the story reaches the chapter's last step (STORY_STGn_END_*,
 * set on the way to the next chapter), the kept state is put back and Mario
 * goes back to the room the replay started in. Levels, items, badges and
 * coins won stay.
 *
 * The kept state is also written to <user>/chapter_replay_<slot>.bin, so a
 * save made during the replay and loaded later goes on with it (the file's
 * story would otherwise be the chapter's from then on).
 *
 * Online it starts (and is left) from the Start menu's form only (lockstep:
 * every game of a session in the same frame; the settings page is one game's
 * alone), on the host's file; a game not the host's keeps the state in the
 * session's folder. A save loaded during a session doesn't go on with a
 * replay (the kept state is one computer's file). */
#include "port/online.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "port/modes.h"
#include "port/settings.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The story's value at the end of each chapter (0: the prologue's): chapter
 * n is played from kEnd[n - 1] to kEnd[n]. */
static const s32 kEnd[PORT_CHAPTERS + 1] = {
    STORY_STG0_END_TO_STAGE_1,        STORY_STG1_END_TO_STAGE_2,     STORY_STG2_END_TO_GLITZVILLE,
    STORY_STG3_END_TO_STAGE_4,        STORY_STG4_END_OFF_TO_THE_KEYS, STORY_STG5_END_THE_TRAIN_LEAVES,
    STORY_STG6_END_ULTRA_HAMMER,      STORY_STG7_END_TO_THE_LAST_DUNGEON, STORY_STG8_END_AFTER_THE_BATTLE,
};

/* Each chapter's saved flags (GSWF), as [from, to) ranges (from the decomp's
 * GswfId names and the area sources that read and write them). The game's
 * flags come in blocks of 150 per area from 1175 (gor 1175, tik 1325, gon
 * 1475, nok 1625, hei 1775, usu 1925, gra 2075, jin 2225, tou 2375 and 2525,
 * win 2675, mri 2825, dou 2975, muj 3125, pik 3275, rsh 3425, eki 3575, hom
 * 3725, bom 3875, moo 4025, aji 4175, las 4325), but a few sit elsewhere:
 * 2374 (the Glitz Pit's arena intro) and 3874 (Goldbob's cannon approval,
 * chapter 7) are the last of the block before; Peach's interludes (chapters 1,
 * 2, 3 and 5) keep theirs in aji's block (4199-4210, 4220), Bowser's Great
 * Tree interlude (chapter 3) its planter in mri's (2883), his castle (chapter
 * 1) the Hammer Bros.' talk at 4775. 4474 is the Tattle Log's base. Rogueport's
 * (gor, tik), the Star Pieces and Shine Sprites (5530-5666) and the Trouble
 * Center's aren't cleared: the town's story is read from GSW 0, and what was
 * found stays found. */
typedef struct SwRange {
    s16 from, to;
} SwRange;
static const SwRange kFlags[PORT_CHAPTERS + 1][4] = {
    {{0, 0}},
    {{1475, 1925}, {4199, 4202}, {4220, 4221}, {4775, 4776}}, /* gon nok hei; Peach 1; Bowser's castle */
    {{2675, 2883}, {2884, 2975}, {4202, 4203}},               /* win mri; Peach 2 */
    {{2374, 2675}, {2883, 2884}, {4203, 4207}},               /* tou; Bowser at the tree; Peach 3 */
    {{1925, 2374}},                                           /* usu gra jin */
    {{2975, 3275}, {4207, 4211}},                             /* dou muj; Peach 5 */
    {{3275, 3874}},                                           /* pik rsh eki hom */
    {{3874, 4199}, {4211, 4220}, {4221, 4325}},               /* bom moo aji (not Peach's) */
    {{4325, 4474}},                                           /* las */
};
/* ... and the chapter's saved words (GSW): every word the chapter's areas
 * use (GswId in evt/evt_story.h). Chapter 4's areas keep none; the Trouble
 * Center's (1420-1474), Bowser's stages' (1221-1223, set up on entry) and
 * the Pit's stay. */
static const SwRange kWords[PORT_CHAPTERS + 1] = {
    {0, 0},
    {301, 302},   /* hei_13's can */
    {651, 669},   /* the Punies */
    {501, 581},   /* the Glitz Pit: match, rules, Yoshi egg time, ranking */
    {0, 0},       /* none */
    {721, 722},   /* Whacka */
    {821, 871},   /* the Excess Express' passengers (821-823) and leg (870) */
    {1071, 1072}, /* aji_01's locked door */
    {1121, 1125}, /* the Palace's corridor, keys, las_17's order, the Shadow Queen */
};

/* ... and the partners the story has at its start, by PartyId bit (each
 * joins in a chapter before): the chapter's scenes have no lines for the
 * others (Yoshi out at chapter 2's start: "No messages."), so those wait
 * until the replay ends. Ms. Mowz from chapter 7 (she can join earlier). */
#define PB(id) (1u << (id))
static const u8 kPartyAtStart[PORT_CHAPTERS + 1] = {
    0,
    PB(PARTNER_GOOMBELLA),
    PB(PARTNER_GOOMBELLA) | PB(PARTNER_KOOPS),
    PB(PARTNER_GOOMBELLA) | PB(PARTNER_KOOPS) | PB(PARTNER_FLURRIE),
    PB(PARTNER_GOOMBELLA) | PB(PARTNER_KOOPS) | PB(PARTNER_FLURRIE) | PB(PARTNER_YOSHI),
    PB(PARTNER_GOOMBELLA) | PB(PARTNER_KOOPS) | PB(PARTNER_FLURRIE) | PB(PARTNER_YOSHI) | PB(PARTNER_VIVIAN),
    PB(PARTNER_GOOMBELLA) | PB(PARTNER_KOOPS) | PB(PARTNER_FLURRIE) | PB(PARTNER_YOSHI) | PB(PARTNER_VIVIAN) |
        PB(PARTNER_BOBBERY),
    0xFE,
    0xFE,
};

#define SNAP_MAGIC 0x50434331u /* "PCC1" */
typedef struct Snapshot {
    u32 magic;
    s32 chapter;
    char returnMap[16];
    char playerName[20];
    s32 gsw0;
    u32 gswf[0x100];
    u8 gsw[0x800];
    u32 lswf[0x10];
    u8 lsw[0x400];
    s16 keyItems[MAX_KEY_ITEMS];
    u16 partyFlags[8];
    u16 starPowers;
    s16 maxSp;
} Snapshot;

static Snapshot sSnap;
static int sActive;              /* chapter being played, 0 none */
static Uint64 sStartedAt;
static int sResultCount, sResultChapter;
static unsigned long long sResultMs;
static int sStartAsked, sAbandonAsked; /* __atomic, from the page */

/* online, a game not the host's plays the host's file: its kept state goes in the session's folder
 * (<user>/online, the host's card's), never over this player's own */
static void snapPath(char* out, size_t size, int slot) {
    const int guest = portOnlineActive() && !portOnlineIsHost();
    snprintf(out, size, "%s%s/chapter_replay_%d.bin", portSettingsUserPath(), guest ? "/online" : "", slot);
}

static int slotNow(void) {
    return gp != NULL && gp->lastSlot >= 0 && gp->lastSlot <= 3 ? (int)gp->lastSlot : 3;
}

static void writeSnap(void) {
    char path[1024];
    FILE* f;
    snapPath(path, sizeof(path), slotNow());
    f = fopen(path, "wb");
    if (f != NULL) {
        fwrite(&sSnap, sizeof(sSnap), 1, f);
        fclose(f);
    }
}

static void removeSnap(int slot) {
    char path[1024];
    snapPath(path, sizeof(path), slot);
    remove(path);
}

int portChapterSelectUnlocked(void) {
    return gp != NULL && swStoryPeek() >= STORY_STG9_02_RETURN_TO_ROGUEPORT;
}

int portChapterActive(void) {
    return __atomic_load_n(&sActive, __ATOMIC_SEQ_CST);
}

unsigned long long portChapterElapsed(void) {
    return sActive ? SDL_GetTicks() - sStartedAt : 0;
}

int portChapterLastResult(int* chapter, unsigned long long* ms) {
    *chapter = sResultChapter;
    *ms = sResultMs;
    return sResultCount;
}

int portChapterStart(int chapter, const char** why) {
    if (chapter < 1 || chapter > PORT_CHAPTERS) {
        *why = "No such chapter.";
        return 0;
    }
    if (portReplayActive()) {
        /* a start is no input: a playback would never go (online the Start menu's form starts it in every
         * game in the same frame; the settings page doesn't offer it then) */
        *why = "Not during a replay.";
        return 0;
    }
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (portChapterActive()) {
        *why = "A chapter is being played again already.";
        return 0;
    }
    if (!portChapterSelectUnlocked()) {
        *why = "Chapter select opens once this file has finished the game.";
        return 0;
    }
    if (portPitCoopActive() || (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished)) {
        *why = "Not while another mode runs.";
        return 0;
    }
    __atomic_store_n(&sStartAsked, chapter, __ATOMIC_SEQ_CST);
    return 1;
}

void portChapterAbandon(void) {
    __atomic_store_n(&sAbandonAsked, 1, __ATOMIC_SEQ_CST);
}

static void keep(int chapter) {
    const PouchData* p = pouchGetPtr();
    int i;
    memset(&sSnap, 0, sizeof(sSnap));
    sSnap.magic = SNAP_MAGIC;
    sSnap.chapter = chapter;
    snprintf(sSnap.returnMap, sizeof(sSnap.returnMap), "%s", gp->currentMapName);
    memcpy(sSnap.playerName, gp->playerName, sizeof(sSnap.playerName) < sizeof(gp->playerName)
                                                  ? sizeof(sSnap.playerName)
                                                  : sizeof(gp->playerName));
    sSnap.gsw0 = gp->gsw0;
    memcpy(sSnap.gswf, gp->gswf, sizeof(sSnap.gswf));
    memcpy(sSnap.gsw, gp->gsw, sizeof(sSnap.gsw));
    memcpy(sSnap.lswf, gp->lswf, sizeof(sSnap.lswf));
    memcpy(sSnap.lsw, gp->lsw, sizeof(sSnap.lsw));
    memcpy(sSnap.keyItems, p->keyItems, sizeof(sSnap.keyItems));
    for (i = 0; i < 8; i++) {
        sSnap.partyFlags[i] = p->partyData[i].flags;
    }
    sSnap.starPowers = p->starPowersObtained;
    sSnap.maxSp = p->maximumSP;
}

static void putBack(void) {
    PouchData* p = pouchGetPtr();
    int i;
    gp->gsw0 = sSnap.gsw0;
    memcpy(gp->gswf, sSnap.gswf, sizeof(sSnap.gswf));
    memcpy(gp->gsw, sSnap.gsw, sizeof(sSnap.gsw));
    memcpy(gp->lswf, sSnap.lswf, sizeof(sSnap.lswf));
    memcpy(gp->lsw, sSnap.lsw, sizeof(sSnap.lsw));
    memcpy(p->keyItems, sSnap.keyItems, sizeof(sSnap.keyItems));
    for (i = 0; i < 8; i++) {
        p->partyData[i].flags = sSnap.partyFlags[i];
    }
    p->starPowersObtained = sSnap.starPowers;
    p->maximumSP = sSnap.maxSp;
    if (p->currentSP > p->maximumSP) {
        p->currentSP = p->maximumSP;
    }
}

static void startNow(int chapter) {
    static char bero[8]; /* seqSetSeq keeps the pointer */
    int i, b;
    keep(chapter);
    /* the chapter's story from its start */
    swByteSet(GSW_STORY_PROGRESS, kEnd[chapter - 1]);
    for (i = 0; i < 4; i++) {
        for (b = kFlags[chapter][i].from; b < kFlags[chapter][i].to; b++) {
            swClear(b);
        }
    }
    for (i = kWords[chapter].from; i < kWords[chapter].to; i++) {
        swByteSet(i, 0);
    }
    memset(gp->lswf, 0, sizeof(gp->lswf));
    memset(gp->lsw, 0, sizeof(gp->lsw));
    /* the party as the story has it (put back at the end with the rest) */
    for (i = PARTNER_GOOMBELLA; i <= PARTNER_MSMOWZ; i++) {
        if (!(kPartyAtStart[chapter] & PB(i))) {
            pouchGetPtr()->partyData[i].flags &= (u16)~POUCH_PARTY_FLAG_JOINED;
        }
    }
    if (marioGetParty() > 0 && !(kPartyAtStart[chapter] & PB(marioGetParty()))) {
        marioPartyEntry(PARTNER_GOOMBELLA);
    }
    sActive = chapter;
    sStartedAt = SDL_GetTicks();
    writeSnap();
    snprintf(bero, sizeof(bero), "stg_%d", chapter);
    printf("[chapters] chapter %d again from %s (story %d)\n", chapter, sSnap.returnMap, (int)kEnd[chapter - 1]);
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, "sys_01", bero);
}

/* done 1: the chapter's end reached; 0: left */
static void endNow(int done) {
    const int chapter = sActive;
    putBack();
    sActive = 0;
    removeSnap(slotNow());
    if (done) {
        sResultChapter = chapter;
        sResultMs = SDL_GetTicks() - sStartedAt;
    } else {
        sResultChapter = 0;
        sResultMs = 0;
    }
    sResultCount++;
    printf("[chapters] chapter %d %s\n", chapter, done ? "played to its end" : "left");
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, sSnap.returnMap[0] != '\0' ? sSnap.returnMap : "gor_01", NULL);
}

/* a save file just loaded: one saved during a replay goes on with it */
static void fileLoaded(void) {
    char path[1024];
    FILE* f;
    Snapshot s;
    const int slot = slotNow();
    const s32 story = swStoryPeek();
    sActive = 0;
    if (portOnlineActive() || portReplayActive()) {
        return; /* the kept state is this player's file, which the others don't have */
    }
    snapPath(path, sizeof(path), slot);
    f = fopen(path, "rb");
    if (f == NULL) {
        return;
    }
    if (fread(&s, sizeof(s), 1, f) != 1 || s.magic != SNAP_MAGIC || s.chapter < 1 || s.chapter > PORT_CHAPTERS) {
        fclose(f);
        return;
    }
    fclose(f);
    if (memcmp(s.playerName, gp->playerName, sizeof(s.playerName) < sizeof(gp->playerName) ? sizeof(s.playerName)
                                                                                         : sizeof(gp->playerName)) !=
            0 ||
        story < kEnd[s.chapter - 1] || story >= kEnd[s.chapter]) {
        if (story >= STORY_STG9_02_RETURN_TO_ROGUEPORT) {
            remove(path); /* the file as it is after the game: a replay left without saving */
        }
        return; /* another file in the slot, or saved after the replay: the kept state isn't this file's */
    }
    sSnap = s;
    sActive = s.chapter;
    sStartedAt = SDL_GetTicks();
    printf("[chapters] a save made during chapter %d's replay: it goes on\n", s.chapter);
    fflush(stdout);
}

/* each frame, from the play modes' marioStMain hook */
void portChaptersFrame(void) {
    static SequenceType sPrevSeq = SEQ_UNDEFINED;
    const SequenceType seq = seqGetSeq();
    if (sPrevSeq == SEQ_LOAD && seq != SEQ_LOAD && seq != SEQ_TITLE && gp != NULL) {
        fileLoaded();
    }
    if (seq == SEQ_TITLE || seq == SEQ_LOGO) {
        sActive = 0; /* the state kept stays on disk for a save made during it */
    }
    sPrevSeq = seq;
    if (seq != SEQ_GAME || gp == NULL || gp->inBattle) {
        return;
    }
    {
        /* tests ($TTYD_CHAPTER_TEST=N, docs/PC_PORT.md): chapter N again once a game runs (with
         * TTYD_MAP and TTYD_STORY=405; online in the same frame of every game) */
        static int sTestAsked;
        const char* test = getenv("TTYD_CHAPTER_TEST");
        if (!sTestAsked && test != NULL) {
            const char* why = "";
            sTestAsked = 1;
            if (!portChapterStart(atoi(test), &why)) {
                printf("[chapters] test start refused: %s\n", why);
            }
        }
    }
    /* a scene or a menu (the Start menu's form, online the way in every game alike): after it */
    const int calm = !seqCheckSeq() && marioStGetSystemLevel() == 0;
    if (sActive != 0) {
        if (calm && __atomic_exchange_n(&sAbandonAsked, 0, __ATOMIC_SEQ_CST)) {
            endNow(0);
        } else if (swStoryPeek() >= kEnd[sActive]) {
            endNow(1);
        }
        return;
    }
    __atomic_store_n(&sAbandonAsked, 0, __ATOMIC_SEQ_CST);
    if (calm) {
        const int chapter = __atomic_exchange_n(&sStartAsked, 0, __ATOMIC_SEQ_CST);
        if (chapter >= 1 && chapter <= PORT_CHAPTERS) {
            startNow(chapter);
        }
    }
}
