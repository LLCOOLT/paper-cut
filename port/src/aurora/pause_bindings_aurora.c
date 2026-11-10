/* The Start menu's bindings page (port/include/port/pause_bindings.h): the
 * Controls category's "Bindings" row opens it, in the game's own windows and
 * font, for player 1. Rows: the game's buttons and stick directions, then
 * Paper Cut's actions (port/include/port/bindings.h), under their headings,
 * Reset all, then the profiles; four cells a row, two shown at a time: the
 * keyboard's key and the second key, or the controller's button and the
 * second button.
 *
 *   up, down       a row        left, right   the first or the second cell
 *   L, R           the keyboard's two or the controller's two (the controller's first when the
 *                  last press was a controller's)
 *   A              asks for a key or button (5 seconds; Escape, the controller's B or a tap
 *                  leaves a key's question, Escape held for a second makes Escape the key;
 *                  Start held for a second leaves any question)
 *   Y              the cell's binding taken off (none)
 *   X              the row back to its defaults
 *   B              back to the settings list (and the last row, Back, for touch)
 *
 * (A, B, X and Y as Paper Cut's menus have them: the controller's own confirm,
 * back and third buttons, port/include/port/menu_buttons.h.) Swap confirm and
 * back (on or off), Menu confirm and Menu back (left and right: the face
 * button, or as the controller has it) are the rows after Reset all, as the
 * settings page's Bindings section has them. The mouse and touch: a row under
 * the pointer is selected (a cell of a binding's row by its side), a click on
 * the selected one is A. While a key is asked for, the keyboard's keys are
 * the answer, the game's B (E) too: the help names Escape alone, and the
 * controller's B only with a controller (or the touch controls) in hand.
 *
 * The profiles' rows: Profile (left and right choose one, A loads it), Save
 * as new... and Rename... (a name typed on the text page, opened over this
 * one), Delete (A twice) and Default for this controller (on or off).
 *
 * A binding that is also another row's shows that row in the help line (the
 * page's Bindings section asks Swap / Keep both; here it is kept, and the
 * help says which other row has it). While a key or button is being asked
 * for, and until everything is let go after, the menu takes no input. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/text_fit.h"
#include "port/pause_bindings.h"
#include "port/menu_buttons.h"
#include "port/bindings.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/button_markup.h"
#include "port/tr.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include "port/menu_tap.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ITEM_HEADING, ITEM_ROW, ITEM_RESET_ALL, ITEM_SWAP, ITEM_PROFILE, ITEM_SAVE_AS, ITEM_RENAME, ITEM_DELETE, ITEM_DEFAULT,
       ITEM_MENU_CONFIRM, ITEM_MENU_BACK, ITEM_BACK };
#define MAX_ITEMS 64
#define VISIBLE 10
#define ROW_X (-250.0f)
#define ROW_TOP 132.0f
#define ROW_STEP 26.0f
#define CELLS 4

typedef struct Item {
    int kind;
    int row; /* a bindings row (ITEM_ROW) */
    const char* label;
} Item;

static Item sItems[MAX_ITEMS];
static int sCount;
static s32 sAt, sTop;
static int sCol;         /* 0 the key, 1 the second key, 2 the controller, 3 the second button */
static int sAskCol = -1; /* the column asked about (a key's: B leaves it) */
static int sProfile = -1; /* the profile chosen on the Profile row */
static int sDelete;      /* Delete's first A was pressed */
static const char* sSaid; /* a profile row's answer (English), until the cursor moves */
static char sHelp[512];
static f32 sDispX, sDispY; /* where the rows were drawn last (the mouse and touch) */

static void add(int kind, int row, const char* label) {
    if (sCount < MAX_ITEMS) {
        sItems[sCount].kind = kind;
        sItems[sCount].row = row;
        sItems[sCount++].label = label;
    }
}

static void build(void) {
    int i;
    sCount = 0;
    add(ITEM_HEADING, 0, "The game's buttons");
    for (i = 0; i < portBindingsRowCount() && sCount < MAX_ITEMS - 13; i++) {
        if (i == portBindingsGameRows()) {
            add(ITEM_HEADING, 0, "Paper Cut's actions");
        }
        add(ITEM_ROW, i, portBindingsRowName(i));
    }
    add(ITEM_RESET_ALL, 0, "Reset all");
    add(ITEM_SWAP, 0, "Swap confirm and back");
    add(ITEM_MENU_CONFIRM, 0, "Menu confirm");
    add(ITEM_MENU_BACK, 0, "Menu back");
    add(ITEM_HEADING, 0, "Profiles");
    add(ITEM_PROFILE, 0, "Profile");
    add(ITEM_SAVE_AS, 0, "Save as new...");
    add(ITEM_RENAME, 0, "Rename...");
    add(ITEM_DELETE, 0, "Delete");
    add(ITEM_DEFAULT, 0, "Default for this controller");
    add(ITEM_BACK, 0, "Back");
}

