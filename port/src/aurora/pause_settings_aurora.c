/* The pause menu's Settings tab (port/include/port/pause_settings.h): the
 * settings in the game's own menu, so nobody has to know about F1 or
 * Back/Select. A tab after the Journal and the Wardrobe, in the game's windows
 * and font. It opens as the Journal does, on its entries' buttons (two
 * columns, each category's picture from the game's icons): the settings
 * page's tabs that hold plain settings (an on/off, a choice, a number;
 * portSettingsControls), not its modes and tools. A opens one's own page (a
 * book, as the Journal's Tattle log), B goes back; each page keeps its cursor
 * while the menu is open. There each setting is changed with left and right
 * (A: the next value) where it is, the selected one's explanation in the help
 * box. After the settings come the features' Start menu forms
 * (port/include/port/pause_forms.h: the window, presets, the saves' tools,
 * the controllers) and, for a section of the full page with no form, a row
 * that opens the full page on its tab. The buttons end with Photo mode and
 * "Full settings page", which opens the full page over the menu (closing it
 * comes back here). A setting that applies from the next start, once changed,
 * brings a "Restart now" row (port/include/port/restart.h), which restarts
 * after a second A. The help box shows the whole note, two lines at a time
 * as the game's own help: the C-stick turns its page, and a longer one turns
 * by itself now and then (portPauseHelpTurn).
 *
 * src/window/win_root.c adds the tab (TARGET_PC blocks): its place in the
 * row, its label, its page (menu id 5, active 16) and its help text, which
 * msgSearch takes from portPauseSettingsMsg. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/pause_settings.h"
#include "port/pause_bindings.h"
#include "port/menu_speed.h"
#include "port/menu_tap.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/speech.h"
#include "port/tr.h"
#include "port/view.h"
#include "driver/camdrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void portSettingsToggle(void); /* port/settings/settings_ui.cpp: the full page */
void portSettingsOpenAt(const char* tab); /* the full page on one of its tabs */

/* KIND_BINDINGS: the Controls page's Bindings row; KIND_FORM: a feature's Start menu form's row
 * (port/include/port/pause_forms.h), KIND_SUBHEAD its heading; KIND_SECTION: a section of the full
 * page with no form here, which opens the full page */
enum { KIND_HEADING = -1, KIND_RATE = 3, KIND_ALL = 4, KIND_RESTART = 5, KIND_PHOTO = 6, KIND_CATEGORY = 7,
       KIND_BINDINGS = 8, KIND_FORM = 9, KIND_SUBHEAD = 10, KIND_SECTION = 11 };
/* the Bindings page open (port/include/port/pause_bindings.h): it has the input and the panel */
static int sBindings;
#define MAX_ROWS 256
/* The front page: the Journal's entry buttons (win_log.c: 224 x 32, the
 * cursor's lifted 8 up and left over its shadow, the others grey) in two
 * columns, filled down the left first; up and down go through them in order,
 * left and right across. Spaced as the Journal's (45) while they fit, closer
 * when they don't. */
#define BANNER_W 200.0f
#define BANNER_COL_X 130.0f /* each column's middle, either side */
#define BANNER_MID 12.0f    /* the middle of the column: the page's */
#define BANNER_SPAN 228.0f  /* the most from the first's middle to the last's */
/* A category's page: a book as the Journal's Tattle log (winBookGX), wider:
 * the category's picture and name heading the left page, then a row a
 * setting, its name on the left page and its value on the right. */
#define BOOK_X (-264.0f)
#define BOOK_W 528.0f
#define BOOK_TOP 144.0f
#define BOOK_PAGE_H 236.0f
#define VISIBLE 8
#define ROW_X (-236.0f)
#define ROW_TOP 94.0f
#define ROW_STEP 24.0f
#define VALUE_X 244.0f
/* the help box holds about 30 letters a line (15 of Japanese) */
#define HELP_COLS 30

typedef struct Row {
    int kind; /* KIND_HEADING, a control's (0-2), KIND_RATE or KIND_ALL */
    const char* label;
    PortSettingsControl c;
    int form;         /* KIND_FORM: its form (sForms) */
    PortPauseRow f;   /* KIND_FORM: the form's row */
    const char* tab;  /* KIND_SECTION: the full page's tab it opens */
} Row;

/* the features' Start menu forms (portPauseAddForm) */
#define MAX_FORMS 32
static const PortPauseForm* sForms[MAX_FORMS];
static int sFormCount;
static const char* sMessage; /* a form's answer, in the help box until the cursor moves (English) */
static int sMessageN;        /* (each answer its own help tag) */

void portPauseAddForm(const PortPauseForm* form) {
    if (sFormCount < MAX_FORMS) {
        sForms[sFormCount++] = form;
    }
}

/* the full page's sections (portSettingsAddSection) with a form here: their
 * forms say which (PortPauseForm.section) */
static int formFor(const char* tab, const char* section) {
    int i;
    for (i = 0; i < sFormCount; i++) {
        if (strcmp(sForms[i]->tab, tab) == 0 && sForms[i]->section != NULL && strcmp(sForms[i]->section, section) == 0) {
            return 1;
        }
    }
    return 0;
}

static int selectable(int kind) {
    return kind != KIND_HEADING && kind != KIND_SUBHEAD;
}

/* a row's name for the game's text: a form's row's own text as it is */
static const char* rowLabel(const Row* r) {
    return r->kind == KIND_FORM && r->f.label == NULL ? r->f.text : portTrGame(r->label);
}

static Row sRows[MAX_ROWS];
static int sCount;
static s32 sRow, sTop;
static char sHelp[1024];
static char sPageHelp[512]; /* a form's page's help (portPausePageMain) */
static int sConfirm; /* the Restart now row's first A was pressed */
#define MAX_CATS 32
static const char* sCats[MAX_CATS]; /* the categories: the settings page's tabs, in its order */
static int sCatCount;
static int sCat = -1;                          /* the category's page shown, or -1: the list */
static s32 sPageRow[MAX_CATS + 1], sPageTop[MAX_CATS + 1]; /* each page's cursor and view (the list's at 0) */
/* the front page's buttons as last built (drawn while they slide away), and
 * the slide: 0 the front page, 1 a category's (its book up, the buttons down
 * behind the help box, as the Journal's entries go to their pages) */
static Row sFront[MAX_CATS + 8];
static int sFrontCount;
static s32 sFrontRow;
static f32 sSlide, sSlideTo;
static Row sOut[MAX_ROWS]; /* the page leaving: its rows, kept while the book slides away */
static int sOutCount;
static s32 sOutRow, sOutTop;

static const char* const kRates[] = {"60", "120", "144", "240", "display"};
static const char* const kRateNames[] = {"60 Hz", "120 Hz", "144 Hz", "240 Hz", "The display's rate"};

float portPauseTabX(int n, int i) {
    return n >= 7 ? i * 78.0f - 234.0f : n >= 6 ? i * 90.0f - 225.0f : i * 105.0f - 205.0f;
}

float portPauseTabScale(int n) {
    return n >= 7 ? 0.76f : n >= 6 ? 0.86f : 1.0f;
}

static int rateIndex(void) {
    int i;
    for (i = 0; i < 5; i++) {
        if (strcmp(portSettings()->framerate, kRates[i]) == 0) {
            return i;
        }
    }
    return 0;
}

/* the settings page's tabs that are modes or tools, not settings (they keep
 * their pages there), and the Wardrobe's (port/src/aurora/wardrobe_aurora.c) */
