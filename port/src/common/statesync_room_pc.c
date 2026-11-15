/* "Sync what matters" (port/include/port/statesync.h), layer 2: a shared
 * room's enemies and objects.
 *
 * In a room both players are in, one game owns the room's NPCs (enemies and
 * townsfolk) and map objects: the player who was there first, the host when
 * both came at once (statesync_pc.c, roomFrame). The owner sends what its NPCs
 * and objects are doing; the other game follows it instead of running its own
 * AI for them (their regular events stopped and frozen, as the game itself
 * freezes NPCs while a battle starts, npcdrv.c; given back when this game owns
 * the room again). Shared both ways, whoever owns the room:
 * - an enemy defeated in either game is defeated in the other: the game's own
 *   npcGroupDead, which runs its dead event and records the defeat;
 * - the room's defeats (FieldBattleData.deadInfo's mask: who stays gone), for
 *   every map, so a room the other player enters later has them too;
 * - an item picked up is gone from the other's room (the pouch's own sync
 *   gives it once: statesync_pc.c), a map object deleted (a brick broken) is
 *   deleted in the other.
 * NPCs the owner's game made at run time (a minion thrown, a spawner's) that
 * this game hasn't are made here as stand-ins of the same model, following the
 * owner's like the rest, and let go when the owner's are gone. In the owner's
 * game an enemy chases whichever player is nearest: while its own checks and
 * events run, Mario stands where that player is (portStateSyncNpcTarget*).
 * The per-type field lists (as sm64coopdx's sync objects declare theirs) are
 * in docs/STATE_SYNC.md. */
#include "driver/animdrv.h"
#include "driver/itemdrv.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "manager/evtmgr.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "port/statesync.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ItemWork* portItemGetWork(void);      /* itemdrv.c */
MapObjectWork* portMobjGetWork(void); /* mobjdrv.c */
/* evt_mobj.c: an object's look as the other game's */
s32 portMobjUsedState(const MapObjectEntry* mobj);
void portMobjShowUsed(MapObjectEntry* mobj);
s32 portMobjIsSwitch(const MapObjectEntry* mobj);
s32 portMobjPressBlueSwitch(MapObjectEntry* mobj);

#define MAX_NPCS 64
#define MAX_MOBJS 64
#define MAX_ITEMS 64
#define NPC_SLOTS 256
#define NPC_EVERY 3
#define MOBJ_EVERY 15
#define NPC_LATER 4 /* the owner's lists waiting to be played out (below, R.later) */

/* An NPC's synced fields: where, which way, its pose, alive and shown */
typedef struct NpcSync {
    char name[32];
    float x, y, z, dir;
    char anim[32];
    uint32_t status; /* 1 dead, 2 hidden */
    char model[32];  /* its model (a stand-in made from it where the follower hasn't the NPC) */
} NpcSync;

#define PUPPETS 16

/* A map object's: where, shown, its type's state */
typedef struct MobjSync {
    char name[16];
    float x, y, z;
    int32_t flags; /* MOBJ_FLAG_HIDDEN | MOBJ_FLAG_INVISIBLE */
    int32_t state;
} MobjSync;

typedef struct ListHead {
    char map[16];
    uint32_t count;
} ListHead;

/* what this game had last frame, to see what went */
typedef struct Seen {
    char name[32];
    int alive;
    int32_t id;
    float x, y, z;
} Seen;

typedef struct DeadMsg {
    char map[16];
    char name[32];
    int32_t mask; /* that map's deadInfo mask */
} DeadMsg;

typedef struct GoneMsg {
    char map[16];
    char name[32];
    int32_t id;
    float x, y, z;
} GoneMsg;

/* the room's digest (SS_MSG_ROOM, "the room's digest" below): the owner's room as it is now */
#define SUM_EVERY 150     /* frames between two of the owner's */
#define ITEM_BASE_FRAMES 30 /* field frames into a room: its set-up's named items placed (the room's own) */
#define SUM_KIND_MAX 64   /* entries of one kind at most (more: that kind isn't compared) */
#define SUM_MAX (3 * SUM_KIND_MAX)
#define SUM_IN_A_ROW 400  /* frames: two digests further apart than this aren't "in a row" */
#define CAND_MAX 64
#define GONE_REC 32
#define LOST_SAID 16

enum { SUM_NPC = 1, SUM_MOBJ, SUM_ITEM };

typedef struct SumEntry {
    uint32_t hash; /* its name's (an item's with its id) */
    uint8_t kind;  /* SUM_* */
    uint8_t state; /* a map object's: 1 used (a chest opened, a block emptied) */
    uint8_t pad[2];
} SumEntry;

typedef struct RoomSum {
    ListHead h;    /* the map; count: the entries after it */
    int32_t mask;  /* the map's defeats record (FieldBattleData.deadInfo) */
    uint16_t n[3]; /* the entries of each kind */
    uint16_t full; /* by bit (1 << (kind - 1)): a kind with more than SUM_KIND_MAX, not compared */
} RoomSum;

/* what differs, waiting for the next digest to say it again (then it's a drift) */
enum { D_ENEMY_DEAD = 1, D_OBJ_GONE, D_OBJ_USED, D_OBJ_TELL, D_OBJ_LOST, D_ITEM_GONE, D_ITEM_TELL,
       D_ITEM_LOST };
typedef struct Cand {
    uint32_t hash;
    uint8_t kind, what;
} Cand;

/* an object or a named item that went in this game, in this room: its word, to tell again */
typedef struct GoneRec {
    uint8_t kind; /* SUM_MOBJ, SUM_ITEM; 0 free */
    uint8_t told; /* told again this many times */
    uint32_t hash;
    GoneMsg m;
} GoneRec;

static struct {
    char map[16];
    int following; /* this game follows the owner's NPCs now */
    uint8_t frozen[NPC_SLOTS]; /* NPCs frozen to follow, by slot, and the flags to give back */
    int32_t savedFlags[NPC_SLOTS];
    Seen npcSeen[NPC_SLOTS];
    Seen mobjSeen[MAX_MOBJS];
    int mobjSeenN;
    Seen itemSeen[MAX_ITEMS];
    int itemSeenN;
    int32_t deadMask0;
    char deadMap0[16];
    /* the owner's latest */
    NpcSync npc[MAX_NPCS];
    int npcN;
    unsigned npcAt; /* our frame when it came */
    MobjSync mobj[MAX_MOBJS];
    int mobjN, mobjNew;
    unsigned frame, applied;
    char puppet[PUPPETS][32]; /* the stand-ins made here for the owner's NPCs made at run time */
    int owner;                /* this game owns the room, with another player in it (the chase) */
    /* the room's digest */
    uint32_t known[NPC_SLOTS]; /* the name's hash of the NPC in that slot followed as one of the owner's (0 none) */
    /* the room's own named items (sumCheck): here ITEM_BASE_FRAMES field frames into the room (or at the first
       digest, if one comes first), or the owner's */
    uint32_t itemBase[MAX_ITEMS];
    int itemBaseN, itemBaseSet, itemBaseFrames;
    int reload; /* a map change seen: the room is a new one even when its name is the same (ssRoomFrame) */
    RoomSum sum;               /* the owner's latest */
    SumEntry sumE[SUM_MAX];
    int sumNew;
    Cand prev[CAND_MAX]; /* what differed at the last one */
    int prevN, prevOk;
    int32_t prevTheirs, prevMine; /* the defeats recorded only there, only here */
    unsigned prevAt;
    GoneRec gone[GONE_REC];
    int goneNext;
    uint32_t lostSaid[LOST_SAID];
    int lostSaidN;
    unsigned drifts, healed, unhealed; /* (the session's: kept from room to room) */
    /* a room on the relay: the owner's lists that came together in one of its batches (it makes one every 3
       frames; a batch may hold two or more), played out one per NPC_EVERY frames as they were made, so the
       NPCs move as smoothly as in the owner's game (taken at once, only the newest showed: a step a batch) */
    NpcSync later[NPC_LATER][MAX_NPCS];
    int laterN[NPC_LATER];
    int laterHead, laterCount;
} R;

/* the owner's: the lists it sent last (a room on the relay: one the same as the last isn't sent again for a
   while; by address nothing is counted, every one goes) */
static struct {
    uint8_t npcs[sizeof(ListHead) + MAX_NPCS * sizeof(NpcSync)];
    size_t npcsLen;
    unsigned npcsAt;
    uint8_t mobjs[sizeof(ListHead) + MAX_MOBJS * sizeof(MobjSync)];
    size_t mobjsLen;
    unsigned mobjsAt;
} sSent;

/* a list the same as the one before, sent again after this many frames at most (the follower lets a list go
   after 60 frames; a player newly linked or newly here has one by then) */
#define NPC_SAME_EVERY 20
#define MOBJ_SAME_EVERY 120

static void npcsLater(void);

/* the list in buf (len bytes) to send now: 1, or 0 when it's the one sent last and that went lately */
static int listChanged(uint8_t* last, size_t* lastLen, unsigned* lastAt, const uint8_t* buf, size_t len,
                       unsigned every) {
    if (ssNetRoom() && len == *lastLen && R.frame - *lastAt < every && !memcmp(last, buf, len)) {
        return 0;
    }
    memcpy(last, buf, len);
    *lastLen = len;
    *lastAt = R.frame;
    return 1;
}
static void goneFrame(void);

/* an NPC's name for the logs: the game's are Shift-JIS, shown as hex */
static const char* safe(const char* name) {
    static char out[4][72];
    static int k;
    char* o = out[k = (k + 1) % 4];
    size_t n = 0;
    for (; *name && n + 3 < sizeof(out[0]); name++) {
        const unsigned char c = (unsigned char)*name;
        if (c >= 0x20 && c < 0x7F) {
            o[n++] = (char)c;
        } else {
            n += (size_t)snprintf(o + n, sizeof(out[0]) - n, "%02X", c);
        }
    }
    o[n] = 0;
    return o;
}

static int sameMap(const char* m) {
    return gp != NULL && !strncmp(m, gp->currentMapName, 16);
}

/* a received number fit to use: finite and within any map (a huge one would spin the game's angle loops) */
static int sane(float v) {
    return isfinite(v) && fabsf(v) < 1e6f;
}

/* a received model name fit to load: a file of a/ by its name alone (no path, not a texture's "name-") */
static int modelNameOk(const char* m) {
    const size_t n = strlen(m);
    return n > 0 && strpbrk(m, "/\\:") == NULL && strstr(m, "..") == NULL && m[n - 1] != '-';
}

static NpcWork* npcs(void) {
    return gp != NULL && !gp->inBattle ? npcGetWorkPtr() : NULL;
}

/* ---- following the owner's NPCs ---- */

static void freeze(int slot, NpcEntry* npc) {
    if (R.frozen[slot]) {
        return;
    }
    R.frozen[slot] = 1;
    R.savedFlags[slot] = npc->flags & (NPC_FLAG_MOVE_ANIM | NPC_FLAG_NO_FIND_EVENTS);
    npc->flags &= ~NPC_FLAG_MOVE_ANIM;
    npc->flags |= NPC_FLAG_NO_FIND_EVENTS;
    npc->jumpFlags |= NPC_STATUS_FROZEN;
    if (npc->regularEvtId) {
        evtStopID(npc->regularEvtId);
    }
}

