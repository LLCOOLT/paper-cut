/* Map edits: the store and the room's runtime (port/include/port/map_edits.h,
 * docs/MAP_EDITOR.md).
 *
 * The store holds sets of things by room: the built-in ones (the remake's
 * map changes, added once by remake_maps_pc.c), the mods' (maps/<room>.json,
 * swapped in whole by the mods' apply) and the editor's live set for the
 * room it edits. A room's things are placed from evt_bero_get_info once a
 * room load, each into a slot of this room's: its names (the mobj's, its
 * collision's, an NPC's), the script that makes it (built for it: the mobj
 * functions take their kind and flag words as they are, so those can't come
 * from a script's LWs) and its own action script (a switch's, a sign's).
 *
 * No state of our own is saved: what's placed is game state (mobjs, NPCs,
 * items, case events), placed again as the room loads, from the data and the
 * saved story state only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/map_edits.h"
#include "port/battlex.h"
#include "data/item_data.h"
#include <dolphin/pad.h>
#include "port/mod_host.h"
#include "port/open_world.h"
#include "port/remake_maps.h"
#include "system.h"
#include "data/npc_data.h"
#include "driver/animdrv.h"
#include "driver/casedrv.h"
#include "driver/hitdrv.h"
#include "driver/itemdrv.h"
#include "driver/mapdrv.h"
#include "driver/mobjdrv.h"
#include "driver/npcdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_case.h"
#include "evt/evt_cmd.h"
#include "evt/evt_fade.h"
#include "evt/evt_item.h"
#include "evt/evt_mario.h"
#include "evt/evt_mobj.h"
#include "evt/evt_msg.h"
#include "evt/evt_npc.h"
#include "evt/evt_pouch.h"
#include "evt/evt_snd.h"
#include "evt/evt_story.h"
#include "driver/fadedrv.h"
#include "evt/evt_window.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

ItemWork* portItemGetWork(void); /* itemdrv.c (PC port) */

/* --- things --- */

static const char* const kTypeNames[PORT_MT_COUNT] = {
    "pipe",       "save_block", "heart_block", "jump_stand",     "block",      "badge_block", "brick",
    "hidden_block", "switch",   "sign",        "item",           "npc",        "zone",        "hide_collision",
    "hide_model", "remove_mobj", "remove_npc", "remove_item",    "exit",
};

void portMapThingInit(PortMapThing* t, int type) {
    memset(t, 0, sizeof(*t));
    t->type = type;
    t->gswf = t->gswfOff = t->gsw = -1;
    t->setting = -1;
    t->usesGsw = -1;
}

