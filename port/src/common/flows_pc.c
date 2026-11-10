/* The device flows (tests: tests/port/testpass/flows_*.json, docs/TESTING.md): an input script's "dev"
 * steps play a session as a player's own devices would, not as the script's controller 1:
 *
 *   dev wait N                  N frames with controller 1 left to the devices (the script's plain steps
 *                               write controller 1 every frame; a dev step never does)
 *   dev pad N attach [KIND]     a virtual SDL controller N (1-4) plugged in: xbox (default), ps, switch, deck
 *   dev pad N detach            ... switched off
 *   dev pad N KEYS F            KEYS held on it for F frames, then let go (a b x y z l r start back,
 *                               dup ddown dleft dright, left right up down the stick, joined with '+'; the
 *                               GameCube's buttons as the port maps them, or south east west north: the
 *                               controller's own face buttons, as Paper Cut's own pages read them)
 *   dev key KEY F               a keyboard key held F frames: a GameCube button's name as the keyboard's
 *                               port has it bound, or esc, enter, tab, f1, backspace
 *   dev touch CONTROL F         a finger on a touch control F frames (a b x y z l r start menu swap,
 *                               stick-left stick-right stick-up stick-down: the touch build)
 *   dev finger NX NY F          a finger at window fraction NX, NY (a tap on a menu's row)
 *   dev until WHAT [MAX]        devices' frames until WHAT: field, battle, menu, nomenu, settled, players=N,
 *                               seq=N (seqdrv.h: 1 title, 6 file select), map=NAME (default at most 3600
 *                               frames; not reached: "[flows] FAIL")
 *   dev mash DEVICE KEY WHAT [MAX]  the same, KEY pressed every 24 frames meanwhile on DEVICE: pad N, key or
 *                               touch (a title, a file menu, a battle played with A)
 *   dev watch N                 the motion watch on: Mario's and each party member's motion logged as it
 *                               changes ("[motion] ..."), and a jump, fall, hop or hammer not back to standing
 *                               or walking within N frames (anything else within 6 N) is "[flows] STUCK"
 *   dev state [LABEL]           the devices' state now, one line ("[flows] state LABEL: ...")
 *   dev ifnot COND then STEP    STEP (a dev step) only when COND doesn't hold: a player's own way out after
 *                               a check failed, so the rest of the flow still runs
 *   dev online leave           the Online page's Leave; dev online join CODE: its Join with a room's code
 *   dev mark                    the partner's place noted, its hops counted from here (partner moved, hopped)
 *   dev editor                  the touch layout editor opened, as its button does (the touch build)
 *   dev back                    a right click with a menu up (the mouse's back in the game's menus)
 *   dev expect COND             "[flows] ok: COND" or "[flows] FAIL: COND (...)": pad N port P (P 1-4 or
 *                               none), keyboard port P, players N, touch shown|hidden, menu open|closed,
 *                               page open|closed (the settings page), editor open|closed (the touch layout
 *                               editor), settled, control (the field, Mario's keys on, no menu: a talk
 *                               over), seq field|battle, partner N (the member out), partner moved
 *                               (20 units or more from the mark), partner hopped (since the mark; the watch on)
 *
 * Each step after the first frame of a press or a key adds 6 frames of nothing held, so a press's release
 * reaches the game before the next one. testpass.py fails a run with any "[flows] FAIL" or "STUCK" line.
 * The devices are Aurora's (port/settings/features/flows_devices.cpp); the headless build has none, and a
 * device step there fails. */
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/hitdrv.h"
#include "evt/evt_msg.h"
#include "evt/evt_window.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "party.h"
#include "port/platform.h"
#include "port/menu_tap.h"
#include "port/players.h"
#include "port/statesync.h"
#include "win/win_main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TTYD_BACKEND_AURORA
int portFlowsPadAttach(int n, const char* kind); /* port/settings/features/flows_devices.cpp */
void portFlowsPadDetach(int n);
int portFlowsPadSet(int n, const char* keys, int down);
int portFlowsPadPort(int n);
int portFlowsKey(const char* key, int down);
int portFlowsTouchShown(void);
int portFlowsTouch(const char* name, float x, float y, int down);
int portFlowsPageOpen(void);
int portFlowsEditor(int open);
#endif

/* the settings page (F1): 1 open, 0 closed, -1 none (headless) */
static int pageOpen(void) {
#ifdef TTYD_BACKEND_AURORA
    return portFlowsPageOpen();
#else
    return -1;
#endif
}

