/* Back to the launcher (port/include/port/launcher_back.h): the steps, between
 * two of the game's frames, and the progress since the last save.
 *
 * The way out is the game's own: online play is left first (a co-op guest's
 * "where now" window dropped, as the player chose where to go), then the game
 * goes back to its title as the console's reset does (soft_reset_pc.c), which
 * ends whatever runs: a battle (its exit puts the field back), a mode (each
 * ends at the title as at a Game Over: the lent pouch, the difficulty and the
 * story's words put back), a map change. Only at the title, with the memory
 * card idle and nothing lent, does the launcher come: a new program on a
 * desktop (restart.cpp, quick resume's save written first for Continue), the
 * launcher in this process on a phone or tablet.
 *
 * "Progress since your last save": frames played with a file (the field, a
 * battle, a map change) since the memory card last finished an operation (a
 * Save Block's save, autosave, the file select's load). Not the title menu's
 * file for the modes (never saved: title_menu.h). */
#include "port/launcher_back.h"
#include "port/aurora_pc.h"
#include "port/companion_menu.h"
#include "port/mode_lending.h"
#include "port/online_end.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/restart.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/soft_reset.h"
#include "port/statesync.h"
#include "port/title_menu.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif
#include "driver/seqdrv.h"
#include "manager/cardmgr.h"
#include "mario/mariost.h"
#include "port/offscreen.h"
#include <aurora/dvd.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_stdinc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { B_IDLE, B_LEAVE, B_TITLE, B_GO };

/* a second of play: a save just made isn't "progress" yet */
#define UNSAVED_FRAMES 60
/* the title waited for this long at most (a reset never seen to hang; then on all the same) */
#define TITLE_WAIT_FRAMES (60 * 30)
/* at the title, a mode's own end given this long to put back what it lent */
#define LENT_WAIT_FRAMES (60 * 5)

static int sStep = B_IDLE;
static int sWaited, sAtTitle;
static unsigned sPlayed; /* frames played with a file since the card's last operation */
static int sCardWas;
static int sHere; /* the launcher running in this process (a phone's) */

static void say(const char* what) {
    printf("[launcher-back] %s\n", what);
    fflush(stdout);
}

/* a desktop restarts into the launcher; a phone or tablet (or a test's $TTYD_LAUNCHER_BACK_HERE) shows it here */
static int here(void) {
    const char* v = getenv("TTYD_LAUNCHER_BACK_HERE");
    return !portRestartSupported() || (v != NULL && v[0] != '\0' && v[0] != '0');
}

static int atTitle(void) {
    SequenceType seq;
    if (gp == NULL) {
        return 1;
    }
    seq = seqGetSeq();
    return (seq == SEQ_UNDEFINED || seq == SEQ_LOGO || seq == SEQ_TITLE || seq == SEQ_E3) && !seqCheckSeq();
}

static void trackUnsaved(void) {
    const int card = gp != NULL && cardIsExec();
    SequenceType seq;
    if (sCardWas && !card) {
        sPlayed = 0; /* a save or a load just finished: the card is the game */
    }
    sCardWas = card;
    if (gp == NULL) {
        sPlayed = 0;
        return;
    }
    seq = seqGetSeq();
    if (seq == SEQ_UNDEFINED || seq == SEQ_LOGO || seq == SEQ_TITLE || seq == SEQ_E3 || seq == SEQ_LOAD ||
        portTitleTempFile()) {
        sPlayed = 0; /* (no file in play, or the title menu's, which is never saved) */
    } else if (!card && (seq == SEQ_GAME || seq == SEQ_BATTLE || seq == SEQ_MAP_CHANGE || seq == SEQ_GAME_OVER) &&
               sPlayed < 0x7FFFFFFFu) {
        sPlayed++;
    }
}

int portLauncherBackUnsaved(void) {
    return sPlayed > UNSAVED_FRAMES;
}

int portLauncherBackBusy(void) {
    return sStep != B_IDLE || sHere;
}

static int sRunning; /* the game's frames have begun (its retrace calls portLauncherBackFrame) */

int portLauncherBackAvailable(void) {
    return sRunning && !sHere && !portReplayPlaying();
}

int portLauncherBackRequest(const char** why) {
    if (gPortCompanionMenu) {
        if (why != NULL) {
            *why = "Not on the second screen: use the game's Start menu.";
        }
        return 0;
    }
    if (portReplayPlaying()) {
        /* (the input is the replay's: on a phone, the launcher here would hand the title back still playing it) */
        if (why != NULL) {
            *why = "Not during a replay.";
        }
        return 0;
    }
    if (sStep != B_IDLE || sHere) {
        return 1; /* (on its way already) */
    }
    printf("[launcher-back] asked (%s%s%s, %s)\n", portLauncherBackUnsaved() ? "progress since the last save" : "saved",
           portStateSyncOn() ? ", online" : "", portModeLending() ? ", a mode lends" : "",
           here() ? "the launcher here" : "this program started again");
    fflush(stdout);
    sStep = B_LEAVE;
    sWaited = 0;
    return 1;
}

static int sForcedRead, sForced;

