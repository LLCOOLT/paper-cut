/* Boss rush and endless Glitz Pit (port/include/port/modes.h): the game's own
 * battle setups, fought one after another. Each fight is on a map the mode
 * adds to the mods' new maps (port/mods/mods.cpp): named for the boss's area,
 * with one of its maps' files, so the map change loads the area's REL and its
 * battle table; the map's only script starts the battle with an invisible
 * stand-in NPC (as the area's own scripts do: evt_npc_set_battle_info, then
 * evt_npc_battle_start) and, after it, goes on to the next map. */
#include "port/modes.h"
#include "port/mode_leave.h"
#include "port/moderun.h"
#include "port/pm64.h"
#include "port/pm64_foes.h"
#include "port/playmodes.h"
#include "port/postgame.h"
#include "port/raid.h"
#include "port/dojo.h"
#include "port/directors_cut.h"
#include "port/map_edits.h"
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
#include "evt/evt_pouch.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "party.h"
#include "mario/mario_pouch.h"
#include "data/item_data.h"
#include "mario/mariost.h"
#include "system.h"
#include "port/title_menu.h"
#include "port/together.h"
#include "port/battlex.h"
#include "port/fast_travel.h"
#include "port/versus.h"
#include "pmario_sound.h"

#include <SDL3/SDL_timer.h>
#include <stdio.h>

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture */
#include <stdlib.h>
#include <string.h>

/* One fight: the mode's map (area prefix: the area's REL), the map whose
 * files it reads, the area battle table's entry, and the name shown. */
