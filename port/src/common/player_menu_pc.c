/* Players 2-4's own Start menu (port/include/port/player_menu.h, docs/PLAYERS.md).
 *
 * The pad read (portPlayerMenuPad, before pings and online's exchange) is where it lives: a player's
 * Start opens their menu, and while it is open their controller is the menu's (the game gets that
 * controller with nothing pressed, so their character stands still, and nobody else's input is
 * touched). Buttons held as it closes stay off until let go (a B that closed it never talks to an
 * NPC). Players 2-4's Start is never the game's while the field is up, so it can't open player 1's
 * menu. What the menu does goes into the player's own input, as their controller would: a partner
 * picked on the Character strip as a mark in analogA (second_player_pc.c's fieldRoute takes it, online
 * the host's game), an emote the same way, taken off again after the exchange (portPlayerMenuTakePad)
 * and shown over that player's character. So a replay plays it back and the host's game sees a guest's.
 *
 * In the field, Z held (players 2-4) puts up a small wheel of the emotes at the player's place: the stick
 * points at one, letting go of Z shows it (the stick at rest: none). Their character stands still while
 * Z is held. Player 1's wheel waits for a hold of Z that can't be Mario's (p1Quick).
 *
 * Notes at a player's place: "2P joined" with Start for their menu (about 3 s); "1P has the menu" when
 * their Start comes while player 1's Start menu is up; online, a guest's "Paused by <host>" for as long as
 * the host's Start menu is up (the guest's picture doesn't have it). An unseated controller's Start takes
 * the next free seat (joinRule, second_player_pc.c's portCoopJoinStart).
 *
 * Online, a co-op guest's game (portStateSyncCoopGuest) is player 2-4's: its own controller (port 0)
 * opens this menu in place of the game's full Start menu, with two buttons more (Your screen, Leave);
 * a seat swap another player asks of it comes up as a small card at its side (A yes, B no).
 *
 * Drawn in the 2D layer the way the game's Start menu is drawn: a card per player along the bottom of the
 * picture in seat order (2P at the left, 3P in the middle, 4P at the right; online every screen draws
 * every player's, each game sending its own player's card with its player message), the menu's
 * own paper (win.tpl's pattern, read from the disc once) in that player's colour, the game's window
 * frames, icons, hand and bubbles (eff_fukidashi.c's, drawn flat), with the menu's sounds. Nothing in
 * it is the game's state: the game's random numbers are put back around the drawing. */
#include "port/achievements.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "eff/eff_fukidashi.h"
#include "manager/fontmgr.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "pmario_sound.h"
#include "port/brothers.h"
#include "port/gameplus.h"
#include "port/companion.h"
#include "port/menu_tap.h"
#include "port/platform.h"
#include "port/player_menu.h"
#include "port/players.h"
#include "port/pm64.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/text_fit.h"
#include "port/together.h"
#include "port/tr.h"
#include "port/ui_boxes.h"
#include "win/win_main.h"
#include "wide_x.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TTYD_BACKEND_AURORA
#include "driver/effdrv.h"
#include "driver/icondrv.h"
#include "driver/windowdrv.h"
#include "icon_ids.h"
#include "port/button_markup.h"
#include "port/luigi.h"
#include "port/party.h"
#include "port/tpl_pc.h"
#include "sdk/texPalette.h"
#include "system.h"
#include "win/win_main.h"
#include <dolphin/dvd.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#endif
#ifdef PORT_TOUCH_OVERLAY
#include "port/touch_overlay.h"
#endif

void portCoopNotice(const char* english); /* second_player_pc.c */
extern BinaryFontHeader* pfh;             /* fontmgr.c: the font, NULL until the logos load it */

#define PORTS 4
#define EMOTES 6
#define EMOTE_MARK 0xE0  /* analogA's top five bits 11100: an emote (1-6) in the low three */
#define PICK_MARK 0xE8   /* 11101: a partner picked on the Character strip (1-7) */
#define PM64_PICK_MARK 0xF0 /* 11110: a Paper Mario 64 partner picked there (1-8, port/pm64.h), less one */
#define STRIP_MAX 16     /* the strip's entries: the seven partners, and the PM64 partners after their hosts */
#define EMOTE_GAP 40     /* frames between a player's emotes */
#define SCENE_FRAMES 45  /* frames without Mario's controls before a menu closes (a scene, a talk) */
#define REPEAT_FIRST 18
#define REPEAT_NEXT 5
#define PICK_REST 12     /* frames the strip's cursor rests on a partner before they come */
#define QUICK_SHOWN 6    /* frames Z is held before the quick wheel shows */
#define P1_QUICK_HOLD 12 /* player 1: frames Z is held alone, standing still, before their quick wheel shows */
#define JOIN_FRAMES 180  /* a join note's frames (about 3 s) */
#define NOTE_FRAMES 90   /* "1P has the menu" */
#define MIRRORS 4        /* online: the other games' players' cards, by seat (sMenu[PORTS + seat - 1]) */
#define SLOTS (PORTS + MIRRORS)
#define SLIDE_FRAMES 10

#define SFX_MOVE ((const char*)0x20005)
#define SFX_OK ((const char*)0x20012)
#define SFX_BACK ((const char*)0x20013)
#define SFX_OPEN ((const char*)0x2002A)
#define SFX_CLOSE ((const char*)0x2002B)

enum { PAGE_HOME, PAGE_CHARACTER, PAGE_SEATS, PAGE_EMOTES, PAGE_CONTROLS, PAGE_SCREEN };
/* the home card's buttons */
enum { B_CHARACTER, B_SEATS, B_EMOTES, B_CONTROLS, B_SCREEN, B_LEAVE, B_COUNT };
static int homeCols(void); /* (4 a row; 3 on a card as narrow as a 4:3 picture's bars) */
#define HOME_COLS homeCols()

typedef struct {
    int open, page, sel;
    int homeSel;        /* the home card's button (kept while a page is open) */
    int confirm;        /* Leave: A once more */
    u16 held;           /* the controller's buttons (and the stick as the D-pad) last frame */
    u16 swallow;        /* held as the menu closed: the game's again once let go */
    int repeatT;        /* frames a direction has been held */
    const char* answer; /* an action's answer (English), in the label line until the cursor moves */
    int pick;           /* a strip entry (a partner 1-7, or PORT_STRIP_PM64) into the player's input */
    int pickT;          /* frames the strip's cursor has rested */
    int pickSent;       /* the strip entry last sent (not again) */
    int emote;          /* an emote (1-6) into the player's input */
    unsigned lastEmote;
    int still;          /* frames without the calm the menu needs */
    char map[16];       /* the room it opened in */
    int quick;          /* frames Z has been held in the field (the quick wheel) */
    int quickSel;       /* the quick wheel's emote (0-5), -1 none */
    int ask;            /* a seat swap asked of this player is up (its card) */
    int askFrom;        /* the asker's seat, 0 unknown */
    /* online: another game's player's card, as its game sends it (portPlayerMenuCard) */
    int mirrored;       /* the seat's player is in another game */
    int online;         /* their card has the online buttons */
    int emoteSeen;      /* their last emote's count seen (-1: none yet) */
    /* drawing */
    int t, pageT;       /* frames since it opened, since the page changed */
    float slide;        /* 0 away .. 1 in place */
    float height;       /* the card's height, easing to the page's */
    int drawnPage;      /* the page drawn (kept while it slides away) */
} Menu;

static Menu sMenu[SLOTS];
static int sPauseT; /* frames player 1's Start menu has had its pages up */
static int sHostPauseT; /* a co-op guest's game: frames the host's Start menu has been up */
static int sJoinT[PORTS];   /* frames left of a port's join note ("2P joined: {START} for your menu") */
static int sJoinWait[PORTS]; /* a join to tell at the next calm field moment */
static int sNoteT[PORTS];   /* frames left of "1P has the menu" (their Start while player 1's is up) */
static int sGuestWas;       /* a co-op guest's game: its player last frame */
static int sEmoteOut;       /* this game's player's emotes, counted, as its card sends them (count << 3 | emote) */
/* player 1's quick wheel (Z held alone): frames held so far, the hold can't open it, held as it closed */
static int sP1Hold, sP1Spoiled, sP1StickOff;
static u16 sP1Swallow;
static int sTakenPick[PORTS];
static unsigned sTakenPickAge[PORTS]; /* (pad reads since it came: kept until fieldRoute can take it) */
#define PICK_KEEP_READS 600
static unsigned sFrame;

/* the emotes: the game's own bubbles (eff_fukidashi.c's types), round the wheel from the top */
static const char* const kEmoteNames[EMOTES] = {"Surprised", "Puzzled", "Thinking", "Dizzy", "Grumpy", "Happy"};
static const int kEmoteType[EMOTES] = {0, 1, 4, 2, 5, 6};
static const int kEmoteNumber[EMOTES] = {0, 0, 3, 0, 0, 0};

/* Your screen (online: this player's own picture only; port/include/port/online_local.h's keys) */
typedef struct {
    const char* label;
    const char* key;
    int min, max, step; /* step 1 on 0..1: a switch */
} Option;
static const Option kOptions[] = {
    {"HUD size", "hud_scale", 50, 150, 10},
    {"Message size", "text_scale", 100, 150, 10},
    {"Less screen shake", "less_shake", 0, 1, 1},
    {"Less flashing", "less_flashing", 0, 1, 1},
};
#define OPTIONS ((int)(sizeof(kOptions) / sizeof(kOptions[0])))

/* Your controls: what each button does for players 2-4 in the field */
typedef struct {
    const char* token; /* button_markup.h's */
    const char* action;
    int who;           /* 0 everyone, 1 the partner's player only (X), 2 not Luigi's (L) */
} Control;
static const Control kControls[] = {
    {"{STICK}", "Walk around", 0}, {"{A}", "Jump", 0},      {"{B}", "Talk", 0},     {"{X}", "Ability", 1},
    {"{L}", "Partner", 2},  {"{Z}", "Emote", 0},     {"{PING}", "Ping", 0},  {"{START}", "Menu", 0},
};
#define CONTROLS ((int)(sizeof(kControls) / sizeof(kControls[0])))

/* --- whose menus --- */

/* this game is a co-op guest's (player 2-4 of the host's world) */
static int guestGame(void) {
    return portStateSyncCoopGuest() && portStateSyncMe() >= 2;
}

/* the player (2-4) a controller port's menu is for, 0 none: on one computer ports 1-3 (players 2-4,
   when they play); a co-op guest's game its own controller (port 0), its player's */
static int playerOf(int port) {
    if (port >= PORTS) {
        return 0; /* (another game's player's card: only drawn here) */
    }
    if (guestGame()) {
        return port == 0 ? portStateSyncMe() : 0;
    }
    if (port >= 1 && portStateSyncCoopPadHere(port + 1)) {
        return 0; /* (the host's: an online guest's seat; its card is the guest's game's, mirrored) */
    }
    return port >= 1 && port < PORTS && portCoopPlayerHere(port) ? port + 1 : 0;
}

/* the menu is this computer's own player's online (Your screen, Leave); tests: $TTYD_PLAYER_MENU_ONLINE
   gives player 2's on one computer those buttons (its look; Leave leaves nothing) */
static int onlineTest(void) {
    static int sTest = -1;
    if (sTest < 0) {
        sTest = getenv("TTYD_PLAYER_MENU_ONLINE") != NULL;
    }
    return sTest;
}

static int ownOnline(int port) {
    if (port >= PORTS) {
        return sMenu[port].online;
    }
    return (guestGame() && port == 0) || (onlineTest() && port == 1 && !guestGame());
}

/* player 1 (controller port 0) on this computer, the online host's or one screen's: an ask for Mario's seat
   (a guest's online, another player's here: portTogetherSeatRequest) shows on its card's place */
#ifdef TTYD_BACKEND_AURORA
static int hostAsked(int port) {
    return port == 0 && !guestGame();
}
#endif

/* player 1's own (the quick wheel): on one computer and the host's, controller port 0 */
static int playerOne(int port) {
    return port == 0 && !guestGame();
}

/* the seat the port's player sits in (1-4), 0 none known */
static int seatOf(int port) {
    int s;
    if (port >= PORTS) {
        return port - PORTS + 1;
    }
    if (!guestGame()) {
        return port + 1;
    }
    for (s = 2; s <= 4; s++) {
        if (portStateSyncSeatPlayer(s) == portStateSyncMe()) {
            return s;
        }
    }
    return 0;
}

/* the Start menu's state (win_main.c: 1-99 its pages up, 100-199 a partner switch after it or the quick
   swap's), 0 closed */
static int pauseState(void) {
    const WindowWork* w = winGetPtr();
    return w != NULL ? (int)w->menuState : 0;
}

/* the field is up for a menu: a file in play, no battle, no party game, player 1's Start menu not open
   (a moment the game holds the field, as a partner switch does, is only not calm) */
static int marioGuest(void);

static int fieldUp(void) {
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle || portStateSyncSharedBattle()) {
        return 0;
    }
    if ((!guestGame() || marioGuest()) && sPauseT > 2) {
        return 0; /* (player 1's Start menu, up a few frames: the quick swap passes through it in one) */
    }
#ifdef TTYD_BACKEND_AURORA
    if (portQuizPad() || portCardsPad()) {
        return 0;
    }
#endif
    return 1;
}

/* no scene, no talk: Mario has the controls (a guest's game: the host's Mario has them) */
static int calm(void) {
    if (guestGame()) {
        const SsPeer* host = ssPeerOf(1);
        return host == NULL || !host->valid || (!host->keysOff && !host->inBattle);
    }
    return marioStGetSystemLevel() == 0 && marioChkKey() && marioChkCtrl();
}

/* a co-op guest playing Mario (seat 1): its Start is Mario's Start menu, the big one (win_main.c), not a card */
static int marioGuest(void) {
    return guestGame() && portStateSyncSeatPlayer(1) == portStateSyncMe();
}

/* "Paused by": a co-op guest's game, the host's world held by a Start menu this game doesn't draw (the host's,
   or another guest's playing Mario); the host's game, held by the Start menu of the guest playing Mario */
static int hostPaused(void) {
    if (guestGame()) {
        return !marioGuest() && sHostPauseT > 2;
    }
    return portStateSyncCoopMarioMenuHeld();
}

/* the quick wheel is up (player 1's and another game's player's as soon as it counts at all) */
static int quickShown(int port) {
    return port >= PORTS || playerOne(port) ? sMenu[port].quick > 0 : sMenu[port].quick > QUICK_SHOWN;
}

static void say(int port, const char* what) {
    printf("[playermenu] player %d: %s\n", playerOf(port) > 0 ? playerOf(port) : port + 1, what);
    fflush(stdout);
}

static void closeMenu(int port, const char* why) {
    Menu* m = &sMenu[port];
    if (!m->open) {
        return;
    }
    m->open = 0;
    m->swallow = m->held; /* (held now: not the game's until let go) */
    m->confirm = 0;
    m->answer = NULL;
    printf("[playermenu] player %d's menu closed (%s)\n", playerOf(port) > 0 ? playerOf(port) : port + 1, why);
    fflush(stdout);
}

static void openMenu(int port) {
    Menu* m = &sMenu[port];
    m->open = 1;
    m->page = PAGE_HOME;
    m->sel = m->homeSel = 0;
    m->confirm = 0;
    m->answer = NULL;
    m->still = 0;
    m->repeatT = 0;
    m->t = m->pageT = 0;
    if (m->slide <= 0.0f) {
        m->height = 0.0f; /* (it grows to the page's as it comes in) */
    }
    snprintf(m->map, sizeof(m->map), "%s", gp->currentMapName);
    psndSFXOn(SFX_OPEN);
    say(port, "opened their menu");
}

