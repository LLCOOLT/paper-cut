/* The game's fingerprint and the controllers as bytes (port/include/port/online.h):
 * what replays, the tests and sync what matters's shared battles use.
 *
 * The fingerprint: a hash of what the game plays out (never a host address,
 * which differs between two computers): the frame, the random numbers'
 * state, the game clock, the sequence and map, Mario's and the partner's
 * positions, the scripts running (their IDs and the command each is on),
 * the NPCs' positions, the story flags and the script flags, and in a battle
 * each unit's HP. Two games on the same input have the same one each frame.
 *
 * $TTYD_FINGERPRINT=<file> (tests): each frame's fingerprint written to the
 * file, "frame hash map", to compare two runs of the same input; with
 * $TTYD_FINGERPRINT_PARTS, the hash after each part too, the random numbers, the game clock and Mario's and
 * the partner's positions. */
#include "port/file_pc.h"
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
    {
        const s64 clock = portFastClockGet();
        mix(&clock, sizeof(clock));
    }
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

/* --- the tests' fingerprint file ---
 *
 * Buffered (a write a frame cost a test run a sixth of its time), written out
 * every FP_FLUSH_EVERY frames and before every exit a test makes
 * (portTestExit), so it ends at the last frame. */
#define FP_FLUSH_EVERY 600
static FILE* sFpFile;

void portTestFlush(void) {
    if (sFpFile != NULL) {
        fflush(sFpFile);
    }
    fflush(stdout);
    fflush(stderr);
}

static int sTestEnding;

void portTestEnding(void) {
    sTestEnding = 1;
}

void portStateSyncExitDrain(void); /* statesync_pc.c */

void portTestExit(int code) {
    if (code == 0) {
        portStateSyncExitDrain(); /* (online: what's on the way delivered before the game goes) */
    }
    portTestFlush();
    _Exit(code);
}

void portFingerprintFrame(void) {
    static int sOpened, sPartsWanted;
    FILE* sFile;
    unsigned i;
    if (!sOpened) {
        const char* path = getenv("TTYD_FINGERPRINT");
        sOpened = 1;
        sFpFile = path != NULL ? portFopen(path, "w") : NULL;
        if (sFpFile != NULL) {
            setvbuf(sFpFile, NULL, _IOFBF, 1 << 16);
        }
        sPartsWanted = getenv("TTYD_FINGERPRINT_PARTS") != NULL;
    }
    sFile = sFpFile;
    if (sFile != NULL && gp != NULL && !sTestEnding) {
        fprintf(sFile, "%u %016llx %s", gp->retraceCount, (unsigned long long)portOnlineFingerprint(),
                gp->currentMapName);
        if (sPartsWanted) {
            const MarioWork* mario = marioGetPtr();
            int k;
            /* (f0 and two controllers' zero bytes: where lockstep online's frame and pads went, kept
               so the files read and compare as before) */
            fprintf(sFile, " f0 r%08x", (unsigned)portRandState());
            fprintf(sFile, " c%llx", (unsigned long long)portFastClockGet());
            fputc(' ', sFile);
            for (k = 0; k < 2 * PORT_ONLINE_PAD_BYTES; k++) {
                fputs("00", sFile);
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
        if (gp->retraceCount % FP_FLUSH_EVERY == 0) {
            fflush(sFile);
        }
    }
}
