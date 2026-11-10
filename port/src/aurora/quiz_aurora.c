/* The quiz party (port/include/port/party.h, docs/MODES.md): a trivia game
 * for up to four players on one screen, in the game's quiz-show style
 * (Quizmo's sounds, the game's windows and font). Each question comes from
 * the game's own data: the items' and badges' table (what an item restores
 * and sells for, a badge's BP), the item shops' own shelves and prices
 * (quiz_shops.inc, tools/port/gen_quiz_shops.py) and the enemies' numbers from their
 * battle units (port/src/common/tattle_cards_gen.c), named with the game's
 * own text, so every language plays in its own words.
 *
 * Each player answers with A, B, X or Y on their own controller; a right
 * answer is worth 100 points and up to 50 more for speed. Ten questions to a
 * round. Online every player answers on their own controller, through
 * lockstep (the questions from the game clock, the same in every game), from
 * the Start menu's form; not in a replay. With each player in their own game
 * ("sync what matters", port/statesync.h) the host's game plays it: the
 * others' presses come from their games, and their games show the host's
 * quiz as it goes. While it's up the
 * field stands still (the system level the game's own item scenes use) and
 * the game gets no input; it ends with Z on the first controller, or B after
 * the last question. */
#include "port/achievements.h"
#include "port/menu_tap.h"
#include "port/party.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/statesync.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "statuswindow.h"
#include "pmario_sound.h"
#include "win/win_main.h"
#include <dolphin/os.h>
#include <dolphin/pad.h>

#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "quiz_shops.inc"

#define QUESTIONS 10
#define ANSWER_FRAMES 600 /* ten seconds */
#define REVEAL_FRAMES 180
#define PLAYERS 4

enum { S_OFF, S_JOIN, S_ASK, S_REVEAL, S_RESULTS };

typedef struct Question {
    char text[192];
    char option[4][96];
    int right;   /* 0-3: A, B, X, Y */
    short icon;  /* an item's icon beside it, or -1 */
} Question;

static int sState, sAsked;
static int sNumber, sClock;
static unsigned sJoined;
static int sScore[PLAYERS], sPick[PLAYERS], sGain[PLAYERS], sPickedAt[PLAYERS];
static Question sQ;
static PortPartyRand sRand;
static int sWinner = -1, sWinnerScore;
static int sOwnLevel;

/* Quiz Whiz ("Win the party quiz"): a win over someone, the top score alone with two players or more (who then
   has points: a 0 to 0 is a draw). A solo quiz has no one to beat: it once gave it, with 0 points too */
static int quizWon(unsigned joined, int winner, int score) {
    int players = 0, p;
    for (p = 0; p < PLAYERS; p++) {
        players += (int)(joined >> p & 1u);
    }
    return winner > 0 && players >= 2 && score > 0;
}

static const unsigned short kButtons[4] = {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X, PAD_BUTTON_Y};
static const char kLetters[4] = {'A', 'B', 'X', 'Y'};
/* the answers' labels on the screen: the buttons that give them (button_markup.h), so a keyboard shows its keys
 * and a controller its face buttons, as the line under the question does (the GameCube's letters A/B/X/Y beside
 * "Answer with SPC, E, R or F" on a keyboard) */
static const char* const kTokens[4] = {"{A}", "{B}", "{X}", "{Y}"};

int portQuizActive(void) {
    return sState != S_OFF;
}

int portQuizPad(void) {
    return sState != S_OFF;
}

int portQuizLastWinner(int* score) {
    *score = sWinnerScore;
    return sWinner;
}