int portPlayerMenuOpen(int port) {
    return port >= 0 && port < PORTS && sMenu[port].open;
}

int portPlayerMenuAny(void) {
    int p;
    for (p = 0; p < PORTS; p++) {
        if (sMenu[p].open || sMenu[p].quick > 0 || sMenu[p].ask) {
            return 1;
        }
    }
    return 0;
}

/* anything drawn in the cards' row at the bottom: a card (this computer's or another game's), a join note,
   "1P has the menu" (second_player_pc.c's notice goes above them, not over them) */
int portPlayerMenuBottomUp(void) {
    int p;
    for (p = 0; p < SLOTS; p++) {
        if (sMenu[p].open || sMenu[p].quick > 0 || sMenu[p].ask || (p < PORTS && (sJoinT[p] > 0 || sNoteT[p] > 0))) {
            return 1;
        }
    }
    return 0;
}

/* --- the pages --- */

/* the home card's buttons */
static int homeButtons(int port, int* out) {
    int n = 0;
    out[n++] = B_CHARACTER;
#ifdef TTYD_BACKEND_AURORA
    out[n++] = B_SEATS;
#endif
    out[n++] = B_EMOTES;
#ifdef TTYD_BACKEND_AURORA
    out[n++] = B_CONTROLS;
#endif
    if (ownOnline(port)) {
        out[n++] = B_SCREEN;
        out[n++] = B_LEAVE;
    }
    return n;
}

static const char* buttonName(int b) {
    static const char* const kNames[B_COUNT] = {"Character", "Who's who", "Emotes", "Controls", "Your screen",
                                                "Leave the room"};
    return b >= 0 && b < B_COUNT ? kNames[b] : "";
}

/* the seat's controller port on this computer (the strip's and the faces' "port": seat - 1) */
static int seatPort(int port) {
    const int s = seatOf(port);
    return s >= 2 ? s - 1 : 0;
}

/* Brothers mode: player 2 is Luigi (no strip) */
static int isLuigi(int port) {
    return !playerOne(port) && portCoopCharacterOf(seatPort(port)) < 0;
}

/* a character (portCoopCharacterOf) of controller port cp (1-3) as a strip entry: a Paper Mario 64 partner's
   (PORT_STRIP_PM64) when drawn as one */
static int characterEntry(int cp, int character) {
    const int pm64 = character > 0 ? portCoopPm64Of(cp) : 0;
    return pm64 > 0 ? PORT_STRIP_PM64(pm64) : character;
}

/* the Character strip: the partners offered, in the Party menu's order, each with the Paper Mario 64
   partners on it after it (a ROM linked, the setting on: port/pm64.h), and how; at most STRIP_MAX */
static int strip(int port, int* members, int* states) {
    int i, p, n = 0;
    const int sp = seatPort(port);
    if (playerOne(port)) {
        /* player 1's: Mario's partners, as the partner wheel offers them (the one out his own); a pick switches
           to them as the wheel's does (port/include/port/gameplus.h) */
        int ids[STRIP_MAX], current = 0;
        const int k = portPartnerWheelList(ids, STRIP_MAX, &current);
        for (i = 0; i < k; i++) {
            members[i] = ids[i];
            states[i] = ids[i] == current ? PORT_STRIP_MINE : PORT_STRIP_FREE;
        }
        return k;
    }
    for (i = 0; i < PORT_PARTNER_MENU_COUNT; i++) {
        const int member = gPortPartnerMenuOrder[i];
        const int state = sp > 0 ? portCoopPickState(sp, member) : PORT_STRIP_NONE;
        if (state != PORT_STRIP_NONE && n < STRIP_MAX) {
            members[n] = member;
            states[n++] = state;
        }
        for (p = 1; sp > 0 && p < PM64_PARTNER_COUNT; p++) {
            const int st = portPm64PartnerHost(p) == member ? portCoopPickState(sp, PORT_STRIP_PM64(p))
                                                            : PORT_STRIP_NONE;
            if (st != PORT_STRIP_NONE && n < STRIP_MAX) {
                members[n] = PORT_STRIP_PM64(p);
                states[n++] = st;
            }
        }
    }
    return n;
}

static int pickable(int state) {
    return state == PORT_STRIP_FREE || state == PORT_STRIP_LENT || state == PORT_STRIP_MINE;
}

static void setPage(int port, int to) {
    Menu* m = &sMenu[port];
    m->page = to;
    m->sel = 0;
    m->answer = NULL;
    m->confirm = 0;
    m->pageT = 0;
    if (to == PAGE_CHARACTER) {
        int members[STRIP_MAX], states[STRIP_MAX], n, i;
        n = strip(port, members, states);
        for (i = 0; i < n; i++) {
            if (states[i] == PORT_STRIP_MINE) {
                m->sel = i;
            }
        }
        m->pickT = PICK_REST; /* (nothing goes out until the cursor moves) */
        m->pickSent = playerOne(port) ? (n > 0 && states[m->sel] == PORT_STRIP_MINE ? members[m->sel] : 0)
                                      : characterEntry(seatPort(port), portCoopCharacterOf(seatPort(port)));
    } else if (to == PAGE_SEATS) {
        m->sel = seatOf(port) > 0 ? seatOf(port) - 1 : 0;
    } else if (to == PAGE_EMOTES) {
        m->sel = -1;
    }
}

static void back(int port) {
    Menu* m = &sMenu[port];
    const int keep = m->homeSel;
    setPage(port, PAGE_HOME);
    m->sel = keep;
}

static void optionStep(int i, int dir) {
    const Option* o = &kOptions[i];
    int* v = portSettingsFind(o->key);
    int next;
    if (v == NULL) {
        return;
    }
    if (o->max - o->min == 1) {
        next = !*v;
    } else {
        next = *v + (dir < 0 ? -o->step : o->step);
        if (next > o->max) {
            next = dir == 0 ? o->min : o->max; /* (A goes round) */
        } else if (next < o->min) {
            next = o->min;
        }
    }
    if (next != *v) {
        *v = next;
        portSettingsSave();
    }
}

static void sendEmote(int port, int e) {
    Menu* m = &sMenu[port];
    char line[32];
    if (sFrame - m->lastEmote < EMOTE_GAP) {
        psndSFXOn(SFX_BACK);
        return;
    }
    m->emote = e;
    m->lastEmote = sFrame;
    if (port == 0) {
        sEmoteOut = ((((sEmoteOut >> 3) + 1) & 31) << 3) | e; /* (the other games show it from the card) */
    }
    snprintf(line, sizeof(line), "emote %d", e);
    say(port, line);
    portAchievementCounterBit("emotes", e - 1); /* (Expressive: all six, port/include/port/achievements.h) */
}

/* the stick's emote on the wheel (0-5, from the top round to the right), -1 at rest */
static int wheelAt(int sx, int sy) {
    float a;
    int i;
    if (sx * sx + sy * sy < 48 * 48) {
        return -1;
    }
    a = atan2f((float)sx, (float)sy) * (180.0f / 3.14159265f); /* 0 up, 90 right */
    if (a < 0.0f) {
        a += 360.0f;
    }
    i = (int)((a + 30.0f) / 60.0f);
    return i % EMOTES;
}

static void homeAct(int port, int b) {
    Menu* m = &sMenu[port];
    switch (b) {
        case B_CHARACTER:
            if (isLuigi(port)) {
                psndSFXOn(SFX_BACK);
                m->answer = "In Brothers mode you play Luigi.";
                return;
            }
            psndSFXOn(SFX_OK);
            setPage(port, PAGE_CHARACTER);
            return;
        case B_SEATS:
            psndSFXOn(SFX_OK);
            setPage(port, PAGE_SEATS);
            return;
        case B_EMOTES:
            psndSFXOn(SFX_OK);
            setPage(port, PAGE_EMOTES);
            return;
        case B_CONTROLS:
            psndSFXOn(SFX_OK);
            setPage(port, PAGE_CONTROLS);
            return;
        case B_SCREEN:
            psndSFXOn(SFX_OK);
            setPage(port, PAGE_SCREEN);
            return;
        case B_LEAVE:
            if (!m->confirm) {
                m->confirm = 1;
                m->answer = "Press {A} again to leave.";
                psndSFXOn(SFX_OK);
                return;
            }
            psndSFXOn(SFX_OK);
            say(port, "left the room");
            closeMenu(port, "left the room");
            portStateSyncLeave();
            return;
    }
}

#ifdef TTYD_BACKEND_AURORA
static void seatAct(int port, int seat) {
    Menu* m = &sMenu[port];
    const char* why = NULL;
    const int mine = seatOf(port);
    char line[64];
    int asked;
    if (seat == mine) {
        psndSFXOn(SFX_BACK);
        m->answer = "Your seat.";
        return;
    }
    /* (on one computer Mario's seat is player 1's to give: their card asks them first, as online's host) */
    asked = guestGame() ? portTogetherSeatSwap(seat, 0, &why) : portTogetherSeatRequest(seat, mine, &why);
    if (!asked) {
        psndSFXOn(SFX_BACK);
        m->answer = why;
        return;
    }
    psndSFXOn(SFX_OK);
    snprintf(line, sizeof(line), "asked for seat %d (from %d)", seat, mine);
    say(port, line);
    if (asked == 2) {
        m->answer = "Asked: the swap happens once they say yes.";
        return;
    }
    if (!guestGame()) {
        /* on one computer the controllers trade places at the calm moment: the menus close now */
        int p;
        portCoopNotice("Asked: the swap comes at the next calm moment.");
        for (p = 0; p < PORTS; p++) {
            closeMenu(p, "a seat swap");
        }
        return;
    }
    m->answer = "Asked: the swap comes at the next calm moment.";
}
#endif

/* a frame of the open menu's input (pressed: new this frame; held: now; the stick as it is) */
static void menuInput(int port, u16 held, u16 pressed, int sx, int sy) {
    Menu* m = &sMenu[port];
    int dirX = 0, dirY = 0;
    if (pressed & PAD_BUTTON_START) {
        psndSFXOn(SFX_CLOSE);
        closeMenu(port, "Start");
        return;
    }
    if (pressed & PAD_BUTTON_B) {
        if (m->page != PAGE_HOME) {
            psndSFXOn(SFX_BACK);
            back(port);
        } else {
            psndSFXOn(SFX_CLOSE);
            closeMenu(port, "B");
        }
        return;
    }
    if (pressed & PAD_BUTTON_LEFT) {
        dirX = -1;
    } else if (pressed & PAD_BUTTON_RIGHT) {
        dirX = 1;
    }
    if (pressed & PAD_BUTTON_UP) {
        dirY = -1;
    } else if (pressed & PAD_BUTTON_DOWN) {
        dirY = 1;
    }
    if (held & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) {
        if (++m->repeatT > REPEAT_FIRST && (m->repeatT - REPEAT_FIRST) % REPEAT_NEXT == 0) {
            dirX = (held & PAD_BUTTON_LEFT) ? -1 : (held & PAD_BUTTON_RIGHT) ? 1 : 0;
            dirY = (held & PAD_BUTTON_UP) ? -1 : (held & PAD_BUTTON_DOWN) ? 1 : 0;
        }
    } else {
        m->repeatT = 0;
    }
    switch (m->page) {
        case PAGE_HOME: {
            int buttons[B_COUNT];
            const int n = homeButtons(port, buttons);
            int to = m->sel;
            if (dirX != 0) {
                to = (m->sel + dirX + n) % n;
            } else if (dirY != 0 && n > HOME_COLS) {
                to = m->sel + dirY * HOME_COLS;
                to = to < 0 ? to + ((n + HOME_COLS - 1) / HOME_COLS) * HOME_COLS : to;
                if (to >= n) {
                    to = dirY > 0 ? m->sel % HOME_COLS : n - 1;
                }
            }
            if (to != m->sel) {
                m->sel = m->homeSel = to;
                m->answer = NULL;
                m->confirm = 0;
                psndSFXOn(SFX_MOVE);
            }
            if (pressed & PAD_BUTTON_A) {
                m->homeSel = m->sel;
                homeAct(port, buttons[m->sel]);
            }
            break;
        }
        case PAGE_CHARACTER: {
            int members[STRIP_MAX], states[STRIP_MAX], i;
            const int n = strip(port, members, states);
            if (n <= 0) {
                break;
            }
            if (m->sel >= n) {
                m->sel = n - 1;
            }
            if (dirX != 0) {
                /* the next one that can be picked (another player's are passed over) */
                for (i = 1; i < n; i++) {
                    const int k = (m->sel + dirX * i + n * 2) % n;
                    if (pickable(states[k])) {
                        if (k != m->sel) {
                            m->sel = k;
                            m->pickT = 0;
                            psndSFXOn(SFX_MOVE);
                        }
                        break;
                    }
                }
            }
            if (m->pickT < PICK_REST) {
                m->pickT++;
            }
            if ((((m->pickT == PICK_REST) && !playerOne(port)) || (pressed & PAD_BUTTON_A)) &&
                pickable(states[m->sel]) && members[m->sel] != m->pickSent) {
                char line[48];
                if (playerOne(port)) {
                    /* (Mario's partner: switched as the partner wheel does, on A only) */
                    portCompanionTap(PORT_COMPANION_TAP_PARTNER, members[m->sel], 0);
                    m->pickSent = members[m->sel];
                } else {
                    m->pick = m->pickSent = members[m->sel];
                }
                m->pickT = PICK_REST + 1;
                if (PORT_STRIP_PM64_OF(members[m->sel]) > 0) {
                    snprintf(line, sizeof(line), "picked Paper Mario 64 partner %d", PORT_STRIP_PM64_OF(members[m->sel]));
                } else {
                    snprintf(line, sizeof(line), "picked partner %d", members[m->sel]);
                }
                say(port, line);
                if (playerOne(port)) {
                    psndSFXOn(SFX_OK);
                    closeMenu(port, "a partner switch"); /* (the switch is the game's now, as the wheel's) */
                    break;
                }
            }
            if (pressed & PAD_BUTTON_A) {
                psndSFXOn(SFX_OK);
                back(port);
            }
            break;
        }
#ifdef TTYD_BACKEND_AURORA
        case PAGE_SEATS:
            if (dirX != 0) {
                m->sel = (m->sel + dirX + PORT_SEATS) % PORT_SEATS;
                m->answer = NULL;
                psndSFXOn(SFX_MOVE);
            }
            if (pressed & PAD_BUTTON_A) {
                seatAct(port, m->sel + 1);
            }
            break;
        case PAGE_CONTROLS:
#ifdef PORT_TOUCH_OVERLAY
            if ((pressed & PAD_BUTTON_A) && ownOnline(port)) {
                psndSFXOn(SFX_OK);
                closeMenu(port, "the touch layout");
                portTouchEditRequest();
            }
#endif
            break;
#endif
        case PAGE_EMOTES: {
            const int at = wheelAt(sx, sy);
            if (at >= 0 && at != m->sel) {
                m->sel = at;
                psndSFXOn(SFX_MOVE);
            } else if (dirX != 0 || dirY != 0) {
                m->sel = m->sel < 0 ? 0 : (m->sel + (dirX != 0 ? dirX : dirY) + EMOTES) % EMOTES;
                psndSFXOn(SFX_MOVE);
            }
            if ((pressed & PAD_BUTTON_A) && m->sel >= 0) {
                sendEmote(port, m->sel + 1);
                closeMenu(port, "an emote");
            }
            break;
        }
        case PAGE_SCREEN:
            if (dirY != 0) {
                m->sel = (m->sel + dirY + OPTIONS) % OPTIONS;
                psndSFXOn(SFX_MOVE);
            } else if (dirX != 0 || (pressed & PAD_BUTTON_A)) {
                optionStep(m->sel, dirX);
                psndSFXOn(SFX_MOVE);
            }
            break;
    }
}

