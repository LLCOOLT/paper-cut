/* The pause menu's Online tab (port/include/port/pause_settings.h): online
 * co-op in the game's own menu, on the launcher's lobby's own functions
 * (port/include/port/online_menu.h), in the game's windows and font.
 *
 * The page: the status at the top (offline, hosting, joined, playing,
 * watching; the other player's name, the round trip, how the game goes, the
 * watchers), the room's code large on its right to read out; under it the
 * rows: host a room (or by address), join by code or by address, watch, the
 * room list, then the options (your name, "List my room", where the host
 * starts from, the port). Once in a room: the code and the invite link to the
 * clipboard, Start together (the host, once the other is here), Leave.
 *
 * Codes, addresses and names are typed on a keyboard of the page's own (a
 * grid of keys the stick moves through, A types, B rubs out, Start is OK; the
 * mouse and touch too), as a game's name entry. The room list is a page of
 * its own: the open rooms (their host's name, how many are in them), the
 * players online, A joins.
 *
 * During a session the menu runs in lockstep in every game of it (the host's
 * controller drives it), so the rows are then the same in all of them, and so
 * is what each does: the clipboard (only this computer's) and Leave, which
 * ends the session in all of them at once. What is shown differs (names, the
 * round trip): win_root.c keeps the game's random numbers as they were while
 * the port's tabs are drawn, so it changes nothing.
 *
 * src/window/win_root.c adds the tab (TARGET_PC blocks): its place in the row
 * (between the Wardrobe and Settings), its label (pause_settings_aurora.c
 * paints it from the Journal's), its page (menu id 7, active 18) and its help
 * text, which msgSearch takes from portPauseOnlineMsg. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/companion_menu.h"
#include "port/menu_tap.h"
#include "port/online.h"
#include "port/online_menu.h"
#include "port/notes.h"
#include "port/pause_notes.h"
#include "port/pause_settings.h"
#include "port/speech.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ROW_HEADING,
    ROW_HOST,
    ROW_HOST_ADDRESS,
    ROW_JOIN_CODE,
    ROW_JOIN_ADDRESS,
    ROW_WATCH,
    ROW_LIST,
    ROW_START,
    ROW_COPY_CODE,
    ROW_COPY_LINK,
    ROW_LEAVE,
    ROW_NAME,
    ROW_LIST_MINE,
    ROW_START_FROM,
    ROW_PORT,
    ROW_NOT_HERE, /* phones and tablets: online play from the launcher */
    ROW_NOTES,    /* paper notes (pause_notes_aurora.c): off a session only */
};

typedef struct Row {
    int kind;
    const char* label;
    const char* note;
    int confirm; /* A twice */
} Row;

#define MAX_ROWS 40
#define VISIBLE 6
/* the page: the status at the top, the rows under it */
#define PANEL_X (-262.0f)
#define PANEL_W 524.0f
#define STATUS_TOP 140.0f
#define STATUS_H 82.0f
#define ROWS_TOP 52.0f
#define ROWS_H 150.0f
#define ROW_X (-236.0f)
#define VALUE_X 240.0f
#define ROW_Y0 38.0f
#define ROW_STEP 24.0f
#define HELP_COLS 30

static Row sRows[MAX_ROWS];
static int sCount;
static s32 sRow, sTop;
static int sWasKind = -1;
static int sConfirm;
static const char* sAnswer; /* an action's answer, in the help box until the cursor moves (English) */
static int sAnswerN;
static char sHelp[1024];

/* the colours: the tab's orange, dark for headings */
static const GXColor kHeading = {0x7A, 0x36, 0x08, 0xFF};
static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x60, 0x60, 0x60, 0xFF};

/* --- text in the game's encoding --- */

static int jp(void) {
    return strcmp(PORT_GAME_ID, "G8MJ01") == 0;
}

/* a name or address as it came (UTF-8) for the game's font: Latin-1 on the US and EU
 * releases, ASCII on the JP one, '?' for what it can't show; '<' and '>' (a tag's) as ( ) */