/* the touch layout editor: 1 open, 0 closed, -1 none in this build; open: asked open now */
static int editorOpen(int open) {
#ifdef TTYD_BACKEND_AURORA
    return portFlowsEditor(open);
#else
    (void)open;
    return -1;
#endif
}

#define RELEASE_GAP 6
#define ACTORS (1 + PORT_PARTY_SLOTS)

/* the press being held: let go when its frames are up */
static struct {
    int left;  /* frames until it is let go (0 none) */
    int kind;  /* 1 pad, 2 key, 3 touch control, 4 finger */
    int pad;
    char what[32];
    float x, y;
} sHeld;

/* an "until" step's condition (0 none) and its frames */
static char sUntil[32];
static int sUntilLeft, sUntilMet;

/* the motion watch */
static int sWatch; /* its limit in frames (0 off) */
static struct {
    int motion; /* last seen (-1 none there) */
    int busy;   /* 0 at rest (standing, walking), 1 quick (a jump, a hop...), 2 slow (anything else) */
    int frames; /* frames counted in this busy spell */
    int stuck;  /* said so this spell */
} sActor[ACTORS];
static int sSettledFrames;
/* dev mark: the partner's place then, and whether it has hopped (a jump motion) since */
static float sMarkX, sMarkZ;
static int sPartnerHopped;
static unsigned sClock; /* game frames seen (portFlowsFrame): the motion lines' */

static void say(const char* fmt, const char* a, const char* b) {
    printf(fmt, a, b);
    fflush(stdout);
}

/* the motion class of Mario's motion m */
static int marioBusy(int m) {
    switch (m) {
    case MARIO_MOTION_STAY:
    case MARIO_MOTION_WALK:
    case MARIO_MOTION_DASH:
        return 0;
    case MARIO_MOTION_JUMP:
    case MARIO_MOTION_JUMP_NPC:
    case MARIO_MOTION_JUMP_SW:
    case MARIO_MOTION_JUMP_STAND:
    case MARIO_MOTION_JUMP_2:
    case MARIO_MOTION_JUMP_3:
    case MARIO_MOTION_JUMP_SMALL:
    case MARIO_MOTION_FALL:
    case MARIO_MOTION_FALL_2:
    case MARIO_MOTION_UPSTAIRS:
    case MARIO_MOTION_LAND:
    case MARIO_MOTION_HIP:
    case MARIO_MOTION_HIP_2:
    case MARIO_MOTION_HAMMER:
    case MARIO_MOTION_HAMMER_2:
    case MARIO_MOTION_DAMAGE:
    case MARIO_MOTION_DAMAGE_TOGE:
        return 1;
    default:
        return 2;
    }
}

/* a party member's: its walk mode's motions (party_move.c) 0 stand, 1 walk, 2 jump, 3 fall, 6 upstairs,
   7 damage; in the air (jumping, falling) is a quick motion whatever its number; in use (its field move) slow */
static int partyBusy(const PartyEntry* p) {
    if (p->flags & PARTY_FLAG_IS_BEING_USED) {
        return 2;
    }
    if ((p->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING)) || p->motionId == 2 || p->motionId == 3 ||
        p->motionId == 6 || p->motionId == 7) {
        return 1;
    }
    return p->motionId <= 1 ? 0 : 2;
}

static void actorName(int i, char* out, size_t size) {
    const PartyEntry* p = i > 0 ? partyGetPtr(i - 1) : NULL;
    if (i == 0) {
        snprintf(out, size, "mario");
    } else if (p != NULL) {
        snprintf(out, size, "party slot %d (member %d%s)", i - 1, (int)p->currentMemberId,
                 i - 1 == marioGetPartyId() ? ", the partner" : "");
    } else {
        snprintf(out, size, "party slot %d", i - 1);
    }
}

/* the field with its player in control, no menu up: where a motion should come back to rest */
static int watchCounts(void) {
    const WindowWork* w = winGetPtr();
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && (w == NULL || w->menuState == 0) &&
           marioChkKey();
}

static int allSettled(void) {
    int i;
    for (i = 0; i < ACTORS; i++) {
        if (sActor[i].motion >= 0 && sActor[i].busy != 0) {
            return 0;
        }
    }
    return 1;
}

