/* The pause menu's Settings tab (port/include/port/pause_settings.h): the
 * settings in the game's own menu, so nobody has to know about F1 or
 * Back/Select. A sixth tab after the Journal, in the game's windows and font:
 * every plain setting of the settings page (an on/off, a choice, a number;
 * portSettingsControls), under its page tab's name, each changed with left and
 * right (A: the next value) where it is, the selected one's explanation in the
 * help box, and "All settings" at the bottom, which opens the full page over
 * the menu (closing it comes back here) for the rest (remapping, mods, saves'
 * tools, practice).
 *
 * src/window/win_root.c adds the tab (TARGET_PC blocks): its place in the
 * row, its label, its page (menu id 5, active 16) and its help text, which
 * msgSearch takes from portPauseSettingsMsg. */
#include "port/pause_settings.h"
#include "port/settings.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void portSettingsToggle(void); /* port/settings/settings_ui.cpp: the full page */

enum { KIND_HEADING = -1, KIND_RATE = 3, KIND_ALL = 4 };
#define MAX_ROWS 256
#define VISIBLE 10
#define ROW_X (-250.0f)
#define ROW_TOP 132.0f
#define ROW_STEP 26.0f
/* the help box holds about 30 letters a line (15 of Japanese) */
#define HELP_COLS 30

typedef struct Row {
    int kind; /* KIND_HEADING, a control's (0-2), KIND_RATE or KIND_ALL */
    const char* label;
    PortSettingsControl c;
} Row;

static Row sRows[MAX_ROWS];
static int sCount;
static s32 sRow, sTop;
static char sHelp[512];

static const char* const kRates[] = {"60", "120", "144", "240", "display"};
static const char* const kRateNames[] = {"60 Hz", "120 Hz", "144 Hz", "240 Hz", "The display's rate"};

float portPauseTabX(int n, int i) {
    return n >= 6 ? i * 90.0f - 225.0f : i * 105.0f - 205.0f;
}

