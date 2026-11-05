/* Tattle cards (port/include/port/party.h, docs/MODES.md): every enemy in
 * the Tattle Log is a card with its own numbers (HP, its strongest plain
 * attack and its Defense, from the decomp's battle units: port/src/common/
 * tattle_cards_gen.c; the strongest brought down to the top of the scale,
 * portCardStats) and a cost of 1 to 5 stars from them. A deck is up to
 * 8 cards and 20 stars, built on the Modes page from the enemies this file
 * has tattled (or made from them when none is built).
 *
 * A duel is best of five: each round both players play a card from their
 * hand at once, unseen, and the two fight it out: each hit does the
 * attacker's Attack less the other's Defense (1 at least), the cheaper card
 * strikes first (the same cost: both at once), and the first to fall loses
 * the round. Three rounds win the duel.
 *
 * Opponents: the card players at Rogueport's Westside (gor_02), three of
 * them, each harder (their decks are drawn from the whole Tattle Log, more
 * stars each), a few coins for a win; a second controller on this screen;
 * or online the other player. Against a player both draft five cards in
 * turn from the same twelve (the file's own tattles), so no one's deck has
 * to cross the network: the draft and every pick come from the controllers
 * through lockstep, the pool from the game's frame clock, and the duel
 * plays out the same in both games. Only the drawing is each player's own
 * (their hand at the front). Online, the card players duel the host (its
 * controller, its deck: the session's, port/include/port/online_session.h);
 * the other games watch it.
 *
 * The field stands still while a duel is up (the system level the game's
 * item scenes use) and the game gets no input; the duel's random numbers are
 * our own. */
#include "port/party.h"
#include "port/online.h"
#include "port/online_session.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/seqdrv.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "statuswindow.h"
#include "pmario_sound.h"
#include <dolphin/os.h>
#include <dolphin/pad.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROUNDS_TO_WIN 3
#define POOL 12
#define STEP_FRAMES 26
#define MAX_CARDS PORT_CARDS_DECK

enum { D_OFF, D_DRAFT, D_PICK, D_CLASH, D_OVER };

typedef struct Side {
    int cards[MAX_CARDS];
    int count;
    unsigned used;
    int cursor, pick, score;
    int pad; /* the controller playing it, -1 the computer */
} Side;

static int sState, sAsk, sOpponent, sOwnLevel, sClock;
static Side sSide[2];
static PortPartyRand sRand;
static int sPool[POOL], sPoolCount, sPoolTaken, sPoolCursor, sDraftTurn;
/* the clash: the hit points after each exchange */
static short sStepHp[64][2];
static int sSteps, sRoundWinner, sRound, sWinner, sPrize;
static int sDeck[MAX_CARDS], sDeckCount = -1;
static int sWins[PORT_DUEL_NPCS];
static int sRead;

static const char* const kOpponents[] = {"Westside rookie", "Westside regular", "Westside champion"};

const char* portCardsOpponent(int opponent) {
    if (opponent >= 0 && opponent < PORT_DUEL_NPCS) {
        return kOpponents[opponent];
    }
    return portOnlineActive() ? "The other player" : "Second player";
}

/* --- the cards ---------------------------------------------------------------- */

static int power(const PortCardStats* s) {
    return s->hp + 3 * s->atk + 4 * s->def;
}

/* The most a card's numbers weigh (power): the top of the five-star band.
 * The strongest kinds (the bosses: the Shadow Queen's 150 HP, Bonetail's
 * 200; an Amazy Dayzee's 20 Attack) are brought down to it, so a five-star
 * card is one of a kind but no card wins every round: their HP first (to a
 * third of it at least), so they keep their Attack, then if need be their
 * Attack and Defense alike. Whole numbers only: an online duel plays out
 * the same. */
#define CARD_POWER_MAX 60

/* A card's numbers as it plays (and shows) them */
PortCardStats portCardStats(int unit) {
    PortCardStats c = *portPartyStats(unit);
    if (power(&c) > CARD_POWER_MAX) {
        const int hits = 3 * c.atk + 4 * c.def;
        int hp = CARD_POWER_MAX - hits;
        hp = hp < CARD_POWER_MAX / 3 ? CARD_POWER_MAX / 3 : hp;
        c.hp = (short)(hp < c.hp ? hp : c.hp);
        if (c.hp + hits > CARD_POWER_MAX) {
            const int room = CARD_POWER_MAX - c.hp;
            c.atk = (short)(c.atk * room / hits);
            c.def = (short)(c.def * room / hits);
            c.atk = c.atk < 1 ? 1 : c.atk;
        }
    }
    return c;
}