typedef struct ModeFight {
    const char* map;
    const char* files;
    s32 battle;
    const char* name;
    s32 pm64; /* a Paper Mario 64 boss (port/include/port/pm64_foes.h): its index + 1; 0: the area's battle */
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

/* Paper Mario 64's bosses, after the game's own with "pm64_bosses" on (pm64_bosses_aurora.c): each on the
 * stage of a boss rush map (its battle entry gives the stage); refights once the story is over. The first ten
 * are the rush's; the eleventh, the Koopa Bros.' rematch (port/include/port/pm64.h), is the boss list's, the
 * board's and the story's only. */
static const ModeFight kPm64Fights[] = {
    {"hei_m0", "hei_10", 19, NULL, 1},   {"gon_m0", "gon_11", 20, NULL, 2},   {"muj_m0", "muj_12", 4, NULL, 3},
    {"jin_m0", "jin_04", 5, NULL, 4},    {"tou_m0", "tou_03", 23, NULL, 5},   {"las_m0", "las_26", 14, NULL, 6},
    {"rsh_m0", "rsh_06_a", 1, NULL, 7},  {"aji_m0", "aji_00", 5, NULL, 8},    {"las_m2", "las_28", 13, NULL, 9},
    {"las_m2", "las_28", 13, NULL, 10},
    {"gon_m0", "gon_11", 20, NULL, 2}, /* the Koopa Bros.' rematch: their stage and boss again */
};
#define PM64_FIGHTS ((s32)(sizeof(kPm64Fights) / sizeof(kPm64Fights[0])))
_Static_assert(PM64_FIGHTS == PM64_BOSS_FIGHTS, "the beaten marks: one per fight (port/include/port/pm64.h)");
#define PM64_RUSH_FIGHTS PM64_BOSS_RUSH_FIGHTS /* (the rush's and the refights': not the rematch) */
_Static_assert(PM64_BOSS_FIGHTS - 1 == PM64_FIGHT_KOOPA_BROS_REMATCH, "the rematch is the last fight");

const char* portPm64FightEnglish(int fight) {
    if (fight == PM64_FIGHT_KOOPA_BROS_REMATCH) {
        return "Koopa Bros. (rematch)";
    }
    return fight >= 0 && fight < PM64_FIGHTS ? portPm64BossEnglish(kPm64Fights[fight].pm64 - 1) : "";
}
USER_FUNC(evt_pm64boss_battle); /* pm64_bosses_aurora.c */

/* the lists' bosses (the refights', the rush's by default): the story's, Paper Mario 64's after with
 * "pm64_bosses" on */
static s32 bossCount(void) {
    return BOSS_COUNT + (portPm64BossesOn() ? PM64_RUSH_FIGHTS : 0);
}

/* the running boss rush's: Paper Mario 64's taken in as it started (the setting on, or "Start at" one of them) */
static int sRushPm64;

static s32 rushCount(void) {
    return BOSS_COUNT + (sRushPm64 ? PM64_RUSH_FIGHTS : 0);
}

/* "Start at" (portModeRushStart): 0..BOSS_COUNT-1 the story's bosses, BOSS_COUNT Paper Mario 64's only */
static int sRushFrom; /* __atomic (the settings page's thread) */

int portModeRushStartCount(void) {
    return BOSS_COUNT + (portPm64BossesReady() ? 1 : 0);
}

const char* portModeRushStartName(int choice) {
    if (choice == BOSS_COUNT) {
        return "Paper Mario 64 bosses only";
    }
    return choice >= 0 && choice < BOSS_COUNT ? kBosses[choice].name : "";
}

int portModeRushStart(void) {
    const int c = __atomic_load_n(&sRushFrom, __ATOMIC_SEQ_CST);
    return c >= 0 && c < portModeRushStartCount() ? c : 0;
}

void portModeRushStartSet(int choice) {
    __atomic_store_n(&sRushFrom, choice >= 0 && choice < portModeRushStartCount() ? choice : 0, __ATOMIC_SEQ_CST);
}

static const ModeFight* bossAt(s32 i) {
    static ModeFight f;
    if (i < BOSS_COUNT) {
        return &kBosses[i];
    }
    f = kPm64Fights[i - BOSS_COUNT];
    f.name = portPm64FightEnglish(i - BOSS_COUNT);
    return &f;
}

static char sReturnMap[16]; /* the room a mode started in (a refight's end and a stop come back to it) */
static Vec sReturnPos;      /*   and where Mario stood in it */
static int sReturnPosAsked; /*   put back once he's free there again */
static s32 sRefightBoss;
/* __atomic: the Paper Mario 64 refight asked for is its boss's first fight (portModePm64FightAs: Jr. Troopa's prologue
   fight, PM64_BOSS_FLAG_EARLY); and the one running is. (A story meeting in Rogueport is no refight: an ordinary
   battle on the boss's field NPC, pm64_story_aurora.c.) */
static int sEarlyAsk;
static int sEarlyRun;

static int isPm64Fight(s32 i) {
    return i >= BOSS_COUNT && i < BOSS_COUNT + PM64_FIGHTS;
}

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
static int sStopPending; /* a stop asked in a battle (or on the way to one): done as the battle ends */
static int sInFight;     /* the fight's script chose a battle and it hasn't ended yet */

static void stopAndReturn(void);

/* port/gameplus.h (not included: its PORT_LOADOUT_COUNT is a macro of its own): the endless Pit's scaling, in
   memory only. It once went into the player's own difficulty settings, which any settings save (a volume
   change, a handheld's suspend) wrote out: a quit mid-run kept enemies at 240% HP in the story. */
void portDifficultyOverride(int hpPercent, int attackPercent);

static void stopNow(void);

/* --- the fight's script --- */

USER_FUNC(evt_mode_battle);
USER_FUNC(evt_mode_battle_end);
USER_FUNC(evt_mode_phase2);

/* the fight couldn't be set (a PM64 boss with no stage): the mode stops, and Mario goes back where it started.
 * (It once only asked portModeStop(), which waits for !sInFight; evt_mode_battle had set it and nothing cleared
 * it on this path, so the mode ran on with Mario on its exitless map: a softlock.) */
USER_FUNC(evt_mode_refused) {
    (void)event;
    (void)isFirstCall;
    printf("[modes] fight refused: the mode stops\n");
    fflush(stdout);
    portCoopNotice("The fight couldn't be set up here: the mode stopped.");
    sInFight = 0;
    stopAndReturn(); /* (in the field, from the host map's script: the map change is safe here) */
    return EVT_RETURN_DONE;
}

/* LW(0): the battle table entry (-1: no mode runs: nothing) */
static EVT_BEGIN(mode_fight_evt)
    WAIT_FRAMES(20)
    CALL(evt_mode_battle, LW(0), LW(1), LW(2), LW(3), LW(4), LW(5))
    IF_INT_EQUAL(LW(0), -1)
        RETURN
    END_IF
    CALL(evt_npc_entry, STRING("mode_foe"), STRING("c_kuribo"))
    CALL(evt_npc_flag_onoff, 1, STRING("mode_foe"), NPC_FLAG_HIDDEN | NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_SHADOW)
    CALL(evt_npc_set_position, STRING("mode_foe"), 0, -1000, 0)
    IF_INT_NOT_EQUAL(LW(4), -1)
        CALL(evt_pm64boss_battle, STRING("mode_foe"), LW(4), LW(0), LW(5), LW(6))
        IF_INT_EQUAL(LW(6), 0)
            /* (refused, no stage to fight it on: no battle, and the mode stops) */
            CALL(evt_npc_delete, STRING("mode_foe"))
            CALL(evt_mode_refused)
            RETURN
        END_IF
    ELSE
        CALL(evt_npc_set_battle_info, STRING("mode_foe"), LW(0))
    END_IF
    IF_INT_EQUAL(LW(1), 1)
        CALL(evt_npc_set_battle_rule, STRING("mode_foe"), LW(2), LW(3), LW(3))
    END_IF
    CALL(evt_npc_battle_start, STRING("mode_foe"))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(0))
    /* the Shadow Queen's first form ends interrupted (result 5): her second fight follows, as in las_29 */
    CALL(evt_mode_phase2, LW(0), LW(7))
    IF_INT_NOT_EQUAL(LW(7), -1)
        CALL(evt_pouch_all_party_recovery) /* (the crystal stars' scene heals the party) */
        CALL(evt_npc_set_battle_info, STRING("mode_foe"), LW(7))
        CALL(evt_npc_battle_start, STRING("mode_foe"))
        CALL(evt_npc_wait_battle_end)
        CALL(evt_npc_get_battle_result, LW(0))
    END_IF
    CALL(evt_npc_get_battle_rule_keep_result, LW(1))
    CALL(evt_mode_battle_end, LW(0), LW(1))
    RETURN
