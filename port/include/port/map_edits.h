#ifndef PORT_MAP_EDITS_H
#define PORT_MAP_EDITS_H

/* Map edits (docs/MAP_EDITOR.md): things placed into a room, and changes to
 * it, from data. A mod's maps/<room>.json (parsed by port/mods/map_edits.cpp),
 * the remake's map changes (a built-in set, port/src/common/remake_maps_pc.c),
 * a Lua mod's map.place, and the map editor's live edits. The store and the
 * room's runtime: port/src/common/map_edits_pc.c.
 *
 * Everything is placed from evt_bero_get_info, which every room's init
 * script calls (portRemakeMapsRoom, then portMapEditsRoom), once a room load,
 * from the data and the game's saved state only, so both games of an online
 * session place the same (a mod with maps/ is one every player needs, its
 * files' hash in the online hello: portModsOnlineList). */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_MT_PIPE,           /* a green pipe: down to (to, entrance) */
    PORT_MT_SAVE_BLOCK,
    PORT_MT_HEART_BLOCK,    /* arg: its price in coins */
    PORT_MT_JUMP_STAND,     /* arg: its power (0: 130); to/entrance: thrown over to another room */
    PORT_MT_BLOCK,          /* a ? block: arg the item */
    PORT_MT_BADGE_BLOCK,    /* a badge block: arg the item */
    PORT_MT_BRICK,          /* a brick block: arg the item (0 none) */
    PORT_MT_HIDDEN_BLOCK,   /* a hidden ? block: arg the item */
    PORT_MT_SWITCH,         /* a blue switch: flag set when pressed, shows its ids placed then */
    PORT_MT_SIGN,           /* a signboard: its text */
    PORT_MT_ITEM,           /* a field item: arg the item, flag set once taken */
    PORT_MT_NPC,            /* model (an animation group), its text; shop: a Toad's window */
    PORT_MT_ZONE,           /* a loading zone: radius, to/entrance */
    PORT_MT_HIDE_COLLISION, /* target: the room's collision object, off */
    PORT_MT_HIDE_MODEL,     /* target: the room's model, hidden */
    PORT_MT_REMOVE_MOBJ,    /* target: the room's mobj, deleted */
    PORT_MT_REMOVE_NPC,     /* target: the room's NPC, deleted */
    PORT_MT_REMOVE_ITEM,    /* target: the room's field item, deleted */
    PORT_MT_EXIT,           /* target: an entrance's hit name, sent to (to, entrance) */
    PORT_MT_COUNT
};

/* a thing's flag (switch pressed, item taken, block hit): none, GSWF, LSWF */
enum { PORT_MT_FLAG_NONE, PORT_MT_FLAG_GSWF, PORT_MT_FLAG_LSWF };
/* the highest of each (GlobalWork's gswf holds 8192 bits, its lswf 512: one past wrote into the area's byte
   words after it, saved with them) */
#define PORT_MT_GSWF_MAX 8191
#define PORT_MT_LSWF_MAX 511

#define PORT_MT_SHOP_MAX 8
#define PORT_MT_SHOWS_MAX 4

typedef struct PortMapThing {
    int type;        /* PORT_MT_* */
    char id[24];     /* its own name in the set ("" none): a switch's shows, Lua's map.remove */
    float pos[3];
    float ry;        /* facing, degrees (NPCs) */
    /* when it is there */
    int story;       /* GSW 0 at least (0: any) */
    int storyBelow;  /* GSW 0 below (0: any) */
    int gswf;        /* this GSWF set (-1: none) */
    int gswfOff;     /* this GSWF clear (-1: none) */
    int gsw;         /* this saved byte (GSW) at least gswMin (-1: none) */
    int gswMin;
    int setting;     /* the built-in remake set's: PORT_RM_* that must be on (-1: none) */
    int hidden;      /* only placed when a switch shows it */
    /* what it does */
    char to[16];       /* a room */
    char entrance[24]; /* an entrance there */
    char target[32];   /* the room's own thing it changes */
    int arg;           /* price, power, item */
    int flagKind;      /* PORT_MT_FLAG_* */
    int flagId;
    int usesGsw;       /* when used (a jump stand thrown, a pipe gone down): this GSW byte ... (-1 none) */
    int usesValue;     /* ... set to this */
    char model[32];    /* an NPC's animation group ("c_kinopio") */
    char text[48];     /* a message tag: an NPC's or a sign's words */
    char say[256];     /* or the words themselves (UTF-8), made a tag of the set's own */
    int shop[PORT_MT_SHOP_MAX];
    int shopCount;
    char shows[PORT_MT_SHOWS_MAX][24]; /* a switch's: the ids it places */
    float radius;      /* a zone's (0: 40) */
} PortMapThing;