static int cardPower(int unit) {
    const PortCardStats c = portCardStats(unit);
    return power(&c);
}

int portCardStars(int unit) {
    const PortCardStats* s;
    int p;
    if (!portPartyEnemyListed(unit)) {
        return 0;
    }
    s = portPartyStats(unit);
    if (s->hp <= 0) {
        return 0;
    }
    p = cardPower(unit);
    return p < 10 ? 1 : p < 18 ? 2 : p < 30 ? 3 : p < 50 ? 4 : 5;
}

int portCardsOwned(int unit) {
    return portCardStars(unit) > 0 && portPartyEnemyTattled(unit);
}

static void path(char* out, size_t size) {
    snprintf(out, size, "%s/cards.txt", portSettingsUserPath());
}

static void save(void) {
    char p[1024];
    FILE* f;
    int i;
    path(p, sizeof(p));
    f = fopen(p, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "deck");
    for (i = 0; i < sDeckCount; i++) {
        fprintf(f, " %d", sDeck[i]);
    }
    fprintf(f, "\nwins %d %d %d\n", sWins[0], sWins[1], sWins[2]);
    fclose(f);
}

static void readSaved(void) {
    char p[1024], line[256];
    FILE* f;
    if (sRead) {
        return;
    }
    sRead = 1;
    sDeckCount = 0;
    path(p, sizeof(p));
    f = fopen(p, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "deck", 4) == 0) {
            const char* q = line + 4;
            int n, at;
            while (sDeckCount < MAX_CARDS && sscanf(q, "%d%n", &n, &at) == 1) {
                sDeck[sDeckCount++] = n;
                q += at;
            }
        } else if (strncmp(line, "wins", 4) == 0) {
            sscanf(line + 4, "%d %d %d", &sWins[0], &sWins[1], &sWins[2]);
        }
    }
    fclose(f);
}

int portCardsBuilt(int* units) {
    readSaved();
    memcpy(units, sDeck, sizeof(int) * (size_t)sDeckCount);
    return sDeckCount;
}

void portCardsSetDeck(const int* units, int n) {
    readSaved();
    sDeckCount = n < 0 ? 0 : n > MAX_CARDS ? MAX_CARDS : n;
    memcpy(sDeck, units, sizeof(int) * (size_t)sDeckCount);
    save();
}

int portCardsWins(int opponent) {
    readSaved();
    return opponent >= 0 && opponent < PORT_DUEL_NPCS ? sWins[opponent] : 0;
}

/* the strongest cards that fit, then the cheapest to fill it */
static int autoDeck(int* out, int (*can)(int unit), int stars, int maxStars, PortPartyRand* shuffle) {
    int cand[PORT_PARTY_UNITS], n = 0, i, j, count = 0, used = 0;
    for (i = 1; i < PORT_PARTY_UNITS; i++) {
        if (can(i) && portCardStars(i) <= maxStars) {
            cand[n++] = i;
        }
    }
    if (shuffle != NULL) {
        for (i = n - 1; i > 0; i--) {
            const int k = portPartyBelow(shuffle, i + 1);
            const int t = cand[i];
            cand[i] = cand[k];
            cand[k] = t;
        }
    } else {
        for (i = 0; i < n; i++) {
            for (j = i + 1; j < n; j++) {
                if (cardPower(cand[j]) > cardPower(cand[i])) {
                    const int t = cand[i];
                    cand[i] = cand[j];
                    cand[j] = t;
                }
            }
        }
    }
    for (i = 0; i < n && count < MAX_CARDS; i++) {
        const int s = portCardStars(cand[i]);
        int dup = 0;
        for (j = 0; j < count; j++) {
            dup |= strcmp(portPartyEnemyName(out[j]), portPartyEnemyName(cand[i])) == 0;
        }
        /* room left for the rest at one star each */
        if (!dup && used + s + (MAX_CARDS - count - 1) <= stars) {
            out[count++] = cand[i];
            used += s;
        }
    }
    return count;
}

static int deckValid(const int* units, int n) {
    int i, stars = 0;
    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!portCardsOwned(units[i])) {
            return 0;
        }
        stars += portCardStars(units[i]);
    }
    return stars <= PORT_CARDS_STARS;
}

/* online: the host's deck for the session (online_session.h), the same in every game of it */
static int sSessionDeck[MAX_CARDS], sSessionCount, sSessionOn;

