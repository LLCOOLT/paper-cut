/* Local achievements (port/include/port/practice.h): read from the save's own
 * flags and the pouch, so a save from anywhere counts what it has done. Kept
 * per save file beside the memory cards, not in the .gci:
 *   <user>/Achievements/<game id>/file<N>.txt  "name=<player>", then "<id> <date>"
 * Given only while the "achievements" setting is on and the practice tools
 * haven't changed the game since the file was loaded; what a file has already
 * done when it's loaded is recorded quietly, without a toast. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "port/practice.h"
#include "port/practice_tools.h"
#include "port/settings.h"
#include "win/win_main.h"
#ifdef _WIN32
#include <direct.h>
#define makeDir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define makeDir(p) mkdir(p, 0755)
#endif

typedef enum Kind {
    K_STORY,   /* GSW 0 reached a */
    K_FLAG,    /* GSWF a set */
    K_STAR_PIECES,
    K_SHINES,
    K_RECIPES,
    K_BADGES,
    K_TATTLES,
    K_TROUBLES,
    K_PARTNERS,
    K_LEVEL,
    K_COINS,
} Kind;

typedef struct Achievement {
    const char* id; /* in the file: never renamed */
    const char* name;
    const char* note;
    Kind kind;
    int a; /* the story value or flag; for counts, the goal (0: all there are) */
} Achievement;

static const Achievement kList[] = {
    {"prologue", "A Paper Adventure", "Finish the prologue.", K_STORY, STORY_STG1_00_PETAL_MEADOWS},
    {"chapter1", "Castle Crasher", "Finish Chapter 1: Castle and Dragon.", K_STORY, STORY_STG2_00_BOGGLY_WOODS},
    {"chapter2", "Tree Hugger", "Finish Chapter 2: The Great Boggly Tree.", K_STORY,
     STORY_STG3_00_WELCOME_TO_GLITZVILLE},
    {"chapter3", "Champ", "Finish Chapter 3: Of Glitz and Glory.", K_STORY, STORY_STG4_00_WELCOME_TO_STAGE_4},
    {"chapter4", "Name Recalled", "Finish Chapter 4: For Pigs the Bell Tolls.", K_STORY, STORY_STG5_00_LOGBOOK},
    {"chapter5", "Shipwrecked", "Finish Chapter 5: The Key to Pirates.", K_STORY, STORY_STG6_00_START},
    {"chapter6", "All Aboard", "Finish Chapter 6: 3 Days of Excess.", K_STORY, STORY_STG7_00_START},
    {"chapter7", "Moonwalker", "Finish Chapter 7: Mario Shoots the Moon.", K_STORY, STORY_STG8_00_START},
    {"chapter8", "The Door Closes", "Defeat the Shadow Queen.", K_STORY, STORY_STG8_END_AFTER_THE_BATTLE},
    {"credits", "Happily Ever After", "See the end credits.", K_STORY, STORY_STG9_01_END_CREDITS},
    {"star_pieces_half", "Shiny Things", "Find 50 Star Pieces.", K_STAR_PIECES, 50},
    {"star_pieces_all", "Star Piece Collector", "Find every Star Piece.", K_STAR_PIECES, 0},
    {"shine_sprites_all", "Sprite Hunter", "Find every Shine Sprite.", K_SHINES, 0},
    {"recipe_first", "First Course", "Have Zess T. cook a dish.", K_FLAG, GSWF_RECIPE_LOG_UNLOCKED},
    {"recipes_all", "Master Chef", "Fill in every recipe in the Journal.", K_RECIPES, 0},
    {"badges_half", "Badge Fan", "Find 40 kinds of badge.", K_BADGES, 40},
    {"badges_all", "Badge Log Complete", "Find every kind of badge.", K_BADGES, 0},
    {"tattles_half", "Know Your Enemy", "Tattle 60 kinds of enemy.", K_TATTLES, 60},
    {"tattles_all", "Tattle Log Complete", "Tattle every enemy in the Tattle Log.", K_TATTLES, 0},
    {"pit_50", "Halfway Down", "Reach the 50th floor of the Pit of 100 Trials.", K_FLAG,
     GSWF_JON_FLOOR50_CHEST_OPENED},
    {"pit_cleared", "Pit of 100 Trials", "Defeat Bonetail at the bottom of the Pit.", K_FLAG,
     GSWF_JON_BONETAIL_DEFEATED},
    {"troubles_all", "Trouble Shooter", "Solve every trouble at the Trouble Center.", K_TROUBLES, 30},
    {"partners_all", "Full Party", "Have all seven partners join.", K_PARTNERS, 7},
    {"level_20", "Seasoned", "Reach level 20.", K_LEVEL, 20},
    {"coins_999", "Rich Uncle", "Carry 999 coins.", K_COINS, 999},
};
#define COUNT ((int)(sizeof(kList) / sizeof(kList[0])))

