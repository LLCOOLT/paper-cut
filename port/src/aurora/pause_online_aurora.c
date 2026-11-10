/* The pause menu's Online tab (port/include/port/pause_settings.h): online
 * play (port/include/port/statesync.h) in the game's own menu, in the game's
 * windows and font; the prefs and the room list are online_prefs.cpp's
 * (port/include/port/online_menu.h).
 *
 * The page: the status at the top (hosting, joining, playing; the others'
 * names), the room's code large on its right to read out; under it the rows:
 * an invite's room, host a room (or by address), join by code or by address,
 * the room list, paper notes, watch, then the options (your name, "List my
 * room", the port). Once in a room: the versus fight and the raid boss, the
 * code and the invite link to the clipboard, Leave.
 *
 * Codes, addresses and names are typed on a keyboard of the page's own (a
 * grid of keys the stick moves through, A types, B rubs out, Start is OK; the
 * mouse and touch too), as a game's name entry. The room list is a page of
 * its own: the open rooms (their host's name, how many are in them), the
 * players online, A joins.
 *
 * src/window/win_root.c adds the tab (TARGET_PC blocks): its place in the row
 * (between the Wardrobe and Settings), its label (pause_settings_aurora.c
 * paints it from the Journal's), its page (menu id 7, active 18) and its help
 * text, which msgSearch takes from portPauseOnlineMsg. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/button_markup.h"
#include "port/text_fit.h"
#include "port/menu_crawl.h"
#include "port/companion_menu.h"
#include "port/menu_tap.h"
#include "port/menu_typing.h"
#include "port/online.h"
#include "port/online_menu.h"
#include "port/notes.h"
#include "port/pause_notes.h"
#include "port/pause_settings.h"
#include "port/raid.h"
#include "port/speech.h"
#include "port/statesync.h"
#include "port/settings.h"
#include "port/together.h"
#include "port/title_menu.h"
#include "port/players.h"
#include "port/modes.h"
#include "port/brothers.h"
#include "data/item_data.h"
#include "driver/icondrv.h"
#include "icon_ids.h"
#include "mario/mario_pouch.h"
#include "port/tr.h"
#include <SDL3/SDL_clipboard.h>
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

/* invite.cpp: an invite link's room code waiting (left there), or taken (once) */
int portOnlinePeekInvite(char* out, size_t size);
int portOnlineTakeInvite(char* out, size_t size);
int portConsoleAccurateLockedValue(unsigned flags, const int* value); /* console_accurate.cpp */

enum {
    ROW_HEADING,
    ROW_HOST,
    ROW_HOST_ADDRESS,
    ROW_JOIN_CODE,
    ROW_JOIN_LAST, /* Last room: the code typed last, joined again at once */
    ROW_JOIN_ADDRESS,
    ROW_WATCH,
    ROW_LIST,
    ROW_COPY_CODE,
    ROW_COPY_LINK,
    ROW_LEAVE,
    ROW_NAME,
    ROW_LIST_MINE,
    ROW_PORT,
    ROW_NOTES,       /* paper notes (pause_notes_aurora.c): off a session only */
    ROW_INVITE,      /* an invite link's room (invite.cpp) */
    ROW_GHOSTS,      /* the other players drawn see-through (statesync_ghost_pc.c) */
    ROW_BROTHERS,   /* Brothers mode, on and off (port/include/port/brothers.h) */
    ROW_MP_SETTINGS, /* Multiplayer settings...: the Settings tab's Multiplayer page, on this one */
    ROW_PARTY,       /* Party: the picture menu */
    ROW_SEATS,       /* Who's who */
    ROW_INVITE_PAGE, /* Invite online: the room's rows */
    ROW_SEAT,        /* Who's who: a seat (arg 1..4) */
    ROW_ASKED_YES,   /* another player asks for a swap: yes */
    ROW_ASKED_NO,    /*   and no */
    ROW_DM_SEAT,     /* the dungeon master: who commands (arg: the seat) */
    ROW_RUSH_FROM,   /* the boss rush: where it begins (port/include/port/modes.h's "Start at") */
    ROW_RUSH_GO,     /*   and its start */
    ROW_TEAM_BONUS,  /* the team bonus setting */
    ROW_HELP,        /* "?": how multiplayer works (a page of its own) */
    ROW_PLAYERS,    /* Players: the room's people (seat, computer, connection; the host removes one) */
    ROW_PLAYER,      /*   one (arg: the player 1..4) */
    ROW_RULES,       /* Rules: the host's rule settings that differ from this player's own (a guest's) */
    ROW_RULE,        /*   one (arg: its place in portStateSyncRuleDiffAt) */
    ROW_RETRY,       /* the online service couldn't be reached: Try again */
    ROW_GIVE_UP,     /*   and Back */
};

/* the page's lists: the front, Invite online, Who's who, the dungeon master's commander */
enum { SUB_MAIN, SUB_INVITE, SUB_SEATS, SUB_DM, SUB_RUSH, SUB_PLAYERS, SUB_RULES };
static int sSub;

typedef struct Row {
    int kind;
    const char* label;
    const char* note;
    int confirm; /* A twice */
    int arg;
} Row;

#define MAX_ROWS 40
#define VISIBLE 6 /* rows shown at most: fewer while the status above takes their room (sVisible) */
/* the page: the status at the top, the rows under it */
#define PANEL_X (-262.0f)
#define PANEL_W 524.0f
#define STATUS_TOP 140.0f
#define STATUS_H 82.0f
#define ROWS_TOP 52.0f
#define ROWS_H 150.0f
#define ROW_X (-236.0f)
#define VALUE_X 240.0f
/* (six rows in ROWS_H: the last one's letters end clear of the box's bottom edge, the JP font's too,
   which a step of 24 put a few pixels past it) */
#define ROW_Y0 40.0f
#define ROW_STEP 22.0f
#define HELP_COLS 30

static Row sRows[MAX_ROWS];
static int sCount;
static s32 sRow, sTop;
/* the status's lines past its two: the status box grows by sGrow, the rows' box starts that much lower
   and shows sVisible of its VISIBLE rows (syncStatusDisp sets them each frame) */
static f32 sGrow;
static int sVisible = VISIBLE;
static int sWasKind = -1;
static int sWasArg = -1; /* and its arg (-1: any): rows of one kind told apart by it (seats, players, rules) */
static int sConfirm;
static const char* sAnswer; /* an action's answer, in the help box until the cursor moves (English) */
static int sAnswerN;
/* Multiplayer settings...: the Settings tab's Multiplayer page shown and run here (pause_settings_aurora.c's
   portPauseSettingsOpenOn) until B; sSettingsAsk: chosen, opened once act's caller has the window */
static int sSettings, sSettingsAsk;
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

/* (text broken into lines: port/include/port/pause_settings.h's portPauseWrapLines, every word kept;
   the page makes room for them; one line narrowed to fit keeps its letters' shape: port/text_fit.h) */

/* --- the keyboard (a page of its own over the tab's) --- */

enum { KEYS_CODE, KEYS_ADDRESS, KEYS_NAME };
enum { KEYS_JOIN, KEYS_WATCH, KEYS_SET_NAME, KEYS_SET_PORT };
/* the keys: 10 a row, then the row of actions */
static const char* const kKeys[] = {
    "23456789ABCDEFGHJKMNPQRSTUVWXYZ",                 /* PORT_ONLINE_CODE_CHARS */
    "1234567890abcdefghijklmnopqrstuvwxyz.:-[]",       /* an address */
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.!?'&+",    /* a name (and the shift: small letters) */
};
enum { ACT_PASTE, ACT_SPACE, ACT_SHIFT, ACT_DELETE, ACT_OK, ACT_SWITCH, ACT_BACK };
/* (ACT_SWITCH: watching, a code or an address; named for the other) */
static const char* const kActNames[] = {"Paste", "Space", "Shift", "Delete", "OK", "By address", "Back"};
/* (ACT_BACK: the keyboard left as B on no letters leaves it, on touch too: the page's own Back button would sit
   on its last row) */
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
    size_t lastLen; /* the text's length a frame ago (a code just made whole: the hand to OK) */
    int caret;      /* where a letter goes in the text (L and R move it, as a console's keyboard) */
} sKb;

/* the text with c put in at the caret, or the letter before the caret rubbed out (B, Delete, Backspace) */
static void kbInsert(char c) {
    const int len = (int)strlen(sKb.text);
    if (sKb.caret > len) {
        sKb.caret = len;
    }
    memmove(sKb.text + sKb.caret + 1, sKb.text + sKb.caret, (size_t)(len - sKb.caret + 1));
    sKb.text[sKb.caret++] = c;
}

static int kbRubOut(void) {
    const int len = (int)strlen(sKb.text);
    if (sKb.caret > len) {
        sKb.caret = len;
    }
    if (sKb.caret == 0) {
        return 0;
    }
    memmove(sKb.text + sKb.caret - 1, sKb.text + sKb.caret, (size_t)(len - sKb.caret + 1));
    sKb.caret--;
    return 1;
}

static int kbActions(int* acts) {
    int n = 0;
    acts[n++] = ACT_BACK; /* (at most five: Back, Paste, By address, Delete, OK) */
    if (sKb.layout != KEYS_NAME) {
        acts[n++] = ACT_PASTE;
    }
    if (sKb.layout == KEYS_NAME) {
        acts[n++] = ACT_SPACE;
        acts[n++] = ACT_SHIFT;
    }
#ifndef __EMSCRIPTEN__ /* (the browser: codes only) */
    if (sKb.purpose == KEYS_WATCH && sKb.layout != KEYS_NAME && portOnlineMenuHasRelay()) {
        acts[n++] = ACT_SWITCH; /* a room's code, or the host's address */
    }
#endif
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
    sKb.caret = (int)strlen(sKb.text);
}

/* where the caret is in the text as shown (kbShown's): the letter it is before, -1 none (a whole code, the
   caret after it) */
static int kbShownCaret(void) {
    if (sKb.layout == KEYS_CODE) {
        return sKb.caret >= PORT_ONLINE_CODE_LENGTH ? -1 : sKb.caret + (sKb.caret >= PORT_ONLINE_ROOM_HALF ? 1 : 0);
    }
    return sKb.caret; /* (at the end: the '_' after the text) */
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
            /* the code or the address (a watcher: following the host's game) */
            portStateSyncWatchNext(sKb.purpose == KEYS_WATCH);
            if (!portStateSyncJoin(sKb.text)) {
                sAnswer = portStateSyncNote()[0] != '\0' ? portStateSyncNote() : "Couldn't join.";
                sAnswerN++;
                return 0;
            }
            return 1;
        case KEYS_SET_NAME:
            if (len == 0) {
                sAnswer = "Type a name first.";
                sAnswerN++;
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
        kbInsert(c);
        psndSFXOn((const char*)0x20005);
    }
}

int portPauseOnlineTakesStart(void) {
    return sKb.open || sConfirm; /* (Start is the keyboard's OK; a row's question waits for its second A) */
}

