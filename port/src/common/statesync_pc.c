/* "Sync what matters" online play, the game's side (port/include/port/statesync.h;
 * the design and every merge rule: docs/STATE_SYNC.md).
 *
 * Layer 1, a shared world with each player free:
 * - every 3 frames each game sends where its player is (map, position, facing,
 *   pose and model, the partner's too, whether they're in a battle or an
 *   event); in the same room the other player is drawn as a ghost
 *   (statesync_ghost_pc.c), interpolated 100 ms behind their game's clock;
 * - the shared progress (story flags and words, the area's flags, the pouch's
 *   items and badges, coins, Star Pieces, Shine Sprites, Mario's level and
 *   the partners) is compared with a copy each frame (the "shadow"); what
 *   changed goes to the other game as a reliable message and is applied
 *   there (and to its shadow, so it isn't sent back);
 * - a new session (a join, mid-game: nothing starts again) first merges the
 *   whole progress both ways (union of flags, the higher of each word, the
 *   more of each item, ...), so the two end up with the same.
 * Layers 2-4 are in statesync_room_pc.c and statesync_battle_pc.c. */
#include "port/qol.h"
#include "port/battlex.h" /* (the port's own item ids: a peer's items checked) */
#include "data/item_data.h"
#include "driver/animdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "eff/eff_recovery.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party/party_yoshi.h"
#include "system.h"
#include "mario/mario_sbr.h"
#include "party.h"
#include "port/brothers.h"
#include "port/costume.h"
#include "port/player_menu.h"
#include "port/gameplus.h"
#include "port/luigi.h"
#include "port/online.h"
#include "port/cardsave_pc.h"
#include "port/players.h"
#include "port/pm64.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/online_end.h"
#include "port/tr.h"
#include "win/win_main.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PORT_MODS
#include "port/lua_mods.h"
#endif
#ifdef TTYD_BACKEND_AURORA
#include "port/online_menu.h"
#include "port/raid.h"
#include "port/versus.h"
/* a raid boss under way: its lent loadout isn't anyone's progress (statesync_modes_pc.inc) */
#define raidHold() portRaidActive()
/* a versus fight, from its start to the field again: its lent loadouts and the ring's own words aren't
   anyone's progress (versus_aurora.c puts the file's back) */
#define versusHold() portVersusBusy()
/* any other mode's lent pouch (a boss rush's or refight's loadout, a puzzle's, the dojo's: the one check,
   port/include/port/mode_lending.h; their ends put the file's pouch back): not anyone's progress either (its
   level, boots and hammer, partners and items went out as gains a peer kept) */
#include "port/mode_lending.h"
#define lentHold() ((portModeLending() & PORT_LEND_POUCH) != 0 && !raidHold() && !versusHold())
/* any: the progress held */
#define modeHold() (raidHold() || versusHold() || lentHold())
/* the party modes alone: playing together stops for these (a boss rush stays together) */
#define partyModeHold() (raidHold() || versusHold())
#else
#define raidHold() 0
#define versusHold() 0
#define lentHold() 0
#define modeHold() 0
#define partyModeHold() 0
#endif

#define SEND_EVERY 3
#define INTERP_MS 100
#define SAMPLES 32
#define IDS 1024
#define LISTS 4

enum { L_KEY, L_HELD, L_STORED, L_BADGE };

typedef struct PlayerMsg {
    uint32_t ms;
    char map[16];
    int32_t seq;
    uint8_t inBattle, keysOff, owns, hasPartner;
    uint32_t roomFrames;
    float x, y, z, dir;
    char model[24];
    char anim[32];
    int32_t partner;
    float px, py, pz, pdir;
    char pmodel[24];
    char panim[32];
    int32_t playAs; /* their Play as pick (portPlayAs): their ghost is drawn as it */
    int32_t player; /* their player number (1 the host, 2..4 the guests) */
    int32_t watch;  /* they watch (a watcher: no ghost, no room, no progress of theirs) */
    uint32_t battleId; /* the shared battle they're player 1 of (a game in the room may join it), 0 */
    char name[SS_NAME]; /* their player's name (untrusted: cleaned as it comes, cleanName) */
    char lua[24];       /* their Lua mods' hash (port/include/port/lua_mods.h), "" none */
    int32_t costume;    /* their costume (portCostumeCurrent: 0 none), worn by their ghost */
    uint8_t hasLuigi;   /* Brothers mode's Luigi is in their field: drawn beside their ghost */
    float lx, ly, lz, ldir;
    char lmodel[24];
    char lanim[32];
    int32_t modeFloor; /* the Pit's or the Paper Tower's floor they stand on (myModeFloor), 0 none */
    int32_t pm64;      /* the Paper Mario 64 partner in their partner's place (port/pm64.h), 0 none */
    int32_t travel;    /* their own fast travel setting (port/include/port/fast_travel.h): everyone's on lets
                          anyone travel */
    uint8_t coop;      /* a co-op guest (follows the host's world: no ghost of its own, statesync_coop_pc.inc) */
    uint8_t hasHelper[2];
    uint8_t paused; /* the host's Start menu is up (its world stands still): a guest's "Paused by" card */
    float hx[2], hy[2], hz[2], hdir[2]; /* the follower and the companion (players 3 and 4's) */
    char hmodel[2][24];
    char hanim[2][32];
    uint8_t card[PORT_PLAYER_MENU_CARD]; /* their player's card (port/player_menu.h), drawn at their place */
    /* the host's, seats 2-4: each guest's own character (statesync_coop_pc.inc "the guest's own character"):
       the epoch its guest's word must carry, the host holding it now, the character taken by its player */
    uint32_t seatEpoch[3];
    uint8_t seatHeld[3], seatFree[3];
    uint8_t pad2[2];
} PlayerMsg;

typedef struct Stats {
    int16_t level, baseMaxHP, baseMaxFP, maximumBP, starPoints, maximumSP, rank;
    uint16_t starPowers;
    int8_t jump, hammer;
    uint16_t partyFlags[8];
    int16_t partyHPLevel[8], partyAttack[8], partyTech[8], partyBaseMaxHP[8];
} Stats;

typedef struct Progress {
    uint32_t gswf[0x100];
    uint8_t gsw[0x800];
    int32_t gsw0;
    char area[4];
    uint32_t lswf[0x10];
    uint8_t lsw[0x400];
    uint8_t count[LISTS][IDS];
    int16_t coins, starPieces, shines;
    Stats stats;
} Progress;

/* another player, as their PLAYER messages say (straight from them, or the host's forwarded) */
typedef struct Other {
    PlayerMsg s[SAMPLES];
    unsigned long long at[SAMPLES]; /* when each came, our clock */
    int count, head;
    long long offset; /* our clock minus theirs, the smallest seen (drifting up slowly) */
    int offsetSet;
    unsigned long long lastAt; /* the newest's arrival (a player not heard from for a while is away) */
    SsPeer info;
    int ghostShown;
    char name[SS_NAME]; /* cleaned */
    char lua[24];
    int travel; /* their PlayerMsg.travel */
} Other;

#define AWAY_MS 2000

static struct {
    int on, checked;
    unsigned frame;
    int seq, lastSeq;
    int baseline; /* the shadow holds this game's progress */
    unsigned snapOwed; /* whole progresses asked for while a mode held it (bit p: player p; bit 0: everyone):
                          sent once it's over (a lent loadout isn't progress: pm64x_online_versus) */
    Progress shadow;
    Progress scratch;
    Other pl[SS_PLAYERS + 1]; /* by player number (1..4; this game's own unused) */
    int battlePeer;           /* the player a shared battle or scene is with (statesync_battle_pc.c) */
    int except;               /* sendDiffs: a player not to send to (0 none) */
    char map[16];
    unsigned roomFrames;
    int owns;
    unsigned logEvery;
    unsigned merges, applied, sentOps;
    int watch;           /* this game watches: it follows the host, its own Mario hidden */
    int pitFloor;        /* GSW_JON_FLOOR as this room was entered (the Pit's rooms repeat their maps) */
    int pitSeq;          /* the sequence last frame, for pitFloor */
    char watchWarp[16];  /* the room it last went to after the host */
    unsigned watchNotice; /* the notice last said (frame) */
    int frameEnded;       /* portStateSyncFrameEnd ran for this frame */
} G;

static const SsPeer kNobody;
static int sCountsAbs; /* the host: its counts to the guests at the frame's end */

/* Playing together (statesync_coop_pc.inc, included below) */
static void coopSession(int player);
static void coopRel(int from, const uint8_t* p, size_t n);
static void coopSaveRel(int from, const uint8_t* p, size_t n);
static void gotPad(int from, const uint8_t* p, size_t n);
static void takeSnapshot(const Progress* r);
static void coopFillHelpers(PlayerMsg* m);
static void coopApplyPad(PADStatus* pads);
static void coopLeave(void);
static void coopDigest(int same);
static int coopTogether(void);
static void ndHostSent(void);          /* the delay log (statesync_coop_pc.inc) */
static void gotOwn(int from, const uint8_t* p, size_t n);
static void coopOwnFill(PlayerMsg* m);
static void coopOwnSend(void);
static void coopOwnFrame(void);
static void coopActRel(int from, const uint8_t* p, size_t n); /* the guests' field actions */
static void coopActFrame(void);
static int coopSendsEveryFrame(void);
static void ndGuestFrame(int afterDraw);

/* player p plays with this game now: linked (directly, or through the host), heard from lately */
static int present(int p) {
    if (p < 1 || p > SS_PLAYERS || p == ssNetMe() || !G.pl[p].info.valid ||
        ssNetMs() - G.pl[p].lastAt > AWAY_MS) {
        return 0;
    }
    return ssNetIsHost() || p == 1 ? ssNetLinked(p) : ssNetLinked(1);
}

/* player p in this game's room, in the field */
static int myModeFloor(void);

static int hereIn(int p) {
    return present(p) && G.pl[p].info.seq == SEQ_GAME && G.seq == SEQ_GAME &&
           !strcmp(G.pl[p].info.map, gp->currentMapName) && G.pl[p].info.modeFloor == myModeFloor();
}

/* the ghost slots' order: the other players by number */
static int otherIndex(int p) {
    const int me = ssNetMe();
    const int i = me != 0 && p > me ? p - 2 : p - 1;
    return i >= 0 && i < SS_PLAYERS - 1 ? i : -1;
}

static void area(const char* map, char out[4]) {
    memset(out, 0, 4);
    memcpy(out, map, 3);
}

/* ---- the pouch's lists, as counts per id ---- */

static int16_t* listOf(PouchData* pd, int l, int* cap) {
    switch (l) {
        case L_KEY:
            *cap = MAX_KEY_ITEMS;
            return pd->keyItems;
        case L_HELD:
            *cap = MAX_HELD_ITEMS;
            return pd->heldItems;
        case L_STORED:
            *cap = MAX_STORED_ITEMS;
            return pd->storedItems;
        default:
            *cap = MAX_BADGES;
            return pd->badges;
    }
}

static void readStats(const PouchData* pd, Stats* s) {
    int i;
    memset(s, 0, sizeof(*s));
    s->level = pd->level;
    s->baseMaxHP = pd->baseMaxHP;
    s->baseMaxFP = pd->baseMaxFP;
    s->maximumBP = pd->maximumBP;
    s->starPoints = pd->starPoints;
    s->maximumSP = pd->maximumSP;
    s->rank = pd->rank;
    s->starPowers = pd->starPowersObtained;
    s->jump = pd->jumpLevel;
    s->hammer = pd->hammerLevel;
    for (i = 0; i < 8; i++) {
        s->partyFlags[i] = pd->partyData[i].flags;
        s->partyHPLevel[i] = pd->partyData[i].HPLevel;
        s->partyAttack[i] = pd->partyData[i].attackLevel;
        s->partyTech[i] = pd->partyData[i].techLevel;
        s->partyBaseMaxHP[i] = pd->partyData[i].baseMaxHP;
    }
}

/* a word each player keeps their own, never crossing: the Paper Mario 64 partners worn (port/pm64.h),
   and the Pit's floor (each player's room is their own floor: the Pit together follows by the floors
   the players stand on, statesync_modes_pc.inc) */
static int ownWord(int i) {
    return portPm64GswOwn(i) || i == GSW_JON_FLOOR;
}

static void readProgress(Progress* p) {
    PouchData* pd = pouchGetPtr();
    int l, i;
    memset(p, 0, sizeof(*p));
    memcpy(p->gswf, gp->gswf, sizeof(p->gswf));
    memcpy(p->gsw, gp->gsw, sizeof(p->gsw));
    p->gsw0 = gp->gsw0;
    area(gp->currentMapName, p->area);
    memcpy(p->lswf, gp->lswf, sizeof(p->lswf));
    memcpy(p->lsw, gp->lsw, sizeof(p->lsw));
    for (l = 0; l < LISTS; l++) {
        int cap;
        const int16_t* list = listOf(pd, l, &cap);
        for (i = 0; i < cap; i++) {
            if (list[i] > 0 && list[i] < IDS && p->count[l][list[i]] < 255) {
                p->count[l][list[i]]++;
            }
        }
    }
    p->coins = pd->coins;
    p->starPieces = pd->starPieceCount;
    p->shines = pd->shineSpriteCount;
    readStats(pd, &p->stats);
}