int portCardsSessionGet(char* out, int size) {
    int i, n;
    readSaved();
    n = snprintf(out, (size_t)size, "deck");
    for (i = 0; i < sDeckCount && n >= 0 && n < size; i++) {
        n += snprintf(out + n, (size_t)(size - n), " %d", sDeck[i]);
    }
    return n >= 0 && n < size ? n : -1;
}

void portCardsSessionPut(const char* data, int len) {
    char text[256];
    const char* q = text + 4;
    int n, at;
    sSessionCount = 0;
    sSessionOn = data != NULL;
    if (data == NULL) {
        return;
    }
    len = len < 0 ? 0 : len > (int)sizeof(text) - 1 ? (int)sizeof(text) - 1 : len;
    memcpy(text, data, (size_t)len);
    text[len] = '\0';
    if (strncmp(text, "deck", 4) != 0) {
        return; /* none: one made from the file's Tattle Log, as for a deck not built */
    }
    while (sSessionCount < MAX_CARDS && sscanf(q, "%d%n", &n, &at) == 1) {
        sSessionDeck[sSessionCount++] = n;
        q += at;
    }
}

int portCardsDeck(int* units) {
    const int* deck = sDeck;
    int count;
    readSaved();
    count = sDeckCount;
    if (sSessionOn) {
        deck = sSessionDeck;
        count = sSessionCount;
    }
    if (deckValid(deck, count)) {
        memcpy(units, deck, sizeof(int) * (size_t)count);
        return count;
    }
    return autoDeck(units, portCardsOwned, PORT_CARDS_STARS, 5, NULL);
}

int portCardsAtTable(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && strcmp(gp->currentMapName, "gor_02") == 0;
}

int portCardsActive(void) {
    return sState != D_OFF;
}

int portCardsPad(void) {
    return sState != D_OFF;
}

int portCardsDuel(int opponent, const char** why) {
    int deck[MAX_CARDS];
    if (portReplayActive()) {
        *why = "Not during a replay.";
        return 0;
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME) {
        *why = "Start it in the field, with a file loaded.";
        return 0;
    }
    if (sState != D_OFF || portQuizActive()) {
        *why = "A party game is already up.";
        return 0;
    }
    if (opponent < PORT_DUEL_PLAYER) {
        /* (online: the host's controller plays the host's deck, portCardsSessionPut, in every game) */
        if (!portCardsAtTable()) {
            *why = "The card players sit at Rogueport's Westside.";
            return 0;
        }
        if (portCardsDeck(deck) < ROUNDS_TO_WIN * 2 - 1) {
            *why = "Tattle at least five enemies for a deck first.";
            return 0;
        }
    }
    sAsk = opponent + 1;
    return 1;
}

/* gor_02.c: a Westside card player's talk is over (args[0]: which one); the
 * duel begins once Mario is free, as from the menu, when one can now (online
 * too: the talk is the game's, the same frame in every game of the session) */
int portEvtCardsTalk(EventEntry* event, int isFirstCall) {
    const int* on = portSettingsFind("cards_talk");
    const char* why = NULL;
    (void)isFirstCall;
    if ((on == NULL || *on) && sAsk == 0) {
        const int opponent = evtGetValue(event, event->args[0]);
        if (opponent >= 0 && opponent < PORT_DUEL_NPCS && !portCardsDuel(opponent, &why)) {
            printf("[cards] no duel on talking: %s\n", why);
        }
    }
    return EVT_RETURN_DONE;
}

/* --- the duel ------------------------------------------------------------------ */

static int unused(const Side* s, int i) {
    return i >= 0 && i < s->count && !(s->used >> i & 1u);
}

