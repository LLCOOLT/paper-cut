/* Online co-op, the game's side (port/include/port/online.h).
 *
 * Lockstep: at each pad read, this player's controller is sent for the
 * frame a few ahead (the input delay), and the frame runs on both players'
 * input for it: controller 1 the host's, controller 2 the guest's, in both
 * games, so they play out the same. The first frames (before any input can
 * have crossed) run with nothing pressed, in both.
 *
 * The fingerprint: a hash of what the game plays out (never a host address,
 * which differs between the two computers): the frame, the random numbers'
 * state, the game clock, the sequence and map, Mario's and the partner's
 * positions, the scripts running (their IDs and the command each is on),
 * the NPCs' positions, the story flags and the script flags, and in a battle
 * each unit's HP. Each game sends its own every PORT_ONLINE_HASH_EVERY
 * frames and compares the other's; if they ever differ the games have gone
 * out of step, and the session ends (each plays on alone).
 *
 * A watcher (port/include/port/online.h) plays both players' input from the
 * host's batches and compares its fingerprints with the host's, which come
 * with them: one that differs is told (this game drifted), and it plays on.
 *
 * Three or four players: controllers 3 and 4 are the seats'. The host's input
 * for a frame carries the seats' as it had them when it sent it, so the host
 * and the guest play them with their own; a seat plays every player's input
 * from the host's batches, as a watcher does, and sends its own controller
 * for the host's next frames. Controllers past the session's players: none.
 *
 * $TTYD_FINGERPRINT=<file> (tests): each frame's fingerprint written to the
 * file, "frame hash map", to compare two runs of the same input; with
 * $TTYD_FINGERPRINT_PARTS, the hash after each part too, the session's frame
 * and both controllers as played, the random numbers, the game clock and
 * Mario's and the partner's positions.
 * $TTYD_ONLINE_DROP_AT=<frame> (tests): this side's connection dropped then,
 * once (the reconnect's test). */
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "party.h"
#include "port/online.h"
#include "port/platform.h"
#include "port/resume.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void portCoopNotice(const char* english); /* second_player_pc.c */

#define RING 512
#define INPUT_WAIT_MS 8000 /* the other player's input not there for this long: the connection made again */

static int sActive, sDelay;
static unsigned sEpoch; /* recoveries done this session (portOnlineFrame's, below) */
static int sAsked;      /* guest: asked the host for this epoch's */
static unsigned sFrame;     /* pad reads since the session began */
static unsigned char sOwn[RING][PORT_ONLINE_PAD_BYTES]; /* this player's input, by the frame it's for */
static struct {
    unsigned frame;
    unsigned long long hash;
    int have;
} sMine[16];

static int sDrifted; /* a watcher: told once that its game drifted */
/* the last frame's controllers as played, and its frame: the tests' parts file */
static unsigned char sPlayed[PORT_ONLINE_MAX_PLAYERS][PORT_ONLINE_PAD_BYTES];
static unsigned sPlayedFrame;

static u64 sHash;
/* the hash after each part (the clock and state, Mario, the partner, the
 * events, the NPCs): the tests' file names the part that went out of step
 * first ($TTYD_FINGERPRINT_PARTS) */
static u64 sParts[5];

static void mix(const void* data, size_t len) {
    const u8* p = (const u8*)data;
    size_t i;
    for (i = 0; i < len; i++) {
        sHash = (sHash ^ p[i]) * 0x100000001B3ull; /* FNV-1a */
    }
}

static void mixU32(u32 v) {
    mix(&v, sizeof(v));
}

static void mixVec(const Vec* v) {
    mix(v, sizeof(*v));
}

