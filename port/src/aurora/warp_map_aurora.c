/* The warp map (port/include/port/warp_map.h): Practice #4's warp to any
 * room on a map, a Start menu page in the game's windows and font.
 *
 * Three levels on the one page, B going back a level:
 * - the world: the Journal's world map, the disc's own TPL read the way the
 *   Journal reads it (src/window/win_log.c; always the whole world, map7.tpl),
 *   with the Journal's dots (win.tpl's) where the Journal puts each area and,
 *   beside it, the picked area's name (the Journal's, else Paper Cut's in the
 *   game's text), its room count and its rooms in small;
 * - an area: its rooms as the game's paper cut-outs on the area's grid
 *   (port/include/port/area_maps.h), joined as their exits join them, a stub
 *   for each way out of the area, Mario's room marked;
 * - a room's ways in, over the area: the safe spawn first (the list's), then
 *   each exit by its direction and where it leads. A asks for the warp
 *   (portRoomWarpRequest: refused out of the field, online or in a replay) and
 *   closes the menu; the warp happens once the menu is gone.
 * The cursor goes to the nearest dot or room in the stick's direction; the
 * mouse and touch pick what is under them (a click on the picked one is A). */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/area_maps.h"
#include "port/button_markup.h"
#include "port/companion_menu.h"
#include "port/fast_travel.h"
#include "port/menu_crawl.h"
#include "port/menu_tap.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/platform.h"
#include "port/practice_more.h"
#include "port/practice_tools.h"
#include "port/replay.h"
#include "port/statesync.h"
#include "port/text_fit.h"
#include "port/tr.h"
#include "port/warp_map.h"
#include "driver/camdrv.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "manager/filemgr.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "system.h"
#include "sdk/texPalette.h"
#include "win/win_log.h"
#include "win/win_main.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void portSettingsOpenAt(const char* tab); /* port/settings/settings_ui.cpp: the full page on one of its tabs */

#define PANEL_X (-270.0f)
#define PANEL_TOP 144.0f
#define PANEL_W 540.0f
#define PANEL_H 270.0f

static const GXColor kBlack = {0x00, 0x00, 0x00, 0xFF};
static const GXColor kGrey = {0x60, 0x60, 0x60, 0xFF};
static const GXColor kHeading = {0x5A, 0x46, 0x9C, 0xFF};
static const GXColor kFrame = {0x30, 0x28, 0x20, 0xFF};
static const GXColor kWhite = {0xFF, 0xFF, 0xFF, 0xFF};
static const GXColor kPaper = {0xF4, 0xEE, 0xDC, 0xFF};
static const GXColor kPicked = {0xE8, 0xD8, 0xB0, 0xFF};
static const GXColor kInk = {0x78, 0x54, 0x32, 0xFF};
static const GXColor kAway = {0x5A, 0x46, 0x9C, 0xC0};
static const GXColor kShut = {0x50, 0x48, 0x40, 0x90}; /* over a room fast travel can't go to now */
static const GXColor kMario = {0xE0, 0x20, 0x20, 0xFF};

/* --- the areas on the world map ---
 * Where the Journal's map puts each one: its first big dot in win_log.c's tubuDt (x right, y up, from
 * the map's middle). The Excess Express and Riverside's platform have no dot of their own: on the
 * line from Rogueport to Riverside, and beside Riverside Station. name: Paper Cut's (English) where
 * the Journal has none. */
static const struct {
    const char* area;
    float x, y;
    const char* name;
} kPins[] = {
    {"gor", 14, -34, "Rogueport"},         {"tik", -33, -38, "Rogueport Sewers"},
    {"hei", 118, 30, "Petal Meadows"},     {"nok", 200, 6, "Petalburg"},
    {"gon", 165, 68, "Hooktail Castle"},   {"win", 0, 36, "Boggly Woods"},
    {"mri", 0, 75, "The Great Tree"},      {"tou", 97, 150, "Glitzville"},
    {"usu", -181, -90, "Twilight Town"},   {"gra", -228, -63, "Twilight Trail"},
    {"jin", -139, -33, "Creepy Steeple"},  {"muj", -11, -163, "Keelhaul Key"},
    {"dou", 41, -163, "Pirate's Grotto"},  {"rsh", 109, 7, "The Excess Express"},
    {"eki", 158, -25, "Riverside Station"}, {"hom", 140, -6, "Riverside Station"},
    {"pik", 155, -90, "Poshley Heights"},  {"bom", -167, 17, "Fahr Outpost"},
    {"moo", -167, 151, "The Moon"},        {"aji", -209, 159, "X-Naut Fortress"},
    {"las", -33, -74, "Palace of Shadow"},
};
#define PINS ((int)(sizeof(kPins) / sizeof(kPins[0])))

/* the exits' directions as steps on the grid (1 north, clockwise; y south) */
static const float kDir[9][2] = {{0, 0},  {0, -1},     {0.7f, -0.7f}, {1, 0}, {0.7f, 0.7f},
                                 {0, 1},  {-0.7f, 0.7f}, {-1, 0},       {-0.7f, -0.7f}};
static const char* const kDirName[9] = {"Way in",     "North", "North-east", "East",      "South-east",
                                        "South",      "South-west", "West",  "North-west"};

/* --- drawing --- */

