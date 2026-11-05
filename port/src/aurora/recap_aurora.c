/* The battle recap (port/include/port/stats.h, docs/BATTLE_EXTRAS.md): each
 * battle won, one card low on the picture with the battle grade (when
 * grades are on) and, as "battle_recap" says, the battle's numbers: a line
 * (short) or two columns of them (full). It comes up as the battle ends,
 * stays a few seconds and goes; A or B puts it away sooner.
 *
 * Draw-only, and the same on both machines online: it never holds the game
 * (nothing waits on it), counts its time in the game's frames (no clock),
 * reads the button presses both players' games share, and keeps the game's
 * random numbers as they were (the game's font takes two a letter). Its
 * texts are in each player's own menu language, which is why.
 *
 * Also the frame hook that counts the lifetime stats (stats_pc.c). */
#include "port/battlex.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/stats.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/seqdrv.h"
#include "driver/windowdrv.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"
#include "win/win_main.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>

#define ENTER 14  /* frames it takes to come down */
#define LEAVE 12  /* and to go */
#define SETTLE 20 /* frames before a press can put it away (not the press that won the battle) */

static int* sMode;   /* "battle_recap": 0 off, 1 short, 2 full */
static unsigned sSeen;
static PortBattleRecap sCard;
static int sStyle;      /* the card shown: 0 the grade alone, 1 short, 2 full */
static int sAge = -1;   /* frames shown, -1 none */
static int sLife;       /* frames it stays */
static int sLeave = -1; /* frames into going, -1 not yet */

/* --- drawing --- */

static const GXColor kInk = {0x20, 0x20, 0x20, 0xFF};
static const GXColor kSoft = {0x60, 0x58, 0x50, 0xFF};

static GXColor gradeColor(int grade) {
    static const GXColor k[PORT_GRADE_COUNT] = {
        {0x80, 0x80, 0x80, 0xFF}, /* D */
        {0x30, 0x90, 0x48, 0xFF}, /* C */
        {0x30, 0x60, 0xC8, 0xFF}, /* B */
        {0xD8, 0x30, 0x30, 0xFF}, /* A */
        {0xF0, 0xB0, 0x10, 0xFF}, /* S */
    };
    return grade >= 0 && grade < PORT_GRADE_COUNT ? k[grade] : kInk;
}

static GXColor faded(GXColor c, u8 a) {
    c.a = (u8)(c.a * a / 0xFF);
    return c;
}

static void text(f32 x, f32 y, f32 scale, GXColor c, u8 a, const char* s) {
    FontDrawStart_alpha(a);
    FontDrawScale(scale);
    FontDrawColor(faded(c, a));
    FontDrawString(x, y, s);
}

static f32 width(const char* s, f32 scale) {
    return (f32)FontGetMessageWidth(s) * scale;
}

/* the grade's letter, stamped down onto the card as it arrives (big, then its size) */
static void gradeLetter(f32 cx, f32 cy, f32 size, u8 a) {
    const char* letter = portBattleGradeLetter(sCard.grade);
    const int t = sAge - ENTER / 2;
    f32 scale = size;
    if (t < 0) {
        return;
    }
    if (t < 8) {
        scale = size * (1.0f + 0.9f * (f32)(8 - t) / 8.0f);
        a = (u8)(a * t / 8);
    }
    FontDrawStart_alpha(a);
    FontDrawEdge();
    FontDrawScale(scale);
    FontDrawColor(faded(gradeColor(sCard.grade), a));
    /* (the font's letters sit on their line: about 0.8 of the scale above it, centred on cy) */
    FontDrawString(cx - width(letter, scale) / 2.0f, cy + 13.0f * scale, letter);
    FontDrawEdgeOff();
}

/* a coin and its count, from (x, y) at the text's top */
static void coins(f32 x, f32 y, int n, u8 a) {
    char buf[16];
    Vec at = {x + 8.0f, y - 9.0f, 0.0f};
    if (n <= 0) {
        return;
    }
    iconDispGx(&at, ICON_DISP_2D, itemDataTable[ITEM_COIN].icon_id, 0.55f);
    snprintf(buf, sizeof(buf), "+%d", n);
    text(x + 20.0f, y, 0.6f, kInk, a, buf);
}

/* label and value as a pair in a column: the label left, the value right */
static void cell(f32 x, f32 y, f32 w, const char* label, int value, u8 a) {
    char buf[16];
    const f32 s = 0.6f;
    f32 lw = width(label, s);
    snprintf(buf, sizeof(buf), "%d", value);
    if (lw > w - 40.0f) {
        FontDrawStart_alpha(a);
        FontDrawScaleVec((Vec){s * (w - 40.0f) / lw, s, s});
        FontDrawColor(faded(kSoft, a));
        FontDrawString(x, y, label);
    } else {
        text(x, y, s, kSoft, a, label);
    }
    text(x + w - width(buf, 0.65f), y, 0.65f, kInk, a, buf);
}

