/* New Game's way in, asked in the game's file menu (port/include/port/gameplus.h, Start in Rogueport).
 *
 * With Start in Rogueport offered (the setting on Offer or Always), {A} on a new file opens a small window
 * over the file menu: "Start from the beginning" and "Start in Rogueport", each with a line saying what it
 * is, and Back. Always puts the cursor on Rogueport first; Offer on the beginning. {B} or Back closes it
 * and the file menu goes on as before. The file's own panel keeps the original "New Game".
 *
 * Drawn the way the file menu draws itself (its white window, black letters and hand), as the co-op save
 * copy's offer is (coop_copy_pc.c), in the middle of the picture over the menu, dimmed, above the menu's
 * bottom buttons. */
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "port/gameplus.h"
#include "port/menu_tap.h"
#include "port/platform.h"
#include "port/text_fit.h"
#include "port/tr.h"
#include <demo/DEMOPad.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>
#ifdef TTYD_BACKEND_AURORA
#include "driver/windowdrv.h"
#include "port/party.h"
#include "sdk/texPalette.h"
#include "win/win_main.h"
#include <math.h>
void* portLoadMenuTpl(void); /* seq_load.c: the file menu's pictures */
#endif

#define SFX_MOVE ((const char*)9)
#define SFX_OK ((const char*)0x12)
#define SFX_NO ((const char*)0x13)

enum { ROW_BEGINNING, ROW_ROGUEPORT, ROW_BACK, ROWS };

static struct {
    int up;
    int row;
    char title[64]; /* the menu's own "New Game", in the game's encoding */
} S;

/* (English, translated as drawn: port/assets/lang/<code>/quick-start.txt) */
static const char* const kName[ROWS] = {"Start from the beginning", "Start in Rogueport", "Back"};
static const char* const kLine[ROWS] = {"The whole opening: the letter, the ship and the harbour.",
                                        "In the plaza with Goombella: the opening done.", NULL};

int portNewFilePickUp(void) {
    return S.up;
}

void portNewFilePickOpen(const char* title, int rogueportFirst) {
    S.up = 1;
    S.row = rogueportFirst ? ROW_ROGUEPORT : ROW_BEGINNING;
    snprintf(S.title, sizeof(S.title), "%s", title != NULL ? title : "");
    printf("[gameplus] new file: the way in asked (%s first)\n", rogueportFirst ? "Rogueport" : "the beginning");
    fflush(stdout);
}

static void pickClose(const char* why) {
    S.up = 0;
    printf("[gameplus] new file: %s\n", why);
    fflush(stdout);
}

#ifdef TTYD_BACKEND_AURORA
#define WIN_W 460.0f
#define WIN_X (-WIN_W / 2.0f)
#define WIN_Y 118.0f
#define WIN_H 214.0f
#define ROW_X (WIN_X + 70.0f)
#define ROW_Y (WIN_Y - 58.0f)
#define ROW_STEP 52.0f
#define TEXT_W (WIN_W - 70.0f - 26.0f)

/* one line in the file menu's black, its top at y, squeezed to fit w; centre: x is its middle */
static void textFit(float x, float y, float w, float scale, GXColor c, const char* text, int centre) {
    const float full = (float)FontGetMessageWidth(text) * scale;
    float sx = scale;
    if (full > w && full > 0.0f) {
        portTextFit("new-file-pick", text, full, w, 1);
        sx = scale * w / full;
    }
    if (centre) {
        x -= (full > w ? w : full) / 2.0f;
    }
    winFontInit();
    winFontSet((Vec){x, y, 0.0f}, (Vec){sx, scale, 1.0f}, c, "%s", text);
}

static float rowY(int i) {
    return ROW_Y - (float)i * ROW_STEP;
}

static void disp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const GXColor black = {0x20, 0x20, 0x20, 0xFF}, grey = {0x60, 0x60, 0x60, 0xFF};
    TEXPalette* tpl = (TEXPalette*)portLoadMenuTpl();
    int i;
    (void)camId;
    (void)param;
    if (!S.up) {
        return;
    }
    /* the menu behind, dimmed: this is the question now */
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x70}, -400.0f, 260.0f, 800.0f, 520.0f, 4.0f);
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x50}, WIN_X + 5.0f, WIN_Y - 5.0f, WIN_W, WIN_H, 20.0f);
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, WIN_X, WIN_Y, WIN_W, WIN_H, 20.0f);
    textFit(0.0f, WIN_Y - 14.0f, WIN_W - 48.0f, 0.85f, black, S.title, 1);
    for (i = 0; i < ROWS; i++) {
        const float y = rowY(i) + (i == ROW_BACK ? 6.0f : 0.0f);
        textFit(ROW_X, y, TEXT_W, 0.7f, black, portTrGame(kName[i]), 0);
        if (kLine[i] != NULL) {
            textFit(ROW_X, y - 24.0f, TEXT_W, 0.5f, grey, portTrGame(kLine[i]), 0);
        }
        if (i != S.row) {
            continue;
        }
        if (tpl != NULL) {
            const float bob = 2.0f * sinf((float)gp->retraceCount * 0.25f);
            winTexInit(tpl);
            winTexSet(0, (Vec){ROW_X - 26.0f + bob + 2.0f, y - 13.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      (GXColor){0, 0, 0, 0x80});
            winTexSet(0, (Vec){ROW_X - 26.0f + bob, y - 11.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        } else {
            portPartyCursor(ROW_X - 22.0f, y - 11.0f, 0.8f); /* (the game's list hand: its font has no ">") */
        }
    }
    portRandSetState(seed);
}

/* the pointer over a row: it gets the cursor, a click is A; a click outside the window is B */
static int pointer(void) {
    float gx, gy;
    int i;
    const int kind = portMenuPoint(&gx, &gy);
    if (kind == 0) {
        return 0;
    }
    if (gx < WIN_X || gx > WIN_X + WIN_W || gy > WIN_Y || gy < WIN_Y - WIN_H) {
        return kind == 1 ? PAD_BUTTON_B : 0;
    }
    for (i = 0; i < ROWS; i++) {
        const float y = rowY(i);
        if (gy <= y + 6.0f && gy > y - ROW_STEP + 6.0f) {
            if (S.row != i) {
                S.row = i;
                psndSFXOn(SFX_MOVE);
            }
            return kind == 1 ? PAD_BUTTON_A : 0;
        }
    }
    return 0;
}
#endif

int portNewFilePick(unsigned trg, unsigned dir) {
    if (!S.up) {
        return 0;
    }
#ifdef TTYD_BACKEND_AURORA
    dispEntry(CAMERA_2D, 960.0f, 1, disp, NULL);
    if (trg == 0 && dir == 0) {
        trg = (unsigned)pointer();
    }
#endif
    if (dir & DEMO_STICK_UP) {
        S.row = (S.row + ROWS - 1) % ROWS;
        psndSFXOn(SFX_MOVE);
    } else if (dir & DEMO_STICK_DOWN) {
        S.row = (S.row + 1) % ROWS;
        psndSFXOn(SFX_MOVE);
    } else if ((trg & PAD_BUTTON_B) || ((trg & PAD_BUTTON_A) && S.row == ROW_BACK)) {
        psndSFXOn(SFX_NO);
        pickClose("back to the files");
        return 4;
    } else if (trg & PAD_BUTTON_A) {
        const int rogueport = S.row == ROW_ROGUEPORT;
        pickClose(rogueport ? "start in Rogueport" : "start from the beginning");
        return rogueport ? 3 : 2;
    }
    return 1;
}