static void watchFrame(void) {
    const int counts = watchCounts();
    int i;
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        for (i = 0; i < ACTORS; i++) {
            sActor[i].motion = -1; /* (a battle, a map change: each starts again) */
        }
        sSettledFrames = 0;
        return;
    }
    for (i = 0; i < ACTORS; i++) {
        const MarioWork* m = i == 0 ? marioGetPtr() : NULL;
        const PartyEntry* p = i > 0 ? partyGetPtr(i - 1) : NULL;
        int motion, busy;
        char who[48];
        float x, y, z;
        if (i == 0 && m != NULL) {
            motion = (int)m->currMotionId;
            busy = marioBusy(motion);
            x = m->position.x, y = m->position.y, z = m->position.z;
        } else if (p != NULL && (p->flags & PARTY_FLAG_IS_ACTIVE)) {
            motion = (int)p->motionId;
            busy = partyBusy(p);
            x = p->position.x, y = p->position.y, z = p->position.z;
            if (i - 1 == marioGetPartyId() && motion == 2) {
                sPartnerHopped = 1; /* (dev expect partner hopped) */
            }
        } else {
            sActor[i].motion = -1;
            continue;
        }
        actorName(i, who, sizeof(who));
        if (sActor[i].motion < 0) {
            sActor[i].motion = motion;
            sActor[i].busy = busy;
            sActor[i].frames = 0;
            sActor[i].stuck = 0;
            continue;
        }
        if (motion != sActor[i].motion || busy != sActor[i].busy) {
            if (busy == 0 && sActor[i].busy != 0) {
                printf("[motion] frame %u: %s: back to %d after %d frames (from %d)\n", sClock, who, motion,
                       sActor[i].frames, sActor[i].motion);
                sActor[i].frames = 0;
                sActor[i].stuck = 0;
            } else if (busy != 0) {
                printf("[motion] frame %u: %s: %d (%s)\n", sClock, who, motion, busy == 1 ? "quick" : "slow");
                if (sActor[i].busy == 0) {
                    sActor[i].frames = 0;
                    sActor[i].stuck = 0;
                }
            }
            fflush(stdout);
            sActor[i].motion = motion;
            sActor[i].busy = busy;
        }
        if (busy != 0 && counts) {
            const int limit = busy == 1 ? sWatch : sWatch * 6;
            if (++sActor[i].frames > limit && !sActor[i].stuck) {
                sActor[i].stuck = 1;
                printf("[flows] STUCK: %s in motion %d for %d frames at (%.1f, %.1f, %.1f)\n", who, motion,
                       sActor[i].frames, x, y, z);
                fflush(stdout);
            }
        }
    }
    sSettledFrames = allSettled() ? sSettledFrames + 1 : 0;
}

static const char* seqName(void) {
    const int s = seqGetSeq();
    return s == SEQ_GAME ? (gp != NULL && gp->inBattle ? "battle" : "field") : s == SEQ_BATTLE ? "battle" : "other";
}

int portMsgWaiting(int* sel, int* numSel, const char** tag); /* msgdrv.c */

/* a talk's choice up, taking keys */
static int choiceOpen(void) {
    int sel = 0, num = 0;
    const char* tag = NULL;
    return portMsgWaiting(&sel, &num, &tag) == 2;
}

static int menuOpen(void) {
    const WindowWork* w = winGetPtr();
    return seqGetSeq() == SEQ_GAME && w != NULL && w->menuState > 0 && w->menuState < 100;
}

static int padPort(int n) {
#ifdef TTYD_BACKEND_AURORA
    return portFlowsPadPort(n);
#else
    (void)n;
    return -1;
#endif
}

static int touchShown(void) {
#ifdef TTYD_BACKEND_AURORA
    return portFlowsTouchShown();
#else
    return -1;
#endif
}

/* the field's talk (or sign) event still running: the user function it waits in, by name for the window ones */
static const char* talkWaits(void) {
    static char buf[48];
    static const struct {
        UserFunction fn;
        const char* name;
    } kNames[] = {{evt_msg_print, "evt_msg_print"},       {evt_msg_select, "evt_msg_select"},
                  {evt_msg_continue, "evt_msg_continue"}, {evt_win_other_select, "evt_win_other_select"},
                  {evt_win_coin_off, "evt_win_coin_off"}, {evt_win_coin_wait, "evt_win_coin_wait"}};
    const EventEntry* e = evtGetPtrID(gp->fbatData->evtId);
    size_t i;
    if (e == NULL) {
        return "running";
    }
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        if (e->userfunc == kNames[i].fn) {
            snprintf(buf, sizeof(buf), "running, in %s", kNames[i].name);
            return buf;
        }
    }
    snprintf(buf, sizeof(buf), "running, in %p", (void*)e->userfunc);
    return buf;
}

