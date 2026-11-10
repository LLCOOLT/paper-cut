/* "Sync what matters" (port/include/port/statesync.h), layers 3 and 4: battles
 * and scenes together.
 *
 * Battles (layer 3). Each player runs their own game, so a shared battle first
 * makes the two games the same where a battle reads them, then plays it in
 * both games with only the players' decisions crossing, and afterwards lets
 * each go on with its own field. No save state crosses (they hold this
 * process's addresses and are Aurora's only, port/savestate.h): a battle is
 * built from little, and that little is sent instead.
 *
 * 1. Player A (the host or a guest) touches an enemy while others are in the
 *    room, free (in the field, their keys on, no battle). A's game has asked
 *    for the battle sequence (fbatBattleMode); at the next frame's start
 *    (makeKey, before seqMain builds the battle) A sends every one of them an
 *    invitation (the enemy's name, how it was hit, A's whole pouch, A's
 *    partner) and waits. Guests reach each other through the host, which
 *    passes the battle's messages on (statesync_pc.c ssBattleSendTo).
 * 2. Each invited player B takes A's pouch (its own kept), A's partner, and
 *    starts the same battle with its own copy of that enemy (the room's same
 *    NPC; fbat mode 5, as the game's own encounter does). At its next frame's
 *    start it is where A is: it says so and waits.
 * 3. A sends the start to those who came, with the battle's places: A is
 *    player 1 (Mario), the first other player 2 (the partner, co-op's second
 *    player: second_player_pc.c; Luigi in Brothers mode), the next players 3
 *    and 4 (companions: the partner they have out, if A's party has it, else
 *    another; when that partner fights, it's theirs). All load the battle at
 *    their own pace and meet at a gate before battle_init, taking A's game
 *    clock and random numbers there.
 * 4. Each game then runs the battle itself, at its own pace; no controller
 *    crosses. A's game is the authority: at each step of the battle (a phase,
 *    the next actor, an action over) it sends its verdict, its random numbers
 *    and a snapshot of what the battle keeps, which every other game takes at
 *    the same step; each player's choices and action-command, guard and
 *    Stylish outcomes come from the game of the player who plays that unit
 *    (below, "the shared battle: sync what matters").
 * 5. Back in the field, each game ends it. Each B gets its own pouch back; A's
 *    game then sends what the battle changed (coins, items used, Star Points:
 *    its usual diffs), as for anything else A does.
 * Everyone busy (a menu, an event, another room), or no answer in 3 seconds: A
 * fights alone.
 *
 * Scenes (layer 4), host-driven: when A's game starts a scene (its Mario's keys
 * off for half a second outside a talk, a battle and the menus) and B is in
 * another room, B is pulled in: warped to A's room, beside A, and held there
 * (keys off) while A's scene runs; B sees it as A plays it (A's ghost, A's
 * NPCs: A owns the room, layer 2), and A's story flags and items come across
 * when it's over. B's own pouch is put back as it was when it came (a scene the
 * room plays for B too must not give B twice). When A's keys come back, B is
 * let go. B busy: pulled once it's free, if the scene still runs. */
#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_break_slot.h"
#include "battle/battle_disp.h"
#include "battle/battle_event_default.h"
#include "battle/battle_pad.h"
#include "battle/battle_information.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_unit.h"
#include "battle/battle_attack_audience.h"
#include "battle/battle_item_data.h"
#include "battle/battle_mario.h"
#include "data/item_data.h"
#include "data/mapdata.h"
#include "party.h"
#include "port/battlex.h"
#include "port/directors_cut.h"
#include "driver/camdrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/windowdrv.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "port/brothers.h"
#include "port/crowd.h"
#include "port/duo.h"
#include "port/dungeon_master.h"
#include "port/luigi.h"
#include "port/online.h"
#include "port/online_faults.h"
#include "port/online_local.h"
#include "port/platform.h"
#include "port/pm64.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/raid.h"
#endif
#include "port/settings.h"
#include "port/statesync.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/tr.h"
#else
#define portTrGame(english) (english) /* (the headless build has no translations) */
#endif
#include "win/win_main.h"
#include <dolphin/pad.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INVITE_WAIT_MS 3000
/* playing together: a seated guest is always asked to the host's battles, also one still coming into the room
   (or whose word is late): A waits this long for it, and it keeps the invitation until it can take it */
#define ARRIVE_WAIT_MS 8000
#define VERSUS_WAIT_MS 15000 /* (a versus fight starts with a fade: longer than a field battle's) */
#define SLOTS 4        /* a shared battle's players: 1 Mario, 2 the partner, 3 and 4 companions */
#define NOT_GONE 0xFFFFFFFFu

enum {
    B_INVITE = 1, /* A: come */
    B_DECLINE,    /* B: can't */
    B_READY,      /* B: at the battle's start */
    B_GO,         /* A: go */
    B_STEP,       /* A: a step gate's verdict, random numbers, who plays, the snapshot (Step) */
    B_EXEC,       /* an action's script starts: its random numbers (Exec); from the game of the player whose unit
                     acts (A's for the enemies and the units A plays) */
    B_CANCEL,     /* A: not waiting any more */
    B_GATE,       /* both: at battle_init (the clock and random numbers, A's taken) */
    B_GAMEOVER,   /* either: this game's game over in the battle: the other's too */
    B_VERSUS,     /* the host: a versus fight, its set-up (VersusMsg) */
    B_LEAVE,      /* any: a player's sharing is over (Small: data[0] their place) */
    B_LATE,       /* a game in the room: may it join the battle under way (Small: id)? */
    B_LATE_INVITE, /* A: the battle's invitation as it was (Invite) */
    B_LATE_GO,    /* A: its places and start, at its gate (LateGo) */
    B_WATCHERS,   /* A: the battle's watchers now (Small: data[0], by bit) */
    B_ACK,        /* a follower: what it waits for from A (Hdr: its step; sub R_EXEC or 0 the next gate) */
    B_CMD,        /* a unit's player: the command menu's outcome (Cmd) */
    B_RES,        /* a unit's player: an outcome measured on their screen (Res: sub its kind) */
    B_TAKE,       /* A: who plays now (Hdr k: the places present; the absent's units A's player's) */
    B_AT = 41,    /* a follower catching up: the step it has got to (Hdr step), so A knows it's coming; sub 1: its
                     beat (where it is, the outcomes it has passed: a late one is a miss) */
    B_CROWD = 40, /* the idle players in the audience: a request to A, A's event to all (crowd_seats_pc.c) */
    S_PULL = 20,  /* A: a scene: come (map, place) */
    S_DONE,       /* A: the scene's over */
    S_TEXT,       /* A: a text box opened in the scene (SceneText) */
    S_TEXT_END,   /* A: that text box closed */
    S_CAM,        /* A: the scene's camera (SceneCam) */
};

#define SCENE_TEXT_MAX 1024
typedef struct SceneText {
    uint8_t kind;
    uint8_t direct; /* text is the text itself (else a message's name, looked up in the same disc's) */
    uint16_t len;
    uint32_t id;      /* the scene's */
    char speaker[32]; /* the NPC speaking (its name), "" none */
    char text[SCENE_TEXT_MAX];
} SceneText;

typedef struct SceneCam {
    uint8_t kind;
    uint8_t pad[3];
    uint32_t id;
    float pos[3], at[3], up[3], fovY;
} SceneCam;

/* ---- every game the same: no program address ever crosses ----
 * The games playing together are the same Paper Cut version on the same disc, but not the same
 * program: Windows, macOS, Linux, Android and iOS builds (other compilers, other layouts) all play
 * with each other. So a pointer into the game's data crosses as what it is there, and each game
 * finds its own:
 * - the enemy's battle (its stage, group, drop lists and the units' kinds): the place in the area's
 *   battle table (AreaData.btlSetupTbl, the same data in every build: walked in one order), else
 *   by value (a group made at run time: Master Quest's), else the receiving game's own same NPC's;
 * - the music: its name, as text;
 * - a command menu's weapons and the chosen move's script: the menu's place (each game builds the
 *   same menu from the same battle), else one of the battle's own named moves and scripts, an item's,
 *   a Special's, a unit's data by its tag, the audience's (wireRef, below).
 * Every message struct is fixed-width fields only (no pointer, no long, no bit-field), checked by
 * size at compile time (the WIRE_SIZE asserts) on every compiler that builds the game. */

#define WIRE_UNITS 16 /* a group sent by value: at most this many units (the game's own have up to 9) */

typedef struct WireUnit {
    int32_t kind;     /* the unit kind: its place in the area's table (-1 none, -2 not there) */
    int32_t unitType; /* (the kind's type: B's own group's unit of that type when the place is -2) */
    int8_t alliance;
    uint8_t pad[3];
    int32_t attackPhase;
    float x, y, z;
    int32_t hitOffsetX;
    int32_t work[4];
    int32_t drop; /* its drop list's place (-1 none) */
} WireUnit;

typedef struct WireBattle {
    int32_t battleFlags, fixedItem, itemDropped, heartsDropped, flowersDropped;
    int32_t heldItems[8], stolenItems[8], recoveredItems[8];
    int32_t scriptWork[4];
    int32_t randomMode;
    int8_t randomValues[16];
    uint8_t battleRule[3];
    uint8_t byValue; /* the group crosses by value (units below), not by its place */
    int32_t stage, group, drops[8]; /* places in the area's table: -1 none, -2 not there */
    char bgm[48];                   /* the music's name, "" none */
    int32_t gNum, gHeldW, gRandW, gNoW, gHp, gFp; /* the group by value */
    WireUnit units[WIRE_UNITS];
} WireBattle;

typedef struct Invite {
    uint8_t kind;
    uint8_t pad[3];
    uint32_t id; /* the battle's number, A's */
    char map[16];
    char npc[32];
    int32_t hitKind, firstAttackParam;
    uint32_t firstAttackFlags;
    int32_t partner; /* A's partner (member id) */
    int32_t route;   /* 0 an enemy met (fbat mode 5), 1 a script's (evt_npc_battle_start) */
    int32_t playAs;  /* A's Play as pick: Mario's unit in B's game is A's character */
    int32_t brosHp, brosFp; /* Brothers mode: A's Luigi's HP and FP (brothers_pc.c), B's kept meanwhile */
    /* the enemy's battle as A's game set it up (its group, stage, drops and the numbers its room rolled
       when it set them: npcSetupBattleInfo takes random numbers, so each game's differ), every
       pointer as what it is in the game's data (wireBattle, below): the same in every build */
    WireBattle battle;
    PouchData pouch;
    uint8_t pm64[8]; /* A's Paper Mario 64 partners in its partners' places (port/pm64.h): B takes them */
} Invite;

/* the area's battle table, walked in one order: each setup's lists, then those the Director's Cut
   puts in their place (a setting: after every list of the game's own, so their places never move).
   Encoding: the place of `want` among that type's pointers (-2 not there); decoding (want NULL): the
   pointer at place `at` */
enum { W_STAGE, W_GROUP, W_DROP, W_KIND, W_PDROP };

typedef struct Walk {
    int type;
    const void* want;
    int at;
    const void* got;
    int n, found;
} Walk;

static void visit(Walk* w, const void* p) {
    if (w->found || p == NULL) {
        return;
    }
    if (w->want != NULL ? p == w->want : w->n == w->at) {
        w->found = 1;
        w->got = p;
        return;
    }
    w->n++;
}

static void walkSelect(Walk* w, const NpcBattleSelect* s) {
    int k, i;
    for (k = 0; s != NULL && s->weight > 0 && k < 64 && !w->found; s++, k++) {
        const BattleGroupSetup* g = s->groupSetup;
        if (w->type == W_STAGE) {
            visit(w, s->stageData);
        } else if (w->type == W_GROUP) {
            visit(w, g);
        } else if (g != NULL && w->type == W_PDROP) {
            visit(w, g->hpDropTable);
            visit(w, g->fpDropTable);
        } else if (g != NULL) {
            for (i = 0; i < g->numEnemies && i < 32 && g->enemyData != NULL; i++) {
                visit(w, w->type == W_KIND ? (const void*)g->enemyData[i].unitKindParams
                                           : (const void*)g->enemyData[i].itemDropTable);
            }
        }
    }
}

static const NpcSetupBattleInfo* areaTable(void) {
    char area[4];
    const AreaData* ad;
    if (gp == NULL) {
        return NULL;
    }
    memcpy(area, gp->currentMapName, 3);
    area[3] = '\0';
    ad = areaDataPtr(area);
    return ad != NULL ? (const NpcSetupBattleInfo*)ad->btlSetupTbl : NULL;
}

static void areaWalk(Walk* w) {
    const NpcSetupBattleInfo* t = areaTable();
    int pass, i;
    for (pass = 0; pass < 2 && t != NULL; pass++) {
        for (i = 0; i < 512 && t[i].name != NULL && !w->found; i++) {
            NpcBattleSelect* lists[2];
            int l;
            lists[0] = t[i].select;
            lists[1] = t[i].altSelect;
            for (l = 0; l < 2; l++) {
                if (pass == 0) {
                    walkSelect(w, lists[l]);
                } else if (lists[l] != NULL) {
                    NpcBattleSelect* dc = (NpcBattleSelect*)portDirectorsCutSelect(lists[l]);
                    if (dc != lists[l]) {
                        walkSelect(w, dc);
                    }
                }
            }
        }
    }
}

static int32_t placeOf(int type, const void* p) {
    Walk w;
    if (p == NULL) {
        return -1;
    }
    memset(&w, 0, sizeof(w));
    w.type = type;
    w.want = p;
    areaWalk(&w);
    return w.found ? w.n : -2;
}

/* the pointer at that place (NULL: none, or not found: *ok 0) */
static const void* atPlace(int type, int32_t at, int* ok) {
    Walk w;
    *ok = 1;
    if (at == -1) {
        return NULL;
    }
    if (at < 0) {
        *ok = 0;
        return NULL;
    }
    memset(&w, 0, sizeof(w));
    w.type = type;
    w.at = at;
    areaWalk(&w);
    *ok = w.found;
    return w.got;
}

static void wireBattle(WireBattle* o, const NpcBattleInfo* bi) {
    int i;
    memset(o, 0, sizeof(*o));
    o->battleFlags = bi->battleFlags;
    o->fixedItem = bi->fixedItem;
    o->itemDropped = bi->itemDropped;
    o->heartsDropped = bi->heartsDropped;
    o->flowersDropped = bi->flowersDropped;
    for (i = 0; i < 8; i++) {
        o->heldItems[i] = bi->heldItems[i];
        o->stolenItems[i] = bi->stolenItems[i];
        o->recoveredItems[i] = bi->recoveredItems[i];
        o->drops[i] = placeOf(W_DROP, bi->itemDropLists[i]);
    }
    for (i = 0; i < 4; i++) {
        o->scriptWork[i] = bi->scriptWork[i];
    }
    o->randomMode = bi->random.mode;
    memcpy(o->randomValues, bi->random.values, sizeof(o->randomValues));
    memcpy(o->battleRule, bi->battleRule, sizeof(o->battleRule));
    o->stage = placeOf(W_STAGE, bi->stageData);
    o->group = placeOf(W_GROUP, bi->groupSetup);
    snprintf(o->bgm, sizeof(o->bgm), "%s", bi->bgmName != NULL ? bi->bgmName : "");
    if (o->group == -2 && bi->groupSetup != NULL) {
        /* (a group made at run time: by value, its kinds and drop lists by their places) */
        const BattleGroupSetup* g = bi->groupSetup;
        o->byValue = 1;
        o->gNum = g->numEnemies < WIRE_UNITS ? g->numEnemies : WIRE_UNITS;
        o->gHeldW = g->heldItemWeight;
        o->gRandW = g->randomItemWeight;
        o->gNoW = g->noItemWeight;
        o->gHp = placeOf(W_PDROP, g->hpDropTable);
        o->gFp = placeOf(W_PDROP, g->fpDropTable);
        for (i = 0; i < o->gNum; i++) {
            const BattleUnitSetup* u = &g->enemyData[i];
            WireUnit* w = &o->units[i];
            w->kind = placeOf(W_KIND, u->unitKindParams);
            w->unitType = u->unitKindParams != NULL ? (int32_t)u->unitKindParams->unitType : 0;
            w->alliance = u->alliance;
            w->attackPhase = u->attackPhase;
            w->x = u->position.x;
            w->y = u->position.y;
            w->z = u->position.z;
            w->hitOffsetX = u->hitOffsetX;
            memcpy(w->work, u->unitWork, sizeof(w->work));
            w->drop = placeOf(W_DROP, u->itemDropTable);
        }
    }
}

/* ---- what another game sends: checked against the game's own ranges ----
 * Any game in the session may send any bytes: every id, level, count and name a message carries is
 * checked here before it indexes a table, sizes a copy or names a file (a bad one: the message dropped,
 * logged). Valid messages pass unchanged. */

/* any item's id: a place in itemDataTable (0 none) */
static int itemIdOk(int32_t id) {
    return id >= 0 && id < PORT_ITEM_TABLE_SIZE;
}

/* the pouch's lists (0 an empty slot): the game's items, badges and key items, or the port's own (battlex.h:
   which of them a game has can depend on its linked ROM, so the port's range as a whole) */
static int heldIdOk(int32_t id) {
    return id == 0 || (id >= ITEM_MIN && id < ITEM_MAX) || (id >= PORT_ITEM_MIN && id < PORT_ITEM_END);
}

static int badgeIdOk(int32_t id) {
    return id == 0 || (id >= BADGE_MIN && id < BADGE_MAX) || (id >= PORT_ITEM_MIN && id < PORT_ITEM_END);
}

static int keyIdOk(int32_t id) {
    return id == 0 || (id >= KEY_ITEM_MIN && id < KEY_ITEM_MAX);
}

/* a name that becomes a file or a sound's name (a map's, the music's): letters, digits and '_' only,
   NUL-terminated within cap */
static int nameOk(const char* s, size_t cap) {
    size_t i;
    for (i = 0; i < cap && s[i] != '\0'; i++) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    return i > 0 && i < cap;
}

/* A's pouch: what's wrong with it, NULL nothing (levels: partners' 0..2, boots and hammer 0..3, Mario's
   1..99, rank 0..3) */
static const char* pouchBad(const PouchData* pd) {
    int i;
    for (i = 0; i < 8; i++) {
        const PouchPartyData* d = &pd->partyData[i];
        if (d->HPLevel < 0 || d->HPLevel > 2 || d->attackLevel < 0 || d->attackLevel > 2 || d->techLevel < 0 ||
            d->techLevel > 2) {
            return "a partner's level";
        }
    }
    if (pd->level < 1 || pd->level > 99 || pd->rank < 0 || pd->rank > 3 || pd->jumpLevel < 0 || pd->jumpLevel > 3 ||
        pd->hammerLevel < 0 || pd->hammerLevel > 3) {
        return "a level";
    }
    for (i = 0; i < MAX_KEY_ITEMS; i++) {
        if (!keyIdOk(pd->keyItems[i])) {
            return "a key item";
        }
    }
    for (i = 0; i < MAX_HELD_ITEMS; i++) {
        if (!heldIdOk(pd->heldItems[i])) {
            return "an item";
        }
    }
    for (i = 0; i < MAX_STORED_ITEMS; i++) {
        if (!heldIdOk(pd->storedItems[i])) {
            return "a stored item";
        }
    }
    for (i = 0; i < MAX_BADGES; i++) {
        if (!badgeIdOk(pd->badges[i]) || !badgeIdOk(pd->equippedBadges[i])) {
            return "a badge";
        }
    }
    return NULL;
}

/* A's enemy's battle: its items (the units' held items, the drops: battle.c equips them) */
static const char* wireBattleBad(const WireBattle* o) {
    int i;
    if (!itemIdOk(o->fixedItem) || !itemIdOk(o->itemDropped)) {
        return "an item";
    }
    for (i = 0; i < 8; i++) {
        if (!itemIdOk(o->heldItems[i]) || !itemIdOk(o->stolenItems[i]) || !itemIdOk(o->recoveredItems[i])) {
            return "an item";
        }
    }
    return NULL;
}

/* B: the group A's game made at run time, here (the battle reads it until it's over) */
static BattleGroupSetup sWireGroup;
static BattleUnitSetup sWireUnits[WIRE_UNITS];
static char sWireBgm[48];
static unsigned sWireMissing; /* this battle's pointers not found here (the logs) */

static BattleUnitKind* kindOf(const WireUnit* w, const BattleGroupSetup* own) {
    int ok, i;
    BattleUnitKind* k = (BattleUnitKind*)atPlace(W_KIND, w->kind, &ok);
    if (ok) {
        return k;
    }
    for (i = 0; own != NULL && i < own->numEnemies && own->enemyData != NULL; i++) {
        if (own->enemyData[i].unitKindParams != NULL && (int32_t)own->enemyData[i].unitKindParams->unitType == w->unitType) {
            return own->enemyData[i].unitKindParams;
        }
    }
    sWireMissing++;
    return NULL;
}

/* B: A's battle into this game's NpcBattleInfo (bi holds this game's own same NPC's, used where A's
   isn't in the table: a feature's own static group, made the same way in both games). Never a NULL stage
   while there is one to take (A's, this game's own NPC's, the area's first); 0 when the battle has no
   stage or no group here: not one this game can start (seq_battle.c reads the stage) */
static int unwireBattle(NpcBattleInfo* bi, const WireBattle* o) {
    const NpcBattleInfo own = *bi;
    int i, ok;
    const void* p;
    sWireMissing = 0;
    memset(bi, 0, sizeof(*bi));
    bi->battleFlags = o->battleFlags;
    bi->fixedItem = (ItemType)o->fixedItem;
    bi->itemDropped = (ItemType)o->itemDropped;
    bi->heartsDropped = o->heartsDropped;
    bi->flowersDropped = o->flowersDropped;
    for (i = 0; i < 8; i++) {
        bi->heldItems[i] = (ItemType)o->heldItems[i];
        bi->stolenItems[i] = (ItemType)o->stolenItems[i];
        bi->recoveredItems[i] = (ItemType)o->recoveredItems[i];
        p = atPlace(W_DROP, o->drops[i], &ok);
        bi->itemDropLists[i] = ok ? (ItemDropData*)p : own.itemDropLists[i];
        sWireMissing += !ok;
    }
    for (i = 0; i < 4; i++) {
        bi->scriptWork[i] = o->scriptWork[i];
    }
    bi->random.mode = o->randomMode;
    memcpy(bi->random.values, o->randomValues, sizeof(bi->random.values));
    memcpy(bi->battleRule, o->battleRule, sizeof(bi->battleRule));
    p = atPlace(W_STAGE, o->stage, &ok);
    bi->stageData = ok ? (struct BattleStageData*)p : own.stageData;
    sWireMissing += !ok;
    if (bi->stageData == NULL) {
        bi->stageData = own.stageData; /* (A's had none, or it isn't in this game's table) */
    }
    if (bi->stageData == NULL) {
        bi->stageData = (struct BattleStageData*)atPlace(W_STAGE, 0, &ok); /* (the area's first stage) */
        sWireMissing++;
    }
    if (o->byValue) {
        const BattleGroupSetup* g = own.groupSetup;
        /* (at most 8: battle.c reads each unit's held item, drop list and stolen item from the NpcBattleInfo's
           lists of 8; a 9th would read past them) */
        const int n = o->gNum < 0 ? 0 : o->gNum > 8 ? 8 : o->gNum;
        if (o->gNum > 8) {
            sWireMissing += (unsigned)(o->gNum > WIRE_UNITS ? WIRE_UNITS : o->gNum) - 8;
        }
        memset(&sWireGroup, 0, sizeof(sWireGroup));
        memset(sWireUnits, 0, sizeof(sWireUnits));
        sWireGroup.numEnemies = n;
        sWireGroup.enemyData = sWireUnits;
        sWireGroup.heldItemWeight = o->gHeldW;
        sWireGroup.randomItemWeight = o->gRandW;
        sWireGroup.noItemWeight = o->gNoW;
        p = atPlace(W_PDROP, o->gHp, &ok);
        sWireGroup.hpDropTable = ok ? (PointDropData*)p : (g != NULL ? g->hpDropTable : NULL);
        p = atPlace(W_PDROP, o->gFp, &ok);
        sWireGroup.fpDropTable = ok ? (PointDropData*)p : (g != NULL ? g->fpDropTable : NULL);
        for (i = 0; i < n; i++) {
            const WireUnit* w = &o->units[i];
            BattleUnitSetup* u = &sWireUnits[i];
            u->unitKindParams = kindOf(w, g);
            u->alliance = w->alliance;
            u->attackPhase = w->attackPhase;
            u->position.x = w->x;
            u->position.y = w->y;
            u->position.z = w->z;
            u->hitOffsetX = w->hitOffsetX;
            memcpy(u->unitWork, w->work, sizeof(u->unitWork));
            p = atPlace(W_DROP, w->drop, &ok);
            u->itemDropTable = (ItemDropData*)p;
            sWireMissing += !ok;
        }
        bi->groupSetup = &sWireGroup;
        for (i = 0; i < n; i++) {
            if (sWireUnits[i].unitKindParams == NULL) {
                bi->groupSetup = own.groupSetup; /* (a kind this game hasn't: its own group, logged) */
                break;
            }
        }
    } else {
        p = atPlace(W_GROUP, o->group, &ok);
        bi->groupSetup = ok ? (BattleGroupSetup*)p : own.groupSetup;
        sWireMissing += !ok;
    }
    if (o->bgm[0] == '\0') {
        bi->bgmName = NULL;
    } else if (!nameOk(o->bgm, sizeof(o->bgm))) {
        bi->bgmName = own.bgmName; /* (not a music's name: this game's own) */
        sWireMissing++;
    } else if (own.bgmName != NULL && strncmp(own.bgmName, o->bgm, sizeof(o->bgm)) == 0) {
        bi->bgmName = own.bgmName;
    } else {
        snprintf(sWireBgm, sizeof(sWireBgm), "%.*s", (int)sizeof(o->bgm) - 1, o->bgm);
        bi->bgmName = sWireBgm;
    }
    return bi->stageData != NULL && bi->groupSetup != NULL;
}

typedef struct Go {
    uint8_t kind;
    uint8_t parts;    /* the battle's players (1 to 4: 1 with watchers only) */
    uint8_t watchers; /* the watchers who came, by bit (player p: 1 << (p - 1)) */
    uint8_t roles;    /* which place plays each character (2 bits each: Mario's, the partner's, the companions'),
                         0 the usual (place n plays character n); a co-op guest in seat 1 plays Mario */
    uint32_t id;
    int64_t clock;
    uint32_t rand;
    int32_t delay;         /* A's: frames between a press and the frame it plays in */
    int8_t slotPlayer[4];  /* each place's player number (the net's: the host 1, guests 2..4) */
    int32_t companion[2];  /* players 3 and 4's companions (a partner's follower form) */
} Go;

/* A's answer to a game joining the battle under way: its start at the gate, who's gone since */
typedef struct LateGo {
    Go g;
    uint32_t left; /* the places whose sharing is over, by bit */
    uint32_t frame; /* A's frame then (the logs) */
} LateGo;

typedef struct Small {
    uint8_t kind;
    uint8_t pad[3];
    uint32_t id;
    uint32_t frame;
    uint8_t data[16];
    uint64_t hash;
} Small;

typedef struct Pull {
    uint8_t kind;
    uint8_t pad[3];
    uint32_t id;
    char map[16];
    float x, y, z;
} Pull;

enum { ST_NONE, ST_INVITED_WAIT /* A */, ST_TRIGGERED /* B */, ST_READY_WAIT /* B */, ST_LOADING, ST_SHARED };