static void gameText(const char* utf8, char* out, size_t size) {
    const unsigned char* p = (const unsigned char*)utf8;
    size_t n = 0;
    while (*p != '\0' && n + 1 < size) {
        unsigned c = *p++;
        if (c >= 0x80) { /* a UTF-8 sequence: its code point */
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            c &= more == 3 ? 0x07 : more == 2 ? 0x0F : 0x1F;
            for (; more > 0 && (*p & 0xC0) == 0x80; more--) {
                c = (c << 6) | (*p++ & 0x3F);
            }
        }
        if (c == '<') {
            c = '(';
        } else if (c == '>') {
            c = ')';
        } else if (c < 0x20 || c == 0x7F || (jp() && (c >= 0x80 || c == '\\' || c == '`')) || (c >= 0x80 && c < 0xA0) ||
                   c > 0xFF) {
            c = '?';
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

/* text in the game's encoding broken into lines no wider than w at scale s (between words;
 * the JP release's full-width space too): up to two, the second cut short with ... */
static void wrapWidth(const char* text, f32 w, f32 s, char* one, char* two, size_t size) {
    char line[256];
    size_t n = 0, lastBreak = 0;
    int which = 0;
    const char* p = text;
    one[0] = two[0] = '\0';
    line[0] = '\0';
    while (*p != '\0') {
        const int wide = jp() && (((unsigned char)*p >= 0x81 && (unsigned char)*p <= 0x9F) ||
                                  ((unsigned char)*p >= 0xE0 && (unsigned char)*p <= 0xFC)) && p[1] != '\0';
        const int brk = *p == ' ' || (wide && (unsigned char)p[0] == 0x81 && (unsigned char)p[1] == 0x40);
        if (n + 3 >= sizeof(line)) {
            break;
        }
        if (brk) {
            lastBreak = n;
        }
        line[n++] = *p++;
        if (wide) {
            line[n++] = *p++;
        }
        line[n] = '\0';
        if (which == 0 && FontGetMessageWidth(line) * s > w) {
            /* this line ends at the last break (or here, in a word longer than the line) */
            const size_t cut = lastBreak > 0 ? lastBreak : n - (wide ? 2 : 1);
            const size_t skip = lastBreak > 0 ? (line[cut] == ' ' ? 1 : 2) : 0;
            memcpy(one, line, cut);
            one[cut] = '\0';
            memmove(line, line + cut + skip, n - cut - skip + 1);
            n -= cut + skip;
            lastBreak = 0;
            which = 1;
        }
    }
    if (which == 0) {
        snprintf(one, size, "%s", line);
    } else {
        snprintf(two, size, "%s%s", line, *p != '\0' ? "..." : "");
    }
}

/* --- the keyboard (a page of its own over the tab's) --- */

enum { KEYS_CODE, KEYS_ADDRESS, KEYS_NAME };
enum { KEYS_JOIN, KEYS_WATCH, KEYS_SET_NAME, KEYS_SET_PORT };
/* the keys: 10 a row, then the row of actions */
static const char* const kKeys[] = {
    "23456789ABCDEFGHJKMNPQRSTUVWXYZ",                 /* PORT_ONLINE_CODE_CHARS */
    "1234567890abcdefghijklmnopqrstuvwxyz.:-[]",       /* an address */
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.!?'&+",    /* a name (and the shift: small letters) */
};
enum { ACT_PASTE, ACT_SPACE, ACT_SHIFT, ACT_DELETE, ACT_OK };
static const char* const kActNames[] = {"Paste", "Space", "Shift", "Delete", "OK"};
#define KEY_COLS 10
#define KEY_W 46.0f
#define KEY_H 28.0f
#define KEY_X0 (-207.0f) /* the first key's middle */
#define KEY_Y0 46.0f

static struct {
    int open;
    int layout, purpose;
    char text[64];
    int max;
    int key;   /* the selected key: the layout's, then its actions */
    int shift; /* a name's small letters */
} sKb;

static int kbActions(int* acts) {
    int n = 0;
    if (sKb.layout != KEYS_NAME) {
        acts[n++] = ACT_PASTE;
    }
    if (sKb.layout == KEYS_NAME) {
        acts[n++] = ACT_SPACE;
        acts[n++] = ACT_SHIFT;
    }
    acts[n++] = ACT_DELETE;
    acts[n++] = ACT_OK;
    return n;
}

static int kbKeyCount(void) {
    return (int)strlen(kKeys[sKb.layout]);
}

static int kbRows(void) {
    return (kbKeyCount() + KEY_COLS - 1) / KEY_COLS;
}

/* key k's middle; the actions in a row of their own, wider */
static void kbAt(int k, f32* x, f32* y, f32* w) {
    const int keys = kbKeyCount();
    if (k < keys) {
        *x = KEY_X0 + (k % KEY_COLS) * KEY_W;
        *y = KEY_Y0 - (k / KEY_COLS) * (KEY_H + 4.0f);
        *w = KEY_W - 6.0f;
    } else {
        int acts[5];
        const int n = kbActions(acts);
        const f32 aw = (KEY_COLS * KEY_W) / n;
        *x = KEY_X0 - KEY_W / 2 + aw * (k - keys) + aw / 2;
        *y = KEY_Y0 - kbRows() * (KEY_H + 4.0f) - 6.0f;
        *w = aw - 8.0f;
    }
}

static char kbChar(int k) {
    char c = kKeys[sKb.layout][k];
    if (sKb.layout == KEYS_NAME && sKb.shift && c >= 'A' && c <= 'Z') {
        c = (char)(c - 'A' + 'a');
    }
    return c;
}

static void kbOpen(int layout, int purpose, const char* text) {
    memset(&sKb, 0, sizeof(sKb));
    sKb.open = 1;
    sKb.layout = layout;
    sKb.purpose = purpose;
    sKb.max = layout == KEYS_CODE ? PORT_ONLINE_CODE_LENGTH : layout == KEYS_NAME ? 24 : 63;
    if (text != NULL) {
        size_t i, n = 0;
        for (i = 0; text[i] != '\0' && (int)n < sKb.max; i++) {
            if (layout != KEYS_CODE || text[i] != '-') {
                sKb.text[n++] = text[i];
            }
        }
        sKb.text[n] = '\0';
    }
    if (purpose == KEYS_SET_PORT) {
        sKb.max = 5;
    }
}

/* the typed text as shown: a code with its dash and the places still to type */
static void kbShown(char* out, size_t size) {
    if (sKb.layout == KEYS_CODE) {
        char code[PORT_ONLINE_CODE_LENGTH + 2];
        int i, n = 0;
        for (i = 0; i < PORT_ONLINE_CODE_LENGTH; i++) {
            if (i == PORT_ONLINE_ROOM_HALF) {
                code[n++] = '-';
            }
            code[n++] = i < (int)strlen(sKb.text) ? sKb.text[i] : '_';
        }
        code[n] = '\0';
        snprintf(out, size, "%s", code);
        return;
    }
    snprintf(out, size, "%s_", sKb.text);
}

/* OK: what the keyboard was for; 1 when done (the keyboard closes) */
static int kbDone(void) {
    const size_t len = strlen(sKb.text);
    switch (sKb.purpose) {
        case KEYS_JOIN:
        case KEYS_WATCH:
            if ((sKb.layout == KEYS_CODE && len != PORT_ONLINE_CODE_LENGTH) || len == 0) {
                sAnswer = sKb.layout == KEYS_CODE ? "A room code has 8 letters and numbers." : "Type the host's address.";
                sAnswerN++;
                return 0;
            }
            portOnlineMenuJoin(sKb.text, sKb.purpose == KEYS_WATCH);
            return 1;
        case KEYS_SET_NAME:
            if (len == 0) {
                return 0;
            }
            portOnlineMenuSetName(sKb.text);
            return 1;
        case KEYS_SET_PORT:
            if (atoi(sKb.text) < 1024 || atoi(sKb.text) > 65535) {
                sAnswer = "A port from 1024 to 65535.";
                sAnswerN++;
                return 0;
            }
            portOnlineMenuSetPort(atoi(sKb.text));
            return 1;
    }
    return 1;
}

static void kbType(char c) {
    const size_t len = strlen(sKb.text);
    if (sKb.purpose == KEYS_SET_PORT && (c < '0' || c > '9')) {
        return;
    }
    if ((int)len < sKb.max) {
        sKb.text[len] = c;
        sKb.text[len + 1] = '\0';
        psndSFXOn((const char*)0x20005);
    }
}

/* the mouse and touch on the keys: the one under the pointer selected, a click on it pressed */
static int kbPointer(void) {
    f32 gx, gy, kx, ky, kw;
    const int kind = portMenuPoint(&gx, &gy);
    int acts[5], k;
    const int total = kbKeyCount() + kbActions(acts);
    if (kind == 0) {
        return 0;
    }
    for (k = 0; k < total; k++) {
        kbAt(k, &kx, &ky, &kw);
        if (gx >= kx - kw / 2 && gx <= kx + kw / 2 && gy <= ky + KEY_H / 2 && gy > ky - KEY_H / 2) {
            if (k == sKb.key) {
                return kind == 1;
            }
            sKb.key = k;
            psndSFXOn((const char*)0x20005);
            return 0;
        }
    }
    return 0;
}

/* each frame the keyboard is open: 1 while it stays */
static int kbMain(WindowWork* wp) {
    int acts[5];
    const int nActs = kbActions(acts);
    const int keys = kbKeyCount();
    const int total = keys + nActs;
    const int rows = kbRows();
    int press = kbPointer() || (wp->buttonTrg & PAD_BUTTON_A);
    if (wp->buttonTrg & PAD_BUTTON_START) {
        sKb.key = total - 1; /* OK */
        press = 1;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        const size_t len = strlen(sKb.text);
        psndSFXOn((const char*)0x20013);
        if (len == 0) {
            sKb.open = 0;
            return 0;
        }
        sKb.text[len - 1] = '\0';
        return 1;
    }
    if (press) {
        if (sKb.key < keys) {
            kbType(kbChar(sKb.key));
        } else {
            switch (acts[sKb.key - keys]) {
                case ACT_PASTE: {
                    char code[16];
                    if (portOnlineMenuPaste(code, sizeof(code))) {
                        snprintf(sKb.text, sizeof(sKb.text), "%s", code);
                        psndSFXOn((const char*)0x20012);
                    } else {
                        sAnswer = "There's no room code on the clipboard.";
                        sAnswerN++;
                        psndSFXOn((const char*)0x20013);
                    }
                    break;
                }
                case ACT_SPACE:
                    kbType(' ');
                    break;
                case ACT_SHIFT:
                    sKb.shift = !sKb.shift;
                    psndSFXOn((const char*)0x20005);
                    break;
                case ACT_DELETE:
                    if (sKb.text[0] != '\0') {
                        sKb.text[strlen(sKb.text) - 1] = '\0';
                        psndSFXOn((const char*)0x20013);
                    }
                    break;
                case ACT_OK:
                    psndSFXOn((const char*)0x20012);
                    if (kbDone()) {
                        sKb.open = 0;
                        return 0;
                    }
                    break;
            }
        }
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int dir = (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1;
        if (sKb.key < keys) {
            const int row = sKb.key / KEY_COLS, inRow = row < rows - 1 ? KEY_COLS : keys - row * KEY_COLS;
            sKb.key = row * KEY_COLS + (sKb.key % KEY_COLS + dir + inRow) % inRow;
        } else {
            sKb.key = keys + (sKb.key - keys + dir + nActs) % nActs;
        }
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        const int dir = (wp->dirRep & DEMO_STICK_UP) ? -1 : 1;
        f32 x, y, w;
        int row = sKb.key < keys ? sKb.key / KEY_COLS : rows;
        int best = 0, k;
        f32 bestD = 1e9f;
        kbAt(sKb.key, &x, &y, &w);
        row = (row + dir + rows + 1) % (rows + 1);
        /* the nearest key in that row */
        for (k = 0; k < total; k++) {
            f32 kx, ky, kw;
            const int r = k < keys ? k / KEY_COLS : rows;
            if (r != row) {
                continue;
            }
            kbAt(k, &kx, &ky, &kw);
            if ((kx - x) * (kx - x) < bestD) {
                bestD = (kx - x) * (kx - x);
                best = k;
            }
        }
        sKb.key = best;
        psndSFXOn((const char*)0x20005);
    }
    /* the hand at the key */
    {
        f32 x, y, w;
        kbAt(sKb.key, &x, &y, &w);
        (void)w;
        wp->helpTgtX = KEY_X0 - KEY_W / 2 - 26.0f; /* (left of the keys, on the key's row: the key lit) */
        wp->helpTgtY = y + 6.0f;
    }
    return 1;
}

/* a filled box of colour c, its top-left at (x, y) */
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

/* text at (x, y) no wider than w: scaled down from s to fit; its width */
static f32 fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
    return full * k;
}

static f32 centredText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    winFontSet((Vec){x - full * k / 2, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
    return full * k;
}

static void kbDisp(WindowWork* wp, f32 x, f32 y) {
    static const char* const kTitles[] = {"Room code", "Address", "Your name"};
    int acts[5], k;
    const int keys = kbKeyCount();
    const int total = keys + kbActions(acts);
    char shown[96], game[96];
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, 68.0f);
    winFontInit();
    fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 8.0f, 200.0f, 0.75f, kHeading,
            portTrGame(sKb.purpose == KEYS_SET_PORT ? "Port" : sKb.purpose == KEYS_WATCH && sKb.layout == KEYS_CODE
                                                                  ? "Room code to watch"
                                                                  : kTitles[sKb.layout]),
            0);
    kbShown(shown, sizeof(shown));
    gameText(shown, game, sizeof(game));
    centredText(x, y + STATUS_TOP - 26.0f, PANEL_W - 40.0f, sKb.layout == KEYS_CODE ? 1.4f : 1.1f, kBlack, game);
    winKirinukiGX(wp, 0, x + PANEL_X, y + KEY_Y0 + KEY_H / 2 + 10.0f, PANEL_W,
                  (kbRows() + 1) * (KEY_H + 4.0f) + 26.0f);
    for (k = 0; k < total; k++) {
        f32 kx, ky, kw;
        const int sel = k == sKb.key;
        char label[16];
        kbAt(k, &kx, &ky, &kw);
        box(x + kx - kw / 2, y + ky + KEY_H / 2, kw, KEY_H,
            sel ? (GXColor){0xF4, 0xC0, 0x80, 0xFF} : (GXColor){0xF8, 0xEC, 0xDC, 0xFF});
        box(x + kx - kw / 2, y + ky - KEY_H / 2 + 2.0f, kw, 2.0f, (GXColor){0xB0, 0x80, 0x50, 0xFF});
        winFontInit();
        if (k < keys) {
            label[0] = kbChar(k);
            label[1] = '\0';
            centredText(x + kx, y + ky + 11.0f, kw - 4.0f, 0.9f, kBlack, label);
        } else {
            const int a = acts[k - keys];
            const char* name = portTrGame(kActNames[a]);
            centredText(x + kx, y + ky + 10.0f, kw - 8.0f, 0.75f,
                        a == ACT_SHIFT && sKb.shift ? kHeading : kBlack, name);
        }
    }
}

/* --- the room list (a page of its own) --- */

static struct {
    int open;
    int row; /* 0 Refresh, then the rooms */
    int top;
} sList;
#define LIST_VISIBLE 6
/* the list's row i (0 Refresh, then the rooms from the view's first): its text's top */
static f32 listRowY(int i) {
    return i == 0 ? 68.0f : 40.0f - (i - 1 - sList.top) * ROW_STEP;
}

static int listMain(WindowWork* wp) {
    const int n = portOnlineMenuListCount();
    const int was = sList.row;
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int press = (wp->buttonTrg & PAD_BUTTON_A) != 0;
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sList.open = 0;
        return 0;
    }
    if (kind != 0) { /* the row under the pointer */
        int i;
        for (i = 0; i <= n; i++) {
            const f32 ry = listRowY(i) - 8.0f;
            if (i > 0 && (i - 1 < sList.top || i - 1 >= sList.top + LIST_VISIBLE)) {
                continue;
            }
            if (gx >= ROW_X - 16.0f && gx <= VALUE_X + 8.0f && gy <= ry + ROW_STEP / 2 && gy > ry - ROW_STEP / 2) {
                if (i == sList.row) {
                    press |= kind == 1;
                } else {
                    sList.row = i;
                }
                break;
            }
        }
    }
    if (press) {
        psndSFXOn((const char*)0x20012);
        if (sList.row == 0) {
            portOnlineMenuListRefresh();
        } else {
            char code[16], name[32];
            int players;
            if (portOnlineMenuListGet(sList.row - 1, code, sizeof(code), name, sizeof(name), &players)) {
                portOnlineMenuJoin(code, 0);
                sList.open = 0;
                return 0;
            }
        }
    } else if (wp->dirRep & DEMO_STICK_UP) {
        sList.row = (sList.row + n) % (n + 1);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        sList.row = (sList.row + 1) % (n + 1);
    }
    if (sList.row > n) {
        sList.row = n;
    }
    if (sList.row != was) {
        psndSFXOn((const char*)0x20005);
    }
    if (sList.row > 0) {
        if (sList.row - 1 < sList.top) {
            sList.top = sList.row - 1;
        }
        if (sList.row - 1 >= sList.top + LIST_VISIBLE) {
            sList.top = sList.row - LIST_VISIBLE;
        }
    }
    wp->helpTgtX = ROW_X - 24.0f;
    wp->helpTgtY = listRowY(sList.row);
    return 1;
}

static void listDisp(WindowWork* wp, f32 x, f32 y) {
    const int state = portOnlineMenuListState();
    const int n = portOnlineMenuListCount();
    const int online = portOnlineMenuListOnline();
    char text[160];
    int i;
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, 58.0f);
    winFontInit();
    fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 8.0f, 300.0f, 0.9f, kHeading, portTrGame("Open rooms"), 0);
    if (online >= 0) {
        snprintf(text, sizeof(text), portTrGame("Players online: %d"), online);
        fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 10.0f, 220.0f, 0.75f, kGrey, text, 1);
    }
    {
        const char* say = state == PORT_ROOM_LIST_LOADING      ? "Looking for rooms..."
                          : state == PORT_ROOM_LIST_OLD_RELAY ? "The room list isn't on this relay yet."
                          : state == PORT_ROOM_LIST_FAILED    ? "The room list couldn't be reached."
                          : n == 0 ? "No open rooms right now. Host one with List my room on."
                                   : "Anyone can join a listed room: A joins.";
        fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 34.0f, PANEL_W - 40.0f, 0.7f, kBlack, portTrGame(say), 0);
    }
    /* Refresh, then the rooms */
    winKirinukiGX(wp, 0, x + PANEL_X, y + 78.0f, PANEL_W, 176.0f);
    winFontInit();
    fitText(x + ROW_X, y + listRowY(0), 200.0f, 0.8f, sList.row == 0 ? kBlack : kGrey, portTrGame("Refresh"), 0);
    for (i = sList.top; i < n && i < sList.top + LIST_VISIBLE; i++) {
        char code[16], name[32], gname[40], shownCode[16];
        int players;
        const f32 ry = y + listRowY(i + 1);
        const GXColor c = sList.row == i + 1 ? kBlack : kGrey;
        if (!portOnlineMenuListGet(i, code, sizeof(code), name, sizeof(name), &players)) {
            break;
        }
        gameText(name[0] != '\0' ? name : "?", gname, sizeof(gname));
        snprintf(text, sizeof(text), portTrGame("%s's room"), gname);
        fitText(x + ROW_X, ry, 230.0f, 0.8f, c, text, 0);
        snprintf(shownCode, sizeof(shownCode), "%.4s-%.4s", code, code + 4);
        fitText(x + 60.0f, ry, 110.0f, 0.7f, c, shownCode, 0);
        snprintf(text, sizeof(text), portTrGame(players == 1 ? "%d player" : "%d players"), players);
        fitText(x + VALUE_X, ry, 90.0f, 0.75f, c, text, 1);
    }
}