static void stateLine(char* out, size_t size) {
    const WindowWork* w = winGetPtr();
    int n, len;
    const PartyEntry* partner = partyGetPtr(marioGetPartyId());
    len = snprintf(out, size, "players %d, partner %d, keyboard port %d,", portCoopPlayers(),
                   partner != NULL ? (int)partner->currentMemberId : 0, portInputHomePort() + 1);
    for (n = 1; n <= 4 && len > 0 && (size_t)len < size; n++) {
        const int p = padPort(n);
        if (p >= 0) {
            len += snprintf(out + len, size - (size_t)len, " pad %d port %d,", n, p + 1);
        }
    }
    if (len > 0 && (size_t)len < size) {
        const int t = touchShown();
        const MarioWork* m = marioGetPtr();
        snprintf(out + len, size - (size_t)len,
                 " touch %s, menu %d, %s (seq %d), settled %d, Mario at %.0f %.0f %.0f, keys off %d, ctrl off %d, "
                 "field mode %d.%d (event %d %s)",
                 t < 0 ? "none" : t ? "shown" : "hidden", w != NULL ? (int)w->menuState : -1, seqName(), seqGetSeq(),
                 allSettled(), m != NULL ? m->position.x : 0.f, m != NULL ? m->position.y : 0.f,
                 m != NULL ? m->position.z : 0.f, m != NULL ? (int)m->keyOffCount : -1, marioCtrlOffChk(),
                 gp != NULL && gp->fbatData != NULL ? (int)gp->fbatData->mode : -1,
                 gp != NULL && gp->fbatData != NULL ? (int)gp->fbatData->subState : -1,
                 gp != NULL && gp->fbatData != NULL ? (int)gp->fbatData->evtId : -1,
                 gp != NULL && gp->fbatData != NULL && evtCheckID(gp->fbatData->evtId) ? talkWaits() : "over");
    }
}

/* COND true now (expect, until) */
static int holds(const char* cond) {
    int n = 0, p = 0;
    char word[16] = "";
    if (sscanf(cond, "pad %d port %15s", &n, word) == 2) {
        return strcmp(word, "none") == 0 ? padPort(n) < 0 : padPort(n) == atoi(word) - 1;
    }
    if (sscanf(cond, "keyboard port %d", &p) == 1) {
        return portInputHomePort() == p - 1;
    }
    if (sscanf(cond, "players %d", &n) == 1 || sscanf(cond, "players=%d", &n) == 1) {
        return portCoopPlayers() == n;
    }
    if (sscanf(cond, "touch %15s", word) == 1) {
        return touchShown() == (strcmp(word, "shown") == 0 ? 1 : 0);
    }
    if (strcmp(cond, "menu open") == 0 || strcmp(cond, "menu") == 0) {
        return menuOpen();
    }
    if (strcmp(cond, "menu closed") == 0 || strcmp(cond, "nomenu") == 0) {
        return !menuOpen();
    }
    if (strcmp(cond, "choice open") == 0 || strcmp(cond, "choice") == 0) {
        return choiceOpen();
    }
    if (strcmp(cond, "choice closed") == 0) {
        return !choiceOpen();
    }
    if (strcmp(cond, "page open") == 0 || strcmp(cond, "page closed") == 0) {
        return pageOpen() == (cond[5] == 'o' ? 1 : 0);
    }
    if (strcmp(cond, "editor open") == 0 || strcmp(cond, "editor closed") == 0) {
        return editorOpen(0) == (cond[7] == 'o' ? 1 : 0);
    }
    if (strcmp(cond, "settled") == 0) {
        return sSettledFrames >= 2;
    }
    if (strcmp(cond, "control") == 0) {
        return watchCounts(); /* (a talk over: Mario free to walk, no window or menu up) */
    }
    if (sscanf(cond, "seq %15s", word) == 1) {
        return strcmp(seqName(), word) == 0;
    }
    if (strcmp(cond, "field") == 0 || strcmp(cond, "battle") == 0) {
        return strcmp(seqName(), cond) == 0;
    }
    if (strcmp(cond, "partner hopped") == 0) {
        return sPartnerHopped;
    }
    if (strcmp(cond, "partner moved") == 0) {
        const PartyEntry* p = partyGetPtr(marioGetPartyId());
        return p != NULL && (p->position.x - sMarkX) * (p->position.x - sMarkX) +
                                    (p->position.z - sMarkZ) * (p->position.z - sMarkZ) > 20.f * 20.f;
    }
    if (sscanf(cond, "partner %d", &n) == 1) {
        const PartyEntry* p = partyGetPtr(marioGetPartyId());
        return p != NULL && p->currentMemberId == n;
    }
    if (strcmp(cond, "floor hazard") == 0 || strcmp(cond, "floor safe") == 0) {
        /* the floor under Mario spiked or burning now (a timed trap's cycle differs by disc: tik_21's) */
        MarioWork* m = marioGetPtr();
        const int hazard = m != NULL && m->hitobjStandOn != NULL &&
                           (hitGetAttr(m->hitobjStandOn) & (HIT_ATTR_SPIKES | HIT_ATTR_DAMAGE_LIQUID)) != 0;
        return m != NULL && m->hitobjStandOn != NULL && hazard == (cond[6] == 'h');
    }
    if (sscanf(cond, "seq=%d", &n) == 1) {
        return seqGetSeq() == n;
    }
    if (sscanf(cond, "map=%15s", word) == 1) {
        return gp != NULL && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, word) == 0;
    }
    return -1;
}

