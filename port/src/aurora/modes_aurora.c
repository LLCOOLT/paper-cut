/* Boss rush and endless Glitz Pit (port/include/port/modes.h): the game's own
 * battle setups, fought one after another. Each fight is on a map the mode
 * adds to the mods' new maps (port/mods/mods.cpp): named for the boss's area,
 * with one of its maps' files, so the map change loads the area's REL and its
 * battle table; the map's only script starts the battle with an invisible
 * stand-in NPC (as the area's own scripts do: evt_npc_set_battle_info, then
 * evt_npc_battle_start) and, after it, goes on to the next map. */
#include "port/modes.h"
#include "port/playmodes.h"
#include "port/postgame.h"
#include "port/raid.h"
#include "port/dojo.h"
#include "port/directors_cut.h"
#include "port/mods.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/settings.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One fight: the mode's map (area prefix: the area's REL), the map whose
 * files it reads, the area battle table's entry, and the name shown. */
typedef struct ModeFight {
    const char* map;
    const char* files;
    s32 battle;
    const char* name;
} ModeFight;

/* The bosses in story order (each area's btlsetup_<area>_tbl) */
static const ModeFight kBosses[] = {
    {"gor_m0", "gor_00", 2, "Lord Crump"},
    {"tik_m0", "tik_02", 3, "Blooper"},
    {"hei_m0", "hei_10", 19, "Gold Fuzzy"},
    {"gon_m0", "gon_11", 20, "Hooktail"},
    {"win_m0", "win_00", 4, "the Shadow Sirens"},
    {"mri_m0", "mri_20", 20, "Magnus von Grapple"},
    {"tou_m0", "tou_03", 23, "Macho Grubba"},
    {"jin_m0", "jin_04", 5, "Doopliss"},
    {"muj_m0", "muj_12", 4, "Cortez"},
    {"muj_m1", "muj_12", 5, "Lord Crump"},
    {"rsh_m0", "rsh_06_a", 1, "Smorg"},
    {"aji_m0", "aji_00", 5, "Magnus von Grapple 2.0"},
    {"las_m0", "las_26", 14, "Gloomtail"},
    {"las_m1", "las_28", 15, "Sir Grodus"},
    {"las_m2", "las_28", 13, "Bowser and Kammy"},
    {"las_m3", "las_29", 16, "the Shadow Queen"},
};
#define BOSS_COUNT ((s32)(sizeof(kBosses) / sizeof(kBosses[0])))

/* Boss refights: the story's progress (GSW 0) from which each boss counts as
 * beaten on this file, kBosses' order. The Shadow Queen: after the battle,
 * as a file saved after the credits is. */
static const s32 kBeatenAt[] = {
    STORY_STG0_19_BLOOPER_BATTLE,      /* Lord Crump (the prologue's, before Blooper) */
    STORY_STG0_19_BLOOPER_BATTLE + 1,  /* Blooper */
    STORY_STG1_06_01_GOLD_FUZZY + 1,   /* Gold Fuzzy */
    STORY_STG1_16_HOOKTAIL + 1,        /* Hooktail */
    STORY_STG2_08_SHADOW_SIRENS_LOSE,  /* the Shadow Sirens */
    STORY_STG2_22_LORD_CRUMP_APPEARS + 1, /* Magnus von Grapple */
    STORY_STG3_33_03_PEACH_GRODUS,     /* Macho Grubba (chapter 3 done) */
    STORY_STG4_15_DOOPLISS_BATTLE_2 + 1, /* Doopliss */
    STORY_STG5_33_CORTEZ_AND_THE_TREASURE + 1, /* Cortez */
    STORY_STG5_58_FAIR_WINDS,          /* Lord Crump (chapter 5's) */
    STORY_STG6_48_SMORG_MIASMA + 1,    /* Smorg */
    STORY_STG7_20_LORD_CRUMP_DEFEATED + 1, /* Magnus von Grapple 2.0 */
    STORY_STG8_04_GLOOMTAIL + 1,       /* Gloomtail */
    STORY_STG8_16_SHADOW_QUEEN,        /* Sir Grodus */
    STORY_STG8_16_SHADOW_QUEEN,        /* Bowser and Kammy */
    STORY_STG8_END_AFTER_THE_BATTLE,   /* the Shadow Queen */
};
_Static_assert(sizeof(kBeatenAt) / sizeof(kBeatenAt[0]) == sizeof(kBosses) / sizeof(kBosses[0]), "one per boss");
static char sReturnMap[16]; /* a refight's: the room it started in */
static s32 sRefightBoss;

