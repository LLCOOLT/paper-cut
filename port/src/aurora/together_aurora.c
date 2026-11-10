/* Playing together (port/include/port/together.h): the picks, the seats and
 * the glue to the title menu, for the Multiplayer page (pause_online_aurora.c).
 *
 * One path for the couch and online: a pick made here starts here (once the
 * menu is closed and Mario is free), and online the host's pick also goes to
 * every guest (portStateSyncPickSet), whose game follows it: off the title
 * into its no-save file, the bingo card in the room; the rest the host's game
 * starts in every game itself (versus, the raid, the quiz, the card duel) or
 * the co-op engine brings the guests along (the adventure, the boss rush). */
#include "port/achievements.h"
#include "port/together.h"
#include "port/brothers.h"
#include "port/companion_menu.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/online_menu.h"
#include "port/party.h"
#include "port/platform.h"
#include "port/player_menu.h"
#include "port/players.h"
#include "port/raid.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/title_menu.h"
#include "port/tr.h"
#include "port/versus.h"
#include "driver/seqdrv.h"
#include "mario/mario_pouch.h"
#include <dolphin/pad.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture (a literal: kept) */

int gPortTogetherJoinOpen; /* the Multiplayer page is up: a controller's A takes the next seat (second_player.cpp) */

typedef struct Pick {
    int pick;
    const char* name;
    const char* note;
    int item;
} Pick;

static const Pick kPicks[] = {
    {PORT_PICK_TOGETHER, "The adventure", /* (no button: a room's default, picked by itself in the host's file) */
     "The adventure on the host's file: player 2 plays the partner (Luigi with Brothers mode), players 3 and 4 "
     "companions. Everyone stays in the same room.",
     ITEM_MUSHROOM},
    {PORT_PICK_VERSUS, "Versus", "Mario's side against Luigi's, on the Versus page's set-up.", ITEM_HAMMER},
    {PORT_PICK_RAID, "Raid boss", "This week's raid boss, everyone against it together.", ITEM_THUNDER_RAGE},
    {PORT_PICK_BOSS_RUSH, "Boss rush", "The story's bosses one after another, against the clock.", ITEM_STOPWATCH},
    {PORT_PICK_QUIZ, "Quiz party", "Trivia from the game's own data: A, B, X or Y answers, ten questions a round.",
     ITEM_MYSTERY},
    {PORT_PICK_CARDS, "Tattle-card duel", "Draft five cards each from the same twelve, then best of five rounds.",
     ITEM_TRADE_OFF},
    {PORT_PICK_BINGO, "Bingo",
     "One card for everyone, played on your files: a square goes to whoever reaches it first.", ITEM_STAR_PIECE},
    {PORT_PICK_DM, "Dungeon master",
     "One of you commands the enemies: a boss rush where the bosses' moves are that player's picks.",
     ITEM_FRIGHT_MASK},
};
#define PICKS ((int)(sizeof(kPicks) / sizeof(kPicks[0])))

static const Pick* find(int pick) {
    int i;
    for (i = 0; i < PICKS; i++) {
        if (kPicks[i].pick == pick) {
            return &kPicks[i];
        }
    }
    return NULL;
}

const char* portTogetherPickName(int pick) {
    const Pick* p = find(pick);
    return p != NULL ? p->name : "";
}

const char* portTogetherPickNote(int pick) {
    const Pick* p = find(pick);
    return p != NULL ? p->note : "";
}

int portTogetherPickItem(int pick) {
    const Pick* p = find(pick);
    return p != NULL ? p->item : ITEM_MUSHROOM;
}

static int online(void) {
    return portStateSyncOn() && portStateSyncPhase() != PORT_SYNC_OFF;
}

static int guest(void) {
    return online() && !ssNetIsHost();
}

int portTogetherCanPick(void) {
    return !guest();
}

static int atTitle(void) {
    return seqGetSeq() == SEQ_TITLE || portTitleScope() == PORT_TITLE_NO_FILE;
}

/* --- the picks --- */

