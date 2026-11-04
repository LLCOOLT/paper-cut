/* The pause menu's Settings tab (port/include/port/pause_settings.h): the
 * settings in the game's own menu, so nobody has to know about F1 or
 * Back/Select. A sixth tab after the Journal, in the game's windows and font:
 * every plain setting of the settings page (an on/off, a choice, a number;
 * portSettingsControls), under its page tab's name, each changed with left and
 * right (A: the next value) where it is, the selected one's explanation in the
 * help box, and "All settings" at the bottom, which opens the full page over
 * the menu (closing it comes back here) for the rest (remapping, mods, saves'
 * tools, practice). A setting that applies from the next start, once changed,
 * brings a "Restart now" row (port/include/port/restart.h), which restarts
 * after a second A. The help box shows a note's first sentence (its first
 * clause when that is still longer than the box's two lines); the page has
 * the whole note.
 *
 * src/window/win_root.c adds the tab (TARGET_PC blocks): its place in the
 * row, its label, its page (menu id 5, active 16) and its help text, which
 * msgSearch takes from portPauseSettingsMsg. */
#include "port/pause_settings.h"
#include "port/restart.h"
#include "port/settings.h"
#include "port/speech.h"
#include "port/tr.h"
#include "port/view.h"
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

enum { KIND_HEADING = -1, KIND_RATE = 3, KIND_ALL = 4, KIND_RESTART = 5, KIND_PHOTO = 6 };
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
static int sConfirm; /* the Restart now row's first A was pressed */

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
    portRestartPending(); /* (the values this run began with, before any is changed here) */
    sCount = 0;
    for (i = 0; i < n && sCount < MAX_ROWS - 3; i++) {
        if (tab == NULL || strcmp(tab, controls[i].tab) != 0) {
            tab = controls[i].tab;
            sRows[sCount].kind = KIND_HEADING;
            sRows[sCount++].label = tab;
            if (strcmp(tab, "Display") == 0) {
                sRows[sCount].kind = KIND_RATE;
                sRows[sCount].label = "Frame rate";
                sRows[sCount].c.flags = 0;
                sRows[sCount].c.note = "Extra frames between the game's own 60: smoother motion.";
                sCount++;
            }
        }
        sRows[sCount].kind = controls[i].kind;
        sRows[sCount].label = controls[i].label;
        sRows[sCount++].c = controls[i];
    }
    if (portRestartSupported() && portRestartPending()) {
        sRows[sCount].kind = KIND_RESTART;
        sRows[sCount].label = "Restart now";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Press A twice to restart the game for these settings; quick resume brings you "
                                 "back here.";
    } else {
        sConfirm = 0;
    }
    if (portPhotoAvailable()) { /* (port/settings/features/photo_mode.cpp) */
        sRows[sCount].kind = KIND_PHOTO;
        sRows[sCount].label = "Photo mode";
        sRows[sCount].c.flags = 0;
        sRows[sCount++].c.note = "Closes the menu and holds the game: the sticks move the camera, A takes a picture, "
                                 "Y hides the HUD, B goes back.";
    }
    sRows[sCount].kind = KIND_ALL;
    sRows[sCount].label = "All settings...";
    sRows[sCount].c.flags = 0;
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

/* text's first sentence (the game's encoding): up to its first ". " or, in
 * the Japanese text, its first full stop (a full-width one), kept; all of it
 * when it has one sentence */
static void firstSentence(const char* text, char* out, size_t size) {
    const char* p = text;
    size_t n;
    while (*p != '\0') {
        if (sjisLead((unsigned char)p[0]) && p[1] != '\0') {
            /* Shift-JIS 0x8142 and 0x8144: the full stops */
            const int stop = (unsigned char)p[0] == 0x81 && ((unsigned char)p[1] == 0x42 || (unsigned char)p[1] == 0x44);
            p += 2;
            if (stop && *p != '\0') {
                break;
            }
            continue;
        }
        if (p[0] == '.' && p[1] == ' ') {
            p++;
            break;
        }
        p++;
    }
    n = (size_t)(p - text) < size - 1 ? (size_t)(p - text) : size - 1;
    memcpy(out, text, n);
    out[n] = '\0';
    /* still more than the box's two lines: up to its first "; ", made a full stop */
    if (n > 2 * HELP_COLS && !sjisLead((unsigned char)0x81)) {
        char* semi = strstr(out, "; ");
        if (semi != NULL && semi - out >= 12) {
            semi[0] = '.';
            semi[1] = '\0';
        }
    }
}

/* the help box shows the row's note (winMsgEntry keeps the text while the tag
 * is the same, so each row has its own tag) and the hand points at the row */