/* a mark in the pad's analogA already (0xC0 and up): the second screen's tap on pad 0 (companion_pc.c),
   a pick or an emote (0xE0 and up). One mark a frame: the next waits (pings too: ping_pc.c) */
static int markBusy(const PADStatus* pad) {
    return (pad->analogA & 0xC0) == 0xC0;
}

/* nothing pressed on this controller (still there; a mark it carries kept: markBusy) */
static void neutral(PADStatus* pad) {
    pad->button = 0;
    pad->stickX = pad->stickY = 0;
    pad->substickX = pad->substickY = 0;
    pad->triggerLeft = pad->triggerRight = 0;
    if (!markBusy(pad)) {
        pad->analogA = pad->analogB = 0;
    }
#ifdef TTYD_BACKEND_AURORA
    pad->extButton = 0;
#endif
}

/* a seat swap another player asks of this computer's own player (online, a guest's game: port 0's
   card; one screen's and the host's player 1, port 0, theirs); tests: $TTYD_PLAYER_MENU_ASK=<port> shows one
   there (answering only closes it) */
static int sAskTestDone;

static int askUp(int port, char* who, size_t size, int* seat) {
    static int sTest = -2;
    if (port >= PORTS) {
        /* (another game's player's: as their game sends it) */
        *seat = seatOf(port);
        snprintf(who, size, "%s", "");
#ifdef TTYD_BACKEND_AURORA
        if (sMenu[port].askFrom > 0) {
            portTogetherSeatWho(sMenu[port].askFrom, who, size);
        }
#endif
        return sMenu[port].ask;
    }
    if (sTest == -2) {
        const char* e = getenv("TTYD_PLAYER_MENU_ASK");
        sTest = e != NULL ? atoi(e) : -1;
    }
    if (port == sTest && sTest >= 0 && !sAskTestDone) {
        snprintf(who, size, portTrGame("Player %d"), 3); /* (as an online player with no name shows) */
        *seat = port + 1;
        return 1;
    }
#ifdef TTYD_BACKEND_AURORA
    /* (the host's player 1 too: a guest asking for Mario's seat was answered only on the host's Multiplayer
       page, with nothing in the field to say so; the multiplayer walkthrough) */
    if (ownOnline(port) || hostAsked(port)) {
        return portTogetherSeatAsked(who, size, seat);
    }
#endif
    (void)who;
    (void)size;
    (void)seat;
    return 0;
}

/* the seat of the player called who (a swap's asker), 0 none */
static int seatCalled(const char* who) {
#ifdef TTYD_BACKEND_AURORA
    int s;
    if (who[0] >= '1' && who[0] <= '4' && who[1] == 'P' && who[2] == '\0') {
        return who[0] - '0'; /* (one screen's asker: their badge, portTogetherSeatAsked) */
    }
    for (s = 1; s <= PORTS; s++) {
        char name[64];
        if (portTogetherSeatWho(s, name, sizeof(name)) && strcmp(name, who) == 0) {
            return s;
        }
    }
#endif
    (void)who;
    return 0;
}

/* someone else to see an emote: another player on this computer, or online playing together */
static int othersHere(void) {
    int p;
    for (p = 1; p < PORTS; p++) {
        if (playerOf(p) > 0) {
            return 1;
        }
    }
    return portStateSyncCoopHost();
}

/* Player 1's quick wheel. Z is Mario's too (pressed: the HUD; held: no D-pad menu shortcut, win_main.c;
   held through mot_kaze's spin, which nothing enters), so the wheel waits for a hold that can't be his:
   Z alone for P1_QUICK_HOLD frames, Mario standing (no spin, no walk), the stick at rest, nothing else
   pressed, Mario's controls his. Anything else in that time and this hold never opens it. Until it shows,
   the game has the controller as it is; once up, the stick is the wheel's and the game sees Z held
   only. Letting go of Z shows the emote; what's held then stays off until let go. */
static void p1Quick(PADStatus* pad) {
    Menu* m = &sMenu[0];
    const MarioWork* mario = marioGetPtr();
    const u16 now = pad->err == PAD_ERR_NONE ? pad->button : 0;
    const u16 others = (u16)(PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_BUTTON_UP | PAD_TRIGGER_R |
                             PAD_TRIGGER_L | PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y |
                             PAD_BUTTON_START);
    const int sx = pad->stickX, sy = pad->stickY;
    const int rest = abs(sx) < 20 && abs(sy) < 20 && abs(pad->substickX) < 20 && abs(pad->substickY) < 20 &&
                     pad->triggerLeft < 40 && pad->triggerRight < 40;
    int alone = (now & others) == 0 && rest;
#ifdef TTYD_BACKEND_AURORA
    alone = alone && pad->extButton == 0;
#endif
    m->ask = 0; /* (their card: p1Card) */
    m->pick = 0;
    if (!(now & PAD_TRIGGER_Z)) {
        if (m->quick > 0) {
            if (m->quickSel >= 0 && fieldUp() && calm()) {
                sendEmote(0, m->quickSel + 1);
            }
            sP1Swallow = now;
            sP1StickOff = 1;
            m->quick = 0;
        }
        sP1Hold = 0;
        sP1Spoiled = 0;
    } else if (m->quick == 0) {
        const int ready = othersHere() && fieldUp() && pauseState() == 0 && calm() && mario != NULL &&
                          mario->currMotionId == MARIO_MOTION_STAY;
        if (sP1Spoiled || !alone || !ready) {
            sP1Spoiled = 1;
            sP1Hold = 0;
        } else if (++sP1Hold >= P1_QUICK_HOLD) {
            m->quick = 1;
            m->quickSel = -1;
            say(0, "quick wheel");
        }
    } else if (!fieldUp() || !calm() || pauseState() != 0) {
        m->quick = 0; /* (a scene, a battle: gone, no emote) */
        sP1Spoiled = 1;
    } else {
        const int at = wheelAt(sx, sy);
        m->quick++;
        if (at >= 0 && at != m->quickSel) {
            m->quickSel = at;
            psndSFXOn(SFX_MOVE);
        }
    }
    if (m->quick > 0) {
        neutral(pad);
        pad->button = PAD_TRIGGER_Z; /* (Z as it was: the game sees it held, nothing else) */
    } else {
        sP1Swallow &= pad->button;
        pad->button &= (u16)~sP1Swallow;
        if (sP1StickOff) {
            if (rest) {
                sP1StickOff = 0;
            } else {
                pad->stickX = pad->stickY = 0;
            }
        }
    }
    if (m->emote && !markBusy(pad)) { /* (over the second screen's tap: next frame) */
        pad->analogA = (u8)(EMOTE_MARK | m->emote);
        m->emote = 0;
    }
}

/* online: the other games' players' cards, from what their games send (portPlayerMenuCard) */
static void showEmoteOver(int c, int e);
static void mirrorFrame(void) {
    int s;
    const int on = portStateSyncCoopOn() && gp != NULL && fieldUp();
    for (s = 1; s <= MIRRORS; s++) {
        Menu* v = &sMenu[PORTS + s - 1];
        const int player = on ? portStateSyncSeatPlayer(s) : 0;
        const SsPeer* o = player > 0 && player != portStateSyncMe() ? ssPeerOf(player) : NULL;
        const unsigned char* c;
        if (o == NULL || !o->valid || o->seq != SEQ_GAME || o->inBattle) {
            v->open = v->quick = v->ask = 0;
            v->mirrored = v->slide > 0.0f; /* (slides away) */
            v->emoteSeen = -1;
            continue;
        }
        c = o->card;
        v->mirrored = 1;
        if ((c[0] & 1) && !v->open) {
            printf("[playermenu] seat %d's card (player %d's game) is open here too\n", s, player);
            fflush(stdout);
            v->t = v->pageT = 0;
            if (v->slide <= 0.0f) {
                v->height = 0.0f;
            }
        }
        v->open = c[0] & 1;
        v->quick = (c[0] & 2) ? v->quick + 1 : 0;
        v->ask = (c[0] & 4) != 0;
        v->online = (c[0] & 8) != 0;
        if (v->page != c[1]) {
            v->pageT = 0;
        }
        v->page = c[1] <= PAGE_SCREEN ? c[1] : PAGE_HOME;
        /* (another game's bytes: each cursor kept to its page's range, -1 for none, so a mismatched build or
           a damaged card never indexes past the emote names or the wheel) */
        {
            const int sel = (int)c[2] - 1, quickSel = (int)c[4] - 1;
            const int selMax = v->page == PAGE_EMOTES     ? EMOTES
                               : v->page == PAGE_CHARACTER ? STRIP_MAX
                               : v->page == PAGE_HOME      ? B_COUNT
                                                           : 64;
            const int selMin = v->page == PAGE_EMOTES ? -1 : 0; /* (only the wheel has none) */
            v->sel = sel >= selMin && sel < selMax ? sel : selMin;
            v->homeSel = c[3] < B_COUNT ? c[3] : 0;
            v->quickSel = quickSel >= -1 && quickSel < EMOTES ? quickSel : -1;
            v->askFrom = c[5] <= PORT_SEATS ? c[5] : 0;
        }
        v->answer = NULL;
        if (v->emoteSeen >= 0 && c[6] != v->emoteSeen && (c[6] & 7) >= 1 && (c[6] & 7) <= EMOTES &&
            !portStateSyncCoopHost()) {
            /* (the host's game has a guest's emote from its controller already) */
            showEmoteOver(s - 1, c[6] & 7);
        }
        v->emoteSeen = c[6];
    }
}

void portPlayerMenuCard(unsigned char* out) {
    const Menu* m = &sMenu[0];
    memset(out, 0, PORT_PLAYER_MENU_CARD);
    if (gp == NULL || (!guestGame() && !portStateSyncCoopHost())) {
        return;
    }
    out[0] = (unsigned char)((m->open ? 1 : 0) | (quickShown(0) ? 2 : 0) | (m->ask ? 4 : 0) | (ownOnline(0) ? 8 : 0));
    out[1] = (unsigned char)m->page;
    out[2] = (unsigned char)(m->sel + 1);
    out[3] = (unsigned char)m->homeSel;
    out[4] = (unsigned char)(m->quickSel + 1);
    out[5] = (unsigned char)m->askFrom;
    out[6] = (unsigned char)sEmoteOut;
}

void portPlayerMenuJoined(int player) {
    const int port = player - 1;
    if (guestGame() || port < 1 || port >= PORTS) {
        return;
    }
    sJoinWait[port] = 1;
}

/* the join rule on one computer: an unseated controller's Start (any time; mid-battle at its end) takes
   the next free seat. Its Start is never the game's (never player 1's Start menu); the press that seats
   it doesn't open the card. */
static int sJoinAsk[PORTS];
static u16 sJoinHeld[PORTS];
static int sStartGuard[PORTS];

static void joinRule(PADStatus* pads) {
    int port;
    for (port = 1; port < PORTS; port++) {
        const u16 now = pads[port].err == PAD_ERR_NONE ? pads[port].button : 0;
        const int pressed = (now & PAD_BUTTON_START) && !(sJoinHeld[port] & PAD_BUTTON_START);
        sJoinHeld[port] = now;
        if (!portCoopUnseated(port)) {
            sJoinAsk[port] = 0;
            continue;
        }
        pads[port].button &= (u16)~PAD_BUTTON_START;
        if (pressed) {
            sJoinAsk[port] = 1;
        }
        if (sJoinAsk[port] && !(gp != NULL && (gp->inBattle || portStateSyncSharedBattle()))) {
            const int seat = portCoopJoinStart(port);
            sJoinAsk[port] = 0;
            if (seat >= 1 && seat < PORTS) {
                sStartGuard[seat] = 20; /* (this press, as the controller moves there: not their card's) */
                psndSFXOn(SFX_OK);
            }
        }
    }
}

static u16 cardTap(int port, Menu* m); /* (the drawing's geometry, below) */

/* --- the players' bars: a row of four along the bottom, 1P to 4P --- */

/* Four bars of one width, side by side across the picture's bottom edge (the 2D layer's: inside the safe
 * area, whatever the aspect ratio or the window), one per seat in seat order, each in its player's colour
 * with their number, face and character; a free seat's bar is a quiet one that says how to join (online:
 * Open), so nothing moves as players come and go. A player's card rises from their bar; their notes ("2P
 * joined", "1P has the menu", "Paused by", a controller away) are said in it. Up whenever more than one
 * plays (or online co-op, or a controller is there to join), in a calm field; while player 1's Start menu
 * is up on this screen (its help box along the bottom) they go to the top edge, and only for a note. */
#define BAR_H 30.0f
#define BAR_GAP 6.0f
#define BAR_MARGIN 8.0f
#define BAR_EDGE_Y 232.0f /* the row's outer edge from the middle (the picture's 240, less a margin) */

static int sBarsUp;    /* the row is up this frame */
static int sBarsTop;   /* at the top edge (player 1's Start menu up on this screen) */
static int sBarsSeats; /* the seats taken when it was last said */

/* player 1's Start menu is up on this screen (its pages; a guest's game only has it playing Mario) */
static int startMenuHere(void) {
    return pauseState() > 0 && pauseState() < 100;
}

/* seat s (1-4) is someone's: Mario's always; online a game's player in it; here a player there (or their
   seat kept while their controller is away) */
static int seatTaken(int s) {
    if (s == 1) {
        return 1;
    }
    if (s < 1 || s > PORTS) {
        return 0;
    }
    if (portStateSyncCoopOn() && portStateSyncSeatPlayer(s) > 0) {
        return 1;
    }
    return !guestGame() && (portCoopPlayerHere(s - 1) || portCoopSeatHeld(s - 1) > 0);
}

static int seatsTaken(void) {
    int s, n = 0;
    for (s = 1; s <= PORTS; s++) {
        n += seatTaken(s);
    }
    return n;
}

/* a controller here that could join (not seated: Co-op's join rule) */
static int padToJoin(void) {
    int p;
    for (p = 1; p < PORTS; p++) {
        if (portCoopUnseated(p)) {
            return 1;
        }
    }
    return 0;
}

/* "Paused by" at this port's bar: a guest's game while the host's Start menu holds the world (hostPaused);
   the test hook's look of it ($TTYD_PLAYER_MENU_ONLINE: player 2 on one computer, while player 1's Start menu
   is up) */
static int pausedCard(int port) {
    if (port == 0 && hostPaused()) {
        return 1;
    }
    return onlineTest() && port == 1 && !guestGame() && sPauseT > 2 && playerOf(1) > 0;
}

/* a note for seat s's bar now: "Paused by", "1P has the menu", a join, a seat kept (drawn in barsDisp) */
static int seatNote(int s) {
    int p;
    for (p = 0; p < PORTS; p++) {
        if (seatOf(p) == s && (pausedCard(p) || sNoteT[p] > 0 || sJoinT[p] > 0 ||
                               (p >= 1 && !guestGame() && portCoopSeatHeld(p) > 0))) {
            return 1;
        }
    }
    return 0;
}

