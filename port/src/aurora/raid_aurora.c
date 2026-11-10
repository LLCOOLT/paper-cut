/* The raid boss (port/include/port/raid.h, docs/RAID.md): the week's boss,
 * one of the game's own, on a small map of the mode's own in its area (as
 * the boss rush's maps are, modes_aurora.c: the area's REL and its battle
 * table, the boss's own stage, audience and music), its battle started from
 * a hidden stand-in NPC. Mario's side is the boss rush's late-game loadout,
 * lent for the fight (badges as they are); losing or fleeing is allowed,
 * and either way Mario is back in the room he left.
 *
 * The fight, through the game's battle hooks (the host's, as code mods use):
 *   - BtlUnit_Entry: the boss's HP set to the raid's (by the party's size);
 *   - BattleCheckDamage, before: its side's attacks as a changed copy of the
 *     weapon (the game's weapon data is never written): stronger, piercing in
 *     the fury, with the status of the attack its phase borrows; and each
 *     Superguard of a hero (Mario, Luigi, the partner) seen for the fury;
 *   - BattleCalculateDamage, after: the damage on the boss as its phase has
 *     it (the shell's 1, the fury's half, a stagger's 1.5x, a duo move's
 *     double in the last stand), and never past its phase's line while the
 *     phase's condition isn't met, so no hit skips a phase; on Mario's side,
 *     the raid's extra attack.
 * Phases go by the boss's HP (75 %, 50 %, 25 %); each is decided where the
 * damage is, on the game's frame, so online games decide alike. */
#include "port/raid.h"
#include "port/date_seed.h"
#include "port/duo.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/moderun.h"
#include "port/modes.h"
#include "port/mods.h"
#include "port/party.h"
#include "port/platform.h" /* portLockstep: the game clock is the session's (online, replays) */
#include "port/players.h"
#include "port/pm64.h"
#include "port/achievements.h"
#include "port/brothers.h"
#include "port/statesync.h"
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_damage.h"
#include "battle/battle_database_common.h"
#include "battle/battle_status_effect.h"
#include "battle/battle_unit.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include <dolphin/os.h>

#include <stdio.h>

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture */
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK


/* --- the bosses and what their phases borrow ------------------------------------- */

/* An attack a phase can borrow: the status another boss's attack gives (a
 * chance and turns like its own), or its strength, or its piercing. */
typedef struct RaidBorrow {
    const char* name;
    int status; /* StatusEffectType, or -1 */
    int chance, turns, strength;
    int attack; /* added to each hit */
    int pierce; /* pierces defence */
} RaidBorrow;

static const RaidBorrow kBorrows[] = {
    {"Hooktail's flame", STATUS_BURN, 50, 2, 1, 0, 0},
    {"Gloomtail's poison breath", STATUS_POISON, 60, 3, 1, 0, 0},
    {"Bonetail's sleepy breath", STATUS_SLEEP, 35, 2, 0, 0, 0},
    {"Macho Grubba's Power Lift", -1, 0, 0, 0, 2, 0},
    {"Smorg's miasma", STATUS_DIZZY, 40, 2, 0, 0, 0},
    {"Magnus 2.0's lasers", -1, 0, 0, 0, 0, 1},
    {"Beldam's Ice Blast", STATUS_FREEZE, 30, 1, 0, 0, 0},
    {"Doopliss's tricks", STATUS_CONFUSE, 40, 2, 0, 0, 0},
    {"Sir Grodus's staff", STATUS_STOP, 25, 1, 0, 0, 0},
    {"the Shadow Queen's curse", STATUS_TINY, 50, 3, 1, 0, 0},
};
#define BORROWS ((int)(sizeof(kBorrows) / sizeof(kBorrows[0])))

/* A raid boss: its map (the area's prefix, so its REL; "_r0" is the raid's),
 * the room whose files the map reads, the area battle table's entry (the
 * boss rush's and the story's), the boss's unit kind, its raid HP for two
 * players, its own borrow (never borrowed from itself). */
typedef struct RaidBoss {
    const char* map;
    const char* files;
    s32 battle;
    int unit;
    int hp;
    int self;
    const char* name;
} RaidBoss;

static const RaidBoss kBosses[] = {
    {"jon_r0", "jon_06", 51, UNIT_BONETAIL, 260, 2, "Bonetail"},
    {"gon_r0", "gon_11", 20, UNIT_HOOKTAIL, 160, 0, "Hooktail"},
    {"aji_r0", "aji_00", 5, UNIT_MAGNUS2_0, 220, 5, "Magnus von Grapple 2.0"},
    {"las_r0", "las_26", 14, UNIT_GLOOMTAIL, 240, 1, "Gloomtail"},
    {"tou_r0", "tou_03", 23, UNIT_MACHO_GRUBBA, 180, 3, "Macho Grubba"},
    {"rsh_r0", "rsh_06_a", 1, UNIT_SMORG, 200, 4, "Smorg"},
};
#define BOSSES ((int)(sizeof(kBosses) / sizeof(kBosses[0])))