static int fight(int a, int b, int record) {
    const PortCardStats ca = portCardStats(a), cb = portCardStats(b);
    const PortCardStats* sa = &ca;
    const PortCardStats* sb = &cb;
    const int starsA = portCardStars(a), starsB = portCardStars(b);
    int hpA = sa->hp, hpB = sb->hp;
    const int hitA = sa->atk - sb->def > 1 ? sa->atk - sb->def : 1; /* what A's hit does to B */
    const int hitB = sb->atk - sa->def > 1 ? sb->atk - sa->def : 1;
    int steps = 0;
    if (record) {
        sStepHp[0][0] = (short)hpA;
        sStepHp[0][1] = (short)hpB;
    }
    while (hpA > 0 && hpB > 0 && steps < 62) {
        if (starsA < starsB) {
            hpB -= hitA;
            if (hpB > 0) {
                hpA -= hitB;
            }
        } else if (starsB < starsA) {
            hpA -= hitB;
            if (hpA > 0) {
                hpB -= hitA;
            }
        } else {
            hpA -= hitB;
            hpB -= hitA;
        }
        steps++;
        if (record) {
            sStepHp[steps][0] = (short)(hpA > 0 ? hpA : 0);
            sStepHp[steps][1] = (short)(hpB > 0 ? hpB : 0);
        }
    }
    if (record) {
        sSteps = steps;
    }
    if (hpA <= 0 && hpB <= 0) {
        return -1;
    }
    if (hpA > 0 && hpB > 0) { /* (neither can fall: the tougher one stands) */
        return hpA * sb->hp >= hpB * sa->hp ? 0 : 1;
    }
    return hpA > 0 ? 0 : 1;
}