int portStateSyncNpcFollowed(const NpcEntry* npc) {
    NpcWork* w = npcs();
    int slot;
    if (w == NULL || npc == NULL || npc < w->entries || npc >= w->entries + w->total) {
        return 0;
    }
    slot = (int)(npc - w->entries);
    return slot < NPC_SLOTS && R.frozen[slot] && R.following;
}

static void thaw(int slot, NpcEntry* npc) {
    if (!R.frozen[slot]) {
        return;
    }
    R.frozen[slot] = 0;
    if ((npc->flags & NPC_FLAG_USED) && !(npc->jumpFlags & NPC_STATUS_DEAD)) {
        npc->flags = (npc->flags & ~(NPC_FLAG_MOVE_ANIM | NPC_FLAG_NO_FIND_EVENTS)) | R.savedFlags[slot];
        npc->jumpFlags &= ~NPC_STATUS_FROZEN;
        if (npc->regularEvtId) {
            evtStartID(npc->regularEvtId);
        }
    }
}

/* A slot's follow state belongs to the NPC that was in it. An NPC gone (defeated, deleted) leaves its slot free
   and npcEntry hands it to the next one made: a stand-in for the owner's same enemy come back. Its thaw (its
   defeat told by the other game) then gave it the gone NPC's walk-animation flag, and a stand-in has no tribe:
   npcMain read tribe->runStartSpeed through NULL (both guests crashed at once, the online soak's seed 303). */
static void forgetSlot(const NpcEntry* npc) {
    NpcWork* w = npcs();
    int slot;
    if (w == NULL || npc < w->entries || npc >= w->entries + w->total) {
        return;
    }
    slot = (int)(npc - w->entries);
    if (slot < NPC_SLOTS) {
        R.frozen[slot] = 0;
        R.savedFlags[slot] = 0;
    }
}

/* each field frame: the slots whose NPC is gone forget it (forgetSlot) */
static void forgetFreedSlots(void) {
    NpcWork* w = npcs();
    int i;
    for (i = 0; w != NULL && i < w->total && i < NPC_SLOTS; i++) {
        if (R.frozen[i] && !(w->entries[i].flags & NPC_FLAG_USED)) {
            R.frozen[i] = 0;
            R.savedFlags[i] = 0;
        }
    }
}

/* ---- stand-ins for the owner's NPCs made at run time ---- */

static int isPuppet(const char* name) {
    int i;
    for (i = 0; i < PUPPETS; i++) {
        if (R.puppet[i][0] && !strcmp(R.puppet[i], name)) {
            return i;
        }
    }
    return -1;
}

static void puppetGone(int i) {
    NpcEntry* npc = npcNameToPtr_NoAssert(R.puppet[i]);
    if (npc != NULL) {
        npcDelete(npc);
    }
    ssLog("room: stand-in %s let go", safe(R.puppet[i]));
    R.puppet[i][0] = '\0';
}

static void puppetsAllGone(void) {
    int i;
    for (i = 0; i < PUPPETS; i++) {
        if (R.puppet[i][0]) {
            puppetGone(i);
        }
    }
}

/* the field's NPC slots not in use: npcEntry takes the first free one and has no check of its own (the
   game never fills them), so a stand-in is only made with PUPPET_SPARE of them left for the room's own */
#define PUPPET_SPARE 8
static int freeNpcSlots(void) {
    NpcWork* w = npcs();
    int i, n = 0;
    if (w == NULL) {
        return 0;
    }
    for (i = 0; i < w->total; i++) {
        n += !(w->entries[i].flags & NPC_FLAG_USED);
    }
    return n;
}

static void puppets(void) {
    int i, k;
    /* the owner's no longer: let go */
    for (i = 0; i < PUPPETS; i++) {
        int there = 0;
        if (!R.puppet[i][0]) {
            continue;
        }
        for (k = 0; k < R.npcN && !there; k++) {
            there = !strcmp(R.npc[k].name, R.puppet[i]) && !(R.npc[k].status & 1);
        }
        if (!there) {
            puppetGone(i);
        }
    }
    /* the owner's not here: a stand-in (once its model has loaded) */
    for (k = 0; k < R.npcN; k++) {
        const NpcSync* s = &R.npc[k];
        NpcEntry* npc;
        if ((s->status & 1) || s->model[0] == '\0' || npcNameToPtr_NoAssert(s->name) != NULL) {
            continue;
        }
        for (i = 0; i < PUPPETS && R.puppet[i][0]; i++) {
        }
        if (i == PUPPETS || freeNpcSlots() < PUPPET_SPARE || !animGroupBaseAsync(s->model, 0, NULL)) {
            continue; /* (no room for more, or loading: the next frame) */
        }
        npcEntry(s->name, s->model);
        npc = npcNameToPtr_NoAssert(s->name);
        if (npc == NULL) {
            continue;
        }
        forgetSlot(npc); /* (a slot an NPC frozen to follow had: none of its state is this stand-in's) */
        npc->flags |= NPC_FLAG_INTANGIBLE | NPC_FLAG_NO_FIND_EVENTS;
        npc->position.x = s->x;
        npc->position.y = s->y;
        npc->position.z = s->z;
        snprintf(R.puppet[i], sizeof(R.puppet[i]), "%s", s->name);
        ssLog("room: the owner's %s (made there) stood in here as %s", safe(s->name), s->model);
    }
}

static void thawAll(void) {
    NpcWork* w = npcs();
    int i;
    puppetsAllGone();
    if (w != NULL) {
        for (i = 0; i < w->total && i < NPC_SLOTS; i++) {
            thaw(i, &w->entries[i]);
        }
    }
    memset(R.frozen, 0, sizeof(R.frozen));
    if (R.following) {
        ssLog("room: this game runs %s's NPCs again", R.map);
    }
    R.following = 0;
}

static uint32_t nameHash(const char* s, int32_t extra); /* (the room's digest, below) */

static void follow(void) {
    NpcWork* w = npcs();
    int i, k, n = 0;
    if (w == NULL || (!marioChkKey() && !ssScenePulled() && !portStateSyncCoopGuest())) {
        return; /* (this player in an event of its own, talking or a scene: the NPCs are its own meanwhile;
                   held for the other's scene, they're the other's. A co-op guest's Mario's keys are always off
                   (Mario is the host's) and its talks and scenes are the host's: it always follows, so the
                   enemies its own character walks to, touches and hammers are where the host's game has them) */
    }
    for (i = 0; i < w->total && i < NPC_SLOTS; i++) {
        NpcEntry* npc = &w->entries[i];
        const NpcSync* s = NULL;
        if (!(npc->flags & NPC_FLAG_USED) || !(npc->flags & NPC_FLAG_ACTIVE) || (npc->jumpFlags & NPC_STATUS_DEAD)) {
            continue; /* (dying: its own dead event) */
        }
        for (k = 0; k < R.npcN; k++) {
            if (!strcmp(R.npc[k].name, npc->description)) {
                s = &R.npc[k];
                break;
            }
        }
        if (s == NULL || (s->status & 1)) {
            continue; /* not the owner's (made here only), or dead there (SS_R_NPC_DEAD does it) */
        }
        freeze(i, npc);
        R.known[i] = nameHash(npc->description, 0); /* (one of the owner's: the room's digest) */
        if (npc->regularEvtId) {
            EventEntry* e = evtGetPtrID(npc->regularEvtId);
            if (e != NULL && !(e->flags & EVENT_STOPPED)) {
                evtStopID(npc->regularEvtId); /* (an interrupt's new event) */
            }
        }
        npc->prevPosition = npc->position;
        npc->position.x = s->x;
        npc->position.y = s->y;
        npc->position.z = s->z;
        npc->direction = s->dir;
        if (s->anim[0] && strcmp(npc->currentAnim, s->anim) != 0) {
            snprintf(npc->currentAnim, sizeof(npc->currentAnim), "%s", s->anim);
            if (npc->poseId >= 0) {
                animPoseSetAnim(npc->poseId, npc->currentAnim, 0);
            }
        }
        if (s->status & 2) {
            npc->flags |= NPC_FLAG_HIDDEN;
        } else {
            npc->flags &= ~NPC_FLAG_HIDDEN;
        }
        n++;
        R.applied++;
    }
    if (!R.following) {
        ssLog("room: following the other game's NPCs in %s (%d of theirs, %d here)", R.map, R.npcN, n);
    }
    R.following = 1;
}

static void sendNpcs(void) {
    NpcWork* w = npcs();
    static uint8_t buf[sizeof(ListHead) + MAX_NPCS * sizeof(NpcSync)];
    ListHead* h = (ListHead*)buf;
    NpcSync* out = (NpcSync*)(buf + sizeof(ListHead));
    int i, n = 0;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->total && n < MAX_NPCS; i++) {
        const NpcEntry* npc = &w->entries[i];
        if (!(npc->flags & NPC_FLAG_USED) || !(npc->flags & NPC_FLAG_ACTIVE)) {
            continue;
        }
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", npc->description);
        out[n].x = npc->position.x;
        out[n].y = npc->position.y;
        out[n].z = npc->position.z;
        out[n].dir = npc->direction;
        snprintf(out[n].anim, sizeof(out[n].anim), "%s", npc->currentAnim);
        out[n].status = ((npc->jumpFlags & NPC_STATUS_DEAD) ? 1u : 0u) | ((npc->flags & NPC_FLAG_HIDDEN) ? 2u : 0u);
        if (npc->poseId >= 0 && animPoseGetAnimBaseDataPtr(npc->poseId) != NULL) {
            snprintf(out[n].model, sizeof(out[n].model), "%s", animPoseGetAnimBaseDataPtr(npc->poseId)->fileName);
        }
        n++;
    }
    memset(h, 0, sizeof(*h));
    snprintf(h->map, sizeof(h->map), "%s", gp->currentMapName);
    h->count = (uint32_t)n;
    if (listChanged(sSent.npcs, &sSent.npcsLen, &sSent.npcsAt, buf, sizeof(ListHead) + (size_t)n * sizeof(NpcSync),
                    NPC_SAME_EVERY)) {
        ssNetSend(SS_MSG_NPCS, buf, sizeof(ListHead) + (size_t)n * sizeof(NpcSync));
    }
    {
        static char sListed[16];
        if (strncmp(sListed, gp->currentMapName, 16) != 0 || R.frame % 600 == 0) {
            snprintf(sListed, sizeof(sListed), "%s", gp->currentMapName);
            for (i = 0; i < n; i++) {
                const NpcEntry* npc = npcNameToPtr_NoAssert(out[i].name);
                ssLog("room: owner's NPC %s%s at (%.1f, %.1f, %.1f) %s", safe(out[i].name),
                      npc != NULL && (npc->flags & NPC_FLAG_ENEMY) ? " (enemy)" : "", out[i].x, out[i].y, out[i].z,
                      out[i].anim);
            }
        }
    }
}

static void sendMobjs(void) {
    MapObjectWork* w = portMobjGetWork();
    static uint8_t buf[sizeof(ListHead) + MAX_MOBJS * sizeof(MobjSync)];
    ListHead* h = (ListHead*)buf;
    MobjSync* out = (MobjSync*)(buf + sizeof(ListHead));
    int i, n = 0;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->count && n < MAX_MOBJS; i++) {
        const MapObjectEntry* m = &w->entries[i];
        if (!(m->flags & MOBJ_FLAG_USED)) {
            continue;
        }
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", m->name);
        out[n].x = m->position.x;
        out[n].y = m->position.y;
        out[n].z = m->position.z;
        out[n].flags = m->flags & (MOBJ_FLAG_HIDDEN | MOBJ_FLAG_INVISIBLE);
        out[n].state = m->state;
        n++;
    }
    memset(h, 0, sizeof(*h));
    snprintf(h->map, sizeof(h->map), "%s", gp->currentMapName);
    h->count = (uint32_t)n;
    if (listChanged(sSent.mobjs, &sSent.mobjsLen, &sSent.mobjsAt, buf, sizeof(ListHead) + (size_t)n * sizeof(MobjSync),
                    MOBJ_SAME_EVERY)) {
        ssNetSend(SS_MSG_MOBJS, buf, sizeof(ListHead) + (size_t)n * sizeof(MobjSync));
    }
}

