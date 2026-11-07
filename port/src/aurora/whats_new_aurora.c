/* The "what's new" page (port/include/port/whats_new.h), in the game's own
 * windows: the first start after an update's window (a system window over
 * the title screen, the file select or the field), and the Start menu's page over the
 * Settings tab (General > What's new). The first start's shows the notes'
 * highlights, one screen, and where the full list is (A, B or a click
 * closes it); the page the highlights, then the full list, scrolled with up
 * and down, the wheel, or a click above or below the middle. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/text_fit.h"
#include "port/menu_tap.h"
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
#define PAGE_ROWS 11 /* the Start menu page's lines */

#define KIND_NOTE 3 /* a line of the page's own (where the full list is), grey */

typedef struct Shown {
    int kind; /* PORT_WHATS_NEW_*, KIND_NOTE, or -1 a blank line */
    int indent;
    char text[160];
} Shown;

/* what the list holds: the Start menu's page (the highlights, then the full
 * list), or the first start's one screen (the highlights, and where the rest is) */
enum { LIST_PAGE, LIST_FIRST_START };

static Shown sLines[MAX_LINES];
static int sLineCount;
static float sLaidWidth = -1.0f;
static int sLaidMode = -1, sLaidLang = -1;
static int sTop;

static const char* gameText(const PortWhatsNewLine* l) {
    return strcmp(PORT_GAME_ID, "G8MJ01") == 0 ? l->ascii : l->latin1;
}

/* a highlight in the menu language (its English the key), else the build's own */
static const char* highlightText(const PortWhatsNewLine* l) {
    const char* t = portTrGameNoPlayAs(l->utf8);
    return t == NULL || t == l->utf8 ? gameText(l) : t;
}

