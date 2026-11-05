/* The "what's new" page (port/include/port/whats_new.h), in the game's own
 * windows: the first start after an update's window (a system window over
 * the title screen, the file select or the field), and the Start menu's page over the
 * Settings tab (General > What's new). Both show the list the build took
 * from the release notes, scrolled with up and down. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/online.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/whats_new.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "driver/windowdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "statuswindow.h"
#include "win/win_main.h"
#include <SDL3/SDL_stdinc.h>
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_SCALE 0.62f
#define LINE_STEP 19.0f
#define MAX_LINES 400

typedef struct Shown {
    int kind; /* PORT_WHATS_NEW_*, or -1 a blank line */
    int indent;
    char text[160];
} Shown;

static Shown sLines[MAX_LINES];
static int sLineCount;
static float sLaidWidth = -1.0f;
static int sTop;

static const char* gameText(const PortWhatsNewLine* l) {
    return strcmp(PORT_GAME_ID, "G8MJ01") == 0 ? l->ascii : l->latin1;
}

static void add(int kind, int indent, const char* text, size_t len) {
    Shown* s;
    if (sLineCount >= MAX_LINES) {
        return;
    }
    s = &sLines[sLineCount++];
    s->kind = kind;
    s->indent = indent;
    if (len >= sizeof(s->text)) {
        len = sizeof(s->text) - 1;
    }
    memcpy(s->text, text, len);
    s->text[len] = '\0';
}

static float width(const char* text, size_t len) {
    char buf[200];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, text, len);
    buf[len] = '\0';
    return (float)FontGetMessageWidth(buf) * TEXT_SCALE;
}

/* the list broken into lines no wider than w (between words) */
static void layout(float w) {
    int i;
    if (sLaidWidth == w) {
        return;
    }
    sLaidWidth = w;
    sLineCount = 0;
    for (i = 0; i < portWhatsNewCount; i++) {
        const PortWhatsNewLine* l = &portWhatsNewLines[i];
        char item[200];
        const char* p;
        int indent = 0;
        if (i == 0 && l->kind == PORT_WHATS_NEW_SECTION) {
            continue; /* ("What's new", under a title that says it) */
        }
        if (l->kind != PORT_WHATS_NEW_ITEM && sLineCount > 0) {
            add(-1, 0, "", 0);
        }
        if (l->kind == PORT_WHATS_NEW_ITEM) {
            snprintf(item, sizeof(item), "- %s", gameText(l));
        } else {
            snprintf(item, sizeof(item), "%s", gameText(l));
        }
        p = item;
        while (*p != '\0') {
            const float room = w - (indent ? 14.0f : 0.0f);
            size_t len = strlen(p), fit = len, k;
            if (width(p, len) > room) {
                fit = 0;
                for (k = 1; k <= len; k++) {
                    if ((p[k] == ' ' || p[k] == '\0') && width(p, k) <= room) {
                        fit = k;
                    }
                }
                if (fit == 0) { /* one long word: as much as fits */
                    for (fit = 1; fit < len && width(p, fit + 1) <= room; fit++) {
                    }
                }
            }
            add(l->kind, indent, p, fit);
            p += fit;
            while (*p == ' ') {
                p++;
            }
            indent = 1;
        }
    }
}

static void title(char* out, size_t size) {
    snprintf(out, size, portTrGame("What's new in Paper Cut %s"), portWhatsNewVersion);
}

/* the list at (x, y) (its top left), w wide, rows lines of it from sTop;
 * dark: on the system window's dark blue (light text), else on paper */