/* --- the rows --- */

static void add(int kind, const char* label, const char* note, int confirm) {
    if (sCount < MAX_ROWS) {
        sRows[sCount].kind = kind;
        sRows[sCount].label = label;
        sRows[sCount].note = note;
        sRows[sCount].confirm = confirm;
        sCount++;
    }
}

static int selectable(int kind) {
    return kind != ROW_HEADING;
}

/* the rows for now: during a session the same in every game of it (only what all of them know) */
static void build(void) {
    const int phase = portOnlineMenuPhase();
    const int relay = portOnlineMenuHasRelay();
    int i;
    sCount = 0;
    if (phase == PORT_ONLINE_MENU_PLAYING || phase == PORT_ONLINE_MENU_WATCHING) {
        add(ROW_COPY_CODE, "Copy room code", "The room's code to the clipboard, for someone to watch.", 0);
        add(ROW_COPY_LINK, "Copy invite link", "A papercut:// link to the clipboard: it opens Paper Cut on the room.", 0);
        add(ROW_LEAVE, "Leave the online game",
            "Ends the online game for everyone in it: each plays on alone from here.", 1);
    } else if (!portOnlineMenuCan()) {
        add(ROW_NOT_HERE, "Online play",
            "On this device, online play starts from the launcher's Online tab, before the game.", 0);
    } else if (phase == PORT_ONLINE_MENU_OFF || phase == PORT_ONLINE_MENU_FAILED) {
        if (relay) {
            add(ROW_HOST, "Host a room", "Opens a room with a code to give the other player: nothing to set up.", 0);
        }
        add(ROW_HOST_ADDRESS, "Host by address",
            "Waits on your port: on the same network, or with the port forwarded or a VPN.", 0);
        if (relay) {
            add(ROW_JOIN_CODE, "Join by code...", "Type the room code the host gives you.", 0);
        }
        add(ROW_JOIN_ADDRESS, "Join by address...", "Type the host's address (and :port when it isn't 47700).", 0);
        add(ROW_WATCH, "Watch a room...", "See two players' game as it goes, a few seconds behind, without playing.", 0);
        if (relay) {
            add(ROW_LIST, "Room list...", "The rooms open to anyone, and how many play online now.", 0);
        }
        if (portNotesOn()) {
            add(ROW_NOTES, "Paper notes...",
                "Leave a note where Mario stands for other players, and rate the ones they left in this room.", 0);
        }
        add(ROW_HEADING, "Options", NULL, 0);
        add(ROW_NAME, "Your name", "The name the other player sees.", 0);
        if (relay) {
            add(ROW_LIST_MINE, "List my room",
                "Rooms you host are in the room list: anyone can join them. Off, only those with the code can.", 0);
        }
        add(ROW_START_FROM, "Start from", "Where the online game starts when you host: the title, or where you are now.",
            0);
        add(ROW_PORT, "Port", "Hosting by address: the port others connect to.", 0);
    } else {
        if (phase == PORT_ONLINE_MENU_PEER_HERE) {
            add(ROW_START, "Start together",
                "Both games start again for the online game: you're Mario, they play the partner.", 0);
        }
        if (phase == PORT_ONLINE_MENU_HOSTING || phase == PORT_ONLINE_MENU_PEER_HERE ||
            phase == PORT_ONLINE_MENU_OPENING) {
            add(ROW_START_FROM, "Start from",
                "Where the online game starts: the title, or where you are now.", 0);
            if (portOnlineMenuCode()[0] != '\0') {
                add(ROW_COPY_CODE, "Copy room code", "The room's code to the clipboard.", 0);
                add(ROW_COPY_LINK, "Copy invite link",
                    "A papercut:// link to the clipboard: it opens Paper Cut on this room.", 0);
            }
            add(ROW_LEAVE, "Close the room", "Stops hosting.", 0);
        } else {
            add(ROW_LEAVE, "Leave", "Leaves the room.", 0);
        }
    }
    /* the cursor on the same kind of row as before */
    for (i = 0; i < sCount; i++) {
        if (sRows[i].kind == sWasKind) {
            sRow = i;
            break;
        }
    }
    if (sRow >= sCount) {
        sRow = 0;
    }
    while (sRow + 1 < sCount && !selectable(sRows[sRow].kind)) {
        sRow++;
    }
    sWasKind = sRows[sRow].kind;
    if (sRow < sTop) {
        sTop = sRow;
    }
    if (sRow >= sTop + VISIBLE) {
        sTop = sRow - VISIBLE + 1;
    }
    if (sTop > 0 && sTop + VISIBLE > sCount) {
        sTop = sCount - VISIBLE < 0 ? 0 : sCount - VISIBLE;
    }
}

