/* The versus set-up (port/include/port/versus.h), a page of the Start menu
 * (port/include/port/pause_forms.h) in the game's windows and font: Mario's
 * side on the left, Luigi's on the right, a row each for the level and its
 * level-ups (HP, FP, BP), the boots and hammer, three partners and their
 * rank, the badges and the items; then the rules both play by and "Fight!".
 *
 * Up and down choose a row, left and right change it; on a side's rows X
 * goes across to the other side. With a second controller, Luigi's player
 * has a cursor of their own on his side (his pad's up, down, left, right and
 * A), so both set up at once. A on Badges or Items opens that side's list:
 * every badge or item allowed, A puts one on or takes it off, B goes back.
 * B on the page goes back to the Start menu's buttons; A on Fight! starts
 * it (the menu closes, the Glitz Pit's ring next). */
/* the Start menu's statics in a block of their own, the second screen's menu's while it runs (port/companion_section.h) */
#define PORT_SECTION_MENU
#include "port/companion_section.h"
#include "port/menu_tap.h"
#include "port/online.h"
#include "port/pause_forms.h"
#include "port/pause_settings.h"
#include "port/tr.h"
#include "port/versus.h"
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "icon_ids.h"
#include "pmario_sound.h"
#include "system.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

#define PANEL_X (-268.0f)
#define PANEL_W 536.0f
#define TOP 146.0f
#define STEP 16.0f /* the lists' rows */
#define ROW_STEP 14.0f /* the page's (the Fight! row clear of the help window) */
#define SCALE 0.6f
#define HALF 262.0f /* a side's width */
#define LIST_ROWS 13

enum {
    ROW_LEVEL, ROW_HP, ROW_FP, ROW_BP, ROW_GEAR, ROW_PARTNER1, ROW_PARTNER2, ROW_PARTNER3, ROW_RANK, ROW_BADGES,
    ROW_ITEMS, SIDE_ROWS,
    ROW_STAGE = SIDE_ROWS, ROW_TIMER, ROW_LUCK, ROW_SHOWKO, ROW_LUIGI, ROW_FIGHT, ROWS
};

static const char* const kRowNames[ROWS] = {
    "Level", "HP", "FP", "BP", "Boots, hammer", "Partner", "Partner", "Partner", "Partner rank", "Badges", "Items",
    "Stage", "Turn timer", "Luck badges", "Showstopper knocks out", "Luigi's side", "Fight!",
};
static const char* const kGear[3] = {"Normal", "Super", "Ultra"};
/* who plays Luigi's side: a player, or the computer at a skill */
static const char* const kPlayers[4] = {"A player", "Computer: easy", "Computer: normal", "Computer: hard"};
static const int kTimers[] = {0, 15, 30, 60};
#define TIMERS ((int)(sizeof(kTimers) / sizeof(kTimers[0])))

typedef struct Cursor {
    int row, side;
} Cursor;

static Cursor sCur[2];        /* the first controller's, and Luigi's player's own (a second controller) */
static int sList = -1;        /* a side's list open: 0 badges, 1 items (-1: none) */
static int sListSide, sListPad, sListRow, sListTop;
static short sListIds[256];
static int sListCount;
static char sHelp[256];
static const char* sMessage;  /* why Fight! didn't start, or NULL */
static int sLadderPick;       /* Fight!: 0 one fight, 1 the ladder (port/include/port/versus.h) */

/* Luigi's player has a controller of their own: a second one here, or online
 * the guest (whose controller is the second in both games: the page runs in
 * lockstep, so it must read the same in both) */
static int secondPad(void) {
    return portOnlineActive() || PADGetIndexForPort(1) >= 0;
}

static void open_(int arg) {
    (void)arg;
    portVersusSetup();
    sCur[0].row = ROW_LEVEL;
    sCur[0].side = 0;
    sCur[1].row = ROW_LEVEL;
    sCur[1].side = 1;
    sList = -1;
    sMessage = NULL;
}

