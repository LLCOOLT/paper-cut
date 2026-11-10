/* Test-only hooks for playing the story unattended (port/include/port/story_test.h,
 * tools/port/story_sweep.py): a warp to any row of the game's story table,
 * battles played by the versus computer, no damage, the text advanced, and a
 * watch on the story value. Each is off unless its variable is set, and none
 * runs in an online session or a replay: normal play never sees them. */
#include "port/story_test.h"
#include "port/practice_jump.h"
#include "port/gameplus.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/online_end.h"
#include "port/open_world.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "battle/battle_audience.h"
#include "battle/battle_break_slot.h"
#include "driver/casedrv.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_motion.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "data/item_data.h"
#include "event.h"
#include "party.h"
#include "party/party_yoshi.h"
#include "statuswindow.h"

#include <dolphin/vi.h>
#include "driver/hitdrv.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int portSecondPlayerMessagePad(void); /* second_player_pc.c */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#ifndef RTLD_DEFAULT /* (glibc declares it only with _GNU_SOURCE; its value) */
#define RTLD_DEFAULT ((void*)0)
#endif
#endif

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

MapObjectWork* portMobjGetWork(void);                            /* mobjdrv.c */
EventEntry* mobjRunEvent(MapObjectEntry* entry, EvtWord* eventCode); /* mobjdrv.c */
int portMsgWaiting(int* sel, int* numSel, const char** tag);    /* msgdrv.c */
void portMsgDump(void);                                         /* msgdrv.c */

static int sAutoItems, sMaxTurns = 60, sTurnsExit, sTurnsFailed;
/* TTYD_AUTO_BATTLE_ASSIST=<n>: the action-command and guard assist the auto battler plays with (default 2, both
 * automatic; 0: none, a player who never presses for them: the plain fight, tests/port/testpass/pm64_jr_troopa_fair_*) */
static int sAssist = 2;
static int sTurns; /* auto-battle: commands chosen this battle */
static int sQuiet, sMaxQuiet = 3600; /* auto-battle: frames since the last command or battle step, and the cap */
int portNameEntryOpen(void); /* nameent.c */
static u32 sQuietSig;
static int sLectureThink;
/* tests: $TTYD_TEST_BINGO_PARTNER=1: the BINGO's cards turned in the partner's first turn (commandInputPre),
 * then its reels watched (1 waiting for the partner's turn, 2 the cards turned, 3 the reels spin, 4 stopped) */
static int sBingoPartner;
/* TTYD_AUTO_BATTLE_RUN=<n>: from the n-th command of a battle on, the auto battler runs away, as a planned move (no
 * STUCK, no failed run): the fights a player is meant to flee. Without it the same happens by itself when every foe
 * takes no damage (PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS on all its parts: gra_00's fake Mario, unit_faker_mario_gra.c)
 * and the battle allows Run Away (no NPC_BATTLE_NO_ESCAPE) */
static int sRunFrom;
static int sRunLogged;

/* a lesson's battle: the menu limited by the lesson's flags (battle.h) */
static int lecture(const BattleWork* wp) {
    return (wp->flags & (BATTLE_FLAG_LECTURE_LOCK_ALL | BATTLE_FLAG_LECTURE_JUMP | BATTLE_FLAG_LECTURE_HAMMER |
                         BATTLE_FLAG_LECTURE_SPECIAL | BATTLE_FLAG_LECTURE_MENU | BATTLE_FLAG_LECTURE_STEP2)) != 0;
}
static int sAutoBattle, sGod, sGodPower = 1, sAutoText, sReadFrames, sWatch, sControlWatch, sUntil = -1, sSettle = 300, sKickAfter = 240;
static int sEventRow = -1;
static int sControl; /* frames Mario has had control and nothing to read */
static char sUntilMap[32];
static int sUntilControl; /* TTYD_STORY_UNTIL_CONTROL: the scene over (Mario's control back a while) is the end too */
static int sArrived;      /* TTYD_EVENT's map reached (or no TTYD_EVENT) */
static const char* sChoice;
#define MAX_KICKS 8
static void* sKick[MAX_KICKS];
static char sKickName[MAX_KICKS][128];
static int sKickCount;

/* never in a session or a replay: the other games, or the recording, would play on without it */
static int blocked(void) {
    return portReplayActive();
}

static int envInt(const char* name, int fallback) {
    const char* v = getenv(name);
    return v != NULL && *v ? atoi(v) : fallback;
}

/* --- symbols: a script by its name in the executable --- */

static void* findSymbol(const char* name) {
    const int deref = name[0] == '*';
    void* p;
#ifdef _WIN32
    p = (void*)GetProcAddress(GetModuleHandleW(NULL), name + deref);
#else
    p = dlsym(RTLD_DEFAULT, name + deref);
#endif
    if (p != NULL && deref) {
        p = *(void* const*)p; /* a static's export: a pointer to it (CMakeLists.txt's ttyd_static_exports) */
    }
    return p;
}

/* --- the running scripts, for a stuck story --- */

static void dumpEvents(void) {
    EventWork* wp = evtGetWork();
    char a[160], b[160];
    s32 i;
    for (i = 0; i < wp->count; i++) {
        EventEntry* e = &wp->entries[i];
        if (!(e->flags & 1)) {
            continue;
        }
        printf("[story]   #%d id %d at %s", (int)i, (int)e->eventId, portSymbolName(e->nextCommand, a, sizeof(a)));
        if (e->opcode == OPCODE_USER_FUNC && e->args != NULL) {
            printf(" -> %s (its message window %d)", portSymbolName((void*)e->args[0], b, sizeof(b)), (int)e->printWindowId);
        }
        printf("%s\n", e->waitingOnEvent ? " (waiting on a child)" : "");
    }
    portMsgDump(); /* the windows a script there may wait on */
    fflush(stdout);
}

/* --- TTYD_EVENT: the scenario select's warp (seq_e3.c, case 102) --- */

/* (the table's walk and its story set-up are the Practice tab's jump's too: port/include/port/practice_jump.h) */
static EventData* eventRow(int row, int* stageOut, int* idxOut, int* prevId) {
    return portEventTableRow(row, stageOut, idxOut, prevId);
}

static void eventSetUp(EventData* row, int stage, int idx, int prevId) {
    int i;
    portEventTableSetUp(row, stage, idx, prevId);
    /* the scenario select's file: 20 HP, FP and BP, a few attack items, two badges */
    memset(pouchGetPtr()->heldItems, 0, sizeof(s16) * MAX_HELD_ITEMS);
    memset(pouchGetPtr()->badges, 0, sizeof(s16) * MAX_BADGES);
    memset(pouchGetPtr()->equippedBadges, 0, sizeof(s16) * MAX_BADGES);
    pouchSetMaxHP(20);
    pouchSetHP(20);
    pouchSetMaxFP(20);
    pouchSetFP(20);
    for (i = 0; i < 8; i++) {
        pouchGetPtr()->partyData[i].currentHP = 20;
        pouchGetPtr()->partyData[i].HPLevel = 1;
    }
    for (i = 0; i < 2; i++) {
        pouchGetItem(ITEM_THUNDER_RAGE);
        pouchGetItem(ITEM_SHOOTING_STAR);
        pouchGetItem(ITEM_FIRE_FLOWER);
        pouchGetItem(ITEM_POW_BLOCK);
    }
    pouchGetPtr()->availableBP = 20;
    pouchGetPtr()->maximumBP = 20;
    pouchGetItem(ITEM_POWER_BOUNCE);
    pouchGetItem(ITEM_POWER_SMASH);
    pouchEquipBadgeID(ITEM_POWER_BOUNCE);
    pouchEquipBadgeID(ITEM_POWER_SMASH);
    {
        /* $TTYD_OPEN_SEED: an open-world randomizer file, as trace_pc.c's map warp makes one */
        const char* seed = portOpenTestSeed();
        if (seed != NULL) {
            portOpenStamp(seed, 0x0F, 0);
        }
    }
    pouchReviseMarioParam();
    pouchRevisePartyParam();
    statusWinForceUpdate();
}

/* $TTYD_STORY_ROW with $TTYD_MAP (trace_pc.c's warp): the new game set up as
 * at that row (story, set-up functions, partners), on the warp's own map */