static void pointAt(WindowWork* wp) {
    static char tags[MAX_ROWS][24];
    const Row* r = &sRows[sRow];
    char text[512];
    if (tags[sRow][0] == '\0') {
        snprintf(tags[sRow], sizeof(tags[sRow]), "pc_menu_settings_%d", (int)sRow);
    }
    wp->helpTgtX = ROW_X - 30.0f;
    wp->helpTgtY = ROW_TOP - (sRow - sTop) * ROW_STEP - 8.0f;
    if (r->kind == KIND_RESTART && sConfirm) {
        /* (a tag of its own: the text changes on the same row) */
        portPauseWrap(portTrGame("Press A again to restart."), sHelp, sizeof(sHelp));
        winMsgEntry(wp, 0, "pc_menu_settings_restart", 0);
        return;
    }
    firstSentence(portTrGame(r->c.note != NULL ? r->c.note : r->label), text, sizeof(text));
    if (r->kind >= 0 && r->kind <= 2 && (r->c.flags & PORT_SETTING_NEXT_START)) {
        /* applies from the next start: how to get there now */
        const char* how = portTrGame(portRestartSupported() ? "Applies after Restart now."
                                                            : "Applies when the app reopens.");
        const size_t len = strlen(text);
        snprintf(text + len, sizeof(text) - len, "\n%s", how);
    }
    portPauseWrap(text, sHelp, sizeof(sHelp));
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
    if (!(wp->buttonTrg & PAD_BUTTON_A) && (wp->buttonTrg != 0 || wp->dirRep != 0)) {
        sConfirm = 0; /* anything else: Restart now asks again */
    }
    if (wp->buttonTrg & PAD_BUTTON_A) {
        if (sRows[sRow].kind == KIND_ALL) {
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
    if (sRow < sCount) { /* read aloud: the row, its value and note, when either changes */
        static char sSaid[600];
        char text[600], value[64];
        valueText(&sRows[sRow], value, sizeof(value));
        snprintf(text, sizeof(text), "%s%s%s. %s", portTrGame(sRows[sRow].label), value[0] != '\0' ? ", " : "",
                 value, sRows[sRow].c.note != NULL ? portTrGame(sRows[sRow].c.note) : "");
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
#define TAB_HUE_SHIFT 55.0f /* degrees: the Journal's blue to violet */

static u8 sTabPixels[TAB_TEX_W * TAB_TEX_H * 4] __attribute__((aligned(32))); /* GX RGBA8 */
static GXTexObj sTabObj;
static const void* sTabFrom; /* the TPL image it was made from (made again when the TPL is) */
static int sTabReady;

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
static BOOL makeTab(TEXPalette* tpl) {
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
    /* violet, the shape as alpha, in GX's RGBA8 (4x4 tiles: AR pairs, then GB) */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            u8* p = out + (y * w + x) * 4;
            u8* tile = sTabPixels + ((y / 4) * (w / 4) + x / 4) * 64;
            const int at = ((y & 3) * 4 + (x & 3)) * 2;
            hueShift(p, TAB_HUE_SHIFT);
            tile[at] = shape[y * w + x];
            tile[at + 1] = p[0];
            tile[32 + at] = p[1];
            tile[32 + at + 1] = p[2];
        }
    }
    GXInitTexObj(&sTabObj, sTabPixels, w, h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&sTabObj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    return TRUE;
}

/* the tab drawn from the picture made above, tinted by c as the game's tabs
 * are (grey when not selected); FALSE when there is no picture */
static BOOL paintedTab(WindowWork* wp, float x, float y, float scale, GXColor c) {
    TEXPalette* tpl = (TEXPalette*)*wp->res->data;
    TEXDescriptor* tab = TEXGet(tpl, 5);
    const void* from = tab != NULL && tab->textureHeader != NULL ? tab->textureHeader->data : NULL;
    const f32 w = TAB_TEX_W * scale, h = TAB_TEX_H * scale;
    Mtx m;
    if (from != sTabFrom) {
        sTabFrom = from;
        sTabReady = from != NULL && makeTab(tpl);
    }
    if (!sTabReady) {
        return FALSE;
    }
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
    GXLoadTexObj(&sTabObj, GX_TEXMAP0);
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
    return TRUE;
}

/* the tab's label: the painted tab (above; a tinted plain one, the page panel's
 * shape and color, if the TPL isn't as expected) with "Settings" on it in the
 * game's font, pale on a dark outline as the other tabs' words */
void portPauseSettingsTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c) {
    const char* kLabel = portTrGame("Settings");
    /* the tab's size, and its middle below the label's top as the game's tabs sit */
    const f32 w = 108.0f * scale, h = 40.0f * scale, cy = y - 6.0f * scale;
    const u8 k = c.r; /* 0xFF selected, 0x80 not: the tab darkens with its label */
    const GXColor shadow = {(u8)(0x3A * k / 0xFF), (u8)(0x1C * k / 0xFF), (u8)(0x66 * k / 0xFF), c.a};
    const GXColor fill = {(u8)(0xF6 * k / 0xFF), (u8)(0xEA * k / 0xFF), (u8)(0xF4 * k / 0xFF), 0xFF};
    Mtx m;
    GXTexObj obj;
    f32 tw;
    int dx, dy;

    if (paintedTab(wp, x, y, scale, c)) {
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

label:
    winFontInit();
    /* as wide as the tab's middle at most: narrower letters for a longer word (Einstellungen) */
    {
        const f32 full = (f32)FontGetMessageWidth(kLabel);
        const f32 room = TAB_TEX_W * scale - 14.0f * scale;
        const f32 sx = full * 0.95f * scale > room ? room / full : 0.95f * scale;
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
        winFontSet((Vec){x - tw / 2, y + 13.0f * scale, 0.0f}, (Vec){sx, sy, sy}, fill, kLabel);
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
