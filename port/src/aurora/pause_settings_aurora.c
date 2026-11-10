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
 * box. A category with pages (Enhancements: port/settings/settings_layout.cpp's PAGE lines) opens
 * on buttons of its own as the front page's, a page each, as the Modes page's hub does; B on one of
 * its pages goes back to them. After the settings come the features' Start menu forms
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
#include "port/text_fit.h"
#include "port/menu_crawl.h"
#include "port/pause_settings.h"
#include "port/bindings.h"
#include "port/photo.h"
#include "port/stats.h"
#include "port/rando_tracker.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/replay.h"
#include "port/pause_bindings.h"
#include "port/menu_speed.h"
#include "port/menu_tap.h"
#include "port/modes_hub.h"
#include "port/mode_leave.h"
#include "port/warp_map.h"
#include "port/companion_menu.h"
#include "port/pause_forms.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/title_menu.h"
#include "port/speech.h"
#include "port/tr.h"
#include "port/button_markup.h"
#include "port/view.h"
#include "port/versus.h"
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif
#include "port/map_edits.h"
#include "port/statesync.h"
#include "driver/seqdrv.h"
#include "driver/camdrv.h"
#include "icon_ids.h"
#include "data/item_data.h"
#include "mario/mario_pouch.h"
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
       KIND_BINDINGS = 8, KIND_FORM = 9, KIND_SUBHEAD = 10, KIND_SECTION = 11, KIND_VERSUS = 12, KIND_GROUP = 13,
       KIND_MAPEDIT = 14 /* the Map editor (port/settings/features/map_editor.cpp): the menu closed, the editor */,
       KIND_TOUCHEDIT = 15 /* the on-screen controls' layout editor (port/touch): the menu closed, the editor */,
       KIND_LEAVE = 16 /* "Leave <mode>" (port/include/port/mode_leave.h): the front page's first, A twice */ };
/* the Bindings page open (port/include/port/pause_bindings.h): it has the input and the panel */
static int sBindings;
#define MAX_ROWS 1024 /* every setting listed fits, with room for the features still coming */
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
    const char* tab;  /* KIND_SECTION: the full page's tab it opens; a Modes hub page's heading: its button */
    int group;        /* KIND_SUBHEAD: a group's heading (port/settings/settings_layout.cpp), not a form's;
                       * KIND_HEADING: a Modes hub button's page (port/include/port/modes_hub.h) */
    int held;         /* the title menu's (no file in play): one that works on a file, greyed (kNoFileNote) */
    int cat;          /* KIND_CATEGORY: the category it opens (sCats) */
} Row;

/* The title menu's scopes (port/include/port/title_menu.h): over the title with no file, the rows
 * that work on the file in play are greyed (PORT_SETTING_IN_GAME) and the Modes page is the title
 * menu's own; on its temporary file, the Modes hub shows the modes that need no file of the player's */
static const char kNoFileNote[] = "This one works on the file in play: load a file, then find it in the Start menu.";

static int titleNoFile(void) {
    return portTitleScope() == PORT_TITLE_NO_FILE;
}

