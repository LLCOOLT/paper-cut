/* First-time tips (port/include/port/tips.h): one at a time, low on the picture in the game's font with
 * its edge, as the first start's settings notice (settings_hint_aurora.c), each once per install. */
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/text_fit.h"
#include "port/tips.h"
#include "port/tr.h"
#include "port/whats_new.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int portSettingsHintShowing(void); /* settings_hint_aurora.c */

#define TIP_FRAMES 390 /* six and a half seconds, the last half second fading */
#define TIP_WAIT_FRAMES 300 /* a tip asked for and not shown in this long is let go (its moment passed) */

static const struct {
    const char* key;
    const char* text; /* English (portTrGame as it is drawn); NULL: drawn by its feature */
    int where;        /* 0 the field, 1 a battle, 2 the file menu */
} kTips[PORT_TIP_COUNT] = {
    {"tip_lent", "Borrowed partner: just for fun: no field ability, no battle turns.", 0},
    {"tip_audience", "In the audience: {A} cheer, {B} boo, {X} clap, {Y} jump. Hold {L} to throw a gift, {R} a rock.",
     1},
    /* (the file menu's New Game window says what each way in gives, each time: new_file_pick_pc.c; a bar of
       its own here ran over the menu's bottom buttons) */
    {"tip_rogueport", NULL, 2},
    {"tip_scene_skip", NULL, 0},
    {"tip_notes", "Notes from other players: {NOTE} reads one. Turn them off in Settings: Multiplayer.", 0},
};

static int* sSeen[PORT_TIP_COUNT];
static int sWaiting[PORT_TIP_COUNT]; /* frames since it was asked for (0: not asked) */
static int sShown = -1;              /* the tip up, -1 none */
static int sAge;

static int* seen(int tip) {
    if (sSeen[tip] == NULL) {
        sSeen[tip] = portSettingsDeclare(kTips[tip].key, 0, 0, 1);
    }
    return sSeen[tip];
}

int portTipsAllowed(void) {
    static int sRule = -1; /* 1 tests that warp to a map: none, unless $TTYD_TIPS=1 */
    if (sRule < 0) {
        const char* force = getenv("TTYD_TIPS");
        sRule = force != NULL ? atoi(force) == 0 : getenv("TTYD_MAP") != NULL || portCleanFrame();
    }
    return !sRule && !portStateSyncOn() && !portReplayActive();
}

int portTipSeen(int tip) {
    return tip >= 0 && tip < PORT_TIP_COUNT && *seen(tip) != 0;
}

int portTipOffer(int tip) {
    if (tip < 0 || tip >= PORT_TIP_COUNT || *seen(tip) || !portTipsAllowed()) {
        return 0;
    }
    if (kTips[tip].text == NULL) {
        *seen(tip) = 1; /* (its feature draws it now) */
        portSettingsSave();
        printf("[tips] %s\n", kTips[tip].key);
        fflush(stdout);
        return 1;
    }
    if (sWaiting[tip] == 0 && sShown != tip) {
        sWaiting[tip] = 1;
    }
    return 1;
}

void portTipsReset(void) {
    int i;
    for (i = 0; i < PORT_TIP_COUNT; i++) {
        *seen(i) = 0;
        sWaiting[i] = 0;
    }
    {
        int* hint = portSettingsFind("settings_hint_shown");
        if (hint != NULL) {
            *hint = 0;
        }
    }
    portSettingsSave();
    printf("[tips] shown again from now\n");
    fflush(stdout);
}

int portTipShowing(void) {
    return sShown >= 0;
}

/* the tip's place is up now: the field with Mario's controls, a battle, the file menu */
static int placeUp(int where) {
    const SequenceType seq = seqGetSeq();
    if (where == 2) {
        return seq == SEQ_LOAD;
    }
    if (gp == NULL || seq != SEQ_GAME) {
        return 0;
    }
    return where == 1 ? gp->inBattle : !gp->inBattle && marioStGetSystemLevel() == 0;
}

/* the text as up to three lines no wider than maxW at scale (between words; the JP release's at its
 * full-width spaces); how many */