/* --- changing the set-up --- */

static int partnerTaken(const PortVersusSetup* s, int side, int slot, int p) {
    int k;
    for (k = 0; k < PORT_VERSUS_PARTNERS; k++) {
        if ((k != slot && s->side[side].partners[k] == p) || s->side[!side].partners[k] == p) {
            return 1;
        }
    }
    return 0;
}

/* the luck rule turned off: the luck badges come off both sides */
static void dropFlagged(PortVersusSetup* s) {
    int i, k;
    for (i = 0; i < 2; i++) {
        for (k = 0; k < PORT_VERSUS_BADGES; k++) {
            if (s->side[i].badges[k] != 0 && !portVersusBadgeAllowed(s->side[i].badges[k], &s->rules)) {
                s->side[i].badges[k] = 0;
            }
        }
    }
}

/* badges off from the last while they need more BP than there is */
static void fitBadges(PortVersusSide* d) {
    int k;
    for (k = PORT_VERSUS_BADGES - 1; k >= 0 && portVersusBpUsed(d) > portVersusBp(d); k--) {
        d->badges[k] = 0;
    }
}

static void changeSide(PortVersusSide* d, PortVersusSetup* s, int side, int row, int dir) {
    int* ups[3] = {&d->hpUps, &d->fpUps, &d->bpUps};
    const int spare = d->level - 1 - d->hpUps - d->fpUps - d->bpUps;
    switch (row) {
        case ROW_LEVEL:
            d->level += dir;
            d->level = d->level < 1 ? 1 : d->level > 30 ? 30 : d->level;
            while (d->hpUps + d->fpUps + d->bpUps > d->level - 1) {
                int* last = d->bpUps > 0 ? &d->bpUps : d->fpUps > 0 ? &d->fpUps : &d->hpUps;
                (*last)--;
            }
            fitBadges(d);
            break;
        case ROW_HP:
        case ROW_FP:
        case ROW_BP:
            if (dir > 0 && spare > 0) {
                (*ups[row - ROW_HP])++;
            } else if (dir < 0 && *ups[row - ROW_HP] > 0) {
                (*ups[row - ROW_HP])--;
            }
            fitBadges(d);
            break;
        case ROW_GEAR:
            d->gear = (d->gear - 1 + dir + 3) % 3 + 1;
            break;
        case ROW_PARTNER1:
        case ROW_PARTNER2:
        case ROW_PARTNER3: {
            const int slot = row - ROW_PARTNER1;
            int p = d->partners[slot], n;
            for (n = 0; n < 8; n++) {
                p = (p + dir + 8) % 8;
                if (p == 0 || !partnerTaken(s, side, slot, p)) {
                    break;
                }
            }
            d->partners[slot] = p;
            break;
        }
        case ROW_RANK:
            d->partnerRank = (d->partnerRank + dir + 3) % 3;
            break;
    }
}

static void changeRules(PortVersusSetup* s, int row, int dir) {
    int i;
    switch (row) {
        case ROW_STAGE:
            s->rules.stage = (s->rules.stage + dir + PORT_VERSUS_STAGES) % PORT_VERSUS_STAGES;
            break;
        case ROW_TIMER:
            for (i = 0; i < TIMERS && kTimers[i] != s->rules.turnTimer; i++) {
            }
            i = i >= TIMERS ? 0 : i;
            s->rules.turnTimer = kTimers[(i + dir + TIMERS) % TIMERS];
            break;
        case ROW_LUCK:
            s->rules.luckBadges = !s->rules.luckBadges;
            dropFlagged(s);
            break;
        case ROW_SHOWKO:
            s->rules.showstopperKo = !s->rules.showstopperKo;
            break;
        case ROW_FIGHT:
            sLadderPick = !sLadderPick;
            break;
        case ROW_LUIGI: {
            PortVersusSide* d = &s->side[1];
            i = d->controller == PORT_VERSUS_COMPUTER ? d->skill + 1 : 0;
            i = (i + dir + 4) % 4;
            d->controller = i > 0 ? PORT_VERSUS_COMPUTER : PORT_VERSUS_LOCAL;
            d->skill = i > 0 ? i - 1 : d->skill;
            break;
        }
    }
}

