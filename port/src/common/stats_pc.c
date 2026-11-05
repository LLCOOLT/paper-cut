/* Lifetime stats (port/include/port/stats.h): counted from the game's own
 * state as it plays, kept per save file beside the achievements and
 * stickers (<user>/Stats/<game id>/file<n>.txt) and for every file on this
 * device (<user>/Stats/totals.txt). Nothing here changes the game: it reads
 * Mario's motion and place, the pouch's coins, and each battle's numbers as
 * battlex_pc.c hands them over. Not in the save-state section (a state
 * loaded doesn't take counts back), and the files are this player's: not
 * counted in replays or puzzles, and an online guest counts only its device's
 * totals (the file played is the host's). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "port/battlex.h"
#include "port/online.h"
#include "port/practice.h"
#include "port/replay.h"
#include "port/stats.h"
#ifdef _WIN32
#include <direct.h>
#define makeDir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define makeDir(p) mkdir(p, 0755)
#endif

/* each counter's name in the files (PORT_STAT_* order) */
static const char* const kKeys[PORT_STAT_COUNT] = {
    "play_frames", "distance", "coins", "field_jumps", "spin_jumps", "field_hammers", "battles_won",
    "battles_fled", "battles_lost", "turns", "enemies", "star_points", "damage_dealt", "damage_taken",
    "best_hit", "superguards", "guards", "stylish", "jump_attacks", "hammer_attacks", "specials",
    "partner_moves", "items_used", "fp_spent", "appeals", "grade_d", "grade_c", "grade_b", "grade_a", "grade_s",
};

static PortStats sFile, sTotals;
static char sPath[1024]; /* this file's record, "" none */
static char sName[20];
static int sTotalsRead, sFileDirty, sTotalsDirty;
/* the field's last frame: Mario's motion and place, the coins (-1: start again) */
static int sPrevMotion = -1, sPrevCoins = -1, sPrevPlace;
static float sPrevX, sPrevZ, sStep; /* (the step not yet a whole unit) */
/* a puzzle or the like being played (set by the Aurora side; NULL none): not the file's */
int (*gPortStatsSkip)(void);

static int isBest(int key) {
    return key == PORT_STAT_BEST_HIT;
}

/* --- the files --- */

static void totalsPath(char* out, size_t size) {
    snprintf(out, size, "%s/Stats/totals.txt", portPracticeUserDir());
}

static void readInto(PortStats* s, FILE* f, const char* name) {
    char line[160];
    int sameName = name == NULL;
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[40];
        long long value;
        int k, n;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        if (strncmp(line, "name=", 5) == 0) {
            /* (another game begun in the slot since: its own record, from nothing) */
            sameName = name == NULL || strcmp(line + 5, name) == 0;
            continue;
        }
        if (!sameName) {
            break;
        }
        if (sscanf(line, "chapter %d %lld", &k, &value) == 2) {
            if (k >= 0 && k < PORT_STATS_CHAPTERS && value >= 0) {
                s->chapterFrames[k] = value;
            }
        } else if (sscanf(line, "enemy %d %d", &k, &n) == 2) {
            if (k > 0 && k < PORT_STATS_KINDS && n >= 0) {
                s->enemies[k] = (unsigned)n;
            }
        } else if (sscanf(line, "%39s %lld", key, &value) == 2 && value >= 0) {
            for (k = 0; k < PORT_STAT_COUNT; k++) {
                if (strcmp(key, kKeys[k]) == 0) {
                    s->v[k] = value;
                }
            }
        }
    }
}

static void writeOut(const PortStats* s, const char* path, const char* name) {
    FILE* f = fopen(path, "w");
    int k;
    if (f == NULL) {
        return;
    }
    fprintf(f, "# Paper Cut lifetime stats (the Journal's Records; docs/BATTLE_EXTRAS.md)\n");
    if (name != NULL) {
        fprintf(f, "name=%s\n", name);
    }
    for (k = 0; k < PORT_STAT_COUNT; k++) {
        fprintf(f, "%s %lld\n", kKeys[k], s->v[k]);
    }
    for (k = 0; k < PORT_STATS_CHAPTERS; k++) {
        if (s->chapterFrames[k] > 0) {
            fprintf(f, "chapter %d %lld\n", k, s->chapterFrames[k]);
        }
    }
    for (k = 1; k < PORT_STATS_KINDS; k++) {
        if (s->enemies[k] > 0) {
            fprintf(f, "enemy %d %u\n", k, s->enemies[k]);
        }
    }
    fclose(f);
}

