// The built-in randomizer's seed generator (docs/RANDOMIZER.md): from a seed
// text and options, which item goes where, over the locations the
// Archipelago support already knows (ttyd_ap_data.h) and the apworld's logic
// (ttyd_rando_logic.h). No game, no files, no globals: the same seed and
// options give the same placements everywhere (its own PRNG; nothing from
// <random>), so tests/port/rando_test.cpp runs it on the host.
#pragma once

#include <stdint.h>

/* the logic tables (ttyd_rando_logic.h, tools/port/gen_ttyd_rando.py) */
enum { RANDO_HAS, RANDO_ALL, RANDO_ANY, RANDO_REACH_LOCATION, RANDO_REACH_REGION };

/* A rule: HAS: item `first` (kTtydRandoItems) at least `count` times; ALL and
 * ANY: at least `count` of the `n` rules at kTtydRandoKids[first...]; REACH_*:
 * location (kTtydRandoLocations) or region `first` reachable. */
typedef struct TtydRandoNode {
    uint8_t op;
    uint8_t count;
    uint16_t first;
    uint16_t n;
} TtydRandoNode;

typedef struct TtydRandoItem {
    const char* name;
    int64_t id; /* Archipelago's (0: none) */
} TtydRandoItem;

typedef struct TtydRandoLocation {
    int64_t id;      /* Archipelago's, as kTtydApLocations */
    int64_t vanilla; /* the item there in the game (Archipelago id) */
    uint8_t region;
    uint16_t tags;   /* RANDO_TAG_* */
    int16_t rule;    /* kTtydRandoNodes, or -1 */
} TtydRandoLocation;

typedef struct TtydRandoEdge {
    uint8_t from, to;
    int16_t rule;
} TtydRandoEdge;

#ifdef __cplusplus
#include <string>
#include <vector>

namespace ttyd_rando {

// What is shuffled: each location whose own item is of a chosen kind gets one
// of the chosen kinds' items.
enum Pool : unsigned {
  kBadges = 1u,
  kItems = 2u,      // consumables, recipe ingredients, coins
  kKeyItems = 4u,   // keys, story items, the curses, hammer and boots (open world only)
  kStarPieces = 8u,
  kShineSprites = 16u,
};

enum Logic {
  // The game as it is: its story decides what opens when. Only what never
  // gates the story moves (badges, items that are not needed anywhere, Star
  // Pieces, Shine Sprites), so any placement is beatable. No mod needed.
  kVanillaStory = 0,
  // The TTYD AP mod's open world (the TTYD apworld's logic): key items too,
  // placed so that the Shadow Queen can be reached. Needs that mod.
  kOpenWorld = 1,
  // Paper Cut's own open world (pc_world.h): each chapter's story runs on
  // its own, so the chapters a stage opens can be played in any order its
  // logic allows; key items and partners placed by it. No mod needed.
  kPaperCut = 2,
};

struct Options {
  std::string seed;  // any text; the same text, the same seed
  unsigned pools = kBadges | kItems | kStarPieces | kShineSprites;
  int logic = kVanillaStory;
};

struct Placement {
  int location;   // index in kTtydApLocations
  int64_t item;   // Archipelago item id
  bool shuffled;  // false: the location's own item, written back (open world)
};

struct Result {
  bool ok = false;
  std::string error;
  uint64_t hash = 0;  // the seed text's
  int attempts = 0;
  int shuffled = 0;   // locations that took part
  int moved = 0;      // of those, the ones whose item changed
  std::vector<Placement> placements;
  std::string spoiler; // the spoiler log's text
};

Result generate(const Options& options);

// The 64-bit seed a text names (FNV-1a), and a fresh seed text from entropy.
uint64_t seedHash(const std::string& text);
std::string seedText(uint64_t entropy);

// Names, for logs: an item's ("?" if unknown), a pool's set ("badges, items").
const char* itemName(int64_t id);
std::string poolsText(unsigned pools);
// The kind of item an Archipelago item is (a Pool bit), or 0: never shuffled
// (partners, Crystal Stars).
unsigned poolOf(int64_t item);
// The partner an Archipelago item is (1 Goombella ... 7 Ms. Mowz, the game's
// party ids), or 0.
int partnerOf(int64_t item);
// An item's Archipelago id by its name (kTtydRandoItemNames), or 0.
int64_t itemByName(const char* name);

} // namespace ttyd_rando
#endif
