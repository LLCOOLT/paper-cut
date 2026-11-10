/* Leaving a mode, one way for all of them: port/include/port/mode_leave.h */
#include "port/mode_leave.h"
#include "port/battlex.h"
#include "port/dojo.h"
#include "port/modes.h"
#include "port/playmodes.h"
#include "port/postgame.h"
#include "port/statesync.h"
#include "port/versus.h"
#include "port/mod_host.h"
#include "port/tr.h"
#include "battle/battle.h"
#include "driver/seqdrv.h"
#include "mario/mariost.h"
#include "system.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

static int running(int mode) {
    const PortModeStatus* s = portModeStatus();
    return s->mode == mode && !s->finished;
}

static int rushOn(void) {
    return running(PORT_MODE_BOSS_RUSH);
}
static int pitOn(void) {
    return running(PORT_MODE_ENDLESS_PIT);
}
static int refightOn(void) {
    return running(PORT_MODE_REFIGHT);
}
static int puzzleOn(void) {
    return portPuzzleActive(NULL, NULL, NULL) >= 0;
}
static int chapterOn(void) {
    return portChapterActive() > 0;
}
static void versusLeave(void) {
    (void)portVersusAbort(); /* (however far it got: the file's words and pouch back, out of the ring) */
}

/* name: the Modes section's (translated as it is); label: the Start menu's row */
static const struct {
    const char* name;
    const char* label;
    int (*on)(void);
    void (*leave)(void);
} kModes[] = {
    {"Boss rush", "Leave the boss rush", rushOn, portModeStop},
    {"Endless Glitz Pit", "Leave the endless Glitz Pit", pitOn, portModeStop},
    {"Boss refights", "Leave the refight", refightOn, portModeStop},
    {"Action-command dojo", "Leave the dojo", portDojoActive, portDojoStop},
    {"Battle puzzles", "Leave the puzzle", puzzleOn, portPuzzleGiveUp},
    {"The Paper Tower", "Leave the Paper Tower", portTowerActive, portTowerStop},
    {"Chapter select", "Leave the chapter", chapterOn, portChapterAbandon},
    {"Pit of 100 Trials in co-op", "Leave the Pit in co-op", portPitCoopActive, portPitCoopStop},
    {"Versus", "Leave the versus", portVersusBusy, versusLeave},
};
#define MODES ((int)(sizeof(kModes) / sizeof(kModes[0])))

static int sEscapeIn; /* frames to the battle's end as run away (once the mode's stop is in: a leave in its battle) */

static int now(void) {
    int i;
    for (i = 0; i < MODES; i++) {
        if (kModes[i].on()) {
            return i;
        }
    }
    return -1;
}

const char* portModeLeaveName(void) {
    const int i = now();
    return i >= 0 ? kModes[i].name : NULL;
}

const char* portModeLeaveLabel(void) {
    const int i = now();
    return i >= 0 ? kModes[i].label : NULL;
}

const char* portModeLeaveRefusal(void) {
    if (now() < 0) {
        return "No mode is running.";
    }
    if (portStateSyncCoopGuest() || portStateSyncPartyRole() == 2) {
        return "The host's mode: only the host can leave it.";
    }
    return NULL;
}

int portModeLeave(const char** why) {
    const int i = now();
    if ((*why = portModeLeaveRefusal()) != NULL) {
        return 0;
    }
    printf("[modes] leave: %s%s\n", kModes[i].name, gp != NULL && gp->inBattle ? " (in its battle)" : "");
    fflush(stdout);
    kModes[i].leave();
    if (gp != NULL && gp->inBattle && kModes[i].leave != versusLeave && kModes[i].leave != portChapterAbandon) {
        sEscapeIn = 2; /* (the stop alone waits for the battle's end: the battle ends now, as run away) */
    } else if (gp != NULL && gp->inBattle && kModes[i].leave == portChapterAbandon) {
        /* (a chapter replay's battles are the story's own, with its own scripts after them: a forced run away
           from a boss that can't be fled played on into its defeat scene, the chapter's end reached and the
           replay recorded as cleared. The battle plays out; the replay ends once it is over and the field calm,
           never as cleared: port/src/aurora/chapters_aurora.c) */
        printf("[modes] leave: the chapter's battle plays out first\n");
        fflush(stdout);
    }
    return 1;
}