static char sEarned[COUNT][11]; /* "YYYY-MM-DD", or "" */
static char sPath[1024];        /* the file's record, "" with no save file */
static char sFile[64];          /* for the page: "File 1 (Mario)" */
static char sName[32];          /* the player's name the record is for */
static int sFirstCheck;         /* the first check after a load: quiet */
static int* sOn;

#define TOASTS 8
static int sToasts[TOASTS];
static double sToastTimes[TOASTS];
static int sToastCount;

/* --- counting ---------------------------------------------------------------- */

static int countNamed(const char* part, int lo, int hi, int* total) {
    int i, n = 0;
    *total = 0;
    for (i = 0; i < portGswfNamesCount; i++) {
        const PortNamedValue* v = &portGswfNames[i];
        if (v->value >= lo && v->value <= hi && strstr(v->name, part) != NULL) {
            (*total)++;
            n += swGet(v->value) ? 1 : 0;
        }
    }
    return n;
}

/* the Journal's own counts (win_log.c) */
static int countRecipes(int* total) {
    int i, n = 0;
    *total = 0;
    for (i = WIN_RECIPE_MIN; i < ITEM_MAX; i++) {
        if (itemDataTable[i].type_sort_order != -1) {
            (*total)++;
            n += swGet(WIN_RECIPE_COOKED_SW(i - WIN_RECIPE_MIN)) ? 1 : 0;
        }
    }
    return n;
}

static int countBadges(int* total) {
    int i, n = 0;
    *total = 0;
    for (i = BADGE_MIN; i < 0x153; i++) {
        if (itemDataTable[i].type_sort_order != -1) {
            (*total)++;
            n += swGet(i - BADGE_MIN + GSWF_BADGE_FOUND_BASE) ? 1 : 0;
        }
    }
    return n;
}

static int countTattles(int* total) {
    int i, n = 0;
    *total = 0;
    for (i = 1; i < 0xD8; i++) {
        if (battleGetUnitMonosiriPtr(i)->tattleLogTag == NULL || i == UNIT_BELDAM_CHAPTER2 ||
            i == UNIT_MARILYN_CHAPTER2 || i == UNIT_DOOPLISS_CHAPTER4_FIGHT1 || i == UNIT_BOWSER_CHAPTER3) {
            continue;
        }
        (*total)++;
        n += swGet(i + GSWF_TATTLE_BASE) ? 1 : 0;
    }
    return n;
}

static int progress(const Achievement* a, int* goal) {
    PouchData* pouch = pouchGetPtr();
    int total = 0, n = 0, i;
    switch (a->kind) {
    case K_STORY:
        *goal = 1;
        return swByteGet(GSW_STORY_PROGRESS) >= a->a;
    case K_FLAG:
        *goal = 1;
        return swGet(a->a) ? 1 : 0;
    case K_STAR_PIECES:
        n = countNamed("STAR_PIECE", GSWF_GOR00_STAR_PIECE, GSWF_GOR00_STAR_PIECE + 99, &total);
        break;
    case K_SHINES:
        n = countNamed("SHINE_SPRITE", 0, 0x7FFFFFFF, &total);
        break;
    case K_RECIPES:
        n = countRecipes(&total);
        break;
    case K_BADGES:
        n = countBadges(&total);
        break;
    case K_TATTLES:
        n = countTattles(&total);
        break;
    case K_TROUBLES:
        total = a->a;
        for (i = 1; i <= total; i++) {
            n += swGet(GSWF_TROUBLE_SOLVED_BASE + i) ? 1 : 0;
        }
        break;
    case K_PARTNERS:
        total = a->a;
        for (i = PARTNER_GOOMBELLA; i <= PARTNER_MSMOWZ; i++) {
            n += (pouch->partyData[i].flags & POUCH_PARTY_FLAG_JOINED) ? 1 : 0;
        }
        break;
    case K_LEVEL:
        *goal = a->a;
        return pouch->level;
    case K_COINS:
        *goal = a->a;
        return pouch->coins;
    }
    *goal = a->a != 0 && a->a < total ? a->a : total;
    return n;
}

/* --- the record -------------------------------------------------------------- */

static void save(void) {
    char dir[1024];
    FILE* f;
    int i;
    if (sPath[0] == '\0') {
        return;
    }
    snprintf(dir, sizeof(dir), "%s/Achievements", portPracticeUserDir());
    makeDir(dir);
    snprintf(dir, sizeof(dir), "%s/Achievements/%s", portPracticeUserDir(), PORT_GAME_ID);
    makeDir(dir);
    f = fopen(sPath, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "name=%s\n", sName);
    for (i = 0; i < COUNT; i++) {
        if (sEarned[i][0] != '\0') {
            fprintf(f, "%s %s\n", kList[i].id, sEarned[i]);
        }
    }
    fclose(f);
}

