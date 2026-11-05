/* The title menu (port/include/port/title_menu.h, docs/PC_PORT.md): once
 * "Press Start" is pressed, a dark band along the bottom with four buttons
 * in a row, painted as the Start menu's Settings tab's front page (the
 * Journal's entries) but smaller: Play, Modes, Online and Settings, with the
 * game's cursor hand and sounds, and under them the cursor's note and the A
 * and B prompts, so the logo and the whole cast stay in view (the copyright
 * goes down out of the picture meanwhile, as at the files). The stick, the
 * D-pad, the keyboard, the mouse and touch move
 * through them; A (a click on the cursor's, a second tap) chooses, B goes
 * back to "Press Start".
 *
 * - Play: on to the file select, as A did (seq_title.c's own curtain).
 * - Settings and Online: the logo goes up and out, and the Start menu's
 *   own code runs over the title (winRootMain and winRootDisp on the game's
 *   menu work, win.tpl loaded here), with only its Online and Settings tabs
 *   (win_root.c, scope PORT_TITLE_NO_FILE): the same book pages, the rows
 *   that work on a file greyed (pause_settings_aurora.c). Closing it brings
 *   the logo and the buttons back.
 * - Modes: the curtain closes as for the files, and a temporary file starts
 *   in memory, as the $TTYD_MAP warps' (trace_pc.c): a new file's state, the
 *   story at its end, Goombella along and the whole Tattle Log lent (the
 *   Tattle-card duels' decks), save blocks off. It goes to Rogueport's
 *   Westside, where the card players sit, and the Start menu opens there on
 *   the Modes hub with only the modes that need no file of the player's
 *   (scope PORT_TITLE_TEMP_FILE). Each mode starts and runs as from the Start
 *   menu (a start closes the menu at once there); the modes go back to the
 *   room they were started from when they're over, and once back there (two
 *   seconds, for their last word), with nothing started, or at once with B on
 *   the hub, it fades out and the game's own way to the title follows (a map
 *   change to "title"), the buttons up again on Modes. Never saved:
 *   autosave and quick resume skip it, the Records don't count it, and its
 *   achievements are held as for the practice tools'.
 *
 * Lockstep: the menu is off online and in replays (Start goes straight to
 * the file select, as before), so a session's games stay in step; its own
 * draws keep the game's random numbers as they were (the font takes some a
 * letter) and read no clock but the frame count. */
#include "port/title_menu.h"
#include "port/brothers.h"
#include "port/dojo.h"
#include "port/battlex.h"
#include "port/hook_list.h"
#include "port/menu_tap.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/online.h"
#include "port/party.h"
#include "port/pause_settings.h"
#include "port/platform.h"
#include "port/practice.h"
#include "port/raid.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/speech.h"
#include "port/tr.h"
#include "port/versus.h"
#include "version.h"
#include "event.h"
#include "seq/seq_title.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/fadedrv.h"
#include "driver/icondrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "driver/windowdrv.h"
#include "evt/evt_story.h"
#include "icon_ids.h"
#include "manager/filemgr.h"
#include "manager/fontmgr.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "pmario_sound.h"
#include "system.h"
#include "win/win_main.h"
#include "data/item_data.h"
#include "wide_x.h"
#include <demo/DEMOPad.h>
#include <dolphin/gx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "win/win_root.h"
#include "driver/mapdrv.h"

extern TitleWork* wp2; /* seq_title.c */

/* --- the buttons --- */

enum { B_PLAY, B_MODES, B_ONLINE, B_SETTINGS, BUTTONS };
static const char* const kLabel[BUTTONS] = {"Play", "Modes", "Online", "Settings"};
static const char* const kNote[BUTTONS] = {
    "Your save files: go on with one, or start a new adventure.",
    "Party games and battles, on a file of their own that is never saved.",
    "Play together over the internet: host a room, join one by its code, or watch.",
    "Paper Cut's settings, in the game's own book.",
};

/* their pictures: the game's icons (icon_ids.h), one or two */
static int buttonIcon(int i, int which) {
    switch (i) {
        case B_PLAY:
            return which == 0 ? ICON_134 : -1; /* a written page: the files */
        case B_MODES:
            return which == 0 ? itemDataTable[ITEM_CHAMPS_BELT].icon_id : -1; /* the Modes page's own */
        case B_ONLINE:
            return which == 0 ? ICON_19E : ICON_155; /* Mario and Goombella, as Multiplayer's */
        default:
            return which == 0 ? ICON_107 : -1; /* a cog, as General's */
    }
}