static int toolTab(const char* tab) {
    static const char* const kTools[] = {"Cosmetics", "Practice",  "Challenges",  "Modes",
                                         "Achievements", "Mods", "Randomizer", "Archipelago", "Streaming"};
    size_t i;
    for (i = 0; i < sizeof(kTools) / sizeof(kTools[0]); i++) {
        if (strcmp(tab, kTools[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The rows of the page shown: the list (sCat -1): each category (a settings
 * page tab with plain settings), then Restart now when a next-start setting
 * changed, Photo mode and All settings; or a category's page: its name,
 * its controls (Display's frame rate, a string, first), and Restart now when
 * one is pending and the page has a next-start setting. The cursor kept on
 * the same row. */
static void buildRows(void) {
    static PortSettingsControl controls[MAX_ROWS];
    /* (by label and kind: a tab and its only setting can share a name, and one string) */
    const char* was = sCount > 0 && sRow < sCount ? sRows[sRow].label : NULL;
    const int wasKind = sCount > 0 && sRow < sCount ? sRows[sRow].kind : 0;
    const int wasForm = sCount > 0 && sRow < sCount ? sRows[sRow].form : -1;
    const int wasId = sCount > 0 && sRow < sCount ? sRows[sRow].f.id : 0;
    const int n = portSettingsControls(controls, MAX_ROWS - 24);
    const int pending = portRestartSupported() && portRestartPending();
    int i, nextStart = 0;
    sCatCount = 0;
    for (i = 0; i < n; i++) {
        if (!toolTab(controls[i].tab) && sCatCount < MAX_CATS &&
            (sCatCount == 0 || strcmp(sCats[sCatCount - 1], controls[i].tab) != 0)) {
            sCats[sCatCount++] = controls[i].tab;
        }
    }
    if (sCat >= sCatCount) {
        sCat = -1;
    }
    sCount = 0;
    if (sCat < 0) {
        for (i = 0; i < sCatCount; i++) {
            sRows[sCount].kind = KIND_CATEGORY;
            sRows[sCount].label = sCats[i];
            sRows[sCount].c.flags = 0;
            sRows[sCount++].c.note = NULL;
        }
    } else {
        const char* tab = sCats[sCat];
        sRows[sCount].kind = KIND_HEADING;
        sRows[sCount++].label = tab;
        if (strcmp(tab, "Display") == 0) {
            sRows[sCount].kind = KIND_RATE;
            sRows[sCount].label = "Frame rate";
            sRows[sCount].c.flags = 0;
            sRows[sCount].c.note = "Extra frames between the game's own 60: smoother motion.";
            sCount++;
        }
        if (strcmp(tab, "Controls") == 0) { /* the Bindings page's row (pause_bindings_aurora.c) */
            sRows[sCount].kind = KIND_BINDINGS;
            sRows[sCount].label = "Bindings...";
            sRows[sCount].c.flags = 0;
            sRows[sCount].c.note = "Every key and controller button: the game's, and Paper Cut's own shortcuts.";
            sCount++;
        }
        for (i = 0; i < n && sCount < MAX_ROWS - 3; i++) {
            if (strcmp(controls[i].tab, tab) != 0) {
                continue;
            }
            nextStart |= (controls[i].flags & PORT_SETTING_NEXT_START) != 0;
            sRows[sCount].kind = controls[i].kind;
            sRows[sCount].label = controls[i].label;
            sRows[sCount++].c = controls[i];
        }
        /* the features' forms for it, then its sections without one: to the full page */
        for (i = 0; i < sFormCount; i++) {
            static PortPauseRow got[48];
            int k, m;
            if (strcmp(sForms[i]->tab, tab) != 0) {
                continue;
            }
            m = sForms[i]->rows(got, 48);
            for (k = 0; k < m && sCount < MAX_ROWS - 3; k++) {
                Row* r = &sRows[sCount++];
                memset(r, 0, sizeof(*r));
                r->kind = (got[k].flags & PORT_PAUSE_ROW_HEADING) ? KIND_SUBHEAD : KIND_FORM;
                r->label = got[k].label != NULL ? got[k].label : "";
                r->c.note = got[k].note;
                r->form = i;
                r->f = got[k];
            }
        }
        {
            const char* tabs[64];
            const char* labels[64];
            const int m = portSettingsSections(tabs, labels, 64);
            int k;
            for (k = 0; k < m && sCount < MAX_ROWS - 3; k++) {
                Row* r;
                if (strcmp(tabs[k], tab) != 0 || formFor(tab, labels[k]) || strcmp(labels[k], "Photo mode") == 0 ||
                    (strcmp(tab, "Controls") == 0 && strcmp(labels[k], "Bindings") == 0)) { /* (rows of their own) */
                    continue;
                }
                r = &sRows[sCount++];
                memset(r, 0, sizeof(*r));
                r->kind = KIND_SECTION;
                r->label = labels[k];
                r->tab = tabs[k];
                r->form = -1;
                r->c.note = "Opens Paper Cut's full settings page on this category: this one isn't in the Start menu.";
            }
        }
    }
    if (pending && (sCat < 0 || nextStart)) {
        memset(&sRows[sCount], 0, sizeof(Row));
        sRows[sCount].form = -1;
        sRows[sCount].kind = KIND_RESTART;
        sRows[sCount].label = "Restart now";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Press A twice to restart the game for these settings; quick resume brings you "
                                 "back here.";
    } else {
        sConfirm = 0;
    }
    if (sCat < 0 && portPhotoAvailable()) { /* (port/settings/features/photo_mode.cpp) */
        sRows[sCount].kind = KIND_PHOTO;
        sRows[sCount].label = "Photo mode";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Closes the menu and holds the game: the sticks move the camera, A takes a picture, "
                                 "Y hides the HUD, B goes back.";
    }
    if (sCat < 0) {
        sRows[sCount].kind = KIND_ALL;
        sRows[sCount].label = "Full settings page...";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Paper Cut's full settings page, over the game: every setting, and remapping, mods "
                                 "and practice.";
    }
    for (i = 0; was != NULL && i < sCount; i++) {
        if (sRows[i].kind == wasKind &&
            (wasKind == KIND_FORM ? sRows[i].form == wasForm && sRows[i].f.id == wasId : sRows[i].label == was)) {
            sRow = i;
            return;
        }
    }
    if (sRow >= sCount || !selectable(sRows[sRow].kind)) {
        sRow = sCat < 0 ? 0 : 1;
        while (sRow + 1 < sCount && !selectable(sRows[sRow].kind)) {
            sRow++;
        }
    }
}

static void build(void) {
    buildRows();
    if (sCat < 0) { /* (kept for the slide) */
        sFrontCount = sCount < (int)(sizeof(sFront) / sizeof(sFront[0])) ? sCount : (int)(sizeof(sFront) / sizeof(sFront[0]));
        memcpy(sFront, sRows, sFrontCount * sizeof(Row));
        sFrontRow = sRow;
    }
}

/* the front page's grid: rows a column, and button k's middle */
static int frontRows(int n) {
    return (n + 1) / 2;
}

static f32 frontStep(int n) {
    const int rows = frontRows(n);
    return rows <= 1 || BANNER_SPAN / (rows - 1) > 45.0f ? 45.0f : BANNER_SPAN / (rows - 1);
}

static void frontAt(int k, int n, f32* x, f32* y) {
    const int rows = frontRows(n);
    *x = k < rows ? -BANNER_COL_X : BANNER_COL_X;
    *y = BANNER_MID + frontStep(n) * (rows - 1) / 2.0f - frontStep(n) * (k % rows);
}

/* a category page's row i (1 on: 0 is its heading): its text's top on the book */
static f32 pageRowY(int i) {
    return ROW_TOP - (i - sTop) * ROW_STEP;
}

/* a category's help: the first few of its settings' names, in the game's encoding */
static void categoryNote(const char* tab, char* out, size_t size) {
    static PortSettingsControl controls[MAX_ROWS];
    const int n = portSettingsControls(controls, MAX_ROWS - 24);
    const int jp = strcmp(PORT_GAME_ID, "G8MJ01") == 0;
    int i, shown = 0, more = 0;
    size_t len = 0;
    out[0] = '\0';
    if (strcmp(tab, "Display") == 0) {
        len += snprintf(out, size, "%s", portTrGame("Frame rate"));
        shown++;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(controls[i].tab, tab) != 0) {
            continue;
        }
        if (shown >= 3) {
            more = 1;
            break;
        }
        if (len < size) {
            len += snprintf(out + len, size - len, "%s%s", shown > 0 ? (jp ? "\x81\x45" : ", ") : "",
                            portTrGame(controls[i].label));
        }
        shown++;
    }
    if (more && len < size) {
        snprintf(out + len, size - len, "%s", jp ? "\x81\x44\x81\x44\x81\x44" : "...");
    }
}

static void valueText(const Row* r, char* out, size_t size) {
    const PortSettingsControl* c = &r->c;
    out[0] = '\0';
    if (r->kind == KIND_FORM) {
        snprintf(out, size, "%s", r->f.valueTr != NULL ? portTrGame(r->f.valueTr) : r->f.value);
    } else if (r->kind == KIND_SECTION) {
        snprintf(out, size, "%s", portTrGame("On the full page"));
    } else if (r->kind == KIND_RATE) {
        snprintf(out, size, "%s", portTrGame(kRateNames[rateIndex()]));
    } else if (r->kind == 0 || r->kind == 1) {
        const int v = *c->value;
        snprintf(out, size, "%s", v >= 0 && v < c->count && c->choices != NULL ? portTrGame(c->choices[v]) : "?");
    } else if (r->kind == 2) {
        snprintf(out, size, c->format != NULL ? portTrGame(c->format) : "%d", *c->value);
    }
    /* '<' starts a tag in the game's text: shown as a look-alike */
    for (; *out != '\0'; out++) {
        *out = *out == '<' ? '(' : *out == '>' ? ')' : *out;
    }
}

/* left (-1) or right (+1): the next value, wrapping like the game's own option
 * lists; numbers step and stop at the ends */
static int change(const Row* r, int dir) {
    PortSettings* s = portSettings();
    const PortSettingsControl* c = &r->c;
    int v;
    switch (r->kind) {
        case KIND_RATE:
            snprintf(s->framerate, sizeof(s->framerate), "%s", kRates[(rateIndex() + dir + 5) % 5]);
            break;
        case 0:
        case 1:
            *c->value = (*c->value + dir + c->count) % c->count;
            break;
        case 2:
            v = *c->value + dir * c->step;
            v = v < c->min ? c->min : v > c->max ? c->max : v;
            if (v == *c->value) {
                return 0;
            }
            *c->value = v;
            break;
        default:
            return 0;
    }
    portSettingsApply();
    portSettingsSave();
    return 1;
}

/* a Shift-JIS lead byte (the JP release's text): a letter two bytes long and
 * two columns wide */
static int sjisLead(unsigned char c) {
    return strcmp(PORT_GAME_ID, "G8MJ01") == 0 && ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC));
}

/* a word's separator: a space, or the full-width space the Japanese text puts
 * between words (as the game's own does); its width in columns, or 0 */
static size_t separator(const char* p) {
    if (*p == ' ') {
        return 1;
    }
    return sjisLead((unsigned char)p[0]) && (unsigned char)p[0] == 0x81 && (unsigned char)p[1] == 0x40 ? 2 : 0;
}

/* text in the game's encoding wrapped at about HELP_COLS columns, its '<'
 * made '(' (a tag in the game's text): between words, and inside a word only
 * when it is longer than a line */
void portPauseWrap(const char* text, char* out, size_t size) {
    size_t n = 0, line = 0;
    while (*text != '\0' && n + 4 < size) {
        const char* end = text;
        size_t width = 0, sep;
        if (*text == '\n') { /* a break the text has itself */
            out[n++] = '\n';
            line = 0;
            text++;
            continue;
        }
        if ((sep = separator(text)) != 0) {
            const int two = sep == 2;
            text += two ? 2 : 1;
            if (line > 0 && line + sep <= HELP_COLS && *text != '\0' && *text != '\n') {
                out[n++] = two ? (char)0x81 : ' ';
                if (two) {
                    out[n++] = 0x40;
                }
                line += sep;
            }
            continue;
        }
        /* the word: up to the next separator or break; letters 1 or 2 columns */
        while (*end != '\0' && *end != '\n' && separator(end) == 0) {
            const int two = sjisLead((unsigned char)*end) && end[1] != '\0';
            end += two ? 2 : 1;
            width += two ? 2 : 1;
        }
        if (line > 0 && line + width > HELP_COLS) {
            out[n++] = '\n';
            line = 0;
        }
        while (text < end && n + 4 < size) {
            const int two = sjisLead((unsigned char)*text) && text[1] != '\0';
            if (line + (two ? 2 : 1) > HELP_COLS) { /* a word longer than a line */
                out[n++] = '\n';
                line = 0;
            }
            out[n++] = *text == '<' ? '(' : *text == '>' ? ')' : *text;
            if (two) {
                out[n++] = text[1];
            }
            text += two ? 2 : 1;
            line += two ? 2 : 1;
        }
    }
    out[n] = '\0';
}

/* each frame of the help box (win_root.c's winMsgMain) on the port's tabs: a
 * note longer than the box's two lines turns its page by itself every few
 * seconds, back to the first after the last (the C-stick still turns it at
 * once, and the wait starts again); each menu its own (the second screen's) */
void portPauseHelpTurn(WindowWork* wp) {
    static const char* sMsg[2];
    static s32 sLine[2];
    static int sWait[2];
    const int k = gPortCompanionMenu != 0;
    if (wp->msg != sMsg[k] || wp->msgLine != sLine[k]) { /* another note, or the C-stick */
        sMsg[k] = wp->msg;
        sLine[k] = wp->msgLine;
        sWait[k] = 0;
        return;
    }
    if (wp->msgLines <= 2 || ++sWait[k] < FPS_FRAMES(210)) {
        return;
    }
    wp->msgLine = wp->msgLine / 2 < (wp->msgLines + 1) / 2 - 1 ? wp->msgLine + 2 : 0;
    sLine[k] = wp->msgLine;
    sWait[k] = 0;
}

/* the help box shows the row's note (winMsgEntry keeps the text while the tag
 * is the same, so each row has its own tag) and the hand points at the row */
#define TAG_ROWS 64
static void pointAt(WindowWork* wp) {
    /* (one tag a page's row: each page's rows are its own) */
    static char tags[MAX_CATS + 1][TAG_ROWS][28];
    const Row* r = &sRows[sRow];
    char* tag = tags[sCat + 1][sRow % TAG_ROWS];
    char text[1024];
    if (tag[0] == '\0') {
        snprintf(tag, sizeof(tags[0][0]), "pc_menu_settings_%d_%d", sCat + 1, (int)(sRow % TAG_ROWS));
    }
    if (sCat < 0) { /* as the Journal's: the hand at the button's left end */
        f32 bx, by;
        frontAt((int)sRow, sCount, &bx, &by);
        wp->helpTgtX = bx - BANNER_W / 2.0f - 16.0f;
        wp->helpTgtY = by + 8.0f;
    } else {
        wp->helpTgtX = ROW_X - 24.0f;
        wp->helpTgtY = pageRowY((int)sRow) - 8.0f;
    }
    if (sMessage != NULL) { /* a form's answer */
        static char tagm[32]; /* (winMsgEntry keeps the tag's address) */
        snprintf(tagm, sizeof(tagm), "pc_menu_settings_msg%d", sMessageN);
        portPauseWrap(portTrGame(sMessage), sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, tagm, 0);
        return;
    }
    if (r->kind == KIND_FORM && sConfirm) {
        portPauseWrap(portTrGame("Press A again to confirm."), sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, "pc_menu_settings_confirm", 0);
        return;
    }
    if (r->kind == KIND_RESTART && sConfirm) {
        /* (a tag of its own: the text changes on the same row) */
        portPauseWrap(portTrGame("Press A again to restart."), sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, "pc_menu_settings_restart", 0);
        return;
    }
    if (r->kind == KIND_CATEGORY) {
        categoryNote(r->label, text, sizeof(text));
        portPauseWrap(text, sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, tag, 0);
        return;
    }
    snprintf(text, sizeof(text), "%s", portTrGame(r->c.note != NULL ? r->c.note : r->label));
    if (r->kind >= 0 && r->kind <= 2 && (r->c.flags & PORT_SETTING_NEXT_START)) {
        /* applies from the next start: how to get there now */
        const char* how = portTrGame(portRestartSupported() ? "Applies after Restart now."
                                                            : "Applies when the app reopens.");
        const size_t len = strlen(text);
        snprintf(text + len, sizeof(text) - len, "\n%s", how);
    }
    portPauseWrap(text, sHelp, sizeof(sHelp));
    winMsgEntry(wp, 0, tag, 0);
}

/* a category page's view: VISIBLE rows from sTop (1 on), the cursor in it */
static void follow(void) {
    if (sTop < 1) {
        sTop = 1;
    }
    if (sRow < sTop) {
        sTop = sRow < 1 ? 1 : sRow;
    }
    if (sRow >= sTop + VISIBLE) {
        sTop = sRow - VISIBLE + 1;
    }
}

static void move(int dir) {
    do {
        sRow = (sRow + dir + sCount) % sCount;
    } while (!selectable(sRows[sRow].kind));
    if (sCat >= 0) {
        follow();
    }
}

/* the front page, left or right: the button beside it in the other column
 * (the column's last when that one is shorter); FALSE at the edge */
static int across(int dir) {
    const int rows = frontRows(sCount);
    int to = (int)sRow + dir * rows;
    if (dir > 0 && sRow < rows && to >= sCount) {
        to = sCount - 1;
    }
    if (to < 0 || to >= sCount || to == sRow) {
        return 0;
    }
    sRow = to;
    return 1;
}

/* the mouse and touch (port/include/port/menu_tap.h), as on the Journal's
 * lists: the button or row under the pointer is selected; a click on the
 * selected one is A */
static int pointer(void) {
    f32 gx, gy, bx, by;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0 || (sSlide > 0.05f && sSlide < 0.95f)) {
        return 0;
    }
    for (i = 0; i < sCount; i++) {
        if (!selectable(sRows[i].kind)) {
            continue;
        }
        if (sCat < 0) {
            frontAt(i, sCount, &bx, &by);
            if (gx < bx - BANNER_W / 2.0f || gx > bx + BANNER_W / 2.0f || gy > by + 16.0f || gy <= by - 16.0f) {
                continue;
            }
        } else {
            const f32 y = pageRowY(i) - 8.0f; /* (the row's middle: the text hangs from its y) */
            if (i < sTop || i >= sTop + VISIBLE || gx < ROW_X - 16.0f || gx > VALUE_X + 8.0f ||
                gy > y + ROW_STEP / 2.0f || gy <= y - ROW_STEP / 2.0f) {
                continue;
            }
        }
        if (i == sRow) {
            return kind == 1;
        }
        sRow = i;
        psndSFXOn((const char*)0x20005);
        return 0;
    }
    return 0;
}

/* to page cat (-1: the list), each page's cursor and view kept while the menu is open */
static void showPage(int cat) {
    if (cat < 0 && sCat >= 0) { /* (drawn as the book goes) */
        memcpy(sOut, sRows, sCount * sizeof(Row));
        sOutCount = sCount;
        sOutRow = sRow;
        sOutTop = sTop;
    }
    sPageRow[sCat + 1] = sRow;
    sPageTop[sCat + 1] = sTop;
    sCat = cat;
    sRow = sPageRow[cat + 1] >= 0 ? sPageRow[cat + 1] : cat < 0 ? 0 : 1;
    sTop = sPageTop[cat + 1];
    sCount = 0; /* (the cursor from this page's own, not the last one's row) */
    sConfirm = 0;
    sSlideTo = cat < 0 ? 0.0f : 1.0f;
    build();
    if (cat >= 0) {
        follow();
    }
}

void portPauseSettingsInit(WindowWork* wp) {
    int i;
    (void)wp;
    for (i = 0; i <= MAX_CATS; i++) {
        sPageRow[i] = -1;
        sPageTop[i] = 0;
    }
    sCat = -1;
    sBindings = 0;
    sCount = 0;
    sRow = 0;
    sTop = 0;
    sSlide = sSlideTo = 0.0f;
    build();
}

void portPauseSettingsInit2(WindowWork* wp) {
    const char* want = getenv("TTYD_PAUSE_ROW"); /* tests: the row chosen first, by its label */
    int i;
    wp->flags |= WIN_FLAG_SUBMENU;
    build();
    for (i = 0; want != NULL && i < sCount; i++) {
        if (sRows[i].label != NULL && strcmp(sRows[i].label, want) == 0) {
            sRow = i;
            sTop = i - 1 < 0 ? 0 : i - 1;
        }
    }
    pointAt(wp);
}

s32 portPauseSettingsMain(WindowWork* wp) {
    const u32 wasTrg = wp->buttonTrg, wasRep = wp->dirRep;
    const s32 wasRow = sRow;
    if (sBindings) {
        if (portPauseBindingsMain(wp) < 0) {
            sBindings = 0;
            pointAt(wp);
        }
        return 0;
    }
    if (portPausePage() != NULL) { /* a form's page of its own (port/include/port/pause_forms.h) */
        portPausePageMain(wp);
        return 0;
    }
    build(); /* a feature's setting shown or hidden by another (widescreen's HUD) */
    if (wp->buttonTrg & PAD_BUTTON_B) {
        sMessage = NULL;
        psndSFXOn((const char*)0x20013);
        if (sCat < 0) {
            return -1;
        }
        showPage(-1); /* a category's page: back to the list */
        pointAt(wp);
        return 0;
    }
    if (!(wp->buttonTrg & PAD_BUTTON_A) && (wp->buttonTrg != 0 || wp->dirRep != 0)) {
        sConfirm = 0; /* anything else: Restart now asks again */
    }
    if (pointer() || (wp->buttonTrg & PAD_BUTTON_A)) {
        if (sRows[sRow].kind == KIND_CATEGORY) {
            psndSFXOn((const char*)0x20012);
            showPage((int)sRow);
        } else if (sRows[sRow].kind == KIND_BINDINGS) {
            psndSFXOn((const char*)0x20012);
            sBindings = 1;
            portPauseBindingsOpen(wp);
            return 0;
        } else if (sRows[sRow].kind == KIND_ALL) {
            psndSFXOn((const char*)0x20012);
            portSettingsToggle();
        } else if (sRows[sRow].kind == KIND_PHOTO) {
            psndSFXOn((const char*)0x20012);
            portPhotoRequest();
            return -2; /* the whole menu closed: photo mode once Mario has the controls */
        } else if (sRows[sRow].kind == KIND_RESTART) {
            psndSFXOn((const char*)0x20012);
            if (sConfirm) {
                portRestartRequest(1);
            }
            sConfirm = !sConfirm;
        } else if (sRows[sRow].kind == KIND_SECTION) { /* not in the Start menu: the full page on its tab */
            psndSFXOn((const char*)0x20012);
            portSettingsOpenAt(sRows[sRow].tab);
        } else if (sRows[sRow].kind == KIND_FORM) {
            const Row* r = &sRows[sRow];
            psndSFXOn((const char*)0x20012);
            if ((r->f.flags & PORT_PAUSE_ROW_CONFIRM) && !sConfirm) {
                sConfirm = 1;
                sMessage = NULL;
            } else {
                sConfirm = 0;
                sMessage = sForms[r->form]->act(r->f.id, 0);
                sMessageN++;
            }
        } else if (change(&sRows[sRow], 1)) {
            psndSFXOn((const char*)0x20005);
        }
    } else if (wp->dirRep & DEMO_STICK_UP) {
        move(-1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & DEMO_STICK_DOWN) {
        move(1);
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int dir = (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1;
        if (sCat >= 0 && sRows[sRow].kind == KIND_FORM) {
            sMessage = sForms[sRows[sRow].form]->act(sRows[sRow].f.id, dir);
            sMessageN++;
            psndSFXOn((const char*)0x20005);
        } else if (sCat < 0 ? across(dir) : change(&sRows[sRow], dir)) {
            psndSFXOn((const char*)0x20005);
        }
    }
    if (sRow != wasRow || (!(wasTrg & PAD_BUTTON_A) && wasTrg != 0) ||
        ((wasRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) != 0)) {
        if (!((wasRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) && sRows[sRow].kind == KIND_FORM)) {
            sMessage = NULL; /* the cursor moved on: the rows' own help again */
        }
    }
    if (sRow != wasRow) {
        sConfirm = 0;
    }
    pointAt(wp);
    if (sRow < sCount) { /* read aloud: the row, its value and note, when either changes */
        static char sSaid[600];
        char text[600], value[64], note[256];
        valueText(&sRows[sRow], value, sizeof(value));
        if (sMessage != NULL) {
            snprintf(note, sizeof(note), "%s", portTrGame(sMessage));
        } else if (sRows[sRow].kind == KIND_CATEGORY) {
            categoryNote(sRows[sRow].label, note, sizeof(note)); /* (what's in it) */
        } else {
            snprintf(note, sizeof(note), "%s", sRows[sRow].c.note != NULL ? portTrGame(sRows[sRow].c.note) : "");
        }
        snprintf(text, sizeof(text), "%s%s%s. %s", rowLabel(&sRows[sRow]), value[0] != '\0' ? ", " : "",
                 value, note);
        if (strcmp(text, sSaid) != 0) {
            snprintf(sSaid, sizeof(sSaid), "%s", text);
            portSpeechMenu(text);
        }
    }
    return 0;
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
}

/* --- The tab's label, painted as the game's other tabs are. Those are
 * pictures in the menu's TPL (win.tpl 1-5: a painted tab with its word, cut
 * out by 0xB2's shape). The Settings tab's is made from the Journal's (5) at
 * load, by recipe: its word taken out (the pale letters and their dark
 * outline painted over with the row's own panel or rim colour), the blue
 * turned violet, 0xB2's shape as its alpha; then "Settings" goes on it
 * in the game's font, as pale as the other tabs' words. Nothing of the disc
 * is kept outside the game's memory. */

#define TAB_TEX_W 96
#define TAB_TEX_H 40

/* a painted tab: the Settings tab's (the Journal's blue turned 55 degrees, to
 * violet) or the Wardrobe's (-35, to teal) */
typedef struct PaintedTab {
    u8 pixels[TAB_TEX_W * TAB_TEX_H * 4] __attribute__((aligned(32))); /* GX RGBA8 */
    GXTexObj obj;
    const void* from; /* the TPL image it was made from (made again when the TPL is) */
    int ready;
    float hue; /* degrees */
} PaintedTab;

static PaintedTab sSettingsTab = {.hue = 55.0f};
static PaintedTab sWardrobeTab = {.hue = -35.0f};

static u32 rd16(const u8* p) {
    return ((u32)p[0] << 8) | p[1];
}

static void rgb565(u32 v, u8* out) {
    out[0] = (u8)(((v >> 11) & 31) * 255 / 31);
    out[1] = (u8)(((v >> 5) & 63) * 255 / 63);
    out[2] = (u8)((v & 31) * 255 / 31);
    out[3] = 0xFF;
}

/* a CMPR (S3TC in 8x8 tiles of 2x2 blocks, big-endian) image into RGBA */
static void decodeCmpr(const u8* src, int w, int h, u8* rgba) {
    int tx, ty, b, i;
    for (ty = 0; ty < h; ty += 8) {
        for (tx = 0; tx < w; tx += 8) {
            for (b = 0; b < 4; b++, src += 8) {
                const int bx = tx + (b & 1) * 4, by = ty + (b >> 1) * 4;
                const u32 c0 = rd16(src), c1 = rd16(src + 2);
                u8 pal[4][4];
                rgb565(c0, pal[0]);
                rgb565(c1, pal[1]);
                for (i = 0; i < 3; i++) {
                    if (c0 > c1) {
                        pal[2][i] = (u8)((2 * pal[0][i] + pal[1][i]) / 3);
                        pal[3][i] = (u8)((pal[0][i] + 2 * pal[1][i]) / 3);
                    } else {
                        pal[2][i] = (u8)((pal[0][i] + pal[1][i]) / 2);
                        pal[3][i] = 0;
                    }
                }
                pal[2][3] = 0xFF;
                pal[3][3] = c0 > c1 ? 0xFF : 0x00;
                for (i = 0; i < 16; i++) {
                    const int px = bx + (i & 3), py = by + (i >> 2);
                    const int sel = (src[4 + (i >> 2)] >> (6 - 2 * (i & 3))) & 3;
                    if (px < w && py < h) {
                        memcpy(rgba + (py * w + px) * 4, pal[sel], 4);
                    }
                }
            }
        }
    }
}

/* an I4 image (8x8 tiles, the high nibble first) into one byte a pixel */
static void decodeI4(const u8* src, int w, int h, u8* out) {
    int tx, ty, i;
    for (ty = 0; ty < h; ty += 8) {
        for (tx = 0; tx < w; tx += 8) {
            for (i = 0; i < 64; i++) {
                const int px = tx + (i & 7), py = ty + (i >> 3);
                const int v = (i & 1) ? (src[i >> 1] & 15) : (src[i >> 1] >> 4);
                if (px < w && py < h) {
                    out[py * w + px] = (u8)(v * 17);
                }
            }
            src += 32;
        }
    }
}

/* the colour turned around the hue circle by deg */
static void hueShift(u8* p, float deg) {
    const float r = p[0] / 255.0f, g = p[1] / 255.0f, b = p[2] / 255.0f;
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const float d = mx - mn;
    float h = 0.0f, f, q, t, rgb[3];
    int sector;
    if (d <= 0.0f) {
        return;
    }
    h = mx == r ? (g - b) / d : mx == g ? 2.0f + (b - r) / d : 4.0f + (r - g) / d;
    h = h * 60.0f + deg;
    while (h < 0.0f) {
        h += 360.0f;
    }
    while (h >= 360.0f) {
        h -= 360.0f;
    }
    sector = (int)(h / 60.0f);
    f = h / 60.0f - sector;
    q = mx - d * f;
    t = mn + d * f;
    switch (sector) {
        case 0: rgb[0] = mx, rgb[1] = t, rgb[2] = mn; break;
        case 1: rgb[0] = q, rgb[1] = mx, rgb[2] = mn; break;
        case 2: rgb[0] = mn, rgb[1] = mx, rgb[2] = t; break;
        case 3: rgb[0] = mn, rgb[1] = q, rgb[2] = mx; break;
        case 4: rgb[0] = t, rgb[1] = mn, rgb[2] = mx; break;
        default: rgb[0] = mx, rgb[1] = mn, rgb[2] = q; break;
    }
    p[0] = (u8)(rgb[0] * 255.0f + 0.5f);
    p[1] = (u8)(rgb[1] * 255.0f + 0.5f);
    p[2] = (u8)(rgb[2] * 255.0f + 0.5f);
}

/* a letter's pale paint (its fill): bright and nearly grey */
static int pale(const u8* p) {
    const int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
    const int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
    return mx >= 120 && (mx - mn) * 100 < 55 * mx;
}

static int colorDist(const u8* a, const u8* b) {
    const int r = a[0] - b[0], g = a[1] - b[1], bl = a[2] - b[2];
    return r * r + g * g + bl * bl;
}

/* the median of some colours, each channel on its own */
typedef struct Median {
    int count[3][256];
    int n;
} Median;

static void medianClear(Median* m) {
    memset(m, 0, sizeof(*m));
}

static void medianAdd(Median* m, const u8* p) {
    m->count[0][p[0]]++;
    m->count[1][p[1]]++;
    m->count[2][p[2]]++;
    m->n++;
}

static BOOL medianGet(const Median* m, u8* out) {
    int i, v;
    if (m->n == 0) {
        return FALSE;
    }
    for (i = 0; i < 3; i++) {
        int seen = 0;
        for (v = 0; v < 256; v++) {
            seen += m->count[i][v];
            if (seen * 2 > m->n) {
                break;
            }
        }
        out[i] = (u8)(v < 256 ? v : 255);
    }
    return TRUE;
}

/* The Settings tab's picture from the TPL's Journal tab (5) and tab shape
 * (0xB2); FALSE when they aren't what the recipe expects (a plain tab then) */
static BOOL makeTab(TEXPalette* tpl, PaintedTab* pt) {
    static u8 rgba[TAB_TEX_W * TAB_TEX_H * 4], out[TAB_TEX_W * TAB_TEX_H * 4];
    static u8 shape[TAB_TEX_W * TAB_TEX_H], letter[TAB_TEX_W * TAB_TEX_H], grown[TAB_TEX_W * TAB_TEX_H];
    const int w = TAB_TEX_W, h = TAB_TEX_H;
    TEXDescriptor* tab = TEXGet(tpl, 5);
    TEXDescriptor* cut = TEXGet(tpl, 0xB2);
    int x, y, top = h, bottom = -1;
    if (tab == NULL || cut == NULL || tab->textureHeader == NULL || cut->textureHeader == NULL ||
        tab->textureHeader->format != GX_TF_CMPR || tab->textureHeader->width != w ||
        tab->textureHeader->height != h || cut->textureHeader->format != GX_TF_I4 ||
        cut->textureHeader->width != w || cut->textureHeader->height != h) {
        return FALSE;
    }
    decodeCmpr((const u8*)tab->textureHeader->data, w, h, rgba);
    decodeI4((const u8*)cut->textureHeader->data, w, h, shape);
    /* the word's rows: where the pale fill is */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (shape[y * w + x] > 0x80 && pale(rgba + (y * w + x) * 4)) {
                top = y < top ? y : top;
                bottom = y > bottom ? y : bottom;
            }
        }
    }
    /* the letters: their fill, and their dark outline within those rows; then a
     * little more around them (their soft edges) */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            const u8* p = rgba + (y * w + x) * 4;
            const int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
            letter[y * w + x] = shape[y * w + x] > 0x80 &&
                                (pale(p) || (mx < 120 && y >= top - 1 && y <= bottom + 1));
        }
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int yy, xx, any = 0;
            for (yy = y - 2; yy <= y + 2 && !any; yy++) {
                for (xx = x - 1; xx <= x + 1 && !any; xx++) {
                    any = yy >= 0 && yy < h && xx >= 0 && xx < w && letter[yy * w + xx];
                }
            }
            grown[y * w + x] = (u8)any;
        }
    }
    /* The tab is a darker rim around a lighter panel. Where the letters were,
     * each row gets the paint of its own panel (inside the panel's box) or of
     * its rim, as the rest of the row has it: */
    {
        static u8 edge[TAB_TEX_W * TAB_TEX_H], panel[TAB_TEX_W * TAB_TEX_H];
        u8 rimRef[3], panRef[3], rowPan[3], rowRim[3];
        int pass, x0, x1, y0, y1;
        Median m;
        /* each pixel's distance from the shape's edge (to 12) */
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                edge[y * w + x] = shape[y * w + x] > 0x80 ? 12 : 0;
            }
        }
        for (pass = 0; pass < 12; pass++) {
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    int yy, xx, d = edge[y * w + x];
                    for (yy = y - 1; yy <= y + 1; yy++) {
                        for (xx = x - 1; xx <= x + 1; xx++) {
                            const int n = yy < 0 || yy >= h || xx < 0 || xx >= w ? 0 : edge[yy * w + xx];
                            d = n + 1 < d ? n + 1 : d;
                        }
                    }
                    edge[y * w + x] = (u8)d;
                }
            }
        }
        /* the rim's paint: next to the edge (not the black past it); the panel's: well inside */
        medianClear(&m);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                const u8* p = rgba + (y * w + x) * 4;
                if (!grown[y * w + x] && edge[y * w + x] >= 1 && edge[y * w + x] <= 3 &&
                    (p[0] > 60 || p[1] > 60 || p[2] > 60)) {
                    medianAdd(&m, p);
                }
            }
        }
        if (!medianGet(&m, rimRef)) {
            return FALSE;
        }
        medianClear(&m);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (!grown[y * w + x] && edge[y * w + x] >= 7) {
                    medianAdd(&m, rgba + (y * w + x) * 4);
                }
            }
        }
        if (!medianGet(&m, panRef)) {
            return FALSE;
        }
        /* each paint pixel the panel's or the rim's (the nearer), and the panel's box */
        {
            static int xs[TAB_TEX_W], ys[TAB_TEX_H];
            int count = 0, k, seen;
            memset(xs, 0, sizeof(xs));
            memset(ys, 0, sizeof(ys));
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    const u8* p = rgba + (y * w + x) * 4;
                    panel[y * w + x] = shape[y * w + x] > 0x80 && !grown[y * w + x] &&
                                       colorDist(p, panRef) < colorDist(p, rimRef);
                    if (panel[y * w + x]) {
                        xs[x]++, ys[y]++, count++;
                    }
                }
            }
            if (count == 0) {
                return FALSE;
            }
            /* (its 2% to 98% of the panel's pixels, either way) */
            for (k = 0, seen = 0, x0 = 0; k < w && seen <= count / 50; k++) seen += xs[k], x0 = k;
            for (k = w - 1, seen = 0, x1 = w - 1; k >= 0 && seen <= count / 50; k--) seen += xs[k], x1 = k;
            for (k = 0, seen = 0, y0 = 0; k < h && seen <= count / 50; k++) seen += ys[k], y0 = k;
            for (k = h - 1, seen = 0, y1 = h - 1; k >= 0 && seen <= count / 50; k--) seen += ys[k], y1 = k;
        }
        memcpy(out, rgba, sizeof(out));
        for (y = 0; y < h; y++) {
            /* this row's panel and rim paint (the whole tab's when the row has none) */
            medianClear(&m);
            for (x = 0; x < w; x++) {
                if (panel[y * w + x]) {
                    medianAdd(&m, rgba + (y * w + x) * 4);
                }
            }
            if (!medianGet(&m, rowPan)) {
                memcpy(rowPan, panRef, 3);
            }
            medianClear(&m);
            for (x = 0; x < w; x++) {
                const u8* p = rgba + (y * w + x) * 4;
                if (shape[y * w + x] > 0x80 && !grown[y * w + x] && !panel[y * w + x] &&
                    (p[0] > 60 || p[1] > 60 || p[2] > 60)) {
                    medianAdd(&m, p);
                }
            }
            if (!medianGet(&m, rowRim)) {
                memcpy(rowRim, rimRef, 3);
            }
            for (x = 0; x < w; x++) {
                if (grown[y * w + x]) {
                    memcpy(out + (y * w + x) * 4, x >= x0 && x <= x1 && y >= y0 && y <= y1 ? rowPan : rowRim, 3);
                }
            }
        }
    }
    /* the tab's colour, the shape as alpha, in GX's RGBA8 (4x4 tiles: AR pairs, then GB) */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u8* p = out + (y * w + x) * 4;
            u8* tile = pt->pixels + ((y / 4) * (w / 4) + x / 4) * 64;
            const int at = ((y & 3) * 4 + (x & 3)) * 2;
            hueShift(p, pt->hue);
            tile[at] = shape[y * w + x];
            tile[at + 1] = p[0];
            tile[32 + at] = p[1];
            tile[32 + at + 1] = p[2];
        }
    }
    GXInitTexObj(&pt->obj, pt->pixels, w, h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&pt->obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    return TRUE;
}