static void valueText(const Row* r, char* out, size_t size) {
    out[0] = '\0';
    switch (r->kind) {
        case ROW_NAME:
            gameText(portOnlineMenuName(), out, size);
            break;
        case ROW_LIST_MINE:
            snprintf(out, size, "%s", portTrGame(portOnlineMenuListMine() ? "On" : "Off"));
            break;
        case ROW_START_FROM:
            snprintf(out, size, "%s", portTrGame(portOnlineMenuStartMode() ? "Where you are" : "The title"));
            break;
        case ROW_PORT:
            snprintf(out, size, "%d", portOnlineMenuPort());
            break;
    }
}

/* A (dir 0), or left and right: the row's action; its answer for the help box, or NULL */
static const char* act(const Row* r, int dir) {
    switch (r->kind) {
        case ROW_HOST:
            if (dir == 0) {
                portOnlineMenuHost(0);
            }
            return NULL;
        case ROW_HOST_ADDRESS:
            if (dir == 0) {
                portOnlineMenuHost(1);
            }
            return NULL;
        case ROW_JOIN_CODE:
            if (dir == 0) {
                const char* last = portOnlineMenuLastTyped();
                kbOpen(KEYS_CODE, KEYS_JOIN, strchr(last, '.') == NULL && strchr(last, ':') == NULL ? last : NULL);
            }
            return NULL;
        case ROW_JOIN_ADDRESS:
            if (dir == 0) {
                const char* last = portOnlineMenuLastTyped();
                kbOpen(KEYS_ADDRESS, KEYS_JOIN, strchr(last, '.') != NULL || strchr(last, ':') != NULL ? last : NULL);
            }
            return NULL;
        case ROW_WATCH:
            if (dir == 0) {
                kbOpen(portOnlineMenuHasRelay() ? KEYS_CODE : KEYS_ADDRESS, KEYS_WATCH, NULL);
            }
            return NULL;
        case ROW_LIST:
            if (dir == 0) {
                memset(&sList, 0, sizeof(sList));
                sList.open = 1;
                portOnlineMenuListRefresh(); /* (fetched as it opens, and on Refresh: never polled) */
            }
            return NULL;
        case ROW_START:
            if (dir == 0) {
                portOnlineMenuStart();
            }
            return NULL;
        case ROW_NOTES:
            if (dir == 0) {
                portPauseNotesOpen();
            }
            return NULL;
        case ROW_COPY_CODE:
        case ROW_COPY_LINK:
            if (dir != 0) {
                return NULL;
            }
            return portOnlineMenuCopy(r->kind == ROW_COPY_LINK) ? "Copied." : "There's no room to copy: this game isn't in one.";
        case ROW_LEAVE:
            if (dir == 0) {
                portOnlineMenuLeave();
            }
            return NULL;
        case ROW_NAME:
            if (dir == 0) {
                kbOpen(KEYS_NAME, KEYS_SET_NAME, portOnlineMenuName());
            }
            return NULL;
        case ROW_LIST_MINE:
            portOnlineMenuSetListMine(!portOnlineMenuListMine());
            return NULL;
        case ROW_START_FROM:
            portOnlineMenuSetStartMode(!portOnlineMenuStartMode());
            return NULL;
        case ROW_PORT:
            if (dir == 0) {
                char port[8];
                snprintf(port, sizeof(port), "%d", portOnlineMenuPort());
                kbOpen(KEYS_ADDRESS, KEYS_SET_PORT, port);
            } else {
                portOnlineMenuSetPort(portOnlineMenuPort() + dir);
            }
            return NULL;
        case ROW_NOT_HERE:
            return NULL;
    }
    return NULL;
}