/* Menu confirm or Menu back's face button, one step left (-1) or right: as the controller has it, then
 * the bottom, right and left buttons (menu_buttons.h: the top one switches the settings page's view) */
static void stepPlace(int role, int dir) {
    const int at = portMenuChosenPlace(role) + 1;
    portMenuSetPlace(role, (at + dir + 4) % 4 - 1);
}

/* a key's question also names the controller's B (it leaves it): a controller in hand, or the touch controls */
static int padToo(void) {
#ifdef PORT_TOUCH_OVERLAY
    if (portTouchPlayerOne()) {
        return 1;
    }
#endif
    return portBindingsKeyQuestionPadToo();
}

/* tests ($TTYD_REBIND_TEST): what the page did */
static void said(const char* what) {
    if (getenv("TTYD_REBIND_TEST") != NULL) {
        printf("[rebind] menu: %s\n", what);
        fflush(stdout);
    }
}

/* the game's text marks a tag with '<': shown as a look-alike */
static void plain(char* s) {
    for (; *s != '\0'; s++) {
        *s = *s == '<' ? '(' : *s == '>' ? ')' : *s;
    }
}

static int profileOk(void) {
    return sProfile >= 0 && sProfile < portBindingsProfileCount();
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
    if (sSaid != NULL) {
        snprintf(text, sizeof(text), "%s", portTrGame(sSaid));
    } else if (it->kind == ITEM_RESET_ALL) {
        snprintf(text, sizeof(text), "%s", portTrGame("{A}: every key and button back as they first were."));
    } else if (it->kind == ITEM_SWAP) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame("On: confirm and back change places in Paper Cut's menus."));
    } else if (it->kind == ITEM_MENU_CONFIRM) {
        snprintf(text, sizeof(text), "%s", portTrGame("Left, right: the button that confirms in Paper Cut's menus."));
    } else if (it->kind == ITEM_MENU_BACK) {
        snprintf(text, sizeof(text), "%s", portTrGame("Left, right: the button that goes back in Paper Cut's menus."));
    } else if (it->kind == ITEM_BACK) {
        snprintf(text, sizeof(text), "%s", portTrGame("{A}: back to the settings list."));
    } else if (it->kind == ITEM_PROFILE) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame(portBindingsProfileCount() == 0
                                ? "No profiles yet: Save as new... keeps these bindings under a name."
                                : "Left, right: choose a profile. {A}: load it. Changes are then saved into it."));
    } else if (it->kind == ITEM_SAVE_AS) {
        snprintf(text, sizeof(text), "%s", portTrGame("{A}: these bindings kept under a name of your choosing."));
    } else if (it->kind == ITEM_RENAME) {
        snprintf(text, sizeof(text), "%s", portTrGame("{A}: a new name for the chosen profile."));
    } else if (it->kind == ITEM_DELETE) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame(sDelete ? "Press {A} again to delete the chosen profile." : "{A} twice: the chosen profile deleted."));
    } else if (it->kind == ITEM_DEFAULT) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame("On: the chosen profile is loaded whenever a controller of this kind connects."));
    } else if (sAskCol >= 0) {
        /* (a key's: Escape, the key that leaves it; the game's B is a key here, E, so {B} only with a
         * controller or the touch controls in hand) */
        snprintf(text, sizeof(text), "%s",
                 portTrGame(sAskCol >= 2 ? "Press a button. Hold {MENU} for a second to leave it as it is."
                            : padToo()   ? "Press a key. Esc or {B}: leave it as it is. Esc held for a second: Esc is the new key."
                                         : "Press a key. Esc: leave it as it is. Esc held for a second: Esc is the new key."));
    } else {
        const char* other = portBindingsConflict(it->row, sCol);
        if (other != NULL) {
            char also[160];
            snprintf(also, sizeof(also), portTrGame("Also %s."), portTrGame(other));
            snprintf(text, sizeof(text), "%s %s", also, portTrGame("{A}: change. {Y}: none. {X}: reset the row."));
        } else {
            snprintf(text, sizeof(text), "%s",
                     portTrGame(sCol >= 2 ? "{A}: change. {Y}: none. {X}: reset. {L}{R}: keyboard."
                                          : "{A}: change. {Y}: none. {X}: reset. {L}{R}: controller."));
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

/* the text page's answers (portPauseTextOpen) */
static void savedAs(const char* text) {
    const int i = portBindingsProfileSaveAs(text, 0);
    if (i >= 0) {
        sProfile = i;
    }
    sSaid = i >= 0 ? "Saved: changes now go into this profile." : "That name can't be used.";
}

static void renamed(const char* text) {
    const int i = profileOk() ? portBindingsProfileRename(sProfile, text) : -1;
    if (i >= 0) {
        sProfile = i;
    }
    sSaid = i >= 0 ? "Renamed." : "That name can't be used.";
}

void portPauseBindingsOpen(WindowWork* wp) {
    build();
    portBindingsProfilesRefresh();
    sAt = 1;
    sTop = 0;
    sCol = portMenuPadInUse() ? 2 : 0; /* (a controller in hand: its buttons first) */
    sAskCol = -1;
    sProfile = portBindingsProfileActive(0);
    sDelete = 0;
    sSaid = NULL;
    help(wp);
}

/* A on a profile row */
static void profileA(const Item* it) {
    switch (it->kind) {
    case ITEM_PROFILE:
        if (profileOk()) {
            sSaid = portBindingsProfileLoad(sProfile, 0) ? "Loaded." : "That profile couldn't be read.";
        }
        break;
    case ITEM_SAVE_AS:
        portPauseTextOpen("Profile name", "", 40, PORT_PAUSE_CHARS_NAME, savedAs);
        break;
    case ITEM_RENAME:
        if (profileOk()) {
            portPauseTextOpen("Rename", portBindingsProfileName(sProfile), 40, PORT_PAUSE_CHARS_NAME, renamed);
        } else {
            sSaid = "Choose a profile on the Profile row first.";
        }
        break;
    case ITEM_DELETE:
        if (!profileOk()) {
            sSaid = "Choose a profile on the Profile row first.";
        } else if (sDelete) {
            const int deleted = portBindingsProfileDelete(sProfile);
            sDelete = 0;
            sProfile = portBindingsProfileActive(0);
            sSaid = deleted ? "Deleted." : "That profile couldn't be deleted.";
        } else {
            sDelete = 1;
        }
        break;
    case ITEM_DEFAULT:
        if (!profileOk()) {
            sSaid = "Choose a profile on the Profile row first.";
        } else if (portBindingsProfileIsDefault(sProfile, 0) < 0) {
            sSaid = "No controller is player 1's.";
        } else {
            portBindingsProfileSetDefault(sProfile, 0, !portBindingsProfileIsDefault(sProfile, 0));
        }
        break;
    default: break;
    }
}

/* the mouse and touch: the row under the pointer selected (a binding's row: the cell on that side); a click
 * on the selected one (its cell) is A */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0 || gx < sDispX + ROW_X - 16.0f || gx > sDispX + 270.0f) {
        return 0;
    }
    for (i = sTop; i < sCount && i < sTop + VISIBLE; i++) {
        const f32 mid = sDispY + ROW_TOP - (i - sTop) * ROW_STEP - 8.0f; /* (the row's middle: the text hangs from its y) */
        int col = sCol;
        if (sItems[i].kind == ITEM_HEADING || gy > mid + ROW_STEP / 2.0f || gy <= mid - ROW_STEP / 2.0f) {
            continue;
        }
        if (sItems[i].kind == ITEM_ROW && gx >= sDispX + 30.0f) {
            col = (sCol & ~1) | (gx >= sDispX + 150.0f ? 1 : 0); /* (the first cell from 40, the second to 255) */
        }
        if (i == sAt && col == sCol) {
            return kind == 1;
        }
        if (kind == 1 || i != sAt) {
            sAt = i;
            sCol = col;
            sSaid = NULL;
            sDelete = 0;
            psndSFXOn((const char*)0x20005);
        }
        return 0;
    }
    return 0;
}

