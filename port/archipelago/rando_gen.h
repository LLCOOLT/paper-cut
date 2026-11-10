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
#include <functional>
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

enum LogicLevel { kGlitchless = 0, kTricks = 1, kNoLogic = 2 };

struct Options {
  std::string seed;  // any text; the same text, the same seed
  unsigned pools = kBadges | kItems | kStarPieces | kShineSprites;
  int logic = kVanillaStory;
  // Paper Cut's open world: the Crystal Stars that are the goal (0: all of
  // the stage's chapters'); the logic makes sure that many can be reached.
  int goalStars = 0;
  // Paper Cut's open world: the logic's version (a file keeps the one it was
  // made with: PORT_OPEN_LOGIC). 0 had no one-way trips (the ship, the train's
  // first ride and the moon counted as ways back) and didn't check the Shadow
  // Queen; 1 has them; 2 keeps the places of the items a rule names that a
  // player can eat or sell (the Coconuts) and of the places a disc can't
  // write (pcFixedPlace) out of the shuffle, so every disc draws the same.
  // The same seed gives other placements in each.
  int pcLogic = kPcLogicVersion;
  static constexpr int kPcLogicVersion = 2;
  // How strict the logic is (the open worlds; the vanilla story needs none):
  // glitchless, glitchless plus the chosen tricks (Paper Cut's open world:
  // pc_world.h's pcTricks), or none at all (items anywhere: a seed may not be
  // finishable).
  int logicLevel = kGlitchless;
  unsigned tricks = 0; // pcTricks' bits, counted at kTricks
  // More to shuffle and show (Extra bits; port/open_world.h's PORT_OPEN_X_*)
  // and the entrances shuffled (Entrance bits).
  unsigned extra = 0;
  unsigned entrances = 0;
};

enum Extra : unsigned {
  kExtraShops = 1u,    // shop stock moves among the shops' shelves (with its price)
  kExtraTroubles = 2u, // troubles' rewards take part (the vanilla story's kinds)
  kExtraMystery = 4u,  // items lying about show as a Mystery until picked up (the game side only)
};
enum Entrance : unsigned {
  kEntranceGates = 1u,      // the chapters' pipes in the sewers lead to other chapters
  kEntranceRooms = 2u,      // room doors swap within their area (pc_doors.h's pcRoomDoors)
  kEntranceMix = 4u,        // the ways between areas mix across the world (pcWays)
  kEntranceTransports = 8u, // the blimp, the ship and the train lead to one another's destinations (pcRides)
};

// The tricks there are (pc_world.h): each one's bit and name, in order.
struct TrickInfo {
  unsigned bit;
  const char* name;
};
std::vector<TrickInfo> tricks();
const char* logicLevelName(int level);

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
  int chaptersReached = 0; // Paper Cut's open world: the chapters whose way in the final sweep reached
};

Result generate(const Options& options);

// Paper Cut's open world: can one-way trip `trip` (pc_world.h's trips: 0 the
// ship to Keelhaul Key, 1 the train's first ride, 2 the cannon to the moon)
// be finished with `held` in hand (Archipelago item ids, one per copy;
// partners as their items) and the places `found` (kTtydApLocations indexes)
// already found, on a seed's placements (generate's, these options)? What
// lies on the way counts; nothing else outside. The game asks before Mario
// takes the trip (open_world_pc.c), as the logic does before it counts on it.
bool pcTripCanEnd(const Options& options, const std::vector<Placement>& placements, int trip,
                  const std::vector<int64_t>& held, const std::vector<int>& found);
// the trips pcTripCanEnd knows
enum { kTripShip = 0, kTripTrain = 1, kTripMoon = 2 };

// What Mario holds as the logic counts it (pcTripCanEnd's and track's `held`),
// from a file's state: the one model the game (open_world_trips.cpp) and the
// tests (tests/port/rando_solve.cpp) both use, so they can't disagree. Every
// item a rule can name counts: a key item as the most of the copies carried
// now and the copies the places found gave (the game takes keys at their
// doors, which stay open), anything else (a Coconut, a mushroom) as carried
// now (it may have been eaten or sold); partners in the party; the Crystal
// Stars held.
struct FileView {
  std::function<int(int gameItem)> pouch;   // copies of a game item carried now (pouchCheckItem)
  std::function<bool(int partner)> partner; // partnerOf's ids: in the party
  int stars = 0;
  std::vector<int> found; // kTtydApLocations indexes whose place was found
};
std::vector<int64_t> heldOf(const Options& options, const std::vector<Placement>& placements, const FileView& file);

// The check tracker: each place a seed shuffled, whether it was found (its
// flag, in `found`) and whether the logic reaches it now with what is in hand
// (`held`, as pcTripCanEnd takes them), nothing more collected. With no logic,
// or the game's own story (nothing gated by the shuffle), every place counts
// as reachable.
struct Tracked {
  int location; // kTtydApLocations index
  int region;   // kTtydRandoRegions index
  bool found;
  bool reachable;
};
std::vector<Tracked> track(const Options& options, const std::vector<Placement>& placements,
                           const std::vector<int64_t>& held, const std::vector<int>& found);
const char* regionName(int region);

// The chapter gates (kEntranceGates, Paper Cut's open world): the gate the
// pipe of gate `gate` leads to (0 tik_02's, chapter 1; 1 tik_03's, 2;
// 2 tik_08's, 4; 3 tik_07's, 7). The gate itself when they aren't shuffled.
int gateTarget(const Options& options, int gate);

// The entrance shuffle's doors (kEntranceRooms, kEntranceMix; pc_doors.h):
// the doors are numbered as doorHalves lists them (each room door's two
// sides, then each way's exit and entrance), and doorTargets gives, for each,
// the door it leads to now (the one it leads to in the game when nothing is
// shuffled). Coupled: if a leads to b, b leads to a.
struct DoorHalf {
  const char* map;
  const char* bero;
};
const std::vector<DoorHalf>& doorHalves();
std::vector<int> doorTargets(const Options& options);
// The rides (kEntranceTransports): where ride `ride` (0 the blimp, 1 Flavio's
// ship, 2 the train) goes, as the ride it stands in for; itself when not shuffled.
int rideTarget(const Options& options, int ride);

// The 64-bit seed a text names (FNV-1a), and a fresh seed text from entropy.
uint64_t seedHash(const std::string& text);
std::string seedText(uint64_t entropy);

// Names, for logs: an item's ("?" if unknown), a pool's set ("badges, items").
const char* itemName(int64_t id);
std::string poolsText(unsigned pools);
// The kind of item an Archipelago item is (a Pool bit), or 0: never shuffled
// (partners, Crystal Stars).
unsigned poolOf(int64_t item);
// Paper Cut's open world: the kind of item it is there (poolOf, but the
// Briefcase is the story's key item), and the game's own item id an
// Archipelago item gives (its romId, but the game's Briefcase for the
// apworld's, which is the TTYD AP mod's own).
unsigned paperCutPoolOf(int64_t item);
int gameItemOf(int64_t item);
// The partner an Archipelago item is (1 Goombella ... 7 Ms. Mowz, the game's
// party ids), or 0.
int partnerOf(int64_t item);
// An item's Archipelago id by its name (kTtydRandoItemNames), or 0.
int64_t itemByName(const char* name);

} // namespace ttyd_rando
#endif