/* a room code from the clipboard (Paste, X) */
static void kbPaste(void) {
    char code[16];
    if (portOnlineMenuPaste(code, sizeof(code))) {
        snprintf(sKb.text, sizeof(sKb.text), "%s", code);
        sKb.caret = (int)strlen(sKb.text);
        psndSFXOn((const char*)0x20012);
        printf("[online] keyboard: a room code pasted\n");
        fflush(stdout);
    } else {
        sAnswer = "There's no room code on the clipboard.";
        sAnswerN++;
        psndSFXOn((const char*)0x20013);
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

/* the keyboard's typing (port/include/port/menu_typing.h): the layout's letters
 * (in its case: a code's capitals, an address's small letters), Backspace,
 * Enter (OK) and Esc; 1 for OK, -1 to leave, else 0 */
static int kbTyped(int total) {
    char typed[80];
    int i, n;
    portMenuTypingFrame();
    n = portMenuTyped(typed, sizeof(typed));
    for (i = 0; i < n; i++) {
        char c = typed[i];
        if (c == '\n') {
            sKb.key = total - 1;
            return 1;
        }
        if (c == '\x1B') {
            psndSFXOn((const char*)0x20013);
            return -1;
        }
        if (c == '\b') {
            if (kbRubOut()) {
                psndSFXOn((const char*)0x20013);
            }
            continue;
        }
        if (sKb.layout == KEYS_CODE && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        } else if (sKb.layout == KEYS_ADDRESS && c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c == ' ' && sKb.layout == KEYS_NAME) || (c != ' ' && strchr(kKeys[sKb.layout], c) != NULL) ||
            (sKb.layout == KEYS_NAME && c >= 'a' && c <= 'z')) {
            kbType(c);
        }
    }
    return 0;
}

static int kbMainFrame(WindowWork* wp);

/* each frame the keyboard is open: 1 while it stays (scripted runs: its text and caret said as they change) */
static int kbMain(WindowWork* wp) {
    static char sSaid[80] = "";
    char now[80];
    const int stays = kbMainFrame(wp);
    if (getenv("TTYD_INPUT") != NULL) {
        snprintf(now, sizeof(now), stays ? "'%s' caret %d" : "closed", sKb.text, sKb.caret);
        if (strcmp(now, sSaid) != 0) {
            snprintf(sSaid, sizeof(sSaid), "%s", now);
            printf("[online] keyboard: %s\n", now);
            fflush(stdout);
        }
    }
    return stays;
}

static int kbMainFrame(WindowWork* wp) {
    int acts[5];
    const int nActs = kbActions(acts);
    const int keys = kbKeyCount();
    const int total = keys + nActs;
    const int rows = kbRows();
    const int typed = kbTyped(total);
    int press = kbPointer() || (wp->buttonTrg & PAD_BUTTON_A) || typed > 0;
    if (typed < 0) {
        sKb.open = 0;
        return 0;
    }
    if (sKb.layout == KEYS_CODE && typed == 0 && strlen(sKb.text) == PORT_ONLINE_CODE_LENGTH &&
        strlen(sKb.text) != sKb.lastLen) {
        sKb.key = total - 1; /* a whole code: the hand on OK */
    }
    sKb.lastLen = strlen(sKb.text);
    if (wp->buttonTrg & PAD_BUTTON_START) {
        sKb.key = total - 1; /* OK */
        press = 1;
    }
    /* a console's keyboard: B rubs out (on no letters, leaves), Start is OK (above), Y a space where one goes,
       X pastes a room code, L and R move the caret (the hints under the keys) */
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        if (sKb.text[0] == '\0') {
            sKb.open = 0;
            return 0;
        }
        if (!kbRubOut()) {
            sKb.caret = (int)strlen(sKb.text); /* (the caret at the start: the last letter, as before) */
            kbRubOut();
        }
        return 1;
    }
    if ((wp->buttonTrg & PAD_BUTTON_Y) && sKb.layout == KEYS_NAME) {
        kbType(' ');
        return 1;
    }
    if ((wp->buttonTrg & PAD_BUTTON_X) && sKb.layout != KEYS_NAME) {
        kbPaste();
        return 1;
    }
    if (wp->buttonTrg & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {
        const int len = (int)strlen(sKb.text);
        const int to = sKb.caret + ((wp->buttonTrg & PAD_TRIGGER_L) ? -1 : 1);
        if (to >= 0 && to <= len) {
            sKb.caret = to;
            psndSFXOn((const char*)0x20005);
        }
        return 1;
    }
    if (press) {
        if (sKb.key < keys) {
            kbType(kbChar(sKb.key));
        } else {
            switch (acts[sKb.key - keys]) {
                case ACT_PASTE:
                    kbPaste();
                    break;
                case ACT_SWITCH: {
                    /* a code or an address: the other keyboard, the hand on this key still */
                    int k2, acts2[6], n2;
                    sKb.layout = sKb.layout == KEYS_CODE ? KEYS_ADDRESS : KEYS_CODE;
                    sKb.max = sKb.layout == KEYS_CODE ? PORT_ONLINE_CODE_LENGTH : 63;
                    sKb.text[0] = '\0';
                    sKb.caret = 0;
                    sKb.lastLen = 0;
                    n2 = kbActions(acts2);
                    for (k2 = 0; k2 < n2; k2++) {
                        if (acts2[k2] == ACT_SWITCH) {
                            sKb.key = kbKeyCount() + k2;
                        }
                    }
                    psndSFXOn((const char*)0x20005);
                    break;
                }
                case ACT_BACK:
                    psndSFXOn((const char*)0x20013);
                    sKb.open = 0;
                    return 0;
                case ACT_SPACE:
                    kbType(' ');
                    break;
                case ACT_SHIFT:
                    sKb.shift = !sKb.shift;
                    psndSFXOn((const char*)0x20005);
                    break;
                case ACT_DELETE:
                    if (kbRubOut()) {
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

/* text at (x, y) no wider than w: narrowed from s to fit, never below PORT_TEXT_SQUEEZE of its height (past
   that, smaller as a whole: never letters squashed thin); its width */
static f32 fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 textWas = portButtonsGameTextScale(s); /* (a {A} in it: centred on the letters at s, button_markup.h) */
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    const f32 h = portTextFitHeight(k, s);
    portTextFit(right ? "online-value" : "online-label", text, full * s, w, 1);
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, h, h}, c, "%s", text);
    portButtonsGameTextScale(textWas);
    return full * k;
}

static f32 centredText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text) {
    const f32 textWas = portButtonsGameTextScale(s);
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    const f32 h = portTextFitHeight(k, s);
    portTextFit("online-centred", text, full * s, w, 1);
    winFontSet((Vec){x - full * k / 2, y, 0.0f}, (Vec){k, h, h}, c, "%s", text);
    portButtonsGameTextScale(textWas);
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
    {
        const f32 s = sKb.layout == KEYS_CODE ? 1.4f : 1.1f;
        const f32 w = centredText(x, y + STATUS_TOP - 26.0f, PANEL_W - 40.0f, s, kBlack, game);
        const f32 full = (f32)FontGetMessageWidth(game);
        const int at = kbShownCaret();
        if (at >= 0 && at < (int)strlen(game) && full > 0.0f) {
            /* the caret (L and R move it): a bar under the letter it is before */
            char before[96];
            f32 x0, x1;
            const f32 k = w / full;
            snprintf(before, sizeof(before), "%.*s", at, game);
            x0 = (f32)FontGetMessageWidth(before) * k;
            snprintf(before, sizeof(before), "%.*s", at + 1, game);
            x1 = (f32)FontGetMessageWidth(before) * k;
            box(x - w / 2 + x0, y + STATUS_TOP - 26.0f - 24.0f * s, x1 - x0, 3.0f, kHeading);
        }
    }
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
            const char* name =
                portTrGame(a == ACT_SWITCH && sKb.layout == KEYS_ADDRESS ? "By code" : kActNames[a]);
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
    return i == 0 ? 68.0f : ROW_Y0 - (i - 1 - sList.top) * ROW_STEP;
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
            portStateSyncListRefresh();
        } else {
            char code[16], name[32];
            int players;
            if (portOnlineMenuListGet(sList.row - 1, code, sizeof(code), name, sizeof(name), &players)) {
                /* a player: the guest's place, or player 3 or 4's when it's taken (the relay says) */
                portStateSyncWatchNext(0);
                if (!portStateSyncJoin(code)) {
                    sAnswer = portStateSyncNote()[0] != '\0' ? portStateSyncNote() : "Couldn't join.";
                    sAnswerN++;
                }
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
                                   : "Anyone can join a listed room: {A} joins.";
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
        if (portOnlineMenuListMax(i) > 2) {
            snprintf(text, sizeof(text), "%d/%d", players, portOnlineMenuListMax(i)); /* "1/4": playing, places */
        } else {
            snprintf(text, sizeof(text), portTrGame(players == 1 ? "%d player" : "%d players"), players);
        }
        fitText(x + VALUE_X, ry, 90.0f, 0.75f, c, text, 1);
    }
}

/* --- the party games' picture menu (a page of its own) --- */

#define GRID_COLS 4
#define GRID_W 120.0f
#define GRID_H 84.0f
#define GRID_X0 (-180.0f) /* the first tile's middle */
#define GRID_Y0 70.0f

static struct {
    int open;
    int at; /* the tile: 0..PORT_PARTY_GAMES - 1 */
} sGrid;

static int gridPick(int i) {
    return PORT_PICK_VERSUS + i;
}

static void gridAt(int i, f32* x, f32* y) {
    *x = GRID_X0 + (i % GRID_COLS) * GRID_W;
    *y = GRID_Y0 - (i / GRID_COLS) * GRID_H;
}

static const char* choose(int pick, int arg, int* ok);

static int gridMain(WindowWork* wp) {
    const int n = PORT_PARTY_GAMES;
    const int was = sGrid.at;
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int press = (wp->buttonTrg & PAD_BUTTON_A) != 0;
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sGrid.open = 0;
        return 0;
    }
    if (kind != 0) {
        int i;
        for (i = 0; i < n; i++) {
            f32 x, y;
            gridAt(i, &x, &y);
            if (gx >= x - GRID_W / 2 + 4.0f && gx <= x + GRID_W / 2 - 4.0f && gy <= y + GRID_H / 2 - 4.0f &&
                gy > y - GRID_H / 2 + 4.0f) {
                if (i == sGrid.at) {
                    press |= kind == 1;
                } else {
                    sGrid.at = i;
                }
                break;
            }
        }
    }
    if (press) {
        psndSFXOn((const char*)0x20012);
        if (gridPick(sGrid.at) == PORT_PICK_DM) { /* who commands: the next list */
            sGrid.open = 0;
            sSub = SUB_DM;
            sRow = sTop = 0;
            return 0;
        }
        if (gridPick(sGrid.at) == PORT_PICK_BOSS_RUSH) { /* where it begins: the next list */
            sGrid.open = 0;
            sSub = SUB_RUSH;
            sRow = sTop = 0;
            sWasKind = -1;
            sWasArg = -1;
            return 0;
        }
        {
            int ok = 0;
            sAnswer = choose(gridPick(sGrid.at), 0, &ok);
            sAnswerN++;
            if (ok) {
                sGrid.open = 0; /* (the answer stays in the help box) */
                return 0;
            }
        }
    } else if (wp->dirRep & DEMO_STICK_LEFT) {
        sGrid.at = (sGrid.at + n - 1) % n;
    } else if (wp->dirRep & DEMO_STICK_RIGHT) {
        sGrid.at = (sGrid.at + 1) % n;
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        const int rows = (n + GRID_COLS - 1) / GRID_COLS;
        int r = sGrid.at / GRID_COLS, c = sGrid.at % GRID_COLS;
        r = (r + ((wp->dirRep & DEMO_STICK_UP) ? rows - 1 : 1)) % rows;
        sGrid.at = r * GRID_COLS + c < n ? r * GRID_COLS + c : n - 1;
    }
    if (sGrid.at != was) {
        psndSFXOn((const char*)0x20005);
        sAnswer = NULL;
    }
    {
        f32 x, y;
        gridAt(sGrid.at, &x, &y);
        wp->helpTgtX = x - GRID_W / 2 - 6.0f;
        wp->helpTgtY = y + 10.0f;
    }
    return 1;
}

static void gridDisp(WindowWork* wp, f32 x, f32 y) {
    int i;
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, 48.0f);
    winFontInit();
    fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 8.0f, 300.0f, 0.9f, kHeading, portTrGame("Party"), 0);
    fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 12.0f, 260.0f, 0.65f, kGrey,
            portTrGame("Everyone goes straight in. No save needed."), 1);
    winKirinukiGX(wp, 0, x + PANEL_X, y + GRID_Y0 + GRID_H / 2 + 6.0f, PANEL_W, 2 * GRID_H + 14.0f);
    for (i = 0; i < PORT_PARTY_GAMES; i++) {
        f32 tx, ty;
        const int sel = i == sGrid.at;
        const int pick = gridPick(i);
        gridAt(i, &tx, &ty);
        box(x + tx - GRID_W / 2 + 5.0f, y + ty + GRID_H / 2 - 5.0f, GRID_W - 10.0f, GRID_H - 10.0f,
            sel ? (GXColor){0xF4, 0xC0, 0x80, 0xFF} : (GXColor){0xF8, 0xEC, 0xDC, 0xFF});
        iconDispGx(&(Vec){x + tx, y + ty + 4.0f, 0.0f}, ICON_DISP_2D,
                   itemDataTable[portTogetherPickItem(pick)].icon_id, sel ? 1.0f : 0.85f);
        winFontInit();
        centredText(x + tx, y + ty - 16.0f, GRID_W - 16.0f, 0.65f, sel ? kBlack : kGrey,
                    portTrGame(portTogetherPickName(pick)));
    }
}