void portAchievementsFileLoaded(void) {
    char line[128];
    FILE* f;
    int i, sameName = 0;
    if (gp == NULL || gp->lastSlot < 0 || gp->lastSlot > 3) {
        sPath[0] = '\0';
        return;
    }
    memset(sEarned, 0, sizeof(sEarned));
    snprintf(sName, sizeof(sName), "%.16s", (const char*)gp->playerName);
    snprintf(sPath, sizeof(sPath), "%s/Achievements/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID,
             (int)gp->lastSlot + 1);
    snprintf(sFile, sizeof(sFile), "File %d (%s)", (int)gp->lastSlot + 1, sName);
    f = fopen(sPath, "r");
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            char id[64], date[16];
            line[strcspn(line, "\r\n")] = '\0';
            if (strncmp(line, "name=", 5) == 0) {
                sameName = strcmp(line + 5, sName) == 0;
                continue;
            }
            if (!sameName || sscanf(line, "%63s %15s", id, date) != 2) {
                continue; /* another game begun in the file: its record starts again */
            }
            for (i = 0; i < COUNT; i++) {
                if (strcmp(kList[i].id, id) == 0) {
                    snprintf(sEarned[i], sizeof(sEarned[i]), "%s", date);
                }
            }
        }
        fclose(f);
    }
    sFirstCheck = 1;
    {
        /* $TTYD_ACHIEVEMENT_TOAST=<n> (tests): achievement n's toast shown as the file loads */
        const char* test = getenv("TTYD_ACHIEVEMENT_TOAST");
        if (test != NULL && atoi(test) >= 0 && atoi(test) < COUNT && sToastCount < TOASTS) {
            sToasts[sToastCount] = atoi(test);
            sToastTimes[sToastCount++] = 0.0;
        }
    }
}

void portAchievementsFrame(void) {
    static unsigned int sTick;
    int i, changed = 0;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("achievements", 0, 0, 1);
    }
    if (!*sOn || sPath[0] == '\0' || gp == NULL || portPracticeTainted() || ++sTick % 30 != 0) {
        return;
    }
    if ((seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_BATTLE) || pouchGetPtr() == NULL) {
        return;
    }
    for (i = 0; i < COUNT; i++) {
        int goal = 1;
        const int done = progress(&kList[i], &goal) >= goal && goal > 0;
        if (done && sEarned[i][0] == '\0') {
            const time_t t = time(NULL);
            struct tm tm;
#if defined(_WIN32)
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            strftime(sEarned[i], sizeof(sEarned[i]), "%Y-%m-%d", &tm);
            changed = 1;
            if (!sFirstCheck && sToastCount < TOASTS) {
                sToasts[sToastCount] = i;
                sToastTimes[sToastCount++] = 0.0; /* timed from when it shows */
            }
            printf("[achievement] %s%s\n", kList[i].name, sFirstCheck ? " (already done)" : "");
            fflush(stdout);
        } else if (sFirstCheck && !done) {
            printf("[achievement] %s: %d of %d\n", kList[i].name, progress(&kList[i], &goal), goal);
            fflush(stdout);
        }
    }
    sFirstCheck = 0;
    if (changed) {
        save();
    }
}

/* --- for the page ------------------------------------------------------------ */

int portAchievementCount(void) {
    return COUNT;
}

const char* portAchievementName(int i) {
    return i >= 0 && i < COUNT ? kList[i].name : "";
}

const char* portAchievementNote(int i) {
    return i >= 0 && i < COUNT ? kList[i].note : "";
}

int portAchievementProgress(int i, int* goal) {
    *goal = 0;
    if (i < 0 || i >= COUNT || !portPracticeInGame() || pouchGetPtr() == NULL) {
        return 0;
    }
    return progress(&kList[i], goal);
}

const char* portAchievementEarned(int i) {
    return i >= 0 && i < COUNT && sPath[0] != '\0' && sEarned[i][0] != '\0' ? sEarned[i] : NULL;
}

const char* portAchievementFile(void) {
    return sPath[0] != '\0' ? sFile : "";
}

int portAchievementToast(double* age) {
    const double now = portHostSeconds();
    while (sToastCount > 0) {
        if (sToastTimes[0] == 0.0) {
            if (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_BATTLE) {
                return -1; /* shown once the room or battle is up, not over a load */
            }
            sToastTimes[0] = now;
        }
        *age = now - sToastTimes[0];
        if (*age < 5.0) {
            return sToasts[0];
        }
        memmove(sToasts, sToasts + 1, sizeof(int) * (TOASTS - 1));
        memmove(sToastTimes, sToastTimes + 1, sizeof(double) * (TOASTS - 1));
        sToastCount--;
    }
    return -1;
}