/* where they are (the 2D camera, y up): one row of four in the dark band along the bottom, so the logo
 * and the whole cast stay in view: the Settings tab's front page's buttons (the Journal's entries, 200 x
 * 32) smaller, on the band's top line, each with room on its left for the hand: at 4:3 (608 wide) 0.6
 * their size, 144 apart; wider pictures a little bigger, at most 0.75 and 180 apart. The row is a little
 * right, so that with the first one's hand it sits in the middle. The cursor's note and the A and B
 * prompts are on a slim line under them. The copyright goes down out of the picture meanwhile, as at
 * the files. */
#define BTN_Y (-180.0f)
#define BAND_Y (-160.0f) /* the band's top */
#define BAND_H 80.0f
#define LINE_DY 43.0f    /* the note's line: its top, under the band's */

static f32 buttonStep(void) {
    const f32 step = (WIDE_X(608.0f) - 32.0f) / 4.0f;
    return step > 180.0f ? 180.0f : step;
}

static f32 buttonScale(void) {
    return buttonStep() / 240.0f; /* (200 wide and a sixth of the step for the hand) */
}

#define BTN_W (200.0f * buttonScale())
#define BTN_H (32.0f * buttonScale())

static f32 buttonX(int i) {
    return ((f32)i - 1.5f) * buttonStep() + buttonStep() / 9.0f;
}

static f32 buttonY(int i) {
    (void)i;
    return BTN_Y;
}

/* --- state --- */

enum {
    S_OFF,       /* "Press Start" (the game's own) */
    S_MENU,      /* the buttons */
    S_TO_PAGES,  /* the logo going up and out, the buttons down */
    S_PAGES,     /* the Start menu's tabs over the title */
    S_FROM_PAGES,/* the logo coming back */
    S_TO_MODES,  /* the curtain closing on the title */
    S_CLOSING,   /* B: back to "Press Start" */
};
static int sState;
static int sCur;
static int sPage;       /* B_ONLINE or B_SETTINGS: the tab the pages opened on */
static int sReopen;     /* back from the temporary file: the buttons at once, on Modes */
static f32 sShow;       /* the buttons: 0 hidden .. 1 in place */
static f32 sLogo;       /* the logo: 0 home .. 1 out of the picture */
static f32 sLogoTop, sLogoBottom; /* its home */
static int sTimer;
static u32 sFrame;      /* (the hand's bob and the buttons' drop: frames, not the clock) */
static FileEntry* sRes; /* win.tpl: the hand, and the Start menu's pages */
static int* sOn;

/* the temporary file (Modes) */
enum { T_NONE, T_ENTER, T_HUB, T_MENU, T_AFTER, T_RUN, T_LEAVE, T_GONE };
static int sTemp;
static int sBack; /* B on the Modes hub: back to the title as soon as the menu is closed */
#define HOLD_MAP "gor_02" /* Rogueport's Westside: the card players' table (cards_aurora.c) */

#if VERSION >= VERSION_US
static const char* const kLang[] = {"jp", "us", "ge", "fr", "sp", "it"};
#else
static const char* const kLang[] = {"jp", "us"};
#endif

static int enabled(void) {
    const char* e = getenv("TTYD_TITLE_MENU");
    if (portOnlineActive() || portReplayActive()) {
        return 0; /* (a session's games, a replay: the game's own Start, in step) */
    }
    if (e != NULL && e[0] != '\0') {
        return atoi(e) != 0; /* (tests: forced on or off) */
    }
    if (getenv("TTYD_INPUT") != NULL) {
        return 0; /* (tests' scripts press Start for the file select) */
    }
    return sOn == NULL || *sOn != 0;
}

int portTitleScope(void) {
    if (sTemp != T_NONE) {
        return PORT_TITLE_TEMP_FILE;
    }
    return sState == S_PAGES ? PORT_TITLE_NO_FILE : PORT_TITLE_NONE;
}

int portTitleTempFile(void) {
    return sTemp != T_NONE;
}

