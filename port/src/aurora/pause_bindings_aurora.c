/* The Start menu's bindings page (port/include/port/pause_bindings.h): the
 * Controls category's "Bindings" row opens it, in the game's own windows and
 * font, for player 1. Rows: the game's buttons and stick directions, then
 * Paper Cut's actions (port/include/port/bindings.h), under their headings,
 * and Reset all at the end; two columns: the keyboard's key and the
 * controller's button.
 *
 *   up, down       a row        left, right   the key or the controller column
 *   A              asks for a key or button (5 seconds; B leaves a key's question)
 *   X              the row back to its defaults
 *   B              back to the settings list
 *
 * A binding that is also another row's shows that row in the help line (the
 * page's Bindings section asks Swap / Keep both; here it is kept, and the
 * help says which other row has it). While a key or button is being asked
 * for, and until everything is let go after, the menu takes no input. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/pause_bindings.h"
#include "port/bindings.h"
#include "port/pause_settings.h"
#include "port/tr.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

enum { ITEM_HEADING, ITEM_ROW, ITEM_RESET_ALL };
#define MAX_ITEMS 64
#define VISIBLE 10
#define ROW_X (-250.0f)
#define ROW_TOP 132.0f
#define ROW_STEP 26.0f

typedef struct Item {
    int kind;
    int row; /* a bindings row (ITEM_ROW) */
    const char* label;
} Item;

static Item sItems[MAX_ITEMS];
static int sCount;
static s32 sAt, sTop;
static int sCol; /* 0 the key, 1 the controller */
static int sAskCol = -1; /* the column asked about (a key's: B leaves it) */
static char sHelp[512];

static void build(void) {
    int i;
    sCount = 0;
    sItems[sCount].kind = ITEM_HEADING;
    sItems[sCount++].label = "The game's buttons";
    for (i = 0; i < portBindingsRowCount() && sCount < MAX_ITEMS - 2; i++) {
        if (i == portBindingsGameRows()) {
            sItems[sCount].kind = ITEM_HEADING;
            sItems[sCount++].label = "Paper Cut's actions";
        }
        sItems[sCount].kind = ITEM_ROW;
        sItems[sCount].row = i;
        sItems[sCount++].label = portBindingsRowName(i);
    }
    sItems[sCount].kind = ITEM_RESET_ALL;
    sItems[sCount++].label = "Reset all";
}

/* the game's text marks a tag with '<': shown as a look-alike */
static void plain(char* s) {
    for (; *s != '\0'; s++) {
        *s = *s == '<' ? '(' : *s == '>' ? ')' : *s;
    }
}

/* the help box: what A, X and B do here, or a conflict, or the question */
static void help(WindowWork* wp) {
    static char sTags[2][32];
    static int sTag;
    static char sLast[512];
    const Item* it = &sItems[sAt];
    char text[512];
    wp->helpTgtX = ROW_X - 30.0f;
    wp->helpTgtY = ROW_TOP - (sAt - sTop) * ROW_STEP - 8.0f;
    if (it->kind == ITEM_RESET_ALL) {
        snprintf(text, sizeof(text), "%s", portTrGame("A: every key and button back as they first were."));
    } else if (sAskCol >= 0) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame(sAskCol == 0 ? "Press a key. B: leave it as it is." : "Press a button."));
    } else {
        const char* other = portBindingsConflict(it->row, sCol);
        if (other != NULL) {
            char also[160];
            snprintf(also, sizeof(also), portTrGame("Also %s."), portTrGame(other));
            snprintf(text, sizeof(text), "%s %s", also, portTrGame("A: change. X: reset the row."));
        } else {
            snprintf(text, sizeof(text), "%s", portTrGame("A: change. X: reset the row. B: back."));
        }
    }
    if (strcmp(text, sLast) != 0) { /* winMsgEntry keeps a tag's text: a new tag for new text */
        snprintf(sLast, sizeof(sLast), "%s", text);
        sTag ^= 1;
        snprintf(sTags[sTag], sizeof(sTags[sTag]), "pc_menu_bindings_%d", sTag);
        portPauseWrap(text, sHelp, sizeof(sHelp));
    }
    winMsgEntry(wp, 0, sTags[sTag], 0);
}

