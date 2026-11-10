/* Chaos mode (port/include/port/toggles.h): every 30 seconds of play a random
 * effect, for 15 seconds or at once. In battle it is one of Crowd Control's
 * commands (port/include/port/crowd.h: the audience throws something, Star
 * Power given or taken, Bingo, a cheer), done by its own engine in the
 * battle's frame, as a viewer's command would be; in the field it is one of
 * this file's (giant or tiny Mario, the controls flipped, the camera upside
 * down, coin rain, a Bob-omb's blast) or one of the fun toggles for a while
 * (moon gravity, big heads, paper-thin, spinning).
 *
 * Off while recording or playing a replay (the effects aren't input) and
 * with console-accurate mode. Its own random numbers, never the game's,
 * seeded from the game clock at the first effect: online that clock is the
 * lockstep one (the host's start, a frame's worth a frame), read in the same
 * frame of every game, so every game of a session draws the same effects at
 * the same moments, from the host's "chaos_mode" (its settings). The flipped
 * controls flip the first controller after online's exchange, in every game. */
#include "port/toggles.h"
#include "port/platform.h"
#include "port/crowd.h"
#include "port/replay.h"
#include "port/settings.h"
#include "driver/camdrv.h"
#include "driver/itemdrv.h"
#include "driver/seqdrv.h"
#include "data/item_data.h"
#include "eff/eff_bomb.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define EVERY_FRAMES (30 * 60) /* an effect every 30 seconds of play */
#define LASTS_FRAMES (15 * 60) /* a lasting one for 15 */
#define COIN_EVERY 12          /* coin rain: a coin each this many frames */
#define NOTICE_FRAMES (3 * 60) /* a one-off effect's notice stays 3 seconds */

enum {
    CHAOS_NONE = -1,
    CHAOS_GIANT,
    CHAOS_TINY,
    CHAOS_FLIPPED,
    CHAOS_UPSIDE_DOWN,
    CHAOS_COIN_RAIN,
    CHAOS_BOMB,
    CHAOS_MOON,
    CHAOS_BIG_HEADS,
    CHAOS_PAPER_THIN,
    CHAOS_SPIN,
    CHAOS_FIELD_COUNT,
    /* in battle: Crowd Control's */
    CHAOS_GIFT = CHAOS_FIELD_COUNT,
    CHAOS_ROCK,
    CHAOS_STAR,
    CHAOS_DRAIN,
    CHAOS_BINGO,
    CHAOS_CHEER,
    CHAOS_COUNT
};

static const char* const kNames[CHAOS_COUNT] = {
    "Giant Mario",        "Tiny Mario",        "Flipped controls", "Upside down",   "Coin rain",
    "Bob-omb!",           "Moon gravity",      "Big heads",        "Paper-thin",    "Spinning paper",
    "A gift from the stands", "A rock from the stands", "A little Star Power", "Star Power drained", "Bingo!",
    "The crowd cheers",
};
static const int kCrowd[] = {PORT_CROWD_GIFT, PORT_CROWD_ROCK, PORT_CROWD_STAR,
                             PORT_CROWD_DRAIN, PORT_CROWD_BINGO, PORT_CROWD_CHEER};
static const int kFun[] = {PORT_FUN_MOON_GRAVITY, PORT_FUN_BIG_HEADS, PORT_FUN_PAPER_THIN, PORT_FUN_SPIN};

static int sEffect = CHAOS_NONE; /* the one running */
static int sLeft;                /* its frames left */
static int sUntilNext = EVERY_FRAMES;
static int sShown = CHAOS_NONE;  /* the last one started, for the notice */
static int sShownLeft;
static unsigned sSeed;
static int sFlipped;             /* portChaosPad: the controls flipped now */

static int setting(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("chaos_mode", 0, 0, 1);
    }
    return *sOn;
}

static int allowed(void) {
    return setting() && !portConsoleAccurate() && !portReplayActive() && gp != NULL;
}

/* the clock runs in the field and in battles (not on the title, a room change, a Game Over) */
static int playing(void) {
    return seqGetSeq() == SEQ_GAME || seqGetSeq() == SEQ_BATTLE;
}

static unsigned next(void) {
    if (sSeed == 0) {
        sSeed = (unsigned)portGameClockPeek() | 1u; /* (a peek: the game clock stays put) */
    }
    sSeed ^= sSeed << 13;
    sSeed ^= sSeed >> 17;
    sSeed ^= sSeed << 5;
    return sSeed;
}

/* the lasting effect put back as the game had it */
static void stop(void) {
    MarioWork* mario = marioGetPtr();
    switch (sEffect) {
        case CHAOS_GIANT:
        case CHAOS_TINY:
            if (mario != NULL) {
                mario->scale = (Vec){1.0f, 1.0f, 1.0f};
            }
            break;
        case CHAOS_UPSIDE_DOWN:
            camGetPtr(CAMERA_3D)->rollAngle = 0.0f;
            break;
        case CHAOS_MOON:
        case CHAOS_BIG_HEADS:
        case CHAOS_PAPER_THIN:
        case CHAOS_SPIN:
            portFunChaosSet(kFun[sEffect - CHAOS_MOON], 0);
            break;
        default:
            break;
    }
    sFlipped = 0;
    sEffect = CHAOS_NONE;
    sLeft = 0;
}