static void cardDraw(void) {
    const int hasGrade = sCard.grade >= 0;
    f32 w, h, x0, y0, slide;
    u8 a;
    /* coming down, going up */
    if (sAge < ENTER) {
        const f32 t = (f32)sAge / ENTER;
        slide = (1.0f - t) * (1.0f - t) * 60.0f;
        a = (u8)(0xFF * t);
    } else if (sLeave >= 0) {
        const f32 t = (f32)sLeave / LEAVE;
        slide = t * t * 40.0f;
        a = (u8)(0xFF * (1.0f - t));
    } else {
        slide = 0.0f;
        a = 0xFF;
    }
    if (sStyle == 2) {
        const char* title = portTrGame("Battle recap");
        char turns[48];
        const f32 left = hasGrade ? 96.0f : 22.0f, colW = 168.0f, gap = 22.0f, row = 19.0f;
        const char* labels[10] = {
            portTrGame("Damage dealt"), portTrGame("Damage taken"), portTrGame("Best hit"),
            portTrGame("Enemies defeated"), portTrGame("Superguards"), portTrGame("Guards"),
            portTrGame("Stylish moves"), portTrGame("Turns"), portTrGame("FP used"), portTrGame("Items used"),
        };
        const int values[10] = {
            sCard.damageDealt, sCard.damageTaken, sCard.bestHit, sCard.enemies, sCard.superguards,
            sCard.guards, sCard.stylish, sCard.turns, sCard.fpUsed, sCard.itemsUsed,
        };
        int i;
        w = left + colW * 2.0f + gap + 22.0f;
        h = 40.0f + row * 5.0f + 6.0f;
        x0 = -w / 2.0f;
        y0 = -94.0f - slide; /* (low on the picture, under the HUD and the Star Points: over the audience) */
        windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, a}, x0, y0, w, h, 10.0f);
        text(x0 + left, y0 - 10.0f, 0.75f, kInk, a, title);
        snprintf(turns, sizeof(turns), portTrGame("%d Star Points"), sCard.starPoints);
        text(x0 + w - 22.0f - width(turns, 0.6f), y0 - 14.0f, 0.6f, kSoft, a, turns);
        for (i = 0; i < 10; i++) {
            cell(x0 + left + (i % 2) * (colW + gap), y0 - 40.0f - (i / 2) * row, colW, labels[i], values[i], a);
        }
        if (hasGrade) {
            const char* word = portTrGame("Grade");
            gradeLetter(x0 + 50.0f, y0 - 56.0f, 2.2f, a);
            text(x0 + 50.0f - width(word, 0.55f) / 2.0f, y0 - 98.0f, 0.55f, kSoft, a, word);
            coins(x0 + 28.0f, y0 - 114.0f, sCard.bonusCoins, a);
        }
    } else {
        /* one line: the grade and, short, the main numbers; the grade alone, what it was made of */
        char line[200];
        const f32 left = hasGrade ? 64.0f : 22.0f;
        f32 lw;
        if (sStyle == 1) {
            snprintf(line, sizeof(line), portTrGame("Dealt %d   Taken %d   Best hit %d   Turns %d"), sCard.damageDealt,
                     sCard.damageTaken, sCard.bestHit, sCard.turns);
            if (sCard.superguards > 0) {
                char more[64];
                snprintf(more, sizeof(more), portTrGame("   Superguards %d"), sCard.superguards);
                strncat(line, more, sizeof(line) - strlen(line) - 1);
            }
        } else {
            snprintf(line, sizeof(line), portTrGame("%d Stylish, %d Superguards, %d damage taken, %d turns"),
                     sCard.stylish, sCard.superguards, sCard.damageTaken, sCard.turns);
        }
        lw = width(line, 0.65f);
        if (lw > 470.0f) {
            lw = 470.0f;
        }
        w = left + lw + (hasGrade && sCard.bonusCoins > 0 ? 58.0f : 0.0f) + 22.0f;
        h = 44.0f;
        x0 = -w / 2.0f;
        y0 = -172.0f - slide;
        windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, a}, x0, y0, w, h, 10.0f);
        FontDrawStart_alpha(a);
        FontDrawScaleVec((Vec){0.65f * lw / width(line, 0.65f), 0.65f, 0.65f}); /* (narrower letters when long) */
        FontDrawColor(faded(kInk, a));
        FontDrawString(x0 + left, y0 - 13.0f, line);
        if (hasGrade) {
            gradeLetter(x0 + 34.0f, y0 - 22.0f, 1.3f, a);
            coins(x0 + left + lw + 12.0f, y0 - 13.0f, sCard.bonusCoins, a);
        }
    }
}

/* the game's font takes two of its random numbers a letter (fontmgr.c): kept as they were, so this
 * player's own card doesn't put an online game out of step */
static void cardDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    (void)camId;
    (void)param;
    cardDraw();
    portRandSetState(seed);
}

/* --- each frame --- */

static int recapPuzzle(void) {
    int daily, turn, maxTurns;
    return portPuzzleActive(&daily, &turn, &maxTurns) >= 0;
}

static void frameHook(void) {
    PortBattleRecap r;
    const unsigned serial = portBattleRecapLast(&r);
    const SequenceType seq = seqGetSeq();
    portStatsFrame();
    if (serial != sSeen) {
        sSeen = serial;
        sStyle = *sMode < 0 ? 0 : *sMode > 2 ? 2 : *sMode;
        if (serial != 0 && (sStyle > 0 || r.grade >= 0)) {
            sCard = r;
            sAge = 0;
            sLeave = -1;
            sLife = sStyle == 2 ? 480 : sStyle == 1 ? 270 : 300;
        }
    }
    if (sAge < 0) {
        return;
    }
    sAge++;
    if (sLeave >= 0) {
        if (++sLeave > LEAVE) {
            sAge = -1;
            sLeave = -1;
            return;
        }
    } else if (sAge >= sLife ||
               (sAge > SETTLE && ((keyGetButtonTrg(0) | keyGetButtonTrg(1)) & (PAD_BUTTON_A | PAD_BUTTON_B)))) {
        sLeave = 0;
    }
    if (gp != NULL && (seq == SEQ_GAME || seq == SEQ_BATTLE) && !winCheck()) { /* (not over the Start menu) */
        dispEntry(CAMERA_2D, 0.0f, 2, cardDisp, NULL); /* (blended, after the 2D picture: over the HUD and its icons) */
    }
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portRecapInit(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    sMode = portSettingsDeclare("battle_recap", 0, 0, 2);
    gPortStatsSkip = recapPuzzle;
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
