/* Pages any Start menu form can open (port/include/port/pause_forms.h), in
 * the game's windows and font, over the Settings tab's page:
 *
 * - the text page (portPauseTextOpen): what is typed so far over a grid of
 *   letters, then Space, Delete, Paste and Done; the stick or the D-pad move
 *   through the grid, A types, B rubs out a letter (with none, leaves the text
 *   as it was), Start is Done. A tap on a key is its A (port/include/port/menu_tap.h);
 *   on a desktop the keyboard types too (port/include/port/menu_typing.h).
 * - the lines page (portPauseInfoOpen): a feature's lines (a log, a wizard's
 *   step, an address), scrolled with up and down when there are more than
 *   fit, with a QR code beside them when the feature has one; A, X and Y are
 *   the feature's. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/text_fit.h"
#include "port/menu_tap.h"
#include "port/menu_typing.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_stdinc.h>
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

#define PANEL_X (-270.0f)
#define PANEL_TOP 144.0f
#define PANEL_W 540.0f
#define PANEL_H 270.0f

static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x60, 0x60, 0x60, 0xFF};
static const GXColor kHeading = {0x5A, 0x46, 0x9C, 0xFF};
static const GXColor kFrame = {0x30, 0x28, 0x20, 0xFF};

static void box(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    Mtx m;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, c);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXConcat(camGetPtr(CAMERA_2D)->view, m, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXPosition3f32(0.0f, -h, 0.0f);
    GXPosition3f32(w, -h, 0.0f);
    GXPosition3f32(w, 0.0f, 0.0f);
}

void portPauseBox(float x, float y, float w, float h, GXColor c) {
    box(x, y, w, h, c);
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void text(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* t, int right) {
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 k = full > 0.0f && full * s > w ? w / full : s;
    portTextFit("page-text", t, full * s, w, 1);
    winFontInit();
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", t);
}

/* '<' starts a tag in the game's text: shown as a look-alike */
static void plain(char* s) {
    for (; *s != '\0'; s++) {
        *s = *s == '<' ? '(' : *s == '>' ? ')' : *s;
    }
}

static char sHelp[512];
static const PortPausePage kTextPage, kInfoPage; /* (below) */

/* --- the text page --- */

#define GRID_COLS 12
#define CELL_W 40.0f
#define CELL_H 30.0f
#define GRID_X (-238.0f)
#define GRID_TOP 64.0f
#define MAX_TEXT 300
enum { KEY_SPACE = -1, KEY_DELETE = -2, KEY_PASTE = -3, KEY_DONE = -4 };

typedef struct Key {
    int ch;        /* a letter, or KEY_* */
    int col, span; /* its place in its row of the grid */
    int row;
} Key;

static Key sKeys[128];
static int sKeyCount, sKeyAt;
static const char* sTitle;
static char sText[MAX_TEXT + 1];
static int sMax;
static void (*sDone)(const char* text);

static void addKey(int ch, int row, int col, int span) {
    if (sKeyCount < (int)(sizeof(sKeys) / sizeof(sKeys[0]))) {
        sKeys[sKeyCount].ch = ch;
        sKeys[sKeyCount].row = row;
        sKeys[sKeyCount].col = col;
        sKeys[sKeyCount++].span = span;
    }
}

void portPauseTextOpen(const char* title, const char* start, int max, const char* charset,
                       void (*done)(const char* text)) {
    int i, n = 0, space = 0, row;
    sTitle = title;
    sMax = max < MAX_TEXT ? max : MAX_TEXT;
    snprintf(sText, sizeof(sText), "%s", start != NULL ? start : "");
    sText[sMax] = '\0';
    sDone = done;
    sKeyCount = 0;
    for (i = 0; charset[i] != '\0'; i++) {
        if (charset[i] == ' ') {
            space = 1;
            continue;
        }
        addKey((unsigned char)charset[i], n / GRID_COLS, n % GRID_COLS, 1);
        n++;
    }
    row = (n + GRID_COLS - 1) / GRID_COLS;
    i = 0;
    if (space) {
        addKey(KEY_SPACE, row, 0, 3);
        i = 3;
    }
    addKey(KEY_DELETE, row, i, 3);
    addKey(KEY_PASTE, row, i + 3, 3);
    addKey(KEY_DONE, row, i + 6, GRID_COLS - i - 6);
    sKeyAt = 0;
    portPauseOpenPage(&kTextPage, 0);
}

static f32 keyX(const Key* k) {
    return GRID_X + k->col * CELL_W;
}

static f32 keyY(const Key* k) {
    return GRID_TOP - k->row * CELL_H;
}