/* the switches shown pressed for the owner's (names, this room) */
static char sPressed[8][16];

static int pressedShown(const char* name) {
    int i;
    for (i = 0; i < 8; i++) {
        if (sPressed[i][0] && !strcmp(sPressed[i], name)) {
            return 1;
        }
    }
    return 0;
}

static void setPressedShown(const char* name, int on) {
    int i;
    for (i = 0; i < 8; i++) {
        if (on ? sPressed[i][0] == 0 : !strcmp(sPressed[i], name)) {
            if (on) {
                snprintf(sPressed[i], sizeof(sPressed[i]), "%s", name);
            } else {
                sPressed[i][0] = 0;
            }
            return;
        }
    }
}

/* the owner's objects: where they are and whether they're shown (falling blocks, hidden ones found),
   chests and blocks used, switches pressed */
static void followMobjs(void) {
    MapObjectWork* w = portMobjGetWork();
    int i, k;
    if (w == NULL || !R.mobjNew) {
        return;
    }
    R.mobjNew = 0;
    for (i = 0; i < w->count; i++) {
        MapObjectEntry* m = &w->entries[i];
        if (!(m->flags & MOBJ_FLAG_USED)) {
            continue;
        }
        for (k = 0; k < R.mobjN; k++) {
            const MobjSync* s = &R.mobj[k];
            if (!strcmp(s->name, m->name)) {
                const float dx = s->x - m->position.x, dy = s->y - m->position.y, dz = s->z - m->position.z;
                if (dx * dx + dy * dy + dz * dz > 4.0f) {
                    mobjSetPosition(m->name, s->x, s->y, s->z);
                }
                if ((m->flags & (MOBJ_FLAG_HIDDEN | MOBJ_FLAG_INVISIBLE)) != s->flags) {
                    ssLog("room: object %s shown as the owner's (%x -> %x)", m->name,
                          m->flags & (MOBJ_FLAG_HIDDEN | MOBJ_FLAG_INVISIBLE), s->flags);
                    m->flags = (m->flags & ~(MOBJ_FLAG_HIDDEN | MOBJ_FLAG_INVISIBLE)) | s->flags;
                }
                /* a chest opened, a ? or badge block emptied there: its used look here (the item and the
                   flag come as their progress) */
                {
                    const s32 used = portMobjUsedState(m);
                    if (used >= 0 && s->state == used && m->state != used && m->state == 0) {
                        portMobjShowUsed(m);
                        ssLog("room: object %s used by the other player: shown used here (%s)", m->name, m->animName);
                    }
                }
                /* a blue switch pressed there: pressed here too, its event and all (a door, a pipe) */
                if (s->state != 0 && s->state != 99 && portMobjPressBlueSwitch(m)) {
                    setPressedShown(m->name, 0);
                    ssLog("room: blue switch %s pressed by the other player: pressed here too", m->name);
                    break;
                }
                /* a switch pressed there: down here too while it is, up again after */
                if (portMobjIsSwitch(m) && m->state == 0 && m->poseId >= 0) {
                    const int down = s->state != 0 && s->state != 99;
                    if (down != pressedShown(m->name)) {
                        animPoseSetAnim(m->poseId, down ? "A_1" : "S_1", 1);
                        setPressedShown(m->name, down);
                        ssLog("room: switch %s %s as the other player's", m->name, down ? "pressed" : "up");
                    }
                }
                break;
            }
        }
    }
}

/* ---- the room's digest: its hashes, and what went here (to tell again) ---- */

static uint32_t nameHash(const char* s, int32_t extra) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 0x01000193u;
    }
    return (h ^ (uint32_t)extra) * 0x01000193u;
}

/* a name the port's own placed things carry, made in each game by its own count or its own player's things (map
   edits' "me<generation>_<n>", map_edits_pc.c; "pc_..."): not the room's to compare */
static int portMade(const char* name) {
    return !strncmp(name, "pc_", 3) || (name[0] == 'm' && name[1] == 'e' && name[2] >= '0' && name[2] <= '9');
}

/* a field item placed by name (one dropped at run time is named by each game's own counter, "_i<n>") */
static int namedItem(const ItemEntry* it) {
    return it->name[0] != '\0' && strncmp(it->name, "_i", 2) != 0 && !portMade(it->name);
}

/* a map object of the room's own (not the port's: portMade) */
static int roomMobj(const MapObjectEntry* m) {
    return (m->flags & MOBJ_FLAG_USED) && !portMade(m->name);
}

static uint32_t itemHash(const char* name, int32_t id) {
    return nameHash(name, id);
}

static void goneAdd(int kind, const GoneMsg* m) {
    const uint32_t h = kind == SUM_ITEM ? itemHash(m->name, m->id) : nameHash(m->name, 0);
    GoneRec* r = NULL;
    int i;
    for (i = 0; i < GONE_REC && r == NULL; i++) {
        if (R.gone[i].kind == kind && R.gone[i].hash == h) {
            r = &R.gone[i];
        }
    }
    if (r == NULL) {
        r = &R.gone[R.goneNext];
        R.goneNext = (R.goneNext + 1) % GONE_REC;
    }
    r->kind = (uint8_t)kind;
    r->told = 0;
    r->hash = h;
    r->m = *m;
}

static GoneRec* goneFind(int kind, uint32_t h) {
    int i;
    for (i = 0; i < GONE_REC; i++) {
        if (R.gone[i].kind == kind && R.gone[i].hash == h) {
            return &R.gone[i];
        }
    }
    return NULL;
}

/* (here again: made again by the room's events, so it didn't go for good) */
static void goneDrop(int kind, uint32_t h) {
    GoneRec* r = goneFind(kind, h);
    if (r != NULL) {
        r->kind = 0;
    }
}

/* ---- what went here: defeats, pickups, objects, told to the other game ---- */

static void watchNpcs(void) {
    NpcWork* w = npcs();
    FieldBattleData* fb = fbatGetPointer();
    int i;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->total && i < NPC_SLOTS; i++) {
        const NpcEntry* npc = &w->entries[i];
        const int alive = (npc->flags & NPC_FLAG_USED) && !(npc->jumpFlags & NPC_STATUS_DEAD);
        Seen* s = &R.npcSeen[i];
        if ((npc->flags & NPC_FLAG_USED) && !strcmp(s->name, npc->description) && s->alive && !alive) {
            DeadMsg m;
            memset(&m, 0, sizeof(m));
            snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
            snprintf(m.name, sizeof(m.name), "%s", npc->description);
            m.mask = fb != NULL ? fb->deadInfo[0].mask : 0;
            ssNetSendRel(SS_R_NPC_DEAD, &m, sizeof(m));
            ssLog("room: %s defeated here in %s: sent", safe(m.name), m.map);
        }
        snprintf(s->name, sizeof(s->name), "%s", (npc->flags & NPC_FLAG_USED) ? npc->description : "");
        s->alive = alive;
    }
    /* this map's defeats record (deadInfo[0]), when it changes */
    if (fb != NULL) {
        if (!strncmp(fb->deadInfo[0].mapName, R.deadMap0, 16) && fb->deadInfo[0].mask != R.deadMask0 &&
            fb->deadInfo[0].mask != 0) {
            DeadMsg m;
            memset(&m, 0, sizeof(m));
            snprintf(m.map, sizeof(m.map), "%s", fb->deadInfo[0].mapName);
            m.mask = fb->deadInfo[0].mask;
            ssNetSendRel(SS_R_NPC_DEAD, &m, sizeof(m));
        }
        R.deadMask0 = fb->deadInfo[0].mask;
        memcpy(R.deadMap0, fb->deadInfo[0].mapName, 16);
    }
}

static void watchItems(void) {
    ItemWork* w = portItemGetWork();
    Seen now[MAX_ITEMS];
    int n = 0, i, k;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->count && n < MAX_ITEMS; i++) {
        const ItemEntry* it = &w->entries[i];
        if (!(it->flags & ITEM_FLAG_USED)) {
            continue;
        }
        memset(&now[n], 0, sizeof(now[n]));
        snprintf(now[n].name, sizeof(now[n].name), "%s", it->name);
        now[n].id = it->itemId;
        now[n].x = it->position.x;
        now[n].y = it->position.y;
        now[n].z = it->position.z;
        now[n].alive = 1;
        if (namedItem(it)) {
            goneDrop(SUM_ITEM, itemHash(it->name, it->itemId));
        }
        n++;
    }
    for (k = 0; k < R.itemSeenN; k++) {
        int still = 0;
        for (i = 0; i < n; i++) {
            if (!strcmp(now[i].name, R.itemSeen[k].name) && now[i].id == R.itemSeen[k].id) {
                still = 1;
                break;
            }
        }
        if (!still && R.itemSeen[k].name[0]) {
            GoneMsg m;
            memset(&m, 0, sizeof(m));
            snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
            snprintf(m.name, sizeof(m.name), "%s", R.itemSeen[k].name);
            m.id = R.itemSeen[k].id;
            m.x = R.itemSeen[k].x;
            m.y = R.itemSeen[k].y;
            m.z = R.itemSeen[k].z;
            ssNetSendRel(SS_R_ITEM_GONE, &m, sizeof(m));
            goneAdd(SUM_ITEM, &m);
            ssLog("room: item %s (%d) gone here in %s: sent", m.name, m.id, m.map);
        }
    }
    memcpy(R.itemSeen, now, (size_t)n * sizeof(Seen));
    R.itemSeenN = n;
}

static void watchMobjs(void) {
    MapObjectWork* w = portMobjGetWork();
    Seen now[MAX_MOBJS];
    int n = 0, i, k;
    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->count && n < MAX_MOBJS; i++) {
        const MapObjectEntry* m = &w->entries[i];
        if (!(m->flags & MOBJ_FLAG_USED)) {
            continue;
        }
        memset(&now[n], 0, sizeof(now[n]));
        snprintf(now[n].name, sizeof(now[n].name), "%s", m->name);
        now[n].alive = 1;
        goneDrop(SUM_MOBJ, nameHash(m->name, 0));
        n++;
    }
    for (k = 0; k < R.mobjSeenN; k++) {
        int still = 0;
        for (i = 0; i < n; i++) {
            if (!strcmp(now[i].name, R.mobjSeen[k].name)) {
                still = 1;
                break;
            }
        }
        if (!still && R.mobjSeen[k].name[0]) {
            GoneMsg m;
            memset(&m, 0, sizeof(m));
            snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
            snprintf(m.name, sizeof(m.name), "%s", R.mobjSeen[k].name);
            ssNetSendRel(SS_R_MOBJ_GONE, &m, sizeof(m));
            goneAdd(SUM_MOBJ, &m);
            ssLog("room: object %s gone here in %s: sent", m.name, m.map);
        }
    }
    memcpy(R.mobjSeen, now, (size_t)n * sizeof(Seen));
    R.mobjSeenN = n;
}