/* --- how multiplayer works (a page of its own, the "?" row) ---
 * Three pages, left and right (a tap: the next): on one screen in two, then online. Each line a picture (the
 * game's faces and menu icons) and a sentence of the launcher's help page (launcher_onboard.cpp), the same
 * English, so one translation serves both. */

typedef struct HelpLine {
    int icon;
    const char* text;
} HelpLine;

static const HelpLine kHelpCouch[] = {
    {ICON_212, "Press {PADSTART} on a controller to join: it takes the next free seat."},
    {ICON_1A4, "1P is Mario, 2P the partner (Luigi in Brothers mode), 3P and 4P companions."},
    {ICON_19E, "Players 2-4's {START} opens their own card: character, Who's who, emotes, controls."},
    {ICON_1A3, "Mario's {START} opens the full Start menu and pauses everyone."},
    {ICON_1A6, "Hold {Z} for the emote wheel: the stick points, let go to show it."},
    {ICON_155, "Who's who swaps seats, at the next calm moment."},
    {ICON_1A7, "No one of yours in a battle? You're in the audience: cheer, and throw gifts or rocks."},
    {ICON_1AE, "The camera follows Mario: anyone who wanders off comes back to his side."},
};
static const HelpLine kHelpOnline[] = {
    {ICON_19E, "Host a room: you get a code to read out, or copy the invite link."},
    {ICON_155, "Join by code: Online, then Join by code, and type your friend's code."},
    {ICON_1A3, "The host's rules: everyone plays the host's adventure and settings; the host is Mario."},
    {ICON_1AF, "Your game keeps a copy of the host's save: keep it as a file of your own next time."},
    {ICON_034, "Leave the room any time: each of you goes on alone with what you have."},
};
#define HELP_PAGES 3

static struct {
    int open;
    int page; /* 0, 1: on one screen; 2: online */
} sHelpPage;

static const HelpLine* helpLines(int page, int* n) {
    if (page == 2) {
        *n = (int)(sizeof(kHelpOnline) / sizeof(kHelpOnline[0]));
        return kHelpOnline;
    }
    *n = 4;
    return kHelpCouch + page * 4;
}

static int helpMain(WindowWork* wp) {
    f32 gx, gy;
    const int was = sHelpPage.page;
    const int kind = portMenuPoint(&gx, &gy);
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sHelpPage.open = 0;
        return 0;
    }
    if (kind == 1) {
        /* a tap on the heading: its half; below it, the next page */
        if (gy > STATUS_TOP - 40.0f && gy <= STATUS_TOP) {
            sHelpPage.page = gx < 0.0f ? (sHelpPage.page == 0 ? 1 : 0) : 2;
        } else {
            sHelpPage.page = (sHelpPage.page + 1) % HELP_PAGES;
        }
    } else if (wp->dirRep & DEMO_STICK_LEFT) {
        sHelpPage.page = (sHelpPage.page + HELP_PAGES - 1) % HELP_PAGES;
    } else if ((wp->dirRep & DEMO_STICK_RIGHT) || (wp->buttonTrg & PAD_BUTTON_A)) {
        sHelpPage.page = (sHelpPage.page + 1) % HELP_PAGES;
    }
    if (sHelpPage.page != was) {
        psndSFXOn((const char*)0x20005);
    }
    wp->helpTgtX = PANEL_X - 6.0f;
    wp->helpTgtY = STATUS_TOP - 12.0f;
    return 1;
}

static void helpDisp(WindowWork* wp, f32 x, f32 y) {
    const f32 top = y + STATUS_TOP - 46.0f;
    const f32 textX = x + ROW_X + 30.0f, textW = PANEL_W - 80.0f;
    const f32 gap = sHelpPage.page == 2 ? 4.0f : 12.0f; /* (between two notes) */
    static char sLines[4][PORT_PAUSE_WRAP_LINE];
    char pos[16];
    int n, i, total = 0, k;
    f32 s = 0.58f, ry;
    const HelpLine* lines = helpLines(sHelpPage.page, &n);
    /* every note whole, each on as many lines as it takes (a translation's longer): the page's notes at
       0.58, smaller (to 0.5) only when all their lines would run past the box */
    for (k = 0; k < 2; k++) {
        total = 0;
        for (i = 0; i < n; i++) {
            const int c = portPauseWrapLines(portTrGame(lines[i].text), textW, s, sLines, 4);
            total += c < 2 ? 2 : c; /* (the icon's height: two lines) */
        }
        if ((f32)total * 17.0f * s / 0.58f + (f32)(n - 1) * gap <= 186.0f) {
            break;
        }
        s = 0.5f;
    }
    /* the heading: the two halves, the one shown dark; where in them */
    winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, 40.0f);
    winFontInit();
    fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 8.0f, 220.0f, 0.8f, sHelpPage.page < 2 ? kHeading : kGrey,
            portTrGame("On one screen"), 0);
    fitText(x + PANEL_X + PANEL_W - 60.0f, y + STATUS_TOP - 8.0f, 180.0f, 0.8f, sHelpPage.page == 2 ? kHeading : kGrey,
            portTrGame("Online"), 1);
    snprintf(pos, sizeof(pos), "%d/%d", sHelpPage.page + 1, HELP_PAGES);
    fitText(x + PANEL_X + PANEL_W - 12.0f, y + STATUS_TOP - 12.0f, 40.0f, 0.55f, kGrey, pos, 1);
    winKirinukiGX(wp, 0, x + PANEL_X, top + 4.0f, PANEL_W, 196.0f);
    ry = top - 10.0f;
    for (i = 0; i < n; i++) {
        const int c = portPauseWrapLines(portTrGame(lines[i].text), textW, s, sLines, 4);
        const f32 step = 17.0f * s / 0.58f;
        iconDispGx(&(Vec){x + ROW_X + 4.0f, ry - 12.0f, 0.0f}, ICON_DISP_2D, (u16)lines[i].icon, 0.5f);
        winFontInit();
        for (k = 0; k < c; k++) {
            fitText(textX, ry - (f32)k * step, textW, s, kBlack, sLines[k], 0);
        }
        ry -= (f32)(c < 2 ? 2 : c) * step + gap;
    }
    /* (tests: the notes past the box's bottom, in its height's pixels) */
    portTextFitLines("online-help-page", portTrGame(lines[0].text), (int)(top - 10.0f - ry - gap), 186);
}

/* --- the rows --- */

static void addArg(int kind, const char* label, const char* note, int confirm, int arg) {
    if (sCount < MAX_ROWS) {
        sRows[sCount].kind = kind;
        sRows[sCount].label = label;
        sRows[sCount].note = note;
        sRows[sCount].confirm = confirm;
        sRows[sCount].arg = arg;
        sCount++;
    }
}

static void add(int kind, const char* label, const char* note, int confirm) {
    addArg(kind, label, note, confirm, 0);
}

static int selectable(int kind) {
    return kind != ROW_HEADING;
}

/* the room code typed last (a code, not an address): 1 and it in out, without its dash; 0 none */
static int lastRoom(char* out, size_t size) {
    const char* last = portOnlineMenuLastTyped();
    size_t i, n = 0;
    if (last == NULL || strchr(last, '.') != NULL || strchr(last, ':') != NULL) {
        return 0;
    }
    for (i = 0; last[i] != '\0' && n + 1 < size; i++) {
        if (last[i] != '-') {
            out[n++] = last[i];
        }
    }
    out[n] = '\0';
    return n == PORT_ONLINE_CODE_LENGTH;
}

static int inRoom(void) {
    return portStateSyncOn() && portStateSyncPhase() != PORT_SYNC_OFF;
}

/* a seat's part, English */
static const char* partName(int seat) {
    switch (portTogetherSeatPart(seat)) {
        case PORT_PART_MARIO:
            return "Mario";
        case PORT_PART_LUIGI:
            return "Luigi";
        case PORT_PART_PARTNER:
            return "The partner";
        default:
            return seat == 3 ? "Companion" : "Second companion";
    }
}