static unsigned long long fnv(const void* data, size_t n, unsigned long long h) {
    const uint8_t* b = (const uint8_t*)data;
    size_t i;
    for (i = 0; i < n; i++) {
        h = (h ^ b[i]) * 0x100000001B3ull;
    }
    return h;
}

/* the shared progress's hashes, for the logs: flags+words, the pouch */
static void digest(const Progress* p, unsigned long long* story, unsigned long long* pouch) {
    uint8_t gsw[sizeof(p->gsw)];
    int i;
    memcpy(gsw, p->gsw, sizeof(gsw));
    for (i = 0; i < (int)sizeof(gsw); i++) {
        if (ownWord(i)) {
            gsw[i] = 0; /* (each player's own: not compared) */
        }
    }
    *story = fnv(gsw, sizeof(gsw), fnv(p->gswf, sizeof(p->gswf), fnv(&p->gsw0, 4, 0xCBF29CE484222325ull)));
    *pouch = fnv(p->count, sizeof(p->count), fnv(&p->coins, 6, 0xCBF29CE484222325ull));
}

/* ---- applying the other game's ---- */

static void addItem(int l, int id) {
    BOOL ok = 0;
    if (l == L_STORED) {
        ok = pouchAddKeepItem((s16)id);
    } else {
        ok = pouchGetItem((ItemType)id);
        if (!ok && l == L_HELD) {
            ok = pouchAddKeepItem((s16)id); /* the bag full: to storage */
        }
    }
    if (!ok) {
        ssLog("merge: no room for item %d (list %d): not added", id, l);
    }
}

static void removeItem(int l, int id) {
    PouchData* pd = pouchGetPtr();
    if (l == L_STORED) {
        int i;
        for (i = 0; i < MAX_STORED_ITEMS; i++) {
            if (pd->storedItems[i] == id) {
                memmove(&pd->storedItems[i], &pd->storedItems[i + 1],
                        (MAX_STORED_ITEMS - 1 - i) * sizeof(pd->storedItems[0]));
                pd->storedItems[MAX_STORED_ITEMS - 1] = 0;
                return;
            }
        }
        return;
    }
    if (!pouchRemoveItem((ItemType)id)) {
        ssLog("merge: item %d (list %d) not here to remove", id, l);
    }
}

/* ---- what another game sends, kept to the game's ranges (its tables are read by them: a peer is
   another program, trusted for nothing) ---- */

/* an item fit for its list: the game's ids of that kind, or the port's own (port/battlex.h) */
static int itemOk(int l, int id) {
    if (l == L_KEY) {
        return id >= KEY_ITEM_MIN && id < KEY_ITEM_MAX;
    }
    if (l == L_BADGE) {
        return (id >= BADGE_MIN && id < BADGE_MAX) || (id >= PORT_ITEM_MIN && id < PORT_ITEM_END);
    }
    return (id >= ITEM_MIN && id < ITEM_MAX) || (id >= PORT_ITEM_MIN && id < PORT_ITEM_END);
}

static int16_t clampTo(int v, int lo, int hi) {
    return (int16_t)(v < lo ? lo : v > hi ? hi : v);
}

/* the levels (the partners' 0..2 index _party_max_hp_table), the rank, boots and hammer (0..3), Mario's
   level, the Star Powers' eight bits and the maxima */
static void statsClamp(Stats* s) {
    int i;
    s->level = clampTo(s->level, 1, 99);
    s->baseMaxHP = clampTo(s->baseMaxHP, 0, 999);
    s->baseMaxFP = clampTo(s->baseMaxFP, 0, 999);
    s->maximumBP = clampTo(s->maximumBP, 0, 999);
    s->starPoints = clampTo(s->starPoints, 0, 99);
    s->maximumSP = clampTo(s->maximumSP, 0, 800);
    s->rank = clampTo(s->rank, 0, 3);
    s->starPowers &= 0xFF;
    s->jump = (int8_t)clampTo(s->jump, 0, 3);
    s->hammer = (int8_t)clampTo(s->hammer, 0, 3);
    for (i = 0; i < 8; i++) {
        s->partyHPLevel[i] = clampTo(s->partyHPLevel[i], 0, 2);
        s->partyAttack[i] = clampTo(s->partyAttack[i], 0, 2);
        s->partyTech[i] = clampTo(s->partyTech[i], 0, 2);
        s->partyBaseMaxHP[i] = clampTo(s->partyBaseMaxHP[i], 0, 999);
    }
}

/* a whole progress as it came (what: for the log): items of their list's kind, no more of one than the
   list holds, the counts and the stats in range */
static void progressCheck(Progress* r, const char* what) {
    int l, id, dropped = 0;
    for (l = 0; l < LISTS; l++) {
        const int cap = l == L_KEY ? MAX_KEY_ITEMS : l == L_HELD ? MAX_HELD_ITEMS : l == L_STORED ? MAX_STORED_ITEMS
                                                                                                  : MAX_BADGES;
        for (id = 0; id < IDS; id++) {
            if (r->count[l][id] != 0 && !itemOk(l, id)) {
                r->count[l][id] = 0;
                dropped++;
            } else if (r->count[l][id] > cap) {
                r->count[l][id] = (uint8_t)cap;
            }
        }
    }
    r->coins = clampTo(r->coins, 0, 9999);
    r->starPieces = clampTo(r->starPieces, 0, 999);
    r->shines = clampTo(r->shines, 0, 999);
    statsClamp(&r->stats);
    if (dropped) {
        ssLog("merge: %s had %d item ids not of their list: left out", what, dropped);
    }
}

static void applyStats(const Stats* r, int log) {
    PouchData* pd = pouchGetPtr();
    Stats mine;
    int i, changed = 0;
    readStats(pd, &mine);
    if (r->level > mine.level) {
        pd->level = r->level;
        pd->baseMaxHP = r->baseMaxHP;
        pd->baseMaxFP = r->baseMaxFP;
        pd->maximumBP = r->maximumBP;
        pd->starPoints = r->starPoints;
        changed = 1;
    } else if (r->level == mine.level && r->starPoints > mine.starPoints) {
        pd->starPoints = r->starPoints;
        changed = 1;
    }
    if (r->maximumSP > mine.maximumSP) {
        pd->maximumSP = r->maximumSP, changed = 1;
    }
    if (r->rank > mine.rank) {
        pd->rank = r->rank, changed = 1;
    }
    if ((r->starPowers | mine.starPowers) != mine.starPowers) {
        pd->starPowersObtained |= r->starPowers, changed = 1;
    }
    if (r->jump > mine.jump) {
        pd->jumpLevel = r->jump, changed = 1;
    }
    if (r->hammer > mine.hammer) {
        pd->hammerLevel = r->hammer, changed = 1;
    }
    for (i = 0; i < 8; i++) {
        PouchPartyData* d = &pd->partyData[i];
        if ((r->partyFlags[i] & POUCH_PARTY_FLAG_JOINED) && !(d->flags & POUCH_PARTY_FLAG_JOINED)) {
            d->flags |= POUCH_PARTY_FLAG_JOINED;
            if (d->baseMaxHP < r->partyBaseMaxHP[i]) {
                d->baseMaxHP = r->partyBaseMaxHP[i];
                d->currentHP = d->baseMaxHP;
            }
            changed = 1;
            if (log) {
                ssLog("merge: partner %d joined the party (the other game's)", i);
            }
        }
        if (r->partyHPLevel[i] > d->HPLevel) {
            d->HPLevel = r->partyHPLevel[i];
            d->baseMaxHP = r->partyBaseMaxHP[i];
            changed = 1;
        }
        if (r->partyAttack[i] > d->attackLevel) {
            d->attackLevel = r->partyAttack[i], changed = 1;
        }
        if (r->partyTech[i] > d->techLevel) {
            d->techLevel = r->partyTech[i], changed = 1;
        }
    }
    if (changed) {
        pouchReviseMarioParam();
        pouchRevisePartyParam();
        if (log) {
            ssLog("merge: stats now level %d, SP max %d, jump %d hammer %d", pd->level, pd->maximumSP, pd->jumpLevel,
                  pd->hammerLevel);
        }
    }
}

/* the whole progress, merged (a new session): union of flags, the higher of
   each word, the more of each item, the higher of each count */
static void mergeSnapshot(const Progress* r) {
    PouchData* pd = pouchGetPtr();
    Progress mine;
    int i, l, id;
    unsigned flags = 0, words = 0, items = 0;
    readProgress(&mine);
    for (i = 0; i < 0x100; i++) {
        if ((gp->gswf[i] | r->gswf[i]) != gp->gswf[i]) {
            flags += (unsigned)__builtin_popcount(r->gswf[i] & ~gp->gswf[i]);
            gp->gswf[i] |= r->gswf[i];
        }
    }
    for (i = 1; i < 0x800; i++) {
        if (r->gsw[i] > gp->gsw[i] && !ownWord(i)) {
            gp->gsw[i] = r->gsw[i];
            words++;
        }
    }
    if (r->gsw0 > gp->gsw0) {
        ssLog("merge: story %d -> %d (the other game's is further)", gp->gsw0, r->gsw0);
        gp->gsw0 = r->gsw0;
    }
    if (!memcmp(r->area, mine.area, 4)) {
        for (i = 0; i < 0x10; i++) {
            gp->lswf[i] |= r->lswf[i];
        }
        for (i = 0; i < 0x400; i++) {
            if (r->lsw[i] > gp->lsw[i]) {
                gp->lsw[i] = r->lsw[i];
            }
        }
    }
    for (l = 0; l < LISTS; l++) {
        for (id = 1; id < IDS; id++) {
            int n;
            for (n = mine.count[l][id]; n < r->count[l][id]; n++) {
                addItem(l, id);
                items++;
            }
        }
    }
    if (r->coins > pd->coins) {
        pd->coins = r->coins;
    }
    if (r->starPieces > pd->starPieceCount) {
        pd->starPieceCount = r->starPieces;
    }
    if (r->shines > pd->shineSpriteCount) {
        pd->shineSpriteCount = r->shines;
    }
    applyStats(&r->stats, 1);
    G.merges++;
    ssLog("merge: joined the other game's progress: +%u flags, +%u words, +%u items, story %d, coins %d", flags, words,
          items, gp->gsw0, pd->coins);
}

static int rd16(const uint8_t* p) {
    int16_t v;
    memcpy(&v, p, 2);
    return v;
}

static uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void applyRel(int type, const uint8_t* p, size_t n) {
    PouchData* pd = gp != NULL ? pouchGetPtr() : NULL;
    size_t at;
    char myArea[4];
    if (gp == NULL || !G.baseline) {
        return; /* (no file yet: a new session's merge comes with the file) */
    }
    area(gp->currentMapName, myArea);
    switch (type) {
        case SS_R_SNAPSHOT:
            if (n >= sizeof(Progress)) {
                static Progress sIn; /* (the payload sits at any offset: copied out, then checked) */
                memcpy(&sIn, p, sizeof(sIn));
                if (portStateSyncCoopGuest()) {
                    takeSnapshot(&sIn); /* (playing together: the host's world, whole) */
                } else if (ssNetIsHost() && coopTogether()) {
                    ssLog("merge: a guest's whole progress: not taken (playing together: the host's world counts)");
                } else {
                    progressCheck(&sIn, "the other game's progress");
                    mergeSnapshot(&sIn);
                }
                readProgress(&G.shadow);
            }
            return;
        case SS_R_GSWF:
            for (at = 0; at + 10 <= n; at += 10) {
                const int w = rd16(p + at) & 0xFF;
                const uint32_t set = rd32(p + at + 2), clr = rd32(p + at + 6);
                gp->gswf[w] = (gp->gswf[w] | set) & ~clr;
                G.shadow.gswf[w] = (G.shadow.gswf[w] | set) & ~clr;
                G.applied++;
                ssLog("merge: GSWF word %d set %08x clear %08x -> %08x", w, set, clr, gp->gswf[w]);
            }
            return;
        case SS_R_GSW:
            for (at = 0; at + 3 <= n; at += 3) {
                const int i = (int)((uint16_t)rd16(p + at)) & 0x7FF;
                if (ownWord(i)) {
                    continue; /* (each player's own) */
                }
                gp->gsw[i] = G.shadow.gsw[i] = p[at + 2];
                G.applied++;
                ssLog("merge: GSW(%d) = %d", i, p[at + 2]);
            }
            return;
        case SS_R_GSW0:
            if (n >= 4) {
                const int32_t v = (int32_t)rd32(p);
                if (v > gp->gsw0) {
                    ssLog("merge: story GSW(0) %d -> %d", gp->gsw0, v);
                    gp->gsw0 = v;
                }
                G.shadow.gsw0 = gp->gsw0;
                G.applied++;
            }
            return;
        case SS_R_LSWF:
        case SS_R_LSW:
            if (n < 5 || memcmp(p, myArea, 3) != 0) {
                return; /* another area's */
            }
            if (type == SS_R_LSWF) {
                for (at = 4; at + 10 <= n; at += 10) {
                    const int w = rd16(p + at) & 0xF;
                    const uint32_t set = rd32(p + at + 2), clr = rd32(p + at + 6);
                    gp->lswf[w] = (gp->lswf[w] | set) & ~clr;
                    G.shadow.lswf[w] = (G.shadow.lswf[w] | set) & ~clr;
                    G.applied++;
                    ssLog("merge: LSWF word %d set %08x clear %08x (area %.3s)", w, set, clr, myArea);
                }
            } else {
                for (at = 4; at + 3 <= n; at += 3) {
                    const int i = (int)((uint16_t)rd16(p + at)) & 0x3FF;
                    gp->lsw[i] = G.shadow.lsw[i] = p[at + 2];
                    G.applied++;
                    ssLog("merge: LSW(%d) = %d (area %.3s)", i, p[at + 2], myArea);
                }
            }
            return;
        case SS_R_ITEMS:
            for (at = 0; at + 4 <= n; at += 4) {
                const int l = p[at] & 3, id = rd16(p + at + 1), d = (int8_t)p[at + 3];
                int k;
                if (!itemOk(l, id)) {
                    ssLog("merge: item %d isn't one of list %d's: not taken", id, l);
                    continue;
                }
                for (k = 0; k < d; k++) {
                    addItem(l, id);
                }
                for (k = 0; k < -d; k++) {
                    removeItem(l, id);
                }
                G.applied++;
                ssLog("merge: item %d %+d (list %d)", id, d, l);
            }
            readProgress(&G.scratch);
            memcpy(G.shadow.count, G.scratch.count, sizeof(G.shadow.count)); /* (what was added or taken) */
            return;
        case SS_R_COUNTS:
            if (n >= 6) {
                int c = pd->coins + rd16(p), s = pd->starPieceCount + rd16(p + 2), h = pd->shineSpriteCount + rd16(p + 4);
                pd->coins = (s16)(c < 0 ? 0 : c > portCoinCap() ? portCoinCap() : c); /* (Coins up to 9,999: the host's setting) */
                pd->starPieceCount = (s16)clampTo(s, 0, 999); /* (as COUNTS_ABS: never past an s16) */
                pd->shineSpriteCount = (s16)clampTo(h, 0, 999);
                G.shadow.coins = pd->coins;
                G.shadow.starPieces = pd->starPieceCount;
                G.shadow.shines = pd->shineSpriteCount;
                G.applied++;
                ssLog("merge: coins %+d -> %d, star pieces %+d, shines %+d", rd16(p), pd->coins, rd16(p + 2),
                      rd16(p + 4));
                if (ssNetIsHost()) {
                    sCountsAbs = 1; /* (the guests get the host's counts as they now are) */
                }
            }
            return;
        case SS_R_COUNTS_ABS:
            if (n >= 6 && !ssNetIsHost()) {
                const int c = clampTo(rd16(p), 0, 9999), s = clampTo(rd16(p + 2), 0, 999), h = clampTo(rd16(p + 4), 0, 999);
                if (c != pd->coins || s != pd->starPieceCount || h != pd->shineSpriteCount) {
                    ssLog("merge: the host's counts: coins %d -> %d, star pieces %d -> %d, shines %d -> %d", pd->coins,
                          c, pd->starPieceCount, s, pd->shineSpriteCount, h);
                }
                pd->coins = (s16)c;
                pd->starPieceCount = (s16)s;
                pd->shineSpriteCount = (s16)h;
                G.shadow.coins = pd->coins;
                G.shadow.starPieces = pd->starPieceCount;
                G.shadow.shines = pd->shineSpriteCount;
            }
            return;
        case SS_R_STATS:
            if (n >= sizeof(Stats)) {
                Stats st; /* (copied out of the payload's odd offset, then kept to the game's ranges) */
                memcpy(&st, p, sizeof(st));
                statsClamp(&st);
                applyStats(&st, 1);
                readStats(pd, &G.shadow.stats);
                G.applied++;
            }
            return;
        case SS_R_DIGEST:
            if (n >= 16) {
                unsigned long long story, pouch, theirS, theirP;
                readProgress(&G.scratch);
                digest(&G.scratch, &story, &pouch);
                memcpy(&theirS, p, 8);
                memcpy(&theirP, p + 8, 8);
                ssLog("digest: story %s (%016llx/%016llx), pouch %s", story == theirS ? "same" : "differs", story,
                      theirS, pouch == theirP ? "same" : "differs");
                coopDigest(story == theirS && pouch == theirP); /* (a co-op guest: the host's again if it differs) */
            }
            return;
        default:
            break;
    }
}

/* ---- this game's changes out ---- */

/* a change to every other player (but G.except: a new session's, whose merge has it) */
static void diffOut(int type, const void* p, size_t n) {
    int q;
    if (G.except == 0) {
        ssNetSendRel(type, p, n);
        return;
    }
    for (q = 1; q <= SS_PLAYERS; q++) {
        if (q != G.except && q != ssNetMe()) {
            ssNetSendRelTo(q, type, p, n);
        }
    }
}

static void sendDiffs(void) {
    Progress* now = &G.scratch;
    uint8_t buf[4096];
    size_t n;
    int i, l, id;
    char myArea[4];
    readProgress(now);
    area(gp->currentMapName, myArea);
    /* GSWF */
    n = 0;
    for (i = 0; i < 0x100; i++) {
        if (now->gswf[i] != G.shadow.gswf[i] && n + 10 <= sizeof(buf)) {
            const uint16_t w = (uint16_t)i;
            const uint32_t set = now->gswf[i] & ~G.shadow.gswf[i], clr = G.shadow.gswf[i] & ~now->gswf[i];
            memcpy(buf + n, &w, 2);
            memcpy(buf + n + 2, &set, 4);
            memcpy(buf + n + 6, &clr, 4);
            n += 10;
        }
    }
    if (n) {
        diffOut(SS_R_GSWF, buf, n);
        G.sentOps++;
    }
    /* GSW */
    n = 0;
    for (i = 1; i < 0x800; i++) {
        if (now->gsw[i] != G.shadow.gsw[i] && !ownWord(i) && n + 3 <= sizeof(buf)) {
            const uint16_t w = (uint16_t)i;
            memcpy(buf + n, &w, 2);
            buf[n + 2] = now->gsw[i];
            n += 3;
        }
    }
    if (n) {
        diffOut(SS_R_GSW, buf, n);
        G.sentOps++;
    }
    if (now->gsw0 != G.shadow.gsw0) {
        diffOut(SS_R_GSW0, &now->gsw0, 4);
        G.sentOps++;
        ssLog("story GSW(0) %d -> %d here: sent", G.shadow.gsw0, now->gsw0);
    }
    /* the area's own: only within one area (a new area clears them, swReInit: not sent) */
    if (!memcmp(myArea, G.shadow.area, 4)) {
        n = 4;
        memcpy(buf, myArea, 4);
        for (i = 0; i < 0x10; i++) {
            if (now->lswf[i] != G.shadow.lswf[i]) {
                const uint16_t w = (uint16_t)i;
                const uint32_t set = now->lswf[i] & ~G.shadow.lswf[i], clr = G.shadow.lswf[i] & ~now->lswf[i];
                memcpy(buf + n, &w, 2);
                memcpy(buf + n + 2, &set, 4);
                memcpy(buf + n + 6, &clr, 4);
                n += 10;
            }
        }
        if (n > 4) {
            diffOut(SS_R_LSWF, buf, n);
            G.sentOps++;
        }
        n = 4;
        for (i = 0; i < 0x400; i++) {
            if (now->lsw[i] != G.shadow.lsw[i] && n + 3 <= sizeof(buf)) {
                const uint16_t w = (uint16_t)i;
                memcpy(buf + n, &w, 2);
                buf[n + 2] = now->lsw[i];
                n += 3;
            }
        }
        if (n > 4) {
            diffOut(SS_R_LSW, buf, n);
            G.sentOps++;
        }
    }
    /* the pouch's lists */
    n = 0;
    for (l = 0; l < LISTS; l++) {
        for (id = 1; id < IDS; id++) {
            const int d = (int)now->count[l][id] - (int)G.shadow.count[l][id];
            if (d != 0 && n + 4 <= sizeof(buf)) {
                const int16_t v = (int16_t)id;
                buf[n] = (uint8_t)l;
                memcpy(buf + n + 1, &v, 2);
                buf[n + 3] = (uint8_t)(int8_t)d;
                n += 4;
                ssLog("item %d %+d here (list %d): sent", id, d, l);
            }
        }
    }
    if (n) {
        diffOut(SS_R_ITEMS, buf, n);
        G.sentOps++;
    }
    if (now->coins != G.shadow.coins || now->starPieces != G.shadow.starPieces || now->shines != G.shadow.shines) {
        int16_t d[3];
        d[0] = (int16_t)(now->coins - G.shadow.coins);
        d[1] = (int16_t)(now->starPieces - G.shadow.starPieces);
        d[2] = (int16_t)(now->shines - G.shadow.shines);
        diffOut(SS_R_COUNTS, d, sizeof(d));
        G.sentOps++;
        ssLog("coins %+d, star pieces %+d, shines %+d here: sent", d[0], d[1], d[2]);
    }
    if (memcmp(&now->stats, &G.shadow.stats, sizeof(Stats)) != 0) {
        diffOut(SS_R_STATS, &now->stats, sizeof(Stats));
        G.sentOps++;
    }
    G.shadow = *now;
}

/* the whole progress to a player (SS_ALL: every one) */
static void sendSnapshot(int to) {
    if (modeHold()) {
        /* (a versus fight's, a raid's or a mode's lent loadout isn't anyone's progress: a guest's ask for the
           host's again, a session's merge, wait for the file's own back) */
        G.snapOwed |= 1u << (to >= 1 && to <= SS_PLAYERS ? to : 0);
        ssLog("the whole progress for player %d held: a mode's under way (sent when it's over)", to);
        return;
    }
    readProgress(&G.scratch);
    ssNetSendRelTo(to, SS_R_SNAPSHOT, &G.scratch, sizeof(Progress));
    if (to == SS_ALL) {
        ssLog("sent the whole progress for the merge (story %d)", G.scratch.gsw0);
    } else {
        ssLog("sent the whole progress to player %d for the merge (story %d)", to, G.scratch.gsw0);
    }
}

static void forgetPlayer(int p) {
    if (p >= 1 && p <= SS_PLAYERS) {
        G.pl[p].count = 0;
        G.pl[p].offsetSet = 0;
    }
}

void ssOnSession(int player, int fresh) {
    ssBattleSession(player, fresh);
    coopSession(player);
    if (fresh) {
        forgetPlayer(player);
        if (G.baseline) {
            /* this game's changes not sent yet go to the others first (the new one's merge has them);
               (in a shared battle the diffs wait: the new one's merge takes them twice then) */
            /* (not while a mode holds the progress: the pouch is a lent loadout then, and its difference
               from the file's own would go out as gains; the shadow is rebased when the mode ends) */
            if (ssNetIsHost() && !ssBattleBusy() && gp != NULL && G.seq == SEQ_GAME && !modeHold()) {
                G.except = player;
                sendDiffs();
                G.except = 0;
            }
            if (!G.watch) {
                sendSnapshot(player); /* (a watcher's progress isn't anyone's, nor a co-op guest's) */
            }
            if (!modeHold()) {
                readProgress(&G.shadow); /* (what changed while apart is in the merge, not sent again) */
            }
        }
    }
}

/* what comes during a shared battle or scene, but for the battle: kept until it's over (both games
   must stay alike while it runs) */
typedef struct Deferred {
    struct Deferred* next;
    int from;
    int type;
    size_t n;
    uint8_t data[1];
} Deferred;
static Deferred *sDeferHead, *sDeferTail;

static void deferRel(int from, int type, const uint8_t* p, size_t n) {
    Deferred* d = (Deferred*)malloc(sizeof(Deferred) + n);
    if (d == NULL) {
        return;
    }
    d->next = NULL;
    d->from = from;
    d->type = type;
    d->n = n;
    memcpy(d->data, p, n);
    if (sDeferTail != NULL) {
        sDeferTail->next = d;
    } else {
        sDeferHead = d;
    }
    sDeferTail = d;
}

static void takeRel(int from, int type, const uint8_t* p, size_t n);

static void flushDeferred(void) {
    while (sDeferHead != NULL) {
        Deferred* d = sDeferHead;
        sDeferHead = d->next;
        if (sDeferHead == NULL) {
            sDeferTail = NULL;
        }
        takeRel(d->from, d->type, d->data, d->n);
        free(d);
    }
}

void ssProgressRebase(void) {
    if (G.baseline && gp != NULL) {
        readProgress(&G.shadow);
    }
}

