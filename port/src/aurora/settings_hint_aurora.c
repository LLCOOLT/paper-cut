/* The first start's notice: once the player has the controls in the first
 * room, a line at the bottom of the screen in the game's font (as the co-op
 * notices, port/src/common/second_player_pc.c) says where Paper Cut's
 * settings are: the Start menu's last tab, or F1 (Back/Select on a
 * controller; the touch controls' three-bar button on a phone or tablet).
 * Shown once per user folder: settings_hint_shown in settings.txt.
 *
 * Not in tests that warp to a map ($TTYD_MAP), whose captures it would cover;
 * $TTYD_SETTINGS_HINT=1 shows it there too. */
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/tr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define HINT_FRAMES 360 /* six seconds, the last half second fading */

#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
#define HINT_TEXT "Paper Cut's settings: the Start menu's last tab, or the three-bar button."
#else
#define HINT_TEXT "Paper Cut's settings: the Start menu's last tab, or F1 (Back on a controller)."
#endif

static int sAge = -1; /* frames shown, -1 not yet */

static void hintDisp(CameraId camId, void* param) {
    const char* text = portTrGame(HINT_TEXT);
    const int left = HINT_FRAMES - sAge;
    const u8 alpha = left < 30 ? (u8)(0xFF * left / 30) : 0xFF;
    const float maxW = 560.0f; /* inside the 4:3 picture, a little in from its edges */
    char lines[2][200];
    int count = 1, i;
    (void)camId;
    (void)param;
    snprintf(lines[0], sizeof(lines[0]), "%s", text);
    lines[1][0] = '\0';
    if (FontGetMessageWidth(text) * 0.75f > maxW) {
        /* two lines, broken after the comma, else at the space nearest the middle */
        const int len = (int)strlen(lines[0]);
        int at = -1, k;
        if (strcmp(PORT_GAME_ID, "G8MJ01") != 0) {
            for (k = 0; k < len; k++) {
                if (lines[0][k] == ' ' && (at < 0 || abs(k - len / 2) < abs(at - len / 2))) {
                    at = k;
                }
            }
            for (k = 1; k < len; k++) {
                if (lines[0][k] == ' ' && lines[0][k - 1] == ',') {
                    at = k;
                    break;
                }
            }
            if (at > 0) {
                snprintf(lines[1], sizeof(lines[1]), "%s", lines[0] + at + 1);
                lines[0][at] = '\0';
                count = 2;
            }
        } else {
            /* the JP release's text (Shift-JIS): at the full-width space (0x8140) nearest the middle */
            for (k = 0; k + 1 < len; k++) {
                const unsigned char b = (unsigned char)lines[0][k];
                if (b == 0x81 && (unsigned char)lines[0][k + 1] == 0x40 &&
                    (at < 0 || abs(k - len / 2) < abs(at - len / 2))) {
                    at = k;
                }
                if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
                    k++; /* a letter two bytes long */
                }
            }
            if (at > 0) {
                snprintf(lines[1], sizeof(lines[1]), "%s", lines[0] + at + 2);
                lines[0][at] = '\0';
                count = 2;
            }
        }
    }
    FontDrawStart_alpha(alpha);
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, alpha});
    for (i = 0; i < count; i++) {
        float scale = 0.75f, w = FontGetMessageWidth(lines[i]) * scale;
        if (w > maxW) {
            scale *= maxW / w; /* still too wide (no space to break at): smaller */
            w = maxW;
        }
        FontDrawScale(scale);
        /* low on the screen, below the co-op notices' place */
        FontDrawString(-w * 0.5f, (count == 2 ? -184.0f : -196.0f) - i * 22.0f, lines[i]);
    }
}

/* each game frame (makeKey, src/system.c) */
void portSettingsHintFrame(void) {
    static int* sShown;
    static int sSkip = -1;
    if (sShown == NULL) {
        sShown = portSettingsDeclare("settings_hint_shown", 0, 0, 1);
    }
    if (sSkip < 0) {
        const char* force = getenv("TTYD_SETTINGS_HINT");
        sSkip = force != NULL ? atoi(force) == 0 : getenv("TTYD_MAP") != NULL;
    }
    if (sAge < 0) {
        /* the first room: the player has the controls in the field */
        if (*sShown || sSkip || gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle ||
            marioStGetSystemLevel() != 0 || !marioChkKey() || !marioChkCtrl()) {
            return;
        }
        sAge = 0;
        *sShown = 1;
        portSettingsSave(); /* once: a crash or a closed window doesn't bring it back */
        printf("[hint] %s\n", HINT_TEXT);
        fflush(stdout);
    }
    if (sAge >= HINT_FRAMES) {
        return;
    }
    sAge++;
    if (gp != NULL && !gp->inBattle && seqGetSeq() == SEQ_GAME) {
        dispEntry(CAMERA_2D, 903.0f, 1, hintDisp, NULL);
    }
}
