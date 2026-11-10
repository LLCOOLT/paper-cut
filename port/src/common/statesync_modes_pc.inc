/* "Sync what matters" (port/include/port/statesync.h): the party modes when each player has
 * their own game: the quiz party, the Tattle-card duel against a player, the dungeon master, the
 * raid boss. Their messages are SS_R_MODES and up.
 * Included at the end of statesync_pc.c (a file of its own would change the source list, and
 * re-configuring an Aurora build re-checks port/patches/aurora against a shared Aurora checkout).
 *
 * The party games (the quiz, the card duel) are shows on one screen played from up to four
 * controllers. With each player in their own game, the host's game plays the show: each guest's
 * game shows the host's state as it comes (the question, the picks, the scores) and sends what its
 * player presses, which the host's game takes as that player's controller (player 2, 3 or 4). */
#include "port/statesync.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    M_PARTY_STATE = SS_R_MODES, /* the host: u8 game, u32 length, the game's state */
    M_PARTY_PRESS,              /* a guest: u8 game, u16 buttons pressed this frame */
    M_RAID_START,               /* the host: s32 week, s32 players */
    M_TOGETHER,                 /* the host: u32 kind (PORT_TOGETHER_*), u32 the tower's seed */
};

#define PARTY_STATE_MAX 8192

static struct {
    int game;
    uint32_t len;
    uint8_t data[PARTY_STATE_MAX];
    unsigned version;                       /* (each new state +1) */
    unsigned short pressed[SS_PLAYERS + 1]; /* the host: each player's buttons, not yet taken */
} sParty;

int portStateSyncPartyRole(void) {
    if (!portStateSyncLinked()) {
        return 0;
    }
    return ssNetIsHost() ? 1 : 2;
}

void portStateSyncPartyState(int game, const void* state, size_t n) {
    static uint8_t buf[5 + PARTY_STATE_MAX];
    uint32_t len = (uint32_t)n;
    if (portStateSyncPartyRole() != 1 || n > PARTY_STATE_MAX) {
        return;
    }
    buf[0] = (uint8_t)game;
    memcpy(buf + 1, &len, 4);
    memcpy(buf + 5, state, n);
    ssNetSendRelTo(SS_ALL, M_PARTY_STATE, buf, 5 + n);
}

int portStateSyncPartyStateGet(int game, void* out, size_t cap, unsigned* version) {
    if (sParty.version == 0 || sParty.game != game || sParty.len > cap) {
        return -1;
    }
    memcpy(out, sParty.data, sParty.len);
    if (version != NULL) {
        *version = sParty.version;
    }
    return (int)sParty.len;
}

void portStateSyncPartyPress(int game, unsigned short buttons) {
    uint8_t m[3];
    if (portStateSyncPartyRole() != 2 || buttons == 0) {
        return;
    }
    m[0] = (uint8_t)game;
    memcpy(m + 1, &buttons, 2);
    ssNetSendRelTo(1, M_PARTY_PRESS, m, sizeof(m));
    {
        static int sSaid;
        if (sSaid < 3) {
            sSaid++;
            ssLog("party: this player's press %04x to the host (game %d)", buttons, game);
        }
    }
}

unsigned short portStateSyncPartyPressed(int player) {
    unsigned short b;
    if (player < 1 || player > SS_PLAYERS) {
        return 0;
    }
    b = sParty.pressed[player];
    sParty.pressed[player] = 0;
    return b;
}

/* ---- the raid boss: the host's start is everyone's ---- */

static struct {
    int pending;
    int32_t week, players;
    unsigned long long at; /* when it came: a game still loading or in a battle takes it once free, for 20 s */
} sRaid;

int portStateSyncPlayersNow(void) {
    int p, n = 1;
    if (!portStateSyncLinked()) {
        return 1;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        n += present(p) && !G.pl[p].info.watch; /* (a watcher isn't a player) */
    }
    return n;
}

int portStateSyncPlayersLinked(void) {
    int p, n = 1;
    if (!portStateSyncLinked()) {
        return 1;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        /* (present's link, without its heard-from-lately: a game loading a room sends nothing for a while) */
        if (p != ssNetMe() && G.pl[p].info.valid && !G.pl[p].info.watch &&
            (ssNetIsHost() || p == 1 ? ssNetLinked(p) : ssNetLinked(1))) {
            n++;
        }
    }
    return n;
}