/* ---- the room's digest: drift between the owner's room and this game's, found and healed ----
 * Each room's news crosses once, as it happens (a defeat, an item taken, an object gone: above), and the
 * NPCs' and objects' places and looks stream all the time. A word that never took (lost with a session, come
 * before its room was made here, or this game's own events gone another way) would leave the rooms apart
 * for good. So every SUM_EVERY frames the owner sends those in its room what its room holds now
 * (SS_MSG_ROOM, unreliable, the latest wins): the room's defeats record (deadInfo's mask for the map) and a
 * hash per enemy alive, per map object (and whether it is used: a chest opened, a block emptied) and per
 * field item placed by name (one dropped at run time is named by each game's own counter: not compared).
 * Each game following the room compares it with its own room. What differs at two digests in a row (a word
 * on its way comes within a round trip) is a drift: logged "DRIFT room" with what, and healed here at once,
 * the owner's room being the room:
 * - an enemy alive here the owner's room hasn't: one followed here as the owner's is defeated here (its
 *   defeat's word lost; one never in the owner's list is left: a port's own, of this player's alone);
 * - a defeat recorded there and not here: recorded here and its enemy defeated; one recorded here and not
 *   there: told again (SS_R_NPC_DEAD with the record), so the owner's game records it and defeats it too;
 * - an object or a named item here the owner's room hasn't: gone here too; one the owner's room has that
 *   went here (this game's word lost): told again; one that never was here can't be made (logged once);
 * - an object used there and not here: shown used. */

static int32_t recordDead(const char* map, int32_t mask);

static int enemyAlive(const NpcEntry* npc) {
    const int32_t want = NPC_FLAG_USED | NPC_FLAG_ACTIVE | NPC_FLAG_ENEMY;
    return (npc->flags & want) == want && !(npc->jumpFlags & NPC_STATUS_DEAD);
}

static int mobjUsed(const MapObjectEntry* m) {
    const s32 used = portMobjUsedState(m);
    return used >= 0 && m->state == used;
}

static int32_t maskOf(const char* map) {
    FieldBattleData* fb = fbatGetPointer();
    int i;
    for (i = 0; fb != NULL && i < 64; i++) {
        if (!strncmp(fb->deadInfo[i].mapName, map, 16)) {
            return fb->deadInfo[i].mask;
        }
    }
    return 0;
}

/* (deadInfo[0], the map this game is in: its record as this game now has it, never sent back as a change) */
static void deadMaskSeen(void) {
    FieldBattleData* fb = fbatGetPointer();
    if (fb != NULL && !strncmp(fb->deadInfo[0].mapName, R.deadMap0, 16)) {
        R.deadMask0 = fb->deadInfo[0].mask;
    }
}

static void sumAdd(RoomSum* s, SumEntry* e, int* k, int kind, uint32_t h, int state) {
    if (s->n[kind - 1] >= SUM_KIND_MAX) {
        s->full |= (uint16_t)(1u << (kind - 1));
        return;
    }
    memset(&e[*k], 0, sizeof(e[*k]));
    e[*k].hash = h;
    e[*k].kind = (uint8_t)kind;
    e[*k].state = (uint8_t)state;
    (*k)++;
    s->n[kind - 1]++;
}

/* this game's room now */
static int sumBuild(RoomSum* s, SumEntry* e) {
    NpcWork* w = npcs();
    MapObjectWork* mw = portMobjGetWork();
    ItemWork* iw = portItemGetWork();
    int i, k = 0;
    memset(s, 0, sizeof(*s));
    snprintf(s->h.map, sizeof(s->h.map), "%s", gp->currentMapName);
    s->mask = maskOf(gp->currentMapName);
    for (i = 0; w != NULL && i < w->total; i++) {
        const NpcEntry* npc = &w->entries[i];
        if (enemyAlive(npc) && isPuppet(npc->description) < 0) {
            sumAdd(s, e, &k, SUM_NPC, nameHash(npc->description, 0), 0);
        }
    }
    for (i = 0; mw != NULL && i < mw->count; i++) {
        const MapObjectEntry* m = &mw->entries[i];
        if (roomMobj(m)) {
            sumAdd(s, e, &k, SUM_MOBJ, nameHash(m->name, 0), mobjUsed(m));
        }
    }
    for (i = 0; iw != NULL && i < iw->count; i++) {
        const ItemEntry* it = &iw->entries[i];
        if ((it->flags & ITEM_FLAG_USED) && namedItem(it)) {
            sumAdd(s, e, &k, SUM_ITEM, itemHash(it->name, it->itemId), 0);
        }
    }
    s->h.count = (uint32_t)k;
    return k;
}

typedef struct SumBuf {
    RoomSum s;
    SumEntry e[SUM_MAX];
} SumBuf;

static void sendSum(void) {
    static SumBuf b;
    const int k = sumBuild(&b.s, b.e);
    ssNetSend(SS_MSG_ROOM, &b, sizeof(RoomSum) + (size_t)k * sizeof(SumEntry));
}

static int sumFind(const SumEntry* e, int n, int kind, uint32_t h) {
    int i;
    for (i = 0; i < n; i++) {
        if (e[i].kind == kind && e[i].hash == h) {
            return i;
        }
    }
    return -1;
}

/* the owner's latest NPC list has it, alive */
static int ownerListsAlive(const char* name) {
    int k;
    for (k = 0; k < R.npcN; k++) {
        if (!strcmp(R.npc[k].name, name)) {
            return !(R.npc[k].status & 1);
        }
    }
    return 0;
}

static int ownerListFresh(void) {
    return R.npcAt != 0 && R.frame - R.npcAt < 60;
}

/* the enemies here whose defeats the record has (by bit: their setup index) defeated here, but one the owner's
   game (followed here) still has alive; how many */
static int killByMask(int32_t bits, const char* why) {
    NpcWork* w = npcs();
    int i, n = 0;
    for (i = 0; w != NULL && bits != 0 && i < w->total && i < NPC_SLOTS; i++) {
        NpcEntry* npc = &w->entries[i];
        if (!enemyAlive(npc) || !(npc->flags & NPC_FLAG_RECORD_DEFEAT) || npc->setupIndex >= 32 ||
            !((uint32_t)bits & (1u << npc->setupIndex))) {
            continue;
        }
        if (R.following && ownerListFresh() && ownerListsAlive(npc->description)) {
            continue; /* (alive in the owner's room: its list says so) */
        }
        thaw(i, npc);
        if (R.following) {
            R.npcSeen[i].alive = 0; /* (not sent back) */
        }
        /* (the room's owner, or alone in it: told as any defeat here is (watchNpcs), so a game following this
           room, which left the enemy to this game's list, defeats it at once by name, not at the room's
           digest) */
        npcGroupDead(npc, 0);
        n++;
        ssLog("room: %s defeated here too (%s)", safe(npc->description), why);
    }
    deadMaskSeen();
    return n;
}

static void addCand(Cand* c, int* n, uint32_t h, int kind, int what) {
    if (*n < CAND_MAX) {
        c[*n].hash = h;
        c[*n].kind = (uint8_t)kind;
        c[*n].what = (uint8_t)what;
        (*n)++;
    }
}

static int wasCand(const Cand* c) {
    int i;
    for (i = 0; i < R.prevN; i++) {
        if (R.prev[i].hash == c->hash && R.prev[i].kind == c->kind && R.prev[i].what == c->what) {
            return 1;
        }
    }
    return 0;
}

/* said already (once per room) */
static int lostSaid(uint32_t h) {
    int i;
    for (i = 0; i < R.lostSaidN; i++) {
        if (R.lostSaid[i] == h) {
            return 1;
        }
    }
    if (R.lostSaidN < LOST_SAID) {
        R.lostSaid[R.lostSaidN++] = h;
    }
    return 0;
}

/* told twice already: the owner's game won't take it (said once as can't be healed) */
static int toldOut(const Cand* c) {
    const GoneRec* r;
    if (c->what != D_OBJ_TELL && c->what != D_ITEM_TELL) {
        return 0;
    }
    r = goneFind(c->kind, c->hash);
    return r == NULL || r->told >= 2;
}

static void objGoneHere(MapObjectEntry* m, const char* why) {
    GoneMsg g;
    int k;
    memset(&g, 0, sizeof(g));
    snprintf(g.map, sizeof(g.map), "%s", gp->currentMapName);
    snprintf(g.name, sizeof(g.name), "%s", m->name);
    for (k = 0; k < R.mobjSeenN; k++) {
        if (!strcmp(R.mobjSeen[k].name, g.name)) {
            R.mobjSeen[k].name[0] = '\0'; /* (not sent back) */
        }
    }
    goneAdd(SUM_MOBJ, &g);
    if (portMobjPressBlueSwitch(m)) {
        ssLog("room: blue switch %s pressed here too (%s)", g.name, why);
        return;
    }
    mobjDelete(g.name);
    ssLog("room: object %s gone here too (%s)", g.name, why);
}

static void itemGoneHere(ItemEntry* it, const char* why) {
    GoneMsg g;
    int k;
    memset(&g, 0, sizeof(g));
    snprintf(g.map, sizeof(g.map), "%s", gp->currentMapName);
    snprintf(g.name, sizeof(g.name), "%s", it->name);
    g.id = it->itemId;
    g.x = it->position.x;
    g.y = it->position.y;
    g.z = it->position.z;
    for (k = 0; k < R.itemSeenN; k++) {
        if (!strcmp(R.itemSeen[k].name, g.name)) {
            R.itemSeen[k].name[0] = '\0'; /* (not sent back) */
        }
    }
    goneAdd(SUM_ITEM, &g);
    itemDelete(g.name);
    ssLog("room: item %s (%d) gone here too (%s)", g.name, (int)g.id, why);
}

/* a drift found at two digests in a row, healed here: 1 (0: it can't be) */
static int heal(const Cand* c) {
    NpcWork* w = npcs();
    MapObjectWork* mw = portMobjGetWork();
    ItemWork* iw = portItemGetWork();
    int i;
    switch (c->what) {
        case D_ENEMY_DEAD:
            for (i = 0; w != NULL && i < w->total && i < NPC_SLOTS; i++) {
                NpcEntry* npc = &w->entries[i];
                if (!enemyAlive(npc) || nameHash(npc->description, 0) != c->hash) {
                    continue;
                }
                thaw(i, npc);
                R.npcSeen[i].alive = 0; /* (not sent back: it's gone there already) */
                npcGroupDead(npc, 0);
                deadMaskSeen();
                ssLog("room: %s defeated here too (gone in the owner's room)", safe(npc->description));
                return 1;
            }
            return 1; /* (gone meanwhile) */
        case D_OBJ_GONE:
        case D_OBJ_USED:
            for (i = 0; mw != NULL && i < mw->count; i++) {
                MapObjectEntry* m = &mw->entries[i];
                if (!roomMobj(m) || nameHash(m->name, 0) != c->hash) {
                    continue;
                }
                if (c->what == D_OBJ_GONE) {
                    objGoneHere(m, "not in the owner's room");
                } else if (!mobjUsed(m) && m->state == 0) {
                    portMobjShowUsed(m);
                    ssLog("room: object %s shown used here too (used in the owner's room)", m->name);
                }
                return 1;
            }
            return 1;
        case D_ITEM_GONE:
            for (i = 0; iw != NULL && i < iw->count; i++) {
                ItemEntry* it = &iw->entries[i];
                if ((it->flags & ITEM_FLAG_USED) && namedItem(it) && itemHash(it->name, it->itemId) == c->hash) {
                    itemGoneHere(it, "not in the owner's room");
                    return 1;
                }
            }
            return 1;
        case D_OBJ_TELL:
        case D_ITEM_TELL: {
            GoneRec* r = goneFind(c->kind, c->hash);
            if (r == NULL) {
                return 0;
            }
            r->told++;
            ssNetSendRel(c->kind == SUM_MOBJ ? SS_R_MOBJ_GONE : SS_R_ITEM_GONE, &r->m, sizeof(r->m));
            ssLog("room: %s %s went here: told the owner's game again", c->kind == SUM_MOBJ ? "object" : "item",
                  r->m.name);
            return 1;
        }
        default:
            return 0;
    }
}

