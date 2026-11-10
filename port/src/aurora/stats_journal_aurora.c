/* The Journal's Stats (port/include/port/stats.h): the lifetime stats in a book as the Tattle log's, after the
 * game's own entries and Photos. A spread at a time: the adventure and the battles; the moves in battle and the
 * grades; the time per chapter and the enemies defeated, kind by kind, on as many pages as they take. A switches
 * between this file's and every file's on this device, L and R (or the stick) turn the pages, B goes back.
 *
 * Draw-only: it reads the counts, never the game's state, and keeps the game's random numbers as they were (the
 * font takes two a letter). Not there online or in a replay, as Photos aren't (the Journal's entries are the
 * same on both sides). */
#include "port/text_fit.h"
#include "port/pause_settings.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/battlex.h"
#include "port/stats.h"
#include "port/tr.h"
#include "battle/battle_monosiri.h"
#include "driver/msgdrv.h"
#include "icon_ids.h"
#include "manager/fontmgr.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the book's top left, from the Journal's (x, y): where the Tattle log's is (and the Photos') */
#define BOOK_DX (-170.0f)
#define BOOK_DY 130.0f
/* each page's text: from the book's left, its width; the rows from the top */
static const float kPageX[2] = {20.0f, 250.0f};
#define PAGE_W 186.0f
#define ROW_TOP 22.0f
#define ROW_H 19.0f
#define ROWS 11       /* a page's rows, its heading's among them */
#define ENEMY_ROWS 10 /* a page of the enemies: its heading and ten */

static int sAll;   /* every file's (1) or this file's (0) */
static int sSpread; /* the spread shown */
static int sOpen;

/* --- what's on the pages --- */

typedef struct Row {
    char label[80];
    char value[32];
    int heading;
} Row;

typedef struct Page {
    Row rows[ROWS];
    int count;
} Page;

#define MAX_PAGES 64
static Page sPages[MAX_PAGES];
static int sPageCount;

static const PortStats* shown(void) {
    const PortStats* f = portStatsFile();
    return sAll || f == NULL ? portStatsTotals() : f;
}

static Page* newPage(const char* heading) {
    Page* p;
    if (sPageCount >= MAX_PAGES) {
        return NULL;
    }
    p = &sPages[sPageCount++];
    memset(p, 0, sizeof(*p));
    snprintf(p->rows[0].label, sizeof(p->rows[0].label), "%s", heading);
    p->rows[0].heading = 1;
    p->count = 1;
    return p;
}

static void row(Page* p, const char* label, const char* value) {
    Row* r;
    if (p == NULL || p->count >= ROWS) {
        return;
    }
    r = &p->rows[p->count++];
    snprintf(r->label, sizeof(r->label), "%s", label);
    snprintf(r->value, sizeof(r->value), "%s", value);
}

static void rowN(Page* p, const char* label, long long n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", n);
    row(p, label, buf);
}

static void hoursMinutes(char* out, size_t size, long long frames) {
    const long long minutes = frames / (60 * 60);
    snprintf(out, size, "%lld:%02lld", minutes / 60, minutes % 60);
}

/* an enemy's name in the game's encoding, its control tags left out ("" none) */
static void enemyName(int kind, char* out, size_t size) {
    const TattleEntry* t = battleGetUnitMonosiriPtr(kind);
    const char* s = t != NULL && t->enemyTag != NULL ? msgSearch(t->enemyTag) : NULL;
    size_t n = 0;
    out[0] = '\0';
    if (s == NULL) {
        return;
    }
    for (; *s != '\0' && n + 1 < size; s++) {
        if (*s == '<') {
            while (*s != '\0' && *s != '>') {
                s++;
            }
            if (*s == '\0') {
                break;
            }
            continue;
        }
        if (*s == '\n') {
            continue;
        }
        out[n++] = *s;
    }
    out[n] = '\0';
}

static int byCount(const void* a, const void* b) {
    const PortStats* s = shown();
    const int ka = *(const int*)a, kb = *(const int*)b;
    if (s->enemies[ka] != s->enemies[kb]) {
        return s->enemies[ka] < s->enemies[kb] ? 1 : -1;
    }
    return ka - kb;
}