static void flatState(GXColor c) {
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, kWhite);
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
    GXLoadPosMtxImm(camGetPtr(CAMERA_2D)->view, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

/* a line from a to b, t thick */
static void line(f32 ax, f32 ay, f32 bx, f32 by, f32 t, GXColor c) {
    const f32 dx = bx - ax, dy = by - ay;
    const f32 len = sqrtf(dx * dx + dy * dy);
    f32 nx, ny;
    if (len < 0.5f) {
        return;
    }
    nx = -dy / len * t * 0.5f;
    ny = dx / len * t * 0.5f;
    flatState(c);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(ax + nx, ay + ny, 0.0f);
    GXPosition3f32(ax - nx, ay - ny, 0.0f);
    GXPosition3f32(bx - nx, by - ny, 0.0f);
    GXPosition3f32(bx + nx, by + ny, 0.0f);
}

/* a box from its top-left (x, y), w across and h down */
static void box(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    portPauseBox(x, y, w, h, c);
}

/* a small diamond about (x, y) */
static void diamond(f32 x, f32 y, f32 r, GXColor c) {
    flatState(c);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(x, y + r, 0.0f);
    GXPosition3f32(x - r, y, 0.0f);
    GXPosition3f32(x, y - r, 0.0f);
    GXPosition3f32(x + r, y, 0.0f);
}

/* text at (x, y) no wider than w: scaled down from s to fit */
static void text(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* t, int right) {
    const f32 textWas = portButtonsGameTextScale(s); /* (a {A} in it: centred on the letters at s, button_markup.h) */
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 k = full > 0.0f && full * s > w ? w / full : s;
    portTextFit("warp-map", t, full * s, w, 1);
    winFontInit();
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, portTextFitHeight(k, s), portTextFitHeight(k, s)}, c, "%s", t);
    portButtonsGameTextScale(textWas);
}

/* text centred on x */
static void textMid(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* t) {
    const f32 full = (f32)FontGetMessageWidth(t) * s;
    text(x - (full < w ? full : w) / 2.0f, y, w, s, c, t, 0);
}

/* '<' starts a tag in the game's text: shown as a look-alike */
static void plain(char* s) {
    for (; *s != '\0'; s++) {
        *s = *s == '<' ? '(' : *s == '>' ? ')' : *s;
    }
}

/* --- the world map's picture: the Journal's TPL, loaded while the page is open --- */

static FileEntry* sMapRes;
static int sMapMissing;

static void wmLoad(void) {
    FileEntry* f;
    if (sMapRes != NULL || sMapMissing) {
        return;
    }
#if VERSION >= VERSION_US
    f = fileAsyncf(4, NULL, "%s/w/%s/map7.tpl", getMarioStDvdRoot(), (u32)gp->language == 0 ? "jp" : "us");
#else
    f = fileAsyncf(4, NULL, WIN_DIR "jp/map7.tpl", getMarioStDvdRoot());
#endif
    if (f == NULL) {
        return; /* still reading */
    }
    if (fileAsyncMissing(f)) {
        sMapMissing = 1;
        return;
    }
#if VERSION >= VERSION_US
    sMapRes = fileAllocf(4, "%s/w/%s/map7.tpl", getMarioStDvdRoot(), (u32)gp->language == 0 ? "jp" : "us");
#else
    sMapRes = fileAllocf(4, WIN_DIR "jp/map7.tpl", getMarioStDvdRoot());
#endif
}

static void wmFree(void) {
    if (sMapRes != NULL) {
        fileFree(sMapRes);
        sMapRes = NULL;
    }
    sMapMissing = 0;
}