/* versus fights: with one player at a time; the host takes a guest's when it isn't sharing one with
   another. (A shared battle's and a scene's are any player's: statesync_battle_pc.c sorts them.) */
static int battleFrom(int from) {
    if (from == G.battlePeer) {
        return 1;
    }
    if (ssNetIsHost() && !ssBattleBusy()) {
        ssLog("battle: now with player %d (was %d)", from, G.battlePeer);
        G.battlePeer = from;
        return 1;
    }
    return 0;
}

static void takeRel(int from, int type, const uint8_t* p, size_t n) {
    if (type == SS_R_COOP) {
        coopRel(from, p, n);
        return;
    }
    if (type == SS_R_COOP_SAVE) {
        coopSaveRel(from, p, n);
        return;
    }
    if (type == SS_R_COOP_ACT) {
        coopActRel(from, p, n); /* (a guest's field action, the host's answer: never deferred, never passed on) */
        return;
    }
    if (type < SS_R_BATTLE && (ssBattleBusy() || modeHold() || (type >= SS_R_NPC_DEAD && gp != NULL && gp->inBattle) ||
                               (type < SS_R_NPC_DEAD && !G.baseline))) {
        /* (no file's game here yet: the other's progress waits for it, its whole merge first) */
        deferRel(from, type, p, n); /* (the room's news in a battle of this player's own: for the field after) */
        return;
    }
    if (type >= SS_R_NPC_DEAD && type < SS_R_BATTLE) {
        ssRoomRel(type, p, n);
    } else if (type >= SS_R_MODES && type < SS_R_MODES_END) {
        ssModesRel(from, type, p, n); /* (the party modes: statesync_modes_pc.c) */
    } else if (type == SS_R_BATTLE || type == SS_R_SETTINGS) {
        if (type == SS_R_BATTLE && ssNetIsHost() && !ssBattleBusy()) {
            (void)battleFrom(from); /* (the one a scene or a versus fight would be with) */
        }
        ssBattleRel(from, type, p, n);
    } else if (type == SS_R_SCENE) {
        ssBattleRel(from, type, p, n); /* (a scene pulls in every player: up to three pull this game) */
    } else if (type >= SS_R_BATTLE) {
        if (battleFrom(from)) {
            ssBattleRel(from, type, p, n);
        } else {
            ssLog("battle: player %d's message (type %d) while sharing with player %d: not taken", from, type,
                  G.battlePeer);
        }
    } else {
        applyRel(type, p, n);
    }
}

/* the host: a guest's message the others need too, tagged with where it came from */
static void forward(int origin, int rel, int type, const uint8_t* p, size_t n, const char* map) {
    uint8_t buf[2 + 65536];
    uint8_t* m = n + 2 <= sizeof(buf) ? buf : (uint8_t*)malloc(n + 2);
    int q;
    if (m == NULL) {
        return;
    }
    m[0] = (uint8_t)origin;
    m[1] = (uint8_t)type;
    memcpy(m + 2, p, n);
    for (q = 2; q <= SS_PLAYERS; q++) {
        if (q == origin) {
            continue;
        }
        if (rel) {
            ssNetSendRelTo(q, SS_R_FWD, m, n + 2);
        } else if (ssNetLinked(q) && (map == NULL || (G.pl[q].info.valid && !strncmp(G.pl[q].info.map, map, 16)))) {
            ssNetSendTo(q, SS_MSG_FWD, m, n + 2);
        }
    }
    if (m != buf) {
        free(m);
    }
}

/* the reliable ones every game takes: the progress (the host is where they all meet), the rooms' */
static int shared(int type) {
    return (type >= SS_R_SNAPSHOT && type <= SS_R_STATS) || (type >= SS_R_NPC_DEAD && type <= SS_R_MOBJ_HIT) ||
           (type >= SS_R_MODES && type < SS_R_MODES_END);
}

/* the host: a guest's message for other players (to: by bit, player p 1 << (p - 1)): this game's
   taken here, the others' passed on tagged with its origin */
static void passOn(int origin, unsigned to, int type, const uint8_t* p, size_t n) {
    uint8_t buf[2 + 4096];
    uint8_t* m = n + 2 <= sizeof(buf) ? buf : (uint8_t*)malloc(n + 2);
    int q;
    if (m == NULL) {
        return;
    }
    m[0] = (uint8_t)origin;
    m[1] = (uint8_t)type;
    memcpy(m + 2, p, n);
    for (q = 2; q <= SS_PLAYERS; q++) {
        if ((to & (1u << (q - 1))) && q != origin) {
            ssNetSendRelTo(q, SS_R_FWD, m, n + 2);
        }
    }
    if (m != buf) {
        free(m);
    }
    if (to & 1u) {
        takeRel(origin, type, p, n);
    }
}

void ssBattleSendTo(unsigned mask, int type, const void* p, size_t n) {
    const int me = ssNetMe();
    int q;
    if (me >= 1 && me <= SS_PLAYERS) {
        mask &= ~(1u << (me - 1));
    }
    if (mask == 0) {
        return;
    }
    if (ssNetIsHost()) {
        for (q = 2; q <= SS_PLAYERS; q++) {
            if (mask & (1u << (q - 1))) {
                ssNetSendRelTo(q, type, p, n);
            }
        }
    } else if (mask == 1u) {
        ssNetSendRelTo(1, type, p, n); /* (the host's alone: as it is) */
    } else {
        /* (for other guests too: once to the host, which passes it on) */
        uint8_t buf[2 + 4096];
        uint8_t* m = n + 2 <= sizeof(buf) ? buf : (uint8_t*)malloc(n + 2);
        if (m == NULL) {
            return;
        }
        m[0] = (uint8_t)(0x80u | (mask & 0x0Fu));
        m[1] = (uint8_t)type;
        memcpy(m + 2, p, n);
        ssNetSendRelTo(1, SS_R_FWD, m, n + 2);
        if (m != buf) {
            free(m);
        }
    }
}

void ssOnRel(int from, int type, const uint8_t* p, size_t n) {
    if (type == SS_R_FWD) {
        if (ssNetIsHost() && from >= 2 && from <= SS_PLAYERS && n >= 2 && (p[0] & 0x80u)) {
            passOn(from, p[0] & 0x0Fu, p[1], p + 2, n - 2); /* (a guest's battle message for others) */
        } else if (!ssNetIsHost() && from == 1 && n >= 2 && p[0] >= 2 && p[0] <= SS_PLAYERS && p[0] != ssNetMe()) {
            takeRel(p[0], p[1], p + 2, n - 2);
        }
        return;
    }
    if (ssNetIsHost() && from >= 2 && shared(type) && !(type == SS_R_SNAPSHOT && coopTogether())) {
        forward(from, 1, type, p, n, NULL);
    }
    takeRel(from, type, p, n);
}

/* ---- the players' places ---- */

static const char* poseModel(s32 pose) {
    if (pose < 0) {
        return "";
    }
    return animPoseGetAnimBaseDataPtr(pose)->fileName;
}

/* ---- the players' names and Lua mods ---- */

/* A name as it came (at most cap bytes, maybe not ended) as one this game shows: whole UTF-8
 * characters only, no control characters (C0, DEL, C1, the line and paragraph separators, the
 * direction overrides), no markup the game's text or the menus read (< > { }), no spaces at its
 * ends, at most size - 1 bytes. */
static void cleanName(const char* in, size_t cap, char* out, size_t size) {
    const unsigned char* p = (const unsigned char*)in;
    const unsigned char* end = p;
    size_t n = 0;
    while ((size_t)(end - p) < cap && *end != '\0') {
        end++;
    }
    while (p < end && n + 1 < size) {
        unsigned c = *p;
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        int k, ok = len > 0 && p + len <= end;
        for (k = 1; ok && k < len; k++) {
            ok = (p[k] & 0xC0) == 0x80;
        }
        if (!ok) {
            p++; /* (not UTF-8: left out) */
            continue;
        }
        if (len > 1) {
            c &= len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07;
            for (k = 1; k < len; k++) {
                c = (c << 6) | (p[k] & 0x3F);
            }
        }
        if (!(c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0) || c == '<' || c == '>' || c == '{' || c == '}' ||
              c == 0x2028 || c == 0x2029 || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) ||
              (len == 2 && c < 0x80) || (len == 3 && c < 0x800) || (len == 4 && c < 0x10000) ||
              (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF || (c == ' ' && n == 0))) {
            if (n + (size_t)len + 1 > size) {
                break;
            }
            memcpy(out + n, p, (size_t)len);
            n += (size_t)len;
        }
        p += len;
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
}

/* this game's player's name: the Online tab's "Your name" ($TTYD_SYNC_NAME: tests, the headless
   build), cleaned as the others' are */
static const char* ownName(void) {
    static char sName[SS_NAME];
    const char* name = getenv("TTYD_SYNC_NAME");
#ifdef TTYD_BACKEND_AURORA
    if (name == NULL) {
        name = portOnlineMenuName();
    }
#endif
    cleanName(name != NULL ? name : "", name != NULL ? 64 : 1, sName, sizeof(sName)); /* ("": its one byte) */
    return sName;
}

static const char* ownLua(void) {
#ifdef PORT_MODS
    return portLuaHash();
#else
    return "";
#endif
}

const char* portStateSyncPlayerName(int player) {
    if (!G.on || player < 1 || player > SS_PLAYERS) {
        return "";
    }
    if (player == ssNetMe()) {
        return ownName();
    }
    /* (kept while they're quiet: a shared battle's wait for them is when their messages stop) */
    return G.pl[player].info.valid ? G.pl[player].name : "";
}

/* The release's font: Latin-1 on the US and EU releases, ASCII on the JP one (its font's
   Shift-JIS has no Latin-1); '?' for what it can't show (pause_online_aurora.c's gameText) */
