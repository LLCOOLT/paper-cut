/* The modes' full runs' checks (tests): port/include/port/moderun.h */
#include "port/moderun.h"
#include "port/mode_leave.h"
#include "port/modes.h"
#include "port/mode_lending.h"
#include "port/platform.h"
#include "port/toggles.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Snap {
    PouchData pouch;
    int story;
} Snap;

static int sOn = -1, sExitAfter, sEnds = 1, sLeaveAt, sLeaveRun;
static Snap sNow, sOld, sBefore; /* this frame's, 30 frames old, the run's own */
static int sOldAge;
static const char* sRunning; /* the mode running (its Leave name) */
static int sRuns, sRunFrames, sLeft;
static unsigned long long sRunStart;
static const char* sOver; /* over, waiting for control back */
static int sOverFrames, sControlFrames, sExitIn = -1;

static int envInt(const char* name, int def) {
    const char* v = getenv(name);
    return v != NULL && *v ? atoi(v) : def;
}

static void take(Snap* s) {
    memcpy(&s->pouch, pouchGetPtr(), sizeof(PouchData));
    s->story = swStoryPeek();
}

static int idleField(void) {
    return seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && !seqCheckSeq();
}

/* one group's verdict, appended to out */
static void group(char* out, size_t cap, const char* name, int same) {
    const size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s%s %s", n ? ", " : "", name, same ? "same" : "changed");
}

static void check(const char* mode) {
    const PouchData* a = &sBefore.pouch;
    const PouchData* b = pouchGetPtr();
    char out[1024] = "";
    char more[512] = "";
    int i, partners = 1;
    const int stats = a->level == b->level && a->baseMaxHP == b->baseMaxHP && a->baseMaxFP == b->baseMaxFP &&
                      a->maximumHP == b->maximumHP && a->maximumFP == b->maximumFP && a->maximumBP == b->maximumBP &&
                      a->maximumSP == b->maximumSP && a->starPowersObtained == b->starPowersObtained &&
                      a->jumpLevel == b->jumpLevel && a->hammerLevel == b->hammerLevel;
    for (i = 0; i < 8; i++) {
        const PouchPartyData* p = &a->partyData[i];
        const PouchPartyData* q = &b->partyData[i];
        if (p->flags != q->flags || p->baseMaxHP != q->baseMaxHP || p->maximumHP != q->maximumHP ||
            p->HPLevel != q->HPLevel || p->attackLevel != q->attackLevel || p->techLevel != q->techLevel) {
            partners = 0;
        }
    }
    group(out, sizeof(out), "stats", stats);
    group(out, sizeof(out), "partners", partners);
    group(out, sizeof(out), "badges",
          memcmp(a->badges, b->badges, sizeof(a->badges)) == 0 &&
              memcmp(a->equippedBadges, b->equippedBadges, sizeof(a->equippedBadges)) == 0);
    group(out, sizeof(out), "items",
          memcmp(a->heldItems, b->heldItems, sizeof(a->heldItems)) == 0 &&
              memcmp(a->storedItems, b->storedItems, sizeof(a->storedItems)) == 0);
    group(out, sizeof(out), "key items", memcmp(a->keyItems, b->keyItems, sizeof(a->keyItems)) == 0);
    if (!stats) {
        snprintf(more, sizeof(more), " (level %d -> %d, HP %d -> %d, FP %d -> %d, BP %d -> %d, powers %x -> %x)",
                 a->level, b->level, a->maximumHP, b->maximumHP, a->maximumFP, b->maximumFP, a->maximumBP,
                 b->maximumBP, a->starPowersObtained, b->starPowersObtained);
    }
    printf("[moderun] check after %s: %s, coins %d -> %d, star points %d -> %d, story %s (%d%s%d)%s\n", mode, out,
           a->coins, b->coins, a->starPoints, b->starPoints, sBefore.story == swStoryPeek() ? "same" : "changed",
           sBefore.story, sBefore.story == swStoryPeek() ? ", now " : " -> ", swStoryPeek(), more);
    fflush(stdout);
}

