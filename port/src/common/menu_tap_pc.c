/* Tapping a row of the game's selection lists (port/include/port/menu_tap.h):
 * item lists, key items at a door, shops, partners. A tap on the picture,
 * handed over by the touch overlay, is taken by the next frame of an open
 * list (winMgrSelect): it selects the row under it, or confirms the row
 * already selected, as A would.
 *
 * Geometry (winmgr.c select_disp*): the game's 2D units, 608 x 480 from the
 * frame's centre, y up (in widescreen the 2D keeps its 4:3 shape, centred,
 * port/include/port/widescreen.h); row i is drawn about (list y - 44 - 24 i +
 * the list's scroll), rows 24 apart, between the list's x and x + width, and
 * shown from 34 below its top to 16 above its bottom (its scissor box). */
#include "manager/winmgr.h"
#include "port/menu_tap.h"
#include "port/companion_menu.h"
#include "wide_x.h"
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>

int portMenuConfirm = 0;

static volatile float sTapX, sTapY;
static volatile int sTapPending; /* 1 a tap, 2 the pointer moved over a row */
static volatile u32 sOpenFrame; /* last retrace a list was up */
static u32 sTapFrame;

void portMenuTap(float nx, float ny) {
    sTapX = nx;
    sTapY = ny;
    sTapFrame = VIGetRetraceCount();
    sTapPending = 1;
}

void portMenuHover(float nx, float ny) {
    if (sTapPending == 1) {
        return; /* a click waiting comes first */
    }
    sTapX = nx;
    sTapY = ny;
    sTapFrame = VIGetRetraceCount();
    sTapPending = 2;
}

int portMenuOpen(void) {
    return VIGetRetraceCount() - sOpenFrame < 3;
}

int portMenuPoint(float* gx, float* gy) {
    static u32 sPointFrame = ~0u;
    static int sKind;
    static float sX, sY;
    const u32 now = VIGetRetraceCount();
    if (gPortCompanionMenu) { /* the second screen's menu: its own taps, the game's left alone */
        return portCompanionMenuPoint(gx, gy);
    }
    sOpenFrame = now;
    if (sPointFrame != now) {
        sPointFrame = now;
        sKind = sTapPending != 0 && now - sTapFrame <= 10 ? sTapPending : 0;
        sTapPending = 0;
        sX = (sTapX - 0.5f) * 608.0f * portWidescreenEdgeScale();
        sY = (0.5f - sTapY) * 480.0f;
        if (sKind == 1 && getenv("TTYD_INPUT") != NULL) { /* scripted runs (tests) */
            printf("[menu] tap at %.0f, %.0f\n", sX, sY);
            fflush(stdout);
        }
    }
    *gx = sX;
    *gy = sY;
    return sKind;
}

/* the presses asked for, applied over the pad's read: B held 2 frames, or
 * the stick tilted 2 frames, then 2 frames of nothing, so each is a new press */
static volatile int sBackPending, sWheelPending;
static int sHold, sStick;

void portMenuBack(void) {
    if (portMenuOpen()) {
        sBackPending = 1;
    }
}

void portMenuWheel(int notches) {
    if (portMenuOpen()) {
        sWheelPending += notches;
    }
}

void portMenuApplyPad(PADStatus* pad) {
    if (sHold == 0) {
        if (sBackPending) {
            sBackPending = 0;
            sWheelPending = 0;
            sStick = 0;
            sHold = 4;
        } else if (sWheelPending != 0) {
            sStick = sWheelPending > 0 ? 127 : -127; /* the wheel up: the stick up */
            sWheelPending += sWheelPending > 0 ? -1 : 1;
            sHold = 4;
        }
    }
    if (sHold == 0) {
        return;
    }
    if (sHold-- > 2) {
        pad[0].err = PAD_ERR_NONE;
        if (sStick != 0) {
            pad[0].stickX = 0;
            pad[0].stickY = (s8)sStick;
        } else {
            pad[0].button |= PAD_BUTTON_B;
            pad[0].analogB = 255;
        }
    }
}

int portMenuTapRow(struct WinMgrSelect* sel, struct WinMgrEntry* list) {
    f32 gx, gy;
    s32 i;
    int hover;
    sOpenFrame = VIGetRetraceCount();
    if (!sTapPending) {
        return -1;
    }
    hover = sTapPending == 2;
    sTapPending = 0;
    if (VIGetRetraceCount() - sTapFrame > 10) {
        return -1; /* stale */
    }
    gx = (sTapX - 0.5f) * 608.0f * portWidescreenEdgeScale();
    gy = (0.5f - sTapY) * 480.0f;
    if (gx < list->x || gx > list->x + list->width || gy > list->y - 34 || gy < list->y - list->height + 16) {
        return -1;
    }
    for (i = 0; i < sel->numItems; i++) {
        const f32 row = sel->scrollY + (f32)(list->y - 44 - i * 24);
        if (gy <= row + 12.0f && gy > row - 12.0f) {
            if (hover && i == sel->cursor) {
                return -1; /* already there: not a confirm */
            }
            if (getenv("TTYD_INPUT") != NULL) { /* scripted runs (tests) */
                printf("[menu] tap on row %d (selected: %d)\n", (int)i, (int)sel->cursor);
                fflush(stdout);
            }
            return i;
        }
    }
    return -1;
}