void portStateSyncPlayerNameShown(int player, char* out, size_t size) {
    const unsigned char* p = (const unsigned char*)portStateSyncPlayerName(player);
    const unsigned char* end = p + strlen((const char*)p); /* (an end the analyzer can see) */
    const int jp = strcmp(PORT_GAME_ID, "G8MJ01") == 0;
    size_t n = 0;
    if (size == 0) {
        return;
    }
    while (p < end && n + 1 < size) {
        unsigned c = *p++;
        if (c >= 0x80) {
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            c &= more == 3 ? 0x07 : more == 2 ? 0x0F : 0x1F;
            for (; more > 0 && p < end && (*p & 0xC0) == 0x80; more--) {
                c = (c << 6) | (*p++ & 0x3F);
            }
        }
        if ((jp && (c >= 0x80 || c == '\\' || c == '`')) || c > 0xFF) {
            c = '?';
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
}

int portStateSyncLuaSame(void) {
    int p;
    if (!G.on) {
        return 1;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (p != ssNetMe() && present(p) && strcmp(G.pl[p].lua, ownLua()) != 0) {
            return 0;
        }
    }
    return 1;
}

int portStateSyncAllTravel(void) {
    int p;
    if (!G.on) {
        return 1;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (p != ssNetMe() && present(p) && !G.pl[p].info.watch && !G.pl[p].travel) {
            return 0;
        }
    }
    return 1;
}

static void sendPlayer(void) {
    PlayerMsg m;
    MarioWork* mario = marioGetPtr();
    PartyEntry* party;
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "%s", ownName());
    snprintf(m.lua, sizeof(m.lua), "%s", ownLua());
    m.travel = portGameplus("fast_travel") != 0;
    m.ms = (uint32_t)ssNetMs();
    snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
    m.seq = G.seq;
    m.inBattle = (uint8_t)(gp->inBattle != 0);
    m.keysOff = (uint8_t)(G.seq == SEQ_GAME && !marioChkKey());
    m.owns = (uint8_t)G.owns;
    m.roomFrames = G.roomFrames;
    m.playAs = portPlayAs();
    m.player = ssNetMe();
    m.watch = G.watch;
    m.coop = (uint8_t)portStateSyncCoopGuest();
    if (m.coop) {
        m.keysOff = (uint8_t)(G.seq == SEQ_GAME && winCheck()); /* (its keys are off: it follows; busy in a menu) */
    }
    {
        const WindowWork* w = winGetPtr();
        /* (the host's: its own Start menu, or the guest playing Mario's, which holds this world too) */
        m.paused = (uint8_t)(G.seq == SEQ_GAME && ((w != NULL && w->menuState > 0 && w->menuState < 100) ||
                                                   portStateSyncCoopMarioMenuHeld()));
    }
    portPlayerMenuCard(m.card);
    coopFillHelpers(&m);
    m.battleId = ssBattleSharedId();
    if (mario != NULL) {
        m.x = mario->position.x;
        m.y = mario->position.y;
        m.z = mario->position.z;
        m.dir = mario->displayDirection;
        if (G.seq == SEQ_GAME) {
            snprintf(m.model, sizeof(m.model), "%s", poseModel(mario->paperAnimGroupId[marioAnimeId()]));
            snprintf(m.anim, sizeof(m.anim), "%s", mario->animName != NULL ? mario->animName : "");
        }
    }
    party = G.seq == SEQ_GAME ? partyGetPtr(marioGetPartyId()) : NULL;
    if (party != NULL && party->paperAnimGroup >= 0) {
        m.hasPartner = 1;
        m.partner = party->currentMemberId;
        m.px = party->position.x;
        m.py = party->position.y;
        m.pz = party->position.z;
        m.pdir = party->displayDirection;
        snprintf(m.pmodel, sizeof(m.pmodel), "%s", poseModel(party->paperAnimGroup));
        snprintf(m.panim, sizeof(m.panim), "%s", party->animName != NULL ? party->animName : "");
        m.pm64 = portPm64PartnerOn(party->currentMemberId);
    }
    {
        /* the costume this player is seen in (unlocked, fitting their pick: portCostumeDraw's), none else */
        const int c = portCostumeCurrent();
        m.costume = c > 0 && portCostumeUnlocked(c - 1) && portCostumeFits(c - 1) ? c : 0;
    }
    m.modeFloor = myModeFloor();
    party = G.seq == SEQ_GAME && portBrothersInField() ? portBrothersFieldLuigi() : NULL;
    if (party != NULL && party->paperAnimGroup >= 0) {
        m.hasLuigi = 1;
        m.lx = party->position.x;
        m.ly = party->position.y;
        m.lz = party->position.z;
        m.ldir = party->displayDirection;
        snprintf(m.lmodel, sizeof(m.lmodel), "%s", poseModel(party->paperAnimGroup));
        snprintf(m.lanim, sizeof(m.lanim), "%s", party->animName != NULL ? party->animName : "");
    }
    if (ssNetIsHost()) {
        coopOwnFill(&m);
        ndHostSent();
    }
    ssNetSend(SS_MSG_PLAYER, &m, sizeof(m));
}

static void gotPlayer(int from, const PlayerMsg* m) {
    Other* o = &G.pl[from];
    const unsigned long long now = ssNetMs();
    const long long off = (long long)(uint32_t)now - (long long)m->ms;
    const int last = (o->head + SAMPLES - 1) % SAMPLES;
    if (o->count > 0 && (int32_t)(m->ms - o->s[last].ms) <= 0) {
        return; /* older than the newest (a reordered one) */
    }
    if (!o->offsetSet || off < o->offset) {
        o->offset = off;
        o->offsetSet = 1;
    } else {
        o->offset += (off - o->offset) / 64; /* their clock drifting: follow slowly */
    }
    if (o->count > 0 && strcmp(o->s[last].map, m->map) != 0) {
        ssLog("peer: player %d moved to %s (from %s)", from, m->map, o->s[last].map);
        o->count = 0; /* nothing to blend with across rooms */
    }
    o->s[o->head] = *m;
    o->at[o->head] = now;
    o->head = (o->head + 1) % SAMPLES;
    if (o->count < SAMPLES) {
        o->count++;
    }
    if (!o->info.valid || now - o->lastAt > AWAY_MS) {
        ssLog("peer: player %d is here in this online game (in %s)", from, m->map);
    }
    o->lastAt = now;
    o->info.valid = 1;
    snprintf(o->info.map, sizeof(o->info.map), "%s", m->map);
    o->info.seq = m->seq;
    o->info.inBattle = m->inBattle;
    o->info.keysOff = m->keysOff;
    o->info.paused = m->paused;
    memcpy(o->info.card, m->card, sizeof(o->info.card));
    o->info.owns = m->owns;
    o->info.roomFrames = m->roomFrames;
    o->info.watch = m->watch;
    o->info.coop = m->coop;
    o->info.battleId = m->battleId;
    o->info.modeFloor = m->modeFloor;
    if ((m->travel != 0) != o->travel) {
        o->travel = m->travel != 0;
        ssLog("peer: player %d's fast travel is %s", from, o->travel ? "on" : "off");
    }
    {
        char name[SS_NAME], lua[sizeof(m->lua)];
        cleanName(m->name, sizeof(m->name), name, sizeof(name));
        if (strcmp(name, o->name) != 0) {
            snprintf(o->name, sizeof(o->name), "%s", name);
            ssLog("peer: player %d's name: \"%s\"", from, name);
        }
        snprintf(lua, sizeof(lua), "%.*s", (int)sizeof(m->lua) - 1, m->lua);
        if (strcmp(lua, o->lua) != 0) {
            snprintf(o->lua, sizeof(o->lua), "%s", lua);
            if (strcmp(o->lua, ownLua()) != 0) {
                ssLog("peer: player %d's Lua mods aren't this game's: they're paused (HUD-only ones still draw)",
                      from);
            }
        }
    }
}

/* who owns this game's room's NPCs and objects: this game (its number), the lowest-numbered other
   player here who says so, 0 none */
static int roomOwner(void) {
    int p;
    if (G.owns) {
        return ssNetMe();
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (hereIn(p) && G.pl[p].info.owns) {
            return p;
        }
    }
    return 0;
}

/* a peer's number: as it is when finite and within any map, else 0 */
static float saneFloat(float v) {
    return isfinite(v) && fabsf(v) < 1e6f ? v : 0.0f;
}

void ssOnMessage(int from, int type, const uint8_t* p, size_t n) {
    if (type == SS_MSG_FWD) {
        if (!ssNetIsHost() && from == 1 && n >= 2 && p[0] >= 2 && p[0] <= SS_PLAYERS && p[0] != ssNetMe()) {
            ssOnMessage(p[0], p[1], p + 2, n - 2);
        }
        return;
    }
    if (from < 1 || from > SS_PLAYERS) {
        return;
    }
    if (type == SS_MSG_PAD) {
        gotPad(from, p, n); /* (a co-op guest's controller: the host's alone) */
        return;
    }
    if (type == SS_MSG_OWN) {
        gotOwn(from, p, n); /* (a co-op guest's own character, as its game walks it: the host's alone) */
        return;
    }
    if (ssNetIsHost() && from >= 2 && (type == SS_MSG_PLAYER || type == SS_MSG_NPCS || type == SS_MSG_MOBJS)) {
        /* (a room's lists only to those in that room) */
        forward(from, 0, type, p, n, type == SS_MSG_PLAYER || n < 16 ? NULL : (const char*)p);
    }
    if (type == SS_MSG_PLAYER && n >= sizeof(PlayerMsg)) {
        PlayerMsg m;
        int k;
        memcpy(&m, p, sizeof(m));
        /* (every name ends inside its field; every place is a number within any map) */
        m.map[15] = m.model[23] = m.anim[31] = m.pmodel[23] = m.panim[31] = m.lmodel[23] = m.lanim[31] = '\0';
        m.lua[sizeof(m.lua) - 1] = m.name[sizeof(m.name) - 1] = '\0';
        for (k = 0; k < 2; k++) {
            m.hmodel[k][23] = m.hanim[k][31] = '\0';
            m.hx[k] = saneFloat(m.hx[k]), m.hy[k] = saneFloat(m.hy[k]), m.hz[k] = saneFloat(m.hz[k]);
            m.hdir[k] = saneFloat(m.hdir[k]);
        }
        m.x = saneFloat(m.x), m.y = saneFloat(m.y), m.z = saneFloat(m.z), m.dir = saneFloat(m.dir);
        m.px = saneFloat(m.px), m.py = saneFloat(m.py), m.pz = saneFloat(m.pz), m.pdir = saneFloat(m.pdir);
        m.lx = saneFloat(m.lx), m.ly = saneFloat(m.ly), m.lz = saneFloat(m.lz), m.ldir = saneFloat(m.ldir);
        gotPlayer(from, &m);
    } else if (gp != NULL && from == roomOwner() && from != ssNetMe()) {
        ssRoomMessage(type, p, n); /* (the room's owner's only) */
    }
}

const SsPeer* ssPeer(void) {
    return G.battlePeer >= 1 && G.battlePeer <= SS_PLAYERS && present(G.battlePeer) ? &G.pl[G.battlePeer].info
                                                                                     : &kNobody;
}

int ssBattlePeer(void) {
    return G.battlePeer;
}

const SsPeer* ssPeerOf(int player) {
    return present(player) ? &G.pl[player].info : &kNobody;
}

/* wait_peer's WHAT (input_pc.c): "card", "paused" (the host's), "here" (linked at all), "battle" (in a
   battle), "field" (in the field: seq 2), "room" (in this game's room), "coop" (a co-op guest following the
   host), "free" (in the field, keys on), "busy" (a player not free), "map=NAME" (in that room); a count after it (e.g. "here3", "room2") asks that many other players at
   least, else one; '!' before it: none shows it */
int portStateSyncPeerShows(const char* what) {
    const int negate = what[0] == '!';
    char name[24];
    size_t n;
    int p, shows = 0, need = 1;
    snprintf(name, sizeof(name), "%s", negate ? what + 1 : what);
    n = strlen(name);
    if (strncmp(name, "map=", 4) != 0) {
        while (n > 0 && name[n - 1] >= '0' && name[n - 1] <= '9') {
            n--;
        }
        if (name[n] != '\0') {
            need = atoi(name + n);
            name[n] = '\0';
        }
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        const SsPeer* o = ssPeerOf(p);
        if (!o->valid) {
            continue;
        }
        if ((strcmp(name, "card") == 0 && (o->card[0] & 1)) || (strcmp(name, "paused") == 0 && p == 1 && o->paused) ||
            strcmp(name, "here") == 0 || (strcmp(name, "battle") == 0 && o->inBattle) ||
            (strcmp(name, "field") == 0 && o->seq == 2 && !o->inBattle) || (strcmp(name, "coop") == 0 && o->coop) ||
            (strcmp(name, "room") == 0 && gp != NULL && strcmp(o->map, gp->currentMapName) == 0) ||
            /* (free: in the field with its keys on, as a versus fight or a party game asks of the others; busy:
               a player, not a watcher, who isn't: on the title, loading, in a battle, a scene or a menu) */
            (strcmp(name, "free") == 0 && !o->watch && o->seq == 2 && !o->inBattle && !o->keysOff) ||
            (strcmp(name, "busy") == 0 && !o->watch && (o->seq != 2 || o->inBattle || o->keysOff)) ||
            (strncmp(name, "map=", 4) == 0 && strcmp(o->map, name + 4) == 0)) {
            shows++;
        }
    }
    return negate ? shows == 0 : shows >= need;
}

int ssPlayerHere(int player) {
    return gp != NULL && hereIn(player);
}

int ssNearestOther(float x, float y, float z, float* ox, float* oy, float* oz) {
    MarioWork* mario = marioGetPtr();
    float best;
    int p, found = 0;
    if (gp == NULL || mario == NULL) {
        return 0;
    }
    best = (mario->position.x - x) * (mario->position.x - x) + (mario->position.z - z) * (mario->position.z - z);
    for (p = 1; p <= SS_PLAYERS; p++) {
        const PlayerMsg* s;
        float d;
        if (p == ssNetMe() || !hereIn(p) || G.pl[p].info.watch || G.pl[p].info.keysOff || G.pl[p].info.inBattle ||
            G.pl[p].count == 0) {
            continue;
        }
        s = &G.pl[p].s[(G.pl[p].head + SAMPLES - 1) % SAMPLES];
        d = (s->x - x) * (s->x - x) + (s->z - z) * (s->z - z);
        (void)y;
        if (d < best) {
            best = d;
            *ox = s->x;
            *oy = s->y;
            *oz = s->z;
            found = 1;
        }
    }
    return found;
}

int ssBattleLinked(void) {
    return G.battlePeer != 0 && ssNetLinked(G.battlePeer);
}

/* the battle's player in this game's room (statesync_battle_pc.c) */
int ssSameRoom(void) {
    return gp != NULL && ssBattleLinked() && hereIn(G.battlePeer);
}

/* who a shared battle or scene would be with: a guest's, the host; the host's, while none runs, a
   guest free in its room (the lowest number), else one in its room, else any */
static void pickBattlePeer(void) {
    int p, pick = 0;
    if (!ssNetIsHost()) {
        G.battlePeer = 1;
        return;
    }
    if (ssBattleBusy() || ssBattleHeld()) {
        return;
    }
    if (G.battlePeer >= 2 && hereIn(G.battlePeer)) {
        return; /* (kept while it's in this room) */
    }
    for (p = 2; p <= SS_PLAYERS && pick == 0; p++) {
        if (hereIn(p) && !G.pl[p].info.inBattle && !G.pl[p].info.keysOff && !G.pl[p].info.watch) {
            pick = p;
        }
    }
    for (p = 2; p <= SS_PLAYERS && pick == 0; p++) {
        if (hereIn(p) && !G.pl[p].info.watch) {
            pick = p;
        }
    }
    for (p = 2; p <= SS_PLAYERS && pick == 0; p++) {
        if (present(p) && !G.pl[p].info.watch) {
            pick = p;
        }
    }
    if (pick != 0 && pick != G.battlePeer) {
        ssLog("battle: shared battles and scenes now with player %d", pick);
        G.battlePeer = pick;
    }
}

static float lerpAngle(float a, float b, float t) {
    float d = fmodf(b - a, 360.0f);
    if (d > 180.0f) {
        d -= 360.0f;
    } else if (d < -180.0f) {
        d += 360.0f;
    }
    return a + d * t;
}

/* player p where their game was INTERP_MS ago, between the two samples around it */
static void between(SsGhostPose* out, float t, float ax, float ay, float az, float adir, float bx, float by, float bz,
                    float bdir, const char* model, const char* aanim, const char* banim) {
    out->x = ax + (bx - ax) * t;
    out->y = ay + (by - ay) * t;
    out->z = az + (bz - az) * t;
    out->dir = lerpAngle(adir, bdir, t);
    snprintf(out->model, sizeof(out->model), "%s", model);
    snprintf(out->anim, sizeof(out->anim), "%s", t < 0.5f ? aanim : banim);
    out->chr = 0;
    out->costume = 0;
    out->pm64 = 0;
}

/* the newest word as it is, no interpolation (a co-op guest's own character: drawn with no buffer's delay) */
static int sInterpNewest;

/* the time a player's ghost is drawn at: INTERP_MS behind their clock (smooth), or their newest word */
static long long interpTarget(int p) {
    if (sInterpNewest && G.pl[p].count > 0) {
        return (long long)G.pl[p].s[(G.pl[p].head + SAMPLES - 1) % SAMPLES].ms;
    }
    return (long long)(uint32_t)ssNetMs() - G.pl[p].offset - INTERP_MS;
}

static int interpolate(int p, SsGhostPose* me, SsGhostPose* partner, int* hasPartner, SsGhostPose* luigi,
                       int* hasLuigi) {
    const long long target = interpTarget(p);
    const PlayerMsg *a = NULL, *b = NULL;
    float t = 0.0f;
    int i;
    if (G.pl[p].count == 0) {
        return 0;
    }
    for (i = 0; i < G.pl[p].count; i++) {
        const PlayerMsg* s = &G.pl[p].s[(G.pl[p].head + SAMPLES - G.pl[p].count + i) % SAMPLES];
        if ((long long)s->ms <= target) {
            a = s;
        } else {
            b = s;
            break;
        }
    }
    if (a == NULL) {
        a = b; /* all newer than the target (just came): the oldest */
        b = NULL;
    }
    if (a == NULL) {
        return 0; /* (never: count > 0 gives one or the other; the bound for the analyzer) */
    }
    if (b != NULL && b->ms != a->ms) {
        t = (float)(target - (long long)a->ms) / (float)(b->ms - a->ms);
    } else {
        b = a;
    }
    me->x = a->x + (b->x - a->x) * t;
    me->y = a->y + (b->y - a->y) * t;
    me->z = a->z + (b->z - a->z) * t;
    me->dir = lerpAngle(a->dir, b->dir, t);
    snprintf(me->model, sizeof(me->model), "%s", a->model);
    snprintf(me->anim, sizeof(me->anim), "%s", t < 0.5f ? a->anim : b->anim);
    me->chr = a->playAs;
    me->costume = a->costume;
    *hasPartner = a->hasPartner && b->hasPartner && !strcmp(a->pmodel, b->pmodel);
    if (*hasPartner) {
        between(partner, t, a->px, a->py, a->pz, a->pdir, b->px, b->py, b->pz, b->pdir, a->pmodel, a->panim, b->panim);
        partner->pm64 = a->pm64; /* their own pick, drawn on their partner's ghost */
    }
    *hasLuigi = a->hasLuigi && b->hasLuigi && !strcmp(a->lmodel, b->lmodel);
    if (*hasLuigi) {
        between(luigi, t, a->lx, a->ly, a->lz, a->ldir, b->lx, b->ly, b->lz, b->ldir, a->lmodel, a->lanim, b->lanim);
    }
    return 1;
}

/* ---- the frame ---- */

static void watchSet(void);
static void modesFrame(void); /* statesync_modes_pc.inc */

int portStateSyncOn(void) {
    if (!G.checked) {
        const char* v = getenv("TTYD_SYNC_LOG");
        G.checked = 1;
        G.on = ssNetStart(); /* (tests: the environment's; players: the menu, the launcher, an invite) */
        G.logEvery = v != NULL && (int)strtol(v, NULL, 10) > 0 ? (unsigned)(int)strtol(v, NULL, 10) : 60;
        G.lastSeq = -1;
        if (G.on) {
            watchSet();
        }
    }
    return G.on;
}

int portStateSyncLinked(void) {
    return portStateSyncOn() && ssNetConnected();
}

static void roomFrame(void) {
    const int me = ssNetMe();
    const int follows = G.watch || portStateSyncCoopGuest(); /* (the host's world: never this game's room) */
    int same = 0, owner = 0, lowest = follows ? 0 : me, p;
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (hereIn(p) && !G.pl[p].info.watch) { /* (a watcher never owns a room) */
            same = 1;
            if (owner == 0 && G.pl[p].info.owns) {
                owner = p;
            }
            if (lowest == 0 || p < lowest) {
                lowest = p;
            }
        }
    }
    if (strcmp(G.map, gp->currentMapName) != 0 || G.seq != SEQ_GAME) {
        if (strcmp(G.map, gp->currentMapName) != 0) {
            snprintf(G.map, sizeof(G.map), "%s", gp->currentMapName);
            G.roomFrames = 0;
            G.owns = 0;
            ssRoomReset();
        }
    } else {
        G.roomFrames++;
    }
    /* who owns the room's enemies and objects: whoever was here first among everyone (one coming
       into a room another owns follows it); at once (two claim, or none does), the lowest player
       number: the host first */
    if (G.seq == SEQ_GAME) {
        if (follows && ssNetConnected()) {
            G.owns = 0;
        } else if (!same) {
            G.owns = 1;
        } else if (G.owns && owner != 0 && owner < me) {
            G.owns = 0;
            ssLog("room: player %d and this game both claimed %s; player %d keeps it", owner, G.map, owner);
        } else if (!G.owns && owner == 0 && lowest == me) {
            G.owns = 1;
        }
    }
    ssRoomFrame(G.frame, same, G.owns);
}