/* --- the help box --- */

static void help(WindowWork* wp, const char* english) {
    static char tags[4][32];
    static int n;
    static char shown[1024];
    char text[1024];
    snprintf(text, sizeof(text), "%s", english);
    if (strcmp(text, shown) == 0) {
        winMsgEntry(wp, 0, tags[n % 4], 0);
        return;
    }
    snprintf(shown, sizeof(shown), "%s", text);
    n++;
    snprintf(tags[n % 4], sizeof(tags[0]), "pc_menu_online_%d", n);
    portPauseWrap(portTrGame(english), sHelp, sizeof(sHelp));
    winMsgEntry(wp, 0, tags[n % 4], 0);
    portSpeechMenu(portTrGame(english));
}

static void pointAt(WindowWork* wp) {
    const Row* r = &sRows[sRow];
    if (sKb.open) {
        help(wp, sAnswer != NULL ? sAnswer
                 : sKb.purpose == KEYS_SET_PORT ? "Type the port, then OK. B rubs out a number; on none, it goes back."
                 : "A types the key, B rubs out a letter (on none, it goes back), Start is OK.");
        return;
    }
    if (sList.open) {
        help(wp, sList.row == 0 ? "Asks the relay again for the open rooms." : "A joins this room.");
        return;
    }
    if (portPauseNotesIsOpen()) {
        help(wp, portPauseNotesHelp());
        return;
    }
    wp->helpTgtX = ROW_X - 24.0f;
    wp->helpTgtY = ROW_Y0 - (sRow - sTop) * ROW_STEP - 8.0f + 8.0f;
    if (sAnswer != NULL) {
        help(wp, sAnswer);
    } else if (sConfirm) {
        help(wp, "Press A again to confirm.");
    } else {
        help(wp, r->note != NULL ? r->note : r->label);
    }
}