int portMapThingType(const char* name) {
    int i;
    for (i = 0; name != NULL && i < PORT_MT_COUNT; i++) {
        if (strcmp(kTypeNames[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

const char* portMapThingTypeName(int type) {
    return type >= 0 && type < PORT_MT_COUNT ? kTypeNames[type] : "";
}

int portMapThingPlaced(int type) {
    return type >= PORT_MT_PIPE && type <= PORT_MT_ZONE;
}

/* --- the store --- */

typedef struct Entry {
    char source[64];
    char room[16];
    PortMapThing t;
} Entry;

typedef struct Store {
    Entry* e;
    int n, cap;
} Store;

static Store sBuiltin; /* the remake's (added once, never cleared) */
static Store sStores[2]; /* the mods': the one in use, the one filling */
static int sUse = -1, sFill = -1;
static Store sLive; /* the editor's set for one room */
static char sLiveSource[64], sLiveRoom[16];
static int sLiveOn;
static char sError[256];

static void storeAdd(Store* s, const char* source, const char* room, const PortMapThing* t) {
    Entry* e;
    if (s->n == s->cap) {
        const int cap = s->cap ? s->cap * 2 : 64;
        Entry* grown = (Entry*)realloc(s->e, sizeof(Entry) * (size_t)cap);
        if (grown == NULL) {
            return;
        }
        s->e = grown;
        s->cap = cap;
    }
    e = &s->e[s->n++];
    snprintf(e->source, sizeof(e->source), "%s", source != NULL ? source : "");
    snprintf(e->room, sizeof(e->room), "%s", room != NULL ? room : "");
    e->t = *t;
}

void portMapEditsAddBuiltin(const char* source, const char* room, const PortMapThing* thing) {
    storeAdd(&sBuiltin, source, room, thing);
}

void portMapEditsBegin(void) {
    sFill = (sUse + 1) % 2; /* (the one in use stays till the commit: slots hold copies) */
    sStores[sFill].n = 0;
}

void portMapEditsAdd(const char* source, const char* room, const PortMapThing* thing) {
    if (sFill < 0) {
        portMapEditsBegin();
    }
    storeAdd(&sStores[sFill], source, room, thing);
}

void portMapEditsCommit(void) {
    if (sFill >= 0) {
        sUse = sFill;
        sFill = -1;
    }
}

/* the set entry in use for (source, room): the editor's live set replaces the mods' one */
static int liveCovers(const Entry* e) {
    return sLiveOn && strcmp(e->source, sLiveSource) == 0 && strcmp(e->room, sLiveRoom) == 0;
}

/* each entry of the room (every source, or one), in order: the built-in, the mods', the editor's */
typedef int (*EntryFn)(const Entry* e, void* user);
static int eachEntry(const char* source, const char* room, EntryFn fn, void* user) {
    const Store* stores[3];
    int s, i;
    stores[0] = &sBuiltin;
    stores[1] = sUse >= 0 ? &sStores[sUse] : NULL;
    stores[2] = sLiveOn ? &sLive : NULL;
    for (s = 0; s < 3; s++) {
        for (i = 0; stores[s] != NULL && i < stores[s]->n; i++) {
            const Entry* e = &stores[s]->e[i];
            if (strcmp(e->room, room) != 0 || (source != NULL && strcmp(e->source, source) != 0)) {
                continue;
            }
            if (s == 1 && liveCovers(e)) {
                continue;
            }
            if (fn(e, user)) {
                return 1;
            }
        }
    }
    return 0;
}

typedef struct CountCtx {
    int want, n;
    const PortMapThing* found;
} CountCtx;

static int countFn(const Entry* e, void* user) {
    CountCtx* c = (CountCtx*)user;
    if (c->n++ == c->want) {
        c->found = &e->t;
        return 1;
    }
    return 0;
}

int portMapEditsCount(const char* source, const char* room) {
    CountCtx c = {-1, 0, NULL};
    eachEntry(source, room, countFn, &c);
    return c.n;
}

const PortMapThing* portMapEditsGet(const char* source, const char* room, int i) {
    CountCtx c = {i, 0, NULL};
    eachEntry(source, room, countFn, &c);
    return c.found;
}

void portMapEditsSetLive(const char* source, const char* room, const PortMapThing* things, int count) {
    int i;
    sLive.n = 0;
    sLiveOn = source != NULL && room != NULL;
    if (sLiveOn) {
        snprintf(sLiveSource, sizeof(sLiveSource), "%s", source);
        snprintf(sLiveRoom, sizeof(sLiveRoom), "%s", room);
        for (i = 0; i < count; i++) {
            storeAdd(&sLive, source, room, &things[i]);
        }
    }
    portMapEditsRespawn();
}

const char* portMapEditsLastError(void) {
    return sError;
}

static void fail(const char* source, const char* what) {
    snprintf(sError, sizeof(sError), "%s: %s", source, what);
    printf("[map edits] %s\n", sError);
    fflush(stdout);
}

/* --- the room's slots --- */

#define SLOTS 128
#define CODE 72

typedef struct Slot {
    int used;
    PortMapThing t;      /* a copy: its strings stay while the room does */
    char source[64];
    char name[16];       /* the mobj's, the item's, the NPC's */
    char hit[24];        /* its collision's (mobjdrv's naming: "MOBJ_" + name) */
    char tag[48];        /* its words' message tag */
    EvtWord code[CODE];  /* the script that makes it */
    EvtWord act[CODE];   /* its action (a switch's, a sign's, a talk) */
    NpcSetupInfo setup[2];
    s32 caseId;          /* a pipe's case event (0 none) */
    s32 evtId;           /* a zone's watching event (0 none) */
    s32 placeId;         /* the script making it, while it may still run (its code is the slot's) */
} Slot;

static Slot sSlots[SLOTS];
static int sGen;          /* names' generation: placed again, named anew */
static s32 sPlacedFor;    /* the room load (its init script's event id) things were placed for */
static char sPlacedRoom[16];
static int sRespawnAsked;
static int sShopSlot = -1; /* the NPC whose shop is open */

/* a free slot, -1 if none */
static int slotFree(void) {
    int i;
    for (i = 0; i < SLOTS; i++) {
        if (!sSlots[i].used) {
            return i;
        }
    }
    return -1;
}

static int slotOfName(const char* name) {
    int i;
    for (i = 0; name != NULL && i < SLOTS; i++) {
        if (sSlots[i].used && strcmp(sSlots[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* the thing there now (the save's state) */
static int unlocked(const PortMapThing* t) {
    const int story = swByteGet(GSW_STORY_PROGRESS);
    if (t->setting >= 0 && !portRemakeMap(t->setting)) {
        return 0;
    }
    if ((t->story > 0 && story < t->story) || (t->storyBelow > 0 && story >= t->storyBelow)) {
        return 0;
    }
    if ((t->gswf >= 0 && !swGet(t->gswf)) || (t->gswfOff >= 0 && swGet(t->gswfOff))) {
        return 0;
    }
    if (t->gsw >= 0 && swByteGet(t->gsw) < t->gswMin) {
        return 0;
    }
    /* an item already taken */
    if (t->type == PORT_MT_ITEM && t->flagKind == PORT_MT_FLAG_GSWF && swGet(t->flagId)) {
        return 0;
    }
    return 1;
}

/* the flag word a mobj or an item takes (a GSWF or LSWF, as the game's scripts write them) */
static EvtWord flagWord(const PortMapThing* t, EvtWord none) {
    switch (t->flagKind) {
        case PORT_MT_FLAG_GSWF:
            return GSWF(t->flagId);
        case PORT_MT_FLAG_LSWF:
            return LSWF(t->flagId);
        default:
            return none;
    }
}

/* a script being built */
typedef struct Code {
    EvtWord* w;
    int n;
} Code;

static void op(Code* c, int opcode, int argc, const EvtWord* args) {
    int i;
    if (c->n + argc + 3 >= CODE) {
        return;
    }
    c->w[c->n++] = EVT_CMD(argc, opcode);
    for (i = 0; i < argc; i++) {
        c->w[c->n++] = args[i];
    }
}

/* CALL(fn, args...) */
static void call(Code* c, void* fn, int argc, const EvtWord* args) {
    EvtWord all[16];
    int i;
    all[0] = (EvtWord)fn;
    for (i = 0; i < argc && i < 15; i++) {
        all[i + 1] = args[i];
    }
    op(c, OPCODE_USER_FUNC, argc + 1, all);
}

static void call0(Code* c, void* fn) {
    call(c, fn, 0, NULL);
}

static void call1(Code* c, void* fn, EvtWord a) {
    call(c, fn, 1, &a);
}

static void end(Code* c) {
    c->w[c->n++] = EVT_CMD(0, OPCODE_END_EVENT);
    c->w[c->n++] = OPCODE_END_SCRIPT;
}

/* the words' tag: the thing's own (text), or its words made a tag of the set's own */
static const char* wordsTag(Slot* s, int index) {
    if (s->t.text[0] != '\0') {
        return s->t.text;
    }
    if (s->t.say[0] == '\0') {
        return NULL;
    }
    {
        unsigned h = 2166136261u;
        const char* p;
        for (p = s->source; *p; p++) {
            h = (h ^ (unsigned char)*p) * 16777619u;
        }
        snprintf(s->tag, sizeof(s->tag), "me_%08x_%s_%d", h, gp->currentMapName, index);
    }
    {
        /* (ends with a wait for A, as the game's own lines) */
        char text[300];
        const size_t len = strlen(s->t.say);
        snprintf(text, sizeof(text), "%s%s", s->t.say, len >= 3 && strcmp(s->t.say + len - 3, "<k>") == 0 ? "" : "<k>");
#ifdef PORT_MODS
        /* laid out as the game's (its font never wraps; its <p> needs a line break and a <k> before it) */
        portModsSetMessage(s->tag, portOpenSpeech(text));
#else
        (void)text;
#endif
    }
    return s->tag;
}

/* --- the actions --- */

static int slotOfEvent(EventEntry* event) {
    const int i = (int)evtGetValue(event, event->args[0]);
    return i >= 0 && i < SLOTS && sSlots[i].used ? i : -1;
}

/* (slot): its "uses" byte written (a jump stand thrown, a pipe gone down, a zone gone through) */
static USER_FUNC(evt_me_used) {
    const int i = slotOfEvent(event);
    (void)isFirstCall;
    if (i >= 0 && sSlots[i].t.usesGsw >= 0) {
        swByteSet(sSlots[i].t.usesGsw, sSlots[i].t.usesValue);
    }
    return EVT_RETURN_DONE;
}

/* (slot, case id): a pipe's case event, kept to take it away again */
static USER_FUNC(evt_me_case_id) {
    const int i = slotOfEvent(event);
    (void)isFirstCall;
    if (i >= 0) {
        sSlots[i].caseId = (s32)evtGetValue(event, event->args[1]);
    }
    return EVT_RETURN_DONE;
}

static s32 pipeMain(MapObjectEntry* mobj) {
    (void)mobj;
    return 0;
}

/* (name, x, y, z): the pipe, once its model is in */
static USER_FUNC(evt_me_pipe_entry) {
    EvtWord* args = event->args;
    const char* name = (const char*)evtGetValue(event, args[0]);
    static const char kModel[] = "MOBJ_GreenPipe_ks";
    MapObjectEntry* mobj;
    (void)isFirstCall;
    if (!animGroupBaseAsync(kModel, 0, NULL)) {
        return EVT_RETURN_BLOCK;
    }
    if (mobjNameToPtrNoAssert(name) != NULL) {
        return EVT_RETURN_DONE;
    }
    mobjEntry(name, kModel);
    mobj = mobjNameToPtr(name);
    mobj->flags |= MOBJ_FLAG_CALC_MTX;
    mobj->position.x = evtGetFloat(event, args[1]);
    mobj->position.y = evtGetFloat(event, args[2]);
    mobj->position.z = evtGetFloat(event, args[3]);
    mobj->mainFunc = pipeMain;
    mobjCalcMtx(mobj);
    animPoseSetAnim(mobj->poseId, "S_1", 1);
    return EVT_RETURN_DONE;
}

/* Down the pipe: LW(12) its slot, LW(13), LW(14) the room and entrance (the
 * case event's copy of the placing script's). As the bero scripts' pipes,
 * without their entrance table. */
static EVT_BEGIN(me_pipe_down)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_mario_wait_landon)
    CALL(evt_mario_cont_onoff, 0)
    CALL(evt_npc_stop_for_event)
    CALL(evt_mario_cam_off)
    CALL(evt_me_used, LW(12))
    CALL(evt_mario_get_pos, 0, LW(0), LW(1), LW(2))
    CALL(evt_snd_sfxon_3d, STRING("SFX_MARIO_DOKAN1"), LW(0), LW(1), LW(2), 0)
    CALL(evt_mario_paper_pose_dokan, 3)
    LOOP(20)
        SUBTRACT(LW(1), 3)
        CALL(evt_mario_set_pos, LW(0), LW(1), LW(2))
        WAIT_FRAMES(1)
    END_LOOP
    CALL(evt_fade_set_mapchange_type, 1, FADE_PIPE_DOWN_A1, 0, -1, -1)
    CALL(evt_fade_set_mapchange_type, 0, FADE_PIPE_DOWN_A1, 0, -1, -1)
    CALL(evt_bero_mapchange, LW(13), LW(14))
    RETURN
EVT_END()

/* LW(0) name, LW(1..3) where, LW(5) its collision's name, LW(12) its slot, LW(13/14) where it goes */
static EVT_BEGIN(me_place_pipe)
    CALL(evt_me_pipe_entry, LW(0), LW(1), LW(2), LW(3))
    CALL(evt_run_case_evt, CASE_COND_STAND_ON_STICK_DOWN, 0, LW(5), 0, PTR(me_pipe_down), LW(6))
    CALL(evt_me_case_id, LW(12), LW(6))
    RETURN
EVT_END()

static EVT_BEGIN(me_place_save)
    CALL(evt_mobj_save_blk, LW(0), LW(1), LW(2), LW(3), 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(me_place_heart)
    CALL(evt_mobj_recovery_blk, LW(0), LW(4), LW(1), LW(2), LW(3), 0, 0)
    RETURN
EVT_END()

/* A jump stand's event as it throws Mario: over to its room (its slot by the mobj's name) */
static USER_FUNC(evt_me_jump_dest) {
    const MapObjectEntry* mobj = event->thisMapObj;
    const int i = mobj != NULL ? slotOfName(mobj->name) : -1;
    (void)isFirstCall;
    event->lwData[0] = 0;
    if (i >= 0 && sSlots[i].t.to[0] != '\0') {
        event->lwData[13] = (EvtWord)sSlots[i].t.to;
        event->lwData[14] = (EvtWord)sSlots[i].t.entrance;
        event->lwData[0] = 1;
        if (sSlots[i].t.usesGsw >= 0) {
            swByteSet(sSlots[i].t.usesGsw, sSlots[i].t.usesValue);
        }
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(me_jump_away)
    CALL(evt_me_jump_dest)
    IF_INT_EQUAL(LW(0), 0)
        RETURN
    END_IF
    WAIT_MS(450)
    CALL(evt_bero_mapchange, LW(13), LW(14))
    RETURN
EVT_END()

static void place(int slot);

/* (slot): a switch pressed: the things it shows placed now */
static USER_FUNC(evt_me_shows) {
    const int i = slotOfEvent(event);
    int k, j;
    (void)isFirstCall;
    if (i < 0) {
        return EVT_RETURN_DONE;
    }
    for (k = 0; k < PORT_MT_SHOWS_MAX; k++) {
        const char* id = sSlots[i].t.shows[k];
        if (id[0] == '\0') {
            continue;
        }
        for (j = 0; j < SLOTS; j++) {
            Slot* s = &sSlots[j];
            /* (a hidden thing has a slot with no name until shown) */
            if (s->used && s->name[0] == '\0' && strcmp(s->t.id, id) == 0) {
                place(j);
            }
        }
    }
    return EVT_RETURN_DONE;
}

/* (slot, out): a zone: Mario in it, and his to move */
static USER_FUNC(evt_me_zone_check) {
    const int i = slotOfEvent(event);
    int in = 0;
    (void)isFirstCall;
    if (i >= 0 && marioGetPtr() != NULL && marioChkKey() && marioChkCtrl()) {
        const PortMapThing* t = &sSlots[i].t;
        const Vec* p = &marioGetPtr()->position;
        const float r = t->radius > 0.0f ? t->radius : 40.0f;
        const float dx = p->x - t->pos[0], dz = p->z - t->pos[2], dy = p->y - t->pos[1];
        in = dx * dx + dz * dz <= r * r && dy > -30.0f && dy < 80.0f;
    }
    evtSetValue(event, event->args[1], in);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(me_zone)
    LABEL(0)
    WAIT_FRAMES(1)
    CALL(evt_me_zone_check, LW(12), LW(0))
    IF_INT_EQUAL(LW(0), 0)
        GOTO(0)
    END_IF
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_me_used, LW(12))
    CALL(evt_bero_mapchange, LW(13), LW(14))
    RETURN
EVT_END()

/* (slot): the shop's items from now (-1: none) */
static USER_FUNC(evt_me_shop) {
    const int i = (int)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    sShopSlot = i >= 0 && i < SLOTS && sSlots[i].used && sSlots[i].t.shopCount > 0 ? i : -1;
    return EVT_RETURN_DONE;
}

/* an item id the item table has an entry for now (the game's, or the port's filled ones: a PM64 one with no
   ROM linked has none). A map edit's are range-checked as they're read (map_edits.cpp); this is the last word
   before itemEntry or a shop indexes the table */
static int knownItem(int id) {
    return id > 0 && id < PORT_ITEM_TABLE_SIZE && (id < ITEM_TYPE_MAX || id >= PORT_ITEM_MIN) &&
           itemDataTable[id].name != NULL;
}

int portMapEditsShopStock(int* out, int max) {
    int i, n = 0;
    if (sShopSlot < 0 || !sSlots[sShopSlot].used) {
        return -1;
    }
    for (i = 0; i < sSlots[sShopSlot].t.shopCount && n < max; i++) {
        if (knownItem(sSlots[sShopSlot].t.shop[i])) { /* (one the table hasn't: left out of the stock) */
            out[n++] = sSlots[sShopSlot].t.shop[i];
        }
    }
    return n;
}

#define SAY(which)                         \
    CALL(portEvtRemakeText, which, LW(10)) \
    CALL(evt_msg_print, MSG_PRINT_DIRECT, LW(10), 0, STRING("me"))

/* a shop NPC's talk after its words (the Grotto Toad's window, the Toad's lines) */
static EVT_BEGIN(me_shop_loop)
    LABEL(0)
    CALL(evt_win_coin_on, 0, LW(12))
    CALL(evt_win_other_select, PORT_SELECT_REMAKE_TOAD)
    IF_INT_EQUAL(LW(0), 0)
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_TOAD_BYE)
        RETURN
    END_IF
    CALL(evt_pouch_get_coin, LW(0))
    IF_INT_LESS(LW(0), LW(3))
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_TOAD_POOR)
        GOTO(0)
    END_IF
    CALL(evt_pouch_add_item, LW(1), LW(0))
    IF_INT_EQUAL(LW(0), -1)
        CALL(evt_win_coin_off, LW(12))
        SAY(PORT_RM_TEXT_TOAD_FULL)
        RETURN
    END_IF
    MULTIPLY(LW(3), -1)
    CALL(evt_pouch_add_coin, LW(3))
    CALL(evt_win_coin_wait, LW(12))
    CALL(evt_win_coin_off, LW(12))
    SAY(PORT_RM_TEXT_TOAD_THANKS)
    GOTO(0)
    RETURN
EVT_END()
#undef SAY

/* LW(0) name, LW(1) model, LW(2) tribe (NULL none), LW(3..5) where, LW(6) facing, LW(7) setup */
static EVT_BEGIN(me_npc_spawn)
    CALL(evt_npc_entry, LW(0), LW(1))
    IF_INT_NOT_EQUAL(LW(2), 0)
        CALL(evt_npc_set_tribe, LW(0), LW(2))
    END_IF
    CALL(evt_npc_set_position, LW(0), LW(3), LW(4), LW(5))
    CALL(evt_npc_set_ry, LW(0), LW(6))
    CALL(evt_npc_setup, LW(7))
    CALL(evt_npc_set_anim, LW(0), STRING("S_1"))
    RETURN
EVT_END()

/* (type, name): a room's own thing taken away, if it's there */
static USER_FUNC(evt_me_remove) {
    const int type = (int)evtGetValue(event, event->args[0]);
    const char* name = (const char*)evtGetValue(event, event->args[1]);
    (void)isFirstCall;
    if (type == PORT_MT_REMOVE_MOBJ && mobjNameToPtrNoAssert(name) != NULL) {
        mobjDelete(name);
    } else if (type == PORT_MT_REMOVE_NPC) {
        NpcEntry* npc = npcNameToPtr_NoAssert(name);
        if (npc != NULL) {
            npcDelete(npc);
        }
    } else if (type == PORT_MT_REMOVE_ITEM) {
        ItemWork* wp = portItemGetWork();
        int i;
        for (i = 0; wp != NULL && i < wp->count; i++) {
            if ((wp->entries[i].flags & ITEM_FLAG_USED) && strcmp(wp->entries[i].name, name) == 0) {
                itemDelete(name);
                break;
            }
        }
    }
    return EVT_RETURN_DONE;
}

/* a tribe whose model is the NPC's (its animations' names), its name as the game's; NULL none */
static const char* tribeOf(const char* model) {
    const NpcTribeInfo* t;
    for (t = npcTribe; t->nameJp != NULL; t++) {
        if (t->modelName != NULL && strcmp(t->modelName, model) == 0) {
            return t->nameJp;
        }
    }
    return NULL;
}

/* the slot's thing put down now (a hidden one: when shown) */
static void place(int slot) {
    Slot* s = &sSlots[slot];
    PortMapThing* t = &s->t;
    const char* tag;
    Code c = {s->code, 0};
    Code a = {s->act, 0};
    EvtWord args[10];
    EventEntry* e = NULL;
    if ((t->type == PORT_MT_BLOCK || t->type == PORT_MT_BADGE_BLOCK || t->type == PORT_MT_BRICK ||
         t->type == PORT_MT_HIDDEN_BLOCK || t->type == PORT_MT_ITEM) &&
        t->arg != 0 && !knownItem(t->arg)) {
        printf("[mapedits] %s: item %d isn't one the game has: left out\n", t->id, t->arg);
        fflush(stdout);
        return;
    }
    snprintf(s->name, sizeof(s->name), "me%d_%d", sGen % 1000, slot);
    snprintf(s->hit, sizeof(s->hit), "MOBJ_%s", s->name);
    args[0] = (EvtWord)s->name;
    args[1] = (EvtWord)(s32)t->pos[0];
    args[2] = (EvtWord)(s32)t->pos[1];
    args[3] = (EvtWord)(s32)t->pos[2];
    switch (t->type) {
        case PORT_MT_PIPE:
            e = evtEntry(me_place_pipe, 0, 0);
            break;
        case PORT_MT_SAVE_BLOCK:
            e = evtEntry(me_place_save, 0, 0);
            break;
        case PORT_MT_HEART_BLOCK:
            e = evtEntry(me_place_heart, 0, 0);
            break;
        case PORT_MT_JUMP_STAND:
            args[0] = 0;
            args[1] = (EvtWord)s->name;
            args[2] = t->arg > 0 ? t->arg : 130;
            args[3] = (EvtWord)(s32)t->pos[0];
            args[4] = (EvtWord)(s32)t->pos[1];
            args[5] = (EvtWord)(s32)t->pos[2];
            args[6] = t->to[0] != '\0' ? PTR(me_jump_away) : 0;
            args[7] = 0;
            call(&c, evt_mobj_jumpstand_blue, 8, args);
            break;
        case PORT_MT_BLOCK:
        case PORT_MT_BADGE_BLOCK:
            args[4] = t->arg;
            args[5] = 0;
            args[6] = flagWord(t, 0);
            args[7] = t->type == PORT_MT_BADGE_BLOCK ? 1 : 0;
            call(&c, evt_mobj_badgeblk, 8, args);
            break;
        case PORT_MT_BRICK:
        case PORT_MT_HIDDEN_BLOCK:
            args[4] = t->arg != 0 || t->type == PORT_MT_BRICK ? t->arg : ITEM_COIN;
            args[5] = t->type == PORT_MT_HIDDEN_BLOCK ? 11 : 0;
            args[6] = 0;
            args[7] = flagWord(t, 0);
            call(&c, evt_mobj_brick, 8, args);
            break;
        case PORT_MT_SWITCH:
            call1(&a, evt_me_shows, slot);
            end(&a);
            args[0] = 0;
            args[1] = (EvtWord)s->name;
            args[2] = (EvtWord)(s32)t->pos[0];
            args[3] = (EvtWord)(s32)t->pos[1];
            args[4] = (EvtWord)(s32)t->pos[2];
            args[5] = PTR(s->act);
            args[6] = flagWord(t, 0);
            call(&c, evt_mobj_switch_blue, 7, args);
            break;
        case PORT_MT_SIGN:
            tag = wordsTag(s, slot);
            call0(&a, evt_npc_stop_for_event);
            call1(&a, evt_mario_key_onoff, 0);
            call0(&a, evt_mario_normalize);
            if (tag != NULL) {
                EvtWord m[4] = {0, (EvtWord)tag, 0, 0};
                call(&a, evt_msg_print, 4, m);
            }
            call1(&a, evt_mario_key_onoff, 1);
            call0(&a, evt_npc_start_for_event);
            end(&a);
            args[4] = PTR(s->act);
            args[5] = 0;
            call(&c, evt_mobj_signboard, 6, args);
            break;
        case PORT_MT_ITEM:
            args[0] = (EvtWord)s->name;
            args[1] = t->arg;
            args[2] = (EvtWord)(s32)t->pos[0];
            args[3] = (EvtWord)(s32)t->pos[1];
            args[4] = (EvtWord)(s32)t->pos[2];
            args[5] = ITEM_ENTRY_STAY;
            args[6] = flagWord(t, -1);
            args[7] = 0;
            call(&c, evt_item_entry, 8, args);
            break;
        case PORT_MT_NPC: {
            /* its talk: the words, then the shop's window */
            tag = wordsTag(s, slot);
            if (t->shopCount > 0) {
                call1(&a, evt_me_shop, slot);
            }
            if (tag != NULL) {
                EvtWord m[4] = {0, (EvtWord)tag, 0, (EvtWord) "me"};
                call(&a, evt_msg_print, 4, m);
            }
            if (t->shopCount > 0) {
                EvtWord child = PTR(me_shop_loop);
                op(&a, OPCODE_RUN_CHILD_EVENT, 1, &child);
                call1(&a, evt_me_shop, -1);
            }
            end(&a);
            memset(s->setup, 0, sizeof(s->setup));
            s->setup[0].name = s->name;
            s->setup[0].flags = NPC_FLAG_SHADOW_NO_COLLISION | NPC_FLAG_MOVE_ANIM | NPC_FLAG_TALK_ANIM;
            s->setup[0].talkEvent = tag != NULL || t->shopCount > 0 ? s->act : NULL;
            e = evtEntry(me_npc_spawn, 0, 0);
            e->lwData[0] = (EvtWord)s->name;
            e->lwData[1] = (EvtWord)t->model;
            e->lwData[2] = (EvtWord)tribeOf(t->model);
            e->lwData[3] = (EvtWord)(s32)t->pos[0];
            e->lwData[4] = (EvtWord)(s32)t->pos[1];
            e->lwData[5] = (EvtWord)(s32)t->pos[2];
            e->lwData[6] = (EvtWord)(s32)t->ry;
            e->lwData[7] = (EvtWord)s->setup;
            s->placeId = e->eventId;
            printf("[map edits] %s: npc %s\n", gp->currentMapName, t->model);
            fflush(stdout);
            return;
        }
        case PORT_MT_ZONE:
            e = evtEntry(me_zone, 0, 0);
            s->evtId = e->eventId;
            break;
        default:
            return;
    }
    if (c.n > 0) {
        end(&c);
        e = evtEntry(s->code, 0, 0);
    }
    if (e == NULL) {
        return;
    }
    if (t->type != PORT_MT_ZONE) {
        s->placeId = e->eventId;
    }
    e->lwData[0] = (EvtWord)s->name;
    e->lwData[1] = (EvtWord)(s32)t->pos[0];
    e->lwData[2] = (EvtWord)(s32)t->pos[1];
    e->lwData[3] = (EvtWord)(s32)t->pos[2];
    e->lwData[4] = t->arg;
    e->lwData[5] = (EvtWord)s->hit;
    e->lwData[12] = slot;
    e->lwData[13] = (EvtWord)t->to;
    e->lwData[14] = (EvtWord)t->entrance;
    printf("[map edits] %s: %s%s%s\n", gp->currentMapName, portMapThingTypeName(t->type),
           t->to[0] != '\0' ? " to " : "", t->to);
    fflush(stdout);
}

/* a change to the room's own things, now (their slot keeps it, to put back) */
static void change(int slot, int undo) {
    Slot* s = &sSlots[slot];
    const PortMapThing* t = &s->t;
    switch (t->type) {
        case PORT_MT_HIDE_COLLISION:
            if (hitNameToPtr(t->target) != NULL) {
                undo ? hitObjFlagOff(t->target, HIT_FLAG_OFF) : hitObjFlagOn(t->target, HIT_FLAG_OFF);
            } else if (!undo) {
                fail(s->source, "hide_collision: no such collision object in the room");
            }
            break;
        case PORT_MT_HIDE_MODEL:
            if (mapGetMapObj(t->target) != NULL) {
                undo ? mapGrpFlagOff(t->target, MAPOBJ_FLAG_HIDE) : mapGrpFlagOn(t->target, MAPOBJ_FLAG_HIDE);
            } else if (!undo) {
                fail(s->source, "hide_model: no such model in the room");
            }
            break;
        case PORT_MT_REMOVE_MOBJ:
        case PORT_MT_REMOVE_NPC:
        case PORT_MT_REMOVE_ITEM:
            if (!undo) {
                /* (after the room's own scripts have made theirs) */
                Code c = {s->code, 0};
                EvtWord w = 2, args[2] = {t->type, (EvtWord)t->target};
                op(&c, OPCODE_WAIT_FRAME, 1, &w);
                call(&c, evt_me_remove, 2, args);
                end(&c);
                evtEntry(s->code, 0, 0);
            }
            break;
        default:
            break;
    }
}

/* the slot's placed thing taken away */
static void unplace(int slot) {
    Slot* s = &sSlots[slot];
    if (!s->used) {
        return;
    }
    if (portMapThingPlaced(s->t.type) && s->name[0] != '\0') {
        if (s->caseId != 0) {
            caseDelete(s->caseId);
        }
        if (s->placeId != 0 && evtCheckID(s->placeId)) {
            evtDeleteID(s->placeId); /* (still waiting for its model: its code goes with the slot) */
        }
        if (s->evtId != 0 && evtCheckID(s->evtId)) {
            evtDeleteID(s->evtId);
        }
        if (s->t.type == PORT_MT_NPC) {
            NpcEntry* npc = npcNameToPtr_NoAssert(s->name);
            if (npc != NULL) {
                npcDelete(npc);
            }
        } else if (s->t.type == PORT_MT_ITEM) {
            ItemWork* wp = portItemGetWork();
            int i;
            for (i = 0; wp != NULL && i < wp->count; i++) {
                if ((wp->entries[i].flags & ITEM_FLAG_USED) && strcmp(wp->entries[i].name, s->name) == 0) {
                    itemDelete(s->name);
                    break;
                }
            }
        } else if (s->t.type != PORT_MT_ZONE && mobjNameToPtrNoAssert(s->name) != NULL) {
            mobjDelete(s->name);
        }
    } else {
        change(slot, 1);
    }
    if (sShopSlot == slot) {
        sShopSlot = -1;
    }
    memset(s, 0, sizeof(*s));
}

/* a thing into a slot, placed (or kept for a switch) */
static int take(const char* source, const PortMapThing* t) {
    const int i = slotFree();
    Slot* s;
    if (i < 0) {
        fail(source, "too many things in the room (128 at most)");
        return 0;
    }
    s = &sSlots[i];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->t = *t;
    snprintf(s->source, sizeof(s->source), "%s", source);
    if (portMapThingPlaced(t->type)) {
        if (!t->hidden) {
            place(i);
        }
    } else {
        change(i, 0);
    }
    return 1;
}

/* exits: the entrances pointed elsewhere, with the game's own destinations to
 * put back (in this table only, and only where ours is still there: an
 * entry of a REL since unloaded is never written) */
typedef struct Exit {
    BeroEntry* e;
    const char* map;  /* the game's */
    const char* bero;
    const char* ourMap; /* ours: to */
    char to[16], entrance[24];
} Exit;
#define EXITS 64
static Exit sExits[EXITS];
static int sExitCount;

static int exitFn(const Entry* en, void* user) {
    BeroEntry* table = (BeroEntry*)user;
    BeroEntry* e;
    if (en->t.type != PORT_MT_EXIT || !unlocked(&en->t)) {
        return 0;
    }
    for (e = table; e->hitName != NULL; e++) {
        if (strcmp(e->hitName, en->t.target) == 0) {
            if (sExitCount < EXITS) {
                Exit* x = &sExits[sExitCount++];
                x->e = e;
                x->map = e->nextMapName;
                x->bero = e->nextBeroName;
                snprintf(x->to, sizeof(x->to), "%s", en->t.to);
                snprintf(x->entrance, sizeof(x->entrance), "%s", en->t.entrance);
                x->ourMap = x->to;
                e->nextMapName = x->to;
                e->nextBeroName = x->entrance;
            }
            return 0;
        }
    }
    fail(en->source, "exit: no such entrance in the room");
    return 0;
}

static BeroEntry* sTable; /* the room's entrances (the last table asked for) */

static void exits(BeroEntry* table) {
    BeroEntry* e;
    int i;
    if (table == NULL) {
        return;
    }
    sTable = table;
    for (i = sExitCount - 1; i >= 0; i--) {
        Exit* x = &sExits[i];
        int inTable = 0;
        for (e = table; e->hitName != NULL; e++) {
            if (e == x->e) {
                inTable = 1;
                break;
            }
        }
        if (!inTable) {
            continue;
        }
        if (x->e->nextMapName == x->ourMap) {
            x->e->nextMapName = x->map;
            x->e->nextBeroName = x->bero;
        }
        sExits[i] = sExits[--sExitCount];
    }
    eachEntry(NULL, gp->currentMapName, exitFn, table);
}

static int placeFn(const Entry* en, void* user) {
    (void)user;
    if (en->t.type != PORT_MT_EXIT && unlocked(&en->t)) {
        take(en->source, &en->t);
    }
    return 0;
}

static void placeRoom(void) {
    eachEntry(NULL, gp->currentMapName, placeFn, NULL);
}

/* Lua's map.place for a room still loading: kept until the room's things are placed */
#define PENDING 16
static PortMapThing sPending[PENDING];
static int sPendingCount;
static char sPendingRoom[sizeof(gp->currentMapName)];

static void takePending(void) {
    int i;
    if (strcmp(sPendingRoom, gp->currentMapName) == 0) {
        for (i = 0; i < sPendingCount; i++) {
            take("lua", &sPending[i]);
        }
    }
    sPendingCount = 0;
    sPendingRoom[0] = '\0';
}

void portMapEditsRoom(struct BeroEntry* table) {
    int i;
    if (gp->inBattle) {
        return;
    }
    exits(table);
    /* (placed once a room load: a room's scripts may ask for its entrances
     * again before what the first call started has made its mobj) */
    if (gp->mapEventId != 0 && gp->mapEventId == sPlacedFor) {
        return;
    }
    sPlacedFor = gp->mapEventId;
    snprintf(sPlacedRoom, sizeof(sPlacedRoom), "%s", gp->currentMapName);
    /* the last room's slots: its things went with it */
    for (i = 0; i < SLOTS; i++) {
        sSlots[i].used = 0;
    }
    sShopSlot = -1;
    sGen++;
    sError[0] = '\0';
    placeRoom();
    takePending();
}

int portMapEditsPlaceNow(const PortMapThing* thing) {
    if (gp == NULL || gp->inBattle || gp->currentMapName[0] == '\0') {
        return 0;
    }
    if (strcmp(sPlacedRoom, gp->currentMapName) != 0) {
        /* the room still loading (Lua's map_load comes before its init script
         * asks for its entrances): placed with its own things */
        if (strcmp(sPendingRoom, gp->currentMapName) != 0) {
            snprintf(sPendingRoom, sizeof(sPendingRoom), "%s", gp->currentMapName);
            sPendingCount = 0;
        }
        if (sPendingCount >= PENDING) {
            fail("lua", "too many things placed before the room is in");
            return 0;
        }
        sPending[sPendingCount++] = *thing;
        return 1;
    }
    return take("lua", thing);
}

int portMapEditsRemoveNow(const char* id) {
    int i, found = 0;
    for (i = 0; id != NULL && id[0] != '\0' && i < sPendingCount; i++) {
        if (strcmp(sPending[i].id, id) == 0) {
            sPending[i--] = sPending[--sPendingCount];
            found = 1;
        }
    }
    for (i = 0; id != NULL && id[0] != '\0' && i < SLOTS; i++) {
        if (sSlots[i].used && strcmp(sSlots[i].t.id, id) == 0) {
            unplace(i);
            found = 1;
        }
    }
    return found;
}

void portMapEditsRespawn(void) {
    sRespawnAsked = 1;
}

const char* portMapEditsRoomName(void) {
    return sPlacedRoom;
}

const char* portMapEditsEntrance(int i) {
    int k;
    if (sTable == NULL || gp == NULL || strcmp(sPlacedRoom, gp->currentMapName) != 0) {
        return NULL;
    }
    for (k = 0; sTable[k].hitName != NULL; k++) {
        if (k == i) {
            return sTable[k].hitName;
        }
    }
    return NULL;
}

int portMapEditsCast(const float origin[3], const float dir[3], float dist, float out[3]) {
    f32 hx, hy, hz, d = dist, nx, ny, nz;
    if (gp == NULL || gp->inBattle) {
        return 0;
    }
    if (hitCheckFilter(NULL, &hx, &hy, &hz, &d, &nx, &ny, &nz, origin[0], origin[1], origin[2], dir[0], dir[1], dir[2]) ==
        NULL) {
        return 0;
    }
    out[0] = hx;
    out[1] = hy;
    out[2] = hz;
    return 1;
}

int portMapEditsFloor(float p[3]) {
    const float from[3] = {p[0], p[1] + 40.0f, p[2]}, down[3] = {0.0f, -1.0f, 0.0f};
    float hit[3];
    if (!portMapEditsCast(from, down, 3000.0f, hit)) {
        return 0;
    }
    p[1] = hit[1];
    return 1;
}

int portMapEditsMario(float out[3]) {
    if (gp == NULL || gp->inBattle || marioGetPtr() == NULL) {
        return 0;
    }
    out[0] = marioGetPtr()->position.x;
    out[1] = marioGetPtr()->position.y;
    out[2] = marioGetPtr()->position.z;
    return 1;
}

const char* portMapEditsModel(int i) {
    static const char* sModels[512];
    static int sCount = -1;
    if (sCount < 0) {
        const NpcTribeInfo* t;
        sCount = 0;
        for (t = npcTribe; t->nameJp != NULL && sCount < 512; t++) {
            int k, seen = 0;
            if (t->modelName == NULL || t->modelName[0] == 0) {
                continue;
            }
            for (k = 0; k < sCount && !seen; k++) {
                seen = strcmp(sModels[k], t->modelName) == 0;
            }
            if (!seen) {
                sModels[sCount++] = t->modelName;
            }
        }
    }
    return i >= 0 && i < sCount ? sModels[i] : NULL;
}

static int sEditorAsked;

void portMapEditorRequest(void) {
    sEditorAsked = 1;
}

int portMapEditorTakeRequest(void) {
    if (sEditorAsked && gp != NULL && !gp->inBattle && marioGetPtr() != NULL && marioChkKey() && marioChkCtrl()) {
        sEditorAsked = 0;
        return 1;
    }
    return 0;
}

static int sHoldWanted, sHeld;

void portMapEditsHold(int on) {
    sHoldWanted = on;
}

/* each frame's controllers (DEMOPad.c): while the editor holds the game, they are the
 * editor's (its keys are the game's too: E is B, R is X, F is Y, Return is Start), so
 * the game gets nothing; what is held as it lets go stays the editor's until let go
 * (B closing it would swing the hammer) */
void portMapEditsPad(PADStatus* pads) {
    static u16 sSwallow;
    u16 still = 0;
    int i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (pads[i].err != PAD_ERR_NONE) {
            continue;
        }
        if (sHoldWanted) {
            still |= pads[i].button;
            pads[i].button = 0;
            pads[i].stickX = pads[i].stickY = pads[i].substickX = pads[i].substickY = 0;
            pads[i].triggerLeft = pads[i].triggerRight = pads[i].analogA = pads[i].analogB = 0;
        } else {
            still |= (u16)(pads[i].button & sSwallow);
            pads[i].button &= (u16)~sSwallow;
        }
    }
    sSwallow = still;
}

/* each game frame, before the game's: Mario held for the editor, the respawn asked for (its changes) */
static int framePre(void) {
    int i;
    if (sHoldWanted != sHeld && gp != NULL && !gp->inBattle) {
        if (sHoldWanted) {
            marioKeyOff();
            npcStopForEvent();
        } else {
            marioKeyOn();
            npcStartForEvent();
        }
        sHeld = sHoldWanted;
    }
    if (!sRespawnAsked || gp == NULL || gp->inBattle || strcmp(sPlacedRoom, gp->currentMapName) != 0) {
        return 0;
    }
    sRespawnAsked = 0;
    for (i = 0; i < SLOTS; i++) {
        unplace(i);
    }
    sGen++;
    sError[0] = '\0';
    placeRoom();
    return 0;
}

#include "port/hook_list.h"
#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

void portMapEditsInit(void) {
#ifdef PORT_MODS
    portModHostAddHook(0, kSlot_marioStMain, (void*)framePre);
#else
    (void)framePre;
#endif
}
