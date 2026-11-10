/* The Journal's Tracker (port/include/port/rando_tracker.h): the randomizer's checks in a book as the Stats',
 * after the game's own entries. The first pages count the places per area (found of all, and how many can be
 * reached now); then each area's places, one row each: found (grey), reachable now (green) or not yet (red).
 * A switches what is listed (every place, the ones reachable now, the ones not found yet), L and R (or the
 * stick) turn the pages, B goes back. The second screen's menu is the game's own pause menu, so it has the
 * same entry.
 *
 * Draw-only: it reads the game's flags and pouch through the tracker, never changes them, and keeps the game's
 * random numbers as they were (the font takes two a letter). Only there while a seed is tracked. */
#include "port/text_fit.h"
#include "port/pause_settings.h"
#include "port/platform.h"
#include "port/rando_tracker.h"
#include "port/tr.h"
#include "driver/msgdrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

/* the book's top left, from the Journal's (x, y): where the Tattle log's is (and the Stats') */
#define BOOK_DX (-170.0f)
#define BOOK_DY 130.0f
static const float kPageX[2] = {20.0f, 250.0f};
#define PAGE_W 186.0f
#define ROW_TOP 22.0f
#define ROW_H 19.0f
#define ROWS 11 /* a page's rows, its heading's among them */

enum { SHOW_ALL, SHOW_REACHABLE, SHOW_MISSING, SHOW_COUNT };
static int sShow;   /* what is listed */
static int sSpread; /* the spread shown */
static int sOpen;

typedef struct Row {
    char label[80];
    char value[24];
    int heading;
    int state; /* PORT_TRACKER_*, or -1: plain ink */
} Row;

typedef struct Page {
    Row rows[ROWS];
    int count;
} Page;

#define MAX_PAGES 96
static Page sPages[MAX_PAGES];
static int sPageCount;

static Page* newPage(const char* heading) {
    Page* p;
    if (sPageCount >= MAX_PAGES) {
        return NULL;
    }
    p = &sPages[sPageCount++];
    memset(p, 0, sizeof(*p));
    snprintf(p->rows[0].label, sizeof(p->rows[0].label), "%s", heading);
    p->rows[0].heading = 1;
    p->rows[0].state = -1;
    p->count = 1;
    return p;
}

/* an empty page (the right one of a spread with nothing on it) */
static void blankPage(void) {
    Page* p = newPage("");
    if (p != NULL) {
        p->count = 0;
    }
}

/* a row; a full page goes on to a new one with the same heading */
static Page* row(Page* p, const char* heading, const char* label, const char* value, int state) {
    Row* r;
    if (p != NULL && p->count >= ROWS) {
        p = newPage(heading);
    }
    if (p == NULL) {
        return NULL;
    }
    r = &p->rows[p->count++];
    snprintf(r->label, sizeof(r->label), "%s", label);
    snprintf(r->value, sizeof(r->value), "%s", value);
    r->state = state;
    return p;
}

static int listed(int state) {
    switch (sShow) {
    case SHOW_REACHABLE: return state == PORT_TRACKER_REACHABLE;
    case SHOW_MISSING: return state != PORT_TRACKER_FOUND;
    default: return 1;
    }
}