/* each frame (portPlayerMenuPad): the row up, and where */
static void barsFrame(void) {
    const int seats = seatsTaken();
    const int wanted = seats >= 2 || portStateSyncCoopOn() || padToJoin();
    int s, note = 0;
    for (s = 1; s <= PORTS; s++) {
        note |= seatNote(s);
    }
    sBarsTop = startMenuHere();
    sBarsUp = wanted && gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && !portStateSyncSharedBattle() &&
              !portReplayPlaying() &&
              (sBarsTop ? note : (fieldUp() && calm()) || portPlayerMenuBottomUp() || note);
    if (sBarsUp && seats != sBarsSeats) {
        printf("[playermenu] bars up: %d of 4 seats%s\n", seats, sBarsTop ? " (at the top: the Start menu)" : "");
        fflush(stdout);
    }
    sBarsSeats = sBarsUp ? seats : 0;
}

int portPlayerMenuBarsUp(void) {
    return sBarsUp;
}

int portPlayerMenuStartMenuUp(void) {
    return startMenuHere();
}

static float barEdge(void) {
    return 304.0f * portWidescreenEdgeScale();
}

static float barW(void) {
    return (2.0f * barEdge() - 2.0f * BAR_MARGIN - 3.0f * BAR_GAP) / 4.0f;
}

/* seat s's bar: its left edge, and its top */
static float barX(int s) {
    return -barEdge() + BAR_MARGIN + (float)(s - 1) * (barW() + BAR_GAP);
}

/* a card's width: its bar's, at most 188 (side by side, open cards never cross) */
static float cardW(void) {
    const float w = barW();
    return w < 188.0f ? w : 188.0f;
}

static int homeCols(void) {
    return cardW() < 170.0f ? 3 : 4;
}

static float barTop(void) {
    return sBarsTop ? BAR_EDGE_Y : -BAR_EDGE_Y + BAR_H;
}

/* a click or tap on this computer's player's bar (port 0-3): taken (no one else gets it), 1 */
static int barTap(int port) {
    float gx, gy, x;
    const int s = seatOf(port);
    if (!sBarsUp || port < 0 || port >= PORTS || s < 1 || !portMenuTapPeek(&gx, &gy)) {
        return 0;
    }
    x = barX(s);
    if (gx < x || gx > x + barW() || gy > barTop() || gy < barTop() - BAR_H) {
        return 0;
    }
    portMenuTapTake();
    return 1;
}

/* Player 1's own card (one screen's, the online host's), as players 2-4 have theirs: a tap on 1P's bar
   opens or closes it, and with a controller Z held and Start (Start alone is the game's Start menu). While
   open it has their controller as theirs has (Mario stands still; Start or B on the home page closes it).
   Its Character strip is Mario's partners (a pick switches, as the partner wheel does). 1 while it is open
   (the quick wheel waits). */
static u16 sP1CardHeld;
static int p1Card(PADStatus* pad, int paused) {
    Menu* m = &sMenu[0];
    u16 now = pad->err == PAD_ERR_NONE ? pad->button : 0;
    u16 pressed;
    const int tapped = barTap(0);
    if (pad->err == PAD_ERR_NONE) {
        /* (the stick as the D-pad, as players 2-4's cards take it) */
        now |= pad->stickY > 48 ? PAD_BUTTON_UP : pad->stickY < -48 ? PAD_BUTTON_DOWN : 0;
        now |= pad->stickX > 48 ? PAD_BUTTON_RIGHT : pad->stickX < -48 ? PAD_BUTTON_LEFT : 0;
    }
    pressed = now & ~sP1CardHeld;
    sP1CardHeld = now;
    if (m->open) {
        if (!fieldUp() || paused || pauseState() != 0) {
            closeMenu(0, gp != NULL && gp->inBattle ? "a battle" : "the field is gone");
        } else if (strncmp(m->map, gp->currentMapName, sizeof(m->map)) != 0) {
            closeMenu(0, "a room change");
        } else if (!calm()) {
            if (++m->still > SCENE_FRAMES) {
                closeMenu(0, "a scene");
            }
        } else {
            m->still = 0;
        }
    }
    if (m->open) {
        portMenuMark(1); /* player 1's card: B backs out on every controller (port/include/port/menu_buttons.h) */
        if (!m->ask) {
            pressed |= cardTap(0, m); /* (with an ask up, p1Ask takes the taps) */
        }
        if (tapped) {
            pressed |= PAD_BUTTON_START;
        }
        menuInput(0, now, pressed, pad->stickX, pad->stickY);
        if (!m->open) {
            sP1Swallow |= now; /* (what closed it stays off until let go) */
        }
        neutral(pad);
        return 1;
    }
    if ((tapped || ((now & PAD_TRIGGER_Z) && (pressed & PAD_BUTTON_START))) && fieldUp() && calm() && !paused &&
        pauseState() == 0) {
        m->quick = 0;
        sP1Hold = 0;
        sP1Spoiled = 1;
        openMenu(0);
        neutral(pad); /* (this Start isn't the game's) */
        sP1Swallow |= now;
        return 1;
    }
    return 0;
}

/* player 1's card on this computer (one screen's, the online host's): a swap asked for Mario's seat (another
   player's here, a guest's online) shows at their place in a calm field, and takes their A and B (a tap too)
   until answered: A yes, B no. The game gets neither meanwhile, nor the one that answered until let go. */
static void p1Ask(PADStatus* pad, int paused) {
    Menu* m = &sMenu[0];
    const u16 now = pad->err == PAD_ERR_NONE ? pad->button : 0;
    const u16 ab = (u16)(PAD_BUTTON_A | PAD_BUTTON_B);
    char who[64];
    int seat = 0;
    u16 pressed = now & ~m->held;
    int answered = 0;
    m->held = now;
    m->ask = fieldUp() && calm() && !paused && m->quick == 0 && askUp(0, who, sizeof(who), &seat);
    m->askFrom = m->ask ? seatCalled(who) : 0;
    (void)seat;
    if (m->ask) {
        pressed |= cardTap(0, m);
    }
    if (m->ask && (pressed & ab)) {
        answered = 1;
        const int yes = (pressed & PAD_BUTTON_A) != 0;
#ifdef TTYD_BACKEND_AURORA
        if (hostAsked(0)) {
            portTogetherSeatAnswer(yes);
        } else
#endif
        {
            sAskTestDone = 1;
        }
        psndSFXOn(yes ? SFX_OK : SFX_BACK);
        say(0, yes ? "said yes to a swap" : "said no to a swap");
        m->ask = 0;
    }
    if (m->ask || answered) {
        m->swallow |= now & ab;
    }
    m->swallow &= now;
    pad->button &= (u16)~m->swallow;
}

void portPlayerMenuPad(PADStatus* pads) {
    int port;
    sFrame++;
    {
        /* a seat swap moved the controllers (portInputSwapPorts): an open menu isn't its new player's */
        static unsigned sSwapsSeen;
        if (sSwapsSeen != portInputSwaps()) {
            sSwapsSeen = portInputSwaps();
            for (port = 0; port < PORTS; port++) {
                closeMenu(port, "the seats swapped");
                sMenu[port].held = 0xFFFF; /* (whatever is held now was the seat's last player's) */
            }
        }
    }
    if (!portReplayPlaying()) {
        joinRule(pads);
    }
    sPauseT = pauseState() > 0 && pauseState() < 100 ? sPauseT + 1 : 0;
    {
        const SsPeer* host = guestGame() ? ssPeerOf(1) : NULL;
        const int was = hostPaused();
        sHostPauseT = host != NULL && host->valid && host->paused && host->seq == SEQ_GAME ? sHostPauseT + 1 : 0;
        if (guestGame() && hostPaused() != was) {
            printf("[playermenu] the host's world %s\n", was ? "goes on" : "is paused (a Start menu)");
            fflush(stdout);
        }
    }
    if (gp == NULL || portReplayPlaying()) {
        /* (a playback: the recording's input as it was, its menus' already in it) */
        for (port = 0; port < SLOTS; port++) {
            sMenu[port].open = 0;
            sMenu[port].quick = 0;
            sMenu[port].ask = 0;
        }
        sP1Hold = 0;
        sBarsUp = 0;
        return;
    }
    mirrorFrame();
    barsFrame();
    /* a co-op guest's game: its player joining (told at a calm moment of the field) */
    if (guestGame() && playerOf(0) > 0 && sGuestWas == 0) {
        sJoinWait[0] = 1;
    }
    sGuestWas = guestGame() ? playerOf(0) : 0;
    for (port = 0; port < PORTS; port++) {
        Menu* m = &sMenu[port];
        PADStatus* pad = &pads[port];
        const int player = playerOf(port);
        /* (a guest's own controller; the host's players on its computer while the guest playing Mario holds
           the world with its Start menu) */
        const int paused = (port == 0 || !guestGame()) && hostPaused();
        /* (and for a Start now: the host's pause as soon as its report is here, not 3 frames on; a guest's
           Start in those frames opened its card, closed at once as "the host's Start menu") */
        const int pausing = paused || (port == 0 && guestGame() && !marioGuest() && sHostPauseT > 0);
        const int mario = port == 0 && marioGuest(); /* (Start is Mario's Start menu's) */
        u16 now = 0, pressed;
        int up, sx = 0, sy = 0;
        char who[64];
        int askSeat;
        if (sJoinWait[port] && player > 0 && fieldUp() && calm() && !paused) {
            sJoinWait[port] = 0;
            sJoinT[port] = JOIN_FRAMES;
            say(port, "joined (their note)");
        }
        if (sJoinT[port] > 0 && (m->open || !fieldUp() || paused)) {
            sJoinT[port] = 0; /* (their menu, a battle, a Start menu: the note's done) */
        } else if (sJoinT[port] > 0) {
            sJoinT[port]--;
        }
        if (sNoteT[port] > 0) {
            sNoteT[port] = sPauseT > 2 || paused ? sNoteT[port] - 1 : 0;
        }
        if (playerOne(port)) {
            if (!p1Card(pad, paused)) {
                p1Quick(pad);
            }
            p1Ask(pad, paused); /* (after: p1Quick clears the ask's state) */
            continue;
        }
        if (player == 0) {
            closeMenu(port, "the player left");
            m->held = m->swallow = 0;
            m->pick = m->emote = 0;
            m->quick = 0;
            m->ask = 0;
            sJoinT[port] = sJoinWait[port] = sNoteT[port] = 0;
            continue;
        }
        if (pad->err == PAD_ERR_NONE) {
            now = pad->button;
            sx = pad->stickX;
            sy = pad->stickY;
            now |= sy > 48 ? PAD_BUTTON_UP : sy < -48 ? PAD_BUTTON_DOWN : 0;
            now |= sx > 48 ? PAD_BUTTON_RIGHT : sx < -48 ? PAD_BUTTON_LEFT : 0;
        }
        pressed = now & ~m->held;
        m->held = now;
        if (sStartGuard[port] > 0) {
            sStartGuard[port]--;
            pressed &= (u16)~PAD_BUTTON_START;
        }
        pressed |= cardTap(port, m); /* a tap on their card: its button (a phone's touch screen, the mouse) */
        if (barTap(port)) {
            pressed |= PAD_BUTTON_START; /* (a tap on their bar: their card, open or closed, as Start) */
        }
        up = fieldUp();
        /* player 1's Start menu up (on this screen): their Start says so, a moment ("1P has the menu") */
        if ((pressed & PAD_BUTTON_START) && ((!guestGame() && sPauseT > 2) || pausing)) {
            sNoteT[port] = NOTE_FRAMES;
            psndSFXOn(SFX_BACK);
            say(port, pausing ?"Start while the host's Start menu is up" : "Start while player 1's Start menu is up");
        }
        if (m->open) {
            if (!up) {
                closeMenu(port, gp->inBattle ? "a battle" : "the field is gone");
            } else if (mario) {
                closeMenu(port, "playing Mario: Start is Mario's Start menu");
            } else if (paused) {
                closeMenu(port, "the host's Start menu");
            } else if (strncmp(m->map, gp->currentMapName, sizeof(m->map)) != 0) {
                closeMenu(port, "a room change");
            } else if (!calm()) {
                if (!(pauseState() >= 100 && pauseState() < 200) && ++m->still > SCENE_FRAMES) {
                    closeMenu(port, "a scene");
                }
            } else {
                m->still = 0;
            }
        }
        /* a seat swap asked of this player: its card takes A and B first */
        m->ask = up && !paused && askUp(port, who, sizeof(who), &askSeat);
        m->askFrom = m->ask ? seatCalled(who) : 0;
        if (up && m->open) {
            /* their card: their B backs out (port/include/port/menu_buttons.h; a swap's question in the field
             * keeps the game's buttons: they walk meanwhile) */
            portMenuMark(1u << port);
        }
        if (m->ask && (pressed & (PAD_BUTTON_A | PAD_BUTTON_B))) {
            const int yes = (pressed & PAD_BUTTON_A) != 0;
#ifdef TTYD_BACKEND_AURORA
            if (ownOnline(port) || hostAsked(port)) {
                portTogetherSeatAnswer(yes);
            } else
#endif
            {
                sAskTestDone = 1;
            }
            psndSFXOn(yes ? SFX_OK : SFX_BACK);
            say(port, yes ? "said yes to a swap" : "said no to a swap");
            m->ask = 0;
            m->swallow |= now & (PAD_BUTTON_A | PAD_BUTTON_B);
            pressed &= (u16)~(PAD_BUTTON_A | PAD_BUTTON_B);
        } else if (m->ask) {
            pressed &= (u16)~(PAD_BUTTON_A | PAD_BUTTON_B);
        }
        if (up) {
            if (m->open) {
                menuInput(port, now, pressed, sx, sy);
                pressed = 0;
            } else if ((pressed & PAD_BUTTON_START) && calm() && !pausing && !mario) {
                m->quick = 0;
                sJoinT[port] = 0;
                openMenu(port);
            }
        }
        /* the quick emote: Z held in the field, the stick points, letting go shows it */
        if (!m->open && up && !paused && calm() && (now & PAD_TRIGGER_Z) && !(m->swallow & PAD_TRIGGER_Z)) {
            const int at = wheelAt(sx, sy);
            if (m->quick == 0) {
                m->quickSel = -1;
            }
            m->quick++;
            if (at >= 0 && at != m->quickSel) {
                m->quickSel = at;
                if (m->quick > QUICK_SHOWN) {
                    psndSFXOn(SFX_MOVE);
                }
            }
        } else if (m->quick > 0) {
            if (!m->open && up && m->quickSel >= 0) {
                sendEmote(port, m->quickSel + 1);
            }
            m->quick = 0;
            m->swallow |= now & PAD_TRIGGER_Z;
        }
        if (m->open || m->quick > 0 || m->ask) {
            neutral(pad); /* (their character stands still: the menu has their controller) */
        } else if (up) {
            if (!mario) {
                pad->button &= (u16)~PAD_BUTTON_START; /* (players 2-4's Start is their menu's, never the game's) */
            }
            m->swallow &= pad->button;
            pad->button &= (u16)~m->swallow;
        }
        if (markBusy(pad)) {
            /* (a mark on the pad already, the second screen's tap: the pick or emote goes next frame) */
        } else if (m->pick) {
            pad->analogA = PORT_STRIP_PM64_OF(m->pick) > 0 ? (u8)(PM64_PICK_MARK | (PORT_STRIP_PM64_OF(m->pick) - 1))
                                                           : (u8)(PICK_MARK | m->pick);
            m->pick = 0;
        } else if (m->emote) {
            pad->analogA = (u8)(EMOTE_MARK | m->emote);
            m->emote = 0;
        }
    }
}

/* --- emotes and picks, everyone's, after the exchange --- */

