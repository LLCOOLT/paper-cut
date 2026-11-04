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
 * $TTYD_FINGERPRINT=<file> (tests): each frame's fingerprint written to the
 * file, "frame hash map", to compare two runs of the same input. */
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
#define WAIT_MS 15000 /* the other player's input not there for this long: the session ends */

static int sActive, sDelay;
static unsigned sEpoch; /* recoveries done this session (portOnlineFrame's, below) */
static int sAsked;      /* guest: asked the host for this epoch's */
static unsigned sFrame;     /* pad reads since the session began */
static const char* sEnded;  /* why it ended, English */
static unsigned char sOwn[RING][PORT_ONLINE_PAD_BYTES]; /* this player's input, by the frame it's for */
static struct {
    unsigned frame;
    unsigned long long hash;
    int have;
} sMine[16];

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

static void pack(const PADStatus* s, unsigned char* p) {
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

static void unpack(const unsigned char* p, PADStatus* s) {
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
    pack(&none, p);
}

static void end(const char* why) {
    if (!sActive) {
        return;
    }
    sActive = 0;
    sEnded = why;
    portOnlineCancel(why);
    portCoopNotice(why);
    printf("[online] ended at frame %u: %s\n", sFrame, why);
    fflush(stdout);
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
    sEnded = NULL;
    memset(sMine, 0, sizeof(sMine));
    for (i = 0; i < RING; i++) {
        neutral(sOwn[i]);
    }
    printf("[online] the session begins: %s, input delay %d frames\n", portOnlineIsHost() ? "host" : "guest",
           sDelay);
    fflush(stdout);
}

int portOnlineActive(void) {
    return sActive;
}

const char* portOnlineEnded(void) {
    return sEnded;
}

void portOnlineApplyPad(PADStatus* pads) {
    unsigned char mine[PORT_ONLINE_PAD_BYTES], own[PORT_ONLINE_PAD_BYTES], theirs[PORT_ONLINE_PAD_BYTES];
    unsigned f;
    int i;
    if (!sActive) {
        return;
    }
    if (!portOnlineConnected()) {
        end("The online game ended: the other player left.");
        return;
    }
    f = sFrame++;
    /* this player's controller now, for the frame the delay ahead, kept and sent */
    pack(&pads[0], mine);
    memcpy(sOwn[(f + sDelay) % RING], mine, sizeof(mine));
    portOnlineSendInput(f + sDelay, mine);
    /* this frame: both players' input for it */
    if (f < (unsigned)sDelay) {
        neutral(own);
        neutral(theirs);
    } else {
        int got;
        memcpy(own, sOwn[f % RING], sizeof(own));
        got = portOnlineWaitInput(f, theirs, WAIT_MS);
        if (got == 2) {
            /* a recovery came (or was asked for): this frame nothing pressed; it's
               put in place once the frame is over (portOnlineFrame) */
            neutral(own);
            neutral(theirs);
        } else if (!got) {
            /* left (closed, or said goodbye), or went quiet */
            end(strstr(portOnlineStatus(), "left") != NULL ? "The online game ended: the other player left."
                                                         : "The online game ended: the other player stopped answering.");
            return;
        }
    }
    unpack(portOnlineIsHost() ? own : theirs, &pads[0]);
    unpack(portOnlineIsHost() ? theirs : own, &pads[1]);
    for (i = 2; i < PAD_MAX_CONTROLLERS; i++) {
        memset(&pads[i], 0, sizeof(pads[i]));
        pads[i].err = PAD_ERR_NO_CONTROLLER;
    }
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

/* each frame: the guest's ask (host), the host's word (guest); 1 when it was taken */
static int recoveryFrame(void) {
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
    portOnlinePump();
    if (recoveryFrame()) {
        return; /* the games went back together: no fingerprint of the frame before */
    }
    if (sFrame > 0 && sFrame % PORT_ONLINE_HASH_EVERY == 0) {
        const int slot = (sFrame / PORT_ONLINE_HASH_EVERY) % 16;
        if (!sMine[slot].have || sMine[slot].frame != sFrame) {
            sMine[slot].frame = sFrame;
            sMine[slot].hash = portOnlineFingerprint();
            sMine[slot].have = 1;
            portOnlineSendHash(sFrame, sMine[slot].hash);
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