/* the same as a label in a line ("Mario: ..., Partner: ..."): never "The partner" halfway through */
static const char* partLabel(int seat) {
    return portTogetherSeatPart(seat) == PORT_PART_PARTNER ? "Partner" : partName(seat);
}

static int sMark; /* Who's who: the seat chosen first (a swap's other seat next), 0 none */

static int serviceFailed(void) {
    return portStateSyncPhase() == PORT_SYNC_FAILED && !strcmp(portStateSyncNote(), PORT_SYNC_NO_SERVICE);
}

static void buildMain(void) {
    char who[64];
    int seat;
    if (serviceFailed()) {
        /* (the online service can't be reached: said in the status above; never a wait without end) */
        add(ROW_RETRY, "Try again", PORT_SYNC_NO_SERVICE, 0);
        add(ROW_GIVE_UP, "Back", "Play on this computer: the room is let go.", 0);
    }
    {
        /* an invite link that came (invite.cpp): its room first, asked, never joined by itself */
        static char sInvite[16];
        if (!inRoom() && portOnlinePeekInvite(sInvite, sizeof(sInvite))) {
            add(ROW_INVITE, "Join the invite's room", "An invite link came for this room: join it.", 0);
        }
    }
    if (portTogetherSeatAsked(who, sizeof(who), &seat)) {
        add(ROW_ASKED_YES, "Swap seats: yes", "Another player asks for your seat: yours becomes theirs.", 0);
        add(ROW_ASKED_NO, "Swap seats: no", "You keep your seat.", 0);
    }
    /* (no row for the adventure: co-op is on by default, a controller's {PADSTART} joins, and a room's guests
       come into the host's adventure by themselves; together_aurora.c) */
    add(ROW_BROTHERS, "Brothers mode", "Player 2 plays Luigi beside Mario.", 0);
    /* (next, in sight on every screen: hosting, joining by code or address, the room list, invites) */
    add(ROW_INVITE_PAGE, "Online...",
        inRoom() ? "The room's code, the invite link, and leaving."
                 : "Host a room or join one: by code, from the room list, or by address.",
        0);
    add(ROW_PARTY, "Party...",
        portTogetherCanPick() ? "Versus, the raid boss, a boss rush, the quiz, card duels, bingo or a dungeon master: "
                                "everyone goes straight in, no save needed."
                              : "The host chooses what you play: you go straight in.",
        0);
    add(ROW_SEATS, "Who's who...", "Who plays Mario, the partner and the companions, and swaps between you.", 0);
    add(ROW_MP_SETTINGS, "Multiplayer settings...",
        "Second player, players, colours, the camera, Brothers mode, duo moves and more.", 0);
    if (inRoom() && portStateSyncPhase() == PORT_SYNC_LINKED) {
        add(ROW_PLAYERS, "Players...",
            ssNetIsHost() ? "Everyone in the room: their seat, their computer and their connection. You can remove a "
                            "player."
                          : "Everyone in the room: their seat, their computer and their connection.",
            0);
        if (!ssNetIsHost()) {
            add(ROW_RULES, "Rules...", "The host's rule settings that differ from yours: the room plays on the host's.",
                0);
        }
    }
    if (inRoom()) {
        add(ROW_LEAVE, "Leave the room", "Each of you goes on alone from here, with everything you have now.",
            portStateSyncPhase() == PORT_SYNC_LINKED);
    }
    add(ROW_HELP, "? How multiplayer works",
        "On one screen and online: joining, the seats, the cards, the audience, rooms and codes.", 0);
    add(ROW_TEAM_BONUS, "Team bonus",
        "When everyone in a turn lands their action command, a little extra Star Power. Two players or more.", 0);
    if (!inRoom() && portNotesOn()) {
        add(ROW_NOTES, "Paper notes...",
            "Leave a note where Mario stands for other players, and rate the ones they left in this room.", 0);
    }
}

static void buildInvite(void) {
    const int relay = portOnlineMenuHasRelay();
    if (inRoom()) {
        if (portStateSyncCode()[0] != '\0') {
            add(ROW_COPY_CODE, "Copy room code", "The room's code to the clipboard.", 0);
            add(ROW_COPY_LINK, "Copy invite link",
                "A papercut:// link to the clipboard: it opens Paper Cut on this room.", 0);
        }
        add(ROW_LEAVE, "Leave the room", "Each of you goes on alone from here, with everything you have now.",
            portStateSyncPhase() == PORT_SYNC_LINKED);
        add(ROW_GHOSTS, "See-through players",
            "The other players are drawn see-through, so they never hide you or the way ahead.", 0);
        return;
    }
    if (serviceFailed()) {
        add(ROW_RETRY, "Try again", PORT_SYNC_NO_SERVICE, 0);
        add(ROW_GIVE_UP, "Back", "Play on this computer: the room is let go.", 0);
    }
    add(ROW_NAME, "Your name",
        "What the others see in the room: at first this computer's user name (or Player). Up to 24 letters.", 0);
    if (relay) {
        add(ROW_HOST, "Host a room", "Opens a room with a code to give your friends: up to four play.", 0);
        add(ROW_JOIN_CODE, "Join by code...", "Type the room code your friend gives you.", 0);
        {
            char last[16];
            if (lastRoom(last, sizeof(last))) {
                add(ROW_JOIN_LAST, "Last room", "The room you joined last: join it again.", 0);
            }
        }
        add(ROW_LIST, "Room list...", "The rooms open to anyone, and how many play online now.", 0);
    }
    add(ROW_WATCH, "Watch...",
#ifdef __EMSCRIPTEN__ /* (codes only: the browser reaches no address) */
        "Type a room code: your game follows the host's and watches their battles. You don't play, and your "
        "file isn't shared.",
#else
        "Type a room code or the host's address: your game follows the host's and watches their battles. You "
        "don't play, and your file isn't shared.",
#endif
        0);
#ifndef __EMSCRIPTEN__ /* (the browser takes no connections: rooms through the relay only) */
    if (relay) {
        add(ROW_HEADING, "By address", NULL, 0);
    }
    add(ROW_HOST_ADDRESS, "Host by address",
        "Waits on your port: on the same network, or with the port forwarded or a VPN.", 0);
    add(ROW_JOIN_ADDRESS, "Join by address...", "Type the host's address (and :port when it isn't 47700).", 0);
#endif
    add(ROW_HEADING, "Options", NULL, 0);
    if (relay) {
        add(ROW_LIST_MINE, "List my room",
            "Rooms you host are in the room list: anyone can join them. Off, only those with the code can.", 0);
    }
#ifndef __EMSCRIPTEN__
    add(ROW_PORT, "Port", "Hosting by address: the port others connect to.", 0);
#endif
    add(ROW_GHOSTS, "See-through players",
        "The other players are drawn see-through, so they never hide you or the way ahead.", 0);
}

static void buildSeats(void) {
    char who[64];
    int seat;
    if (portTogetherSeatAsked(who, sizeof(who), &seat)) {
        add(ROW_ASKED_YES, "Swap seats: yes", "Another player asks for your seat: yours becomes theirs.", 0);
        add(ROW_ASKED_NO, "Swap seats: no", "You keep your seat.", 0);
    }
    for (seat = 1; seat <= PORT_SEATS; seat++) {
        addArg(ROW_SEAT, partName(seat),
               inRoom() && !ssNetIsHost()
                   ? "{A} asks for this seat: its player says yes or no. The swap happens at the next calm moment."
                   : "{A} on one seat, then {A} on another swaps them, at the next calm moment (in the field, "
                     "no scene or battle). An open seat: {PADSTART} on a controller joins.",
               0, seat);
    }
}

static void buildDm(void) {
    int seat;
    for (seat = 2; seat <= PORT_SEATS; seat++) {
        addArg(ROW_DM_SEAT, partName(seat),
               "This seat's player commands the bosses: on each of their turns, which move and on whom.", 0, seat);
    }
}

/* Players: the room's people, in player order (a guest sees the host, itself and those seated) */
static char sPlayerNames[SS_PLAYERS + 1][64];
static int sRemoveStep, sRemoveWho; /* the host: a removal asked for (A twice more), and whose */

static int playerShown(int p) {
    int s;
    if (p == 1) {
        return 1;
    }
    if (p == portStateSyncMe()) {
        return 1;
    }
    if (ssNetIsHost()) {
        return ssNetLinked(p);
    }
    for (s = 2; s <= PORT_SEATS; s++) {
        if (portStateSyncSeatPlayer(s) == p) {
            return 1;
        }
    }
    return 0;
}

static void buildPlayers(void) {
    int p;
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (!playerShown(p)) {
            continue;
        }
        portStateSyncPlayerNameShown(p, sPlayerNames[p], sizeof(sPlayerNames[p]));
        if (sPlayerNames[p][0] == '\0') {
            snprintf(sPlayerNames[p], sizeof(sPlayerNames[p]), "%s", portTrGame(p == 1 ? "The host" : "A player"));
        }
        addArg(ROW_PLAYER, sPlayerNames[p],
               ssNetIsHost() && p >= 2 ? "Their seat, their computer and their connection. {A} removes them from the "
                                         "room (asked twice)."
                                       : "Their seat, their computer and their connection.",
               0, p);
    }
}

/* Rules: the setting's name on the page (its control's label), its value as the page says it */
static const PortSettingsControl* ruleControl(const char* key) {
    static PortSettingsControl sControls[600];
    static int sN = -1;
    const int* v = portSettingsFind(key);
    int i;
    if (sN < 0) {
        sN = portSettingsControls(sControls, (int)(sizeof(sControls) / sizeof(sControls[0])));
    }
    for (i = 0; v != NULL && i < sN; i++) {
        if (sControls[i].value == v) {
            return &sControls[i];
        }
    }
    return NULL;
}

static void ruleValue(const PortSettingsControl* c, int v, char* out, size_t size) {
    if (c != NULL && c->kind == 0) {
        snprintf(out, size, "%s", portTrGame(v ? "On" : "Off"));
    } else if (c != NULL && c->kind == 1 && v >= 0 && v < c->count && c->choices != NULL) {
        snprintf(out, size, "%s", portTrGame(c->choices[v]));
    } else if (c != NULL && c->kind == 2 && c->format != NULL) {
        snprintf(out, size, c->format, v);
    } else {
        snprintf(out, size, "%d", v);
    }
}

static void buildRules(void) {
    const int n = portStateSyncRuleDiffs();
    int i;
    if (n == 0) {
        add(ROW_HEADING, "Same rules as yours.", NULL, 0);
        return;
    }
    add(ROW_HEADING, "Yours, then the host's", NULL, 0);
    for (i = 0; i < n; i++) {
        int own, host;
        const char* key = portStateSyncRuleDiffAt(i, &own, &host);
        const PortSettingsControl* c = ruleControl(key);
        addArg(ROW_RULE, c != NULL ? c->label : key,
               "The room plays on the host's while you're in it; yours come back when you leave.", 0, i);
    }
}