/* Tests (headless has no menu): the Start menu's actions at a frame, as a player would choose them.
 * $TTYD_SYNC_MENU_JOIN_AT=<frame>:<code or address> joins then (portStateSyncJoin; with
 * TTYD_SYNC_TEST_RELAY for a code), $TTYD_SYNC_MENU_HOST_AT=<frame>[:address] hosts a room (or by
 * address: on $TTYD_SYNC_PORT), $TTYD_SYNC_LEAVE_AT=<frame> leaves (portStateSyncLeave). */
static void menuTests(void) {
    static unsigned sFrame;
    static int sRead, sJoinAt = -1, sHostAt = -1, sLeaveAt = -1, sHostAddress;
    static char sJoin[64];
    sFrame++;
    if (!sRead) {
        const char* v;
        sRead = 1;
        if ((v = getenv("TTYD_SYNC_MENU_JOIN_AT")) != NULL && strchr(v, ':') != NULL) {
            sJoinAt = (int)strtol(v, NULL, 10);
            snprintf(sJoin, sizeof(sJoin), "%s", strchr(v, ':') + 1);
        }
        if ((v = getenv("TTYD_SYNC_MENU_HOST_AT")) != NULL) {
            sHostAt = (int)strtol(v, NULL, 10);
            sHostAddress = strstr(v, ":address") != NULL;
        }
        if ((v = getenv("TTYD_SYNC_LEAVE_AT")) != NULL) {
            sLeaveAt = (int)strtol(v, NULL, 10);
        }
    }
    if ((int)sFrame == sJoinAt) {
        ssLog("test: the menu's Join with %s: %s", sJoin, portStateSyncJoin(sJoin) ? "on" : portStateSyncNote());
    }
    if ((int)sFrame == sHostAt) {
        const char* port = getenv("TTYD_SYNC_PORT");
        ssLog("test: the menu's Host%s: %s", sHostAddress ? " by address" : " a room",
              portStateSyncHost(sHostAddress, port != NULL ? (int)strtol(port, NULL, 10) : 0) ? portStateSyncCode()
                                                                            : portStateSyncNote());
    }
    if ((int)sFrame == sLeaveAt) {
        const unsigned long long t0 = ssNetMs();
        ssLog("test: the menu's Leave");
        portStateSyncLeave();
        /* (the game never waits on the network: a relay thread stuck connecting is let go, not waited for) */
        ssLog("test: Leave took %llu ms", (unsigned long long)(ssNetMs() - t0));
    }
    /* $TTYD_SYNC_MENU_AT=<frame>:<pit|tower|versus|raid>[,<frame>:...]: the Online tab's rows for the modes,
       chosen at those frames (versus+N: the opponent N steps on from the first one). A refusal that can clear (the
       other game not connected yet, busy, or a shared battle or scene under way) is tried again each frame
       for up to $TTYD_SYNC_MENU_RETRY frames (3600): a loaded machine's other game once reached the field
       after the one frame named, and the fight never happened. */
    {
        enum { MENU_AT_ROWS = 32 }; /* (testpass.py's lint menu_at_rows keeps scenarios within it) */
        static const char* sAt;
        static int sAtRead, sRetry, sTooMany;
        static unsigned char sDone[MENU_AT_ROWS], sStepped[MENU_AT_ROWS], sSaid[MENU_AT_ROWS];
        const char* v;
        int row = 0;
        if (!sAtRead) {
            const char* r = getenv("TTYD_SYNC_MENU_RETRY");
            sAtRead = 1;
            sAt = getenv("TTYD_SYNC_MENU_AT");
            sRetry = r != NULL && *r ? (int)strtol(r, NULL, 10) : 3600;
        }
        for (v = sAt; v != NULL && *v != '\0'; row++) {
            const char* what = strchr(v, ':');
            const char* why = "";
            int ok = 0;
            const int at = (int)strtol(v, NULL, 10);
            if (what == NULL) {
                break;
            }
            what++;
            if (row >= MENU_AT_ROWS && !sTooMany) {
                /* (never skipped without a word: a check on a later effect alone would pass untried) */
                sTooMany = 1;
                ssLog("test: TTYD_SYNC_MENU_AT has more than %d rows: row %d on never run", MENU_AT_ROWS, row + 1);
            }
            if (row < MENU_AT_ROWS && !sDone[row] && (int)sFrame >= at) {
                if (!strncmp(what, "pit", 3) || !strncmp(what, "tower", 5)) {
                    ok = portStateSyncTogether(what[0] == 'p' ? PORT_TOGETHER_PIT : PORT_TOGETHER_TOWER, &why);
                } else if (!strncmp(what, "versus", 6)) {
                    int k;
                    /* (stepped once: a retry asks the same opponent again. Counted from the first opponent, not
                       the battle peer of the moment: that moves to whoever last shared a battle or scene, and
                       versus+1 once wrapped from player 3 back to player 2) */
                    if (what[6] == '+' && !sStepped[row]) {
                        portStateSyncVersusStep(0);
                    }
                    for (k = what[6] == '+' && !sStepped[row] ? (int)strtol(what + 7, NULL, 10) : 0; k > 0; k--) {
                        portStateSyncVersusStep(1);
                    }
                    sStepped[row] = 1;
                    ok = portStateSyncVersus(&why);
                } else if (!strncmp(what, "raid", 4)) {
#ifdef TTYD_BACKEND_AURORA
                    ok = portRaidStart(&why);
#else
                    why = "Not in this build.";
#endif
                }
                {
                    const char* said = why != NULL ? why : "";
                    const int clears = !ok && (strstr(said, "Not connected") != NULL || strstr(said, "busy") != NULL ||
                                               strstr(said, "under way") != NULL);
                    const int last = !clears || (int)sFrame - at >= sRetry;
                    if (ok || last) {
                        sDone[row] = 1;
                        ssLog("test: the menu's %.*s: %s%s", (int)strcspn(what, ","), what, ok ? "begun" : said,
                              !ok && clears ? " (tried until the deadline)" : "");
                    } else if (!sSaid[row]) {
                        sSaid[row] = 1; /* (the first refusal said once, then tried each frame) */
                        ssLog("test: the menu's %.*s: %s (trying again each frame)", (int)strcspn(what, ","), what,
                              said);
                    }
                }
            }
            v = strchr(what, ',');
            v = v != NULL ? v + 1 : NULL;
        }
    }
}

/* ---- watchers ----
 * A watcher's game follows the host's: warped to the host's room as the host goes, its own Mario and
 * partner hidden and kept where the host's Mario is (the camera follows it), the host's ghost drawn
 * there. It sends no progress (it takes the others', as any player's game does), owns no room, isn't
 * drawn in the others' games and isn't asked into scenes or versus fights. A shared battle takes it
 * as a watcher: it plays the battle from the players' controllers and is never waited for
 * (statesync_battle_pc.c). */

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture */

static int sWatchNext; /* the menu's choice for the next join */

void portStateSyncWatchNext(int on) {
    sWatchNext = on != 0;
}

int portStateSyncWatcher(void) {
    return G.on && G.watch;
}

