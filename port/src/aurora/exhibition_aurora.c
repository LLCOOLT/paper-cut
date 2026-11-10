/* Glitz Pit exhibition matches, the returning champion and Nighttail
 * (port/include/port/postgame_extra.h).
 *
 * Each is a fight on a map of its own (as the boss rush's, modes_aurora.c):
 * named for its area, with one of the area's maps' files, so the map change
 * loads the area's REL and its battle table; the map's script starts the
 * battle from a hidden stand-in NPC, and after it Mario goes back to the
 * room he started from, won, lost or fled.
 *
 *   - Exhibition matches: any Glitz Pit team ranked below Mario (beaten in
 *     the story; all of them once he's the champion), Wings of Night and the
 *     Destructors too, the only way to meet those two again. The game's own
 *     setups (btlsetup_tou_tbl) with no match conditions.
 *   - The returning champion: Paper Cut's own super-boss, made of the disc's
 *     models with the Enemy maker's tools (port/include/port/enemy_variant.h):
 *     Rawk Hawk's body gilded, bigger and stronger, with a burning kick, and
 *     two Red Chomps of the Chomp Country's in his colours. After the credits.
 *   - Nighttail: a single-player super-boss from the game's own models:
 *     Gloomtail's, in night's colours, with Bonetail's sleepy breath, on
 *     Gloomtail's stage in the Palace. After the credits.
 * None of them runs online or in a replay. */
#include "port/statesync.h"
#include "port/postgame_extra.h"
#include "port/enemy_variant.h"
#include "port/mods.h"
#include "port/online.h"
#include "port/replay.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mariost.h"

#include <stdio.h>

void npcSetupBattleInfo(NpcEntry* npc, NpcSetupBattleInfo* setup); /* npcdrv.c */
#include <string.h>

enum { FIGHT_NONE, FIGHT_EXHIBITION, FIGHT_CHAMPION, FIGHT_NIGHTTAIL };

/* The Glitz Pit's roster (tou2.c fighterDt, the player's own left out): the
 * English name, its fighter id (GSWF_TOU_FIGHTER_BELOW_BASE + id: ranked
 * below Mario) and its setup in btlsetup_tou_tbl */
static const struct {
    const char* name;
    int fighter;
    int setup;
} kTeams[] = {
    {"Rawk Hawk", 0, 22},
    {"The Koopinator", 1, 19},
    {"Chomp Country", 2, 18},
    {"Hamma, Bamma, and Flare", 3, 17},
    {"Craw-Daddy", 4, 16},
    {"The Magikoopa Masters", 5, 15},
    {"The Fuzz", 6, 14},
    {"The Shellshockers", 7, 13},
    {"The Poker Faces", 8, 12},
    {"The Tiny Spinies", 9, 11},
    {"The Armored Harriers", 10, 10},
    {"The Hand-It-Overs", 11, 9},
    {"The Bob-omb Squad", 12, 8},
    {"The Punk Rocks", 13, 7},
    {"The Mind-Bogglers", 14, 6},
    {"Spike Storm", 15, 5},
    {"The Dead Bones", 16, 4},
    {"The Pokey Triplets", 17, 3},
    {"The KP Koopas", 18, 2},
    {"The Goomba Bros.", 19, 1},
    {"Wings of Night", 21, 20},
    {"The Destructors", 22, 21},
};
#define TEAMS ((int)(sizeof(kTeams) / sizeof(kTeams[0])))
_Static_assert(TEAMS <= PORT_EXHIBITION_TEAMS, "the header's count");

#define CHAMPION_SETUP 22 /* Rawk Hawk's: his stage, his music */
#define CHOMP_SETUP 18    /* Chomp Country's Red Chomps */
#define GLOOMTAIL_SETUP 14 /* btlsetup_las_tbl's Gloomtail (modes_aurora.c's boss rush) */

static const char* const kMap[] = {NULL, "tou_pg", "tou_pg", "las_pg"};
static const char* const kFiles[] = {NULL, "tou_03", "tou_03", "las_26"};
static const char* const kArea[] = {NULL, "tou", "tou", "las"};

