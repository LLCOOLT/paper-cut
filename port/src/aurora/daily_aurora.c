/* The daily seed's game side (port/include/port/party.h, docs/MODES.md): a
 * daily file is one of Paper Cut's open-world files stamped with the day's
 * seed ("DAILY-YYYYMMDD", port/settings/features/daily.cpp puts it in the
 * randomizer's settings). While one is in play its frames are counted (every
 * game frame, menus too: the clock a race would keep) and kept in
 * <user>/daily_YYYYMMDD.txt, so a restart or a load goes on from there; the
 * goal is portDailyGoalStars() Crystal Stars of any of the open world's
 * chapters (it follows PORT_OPEN_STAGE), and the frame it's reached is
 * kept as the result. Only reads the game. */
#include "port/party.h"
#include "port/open_world.h"
#include "port/settings.h"
#include "driver/seqdrv.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int sDate;            /* the file whose clock is loaded */
static unsigned sFrames, sDone;
static unsigned sUnsaved;

int portDailyToday(void) {
    const time_t now = time(NULL);
    struct tm t;
#ifdef _WIN32
    gmtime_s(&t, &now);
#else
    gmtime_r(&now, &t);
#endif
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

int portDailyFileDate(void) {
    const char* seed;
    int date;
    if (gp == NULL || !portOpenOn()) {
        return 0;
    }
    seed = portOpenFileSeed();
    if (strncmp(seed, "DAILY-", 6) != 0 || strlen(seed) != 14) {
        return 0;
    }
    date = atoi(seed + 6);
    return date >= 20260101 && date <= 99991231 ? date : 0;
}

static void path(char* out, size_t size, int date) {
    snprintf(out, size, "%s/daily_%08d.txt", portSettingsUserPath(), date);
}

static void save(void) {
    char p[1024];
    FILE* f;
    if (sDate == 0) {
        return;
    }
    path(p, sizeof(p), sDate);
    f = fopen(p, "w");
    if (f != NULL) {
        fprintf(f, "frames %u\ndone %u\n", sFrames, sDone);
        fclose(f);
    }
    sUnsaved = 0;
}

static void load(int date) {
    char p[1024];
    FILE* f;
    if (sDate == date) {
        return;
    }
    if (sUnsaved) {
        save();
    }
    sDate = date;
    sFrames = sDone = 0;
    path(p, sizeof(p), date);
    f = fopen(p, "r");
    if (f != NULL) {
        if (fscanf(f, "frames %u done %u", &sFrames, &sDone) != 2) {
            sFrames = sDone = 0;
        }
        fclose(f);
    }
}

unsigned portDailyFrames(void) {
    return portDailyFileDate() != 0 && portDailyFileDate() == sDate ? sFrames : 0;
}

unsigned portDailyDoneFrames(void) {
    return portDailyFileDate() != 0 && portDailyFileDate() == sDate ? sDone : 0;
}

static int hasKey(int item) {
    const PouchData* p = pouchGetPtr();
    int i;
    for (i = 0; i < MAX_KEY_ITEMS; i++) {
        if (p->keyItems[i] == item) {
            return 1;
        }
    }
    return 0;
}

/* the Crystal Stars this build's open world has: its stage's chapters (stage
 * 1: chapters 1 and 2, 2: 1 to 5, 3 on: all 8), chapter 8 giving none */
int portDailyStageStars(void) {
    return PORT_OPEN_STAGE >= 3 ? 7 : PORT_OPEN_STAGE == 2 ? 5 : 2;
}

int portDailyGoalStars(void) {
    return portDailyStageStars() < 3 ? portDailyStageStars() : 3;
}

/* the Crystal Stars Mario holds, of any chapter */
static int starsHeld(void) {
    int item, n = 0;
    for (item = ITEM_DIAMOND_STAR; item <= ITEM_CRYSTAL_STAR; item++) {
        n += hasKey(item);
    }
    return n;
}

void portDailyFrame(void) {
    const SequenceType seq = seqGetSeq();
    int date;
    if (seq != SEQ_GAME && seq != SEQ_BATTLE && seq != SEQ_MAP_CHANGE && seq != SEQ_GAME_OVER) {
        if (sUnsaved) {
            save();
        }
        return;
    }
    date = portDailyFileDate();
    if (date == 0) {
        return;
    }
    load(date);
    if (sDone != 0) {
        return;
    }
    sFrames++;
    if (++sUnsaved >= 600) {
        save();
    }
    if ((sFrames & 15) == 0 && starsHeld() >= portDailyGoalStars()) {
        sDone = sFrames;
        save();
        printf("[daily] %08d done in %u frames\n", date, sDone);
        fflush(stdout);
    }
}