static void buildRush(void) {
    addArg(ROW_RUSH_FROM, "Start at",
           "Where the rush begins: from the start, at any boss, or with Paper Mario 64's bosses only (from Jr. "
           "Troopa). Left and right choose.",
           0, 0);
    addArg(ROW_RUSH_GO, "Start the boss rush", "The story's bosses one after another, against the clock.", 0, 0);
}

/* the rows for now */
static void build(void) {
    int i;
    sCount = 0;
    switch (sSub) {
        case SUB_INVITE:
            buildInvite();
            break;
        case SUB_SEATS:
            buildSeats();
            break;
        case SUB_DM:
            buildDm();
            break;
        case SUB_PLAYERS:
            buildPlayers();
            break;
        case SUB_RULES:
            buildRules();
            break;
        case SUB_RUSH:
            buildRush();
            break;
        default:
            buildMain();
            break;
    }
    /* the cursor on the same row as before: kind and arg (a seat, a player, a rule: rows of one kind told
       apart by their arg), the one it is on if that still matches; else the first row of that kind */
    if (sWasKind >= 0 && !(sRow >= 0 && sRow < sCount && sRows[sRow].kind == sWasKind &&
                           (sWasArg < 0 || sRows[sRow].arg == sWasArg))) {
        int kindAt = -1;
        for (i = 0; i < sCount; i++) {
            if (sRows[i].kind != sWasKind) {
                continue;
            }
            if (kindAt < 0) {
                kindAt = i;
            }
            if (sWasArg < 0 || sRows[i].arg == sWasArg) {
                kindAt = i;
                break;
            }
        }
        if (kindAt >= 0) {
            sRow = kindAt;
        }
    }
    if (sRow >= sCount) {
        sRow = 0;
    }
    while (sRow + 1 < sCount && !selectable(sRows[sRow].kind)) {
        sRow++;
    }
    sWasKind = sRows[sRow].kind;
    sWasArg = sRows[sRow].arg;
    if (sRow < sTop) {
        sTop = sRow;
    }
    if (sRow >= sTop + sVisible) {
        sTop = sRow - sVisible + 1;
    }
    if (sTop > 0 && sTop + sVisible > sCount) {
        sTop = sCount - sVisible < 0 ? 0 : sCount - sVisible;
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
        case ROW_GHOSTS:
            snprintf(out, size, "%s", portTrGame(portStateSyncGhostSeeThrough() ? "On" : "Off"));
            break;
        case ROW_TEAM_BONUS:
            snprintf(out, size, "%s", portTrGame(portTeamBonusOn() ? "On" : "Off"));
            break;
        case ROW_BROTHERS:
            snprintf(out, size, "%s", portTrGame(portBrothersSetting() ? "On" : "Off"));
            break;
        case ROW_PORT:
            snprintf(out, size, "%d", portOnlineMenuPort());
            break;
        case ROW_JOIN_LAST: {
            char code[16];
            if (lastRoom(code, sizeof(code))) {
                snprintf(out, size, "%.4s-%s", code, code + PORT_ONLINE_ROOM_HALF);
            }
            break;
        }
        case ROW_INVITE: {
            char code[16];
            if (portOnlinePeekInvite(code, sizeof(code))) {
                snprintf(out, size, "%.4s-%s", code, code + PORT_ONLINE_ROOM_HALF);
            }
            break;
        }
        case ROW_ASKED_YES: {
            int seat;
            portTogetherSeatAsked(out, size, &seat);
            break;
        }
        case ROW_INVITE_PAGE:
            if (portStateSyncCode()[0] != '\0') {
                snprintf(out, size, "%s", portStateSyncCode());
            }
            break;
        case ROW_PARTY: {
            const int picked = portTogetherPicked();
            if (picked != PORT_PICK_NONE && picked != PORT_PICK_TOGETHER) {
                snprintf(out, size, "%s", portTrGame(portTogetherPickName(picked)));
            }
            break;
        }
        case ROW_PLAYER: {
            /* "2P  Windows  good" */
            static const char* const kOs[] = {"", "Windows", "Mac", "Linux", "iPhone or iPad", "Android", "Browser"};
            static const char* const kLink[] = {"", "good", "fair", "poor"};
            const int os = portStateSyncPlayerPlatform(r->arg);
            const int q = portStateSyncPlayerQuality(r->arg);
            int s, seat = r->arg == 1 ? 1 : 0;
            size_t n;
            for (s = 2; s <= PORT_SEATS && seat == 0; s++) {
                if (portStateSyncSeatPlayer(s) == r->arg) {
                    seat = s;
                }
            }
            if (seat > 0) {
                snprintf(out, size, "%dP", seat);
            } else {
                snprintf(out, size, "%s", portTrGame("Watching"));
            }
            if (os > 0 && os < (int)(sizeof(kOs) / sizeof(kOs[0]))) {
                n = strlen(out);
                snprintf(out + n, size - n, "  %s", portTrGame(kOs[os]));
            }
            if (q > 0 && q <= 3) {
                n = strlen(out);
                snprintf(out + n, size - n, "  %s", portTrGame(kLink[q]));
            }
            if (sRemoveStep > 0 && sRemoveWho == r->arg) {
                n = strlen(out);
                snprintf(out + n, size - n, "  %s", portTrGame("Remove?"));
            }
            break;
        }
        case ROW_RULE: {
            int own, host;
            char a[48], b[48];
            const char* key = portStateSyncRuleDiffAt(r->arg, &own, &host);
            const PortSettingsControl* c = key != NULL ? ruleControl(key) : NULL;
            if (key == NULL) {
                break;
            }
            ruleValue(c, own, a, sizeof(a));
            ruleValue(c, host, b, sizeof(b));
            snprintf(out, size, "%s > %s", a, b);
            break;
        }
        case ROW_RULES: {
            const int n = portStateSyncRuleDiffs();
            if (n == 0) {
                snprintf(out, size, "%s", portTrGame("Same as yours"));
            } else {
                snprintf(out, size, "%d", n);
            }
            break;
        }
        case ROW_RUSH_FROM: {
            const int at = portModeRushStart();
            snprintf(out, size, "%s", portTrGame(at == 0 ? "The start (Lord Crump)" : portModeRushStartName(at)));
            break;
        }
        case ROW_SEAT:
        case ROW_DM_SEAT: {
            char who[64];
            if (portTogetherSeatWho(r->arg, who, sizeof(who))) {
                snprintf(out, size, "%s%s", r->kind == ROW_SEAT && sMark == r->arg ? "> " : "", who);
            } else {
                snprintf(out, size, "%s", portTrGame("Open"));
            }
            break;
        }
    }
}

/* a pick from the page: its answer for the help box (*ok: it was made) */
static const char* choose(int pick, int arg, int* ok) {
    const char* why = NULL;
    *ok = portTogetherChoose(pick, arg, &why);
    if (!*ok) {
        return why;
    }
    return "It begins once the menu is closed.";
}

