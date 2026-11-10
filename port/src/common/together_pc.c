/* Playing together's game side (port/include/port/together.h): the team bonus,
 * and stand-ins for the co-op engine's lobby calls until statesync.h has them.
 *
 * The team bonus: on one computer with two players or more, when every player
 * who acted in a turn of Mario's side landed their action command (Nice or
 * better), a small pop-up says "Team bonus!" and the party gets a quarter of a
 * Star Power bar. Online the host's world decides it (the co-op engine counts
 * it with stable ids, portStateSyncTeamBonusCount) and every game shows it. */
#include "port/file_pc.h"
#include "port/together.h"
#include "port/players.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "mario/mario_pouch.h"
#include "port/cardsave_pc.h"
#include "port/save_files.h"
#include <stdio.h>
#include <stdlib.h>

int portSecondPlayerPlayerFor(void* unit, int guard); /* second_player_pc.c */

#define TEAM_BONUS_SP 25 /* a quarter of a bar */

int portTeamBonusOn(void) {
    const int* v = portSettingsFind("team_bonus");
    return v == NULL || *v != 0;
}

static int sTurn = -1;
static unsigned sActed, sMissed;
static int sGiven;
static unsigned sLocalCount;

void portTeamBonusAc(void* battleWork) {
    BattleWork* wp = (BattleWork*)battleWork;
    BattleUnit* unit;
    int player;
    if (wp == NULL || !portTeamBonusOn() || portStateSyncOn() || portCoopPlayers() < 2) {
        return; /* (online: the host's world gives it) */
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    if (unit == NULL || unit->alliance != 0) {
        return;
    }
    player = portSecondPlayerPlayerFor(unit, 0);
    if (player < 0 || player >= PORT_PLAYERS_MAX) {
        return;
    }
    if (wp->turnCount != sTurn) { /* a new turn: nobody has acted yet */
        sTurn = wp->turnCount;
        sActed = sMissed = 0;
        sGiven = 0;
    }
    sActed |= 1u << player;
    if ((wp->actionCommands.result & 2) == 0) {
        sMissed |= 1u << player;
    }
    /* two players or more acted this turn, every one of them landed it */
    if (!sGiven && sMissed == 0 && (sActed & (sActed - 1)) != 0) {
        sGiven = 1;
        sLocalCount++;
        pouchAddAP(TEAM_BONUS_SP);
        portPopupTeamBonus();
        printf("[together] team bonus: turn %d, players 0x%x\n", sTurn, sActed);
        fflush(stdout);
    }
}

/* --- a file started from outside the file menu --- */
static int sBootSlot = -1, sBootRogueport;

void portBootRequestNew(int slot, int rogueport) {
    sBootSlot = slot;
    sBootRogueport = rogueport;
    printf("[together] new file asked for: slot %d, %s\n", slot, rogueport ? "in Rogueport" : "from the beginning");
    fflush(stdout);
}

int portBootTakeNew(int* slot, int* rogueport) {
    if (sBootSlot < 0) {
        return 0;
    }
    *slot = sBootSlot;
    *rogueport = sBootRogueport;
    sBootSlot = -1;
    return 1;
}

int portBootNewPending(void) {
    return sBootSlot >= 0;
}

/* --- a guest's way off the title, owed (together_aurora.c's guestGo; the logos and the file menu read it) --- */
static int sGuestOwed;

void portTogetherGuestOwedSet(int where) {
    sGuestOwed = where;
}

int portTogetherGuestOwed(void) {
    return sGuestOwed;
}

unsigned portTeamBonusShown(void) {
    return sLocalCount + portStateSyncTeamBonusCount();
}

/* --- stand-ins for the co-op engine's lobby (until statesync.h defines PORT_STATESYNC_LOBBY) --- */
#ifndef PORT_STATESYNC_LOBBY
static int sPick, sPickA, sPickB;
static unsigned sPickVersion;

void portStateSyncPickSet(int pick, int a, int b) {
    sPick = pick;
    sPickA = a;
    sPickB = b;
    sPickVersion++;
}

int portStateSyncPick(int* a, int* b, unsigned* version) {
    if (a != NULL) {
        *a = sPickA;
    }
    if (b != NULL) {
        *b = sPickB;
    }
    if (version != NULL) {
        *version = sPickVersion;
    }
    return sPick;
}

int portStateSyncSeatPlayer(int seat) {
    if (!portStateSyncLinked() || seat < 1 || seat > SS_PLAYERS) {
        return 0;
    }
    return seat == ssNetMe() || ssPeerOf(seat)->valid ? seat : 0; /* (player n on seat n) */
}

void portStateSyncSeatAsk(int seat) {
    (void)seat;
}

int portStateSyncSeatAsked(int* fromPlayer, int* seat) {
    (void)fromPlayer;
    (void)seat;
    return 0;
}

void portStateSyncSeatAnswer(int yes) {
    (void)yes;
}

void portStateSyncSeatSet(int seat, int player) {
    (void)seat;
    (void)player;
}

int portStateSyncSeatPending(void) {
    return 0;
}

unsigned portStateSyncTeamBonusCount(void) {
    return 0;
}

int portStateSyncCoopOn(void) {
    return portStateSyncLinked();
}

int portStateSyncCoopPart(int player) {
    return player == 1 ? 1 : 0;
}

int portStateSyncMe(void) {
    return ssNetMe();
}

int portStateSyncCoopGuest(void) {
    return 0; /* (no co-op guest before the engine: each game plays its own Mario) */
}

/* the guest's copy: tests only until the engine keeps them, $TTYD_COOP_COPY=<a slot image (0x4000 bytes) or a
   .gci (its first file)>, $TTYD_COOP_COPY_HOST=<the host's name> */
static int sCopyTaken;

int portCoopCopyPending(char* hostName, size_t cap, unsigned char* image) {
    const char* path = getenv("TTYD_COOP_COPY");
    const char* host = getenv("TTYD_COOP_COPY_HOST");
    if (path == NULL || sCopyTaken) {
        return 0;
    }
    if (image != NULL) {
        PortSaveInfo info;
        FILE* f = portFopen(path, "rb");
        long size;
        size_t n;
        if (f == NULL) {
            return 0;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, size > (long)PORT_CARD_SLOT_SIZE ? 0x40 + (long)PORT_CARD_META_SIZE : 0, SEEK_SET);
        n = fread(image, 1, PORT_CARD_SLOT_SIZE, f);
        fclose(f);
        if (n != PORT_CARD_SLOT_SIZE || !portSaveImageInfo(image, &info)) {
            return 0;
        }
    }
    if (hostName != NULL && cap > 0) {
        snprintf(hostName, cap, "%s", host != NULL ? host : "Host");
    }
    return 1;
}

void portCoopCopyDone(void) {
    sCopyTaken = 1;
    printf("[together] the co-op copy is taken (or declined)\n");
    fflush(stdout);
}
#endif
