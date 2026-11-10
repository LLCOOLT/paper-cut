/* The party modes' full runs' checks (tests): port/include/port/moderun.h, its "party modes" part.
 *
 * The quiz, the Tattle-card duels and Bingo aren't modes Leave covers (they lend nothing), so
 * moderun_aurora.c's checks don't see them: here each one's run is watched from what the mode
 * already tells (portQuizActive, portCardsActive, the Bingo card's seed), with the same lines
 * moderun_aurora.c prints for a Leave mode ("[moderun] <mode> started / over after / control back
 * after / check after", "[moderun] done"), and TTYD_MODERUN_EXIT ends the run once the mode is over
 * and Mario has his control back. Two set-ups the modes can't be given from the environment:
 * the Tattle Log (a Westside duel needs five tattles) and a Bingo card made, then cleared once a
 * line is done (the settings page's New card and Clear); and a duel asked for once the $TTYD_MAP warp
 * has settled (TTYD_PARTY_GAME_TEST's asks on the first field frame, before a warp's map could be the
 * table's). Each is off unless its variable is set;
 * a frame, from party_aurora.c's frameHook (before the party games' own frames). */
#include "port/moderun.h"
#include "port/party.h"
#include "port/platform.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sOn = -1, sExitAfter, sEnds = 1, sExitIn = -1, sRuns;
static const char* sTattles; /* $TTYD_MODERUN_TATTLES */
static const char* sBingoSeed; /* $TTYD_MODERUN_BINGO */
static int sBingoClearAfter = 120, sDuel = -1, sDuelAsked;
static int sTattlesDone, sBingoMade, sBingoLineAt = -1, sBingoLines;
static const char* sRunning; /* the party game up (its name here) */
static const char* sOver;    /* over, waiting for control back */
static int sRunFrames, sOverFrames, sControlFrames;
static PouchData sBefore;
static int sBeforeStory, sCardWins[PORT_DUEL_NPCS];

static int envInt(const char* name, int def) {
    const char* v = getenv(name);
    return v != NULL && *v ? atoi(v) : def;
}

static int idleField(void) {
    return seqGetSeq() == SEQ_GAME && gp != NULL && !gp->inBattle && !seqCheckSeq();
}

/* the party game up now (NULL: none) */
static const char* upNow(void) {
    if (portQuizActive()) {
        return "Quiz";
    }
    if (portCardsActive()) {
        return "Tattle cards";
    }
    if (sBingoMade && portBingoSeed()[0] != '\0') {
        return "Bingo";
    }
    return NULL;
}

static void group(char* out, size_t cap, const char* name, int same) {
    const size_t n = strlen(out);
    snprintf(out + n, cap - n, "%s%s %s", n ? ", " : "", name, same ? "same" : "changed");
}

/* as moderun_aurora.c's check (the same line) */
static void check(const char* mode) {
    const PouchData* a = &sBefore;
    const PouchData* b = pouchGetPtr();
    char out[512] = "";
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
    printf("[moderun] check after %s: %s, coins %d -> %d, star points %d -> %d, story %s (%d%s%d)\n", mode, out,
           a->coins, b->coins, a->starPoints, b->starPoints, sBeforeStory == swStoryPeek() ? "same" : "changed",
           sBeforeStory, sBeforeStory == swStoryPeek() ? ", now " : " -> ", swStoryPeek());
    fflush(stdout);
}

/* the set-ups, once the field is up */
static void setUps(void) {
    if (!portWarpSettled()) {
        return; /* (the warp's new file would wipe what is set before it) */
    }
    if (sTattles != NULL && !sTattlesDone && gp != NULL && seqGetSeq() == SEQ_GAME) {
        /* $TTYD_MODERUN_TATTLES=all: every enemy the Tattle Log lists tattled (as the title's temporary file
           lends them); =<n>: the first n */
        const int most = strcmp(sTattles, "all") == 0 ? PORT_PARTY_UNITS : atoi(sTattles);
        int unit, n = 0;
        sTattlesDone = 1;
        for (unit = 1; unit < PORT_PARTY_UNITS && n < most; unit++) {
            if (portPartyEnemyListed(unit)) {
                swSet(unit + GSWF_TATTLE_BASE);
                n++;
            }
        }
        printf("[moderun] tattles: %d set\n", n);
        fflush(stdout);
    }
    if (sDuel >= 0 && !sDuelAsked && idleField() && marioChkKey() && marioStGetSystemLevel() == 0) {
        /* $TTYD_MODERUN_DUEL=<n>: a duel against the Westside's card player n (0 the rookie .. 2 the
           champion), as talking to them asks for one */
        const char* why = "";
        sDuelAsked = 1;
        if (!portCardsDuel(sDuel, &why)) {
            printf("[moderun] duel refused: %s\n", why);
            fflush(stdout);
        }
    }
    if (sBingoSeed != NULL && !sBingoMade && idleField() && marioChkKey() && marioStGetSystemLevel() == 0) {
        /* $TTYD_MODERUN_BINGO=<seed>: a new card, as the Bingo page's New card makes it */
        sBingoMade = 1;
        portBingoNew(sBingoSeed);
    }
    if (sRunning != NULL && strcmp(sRunning, "Bingo") == 0) {
        const int lines = portBingoLines();
        if (lines != sBingoLines) {
            printf("[moderun] Bingo: %d line(s), %d squares, after %d frames\n", lines, portBingoCount(), sRunFrames);
            fflush(stdout);
            sBingoLines = lines;
            if (lines > 0 && sBingoLineAt < 0) {
                sBingoLineAt = sRunFrames;
            }
        }
        if (sBingoLineAt >= 0 && sRunFrames >= sBingoLineAt + sBingoClearAfter) {
            /* the card cleared, as the page's Clear (its achievement counted meanwhile) */
            printf("[moderun] Bingo: the card cleared\n");
            fflush(stdout);
            portBingoClear();
        }
    }
}