/* a picture made at load (GX RGBA8) drawn w x h about (x, y), tinted by c
 * (its colour and alpha times c's) */
static void tintedQuad(GXTexObj* obj, float x, float y, float w, float h, GXColor c) {
    Mtx m;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, c);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXConcat(camGetPtr(CAMERA_2D)->view, m, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXLoadTexObj(obj, GX_TEXMAP0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-w / 2, h / 2, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(-w / 2, -h / 2, 0.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXPosition3f32(w / 2, -h / 2, 0.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(w / 2, h / 2, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
}

/* the tab drawn from the picture made above, tinted by c as the game's tabs
 * are (grey when not selected); FALSE when there is no picture */
static BOOL paintedTab(WindowWork* wp, PaintedTab* pt, float x, float y, float scale, GXColor c) {
    TEXPalette* tpl = (TEXPalette*)*wp->res->data;
    TEXDescriptor* tab = TEXGet(tpl, 5);
    const void* from = tab != NULL && tab->textureHeader != NULL ? tab->textureHeader->data : NULL;
    if (from != pt->from) {
        pt->from = from;
        pt->ready = from != NULL && makeTab(tpl, pt);
    }
    if (!pt->ready) {
        return FALSE;
    }
    tintedQuad(&pt->obj, x, y, TAB_TEX_W * scale, TAB_TEX_H * scale, c);
    return TRUE;
}

/* a word in the game's font, pale on a dark outline as the tabs' and the
 * Journal's buttons' words, its left at x */
static void outlinedText(f32 x, f32 y, f32 sx, f32 sy, GXColor dark, GXColor fill, const char* text) {
    int dx, dy;
    winFontInit();
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            if (dx != 0 || dy != 0) {
                winFontSet((Vec){x + dx * 1.5f, y + dy * 1.5f, 0.0f}, (Vec){sx, sy, sy}, dark, "%s", text);
            }
        }
    }
    winFontSet((Vec){x, y, 0.0f}, (Vec){sx, sy, sy}, fill, "%s", text);
}

/* a tab's label: the painted tab (above; a tinted plain one, the page panel's
 * shape and color, if the TPL isn't as expected) with its word on it in the
 * game's font, pale on a dark outline (dark's colour) as the other tabs' words */
static void tabDisp(WindowWork* wp, PaintedTab* pt, const char* kLabel, GXColor dark, GXColor pale, GXColor plain,
                    float x, float y, float scale, GXColor c) {
    /* the tab's size, and its middle below the label's top as the game's tabs sit */
    const f32 w = 108.0f * scale, h = 40.0f * scale, cy = y - 6.0f * scale;
    const u8 k = c.r; /* 0xFF selected, 0x80 not: the tab darkens with its label */
    const GXColor shadow = {(u8)(dark.r * k / 0xFF), (u8)(dark.g * k / 0xFF), (u8)(dark.b * k / 0xFF), c.a};
    const GXColor fill = {(u8)(pale.r * k / 0xFF), (u8)(pale.g * k / 0xFF), (u8)(pale.b * k / 0xFF), 0xFF};
    Mtx m;
    GXTexObj obj;
    f32 tw;

    if (paintedTab(wp, pt, x, y, scale, c)) {
        goto label;
    }
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_FALSE);
    GXSetAlphaCompare(GX_GEQUAL, 0x80, GX_AOP_OR, GX_NEVER, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, (GXColor){(u8)(plain.r * k / 0xFF), (u8)(plain.g * k / 0xFF), (u8)(plain.b * k / 0xFF), 0xFF});
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXTrans(m, x, cy, 0.0f);
    PSMTXConcat(camGetPtr(CAMERA_2D)->view, m, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    TEXGetGXTexObjFromPalette((TEXPalette*)*wp->res->data, &obj, 7);
    GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-w / 2, h / 2, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(-w / 2, -h / 2, 0.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXPosition3f32(w / 2, -h / 2, 0.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(w / 2, h / 2, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);

label:
    winFontInit();
    /* as wide as the tab's middle at most: narrower letters for a longer word (Einstellungen) */
    {
        const f32 full = (f32)FontGetMessageWidth(kLabel);
        const f32 room = TAB_TEX_W * scale - 14.0f * scale;
        const f32 sx = full * 0.95f * scale > room ? room / full : 0.95f * scale;
        const f32 sy = 0.95f * scale;
        tw = sx * full;
        outlinedText(x - tw / 2, y + 13.0f * scale, sx, sy, shadow, fill, kLabel);
    }
}

void portPauseSettingsTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    tabDisp(wp, &sSettingsTab, portTrGame("Settings"), (GXColor){0x3A, 0x1C, 0x66, 0xFF},
            (GXColor){0xF6, 0xEA, 0xF4, 0xFF}, (GXColor){0x6E, 0x5A, 0xB4, 0xFF}, x, y, scale, c);
}

void portPauseWardrobeTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    tabDisp(wp, &sWardrobeTab, portTrGame("Wardrobe"), (GXColor){0x0E, 0x46, 0x40, 0xFF},
            (GXColor){0xEA, 0xF6, 0xF2, 0xFF}, (GXColor){0x2A, 0x9A, 0x8E, 0xFF}, x, y, scale, c);
}

/* --- The front page's buttons, painted as the Journal's are. Those are
 * pictures in the menu's TPL (win.tpl 0x1F-0x23: a blue painted strip with
 * its word, cut out by 0xB5's shape). The Settings tab's are made from the
 * Journal's first (0x1F, the map's: its word the shortest) at load, by
 * recipe: its word taken out (the pale letters and their dark outline, each
 * row painted over with the median of that row's own paint either side of
 * the word), the blue turned violet as the Settings tab's, 0xB5's shape as
 * its alpha. Each category's picture and name go on it from the game's icons
 * and font. Nothing of the disc is kept outside the game's memory. */

#define BANNER_TEX_W 224
#define BANNER_TEX_H 32

static struct {
    u8 pixels[BANNER_TEX_W * BANNER_TEX_H * 4] __attribute__((aligned(32))); /* GX RGBA8 */
    GXTexObj obj;
    const void* from;
    int ready;
} sBanner;

/* a letter's pale paint on the Journal's buttons: bright and nearly grey */
static int bannerLetter(const u8* p) {
    const int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
    const int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
    return mx >= 150 && (mx - mn) * 100 < 45 * mx;
}

static BOOL makeBanner(TEXPalette* tpl) {
    static u8 rgba[BANNER_TEX_W * BANNER_TEX_H * 4], shape[BANNER_TEX_W * BANNER_TEX_H];
    const int w = BANNER_TEX_W, h = BANNER_TEX_H;
    TEXDescriptor* strip = TEXGet(tpl, 0x1F);
    TEXDescriptor* cut = TEXGet(tpl, 0xB5);
    int x, y, x0 = w, x1 = -1;
    if (strip == NULL || cut == NULL || strip->textureHeader == NULL || cut->textureHeader == NULL ||
        strip->textureHeader->format != GX_TF_CMPR || strip->textureHeader->width != w ||
        strip->textureHeader->height != h || cut->textureHeader->format != GX_TF_I4 ||
        cut->textureHeader->width != w || cut->textureHeader->height != h) {
        return FALSE;
    }
    decodeCmpr((const u8*)strip->textureHeader->data, w, h, rgba);
    decodeI4((const u8*)cut->textureHeader->data, w, h, shape);
    /* the word's columns: where the pale fill is */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (shape[y * w + x] > 0x80 && bannerLetter(rgba + (y * w + x) * 4)) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
            }
        }
    }
    /* with its outline and shadow (a little more to the right) */
    x0 -= 4;
    x1 += 6;
    if (x1 < x0 || x0 < 14 || x1 > w - 16) {
        return FALSE;
    }
    for (y = 0; y < h; y++) {
        Median m;
        u8 paint[3];
        medianClear(&m);
        for (x = 10; x < w - 12; x++) {
            if ((x < x0 - 2 || x > x1 + 2) && shape[y * w + x] > 0x80) {
                medianAdd(&m, rgba + (y * w + x) * 4);
            }
        }
        if (!medianGet(&m, paint)) {
            continue;
        }
        for (x = x0; x <= x1; x++) {
            memcpy(rgba + (y * w + x) * 4, paint, 3);
        }
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u8* p = rgba + (y * w + x) * 4;
            u8* tile = sBanner.pixels + ((y / 4) * (w / 4) + x / 4) * 64;
            const int at = ((y & 3) * 4 + (x & 3)) * 2;
            hueShift(p, sSettingsTab.hue);
            tile[at] = shape[y * w + x];
            tile[at + 1] = p[0];
            tile[32 + at] = p[1];
            tile[32 + at + 1] = p[2];
        }
    }
    GXInitTexObj(&sBanner.obj, sBanner.pixels, w, h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&sBanner.obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    return TRUE;
}