/* --- the tab --- */

void portPauseOnlineInit(WindowWork* wp) {
    (void)wp;
    sRow = sTop = 0;
    sWasKind = -1;
    sConfirm = 0;
    sAnswer = NULL;
    memset(&sKb, 0, sizeof(sKb));
    memset(&sList, 0, sizeof(sList));
    portPauseNotesClose();
    build();
}

void portPauseOnlineInit2(WindowWork* wp) {
    wp->flags |= WIN_FLAG_SUBMENU;
    build();
    pointAt(wp);
}

static void move(int dir) {
    int guard = MAX_ROWS;
    do {
        sRow = (sRow + dir + sCount) % sCount;
    } while (!selectable(sRows[sRow].kind) && --guard > 0);
    if (sRow < sTop) {
        sTop = sRow;
    }
    if (sRow >= sTop + VISIBLE) {
        sTop = sRow - VISIBLE + 1;
    }
    sWasKind = sRows[sRow].kind;
}

/* the mouse and touch on the rows: the one under the pointer selected, a click on it A */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0 || gx < ROW_X - 16.0f || gx > VALUE_X + 8.0f) {
        return 0;
    }
    for (i = sTop; i < sCount && i < sTop + VISIBLE; i++) {
        const f32 y = ROW_Y0 - (i - sTop) * ROW_STEP - 8.0f;
        if (selectable(sRows[i].kind) && gy <= y + ROW_STEP / 2 && gy > y - ROW_STEP / 2) {
            if (i == sRow) {
                return kind == 1;
            }
            sRow = i;
            sWasKind = sRows[i].kind;
            psndSFXOn((const char*)0x20005);
            return 0;
        }
    }
    return 0;
}