static int sKind;          /* the fight going (FIGHT_*) */
static int sTeam;          /* an exhibition's team */
static char sReturn[16];   /* the room it started from */
static int sAskKind, sAskTeam; /* from the settings page's thread (__atomic) */

static int finished(void) {
    return gp != NULL && swByteGet(0) >= STORY_STG8_END_AFTER_THE_BATTLE;
}

static int allowed(const char** why) {
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (portStateSyncLinked() || portReplayActive()) {
        *why = "Not online or in a replay.";
        return 0;
    }
    if (sKind != FIGHT_NONE) {
        *why = "A fight is already on.";
        return 0;
    }
    return 1;
}

int portExhibitionCount(void) {
    return TEAMS;
}

const char* portExhibitionName(int team) {
    return team >= 0 && team < TEAMS ? kTeams[team].name : "";
}

int portExhibitionOpen(int team) {
    if (gp == NULL || team < 0 || team >= TEAMS) {
        return 0;
    }
    if (swByteGet(0) >= STORY_STG3_34_RETURN_TO_ROGUEPORT) {
        return 1; /* the champion: everyone is below him */
    }
    return kTeams[team].fighter != 0 && swGet(GSWF_TOU_FIGHTER_BELOW_BASE + kTeams[team].fighter);
}

int portExhibitionStart(int team, const char** why) {
    if (!allowed(why)) {
        return 0;
    }
    if (!portExhibitionOpen(team)) {
        *why = "Only a team ranked below Mario: beat it in the Glitz Pit first.";
        return 0;
    }
    __atomic_store_n(&sAskTeam, team, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sAskKind, FIGHT_EXHIBITION, __ATOMIC_SEQ_CST);
    return 1;
}

int portChampionOpen(void) {
    return finished();
}

int portChampionStart(const char** why) {
    if (!allowed(why)) {
        return 0;
    }
    if (!portChampionOpen()) {
        *why = "The champion comes back once the story is over.";
        return 0;
    }
    __atomic_store_n(&sAskKind, FIGHT_CHAMPION, __ATOMIC_SEQ_CST);
    return 1;
}

int portNighttailOpen(void) {
    return finished();
}

int portNighttailStart(const char** why) {
    if (!allowed(why)) {
        return 0;
    }
    if (!portNighttailOpen()) {
        *why = "Nighttail stirs once the story is over.";
        return 0;
    }
    __atomic_store_n(&sAskKind, FIGHT_NIGHTTAIL, __ATOMIC_SEQ_CST);
    return 1;
}

const char* portExhibitionRunning(void) {
    switch (sKind) {
    case FIGHT_EXHIBITION: return kTeams[sTeam].name;
    case FIGHT_CHAMPION: return "The Golden Talon";
    case FIGHT_NIGHTTAIL: return "Nighttail";
    default: return NULL;
    }
}

static int pgMap(void) {
    return gp != NULL && (strcmp(gp->currentMapName, "tou_pg") == 0 || strcmp(gp->currentMapName, "las_pg") == 0);
}

int portExhibitionRoom(void) {
    return sKind != FIGHT_NONE || pgMap();
}

/* --- the battles ------------------------------------------------------------ */

static NpcSetupBattleInfo sInfo;
static NpcBattleSelect sSelect[2];
static BattleGroupSetup sGroup;
static BattleUnitSetup sUnits[3];
static PortEnemyVariant sVariants[2];

static void variantTexts(PortEnemyVariant* v, const char* name, const char* tattle) {
    snprintf(v->name, sizeof(v->name), "%s", portPgTr(name));
    snprintf(v->tattle, sizeof(v->tattle), "%s", portGameLayout(portPgTr(tattle), 30, 3));
}

