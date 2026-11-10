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
} R;

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
    ssNetSend(SS_MSG_NPCS, buf, sizeof(ListHead) + (size_t)n * sizeof(NpcSync));
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
    ssNetSend(SS_MSG_MOBJS, buf, sizeof(ListHead) + (size_t)n * sizeof(MobjSync));
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
            ssLog("room: object %s gone here in %s: sent", m.name, m.map);
        }
    }
    memcpy(R.mobjSeen, now, (size_t)n * sizeof(Seen));
    R.mobjSeenN = n;
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
    if (gp != NULL) {
        snprintf(R.map, sizeof(R.map), "%s", gp->currentMapName);
    }
}

void ssRoomFrame(unsigned frame, int peerHere, int iOwn) {
    R.frame = frame;
    R.owner = 0;
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle) {
        return;
    }
    if (strncmp(R.map, gp->currentMapName, 16) != 0) {
        ssRoomReset();
    }
    if (ssNetConnected()) {
        /* what went here (in any room: the other game records it for when it comes) */
        watchNpcs();
        watchItems();
        watchMobjs();
    }
    if (!peerHere || iOwn) {
        if (R.following) {
            thawAll();
        }
        if (peerHere && iOwn) {
            R.owner = 1;
            if (frame % NPC_EVERY == 0) {
                sendNpcs();
            }
            if (frame % MOBJ_EVERY == 0) {
                sendMobjs();
            }
        }
        return;
    }
    /* the other game owns this room: its NPCs (a list not too old) and objects */
    if (R.npcN > 0 && frame - R.npcAt < 60) {
        puppets();
        follow();
    }
    followMobjs();
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
        const size_t c = h.count < MAX_NPCS ? h.count : MAX_NPCS;
        size_t i;
        if (n < sizeof(ListHead) + c * sizeof(NpcSync)) {
            return;
        }
        memcpy(R.npc, p + sizeof(ListHead), c * sizeof(NpcSync));
        for (i = 0; i < c; i++) {
            NpcSync* s = &R.npc[i];
            s->name[31] = s->anim[31] = s->model[31] = '\0';
            if (!sane(s->x) || !sane(s->y) || !sane(s->z) || !sane(s->dir)) {
                R.npcN = 0;
                ssLog("room: the owner's NPC list dropped (%s's place out of range)", safe(s->name));
                return;
            }
            if (s->model[0] && !modelNameOk(s->model)) {
                ssLog("room: the owner's %s has a model name not loaded here: no stand-in", safe(s->name));
                s->model[0] = '\0';
            }
        }
        R.npcN = (int)c;
        R.npcAt = R.frame;
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
    }
}

static void recordDead(const char* map, int32_t mask) {
    FieldBattleData* fb = fbatGetPointer();
    int i, empty = -1;
    if (fb == NULL || mask == 0 || !map[0]) {
        return;
    }
    for (i = 0; i < 64; i++) {
        if (!strncmp(fb->deadInfo[i].mapName, map, 16)) {
            if ((fb->deadInfo[i].mask | mask) != fb->deadInfo[i].mask) {
                fb->deadInfo[i].mask |= mask;
                ssLog("room: %s's defeats now %08x (the other game's)", map, fb->deadInfo[i].mask);
                if (i == 0) {
                    R.deadMask0 = fb->deadInfo[0].mask; /* (not sent back) */
                }
            }
            return;
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
    }
}

void ssRoomRel(int type, const uint8_t* p, size_t n) {
    if (gp == NULL || gp->inBattle) {
        return; /* (a battle here: what the room lost reaches it when the battle's over? no: see the doc) */
    }
    if (type == SS_R_NPC_DEAD && n >= sizeof(DeadMsg)) {
        DeadMsg m;
        memcpy(&m, p, sizeof(m));
        m.map[15] = m.name[31] = '\0';
        recordDead(m.map, m.mask);
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
    } else if (type == SS_R_ITEM_GONE && n >= sizeof(GoneMsg)) {
        GoneMsg m;
        ItemWork* w = portItemGetWork();
        int i;
        memcpy(&m, p, sizeof(m));
        m.map[15] = m.name[31] = '\0';
        if (!sameMap(m.map) || w == NULL) {
            return;
        }
        for (i = 0; i < w->count; i++) {
            ItemEntry* it = &w->entries[i];
            const float dx = it->position.x - m.x, dz = it->position.z - m.z;
            if ((it->flags & ITEM_FLAG_USED) && !strcmp(it->name, m.name) && it->itemId == m.id &&
                dx * dx + dz * dz < 60.0f * 60.0f) {
                int k;
                for (k = 0; k < R.itemSeenN; k++) {
                    if (!strcmp(R.itemSeen[k].name, m.name)) {
                        R.itemSeen[k].name[0] = '\0'; /* (not sent back) */
                    }
                }
                itemDelete(m.name);
                ssLog("room: item %s (%d) taken by the other player: gone here too", m.name, m.id);
                break;
            }
        }
    } else if (type == SS_R_MOBJ_GONE && n >= sizeof(GoneMsg)) {
        GoneMsg m;
        int k;
        memcpy(&m, p, sizeof(m));
        m.map[15] = '\0';
        m.name[15] = '\0';
        if (!sameMap(m.map) || mobjNameToPtrNoAssert(m.name) == NULL) {
            return;
        }
        for (k = 0; k < R.mobjSeenN; k++) {
            if (!strcmp(R.mobjSeen[k].name, m.name)) {
                R.mobjSeen[k].name[0] = '\0';
            }
        }
        if (portMobjPressBlueSwitch(mobjNameToPtrNoAssert(m.name))) {
            /* (a blue switch the other player pressed: its event here too; it goes by itself after) */
            ssLog("room: blue switch %s pressed by the other player: pressed here too", m.name);
            return;
        }
        mobjDelete(m.name);
        ssLog("room: object %s gone by the other player: gone here too", m.name);
    }
}

void ssRoomStop(void) {
    if (R.following) {
        thawAll();
    }
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
