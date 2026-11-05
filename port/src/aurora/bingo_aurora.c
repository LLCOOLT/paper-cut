/* Bingo (port/include/port/party.h, docs/MODES.md): a 5x5 card of goals
 * from a seed, each checked from the file in play: its pouch (key items,
 * items, badges, coins, Star Pieces), partners and their ranks, the Badge,
 * Recipe and Tattle Logs, the Trouble Center and the Pit's deepest floor.
 * A goal once met stays marked (coins spent later don't take it away).
 *
 * The card: each goal has a difficulty from 1 to 5; five of each are drawn
 * (no two of a kind: one coin goal, one level goal...), and laid out on a
 * pandiagonal Latin square ((2 row + column) mod 5), so every row, column
 * and diagonal holds one goal of each difficulty. The same seed makes the
 * same card on every computer (our own generator: party_aurora.c).
 *
 * Only reads the game: nothing here changes play, and its drawing uses the
 * settings page's font, so online co-op stays in step. The card, its marks
 * and its clock are kept in <user>/bingo.txt. */
#include "port/party.h"
#include "port/versus.h"
#include "port/gameplus.h"
#include "port/settings.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void portCompanionUtf8(const char* game, char* out, int size, int lines); /* companion_pc.c */

enum {
    G_ITEM,      /* a: the item (key item, item or badge) in the pouch */
    G_PARTNER,   /* a: PartyId joined */
    G_COINS,     /* coins held >= a */
    G_LEVEL,     /* Mario's level >= a */
    G_STARPIECE, /* Star Pieces held >= a */
    G_SHINE,     /* Shine Sprites held >= a */
    G_BADGES,    /* badges found (the Badge Log) >= a */
    G_TATTLES,   /* Tattle Log entries >= a */
    G_RECIPES,   /* recipes cooked >= a */
    G_TROUBLES,  /* Trouble Center jobs >= a */
    G_MAXHP,     /* Mario's max HP >= a */
    G_MAXBP,     /* max BP >= a */
    G_PIT,       /* the Pit's deepest floor >= a */
    G_RANK,      /* a partner at rank >= a (1 Super, 2 Ultra) */
    G_BOOTS,     /* jump level >= a */
    G_HAMMER,    /* hammer level >= a */
    G_CRYSTALS,  /* Crystal Stars >= a */
    G_EQUIPPED,  /* badges worn at once >= a */
};

/* each kind but G_ITEM and G_PARTNER is its own group: at most one on a card */
typedef struct Goal {
    unsigned char kind, tier;
    short a;
} Goal;