/* the key in the row next to this one's (dir -1 up, +1 down) under its middle */
static int keyAcross(int at, int dir) {
    const int rows = sKeys[sKeyCount - 1].row + 1;
    const int row = (sKeys[at].row + dir + rows) % rows;
    const f32 mid = sKeys[at].col + sKeys[at].span / 2.0f;
    int i, best = at;
    f32 bestD = 1e9f;
    for (i = 0; i < sKeyCount; i++) {
        if (sKeys[i].row == row) {
            const f32 m = sKeys[i].col + sKeys[i].span / 2.0f;
            const f32 d = m > mid ? m - mid : mid - m;
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
    }
    return best;
}

static void typeKey(const Key* k) {
    const size_t len = strlen(sText);
    if (k->ch == KEY_DELETE) {
        if (len > 0) {
            sText[len - 1] = '\0';
        }
    } else if (k->ch == KEY_PASTE) {
        char* clip = SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
        if (clip != NULL) {
            size_t i, n = strlen(sText);
            for (i = 0; clip[i] != '\0' && n < (size_t)sMax; i++) {
                const unsigned char c = (unsigned char)clip[i];
                if (c >= 0x20 && c < 0x7F) { /* (the game's font: ASCII is the same in every release) */
                    sText[n++] = (char)c;
                }
            }
            sText[n] = '\0';
            SDL_free(clip);
        }
    } else if (len < (size_t)sMax) {
        sText[len] = k->ch == KEY_SPACE ? ' ' : (char)k->ch;
        sText[len + 1] = '\0';
    }
}

/* the mouse and touch: the key under the pointer selected; a click on the selected one types it */
static int textPointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0) {
        return 0;
    }
    for (i = 0; i < sKeyCount; i++) {
        const f32 x = keyX(&sKeys[i]), y = keyY(&sKeys[i]);
        if (gx >= x && gx < x + sKeys[i].span * CELL_W - 4.0f && gy <= y && gy > y - CELL_H + 4.0f) {
            if (i == sKeyAt) {
                return kind == 1;
            }
            sKeyAt = i;
            psndSFXOn((const char*)0x20005);
            return 0;
        }
    }
    return 0;
}

/* the key that types c: itself, else the other case of it (a code's capitals), or -1 */
static int keyFor(char c) {
    int i, pass;
    for (pass = 0; pass < 2; pass++) {
        const int want = c == ' '  ? KEY_SPACE
                         : pass == 0 ? (unsigned char)c
                         : (c >= 'a' && c <= 'z') ? c - 'a' + 'A'
                         : (c >= 'A' && c <= 'Z') ? c - 'A' + 'a'
                                                  : (unsigned char)c;
        for (i = 0; i < sKeyCount; i++) {
            if (sKeys[i].ch == want) {
                return i;
            }
        }
    }
    return -1;
}

/* the keyboard's typing (port/include/port/menu_typing.h): letters, Backspace,
 * Enter (Done) and Esc (B); 1 when it closed the page */
static int textTyped(void) {
    static const Key kDel = {KEY_DELETE, 0, 0, 0};
    char typed[80];
    int i, n;
    portMenuTypingFrame();
    n = portMenuTyped(typed, sizeof(typed));
    for (i = 0; i < n; i++) {
        if (typed[i] == '\n') {
            psndSFXOn((const char*)0x20012);
            if (sDone != NULL) {
                sDone(sText);
            }
            return 1;
        }
        if (typed[i] == '\x1B') {
            psndSFXOn((const char*)0x20013);
            return 1;
        }
        if (typed[i] == '\b') {
            typeKey(&kDel);
            psndSFXOn((const char*)0x20005);
        } else {
            const int k = keyFor(typed[i]);
            if (k >= 0) {
                sKeyAt = k;
                typeKey(&sKeys[k]);
                psndSFXOn((const char*)0x20005);
            }
        }
    }
    return 0;
}

