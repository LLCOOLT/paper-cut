/* The Journal's Paper Mario 64 Tattle Log (port/include/port/pm64_media.h): with a ROM linked, an entry after
 * Records with a book as the Tattle log's, a spread an enemy: on the left its sprite from the ROM, playing its
 * idle, its name, number, HP and the enemy it fights as; on the right its Tattle from the ROM. Enemies not met
 * yet in battle (port/pm64_enemy.h: portPm64EnemySeen) are "?" and say so. L and R (or the stick) turn the
 * pages, B goes back.
 *
 * Draw-only, keeps the game's random numbers as they were (the font takes two a letter). Not in a replay
 * (the Journal's entries are the same on both sides, as Records'). Also the 2D draws of ROM pictures the
 * other PM64 pages use (portPm64Quad2D, portPm64AnimDraw2D). */
#include "port/text_fit.h"
#include "port/pause_settings.h"
#include "port/platform.h"
#include "port/party.h"
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_media.h"
#include "port/replay.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

#define BOOK_DX (-170.0f)
#define BOOK_DY 130.0f
static const float kPageX[2] = {20.0f, 250.0f};
#define PAGE_W 186.0f
#define LINES 11

static int sOpen, sAt;
static Pm64Anim sAnim;
static int sAnimOf = -1;
static unsigned sAnimGen, sAnimFrame;

/* --- 2D draws ------------------------------------------------------------------- */

void portPm64Quad2D(const void* tex, int texW, int texH, float u, float v, float x, float y, float w, float h,
                    unsigned rgba) {
    GXTexObj obj;
    Mtx m;
    const GXColor c = {(u8)(rgba >> 24), (u8)(rgba >> 16), (u8)(rgba >> 8), (u8)rgba};
    if (tex == NULL || texW <= 0 || texH <= 0) {
        return;
    }
    GXInitTexObj(&obj, (void*)tex, (u16)texW, (u16)texH, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_GREATER, 8, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
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
    GXLoadTexObj(&obj, GX_TEXMAP0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-w / 2, h / 2, 0.0f);
    GXTexCoord2f32(0.0f, 0.0f);
    GXPosition3f32(-w / 2, -h / 2, 0.0f);
    GXTexCoord2f32(0.0f, v);
    GXPosition3f32(w / 2, -h / 2, 0.0f);
    GXTexCoord2f32(u, v);
    GXPosition3f32(w / 2, h / 2, 0.0f);
    GXTexCoord2f32(u, 0.0f);
    GXEnd();
}

void portPm64AnimDraw2D(const Pm64Anim* a, int palette, float x, float y, float scale, unsigned rgba) {
    int i;
    if (a == NULL || a->sprite == NULL) {
        return;
    }
    for (i = 0; i < a->count; i++) {
        const Pm64Part* p = &a->part[i];
        const int pal = p->palette >= 0 ? p->palette : palette;
        const void* tex;
        int w, h, tw, th;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) ||
            (tex = portPm64RasterTexture(a->sprite, p->raster, pal, &tw, &th)) == NULL) {
            continue;
        }
        /* (the part's place is from the feet, y up; the 2D camera's y is up too) */
        portPm64Quad2D(tex, tw, th, (float)w / (float)tw, (float)h / (float)th, x + p->x * scale,
                       y + (p->y + (float)h * p->sy / 2.0f) * scale, (float)w * p->sx * scale,
                       (float)h * p->sy * scale, rgba);
    }
}

/* --- the entry ------------------------------------------------------------------ */

int portPm64JournalEntry(void) {
    return portPm64Linked() != PM64_NONE && !portReplayActive();
}

void portPm64JournalOpen(struct WindowWork* wp) {
    (void)wp;
    sOpen = 1;
    if (sAt >= portPm64EnemyCount()) {
        sAt = 0;
    }
}

static void help(WindowWork* wp) {
    static char sTag[48];
    snprintf(sTag, sizeof(sTag), "pc_menu_pm64_page_%d", sAt);
    winMsgEntry(wp, 0, sTag, NULL);
}