static struct {
    int state;
    int iAmA;
    uint32_t id, nextId;
    unsigned long long since;
    Invite invite;      /* B: the one taken */
    PouchData ownPouch; /* B: its own, for after */
    uint8_t ownPm64[8]; /* B: its own Paper Mario 64 partners' places (port/pm64.h), for after */
    int haveOwn;
    int go;
    unsigned invited, readyMask, declinedMask; /* A: the players asked, by bit (player p: 1 << (p - 1)) */
    int partnerOf[SS_PLAYERS + 1];             /* A: each player who came: the partner they had out */
    int aPlayer;                               /* B: the player who asked (the battle's player 1) */
    int ownPartner;                            /* B: its partner before it took A's */
    Go goMsg;
    Go gateTheirs;
    int gateSent, gateGot;
    unsigned gateMask; /* A: the places whose game reached the gate, by bit */
    /* the battle's players: place 0 A (Mario), 1 the partner's, 2 and 3 the companions' */
    int parts;
    int slotPlayer[SLOTS];
    int rolePlace[SLOTS]; /* the place that plays each character (Go.roles) */
    int mySlot;        /* -1: this game watches the battle */
    unsigned watchers; /* the watchers, by bit: sent every controller and hash, never waited for */
    int watching;      /* this game is one of them */
    /* the shared battle (sync what matters) */
    uint32_t frame;           /* frames since the gate */
    uint32_t step;            /* the step gates passed */
    uint32_t topStep;         /* a follower: A's steps here (+1): behind, it catches up */
    uint32_t kCount[16];      /* each kind's messages in this step (R_AC...) */
    unsigned present;         /* the places playing their own units now, by bit (A's word) */
    unsigned leftMask;        /* the places whose sharing is over for good */
    unsigned long long unreachSince[SLOTS]; /* A: since when that place's game can't be reached */
    unsigned long long atAt[SLOTS];         /* A: when that place's game last said how far it has got (B_AT) */
    uint32_t atStep[SLOTS];                 /* A: and the step (+1) */
    uint32_t stallStep[SLOTS]; /* A: the step (+1) a place was taken over at for saying nothing, else 0: its game is
                                  still linked and "caught up", so it is back only from a later step (it was
                                  back the next frame, A's menu for its unit closed, 20 s more: for ever) */
    uint32_t takeAt[SLOTS];    /* a follower: the step (+1) A's latest B_TAKE took that place over at (kept even
                                  while this game is behind), else 0: in that step A's choices for its units
                                  count, never the place's own coming late (A ignores those) */
    int boundary;                          /* A: a phase began this frame: a player back takes their units */
    unsigned long long aGoneSince;          /* a follower: since when A can't be reached */
    uint32_t ackStep[SLOTS];  /* A: each place's step when it last said it waits on A (+1) */
    int ackWhat[SLOTS];
    uint32_t ackK[SLOTS];
    uint64_t sentK[16];       /* A: the decisions of each kind (R_AC... R_CMD) it sent in this step, by k (bit) */
    uint32_t toldStep;        /* a follower: what it last said it waits for */
    int toldWhat;
    uint32_t toldK;
    /* a player's own outcomes, judged on their own screen (beatSend, resultLate) */
    unsigned long long beatAt[SLOTS]; /* A: when that place's game last said where it is (its beat: B_AT sub 1) */
    uint32_t beatStep[SLOTS];         /* A: its step then (+1), 0 none yet */
    uint8_t beatK[SLOTS][3];          /* A: the outcomes of each kind (R_AC, R_GUARD, R_STYLE) it had passed in it */
    unsigned long long beatSent;      /* a follower: its last beat */
    unsigned ownStarts;               /* a follower: its player's units' actions it started itself (no wait) */
    unsigned givenUp;                 /* A: other players' outcomes that never came in time: taken as misses */
    int waitOn;               /* the place waited on now */
    unsigned long long waitSince, noteAt, acHeldSince;
    int waitedNow;
    uint32_t waitFrames0;
    unsigned waits, waitFrames, longest; /* this battle's waits: how many, their frames, the longest */
    unsigned corrections, correctedSteps, unmatched, unmatchedSaid, snapBytes, takeovers;
    unsigned choicesMade, choicesTaken, outcomesTaken;
    unsigned battles, together;
    uint32_t lastId; /* the last shared battle (a game over after its sharing ended: the other's too) */
    int overSent;
    int versus; /* the shared battle is a versus fight (the host's game Mario's side) */
    int versusHeld; /* A: a versus fight's set-up sent, its own fight not started until the other player's yes */
    int everShared; /* this battle reached ST_SHARED (a versus fight never shared is taken back, not fought alone) */
    int ownBrosHp, ownBrosFp; /* B: its own Luigi's, for after */
    unsigned watchInvited;    /* A: the watchers among those asked */
    unsigned coopInvited;     /* A: the co-op guests among those asked (waited for longer: maybe still arriving) */
    Invite pend;              /* B, a co-op guest: the host's invitation kept until this game can take it */
    int pendFrom;             /* ... its sender, 0 none */
    unsigned long long pendAt;
    int late;            /* this game joins the battle under way (as a watcher) */
    uint32_t lateAsked;  /* the battle it asked to join */
    int lateAsks;
    unsigned long long lateAskedAt;
    uint32_t endedId;    /* the last battle this game shared (not joined again late) */
    int flee;            /* B, a co-op guest: a battle of the host's it couldn't share runs here as run away */
    int fleeSaw;         /* ... its battle sequence seen (one met in the field starts a frame or so later) */
    uint32_t fleeId;
    unsigned long long fleeAt;
    unsigned fleeField;  /* ... frames in the field since, no battle under way or ahead */
    unsigned inviteFrames; /* A: the frames its start waited for the answers (the game going on meanwhile) */
    int postWait;    /* B: a script battle's field part after it (marioFBattlePost) waits till it's back there */
    int partnerBack; /* B: its own field partner (+1) to put back once in the field, 0 none */
    unsigned long long pullGoneSince, pullFreeSince; /* B, held for a scene: since when its puller is gone, free */
    /* scenes */
    int sceneFrames; /* A: frames its keys have been off */
    int sceneOn;     /* A: a pull sent */
    uint32_t sceneId;
    unsigned sceneMask; /* A: the players pulled in (bit p - 1: player p) */
    int pullFrom;       /* B: the player whose scene this game is held for */
    int pulled;   /* B: held for A's scene */
    int pullWant; /* B: a pull waiting till it's free */
    int warpWait; /* B: warped, not there yet */
    Pull pull;
    PouchData pulledPouch;
    unsigned noticeAt; /* B: the watching notice last said (retrace count) */
    int textWin;       /* A: the scene's text box sent (its window + 1), 0 none */
    int textEarly;     /* A: a text box opened before its scene began (its window + 1), 0 none */
    unsigned textEarlyAt; /* A: when (retrace count) */
    int textOpen;      /* B: a text box of A's open here */
    int textPend;      /* B: A's text box come before this game was held for its scene (sent with the
                          pull, a Tattle's): shown once it is */
    SceneCam cam;     /* B: A's scene camera, the last */
    int camHeld;       /* B: this game's camera held on A's */
} B;

int ssBattleBusy(void) {
    return B.state != ST_NONE || B.pulled || B.flee; /* (a co-op guest running from a battle: not progress) */
}

int ssScenePulled(void) {
    return B.pulled;
}

int ssBattleHeld(void) {
    return B.state != ST_NONE || B.pulled || B.pullWant || B.warpWait || B.sceneOn || B.flee;
}

/* the battle sequence is this game's now, or the one it goes to */
static int battleAhead(void) {
    return seqCheckSeq() ? seqGetNextSeq() == SEQ_BATTLE : seqGetSeq() == SEQ_BATTLE;
}

static void endBattle(const char* why);
static void leaveShared(const char* why);
static void teamEval(void);
static void gone(int slot);
static void sendLeave(unsigned to, int slot);
static void runAway(void);
static int sSetLinked; /* (the battle's settings, below) */

static unsigned bitOf(int player) {
    return player >= 1 && player <= SS_PLAYERS ? 1u << (player - 1) : 0u;
}

/* this game reaches that player (a guest: through the host) */
static int reach(int player) {
    return ssNetIsHost() ? ssNetLinked(player) : ssNetLinked(1);
}

static int sharing(void) {
    return B.state == ST_SHARED || B.state == ST_LOADING;
}

/* the battle's place of that player, -1 none */
static int slotOf(int player) {
    int s;
    for (s = 0; s < B.parts; s++) {
        if (B.slotPlayer[s] == player) {
            return s;
        }
    }
    return -1;
}

/* the other players still sharing the battle, by bit */
static unsigned othersMask(void) {
    unsigned m = 0;
    int s;
    for (s = 0; s < B.parts; s++) {
        if (s != B.mySlot && !(B.leftMask & (1u << s))) {
            m |= bitOf(B.slotPlayer[s]);
        }
    }
    return (m | B.watchers) & ~bitOf(ssNetMe()); /* (the watchers: sent everything) */
}

/* a session with another game began again (statesync_pc.c): a new one with a battle's player
   (their game started again, or ours) loses what was on the way: theirs ends (the host tells the
   others; A plays their units from there); the host's own new session ends a guest's. A new player
   gets the host's settings. */
static void sceneDone(uint32_t id);

void ssBattleSession(int player, int fresh) {
    int s;
    if (fresh) {
        sSetLinked = 0;
    }
    if (fresh && player == B.pullFrom && (B.pulled || B.pullWant || B.warpWait)) {
        ssLog("scene %u: the other player's game started again: its scene is over here", B.pull.id);
        B.warpWait = 0;
        sceneDone(B.pull.id); /* (its S_DONE would never come) */
    }
    if (!fresh || !sharing()) {
        return;
    }
    if (!ssNetIsHost()) {
        ssLog("battle %u: the other game started a new session: this battle alone", B.id);
        leaveShared("a new session");
        return;
    }
    s = slotOf(player);
    if (s < 0 || s == B.mySlot || (B.leftMask & (1u << s))) {
        return;
    }
    if (s == 0 || B.parts <= 2) {
        ssLog("battle %u: the other game started a new session: this battle alone", B.id);
        leaveShared("a new session");
        return;
    }
    ssLog("battle %u: player %d's game started a new session: out of the battle", B.id, s + 1);
    sendLeave(othersMask() & ~bitOf(player), s);
    gone(s);
}

int portStateSyncWatching(void) {
    return B.pulled;
}

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture */

static const char kWatchingNotice[] =
    "Watching the other player's scene: you can move again once it's over.";
static const char kWaitNotice[] = "Waiting for the other player...";

/* the wait's notice: whom it waits for by name (in the game's font), or the other player */
static void waitNotice(int player) {
    static char sText[160];
    char name[SS_NAME];
    portStateSyncPlayerNameShown(player, name, sizeof(name));
    if (name[0] == '\0') {
        portCoopNotice(kWaitNotice);
        return;
    }
    snprintf(sText, sizeof(sText), portTrGame("Waiting for %s..."), name);
    portCoopNotice(sText);
}


static void sceneDone(uint32_t id);
static void endBattle(const char* why);
static void rulesStop(void);

/* the mode off (this player left): a pull let go, a battle's lockstep ended (each goes on alone) */
void ssBattleStop(void) {
    if (B.pulled || B.pullWant || B.warpWait) {
        B.warpWait = 0;
        sceneDone(B.pull.id);
    }
    if (B.state != ST_NONE) {
        leaveShared("this player left the online game");
    }
    B.flee = 0; /* (off line: this game's battle is its own again) */
    B.sceneOn = 0;
    B.sceneFrames = 0;
    B.lastId = 0;
    sSetLinked = 0;
    rulesStop(); /* (every end: the window's choices, the Start menu's Leave, a removal, Try again) */
}

int portStateSyncSharedBattle(void) {
    return B.state == ST_SHARED;
}

int portStateSyncBattleSelf(void) {
    return B.state == ST_SHARED ? B.mySlot : -1;
}

int portStateSyncBattlePlayers(void) {
    return sharing() ? B.parts : 0;
}

/* to the players in mask (by bit; the host passes a guest's on to the other guests) */
static void sendTo(unsigned mask, const void* p, size_t n) {
    if (mask != 0) {
        ssBattleSendTo(mask, SS_R_BATTLE, p, n);
    }
}

static void sendSmall(unsigned to, int kind, uint32_t frame, const uint8_t* data, uint64_t hash) {
    Small s;
    memset(&s, 0, sizeof(s));
    s.kind = (uint8_t)kind;
    s.id = B.id;
    s.frame = frame;
    if (data != NULL) {
        memcpy(s.data, data, 16);
    }
    s.hash = hash;
    sendTo(to, &s, sizeof(s));
}

/* a place's sharing is over for good (A plays its units from there) */
static void sendLeave(unsigned to, int slot) {
    uint8_t d[16];
    memset(d, 0, sizeof(d));
    d[0] = (uint8_t)slot;
    sendSmall(to, B_LEAVE, B.step, d, 0);
}

/* nothing pressed this frame (the frame both start the battle from: each had its own) */
static void neutralKeys(void) {
    memset(gp->button, 0, sizeof(gp->button));
    memset(gp->buttonNew, 0, sizeof(gp->buttonNew));
    memset(gp->buttonRepeat, 0, sizeof(gp->buttonRepeat));
    memset(gp->buttonUp, 0, sizeof(gp->buttonUp));
    memset(gp->dirs, 0, sizeof(gp->dirs));
    memset(gp->dirsNew, 0, sizeof(gp->dirsNew));
}

/* ---- the battle's settings: the host's in both games ----
 * The host sends its settings that change play (not the local ones: online_local.h, nor its Play as
 * pick) when a session begins and when they change; the guest plays a shared battle on them and has
 * its own back after it. B's Play as pick is A's for the battle: Mario's unit is A's character. */

#define SET_TEXT_MAX (32 * 1024)
#define SWAP_MAX 512

static char* sHostSet;  /* guest: the host's, "key=value" lines */
static uint64_t sSetSent; /* host: the hash of what was sent last */
static int sSetLinked;    /* host: connected at the last look */
static struct {
    char key[48];
    char own[16];
    int known; /* own is this game's value (else its default afterwards) */
} sSwap[SWAP_MAX];
static int sSwapN;

static int sharedKey(const char* key) {
    return key[0] != '\0' && !portOnlineLocalKey(key) && strcmp(key, "playable_luigi") != 0;
}

static size_t settingsText(char* out, size_t cap) {
    size_t n = 0;
    int i, value;
    const char* key;
    out[0] = '\0';
    for (i = 0; (key = portSettingsFieldAt(i, &value)) != NULL; i++) {
        if (sharedKey(key) && n + 64 < cap) {
            n += (size_t)snprintf(out + n, cap - n, "%s=%d\n", key, value);
        }
    }
    for (i = 0; i < portSettingsExtraCount(); i++) {
        key = portSettingsExtraAt(i, &value);
        if (key != NULL && sharedKey(key) && n + 64 < cap) {
            n += (size_t)snprintf(out + n, cap - n, "%s=%d\n", key, value);
        }
    }
    return n;
}

/* ---- the session's field rules (online_local.h): the host's here for the whole session ----
 * A guest plays the field on the host's bag size, coin cap and the like while the session lasts
 * (its settings.txt keeps the player's own meanwhile: portSettingsExtraSaveAs), and has its own back
 * once it's over: off line, or no host for a minute (a dropped connection that comes back keeps
 * them: a bag shrinking and growing again would be worse than a short wait). */
#define RULES_MAX 64
#define RULES_GONE_MS 60000ull

static struct {
    char key[48];
    int own;
    int known; /* own is the player's (else: not read or declared here yet, its default afterwards) */
} sRule[RULES_MAX];
static int sRuleN;
static int sRulesDirty;                  /* the host's settings came: look again */
static unsigned long long sRulesLinkedAt; /* when this guest was last linked */

static int ruleAt(const char* key) {
    int i;
    for (i = 0; i < sRuleN; i++) {
        if (!strcmp(sRule[i].key, key)) {
            return i;
        }
    }
    return -1;
}

static void rulesTake(void) {
    const char* at = sHostSet;
    char line[128], list[256];
    size_t listAt = 0;
    int changed = 0;
    list[0] = '\0';
    while (at != NULL && *at != '\0') {
        const size_t len = strcspn(at, "\n");
        char* eq;
        int r, known, now = 0;
        snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) ? len : sizeof(line) - 1), at);
        at += len;
        if (*at == '\n') {
            at++;
        }
        eq = strchr(line, '=');
        if (eq == NULL || eq == line) {
            continue;
        }
        *eq = '\0';
        if (!portOnlineSessionKey(line) || strlen(line) >= sizeof(sRule[0].key)) {
            continue;
        }
        /* (the player's own value: a declared setting's, or one only read from settings.txt so far, declared
           on first use: master_quest_set, ...; portSettingsFind has only the declared ones) */
        known = portSettingsExtraRaw(line, &now);
        if (known && now == (int)strtol(eq + 1, NULL, 10)) {
            continue;
        }
        if ((r = ruleAt(line)) < 0) {
            if (sRuleN >= RULES_MAX) {
                continue;
            }
            r = sRuleN++;
            snprintf(sRule[r].key, sizeof(sRule[0].key), "%s", line);
            sRule[r].own = known ? now : 0;
            sRule[r].known = known;
        }
        portSettingsExtraForce(line, eq + 1); /* (one not read here yet: kept for its declaration) */
        /* (settings.txt: the player's own, or nothing for a value the player never had) */
        portSettingsExtraSaveAs(line, sRule[r].known ? 1 : 2, sRule[r].own);
        changed++;
        if (listAt + 40 < sizeof(list)) {
            listAt += (size_t)snprintf(list + listAt, sizeof(list) - listAt, " %s=%s", line, eq + 1);
        }
    }
    if (changed > 0) {
        portSettingsApply();
        ssLog("settings: the session's field rules are the host's here (%d):%s", changed, list);
    }
}

static void rulesBack(const char* why) {
    int i;
    char value[16];
    if (sRuleN == 0) {
        return;
    }
    for (i = 0; i < sRuleN; i++) {
        snprintf(value, sizeof(value), "%d", sRule[i].own);
        portSettingsExtraSaveAs(sRule[i].key, 0, 0);
        if (sRule[i].known) {
            portSettingsExtraForce(sRule[i].key, value);
        } else {
            portSettingsExtraToDefault(sRule[i].key);
        }
    }
    portSettingsApply();
    ssLog("settings: this game's own %d field rules back (%s)", sRuleN, why);
    sRuleN = 0;
}

/* the session over (portStateSyncLeave: G.on is 0 from now, so rulesFrame never runs again): the player's
   own rules back at once, and the host's set forgotten (a later session sends its own) */
static void rulesStop(void) {
    rulesBack("left the session");
    free(sHostSet);
    sHostSet = NULL;
    sRulesDirty = 0;
    sRulesLinkedAt = 0;
}

int portStateSyncRuleOwn(const char* key, int* own) {
    const int r = key != NULL ? ruleAt(key) : -1;
    if (r < 0) {
        return 0;
    }
    if (!sRule[r].known) {
        return 2;
    }
    if (own != NULL) {
        *own = sRule[r].own;
    }
    return 1;
}

/* a file loaded during a session (save_files.cpp's per-file settings): a session rule's value from the file is
   the player's own, put back after the session; the host's stays meanwhile. One the host's set has but this
   game plays on already (no rule held: equal): held now too, so the file's value can't override the host's */
int portStateSyncRuleSetOwn(const char* key, int value) {
    int r;
    if (key == NULL || !portOnlineSessionKey(key) || strlen(key) >= sizeof(sRule[0].key)) {
        return 0;
    }
    if ((r = ruleAt(key)) < 0) {
        if (sHostSet == NULL || ssNetIsHost() || !portStateSyncOn() || sRuleN >= RULES_MAX) {
            return 0;
        }
        {
            /* (the host's set has it?) */
            const size_t n = strlen(key);
            const char* at = sHostSet;
            int has = 0;
            while (at != NULL && *at != '\0' && !has) {
                has = strncmp(at, key, n) == 0 && at[n] == '=';
                at = strchr(at, '\n');
                at = at != NULL ? at + 1 : NULL;
            }
            if (!has) {
                return 0;
            }
        }
        r = sRuleN++;
        snprintf(sRule[r].key, sizeof(sRule[0].key), "%s", key);
    }
    sRule[r].own = value;
    sRule[r].known = 1;
    portSettingsExtraSaveAs(key, 1, value);
    ssLog("settings: a file's %s=%d kept as this player's own until the session ends", key, value);
    return 1;
}

/* guest: each frame */
static void rulesFrame(void) {
    const unsigned long long now = ssNetMs();
    if (ssNetConnected()) {
        sRulesLinkedAt = now;
        if (sRulesDirty && sHostSet != NULL && !ssBattleBusy()) {
            sRulesDirty = 0; /* (in a shared battle the battle's own swap has them: after it) */
            rulesTake();
        }
    } else if (sRuleN > 0 && (!portStateSyncOn() || now - sRulesLinkedAt > RULES_GONE_MS)) {
        rulesBack(portStateSyncOn() ? "no host for a minute" : "off line");
        sRulesDirty = 1; /* (a host found again: its rules again) */
    }
}

/* host: its settings across when the session begins or they change (a look every 2 seconds) */
static void settingsFrame(unsigned frame) {
    static char text[SET_TEXT_MAX];
    size_t n, i;
    uint64_t h = 0xCBF29CE484222325ull;
    const int linked = ssNetConnected();
    if (!ssNetIsHost()) {
        rulesFrame();
        return;
    }
    rulesBack("this game hosts");
    if (!linked) {
        sSetLinked = 0;
        return;
    }
    if (sSetLinked && frame % 120 != 0) {
        return;
    }
    n = settingsText(text, sizeof(text));
    for (i = 0; i < n; i++) {
        h = (h ^ (uint8_t)text[i]) * 0x100000001B3ull;
    }
    if (!sSetLinked || h != sSetSent) {
        ssNetSendRel(SS_R_SETTINGS, text, n);
        ssLog("settings: this game's (the host's) sent for shared battles (%u bytes)", (unsigned)n);
    }
    sSetLinked = 1;
    sSetSent = h;
}

static void gotSettings(const uint8_t* p, size_t n) {
    free(sHostSet);
    sHostSet = (char*)malloc(n + 1);
    if (sHostSet == NULL) {
        return;
    }
    memcpy(sHostSet, p, n);
    sHostSet[n] = '\0';
    sRulesDirty = 1;
    ssLog("settings: the host's came (%u bytes): shared battles play on them", (unsigned)n);
}

/* this game's value of key now, as text: 0 when it has none */
static int ownValue(const char* key, char* out, size_t cap) {
    int i, value;
    const char* k;
    const int* extra;
    for (i = 0; (k = portSettingsFieldAt(i, &value)) != NULL; i++) {
        if (!strcmp(k, key)) {
            snprintf(out, cap, "%d", value);
            return 1;
        }
    }
    if ((extra = portSettingsFind(key)) != NULL) {
        snprintf(out, cap, "%d", *extra);
        return 1;
    }
    return 0;
}

static void swapOne(const char* key, const char* value) {
    char own[16];
    const int known = ownValue(key, own, sizeof(own));
    if ((known && !strcmp(own, value)) || sSwapN >= SWAP_MAX || strlen(key) >= sizeof(sSwap[0].key)) {
        return;
    }
    snprintf(sSwap[sSwapN].key, sizeof(sSwap[0].key), "%s", key);
    snprintf(sSwap[sSwapN].own, sizeof(sSwap[0].own), "%s", known ? own : "");
    sSwap[sSwapN].known = known;
    sSwapN++;
    portSettingsSetForRun(key, value);
}

/* ---- the host's rules beside this player's own (the Multiplayer page's Rules, the join note) ----
   Every setting the host sends (those that change play: never display, sound or controls) whose value
   differs from the player's own: the field rules' own is the one kept aside (sRule), a battle's swap's
   its own (sSwap), else the setting as it is here. */
#define DIFF_MAX 64
static struct {
    char key[48];
    int own, host;
} sDiff[DIFF_MAX];
static int sDiffN;

int portStateSyncRuleDiffs(void) {
    const char* at = sHostSet;
    char line[128], own[16];
    sDiffN = 0;
    if (!portStateSyncOn() || ssNetIsHost()) {
        return 0;
    }
    while (at != NULL && *at != '\0' && sDiffN < DIFF_MAX) {
        const size_t len = strcspn(at, "\n");
        char* eq;
        int r, i, mine, known = 0;
        snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) ? len : sizeof(line) - 1), at);
        at += len;
        if (*at == '\n') {
            at++;
        }
        eq = strchr(line, '=');
        if (eq == NULL || eq == line) {
            continue;
        }
        *eq = '\0';
        if (!sharedKey(line) || strlen(line) >= sizeof(sDiff[0].key)) {
            continue;
        }
        mine = 0;
        if ((r = ruleAt(line)) >= 0) {
            mine = sRule[r].own;
            known = 1;
        } else {
            for (i = 0; i < sSwapN; i++) {
                if (!strcmp(sSwap[i].key, line) && sSwap[i].known) {
                    mine = (int)strtol(sSwap[i].own, NULL, 10);
                    known = 1;
                    break;
                }
            }
            if (!known && ownValue(line, own, sizeof(own))) {
                mine = (int)strtol(own, NULL, 10);
                known = 1;
            }
        }
        if (!known || mine == (int)strtol(eq + 1, NULL, 10)) {
            continue; /* (one this game doesn't have: nothing to say about it) */
        }
        snprintf(sDiff[sDiffN].key, sizeof(sDiff[0].key), "%s", line);
        sDiff[sDiffN].own = mine;
        sDiff[sDiffN].host = (int)strtol(eq + 1, NULL, 10);
        sDiffN++;
    }
    return sDiffN;
}

const char* portStateSyncRuleDiffAt(int i, int* own, int* host) {
    if (i < 0 || i >= sDiffN) {
        return NULL;
    }
    *own = sDiff[i].own;
    *host = sDiff[i].host;
    return sDiff[i].key;
}

/* the battle begins: the host's settings here (the guest), A's pick (B) */
static void settingsBattleBegin(void) {
    sSwapN = 0;
    if (!ssNetIsHost() && sHostSet != NULL) {
        const char* at = sHostSet;
        char line[128];
        while (*at != '\0') {
            const size_t len = strcspn(at, "\n");
            char* eq;
            snprintf(line, sizeof(line), "%.*s", (int)(len < sizeof(line) ? len : sizeof(line) - 1), at);
            at += len;
            if (*at == '\n') {
                at++;
            }
            eq = strchr(line, '=');
            if (eq != NULL && eq != line) {
                *eq = '\0';
                if (sharedKey(line)) {
                    swapOne(line, eq + 1);
                }
            }
        }
    }
    if (!B.iAmA) {
        char pick[16];
        snprintf(pick, sizeof(pick), "%d", (int)B.invite.playAs);
        swapOne("playable_luigi", pick);
    }
    if (sSwapN > 0) {
        portSettingsHoldSaves(1);
        portSettingsApply();
        ssLog("battle %u: %d settings as the host's / the other player's for the battle", B.id, sSwapN);
    }
}

static void settingsBattleEnd(void) {
    int i;
    if (sSwapN == 0) {
        return;
    }
    for (i = 0; i < sSwapN; i++) {
        if (sSwap[i].known) {
            portSettingsSetForRun(sSwap[i].key, sSwap[i].own);
        } else {
            portSettingsExtraToDefault(sSwap[i].key);
        }
    }
    portSettingsHoldSaves(0);
    portSettingsApply();
    ssLog("battle %u: this game's own %d settings back", B.id, sSwapN);
    sSwapN = 0;
}

static void storeClear(void);

/* the battle's places from A's start (Go): this game's own among them (0 when it isn't) */
static int takePlaces(const Go* g) {
    int s;
    B.parts = g->parts >= 1 && g->parts <= SLOTS ? g->parts : 2;
    B.mySlot = -1;
    B.watchers = g->watchers;
    B.watching = (B.watchers & bitOf(ssNetMe())) != 0;
    for (s = 0; s < SLOTS; s++) {
        B.slotPlayer[s] = s < B.parts ? g->slotPlayer[s] : 0;
        if (s < B.parts && B.slotPlayer[s] == ssNetMe()) {
            B.mySlot = s;
        }
        B.rolePlace[s] = g->roles != 0 ? (g->roles >> (2 * s)) & 3 : s;
    }
    /* (another game's steps and choices can come before this one is through the gate: kept from here) */
    storeClear();
    B.leftMask = 0;
    B.present = (1u << B.parts) - 1;
    B.step = B.topStep = B.frame = 0;
    memset(B.atAt, 0, sizeof(B.atAt));
    memset(B.atStep, 0, sizeof(B.atStep));
    B.boundary = 0;
    memset(B.kCount, 0, sizeof(B.kCount));
    memset(B.sentK, 0, sizeof(B.sentK));
    memset(B.unreachSince, 0, sizeof(B.unreachSince));
    memset(B.stallStep, 0, sizeof(B.stallStep));
    memset(B.takeAt, 0, sizeof(B.takeAt));
    memset(B.ackStep, 0, sizeof(B.ackStep));
    memset(B.beatAt, 0, sizeof(B.beatAt));
    memset(B.beatStep, 0, sizeof(B.beatStep));
    memset(B.beatK, 0, sizeof(B.beatK));
    B.beatSent = 0;
    B.ownStarts = B.givenUp = 0;
    B.aGoneSince = 0;
    B.toldStep = 0;
    B.waitOn = 0;
    B.waitSince = B.noteAt = B.acHeldSince = 0;
    B.waitedNow = 0;
    B.waits = B.waitFrames = B.longest = 0;
    B.corrections = B.correctedSteps = B.unmatched = B.unmatchedSaid = B.snapBytes = B.takeovers = 0;
    B.choicesMade = B.choicesTaken = B.outcomesTaken = 0;
    B.gateSent = B.gateGot = 0;
    B.gateMask = 0;
    return B.mySlot >= 0 || B.watching;
}

static const char* placeName(int s) {
    static const char* const kNames[SLOTS] = {"player 1, Mario", "player 2, the partner", "player 3, a companion",
                                              "player 4, a companion"};
    return s >= 0 && s < SLOTS ? kNames[s] : B.watching ? "a watcher" : "not a player";
}

/* every game is in the battle sequence: each loads the battle (its own pace: files, fades), up to
   battle_init, where they wait for each other (portStateSyncBattleGate) */
static void beginLoading(const Go* g) {
    int companions[2];
    settingsBattleBegin();
    B.overSent = 0;
    portLockstepBegin(g->clock);
    portRandSetState(g->rand);
    neutralKeys();
    B.state = ST_LOADING;
    companions[0] = g->companion[0];
    companions[1] = g->companion[1];
    portCoopBattleCompanions(B.parts, companions);
    B.since = ssNetMs();
    if (B.parts <= 2) {
        ssLog("battle %u: both games load the battle (%s)", B.id, placeName(B.mySlot));
    } else {
        ssLog("battle %u: all %d games load the battle (%s; players %d %d %d %d by place; companions %d, %d)", B.id,
              B.parts, placeName(B.mySlot), B.slotPlayer[0], B.slotPlayer[1], B.slotPlayer[2], B.slotPlayer[3],
              companions[0], companions[1]);
    }
}

static void beginShared(const Go* g) {
    portLockstepBegin(g->clock);
    portRandSetState(g->rand);
    neutralKeys();
    B.state = ST_SHARED;
    B.everShared = 1;
    B.frame = 0;
    B.together++;
    ssLog("battle %u: the shared battle begins (%s, clock %lld, random %08x): player 1's game decides, the "
          "others follow its steps",
          B.id, placeName(B.mySlot), (long long)g->clock, g->rand);
}

/* the battle's end here: a follower's taking A's side as run away (A gone: the battle ends at its
   next check, nothing lost: its own pouch comes back) */
static void runAway(void) {
    if (!B.iAmA && _battleWorkPointer != NULL && gp != NULL && gp->inBattle) {
        _battleWorkPointer->flags |= BATTLE_FLAG_ESCAPED;
    }
}

static void afterFrame(void);
#ifdef TTYD_BACKEND_AURORA
int portVersusAbort(void); /* (port/versus.h, included with the versus code below) */
#endif

/* A script's battle this game fought as another player's: takeInvite started it (as evt_npc_battle_start would),
   not this game's own script, which hadn't come to that fight yet. Coming to it later (a co-op guest's copy of
   the room's scene, run once the host's Mario crossed its trigger here; a game that joined a guest's), that
   script fought it again, and as the one asking pulled everyone into a second fight with the same enemies
   (mp_online_goons_leave3: the goons fought twice; a guest back in the room after a leave was player 1 of the
   second). The script's evt_npc_battle_start asks first (portStateSyncScriptBattleFought, evt_npc.c): the same
   enemy, in the same room, won together not long ago, is that fight's result, with no battle */
#define SCRIPT_TAKEN_MS 180000ull
static struct {
    const NpcEntry* taking; /* (takeInvite: the enemy of the script's battle being taken) */
    const NpcEntry* npc;
    char map[32];
    unsigned long long at;
    uint32_t id;
} sScriptTaken;

static void scriptTakenNote(const NpcEntry* npc) {
    sScriptTaken.taking = NULL;
    if (npc == NULL || gp == NULL) {
        return;
    }
    sScriptTaken.npc = npc;
    snprintf(sScriptTaken.map, sizeof(sScriptTaken.map), "%s", gp->currentMapName);
    sScriptTaken.at = ssNetMs();
    sScriptTaken.id = B.id;
}

int portStateSyncScriptBattleFought(const void* npc) {
    const FieldBattleData* fb = fbatGetPointer();
    const int hit = sScriptTaken.npc != NULL && npc == sScriptTaken.npc && gp != NULL && ssNetConnected() &&
                    strcmp(sScriptTaken.map, gp->currentMapName) == 0 && ssNetMs() - sScriptTaken.at < SCRIPT_TAKEN_MS &&
                    fb != NULL && BattleInformationGetResult((FieldBattleInfo*)&fb->info) == 1;
    if (sScriptTaken.npc != NULL && npc == sScriptTaken.npc) {
        sScriptTaken.npc = NULL; /* (once: a fight after that is the script's own) */
    }
    if (hit) {
        ssLog("battle %u: this game's own script comes to the fight it already won with the others here (%s): "
              "won, not fought again",
              sScriptTaken.id, sScriptTaken.map);
    }
    return hit;
}