/* the champion's team: him at his place, a Red Chomp either side */
static int championGroup(const NpcSetupBattleInfo* tbl) {
    const BattleGroupSetup* hawk = tbl[CHAMPION_SETUP].select != NULL ? tbl[CHAMPION_SETUP].select[0].groupSetup : NULL;
    const BattleGroupSetup* chomps = tbl[CHOMP_SETUP].select != NULL ? tbl[CHOMP_SETUP].select[0].groupSetup : NULL;
    if (hawk == NULL || chomps == NULL || hawk->numEnemies < 1 || chomps->numEnemies < 1) {
        return 0;
    }
    sGroup = *hawk;
    sUnits[0] = hawk->enemyData[0];
    sUnits[1] = chomps->enemyData[0];
    sUnits[2] = chomps->enemyData[0];
    sUnits[1].position.x = 150.0f;
    sUnits[1].position.z = 30.0f;
    sUnits[2].position.x = 40.0f;
    sUnits[2].position.z = 30.0f;
    sGroup.numEnemies = 3;
    sGroup.enemyData = sUnits;
    portEnemyVariantInit(&sVariants[0], sUnits[0].unitKindParams->unitType);
    sVariants[0].hpPercent = 300;
    sVariants[0].attack = 3;
    sVariants[0].defense = 1;
    sVariants[0].color = 0xF0C850FFu;
    sVariants[0].sizePercent = 115;
    sVariants[0].move = PORT_VARIANT_MOVE_BURN;
    sVariants[0].coins = 50;
    variantTexts(&sVariants[0], "The Golden Talon",
                 "That's the Golden Talon, the champion who went away before Rawk Hawk ever wore the belt.<p>He's "
                 "back, and he wants it all back too.<p>Max HP is three times Rawk Hawk's, and his kicks burn!<p>"
                 "Take out his Chomps first, or they'll wear us down while he shows off.<k>");
    portEnemyVariantInit(&sVariants[1], sUnits[1].unitKindParams->unitType);
    sVariants[1].hpPercent = 150;
    sVariants[1].attack = 1;
    sVariants[1].color = 0xB08A30FFu;
    sVariants[1].sizePercent = 90;
    variantTexts(&sVariants[1], "Talon's Chomp",
                 "One of the Golden Talon's Chomps. He had them painted to match his feathers.<p>They're tougher than "
                 "the Chomp Country's, and they bite just as hard.<k>");
    portEnemyVariantArm(sVariants, 2);
    return 1;
}

static void nighttail(const NpcSetupBattleInfo* tbl) {
    const BattleGroupSetup* g = tbl[GLOOMTAIL_SETUP].select[0].groupSetup;
    portEnemyVariantInit(&sVariants[0], g->enemyData[0].unitKindParams->unitType);
    sVariants[0].hpPercent = 250;
    sVariants[0].attack = 2;
    sVariants[0].defense = 1;
    sVariants[0].color = 0x6A50A8FFu;
    sVariants[0].sizePercent = 105;
    sVariants[0].move = PORT_VARIANT_MOVE_SLEEP;
    sVariants[0].coins = 80;
    variantTexts(&sVariants[0], "Nighttail",
                 "Nighttail... the eldest of the three, so they say. Hooktail and Gloomtail never talked about "
                 "her.<p>She breathes the dark itself, and it puts you to sleep like Bonetail's did.<p>Her HP is way "
                 "past Gloomtail's. Keep your HP up, and don't nap in front of her!<k>");
    portEnemyVariantArm(sVariants, 1);
}

USER_FUNC(evt_pg_fight);
USER_FUNC(evt_pg_fight_end);

