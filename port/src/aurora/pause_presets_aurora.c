/* The Start menu's presets page (port/include/port/presets.h): the Presets row (Settings, Gameplay) opens
 * it over the tab. Every preset a tile in a grid of three across, in its group's order (Classic, Helpful,
 * Challenge, Special), each with its item's icon, its name and its tagline; the one the settings match
 * now framed in gold, and the title line saying which (or Custom, how many differ from the nearest). The
 * help box has the tile's one sentence. The stick or the D-pad move, A twice puts the preset in place (the
 * first says what the second does), B goes back. A tap or a click on a tile is its A. */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/button_markup.h"
#include "port/text_fit.h"
#include "port/menu_tap.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/presets.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "mario/mario_pouch.h"
#include "driver/camdrv.h"
#include "driver/icondrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

#define PANEL_X (-270.0f)
#define PANEL_TOP 144.0f
#define PANEL_W 540.0f
#define PANEL_H 270.0f
#define COLS 3
#define CELL_W 173.0f
#define CELL_H 46.0f
#define GRID_X (PANEL_X + 10.5f)
#define GRID_TOP (PANEL_TOP - 34.0f)

static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x60, 0x60, 0x60, 0xFF};
static const GXColor kHeading = {0x5A, 0x46, 0x9C, 0xFF};

static int sAt;        /* the tile the cursor is on */
static int sArmed = -1; /* the tile A was pressed on once (the next A puts it in place) */
static const char* sSaid; /* an answer for the help box (English), until the cursor moves */
static char sHelp[512];

int portPresetItemIcon(int item) {
    return item > 0 && item < ITEM_MAX ? (int)itemDataTable[item].icon_id : ICON_19E; /* (none: Mario's head) */
}

static int tileCount(void) {
    int n = 0;
    while (portPresetShown(n) >= 0) {
        n++;
    }
    return n;
}

static void tileAt(int k, f32* x, f32* top) {
    *x = GRID_X + (k % COLS) * CELL_W;
    *top = GRID_TOP - (k / COLS) * CELL_H;
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void text(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* t, int right) {
    const f32 textWas = portButtonsGameTextScale(s); /* (a {A} in it: centred on the letters at s, button_markup.h) */
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 k = full > 0.0f && full * s > w ? w / full : s;
    portTextFit("presets-page", t, full * s, w, 1);
    winFontInit();
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, portTextFitHeight(k, s), portTextFitHeight(k, s)}, c, "%s", t);
    portButtonsGameTextScale(textWas);
}

/* the mouse and touch: the tile under the pointer selected; a click on the selected one is its A */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    const int n = tileCount();
    int k;
    if (kind == 0) {
        return 0;
    }
    for (k = 0; k < n; k++) {
        f32 x, top;
        tileAt(k, &x, &top);
        if (gx >= x && gx < x + CELL_W - 4.0f && gy <= top && gy > top - CELL_H + 4.0f) {
            if (k == sAt) {
                return kind == 1;
            }
            sAt = k;
            psndSFXOn((const char*)0x20005);
            return 0;
        }
    }
    return 0;
}

static void pageOpen(int arg) {
    int now, nearest = 0, k;
    (void)arg;
    now = portPresetNow(&nearest, NULL);
    sAt = 0;
    for (k = 0; portPresetShown(k) >= 0; k++) {
        if (portPresetShown(k) == (now >= 0 ? now : nearest)) {
            sAt = k; /* (on the one in place, or the nearest) */
        }
    }
    sArmed = -1;
    sSaid = NULL;
}

