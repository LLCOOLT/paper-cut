/* The modes' full runs, the hooks for the modes env alone can't drive (tests, Aurora only): see
 * port/include/port/moderun.h's second list. Each does nothing unless its variable is set. */
#include "port/moderun.h"
#include "port/dojo.h"
#include "port/mode_leave.h"
#include "port/mode_lending.h"
#include "port/platform.h"
#include "port/playmodes.h"
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

/* the test's starts: as $TTYD_MODE_TEST's, this many game frames after the $TTYD_MAP warp settled */
#define START_AT 700

static int envInt(const char* name, int def) {
    const char* v = getenv(name);
    return v != NULL && *v ? atoi(v) : def;
}

static int idleField(void) {
    return seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && !seqCheckSeq();
}

/* a Leave as the Settings tab's button does it, said in the log */
static void leaveNow(const char* what) {
    const char* why = NULL;
    printf("[moderun] %s: leave asked (%s)\n", what, gp != NULL && gp->inBattle ? "in a battle" : "in the field");
    fflush(stdout);
    if (!portModeLeave(&why)) {
        printf("[moderun] leave refused: %s\n", why != NULL ? why : "?");
        fflush(stdout);
    }
}

/* --- the endless Glitz Pit --- */

int portModeRunPitMatch(int step, int rolled, int count) {
    static int sRule = -2, sRounds, sLeft;
    if (sRule == -2) {
        sRule = envInt("TTYD_MODERUN_PIT_RULE", -1);
        sRounds = envInt("TTYD_MODERUN_PIT_ROUNDS", 0);
        if (sRule >= count) {
            sRule = -1;
        }
    }
    if (sRounds > 0 && step > sRounds && !sLeft) {
        sLeft = 1;
        printf("[moderun] Endless Glitz Pit: %d rounds won\n", step - 1);
        fflush(stdout);
        leaveNow("Endless Glitz Pit");
    } else if (step <= 1) {
        sLeft = 0; /* (a new run) */
    }
    if (sRule >= 0) {
        printf("[moderun] Endless Glitz Pit round %d: condition %d (rolled %d)\n", step, sRule, rolled);
        fflush(stdout);
        return sRule;
    }
    return rolled;
}

/* --- the action-command dojo --- */

void portModeRunDojoFrame(void) {
    static int sState = -1, sFrames;
    static const char* sLastWhy;
    PortDojoSetup s = {0, PORT_DOJO_DUMMY_EVERY_TURN, 1, 1};
    const char* env;
    const char* why = NULL;
    if (sState < 0) {
        sState = getenv("TTYD_MODERUN_DOJO") != NULL ? 0 : 1;
    }
    if (sState != 0 || !portWarpSettled() || ++sFrames < START_AT) {
        return;
    }
    env = getenv("TTYD_MODERUN_DOJO");
    sscanf(env, "%d,%d,%d,%d", &s.dummy, &s.attacks, &s.partner, &s.everyMove);
    if (portDojoStart(&s, &why)) {
        sState = 1;
        printf("[moderun] dojo asked: dummy %d, attacks %d, partner %d, every move %d\n", s.dummy, s.attacks,
               s.partner, s.everyMove);
        fflush(stdout);
    } else if (why != sLastWhy) {
        printf("[moderun] dojo not started: %s\n", why != NULL ? why : "?");
        fflush(stdout);
    }
    sLastWhy = why;
}

/* --- the Pit of 100 Trials in co-op --- */

void portModeRunPitCoopFrame(void) {
    static int sFloors = -1, sFrames, sStarted, sLeft;
    static const char* sLastWhy;
    if (sFloors < 0) {
        sFloors = envInt("TTYD_MODERUN_PITCOOP", 0);
    }
    if (sFloors <= 0 || gp == NULL) {
        return;
    }
    if (!sStarted) {
        const char* why = NULL;
        if (!portWarpSettled() || ++sFrames < START_AT) {
            return;
        }
        if (portPitCoopStart(&why)) {
            sStarted = 1;
            printf("[moderun] Pit in co-op asked: %d floors, then leave\n", sFloors);
            fflush(stdout);
        } else if (why != sLastWhy) {
            printf("[moderun] Pit in co-op not started: %s\n", why != NULL ? why : "?");
            fflush(stdout);
        }
        sLastWhy = why;
        return;
    }
    /* the floors played: the next one reached and Mario free on it */
    if (!sLeft && portPitCoopActive() && portPitCoopFloor() > sFloors && idleField() && marioChkKey() &&
        marioStGetSystemLevel() == 0) {
        sLeft = 1;
        printf("[moderun] Pit in co-op: floor %d reached\n", portPitCoopFloor());
        fflush(stdout);
        leaveNow("Pit of 100 Trials in co-op");
    }
}

