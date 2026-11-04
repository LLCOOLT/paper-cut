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
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void portCoopNotice(const char* english); /* second_player_pc.c */

#define RING 512
#define WAIT_MS 15000 /* the other player's input not there for this long: the session ends */

static int sActive, sDelay;
static unsigned sFrame;     /* pad reads since the session began */
static const char* sEnded;  /* why it ended, English */
static unsigned char sOwn[RING][PORT_ONLINE_PAD_BYTES]; /* this player's input, by the frame it's for */
static struct {
    unsigned frame;
    unsigned long long hash;
    int have;
} sMine[16];

static u64 sHash;

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
    mario = marioGetPtr();
    if (mario == NULL) {
        return sHash; /* the game still starting (DEMOInit's frames): nothing else is there yet */
    }
    mixVec(&mario->position);
    party = partyGetPtr(marioGetPartyId());
    if (party != NULL) {
        mixVec(&party->position);
    }
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
    nw = npcGetWorkPtr();
    for (i = 0; nw != NULL && i < nw->count; i++) {
        if (nw->entries[i].flags & 1) {
            mixVec(&nw->entries[i].position);
        }
    }
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
        memcpy(own, sOwn[f % RING], sizeof(own));
        if (!portOnlineWaitInput(f, theirs, WAIT_MS)) {
            end("The online game ended: the other player stopped answering.");
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

/* each frame, after the game's (vi_aurora.c): the fingerprints, and the
 * tests' fingerprint file */
void portOnlineFrame(void) {
    static FILE* sFile;
    static int sOpened;
    unsigned i;
    if (!sOpened) {
        const char* path = getenv("TTYD_FINGERPRINT");
        sOpened = 1;
        sFile = path != NULL ? fopen(path, "w") : NULL;
    }
    if (sFile != NULL && gp != NULL) {
        fprintf(sFile, "%u %016llx %s\n", gp->retraceCount, (unsigned long long)portOnlineFingerprint(),
                gp->currentMapName);
        fflush(sFile);
    }
    if (!sActive) {
        return;
    }
    portOnlinePump();
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
                end("The online game ended: the two games went out of step.");
                return;
            }
        }
    }
}