int portPm64JournalMain(struct WindowWork* wp) {
    const int n = portPm64EnemyCount();
    if (!sOpen) {
        portPm64JournalOpen(wp);
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
    if ((wp->buttonRep & PAD_TRIGGER_R) || (wp->dirRep & (DEMO_STICK_RIGHT | DEMO_STICK_DOWN))) {
        if (sAt + 1 < n) {
            sAt++;
            psndSFXOn(SFX_NAME_WORD(0x20035));
        }
    } else if ((wp->buttonRep & PAD_TRIGGER_L) || (wp->dirRep & (DEMO_STICK_LEFT | DEMO_STICK_UP))) {
        if (sAt > 0) {
            sAt--;
            psndSFXOn(SFX_NAME_WORD(0x20035));
        }
    }
    wp->helpTgtX = BOOK_DX - 128.0f;
    wp->helpTgtY = BOOK_DY - 172.0f;
    help(wp);
    return 0;
}

/* --- drawing -------------------------------------------------------------------- */

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

/* the text broken between words to fit width at scale, a line a row of 19 units at 0.62 (rowH); drawn when draw
 * is set; the rows it takes (more than LINES: it didn't fit) */
static int layout(f32 x, f32 y, f32 scale, f32 width, GXColor c, const char* text, int draw) {
    char line[128], word[64];
    int n = 0, rows = 0;
    const char* s = text;
    const f32 rowH = 19.0f * scale / 0.62f;
    line[0] = '\0';
    while (*s != '\0') {
        int w = 0;
        while (*s == ' ' || *s == '\n') {
            s++;
        }
        while (*s != '\0' && *s != ' ' && *s != '\n' && w + 1 < (int)sizeof(word)) {
            word[w++] = *s++;
        }
        word[w] = '\0';
        if (w == 0) {
            break;
        }
        {
            char test[192];
            snprintf(test, sizeof(test), n > 0 ? "%s %s" : "%s%s", line, word);
            if (n > 0 && (f32)FontGetMessageWidth(test) * scale > width) {
                if (draw && rows < LINES * 2) {
                    textAt(x, y - rowH * (f32)rows, scale, width, c, line);
                }
                rows++;
                snprintf(line, sizeof(line), "%s", word);
            } else {
                snprintf(line, sizeof(line), "%s", test);
            }
            n = (int)strlen(line);
        }
    }
    if (n > 0) {
        if (draw && rows < LINES * 2) {
            textAt(x, y - rowH * (f32)rows, scale, width, c, line);
        }
        rows++;
    }
    return rows;
}

/* The text on a page: at scale, or smaller until it all fits the page's height (LINES rows at scale: a long
 * Tattle, the ROM's are several boxes long, whole rather than cut off) */
static void wrapped(f32 x, f32 y, f32 scale, f32 width, GXColor c, const char* text) {
    const f32 height = 19.0f * (f32)LINES;
    f32 s = scale;
    while (s > 0.42f && (f32)layout(x, y, s, width, c, text, 0) * 19.0f * s / 0.62f > height) {
        s -= 0.02f;
    }
    layout(x, y, s, width, c, text, 1);
}

void portPm64JournalDisp(struct WindowWork* wp, float x, float y) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const GXColor ink = {0x20, 0x18, 0x10, 0xFF};
    const GXColor head = {0x90, 0x28, 0x20, 0xFF};
    const f32 bx = x + BOOK_DX, by = y + BOOK_DY;
    const int n = portPm64EnemyCount();
    const u32 seed = portRandState();
    const Pm64EnemyInfo* e = portPm64EnemyInfo(sAt);
    const int seen = portPm64EnemySeen(sAt);
    char buf[512], name[48];
    int i, met = 0;
    winBookGX(wp, 1, bx, by);
    for (i = 0; i < n; i++) {
        met += portPm64EnemySeen(i);
    }
    winKirinukiGX(wp, 0, bx - 110.0f, by - 160.0f, 100.0f, 70.0f);
    centredText(bx - 60.0f, by - 166.0f, 0.7f, 88.0f, black, portTrGame("Paper Mario"));
    snprintf(buf, sizeof(buf), portTrGame("%d met"), met);
    centredText(bx - 60.0f, by - 184.0f, 0.6f, 88.0f, black, buf);
    snprintf(buf, sizeof(buf), "%d/%d", sAt + 1, n);
    centredText(bx - 60.0f, by - 202.0f, 0.65f, 88.0f, black, buf);
    if (e != NULL) {
        const f32 lx = bx + kPageX[0], rx = bx + kPageX[1];
        if (seen) {
            portPm64EnemyName(sAt, name, (int)sizeof(name));
        } else {
            snprintf(name, sizeof(name), "?");
        }
        centredText(lx + PAGE_W / 2.0f, by - 16.0f, 0.8f, PAGE_W, head, name);
        /* the sprite, playing its idle (drawn dark when not met, as a silhouette) */
        if (sAnimOf != sAt || sAnimGen != portPm64Generation()) {
            sAnimOf = sAt;
            sAnimGen = portPm64Generation();
            sAnimFrame = gp->retraceCount;
            portPm64AnimStart(&sAnim, portPm64NpcSprite(e->sprite), e->idle);
        } else if (sAnimFrame != gp->retraceCount) {
            const unsigned d = gp->retraceCount - sAnimFrame;
            portPm64AnimStep(&sAnim, (float)(d > 4 ? 4 : d));
            sAnimFrame = gp->retraceCount;
        }
        portPm64AnimDraw2D(&sAnim, e->palette, lx + PAGE_W / 2.0f, by - 150.0f, 1.5f,
                           seen ? 0xFFFFFFFFu : 0x302820FFu);
        snprintf(buf, sizeof(buf), portTrGame("No. %d"), sAt + 1);
        textAt(lx, by - 166.0f, 0.62f, PAGE_W, ink, buf);
        if (seen) {
            snprintf(buf, sizeof(buf), portTrGame("Max HP %d"), e->hp);
            textAt(lx, by - 185.0f, 0.62f, PAGE_W, ink, buf);
            snprintf(buf, sizeof(buf), portTrGame("Fights as: %s"), portPartyEnemyName(e->host));
            textAt(lx, by - 204.0f, 0.62f, PAGE_W, ink, buf);
            portPm64EnemyTattle(sAt, buf, (int)sizeof(buf));
            wrapped(rx, by - 22.0f, 0.62f, PAGE_W, ink, buf[0] != '\0' ? buf : portTrGame("(No Tattle in this ROM's text.)"));
        } else {
            wrapped(rx, by - 22.0f, 0.62f, PAGE_W, ink,
                    portTrGame("Not met yet. Paper Mario's enemies turn up in the Pit of 100 Trials and in Master Quest."));
        }
    }
    if (sAt > 0) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, 18.0f + by, 0.0f}, (Vec){-1.0f, -1.0f, -1.0f}, white);
        winIconInit();
        winIconSet(ICON_07E, (Vec){440.0f + bx, by, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    if (sAt < n - 1) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, (by - 15.0f) - 240.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f}, white);
        winIconInit();
        winIconSet(ICON_080, (Vec){440.0f + bx, by - 240.0f, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    portRandSetState(seed);
}

const char* portPm64JournalMsg(const char* tag) {
    static char sHelp[320];
    if (strncmp(tag, "pc_menu_pm64", 12) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_menu_pm64") == 0) {
        portPauseWrap(portTrGame("Paper Mario's enemies you've met, with Goombario's Tattles."), sHelp, sizeof(sHelp));
        return sHelp;
    }
    portPauseWrap(portTrGame("L, R: turn the page."), sHelp, sizeof(sHelp));
    return sHelp;
}