static int sPending;    /* a pick waiting for the field (PORT_PICK_*) */
static int sPendingArg; /* the dungeon master's seat; bingo's seed */
static int sPicked;     /* the last pick (this game's, or the host's) */
static unsigned sSeenVersion;
static int sDmRun; /* the dungeon master's boss rush runs: the setting put back after it */

static int coopOn(int on) {
    int* v = portSettingsFind("second_player");
    if (v != NULL && *v != on) {
        *v = on;
        portSettingsSave();
    }
    return 1;
}

static void bingoSeed(int arg, char* out, size_t size) {
    snprintf(out, size, "PARTY-%08X", (unsigned)arg);
}

/* a pick put into play, in the field (1 started, 0 and why not) */
static int start(int pick, int arg, const char** why) {
    *why = NULL;
    switch (pick) {
        case PORT_PICK_TOGETHER:
            return 1;
        case PORT_PICK_VERSUS:
            return online() ? portStateSyncVersus(why) : portVersusStart(why);
        case PORT_PICK_RAID:
            return guest() ? 1 : portRaidStart(why);
        case PORT_PICK_BOSS_RUSH:
            return guest() ? 1 : portModeStart(PORT_MODE_BOSS_RUSH, PORT_LOADOUT_MID, why);
        case PORT_PICK_DM: {
            int* dm = portSettingsFind("dungeon_master");
            if (guest()) {
                return 1;
            }
            if (dm != NULL) {
                *dm = arg >= 2 && arg <= 4 ? arg - 1 : 1; /* (the setting: 1..3, players 2..4) */
                portSettingsSave();
            }
            sDmRun = portModeStart(PORT_MODE_BOSS_RUSH, PORT_LOADOUT_MID, why);
            return sDmRun;
        }
        case PORT_PICK_QUIZ:
            return guest() ? 1 : portQuizStart(why);
        case PORT_PICK_CARDS:
            return guest() ? 1 : portCardsDuel(PORT_DUEL_PLAYER, why);
        case PORT_PICK_BINGO: {
            char seed[32];
            bingoSeed(arg, seed, sizeof(seed));
            if (online() && portStateSyncCode()[0] != '\0') {
                char room[16];
                size_t i, n = 0;
                const char* code = portStateSyncCode();
                for (i = 0; code[i] != '\0' && n + 1 < sizeof(room); i++) {
                    if (code[i] != '-') {
                        room[n++] = code[i];
                    }
                }
                room[n] = '\0';
                portBingoJoinOnline(room, portOnlineMenuName(), seed);
            } else {
                portBingoNew(seed);
            }
            return 1;
        }
    }
    *why = "That isn't one of the party games.";
    return 0;
}

/* the title going to the file select (its Play, or a pick here). $TTYD_FILES_NEW=<slot 0-3>:<rogueport 0|1>
   (tests): New adventure there, in that slot (the launcher's New adventure's way, portBootRequestNew) */
void portTogetherFilesTest(void) {
    const char* p = getenv("TTYD_FILES_NEW");
    if (p != NULL && p[0] >= '0' && p[0] <= '3') {
        portBootRequestNew(p[0] - '0', p[1] == ':' && p[2] == '1');
    }
}

static void toFiles(void) {
    portTogetherFilesTest();
    portTitleGo(1);
}

/* the title: the adventure and bingo need a file (the file select); the rest the no-save file */
static void leaveTitle(int pick) {
    if (guest() && pick == PORT_PICK_TOGETHER) {
        portTitleGo(2);
    } else if (pick == PORT_PICK_TOGETHER || pick == PORT_PICK_BINGO) {
        toFiles();
    } else {
        portTitleGo(2);
    }
}