static void listDisp(float x, float y, float w, int rows, int dark) {
    const GXColor black = dark ? (GXColor){0xFF, 0xFF, 0xFF, 0xFF} : (GXColor){0x00, 0x00, 0x00, 0xFF};
    const GXColor heading = dark ? (GXColor){0xFF, 0xE0, 0x50, 0xFF} : (GXColor){0x3A, 0x1C, 0x66, 0xFF};
    const GXColor sub = dark ? (GXColor){0xFF, 0xB0, 0x60, 0xFF} : (GXColor){0x6A, 0x3C, 0x14, 0xFF};
    const GXColor grey = dark ? (GXColor){0xB0, 0xB8, 0xD0, 0xFF} : (GXColor){0x60, 0x60, 0x60, 0xFF};
    const float top = y;
    char head[120];
    int i;
    layout(w);
    winFontInit();
    title(head, sizeof(head));
    {
        const float full = (float)FontGetMessageWidth(head);
        const float k = full * 0.85f > w ? w / full : 0.85f;
        winFontSet((Vec){x, y, 0.0f}, (Vec){k, 0.85f, 0.85f}, heading, "%s", head);
    }
    {
        /* installed by the update at this start (update_install.cpp): said under the title */
        const char* applied = SDL_getenv("TTYD_UPDATE_APPLIED");
        if (applied != NULL && applied[0] != '\0') {
            char note[120];
            snprintf(note, sizeof(note), portTrGame("Updated to Paper Cut %s."), applied);
            winFontSet((Vec){x, y - 24.0f, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, sub, "%s", note);
            y -= 16.0f;
            rows--;
        }
    }
    if (portWhatsNewCount == 0) {
        winFontSet((Vec){x, y - 34.0f, 0.0f}, (Vec){TEXT_SCALE, TEXT_SCALE, TEXT_SCALE}, grey, "%s",
                   portTrGame("No notes for this version."));
        return;
    }
    for (i = 0; i < rows && sTop + i < sLineCount; i++) {
        const Shown* s = &sLines[sTop + i];
        if (s->kind < 0) {
            continue;
        }
        winFontSet((Vec){x + (s->indent ? 14.0f : 0.0f), y - 34.0f - i * LINE_STEP, 0.0f},
                   (Vec){TEXT_SCALE, TEXT_SCALE, TEXT_SCALE},
                   s->kind == PORT_WHATS_NEW_SECTION ? heading : s->kind == PORT_WHATS_NEW_SUBHEAD ? sub : black, "%s",
                   s->text);
    }
    if (sLineCount > rows) { /* where the view is, as the Journal's logs count theirs */
        char pos[24];
        const int last = sTop + rows < sLineCount ? sTop + rows : sLineCount;
        snprintf(pos, sizeof(pos), "%d-%d/%d", sTop + 1, last, sLineCount);
        winFontSet((Vec){x + w - (float)FontGetMessageWidth(pos) * 0.6f, top, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, grey,
                   "%s", pos);
    }
}

static int scroll(int dir, int rows) {
    const int max = sLineCount > rows ? sLineCount - rows : 0;
    const int was = sTop;
    sTop += dir;
    sTop = sTop < 0 ? 0 : sTop > max ? max : sTop;
    return sTop != was;
}

/* --- the Start menu's page, over the Settings tab ------------------------ */

#define PAGE_X (-262.0f)
#define PAGE_TOP 132.0f
#define PAGE_W 524.0f
#define PAGE_ROWS 11

static char sHelp[256];

static void pageOpen(int arg) {
    (void)arg;
    sTop = 0;
}

static int pageMain(WindowWork* wp) {
    if (wp->buttonTrg & (PAD_BUTTON_B | PAD_BUTTON_A)) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        if (scroll((wp->dirRep & DEMO_STICK_UP) ? -1 : 1, PAGE_ROWS)) {
            psndSFXOn((const char*)0x20005);
        }
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        if (scroll((wp->dirRep & DEMO_STICK_LEFT) ? -PAGE_ROWS : PAGE_ROWS, PAGE_ROWS)) {
            psndSFXOn((const char*)0x20005);
        }
    }
    wp->helpTgtX = PAGE_X - 24.0f;
    wp->helpTgtY = PAGE_TOP - 8.0f;
    return 0;
}

static const char* pageHelp(void) {
    portPauseWrap(portTrGame("Up and down to read, left and right a page at a time; B goes back."), sHelp,
                  sizeof(sHelp));
    return sHelp;
}

static void pageDisp(WindowWork* wp, float x, float y) {
    const u32 seed = portRandState();
    winKirinukiGX(wp, 0, x + PAGE_X - 8.0f, y + PAGE_TOP + 14.0f, PAGE_W + 16.0f, 270.0f);
    listDisp(x + PAGE_X + 8.0f, y + PAGE_TOP - 4.0f, PAGE_W - 16.0f, PAGE_ROWS, 0);
    portRandSetState(seed);
}

const PortPausePage gPortWhatsNewPage = {pageOpen, pageMain, pageDisp, pageHelp};

/* --- the first start after an update ------------------------------------ */

#define WIN_X (-262.0f)
#define WIN_Y 205.0f
#define WIN_W 524.0f
#define WIN_H 410.0f
#define WIN_ROWS 15

static int sShowing, sDone;
static int sHudClosed; /* the status bar kept closed under the page (in the field) */
static u16 sHeld;
static int sRepeat;

int portWhatsNewShowing(void) {
    return sShowing;
}

/* "0.4.0" -> 400, "1.2.3" -> 10203; 0 for anything else ("dev") */
static int versionNumber(const char* v) {
    int n[3] = {0, 0, 0}, parts = 0;
    const char* p = v;
    while (parts < 3) {
        char* end;
        long x = strtol(p, &end, 10);
        if (end == p || x < 0 || x > 99) {
            return 0;
        }
        n[parts++] = (int)x;
        if (*end == '\0') {
            break;
        }
        if (*end != '.') {
            return 0;
        }
        p = end + 1;
    }
    return n[0] * 10000 + n[1] * 100 + n[2];
}

static void winDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const GXColor grey = {0xB0, 0xB8, 0xD0, 0xFF};
    char foot[200];
    (void)camId;
    (void)param;
    windowDispGX_System(0, 0xFF, WIN_X, WIN_Y, WIN_W, WIN_H);
    listDisp(WIN_X + 26.0f, WIN_Y - 30.0f, WIN_W - 52.0f, WIN_ROWS, 1);
    snprintf(foot, sizeof(foot), "%s", portTrGame("A or B closes this. It's in the Start menu's Settings, General, too."));
    {
        const float full = (float)FontGetMessageWidth(foot);
        const float room = WIN_W - 52.0f;
        const float k = full * 0.55f > room ? room / full : 0.55f;
        winFontInit();
        winFontSet((Vec){WIN_X + 26.0f, WIN_Y - WIN_H + 36.0f, 0.0f}, (Vec){k, 0.55f, 0.55f}, grey, "%s", foot);
    }
    portRandSetState(seed);
}