static int pageMain(WindowWork* wp) {
    const int n = tileCount();
    const int was = sAt;
    const int click = pointer();
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (n <= 0) {
        return 0; /* (no presets to show: nothing to move between) */
    }
    if (sAt >= n) {
        sAt = n - 1;
    }
    if (click || (wp->buttonTrg & PAD_BUTTON_A)) {
        const int p = portPresetShown(sAt);
        if (!portPresetAvailable(p)) {
            psndSFXOn((const char*)0x20013);
            sSaid = portPresetUse(p); /* (why not) */
        } else if (sArmed != sAt) {
            psndSFXOn((const char*)0x20012);
            sArmed = sAt;
            sSaid = NULL;
        } else {
            psndSFXOn((const char*)0x20012);
            sSaid = portPresetUse(p);
            if (sSaid == NULL) {
                sSaid = "Done: this preset is in place.";
            }
            sArmed = -1;
        }
    } else if (wp->dirRep & DEMO_STICK_LEFT) {
        sAt = (sAt + n - 1) % n;
    } else if (wp->dirRep & DEMO_STICK_RIGHT) {
        sAt = (sAt + 1) % n;
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        const int rows = (n + COLS - 1) / COLS;
        int r = sAt / COLS;
        const int c = sAt % COLS;
        do {
            r = (r + ((wp->dirRep & DEMO_STICK_UP) ? rows - 1 : 1)) % rows;
        } while (r * COLS + c >= n);
        sAt = r * COLS + c;
    }
    if (sAt != was) {
        psndSFXOn((const char*)0x20005);
        sArmed = -1;
        sSaid = NULL;
    }
    {
        f32 x, top;
        tileAt(sAt, &x, &top);
        wp->helpTgtX = x - 4.0f;
        wp->helpTgtY = top - CELL_H / 2.0f + 10.0f;
    }
    return 0;
}

static const char* pageHelp(void) {
    char both[512];
    const int p = portPresetShown(sAt);
    if (sSaid != NULL) {
        portPauseWrap(portTrGame(sSaid), sHelp, sizeof(sHelp));
        return sHelp;
    }
    snprintf(both, sizeof(both), "%s %s", portTrGame(portPresetAbout(p)),
             portTrGame(sArmed == sAt ? "{A} again: put it in place." : "{A}: use this preset. {B}: back."));
    portPauseWrap(both, sHelp, sizeof(sHelp));
    return sHelp;
}

static void pageDisp(WindowWork* wp, float x, float y) {
    const int n = tileCount();
    int nearest = 0, differ = 0, k;
    const int now = portPresetNow(&nearest, &differ);
    char line[128];
    winKirinukiGX(wp, 0, x + PANEL_X, y + PANEL_TOP, PANEL_W, PANEL_H);
    text(x + PANEL_X + 20.0f, y + PANEL_TOP - 10.0f, 200.0f, 0.75f, kHeading, portTrGame("Presets"), 0);
    if (now >= 0) {
        snprintf(line, sizeof(line), portTrGame("Now: %s"), portTrGame(portPresetName(now)));
    } else if (differ == 1) {
        snprintf(line, sizeof(line), portTrGame("Custom: 1 differs from %s"), portTrGame(portPresetName(nearest)));
    } else {
        snprintf(line, sizeof(line), portTrGame("Custom: %d differ from %s"), differ,
                 portTrGame(portPresetName(nearest)));
    }
    text(x + PANEL_X + PANEL_W - 20.0f, y + PANEL_TOP - 13.0f, 300.0f, 0.62f, kGrey, line, 1);
    for (k = 0; k < n; k++) {
        const int p = portPresetShown(k);
        const int here = k == sAt, on = p == now, can = portPresetAvailable(p);
        f32 tx, top;
        tileAt(k, &tx, &top);
        tx += x;
        top += y;
        if (on) { /* in place: a gold frame */
            portPauseBox(tx + 1.0f, top - 1.0f, CELL_W - 6.0f, CELL_H - 6.0f, (GXColor){0xD8, 0xA0, 0x20, 0xFF});
        }
        portPauseBox(tx + 3.0f, top - 3.0f, CELL_W - 10.0f, CELL_H - 10.0f,
                     here ? (sArmed == sAt ? (GXColor){0xF8, 0xB0, 0x70, 0xFF} : (GXColor){0xF4, 0xD0, 0x90, 0xFF})
                          : (GXColor){0xF8, 0xEC, 0xDC, 0xFF});
        iconDispGxAlpha(&(Vec){tx + 22.0f, top - CELL_H / 2.0f - 13.0f, 0.0f}, ICON_DISP_2D,
                        (u16)portPresetItemIcon(portPresetItem(p)), can ? 0xFF : 0x70, 0.8f);
        text(tx + 42.0f, top - 5.0f, CELL_W - 52.0f, 0.68f, can ? kBlack : kGrey, portTrGame(portPresetName(p)), 0);
        text(tx + 42.0f, top - 23.0f, CELL_W - 52.0f, 0.52f, kGrey, portTrGame(portPresetTagline(p)), 0);
    }
}

static const PortPausePage kPresetsPage = {pageOpen, pageMain, pageDisp, pageHelp};

void portPresetsPageOpen(void) {
    portPauseOpenPage(&kPresetsPage, 0);
}