/* --- the lists --- */

static int hasEntry(const short* v, int n, int id) {
    int k;
    for (k = 0; k < n; k++) {
        if (v[k] == id) {
            return 1;
        }
    }
    return 0;
}

static int itemCount(const short* v, int n, int id) {
    int k, c = 0;
    for (k = 0; k < n; k++) {
        c += v[k] == id;
    }
    return c;
}

static void openList(int which, int side, int pad) {
    const PortVersusSetup* s = portVersusSetup();
    int id;
    sList = which;
    sListSide = side;
    sListPad = pad;
    sListRow = sListTop = 0;
    sListCount = 0;
    if (which == 0) {
        for (id = ITEM_POWER_JUMP; id < BADGE_MAX && sListCount < 256; id++) {
            if (portVersusBadgeAllowed(id, &s->rules)) {
                sListIds[sListCount++] = (short)id;
            }
        }
    } else {
        for (id = ITEM_THUNDER_BOLT; id < ITEM_MAX && sListCount < 256; id++) {
            if (portVersusItemAllowed(id)) {
                sListIds[sListCount++] = (short)id;
            }
        }
    }
}

/* A on a list's row: a badge on or off (if the BP allow it); an item one
 * more (up to the most), or, with right/left, one more or less */
static void listAct(int dir) {
    PortVersusSetup* s = portVersusSetup();
    PortVersusSide* d = &s->side[sListSide];
    const int id = sListIds[sListRow];
    int k;
    if (sList == 0) {
        if (hasEntry(d->badges, PORT_VERSUS_BADGES, id)) {
            for (k = 0; k < PORT_VERSUS_BADGES; k++) {
                if (d->badges[k] == id) {
                    d->badges[k] = 0;
                    break;
                }
            }
            psndSFXOn((const char*)0x20013);
            return;
        }
        if (portVersusBpUsed(d) + itemDataTable[id].bp_cost > portVersusBp(d)) {
            psndSFXOn((const char*)0x20013); /* (the menu's "can't") */
            return;
        }
        for (k = 0; k < PORT_VERSUS_BADGES && d->badges[k] != 0; k++) {
        }
        if (k < PORT_VERSUS_BADGES) {
            d->badges[k] = (short)id;
            psndSFXOn((const char*)0x20012);
        }
        return;
    }
    if (dir < 0) {
        for (k = PORT_VERSUS_ITEMS - 1; k >= 0; k--) {
            if (d->items[k] == id) {
                d->items[k] = 0;
                psndSFXOn((const char*)0x20013);
                return;
            }
        }
        return;
    }
    for (k = 0; k < PORT_VERSUS_ITEMS && d->items[k] != 0; k++) {
    }
    if (k < PORT_VERSUS_ITEMS) {
        d->items[k] = (short)id;
        psndSFXOn((const char*)0x20012);
    } else {
        psndSFXOn((const char*)0x20013);
    }
}

static int listMain(u32 trg, u32 rep) {
    if (trg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        sList = -1;
        return 0;
    }
    if (trg & PAD_BUTTON_A) {
        listAct(1);
    } else if (rep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        sListRow = (sListRow + ((rep & DEMO_STICK_UP) ? -1 : 1) + sListCount) % sListCount;
        psndSFXOn((const char*)0x20005);
    } else if (sList == 1 && (rep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT))) {
        listAct((rep & DEMO_STICK_LEFT) ? -1 : 1);
    }
    if (sListRow < sListTop) {
        sListTop = sListRow;
    }
    if (sListRow >= sListTop + LIST_ROWS) {
        sListTop = sListRow - LIST_ROWS + 1;
    }
    return 0;
}