static BOOL bannerReady(WindowWork* wp) {
    TEXPalette* tpl = (TEXPalette*)*wp->res->data;
    TEXDescriptor* strip = TEXGet(tpl, 0x1F);
    const void* from = strip != NULL && strip->textureHeader != NULL ? strip->textureHeader->data : NULL;
    if (from != sBanner.from) {
        sBanner.from = from;
        sBanner.ready = from != NULL && makeBanner(tpl);
    }
    return sBanner.ready;
}

/* A category's picture: the game's own icons (by their ids in every
 * release, icon_ids.h), one or two of them composed. */
typedef struct Picture {
    s32 icon[2];
    f32 dx[2], dy[2], scale[2];
} Picture;

static Picture picture(const Row* r) {
    static const struct {
        const char* tab;
        Picture p;
    } kPictures[] = {
        {"Gameplay", {{ICON_146, -1}, {0}, {0}, {0.8f}}},     /* Mario's boots */
        {"Enhancements", {{ICON_102, -1}, {0}, {0}, {0.8f}}}, /* a Shine Sprite */
        {"Display", {{ICON_10B, -1}, {0}, {0}, {0.8f}}},      /* the Mailbox SP: a screen */
        {"Graphics", {{ICON_01B, -1}, {0}, {0}, {0.8f}}},     /* a painted picture */
        {"Interface", {{ICON_19F, -1}, {0}, {0}, {0.8f}}},    /* the HUD's HP heart */
        {"Audio", {{ICON_016, -1}, {0}, {0}, {0.8f}}},        /* a music note */
        {"Controls", {{ICON_212, -1}, {0}, {0}, {0.52f}}},    /* the control pad */
        {"Multiplayer", {{ICON_19E, ICON_155}, {-6.0f, 7.0f}, {3.0f, -3.0f}, {0.62f, 0.62f}}}, /* Mario and Goombella */
        {"Accessibility", {{ICON_008, -1}, {0}, {0}, {0.66f}}}, /* two hearts: care */
        {"Language", {{ICON_117, -1}, {0}, {0}, {0.8f}}},      /* a book */
        {"Saves", {{ICON_134, -1}, {0}, {0}, {0.8f}}},         /* a written page */
        {"General", {{ICON_107, -1}, {0}, {0}, {0.8f}}},       /* a cog */
    };
    static const Picture kPhoto = {{ICON_03B, ICON_19E}, {0.0f, 0.0f}, {0.0f, 1.0f}, {0.8f, 0.4f}}; /* Mario in a frame */
    static const Picture kAll = {{ICON_191, -1}, {0}, {0}, {0.8f}};                                  /* the Strange Sack */
    static const Picture kRestart = {{ICON_034, -1}, {0}, {0}, {0.8f}};                              /* round arrows */
    size_t i;
    if (r->kind == KIND_PHOTO) {
        return kPhoto;
    }
    if (r->kind == KIND_ALL) {
        return kAll;
    }
    if (r->kind == KIND_RESTART) {
        return kRestart;
    }
    for (i = 0; i < sizeof(kPictures) / sizeof(kPictures[0]); i++) {
        if (strcmp(r->label, kPictures[i].tab) == 0) {
            return kPictures[i].p;
        }
    }
    return kPictures[sizeof(kPictures) / sizeof(kPictures[0]) - 1].p; /* (another: the cog) */
}