/* each phase's extra attack on Mario's side, and the shell's hits without duo moves */
static const int kPhaseAttack[PORT_RAID_PHASES] = {1, 2, 3, 2};
#define SHELL_HITS 10

int portRaidBossCount(void) {
    return BOSSES;
}

const char* portRaidBossName(int boss) {
    return boss >= 0 && boss < BOSSES ? kBosses[boss].name : "";
}

int portRaidBorrowCount(void) {
    return BORROWS;
}

const char* portRaidBorrowName(int borrow) {
    return borrow >= 0 && borrow < BORROWS ? kBorrows[borrow].name : "";
}

/* --- the week ----------------------------------------------------------------------- */

/* days since 1970-01-01 (UTC): the lockstep clock's (date_seed.h), read without moving it (a game
 * clock read moves it 1 us: the page and the menu's rows ask too) */
static unsigned dayNow(void) {
    return portDateDay();
}

int portRaidWeek(void) {
    return (int)((dayNow() + 3u) / 7u); /* 1970-01-01 was a Thursday: weeks from Monday */
}

void portRaidWeekText(int week, char* out, int size) {
    const int monday = week * 7 - 3;
    if (size <= 0) {
        return;
    }
    if (monday < 0) {
        snprintf(out, (size_t)size, "-");
        return;
    }
    portDateText((unsigned)monday, out, (size_t)size);
}

/* The boss goes round the list week by week; the two borrows are drawn from the week's seed. */
int portRaidOfWeek(int week, int* borrowA, int* borrowB) {
    const int boss = (int)((unsigned)week % (unsigned)BOSSES);
    PortSeedRand r;
    int a, b;
    portSeedRandInit(&r, (unsigned)week * 7u + 0x5241u);
    do {
        a = (int)portSeedRandNext(&r, BORROWS);
    } while (a == kBosses[boss].self);
    do {
        b = (int)portSeedRandNext(&r, BORROWS);
    } while (b == kBosses[boss].self || b == a);
    if (borrowA != NULL) {
        *borrowA = a;
    }
    if (borrowB != NULL) {
        *borrowB = b;
    }
    return boss;
}

int portRaidHp(int boss, int players) {
    if (boss < 0 || boss >= BOSSES) {
        return 0;
    }
    players = players < 2 ? 2 : players > 4 ? 4 : players;
    return kBosses[boss].hp * (4 + players - 2) / 4; /* 2 players 100 %, 3 125 %, 4 150 % */
}

/* --- the run ------------------------------------------------------------------------ */

static PortRaidStatus sSt;
static PortRaidResult sLast;
static int sAsk;            /* a start asked for in the game's frame */
static int sAskThread;      /* __atomic: from the settings page */
static char sReturnMap[16];
static PouchData sSavedPouch;
static int sPouchSaved;
static int sBattleOn;       /* this battle is the raid's */
static int sBossUnit = -1;  /* the boss's unit id */
static unsigned long long sGuarded; /* phase 3: the units (ids) that Superguarded */
static int sGuardTotal;     /* phase 3: Superguards from anyone */
static int sStaggered;      /* phase 3: everyone Superguarded */
static int sShellBroken;    /* phase 2 */
static unsigned sFights;    /* the raid's battles begun here (unitSetupPre): a guest's join seen by it */
static unsigned sFights0;   /* sFights when this raid began (begin) */
static unsigned sHostAway;  /* a guest: frames the host's game has been in the field outside the boss's room */
static int sHostSeen;       /* a guest: the host's game seen in the boss's room (this raid) */

int portRaidActive(void) {
    return sSt.active;
}

const PortRaidStatus* portRaidStatus(void) {
    return &sSt;
}

const PortRaidResult* portRaidLast(void) {
    return &sLast;
}

static int otherModeRuns(void) {
    const PortModeStatus* m = portModeStatus();
    return (m->mode != PORT_MODE_NONE && !m->finished) || portVersusOn();
}

static int playersNow(void) {
    const char* solo = getenv("TTYD_RAID_SOLO"); /* tests: one player may start it */
    int n = portCoopPlayers();
    if (portStateSyncLinked() && portStateSyncPlayersNow() > n) {
        n = portStateSyncPlayersNow(); /* (each player in their own game: one shared boss fight) */
    }
    if (n < 2 && portBrothersOn()) {
        n = 2; /* Brothers mode's Luigi is a second hero, played as the mode has him */
    }
    return solo != NULL && n < 2 ? 2 : n;
}