static EVT_BEGIN(pg_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_npc_entry, STRING("pg_foe"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("pg_foe"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("pg_foe"), 0, -1000, 0)
    CALL(evt_pg_fight, STRING("pg_foe"), LW(0))
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    CALL(evt_npc_battle_start, STRING("pg_foe"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_pg_fight_end, LW(0))
    RETURN
EVT_END()

static EVT_BEGIN(pg_fight_map_init)
    SPAWN_EVENT(pg_fight_evt)
    RETURN
EVT_END()

static void goBack(void) {
    seqSetSeq(SEQ_MAP_CHANGE, sReturn[0] != '\0' ? sReturn : "gor_01", NULL);
}

/* in: the stand-in's name; out: 1 when the battle is set */
USER_FUNC(evt_pg_fight) {
    EvtWord* args = event->args;
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, args[0]));
    AreaData* ad;
    const NpcSetupBattleInfo* tbl;
    int setup;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    if (sKind == FIGHT_NONE && pgMap()) {
        /* (here with no fight: a save or state from these rooms loaded, sKind not in it. The room has no way
           out, so back to the town rather than left there for ever) */
        printf("[postgame] in %s with no fight: back\n", gp->currentMapName);
        fflush(stdout);
        goBack();
        return EVT_RETURN_DONE;
    }
    if (sKind == FIGHT_NONE || npc == NULL || strcmp(gp->currentMapName, kMap[sKind]) != 0) {
        return EVT_RETURN_DONE;
    }
    ad = areaDataPtr(kArea[sKind]);
    tbl = ad != NULL ? (const NpcSetupBattleInfo*)ad->btlSetupTbl : NULL;
    setup = sKind == FIGHT_EXHIBITION ? kTeams[sTeam].setup : sKind == FIGHT_CHAMPION ? CHAMPION_SETUP : GLOOMTAIL_SETUP;
    if (tbl == NULL || tbl[setup].select == NULL) {
        printf("[postgame] no battle %d in %s's table\n", setup, kArea[sKind]);
        fflush(stdout);
        sKind = FIGHT_NONE;
        goBack();
        return EVT_RETURN_DONE;
    }
    sInfo = tbl[setup];
    sInfo.fixedItem = 0;
    sInfo.swFlag = -1;
    sInfo.altSelect = NULL;
    sInfo.battleFlags |= NPC_BATTLE_LOSS_OK;
    if (sKind == FIGHT_CHAMPION) {
        if (!championGroup(tbl)) {
            sKind = FIGHT_NONE;
            goBack();
            return EVT_RETURN_DONE;
        }
        memset(sSelect, 0, sizeof(sSelect));
        sSelect[0].weight = 1;
        sSelect[0].groupSetup = &sGroup;
        sSelect[0].stageData = tbl[CHAMPION_SETUP].select[0].stageData;
        sInfo.select = sSelect;
    } else if (sKind == FIGHT_NIGHTTAIL) {
        nighttail(tbl);
        sInfo.bgmName = "BGM_BOSS_STG8_3RDHEAD1";
    }
    npcSetupBattleInfo(npc, &sInfo);
    printf("[postgame] %s: battle %d\n", portExhibitionRunning(), setup);
    fflush(stdout);
    evtSetValue(event, args[1], 1);
    return EVT_RETURN_DONE;
}

/* in: the result (1 won, 3 lost, 4 fled) */
USER_FUNC(evt_pg_fight_end) {
    const int result = evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    if (result == 1 && sKind == FIGHT_CHAMPION) {
        swSet(PORT_GSWF_PG_CHAMPION_BEATEN);
    } else if (result == 1 && sKind == FIGHT_NIGHTTAIL) {
        swSet(PORT_GSWF_PG_NIGHTTAIL_BEATEN);
    }
    printf("[postgame] %s: %s\n", portExhibitionRunning() != NULL ? portExhibitionRunning() : "?",
           result == 1 ? "won" : result == 4 ? "fled" : "lost");
    fflush(stdout);
    sKind = FIGHT_NONE;
    goBack();
    return EVT_RETURN_DONE;
}

void portExhibitionFrame(void) {
    int kind;
    if (gp == NULL) {
        return;
    }
    if (sKind != FIGHT_NONE && (seqGetSeq() == SEQ_GAME_OVER || seqGetSeq() == SEQ_TITLE)) {
        sKind = FIGHT_NONE;
    }
    if (seqGetSeq() != SEQ_GAME || gp->inBattle || seqCheckSeq() || marioStGetSystemLevel() != 0) {
        return;
    }
    kind = __atomic_exchange_n(&sAskKind, 0, __ATOMIC_SEQ_CST);
    if (kind == FIGHT_NONE || sKind != FIGHT_NONE) {
        return;
    }
    sKind = kind;
    sTeam = __atomic_load_n(&sAskTeam, __ATOMIC_SEQ_CST);
    snprintf(sReturn, sizeof(sReturn), "%s", gp->currentMapName);
    seqSetSeq(SEQ_MAP_CHANGE, kMap[kind], NULL);
}

void portExhibitionRegister(void) {
    portModsAddHostMap("tou_pg", pg_fight_map_init, "tou_03");
    portModsAddHostMap("las_pg", pg_fight_map_init, "las_26");
}