/* The Glitz Pit: tou_03's files and REL (seq_mapchange.c); the teams are
 * btlsetup_tou_tbl's 1-22, the ranking's 20th to the top (Grubba is 23) */
static const ModeFight kPitMap = {"tou_m1", "tou_03", 0, NULL};
#define PIT_TEAMS 22

/* The Glitz Pit's match conditions (tou2.c's major-league set): rule id, its
 * two parameters, and the condition's text */
static const struct {
    u8 rule, param, param2;
    const char* msg;
} kConditions[] = {
    {18, 5, 5, "stg3_tou_cp_010"},     {1, 1, 1, "stg3_tou_cp_011"},     {3, 1, 1, "stg3_tou_cp_012"},
    {48, 1, 1, "stg3_tou_cp_013"},     {5, 1, 1, "stg3_tou_cp_014"},     {26, 1, 1, "stg3_tou_cp_015"},
    {32, 1, 1, "stg3_tou_cp_016"},     {59, 5, 5, "stg3_tou_cp_017"},    {6, 1, 1, "stg3_tou_cp_019"},
    {11, 20, 20, "stg3_tou_cp_020"},   {43, 3, 3, "stg3_tou_cp_021"},    {65, 3, 3, "stg3_tou_cp_022"},
    {64, 255, 255, "stg3_tou_cp_023"}, {63, 255, 255, "stg3_tou_cp_024"},
};
#define CONDITION_COUNT ((s32)(sizeof(kConditions) / sizeof(kConditions[0])))

static PortModeStatus sStatus;
static Uint64 sStartedAt;
static s32 sCondition = -1;    /* the Pit match's condition, kConditions index */
static s32 sTeam;              /* the Pit match's team */
static char sTeamName[32];
static PouchData sSavedPouch;  /* the boss rush's: Mario as he was */
static int sPouchSaved;
static int sSavedDifficulty[3]; /* the endless Pit's: the difficulty settings as they were */

static const char* const kDifficultyKeys[3] = {"difficulty", "difficulty_enemy_hp", "difficulty_enemy_attack"};

static void stopNow(void);

/* --- the fight's script --- */

USER_FUNC(evt_mode_battle);
USER_FUNC(evt_mode_battle_end);