float portPauseTabScale(int n) {
    return n >= 6 ? 0.86f : 1.0f;
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

/* the rows: each page tab's name, then its controls (Display's frame rate,
 * a string, first), then All settings; the cursor kept on the same setting */
static void build(void) {
    static PortSettingsControl controls[MAX_ROWS];
    /* (by label and kind: a tab and its only setting can share a name, and one string) */
    const char* was = sCount > 0 && sRow < sCount ? sRows[sRow].label : NULL;
    const int wasKind = sCount > 0 && sRow < sCount ? sRows[sRow].kind : 0;
    const char* tab = NULL;
    const int n = portSettingsControls(controls, MAX_ROWS - 24);
    int i;
    sCount = 0;
    for (i = 0; i < n && sCount < MAX_ROWS - 3; i++) {
        if (tab == NULL || strcmp(tab, controls[i].tab) != 0) {
            tab = controls[i].tab;
            sRows[sCount].kind = KIND_HEADING;
            sRows[sCount++].label = tab;
            if (strcmp(tab, "Display") == 0) {
                sRows[sCount].kind = KIND_RATE;
                sRows[sCount].label = "Frame rate";
                sRows[sCount].c.note = "Extra frames between the game's own 60: smoother motion.";
                sCount++;
            }
        }
        sRows[sCount].kind = controls[i].kind;
        sRows[sCount].label = controls[i].label;
        sRows[sCount++].c = controls[i];
    }
    sRows[sCount].kind = KIND_ALL;
    sRows[sCount].label = "All settings...";
    sRows[sCount++].c.note = "Every setting: also remapping, mods, saves' tools and practice.";
    for (i = 0; was != NULL && i < sCount; i++) {
        if (sRows[i].label == was && sRows[i].kind == wasKind) {
            sRow = i;
            return;
        }
    }
    if (sRow >= sCount || sRows[sRow].kind == KIND_HEADING) {
        sRow = 1;
    }
}

static void valueText(const Row* r, char* out, size_t size) {
    const PortSettingsControl* c = &r->c;
    out[0] = '\0';
    if (r->kind == KIND_RATE) {
        snprintf(out, size, "%s", portTrGame(kRateNames[rateIndex()]));
    } else if (r->kind == 0 || r->kind == 1) {
        const int v = *c->value;
        snprintf(out, size, "%s", v >= 0 && v < c->count && c->choices != NULL ? portTrGame(c->choices[v]) : "?");
    } else if (r->kind == 2) {
        snprintf(out, size, c->format != NULL ? c->format : "%d", *c->value);
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

/* the help box shows the row's note (winMsgEntry keeps the text while the tag
 * is the same, so each row has its own tag) and the hand points at the row */
static void pointAt(WindowWork* wp) {
    static char tags[MAX_ROWS][24];
    const Row* r = &sRows[sRow];
    if (tags[sRow][0] == '\0') {
        snprintf(tags[sRow], sizeof(tags[sRow]), "pc_menu_settings_%d", (int)sRow);
    }
    portPauseWrap(portTrGame(r->c.note != NULL ? r->c.note : r->label), sHelp, sizeof(sHelp));
    wp->helpTgtX = ROW_X - 30.0f;
    wp->helpTgtY = ROW_TOP - (sRow - sTop) * ROW_STEP - 8.0f;
    winMsgEntry(wp, 0, tags[sRow], 0);
}

static void move(int dir) {
    do {
        sRow = (sRow + dir + sCount) % sCount;
    } while (sRows[sRow].kind == KIND_HEADING);
    /* the view follows, showing a heading above its first setting */
    if (sRow - 1 < sTop) {
        sTop = sRow - 1 < 0 ? 0 : sRow - 1;
    }
    if (sRow >= sTop + VISIBLE) {
        sTop = sRow - VISIBLE + 1;
    }
}

void portPauseSettingsInit(WindowWork* wp) {
    (void)wp;
    sCount = 0;
    sRow = 1;
    sTop = 0;
    build();
}

void portPauseSettingsInit2(WindowWork* wp) {
    wp->flags |= WIN_FLAG_SUBMENU;
    build();
    pointAt(wp);
}

s32 portPauseSettingsMain(WindowWork* wp) {
    build(); /* a feature's setting shown or hidden by another (widescreen's HUD) */
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        return -1;
    }
    if (wp->buttonTrg & PAD_BUTTON_A) {
        if (sRows[sRow].kind == KIND_ALL) {
            psndSFXOn((const char*)0x20012);
            portSettingsToggle();
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
        if (change(&sRows[sRow], (wp->dirRep & DEMO_STICK_LEFT) ? -1 : 1)) {
            psndSFXOn((const char*)0x20005);
        }
    }
    pointAt(wp);
    return 0;
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void fitText(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* text, int right) {
    const f32 full = (f32)FontGetMessageWidth(text);
    const f32 k = full * s > w ? w / full : s;
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", text);
}

void portPauseSettingsDisp(s32 camId, WindowWork* wp, s32 idx) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor heading = {0x5A, 0x46, 0x9C, 0xFF};
    const f32 x = wp->menu[idx].x;
    const f32 y = wp->menu[idx].y;
    const int inside = wp->active == PORT_PAUSE_SETTINGS_TAB + 11;
    int i;
    (void)camId;

    winBgGX(wp, PORT_PAUSE_SETTINGS_TAB, x, y);
    winKirinukiGX(wp, 0, x - 270.0f, y + ROW_TOP + 12.0f, 540.0f, VISIBLE * ROW_STEP + 10.0f);
    winFontInit();
    for (i = sTop; i < sCount && i < sTop + VISIBLE; i++) {
        const Row* r = &sRows[i];
        const f32 ry = y + ROW_TOP - (i - sTop) * ROW_STEP;
        const GXColor c = !inside || i == sRow ? black : grey;
        char value[64];
        if (r->kind == KIND_HEADING) {
            fitText(x - 262.0f, ry, 300.0f, 0.75f, heading, portTrGame(r->label), 0);
            continue;
        }
        fitText(x + ROW_X, ry, 300.0f, 0.85f, c, portTrGame(r->label), 0);
        valueText(r, value, sizeof(value));
        if (value[0] != '\0') {
            fitText(x + 255.0f, ry, 190.0f, 0.85f, c, value, 1);
        }
    }
    /* where the view is in the list: a position, as the game's lists show more below */
    if (sCount > VISIBLE) {
        char pos[16];
        snprintf(pos, sizeof(pos), "%d/%d", (int)sRow + 1, sCount);
        fitText(x + 262.0f, y - 140.0f, 80.0f, 0.6f, grey, pos, 1);
    }
}

/* the tab's label: a tinted tab (the page panel's shape and color) with
 * "Settings" on it in the game's font, white on a dark outline */
void portPauseSettingsTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    const char* kLabel = portTrGame("Settings");
    /* the tab's size, and its middle below the label's top as the game's tabs sit */
    const f32 w = 108.0f * scale, h = 40.0f * scale, cy = y - 6.0f * scale;
    const GXColor shadow = {0x30, 0x20, 0x50, c.a};
    const u8 k = c.r; /* 0xFF selected, 0x80 not: the tab darkens with its label */
    Mtx m;
    GXTexObj obj;
    f32 tw;
    int dx, dy;

    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_FALSE);
    GXSetAlphaCompare(GX_GEQUAL, 0x80, GX_AOP_OR, GX_NEVER, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, (GXColor){(u8)(0x6E * k / 0xFF), (u8)(0x5A * k / 0xFF), (u8)(0xB4 * k / 0xFF), 0xFF});
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

    winFontInit();
    /* as wide as the tab at most: narrower letters for a longer word (Einstellungen) */
    {
        const f32 full = (f32)FontGetMessageWidth(kLabel);
        const f32 sx = full * 0.95f * scale > w - 10.0f ? (w - 10.0f) / full : 0.95f * scale;
        const f32 sy = 0.95f * scale;
        tw = sx * full;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                if (dx != 0 || dy != 0) {
                    winFontSet((Vec){x - tw / 2 + dx * 1.5f, y + 13.0f * scale + dy * 1.5f, 0.0f}, (Vec){sx, sy, sy},
                               shadow, kLabel);
                }
            }
        }
        winFontSet((Vec){x - tw / 2, y + 13.0f * scale, 0.0f}, (Vec){sx, sy, sy}, (GXColor){c.r, c.g, c.b, 0xFF},
                   kLabel);
    }
}

const char* portPauseSettingsMsg(const char* tag) {
    if (strncmp(tag, "pc_menu_", 8) != 0) {
        return NULL;
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