static const Goal kGoals[] = {
    /* 1: the prologue and chapter 1 */
    {G_PARTNER, 1, PARTNER_KOOPS},
    {G_COINS, 1, 100},
    {G_LEVEL, 1, 3},
    {G_STARPIECE, 1, 5},
    {G_SHINE, 1, 1},
    {G_BADGES, 1, 5},
    {G_TATTLES, 1, 10},
    {G_ITEM, 1, ITEM_SUN_STONE},
    {G_ITEM, 1, ITEM_MOON_STONE},
    {G_ITEM, 1, ITEM_FIRE_FLOWER},
    {G_ITEM, 1, ITEM_HONEY_SYRUP},
    {G_ITEM, 1, ITEM_MYSTERY},
    {G_EQUIPPED, 1, 3},
    {G_MAXBP, 1, 6},
    /* 2: chapters 1 and 2 */
    {G_PARTNER, 2, PARTNER_FLURRIE},
    {G_ITEM, 2, ITEM_DIAMOND_STAR},
    {G_ITEM, 2, ITEM_PAPER_CURSE},
    {G_ITEM, 2, ITEM_PLANE_CURSE},
    {G_ITEM, 2, ITEM_PUNI_ORB},
    {G_ITEM, 2, ITEM_CONTACT_LENS},
    {G_ITEM, 2, ITEM_LIFE_SHROOM},
    {G_ITEM, 2, ITEM_THUNDER_RAGE},
    {G_ITEM, 2, ITEM_BOOS_SHEET},
    {G_ITEM, 2, ITEM_COCONUT},
    {G_BOOTS, 2, 2},
    {G_LEVEL, 2, 6},
    {G_TATTLES, 2, 25},
    {G_RECIPES, 2, 3},
    {G_TROUBLES, 2, 3},
    {G_MAXHP, 2, 20},
    {G_RANK, 2, 1},
    {G_BADGES, 2, 12},
    /* 3: chapters 2 to 4 */
    {G_PARTNER, 3, PARTNER_YOSHI},
    {G_PARTNER, 3, PARTNER_VIVIAN},
    {G_ITEM, 3, ITEM_EMERALD_STAR},
    {G_ITEM, 3, ITEM_GOLD_STAR},
    {G_ITEM, 3, ITEM_TUBE_CURSE},
    {G_ITEM, 3, ITEM_CHAMPS_BELT},
    {G_ITEM, 3, ITEM_ULTRA_SHROOM},
    {G_ITEM, 3, ITEM_JAMMIN_JELLY},
    {G_ITEM, 3, ITEM_SHOOTING_STAR},
    {G_ITEM, 3, ITEM_MOUSSE_CAKE},
    {G_ITEM, 3, ITEM_KEEL_MANGO},
    {G_HAMMER, 3, 2},
    {G_COINS, 3, 300},
    {G_LEVEL, 3, 10},
    {G_STARPIECE, 3, 15},
    {G_TATTLES, 3, 50},
    {G_TROUBLES, 3, 8},
    {G_PIT, 3, 10},
    {G_CRYSTALS, 3, 3},
    {G_EQUIPPED, 3, 8},
    /* 4: chapters 4 to 6 */
    {G_PARTNER, 4, PARTNER_BOBBERY},
    {G_PARTNER, 4, PARTNER_MSMOWZ},
    {G_ITEM, 4, ITEM_RUBY_STAR},
    {G_ITEM, 4, ITEM_SAPPHIRE_STAR},
    {G_ITEM, 4, ITEM_BOAT_CURSE},
    {G_ITEM, 4, ITEM_CHUCKOLA_COLA},
    {G_ITEM, 4, ITEM_TRAIN_TICKET},
    {G_ITEM, 4, ITEM_ZESS_DYNAMITE},
    {G_ITEM, 4, ITEM_COURAGE_MEAL},
    {G_BOOTS, 4, 3},
    {G_COINS, 4, 500},
    {G_LEVEL, 4, 15},
    {G_SHINE, 4, 5},
    {G_BADGES, 4, 30},
    {G_TATTLES, 4, 75},
    {G_RECIPES, 4, 10},
    {G_MAXHP, 4, 35},
    {G_MAXBP, 4, 18},
    {G_RANK, 4, 2},
    {G_PIT, 4, 30},
    /* 5: chapters 6 to 8, and the long way round */
    {G_ITEM, 5, ITEM_GARNET_STAR},
    {G_ITEM, 5, ITEM_BLIMP_TICKET},
    {G_ITEM, 5, ITEM_SPACE_FOOD},
    {G_HAMMER, 5, 3},
    {G_COINS, 5, 999},
    {G_LEVEL, 5, 20},
    {G_STARPIECE, 5, 30},
    {G_BADGES, 5, 45},
    {G_TATTLES, 5, 100},
    {G_RECIPES, 5, 20},
    {G_TROUBLES, 5, 20},
    {G_PIT, 5, 50},
    {G_CRYSTALS, 5, 6},
    {G_EQUIPPED, 5, 15},
};
#define GOAL_COUNT ((int)(sizeof(kGoals) / sizeof(kGoals[0])))

static char sSeed[PORT_BINGO_SEED_MAX + 1];
static short sCell[PORT_BINGO_CELLS]; /* kGoals index, -1 none */
static unsigned sDone;                /* bit per cell */
static unsigned char sOwner[PORT_BINGO_CELLS];
static long long sStartedAt;          /* time(): the card's start */
static unsigned long long sFirstLine; /* ms after the start, 0 not yet */
static int sChanges, sRead, sLockout;

static void path(char* out, size_t size) {
    snprintf(out, size, "%s/bingo.txt", portSettingsUserPath());
}

static void save(void) {
    char p[1024];
    FILE* f;
    path(p, sizeof(p));
    f = fopen(p, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "seed %s\nstart %lld\nfirst %llu\ndone %x\n", sSeed[0] != '\0' ? sSeed : "-", sStartedAt, sFirstLine,
            sDone);
    fclose(f);
}