/* --- the page's input --- */

static f32 rowY(int row) {
    return row < SIDE_ROWS ? TOP - 22.0f - row * ROW_STEP : TOP - 28.0f - SIDE_ROWS * ROW_STEP - (row - SIDE_ROWS) * 13.0f;
}

/* one controller's input on its cursor: pad 0 the first, 1 Luigi's player's (his side only) */
static int cursorMain(Cursor* c, int pad, u32 trg, u32 rep) {
    PortVersusSetup* s = portVersusSetup();
    const int maxRow = pad == 0 ? ROWS : SIDE_ROWS;
    if (trg & PAD_BUTTON_A) {
        if (c->row == ROW_BADGES || c->row == ROW_ITEMS) {
            psndSFXOn((const char*)0x20012);
            openList(c->row == ROW_BADGES ? 0 : 1, c->side, pad);
        } else if (c->row == ROW_FIGHT) {
            const char* why = NULL;
            if (sLadderPick ? portVersusLadderStart(&why) : portVersusStart(&why)) {
                psndSFXOn((const char*)0x20012);
                return -2; /* the whole menu closes: the fight next */
            }
            psndSFXOn((const char*)0x20013);
            sMessage = why;
        } else if (c->row < SIDE_ROWS) {
            changeSide(&s->side[c->side], s, c->side, c->row, 1);
            psndSFXOn((const char*)0x20005);
        } else {
            changeRules(s, c->row, 1);
            psndSFXOn((const char*)0x20005);
        }
    } else if (pad == 0 && (trg & (PAD_BUTTON_X | PAD_BUTTON_Y)) && c->row < SIDE_ROWS) {
        c->side = !c->side;
        psndSFXOn((const char*)0x20005);
    } else if (rep & (DEMO_STICK_UP | DEMO_STICK_DOWN)) {
        c->row = (c->row + ((rep & DEMO_STICK_UP) ? -1 : 1) + maxRow) % maxRow;
        sMessage = NULL;
        psndSFXOn((const char*)0x20005);
    } else if (rep & (DEMO_STICK_LEFT | DEMO_STICK_RIGHT)) {
        const int dir = (rep & DEMO_STICK_LEFT) ? -1 : 1;
        if (c->row < SIDE_ROWS) {
            changeSide(&s->side[c->side], s, c->side, c->row, dir);
        } else {
            changeRules(s, c->row, dir);
        }
        psndSFXOn((const char*)0x20005);
    }
    return 0;
}

static int main_(WindowWork* wp) {
    int ret;
    if (sList >= 0) {
        const u32 trg = sListPad == 0 ? wp->buttonTrg : keyGetButtonTrg(1);
        const u32 rep = sListPad == 0 ? wp->dirRep : keyGetDirRep(1);
        listMain(trg, rep);
        wp->helpTgtX = (sListSide == 0 ? PANEL_X + 8.0f : PANEL_X + HALF + 20.0f) - 12.0f;
        wp->helpTgtY = TOP - 40.0f - (sListRow - sListTop) * STEP - 8.0f;
        return 0;
    }
    if (wp->buttonTrg & PAD_BUTTON_B) {
        psndSFXOn((const char*)0x20013);
        portVersusSave();
        return -1;
    }
    if ((ret = cursorMain(&sCur[0], 0, wp->buttonTrg, wp->dirRep)) != 0) {
        return ret;
    }
    if (secondPad() && sList < 0) { /* Luigi's player, at the same time */
        sCur[1].side = 1;
        cursorMain(&sCur[1], 1, keyGetButtonTrg(1), keyGetDirRep(1));
    }
    /* the hand at the first controller's row */
    wp->helpTgtX = (sCur[0].row >= SIDE_ROWS ? PANEL_X + 138.0f : sCur[0].side == 0 ? PANEL_X + 8.0f
                                                                                     : PANEL_X + HALF + 20.0f) - 14.0f;
    wp->helpTgtY = rowY(sCur[0].row) - 8.0f;
    return 0;
}