static int textMain(WindowWork* wp) {
    const int click = textPointer();
    if (textTyped()) {
        return -1;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        /* as the game's own name entry and the Online tab's keyboard: a letter
         * rubbed out; with none, back (the text as it was) */
        const size_t len = strlen(sText);
        psndSFXOn((const char*)0x20013);
        if (len == 0) {
            return -1;
        }
        sText[len - 1] = '\0';
        return 0;
    }
    if (wp->buttonTrg & PAD_BUTTON_START) { /* Start: Done, as the game's name entry */
        sKeyAt = sKeyCount - 1;
    }
    if (click || (wp->buttonTrg & (PAD_BUTTON_A | PAD_BUTTON_START))) {
        const Key* k = &sKeys[sKeyAt];
        psndSFXOn((const char*)0x20012);
        if (k->ch == KEY_DONE) {
            if (sDone != NULL) {
                sDone(sText);
            }
            return -1;
        }
        typeKey(k);
    } else if (wp->buttonTrg & PAD_BUTTON_X) { /* (as a keyboard's Backspace) */
        const Key del = {KEY_DELETE, 0, 0, 0};
        psndSFXOn((const char*)0x20005);
        typeKey(&del);
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        sKeyAt = keyAcross(sKeyAt, (wp->dirRep & DEMO_STICK_UP) ? -1 : 1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int dir = (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1;
        const int row = sKeys[sKeyAt].row;
        do {
            sKeyAt = (sKeyAt + dir + sKeyCount) % sKeyCount;
        } while (sKeys[sKeyAt].row != row);
        psndSFXOn((const char*)0x20005);
    }
    wp->helpTgtX = keyX(&sKeys[sKeyAt]) - 14.0f;
    wp->helpTgtY = keyY(&sKeys[sKeyAt]) - CELL_H / 2.0f + 2.0f;
    return 0;
}

static const char* keyName(int ch) {
    return ch == KEY_SPACE ? "Space" : ch == KEY_DELETE ? "Delete" : ch == KEY_PASTE ? "Paste" : "Done";
}

static const char* textHelp(void) {
    const Key* k = &sKeys[sKeyAt];
    const char* t = k->ch == KEY_DONE    ? "{A}: keep this text. {B}: leave it as it was."
                    : k->ch == KEY_PASTE ? "{A}: the text you copied, typed here."
                                         : "{A} types the key, {B} rubs out a letter (on none, it goes back), {START} is Done.";
    portPauseWrap(portTrGame(t), sHelp, sizeof(sHelp));
    return sHelp;
}

static void textDisp(WindowWork* wp, float x, float y) {
    char shown[MAX_TEXT + 2];
    int i;
    winKirinukiGX(wp, 0, x + PANEL_X, y + PANEL_TOP, PANEL_W, PANEL_H);
    text(x + PANEL_X + 20.0f, y + PANEL_TOP - 12.0f, 300.0f, 0.75f, kHeading, portTrGame(sTitle), 0);
    /* what is typed so far, its end in view, and the cursor */
    box(x + PANEL_X + 18.0f, y + 104.0f, PANEL_W - 36.0f, 28.0f, kFrame);
    box(x + PANEL_X + 20.0f, y + 102.0f, PANEL_W - 40.0f, 24.0f, (GXColor){0xF4, 0xEE, 0xDC, 0xFF});
    {
        const size_t len = strlen(sText);
        const size_t from = len > 40 ? len - 40 : 0;
        snprintf(shown, sizeof(shown), "%s%s_", from > 0 ? ".." : "", sText + from);
        plain(shown);
        text(x + PANEL_X + 28.0f, y + 98.0f, PANEL_W - 56.0f, 0.8f, kBlack, shown, 0);
    }
    for (i = 0; i < sKeyCount; i++) {
        const Key* k = &sKeys[i];
        const f32 kx = x + keyX(k), ky = y + keyY(k), w = k->span * CELL_W - 4.0f;
        const int here = i == sKeyAt;
        char c[2] = {(char)k->ch, '\0'};
        if (here) {
            box(kx - 2.0f, ky + 2.0f, w + 4.0f, CELL_H, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        box(kx, ky, w, CELL_H - 4.0f, here ? (GXColor){0xE8, 0xD8, 0xB0, 0xFF} : (GXColor){0xD8, 0xD0, 0xC0, 0xFF});
        if (k->ch < 0) {
            const char* name = portTrGame(keyName(k->ch));
            const f32 tw = (f32)FontGetMessageWidth(name) * 0.7f;
            text(kx + (w - (tw < w - 8.0f ? tw : w - 8.0f)) / 2.0f, ky - 4.0f, w - 8.0f, 0.7f, here ? kBlack : kGrey,
                 name, 0);
        } else {
            plain(c);
            text(kx + w / 2.0f - (f32)FontGetMessageWidth(c) * 0.4f, ky - 3.0f, w, 0.8f, here ? kBlack : kGrey, c, 0);
        }
    }
}

static const PortPausePage kTextPage = {NULL, textMain, textDisp, textHelp};

/* --- the lines page --- */

#define INFO_VISIBLE 9
#define INFO_STEP 24.0f
#define INFO_TOP 104.0f
#define QR_SIDE 210.0f

static const PortPauseInfo* sInfo;
static char sLines[256][PORT_PAUSE_LINE];
static int sLineCount, sFirst;

static void infoRead(void) {
    sLineCount = sInfo != NULL && sInfo->lines != NULL ? sInfo->lines(sLines, 256) : 0;
    sLineCount = sLineCount < 0 ? 0 : sLineCount > 256 ? 256 : sLineCount;
}

static int infoMax(void) {
    return sLineCount > INFO_VISIBLE ? sLineCount - INFO_VISIBLE : 0;
}

void portPauseInfoOpen(const PortPauseInfo* info) {
    sInfo = info;
    infoRead();
    sFirst = info->bottom ? infoMax() : 0;
    portPauseOpenPage(&kInfoPage, 0);
}

static int infoMain(WindowWork* wp) {
    const int max0 = infoMax();
    infoRead();
    if (sInfo->bottom && sFirst == max0) { /* (a log: kept at its end as it grows) */
        sFirst = infoMax();
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (sInfo->button != NULL && (wp->buttonTrg & (PAD_BUTTON_A | PAD_BUTTON_X | PAD_BUTTON_Y))) {
        const int b = (wp->buttonTrg & PAD_BUTTON_A) ? 'A' : (wp->buttonTrg & PAD_BUTTON_X) ? 'X' : 'Y';
        psndSFXOn((const char*)0x20012);
        if (sInfo->button(b) < 0) {
            return -1;
        }
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        const int to = sFirst + ((wp->dirRep & DEMO_STICK_UP) ? -1 : 1);
        if (to >= 0 && to <= infoMax()) {
            sFirst = to;
            psndSFXOn((const char*)0x20005);
        }
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) { /* a page at a time */
        const int to = sFirst + ((wp->dirRep & DEMO_STICK_LEFT) ? -INFO_VISIBLE : INFO_VISIBLE);
        sFirst = to < 0 ? 0 : to > infoMax() ? infoMax() : to;
        psndSFXOn((const char*)0x20005);
    }
    if (sFirst > infoMax()) {
        sFirst = infoMax();
    }
    wp->helpTgtX = PANEL_X + 4.0f;
    wp->helpTgtY = PANEL_TOP - 20.0f;
    return 0;
}

static const char* infoHelp(void) {
    const char* t = sInfo != NULL && sInfo->help != NULL ? sInfo->help() : NULL;
    portPauseWrap(portTrGame(t != NULL ? t : "{B}: back."), sHelp, sizeof(sHelp));
    return sHelp;
}

static void infoDisp(WindowWork* wp, float x, float y) {
    const int qr = sInfo != NULL && sInfo->qrSize != NULL ? sInfo->qrSize() : 0;
    const f32 width = qr > 0 ? PANEL_W - QR_SIDE - 60.0f : PANEL_W - 40.0f;
    int i;
    winKirinukiGX(wp, 0, x + PANEL_X, y + PANEL_TOP, PANEL_W, PANEL_H);
    if (sInfo == NULL) {
        return;
    }
    text(x + PANEL_X + 20.0f, y + PANEL_TOP - 12.0f, 300.0f, 0.75f, kHeading, portTrGame(sInfo->title), 0);
    for (i = sFirst; i < sLineCount && i < sFirst + INFO_VISIBLE; i++) {
        char line[PORT_PAUSE_LINE];
        snprintf(line, sizeof(line), "%s", sLines[i]);
        plain(line);
        text(x + PANEL_X + 20.0f, y + INFO_TOP - (i - sFirst) * INFO_STEP, width, 0.7f, kBlack, line, 0);
    }
    if (sLineCount > INFO_VISIBLE) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d-%d/%d", sFirst + 1,
                 sFirst + INFO_VISIBLE < sLineCount ? sFirst + INFO_VISIBLE : sLineCount, sLineCount);
        text(x + PANEL_X + PANEL_W - 20.0f, y + PANEL_TOP - 12.0f, 120.0f, 0.6f, kGrey, pos, 1);
    }
    if (qr > 0 && sInfo->qrModule != NULL) { /* white with its margin, the dark modules on it */
        const f32 module = (f32)(int)(QR_SIDE / (f32)(qr + 8));
        const f32 full = module * (f32)(qr + 8);
        const f32 qx = x + PANEL_X + PANEL_W - 24.0f - full, qy = y + PANEL_TOP - 30.0f;
        int mx, my;
        box(qx, qy, full, full, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        for (my = 0; my < qr; my++) {
            for (mx = 0; mx < qr; mx++) {
                if (sInfo->qrModule(mx, my)) {
                    box(qx + (mx + 4) * module, qy - (my + 4) * module, module, module, kBlack);
                }
            }
        }
    }
}

static const PortPausePage kInfoPage = {NULL, infoMain, infoDisp, infoHelp};
