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
#include "wide_x.h"
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>

int portMenuConfirm = 0;

static volatile float sTapX, sTapY;
static volatile int sTapPending;
static volatile u32 sOpenFrame; /* last retrace a list was up */
static u32 sTapFrame;

void portMenuTap(float nx, float ny) {
    sTapX = nx;
    sTapY = ny;
    sTapFrame = VIGetRetraceCount();
    sTapPending = 1;
}

int portMenuOpen(void) {
    return VIGetRetraceCount() - sOpenFrame < 3;
}

int portMenuTapRow(struct WinMgrSelect* sel, struct WinMgrEntry* list) {
    f32 gx, gy;
    s32 i;
    sOpenFrame = VIGetRetraceCount();
    if (!sTapPending) {
        return -1;
    }
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
            if (getenv("TTYD_INPUT") != NULL) { /* scripted runs (tests) */
                printf("[menu] tap on row %d (selected: %d)\n", (int)i, (int)sel->cursor);
                fflush(stdout);
            }
            return i;
        }
    }
    return -1;
}