static void readTotals(void) {
    char path[1024];
    FILE* f;
    if (sTotalsRead) {
        return;
    }
    sTotalsRead = 1;
    totalsPath(path, sizeof(path));
    if ((f = fopen(path, "r")) != NULL) {
        readInto(&sTotals, f, NULL);
        fclose(f);
    }
}

static void makeDirs(void) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/Stats", portPracticeUserDir());
    makeDir(dir);
    snprintf(dir, sizeof(dir), "%s/Stats/%s", portPracticeUserDir(), PORT_GAME_ID);
    makeDir(dir);
}

void portStatsSave(void) {
    if (sFileDirty && sPath[0] != '\0') {
        makeDirs();
        writeOut(&sFile, sPath, sName);
    }
    if (sTotalsDirty) {
        char path[1024];
        makeDirs();
        totalsPath(path, sizeof(path));
        writeOut(&sTotals, path, NULL);
    }
    sFileDirty = 0;
    sTotalsDirty = 0;
}

void portStatsFileLoaded(void) {
    FILE* f;
    portStatsSave(); /* the file before's */
    readTotals();
    memset(&sFile, 0, sizeof(sFile));
    sPath[0] = '\0';
    sPrevMotion = -1;
    sPrevCoins = -1;
    sPrevPlace = 0;
    if (gp == NULL || gp->lastSlot < 0 || gp->lastSlot > 3) {
        return;
    }
    snprintf(sName, sizeof(sName), "%.16s", (const char*)gp->playerName);
    snprintf(sPath, sizeof(sPath), "%s/Stats/%s/file%d.txt", portPracticeUserDir(), PORT_GAME_ID,
             (int)gp->lastSlot + 1);
    /* a new game (the story not begun) starts its own record; a file continued, its own again */
    if (swByteGet(GSW_STORY_PROGRESS) == 0) {
        sFileDirty = 1;
        return;
    }
    if ((f = fopen(sPath, "r")) != NULL) {
        readInto(&sFile, f, sName);
        fclose(f);
    }
}

/* --- counting --- */

/* what's counted now: this file's (1), the device's (2) */
static int counting(void) {
    int which = 0;
    if (gp == NULL || portReplayActive() || portOnlineWatching() || (gPortStatsSkip != NULL && gPortStatsSkip())) {
        return 0;
    }
    readTotals();
    which |= 2;
    if (sPath[0] != '\0' && (!portOnlineActive() || portOnlineIsHost())) {
        which |= 1;
    }
    return which;
}

static void add(int key, long long n) {
    const int which = counting();
    if (n <= 0 || which == 0) {
        return;
    }
    if (which & 1) {
        sFile.v[key] = isBest(key) ? (n > sFile.v[key] ? n : sFile.v[key]) : sFile.v[key] + n;
        sFileDirty = 1;
    }
    sTotals.v[key] = isBest(key) ? (n > sTotals.v[key] ? n : sTotals.v[key]) : sTotals.v[key] + n;
    sTotalsDirty = 1;
}

int portStatsChapter(int p) {
    static const int kStarts[PORT_STATS_CHAPTERS - 1] = {
        STORY_STG1_00_PETAL_MEADOWS, STORY_STG2_00_BOGGLY_WOODS, STORY_STG3_00_WELCOME_TO_GLITZVILLE,
        STORY_STG4_00_WELCOME_TO_STAGE_4, STORY_STG5_00_LOGBOOK, STORY_STG6_00_START, STORY_STG7_00_START,
        STORY_STG8_00_START, STORY_STG9_00_EPILOGUE,
    };
    int c = 0;
    while (c < PORT_STATS_CHAPTERS - 1 && p >= kStarts[c]) {
        c++;
    }
    return c;
}

void portStatsEnemyDefeated(int kind) {
    const int which = counting();
    if (which == 0 || kind <= 0 || kind >= PORT_STATS_KINDS) {
        return;
    }
    if (which & 1) {
        sFile.enemies[kind]++;
    }
    sTotals.enemies[kind]++;
    add(PORT_STAT_ENEMIES, 1);
}