int ssWatchChosen(void) {
    const char* v = getenv("TTYD_SYNC_WATCH");
    return sWatchNext || (v != NULL && (int)strtol(v, NULL, 10) > 0);
}

static void watchSet(void) {
    G.watch = !ssNetIsHost() && ssWatchChosen();
    G.watchWarp[0] = 0;
    G.watchNotice = 0;
    if (G.watch) {
        ssLog("watch: this game watches the host's");
    }
}

static void watchShow(int hidden) {
    MarioWork* mario = marioGetPtr();
    PartyEntry* party = partyGetPtr(marioGetPartyId());
    if (mario != NULL) {
        mario->dispFlags = hidden ? (mario->dispFlags | MARIO_DISP_FLAG_HIDDEN)
                                  : (mario->dispFlags & ~(u32)MARIO_DISP_FLAG_HIDDEN);
    }
    if (party != NULL) {
        party->flags2 = hidden ? (party->flags2 | PARTY_DISP_FLAG_HIDDEN) : (party->flags2 & ~PARTY_DISP_FLAG_HIDDEN);
    }
}

static void watchFrame(void) {
    static const char kNotice[] = "Watching the host's game.";
    const Other* h = &G.pl[1];
    MarioWork* mario = marioGetPtr();
    {
        /* $TTYD_SYNC_WATCH_AT=<frame> (tests): a guest playing until then watches from then (to come
           into the host's room while a battle is under way) */
        static int sAt = -1;
        if (sAt < 0) {
            const char* v = getenv("TTYD_SYNC_WATCH_AT");
            sAt = v != NULL ? (int)strtol(v, NULL, 10) : 0;
        }
        if (sAt > 0 && !G.watch && !ssNetIsHost() && G.frame >= (unsigned)sAt) {
            sAt = 0;
            G.watch = 1;
            ssLog("watch: (test) this game watches the host's from now");
        }
    }
    if (!G.watch || !ssNetConnected() || G.seq != SEQ_GAME || seqCheckSeq() || gp->inBattle || ssBattleBusy() ||
        mario == NULL) {
        return;
    }
    if (marioChkKey()) {
        marioKeyOff();
    }
    watchShow(1);
    if (G.watchNotice == 0 || G.frame - G.watchNotice >= 600) {
        G.watchNotice = G.frame;
        portCoopNotice(kNotice);
    }
    if (!present(1) || h->count == 0 || (h->info.seq != SEQ_GAME && h->info.seq != SEQ_BATTLE)) {
        return;
    }
    if (strcmp(h->info.map, gp->currentMapName) != 0) {
        if (strcmp(G.watchWarp, h->info.map) != 0) {
            snprintf(G.watchWarp, sizeof(G.watchWarp), "%s", h->info.map);
            ssLog("watch: the host went to %s: after them", h->info.map);
            seqSetSeq(SEQ_MAP_CHANGE, h->info.map, NULL);
        }
        return;
    }
    if (h->info.seq != SEQ_GAME) {
        return; /* (in a battle: joined under way, statesync_battle_pc.c) */
    }
    {
        const PlayerMsg* m = &h->s[(h->head + SAMPLES - 1) % SAMPLES];
        mario->position.x = m->x;
        mario->position.y = m->y;
        mario->position.z = m->z;
    }
}

#include "statesync_coop_pc.inc"

static void launchFrame(void);

void portStateSyncGameFrame(void) {
    int p;

    menuTests();
    launchFrame();
    portOnlineEndFrame(); /* (the host gone, removed, left, the service unreachable: where now; online_end_pc.c) */
    ssBattleFieldFrame(); /* (on or off line: a battle taken from another game ends in the field all the same) */
    if (!portStateSyncOn()) {
        return;
    }
    if (!G.frameEnded && G.frame > 0) {
        portStateSyncFrameEnd(); /* (the last frame's end never came: a loop of its own, a hook in its place) */
    }
    G.frame++;
    G.seq = seqGetSeq();
    G.frameEnded = 0;
    if (gp == NULL) {
        return;
    }
    /* the progress's copy: taken when a file's game starts (new, loaded, or a test's warp) */
    if (G.seq != G.lastSeq) {
        if (G.seq == SEQ_TITLE || G.seq == SEQ_LOAD || G.seq == SEQ_LOGO) {
            G.baseline = 0;
        }
        G.lastSeq = G.seq;
    }
    /* the Pit's floor this room is (taken as it's entered: a floor gone down by another player
       changes the shared word before this player follows) */
    if (G.seq == SEQ_GAME && G.pitSeq != SEQ_GAME && G.pitSeq != SEQ_BATTLE) {
        G.pitFloor = swByteGet(GSW_JON_FLOOR);
    }
    G.pitSeq = G.seq;
    if (!G.baseline && G.seq == SEQ_GAME) {
        readProgress(&G.shadow);
        G.baseline = 1;
        ssLog("a file's game began on %s, story %d: its progress is the shadow's", gp->currentMapName, gp->gsw0);
        if (ssNetConnected() && !G.watch) {
            sendSnapshot(SS_ALL); /* (another file here: merged again) */
        }
    }
    {
        static int sRaidHeld;
        const int held = raidHold();
        if (sRaidHeld && !held) {
            ssProgressRebase(); /* (the raid's over, the game's own loadout back: not a change) */
            ssLog("raid: over: the progress is shared again");
        } else if (!sRaidHeld && held) {
            ssLog("raid: under way: the progress isn't shared until it's over");
        }
        sRaidHeld = held;
    }
    {
        static int sVersusHeld;
        const int held = versusHold();
        if (sVersusHeld && !held) {
            ssProgressRebase(); /* (the fight's over, the file's own loadout and words back: not a change) */
            ssLog("versus: over: the progress is shared again");
        } else if (!sVersusHeld && held) {
            ssLog("versus: under way: the progress isn't shared until it's over");
        }
        sVersusHeld = held;
    }
    {
        static int sLentHeld;
        const int held = lentHold();
        if (sLentHeld && !held) {
            ssProgressRebase(); /* (the run's over, the file's own pouch back: not a change) */
            ssLog("modes: lent loadout back: the progress is shared again");
        } else if (!sLentHeld && held) {
            ssLog("modes: a lent loadout: the progress isn't shared until it's given back");
        }
        sLentHeld = held;
    }
    if (G.snapOwed != 0 && !modeHold()) {
        /* (the whole progresses asked for while a mode held it: the file's own now) */
        const unsigned owed = G.snapOwed;
        int p;
        G.snapOwed = 0;
        if (G.baseline && ssNetConnected() && !G.watch) {
            if (owed & 1u) {
                sendSnapshot(SS_ALL);
            } else {
                for (p = 1; p <= SS_PLAYERS; p++) {
                    if (owed & (1u << p)) {
                        sendSnapshot(p);
                    }
                }
            }
        }
    }
    if (G.baseline && (G.seq == SEQ_GAME || G.seq == SEQ_BATTLE || G.seq == SEQ_MAP_CHANGE) && !ssBattleBusy() &&
        !modeHold()) {
        char myArea[4];
        area(gp->currentMapName, myArea);
        if (memcmp(myArea, G.shadow.area, 4) != 0) {
            /* a new area: its own flags start again (swReInit); not a change to send */
            memcpy(G.shadow.lswf, gp->lswf, sizeof(G.shadow.lswf));
            memcpy(G.shadow.lsw, gp->lsw, sizeof(G.shadow.lsw));
            memcpy(G.shadow.area, myArea, 4);
        }
        if (ssNetConnected() && !G.watch) {
            sendDiffs(); /* (a co-op guest's too: an item it uses from its own menu is the shared pouch's) */
        } else {
            /* not connected: the changes wait in the shadow's difference until the session
               (the new session's merge carries them) */
        }
    }
    ssNetPump(G.frame);
    if (sCountsAbs && ssNetIsHost() && ssNetConnected() && G.baseline && G.seq == SEQ_GAME && !ssBattleBusy() &&
        !modeHold()) {
        /* (after this frame's own changes went out as deltas: the guests end where the host is) */
        int16_t c[3];
        sendDiffs();
        c[0] = pouchGetPtr()->coins;
        c[1] = pouchGetPtr()->starPieceCount;
        c[2] = pouchGetPtr()->shineSpriteCount;
        ssNetSendRel(SS_R_COUNTS_ABS, c, sizeof(c));
        sCountsAbs = 0;
    }
    if (!ssBattleBusy() && !gp->inBattle && G.baseline && !modeHold()) {
        flushDeferred();
    }
    if (G.frame % SEND_EVERY == 0 && ssNetConnected() && !coopSendsEveryFrame()) {
        sendPlayer(); /* (a co-op host's: every frame, at the frame's end, portStateSyncFrameEnd) */
    }
    if (G.frame % 300 == 0 && ssNetConnected() && G.baseline && !modeHold()) {
        /* (none while a mode holds the progress: a co-op guest took the host's lent versus loadout as the
           host's progress when the digests differed, and kept it after: pm64x_online_versus) */
        unsigned long long d[2];
        readProgress(&G.scratch);
        digest(&G.scratch, &d[0], &d[1]);
        ssNetSendRel(SS_R_DIGEST, d, sizeof(d));
        if (ssNetIsHost()) {
            sCountsAbs = 1;
        }
    }
    pickBattlePeer();
    roomFrame();
    watchFrame();
    coopSeatsFrame();
    coopMarioMenuFrame();
    coopFollowFrame();
    coopOwnFrame(); /* (each guest's own character: the host's hold, the guest's own walk) */
    coopActFrame(); /* (the guests' field actions: answers waited for, the host's watched ones) */
    coopSaveFrame();
    if (coopPeopleFrame()) {
        return; /* (this game left: the room is over, online_end_pc.c asks where to go) */
    }
    /* (coopPeopleFrame above: the players list, the host gone, removals, welcome back: statesync_coop_pc.inc) */
    coopHostLog();
    if (portStateSyncSoloOnly() != NULL && ssNetConnected()) {
        ssLog("solo: %s: off line", portStateSyncSoloOnly());
        portCoopNotice(portStateSyncSoloOnly());
        portStateSyncLeave();
        return;
    }
    modesFrame();
    ssBattleFrame(G.frame, ssSameRoom());
    /* the ghosts: every other player in this room, with their partner */
    for (p = 1; p <= SS_PLAYERS; p++) {
        Other* o = &G.pl[p];
        const int slot = otherIndex(p);
        SsGhostPose me, partner, luigi;
        int hasPartner = 0, hasLuigi = 0;
        if (p == ssNetMe() || slot < 0) {
            continue;
        }
        /* (a co-op guest has no character of its own here: the host's game draws the one it plays) */
        if (hereIn(p) && !G.pl[p].info.watch && !G.pl[p].info.coop && !gp->inBattle &&
            interpolate(p, &me, &partner, &hasPartner, &luigi, &hasLuigi)) {
            if (p == 1) {
                coopOwnNewest(&partner, &hasPartner, &luigi, &hasLuigi); /* (a co-op guest's own character) */
            }
            coopHelperGhosts(p, slot, portStateSyncCoopGuest() && p == 1);
            ssGhostDraw(slot, 0, &me);
            if (me.costume > 0) {
                portCostumeWantOther(me.costume); /* (its pose loaded here too: else it's never drawn) */
            }
            if (hasPartner) {
                ssGhostDraw(slot, 1, &partner);
            } else {
                ssGhostHide(slot, 1);
            }
            if (hasLuigi) {
                ssGhostDraw(slot, 2, &luigi);
            } else {
                ssGhostHide(slot, 2);
            }
            if (!o->ghostShown) {
                ssLog("ghost: player %d is here in %s", p, gp->currentMapName);
            }
            o->ghostShown = 1;
            if (G.frame % G.logEvery == 0) {
                MarioWork* mario = marioGetPtr();
                ssLog("ghost: player %d at (%.1f, %.1f, %.1f) dir %.0f %s/%s partner %s/%s at (%.1f, %.1f, %.1f); me at "
                      "(%.1f, %.1f, %.1f)",
                      p, me.x, me.y, me.z, me.dir, me.model, me.anim, hasPartner ? partner.model : "-",
                      hasPartner ? partner.anim : "-", hasPartner ? partner.x : 0.0f, hasPartner ? partner.y : 0.0f,
                      hasPartner ? partner.z : 0.0f, mario->position.x, mario->position.y, mario->position.z);
            }
        } else if (o->ghostShown) {
            o->ghostShown = 0;
            ssGhostHide(slot, -1);
            ssLog("ghost: player %d gone (%s)", p, present(p) ? o->info.map : "not here");
        }
    }
    if (portStateSyncCoopGuest()) {
        ndGuestFrame(1); /* (the delay log: this guest's own character as drawn now) */
    }
    if (G.frame % G.logEvery == 0) {
        unsigned long long d[2] = {0, 0};
        const SsPeer* bp = ssPeer();
        char progress[40];
        if (G.baseline) {
            readProgress(&G.scratch);
            digest(&G.scratch, &d[0], &d[1]);
        }
        /* (a party game's or a battle's isn't the shared progress: held, so a log's last progress line is
           the field's, as portStateSyncExitDrain's; a test that ends with a game still in the versus ring or
           a battle compares what was shared before it) */
        if (G.baseline && (modeHold() || ssBattleBusy() || gp->inBattle)) {
            snprintf(progress, sizeof(progress), "held (%s)", gp->inBattle || ssBattleBusy() ? "a battle" : "a party game");
        } else {
            snprintf(progress, sizeof(progress), "%016llx/%016llx", d[0], d[1]);
        }
        ssLog("status: %s seq %d map %s story %d coins %d keys %d | peer %s %s | owns %d | progress %s | "
              "merges %u applied %u sent %u | %s",
              ssNetIsHost() ? "host" : "guest", G.seq, gp->currentMapName, gp->gsw0, pouchGetPtr()->coins,
              G.seq == SEQ_GAME ? (int)marioChkKey() : -1, ssNetConnected() ? "linked" : "not linked",
              bp->valid ? bp->map : "-", G.owns, progress, G.merges, G.applied, G.sentOps, ssNetSummary());
    }
}

