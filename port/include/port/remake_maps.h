#ifndef PORT_REMAKE_MAPS_H
#define PORT_REMAKE_MAPS_H
#include "port/save_words.h"
/* The remake's map changes (Enhancements > The remake's map changes;
 * port/src/common/remake_maps_pc.c, docs/REMAKE_MAPS.md): new pipes, Save and
 * Heart Blocks, shortcuts and stops, each its own setting, off by default.
 * Only the disc's own models (the field's mobjs), placed by the room's story
 * flags; the settings are part of what online's host sends, and everything
 * follows from them and the game's own state. */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_RM_WARP_PIPES,       /* "remake_warp_pipes": a warp pipe in the sewers' pipe rooms */
    PORT_RM_SHORTCUT_PIPES,   /* "remake_shortcut_pipes": Hooktail Castle, the Steeple, the Palace */
    PORT_RM_EXPRESS,          /* "remake_express_skip": the Excess Express rides past Riverside */
    PORT_RM_CHAPTER_SHORTCUTS,/* "remake_chapter_shortcuts": pipes for chapters 4 to 8's long walks */
    PORT_RM_KEELHAUL,         /* "remake_keelhaul": Keelhaul Key's jump stand, the Grotto's barrels, Fuzzies */
    PORT_RM_STEEPLE_STAIRS,   /* "remake_steeple_stairs": jump down the Steeple's stairs */
    PORT_RM_TWILIGHT,         /* "remake_twilight": the pumpkin patch, a shorter General White hunt */
    PORT_RM_BLOCKS,           /* "remake_blocks": the remake's new Save and Heart Blocks */
    PORT_RM_AMBUSHES,         /* "remake_ambushes": new ambush battles */
    PORT_RM_MERCHANTS,        /* "remake_merchants": Charlieton in Boggly Woods, a Toad in the Grotto */
    PORT_RM_COUNT
};

/* The save's own switch for the map changes (GSW 2030, port/include/port/save_words.h): Keelhaul Key's jump
 * stands, off until Mario has come over by the Skull Rock end's once (the remake's way: a shortcut opens from
 * its far side) */
enum { PORT_RM_GSW_KEELHAUL_STANDS = PORT_GSW_KEELHAUL_FIRST };

/* the setting's value (0 off) and key */
int portRemakeMap(int which);
const char* portRemakeMapKey(int which);

struct EventEntry;
/* evt_bero_get_info (TARGET_PC), LW(0) the room's entrance table: its
 * changes (an exit pointed elsewhere) and its new things placed. Every call
 * from a room's scripts may come here; what's placed is placed once. */
void portRemakeMapsRoom(struct EventEntry* event);

/* Script commands for the RELs (TARGET_PC blocks):
 *   portEvtRemakeOn(which, out)      out = the setting (portRemakeMap)
 *   portEvtRemakeText(which, out)    out = a text of ours (PORT_RM_TEXT_*) in the game's encoding */
int portEvtRemakeOn(struct EventEntry* event, int isFirstCall);
int portEvtRemakeText(struct EventEntry* event, int isFirstCall);
enum {
    PORT_RM_TEXT_WARP_ASK,      /* the warp pipe: "Where to?" */
    PORT_RM_TEXT_WARP_SELECT,   /* its choice (the lit hubs, then Stay here) */
    PORT_RM_TEXT_EXPRESS_ASK,   /* the train's bed: stop at Riverside? */
    PORT_RM_TEXT_EXPRESS_SELECT,
    PORT_RM_TEXT_CHARLIETON_HELLO, /* Charlieton in Boggly Woods */
    PORT_RM_TEXT_CHARLIETON_BYE,
    PORT_RM_TEXT_CHARLIETON_POOR,
    PORT_RM_TEXT_CHARLIETON_FULL,
    PORT_RM_TEXT_CHARLIETON_THANKS,
    PORT_RM_TEXT_CHARLIETON_SOLDOUT,
    PORT_RM_TEXT_TOAD_HELLO, /* the Toad at the end of the Pirate's Grotto */
    PORT_RM_TEXT_TOAD_BYE,
    PORT_RM_TEXT_TOAD_POOR,
    PORT_RM_TEXT_TOAD_FULL,
    PORT_RM_TEXT_TOAD_THANKS,
    PORT_RM_TEXT_COUNT
};

/* The Grotto's Toad's list (winmgr.c): evt_win_other_select's type for it,
 * shown in the badge shop's window (coins), and its items (count, up to max) */
#define PORT_SELECT_REMAKE_TOAD 0x7F02
int portRemakeToadStock(int* out, int max);

#ifdef __cplusplus
}
#endif

#endif