static int aiPick(void) {
    Side* me = &sSide[1];
    const Side* them = &sSide[0];
    int best = -1, bestScore = -1000, i, j;
    for (i = 0; i < me->count; i++) {
        int score = 0;
        if (!unused(me, i)) {
            continue;
        }
        if (sOpponent == PORT_DUEL_ROOKIE) {
            score = portPartyBelow(&sRand, 100);
        } else if (sOpponent == PORT_DUEL_REGULAR) {
            score = cardPower(me->cards[i]) + portPartyBelow(&sRand, 20);
        } else {
            for (j = 0; j < them->count; j++) { /* the card that beats most of what's left to the player */
                if (unused(them, j)) {
                    const int w = fight(me->cards[i], them->cards[j], 0);
                    score += w == 0 ? 10 : w < 0 ? 3 : 0;
                }
            }
            score = score * 4 - portCardStars(me->cards[i]) + portPartyBelow(&sRand, 4);
        }
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

static void firstFree(Side* s) {
    int i;
    for (i = 0; i < s->count; i++) {
        if (unused(s, i)) {
            s->cursor = i;
            return;
        }
    }
}

static void startRound(void) {
    int k;
    sRound++;
    for (k = 0; k < 2; k++) {
        sSide[k].pick = -1;
        if (!unused(&sSide[k], sSide[k].cursor)) {
            firstFree(&sSide[k]);
        }
    }
    sState = D_PICK;
    sClock = 0;
}

static void begin(int opponent) {
    int k;
    memset(sSide, 0, sizeof(sSide));
    sOpponent = opponent;
    sRound = 0;
    sWinner = -1;
    sPrize = 0;
    /* (the clock read without moving it: online the host's, in the same frame of every game) */
    sRand.s = (unsigned long long)portLockstepClock() ^ 0xC0FFEEull * (unsigned long long)(opponent + 1);
    sSide[0].pad = 0;
    sSide[1].pad = opponent == PORT_DUEL_PLAYER ? 1 : -1;
    if (opponent < PORT_DUEL_PLAYER) {
        static const int kStars[] = {14, 20, 26};
        static const int kMax[] = {2, 4, 5};
        sSide[0].count = portCardsDeck(sSide[0].cards);
        {
            PortPartyRand deal = sRand;
            sSide[1].count = autoDeck(sSide[1].cards, portPartyEnemyListed, kStars[opponent], kMax[opponent], &deal);
        }
        startRound();
    } else {
        /* the pool: the file's tattles first, then any enemy, the same in both games */
        int cand[PORT_PARTY_UNITS], n = 0, i, pass;
        sPoolCount = 0;
        for (pass = 0; pass < 2 && sPoolCount < POOL; pass++) {
            n = 0;
            for (i = 1; i < PORT_PARTY_UNITS; i++) {
                int j, dup = 0;
                if (portCardStars(i) == 0 || (pass == 0 && !portCardsOwned(i))) {
                    continue;
                }
                for (j = 0; j < sPoolCount; j++) {
                    dup |= sPool[j] == i || strcmp(portPartyEnemyName(sPool[j]), portPartyEnemyName(i)) == 0;
                }
                if (!dup) {
                    cand[n++] = i;
                }
            }
            while (n > 0 && sPoolCount < POOL) {
                const int j = portPartyBelow(&sRand, n);
                int d, dup = 0;
                for (d = 0; d < sPoolCount; d++) {
                    dup |= strcmp(portPartyEnemyName(sPool[d]), portPartyEnemyName(cand[j])) == 0;
                }
                if (!dup) {
                    sPool[sPoolCount++] = cand[j];
                }
                cand[j] = cand[--n];
            }
        }
        sPoolTaken = 0;
        sPoolCursor = 0;
        sDraftTurn = 0;
        sState = D_DRAFT;
    }
    for (k = 0; k < 2; k++) {
        sSide[k].pick = -1;
    }
    marioStSystemLevel(1);
    statusWinForceClose(); /* the HUD out of the duel's way */
    sOwnLevel = 1;
    psndSFXOn("SFX_QUIZ_START1");
    printf("[cards] duel against %s\n", portCardsOpponent(opponent));
    fflush(stdout);
}

static void end(void) {
    if (sOwnLevel) {
        marioStSystemLevel(0);
        statusWinForceCloseClear(); /* the HUD back as the field has it */
        sOwnLevel = 0;
    }
    sState = D_OFF;
}

/* the snake draft: 1 2 2 1 1 2 2 1 1 2 */
static int drafter(int taken) {
    static const unsigned char kOrder[10] = {0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
    return kOrder[taken % 10];
}

static void moveCursor(int* cursor, int count, unsigned short b, int columns, unsigned taken) {
    int step = 0, tries;
    if (b & PAD_BUTTON_UP) {
        step = -columns;
    } else if (b & PAD_BUTTON_DOWN) {
        step = columns;
    } else if (columns > 1 && (b & PAD_BUTTON_LEFT)) {
        step = -1;
    } else if (columns > 1 && (b & PAD_BUTTON_RIGHT)) {
        step = 1;
    }
    if (step == 0 || count <= 0) {
        return;
    }
    for (tries = 0; tries < count; tries++) {
        *cursor = (*cursor + step + count) % count;
        if (!(taken >> *cursor & 1u)) {
            break;
        }
    }
    psndSFXOn((const char*)0x20005);
}

/* --- drawing ------------------------------------------------------------------ */

#define WIN_X (-300.0f)
#define WIN_Y 212.0f
#define WIN_W 600.0f
#define WIN_H 424.0f

/* this screen's player: the guest's game puts the second side at the front
 * (a card player's duel online is the host's: the other games watch it) */
static int me(void) {
    return portOnlineActive() && !portOnlineIsHost() && sOpponent == PORT_DUEL_PLAYER ? 1 : 0;
}

/* online, a card player's duel in a game that isn't the host's: it watches */
static int watching(void) {
    return portOnlineActive() && !portOnlineIsHost() && sOpponent < PORT_DUEL_PLAYER;
}

static void stars(char* out, size_t size, int n) {
    int i;
    for (i = 0; i < n && (size_t)i + 1 < size; i++) {
        out[i] = '*';
    }
    out[i < (int)size ? i : (int)size - 1] = '\0';
}

static void cardLine(float x, float y, int unit, unsigned rgba, int hp) {
    const PortCardStats c = portCardStats(unit);
    const PortCardStats* s = &c;
    char line[96], st[8];
    stars(st, sizeof(st), portCardStars(unit));
    portPartyText(x, y, 0.6f, rgba, 250.0f, portPartyEnemyName(unit));
    if (hp == 0) {
        /* a clash's card that fell: said so, in red ("HP 0" read as a wrong number) */
        snprintf(line, sizeof(line), portTrGame("Knocked out  ATK %d  DEF %d  %s"), s->atk, s->def, st);
        portPartyText(x + 10.0f, y - 17.0f, 0.5f, 0xFF8060FFu, 0.0f, line);
        return;
    }
    snprintf(line, sizeof(line), portTrGame("HP %d  ATK %d  DEF %d  %s"), hp >= 0 ? hp : s->hp, s->atk, s->def, st);
    portPartyText(x + 10.0f, y - 17.0f, 0.5f, (rgba & 0xFFFFFF00u) == 0xFFFFFF00u ? 0xB0B8D0FFu : rgba, 0.0f, line);
}

static void handDisp(float x, float y, const Side* s, int showCursor) {
    int i;
    for (i = 0; i < s->count; i++) {
        unsigned c = unused(s, i) ? 0xFFFFFFFFu : 0x606880FFu;
        if (showCursor && i == s->cursor && unused(s, i)) {
            c = s->pick >= 0 ? 0x90FF90FFu : 0xFFE050FFu;
            portPartyText(x - 18.0f, y - i * 40.0f, 0.6f, c, 0.0f, ">");
        }
        cardLine(x, y - i * 40.0f, s->cards[i], c, -1);
    }
}

static const char* sideName(int k) {
    static char sName[2][48];
    if (sOpponent == PORT_DUEL_PLAYER) {
        if (portOnlineActive()) {
            snprintf(sName[k], sizeof(sName[k]), "%s", portTrGame(k == me() ? "You" : "The other player"));
        } else {
            snprintf(sName[k], sizeof(sName[k]), portTrGame("Player %d"), k + 1);
        }
    } else {
        snprintf(sName[k], sizeof(sName[k]), "%s",
                 portTrGame(k == 0 ? (watching() ? "The other player" : "You") : kOpponents[sOpponent]));
    }
    return sName[k];
}

static void disp(CameraId camId, void* param) {
    const u32 seed = portRandState();
    const int front = me(), back = 1 - front;
    const int bothHere = sOpponent == PORT_DUEL_PLAYER && !portOnlineActive();
    char line[160];
    int i;
    (void)camId;
    (void)param;
    portPartyWindow(WIN_X, WIN_Y, WIN_W, WIN_H);
    snprintf(line, sizeof(line), portTrGame("Tattle cards: %s"), portTrGame(portCardsOpponent(sOpponent)));
    portPartyText(WIN_X + 26.0f, WIN_Y - 26.0f, 0.75f, 0xFFE050FFu, 0.0f, line);
    if (sState != D_DRAFT) {
        snprintf(line, sizeof(line), "%s %d - %d %s", sideName(front), sSide[front].score, sSide[back].score,
                 sideName(back));
        portPartyText(WIN_X + 330.0f, WIN_Y - 28.0f, 0.6f, 0xFFFFFFFFu, 250.0f, line);
    }
    if (sState == D_DRAFT) {
        const int who = drafter(sPoolTaken);
        unsigned taken = 0;
        for (i = 0; i < sSide[0].count; i++) {
            int j;
            for (j = 0; j < sPoolCount; j++) {
                taken |= (unsigned)(sPool[j] == sSide[0].cards[i]) << j;
            }
        }
        for (i = 0; i < sSide[1].count; i++) {
            int j;
            for (j = 0; j < sPoolCount; j++) {
                taken |= (unsigned)(sPool[j] == sSide[1].cards[i]) << j;
            }
        }
        snprintf(line, sizeof(line), portTrGame("The draft: %s takes a card (%d of %d each)"), sideName(who),
                 sSide[who].count + 1, PORT_CARDS_HAND);
        portPartyText(WIN_X + 26.0f, WIN_Y - 56.0f, 0.6f, 0xFFFFFFFFu, WIN_W - 52.0f, line);
        for (i = 0; i < sPoolCount; i++) {
            const float x = WIN_X + 40.0f + (i % 2) * 280.0f;
            const float y = WIN_Y - 90.0f - (i / 2) * 44.0f;
            unsigned c = (taken >> i & 1u) ? 0x606880FFu : 0xFFFFFFFFu;
            if (i == sPoolCursor) {
                c = 0xFFE050FFu;
                portPartyText(x - 18.0f, y, 0.6f, c, 0.0f, ">");
            }
            cardLine(x, y, sPool[i], c, -1);
        }
    } else if (sState == D_PICK) {
        snprintf(line, sizeof(line), portTrGame("Round %d: play a card"), sRound);
        portPartyText(WIN_X + 26.0f, WIN_Y - 56.0f, 0.6f, 0xFFFFFFFFu, 0.0f, line);
        handDisp(WIN_X + 46.0f, WIN_Y - 90.0f, &sSide[front], 1);
        if (bothHere) {
            handDisp(WIN_X + 330.0f, WIN_Y - 90.0f, &sSide[back], 1);
        } else {
            int left = 0;
            for (i = 0; i < sSide[back].count; i++) {
                left += unused(&sSide[back], i);
            }
            snprintf(line, sizeof(line), portTrGame("%s: %d cards left"), sideName(back), left);
            portPartyText(WIN_X + 330.0f, WIN_Y - 90.0f, 0.6f, 0xFFFFFFFFu, 250.0f, line);
            portPartyText(WIN_X + 330.0f, WIN_Y - 120.0f, 0.6f, sSide[back].pick >= 0 ? 0x90FF90FFu : 0xB0B8D0FFu,
                          0.0f, portTrGame(sSide[back].pick >= 0 ? "Ready!" : "Choosing..."));
        }
    } else if (sState == D_CLASH || sState == D_OVER) {
        const int step = sClock / STEP_FRAMES < sSteps ? sClock / STEP_FRAMES : sSteps;
        if (sState == D_CLASH || sRoundWinner >= -1) {
            int k;
            for (k = 0; k < 2; k++) {
                const int side = k == 0 ? front : back;
                const Side* s = &sSide[side];
                const float x = WIN_X + 50.0f + k * 290.0f;
                if (s->pick < 0) {
                    continue;
                }
                portPartyText(x, WIN_Y - 70.0f, 0.55f, 0xB0B8D0FFu, 0.0f, sideName(side));
                cardLine(x, WIN_Y - 96.0f, s->cards[s->pick], 0xFFFFFFFFu, sStepHp[step][side]);
            }
            portPartyText(WIN_X + 280.0f, WIN_Y - 100.0f, 0.8f, 0xFF8060FFu, 0.0f, "VS");
        }
        if (sState == D_CLASH && step >= sSteps) {
            if (sRoundWinner < 0) {
                snprintf(line, sizeof(line), "%s", portTrGame("Both fall: no one takes the round."));
            } else {
                snprintf(line, sizeof(line), portTrGame("%s takes the round!"), sideName(sRoundWinner));
            }
            portPartyText(WIN_X + 40.0f, WIN_Y - 190.0f, 0.75f, 0x90FF90FFu, WIN_W - 80.0f, line);
        }
        if (sState == D_OVER) {
            if (sWinner < 0) {
                snprintf(line, sizeof(line), "%s", portTrGame("The duel is a draw."));
            } else if (sOpponent == PORT_DUEL_PLAYER && !portOnlineActive()) {
                snprintf(line, sizeof(line), portTrGame("Player %d wins the duel!"), sWinner + 1);
            } else if (watching()) {
                snprintf(line, sizeof(line), portTrGame("%s wins the duel!"), sideName(sWinner));
            } else {
                snprintf(line, sizeof(line), "%s", portTrGame(sWinner == front ? "You win the duel!" : "You lose the duel."));
            }
            portPartyText(WIN_X + 40.0f, WIN_Y - 230.0f, 0.9f, 0xFFE050FFu, WIN_W - 80.0f, line);
            if (sPrize > 0) {
                snprintf(line, sizeof(line), portTrGame("A prize of %d coins."), sPrize);
                portPartyText(WIN_X + 40.0f, WIN_Y - 270.0f, 0.65f, 0xFFFFFFFFu, 0.0f, line);
            }
        }
    }
    portPartyTextAbove(WIN_X + 26.0f, WIN_Y - WIN_H + 34.0f, 0.5f, 0xB0B8D0FFu, WIN_W - 52.0f,
                  portTrGame(sState == D_OVER ? "{A}: leave the table." :
                             "Up and down (left and right): a card. {A}: take it or play it. {Z} on the first controller: leave."));
    portRandSetState(seed);
}

/* --- the game's frame ------------------------------------------------------------ */

static void sideInput(int k) {
    Side* s = &sSide[k];
    unsigned short b;
    unsigned taken = s->used;
    if (s->pick >= 0) {
        return;
    }
    if (s->pad < 0) {
        if (sClock > 40 + k * 10) { /* the computer "thinks" a moment */
            s->pick = aiPick();
            s->cursor = s->pick;
        }
        return;
    }
    b = portPartyPressed(s->pad);
    moveCursor(&s->cursor, s->count, b, 1, taken);
    if ((b & PAD_BUTTON_A) && unused(s, s->cursor)) {
        s->pick = s->cursor;
        psndSFXOn((const char*)0x20012);
    }
}

void portCardsFrame(void) {
    int k;
    static int sTestAsked;
    const char* test = getenv("TTYD_PARTY_GAME_TEST");
    if (!sTestAsked && gp != NULL && seqGetSeq() == SEQ_GAME && test != NULL && strncmp(test, "duel", 4) == 0) {
        /* tests: duelN (0 rookie .. 2 champion, 3 the other player) once a game runs (docs/PC_PORT.md) */
        const char* why = "";
        sTestAsked = 1;
        if (!portCardsDuel(atoi(test + 4), &why)) {
            printf("[cards] test duel refused: %s\n", why);
        }
    }
    if (sState == D_OFF) {
        if (sAsk == 0) {
            return;
        }
        if (portReplayActive() || seqGetSeq() != SEQ_GAME ||
            (sAsk - 1 < PORT_DUEL_PLAYER && !portCardsAtTable())) {
            sAsk = 0;
            return;
        }
        if (!portPartyFieldReady()) {
            return; /* the menu closing, Mario free (the same frame in both games online) */
        }
        k = sAsk - 1;
        sAsk = 0;
        begin(k);
    }
    if (seqGetSeq() != SEQ_GAME || gp == NULL || gp->inBattle) {
        end();
        return;
    }
    if (portPartyPressed(0) & PAD_TRIGGER_Z) {
        psndSFXOn((const char*)0x20013);
        end();
        return;
    }
    sClock++;
    switch (sState) {
    case D_DRAFT: {
        const int who = drafter(sPoolTaken);
        const unsigned short b = portPartyPressed(sSide[who].pad);
        unsigned taken = 0;
        int i, j;
        for (k = 0; k < 2; k++) {
            for (i = 0; i < sSide[k].count; i++) {
                for (j = 0; j < sPoolCount; j++) {
                    taken |= (unsigned)(sPool[j] == sSide[k].cards[i]) << j;
                }
            }
        }
        if (taken >> sPoolCursor & 1u) {
            for (j = 0; j < sPoolCount; j++) {
                if (!(taken >> j & 1u)) {
                    sPoolCursor = j;
                    break;
                }
            }
        }
        moveCursor(&sPoolCursor, sPoolCount, b, 2, taken);
        if ((b & PAD_BUTTON_A) && !(taken >> sPoolCursor & 1u)) {
            Side* s = &sSide[who];
            s->cards[s->count++] = sPool[sPoolCursor];
            sPoolTaken++;
            psndSFXOn((const char*)0x20012);
            if (sPoolTaken >= PORT_CARDS_HAND * 2 || sPoolTaken >= sPoolCount) {
                startRound();
            }
        }
        break;
    }
    case D_PICK:
        sideInput(0);
        sideInput(1);
        if (sSide[0].pick >= 0 && sSide[1].pick >= 0) {
            sRoundWinner = fight(sSide[0].cards[sSide[0].pick], sSide[1].cards[sSide[1].pick], 1);
            sSide[0].used |= 1u << sSide[0].pick;
            sSide[1].used |= 1u << sSide[1].pick;
            sState = D_CLASH;
            sClock = 0;
            psndSFXOn("SFX_QUESTION_MARK1");
        }
        break;
    case D_CLASH:
        if (sClock == sSteps * STEP_FRAMES) {
            if (sRoundWinner >= 0) {
                sSide[sRoundWinner].score++;
            }
            psndSFXOn(sRoundWinner < 0 ? "SFX_BAD_SOUND1" :
                      (sRoundWinner == me() || (sOpponent == PORT_DUEL_PLAYER && !portOnlineActive())) ? "SFX_GOOD_SOUND1"
                                                                                                        : "SFX_BAD_SOUND1");
        }
        if (sClock >= sSteps * STEP_FRAMES + 100) {
            int left0 = 0, left1 = 0, i;
            for (i = 0; i < sSide[0].count; i++) {
                left0 += unused(&sSide[0], i);
            }
            for (i = 0; i < sSide[1].count; i++) {
                left1 += unused(&sSide[1], i);
            }
            if (sSide[0].score >= ROUNDS_TO_WIN || sSide[1].score >= ROUNDS_TO_WIN || left0 == 0 || left1 == 0 ||
                sRound >= ROUNDS_TO_WIN * 2 - 1) {
                sWinner = sSide[0].score > sSide[1].score ? 0 : sSide[1].score > sSide[0].score ? 1 : -1;
                if (sWinner == 0 && sOpponent < PORT_DUEL_PLAYER) {
                    static const int kPrize[] = {10, 30, 60};
                    if (!watching()) { /* (the wins are the host's: its computer counts them) */
                        readSaved();
                        sWins[sOpponent]++;
                        save();
                    }
                    sPrize = kPrize[sOpponent];
                    pouchAddCoin(sPrize); /* the game's: in every game of a session alike */
                }
                sRoundWinner = -2; /* (the last cards stay up) */
                sState = D_OVER;
                sClock = 0;
                psndSFXOn(sWinner == me() ? "SFX_GOOD_SOUND2" : "SFX_BAD_SOUND2");
            } else {
                startRound();
            }
        }
        break;
    case D_OVER:
        if (sClock > 30 && ((portPartyPressed(0) | portPartyPressed(1)) & PAD_BUTTON_A)) {
            psndSFXOn((const char*)0x20013);
            end();
        }
        break;
    }
    if (sState != D_OFF) {
        dispEntry(CAMERA_2D, 904.0f, 1, disp, NULL);
    }
}