int portLauncherBackForced(void) {
    if (!sForcedRead) {
        const char* v = SDL_getenv("TTYD_LAUNCHER_BACK");
        sForcedRead = 1;
        sForced = v != NULL && v[0] != '\0' && v[0] != '0';
        if (v != NULL) {
            /* (a version's program started from the launcher takes this one's environment) */
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "TTYD_LAUNCHER_BACK");
        }
        if (sForced) {
            say("this start is Back to the launcher's: the launcher shows");
        }
    }
    return sForced || sHere;
}

/* the launcher in this process, over the title: its Play or Continue of this version comes back here */
static void launcherHere(void) {
    static char path[4096], was[4096], why[4600];
    snprintf(was, sizeof(was), "%s", portAuroraDiscPath());
    path[0] = '\0';
    portPause(); /* the game clock and its sound stopped meanwhile */
    aurora_dvd_wait_idle(); /* (the title's reads done: the launcher reads its discs through the same drive) */
    sHere = 1;
    say("the launcher, in this process");
    portLauncherRun(portSettingsUserPath(), "", path, sizeof(path));
    sHere = 0;
#ifdef PORT_TOUCH_OVERLAY
    portTouchReleaseAll(); /* (the launcher's touches: no on-screen button of the game's left held) */
#endif
    /* The launcher looked into its discs (their versions and banners) through Aurora's one drive, which
     * it closed after: the disc chosen (Play's: an image of this version), else the one before, opened
     * again before the game reads anything */
    if (path[0] == '\0' || !portAuroraOpenDiscPath(path, why, sizeof(why))) {
        if (path[0] != '\0') {
            printf("[launcher-back] %s: the disc before instead\n", why);
        }
        if (!portAuroraOpenDiscPath(was, why, sizeof(why))) {
            printf("[launcher-back] the disc can't be opened again: %s\n", why);
            fflush(stdout);
            portShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Paper Cut", why, NULL);
            exit(1);
        }
    }
    portResume();
    if (strcmp(was, portAuroraDiscPath()) != 0) {
        printf("[launcher-back] back from the launcher: the game at its title, from another image of this version\n");
    } else {
        printf("[launcher-back] back from the launcher: the game at its title\n");
    }
    fflush(stdout);
}

static void testFrame(void) {
    /* $TTYD_LAUNCHER_BACK_AT=<frame> (tests): asked at that retrace, as the menus' confirmed row asks */
    static int sRead;
    static long sAt = -1;
    static unsigned sFrame;
    if (!sRead) {
        const char* v = getenv("TTYD_LAUNCHER_BACK_AT");
        sRead = 1;
        sAt = v != NULL ? atol(v) : -1;
    }
    if (sAt >= 0 && (long)++sFrame == sAt) {
        const char* why = NULL;
        say("TTYD_LAUNCHER_BACK_AT: asked");
        if (!portLauncherBackRequest(&why)) {
            printf("[launcher-back] refused: %s\n", why != NULL ? why : "");
            fflush(stdout);
        }
    }
}

void portLauncherBackFrame(void) {
    sRunning = 1;
    trackUnsaved();
    testFrame();
    switch (sStep) {
        case B_LEAVE:
            if (portStateSyncOn()) {
                portStateSyncLeave();
                say("online: left the room");
            }
            portOnlineEndDrop(); /* (this player chose where to go: no "where now") */
            portReplaySaveIfRecording();
            if (gp != NULL && !atTitle()) {
                gp->resetType = 1; /* soft_reset_pc.c: the title, the game's own way */
                say("back to the title first");
            }
            sStep = B_TITLE;
            sWaited = 0;
            sAtTitle = 0;
            return;
        case B_TITLE:
            portOnlineEndDrop();
            sWaited++;
            if (!atTitle()) {
                if (gp != NULL && !portSoftResetBusy()) {
                    gp->resetType = 1; /* (asked again: one dropped, a sequence that wasn't settled) */
                }
                if (sWaited < TITLE_WAIT_FRAMES) {
                    return;
                }
                say("the title never came: on all the same");
            } else {
                const int lent = portModeLending();
                if (sAtTitle++ == 0) {
                    printf("[launcher-back] at the title (%d frames)%s\n", sWaited,
                           lent ? ": a mode still lends, its end awaited" : "");
                    fflush(stdout);
                }
                if ((gp != NULL && cardIsExec()) || portSoftResetBusy()) {
                    return;
                }
                if (lent != 0 && sAtTitle < LENT_WAIT_FRAMES) {
                    return;
                }
                if (lent != 0) {
                    printf("[launcher-back] a mode still lends (%d) at the title: nothing of it is saved\n", lent);
                } else {
                    printf("[launcher-back] nothing lent: the file's own pouch\n");
                }
                fflush(stdout);
            }
            sStep = B_GO;
            return;
        case B_GO:
            sStep = B_IDLE;
            if (here()) {
                /* quick resume's save written for the launcher's Continue, as the desktop's restart writes it
                   (restart.cpp): handhelds write it only every so often */
                portResumeFlush();
                launcherHere();
            } else if (!portRestartToLauncher()) {
                say("could not start the launcher: the game plays on at its title");
            }
            return;
        default:
            return;
    }
}