static void make(const char* seed) {
    PortPartyRand r;
    int picked[5][5];
    int count[5] = {0, 0, 0, 0, 0};
    unsigned groups = 0; /* the kinds already on the card (but items and partners) */
    int order[GOAL_COUNT];
    int i, t;
    snprintf(sSeed, sizeof(sSeed), "%s", seed);
    r.s = portPartyHash(sSeed);
    for (i = 0; i < GOAL_COUNT; i++) {
        order[i] = i;
    }
    for (i = GOAL_COUNT - 1; i > 0; i--) { /* shuffled, then taken in turn */
        const int j = portPartyBelow(&r, i + 1);
        const int k = order[i];
        order[i] = order[j];
        order[j] = k;
    }
    for (i = 0; i < GOAL_COUNT; i++) {
        const Goal* g = &kGoals[order[i]];
        const int tier = g->tier - 1;
        const unsigned bit = 1u << g->kind;
        if (count[tier] >= 5) {
            continue;
        }
        if (g->kind != G_ITEM && g->kind != G_PARTNER) {
            if (groups & bit) {
                continue;
            }
            groups |= bit;
        }
        picked[tier][count[tier]++] = order[i];
    }
    for (t = 0; t < 5; t++) {
        int n = 0;
        for (i = 0; i < PORT_BINGO_CELLS; i++) {
            const int row = i / 5, col = i % 5;
            if ((2 * row + col) % 5 == t) {
                sCell[i] = (short)(n < count[t] ? picked[t][n] : -1);
                n++;
            }
        }
    }
}

static void readSaved(void) {
    char p[1024], seed[64];
    long long start = 0;
    unsigned long long first = 0;
    unsigned done = 0;
    FILE* f;
    if (sRead) {
        return;
    }
    sRead = 1;
    memset(sCell, 0xFF, sizeof(sCell));
    path(p, sizeof(p));
    f = fopen(p, "r");
    if (f == NULL) {
        return;
    }
    if (fscanf(f, "seed %63s start %lld first %llu done %x", seed, &start, &first, &done) == 4 &&
        strcmp(seed, "-") != 0) {
        make(seed);
        sStartedAt = start;
        sFirstLine = first;
        sDone = done & ((1u << PORT_BINGO_CELLS) - 1);
    }
    fclose(f);
}

const char* portBingoSeed(void) {
    readSaved();
    return sSeed;
}