static const char* help(void) {
    static const char* const kNotes[ROWS] = {
        "The side's level: each level-up above 1 goes to HP, FP or BP.",
        "Left and right put a level-up into HP (5 each) or take one back.",
        "Left and right put a level-up into FP (5 each) or take one back.",
        "Left and right put a level-up into BP (3 each) or take one back.",
        "The boots and hammer: the jump and hammer moves they give.",
        "The partner who comes out first; the others swap in from the Tactics menu.",
        "A partner to swap to. Each partner fights on one side only.",
        "A partner to swap to. Each partner fights on one side only.",
        "The partners' rank: their HP and their moves.",
        "{A} opens the badges: as many as the BP allow.",
        "{A} opens the items: up to six. None at all is fine.",
        "Where the fight is: the Glitz Pit's ring, or a battle stage from one of the chapters, with its audience and music.",
        "How long each command can take; when it runs out the turn is passed.",
        "Close Call, Pretty Lucky, Lucky Day and Lucky Start leave hits to chance: off by default.",
        "Off: Showstopper takes up to a third of each foe's HP instead of knocking them out. On: as in the story.",
        "Who plays Luigi's side: a player on the second controller, or the computer, easy, normal or hard.",
        "Starts the fight. Mario's player is the first controller, Luigi's the second.",
    };
    const char* text;
    if (sList >= 0) {
        text = sList == 0 ? "{A} puts the badge on or takes it off. {B} goes back."
                          : "{A} or right adds one, left takes one back. {B} goes back.";
    } else if (sMessage != NULL) {
        text = sMessage;
    } else if (sCur[0].row == ROW_FIGHT && sLadderPick) {
        text = "The ladder: Mario's side against the computer's Luigi, a stronger one after each win, until Mario falls.";
    } else if (sCur[0].row < SIDE_ROWS && !secondPad()) {
        /* (each half in the game's encoding: a joined text has no table entry) */
        char both[200];
        snprintf(both, sizeof(both), "%s %s", portTrGame(kNotes[sCur[0].row]), portTrGame("{X}: the other side."));
        portPauseWrap(both, sHelp, sizeof(sHelp));
        return sHelp;
    } else {
        text = kNotes[sCur[0].row];
    }
    portPauseWrap(portTrGame(text), sHelp, sizeof(sHelp));
    return sHelp;
}

/* --- drawing --- */

static void text(f32 x, f32 y, f32 w, f32 s, GXColor c, const char* t, int right) {
    const f32 full = (f32)FontGetMessageWidth(t);
    const f32 k = full * s > w ? w / full : s;
    winFontInit();
    winFontSet((Vec){right ? x - full * k : x, y, 0.0f}, (Vec){k, s, s}, c, "%s", t);
}

static const char* partnerName(int p) {
    static const char* const kTags[8] = {NULL,          "name_party0", "name_party1", "name_party2",
                                         "name_party3", "name_party4", "name_party5", "name_party6"};
    return p > 0 && p < 8 ? msgSearch(kTags[p]) : portTrGame("None");
}