static void endBattle(const char* why) {
    /* playing together, a co-op guest has no battle of its own (its keys follow the host's world): one of the
       host's it took but can't share (refused as too late, its start lost, left out) isn't fought here alone,
       where nobody plays it and it never ends (online_coop_battle_arriving: stuck loading to the end); it runs
       from it, back to the field, and may join the host's under way from there (lateFrame) */
    const FieldBattleData* fbNow = fbatGetPointer();
    const int flee = !B.iAmA && B.haveOwn && !B.versus && portStateSyncCoopGuest() &&
                     (battleAhead() || (B.invite.route == 0 && fbNow != NULL && fbNow->mode == 5)); /* (or soon) */
    const int wasShared = B.state == ST_SHARED;
    /* (a versus fight that ends before it was ever shared, in either game: cancelled, declined, never started, or
       given up at the gate (ST_LOADING: the partner lost before the GO, or after it before the gate let go). Each
       game had already asked its own, the guest at the set-up and the host at the yes: taken back here, not fought
       alone with nobody on the other side. One that was shared and lost its partner mid-fight is fought to the end) */
    const int versusCut = B.versus && B.state != ST_NONE && !B.everShared;
    if (B.waitSince != 0 && B.frame - B.waitFrames0 > B.longest) {
        B.longest = B.frame - B.waitFrames0; /* (a wait the end cut short: the longest too) */
    }
    if (sharing()) {
        ssLog("battle %u: the shared battle ends (%s) after %u frames, %u steps: corrections %u in %u steps, %u "
              "unmatched, snapshots %u bytes; choices made %u, taken %u, outcomes taken %u; this game waited %u "
              "frames (%u waits, the longest %u frames); takeovers %u; own action starts %u, late outcomes taken "
              "as misses %u",
              B.id, why, B.frame, B.step, B.corrections, B.correctedSteps, B.unmatched, B.snapBytes, B.choicesMade,
              B.choicesTaken, B.outcomesTaken, B.waitFrames, B.waits, B.longest, B.takeovers, B.ownStarts, B.givenUp);
        B.lastId = B.id;
    }
    if (!B.iAmA && B.invite.route == 1 && !B.versus && wasShared && B.step > 0 && ssNetConnected()) {
        /* (fought to its end with the others: not one this player left at its start, the room left behind) */
        scriptTakenNote(sScriptTaken.taking);
    }
    /* (a co-op guest that never shared it, refused or its start lost, may still join the host's under way) */
    B.endedId = !wasShared && !B.iAmA && portStateSyncCoopGuest() ? 0 : B.id;
    B.late = 0;
    B.state = ST_NONE;
    portCoopBattleCompanions(0, NULL);
    settingsBattleEnd();
    const int wasB = !B.iAmA && B.haveOwn;
    if (!B.iAmA && B.haveOwn) {
        /* B's own pouch back; A's game sends what the battle changed (its items, coins, Star Points and
           level: the progress's changes, sent once A is back in the field) */
        PouchData* pd = pouchGetPtr();
        static PouchData sAfter;
        int i, brosHp = 0, brosFp = 0;
        /* playing together, the battle's party is this player's too: HP, FP, star power and the partners'
           HP as it left them (the progress never carries them: the HUD showed the ones from before) */
        const int together = portStateSyncCoopGuest() && B.aPlayer == 1 && !B.versus;
        sAfter = *pd;
        portBrothersGetStats(&brosHp, &brosFp);
        *pd = B.ownPouch;
        portPm64WornSet(B.ownPm64);
        if (together) {
            pd->currentHP = sAfter.currentHP;
            pd->currentFP = sAfter.currentFP;
            pd->currentSP = sAfter.currentSP;
            pd->audienceCount = sAfter.audienceCount;
            for (i = 0; i < 8; i++) {
                pd->partyData[i].currentHP = sAfter.partyData[i].currentHP;
            }
            portBrothersSetStats(brosHp, brosFp);
            ssLog("battle %u: the shared party's HP %d, FP %d, star power %d kept here", B.id, pd->currentHP,
                  pd->currentFP, pd->currentSP);
        } else {
            portBrothersSetStats(B.ownBrosHp, B.ownBrosFp);
        }
        B.haveOwn = 0;
        ssLog("battle %u: this player's own pouch back (coins %d, HP %d)", B.id, pd->coins, pd->currentHP);
        if (B.invite.route == 1 && !B.versus) {
            /* (a script battle's field part after it: once back in the field, not before a battle that's still
               to run here, cancelled or not started yet) */
            B.postWait = 1;
        }
        if (!B.versus && !portStateSyncCoopGuest() && B.ownPartner != marioGetParty()) {
            /* (its own field partner back: A's was out for the battle; a co-op guest's is the host's anyway) */
            B.partnerBack = B.ownPartner + 1;
        }
    }
    afterFrame(); /* (now when this game is already back in the field: the battle's end there) */
    if (wasB || B.versus) {
        /* (B: its own pouch back is no change of its own; a versus fight's loadout isn't progress) */
        ssProgressRebase();
    } else if (B.iAmA && B.lastId == B.id) {
        /* A: the battle's changes (Star Points, a level, items used, coins) stay the shadow's difference:
           sent with the next frame's changes, as the others' games took their own pouches back (taking it
           as the shadow lost them: a guest's HUD showed 0 Star Points after a battle) */
        ssLog("battle %u: what the battle changed goes to the others", B.id);
    }
    B.versus = 0;
    B.versusHeld = 0;
    if (flee) {
        B.flee = 1;
        B.fleeId = B.id;
        B.fleeSaw = 0;
        B.fleeField = 0;
        B.fleeAt = ssNetMs();
        ssLog("battle %u: playing together, the host's battle isn't fought here alone: this game runs from it, "
              "back to the field",
              B.id);
    }
#ifdef TTYD_BACKEND_AURORA
    if (versusCut) {
        const int how = portVersusAbort();
        if (how != 0) {
            ssLog("battle %u: the versus fight isn't fought here alone: %s", B.id,
                  how == 1 ? "its start taken back" : "this game's, under way, ended (no winner) and back");
        }
    }
#else
    (void)versusCut;
#endif
}

/* B back in the field after a battle it took from A: the script battle's field part (what evt_npc_battle_start's
   script does after it) and its own field partner (A's was out for the battle) */
static void afterFrame(void) {
    if ((!B.postWait && !B.partnerBack) || gp == NULL || seqGetSeq() != SEQ_GAME || seqCheckSeq() || gp->inBattle) {
        return;
    }
    if (B.postWait) {
        B.postWait = 0;
        marioCtrlOn();
        if (partyGetPtr(marioGetPartyId()) != NULL) {
            partyCtrlOn();
        }
        marioFBattlePost();
    }
    if (B.partnerBack) {
        const int own = B.partnerBack - 1;
        B.partnerBack = 0;
        if (own != marioGetParty()) {
            marioPartyKill();
            if (own > 0) {
                marioPartyEntry((PartyId)own);
            }
            ssLog("battle %u: this player's own partner (%d) back in the field", B.id, own);
        }
    }
}

void ssBattleFieldForget(const char* why) {
    sScriptTaken.npc = NULL; /* (that room's fights: another world now) */
    sScriptTaken.taking = NULL;
    if (B.postWait || B.partnerBack) {
        ssLog("battle %u: its field part after it forgotten (%s)", B.id, why);
        B.postWait = 0;
        B.partnerBack = 0;
    }
}

/* every game frame, the session on or not (portStateSyncGameFrame): a session that ends while this game is
   still in a battle it took (the host gone, removed, a Leave) leaves the field part to do, and nothing of
   the session's own runs then (Mario's controls stayed off) */
void ssBattleFieldFrame(void) {
    const SequenceType seq = seqGetSeq();
    if (seq == SEQ_TITLE || seq == SEQ_LOGO || seq == SEQ_LOAD || seq == SEQ_GAME_OVER) {
        /* (a game over, the title, a file chosen or a new game: that file's field isn't there any more) */
        ssBattleFieldForget(seq == SEQ_GAME_OVER ? "a game over" : "the title or a file loaded");
        return;
    }
    afterFrame();
}

/* a co-op guest's flight (endBattle): the battle ends here as run away as soon as it's built; over once this
   game is back in the field */
static void fleeFrame(void) {
    if (!B.flee) {
        return;
    }
    if (B.state != ST_NONE) {
        B.flee = 0; /* (another battle taken: its own) */
        return;
    }
    if (battleAhead() || gp->inBattle) {
        B.fleeSaw = 1;
        B.fleeField = 0;
    } else if (seqGetSeq() == SEQ_GAME && !seqCheckSeq()) {
        B.fleeField++;
    }
    /* (one met in the field starts within a frame or so: 60 field frames with none is none coming, not only 3 s
       of real time, which a fast game's frames outran: the guest's merge after it came late, walk_chase_2p_online) */
    if (seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle &&
        (B.fleeSaw || B.fleeField >= 60 || ssNetMs() - B.fleeAt > 3000)) {
        B.flee = 0;
        ssProgressRebase(); /* (what the battle here changed isn't the shared progress) */
        ssLog("battle %u: back in the field after running from the host's battle", B.fleeId);
        return;
    }
    if (gp->inBattle && _battleWorkPointer != NULL && !(_battleWorkPointer->flags & BATTLE_FLAG_ESCAPED)) {
        _battleWorkPointer->flags |= BATTLE_FLAG_ESCAPED;
        ssLog("battle %u: the host's battle here ends as run away", B.fleeId);
    }
}

/* this game's sharing ends before the battle does: the others told (A plays its units from there;
   A's leaving ends everyone's: each finishes as run away) */
static void leaveShared(const char* why) {
    if (sharing() && B.mySlot >= 0) {
        sendLeave(othersMask(), B.mySlot);
    }
    endBattle(why);
}

/* a place's sharing is over for good */
static void gone(int s) {
    if (s < 0 || s >= B.parts || s >= SLOTS) { /* (parts is at most SLOTS: the bound for the analyzer) */
        return;
    }
    B.leftMask |= 1u << s;
    if (!sharing()) {
        return;
    }
    if (s == B.mySlot) {
        ssLog("battle %u: left out of the shared battle: this battle alone", B.id);
        endBattle("left out");
    } else if (s == 0 && !B.iAmA) {
        ssLog("battle %u: player 1 left the battle: this game finishes it alone, as run away", B.id);
        runAway();
        endBattle("player 1 left");
    } else if (othersMask() == 0) {
        endBattle(B.parts <= 2 ? "the other went on alone" : "the others went on alone");
    } else {
        ssLog("battle %u: player %d left the battle: %s", B.id, s + 1,
              B.iAmA ? "this player plays their units" : "player 1's player plays their units");
    }
}

/* every other player of the battle (also those whose sharing ended) */
static unsigned allOthers(void) {
    unsigned m = 0;
    int s;
    for (s = 0; s < B.parts; s++) {
        if (s != B.mySlot) {
            m |= bitOf(B.slotPlayer[s]);
        }
    }
    return (m | B.watchers) & ~bitOf(ssNetMe()); /* (the watchers: sent everything) */
}

/* this game's game over in shared battle id: the others' too */
static void sendGameOver(uint32_t id) {
    Small s;
    memset(&s, 0, sizeof(s));
    s.kind = B_GAMEOVER;
    s.id = id;
    sendTo(allOthers(), &s, sizeof(s));
    B.overSent = 1;
    ssLog("battle %u: game over here: the other game's too", id);
}

/* another game's game over in shared battle id: this one goes there too, if it's still in it */
static void gotGameOver(uint32_t id) {
    const int inIt = (id == B.id && sharing()) || (id == B.lastId);
    if (!inIt || seqGetSeq() == SEQ_GAME_OVER || seqGetNextSeq() == SEQ_GAME_OVER) {
        return;
    }
    B.overSent = 1; /* (not told back) */
    if (B.state != ST_NONE) {
        endBattle("the other game's game over");
    }
    B.flee = 0; /* (the game over, not a flight) */
    B.lastId = 0;
    ssLog("battle %u: the other player's game over: this game's too", id);
    seqSetSeq(SEQ_GAME_OVER, gp != NULL && gp->inBattle ? (const char*)1 : NULL, NULL);
}

/* ---- A ---- */

/* the players in this game's room, free (in the field, their keys on, no battle), by bit */
static unsigned freeHere(void) {
    unsigned m = 0;
    int p;
    for (p = 1; p <= SS_PLAYERS; p++) {
        const SsPeer* o;
        if (p == ssNetMe() || !ssPlayerHere(p) || !reach(p)) {
            continue;
        }
        o = ssPeerOf(p);
        if (o->valid && !o->inBattle && !o->keysOff && o->seq == SEQ_GAME && !o->watch) {
            m |= bitOf(p);
        }
    }
    return m;
}

/* playing together, the host: every seated guest it reaches, by bit, whatever its last word says (it follows
   the host's room: one just arriving, or whose word is late, is still this battle's player; takeInvite keeps
   the invitation until it can take it). Never one already in a battle, or watching. */
static unsigned coopComing(void) {
    unsigned m = 0;
    int p;
    if (!ssNetIsHost()) {
        return 0;
    }
    for (p = 2; p <= SS_PLAYERS; p++) {
        const SsPeer* o;
        if (p == ssNetMe() || !reach(p) || portStateSyncSeatOf(p) < 1) {
            continue;
        }
        o = ssPeerOf(p);
        if (o->valid && (o->watch || o->inBattle)) {
            continue;
        }
        m |= bitOf(p);
    }
    return m;
}

/* a raid boss under way here, under sync (raid_aurora.c): the host's fight is every player's. A guest's game
   comes into the boss's room with its keys on before its script has put the boss's stand-in there (some 20
   frames on): an invitation then found "that enemy isn't here", the host went on alone and the guest joined
   under way as a watcher. So, as playing together: the host waits for its guests as for arriving co-op
   guests, and a guest keeps the invitation until it can take it */
static int raidShared(void) {
#ifdef TTYD_BACKEND_AURORA
    return portRaidActive() && portStateSyncPartyRole() != 0;
#else
    return 0;
#endif
}

/* the watchers in this room, in the field (their keys off: watching) */
static unsigned watchersHere(void) {
    unsigned m = 0;
    int p;
    for (p = 1; p <= SS_PLAYERS; p++) {
        const SsPeer* o;
        if (p == ssNetMe() || !ssPlayerHere(p) || !reach(p)) {
            continue;
        }
        o = ssPeerOf(p);
        if (o->valid && o->watch && !o->inBattle && o->seq == SEQ_GAME) {
            m |= bitOf(p);
        }
    }
    return m;
}

static int countBits(unsigned m) {
    int n = 0;
    for (; m != 0; m &= m - 1) {
        n++;
    }
    return n;
}

static void invite(unsigned to) {
    FieldBattleData* fb = fbatGetPointer();
    Invite* v = &B.invite;
    memset(v, 0, sizeof(*v));
    v->kind = B_INVITE;
    B.id = ++B.nextId * 8 + (uint32_t)ssNetMe();
    v->id = B.id;
    snprintf(v->map, sizeof(v->map), "%s", gp->currentMapName);
    snprintf(v->npc, sizeof(v->npc), "%s", fb->target->description);
    v->hitKind = fb->hitKind;
    v->firstAttackParam = fb->firstAttackParam;
    v->firstAttackFlags = fb->firstAttackFlags;
    v->partner = marioGetParty();
    v->route = fb->mode == 5 ? 0 : 1;
    wireBattle(&v->battle, &fb->target->battleInfo);
    /* (the logs: where this program has the enemy's group, from its code; another build's differs, which
       is why it crosses as its place in the area's table, wireBattle) */
#ifdef TTYD_LAYOUT_PAD
    {
        int portLayoutPadTouch(void);
        (void)portLayoutPadTouch();
    }
#endif
    ssLog("battle %u: layout: the group at code%+lld here, sent as table place %d", B.id,
          fb->target->battleInfo.groupSetup != NULL
              ? (long long)((intptr_t)fb->target->battleInfo.groupSetup - (intptr_t)&portStateSyncSharedBattle)
              : 0LL,
          (int)v->battle.group);
    if (v->battle.byValue || v->battle.group == -2 || v->battle.stage == -2) {
        ssLog("battle %u: its group %s, its stage %s in the area's table", B.id,
              v->battle.byValue ? "by value" : v->battle.group == -2 ? "not" : "found",
              v->battle.stage == -2 ? "not" : "found");
    }
    v->pouch = *pouchGetPtr();
    portPm64WornGet(v->pm64);
    v->playAs = portPlayAs();
    portBrothersGetStats(&v->brosHp, &v->brosFp);
    sendTo(to, v, sizeof(*v));
    B.iAmA = 1;
    B.invited = to;
    B.watchInvited = to & watchersHere();
    B.coopInvited = to & (raidShared() && ssNetIsHost() ? ~0u : coopComing()) & ~B.watchInvited;
    B.readyMask = B.declinedMask = 0;
    B.state = ST_INVITED_WAIT;
    B.since = ssNetMs();
    B.inviteFrames = 0;
    if (to == 0) {
        ssLog("battle %u: %s %s here, nobody else free in the room: shared all the same (whoever comes in "
              "may join it)",
              B.id, v->npc, v->route ? "fought (a script's battle)" : "met");
    }
    if ((B.coopInvited & ~freeHere()) != 0) {
        ssLog("battle %u: playing together: the seated guests not free here yet (%x) invited too, waited for up to "
              "%d s (still arriving, or their word late)",
              B.id, B.coopInvited & ~freeHere(), ARRIVE_WAIT_MS / 1000);
    }
    if (to == 0) {
        /* (logged above) */
    } else if (countBits(to) == 1) {
        ssLog("battle %u: %s %s here with the other player in the room: invited (hit %x, partner %d)", B.id, v->npc,
              v->route ? "fought (a script's battle)" : "met", v->hitKind, v->partner);
    } else {
        ssLog("battle %u: %s %s here with %d other players in the room: all invited (hit %x, partner %d)", B.id,
              v->npc, v->route ? "fought (a script's battle)" : "met", countBits(to), v->hitKind, v->partner);
    }
}

/* A at the battle's start, once a frame while its battle sequence opens (the curtain, then held at the gate,
   portStateSyncBattleGate): everyone invited answers (comes, says no) or is too late, then those who came go,
   each in their place. 1 while it still waits. Never a wait inside one frame: the game goes on drawing and
   reading the network meanwhile (a loop here stood the host's whole game still for up to 3 s an invitation,
   8 s for a co-op guest still arriving, 15 s for a versus fight; and a co-op guest whose yes came while it
   stood never came in: online_coop_battle_arriving) */
/* (tests: $TTYD_VERSUS_DROP_AFTER_YES=<ms>, A drops its versus fight that long after the other player's yes,
   holding its start meanwhile: the guest's taken back on its way to the ring or with its battle opened.
   together_party_versus_drop_*) */
static uint32_t sDropId; /* the versus fight it's armed for (0 none) */

static int versusDropArmed(void) {
    return sDropId != 0 && sDropId == B.id && B.iAmA && B.versus && B.state == ST_INVITED_WAIT;
}

#ifdef TTYD_BACKEND_AURORA
static long sDropAfterYes = -2; /* -2 not read yet, -1 off */
static unsigned long long sDropYesAt;

static void versusDropArm(void) {
    if (sDropAfterYes == -2) {
        const char* e = getenv("TTYD_VERSUS_DROP_AFTER_YES");
        sDropAfterYes = e != NULL && *e != '\0' ? atol(e) : -1;
    }
    if (sDropAfterYes >= 0) {
        sDropId = B.id;
        sDropYesAt = ssNetMs();
    }
}

static void versusDropTest(void) {
    if (!versusDropArmed() || ssNetMs() - sDropYesAt < (unsigned long long)sDropAfterYes) {
        return;
    }
    sDropId = 0;
    ssLog("battle %u: test: the host drops its versus fight %ld ms after the yes", B.id, sDropAfterYes);
    {
        Small c;
        memset(&c, 0, sizeof(c));
        c.kind = B_CANCEL;
        c.id = B.id;
        sendTo(B.invited, &c, sizeof(c));
    }
    endBattle("dropped (test)");
}
#endif

static int inviteFrame(void) {
    const unsigned long long limit = B.versus ? VERSUS_WAIT_MS : INVITE_WAIT_MS;
    unsigned came, waiting;
    int p, s, n;
    if (B.state != ST_INVITED_WAIT || !B.iAmA) {
        return 0;
    }
    if (B.versusHeld) {
        return 1; /* (a battle of its own while a versus fight waits for its yes: versusHeldFrame drops that) */
    }
    if (versusDropArmed()) {
        return 1; /* (tests: its start held till the drop, versusDropTest) */
    }
    waiting = B.invited & ~(B.readyMask | B.declinedMask);
    for (p = 1; p <= SS_PLAYERS; p++) {
        if ((waiting & bitOf(p)) && !reach(p)) {
            B.declinedMask |= bitOf(p); /* (gone: not waited for) */
        }
    }
    waiting = B.invited & ~(B.readyMask | B.declinedMask);
    if (waiting != 0 &&
        ssNetMs() - B.since < ((waiting & B.coopInvited) != 0 && limit < ARRIVE_WAIT_MS ? ARRIVE_WAIT_MS : limit)) {
        return 1;
    }
    if (B.invited != 0) {
        ssLog("battle %u: the invitation's answers %s after %llu ms, %u frames (the game went on meanwhile)", B.id,
              waiting == 0 ? "all in" : "waited for", ssNetMs() - B.since, B.inviteFrames);
    }
    came = B.readyMask & B.invited;
    if (came != B.invited) {
        /* (those who said no, or too late: not waiting any more) */
        Small c;
        memset(&c, 0, sizeof(c));
        c.kind = B_CANCEL;
        c.id = B.id;
        sendTo(B.invited & ~came, &c, sizeof(c));
    }
    if (came == 0 && !B.versus && ssNetConnected()) {
        /* (nobody came: player 1's alone, but shared, for whoever comes in later) */
        if (B.invited != 0) {
            ssLog("battle %u: nobody can come: player 1 alone, shared for whoever comes in", B.id);
        }
    } else if (came == 0) {
        if (countBits(B.invited) == 1) {
            ssLog("battle %u: %s: fighting alone", B.id,
                  B.declinedMask != 0 ? "the other player can't come" : "no answer");
        } else {
            ssLog("battle %u: nobody can come (%d said no, the rest no answer): fighting alone", B.id,
                  countBits(B.declinedMask));
        }
        B.state = ST_NONE;
        if (B.versus) {
            /* (a versus fight whose only invitee said no after its yes: taken back, as in endBattle, not fought
               alone with both sides on this screen) */
            B.versus = 0;
            B.versusHeld = 0;
#ifdef TTYD_BACKEND_AURORA
            {
                const int how = portVersusAbort();
                if (how != 0) {
                    ssLog("battle %u: the versus fight isn't fought here alone: %s", B.id,
                          how == 1 ? "its start taken back" : "this game's, under way, ended (no winner) and back");
                }
            }
#endif
        }
        return 0;
    }
    if (came != B.invited) {
        ssLog("battle %u: %d of %d invited came (the others said no or didn't answer)", B.id, countBits(came),
              countBits(B.invited));
    }
    {
        Go g;
        int taken = 0;
        memset(&g, 0, sizeof(g));
        g.kind = B_GO;
        g.id = B.id;
        g.clock = portLockstepClock();
        g.rand = portRandState();
        g.delay = 0;
        n = 0;
        g.slotPlayer[n++] = (int8_t)ssNetMe();
        g.watchers = (uint8_t)(came & B.watchInvited);
        {
            /* the places by co-op seat (playing together: seat 2 the partner, 3 and 4 the companions),
               then the others by number */
            unsigned left = came & ~B.watchInvited;
            int seat;
            for (seat = 2; seat <= 4 && n < SLOTS; seat++) {
                const int q = portStateSyncSeatPlayer(seat);
                if (q >= 2 && (left & bitOf(q))) {
                    g.slotPlayer[n++] = (int8_t)q;
                    left &= ~bitOf(q);
                }
            }
            for (p = 1; p <= SS_PLAYERS && n < SLOTS; p++) {
                if (left & bitOf(p)) {
                    g.slotPlayer[n++] = (int8_t)p;
                }
            }
        }
        g.parts = (uint8_t)n;
        {
            /* playing together: each place plays its player's seat's character (a guest in seat 1 Mario,
               the host then the partner or a companion); the usual when the seats don't make one each */
            int role[SLOTS], used = 0, ok = 1, i, r;
            for (i = 0; i < SLOTS; i++) {
                role[i] = i;
            }
            for (i = 0; i < n; i++) {
                const int seat = portStateSyncSeatOf(g.slotPlayer[i]);
                r = seat >= 1 && seat <= SLOTS ? seat - 1 : i;
                if (r >= n || (used & (1 << r))) {
                    ok = 0;
                    break;
                }
                used |= 1 << r;
                role[r] = i; /* (character r is place i's) */
            }
            g.roles = 0;
            if (ok && role[0] != 0) {
                for (r = 0; r < SLOTS; r++) {
                    g.roles |= (uint8_t)((role[r] & 3) << (2 * r));
                }
                ssLog("battle %u: Mario is place %d's (seat 1's player), the partner place %d's", B.id, role[0],
                      role[1]);
            }
        }
        /* players 3 and 4's companions: the partner each has out, if this party has it, else another */
        for (s = 2; s < n; s++) {
            g.companion[s - 2] = portCoopBattleCompanion(B.partnerOf[g.slotPlayer[s]], marioGetParty(), taken);
            taken = g.companion[s - 2];
        }
        sendTo(came, &g, sizeof(g));
        B.goMsg = g;
        takePlaces(&g);
        beginLoading(&g);
    }
    return 0;
}

/* ---- B ---- */

static void decline(int to, const Invite* v, const char* why) {
    Small s;
    memset(&s, 0, sizeof(s));
    s.kind = B_DECLINE;
    s.id = v->id;
    sendTo(bitOf(to), &s, sizeof(s));
    ssLog("battle %u: invited to fight %s, but %s", v->id, v->npc, why);
}

/* fbat's mode is the field's own (1) for a battle to share; a script's battle (route 1) also in a room
   that never set up its NPCs (mode 0: evt_npc_setup sets 1), as the raid boss's room and other rooms
   made here start their fights */
static int fieldMode(int mode, int route) {
    return mode == 1 || (route == 1 && mode == 0);
}

/* an invitation's ids and levels: what's wrong, NULL nothing */
static const char* inviteBad(const Invite* v) {
    const char* why;
    if (v->partner < PARTNER_NONE || v->partner > PARTNER_MSMOWZ) {
        return "the partner";
    }
    if (v->playAs < 0 || v->playAs >= PORT_PLAY_AS_COUNT) {
        return "the Play as pick";
    }
    if ((why = pouchBad(&v->pouch)) != NULL) {
        return why;
    }
    return wireBattleBad(&v->battle);
}

static void takeInvite(int from, const Invite* v) {
    FieldBattleData* fb;
    afterFrame(); /* (the last battle's field part first: its own partner is the one kept for after this one) */
    fb = fbatGetPointer();
    NpcEntry* npc = NULL;
    const char* why = NULL;
    const int coop = portStateSyncCoopGuest() && from == 1; /* (playing together: always the host's battles) */
    const int again = B.pendFrom == from && B.pend.id == v->id; /* (an invitation kept: tried again) */
    /* (a raid's guest: the host's fight is this game's too, raidShared) */
    const int keep = coop || (raidShared() && from == 1 && !ssNetIsHost());
    if ((why = inviteBad(v)) != NULL) {
        ssLog("battle %u: player %d's invitation dropped: %s out of the game's range", v->id, from, why);
        decline(from, v, "its invitation isn't one this game can take");
        return;
    }
    if (coop && B.state == ST_NONE && (B.pulled || B.pullWant || B.warpWait) && from == B.pullFrom) {
        B.warpWait = 0;
        sceneDone(B.pull.id); /* (the host's scene goes on into its battle: this game with it) */
    }
    if (B.state != ST_NONE || B.pulled) {
        why = B.state != ST_NONE ? "busy with another" : "held in another player's scene";
        if (!again) {
            ssLog("battle %u: (this game: state %d, held %d, co-op %d, the scene of player %d)", v->id, B.state,
                  B.pulled, coop, B.pullFrom);
        }
    } else if (gp->inBattle || seqGetSeq() != SEQ_GAME || seqCheckSeq()) {
        why = "not in the field";
    } else if (strncmp(v->map, gp->currentMapName, 16) != 0) {
        why = "in another room";
    } else if ((v->route == 0 && !marioChkKey() && !portStateSyncWatcher() && !coop) || winCheck() || fb == NULL ||
               !fieldMode(fb->mode, v->route)) { /* (a watcher's keys are off: it's watching) */
        why = "busy (an event, a menu)";
    } else if ((npc = npcNameToPtr_NoAssert(v->npc)) == NULL || (npc->jumpFlags & NPC_STATUS_DEAD) ||
               !(npc->flags & NPC_FLAG_ACTIVE)) {
        why = "that enemy isn't here";
    } else {
        /* (the battle as it would come here, tried on a copy: no stage or no group, no battle) */
        NpcBattleInfo test = npc->battleInfo;
        if (!unwireBattle(&test, &v->battle)) {
            why = test.stageData == NULL ? "its battle has no stage here" : "its battle has no group here";
        }
    }
    if (why != NULL) {
        /* playing together, the host's battle is this player's too: one that can't come this moment (still
           coming into the room, a text box, its world a step behind the host's) keeps the invitation and comes
           as soon as it can, while the host still waits for it (ARRIVE_WAIT_MS) */
        if (keep && v->kind == B_INVITE && B.state == ST_NONE &&
            (!again || ssNetMs() - B.pendAt < ARRIVE_WAIT_MS - 1000)) {
            if (!again) {
                B.pend = *v;
                B.pendFrom = from;
                B.pendAt = ssNetMs();
                ssLog("battle %u: invited to fight %s, but %s: playing together, kept until this game can come",
                      v->id, v->npc, why);
            }
            return;
        }
        B.pendFrom = 0;
        decline(from, v, why);
        return;
    }
    if (again) {
        ssLog("battle %u: the kept invitation taken after %llu ms", v->id, ssNetMs() - B.pendAt);
    }
    B.pendFrom = 0;
    B.id = v->id;
    B.invite = *v;
    B.iAmA = 0;
    B.aPlayer = from;
    B.parts = 0;
    B.ownPartner = marioGetParty();
    B.ownPouch = *pouchGetPtr();
    portPm64WornGet(B.ownPm64);
    portPm64WornSet(v->pm64);
    portBrothersGetStats(&B.ownBrosHp, &B.ownBrosFp);
    portBrothersSetStats(v->brosHp, v->brosFp);
    B.haveOwn = 1;
    *pouchGetPtr() = v->pouch;
    if (v->partner != marioGetParty()) {
        marioPartyKill();
        if (v->partner > 0) {
            marioPartyEntry((PartyId)v->partner);
        }
        ssLog("battle %u: partner %d out, as the other player's", v->id, v->partner);
    }
    {
        /* the enemy's battle, A's */
        NpcBattleInfo* bi = &npc->battleInfo;
        int k;
        unwireBattle(bi, &v->battle);
        ssLog("battle %u: layout: table place %d is the group at code%+lld here", v->id, (int)v->battle.group,
              bi->groupSetup != NULL ? (long long)((intptr_t)bi->groupSetup - (intptr_t)&portStateSyncSharedBattle)
                                     : 0LL);
        if (sWireMissing > 0) {
            ssLog("battle %u: %u of the enemy's battle data not found here: this game's own taken there", v->id,
                  sWireMissing);
        }
        for (k = 0; k < 4; k++) {
            if (npc->slaves[k] != NULL) {
                npc->slaves[k]->battleInfo = *bi;
            }
        }
    }
    if (v->route == 1) {
        /* a script's battle: what evt_npc_battle_start does (evt_npc.c); its waiting script's part
           after the battle (marioCtrlOn, marioFBattlePost) is endBattle's here */
        fb->target = npc;
        sScriptTaken.taking = npc;
        marioFBattlePrepare();
        marioCtrlOff();
        if (partyGetPtr(marioGetPartyId()) != NULL) {
            partyCtrlOff();
        }
        BattleInformationSetResult(&fb->info, 0);
        seqSetSeq(SEQ_BATTLE, NULL, NULL);
        B.state = ST_TRIGGERED;
        B.go = 0;
        B.since = ssNetMs();
        ssLog("battle %u: joining the other player's script battle with %s", v->id, v->npc);
        return;
    }
    /* the game's own start of a battle with an enemy met (npcdrv.c fbat_setHit) */
    fb->mode = 5;
    fb->subState = 0;
    fb->npc = npc;
    fb->target = npc;
    fb->hitKind = v->hitKind;
    fb->firstAttackParam = v->firstAttackParam;
    fb->firstAttackFlags = v->firstAttackFlags;
    npc->fbatHitKind = (u32)v->hitKind;
    B.state = ST_TRIGGERED;
    B.go = 0;
    B.since = ssNetMs();
    ssLog("battle %u: joining the other player's battle with %s", v->id, v->npc);
}