int portStoryTestRowSetUp(int row) {
    int stage, idx, prevId;
    EventData* d = eventRow(row, &stage, &idx, &prevId);
    if (d == NULL || blocked()) {
        printf("[event] FAIL: no row %d in the story table\n", row);
        fflush(stdout);
        return 0;
    }
    eventSetUp(d, stage, idx, prevId);
    printf("[event] row %d (%s, stage %d) set up: story %d\n", row, d->textId, stage, prevId);
    fflush(stdout);
    return 1;
}

static void eventFrame(void) {
    static int state; /* 0 waiting for the title, 1 warped, 2 there */
    static time_t start;
    static char map[32];
    static int limit;
    int stage, idx, prevId;
    EventData* row;
    if (sEventRow < 0 || state == 2 || gp == NULL) {
        return;
    }
    if (start == 0) {
        start = time(NULL);
        limit = envInt("TTYD_WARP_LIMIT", 60);
    }
    row = eventRow(sEventRow, &stage, &idx, &prevId);
    if (row == NULL) {
        printf("[event] FAIL: no row %d in the story table\n", sEventRow);
        fflush(stdout);
        portTestExit(3);
    }
    if (state == 0 && (seqGetSeq() == SEQ_TITLE || seqGetSeq() == SEQ_GAME)) {
        const char* bero = row->beroName != NULL && row->beroName[0] ? row->beroName : NULL;
        eventSetUp(row, stage, idx, prevId);
        snprintf(map, sizeof(map), "%s", row->mapName);
        printf("[event] frame %u: row %d (%s, story %d, stage %d) '%s': story %d, to %s%s%s\n",
               (unsigned)VIGetRetraceCount(), sEventRow, row->textId, (int)row->id, stage,
#if VERSION >= VERSION_US
               row->nameEn,
#else
               row->textId,
#endif
               prevId, map, bero ? " " : "", bero ? bero : "");
        fflush(stdout);
        seqSetSeq(SEQ_MAP_CHANGE, row->mapName, bero);
        state = 1;
    } else if (state == 1 && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, map) == 0) {
        printf("[event] frame %u: on %s\n", (unsigned)VIGetRetraceCount(), map);
        fflush(stdout);
        state = 2;
        sArrived = 1;
    }
    if (state < 2 && time(NULL) - start >= limit) {
        printf("[event] FAIL: %s not reached in %ds (sequence %d, map '%s')\n", map[0] ? map : "the title", limit,
               (int)seqGetSeq(), gp->currentMapName);
        fflush(stdout);
        portTestExit(3);
    }
}

/* every frame (portTraceFrame, from the VI retrace as $TTYD_MAP's warp): the
 * title runs no field frame, so marioStMain's hook would never see it */
void portStoryTestEventFrame(void) {
    if (blocked()) {
        return;
    }
    eventFrame();
}

/* --- TTYD_STORY_KICK: a script started as the game would start it --- */

static int sKickEvt = -1, sKickNext, sKickRounds;
static NpcEntry* sKickNpc;
static char sKickMap[16]; /* the map the kicked talk began on */

/* a walk: towards a hit object (an exit's zone) or a point ("_" keeps Mario's own x or z) */
static int sWalkFrames = -1, sWalkStuck, sWalkTries, sWalkDetour, sWalkStory, sWalkPast;
static s8 sWalkStickX, sWalkStickY;
static char sWalkTo[64], sWalkMap[16];
static Vec sWalkPrev;

static int walkTarget(Vec* to) {
    MarioWork* m = marioGetPtr();
    if (strchr(sWalkTo, ':') != NULL) {
        char x[24], z[24];
        if (sscanf(sWalkTo, "%23[^:]:%23s", x, z) != 2) {
            return 0;
        }
        to->x = x[0] == '_' ? m->position.x : (f32)atof(x);
        to->y = m->position.y;
        to->z = z[0] == '_' ? m->position.z : (f32)atof(z);
        return 1;
    }
    hitObjGetPos(sWalkTo, to);
    return 1;
}

static void kick(int k) {
    void* code = sKick[k];
    int i;
    EventEntry* e = NULL;
    if (strncmp(sKickName[k], "case:", 5) == 0) {
        /* a hit object's case (a door's, a sign's): triggered as when its condition holds */
        for (i = 0; i < 64; i++) {
            CaseEntry* c = caseIdToPtr(i);
            if (c != NULL && strcmp(c->hitObjName, sKickName[k] + 5) == 0) {
                c->flags |= CASE_ENTRY_FLAG_TRIGGERED;
                printf("[story] frame %u: kick %s: its case\n", (unsigned)VIGetRetraceCount(), sKickName[k]);
                fflush(stdout);
                return;
            }
        }
        printf("[story] frame %u: kick %s: no case on it\n", (unsigned)VIGetRetraceCount(), sKickName[k]);
        fflush(stdout);
        return;
    }
    if (strncmp(sKickName[k], "walk:", 5) == 0) {
        snprintf(sWalkTo, sizeof(sWalkTo), "%s", sKickName[k] + 5);
        snprintf(sWalkMap, sizeof(sWalkMap), "%s", gp->currentMapName);
        sWalkFrames = 0;
        sWalkStuck = sWalkTries = sWalkDetour = 0;
        sWalkPrev = marioGetPtr()->position;
        sWalkStory = swByteGet(GSW_STORY_PROGRESS);
        printf("[story] frame %u: kick %s: walking there\n", (unsigned)VIGetRetraceCount(), sKickName[k]);
        fflush(stdout);
        return;
    }
    for (i = 0; i < 64; i++) {
        CaseEntry* c = caseIdToPtr(i); /* (NULL for a free slot) */
        if (c != NULL && (c->flags & CASE_ENTRY_FLAG_IN_USE) && c->evtCode == code) {
            c->flags |= CASE_ENTRY_FLAG_TRIGGERED; /* caseMain starts it next, as when its condition holds */
            printf("[story] frame %u: kick %s: case '%s'\n", (unsigned)VIGetRetraceCount(), sKickName[k], c->hitObjName);
            fflush(stdout);
            return;
        }
    }
    {
        NpcWork* wp = npcGetWorkPtr();
        for (i = 0; i < wp->total; i++) {
            NpcEntry* npc = &wp->entries[i];
            if ((npc->flags & NPC_FLAG_USED) && npc->talkEvt == code) {
                /* as fbatTalkMode does when A is pressed in front of it */
                evtStopID(npc->regularEvtId);
                marioKeyOff();
                partyKeyOff();
                e = evtEntry(npc->talkEvt, 0, 0);
                e->wNpcEventType = 6;
                e->ownerNPC = npc;
                marioChgTalkMotion();
                sKickNpc = npc;
                snprintf(sKickMap, sizeof(sKickMap), "%s", gp->currentMapName);
                printf("[story] frame %u: kick %s: talk to '%s'\n", (unsigned)VIGetRetraceCount(), sKickName[k],
                       npc->description);
                break;
            }
        }
    }
    if (e == NULL) {
        MapObjectWork* wp = portMobjGetWork();
        for (i = 0; i < wp->count; i++) {
            MapObjectEntry* m = &wp->entries[i];
            if ((m->flags & MOBJ_FLAG_USED) && (void*)m->event == code) {
                e = mobjRunEvent(m, (EvtWord*)m->event);
                printf("[story] frame %u: kick %s: map object '%s'\n", (unsigned)VIGetRetraceCount(), sKickName[k],
                       m->name);
                break;
            }
        }
    }
    if (e == NULL && strncmp(sKickName[k], "talk:", 5) == 0) {
        /* an NPC's talk script with no NPC to own it ("me" is nobody): run alone it read a NULL NPC and
         * crashed (gon_03's leader Dull Bones once he had left); wait for the NPC instead */
        printf("[story] frame %u: kick %s: no NPC with it here\n", (unsigned)VIGetRetraceCount(), sKickName[k]);
        fflush(stdout);
        return;
    }
    if (e == NULL) {
        e = evtEntry(code, 0, 0);
        printf("[story] frame %u: kick %s: on its own\n", (unsigned)VIGetRetraceCount(), sKickName[k]);
    }
    fflush(stdout);
    sKickEvt = e != NULL ? e->eventId : -1;
}

/* --- the frame --- */