/* A (dir 0), or left and right: the row's action; its answer for the help box, or NULL */
static const char* act(const Row* r, int dir) {
    switch (r->kind) {
        case ROW_PARTY:
            if (dir == 0) {
                if (!portTogetherCanPick()) {
                    return "The host chooses what you play: you go straight in.";
                }
                memset(&sGrid, 0, sizeof(sGrid));
                sGrid.open = 1;
            }
            return NULL;
        case ROW_HELP:
            if (dir == 0) {
                sHelpPage.open = 1;
                sHelpPage.page = 0;
            }
            return NULL;
        case ROW_PLAYERS:
        case ROW_RULES:
            if (dir == 0) {
                sSub = r->kind == ROW_PLAYERS ? SUB_PLAYERS : SUB_RULES;
                sRemoveStep = 0;
                sRow = sTop = 0;
                sWasKind = -1;
                sWasArg = -1;
            }
            return NULL;
        case ROW_PLAYER:
            /* the host removes a guest: asked twice, then done (they go back to their own adventure) */
            if (dir != 0) {
                return NULL;
            }
            if (!ssNetIsHost()) {
                return "Only the host can remove a player.";
            }
            if (r->arg < 2) {
                return "You can't remove yourself."; /* (player 1: the host) */
            }
            if (sRemoveWho != r->arg) {
                sRemoveWho = r->arg;
                sRemoveStep = 0;
            }
            if (sRemoveStep == 0) {
                sRemoveStep = 1;
                return "Remove this player from the room? {A} again to go on, {B} to keep them.";
            }
            if (sRemoveStep == 1) {
                sRemoveStep = 2;
                return "Sure? They go back to their own adventure and can't come back in this time. {A} removes them.";
            }
            sRemoveStep = 0;
            return portStateSyncRemove(r->arg) ? "Removed: they're told, and their copy of the adventure is kept."
                                               : "They're not in the room any more.";
        case ROW_RETRY:
            if (dir == 0) {
                if (!portStateSyncRetry()) {
                    return portStateSyncNote()[0] != '\0' ? portStateSyncNote() : "Couldn't join.";
                }
                return "Trying again...";
            }
            return NULL;
        case ROW_GIVE_UP:
            if (dir == 0) {
                portStateSyncLeave();
                sSub = SUB_MAIN;
            }
            return NULL;
        case ROW_SEATS:
        case ROW_INVITE_PAGE:
            if (dir == 0) {
                sSub = r->kind == ROW_SEATS ? SUB_SEATS : SUB_INVITE;
                sMark = 0;
                sRow = sTop = 0;
                sWasKind = -1;
                sWasArg = -1;
            }
            return NULL;
        case ROW_SEAT:
            if (dir != 0) {
                return NULL;
            }
            if (inRoom() && !ssNetIsHost()) {
                const char* why = NULL;
                return portTogetherSeatSwap(r->arg, 0, &why) ? "Asked: the swap happens once they say yes." : why;
            }
            if (sMark == 0) {
                sMark = r->arg;
                return "Now {A} on the seat to swap with.";
            }
            if (sMark == r->arg) {
                sMark = 0;
                return NULL;
            }
            {
                const char* why = NULL;
                /* (the host online: the marked seat's player onto this seat, swapping with its holder) */
                const int with = inRoom() ? portStateSyncSeatPlayer(sMark) : sMark;
                const int ok = inRoom() ? portTogetherSeatSwap(r->arg, with, &why)
                                        : portTogetherSeatSwap(sMark, r->arg, &why);
                sMark = 0;
                return ok ? "Swapped at the next calm moment: in the field, no scene or battle." : why;
            }
        case ROW_DM_SEAT:
            if (dir == 0) {
                int ok;
                const char* answer = choose(PORT_PICK_DM, r->arg, &ok);
                if (ok) {
                    sSub = SUB_MAIN;
                    sWasKind = ROW_PARTY;
                    sWasArg = -1;
                }
                return answer;
            }
            return NULL;
        case ROW_RUSH_FROM: {
            const int count = portModeRushStartCount();
            portModeRushStartSet((portModeRushStart() + (dir < 0 ? count - 1 : 1)) % count);
            return NULL;
        }
        case ROW_RUSH_GO:
            if (dir == 0) {
                int ok;
                const char* answer = choose(PORT_PICK_BOSS_RUSH, portModeRushStart(), &ok);
                if (ok) {
                    sSub = SUB_MAIN;
                    sWasKind = ROW_PARTY;
                    sWasArg = -1;
                }
                return answer;
            }
            return NULL;
        case ROW_ASKED_YES:
        case ROW_ASKED_NO:
            if (dir == 0) {
                portTogetherSeatAnswer(r->kind == ROW_ASKED_YES);
            }
            return NULL;
        case ROW_MP_SETTINGS:
            if (dir == 0) {
                sSettingsAsk = 1;
            }
            return NULL;
        case ROW_BROTHERS: {
            int* v = portSettingsFind("brothers_mode");
            const char* why;
            if (v == NULL) {
                return NULL;
            }
            if (!portTogetherCanPick()) {
                /* (a guest plays in the host's world: the host's setting is the room's) */
                return "Online, the host's world decides: only the host can turn Brothers mode on or off.";
            }
            if (portConsoleAccurateLockedValue(PORT_SETTING_CHANGES_PLAY, v)) {
                return "Console-accurate mode is on: rules that change play stay as the original's.";
            }
            if ((why = portSettingWhyLocked(v)) != NULL) {
                return why;
            }
            *v = !*v;
            portSettingsApply();
            portSettingsSave();
            printf("[online] Brothers mode %s from the Multiplayer page\n", *v ? "on" : "off");
            fflush(stdout);
            if (!*v) {
                return "Brothers mode is off: Luigi leaves, and player 2 plays the partner again.";
            }
            if (portTitleScope() == PORT_TITLE_NO_FILE) {
                return "Brothers mode is on: Luigi is at Mario's side once the adventure starts.";
            }
            return inRoom() ? "Brothers mode is on: Luigi joins Mario, in everyone's game in the room."
                            : "Brothers mode is on: Luigi joins Mario. Player 2 plays him; alone, he follows Mario.";
        }
        case ROW_TEAM_BONUS: {
            int* v = portSettingsFind("team_bonus");
            if (v != NULL && portConsoleAccurateLockedValue(PORT_SETTING_CHANGES_PLAY, v)) {
                return "Console-accurate mode is on: rules that change play stay as the original's.";
            }
            if (v != NULL) {
                *v = !*v;
                portSettingsSave();
            }
            return NULL;
        }
        case ROW_HOST:
        case ROW_HOST_ADDRESS:
            if (dir == 0) {
                /* "List my room": a room (not by address) under this player's name */
                portStateSyncListAs(r->kind == ROW_HOST && portOnlineMenuListMine() ? portOnlineMenuName() : NULL);
                if (!portStateSyncHost(r->kind == ROW_HOST_ADDRESS, portOnlineMenuPort())) {
                    return portStateSyncNote()[0] != '\0' ? portStateSyncNote() : "Couldn't start hosting.";
                }
                if (portTitleOnPages()) {
                    /* over the title, no file: the room is open now, the guests wait in it. Party... plays with
                       no save; the title's Play opens the host's files, and once the host is in one the room
                       plays that adventure (together_aurora.c's hostFrame) */
                    printf("[online] a room hosted from the title: Party..., or Play for the host's files\n");
                    fflush(stdout);
                    return "Room open. Party... starts a game for everyone, no save needed. Or close this page "
                           "and choose Play: once you're in a file, everyone joins your adventure.";
                }
            }
            return NULL;
        case ROW_JOIN_CODE:
            if (dir == 0) {
                /* (empty: the last room is a row of its own, Last room) */
                kbOpen(KEYS_CODE, KEYS_JOIN, NULL);
                printf("[online] keyboard: join by code, %s\n", sKb.text[0] == '\0' ? "empty" : "filled");
                fflush(stdout);
            }
            return NULL;
        case ROW_JOIN_LAST:
            if (dir == 0) {
                char code[16];
                if (!lastRoom(code, sizeof(code))) {
                    return "No room was joined from here yet.";
                }
                printf("[online] joining the last room\n");
                fflush(stdout);
                if (!portStateSyncJoin(code)) {
                    return portStateSyncNote()[0] != '\0' ? portStateSyncNote() : "Couldn't join.";
                }
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
                /* a code or an address as was typed last (By address / By code on the keyboard switches) */
                const char* last = portOnlineMenuLastTyped();
#ifdef __EMSCRIPTEN__
                const int address = 0; /* (the browser: codes only) */
#else
                const int address = !portOnlineMenuHasRelay() || strchr(last, '.') != NULL || strchr(last, ':') != NULL;
#endif
                kbOpen(address ? KEYS_ADDRESS : KEYS_CODE, KEYS_WATCH, address ? last : NULL); /* (a code: empty) */
            }
            return NULL;
        case ROW_LIST:
            if (dir == 0) {
                memset(&sList, 0, sizeof(sList));
                sList.open = 1;
                portStateSyncListRefresh(); /* (fetched as it opens, and on Refresh: never polled) */
            }
            return NULL;
        case ROW_NOTES:
            if (dir == 0) {
                portPauseNotesOpen();
            }
            return NULL;
        case ROW_COPY_CODE:
            if (dir != 0) {
                return NULL;
            }
            return portStateSyncCode()[0] != '\0' && SDL_SetClipboardText(portStateSyncCode())
                       ? "Copied: send it to your friends (Online, Join by code)."
                       : "There's no room to copy: this game isn't in one.";
        case ROW_COPY_LINK:
            if (dir != 0) {
                return NULL;
            }
            {
                char link[64];
                return portStateSyncInviteLink(link, sizeof(link)) && SDL_SetClipboardText(link)
                           ? "Copied: send the link, and a click on it opens Paper Cut on this room."
                           : "There's no room to copy: this game isn't in one.";
            }
        case ROW_INVITE:
            if (dir == 0) {
                char code[16];
                if (!portOnlineTakeInvite(code, sizeof(code))) {
                    return "The invite has expired.";
                }
                if (!portStateSyncJoin(code)) {
                    return portStateSyncNote()[0] != '\0' ? portStateSyncNote() : "Couldn't join.";
                }
            }
            return NULL;
        case ROW_LEAVE:
            if (dir == 0) {
                portStateSyncLeave();
                sSub = SUB_MAIN;
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
        case ROW_GHOSTS:
            portStateSyncGhostSetSeeThrough(!portStateSyncGhostSeeThrough());
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
    }
    return NULL;
}

/* --- the help box --- */

static void help(WindowWork* wp, const char* english) {
    static char tags[4][32];
    static int n;
    static char shown[1024];
    static f32 shownX, shownY; /* (the hand's place: the same text at another row, as an answer kept while
                                  the hand moves, is entered again so the hand goes there) */
    char text[1024];
    snprintf(text, sizeof(text), "%s", english);
    if (strcmp(text, shown) == 0 && shownX == wp->helpTgtX && shownY == wp->helpTgtY) {
        winMsgEntry(wp, 0, tags[n % 4], 0);
        return;
    }
    snprintf(shown, sizeof(shown), "%s", text);
    shownX = wp->helpTgtX;
    shownY = wp->helpTgtY;
    n++;
    snprintf(tags[n % 4], sizeof(tags[0]), "pc_menu_online_%d", n);
    portPauseWrap(portTrGame(english), sHelp, sizeof(sHelp));
    winMsgEntry(wp, 0, tags[n % 4], 0);
    portSpeechMenu(portTrGame(english));
}

static void pointAt(WindowWork* wp) {
    const Row* r = &sRows[sRow];
    if (sKb.open) {
        /* the shortcuts, as the player's own buttons (the menu layer's pictures): a console's keyboard */
        help(wp, sAnswer != NULL ? sAnswer
                 : sKb.purpose == KEYS_SET_PORT
                     ? "{A} type   {B} rub out (none: back)   {START} OK   {L}{R} move. Or type it."
                 : sKb.layout == KEYS_NAME
                     ? "{A} type   {B} rub out (none: back)   {Y} space   {START} OK   {L}{R} move. Or type it."
                     : "{A} type   {B} rub out (none: back)   {X} paste   {START} OK   {L}{R} move. Or type it.");
        return;
    }
    if (sList.open) {
        help(wp, sList.row == 0 ? "Asks the relay again for the open rooms." : "{A} joins this room.");
        return;
    }
    if (sGrid.open) {
        help(wp, sAnswer != NULL ? sAnswer : portTogetherPickNote(gridPick(sGrid.at)));
        return;
    }
    if (sHelpPage.open) {
        help(wp, "Left and right turn the pages; {B} goes back.");
        return;
    }
    if (portPauseNotesIsOpen()) {
        help(wp, portPauseNotesHelp());
        return;
    }
    wp->helpTgtX = ROW_X - 24.0f;
    wp->helpTgtY = ROW_Y0 - sGrow - (sRow - sTop) * ROW_STEP - 8.0f + 8.0f;
    if (sAnswer != NULL) {
        help(wp, sAnswer);
    } else if (sConfirm) {
        help(wp, "Press {A} again to confirm.");
    } else {
        help(wp, r->note != NULL ? r->note : r->label);
    }
}

/* --- the tab --- */

void portPauseOnlineInit(WindowWork* wp) {
    (void)wp;
    sSub = SUB_MAIN;
    sMark = 0;
    sSettings = sSettingsAsk = 0;
    memset(&sGrid, 0, sizeof(sGrid));
    memset(&sHelpPage, 0, sizeof(sHelpPage));
    {
        /* tests, screenshots: $TTYD_ONLINE_HELP=N, the help page open on its page N (1-3) */
        const char* help = getenv("TTYD_ONLINE_HELP");
        if (help != NULL && atoi(help) >= 1 && atoi(help) <= HELP_PAGES) {
            sHelpPage.open = 1;
            sHelpPage.page = atoi(help) - 1;
        }
    }
    sRow = sTop = 0;
    sWasKind = -1;
    sWasArg = -1;
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
    /* tests, screenshots: $TTYD_NOTES_PAGE=1, the Paper notes page open as the tab is chosen (with its row there) */
    if (getenv("TTYD_NOTES_PAGE") != NULL && !inRoom() && portNotesOn()) {
        portPauseNotesOpen();
    }
}

static void move(int dir) {
    int guard = MAX_ROWS;
    do {
        sRow = (sRow + dir + sCount) % sCount;
    } while (!selectable(sRows[sRow].kind) && --guard > 0);
    if (sRow < sTop) {
        sTop = sRow;
    }
    if (sRow >= sTop + sVisible) {
        sTop = sRow - sVisible + 1;
    }
    sWasKind = sRows[sRow].kind;
    sWasArg = sRows[sRow].arg;
}

/* the mouse and touch on the rows: the one under the pointer selected, a click on it A */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0 || gx < ROW_X - 16.0f || gx > VALUE_X + 8.0f) {
        return 0;
    }
    for (i = sTop; i < sCount && i < sTop + sVisible; i++) {
        const f32 y = ROW_Y0 - sGrow - (i - sTop) * ROW_STEP - 8.0f;
        if (selectable(sRows[i].kind) && gy <= y + ROW_STEP / 2 && gy > y - ROW_STEP / 2) {
            if (i == sRow) {
                return kind == 1;
            }
            sRow = i;
            sWasKind = sRows[i].kind;
            sWasArg = sRows[i].arg;
            psndSFXOn((const char*)0x20005);
            return 0;
        }
    }
    return 0;
}

