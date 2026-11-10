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
#include "port/menu_buttons.h"
#include "port/statesync.h"
#include "port/replay.h"
#include "port/companion_menu.h"
#include "wide_x.h"
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int portMenuConfirm = 0;

/* the pointer, in the game's 2D units (608 x 480 from the centre, y up) */
static volatile float sTapX, sTapY;
static int sTapPending;          /* 1 a tap, 2 the pointer moved over a row */
static volatile u32 sOpenFrame; /* last retrace a list was up */
static u32 sTapFrame;

/* A replay: the pointer chooses nothing (a row chosen outside the pad's input
 * wouldn't be in the recording). A battle shared online: nor there (its games
 * play the battle's controllers frame for frame, and a row chosen here alone
 * would move this game's cursor and not the other's). The mouse's B (a right
 * click) and wheel are controller 1's presses already (portMenuApplyPad). */
static void pointerAt(float nx, float ny, float* gx, float* gy) {
    /* (this game's widescreen: the 4:3 units are the same in every game) */
    *gx = (nx - 0.5f) * 608.0f * portWidescreenEdgeScale();
    *gy = (0.5f - ny) * 480.0f;
}

static void pointer(int kind, float nx, float ny) {
    float gx, gy;
    if (portReplayActive() || portStateSyncSharedBattle()) {
        return;
    }
    pointerAt(nx, ny, &gx, &gy);
    if (kind == 2 && sTapPending == 1) {
        return;
    }
    sTapX = gx;
    sTapY = gy;
    sTapFrame = VIGetRetraceCount();
    __atomic_store_n(&sTapPending, kind, __ATOMIC_RELEASE); /* (after the place and frame: a reader on another thread sees them) */
}

void portMenuTap(float nx, float ny) {
    pointer(1, nx, ny);
}

void portMenuHover(float nx, float ny) {
    pointer(2, nx, ny);
}

int portMenuOpen(void) {
    return VIGetRetraceCount() - sOpenFrame < 3;
}

int portMsgWaiting(int* sel, int* numSel, const char** tag); /* msgdrv.c */

int portMenuBackOpen(void) {
    int sel = 0, num = 0;
    const char* tag = NULL;
    /* (a talk's message or choice up: Esc and a right click were nothing there, and a keyboard or mouse player
     * who took them for the game's B, as in its lists, was left in the talk for good) */
    return portMenuOpen() || portMsgWaiting(&sel, &num, &tag) != 0 || portMenuLayerOn(0);
}

/* The menu layer's frames (port/include/port/menu_buttons.h): a menu's mark this frame turns it on for its
 * ports from the next pad read until two reads after the last mark */
static u32 sLayerClock = 1;
static u32 sMarkAt[4]; /* the clock at a port's last mark (0 none) */
static u32 sTextAt[4]; /* and at its last message's */
static int sLayerOn[4];
static int sPictures[4]; /* on for a menu (not a message alone): the button pictures follow it */

static void mark(u32* at, unsigned ports) {
    int p;
    if (gPortCompanionMenu) { /* the second screen's menu: its presenter's keys, not this game's pads */
        return;
    }
    for (p = 0; p < 4; p++) {
        if (ports & (1u << p)) {
            at[p] = sLayerClock;
        }
    }
}

void portMenuMark(unsigned ports) {
    mark(sMarkAt, ports);
}

void portMenuMarkText(unsigned ports) {
    mark(sTextAt, ports);
}

int portMenuLayerPictures(int port) {
    return port >= 0 && port < 4 && sPictures[port];
}

void portMenuLayerFrame(void) {
    static int sOff = -1;
    int p;
    if (sOff < 0) {
        sOff = getenv("TTYD_MENU_LAYER_OFF") != NULL; /* tests: the game's buttons as 0.5.0 had them, before */
    }
    sLayerClock++;
    for (p = 0; p < 4; p++) {
        const int menu = !sOff && sMarkAt[p] != 0 && sLayerClock - sMarkAt[p] <= 2;
        const int on = menu || (!sOff && sTextAt[p] != 0 && sLayerClock - sTextAt[p] <= 2);
        sPictures[p] = menu;
        if (on != sLayerOn[p] && getenv("TTYD_INPUT") != NULL) { /* scripted runs (tests) */
            printf("[menuback] port %d: menu layer %s\n", p + 1, on ? "on" : "off");
            fflush(stdout);
        }
        sLayerOn[p] = on;
    }
}