/* B at its battle's start (its battle sequence begun): ready, waiting for A's start */
static void readyNow(void) {
    uint8_t d[16];
    if (B.state != ST_TRIGGERED) {
        return;
    }
    memset(d, 0, sizeof(d));
    d[0] = (uint8_t)(B.ownPartner > 0 && B.ownPartner < 256 ? B.ownPartner : 0); /* (for a companion's) */
    if (!B.late) {
        sendSmall(bitOf(B.aPlayer), B_READY, 0, d, 0); /* (one joining under way has its start: LATE_GO) */
    }
    B.state = ST_READY_WAIT;
    B.since = ssNetMs();
}

/* B, once a frame while its battle sequence opens (held at the gate): A's start taken once it comes. 1 while
   it still waits. Never a wait inside one frame (as A's, inviteFrame); and as long as A may wait for the
   others (a co-op guest still arriving, a versus fight's fade), so a yes given early isn't given up on while
   A still waits for someone else */
static int goFrame(void) {
    const unsigned long long limit = (B.versus ? VERSUS_WAIT_MS : ARRIVE_WAIT_MS) + 2000;
    if (B.state != ST_READY_WAIT || B.iAmA) {
        return 0;
    }
    if (!B.go && reach(B.aPlayer) && ssNetMs() - B.since < limit) {
        return 1;
    }
    if (!B.go) {
        Small d;
        ssLog("battle %u: the start never came: this battle alone", B.id);
        memset(&d, 0, sizeof(d));
        d.kind = B_DECLINE; /* (A, still waiting or at the gate, drops this game at once: not 30 s at its gate) */
        d.id = B.id;
        sendTo(bitOf(B.aPlayer), &d, sizeof(d));
        endBattle("no start");
        return 0;
    }
    if (B.mySlot < 0 && !B.watching) {
        ssLog("battle %u: not among the battle's players: this battle alone", B.id);
        endBattle("not taken");
        return 0;
    }
    ssLog("battle %u: player 1's start came after %llu ms", B.id, ssNetMs() - B.since);
    beginLoading(&B.goMsg);
    return 0;
}

/* the gate's patience with that player's game: out of reach, or 8 s gone by, as before; but a game that
   is still loading the battle (its player message says the battle sequence) on a busy computer gets up to
   30 s: giving up on it too early ended the sharing for a game that was only slow, and A then fought alone
   (no "back in the field": sync_soak coopbattle under a full machine) */
#define GATE_MS 8000
#define GATE_LOADING_MS 30000
static int gateLate(int player) {
    const unsigned long long t = ssNetMs() - B.since;
    const SsPeer* o = ssPeerOf(player);
    if (!reach(player) || t > GATE_LOADING_MS) {
        return 1;
    }
    return t > GATE_MS && !(o->valid && (o->inBattle || o->seq == SEQ_BATTLE));
}

/* seq_battle.c, before battle_init (which builds the units and takes random numbers): 1 while this
   game waits for the others to get there (A: everyone; the others: A); then all take A's clock and
   random numbers and the battle plays in lockstep from here */
int portStateSyncBattleGate(void) {
    if (B.state == ST_INVITED_WAIT || B.state == ST_TRIGGERED || B.state == ST_READY_WAIT) {
        /* (the invitation's answers, A's start: waited for here, a frame at a time, the field drawn) */
        ssNetPump(0xFFFFFFFFu);
        readyNow();
        if (inviteFrame() || goFrame()) {
            return 1;
        }
    }
    if (B.state != ST_LOADING) {
        return 0;
    }
    if (!B.gateSent) {
        Go g = B.goMsg; /* (the places with it) */
        g.kind = B_GATE;
        g.id = B.id;
        g.clock = portLockstepClock();
        g.rand = portRandState();
        g.delay = 0;
        sendTo(B.iAmA ? othersMask() : bitOf(B.slotPlayer[0]), &g, sizeof(g));
        B.goMsg = g;
        B.gateSent = 1;
        B.since = ssNetMs();
    }
    ssNetPump(0xFFFFFFFFu);
    if (B.state != ST_LOADING) {
        return 0;
    }
    if (B.iAmA) {
        unsigned need = 0;
        int s;
        for (s = 1; s < B.parts; s++) {
            if (!(B.leftMask & (1u << s))) {
                need |= 1u << s;
            }
        }
        if ((B.gateMask & need) != need) {
            int late = 0;
            for (s = 1; s < B.parts && s < SLOTS && !late; s++) { /* (parts <= SLOTS: for the analyzer) */
                if ((need & ~B.gateMask & (1u << s)) && gateLate(B.slotPlayer[s])) {
                    late = 1;
                }
            }
            if (!late) {
                return 1;
            }
            /* those not there: out of the battle (their controller nothing from its start) */
            for (s = 1; s < B.parts && B.state == ST_LOADING; s++) {
                if (need & ~B.gateMask & (1u << s)) {
                    if (B.parts <= 2) {
                        ssLog("battle %u: the other game never reached the battle's start: this battle alone", B.id);
                    } else {
                        ssLog("battle %u: player %d's game never reached the battle's start: out of it", B.id, s + 1);
                    }
                    sendLeave(othersMask(), s);
                    gone(s);
                }
            }
            if (B.state != ST_LOADING) {
                return 0;
            }
        }
    } else if (!B.gateGot) {
        if (gateLate(B.slotPlayer[0])) {
            ssLog("battle %u: the other game never reached the battle's start: this battle alone", B.id);
            leaveShared("no gate");
        }
        return B.state == ST_LOADING;
    }
    beginShared(B.iAmA ? &B.goMsg : &B.gateTheirs);
    return 0;
}

/* ---- joining a battle under way ----
 * A game free in the room of another's shared battle (A's) asks to join it. A answers with the
 * battle's invitation as it was and its start at the gate, and takes the game on as a watcher:
 * its game starts the same battle from the same start and plays it from the players' controllers,
 * the ones it missed from A's log (asked as it goes), faster than real time until it has caught
 * up (portStateSyncCatchUp). Without a save state there's no other way in: a battle is its start
 * and its controllers. */

uint32_t ssBattleSharedId(void) {
    return B.iAmA && B.state == ST_SHARED && !B.versus ? B.id : 0;
}

int portStateSyncCatchUp(void) {
    /* a watcher joined under way, or a follower whose link dropped (A played its units meanwhile): A's
       steps here and this game's well behind them. (Not only watchers: a player back after a drop never
       caught up before the battle ended, so A went on playing their units to the end.) */
    return (B.watching || !B.iAmA) && B.state == ST_SHARED && B.topStep > B.step + 4 ? 8 : 1;
}

static void lateFrame(void) {
    int p;
    /* (a watcher's keys are off: it watches; a co-op guest's always are: it follows the host's world, and
       comes into the host's battle under way if it missed its start, arriving after it) */
    if (B.pulled || B.pullWant || B.warpWait || B.sceneOn || B.pendFrom != 0 || seqGetSeq() != SEQ_GAME ||
        seqCheckSeq() || gp->inBattle || winCheck() ||
        (!marioChkKey() && !portStateSyncWatcher() && !portStateSyncCoopGuest())) {
        return;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        const SsPeer* o;
        Small m;
        if (p == ssNetMe() || !reach(p)) {
            continue;
        }
        o = ssPeerOf(p);
        if (!o->valid || !o->inBattle || o->battleId == 0 || o->battleId == B.endedId ||
            strncmp(o->map, gp->currentMapName, 16) != 0) {
            continue;
        }
        /* (six times, 3 s apart: A may still be at its start's gate, its answers and the others' loading, when
           a game that couldn't come asks) */
        if (o->battleId == B.lateAsked && (B.lateAsks >= 6 || ssNetMs() - B.lateAskedAt < 3000)) {
            continue;
        }
        if (o->battleId != B.lateAsked) {
            B.lateAsked = o->battleId;
            B.lateAsks = 0;
        }
        B.lateAsks++;
        B.lateAskedAt = ssNetMs();
        memset(&m, 0, sizeof(m));
        m.kind = B_LATE;
        m.id = o->battleId;
        sendTo(bitOf(p), &m, sizeof(m));
        ssLog("battle %u: player %d's battle is under way here: asked to join it", o->battleId, p);
        return;
    }
}

/* A: a game asks to join the battle under way */
static void sendLog(int to);
static int logFull(void);

static void takeLate(int from, uint32_t id) {
    static Invite v;
    LateGo lg;
    Small w;
    
    if (!B.iAmA || B.state != ST_SHARED || id != B.id || B.versus || slotOf(from) >= 0 ||
        (B.watchers & bitOf(from)) || logFull()) { /* (the log full: what it missed isn't all here) */
        ssLog("battle %u: player %d asked to join it: not now", id, from);
        return;
    }
    v = B.invite;
    v.kind = B_LATE_INVITE;
    sendTo(bitOf(from), &v, sizeof(v));
    memset(&lg, 0, sizeof(lg));
    lg.g = B.goMsg; /* (at the gate: its clock and random numbers) */
    lg.g.kind = B_LATE_GO;
    lg.g.id = B.id;
    lg.g.watchers = (uint8_t)(B.watchers | bitOf(from));
    lg.left = B.leftMask;
    lg.frame = B.frame;
    sendTo(bitOf(from), &lg, sizeof(lg));
    sendLog(from);
    B.watchers |= bitOf(from);
    memset(&w, 0, sizeof(w));
    w.kind = B_WATCHERS;
    w.id = B.id;
    w.data[0] = (uint8_t)B.watchers;
    sendTo(othersMask() & ~bitOf(from), &w, sizeof(w));
    ssLog("battle %u: player %d joins it under way at frame %u, as a watcher", B.id, from, B.frame);
}

/* ---- scenes (layer 4) ---- */

/* ---- the scene's text boxes ---- */

static const char kSceneTextTag[] = "statesync_scene";

/* evt_msg.c, a text box opened by a script (A: in a scene the other player was pulled into, sent
   to them: the message's name, or its text when it has no name; the NPC speaking). One opened in
   the frames before its scene counts as begun (a partner's field ability, Goombella's Tattle, speaks
   on its first frames) is kept and sent as the scene begins, while it's still open. */
static SceneText sSceneTextOut;

static void sceneTextSend(int window) {
    sSceneTextOut.id = B.sceneId;
    ssBattleSendTo(B.sceneMask, SS_R_SCENE, &sSceneTextOut, offsetof(SceneText, text) + sSceneTextOut.len);
    B.textWin = window + 1;
}

void portStateSyncSceneText(const char* tag, const char* text, const char* speaker, int window) {
    SceneText* const m = &sSceneTextOut;
    if (B.pulled) {
        return;
    }
    if (!B.sceneOn && (!ssNetConnected() || portStateSyncCoopGuest() || portStateSyncWatcher())) {
        return; /* (a game that never begins a scene: nothing to keep) */
    }
    if (B.sceneOn && B.sceneMask == 0) {
        return;
    }
    memset(m, 0, offsetof(SceneText, text));
    m->kind = S_TEXT;
    m->direct = tag == NULL || strcmp(tag, "DIRECT") == 0;
    snprintf(m->speaker, sizeof(m->speaker), "%s", speaker != NULL ? speaker : "");
    snprintf(m->text, sizeof(m->text), "%s", m->direct ? (text != NULL ? text : "") : tag);
    m->len = (uint16_t)(strlen(m->text) + 1);
    if (!B.sceneOn) {
        B.textEarly = window + 1; /* (sent if its scene begins while it's open) */
        B.textEarlyAt = gp != NULL ? (unsigned)gp->retraceCount : 0u;
        return;
    }
    B.textEarly = 0;
    sceneTextSend(window);
}

/* B: A's text box, here too (the same message from this game's own text, the same speaker in this
   room); it closes when A's does, or as this player reads on */
static SceneText sTextPend; /* B: one come before this game was held (B.textPend) */

static void sceneText(const SceneText* m) {
    static char sText[SCENE_TEXT_MAX];
    const char* text;
    s32 id;
    WindowEntry* win;
    NpcEntry* npc;
    if (!B.pulled || seqGetSeq() != SEQ_GAME) {
        return;
    }
    if (B.textOpen) {
        msgWindow_ForceClose(kSceneTextTag);
        B.textOpen = 0;
    }
    if (m->direct) {
        snprintf(sText, sizeof(sText), "%s", m->text);
        text = sText;
    } else {
        text = msgSearch(m->text);
    }
    if (text == NULL) {
        return;
    }
    id = msgWindow_Entry(text, 0, 0);
    win = windowGetPointer(id);
    if (win == NULL || win->userdata == NULL) {
        return;
    }
    ((MsgWinData*)win->userdata->address)->tag = kSceneTextTag;
    npc = m->speaker[0] != '\0' ? npcNameToPtr_NoAssert(m->speaker) : NULL;
    if (npc != NULL) {
        win->speakerPos = npc->position;
        win->speakerPos.y += npc->height;
        snprintf(((MsgWinData*)win->userdata->address)->speakerName,
                 sizeof(((MsgWinData*)win->userdata->address)->speakerName), "%s", npc->description);
        win->tailMode = 2;
    } else if (strcmp(m->speaker, "party") == 0 || strcmp(m->speaker, "extparty") == 0 ||
               strcmp(m->speaker, "mario") == 0) {
        /* the partner's own line (a Tattle) or Mario's: from this game's partner or Mario, as evt_msg.c
           points it (playing together, a guest's partner is its own character) */
        const PartyEntry* p = m->speaker[0] == 'm' ? NULL
                              : partyGetPtr(m->speaker[0] == 'e' ? marioGetExtraPartyId() : marioGetPartyId());
        if (p != NULL) {
            win->speakerPos = p->position;
            win->speakerPos.y += p->height;
        } else {
            win->speakerPos = marioGetPtr()->position;
            win->speakerPos.y += marioGetPtr()->wPlayerCollisionBox.y;
        }
        snprintf(((MsgWinData*)win->userdata->address)->speakerName,
                 sizeof(((MsgWinData*)win->userdata->address)->speakerName), "%s", m->speaker);
        win->tailMode = 2;
    }
    B.textOpen = 1;
    B.textPend = 0;
    ssLog("scene %u: the other player's text box here too (%s%s%s)", m->id, m->direct ? "its text" : m->text,
          m->speaker[0] != '\0' ? ", said by " : "", m->speaker);
}

static int inScene(void) {
    FieldBattleData* fb = fbatGetPointer();
    /* (a talk is fbat mode 2 or 3, a battle 5; playing together, a talk is a scene too: the guests see its
       text boxes) */
    return seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && !marioChkKey() && !winCheck() &&
           fb != NULL && fb->mode != 5 && ((fb->mode != 2 && fb->mode != 3) || portStateSyncCoopHost());
}

/* A: every other player (not a watcher) free in the field in another room: they're pulled into this
   game's scene, up to three (a player already in this room isn't held for it) */
static unsigned scenePullable(void) {
    unsigned m = 0;
    int p;
    for (p = 1; p <= SS_PLAYERS; p++) {
        const SsPeer* o = ssPeerOf(p);
        if (p != ssNetMe() && o->valid && !o->watch && o->seq == SEQ_GAME &&
            (strncmp(o->map, gp->currentMapName, 16) != 0 || o->coop)) { /* (co-op guests: here too) */
            m |= bitOf(p);
        }
    }
    return m;
}

static void sceneFrame(void) {
    MarioWork* mario = marioGetPtr();
    if (!ssNetConnected() || mario == NULL) {
        return;
    }
    if (seqGetSeq() == SEQ_GAME && !marioChkKey() && gp->retraceCount % 300 == 0) {
        FieldBattleData* fb = fbatGetPointer();
        ssLog("scene: keys off here (fbat mode %d, menu %d, counted %d)", fb != NULL ? fb->mode : -1, (int)winCheck(),
              B.sceneFrames);
    }
    /* A: a scene starts here (keys off for half a second, not a talk, a battle or a menu); never a game that
       follows the host's (a co-op guest's or a watcher's keys are always off: the host's scenes are theirs) */
    if (!B.pulled && inScene() && !portStateSyncCoopGuest() && !portStateSyncWatcher()) {
        B.sceneFrames++;
    } else if (!B.pulled) {
        if (B.sceneOn) {
            uint8_t m[8] = {S_DONE};
            memcpy(m + 4, &B.sceneId, 4);
            ssBattleSendTo(B.sceneMask, SS_R_SCENE, m, sizeof(m));
            ssLog("scene %u: over here (%d frames)", B.sceneId, B.sceneFrames);
            B.sceneOn = 0;
            B.sceneMask = 0;
        }
        B.sceneFrames = 0;
    }
    if (B.sceneFrames >= (portStateSyncCoopHost() ? 2 : 30) && !B.sceneOn) {
        const unsigned to = scenePullable();
        if (to != 0) {
            Pull m;
            memset(&m, 0, sizeof(m));
            m.kind = S_PULL;
            m.id = B.sceneId = ++B.nextId * 2 + (ssNetIsHost() ? 0 : 1);
            snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
            m.x = mario->position.x;
            m.y = mario->position.y;
            m.z = mario->position.z;
            ssBattleSendTo(to, SS_R_SCENE, &m, sizeof(m));
            B.sceneOn = 1;
            B.sceneMask = to;
            ssLog("scene %u: a scene begins here in %s: players %x pulled in", m.id, m.map, to);
            if (B.textEarly > 0 && (unsigned)gp->retraceCount - B.textEarlyAt <= 60u &&
                windowCheckID(B.textEarly - 1)) {
                /* its text box opened in the frames before (a Tattle's): theirs too */
                sceneTextSend(B.textEarly - 1);
                ssLog("scene %u: its text box from before it began sent too (%s)", B.sceneId,
                      sSceneTextOut.direct ? "its text" : sSceneTextOut.text);
            }
        }
    }
    if (B.sceneOn) {
        B.textEarly = 0; /* (sent above, or long closed: none left over for a later scene) */
    }
    /* A: a player free only after the scene began (a co-op guest still on its way into the room: a new
       adventure's first scene starts as the host arrives) is pulled in then: else it never was, its camera
       left on its own Mario, kept at the host's (inside aaa_00's trees) for the whole scene */
    if (B.sceneOn && B.sceneFrames > 0) {
        unsigned late = scenePullable() & ~B.sceneMask;
        int p;
        for (p = 1; p <= SS_PLAYERS; p++) {
            if ((late & bitOf(p)) && !ssPeerOf(p)->coop) {
                late &= ~bitOf(p); /* (a player in their own adventure isn't taken from it halfway) */
            }
        }
        if (late != 0) {
            Pull m;
            memset(&m, 0, sizeof(m));
            m.kind = S_PULL;
            m.id = B.sceneId;
            snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
            m.x = mario->position.x;
            m.y = mario->position.y;
            m.z = mario->position.z;
            ssBattleSendTo(late, SS_R_SCENE, &m, sizeof(m));
            B.sceneMask |= late;
            ssLog("scene %u: players %x free now: pulled in too", m.id, late);
            if (B.textWin > 0 && windowCheckID(B.textWin - 1)) {
                ssBattleSendTo(late, SS_R_SCENE, &sSceneTextOut, offsetof(SceneText, text) + sSceneTextOut.len);
            }
        }
    }
    /* A: the scene's camera and its text box closing, to the player pulled in */
    if (B.sceneOn) {
        if (gp->retraceCount % 3 == 0) {
            const CameraEntry* c = camGetPtr(CAMERA_3D);
            SceneCam m;
            memset(&m, 0, sizeof(m));
            m.kind = S_CAM;
            m.id = B.sceneId;
            if (c != NULL) {
                m.pos[0] = c->cameraPos.x, m.pos[1] = c->cameraPos.y, m.pos[2] = c->cameraPos.z;
                m.at[0] = c->cameraAt.x, m.at[1] = c->cameraAt.y, m.at[2] = c->cameraAt.z;
                m.up[0] = c->cameraUp.x, m.up[1] = c->cameraUp.y, m.up[2] = c->cameraUp.z;
                m.fovY = c->fovY;
                ssBattleSendTo(B.sceneMask, SS_R_SCENE, &m, sizeof(m));
            }
        }
        if (B.textWin > 0 && !windowCheckID(B.textWin - 1)) {
            uint8_t m[8] = {S_TEXT_END};
            memcpy(m + 4, &B.sceneId, 4);
            ssBattleSendTo(B.sceneMask, SS_R_SCENE, m, sizeof(m));
            B.textWin = 0;
        }
    }
    /* B: held: its camera on A's */
    if (B.pulled && B.cam.kind == S_CAM) {
        CameraEntry* c = camGetPtr(CAMERA_3D);
        if (c != NULL) {
            c->flags |= CAM_FLAG_CTRL_OFF; /* (cam3dMain leaves it as set here) */
            c->cameraPos.x = B.cam.pos[0], c->cameraPos.y = B.cam.pos[1], c->cameraPos.z = B.cam.pos[2];
            c->cameraAt.x = B.cam.at[0], c->cameraAt.y = B.cam.at[1], c->cameraAt.z = B.cam.at[2];
            c->cameraUp.x = B.cam.up[0], c->cameraUp.y = B.cam.up[1], c->cameraUp.z = B.cam.up[2];
            if (B.cam.fovY > 1.0f && B.cam.fovY < 120.0f) {
                c->fovY = B.cam.fovY;
            }
            if (!B.camHeld) {
                ssLog("scene %u: the camera follows the other player's", B.pull.id);
            }
            B.camHeld = 1;
        }
    }
    /* B: pulled in, once free */
    if (B.pullWant && !B.pulled && !B.warpWait && seqGetSeq() == SEQ_GAME && !seqCheckSeq() &&
        (marioChkKey() || portStateSyncCoopGuest()) && !winCheck() && !gp->inBattle) {
        B.pulledPouch = *pouchGetPtr();
        if (strncmp(B.pull.map, gp->currentMapName, 16) != 0) {
            ssLog("scene %u: warped to %s for the other player's scene", B.pull.id, B.pull.map);
            seqSetSeq(SEQ_MAP_CHANGE, B.pull.map, NULL);
        }
        B.warpWait = 1;
    }
    if (B.warpWait && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !strncmp(B.pull.map, gp->currentMapName, 16)) {
        B.warpWait = 0;
        B.pullWant = 0;
        mario->position.x = B.pull.x + 30.0f;
        mario->position.y = B.pull.y;
        mario->position.z = B.pull.z + 20.0f;
        marioKeyOff();
        B.pulled = 1;
        B.noticeAt = gp->retraceCount;
        if (!portStateSyncCoopGuest()) {
            portCoopNotice(kWatchingNotice); /* (playing together: the host's scene is everyone's) */
        }
        ssLog("scene %u: here in %s beside the other player, held while their scene plays", B.pull.id, B.pull.map);
        if (B.textPend) {
            /* (its text box came with the pull, before this game was held: a Tattle's, said at once) */
            B.textPend = 0;
            sceneText(&sTextPend);
        }
    }
    /* (said again now and then while it lasts: a scene can be long) */
    if (B.pulled && gp->retraceCount - B.noticeAt >= 300 && !portStateSyncCoopGuest()) {
        B.noticeAt = gp->retraceCount;
        portCoopNotice(kWatchingNotice);
    }
}

static void sceneLetGo(void) {
    B.textPend = 0;
    if (B.textOpen) {
        msgWindow_ForceClose(kSceneTextTag);
        B.textOpen = 0;
    }
    if (B.camHeld) {
        CameraEntry* c = camGetPtr(CAMERA_3D);
        if (c != NULL) {
            c->flags &= (u16)~CAM_FLAG_CTRL_OFF;
        }
        B.camHeld = 0;
    }
    memset(&B.cam, 0, sizeof(B.cam));
}

static void sceneDone(uint32_t id) {
    sceneLetGo();
    B.pullWant = 0;
    if (B.pulled) {
        B.pulled = 0;
        *pouchGetPtr() = B.pulledPouch; /* (what the room's own copy of the scene gave: A's comes across) */
        ssProgressRebase();
        marioKeyOn();
        ssLog("scene %u: over: free again", id);
    } else if (B.warpWait) {
        /* (still on its way: it arrives free, never held for a scene that's over; it was held for the rest
           of the session) */
        B.warpWait = 0;
        ssLog("scene %u: over before this player got there", id);
    }
}

/* ---- versus online (Aurora: port/include/port/versus.h) ----
 * The host starts a versus fight from the Online tab: its versus set-up crosses with the start,
 * and both games begin the fight with it (the host's game as Mario's side, the guest's as Luigi's,
 * Luigi's side a player's), meet at the battle's gate and play it frame for frame as a shared
 * battle: while a side chooses its command only its player's controller is read; while it acts
 * both (the other side guards). Afterwards each game is back in its own field. */

#define VERSUS_TEXT_MAX 8192

typedef struct VersusMsg {
    uint8_t kind;
    uint8_t pad[3];
    uint32_t id;
    int32_t playAs;
    uint32_t len;
    char text[VERSUS_TEXT_MAX];
} VersusMsg;

#ifdef TTYD_BACKEND_AURORA
#include "port/versus.h"
#endif

/* The host's opponent for a versus fight: any player (not a watcher) playing with it, chosen on
   the Online tab with left and right (players 3 and 4 too); 0 none */
static int sVersusFoe;

static int versusCan(int p) {
    const SsPeer* o = ssPeerOf(p);
    return p >= 2 && p <= SS_PLAYERS && p != ssNetMe() && o->valid && !o->watch && ssNetLinked(p);
}

int portStateSyncVersusOpponent(void) {
    int p;
    if (!ssNetIsHost()) {
        return ssNetConnected() ? 1 : 0;
    }
    if (versusCan(sVersusFoe)) {
        return sVersusFoe;
    }
    if (versusCan(ssBattlePeer())) {
        return ssBattlePeer();
    }
    for (p = 2; p <= SS_PLAYERS; p++) {
        if (versusCan(p)) {
            return p;
        }
    }
    return 0;
}

/* (the whole wait before the fight: held for the yes, and from the yes to the GO. Only the held part once, and
   on the yes's frame neither this nor portVersusBusy was on for the Multiplayer page's party watch, which ran
   first: the party ended as its fight began, players 3 and 4 sent along into the ring) */
int portStateSyncVersusPending(void) {
    return B.iAmA && B.versus && B.state == ST_INVITED_WAIT;
}

int portStateSyncVersusChoices(void) {
    int p, n = 0;
    for (p = 2; p <= SS_PLAYERS && ssNetIsHost(); p++) {
        n += versusCan(p);
    }
    return n;
}

void portStateSyncVersusStep(int dir) {
    int p = portStateSyncVersusOpponent(), k;
    if (dir == 0) {
        /* back to the first it can choose (the lowest player number), whoever the battle peer is now */
        for (p = 2; p <= SS_PLAYERS; p++) {
            if (versusCan(p)) {
                sVersusFoe = p;
                return;
            }
        }
        return;
    }
    for (k = 0; k < SS_PLAYERS && p != 0; k++) {
        p += dir < 0 ? -1 : 1;
        if (p < 2) {
            p = SS_PLAYERS;
        } else if (p > SS_PLAYERS) {
            p = 2;
        }
        if (versusCan(p)) {
            sVersusFoe = p;
            return;
        }
    }
}

int portStateSyncVersus(const char** why) {
#ifdef TTYD_BACKEND_AURORA
    static VersusMsg m;
    int len;
    PortVersusSetup* s = portVersusSetup();
    const int foe = portStateSyncVersusOpponent();
    if (!ssNetConnected() || foe == 0) {
        *why = "Not connected to the other player.";
        return 0;
    }
    if (!ssNetIsHost()) {
        *why = "The host starts a versus fight: their set-up is the fight's.";
        return 0;
    }
    if (B.state != ST_NONE || B.pulled) {
        *why = "A shared battle or scene is under way.";
        return 0;
    }
    if (ssPeerOf(foe)->seq != SEQ_GAME || ssPeerOf(foe)->inBattle || ssPeerOf(foe)->keysOff) {
        *why = "The other player is busy: try again once they're free in the field.";
        return 0;
    }
    s->side[1].controller = PORT_VERSUS_LOCAL; /* (Luigi's side is the other player's) */
    /* (only looked at here: the fight starts once the other player says yes, versusHeldFrame. Started at
       once, a guest that said no left this game fighting an empty Luigi side in the ring, its progress
       then not the guest's: pm64x_online_versus) */
    if (!portVersusCanStart(why)) {
        return 0;
    }
    memset(&m, 0, sizeof(m));
    m.kind = B_VERSUS;
    B.id = ++B.nextId * 8 + (uint32_t)ssNetMe();
    m.id = B.id;
    m.playAs = portPlayAs();
    len = portVersusSetupText(m.text, sizeof(m.text));
    m.len = len > 0 ? (uint32_t)len : 0;
    sendTo(bitOf(foe), &m, offsetof(VersusMsg, text) + m.len);
    memset(&B.invite, 0, sizeof(B.invite));
    B.invite.playAs = m.playAs;
    B.iAmA = 1;
    B.versus = 1;
    B.everShared = 0;
    B.haveOwn = 0;
    B.invited = bitOf(foe); /* (versus: two sides, two players: the host and the one it chose) */
    B.watchInvited = 0;
    B.coopInvited = 0;
    B.readyMask = B.declinedMask = 0;
    B.state = ST_INVITED_WAIT;
    B.versusHeld = 1;
    B.since = ssNetMs();
    B.inviteFrames = 0;
    B.battles++;
    ssLog("battle %u: a versus fight with player %d: the set-up sent (%u bytes), waiting for their yes", B.id, foe,
          m.len);
    return 1;
#else
    *why = "Versus fights are in the windowed build only.";
    return 0;
#endif
}