static void sideValue(const PortVersusSide* d, int row, char* out, size_t size) {
    switch (row) {
        case ROW_LEVEL:
            snprintf(out, size, "%d", d->level);
            if (d->hpUps + d->fpUps + d->bpUps < d->level - 1) {
                snprintf(out, size, "%d (%d %s)", d->level, d->level - 1 - d->hpUps - d->fpUps - d->bpUps,
                         portTrGame("to share"));
            }
            break;
        case ROW_HP:
            snprintf(out, size, "%d", portVersusMaxHp(d));
            break;
        case ROW_FP:
            snprintf(out, size, "%d", portVersusMaxFp(d));
            break;
        case ROW_BP:
            snprintf(out, size, "%d", portVersusBp(d));
            break;
        case ROW_GEAR:
            snprintf(out, size, "%s", portTrGame(kGear[d->gear - 1]));
            break;
        case ROW_PARTNER1:
        case ROW_PARTNER2:
        case ROW_PARTNER3:
            snprintf(out, size, "%s", partnerName(d->partners[row - ROW_PARTNER1]));
            break;
        case ROW_RANK:
            snprintf(out, size, "%s", portTrGame(kGear[d->partnerRank]));
            break;
        case ROW_BADGES:
            snprintf(out, size, "%d/%d BP", portVersusBpUsed(d), portVersusBp(d));
            break;
        case ROW_ITEMS: {
            int k, n = 0;
            for (k = 0; k < PORT_VERSUS_ITEMS; k++) {
                n += d->items[k] != 0;
            }
            snprintf(out, size, "%d/%d", n, PORT_VERSUS_ITEMS);
            break;
        }
        default:
            out[0] = '\0';
    }
}