/* the picture w x h with its top-left at (x, y), filtered (it is drawn at about half its size) */
static void wmImage(f32 x, f32 y, f32 w, f32 h) {
    GXTexObj obj;
    TEXGetGXTexObjFromPalette(*(TEXPalette**)sMapRes->data, &obj, 0);
    GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, kWhite);
    GXSetNumChans(0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXLoadPosMtxImm(camGetPtr(CAMERA_2D)->view, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
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

/* --- the page's state --- */

enum { LEVEL_WORLD, LEVEL_AREA, LEVEL_WAYS };

#define AREA_ROOMS 64
#define WAYS_MAX 24
#define WAYS_SHOWN 7

static int sLevel;
static int sPin;                       /* the world's picked area (kPins) */
static int sRooms[AREA_ROOMS], sRoomCount; /* the picked area's rooms (portAreaRooms indices) */
static int sGridW, sGridH;
static int sRoom;                      /* the picked room (index into sRooms) */
static int sWays[WAYS_MAX], sWayCount; /* the picked room's exits (portAreaExits indices) */
static int sWay, sWayTop;              /* 0: the safe spawn, k: sWays[k - 1] */
static const char* sMessage;           /* a refusal, for the help box */
static char sHelp[512];
static int sTour, sTourStep, sTourWait; /* $TTYD_WARPMAP_TOUR */
static int sTravel;                    /* fast travel's (the Journal's map): Mario's rooms and ways only */
#define SAFE_ROWS (sTravel ? 0 : 1)    /* the ways' list's safe spawn row: the warp's only */

/* a room shown: every room for the warp; for fast travel, the ones Mario has been in */
static int shown(int room) {
    return !sTravel || portTravelRoomKnown(room);
}

static const char* pinName(int pin, char* out, size_t size) {
    char tag[32];
    const char* missing = msgSearch("pc_warp_map_no_such_message");
    const char* t;
    snprintf(tag, sizeof(tag), "menu_monosiri_%s", kPins[pin].area);
    t = msgSearch(tag);
    if (t == NULL || t == missing || t[0] == '\0') {
        t = portTrGame(kPins[pin].name); /* (none in the Journal: the Excess Express) */
        if (strcmp(kPins[pin].area, "hom") == 0) {
            snprintf(tag, sizeof(tag), "menu_monosiri_eki"); /* Riverside's platform: the Journal's Riverside */
            if (msgSearch(tag) != NULL && msgSearch(tag) != missing) {
                t = msgSearch(tag);
            }
        }
    }
    snprintf(out, size, "%s", t);
    plain(out);
    return out;
}

/* the pin of an area ("gor"), or -1 */
static int pinOf(const char* area) {
    int i;
    for (i = 0; area != NULL && i < PINS; i++) {
        if (strncmp(kPins[i].area, area, 3) == 0) {
            return i;
        }
    }
    return -1;
}

/* Mario's room now, as the area maps keep it ("gor_01"), or "" */
static const char* hereRoom(void) {
    static char room[8];
    room[0] = '\0';
    if (gp != NULL && seqGetSeq() == SEQ_GAME) {
        snprintf(room, sizeof(room), "%.6s", gp->currentMapName);
    }
    return room;
}

static int roomCountOf(const char* area) {
    int i, n = 0;
    for (i = 0; i < portAreaRoomCount; i++) {
        n += strcmp(portAreaRooms[i].area, area) == 0;
    }
    return n;
}

/* the area's rooms into sRooms, the cursor on Mario's room when he is in it */
static void areaBuild(int pin) {
    const char* here = hereRoom();
    int i;
    sRoomCount = 0;
    sGridW = sGridH = 1;
    sRoom = 0;
    for (i = 0; i < portAreaRoomCount && sRoomCount < AREA_ROOMS; i++) {
        const PortAreaRoom* r = &portAreaRooms[i];
        if (strcmp(r->area, kPins[pin].area) != 0) {
            continue;
        }
        /* (the grid of every room, so a room stands where it does on the warp's map) */
        sGridW = r->x + 1 > sGridW ? r->x + 1 : sGridW;
        sGridH = r->y + 1 > sGridH ? r->y + 1 : sGridH;
        if (!shown(i)) {
            continue;
        }
        if (strcmp(r->map, here) == 0) {
            sRoom = sRoomCount;
        }
        sRooms[sRoomCount++] = i;
    }
}

static void waysBuild(void) {
    int i;
    sWayCount = 0;
    sWay = sWayTop = 0;
    for (i = 0; i < portAreaExitCount && sWayCount < WAYS_MAX; i++) {
        if (portAreaExits[i].room == sRooms[sRoom] && portAreaExits[i].hit != NULL &&
            (!sTravel || portTravelWayKnown(i))) {
            sWays[sWayCount++] = i;
        }
    }
}

/* an exit's name in the list: its direction (or a pipe), and the room it leads to */
static const char* wayName(int k, char* out, size_t size) {
    const PortAreaExit* e;
    const char* what;
    if (k < SAFE_ROWS) {
        snprintf(out, size, "%s", portTrGame("Safe spawn (the room's start)"));
        plain(out);
        return out;
    }
    e = &portAreaExits[sWays[k - SAFE_ROWS]];
    what = e->dir > 0 && e->dir <= 8 ? portTrGame(kDirName[e->dir])
           : strstr(e->hit, "dokan") != NULL ? portTrGame("Pipe")
                                             : e->hit;
    if (e->to != NULL && e->to[0] != '\0') {
        snprintf(out, size, "%s - %.6s", what, e->to);
    } else {
        snprintf(out, size, "%s", what);
    }
    plain(out);
    return out;
}

/* --- layout (page coordinates: (0, 0) the page's origin, as the pointer's) --- */

#define MAP_X (PANEL_X + 12.0f)
#define MAP_TOP (PANEL_TOP - 12.0f)
#define MAP_H (PANEL_H - 24.0f)
#define MAP_W (MAP_H * 608.0f / 480.0f)
#define SIDE_X (MAP_X + MAP_W + 14.0f)
#define SIDE_W (PANEL_X + PANEL_W - 16.0f - SIDE_X)

static void pinAt(int pin, f32* x, f32* y) {
    const f32 s = MAP_H / 480.0f;
    *x = MAP_X + MAP_W / 2.0f + kPins[pin].x * s;
    *y = MAP_TOP - MAP_H / 2.0f + kPins[pin].y * s;
}

typedef struct Grid {
    f32 left, top, cell, tileW, tileH;
} Grid;

/* the area's grid in the rectangle from (left, top), w across and h down */
static Grid gridIn(f32 left, f32 top, f32 w, f32 h, f32 maxCell, int mini) {
    Grid g;
    f32 cw = w / (f32)sGridW, ch = h / (f32)sGridH;
    g.cell = cw < ch ? cw : ch;
    g.cell = g.cell > maxCell ? maxCell : g.cell;
    g.left = left + (w - g.cell * sGridW) / 2.0f;
    g.top = top - (h - g.cell * sGridH) / 2.0f;
    g.tileW = mini ? g.cell * 0.7f : (g.cell * 0.8f < 76.0f ? g.cell * 0.8f : 76.0f);
    g.tileH = mini ? g.cell * 0.55f : (g.cell * 0.64f < 46.0f ? g.cell * 0.64f : 46.0f);
    return g;
}

static void roomAt(const Grid* g, int room, f32* x, f32* y) {
    const PortAreaRoom* r = &portAreaRooms[room];
    *x = g->left + (r->x + 0.5f) * g->cell;
    *y = g->top - (r->y + 0.5f) * g->cell;
}

#define AREA_LEFT (PANEL_X + 20.0f)
#define AREA_TOP (PANEL_TOP - 40.0f)
#define AREA_W (PANEL_W - 40.0f)
#define AREA_H (PANEL_H - 52.0f)

static Grid areaGrid(void) {
    return gridIn(AREA_LEFT, AREA_TOP, AREA_W, AREA_H, 96.0f, 0);
}

/* the ways-in list's box: on the side away from the picked room */
#define WAYS_W 250.0f
#define WAY_STEP 22.0f
static f32 waysX(void) {
    const Grid g = areaGrid();
    f32 rx, ry;
    roomAt(&g, sRooms[sRoom], &rx, &ry);
    return rx > 0.0f ? PANEL_X + 16.0f : PANEL_X + PANEL_W - 16.0f - WAYS_W;
}
#define WAYS_TOP (PANEL_TOP - 44.0f)

/* --- the area's rooms drawn --- */

static int inArea(int room) {
    return room >= 0 && room < portAreaRoomCount && strcmp(portAreaRooms[room].area, kPins[sPin].area) == 0 &&
           shown(room);
}

static int roomIndex(const char* map) {
    int i;
    for (i = 0; map != NULL && i < portAreaRoomCount; i++) {
        if (strncmp(portAreaRooms[i].map, map, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void drawArea(WindowWork* wp, const Grid* g, f32 ox, f32 oy, int mini, int picked) {
    const char* here = hereRoom();
    int i, k;
    (void)wp;
    /* the ways between them, then the ways out of the area */
    for (i = 0; i < portAreaExitCount; i++) {
        const PortAreaExit* e = &portAreaExits[i];
        const int to = e->toRoom >= 0 ? e->toRoom : roomIndex(e->to);
        f32 ax, ay, bx, by;
        if (!inArea(e->room)) {
            continue;
        }
        roomAt(g, e->room, &ax, &ay);
        if (inArea(to)) {
            if (to > e->room) {
                roomAt(g, to, &bx, &by);
                line(ox + ax, oy + ay, ox + bx, oy + by, mini ? 1.5f : 3.0f, kInk);
            }
        } else if (e->dir > 0 && e->dir <= 8 && !mini && (!sTravel || portTravelWayKnown(i))) {
            bx = ax + kDir[e->dir][0] * g->cell * 0.55f;
            by = ay - kDir[e->dir][1] * g->cell * 0.55f;
            line(ox + ax, oy + ay, ox + bx, oy + by, 3.0f, kAway);
            diamond(ox + bx, oy + by, 3.5f, kAway);
        }
    }
    for (k = 0; k < sRoomCount; k++) {
        const int room = sRooms[k];
        const PortAreaRoom* r = &portAreaRooms[room];
        f32 cx, cy, tx, ty;
        roomAt(g, room, &cx, &cy);
        cx += ox;
        cy += oy;
        tx = cx - g->tileW / 2.0f;
        ty = cy + g->tileH / 2.0f;
        if (mini) {
            box(tx - 1.0f, ty + 1.0f, g->tileW + 2.0f, g->tileH + 2.0f, kFrame);
            box(tx, ty, g->tileW, g->tileH, strcmp(r->map, here) == 0 ? kPicked : kPaper);
        } else {
            char label[8];
            const f32 s = g->tileH < 30.0f ? g->tileH / 34.0f : 0.8f;
            if (k == picked) {
                box(tx - 3.0f, ty + 3.0f, g->tileW + 6.0f, g->tileH + 6.0f, kWhite);
            }
            if (g->tileW >= 16.0f && g->tileH >= 16.0f) {
                winKirinukiGX(wp, 0, tx, ty, g->tileW, g->tileH);
            } else {
                box(tx - 1.0f, ty + 1.0f, g->tileW + 2.0f, g->tileH + 2.0f, kFrame);
                box(tx, ty, g->tileW, g->tileH, kPaper);
            }
            if (k == picked) {
                box(tx + 3.0f, ty - 3.0f, g->tileW - 6.0f, g->tileH - 6.0f, kPicked);
            }
            if (sTravel && portTravelArriveRefusal(room) != NULL) {
                box(tx, ty, g->tileW, g->tileH, kShut);
            }
            snprintf(label, sizeof(label), "%s", r->map + 4); /* the room's number ("01") */
            textMid(cx, cy + s * 13.0f, g->tileW - 4.0f, s, k == picked ? kBlack : kGrey, label);
        }
        if (strcmp(r->map, here) == 0 && !mini) { /* Mario is here: the Journal's red */
            diamond(tx + g->tileW - 2.0f, ty - 2.0f, 5.0f, kMario);
        }
    }
}

/* --- the page --- */

static void pointAt(WindowWork* wp) {
    f32 x, y;
    if (sLevel == LEVEL_WORLD) {
        pinAt(sPin, &x, &y);
        wp->helpTgtX = x - 12.0f;
        wp->helpTgtY = y;
    } else if (sLevel == LEVEL_AREA) {
        const Grid g = areaGrid();
        roomAt(&g, sRooms[sRoom], &x, &y);
        wp->helpTgtX = x - g.tileW / 2.0f - 14.0f;
        wp->helpTgtY = y;
    } else {
        wp->helpTgtX = waysX() + 2.0f;
        wp->helpTgtY = WAYS_TOP - 40.0f - (sWay - sWayTop) * WAY_STEP - WAY_STEP / 2.0f + 4.0f;
    }
}

static void enterArea(int pin) {
    char name[64];
    sPin = pin;
    areaBuild(pin);
    sLevel = LEVEL_AREA;
    printf("[warpmap] area %s (%d rooms): %s\n", kPins[pin].area, sRoomCount, pinName(pin, name, sizeof(name)));
    fflush(stdout);
}

static void wmOpen(int arg) {
    const char* want = getenv("TTYD_WARPMAP_AREA");
    int pin = pinOf(want != NULL ? want : gp != NULL ? gp->currentAreaName : NULL);
    (void)arg;
    sPin = pin >= 0 ? pin : 0;
    sLevel = LEVEL_WORLD;
    sMessage = NULL;
    sTravel = 0;
    sTour = getenv("TTYD_WARPMAP_TOUR") != NULL;
    sTourStep = 0;
    sTourWait = 40;
    wmFree();
    wmLoad();
    printf("[warpmap] open on %s\n", kPins[sPin].area);
    fflush(stdout);
}


/* the nearest of n points (px, py) from (x, y) in the stick's direction (dx, dy; y up), or -1 */
static int nearest(f32 x, f32 y, f32 dx, f32 dy, const f32* px, const f32* py, int n, int self) {
    int i, best = -1;
    f32 bestScore = 1e9f;
    for (i = 0; i < n; i++) {
        const f32 vx = px[i] - x, vy = py[i] - y;
        const f32 along = vx * dx + vy * dy;
        const f32 across = fabsf(vx * dy - vy * dx);
        f32 score;
        if (i == self || along <= 0.5f || across > along * 2.5f) {
            continue;
        }
        score = along + across * 2.0f;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

static int stickDir(WindowWork* wp, f32* dx, f32* dy) {
    *dx = (wp->dirRep & DEMO_STICK_RIGHT) ? 1.0f : (wp->dirRep & DEMO_STICK_LEFT) ? -1.0f : 0.0f;
    *dy = (wp->dirRep & DEMO_STICK_UP) ? 1.0f : (wp->dirRep & DEMO_STICK_DOWN) ? -1.0f : 0.0f;
    if (*dx != 0.0f && *dy != 0.0f) {
        *dx *= 0.7071f;
        *dy *= 0.7071f;
    }
    return *dx != 0.0f || *dy != 0.0f;
}

static int warpNow(void) {
    const PortAreaExit* e = sWay >= SAFE_ROWS ? &portAreaExits[sWays[sWay - SAFE_ROWS]] : NULL;
    const char* map = portAreaRooms[sRooms[sRoom]].map;
    if (gPortCompanionMenu) {
        sMessage = sTravel ? "Travel from the game's own Start menu, not the second screen's."
                           : "Warp from the game's own Start menu, not the second screen's.";
        return 0;
    }
    if (sTravel) {
        sMessage = e != NULL ? portTravelRequest(sWays[sWay - SAFE_ROWS]) : "Mario hasn't been here yet.";
        return sMessage == NULL;
    }
    if (!portRoomWarpRequest(map, e != NULL ? e->hit : NULL)) {
        /* (the help box is the game's font: the mode isn't named, the Start menu's first row leaves it) */
        sMessage = portPracticeModeRefusal() != NULL ? "Not while a mode runs."
                                                     : "Only in the field, and not online or in a replay.";
        return 0;
    }
    printf("[warpmap] warp %s %s\n", map, e != NULL ? e->hit : "(safe spawn)");
    fflush(stdout);
    return 1;
}

/* the mouse and touch: what is under the pointer picked; 1 when a click chose the picked one */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0) {
        return 0;
    }
    if (sLevel == LEVEL_WORLD) {
        int best = -1;
        f32 bestD = 14.0f * 14.0f;
        for (i = 0; i < PINS; i++) {
            f32 x, y;
            pinAt(i, &x, &y);
            if ((x - gx) * (x - gx) + (y - gy) * (y - gy) < bestD) {
                bestD = (x - gx) * (x - gx) + (y - gy) * (y - gy);
                best = i;
            }
        }
        if (best < 0) {
            return 0;
        }
        if (best == sPin) {
            return kind == 1;
        }
        sPin = best;
        psndSFXOn((const char*)0x20005);
        printf("[warpmap] picked %s (%s)\n", kPins[best].area, kind == 1 ? "a click or tap" : "the pointer over it");
        fflush(stdout);
        return 0;
    }
    if (sLevel == LEVEL_AREA) {
        const Grid g = areaGrid();
        for (i = 0; i < sRoomCount; i++) {
            f32 x, y;
            roomAt(&g, sRooms[i], &x, &y);
            if (fabsf(gx - x) <= g.tileW / 2.0f + 3.0f && fabsf(gy - y) <= g.tileH / 2.0f + 3.0f) {
                if (i == sRoom) {
                    return kind == 1;
                }
                sRoom = i;
                psndSFXOn((const char*)0x20005);
                if (kind == 1) {
                    printf("[warpmap] picked %s\n", portAreaRooms[sRooms[i]].map);
                    fflush(stdout);
                }
                return 0;
            }
        }
        return 0;
    }
    {
        const f32 x0 = waysX();
        const int shown = sWayCount + SAFE_ROWS < WAYS_SHOWN ? sWayCount + SAFE_ROWS : WAYS_SHOWN;
        for (i = 0; i < shown; i++) {
            const f32 top = WAYS_TOP - 40.0f - i * WAY_STEP;
            if (gx >= x0 && gx < x0 + WAYS_W && gy <= top && gy > top - WAY_STEP) {
                if (sWayTop + i == sWay) {
                    return kind == 1;
                }
                sWay = sWayTop + i;
                psndSFXOn((const char*)0x20005);
                return 0;
            }
        }
    }
    return 0;
}

/* $TTYD_WARPMAP_TOUR: every area's dot picked, then its rooms, a picture of each */
static int tourTick(WindowWork* wp) {
    static char sShot[64];
    wp->buttonTrg = 0;
    wp->dirRep = 0;
    if (--sTourWait > 0) {
        return 1;
    }
    if (sShot[0] != '\0') { /* the view set last step, the glove there by now */
        portMenuCrawlShot(sShot);
        sShot[0] = '\0';
        sTourWait = 4;
        return 1;
    }
    if (sTourStep >= PINS * 2) {
        sTour = 0;
        sLevel = LEVEL_WORLD;
        printf("[warpmap] tour done\n");
        fflush(stdout);
        return 0;
    }
    if (sTourStep % 2 == 0) {
        sLevel = LEVEL_WORLD;
        sPin = sTourStep / 2;
        snprintf(sShot, sizeof(sShot), "warpmap_world_%s", kPins[sPin].area);
    } else {
        enterArea(sTourStep / 2);
        snprintf(sShot, sizeof(sShot), "warpmap_area_%s", kPins[sPin].area);
    }
    sTourWait = 20;
    sTourStep++;
    return 1;
}

static int wmMain(WindowWork* wp) {
    int click;
    f32 dx, dy;
    if (!sTravel) {
        wmLoad();
    }
    if (sTour) {
        tourTick(wp);
        pointAt(wp);
        return 0;
    }
    click = pointer();
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sMessage = NULL;
        if (sLevel == LEVEL_WORLD || (sTravel && sLevel == LEVEL_AREA)) {
            wmFree();
            return -1;
        }
        sLevel--;
        pointAt(wp);
        return 0;
    }
    if ((wp->buttonTrg & PAD_BUTTON_X) && !sTravel) { /* the list: the full settings page, on the warp's tab */
        psndSFXOn((const char*)0x20012);
        portSettingsOpenAt("Enhancements");
        return 0;
    }
    if (click || (wp->buttonTrg & PAD_BUTTON_A)) {
        psndSFXOn((const char*)0x20012);
        sMessage = NULL;
        if (sLevel == LEVEL_WORLD) {
            enterArea(sPin);
        } else if (sLevel == LEVEL_AREA && sTravel && sRoomCount > 0 &&
                   (sMessage = portTravelArriveRefusal(sRooms[sRoom])) != NULL) {
            psndSFXOn((const char*)0x20014); /* (the buzzer; stays: the help box says why) */
        } else if (sLevel == LEVEL_AREA && sRoomCount > 0) {
            waysBuild();
            sLevel = LEVEL_WAYS;
            printf("[warpmap] room %s (%d ways in)\n", portAreaRooms[sRooms[sRoom]].map, sWayCount);
            fflush(stdout);
        } else if (warpNow()) {
            wmFree();
            return -2; /* the whole menu closed: the warp once Mario has the controls */
        }
    } else if (sLevel == LEVEL_WAYS && (wp->dirRep & (DEMO_STICK_UP | DEMO_STICK_DOWN))) {
        const int to = sWay + ((wp->dirRep & DEMO_STICK_UP) ? -1 : 1);
        if (to >= 0 && to < sWayCount + SAFE_ROWS) {
            sWay = to;
            psndSFXOn((const char*)0x20005);
        }
    } else if (sLevel != LEVEL_WAYS && stickDir(wp, &dx, &dy)) {
        f32 px[AREA_ROOMS > PINS ? AREA_ROOMS : PINS], py[AREA_ROOMS > PINS ? AREA_ROOMS : PINS];
        int i, to;
        if (sLevel == LEVEL_WORLD) {
            for (i = 0; i < PINS; i++) {
                pinAt(i, &px[i], &py[i]);
            }
            to = nearest(px[sPin], py[sPin], dx, dy, px, py, PINS, sPin);
            if (to >= 0) {
                sPin = to;
            }
        } else {
            const Grid g = areaGrid();
            for (i = 0; i < sRoomCount; i++) {
                roomAt(&g, sRooms[i], &px[i], &py[i]);
            }
            to = nearest(px[sRoom], py[sRoom], dx, dy, px, py, sRoomCount, sRoom);
            if (to >= 0) {
                sRoom = to;
            }
        }
        if (to >= 0) {
            psndSFXOn((const char*)0x20005);
        }
    }
    if (sWay < sWayTop) {
        sWayTop = sWay;
    } else if (sWay >= sWayTop + WAYS_SHOWN) {
        sWayTop = sWay - WAYS_SHOWN + 1;
    }
    pointAt(wp);
    return 0;
}

static const char* wmHelp(void) {
    const char* t;
    char line[384];
    if (sMessage != NULL) {
        t = portTrGame(sMessage);
    } else if (sTravel && sLevel == LEVEL_AREA) {
        t = portTrGame("{A}: this room's ways in. {B}: the map.");
    } else if (sTravel) {
        t = portTrGame("{A}: travel there. {B}: back.");
    } else if (sLevel == LEVEL_WORLD) {
        t = portTrGame("{A}: this area's rooms. {X}: the room list. {B}: back.");
    } else if (sLevel == LEVEL_AREA) {
        t = portTrGame("{A}: this room's ways in. {B}: the world map.");
    } else {
        snprintf(line, sizeof(line), "%s %s", portTrGame("{A}: warp there. {B}: back."),
                 portTrGame("A warp counts as practice: no achievements until a file is loaded."));
        t = line;
    }
    portPauseWrap(t, sHelp, sizeof(sHelp));
    return sHelp;
}

static void worldDisp(WindowWork* wp, f32 x, f32 y) {
    char name[64], line[64];
    const char* hereArea = gp != NULL ? gp->currentAreaName : "";
    int i;
    /* the Journal's map in a dark rule */
    box(x + MAP_X - 2.0f, y + MAP_TOP + 2.0f, MAP_W + 4.0f, MAP_H + 4.0f, kFrame);
    if (sMapRes != NULL) {
        wmImage(x + MAP_X, y + MAP_TOP, MAP_W, MAP_H);
    } else {
        box(x + MAP_X, y + MAP_TOP, MAP_W, MAP_H, kPaper);
    }
    winTexInit(*(TEXPalette**)wp->res->data);
    for (i = 0; i < PINS; i++) {
        f32 px, py;
        pinAt(i, &px, &py);
        if (i == sPin) {
            winTexSet(((gp->retraceCount >> 4) & 1) ? 0xB9 : 0xBA, (Vec){x + px, y + py, 0.0f}, (Vec){1.0f, 1.0f, 1.0f},
                      kWhite);
        } else {
            winTexSet(0xBA, (Vec){x + px, y + py, 0.0f}, (Vec){0.8f, 0.8f, 1.0f}, kWhite);
        }
    }
    for (i = 0; i < PINS; i++) { /* Mario's area */
        f32 px, py;
        if (strncmp(kPins[i].area, hereArea, 3) == 0) {
            pinAt(i, &px, &py);
            diamond(x + px + 8.0f, y + py + 8.0f, 4.5f, kMario);
        }
    }
    /* beside it: the picked area's name, its rooms */
    text(x + SIDE_X, y + PANEL_TOP - 14.0f, SIDE_W, 0.8f, kHeading, pinName(sPin, name, sizeof(name)), 0);
    snprintf(line, sizeof(line), portTrGame("%d rooms"), roomCountOf(kPins[sPin].area));
    text(x + SIDE_X, y + PANEL_TOP - 44.0f, SIDE_W, 0.65f, kGrey, line, 0);
    if (strncmp(kPins[sPin].area, hereArea, 3) == 0) {
        snprintf(line, sizeof(line), "%s", portTrGame("Mario is here."));
        plain(line);
        text(x + SIDE_X, y + PANEL_TOP - 66.0f, SIDE_W, 0.65f, kMario, line, 0);
    }
    {
        Grid g;
        const int was = sRoom;
        areaBuild(sPin);
        sRoom = was;
        g = gridIn(SIDE_X, PANEL_TOP - 96.0f, SIDE_W, PANEL_H - 108.0f, 30.0f, 1);
        box(x + SIDE_X - 4.0f, y + PANEL_TOP - 92.0f, SIDE_W + 8.0f, PANEL_H - 100.0f, (GXColor){0xE4, 0xDA, 0xC0, 0xFF});
        drawArea(wp, &g, x, y, 1, -1);
    }
    if (sMapMissing) {
        text(x + MAP_X + 8.0f, y + MAP_TOP - 8.0f, MAP_W - 16.0f, 0.6f, kGrey, "map7.tpl", 0);
    }
}

static void areaDisp(WindowWork* wp, f32 x, f32 y) {
    char name[64], line[64];
    const Grid g = areaGrid();
    const char* map = portAreaRooms[sRooms[sRoom]].map;
    text(x + PANEL_X + 20.0f, y + PANEL_TOP - (sTravel ? 22.0f : 12.0f), 300.0f, 0.75f, kHeading, pinName(sPin, name, sizeof(name)), 0);
    snprintf(line, sizeof(line), "%s", map);
    text(x + PANEL_X + PANEL_W - 20.0f, y + PANEL_TOP - (sTravel ? 22.0f : 12.0f), 160.0f, 0.75f,
         strcmp(map, hereRoom()) == 0 ? kMario : kBlack, line, 1);
    drawArea(wp, &g, x, y, 0, sRoom);
}

static void waysDisp(WindowWork* wp, f32 x, f32 y) {
    char line[96];
    const f32 x0 = x + waysX();
    const int rows = sWayCount + SAFE_ROWS;
    const int shown = rows < WAYS_SHOWN ? rows : WAYS_SHOWN;
    int i;
    winKirinukiGX(wp, 0, x0, y + WAYS_TOP, WAYS_W, 52.0f + shown * WAY_STEP);
    snprintf(line, sizeof(line), "%s  %s", portTrGame("Ways in"), portAreaRooms[sRooms[sRoom]].map);
    plain(line);
    text(x0 + 14.0f, y + WAYS_TOP - 10.0f, WAYS_W - 28.0f, 0.7f, kHeading, line, 0);
    for (i = 0; i < shown; i++) {
        const int k = sWayTop + i;
        const f32 top = y + WAYS_TOP - 40.0f - i * WAY_STEP;
        if (k == sWay) {
            box(x0 + 10.0f, top, WAYS_W - 20.0f, WAY_STEP - 2.0f, kPicked);
        }
        text(x0 + 24.0f, top - 1.0f, WAYS_W - 40.0f, 0.62f, k == sWay ? kBlack : kGrey, wayName(k, line, sizeof(line)), 0);
    }
    if (rows > WAYS_SHOWN) {
        snprintf(line, sizeof(line), "%d-%d/%d", sWayTop + 1, sWayTop + shown, rows);
        text(x0 + WAYS_W - 14.0f, y + WAYS_TOP - 10.0f, 80.0f, 0.55f, kGrey, line, 1);
    }
}

static void wmDisp(WindowWork* wp, float x, float y) {
    winKirinukiGX(wp, 0, x + PANEL_X, y + PANEL_TOP, PANEL_W, PANEL_H);
    if (sLevel == LEVEL_WORLD) {
        worldDisp(wp, x, y);
    } else {
        areaDisp(wp, x, y);
        if (sLevel == LEVEL_WAYS) {
            waysDisp(wp, x, y);
        }
    }
}

static const PortPausePage kPage = {wmOpen, wmMain, wmDisp, wmHelp};

void portWarpMapOpen(void) {
    portPauseOpenPage(&kPage, 0);
}

/* --- reaching it --- */

static int sWantOpen; /* winMain opened the Start menu for the map: win_root.c opens it on the Settings tab */

const char* portWarpMapRefusal(void) {
    if (gPortCompanionMenu) {
        return "Warp from the game's own Start menu, not the second screen's.";
    }
    if (!portPracticeInGame() || seqGetSeq() != SEQ_GAME || portReplayActive() || portStateSyncOn()) {
        return "Only in the field, and not online or in a replay.";
    }
    return NULL;
}

int portWarpMapAvailable(void) {
    return portWarpMapRefusal() == NULL;
}

int portWarpMapPoll(void) {
    /* (asked for on the full page; taken here once the Start menu can open in the field) */
    if (!portWarpMapRequested()) {
        return 0;
    }
    if (!portWarpMapAvailable()) {
        return 0;
    }
    sWantOpen = 1;
    return 1;
}

int portWarpMapOpenIfRequested(void) {
    if (gPortCompanionMenu) {
        return 0; /* (the second screen's menu: the request is the game's own menu's) */
    }
    if (!sWantOpen && !(portWarpMapRequested() && portWarpMapAvailable())) {
        return 0;
    }
    sWantOpen = 0;
    portWarpMapOpen();
    return 1;
}

/* --- fast travel: the Journal's map (src/window/win_log.c), {A} on a place --- */

/* the area has a room Mario has been in */
static int travelRooms(int pin) {
    int i;
    for (i = 0; pin >= 0 && i < portAreaRoomCount; i++) {
        if (strcmp(portAreaRooms[i].area, kPins[pin].area) == 0 && portTravelRoomKnown(i)) {
            return 1;
        }
    }
    return 0;
}

GXColor portTravelJournalDot(const char* area, GXColor c) {
    static const GXColor kDark = {0x48, 0x48, 0x48, 0xFF};
    return portTravelOn() && !travelRooms(pinOf(area)) ? kDark : c;
}

int portTravelJournalOpen(WindowWork* wp, const char* area) {
    const char* want = getenv("TTYD_TRAVEL_AREA"); /* (tests: another area than the cursor's) */
    const int pin = pinOf(want != NULL ? want : area);
    if (!portTravelOn() || pin < 0 || !travelRooms(pin)) {
        psndSFXOn((const char*)0x20014); /* the buzzer: never been there */
        return 0;
    }
    psndSFXOn((const char*)0x20012);
    sTravel = 1;
    sTour = 0;
    enterArea(pin);
    sMessage = portTravelDepartRefusal(); /* (said at once: browsing is still fine) */
    pointAt(wp);
    return 1;
}

int portTravelJournalMain(WindowWork* wp) {
    static char sTag[32];
    unsigned h = 5381;
    const char* t;
    int r;
    if (wp->buttonTrg & PAD_BUTTON_START) {
        sTravel = 0;
        return -2;
    }
    r = wmMain(wp);
    if (r != 0) {
        sTravel = 0;
        return r;
    }
    /* the help box: a tag of its text, so a change is seen (winMsgEntry) */
    for (t = wmHelp(); *t != '\0'; t++) {
        h = h * 33u + (unsigned char)*t;
    }
    snprintf(sTag, sizeof(sTag), "pc_menu_travel_%08x", h);
    winMsgEntry(wp, 0, sTag, NULL);
    return 0;
}

void portTravelJournalDisp(WindowWork* wp, float x, float y) {
    if (sTravel && sRoomCount > 0) {
        wmDisp(wp, x, y);
    }
}

const char* portTravelJournalMsg(const char* tag) {
    if (strncmp(tag, "pc_menu_travel", 14) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_menu_travel") == 0) { /* (the Journal's map, fast travel on) */
        portPauseWrap(portTrGame("{A} on a place Mario has been: travel there."), sHelp, sizeof(sHelp));
        return sHelp;
    }
    return sHelp;
}
