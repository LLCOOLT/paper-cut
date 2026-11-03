/* Practice and speedrun tools, the game's side (port/include/port/practice.h):
 * the timer and its splits, LiveSplit's commands, frame advance and the RNG.
 * Off until a setting says otherwise; nothing here changes what the game does
 * except the RNG seeding the player asks for. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mariost.h"
#include "port/platform.h"
#include "port/practice.h"
#include "port/settings.h"

extern SequenceWork seqWork;

/* --- settings ----------------------------------------------------------- */

static int* sTimerCorner;  /* speedrun_timer: 0 off, 1-4 a corner (TL TR BL BR) */
static int* sLoadRemoval;  /* timer_load_removal */
static int* sAutoStart;    /* timer_autostart: start on a new file, reset at the title */
static int* sCategory;     /* split_category */
static int* sLiveSplit;    /* livesplit */
static int* sLiveSplitPort; /* livesplit_port */
static int* sRngFixed;     /* rng_fixed: rand() set to rng_seed at each room and battle */
static int* sRngSeed;

#define CATEGORY_ANY 0
#define CATEGORY_STEPS 1
#define CATEGORY_CUSTOM 2
#define CATEGORY_COUNT 3

void portPracticeDeclare(void) {
    if (sTimerCorner != NULL) {
        return;
    }
    sTimerCorner = portSettingsDeclare("speedrun_timer", 0, 0, 4);
    sLoadRemoval = portSettingsDeclare("timer_load_removal", 1, 0, 1);
    sAutoStart = portSettingsDeclare("timer_autostart", 1, 0, 1);
    sCategory = portSettingsDeclare("split_category", 0, 0, CATEGORY_COUNT - 1);
    sLiveSplit = portSettingsDeclare("livesplit", 0, 0, 1);
    sLiveSplitPort = portSettingsDeclare("livesplit_port", 16834, 1, 65535);
    sRngFixed = portSettingsDeclare("rng_fixed", 0, 0, 1);
    sRngSeed = portSettingsDeclare("rng_seed", 1, 0, 0x7FFFFFFF);
    portSettingsDeclare("frame_advance", 0, 0, 1);
    portSettingsDeclare("memory_watch", 0, 0, 4);
    portSettingsDeclare("achievements", 0, 0, 1);
}

const char* portPracticeUserDir(void) {
#ifdef TTYD_BACKEND_AURORA
    return portSettingsUserPath();
#else
    const char* dir = getenv("TTYD_USER_DIR");
    return dir != NULL && dir[0] != '\0' ? dir : ".";
#endif
}