static void storyFrame(void) {
    static int last = -1, settle = -1, stuckSaid, exiting;
    static u32 lastChange;
    const u32 f = VIGetRetraceCount();
    const int story = swByteGet(GSW_STORY_PROGRESS);
    int sel, num;
    const char* tag;
    if (exiting) {
        if (!portShotPending()) {
            portTestExit(exiting - 1);
        }
        return;
    }
    if (story != last) {
        if (sWatch > 0 || sUntil >= 0) {
            printf("[story] frame %u: %d -> %d (map '%s', sequence %d)\n", (unsigned)f, last, story,
                   gp->currentMapName, (int)seqGetSeq());
            fflush(stdout);
        }
        last = story;
        lastChange = f;
        stuckSaid = 0;
    }
    if (seqGetSeq() == SEQ_GAME_OVER && sUntil >= 0) {
        printf("[story] FAIL: a Game Over at frame %u (story %d, map '%s')\n", (unsigned)f, story, gp->currentMapName);
        fflush(stdout);
        portShotRequest("story");
        exiting = 7 + 1;
        return;
    }
    if (sWatch > 0 && !stuckSaid && f - lastChange >= (u32)sWatch * 60) {
        printf("[story] STUCK: story %d unchanged for %ds (map '%s', sequence %d, frame %u); the running scripts:\n",
               story, sWatch, gp->currentMapName, (int)seqGetSeq(), (unsigned)f);
        dumpEvents();
        stuckSaid = 1;
        if (sUntil >= 0) {
            portShotRequest("story");
            exiting = 6 + 1;
            return;
        }
    }
    /* TTYD_CONTROL_WATCH (random play, tools/port/monkey.py): in the field, Mario without control that
     * long (no battle, no map change) is a softlock: the scripts that hold him, then exit 6 */
    if (sControlWatch > 0) {
        static u32 lastControl, lastSeen;
        const BOOL gap = f - lastSeen > 2; /* this hook not run meanwhile (a battle, a map change) */
        lastSeen = f;
        if (seqGetSeq() != SEQ_GAME || marioChkKey() || lastControl == 0 || gap) {
            lastControl = f;
        } else if (f - lastControl >= (u32)sControlWatch * 60) {
            printf("[story] NO CONTROL: Mario without control for %ds (story %d, map '%s', frame %u); the running "
                   "scripts:\n",
                   sControlWatch, story, gp->currentMapName, (unsigned)f);
            dumpEvents();
            exiting = 6 + 1;
            return;
        }
    }
    if (sUntil >= 0 && settle < 0 &&
        (story >= sUntil || (sUntilMap[0] && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, sUntilMap) == 0) ||
         (sUntilControl && sArrived && sControl >= sUntilControl))) {
        printf("[story] reached: story %d (map '%s') at frame %u\n", story, gp->currentMapName, (unsigned)f);
        fflush(stdout);
        settle = 0;
    }
    if (settle >= 0 && ++settle >= sSettle) {
        printf("[story] PASS: story %d (map '%s', sequence %d) at frame %u\n", story, gp->currentMapName,
               (int)seqGetSeq(), (unsigned)f);
        fflush(stdout);
        portShotRequest("story");
        exiting = 0 + 1;
        return;
    }
    /* the kicks: once Mario has had control a while and the story hasn't moved */
    if (sKickEvt >= 0 && !evtCheckID(sKickEvt)) {
        if (sKickNpc != NULL && (seqGetSeq() != SEQ_GAME || strcmp(gp->currentMapName, sKickMap) != 0)) {
            sKickNpc = NULL; /* the talk ended in a map change: fbatTalkMode never sees its end (the next map's
                                Mario starts afresh, marioReInit), so no keys given back here either */
        }
        if (sKickNpc != NULL) { /* a talk's end, as fbatTalkMode's */
            if (sKickNpc->flags & NPC_FLAG_USED) {
                evtStartID(sKickNpc->regularEvtId);
            }
            marioKeyOn();
            partyKeyOn();
            marioChgStayMotion();
            sKickNpc = NULL;
        }
        sKickEvt = -1;
    }
    if (seqGetSeq() == SEQ_GAME && marioChkKey() && portMsgWaiting(&sel, &num, &tag) == 0 && sKickEvt < 0 &&
        sWalkFrames < 0) {
        sControl++;
    } else {
        sControl = 0;
    }
    if (sKickCount > 0 && sControl >= sKickAfter && settle < 0 && sKickRounds < 3) {
        sControl = 0;
        kick(sKickNext);
        if (++sKickNext >= sKickCount) {
            sKickNext = 0;
            sKickRounds++;
        }
    }
}

static void godFrame(void) {
    PouchData* pouch = pouchGetPtr();
    int i;
    if (seqGetSeq() != SEQ_GAME) {
        return;
    }
    if (pouch->currentHP < pouch->maximumHP) {
        pouchSetHP(pouch->maximumHP);
    }
    if (pouch->currentFP < pouch->maximumFP) {
        pouchSetFP(pouch->maximumFP);
    }
    for (i = 1; i < 8; i++) {
        if (pouch->partyData[i].currentHP < pouch->partyData[i].maximumHP) {
            pouch->partyData[i].currentHP = pouch->partyData[i].maximumHP;
        }
    }
}

/* action commands and guards automatic (the accessibility assist: Settings, "ac_attack_assist" 2);
 * at start and now and then after, over whatever the settings file says */
static void forceAssist(void) {
    int *a, *g;
    (void)portGameplus("ac_attack_assist"); /* (its settings declared) */
    a = portSettingsFind("ac_attack_assist");
    g = portSettingsFind("ac_guard_assist");
    if (a != NULL) {
        *a = sAssist;
    }
    if (g != NULL) {
        *g = sAssist;
    }
}

static int sSweep, sSweepPhase; /* (the move sweep, below) */
static void sweepMoveOver(const char* how);