unsigned long long portOnlineFingerprint(void) {
    EventWork* ew;
    NpcWork* nw;
    MarioWork* mario;
    PartyEntry* party;
    s32 i;
    sHash = 0xCBF29CE484222325ull;
    if (gp == NULL) {
        return sHash;
    }
    mixU32(gp->retraceCount);
    mixU32(portRandState());
#ifdef TTYD_BACKEND_AURORA
    {
        const s64 clock = portFastClockGet();
        mix(&clock, sizeof(clock));
    }
#endif
    mixU32((u32)seqGetSeq());
    mix(gp->currentMapName, sizeof(gp->currentMapName));
    mixU32((u32)gp->inBattle);
    memset(sParts, 0, sizeof(sParts));
    sParts[0] = sHash;
    mario = marioGetPtr();
    if (mario == NULL) {
        return sHash; /* the game still starting (DEMOInit's frames): nothing else is there yet */
    }
    mixVec(&mario->position);
    sParts[1] = sHash;
    party = partyGetPtr(marioGetPartyId());
    if (party != NULL) {
        mixVec(&party->position);
    }
    sParts[2] = sHash;
    ew = evtGetWork();
    if (ew != NULL) {
        mix(ew->gfData, sizeof(ew->gfData));
        for (i = 0; i < ew->count; i++) {
            EventEntry* e = &ew->entries[i];
            if (e->flags & 1) {
                mixU32((u32)e->eventId);
                mixU32((u32)e->opcode);
                mixU32((u32)e->loopDepth);
            }
        }
    }
    sParts[3] = sHash;
    nw = npcGetWorkPtr();
    for (i = 0; nw != NULL && i < nw->count; i++) {
        if (nw->entries[i].flags & 1) {
            mixVec(&nw->entries[i].position);
        }
    }
    sParts[4] = sHash;
    if (gp->inBattle && _battleWorkPointer != NULL) {
        for (i = 0; i < BATTLE_UNIT_MAX; i++) {
            BattleUnit* unit = _battleWorkPointer->units[i];
            if (unit != NULL) {
                mixU32((u32)(s32)unit->currentHp);
            }
        }
    }
    return sHash;
}

/* --- the controllers --- */

void portOnlinePackPad(const PADStatus* s, unsigned char* p) {
    memset(p, 0, PORT_ONLINE_PAD_BYTES);
    p[0] = (unsigned char)s->button;
    p[1] = (unsigned char)(s->button >> 8);
    p[2] = (unsigned char)s->stickX;
    p[3] = (unsigned char)s->stickY;
    p[4] = (unsigned char)s->substickX;
    p[5] = (unsigned char)s->substickY;
    p[6] = s->triggerLeft;
    p[7] = s->triggerRight;
    p[8] = s->analogA;
    p[9] = s->analogB;
    p[10] = (unsigned char)s->err;
#ifdef TTYD_BACKEND_AURORA /* the extra buttons (Aurora's PADStatus) */
    p[12] = (unsigned char)s->extButton;
    p[13] = (unsigned char)(s->extButton >> 8);
    p[14] = (unsigned char)(s->extButton >> 16);
    p[15] = (unsigned char)(s->extButton >> 24);
#endif
}

void portOnlineUnpackPad(const unsigned char* p, PADStatus* s) {
    memset(s, 0, sizeof(*s));
    s->button = (u16)(p[0] | (p[1] << 8));
    s->stickX = (s8)p[2];
    s->stickY = (s8)p[3];
    s->substickX = (s8)p[4];
    s->substickY = (s8)p[5];
    s->triggerLeft = p[6];
    s->triggerRight = p[7];
    s->analogA = p[8];
    s->analogB = p[9];
    s->err = (s8)p[10];
#ifdef TTYD_BACKEND_AURORA
    s->extButton = p[12] | (p[13] << 8) | (p[14] << 16) | ((u32)p[15] << 24);
#endif
}

/* nothing pressed, a controller there: the first frames' input, in both games */
static void neutral(unsigned char* p) {
    PADStatus none;
    memset(&none, 0, sizeof(none));
    none.err = PAD_ERR_NONE;
    portOnlinePackPad(&none, p);
}

static void end(const char* why) {
    if (!sActive) {
        return;
    }
    sActive = 0;
    portOnlineCancel(why);
    portCoopNotice(why);
    printf("[online] ended at frame %u: %s\n", sFrame, why);
    fflush(stdout);
}

/* the other side gone: their goodbye's reason when it says the game ended (a Leave from the
 * Start menu, which reaches this game a frame or two before its own Leave would), else left or
 * quiet */
static void endLost(void) {
    char status[160];
    snprintf(status, sizeof(status), "%s", portOnlineStatus()); /* (a copy: ending sets the status) */
    end(strncmp(status, "The online game ended", 21) == 0 ? status
        : strstr(status, "left") != NULL                ? "The online game ended: the other player left."
                                                        : "The online game ended: the other player stopped answering.");
}