/* a Modes page row (its section's label, its group) shown on the temporary file */
static int titleModeShown(const char* label, const char* group) {
    static const char* const kShown[] = {"Versus battle", "Raid boss",      "Quiz party",          "Tattle cards",
                                         "Dungeon master", "Boss rush",     "Battle puzzles", "Action-command dojo"};
    size_t i;
    if (portTitleScope() != PORT_TITLE_TEMP_FILE) {
        return 1;
    }
    if (group != NULL && strcmp(group, "Just for fun") == 0) {
        return 1; /* (switches: they go with whichever mode is played) */
    }
    for (i = 0; label != NULL && i < sizeof(kShown) / sizeof(kShown[0]); i++) {
        if (strcmp(label, kShown[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* the features' Start menu forms (portPauseAddForm) */
#define MAX_FORMS 128
static const PortPauseForm* sForms[MAX_FORMS];
static int sFormCount;
static const char* sMessage; /* a form's answer, in the help box until the cursor moves (English) */
static int sMessageN;        /* (each answer its own help tag) */
/* A start refused because another mode runs asks to leave it first (port/include/port/mode_leave.h): the ask,
 * already in the game's encoding (two names in a translated sentence), shown while sMessage points at it */
static char sSwitchAsk[256];

/* sMessage in the game's encoding */
static const char* messageText(void) {
    return sMessage == sSwitchAsk ? sSwitchAsk : portTrGame(sMessage);
}

void portPauseAddForm(const PortPauseForm* form) {
    if (sFormCount < MAX_FORMS) {
        sForms[sFormCount++] = form;
    }
}

/* a form's category: where its section is shown (port/settings/settings_layout.cpp) */
static const char* formTab(int f) {
    PortSettingsPlaceInfo p;
    portSettingsPlace(sForms[f]->tab, sForms[f]->section, &p);
    return p.tab;
}

static int selectable(int kind) {
    return kind != KIND_HEADING && kind != KIND_SUBHEAD;
}

/* a row's name for the game's text: a form's row's own text as it is */
static const char* rowLabel(const Row* r) {
    if (r->kind == KIND_GROUP) { /* a Modes hub button: its name on the hub */
        return portTrGame(portModesHubName(r->label));
    }
    return (r->kind == KIND_FORM || (r->kind == KIND_SUBHEAD && r->form >= 0)) && r->f.label == NULL
               ? r->f.text
               : portTrGame(r->label);
}

static Row sRows[MAX_ROWS];
static int sCount;
static s32 sRow, sTop;

/* A choice row whose name and value don't fit the line side by side (a long name with a long
 * value: "Battle music: <area>" with a boss theme, "Colour-blind filter" with "Deuteranopia
 * (green-blind)", in most languages): two lines, the name on its own line at full width and the
 * value under it, so neither is narrowed. The value shown decides (value index v; the row's own
 * when v < 0): a short one ("Off") sits on the name's line, at its right, as every other row's.
 * Measured once a language and value. */
static int rowSlotsFor(const Row* r, int v) {
    static struct {
        const char* label;
        const char* const* choices;
        int lang, v, slots;
    } cache[128];
    static int next;
    const int lang = portTrLanguage();
    f32 lw, vw;
    int i;
    if ((r->kind != 0 && r->kind != 1) || r->c.choices == NULL || r->c.count <= 0) {
        return 1;
    }
    if (v < 0) {
        v = r->c.value != NULL ? *r->c.value : 0;
    }
    if (v < 0 || v >= r->c.count) {
        return 1; /* (valueText's "?") */
    }
    for (i = 0; i < (int)(sizeof(cache) / sizeof(cache[0])); i++) {
        if (cache[i].label == r->label && cache[i].choices == r->c.choices && cache[i].lang == lang &&
            cache[i].v == v) {
            return cache[i].slots;
        }
    }
    lw = (f32)FontGetMessageWidth(rowLabel(r)) * 0.8f;
    vw = (f32)FontGetMessageWidth(portTrGame(r->c.choices[v])) * 0.8f;
    i = next++ % (int)(sizeof(cache) / sizeof(cache[0]));
    cache[i].label = r->label;
    cache[i].choices = r->c.choices;
    cache[i].lang = lang;
    cache[i].v = v;
    cache[i].slots = lw + vw > VALUE_X - ROW_X - 16.0f ? 2 : 1;
    return cache[i].slots;
}

static int rowSlots(const Row* r) {
    return rowSlotsFor(r, -1);
}

/* the lines rows a..b-1 take (a <= b) */
static int slotsBetween(const Row* rows, int a, int b) {
    int n = 0;
    for (; a < b; a++) {
        n += rowSlots(&rows[a]);
    }
    return n;
}

/* row i wholly in the view of VISIBLE lines from row top */
static int inView(const Row* rows, int top, int i) {
    return i >= top && slotsBetween(rows, top, i) + rowSlots(&rows[i]) <= VISIBLE;
}
static char sHelp[1024];
static char sPageHelp[512]; /* a form's page's help (portPausePageMain) */
static int sConfirm; /* the Restart now row's first A was pressed (2: a form's start asked to leave a mode first) */
static int sConfirmTap; /* that first A was a tap or a click */
static int sTapMoved;   /* this frame's tap moved the cursor to the row it acts on (pointer) */
#define MAX_CATS 48
static const char* sCats[MAX_CATS]; /* the categories: the settings page's tabs, in its order */
static int sCatParent[MAX_CATS];    /* a page's category (Enhancements for Field), or -1 */
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
/* The Modes page (port/include/port/modes_hub.h): a hub of picture buttons as the front page's, one
 * a group of the Modes tab, each opening its group's rows on the book. The hub's buttons come in
 * from the right as the front page's go out to the left (sSide: 0 the front page's, 1 the hub's). */
#define MAX_HUB 16
static int sGroup = -1;           /* on the Modes page: the button's page shown, or -1: the hub */
static const char* sHub[MAX_HUB]; /* the hub's buttons (each its group's English name), in the page's order */
static int sHubCount;
static s32 sGroupRow[MAX_HUB], sGroupTop[MAX_HUB]; /* each button's page's cursor and view */
static Row sHubFront[MAX_HUB];    /* the hub's buttons as last built (drawn while they slide) */
static int sHubFrontCount;
static s32 sHubFrontRow;
static f32 sSide, sSideTo;
#define HUB_SLIDE_X 640.0f

/* the category is the Modes page, with its hub */
static int isModes(int cat) {
    return cat >= 0 && cat < sCatCount && strcmp(sCats[cat], "Modes") == 0;
}

/* the category has pages (Enhancements): its own buttons, a page each */
static int hasPages(int cat) {
    int i;
    for (i = 0; cat >= 0 && i < sCatCount; i++) {
        if (sCatParent[i] == cat) {
            return 1;
        }
    }
    return 0;
}

/* the category is a page of another's (Field, of Enhancements): its index, or -1 */
static int parentCat(int cat) {
    return cat >= 0 && cat < sCatCount ? sCatParent[cat] : -1;
}

/* the hub shown (rather than a button's page): the Modes page's, or a category's with pages */
static int hubShown(void) {
    return (isModes(sCat) && sGroup < 0) || hasPages(sCat);
}

/* the hub's name over its buttons (hubTitleDisp): the Modes page's or the category's */
static const char* sHubTitle = "Modes";

/* buttons shown: the front page's or the hub's */
static int buttonsShown(void) {
    return sCat < 0 || hubShown();
}

static const char* const kRates[] = {"60", "120", "144", "240", "display"};
static const char* const kRateNames[] = {"60 Hz", "120 Hz", "144 Hz", "240 Hz", "The display's rate"};

float portPauseTabX(int n, int i) {
    return n >= 8 ? i * 70.0f - 245.0f : n >= 7 ? i * 78.0f - 234.0f : n >= 6 ? i * 90.0f - 225.0f : i * 105.0f - 205.0f;
}

float portPauseTabScale(int n) {
    return n >= 8 ? 0.7f : n >= 7 ? 0.76f : n >= 6 ? 0.86f : 1.0f;
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
 * their pages there), and the Wardrobe's (port/src/aurora/wardrobe_aurora.c).
 * (The Randomizer has a form here: port/settings/features/randomizer_menu.cpp. Modes
 * too: its switches, and the forms of the modes started from the Start menu, the
 * quiz party, bingo, Tattle cards, the Paper Tower... And Mods, the mod manager's:
 * port/settings/features/mods_manager.cpp.) */
static int toolTab(const char* tab) {
    static const char* const kTools[] = {"Cosmetics", "Practice", "Challenges", "Achievements", "Archipelago",
                                         "Streaming"};
    size_t i;
    for (i = 0; i < sizeof(kTools) / sizeof(kTools[0]); i++) {
        if (strcmp(tab, kTools[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* a form's rows on the page shown, under its section's name when it has more than one (or only a
 * note) and no heading of its own first */
#define OWN_ROW_ID (-0x0E1E) /* the one row of a device's own form online and in replays */
static const char kOwnNote[] = "This one is this device's own (its files, controllers or screen): it waits until the "
                               "online game or the replay is over.";

static int formOwnHeld(int f) {
    return (sForms[f]->flags & PORT_PAUSE_FORM_OWN) && portReplayActive();
}

static const char* formAct(int f, int id, int dir) {
    if (id == OWN_ROW_ID) {
        return dir == 0 ? kOwnNote : NULL;
    }
    {
        const char* said = sForms[f]->act(id, dir);
        if (dir == 0 && said != NULL) { /* (A on a row: its answer, shown in the help box, in the log too) */
            printf("[pause] %s: %s\n", sForms[f]->section != NULL ? sForms[f]->section : sForms[f]->tab, said);
            fflush(stdout);
        }
        return said;
    }
}

/* A form row's text cut short in its buffer (the game's text, a name, can carry tags) may end inside a
 * tag: the font's measuring and drawing read past a '<' with no '>' (a crash), so the open tag goes. */
static void cutOpenTag(char* text) {
    char* open = strrchr(text, '<');
    if (open != NULL && strchr(open, '>') == NULL) {
        *open = '\0';
    }
}

/* A form row's text or value that filled its buffer was cut there, mid-word or mid-number ("..., 18, 19, 2"):
 * it ends in "..." instead, after its last whole letter (a Shift-JIS letter's two bytes kept together; on the
 * Latin-1 releases an accented letter can pass for a first byte, which only ends it a letter sooner). */
static void cutShort(char* text, size_t size) {
    size_t i = 0, keep = 0;
    if (strlen(text) + 1 < size) {
        cutOpenTag(text);
        return;
    }
    cutOpenTag(text);
    while (text[i] != '\0') {
        const unsigned char c = (unsigned char)text[i];
        const size_t n = ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) && text[i + 1] != '\0' ? 2 : 1;
        if (i + n + 3 >= size) {
            break;
        }
        i += n;
        keep = i;
    }
    while (keep > 0 && (text[keep - 1] == ' ' || text[keep - 1] == ',')) {
        keep--; /* ("19..." rather than "19, ...") */
    }
    snprintf(text + keep, size - keep, "...");
}

static void addForm(int f) {
    static PortPauseRow got[48];
    int m, k;
    if (formOwnHeld(f)) {
        memset(&got[0], 0, sizeof(got[0]));
        got[0].label = "Not during online play or a replay";
        got[0].note = kOwnNote;
        got[0].id = OWN_ROW_ID;
        got[0].flags = PORT_PAUSE_ROW_DIM;
        m = 1;
    } else {
        m = sForms[f]->rows(got, 48);
    }
    if ((m > 1 || (m == 1 && (got[0].flags & PORT_PAUSE_ROW_DIM))) && !(got[0].flags & PORT_PAUSE_ROW_HEADING) &&
        sForms[f]->section != NULL && sCount < MAX_ROWS - 3) {
        Row* r = &sRows[sCount++];
        memset(r, 0, sizeof(*r));
        r->kind = KIND_SUBHEAD;
        r->label = sForms[f]->section;
        r->form = -1;
    }
    for (k = 0; k < m && sCount < MAX_ROWS - 3; k++) {
        Row* r = &sRows[sCount++];
        memset(r, 0, sizeof(*r));
        r->kind = (got[k].flags & PORT_PAUSE_ROW_HEADING) ? KIND_SUBHEAD : KIND_FORM;
        r->label = got[k].label != NULL ? got[k].label : "";
        r->c.note = got[k].note;
        r->form = f;
        r->f = got[k];
        cutShort(r->f.text, sizeof(r->f.text));
        cutShort(r->f.value, sizeof(r->f.value));
    }
}

/* Versus battle's row works here: in a room, not in a battle, and not on the second screen (its
 * menu is one player's alone, and the page's set-up and Fight! change the game) */
static int versusHere(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && !gPortCompanionMenu;
}

/* a page's row not shown in the Start menu: Photo mode (on the front page), and Versus battle where
 * it can't start */
static int pageRowHidden(const PortSettingsPageRow* p) {
    return (p->c.kind == 3 && (strcmp(p->c.label, "Photo mode") == 0 ||
                               (strcmp(p->c.label, "Versus battle") == 0 && !versusHere()))) ||
           /* (as buildRows leaves it out: a hub button whose only row it is isn't shown either) */
           (strcmp(p->c.label, "Map editor") == 0 && (!versusHere() || portReplayActive() || portStateSyncOn())) ||
           (p->c.tab != NULL && strcmp(p->c.tab, "Modes") == 0 && !titleModeShown(p->c.label, p->group));
}

/* a page row's hub button (port/include/port/modes_hub.h): its group's, or "More" with none */
static const char* hubButton(const PortSettingsPageRow* p) {
    return portModesHubButton(p->group != NULL ? p->group : "More");
}

/* the Modes hub's buttons from its page's rows, in their order: the buttons with a row shown */
static int hubButtons(const PortSettingsPageRow* page, int m, const char** out) {
    int k, i, n = 0;
    for (k = 0; k < m; k++) {
        const char* b = hubButton(&page[k]);
        int have = 0;
        if (pageRowHidden(&page[k])) {
            continue;
        }
        for (i = 0; i < n && !have; i++) {
            have = strcmp(out[i], b) == 0;
        }
        if (!have && n < MAX_HUB) {
            out[n++] = b;
        }
    }
    return n;
}

/* a Modes form with no section of the page's (a row to another tab's setting): the hub button of
 * the group its section is placed in (port/settings/settings_layout.cpp), or the last one */
static int formHubPage(int f) {
    PortSettingsPlaceInfo p;
    int i;
    portSettingsPlace(sForms[f]->tab, sForms[f]->section, &p);
    for (i = 0; i < sHubCount; i++) {
        if (strcmp(sHub[i], portModesHubButton(p.group != NULL ? p.group : "More")) == 0) {
            return i;
        }
    }
    return sHubCount - 1;
}

/* a category, once (a page's after its category: Field after Enhancements, which has no settings of its own) */
static void addCat(const char* tab) {
    const char* parent;
    int k, p = -1;
    for (k = 0; k < sCatCount; k++) {
        if (strcmp(sCats[k], tab) == 0) {
            return;
        }
    }
    parent = portSettingsTabParent(tab);
    if (parent != NULL) {
        addCat(parent);
        for (k = 0; k < sCatCount && p < 0; k++) {
            p = strcmp(sCats[k], parent) == 0 ? k : -1;
        }
    }
    if (sCatCount < MAX_CATS) {
        sCatParent[sCatCount] = p;
        sCats[sCatCount++] = tab;
    }
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
    /* which of the form's rows with that id (a list's text rows share id -1: the cursor stays on its own one,
       not the first, so it can go down past them) */
    int wasNth = 0, nth = 0, at = -1;
    const int n = portSettingsControls(controls, MAX_ROWS - 24);
    const int pending = portRestartSupported() && portRestartPending();
    int i, nextStart = 0;
    for (i = 0; was != NULL && wasKind == KIND_FORM && i < sRow; i++) {
        wasNth += sRows[i].kind == KIND_FORM && sRows[i].form == wasForm && sRows[i].f.id == wasId;
    }
    sCatCount = 0;
    for (i = 0; i < n; i++) {
        if (titleNoFile() && strcmp(controls[i].tab, "Modes") == 0) {
            continue; /* (over the title: the title menu's own Modes) */
        }
        if (!toolTab(controls[i].tab) && (sCatCount == 0 || strcmp(sCats[sCatCount - 1], controls[i].tab) != 0)) {
            addCat(controls[i].tab);
        }
    }
    /* and a tab with only forms (the Randomizer), after them */
    for (i = 0; i < sFormCount; i++) {
        if (!toolTab(formTab(i)) && !(titleNoFile() && strcmp(formTab(i), "Modes") == 0)) {
            addCat(formTab(i));
        }
    }
    if (sCat >= sCatCount) {
        sCat = -1;
    }
    sCount = 0;
    if (sCat < 0 && !titleNoFile() && !gPortCompanionMenu && portModeLeaveLabel() != NULL) {
        /* a mode runs: leaving it is the front page's first button (A twice) */
        memset(&sRows[sCount], 0, sizeof(Row));
        sRows[sCount].form = -1;
        sRows[sCount].kind = KIND_LEAVE;
        sRows[sCount].label = portModeLeaveLabel();
        sRows[sCount++].c.note = "Ends it as its own Stop does: the lent loadout given back and Mario back where it "
                                 "started. Press {A} twice.";
    }
    if (sCat < 0 || hasPages(sCat)) { /* the front page's buttons, or a category's pages' */
        for (i = 0; i < sCatCount; i++) {
            if (sCatParent[i] != sCat) {
                continue;
            }
            memset(&sRows[sCount], 0, sizeof(Row));
            sRows[sCount].kind = KIND_CATEGORY;
            sRows[sCount].label = sCats[i];
            sRows[sCount].form = -1;
            sRows[sCount++].cat = i;
        }
    } else {
        /* the category's rows in the page's order (port/settings/settings_layout.cpp), each group
         * under its heading: its plain controls, and for each of the full page's sections its form
         * (a heading over it when it has more than one row) or a row to the full page; then any
         * form of no section */
        static PortSettingsPageRow page[MAX_ROWS];
        const char* tab = sCats[sCat];
        int m = portSettingsPageRows(tab, page, MAX_ROWS - 24);
        const char* group = NULL;
        int done[MAX_FORMS] = {0};
        int k, f;
        /* the Modes page: its hub's buttons, a group each (a small one folded into another's), in
         * the page's order; on the hub only they are its rows, on a button's page only its groups' */
        const int modes = isModes(sCat);
        if (modes) {
            sHubCount = hubButtons(page, m, sHub);
            if (sGroup >= sHubCount) {
                sGroup = -1;
            }
        }
        if (modes && sGroup < 0) {
            for (i = 0; i < sHubCount; i++) {
                memset(&sRows[sCount], 0, sizeof(Row));
                sRows[sCount].kind = KIND_GROUP;
                sRows[sCount].form = -1;
                sRows[sCount++].label = sHub[i];
            }
            m = 0; /* (no rows of the page's own) */
        }
        if (modes && sGroup >= 0) { /* the button's name and picture heading its page */
            memset(&sRows[sCount], 0, sizeof(Row));
            sRows[sCount].kind = KIND_HEADING;
            sRows[sCount].label = portModesHubName(sHub[sGroup]);
            sRows[sCount].tab = sHub[sGroup];
            sRows[sCount].form = -1;
            sRows[sCount++].group = 1;
        } else if (!modes) {
            memset(&sRows[sCount], 0, sizeof(Row)); /* (not the last page's row's held, form or tab) */
            sRows[sCount].kind = KIND_HEADING;
            sRows[sCount].form = -1;
            sRows[sCount].group = 0;
            sRows[sCount++].label = tab;
        }
        for (k = 0; k < m && sCount < MAX_ROWS - 6; k++) {
            const PortSettingsPageRow* p = &page[k];
            const char* label = p->c.label;
            int any = 0;
            if (pageRowHidden(p)) {
                continue;
            }
            if (modes && strcmp(hubButton(p), sHub[sGroup]) != 0) {
                continue; /* (another button's) */
            }
            if (modes && p->group != NULL && strcmp(p->group, sHub[sGroup]) == 0) {
                group = p->group; /* (the button's own group: its name heads the page) */
            }
            if (p->group != NULL && (group == NULL || strcmp(p->group, group) != 0)) {
                Row* r = &sRows[sCount++];
                memset(r, 0, sizeof(*r));
                r->kind = KIND_SUBHEAD;
                r->label = p->group;
                r->form = -1;
                r->group = 1;
                group = p->group;
            }
            if (p->c.kind != 3) {
                if (strcmp(tab, "Display") == 0 && strcmp(label, "VSync") == 0) { /* (a string: its own row) */
                    memset(&sRows[sCount], 0, sizeof(Row));
                    sRows[sCount].form = -1;
                    sRows[sCount].kind = KIND_RATE;
                    sRows[sCount].label = "Frame rate";
                    sRows[sCount++].c.note = "Extra frames between the game's own 60: smoother motion.";
                }
                nextStart |= (p->c.flags & PORT_SETTING_NEXT_START) != 0;
                memset(&sRows[sCount], 0, sizeof(Row));
                sRows[sCount].form = -1;
                sRows[sCount].kind = p->c.kind;
                sRows[sCount].label = label;
                sRows[sCount].held = titleNoFile() && (p->c.flags & PORT_SETTING_IN_GAME);
                sRows[sCount++].c = p->c;
                continue;
            }
            if (strcmp(tab, "Controls") == 0 && strcmp(label, "Bindings") == 0) {
                /* the Bindings page's row (pause_bindings_aurora.c) */
                memset(&sRows[sCount], 0, sizeof(Row));
                sRows[sCount].form = -1;
                sRows[sCount].kind = KIND_BINDINGS;
                sRows[sCount].label = "Bindings...";
                sRows[sCount++].c.note = "Every key and controller button: the game's, and Paper Cut's own shortcuts.";
                continue;
            }
            if (strcmp(label, "Versus battle") == 0) { /* (port/include/port/versus.h) */
                memset(&sRows[sCount], 0, sizeof(Row));
                sRows[sCount].form = -1;
                sRows[sCount].kind = KIND_VERSUS;
                sRows[sCount].label = "Versus battle";
                sRows[sCount++].c.note = "Mario and his partners against Luigi and his, two players on one screen: "
                                         "set up both sides, then fight in the Glitz Pit.";
                continue;
            }
#ifdef PORT_TOUCH_OVERLAY
            if (strcmp(label, "Move the on-screen controls") == 0) { /* (port/include/port/touch_overlay.h) */
                memset(&sRows[sCount], 0, sizeof(Row));
                sRows[sCount].form = -1;
                sRows[sCount].kind = KIND_TOUCHEDIT;
                sRows[sCount].label = label;
                sRows[sCount++].c.note = "Closes the menu: over the paused game, drag the on-screen controls where you "
                                         "like and size them.";
                continue;
            }
#endif
            if (strcmp(label, "Map editor") == 0) { /* (port/include/port/map_edits.h) */
                if (!versusHere() || portReplayActive() || portStateSyncOn()) {
                    continue; /* (in the field, not online or in a replay) */
                }
                memset(&sRows[sCount], 0, sizeof(Row));
                sRows[sCount].form = -1;
                sRows[sCount].kind = KIND_MAPEDIT;
                sRows[sCount].label = "Map editor";
                sRows[sCount++].c.note = "Closes the menu: place pipes, blocks, NPCs and doors in this room, and save "
                                         "them as a mod.";
                continue;
            }
            {
                const int from = sCount;
                for (f = 0; f < sFormCount; f++) {
                    if (!done[f] && sForms[f]->section != NULL && strcmp(sForms[f]->section, label) == 0 &&
                        strcmp(formTab(f), tab) == 0) {
                        done[f] = any = 1;
                        addForm(f);
                    }
                }
                if (!any) {
                    Row* r = &sRows[sCount++];
                    memset(r, 0, sizeof(*r));
                    r->kind = KIND_SECTION;
                    r->label = label;
                    r->tab = tab;
                    r->form = -1;
                    r->c.note = "Opens Paper Cut's full settings page on this category: this one isn't in the Start menu.";
                }
                if (titleNoFile() && (p->c.flags & PORT_SETTING_IN_GAME) && strcmp(label, "Mods") != 0) {
                    /* (the title menu: no file to work on; the mod manager's needs none) */
                    int k2;
                    for (k2 = from; k2 < sCount; k2++) {
                        sRows[k2].held = sRows[k2].kind != KIND_SUBHEAD;
                    }
                }
            }
        }
        for (f = 0; f < sFormCount && sCount < MAX_ROWS - 6; f++) {
            if (!done[f] && strcmp(formTab(f), tab) == 0 && (!modes || formHubPage(f) == sGroup) &&
                (!modes || titleModeShown(sForms[f]->section, NULL))) {
                addForm(f);
            }
        }
    }
    if (pending && (sCat < 0 || nextStart)) {
        memset(&sRows[sCount], 0, sizeof(Row));
        sRows[sCount].form = -1;
        sRows[sCount].kind = KIND_RESTART;
        sRows[sCount].label = "Restart now";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Press {A} twice to restart; quick resume brings you back.";
    }
    if (sCat < 0 && portPhotoAvailable()) { /* (port/settings/features/photo_mode.cpp) */
        memset(&sRows[sCount], 0, sizeof(Row)); /* (not the last page's row's held, form or tab) */
        sRows[sCount].form = -1;
        sRows[sCount].kind = KIND_PHOTO;
        sRows[sCount].label = "Photo mode";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Closes the menu and holds the game: the sticks move the camera, {OK:Return} takes a picture, "
                                 "{NORTH:H} hides the HUD, {BACK:Escape} goes back.";
    }
    if (sCat < 0) {
        memset(&sRows[sCount], 0, sizeof(Row));
        sRows[sCount].form = -1;
        sRows[sCount].kind = KIND_ALL;
        sRows[sCount].label = "Full settings page...";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Paper Cut's full settings page, over the game: every setting, and remapping, mods "
                                 "and practice.";
    }
    for (i = 0; was != NULL && i < sCount; i++) {
        if (sRows[i].kind == wasKind &&
            (wasKind == KIND_FORM ? sRows[i].form == wasForm && sRows[i].f.id == wasId : sRows[i].label == was)) {
            at = i;
            if (wasKind != KIND_FORM || nth++ == wasNth) {
                break;
            }
        }
    }
    if (at >= 0) { /* (the n-th such row, else the last of them when fewer are left) */
        sRow = at;
        return; /* (the same row: a first A on it, Restart now's or a form row's, still waits for the second) */
    }
    sConfirm = 0; /* (the row asking is gone: Restart now once nothing is pending) */
    if (sRow >= sCount || !selectable(sRows[sRow].kind)) {
        sRow = buttonsShown() ? 0 : 1;
        while (sRow + 1 < sCount && !selectable(sRows[sRow].kind)) {
            sRow++;
        }
    }
}

static void build(void) {
    buildRows();
    if (hubShown()) {
        sHubTitle = isModes(sCat) ? "Modes" : sCats[sCat];
    }
    if (sCat < 0) { /* (kept for the slide) */
        sFrontCount = sCount < (int)(sizeof(sFront) / sizeof(sFront[0])) ? sCount : (int)(sizeof(sFront) / sizeof(sFront[0]));
        memcpy(sFront, sRows, sFrontCount * sizeof(Row));
        sFrontRow = sRow;
    } else if (hubShown()) {
        sHubFrontCount = sCount < MAX_HUB ? sCount : MAX_HUB;
        memcpy(sHubFront, sRows, sHubFrontCount * sizeof(Row));
        sHubFrontRow = sRow;
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

/* hub: a hub's buttons (Modes, Enhancements), under its title: the first row's middle at HUB_TOP, clear
 * of the title with the cursor's lift, the rows closer when they'd reach the front page's last row */
#define HUB_TOP 58.0f
static void frontAt(int k, int n, int hub, f32* x, f32* y) {
    const int rows = frontRows(n);
    f32 step = frontStep(n), top;
    if (hub && rows > 1 && step * (rows - 1) > HUB_TOP - (BANNER_MID - BANNER_SPAN / 2.0f)) {
        step = (HUB_TOP - (BANNER_MID - BANNER_SPAN / 2.0f)) / (rows - 1);
    }
    top = hub ? HUB_TOP : BANNER_MID + step * (rows - 1) / 2.0f;
    *x = k < rows ? -BANNER_COL_X : BANNER_COL_X;
    *y = top - step * (k % rows);
}

/* a category page's row i (1 on: 0 is its heading): its text's top on the book */
static f32 pageRowY(int i) {
    return ROW_TOP - (i < sTop ? i - sTop : slotsBetween(sRows, sTop, i)) * ROW_STEP;
}

/* a category's help: the first few of its settings' names, in the game's encoding */
static void categoryNote(const char* tab, char* out, size_t size) {
    static PortSettingsControl controls[MAX_ROWS];
    const int n = portSettingsControls(controls, MAX_ROWS - 24);
    const int jp = strcmp(PORT_GAME_ID, "G8MJ01") == 0;
    int i, shown = 0, more = 0;
    size_t len = 0;
    out[0] = '\0';
    if (strcmp(tab, "Modes") == 0 || strncmp(tab, "hub:", 4) == 0) { /* the hub's buttons; "hub:<button>": its rows */
        static PortSettingsPageRow page[MAX_ROWS];
        static const char* buttons[MAX_HUB];
        const int m = portSettingsPageRows("Modes", page, MAX_ROWS - 24);
        const int hub = tab[0] == 'M';
        const int count = hub ? hubButtons(page, m, buttons) : m;
        for (i = 0; i < count; i++) {
            const char* name;
            if (hub) {
                name = portModesHubName(buttons[i]);
            } else if (pageRowHidden(&page[i]) || strcmp(hubButton(&page[i]), tab + 4) != 0) {
                continue;
            } else {
                name = page[i].c.label;
            }
            if (shown >= 3) {
                more = 1;
                break;
            }
            if (len < size) {
                len += snprintf(out + len, size - len, "%s%s", shown > 0 ? (jp ? "\x81\x45" : ", ") : "",
                                portTrGame(name));
            }
            shown++;
        }
        if (more && len < size) {
            snprintf(out + len, size - len, "%s", jp ? "\x81\x44\x81\x44\x81\x44" : "...");
        }
        return;
    }
    if (strcmp(tab, "Display") == 0) {
        len += snprintf(out, size, "%s", portTrGame("Frame rate"));
        shown++;
    } else if (strcmp(tab, "Remake") == 0) { /* (its one switch first: enhancement_presets.cpp) */
        len += snprintf(out, size, "%s", portTrGame("Play it like the Switch remake"));
        shown++;
    }
    for (i = 0; i < sCatCount; i++) { /* a category with pages (Enhancements): their names */
        if (sCatParent[i] < 0 || strcmp(sCats[sCatParent[i]], tab) != 0) {
            continue;
        }
        if (shown >= 4) {
            more = 1;
            break;
        }
        if (len < size) {
            len += snprintf(out + len, size - len, "%s%s", shown > 0 ? (jp ? "\x81\x45" : ", ") : "",
                            portTrGame(sCats[i]));
        }
        shown++;
    }
    for (i = 0; i < n && !more; i++) {
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

/* a button's help: what's in its page (a Modes hub button's, its rows) */
static void buttonNote(const Row* r, char* out, size_t size) {
    char key[96];
    if (r->kind == KIND_GROUP) {
        snprintf(key, sizeof(key), "hub:%s", r->label);
        categoryNote(key, out, size);
    } else {
        categoryNote(r->label, out, size);
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
int portConsoleAccurateLockedValue(unsigned flags, const int* value); /* console_accurate.cpp */

static int change(const Row* r, int dir) {
    PortSettings* s = portSettings();
    const PortSettingsControl* c = &r->c;
    int v;
    if ((r->kind == 0 || r->kind == 1 || r->kind == 2) && portConsoleAccurateLockedValue(c->flags, c->value)) {
        /* console-accurate mode keeps it at the original's: said, with the buzzer (it was silent) */
        sMessage = "Console-accurate mode is on: rules that change play stay as the original's.";
        sMessageN++;
        psndSFXOn((const char*)0x20014);
        return 0;
    }
    if ((r->kind == 0 || r->kind == 1 || r->kind == 2) && portSettingWhyLocked(c->value) != NULL) {
        sMessage = portSettingWhyLocked(c->value); /* (Play as with Brothers mode on) */
        sMessageN++;
        psndSFXOn((const char*)0x20014);
        return 0;
    }
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
    size_t lines = 1, n, lastStart = 0, prevStart = 0;
    portPauseWrapCols(text, out, size, HELP_COLS);
    /* The box shows two lines a page. An odd count past two left a last page of one line alone ("now."
       under the box's arrows, the note looking cut off): that page shows the two last lines instead,
       the one before the last again above it, so every page reads as a whole. */
    for (n = 0; out[n] != '\0'; n++) {
        if (out[n] == '\n' && out[n + 1] != '\0') {
            lines++;
            prevStart = lastStart;
            lastStart = n + 1;
        }
    }
    if (lines > 2 && lines % 2 == 1 && n + (lastStart - prevStart) + 1 < size) {
        const size_t dup = lastStart - prevStart; /* the line before the last, with its break */
        memmove(out + lastStart + dup, out + lastStart, n - lastStart + 1);
        memcpy(out + lastStart, out + prevStart, dup);
    }
}

void portPauseWrapCols(const char* text, char* out, size_t size, int cols) {
    const size_t maxCols = (size_t)cols;
    size_t n = 0, line = 0, lineStart = 0;
    while (*text != '\0' && n + 4 < size) {
        const char* end = text;
        size_t width = 0, sep;
        if (*text == '\n') { /* a break the text has itself */
            out[n++] = '\n';
            line = 0;
            lineStart = n;
            text++;
            continue;
        }
        if ((sep = separator(text)) != 0) {
            const int two = sep == 2;
            text += two ? 2 : 1;
            if (line > 0 && line + sep <= maxCols && *text != '\0' && *text != '\n') {
                out[n++] = two ? (char)0x81 : ' ';
                if (two) {
                    out[n++] = 0x40;
                }
                line += sep;
            }
            continue;
        }
        /* the word: up to the next separator or break; letters 1 or 2 columns */
        for (;;) {
            while (*end != '\0' && *end != '\n' && separator(end) == 0) {
                const int two = sjisLead((unsigned char)*end) && end[1] != '\0';
                int len, cols;
                if (*end == '{' && (cols = portButtonsGameColumns(end, &len)) >= 0) { /* a button: as it shows */
                    end += len;
                    width += (size_t)cols;
                    continue;
                }
                end += two ? 2 : 1;
                width += two ? 2 : 1;
                /* Japanese: a line may also end after ・ ： ） (a list with no spaces, "A・B・C",
                   breaks between its items, not inside one) */
                if (two && (unsigned char)end[-2] == 0x81 &&
                    ((unsigned char)end[-1] == 0x45 || (unsigned char)end[-1] == 0x46 ||
                     (unsigned char)end[-1] == 0x6A)) {
                    break;
                }
            }
            /* "{X}: reset": a label's colon keeps its next word on its line (no line ending in "X:") */
            if (end > text && end[-1] == ':' && *end == ' ' && end[1] != '\0' && end[1] != '\n' && end[1] != ' ') {
                end++;
                width++;
                continue;
            }
            break;
        }
        if (line > 0 && line + width <= maxCols) {
            /* the columns fit, the letters may not (wide ones, a translation's): the line with the word,
               measured in the game's font, kept inside about 15 a column (the help box's 450 at 30) */
            char probe[256];
            const size_t a = n - lineStart, b = (size_t)(end - text);
            size_t i;
            if (a + b < sizeof(probe)) {
                memcpy(probe, out + lineStart, a);
                for (i = 0; i < b; i++) {
                    probe[a + i] = text[i] == '<' ? '(' : text[i] == '>' ? ')' : text[i];
                }
                probe[a + b] = '\0';
                if ((f32)FontGetMessageWidth(probe) > (f32)maxCols * 15.0f) {
                    width = maxCols + 1; /* (a break before it, below) */
                }
            }
        }
        if (line > 0 && line + width > maxCols) {
            out[n++] = '\n';
            line = 0;
            lineStart = n;
        }
        while (text < end && n + 4 < size) {
            const int two = sjisLead((unsigned char)*text) && text[1] != '\0';
            int len, cols;
            if (*text == '{' && (cols = portButtonsGameColumns(text, &len)) >= 0 && n + (size_t)len + 4 < size) {
                memcpy(out + n, text, (size_t)len); /* a button whole */
                n += (size_t)len;
                text += len;
                line += (size_t)cols;
                continue;
            }
            if (line + (two ? 2 : 1) > maxCols) { /* a word longer than a line */
                out[n++] = '\n';
                line = 0;
                lineStart = n;
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

int portPauseWrapLines(const char* text, float w, float s, char lines[][PORT_PAUSE_WRAP_LINE], int max) {
    const char* p = text != NULL ? text : "";
    int count = 0;
    while (*p != '\0' && count < max) {
        char* line = lines[count];
        const int last = count == max - 1;
        size_t n = 0, brk = 0;
        const char* resume = NULL;
        line[0] = '\0';
        for (;;) {
            size_t len, sep;
            if (*p == '\0') {
                break;
            }
            if (*p == '\n' && !last) {
                p++;
                break;
            }
            len = sjisLead((unsigned char)*p) && p[1] != '\0' ? 2 : 1;
            if (n + len >= PORT_PAUSE_WRAP_LINE) {
                break; /* (a line this long: the rest on the next) */
            }
            if ((sep = separator(p)) != 0 && n > 0) {
                brk = n; /* the line can end before this space */
                resume = p + sep;
            }
            memcpy(line + n, p, len);
            if (*p == '\n') {
                line[n] = ' '; /* (the last line: its breaks as spaces, every word kept) */
            }
            line[n + len] = '\0';
            if (!last && n > 0 && (f32)FontGetMessageWidth(line) * s > w) {
                if (brk > 0) {
                    line[brk] = '\0';
                    p = resume;
                } else {
                    line[n] = '\0'; /* a word longer than the line: broken inside it */
                }
                break;
            }
            n += len;
            p += len;
        }
        count++;
    }
    return count;
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
    static char tags[MAX_CATS + 1 + MAX_HUB][TAG_ROWS][28];
    const Row* r = &sRows[sRow];
    const int page = isModes(sCat) && sGroup >= 0 ? MAX_CATS + 1 + sGroup : sCat + 1; /* (a hub button's its own) */
    char* tag = tags[page][sRow % TAG_ROWS];
    char text[1024];
    if (tag[0] == '\0') {
        snprintf(tag, sizeof(tags[0][0]), "pc_menu_settings_%d_%d", page, (int)(sRow % TAG_ROWS));
    }
    if (buttonsShown()) { /* as the Journal's: the hand at the button's left end */
        f32 bx, by;
        frontAt((int)sRow, sCount, hubShown(), &bx, &by);
        wp->helpTgtX = bx - BANNER_W / 2.0f - (r->kind == KIND_GROUP ? 30.0f : 16.0f); /* (clear of a hub button's sticker) */
        wp->helpTgtY = by + 8.0f;
    } else {
        wp->helpTgtX = ROW_X - 24.0f;
        wp->helpTgtY = pageRowY((int)sRow) - 8.0f;
    }
    if (sMessage != NULL) { /* a form's answer */
        static char tagm[32]; /* (winMsgEntry keeps the tag's address) */
        snprintf(tagm, sizeof(tagm), "pc_menu_settings_msg%d", sMessageN);
        portPauseWrap(messageText(), sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, tagm, 0);
        return;
    }
    if (r->kind == KIND_FORM && sConfirm) {
        /* (armed by a tap or a click: said as one, "{A}" means nothing to a finger) */
        portPauseWrap(portTrGame(sConfirmTap ? "Tap again to confirm." : "Press {A} again to confirm."), sHelp,
                      sizeof(sHelp));
        winMsgEntry(wp, 0, sConfirmTap ? "pc_menu_settings_confirm_tap" : "pc_menu_settings_confirm", 0);
        return;
    }
    if (r->kind == KIND_LEAVE && sConfirm) {
        portPauseWrap(portTrGame(sConfirmTap ? "Tap again to leave it." : "Press {A} again to leave it."), sHelp,
                      sizeof(sHelp));
        winMsgEntry(wp, 0, sConfirmTap ? "pc_menu_settings_leave_tap" : "pc_menu_settings_leave", 0);
        return;
    }
    if (r->kind == KIND_RESTART && sConfirm) {
        /* (a tag of its own: the text changes on the same row) */
        portPauseWrap(portTrGame("Press {A} again to restart."), sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, "pc_menu_settings_restart", 0);
        return;
    }
    if (r->kind == KIND_CATEGORY || r->kind == KIND_GROUP) {
        buttonNote(r, text, sizeof(text));
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
    while (sTop < sRow && !inView(sRows, sTop, sRow)) { /* (a two-line row: a line more) */
        sTop++;
    }
}

static void move(int dir) {
    int tries = 0;
    if (sCount <= 0) {
        return;
    }
    do {
        sRow = (sRow + dir + sCount) % sCount;
    } while (!selectable(sRows[sRow].kind) && ++tries < sCount); /* (a page with nothing to pick: once round) */
    if (!buttonsShown()) {
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
    if (kind == 0 || (sSlide > 0.05f && sSlide < 0.95f) || (sSide > 0.05f && sSide < 0.95f)) {
        return 0;
    }
    for (i = 0; i < sCount; i++) {
        if (!selectable(sRows[i].kind)) {
            continue;
        }
        if (buttonsShown()) {
            frontAt(i, sCount, hubShown(), &bx, &by);
            if (gx < bx - BANNER_W / 2.0f || gx > bx + BANNER_W / 2.0f || gy > by + 16.0f || gy <= by - 16.0f) {
                continue;
            }
        } else {
            const f32 y = pageRowY(i) - 8.0f; /* (the row's middle: the text hangs from its y) */
            if (!inView(sRows, sTop, i) || gx < ROW_X - 16.0f || gx > VALUE_X + 8.0f || gy > y + ROW_STEP / 2.0f ||
                gy <= y - ROW_STEP / 2.0f - (rowSlots(&sRows[i]) - 1) * ROW_STEP) {
                continue;
            }
        }
        if (i == sRow) {
            return kind == 1;
        }
        sRow = i;
        psndSFXOn((const char*)0x20005);
        if (kind == 1 && (sRows[i].kind == KIND_LEAVE ||
                          (sRows[i].kind == KIND_FORM &&
                           (sRows[i].f.flags & (PORT_PAUSE_ROW_CONFIRM | PORT_PAUSE_ROW_DIM)) != 0))) {
            /* a tap on a row that asks to be sure (a mode's start, Leave), or a dimmed one: as A at once, so the
               first tap asks (or says why not) and the second does it. Three taps once (point, ask, do): on a
               phone the first two looked like a button that did nothing. */
            sConfirm = 0;
            sTapMoved = 1;
            return 1;
        }
        return 0;
    }
    return 0;
}

/* to page cat (-1: the list), each page's cursor and view kept while the menu is open */
static void showPage(int cat) {
    const int toButtons = cat < 0 || isModes(cat) || hasPages(cat);
    if (toButtons && sCat >= 0 && !hubShown()) { /* (drawn as the book goes) */
        memcpy(sOut, sRows, sCount * sizeof(Row));
        sOutCount = sCount;
        sOutRow = sRow;
        sOutTop = sTop;
    }
    sPageRow[sCat + 1] = sRow;
    sPageTop[sCat + 1] = sTop;
    sCat = cat;
    sGroup = -1; /* (the Modes page opens on its hub) */
    sRow = sPageRow[cat + 1] >= 0 ? sPageRow[cat + 1] : toButtons ? 0 : 1;
    sTop = sPageTop[cat + 1];
    sCount = 0; /* (the cursor from this page's own, not the last one's row) */
    sConfirm = 0;
    /* the Modes page, or a category with pages: the hub's buttons in from the right, no book (a page of
     * the category's: the book over them); back: the front page's in from the left */
    sSlideTo = toButtons ? 0.0f : 1.0f;
    sSideTo = isModes(cat) || hasPages(cat) || parentCat(cat) >= 0 ? 1.0f : 0.0f;
    build();
    if (!buttonsShown()) {
        follow();
    }
}

/* on the Modes page: to hub button g's page (its book up, the hub's buttons down, as a category's
 * page from the front page), or back to the hub (-1); each page's cursor and view kept */
static void showGroup(int g) {
    if (g < 0 && sGroup >= 0) { /* (drawn as the book goes) */
        memcpy(sOut, sRows, sCount * sizeof(Row));
        sOutCount = sCount;
        sOutRow = sRow;
        sOutTop = sTop;
        sGroupRow[sGroup] = sRow;
        sGroupTop[sGroup] = sTop;
        sRow = sPageRow[sCat + 1] >= 0 ? sPageRow[sCat + 1] : 0;
    } else if (g >= 0) {
        sPageRow[sCat + 1] = sRow; /* (the hub's cursor) */
        sRow = sGroupRow[g] >= 0 ? sGroupRow[g] : 1;
        sTop = sGroupTop[g];
    }
    sGroup = g;
    sCount = 0;
    sConfirm = 0;
    sSlideTo = g < 0 ? 0.0f : 1.0f;
    build();
    if (g >= 0) {
        follow();
    }
}

/* The title menu's Modes (port/include/port/title_menu.h): the tab entered straight on the Modes hub,
 * its buttons already in place */
void portPauseSettingsOpenModes(void) {
    int i;
    build();
    for (i = 0; i < sCatCount; i++) {
        if (isModes(i)) {
            showPage(i);
            sSlide = sSlideTo;
            sSide = sSideTo;
            return;
        }
    }
}

/* A category's page opened from another tab's page (the Multiplayer page's "Multiplayer settings...",
 * pause_online_aurora.c): that page draws and runs this one in its place, and B on it gives -1 to that
 * page (its own list again, never this tab's front page) */
static int sHosted;

int portPauseSettingsOpenOn(WindowWork* wp, const char* cat) {
    int i;
    build();
    for (i = 0; i < sCatCount; i++) {
        if (strcmp(sCats[i], cat) == 0 && sCatParent[i] < 0) {
            sHosted = 1;
            sMessage = NULL;
            showPage(i);
            sSlide = sSlideTo;
            sSide = sSideTo;
            pointAt(wp);
            return 1;
        }
    }
    return 0;
}

void portPauseSettingsCloseHosted(void) {
    if (!sHosted) {
        return;
    }
    sHosted = 0;
    sMessage = NULL;
    sConfirm = 0;
    sBindings = 0;
    portPauseOpenPage(NULL, 0);
    showPage(-1); /* (the Settings tab opens on its front page as ever) */
    sSlide = sSlideTo;
    sSide = sSideTo;
}

void portPauseSettingsInit(WindowWork* wp) {
    int i;
    (void)wp;
    sHosted = 0;
    for (i = 0; i <= MAX_CATS; i++) {
        sPageRow[i] = -1;
        sPageTop[i] = 0;
    }
    for (i = 0; i < MAX_HUB; i++) {
        sGroupRow[i] = -1;
        sGroupTop[i] = 0;
    }
    sCat = -1;
    sGroup = -1;
    sSide = sSideTo = 0.0f;
    sBindings = 0;
    sCount = 0;
    sRow = 0;
    sTop = 0;
    sSlide = sSlideTo = 0.0f;
    build();
}

void portPauseSettingsInit2(WindowWork* wp) {
    /* tests: the row chosen first, by its label; "Category/Row": that category's page open on that row;
     * "Modes/Button/Row": the Modes hub's button's page (by its group's name or the button's) */
    const char* want = getenv("TTYD_PAUSE_ROW");
    const char* slash = want != NULL ? strchr(want, '/') : NULL;
    int i;
    wp->flags |= WIN_FLAG_SUBMENU;
    build();
    if (slash != NULL) { /* (any category: a page of one's, Field, too) */
        for (i = 0; i < sCatCount; i++) {
            if (strncmp(sCats[i], want, (size_t)(slash - want)) == 0 && sCats[i][slash - want] == 0) {
                showPage(i);
                sSlide = sSlideTo;
                sSide = sSideTo;
                break;
            }
        }
        want = slash + 1;
        slash = hasPages(sCat) ? strchr(want, '/') : NULL; /* "Enhancements/Field/Row" */
        for (i = 0; slash != NULL && i < sCount; i++) {
            if (sRows[i].kind == KIND_CATEGORY && strncmp(sRows[i].label, want, (size_t)(slash - want)) == 0 &&
                sRows[i].label[slash - want] == 0) {
                showPage(sRows[i].cat);
                sSlide = sSlideTo;
                want = slash + 1;
                break;
            }
        }
        slash = hubShown() && isModes(sCat) ? strchr(want, '/') : NULL;
        for (i = 0; slash != NULL && i < sCount; i++) {
            const char* name = portModesHubName(sRows[i].label);
            const size_t len = (size_t)(slash - want);
            if (sRows[i].kind == KIND_GROUP && ((strncmp(sRows[i].label, want, len) == 0 && sRows[i].label[len] == 0) ||
                                                (strncmp(name, want, len) == 0 && name[len] == 0))) {
                sRow = i;
                showGroup(i);
                sSlide = 1.0f;
                want = slash + 1;
                break;
            }
        }
    }
    for (i = 0; want != NULL && i < sCount; i++) {
        if (sRows[i].label != NULL && strcmp(sRows[i].label, want) == 0 && selectable(sRows[i].kind)) {
            sRow = i;
            sTop = i - 1 < 1 ? 1 : i - 1;
            if (!buttonsShown()) {
                follow();
            }
            break;
        }
    }
    pointAt(wp);
}

/* --- Tests: the tab walked through by itself ($TTYD_MENU_CRAWL, port/include/port/menu_crawl.h) --- */

enum { CRAWL_START, CRAWL_FRONT, CRAWL_PAGE, CRAWL_HUB, CRAWL_SUBPAGE, CRAWL_PAGES, CRAWL_DONE };
static struct {
    int phase, next;                        /* the step, and the one after a sub-page */
    int cats[MAX_CATS], catCount, cat;      /* the front page's categories (their rows), the one walked */
    const char* catNames[MAX_CATS];         /* (their English names) */
    char pageNames[MAX_CATS][48];           /* a category's page's: "Enhancements_Field" */
    int groups[MAX_HUB], groupCount, group; /* the Modes hub's buttons, the one walked (-1: the hub) */
    int row, wait, top, shoot, subs;        /* the row walked, frames to wait, the view's top shot, a shot due */
    int versus, bindings;                   /* the page has its row: walked after it */
    char name[96];
} sCw;

static void crawlName(const char* what) {
    char screen[112];
    snprintf(sCw.name, sizeof(sCw.name), "set_%s", what);
    snprintf(screen, sizeof(screen), "Settings/%s", what);
    portTextFitScreen(screen);
}

/* each of a choice row's values measured, as its value is drawn (pageDisp) */
static void crawlChoices(const Row* r) {
    int j;
    if ((r->kind != 0 && r->kind != 1) || r->c.choices == NULL) {
        return;
    }
    for (j = 0; j < r->c.count; j++) {
        /* (as pageDisp lays out the row: the name and the value in the row's room together; a two-line
           row's value alone on its line: below 80% of the room, its letters would be under 0.64) */
        const char* t = portTrGame(r->c.choices[j]);
        const f32 lw = (f32)FontGetMessageWidth(rowLabel(r)) * 0.8f;
        if (rowSlotsFor(r, j) > 1) {
            portTextFit("settings-value", t, (f32)FontGetMessageWidth(t) * 0.8f, VALUE_X - ROW_X - 40.0f, 1);
        } else {
            portTextFit("settings-value", t, lw + (f32)FontGetMessageWidth(t) * 0.8f, VALUE_X - ROW_X - 16.0f, 1);
        }
    }
}

/* the page shown (a category's or a hub button's), a row a step: 1 while going */
static int crawlRows(WindowWork* wp) {
    char shot[128];
    if (sCw.shoot) {
        snprintf(shot, sizeof(shot), "%s_%02d", sCw.name, (int)sTop);
        portMenuCrawlShot(shot);
        sCw.shoot = 0;
        sCw.wait = 8;
        return 1;
    }
    while (sCw.row < sCount && !selectable(sRows[sCw.row].kind)) {
        sCw.row++;
    }
    if (sCw.row >= sCount) {
        return 0;
    }
    sRow = sCw.row++;
    follow();
    pointAt(wp);
    crawlChoices(&sRows[sRow]);
    sCw.versus |= sRows[sRow].kind == KIND_VERSUS;
    sCw.bindings |= sRows[sRow].kind == KIND_BINDINGS;
    if (sCw.top < 0 || (sTop > sCw.top && slotsBetween(sRows, sCw.top, sTop) >= VISIBLE - 1) ||
        (sCw.row >= sCount && sTop != sCw.top)) { /* (a view a picture) */
        sCw.top = sTop;
        sCw.shoot = 1;
        sCw.wait = 16; /* (the help box and the band settled) */
    } else {
        sCw.wait = 3;
    }
    return 1;
}

/* a page with the pad (the versus set-up, the Bindings page) open: walked, then B; 1 once it's closed */
static int crawlSub(WindowWork* wp) {
    if (portPausePage() == NULL && !sBindings) {
        return 1;
    }
    if (!portMenuCrawlPad(wp, sCw.name, 24)) {
        wp->buttonTrg = PAD_BUTTON_B;
        if (++sCw.subs > 8) { /* (a page that keeps B for itself) */
            portPauseOpenPage(NULL, 0);
            sBindings = 0;
        }
    }
    return 0;
}

static void crawlPage(int cat, int group) {
    if (group < 0) {
        showPage(cat);
    } else {
        sRow = group;
        showGroup(group);
    }
    sSlide = sSlideTo;
    sSide = sSideTo;
    sCw.row = buttonsShown() ? 0 : 1;
    sCw.top = -1;
    sCw.shoot = 0;
    sCw.wait = 30;
    sCw.versus = sCw.bindings = 0;
}

/* on the Modes hub: its buttons, then each one's page */
static void crawlHub(void) {
    int i;
    sCw.groupCount = 0;
    for (i = 0; i < sCount && sCw.groupCount < MAX_HUB; i++) {
        if (sRows[i].kind == KIND_GROUP) {
            sCw.groups[sCw.groupCount++] = i;
        }
    }
    sCw.group = -1;
    sCw.row = 0;
    sCw.top = 0;
    sCw.wait = 30;
    sCw.phase = CRAWL_HUB;
}

/* a sub-page from the page walked: open it, walk it, then back to step next */
static int crawlOpenSub(WindowWork* wp, int bindings, int next) {
    snprintf(sCw.name, sizeof(sCw.name), bindings ? "bindings" : "versus_setup");
    portTextFitScreen(bindings ? "Bindings" : "Versus set-up");
    if (bindings) {
        sBindings = 1;
        portPauseBindingsOpen(wp);
    } else {
        portPauseOpenPage(&gPortVersusSetupPage, 0);
    }
    sCw.subs = 0;
    sCw.next = next;
    sCw.phase = CRAWL_SUBPAGE;
    return 1;
}

/* each frame of the tab: 1 when the crawl had it (the tab's own main skipped), 0 to go on to it (a
 * sub-page's, with the crawl's pad) */
static int crawlTick(WindowWork* wp) {
    int i;
    wp->buttonTrg = 0;
    wp->dirRep = 0;
    if (sCw.phase == CRAWL_SUBPAGE) {
        if (!crawlSub(wp)) {
            return 0;
        }
        sCw.phase = sCw.next;
        sCw.wait = 20;
        pointAt(wp);
        return 1;
    }
    build();
    if (sCw.wait > 0) {
        sCw.wait--;
        pointAt(wp);
        return 1;
    }
    switch (sCw.phase) {
        case CRAWL_START:
            if (sCat >= 0 && isModes(sCat)) { /* the title menu's Modes: its hub alone */
                sCw.catCount = 1;
                sCw.cats[0] = sCat;
                sCw.catNames[0] = "Modes";
                sCw.cat = 0;
                crawlName("Modes");
                crawlHub();
                printf("[crawl] modes: %d buttons\n", sCw.groupCount);
                return 1;
            }
            crawlPage(-1, -1);
            crawlName("front");
            sCw.catCount = 0;
            for (i = 0; i < sCount && sCw.catCount < MAX_CATS; i++) {
                if (sRows[i].kind == KIND_CATEGORY) {
                    const int cat = sRows[i].cat;
                    int k;
                    sCw.catNames[sCw.catCount] = sRows[i].label;
                    sCw.cats[sCw.catCount++] = cat;
                    for (k = 0; k < sCatCount && sCw.catCount < MAX_CATS; k++) { /* (its pages, after it) */
                        if (sCatParent[k] == cat) {
                            snprintf(sCw.pageNames[sCw.catCount], sizeof(sCw.pageNames[0]), "%s_%s", sCats[cat],
                                     sCats[k]);
                            sCw.catNames[sCw.catCount] = sCw.pageNames[sCw.catCount];
                            sCw.cats[sCw.catCount++] = k;
                        }
                    }
                }
            }
            sCw.cat = -1;
            sCw.top = 0;
            sCw.phase = CRAWL_FRONT;
            printf("[crawl] settings: %d categories\n", sCw.catCount);
            return 1;
        case CRAWL_FRONT: /* the buttons: one picture, each one's note */
            if (sCw.top == 0) {
                sCw.top = 1;
                portMenuCrawlShot("set_front");
                sCw.wait = 8;
                return 1;
            }
            if (sCw.row < sCount) {
                sRow = sCw.row++;
                pointAt(wp);
                sCw.wait = 3;
                return 1;
            }
            sCw.phase = CRAWL_PAGE;
            return 1;
        case CRAWL_PAGE:
            if (sCw.cat >= 0 && crawlRows(wp)) {
                return 1;
            }
            if (sCw.cat >= 0 && sCw.versus) {
                sCw.versus = 0;
                return crawlOpenSub(wp, 0, CRAWL_PAGE);
            }
            if (sCw.cat >= 0 && sCw.bindings) {
                sCw.bindings = 0;
                return crawlOpenSub(wp, 1, CRAWL_PAGE);
            }
            if (sCw.cat >= 0) {
                showPage(-1);
            }
            if (++sCw.cat >= sCw.catCount) {
                sCw.phase = CRAWL_DONE;
                return 1;
            }
            crawlPage(sCw.cats[sCw.cat], -1);
            crawlName(sCw.catNames[sCw.cat] != NULL ? sCw.catNames[sCw.cat] : "page");
            if (isModes(sCat)) { /* the hub: its buttons, then each one's page */
                crawlHub();
            } else if (hasPages(sCat)) { /* its pages' buttons (the pages: the categories after it) */
                sCw.top = 0;
                sCw.row = 0;
                sCw.phase = CRAWL_PAGES;
            }
            return 1;
        case CRAWL_PAGES: /* a category's pages' buttons: one picture, each one's note */
            if (sCw.top == 0) {
                char shot[96];
                sCw.top = 1;
                snprintf(shot, sizeof(shot), "set_%s_hub", sCats[sCat]);
                portMenuCrawlShot(shot);
                sCw.wait = 8;
                return 1;
            }
            if (sCw.row < sCount) {
                sRow = sCw.row++;
                pointAt(wp);
                sCw.wait = 3;
                return 1;
            }
            sCw.phase = CRAWL_PAGE; /* (on to the next category: its first page) */
            sCw.versus = sCw.bindings = 0;
            return 1;
        case CRAWL_HUB:
            if (sCw.group < 0) { /* the hub's buttons */
                if (sCw.top == 0) {
                    sCw.top = 1;
                    portMenuCrawlShot("set_Modes_hub");
                    sCw.wait = 8;
                    return 1;
                }
                if (sCw.row < sCount) {
                    sRow = sCw.row++;
                    pointAt(wp);
                    sCw.wait = 3;
                    return 1;
                }
            } else if (crawlRows(wp)) {
                return 1;
            }
            if (sCw.group >= 0 && sCw.versus) {
                sCw.versus = 0;
                return crawlOpenSub(wp, 0, CRAWL_HUB);
            }
            if (sCw.group >= 0) {
                showGroup(-1);
            }
            if (++sCw.group >= sCw.groupCount) {
                sCw.phase = CRAWL_PAGE;
                sCw.row = sCount; /* (the Modes page walked: on to the next category) */
                sCw.versus = sCw.bindings = 0;
                return 1;
            }
            {
                char what[96];
                const int g = sCw.groups[sCw.group];
                snprintf(what, sizeof(what), "Modes_%s", portModesHubName(sRows[g].label));
                crawlPage(sCat, g);
                crawlName(what);
            }
            return 1;
        default:
            portMenuCrawlDone();
            return 1;
    }
}

s32 portPauseSettingsMain(WindowWork* wp) {
    u32 wasTrg, wasRep;
    s32 wasRow;
    int tapped;
    if (portMenuCrawl() != NULL && strcmp(portMenuCrawl(), "settings") == 0 && crawlTick(wp)) {
        return 0; /* tests: walked through by itself */
    }
    if (portPausePage() == NULL && !sBindings && portWarpMapOpenIfRequested()) {
        return 0; /* the full settings page's warp map, asked for over this tab (port/include/port/warp_map.h) */
    }
    wasTrg = wp->buttonTrg;
    wasRep = wp->dirRep;
    wasRow = sRow;
    /* a form's page of its own (port/include/port/pause_forms.h) first: the Bindings page opens one
     * (a profile's name typed) over itself */
    if (portPausePage() != NULL) {
        return portPausePageMain(wp) == -2 ? -2 : 0; /* (-2: the whole menu closed, as for Photo mode) */
    }
    if (sBindings) {
        if (portPauseBindingsMain(wp) < 0) {
            sBindings = 0;
            pointAt(wp);
        }
        return 0;
    }
    build(); /* a feature's setting shown or hidden by another (widescreen's HUD) */
    if (wp->buttonTrg & PAD_BUTTON_B) {
        sMessage = NULL;
        psndSFXOn((const char*)0x20013);
        if (sCat < 0 || sHosted) {
            return -1; /* (opened from the Multiplayer page: back to it) */
        }
        if (hubShown() && portTitleScope() == PORT_TITLE_TEMP_FILE) {
            portTitleModesBack();
            return -2; /* the title menu's Modes: the whole menu closes, back to the title */
        }
        if (isModes(sCat) && sGroup >= 0) {
            showGroup(-1); /* a Modes hub button's page: back to the hub */
            pointAt(wp);
            return 0;
        }
        showPage(parentCat(sCat)); /* a category's page (or a hub): back to the list, or its category's pages */
        pointAt(wp);
        return 0;
    }
    if (!(wp->buttonTrg & PAD_BUTTON_A) && (wp->buttonTrg != 0 || wp->dirRep != 0)) {
        sConfirm = 0; /* anything else: Restart now asks again */
    }
    if (sRow < sCount && sRows[sRow].held &&
        ((wp->buttonTrg & PAD_BUTTON_A) || (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)))) {
        psndSFXOn((const char*)0x20014); /* (the game's buzzer, a badge with too little BP: no file to work on) */
        sMessage = kNoFileNote;
        sMessageN++;
        wp->buttonTrg &= ~PAD_BUTTON_A;
        wp->dirRep &= ~(DEMO_STICK_LEFT | DEMO_STICK_RIGHT);
    }
    sTapMoved = 0;
    tapped = pointer();
    if (tapped || (wp->buttonTrg & PAD_BUTTON_A)) {
        if (sRows[sRow].held) {
            /* (a click on it: as A above) */
            psndSFXOn((const char*)0x20014);
            sMessage = kNoFileNote;
            sMessageN++;
        } else if (sRows[sRow].kind == KIND_CATEGORY) {
            psndSFXOn((const char*)0x20012);
            showPage(sRows[sRow].cat);
        } else if (sRows[sRow].kind == KIND_GROUP) {
            psndSFXOn((const char*)0x20012);
            showGroup((int)sRow);
        } else if (sRows[sRow].kind == KIND_BINDINGS) {
            psndSFXOn((const char*)0x20012);
            sBindings = 1;
            portPauseBindingsOpen(wp);
            return 0;
        } else if (sRows[sRow].kind == KIND_ALL) {
            psndSFXOn((const char*)0x20012);
            portSettingsToggle();
        } else if (sRows[sRow].kind == KIND_VERSUS) {
            psndSFXOn((const char*)0x20012);
            portPauseOpenPage(&gPortVersusSetupPage, 0);
            return 0;
#ifdef PORT_TOUCH_OVERLAY
        } else if (sRows[sRow].kind == KIND_TOUCHEDIT) {
            psndSFXOn((const char*)0x20012);
            portTouchEditRequest();
            return -2; /* the whole menu closed: the editor a moment later */
#endif
        } else if (sRows[sRow].kind == KIND_MAPEDIT) {
            psndSFXOn((const char*)0x20012);
            portMapEditorRequest();
            return -2; /* the whole menu closed: the editor once Mario has the controls */
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
        } else if (sRows[sRow].kind == KIND_LEAVE) {
            psndSFXOn((const char*)0x20012);
            if (!sConfirm) {
                sConfirm = 1;
                sConfirmTap = tapped;
                sMessage = NULL;
            } else {
                const char* why = portModeLeaveRefusal();
                sConfirm = 0;
                if (why == NULL) {
                    portModeLeaveAsk(NULL, 0, NULL);
                    return -2; /* the whole menu closed: the mode's stop and the way back once it's shut */
                }
                psndSFXOn((const char*)0x20014);
                sMessage = why;
                sMessageN++;
            }
        } else if (sRows[sRow].kind == KIND_SECTION) { /* not in the Start menu: the full page on its tab */
            psndSFXOn((const char*)0x20012);
            portSettingsOpenAt(sRows[sRow].tab);
        } else if (sRows[sRow].kind == KIND_FORM) {
            const Row* r = &sRows[sRow];
            psndSFXOn((const char*)0x20012);
            if (sConfirm == 2) {
                /* yes to "leave <the mode running> and start this": the leave now, the start once it's over */
                const char* why = portModeLeaveRefusal();
                sConfirm = 0;
                if (why == NULL) {
                    portModeLeaveAsk(sForms[r->form]->act, r->f.id,
                                     sForms[r->form]->section != NULL ? sForms[r->form]->section : "a mode");
                    return -2; /* (the leave once the menu is shut, the start once the mode left is over) */
                }
                psndSFXOn((const char*)0x20014);
                sMessage = why;
                sMessageN++;
            } else if ((r->f.flags & PORT_PAUSE_ROW_CONFIRM) && !sConfirm) {
                sConfirm = 1;
                sConfirmTap = tapped;
                sMessage = NULL;
            } else {
                sConfirm = 0;
                sMessage = formAct(r->form, r->f.id, 0);
                sMessageN++;
                if (portModeAnotherRuns(sMessage) && portModeLeaveName() != NULL && !gPortCompanionMenu) {
                    /* another mode runs: ask to leave it and start this one instead of only refusing */
                    char leave[96], start[96];
                    snprintf(leave, sizeof(leave), "%s", portTrGame(portModeLeaveName()));
                    snprintf(start, sizeof(start), "%s",
                             portTrGame(sForms[r->form]->section != NULL ? sForms[r->form]->section : rowLabel(r)));
                    snprintf(sSwitchAsk, sizeof(sSwitchAsk),
                             portTrGame(tapped ? "Tap again to leave %s and start %s." : "Press {A} again to leave %s and start %s."),
                             leave, start);
                    printf("[pause] another mode runs: asked to leave %s and start %s\n", portModeLeaveName(),
                           sForms[r->form]->section != NULL ? sForms[r->form]->section : "a mode");
                    fflush(stdout);
                    sMessage = sSwitchAsk;
                    sConfirm = 2;
                    sConfirmTap = tapped;
                } else if (sMessage != NULL && strstr(sMessage, "once the menu is closed") != NULL) {
                    /* a mode started: the menu closes for it at once (before, it waited for the player to close
                       it, and a start read as a button that did nothing, on a phone above all) */
                    return -2;
                } else if (sMessage != NULL && portModeAnotherRuns(sMessage) == 0 && (r->f.flags & PORT_PAUSE_ROW_DIM)) {
                    psndSFXOn((const char*)0x20014); /* (a dimmed row's why: the game's buzzer with it) */
                }
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
            sMessage = formAct(sRows[sRow].form, sRows[sRow].f.id, dir);
            sMessageN++;
            psndSFXOn((const char*)0x20005);
        } else if (buttonsShown() ? across(dir) : change(&sRows[sRow], dir)) {
            psndSFXOn((const char*)0x20005);
        }
    }
    /* (a tap that moved to a row and acted on it at once: its answer or ask is this row's own, kept) */
    if ((sRow != wasRow && !sTapMoved) || (!(wasTrg & PAD_BUTTON_A) && wasTrg != 0) ||
        ((wasRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) != 0)) {
        if (!((wasRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) && sRows[sRow].kind == KIND_FORM)) {
            sMessage = NULL; /* the cursor moved on: the rows' own help again */
        }
    }
    if (sRow != wasRow && !sTapMoved) {
        sConfirm = 0;
    }
    if (!sConfirm) {
        sConfirmTap = 0;
    }
    pointAt(wp);
    if (sRow < sCount) { /* read aloud: the row, its value and note, when either changes */
        static char sSaid[600];
        char text[600], value[64], note[256];
        valueText(&sRows[sRow], value, sizeof(value));
        if (sMessage != NULL) {
            snprintf(note, sizeof(note), "%s", messageText());
        } else if (sRows[sRow].kind == KIND_CATEGORY || sRows[sRow].kind == KIND_GROUP) {
            buttonNote(&sRows[sRow], note, sizeof(note)); /* (what's in it) */
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
    const f32 textWas = portButtonsGameTextScale(s); /* (a {A} in it: centred on the letters at s, button_markup.h) */
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    portTextFit(right ? "settings-value" : "settings-label", text, full * s, w, 1);
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, portTextFitHeight(k, s), portTextFitHeight(k, s)}, c, "%s", text);
    portButtonsGameTextScale(textWas);
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
static PaintedTab sOnlineTab = {.hue = 180.0f}; /* the Journal's blue to orange */

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
        /* (letters no thinner than port/text_fit.h's PORT_TEXT_SQUEEZE: past it smaller, kept in the tab's
           middle) */
        const f32 sy = portTextFitHeight(sx, 0.95f * scale);
        portTextFit("tab-label", kLabel, full * 0.95f * scale, room, 1);
        tw = sx * full;
        outlinedText(x - tw / 2, y + 13.0f * scale - (0.95f * scale - sy) * 12.0f, sx, sy, shadow, fill, kLabel);
    }
}

/* the Settings tab's word: a short one of its own where the language's "Settings" is too long for the
 * painted tab (German's, Italian's), else "Settings" */
static const char* settingsTabWord(void) {
    const char* t = portTrGame("Settings tab");
    return strcmp(t, "Settings tab") == 0 ? portTrGame("Settings") : t;
}

void portPauseSettingsTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    tabDisp(wp, &sSettingsTab, settingsTabWord(), (GXColor){0x3A, 0x1C, 0x66, 0xFF},
            (GXColor){0xF6, 0xEA, 0xF4, 0xFF}, (GXColor){0x6E, 0x5A, 0xB4, 0xFF}, x, y, scale, c);
}

void portPauseWardrobeTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    tabDisp(wp, &sWardrobeTab, portTrGame("Wardrobe"), (GXColor){0x0E, 0x46, 0x40, 0xFF},
            (GXColor){0xEA, 0xF6, 0xF2, 0xFF}, (GXColor){0x2A, 0x9A, 0x8E, 0xFF}, x, y, scale, c);
}

void portPauseOnlineTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    tabDisp(wp, &sOnlineTab, portTrGame("Multiplayer"), (GXColor){0x5A, 0x28, 0x06, 0xFF},
            (GXColor){0xFA, 0xEE, 0xE0, 0xFF}, (GXColor){0xCE, 0x76, 0x2C, 0xFF}, x, y, scale, c);
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

typedef struct Banner {
    u8 pixels[BANNER_TEX_W * BANNER_TEX_H * 4] __attribute__((aligned(32))); /* GX RGBA8 */
    GXTexObj obj;
    const void* from;
    int ready;
} Banner;
/* the Settings tab's (violet), and the Journal's Photos entry's (its own blue: port/include/port/photo.h) */
static Banner sBanner, sJournalBanner;

/* a letter's pale paint on the Journal's buttons: bright and nearly grey */
static int bannerLetter(const u8* p) {
    const int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
    const int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
    return mx >= 150 && (mx - mn) * 100 < 45 * mx;
}

static BOOL makeBanner(Banner* banner, TEXPalette* tpl, float hue) {
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
            u8* tile = banner->pixels + ((y / 4) * (w / 4) + x / 4) * 64;
            const int at = ((y & 3) * 4 + (x & 3)) * 2;
            if (hue != 0.0f) {
                hueShift(p, hue);
            }
            tile[at] = shape[y * w + x];
            tile[at + 1] = p[0];
            tile[32 + at] = p[1];
            tile[32 + at + 1] = p[2];
        }
    }
    GXInitTexObj(&banner->obj, banner->pixels, w, h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&banner->obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    return TRUE;
}

static BOOL bannerMade(WindowWork* wp, Banner* banner, float hue) {
    TEXPalette* tpl = (TEXPalette*)*wp->res->data;
    TEXDescriptor* strip = TEXGet(tpl, 0x1F);
    const void* from = strip != NULL && strip->textureHeader != NULL ? strip->textureHeader->data : NULL;
    if (from != banner->from) {
        banner->from = from;
        banner->ready = from != NULL && makeBanner(banner, tpl, hue);
    }
    return banner->ready;
}

static BOOL bannerReady(WindowWork* wp) {
    return bannerMade(wp, &sBanner, sSettingsTab.hue);
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
    static const Picture kVersus = {{ICON_19E, ICON_19E}, {-7.0f, 7.0f}, {3.0f, -3.0f}, {0.62f, 0.62f}}; /* the brothers */
    /* the Remake page and Enhancements' pages: an item's own icon each */
    static const struct {
        const char* tab;
        int item, second; /* second: the game's icon beside it, or -1 */
    } kItemPictures[] = {
        {"Remake", ITEM_DIAMOND_STAR, -1},
        {"Field", ITEM_MAGICAL_MAP, -1},
        {"Battle", ITEM_POW_BLOCK, -1},
        {"Characters", ITEM_L_EMBLEM, ICON_155}, /* Luigi's emblem and Goombella */
        {"Items and badges", ITEM_MUSHROOM, -1},
        {"Story", ITEM_RAGGED_DIARY, -1},
        {"Lists", ITEM_OLD_LETTER, -1},
        {"Restorations", ITEM_ULTRA_STONE, -1},
        {"Paper Mario 64", ITEM_SHOOTING_STAR, -1},
    };
    size_t i;
    for (i = 0; (r->kind == KIND_CATEGORY || r->kind == KIND_HEADING) && !r->group &&
                i < sizeof(kItemPictures) / sizeof(kItemPictures[0]);
         i++) {
        if (strcmp(r->label, kItemPictures[i].tab) == 0) {
            Picture p = {{itemDataTable[kItemPictures[i].item].icon_id, kItemPictures[i].second}, {0}, {0}, {0.8f}};
            if (kItemPictures[i].second >= 0) { /* (two, as Multiplayer's) */
                p.dx[0] = -6.0f;
                p.dx[1] = 7.0f;
                p.dy[0] = 3.0f;
                p.dy[1] = -3.0f;
                p.scale[0] = p.scale[1] = 0.62f;
            }
            return p;
        }
    }
    if ((r->kind == KIND_CATEGORY || r->kind == KIND_HEADING) && strcmp(r->label, "Randomizer") == 0) { /* a Star Piece (the item's own icon) */
        Picture p = {{itemDataTable[ITEM_STAR_PIECE].icon_id, -1}, {0}, {0}, {0.8f}};
        return p;
    }
    if (((r->kind == KIND_CATEGORY || r->kind == KIND_HEADING) && strcmp(r->label, "Modes") == 0) ||
        r->kind == KIND_GROUP || r->kind == KIND_LEAVE ||
        (r->kind == KIND_HEADING && r->group)) { /* the Glitz Pit's Champ's Belt */
        Picture p = {{itemDataTable[ITEM_CHAMPS_BELT].icon_id, -1}, {0}, {0}, {0.8f}};
        return p;
    }
    if (r->kind == KIND_PHOTO || r->kind == KIND_MAPEDIT) {
        return kPhoto;
    }
    if (r->kind == KIND_VERSUS) {
        return kVersus;
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

/* a Modes hub button's picture (port/include/port/modes_hub.h): the recipe's sticker, or the game's
 * icon it is made from; FALSE: neither (the Modes page's own picture then) */
static BOOL hubPictureDisp(const Row* r, f32 x, f32 y, f32 scale, GXColor c) {
    const char* button = r->kind == KIND_GROUP ? r->label : r->kind == KIND_HEADING && r->group ? r->tab : NULL;
    GXTexObj obj;
    f32 w, h;
    int icon = -1;
    if (button == NULL) {
        return FALSE;
    }
    if (portModesHubPicture(button, &obj, &w, &h, &icon)) {
        tintedQuad(&obj, x, y, w * scale, h * scale, c);
        return TRUE;
    }
    if (icon >= 0) {
        winIconInit();
        winIconSet(icon, (Vec){x, y, 0.0f}, (Vec){0.8f * scale, 0.8f * scale, 1.0f}, c);
        return TRUE;
    }
    return FALSE;
}

static void pictureDisp(const Row* r, f32 x, f32 y, f32 scale, GXColor c) {
    const Picture p = picture(r);
    int i;
    if (hubPictureDisp(r, x, y, scale, c)) {
        return;
    }
    winIconInit();
    for (i = 0; i < 2 && p.icon[i] >= 0; i++) {
        const f32 s = p.scale[i] * scale;
        winIconSet(p.icon[i], (Vec){x + p.dx[i] * scale, y + p.dy[i] * scale, 0.0f}, (Vec){s, s, 1.0f}, c);
    }
}

/* a front page button at (x, y): as the Journal draws its entries, the
 * cursor's lifted over its shadow, the others a little shaded (lighter than the Journal's grey,
 * so every name reads at a glance); on it the
 * category's picture, and its name pale on a dark outline as the Journal's */
static void bannerDisp(WindowWork* wp, const Row* r, f32 x, f32 y, int selected) {
    const u8 k = selected ? 0xFF : 0xC8;
    const GXColor c = {k, k, k, 0xFF};
    const GXColor dark = {(u8)(0x3A * k / 0xFF), (u8)(0x1C * k / 0xFF), (u8)(0x66 * k / 0xFF), 0xFF};
    const GXColor fill = {(u8)(0xF6 * k / 0xFF), (u8)(0xEA * k / 0xFF), (u8)(0xF4 * k / 0xFF), 0xFF};
    const char* name = rowLabel(r);
    const int hub = r->kind == KIND_GROUP; /* (its sticker wider than an icon: the name a little right) */
    const f32 room = BANNER_W - (hub ? 68.0f : 58.0f);
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
    pictureDisp(r, x - BANNER_W / 2.0f + (hub ? 20.0f : 24.0f), y - 1.0f, 1.0f, c);
    /* the name in the rest, in its middle; narrower letters when long */
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.8f > room ? room / full : 0.8f;
    portTextFit("settings-button", name, full * 0.8f, room, 1);
    outlinedText(x - BANNER_W / 2.0f + (hub ? 58.0f : 48.0f) + (room - full * sx) / 2.0f, y + 10.0f, sx, 0.8f, dark,
                 fill, name);
}

/* The title menu's buttons (port/include/port/title_menu.h): a front page button as above, s times its
 * size (the whole of it: the banner, the lift, the icons and the word), with one or two of the game's
 * icons (-1: none) and a word, at alpha a (their drop-in) */
void portPauseTitleButton(WindowWork* wp, float x, float y, float s, const char* name, int icon, int icon2,
                          int selected, int a) {
    const u8 k = selected ? 0xFF : 0xC8;
    const GXColor c = {k, k, k, (u8)a};
    const GXColor dark = {(u8)(0x3A * k / 0xFF), (u8)(0x1C * k / 0xFF), (u8)(0x66 * k / 0xFF), (u8)a};
    const GXColor fill = {(u8)(0xF6 * k / 0xFF), (u8)(0xEA * k / 0xFF), (u8)(0xF4 * k / 0xFF), (u8)a};
    const f32 bw = BANNER_W * s, room = (BANNER_W - 54.0f) * s, lift = 8.0f * s;
    f32 full, sx;
    if (bannerReady(wp)) {
        if (selected) {
            tintedQuad(&sBanner.obj, x, y, bw, BANNER_TEX_H * s, (GXColor){0x00, 0x00, 0x00, (u8)(a / 2)});
            x -= lift;
            y += lift;
        }
        tintedQuad(&sBanner.obj, x, y, bw, BANNER_TEX_H * s, c);
    } else if (selected) {
        x -= lift;
        y += lift;
    }
    winIconInit();
    if (icon >= 0 && icon2 >= 0) { /* (two: side by side, a little smaller, as Multiplayer's) */
        winIconSet(icon, (Vec){x - bw / 2.0f + 18.0f * s, y + 2.0f * s, 0.0f}, (Vec){0.62f * s, 0.62f * s, 1.0f}, c);
        winIconSet(icon2, (Vec){x - bw / 2.0f + 31.0f * s, y - 4.0f * s, 0.0f}, (Vec){0.62f * s, 0.62f * s, 1.0f}, c);
    } else if (icon >= 0) {
        winIconSet(icon, (Vec){x - bw / 2.0f + 24.0f * s, y - 1.0f * s, 0.0f}, (Vec){0.8f * s, 0.8f * s, 1.0f}, c);
    }
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.8f * s > room ? room / full : 0.8f * s;
    portTextFit("title-button", name, full * 0.8f * s, room, 1);
    outlinedText(x - bw / 2.0f + 44.0f * s + (room - full * sx) / 2.0f, y + 10.0f * s, sx, 0.8f * s, dark, fill,
                 name);
}

/* The Journal's Photos entry (port/include/port/photo.h): its button as the Journal's own, made as the front
 * page's are but its blue kept (the Journal's 224 wide), Mario in a frame and its word on it as theirs */
void portPhotoJournalButton(WindowWork* wp, float x, float y, int selected) {
    const u8 k = selected ? 0xFF : 0x80;
    const GXColor c = {k, k, k, 0xFF};
    const GXColor dark = {(u8)(0x14 * k / 0xFF), (u8)(0x2C * k / 0xFF), (u8)(0x6E * k / 0xFF), 0xFF};
    const GXColor fill = {(u8)(0xF2 * k / 0xFF), (u8)(0xF4 * k / 0xFF), (u8)(0xFA * k / 0xFF), 0xFF};
    const char* name = portTrGame("Photos");
    const f32 w = (f32)BANNER_TEX_W, room = w - 64.0f;
    const Row photo = {.kind = KIND_PHOTO};
    f32 full, sx;
    if (bannerMade(wp, &sJournalBanner, 0.0f)) {
        if (selected) {
            tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, (GXColor){0x00, 0x00, 0x00, 0x80});
            x -= 8.0f;
            y += 8.0f;
        }
        tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, c);
    } else if (selected) {
        x -= 8.0f;
        y += 8.0f;
    }
    pictureDisp(&photo, x - w / 2.0f + 28.0f, y - 1.0f, 1.0f, c);
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.85f > room ? room / full : 0.85f;
    outlinedText(x - w / 2.0f + 50.0f + (room - full * sx) / 2.0f, y + 11.0f, sx, 0.85f, dark, fill, name);
}

/* The Journal's Stats entry (port/include/port/stats.h): the same button as the Photos', the Champ's Belt on it */
void portStatsJournalButton(WindowWork* wp, float x, float y, int selected) {
    const u8 k = selected ? 0xFF : 0x80;
    const GXColor c = {k, k, k, 0xFF};
    const GXColor dark = {(u8)(0x14 * k / 0xFF), (u8)(0x2C * k / 0xFF), (u8)(0x6E * k / 0xFF), 0xFF};
    const GXColor fill = {(u8)(0xF2 * k / 0xFF), (u8)(0xF4 * k / 0xFF), (u8)(0xFA * k / 0xFF), 0xFF};
    const char* name = portTrGame("Records");
    const f32 w = (f32)BANNER_TEX_W, room = w - 64.0f;
    f32 full, sx;
    if (bannerMade(wp, &sJournalBanner, 0.0f)) {
        if (selected) {
            tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, (GXColor){0x00, 0x00, 0x00, 0x80});
            x -= 8.0f;
            y += 8.0f;
        }
        tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, c);
    } else if (selected) {
        x -= 8.0f;
        y += 8.0f;
    }
    winIconInit();
    winIconSet(itemDataTable[ITEM_CHAMPS_BELT].icon_id, (Vec){x - w / 2.0f + 28.0f, y - 1.0f, 0.0f},
               (Vec){0.7f, 0.7f, 1.0f}, c);
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.85f > room ? room / full : 0.85f;
    outlinedText(x - w / 2.0f + 50.0f + (room - full * sx) / 2.0f, y + 11.0f, sx, 0.85f, dark, fill, name);
}

/* The Journal's Tracker entry (port/include/port/rando_tracker.h): the same button, the Magical Map on it */
void portTrackerJournalButton(WindowWork* wp, float x, float y, int selected) {
    const u8 k = selected ? 0xFF : 0x80;
    const GXColor c = {k, k, k, 0xFF};
    const GXColor dark = {(u8)(0x14 * k / 0xFF), (u8)(0x2C * k / 0xFF), (u8)(0x6E * k / 0xFF), 0xFF};
    const GXColor fill = {(u8)(0xF2 * k / 0xFF), (u8)(0xF4 * k / 0xFF), (u8)(0xFA * k / 0xFF), 0xFF};
    const char* name = portTrGame("Tracker");
    const f32 w = (f32)BANNER_TEX_W, room = w - 64.0f;
    f32 full, sx;
    if (bannerMade(wp, &sJournalBanner, 0.0f)) {
        if (selected) {
            tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, (GXColor){0x00, 0x00, 0x00, 0x80});
            x -= 8.0f;
            y += 8.0f;
        }
        tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, c);
    } else if (selected) {
        x -= 8.0f;
        y += 8.0f;
    }
    winIconInit();
    winIconSet(itemDataTable[ITEM_MAGICAL_MAP].icon_id, (Vec){x - w / 2.0f + 28.0f, y - 1.0f, 0.0f},
               (Vec){0.7f, 0.7f, 1.0f}, c);
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.85f > room ? room / full : 0.85f;
    outlinedText(x - w / 2.0f + 50.0f + (room - full * sx) / 2.0f, y + 11.0f, sx, 0.85f, dark, fill, name);
}

/* The Journal's Paper Mario 64 Tattle Log entry (port/include/port/pm64_media.h): the same button, Goombario
 * from the linked ROM on it (his first raster), the Tattle Log's icon without one */
void portPm64JournalButton(WindowWork* wp, float x, float y, int selected) {
    const u8 k = selected ? 0xFF : 0x80;
    const GXColor c = {k, k, k, 0xFF};
    const GXColor dark = {(u8)(0x14 * k / 0xFF), (u8)(0x2C * k / 0xFF), (u8)(0x6E * k / 0xFF), 0xFF};
    const GXColor fill = {(u8)(0xF2 * k / 0xFF), (u8)(0xF4 * k / 0xFF), (u8)(0xFA * k / 0xFF), 0xFF};
    const char* name = portTrGame("Paper Mario");
    const f32 w = (f32)BANNER_TEX_W, room = w - 64.0f;
    const Pm64Sprite* g = portPm64NpcSprite(PM64_SPR_WORLD_GOOMBARIO);
    const void* tex;
    int rw, rh, tw, th;
    f32 full, sx;
    if (bannerMade(wp, &sJournalBanner, 0.0f)) {
        if (selected) {
            tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, (GXColor){0x00, 0x00, 0x00, 0x80});
            x -= 8.0f;
            y += 8.0f;
        }
        tintedQuad(&sJournalBanner.obj, x, y, w, BANNER_TEX_H, c);
    } else if (selected) {
        x -= 8.0f;
        y += 8.0f;
    }
    if (g != NULL && portPm64SpriteRaster(g, 0, &rw, &rh, NULL) && (tex = portPm64RasterTexture(g, 0, -1, &tw, &th)) != NULL) {
        const f32 sc = 30.0f / (f32)(rw > rh ? rw : rh);
        portPm64Quad2D(tex, tw, th, (f32)rw / (f32)tw, (f32)rh / (f32)th, x - w / 2.0f + 28.0f, y - 1.0f, rw * sc,
                       rh * sc, (u32)k << 24 | (u32)k << 16 | (u32)k << 8 | 0xFFu);
    }
    full = (f32)FontGetMessageWidth(name);
    sx = full * 0.85f > room ? room / full : 0.85f;
    outlinedText(x - w / 2.0f + 50.0f + (room - full * sx) / 2.0f, y + 11.0f, sx, 0.85f, dark, fill, name);
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
 * heading the left page, its rows' names there and their values on the right.
 * As the game's own lists (items, badges): every row in dark ink, the cursor's
 * shown by the hand and a band of highlight behind it; a row not available
 * now in a lighter ink. Each group's heading in the heading's violet over a
 * rule, a form's own heading in brown. */
static void pageDisp(WindowWork* wp, const Row* rows, int count, s32 row, s32 top, f32 x, f32 y, int inside) {
    const GXColor ink = {0x1E, 0x16, 0x10, 0xFF};
    const GXColor value = {0x2A, 0x2E, 0x6E, 0xFF}; /* the values: a blue ink, apart from the names */
    const GXColor dim = {0x74, 0x6A, 0x60, 0xFF}; /* (lighter, still readable) */
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor heading = {0x3A, 0x1C, 0x66, 0xFF};
    const GXColor subhead = {0x7A, 0x4E, 0x22, 0xFF};
    const GXColor rule = {0x3A, 0x1C, 0x66, 0x50};
    const GXColor band = {0xFF, 0xD8, 0x6A, 0x8C};
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    int i;
    bookGX(wp, x + BOOK_X, y + BOOK_TOP, BOOK_W, BOOK_PAGE_H);
    if (inside && row < count && inView(rows, top, row) && selectable(rows[row].kind)) {
        portPauseBox(x + ROW_X - 12.0f, y + ROW_TOP - slotsBetween(rows, top, row) * ROW_STEP + 1.0f,
                     VALUE_X - ROW_X + 22.0f, rowSlots(&rows[row]) * ROW_STEP - 2.0f, band);
    }
    if (count > 0 && rows[0].kind == KIND_HEADING) {
        /* (a Modes hub button's page: its sticker, wider than an icon, a little smaller) */
        const f32 shift = rows[0].group ? 10.0f : 0.0f;
        pictureDisp(&rows[0], x + BOOK_X + 40.0f, y + BOOK_TOP - 28.0f, rows[0].group ? 0.85f : 1.0f, white);
        winFontInit();
        fitText(x + BOOK_X + 62.0f + shift, y + BOOK_TOP - 17.0f, 200.0f - shift, 0.95f, heading,
                portTrGame(rows[0].label), 0);
    }
    for (i = top; i < count && inView(rows, top, i); i++) {
        const Row* r = &rows[i];
        const f32 ry = y + ROW_TOP - slotsBetween(rows, top, i) * ROW_STEP;
        const int off = (r->kind == KIND_FORM && (r->f.flags & PORT_PAUSE_ROW_DIM)) || r->held;
        char text[64];
        if (r->kind == KIND_HEADING) {
            continue;
        }
        if (r->kind == KIND_SUBHEAD && r->group) {
            portPauseBox(x + ROW_X - 10.0f, ry - ROW_STEP + 4.0f, VALUE_X - ROW_X + 18.0f, 1.5f, rule);
            winFontInit();
            fitText(x + ROW_X - 10.0f, ry, BOOK_W - 60.0f, 0.78f, heading, rowLabel(r), 0);
            continue;
        }
        winFontInit();
        if (r->kind == KIND_SUBHEAD) {
            fitText(x + ROW_X - 4.0f, ry, BOOK_W / 2.0f - 36.0f, 0.72f, subhead, rowLabel(r), 0);
            continue;
        }
        valueText(r, text, sizeof(text));
        if (rowSlots(r) > 1) {
            /* two lines: the name across the line, the value under it to the right, each in smaller
               letters (to 0.62) before narrower ones */
            const f32 lroom = VALUE_X - ROW_X, vroom = VALUE_X - ROW_X - 40.0f;
            const f32 lfull = (f32)FontGetMessageWidth(rowLabel(r)), vfull = (f32)FontGetMessageWidth(text);
            const f32 ls = lfull * 0.8f <= lroom ? 0.8f : (lroom / lfull > 0.62f ? lroom / lfull : 0.62f);
            const f32 vs = vfull * 0.8f <= vroom ? 0.8f : (vroom / vfull > 0.62f ? vroom / vfull : 0.62f);
            fitText(x + ROW_X, ry - (0.8f - ls) * 10.0f, lroom + 0.5f, ls, off ? dim : ink, rowLabel(r), 0);
            fitText(x + VALUE_X, ry - ROW_STEP - (0.8f - vs) * 10.0f, vroom + 0.5f, vs, off ? dim : value, text, 1);
            continue;
        }
        {
            /* the name from the left, the value to the right: each its own page while they fit there, else
               the room of the two shared (a long name past the fold over a short value, a translation's), and
               narrowed together only when even that is too little; a row with no value, a note: all of it */
            const f32 room = VALUE_X - ROW_X - 16.0f, half = BOOK_W / 2.0f - 44.0f;
            f32 lw = (f32)FontGetMessageWidth(rowLabel(r)) * 0.8f;
            f32 vw = text[0] != '\0' ? (f32)FontGetMessageWidth(text) * 0.8f : 0.0f;
            if (text[0] == '\0') {
                lw = room + 16.0f;
            } else if (lw <= half && vw <= half) {
                lw = half;
                vw = half;
            } else if (lw + vw <= room) {
                lw = lw > half ? room - vw : half;
                vw = room - lw;
            } else {
                const f32 k = room / (lw + vw);
                lw *= k;
                vw *= k;
            }
            if (lw < (f32)FontGetMessageWidth(rowLabel(r)) * 0.8f) {
                /* a long name or note: smaller letters (to 0.62) before narrower ones, not squashed */
                const f32 full = (f32)FontGetMessageWidth(rowLabel(r));
                const f32 s = lw / full > 0.62f ? lw / full : 0.62f;
                fitText(x + ROW_X, ry - (0.8f - s) * 10.0f, lw + 0.5f, s, off ? dim : ink, rowLabel(r), 0);
            } else {
                fitText(x + ROW_X, ry, lw + 0.5f, 0.8f, off ? dim : ink, rowLabel(r), 0);
            }
            if (text[0] != '\0') {
                fitText(x + VALUE_X, ry, vw + 0.5f, 0.8f, off ? dim : value, text, 1);
            }
        }
    }
    /* where the view is in the page, as the Journal's logs count theirs */
    if (count > 1 && slotsBetween(rows, 1, count) > VISIBLE) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)row, count - 1);
        fitText(x + VALUE_X, y + BOOK_TOP - 20.0f, 80.0f, 0.7f, grey, pos, 1);
    }
}

/* the Modes hub's title over its buttons: the Modes page's picture and its name, as a button's (it
 * goes up as the book comes) */
static void hubTitleDisp(f32 x, f32 y) {
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const GXColor dark = {0x3A, 0x1C, 0x66, 0xFF};
    const GXColor fill = {0xF6, 0xEA, 0xF4, 0xFF};
    const Row modes = {.kind = KIND_HEADING, .label = sHubTitle};
    const char* name = portTrGame(sHubTitle);
    const f32 full = (f32)FontGetMessageWidth(name) * 1.0f;
    const f32 left = x - (full + 40.0f) / 2.0f;
    pictureDisp(&modes, left + 14.0f, y + 113.0f, 1.0f, white);
    outlinedText(left + 36.0f, y + 124.0f, 1.0f, 1.0f, dark, fill, name);
}

/* the front page's or the Modes hub's buttons at (x, y), the cursor on cur's, all down behind the
 * help box as the book comes up (sSlide) */
static void buttonsDisp(WindowWork* wp, const Row* rows, int count, s32 cur, int hub, f32 x, f32 y) {
    int i;
    for (i = 0; i <= count; i++) {
        /* (the cursor's last, over the others) */
        const int k = i < count ? i : (int)cur;
        f32 bx, by;
        if (k >= count || (i < count && k == cur)) {
            continue;
        }
        frontAt(k, count, hub, &bx, &by);
        by += (-160.0f - by) * sSlide;
        bannerDisp(wp, &rows[k], x + bx, y + by, i == count);
    }
}

void portPauseSettingsDisp(s32 camId, WindowWork* wp, s32 idx) {
    static u32 sStepped;
    const f32 x = wp->menu[idx].x;
    const f32 y = wp->menu[idx].y;
    /* (opened from the Multiplayer page: on its tab, in its colour) */
    const int inside = wp->active == PORT_PAUSE_SETTINGS_TAB + 11 || (sHosted && wp->active == PORT_PAUSE_ONLINE_TAB + 11);
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
        sSide += (sSideTo - sSide) / d;
        if (fabsf(sSideTo - sSide) < 0.002f) {
            sSide = sSideTo;
        }
    }

    winBgGX(wp, sHosted ? PORT_PAUSE_ONLINE_TAB : PORT_PAUSE_SETTINGS_TAB, x, y);
    if (portPausePage() != NULL) { /* (over the Bindings page too) */
        portPausePageDisp(wp, x, y);
        return;
    }
    if (sBindings) { /* the Bindings page (pause_bindings_aurora.c): on the page's panel */
        winKirinukiGX(wp, 0, x - 270.0f, y + 144.0f, 540.0f, 270.0f);
        portPauseBindingsDisp(wp, x, y, inside);
        return;
    }
    if (sSlide < 1.0f) { /* the buttons: down behind the help box, the book over them, as it comes up */
        if (sSide < 1.0f) {
            buttonsDisp(wp, sFront, sFrontCount, sCat < 0 ? sRow : sFrontRow, 0, x - HUB_SLIDE_X * sSide, y);
        }
        if (sSide > 0.0f) { /* the Modes hub's: in from the right */
            buttonsDisp(wp, sHubFront, sHubFrontCount, hubShown() ? sRow : sHubFrontRow, 1,
                        x + HUB_SLIDE_X * (1.0f - sSide), y);
            hubTitleDisp(x + HUB_SLIDE_X * (1.0f - sSide), y + 200.0f * sSlide);
        }
    }
    if (sSlide > 0.0f) { /* the book: up from below, as the Journal's map comes */
        const f32 by = y - 480.0f * (1.0f - sSlide);
        if (sCat >= 0 && !hubShown()) {
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

int portPauseStartTaken(void) {
    return sConfirm != 0 || portPauseTextUp() || portPauseOnlineTakesStart() || portBindingsCapturing();
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
    if (strncmp(tag, "pc_menu_online", 14) == 0) {
        return portPauseOnlineMsg(tag);
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