static void build(void) {
    char buf[96];
    Page* p;
    int a, i, found = 0, reachable = 0, total;
    const char* head = portTrGame("Tracker");
    sPageCount = 0;
    if (!portTrackerRefresh()) {
        p = newPage(head);
        row(p, head, portTrGame("No seed is being played."), "", -1);
        blankPage();
        return;
    }
    total = portTrackerTotals(&found, &reachable);
    p = newPage(head);
    snprintf(buf, sizeof(buf), "%d/%d", found, total);
    p = row(p, head, portTrGame("Found"), buf, -1);
    if (portTrackerHasLogic()) {
        snprintf(buf, sizeof(buf), "%d", reachable);
        p = row(p, head, portTrGame("Reachable now"), buf, -1);
    }
    for (a = 0; a < portTrackerAreaCount(); a++) {
        const PortTrackerArea* ar = portTrackerArea(a);
        if (portTrackerHasLogic() && ar->reachable > 0) {
            snprintf(buf, sizeof(buf), "%d/%d +%d", ar->found, ar->total, ar->reachable);
        } else {
            snprintf(buf, sizeof(buf), "%d/%d", ar->found, ar->total);
        }
        p = row(p, head, ar->name, buf,
                ar->found == ar->total ? PORT_TRACKER_FOUND : ar->reachable > 0 ? PORT_TRACKER_REACHABLE : -1);
    }
    if (sPageCount % 2 != 0) {
        blankPage();
    }
    for (a = 0; a < portTrackerAreaCount(); a++) {
        const PortTrackerArea* ar = portTrackerArea(a);
        int any = 0;
        p = NULL;
        for (i = 0; i < portTrackerPlaceCount(a); i++) {
            const int state = portTrackerPlaceState(a, i);
            const char* value = state == PORT_TRACKER_FOUND        ? portTrGame("Found")
                                : state == PORT_TRACKER_REACHABLE ? (portTrackerHasLogic() ? portTrGame("Can reach") : "")
                                                                  : "";
            if (!listed(state)) {
                continue;
            }
            if (!any) {
                p = newPage(ar->name);
                any = 1;
            }
            p = row(p, ar->name, portTrackerPlaceName(a, i), value, state);
        }
    }
    if (sPageCount % 2 != 0) {
        blankPage();
    }
}

static int spreads(void) {
    return (sPageCount + 1) / 2;
}

/* --- the entry --- */

int portTrackerJournalEntry(void) {
    return portTrackerRefresh();
}

void portTrackerJournalOpen(struct WindowWork* wp) {
    (void)wp;
    sOpen = 1;
    build();
    if (sSpread >= spreads()) {
        sSpread = 0;
    }
}

static void help(WindowWork* wp) {
    static char sTag[64];
    snprintf(sTag, sizeof(sTag), "pc_menu_tracker_page_%d_%d", sShow, sSpread);
    winMsgEntry(wp, 0, sTag, NULL);
}

int portTrackerJournalMain(struct WindowWork* wp) {
    const int n = spreads();
    if (!sOpen) {
        portTrackerJournalOpen(wp); /* (a save state loaded in here) */
    }
    if (wp->buttonTrg & PAD_BUTTON_START) {
        sOpen = 0;
        return -2;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn(SFX_NAME_WORD(0x20013));
        sOpen = 0;
        return -1;
    }
    if (wp->buttonTrg & PAD_BUTTON_A) {
        sShow = (sShow + 1) % SHOW_COUNT;
        build();
        if (sSpread >= spreads()) {
            sSpread = spreads() - 1;
        }
        psndSFXOn(SFX_NAME_WORD(0x20012));
    } else if ((wp->buttonRep & PAD_TRIGGER_R) || (wp->dirRep & DEMO_STICK_RIGHT)) {
        if (sSpread + 1 < n) {
            sSpread++;
            psndSFXOn(SFX_NAME_WORD(0x20035));
        }
    } else if ((wp->buttonRep & PAD_TRIGGER_L) || (wp->dirRep & DEMO_STICK_LEFT)) {
        if (sSpread > 0) {
            sSpread--;
            psndSFXOn(SFX_NAME_WORD(0x20035));
        }
    }
    wp->helpTgtX = BOOK_DX - 128.0f;
    wp->helpTgtY = BOOK_DY - 172.0f;
    help(wp);
    return 0;
}

/* --- drawing --- */

static void textAt(f32 x, f32 y, f32 scale, f32 room, GXColor c, const char* text) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 sx = full * scale > room ? room / full : scale;
    winFontInit();
    winFontSet((Vec){x, y, 0.0f}, (Vec){sx, portTextFitHeight(sx, scale), portTextFitHeight(sx, scale)}, c, "%s", text);
}

