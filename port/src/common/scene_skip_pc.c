/* Skip any scene, and fly through text (port/include/port/scene_skip.h).
 *
 * A scene, here: the field (SEQ_GAME, a file loaded) with Mario held by an
 * event (marioChkKey off), and nothing on the screen that waits for the
 * player's choice: no choice window (msgdrv.c portMsgChoiceOpen), no name
 * entry, no pause menu, and no script or minigame that read the pad in the
 * last half second (portSceneSkipListening: evt_key_get_*, the winMgr lists,
 * the unit windows, the shop's prize list, the Trouble Center's board, the
 * rankings, the slots, Peach's dance). The Pianta Parlor's game rooms (yuu_)
 * never count.
 *
 * START pressed in a scene and held for HOLD_FRAMES fills the ring; full, the
 * skip runs: the player's own input is held back (the skip's presses only),
 * each waiting message gets A (a page turned, a printing one printed at once),
 * the few "press A" waits outside messages press on (portSceneSkipAdvance:
 * the item and coin boxes, the mail notice, the opening's logo), and the
 * Aurora side runs the frames at SKIP_SPEED, undrawn but one in DRAW_EVERY,
 * silent (a frame with a draw pass the frame before hadn't is drawn all the
 * same: a pass made once, as the X-Naut crowd's sprite textures, would
 * otherwise never be made: battery_aurora.c newDrawPass). It ends when Mario has control again (a few frames running; after a
 * map change, a moment more), and stops early, back to normal, at a choice, a
 * list, a name entry, a battle (which starts as ever), a script that reads
 * the pad, or after CAP_FRAMES. START stays held back until it is let go (so
 * the pause menu doesn't open as control comes back).
 *
 * B pressed during a skip (a fresh press on any of the four pads, read before
 * the hold-back: a controller's B, the keyboard's key for it, the on-screen B)
 * stops it the same way, the scene playing on as ever from there; that B is
 * held back from the game until it is let go (it doesn't also cancel or close
 * anything, nor start Fly through text). Online and in replays no skip runs,
 * so there is nothing to stop.
 *
 * B held in a conversation (text_hold_skip): after a moment, B pressed for the
 * player every other frame while a message is up, up to a choice. */
#include <stdio.h>
#include <string.h>
#include "port/achievements.h"
#include "port/scene_skip.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/replay.h"
#include "port/tips.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "win/win_main.h"
#include <dolphin/pad.h>

#define HOLD_FRAMES 45      /* START held this long fills the ring (3/4 s) */
#define SKIP_SPEED 6400     /* the game's clock while skipping, % (Aurora) */
#define DRAW_EVERY 16       /* while skipping, one frame in this many is drawn (the ring keeps turning) */
#define LISTEN_FRAMES 30    /* a pad read this recent: the scene listens to the player */
#define KEY_BACK_FRAMES 3   /* Mario in control this many frames running: the scene is over */
#define MAP_GRACE_FRAMES 30 /* after a map change, control may come and go while the next map starts */
#define CAP_FRAMES (60 * 60 * 5) /* the longest skip: five minutes of the scene's frames */
#define HINT_AFTER 40       /* the first skippable scene (once per install): its ring hint after this long */
#define HINT_FRAMES 300     /* and for this long, or until the scene ends or START is held */

int portMsgWaiting(int* sel, int* numSel, const char** tag); /* msgdrv.c */
int portMsgChoiceOpen(void);                                  /* msgdrv.c */
int portNameEntryOpen(void);                                  /* nameent.c */

static u32 sFrame;          /* makeKey's frames */
static u32 sListenFrame;    /* the frame of the last pad read by a script or minigame (0 none) */
static int sHold;           /* frames START held in a scene (0: not holding) */
static int sStartLatch;     /* START held back until it is let go */
static int sSkipping;       /* a skip under way */
static int sSkipFrames;     /* its frames so far */
static int sKeyBack;        /* frames running Mario has had control */
static int sGrace;          /* frames of a new map's start to wait out */
static int sTextHold;       /* frames B held in a conversation */
static int sFlyTold;        /* this hold's "[sceneskip] flying through text" said */
static int sSceneFor;       /* frames this scene has been skippable, nothing held */
static int sHint;           /* frames the first-time hint has been up (0 none) */
static u32 sBDown;          /* the pads (bit i: pad i) with B down last frame, as read before the hold-back */
static int sBLatch;         /* B held back from the game until every pad lets it go (it stopped a skip) */

static int setting(const char* key, int** cache) {
    if (*cache == NULL) {
        *cache = portSettingsDeclare(key, 0, 0, 1);
    }
    return *cache != NULL && **cache != 0;
}

static int skipOn(void) {
    static int* sOn;
    return setting("skip_any_scene", &sOn);
}