s32 portPauseBindingsMain(WindowWork* wp) {
    const Item* it;
    int click;
    if (portBindingsBusy()) {
        f32 gx, gy;
        /* a key's question: the controller's B (or the touch controls' B) leaves it, as does a tap or click (the
         * keyboard's keys are the answer, the game's B key too: Escape leaves it, bindings.cpp) */
        if (sAskCol >= 0 && sAskCol < 2 && ((wp->buttonTrg & PAD_BUTTON_B) || portMenuPoint(&gx, &gy) == 1)) {
            portBindingsCancel(); /* (a controller's question can't be left with a button: it waits 5 seconds) */
        }
        help(wp);
        return 0;
    }
    sAskCol = -1;
    click = pointer();
    it = &sItems[sAt];
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        said("back (B)");
        return -1;
    }
    if (click) {
        wp->buttonTrg |= PAD_BUTTON_A;
    }
    if ((wp->buttonTrg | wp->dirRep) != 0) {
        sSaid = NULL; /* (an answer stays until the next press) */
        if (!(wp->buttonTrg & PAD_BUTTON_A)) {
            sDelete = 0;
        }
    }
    if ((wp->buttonTrg & PAD_BUTTON_A) && it->kind == ITEM_BACK) {
        psndSFXOn((const char*)0x20013);
        said("back (the Back row)");
        return -1;
    }
    if ((wp->buttonTrg & PAD_BUTTON_A) && it->kind == ITEM_ROW && portBindingsAskNeedsPad(it->row, sCol)) {
        /* a controller's cell, none connected: nothing could answer the question (bindings.cpp's portBindingsAsk) */
        psndSFXOn((const char*)0x20013);
        sSaid = "Connect a controller first.";
    } else if (wp->buttonTrg & PAD_BUTTON_A) {
        psndSFXOn((const char*)0x20012);
        if (it->kind == ITEM_RESET_ALL) {
            portBindingsResetAll();
        } else if (it->kind == ITEM_SWAP) {
            portMenuSetSwapped(!portMenuSwapped());
        } else if (it->kind == ITEM_MENU_CONFIRM || it->kind == ITEM_MENU_BACK) {
            stepPlace(it->kind == ITEM_MENU_CONFIRM ? PORT_MENU_CONFIRM : PORT_MENU_BACK, 1);
        } else if (it->kind == ITEM_ROW) {
            portBindingsAsk(it->row, sCol);
            if (portBindingsBusy()) {
                sAskCol = sCol;
            }
        } else {
            profileA(it);
        }
    } else if ((wp->buttonTrg & PAD_BUTTON_X) && it->kind == ITEM_ROW) {
        psndSFXOn((const char*)0x20005);
        portBindingsResetRow(it->row);
    } else if (wp->buttonTrg & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {
        sCol = sCol < 2 ? sCol + 2 : sCol - 2; /* the keyboard's or the controller's */
        psndSFXOn((const char*)0x20005);
    } else if ((wp->buttonTrg & PAD_BUTTON_Y) && it->kind == ITEM_ROW) {
        psndSFXOn((const char*)0x20005);
        portBindingsClear(it->row, sCol);
    } else if (wp->dirRep & DEMO_STICK_UP) {
        move(-1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        move(1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int dir = (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1;
        const int n = portBindingsProfileCount();
        if (it->kind == ITEM_PROFILE && n > 0) {
            sProfile = sProfile < 0 ? (dir > 0 ? 0 : n - 1) : (sProfile + dir + n) % n;
        } else if (it->kind == ITEM_DEFAULT) {
            profileA(it);
        } else if (it->kind == ITEM_SWAP) {
            portMenuSetSwapped(!portMenuSwapped());
        } else if (it->kind == ITEM_MENU_CONFIRM || it->kind == ITEM_MENU_BACK) {
            stepPlace(it->kind == ITEM_MENU_CONFIRM ? PORT_MENU_CONFIRM : PORT_MENU_BACK, dir);
        } else if (it->kind == ITEM_ROW) {
            sCol = (sCol & ~1) | (dir > 0 ? 1 : 0); /* within the pair shown (L and R: the other pair) */
        }
        psndSFXOn((const char*)0x20005);
    }
    help(wp);
    return 0;
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    portTextFit(right ? "bindings-value" : "bindings-label", text, full * s, w, 1);
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, portTextFitHeight(k, s), portTextFitHeight(k, s)}, c, "%s", text);
}

/* a profile row's value on the right */
static const char* profileValue(const Item* it) {
    static char sName[64];
    switch (it->kind) {
    case ITEM_PROFILE:
        if (!profileOk()) {
            return portTrGame(portBindingsProfileCount() == 0 ? "(none yet)" : "(none)");
        }
        snprintf(sName, sizeof(sName), "%s%s", portBindingsProfileName(sProfile),
                 sProfile == portBindingsProfileActive(0) ? " *" : "");
        plain(sName);
        return sName;
    case ITEM_DEFAULT: {
        const int d = profileOk() ? portBindingsProfileIsDefault(sProfile, 0) : 0;
        return d < 0 ? portTrGame("No controller") : portTrGame(d ? "On" : "Off");
    }
    case ITEM_DELETE: return sDelete ? portTrGame("Sure?") : "";
    case ITEM_SWAP: return portTrGame(portMenuSwapped() ? "On" : "Off");
    case ITEM_MENU_CONFIRM:
    case ITEM_MENU_BACK: {
        /* the button by its place (drawn as the controller in hand's, button_markup.h) */
        static const char* const kPlaces[3] = {"{SOUTH}", "{EAST}", "{WEST}"};
        const int place = portMenuChosenPlace(it->kind == ITEM_MENU_CONFIRM ? PORT_MENU_CONFIRM : PORT_MENU_BACK);
        return place >= 0 && place < 3 ? kPlaces[place] : portTrGame("As the controller has it");
    }
    default: return "";
    }
}

void portPauseBindingsDisp(WindowWork* wp, f32 x, f32 y, int inside) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor pale = {0x98, 0x98, 0x98, 0xFF};
    const GXColor heading = {0x5A, 0x46, 0x9C, 0xFF};
    const int pair = sCol / 2; /* the two cells shown */
    /* the rows' letters are 0.8 of the help box's: their icons too (a capital letter high) */
    const float iconWas = portButtonsGameIconScale(0.32f);
    int i;
    (void)wp;
    sDispX = x;
    sDispY = y;
    winFontInit();
    /* the columns' names over them, and which half of the row this is */
    /* (each within its half: "Controller" and "Second button" met in the middle) */
    fitText(x + 40.0f, y + ROW_TOP + 26.0f, 100.0f, 0.65f, heading, portTrGame(pair == 0 ? "Keyboard" : "Controller"), 0);
    fitText(x + 270.0f, y + ROW_TOP + 26.0f, 125.0f, 0.65f, heading,
            portTrGame(pair == 0 ? "Second key" : "Second button"), 1);
    fitText(x + 30.0f, y + ROW_TOP + 26.0f, 40.0f, 0.6f, grey, pair == 0 ? "1/2" : "2/2", 1);
    for (i = sTop; i < sCount && i < sTop + VISIBLE; i++) {
        const Item* it = &sItems[i];
        const f32 ry = y + ROW_TOP - (i - sTop) * ROW_STEP;
        const int here = inside && i == sAt;
        const GXColor c = !inside || here ? black : grey;
        char first[64], second[64];
        if (it->kind == ITEM_HEADING) {
            fitText(x - 262.0f, ry, 300.0f, 0.75f, heading, portTrGame(it->label), 0);
            continue;
        }
        fitText(x + ROW_X, ry, it->kind == ITEM_ROW ? 280.0f : 300.0f, 0.85f, c, portTrGame(it->label), 0);
        if (it->kind != ITEM_ROW) {
            const char* v = profileValue(it);
            if (v[0] != '\0') {
                fitText(x + 255.0f, ry, 200.0f, 0.8f, here ? black : grey, v, 1);
            }
            continue;
        }
        snprintf(first, sizeof(first), "%s", portBindingsCellText(it->row, pair * 2));
        snprintf(second, sizeof(second), "%s", portBindingsCellText(it->row, pair * 2 + 1));
        plain(first);
        plain(second);
        /* the selected cell dark, the other one of the row paler */
        fitText(x + 40.0f, ry, 110.0f, 0.8f, here && sCol % 2 == 0 ? black : here ? grey : pale, first, 0);
        fitText(x + 255.0f, ry, 140.0f, 0.8f, here && sCol % 2 == 1 ? black : here ? grey : pale, second, 1);
    }
    if (sCount > VISIBLE) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)sAt + 1, sCount);
        fitText(x + 262.0f, y - 140.0f, 80.0f, 0.6f, grey, pos, 1);
    }
    portButtonsGameIconScale(iconWas);
}

const char* portPauseBindingsHelp(const char* tag) {
    return strncmp(tag, "pc_menu_bindings_", 17) == 0 ? sHelp : NULL;
}