int portTogetherChoose(int pick, int arg, const char** why) {
    *why = NULL;
    if (find(pick) == NULL) {
        *why = "That isn't one of the party games.";
        return 0;
    }
    if (guest()) {
        *why = "The host chooses: you go straight in.";
        return 0;
    }
    if (gPortCompanionMenu && pick != PORT_PICK_TOGETHER) {
        *why = "Not on the second screen: use the game's Start menu.";
        return 0;
    }
    if (pick == PORT_PICK_BINGO) {
        arg = (int)((unsigned)time(NULL) * 2654435761u);
    }
    if (pick == PORT_PICK_TOGETHER || !online()) {
        coopOn(1); /* (on one computer the controllers play; online the engine's co-op runs anyway) */
    }
    sPicked = pick;
    sPending = pick == PORT_PICK_TOGETHER ? PORT_PICK_NONE : pick;
    sPendingArg = arg;
    if (online()) {
        portStateSyncPickSet(pick, arg, 0);
    }
    printf("[together] pick %d (%s), arg %d%s\n", pick, portTogetherPickName(pick), arg, online() ? ", sent" : "");
    fflush(stdout);
    if (atTitle()) {
        leaveTitle(pick);
    }
    return 1;
}

int portTogetherPicked(void) {
    return sPicked;
}

/* a guest: the host's new pick followed */
static void follow(void) {
    int a = 0, b = 0;
    unsigned version = 0;
    const int pick = portStateSyncPick(&a, &b, &version);
    if (version == sSeenVersion) {
        return;
    }
    sSeenVersion = version;
    if (pick == PORT_PICK_NONE) {
        return;
    }
    sPicked = pick;
    if (pick == PORT_PICK_BINGO) {
        sPending = pick;
        sPendingArg = a;
    } else if (pick != PORT_PICK_TOGETHER) {
        sPending = PORT_PICK_NONE; /* (the rest come from the host's game) */
    } /* (the adventure after Bingo, as the host's game goes back at once: the card still starts here) */
    printf("[together] the host picked %d (%s)\n", pick, portTogetherPickName(pick));
    fflush(stdout);
    /* (off the title: now, or once this game gets there; guestGo) */
    portTogetherGuestOwedSet(pick == PORT_PICK_BINGO ? 1 : 2);
}

/* a guest's way into the host's game, kept until it can be taken: a guest links whenever it likes (the
   logos still up, the file menu open, the launcher's join while its game boots, in its own field) and the
   host's pick comes once, at the link: the title takes it (its no-save file, which the co-op engine brings
   to the host's room; bingo: the files), the file menu goes back to the title for it (seq_load.c), and a
   game already in the field needs nothing (the engine brings it along) */
static void guestGo(void) {
    const int go = portTogetherGuestOwed();
    SequenceType seq;
    if (!guest()) {
        portTogetherGuestOwedSet(0);
        return;
    }
    if (go == 0) {
        return;
    }
    seq = seqGetSeq();
    if (atTitle()) {
        printf("[together] off the title for the host's pick (%s)\n", go == 1 ? "the files" : "into the host's game");
        fflush(stdout);
        portTitleGo(go);
        portTogetherGuestOwedSet(0);
    } else if (seq == SEQ_LOAD && go == 1) {
        portTogetherGuestOwedSet(0); /* (bingo: the files are where it starts) */
    } else if (seq == SEQ_GAME || seq == SEQ_MAP_CHANGE || seq == SEQ_BATTLE) {
        printf("[together] already in a game: the host's world brings this one along\n");
        fflush(stdout);
        portTogetherGuestOwedSet(0);
    }
}

/* --- seats --- */

static int sSwapA, sSwapB; /* a swap on this computer, waiting for the calm moment */

int portTogetherSeatPart(int seat) {
    switch (seat) {
        case 1:
            return PORT_PART_MARIO;
        case 2:
            return portBrothersSetting() ? PORT_PART_LUIGI : PORT_PART_PARTNER;
        case 3:
        case 4:
            return PORT_PART_COMPANION;
    }
    return PORT_PART_NONE;
}