/* --- win.tpl --- */

static int resReady(void) {
    const int lang = gp->language >= 0 && gp->language < (s32)(sizeof(kLang) / sizeof(kLang[0])) ? gp->language : 1;
    if (sRes == NULL && fileAsyncf(4, 0, WIN_DIR "%s/win.tpl", getMarioStDvdRoot(), kLang[lang]) != NULL) {
        sRes = fileAllocf(4, WIN_DIR "%s/win.tpl", getMarioStDvdRoot(), kLang[lang]);
    }
    return sRes != NULL;
}

static void resFree(void) {
    if (winGetPtr()->res == sRes) {
        winGetPtr()->res = NULL; /* (lent to the menu's work while drawn) */
    }
    if (sRes != NULL) {
        fileFree(sRes);
        sRes = NULL;
    }
}

/* --- input --- */

static u32 sTrg, sDir;

static void readPad(void) {
    const u32 rep = keyGetButtonRep(0);
    sTrg = keyGetButtonTrg(0);
    sDir = keyGetDirRep(0);
    sDir |= (rep & PAD_BUTTON_LEFT) ? DEMO_STICK_LEFT : 0;
    sDir |= (rep & PAD_BUTTON_RIGHT) ? DEMO_STICK_RIGHT : 0;
    sDir |= (rep & PAD_BUTTON_UP) ? DEMO_STICK_UP : 0;
    sDir |= (rep & PAD_BUTTON_DOWN) ? DEMO_STICK_DOWN : 0;
}

/* the mouse or a tap: the button under the pointer gets the cursor, a click on the cursor's chooses */
static int pointer(void) {
    f32 gx, gy;
    const int kind = portMenuPoint(&gx, &gy);
    int i;
    if (kind == 0) {
        return 0;
    }
    for (i = 0; i < BUTTONS; i++) { /* (each one's half of the row, the hand's gap with it: small buttons) */
        if (fabsf(gx - buttonX(i)) <= buttonStep() / 2.0f &&fabsf(gy - buttonY(i)) <= BTN_H / 2.0f + 8.0f) {
            if (i != sCur) {
                sCur = i;
                psndSFXOn((const char*)0x20005);
                return 0; /* (the first click or tap points; the next chooses, as the Start menu's) */
            }
            return kind == 1;
        }
    }
    return 0;
}

static void said(void) {
    char text[300];
    snprintf(text, sizeof(text), "%s. %s", portTr(kLabel[sCur]), portTr(kNote[sCur]));
    portSpeechMenu(text);
}

/* --- drawing --- */

static f32 ease(f32 t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

/* text from x (its middle with mid), no wider than maxW (narrower letters), white on the game's edge */
static f32 edged(f32 x, f32 y, f32 scale, u8 a, const char* text, f32 maxW, int mid) {
    f32 sx = scale;
    f32 w = (f32)FontGetMessageWidth(text) * scale;
    if (w > maxW && w > 0.0f) {
        sx = scale * maxW / w;
        w = maxW;
    }
    FontDrawStart();
    FontDrawEdge();
    FontDrawColor((GXColor){0xFF, 0xFF, 0xFF, a});
    FontDrawScaleVec((Vec){sx, scale, 1.0f});
    FontDrawString(mid ? x - w / 2.0f : x, y, text);
    FontDrawEdgeOff();
    FontDrawScaleVec((Vec){1.0f, 1.0f, 1.0f});
    return w;
}

/* a Shift JIS letter's first byte (the JP release's text): two bytes */
static int sjisLead(unsigned char c) {
    return strcmp(PORT_GAME_ID, "G8MJ01") == 0 && ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC));
}

#define NOTE_S 0.6f  /* one line */
#define NOTE_S2 0.5f /* two */

/* the cursor's note on the band's line (its top y), from x, at most maxW wide: one line (a little narrower
 * if need be), or (a long one, as German's often are, at 4:3) two smaller lines split nearest its middle
 * at a space, or for Japanese after its 、, 。 or ：, each narrowed only if still too wide */
