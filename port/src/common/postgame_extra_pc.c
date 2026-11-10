/* The post-game's calls from the game's own code (port/include/port/postgame_extra.h),
 * each behind its setting (off by default):
 *
 *   - portPgRecipeCook (gor_01.c cook_chk2, through portRecipeCook): the
 *     Japanese release's recipe its US and European tables dropped, Thunder
 *     Rage with Thunder Bolt for a Trial Stew ("pg_jp_recipes").
 *   - portPgCranePrize (aji_03.c): after the story, the crane game's coins
 *     are one of four batches of prizes, the next batch each visit
 *     ("pg_crane_prizes"; the batch is kept in the save's GSWF bits).
 *   - portPgJpBadEnding (las_29.c): the Japanese release's bad ending, where
 *     Mario stays in the room, the Shadow Queen's, instead of fading away
 *     ("pg_jp_bad_ending"; on the JP release it's the game's own).
 *   - portPgBattleTheme (pmario_sound.c through music_pc.c's portMusicPick):
 *     the usual battle music swapped, area by area, for another of the disc's
 *     battle tracks ("pg_battle_theme_<n>"; music only, so online too). */
#include "port/statesync.h"
#include "port/postgame_extra.h"
#include "port/gameplus.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/settings.h"
#include "driver/itemdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr_cmd.h"
#include "version.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <string.h>

_Static_assert(PORT_GSWF_PG_NIGHTTAIL_BEATEN <= PORT_GSWF_POSTGAME_LAST, "the postgame's flags: port/include/port/save_words.h");

static int settingOn(const char* key) {
    int* v = portSettingsDeclare(key, 0, 0, 1);
    return v != NULL && *v != 0;
}

/* play-changing ones: never online or in a replay */
static int playOn(const char* key) {
    return settingOn(key) && !portStateSyncLinked() && !portReplayActive();
}

/* --- the Japanese release's recipe ------------------------------------------------- */

int portPgRecipeCook(int a, int b) {
#if VERSION != VERSION_JP
    if (playOn("pg_jp_recipes") && ((a == ITEM_THUNDER_RAGE && b == ITEM_THUNDER_BOLT) ||
                                    (a == ITEM_THUNDER_BOLT && b == ITEM_THUNDER_RAGE))) {
        printf("[postgame] Japanese recipe: 0x%x + 0x%x -> Trial Stew\n", (unsigned)a, (unsigned)b);
        fflush(stdout);
        return ITEM_TRIAL_STEW;
    }
#else
    (void)a;
    (void)b;
#endif
    return 0;
}

/* --- the crane game ---------------------------------------------------------------- */

/* four batches of prizes for the crane's coin places (keihin00-03, 05, 07) */
static const short kCrane[4][6] = {
    {ITEM_SUPER_SHROOM, ITEM_MAPLE_SYRUP, ITEM_FIRE_FLOWER, ITEM_THUNDER_RAGE, ITEM_HONEY_SYRUP, ITEM_COIN},
    {ITEM_ULTRA_SHROOM, ITEM_JAMMIN_JELLY, ITEM_ICE_STORM, ITEM_EARTH_QUAKE, ITEM_MUSHROOM, ITEM_COIN},
    {ITEM_LIFE_SHROOM, ITEM_SHOOTING_STAR, ITEM_REPEL_CAPE, ITEM_BOOS_SHEET, ITEM_SUPER_SHROOM, ITEM_COIN},
    {ITEM_MYSTERY, ITEM_POWER_PUNCH, ITEM_COURAGE_SHELL, ITEM_STOPWATCH, ITEM_ULTRA_SHROOM, ITEM_COIN},
};

int portPgCranePrize(int slot, int fallback) {
    static const signed char kPlace[9] = {0, 1, 2, 3, -1, 4, -1, 5, -1};
    int batch, place;
    if (gp == NULL || slot < 0 || slot > 8) {
        return fallback;
    }
    place = kPlace[slot]; /* (read once: the analyzer lost the check across the calls below) */
    if (place < 0 || swByteGet(0) < STORY_STG8_END_AFTER_THE_BATTLE || !playOn("pg_crane_prizes")) {
        return fallback;
    }
    batch = (swGet(PORT_GSWF_PG_CRANE_0) ? 1 : 0) | (swGet(PORT_GSWF_PG_CRANE_1) ? 2 : 0);
    if (slot == 8 || slot == 7) {
        /* the last of the room's places: the next visit, the next batch (also when the
         * randomizer holds this place) */
        const int next = (batch + 1) & 3;
        (next & 1) ? swSet(PORT_GSWF_PG_CRANE_0) : swClear(PORT_GSWF_PG_CRANE_0);
        (next & 2) ? swSet(PORT_GSWF_PG_CRANE_1) : swClear(PORT_GSWF_PG_CRANE_1);
    }
    if (fallback != ITEM_COIN) {
        return fallback; /* a randomizer's item in the coin's place */
    }
    return kCrane[batch][place];
}

ItemWork* portItemGetWork(void); /* itemdrv.c */

int portEvtCranePrize(struct EventEntry* event, int isFirstCall) {
    /* after evt_item_entry(name, ITEM_COIN, ...): the coin swapped for the prize. The coin stays in
     * the script so the randomizer's places (ttyd_ap_data.h) hold the disc's word. */
    const char* name = (const char*)evtGetValue(event, event->args[1]);
    ItemWork* wp = portItemGetWork();
    ItemEntry* entry = NULL;
    int i;
    (void)isFirstCall;
    for (i = 0; name != NULL && wp != NULL && i < wp->count; i++) {
        if ((wp->entries[i].flags & ITEM_FLAG_USED) && !strcmp(wp->entries[i].name, name)) {
            entry = &wp->entries[i];
            break;
        }
    }
    if (entry != NULL) {
        const int prize = portPgCranePrize(evtGetValue(event, event->args[0]), entry->itemId);
        if (prize != entry->itemId) {
            const Vec pos = entry->position;
            const u16 type = entry->type;
            EvtWord* pickup = (EvtWord*)entry->evtCode;
            char keep[sizeof entry->name];
            strcpy(keep, entry->name);
            itemDelete(keep);
            itemEntry(pos.x, pos.y, pos.z, keep, prize, type, -1, pickup);
        }
    }
    return EVT_RETURN_DONE;
}

