/* The Wardrobe's colour picker (port/include/port/pause_forms.h), in the
 * game's windows: which part, a grid of swatches (the Wardrobe's named
 * colours and a few more), and red, green and blue as bars changed with left
 * and right. Each change is the part's own colour (its _rgb key) and puts it
 * on (Custom, port/include/port/cosmetics.h), so the Wardrobe's stage beside
 * it shows the result as it changes. B goes back to the Wardrobe's rows.
 *
 * portPauseColourOpen puts the same picker on any one colour (the
 * controller's light, on the Settings tab): its name where the part is, the
 * panel in the middle of the page. */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/cosmetics.h"
#include "port/menu_tap.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "manager/fontmgr.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

/* the panel beside the stage, as the Wardrobe's rows; or in the middle (one colour) */
#define PANEL_X sPanelX
#define PANEL_R (sPanelX + 288.0f)
#define TOP 132.0f
#define SWATCH 26.0f
#define COLS 8

enum { ROW_PART, ROW_SWATCH1, ROW_SWATCH2, ROW_RED, ROW_GREEN, ROW_BLUE, ROWS };

static const unsigned kSwatches[] = {
    0xE62814, 0x28AA32, 0xFAD21E, 0x6E2896, 0xEBEBEB, 0x2846E6, 0x1E2878, 0x5AAAF0, /* the Wardrobe's own */
    0xF56EB4, 0xF58214, 0x282830, 0xD7A528, 0x824B23, 0x1E9A8C, 0xB496E6, 0xA0A8B4, /* and teal, lilac, silver */
};
#define SWATCHES ((int)(sizeof(kSwatches) / sizeof(kSwatches[0])))

static int sSlot, sRow, sCol;
static char sHelp[256];
static f32 sPanelX = -30.0f;
/* portPauseColourOpen's colour, or NULL: the Wardrobe's slots */
static const char* sLabel;
static unsigned (*sGet)(void);
static void (*sSet)(unsigned rgb);

static unsigned colourNow(void) {
    return sGet != NULL ? sGet() & 0xFFFFFF : portCosmeticsSlotRgb(sSlot);
}

static void colourSet(unsigned rgb) {
    if (sGet != NULL) {
        sSet(rgb);
    } else {
        portCosmeticsSetRgb(sSlot, rgb);
    }
}

static void pickerOpen(int slot) {
    if (slot >= 0) { /* (the Wardrobe's) */
        sGet = NULL;
        sSet = NULL;
        sLabel = NULL;
        sPanelX = -30.0f;
    }
    sSlot = slot >= 0 && slot < portCosmeticsSlotCount() ? slot : 0;
    sRow = ROW_SWATCH1;
    sCol = 0;
}

int portPausePickerSlot(void) {
    return sSlot;
}

void portPauseColourOpen(const char* label, unsigned (*get)(void), void (*set)(unsigned rgb)) {
    sLabel = label;
    sGet = get;
    sSet = set;
    sPanelX = -144.0f;
    portPauseOpenPage(&gPortPausePicker, -1);
}

static void setChannel(int ch, int dir) {
    const unsigned rgb = colourNow();
    const int shift = 16 - 8 * ch;
    int v = (int)((rgb >> shift) & 0xFF) + dir * 8;
    v = v < 0 ? 0 : v > 255 ? 255 : v;
    colourSet((rgb & ~(0xFFu << shift)) | ((unsigned)v << shift));
}

/* the mouse and touch: a swatch or a bar under the pointer; a click puts it on */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int r, c;
    if (kind == 0) {
        return 0;
    }
    for (r = ROW_SWATCH1; r <= ROW_SWATCH2; r++) {
        const f32 y = TOP - 40.0f - (r - ROW_SWATCH1) * (SWATCH + 6.0f);
        for (c = 0; c < COLS; c++) {
            const f32 x = PANEL_X + c * (SWATCH + 8.0f);
            if (gx >= x && gx < x + SWATCH && gy <= y && gy > y - SWATCH) {
                if (sRow == r && sCol == c) {
                    return kind == 1;
                }
                sRow = r;
                sCol = c;
                psndSFXOn((const char*)0x20005);
                return 0;
            }
        }
    }
    return 0;
}