static void listDisp(WindowWork* wp, f32 x, f32 y) {
    const PortVersusSetup* s = portVersusSetup();
    const PortVersusSide* d = &s->side[sListSide];
    const GXColor black = {0x00, 0x00, 0x00, 0xFF}, grey = {0x60, 0x60, 0x60, 0xFF}, red = {0xA0, 0x20, 0x20, 0xFF};
    const f32 lx = x + (sListSide == 0 ? PANEL_X + 8.0f : PANEL_X + HALF + 20.0f);
    char num[32];
    int i;
    (void)wp;
    text(lx, y + TOP - 18.0f, HALF - 30.0f, 0.7f, black,
         portTrGame(sList == 0 ? (sListSide ? "Luigi's badges" : "Mario's badges")
                               : (sListSide ? "Luigi's items" : "Mario's items")),
         0);
    if (sList == 0) {
        snprintf(num, sizeof(num), "%d/%d BP", portVersusBpUsed(d), portVersusBp(d));
    } else {
        int k, n = 0;
        for (k = 0; k < PORT_VERSUS_ITEMS; k++) {
            n += d->items[k] != 0;
        }
        snprintf(num, sizeof(num), "%d/%d", n, PORT_VERSUS_ITEMS);
    }
    text(lx + HALF - 30.0f, y + TOP - 18.0f, 100.0f, 0.7f, black, num, 1);
    winIconInit();
    for (i = 0; i < LIST_ROWS && sListTop + i < sListCount; i++) {
        const int id = sListIds[sListTop + i];
        winIconSet(itemDataTable[id].icon_id, (Vec){lx + 8.0f, y + TOP - 46.0f - i * STEP, 0.0f},
                   (Vec){0.42f, 0.42f, 1.0f}, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    }
    for (i = 0; i < LIST_ROWS && sListTop + i < sListCount; i++) {
        const int id = sListIds[sListTop + i];
        const f32 ry = y + TOP - 40.0f - i * STEP;
        const int on = sList == 0 ? hasEntry(d->badges, PORT_VERSUS_BADGES, id) : itemCount(d->items, PORT_VERSUS_ITEMS, id);
        const GXColor c = sListTop + i == sListRow ? black : grey;
        text(lx + 20.0f, ry, HALF - 110.0f, SCALE, on ? red : c, msgSearch(itemDataTable[id].name), 0);
        if (sList == 0) {
            snprintf(num, sizeof(num), "%s%d BP", on ? "* " : "", itemDataTable[id].bp_cost);
        } else {
            snprintf(num, sizeof(num), on ? "x%d" : "-", on);
        }
        text(lx + HALF - 30.0f, ry, 70.0f, SCALE, on ? red : c, num, 1);
    }
}

static void disp(WindowWork* wp, float x, float y) {
    const PortVersusSetup* s = portVersusSetup();
    const GXColor black = {0x00, 0x00, 0x00, 0xFF}, grey = {0x60, 0x60, 0x60, 0xFF};
    const GXColor red = {0xC0, 0x20, 0x20, 0xFF}, green = {0x20, 0x90, 0x30, 0xFF};
    char value[96];
    int side, row;
    winKirinukiGX(wp, 0, x + PANEL_X, y + TOP + 8.0f, PANEL_W, 278.0f);
    if (sList >= 0) {
        listDisp(wp, x, y);
    }
    for (side = 0; side < 2; side++) {
        const PortVersusSide* d = &s->side[side];
        const f32 sx = x + PANEL_X + 8.0f + side * (HALF + 12.0f);
        if (sList >= 0 && sListSide == side) {
            continue; /* (its list over it) */
        }
        text(sx, y + TOP, HALF - 20.0f, 0.75f, side ? green : red, portTrGame(side ? "Luigi" : "Mario"), 0);
        winIconInit();
        winIconSet(side ? itemDataTable[ITEM_L_EMBLEM].icon_id : ICON_19E, /* (Luigi: the L Emblem's) */ (Vec){sx + HALF - 30.0f, y + TOP - 8.0f, 0.0f}, (Vec){0.4f, 0.4f, 1.0f},
                   (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
        for (row = 0; row < SIDE_ROWS; row++) {
            const int mine = (sCur[0].row == row && sCur[0].side == side) ||
                             (side == 1 && secondPad() && sCur[1].row == row);
            const GXColor c = mine ? black : grey;
            const char* name = portTrGame(kRowNames[row]);
            text(sx + 12.0f, y + rowY(row), 110.0f, SCALE, c, name, 0);
            sideValue(d, row, value, sizeof(value));
            text(sx + HALF - 30.0f, y + rowY(row), 130.0f, SCALE, c, value, 1);
        }
    }
    /* the rules, across both */
    for (row = SIDE_ROWS; row < ROWS; row++) {
        const GXColor c = sCur[0].row == row ? black : grey;
        const f32 lx = x + PANEL_X + 150.0f;
        text(lx, y + rowY(row), 185.0f, row == ROW_FIGHT ? 0.7f : SCALE, row == ROW_FIGHT && sCur[0].row == row ? red : c,
             portTrGame(kRowNames[row]), 0);
        value[0] = '\0';
        if (row == ROW_STAGE) {
            snprintf(value, sizeof(value), "%s", portVersusStageName(s->rules.stage));
        } else if (row == ROW_TIMER) {
            if (s->rules.turnTimer > 0) {
                snprintf(value, sizeof(value), "%d s", s->rules.turnTimer);
            } else {
                snprintf(value, sizeof(value), "%s", portTrGame("Off"));
            }
        } else if (row == ROW_LUCK) {
            snprintf(value, sizeof(value), "%s", portTrGame(s->rules.luckBadges ? "Allowed" : "Not allowed"));
        } else if (row == ROW_SHOWKO) {
            snprintf(value, sizeof(value), "%s", portTrGame(s->rules.showstopperKo ? "On" : "Off"));
        } else if (row == ROW_LUIGI) {
            const PortVersusSide* d = &s->side[1];
            snprintf(value, sizeof(value), "%s",
                     portTrGame(kPlayers[d->controller == PORT_VERSUS_COMPUTER ? d->skill + 1 : 0]));
        } else if (row == ROW_FIGHT) {
            int best, last;
            portVersusLadder(&best, &last);
            if (!sLadderPick) {
                snprintf(value, sizeof(value), "%s", portTrGame("One fight"));
            } else if (best > 0) {
                snprintf(value, sizeof(value), "%s (%s %d)", portTrGame("The ladder"), portTrGame("best"), best);
            } else {
                snprintf(value, sizeof(value), "%s", portTrGame("The ladder"));
            }
        }
        text(x + PANEL_X + PANEL_W - 150.0f, y + rowY(row), 140.0f, SCALE, c, value, 1);
    }
}

const PortPausePage gPortVersusSetupPage = {open_, main_, disp, help};