/* a controller's name for the game's font: printable ASCII only */
static void ascii(const char* in, char* out, size_t size) {
    size_t n = 0;
    for (; in != NULL && *in != '\0' && n + 1 < size; in++) {
        const unsigned char c = (unsigned char)*in;
        if (c >= 0x20 && c < 0x7F && c != '<' && c != '>' && c != '{' && c != '}') {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

int portTogetherSeatWho(int seat, char* out, size_t size) {
    out[0] = '\0';
    if (seat < 1 || seat > PORT_SEATS) {
        return 0;
    }
    if (online()) {
        const int player = portStateSyncSeatPlayer(seat);
        if (player <= 0) {
            return 0;
        }
        portStateSyncPlayerNameShown(player, out, size);
        if (out[0] == '\0') {
            snprintf(out, size, portTrGame("Player %d"), player);
        }
        return 1;
    }
    {
        const s32 index = PADGetIndexForPort((u32)(seat - 1));
        if (index >= 0) {
            ascii(PADGetNameForControllerIndex((u32)index), out, size);
            if (out[0] == '\0') {
                snprintf(out, size, "%s %d", portTrGame("Controller"), seat);
            }
            return 1;
        }
        if (seat - 1 == portInputHomePort()) {
            snprintf(out, size, "%s", portTrGame("Keyboard")); /* (this computer's own: the keyboard, touch) */
            return 1;
        }
        if (portInputVirtualAt(seat - 1)) {
            snprintf(out, size, "%s %d", portTrGame("Controller"), seat); /* (a test's scripted controller) */
            return 1;
        }
    }
    return 0;
}

/* a local swap would leave player 1 (Mario's seat) with no input at all: the other seat has none */
static int leavesMarioBare(int a, int b) {
    return (a == 1 && !portInputHasAny(b - 1)) || (b == 1 && !portInputHasAny(a - 1));
}

int portTogetherSeatLocked(int seat) {
    (void)seat; /* (online a guest can play Mario too: the host's game stays the world, statesync.h) */
    return 0;
}

int portTogetherSeatSwap(int seat, int with, const char** why) {
    *why = NULL;
    if (seat < 1 || seat > PORT_SEATS || portTogetherSeatLocked(seat)) {
        *why = "Choose two seats to swap.";
        return 0;
    }
    if (online()) {
        if (ssNetIsHost() && with >= 1 && with <= SS_PLAYERS) {
            portStateSyncSeatSet(seat, with); /* (the host: directly; with is a player here) */
        } else {
            portStateSyncSeatAsk(seat);
        }
        return 1;
    }
    if (with < 1 || with > PORT_SEATS || with == seat) {
        *why = "Choose two seats to swap.";
        return 0;
    }
    if (leavesMarioBare(seat, with)) {
        *why = "That seat has no controller.";
        return 0;
    }
    sSwapA = seat;
    sSwapB = with;
    return 1;
}

/* on one computer: player 1 is asked before anyone takes Mario's seat (seat: the one asked for; from: the
   asker's), as online's host is; 0 none */
static int sAskSeat, sAskFrom;

int portTogetherSeatRequest(int seat, int from, const char** why) {
    *why = NULL;
    if (online() || (seat != 1 && from != 1)) {
        return portTogetherSeatSwap(seat, from, why);
    }
    if (from < 1 || from > PORT_SEATS || from == seat) {
        *why = "Choose two seats to swap.";
        return 0;
    }
    if (leavesMarioBare(seat, from)) {
        *why = "That seat has no controller.";
        return 0;
    }
    sAskSeat = seat;
    sAskFrom = from;
    printf("[together] %dP asks 1P for seat %d: 1P's answer first\n", from, seat);
    fflush(stdout);
    return 2;
}

int portTogetherSeatAsked(char* who, size_t size, int* seat) {
    int from = 0, s = 0;
    who[0] = '\0';
    if (!online()) {
        if (sAskFrom == 0) {
            return 0;
        }
        snprintf(who, size, "%dP", sAskFrom); /* (their badge: two controllers can share a name) */
        *seat = sAskSeat;
        return 1;
    }
    if (!portStateSyncSeatAsked(&from, &s)) {
        return 0;
    }
    portStateSyncPlayerNameShown(from, who, size);
    if (who[0] == '\0') {
        snprintf(who, size, portTrGame("Player %d"), from);
    }
    *seat = s;
    return 1;
}

void portTogetherSeatAnswer(int yes) {
    if (!online()) {
        if (sAskFrom == 0) {
            return;
        }
        printf("[together] 1P says %s to %dP's swap for seat %d\n", yes ? "yes" : "no", sAskFrom, sAskSeat);
        fflush(stdout);
        if (yes && !leavesMarioBare(sAskSeat, sAskFrom)) {
            sSwapA = sAskSeat;
            sSwapB = sAskFrom;
        }
        sAskSeat = sAskFrom = 0;
        return;
    }
    portStateSyncSeatAnswer(yes);
}

int portTogetherSeatWaiting(void) {
    return online() ? portStateSyncSeatPending() : sSwapA != 0 || sAskFrom != 0;
}

/* the swap on this computer: everything in the two ports trades places (portInputSwapPorts: the
   controllers, the keyboard and touch controls, a test's scripted controllers), and the seats' own state
   with them (second_player_pc.c); none is done that would leave Mario's seat with no input */
static void swapPorts(int a, int b) {
    const s32 ia = PADGetIndexForPort((u32)(a - 1));
    const s32 ib = PADGetIndexForPort((u32)(b - 1));
    if (leavesMarioBare(a, b)) {
        printf("[together] seats %d and %d not swapped: one has no controller now\n", a, b);
        fflush(stdout);
        return;
    }
    portAchievementCounterAdd("seat_swaps", 1); /* (Musical Chairs, port/include/port/achievements.h) */
    portInputSwapPorts(a - 1, b - 1);
    portCoopSeatsSwapped(a - 1, b - 1);
    printf("[together] seats %d and %d swapped (controllers %d and %d; keyboard in %d)\n", a, b, (int)ia, (int)ib,
           portInputHomePort() + 1);
    fflush(stdout);
}

/* --- tests: $TTYD_TOGETHER_PICK=<frame>:<pick>[,...] (together, versus, raid, bossrush, quiz, cards, bingo,
   dm2..dm4) chooses as the page would at that frame; $TTYD_TOGETHER_SEAT=<frame>:<seat>:<with>[,...] a swap --- */

static int pickNamed(const char* name, size_t n, int* arg) {
    static const struct {
        const char* name;
        int pick;
    } kNames[] = {{"together", PORT_PICK_TOGETHER}, {"versus", PORT_PICK_VERSUS}, {"raid", PORT_PICK_RAID},
                  {"bossrush", PORT_PICK_BOSS_RUSH}, {"quiz", PORT_PICK_QUIZ},   {"cards", PORT_PICK_CARDS},
                  {"bingo", PORT_PICK_BINGO},        {"dm", PORT_PICK_DM}};
    size_t i;
    *arg = 0;
    for (i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++) {
        const size_t len = strlen(kNames[i].name);
        if (n >= len && strncmp(name, kNames[i].name, len) == 0) {
            if (kNames[i].pick == PORT_PICK_DM) {
                *arg = n > len ? name[len] - '0' : 2;
            }
            return kNames[i].pick;
        }
    }
    return PORT_PICK_NONE;
}

int portTogetherTest(const char* what, const char** why) {
    int seat = 0, with = 0, arg = 0, pick;
    *why = NULL;
    if (sscanf(what, "seat %d %d", &seat, &with) == 2) {
        return portTogetherSeatSwap(seat, with, why);
    }
    pick = pickNamed(what, strlen(what), &arg);
    if (pick == PORT_PICK_NONE) {
        *why = "no such pick";
        return 0;
    }
    return portTogetherChoose(pick, arg, why);
}

#define TEST_PICK_HOLD 3600
static const char* sTestHeld; /* a test pick held for the others (its name in $TTYD_TOGETHER_PICK) */
static size_t sTestHeldN;
static unsigned sTestHeldAt;
static int sTestFree;

static void testPick(const char* name, size_t n) {
    int arg;
    const char* why = NULL;
    const int pick = pickNamed(name, n, &arg);
    if (pick == PORT_PICK_NONE || !portTogetherChoose(pick, arg, &why)) {
        printf("[together] test pick %.*s refused: %s\n", (int)n, name, why != NULL ? why : "unknown");
    }
    fflush(stdout);
}

static void testFrame(unsigned frame) {
    const char* picks = getenv("TTYD_TOGETHER_PICK");
    const char* seats = getenv("TTYD_TOGETHER_SEAT");
    const char* p;
    /* $TTYD_BOOT_NEW=<slot 0-3>:<rogueport 0|1>: the launcher's New adventure, at the first title frame */
    if (frame == 1 && (p = getenv("TTYD_BOOT_NEW")) != NULL && p[0] >= '0' && p[0] <= '3') {
        portBootRequestNew(p[0] - '0', p[1] == ':' && p[2] == '1');
    }
    /* (an online host's party game is held until every other player is free in the field half a second, at
       most TEST_PICK_HOLD frames: a versus fight or a party game a guest still on the title or loading can't
       join is refused as busy, and the games' launches are staggered under load, so a fixed frame alone
       raced the guest's start: together_party_versus) */
    if (sTestHeld != NULL) {
        sTestFree = portStateSyncPeerShows("here") && !portStateSyncPeerShows("busy") ? sTestFree + 1 : 0;
        if (sTestFree >= 30 || frame - sTestHeldAt >= TEST_PICK_HOLD) {
            const char* name = sTestHeld;
            sTestHeld = NULL;
            printf("[together] test pick %.*s: %s after %u frames\n", (int)sTestHeldN, name,
                   sTestFree >= 30 ? "the others are free" : "still waiting, picked anyway", frame - sTestHeldAt);
            testPick(name, sTestHeldN);
        }
    }
    for (p = picks; p != NULL && *p != '\0';) {
        const unsigned at = (unsigned)strtoul(p, (char**)&p, 10);
        const char* name = *p == ':' ? p + 1 : p;
        const size_t n = strcspn(name, ",");
        if (at == frame) {
            int arg;
            const int pick = pickNamed(name, n, &arg);
            if (pick != PORT_PICK_NONE && pick != PORT_PICK_TOGETHER && online() && !guest() &&
                (!portStateSyncPeerShows("here") || portStateSyncPeerShows("busy"))) {
                sTestHeld = name;
                sTestHeldN = n;
                sTestHeldAt = frame;
                sTestFree = 0;
                printf("[together] test pick %.*s held until the other players are free in the field\n", (int)n,
                       name);
                fflush(stdout);
            } else {
                testPick(name, n);
            }
        }
        p = name + n + (name[n] == ',');
    }
    for (p = seats; p != NULL && *p != '\0';) {
        const unsigned at = (unsigned)strtoul(p, (char**)&p, 10);
        int seat = 0, with = 0;
        if (*p == ':') {
            seat = (int)strtol(p + 1, (char**)&p, 10);
        }
        if (*p == ':') {
            with = (int)strtol(p + 1, (char**)&p, 10);
        }
        if (at == frame) {
            const char* why = NULL;
            printf("[together] test seat %d with %d: %s\n", seat, with,
                   portTogetherSeatSwap(seat, with, &why) ? "asked" : why != NULL ? why : "refused");
            fflush(stdout);
        }
        p += strcspn(p, ",");
        p += *p == ',';
    }
}

/* --- each frame --- */

/* a party game the host's game started (online): 1 waiting for it to begin, 2 under way; when it's over
   the room goes back to the adventure (the pick PORT_PICK_TOGETHER again: the engine's co-op on again) */
static int sParty;
static unsigned sPartyFrames;

static void partyOver(void) {
    sParty = 0;
    if (online() && !guest()) {
        portStateSyncPickSet(PORT_PICK_TOGETHER, 0, 0);
        sPicked = PORT_PICK_TOGETHER;
        printf("[together] the party game is over: back to the adventure\n");
        fflush(stdout);
    }
}

static void partyFrame(void) {
    int running;
    if (sParty == 0) {
        return;
    }
    /* (versus online: asked, its start waiting for the other player's yes, counts; dropped on a no, over) */
    running = portQuizActive() || portCardsActive() || portModeStatus()->mode != PORT_MODE_NONE || portVersusBusy() ||
              portStateSyncVersusPending() || portRaidActive();
    if (sParty == 1) {
        if (running) {
            sParty = 2;
        } else if (++sPartyFrames > 900) { /* (it never began: 15 seconds) */
            partyOver();
        }
    } else if (!running) {
        partyOver();
    }
}

/* --- hosting: the room plays the host's adventure --- */

static void hostFrame(void) {
    unsigned version = 0;
    if (atTitle() || portTitleTempFile() || seqGetSeq() != SEQ_GAME) {
        return;
    }
    /* a room with nothing picked yet once the host is in a file (hosted there, or from the title and then its
       Play): the adventure, sent to every guest (the pick a guest's title, file menu or logos wait for:
       guestGo; until then they wait in the room). Not the player's "second_player" setting: online the
       engine's co-op runs anyway. (Tests with $TTYD_TOGETHER_PICK pick at their own frame.) */
    if (online() && !guest() && getenv("TTYD_TOGETHER_PICK") == NULL) {
        portStateSyncPick(NULL, NULL, &version);
        if (version == 0) {
            sPicked = PORT_PICK_TOGETHER;
            portStateSyncPickSet(PORT_PICK_TOGETHER, 0, 0);
            printf("[together] hosting in a file: the room plays the host's adventure (pick 1, sent)\n");
            fflush(stdout);
        }
    }
}

void portTogetherFrame(void) {
    static unsigned sFrame;
    const int calm = !atTitle() && portPartyFieldReady();
    testFrame(++sFrame);
    if (guest()) {
        follow();
    }
    guestGo();
    hostFrame();
    partyFrame();
    if (sAskFrom != 0 && (online() || !portCoopPlayerHere(sAskFrom - 1))) {
        printf("[together] %dP's ask for seat %d is gone (%s)\n", sAskFrom, sAskSeat, online() ? "online" : "they left");
        fflush(stdout);
        sAskSeat = sAskFrom = 0;
    }
    if (sDmRun && portModeStatus()->mode == PORT_MODE_NONE) { /* (the party's over: the story's battles as before) */
        int* dm = portSettingsFind("dungeon_master");
        sDmRun = 0;
        if (dm != NULL) {
            *dm = 0;
            portSettingsSave();
        }
    }
    if (guest() && sPending == PORT_PICK_BINGO && !calm && !atTitle() && seqGetSeq() == SEQ_GAME) {
        /* (a guest's Bingo is a card beside the adventure: it needs no free field, and a guest playing
           together as the partner may have none for long; the host's game is back to the adventure) */
        const char* why = NULL;
        sPending = PORT_PICK_NONE;
        if (!start(PORT_PICK_BINGO, sPendingArg, &why)) {
            printf("[together] %s couldn't start: %s\n", portTogetherPickName(PORT_PICK_BINGO), why != NULL ? why : "?");
            fflush(stdout);
            portCoopNotice(why != NULL ? why : "It can't start now."); /* (said, not only logged) */
        }
    }
    if (!calm) {
        return;
    }
    if (sSwapA != 0) {
        swapPorts(sSwapA, sSwapB);
        sSwapA = sSwapB = 0;
    }
    if (sPending != PORT_PICK_NONE) {
        const char* why = NULL;
        const int pick = sPending;
        sPending = PORT_PICK_NONE;
        if (!start(pick, sPendingArg, &why)) {
            printf("[together] %s couldn't start: %s\n", portTogetherPickName(pick), why != NULL ? why : "?");
            fflush(stdout);
            portCoopNotice(why != NULL ? why : "It can't start now."); /* (said, not only logged) */
            partyOver(); /* (the room back to the adventure) */
        } else if (online() && !guest()) {
            /* the host's game watches each until it's over (partyFrame), versus and the raid too: they end in
               the engine, and the room went on not playing together for good after them */
            sParty = pick == PORT_PICK_QUIZ || pick == PORT_PICK_CARDS || pick == PORT_PICK_BOSS_RUSH ||
                             pick == PORT_PICK_DM || pick == PORT_PICK_VERSUS || pick == PORT_PICK_RAID
                         ? 1
                         : 0;
            sPartyFrames = 0;
            if (pick == PORT_PICK_BINGO) {
                partyOver(); /* (bingo is a card beside the adventure: the room plays on together) */
            }
        }
    }
}

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void frameHook(void) {
    portTogetherFrame();
}

/* (marioStMain: each game frame; the title's frames call portTogetherFrame themselves) */
void portTogetherInit(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