int portMenuLayerOn(int port) {
    return port >= 0 && port < 4 && sLayerOn[port];
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
        const int kind = __atomic_exchange_n(&sTapPending, 0, __ATOMIC_ACQUIRE);
        sKind = kind != 0 && now - sTapFrame <= 10 ? kind : 0;
        sX = sTapX;
        sY = sTapY;
        if (sKind == 1 && getenv("TTYD_INPUT") != NULL) { /* scripted runs (tests) */
            printf("[menu] tap at %.0f, %.0f\n", sX, sY);
            fflush(stdout);
        }
    }
    *gx = sX;
    *gy = sY;
    return sKind;
}

int portMenuTapPeek(float* gx, float* gy) {
    if (gPortCompanionMenu || __atomic_load_n(&sTapPending, __ATOMIC_ACQUIRE) != 1 ||
        VIGetRetraceCount() - sTapFrame > 10) {
        return 0;
    }
    *gx = sTapX;
    *gy = sTapY;
    return 1;
}

void portMenuTapTake(void) {
    __atomic_store_n(&sTapPending, 0, __ATOMIC_RELEASE);
    if (getenv("TTYD_INPUT") != NULL) { /* scripted runs (tests) */
        printf("[menu] tap taken by the players' bars\n");
        fflush(stdout);
    }
}

/* the presses asked for, applied over the pad's read: B held 2 frames, or
 * the stick tilted 2 frames, then 2 frames of nothing, so each is a new press */
static volatile int sBackPending, sWheelPending;
static int sHold, sStick;
static unsigned sInjected;                 /* the B held by the last read (portMenuApplyPad), port 0's */
static volatile unsigned sVirtualHeld[4]; /* the touch controls', by port */

unsigned portMenuVirtualHeld(int port) {
    if (port < 0 || port >= 4) {
        return 0;
    }
    return sVirtualHeld[port] | (port == 0 ? sInjected : 0u);
}

void portMenuSetVirtualHeld(int port, unsigned buttons) {
    int i;
    for (i = 0; i < 4; i++) {
        sVirtualHeld[i] = i == port ? buttons : 0u;
    }
}

void portMenuBack(void) {
    if (portMenuBackOpen()) {
        sBackPending = 1;
    }
}

void portMenuWheel(int notches) {
    if (portMenuOpen()) {
        __atomic_add_fetch(&sWheelPending, notches, __ATOMIC_SEQ_CST);
    }
}

/* A DualSense's or DualShock 4's touchpad (port/settings/features/
 * controller_light.cpp): a finger's move (0-1 across the pad) while a menu is
 * up, the stick tilted against it the next frame, as a phone's list follows
 * the finger: the map pans and the Journal and lists scroll under it. */
static float sPadDX, sPadDY;

void portMenuTouchpad(float dx, float dy) {
    if (portMenuOpen()) {
        sPadDX += dx;
        sPadDY += dy;
    }
}

static s8 touchStick(float d) {
    const float v = d * 1400.0f; /* a tenth of the pad in a frame: the stick all the way */
    return (s8)(v > 127.0f ? 127 : v < -127.0f ? -127 : v);
}

void portMenuApplyPad(PADStatus* pad) {
    if (sHold == 0 && (sPadDX != 0.0f || sPadDY != 0.0f)) {
        const s8 x = touchStick(-sPadDX), y = touchStick(sPadDY); /* (the pad's y grows down, the stick's up) */
        sPadDX = sPadDY = 0.0f;
        if (portMenuOpen() && (x != 0 || y != 0)) {
            pad[0].err = PAD_ERR_NONE;
            pad[0].stickX = x;
            pad[0].stickY = y;
        }
    }
    if (sHold == 0) {
        if (sBackPending) {
            sBackPending = 0;
            sWheelPending = 0;
            sStick = 0;
            sHold = 4;
        } else if (sWheelPending != 0) {
            sStick = sWheelPending > 0 ? 127 : -127; /* the wheel up: the stick up */
            __atomic_add_fetch(&sWheelPending, sStick > 0 ? -1 : 1, __ATOMIC_SEQ_CST);
            sHold = 4;
        }
    }
    sInjected = 0;
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
            sInjected = PAD_BUTTON_B; /* (the game's B, never a controller's moved: portMenuButtons) */
        }
    }
}

int portMenuTapRow(struct WinMgrSelect* sel, struct WinMgrEntry* list) {
    f32 gx, gy;
    s32 i;
    int hover;
    sOpenFrame = VIGetRetraceCount();
    if (!__atomic_load_n(&sTapPending, __ATOMIC_RELAXED)) {
        return -1;
    }
    hover = __atomic_exchange_n(&sTapPending, 0, __ATOMIC_ACQUIRE) == 2;
    if (VIGetRetraceCount() - sTapFrame > 10) {
        return -1; /* stale */
    }
    gx = sTapX;
    gy = sTapY;
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