EVT_END()

static EVT_BEGIN(mode_map_init)
    SPAWN_EVENT(mode_fight_evt)
    RETURN
EVT_END()

static const ModeFight* currentFight(void) {
    /* (a refight: any of them, the boss list's whatever "pm64_bosses" says) */
    if ((sStatus.mode == PORT_MODE_BOSS_RUSH || sStatus.mode == PORT_MODE_REFIGHT) && sStatus.step >= 1 &&
        sStatus.step <= (sStatus.mode == PORT_MODE_REFIGHT ? BOSS_COUNT + PM64_FIGHTS : rushCount())) {
        return bossAt(sStatus.step - 1);
    }
    if (sStatus.mode == PORT_MODE_ENDLESS_PIT) {
        return &kPitMap;
    }
    return NULL;
}

int portModeStoryPhase2(const char* files, int battle) {
    /* (btlsetup_las_tbl: 16, her first fight, ends by btlevtcmd_InterruptStop(1) in queen_phase_event, her
       first form invincible; 17, the second, is the fight that can be won. It once fought 16 alone:
       every boss rush stopped at her, result 5, and a refight of her could never be won.) */
    if (files != NULL && strncmp(files, "las", 3) == 0 && battle == 16) {
        return 17;
    }
    return -1;
}

/* in: the battle's result; out: the story fight that follows an interrupted one (portModeStoryPhase2), -1 none */
USER_FUNC(evt_mode_phase2) {
    EvtWord* args = event->args;
    const s32 result = evtGetValue(event, args[0]);
    const ModeFight* f = currentFight();
    s32 next = -1;
    (void)isFirstCall;
    if (result == 5 && !sStopPending && sStatus.mode != PORT_MODE_NONE && !sStatus.finished && f != NULL &&
        f->pm64 == 0 && gp != NULL && strcmp(gp->currentMapName, f->map) == 0) {
        next = portModeStoryPhase2(f->files, f->battle);
    }
    if (next >= 0) {
        sInFight = 1; /* (the second battle: a stop waits for its end again) */
        printf("[modes] %s's first form held off: the second fight, battle %d (step %d)\n",
               f->name != NULL ? f->name : "the boss", (int)next, (int)sStatus.step);
        fflush(stdout);
    }
    evtSetValue(event, args[1], next);
    return EVT_RETURN_DONE;
}