const PortNamedValue* portStoryName(int value) {
    int lo = 0, hi = portStoryNamesCount - 1;
    while (lo <= hi) { /* the table is sorted by value */
        const int mid = (lo + hi) / 2;
        if (portStoryNames[mid].value == value) {
            return &portStoryNames[mid];
        }
        if (portStoryNames[mid].value < value) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}

/* --- splits --------------------------------------------------------------- */

typedef struct Split {
    int story; /* split when GSW 0 reaches it */
    char name[48];
    double time; /* game time it was made at, <0 not yet */
} Split;

#define SPLIT_MAX 512
static Split sSplits[SPLIT_MAX];
static int sSplitCount, sNext;
static int sLoadedCategory = -1;
static int sLastStory = -1; /* "Every story step": the value split at last */

/* Any%: each chapter ends when the next one's title card comes */
static const struct {
    int story;
    const char* name;
} kAnyPercent[] = {
    {STORY_STG1_00_PETAL_MEADOWS, "Prologue"},
    {STORY_STG2_00_BOGGLY_WOODS, "Chapter 1"},
    {STORY_STG3_00_WELCOME_TO_GLITZVILLE, "Chapter 2"},
    {STORY_STG4_00_WELCOME_TO_STAGE_4, "Chapter 3"},
    {STORY_STG5_00_LOGBOOK, "Chapter 4"},
    {STORY_STG6_00_START, "Chapter 5"},
    {STORY_STG7_00_START, "Chapter 6"},
    {STORY_STG8_00_START, "Chapter 7"},
    {STORY_STG8_END_AFTER_THE_BATTLE, "Shadow Queen"},
};

static const char* const kCategoryNames[CATEGORY_COUNT] = {"Any%", "Every story step", "Custom (splits.txt)"};

int portSplitCategoryCount(void) {
    return CATEGORY_COUNT;
}

const char* portSplitCategoryName(int i) {
    return i >= 0 && i < CATEGORY_COUNT ? kCategoryNames[i] : "";
}

static void addSplit(int story, const char* name) {
    if (sSplitCount < SPLIT_MAX) {
        Split* s = &sSplits[sSplitCount++];
        s->story = story;
        snprintf(s->name, sizeof(s->name), "%s", name);
        s->time = -1.0;
    }
}

/* splits.txt: a line per split, "<story value or STORY_ name> [its name]";
 * '#' starts a comment */
static void readCustom(void) {
    char path[1024], line[256];
    FILE* f;
    snprintf(path, sizeof(path), "%s/splits.txt", portPracticeUserDir());
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char* p = line;
        char word[96];
        int story = -1, i, n = 0;
        char* hash = strchr(p, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && n < (int)sizeof(word) - 1) {
            word[n++] = *p++;
        }
        word[n] = '\0';
        if (n == 0) {
            continue;
        }
        if (word[0] >= '0' && word[0] <= '9') {
            story = atoi(word);
        } else {
            for (i = 0; i < portStoryNamesCount; i++) {
                if (strcmp(portStoryNames[i].name, word) == 0) {
                    story = portStoryNames[i].value;
                    break;
                }
            }
        }
        if (story < 0) {
            continue;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        p[strcspn(p, "\r\n")] = '\0';
        if (*p == '\0') {
            const PortNamedValue* v = portStoryName(story);
            p = v != NULL && v->note[0] != '\0' ? (char*)v->note : word;
        }
        addSplit(story, p);
    }
    fclose(f);
}

void portSplitsReload(void) {
    size_t i;
    portPracticeDeclare();
    sSplitCount = 0;
    sNext = 0;
    sLoadedCategory = *sCategory;
    switch (*sCategory) {
    case CATEGORY_ANY:
        for (i = 0; i < sizeof(kAnyPercent) / sizeof(kAnyPercent[0]); i++) {
            addSplit(kAnyPercent[i].story, kAnyPercent[i].name);
        }
        break;
    case CATEGORY_CUSTOM:
        readCustom();
        break;
    default: /* every step: made as the story moves */
        break;
    }
}

int portSplitCount(void) {
    return sSplitCount;
}

const char* portSplitName(int i) {
    return i >= 0 && i < sSplitCount ? sSplits[i].name : "";
}

double portSplitTime(int i) {
    return i >= 0 && i < sSplitCount ? sSplits[i].time : -1.0;
}

int portSplitNext(void) {
    return sNext;
}

/* --- the timer ------------------------------------------------------------ */

static int sState;
static unsigned long long sFrames; /* game frames counted */
static double sRealStart, sRealEnd;
static int sLoading;
static double sLastSync;

int portTimerState(void) {
    return sState;
}

double portTimerGameSeconds(void) {
    return (double)sFrames / (double)portFieldRate();
}

double portTimerRealSeconds(void) {
    if (sState == PORT_TIMER_IDLE) {
        return 0.0;
    }
    return (sState == PORT_TIMER_DONE ? sRealEnd : portHostSeconds()) - sRealStart;
}

int portTimerLoading(void) {
    return sLoading;
}

void portTimerFormat(double seconds, char* out, int size) {
    const long long cs = seconds > 0.0 ? (long long)(seconds * 100.0) : 0;
    const int h = (int)(cs / 360000), m = (int)(cs / 6000 % 60), s = (int)(cs / 100 % 60), c = (int)(cs % 100);
    if (h > 0) {
        snprintf(out, (size_t)size, "%d:%02d:%02d.%02d", h, m, s, c);
    } else {
        snprintf(out, (size_t)size, "%d:%02d.%02d", m, s, c);
    }
}

/* LiveSplit's game time (its TimeSpan text: h:mm:ss.ff) */
static void syncGameTime(void) {
    char cmd[64];
    const long long cs = (long long)(portTimerGameSeconds() * 100.0);
    snprintf(cmd, sizeof(cmd), "setgametime %d:%02d:%02d.%02d", (int)(cs / 360000), (int)(cs / 6000 % 60),
             (int)(cs / 100 % 60), (int)(cs % 100));
    portLiveSplitSend(cmd);
    sLastSync = portHostSeconds();
}

void portTimerReset(void) {
    int i;
    if (sState != PORT_TIMER_IDLE) {
        portLiveSplitSend("reset");
    }
    sState = PORT_TIMER_IDLE;
    sFrames = 0;
    sNext = 0;
    for (i = 0; i < sSplitCount; i++) {
        sSplits[i].time = -1.0;
    }
    if (sLoadedCategory == CATEGORY_STEPS) {
        sSplitCount = 0;
    }
}

void portTimerStart(void) {
    portPracticeDeclare();
    if (sLoadedCategory != *sCategory || *sCategory == CATEGORY_CUSTOM) {
        portSplitsReload(); /* splits.txt read at each start: edits apply to the next run */
    }
    portTimerReset();
    sState = PORT_TIMER_RUNNING;
    sRealStart = portHostSeconds();
    sLastStory = gp != NULL ? swByteGet(GSW_STORY_PROGRESS) : -1;
    portLiveSplitSend("initgametime");
    portLiveSplitSend("starttimer");
    if (*sLoadRemoval) {
        syncGameTime();
    }
}

static void finish(void) {
    sState = PORT_TIMER_DONE;
    sRealEnd = portHostSeconds();
}

void portTimerSplit(void) {
    if (sState != PORT_TIMER_RUNNING) {
        return;
    }
    if (*sLoadRemoval) {
        syncGameTime();
    }
    portLiveSplitSend("split");
    if (sLoadedCategory == CATEGORY_STEPS) {
        return; /* split by hand: no row of its own */
    }
    if (sNext < sSplitCount) {
        sSplits[sNext++].time = portTimerGameSeconds();
    }
    if (sNext >= sSplitCount && sSplitCount > 0) {
        finish();
    }
}

/* the story moved: the splits it reached */
static void checkSplits(int story) {
    if (sLoadedCategory == CATEGORY_STEPS) {
        if (story != sLastStory && story > 0) {
            const PortNamedValue* v = portStoryName(story);
            char name[48];
            snprintf(name, sizeof(name), "%d %s", story, v != NULL ? v->note : "");
            if (sSplitCount == SPLIT_MAX) { /* the oldest out */
                memmove(sSplits, sSplits + 1, sizeof(Split) * (SPLIT_MAX - 1));
                sSplitCount--;
            }
            addSplit(story, name);
            sSplits[sSplitCount - 1].time = portTimerGameSeconds();
            sNext = sSplitCount;
            if (*sLoadRemoval) {
                syncGameTime();
            }
            portLiveSplitSend("split");
            if (story >= STORY_STG8_END_AFTER_THE_BATTLE) {
                finish();
            }
        }
        sLastStory = story;
        return;
    }
    while (sState == PORT_TIMER_RUNNING && sNext < sSplitCount && story >= sSplits[sNext].story) {
        sSplits[sNext++].time = portTimerGameSeconds();
        if (*sLoadRemoval) {
            syncGameTime();
        }
        portLiveSplitSend("split");
        if (sNext >= sSplitCount) {
            finish();
        }
    }
}

/* a load: a map change, or a battle's set-up (seq_battle states 1-4: the
 * curtain, the audience's textures, the stage, battle_init) */
static int loadingNow(SequenceType seq) {
    return seq == SEQ_MAP_CHANGE || (seq == SEQ_BATTLE && seqWork.state >= 1 && seqWork.state <= 4);
}

/* --- frame advance and the RNG -------------------------------------------- */

static int sHold, sSteps;

void portFrameAdvanceHold(int hold) {
    sHold = hold;
    sSteps = 0;
}

int portFrameAdvanceHeld(void) {
    return sHold;
}

void portFrameAdvanceStep(void) {
    if (!sHold) {
        sHold = 1;
    } else {
        sSteps++;
    }
}

int portFrameAdvanceWait(void) {
    if (!sHold) {
        return 0;
    }
    if (sSteps > 0) {
        sSteps--;
        return 0;
    }
    return 1;
}

unsigned int portPracticeRand(void) {
    return portRandState();
}

void portPracticeSetRand(unsigned int state) {
    portRandSetState(state);
}

static int sTainted;

void portPracticeTaint(void) {
    sTainted = 1;
}

int portPracticeTainted(void) {
    return sTainted;
}

void portPracticeFileLoaded(void) {
    sTainted = 0; /* its achievements, without the practice tools' changes */
    portAchievementsFileLoaded();
}

/* --- each frame ----------------------------------------------------------- */

void portPracticeFrame(void) {
    static SequenceType sPrevSeq = SEQ_UNDEFINED;
    static int sPrevBattleState;
    const SequenceType seq = seqGetSeq();
    const int story = gp != NULL ? swByteGet(GSW_STORY_PROGRESS) : 0;
    int loading;
    portPracticeDeclare();
    portLiveSplitTick(*sLiveSplit, *sLiveSplitPort);
    if (sPrevSeq == SEQ_LOAD && seq != SEQ_LOAD && seq != SEQ_TITLE) {
        portPracticeFileLoaded(); /* the file select's new game or continue */
    }
    /* the RNG fixed: set again as each room and battle starts */
    if (*sRngFixed) {
        const int battleStart = seq == SEQ_BATTLE && seqWork.state == 5 && sPrevBattleState != 5;
        if ((sPrevSeq == SEQ_MAP_CHANGE && seq == SEQ_GAME) || battleStart) {
            portRandSetState((unsigned int)*sRngSeed);
        }
    }
    sPrevBattleState = seq == SEQ_BATTLE ? seqWork.state : -1;
    loading = loadingNow(seq);
    if (sState == PORT_TIMER_IDLE && sLoadedCategory != *sCategory) {
        portSplitsReload(); /* the page lists the category's splits before a run */
    }
    if (*sTimerCorner == 0 && !*sLiveSplit) {
        if (sState != PORT_TIMER_IDLE) {
            portTimerReset();
        }
    } else {
        if (*sAutoStart && sPrevSeq == SEQ_LOAD && seq != SEQ_LOAD && seq != SEQ_TITLE && story == 0) {
            portTimerStart(); /* a new file */
        } else if (*sAutoStart && sState == PORT_TIMER_RUNNING && (seq == SEQ_TITLE || seq == SEQ_LOGO)) {
            portTimerReset();
        }
        if (sState == PORT_TIMER_RUNNING) {
            if (*sLoadRemoval && loading != sLoading) {
                syncGameTime();
                portLiveSplitSend(loading ? "pausegametime" : "unpausegametime");
            }
            if (!(loading && *sLoadRemoval)) {
                sFrames++;
            }
            checkSplits(story);
            if (*sLoadRemoval && portHostSeconds() - sLastSync >= 1.0) {
                syncGameTime();
            }
        }
    }
    sLoading = loading;
    sPrevSeq = seq;
    portAchievementsFrame();
}