/* LW(0): the battle table entry (-1: no mode runs: nothing) */
static EVT_BEGIN(mode_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_mode_battle, LW(0), LW(1), LW(2), LW(3))
    IF_INT_EQUAL(LW(0), -1)
        RETURN
    END_IF
    CALL(evt_npc_entry, STRING("mode_foe"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("mode_foe"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("mode_foe"), 0, -1000, 0)
    CALL(evt_npc_set_battle_info, STRING("mode_foe"), LW(0))
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_npc_set_battle_rule, STRING("mode_foe"), LW(2), LW(3), LW(3))
    END_IF
    CALL(evt_npc_battle_start, STRING("mode_foe"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    CALL(evt_npc_get_battle_rule_keep_result, LW(1))
    CALL(evt_mode_battle_end, LW(0), LW(1))
    RETURN
EVT_END()

static EVT_BEGIN(mode_map_init)
    SPAWN_EVENT(mode_fight_evt)
    RETURN
EVT_END()

static const ModeFight* currentFight(void) {
    if ((sStatus.mode == PORT_MODE_BOSS_RUSH || sStatus.mode == PORT_MODE_REFIGHT) && sStatus.step >= 1 &&
        sStatus.step <= BOSS_COUNT) {
        return &kBosses[sStatus.step - 1];
    }
    if (sStatus.mode == PORT_MODE_ENDLESS_PIT) {
        return &kPitMap;
    }
    return NULL;
}

/* out: the battle entry (-1 none), whether a rule applies, the rule and its parameter */
USER_FUNC(evt_mode_battle) {
    EvtWord* args = event->args;
    const ModeFight* f = currentFight();
    s32 battle = -1;
    (void)isFirstCall;
    if (f != NULL && strcmp(gp->currentMapName, f->map) == 0) {
        battle = sStatus.mode == PORT_MODE_ENDLESS_PIT ? sTeam : f->battle;
    }
    evtSetValue(event, args[0], battle);
    evtSetValue(event, args[1], sStatus.mode == PORT_MODE_ENDLESS_PIT && sCondition >= 0);
    evtSetValue(event, args[2], sCondition >= 0 ? kConditions[sCondition].rule : 0);
    evtSetValue(event, args[3], sCondition >= 0 ? kConditions[sCondition].param : 0);
    if (sStatus.mode == PORT_MODE_ENDLESS_PIT && sCondition >= 0) {
        /* the condition's text from the Pit map's own messages (stg3_tou_*), loaded now; looked up
         * before the map change it was the game's red "No messages" */
        sStatus.condition = msgSearch(kConditions[sCondition].msg);
    }
    printf("[modes] map %s: battle %d\n", gp->currentMapName, (int)battle);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* --- the runs --- */

static void restorePouch(void) {
    if (sPouchSaved) {
        *pouchGetPtr() = sSavedPouch;
        sPouchSaved = 0;
    }
}

static void restoreDifficulty(void) {
    int i;
    if (sStatus.mode != PORT_MODE_ENDLESS_PIT) {
        return;
    }
    for (i = 0; i < 3; i++) {
        int* v = portSettingsFind(kDifficultyKeys[i]);
        if (v != NULL) {
            *v = sSavedDifficulty[i];
        }
    }
}

static void finish(int how) {
    sStatus.ms = SDL_GetTicks() - sStartedAt;
    sStatus.finished = how;
    printf("[modes] %s %s at step %d, %llu ms\n",
           sStatus.mode == PORT_MODE_BOSS_RUSH ? "boss rush" : sStatus.mode == PORT_MODE_REFIGHT ? "refight" : "endless pit",
           how == 1 ? "won" : "lost", sStatus.step, sStatus.ms);
    fflush(stdout);
    restoreDifficulty();
    restorePouch();
}

/* The next Pit match: a team a little above the round (all of them past the
 * 22nd), a random condition, enemies stronger by 10% each round. */
static void pitNextMatch(void) {
    int* v;
    s32 lo = sStatus.step < PIT_TEAMS ? sStatus.step : PIT_TEAMS - 8;
    s32 hi = sStatus.step + 2 < PIT_TEAMS ? sStatus.step + 2 : PIT_TEAMS;
    sTeam = lo + irand(hi - lo + 1);
    sCondition = irand(CONDITION_COUNT);
    snprintf(sTeamName, sizeof(sTeamName), "Glitz Pit team %d of %d", (int)sTeam, PIT_TEAMS);
    sStatus.next = sTeamName;
    sStatus.condition = NULL; /* its text once on the Pit's map (evt_mode_battle) */
    if ((v = portSettingsFind("difficulty")) != NULL) {
        *v = 3; /* custom: the two percentages */
    }
    if ((v = portSettingsFind("difficulty_enemy_hp")) != NULL) {
        *v = 100 + 10 * (sStatus.step - 1) < 300 ? 100 + 10 * (sStatus.step - 1) : 300;
    }
    if ((v = portSettingsFind("difficulty_enemy_attack")) != NULL) {
        *v = 100 + 5 * (sStatus.step - 1) < 300 ? 100 + 5 * (sStatus.step - 1) : 300;
    }
}

static void goTo(const ModeFight* f) {
    seqSetSeq(SEQ_MAP_CHANGE, f->map, NULL);
}

/* in: the battle's result (1 won, 3 lost, 4 fled), whether the rule was kept (6) */
USER_FUNC(evt_mode_battle_end) {
    EvtWord* args = event->args;
    s32 result = evtGetValue(event, args[0]);
    s32 kept = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (sStatus.mode == PORT_MODE_NONE || sStatus.finished) {
        return EVT_RETURN_DONE;
    }
    if (sStatus.mode == PORT_MODE_REFIGHT) {
        /* won or fled: back where it started (a loss is the game's Game Over, as in the story) */
        if (result == 1 || result == 4) {
            finish(result == 1 ? 1 : 2);
            seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
        } else {
            finish(2);
        }
        return EVT_RETURN_DONE;
    }
    if (result == 4) {
        goTo(currentFight()); /* fled: the same fight again */
        return EVT_RETURN_DONE;
    }
    if (result != 1) {
        finish(2);
        return EVT_RETURN_DONE;
    }
    if (sStatus.mode == PORT_MODE_BOSS_RUSH) {
        if (sStatus.step == BOSS_COUNT) {
            finish(1);
            seqSetSeq(SEQ_MAP_CHANGE, "gor_01", NULL); /* back to Rogueport */
            return EVT_RETURN_DONE;
        }
        sStatus.step++;
        sStatus.next = kBosses[sStatus.step - 1].name;
        goTo(&kBosses[sStatus.step - 1]);
    } else {
        if (sCondition >= 0 && kept != 6) {
            finish(2); /* the condition was broken: the run ends here */
            seqSetSeq(SEQ_MAP_CHANGE, "tou_02", NULL);
            return EVT_RETURN_DONE;
        }
        pouchAddCoin(10 + sStatus.step);
        sStatus.step++;
        pitNextMatch();
        goTo(&kPitMap);
    }
    return EVT_RETURN_DONE;
}

/* The loadouts: level, HP, FP, BP, boots and hammer, Star Powers, items */
static void giveLoadout(int loadout) {
    static const s16 kItems[2][8] = {
        {ITEM_MUSHROOM, ITEM_MUSHROOM, ITEM_SUPER_SHROOM, ITEM_SUPER_SHROOM, ITEM_HONEY_SYRUP, ITEM_HONEY_SYRUP,
         ITEM_FIRE_FLOWER, ITEM_THUNDER_RAGE},
        {ITEM_ULTRA_SHROOM, ITEM_ULTRA_SHROOM, ITEM_ULTRA_SHROOM, ITEM_LIFE_SHROOM, ITEM_JAMMIN_JELLY,
         ITEM_JAMMIN_JELLY, ITEM_MAPLE_SYRUP, ITEM_THUNDER_RAGE},
    };
    PouchData* p = pouchGetPtr();
    const int late = loadout == PORT_LOADOUT_LATE;
    s32 i;
    sSavedPouch = *p;
    sPouchSaved = 1;
    for (i = 1; i <= 7; i++) {
        partyJoin((PartyId)i);
        p->partyData[i].attackLevel = (s16)(late ? 2 : 1);
        p->partyData[i].HPLevel = (s16)(late ? 2 : 1);
        p->partyData[i].techLevel = (s16)(late ? 2 : 1);
    }
    p->level = (s16)(late ? 30 : 15);
    p->baseMaxHP = p->maximumHP = p->currentHP = (s16)(late ? 60 : 30);
    p->baseMaxFP = p->maximumFP = p->currentFP = (s16)(late ? 50 : 25);
    p->maximumBP = (s16)(late ? 51 : 21);
    p->availableBP = p->maximumBP;
    p->jumpLevel = (s8)(late ? 2 : 1);
    p->hammerLevel = (s8)(late ? 2 : 1);
    p->starPowersObtained = (u16)(late ? 0xFF : 0x0F);
    p->maximumSP = p->currentSP = (s16)(late ? 800 : 400);
    memset(p->heldItems, 0, sizeof(p->heldItems));
    for (i = 0; i < 8; i++) {
        pouchGetItem((ItemType)kItems[late][i]);
    }
}

int portModePitUnlocked(void) {
    return swStoryPeek() >= STORY_STG3_34_RETURN_TO_ROGUEPORT; /* (menus read it too: no side effects) */
}

/* Starts and stops asked for from the settings page (its own thread): done
 * on the game's frame (frameHook) */
static int sStartMode, sStartLoadout, sStopAsked; /* __atomic */

static void startNow(int mode, int loadout) {
    int i;
    stopNow();
    memset(&sStatus, 0, sizeof(sStatus));
    sStatus.mode = mode;
    sStatus.step = 1;
    sStatus.loadout = loadout;
    sStartedAt = SDL_GetTicks();
    if (mode == PORT_MODE_REFIGHT) {
        snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
        sStatus.step = __atomic_load_n(&sRefightBoss, __ATOMIC_SEQ_CST) + 1;
        sStatus.steps = 1;
        sStatus.next = kBosses[sStatus.step - 1].name;
        sCondition = -1;
        if (loadout != PORT_LOADOUT_OWN) {
            giveLoadout(loadout);
        }
        goTo(&kBosses[sStatus.step - 1]);
    } else if (mode == PORT_MODE_BOSS_RUSH) {
        /* $TTYD_MODE_STEP=N: from the Nth boss (tests) */
        const char* step = getenv("TTYD_MODE_STEP");
        if (step != NULL && atoi(step) >= 1 && atoi(step) <= BOSS_COUNT) {
            sStatus.step = atoi(step);
        }
        sStatus.steps = BOSS_COUNT;
        sStatus.next = kBosses[sStatus.step - 1].name;
        sCondition = -1;
        if (loadout != PORT_LOADOUT_OWN) {
            giveLoadout(loadout);
        }
        goTo(&kBosses[sStatus.step - 1]);
    } else {
        for (i = 0; i < 3; i++) {
            const int* v = portSettingsFind(kDifficultyKeys[i]);
            sSavedDifficulty[i] = v != NULL ? *v : 0;
        }
        pitNextMatch();
        goTo(&kPitMap);
    }
    printf("[modes] %s started\n",
           mode == PORT_MODE_BOSS_RUSH ? "boss rush" : mode == PORT_MODE_REFIGHT ? "refight" : "endless pit");
    fflush(stdout);
}

static void stopNow(void) {
    if (sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
        restoreDifficulty();
        restorePouch();
    }
    memset(&sStatus, 0, sizeof(sStatus));
    sCondition = -1;
}

/* After marioStMain, each frame of the game */
static void frameHook(void) {
    if (__atomic_exchange_n(&sStopAsked, 0, __ATOMIC_SEQ_CST)) {
        stopNow();
    }
    /* a start waits for the field (not a map change or a battle) */
    if (seqGetSeq() == SEQ_GAME && !gp->inBattle) {
        const int mode = __atomic_exchange_n(&sStartMode, 0, __ATOMIC_SEQ_CST);
        if (mode != PORT_MODE_NONE) {
            startNow(mode, __atomic_load_n(&sStartLoadout, __ATOMIC_SEQ_CST));
        }
    }
    if (sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
        const int seq = seqGetSeq();
        if (seq == SEQ_GAME_OVER || seq == SEQ_TITLE) {
            finish(2); /* a Game Over, or back to the title */
        }
    }
}

int portModeStart(int mode, int loadout, const char** why) {
    if (seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (mode == PORT_MODE_ENDLESS_PIT && !portModePitUnlocked()) {
        *why = "The endless Glitz Pit opens once the story's Glitz Pit is done.";
        return 0;
    }
    __atomic_store_n(&sStartLoadout, loadout, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartMode, mode, __ATOMIC_SEQ_CST);
    return 1;
}

int portModeBossCount(void) {
    return BOSS_COUNT;
}

const char* portModeBossName(int boss) {
    return boss >= 0 && boss < BOSS_COUNT ? kBosses[boss].name : "";
}

int portModeBossBeaten(int boss) {
    return boss >= 0 && boss < BOSS_COUNT && gp != NULL && swStoryPeek() >= kBeatenAt[boss];
}

int portModeRefight(int boss, int loadout, const char** why) {
    if (boss < 0 || boss >= BOSS_COUNT) {
        *why = "No such boss.";
        return 0;
    }
    if (seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (!portModeBossBeaten(boss)) {
        *why = "Beat this boss in the story first.";
        return 0;
    }
    __atomic_store_n(&sRefightBoss, boss, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartLoadout, loadout, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartMode, PORT_MODE_REFIGHT, __ATOMIC_SEQ_CST);
    return 1;
}

/* A leaderboard run (board_run_aurora.c): started on the first frame in the
 * field, as a start from the Modes tab is, without its checks (at start-up
 * there's no field yet) */
void portModeQueue(int mode, int loadout) {
    __atomic_store_n(&sStartLoadout, loadout, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartMode, mode, __ATOMIC_SEQ_CST);
}

void portModeStop(void) {
    __atomic_store_n(&sStopAsked, 1, __ATOMIC_SEQ_CST);
}

const PortModeStatus* portModeStatus(void) {
    if (sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
        sStatus.ms = SDL_GetTicks() - sStartedAt;
    }
    return &sStatus;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portModesRegisterMaps(void) {
    s32 i;
    for (i = 0; i < BOSS_COUNT; i++) {
        portModsAddHostMap(kBosses[i].map, mode_map_init, kBosses[i].files);
    }
    /* the bosses whose speeches are in another room's text than the ground's
     * (the boss's own room, as the story's battle loads it) */
    portModsSetHostMapText("mri_m0", "mri_01"); /* Magnus von Grapple */
    portModsSetHostMapText("muj_m1", "muj_00"); /* Lord Crump, chapter 5 */
    portModsSetHostMapText("aji_m0", "aji_14"); /* Magnus von Grapple 2.0 */
    portModsAddHostMap(kPitMap.map, mode_map_init, kPitMap.files);
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portPlayModesInit(); /* chapter select and the other play modes (playmodes.h) */
    portPostgameInit();  /* the trophy room and the other post-game modes (postgame.h) */
    portDirectorsCutInit(); /* Director's Cut's hooks (directors_cut.h) */
    portRaidInit();         /* the raid boss's maps and hooks (raid.h) */
    portDojoRegisterMaps(); /* the action-command dojo's map and hooks (dojo.h) */
}