/* why a raid can't start now, or NULL (no side effects: the settings page asks it on its button) */
const char* portRaidRefusal(void) {
    const char* refusal;
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        return "Start it in the field, with a file loaded.";
    }
    if (sSt.active || sAsk) {
        return "A raid is already on.";
    }
    if (portStateSyncPartyRole() == 2) {
        return "The host starts a raid: their game's is everyone's.";
    }
    if (otherModeRuns()) {
        return "Another mode is running: finish it first.";
    }
    if ((refusal = portModeStartRefusal()) != NULL) { /* (any mode lending, indoors, the story's stretches) */
        return refusal;
    }
    if (playersNow() < 2) {
        return "A raid needs two players or more: online, a second controller, or Brothers mode's Luigi.";
    }
    return NULL;
}

int portRaidStart(const char** why) {
    if ((*why = portRaidRefusal()) != NULL) {
        return 0;
    }
    sAsk = 1;
    return 1;
}

static const char* sAskRefusal; /* __atomic: the page's ask refused, and why */

void portRaidAsk(void) {
    __atomic_store_n(&sAskRefusal, (const char*)NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sAskThread, 1, __ATOMIC_SEQ_CST);
}

const char* portRaidAskRefusal(void) {
    return __atomic_load_n(&sAskRefusal, __ATOMIC_SEQ_CST);
}

/* the boss rush's late-game loadout (modes_aurora.c), lent */
static void lendLoadout(void) {
    static const s16 kItems[8] = {ITEM_ULTRA_SHROOM, ITEM_ULTRA_SHROOM, ITEM_ULTRA_SHROOM, ITEM_LIFE_SHROOM,
                                  ITEM_JAMMIN_JELLY, ITEM_JAMMIN_JELLY, ITEM_MAPLE_SYRUP,  ITEM_THUNDER_RAGE};
    PouchData* p = pouchGetPtr();
    s32 i;
    sSavedPouch = *p;
    sPouchSaved = 1;
    portPm64JoinHold(1); /* (the lent partners aren't joins: no PM64 partner by them, Watt's mark stays) */
    for (i = 1; i <= 7; i++) {
        partyJoin((PartyId)i);
        p->partyData[i].attackLevel = 2;
        p->partyData[i].HPLevel = 2;
        p->partyData[i].techLevel = 2;
    }
    p->level = 30;
    p->baseMaxHP = p->maximumHP = p->currentHP = 60;
    p->baseMaxFP = p->maximumFP = p->currentFP = 50;
    p->maximumBP = 51;
    p->availableBP = p->maximumBP;
    portModeLendGear(3, 3); /* (the late game's Ultra Boots and Hammer: they were the Super ones) */
    p->starPowersObtained = 0xFF;
    p->maximumSP = p->currentSP = 800;
    memset(p->heldItems, 0, sizeof(p->heldItems));
    for (i = 0; i < 8; i++) {
        pouchGetItem((ItemType)kItems[i]);
    }
    portModeLendRevise(1);
    printf("[raid] loadout lent: level %d, HP %d, FP %d, BP %d, boots %d, hammer %d\n", p->level, p->maximumHP,
           p->maximumFP, p->maximumBP, (int)pouchGetJumpLv(), (int)pouchGetHammerLv());
    fflush(stdout);
}

static void giveBack(void) {
    if (sPouchSaved) {
        *pouchGetPtr() = sSavedPouch;
        sPouchSaved = 0;
        portPm64JoinHold(0);
    }
}

/* week, players: the host's (a guest under sync), or -1: this game's own */
static void begin(int week, int players) {
    memset(&sSt, 0, sizeof(sSt));
    sSt.active = 1;
    sSt.week = week >= 0 ? week : portRaidWeek();
    sSt.boss = portRaidOfWeek(sSt.week, &sSt.borrowA, &sSt.borrowB);
    sSt.players = players > 0 ? players : playersNow();
    portStateSyncRaidSend(sSt.week, sSt.players); /* (the host: the guests' games start it too) */
    sSt.phase = 1;
    sSt.shellNeeded = SHELL_HITS;
    sHostAway = 0;
    sHostSeen = 0;
    sFights0 = sFights;
    snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
    lendLoadout();
    printf("[raid] week %d: %s (%s, %s), %d players\n", sSt.week, kBosses[sSt.boss].name,
           kBorrows[sSt.borrowA].name, kBorrows[sSt.borrowB].name, sSt.players);
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, kBosses[sSt.boss].map, NULL);
}