void portBingoNew(const char* seed) {
    char made[PORT_BINGO_SEED_MAX + 1];
    readSaved();
    if (seed == NULL || seed[0] == '\0') {
        static const char kChars[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
        PortPartyRand r;
        int i;
        r.s = (unsigned long long)time(NULL) * 2654435761ull ^ (unsigned long long)(size_t)&r;
        for (i = 0; i < 6; i++) {
            made[i] = kChars[portPartyBelow(&r, (int)sizeof(kChars) - 1)];
        }
        made[6] = '\0';
        seed = made;
    } else {
        /* one word of printable letters, so the file and the room take it as it is */
        int i, n = 0;
        for (i = 0; seed[i] != '\0' && n < PORT_BINGO_SEED_MAX; i++) {
            const char c = seed[i];
            if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_') {
                made[n++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
            }
        }
        made[n] = '\0';
        if (n == 0) {
            snprintf(made, sizeof(made), "BINGO");
        }
        seed = made;
    }
    make(seed);
    sDone = 0;
    memset(sOwner, 0, sizeof(sOwner));
    sStartedAt = (long long)time(NULL);
    sFirstLine = 0;
    sChanges++;
    save();
    printf("[bingo] card %s\n", sSeed);
    fflush(stdout);
}

void portBingoClear(void) {
    readSaved();
    sSeed[0] = '\0';
    memset(sCell, 0xFF, sizeof(sCell));
    sDone = 0;
    memset(sOwner, 0, sizeof(sOwner));
    sChanges++;
    save();
}

static const char* partnerTag(int id) {
    static const char* const kTags[] = {"name_party0", "name_party1", "name_party2", "name_party3",
                                        "name_party4", "name_party5", "name_party6"};
    /* name_partyN is PartyId N + 1 (win_party.c) */
    return id >= 1 && id <= 7 ? kTags[id - 1] : NULL;
}

/* the item's or partner's name, from the game's text (in its encoding) */
static const char* gameName(const Goal* g) {
    const char* tag = NULL;
    const char* text;
    if (g->kind == G_ITEM && g->a > 0 && g->a < ITEM_TYPE_MAX) {
        tag = itemDataTable[g->a].name;
    } else if (g->kind == G_PARTNER) {
        tag = partnerTag(g->a);
    }
    if (tag == NULL || seqGetSeq() == SEQ_LOGO) {
        return "?";
    }
    text = msgSearch(tag);
    return text != NULL ? text : "?";
}

static const char* templateOf(const Goal* g) {
    switch (g->kind) {
    case G_ITEM:
        return "Get %s";
    case G_PARTNER:
        return "%s joins the party";
    case G_COINS:
        return "%d coins at once";
    case G_LEVEL:
        return "Level %d";
    case G_STARPIECE:
        return "%d Star Pieces at once";
    case G_SHINE:
        return "%d Shine Sprites at once";
    case G_BADGES:
        return "%d badges found";
    case G_TATTLES:
        return "%d Tattle Log entries";
    case G_RECIPES:
        return "%d recipes cooked";
    case G_TROUBLES:
        return "%d troubles solved";
    case G_MAXHP:
        return "%d max HP";
    case G_MAXBP:
        return "%d max BP";
    case G_PIT:
        return "Pit of 100 Trials: floor %d";
    case G_RANK:
        return g->a >= 2 ? "A partner at Ultra Rank" : "A partner at Super Rank";
    case G_BOOTS:
        return g->a >= 3 ? "The Ultra Boots" : "The Super Boots";
    case G_HAMMER:
        return g->a >= 3 ? "The Ultra Hammer" : "The Super Hammer";
    case G_CRYSTALS:
        return "%d Crystal Stars";
    case G_EQUIPPED:
        return "%d badges worn at once";
    }
    return "?";
}

void portBingoCellText(int i, int game, char* out, size_t size) {
    const Goal* g;
    const char* t;
    readSaved();
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    if (i < 0 || i >= PORT_BINGO_CELLS || sCell[i] < 0) {
        return;
    }
    g = &kGoals[sCell[i]];
    t = game ? portTrGame(templateOf(g)) : portTr(templateOf(g));
    if (g->kind == G_ITEM || g->kind == G_PARTNER) {
        char name[128];
        if (game) {
            snprintf(name, sizeof(name), "%s", gameName(g));
        } else {
            portCompanionUtf8(gameName(g), name, (int)sizeof(name), 0);
        }
        snprintf(out, size, t, name);
    } else if (strstr(t, "%d") != NULL) {
        snprintf(out, size, t, g->a);
    } else {
        snprintf(out, size, "%s", t);
    }
}

int portBingoDone(int i) {
    readSaved();
    return i >= 0 && i < PORT_BINGO_CELLS && (sDone >> i & 1u) != 0;
}

int portBingoOwner(int i) {
    return i >= 0 && i < PORT_BINGO_CELLS ? sOwner[i] : 0;
}

void portBingoSetOwner(int i, int owner) {
    if (i >= 0 && i < PORT_BINGO_CELLS && sOwner[i] != (unsigned char)owner) {
        sOwner[i] = (unsigned char)owner;
        sChanges++;
    }
}

void portBingoSetLockout(int on) {
    if (sLockout != (on != 0)) {
        sLockout = on != 0;
        if (!sLockout) {
            memset(sOwner, 0, sizeof(sOwner));
        }
        sChanges++;
    }
}

int portBingoLockout(void) {
    return sLockout;
}

/* the squares that count: done, and in lockout this player's own */
static unsigned counted(void) {
    unsigned m = sDone;
    int i;
    if (sLockout) {
        for (i = 0; i < PORT_BINGO_CELLS; i++) {
            if (sOwner[i] != 1) {
                m &= ~(1u << i);
            }
        }
    }
    return m;
}

static unsigned lineMask(int line) {
    unsigned m = 0;
    int k;
    for (k = 0; k < 5; k++) {
        const int cell = line < 5 ? line * 5 + k          /* rows */
                         : line < 10 ? k * 5 + (line - 5) /* columns */
                         : line == 10 ? k * 6             /* the diagonal */
                                      : k * 4 + 4;        /* the other */
        m |= 1u << cell;
    }
    return m;
}

int portBingoLines(void) {
    int line, n = 0;
    readSaved();
    for (line = 0; line < 12; line++) {
        const unsigned m = lineMask(line);
        if ((counted() & m) == m) {
            n++;
        }
    }
    return n;
}

int portBingoCount(void) {
    unsigned d;
    int n = 0;
    readSaved();
    for (d = counted(); d != 0; d &= d - 1) {
        n++;
    }
    return n;
}

int portBingoChanges(void) {
    return sChanges;
}

unsigned long long portBingoElapsed(void) {
    readSaved();
    if (sSeed[0] == '\0' || sStartedAt == 0) {
        return 0;
    }
    return (unsigned long long)((long long)time(NULL) - sStartedAt) * 1000ull;
}

unsigned long long portBingoFirstLine(void) {
    return sFirstLine;
}

static int has(int item) {
    const PouchData* p = pouchGetPtr();
    int i;
    if (item >= KEY_ITEM_MIN && item < KEY_ITEM_MAX) {
        for (i = 0; i < MAX_KEY_ITEMS; i++) {
            if (p->keyItems[i] == item) {
                return 1;
            }
        }
    } else if (item >= ITEM_MIN && item < ITEM_MAX) {
        for (i = 0; i < MAX_HELD_ITEMS; i++) {
            if (p->heldItems[i] == item) {
                return 1;
            }
        }
        for (i = 0; i < MAX_STORED_ITEMS; i++) {
            if (p->storedItems[i] == item) {
                return 1;
            }
        }
    } else if (item >= BADGE_MIN && item < BADGE_MAX) {
        for (i = 0; i < MAX_BADGES; i++) {
            if (p->badges[i] == item) {
                return 1;
            }
        }
    }
    return 0;
}

static int met(const Goal* g, const PortJournal* j) {
    const PouchData* p = pouchGetPtr();
    int i, n;
    switch (g->kind) {
    case G_ITEM:
        return has(g->a);
    case G_PARTNER:
        return (p->partyData[g->a].flags & POUCH_PARTY_FLAG_JOINED) != 0;
    case G_COINS:
        return p->coins >= g->a;
    case G_LEVEL:
        return p->level >= g->a;
    case G_STARPIECE:
        return p->starPieceCount >= g->a;
    case G_SHINE:
        return p->shineSpriteCount >= g->a;
    case G_BADGES:
        return j->badges >= g->a;
    case G_TATTLES:
        return j->tattles >= g->a;
    case G_RECIPES:
        return j->recipes >= g->a;
    case G_TROUBLES:
        return j->troubles >= g->a;
    case G_MAXHP:
        return p->baseMaxHP >= g->a;
    case G_MAXBP:
        return p->maximumBP >= g->a;
    case G_PIT:
        return swByteGet(GSW_JON_DEEPEST_FLOOR) >= g->a;
    case G_RANK:
        for (i = PARTNER_GOOMBELLA; i <= PARTNER_MSMOWZ; i++) {
            if ((p->partyData[i].flags & POUCH_PARTY_FLAG_JOINED) && p->partyData[i].attackLevel >= g->a) {
                return 1;
            }
        }
        return 0;
    case G_BOOTS:
        return p->jumpLevel >= g->a;
    case G_HAMMER:
        return p->hammerLevel >= g->a;
    case G_CRYSTALS:
        return j->crystalStars >= g->a;
    case G_EQUIPPED:
        for (i = 0, n = 0; i < MAX_BADGES; i++) {
            n += p->equippedBadges[i] != 0;
        }
        return n >= g->a;
    }
    return 0;
}

void portBingoFrame(void) {
    static unsigned sTick;
    PortJournal j;
    const SequenceType seq = seqGetSeq();
    unsigned newly = 0;
    int i;
    readSaved();
    if (sSeed[0] == '\0' || (seq != SEQ_GAME && seq != SEQ_BATTLE) || gp == NULL) {
        return;
    }
    if (portVersusOn()) {
        return; /* a versus fight's pouch is one built for it (versus_aurora.c), not the file's */
    }
    if (++sTick % 15 != 0) { /* four times a second is plenty */
        return;
    }
    if (!portJournal(&j)) {
        return;
    }
    for (i = 0; i < PORT_BINGO_CELLS; i++) {
        if (sCell[i] >= 0 && !(sDone >> i & 1u) && met(&kGoals[sCell[i]], &j)) {
            newly |= 1u << i;
        }
    }
    if (newly == 0) {
        return;
    }
    sDone |= newly;
    sChanges++;
    if (sFirstLine == 0 && portBingoLines() > 0) {
        sFirstLine = portBingoElapsed();
        if (sFirstLine == 0) {
            sFirstLine = 1;
        }
    }
    save();
    for (i = 0; i < PORT_BINGO_CELLS; i++) {
        if (newly >> i & 1u) {
            char text[160];
            portBingoCellText(i, 0, text, sizeof(text));
            printf("[bingo] square %d done: %s\n", i + 1, text);
        }
    }
    fflush(stdout);
}