s32 portPauseOnlineMain(WindowWork* wp) {
    const s32 wasRow = sRow;
    const int wasPage = sKb.open * 2 + sList.open;
    if (sKb.open) {
        const int answerN = sAnswerN;
        if (!kbMain(wp)) {
            sAnswer = NULL;
        } else if (sAnswerN == answerN && (wp->buttonTrg != 0 || wp->dirRep != 0)) {
            sAnswer = NULL; /* (an answer stays until the next key) */
        }
        build();
        pointAt(wp);
        return 0;
    }
    if (sList.open) {
        listMain(wp);
        build();
        pointAt(wp);
        return 0;
    }
    if (portPauseNotesIsOpen()) {
        portPauseNotesMain(wp);
        build();
        pointAt(wp);
        return 0;
    }
    build(); /* the lobby moves on by itself */
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sAnswer = NULL;
        sConfirm = 0;
        return -1;
    }
    if (pointer() || (wp->buttonTrg & PAD_BUTTON_A)) {
        const Row* r = &sRows[sRow];
        psndSFXOn((const char*)0x20012);
        if (r->confirm && !sConfirm) {
            sConfirm = 1;
            sAnswer = NULL;
        } else {
            sConfirm = 0;
            sAnswer = act(r, 0);
            sAnswerN++;
        }
    } else if (wp->dirRep & DEMO_STICK_UP) {
        move(-1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        move(1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int k = sRows[sRow].kind;
        if (k == ROW_LIST_MINE || k == ROW_START_FROM || k == ROW_PORT) {
            act(&sRows[sRow], (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1);
            psndSFXOn((const char*)0x20005);
        }
    }
    if (sRow != wasRow) {
        sAnswer = NULL;
        sConfirm = 0;
    }
    (void)wasPage;
    build();
    pointAt(wp);
    return 0;
}

void portPauseOnlineExit(WindowWork* wp) {
    (void)wp;
    sKb.open = 0;
    sList.open = 0;
    portPauseNotesClose();
}

/* the status at the top: what goes on, who with, the room's code to read out */
static void statusDisp(WindowWork* wp, f32 x, f32 y) {
    const int phase = portOnlineMenuPhase();
    const char* code = portOnlineMenuCode();
    const char* address = portOnlineMenuAddress();
    char peer[48], line[200], detail[200];
    const char* title;
    const int ping = portOnlineMenuPing();
    const int watchers = portOnlineMenuWatchers();
    f32 codeW = 0.0f;
    gameText(portOnlineMenuPeer()[0] != '\0' ? portOnlineMenuPeer() : "?", peer, sizeof(peer));
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, STATUS_H);
    switch (phase) {
        case PORT_ONLINE_MENU_OPENING: title = "Opening a room..."; break;
        case PORT_ONLINE_MENU_HOSTING: title = "Hosting: waiting for someone to join."; break;
        case PORT_ONLINE_MENU_PEER_HERE: title = "%s joined."; break;
        case PORT_ONLINE_MENU_JOINING: title = "Joining..."; break;
        case PORT_ONLINE_MENU_JOINED: title = "In %s's room: waiting for them to start."; break;
        case PORT_ONLINE_MENU_WATCH_WAIT: title = "Watching: waiting for the players to start."; break;
        case PORT_ONLINE_MENU_STARTING: title = "Starting the online game..."; break;
        case PORT_ONLINE_MENU_PLAYING: title = "Playing online with %s."; break;
        case PORT_ONLINE_MENU_WATCHING: title = "Watching %s's game."; break;
        case PORT_ONLINE_MENU_FAILED: title = "Offline"; break;
        default: title = "Offline"; break;
    }
    snprintf(line, sizeof(line), portTrGame(title), peer);
    winFontInit();
    /* the code, large, on the right: read out to the other player */
    if (code[0] != '\0' && phase != PORT_ONLINE_MENU_FAILED) {
        fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 10.0f, 160.0f, 0.65f, kGrey, portTrGame("Room code"), 1);
        codeW = fitText(x + PANEL_X + PANEL_W - 18.0f, y + STATUS_TOP - 28.0f, 200.0f, 1.45f, kHeading, code, 1);
    } else if (address[0] != '\0') {
        char shown[80];
        gameText(address, shown, sizeof(shown));
        fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 10.0f, 160.0f, 0.65f, kGrey, portTrGame("Your address"), 1);
        codeW = fitText(x + PANEL_X + PANEL_W - 18.0f, y + STATUS_TOP - 34.0f, 220.0f, 0.95f, kHeading, shown, 1);
    }
    {
        const f32 room = PANEL_W - 44.0f - (codeW > 0.0f ? codeW + 20.0f : 0.0f);
        fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 10.0f, room, 0.9f, kHeading, line, 0);
        detail[0] = '\0';
        if (phase == PORT_ONLINE_MENU_FAILED) {
            snprintf(detail, sizeof(detail), "%s", portTrGame(portOnlineMenuReason()));
        } else if (phase == PORT_ONLINE_MENU_OFF) {
            snprintf(detail, sizeof(detail), "%s",
                     portTrGame("Play together over the internet: one hosts, the other joins with the code."));
        } else {
            size_t n = 0;
            if (ping >= 0) {
                n += snprintf(detail + n, sizeof(detail) - n, portTrGame("Round trip %d ms"), ping);
            }
            if ((phase == PORT_ONLINE_MENU_PEER_HERE || phase == PORT_ONLINE_MENU_PLAYING) && n < sizeof(detail)) {
                const char* link = portOnlineMenuLink();
                n += snprintf(detail + n, sizeof(detail) - n, "%s%s", n > 0 ? ", " : "",
                              portTrGame(strcmp(link, "relay") == 0 ? "through the relay" : "direct"));
            }
            if (watchers > 0 && n < sizeof(detail)) {
                n += snprintf(detail + n, sizeof(detail) - n, "%s", n > 0 ? ", " : "");
                if (n < sizeof(detail)) {
                    snprintf(detail + n, sizeof(detail) - n, portTrGame("%d watching"), watchers);
                }
            }
        }
        /* (two lines when it's long: the reason a room failed) */
        {
            char one[256], two[256];
            wrapWidth(detail, room, 0.7f, one, two, sizeof(one));
            fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 38.0f, room, 0.7f, kBlack, one, 0);
            if (two[0] != '\0') {
                fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 58.0f, room, 0.7f, kBlack, two, 0);
            }
        }
    }
}