/* A thing with nothing set (no conditions, no flag). */
void portMapThingInit(PortMapThing* t, int type);
/* The types by name ("pipe", "save_block"...), and back; -1 / "" for none. */
int portMapThingType(const char* name);
const char* portMapThingTypeName(int type);
/* The type places something you can see (not a change to the room's own). */
int portMapThingPlaced(int type);
/* A thing's numbers within what the game can take (port/mods/map_edits.cpp):
 * the ranges a maps/ file is read with (flags, items, saved bytes, the shop,
 * finite positions within a room). 1 if it is; else 0 and why (size bytes).
 * Every thing from outside (a maps/ file, Lua's map.place) goes through it. */
int portMapThingCheck(const PortMapThing* t, char* why, int size);

/* --- the store: sets of things, by room --- */

/* A built-in set's thing (the remake's: remake_maps_pc.c), kept for good. */
void portMapEditsAddBuiltin(const char* source, const char* room, const PortMapThing* thing);
/* A new store being filled (the mods' apply); the built-in sets stay. */
void portMapEditsBegin(void);
/* A set's thing for room (source: the mod's id, for the log and the tags). */
void portMapEditsAdd(const char* source, const char* room, const PortMapThing* thing);
/* The filled store in use from the next room (placed things stay till then). */
void portMapEditsCommit(void);
/* How many things a room has from source (NULL: every source), and one. */
int portMapEditsCount(const char* source, const char* room);
const PortMapThing* portMapEditsGet(const char* source, const char* room, int i);

/* --- the room --- */

struct BeroEntry;
/* evt_bero_get_info (by portRemakeMapsRoom): the room's entrances (exits
 * pointed elsewhere every call), its things (once a room load). */
void portMapEditsRoom(struct BeroEntry* table);
/* Each game frame (marioStMain): what was asked from outside the game's frame
 * (the editor's changes, Lua's) done in it. Hooked by portMapEditsInit. */
void portMapEditsInit(void);

/* Placed now, in the room now, besides its edits (Lua's map.place): 1 if it
 * was (a room in the field, a slot free). Gone at the next room. */
int portMapEditsPlaceNow(const PortMapThing* thing);
/* A thing placed now or from the edits, by its id, taken away: 1 if found. */
int portMapEditsRemoveNow(const char* id);

/* The editor: its own set for the room now, over the mods' for it while it
 * edits (source: the mod it saves to, whose set for the room it replaces;
 * NULL: off). The room's things placed again from them (in the next frame). */
void portMapEditsSetLive(const char* source, const char* room, const PortMapThing* things, int count);
/* The room's placed things taken away and placed again (in the next frame). */
void portMapEditsRespawn(void);
/* What the last placing said (an error, a thing it couldn't place), or "". */
const char* portMapEditsLastError(void);

/* For the editor: the room things were last placed for ("" none); the
 * room's entrances (its table's hit names, NULL past the last); a ray cast
 * on the room's collision (1 and where it hit, within dist); a point put down
 * on the floor under it (1 if there was one); Mario and the NPCs held still
 * (on) or let go, from the next game frame; Mario's feet. */
const char* portMapEditsRoomName(void);
const char* portMapEditsEntrance(int i);
int portMapEditsCast(const float origin[3], const float dir[3], float dist, float out[3]);
int portMapEditsFloor(float p[3]);
void portMapEditsHold(int on);
int portMapEditsMario(float out[3]);
/* Each frame's controllers (DEMOPad.c): nothing for the game while the editor
 * holds it (the editor's keys and buttons are its own) */
struct PADStatus;
void portMapEditsPad(struct PADStatus* pads);

/* The disc's NPC models (npcTribe's animation groups, each once), NULL past the last. */
const char* portMapEditsModel(int i);
/* The Start menu's Map editor row: asked for, taken (once) by the editor when
 * Mario has the controls again, the menu closed. */
void portMapEditorRequest(void);
int portMapEditorTakeRequest(void);

/* The shop window of a map edit's NPC (winmgr's PORT_SELECT_REMAKE_TOAD):
 * its items while one is talking, else -1. */
int portMapEditsShopStock(int* out, int max);

#ifdef __cplusplus
}
#include <string>
#include <vector>
/* port/mods/map_edits.cpp: a maps/ file read into things (room: the file's
 * "room", else its name's); errors (with the file's name) appended. */
bool portMapEditsParse(const std::string& text, const std::string& file, std::string& room,
                       std::vector<PortMapThing>& out, std::vector<std::string>& errors);
/* A room's things as a maps/ file's text. */
std::string portMapEditsWrite(const std::string& room, const std::vector<PortMapThing>& things);
#endif

#endif