int portStateSyncPlayerHere(int player) {
    if (!portStateSyncLinked() || player < 1 || player > SS_PLAYERS) {
        return 0;
    }
    return player == ssNetMe() || (present(player) && !G.pl[player].info.watch);
}

int portStateSyncOthersAway(void) {
    int p, n = 0;
    if (!portStateSyncLinked() || gp == NULL) {
        return 0;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        /* (in the room but its keys still off, coming in: not free for an invitation yet) */
        n += present(p) && !G.pl[p].info.watch && (!hereIn(p) || G.pl[p].info.keysOff || G.pl[p].info.inBattle);
    }
    return n;
}

void portStateSyncRaidSend(int week, int players) {
    int32_t m[2];
    if (portStateSyncPartyRole() != 1) {
        return;
    }
    m[0] = week;
    m[1] = players;
    ssNetSendRelTo(SS_ALL, M_RAID_START, m, sizeof(m));
    ssLog("raid: started here (week %d, %d players): sent to every guest", week, players);
}

int portStateSyncRaidTake(int* week, int* players) {
    if (!sRaid.pending) {
        return 0;
    }
    sRaid.pending = 0;
    if (ssNetMs() - sRaid.at > 20000) {
        ssLog("raid: the host's start came 20 s ago and this game wasn't free since: not joined");
        return 0;
    }
    *week = sRaid.week;
    *players = sRaid.players;
    return 1;
}

/* ---- the Pit of 100 Trials and the Paper Tower together ---- */

#ifdef TTYD_BACKEND_AURORA
#include "port/playmodes.h"
#include "port/postgame.h"
#define towerOn() portTowerActive()
#define towerFloor() portTowerFloor()
#else
#define towerOn() 0
#define towerFloor() 0
#endif

static struct {
    int pending;
    uint32_t kind, seed;
    unsigned followAt; /* G.frame of the last follow asked */
} sTogether;

/* the floor this player stands on: the Pit's 1000 + floor (1-100), the tower's 2000 + floor, 0 */
static int myModeFloor(void) {
    if (gp == NULL) {
        return 0;
    }
    if (!strncmp(gp->currentMapName, "jon", 3)) {
        return 1000 + G.pitFloor + 1;
    }
    if (towerOn()) {
        return 2000 + towerFloor();
    }
    return 0;
}

int portEvtPitFloorShared(struct EventEntry* event, int isFirstCall) {
    (void)event;
    (void)isFirstCall;
    if (portStateSyncLinked() && gp != NULL && swByteGet(GSW_JON_FLOOR) != G.pitFloor) {
        ssLog("pit: this room is floor %d (the shared floor says %d): down from here", G.pitFloor + 1,
              swByteGet(GSW_JON_FLOOR) + 1);
        swByteSet(GSW_JON_FLOOR, G.pitFloor);
    }
    return 2; /* EVT_RETURN_DONE */
}

static int startTogether(uint32_t kind, uint32_t seed, const char** why) {
#ifdef TTYD_BACKEND_AURORA
    if (kind == PORT_TOGETHER_PIT) {
        return portPitCoopStartOnline(why);
    }
    if (kind == PORT_TOGETHER_TOWER) {
        return portTowerStartSeed(seed, why);
    }
#else
    (void)seed;
#endif
    (void)kind;
    *why = "Not in this build.";
    return 0;
}

int portStateSyncTogether(int kind, const char** why) {
    uint32_t m[2];
    if (!portStateSyncLinked()) {
        *why = "Not connected to the other player.";
        return 0;
    }
    if (!ssNetIsHost()) {
        *why = "The host starts it: everyone's game goes there together.";
        return 0;
    }
    m[0] = (uint32_t)kind;
#ifdef TTYD_BACKEND_AURORA
    m[1] = kind == PORT_TOGETHER_TOWER ? portTowerSeedNow() : 0;
#else
    m[1] = 0;
#endif
    if (!startTogether(m[0], m[1], why)) {
        return 0;
    }
    ssNetSendRelTo(SS_ALL, M_TOGETHER, m, sizeof(m));
    ssLog("together: %s started here (seed %u): sent to every guest", kind == PORT_TOGETHER_PIT ? "the Pit" : "the tower",
          m[1]);
    return 1;
}