void portModeRunFrame(void) {
    const char* name;
    if (sOn < 0) {
        sOn = envInt("TTYD_MODERUN", 0) > 0;
        sExitAfter = envInt("TTYD_MODERUN_EXIT", 0);
        sEnds = envInt("TTYD_MODERUN_ENDS", 1);
        sLeaveAt = envInt("TTYD_MODERUN_LEAVE", 0);
        sLeaveRun = envInt("TTYD_MODERUN_LEAVE_RUN", 0);
    }
    if (!sOn || gp == NULL) {
        return;
    }
    if (sExitIn >= 0 && sExitIn-- == 0) {
        printf("[moderun] done: %d run(s) checked\n", sRuns);
        fflush(stdout);
        portTestExit(0);
    }
    name = portModeLeaveName();
    if (name == NULL && sRunning == NULL && sOver == NULL && idleField() && portModeLending() == 0) {
        /* the file as it is, the old snapshot 30 frames behind */
        if (++sOldAge >= 30) {
            sOld = sNow;
            sOldAge = 0;
        }
        take(&sNow);
        if (sRuns == 0 && sOld.story == 0 && sOld.pouch.level == 0) {
            sOld = sNow;
        }
    }
    if (name != NULL && sRunning == NULL) {
        sRunning = name;
        sRuns++;
        sRunFrames = 0;
        sLeft = 0;
        sRunStart = SDL_GetTicks();
        sBefore = sOld;
        sOver = NULL;
        printf("[moderun] %s started (run %d; before it: level %d, HP %d, FP %d, BP %d, coins %d, story %d)\n", name,
               sRuns, sBefore.pouch.level, sBefore.pouch.maximumHP, sBefore.pouch.maximumFP, sBefore.pouch.maximumBP,
               sBefore.pouch.coins, sBefore.story);
        fflush(stdout);
    }
    if (sRunning != NULL && name != NULL) {
        sRunFrames++;
        if (sLeaveAt > 0 && !sLeft && sRunFrames >= sLeaveAt && (sLeaveRun == 0 || sLeaveRun == sRuns) &&
            marioStGetSystemLevel() == 0) {
            const char* why = NULL;
            sLeft = 1;
            printf("[moderun] leave asked (%s)\n", gp->inBattle ? "in a battle" : "in the field");
            fflush(stdout);
            if (!portModeLeave(&why)) {
                printf("[moderun] leave refused: %s\n", why != NULL ? why : "?");
                fflush(stdout);
            }
        }
    }
    if (sRunning != NULL && name == NULL) {
        printf("[moderun] %s over after %d frames, %.1f s\n", sRunning, sRunFrames,
               (double)(SDL_GetTicks() - sRunStart) / 1000.0);
        fflush(stdout);
        sOver = sRunning;
        sRunning = NULL;
        sOverFrames = 0;
        sControlFrames = 0;
    }
    if (sOver != NULL) {
        sOverFrames++;
        if (idleField() && marioChkKey() && marioStGetSystemLevel() == 0 && portModeLending() == 0) {
            sControlFrames++;
        } else {
            sControlFrames = 0;
        }
        if (sControlFrames >= 60) {
            printf("[moderun] control back after %s: %d frames, no menu, lending 0\n", sOver, sOverFrames);
            check(sOver);
            if (sExitAfter > 0 && sRuns >= sEnds) {
                sExitIn = sExitAfter;
            }
            sOver = NULL;
            sOldAge = 0;
            take(&sNow);
            sOld = sNow;
        } else if (sOverFrames == 3600) {
            printf("[moderun] control not back after %s (map %s, seq %d, battle %d, menu %d, lending %d)\n", sOver,
                   gp->currentMapName, seqGetSeq(), gp->inBattle, marioStGetSystemLevel(), portModeLending());
            check(sOver);
            fflush(stdout);
            if (sExitAfter > 0) {
                portTestExit(6);
            }
        }
    }
}