/* character c's place (0 Mario, 1 the partner or Luigi, 2 the follower, 3 the companion) and height */
static int characterAt(int c, Vec* at, float* height) {
    PartyEntry* party = NULL;
    MarioWork* mario = marioGetPtr();
    if (c == 1 && portBrothersOn()) {
        party = portBrothersFieldLuigi();
    } else if (c == 1) {
        party = partyGetPtr(marioGetPartyId());
        if (party == NULL) {
            party = partyGetPtr(PORT_PARTY_SLOT_LENT);
        }
    } else if (c == 2) {
        party = partyGetPtr(marioGetExtraPartyId());
    } else if (c == 3) {
        party = partyGetPtr(PORT_PARTY_SLOT_COMPANION);
    }
    if (party != NULL) {
        *at = party->position;
        *height = party->height;
        return 1;
    }
    if (c == 0 && mario != NULL) {
        *at = mario->position;
        *height = 40.0f;
        return 1;
    }
    return 0;
}

/* emote e (1-6) over character c (0 Mario, 1-3 players 2-4's) */
static void showEmoteOver(int c, int e) {
    const u32 seed = portRandState();
    Vec at;
    float height;
    if (gp == NULL || gp->inBattle || seqGetSeq() != SEQ_GAME || c < 0 || !characterAt(c, &at, &height)) {
        return;
    }
    effFukidashiEntry(kEmoteType[e - 1], kEmoteNumber[e - 1], NULL, at.x, at.y + height + 6.0f, at.z, 10.0f, 0.0f,
                      60);
    portRandSetState(seed); /* (the bubble's own drawing is the effect's; the game's numbers as they were) */
    printf("[playermenu] emote %d over character %d\n", e, c);
    fflush(stdout);
}

static void showEmote(int port, int e) {
    /* the port's character: on one computer (and the host's, a guest's controller in its seat's port)
       port p plays character p (0: Mario, player 1's quick wheel); a guest's own controller its seat's */
    showEmoteOver(guestGame() && port == 0 ? seatOf(0) - 1 : port, e);
}

void portPlayerMenuTakePad(PADStatus* pads) {
    int port;
    for (port = 0; port < PORTS; port++) {
        const u8 a = pads[port].analogA;
        /* a pick is one frame's mark: kept until second_player_pc.c's fieldRoute takes it (it takes picks only
           while the field has control: one that came while the host's own partner swap or a scene had it was
           lost, pm64x_online_both), for at most PICK_KEEP_READS pad reads */
        if (sTakenPick[port] != 0 && ++sTakenPickAge[port] > PICK_KEEP_READS) {
            sTakenPick[port] = 0;
        }
        if ((a & 0xF8) == EMOTE_MARK) {
            const int e = a & 7;
            pads[port].analogA = 0;
            if (e >= 1 && e <= EMOTES) {
                showEmote(port, e);
            }
        } else if ((a & 0xF8) == PICK_MARK) {
            pads[port].analogA = 0;
            sTakenPick[port] = a & 7; /* (second_player_pc.c's fieldRoute takes it) */
            sTakenPickAge[port] = 0;
        } else if ((a & 0xF8) == PM64_PICK_MARK) {
            pads[port].analogA = 0;
            sTakenPick[port] = PORT_STRIP_PM64((a & 7) + 1);
            sTakenPickAge[port] = 0;
        }
    }
}

int portPlayerMenuTakePick(int port) {
    int member;
    if (port < 0 || port >= PORTS) {
        return 0;
    }
    member = sTakenPick[port];
    sTakenPick[port] = 0;
    return member;
}

/* (a co-op guest's game that plays the partner, its player seated: its own field is the host's Mario's) */
static int wheelGuest(void) {
    return guestGame() && !marioGuest() && playerOf(0) > 0 && !portReplayPlaying();
}

int portPlayerMenuWheelField(void) {
    if (!wheelGuest()) {
        return -1;
    }
    return fieldUp() && calm() && !hostPaused() && !sMenu[0].open;
}

int portPlayerMenuWheelPick(int id) {
    char line[64];
    if (!wheelGuest()) {
        return 0;
    }
    if (id <= 0 || id >= PORT_STRIP_PM64(PM64_PARTNER_COUNT)) {
        return 1; /* (nothing chosen: nothing to send) */
    }
    sMenu[0].pick = sMenu[0].pickSent = id; /* (portPlayerMenuPad puts it in the controller's input, as the strip's) */
    if (PORT_STRIP_PM64_OF(id) > 0) {
        snprintf(line, sizeof(line), "picked Paper Mario 64 partner %d (the wheel)", PORT_STRIP_PM64_OF(id));
    } else {
        snprintf(line, sizeof(line), "picked partner %d (the wheel)", id);
    }
    say(0, line);
    return 1;
}

/* --- drawing --- */

#ifdef TTYD_BACKEND_AURORA
#define CARD_W cardW() /* (188 wide, or its bar's width where that is less: 4:3) */
#define CARD_BOTTOM (-BAR_EDGE_Y + BAR_H + 4.0f) /* (on top of the bars' row) */
#define HEAD_H 34.0f
#define TEXT_DARK 0x46302AFFu

/* the players' colours: the Player colours setting's (port/include/port/players.h) */
#define kPlayerColour(i) portPlayerColour(i)

static GXColor rgba(unsigned v) {
    GXColor c;
    c.r = (u8)(v >> 24);
    c.g = (u8)(v >> 16);
    c.b = (u8)(v >> 8);
    c.a = (u8)v;
    return c;
}

/* a toward b by t (0-1); alpha a */
static GXColor mix(GXColor a, GXColor b, float t, u8 alpha) {
    GXColor c;
    c.r = (u8)(a.r + (b.r - a.r) * t);
    c.g = (u8)(a.g + (b.g - a.g) * t);
    c.b = (u8)(a.b + (b.b - a.b) * t);
    c.a = alpha;
    return c;
}

static GXColor shade(unsigned colour, float toWhite, float toBlack) {
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF}, black = {0, 0, 0, 0xFF};
    GXColor c = mix(rgba(colour), white, toWhite, 0xFF);
    return mix(c, black, toBlack, 0xFF);
}