static void pictureDisp(const Row* r, f32 x, f32 y, f32 scale, GXColor c) {
    const Picture p = picture(r);
    int i;
    winIconInit();
    for (i = 0; i < 2 && p.icon[i] >= 0; i++) {
        const f32 s = p.scale[i] * scale;
        winIconSet(p.icon[i], (Vec){x + p.dx[i] * scale, y + p.dy[i] * scale, 0.0f}, (Vec){s, s, 1.0f}, c);
    }
}

/* a front page button at (x, y): as the Journal draws its entries, the
 * cursor's white and lifted over its shadow, the others grey; on it the
 * category's picture, and its name pale on a dark outline as the Journal's */
static void bannerDisp(WindowWork* wp, const Row* r, f32 x, f32 y, int selected) {
    const u8 k = selected ? 0xFF : 0x80;
    const GXColor c = {k, k, k, 0xFF};
    const GXColor dark = {(u8)(0x3A * k / 0xFF), (u8)(0x1C * k / 0xFF), (u8)(0x66 * k / 0xFF), 0xFF};
    const GXColor fill = {(u8)(0xF6 * k / 0xFF), (u8)(0xEA * k / 0xFF), (u8)(0xF4 * k / 0xFF), 0xFF};
    const char* name = portTrGame(r->label);
    const f32 room = BANNER_W - 54.0f;
    f32 full, sx;
    if (bannerReady(wp)) {
        if (selected) {
            tintedQuad(&sBanner.obj, x, y, BANNER_W, BANNER_TEX_H, (GXColor){0x00, 0x00, 0x00, 0x80});
        }
        if (selected) {
            x -= 8.0f;
            y += 8.0f;
        }
        tintedQuad(&sBanner.obj, x, y, BANNER_W, BANNER_TEX_H, c);
    } else if (selected) {
        x -= 8.0f;
        y += 8.0f;
    }
    pictureDisp(r, x - BANNER_W / 2.0f + 24.0f, y - 1.0f, 1.0f, c);
    /* the name in the rest, in its middle; narrower letters when long */
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.8f > room ? room / full : 0.8f;
    outlinedText(x - BANNER_W / 2.0f + 44.0f + (room - full * sx) / 2.0f, y + 10.0f, sx, 0.8f, dark, fill, name);
}