/* this game's keys free for an invitation: as its player message says them (keysOff, which the host's
   portStateSyncVersus looks at). A co-op guest's keys are off while it follows the host's world, idle in
   the field: free all the same unless it's in a menu (it said no to every versus fight: pm64x_online_versus) */
static int freeKeys(void) {
    if (winCheck()) {
        return 0;
    }
    return portStateSyncCoopGuest() || marioChkKey();
}

#ifdef TTYD_BACKEND_AURORA
/* A, its versus fight held for the other player's yes: no fight, back to the field as it was */
static void versusDrop(const char* why, int cancel) {
    if (cancel && B.invited != 0) {
        Small c;
        memset(&c, 0, sizeof(c));
        c.kind = B_CANCEL;
        c.id = B.id;
        sendTo(B.invited, &c, sizeof(c));
    }
    ssLog("battle %u: the versus fight isn't on: %s", B.id, why);
    portCoopNotice(why);
    B.versusHeld = 0;
    B.versus = 0;
    B.invited = B.readyMask = B.declinedMask = 0;
    B.state = ST_NONE;
}

/* A, once a frame while its versus fight waits for the other player's yes: started on it; dropped on a no,
   the player gone, no answer in time or a battle of this game's own starting meanwhile */
static void versusHeldFrame(void) {
    const char* why = NULL;
    int p, foe = 0;
    if (!B.versusHeld) {
        return;
    }
    if (B.state != ST_INVITED_WAIT || !B.iAmA || !B.versus) {
        B.versusHeld = 0;
        return;
    }
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (B.invited & bitOf(p)) {
            foe = p;
        }
    }
    if (foe != 0 && (B.readyMask & bitOf(foe))) {
        if (battleAhead() || !portVersusStart(&why)) {
            versusDrop(why != NULL ? why : "a battle of this game's own began first.", 1);
            return;
        }
        ssLog("battle %u: player %d said yes after %llu ms: the versus fight starts", B.id, foe,
              ssNetMs() - B.since);
        B.versusHeld = 0;
        B.since = ssNetMs(); /* (the start's wait from here: the fight's fade) */
        versusDropArm();
    } else if (foe == 0 || (B.declinedMask & bitOf(foe))) {
        versusDrop("The other player can't fight now: try again once they're free in the field.", 0);
    } else if (!reach(foe)) {
        versusDrop("The other player's game is out of reach.", 1);
    } else if (battleAhead()) {
        versusDrop("A battle began here before the other player's answer.", 1);
    } else if (ssNetMs() - B.since > VERSUS_WAIT_MS) {
        versusDrop("No answer from the other player.", 1);
    }
}
#endif

static void takeVersus(int from, const uint8_t* p, size_t n) {
    static VersusMsg m;
    const char* why = NULL;
    Invite v;
    memset(&m, 0, sizeof(m));
    memcpy(&m, p, n < sizeof(m) ? n : sizeof(m));
    memset(&v, 0, sizeof(v));
    v.id = m.id;
    snprintf(v.npc, sizeof(v.npc), "%s", "a versus fight");
    if (m.playAs < 0 || m.playAs >= PORT_PLAY_AS_COUNT) {
        why = "its Play as pick isn't one of the game's";
    } else if (B.state != ST_NONE || B.pulled) {
        why = "busy with another";
    } else if (gp == NULL || gp->inBattle || seqGetSeq() != SEQ_GAME || seqCheckSeq() || !freeKeys()) {
        why = "not free in the field";
    }
#ifdef TTYD_BACKEND_AURORA
    if (why == NULL) {
        if (m.len > sizeof(m.text)) {
            m.len = sizeof(m.text);
        }
        portVersusSetupForSession(m.text, m.len, 1); /* (the host's, for this fight: not saved here) */
        portVersusSetup()->side[1].controller = PORT_VERSUS_LOCAL;
        if (!portVersusStart(&why)) {
            /* (why set) */
        }
    }
#else
    if (why == NULL) {
        why = "versus fights are in the windowed build only";
    }
#endif
    if (why != NULL) {
        decline(from, &v, why);
        return;
    }
    B.id = m.id;
    memset(&B.invite, 0, sizeof(B.invite));
    B.invite.id = m.id;
    B.invite.playAs = m.playAs;
    B.iAmA = 0;
    B.aPlayer = from;
    B.parts = 0;
    B.ownPartner = 0;
    B.versus = 1;
    B.everShared = 0;
    B.haveOwn = 0;
    B.state = ST_TRIGGERED;
    B.go = 0;
    B.since = ssNetMs();
    {
        /* (the yes at once: the host's fight waits for it, versusHeldFrame; readyNow says it again at the start) */
        uint8_t d[16];
        memset(d, 0, sizeof(d));
        sendSmall(bitOf(from), B_READY, 0, d, 0);
    }
    portCoopNotice("The other player started a versus fight: you're Luigi's side.");
    ssLog("battle %u: the other player's versus fight: joining as Luigi's side", B.id);
}

/* B, held for (or going to) another player's scene: let go when the puller's game is out of reach (5 s) or
   back in its field with its keys on and no scene (3 s): its S_DONE, the only other way out, would never
   come (a puller that crashed or dropped held this player for good, its progress deferred meanwhile) */
static void pullWatch(void) {
    const unsigned long long now = ssNetMs();
    const SsPeer* o;
    if (!(B.pulled || B.pullWant || B.warpWait) || B.pullFrom == 0) {
        B.pullGoneSince = B.pullFreeSince = 0;
        return;
    }
    o = ssPeerOf(B.pullFrom);
    if (reach(B.pullFrom)) {
        B.pullGoneSince = 0;
    } else if (B.pullGoneSince == 0) {
        B.pullGoneSince = now;
    }
    if (o->valid && o->seq == SEQ_GAME && !o->keysOff && !o->inBattle) {
        if (B.pullFreeSince == 0) {
            B.pullFreeSince = now;
        }
    } else {
        B.pullFreeSince = 0;
    }
    if ((B.pullGoneSince != 0 && now - B.pullGoneSince > 5000) ||
        (B.pullFreeSince != 0 && now - B.pullFreeSince > 3000)) {
        ssLog("scene %u: player %d's %s: its scene is over here", B.pull.id, B.pullFrom,
              B.pullGoneSince != 0 ? "game is gone" : "game is free again without saying");
        B.pullGoneSince = B.pullFreeSince = 0;
        B.warpWait = 0;
        sceneDone(B.pull.id);
    }
}

/* ---- the frame (makeKey) ---- */

void ssBattleFrame(unsigned frame, int peerHere) {
    (void)peerHere;
    if (gp == NULL) {
        return;
    }
    afterFrame();
    pullWatch();
    if (sScriptTaken.npc != NULL && strcmp(sScriptTaken.map, gp->currentMapName) != 0) {
        sScriptTaken.npc = NULL; /* (another room: that fight's NPC isn't this one's) */
    }
#ifdef TTYD_BACKEND_AURORA
    versusHeldFrame(); /* (first: a battle of this game's own met meanwhile is then invited as usual below) */
    versusDropTest();
#endif
    if (B.pendFrom != 0 && B.state == ST_NONE) {
        /* a co-op guest's kept invitation: taken once this game can (or declined once too late). Before the
           start's look below: a script's battle taken here starts this frame (seqSetSeq), and a start looked
           for before it was never seen, so the guest never said it was ready and the host went on without
           it (online_coop_battle_arriving: "the battle didn't start here", then stuck) */
        if (!reach(B.pendFrom)) {
            B.pendFrom = 0;
        } else if (!seqCheckSeq()) {
            takeInvite(B.pendFrom, &B.pend);
        }
    }
    /* the battle's start, before seqMain builds it: this frame */
    if (seqGetSeq() == SEQ_GAME && seqCheckSeq() && seqGetNextSeq() == SEQ_BATTLE) {
        FieldBattleData* fb = fbatGetPointer();
        unsigned here;
        if (B.state == ST_TRIGGERED) {
            readyNow();
        } else if (B.state == ST_NONE && !B.pulled && !B.flee && fb != NULL && fb->target != NULL &&
                   (fb->mode == 5 || fieldMode(fb->mode, 1)) && !portStateSyncWatcher() &&
                   ((here = freeHere() | coopComing() | watchersHere()) != 0 || ssNetConnected())) {
            B.battles++;
            invite(here);
        }
    } else if (B.state == ST_TRIGGERED && battleAhead()) {
        readyNow(); /* (its start seen late: ready all the same, the battle held at the gate till it goes) */
    } else if ((B.state == ST_TRIGGERED || (B.state == ST_INVITED_WAIT && B.versus && !B.versusHeld)) &&
               !battleAhead() &&
               ssNetMs() - B.since > (B.versus ? VERSUS_WAIT_MS : 2000)) {
        /* the game didn't start it (the battle skipped here: its dead event, ...) */
        ssLog("battle %u: the battle didn't start here", B.id);
        endBattle("didn't start");
    }
    /* the start's waits, a frame at a time while the battle sequence opens (also held at its gate) */
    if (B.state == ST_INVITED_WAIT && battleAhead()) {
        B.inviteFrames += (unsigned)inviteFrame();
    } else if (B.state == ST_READY_WAIT) {
        (void)goFrame();
    }
    fleeFrame();
    if (B.state == ST_NONE && B.lastId != 0) {
        if (seqGetSeq() == SEQ_GAME_OVER && !B.overSent) {
            /* a shared battle this game went on with alone (a drift, the connection) lost here: the
               other game's game over too */
            sendGameOver(B.lastId);
        } else if (seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle) {
            B.lastId = 0; /* back in the field: that battle is over */
        }
    }
    settingsFrame(frame);
    if (B.state == ST_NONE) {
        sceneFrame();
    }
    if (B.state == ST_NONE) {
        lateFrame();
    }
}

static void gotDecision(int from, const uint8_t* p, size_t n);

void ssBattleRel(int from, int type, const uint8_t* p, size_t n) {
    uint8_t kind;
    if (type == SS_R_SETTINGS) {
        /* (the host's only, at most what a host sends) */
        if (from != 1 || n > SET_TEXT_MAX) {
            ssLog("settings: player %d's (%u bytes) dropped: only the host's are taken", from, (unsigned)n);
            return;
        }
        gotSettings(p, n);
        return;
    }
    if (n < 8) {
        return;
    }
    kind = p[0];
    if (type == SS_R_SCENE) {
        if (kind == S_PULL && n >= sizeof(Pull)) {
            Pull m;
            if ((B.pulled || B.pullWant || B.warpWait) && from != B.pullFrom) {
                ssLog("scene: player %d's scene while held for player %d's: not taken", from, B.pullFrom);
                return;
            }
            memcpy(&m, p, sizeof(Pull));
            if (!nameOk(m.map, sizeof(m.map))) { /* (it becomes the map to load: a file's name) */
                ssLog("scene: player %d's pull dropped: its map isn't a map's name", from);
                return;
            }
            B.pullFrom = from;
            B.pull = m;
            B.pullWant = 1;
            ssLog("scene %u: the other player's scene in %s: pulled in", B.pull.id, B.pull.map);
        } else if (kind == S_DONE) {
            uint32_t id;
            memcpy(&id, p + 4, 4);
            if (from == B.pullFrom && id == B.pull.id) {
                sceneDone(id);
            }
        } else if (kind == S_TEXT && n >= offsetof(SceneText, text)) {
            static SceneText t;
            memset(&t, 0, sizeof(t));
            memcpy(&t, p, n < sizeof(t) ? n : sizeof(t));
            t.speaker[31] = '\0';
            t.text[SCENE_TEXT_MAX - 1] = '\0';
            if (t.id == B.pull.id && from == B.pullFrom) {
                if (B.pulled) {
                    sceneText(&t);
                } else if (B.pullWant || B.warpWait) {
                    /* (sent with the pull, in the same message batch: this game isn't held yet, a frame
                       or a room change away; shown once it is) */
                    sTextPend = t;
                    B.textPend = 1;
                    ssLog("scene %u: the other player's text box kept till this player is there (%s)", t.id,
                          t.direct ? "its text" : t.text);
                }
            }
        } else if (kind == S_TEXT_END) {
            if (B.textPend && from == B.pullFrom) {
                B.textPend = 0; /* (closed there before it was shown here) */
            }
            if (B.textOpen && from == B.pullFrom) {
                msgWindow_ForceClose(kSceneTextTag);
                B.textOpen = 0;
                ssLog("scene: the other player's text box closed: here too");
            }
        } else if (kind == S_CAM && n >= sizeof(SceneCam)) {
            SceneCam c;
            memcpy(&c, p, sizeof(c));
            if (c.id == B.pull.id && B.pulled && from == B.pullFrom) {
                B.cam = c;
            }
        }
        return;
    }
    switch (kind) {
        case B_VERSUS:
            if (n >= offsetof(VersusMsg, text)) {
                takeVersus(from, p, n);
            }
            break;
        case B_INVITE:
            if (n >= sizeof(Invite)) {
                static Invite v;
                memcpy(&v, p, sizeof(v));
                v.map[15] = v.npc[31] = '\0';
                v.pouch.yoshiName[sizeof(v.pouch.yoshiName) - 1] = '\0';
                takeInvite(from, &v);
            }
            break;
        case B_GAMEOVER:
            if (n >= sizeof(Small)) {
                Small s;
                memcpy(&s, p, sizeof(s));
                gotGameOver(s.id);
            }
            break;
        case B_LEAVE:
            if (n >= sizeof(Small) && B.state != ST_NONE && B.parts > 0) {
                Small m;
                memcpy(&m, p, sizeof(m));
                if (m.id == B.id && (slotOf(from) == 0 || from == 1 || m.data[0] == slotOf(from))) {
                    gone(m.data[0]); /* (theirs; A's or the host's word for another) */
                }
            }
            break;
        case B_DECLINE:
        case B_READY:
            if (n >= sizeof(Small) && B.state == ST_INVITED_WAIT && (B.invited & bitOf(from))) {
                Small s;
                memcpy(&s, p, sizeof(s));
                if (s.id != B.id) {
                    break;
                }
                if (kind == B_READY) {
                    B.readyMask |= bitOf(from);
                    B.partnerOf[from] = s.data[0];
                } else {
                    B.declinedMask |= bitOf(from);
                    B.readyMask &= ~bitOf(from); /* (ready, then gave up before the start: not one that came) */
                }
            } else if (kind == B_DECLINE && n >= sizeof(Small) && B.iAmA && B.state == ST_LOADING) {
                /* (one that came gave up before the start reached it: out of the battle now, not at the gate) */
                Small s;
                int at;
                memcpy(&s, p, sizeof(s));
                at = slotOf(from);
                if (s.id == B.id && at > 0 && !(B.gateMask & (1u << at)) && !(B.leftMask & (1u << at))) {
                    ssLog("battle %u: player %d's game gave up on the start: out of it", B.id, at + 1);
                    sendLeave(othersMask() & ~bitOf(from), at);
                    gone(at);
                }
            }
            break;
        case B_CANCEL:
            if (n >= sizeof(Small) && B.pendFrom == from) {
                Small c;
                memcpy(&c, p, sizeof(c));
                if (c.id == B.pend.id) {
                    B.pendFrom = 0; /* (the host went on without this game: it may join under way, lateFrame) */
                    ssLog("battle %u: the host went on without this game: the kept invitation dropped", c.id);
                }
            }
            if (!B.iAmA && from == B.aPlayer && (B.state == ST_TRIGGERED || B.state == ST_READY_WAIT)) {
                ssLog("battle %u: the other player went on alone", B.id);
#ifdef TTYD_BACKEND_AURORA
                if (B.versus) {
                    /* (the host's versus fight never started: neither does this game's, not fought alone. Its own
                       may already be on its way, asked as the set-up came: endBattle takes it back, from the ring
                       or its battle run from as it opens, no winner) */
                    ssLog("battle %u: the host's versus fight isn't on", B.id);
                    portCoopNotice("The other player's versus fight isn't on.");
                }
#endif
                endBattle("cancelled");
            }
            break;
        case B_GATE:
            if (n >= sizeof(Go) && B.state != ST_NONE && B.parts > 0) {
                Go g;
                memcpy(&g, p, sizeof(Go));
                if (g.id != B.id) {
                    break;
                }
                if (B.iAmA) {
                    const int s = slotOf(from);
                    if (s > 0) {
                        B.gateMask |= 1u << s;
                    }
                } else if (from == B.slotPlayer[0]) {
                    B.gateTheirs = g;
                    B.gateGot = 1;
                }
            }
            break;
        case B_LATE:
            if (n >= sizeof(Small)) {
                Small m;
                memcpy(&m, p, sizeof(m));
                takeLate(from, m.id);
            }
            break;
        case B_LATE_INVITE:
            if (n >= sizeof(Invite)) {
                static Invite v;
                memcpy(&v, p, sizeof(v));
                v.map[15] = v.npc[31] = '\0';
                v.pouch.yoshiName[sizeof(v.pouch.yoshiName) - 1] = '\0';
                if (v.id == B.lateAsked) {
                    takeInvite(from, &v);
                    if (B.state == ST_TRIGGERED && B.id == v.id) {
                        B.late = 1;
                    }
                }
            }
            break;
        case B_LATE_GO:
            if (n >= sizeof(LateGo) && B.late && !B.iAmA && from == B.aPlayer &&
                (B.state == ST_TRIGGERED || B.state == ST_READY_WAIT)) {
                LateGo lg;
                memcpy(&lg, p, sizeof(lg));
                if (lg.g.id != B.id) {
                    break;
                }
                lg.g.kind = B_GO;
                lg.g.watchers |= (uint8_t)bitOf(ssNetMe());
                B.goMsg = lg.g;
                takePlaces(&B.goMsg);
                B.leftMask = lg.left;
                B.gateTheirs = lg.g; /* (its gate passed long ago: its start at it) */
                B.gateGot = 1;
                B.go = 1;
                ssLog("battle %u: joining it under way (player 1 at frame %u), as a watcher", B.id, lg.frame);
            }
            break;
        case B_WATCHERS:
            if (n >= sizeof(Small) && sharing() && B.parts > 0 && from == B.slotPlayer[0]) {
                Small m;
                memcpy(&m, p, sizeof(m));
                if (m.id == B.id) {
                    B.watchers |= m.data[0];
                }
            }
            break;
        case B_CROWD:
            if (B.state == ST_SHARED && B.parts > 0) {
                portCrowdSeatsNet(slotOf(from), p, n);
            }
            break;
        case B_STEP:
        case B_EXEC:
        case B_ACK:
        case B_CMD:
        case B_RES:
        case B_TAKE:
        case B_AT:
            gotDecision(from, p, n);
            break;
        case B_GO:
            if (n >= sizeof(Go) && !B.iAmA && from == B.aPlayer &&
                (B.state == ST_TRIGGERED || B.state == ST_READY_WAIT)) {
                Go g;
                memcpy(&g, p, sizeof(Go));
                if (g.id == B.id) {
                    B.goMsg = g;
                    takePlaces(&B.goMsg);
                    B.go = 1;
                }
            }
            break;
    }
}

/* ---- the shared battle: sync what matters ----
 *
 * After the gate every game runs the same battle itself, at its own pace, and nobody's controller
 * crosses. Place 0's game (A, the player whose encounter, script, raid or versus fight it is) is the
 * battle's authority. What crosses is what the battle decides:
 * - A's steps (B_STEP): at each of the battle sequence's step gates (battle_seq.c btlseqMove: a phase
 *   begins, a phase event is over, the next actor is picked, an action is over) A sends its verdict (the
 *   actor; concluded; the phase over), its random numbers, who plays now, and a snapshot of what the
 *   battle keeps (every unit's HP, FP, statuses and their turns, moves; Star Power, the bag's items; the
 *   sides' clear conditions). Every other game waits at the same gate for it, takes A's snapshot where
 *   its own differs (each difference a correction, logged) and goes on with A's verdict, so every game
 *   passes the same gates in the same order;
 * - the random numbers again as an action's script starts (B_EXEC, btlseqAct 0x6000006): those of the
 *   game of the player whose unit acts (A's for the enemies), so a player's own action starts on their
 *   screen the moment they choose it, with no round trip to A and back;
 * - each player's decisions, from the game of the player who controls the unit (B_CMD: the command
 *   menu's outcome; B_RES: an action command's outcome, a guard, a Stylish timing, a dungeon master's
 *   pick), measured on that player's own screen, on its own clock, from its own controller: the menu,
 *   the presses and their judging never wait on another game. Only the outcome crosses. The other games
 *   skip the menu and take it, or hold the script that needs the outcome (EVT_RETURN_BLOCK) until it
 *   comes. Each follower beats to A (B_AT sub 1, every BEAT_MS: its step and the outcomes it has passed);
 *   an outcome its game has passed that hasn't come in RESULT_LATE_MS, or one A waits on while that game
 *   says nothing for RESULT_SILENT_MS, is a miss: A sends the miss as its own (marked, Hdr.pad), every
 *   game takes it, and A's next snapshot puts the player's own game right (resultLate, logged).
 * A game waits only when what it needs hasn't come (another player's turn, their outcome, A's next
 * step): the picture runs, and after half a second "Waiting for <name>..." says who for. A player
 * whose link drops is played by A's player meanwhile (B_TAKE: their units A's, with A's controller,
 * as single-player plays both), and plays again once their game has caught up. A leaving, the others
 * finish the battle alone, as run away (each gets their own pouch back after a shared battle anyway;
 * A's game shares its own outcome as its progress). The design in prose: docs/STATE_SYNC.md, "Layer 3: battles together". */

#define GATE_WAIT_NOTE_MS 500
#define TAKEOVER_MS 5000      /* a player unreachable this long: A plays their units */
#define STALL_MS 20000        /* A waited this long on a player that says nothing: the same */
#define A_GONE_MS 10000       /* A unreachable this long: this game finishes alone */
#define STORE_MAX 1024
#define PEND_MAX 16
#define BEAT_MS 100          /* a follower's beat to A: where its game is (its step, the outcomes passed) */
#define RESULT_LATE_MS 1000  /* an outcome the player's game has passed (its beat says so) not here this long */
#define RESULT_SILENT_MS 1500 /* A waiting on a player's outcome, their game saying nothing this long */

enum { R_AC = 1, R_GUARD, R_STYLE, R_DM, R_DM_LIVE, R_DUO, R_PICK, R_CMD = 8, R_EXEC = 9, R_KINDS = 10 };

/* every message after the gate starts so */
typedef struct Hdr {
    uint8_t kind;
    uint8_t place; /* the battle place it is from (A relays others' to a game joining late) */
    uint8_t sub;   /* B_RES: R_AC...; B_STEP: the gate; B_ACK: what it waits for (R_EXEC, or 0 a step); B_AT: 1 a
                      beat (k: the outcomes passed in the step, a byte each: R_AC, R_GUARD, R_STYLE), 0 catching up */
    uint8_t pad;   /* B_RES from A: 0x80 | the place whose outcome A took as a miss (it came late), else 0 */
    uint32_t id;
    uint32_t step; /* the gates passed when it was made (B_STEP: the gate's own number) */
    uint32_t k;    /* the index of its kind within the step (B_STEP, B_TAKE: the places present) */
} Hdr;

typedef struct SnapUnit {
    int16_t id;
    int8_t alliance, moveState;
    int32_t kind;
    int16_t hp, maxHp, fp, maxFp;
    int8_t moves, maxMoves;
    uint8_t pad[2];
    int32_t statusFlags;
    int32_t heldItem;
    uint8_t status[sizeof(BtlStatusCopy)];
} SnapUnit;

typedef struct Step {
    Hdr h;
    int32_t verdict;
    uint32_t rand;
    uint32_t flags; /* BattleWork.flags: ESCAPED, INTERRUPTED, RESET_TURN */
    uint8_t clear[3];
    uint8_t pouchOn; /* the bag's part (not in versus: its sides swap pouches) */
    int16_t sp, maxSp;
    int16_t items[MAX_HELD_ITEMS];
    uint16_t nUnits;
    uint16_t pad;
    SnapUnit u[BATTLE_UNIT_MAX];
} Step;

typedef struct Exec {
    Hdr h;
    uint32_t rand;
} Exec;

/* a weapon or a script of the game's, as what it is (never its address: see "every game the same",
   above). kind: P_*; ev 1: the weapon's own script (WEVT); a, b: which */
typedef struct WirePtr {
    uint8_t kind, ev;
    int16_t a;
    int32_t b;
} WirePtr;

enum {
    P_NULL,
    P_NAMED, /* a: kNamed's entry */
    P_ITEM,  /* a: the item's id (itemDataTable's battle_params) */
    P_SUPER, /* a: superActionTable's entry */
    P_UNIT,  /* a: the unit's id, b: the tag in its kind's data table (BtlUnit_GetData) */
    P_MENU,  /* a weapon of the command menus: a its place in the menu built last (-1 none), b its print
                (weaponPrint); each game builds the menus again from the same battle and finds it there */
    P_CHOSEN, /* (a script) the chosen weapon's own (the Cmd's weapon) */
    P_LOST = 255, /* none of those: this game's own taken (logged) */
};

/* the battle's own weapons and scripts, named (the list is the source's: the same in every build) */
extern BattleWeapon marioDefaultWeapon_Jump, marioDefaultWeapon_Hammer, marioWeapon_KururinJump;
static const struct {
    const void* p;
    uint8_t weapon;
} kNamed[] = {
    {&marioDefaultWeapon_Jump, 1}, {&marioDefaultWeapon_Hammer, 1}, {&marioWeapon_KururinJump, 1},
    {&marioWeapon_JyabaraJump, 1}, {&marioWeapon_KaitenHammer, 1}, {&marioWeapon_UltraHammer, 1},
    {&marioWeapon_KaitenHammerInvolved, 1}, {&marioWeapon_UltraHammerInvolved, 1},
    {&marioDefaultWeapon_FirstAttackKaitenHammer, 1}, {&marioDefaultWeapon_FirstAttackUltraHammer, 1},
    {&ItemWeaponData_CookingItem, 1}, {&badgeWeapon_2KaiItem, 1}, {&badgeWeapon_3KaiItem, 1},
    {&badgeWeapon_Charge, 1}, {&badgeWeapon_ChargeP, 1}, {&badgeWeapon_ConfuseHammer, 1},
    {&badgeWeapon_DefenceCommand, 1}, {&badgeWeapon_DefenceCommandP, 1}, {&badgeWeapon_DokanHammer, 1},
    {&badgeWeapon_DokanJump, 1}, {&badgeWeapon_FireNaguri, 1}, {&badgeWeapon_FunyafunyaJump, 1},
    {&badgeWeapon_GatsunHammer, 1}, {&badgeWeapon_GatsunJump, 1}, {&badgeWeapon_HammerNageru, 1},
    {&badgeWeapon_IceNaguri, 1}, {&badgeWeapon_JishinHammer, 1}, {&badgeWeapon_MiniminiFumi, 1},
    {&badgeWeapon_NemuraseFumi, 1}, {&badgeWeapon_RenzokuJump, 1}, {&badgeWeapon_SuperCharge, 1},
    {&badgeWeapon_SuperChargeP, 1}, {&badgeWeapon_TatsumakiJump, 1}, {&badgeWeapon_TsuranukiNaguri, 1},
    {&badgeWeapon_TugitugiJump, 1}, {&badgeWeapon_UltraJishinHammer, 1},
    {btldefaultevt_Damage, 0}, {btldefaultevt_Confuse, 0}, {btldefaultevt_Appeal, 0},
    {btldefaultevt_BiribiriMove, 0}, {btldefaultevt_CantMoveZeroGravity, 0}, {btldefaultevt_ChangeParty, 0},
    {btldefaultevt_Defend, 0}, {btldefaultevt_DefendParty, 0}, {btldefaultevt_Dummy, 0},
    {btldefaultevt_Escape, 0}, {btldefaultevt_LectureEnd, 0}, {btldefaultevt_ProtectPartner, 0},
    {btldefaultevt_SuitoruBadgeEffect, 0}, {btldefaultevt_Wait, 0}, {btldefaultevt_avoid_counter_event, 0},
    {marioAttackEvent_MajinaiDefenceUp, 0}, {marioAttackEvent_MajinaiExpUp, 0},
    {btlataudevtPresentItem_Damage, 0}, {btlataudevtPresentItem_Get, 0},
};
#define NAMED_N ((int)(sizeof(kNamed) / sizeof(kNamed[0])))

s32 portBtlcmdWeaponTableType(void);         /* battle_seq_command.c */
s32 portBtlcmdWeaponTable(BattleWork* wp, s32 type);
s32 portBtlcmdCloseMenus(BattleWork* wp); /* battle_seq_command.c: this game's open command menu closed */

/* what a weapon is, the same in every build: its name's and description's text, item, costs, icon, power
   and targets */
static uint32_t weaponPrint(const BattleWeapon* w) {
    uint32_t h = 2166136261u;
    const char* t;
    int i;
    if (w == NULL) {
        return 0;
    }
    for (t = w->name; t != NULL && *t != '\0'; t++) {
        h = (h ^ (uint8_t)*t) * 16777619u;
    }
    h = (h ^ 0xFF) * 16777619u;
    for (t = w->description; t != NULL && *t != '\0'; t++) {
        h = (h ^ (uint8_t)*t) * 16777619u;
    }
    h = (h ^ (uint32_t)w->itemId) * 16777619u;
    h = (h ^ (uint32_t)(uint8_t)w->baseFpCost) * 16777619u;
    h = (h ^ (uint32_t)w->baseSpCost) * 16777619u;
    h = (h ^ (uint32_t)w->icon) * 16777619u;
    for (i = 0; i < 8; i++) {
        h = (h ^ w->hpDamageWork[i]) * 16777619u;
    }
    h = (h ^ w->targetClassFlags) * 16777619u;
    h = (h ^ w->targetPropertyFlags) * 16777619u;
    return h != 0 ? h : 1u;
}

/* this game's weapon with that print among the command menus' (each built again for the battle's active
   unit: the partner's moves, Mario's jumps, hammers, items, Specials), NULL none */
static const BattleWeapon* menuFind(BattleWork* wp, uint32_t print) {
    static const s32 kTypes[] = {6, 0, 1, 2, 4};
    int t, i, n;
    for (t = 0; t < (int)(sizeof(kTypes) / sizeof(kTypes[0])) && wp != NULL; t++) {
        n = (int)portBtlcmdWeaponTable(wp, kTypes[t]);
        for (i = 0; i < n && i < 21; i++) {
            if (weaponPrint(wp->commandMenu.weapon[i].weapon) == print) {
                return wp->commandMenu.weapon[i].weapon;
            }
        }
    }
    return NULL;
}