void portPauseOnlineDisp(s32 camId, WindowWork* wp, s32 idx) {
    const f32 x = wp->menu[idx].x;
    const f32 y = wp->menu[idx].y;
    const int inside = wp->active == PORT_PAUSE_ONLINE_TAB + 11;
    int i;
    (void)camId;
    winBgGX(wp, PORT_PAUSE_ONLINE_TAB, x, y);
    if (sKb.open) {
        kbDisp(wp, x, y);
        return;
    }
    if (sList.open) {
        listDisp(wp, x, y);
        return;
    }
    if (portPauseNotesIsOpen()) {
        portPauseNotesDisp(wp, x, y);
        return;
    }
    statusDisp(wp, x, y);
    winKirinukiGX(wp, 0, x + PANEL_X, y + ROWS_TOP, PANEL_W, ROWS_H);
    for (i = sTop; i < sCount && i < sTop + VISIBLE; i++) {
        const Row* r = &sRows[i];
        const f32 ry = y + ROW_Y0 - (i - sTop) * ROW_STEP;
        const GXColor c = !inside || i == sRow ? kBlack : kGrey;
        char value[64];
        winFontInit();
        if (r->kind == ROW_HEADING) {
            fitText(x + ROW_X - 8.0f, ry, 240.0f, 0.75f, kHeading, portTrGame(r->label), 0);
            continue;
        }
        fitText(x + ROW_X, ry, 250.0f, 0.8f, r->kind == ROW_NOT_HERE ? kGrey : c, portTrGame(r->label), 0);
        valueText(r, value, sizeof(value));
        if (value[0] != '\0') {
            fitText(x + VALUE_X, ry, 200.0f, 0.8f, c, value, 1);
        }
    }
    /* more rows above or below */
    if (sTop > 0 || sTop + VISIBLE < sCount) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)sRow + 1, sCount);
        winFontInit();
        fitText(x + VALUE_X, y + ROWS_TOP - ROWS_H + 22.0f, 80.0f, 0.6f, kGrey, pos, 1);
    }
}

const char* portPauseOnlineMsg(const char* tag) {
    if (strcmp(tag, "pc_menu_online") == 0) {
        static char sTab[256];
        portPauseWrap(portTrGame("Play online: host a room, or join one with its code."), sTab, sizeof(sTab));
        return sTab;
    }
    if (strncmp(tag, "pc_menu_online_", 15) == 0) {
        return sHelp; /* the selected row's (pointAt) */
    }
    return NULL;
}