/* the Journal's book's pieces (win.tpl 0x26-0x34), each over w x h from its
 * top left, as winBookGX lays them */
static void bookTex(WindowWork* wp, s32 id, f32 x, f32 y, f32 w, f32 h) {
    GXTexObj obj;
    Mtx m;
    TEXGetGXTexObjFromPalette((TEXPalette*)*wp->res->data, &obj, id);
    GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    PSMTXScale(m, w / GXGetTexObjWidth(&obj), h / GXGetTexObjHeight(&obj), 1.0f);
    GXLoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x, y, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(x, y - h, 0.0f);
    GXTexCoord2f32(0.0f, 1.0f);
    GXPosition3f32(x + w, y - h, 0.0f);
    GXTexCoord2f32(1.0f, 1.0f);
    GXPosition3f32(x + w, y, 0.0f);
    GXTexCoord2f32(1.0f, 0.0f);
}

/* The Tattle log's open book (winBookGX), w wide with pages h tall, its top
 * left at (x, y); one of its entries' torn paper slips under the heading */
static void bookGX(WindowWork* wp, f32 x, f32 y, f32 w, f32 h) {
    const f32 p = (w - 80.0f) / 2.0f; /* each page's edge, either side of the spine's 48 */
    const f32 yb = y - 8.0f - h;
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZCompLoc(GX_FALSE);
    GXSetAlphaCompare(GX_GEQUAL, 0x80, GX_AOP_OR, GX_NEVER, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXLoadPosMtxImm(camGetPtr(CAMERA_2D)->view, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    bookTex(wp, 0x33, x + 8.0f, y - 8.0f, w - 16.0f, h);
    bookTex(wp, 0x28, x, y, 16.0f, 8.0f);
    bookTex(wp, 0x2C, x + 16.0f, y, p, 8.0f);
    bookTex(wp, 0x26, x + 16.0f + p, y, 48.0f, 16.0f);
    bookTex(wp, 0x2D, x + 64.0f + p, y, p, 8.0f);
    bookTex(wp, 0x2A, x + 64.0f + 2.0f * p, y, 16.0f, 8.0f);
    bookTex(wp, 0x30, x, y - 8.0f, 8.0f, h);
    bookTex(wp, 0x32, x + 24.0f + p, y - 16.0f, 16.0f, h - 8.0f);
    bookTex(wp, 0x31, x + w - 8.0f, y - 8.0f, 8.0f, h);
    bookTex(wp, 0x29, x, yb, 16.0f, 16.0f);
    bookTex(wp, 0x2E, x + 16.0f, yb, p, 16.0f);
    bookTex(wp, 0x27, x + 16.0f + p, yb, 48.0f, 24.0f);
    bookTex(wp, 0x2F, x + 64.0f + p, yb, p, 16.0f);
    bookTex(wp, 0x2B, x + 64.0f + 2.0f * p, yb, 16.0f, 16.0f);
    bookTex(wp, 0x34, x + 16.0f, y - 12.0f, p, 32.0f);
}

/* a category's page on the book at (x, y): the category's picture and name
 * heading the left page, its rows' names there and their values on the right */
static void pageDisp(WindowWork* wp, const Row* rows, int count, s32 row, s32 top, f32 x, f32 y, int inside) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor heading = {0x3A, 0x1C, 0x66, 0xFF};
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    int i;
    bookGX(wp, x + BOOK_X, y + BOOK_TOP, BOOK_W, BOOK_PAGE_H);
    if (count > 0 && rows[0].kind == KIND_HEADING) {
        pictureDisp(&rows[0], x + BOOK_X + 40.0f, y + BOOK_TOP - 28.0f, 1.0f, white);
        winFontInit();
        fitText(x + BOOK_X + 62.0f, y + BOOK_TOP - 17.0f, 160.0f, 0.95f, heading, portTrGame(rows[0].label), 0);
    }
    winFontInit();
    for (i = top; i < count && i < top + VISIBLE; i++) {
        const Row* r = &rows[i];
        const f32 ry = y + ROW_TOP - (i - top) * ROW_STEP;
        const GXColor c = !inside || i == row ? black : grey;
        char value[64];
        if (r->kind == KIND_HEADING) {
            continue;
        }
        if (r->kind == KIND_SUBHEAD) {
            fitText(x + ROW_X - 8.0f, ry, BOOK_W / 2.0f - 36.0f, 0.75f, heading, rowLabel(r), 0);
            continue;
        }
        fitText(x + ROW_X, ry, BOOK_W / 2.0f - 44.0f, 0.8f,
                (r->kind == KIND_FORM && (r->f.flags & PORT_PAUSE_ROW_DIM)) ? grey : c, rowLabel(r), 0);
        valueText(r, value, sizeof(value));
        if (value[0] != '\0') {
            fitText(x + VALUE_X, ry, BOOK_W / 2.0f - 46.0f, 0.8f, c, value, 1);
        }
    }
    /* where the view is in the page, as the Journal's logs count theirs */
    if (count - 1 > VISIBLE) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)row, count - 1);
        fitText(x + VALUE_X, y + BOOK_TOP - 20.0f, 80.0f, 0.7f, grey, pos, 1);
    }
}