int portModeAnotherRuns(const char* why) {
    static const char* const kTexts[] = {
        "Another mode is running: finish it first.",
        "Not while another mode runs.",
        "Not while a mode runs.",
    };
    size_t i;
    for (i = 0; why != NULL && i < sizeof(kTexts) / sizeof(kTexts[0]); i++) {
        if (strcmp(why, kTexts[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* --- the start asked for after a leave --- */

static const char* (*sAct)(int id, int dir);
static int sActId, sWait;
static const char* sActName;

void portModeSwitchQueue(const char* (*act)(int id, int dir), int id, const char* name) {
    sAct = act;
    sActId = id;
    sActName = name;
    sWait = 1200; /* (game frames: the stop's battle end, the map change back, Mario in his spot) */
}

/* the settings page's ask (its thread): taken by the game's frame */
static const char* (*sAskAct)(int id, int dir); /* __atomic, with the rest once sAsked is set */
static int sAskId, sAsked;
static const char* sAskName;
static const char* sAskRefusal; /* __atomic */

void portModeLeaveAsk(const char* (*act)(int id, int dir), int id, const char* name) {
    __atomic_store_n(&sAskRefusal, (const char*)NULL, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sAskAct, act, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sAskId, id, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sAskName, name, __ATOMIC_SEQ_CST);
    __atomic_store_n(&sAsked, 1, __ATOMIC_SEQ_CST);
}

const char* portModeLeaveAskRefusal(void) {
    return __atomic_load_n(&sAskRefusal, __ATOMIC_SEQ_CST);
}

/* --- a mode's battle left: Start, then Start again (the Start menu doesn't open in a battle) --- */

#define BATTLE_ASK_FRAMES 240 /* (4 s to press it again) */
#define SAID_FRAMES 180
static int sBattleAsk;      /* frames left of the ask shown */
static const char* sSaid;   /* a leave refused in a battle (English), shown SAID_FRAMES */
static int sSaidFrames;

static void battleFrame(void) {
    u32 trg;
    static int sEscapeTries; /* frames the run away has waited for the battle's main phase */
    if (sEscapeIn > 0 && --sEscapeIn == 0) {
        /* (a frame after the stop was asked: the mode's battle end then sees it pending and goes back, where
           a plain run away would start the fight again) */
        BattleWork* wp = _battleWorkPointer;
        if (gp != NULL && gp->inBattle && wp != NULL && !(wp->flags & BATTLE_FLAG_ESCAPED) &&
            BattleGetSeq(wp, SEQ_UNKNOWN) != 2 && ++sEscapeTries < 3600) {
            /* (the battle's entry still playing: the run away waits for its main phase, frame by frame. It once
               tried this one frame only, and a leave asked during the entry left the mode running until the
               whole fight was over; in the dojo for good, its dummy never falling) */
            sEscapeIn = 1;
        } else if (gp != NULL && gp->inBattle && wp != NULL && !(wp->flags & BATTLE_FLAG_ESCAPED) &&
                   BattleGetSeq(wp, SEQ_UNKNOWN) == 2) {
            sEscapeTries = 0;
            /* run away, ended at once from wherever it is (the command menu waits for a choice, and the flag
               alone is read only after an action): the game's own abort, as battle.c's kiosk timeout does it */
            wp->flags |= BATTLE_FLAG_ESCAPED;
            BattleSetSeq(wp, SEQ_UNKNOWN, 3);
            BattleSetSeq(wp, SEQ_END, 0x7000000);
            printf("[modes] leave: the battle ends as run away\n");
            fflush(stdout);
        }
        if (sEscapeIn == 0) {
            sEscapeTries = 0;
        }
    }
    if (sSaidFrames > 0) {
        sSaidFrames--;
    }
    if (gp == NULL || !gp->inBattle || now() < 0 || sEscapeIn > 0 || portVersusOn()) {
        sBattleAsk = 0; /* (versus: Start is its own there, as its fight's pause) */
        return;
    }
    trg = keyGetButtonTrg(0);
    if (sBattleAsk > 0) {
        sBattleAsk--;
        if (trg & PAD_BUTTON_START) {
            const char* why = NULL;
            sBattleAsk = 0;
            if (!portModeLeave(&why)) { /* (left: portModeLeave ends the battle) */
                printf("[modes] leave refused: %s\n", why);
                fflush(stdout);
                sSaid = why;
                sSaidFrames = SAID_FRAMES;
            }
        }
    } else if (trg & PAD_BUTTON_START) {
        sBattleAsk = BATTLE_ASK_FRAMES;
        printf("[modes] leave asked in a battle: %s\n", portModeLeaveLabel());
        fflush(stdout);
    }
}

/* the ask (or a refusal) over the picture, in the modes' own box (port/settings/features/modes.cpp) */
static void battleDraw(void* user) {
    const char* line1;
    const char* line2;
    (void)user;
    if (sBattleAsk > 0 && portModeLeaveLabel() != NULL) {
        line1 = portTr(portModeLeaveLabel());
        line2 = portTr("Press Start again to leave, or wait to play on.");
    } else if (sSaidFrames > 0 && sSaid != NULL) {
        line1 = portTr(sSaid);
        line2 = NULL;
    } else {
        return;
    }
    portModHostDrawRect(140.0f, 200.0f, 360.0f, line2 != NULL ? 52.0f : 30.0f, 0x000000C0u);
    portModHostDrawText(150.0f, 206.0f, 15.0f, 0xFFFFFFFFu, line1);
    if (line2 != NULL) {
        portModHostDrawText(150.0f, 228.0f, 13.0f, 0xFFE080FFu, line2);
    }
}

void portModeLeaveInit(void) {
    portModHostAddDraw(battleDraw, NULL);
}

void portModeSwitchFrame(void) {
    const char* said;
    battleFrame();
    /* (taken once the Start menu (or a map change) is over: in its battle, or Mario free in the field) */
    if (__atomic_load_n(&sAsked, __ATOMIC_SEQ_CST) && gp != NULL && !seqCheckSeq() &&
        ((seqGetSeq() == SEQ_BATTLE && gp->inBattle) || (seqGetSeq() == SEQ_GAME && marioStGetSystemLevel() == 0)) &&
        __atomic_exchange_n(&sAsked, 0, __ATOMIC_SEQ_CST)) {
        const char* (*act)(int, int) = __atomic_load_n(&sAskAct, __ATOMIC_SEQ_CST);
        const char* why = NULL;
        if (!portModeLeave(&why)) {
            printf("[modes] leave refused: %s\n", why);
            fflush(stdout);
            __atomic_store_n(&sAskRefusal, why, __ATOMIC_SEQ_CST);
        } else if (act != NULL) {
            portModeSwitchQueue(act, __atomic_load_n(&sAskId, __ATOMIC_SEQ_CST),
                                __atomic_load_n(&sAskName, __ATOMIC_SEQ_CST));
        }
    }
    if (sAct == NULL) {
        return;
    }
    if (--sWait <= 0) {
        printf("[modes] switch: %s not started (the mode left wasn't over in time: running %d, lending %d, seq %d, "
               "map %s, system level %d)\n",
               sActName, now(), portModeLending(), seqGetSeq(), gp != NULL ? gp->currentMapName : "-",
               (int)marioStGetSystemLevel());
        fflush(stdout);
        sAct = NULL;
        return;
    }
    if (now() >= 0 || portModeLending() != 0 || seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle ||
        seqCheckSeq() || marioStGetSystemLevel() != 0) {
        return;
    }
    said = sAct(sActId, 0);
    printf("[modes] switch: %s: %s\n", sActName, said != NULL ? said : "(no answer)");
    fflush(stdout);
    sAct = NULL;
}