void portOnlineBegin(int inputDelay) {
    unsigned i;
    sActive = 1;
    sDelay = inputDelay < 1 ? 1 : inputDelay > 30 ? 30 : inputDelay;
    sFrame = 0;
    /* recoveries this session: none, or as many as before the restart it began from */
    sEpoch = getenv("TTYD_ONLINE_RECOVERIES") != NULL ? (unsigned)atoi(getenv("TTYD_ONLINE_RECOVERIES")) : 0;
    if (sEpoch > 0) {
        portCoopNotice("Out of step: both games started again from the host's last save.");
    }

    sAsked = 0;
    sDrifted = 0;
    memset(sMine, 0, sizeof(sMine));
    for (i = 0; i < RING; i++) {
        neutral(sOwn[i]);
    }
    printf("[online] the session begins: %s, %d players, input delay %d frames\n",
           portOnlineWatching() ? "watcher" : portOnlineSeating() ? "seat" : portOnlineIsHost() ? "host" : "guest",
           portOnlinePlayers(), sDelay);
    fflush(stdout);
}

int portOnlineActive(void) {
    return sActive;
}

void portOnlineLeave(void) {
    end("The online game ended from the Start menu: each plays on alone.");
}

void portOnlineQuit(void) {
    if (!sActive) {
        return;
    }
    sActive = 0;
    portOnlineCancel("The online game ended: the other player left.");
    portCoopNotice("You left the online game: you play on alone.");
    printf("[online] left by this player at frame %u\n", sFrame);
    fflush(stdout);
}

/* the frame's input, every player's, into the controllers (none past the players) */
static void play(unsigned f, unsigned char frame[PORT_ONLINE_MAX_PLAYERS][PORT_ONLINE_PAD_BYTES], PADStatus* pads) {
    const int players = portOnlinePlayers();
    int i;
    memcpy(sPlayed, frame, sizeof(sPlayed));
    sPlayedFrame = f;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (i < players && i < PORT_ONLINE_MAX_PLAYERS) {
            portOnlineUnpackPad(frame[i], &pads[i]);
        } else {
            memset(&pads[i], 0, sizeof(pads[i]));
            pads[i].err = PAD_ERR_NO_CONTROLLER;
        }
    }
}

/* the wait for input over: the frame begun again if the wait's notice showed this one
 * (vi_aurora.c's portOnlineHoldFrame) */
static void holdEnd(void) {
#ifdef TTYD_BACKEND_AURORA
    void portOnlineHoldEnd(void); /* vi_aurora.c */
    portOnlineHoldEnd();
#endif
}