static int breakLines(const char* text, float scale, float maxW, char lines[3][200]) {
    const int jp = strcmp(PORT_GAME_ID, "G8MJ01") == 0;
    const char* p = text;
    int n = 0;
    while (*p != '\0' && n < 3) {
        char buf[200];
        size_t len = strlen(p), fit = len, k;
        snprintf(buf, sizeof(buf), "%s", p);
        if (n < 2 && FontGetMessageWidth(buf) * scale > maxW) {
            fit = 0;
            for (k = 1; k < len && k < sizeof(buf) - 1; k++) {
                const unsigned char b = (unsigned char)p[k];
                const int space = jp ? (b == 0x81 && (unsigned char)p[k + 1] == 0x40) : p[k] == ' ';
                if (space) {
                    memcpy(buf, p, k);
                    buf[k] = '\0';
                    if (FontGetMessageWidth(buf) * scale <= maxW) {
                        fit = k;
                    }
                }
                if (jp && ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC))) {
                    k++; /* a letter two bytes long */
                }
            }
            if (fit == 0) {
                fit = len; /* (no space to break at: scaled down as drawn) */
            }
        }
        if (fit >= sizeof(lines[n])) {
            fit = sizeof(lines[n]) - 1;
        }
        memcpy(lines[n], p, fit);
        lines[n][fit] = '\0';
        n++;
        p += fit;
        while (*p == ' ') {
            p++;
        }
        if (jp && (unsigned char)p[0] == 0x81 && (unsigned char)p[1] == 0x40) {
            p += 2;
        }
    }
    return n;
}

static void tipDraw(void) {
    const char* text = portTrGame(kTips[sShown].text);
    const int left = TIP_FRAMES - sAge;
    const u8 alpha = left < 30 ? (u8)(0xFF * left / 30) : sAge < 10 ? (u8)(0xFF * sAge / 10) : 0xFF;
    const float maxW = 560.0f; /* inside the 4:3 picture, a little in from its edges */
    char lines[3][200];
    const int count = breakLines(text, 0.7f, maxW, lines);
    int i;
    FontDrawStart_alpha(alpha);
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, alpha});
    for (i = 0; i < count; i++) {
        float scale = 0.7f, w = FontGetMessageWidth(lines[i]) * scale;
        portTextFit("tip-line", lines[i], w, maxW, 1);
        if (w > maxW) {
            scale *= maxW / w;
            w = maxW;
        }
        FontDrawScale(scale);
        /* low on the screen, where the settings notice goes (below the co-op notices' place) */
        FontDrawString(-w * 0.5f, -206.0f + (float)(count - 1) * 21.0f - (float)i * 21.0f, lines[i]);
    }
}

/* (the game's font takes its random numbers: put back, as the settings notice does) */
static void tipDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    (void)camId;
    (void)param;
    if (sShown >= 0) {
        tipDraw();
    }
    portRandSetState(seed);
}

void portTipsFrame(void) {
    int i;
    if (sShown >= 0) {
        if (++sAge >= TIP_FRAMES || !placeUp(kTips[sShown].where) || !portTipsAllowed()) {
            sShown = -1;
        } else {
            dispEntry(CAMERA_2D, 903.0f, 1, tipDisp, NULL);
        }
        return;
    }
    for (i = 0; i < PORT_TIP_COUNT; i++) {
        if (sWaiting[i] == 0) {
            continue;
        }
        if (++sWaiting[i] > TIP_WAIT_FRAMES || *seen(i) || !portTipsAllowed()) {
            sWaiting[i] = 0;
            continue;
        }
        if (placeUp(kTips[i].where) && !portSettingsHintShowing() && !portWhatsNewShowing()) {
            sWaiting[i] = 0;
            sShown = i;
            sAge = 0;
            *seen(i) = 1;
            portSettingsSave(); /* once: a crash or a closed window doesn't bring it back */
            printf("[tips] %s: %s\n", kTips[i].key, kTips[i].text);
            fflush(stdout);
            return;
        }
    }
}