static int textOn(void) {
    static int* sOn;
    return setting("text_hold_skip", &sOn);
}

/* never online (each player's game would be at a different place), nor in a replay (its frames are the
 * recording's) */
static int allowed(void) {
    return gp != NULL && !portStateSyncOn() && !portReplayActive();
}

static int listening(u32 frames) {
    return sListenFrame != 0 && sFrame - sListenFrame <= frames;
}

static int partyRoom(void) {
    return strncmp(gp->currentMapName, "yuu_", 4) == 0; /* the Pianta Parlor's games */
}

/* nothing on the screen waits for the player's say */
static int nothingAsked(u32 listenFrames) {
    return !portMsgChoiceOpen() && !portNameEntryOpen() && !winCheck() && !listening(listenFrames);
}

static int sceneNow(void) {
    return skipOn() && allowed() && seqGetSeq() == SEQ_GAME && pouchGetPtr() != NULL && !marioChkKey() &&
           !partyRoom() && nothingAsked(LISTEN_FRAMES);
}

void portSceneSkipListening(void) {
    sListenFrame = sFrame != 0 ? sFrame : 1;
}

int portSceneSkipAdvance(void) {
    return sSkipping && (sSkipFrames & 1);
}

int portSceneSkipSpeedPct(void) {
    return sSkipping ? SKIP_SPEED : 100;
}

int portSceneSkipUndrawn(void) {
    return sSkipping && sFrame % DRAW_EVERY != 0;
}

int portSceneSkipMuted(void) {
    return sSkipping;
}

int portSceneSkipRing(float* fill) {
    if (sHint > 0 && !sSkipping && sHold == 0) {
        *fill = (float)sHint; /* (the hint's frames so far: its fade) */
        return 3;
    }
    if (sSkipping) {
        *fill = 1.0f;
        return 2;
    }
    if (sHold > 0) {
        *fill = (float)sHold / (float)HOLD_FRAMES;
        return 1;
    }
    *fill = 0.0f;
    return 0;
}

static void stopSkip(const char* why) {
    printf("[sceneskip] %s after %d frames (map '%s', Mario %s)\n", why, sSkipFrames, gp->currentMapName,
           marioChkKey() ? "in control" : "held");
    fflush(stdout);
    sSkipping = 0;
    sSkipFrames = 0;
    sStartLatch = 1; /* (START still down: the pause menu waits for it to be let go) */
}

/* the reason to stop the skip under way now, or NULL to go on */
static const char* stopReason(void) {
    const s32 seq = (s32)seqGetSeq();
    if (!skipOn() || !allowed()) {
        return "stopped: off here";
    }
    if (seq == SEQ_MAP_CHANGE) {
        sGrace = MAP_GRACE_FRAMES;
        return NULL;
    }
    if (seq != SEQ_GAME || pouchGetPtr() == NULL) {
        return seq == SEQ_BATTLE ? "stopped: a battle" : "stopped: left the field";
    }
    if (portMsgChoiceOpen()) {
        return "stopped: a choice";
    }
    if (portNameEntryOpen()) {
        return "stopped: a name entry";
    }
    if (winCheck()) {
        return "stopped: a menu";
    }
    if (listening(1) || partyRoom()) {
        return "stopped: the scene listens to the player";
    }
    if (marioChkKey()) {
        if (sGrace > 0) {
            sGrace--;
        } else if (++sKeyBack >= KEY_BACK_FRAMES) {
            return "done: control back";
        }
    } else {
        sKeyBack = 0;
    }
    if (sSkipFrames >= CAP_FRAMES) {
        return "stopped: too long";
    }
    return NULL;
}

/* the player's own input this frame held back (the skip's presses alone) */
static void quietPad(void) {
    gp->button[0] = 0;
    gp->buttonNew[0] = 0;
    gp->buttonRepeat[0] = 0;
    gp->dirs[0] = 0;
    gp->dirsNew[0] = 0;
    gp->dirsRepeat[0] = 0;
    gp->stickX[0] = 0;
    gp->stickY[0] = 0;
    gp->substickX[0] = 0;
    gp->substickY[0] = 0;
    gp->triggerLeft[0] = 0;
    gp->triggerRight[0] = 0;
}

static void skipStep(void) {
    int sel = 0, num = 0, kind;
    const char* tag = NULL;
    const char* why = stopReason();
    if (why != NULL) {
        stopSkip(why);
        return;
    }
    sSkipFrames++;
    quietPad();
    kind = portMsgWaiting(&sel, &num, &tag);
    if ((kind == 1 || kind == 3) && (sSkipFrames & 1)) {
        /* a page waiting turns; one printing prints at once (A on a message, as a player's) */
        gp->button[0] |= PAD_BUTTON_A;
        gp->buttonNew[0] |= PAD_BUTTON_A;
    }
}