void portWhatsNewFrame(void) {
    static int* sSeen;
    static int sChecked, sWait;
    const int current = versionNumber(portWhatsNewBuild);
    if (sDone) {
        if (sHudClosed) {
            sHudClosed = 0;
            statusWinForceCloseClear();
        }
        return;
    }
    if (!sChecked) {
        const char* force = getenv("TTYD_WHATS_NEW");
        const int* played = portSettingsDeclare("settings_hint_shown", 0, 0, 1);
        sChecked = 1;
        sSeen = portSettingsDeclare("whats_new_seen", 0, 0, 999999);
        if (force != NULL) {
            if (atoi(force) == 0) { /* $TTYD_WHATS_NEW=0: never; =1: this start, whatever was seen */
                sDone = 1;
                return;
            }
        } else if (current == 0 || getenv("TTYD_FAST") != NULL || getenv("TTYD_MAP") != NULL) {
            sDone = 1; /* a dev build, or tests */
            return;
        } else if (*sSeen >= current) {
            sDone = 1;
            return;
        } else if (*sSeen == 0 && !*played) {
            /* the first start ever: nothing was updated */
            *sSeen = current;
            portSettingsSave();
            sDone = 1;
            return;
        }
    }
    if (!sShowing) {
        const SequenceType seq = seqGetSeq();
        const int ready = (seq == SEQ_TITLE || seq == SEQ_LOAD) ||
                          (seq == SEQ_GAME && gp != NULL && !gp->inBattle && marioStGetSystemLevel() == 0 &&
                           marioChkKey() && marioChkCtrl());
        if (portOnlineActive() || portReplayActive() || !ready) {
            sWait = 0;
            return;
        }
        if (++sWait < 30) { /* half a second in: the screen settled */
            return;
        }
        sShowing = 1;
        sTop = 0;
        sHeld = 0xFFFF; /* what's held now doesn't count until let go */
        if (current != 0) {
            *sSeen = current;
            portSettingsSave(); /* once: a crash or a closed window doesn't bring it back */
        }
        printf("[whats_new] %s\n", portWhatsNewVersion);
        fflush(stdout);
    }
    if (!sHudClosed && seqGetSeq() == SEQ_GAME) {
        sHudClosed = 1; /* (it would draw over the page) */
        statusWinForceClose();
    }
    dispEntry(CAMERA_2D, 904.0f, 1, winDisp, NULL);
}

void portWhatsNewPad(PADStatus* pads) {
    u16 now = 0, pressed;
    int i, dir = 0;
    if (!sShowing) {
        return;
    }
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (pads[i].err == PAD_ERR_NONE) {
            now |= pads[i].button;
            if (pads[i].stickY > 48) {
                now |= PAD_BUTTON_UP;
            } else if (pads[i].stickY < -48) {
                now |= PAD_BUTTON_DOWN;
            }
        }
    }
    pressed = now & ~sHeld;
    sHeld = now;
    if (pressed & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_START)) {
        sShowing = 0;
        sDone = 1;
    } else if (now & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) {
        /* held: once, then every 4 reads after 20 */
        dir = (now & PAD_BUTTON_UP) ? -1 : 1;
        if (pressed & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) {
            sRepeat = 0;
        } else if (++sRepeat < 20 || sRepeat % 4 != 0) {
            dir = 0;
        }
    } else if (pressed & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) {
        dir = (pressed & PAD_BUTTON_LEFT) ? -WIN_ROWS : WIN_ROWS;
    }
    if (dir != 0) {
        layout(WIN_W - 52.0f);
        scroll(dir, WIN_ROWS);
    }
    /* the game gets nothing pressed while the window is up */
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        pads[i].button = 0;
        pads[i].stickX = pads[i].stickY = pads[i].substickX = pads[i].substickY = 0;
        pads[i].triggerLeft = pads[i].triggerRight = pads[i].analogA = pads[i].analogB = 0;
#ifdef TARGET_PC
        pads[i].extButton = 0;
#endif
    }
}