void portModeRunPartyFrame(void) {
    const char* name;
    if (sOn < 0) {
        sOn = envInt("TTYD_MODERUN", 0) > 0;
        sExitAfter = envInt("TTYD_MODERUN_EXIT", 0);
        sEnds = envInt("TTYD_MODERUN_ENDS", 1);
        sTattles = getenv("TTYD_MODERUN_TATTLES");
        sTattles = sTattles != NULL && *sTattles ? sTattles : NULL;
        sBingoSeed = getenv("TTYD_MODERUN_BINGO");
        sBingoSeed = sBingoSeed != NULL && *sBingoSeed ? sBingoSeed : NULL;
        sBingoClearAfter = envInt("TTYD_MODERUN_BINGO_CLEAR", 120);
        sDuel = envInt("TTYD_MODERUN_DUEL", -1);
    }
    if (!sOn || gp == NULL) {
        return;
    }
    if (sExitIn >= 0 && sExitIn-- == 0) {
        printf("[moderun] done: %d party run(s) checked\n", sRuns);
        fflush(stdout);
        portTestExit(0);
    }
    if (sRunning == NULL && sOver == NULL) {
        memcpy(&sBefore, pouchGetPtr(), sizeof(PouchData)); /* (the file as it is before a game comes up) */
        sBeforeStory = swStoryPeek();
    }
    setUps();
    name = upNow();
    if (name != NULL && sRunning == NULL) {
        int i;
        sRunning = name;
        sRuns++;
        sRunFrames = 0;
        sOver = NULL;
        for (i = 0; i < PORT_DUEL_NPCS; i++) {
            sCardWins[i] = portCardsWins(i);
        }
        printf("[moderun] %s started (party run %d; before it: level %d, coins %d, story %d)\n", name, sRuns,
               sBefore.level, sBefore.coins, sBeforeStory);
        fflush(stdout);
    }
    if (sRunning != NULL && name != NULL) {
        sRunFrames++;
    }
    if (sRunning != NULL && name == NULL) {
        printf("[moderun] %s over after %d frames", sRunning, sRunFrames);
        if (strcmp(sRunning, "Quiz") == 0) {
            int score = 0;
            const int winner = portQuizLastWinner(&score);
            if (winner > 0) {
                printf(": player %d wins, %d points", winner, score);
            } else {
                printf(": %s, %d points", winner == 0 ? "a draw" : "no result", score);
            }
        } else if (strcmp(sRunning, "Tattle cards") == 0) {
            int i;
            printf(": wins");
            for (i = 0; i < PORT_DUEL_NPCS; i++) {
                printf(" %s %d -> %d", portCardsOpponent(i), sCardWins[i], portCardsWins(i));
            }
        }
        printf("\n");
        fflush(stdout);
        sOver = sRunning;
        sRunning = NULL;
        sOverFrames = 0;
        sControlFrames = 0;
    }
    if (sOver != NULL) {
        sOverFrames++;
        if (idleField() && marioChkKey() && marioStGetSystemLevel() == 0 && upNow() == NULL) {
            sControlFrames++;
        } else {
            sControlFrames = 0;
        }
        if (sControlFrames >= 60) {
            printf("[moderun] control back after %s: %d frames, no menu\n", sOver, sOverFrames);
            check(sOver);
            if (sExitAfter > 0 && sRuns >= sEnds) {
                sExitIn = sExitAfter;
            }
            sOver = NULL;
        } else if (sOverFrames == 3600) {
            printf("[moderun] control not back after %s (map %s, seq %d, battle %d, menu %d)\n", sOver,
                   gp->currentMapName, seqGetSeq(), gp->inBattle, marioStGetSystemLevel());
            check(sOver);
            fflush(stdout);
            if (sExitAfter > 0) {
                portTestExit(6);
            }
        }
    }
}

int portModeRunVersusCpu(void) {
    static int on = -1;
    if (on < 0) {
        on = envInt("TTYD_MODERUN_VERSUS_CPU", 0) > 0;
    }
    return on;
}