static const char* candWords(int what) {
    switch (what) {
        case D_ENEMY_DEAD:
            return "an enemy alive here is gone there";
        case D_OBJ_GONE:
            return "an object here is gone there";
        case D_OBJ_USED:
            return "an object used there isn't here";
        case D_OBJ_TELL:
            return "an object gone here is still there";
        case D_ITEM_GONE:
            return "an item here is gone there";
        case D_ITEM_TELL:
            return "an item gone here is still there";
        default:
            return "?";
    }
}

static int itemBaseHas(uint32_t h) {
    int i;
    for (i = 0; i < R.itemBaseN; i++) {
        if (R.itemBase[i] == h) {
            return 1;
        }
    }
    return 0;
}

static void itemBaseAdd(uint32_t h) {
    if (R.itemBaseN < MAX_ITEMS && !itemBaseHas(h)) {
        R.itemBase[R.itemBaseN++] = h;
    }
}

/* the owner's digest against this game's room */
static void sumCheck(void) {
    static SumBuf mine;
    Cand now[CAND_MAX];
    int nowN = 0, i, acted = 0, healedNow = 0;
    const int theirN = (int)R.sum.h.count;
    NpcWork* w = npcs();
    MapObjectWork* mw = portMobjGetWork();
    ItemWork* iw = portItemGetWork();
    int32_t theirs, ours;
    char line[240];
    size_t ln = 0;
    R.sumNew = 0;
    if (gp == NULL || ssBattleBusy() || (!marioChkKey() && !ssScenePulled() && !portStateSyncCoopGuest())) {
        R.prevOk = 0; /* (this player's own talk or scene: the room's NPCs its own meanwhile) */
        return;
    }
    sumBuild(&mine.s, mine.e);
    /* enemies alive here the owner's room hasn't (its NPC list fresh: not alive there either) */
    if (!(R.sum.full & 1) && !(mine.s.full & 1) && ownerListFresh()) {
        for (i = 0; w != NULL && i < w->total && i < NPC_SLOTS; i++) {
            const NpcEntry* npc = &w->entries[i];
            uint32_t h;
            if (!enemyAlive(npc) || isPuppet(npc->description) >= 0) {
                continue;
            }
            h = nameHash(npc->description, 0);
            /* (only one followed here as the owner's: a port's own enemy of this player's alone is its own) */
            if (R.known[i] == h && sumFind(R.sumE, theirN, SUM_NPC, h) < 0 && !ownerListsAlive(npc->description)) {
                addCand(now, &nowN, h, SUM_NPC, D_ENEMY_DEAD);
            }
        }
    }
    /* the map objects, both ways */
    if (!(R.sum.full & 2) && !(mine.s.full & 2)) {
        for (i = 0; mw != NULL && i < mw->count; i++) {
            const MapObjectEntry* m = &mw->entries[i];
            uint32_t h;
            int at;
            if (!roomMobj(m)) {
                continue;
            }
            h = nameHash(m->name, 0);
            at = sumFind(R.sumE, theirN, SUM_MOBJ, h);
            if (at < 0) {
                addCand(now, &nowN, h, SUM_MOBJ, D_OBJ_GONE);
            } else if (R.sumE[at].state && !mobjUsed(m) && m->state == 0 && portMobjUsedState(m) >= 0) {
                addCand(now, &nowN, h, SUM_MOBJ, D_OBJ_USED);
            }
        }
        for (i = 0; i < theirN; i++) {
            if (R.sumE[i].kind == SUM_MOBJ && sumFind(mine.e, (int)mine.s.h.count, SUM_MOBJ, R.sumE[i].hash) < 0) {
                addCand(now, &nowN, R.sumE[i].hash, SUM_MOBJ,
                        goneFind(SUM_MOBJ, R.sumE[i].hash) != NULL ? D_OBJ_TELL : D_OBJ_LOST);
            }
        }
    }
    /* the named items, both ways */
    if (!(R.sum.full & 4) && !(mine.s.full & 4)) {
        /* (the room's own items: those here at the first digest compared in it, and those the owner's room has
           had. One made later by this game's own event (a tree shaken here: its coins are named) was never the
           owner's room's to lose: this player's own, left) */
        const int first = !R.itemBaseSet;
        R.itemBaseSet = 1;
        for (i = 0; i < theirN; i++) {
            if (R.sumE[i].kind == SUM_ITEM) {
                itemBaseAdd(R.sumE[i].hash);
            }
        }
        for (i = 0; iw != NULL && i < iw->count; i++) {
            const ItemEntry* it = &iw->entries[i];
            uint32_t h;
            if (!(it->flags & ITEM_FLAG_USED) || !namedItem(it)) {
                continue;
            }
            h = itemHash(it->name, it->itemId);
            if (first) {
                itemBaseAdd(h);
            }
            if (sumFind(R.sumE, theirN, SUM_ITEM, h) < 0 && itemBaseHas(h)) {
                addCand(now, &nowN, h, SUM_ITEM, D_ITEM_GONE);
            }
        }
        for (i = 0; i < theirN; i++) {
            if (R.sumE[i].kind == SUM_ITEM && sumFind(mine.e, (int)mine.s.h.count, SUM_ITEM, R.sumE[i].hash) < 0) {
                addCand(now, &nowN, R.sumE[i].hash, SUM_ITEM,
                        goneFind(SUM_ITEM, R.sumE[i].hash) != NULL ? D_ITEM_TELL : D_ITEM_LOST);
            }
        }
    }
    /* the defeats record, both ways */
    theirs = R.sum.mask & ~mine.s.mask;
    ours = mine.s.mask & ~R.sum.mask;
    if (R.prevOk && R.frame - R.prevAt < SUM_IN_A_ROW) {
        const int32_t theirsNow = theirs & R.prevTheirs, oursNow = ours & R.prevMine;
        for (i = 0; i < nowN; i++) {
            const Cand* c = &now[i];
            if (!wasCand(c)) {
                continue;
            }
            if (c->what == D_OBJ_LOST || c->what == D_ITEM_LOST || toldOut(c)) {
                if (!lostSaid(c->hash)) {
                    R.unhealed++;
                    ssLog("room: the owner's %s %08x isn't in this game's room and can't be made here: left as is",
                          c->kind == SUM_MOBJ ? "object" : "item", c->hash);
                }
                continue;
            }
            acted++;
            if (ln < sizeof(line) - 64) {
                ln += (size_t)snprintf(line + ln, sizeof(line) - ln, "%s%s (%08x)", ln ? "; " : "",
                                       candWords(c->what), c->hash);
            }
        }
        if (theirsNow != 0 && ln < sizeof(line) - 64) {
            ln += (size_t)snprintf(line + ln, sizeof(line) - ln, "%sdefeats recorded there, not here (%08x)",
                                   ln ? "; " : "", (unsigned)theirsNow);
        }
        if (oursNow != 0 && ln < sizeof(line) - 64) {
            ln += (size_t)snprintf(line + ln, sizeof(line) - ln, "%sdefeats recorded here, not there (%08x)",
                                   ln ? "; " : "", (unsigned)oursNow);
        }
        acted += (theirsNow != 0) + (oursNow != 0);
        if (acted > 0) {
            R.drifts += (unsigned)acted;
            ssLog("DRIFT room %s: %s (the owner's digest: %u enemies, %u objects, %u items; this game's: %u, %u, %u)",
                  gp->currentMapName, ln ? line : "?", (unsigned)R.sum.n[0], (unsigned)R.sum.n[1],
                  (unsigned)R.sum.n[2], (unsigned)mine.s.n[0], (unsigned)mine.s.n[1], (unsigned)mine.s.n[2]);
            for (i = 0; i < nowN; i++) {
                const Cand* c = &now[i];
                if (c->what != D_OBJ_LOST && c->what != D_ITEM_LOST && wasCand(c) && !toldOut(c)) {
                    healedNow += heal(c);
                }
            }
            if (theirsNow != 0) {
                const int32_t added = recordDead(gp->currentMapName, theirsNow);
                killByMask(added != 0 ? added : theirsNow, "recorded in the owner's room");
                healedNow++;
            }
            if (oursNow != 0) {
                DeadMsg m;
                memset(&m, 0, sizeof(m));
                snprintf(m.map, sizeof(m.map), "%s", gp->currentMapName);
                m.mask = mine.s.mask;
                ssNetSendRel(SS_R_NPC_DEAD, &m, sizeof(m));
                ssLog("room: %s's defeats record %08x told the owner's game again", m.map, (unsigned)m.mask);
                healedNow++;
            }
            R.healed += (unsigned)healedNow;
            ssLog("room: drift in %s healed: %d of %d (this session: %u found, %u healed)", gp->currentMapName,
                  healedNow, acted, R.drifts, R.healed);
        }
    }
    memcpy(R.prev, now, (size_t)nowN * sizeof(Cand));
    R.prevN = nowN;
    R.prevTheirs = theirs;
    R.prevMine = ours;
    R.prevAt = R.frame;
    R.prevOk = 1;
}

void ssRoomDesyncCounts(unsigned* found, unsigned* healed, unsigned* unhealed) {
    *found = R.drifts;
    *healed = R.healed;
    *unhealed = R.unhealed;
}

/* ---- tests: this game's room put wrong on purpose ($TTYD_SYNC_CORRUPT, port/statesync.h) ---- */

#define CORRUPT_MAX 8
static struct {
    char kind[16];
    char arg[32];
    unsigned at;
    int done;
} sCorrupt[CORRUPT_MAX];
static int sCorruptN = -1;

int ssCorruptDue(const char* kind, unsigned when, char* arg, size_t cap) {
    int i;
    if (sCorruptN < 0) {
        const char* env = getenv("TTYD_SYNC_CORRUPT");
        sCorruptN = 0;
        while (env != NULL && *env && sCorruptN < CORRUPT_MAX) {
            const char* end = strchr(env, ',');
            const size_t len = end != NULL ? (size_t)(end - env) : strlen(env);
            char one[64];
            char *at, *colon;
            snprintf(one, sizeof(one), "%.*s", (int)(len < sizeof(one) - 1 ? len : sizeof(one) - 1), env);
            at = strchr(one, '@');
            if (at != NULL) {
                *at = '\0';
                colon = strchr(one, ':');
                if (colon != NULL) {
                    *colon = '\0';
                }
                snprintf(sCorrupt[sCorruptN].kind, sizeof(sCorrupt[0].kind), "%s", one);
                snprintf(sCorrupt[sCorruptN].arg, sizeof(sCorrupt[0].arg), "%s", colon != NULL ? colon + 1 : "");
                sCorrupt[sCorruptN].at = (unsigned)strtoul(at + 1, NULL, 10);
                sCorrupt[sCorruptN].done = 0;
                ssLog("test: corrupt: %s%s%s at %u", sCorrupt[sCorruptN].kind, colon != NULL ? ":" : "",
                      sCorrupt[sCorruptN].arg, sCorrupt[sCorruptN].at);
                sCorruptN++;
            }
            env = end != NULL ? end + 1 : NULL;
        }
    }
    for (i = 0; i < sCorruptN; i++) {
        if (!sCorrupt[i].done && !strcmp(sCorrupt[i].kind, kind) && when >= sCorrupt[i].at) {
            sCorrupt[i].done = 1;
            if (arg != NULL && cap > 0) {
                snprintf(arg, cap, "%s", sCorrupt[i].arg);
            }
            return 1;
        }
    }
    return 0;
}