/* --- the raid boss --- */

int portModeRunRaidGuard(int defenceResult) {
    static int sOn = -1;
    if (sOn < 0) {
        sOn = envInt("TTYD_MODERUN_RAID_GUARD", 0) > 0;
    }
    return sOn && defenceResult == 4;
}

/* The raid isn't one of Leave's modes (port/include/port/mode_leave.h), so moderun_aurora.c's checks don't see
 * it: the same lines for it here ("[moderun] Raid boss started", "over after", "control back after", "check
 * after", "done"), stepping aside if it ever is one (portModeLeaveName then names it). */
typedef struct RunSnap {
    PouchData pouch;
    int story;
} RunSnap;

static void take(RunSnap* s) {
    memcpy(&s->pouch, pouchGetPtr(), sizeof(PouchData));
    s->story = swStoryPeek();
}

static void group(char* out, size_t cap, const char* name, int same) {
    const size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s%s %s", n ? ", " : "", name, same ? "same" : "changed");
}

static void check(const char* mode, const RunSnap* before) {
    const PouchData* a = &before->pouch;
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
           a->coins, b->coins, a->starPoints, b->starPoints, before->story == swStoryPeek() ? "same" : "changed",
           before->story, before->story == swStoryPeek() ? ", now " : " -> ", swStoryPeek(), more);
    fflush(stdout);
}

void portModeRunRaidFrame(int active) {
    static const char* const kName = "Raid boss";
    static int sOn = -1, sExitAfter, sExitIn = -1, sOldAge, sRuns, sRunning, sOver, sRunFrames, sOverFrames,
               sControlFrames;
    static RunSnap sNow, sOld, sBefore;
    static Uint64 sRunStart;
    if (sOn < 0) {
        sOn = envInt("TTYD_MODERUN", 0) > 0;
        sExitAfter = envInt("TTYD_MODERUN_EXIT", 0);
    }
    if (!sOn || gp == NULL) {
        return;
    }
    if (sExitIn >= 0 && sExitIn-- == 0) {
        printf("[moderun] done: %d run(s) checked\n", sRuns);
        fflush(stdout);
        portTestExit(0);
    }
    if (active && portModeLeaveName() != NULL && !sRunning) {
        return; /* (one of Leave's modes after all: moderun_aurora.c checks it) */
    }
    if (!active && !sRunning && !sOver && idleField() && portModeLending() == 0 && portModeLeaveName() == NULL) {
        if (++sOldAge >= 30) {
            sOld = sNow;
            sOldAge = 0;
        }
        take(&sNow);
        if (sRuns == 0 && sOld.story == 0 && sOld.pouch.level == 0) {
            sOld = sNow;
        }
    }
    if (active && !sRunning) {
        sRunning = 1;
        sRuns++;
        sRunFrames = 0;
        sRunStart = SDL_GetTicks();
        sBefore = sOld;
        sOver = 0;
        printf("[moderun] %s started (run %d; before it: level %d, HP %d, FP %d, BP %d, coins %d, story %d)\n",
               kName, sRuns, sBefore.pouch.level, sBefore.pouch.maximumHP, sBefore.pouch.maximumFP,
               sBefore.pouch.maximumBP, sBefore.pouch.coins, sBefore.story);
        fflush(stdout);
    }
    if (sRunning && active) {
        sRunFrames++;
    }
    if (sRunning && !active) {
        printf("[moderun] %s over after %d frames, %.1f s\n", kName, sRunFrames,
               (double)(SDL_GetTicks() - sRunStart) / 1000.0);
        fflush(stdout);
        sRunning = 0;
        sOver = 1;
        sOverFrames = 0;
        sControlFrames = 0;
    }
    if (sOver) {
        sOverFrames++;
        if (idleField() && marioChkKey() && marioStGetSystemLevel() == 0 && portModeLending() == 0) {
            sControlFrames++;
        } else {
            sControlFrames = 0;
        }
        if (sControlFrames >= 60) {
            printf("[moderun] control back after %s: %d frames, no menu, lending 0\n", kName, sOverFrames);
            check(kName, &sBefore);
            if (sExitAfter > 0) {
                sExitIn = sExitAfter;
            }
            sOver = 0;
            sOldAge = 0;
            take(&sNow);
            sOld = sNow;
        } else if (sOverFrames == 3600) {
            printf("[moderun] control not back after %s (map %s, seq %d, battle %d, menu %d, lending %d)\n", kName,
                   gp->currentMapName, seqGetSeq(), gp->inBattle, marioStGetSystemLevel(), portModeLending());
            check(kName, &sBefore);
            fflush(stdout);
            if (sExitAfter > 0) {
                portTestExit(6);
            }
        }
    }
}