/* the raid over: 1 won, 2 lost, fled or stopped */
static void finish(int how) {
    if (!sSt.active) {
        return;
    }
    giveBack();
    sLast.result = how;
    sLast.week = sSt.week;
    sLast.boss = sSt.boss;
    sLast.players = sSt.players;
    sLast.turns = sSt.turns;
    sLast.phase = sSt.phase;
    sLast.frames = sSt.frames;
    sLast.serial++;
    memset(sLast.names, 0, sizeof(sLast.names));
    if (portStateSyncLinked()) {
        /* each in their own game: the players playing with this one now, by player number (a
           watcher's game sends a name too, but plays no part: theirs is left out) */
        int p, k = 0;
        for (p = 1; p <= SS_PLAYERS && k < PORT_RAID_NAMES; p++) {
            const SsPeer* who = ssPeerOf(p);
            const char* name = portStateSyncPlayerName(p);
            if (name[0] != '\0' && (p == ssNetMe() || (who->valid && !who->watch))) {
                snprintf(sLast.names[k], sizeof(sLast.names[k]), "%s", name);
                k++;
            }
        }
    }
    if (how == 1) {
        pouchAddCoin(50); /* (after the lent pouch went back: the coins stay) */
        /* the achievements' counters: draw-only, kept beside the memory cards */
        portAchievementCounterAdd("raid_wins", 1);
        {
            const int bosses = portAchievementCounter("raid_bosses") | (1 << sSt.boss); /* (bits) */
            int k, kinds = 0;
            for (k = 0; k < BOSSES; k++) {
                kinds += (bosses >> k) & 1;
            }
            portAchievementCounterMax("raid_bosses", bosses);
            portAchievementCounterMax("raid_kinds", kinds);
        }
        if (sSt.players >= 4) {
            portAchievementCounterMax("raid_full", 1);
        }
    }
    printf("[raid] %s: week %d, %s, %u frames, %d turns\n", how == 1 ? "won" : "over", sSt.week,
           kBosses[sSt.boss].name, sSt.frames, sSt.turns);
    fflush(stdout);
    memset(&sSt, 0, sizeof(sSt));
    sBattleOn = 0;
    sBossUnit = -1;
}

/* --- the fight's script ------------------------------------------------------------- */

USER_FUNC(evt_raid_battle);
USER_FUNC(evt_raid_rules);
USER_FUNC(evt_raid_battle_end);
USER_FUNC(evt_raid_wait_others);
USER_FUNC(evt_raid_join_host);