/* --- the Japanese bad ending -------------------------------------------------------- */

int portPgJpBadEnding(void) {
#if VERSION == VERSION_JP
    return 1;
#else
    return settingOn("pg_jp_bad_ending");
#endif
}

int portEvtJpBadEnding(struct EventEntry* event, int isFirstCall) {
    const int jp = portPgJpBadEnding();
    (void)isFirstCall;
    printf("[postgame] bad ending: %s\n", jp ? "the Japanese release's" : "the original");
    fflush(stdout);
    evtSetValue(event, event->args[0], jp);
    return EVT_RETURN_DONE;
}

/* --- battle themes ------------------------------------------------------------------ */

static const struct {
    const char* track;
    const char* name;
} kThemes[] = {
    {NULL, "The game's own"},
    {"BGM_ZAKO_BATTLE1", "Battle"},
    {"BGM_KOBOSS_BATTLE1", "Strong foe"},
    {"BGM_CHUBOSS_BATTLE1", "Big battle"},
    {"BGM_BOSS_STG1_GONBABA1", "Hooktail"},
    {"BGM_BOSS_STG3_3WOMEN1", "The Shadow Sirens"},
    {"BGM_BOSS_STG3_CHAMP1", "Rawk Hawk"},
    {"BGM_BOSS_STG3_GANCE1", "Macho Grubba"},
    {"BGM_BOSS_STG3_3RDLEADER1", "Lord Crump"},
    {"BGM_BOSS_STG4_RUNPELL1", "Doopliss"},
    {"BGM_BOSS_STG4_FAKEMARIO1", "Doopliss as Mario"},
    {"BGM_BOSS_STG5_KORTESS1", "Cortez"},
    {"BGM_BOSS_STG5_SHIP1", "The pirate ship"},
    {"BGM_BOSS_STG6_GREAT_MOAMOA1", "Smorg"},
    {"BGM_BOSS_STG8_3RDHEAD1", "Sir Grodus"},
    {"BGM_BOSS_STG8_KOOPA", "Bowser"},
    {"BGM_BOSS_STG8_WITCH", "The Shadow Sirens, again"},
    {"BGM_BOSS_STG8_LAST_BOSS1", "The Shadow Queen"},
    {"BGM_BOSS_STG8_LAST_BOSS2", "The Shadow Queen's last stand"},
};
#define THEMES ((int)(sizeof(kThemes) / sizeof(kThemes[0])))

static const struct {
    const char* name;
    const char* areas[5];
} kAreas[PORT_PG_THEME_AREAS] = {
    {"Rogueport and its underground", {"gor", "tik", "aaa"}},
    {"Petal Meadows and Hooktail Castle", {"hei", "nok", "gon"}},
    {"Boggly Woods and the Great Tree", {"win", "mri"}},
    {"Glitzville", {"tou"}},
    {"Twilight Town and Creepy Steeple", {"usu", "gra", "jin"}},
    {"Keelhaul Key and Pirate's Grotto", {"muj", "dou"}},
    {"The Excess Express, Riverside and Poshley Heights", {"rsh", "eki", "hom", "pik"}},
    {"Fahr Outpost, the moon and the X-Naut Fortress", {"bom", "moo", "aji"}},
    {"The Palace of Shadow", {"las"}},
    {"The Pit of 100 Trials", {"jon"}},
};

int portPgBattleThemeCount(void) {
    return THEMES;
}

const char* portPgBattleThemeName(int i) {
    return i >= 0 && i < THEMES ? kThemes[i].name : "";
}

const char* portPgThemeAreaName(int area) {
    return area >= 0 && area < PORT_PG_THEME_AREAS ? kAreas[area].name : "";
}

static int trackId(const char* name) {
    int i;
    for (i = 1; i < portBgmCount(); i++) {
        if (strcmp(portBgmName(i), name) == 0) {
            return i;
        }
    }
    return -1;
}

int portPgBattleTheme(int id) {
    static int sUsual = -2;
    static int* sKeys[PORT_PG_THEME_AREAS];
    int area, a, pick, to;
    if (sUsual == -2) {
        sUsual = trackId("BGM_ZAKO_BATTLE1");
        for (area = 0; area < PORT_PG_THEME_AREAS; area++) {
            char key[32];
            snprintf(key, sizeof(key), "pg_battle_theme_%d", area);
            sKeys[area] = portSettingsDeclare(key, 0, 0, THEMES - 1);
        }
    }
    if (id != sUsual || id < 0 || gp == NULL) {
        return id;
    }
    for (area = 0; area < PORT_PG_THEME_AREAS; area++) {
        for (a = 0; a < 5 && kAreas[area].areas[a] != NULL; a++) {
            if (strncmp(gp->currentMapName, kAreas[area].areas[a], 3) == 0) {
                pick = sKeys[area] != NULL ? *sKeys[area] : 0;
                if (pick <= 0 || pick >= THEMES) {
                    return id;
                }
                to = trackId(kThemes[pick].track);
                return to > 0 ? to : id;
            }
        }
    }
    return id;
}