/* out: the battle entry (-1 none), whether a rule applies, the rule and its parameter */
USER_FUNC(evt_mode_battle) {
    EvtWord* args = event->args;
    const ModeFight* f = currentFight();
    s32 battle = -1;
    (void)isFirstCall;
    sInFight = 0;
    if (sStopPending && sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
        stopAndReturn(); /* (a stop asked on the way here: no battle, back) */
        f = NULL;
    }
    if (f != NULL && strcmp(gp->currentMapName, f->map) == 0) {
        battle = sStatus.mode == PORT_MODE_ENDLESS_PIT ? sTeam : f->battle;
        sInFight = 1; /* (from here to evt_mode_battle_end a stop waits for the battle's end) */
    }
    evtSetValue(event, args[0], battle);
    evtSetValue(event, args[1], sStatus.mode == PORT_MODE_ENDLESS_PIT && sCondition >= 0);
    evtSetValue(event, args[2], sCondition >= 0 ? kConditions[sCondition].rule : 0);
    evtSetValue(event, args[3], sCondition >= 0 ? kConditions[sCondition].param : 0);
    evtSetValue(event, args[4], battle >= 0 && f != NULL && f->pm64 > 0 ? f->pm64 - 1 : -1);
    /* a Paper Mario 64 boss fought on its own: a loss isn't the game's Game Over (back where it started) */
    /* (and, "pm64_boss_scale" on, scaled to Mario's level: the boss rush keeps its post-game rule) */
    {
        static int* sScaleOn;
        const int pm64 = sStatus.mode == PORT_MODE_REFIGHT && isPm64Fight(sStatus.step - 1);
        if (sScaleOn == NULL) {
            sScaleOn = portSettingsDeclare("pm64_boss_scale", 1, 0, 1);
        }
        evtSetValue(event, args[5],
                    pm64 ? NPC_BATTLE_LOSS_OK | (sScaleOn != NULL && *sScaleOn ? PM64_BOSS_FLAG_SCALE : 0) |
                               (sEarlyRun ? PM64_BOSS_FLAG_EARLY : 0)
                         : 0);
    }
    if (sStatus.mode == PORT_MODE_ENDLESS_PIT && sCondition >= 0) {
        /* the condition's text from the Pit map's own messages (stg3_tou_*), loaded now; looked up
         * before the map change it was the game's red "No messages" */
        sStatus.condition = msgSearch(kConditions[sCondition].msg);
    }
    printf("[modes] map %s: battle %d (step %d, PM64 boss %d)\n", gp->currentMapName, (int)battle, (int)sStatus.step,
           battle >= 0 && f != NULL && f->pm64 > 0 ? (int)f->pm64 - 1 : -1);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* --- the runs --- */

static void restorePouch(void) {
    if (sPouchSaved) {
        *pouchGetPtr() = sSavedPouch;
        sPouchSaved = 0;
        printf("[modes] the file's pouch back: level %d, HP %d/%d, FP %d/%d\n", pouchGetPtr()->level,
               pouchGetPtr()->currentHP, pouchGetPtr()->maximumHP, pouchGetPtr()->currentFP, pouchGetPtr()->maximumFP);
        fflush(stdout);
    }
    portPm64JoinHold(0); /* (the file's own partners again: the PM64 ones join by them) */
}

static void restoreDifficulty(void) {
    const int* d = portSettingsFind("difficulty");
    if (sStatus.mode == PORT_MODE_ENDLESS_PIT) {
        printf("[modes] the Pit's scaling off: the player's own difficulty again (difficulty %d)\n",
               d != NULL ? *d : -1);
        fflush(stdout);
    }
    portDifficultyOverride(0, 0); /* (it was never changed) */
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
    s32 lo = sStatus.step < PIT_TEAMS ? sStatus.step : PIT_TEAMS - 8;
    s32 hi = sStatus.step + 2 < PIT_TEAMS ? sStatus.step + 2 : PIT_TEAMS;
    sTeam = lo + irand(hi - lo + 1);
    sCondition = portModeRunPitMatch((int)sStatus.step, irand(CONDITION_COUNT), CONDITION_COUNT); /* (tests) */
    snprintf(sTeamName, sizeof(sTeamName), "Glitz Pit team %d of %d", (int)sTeam, PIT_TEAMS);
    sStatus.next = sTeamName;
    sStatus.condition = NULL; /* its text once on the Pit's map (evt_mode_battle) */
    {
        const int* d = portSettingsFind("difficulty");
        const int* hp = portSettingsFind("difficulty_enemy_hp");
        const int* atk = portSettingsFind("difficulty_enemy_attack");
        printf("[modes] pit round %d: enemies at %d%% HP, %d%% attack (the mode's own scaling; the player's "
               "settings stay difficulty %d, enemy HP %d%%, attack %d%%)\n",
               (int)sStatus.step, 100 + 10 * (sStatus.step - 1) < 300 ? 100 + 10 * (int)(sStatus.step - 1) : 300,
               100 + 5 * (sStatus.step - 1) < 300 ? 100 + 5 * (int)(sStatus.step - 1) : 300, d != NULL ? *d : -1,
               hp != NULL ? *hp : -1, atk != NULL ? *atk : -1);
        fflush(stdout);
    }
    /* (the mode's own percentages over the difficulty, as custom's were: never the player's settings) */
    portDifficultyOverride(100 + 10 * (sStatus.step - 1) < 300 ? 100 + 10 * (sStatus.step - 1) : 300,
                           100 + 5 * (sStatus.step - 1) < 300 ? 100 + 5 * (sStatus.step - 1) : 300);
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
    sInFight = 0;
    if (sStatus.mode == PORT_MODE_NONE || sStatus.finished) {
        return EVT_RETURN_DONE;
    }
    if (result == 1 && (sStatus.mode == PORT_MODE_BOSS_RUSH || sStatus.mode == PORT_MODE_REFIGHT) &&
        isPm64Fight(sStatus.step - 1)) {
        portPm64BossSetBeaten(sStatus.step - 1 - BOSS_COUNT); /* (its star; in "Beat their boss", its partner) */
    }
    if (sStopPending) {
        stopAndReturn(); /* (asked in the battle: the battle over, the pouch back after it wrote its end) */
        return EVT_RETURN_DONE;
    }
    if (result == 5) {
        /* interrupted, or refused before it began (evt_npc_battle_start: no stage for its entry): a stop and
           back, never finish(2) with Mario left on the mode's exitless map */
        printf("[modes] fight refused (battle result 5): the mode stops\n");
        fflush(stdout);
        portCoopNotice("The fight couldn't be set up here: the mode stopped.");
        stopAndReturn();
        return EVT_RETURN_DONE;
    }
    if (sStatus.mode == PORT_MODE_REFIGHT) {
        /* won or fled: back where it started (a loss is the game's Game Over, as in the story; Paper Mario 64's
           boss lost: back too, the battle's LOSS_OK) */
        if (result == 1 || result == 4 || isPm64Fight(sStatus.step - 1)) {
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
        if (sStatus.step >= rushCount()) {
            finish(1);
            /* back where it started, as a stop and a refight's end come back (it once always went to Rogueport:
               a rush begun from the Modes page in Glitzville or Petalburg ended in gor_01) */
            seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
            return EVT_RETURN_DONE;
        }
        sStatus.step++;
        sStatus.next = bossAt(sStatus.step - 1)->name;
        goTo(bossAt(sStatus.step - 1));
    } else {
        if (sCondition >= 0 && kept != 6) {
            finish(2); /* the condition was broken: the run ends here */
            /* back where it started (the Glitz Pit's lobby, tou_02, when begun there): it once always went to
               tou_02, so a run begun from the Modes page elsewhere left Mario in Glitzville */
            seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "tou_02", NULL);
            return EVT_RETURN_DONE;
        }
        pouchAddCoin(10 + sStatus.step);
        sStatus.step++;
        pitNextMatch();
        goTo(&kPitMap);
    }
    return EVT_RETURN_DONE;
}

void portModeLendGear(int jump, int hammer) {
    static const s16 kBoots[3] = {ITEM_BOOTS, ITEM_SUPER_BOOTS, ITEM_ULTRA_BOOTS};
    static const s16 kHammers[3] = {ITEM_HAMMER, ITEM_SUPER_HAMMER, ITEM_ULTRA_HAMMER};
    PouchData* p = pouchGetPtr();
    s32 i, n = 0;
    jump = jump < 0 ? 0 : jump > 3 ? 3 : jump;
    hammer = hammer < 0 ? 0 : hammer > 3 ? 3 : hammer;
    /* the file's boots and hammers out (a higher tier would win the recount), the rest kept in order */
    for (i = 0; i < MAX_KEY_ITEMS; i++) {
        const s16 k = p->keyItems[i];
        if (k >= ITEM_BOOTS && k <= ITEM_ULTRA_HAMMER) {
            continue;
        }
        p->keyItems[n++] = k;
    }
    for (i = n; i < MAX_KEY_ITEMS; i++) {
        p->keyItems[i] = ITEM_NULL;
    }
    /* (written straight in, not by pouchGetItem: no item-got hooks for lent gear) */
    if (jump > 0 && n < MAX_KEY_ITEMS) {
        p->keyItems[n++] = kBoots[jump - 1];
    }
    if (hammer > 0 && n < MAX_KEY_ITEMS) {
        p->keyItems[n++] = kHammers[hammer - 1];
    }
    p->jumpLevel = (s8)jump;
    p->hammerLevel = (s8)hammer;
}

void portModeLendRevise(int fitBadges) {
    PouchData* p = pouchGetPtr();
    s32 i, cost = 0;
    for (i = 0; i < MAX_BADGES; i++) {
        cost += itemDataTable[p->equippedBadges[i]].bp_cost;
    }
    for (i = MAX_BADGES - 1; fitBadges && i >= 0 && cost > p->maximumBP; i--) {
        if (p->equippedBadges[i] != ITEM_NULL) {
            cost -= itemDataTable[p->equippedBadges[i]].bp_cost;
            p->equippedBadges[i] = ITEM_NULL;
        }
    }
    pouchReviseMarioParam(); /* (max HP and FP from the base and HP/FP Plus, the BP left) */
    pouchRevisePartyParam(); /* (each partner's max HP from its rank and HP Plus P) */
    p->currentHP = p->maximumHP;
    p->currentFP = p->maximumFP;
    for (i = 1; i <= 7; i++) {
        p->partyData[i].currentHP = p->partyData[i].maximumHP;
    }
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
    portPm64JoinHold(1); /* (every TTYD partner lent for the run: no PM64 partner joins by them) */
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
    /* (boots and hammer 1 normal, 2 Super, 3 Ultra: mario_pouch.c. Mid-game 1 was the normal boots, whose 1 a hit
       did nothing to a defence of 2: Jr. Troopa stood through every turn) */
    portModeLendGear(late ? 3 : 2, late ? 3 : 2); /* (with the key items the field recounts them from) */
    p->starPowersObtained = (u16)(late ? 0xFF : 0x0F);
    p->maximumSP = p->currentSP = (s16)(late ? 800 : 400);
    memset(p->heldItems, 0, sizeof(p->heldItems));
    for (i = 0; i < 8; i++) {
        pouchGetItem((ItemType)kItems[late][i]);
    }
    /* (the file's badges stay worn as far as the lent BP goes; the partners' max HP by their lent rank: before,
       they kept the file's, and HP Plus counted only at the next equip) */
    portModeLendRevise(1);
    printf("[modes] loadout %s: level %d, HP %d, FP %d, BP %d (%d left), boots %d, hammer %d (recounted %d %d), "
           "Goombella HP %d\n",
           late ? "late" : "mid", p->level, p->maximumHP, p->maximumFP, p->maximumBP, p->availableBP, p->jumpLevel,
           p->hammerLevel, (int)pouchGetJumpLv(), (int)pouchGetHammerLv(), p->partyData[1].maximumHP);
    fflush(stdout);
}

int portModePitUnlocked(void) {
    return swStoryPeek() >= STORY_STG3_34_RETURN_TO_ROGUEPORT; /* (menus read it too: no side effects) */
}

/* Starts and stops asked for from the settings page (its own thread): done
 * on the game's frame (frameHook) */
static int sStartMode, sStartLoadout, sStopAsked; /* __atomic */
static int sStartFrom = 1; /* __atomic: a boss rush's first step */

static void startNow(int mode, int loadout) {
    stopNow();
    memset(&sStatus, 0, sizeof(sStatus));
    sStatus.mode = mode;
    sStatus.step = 1;
    sStatus.loadout = loadout;
    sStartedAt = SDL_GetTicks();
    sStopPending = 0;
    sInFight = 0;
    sEarlyRun = __atomic_exchange_n(&sEarlyAsk, 0, __ATOMIC_SEQ_CST) && mode == PORT_MODE_REFIGHT;
    /* where it started, for every mode: a refight's end and a stop come back to it (the modes' maps have no way
       out) */
    snprintf(sReturnMap, sizeof(sReturnMap), "%s", gp->currentMapName);
    sReturnPos = marioGetPtr()->position;
    sReturnPosAsked = 1;
    if (mode == PORT_MODE_REFIGHT) {
        sStatus.step = __atomic_load_n(&sRefightBoss, __ATOMIC_SEQ_CST) + 1;
        sStatus.steps = 1;
        sStatus.next = bossAt(sStatus.step - 1)->name;
        sCondition = -1;
        if (loadout != PORT_LOADOUT_OWN) {
            giveLoadout(loadout);
        }
        goTo(bossAt(sStatus.step - 1));
    } else if (mode == PORT_MODE_BOSS_RUSH) {
        /* "Start at" (as it was asked for: a leaderboard's run from the start); $TTYD_MODE_STEP=N: from the Nth
           boss (tests) */
        const char* step = getenv("TTYD_MODE_STEP");
        const int from = step != NULL && atoi(step) >= 1 ? atoi(step) : __atomic_load_n(&sStartFrom, __ATOMIC_SEQ_CST);
        sRushPm64 = portPm64BossesOn() || (from > BOSS_COUNT && portPm64BossesReady());
        sStatus.step = from >= 1 && from <= rushCount() ? from : 1;
        sStatus.from = sStatus.step;
        sStatus.steps = rushCount();
        sStatus.next = bossAt(sStatus.step - 1)->name;
        sCondition = -1;
        if (loadout != PORT_LOADOUT_OWN) {
            giveLoadout(loadout);
        }
        goTo(bossAt(sStatus.step - 1));
    } else {
        /* $TTYD_MODE_STEP=N: from the Nth round (tests: the scaling above 100%) */
        const char* step = getenv("TTYD_MODE_STEP");
        if (step != NULL && atoi(step) >= 1) {
            sStatus.step = atoi(step);
        }
        pitNextMatch();
        goTo(&kPitMap);
    }
    printf("[modes] %s started (from %s at %.0f %.0f %.0f)\n",
           mode == PORT_MODE_BOSS_RUSH ? "boss rush" : mode == PORT_MODE_REFIGHT ? "refight" : "endless pit", sReturnMap,
           sReturnPos.x, sReturnPos.y, sReturnPos.z);
    fflush(stdout);
}

static void stopNow(void) {
    if (sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
        restoreDifficulty();
        restorePouch();
    }
    memset(&sStatus, 0, sizeof(sStatus));
    sCondition = -1;
    sStopPending = 0;
    sInFight = 0;
}

/* A stop (the Modes tab, the Start menu's row) once no battle is on: the file's pouch and the player's
 * difficulty back, and Mario back where the mode started, in his spot (returnToSpot). Before, he stayed on the
 * mode's map, which has no way out (a softlock), and a stop in a battle put the file's pouch back under it, the
 * battle's end then writing the loadout's HP into it. */
static void stopAndReturn(void) {
    const int running = sStatus.mode != PORT_MODE_NONE && !sStatus.finished;
    const int away = gp != NULL && portModsIsHostMap(gp->currentMapName);
    if (running) {
        printf("[modes] %s stopped at step %d: back to %s\n",
               sStatus.mode == PORT_MODE_BOSS_RUSH ? "boss rush"
               : sStatus.mode == PORT_MODE_REFIGHT ? "refight"
                                                   : "endless pit",
               (int)sStatus.step, sReturnMap[0] != '\0' ? sReturnMap : "gor_01");
        fflush(stdout);
    }
    stopNow();
    if (running && away) {
        seqSetSeq(SEQ_MAP_CHANGE, sReturnMap[0] != '\0' ? sReturnMap : "gor_01", NULL);
    }
}

/* After a refight (or a stop), back in its room: Mario (and his partner) where he stood when it began, once
 * he's free */
static void returnToSpot(void) {
    PartyEntry* party;
    int i;
    if (!sReturnPosAsked || seqGetSeq() != SEQ_GAME || seqCheckSeq() || gp->inBattle ||
        (sStatus.mode != PORT_MODE_NONE && !sStatus.finished)) {
        return;
    }
    if (strcmp(gp->currentMapName, sReturnMap) != 0) {
        sReturnPosAsked = 0; /* (somewhere else: a Game Over's file loaded, a warp) */
        return;
    }
    if (!marioChkCtrl()) {
        return;
    }
    sReturnPosAsked = 0;
    marioGetPtr()->position = sReturnPos;
    for (i = 0; i < 2; i++) {
        if ((party = partyGetPtr(i == 0 ? partyCtrlNo : 1)) != NULL) {
            party->position = sReturnPos;
        }
    }
    printf("[modes] back at %.0f %.0f %.0f in %s\n", sReturnPos.x, sReturnPos.y, sReturnPos.z, sReturnMap);
    fflush(stdout);
}

/* After marioStMain, each frame of the game */
static void frameHook(void) {
    {
        /* $TTYD_MODE_STOP=N (tests): a stop asked N frames into the run, as the Modes tab's Stop is */
        static const char* sStopTest;
        static int sStopRead;
        static unsigned sRunFrames;
        if (!sStopRead) {
            sStopRead = 1;
            sStopTest = getenv("TTYD_MODE_STOP");
        }
        sRunFrames = sStatus.mode != PORT_MODE_NONE && !sStatus.finished ? sRunFrames + 1 : 0;
        if (sStopTest != NULL && atoi(sStopTest) > 0 && sRunFrames == (unsigned)atoi(sStopTest)) {
            printf("[modes] TTYD_MODE_STOP: stop asked (%s)\n", gp->inBattle ? "in a battle" : "in the field");
            fflush(stdout);
            portModeStop();
        }
    }
    if (__atomic_exchange_n(&sStopAsked, 0, __ATOMIC_SEQ_CST)) {
        if (sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
            sStopPending = 1; /* (done below in the field, or as the battle ends) */
        } else {
            stopNow();
        }
    }
    if (sStopPending) {
        if (sStatus.mode == PORT_MODE_NONE || sStatus.finished) {
            sStopPending = 0; /* (ended meanwhile: a Game Over, the title) */
        } else if (seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && !seqCheckSeq() && !sInFight) {
            stopAndReturn(); /* (on a mode's map before its battle, or a start's frame) */
        }
    }
    returnToSpot();
    /* a start waits for the field (not a map change or a battle) and for a menu to close (the Start
     * menu's pages start these too: port/settings/features/modes.cpp) */
    if (seqGetSeq() == SEQ_GAME && !gp->inBattle && !seqCheckSeq() && marioStGetSystemLevel() == 0) {
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
    if (seqGetSeq() == SEQ_GAME_OVER || seqGetSeq() == SEQ_TITLE) {
        sReturnPosAsked = 0;
    }
    portModeSwitchFrame(); /* (a start asked for with a leave: once the mode left is over) */
    portModeRunFrame();    /* (tests: $TTYD_MODERUN's checks, port/include/port/moderun.h) */
}

int portModeLending(void) {
    int s = 0;
    if (gp == NULL) {
        return 0;
    }
    if (sStatus.mode != PORT_MODE_NONE && !sStatus.finished) {
        s |= PORT_LEND_MAP | (sStatus.mode == PORT_MODE_ENDLESS_PIT ? PORT_LEND_SETTINGS : 0);
    }
    if (sPouchSaved) {
        s |= PORT_LEND_POUCH;
    }
    if (portRaidActive()) {
        s |= PORT_LEND_POUCH | PORT_LEND_MAP;
    }
    if (portDojoActive()) {
        s |= PORT_LEND_POUCH | PORT_LEND_MAP; /* (its end puts the file's pouch back, "every move" or not) */
    }
    if (portPuzzleActive(NULL, NULL, NULL) >= 0) {
        s |= PORT_LEND_POUCH | PORT_LEND_MAP;
    }
    if (portVersusBusy()) {
        s |= PORT_LEND_POUCH | PORT_LEND_WORDS | PORT_LEND_MAP;
    }
    /* the modes' own maps (a start's and an end's frames too: the map change asked, not yet made), and a
       refight's room before Mario is back in his spot */
    if (portModsIsHostMap(gp->currentMapName) || portPuzzleReturning() ||
        (sReturnPosAsked && strcmp(gp->currentMapName, sReturnMap) == 0)) {
        s |= PORT_LEND_MAP;
    }
    return s;
}

const char* portModeStartRefusal(void) {
    const char* story;
    if (gp == NULL) {
        return "Start it in the field, with a file loaded.";
    }
    if (portModeLending() != 0 || __atomic_load_n(&sStartMode, __ATOMIC_SEQ_CST) != PORT_MODE_NONE ||
        portModeLeaveName() != NULL) {
        /* (portModeLeaveName: the field's modes too, which lend nothing: a climb of the Paper Tower, a chapter
           replay, the Pit in co-op. Before, a boss rush could start in the middle of one. The Start menu then
           asks to leave it and start the new one: port/include/port/mode_leave.h) */
        return "Another mode is running: finish it first.";
    }
    if (psndGetFlag() & PSND_FLAG_MUTE_DOOR_SFX) {
        /* (a house or shop the game shows on walking in: come back to without its door, the inside is hidden and
           its floor off, and Mario fell for ever: resume_aurora.c) */
        return "Not indoors: step outside first.";
    }
    if ((story = portTravelStoryKeeps()) != NULL) {
        return story; /* (the trip away and back would undo the stretch: the train, a timer, a follower) */
    }
    return NULL;
}

static int refused(const char** why) {
    static const char* sLastSaid; /* (a test start is tried each frame: each reason said once in a row) */
    const char* r = portModeStartRefusal();
    if (r != NULL) {
        *why = r;
        if (r != sLastSaid) {
            printf("[modes] start refused: %s\n", r);
            fflush(stdout);
        }
        sLastSaid = r;
        return 1;
    }
    sLastSaid = NULL;
    return 0;
}

int portModeStart(int mode, int loadout, const char** why) {
    if (seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (refused(why)) {
        return 0;
    }
    if (mode == PORT_MODE_ENDLESS_PIT && !portModePitUnlocked()) {
        *why = "The endless Glitz Pit opens once the story's Glitz Pit is done.";
        return 0;
    }
    __atomic_store_n(&sStartFrom, portModeRushStart() + 1, __ATOMIC_SEQ_CST); /* (choice c: step c + 1) */
    __atomic_store_n(&sStartLoadout, loadout, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartMode, mode, __ATOMIC_SEQ_CST);
    return 1;
}

int portModeBossCount(void) {
    return bossCount();
}

const char* portModeBossName(int boss) {
    return boss >= 0 && boss < bossCount() ? bossAt(boss)->name : "";
}

int portModeBossBeaten(int boss) {
    if (boss >= BOSS_COUNT && boss < bossCount()) {
        return gp != NULL; /* Paper Mario 64's: open any time */
    }
    return boss >= 0 && boss < BOSS_COUNT && gp != NULL && swStoryPeek() >= kBeatenAt[boss];
}

int portModeRefight(int boss, int loadout, const char** why) {
    if (boss < 0 || boss >= bossCount()) {
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
    if (refused(why)) {
        return 0;
    }
    __atomic_store_n(&sRefightBoss, boss, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartLoadout, loadout, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartMode, PORT_MODE_REFIGHT, __ATOMIC_SEQ_CST);
    return 1;
}

int portModePm64Fight(int fight, int loadout, const char** why) {
    return portModePm64FightAs(fight, loadout, 0, why);
}

int portModePm64FightAs(int fight, int loadout, int early, const char** why) {
    const PortModeStatus* s = &sStatus;
    if (fight < 0 || fight >= PM64_FIGHTS) {
        *why = "No such boss.";
        return 0;
    }
    if (!portPm64BossesReady()) {
        *why = portPm64Linked() == PM64_NONE ? "Link your Paper Mario ROM first." : "Not during online play or a replay.";
        return 0;
    }
    if (seqGetSeq() != SEQ_GAME || gp->inBattle) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (s->mode != PORT_MODE_NONE && !s->finished) {
        *why = "Not while another mode runs.";
        return 0;
    }
    if (refused(why)) {
        return 0;
    }
    __atomic_store_n(&sRefightBoss, BOSS_COUNT + fight, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartLoadout, loadout, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sEarlyAsk, early && portPm64FightHasEarly(fight), __ATOMIC_SEQ_CST);
    __atomic_store_n(&sStartMode, PORT_MODE_REFIGHT, __ATOMIC_SEQ_CST);
    printf("[modes] Paper Mario 64 refight asked: %s, loadout %d%s\n", portPm64FightEnglish(fight), loadout,
           early && portPm64FightHasEarly(fight) ? ", his first fight" : "");
    fflush(stdout);
    return 1;
}

/* A leaderboard run (board_run_aurora.c): started on the first frame in the
 * field, as a start from the Modes tab is, without its checks (at start-up
 * there's no field yet); a boss rush from the start */
void portModeQueue(int mode, int loadout) {
    __atomic_store_n(&sStartFrom, 1, __ATOMIC_SEQ_CST);
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
    (void)portPm64BossesOn(); /* (its setting declared; its fights are on the maps above) */
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
    portMapEditsInit();     /* the map edits' frame (map_edits.h) */
    portRaidInit();         /* the raid boss's maps and hooks (raid.h) */
    portDojoRegisterMaps(); /* the action-command dojo's map and hooks (dojo.h) */
    portTitleMenuInit();    /* the title menu's setting and its temporary file's hook (title_menu.h) */
    portTogetherInit();     /* the Multiplayer page's picks and seats each frame (together.h) */
    portModeLeaveInit();    /* a mode's battle left by Start, Start (mode_leave.h) */
}