static EVT_BEGIN(raid_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_raid_battle, LW(0))
    IF_INT_EQUAL(LW(0), -1)
        RETURN
    END_IF
    CALL(evt_npc_entry, STRING("raid_foe"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("raid_foe"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("raid_foe"), 0, -1000, 0)
    CALL(evt_npc_set_battle_info, STRING("raid_foe"), LW(0))
    CALL(evt_raid_rules)
    CALL(evt_raid_wait_others)
    CALL(evt_raid_join_host, LW(1))
    IF_INT_EQUAL(LW(1), 0)
        CALL(evt_npc_battle_start, STRING("raid_foe"))
        CALL(evt_npc_wait_battle_end)
    END_IF
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_raid_battle_end, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(raid_map_init)
    SPAWN_EVENT(raid_fight_evt)
    RETURN
EVT_END()

/* out: the battle table's entry, or -1 (no raid, or not its map) */
USER_FUNC(evt_raid_battle) {
    s32 battle = -1;
    (void)isFirstCall;
    if (sSt.active && strcmp(gp->currentMapName, kBosses[sSt.boss].map) == 0) {
        battle = kBosses[sSt.boss].battle;
    }
    evtSetValue(event, event->args[0], battle);
    return EVT_RETURN_DONE;
}

/* losing is allowed (no Game Over: Mario keeps 1 HP, and the loadout goes back anyway) */
USER_FUNC(evt_raid_rules) {
    NpcEntry* npc = npcNameToPtr_NoAssert("raid_foe");
    (void)event;
    (void)isFirstCall;
    if (npc != NULL) {
        npc->battleInfo.battleFlags |= NPC_BATTLE_LOSS_OK;
        npc->battleInfo.battleFlags &= ~NPC_BATTLE_NO_ESCAPE; /* fleeing ends the raid */
    }
    return EVT_RETURN_DONE;
}

/* Under sync (each player in their own game), the boss fight is one shared battle: the host's game
 * starts it once every other player's game has come to the boss's room (or 15 s on), and invites
 * them (statesync_battle_pc.c); a guest's game doesn't start its own but joins the host's. */
#define RAID_WAIT_FRAMES (15 * 60)
#define RAID_JOIN_FRAMES (25 * 60)

/* the host: the others' games here, or long enough */
USER_FUNC(evt_raid_wait_others) {
    static int sFrames;
    (void)event;
    if (isFirstCall) {
        sFrames = 0;
    }
    if (portStateSyncPartyRole() != 1 || portStateSyncOthersAway() == 0) {
        return EVT_RETURN_DONE;
    }
    if (++sFrames >= RAID_WAIT_FRAMES) {
        printf("[raid] %d player(s) not in the boss's room after 15 s: starting without them\n",
               portStateSyncOthersAway());
        fflush(stdout);
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* a guest: out 1 when the host's fight was joined and is over (its result is this game's), 0 when
   this game fights alone (not a guest, or no invitation came) */
USER_FUNC(evt_raid_join_host) {
    static int sFrames, sJoined;
    if (isFirstCall) {
        sFrames = 0;
        sJoined = 0;
    }
    if (!sSt.active) {
        /* (the raid ended here meanwhile, frameHook's host-gone check: no fight of this game's own) */
        evtSetValue(event, event->args[0], 1);
        return EVT_RETURN_DONE;
    }
    if (portStateSyncPartyRole() != 2) {
        evtSetValue(event, event->args[0], sJoined);
        return EVT_RETURN_DONE;
    }
    /* (the host's fight joined: statesync_battle_pc.c's takeInvite starts the battle sequence in its own
       frame and the field's scripts don't run in a battle, so this script may never see it under way; the
       raid's battle begun here is the sign, sFights. Counted from the raid's start, not this script's first
       call: a guest that arrives with the host's invitation kept takes it within a frame or two of coming
       in, before the script's opening wait is over, so the fight was already behind it here and the script
       waited 25 s for one that never came, its raid ended only by hostRaidOver: mp_party_raid2) */
    if (gp->inBattle || seqGetSeq() == SEQ_BATTLE || sFights != sFights0) {
        sJoined = 1;
    }
    if (gp->inBattle || seqGetSeq() == SEQ_BATTLE) {
        return EVT_RETURN_BLOCK;
    }
    if (sJoined) {
        if (seqGetSeq() != SEQ_GAME || seqCheckSeq()) {
            return EVT_RETURN_BLOCK; /* (on the way back to the field) */
        }
        evtSetValue(event, event->args[0], 1);
        return EVT_RETURN_DONE;
    }
    if (++sFrames >= RAID_JOIN_FRAMES) {
        printf("[raid] no invitation to the host's fight after 25 s: fighting alone\n");
        fflush(stdout);
        evtSetValue(event, event->args[0], 0);
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}

/* in: the result (1 won, 3 lost, 4 fled) */
USER_FUNC(evt_raid_battle_end) {
    const s32 result = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (sSt.active) {
        finish(result == 1 ? 1 : 2);
        seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    }
    return EVT_RETURN_DONE;
}

/* --- the fight ------------------------------------------------------------------------ */

static BattleUnit* bossUnit(void) {
    BattleWork* wp = _battleWorkPointer;
    if (!sBattleOn || wp == NULL || sBossUnit < 0 || sBossUnit >= BATTLE_UNIT_MAX) {
        return NULL;
    }
    return wp->units[sBossUnit];
}

static int lineOf(int phase, int maxHp) {
    return phase >= PORT_RAID_PHASES ? 0 : maxHp * (PORT_RAID_PHASES - phase) / PORT_RAID_PHASES;
}

static int isHero(const BattleUnit* u) {
    return u != NULL && u->alliance == 0 && u->currentHp > 0 &&
           (u->currentKind == UNIT_MARIO || (u->currentKind >= UNIT_GOOMBELLA && u->currentKind <= UNIT_MS_MOWZ));
}

/* the heroes standing (Mario, Luigi, the partner), as unit-id bits */
static unsigned long long heroesStanding(int* count) {
    BattleWork* wp = _battleWorkPointer;
    unsigned long long bits = 0;
    int i, n = 0;
    for (i = 0; wp != NULL && i < BATTLE_UNIT_MAX; i++) {
        if (isHero(wp->units[i])) {
            bits |= 1ull << i;
            n++;
        }
    }
    *count = n;
    return bits;
}

/* phase 3's condition: every hero standing Superguarded once (or as many Superguards as twice
 * their number, from whoever the fury picked on) */
static int furyStopped(void) {
    int n;
    const unsigned long long heroes = heroesStanding(&n);
    return n > 0 && ((sGuarded & heroes) == heroes || sGuardTotal >= 2 * n);
}

static int gateOpen(void) {
    switch (sSt.phase) {
        case 1: return 1;
        case 2: return sShellBroken;
        case 3: return sStaggered;
        default: return 1;
    }
}

static void setPhase(int phase) {
    sSt.phase = phase;
    sGuarded = 0;
    sGuardTotal = 0;
    sStaggered = 0;
    sShellBroken = 0;
    sSt.shellHits = 0;
    printf("[raid] phase %d at turn %d\n", phase, sSt.turns);
    fflush(stdout);
}

/* the phase moves on once the boss is at its line and the phase's condition is met */
static void advance(int hp, int maxHp) {
    while (sSt.phase < PORT_RAID_PHASES && gateOpen() && hp <= lineOf(sSt.phase, maxHp)) {
        setPhase(sSt.phase + 1);
    }
}

/* Btl_UnitSetup(wp), before: this battle is the raid's */
static int unitSetupPre(BOOL* result, BattleWork** wpp) {
    (void)result;
    (void)wpp;
    sBattleOn = sSt.active && gp != NULL && strcmp(gp->currentMapName, kBosses[sSt.boss].map) == 0;
    sBossUnit = -1;
    if (sBattleOn) {
        sFights++;
        sSt.inFight = 1;
        sSt.frames = 0;
        setPhase(1);
    }
    return 0;
}

/* BtlUnit_Entry(setup), after: the boss at the raid's HP */
static void entryPost(BattleUnit** result, BattleUnitSetup* setup) {
    BattleUnit* unit = *result;
    int hp;
    (void)setup;
    if (!sBattleOn || unit == NULL || unit->alliance != 1 || (int)unit->initialKind != kBosses[sSt.boss].unit ||
        sBossUnit >= 0) {
        return;
    }
    hp = portRaidHp(sSt.boss, sSt.players);
    hp = hp > 999 ? 999 : hp;
    unit->baseMaxHp = (s16)hp;
    unit->maxHp = (s16)hp;
    unit->currentHp = (s16)hp;
    sBossUnit = unit->unitId;
}

/* the weapon's status from a borrowed attack (the larger chance wins) */
static void borrowStatus(BattleWeapon* w, const RaidBorrow* b) {
    u8* chance = NULL;
    s8* time = NULL;
    s8* strength = NULL;
    s8 sign = 1;
    switch (b->status) {
        case STATUS_SLEEP: chance = &w->sleepChance; time = &w->sleepTime; break;
        case STATUS_STOP: chance = &w->stopChance; time = &w->stopTime; break;
        case STATUS_DIZZY: chance = &w->dizzyChance; time = &w->dizzyTime; break;
        case STATUS_POISON: chance = &w->poisonChance; time = &w->poisonTime; strength = &w->poisonStrength; break;
        case STATUS_CONFUSE: chance = &w->confuseChance; time = &w->confuseTime; break;
        case STATUS_BURN: chance = &w->burnChance; time = &w->burnTime; break;
        case STATUS_FREEZE: chance = &w->freezeChance; time = &w->freezeTime; break;
        case STATUS_TINY:
            chance = &w->sizeChangeChance; time = &w->sizeChangeTime; strength = &w->sizeChangeStrength; sign = -1;
            break;
        default: return;
    }
    if (*chance >= (u8)b->chance) {
        return;
    }
    *chance = (u8)b->chance;
    *time = (s8)b->turns;
    if (strength != NULL) {
        *strength = (s8)(sign * (b->strength > 0 ? b->strength : 1));
    }
}

#define COPIES 8
static BattleWeapon sCopies[COPIES];
static int sCopyNext;

/* BattleCheckDamage(attacker, target, part, weapon, flags), before: a Superguard seen; the boss's
 * side's attack on Mario's as its phase has it */
static int checkDamagePre(s32* result, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                          BattleWeapon** weapon, s32* flags) {
    BattleWeapon* w;
    const RaidBorrow* borrows[2] = {NULL, NULL};
    int k, pierce = 0;
    (void)result;
    (void)part;
    (void)flags;
    if (!sBattleOn || *attacker == NULL || *target == NULL || *weapon == NULL || (*attacker)->alliance != 1 ||
        (*target)->alliance != 0) {
        return 0;
    }
    /* the Superguard BattleCheckDamage is about to see (its own test) */
    if (sSt.phase == 3 && !sStaggered && !((*weapon)->special & SPECIAL_UNGUARDABLE) &&
        BtlUnit_GetACPossibility(*target) &&
        (BattleActionCommandGetDefenceResult() == 5 || portModeRunRaidGuard(BattleActionCommandGetDefenceResult()))) {
        if (isHero(*target) && (*target)->unitId >= 0 && (*target)->unitId < BATTLE_UNIT_MAX) {
            sGuarded |= 1ull << (*target)->unitId;
        }
        sGuardTotal++;
        if (furyStopped()) {
            const BattleUnit* boss = bossUnit();
            sStaggered = 1;
            printf("[raid] the fury stopped: staggered\n");
            fflush(stdout);
            if (boss != NULL) {
                advance(boss->currentHp, boss->maxHp);
            }
        }
    }
    if (sSt.phase == 2 || sSt.phase == 4) {
        borrows[0] = &kBorrows[sSt.borrowA];
    }
    if (sSt.phase == 4) {
        borrows[1] = &kBorrows[sSt.borrowB];
    }
    pierce = sSt.phase == 3;
    for (k = 0; k < 2; k++) {
        pierce |= borrows[k] != NULL && borrows[k]->pierce;
    }
    if ((*weapon)->hpDamageFn == NULL || (!pierce && borrows[0] == NULL)) {
        return 0;
    }
    w = &sCopies[sCopyNext];
    sCopyNext = (sCopyNext + 1) % COPIES;
    *w = **weapon;
    if (pierce) {
        w->special |= SPECIAL_PIERCES_DEFENSE;
    }
    for (k = 0; k < 2; k++) {
        if (borrows[k] != NULL && borrows[k]->status >= 0) {
            borrowStatus(w, borrows[k]);
        }
    }
    *weapon = w;
    return 0;
}

/* BattleCalculateDamage(...), after: the damage as the phase has it */
static void damagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                       BattleWeapon* weapon, u32* resultFlags, u32 flags) {
    int damage = *result;
    (void)part;
    (void)weapon;
    (void)resultFlags;
    (void)flags;
    if (!sBattleOn || damage <= 0 || attacker == NULL || target == NULL) {
        return;
    }
    if (attacker->alliance == 1 && target->alliance == 0) {
        int add = kPhaseAttack[sSt.phase - 1], k;
        const int borrow[2] = {sSt.phase == 2 || sSt.phase == 4 ? sSt.borrowA : -1, sSt.phase == 4 ? sSt.borrowB : -1};
        for (k = 0; k < 2; k++) {
            add += borrow[k] >= 0 ? kBorrows[borrow[k]].attack : 0;
        }
        *result = damage + add;
        return;
    }
    if (attacker->alliance == 0 && target->unitId == sBossUnit && sBossUnit >= 0) {
        const int duo = portDuoActive();
        const int line = lineOf(sSt.phase, target->maxHp);
        switch (sSt.phase) {
            case 2:
                if (!sShellBroken) {
                    if (duo) {
                        sShellBroken = 1; /* a duo move breaks the shell: its hit lands whole */
                        printf("[raid] the shell broken by a duo move\n");
                    } else {
                        damage = 1;
                        if (!portDuoEnabled() && ++sSt.shellHits >= SHELL_HITS) {
                            sShellBroken = 1; /* no duo moves in this fight: worn through */
                        }
                    }
                }
                break;
            case 3:
                damage = sStaggered ? (damage * 3 + 1) / 2 : (damage + 1) / 2;
                break;
            case 4:
                damage = duo ? damage * 2 : damage;
                break;
        }
        /* never past the phase's line: the next phase always comes */
        if (sSt.phase < PORT_RAID_PHASES && target->currentHp - damage < line) {
            damage = target->currentHp > line ? target->currentHp - line : 0;
        }
        *result = damage;
        advance(target->currentHp - damage, target->maxHp);
    }
}

/* --- each frame ------------------------------------------------------------------------ */

static void snapshot(void) {
    const BattleUnit* boss = bossUnit();
    BattleWork* wp = _battleWorkPointer;
    int n = 0;
    if (boss != NULL) {
        sSt.hp = boss->currentHp;
        sSt.maxHp = boss->maxHp;
        sSt.line = lineOf(sSt.phase, boss->maxHp);
    }
    sSt.gateOpen = gateOpen();
    if (sSt.phase == 3 && sBattleOn) {
        const unsigned long long heroes = heroesStanding(&n);
        unsigned long long g = sGuarded & heroes;
        sSt.guardsNeeded = n;
        sSt.guards = 0;
        while (g != 0) {
            sSt.guards += (int)(g & 1ull);
            g >>= 1;
        }
    }
    if (wp != NULL && sBattleOn) {
        sSt.turns = wp->turnCount;
    }
}

/* a guest, its raid on, in the field: the host's game back in the field outside the boss's room for a while
   (its raid won, lost or stopped) ends this game's too, back to the host's room. Its own end (the fight's
   script, evt_raid_battle_end) is the rule; this is for a guest whose fight never came or ended without
   that script's part (its raid on for ever held playing together off: the guest stayed in the boss's room,
   "players 1 guest 0", while the host was back in the adventure). 1 when it ended the raid */
#define HOST_AWAY_FRAMES 300
#define HOST_UNSEEN_FRAMES 3600
static int hostRaidOver(void) {
    const SsPeer* host;
    if (portStateSyncPartyRole() != 2 || gp == NULL || seqGetSeq() != SEQ_GAME || seqCheckSeq() || gp->inBattle) {
        return 0;
    }
    host = ssPeerOf(1);
    if (!host->valid) {
        return 0;
    }
    if (strncmp(host->map, kBosses[sSt.boss].map, sizeof(host->map)) == 0) {
        sHostSeen = 1;
        sHostAway = 0;
        return 0;
    }
    if (host->seq != SEQ_GAME || host->inBattle || host->map[0] == '\0') {
        return 0;
    }
    if (++sHostAway < (sHostSeen ? HOST_AWAY_FRAMES : HOST_UNSEEN_FRAMES)) {
        return 0;
    }
    {
        static char map[sizeof(((SsPeer*)0)->map) + 1]; /* (static: seqSetSeq keeps the pointer) */
        snprintf(map, sizeof(map), "%.*s", (int)sizeof(host->map), host->map);
        printf("[raid] the host's game is back in %s, out of the boss's room: this game's raid ends too\n", map);
        fflush(stdout);
        finish(2);
        if (strcmp(gp->currentMapName, map) != 0) {
            seqSetSeq(SEQ_MAP_CHANGE, map, NULL);
        }
    }
    return 1;
}

/* after marioStMain, each frame of the game */
static void frameHook(void) {
    portModeRunRaidFrame(sSt.active); /* (tests: $TTYD_MODERUN's lines for the raid, port/include/port/moderun.h) */
    if (__atomic_exchange_n(&sAskThread, 0, __ATOMIC_SEQ_CST)) {
        const char* why = NULL;
        if (!portRaidStart(&why)) {
            printf("[raid] not started: %s\n", why);
            fflush(stdout);
            __atomic_store_n(&sAskRefusal, why != NULL ? why : "Not now.", __ATOMIC_SEQ_CST);
            portCoopNotice(why != NULL ? why : "Not now."); /* (the page is closed by now: said over the game too) */
        }
    }
    /* $TTYD_RAID_TEST: a raid asked for 30 s in (tests; with TTYD_RAID_SOLO for one player) */
    static int sTest = -1;
    if (sTest < 0) {
        sTest = getenv("TTYD_RAID_TEST") != NULL;
    }
    if (sTest) {
        static int sTested;
        static unsigned sFrames;
        const char* why = NULL;
        if (!sTested && ++sFrames > 1800 && gp != NULL && seqGetSeq() == SEQ_GAME) {
            sTested = 1;
            if (!portRaidStart(&why)) {
                printf("[raid] test not started: %s\n", why);
            }
        }
    }
    if (sAsk) {
        if (gp == NULL || seqGetSeq() != SEQ_GAME || sSt.active || otherModeRuns()) {
            sAsk = 0;
        } else if (portPartyFieldReady()) {
            sAsk = 0;
            begin(-1, 0);
        }
    }
    {
        /* under sync, a guest: the host's raid is this game's too */
        int week, players;
        /* (a game still loading, changing rooms or in a battle takes it once it's free: portStateSyncRaidTake
           keeps it 20 s) */
        if (gp != NULL && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle &&
            portStateSyncRaidTake(&week, &players)) {
            if (sSt.active || otherModeRuns()) {
                printf("[raid] the host started one, but this game is busy: not joined\n");
                fflush(stdout);
            } else {
                printf("[raid] the host started one: joining\n");
                begin(week, players);
            }
        }
    }
    if (!sSt.active) {
        return;
    }
    {
        const int seq = seqGetSeq();
        if (seq == SEQ_GAME_OVER || seq == SEQ_TITLE) {
            finish(2);
            return;
        }
    }
    if (hostRaidOver()) {
        return;
    }
    if (gp != NULL && gp->inBattle && sBattleOn) {
        sSt.frames++;
    } else if (sSt.inFight && !(gp != NULL && gp->inBattle)) {
        sSt.inFight = 0;
    }
    snapshot();
}

void portRaidInit(void) {
    static int sDone;
    int i;
    if (sDone) {
        return;
    }
    sDone = 1;
    for (i = 0; i < BOSSES; i++) {
        portModsAddHostMap(kBosses[i].map, raid_map_init, kBosses[i].files);
    }
    /* Magnus 2.0's speeches are in his room's text (aji_14), not the ground's
     * ("No messages." before the fight) */
    portModsSetHostMapText("aji_r0", "aji_14");
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    portModHostAddHook(1, kSlot_BtlUnit_Entry, (void*)entryPost);
    portModHostAddHook(0, kSlot_BattleCheckDamage, (void*)checkDamagePre);
    portModHostAddHook(1, kSlot_BattleCalculateDamage, (void*)damagePost);
}