static int main_(WindowWork* wp) {
    const int n = portCosmeticsSlotCount();
    const int click = pointer();
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (click || (wp->buttonTrg & PAD_BUTTON_A)) {
        if (sRow == ROW_SWATCH1 || sRow == ROW_SWATCH2) {
            psndSFXOn((const char*)0x20012);
            colourSet(kSwatches[(sRow - ROW_SWATCH1) * COLS + sCol]);
        } else if (sRow == ROW_PART && sGet == NULL) {
            psndSFXOn((const char*)0x20005);
            sSlot = (sSlot + 1) % n;
        }
    } else if (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        sRow = (sRow + ((wp->dirRep & DEMO_STICK_UP) ? -1 : 1) + ROWS) % ROWS;
        psndSFXOn((const char*)0x20005);
    } else if (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int dir = (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1;
        if (sRow == ROW_PART) {
            sSlot = sGet != NULL ? sSlot : (sSlot + dir + n) % n;
        } else if (sRow == ROW_SWATCH1 || sRow == ROW_SWATCH2) {
            sCol = (sCol + dir + COLS) % COLS;
        } else {
            setChannel(sRow - ROW_RED, dir);
        }
        psndSFXOn((const char*)0x20005);
    }
    /* the hand at the row */
    if (sRow == ROW_SWATCH1 || sRow == ROW_SWATCH2) {
        wp->helpTgtX = PANEL_X + sCol * (SWATCH + 8.0f) - 12.0f;
        wp->helpTgtY = TOP - 40.0f - (sRow - ROW_SWATCH1) * (SWATCH + 6.0f) - SWATCH / 2.0f;
    } else {
        wp->helpTgtX = PANEL_X - 24.0f;
        wp->helpTgtY = (sRow == ROW_PART ? TOP : TOP - 110.0f - (sRow - ROW_RED) * 30.0f) - 8.0f;
    }
    return 0;
}

static const char* help(void) {
    const char* text = sRow == ROW_PART && sGet != NULL                ? "The colour it is now."
                       : sRow == ROW_PART                              ? "Which part the colour is for: left and right change it."
                       : sRow == ROW_SWATCH1 || sRow == ROW_SWATCH2 ? "A puts this colour on."
                                                                    : "Left and right make it more or less; it's on as it changes.";
    portPauseWrap(portTrGame(text), sHelp, sizeof(sHelp));
    return sHelp;
}

static void box(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    Mtx m;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, c);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXConcat(camGetPtr(CAMERA_2D)->view, m, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXPosition3f32(0.0f, -h, 0.0f);
    GXPosition3f32(w, -h, 0.0f);
    GXPosition3f32(w, 0.0f, 0.0f);
}

static void text(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* t, int right) {
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 k = full * s > w ? w / full : s;
    winFontInit();
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", t);
}

static void disp(WindowWork* wp, float x, float y) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor frame = {0x30, 0x28, 0x20, 0xFF};
    const unsigned rgb = colourNow();
    static const char* const kChannels[] = {"Red", "Green", "Blue"};
    char num[8];
    int i;
    winKirinukiGX(wp, 0, x + PANEL_X - 28.0f, y + TOP + 12.0f, PANEL_R - PANEL_X + 40.0f, 270.0f);
    /* the part, and its colour now */
    text(x + PANEL_X, y + TOP, 120.0f, 0.8f, sRow == ROW_PART ? black : grey,
         portTrGame(sGet != NULL ? "Colour" : "Part"), 0);
    box(x + PANEL_R - 22.0f, y + TOP - 2.0f, 22.0f, 18.0f, frame);
    box(x + PANEL_R - 20.0f, y + TOP - 4.0f, 18.0f, 14.0f, (GXColor){(u8)(rgb >> 16), (u8)(rgb >> 8), (u8)rgb, 0xFF});
    text(x + PANEL_R - 30.0f, y + TOP, 170.0f, 0.8f, sRow == ROW_PART ? black : grey,
         portTrGame(sGet != NULL ? sLabel : portCosmeticsSlotLabel(sSlot)), 1);
    /* the swatches, the selected one framed in white */
    for (i = 0; i < SWATCHES; i++) {
        const int r = ROW_SWATCH1 + i / COLS, c = i % COLS;
        const f32 sx = x + PANEL_X + c * (SWATCH + 8.0f), sy = y + TOP - 40.0f - (i / COLS) * (SWATCH + 6.0f);
        const unsigned v = kSwatches[i];
        if (sRow == r && sCol == c) {
            box(sx - 3.0f, sy + 3.0f, SWATCH + 6.0f, SWATCH + 6.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        }
        box(sx, sy, SWATCH, SWATCH, frame);
        box(sx + 2.0f, sy - 2.0f, SWATCH - 4.0f, SWATCH - 4.0f, (GXColor){(u8)(v >> 16), (u8)(v >> 8), (u8)v, 0xFF});
    }
    /* red, green and blue: bars as the game's gauges, a dark frame with the amount filled */
    for (i = 0; i < 3; i++) {
        const f32 by = y + TOP - 110.0f - i * 30.0f;
        const int v = (int)((rgb >> (16 - 8 * i)) & 0xFF);
        const GXColor fill = {(u8)(i == 0 ? 0xE6 : 0x30), (u8)(i == 1 ? 0xC8 : 0x30), (u8)(i == 2 ? 0xF0 : 0x30), 0xFF};
        const GXColor c = sRow == ROW_RED + i ? black : grey;
        text(x + PANEL_X, by, 70.0f, 0.8f, c, portTrGame(kChannels[i]), 0);
        box(x + PANEL_X + 74.0f, by - 2.0f, 134.0f, 16.0f, frame);
        box(x + PANEL_X + 76.0f, by - 4.0f, 130.0f, 12.0f, (GXColor){0xD8, 0xD0, 0xC0, 0xFF});
        box(x + PANEL_X + 76.0f, by - 4.0f, 130.0f * v / 255.0f, 12.0f, fill);
        snprintf(num, sizeof(num), "%d", v);
        text(x + PANEL_R, by, 44.0f, 0.8f, c, num, 1);
    }
}

const PortPausePage gPortPausePicker = {pickerOpen, main_, disp, help};