void portOnlineApplyPad(PADStatus* pads) {
    unsigned char mine[PORT_ONLINE_PAD_BYTES], own[PORT_ONLINE_PAD_BYTES], theirs[PORT_ONLINE_FRAME_BYTES];
    unsigned char frame[PORT_ONLINE_MAX_PLAYERS][PORT_ONLINE_PAD_BYTES];
    const int players = portOnlinePlayers();
    unsigned f;
    int i;
    if (!sActive) {
        return;
    }
    if (portOnlineWatching() || portOnlineSeating()) {
        /* every player's input, from the host's batches (a seat's own among them, as the host took it) */
        int got;
        const int seat = portOnlineSeating();
        f = sFrame++;
        if (seat) {
            /* this player's controller (before it's replaced), for the host's next frame: sent
               before the wait for the host's batch, not after it, so it isn't older than it
               need be when it gets there (the host only takes what has come) */
            portOnlinePackPad(&pads[0], mine);
            portOnlineSendInput(f, mine);
        }
        got = portOnlineWatchInput(f, frame[0]);
        holdEnd();
        if (got == 0) {
            end(seat ? "The online game ended: the host left." : "Watching ended: the players' game is over.");
            return;
        }
        if (got == 2) { /* the players start again: so does this game (portOnlineFrame) */
            for (i = 0; i < PORT_ONLINE_MAX_PLAYERS; i++) {
                neutral(frame[i]);
            }
        }
        play(f, frame, pads);
        return;
    }
    if (!portOnlineConnected()) {
        endLost();
        return;
    }
    {
        /* $TTYD_ONLINE_DROP_AT=<frame> (tests): the connection dropped there, once */
        static int sDropped;
        const char* at = getenv("TTYD_ONLINE_DROP_AT");
        if (!sDropped && at != NULL && sEpoch == 0 && sFrame == (unsigned)atoi(at)) {
            sDropped = 1;
            printf("[online] test: the connection dropped on purpose at frame %u\n", sFrame);
            fflush(stdout);
            portOnlineTestDrop();
        }
    }
    {
        /* $TTYD_ONLINE_STALL_AT=<frame>:<ms> (tests): this side stops that long there, once, as a
           busy computer would (the other side's wait, and its notice, vi_aurora.c) */
        static int sStalled;
        const char* at = getenv("TTYD_ONLINE_STALL_AT");
        if (!sStalled && at != NULL && sFrame == (unsigned)atoi(at)) {
            const char* ms = strchr(at, ':');
            sStalled = 1;
            printf("[online] test: this side stalls at frame %u\n", sFrame);
            fflush(stdout);
            portOnlineTestStall(ms != NULL ? atoi(ms + 1) : 3000);
        }
    }
    f = sFrame++;
    /* this player's controller now, for the frame the delay ahead, kept and sent */
    portOnlinePackPad(&pads[0], mine);
    memcpy(sOwn[(f + sDelay) % RING], mine, sizeof(mine));
    portOnlineSendInput(f + sDelay, mine);
    /* this frame: every player's input for it */
    for (i = 0; i < PORT_ONLINE_MAX_PLAYERS; i++) {
        neutral(frame[i]);
    }
    if (f >= (unsigned)sDelay) {
        int got;
        memcpy(own, sOwn[f % RING], sizeof(own));
        memset(theirs, 0, sizeof(theirs));
        got = portOnlineWaitInput(f, theirs, INPUT_WAIT_MS);
        holdEnd();
        if (got == 2) {
            /* a recovery came (or was asked for): this frame nothing pressed; it's
               put in place once the frame is over (portOnlineFrame) */
        } else if (!got) {
            /* left (closed, or said goodbye), or went quiet */
            endLost();
            return;
        } else if (portOnlineIsHost()) {
            /* the host's own, the guest's, the seats' (as the host sent them with its own) */
            memcpy(frame[0], own, PORT_ONLINE_PAD_BYTES);
            memcpy(frame[1], theirs, PORT_ONLINE_PAD_BYTES);
            if (players > 2 && !portOnlineSeatPads(f, frame[2])) {
                /* (the seats' pads as sent are gone: playing them neutral would differ) */
                end("The online game ended: the two games went out of step.");
                return;
            }
        } else {
            /* the host's (its own, then the seats'), and the guest's own */
            memcpy(frame[0], theirs, PORT_ONLINE_PAD_BYTES);
            memcpy(frame[1], own, PORT_ONLINE_PAD_BYTES);
            for (i = 2; i < players && i < PORT_ONLINE_MAX_PLAYERS; i++) {
                memcpy(frame[i], theirs + (size_t)(i - 1) * PORT_ONLINE_PAD_BYTES, PORT_ONLINE_PAD_BYTES);
            }
        }
    }
    if (portOnlineIsHost()) {
        portOnlineWatchRecord(f, frame[0]); /* the frame as played, for the watchers and the seats */
    }
    play(f, frame, pads);
}

/* --- recovery: out of step, both start again from the host's last save ---
 *
 * A save loaded into the running games isn't enough: what isn't in a save
 * (scripts halfway, effects, the characters' AI and the random numbers they
 * draw while the room loads) was already different, and the games drift
 * apart again. A fresh start is what keeps two games the same (a session's
 * own). So the host tells the guest ("RSYN" and the recovery's number), and
 * both do what Restart now does: the host writes its last clean save (quick
 * resume's, resume_aurora.c), each program starts again, the host hosting
 * the same room (or port) and the guest joining it, and the session begins
 * from the host's save as "Continue where you left off" (online.cpp,
 * portOnlineRestartRejoin). A guest out of step first asks the host. Not on
 * phones (their apps can't start themselves again: the session ends there),
 * and not more than kMaxRecoveries times a session. */
#define RESYNC_MAGIC 0x4E595352u /* "RSYN" */
static const unsigned kMaxRecoveries = 3;

#ifdef TTYD_BACKEND_AURORA
int portRestartSupported(void);                       /* restart.cpp */
/* host 1, guest 0, a watcher 2 */
void portOnlineRestartRejoin(int host, unsigned recoveries); /* online.cpp: doesn't return */
#endif