int portQuizStart(const char** why) {
    if (portReplayActive()) {
        *why = "Not during a replay.";
        return 0;
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (sState != S_OFF || portCardsActive()) {
        *why = "A party game is already up.";
        return 0;
    }
    if (portStateSyncPartyRole() == 2) {
        *why = "Online, the host starts the quiz: it shows here too.";
        return 0;
    }
    sAsked = 1;
    return 1;
}

/* --- online, each player in their own game ("sync what matters", port/statesync.h) ---------------
 * The host's game plays the quiz: players 2-4's presses come from their games, and the state goes
 * to every guest's game, which shows it (the field held still meanwhile) and sends its player's
 * presses. */

static unsigned short sNow[PLAYERS]; /* this frame's presses, each player's */

/* the presses read this frame (once: a guest's come once, taken as they're read) */
static void readPresses(void) {
    int p;
    if (sState == S_OFF) {
        /* (the guests' presses are taken only while the quiz is on: read every frame, the quiz's off
           frames took a card duel's presses before cards_aurora.c could, and its draft waited for ever
           on the other player) */
        memset(sNow, 0, sizeof(sNow));
        return;
    }
    for (p = 0; p < PLAYERS; p++) {
        sNow[p] = portStateSyncPartyRole() == 1 && p >= 1 ? portStateSyncPartyPressed(p + 1) : portPartyPressed(p);
    }
}

static unsigned short pressedNow(int p) {
    return p >= 0 && p < PLAYERS ? sNow[p] : 0;
}

typedef struct QuizNet {
    int state, number, clock;
    unsigned joined;
    int score[PLAYERS], pick[PLAYERS], gain[PLAYERS];
    int winner, winnerScore;
    Question q;
} QuizNet;

static void netFill(QuizNet* n) {
    memset(n, 0, sizeof(*n));
    n->state = sState;
    n->number = sNumber;
    n->clock = sClock;
    n->joined = sJoined;
    memcpy(n->score, sScore, sizeof(n->score));
    memcpy(n->pick, sPick, sizeof(n->pick));
    memcpy(n->gain, sGain, sizeof(n->gain));
    n->winner = sWinner;
    n->winnerScore = sWinnerScore;
    n->q = sQ;
}

/* the host: the state to the guests when it changed (the clock alone: every half second) */
static void netSend(void) {
    static QuizNet sSent;
    static int sSentAt;
    QuizNet now;
    if (portStateSyncPartyRole() != 1) {
        return;
    }
    netFill(&now);
    sSentAt++;
    if (now.state == sSent.state && now.number == sSent.number && now.joined == sSent.joined &&
        !memcmp(now.pick, sSent.pick, sizeof(now.pick)) && !memcmp(now.score, sSent.score, sizeof(now.score)) &&
        sSentAt < 30) {
        return;
    }
    portStateSyncPartyState(PORT_PARTY_QUIZ, &now, sizeof(now));
    sSent = now;
    sSentAt = 0;
}

static void disp(CameraId camId, void* param);

/* an icon the quiz could have shown: some item's (another number from the host isn't drawn) */
static short iconChecked(short icon) {
    int id;
    if (icon < 0) {
        return -1;
    }
    for (id = 1; id < ITEM_TYPE_MAX; id++) {
        if (itemDataTable[id].icon_id == icon) {
            return icon;
        }
    }
    return -1;
}

/* the host's state as received, every index and string kept in range before it is used here */
static void netCheck(QuizNet* n) {
    int p, k;
    if (n->state < S_OFF || n->state > S_RESULTS) {
        n->state = S_OFF;
    }
    if (n->number < 0 || n->number > QUESTIONS) {
        n->number = n->number < 0 ? 0 : QUESTIONS;
    }
    n->joined &= (1u << PLAYERS) - 1u;
    for (p = 0; p < PLAYERS; p++) {
        if (n->pick[p] < -1 || n->pick[p] > 3) {
            n->pick[p] = -1;
        }
    }
    if (n->winner < -1 || n->winner > PLAYERS) {
        n->winner = -1;
    }
    if (n->q.right < 0 || n->q.right > 3) {
        n->q.right = 0;
    }
    n->q.icon = iconChecked(n->q.icon);
    n->q.text[sizeof(n->q.text) - 1] = '\0';
    for (k = 0; k < 4; k++) {
        n->q.option[k][sizeof(n->q.option[k]) - 1] = '\0';
    }
}

/* a guest: the host's quiz shown here, this player's presses sent */
static void guestFrame(void) {
    static unsigned sSeen;
    QuizNet n;
    unsigned version = 0;
    const int was = sState;
    if (portStateSyncPartyStateGet(PORT_PARTY_QUIZ, &n, sizeof(n), &version) == (int)sizeof(n) && version != sSeen) {
        sSeen = version;
        netCheck(&n);
        if (n.number != sNumber && n.state == S_ASK) {
            psndSFXOn("SFX_QUESTION_MARK1");
        }
        sState = n.state;
        sNumber = n.number;
        sClock = n.clock;
        sJoined = n.joined;
        memcpy(sScore, n.score, sizeof(sScore));
        memcpy(sPick, n.pick, sizeof(sPick));
        memcpy(sGain, n.gain, sizeof(sGain));
        if (n.winner != sWinner && n.winner > 0 && n.winner == portStateSyncMe() &&
            quizWon(n.joined, n.winner, n.winnerScore)) {
            portAchievementCounterMax("quiz_won", 1); /* (a guest's win: Quiz Whiz) */
        }
        sWinner = n.winner;
        sWinnerScore = n.winnerScore;
        sQ = n.q;
    }
    if (sState != S_OFF && (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle)) {
        return; /* (shown again once this game is back in the field) */
    }
    if (sState != S_OFF && !sOwnLevel) {
        marioStSystemLevel(1);
        statusWinForceClose();
        sOwnLevel = 1;
        printf("[quiz] the host's quiz shown here\n");
        fflush(stdout);
    } else if (sState == S_OFF && was != S_OFF) {
        if (sOwnLevel) {
            marioStSystemLevel(0);
            statusWinForceCloseClear();
            sOwnLevel = 0;
        }
        printf("[quiz] the host's quiz is over\n");
        fflush(stdout);
    }
    if (sState != S_OFF) {
        portStateSyncPartyPress(PORT_PARTY_QUIZ, portPartyPressed(0));
        dispEntry(CAMERA_2D, 904.0f, 1, disp, NULL);
    }
}

/* --- questions --------------------------------------------------------------- */

static int itemOk(int id) {
    return id > 0 && id < ITEM_TYPE_MAX && itemDataTable[id].name != NULL && itemDataTable[id].type_sort_order != -1 &&
           msgSearch(itemDataTable[id].name) != NULL;
}

static const char* itemName(int id) {
    const char* t = msgSearch(itemDataTable[id].name);
    return t != NULL ? t : "?";
}

/* a field item (no coins, hearts or Star Pieces) or a badge, as asked */
static int pickItem(int badge, int (*want)(int id)) {
    int tries;
    for (tries = 0; tries < 400; tries++) {
        const int id = badge ? BADGE_MIN + portPartyBelow(&sRand, BADGE_COUNT)
                             : ITEM_THUNDER_BOLT + portPartyBelow(&sRand, ITEM_MAX - ITEM_THUNDER_BOLT);
        if (itemOk(id) && (want == NULL || want(id))) {
            return id;
        }
    }
    return 0;
}

static int wantsHp(int id) {
    return itemDataTable[id].hp_restored > 0;
}
static int wantsFp(int id) {
    return itemDataTable[id].fp_restored > 0;
}
/* an item every shop pays the same for: the item table's sell price, which
 * a few shops replace with their own for some items (kQuizShopTrades) */
static int wantsSell(int id) {
    int k;
    if (itemDataTable[id].sell_price == 0) {
        return 0;
    }
    for (k = 0; k < (int)(sizeof(kQuizShopTrades) / sizeof(kQuizShopTrades[0])); k++) {
        if (kQuizShopTrades[k].item == id && kQuizShopTrades[k].coins != itemDataTable[id].sell_price) {
            return 0;
        }
    }
    return 1;
}

/* what the item shops sell it for, cheapest and dearest (0: no shop sells it) */
static int shopPrice(int id, int* most) {
    int k, least = 0;
    *most = 0;
    for (k = 0; k < (int)(sizeof(kQuizShopGoods) / sizeof(kQuizShopGoods[0])); k++) {
        if (kQuizShopGoods[k].item == id) {
            const int c = kQuizShopGoods[k].coins;
            least = least == 0 || c < least ? c : least;
            *most = c > *most ? c : *most;
        }
    }
    return least;
}
static int wantsBuy(int id) {
    int most;
    return shopPrice(id, &most) > 0;
}

static void shuffleIn(int right, const char* rightText, const char (*others)[96]) {
    int k, o = 0;
    sQ.right = right;
    for (k = 0; k < 4; k++) {
        snprintf(sQ.option[k], sizeof(sQ.option[k]), "%s", k == right ? rightText : others[o++]);
    }
}

/* the right number and three others near it (step apart, from lo up) */
static void numbers(int value, int lo, int step) {
    char others[3][96];
    int cand[24], n = 0, i, k;
    for (i = -8; i <= 8 && n < 24; i++) {
        const int v = value + i * step;
        if (i != 0 && v >= lo) {
            cand[n++] = v;
        }
    }
    for (k = 0; k < 3; k++) { /* the nearest are likelier: from a window that widens */
        const int window = n < 6 ? n : 6;
        const int j = portPartyBelow(&sRand, window);
        snprintf(others[k], sizeof(others[k]), "%d", cand[j]);
        cand[j] = cand[--n];
    }
    {
        char right[16];
        snprintf(right, sizeof(right), "%d", value);
        shuffleIn(portPartyBelow(&sRand, 4), right, (const char(*)[96])others);
    }
}

/* four listed enemies with different names and numbers; the one with the most (field 0 hp, 1 level) first */
static int pickEnemies(int* out, int count) {
    int tries, n = 0;
    for (tries = 0; tries < 2000 && n < count; tries++) {
        const int u = 1 + portPartyBelow(&sRand, PORT_PARTY_UNITS - 1);
        int k, clash = 0;
        if (!portPartyEnemyListed(u) || portPartyEnemyName(u)[0] == '\0') {
            continue;
        }
        for (k = 0; k < n; k++) {
            if (strcmp(portPartyEnemyName(out[k]), portPartyEnemyName(u)) == 0 ||
                portPartyStats(out[k])->hp == portPartyStats(u)->hp) {
                clash = 1;
            }
        }
        if (!clash) {
            out[n++] = u;
        }
    }
    return n == count;
}

static int makeQuestion(void) {
    const int kind = portPartyBelow(&sRand, 9);
    char others[3][96];
    int id, k;
    memset(&sQ, 0, sizeof(sQ));
    sQ.icon = -1;
    switch (kind) {
    case 0:
    case 1: { /* what an item restores */
        const int hp = kind == 0;
        id = pickItem(0, hp ? wantsHp : wantsFp);
        if (id == 0) {
            return 0;
        }
        snprintf(sQ.text, sizeof(sQ.text), portTrGame(hp ? "How much HP does %s restore?" : "How much FP does %s restore?"),
                 itemName(id));
        numbers(hp ? itemDataTable[id].hp_restored : itemDataTable[id].fp_restored, 1,
                (hp ? itemDataTable[id].hp_restored : itemDataTable[id].fp_restored) >= 10 ? 5 : 1);
        sQ.icon = (short)itemDataTable[id].icon_id;
        return 1;
    }
    case 2: /* a badge's BP */
        id = pickItem(1, NULL);
        if (id == 0) {
            return 0;
        }
        snprintf(sQ.text, sizeof(sQ.text), portTrGame("How many BP does %s cost to wear?"), itemName(id));
        numbers(itemDataTable[id].bp_cost, 0, 1);
        sQ.icon = (short)itemDataTable[id].icon_id;
        return 1;
    case 3: /* what an item sells for (shops buy items, not badges) */
        id = pickItem(0, wantsSell);
        if (id == 0) {
            return 0;
        }
        snprintf(sQ.text, sizeof(sQ.text), portTrGame("How many coins does a shop give for %s?"), itemName(id));
        numbers(itemDataTable[id].sell_price, 1, itemDataTable[id].sell_price >= 20 ? 5 : 1);
        sQ.icon = (short)itemDataTable[id].icon_id;
        return 1;
    case 4: { /* the dearest of four, of what the shops sell: dearer than the others
               * in every shop (a few sell for different prices in different shops) */
        int ids[4], least[4], most[4], best = 0, n = 0, tries;
        for (tries = 0; tries < 200 && n < 4; tries++) {
            const int c = pickItem(0, wantsBuy);
            int j, hi, lo, clash = c == 0;
            for (j = 0; j < n && !clash; j++) {
                clash = ids[j] == c || strcmp(itemName(ids[j]), itemName(c)) == 0;
            }
            if (clash) {
                continue;
            }
            lo = shopPrice(c, &hi);
            ids[n] = c;
            least[n] = lo;
            most[n++] = hi;
        }
        if (n < 4) {
            return 0;
        }
        for (k = 1; k < 4; k++) {
            if (least[k] > least[best]) {
                best = k;
            }
        }
        for (k = 0; k < 4; k++) {
            if (k != best && most[k] >= least[best]) {
                return 0; /* no clear answer: another four */
            }
        }
        snprintf(sQ.text, sizeof(sQ.text), "%s", portTrGame("Which of these costs the most in a shop?"));
        for (k = 0; k < 4; k++) {
            snprintf(sQ.option[k], sizeof(sQ.option[k]), "%s", itemName(ids[k]));
        }
        sQ.right = best;
        return 1;
    }
    case 5: { /* the toughest of four enemies */
        int u[4], best = 0;
        if (!pickEnemies(u, 4)) {
            return 0;
        }
        for (k = 1; k < 4; k++) {
            if (portPartyStats(u[k])->hp > portPartyStats(u[best])->hp) {
                best = k;
            }
        }
        snprintf(sQ.text, sizeof(sQ.text), "%s", portTrGame("Which of these enemies has the most HP?"));
        for (k = 0; k < 4; k++) {
            snprintf(sQ.option[k], sizeof(sQ.option[k]), "%s", portPartyEnemyName(u[k]));
        }
        sQ.right = best;
        return 1;
    }
    case 6:
    case 7: { /* an enemy's Defense or Attack */
        int u[1];
        const int def = kind == 6;
        if (!pickEnemies(u, 1) || (!def && portPartyStats(u[0])->atk <= 0)) {
            return 0;
        }
        snprintf(sQ.text, sizeof(sQ.text),
                 portTrGame(def ? "What is the Defense of %s?" : "How much damage does the strongest plain attack of %s do?"),
                 portPartyEnemyName(u[0]));
        numbers(def ? portPartyStats(u[0])->def : portPartyStats(u[0])->atk, def ? 0 : 1, 1); /* an attack does 1 at least */
        return 1;
    }
    default: { /* the badge among items */
        const int badge = pickItem(1, NULL);
        if (badge == 0) {
            return 0;
        }
        for (k = 0; k < 3; k++) {
            const int it = pickItem(0, NULL);
            int j;
            if (it == 0) {
                return 0;
            }
            for (j = 0; j < k; j++) {
                if (strcmp(others[j], itemName(it)) == 0) {
                    return 0; /* the same item twice: another question */
                }
            }
            snprintf(others[k], sizeof(others[k]), "%s", itemName(it));
        }
        snprintf(sQ.text, sizeof(sQ.text), "%s", portTrGame("Which of these is a badge?"));
        shuffleIn(portPartyBelow(&sRand, 4), itemName(badge), (const char(*)[96])others);
        return 1;
    }
    }
}

static void nextQuestion(void) {
    int tries, p;
    for (tries = 0; tries < 50 && !makeQuestion(); tries++) {
    }
    sNumber++;
    /* the log (tests): the question, its answers, the right one */
    printf("[quiz] %d: %s | A %s | B %s | X %s | Y %s | right %c\n", sNumber, sQ.text, sQ.option[0], sQ.option[1],
           sQ.option[2], sQ.option[3], kLetters[sQ.right]);
    fflush(stdout);
    sClock = 0;
    for (p = 0; p < PLAYERS; p++) {
        sPick[p] = -1;
        sGain[p] = 0;
    }
    sState = S_ASK;
    psndSFXOn("SFX_QUESTION_MARK1");
}

static void end(void) {
    if (sOwnLevel) {
        marioStSystemLevel(0);
        statusWinForceCloseClear(); /* the HUD back as the field has it */
        sOwnLevel = 0;
    }
    sState = S_OFF;
    printf("[quiz] over\n");
    fflush(stdout);
    netSend(); /* (online: the guests' games close it too) */
}

static void finish(void) {
    int p, best = -1, bestScore = -1, draw = 0, players = 0, won;
    for (p = 0; p < PLAYERS; p++) {
        if (!(sJoined >> p & 1u)) {
            continue;
        }
        players++;
        if (sScore[p] > bestScore) {
            best = p;
            bestScore = sScore[p];
            draw = 0;
        } else if (sScore[p] == bestScore) {
            draw = 1;
        }
    }
    sWinner = draw ? 0 : best + 1;
    sWinnerScore = bestScore;
    won = quizWon(sJoined, sWinner, bestScore);
    if (won && (!portStateSyncLinked() || sWinner == portStateSyncMe())) {
        portAchievementCounterMax("quiz_won", 1); /* (this game's player, or one at this computer: Quiz Whiz) */
    }
    /* the log (tests): the result, each player's points */
    printf("[quiz] result: %d player%s,", players, players == 1 ? "" : "s");
    for (p = 0; p < PLAYERS; p++) {
        if (sJoined >> p & 1u) {
            printf(" P%d %d", p + 1, sScore[p]);
        }
    }
    if (won) {
        printf("; player %d wins\n", sWinner);
    } else {
        printf("%s\n", players < 2 ? "; solo: no one to beat" : "; a draw");
    }
    fflush(stdout);
    sState = S_RESULTS;
    psndSFXOn("SFX_GOOD_SOUND2");
}

/* --- drawing --------------------------------------------------------------- */

#define WIN_X (-290.0f)
#define WIN_Y 200.0f
#define WIN_W 580.0f
#define WIN_H 400.0f

static void disp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    char line[200];
    int p, k;
    float y;
    (void)camId;
    (void)param;
    if (sState == S_OFF) {
        return; /* (queued before a save state or rewind put this frame back, and since closed) */
    }
    portPartyWindow(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (sState == S_JOIN) {
        portPartyText(WIN_X + 30.0f, WIN_Y - 30.0f, 1.0f, 0xFFE050FFu, 0.0f, portTrGame("Quiz party!"));
        portPartyText(WIN_X + 30.0f, WIN_Y - 80.0f, 0.7f, 0xFFFFFFFFu, WIN_W - 60.0f,
                      portTrGame("Ten questions about the game. Each player presses {A} on their own controller to "
                                 "join; then {START} begins."));
        /* online (each player in their own game): the players in the room, each told which one they are;
           else the controllers at this computer */
        const int online = portStateSyncPartyRole() != 0;
        const int mine = online ? portStateSyncMe() - 1 : -1;
        if (mine >= 0) {
            snprintf(line, sizeof(line), portTrGame("You are player %d."), mine + 1);
            portPartyText(WIN_X + 30.0f, WIN_Y - 150.0f, 0.7f, 0xFFE050FFu, WIN_W - 60.0f, line);
        }
        for (p = 0; p < PLAYERS; p++) {
            const int in = sJoined >> p & 1u;
            const int there = online ? portStateSyncPlayerHere(p + 1) : (int)(portPartyConnected() >> p & 1u);
            snprintf(line, sizeof(line), portTrGame(in ? "Player %d: in!" : there ? "Player %d: press {A}" : "Player %d: -"),
                     p + 1);
            portPartyText(WIN_X + 60.0f + (p % 2) * 250.0f, WIN_Y - 190.0f - (p / 2) * 40.0f, 0.75f,
                          in ? 0x90FF90FFu : p == mine ? 0xFFE050FFu : 0xB0B8D0FFu, 0.0f, line);
        }
        portPartyText(WIN_X + 30.0f, WIN_Y - WIN_H + 40.0f, 0.55f, 0xB0B8D0FFu, WIN_W - 60.0f,
                      portTrGame("{A}: join   {START}: begin   {B} on the first controller: leave"));
        portRandSetState(seed);
        return;
    }
    if (sState == S_RESULTS) {
        int order[PLAYERS], n = 0, i, j;
        portPartyText(WIN_X + 30.0f, WIN_Y - 30.0f, 1.0f, 0xFFE050FFu, 0.0f, portTrGame("The results"));
        for (p = 0; p < PLAYERS; p++) {
            if (sJoined >> p & 1u) {
                order[n++] = p;
            }
        }
        for (i = 0; i < n; i++) {
            for (j = i + 1; j < n; j++) {
                if (sScore[order[j]] > sScore[order[i]]) {
                    const int t = order[i];
                    order[i] = order[j];
                    order[j] = t;
                }
            }
        }
        for (i = 0; i < n; i++) {
            snprintf(line, sizeof(line), portTrGame("Player %d: %d points"), order[i] + 1, sScore[order[i]]);
            portPartyText(WIN_X + 60.0f, WIN_Y - 100.0f - i * 40.0f, 0.85f, i == 0 ? 0xFFE050FFu : 0xFFFFFFFFu, 0.0f,
                          line);
        }
        if (sWinner == 0) {
            snprintf(line, sizeof(line), "%s", portTrGame("A draw!"));
        } else {
            snprintf(line, sizeof(line), portTrGame("Player %d wins!"), sWinner);
        }
        portPartyText(WIN_X + 30.0f, WIN_Y - 290.0f, 0.9f, 0x90FF90FFu, 0.0f, line);
        portPartyText(WIN_X + 30.0f, WIN_Y - WIN_H + 40.0f, 0.55f, 0xB0B8D0FFu, WIN_W - 60.0f,
                      portTrGame("{A}: play again   {B}: leave"));
        portRandSetState(seed);
        return;
    }
    snprintf(line, sizeof(line), portTrGame("Question %d of %d"), sNumber, QUESTIONS);
    portPartyText(WIN_X + 30.0f, WIN_Y - 26.0f, 0.65f, 0xFFE050FFu, 0.0f, line);
    if (sState == S_ASK) {
        snprintf(line, sizeof(line), "%d", (ANSWER_FRAMES - sClock + 59) / 60);
        portPartyText(WIN_X + WIN_W - 60.0f, WIN_Y - 26.0f, 0.8f, sClock > ANSWER_FRAMES - 180 ? 0xFF8060FFu : 0xFFFFFFFFu,
                      0.0f, line);
    }
    y = WIN_Y - 66.0f;
    y -= 24.0f * 0.8f * (float)portPartyText(WIN_X + 30.0f, y, 0.8f, 0xFFFFFFFFu, WIN_W - (sQ.icon >= 0 ? 130.0f : 60.0f),
                                               sQ.text);
    if (sQ.icon >= 0) {
        winIconInit(); /* the icon's vertex set-up: the text above left the font's */
        winIconSet(sQ.icon, (Vec){WIN_X + WIN_W - 60.0f, WIN_Y - 90.0f, 0.0f}, (Vec){1.2f, 1.2f, 1.2f},
                   (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    }
    y -= 20.0f;
    for (k = 0; k < 4; k++) {
        static const unsigned kLetterColour[4] = {0x40E070FFu, 0xFF5050FFu, 0xD0D0D0FFu, 0xD0D0D0FFu};
        const int revealed = sState == S_REVEAL;
        const unsigned c = !revealed ? 0xFFFFFFFFu : k == sQ.right ? 0x90FF90FFu : 0x8088A0FFu;
        portPartyText(WIN_X + 50.0f, y, 0.8f, revealed && k != sQ.right ? 0x8088A0FFu : kLetterColour[k], 0.0f,
                      kTokens[k]);
        /* (an answer that wraps pushes the next one down, not into it) */
        const int lines = portPartyText(WIN_X + 90.0f, y, 0.75f, c, WIN_W - 130.0f, sQ.option[k]);
        y -= 36.0f + (float)(lines > 1 ? lines - 1 : 0) * 24.0f * 0.75f;
    }
    for (p = 0; p < PLAYERS; p++) {
        unsigned c = 0xB0B8D0FFu;
        if (!(sJoined >> p & 1u)) {
            continue;
        }
        if (sState == S_ASK) {
            snprintf(line, sizeof(line), "P%d %d%s", p + 1, sScore[p], sPick[p] >= 0 ? " !" : "");
            c = sPick[p] >= 0 ? 0xFFFFFFFFu : 0xB0B8D0FFu;
        } else {
            snprintf(line, sizeof(line), "P%d %s %+d", p + 1, sPick[p] >= 0 ? kTokens[sPick[p]] : "-", sGain[p]);
            c = sGain[p] > 0 ? 0x90FF90FFu : 0xFF8080FFu;
        }
        portPartyText(WIN_X + 30.0f + p * 135.0f, WIN_Y - WIN_H + 64.0f, 0.65f, c, 0.0f, line);
    }
    portPartyTextAbove(WIN_X + 30.0f, WIN_Y - WIN_H + 34.0f, 0.5f, 0xB0B8D0FFu, WIN_W - 60.0f,
                  portTrGame("Answer with {A}, {B}, {X} or {Y}. {Z} on the first controller ends the quiz."));
    portRandSetState(seed);
}

/* --- the game's frame ----------------------------------------------------------- */

void portQuizFrame(void) {
    int p;
    static int sTestAsked;
    if (!sTestAsked && gp != NULL && seqGetSeq() == SEQ_GAME && getenv("TTYD_PARTY_GAME_TEST") != NULL &&
        strcmp(getenv("TTYD_PARTY_GAME_TEST"), "quiz") == 0) {
        /* tests: a quiz party once a game runs (docs/PC_PORT.md) */
        const char* why = "";
        sTestAsked = 1;
        if (!portQuizStart(&why)) {
            printf("[quiz] test start refused: %s\n", why);
        }
    }
    if (portStateSyncPartyRole() == 2) {
        guestFrame(); /* (online: the host's quiz, shown here) */
        return;
    }
    readPresses();
    if (sState == S_OFF) {
        if (!sAsked) {
            return;
        }
        if (portReplayActive() || seqGetSeq() != SEQ_GAME) {
            sAsked = 0;
            return;
        }
        if (!portPartyFieldReady()) {
            return; /* waits for the menu to close, Mario to be free */
        }
        sAsked = 0;
        /* (online, each in their own game: the host runs the quiz and sends what it shows) */
        sRand.s = (unsigned long long)time(NULL) ^ ((unsigned long long)SDL_GetTicks() << 20);
        sJoined = 0;
        sNumber = 0;
        memset(sScore, 0, sizeof(sScore));
        marioStSystemLevel(1); /* the field stands still */
        statusWinForceClose(); /* the HUD out of the window's way */
        sOwnLevel = 1;
        sState = S_JOIN;
        psndSFXOn("SFX_QUIZ_START1");
        printf("[quiz] up\n");
        fflush(stdout);
    }
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        end(); /* a map change or a battle came anyway */
        return;
    }
    if (pressedNow(0) & PAD_TRIGGER_Z) {
        psndSFXOn((const char*)0x20013);
        end();
        return;
    }
    if (sState == S_JOIN || sState == S_RESULTS) {
        /* joining and the results: B backs out on every controller (port/include/port/menu_buttons.h); the
         * questions keep the game's A, B, X and Y as their answers, as drawn */
        portMenuMark(0xF);
    }
    switch (sState) {
    case S_JOIN:
        for (p = 0; p < PLAYERS; p++) {
            const unsigned short b = pressedNow(p);
            if ((b & PAD_BUTTON_A) && !(sJoined >> p & 1u)) {
                sJoined |= 1u << p;
                psndSFXOn((const char*)0x20012);
            }
            if ((b & PAD_BUTTON_START) && sJoined != 0) {
                nextQuestion();
                break;
            }
        }
        if (sState == S_JOIN && (pressedNow(0) & PAD_BUTTON_B)) {
            psndSFXOn((const char*)0x20013);
            end();
        }
        break;
    case S_ASK: {
        int all = 1;
        sClock++;
        for (p = 0; p < PLAYERS; p++) {
            int k;
            if (!(sJoined >> p & 1u)) {
                continue;
            }
            if (sPick[p] < 0) {
                for (k = 0; k < 4; k++) {
                    if (pressedNow(p) & kButtons[k]) {
                        sPick[p] = k;
                        sPickedAt[p] = sClock;
                        psndSFXOn((const char*)0x20005);
                        break;
                    }
                }
            }
            all &= sPick[p] >= 0;
        }
        if (all || sClock >= ANSWER_FRAMES) {
            int anyRight = 0;
            for (p = 0; p < PLAYERS; p++) {
                if ((sJoined >> p & 1u) && sPick[p] == sQ.right) {
                    sGain[p] = 100 + (ANSWER_FRAMES - sPickedAt[p]) * 50 / ANSWER_FRAMES;
                    sScore[p] += sGain[p];
                    anyRight = 1;
                }
            }
            psndSFXOn(anyRight ? "SFX_GOOD_SOUND1" : "SFX_BAD_SOUND1");
            sState = S_REVEAL;
            sClock = 0;
        }
        break;
    }
    case S_REVEAL:
        if (++sClock >= REVEAL_FRAMES || (sClock > 30 && (pressedNow(0) & PAD_BUTTON_START))) {
            if (sNumber >= QUESTIONS) {
                finish();
            } else {
                nextQuestion();
            }
        }
        break;
    case S_RESULTS:
        if (pressedNow(0) & PAD_BUTTON_A) {
            memset(sScore, 0, sizeof(sScore));
            sNumber = 0;
            nextQuestion();
        } else if (pressedNow(0) & PAD_BUTTON_B) {
            psndSFXOn((const char*)0x20013);
            end();
        }
        break;
    }
    if (sState != S_OFF) {
        dispEntry(CAMERA_2D, 904.0f, 1, disp, NULL);
    }
    netSend(); /* (online: the guests' games show it) */
}