static float ease(float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/* win.tpl (the Start menu's pictures), read from the disc once into memory of our own: the game's heap
   and files never see it */
static TEXPalette* menuTpl(void) {
    static const char* const kLang[] = {"jp", "us", "ge", "fr", "sp", "it"};
    static TEXPalette* sTpl;
    static int sTried;
    DVDFileInfo info;
    char path[96];
    u8* data;
    u32 len;
    int lang;
    if (sTpl != NULL || sTried) {
        return sTpl;
    }
    sTried = 1;
    lang = gp != NULL && gp->language >= 0 && gp->language < 6 ? gp->language : 1;
#if VERSION < VERSION_US
    lang = 0;
#endif
    snprintf(path, sizeof(path), WIN_DIR "%s/win.tpl", getMarioStDvdRoot(), kLang[lang]);
    if (!DVDOpen(path, &info)) {
        printf("[playermenu] no %s: the cards without the menu's paper\n", path);
        fflush(stdout);
        return NULL;
    }
    len = (info.length + 31) & ~31u;
    data = (u8*)malloc(len ? len : 32);
    if (data != NULL && DVDReadPrio(&info, data, (s32)len, 0, 2) < 0) {
        free(data);
        data = NULL;
    }
    DVDClose(&info);
    if (data != NULL) {
        sTpl = portTplDecode(data, info.length, "win.tpl");
    }
    return sTpl;
}

static void mtx2D(Mtx out) {
    PSMTXCopy(camGetPtr(CAMERA_2D)->view, out);
}

static void quadUV(float l, float t, float r, float b, float u0, float v0, float u1, float v1) {
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(l, t, 0.0f);
    GXTexCoord2f32(u0, v0);
    GXPosition3f32(r, t, 0.0f);
    GXTexCoord2f32(u1, v0);
    GXPosition3f32(r, b, 0.0f);
    GXTexCoord2f32(u1, v1);
    GXPosition3f32(l, b, 0.0f);
    GXTexCoord2f32(u0, v1);
}

static void flatSetup(void) {
    GXSetCullMode(GX_CULL_NONE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetNumTevStages(1);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetCurrentMtx(GX_PNMTX0);
}

/* the Start menu's paper (win.tpl 6, winBgGX's pattern at its own size) over the rectangle, in two shades */
static void paper(float x, float y, float w, float h, GXColor dark, GXColor light) {
    TEXPalette* tpl = menuTpl();
    GXTexObj obj;
    Mtx m;
    if (tpl == NULL) {
        return;
    }
    flatSetup();
    TEXGetGXTexObjFromPalette(tpl, &obj, 6);
    GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP0);
    GXSetNumChans(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_C1, GX_CC_C2, GX_CC_TEXC, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A2);
    GXSetTevColor(GX_TEVREG1, dark);
    GXSetTevColor(GX_TEVREG2, light);
    mtx2D(m);
    GXLoadPosMtxImm(m, GX_PNMTX0);
    quadUV(x, y, x + w, y - h, 0.0f, 0.0f, w / 570.0f, h / 288.0f);
}

/* win.tpl picture id centred at (x, y), its size times s (negative: mirrored), tinted c */
static void menuTex(u32 id, float x, float y, float sx, float sy, GXColor c) {
    TEXPalette* tpl = menuTpl();
    if (tpl == NULL) {
        return;
    }
    winTexInit(tpl);
    winTexSet(id, (Vec){x, y, 0.0f}, (Vec){sx, sy, 1.0f}, c);
}

/* the menu's hand, pointing right at (x, y), bobbing as the Start menu's */
static void hand(float x, float y) {
    const float bob = 2.0f * sinf((float)sFrame * 0.25f);
    menuTex(0, x - 18.0f + bob + 2.0f, y - 2.0f, 1.0f, 1.0f, (GXColor){0, 0, 0, 0x80});
    menuTex(0, x - 18.0f + bob, y, 1.0f, 1.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
}

static void icon(int id, float x, float y, float s, GXColor c) {
    if (id < 0) {
        return;
    }
    winIconInit();
    winIconSet(id, (Vec){x, y, 0.0f}, (Vec){s, s, 1.0f}, c);
}

/* one line of the game's font, its top at y: squeezed (letters narrower) to fit w, smaller as well past
   two thirds of its width; right: x is its right end, centre: its middle. Its width. */
enum { AT_LEFT, AT_CENTRE, AT_RIGHT };
static float text(float x, float y, float w, float s, GXColor c, const char* t, int align, int edge) {
    /* a {START} in it: centred on the capitals at this text's size (button_markup.h; placed for text at 1, the
       bars' "{START} to join" at 0.5 had its icon sitting high) */
    const float textWas = portButtonsGameTextScale(s);
    const float full = t != NULL ? (float)FontGetMessageWidth(t) * s : 0.0f;
    float sx = s, sy = s, drawn;
    if (t == NULL || t[0] == '\0') {
        portButtonsGameTextScale(textWas);
        return 0.0f;
    }
    if (full > w && full > 0.0f) {
        const float k = w / full;
        portTextFit("player-menu", t, full, w, 1);
        if (strchr(t, '{') != NULL || k < 0.66f) {
            /* (button icons keep their shape; very long: smaller all round, centred in the line) */
            sy = s * (strchr(t, '{') != NULL ? k : k / 0.66f > 1.0f ? 1.0f : k / 0.66f);
            sx = s * k;
            y -= (s - sy) * 12.0f;
        } else {
            sx = s * k;
        }
    }
    drawn = full > w ? w : full;
    x = align == AT_RIGHT ? x - drawn : align == AT_CENTRE ? x - drawn / 2.0f : x;
    winFontInit();
    portButtonsGameTextScale(sy); /* (squeezed to fit: the icons with the letters) */
    if (edge && strchr(t, '{') != NULL) {
        /* (winFontSetEdge's string skips tags: a {START} in it showed as its name; the message path draws
           the icon and keeps the outline on the letters) */
        FontDrawEdge();
        winFontSet((Vec){x, y, 0.0f}, (Vec){sx, sy, 1.0f}, c, "%s", t);
        FontDrawEdgeOff();
    } else if (edge) {
        winFontSetEdge((Vec){x, y, 0.0f}, (Vec){sx, sy, 1.0f}, c, "%s", t);
    } else {
        winFontSet((Vec){x, y, 0.0f}, (Vec){sx, sy, 1.0f}, c, "%s", t);
    }
    portButtonsGameTextScale(textWas);
    return drawn;
}

/* a button's picture: its token ({A}, {B}...) as the game's font draws it, centred at (x, y). A button with no
 * icon (a key: Ping's G, a controller's Back) comes out as its name: in word's colour, as white letters on a
 * light card were a ghost of a letter. */
static float glyphIn(const char* token, float x, float y, float s, GXColor word) {
    const float was = portButtonsGameTextScale(s);
    const char* t = portTrGame(token);
    const char* shown = portButtonsGame(t);
    const int icon = shown != NULL && strstr(shown, "<icon") != NULL;
    const float w = (float)FontGetMessageWidth(t) * s;
    winFontInit();
    winFontSet((Vec){x - w / 2.0f, y + 12.0f * s, 0.0f}, (Vec){s, s, 1.0f},
               icon ? (GXColor){0xFF, 0xFF, 0xFF, 0xFF} : word, "%s", t);
    portButtonsGameIconScale(was);
    return w;
}

/* on a card's light body: a key's name in the chosen row's blue */
static float glyph(const char* token, float x, float y, float s) {
    return glyphIn(token, x, y, s, (GXColor){0x20, 0x50, 0xB0, 0xFF});
}

/* the width glyph draws token at s: an icon's, or a word's where the button has none */
static float glyphWidth(const char* token, float s) {
    const float was = portButtonsGameTextScale(s);
    const float w = (float)FontGetMessageWidth(portTrGame(token)) * s;
    portButtonsGameIconScale(was);
    return w;
}

/* --- the faces --- */

/* the party menu's picture of partner member (1-7), Yoshi in his colour */
static int partnerIcon(int member) {
    static const int kIcons[8] = {-1, ICON_155, ICON_156, ICON_157, ICON_158, ICON_15F, ICON_160, ICON_161};
    if (member == PARTY_MEMBER_YOSHI) {
        const int colour = pouchGetPartyColor(PARTNER_YOSHI);
        return ICON_158 + (colour >= 0 && colour <= 6 ? colour : 0);
    }
    return member >= 1 && member <= 7 ? kIcons[member] : -1;
}

/* a strip entry's name: the partner's, or the Paper Mario 64 partner's (the ROM's, in the game's encoding) */
static const char* partnerName(int member) {
    static const char* const kTags[7] = {"name_party0", "name_party1", "name_party2", "name_party3",
                                         "name_party4", "name_party5", "name_party6"};
    if (PORT_STRIP_PM64_OF(member) > 0) {
        return portPm64PartnerGameName(PORT_STRIP_PM64_OF(member));
    }
    return member >= 1 && member <= 7 ? msgSearch(kTags[member - 1]) : "";
}

/* a strip entry's picture centred at (x, y), s its size as the icons', tinted c: the party menu's icon, or the
   Paper Mario 64 partner's face from the ROM (port/pm64.h), as big as its host's icon */
static void partnerFace(int member, float x, float y, float s, GXColor c) {
    const int p = PORT_STRIP_PM64_OF(member);
    const int host = p > 0 ? portPm64PartnerHost(p) : member;
    int size = 0;
    const void* tex = p > 0 ? portPm64MenuFace(p, &size) : NULL;
    u16 iw = 0, ih = 0;
    GXTexObj obj;
    Mtx mtx;
    float half;
    if (tex == NULL) {
        icon(partnerIcon(host), x, y, s, c);
        return;
    }
    iconGetWidthHight(&iw, &ih, partnerIcon(host));
    /* (the face's head fills about 40 of its 64 texels: as tall as the icon's partner) */
    half = 0.5f * (float)(iw > ih ? iw : ih > 0 ? ih : 48) * s * 1.45f;
    flatSetup();
    GXInitTexObj(&obj, (void*)tex, (u16)size, (u16)size, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP0);
    GXSetNumChans(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C1, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A1, GX_CA_ZERO);
    GXSetTevColor(GX_TEVREG1, c);
    mtx2D(mtx);
    GXLoadPosMtxImm(mtx, GX_PNMTX0);
    quadUV(x - half, y + half, x + half, y - half, 0.0f, 0.0f, 1.0f, 1.0f);
}

/* a seat's face (seat 1 Mario, or whoever is played; 2-4 the partner, Luigi or a companion) */
static void seatFace(int seat, float x, float y, float s, GXColor c) {
    int character;
    if (seat == 1) {
        icon(ICON_19E, x, y, s, c);
        return;
    }
    character = portCoopCharacterOf(seat - 1);
    if (character < 0) {
        portLuigiFaceForce(1);
        icon(ICON_19E, x, y, s, c);
        portLuigiFaceForce(-1);
    } else if (character > 0) {
        partnerFace(characterEntry(seat - 1, character), x, y, s, c);
    }
}

static const char* seatName(int seat) {
    int character;
    if (seat == 1) {
        return portTrGame("Mario");
    }
    character = portCoopCharacterOf(seat - 1);
    return character < 0    ? portTrGame("Luigi")
           : character > 0 ? partnerName(characterEntry(seat - 1, character))
                           : portTrGame("Open");
}

/* --- the bubbles (eff_fukidashi.c's, flat in the 2D layer) --- */

/* (their sizes as eff_fukidashi.c has them: Aurora uploads what an array holds) */
extern u8 moja_model_vertex_tbl[396], moja_model_normal_tbl[69];
#if VERSION <= VERSION_DEMO_JP
extern u8 moja_model_color0_tbl[4];
#else
extern u8 moja_model_color0_tbl[32];
#endif
extern u8* moja_model_dl_0_tbl[];
extern u8 moja_model_dl_0_size_tbl[];

/* emote e (0-5) at (x, y), s times the bubble's size (about 32 across at 1); live: its mark moves */
static void bubble(int e, float x, float y, float s, int live, u8 alpha) {
    const int type = kEmoteType[e];
    const unsigned f = live ? sFrame / 4 : 1000;
    GXTexObj tex;
    Mtx base, m, k;
    flatSetup();
    PSMTXIdentity(m);
    GXLoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    mtx2D(base);
    PSMTXTrans(m, x, y, 0.0f);
    PSMTXConcat(base, m, base);
    PSMTXScale(m, s, s, 1.0f);
    PSMTXConcat(base, m, base);
    /* the bubble: its shadow, then it */
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_CLAMP, GX_AF_NONE);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    effGetTexObj(5, &tex);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    GXSetChanMatColor(GX_COLOR0A0, (GXColor){0, 0, 0, alpha});
    PSMTXTrans(m, 1.3f, -1.3f, 0.0f);
    PSMTXConcat(base, m, m);
    GXLoadPosMtxImm(m, GX_PNMTX0);
    quadUV(-16.0f, 13.0f, 16.0f, -19.0f, 0.0f, 0.0f, 2.0f, 1.0f);
    effGetTexObj(4, &tex);
    GXLoadTexObj(&tex, GX_TEXMAP0);
    GXSetChanMatColor(GX_COLOR0A0, (GXColor){0xFF, 0xFF, 0xFF, alpha});
    GXLoadPosMtxImm(base, GX_PNMTX0);
    quadUV(-16.0f, 13.0f, 16.0f, -19.0f, 0.0f, 0.0f, 2.0f, 1.0f);
    /* its mark */
    PSMTXScale(m, 1.3f, 1.3f, 1.3f);
    PSMTXConcat(base, m, base);
    GXSetNumChans(0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_A0, GX_CA_TEXA, GX_CA_ZERO);
    switch (type) {
        case 0:
        case 1: {
            /* "!" (six frames, held on the last) and "?" (four, held on the last) */
            const int frames = type == 0 ? 6 : 4;
            const int frame = live ? (int)(f % (unsigned)(frames + 6)) : frames - 1;
            effGetTexObj(type == 0 ? 6 : 7, &tex);
            GXLoadTexObj(&tex, GX_TEXMAP0);
            PSMTXScale(m, 1.0f / (float)frames, 1.0f, 1.0f);
            PSMTXTrans(k, (float)(frame < frames ? frame : frames - 1), 0.0f, 0.0f);
            PSMTXConcat(m, k, m);
            GXLoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
            GXSetTevColor(GX_TEVREG0, (GXColor){0, 0, 0, (u8)(alpha * 0xB0 / 0xFF)});
            PSMTXTrans(k, 1.0f, -1.0f, 0.0f);
            PSMTXConcat(base, k, k);
            GXLoadPosMtxImm(k, GX_PNMTX0);
            if (type == 0) {
                quadUV(-4.0f, 8.0f, 4.0f, -8.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            } else {
                quadUV(-8.0f, 16.0f, 8.0f, -8.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            }
            GXSetTevColor(GX_TEVREG0, (GXColor){0xFF, 0, 0, alpha});
            GXLoadPosMtxImm(base, GX_PNMTX0);
            if (type == 0) {
                quadUV(-4.0f, 8.0f, 4.0f, -8.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            } else {
                quadUV(-8.0f, 16.0f, 8.0f, -8.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            }
            break;
        }
        case 4: {
            /* "..." (the dots come one by one) */
            const int dots = live ? 1 + (int)(f / 3 % 4) : 3;
            int i;
            effGetTexObj(8, &tex);
            GXLoadTexObj(&tex, GX_TEXMAP0);
            GXSetTevColor(GX_TEVREG0, (GXColor){0, 0, 0, alpha});
            for (i = 0; i < dots && i < 3; i++) {
                PSMTXTrans(k, 5.0f * (float)i - 5.0f, 0.0f, 0.0f);
                PSMTXConcat(base, k, k);
                GXLoadPosMtxImm(k, GX_PNMTX0);
                quadUV(-3.0f, 3.0f, 3.0f, -3.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            }
            break;
        }
        case 5: {
            /* grumpy (four frames, round and round) */
            effGetTexObj(9, &tex);
            GXLoadTexObj(&tex, GX_TEXMAP0);
            PSMTXScale(m, 0.25f, 1.0f, 1.0f);
            PSMTXTrans(k, (float)(live ? f % 4 : 0), 0.0f, 0.0f);
            PSMTXConcat(m, k, m);
            GXLoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
            GXSetTevColor(GX_TEVREG0, (GXColor){0, 0, 0, alpha});
            GXLoadPosMtxImm(base, GX_PNMTX0);
            quadUV(-7.0f, 7.0f, 7.0f, -7.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            break;
        }
        case 6: {
            /* happy: the note, gold on its shadow, bobbing */
            const float bob = live ? 1.5f * sinf((float)sFrame * 0.3f) : 0.0f;
            effGetTexObj(10, &tex);
            GXLoadTexObj(&tex, GX_TEXMAP0);
            GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_C0, GX_CC_TEXC, GX_CC_ZERO);
            PSMTXScale(k, 0.7f, 0.7f, 1.0f);
            PSMTXConcat(base, k, base);
            GXSetTevColor(GX_TEVREG0, (GXColor){0, 0, 0, (u8)(alpha * 0xB0 / 0xFF)});
            PSMTXTrans(k, 1.0f, -1.0f + bob, 0.0f);
            PSMTXConcat(base, k, k);
            GXLoadPosMtxImm(k, GX_PNMTX0);
            quadUV(-8.0f, 16.0f, 8.0f, -8.0f, 0.0f, 0.0f, 2.0f, 2.0f);
            GXSetTevColor(GX_TEVREG0, (GXColor){0xFF, 0xCD, 0x28, alpha});
            PSMTXTrans(k, 0.0f, bob, 0.0f);
            PSMTXConcat(base, k, k);
            GXLoadPosMtxImm(k, GX_PNMTX0);
            quadUV(-8.0f, 16.0f, 8.0f, -8.0f, 0.0f, 0.0f, 2.0f, 2.0f);
            break;
        }
        case 2: {
            /* dizzy: the game's swirl (a little model), turning, in its blue */
            int j;
            PSMTXTrans(k, 5.0f, -8.0f, 0.0f);
            PSMTXConcat(base, k, m);
            PSMTXScale(k, 1.0f, 1.0f, 0.01f); /* (flat: the 2D layer's depth is thin) */
            PSMTXConcat(m, k, m);
            PSMTXRotRad(k, 'z', 0.43633232f);
            PSMTXConcat(m, k, m);
            PSMTXRotRad(k, 'x', 1.1f); /* (seen from a little above, as over a head: a ring, not a line) */
            PSMTXConcat(m, k, m);
            PSMTXRotRad(k, 'y', (float)(live ? sFrame * 10 : 30) * (3.14159265f / 180.0f));
            PSMTXConcat(m, k, m);
            PSMTXScale(k, 0.3f, 0.3f, 0.3f); /* (the effect's 0.15, twice: it reads at this size) */
            PSMTXConcat(m, k, m);
            GXLoadPosMtxImm(m, GX_PNMTX0);
            PSMTXInvXpose(m, k);
            GXLoadNrmMtxImm(k, GX_PNMTX0);
            GXSetNumTexGens(0);
            GXSetNumChans(1);
            GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
            GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
            GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
            GXClearVtxDesc();
            GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 8);
            GXSetArray(GX_VA_POS, moja_model_vertex_tbl, 6);
            GXSetVtxDesc(GX_VA_NRM, GX_INDEX8);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 6);
            GXSetArray(GX_VA_NRM, moja_model_normal_tbl, 3);
            GXSetVtxDesc(GX_VA_CLR0, GX_INDEX8);
            GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
            GXSetArray(GX_VA_CLR0, moja_model_color0_tbl, 4);
            for (j = 0; j < 21; j++) {
                GXCallDisplayList(moja_model_dl_0_tbl[j], (u32)moja_model_dl_0_size_tbl[j] << 5);
            }
            break;
        }
    }
    GXSetNumChans(0);
}

/* the wheel of the emotes round (cx, cy), radius r; sel's bigger and moving */
static void wheel(float cx, float cy, float r, int sel, GXColor pad) {
    int i;
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x40}, cx - r - 20.0f, cy + r + 24.0f, 2.0f * r + 44.0f,
                          2.0f * r + 44.0f, r + 22.0f);
    windowDispGX_Waku_col(0, pad, cx - r - 22.0f, cy + r + 22.0f, 2.0f * r + 44.0f, 2.0f * r + 44.0f, r + 22.0f);
    for (i = 0; i < EMOTES; i++) {
        const float a = (float)i * (3.14159265f / 3.0f);
        const float bx = cx + r * sinf(a), by = cy + r * cosf(a) + 3.0f;
        if (i != sel) {
            bubble(i, bx, by, 0.9f, 0, 0xFF);
        }
    }
    if (sel >= 0) { /* (the cursor's over the others) */
        const float a = (float)sel * (3.14159265f / 3.0f);
        bubble(sel, cx + (r + 3.0f) * sinf(a), cy + (r + 3.0f) * cosf(a) + 3.0f, 1.3f, 1, 0xFF);
    }
}

/* --- the card --- */

/* the card's left: over its seat's bar (barX), clamped inside the picture; it rises from the bar's top */
static float cardX(int port) {
    const float edge = barEdge() - BAR_MARGIN;
    const int seat = seatOf(port) >= 1 ? seatOf(port) : 1;
    const float x = barX(seat) + barW() / 2.0f - CARD_W / 2.0f;
    return x < -edge ? -edge : x > edge - CARD_W ? edge - CARD_W : x;
}

/* whose card a slot draws: the player (1-4) or 0 none (a port's own; another game's player's online) */
static int cardPlayer(int port) {
    if (port >= PORTS) {
        return port < SLOTS && sMenu[port].mirrored ? seatOf(port) : 0;
    }
    return playerOne(port) ? 1 : playerOf(port);
}

static unsigned cardColour(int port) {
    return kPlayerColour((seatOf(port) - 1) & 3);
}

static float pageHeight(int port, int page) {
    int buttons[B_COUNT];
    switch (page) {
        case PAGE_HOME:
            return HEAD_H + 16.0f + 46.0f * (float)((homeButtons(port, buttons) + HOME_COLS - 1) / HOME_COLS) + 30.0f;
        case PAGE_CHARACTER:
            return HEAD_H + 16.0f + 58.0f + 30.0f;
        case PAGE_SEATS:
            return HEAD_H + 16.0f + 62.0f + 30.0f;
        case PAGE_EMOTES:
            return HEAD_H + 16.0f + 130.0f + 30.0f;
        case PAGE_CONTROLS:
            return HEAD_H + 16.0f + 23.0f * (float)((CONTROLS + 1) / 2) + (ownOnline(port) ? 28.0f : 8.0f);
        case PAGE_SCREEN:
            return HEAD_H + 16.0f + 24.0f * (float)OPTIONS + 10.0f;
    }
    return 120.0f;
}

/* the card's body: shadow, frame, the menu's paper in the player's shades, and its head band */
static void cardBody(float x, float top, float w, float h, unsigned colour, u8 alpha) {
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, (u8)(alpha / 3)}, x + 4.0f, top - 4.0f, w, h, 16.0f);
    GXColor face = shade(colour, 0.80f, 0.0f), band = shade(colour, 0.0f, 0.18f);
    face.a = band.a = alpha;
    windowDispGX_Waku_col(0, face, x, top, w, h, 16.0f);
    if (h > 16.0f && alpha == 0xFF) {
        paper(x + 7.0f, top - 7.0f, w - 14.0f, h - 14.0f, shade(colour, 0.55f, 0.05f), shade(colour, 0.80f, 0.0f));
    }
    windowDispGX_Waku_col(0, band, x + 5.0f, top - 5.0f, w - 10.0f, HEAD_H, 13.0f);
}

/* the head band: "2P", a face or none, the title fitted, B's picture at the right */
static void cardHead(int port, float x, float top, const char* title, int face) {
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    char badge[8];
    float tx = x + 14.0f;
    snprintf(badge, sizeof(badge), "%dP", seatOf(port));
    tx += text(tx, top - 10.0f, 40.0f, 0.75f, white, badge, AT_LEFT, 1) + 6.0f;
    if (face) {
        seatFace(seatOf(port), tx + 13.0f, top - 5.0f - HEAD_H / 2.0f, 0.5f, white);
        tx += 30.0f;
    }
    text(tx, top - 12.0f, x + CARD_W - 34.0f - tx, 0.6f, white, title, AT_LEFT, 1);
    glyphIn("{B}", x + CARD_W - 20.0f, top - 5.0f - HEAD_H / 2.0f, 0.75f, white); /* (on the coloured head) */
}

/* the line under a page: A's picture and what it does now (or an answer) */
static void cardLabel(float x, float y, const char* what, int withA, GXColor c) {
    float lx = x + 14.0f;
    if (what == NULL || what[0] == '\0') {
        return;
    }
    if (withA) {
        lx += glyph("{A}", lx + 9.0f, y - 9.0f, 0.7f) + 6.0f;
    }
    text(lx, y, x + CARD_W - 14.0f - lx, 0.55f, c, what, AT_LEFT, 0);
}