static void blast(void) {
    MarioWork* mario = marioGetPtr();
    if (mario != NULL) {
        effBombEntry(0, mario->position.x, mario->position.y + 20.0f, mario->position.z + 10.0f, 1.5f);
        psndSFXOn("SFX_PARTY_PBM_BOMB1");
    }
}

static void coin(void) {
    MarioWork* mario = marioGetPtr();
    if (mario != NULL) {
        const float a = (float)(next() % 360) * 0.017453292f, r = (float)(next() % 80);
        itemEntry(mario->position.x + cosf(a) * r, mario->position.y + 160.0f, mario->position.z + sinf(a) * r, NULL,
                  ITEM_COIN, ITEM_ENTRY_DROP_8S, -1, NULL);
    }
}

static void start(int effect) {
    stop();
    sShown = effect;
    sShownLeft = NOTICE_FRAMES;
    printf("[chaos] %s\n", kNames[effect]);
    fflush(stdout);
    if (effect >= CHAOS_FIELD_COUNT) {
        portCrowdPushGame(kCrowd[effect - CHAOS_FIELD_COUNT]); /* Crowd Control's engine, in the battle's frame */
        return;
    }
    switch (effect) {
        case CHAOS_BOMB:
            blast();
            return;
        case CHAOS_MOON:
        case CHAOS_BIG_HEADS:
        case CHAOS_PAPER_THIN:
        case CHAOS_SPIN:
            portFunChaosSet(kFun[effect - CHAOS_MOON], 1);
            break;
        default:
            break;
    }
    sEffect = effect;
    sLeft = LASTS_FRAMES;
    sShownLeft = LASTS_FRAMES;
}

void portChaosFrame(void) {
    MarioWork* mario;
    const int battle = gp != NULL && gp->inBattle;
    if (!allowed()) {
        if (sEffect != CHAOS_NONE) {
            stop();
        }
        sUntilNext = EVERY_FRAMES;
        sShownLeft = 0;
        return;
    }
    if (!playing()) {
        return; /* (held: the effect and the clock go on in the next room) */
    }
    if (sShownLeft > 0) {
        sShownLeft--;
    }
    if (--sUntilNext <= 0) {
        sUntilNext = EVERY_FRAMES;
        if (battle) {
            /* the battle's: Crowd Control's, or a fun toggle (they show in battle too) */
            static const int kBattlePicks[] = {CHAOS_GIFT,  CHAOS_ROCK,      CHAOS_STAR,       CHAOS_DRAIN, CHAOS_BINGO,
                                               CHAOS_CHEER, CHAOS_BIG_HEADS, CHAOS_PAPER_THIN, CHAOS_SPIN};
            start(kBattlePicks[next() % (sizeof(kBattlePicks) / sizeof(kBattlePicks[0]))]);
        } else {
            start((int)(next() % CHAOS_FIELD_COUNT));
        }
    }
    if (sEffect == CHAOS_NONE) {
        return;
    }
    /* the lasting one, kept up each frame (the game sets these back on its own at times) */
    mario = marioGetPtr();
    switch (sEffect) {
        case CHAOS_GIANT:
        case CHAOS_TINY:
            if (mario != NULL && !battle) {
                const float s = sEffect == CHAOS_GIANT ? 2.0f : 0.5f;
                mario->scale = (Vec){s, s, s};
            }
            break;
        case CHAOS_FLIPPED:
            sFlipped = 1;
            break;
        case CHAOS_UPSIDE_DOWN:
            camGetPtr(CAMERA_3D)->rollAngle = 180.0f;
            break;
        case CHAOS_COIN_RAIN:
            if (!battle && sLeft % COIN_EVERY == 0) {
                coin();
            }
            break;
        default:
            break;
    }
    if (--sLeft <= 0) {
        stop();
    }
}

void portChaosPad(PADStatus* pads) {
    /* after online's exchange (the same pads in every game of a session) and the
     * replay's recording (chaos is never in a replay: allowed) */
    if (sFlipped && allowed() && playing()) {
        pads[0].stickX = (s8)(pads[0].stickX == -128 ? 127 : -pads[0].stickX);
        pads[0].stickY = (s8)(pads[0].stickY == -128 ? 127 : -pads[0].stickY);
    }
}

const char* portChaosEffect(int* msLeft) {
    if (sShown == CHAOS_NONE || sShownLeft <= 0 || !allowed()) {
        return NULL;
    }
    *msLeft = sShownLeft * 1000 / 60;
    return kNames[sShown];
}

int portChaosNext(void) {
    return allowed() ? (sUntilNext + 59) / 60 : -1;
}