static void noteDisp(f32 x, f32 y, u8 a, const char* text, f32 maxW) {
    char first[300];
    const char* best = NULL; /* the second line's start */
    const char* p;
    const long half = (long)strlen(text) / 2;
    if ((f32)FontGetMessageWidth(text) * NOTE_S * 0.85f <= maxW || strlen(text) >= sizeof(first)) {
        edged(x, y, NOTE_S, a, text, maxW, 0); /* (narrowed 15% at most) */
        return;
    }
    for (p = text; *p != '\0'; p++) {
        const char* next = NULL;
        if (sjisLead((unsigned char)p[0]) && p[1] != '\0') {
            const unsigned char t = (unsigned char)p[1];
            if ((unsigned char)p[0] == 0x81 && (t == 0x40 || t == 0x41 || t == 0x42 || t == 0x46)) {
                next = p + 2; /* (the full-width space, 、, 。 and ：) */
            }
            p++;
        } else if (*p == ' ') {
            next = p + 1;
        }
        if (next != NULL && *next != '\0' &&
            (best == NULL || labs((long)(next - text) - half) < labs((long)(best - text) - half))) {
            best = next;
        }
    }
    if (best == NULL) { /* (nowhere to split: one line, narrowed) */
        edged(x, y, NOTE_S, a, text, maxW, 0);
        return;
    }
    memcpy(first, text, (size_t)(best - text));
    first[best - text] = '\0';
    if (best[-1] == ' ') {
        first[best - text - 1] = '\0';
    }
    edged(x, y + 6.0f, NOTE_S2, a, first, maxW, 0);
    edged(x, y - 8.0f, NOTE_S2, a, best, maxW, 0);
}