static void centredText(f32 cx, f32 y, f32 scale, f32 room, GXColor c, const char* text) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 sx = full * scale > room ? room / full : scale;
    textAt(cx - full * sx / 2.0f, y, scale, room, c, text);
}

static GXColor inkOf(int state) {
    switch (state) {
    case PORT_TRACKER_FOUND: return (GXColor){0x80, 0x78, 0x70, 0xFF};
    case PORT_TRACKER_REACHABLE: return (GXColor){0x10, 0x70, 0x28, 0xFF};
    case PORT_TRACKER_LOCKED: return (GXColor){0x90, 0x28, 0x20, 0xFF};
    default: return (GXColor){0x20, 0x18, 0x10, 0xFF};
    }
}

static void pageDisp(const Page* p, f32 x, f32 y) {
    const GXColor head = {0x90, 0x28, 0x20, 0xFF};
    int i;
    for (i = 0; i < p->count; i++) {
        const Row* r = &p->rows[i];
        const f32 ry = y - ROW_TOP - ROW_H * (f32)i;
        const GXColor ink = inkOf(r->state);
        if (r->heading) {
            centredText(x + PAGE_W / 2.0f, ry + 6.0f, 0.8f, PAGE_W, head, r->label);
            continue;
        }
        {
            const f32 vs = 0.62f;
            const f32 vw = r->value[0] != '\0' ? (f32)FontGetMessageWidth(r->value) * vs : 0.0f;
            textAt(x, ry, 0.58f, PAGE_W - vw - 8.0f, ink, r->label);
            if (r->value[0] != '\0') {
                textAt(x + PAGE_W - vw, ry, vs, PAGE_W, ink, r->value);
            }
        }
    }
}

void portTrackerJournalDisp(struct WindowWork* wp, float x, float y) {
    static const char* const kShow[SHOW_COUNT] = {"Every place", "Reachable now", "Not found yet"};
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const f32 bx = x + BOOK_DX, by = y + BOOK_DY;
    const int n = spreads();
    const u32 seed = portRandState(); /* (the font's random numbers: as they were) */
    char buf[32];
    int side;
    winBookGX(wp, 1, bx, by);
    winKirinukiGX(wp, 0, bx - 110.0f, by - 160.0f, 100.0f, 70.0f);
    centredText(bx - 60.0f, by - 166.0f, 0.75f, 88.0f, black, portTrGame("Tracker"));
    centredText(bx - 60.0f, by - 184.0f, 0.6f, 88.0f, black, portTrGame(kShow[sShow]));
    snprintf(buf, sizeof(buf), "%d/%d", sSpread + 1, n);
    centredText(bx - 60.0f, by - 202.0f, 0.65f, 88.0f, black, buf);
    for (side = 0; side < 2; side++) {
        const int at = sSpread * 2 + side;
        if (at < sPageCount) {
            pageDisp(&sPages[at], bx + kPageX[side], by);
        }
    }
    if (sSpread > 0) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, 18.0f + by, 0.0f}, (Vec){-1.0f, -1.0f, -1.0f}, white);
        winIconInit();
        winIconSet(ICON_07E, (Vec){440.0f + bx, by, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    if (sSpread < n - 1) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, (by - 15.0f) - 240.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f}, white);
        winIconInit();
        winIconSet(ICON_080, (Vec){440.0f + bx, by - 240.0f, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    portRandSetState(seed);
}

/* --- the help box --- */

const char* portTrackerJournalMsg(const char* tag) {
    static char sHelp[320];
    if (strncmp(tag, "pc_menu_tracker", 15) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_menu_tracker") == 0) {
        portPauseWrap(portTrGame("The randomizer's places: found, reachable now, or not yet."), sHelp,
                      sizeof(sHelp));
        return sHelp;
    }
    portPauseWrap(portTrGame("L, R: turn the page. A: every place, the ones reachable now, the ones not found yet."),
                  sHelp, sizeof(sHelp));
    return sHelp;
}