static int sameAs(const BattleWeapon* w, const void* p, int ev) {
    return w != NULL && (ev ? (const void*)w->attackEvtScript == p : (const void*)w == p);
}

/* p (a weapon, or with ev a script) as what it is */
static WirePtr wirePtr(BattleWork* wp, const void* p, int ev) {
    WirePtr r;
    int i;
    memset(&r, 0, sizeof(r));
    if (p == NULL) {
        return r;
    }
    r.ev = (uint8_t)ev;
    for (i = 0; i < NAMED_N; i++) {
        if (kNamed[i].weapon ? sameAs((const BattleWeapon*)kNamed[i].p, p, ev) : (ev && kNamed[i].p == p)) {
            r.kind = P_NAMED;
            r.a = (int16_t)i;
            return r;
        }
    }
    for (i = 1; i < PORT_ITEM_TABLE_SIZE; i++) {
        if (sameAs(itemDataTable[i].battle_params, p, ev)) {
            r.kind = P_ITEM;
            r.a = (int16_t)i;
            return r;
        }
    }
    for (i = 0; i < 18; i++) {
        if (sameAs(superActionTable[i], p, ev)) {
            r.kind = P_SUPER;
            r.a = (int16_t)i;
            return r;
        }
    }
    for (i = 0; i < BATTLE_UNIT_MAX && wp != NULL; i++) {
        const BattleUnit* u = wp->units[i];
        const BattleDataEntry* e;
        int k;
        if (u == NULL || u->dataTable == NULL) {
            continue;
        }
        for (e = u->dataTable, k = 0; e->tag != 0 && k < 256; e++, k++) {
            if (e->data == p) { /* (the data itself: never read through, it may be anything) */
                r.kind = P_UNIT;
                r.ev = 0;
                r.a = (int16_t)u->unitId;
                r.b = e->tag;
                return r;
            }
        }
    }
    if (ev && wp != NULL && sameAs(wp->weaponTargets.weapon, p, 1)) {
        r.kind = P_CHOSEN;
        return r;
    }
    for (i = 0; i < 21 && wp != NULL; i++) {
        if (sameAs(wp->commandMenu.weapon[i].weapon, p, ev)) {
            r.kind = P_MENU;
            r.a = (int16_t)i;
            r.b = (int32_t)weaponPrint(wp->commandMenu.weapon[i].weapon);
            return r;
        }
    }
    if (!ev) {
        /* (a weapon from elsewhere, a computer player's own pick: found by its print) */
        r.kind = P_MENU;
        r.a = -1;
        r.b = (int32_t)weaponPrint((const BattleWeapon*)p);
        return r;
    }
    r.kind = P_LOST;
    return r;
}

/* this game's own of r (menuN: this game's menu's length, built again, -1: not built: search the menus;
   chosen: the Cmd's weapon here, for P_CHOSEN); *ok 0: not found here */
static const void* unwirePtr(BattleWork* wp, WirePtr r, int menuN, const BattleWeapon* chosen, int* ok) {
    const BattleWeapon* w = NULL;
    *ok = 1;
    switch (r.kind) {
        case P_NULL:
            return NULL;
        case P_NAMED:
            if (r.a < 0 || r.a >= NAMED_N) {
                break;
            }
            if (!kNamed[r.a].weapon) {
                return kNamed[r.a].p;
            }
            w = (const BattleWeapon*)kNamed[r.a].p;
            return r.ev ? (const void*)w->attackEvtScript : (const void*)w;
        case P_ITEM:
            if (r.a <= 0 || r.a >= PORT_ITEM_TABLE_SIZE || (w = itemDataTable[r.a].battle_params) == NULL) {
                break;
            }
            return r.ev ? (const void*)w->attackEvtScript : (const void*)w;
        case P_SUPER:
            if (r.a < 0 || r.a >= 18 || (w = superActionTable[r.a]) == NULL) {
                break;
            }
            return r.ev ? (const void*)w->attackEvtScript : (const void*)w;
        case P_UNIT: {
            BattleUnit* u = wp != NULL ? BattleGetUnitPtr(wp, r.a) : NULL;
            if (u == NULL || !BtlUnit_CheckData(u, r.b)) {
                break;
            }
            return BtlUnit_GetData(u, r.b);
        }
        case P_MENU:
            if (wp == NULL) {
                break;
            }
            if (menuN >= 0) {
                /* (the menu built again: the same place, if it's the same weapon) */
                if (r.a < 0 || r.a >= 21 || r.a >= menuN || (w = wp->commandMenu.weapon[r.a].weapon) == NULL ||
                    weaponPrint(w) != (uint32_t)r.b) {
                    break;
                }
            } else if ((w = menuFind(wp, (uint32_t)r.b)) == NULL) {
                break;
            }
            return r.ev ? (const void*)w->attackEvtScript : (const void*)w;
        case P_CHOSEN:
            if (chosen == NULL) {
                break;
            }
            return chosen->attackEvtScript;
        default:
            break;
    }
    *ok = 0;
    return NULL;
}

/* the targets the menu chose (BattleWorkWeaponTargets, its weapon apart) */
typedef struct WireTarget {
    int16_t unitIdx, partIdx, posX, posY, posZ;
    uint8_t forwardLv;
    int8_t side;
    int32_t x, y, z, score, reserved;
} WireTarget;

typedef struct WireTargets {
    WireTarget t[74];
    int8_t numTargets, currentTarget, belong, direction;
    int8_t indices[74];
    uint8_t pad[2];
    int32_t unitId;
    uint32_t classFlags, propFlags;
} WireTargets;

static void wireTargets(WireTargets* o, const BattleWorkWeaponTargets* t) {
    int i;
    memset(o, 0, sizeof(*o));
    for (i = 0; i < 74; i++) {
        const BattleWorkTarget* s = &t->targets[i];
        WireTarget* d = &o->t[i];
        d->unitIdx = s->unitIdx;
        d->partIdx = s->partIdx;
        d->posX = s->posX;
        d->posY = s->posY;
        d->posZ = s->posZ;
        d->forwardLv = s->forwardLv;
        d->side = s->side;
        d->x = s->x;
        d->y = s->y;
        d->z = s->z;
        d->score = s->score;
        d->reserved = s->reserved_20;
        o->indices[i] = t->targetIndices[i];
    }
    o->numTargets = t->numTargets;
    o->currentTarget = t->currentTarget;
    o->belong = t->belong;
    o->direction = t->direction;
    o->unitId = t->unitId;
    o->classFlags = t->classFlags;
    o->propFlags = t->propFlags;
}

static void unwireTargets(BattleWorkWeaponTargets* t, const WireTargets* o) {
    int i;
    for (i = 0; i < 74; i++) {
        const WireTarget* s = &o->t[i];
        BattleWorkTarget* d = &t->targets[i];
        d->unitIdx = s->unitIdx;
        d->partIdx = s->partIdx;
        d->posX = s->posX;
        d->posY = s->posY;
        d->posZ = s->posZ;
        d->forwardLv = s->forwardLv;
        d->side = s->side;
        d->x = s->x;
        d->y = s->y;
        d->z = s->z;
        d->score = s->score;
        d->reserved_20 = s->reserved;
        t->targetIndices[i] = o->indices[i] >= -1 && o->indices[i] < 74 ? o->indices[i] : -1;
    }
    t->numTargets = o->numTargets >= 0 && o->numTargets <= 74 ? o->numTargets : 0;
    t->currentTarget = o->currentTarget >= -1 && o->currentTarget < 74 ? o->currentTarget : -1;
    t->belong = o->belong;
    t->direction = o->direction;
    t->unitId = o->unitId;
    t->classFlags = o->classFlags;
    t->propFlags = o->propFlags;
}

/* the targets' places as the game reads them: targets[targetIndices[i]] for i below numTargets and at
   currentTarget (-1: an unset place, never read) */
static int targetsOk(const WireTargets* o) {
    int i;
    if (o->numTargets < 0 || o->numTargets > 74 || o->currentTarget < -1 || o->currentTarget >= 74) {
        return 0;
    }
    for (i = 0; i < 74; i++) {
        if (o->indices[i] < -1 || o->indices[i] >= 74 || (i < o->numTargets && o->indices[i] < 0)) {
            return 0;
        }
    }
    return o->currentTarget < 0 || o->indices[o->currentTarget] >= 0;
}

/* a menu cursor (BattleCursor) */
typedef struct WireCursor {
    int32_t absPos, relPos, count;
} WireCursor;

/* the command menu's outcome (BattleCommandInput's, what btlseqAct and the action read after it) */
typedef struct Cmd {
    Hdr h;
    int32_t unitId;
    int32_t attackEventFlags;
    WirePtr attackEvent;
    int8_t moveState;
    uint8_t dipMode, cancelled, attackAudience;
    int32_t actType, prevActType, operationType, partyKind;
    WireCursor cursor[14];
    int32_t menuType; /* the weapon menu built last (each game builds it again), -1 none */
    WirePtr menuWeapon[21]; /* the menu's weapon table (BattleCommand.weapon) */
    int32_t weaponItem[21], weaponIndex[21], weaponEnabled[21];
    uint32_t decided;
    WirePtr weapon;
    WireTargets targets;
    BattleActRecord rec; /* (32 bytes) */
} Cmd;

typedef struct Res {
    Hdr h;
    int32_t v[12];
} Res;

/* what came, kept by (kind, step, k) until the battle is over */
typedef struct Got {
    uint8_t* p;
    uint32_t n;
    int from;
} Got;
static Got* sGot; /* (grows past STORE_MAX when nothing is old enough to go: a late joiner's whole history) */
static int sGotN, sGotCap;

/* A: every decision message of the battle, in order (a game joining it under way plays it from them) */
static Got* sLog;
static int sLogN, sLogCap;
static size_t sLogBytes; /* (their bytes) */
static int sLogLost;     /* one not kept (the cap, no memory): the log isn't the whole battle any more */
#define LOG_MAX 65536 /* (a battle's messages kept at most: a joiner can't come in past it) */
#define LOG_BYTES_MAX (16u << 20) /* (and their bytes at most: a very long battle's never more memory than this) */

static int logFull(void) {
    return sLogN >= LOG_MAX - 1 || sLogLost;
}

static int gotRoom(Got** a, int* cap, int need) {
    Got* g;
    int c;
    if (need <= *cap) {
        return 1;
    }
    if (need > LOG_MAX) {
        return 0;
    }
    c = *cap > 0 ? *cap : STORE_MAX;
    while (c < need) {
        c *= 2;
    }
    g = (Got*)realloc(*a, (size_t)c * sizeof(Got));
    if (g == NULL) {
        return 0;
    }
    *a = g;
    *cap = c;
    return 1;
}

/* a script call waiting for an outcome (its event): the index it took */
static struct {
    void* e;
    int kind;
    uint32_t step;
    uint32_t k;
} sPend[PEND_MAX];

static void storeClear(void) {
    int i;
    for (i = 0; i < sGotN; i++) {
        free(sGot[i].p);
    }
    sGotN = 0;
    for (i = 0; i < sLogN; i++) {
        free(sLog[i].p);
    }
    sLogN = 0;
    sLogBytes = 0;
    sLogLost = 0;
    memset(sPend, 0, sizeof(sPend));
}

static void logKeep(const void* p, size_t n, int from) {
    if (!B.iAmA || sLogLost) {
        return;
    }
    if (n > 0x10000 || sLogBytes + n > LOG_BYTES_MAX || !gotRoom(&sLog, &sLogCap, sLogN + 1)) {
        /* (past the caps, or no memory: from now on a joiner can't come in, the history isn't whole) */
        sLogLost = 1;
        ssLog("battle %u: its log is full (%d messages, %u bytes): no one joins it under way now", B.id, sLogN,
              (unsigned)sLogBytes);
        return;
    }
    sLog[sLogN].p = (uint8_t*)malloc(n);
    if (sLog[sLogN].p == NULL) {
        sLogLost = 1;
        return;
    }
    memcpy(sLog[sLogN].p, p, n);
    sLog[sLogN].n = (uint32_t)n;
    sLog[sLogN].from = from;
    sLogN++;
    sLogBytes += n;
}

/* the message of that kind (B_STEP, B_EXEC, B_CMD, B_RES's sub) at step and k from that place (the
   one who plays it now; a taken-over player's late one isn't it), NULL not here */
static const Got* gotFind(int kind, int sub, uint32_t step, uint32_t k, int place) {
    int i;
    for (i = 0; i < sGotN; i++) {
        const Hdr* h = (const Hdr*)sGot[i].p;
        if (h->kind == kind && (kind != B_RES || h->sub == sub) && h->step == step &&
            (kind == B_STEP || h->k == k) && h->place == place) {
            return &sGot[i];
        }
    }
    return NULL;
}

/* the controller of a place now: its own game while present, else A's (A's player plays it) */
static int controller(int place) {
    if (place <= 0 || place >= B.parts || place >= SLOTS) {
        return 0;
    }
    return (B.present & (1u << place)) ? place : 0;
}

/* the battle place whose player decides for this unit (the same in every game) */
static int unitPlace(const BattleUnit* u) {
    if (u == NULL) {
        return 0;
    }
#ifdef TTYD_BACKEND_AURORA
    if (B.versus) {
        /* versus: Mario's side the host's (place 0), Luigi's side's player units the guest's */
        return u->alliance == 1 && portVersusPlayerUnit(u) ? 1 : 0;
    }
#endif
    if (u->alliance != 0) {
        return 0; /* (the enemies: their AI; a dungeon master's picks are R_DM's) */
    }
    if (portBrothersOn() && portBrothersLuigiUnit(u)) {
        return B.rolePlace[1];
    }
    if (u->currentKind == UNIT_MARIO) {
        return B.rolePlace[0];
    }
    {
        const int r = portSecondPlayerPartnerOwner(u);
        return r >= 0 && r < SLOTS ? B.rolePlace[r] : r;
    }
}

/* this game decides for that place now */
static int mine(int place) {
    return B.mySlot >= 0 && controller(place) == B.mySlot;
}

int portStateSyncBattleRemote(int place) {
    return B.state == ST_SHARED && place >= 0 && place < SLOTS && !mine(place);
}

/* the shared battle's place of a co-op seat's player (the dungeon master's): its player's place among those who
   came, -1 when it isn't in the battle (never another place in its stead: that place's game would wait on a
   pick its player never makes); seat - 1 outside a shared battle */
int portStateSyncSeatPlace(int seat) {
    int q;
    if (!sharing()) {
        return seat - 1;
    }
    q = portStateSyncSeatPlayer(seat);
    if (q <= 0) {
        q = seat; /* (not playing together: the session's player of that number) */
    }
    return slotOf(q);
}

/* the place that plays character r (0 Mario, 1 the partner, 2 and 3 the companions), and the character a place
   plays: themselves outside a shared battle (Go.roles: a guest in seat 1 plays Mario) */
int portStateSyncBattlePlaceOf(int character) {
    return sharing() && character >= 0 && character < SLOTS && character < B.parts ? B.rolePlace[character]
                                                                                   : character;
}

int portStateSyncBattleCharacterOf(int place) {
    int r;
    if (!sharing()) {
        return place;
    }
    for (r = 0; r < B.parts && r < SLOTS; r++) {
        if (B.rolePlace[r] == place) {
            return r;
        }
    }
    return place;
}

/* the others in the battle (and its watchers): every decision goes to all */
static void sendAll(const void* p, size_t n) {
    logKeep(p, n, B.mySlot);
    sendTo(othersMask(), p, n);
}

static void hdr(Hdr* h, int kind, int sub, uint32_t step, uint32_t k) {
    memset(h, 0, sizeof(*h));
    h->kind = (uint8_t)kind;
    h->place = (uint8_t)(B.mySlot >= 0 ? B.mySlot : 0xFF);
    h->sub = (uint8_t)sub;
    h->id = B.id;
    h->step = step;
    h->k = k;
}

/* TTYD_SYNC_BATTLE_LOG=1: every decision and outcome sent and taken, in the log (finding a correction's cause) */
static int verbose(void) {
    static int sV = -1;
    if (sV < 0) {
        const char* e = getenv("TTYD_SYNC_BATTLE_LOG");
        sV = e != NULL && e[0] == '1';
    }
    return sV;
}

/* ---- waiting ---- */

static void waitBegin(int place) {
    const unsigned long long now = ssNetMs();
    if (B.waitSince == 0) {
        B.waitSince = now;
        B.waits++;
        B.waitFrames0 = B.frame;
    }
    B.waitedNow = 1;
    if (now - B.waitSince > GATE_WAIT_NOTE_MS && (B.noteAt == 0 || now - B.noteAt > 5000)) {
        B.noteAt = now;
        waitNotice(B.slotPlayer[place >= 0 && place < SLOTS ? place : 0]);
    }
}

/* the frame's end of the waits (ssBattleApplyPad, the next frame's start) */
static void waitFrameEnd(void) {
    if (B.waitedNow) {
        B.waitFrames++;
    } else if (B.waitSince != 0) {
        const unsigned len = B.frame - B.waitFrames0;
        if (len > B.longest) {
            B.longest = len;
        }
        B.waitSince = 0;
        B.noteAt = 0;
    }
    B.waitedNow = 0;
}

/* a decision taken in sequence: while a game waits for A's, that game's script sends nothing more of its
   own in this step (a choice, an outcome once its command is over here, a guard, a Stylish, a dungeon
   master's pick). A duo move's halves are not: both run at once, each game judging its own half while it
   waits for the other's (duo_pc.c), so a wait on A's half never means A waits on it in vain */
static int inSequence(int what) {
    return what == R_CMD || what == R_AC || what == R_GUARD || what == R_STYLE || what == R_DM || what == R_DM_LIVE ||
           what == R_PICK;
}

/* a follower tells A what it waits for from A (once each): A then knows it won't send more this step. Only
   the next gate (0), an action's start (R_EXEC) and the decisions taken in sequence (inSequence) */
static void tellWaiting(int what, uint32_t k) {
    Hdr h;
    if (B.iAmA || B.mySlot < 0 || (B.toldStep == B.step + 1 && B.toldWhat == what && B.toldK == k)) {
        return;
    }
    if (what != 0 && what != R_EXEC && !inSequence(what)) {
        return;
    }
    B.toldStep = B.step + 1;
    B.toldWhat = what;
    B.toldK = k;
    hdr(&h, B_ACK, what, B.step, k);
    sendTo(bitOf(B.slotPlayer[0]), &h, sizeof(h));
}

/* A: that place's game waits on A for something A hasn't sent yet: it won't send more this step */
static int blockedOnA(int place) {
    const uint32_t s = B.ackStep[place];
    if (s != B.step + 1) {
        return 0;
    }
    if (B.ackWhat[place] == 0) {
        return 1; /* (the next gate: A hasn't passed it) */
    }
    if (B.ackWhat[place] == R_EXEC) {
        return B.ackK[place] >= B.kCount[R_EXEC];
    }
    if (inSequence(B.ackWhat[place]) && B.ackK[place] < 64) {
        /* (never a duo move's half, inSequence: the halves run at once, so both games waiting on each other's
           is no deadlock; A giving up there scored the other player's half a miss before they had pressed)
           a choice or an outcome of A's player's (or of a place A plays) that A hasn't sent: A waits on that
           game while it waits on A, both for ever (their battles went apart: an enemy's attack on another
           target in each). A gives up (its own stands), finishes the step, and the step's word ends the
           other's wait too; the snapshot then makes them one again */
        return !(B.sentK[B.ackWhat[place]] & (1ull << B.ackK[place]));
    }
    return 0;
}

/* A: a decision of that kind at k sent (blockedOnA) */
static void sentNote(int kind, uint32_t k) {
    if (B.iAmA && kind > 0 && kind < 16 && k < 64) {
        B.sentK[kind] |= 1ull << k;
    }
}

/* a wait for another game's decision in this step, given up: A finished the step without it (a
   follower: A's next gate is here), or (A) the one it waits for waits on A */
static int giveUp(int place) {
    if (!B.iAmA) {
        return gotFind(B_STEP, 0, B.step, 0, 0) != NULL;
    }
    if (!blockedOnA(place)) {
        return 0;
    }
    /* (said each time: a mutual wait ended here, not by the stall takeover; the tests look for it) */
    ssLog("battle %u: step %u: player %d waits on this game for %d #%u: given up", B.id, B.step, place + 1,
          B.ackWhat[place], B.ackK[place]);
    return 1;
}

unsigned portStateSyncBattleWaitMs(void) {
    if (B.state == ST_NONE || B.waitSince == 0) {
        return 0;
    }
    return (unsigned)(ssNetMs() - B.waitSince) + 1;
}

/* tests (a hang's report): where this game's shared battle stands, and what it holds of this step */
void portStateSyncBattleDebug(void) {
    int i, s;
    if (B.state == ST_NONE) {
        return;
    }
    printf("[sync] battle %u debug: state %d A %d place %d step %u top %u present %x waitOn %d waiting %d", B.id,
           (int)B.state, B.iAmA, B.mySlot, B.step, B.topStep, B.present, B.waitOn, B.waitSince != 0);
    for (s = 1; s < SLOTS; s++) {
        printf(" | p%d takeAt %u ack %u/%d/%u", s, B.takeAt[s], B.ackStep[s], B.ackWhat[s], B.ackK[s]);
    }
    printf(" | k cmd %u exec %u ac %u guard %u style %u\n", B.kCount[R_CMD], B.kCount[R_EXEC], B.kCount[R_AC],
           B.kCount[R_GUARD], B.kCount[R_STYLE]);
    for (i = 0; i < sGotN; i++) {
        const Hdr* h = (const Hdr*)sGot[i].p;
        if (h->step + 2 >= B.step) {
            printf("[sync]   got kind %d sub %d step %u k %u place %d\n", h->kind, h->sub, h->step, h->k, h->place);
        }
    }
}

/* ---- presence: A's word on who plays (A's player plays the absent's units) ---- */

static void sendTake(void) {
    Hdr h;
    hdr(&h, B_TAKE, 0, B.step, B.present);
    sendAll(&h, sizeof(h));
}

/* who decides for a place's unit in this step: on a follower, A once A's B_TAKE took the place over in this
   step, even if this step's header still had it present (behind, or the place's late choice came first) */
static int deciderOf(int place) {
    const int c = controller(place);
    if (!B.iAmA && c != 0 && B.takeAt[c] == B.step + 1) {
        return 0;
    }
    return c;
}

/* the decision (B_CMD, or B_RES of kind sub) at k for a place's unit in this step, or NULL (not yet). A
   sends one for another place's unit only when it took that place over, and then ignores the place's own
   (a stall that ended just as the place chose: its choice comes late): a follower takes A's whenever it's
   there, and waits for A's once A's B_TAKE said so, so every game acts on the same choice. But A takes a
   place over mid-step, and that step's decisions from before the takeover were the place's own (A used
   them and sent no copy): once A has finished the step (its B_STEP, sent after all its decisions) with no
   copy of its own at k, A used the place's, so a follower behind takes the place's too */
static const Got* decisionOf(int kind, int sub, uint32_t k, int place) {
    const int c = controller(place);
    const Got* a;
    if (B.iAmA || c == 0) {
        return gotFind(kind, sub, B.step, k, c);
    }
    a = gotFind(kind, sub, B.step, k, 0);
    if (a != NULL) {
        return a;
    }
    if (deciderOf(place) == 0 && gotFind(B_STEP, 0, B.step, 0, 0) == NULL) {
        return NULL; /* (A may still send its own: wait for it) */
    }
    return gotFind(kind, sub, B.step, k, c);
}

static void presenceFrame(void) {
    const unsigned long long now = ssNetMs();
    int s;
    if (!B.iAmA) {
        /* A gone: this game finishes the battle alone, as run away (A's game keeps the outcome) */
        const SsPeer* a = ssPeerOf(B.slotPlayer[0]);
        static unsigned long long sOutSince;
        /* (a watcher never waits on A's steps: so the time A has been out of the battle, as its reports
           say. A guest that joined the raid under way as a watcher stayed in it for ever after the host's
           was won and the host back in the field) */
        sOutSince = a->valid && !a->inBattle ? (sOutSince != 0 ? sOutSince : now) : 0;
        if (B.watching && sOutSince != 0 && now - sOutSince > 3000) {
            ssLog("battle %u: player 1's game is out of the battle: this watcher's battle ends, as run away", B.id);
            sOutSince = 0;
            runAway();
            leaveShared("player 1 out of the battle");
            return;
        }
        if (B.waitSince != 0 && now - B.waitSince > 3000 && a->valid && !a->inBattle) {
            ssLog("battle %u: player 1's game is out of the battle: this battle alone, as run away", B.id);
            runAway();
            leaveShared("player 1 out of the battle");
            return;
        }
        if (reach(B.slotPlayer[0])) {
            B.aGoneSince = 0;
        } else if (B.aGoneSince == 0) {
            B.aGoneSince = now;
        } else if (now - B.aGoneSince > A_GONE_MS) {
            ssLog("battle %u: player 1's game is gone: this battle alone, as run away", B.id);
            runAway();
            leaveShared("player 1 gone");
        }
        return;
    }
    for (s = 1; s < B.parts && s < SLOTS; s++) {
        const unsigned bit = 1u << s;
        const int p = B.slotPlayer[s];
        if (B.leftMask & bit) {
            if (B.present & bit) {
                B.present &= ~bit;
                sendTake();
            }
            continue;
        }
        if (B.waitOn == s && B.waitSince != 0 && now - B.waitSince > 3000 && ssPeerOf(p)->valid &&
            !ssPeerOf(p)->inBattle) {
            /* (their game is out of the battle: their sharing is over) */
            ssLog("battle %u: player %d's game is out of the battle", B.id, s + 1);
            sendLeave(othersMask() & ~bitOf(p), s);
            gone(s);
            if (B.state != ST_SHARED) {
                return;
            }
            continue;
        }
        if (reach(p)) {
            B.unreachSince[s] = 0;
        } else if (B.unreachSince[s] == 0) {
            B.unreachSince[s] = now;
        }
        if (B.present & bit) {
            /* (a game catching up says how far it has got: not stalled while it gets further) */
            const unsigned long long since = B.atAt[s] > B.waitSince ? B.atAt[s] : B.waitSince;
            const int stalled = B.waitOn == s && B.waitSince != 0 && now - since > STALL_MS;
            if ((B.unreachSince[s] != 0 && now - B.unreachSince[s] > TAKEOVER_MS) || stalled) {
                B.present &= ~bit;
                B.takeovers++;
                B.stallStep[s] = stalled ? B.step + 1 : 0;
                ssLog("battle %u: player %d's game %s: their units are this player's until they're back", B.id,
                      s + 1, stalled ? "says nothing" : "can't be reached");
                sendTake();
            }
        } else if (B.unreachSince[s] == 0 && (B.stallStep[s] == 0 || B.step + 1 > B.stallStep[s]) &&
                   (B.ackStep[s] + 2 >= B.step + 1 ||
                    (B.boundary && B.atAt[s] != 0 && now - B.atAt[s] < 3000))) {
            /* back: caught up; or, still catching up but heard from, at the next phase's start: their units
               theirs from this step (each step's word says so), A waiting for their choices as for anyone's */
            B.present |= bit;
            ssLog("battle %u: player %d is back: their units are theirs again from step %u", B.id, s + 1, B.step);
            sendTake();
        }
    }
    B.boundary = 0;
}

/* ---- the snapshot ---- */

static size_t snapTake(Step* m) {
    BattleWork* wp = _battleWorkPointer;
    int i, n = 0;
    m->flags = (uint32_t)wp->flags & (BATTLE_FLAG_ESCAPED | BATTLE_FLAG_INTERRUPTED | BATTLE_FLAG_RESET_TURN);
    for (i = 0; i < 3; i++) {
        m->clear[i] = (uint8_t)wp->alliances[i].clearConditionMet;
    }
    m->pouchOn = (uint8_t)!B.versus;
    if (m->pouchOn) {
        const PouchData* pd = pouchGetPtr();
        m->sp = pd->currentSP;
        m->maxSp = pd->maximumSP;
        memcpy(m->items, pd->heldItems, sizeof(m->items));
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = wp->units[i];
        SnapUnit* s;
        if (u == NULL) {
            continue;
        }
        s = &m->u[n++];
        memset(s, 0, sizeof(*s));
        s->id = (int16_t)i;
        s->alliance = u->alliance;
        s->moveState = u->moveState;
        s->kind = (int32_t)u->currentKind;
        s->hp = u->currentHp;
        s->maxHp = u->maxHp;
        s->fp = u->currentFp;
        s->maxFp = u->maxFp;
        s->moves = u->movesRemaining;
        s->maxMoves = u->maxMovesThisTurn;
        s->statusFlags = u->statusFlags;
        s->heldItem = (int32_t)u->heldItem;
        memcpy(s->status, &u->statusTurns, sizeof(s->status));
    }
    m->nUnits = (uint16_t)n;
    return offsetof(Step, u) + (size_t)n * sizeof(SnapUnit);
}

/* the corrections' words (the step's log line: the first few) */
static char sFix[160];
static size_t sFixN;
static unsigned sFixCount;
static void fix(int unit, const char* what, int ours, int theirs) {
    sFixCount++;
    if (sFixN < sizeof(sFix) - 40) {
        const char* sep = sFixN ? ", " : "";
        if (unit >= 0) {
            sFixN += (size_t)snprintf(sFix + sFixN, sizeof(sFix) - sFixN, "%sunit %d %s %d->%d", sep, unit, what, ours,
                                      theirs);
        } else {
            sFixN += (size_t)snprintf(sFix + sFixN, sizeof(sFix) - sFixN, "%s%s %d->%d", sep, what, ours, theirs);
        }
    }
}

#define FIX(unit, field, theirs, name)                    \
    do {                                                  \
        if ((field) != (theirs)) {                        \
            fix(unit, name, (int)(field), (int)(theirs)); \
            (field) = (theirs);                           \
        }                                                 \
    } while (0)