static void move(int dir) {
    do {
        sAt = (sAt + dir + sCount) % sCount;
    } while (sItems[sAt].kind == ITEM_HEADING);
    if (sAt - 1 < sTop) {
        sTop = sAt - 1 < 0 ? 0 : sAt - 1;
    }
    if (sAt >= sTop + VISIBLE) {
        sTop = sAt - VISIBLE + 1;
    }
}

void portPauseBindingsOpen(WindowWork* wp) {
    build();
    sAt = 1;
    sTop = 0;
    sCol = 0;
    sAskCol = -1;
    help(wp);
}

s32 portPauseBindingsMain(WindowWork* wp) {
    const Item* it;
    if (portBindingsBusy()) {
        if (sAskCol == 0 && (wp->buttonTrg & PAD_BUTTON_B)) {
            portBindingsCancel(); /* (a controller's question can't be left with a button: it waits 5 seconds) */
        }
        help(wp);
        return 0;
    }
    sAskCol = -1;
    it = &sItems[sAt];
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (wp->buttonTrg & PAD_BUTTON_A) {
        psndSFXOn((const char*)0x20012);
        if (it->kind == ITEM_RESET_ALL) {
            portBindingsResetAll();
        } else if (it->kind == ITEM_ROW) {
            portBindingsAsk(it->row, sCol);
            if (portBindingsBusy()) {
                sAskCol = sCol;
            }
        }
    } else if ((wp->buttonTrg & PAD_BUTTON_X) && it->kind == ITEM_ROW) {
        psndSFXOn((const char*)0x20005);
        portBindingsResetRow(it->row);
    } else if (wp->dirRep & DEMO_STICK_UP) {
        move(-1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        move(1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        sCol = (wp->dirRep & DEMO_STICK_LEFT) ? 0 : 1;
        psndSFXOn((const char*)0x20005);
    }
    help(wp);
    return 0;
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
}

void portPauseBindingsDisp(WindowWork* wp, f32 x, f32 y, int inside) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor pale = {0x98, 0x98, 0x98, 0xFF};
    const GXColor heading = {0x5A, 0x46, 0x9C, 0xFF};
    int i;
    (void)wp;
    winFontInit();
    /* the columns' names over them */
    fitText(x + 40.0f, y + ROW_TOP + 26.0f, 120.0f, 0.65f, heading, portTrGame("Keyboard"), 0);
    fitText(x + 255.0f, y + ROW_TOP + 26.0f, 140.0f, 0.65f, heading, portTrGame("Controller"), 1);
    for (i = sTop; i < sCount && i < sTop + VISIBLE; i++) {
        const Item* it = &sItems[i];
        const f32 ry = y + ROW_TOP - (i - sTop) * ROW_STEP;
        const int here = inside && i == sAt;
        const GXColor c = !inside || here ? black : grey;
        char key[64], pad[64];
        if (it->kind == ITEM_HEADING) {
            fitText(x - 262.0f, ry, 300.0f, 0.75f, heading, portTrGame(it->label), 0);
            continue;
        }
        fitText(x + ROW_X, ry, it->kind == ITEM_ROW ? 280.0f : 300.0f, 0.85f, c, portTrGame(it->label), 0);
        if (it->kind != ITEM_ROW) {
            continue;
        }
        snprintf(key, sizeof(key), "%s", portBindingsCellText(it->row, 0));
        snprintf(pad, sizeof(pad), "%s", portBindingsCellText(it->row, 1));
        plain(key);
        plain(pad);
        /* the selected cell dark, the other one of the row paler */
        fitText(x + 40.0f, ry, 110.0f, 0.8f, here && sCol == 0 ? black : here ? grey : pale, key, 0);
        fitText(x + 255.0f, ry, 140.0f, 0.8f, here && sCol == 1 ? black : here ? grey : pale, pad, 1);
    }
    if (sCount > VISIBLE) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)sAt + 1, sCount);
        fitText(x + 262.0f, y - 140.0f, 80.0f, 0.6f, grey, pos, 1);
    }
}

const char* portPauseBindingsHelp(const char* tag) {
    return strncmp(tag, "pc_menu_bindings_", 17) == 0 ? sHelp : NULL;
}