static void build(void) {
    const PortStats* s = shown();
    char buf[64];
    Page* p;
    int i, g, best = -1, last = 0;
    int kinds[PORT_STATS_KINDS], kindCount = 0;
    sPageCount = 0;

    /* 1: the adventure, the battles */
    p = newPage(portTrGame("Adventure"));
    hoursMinutes(buf, sizeof(buf), s->v[PORT_STAT_PLAY_FRAMES]);
    row(p, portTrGame("Time played"), buf);
    if (s->v[PORT_STAT_DISTANCE] >= 100000) {
        snprintf(buf, sizeof(buf), "%lld.%lld km", s->v[PORT_STAT_DISTANCE] / 100000,
                 s->v[PORT_STAT_DISTANCE] / 10000 % 10);
    } else {
        snprintf(buf, sizeof(buf), "%lld m", s->v[PORT_STAT_DISTANCE] / 100);
    }
    row(p, portTrGame("Distance walked"), buf);
    rowN(p, portTrGame("Coins gained"), s->v[PORT_STAT_COINS]);
    rowN(p, portTrGame("Jumps"), s->v[PORT_STAT_FIELD_JUMPS]);
    rowN(p, portTrGame("Spin Jumps"), s->v[PORT_STAT_SPIN_JUMPS]);
    rowN(p, portTrGame("Hammer swings"), s->v[PORT_STAT_FIELD_HAMMERS]);
    rowN(p, portTrGame("Enemies defeated"), s->v[PORT_STAT_ENEMIES]);
    rowN(p, portTrGame("Star Points earned"), s->v[PORT_STAT_STAR_POINTS]);
    p = newPage(portTrGame("Battles"));
    rowN(p, portTrGame("Battles won"), s->v[PORT_STAT_BATTLES_WON]);
    rowN(p, portTrGame("Battles fled"), s->v[PORT_STAT_BATTLES_FLED]);
    rowN(p, portTrGame("Battles lost"), s->v[PORT_STAT_BATTLES_LOST]);
    rowN(p, portTrGame("Turns"), s->v[PORT_STAT_TURNS]);
    rowN(p, portTrGame("Damage dealt"), s->v[PORT_STAT_DAMAGE_DEALT]);
    rowN(p, portTrGame("Damage taken"), s->v[PORT_STAT_DAMAGE_TAKEN]);
    rowN(p, portTrGame("Best hit"), s->v[PORT_STAT_BEST_HIT]);
    rowN(p, portTrGame("Superguards"), s->v[PORT_STAT_SUPERGUARDS]);
    rowN(p, portTrGame("Guards"), s->v[PORT_STAT_GUARDS]);
    rowN(p, portTrGame("Stylish moves"), s->v[PORT_STAT_STYLISH]);

    /* 2: the moves in battle, the grades */
    p = newPage(portTrGame("In battle"));
    rowN(p, portTrGame("Jump attacks"), s->v[PORT_STAT_JUMP_ATTACKS]);
    rowN(p, portTrGame("Hammer attacks"), s->v[PORT_STAT_HAMMER_ATTACKS]);
    rowN(p, portTrGame("Special moves"), s->v[PORT_STAT_SPECIALS]);
    rowN(p, portTrGame("Partner moves"), s->v[PORT_STAT_PARTNER_MOVES]);
    rowN(p, portTrGame("Items used"), s->v[PORT_STAT_ITEMS_USED]);
    rowN(p, portTrGame("FP spent"), s->v[PORT_STAT_FP_SPENT]);
    rowN(p, portTrGame("Appeals"), s->v[PORT_STAT_APPEALS]);
    p = newPage(portTrGame("Battle grades"));
    for (g = PORT_GRADE_COUNT - 1; g >= 0; g--) {
        snprintf(buf, sizeof(buf), portTrGame("Grade %s"), portBattleGradeLetter(g));
        rowN(p, buf, s->v[PORT_STAT_GRADE_D + g]);
        if (best < 0 && s->v[PORT_STAT_GRADE_D + g] > 0) {
            best = g;
        }
    }
    row(p, portTrGame("Best grade"), best >= 0 ? portBattleGradeLetter(best) : "-");

    /* 3: the time per chapter, then the enemies */
    p = newPage(portTrGame("Time per chapter"));
    for (i = 0; i < PORT_STATS_CHAPTERS; i++) {
        if (s->chapterFrames[i] > 0) {
            last = i;
        }
    }
    for (i = 0; i <= last; i++) {
        char name[48];
        if (i == 0) {
            snprintf(name, sizeof(name), "%s", portTrGame("Prologue"));
        } else if (i == PORT_STATS_CHAPTERS - 1) {
            snprintf(name, sizeof(name), "%s", portTrGame("Epilogue and after"));
        } else {
            snprintf(name, sizeof(name), portTrGame("Chapter %d"), i);
        }
        hoursMinutes(buf, sizeof(buf), s->chapterFrames[i]);
        row(p, name, buf);
    }
    for (i = 1; i < PORT_STATS_KINDS; i++) {
        if (s->enemies[i] > 0) {
            kinds[kindCount++] = i;
        }
    }
    qsort(kinds, (size_t)kindCount, sizeof(kinds[0]), byCount);
    p = newPage(portTrGame("Enemies defeated"));
    if (kindCount == 0) {
        row(p, portTrGame("None yet."), "");
    }
    for (i = 0; i < kindCount; i++) {
        char name[64];
        if (p != NULL && p->count > ENEMY_ROWS) {
            p = newPage(portTrGame("Enemies defeated"));
        }
        enemyName(kinds[i], name, sizeof(name));
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "#%d", kinds[i]);
        }
        snprintf(buf, sizeof(buf), "%u", s->enemies[kinds[i]]);
        row(p, name, buf);
    }
    if (sPageCount % 2 != 0) {
        newPage(""); /* (the last spread's right page blank) */
        sPages[sPageCount - 1].count = 0;
    }
}