/* a pad behind a picture: lifted over its shadow when it is the cursor's */
static void pad(float cx, float cy, float w, float h, int on, unsigned colour) {
    if (on) {
        windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x50}, cx - w / 2.0f + 3.0f, cy + h / 2.0f - 3.0f, w, h, 10.0f);
        windowDispGX_Waku_col(0, shade(colour, 0.15f, 0.0f), cx - w / 2.0f - 1.0f, cy + h / 2.0f + 1.0f, w, h, 10.0f);
    } else {
        windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0x90}, cx - w / 2.0f, cy + h / 2.0f, w, h, 10.0f);
    }
}

static void homeButtonPicture(int port, int b, float cx, float cy, float s, GXColor c) {
    switch (b) {
        case B_CHARACTER: {
            const int character = portCoopCharacterOf(seatPort(port));
            if (character < 0 || character == 0) {
                seatFace(seatOf(port), cx, cy, 0.62f * s, c);
            } else {
                partnerFace(characterEntry(seatPort(port), character), cx, cy, 0.62f * s, c);
            }
            break;
        }
        case B_SEATS:
            icon(ICON_19E, cx - 6.0f * s, cy + 3.0f * s, 0.5f * s, c);
            icon(ICON_155, cx + 7.0f * s, cy - 4.0f * s, 0.5f * s, c);
            break;
        case B_EMOTES:
            bubble(0, cx, cy + 2.0f, 0.85f * s, s > 1.0f, c.r);
            break;
        case B_CONTROLS:
            icon(ICON_212, cx, cy, 0.42f * s, c);
            break;
        case B_SCREEN:
            icon(ICON_10B, cx, cy, 0.62f * s, c);
            break;
        case B_LEAVE:
            icon(ICON_034, cx, cy, 0.62f * s, c);
            break;
    }
}

static void homeDisp(int port, const Menu* m, float x, float top, unsigned colour) {
    int buttons[B_COUNT], i;
    const int n = homeButtons(port, buttons);
    const float cellW = (CARD_W - 16.0f) / (float)HOME_COLS;
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF}, dim = {0x90, 0x90, 0x90, 0xFF};
    const char* label;
    float ly;
    cardHead(port, x, top, seatName(seatOf(port)), 1);
    for (i = 0; i < n; i++) {
        const int on = i == m->sel;
        const int row = i / HOME_COLS, col = i % HOME_COLS;
        const int inRow = n - row * HOME_COLS < HOME_COLS ? n - row * HOME_COLS : HOME_COLS;
        const float cx = x + CARD_W / 2.0f + ((float)col - (float)(inRow - 1) / 2.0f) * cellW;
        const float cy = top - HEAD_H - 34.0f - 46.0f * (float)row;
        const float pop = on ? 1.04f + 0.04f * sinf((float)sFrame * 0.2f) : 1.0f;
        const int greyed = buttons[i] == B_CHARACTER && isLuigi(port);
        pad(cx - (on ? 1.0f : 0.0f), cy + (on ? 1.0f : 0.0f), 38.0f * pop, 38.0f * pop, on, colour);
        homeButtonPicture(port, buttons[i], cx - (on ? 1.0f : 0.0f), cy + (on ? 1.0f : 0.0f), on ? 1.15f : 1.0f,
                          greyed ? dim : white);
    }
    label = m->answer != NULL ? portTrGame(m->answer) : portTrGame(buttonName(buttons[m->sel < n ? m->sel : 0]));
    ly = top - HEAD_H - 16.0f - 46.0f * (float)((n + HOME_COLS - 1) / HOME_COLS) - 4.0f;
    cardLabel(x, ly, label, m->answer == NULL, rgba(m->answer != NULL ? 0xB03020FFu : TEXT_DARK));
}

/* a strip entry's tint carries its player's number too (Player colours, port/include/port/players.h): a
   borrowed one this player's, a taken one its player's, in their colour at its corner */
static void stripBadge(int port, int member, int state, float bx, float by) {
    const int who = state == PORT_STRIP_LENT ? seatOf(port) : state == PORT_STRIP_TAKEN
                        ? portCoopPartnerPlayer(PORT_STRIP_PM64_OF(member) > 0 ? portPm64PartnerHost(PORT_STRIP_PM64_OF(member)) : member)
                        : 0;
    char badge[8];
    if (who < 1 || who > PORTS) {
        return;
    }
    snprintf(badge, sizeof(badge), "%dP", who);
    windowDispGX_Waku_col(0, shade(kPlayerColour(who - 1), 0.0f, 0.15f), bx - 11.0f, by + 6.0f, 22.0f, 12.0f, 5.0f);
    text(bx, by + 6.0f, 20.0f, 0.36f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, badge, AT_CENTRE, 1);
}

static void characterDisp(int port, const Menu* m, float x, float top, unsigned colour) {
    int members[STRIP_MAX], states[STRIP_MAX], i;
    const int n = strip(port, members, states);
    const float cx = x + CARD_W / 2.0f, cy = top - HEAD_H - 40.0f;
    const float step = 36.0f;
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    char line[96];
    cardHead(port, x, top, portTrGame("Character"), 0);
    for (i = 0; i < n; i++) {
        const float dx = ((float)i - (float)m->sel) * step;
        const int on = i == m->sel;
        GXColor c = white;
        if (fabsf(dx) > (CARD_W / 2.0f - 16.0f)) { /* (as many as the card has room for) */
            continue;
        }
        if (states[i] == PORT_STRIP_TAKEN) {
            c = (GXColor){0x60, 0x60, 0x60, 0xC0};
        } else if (states[i] == PORT_STRIP_LENT) {
            c = shade(colour, 0.35f, 0.0f); /* (borrowed: in the player's colour, as in the field) */
        }
        if (on) {
            pad(cx + dx, cy, 44.0f, 44.0f, 1, colour);
        } else if (states[i] == PORT_STRIP_MINE) {
            pad(cx + dx, cy, 32.0f, 32.0f, 0, colour); /* (the one they have now) */
        }
        partnerFace(members[i], cx + dx, cy, on ? 0.78f : 0.55f, c);
        stripBadge(port, members[i], states[i], cx + dx + (on ? 13.0f : 9.0f), cy + (on ? 19.0f : 14.0f));
    }
    if (m->sel > 0) {
        menuTex(0xC0, x + 16.0f, cy, -0.5f, 0.5f, white);
    }
    if (m->sel < n - 1) {
        menuTex(0xC0, x + CARD_W - 16.0f, cy, 0.5f, 0.5f, white);
    }
    if (n > 0 && m->sel < n) {
        const char* name = partnerName(members[m->sel]);
        if (states[m->sel] == PORT_STRIP_LENT) {
            snprintf(line, sizeof(line), portTrGame("%s (borrowed)"), name);
            name = line;
        }
        text(cx, cy - 34.0f, CARD_W - 28.0f, 0.6f, rgba(TEXT_DARK), name, AT_CENTRE, 0);
    }
}

static void seatsDisp(int port, const Menu* m, float x, float top) {
    int s;
    const float cellW = (CARD_W - 16.0f) / 4.0f, cy = top - HEAD_H - 40.0f;
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    char who[64], line[96];
    cardHead(port, x, top, portTrGame("Who's who"), 0);
    for (s = 1; s <= PORT_SEATS; s++) {
        const float cx = x + 8.0f + cellW * ((float)s - 0.5f);
        const int on = s - 1 == m->sel, mine = s == seatOf(port);
        char badge[8];
        pad(cx, cy - 2.0f, cellW - 4.0f, 50.0f, on, kPlayerColour(s - 1));
        windowDispGX_Waku_col(0, shade(kPlayerColour(s - 1), 0.0f, 0.15f), cx - 13.0f + (on ? -1.0f : 0.0f),
                              cy + 25.0f + (on ? 1.0f : 0.0f), 26.0f, 14.0f, 6.0f);
        snprintf(badge, sizeof(badge), "%dP", s);
        text(cx + (on ? -1.0f : 0.0f), cy + 25.0f, 24.0f, 0.42f, white, badge, AT_CENTRE, 1);
        seatFace(s, cx + (on ? -1.0f : 0.0f), cy - 4.0f, on ? 0.62f : 0.52f, white);
        if (mine) {
            menuTex(0xC0, cx, cy - 26.0f, 0.3f, 0.3f, white);
        }
    }
    if (m->answer != NULL) {
        cardLabel(x, cy - 36.0f, portTrGame(m->answer), 0, rgba(0xB03020FFu));
        return;
    }
    if (m->sel + 1 == seatOf(port)) {
        snprintf(line, sizeof(line), "%s", portTrGame("Your seat."));
    } else {
        if (portTogetherSeatWho(m->sel + 1, who, sizeof(who))) {
            snprintf(line, sizeof(line), portTrGame("Swap with %s"), who);
        } else {
            snprintf(line, sizeof(line), "%s", portTrGame("Open")); /* (nobody there: A takes it) */
        }
    }
    cardLabel(x, cy - 36.0f, line, m->sel + 1 != seatOf(port), rgba(TEXT_DARK));
}