void portStatsBattleOver(int result, const PortBattleRecap* recap, int jumps, int hammers, int specials,
                         int partnerMoves, int itemsUsed, int fpSpent, int appeals, int turns, int damageTaken) {
    if (result == 1) {
        add(PORT_STAT_BATTLES_WON, 1);
    } else if (result == 3) {
        add(PORT_STAT_BATTLES_LOST, 1);
    } else if (result == 4) {
        add(PORT_STAT_BATTLES_FLED, 1);
    } else {
        return; /* (interrupted: not a battle of the player's) */
    }
    add(PORT_STAT_TURNS, turns);
    add(PORT_STAT_JUMP_ATTACKS, jumps);
    add(PORT_STAT_HAMMER_ATTACKS, hammers);
    add(PORT_STAT_SPECIALS, specials);
    add(PORT_STAT_PARTNER_MOVES, partnerMoves);
    add(PORT_STAT_ITEMS_USED, itemsUsed);
    add(PORT_STAT_FP_SPENT, fpSpent);
    add(PORT_STAT_APPEALS, appeals);
    add(PORT_STAT_DAMAGE_TAKEN, damageTaken);
    if (recap != NULL) {
        add(PORT_STAT_DAMAGE_DEALT, recap->damageDealt);
        add(PORT_STAT_BEST_HIT, recap->bestHit);
        add(PORT_STAT_SUPERGUARDS, recap->superguards);
        add(PORT_STAT_GUARDS, recap->guards);
        add(PORT_STAT_STYLISH, recap->stylish);
        add(PORT_STAT_STAR_POINTS, recap->starPoints);
        if (recap->grade >= 0 && recap->grade < PORT_GRADE_COUNT) {
            add(PORT_STAT_GRADE_D + recap->grade, 1);
        }
    }
}

void portStatsFrame(void) {
    static unsigned sTick;
    static int sWasBattle;
    const SequenceType seq = seqGetSeq();
    const int playing = gp != NULL && (seq == SEQ_GAME || seq == SEQ_BATTLE) && pouchGetPtr() != NULL;
    const int which = playing ? counting() : 0;
    if (which == 0) {
        sPrevMotion = -1;
        sPrevCoins = -1;
        sPrevPlace = 0;
    } else {
        const int chapter = portStatsChapter(swByteGet(GSW_STORY_PROGRESS));
        const int coins = pouchGetPtr()->coins;
        add(PORT_STAT_PLAY_FRAMES, 1);
        if (which & 1) {
            sFile.chapterFrames[chapter]++;
        }
        sTotals.chapterFrames[chapter]++;
        /* coins gained (the battles' paid out in the field; the Bowser stages' are the pouch's too) */
        if (sPrevCoins >= 0 && coins > sPrevCoins) {
            add(PORT_STAT_COINS, coins - sPrevCoins);
        }
        sPrevCoins = coins;
        if (seq == SEQ_GAME && !gp->inBattle) {
            MarioWork* m = marioGetPtr();
            const int motion = (int)m->currMotionId;
            if (sPrevMotion >= 0 && motion != sPrevMotion) {
                if (motion == MARIO_MOTION_JUMP || motion == MARIO_MOTION_JUMP_STAND) {
                    add(PORT_STAT_FIELD_JUMPS, 1);
                } else if (motion == MARIO_MOTION_HIP) {
                    add(PORT_STAT_SPIN_JUMPS, 1);
                } else if (motion == MARIO_MOTION_HAMMER) {
                    add(PORT_STAT_FIELD_HAMMERS, 1);
                }
            }
            sPrevMotion = motion;
            /* the ground covered: a frame's step, not a warp or a room's change */
            if (sPrevPlace) {
                const float dx = m->position.x - sPrevX, dz = m->position.z - sPrevZ;
                const float d2 = dx * dx + dz * dz;
                if (d2 > 0.0001f && d2 < 30.0f * 30.0f) {
                    sStep += sqrtf(d2);
                    if (sStep >= 1.0f) {
                        add(PORT_STAT_DISTANCE, (long long)sStep);
                        sStep -= (float)(long long)sStep;
                    }
                }
            }
            sPrevX = m->position.x;
            sPrevZ = m->position.z;
            sPrevPlace = 1;
        } else {
            sPrevMotion = -1;
            sPrevPlace = 0;
        }
    }
    if (playing && !gp->inBattle && (sFileDirty || sTotalsDirty) && (++sTick % 600 == 0 || sWasBattle)) {
        portStatsSave(); /* every ten seconds or so while it changes, and as a battle ends; outside battles */
    }
    sWasBattle = playing && gp->inBattle;
}

const PortStats* portStatsFile(void) {
    return sPath[0] != '\0' ? &sFile : NULL;
}

const PortStats* portStatsTotals(void) {
    readTotals();
    return &sTotals;
}