static void frameHook(void) {
    if (blocked() || gp == NULL) {
        return;
    }
    if (seqGetSeq() != SEQ_BATTLE) {
        if (sSweep && sSweepPhase != 0) {
            sweepMoveOver("battle over");
        }
        sTurns = 0;
        sQuiet = 0;
        if (sTurnsFailed && !sTurnsExit) {
            sTurnsExit = 1; /* (ran away from a battle over the cap: the run still fails) */
        }
    } else if (sAutoBattle && _battleWorkPointer != NULL) {
        if (sBingoPartner >= 2 && sBingoPartner < 4) {
            /* ($TTYD_TEST_BINGO_PARTNER: the reels' spin and stop, said once each) */
            const int phase = BattleBreakSlotGetPtr()->phase;
            if (sBingoPartner == 2 && phase == 3) {
                sBingoPartner = 3;
                printf("[story] test: the BINGO's reels spin (active unit %d)\n", (int)_battleWorkPointer->activeUnitId);
                fflush(stdout);
            } else if (sBingoPartner == 3 && phase != 3) {
                sBingoPartner = 4;
                printf("[story] test: the BINGO's reels stopped\n");
                fflush(stdout);
            }
        }
        /* the battle's own progress (a new turn, phase, actor or step of an act) counts as much as our
         * commands: with the whole party asleep (Sleepy Sheep) no command comes for minutes, yet the enemies
         * act; only a battle where nothing at all moves is a hang */
        const BattleWork* w = _battleWorkPointer;
        const u32 sig = (u32)w->seqTurnValue * 31u + (u32)w->seqPhaseValue * 17u + (u32)w->seqMoveValue * 13u +
                        (u32)w->seqActValue * 7u + (u32)w->activeUnitId;
        if (sig != sQuietSig) {
            sQuietSig = sig;
            sQuiet = 0;
        }
        /* (online, waiting on the other game's word: its player's turn, however slow that game runs under
           load, isn't a hang here; this game's frames race on meanwhile, and 3600 of them ended the host's game
           mid-fight while the guest, at half its frame rate, chose: pm64x_online_versus. Two minutes of real
           time waiting on one word is a hang all the same: the frames count again then) */
        {
            const unsigned waited = portStateSyncBattleWaitMs();
            if (waited > 0 && waited < 120000 && sQuiet > 0) {
                sQuiet--;
            }
        }
    }
    if (seqGetSeq() == SEQ_BATTLE && sAutoBattle && ++sQuiet == sMaxQuiet) {
        /* a hang inside one move or one enemy turn (an attack's script that never ends) is caught too: no new
         * command for $TTYD_AUTO_BATTLE_FRAMES frames (default 3600, a minute of game time) */
        printf("[story] STUCK: battle with no command or battle step for %d frames (story %d, map '%s'); the running scripts:\n",
               sMaxQuiet, (int)swByteGet(GSW_STORY_PROGRESS), gp->currentMapName);
        dumpEvents();
        if (_battleWorkPointer != NULL) {
            /* where the battle's own sequence stands, and who is left */
            BattleWork* w = _battleWorkPointer;
            int i;
            printf("[story]   battle: seq init %x first %x turn %x phase %x move %x act %x end %x, flags %x;",
                   (unsigned)w->seqInitValue, (unsigned)w->seqFirstActValue, (unsigned)w->seqTurnValue,
                   (unsigned)w->seqPhaseValue, (unsigned)w->seqMoveValue, (unsigned)w->seqActValue,
                   (unsigned)w->seqEndValue, (unsigned)w->flags);
            for (i = 0; i < BATTLE_UNIT_MAX; i++) {
                if (w->units[i] != NULL && w->units[i]->alliance != 2) {
                    printf(" kind %d hp %d", (int)w->units[i]->currentKind, (int)w->units[i]->currentHp);
                }
            }
            printf("\n");
            {
                /* what btlseqAct 0x600000A holds on: the audience's script, the BINGO's spin */
                BattleAudience* au = BattleAudienceBaseGetPtr();
                BreakSlotWork* sl = BattleBreakSlotGetPtr();
                printf("[story]   audience event %s (id %d), bingo phase %d timer %d cards %d evt %s, reels %d %d %d\n",
                       au->event ? "set" : "none", au->event ? (int)au->event->eventId : 0, (int)sl->phase,
                       (int)sl->phaseTimer, (int)sl->reelNo, sl->evt ? "set" : "none", (int)sl->reels[0].state,
                       (int)sl->reels[1].state, (int)sl->reels[2].state);
            }
            portStateSyncBattleDebug();
        }
        fflush(stdout);
        portShotRequest("story");
        sTurnsExit = 1;
    }
    if (sTurnsExit && ++sTurnsExit > 4 && !portShotPending()) {
        _Exit(6);
    }
    if (sAutoBattle && VIGetRetraceCount() % 60 == 0) {
        forceAssist();
    }
    if (sGod) {
        godFrame();
    }
    storyFrame();
}

/* --- battles --- */

/* --- every partner move once ($TTYD_MOVE_SWEEP with TTYD_AUTO_BATTLE: the partner_moves_all scenarios) ---
 * The partner's turns go down its move list, one row a turn, through the menu itself: the list opened from
 * the act menu, the cursor moved down to the row and A pressed, then A on the targets after a while on them
 * (every display the menu has drawn: the immunity crosses of "battle_immune_cross" too). Mario Defends. Each
 * command first puts back both sides' HP and the FP, so the fight lasts; $TTYD_MOVE_SWEEP_IMMUNE also makes
 * the foes take no status at all (none runs from Spook, is blown away, or eaten), so one battle sees every
 * move. Once the list's end is reached, both of them Run Away, until the partner or the rank changes (the
 * input script's "rank N"). Lines: "[movesweep] <tag> (row i of n, rank r): used", "... back to the menu"
 * (or "battle over") after it, "[movesweep] rank r: all n moves used". */
s32 portBtlcmdWeaponTable(BattleWork* wp, s32 type); /* battle_seq_command.c */
void BattleCommandDisplay_ActMenuEnd(BattleWork* wp);
void BattleCommandDisplay_WeaponSelectMenuEnd(BattleWork* wp);

static int sSweep, sSweepImmune;
static const char* sSweepOnly; /* $TTYD_MOVE_SWEEP_ONLY */
static int sSweepKey = -1;      /* the party member and rank the rows are of (party * 4 + rank) */
static int sSweepIndex;         /* the partner's next row */
static int sSweepCount;         /* the rows its list had */
static int sSweepDone;          /* the list's end reached for this key */
static int sSweepPhase;         /* 0 the act menu, 1 the move list, 2 the targets, 3 the move under way */
static int sSweepWait;
static u32 sSweepPad;           /* buttons pressed on the next pad read (portStoryTestPad) */
static char sSweepName[96];     /* the row being used */
static BattleUnitKind sSweepKinds[BATTLE_UNIT_MAX];
static StatusVulnerability sSweepNoStatus; /* (all 0: no status taken) */

/* the partner in the battle (its party number, by its unit's kind) and its rank */
static int sweepKey(BattleWork* wp, int* rank) {
    s32 party = 0, p, i;
    for (i = 0; i < BATTLE_UNIT_MAX && party == 0; i++) {
        const BattleUnit* u = wp->units[i];
        if (u == NULL || u->alliance != 0 || u->currentKind == UNIT_MARIO) {
            continue;
        }
        for (p = 1; p < 8; p++) {
            if ((s32)BattleTransPartyIdToUnitKind(p) == (s32)u->currentKind) {
                party = p;
                break;
            }
        }
    }
    *rank = party > 0 ? pouchGetPtr()->partyData[party].techLevel : 0;
    return party * 4 + *rank;
}

/* the move under way is over (the menu back, or the battle): the next row */
static void sweepMoveOver(const char* how) {
    if (sSweepPhase == 3) {
        printf("[movesweep] %s: %s\n", sSweepName, how);
        fflush(stdout);
        sSweepIndex++;
    }
    sSweepPhase = 0;
    sSweepWait = 0;
}

/* each command's start: both sides whole again, the FP full, the foes without statuses ($TTYD_MOVE_SWEEP_IMMUNE) */
static void sweepCommandInit(BattleWork* wp) {
    PouchData* pouch = pouchGetPtr();
    int i, rank;
    const int key = sweepKey(wp, &rank);
    if (key != sSweepKey) {
        sSweepKey = key;
        sSweepIndex = 0;
        sSweepDone = 0;
        printf("[movesweep] party %d rank %d\n", key / 4, rank);
        fflush(stdout);
    }
    sweepMoveOver("back to the menu");
    pouchSetFP(pouch->maximumFP);
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u == NULL || u->alliance == 2 || u->maxHp <= 0) {
            continue;
        }
        u->currentHp = u->maxHp;
        if (u->alliance == 1 && sSweepImmune && u->kindParams != NULL && u->kindParams != &sSweepKinds[i]) {
            sSweepKinds[i] = *u->kindParams;
            sSweepKinds[i].statusVulnerability = &sSweepNoStatus;
            u->kindParams = &sSweepKinds[i];
        }
    }
}