void portPauseSettingsDisp(s32 camId, WindowWork* wp, s32 idx) {
    static u32 sStepped;
    const f32 x = wp->menu[idx].x;
    const f32 y = wp->menu[idx].y;
    const int inside = wp->active == PORT_PAUSE_SETTINGS_TAB + 11;
    int i;
    (void)camId;

    /* the slide between the pages, once a frame, at the menu's speed (port/include/port/menu_speed.h) */
    if (sStepped != gp->retraceCount) {
        f32 d = (f32)portMenuFrames(FPS_FRAMES(8));
        sStepped = gp->retraceCount;
        d = d < 1.0f ? 1.0f : d;
        sSlide += (sSlideTo - sSlide) / d;
        if (fabsf(sSlideTo - sSlide) < 0.002f) {
            sSlide = sSlideTo;
        }
    }

    winBgGX(wp, PORT_PAUSE_SETTINGS_TAB, x, y);
    if (sBindings) { /* the Bindings page (pause_bindings_aurora.c): on the page's panel */
        winKirinukiGX(wp, 0, x - 270.0f, y + 144.0f, 540.0f, 270.0f);
        portPauseBindingsDisp(wp, x, y, inside);
        return;
    }
    if (portPausePage() != NULL) {
        portPausePageDisp(wp, x, y);
        return;
    }
    if (sSlide < 1.0f) { /* the buttons: down behind the help box, the book over them, as it comes up */
        for (i = 0; i <= sFrontCount; i++) {
            /* (the cursor's last, over the others) */
            const s32 cur = sCat < 0 ? sRow : sFrontRow;
            const int k = i < sFrontCount ? i : (int)cur;
            f32 bx, by;
            if (k >= sFrontCount || (i < sFrontCount && k == cur)) {
                continue;
            }
            frontAt(k, sFrontCount, &bx, &by);
            by += (-160.0f - by) * sSlide;
            bannerDisp(wp, &sFront[k], x + bx, y + by, i == sFrontCount);
        }
    }
    if (sSlide > 0.0f) { /* the book: up from below, as the Journal's map comes */
        const f32 by = y - 480.0f * (1.0f - sSlide);
        if (sCat >= 0) {
            pageDisp(wp, sRows, sCount, sRow, sTop, x, by, inside);
        } else {
            pageDisp(wp, sOut, sOutCount, sOutRow, sOutTop, x, by, inside);
        }
    }
}

