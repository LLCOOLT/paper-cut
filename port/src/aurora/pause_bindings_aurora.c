/* The Start menu's bindings page (port/include/port/pause_bindings.h): the
 * Controls category's "Bindings" row opens it, in the game's own windows and
 * font, for player 1. Rows: the game's buttons and stick directions, then
 * Paper Cut's actions (port/include/port/bindings.h), under their headings,
 * Reset all, then the profiles; four cells a row, two shown at a time: the
 * keyboard's key and the second key, or the controller's button and the
 * second button.
 *
 *   up, down       a row        left, right   the next cell (the other two shown past the second)
 *   A              asks for a key or button (5 seconds; B leaves a key's question)
 *   X              the row back to its defaults
 *   B              back to the settings list
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
#include "port/pause_bindings.h"
#include "port/bindings.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/tr.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

enum { ITEM_HEADING, ITEM_ROW, ITEM_RESET_ALL, ITEM_PROFILE, ITEM_SAVE_AS, ITEM_RENAME, ITEM_DELETE, ITEM_DEFAULT };
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
    for (i = 0; i < portBindingsRowCount() && sCount < MAX_ITEMS - 8; i++) {
        if (i == portBindingsGameRows()) {
            add(ITEM_HEADING, 0, "Paper Cut's actions");
        }
        add(ITEM_ROW, i, portBindingsRowName(i));
    }
    add(ITEM_RESET_ALL, 0, "Reset all");
    add(ITEM_HEADING, 0, "Profiles");
    add(ITEM_PROFILE, 0, "Profile");
    add(ITEM_SAVE_AS, 0, "Save as new...");
    add(ITEM_RENAME, 0, "Rename...");
    add(ITEM_DELETE, 0, "Delete");
    add(ITEM_DEFAULT, 0, "Default for this controller");
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
        snprintf(text, sizeof(text), "%s", portTrGame("A: every key and button back as they first were."));
    } else if (it->kind == ITEM_PROFILE) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame(portBindingsProfileCount() == 0
                                ? "No profiles yet: Save as new... keeps these bindings under a name."
                                : "Left, right: choose a profile. A: load it. Changes are then saved into it."));
    } else if (it->kind == ITEM_SAVE_AS) {
        snprintf(text, sizeof(text), "%s", portTrGame("A: these bindings kept under a name of your choosing."));
    } else if (it->kind == ITEM_RENAME) {
        snprintf(text, sizeof(text), "%s", portTrGame("A: a new name for the chosen profile."));
    } else if (it->kind == ITEM_DELETE) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame(sDelete ? "Press A again to delete the chosen profile." : "A twice: the chosen profile deleted."));
    } else if (it->kind == ITEM_DEFAULT) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame("On: the chosen profile is loaded whenever a controller of this kind connects."));
    } else if (sAskCol >= 0) {
        snprintf(text, sizeof(text), "%s",
                 portTrGame(sAskCol < 2 ? "Press a key. B: leave it as it is." : "Press a button."));
    } else {
        const char* other = portBindingsConflict(it->row, sCol);
        if (other != NULL) {
            char also[160];
            snprintf(also, sizeof(also), portTrGame("Also %s."), portTrGame(other));
            snprintf(text, sizeof(text), "%s %s", also, portTrGame("A: change. X: reset the row."));
        } else {
            snprintf(text, sizeof(text), "%s",
                     portTrGame("A: change. X: reset the row. Left, right: the other keys and buttons."));
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
    sCol = 0;
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
            portBindingsProfileDelete(sProfile);
            sDelete = 0;
            sProfile = portBindingsProfileActive(0);
            sSaid = "Deleted.";
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

s32 portPauseBindingsMain(WindowWork* wp) {
    const Item* it;
    if (portBindingsBusy()) {
        if (sAskCol >= 0 && sAskCol < 2 && (wp->buttonTrg & PAD_BUTTON_B)) {
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
    if ((wp->buttonTrg | wp->dirRep) != 0) {
        sSaid = NULL; /* (an answer stays until the next press) */
        if (!(wp->buttonTrg & PAD_BUTTON_A)) {
            sDelete = 0;
        }
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
        } else {
            profileA(it);
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
        const int dir = (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1;
        const int n = portBindingsProfileCount();
        if (it->kind == ITEM_PROFILE && n > 0) {
            sProfile = sProfile < 0 ? (dir > 0 ? 0 : n - 1) : (sProfile + dir + n) % n;
        } else if (it->kind == ITEM_DEFAULT) {
            profileA(it);
        } else if (it->kind == ITEM_ROW) {
            sCol = sCol + dir < 0 ? 0 : sCol + dir >= CELLS ? CELLS - 1 : sCol + dir;
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
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
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
    default: return "";
    }
}

void portPauseBindingsDisp(WindowWork* wp, f32 x, f32 y, int inside) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor pale = {0x98, 0x98, 0x98, 0xFF};
    const GXColor heading = {0x5A, 0x46, 0x9C, 0xFF};
    const int pair = sCol / 2; /* the two cells shown */
    int i;
    (void)wp;
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
}

const char* portPauseBindingsHelp(const char* tag) {
    return strncmp(tag, "pc_menu_bindings_", 17) == 0 ? sHelp : NULL;
}