static void menuDisp(CameraId camId, void* param) {
    const u32 rand = portRandState(); /* (the font takes the game's random numbers: put back after) */
    WindowWork* w = winGetPtr();
    int i, k;
    (void)camId;
    (void)param;
    if (sRes == NULL) {
        return; /* (win.tpl: a frame or two) */
    }
    w->res = sRes;
    if (sShow > 0.0f) {
        /* the band at the bottom (under the buttons): the cursor's note on the left of its line, A and B on
           the right */
        const f32 e = ease(sShow);
        const u8 a = (u8)(255.0f * e);
        const f32 y = BAND_Y - (1.0f - e) * BAND_H;
        const f32 line = y - LINE_DY;
        const char* choose = portTrGame("Choose");
        const char* back = portTrGame("Back");
        const f32 s = 0.6f;
        const f32 wb = (f32)FontGetMessageWidth(back) * s;
        const f32 wc = (f32)FontGetMessageWidth(choose) * s;
        f32 x = WIDE_X(290.0f) - wb; /* (the band's ends: the picture's, wide or not) */
        portPauseBox(-WIDE_X(320.0f), y, WIDE_X(640.0f), BAND_H + 40.0f, (GXColor){0x10, 0x08, 0x20, (u8)(0x90 * e)});
        portPauseBox(-WIDE_X(320.0f), y, WIDE_X(640.0f), 2.0f, (GXColor){0xF6, 0xEA, 0xF4, (u8)(0x60 * e)});
        edged(x, line, s, a, back, 120.0f, 0);
        iconDispGx(&(Vec){x - 13.0f, line - 8.0f, 0.0f}, ICON_DISP_2D, ICON_066, 0.55f);
        x -= 30.0f + wc;
        edged(x, line, s, a, choose, 120.0f, 0);
        iconDispGx(&(Vec){x - 13.0f, line - 8.0f, 0.0f}, ICON_DISP_2D, ICON_064, 0.55f);
        noteDisp(-WIDE_X(290.0f), line, a, portTrGame(kNote[sCur]), x - 30.0f + WIDE_X(290.0f));
    }
    for (k = 0; k <= BUTTONS; k++) {
        /* each drops in a little after the one before, as the Start menu's tabs; the cursor's last, over
           the others */
        const int sel = k == BUTTONS;
        f32 e;
        i = sel ? sCur : k;
        if (!sel && i == sCur) {
            continue;
        }
        e = ease(sShow * 1.6f - (f32)i * 0.2f);
        if (e <= 0.0f) {
            continue;
        }
        portPauseTitleButton(w, buttonX(i), buttonY(i) - (1.0f - e) * 48.0f, buttonScale(), portTrGame(kLabel[i]),
                             buttonIcon(i, 0), buttonIcon(i, 1), sel && sState == S_MENU, (int)(255.0f * e));
    }
    if (sState == S_MENU && sShow >= 1.0f) {
        /* the game's hand at the cursor's left, bobbing as the Start menu's: where the full-size buttons
           had it, made as much smaller (0.8 at 4:3) */
        const f32 s = buttonScale(), hs = s / 0.75f;
        const f32 bob = fabsf(2.0f * hs * (f32)sin((f32)sFrame / 8.0f));
        const f32 hx = buttonX(sCur) - BTN_W / 2.0f - 8.0f * s - 18.0f * hs + bob;
        const f32 hy = buttonY(sCur) + 8.0f * s;
        winTexInit((TEXPalette*)*sRes->data);
        winTexSet(0, (Vec){2.0f * hs + hx, hy - 2.0f * hs, 0.0f}, (Vec){hs, hs, 1.0f}, (GXColor){0x00, 0x00, 0x00, 0x80});
        winTexSet(0, (Vec){hx, hy, 0.0f}, (Vec){hs, hs, 1.0f}, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    }
    portRandSetState(rand);
}

static void pagesDisp(CameraId camId, void* param) {
    (void)param;
    winRootDisp(camId, winGetPtr()); /* (it keeps the random numbers itself in the title's scope) */
}


/* --- the Start menu's pages over the title --- */

static void pagesOpen(void) {
    WindowWork* w = winGetPtr();
    winReInit();
    w->res = sRes;
    w->menuSel = sPage == B_ONLINE ? PORT_PAUSE_ONLINE_TAB : PORT_PAUSE_SETTINGS_TAB;
    w->active = 0;
    w->menuState = 2; /* (winMain's "open": nothing reads it on the title but winCheck) */
    w->item.mailX = 1000.0f; /* (the mail window: out of the picture, as the Items tab's open puts it) */
    w->item.mailY = -1000.0f;
}

static int pagesMain(void) {
    WindowWork* w = winGetPtr();
    s32 r;
    w->buttonTrg = sTrg;
    w->buttonRep = keyGetButtonRep(0);
    w->dirTrg = keyGetDirTrg(0);
    w->dirRep = sDir;
    r = winRootMain(w);
    if (r != 0) {
        w->res = NULL; /* (ours: freed with the title menu) */
        winReInit();
        N_mapDispOn();
        camDispOn(CAMERA_3D);
        return 1;
    }
    dispEntry(CAMERA_2D, 250.0f, 0, pagesDisp, NULL);
    return 0;
}

/* --- the temporary file --- */

static void tempStart(void) {
    int unit;
    stg0_00_init(); /* a new file's state, as the file select's new game */
    marioPartyKill();
    portBrothersReset();
    swInit();
    swByteSet(GSW_STORY_PROGRESS, STORY_STG9_END_ROGUEPORT); /* (the story over: no scenes in Rogueport) */
    partyJoin(1);
    marioPartyEntry(1);
    for (unit = 1; unit < PORT_PARTY_UNITS; unit++) { /* the Tattle Log lent: the card duels' decks */
        if (portPartyEnemyListed(unit)) {
            swSet(unit + GSWF_TATTLE_BASE);
        }
    }
    gp->flags |= GLOBAL_FLAG_SAVE_BLOCKS_OFF; /* (no save blocks: the title clears it again) */
    portPracticeTaint();                      /* (no achievements for the file: none to keep them) */
    gp->areaFadeInType = FADE_NONE;
    gp->areaFadeOutType = FADE_CURTAIN_SIDES_A2;
    sTemp = T_ENTER;
    printf("[title] temporary file: to %s\n", HOLD_MAP);
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, HOLD_MAP, "w_bero"); /* (in from the plaza, as a walk there comes) */
}

/* a mode asked for, running or ending */
static int modeBusy(void) {
    int daily, turn, maxTurns;
    const PortModeStatus* m = portModeStatus();
    return portQuizActive() || portCardsActive() || portVersusBusy() || portRaidActive() || portDojoRunning() ||
           portPuzzleActive(&daily, &turn, &maxTurns) >= 0 || (m->mode != PORT_MODE_NONE && !m->finished);
}

int portTitleModesPoll(void) {
    if (sTemp == T_HUB && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, HOLD_MAP) == 0) {
        sTemp = T_MENU;
        printf("[title] the Modes hub\n");
        fflush(stdout);
        return 1;
    }
    return 0;
}