static void corruptFrame(void) {
    char arg[32];
    if (ssCorruptDue("mobj_gone", R.frame, arg, sizeof(arg))) {
        MapObjectEntry* m = mobjNameToPtrNoAssert(arg);
        if (m == NULL) {
            ssLog("test: corrupt: no object %s here", arg);
        } else {
            GoneMsg g;
            int k;
            for (k = 0; k < R.mobjSeenN; k++) {
                if (!strcmp(R.mobjSeen[k].name, arg)) {
                    R.mobjSeen[k].name[0] = '\0'; /* (its word never goes) */
                }
            }
            memset(&g, 0, sizeof(g));
            snprintf(g.map, sizeof(g.map), "%s", gp->currentMapName);
            snprintf(g.name, sizeof(g.name), "%s", arg);
            goneAdd(SUM_MOBJ, &g); /* (gone here: as a word lost on the way) */
            mobjDelete(arg);
            ssLog("test: corrupt: object %s deleted here, unsaid", arg);
        }
    }
    if (ssCorruptDue("item_gone", R.frame, arg, sizeof(arg))) {
        ItemWork* iw = portItemGetWork();
        int i, done = 0;
        for (i = 0; iw != NULL && i < iw->count && !done; i++) {
            ItemEntry* it = &iw->entries[i];
            if ((it->flags & ITEM_FLAG_USED) && !strcmp(it->name, arg)) {
                GoneMsg g;
                int k;
                memset(&g, 0, sizeof(g));
                snprintf(g.map, sizeof(g.map), "%s", gp->currentMapName);
                snprintf(g.name, sizeof(g.name), "%s", it->name);
                g.id = it->itemId;
                g.x = it->position.x;
                g.y = it->position.y;
                g.z = it->position.z;
                for (k = 0; k < R.itemSeenN; k++) {
                    if (!strcmp(R.itemSeen[k].name, arg)) {
                        R.itemSeen[k].name[0] = '\0';
                    }
                }
                if (namedItem(it)) {
                    goneAdd(SUM_ITEM, &g);
                }
                itemDelete(arg);
                done = 1;
            }
        }
        if (done) {
            ssLog("test: corrupt: item %s deleted here, unsaid", arg);
        } else {
            ssLog("test: corrupt: no item %s here", arg);
        }
    }
    if (ssCorruptDue("npc_dead", R.frame, arg, sizeof(arg))) {
        NpcWork* w = npcs();
        int i, done = 0;
        for (i = 0; w != NULL && i < w->total && i < NPC_SLOTS && !done; i++) {
            NpcEntry* npc = &w->entries[i];
            if (!(npc->flags & NPC_FLAG_USED) || (npc->jumpFlags & NPC_STATUS_DEAD)) {
                continue;
            }
            if (!strcmp(arg, "enemy") ? enemyAlive(npc) : !strcmp(npc->description, arg)) {
                thaw(i, npc);
                R.npcSeen[i].alive = 0; /* (its word never goes) */
                npcGroupDead(npc, 0);
                deadMaskSeen();
                ssLog("test: corrupt: %s defeated here, unsaid", safe(npc->description));
                done = 1;
            }
        }
        if (!done) {
            ssLog("test: corrupt: no NPC %s alive here", arg);
        }
    }
    if (ssCorruptDue("mask_clear", R.frame, arg, sizeof(arg))) {
        FieldBattleData* fb = fbatGetPointer();
        int i;
        for (i = 0; fb != NULL && i < 64; i++) {
            if (!strncmp(fb->deadInfo[i].mapName, gp->currentMapName, 16)) {
                ssLog("test: corrupt: %s's defeats record %08x forgotten here, unsaid", gp->currentMapName,
                      (unsigned)fb->deadInfo[i].mask);
                fb->deadInfo[i].mask = 0;
                break;
            }
        }
        deadMaskSeen();
    }
}

/* ---- the frame ---- */

void ssRoomReset(void) {
    if (R.following) {
        ssLog("room: left %s", R.map);
    }
    R.following = 0;
    R.owner = 0;
    memset(R.puppet, 0, sizeof(R.puppet)); /* (the room's NPCs are gone with it) */
    memset(R.frozen, 0, sizeof(R.frozen));
    memset(sPressed, 0, sizeof(sPressed));
    memset(R.npcSeen, 0, sizeof(R.npcSeen));
    R.mobjSeenN = R.itemSeenN = 0;
    R.npcN = R.mobjN = 0;
    R.npcAt = 0;
    /* (the room's digest: the next room's own) */
    memset(R.known, 0, sizeof(R.known));
    R.itemBaseN = R.itemBaseSet = R.itemBaseFrames = 0;
    memset(R.gone, 0, sizeof(R.gone));
    R.goneNext = 0;
    R.sumNew = R.prevN = R.prevOk = R.lostSaidN = 0;
    R.laterCount = 0;
    if (gp != NULL) {
        snprintf(R.map, sizeof(R.map), "%s", gp->currentMapName);
    }
}

void ssRoomFrame(unsigned frame, int peerHere, int iOwn) {
    const int wasOwner = R.owner; /* (last frame's) */
    R.frame = frame;
    R.owner = 0;
    if (seqGetSeq() == SEQ_MAP_CHANGE) {
        R.reload = 1;
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        return;
    }
    if (strncmp(R.map, gp->currentMapName, 16) != 0 || R.reload) {
        /* (the same room entered again (a fall's way back in, a co-op guest's room as the host's file has it) is a
           new room too: what the last one had wasn't "gone here", sent to the others as taken: an open-world
           guest's room entered again sent its seed's item as gone, online_coop_guest_open_file) */
        R.reload = 0;
        ssRoomReset();
    }
    goneFrame(); /* (what went in the other game's room as this game was coming into it) */
    forgetFreedSlots();
    if (!R.itemBaseSet && ++R.itemBaseFrames >= ITEM_BASE_FRAMES) {
        /* the room's own named items: those its set-up placed, whoever owns the room. Taken at the first
           digest compared, a room this game owned first and lost later (another player came in while this one
           fought: both claimed it, the lower number kept it) counted what this game's own events had made
           meanwhile as the room's: a block's POW Block, hit here, then "an item here is gone there", taken
           away (DRIFT, the online soak's seed 202) */
        ItemWork* iw = portItemGetWork();
        int i;
        R.itemBaseSet = 1;
        for (i = 0; iw != NULL && i < iw->count; i++) {
            const ItemEntry* it = &iw->entries[i];
            if ((it->flags & ITEM_FLAG_USED) && namedItem(it)) {
                itemBaseAdd(itemHash(it->name, it->itemId));
            }
        }
    }
    if (ssNetConnected()) {
        /* what went here (in any room: the other game records it for when it comes) */
        watchNpcs();
        watchItems();
        watchMobjs();
        corruptFrame(); /* (tests: this game's room put wrong on purpose, $TTYD_SYNC_CORRUPT) */
    }
    if (!peerHere || iOwn) {
        if (R.following) {
            thawAll();
        }
        R.sumNew = R.prevOk = 0; /* (not following: no owner's digest to compare) */
        if (peerHere && iOwn) {
            if (!wasOwner) {
                sSent.npcsLen = sSent.mobjsLen = 0; /* (owning it afresh: the lists go at once, never held back) */
            }
            R.owner = 1;
            if (frame % NPC_EVERY == 0) {
                sendNpcs();
            }
            if (frame % MOBJ_EVERY == 0) {
                sendMobjs();
            }
            if (frame % SUM_EVERY == 0) {
                sendSum();
            }
        }
        return;
    }
    /* the other game owns this room: its NPCs (a list not too old) and objects */
    npcsLater();
    if (R.npcN > 0 && frame - R.npcAt < 60) {
        puppets();
        follow();
    }
    followMobjs();
    if (R.sumNew) {
        sumCheck(); /* (the owner's room's digest: what differs at two in a row healed here) */
    }
    if (R.following && frame % 120 == 0) {
        int i;
        ssLog("room: following %s: %d NPCs of theirs, %u updates", R.map, R.npcN, R.applied);
        for (i = 0; i < R.npcN && i < 3; i++) {
            NpcEntry* npc = npcNameToPtr_NoAssert(R.npc[i].name);
            if (npc != NULL) {
                ssLog("room: NPC %s here at (%.1f, %.1f, %.1f) %s, theirs (%.1f, %.1f, %.1f)", safe(R.npc[i].name),
                      npc->position.x, npc->position.y, npc->position.z, npc->currentAnim, R.npc[i].x, R.npc[i].y,
                      R.npc[i].z);
            }
        }
    }
}

/* the owner's list (checked) as this game's to follow from this frame */
static void npcsTake(const NpcSync* list, int n) {
    memcpy(R.npc, list, (size_t)n * sizeof(NpcSync));
    R.npcN = n;
    R.npcAt = R.frame;
}

/* a room's lists that came together: the next one, NPC_EVERY frames after the last taken */
static void npcsLater(void) {
    if (R.laterCount > 0 && R.frame - R.npcAt >= NPC_EVERY) {
        npcsTake(R.later[R.laterHead], R.laterN[R.laterHead]);
        R.laterHead = (R.laterHead + 1) % NPC_LATER;
        R.laterCount--;
    }
}