/* a device's press (kind 1 pad, 2 key, 3 touch control) held or let go: 0 none such */
static int devPress(int kind, int pad, const char* what, int down) {
#ifdef TTYD_BACKEND_AURORA
    switch (kind) {
    case 1:
        return portFlowsPadSet(pad, what, down);
    case 2:
        return portFlowsKey(what, down);
    case 3:
        return portFlowsTouch(what, 0.f, 0.f, down);
    default:
        return 0;
    }
#else
    (void)kind, (void)pad, (void)what, (void)down;
    return 0;
#endif
}

static void release(void) {
    if (sHeld.kind == 4) {
#ifdef TTYD_BACKEND_AURORA
        portFlowsTouch(NULL, sHeld.x, sHeld.y, 0);
#endif
    } else {
        devPress(sHeld.kind, sHeld.pad, sHeld.what, 0);
    }
    sHeld.left = 0;
    sHeld.kind = 0;
}

/* dev mash: a press every MASH_EVERY frames (held MASH_HOLD) until its "until" is met */
#define MASH_EVERY 24
#define MASH_HOLD 4
static struct {
    int kind, pad, clock;
    char what[16];
} sMash;

static void mashFrame(void) {
    if (sMash.kind == 0) {
        return;
    }
    if (sUntil[0] == '\0') {
        devPress(sMash.kind, sMash.pad, sMash.what, 0);
        sMash.kind = 0;
        return;
    }
    if (sMash.clock % MASH_EVERY == 0) {
        devPress(sMash.kind, sMash.pad, sMash.what, 1);
    } else if (sMash.clock % MASH_EVERY == MASH_HOLD) {
        devPress(sMash.kind, sMash.pad, sMash.what, 0);
    }
    sMash.clock++;
}

/* each game frame (portInputApply): a held press let go when its time is up, an "until" checked, the watch */
void portFlowsFrame(void) {
    sClock++;
    if (sHeld.left > 0 && --sHeld.left == 0) {
        release();
    }
    mashFrame();
    if (sWatch > 0) {
        watchFrame();
    }
    if (sUntil[0] != '\0') {
        if (!sWatch && strcmp(sUntil, "settled") == 0) {
            watchFrame();
        }
        if (holds(sUntil) == 1) {
            say("[flows] reached: %s%s\n", sUntil, "");
            sUntil[0] = '\0';
            sUntilMet = 1;
        } else if (--sUntilLeft <= 0) {
            char line[384];
            stateLine(line, sizeof(line));
            say("[flows] FAIL: until %s not reached (%s)\n", sUntil, line);
            sUntil[0] = '\0';
            sUntilMet = 1;
        }
    }
}

/* an "until" step is over (input_pc.c ends the step) */
int portFlowsStepDone(void) {
    const int met = sUntilMet;
    sUntilMet = 0;
    return met;
}