/* a test game's last frame (fingerprint_pc.c portTestExit): its reliable messages delivered, and its
   progress as it ends, after what came meanwhile (the logs' last progress line) */
void portStateSyncExitDrain(void) {
    unsigned long long d[2] = {0, 0};
    if (!G.on || gp == NULL) {
        return;
    }
    ssNetDrain(4000);
    if (G.baseline && !modeHold() && !ssBattleBusy() && !gp->inBattle) { /* (a party game's or a battle's isn't progress) */
        readProgress(&G.scratch);
        digest(&G.scratch, &d[0], &d[1]);
        ssLog("status: at the end, map %s story %d coins %d | progress %016llx/%016llx", gp->currentMapName, gp->gsw0,
              pouchGetPtr()->coins, d[0], d[1]);
    }
    /* (the connections closed now: the others see this game go at once, never a connection left open and
       silent while the program winds down, dropped as "quiet" seconds later) */
    ssNetStop();
}

/* the frame's end (marioStMain, after every system's update): the host's word to its co-op guests every frame,
   as the frame left it (its guests' characters moved by the controllers it just played); a room's batch; the
   tests' pace (where a real game waits for the screen) */
void portStateSyncFrameEnd(void) {
    if (!portStateSyncOn() || G.frameEnded) {
        return;
    }
    G.frameEnded = 1;
    if (gp != NULL && ssNetConnected() && coopSendsEveryFrame()) {
        sendPlayer();
    }
    if (gp != NULL && ssNetConnected()) {
        coopOwnSend(); /* (a co-op guest: its own character as this frame left it) */
        if (portStateSyncCoopGuest() && portStateSyncCoopLocalSeat() != 0) {
            ndGuestFrame(1); /* (the delay log: walked here, it's drawn as this frame's update left it) */
        }
    }
    ssNetFrameEnd();
    {
        /* $TTYD_SYNC_PACE=<fps> (tests): two unpaced test games ($TTYD_FAST) kept to one rate, so
           their frames line up roughly in time as two players' would */
        static double sNext;
        static int sPace = -1;
        if (sPace < 0) {
            const char* v = getenv("TTYD_SYNC_PACE");
            sPace = v != NULL ? (int)strtol(v, NULL, 10) : 0;
        }
        if (sPace > 0 && portStateSyncCatchUp() == 1) { /* (catching up a shared battle: as fast as it can) */
            const double now = (double)ssNetMs();
            if (sNext == 0.0 || now - sNext > 500.0) {
                sNext = now;
            }
            sNext += 1000.0 / sPace;
            if (now < sNext - 1.0) {
                ssNetSleep((int)(sNext - now)); /* (a sleep: a wait would end at once on what came meanwhile) */
            }
        }
    }
}

void portStateSyncApplyPad(PADStatus* pads) {
    if (!G.on) {
        return;
    }
    if (portStateSyncCoopHost() && gp != NULL && !gp->inBattle) {
        ssNetPoll(); /* (the guests' controller frames come since the frame's pump: played now, not next frame) */
    }
    coopApplyPad(pads);
    ssBattleApplyPad(pads);
}

/* ---- the Start menu's (pause_online_aurora.c; port/include/port/statesync.h) ---- */

int ssNetFailed(void); /* statesync_net_pc.c */

/* on from the menu: this game's progress is the shadow's at its next field frame, merged with the
   other's once they're there */
static void menuStarted(void) {
    portOnlineEndStale(); /* (the last room's leave window: over, this is a new session) */
    G.on = 1;
    G.lastSeq = -1;
    G.baseline = 0;
    memset(G.pl, 0, sizeof(G.pl));
    G.battlePeer = 0;
    G.map[0] = '\0';
    G.owns = 0;
    watchSet();
}

static const char* sSoloNote;

/* the last Host or Join as asked (Try again: portStateSyncRetry) */
static struct {
    int kind; /* 0 none, 1 host, 2 join */
    int byAddress, port, watch;
    char text[128];
} sLastAsk;

int portStateSyncRetry(void) {
    const int kind = sLastAsk.kind;
    char text[sizeof(sLastAsk.text)];
    snprintf(text, sizeof(text), "%s", sLastAsk.text);
    portStateSyncLeave(); /* (the failed one off first) */
    if (kind == 1) {
        return portStateSyncHost(sLastAsk.byAddress, sLastAsk.port);
    }
    if (kind == 2) {
        portStateSyncWatchNext(sLastAsk.watch);
        return portStateSyncJoin(text);
    }
    return 0;
}

int portStateSyncHost(int byAddress, int port) {
    if (portStateSyncOn()) {
        return 1;
    }
    sLastAsk.kind = 1;
    sLastAsk.byAddress = byAddress;
    sLastAsk.port = port;
    if ((sSoloNote = portStateSyncSoloOnly()) != NULL) {
        return 0; /* (portStateSyncNote says why) */
    }
    if (!ssNetBegin(1, !byAddress, NULL, port)) {
        return 0;
    }
    menuStarted();
    return 1;
}

int portStateSyncJoin(const char* text) {
    char code[16];
    int n = 0;
    const char* c;
    if (portStateSyncOn()) {
        return 1;
    }
    sLastAsk.kind = 2;
    sLastAsk.watch = sWatchNext;
    snprintf(sLastAsk.text, sizeof(sLastAsk.text), "%s", text);
    if ((sSoloNote = portStateSyncSoloOnly()) != NULL) {
        return 0;
    }
    /* a code: 8 of the code's characters (a dash and spaces left out); anything else an address */
    for (c = text; *c != '\0' && n < 15; c++) {
        if (*c != '-' && *c != ' ') {
            code[n++] = *c;
        }
    }
    code[n] = '\0';
    if (!ssNetBegin(0, n == 8 && strchr(text, '.') == NULL && strchr(text, ':') == NULL, text, 0)) {
        return 0;
    }
    menuStarted();
    return 1;
}

void portStateSyncLeave(void) {
    if (!G.on) {
        return;
    }
    ssBattleStop();
    ssRoomStop();
    ssGhostHide(-1, -1);
    coopLeave();
    ssNetStop();
    while (sDeferHead != NULL) {
        Deferred* d = sDeferHead;
        sDeferHead = d->next;
        free(d);
    }
    sDeferTail = NULL;
    G.on = 0;
    G.baseline = 0;
    memset(G.pl, 0, sizeof(G.pl));
    G.battlePeer = 0;
    G.owns = 0;
    if (G.watch && gp != NULL && seqGetSeq() == SEQ_GAME) {
        watchShow(0);
        marioKeyOn();
    }
    G.watch = 0;
}

int portStateSyncPhase(void) {
    if (!portStateSyncOn()) {
        return ssNetNote()[0] != '\0' ? PORT_SYNC_FAILED : PORT_SYNC_OFF;
    }
    if (ssNetConnected()) {
        return PORT_SYNC_LINKED;
    }
    return ssNetFailed() ? PORT_SYNC_FAILED : PORT_SYNC_WAITING;
}

const char* portStateSyncCode(void) {
    return ssNetCode();
}

int portStateSyncPort(void) {
    return ssNetPort();
}

const char* portStateSyncNote(void) {
    if (sSoloNote != NULL && !G.on) {
        return sSoloNote;
    }
    return ssNetNote();
}

/* ---- invite links and the launcher ---- */

static int sameNoCase(const char* a, const char* b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        const int x = a[i] >= 'a' && a[i] <= 'z' ? a[i] - 32 : a[i];
        const int y = b[i] >= 'a' && b[i] <= 'z' ? b[i] - 32 : b[i];
        if (x != y) {
            return 0;
        }
        if (x == '\0') {
            return 1;
        }
    }
    return 1;
}

/* papercut://join/K7P2-QX9M (any case, the dash or none, anything after a / ? or # left out) */
int portStateSyncInviteCode(const char* url, char* out, size_t size) {
    char code[PORT_ONLINE_CODE_LENGTH + 1];
    size_t n = 0;
    const char* p;
    if (url == NULL || size < sizeof(code) || !sameNoCase(url, "papercut:", 9)) {
        return 0;
    }
    for (p = url + 9; *p == '/'; p++) {
    }
    if (!sameNoCase(p, "join/", 5)) {
        return 0;
    }
    for (p += 5; *p != '\0' && *p != '/' && *p != '?' && *p != '#'; p++) {
        const char c = *p >= 'a' && *p <= 'z' ? (char)(*p - 32) : *p;
        if (c == '-') {
            continue;
        }
        if (strchr(PORT_ONLINE_CODE_CHARS, c) == NULL || n >= PORT_ONLINE_CODE_LENGTH) {
            return 0;
        }
        code[n++] = c;
    }
    if (n != PORT_ONLINE_CODE_LENGTH) {
        return 0;
    }
    code[n] = '\0';
    snprintf(out, size, "%s", code);
    return 1;
}

int portStateSyncInviteLink(char* out, size_t size) {
    const char* code = ssNetCode();
    if (code[0] == '\0') {
        return 0;
    }
    snprintf(out, size, "papercut://join/%s", code);
    return 1;
}

static struct {
    int set, host, byAddress, port, watch;
    char code[128];
} sLaunch;

void portStateSyncLaunch(int host, int byAddress, const char* code, int port, int watch) {
    sLaunch.set = 1;
    sLaunch.host = host;
    sLaunch.byAddress = byAddress;
    sLaunch.port = port;
    sLaunch.watch = watch;
    snprintf(sLaunch.code, sizeof(sLaunch.code), "%s", code != NULL ? code : "");
}

/* each frame: the launcher's choice once the game runs ($TTYD_SYNC_INVITE: a link's, tests); and
   an invite link that came while it runs (Aurora: invite.cpp), told once (the Start menu's Online
   tab offers its room: it never joins by itself) */
#ifdef TTYD_BACKEND_AURORA
int portOnlinePeekInvite(char* out, size_t size); /* invite.cpp */
#endif
static void launchFrame(void) {
    static int sRead;
    if (!sRead) {
        const char* link = getenv("TTYD_SYNC_INVITE");
        char code[16];
        sRead = 1;
        if (link != NULL && *link) {
            if (portStateSyncInviteCode(link, code, sizeof(code))) {
                /* (the room's half only: logs go out with crash reports) */
                ssLog("invite: a link to room %.4s-****: joining it", code);
                portStateSyncLaunch(0, 0, code, 0, 0);
            } else {
                ssLog("invite: not an invite link");
            }
        }
    }
    if (sLaunch.set && !G.on) {
        int ok;
        sLaunch.set = 0;
        portStateSyncWatchNext(sLaunch.watch);
        ok = sLaunch.host ? portStateSyncHost(sLaunch.byAddress, sLaunch.port) : portStateSyncJoin(sLaunch.code);
        ssLog("launch: %s%s: %s", sLaunch.host ? "host" : sLaunch.watch ? "watch" : "join",
              sLaunch.host ? (sLaunch.byAddress ? " by address" : " a room") : "",
              ok ? "on" : portStateSyncNote());
        if (ok && sLaunch.host && !sLaunch.byAddress) {
            portCoopNotice("Your room is open: its code is in the Start menu, Multiplayer, Online.");
        }
    }
#ifdef TTYD_BACKEND_AURORA
    {
        static char sTold[16];
        static unsigned sAt;
        char code[16];
        if (++sAt % 30 == 0 && !G.on && portOnlinePeekInvite(code, sizeof(code)) && strcmp(code, sTold) != 0) {
            snprintf(sTold, sizeof(sTold), "%s", code);
            portCoopNotice("An invite link came: the Start menu's Multiplayer page offers its room.");
        }
    }
#endif
}

/* the party modes (their own file, included) */
#include "statesync_modes_pc.inc"

/* every message's size, the same in every build (port/statesync.h WIRE_SIZE) */
WIRE_SIZE(PlayerMsg, 520);
WIRE_SIZE(Stats, 98);
WIRE_SIZE(Progress, 8368);