void ssRoomMessage(int type, const uint8_t* p, size_t n) {
    ListHead h;
    if (n < sizeof(ListHead)) {
        return;
    }
    memcpy(&h, p, sizeof(h));
    h.map[15] = '\0';
    if (!sameMap(h.map)) {
        return;
    }
    if (type == SS_MSG_NPCS) {
        static NpcSync sIn[MAX_NPCS];
        const size_t c = h.count < MAX_NPCS ? h.count : MAX_NPCS;
        size_t i;
        if (n < sizeof(ListHead) + c * sizeof(NpcSync)) {
            return;
        }
        memcpy(sIn, p + sizeof(ListHead), c * sizeof(NpcSync));
        for (i = 0; i < c; i++) {
            NpcSync* s = &sIn[i];
            s->name[31] = s->anim[31] = s->model[31] = '\0';
            if (!sane(s->x) || !sane(s->y) || !sane(s->z) || !sane(s->dir)) {
                R.npcN = 0;
                R.laterCount = 0;
                ssLog("room: the owner's NPC list dropped (%s's place out of range)", safe(s->name));
                return;
            }
            if (s->model[0] && !modelNameOk(s->model)) {
                ssLog("room: the owner's %s has a model name not loaded here: no stand-in", safe(s->name));
                s->model[0] = '\0';
            }
        }
        if (ssNetRoom() && (R.laterCount > 0 || R.frame - R.npcAt < NPC_EVERY)) {
            /* (another of the owner's lists came in the same batch or just before: played out after it, as it
               was made; more than NPC_LATER behind, the oldest goes now, so they never lag far) */
            int k;
            if (R.laterCount == NPC_LATER) {
                npcsTake(R.later[R.laterHead], R.laterN[R.laterHead]);
                R.laterHead = (R.laterHead + 1) % NPC_LATER;
                R.laterCount--;
            }
            k = (R.laterHead + R.laterCount) % NPC_LATER;
            memcpy(R.later[k], sIn, c * sizeof(NpcSync));
            R.laterN[k] = (int)c;
            R.laterCount++;
            return;
        }
        npcsTake(sIn, (int)c);
    } else if (type == SS_MSG_MOBJS) {
        const size_t c = h.count < MAX_MOBJS ? h.count : MAX_MOBJS;
        size_t i;
        if (n < sizeof(ListHead) + c * sizeof(MobjSync)) {
            return;
        }
        memcpy(R.mobj, p + sizeof(ListHead), c * sizeof(MobjSync));
        for (i = 0; i < c; i++) {
            MobjSync* s = &R.mobj[i];
            s->name[15] = '\0';
            s->flags &= MOBJ_FLAG_HIDDEN | MOBJ_FLAG_INVISIBLE; /* (only these are the owner's to set) */
            if (!sane(s->x) || !sane(s->y) || !sane(s->z)) {
                R.mobjN = 0;
                ssLog("room: the owner's object list dropped (%s's place out of range)", safe(s->name));
                return;
            }
        }
        R.mobjN = (int)c;
        R.mobjNew = 1;
    } else if (type == SS_MSG_ROOM && n >= sizeof(RoomSum)) {
        RoomSum s;
        const size_t c = h.count;
        memcpy(&s, p, sizeof(s));
        if (c > SUM_MAX || n < sizeof(RoomSum) + c * sizeof(SumEntry) || (size_t)s.n[0] + s.n[1] + s.n[2] != c) {
            ssLog("room: the owner's digest dropped (its counts don't add up)");
            return;
        }
        s.h.map[15] = '\0';
        R.sum = s;
        memcpy(R.sumE, p + sizeof(RoomSum), c * sizeof(SumEntry));
        R.sumNew = 1;
    }
}

/* the other game's defeats record for a map, joined with this game's: the defeats new here (by bit) */
static int32_t recordDead(const char* map, int32_t mask) {
    FieldBattleData* fb = fbatGetPointer();
    int i, empty = -1;
    if (fb == NULL || mask == 0 || !map[0]) {
        return 0;
    }
    for (i = 0; i < 64; i++) {
        if (!strncmp(fb->deadInfo[i].mapName, map, 16)) {
            const int32_t added = mask & ~fb->deadInfo[i].mask;
            if (added != 0) {
                fb->deadInfo[i].mask |= mask;
                ssLog("room: %s's defeats now %08x (the other game's)", map, fb->deadInfo[i].mask);
                if (i == 0) {
                    R.deadMask0 = fb->deadInfo[0].mask; /* (not sent back) */
                }
            }
            return added;
        }
        if (empty < 0 && i > 0 && fb->deadInfo[i].mapName[0] == '\0') {
            empty = i;
        }
    }
    if (empty > 0) {
        memset(&fb->deadInfo[empty], 0, sizeof(fb->deadInfo[empty]));
        snprintf(fb->deadInfo[empty].mapName, sizeof(fb->deadInfo[empty].mapName), "%s", map);
        fb->deadInfo[empty].mask = mask;
        ssLog("room: %s's defeats recorded %08x (the other game's)", map, mask);
        return mask;
    }
    return 0;
}

/* ---- when the enemies come back: one rule for every game ----
 * The game keeps a record per map of the enemies defeated there (deadInfo: who stays gone) and forgets records
 * as the player moves on (setupDataBase): every one when the area changes, and on the open field maps (hei,
 * gra, ...) all but the map's own and the one before it. A forgotten record's enemies are back the next time
 * the map loads. Each game forgetting by its own player's moves let the games disagree about a room both are
 * in: one game came back to a map its own rule had forgotten, its enemies back, while the game that owned the
 * room still had them gone; the room's digest found it and defeated them again, the next walk away forgot it
 * again, and so on for as long as they played (and one recorded only in the follower was told to the owner
 * and defeated there, its own rule having brought them back).
 * So online with others, no game forgets a record by its own rule alone. Each game keeps its own list of the
 * maps whose records its own rule still keeps (sKeep: the same steps as the game's on the table's names) and
 * tells the others when it changes (SS_R_DEAD_HELD, with its masks; again for a new session or a newcomer).
 * As a map loads, its record is kept if this game's own rule keeps it or another player's game keeps it by
 * theirs; else it's forgotten here, its enemies back, and the others forget it too (the same message names
 * it). So the enemies come back once every player has left them behind as the game would for one, and a room
 * two players share has the same record in both games. Records no player keeps are dropped from the table as
 * a map loads (the game's own forgetting, done once nobody keeps them). */
#define KEEP_MAX 64
typedef struct HeldEntry {
    char map[16];
    int32_t mask;
} HeldEntry;
typedef struct HeldHead {
    char back[16]; /* the map whose enemies this game just let come back ("" none) */
    uint32_t count;
} HeldHead;
static char sKeep[KEEP_MAX][16];                     /* this game's own rule's list, the newest first */
static char sHeldBy[SS_PLAYERS + 1][KEEP_MAX][16];   /* each other player's (theirs, as last told) */
static int sHeldN[SS_PLAYERS + 1];

static int keepHas(const char* map) {
    int i;
    for (i = 0; i < KEEP_MAX && sKeep[i][0]; i++) {
        if (!strncmp(sKeep[i], map, 16)) {
            return 1;
        }
    }
    return 0;
}

/* another player in the room keeps the map's record by their own game's rule: who (0 none) */
static int heldByOther(const char* map) {
    int p, i;
    for (p = 1; p <= SS_PLAYERS; p++) {
        if (!ssPlayerIn(p)) {
            continue;
        }
        for (i = 0; i < sHeldN[p]; i++) {
            if (!strncmp(sHeldBy[p][i], map, 16)) {
                return p;
            }
        }
    }
    return 0;
}

/* this game's list to the others: when the maps in it changed (its order alone isn't news), a map's enemies came
   back (back), or always (force: a new session, a newcomer) */
static void heldSend(const char* back, int force) {
    static char sSent[KEEP_MAX][16];
    FieldBattleData* fb = fbatGetPointer();
    uint8_t buf[sizeof(HeldHead) + KEEP_MAX * sizeof(HeldEntry)];
    HeldHead h;
    uint32_t n = 0;
    int i, k, same = 1;
    if (!ssNetConnected()) {
        return;
    }
    for (i = 0; i < KEEP_MAX && same; i++) {
        if (sKeep[i][0]) {
            for (k = 0; k < KEEP_MAX && strncmp(sSent[k], sKeep[i], 16) != 0; k++) {
            }
            same = k < KEEP_MAX;
        }
        if (sSent[i][0]) {
            same = same && keepHas(sSent[i]);
        }
    }
    if (same && !force && (back == NULL || !back[0])) {
        return;
    }
    memcpy(sSent, sKeep, sizeof(sSent));
    memset(&h, 0, sizeof(h));
    snprintf(h.back, sizeof(h.back), "%s", back != NULL ? back : "");
    for (i = 0; i < KEEP_MAX && sKeep[i][0]; i++) {
        HeldEntry e;
        memset(&e, 0, sizeof(e));
        memcpy(e.map, sKeep[i], 16);
        for (k = 0; fb != NULL && k < 64; k++) {
            if (!strncmp(fb->deadInfo[k].mapName, sKeep[i], 16)) {
                e.mask = fb->deadInfo[k].mask;
                break;
            }
        }
        memcpy(buf + sizeof(h) + n * sizeof(e), &e, sizeof(e));
        n++;
    }
    h.count = n;
    memcpy(buf, &h, sizeof(h));
    ssNetSendRel(SS_R_DEAD_HELD, buf, sizeof(h) + n * sizeof(HeldEntry));
}

void ssRoomHeldSend(void) {
    heldSend(NULL, 1);
}

void portDefeatsTableSet(void) {
    FieldBattleData* fb = fbatGetPointer();
    int i, n = 0;
    memset(sKeep, 0, sizeof(sKeep));
    for (i = 0; fb != NULL && i < 64; i++) {
        if (fb->deadInfo[i].mapName[0]) {
            memcpy(sKeep[n++], fb->deadInfo[i].mapName, 16);
            sKeep[n - 1][15] = '\0';
        }
    }
    heldSend(NULL, 0);
}

int portDefeatsMapLoad(const char* mapName, int recordMap, int rule) {
    FieldBattleData* fb = fbatGetPointer();
    int i, found, by;
    /* this game's own list, as the game's own steps take the table (setupDataBase): the map first, then forget */
    for (i = 0; i < KEEP_MAX; i++) {
        if (!sKeep[i][0] || !strncmp(sKeep[i], mapName, 16)) {
            break;
        }
    }
    found = i < KEEP_MAX && sKeep[i][0] != '\0';
    if (i == KEEP_MAX) {
        i = KEEP_MAX - 1; /* (all in use, none this map's: the oldest goes, as setupDataBase's) */
    }
    if (i > 0) {
        memmove(sKeep[1], sKeep[0], (size_t)i * sizeof(sKeep[0]));
    }
    memset(sKeep[0], 0, sizeof(sKeep[0]));
    snprintf(sKeep[0], sizeof(sKeep[0]), "%s", mapName);
    if (rule == 2) {
        memset(sKeep[1], 0, (KEEP_MAX - 1) * sizeof(sKeep[0]));
        found = 0; /* (the area changed: this map's own record forgotten too) */
    } else if (rule == 1) {
        memset(sKeep[2], 0, (KEEP_MAX - 2) * sizeof(sKeep[0]));
    }
    if (fb == NULL || !ssNetConnected() || !portStateSyncOn()) {
        return 0; /* (alone: the game's own rule on the table, which this list is) */
    }
    /* online with others: this map's record kept by this game's own rule or another player's */
    by = 0;
    if (recordMap && !found && (by = heldByOther(mapName)) == 0) {
        if (fb->deadInfo[0].mask != 0) {
            ssLog("room: %s's defeats forgotten (%08x): no player keeps them now, its enemies are back", mapName,
                  (unsigned)fb->deadInfo[0].mask);
        }
        fb->deadInfo[0].mask = 0;
    } else if (recordMap && !found && fb->deadInfo[0].mask != 0) {
        ssLog("room: %s's defeats kept (%08x): player %d's game keeps them", mapName, (unsigned)fb->deadInfo[0].mask,
              by);
    }
    /* the records no player keeps any more: gone from the table, as the game's own rule would have */
    for (i = 1; i < 64; i++) {
        const char* m = fb->deadInfo[i].mapName;
        if (m[0] && !keepHas(m) && !heldByOther(m)) {
            if (fb->deadInfo[i].mask != 0) {
                ssLog("room: %.16s's defeats forgotten (%08x): no player keeps them now", m,
                      (unsigned)fb->deadInfo[i].mask);
            }
            memset(&fb->deadInfo[i], 0, sizeof(fb->deadInfo[i]));
        }
    }
    heldSend(recordMap && !found && by == 0 ? mapName : NULL, 0);
    return 1;
}