/* the first scene that can be skipped while the setting is on, once per install (port/include/port/tips.h):
 * the ring's hint, "Hold {START} to skip", a few seconds */
static void hintStep(int scene) {
    if (!scene || sHold > 0 || sSkipping) {
        sSceneFor = 0;
        sHint = 0;
        return;
    }
    if (sHint > 0) {
        sHint = sHint >= HINT_FRAMES ? 0 : sHint + 1;
        return;
    }
    if (++sSceneFor == HINT_AFTER && !portTipSeen(PORT_TIP_SCENE_SKIP) && portTipsAllowed() &&
        portTipOffer(PORT_TIP_SCENE_SKIP)) {
        sHint = 1;
    }
}

static void holdStep(u32 held, u32 trg) {
    const int scene = sceneNow();
    hintStep(scene);
    if (!scene) {
        sHold = 0;
        return;
    }
    if (sHold == 0) {
        if (trg & PAD_BUTTON_START) {
            sHold = 1; /* (a fresh press: START already down as a scene began doesn't count) */
            sStartLatch = 1;
        }
        return;
    }
    if (!(held & PAD_BUTTON_START)) {
        sHold = 0;
        return;
    }
    if (++sHold >= HOLD_FRAMES) {
        sHold = 0;
        sSkipping = 1;
        sSkipFrames = 0;
        sKeyBack = 0;
        sGrace = 0;
        printf("[sceneskip] skipping from map '%s'\n", gp->currentMapName);
        fflush(stdout);
        portAchievementCounterAdd("scene_skips", 1); /* (port/include/port/achievements.h) */
    }
}

static void textStep(u32 held) {
    int sel = 0, num = 0, kind;
    const char* tag = NULL;
    if (!textOn() || !allowed() || sSkipping || seqGetSeq() != SEQ_GAME || !(held & PAD_BUTTON_B) ||
        marioChkKey() || portMsgChoiceOpen()) {
        sTextHold = 0;
        sFlyTold = 0;
        return;
    }
    /* a moment first, so a tap of B is the original's; then a press every other frame while a message is up
     * (between two, B still held, it goes on at the next at once) */
    sTextHold++;
    kind = portMsgWaiting(&sel, &num, &tag);
    if ((kind == 1 || kind == 3) && sTextHold > 12 && (sTextHold & 1)) {
        if (!sFlyTold) {
            sFlyTold = 1;
            printf("[sceneskip] flying through text (B held, map '%s')\n", gp->currentMapName);
            fflush(stdout);
        }
        gp->buttonNew[0] |= PAD_BUTTON_B;
    }
}

/* the pads with B down now, and those whose B went down this frame (from the pads as read, not
 * gp->buttonNew: during a skip the hold-back zeroes port 0 each frame, so a held B would look new) */
static u32 readB(u32* fresh) {
    u32 down = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (gp->button[i] & PAD_BUTTON_B) {
            down |= 1u << i;
        }
    }
    *fresh = down & ~sBDown;
    sBDown = down;
    return down;
}

/* B kept from the game on every pad until it is let go */
static void bLatchStep(u32 down) {
    int i;
    if (!sBLatch) {
        return;
    }
    if (down == 0) {
        sBLatch = 0;
        return;
    }
    for (i = 0; i < 4; i++) {
        gp->button[i] &= ~PAD_BUTTON_B;
        gp->buttonNew[i] &= ~PAD_BUTTON_B;
        gp->buttonRepeat[i] &= ~PAD_BUTTON_B;
    }
}

void portSceneSkipFrame(void) {
    u32 held, trg, bDown, bFresh;
    sFrame++;
    if (gp == NULL) {
        return;
    }
    held = gp->button[0];
    trg = gp->buttonNew[0];
    bDown = readB(&bFresh);
    if (sSkipping && bFresh != 0) {
        stopSkip("stopped by B"); /* (the player's way out: the scene plays on from here) */
        sBLatch = 1;
        printf("[sceneskip] back to normal: speed %d%%, %s, sound %s\n", portSceneSkipSpeedPct(),
               portSceneSkipUndrawn() ? "frames undrawn" : "every frame drawn", portSceneSkipMuted() ? "off" : "on");
        fflush(stdout);
    } else if (sSkipping) {
        skipStep();
    } else {
        holdStep(held, trg);
    }
    bLatchStep(bDown);
    if (sStartLatch) {
        if (!(held & PAD_BUTTON_START)) {
            sStartLatch = 0;
        } else {
            gp->button[0] &= ~PAD_BUTTON_START;
            gp->buttonNew[0] &= ~PAD_BUTTON_START;
            gp->buttonRepeat[0] &= ~PAD_BUTTON_START;
        }
    }
    textStep(gp->button[0]);
}