/* A's snapshot here: what differs is A's (returns 0 when a unit A has isn't in this battle) */
static int snapApply(const Step* m) {
    BattleWork* wp = _battleWorkPointer;
    uint8_t seen[BATTLE_UNIT_MAX];
    int i, ok = 1;
    sFixN = 0;
    sFixCount = 0;
    sFix[0] = '\0';
    memset(seen, 0, sizeof(seen));
    {
        const u32 keep = BATTLE_FLAG_ESCAPED | BATTLE_FLAG_INTERRUPTED | BATTLE_FLAG_RESET_TURN;
        if (((u32)wp->flags & keep) != (m->flags & keep)) {
            fix(-1, "flags", (int)((u32)wp->flags & keep), (int)(m->flags & keep));
            wp->flags = (s32)(((u32)wp->flags & ~keep) | (m->flags & keep));
        }
    }
    for (i = 0; i < 3; i++) {
        wp->alliances[i].clearConditionMet = m->clear[i]; /* (BattleCheckConcluded's own, every check) */
    }
    if (m->pouchOn && !B.versus) {
        PouchData* pd = pouchGetPtr();
        int itemsOk = 1;
        for (i = 0; i < MAX_HELD_ITEMS; i++) {
            itemsOk &= heldIdOk(m->items[i]);
        }
        FIX(-1, pd->currentSP, m->sp, "SP");
        FIX(-1, pd->maximumSP, m->maxSp, "max SP");
        if (!itemsOk) {
            ssLog("battle %u: step %u: player 1's bag has an id that isn't an item: this game's own kept", B.id,
                  m->h.step);
        } else if (memcmp(pd->heldItems, m->items, sizeof(m->items)) != 0) {
            fix(-1, "items", 0, 1);
            memcpy(pd->heldItems, m->items, sizeof(m->items));
        }
    }
    for (i = 0; i < m->nUnits && i < BATTLE_UNIT_MAX; i++) {
        const SnapUnit* s = &m->u[i];
        BattleUnit* u = s->id >= 0 && s->id < BATTLE_UNIT_MAX ? wp->units[s->id] : NULL;
        if (u == NULL || (int32_t)u->currentKind != s->kind) {
            B.unmatched++;
            if (B.unmatchedSaid < 8) {
                B.unmatchedSaid++;
                ssLog("battle %u: step %u: player 1's unit %d (kind %d) isn't in this battle", B.id, m->h.step,
                      (int)s->id, (int)s->kind);
            }
            ok = 0;
            continue;
        }
        seen[s->id] = 1;
        FIX(s->id, u->currentHp, s->hp, "HP");
        FIX(s->id, u->maxHp, s->maxHp, "max HP");
        FIX(s->id, u->currentFp, s->fp, "FP");
        FIX(s->id, u->maxFp, s->maxFp, "max FP");
        FIX(s->id, u->moveState, s->moveState, "move state");
        FIX(s->id, u->movesRemaining, s->moves, "moves");
        FIX(s->id, u->maxMovesThisTurn, s->maxMoves, "moves this turn");
        FIX(s->id, u->statusFlags, s->statusFlags, "status flags");
        if ((int32_t)u->heldItem != s->heldItem && itemIdOk(s->heldItem)) { /* (an id past the item table: ours kept) */
            fix(s->id, "item", (int)u->heldItem, (int)s->heldItem);
            u->heldItem = (ItemType)s->heldItem;
        }
        if (memcmp(&u->statusTurns, s->status, sizeof(s->status)) != 0) {
            fix(s->id, "statuses", 0, 1);
            memcpy(&u->statusTurns, s->status, sizeof(s->status));
        }
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = wp->units[i];
        if (u != NULL && !seen[i] && u->instantKillStrength == 0 && u->alliance != 2) {
            /* gone in A's battle: out of this one too (it neither acts nor counts) */
            fix(i, "out", 0, 1);
            u->instantKillStrength = 1;
        }
    }
    if (sFixCount > 0) {
        B.corrections += sFixCount;
        B.correctedSteps++;
        ssLog("battle %u: step %u: %u correction%s from player 1's: %s", B.id, m->h.step, sFixCount,
              sFixCount == 1 ? "" : "s", sFix);
    }
    return ok;
}

/* ---- an action's own random numbers ----
 * An action starts from the same random numbers in every game (B_EXEC), but the game's random numbers are
 * one sequence, and every frame each game also draws from it for its own: the audience, effects, a test's
 * presses, its own player's judging. Those draws differ by game (each runs at its own pace, waits on other
 * games, shows its own player's side), so an enemy's script, drawing after them (its target: unit 6 hit
 * Mario in player 1's game and the partner in player 2's), drew from numbers gone apart. So the scripts of
 * the acting unit (the event its action started, its children and brothers: they carry its unit) draw from a
 * sequence of their own while the action runs, from the action's start to the next step gate: every game's
 * the same, as each game's scripts make the same calls. The rest of the game draws from the other, as ever.
 * And the action's decisions by chance (its target, which move, a draw) are the deciding game's besides
 * (R_PICK, below): the other games take them, never their own. */
static struct {
    int on;          /* an action under way (its start to the next step gate) */
    int unitId;      /* its unit */
    int inside;      /* one of its scripts running now (evtmgrMain) */
    uint32_t rand;   /* the action's own random numbers */
    uint32_t world;  /* the rest's, kept meanwhile */
} sAct;

static void actStart(BattleWork* wp) {
    const BattleUnit* u = wp != NULL ? BattleGetUnitPtr(wp, wp->activeUnitId) : NULL;
    sAct.on = B.state == ST_SHARED && u != NULL;
    sAct.unitId = u != NULL ? (int)u->unitId : -1; /* (its scripts': btlseqAct sets EventEntry.unitId so) */
    sAct.rand = portRandState();
}

static void actStop(void) {
    sAct.on = 0; /* (between scripts: evtmgrMain puts the rest's back as each ends) */
}

/* evtmgrMain, an event's turn: the acting unit's scripts draw from the action's own numbers */
void portStateSyncBattleEvtBegin(int unitId) {
    if (!sAct.on || sAct.inside || unitId < 0 || unitId != sAct.unitId || B.state != ST_SHARED) {
        return;
    }
    sAct.world = portRandState();
    portRandSetState(sAct.rand);
    sAct.inside = 1;
}

void portStateSyncBattleEvtEnd(void) {
    if (!sAct.inside) {
        return;
    }
    sAct.rand = portRandState();
    portRandSetState(sAct.world);
    sAct.inside = 0;
}

/* ---- the step gates (battle_seq.c) ---- */

/* a follower without A's step for this gate yet: 1 (the sequence stays where it is) */
int portStateSyncBattleWait(void* work, int gate) {
    (void)work;
    (void)gate;
    if (B.state != ST_SHARED || B.iAmA) {
        return 0;
    }
    if (gotFind(B_STEP, 0, B.step, 0, 0) != NULL) {
        return 0;
    }
    tellWaiting(0, 0);
    waitBegin(0);
    return 1;
}

/* tests: $TTYD_SYNC_BINGO_STEP=<n>: every game turns the BINGO's three cards as its step counter reaches n (the
   same random numbers in each: after A's, taken), so the spin starts as that step's action is over (between
   turns, BattleBreakSlot_CheckReaction) and its last reel waits for an A on each screen */
static int sBingoWatch; /* (bingoWatch: the cards turned, the reels' stop not seen yet) */

static void bingoTest(void) {
    static int sAt = -2;
    int turns;
    if (sAt == -2) {
        const char* e = getenv("TTYD_SYNC_BINGO_STEP");
        sAt = e != NULL ? atoi(e) : -1;
    }
    if (sAt < 0 || B.step != (uint32_t)sAt || _battleWorkPointer == NULL) {
        return;
    }
    for (turns = 0; turns < 3 && pouchGetPtr()->starPowersObtained != 0 && BattleBreakSlotGetPtr()->reelNo < 3;
         turns++) {
        BattleBreakSlot_PointInc();
    }
    ssLog("battle %u: step %u: test: the BINGO's cards turned (%d)", B.id, B.step, (int)BattleBreakSlotGetPtr()->reelNo);
    sBingoWatch = 1;
}

/* tests ($TTYD_SYNC_BINGO_STEP, a frame of a shared battle): once the cards are turned, the side whose phase the
   reels spin in (a versus fight: Luigi's reads the second controller as the first, portVersusRoute) and the
   reels' stop (all three stopped: BattleBreakSlot_Main's phase 3 over), said once each */
static void bingoWatch(void) {
    static int sLast;
    int phase;
    int side = 0;
    /* (the battle's work read only when there is one: a watcher's game in a shared battle's frames before
       its own battle began, or after it ended, has none. Read first, as it was, it was a read through
       NULL, and the compiler, taking it as there, also left out ssBattleApplyPad's own check after it: a
       watcher's crash in the goons' fight) */
    if (sBingoWatch == 0 || !gp->inBattle || _battleWorkPointer == NULL) {
        return;
    }
    phase = BattleBreakSlotGetPtr()->phase;
#ifdef TTYD_BACKEND_AURORA
    side = B.versus ? portVersusSide() : 0;
#endif
    if (phase == 3 && sLast != 3) {
        ssLog("battle %u: step %u: test: the BINGO's reels spin in %s's side's phase", B.id, B.step,
              side ? "Luigi" : "Mario");
    } else if (phase != 3 && sLast == 3) {
        ssLog("battle %u: step %u: test: the BINGO's reels stopped (%s's side's phase)", B.id, B.step,
              side ? "Luigi" : "Mario");
        sBingoWatch = 0;
    }
    sLast = phase;
}

/* the gate: A's verdict (A: its own, sent with its snapshot; a follower: A's, its snapshot taken) */
int portStateSyncBattleStep(void* work, int gate, int ours) {
    static Step m;
    BattleWork* wp = (BattleWork*)work;
    if (B.state != ST_SHARED || wp == NULL) {
        return ours;
    }
    actStop(); /* (an action over: the next starts from the step's numbers, B_EXEC) */
    if (B.iAmA) {
        size_t n;
        if (gate == 3) {
            teamEval(); /* (a phase begins: the players' turn before it judged) */
            B.boundary = 1;
        }
        memset(&m, 0, offsetof(Step, u));
        hdr(&m.h, B_STEP, gate, B.step, B.present);
        m.verdict = ours;
        m.rand = portRandState();
        n = snapTake(&m);
        sendAll(&m, n);
        B.snapBytes += (unsigned)n;
        B.step++;
        memset(B.kCount, 0, sizeof(B.kCount));
        memset(B.sentK, 0, sizeof(B.sentK));
        bingoTest();
        return ours;
    }
    {
        const Got* g = gotFind(B_STEP, 0, B.step, 0, 0);
        int verdict;
        if (g == NULL) {
            return ours; /* (portStateSyncBattleWait came first: not reached) */
        }
        memset(&m, 0, sizeof(m));
        memcpy(&m, g->p, g->n < sizeof(m) ? g->n : sizeof(m));
        B.snapBytes += g->n;
        if (m.h.sub != gate) {
            ssLog("battle %u: step %u is player 1's gate %d, this game's %d", B.id, B.step, m.h.sub, gate);
        }
        B.present = m.h.k;
        portRandSetState(m.rand);
        snapApply(&m);
        verdict = m.verdict;
        if (gate == 0) {
            /* the actor: A's (a unit this battle lacks: this game can't follow it any more) */
            if (verdict < 0 || verdict >= BATTLE_UNIT_MAX || wp->units[verdict] == NULL) {
                ssLog("battle %u: step %u: player 1's actor %d isn't in this battle: this battle alone", B.id,
                      B.step, verdict);
                B.step++;
                leaveShared("out of step");
                return ours;
            }
            if (verdict != ours) {
                /* (battle_seq.c makes it the actor) */
                ssLog("battle %u: step %u: player 1's actor (unit %d) in place of this game's (unit %d)", B.id,
                      B.step, verdict, ours);
                B.corrections++;
            }
        } else if (verdict != ours) {
            ssLog("battle %u: step %u: player 1's verdict %d at gate %d (this game's %d)", B.id, B.step, verdict,
                  gate, ours);
        }
        B.step++;
        memset(B.kCount, 0, sizeof(B.kCount));
        bingoTest();
        portNetFaultsBattleStep(B.step); /* (tests: a drop at a battle's step) */
        if (B.mySlot > 0 && B.topStep > B.step + 1 && B.step % 2 == 0) {
            /* (catching up after a drop: A hears how far this game has got, so it waits for it) */
            Hdr h;
            hdr(&h, B_AT, 0, B.step, 0);
            sendTo(bitOf(B.slotPlayer[0]), &h, sizeof(h));
        }
        return verdict;
    }
}

/* this game's random numbers as an action starts here, for the others (B_EXEC at the next k) */
static void execSend(void) {
    Exec x;
    hdr(&x.h, B_EXEC, 0, B.step, B.kCount[R_EXEC]++);
    x.rand = portRandState();
    sendAll(&x, sizeof(x));
}

/* btlseqAct 0x6000006, the action's script about to start: the random numbers of the game whose player plays
   the acting unit (A's for the enemies and for the units A plays). That game starts the action at once and
   sends its numbers: a player's own action never waits a round trip to A after the choice. Every other game
   (A too) waits for them: 1 while they haven't come */
static int execGate(BattleWork* wp) {
    const Got* g;
    int place, c;
    uint32_t k;
    if (B.state != ST_SHARED) {
        return 0;
    }
    place = wp != NULL ? unitPlace(BattleGetUnitPtr(wp, wp->activeUnitId)) : 0;
    c = deciderOf(place);
    k = B.kCount[R_EXEC];
    if (c == 0 && B.iAmA) {
        execSend();
        return 0;
    }
    if (c != 0 && c == B.mySlot && mine(place)) {
        /* (this game's player's own unit: the action starts here now, the others take these numbers) */
        execSend();
        B.ownStarts++;
        if (verbose()) {
            ssLog("battle %u: step %u: unit %d's action starts here at once (its player's game: no wait)", B.id,
                  B.step, wp != NULL ? (int)wp->activeUnitId : -1);
        }
        return 0;
    }
    /* (A: the acting unit's player's start; a follower: A's, or another player's for their unit) */
    g = B.iAmA ? gotFind(B_EXEC, 0, B.step, k, c) : decisionOf(B_EXEC, 0, k, place);
    if (g == NULL) {
        if (B.iAmA) {
            if (giveUp(c)) {
                /* (that game waits on A meanwhile: their battles went apart. A starts it with its own) */
                ssLog("battle %u: step %u: player %d's action start never came: this game's own numbers", B.id,
                      B.step, c + 1);
                B.unmatched++;
                execSend();
                return 0;
            }
            B.waitOn = c;
            waitBegin(c);
            return 1;
        }
        if (gotFind(B_STEP, 0, B.step, 0, 0) != NULL) {
            B.kCount[R_EXEC]++; /* (A's step is over without it: an action A's battle didn't have) */
            B.unmatched++;
            return 0;
        }
        if (c == 0) {
            tellWaiting(R_EXEC, k);
        }
        B.waitOn = c;
        waitBegin(c);
        return 1;
    }
    if (g->n >= sizeof(Exec)) {
        Exec x;
        memcpy(&x, g->p, sizeof(x));
        portRandSetState(x.rand);
    } else {
        ssLog("battle %u: step %u: player %d's action start too short (%u bytes): its random numbers not taken",
              B.id, B.step, ((const Hdr*)g->p)->place + 1, (unsigned)g->n);
    }
    B.kCount[R_EXEC]++;
    return 0;
}

int portStateSyncBattleExec(void* work) {
    BattleWork* wp = (BattleWork*)work;
    if (execGate(wp)) {
        return 1;
    }
    actStart(wp); /* (the action's scripts draw from these numbers: "an action's own random numbers") */
    return 0;
}

/* ---- the command menu ---- */

/* btlseqAct 0x6000002: -1 this game's player chooses (BattleCommandInput as ever), 0 another's choice
   hasn't come, 1 it's taken (the menu's outcome written) */
int portStateSyncBattleCommand(void* work) {
    BattleWork* wp = (BattleWork*)work;
    BattleUnit* unit;
    BattleCommand* cmd;
    const Got* g;
    int place;
    static Cmd c;
    int i;
    if (B.state != ST_SHARED || wp == NULL) {
        return -1;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    place = unitPlace(unit);
    if (unit == NULL || mine(place)) {
        return -1;
    }
    if (portBtlcmdCloseMenus(wp)) {
        /* (this game's menu for the unit was open: player 1 took the turn over, its choice comes from there) */
        ssLog("battle %u: step %u: unit %d is player %d's to play now: this game's menu for it closed", B.id, B.step,
              wp->activeUnitId, controller(place) + 1);
    }
    /* (A took the place over in this step: A's choice, never the place's own coming late) */
    g = decisionOf(B_CMD, 0, B.kCount[R_CMD], place);
    if (g == NULL || g->n < sizeof(Cmd)) {
        if (giveUp(deciderOf(place))) {
            /* (never coming this step: the unit waits) */
            ssLog("battle %u: step %u: player %d's choice never came: the unit waits", B.id, B.step,
                  deciderOf(place) + 1);
            B.unmatched++;
            B.kCount[R_CMD]++;
            unit->attackEventCode = btldefaultevt_Wait;
            unit->attackEventFlags = ATTACK_EVENT_KEEP_STATUS_WINDOW;
            unit->moveState = 0;
            wp->commandMenu.cancelled = 1;
            return 1;
        }
        if (deciderOf(place) == 0) {
            tellWaiting(R_CMD, B.kCount[R_CMD]);
        }
        B.waitOn = deciderOf(place);
        waitBegin(deciderOf(place));
        return 0;
    }
    memcpy(&c, g->p, sizeof(c));
    B.kCount[R_CMD]++;
    if (!targetsOk(&c.targets)) {
        /* (targets the game would read past its list: the choice dropped, the unit waits) */
        ssLog("battle %u: step %u: player %d's choice dropped: its targets are out of range (%d, at %d)", B.id,
              B.step, c.h.place + 1, (int)c.targets.numTargets, (int)c.targets.currentTarget);
        B.unmatched++;
        unit->attackEventCode = btldefaultevt_Wait;
        unit->attackEventFlags = ATTACK_EVENT_KEEP_STATUS_WINDOW;
        unit->moveState = 0;
        wp->commandMenu.cancelled = 1;
        return 1;
    }
    /* (the rest within the game's ranges: the menu's type one with a cursor (battle_seq_command.c
       _btlcmd_GetCursorPtr), each cursor within the longest list (the weapons' 21: the game clamps it to its
       menu's when it builds one), each item an item, its pouch place within the pouch's lists, the partner
       a partner's kind (BattleChangeParty reads entryunit_party by it: 0 none, the game's own "no choice")) */
    if (c.menuType < -1 || (c.menuType > 8 && c.menuType != 10 && c.menuType != 14)) {
        c.menuType = -1;
    }
    for (i = 0; i < 14; i++) {
        if (c.cursor[i].absPos < -1 || c.cursor[i].absPos >= 21) {
            c.cursor[i].absPos = 0;
        }
        if (c.cursor[i].relPos < -1 || c.cursor[i].relPos >= 21) {
            c.cursor[i].relPos = 0;
        }
        if (c.cursor[i].count < 0 || c.cursor[i].count > 21) {
            c.cursor[i].count = 0;
        }
    }
    for (i = 0; i < 21; i++) {
        if (!itemIdOk(c.weaponItem[i])) {
            c.weaponItem[i] = 0;
        }
        if (c.weaponItem[i] != 0 && (c.weaponIndex[i] < 0 || c.weaponIndex[i] >= MAX_BADGES)) {
            c.weaponIndex[i] = 0;
        }
    }
    if ((c.partyKind & ~0xFFF) != 0 || PM64_BTL_KIND_OF(c.partyKind) < TYPE_PARTNER_MIN ||
        PM64_BTL_KIND_OF(c.partyKind) >= TYPE_PARTNER_MAX) {
        c.partyKind = 0;
    }
    cmd = &wp->commandMenu;
    if (c.unitId != wp->activeUnitId) {
        ssLog("battle %u: step %u: the choice came for unit %d, this game's actor is unit %d", B.id, B.step,
              c.unitId, wp->activeUnitId);
    }
    {
        /* this game's own weapons and scripts for what was chosen: its menu built again first (the same
           menu: the same battle, the same pouch), then each named as A's game named it */
        int menuN = 0, ok, lost = 0;
        const void* q;
        const BattleWeapon* chosen;
        const void* event;
        /* the chosen weapon and its script first (a weapon not in the menu built last: searched for in all
           of them), then the menu built last, as the other game had it */
        chosen = (const BattleWeapon*)unwirePtr(wp, c.weapon, -1, NULL, &ok);
        lost += !ok;
        event = unwirePtr(wp, c.attackEvent, -1, chosen, &ok);
        if (!ok) {
            event = btldefaultevt_Wait;
            lost++;
        }
        {
            /* the menu's table (the action reads its weapon from it, BattleGetSelectWeapon): each entry at
               its place in the menu built again, else found by its print among all the menus (a computer
               player's pick, a menu the other game built in another order); the menus are built during the
               search, so the weapons are kept apart first and written after */
            static const BattleWeapon* sFound[21];
            if (c.menuType >= 0) {
                menuN = (int)portBtlcmdWeaponTable(wp, c.menuType);
            }
            for (i = 0; i < 21; i++) {
                q = unwirePtr(wp, c.menuWeapon[i], menuN, chosen, &ok);
                sFound[i] = ok ? (const BattleWeapon*)q : NULL;
                if (!ok && c.menuWeapon[i].kind == P_MENU) {
                    sFound[i] = (const BattleWeapon*)unwirePtr(wp, c.menuWeapon[i], -1, chosen, &ok);
                    if (c.menuType >= 0) {
                        menuN = (int)portBtlcmdWeaponTable(wp, c.menuType); /* (as it was, for the rest) */
                    }
                }
                if (!ok) {
                    lost++;
                    sFound[i] = i < menuN ? cmd->weapon[i].weapon : NULL; /* (this game's own menu's stays) */
                }
            }
            for (i = 0; i < 21; i++) {
                cmd->weapon[i].weapon = (BattleWeapon*)sFound[i];
            }
        }
        for (i = 0; i < 21; i++) {
            cmd->weapon[i].itemId = c.weaponItem[i];
            cmd->weapon[i].index = c.weaponIndex[i];
            cmd->weapon[i].enabled = c.weaponEnabled[i];
        }
        unit->attackEventCode = (EvtWord*)event;
        unwireTargets(&wp->weaponTargets, &c.targets);
        wp->weaponTargets.weapon = (BattleWeapon*)chosen;
        if (lost > 0) {
            ssLog("battle %u: step %u: %d of the choice's weapons and scripts not found here (kinds %d %d)", B.id,
                  B.step, lost, c.attackEvent.kind, c.weapon.kind);
        }
    }
    unit->attackEventFlags = c.attackEventFlags;
    unit->moveState = c.moveState;
    cmd->actType = c.actType;
    cmd->prevActType = c.prevActType;
    cmd->operationType = c.operationType;
    cmd->partyKind = c.partyKind;
    cmd->dipMode = c.dipMode;
    cmd->cancelled = c.cancelled;
    cmd->attackAudience = c.attackAudience;
    for (i = 0; i < 14; i++) {
        cmd->cursor[i].absPos = c.cursor[i].absPos;
        cmd->cursor[i].relPos = c.cursor[i].relPos;
        cmd->cursor[i].count = c.cursor[i].count;
    }
    wp->flags = (s32)(((u32)wp->flags & ~(u32)BATTLE_FLAG_ACT_DECIDED) | (c.decided & BATTLE_FLAG_ACT_DECIDED));
    wp->actRecord = c.rec;
    cmd->flags &= ~CMD_MENU_FLAG_SHOW_ALL_ENEMY_HP;
    wp->dispFlags &= ~BATTLE_DISP_FLAG_COMMAND_MENU;
    B.choicesTaken++;
    if (verbose()) {
        ssLog("battle %u: step %u: choice taken: event %d/%d, weapon %d/%d, %d targets", B.id, B.step,
              c.attackEvent.kind, c.attackEvent.a, c.weapon.kind, c.weapon.a, (int)c.targets.numTargets);
    }
    ssLog("battle %u: step %u: player %d's choice for unit %d taken (act %d)", B.id, B.step, c.h.place + 1,
          wp->activeUnitId, c.actType);
    return 1;
}

/* the menu decided here (BattleCommandInput returned 1): the others take it */
void portStateSyncBattleCommandMade(void* work) {
    static Cmd c;
    BattleWork* wp = (BattleWork*)work;
    BattleUnit* unit;
    const BattleCommand* cmd;
    int i;
    if (B.state != ST_SHARED || wp == NULL) {
        return;
    }
    unit = BattleGetUnitPtr(wp, wp->activeUnitId);
    cmd = &wp->commandMenu;
    memset(&c, 0, sizeof(c));
    sentNote(R_CMD, B.kCount[R_CMD]);
    hdr(&c.h, B_CMD, 0, B.step, B.kCount[R_CMD]++);
    c.unitId = wp->activeUnitId;
    if (unit != NULL) {
        c.attackEventFlags = unit->attackEventFlags;
        c.attackEvent = wirePtr(wp, unit->attackEventCode, 1);
        c.moveState = unit->moveState;
    }
    c.dipMode = cmd->dipMode;
    c.cancelled = cmd->cancelled;
    c.attackAudience = cmd->attackAudience;
    c.actType = cmd->actType;
    c.prevActType = cmd->prevActType;
    c.operationType = cmd->operationType;
    c.partyKind = cmd->partyKind;
    for (i = 0; i < 14; i++) {
        c.cursor[i].absPos = cmd->cursor[i].absPos;
        c.cursor[i].relPos = cmd->cursor[i].relPos;
        c.cursor[i].count = cmd->cursor[i].count;
    }
    c.menuType = portBtlcmdWeaponTableType();
    for (i = 0; i < 21; i++) {
        c.menuWeapon[i] = wirePtr(wp, cmd->weapon[i].weapon, 0);
        c.weaponItem[i] = cmd->weapon[i].itemId;
        c.weaponIndex[i] = cmd->weapon[i].index;
        c.weaponEnabled[i] = cmd->weapon[i].enabled;
    }
    c.decided = (u32)wp->flags & BATTLE_FLAG_ACT_DECIDED;
    wireTargets(&c.targets, &wp->weaponTargets);
    c.weapon = wirePtr(wp, wp->weaponTargets.weapon, 0);
    c.rec = wp->actRecord;
    if (c.attackEvent.kind == P_LOST || c.weapon.kind == P_LOST) {
        ssLog("battle %u: step %u: the choice's %s isn't one this game can name: the others take their own",
              B.id, B.step, c.attackEvent.kind == P_LOST ? "script" : "weapon");
    }
    sendAll(&c, sizeof(c));
    B.choicesMade++;
    if (verbose()) {
        ssLog("battle %u: step %u: choice for unit %d sent: act %d, event %d/%d, weapon %d/%d, menu %d", B.id,
              B.step, c.unitId, c.actType, c.attackEvent.kind, c.attackEvent.a, c.weapon.kind, c.weapon.a,
              (int)c.menuType);
    }
}

/* ---- outcomes (battle_event_cmd.c, dungeon_master_aurora.c) ---- */

/* tests: $TTYD_SYNC_HOLD_RESULT=<n>: a follower's n-th own outcome of this battle (an action command, a guard or a
   Stylish, from 1) is never sent, as a message lost for good would be: A takes it as a miss (resultLate) */
static int holdResult(int kind) {
    static int sAt = -2, sCount;
    static uint32_t sId;
    if (sAt == -2) {
        const char* e = getenv("TTYD_SYNC_HOLD_RESULT");
        sAt = e != NULL ? atoi(e) : -1;
    }
    if (sAt <= 0 || B.iAmA || (kind != R_AC && kind != R_GUARD && kind != R_STYLE)) {
        return 0;
    }
    if (sId != B.id) {
        sId = B.id;
        sCount = 0;
    }
    if (++sCount != sAt) {
        return 0;
    }
    ssLog("battle %u: step %u: test: this player's outcome %d #%u held back (never sent)", B.id, B.step, kind,
          B.kCount[kind]);
    return 1;
}

static void putRes(int kind, const int32_t* v, int n) {
    Res r;
    if (verbose()) {
        ssLog("battle %u: step %u: outcome %d #%u sent: %d %d %d %d", B.id, B.step, kind, B.kCount[kind], (int)v[0],
              n > 1 ? (int)v[1] : 0, n > 2 ? (int)v[2] : 0, n > 3 ? (int)v[3] : 0);
    }
    sentNote(kind, B.kCount[kind]);
    hdr(&r.h, B_RES, kind, B.step, B.kCount[kind]++);
    memset(r.v, 0, sizeof(r.v));
    memcpy(r.v, v, (size_t)(n < 12 ? n : 12) * sizeof(int32_t));
    if (holdResult(kind)) {
        return;
    }
    sendAll(&r, sizeof(r));
}

/* ---- a player's outcome late or lost: a miss every game agrees on ----
 * The player's own game judged it and goes on with its own; A must not wait for ever, nor give up on a player
 * whose game is merely behind (its own pace: the action began later there). So A goes by the player's beat: an
 * outcome the beat says that game has passed (sent) and that hasn't come in RESULT_LATE_MS, or any A waits on
 * while that game has said nothing for RESULT_SILENT_MS, is a miss. A sends the miss as its own at that k
 * (marked: Hdr.pad), so every other game takes A's and not the player's coming later; the player's own game is
 * put right by A's next snapshot. Never a menu choice (a player may think), a dungeon master's pick or a duo half
 * (both halves run at once: their own rules) */

static int beatIdx(int kind) {
    return kind == R_AC ? 0 : kind == R_GUARD ? 1 : kind == R_STYLE ? 2 : -1;
}

/* a follower: its beat to A, every BEAT_MS (ssBattleApplyPad, each frame) */
static void beatSend(void) {
    const unsigned long long now = ssNetMs();
    Hdr h;
    if (B.iAmA || B.mySlot <= 0 || B.watching || now - B.beatSent < BEAT_MS) {
        return;
    }
    B.beatSent = now;
    hdr(&h, B_AT, 1, B.step,
        (B.kCount[R_AC] & 0xFFu) | (B.kCount[R_GUARD] & 0xFFu) << 8 | (B.kCount[R_STYLE] & 0xFFu) << 16);
    sendTo(bitOf(B.slotPlayer[0]), &h, sizeof(h));
}

/* A, waiting on place c's outcome of that kind at k: 1 it is late or lost (logged): a miss */
static int resultLate(int kind, uint32_t k, int c) {
    static struct LateWait {
        uint32_t id, step, k;
        unsigned long long since, passedAt;
    } sWait[3]; /* (by kind: a guard's wait and a Stylish's can be under way at once) */
    const unsigned long long now = ssNetMs();
    const int i = beatIdx(kind);
    struct LateWait* w;
    unsigned long long heard;
    int passed;
    if (!B.iAmA || i < 0 || c <= 0 || c >= SLOTS) {
        return 0;
    }
    w = &sWait[i];
    if (w->id != B.id || w->step != B.step + 1 || w->k != k) {
        w->id = B.id;
        w->step = B.step + 1;
        w->k = k;
        w->since = now;
        w->passedAt = 0;
    }
    passed = B.beatStep[c] > B.step + 1 || (B.beatStep[c] == B.step + 1 && B.beatK[c][i] > k);
    if (passed && w->passedAt == 0) {
        w->passedAt = now;
    }
    heard = B.beatAt[c] > w->since ? B.beatAt[c] : w->since;
    if (passed && now - w->passedAt > RESULT_LATE_MS) {
        ssLog("battle %u: step %u: player %d's outcome %d #%u never came (%u ms after their game had passed it): a "
              "miss",
              B.id, B.step, c + 1, kind, k, (unsigned)(now - w->passedAt));
    } else if (now - heard > RESULT_SILENT_MS) {
        ssLog("battle %u: step %u: player %d's outcome %d #%u never came (their game silent %u ms): a miss", B.id,
              B.step, c + 1, kind, k, (unsigned)(now - heard));
    } else {
        return 0;
    }
    w->id = 0;
    B.givenUp++;
    return 1;
}

/* A: the miss sent as its own at k, for that place (every game takes A's; the player's own logs it) */
static void putMiss(int kind, uint32_t k, int place, const int32_t* v, int n) {
    Res r;
    sentNote(kind, k);
    hdr(&r.h, B_RES, kind, B.step, k);
    r.h.pad = (uint8_t)(0x80 | (place & 0x7F));
    memset(r.v, 0, sizeof(r.v));
    memcpy(r.v, v, (size_t)(n < 12 ? n : 12) * sizeof(int32_t));
    sendAll(&r, sizeof(r));
}

/* the index a script call of that kind waits with (its first call takes the next) */
static uint32_t pendKey(void* e, int kind, int first) {
    int i, freeAt = -1;
    for (i = 0; i < PEND_MAX; i++) {
        if (sPend[i].e == e && sPend[i].kind == kind && sPend[i].step == B.step + 1) {
            if (!first) {
                return sPend[i].k;
            }
            sPend[i].e = NULL;
        }
        if (sPend[i].e == NULL && freeAt < 0) {
            freeAt = i;
        }
    }
    if (freeAt < 0) {
        freeAt = 0;
    }
    sPend[freeAt].e = e;
    sPend[freeAt].kind = kind;
    sPend[freeAt].step = B.step + 1;
    sPend[freeAt].k = B.kCount[kind]++;
    return sPend[freeAt].k;
}

static void pendDone(void* e, int kind) {
    int i;
    for (i = 0; i < PEND_MAX; i++) {
        if (sPend[i].e == e && sPend[i].kind == kind) {
            sPend[i].e = NULL;
        }
    }
}

/* another game's outcome of that kind at k: 1 its values, 0 not yet, -1 never this step (keep ours), -2 (A) late
   or lost: a miss, the caller sends it (putMiss). tell: a wait A may hear of (tellWaiting): 0 while this game's
   own side of it still runs (an action command still going here), when the script here isn't held by it yet */
static int takeRes(int kind, uint32_t k, int place, int32_t* v, int tell) {
    const Got* g = decisionOf(B_RES, kind, k, place); /* (taken over mid-step: A's, as for the choice) */
    if (g != NULL && g->n >= sizeof(Res)) {
        memcpy(v, ((const Res*)g->p)->v, sizeof(((const Res*)g->p)->v));
        B.outcomesTaken++;
        if (verbose()) {
            ssLog("battle %u: step %u: outcome %d #%u from player %d: %d %d %d %d", B.id, B.step, kind, k,
                  ((const Hdr*)g->p)->place + 1, (int)v[0], (int)v[1], (int)v[2], (int)v[3]);
        }
        return 1;
    }
    if (giveUp(deciderOf(place))) {
        B.unmatched++;
        return -1;
    }
    if (resultLate(kind, k, deciderOf(place))) {
        return -2;
    }
    if (tell && deciderOf(place) == 0) {
        tellWaiting(kind, k); /* (A, waiting on this game meanwhile, then gives up: blockedOnA) */
    }
    B.waitOn = deciderOf(place);
    waitBegin(deciderOf(place));
    return 0;
}

/* btlevtcmd_ResultAC (running: the command still going here): 1 the script waits on */
/* ---- the team bonus (playing together: statesync.h portStateSyncTeamBonusCount) ----
 * A's game (the host's, with co-op guests) counts each place's action commands in a turn of the players'
 * side; when a phase begins after it, if two players or more acted and every command landed (Nice or
 * better), a quarter bar of Star Power is added in A's game (the next step's snapshot takes it to every
 * game) and every game's count goes up (ssCoopBonus). The host's setting team_bonus (0 off). */
static struct {
    unsigned acted, failed; /* the places, by bit */
} sTeam;

static int teamOn(void) {
    const int* v = portSettingsFind("team_bonus");
    return B.iAmA && portStateSyncCoopHost() && (v == NULL || *v != 0);
}

static void teamNote(const BattleUnit* unit, int place, int32_t result) {
    if (!teamOn() || unit == NULL || unit->alliance != 0 || place < 0 || place >= SLOTS) {
        return;
    }
    sTeam.acted |= 1u << place;
    if (!(result & 2)) {
        sTeam.failed |= 1u << place;
    }
}

static void teamEval(void) {
    if (sTeam.acted != 0 && teamOn() && sTeam.failed == 0 && countBits(sTeam.acted) >= 2) {
        PouchData* pd = pouchGetPtr();
        const int sp = pd->currentSP + 25;
        pd->currentSP = (s16)(sp > pd->maximumSP ? pd->maximumSP : sp);
        ssCoopBonus();
        ssLog("battle %u: step %u: team bonus: %d players all landed their commands: Star Power %d", B.id, B.step,
              countBits(sTeam.acted), pd->currentSP);
    }
    sTeam.acted = sTeam.failed = 0;
}

int portStateSyncBattleAcResult(void* work, int running) {
    BattleWork* wp = (BattleWork*)work;
    int32_t v[12];
    int place, r, i;
    if (B.state != ST_SHARED || wp == NULL) {
        return 0;
    }
    place = unitPlace(wp->actionCommands.acUnit);
    if (mine(place)) {
        if (!running) {
            teamNote(wp->actionCommands.acUnit, place, wp->actionCommands.result);
            v[0] = wp->actionCommands.result;
            v[1] = wp->actionCommands.resultCount;
            for (i = 0; i < 8; i++) {
                v[2 + i] = wp->actionCommands.outputParams[i];
            }
            putRes(R_AC, v, 10);
        }
        return 0;
    }
    r = takeRes(R_AC, B.kCount[R_AC], place, v, !running);
    if (r == 0) {
        return 1;
    }
    if (r == -2) {
        /* (A: late or lost: a miss. This game's command for the unit, on no presses, stopped; its gauge as it
           stands, the result a failure, for every game) */
        if (running) {
            wp->actionCommands.maincb = NULL;
            wp->actionCommands.dispcb = NULL;
        }
        B.acHeldSince = 0;
        wp->actionCommands.result = 0;
        wp->actionCommands.resultCount = 0;
        v[0] = 0;
        v[1] = 0;
        for (i = 0; i < 8; i++) {
            v[2 + i] = wp->actionCommands.outputParams[i];
        }
        putMiss(R_AC, B.kCount[R_AC], place, v, 10);
        B.kCount[R_AC]++;
        teamNote(wp->actionCommands.acUnit, place, 0);
        return 0;
    }
    if (r < 0) {
        B.kCount[R_AC]++;
        return 0; /* (this game's own outcome stands) */
    }
    if (running) {
        /* (the outcome came, the command here still going: held a moment, then stopped) */
        if (B.acHeldSince == 0) {
            B.acHeldSince = ssNetMs();
        }
        if (ssNetMs() - B.acHeldSince < 3000) {
            B.outcomesTaken--; /* (counted when taken) */
            return 1;
        }
        wp->actionCommands.maincb = NULL;
        wp->actionCommands.dispcb = NULL;
    }
    B.acHeldSince = 0;
    B.kCount[R_AC]++;
    wp->actionCommands.result = v[0] & ~1;
    wp->actionCommands.resultCount = v[1];
    teamNote(wp->actionCommands.acUnit, place, wp->actionCommands.result);
    for (i = 0; i < 8; i++) {
        wp->actionCommands.outputParams[i] = v[2 + i];
    }
    return 0;
}

/* tests: $TTYD_SYNC_SPLIT_GUARD=<n>: the first guard from step n on is taken as two games whose battles went
   apart would: A's game takes Mario's guard as player 2's and player 2's game takes its own unit's as A's. One
   of the two is that guard's real player, so one game waits on the other for a guard it never sends while the
   other waits on it: the mutual wait blockedOnA ends */
static int splitGuard(void* event, int first, int place) {
    static int sAt = -2, sDone, sTo;
    static void* sEvent;
    int to = place;
    if (sAt == -2) {
        const char* e = getenv("TTYD_SYNC_SPLIT_GUARD");
        sAt = e != NULL ? atoi(e) : -1;
    }
    if (sAt < 0) {
        return place;
    }
    if (sDone) {
        return !first && event == sEvent ? sTo : place; /* (the same guard's next calls, while it waits) */
    }
    if (B.step < (uint32_t)sAt || B.parts < 2) {
        return place;
    }
    sDone = 1;
    sEvent = event;
    if (B.iAmA && place == 0) {
        to = 1;
    } else if (!B.iAmA && place == B.mySlot) {
        to = 0;
    }
    sTo = to;
    ssLog("battle %u: step %u: test: this guard taken as place %d's (its player's is place %d)", B.id, B.step, to,
          place);
    return to;
}

/* a guard judged on this game's screen alone (its own player's, or its own in a step A finished without the
   player's): it draws none of the action's random numbers, which the games that take it never draw */
static void guardHere(BattleUnit* unit, void* weapon) {
    const uint32_t keep = portRandState();
    BattleActionCommandCheckDefence(unit, (BattleWeapon*)weapon);
    portRandSetState(keep);
}

/* btlevtcmd_ResultACDefence: the guard of the unit's player (1: the script waits for it) */
int portStateSyncBattleGuard(void* event, void* unitp, void* weapon, int first) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* unit = (BattleUnit*)unitp;
    int32_t v[12];
    int place, r;
    uint32_t k;
    if (B.state != ST_SHARED || wp == NULL || unit == NULL) {
        BattleActionCommandCheckDefence(unit, (BattleWeapon*)weapon);
        return 0;
    }
    place = splitGuard(event, first, unitPlace(unit));
    if (mine(place)) {
        guardHere(unit, weapon);
        v[0] = wp->actionCommands.defenseResult;
        putRes(R_GUARD, v, 1);
        return 0;
    }
    k = pendKey(event, R_GUARD, first);
    r = takeRes(R_GUARD, k, place, v, 1);
    if (r == 0) {
        return 1;
    }
    pendDone(event, R_GUARD);
    if (r == -2) {
        /* (A: late or lost: a miss, no guard: the game's own "no press") */
        v[0] = 2;
        putMiss(R_GUARD, k, place, v, 1);
        wp->actionCommands.defenseResult = 2;
        BtlPad_WorkInit(&wp->actionCommands.pad);
        return 0;
    }
    if (r < 0) {
        guardHere(unit, weapon);
        return 0;
    }
    wp->actionCommands.defenseResult = v[0];
    BtlPad_WorkInit(&wp->actionCommands.pad);
    return 0;
}