/* The page's way out, in sight on every input: a button under the panel at its right, Close on the front
   list (the whole menu: over the title, back to its buttons; in a file, back to the game) and Back on every
   page and list inside it (the front list again). A tap or a click on it; {B} on a controller or the keyboard
   does the same (B on the front list goes to the tabs, as the game's own pages do). */
#define BACK_W 128.0f
#define BACK_H 24.0f
#define BACK_RIGHT (PANEL_X + PANEL_W)
#define BACK_TOP (ROWS_TOP - ROWS_H - 6.0f)

static int frontList(void) {
    return !sKb.open && !sList.open && !sGrid.open && !sHelpPage.open && !portPauseNotesIsOpen() && sSub == SUB_MAIN &&
           !sSettings;
}

static int backTapped(void) {
    f32 gx, gy;
    /* (the keyboard has a Back key of its own, ACT_BACK; Paper notes' rows reach down where the button would
       be: there the touch controls' B and the mouse's right click go back) */
    if (sKb.open || portPauseNotesIsOpen() || portMenuPoint(&gx, &gy) != 1) {
        return 0;
    }
    return gx >= BACK_RIGHT - BACK_W && gx <= BACK_RIGHT && gy <= BACK_TOP && gy > BACK_TOP - BACK_H;
}

static void backDisp(WindowWork* wp, f32 x, f32 y) {
    if (sKb.open || portPauseNotesIsOpen()) {
        return;
    }
    winKirinukiGX(wp, 0, x + BACK_RIGHT - BACK_W, y + BACK_TOP, BACK_W, BACK_H);
    winFontInit();
    centredText(x + BACK_RIGHT - BACK_W / 2.0f, y + BACK_TOP - 4.0f, BACK_W - 12.0f, 0.7f, kBlack,
                portTrGame(frontList() ? "{B} Close" : "{B} Back"));
}

/* the page in sight, logged as it changes ("[online] page: ..."): the tests' way to see that every B, Escape
   or tap on Back left the page it was on (tests/port/testpass/mp_pages_*) */
static void logView(void) {
    static const char* const kSubs[] = {"front", "invite", "who's who", "dungeon master", "boss rush", "players",
                                        "rules"};
    static int sWas = -1;
    const int view = sSub * 64 + sKb.open + sList.open * 2 + sGrid.open * 4 + sHelpPage.open * 8 +
                     portPauseNotesIsOpen() * 16 + sSettings * 32;
    if (view != sWas) {
        sWas = view;
        printf("[online] page: %s\n", sSettings ? "multiplayer settings" : sKb.open ? "keyboard" : sList.open ? "room list" : sGrid.open ? "pictures"
                                      : sHelpPage.open ? "help" : portPauseNotesIsOpen() ? "paper notes"
                                      : sSub >= 0 && sSub < (int)(sizeof(kSubs) / sizeof(kSubs[0])) ? kSubs[sSub]
                                                                                                    : "?");
        fflush(stdout);
    }
}