void portTitleModesBack(void) {
    sBack = sTemp != T_NONE;
}

static void tempEnd(void) {
    sTemp = T_NONE;
    sBack = 0;
    sReopen = 1;
    sCur = B_MODES;
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* after each game frame: the temporary file's way back to the title */
static void frameHook(void) {
    const SequenceType seq = seqGetSeq();
    const int home = gp != NULL && seq == SEQ_GAME && strcmp(gp->currentMapName, HOLD_MAP) == 0;
    sFrame++;
    if (sState == S_OFF && sShow > 0.0f) { /* Play: the buttons fade out as the title's curtain closes */
        sShow -= 0.1f;
        if (sShow > 0.0f && seq == SEQ_TITLE) {
            dispEntry(CAMERA_2D, 300.0f, 0, menuDisp, NULL);
        } else {
            sShow = 0.0f;
            resFree();
        }
    }
    if (sTemp == T_NONE) {
        return;
    }
    if ((seq == SEQ_TITLE && sTemp != T_ENTER) || seq == SEQ_LOGO || seq == SEQ_LOAD) { /* (T_ENTER: the title still, its map change asked for) */
        tempEnd(); /* (a Game Over's way back, or ours) */
        return;
    }
    switch (sTemp) {
        case T_ENTER:
            if (home) {
                sTemp = T_HUB; /* (winMain opens the Start menu on it, portTitleModesPoll) */
            }
            break;
        case T_MENU:
            if (!winCheck()) {
                sTemp = T_AFTER;
                sTimer = 0;
            }
            break;
        case T_AFTER: /* the menu closed: a mode started, or nothing (B on the hub) */
            if (winCheck()) {
                sTemp = T_MENU;
            } else if (sBack) {
                sBack = 0;
                sTemp = T_LEAVE;
                sTimer = 0;
            } else if (modeBusy() || !home) {
                sTemp = T_RUN;
                sTimer = 0;
            } else if (++sTimer > 90) { /* (nothing started after all) */
                sTemp = T_LEAVE;
                sTimer = 0;
            }
            break;
        case T_RUN: /* the mode over and back in the field: a moment, then the title */
            /* (most modes go back to the room they started from; the boss rush won ends in Rogueport's
               square, a rush stopped from the menu leaves Mario at the boss's: four seconds there) */
            if (seq == SEQ_GAME && !modeBusy() && !winCheck() && fadeIsFinish() && !gp->inBattle) {
                if (++sTimer > (home ? 120 : 240)) { /* (two seconds: its last word read) */
                    sTemp = T_LEAVE;
                    sTimer = 0;
                }
            } else {
                sTimer = 0;
            }
            break;
        case T_LEAVE:
            if (sTimer++ == 0) {
                fadeEntry(FADE_OUT_BLACK, 600, (GXColor){0x00, 0x00, 0x00, 0xFF});
                psndBGMOff_f_d(PSND_FADE_OUT, 600, 0);
                psndBGMOff_f_d(PSND_FADE_OUT | PSND_CH(1), 600, 0);
            } else if (fadeIsFinish() && sTimer > 4) {
                printf("[title] temporary file over: back to the title\n");
                fflush(stdout);
                psndSFXAllOff();
                sTemp = T_GONE;
                seqSetSeq(SEQ_MAP_CHANGE, "title", NULL); /* (the game's own way: the area unloaded, then the title) */
            }
            break;
    }
}

/* --- the title's frame (state 3) --- */

static void open(int sound) {
    sState = S_MENU;
    sShow = 0.0f;
    sLogo = 0.0f;
    sLogoTop = wp2->topPosY;
    sLogoBottom = wp2->bottomPosY;
    if (sound) {
        psndSFXOn((const char*)8); /* (the game's own Press Start) */
    }
    said();
}

/* the logo up and out for the pages (sLogo), the copyright down and out while the buttons are up */
static void place(void) {
    const f32 down = sShow > sLogo ? sShow : sLogo;
    wp2->topPosY = sLogoTop + 330.0f * ease(sLogo);
    wp2->bottomPosY = sLogoBottom - 200.0f * ease(down);
}

int portTitleMenuMain(void) {
    readPad();
    if (sState == S_OFF) {
        if (!enabled()) {
            sReopen = 0;
            return 0;
        }
        if (sReopen) {
            sReopen = 0;
            open(0);
        } else if (sTrg & (PAD_BUTTON_A | PAD_BUTTON_START)) {
            sCur = B_PLAY;
            open(1);
            sTrg = 0;
        } else {
            return 0;
        }
    }
    resReady();
    /* the "Press Start" prompt out while the buttons are up */
    wp2->fadeAlpha -= 0.15f;
    if (wp2->fadeAlpha < 0.0f) {
        wp2->fadeAlpha = 0.0f;
    }
    wp2->idle = 0;
    switch (sState) {
        case S_MENU: {
            const int was = sCur;
            sShow += 0.08f;
            sShow = sShow > 1.0f ? 1.0f : sShow;
            if (sLogo > 0.0f) { /* (back from the pages) */
                sLogo -= 0.08f;
                sLogo = sLogo < 0.0f ? 0.0f : sLogo;
            }
            if (sShow < 0.5f) {
                break;
            }
            if (pointer() || (sTrg & (PAD_BUTTON_A | PAD_BUTTON_START))) {
                psndSFXOn((const char*)0x20012);
                switch (sCur) {
                    case B_PLAY:
                        sState = S_OFF; /* (the buttons fade as the curtain closes: frameHook) */
                        return 2; /* (seq_title.c: the curtain, then the file select) */
                    case B_MODES:
                        sState = S_TO_MODES;
                        sTimer = 0;
                        break;
                    default:
                        sPage = sCur;
                        sState = S_TO_PAGES;
                        break;
                }
            } else if (sTrg & PAD_BUTTON_B) {
                psndSFXOn((const char*)0x20013);
                sState = S_CLOSING;
            } else if (sDir & DEMO_STICK_LEFT) {
                sCur = (sCur + BUTTONS - 1) % BUTTONS; /* (along the row, round at its ends) */
            } else if (sDir & DEMO_STICK_RIGHT) {
                sCur = (sCur + 1) % BUTTONS;
            }
            if (sCur != was) {
                if (sDir != 0) {
                    psndSFXOn((const char*)0x20005);
                }
                said();
            }
            break;
        }
        case S_CLOSING: /* B: the buttons go, the copyright comes back, "Press Start" again */
            sShow -= 0.12f;
            if (sShow <= 0.0f) {
                sShow = 0.0f;
                place();
                sState = S_OFF;
                resFree();
                return 1;
            }
            break;
        case S_TO_PAGES:
            sShow -= 0.12f;
            sLogo += 0.08f;
            sLogo = sLogo > 1.0f ? 1.0f : sLogo;
            if (sLogo >= 1.0f && sShow <= 0.0f && resReady()) {
                sShow = 0.0f;
                sState = S_PAGES;
                pagesOpen();
            }
            break;
        case S_PAGES:
            if (pagesMain()) {
                sState = S_MENU; /* (the logo and the buttons back in) */
                sShow = 0.0f;
                said();
            }
            return 1;
        case S_TO_MODES: /* the title's own curtain, as for the files (seq_title.c states 10-12) */
            sShow -= 0.12f;
            if (sTimer == 0) {
                fadeEntry(FADE_CURTAIN_SIDES_A1_TOP, 0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
                sTimer = 1;
            } else if (sTimer == 1 && fadeIsFinish()) {
                fadeEntry(FADE_CURTAIN_SIDES_Z1_3D, 0, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
                sTimer = 2;
            } else if (sTimer == 2 && fadeIsFinish()) {
                int i;
                sState = S_OFF;
                resFree();
                for (i = 0; i < 5; i++) {
                    fadeReset(i);
                }
                fadeEntry(FADE_CURTAIN_SIDES_A1, 0, (GXColor){0x00, 0x00, 0x00, 0xFF});
                tempStart();
                return 1;
            }
            break;
    }
    sShow = sShow < 0.0f ? 0.0f : sShow;
    place();
    if (sShow > 0.0f) {
        dispEntry(CAMERA_2D, 300.0f, 0, menuDisp, NULL);
    }
    return 1;
}

void portTitleMenuInit(void) {
    sOn = portSettingsFind("title_menu");
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