/* btlevtcmd_ACRGetResult: the Stylish timing of the acting unit's player (1: the script waits) */
int portStateSyncBattleStylish(void* event, int first, int* result, int* frame) {
    BattleWork* wp = _battleWorkPointer;
    int32_t v[12];
    int place, r;
    uint32_t k;
    if (B.state != ST_SHARED || wp == NULL) {
        return 0;
    }
    place = unitPlace(BattleGetUnitPtr(wp, wp->stylishUnitId));
    if (mine(place)) {
        v[0] = *result;
        v[1] = *frame;
        putRes(R_STYLE, v, 2);
        return 0;
    }
    k = pendKey(event, R_STYLE, first);
    r = takeRes(R_STYLE, k, place, v, 1);
    if (r == 0) {
        return 1;
    }
    pendDone(event, R_STYLE);
    if (r == -2) {
        /* (A: late or lost: a miss. This game's own timing for the unit, read from a controller that pressed
           nothing for it: no Stylish) */
        v[0] = *result;
        v[1] = *frame;
        putMiss(R_STYLE, k, place, v, 2);
        return 0;
    }
    if (r > 0) {
        *result = v[0];
        *frame = v[1];
    }
    return 0;
}

/* ---- an action's draws: the deciding game's (R_PICK) ----
 * The acting unit's scripts (an enemy's AI: A's game decides; a player's unit: their game) draw by chance:
 * the target (btlevtcmd_ChoiceSamplingEnemy), which move (btlevtcmd_DrawLots), a number (evt_sub_random,
 * btlevtcmd_GetRandomValue). The deciding game sends each draw as it makes it (v[0] what: PICK_*, then the
 * values), and the other games hold that script call (EVT_RETURN_BLOCK) until it comes and take it in place
 * of their own. Each game still makes its own draw first, so the action's own random numbers go on alike in
 * every game; a draw of its own that came out otherwise is logged (and counted a correction): the sign of
 * random numbers gone apart. A draw that isn't there (another kind at that k, a target this game's list lacks)
 * leaves this game's own, logged: A's next snapshot puts the rest right */
enum { PICK_TARGET = 1, PICK_DRAW = 2 };

static struct {
    void* e;      /* the script call that waited for it */
    int have;     /* 1 the decider's came (v), 0 never (this game's own stands) */
    uint32_t k;
    int32_t v[12];
} sDraw;

/* a draw of the acting unit's scripts in a shared battle: its place (never with a dungeon master, the setting
   every game of the battle has alike: its own picks cross, R_DM, and its draws are its own) */
static int drawPlace(int unitId, int* place) {
    BattleWork* wp = _battleWorkPointer;
    if (B.state != ST_SHARED || wp == NULL || !sAct.on || unitId < 0 || unitId != sAct.unitId ||
        portDmPlayer() != 0) {
        return 0;
    }
    *place = unitPlace(BattleGetUnitPtr(wp, wp->activeUnitId));
    return 1;
}

/* this game decides for the acting unit (as for its start, portStateSyncBattleExec) */
static int drawMine(int place) {
    const int c = deciderOf(place);
    return c == 0 ? B.iAmA : c == B.mySlot && mine(place);
}

int portStateSyncBattleDrawWait(void* event, int unitId, int first) {
    int32_t v[12];
    int place, r;
    uint32_t k;
    if (sDraw.e == event) {
        sDraw.e = NULL;
    }
    if (!drawPlace(unitId, &place) || drawMine(place)) {
        return 0;
    }
    k = pendKey(event, R_PICK, first);
    r = takeRes(R_PICK, k, place, v, 1);
    if (r == 0) {
        return 1;
    }
    pendDone(event, R_PICK);
    sDraw.e = event;
    sDraw.have = r > 0;
    sDraw.k = k;
    memcpy(sDraw.v, v, sizeof(sDraw.v));
    if (r < 0) {
        ssLog("battle %u: step %u: draw #%u of unit %d's action never came: this game's own", B.id, B.step, k,
              unitId);
    }
    return 0;
}

/* the target the decider drew, in this game's list (its unit and part among the targets) */
static int targetListed(const BattleWorkWeaponTargets* t, int32_t unit, int32_t part) {
    int i;
    if (t == NULL) {
        return 0;
    }
    for (i = 0; i < t->numTargets && i < 74; i++) {
        const int at = t->targetIndices[i];
        if (at >= 0 && at < 74 && t->targets[at].unitIdx == unit && t->targets[at].partIdx == part) {
            return 1;
        }
    }
    return 0;
}

/* after this game's own draw (what PICK_*: a target, v[0] unit and v[1] part, from targets; a draw, v[0] in
   [0, max)): the decider's game sends it, another takes the decider's in its place */
static void drawDone(void* event, int unitId, int what, int32_t* v, int n, const void* targets, int max) {
    int32_t out[3];
    int place, ok, i;
    if (!drawPlace(unitId, &place)) {
        return;
    }
    if (drawMine(place)) {
        out[0] = what;
        for (i = 0; i < n; i++) {
            out[1 + i] = v[i];
        }
        putRes(R_PICK, out, 1 + n);
        return;
    }
    if (sDraw.e != event || !sDraw.have) {
        sDraw.e = NULL;
        return;
    }
    sDraw.e = NULL;
    if (what == PICK_TARGET) {
        ok = sDraw.v[0] == what && targetListed((const BattleWorkWeaponTargets*)targets, sDraw.v[1], sDraw.v[2]);
    } else {
        ok = sDraw.v[0] == what && (max <= 0 || (sDraw.v[1] >= 0 && sDraw.v[1] < max));
    }
    if (!ok) {
        ssLog("battle %u: step %u: player %d's draw #%u (%d: %d %d) doesn't fit this game's (%d): its own", B.id,
              B.step, deciderOf(place) + 1, sDraw.k, (int)sDraw.v[0], (int)sDraw.v[1], (int)sDraw.v[2], what);
        B.unmatched++;
        return;
    }
    for (i = 0; i < n; i++) {
        if (v[i] != sDraw.v[1 + i]) {
            ssLog("battle %u: step %u: unit %d's %s: player %d's (%d %d) in place of this game's (%d %d)", B.id,
                  B.step, unitId, what == PICK_TARGET ? "target" : "draw", deciderOf(place) + 1,
                  (int)sDraw.v[1], n > 1 ? (int)sDraw.v[2] : 0, (int)v[0], n > 1 ? (int)v[1] : 0);
            B.corrections++;
            break;
        }
    }
    for (i = 0; i < n; i++) {
        v[i] = sDraw.v[1 + i];
    }
}

void portStateSyncBattleTargetDone(void* event, int unitId, const void* targets, int* unit, int* part) {
    int32_t v[2];
    v[0] = *unit;
    v[1] = *part;
    drawDone(event, unitId, PICK_TARGET, v, 2, targets, 0);
    *unit = (int)v[0];
    *part = (int)v[1];
}

void portStateSyncBattleDrawDone(void* event, int unitId, int* value, int max) {
    int32_t v[1];
    v[0] = *value;
    drawDone(event, unitId, PICK_DRAW, v, 1, NULL, max);
    *value = (int)v[0];
}

/* the dungeon master's picks (dungeon_master_aurora.c): kind 0 the move, 1 a target as the script runs */
void portStateSyncBattlePut(int kind, int a, int b, int c) {
    int32_t v[3];
    if (B.state != ST_SHARED) {
        return;
    }
    v[0] = a;
    v[1] = b;
    v[2] = c;
    putRes(kind ? R_DM_LIVE : R_DM, v, 3);
}

/* the idle players in the audience (crowd_seats_pc.c): its messages, A's verdicts */
int portStateSyncCrowdSend(int toA, const void* data, size_t n) {
    if (B.state != ST_SHARED || B.mySlot < 0) {
        return 0;
    }
    if (toA) {
        if (B.iAmA) {
            return 0;
        }
        sendTo(bitOf(B.slotPlayer[0]), data, n);
    } else {
        sendTo(othersMask(), data, n);
    }
    return 1;
}

int portStateSyncBattleIsA(void) {
    return B.state == ST_SHARED && B.iAmA;
}

unsigned portStateSyncBattleId(void) {
    return B.id;
}

int portStateSyncBattleTake(int kind, int place, int* a, int* b, int* c) {
    int32_t v[12];
    const int rk = kind ? R_DM_LIVE : R_DM;
    int r;
    if (B.state != ST_SHARED) {
        return 2;
    }
    r = takeRes(rk, B.kCount[rk], place, v, 1);
    if (r == 0) {
        return 0;
    }
    B.kCount[rk]++;
    if (r < 0) {
        return 2;
    }
    *a = v[0];
    *b = v[1];
    *c = v[2];
    return 1;
}

/* ---- a duo move's halves (duo_pc.c: each half judged on its own player's screen) ---- */

int portStateSyncBattleDuoMine(void* unitp) {
    const BattleUnit* unit = (const BattleUnit*)unitp;
    if (B.state != ST_SHARED || unit == NULL) {
        return -1;
    }
    /* (this game's own controller is its place's pad, ssBattleApplyPad; A's own place 0 also plays
       the taken-over places, from its own controller too) */
    return mine(unitPlace(unit)) ? (B.mySlot > 0 ? B.mySlot : 0) : -2;
}

void portStateSyncBattleDuoPut(int half, int result, int at, int count) {
    Res r;
    if (B.state != ST_SHARED) {
        return;
    }
    hdr(&r.h, B_RES, R_DUO, B.step, (uint32_t)(half & 1)); /* (one duo move a step: k is the half) */
    memset(r.v, 0, sizeof(r.v));
    r.v[0] = result;
    r.v[1] = at;
    r.v[2] = count;
    sendAll(&r, sizeof(r));
    if (verbose()) {
        ssLog("battle %u: step %u: duo half %d judged here: %d (frame %d, %d presses)", B.id, B.step, half & 1,
              result, at, count);
    }
}

int portStateSyncBattleDuoTake(void* unitp, int half, int* result, int* at, int* count) {
    int32_t v[12];
    int r;
    if (B.state != ST_SHARED || unitp == NULL) {
        return -1;
    }
    r = takeRes(R_DUO, (uint32_t)(half & 1), unitPlace((const BattleUnit*)unitp), v, 0);
    if (r > 0) {
        *result = v[0];
        *at = v[1];
        *count = v[2];
    }
    return r;
}

/* ---- the messages ---- */

static void gotDecision(int from, const uint8_t* p, size_t n) {
    Hdr h;
    int s;
    if (n < sizeof(Hdr) || B.state == ST_NONE || B.state == ST_INVITED_WAIT) {
        return;
    }
    if (n > sizeof(Step) && n > sizeof(Cmd)) {
        /* (longer than any of them: not kept, STORE_MAX of them would be a lot of memory) */
        ssLog("battle %u: player %d's battle message %d dropped: %u bytes", B.id, from, (int)p[0], (unsigned)n);
        return;
    }
    memcpy(&h, p, sizeof(h));
    if (h.id != B.id || B.parts == 0) {
        return;
    }
    s = slotOf(from);
    if (s < 0 && !(B.watchers & bitOf(from))) {
        return;
    }
    if (h.place >= SLOTS || (s != 0 && h.place != s)) {
        return; /* (A relays everyone's to a late joiner: any place from A; from another, theirs alone) */
    }
    if (h.kind == B_ACK) {
        if (B.iAmA && s > 0) {
            if (h.step + 1 > B.ackStep[s] || (h.step + 1 == B.ackStep[s])) {
                B.ackStep[s] = h.step + 1;
                B.ackWhat[s] = h.sub;
                B.ackK[s] = h.k;
            }
        }
        return;
    }
    if (h.kind == B_AT) {
        if (B.iAmA && s > 0 && s < SLOTS && h.sub == 1) {
            /* a beat: where that game is (beats may cross: the newest step's, each count's highest) */
            if (h.step + 1 > B.beatStep[s]) {
                B.beatStep[s] = h.step + 1;
                memset(B.beatK[s], 0, sizeof(B.beatK[s]));
            }
            if (h.step + 1 == B.beatStep[s]) {
                int i;
                for (i = 0; i < 3; i++) {
                    const uint8_t n = (uint8_t)(h.k >> (8 * i));
                    if (n > B.beatK[s][i]) {
                        B.beatK[s][i] = n;
                    }
                }
            }
            B.beatAt[s] = ssNetMs();
            return;
        }
        if (B.iAmA && s > 0 && s < SLOTS && h.sub == 0) {
            B.atAt[s] = ssNetMs();
            B.atStep[s] = h.step + 1;
        }
        return;
    }
    if (h.kind == B_TAKE) {
        if (h.place == 0 && !B.iAmA) {
            int s;
            /* (A's B_TAKEs come in order: the latest says, for its step, whether A plays each place) */
            for (s = 1; s < SLOTS; s++) {
                if (!(h.k & (1u << s))) {
                    B.takeAt[s] = h.step + 1;
                } else if (B.takeAt[s] == h.step + 1) {
                    B.takeAt[s] = 0;
                }
            }
            if (B.topStep <= B.step + 1) { /* (behind: each step's own word comes with it) */
                B.present = h.k;
                ssLog("battle %u: player 1 says who plays now: places %x", B.id, h.k);
            }
        }
        return;
    }
    if (h.kind == B_STEP && h.place != 0) {
        return; /* (an action's start, B_EXEC, comes from the game of the player whose unit acts) */
    }
    if (h.kind == B_STEP && h.step + 1 > B.topStep) {
        B.topStep = h.step + 1;
    }
    /* (a resend, a duplicate: the first kept) */
    if (gotFind(h.kind, h.sub, h.step, h.k, h.place) != NULL) {
        return;
    }
    if (h.kind == B_RES && h.place == 0 && (h.pad & 0x80) && B.mySlot > 0 && (h.pad & 0x7F) == B.mySlot) {
        /* (A took this player's own outcome as a miss: it came too late there. This game's own stood here; A's
           next snapshot puts it right) */
        ssLog("battle %u: step %u: player 1's game took this player's outcome %d #%u as a miss (it came too late "
              "there)",
              B.id, h.step, h.sub, h.k);
    }
    if (sGotN >= STORE_MAX) {
        /* (full: the oldest steps' go, this game is past them) */
        int i, j = 0;
        for (i = 0; i < sGotN; i++) {
            const Hdr* o = (const Hdr*)sGot[i].p;
            if (o->step + 2 < B.step) {
                free(sGot[i].p);
            } else {
                sGot[j++] = sGot[i];
            }
        }
        sGotN = j;
    }
    if (!gotRoom(&sGot, &sGotCap, sGotN + 1)) {
        return;
    }
    sGot[sGotN].p = (uint8_t*)malloc(n);
    if (sGot[sGotN].p == NULL) {
        return;
    }
    memcpy(sGot[sGotN].p, p, n);
    sGot[sGotN].n = (uint32_t)n;
    sGot[sGotN].from = from;
    sGotN++;
    logKeep(p, n, h.place);
}

/* A: a game joining under way gets the battle so far */
static void sendLog(int to) {
    int i;
    for (i = 0; i < sLogN; i++) {
        sendTo(bitOf(to), sLog[i].p, sLog[i].n);
    }
    ssLog("battle %u: player %d gets the battle so far (%d messages, step %u)", B.id, to, sLogN, B.step);
}

/* ---- the pad read: each game's own controller for its own player's place ---- */

/* a message window is up (msgdrv's: its owner's data is a message's) */
static int messageUp(void) {
    int i;
    for (i = 0; i < PORT_WINDOW_SLOTS; i++) {
        const WindowEntry* w = windowGetPointer(i);
        if ((w->flags & WINDOW_FLAG_USED) && w->userdata != NULL) {
            return 1;
        }
    }
    return 0;
}

int ssBattleApplyPad(PADStatus* pads) {
    PADStatus local, out[SLOTS];
    int s;
    if (B.state != ST_SHARED) {
        return 0;
    }
    /* over: back in the field (each game when it gets there) */
    if (B.frame > 0 && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle) {
        endBattle("back in the field");
        return 0;
    }
    if (seqGetSeq() == SEQ_GAME_OVER) {
        sendGameOver(B.id);
        endBattle("game over");
        return 0;
    }
    waitFrameEnd();
    B.frame++;
    presenceFrame();
    if (B.state != ST_SHARED) {
        return 0;
    }
    beatSend(); /* (a follower: where it is, so A tells a late outcome from a game merely behind) */
    bingoWatch();
    if (_battleWorkPointer != NULL &&
        (!(_battleWorkPointer->flags & BATTLE_FLAG_COMMAND_INPUT) ||
         BattleGetSeq(_battleWorkPointer, SEQ_ACT) != 0x6000002) && /* (btlseqAct's menu step: battle_seq.c) */
        portBtlcmdCloseMenus(_battleWorkPointer)) {
        /* (a menu of this game's left open when the turn went on without it: a turn taken over, a step
           caught up: closed, never drawn over the rest of the battle) */
        ssLog("battle %u: step %u: a command menu left open after its turn: closed", B.id, B.step);
    }
    local = pads[0];
    memset(out, 0, sizeof(out));
    if (B.mySlot >= 0) {
        out[B.mySlot] = local;
    }
    if (B.iAmA) {
        for (s = 1; s < B.parts && s < SLOTS; s++) {
            if (!(B.present & (1u << s))) {
                out[s] = local; /* (taken over: this player plays them) */
            }
        }
    }
    /* (A too: when a guest plays Mario, A's own controller reached nothing that doesn't cross, and A's battle
       waited for ever on a boss's text box or a level up nobody could read on) */
    if (_battleWorkPointer != NULL && gp->inBattle &&
        (BattleGetSeq(_battleWorkPointer, SEQ_UNKNOWN) != 2 || BattleBreakSlotGetPtr()->phase == 3)) {
        /* the battle's start and end (the tally, a level up) and the BINGO reels: each screen's own
           (what they change is A's: its snapshot, its progress afterwards) */
        /* (every place's: whose controller the game reads as the first's then is the turn's player's. A
           versus fight in Luigi's side's phase reads the second controller as the first (versus_aurora.c
           portVersusRoute), and co-op in the partner's turn its player's (second_player_pc.c
           portSecondPlayerRoute): a BINGO spun after such an action waited for an A from a controller that
           never crosses. A third player's game stopped two reels of three and waited on the last for ever,
           player 1's steps piling up unread, as the second player's partner had the turn) */
        for (s = 0; s < B.parts && s < SLOTS; s++) {
            out[s] = local;
        }
    } else if (messageUp()) {
        /* a text box in the battle (a boss's words in its attack): read on with this player's own A and B,
           as A's game does with its own; it changes nothing that crosses. In a versus fight's Luigi's-side phase
           the second controller reads as the first (above): the host's own A and B there too (the BINGO's
           words, a Star Power's) */
        const u16 ab = (u16)(local.button & (PAD_BUTTON_A | PAD_BUTTON_B));
        /* (every place's, as above: the partner's turn in co-op reads its player's controller as the first) */
        for (s = 0; s < B.parts && s < SLOTS; s++) {
            if (s != B.mySlot) {
                out[s].button |= ab;
            }
        }
    }
    for (s = 0; s < SLOTS && s < PAD_MAX_CONTROLLERS; s++) {
        if (s < B.parts) {
            pads[s] = out[B.rolePlace[s] < B.parts ? B.rolePlace[s] : s]; /* (controller n: character n's player's) */
            pads[s].err = PAD_ERR_NONE;
        } else {
            memset(&pads[s], 0, sizeof(pads[s]));
            pads[s].err = PAD_ERR_NO_CONTROLLER;
        }
    }
    return 1;
}

/* every message's size, the same in every build (port/statesync.h WIRE_SIZE) */
WIRE_SIZE(Invite, 2688);
WIRE_SIZE(WireBattle, 1100);
WIRE_SIZE(WireUnit, 52);
WIRE_SIZE(Go, 40);
WIRE_SIZE(LateGo, 48);
WIRE_SIZE(Small, 40);
WIRE_SIZE(Pull, 36);
WIRE_SIZE(SceneText, 1064);
WIRE_SIZE(SceneCam, 48);
WIRE_SIZE(VersusMsg, 8208);
WIRE_SIZE(Hdr, 16);
WIRE_SIZE(SnapUnit, 60);
WIRE_SIZE(Step, 3920);
WIRE_SIZE(Exec, 20);
WIRE_SIZE(Cmd, 3148);
WIRE_SIZE(Res, 64);
WIRE_SIZE(WirePtr, 8);
WIRE_SIZE(WireTargets, 2460);
WIRE_SIZE(PouchData, 0x5D4);
WIRE_SIZE(BattleActRecord, 32);