/* --- A form's page of its own (port/include/port/pause_forms.h), over the
 * Settings or the Wardrobe tab */

static const PortPausePage* sPage;

void portPauseOpenPage(const PortPausePage* page, int arg) {
    sPage = page;
    if (page != NULL && page->open != NULL) {
        page->open(arg);
    }
}

const PortPausePage* portPausePage(void) {
    return sPage;
}

int portPausePageMain(WindowWork* wp) {
    static char sShown[sizeof(sPageHelp)];
    static int sN;
    const PortPausePage* page = sPage;
    const char* help;
    int ret;
    if (page == NULL) {
        return -1;
    }
    ret = page->main(wp);
    if (ret < 0) {
        sPage = NULL;
        sConfirm = 0;
        return ret;
    }
    help = page->help != NULL ? page->help() : NULL;
    if (help != NULL) {
        static char tag[32]; /* (winMsgEntry keeps the tag's address) */
        if (strcmp(help, sShown) != 0) { /* (a tag a text: winMsgEntry keeps the text while the tag is the same) */
            snprintf(sShown, sizeof(sShown), "%s", help);
            snprintf(sPageHelp, sizeof(sPageHelp), "%s", help);
            sN++;
            portSpeechMenu(help);
        }
        snprintf(tag, sizeof(tag), "pc_menu_page%d", sN);
        winMsgEntry(wp, 0, tag, 0);
    }
    return 0;
}

void portPausePageDisp(WindowWork* wp, float x, float y) {
    if (sPage != NULL && sPage->disp != NULL) {
        sPage->disp(wp, x, y);
    }
}

const char* portPauseSettingsMsg(const char* tag) {
    if (strncmp(tag, "pc_menu_", 8) != 0) {
        return NULL;
    }
    if (strncmp(tag, "pc_menu_page", 12) == 0) {
        return sPageHelp; /* a form's page's (portPausePageMain) */
    }
    if (strncmp(tag, "pc_menu_wardrobe", 16) == 0) {
        return portPauseWardrobeMsg(tag);
    }
    if (portPauseBindingsHelp(tag) != NULL) {
        return portPauseBindingsHelp(tag);
    }
    if (strcmp(tag, "pc_menu_settings") == 0) {
        static char sTab[256];
        portPauseWrap(portTrGame("Change Paper Cut's settings here."), sTab, sizeof(sTab));
        return sTab;
    }
    if (strncmp(tag, "pc_menu_settings_", 17) == 0) {
        return sHelp; /* the selected row's (pointAt) */
    }
    return NULL;
}