/* an "until" begins: WHAT [MAX]; its step's frames */
static int startUntil(const char* what) {
    char* sp;
    while (*what == ' ') {
        what++;
    }
    snprintf(sUntil, sizeof(sUntil), "%s", what);
    sUntilLeft = 3600;
    /* a number at its end: at most that many frames */
    sp = strrchr(sUntil, ' ');
    if (sp != NULL && atoi(sp + 1) > 0) {
        sUntilLeft = atoi(sp + 1);
        *sp = '\0';
    }
    if (holds(sUntil) < 0) {
        say("[flows] FAIL: until %s: no such check%s\n", sUntil, "");
        sUntil[0] = '\0';
        return 0;
    }
    sUntilMet = 0;
    return sUntilLeft + 1;
}

/* a "dev" step begins (input_pc.c): its frames (0: done at once) */
int portFlowsStep(const char* args) {
    char cmd[16] = "", a1[32] = "", a2[32] = "", a3[16] = "";
    int n = 0;
    sscanf(args, "%15s %31s %31s %15s", cmd, a1, a2, a3);
    if (sHeld.left > 0) {
        release(); /* (a step cut short: nothing stays held into the next) */
    }
    if (sMash.kind != 0) {
        devPress(sMash.kind, sMash.pad, sMash.what, 0);
        sMash.kind = 0;
    }
    sUntil[0] = '\0';
    if (strcmp(cmd, "wait") == 0) {
        return atoi(a1);
    }
    if (strcmp(cmd, "watch") == 0) {
        sWatch = atoi(a1);
        return 0;
    }
    if (strcmp(cmd, "ifnot") == 0) {
        /* ifnot COND then STEP: STEP only when COND doesn't hold (a player's way out after a failed check) */
        const char* then = strstr(args, " then ");
        char cond[48];
        if (then == NULL) {
            say("[flows] FAIL: ifnot without then: %s%s\n", args, "");
            return 0;
        }
        snprintf(cond, sizeof(cond), "%.*s", (int)(then - (args + 6)), args + 6);
        if (holds(cond) == 1) {
            return 0;
        }
        say("[flows] %s doesn't hold: %s\n", cond, then + 6);
        return portFlowsStep(then + 6);
    }
    if (strcmp(cmd, "online") == 0) {
        /* online leave | online join CODE: the Online page's Leave, or its Join with a room's code typed */
        if (strcmp(a1, "leave") == 0) {
            portStateSyncLeave();
            say("[flows] online: left%s%s\n", "", "");
        } else if (strcmp(a1, "join") == 0 && a2[0]) {
            const int on = portStateSyncJoin(a2);
            say("[flows] online: join %s: %s\n", a2, on ? "on" : portStateSyncNote());
        } else {
            say("[flows] FAIL: online %s: leave or join CODE%s\n", a1, "");
        }
        return 0;
    }
    if (strcmp(cmd, "back") == 0) {
        portMenuBack(); /* a right click (or the mouse's back button) with a menu up (menu_mouse.cpp) */
        return RELEASE_GAP;
    }
    if (strcmp(cmd, "editor") == 0) {
        if (editorOpen(1) < 0) {
            say("[flows] FAIL: editor: no touch controls in this build%s%s\n", "", "");
        }
        return 0;
    }
    if (strcmp(cmd, "mark") == 0) {
        const PartyEntry* p = partyGetPtr(marioGetPartyId());
        sMarkX = p != NULL ? p->position.x : 0.f;
        sMarkZ = p != NULL ? p->position.z : 0.f;
        sPartnerHopped = 0;
        return 0;
    }
    if (strcmp(cmd, "state") == 0) {
        char line[384];
        stateLine(line, sizeof(line));
        printf("[flows] state%s%s: %s\n", a1[0] ? " " : "", a1, line);
        fflush(stdout);
        return 0;
    }
    if (strcmp(cmd, "expect") == 0) {
        const char* cond = strstr(args, "expect") + 6;
        int r;
        while (*cond == ' ') {
            cond++;
        }
        if (strcmp(cond, "settled") == 0 && !sWatch) {
            watchFrame();
            watchFrame();
        }
        r = holds(cond);
        if (r == 1) {
            say("[flows] ok: %s%s\n", cond, "");
        } else {
            char line[384];
            stateLine(line, sizeof(line));
            printf("[flows] FAIL: %s (%s%s)\n", cond, r < 0 ? "no such check; " : "now ", line);
            fflush(stdout);
        }
        return 0;
    }
    if (strcmp(cmd, "until") == 0) {
        return startUntil(strstr(args, "until") + 5);
    }
    if (strcmp(cmd, "mash") == 0) {
        /* mash pad N KEY WHAT [MAX] | mash key KEY WHAT [MAX] | mash touch CONTROL WHAT [MAX] */
        const int kind = strcmp(a1, "pad") == 0 ? 1 : strcmp(a1, "key") == 0 ? 2 : strcmp(a1, "touch") == 0 ? 3 : 0;
        const char* p = strstr(args, a1) + strlen(a1);
        int skip = kind == 1 ? 2 : 1, frames;
        memset(&sMash, 0, sizeof(sMash));
        if (kind == 1) {
            sMash.pad = atoi(a2);
        }
        snprintf(sMash.what, sizeof(sMash.what), "%s", kind == 1 ? a3 : a2);
        for (; skip > 0 && *p != '\0'; skip--) { /* past its device's words to WHAT */
            while (*p == ' ') {
                p++;
            }
            while (*p != ' ' && *p != '\0') {
                p++;
            }
        }
        if (kind == 0) {
            say("[flows] FAIL: mash %s: no such device%s\n", a1, "");
            return 0;
        }
        frames = startUntil(p);
        if (frames > 0) {
            sMash.kind = kind;
        }
        return frames;
    }
#ifdef TTYD_BACKEND_AURORA
    if (strcmp(cmd, "pad") == 0) {
        n = atoi(a1);
        if (strcmp(a2, "attach") == 0) {
            if (!portFlowsPadAttach(n, a3[0] ? a3 : NULL)) {
                say("[flows] FAIL: pad %s attach%s\n", a1, "");
            }
            return 0;
        }
        if (strcmp(a2, "detach") == 0) {
            portFlowsPadDetach(n);
            return 0;
        }
        if (!portFlowsPadSet(n, a2, 1)) {
            say("[flows] FAIL: pad %s: can't press %s\n", a1, a2);
            return 0;
        }
        sHeld.kind = 1;
        sHeld.pad = n;
        snprintf(sHeld.what, sizeof(sHeld.what), "%s", a2);
        sHeld.left = atoi(a3) > 0 ? atoi(a3) : 4;
        return sHeld.left + RELEASE_GAP;
    }
    if (strcmp(cmd, "key") == 0) {
        if (!portFlowsKey(a1, 1)) {
            say("[flows] FAIL: key %s: no such key bound%s\n", a1, "");
            return 0;
        }
        sHeld.kind = 2;
        snprintf(sHeld.what, sizeof(sHeld.what), "%s", a1);
        sHeld.left = atoi(a2) > 0 ? atoi(a2) : 4;
        return sHeld.left + RELEASE_GAP;
    }
    if (strcmp(cmd, "touch") == 0) {
        if (!portFlowsTouch(a1, 0.f, 0.f, 1)) {
            say("[flows] FAIL: touch %s: no such control (a build without the touch controls?)%s\n", a1, "");
            return 0;
        }
        sHeld.kind = 3;
        snprintf(sHeld.what, sizeof(sHeld.what), "%s", a1);
        sHeld.left = atoi(a2) > 0 ? atoi(a2) : 4;
        return sHeld.left + RELEASE_GAP;
    }
    if (strcmp(cmd, "finger") == 0) {
        sHeld.x = (float)atof(a1);
        sHeld.y = (float)atof(a2);
        if (!portFlowsTouch(NULL, sHeld.x, sHeld.y, 1)) {
            say("[flows] FAIL: finger: no touch controls in this build%s%s\n", "", "");
            return 0;
        }
        sHeld.kind = 4;
        sHeld.left = atoi(a3) > 0 ? atoi(a3) : 4;
        return sHeld.left + RELEASE_GAP;
    }
#else
    if (strcmp(cmd, "pad") == 0 || strcmp(cmd, "key") == 0 || strcmp(cmd, "touch") == 0 || strcmp(cmd, "finger") == 0) {
        say("[flows] FAIL: %s: the devices need the Aurora build%s\n", cmd, "");
        return 0;
    }
#endif
    (void)n;
    say("[flows] FAIL: unknown step '%s'%s\n", args, "");
    return 0;
}