static void sweepCommandInput(BattleWork* wp, BattleUnit* unit) {
    BattleCommand* cmd = &wp->commandMenu;
    BattleCursor* cursor = &cmd->cursor[4]; /* (the partner's move list's: _btlcmd_GetCursorPtr) */
    if (unit->currentKind == UNIT_MARIO || sSweepDone) {
        if (cmd->state == 2 && ++sSweepWait >= 20) {
            sSweepWait = 0;
            if (sSweepDone) {
                portVersusCpuEscape(wp, unit);
            } else {
                portVersusCpuDefend(wp, unit);
            }
        }
        return;
    }
    switch (sSweepPhase) {
        case 0: /* the act menu: the list opened, or the next row */
            if (cmd->state != 2 || ++sSweepWait < 20) {
                return;
            }
            sSweepWait = 0;
            sSweepCount = portBtlcmdWeaponTable(wp, 6);
            if (sSweepOnly != NULL) {
                /* ($TTYD_MOVE_SWEEP_ONLY=Name,Name: only the rows of those names, as the menu shows them) */
                while (sSweepIndex < sSweepCount) {
                    const char* nm = cmd->weapon[sSweepIndex].name != NULL ? cmd->weapon[sSweepIndex].name : "";
                    const char* at = strstr(sSweepOnly, nm);
                    const size_t len = strlen(nm);
                    if (len > 0 && at != NULL && (at == sSweepOnly || at[-1] == ',') && (at[len] == ',' || at[len] == 0)) {
                        break;
                    }
                    sSweepIndex++;
                }
            }
            if (sSweepIndex >= sSweepCount) {
                printf("[movesweep] rank %d: all %d moves used\n", sSweepKey % 4, sSweepCount);
                fflush(stdout);
                sSweepDone = 1;
                portVersusCpuEscape(wp, unit);
                return;
            }
            snprintf(sSweepName, sizeof(sSweepName), "%s '%s' (row %d of %d, rank %d)",
                     cmd->weapon[sSweepIndex].weapon->name != NULL ? cmd->weapon[sSweepIndex].weapon->name : "?",
                     cmd->weapon[sSweepIndex].name != NULL ? cmd->weapon[sSweepIndex].name : "",
                     sSweepIndex + 1, sSweepCount, sSweepKey % 4);
            if (!cmd->weapon[sSweepIndex].enabled) {
                printf("[movesweep] %s: not usable now (reason %d), passed\n", sSweepName,
                       (int)cmd->weapon[sSweepIndex].disableReason);
                fflush(stdout);
                sSweepIndex++;
                return;
            }
            BattleCommandDisplay_ActMenuEnd(wp);
            cmd->actType = 6;
            cmd->state = 6; /* (the act menu's own step to the move list) */
            sSweepPhase = 1;
            return;
        case 1: /* the list: down to the row, then A */
            if (cmd->state != 7) {
                if (++sSweepWait > 600) {
                    printf("[movesweep] %s: STUCK (the list never opened, menu state %d)\n", sSweepName, (int)cmd->state);
                    fflush(stdout);
                    sSweepPhase = 3;
                    sweepMoveOver("skipped");
                }
                return;
            }
            if (++sSweepWait < 12 || sSweepWait % 4 != 0) {
                return;
            }
            if (cursor->absPos != sSweepIndex) {
                sSweepPad = cursor->absPos < sSweepIndex ? PAD_BUTTON_DOWN : PAD_BUTTON_UP;
                return;
            }
            sSweepPad = PAD_BUTTON_A;
            sSweepPhase = 2;
            sSweepWait = 0;
            return;
        case 2: /* the targets: a while on them, then A */
            ++sSweepWait;
            if (cmd->state == 7 && sSweepWait % 8 == 0) {
                sSweepPad = PAD_BUTTON_A; /* (the first A not taken) */
            }
            if (sSweepWait > 900) {
                printf("[movesweep] %s: STUCK (no targets, menu state %d)\n", sSweepName, (int)cmd->state);
                fflush(stdout);
                sSweepPhase = 3;
                sweepMoveOver("skipped");
                return;
            }
            if ((cmd->state == 10 || cmd->state == 11) && sSweepWait >= 40 && sSweepWait % 4 == 0) {
                printf("[movesweep] %s: used\n", sSweepName);
                fflush(stdout);
                sSweepPad = PAD_BUTTON_A;
                sSweepPhase = 3;
            }
            return;
        default: /* A pressed on the targets: the move starts (the next command, or the battle's end, says over) */
            if ((cmd->state == 10 || cmd->state == 11) && ++sSweepWait % 8 == 0) {
                sSweepPad = PAD_BUTTON_A;
            }
            return;
    }
}

static int commandInitPre(BattleWork** wp) {
    if (sSweep && !blocked()) {
        sweepCommandInit(*wp);
    }
    if (sAutoBattle && !blocked()) {
        portVersusCpuCommandBegins();
        /* a fight that never ends is a clear failure, not a long wait: past $TTYD_AUTO_BATTLE_TURNS
         * commands (Mario's and the partner's, default 60) "[story] STUCK", Run Away, and once the battle
         * is over (or 10 commands more) exit 6 */
        sQuiet = 0;
        if (++sTurns == 1) {
            portVersusCpuAutoReset();
            sRunLogged = 0;
            /* the battle's units, so a report names what was fought */
            BattleWork* w = *wp;
            const int foeHp = envInt("TTYD_FOE_HP", 0);
            int i;
            if (foeHp > 0) {
                /* (tests: foes that outlast a first hit, every game's alike before anyone acts) */
                for (i = 0; i < BATTLE_UNIT_MAX; i++) {
                    BattleUnit* u = w->units[i];
                    if (u != NULL && u->alliance == 1) {
                        u->maxHp = u->currentHp = (s16)foeHp;
                    }
                }
            }
            printf("[story] battle (story %d, map '%s'):", (int)swByteGet(GSW_STORY_PROGRESS), gp->currentMapName);
            for (i = 0; i < BATTLE_UNIT_MAX; i++) {
                BattleUnit* u = w->units[i];
                if (u != NULL && u->alliance != 2) {
                    printf(" %s%d hp %d/%d%s", u->alliance == 0 ? "ours " : "kind ", (int)u->currentKind,
                           (int)u->currentHp, (int)u->maxHp, i + 1 < BATTLE_UNIT_MAX ? ";" : "");
                }
            }
            printf("\n");
            fflush(stdout);
        }
        if (sTurns == sMaxTurns + 1) {
            printf("[story] STUCK: battle over %d turns (story %d, map '%s'); running away\n", sMaxTurns,
                   (int)swByteGet(GSW_STORY_PROGRESS), gp->currentMapName);
            dumpEvents();
            fflush(stdout);
            portShotRequest("story");
            sTurnsFailed = 1;
        } else if (sTurns > sMaxTurns + 10 && !sTurnsExit) {
            sTurnsExit = 1;
        }
    }
    return 0;
}

/* Run Away planned for this command (sRunFrom above) */
static int planRun(const BattleWork* wp) {
    int i, foes = 0;
    if (wp->fbatInfo != NULL && wp->fbatInfo->setup != NULL &&
        (wp->fbatInfo->setup->battleFlags & NPC_BATTLE_NO_ESCAPE)) {
        return 0;
    }
    if (sRunFrom > 0) {
        return sTurns >= sRunFrom;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = wp->units[i];
        const BattleUnitPart* part;
        if (u == NULL || u->alliance != 1 || u->currentHp <= 0) {
            continue;
        }
        for (part = u->parts; part != NULL; part = part->nextPart) {
            if (!(part->attributes & PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS)) {
                return 0; /* (a part that takes damage: a fight to win) */
            }
        }
        foes++;
    }
    return foes > 0;
}

