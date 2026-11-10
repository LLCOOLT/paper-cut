// Archipelago for TTYD (docs/MODDING.md, "Archipelago"): a seed (.apttyd,
// the TTYD apworld's patch file) applied to the PC build as its patcher
// applies it to a disc image, and the slot kept in step with a multiworld
// server, as the apworld's client does through Dolphin's memory. The game
// side of the randomizer (items given, flags, the received-item feed) is the
// TTYD AP mod (github.com/jamesbrq/TTYDAP), ported as a code mod; the two
// meet in the GameCube low memory it uses (portGcLowMem) and the game's GSWs.
#pragma once

#include <stdint.h>

/* the generated data (port/archipelago/ttyd_ap_data.h, tools/port/gen_ttyd_ap.py) */
enum { TTYD_AP_SCRIPT, TTYD_AP_DATA, TTYD_AP_LOWMEM };
enum { TTYD_AP_SHOP = 1, TTYD_AP_TATTLE = 2 };

typedef struct TtydApItem {
    int64_t id;
    int romId; /* the game's item id */
    int cls;   /* 1 progression, 2 useful, 4 trap */
} TtydApItem;

/* where a location's item (or a shop's price) is in the PC build */
typedef struct TtydApTarget {
    const char* name; /* the game's symbol (file::name for a static) */
    const char* area; /* its REL ("" for the DOL's) */
    uint32_t offset;  /* PC byte offset in it */
    uint8_t kind;     /* TTYD_AP_SCRIPT (an EvtWord), _DATA (a 4-byte s32), _LOWMEM (portGcLowMem) */
    uint8_t width;    /* the value's bytes on the GameCube: 4, or 2 (the DOL's tables) */
    uint8_t price;    /* a shop's price, not its item */
    uint32_t gcOffset; /* the GameCube file's offset (the area's REL, or main.dol) */
} TtydApTarget;

typedef struct TtydApLocation {
    int64_t id;
    const char* name; /* as the seed's locations.json names it; "" if it has no item */
    uint8_t gswType; /* 0 GSW (checked once >= value), 1 GSWF */
    uint16_t gswOffset;
    uint16_t gswValue;
    uint8_t flags; /* TTYD_AP_SHOP, TTYD_AP_TATTLE */
    uint16_t first, count; /* its targets */
    int16_t vanilla;       /* the game's own item there */
} TtydApLocation;

typedef struct TtydApTattle {
    int64_t location;
    int unit;
} TtydApTattle;

#ifdef __cplusplus
extern "C" {
#endif

/* at start, before the mods apply: settings (<user>/archipelago.json), the
 * seed's options, files and placements */
void portApInit(const char* userPath);
/* an area's code linked (its data fresh from the disc): the seed's items in it */
void portApAreaLinked(const char* area);
/* each game frame, on the game's thread: the server's packets, the slot's sync */
void portApFrame(void);
/* the settings page's state and actions */
const char* portApStatus(void);
void portApConnect(const char* server, const char* slot, const char* password);
void portApDisconnect(void);
/* a seed for the next start (an .apttyd); its settings kept */
void portApSetSeed(const char* path);
const char* portApSeedPath(void); /* the next start's */
const char* portApSlotName(void); /* the loaded seed's player, or "" */

/* $TTYD_AP_VERIFY: every REL target checked against the disc's REL, each one
 * that differs logged; how many differ (ap_verify.cpp, every backend) */
int portApVerifyTargets(void);

/* the game side (port/src/common/ap_game_pc.c) */
int portApGswfGet(int flag);
void portApGswfSet(int flag);
int portApGswGet(int index);
void portApGswSet(int index, int value);
uint8_t* portApGswBytes(void); /* the GSW bytes (index 1..), as the mod keeps words in them */
const char* portApMapName(void);
int portApInFile(void); /* the game's sequence is a file's (not the title, a load) */
/* a whole disc file ("rel/gor.rel"), malloc'd; NULL if it can't be read */
void* portApReadDiscFile(const char* path, int* size);
int portApSellPrice(int item);
void portApSetSellPrice(int item, int price);
int portApBuyPrice(int item); /* the item table's buy price (a shuffled shop shelf's) */

#ifdef __cplusplus
}
#endif