static void add(int kind, int indent, const char* text, size_t len) {
    Shown* s;
    if (sLineCount >= MAX_LINES) {
        static int sTold;
        if (!sTold) {
            sTold = 1;
            printf("[whats new] more than %d lines: the rest left out\n", MAX_LINES);
        }
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

/* a letter's bytes: two for a Shift-JIS one on the JP release (its menu
 * texts in Japanese), else one; a line never breaks inside one */
static size_t charLen(const char* p) {
    const unsigned char c = (unsigned char)*p;
    if (c == 0) {
        return 1;
    }
    if (strcmp(PORT_GAME_ID, "G8MJ01") == 0 && ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) &&
        p[1] != '\0') {
        return 2;
    }
    return 1;
}

/* a space's bytes at p (' ', or the JP release's full-width one, between its words), else 0 */
static size_t spaceLen(const char* p) {
    if (*p == ' ') {
        return 1;
    }
    if (strcmp(PORT_GAME_ID, "G8MJ01") == 0 && (unsigned char)p[0] == 0x81 && (unsigned char)p[1] == 0x40) {
        return 2;
    }
    return 0;
}

/* one entry of the list as lines no wider than w (between words), the
 * lines after its first indented */
static void wrap(int kind, const char* text, float w) {
    char item[200];
    const char* p;
    int indent = 0;
    if (kind == PORT_WHATS_NEW_ITEM) {
        snprintf(item, sizeof(item), "- %s", text);
    } else {
        snprintf(item, sizeof(item), "%s", text);
    }
    p = item;
    while (*p != '\0') {
        const float room = w - (indent ? 14.0f : 0.0f);
        size_t len = strlen(p), fit = len, k;
        if (width(p, len) > room) {
            fit = 0;
            for (k = charLen(p); k <= len; k += charLen(p + k)) {
                if ((p[k] == '\0' || spaceLen(p + k) > 0) && width(p, k) <= room) {
                    fit = k;
                }
                if (p[k] == '\0') {
                    break;
                }
            }
            if (fit == 0) { /* one long word: as many letters as fit */
                fit = charLen(p);
                while (fit < len && width(p, fit + charLen(p + fit)) <= room) {
                    fit += charLen(p + fit);
                }
            }
        }
        add(kind, indent, p, fit);
        p += fit;
        while (spaceLen(p) > 0) {
            p += spaceLen(p);
        }
        indent = 1;
    }
}

/* the list (mode LIST_*) broken into lines no wider than w */
static void layout(float w, int mode) {
    const int lang = portTrLanguage();
    int i;
    if (sLaidWidth == w && sLaidMode == mode && sLaidLang == lang) {
        return;
    }
    sLaidWidth = w;
    sLaidMode = mode;
    sLaidLang = lang;
    sLineCount = 0;
    for (i = 0; i < portWhatsNewHighlightCount; i++) {
        wrap(PORT_WHATS_NEW_ITEM, highlightText(&portWhatsNewHighlights[i]), w);
    }
    if (mode == LIST_FIRST_START) {
        if (portWhatsNewHighlightCount > 0) {
            add(-1, 0, "", 0);
            wrap(KIND_NOTE, portTrGame("The full list is in the Start menu: Settings, General, What's new."), w);
            return;
        }
        /* (notes with no highlights: the full list, as before) */
    }
    for (i = 0; i < portWhatsNewCount; i++) {
        const PortWhatsNewLine* l = &portWhatsNewLines[i];
        if (i == 0 && l->kind == PORT_WHATS_NEW_SECTION) {
            /* ("What's new", under a title that says it; after the highlights, where the full list starts) */
            if (sLineCount > 0) {
                /* the page: the highlights a page of their own, the full list from the next one's top */
                do {
                    add(-1, 0, "", 0);
                } while (mode == LIST_PAGE && sLineCount % PAGE_ROWS != 0 && sLineCount < MAX_LINES);
                wrap(PORT_WHATS_NEW_SECTION, portTrGame("The full list"), w);
            }
            continue;
        }
        if (l->kind != PORT_WHATS_NEW_ITEM && sLineCount > 0) {
            add(-1, 0, "", 0);
        }
        wrap(l->kind, gameText(l), w);
    }
}

static void title(char* out, size_t size) {
    snprintf(out, size, portTrGame("What's new in Paper Cut %s"), portWhatsNewVersion);
}

/* the list at (x, y) (its top left), w wide, rows lines of it from sTop;
 * dark: on the system window's dark blue (light text), else on paper; mode LIST_* */
static void listDisp(float x, float y, float w, int rows, int dark, int mode) {
    const GXColor black = dark ? (GXColor){0xFF, 0xFF, 0xFF, 0xFF} : (GXColor){0x00, 0x00, 0x00, 0xFF};
    const GXColor heading = dark ? (GXColor){0xFF, 0xE0, 0x50, 0xFF} : (GXColor){0x3A, 0x1C, 0x66, 0xFF};
    const GXColor sub = dark ? (GXColor){0xFF, 0xB0, 0x60, 0xFF} : (GXColor){0x6A, 0x3C, 0x14, 0xFF};
    const GXColor grey = dark ? (GXColor){0xB0, 0xB8, 0xD0, 0xFF} : (GXColor){0x60, 0x60, 0x60, 0xFF};
    const float top = y;
    char head[120];
    int i;
    layout(w, mode);
    winFontInit();
    title(head, sizeof(head));
    {
        const float full = (float)FontGetMessageWidth(head);
        const float k = full * 0.85f > w ? w / full : 0.85f;
        portTextFit("whatsnew-title", head, full * 0.85f, w, 1);
        winFontSet((Vec){x, y, 0.0f}, (Vec){k, 0.85f, 0.85f}, heading, "%s", head);
    }
    {
        /* installed by the update at this start (update_install.cpp): said under the title */
        const char* applied = SDL_getenv("TTYD_UPDATE_APPLIED");
        if (applied != NULL && applied[0] != '\0') {
            char note[120];
            snprintf(note, sizeof(note), portTrGame("Updated to Paper Cut %s."), applied);
            portTextFit("whatsnew-updated", note, (float)FontGetMessageWidth(note) * 0.6f, w, 0);
            winFontSet((Vec){x, y - 24.0f, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, sub, "%s", note);
            y -= 16.0f;
            rows--;
        }
    }
    if (sLineCount == 0) {
        winFontSet((Vec){x, y - 34.0f, 0.0f}, (Vec){TEXT_SCALE, TEXT_SCALE, TEXT_SCALE}, grey, "%s",
                   portTrGame("No notes for this version."));
        return;
    }
    for (i = 0; i < rows && sTop + i < sLineCount; i++) {
        const Shown* s = &sLines[sTop + i];
        if (s->kind < 0) {
            continue;
        }
        portTextFit("whatsnew-line", s->text, (float)FontGetMessageWidth(s->text) * TEXT_SCALE,
                    w - (s->indent ? 14.0f : 0.0f), 0);
        winFontSet((Vec){x + (s->indent ? 14.0f : 0.0f), y - 34.0f - i * LINE_STEP, 0.0f},
                   (Vec){TEXT_SCALE, TEXT_SCALE, TEXT_SCALE},
                   s->kind == PORT_WHATS_NEW_SECTION   ? heading
                   : s->kind == PORT_WHATS_NEW_SUBHEAD ? sub
                   : s->kind == KIND_NOTE              ? grey
                                                       : black,
                   "%s", s->text);
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

static char sHelp[256];

static void pageOpen(int arg) {
    (void)arg;
    sTop = 0;
}

static int pageMain(WindowWork* wp) {
    float gx, gy;
    const int point = portMenuPoint(&gx, &gy); /* (a menu up: the wheel scrolls, a right click goes back) */
    layout(PAGE_W - 16.0f, LIST_PAGE);
    if (wp->buttonTrg & (PAD_BUTTON_B | PAD_BUTTON_A)) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (point == 1) { /* a click or a tap: the page above the middle, the next one below it */
        if (scroll(gy > 0.0f ? -PAGE_ROWS : PAGE_ROWS, PAGE_ROWS)) {
            psndSFXOn((const char*)0x20005);
        }
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
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
    portPauseWrap(portTrGame("Up and down to read, left and right a page at a time; {B} goes back."), sHelp,
                  sizeof(sHelp));
    return sHelp;
}

static void pageDisp(WindowWork* wp, float x, float y) {
    const u32 seed = portRandState();
    winKirinukiGX(wp, 0, x + PAGE_X - 8.0f, y + PAGE_TOP + 14.0f, PAGE_W + 16.0f, 270.0f);
    listDisp(x + PAGE_X + 8.0f, y + PAGE_TOP - 4.0f, PAGE_W - 16.0f, PAGE_ROWS, 0, LIST_PAGE);
    portRandSetState(seed);
}

const PortPausePage gPortWhatsNewPage = {pageOpen, pageMain, pageDisp, pageHelp};

/* --- the first start after an update ------------------------------------ */

#define WIN_X (-262.0f)
#define WIN_W 524.0f
#define WIN_H 450.0f
#define WIN_ROWS 17

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
    const char* applied = SDL_getenv("TTYD_UPDATE_APPLIED");
    char foot[200];
    float h, top;
    int rows;
    (void)camId;
    (void)param;
    /* one screen: the window as tall as the highlights need, in the middle (at most WIN_H) */
    layout(WIN_W - 52.0f, LIST_FIRST_START);
    rows = sLineCount < 1 ? 1 : sLineCount > WIN_ROWS ? WIN_ROWS : sLineCount;
    h = 30.0f + 34.0f + rows * LINE_STEP + 54.0f + (applied != NULL && applied[0] != '\0' ? 16.0f : 0.0f);
    h = h > WIN_H ? WIN_H : h;
    top = h * 0.5f;
    windowDispGX_System(0, 0xFF, WIN_X, top, WIN_W, h);
    listDisp(WIN_X + 26.0f, top - 30.0f, WIN_W - 52.0f, WIN_ROWS, 1, LIST_FIRST_START);
    snprintf(foot, sizeof(foot), "%s", portTrGame("{A} or {B} closes this."));
    {
        const float full = (float)FontGetMessageWidth(foot);
        const float room = WIN_W - 52.0f;
        const float k = full * 0.55f > room ? room / full : 0.55f;
        portTextFit("whatsnew-foot", foot, full * 0.55f, room, 1);
        winFontInit();
        winFontSet((Vec){WIN_X + 26.0f, top - h + 36.0f, 0.0f}, (Vec){k, 0.55f, 0.55f}, grey, "%s", foot);
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
        } else if (*sSeen == 0 && !*played && !portSettingsFileFound()) {
            /* the first start ever: nothing was updated (a settings.txt from
             * 0.1.0 or 0.2.0 has neither key, but it was an update) */
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
    {
        /* the mouse and touch: up while the window is (the wheel scrolls, a right click is B); a click or tap closes it */
        float gx, gy;
        if (portMenuPoint(&gx, &gy) == 1) {
            sShowing = 0;
            sDone = 1;
            return;
        }
    }
    dispEntry(CAMERA_2D, 904.0f, 1, winDisp, NULL);
}

void portWhatsNewPad(PADStatus* pads) {
    static u16 sSwallow; /* the buttons that closed the window: the game's only once let go */
    u16 now = 0, pressed;
    int i, dir = 0;
    if (!sShowing) {
        if (sSwallow != 0) {
            u16 still = 0;
            for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
                if (pads[i].err == PAD_ERR_NONE) {
                    still |= pads[i].button & sSwallow;
                    pads[i].button &= (u16)~sSwallow;
                }
            }
            sSwallow = still; /* (else the A that closed it starts the game from the title) */
        }
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
        sSwallow = now & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_START);
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
        layout(WIN_W - 52.0f, LIST_FIRST_START);
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