static int spreads(void) {
    return (sPageCount + 1) / 2;
}

/* --- the entry --- */

int portStatsJournalEntry(void) {
    return !portReplayActive();
}

void portStatsJournalOpen(struct WindowWork* wp) {
    (void)wp;
    sOpen = 1;
    if (portStatsFile() == NULL) {
        sAll = 1;
    }
    build();
    if (sSpread >= spreads()) {
        sSpread = 0;
    }
}

/* the help box: "pc_menu_stats_page" with what it says in its tag, so a change is seen (winMsgEntry) */
static void help(WindowWork* wp) {
    static char sTag[64];
    snprintf(sTag, sizeof(sTag), "pc_menu_stats_page_%d_%d", sAll, sSpread);
    winMsgEntry(wp, 0, sTag, NULL);
}

int portStatsJournalMain(struct WindowWork* wp) {
    const int n = spreads();
    if (!sOpen) {
        portStatsJournalOpen(wp); /* (a save state loaded in here) */
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
    if (wp->buttonTrg & PAD_BUTTON_A) {
        if (portStatsFile() != NULL) {
            sAll = !sAll;
            build();
            if (sSpread >= spreads()) {
                sSpread = spreads() - 1;
            }
            psndSFXOn(SFX_NAME_WORD(0x20012));
        }
    } else if ((wp->buttonRep & PAD_TRIGGER_R) || (wp->dirRep & DEMO_STICK_RIGHT)) {
        if (sSpread + 1 < n) {
            sSpread++;
            psndSFXOn(SFX_NAME_WORD(0x20035));
        }
    } else if ((wp->buttonRep & PAD_TRIGGER_L) || (wp->dirRep & DEMO_STICK_LEFT)) {
        if (sSpread > 0) {
            sSpread--;
            psndSFXOn(SFX_NAME_WORD(0x20035));
        }
    } else {
        build(); /* (the counts as they are: a battle can't end here, but the time goes on) */
    }
    wp->helpTgtX = BOOK_DX - 128.0f; /* (the hand at the box beside the book) */
    wp->helpTgtY = BOOK_DY - 172.0f;
    help(wp);
    return 0;
}

/* --- drawing --- */

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

static void pageDisp(const Page* p, f32 x, f32 y) {
    const GXColor ink = {0x20, 0x18, 0x10, 0xFF};
    const GXColor head = {0x90, 0x28, 0x20, 0xFF};
    const GXColor rule = {0xB0, 0xA0, 0x88, 0xFF};
    int i;
    for (i = 0; i < p->count; i++) {
        const Row* r = &p->rows[i];
        const f32 ry = y - ROW_TOP - ROW_H * (f32)i;
        if (r->heading) {
            centredText(x + PAGE_W / 2.0f, ry + 6.0f, 0.8f, PAGE_W, head, r->label);
            continue;
        }
        {
            const f32 vs = 0.68f;
            const f32 vw = r->value[0] != '\0' ? (f32)FontGetMessageWidth(r->value) * vs : 0.0f;
            textAt(x, ry, 0.62f, PAGE_W - vw - 10.0f, ink, r->label);
            if (r->value[0] != '\0') {
                textAt(x + PAGE_W - vw, ry, vs, PAGE_W, ink, r->value);
            }
        }
    }
    (void)rule;
}

void portStatsJournalDisp(struct WindowWork* wp, float x, float y) {
    const GXColor black = {0x00, 0x00, 0x00, 0xFF};
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const f32 bx = x + BOOK_DX, by = y + BOOK_DY;
    const int n = spreads();
    const u32 seed = portRandState(); /* (the font's random numbers: as they were) */
    char buf[32];
    int side;
    winBookGX(wp, 1, bx, by);
    /* whose, and the page, in the box beside the book as the Tattle log's count */
    winKirinukiGX(wp, 0, bx - 110.0f, by - 160.0f, 100.0f, 70.0f);
    centredText(bx - 60.0f, by - 166.0f, 0.75f, 88.0f, black, portTrGame("Records"));
    centredText(bx - 60.0f, by - 184.0f, 0.6f, 88.0f, black,
                sAll || portStatsFile() == NULL ? portTrGame("All files") : portTrGame("This file"));
    snprintf(buf, sizeof(buf), "%d/%d", sSpread + 1, n);
    centredText(bx - 60.0f, by - 202.0f, 0.65f, 88.0f, black, buf);
    for (side = 0; side < 2; side++) {
        const int at = sSpread * 2 + side;
        if (at < sPageCount) {
            pageDisp(&sPages[at], bx + kPageX[side], by);
        }
    }
    /* the page arrows and L and R, as the Tattle log's */
    if (sSpread > 0) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, 18.0f + by, 0.0f}, (Vec){-1.0f, -1.0f, -1.0f}, white);
        winIconInit();
        winIconSet(ICON_07E, (Vec){440.0f + bx, by, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    if (sSpread < n - 1) {
        winTexInit(*wp->res->data);
        winTexSet(0x17, (Vec){440.0f + bx, (by - 15.0f) - 240.0f, 0.0f}, (Vec){1.0f, 1.0f, 1.0f}, white);
        winIconInit();
        winIconSet(ICON_080, (Vec){440.0f + bx, by - 240.0f, 0.0f}, (Vec){0.6f, 0.6f, 0.6f}, white);
    }
    portRandSetState(seed);
}

/* --- the help box --- */

const char* portStatsJournalMsg(const char* tag) {
    static char sHelp[320];
    if (strncmp(tag, "pc_menu_stats", 13) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_menu_stats") == 0) {
        portPauseWrap(portTrGame("What you've done: on this file, and on every file."), sHelp,
                      sizeof(sHelp));
        return sHelp;
    }
    if (portStatsFile() == NULL) {
        portPauseWrap(portTrGame("L, R: turn the page."), sHelp, sizeof(sHelp));
    } else {
        portPauseWrap(portTrGame("L, R: turn the page. A: this file or all files."), sHelp, sizeof(sHelp));
    }
    return sHelp;
}