static void emotesDisp(int port, const Menu* m, float x, float top, unsigned colour) {
    const float cx = x + CARD_W / 2.0f, cy = top - HEAD_H - 16.0f - 64.0f;
    cardHead(port, x, top, portTrGame("Emotes"), 0);
    wheel(cx, cy, 44.0f, m->sel, shade(colour, 0.25f, 0.1f));
    seatFace(seatOf(port), cx, cy, 0.5f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    if (m->sel >= 0) {
        cardLabel(x, top - HEAD_H - 16.0f - 130.0f - 2.0f, portTrGame(kEmoteNames[m->sel]), 1, rgba(TEXT_DARK));
    } else {
        glyph("{STICK}", cx, top - HEAD_H - 16.0f - 130.0f - 11.0f, 0.7f);
    }
    (void)colour;
}

static int controlShown(int port, const Control* c) {
    if (c->who == 1) {
        return seatOf(port) == 2 && !isLuigi(port);
    }
    if (c->who == 2) {
        return !isLuigi(port);
    }
    return 1;
}

static void controlsDisp(int port, float x, float top) {
    int i, k = 0;
    const float colW = (CARD_W - 20.0f) / 2.0f;
    cardHead(port, x, top, portTrGame("Controls"), 0);
    for (i = 0; i < CONTROLS; i++) {
        const float cx = x + 10.0f + colW * (float)(k % 2);
        const float cy = top - HEAD_H - 20.0f - 23.0f * (float)(k / 2);
        if (!controlShown(port, &kControls[i])) {
            continue;
        }
        if (glyphWidth(kControls[i].token, 0.7f) <= 24.0f) {
            glyph(kControls[i].token, cx + 12.0f, cy, 0.7f);
            text(cx + 26.0f, cy + 7.0f, colW - 30.0f, 0.5f, rgba(TEXT_DARK), portTrGame(kControls[i].action), AT_LEFT,
                 0);
        } else {
            /* a button with no icon (a controller's Back or Share, a key with Ctrl): its name in the
               chosen row's blue, then the action, both in the column (centred on the icon's spot, the
               white name ran over the row's text) */
            const float room = colW - 6.0f;
            const float named = text(cx + 2.0f, cy + 7.0f, room * 0.5f, 0.5f, rgba(0x2050B0FFu),
                                     portTrGame(kControls[i].token), AT_LEFT, 0);
            text(cx + 2.0f + named + 5.0f, cy + 7.0f, room - named - 5.0f, 0.5f, rgba(TEXT_DARK),
                 portTrGame(kControls[i].action), AT_LEFT, 0);
        }
        k++;
    }
#ifdef PORT_TOUCH_OVERLAY
    if (ownOnline(port)) {
        cardLabel(x, top - HEAD_H - 16.0f - 23.0f * (float)((k + 1) / 2) - 4.0f,
                  portTrGame("Move the on-screen controls"), 1, rgba(TEXT_DARK));
    }
#endif
}

static void screenDisp(int port, const Menu* m, float x, float top) {
    int i;
    char value[16];
    cardHead(port, x, top, portTrGame("Your screen"), 0);
    for (i = 0; i < OPTIONS; i++) {
        const Option* o = &kOptions[i];
        const int* v = portSettingsFind(o->key);
        const float ry = top - HEAD_H - 14.0f - 24.0f * (float)i;
        const int on = i == m->sel;
        const GXColor c = rgba(on ? 0x2050B0FFu : TEXT_DARK);
        if (v == NULL) {
            snprintf(value, sizeof(value), "-");
        } else if (o->max - o->min == 1) {
            snprintf(value, sizeof(value), "%s", portTrGame(*v ? "On" : "Off"));
        } else {
            snprintf(value, sizeof(value), "%d%%", *v);
        }
        if (on) {
            hand(x + 34.0f, ry - 9.0f);
        }
        text(x + CARD_W - 14.0f, ry, 44.0f, 0.5f, c, value, AT_RIGHT, 0);
        text(x + 36.0f, ry, CARD_W - 100.0f, 0.5f, c, portTrGame(o->label), AT_LEFT, 0);
    }
}

/* the small card of a seat swap asked of this player, over their menu's place */
static void askDisp(int port, float x, float top) {
    char who[64], line[96];
    int seat, s, asker = 0;
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    float lx;
    if (!askUp(port, who, sizeof(who), &seat)) {
        return;
    }
    for (s = 1; s <= PORT_SEATS; s++) {
        char name[64];
        if (portTogetherSeatWho(s, name, sizeof(name)) && strcmp(name, who) == 0) {
            asker = s;
        }
    }
    if (asker > 0) {
        char badge[8];
        snprintf(badge, sizeof(badge), "%dP", asker);
        snprintf(line, sizeof(line), portTrGame("Swap with %s?"), badge);
    } else {
        snprintf(line, sizeof(line), portTrGame("Swap with %s?"), who);
    }
    cardBody(x, top, CARD_W, 70.0f, cardColour(port), 0xFF);
    text(x + 14.0f, top - 12.0f, CARD_W - 28.0f, 0.6f, white, line, AT_LEFT, 1);
    lx = x + 22.0f;
    lx += glyph("{A}", lx, top - 52.0f, 0.75f) + 2.0f;
    lx += text(lx, top - 44.0f, 60.0f, 0.55f, rgba(TEXT_DARK), portTrGame("Yes"), AT_LEFT, 0) + 18.0f;
    lx += glyph("{B}", lx, top - 52.0f, 0.75f) + 2.0f;
    text(lx, top - 44.0f, x + CARD_W - 14.0f - lx, 0.55f, rgba(TEXT_DARK), portTrGame("No"), AT_LEFT, 0);
}

/* the controller port whose buttons a card shows (button_markup.h's portButtonsPlayer): a player's own on
   this computer; a guest's game and another game's card: this computer's first (its own player's) */
static int markupPort(int port) {
    return port >= 1 && port < PORTS && !guestGame() ? port : 0;
}

static void disp(CameraId camId, void* param) {
    const int port = (int)(intptr_t)param;
    Menu* m;
    const u32 seed = portRandState();
    float x, top, h;
    unsigned colour;
    int markWas;
    (void)camId;
    if (port < 0 || port >= SLOTS || cardPlayer(port) == 0) {
        return; /* (queued before a save state or rewind put this frame back, and since closed) */
    }
    m = &sMenu[port];
    markWas = portButtonsPlayer(markupPort(port)); /* (the buttons on their card: their controller's) */
    colour = cardColour(port);
    x = cardX(port);
    if (m->slide > 0.0f) {
        const float target = pageHeight(port, m->drawnPage);
        h = m->height;
        top = CARD_BOTTOM + h - (1.0f - ease(m->slide)) * (h + 30.0f);
        cardBody(x, top, CARD_W, h, colour, 0xFF);
        if (portUiBoxOn() && m->slide >= 1.0f) {
            char name[24];
            snprintf(name, sizeof(name), "card%d:open", seatOf(port));
            portUiBox(name, x, top - h, x + CARD_W, top);
        }
        if (fabsf(h - target) < 1.0f) {
            switch (m->drawnPage) {
                case PAGE_HOME:
                    homeDisp(port, m, x, top, colour);
                    break;
                case PAGE_CHARACTER:
                    characterDisp(port, m, x, top, colour);
                    break;
                case PAGE_SEATS:
                    seatsDisp(port, m, x, top);
                    break;
                case PAGE_EMOTES:
                    emotesDisp(port, m, x, top, colour);
                    break;
                case PAGE_CONTROLS:
                    controlsDisp(port, x, top);
                    break;
                case PAGE_SCREEN:
                    screenDisp(port, m, x, top);
                    break;
            }
        }
    }
    if (quickShown(port) && !m->open) {
        const float cx = x + CARD_W / 2.0f, cy = CARD_BOTTOM + 70.0f;
        wheel(cx, cy, 44.0f, m->quickSel, shade(colour, 0.25f, 0.1f));
        seatFace(seatOf(port), cx, cy, 0.5f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    }
    if (m->ask) {
        askDisp(port, x, (m->slide > 0.0f ? CARD_BOTTOM + m->height + 82.0f : CARD_BOTTOM + 70.0f));
    }
    portButtonsPlayer(markWas);
    portRandSetState(seed);
}

/* --- the bars (a row of four, 1P to 4P: barsFrame) and their notes: a join, "1P has the menu", "Paused
   by" (online, the host's Start menu), a controller away --- */

/* this computer's port whose player is in seat s (their notes, their buttons' pictures), -1 none */
static int portOfSeat(int s) {
    int p;
    for (p = 0; p < PORTS; p++) {
        if (seatOf(p) == s && (cardPlayer(p) > 0 || pausedCard(p) || (p >= 1 && portCoopSeatHeld(p) > 0))) {
            return p;
        }
    }
    return -1;
}

/* seat s's bar: a free seat's quiet one, how to join; a player's in their colour, "2P", their face and
   character, or their note now (with a nudge as it comes) */
static void barDisp(int s) {
    const float w = barW(), x = barX(s), top = barTop();
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const int port = portOfSeat(s);
    const unsigned colour = kPlayerColour((s - 1) & 3);
    char line[128], badge[8];
    const char* say = NULL;
    float tx, bump = 0.0f;
    GXColor band;
    portUiBox("bars:seat", x, top - BAR_H, x + w, top);
    snprintf(badge, sizeof(badge), "%dP", s);
    if (!seatTaken(s)) {
        windowDispGX_Waku_col(0, (GXColor){0x10, 0x10, 0x18, 0x60}, x, top, w, BAR_H, 12.0f);
        text(x + 8.0f, top - 7.0f, 30.0f, 0.55f, (GXColor){0xD0, 0xD0, 0xD0, 0x90}, badge, AT_LEFT, 0);
        text(x + w / 2.0f + 12.0f, top - 8.0f, w - 52.0f, 0.5f, (GXColor){0xE8, 0xE8, 0xE8, 0xB0},
             portTrGame(portStateSyncCoopOn() ? "Open" : "{PADSTART} to join"), AT_CENTRE, 0);
        return;
    }
    if (port >= 0 && pausedCard(port)) {
        /* lasting while the host's Start menu is up (this screen hasn't got it); their Start nudges it */
        char name[64];
        const char* test = getenv("TTYD_PLAYER_MENU_HOST_NAME");
        name[0] = '\0';
        if (guestGame() || portStateSyncCoopMarioMenuHeld()) {
            portTogetherSeatWho(1, name, sizeof(name)); /* (seat 1's: the host, or the guest playing Mario) */
        } else if (test != NULL) {
            snprintf(name, sizeof(name), "%s", test);
        }
        if (name[0] == '\0') {
            snprintf(name, sizeof(name), "%s", "1P");
        }
        snprintf(line, sizeof(line), portTrGame("Paused by %s"), name);
        say = line;
        bump = sNoteT[port] > NOTE_FRAMES - 10 ? 4.0f * sinf((float)(NOTE_FRAMES - sNoteT[port]) * 0.9f) : 0.0f;
    } else if (port >= 1 && !guestGame() && portCoopSeatHeld(port) > 0) {
        const int left = portCoopSeatHeld(port);
        snprintf(line, sizeof(line), portTrGame("Seat kept for %d s"), (left + portFieldRate() - 1) / portFieldRate());
        say = line;
    } else if (port >= 0 && sNoteT[port] > 0) {
        /* their Start while player 1's Start menu is up (on this screen too): a moment */
        say = portTrGame("1P has the menu");
        bump = sNoteT[port] > NOTE_FRAMES - 10 ? 4.0f * sinf((float)(NOTE_FRAMES - sNoteT[port]) * 0.9f) : 0.0f;
    } else if (port >= 0 && sJoinT[port] > 0 && sMenu[port].slide <= 0.0f) {
        /* "2P joined": Start's picture and what it does */
        snprintf(line, sizeof(line), "%s %s", portTrGame("{START}"), portTrGame("for your menu"));
        say = line;
        bump = sJoinT[port] > JOIN_FRAMES - 10 ? 4.0f * sinf((float)(JOIN_FRAMES - sJoinT[port]) * 0.9f) : 0.0f;
    }
    band = shade(colour, 0.0f, say != NULL ? 0.05f : 0.18f);
    windowDispGX_Waku_col(0, (GXColor){0, 0, 0, 0x50}, x + 3.0f, top - 3.0f, w, BAR_H, 12.0f);
    windowDispGX_Waku_col(0, band, x, top + bump, w, BAR_H, 12.0f);
    tx = x + 8.0f;
    tx += text(tx, top + bump - 6.0f, 30.0f, 0.6f, white, badge, AT_LEFT, 1) + 4.0f;
    seatFace(s, tx + 10.0f, top + bump - BAR_H / 2.0f, 0.4f, white);
    tx += 24.0f;
    text(tx, top + bump - 8.0f, x + w - 8.0f - tx, 0.5f, white, say != NULL ? say : seatName(s), AT_LEFT, 1);
}

static void barsDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    int s;
    (void)camId;
    (void)param;
    if (!sBarsUp) {
        return;
    }
    for (s = 1; s <= PORTS; s++) {
        const int port = portOfSeat(s);
        const int markWas = portButtonsPlayer(port >= 0 ? markupPort(port) : 0); /* (their controller's pictures) */
        barDisp(s);
        portButtonsPlayer(markWas);
    }
    portRandSetState(seed);
}

/* the whole picture (edge: the 2D layer's half width) darkened by alpha */
static void dimScreen(float edge, u8 alpha) {
    Mtx m;
    GXSetCullMode(GX_CULL_NONE);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetNumTexGens(0);
    GXSetNumChans(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C1);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A1);
    GXSetTevColor(GX_TEVREG1, (GXColor){0, 0, 0, alpha});
    mtx2D(m);
    GXLoadPosMtxImm(m, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-edge, 248.0f, 0.0f);
    GXPosition3f32(edge, 248.0f, 0.0f);
    GXPosition3f32(edge, -248.0f, 0.0f);
    GXPosition3f32(-edge, -248.0f, 0.0f);
}

/* player 1's: the picture dimmed, a card in the middle (the game holds on this frame, vi_aurora.c) */
static void p1LostDisp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const GXColor white = {0xFF, 0xFF, 0xFF, 0xFF};
    const float edge = 304.0f * portWidescreenEdgeScale();
    const float w = 360.0f, h = HEAD_H + 46.0f, x = -w / 2.0f, top = h / 2.0f + 16.0f;
    char line[96];
    (void)camId;
    (void)param;
    if (!portCoopPadLost()) {
        return;
    }
    dimScreen(edge + 8.0f, 0x90);
    cardBody(x, top, w, h, kPlayerColour(0), 0xFF);
    seatFace(1, x + 27.0f, top - 5.0f - HEAD_H / 2.0f, 0.5f, white);
    snprintf(line, sizeof(line), portTrGame("%s's controller disconnected"), "1P");
    text(x + 46.0f, top - 12.0f, w - 60.0f, 0.6f, white, line, AT_LEFT, 1);
    text(0.0f, top - HEAD_H - 18.0f, w - 32.0f, 0.6f, rgba(TEXT_DARK),
         portTrGame("Press {START} on a controller to continue."), AT_CENTRE, 0);
    portRandSetState(seed);
}

/* each frame: the cards come in and go, grow to their page's size */
static void animate(int port) {
    Menu* m = &sMenu[port];
    const float step = 1.0f / (float)SLIDE_FRAMES;
    if (m->open) {
        m->t++;
        m->pageT++;
        m->drawnPage = m->page;
        m->slide = m->slide + step > 1.0f ? 1.0f : m->slide + step;
    } else {
        m->slide = m->slide - step < 0.0f ? 0.0f : m->slide - step;
    }
    if (m->slide > 0.0f) {
        const float target = pageHeight(port, m->drawnPage);
        m->height = m->height <= 0.0f ? target : m->height + (target - m->height) * 0.45f;
        if (fabsf(m->height - target) < 1.0f) {
            m->height = target;
        }
    }
}

/* A tap (a phone's touch screen; a click) on this computer's player's card, in the drawing's places: the
 * ask card's Yes and No, the head's {B}, and on each page the item under it, selected, or (the selected one)
 * done as A does. Returns the presses it stands for, for this frame's menu input; the selection itself is
 * moved here. Nothing outside a card is taken. The pointer is no one's in a replay or a shared battle
 * (menu_tap_pc.c), and what a card does goes into its player's input as before. */
static u16 cardTap(int port, Menu* m) {
    float gx, gy, x, top;
    int i;
    if (port < 0 || port >= PORTS || (!m->open && !m->ask) || cardPlayer(port) == 0) { /* (player 1's: an ask) */
        return 0;
    }
    if (portMenuPoint(&gx, &gy) != 1) {
        return 0;
    }
    x = cardX(port);
    if (gx < x || gx > x + CARD_W) {
        return 0;
    }
    if (m->ask) {
        const float askTop = m->slide > 0.0f ? CARD_BOTTOM + m->height + 82.0f : CARD_BOTTOM + 70.0f;
        if (gy <= askTop && gy >= askTop - 70.0f) {
            return gx < x + CARD_W / 2.0f ? PAD_BUTTON_A : PAD_BUTTON_B; /* (Yes at the left, No at the right) */
        }
    }
    if (!m->open || m->slide < 1.0f) {
        return 0;
    }
    top = CARD_BOTTOM + m->height;
    if (gy > top || gy < CARD_BOTTOM) {
        return 0;
    }
    if (gy > top - 5.0f - HEAD_H && gx > x + CARD_W - 40.0f) {
        return PAD_BUTTON_B; /* the head's {B} */
    }
#define PICK(k)                                                                                                    \
    do {                                                                                                           \
        if ((k) == m->sel) {                                                                                       \
            return PAD_BUTTON_A;                                                                                   \
        }                                                                                                          \
        psndSFXOn(SFX_MOVE);                                                                                       \
        m->sel = (k);                                                                                              \
        m->answer = NULL;                                                                                          \
        m->confirm = 0;                                                                                            \
        return 0;                                                                                                  \
    } while (0)
    switch (m->page) {
        case PAGE_HOME: {
            int buttons[B_COUNT];
            const int n = homeButtons(port, buttons);
            const float cellW = (CARD_W - 16.0f) / (float)HOME_COLS;
            for (i = 0; i < n; i++) {
                const int row = i / HOME_COLS, col = i % HOME_COLS;
                const int inRow = n - row * HOME_COLS < HOME_COLS ? n - row * HOME_COLS : HOME_COLS;
                const float cx = x + CARD_W / 2.0f + ((float)col - (float)(inRow - 1) / 2.0f) * cellW;
                const float cy = top - HEAD_H - 34.0f - 46.0f * (float)row;
                if (fabsf(gx - cx) <= cellW / 2.0f && fabsf(gy - cy) <= 23.0f) {
                    m->homeSel = i;
                    PICK(i);
                }
            }
            return 0;
        }
        case PAGE_CHARACTER: {
            int members[STRIP_MAX], states[STRIP_MAX];
            const int n = strip(port, members, states);
            const float cx = x + CARD_W / 2.0f, cy = top - HEAD_H - 40.0f;
            for (i = 0; i < n; i++) {
                if (fabsf(gx - (cx + ((float)i - (float)m->sel) * 36.0f)) <= 18.0f && fabsf(gy - cy) <= 24.0f) {
                    if (i == m->sel) {
                        return PAD_BUTTON_A;
                    }
                    if (pickable(states[i])) {
                        psndSFXOn(SFX_MOVE);
                        m->sel = i;
                        m->pickT = 0;
                    }
                    return 0;
                }
            }
            return 0;
        }
        case PAGE_SEATS: {
            const float cellW = (CARD_W - 16.0f) / 4.0f, cy = top - HEAD_H - 40.0f;
            for (i = 0; i < PORT_SEATS; i++) {
                const float cx = x + 8.0f + cellW * ((float)i + 0.5f);
                if (fabsf(gx - cx) <= cellW / 2.0f && fabsf(gy - (cy - 2.0f)) <= 26.0f) {
                    PICK(i);
                }
            }
            return 0;
        }
        case PAGE_EMOTES: {
            const float cx = x + CARD_W / 2.0f, cy = top - HEAD_H - 16.0f - 64.0f;
            for (i = 0; i < EMOTES; i++) {
                const float a = (float)i * (3.14159265f / 3.0f);
                const float bx = cx + 44.0f * sinf(a), by = cy + 44.0f * cosf(a) + 3.0f;
                if ((gx - bx) * (gx - bx) + (gy - by) * (gy - by) <= 20.0f * 20.0f) {
                    PICK(i);
                }
            }
            return 0;
        }
        case PAGE_SCREEN:
            for (i = 0; i < OPTIONS; i++) {
                const float ry = top - HEAD_H - 14.0f - 24.0f * (float)i - 9.0f;
                if (fabsf(gy - ry) <= 12.0f) {
                    PICK(i);
                }
            }
            return 0;
        case PAGE_CONTROLS:
            /* (online on a touch screen: the line under the list moves the on-screen controls) */
            return gy < CARD_BOTTOM + 30.0f ? PAD_BUTTON_A : 0;
    }
#undef PICK
    return 0;
}
#else
static u16 cardTap(int port, Menu* m) {
    (void)port;
    (void)m;
    return 0;
}
#endif

void portPlayerMenuFrame(void) {
#ifdef TTYD_BACKEND_AURORA
    int port;
    for (port = 0; port < SLOTS; port++) {
        Menu* m = &sMenu[port];
        animate(port);
        if ((m->slide > 0.0f || quickShown(port) || m->ask) && pfh != NULL && cardPlayer(port) > 0 &&
            !startMenuHere()) { /* (never over a Start menu on this screen: a guest playing Mario's too) */
            dispEntry(CAMERA_2D, 904.0f, 1, disp, (void*)(intptr_t)port);
        }
    }
    if (sBarsUp && pfh != NULL) {
        /* (in front of player 1's Start menu's windows: at the top edge then, over nothing of it) */
        dispEntry(CAMERA_2D, 100.0f, 1, barsDisp, NULL);
    }
    if (pfh != NULL && portCoopPadLost()) {
        dispEntry(CAMERA_2D, 50.0f, 1, p1LostDisp, NULL); /* (player 1's: in front of everything, the tags too) */
    }
#endif
}