s32 portPauseOnlineMain(WindowWork* wp) {
    const s32 wasRow = sRow;
    const int wasPage = sKb.open * 2 + sList.open;
    logView();
    if (backTapped()) {
        /* the page's Back (or Close) tapped or clicked: B on a page inside; the whole menu from the front */
        if (frontList()) {
            psndSFXOn((const char*)0x20013);
            printf("[online] the Multiplayer page closed by its Close button\n");
            fflush(stdout);
            sAnswer = NULL;
            sConfirm = 0;
            return -2;
        }
        printf("[online] the Multiplayer page's Back button\n");
        fflush(stdout);
        wp->buttonTrg |= PAD_BUTTON_B;
    }
    if (sSettings) {
        /* Multiplayer settings...: the Settings tab's page runs here; its B (Escape, Back) is this list again */
        const s32 ret = portPauseSettingsMain(wp);
        if (ret == -1) {
            portPauseSettingsCloseHosted();
            sSettings = 0;
            sAnswer = NULL;
            sWasKind = ROW_MP_SETTINGS;
            sWasArg = -1;
            logView();
            build();
            pointAt(wp);
            return 0;
        }
        return ret;
    }
    if (portMenuCrawl() != NULL && strcmp(portMenuCrawl(), "online") == 0) {
        /* tests: its rows walked down by themselves, nothing chosen, then the end */
        portTextFitScreen("Multiplayer");
        if (!portMenuCrawlPad(wp, "online", sCount + 2)) {
            portMenuCrawlDone();
        }
    }
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
    gPortTogetherJoinOpen = 1; /* (a controller's A takes the next seat while the page is up) */
    if (sList.open) {
        listMain(wp);
        build();
        pointAt(wp);
        return 0;
    }
    if (sGrid.open) {
        const int answerN = sAnswerN;
        gridMain(wp);
        if (sAnswerN == answerN && (wp->buttonTrg != 0 || wp->dirRep != 0)) {
            sAnswer = NULL;
        }
        build();
        pointAt(wp);
        return 0;
    }
    if (sHelpPage.open) {
        helpMain(wp);
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
        if (sSub != SUB_MAIN) { /* (back to the front list; the commander's back to the pictures) */
            sWasKind = sSub == SUB_SEATS     ? ROW_SEATS
                       : sSub == SUB_INVITE  ? ROW_INVITE_PAGE
                       : sSub == SUB_PLAYERS ? ROW_PLAYERS
                       : sSub == SUB_RULES   ? ROW_RULES
                                             : ROW_PARTY;
            sWasArg = -1;
            sRemoveStep = 0;
            sGrid.open = sSub == SUB_DM || sSub == SUB_RUSH;
            sSub = SUB_MAIN;
            sMark = 0;
            build();
            pointAt(wp);
            return 0;
        }
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
            if (sSettingsAsk) {
                /* Multiplayer settings...: the Settings tab's Multiplayer page here, B back to this list */
                sSettingsAsk = 0;
                if (portPauseSettingsOpenOn(wp, "Multiplayer")) {
                    sSettings = 1;
                    printf("[online] Multiplayer settings opened from the Multiplayer page\n");
                    fflush(stdout);
                    logView();
                    return 0;
                }
            }
        }
    } else if (wp->dirRep & DEMO_STICK_UP) {
        move(-1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        move(1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int k = sRows[sRow].kind;
        if (k == ROW_LIST_MINE || k == ROW_PORT || k == ROW_GHOSTS || k == ROW_TEAM_BONUS || k == ROW_BROTHERS) {
            /* (a refusal or an answer said, as for A: console-accurate mode's lock was silent on left and right) */
            const char* answer = act(&sRows[sRow], (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1);
            if (answer != NULL) {
                sAnswer = answer;
                sAnswerN++;
            }
            psndSFXOn((const char*)0x20005);
        }
    }
    if (sRow != wasRow) {
        sAnswer = NULL;
        sConfirm = 0;
        sRemoveStep = 0;
    }
    (void)wasPage;
    build();
    pointAt(wp);
    return 0;
}

void portPauseOnlineExit(WindowWork* wp) {
    (void)wp;
    if (sSettings) {
        portPauseSettingsCloseHosted();
        sSettings = 0;
    }
    sKb.open = 0;
    sList.open = 0;
    sGrid.open = 0;
    sHelpPage.open = 0;
    gPortTogetherJoinOpen = 0;
    portPauseNotesClose();
}


/* each their own game's status: hosting (the code or the address to give), joining, playing */
static void syncStatusDisp(WindowWork* wp, f32 x, f32 y) {
    const int phase = portStateSyncPhase();
    const char* code = portStateSyncCode();
    const char* note = portStateSyncNote();
    const int hosting = portStateSyncOn() && (code[0] != '\0' ? phase == PORT_SYNC_WAITING : portStateSyncPort() > 0);
    char title[200], detail[400], address[96];
    f32 codeW = 0.0f;
    address[0] = '\0';
    detail[0] = '\0';
    switch (phase) {
        case PORT_SYNC_WAITING:
            snprintf(title, sizeof(title), "%s",
                     portTrGame(hosting ? "Room open: waiting for friends." : "Joining..."));
            if (hosting) {
                snprintf(detail, sizeof(detail), "%s",
                         portTrGame(code[0] != '\0' ? "Tell your friends this code: Online, then Join by code."
                                                    : "Give your friends this address: Online, then Join by "
                                                      "address."));
            } else if (note[0] != '\0') {
                snprintf(detail, sizeof(detail), "%s", portTrGame(note));
            }
            break;
        case PORT_SYNC_LINKED: {
            /* who's here: the others' names (the game's font), by player number */
            char names[160], one[SS_NAME];
            int p;
            names[0] = '\0';
            for (p = 1; p <= SS_PLAYERS; p++) {
                if (p != ssNetMe() && ssPeerOf(p)->valid) {
                    portStateSyncPlayerNameShown(p, one, sizeof(one));
                    if (one[0] != '\0' && strlen(names) + strlen(one) + 3 < sizeof(names)) {
                        snprintf(names + strlen(names), sizeof(names) - strlen(names), "%s%s",
                                 names[0] != '\0' ? ", " : "", one);
                    }
                }
            }
            if (names[0] != '\0') {
                snprintf(title, sizeof(title), portTrGame("Playing with %s."), names);
            } else {
                snprintf(title, sizeof(title), "%s", portTrGame("Playing together online."));
            }
            if (portStateSyncWatching()) {
                snprintf(detail, sizeof(detail), "%s",
                         portTrGame("Watching another player's scene: you can move again once it's over."));
            } else if (!ssNetIsHost()) {
                const int picked = portTogetherPicked();
                if (picked == PORT_PICK_NONE || picked == PORT_PICK_TOGETHER) {
                    snprintf(detail, sizeof(detail), "%s", portTrGame("You're in the host's adventure."));
                } else {
                    snprintf(detail, sizeof(detail), portTrGame("The host chose %s: you go straight in."),
                             portTrGame(portTogetherPickName(picked)));
                }
            } else {
                snprintf(detail, sizeof(detail), "%s",
                         portTrGame("Everyone plays your adventure. Party... starts a game for all of you."));
            }
            break;
        }
        case PORT_SYNC_FAILED:
            snprintf(title, sizeof(title), "%s", portTrGame("Not connected"));
            snprintf(detail, sizeof(detail), "%s", portTrGame(note));
            break;
        default: {
            const int players = portCoopPlayers();
            const int* coop = portSettingsFind("second_player");
            snprintf(title, sizeof(title), "%s", portTrGame("Multiplayer"));
            if (coop != NULL && *coop && portTitleScope() != PORT_TITLE_NO_FILE && players < PORT_SEATS) {
                /* co-op on ("second_player", on by default), in a file: the open seats, and how to take one */
                char open[32];
                int seat;
                open[0] = '\0';
                for (seat = 2; seat <= PORT_SEATS; seat++) {
                    char who[48];
                    if (!portTogetherSeatWho(seat, who, sizeof(who))) {
                        snprintf(open + strlen(open), sizeof(open) - strlen(open), "%s%dP", open[0] ? ", " : "", seat);
                    }
                }
                snprintf(detail, sizeof(detail), portTrGame("Open seats: %s. A controller joins with {PADSTART}."),
                         open);
            } else if (players > 1) {
                snprintf(detail, sizeof(detail),
                         portTrGame("%d players on this screen. Another controller joins with {PADSTART}."), players);
            } else {
                snprintf(detail, sizeof(detail), "%s",
                         portTrGame("A controller joins with {PADSTART}. Online brings in friends from other computers."));
            }
            break;
        }
    }
    if (phase == PORT_SYNC_LINKED && portTitleScope() == PORT_TITLE_NO_FILE && sSub == SUB_MAIN) {
        /* the lobby (the title, before the host's Start): everyone's seat */
        int seat;
        detail[0] = '\0';
        for (seat = 1; seat <= PORT_SEATS; seat++) {
            char who[48];
            if (portTogetherSeatWho(seat, who, sizeof(who)) &&
                strlen(detail) + strlen(who) + 40 < sizeof(detail)) {
                snprintf(detail + strlen(detail), sizeof(detail) - strlen(detail), "%s%s: %s",
                         detail[0] != '\0' ? ", " : "", portTrGame(partLabel(seat)), who);
            }
        }
    }
    if (sSub == SUB_SEATS || sSub == SUB_DM || sSub == SUB_RUSH) {
        snprintf(title, sizeof(title), "%s",
                 portTrGame(sSub == SUB_SEATS ? "Who's who" : sSub == SUB_RUSH ? "Boss rush" : "Who commands the enemies?"));
        if (portTogetherSeatWaiting()) {
            snprintf(detail, sizeof(detail), "%s",
                     portTrGame("A swap waits for the next calm moment: in the field, no scene or battle."));
        }
    }
    {
        /* refused for another protocol: both builds under the note, for information */
        const char *theirs, *mine;
        if (phase == PORT_SYNC_FAILED && portStateSyncRefusedBuilds(&theirs, &mine)) {
            char builds[160];
            snprintf(builds, sizeof(builds), portTrGame("Your friend's: Paper Cut %s. Yours: Paper Cut %s."), theirs,
                     mine);
            snprintf(detail + strlen(detail), sizeof(detail) - strlen(detail), "%s%s", detail[0] != '\0' ? "\n" : "",
                     builds);
        }
    }
    {
        /* the room code (or the address) on the right; the title and the note on the left, every word of
           them on as many lines as they take: the box grows down for them, over the rows' room (at least
           MIN_ROWS rows stay), and past that the note turns its lines' pages */
        enum { MIN_ROWS = 3, DETAIL_MAX = 12 };
        static char sTitle[2][PORT_PAUSE_WRAP_LINE], sDetail[DETAIL_MAX][PORT_PAUSE_WRAP_LINE];
        static int sPage, sPageWait;
        static char sPageOf[400];
        const char* label = NULL;
        const char* big = NULL;
        char shown[120];
        f32 bigS = 1.45f, bigW = 200.0f, bigY = 28.0f, room, extra, most;
        int titles, details, perPage, pages, from, shownN, i;
        if (code[0] != '\0' && phase != PORT_SYNC_FAILED) {
            label = portTrGame("Room code");
            big = code;
        } else if (hosting && phase != PORT_SYNC_FAILED && portOnlineLocalAddress(address, sizeof(address))) {
            /* (not once it failed, as the code: the note why takes the box's width) */
            char withPort[120];
            snprintf(withPort, sizeof(withPort), "%s:%d", address, portStateSyncPort());
            gameText(withPort, shown, sizeof(shown));
            label = portTrGame("Your address");
            big = shown;
            bigS = 0.95f, bigW = 220.0f, bigY = 34.0f;
        }
        if (big != NULL) {
            const f32 full = (f32)FontGetMessageWidth(big);
            codeW = full * bigS > bigW ? bigW : full * bigS;
        }
        room = PANEL_W - 44.0f - (codeW > 0.0f ? codeW + 20.0f : 0.0f);
        titles = portPauseWrapLines(title, room, 0.9f, sTitle, 2);
        details = detail[0] != '\0' ? portPauseWrapLines(detail, room, 0.7f, sDetail, DETAIL_MAX) : 0;
        /* (one title line and two of the note fit the box as it is: 24 a title line past it, 20 a note's) */
        most = (f32)(VISIBLE - MIN_ROWS) * ROW_STEP;
        perPage = 2 + (int)((most - (f32)(titles - 1) * 24.0f) / 20.0f);
        pages = details > perPage ? (details + perPage - 1) / perPage : 1;
        if (pages > 1) {
            /* (the note's pages turn every 3 seconds; a new note starts at its first) */
            if (strcmp(sPageOf, detail) != 0) {
                snprintf(sPageOf, sizeof(sPageOf), "%s", detail);
                sPage = sPageWait = 0;
            } else if (++sPageWait >= FPS_FRAMES(180)) {
                sPage = (sPage + 1) % pages;
                sPageWait = 0;
            }
        } else {
            sPage = 0;
        }
        from = sPage * perPage;
        shownN = pages > 1 ? perPage : details;
        extra = (f32)(titles - 1) * 24.0f + (f32)(shownN > 2 ? shownN - 2 : 0) * 20.0f;
        sGrow = extra > most ? most : extra;
        sVisible = VISIBLE - (int)((sGrow + ROW_STEP - 1.0f) / ROW_STEP);
        if (sRow >= sTop + sVisible) {
            sTop = sRow - sVisible + 1; /* (the chosen row stays in view) */
        }
        winKirinukiGX(wp, 0, x + PANEL_X, y + STATUS_TOP, PANEL_W, STATUS_H + sGrow);
        winFontInit();
        if (big != NULL) {
            fitText(x + PANEL_X + PANEL_W - 20.0f, y + STATUS_TOP - 10.0f, 160.0f, 0.65f, kGrey, label, 1);
            fitText(x + PANEL_X + PANEL_W - 18.0f, y + STATUS_TOP - bigY, bigW, bigS, kHeading, big, 1);
        }
        for (i = 0; i < titles; i++) {
            fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 10.0f - (f32)i * 24.0f, room, 0.9f, kHeading, sTitle[i], 0);
        }
        for (i = from; i < details && i < from + perPage; i++) {
            fitText(x + PANEL_X + 20.0f, y + STATUS_TOP - 38.0f - (f32)(titles - 1) * 24.0f - (f32)(i - from) * 20.0f,
                    room, 0.7f, kBlack, sDetail[i], 0);
        }
        if (pages > 1) {
            char pos[16];
            snprintf(pos, sizeof(pos), "%d/%d", sPage + 1, pages);
            fitText(x + PANEL_X + PANEL_W - 12.0f, y + STATUS_TOP - STATUS_H - sGrow + 14.0f, 40.0f, 0.5f, kGrey, pos, 1);
        }
        portTextFitLines("online-status", detail, details, pages > 1 ? perPage : details);
    }
}

void portPauseOnlineDisp(s32 camId, WindowWork* wp, s32 idx) {
    const f32 x = wp->menu[idx].x;
    const f32 y = wp->menu[idx].y;
    const int inside = wp->active == PORT_PAUSE_ONLINE_TAB + 11;
    int i;
    (void)camId;
    if (sSettings) { /* Multiplayer settings...: the Settings tab's page in this one's colour, and the way back */
        portPauseSettingsDisp(camId, wp, idx);
        backDisp(wp, x, y);
        return;
    }
    winBgGX(wp, PORT_PAUSE_ONLINE_TAB, x, y);
    backDisp(wp, x, y); /* (the way out, on every page of it) */
    if (sKb.open) {
        kbDisp(wp, x, y);
        return;
    }
    if (sList.open) {
        listDisp(wp, x, y);
        return;
    }
    if (sGrid.open) {
        gridDisp(wp, x, y);
        return;
    }
    if (sHelpPage.open) {
        helpDisp(wp, x, y);
        return;
    }
    if (portPauseNotesIsOpen()) {
        portPauseNotesDisp(wp, x, y);
        return;
    }
    syncStatusDisp(wp, x, y);
    winKirinukiGX(wp, 0, x + PANEL_X, y + ROWS_TOP - sGrow, PANEL_W, ROWS_H - sGrow);
    for (i = sTop; i < sCount && i < sTop + sVisible; i++) {
        const Row* r = &sRows[i];
        const f32 ry = y + ROW_Y0 - sGrow - (i - sTop) * ROW_STEP;
        /* every row black as the game's own lists (the hand shows the chosen one); grey only for what this
           player can't use: a guest's Brothers mode and Party (the host chooses) */
        const int off = (r->kind == ROW_BROTHERS || r->kind == ROW_PARTY) &&
                        !portTogetherCanPick();
        const GXColor c = off && (!inside || i != sRow) ? kGrey : kBlack;
        char value[64];
        winFontInit();
        if (r->kind == ROW_HEADING) {
            fitText(x + ROW_X - 8.0f, ry, 240.0f, 0.75f, kHeading, portTrGame(r->label), 0);
            continue;
        }
        fitText(x + ROW_X, ry, 250.0f, 0.8f, c, portTrGame(r->label), 0);
        valueText(r, value, sizeof(value));
        if (value[0] != '\0') {
            fitText(x + VALUE_X, ry, 200.0f, 0.8f, c, value, 1);
        }
    }
    /* more rows above or below */
    if (sTop > 0 || sTop + sVisible < sCount) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)sRow + 1, sCount);
        winFontInit();
        /* (in the panel's top margin, right: clear of the rows' values) */
        fitText(x + PANEL_X + PANEL_W - 12.0f, y + ROWS_TOP - sGrow - 1.0f, 80.0f, 0.5f, kGrey, pos, 1);
    }
}

const char* portPauseOnlineMsg(const char* tag) {
    if (strcmp(tag, "pc_menu_online") == 0) {
        static char sTab[256];
        portPauseWrap(portTrGame("Play together, on this screen or online: the adventure, party games, who's who."),
                      sTab, sizeof(sTab));
        return sTab;
    }
    if (strncmp(tag, "pc_menu_online_", 15) == 0) {
        return sHelp; /* the selected row's (pointAt) */
    }
    return NULL;
}