void ssRoomHeldRel(int from, const uint8_t* p, size_t n) {
    FieldBattleData* fb = fbatGetPointer();
    HeldHead h;
    size_t c;
    int i;
    if (from < 1 || from > SS_PLAYERS || from == ssNetMe() || n < sizeof(HeldHead)) {
        return;
    }
    memcpy(&h, p, sizeof(h));
    c = h.count;
    if (c > KEEP_MAX || n < sizeof(HeldHead) + c * sizeof(HeldEntry)) {
        ssLog("room: player %d's list of defeat records dropped (its count doesn't add up)", from);
        return;
    }
    h.back[15] = '\0';
    /* theirs: the maps whose records their own game's rule keeps */
    memset(sHeldBy[from], 0, sizeof(sHeldBy[from]));
    sHeldN[from] = 0;
    for (i = 0; i < (int)c; i++) {
        HeldEntry e;
        memcpy(&e, p + sizeof(HeldHead) + (size_t)i * sizeof(HeldEntry), sizeof(e));
        e.map[15] = '\0';
        if (!ssMapNameOk(e.map, sizeof(e.map))) {
            continue;
        }
        memcpy(sHeldBy[from][sHeldN[from]++], e.map, 16);
    }
    /* a map whose enemies their game just let come back (no player kept its record): forgotten here too */
    if (h.back[0] && fb != NULL && ssMapNameOk(h.back, sizeof(h.back))) {
        for (i = 0; i < 64; i++) {
            if (!strncmp(fb->deadInfo[i].mapName, h.back, 16)) {
                if (i == 0 && sameMap(h.back) && fb->deadInfo[0].mask != 0) {
                    /* (this game came into it meanwhile: its room has them gone, and its list says so next) */
                    ssLog("room: player %d let %s's enemies come back as this game came in: kept here", from, h.back);
                } else if (fb->deadInfo[i].mask != 0) {
                    ssLog("room: %s's defeats forgotten (%08x): player %d's game let its enemies come back", h.back,
                          (unsigned)fb->deadInfo[i].mask, from);
                    fb->deadInfo[i].mask = 0;
                }
                break;
            }
        }
    }
    /* their records of the maps they keep, joined with this game's (a defeat here and there: the same set) */
    for (i = 0; i < (int)c; i++) {
        HeldEntry e;
        int32_t added;
        memcpy(&e, p + sizeof(HeldHead) + (size_t)i * sizeof(HeldEntry), sizeof(e));
        e.map[15] = '\0';
        if (e.mask == 0 || !ssMapNameOk(e.map, sizeof(e.map))) {
            continue;
        }
        added = recordDead(e.map, e.mask);
        if (added != 0 && sameMap(e.map) && gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle) {
            killByMask(added, "the other game's defeats record");
        }
    }
}

/* ---- an item taken, an object gone in the other game ---- */

/* what went in the other game's room while this game wasn't in it yet (loading into it, its entry
   script not yet making the item or object): kept a few seconds, tried again each field frame */
#define GONE_KEEP 16
#define GONE_KEEP_MS 5000
typedef struct GoneKept {
    int type;
    GoneMsg m;
    unsigned long long until;
} GoneKept;
static GoneKept sGone[GONE_KEEP];
static int sGoneN;

/* 1: done (gone here now, or not this game's room to wait for); 0: the item or object isn't here yet */
static int goneApply(int type, const GoneMsg* m) {
    int k;
    if (gp == NULL || !sameMap(m->map) || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        return 0;
    }
    if (type == SS_R_ITEM_GONE) {
        ItemWork* w = portItemGetWork();
        int i;
        if (w == NULL) {
            return 0;
        }
        for (i = 0; i < w->count; i++) {
            ItemEntry* it = &w->entries[i];
            const float dx = it->position.x - m->x, dz = it->position.z - m->z;
            /* (a room's own named item is the one of its name wherever it is: a partner fetching it moves it first,
               Koops's shell bringing Key 4 in gon_05 took it 150 from where the other game still had it, s51-009) */
            if ((it->flags & ITEM_FLAG_USED) && !strcmp(it->name, m->name) && it->itemId == m->id &&
                (namedItem(it) || dx * dx + dz * dz < 60.0f * 60.0f)) {
                for (k = 0; k < R.itemSeenN; k++) {
                    if (!strcmp(R.itemSeen[k].name, m->name)) {
                        R.itemSeen[k].name[0] = '\0'; /* (not sent back) */
                    }
                }
                if (namedItem(it)) {
                    goneAdd(SUM_ITEM, m); /* (gone here too: told again if the owner's room still has it) */
                }
                itemDelete(m->name);
                ssLog("room: item %s (%d) taken by the other player: gone here too", m->name, m->id);
                return 1;
            }
        }
        return 0;
    }
    if (mobjNameToPtrNoAssert(m->name) == NULL) {
        return 0;
    }
    for (k = 0; k < R.mobjSeenN; k++) {
        if (!strcmp(R.mobjSeen[k].name, m->name)) {
            R.mobjSeen[k].name[0] = '\0';
        }
    }
    if (portMobjPressBlueSwitch(mobjNameToPtrNoAssert(m->name))) {
        /* (a blue switch the other player pressed: its event here too; it goes by itself after) */
        ssLog("room: blue switch %s pressed by the other player: pressed here too", m->name);
        return 1;
    }
    mobjDelete(m->name);
    goneAdd(SUM_MOBJ, m);
    ssLog("room: object %s gone by the other player: gone here too", m->name);
    return 1;
}

/* a room this game is in or may be going to: in its field, or not in the field (loading, a switch asked) */
static int goneMayCome(const GoneMsg* m) {
    return gp != NULL && (sameMap(m->map) || seqGetSeq() != SEQ_GAME || seqCheckSeq());
}

static void goneKeep(int type, const GoneMsg* m) {
    int i;
    if (!goneMayCome(m)) {
        return; /* (another room's: an item dropped there is that game's alone; a room's own stay gone by its flags) */
    }
    for (i = 0; i < sGoneN; i++) {
        /* (the same word again: the room's digest told it again, sumCheck, while the first still waits here:
           one kept, its time from now, never two applied) */
        if (sGone[i].type == type && !strcmp(sGone[i].m.name, m->name) && !strcmp(sGone[i].m.map, m->map) &&
            (type != SS_R_ITEM_GONE || sGone[i].m.id == m->id)) {
            sGone[i].until = ssNetMs() + GONE_KEEP_MS;
            return;
        }
    }
    if (sGoneN == GONE_KEEP) {
        memmove(&sGone[0], &sGone[1], (GONE_KEEP - 1) * sizeof(sGone[0])); /* (the oldest goes) */
        sGoneN--;
    }
    sGone[sGoneN].type = type;
    sGone[sGoneN].m = *m;
    sGone[sGoneN].until = ssNetMs() + GONE_KEEP_MS;
    sGoneN++;
    ssLog("room: %s %s gone in the other game's %s: not here yet, kept a few seconds",
          type == SS_R_ITEM_GONE ? "item" : "object", m->name, m->map);
}

/* each field frame: the kept ones tried again; dropped once done, too old, or this game in another room */
static void goneFrame(void) {
    const unsigned long long now = ssNetMs();
    int i, n = 0;
    for (i = 0; i < sGoneN; i++) {
        GoneKept* g = &sGone[i];
        if (goneApply(g->type, &g->m) || now > g->until || !goneMayCome(&g->m)) {
            continue;
        }
        sGone[n++] = *g;
    }
    sGoneN = n;
}

void ssRoomRel(int type, const uint8_t* p, size_t n) {
    if (gp == NULL || gp->inBattle) {
        return; /* (a battle here: what the room lost reaches it when the battle's over? no: see the doc) */
    }
    if (type == SS_R_NPC_DEAD && n >= sizeof(DeadMsg)) {
        DeadMsg m;
        memcpy(&m, p, sizeof(m));
        m.map[15] = m.name[31] = '\0';
        {
            const int32_t added = recordDead(m.map, m.mask);
            if (added != 0 && sameMap(m.map) && seqGetSeq() == SEQ_GAME) {
                killByMask(added, "the other game's defeats record"); /* (a defeat whose own word was lost) */
            }
        }
        if (m.name[0] && sameMap(m.map) && seqGetSeq() == SEQ_GAME) {
            NpcEntry* npc = npcNameToPtr_NoAssert(m.name);
            if (npc != NULL && !(npc->jumpFlags & NPC_STATUS_DEAD)) {
                NpcWork* w = npcs();
                const int slot = w != NULL ? (int)(npc - w->entries) : -1;
                if (slot >= 0 && slot < NPC_SLOTS) {
                    thaw(slot, npc);
                    R.npcSeen[slot].alive = 0; /* (not sent back) */
                }
                npcGroupDead(npc, 0);
                ssLog("room: %s defeated by the other player: gone here too", safe(m.name));
            }
        }
    } else if ((type == SS_R_ITEM_GONE || type == SS_R_MOBJ_GONE) && n >= sizeof(GoneMsg)) {
        GoneMsg m;
        memcpy(&m, p, sizeof(m));
        m.map[15] = m.name[31] = '\0';
        if (type == SS_R_MOBJ_GONE) {
            m.name[15] = '\0';
        }
        if (!goneApply(type, &m)) {
            goneKeep(type, &m); /* (not here yet: this game is loading into that room, or its entry hasn't made it) */
        }
    }
}

void ssRoomStop(void) {
    if (R.following) {
        thawAll();
    }
    sGoneN = 0;
    ssRoomReset();
}

/* ---- the owner's enemies chase the nearest player ---- */

static Vec sMarioWas, sMarioAt;
static int sSwapped;

void portStateSyncNpcTargetBegin(NpcEntry* npc) {
    MarioWork* mario;
    float x, y, z;
    /* (only while this player is free: in a talk or a scene of its own, its events see its own Mario,
       as the game alone would; an enemy's events may move Mario there) */
    if (sSwapped || npc == NULL || !R.owner || (npc->flags & (NPC_FLAG_USED | NPC_FLAG_ENEMY)) !=
                                                     (NPC_FLAG_USED | NPC_FLAG_ENEMY) ||
        !portStateSyncLinked() || (mario = marioGetPtr()) == NULL || !marioChkKey()) {
        return;
    }
    if (ssNearestOther(npc->position.x, npc->position.y, npc->position.z, &x, &y, &z)) {
        sMarioWas = mario->position;
        mario->position.x = x;
        mario->position.y = y;
        mario->position.z = z;
        sMarioAt = mario->position;
        sSwapped = 1;
    }
}

void portStateSyncNpcTargetEnd(void) {
    if (sSwapped) {
        /* back where it was, with whatever the checks or events moved it by meanwhile (a push) */
        MarioWork* mario = marioGetPtr();
        mario->position.x = sMarioWas.x + (mario->position.x - sMarioAt.x);
        mario->position.y = sMarioWas.y + (mario->position.y - sMarioAt.y);
        mario->position.z = sMarioWas.z + (mario->position.z - sMarioAt.z);
        sSwapped = 0;
    }
}

int portStateSyncNpcTargetSwapped(void) {
    return sSwapped;
}

/* every message's size, the same in every build (port/statesync.h WIRE_SIZE) */
WIRE_SIZE(NpcSync, 116);
WIRE_SIZE(MobjSync, 36);
WIRE_SIZE(ListHead, 20);
WIRE_SIZE(DeadMsg, 52);
WIRE_SIZE(GoneMsg, 64);
WIRE_SIZE(SumEntry, 8);
WIRE_SIZE(RoomSum, 32);
WIRE_SIZE(HeldHead, 20);
WIRE_SIZE(HeldEntry, 20);