static void modesFrame(void) {
    const int mine = myModeFloor();
    int p, best = 0, who = 0;
    if (gp == NULL || !portStateSyncLinked()) {
        return;
    }
    if (sTogether.pending && G.seq == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && marioChkKey()) {
        const char* why = NULL;
        sTogether.pending = 0;
        if (startTogether(sTogether.kind, sTogether.seed, &why)) {
            portCoopNotice(sTogether.kind == PORT_TOGETHER_PIT ? "The host started the Pit of 100 Trials for everyone."
                                                              : "The host started the Paper Tower for everyone.");
        } else {
            portCoopNotice(why);
            ssLog("together: the host's start refused here: %s", why);
        }
    }
    /* another player gone on to a higher floor of the same: followed once this player is free */
    if (mine == 0 || G.watch || G.seq != SEQ_GAME || seqCheckSeq() || gp->inBattle || !marioChkKey() ||
        ssBattleHeld() || G.frame - sTogether.followAt < 60) {
        return;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        const SsPeer* o = &G.pl[p].info;
        if (p != ssNetMe() && present(p) && !o->watch && o->seq == SEQ_GAME && o->modeFloor / 1000 == mine / 1000 &&
            o->modeFloor > best) {
            best = o->modeFloor;
            who = p;
        }
    }
    if (best <= mine) {
        return;
    }
    sTogether.followAt = G.frame;
    if (mine / 1000 == 1) {
        if (strncmp(G.pl[who].info.map, "jon", 3) != 0) {
            return;
        }
        swByteSet(GSW_JON_FLOOR, best % 1000 - 1);
        ssLog("pit: player %d is on floor %d: after them to %s", who, best % 1000, G.pl[who].info.map);
        seqSetSeq(SEQ_MAP_CHANGE, G.pl[who].info.map, "dokan_2");
    } else {
#ifdef TTYD_BACKEND_AURORA
        ssLog("tower: player %d is on floor %d: after them", who, best % 1000);
        portTowerJoinFloor(best % 1000);
#endif
    }
}

void ssModesRel(int from, int type, const uint8_t* p, size_t n) {
    switch (type) {
        case M_PARTY_STATE: {
            uint32_t len;
            if (from != 1 || ssNetIsHost() || n < 5) {
                return;
            }
            memcpy(&len, p + 1, 4);
            if (len > PARTY_STATE_MAX || len > n - 5) {
                return;
            }
            sParty.game = p[0];
            sParty.len = len;
            memcpy(sParty.data, p + 5, len);
            sParty.version++;
            break;
        }
        case M_TOGETHER:
            if (from != 1 || ssNetIsHost() || n < 8) {
                return;
            }
            memcpy(&sTogether.kind, p, 4);
            memcpy(&sTogether.seed, p + 4, 4);
            sTogether.pending = 1;
            ssLog("together: the host started %u (seed %u): starting it here", sTogether.kind, sTogether.seed);
            break;
        case M_PARTY_PRESS: {
            unsigned short b;
            if (!ssNetIsHost() || from < 2 || from > SS_PLAYERS || n < 3) {
                return;
            }
            memcpy(&b, p + 1, 2);
            sParty.pressed[from] |= b;
            {
                static int sSaid;
                if (sSaid < 3) {
                    sSaid++;
                    ssLog("party: player %d's press %04x (game %d)", from, b, p[0]);
                }
            }
            break;
        }
        case M_RAID_START: {
            int32_t m[2];
            if (from != 1 || ssNetIsHost() || n < sizeof(m)) {
                return;
            }
            memcpy(m, p, sizeof(m));
            if (m[1] > SS_PLAYERS) {
                m[1] = SS_PLAYERS; /* (no more players than a room holds) */
            }
            sRaid.week = m[0];
            sRaid.players = m[1];
            sRaid.pending = 1;
            sRaid.at = ssNetMs();
            ssLog("raid: the host started one (week %d, %d players)", m[0], m[1]);
            break;
        }
        default:
            ssLog("modes: player %d's message %d (%u bytes): not known here", from, type, (unsigned)n);
            break;
    }
}