static int commandInputPre(s32* r, void** work) {
    BattleWork* wp = (BattleWork*)*work;
    BattleUnit* unit;
    (void)r;
    if (!sAutoBattle || blocked() || portVersusOn()) {
        return 0;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (sSweep && unit != NULL && unit->alliance == 0 && !lecture(wp)) {
        sweepCommandInput(wp, unit);
        return 0;
    }
    if (sBingoPartner == 1 && unit != NULL && unit->alliance == 0 && unit->currentKind >= TYPE_PARTNER_MIN &&
        unit->currentKind != UNIT_MARIO && BattleBreakSlotGetPtr()->phase == 0) {
        /* ($TTYD_TEST_BINGO_PARTNER: every card turned as the partner's first command is chosen
         * (BattleBreakSlot_PointInc, as Stylish moves do), so the BINGO spins as its act is over with the
         * partner's the last turn: co-op's route then gave the first controller to the partner's player) */
        int turns;
        for (turns = 0; turns < 3 && pouchGetPtr()->starPowersObtained != 0 && BattleBreakSlotGetPtr()->reelNo < 3;
             turns++) {
            BattleBreakSlot_PointInc();
        }
        sBingoPartner = 2;
        printf("[story] test: the BINGO's cards turned in the partner's turn (%d)\n",
               (int)BattleBreakSlotGetPtr()->reelNo);
        fflush(stdout);
    }
    if (unit != NULL && unit->alliance == 0 && lecture(wp)) {
        /* a lesson (Frankly's, the partners' first fights): "End Lesson" when the menu has it; a step that
         * allows one move: the lesson's own move (versus_cpu_aurora.c's listOptions); everything locked:
         * nothing, the lesson goes on by itself. A move the menu refused was an error message every turn. */
        if ((wp->flags & BATTLE_FLAG_LECTURE_MENU) &&
            !(wp->flags & (BATTLE_FLAG_LECTURE_LOCK_ALL | BATTLE_FLAG_LECTURE_STEP2)) && wp->commandMenu.state == 2) {
            if (++sLectureThink >= 30) {
                sLectureThink = 0;
                portVersusCpuLectureEnd(wp, unit);
            }
        } else if (!(wp->flags & BATTLE_FLAG_LECTURE_LOCK_ALL)) {
            portVersusCpuAutoBoost(sGod ? sGodPower : 1);
            portVersusCpuCommandAuto(wp, unit, 0);
        }
    } else if (unit != NULL && unit->alliance == 0 && sTurns > sMaxTurns) {
        portVersusCpuEscape(wp, unit); /* (over the turn cap: Run Away, then the run fails) */
    } else if (unit != NULL && unit->alliance == 0 && planRun(wp)) {
        if (!sRunLogged) {
            sRunLogged = 1;
            printf("[story] battle (story %d, map '%s'): running away (%s)\n", (int)swByteGet(GSW_STORY_PROGRESS),
                   gp->currentMapName, sRunFrom > 0 ? "TTYD_AUTO_BATTLE_RUN" : "no foe takes damage");
            fflush(stdout);
        }
        portVersusCpuEscape(wp, unit);
    } else if (unit != NULL && unit->alliance == 0) {
        /* attacks only, so a run doesn't hang on the bag it starts with: items with TTYD_AUTO_BATTLE_ITEMS=1,
         * or, with no god mode, when Mario or the partner is close to a knock-out (deterministic: the
         * computer's own random numbers, from the same start each battle) */
        int items = sAutoItems > 0; /* (TTYD_AUTO_BATTLE_ITEMS=-1: never, not even near a knock-out) */
        if (!items && !sGod && sAutoItems == 0) {
            BattleUnit* mario = BattleGetMarioPtr(wp);
            BattleUnit* party = mario != NULL ? BattleGetPartnerPtr(wp, mario) : NULL;
            items = (mario != NULL && mario->currentHp * 4 <= mario->maxHp) ||
                    (party != NULL && party->currentHp > 0 && party->currentHp * 4 <= party->maxHp);
        }
        portVersusCpuAutoBoost(sGod ? sGodPower : 1);
        portVersusCpuCommandAuto(wp, unit, items); /* (once it has chosen, the menu's own last step makes the attack) */
    }
    return 0;
}

static int damagePre(s32* unitId, BattleUnit** target, BattleUnitPart** part, s32* damage, s32* fpDamage, u32* result,
                     u32* pattern, s32* flag) {
    (void)unitId, (void)part, (void)result, (void)pattern, (void)flag;
    if (*target != NULL && getenv("TTYD_SYNC_BATTLE_LOG") != NULL) {
        /* (online tests: each game's hits, to compare with the other's) */
        printf("[story] damage: unit %d hits unit %d (kind %d, HP %d): %d\n", (int)*unitId, (int)(*target)->unitId,
               (int)(*target)->currentKind, (int)(*target)->currentHp, (int)*damage);
        fflush(stdout);
    }
    if (blocked() || *target == NULL || portVersusOn()) {
        return 0;
    }
    if ((*target)->alliance == 0) {
        if (*damage > 0) {
            *damage = 0;
        }
        if (*fpDamage > 0) {
            *fpDamage = 0;
        }
    } else if ((*target)->alliance == 1 && sGodPower > 1 && *damage >= 0 && *unitId >= 0) {
        /* a hit by Mario's side: multiplied, and at least TTYD_GOD_POWER even when the enemy's defence took
         * it to 0 (a Bombshell Bill Blaster's 4: the fight never ended) */
        BattleUnit* attacker = BattleGetUnitPtr(_battleWorkPointer, *unitId);
        const BattleUnitPart* pt = *part;
        /* never through an immunity (a barrier, an invulnerable phase: Grodus while his four Grodus X stand,
         * the Shadow Queen's first form): those must stay as the game has them */
        const int immune = pt != NULL && ((pt->attributes & (PART_ATTR_NO_HP_LOSS | PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS)) ||
                                          (pt->defenseAttr != NULL && pt->defenseAttr->defenseAttrs[0] >= 2) ||
                                          ((*target)->attributeFlags & UNIT_ATTR_HP_LOCKED));
        if (attacker != NULL && attacker->alliance == 0 && !immune) {
            *damage = (*damage > 0 ? *damage : 1) * sGodPower;
        }
    }
    return 0;
}

/* --- the pads: the text advance, A now and then in battle --- */

static int choiceFor(const char* tag, const char* before, int num) {
    const char* p = sChoice;
    int fallback = 0;
    while (p != NULL && *p) {
        const char* colon = strchr(p, ':');
        const char* comma = strchr(p, ',');
        size_t n;
        if (colon == NULL || (comma != NULL && comma < colon)) {
            break;
        }
        n = (size_t)(colon - p);
        if (n == 1 && p[0] == '*') {
            fallback = atoi(colon + 1);
        } else if ((tag != NULL && strlen(tag) == n && strncmp(tag, p, n) == 0) ||
                   (before[0] && strlen(before) == n && strncmp(before, p, n) == 0)) {
            const int v = atoi(colon + 1);
            return v < 0 ? num + v : v;
        }
        p = comma != NULL ? comma + 1 : NULL;
    }
    return fallback < 0 ? num + fallback : fallback;
}

void portStoryTestPad(PADStatus* pads) {
    static u32 n;
    static char before[48]; /* the last message's tag, for the choice that follows it */
    static int idle, read;
    int sel = 0, num = 0, kind;
    const char* tag = NULL;
    if (!(sAutoText || sAutoBattle) || gp == NULL || blocked()) {
        return;
    }
    n++;
    if (sSweepPad != 0) {
        /* (the move sweep's press in the battle menu; up and down as the stick, which the battle's lists
         * read: battle_pad.c's BTL_PAD_STICK_UP/DOWN) */
        pads[0].err = PAD_ERR_NONE;
        if (sSweepPad & PAD_BUTTON_DOWN) {
            pads[0].stickY = -100;
        } else if (sSweepPad & PAD_BUTTON_UP) {
            pads[0].stickY = 100;
        } else {
            pads[0].button |= (u16)sSweepPad;
        }
        sSweepPad = 0;
        return;
    }
    if (sWalkFrames >= 0) {
        MarioWork* m = marioGetPtr();
        Vec to;
        if (strcmp(gp->currentMapName, sWalkMap) != 0 || swByteGet(GSW_STORY_PROGRESS) != sWalkStory ||
            ++sWalkFrames > 1200 || !walkTarget(&to)) {
            printf("[story] frame %u: walk to %s: %s after %d frames (Mario at %.0f %.0f %.0f)\n",
                   (unsigned)VIGetRetraceCount(), sWalkTo, sWalkFrames > 1200 ? "not reached" : "over", sWalkFrames,
                   m->position.x, m->position.y, m->position.z);
            fflush(stdout);
            sWalkFrames = -1;
        } else if (seqGetSeq() == SEQ_GAME && marioChkKey()) {
            /* as input_pc.c's walk_to: the stick towards it, a jump when stuck, then along the wall */
            const f32 dx = to.x - m->position.x, dz = to.z - m->position.z;
            const f32 len = sqrtf(dx * dx + dz * dz);
            if ((len < 6.0f || sWalkPast > 0) && strchr(sWalkTo, ':') != NULL) {
                /* there: on a little way the same way, so a line to cross is crossed, not only touched */
                if (++sWalkPast > 20) {
                    printf("[story] frame %u: walk to %s: there\n", (unsigned)VIGetRetraceCount(), sWalkTo);
                    fflush(stdout);
                    sWalkFrames = -1;
                    sWalkPast = 0;
                    return;
                }
                pads[0].err = PAD_ERR_NONE;
                pads[0].stickX = sWalkStickX;
                pads[0].stickY = sWalkStickY;
                return;
            }
            pads[0].err = PAD_ERR_NONE;
            if (len > 1.0f) {
                pads[0].stickX = (s8)(dx / len * 100.0f); /* (the camera looks down -z: up is -z) */
                pads[0].stickY = (s8)(-dz / len * 100.0f);
                sWalkStickX = pads[0].stickX;
                sWalkStickY = pads[0].stickY;
            }
            if (sWalkFrames % 20 == 0) {
                const f32 moved = fabsf(m->position.x - sWalkPrev.x) + fabsf(m->position.z - sWalkPrev.z);
                sWalkStuck = moved < 6.0f ? sWalkStuck + 1 : 0;
                sWalkPrev = m->position;
                if (sWalkStuck >= 2 && sWalkDetour <= 0) {
                    sWalkTries++;
                    sWalkDetour = 60 * (1 + sWalkTries / 2);
                    sWalkStuck = 0;
                }
            }
            if (sWalkStuck > 0 && sWalkFrames % 20 < 3) {
                pads[0].button |= PAD_BUTTON_A;
            }
            if (sWalkDetour > 0 && len > 1.0f) {
                const f32 side = sWalkTries % 2 ? 1.0f : -1.0f;
                sWalkDetour--;
                pads[0].stickX = (s8)(-dz / len * 100.0f * side);
                pads[0].stickY = (s8)(-dx / len * 100.0f * side);
            }
            return;
        }
    }
    kind = sAutoText ? portMsgWaiting(&sel, &num, &tag) : 0;
    if ((kind == 1 || kind == 3) && sAutoText < 2 && seqGetSeq() == SEQ_GAME && marioChkKey()) {
        /* a line shown while Mario can still move (Flavio's song in Podley's bar, mac_4_004): it closes
         * itself and waits for no one. A pressed there made Mario talk to whoever he faced, over and
         * over (the sailor Bob-omb's talk each time it ended): no press while Mario has control */
        kind = 0;
    }
    if (kind != 1 && kind != 3) {
        read = 0;
    }
    if (kind == 1 || kind == 3) {
        if (tag != NULL) {
            snprintf(before, sizeof(before), "%s", tag);
        }
        /* a slow reader (TTYD_AUTO_TEXT_DELAY): each message held that many frames first, so a
         * long scene lasts as long as it does for a player (its scripts' clocks past 53 s) */
        if (++read <= sReadFrames) {
            idle = 0;
            return;
        }
        if (n % 6 == 1) {
            read = 0; /* (after this press, the next page or message is held again) */
        }
        if (n % 6 < 2) {
            pads[0].err = PAD_ERR_NONE;
            pads[0].button |= PAD_BUTTON_A;
            if (seqGetSeq() == SEQ_BATTLE && portSecondPlayerMessagePad() > 0) {
                /* in the partner's turn a message reads its player's controller (second_player_pc.c), and
                 * headless counts a second one with nobody at it: A there too (the full run, win_00:
                 * Vivian's last words, btl_majo3_10, after Goombella's blow waited for ever) */
                const int p = portSecondPlayerMessagePad();
                pads[p].err = PAD_ERR_NONE;
                pads[p].button |= PAD_BUTTON_A;
            }
        }
        if (sAutoBattle && seqGetSeq() == SEQ_BATTLE && _battleWorkPointer != NULL &&
            !(_battleWorkPointer->flags & BATTLE_FLAG_COMMAND_INPUT) && lecture(_battleWorkPointer)) {
            /* (a lesson's ask stays on screen while it waits: its stick and B meanwhile, as below) */
            pads[0].err = PAD_ERR_NONE;
            if (n % 90 < 60) {
                pads[0].stickX = -127;
            } else if (n % 90 >= 75 && n % 90 < 77) {
                pads[0].button |= PAD_BUTTON_B;
            }
        }
        idle = 0;
        return;
    }
    if (kind == 2) {
        const int want = choiceFor(tag, before, num);
        if (n % 8 < 2) {
            pads[0].err = PAD_ERR_NONE;
            if (sel != want && want >= 0 && want < num) {
                pads[0].button |= PAD_BUTTON_DOWN;
            } else {
                printf("[story] frame %u: choice '%s' (after '%s'): option %d of %d\n", (unsigned)VIGetRetraceCount(),
                       tag != NULL ? tag : "", before, sel, num);
                fflush(stdout);
                pads[0].button |= PAD_BUTTON_A;
            }
        }
        idle = 0;
        return;
    }
    if (sAutoBattle && seqGetSeq() == SEQ_BATTLE && _battleWorkPointer != NULL &&
        (_battleWorkPointer->flags & BATTLE_FLAG_COMMAND_INPUT) && (_battleWorkPointer->flags & BATTLE_FLAG_LECTURE_LOCK_ALL)) {
        /* a lesson step with every command locked: it talks over the menu (its notices close with A), or
         * waits for X, the audience attack (Frankly's Star Power lesson: "stop the troublemaker"); nothing
         * can be picked, so neither does harm */
        if (n % 40 < 2) {
            pads[0].err = PAD_ERR_NONE;
            pads[0].button |= PAD_BUTTON_A;
        } else if (n % 40 >= 20 && n % 40 < 22) {
            pads[0].err = PAD_ERR_NONE;
            pads[0].button |= PAD_BUTTON_X;
        }
        return;
    }
    if (sAutoBattle && seqGetSeq() == SEQ_BATTLE && _battleWorkPointer != NULL &&
        !(_battleWorkPointer->flags & BATTLE_FLAG_COMMAND_INPUT)) {
        /* only once the battle is ending (the level-up's choice, a screen that waits for A): an A
         * during an attack spoiled its action command (an early press fails even with the assist:
         * one stomp of two, "Stylish"), so fights took several times the turns */
        static int lvups, lvupSeen; /* level-ups so far, and whether this one is counted */
        const s32 end = _battleWorkPointer->seqEndValue;
        if (end == 0x7000011 || end == 0x7000012) {
            /* the level-up (battle_seq_end.c): the curtain's "select one to upgrade" waits for the stick (left:
             * BP, right: HP; US and later), then the choice takes left/right and A. A alone waited for ever
             * (tik_15's Hammer Bros, level 20 to 21). The pick goes round HP, FP, BP, so a long run stays
             * balanced and plays the same every time */
            s32 want, at;
            if (!lvupSeen) {
                lvupSeen = 1;
                lvups++;
            }
            want = (lvups - 1) % 3;
            at = ((const s32*)(const void*)_battleWorkPointer->seqEndWork)[4]; /* SeqEndWork.selected (0x10) */
            pads[0].err = PAD_ERR_NONE;
            if (end == 0x7000011) {
                if (n % 20 < 4) {
                    pads[0].stickX = want == 2 ? -127 : 127; /* (a push, then let go: the event wants a trigger) */
                } else if (n % 30 < 2) {
                    pads[0].button |= PAD_BUTTON_A; /* (its messages) */
                }
            } else if (at != want) {
                if (n % 20 < 4) {
                    pads[0].stickX = at < want ? 127 : -127;
                }
            } else if (n % 30 < 2) {
                pads[0].button |= PAD_BUTTON_A;
            }
            return;
        }
        if (end != 0x7000013 && end != 0x7000014) {
            lvupSeen = 0;
        }
        if ((end != 0 || BattleBreakSlotGetPtr()->phase == 3) && n % 30 < 2) {
            /* (also BINGO!'s reels, which spin until A stops each one: the battle waited on them for ever,
             * in Grodus's fight after a Stylish-filled card) */
            pads[0].err = PAD_ERR_NONE;
            pads[0].button |= PAD_BUTTON_A;
            if (portSecondPlayerMessagePad() > 0) {
                /* spun after the partner's act, the reels read its player's controller (second_player_pc.c),
                 * and headless counts a second one with nobody at it: A there too (the full run, mri_06's
                 * Piders: the third reel waited for ever after Flurrie's blow) */
                const int p = portSecondPlayerMessagePad();
                pads[p].err = PAD_ERR_NONE;
                pads[p].button |= PAD_BUTTON_A;
            }
        } else if (lecture(_battleWorkPointer)) {
            /* a lesson's move waits for what the teacher asks, its own action command too (Frankly: "tilt the
             * stick left and hold it", then "let go"): the stick held left a while, let go, then A, over and
             * over */
            pads[0].err = PAD_ERR_NONE;
            if (n % 90 < 60) {
                pads[0].stickX = -127;
            } else if (n % 90 < 62) {
                pads[0].button |= PAD_BUTTON_A;
            } else if (n % 90 >= 75 && n % 90 < 77) {
                pads[0].button |= PAD_BUTTON_B; /* (the Superguard lesson asks for B) */
            }
        }
        return;
    }
    if (sAutoText && seqGetSeq() == SEQ_GAME && portStateSyncCoopGuest()) {
        /* a co-op guest: its own Mario never has keys (the host's world is played), so "no keys" doesn't
           mean something waits for a button here; an idle A would reach the guest's card or Start menu
           (players 2-4's, player_menu_pc.c) and the host's game as its seat's press. Its messages above
           are still read on. */
        idle = 0;
        return;
    }
    if (sAutoText && portOnlineEndUp()) {
        /* a room over for this player (online_end_pc.c's "where now"): Mario's keys are off while it's
           up, but it waits for the player's own choice ($TTYD_SYNC_END_PICK in tests), never an idle A */
        idle = 0;
        return;
    }
    if (sAutoText && seqGetSeq() == SEQ_GAME && (sAutoText >= 2 || !marioChkKey()) && yoshiGetStatus() == 0) {
        /* nothing to read: A now and then for a list to pick from or a screen that waits for a
         * button; with control too only with TTYD_AUTO_TEXT=2 (a chapter's title card waits with
         * Mario's keys on; a jump would spoil an input script's walk), and rarely, after the kicks
         * have had their turn (A in front of someone talks to them). Never on Yoshi's back: Mario's
         * keys are off there, the player steers Yoshi, and that A was Yoshi's flutter at a moment the
         * script never chose (the full run's Dubious Paper: the flutter spent before the script's A) */
        const int every = marioChkKey() ? 600 : 90;
        /* (never on the name-entry grid: each A there types a letter, as into chapter 7's access code) */
        if (++idle % every < 2 && idle > every && !portNameEntryOpen()) {
            pads[0].err = PAD_ERR_NONE;
            pads[0].button |= PAD_BUTTON_A;
        }
        return;
    }
    idle = 0;
}

#ifndef PORT_MODS
/* Builds without code mods (headless): no mods' host, so the hooks are
 * called straight from the game's functions (port/include/port/story_test.h),
 * each only once set up here, as the host would run them. */
static void* sDirect[4]; /* the command menu's start, its input, damage, the frame */

static void addDirect(int slot, void* fn) {
    sDirect[slot == kSlot_BattleCommandInit ? 0 : slot == kSlot_BattleCommandInput ? 1
            : slot == kSlot_BattleDamageDirect ? 2 : 3] = fn;
}
#define portModHostAddHook(kind, slot, fn) addDirect(slot, fn)

void portStoryTestCommandInitPre(void** wp) {
    if (sDirect[0] != NULL) {
        commandInitPre((BattleWork**)wp);
    }
}

void portStoryTestCommandInputPre(void** work) {
    if (sDirect[1] != NULL) {
        s32 r = 0;
        commandInputPre(&r, work);
    }
}

void portStoryTestDamagePre(s32* unitId, void** target, void** part, s32* damage, s32* fpDamage, u32* result,
                            u32* pattern, s32* flag) {
    if (sDirect[2] != NULL) {
        damagePre(unitId, (BattleUnit**)target, (BattleUnitPart**)part, damage, fpDamage, result, pattern, flag);
    }
}

void portStoryTestFramePost(void) {
    if (sDirect[3] != NULL) {
        frameHook();
    }
}
#endif

void portStoryTestInit(void) {
    const char* v;
    int any;
    sAutoBattle = envInt("TTYD_AUTO_BATTLE", 0);
    sSweep = sAutoBattle && envInt("TTYD_MOVE_SWEEP", 0);
    sSweepImmune = envInt("TTYD_MOVE_SWEEP_IMMUNE", 0);
    sSweepOnly = getenv("TTYD_MOVE_SWEEP_ONLY");
    sAutoItems = envInt("TTYD_AUTO_BATTLE_ITEMS", 0);
    sAssist = envInt("TTYD_AUTO_BATTLE_ASSIST", 2);
    sMaxTurns = envInt("TTYD_AUTO_BATTLE_TURNS", 60);
    sRunFrom = envInt("TTYD_AUTO_BATTLE_RUN", 0);
    sMaxQuiet = envInt("TTYD_AUTO_BATTLE_FRAMES", 3600);
    sGod = envInt("TTYD_GOD", 0);
    sGodPower = envInt("TTYD_GOD_POWER", 1);
    sBingoPartner = envInt("TTYD_TEST_BINGO_PARTNER", 0) ? 1 : 0;
    sAutoText = envInt("TTYD_AUTO_TEXT", 0);
    sReadFrames = envInt("TTYD_AUTO_TEXT_DELAY", 0);
    sWatch = envInt("TTYD_STORY_WATCH", 0);
    sControlWatch = envInt("TTYD_CONTROL_WATCH", 0);
    sUntil = envInt("TTYD_STORY_UNTIL", -1);
    sSettle = envInt("TTYD_STORY_SETTLE", 300);
    sKickAfter = envInt("TTYD_STORY_KICK_AFTER", 240);
    sEventRow = envInt("TTYD_EVENT", -1);
    sChoice = getenv("TTYD_AUTO_CHOICE");
    sUntilControl = envInt("TTYD_STORY_UNTIL_CONTROL", 0);
    sArrived = sEventRow < 0;
    if ((v = getenv("TTYD_STORY_UNTIL_MAP")) != NULL) {
        snprintf(sUntilMap, sizeof(sUntilMap), "%s", v);
    }
    any = sAutoBattle || sGod || sAutoText || sWatch > 0 || sControlWatch > 0 || sUntil >= 0 || sEventRow >= 0 || sUntilMap[0] ||
          getenv("TTYD_STORY_KICK") != NULL;
    if (!any) {
        return; /* normal play: no hooks at all */
    }
    if ((v = getenv("TTYD_STORY_KICK")) != NULL) {
        while (*v && sKickCount < MAX_KICKS) {
            const size_t n = strcspn(v, ",");
            snprintf(sKickName[sKickCount], sizeof(sKickName[0]), "%.*s", (int)n, v);
            sKick[sKickCount] = (strncmp(sKickName[sKickCount], "walk:", 5) == 0 || strncmp(sKickName[sKickCount], "case:", 5) == 0)
                                      ? (void*)sKickName[sKickCount]
                                  : findSymbol(sKickName[sKickCount] + (strncmp(sKickName[sKickCount], "talk:", 5) == 0 ? 5 : 0));
            if (sKick[sKickCount] == NULL) {
                printf("[story] kick: no script '%s' in this executable\n", sKickName[sKickCount]);
            } else {
                sKickCount++;
            }
            v += n;
            if (*v == ',') {
                v++;
            }
        }
        fflush(stdout);
    }
    if (sAutoBattle) {
        forceAssist();
        portModHostAddHook(0, kSlot_BattleCommandInit, (void*)commandInitPre);
        portModHostAddHook(0, kSlot_BattleCommandInput, (void*)commandInputPre);
    }
    if (sGod) {
        portModHostAddHook(0, kSlot_BattleDamageDirect, (void*)damagePre);
    }
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    printf("[story] test hooks:%s%s%s%s%s%s%s%s\n", sAutoBattle ? " auto-battle" : "", sSweep ? " move-sweep" : "",
           sGod ? " god" : "",
           sAutoText ? " auto-text" : "", sWatch > 0 ? " watch" : "", sControlWatch > 0 ? " control-watch" : "",
           sEventRow >= 0 ? " event" : "",
           sKickCount ? " kick" : "");
    fflush(stdout);
}

/* --- an input script's "key NAME [FRAMES]" (port/story_test.h): a keyboard key as the window's own event
   (Aurora's: this file is in the headless build too, which has no window) --- */
#ifdef TTYD_BACKEND_AURORA
#include <SDL3/SDL.h>

int portStoryTestKey(const char* name, int down) {
    SDL_Event e;
    const SDL_Scancode sc = SDL_GetScancodeFromName(name);
    if (sc == SDL_SCANCODE_UNKNOWN) {
        return 0;
    }
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    e.key.down = down != 0;
    {
        /* (the game's window's: Dear ImGui takes a key only for a window of its own) */
        int n = 0;
        SDL_Window** windows = SDL_GetWindows(&n);
        if (windows != NULL && n > 0) {
            e.key.windowID = SDL_GetWindowID(windows[0]);
        }
        SDL_free(windows);
    }
    return SDL_PushEvent(&e) ? 1 : 0;
}
#endif