static void wr32(unsigned char* p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
static unsigned rd32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int canRecover(void) {
#ifdef TTYD_BACKEND_AURORA
    return portRestartSupported() && sEpoch < kMaxRecoveries;
#else
    return 0;
#endif
}

/* host: the guest told, then both start again */
static void hostRecovery(void) {
#ifdef TTYD_BACKEND_AURORA
    unsigned char m[8];
    int slot;
    size_t size;
    if (!canRecover() || portResumeImage(&slot, &size) == NULL) {
        end("The online game ended: the two games went out of step.");
        return;
    }
    wr32(m, RESYNC_MAGIC);
    wr32(m + 4, sEpoch + 1);
    portOnlineSendResync(m, sizeof(m));
    printf("[online] out of step: both games start again from the host's last save (%u of %u)\n", sEpoch + 1,
           kMaxRecoveries);
    fflush(stdout);
    portOnlineRestartRejoin(1, sEpoch + 1);
#endif
    end("The online game ended: the two games went out of step.");
}

/* a mismatch: the host starts the recovery, the guest asks it to */
static void outOfStep(void) {
    if (!canRecover()) {
        end("The online game ended: the two games went out of step.");
    } else if (portOnlineIsHost()) {
        hostRecovery();
    } else if (!sAsked) {
        sAsked = 1;
        portOnlineAskResync(sEpoch);
    }
}

/* each frame: the guest's ask (host), the host's word (guest, a watcher); 1 when it was taken */
static int recoveryFrame(void) {
    if (portOnlineWatching() || portOnlineSeating()) {
        const unsigned epoch = portOnlineWatchResync();
        if (epoch != 0) {
#ifdef TTYD_BACKEND_AURORA
            if (portRestartSupported()) {
                printf("[online] the players start again from the host's last save: so does this game\n");
                fflush(stdout);
                portOnlineRestartRejoin(portOnlineSeating() ? 3 : 2, epoch); /* (a seat joins as one again) */
            }
#endif
            end(portOnlineSeating() ? "The online game ended: the two games went out of step."
                                    : "Watching ended: the players started again.");
            return 1;
        }
        return 0;
    }
    if (portOnlineIsHost()) {
        const int asked = portOnlineResyncAsked();
        if (asked >= 0 && (unsigned)asked == sEpoch) { /* (an older ask: that one's done) */
            hostRecovery();
            return 1;
        }
    } else {
        const void* p = NULL;
        size_t len = 0;
        if (portOnlineResyncArrived(&p, &len)) {
            const unsigned char* m = (const unsigned char*)p;
            const unsigned epoch = len >= 8 && rd32(m) == RESYNC_MAGIC ? rd32(m + 4) : 0;
            if (epoch == 0 || !canRecover()) {
                end("The online game ended: the two games went out of step.");
                return 1;
            }
#ifdef TTYD_BACKEND_AURORA
            printf("[online] out of step: starting again from the host's last save (%u of %u)\n", epoch,
                   kMaxRecoveries);
            fflush(stdout);
            portOnlineRestartRejoin(0, epoch);
#endif
            end("The online game ended: the two games went out of step.");
            return 1;
        }
    }
    {
        /* $TTYD_ONLINE_DESYNC_AT=<frame> (tests, the guest): its random numbers nudged once,
           in its first session only (not the one it starts again into) */
        static int sNudged;
        const char* at = getenv("TTYD_ONLINE_DESYNC_AT");
        if (!sNudged && at != NULL && sEpoch == 0 && !portOnlineIsHost() && sFrame == (unsigned)atoi(at)) {
            sNudged = 1;
            portRandSetState(portRandState() ^ 0x5A5A5A5Au);
            printf("[online] test: out of step on purpose at frame %u\n", sFrame);
            fflush(stdout);
        }
    }
    return 0;
}

/* each frame, after the game's (vi_aurora.c): the fingerprints, and the
 * tests' fingerprint file */
void portOnlineFrame(void) {
    static FILE* sFile;
    static int sOpened, sPartsWanted;
    unsigned i;
    if (!sOpened) {
        const char* path = getenv("TTYD_FINGERPRINT");
        sOpened = 1;
        sFile = path != NULL ? fopen(path, "w") : NULL;
        sPartsWanted = getenv("TTYD_FINGERPRINT_PARTS") != NULL;
    }
    if (sFile != NULL && gp != NULL) {
        fprintf(sFile, "%u %016llx %s", gp->retraceCount, (unsigned long long)portOnlineFingerprint(),
                gp->currentMapName);
        if (sPartsWanted) {
            const MarioWork* mario = marioGetPtr();
            int k;
            fprintf(sFile, " f%u r%08x", sPlayedFrame, (unsigned)portRandState());
#ifdef TTYD_BACKEND_AURORA
            fprintf(sFile, " c%llx", (unsigned long long)portFastClockGet());
#endif
            fputc(' ', sFile);
            for (k = 0; k < portOnlinePlayers() * PORT_ONLINE_PAD_BYTES; k++) {
                fprintf(sFile, "%02x", sPlayed[k / PORT_ONLINE_PAD_BYTES][k % PORT_ONLINE_PAD_BYTES]);
            }
            for (i = 0; i < 5; i++) {
                fprintf(sFile, " %c%016llx", "smpen"[i], (unsigned long long)sParts[i]);
            }
            if (mario != NULL) {
                const u32* p = (const u32*)&mario->position;
                const PartyEntry* party = partyGetPtr(marioGetPartyId());
                fprintf(sFile, " mario %08x %08x %08x", (unsigned)p[0], (unsigned)p[1], (unsigned)p[2]);
                if (party != NULL) {
                    const u32* q = (const u32*)&party->position;
                    fprintf(sFile, " party %08x %08x %08x", (unsigned)q[0], (unsigned)q[1], (unsigned)q[2]);
                }
            }
        }
        fputc('\n', sFile);
        fflush(sFile);
    }
    if (!sActive) {
        return;
    }
#ifdef TTYD_BACKEND_AURORA
    {
        void portOnlineHoldFrame(const char* english); /* vi_aurora.c: the window's title back */
        portOnlineHoldFrame(NULL);
    }
#endif
    portOnlinePump();
    {
        /* after a reconnect: said once the game goes on */
        static int sReconnects;
        if (portOnlineReconnected() != sReconnects && !portOnlineReconnecting() && sActive) {
            sReconnects = portOnlineReconnected();
            portCoopNotice("Reconnected: the online game goes on.");
        }
    }
    if (recoveryFrame()) {
        return; /* the games went back together: no fingerprint of the frame before */
    }
    if (portOnlineWatching() || portOnlineSeating()) {
        /* the host's fingerprints, with its batches: this game's compared with them */
        static unsigned long long sMineAt[64];
        static unsigned sMineFrame[64];
        if (sFrame > 0 && sFrame % PORT_ONLINE_HASH_EVERY == 0) {
            const int slot = (sFrame / PORT_ONLINE_HASH_EVERY) % 64;
            sMineFrame[slot] = sFrame;
            sMineAt[slot] = portOnlineFingerprint();
        }
        for (i = 0; i < 64; i++) {
            unsigned long long theirs;
            if (sMineFrame[i] != 0 && portOnlineWatchHostHash(sMineFrame[i], &theirs)) {
                if (theirs != sMineAt[i] && !sDrifted) {
                    sDrifted = 1;
                    printf("[online] watching: this game drifted from the players' at frame %u: %016llx here, %016llx "
                           "the host's\n",
                           sMineFrame[i], sMineAt[i], theirs);
                    fflush(stdout);
                    portCoopNotice("Watching: this game drifted from the players' (it plays on).");
                } else if (theirs == sMineAt[i] && sMineFrame[i] % 3600 == 0) {
                    printf("[online] watching: in step with the host at frame %u\n", sMineFrame[i]);
                    fflush(stdout);
                }
                sMineFrame[i] = 0;
            }
        }
        return;
    }
    if (sFrame > 0 && sFrame % PORT_ONLINE_HASH_EVERY == 0) {
        const int slot = (sFrame / PORT_ONLINE_HASH_EVERY) % 16;
        if (!sMine[slot].have || sMine[slot].frame != sFrame) {
            sMine[slot].frame = sFrame;
            sMine[slot].hash = portOnlineFingerprint();
            sMine[slot].have = 1;
            portOnlineSendHash(sFrame, sMine[slot].hash);
            if (portOnlineIsHost()) {
                portOnlineWatchHash(sFrame, sMine[slot].hash); /* (the watchers compare theirs) */
            }
        }
    }
    for (i = 0; i < 16; i++) {
        unsigned long long theirs;
        if (sMine[i].have && portOnlinePeerHash(sMine[i].frame, &theirs)) {
            sMine[i].have = 0;
            if (theirs != sMine[i].hash) {
                printf("[online] out of step at frame %u: %016llx here, %016llx there\n", sMine[i].frame,
                       sMine[i].hash, theirs);
                fflush(stdout);
                outOfStep();
                return;
            }
        }
    }
}
