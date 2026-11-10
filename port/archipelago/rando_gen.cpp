// The built-in randomizer's seed generator (port/archipelago/rando_gen.h).
//
// Open world: the apworld's way (Fill.py's fill_restrictive, "assumed fill"):
// the progression items are placed one at a time, each where it can be
// reached with every progression item not yet placed assumed in hand, so
// whatever order they are found in, each one's place opens before it is
// needed; then the rest go anywhere. Vanilla story: a plain shuffle of items
// the story never asks for.
#include "rando_gen.h"
#include "ttyd_ap_data.h"
#include "ttyd_rando_logic.h"
#include "pc_world.h"
#include "pc_doors.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ttyd_rando {
namespace {

constexpr int kLocationCount = int(sizeof(kTtydRandoLocations) / sizeof(kTtydRandoLocations[0]));
constexpr int kItemCount = int(sizeof(kTtydRandoItems) / sizeof(kTtydRandoItems[0]));
constexpr int kStarsItem = 0;  // kTtydRandoItems[0]: the Crystal Stars collected
constexpr int kMaxAttempts = 100;
constexpr int kStarPiece = 0x7D, kShineSprite = 0x57;
constexpr int64_t kStartingPartnerLocation = 78780002; // Rogueport Center: Goombella
const char* const kPartners[] = {"Goombella", "Koops", "Flurrie", "Yoshi", "Vivian", "Bobbery", "Ms. Mowz"};

// xoshiro256** seeded by splitmix64: small, fast, the same on every compiler
struct Rng {
  uint64_t s[4];
  explicit Rng(uint64_t seed) {
    for (uint64_t& v : s) {
      seed += 0x9E3779B97F4A7C15ull;
      uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      v = z ^ (z >> 31);
    }
  }
  static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
  uint64_t next() {
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
  }
  // uniform in [0, n), without modulo bias
  uint32_t below(uint32_t n) {
    const uint64_t limit = UINT64_MAX - UINT64_MAX % n;
    uint64_t x;
    do {
      x = next();
    } while (x >= limit);
    return uint32_t(x % n);
  }
  template <typename T>
  void shuffle(std::vector<T>& v) {
    for (size_t i = v.size(); i > 1; i--) {
      std::swap(v[i - 1], v[below(uint32_t(i))]);
    }
  }
};

const TtydApItem* apItem(int64_t id) {
  for (const TtydApItem& i : kTtydApItems) {
    if (i.id == id) return &i;
  }
  return nullptr;
}

bool isStar(int64_t id) {
  for (int64_t s : kTtydRandoStars) {
    if (s == id) return true;
  }
  return false;
}

// The logic's item index of an Archipelago item, or -1
int logicItem(int64_t id) {
  static const std::map<int64_t, int> map = [] { // (built once, thread-safely)
    std::map<int64_t, int> m;
    for (int i = 0; i < kItemCount; i++) {
      if (kTtydRandoItems[i].id != 0) m[kTtydRandoItems[i].id] = i;
    }
    return m;
  }();
  const auto it = map.find(id);
  return it == map.end() ? -1 : it->second;
}

// kTtydApLocations' index of a location id, or -1
int apLocation(int64_t id) {
  static const std::map<int64_t, int> map = [] {
    std::map<int64_t, int> m;
    const int n = int(sizeof(kTtydApLocations) / sizeof(kTtydApLocations[0]));
    for (int i = 0; i < n; i++) m[kTtydApLocations[i].id] = i;
    return m;
  }();
  const auto it = map.find(id);
  return it == map.end() ? -1 : it->second;
}

// --- the logic: what can be reached with what is in hand ---
struct Sweep {
  std::vector<int> have = std::vector<int>(kItemCount, 0);
  std::vector<char> region = std::vector<char>(RANDO_REGION_COUNT, 0);
  std::vector<char> reached = std::vector<char>(kLocationCount, 0);

  void collect(int64_t item) {
    const int i = logicItem(item);
    if (i >= 0) have[i]++;
    if (isStar(item)) have[kStarsItem]++;
  }

  bool eval(int node) const {
    if (node < 0) return true;
    const TtydRandoNode& n = kTtydRandoNodes[node];
    switch (n.op) {
    case RANDO_HAS: return have[n.first] >= n.count;
    case RANDO_REACH_LOCATION: return reached[n.first] != 0;
    case RANDO_REACH_REGION: return region[n.first] != 0;
    default: {
      int ok = 0;
      for (int k = 0; k < n.n && ok < n.count; k++) ok += eval(kTtydRandoKids[n.first + k]) ? 1 : 0;
      return ok >= n.count;
    }
    }
  }

  void regions() {
    region[RANDO_REGION_MENU] = 1;
    for (bool changed = true; changed;) {
      changed = false;
      for (const TtydRandoEdge& e : kTtydRandoEdges) {
        if (region[e.from] && !region[e.to] && eval(e.rule)) {
          region[e.to] = 1;
          changed = true;
        }
      }
    }
  }

  // Everything reachable, collecting each reached location's item (item[i];
  // 0: nothing known there yet). inLogic: the locations that count.
  // sphere: only one round (what is reachable with what was in hand), its
  // locations returned.
  std::vector<int> run(const std::vector<int64_t>& item, const std::vector<char>& inLogic, bool sphere = false) {
    std::vector<int> found;
    for (bool changed = true; changed;) {
      changed = false;
      regions();
      std::vector<int> round;
      for (int i = 0; i < kLocationCount; i++) {
        if (!inLogic[i] || reached[i]) continue;
        const TtydRandoLocation& l = kTtydRandoLocations[i];
        if (region[l.region] && eval(l.rule)) {
          reached[i] = 1;
          round.push_back(i);
          changed = true;
          if (!sphere && item[i] != 0) collect(item[i]);
        }
      }
      if (sphere) {
        for (int i : round) {
          if (item[i] != 0) collect(item[i]);
        }
        regions();
        return round;
      }
    }
    return found;
  }
};

// The game item id of the apworld's "10 Coins" (the TTYD AP mod's own)
constexpr int kModCoins = 0x53;

// Whether every word of a location's item is placed on this version (the
// apworld's offsets are the US disc's; ttyd_ap_data.h's JP and PAL tables
// leave a place out, with an empty name, where that disc has it otherwise).
bool placedHere(const TtydApLocation& loc) {
  for (int i = loc.first; i < loc.first + loc.count; i++) {
    if (kTtydApTargets[i].name[0] == '\0') return false;
  }
  return true;
}

// Whether every disc can write a location's item: kTtydApNotEveryDisc lists
// the ones a disc can't (the JP disc's Pirate's Grotto Staircase block). A
// seed treats them alike on every disc, so its draws, and every placement
// after them, are the same everywhere.
bool onEveryDisc(int64_t id) {
  return std::find(std::begin(kTtydApNotEveryDisc), std::end(kTtydApNotEveryDisc), id) ==
         std::end(kTtydApNotEveryDisc);
}

// A vanilla-story location: its item given by the game's own script, nothing
// of the TTYD AP mod's (its low-memory tables, the DOL's shop and Dazzle
// tables), and nothing the story itself hands over. everyDisc: a place some
// disc can't write (onEveryDisc) is judged as on the discs that write it, so
// every disc draws alike, and as 0.4.1 drew (it had one table, the US disc's,
// for every disc); generate() then leaves it unwritten where !placedHere.
bool plainScriptLocation(const TtydApLocation& loc, const TtydRandoLocation& r, bool troubles = false,
                         bool everyDisc = false) {
  const bool elsewhere = everyDisc && !onEveryDisc(loc.id) && !placedHere(loc);
  const auto placed = [&] { return elsewhere || placedHere(loc); };
  const auto scriptWords = [&] {
    if (elsewhere) return true; // (a script word on the discs that have the place)
    for (int i = loc.first; i < loc.first + loc.count; i++) {
      const TtydApTarget& t = kTtydApTargets[i];
      if (t.kind != TTYD_AP_SCRIPT || t.area[0] == '\0' || t.price) return false;
    }
    return true;
  };
  // A trouble's reward (Options::extra's kExtraTroubles) is a script's word
  // as the others, when it is an item: the many that pay coins have the
  // apworld's "10 Coins" there (the TTYD AP mod's own item: the word is
  // evt_sub_get_coin's count), and they stay out.
  if ((r.tags & RANDO_TAG_TROUBLE) && troubles && !(r.tags & RANDO_TAG_PARTNER) && loc.vanilla != kModCoins &&
      loc.count > 0 && placed()) {
    return scriptWords();
  }
  // Places whose word in the apworld's data isn't an item the game's own
  // script gives there (the TTYD AP mod reworks them): Whacka's bumps (words
  // in the middle of muj_00's koburon_dead) and the Excess Express madam's 30
  // coins (evt_sub_get_coin's count). Checked against the disc's RELs: every
  // other script word is an item, or a Shine Sprite or coin block's word the
  // port reads as one (evt_mobj.c), or a black chest's, which the open world
  // asks for by its place (open_world.cpp).
  static const int64_t kNotItemWords[] = {78780454, 78780455, 78780456, 78780457, 78780458,
                                          78780459, 78780460, 78780806, 78780515};
  if (std::find(std::begin(kNotItemWords), std::end(kNotItemWords), loc.id) != std::end(kNotItemWords)) return false;
  if (loc.count == 0 || (loc.flags & (TTYD_AP_SHOP | TTYD_AP_TATTLE)) || !placed()) return false;
  if (r.tags & (RANDO_TAG_SHOP | RANDO_TAG_TROUBLE | RANDO_TAG_COOKING | RANDO_TAG_DAZZLE | RANDO_TAG_PIT_FLOOR |
                RANDO_TAG_STAR | RANDO_TAG_PARTNER)) {
    return false;
  }
  return scriptWords();
}

// An item the vanilla game can be given anywhere and never needs for its story
bool vanillaSafe(int64_t item) {
  const TtydApItem* it = apItem(item);
  if (it == nullptr) return false;
  if (std::string(itemName(item)) == "Briefcase") return false; // (the story's own: see gameItemOf)
  const int rom = it->romId;
  if (rom == kStarPiece || rom == kShineSprite) return true;
  if (rom >= 0xF0 && rom < 0x200) return true;           // badges: never asked for
  return rom >= 0x7E && rom <= 0xEF && it->cls != 1;      // items nothing needs (not coins, hearts)
}

std::string hex(uint64_t v) {
  char text[24];
  std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(v));
  return text;
}

// What the logic level and tricks add to a seed's random numbers: nothing
// when glitchless, so those seeds stay as they were.
uint64_t logicSalt(const Options& o) {
  if (o.logicLevel == kGlitchless) return 0;
  const uint64_t tricks = o.logicLevel == kTricks ? o.tricks : 0u;
  return (uint64_t(o.logicLevel) << 36) ^ (tricks << 24);
}

// The spoiler log's line for them
std::string logicLine(const Options& o) {
  std::string s = std::string("Logic level: ") + logicLevelName(o.logicLevel);
  if (o.logicLevel == kTricks) {
    std::string names;
    for (const PcTrick& t : pcTricks()) {
      if (o.tricks & t.bit) names += (names.empty() ? "" : ", ") + std::string(t.name);
    }
    s += " (" + (names.empty() ? std::string("none chosen") : names) + ")";
  }
  return s + "\n";
}

std::string locationName(int i) {
  const int ap = apLocation(kTtydRandoLocations[i].id);
  return ap >= 0 ? kTtydApLocations[ap].name : "?";
}

} // namespace

std::vector<TrickInfo> tricks() {
  std::vector<TrickInfo> out;
  for (const PcTrick& t : pcTricks()) out.push_back({t.bit, t.name});
  return out;
}

const char* logicLevelName(int level) {
  switch (level) {
  case kTricks: return "tricks";
  case kNoLogic: return "no logic";
  default: return "glitchless";
  }
}

uint64_t seedHash(const std::string& text) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (unsigned char c : text) {
    h ^= c;
    h *= 0x100000001B3ull;
  }
  return h;
}

std::string seedText(uint64_t entropy) {
  static const char kChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O, 1/I
  Rng rng(entropy);
  std::string s;
  for (int i = 0; i < 10; i++) s += kChars[rng.below(32)];
  return s;
}

const char* itemName(int64_t id) {
  for (const TtydRandoItem& i : kTtydRandoItemNames) {
    if (i.id == id) return i.name;
  }
  return "?";
}

std::string poolsText(unsigned pools) {
  static const std::pair<unsigned, const char*> kNames[] = {
      {kBadges, "badges"}, {kItems, "items"}, {kKeyItems, "key items"},
      {kStarPieces, "Star Pieces"}, {kShineSprites, "Shine Sprites"}};
  std::string s;
  for (const auto& [bit, name] : kNames) {
    if (pools & bit) s += (s.empty() ? "" : ", ") + std::string(name);
  }
  return s.empty() ? "nothing" : s;
}

unsigned poolOf(int64_t item) {
  const char* name = itemName(item);
  for (const char* p : kPartners) {
    if (std::string(name) == p) return kKeyItems; // (the open world's partners are items)
  }
  if (isStar(item)) return 0;
  const TtydApItem* it = apItem(item);
  if (it == nullptr || it->romId <= 0) return 0;
  const int rom = it->romId;
  if (rom == kStarPiece) return kStarPieces;
  if (rom == kShineSprite) return kShineSprites;
  if (rom >= 0xF0) return kBadges;
  if (rom >= 0x79) return kItems;
  return it->cls == 0 ? kItems : kKeyItems; // (the mod's own "10 Coins" is filler)
}

// The apworld names the TTYD AP mod's own item for the Briefcase (the mod
// reworks Zip Toad's scene); in the game's own story it is the game's key
// item, which rsh_02's evt_toron_catch2 asks for until Mario hands it over.
int gameItemOf(int64_t item) {
  if (std::string(itemName(item)) == "Briefcase") return 0x51; // ITEM_BRIEFCASE
  const TtydApItem* it = apItem(item);
  return it != nullptr ? it->romId : 0;
}

unsigned paperCutPoolOf(int64_t item) {
  return std::string(itemName(item)) == "Briefcase" ? unsigned(kKeyItems) : poolOf(item);
}

int partnerOf(int64_t item) {
  static const std::pair<const char*, int> kIds[] = {{"Goombella", 1}, {"Koops", 2},   {"Bobbery", 3}, {"Yoshi", 4},
                                                     {"Flurrie", 5},   {"Vivian", 6}, {"Ms. Mowz", 7}};
  const char* name = itemName(item);
  for (const auto& [n, id] : kIds) {
    if (std::string(name) == n) return id;
  }
  return 0;
}

int64_t itemByName(const char* name) {
  for (const TtydRandoItem& i : kTtydRandoItemNames) {
    if (std::string(i.name) == name) return i.id;
  }
  return 0;
}

namespace {

// --- Paper Cut's own open world (pc_world.h) ---
struct PcLogic {
  const PcWorld& w = pcWorld();
  std::vector<char> regionOn = std::vector<char>(RANDO_REGION_COUNT, 0);
  std::vector<char> hub = std::vector<char>(RANDO_REGION_COUNT, 0);
  struct Edge {
    int from, to;
    const PcRule* rule;
  };
  std::vector<Edge> edges;
  std::vector<const PcRule*> extra = std::vector<const PcRule*>(kLocationCount, nullptr);
  std::vector<const PcRule*> replaced = std::vector<const PcRule*>(kLocationCount, nullptr); // a trick's, for the apworld's
  std::vector<char> hubScene = std::vector<char>(kLocationCount, 0); // a Rogueport scene that may hold a key item
  std::map<std::string, int> locationIndex; // by name: kTtydRandoLocations' index
  std::map<std::string, int> itemIndex;     // by name: kTtydRandoItems' index
  std::map<std::string, int64_t> otherItem; // by name: items no apworld rule names (the Briefcase), their ids
  std::vector<int> goal;                    // the goal places' indexes
  struct Trip {
    int entry;
    std::vector<char> inside; // by region
    int exitPlace = -1, exitRegion = -1;
    bool revisit;
  };
  std::vector<Trip> trips;
  std::vector<int> tripOf = std::vector<int>(RANDO_REGION_COUNT, -1); // a trip's way-in region: its index
  std::string error;

  static int region(const char* name) {
    for (int i = 0; i < RANDO_REGION_COUNT; i++) {
      if (std::string(kTtydRandoRegions[i]) == name) return i;
    }
    return -1;
  }

  bool check(const PcRule& r) {
    if (r.op == PcRule::kHas && !itemIndex.count(r.name)) {
      if (const int64_t id = itemByName(r.name.c_str())) otherItem[r.name] = id;
      else error = "pc_world.h: no item \"" + r.name + "\"";
    } else if (r.op == PcRule::kReach && !locationIndex.count(r.name)) {
      error = "pc_world.h: no place \"" + r.name + "\"";
    } else if (r.op == PcRule::kRegion && region(r.name.c_str()) < 0) {
      error = "pc_world.h: no region \"" + r.name + "\"";
    }
    for (const PcRule& k : r.kids) check(k);
    return error.empty();
  }

  // The chapter gates (Options::entrances' kEntranceGates): the sewers' four
  // chapter pipes' ways, each to the chapter perm[its gate] (pcGateRegions'
  // order); the gate's own rule stays (it is the pipe room's).
  static int gateOf(const char* region) {
    for (int g = 0; g < 4; g++) {
      if (std::string(pcGateRegions()[g]) == region) return g;
    }
    return -1;
  }
  static bool sewer(const char* region) {
    return std::string(region).rfind("Rogueport Sewers", 0) == 0;
  }

  // the entrance shuffle's own rules (a mixed way's or ride's two rules
  // together); a deque, so the edges' pointers stay
  std::deque<PcRule> owned;

  PcLogic(unsigned tricks, unsigned gates, const std::vector<int>& doors, unsigned rides) {
    const int perm[4] = {int(gates & 3), int(gates >> 2 & 3), int(gates >> 4 & 3), int(gates >> 6 & 3)};
    auto to = [&](const char* from, const char* dest) {
      const int g = gates != 0 && sewer(from) ? gateOf(dest) : -1;
      return g >= 0 ? region(pcGateRegions()[perm[g]]) : region(dest);
    };
    // The ways between areas and the rides the shuffle moved: their edges in
    // the game are replaced by where they lead now (pc_doors.h)
    const int roomHalves = int(pcRoomDoors().size()) * 2;
    auto worldRule = [&](const char* from, const char* dest) -> const PcRule* {
      const PcRule* found = nullptr;
      for (const PcEdge& e : w.edges) {
        if (std::string(e.from) != from || std::string(e.to) != dest) continue;
        if (found != nullptr) error = std::string("pc_world.h: two edges ") + from + " -> " + dest + " (pc_doors.h needs one)";
        found = &e.rule;
      }
      if (found == nullptr) error = std::string("pc_world.h: no edge ") + from + " -> " + dest + " (pc_doors.h)";
      return found;
    };
    std::vector<std::pair<std::string, std::string>> replacedEdges;
    struct Added {
      const char* from;
      const char* to;
      const PcRule* rule;
    };
    std::vector<Added> added;
    if (!doors.empty()) {
      const std::vector<PcWay>& ways = pcWays();
      for (size_t k = 0; k < ways.size(); k++) {
        const int exit = roomHalves + int(k) * 2;
        const int p = (doors[exit] - roomHalves) / 2;
        if (p == int(k)) continue;
        const PcRule* a = worldRule(ways[k].from, ways[k].to);
        const PcRule* b = worldRule(ways[p].from, ways[p].to);
        if (a == nullptr || b == nullptr) continue;
        replacedEdges.push_back({ways[k].from, ways[k].to});
        owned.push_back(pcAll({*a, *b}));
        added.push_back({ways[k].from, ways[p].to, &owned.back()});
      }
      // room doors: the regions whose rooms now join up through them reach
      // one another (a shuffle that cut a region apart was drawn again)
      std::map<std::string, std::string> root;
      std::function<std::string(const std::string&)> find = [&](const std::string& m) -> std::string {
        auto it = root.find(m);
        if (it == root.end() || it->second == m) return m;
        return it->second = find(it->second);
      };
      const std::vector<DoorHalf>& halves = doorHalves();
      auto regionOf = [&](int h) { return pcRoomDoors()[h / 2].region; };
      for (int h = 0; h < roomHalves; h++) {
        root.emplace(halves[h].map, halves[h].map);
        root.emplace(halves[doors[h]].map, halves[doors[h]].map);
        root[find(halves[h].map)] = find(halves[doors[h]].map);
      }
      std::map<std::string, std::vector<const char*>> joined;
      for (int h = 0; h < roomHalves; h++) {
        std::vector<const char*>& list = joined[find(halves[h].map)];
        const std::string mine = regionOf(h);
        if (std::find_if(list.begin(), list.end(), [&](const char* r) { return mine == r; }) == list.end()) {
          list.push_back(regionOf(h));
        }
      }
      for (const auto& [room, list] : joined) {
        for (const char* a : list) {
          for (const char* b : list) {
            if (a != b) added.push_back({a, b, &owned.emplace_back()});
          }
        }
      }
    }
    if (rides != 0) {
      const std::vector<PcRide>& r = pcRides();
      for (size_t k = 0; k < r.size(); k++) {
        const int p = int(rides >> (k * 2) & 3);
        if (p == int(k) || p >= int(r.size())) continue;
        replacedEdges.push_back({r[k].from, r[k].to});
        owned.push_back(pcAll({r[k].leave, r[p].arrive}));
        added.push_back({r[k].from, r[p].to, &owned.back()});
      }
    }
    auto isReplaced = [&](const PcEdge& e) {
      for (const auto& [f, t] : replacedEdges) {
        if (f == e.from && t == e.to) return true;
      }
      return false;
    };
    for (int i = 0; i < kItemCount; i++) itemIndex[kTtydRandoItems[i].name] = i;
    for (int i = 0; i < kLocationCount; i++) locationIndex[locationName(i)] = i;
    for (const char* n : w.regions) {
      if (region(n) < 0) error = std::string("pc_world.h: no region \"") + n + "\"";
      else regionOn[region(n)] = 1;
    }
    for (const char* n : w.hubRegions) {
      if (region(n) >= 0) hub[region(n)] = 1;
    }
    for (const PcEdge& e : w.edges) {
      const int a = region(e.from), b = to(e.from, e.to);
      if (a < 0 || b < 0) {
        error = std::string("pc_world.h: an edge's region ") + e.from + " -> " + e.to;
        continue;
      }
      check(e.rule);
      if (isReplaced(e)) continue;
      edges.push_back({a, b, &e.rule});
    }
    for (const Added& e : added) {
      const int a = region(e.from), b = region(e.to);
      if (a < 0 || b < 0) {
        error = std::string("pc_doors.h: a region ") + e.from + " -> " + e.to;
        continue;
      }
      check(*e.rule);
      edges.push_back({a, b, e.rule});
    }
    auto placeRules = [&](const std::vector<PcPlaceRule>& list, bool scene) {
      for (const PcPlaceRule& p : list) {
        const auto it = locationIndex.find(p.location);
        if (it == locationIndex.end()) {
          error = std::string("pc_world.h: no place \"") + p.location + "\"";
          continue;
        }
        if (extra[it->second] != nullptr) error = std::string("pc_world.h: two rules for \"") + p.location + "\"";
        check(p.rule);
        extra[it->second] = &p.rule;
        hubScene[it->second] = scene;
      }
    };
    placeRules(w.places, false);
    placeRules(w.hubScenes, true);
    // the chosen tricks: their ways, and their places' own rules
    for (const PcTrick& t : pcTricks()) {
      if (!(tricks & t.bit)) continue;
      for (const PcEdge& e : t.edges) {
        const int a = region(e.from), b = to(e.from, e.to);
        if (a < 0 || b < 0) {
          error = std::string("pc_world.h: ") + t.name + "'s region " + e.from + " -> " + e.to;
          continue;
        }
        check(e.rule);
        edges.push_back({a, b, &e.rule});
      }
      for (const PcPlaceRule& p : t.replace) {
        const auto it = locationIndex.find(p.location);
        if (it == locationIndex.end()) {
          error = std::string("pc_world.h: ") + t.name + ": no place \"" + p.location + "\"";
          continue;
        }
        check(p.rule);
        replaced[it->second] = &p.rule;
      }
    }
    for (int64_t id : w.goal) {
      int at = -1;
      for (int i = 0; i < kLocationCount; i++) {
        if (kTtydRandoLocations[i].id == id) at = i;
      }
      if (at < 0) error = "pc_world.h: no goal place " + std::to_string(id);
      else goal.push_back(at);
    }
    for (const char* n : w.extraItems) {
      if (itemByName(n) == 0) error = std::string("pc_world.h: no item \"") + n + "\" to place";
    }
    for (const PcTrip& t : w.trips) {
      Trip trip;
      trip.entry = region(t.entry);
      trip.inside.assign(RANDO_REGION_COUNT, 0);
      for (const char* n : t.inside) {
        if (region(n) < 0) error = std::string("pc_world.h: a trip's region \"") + n + "\"";
        else trip.inside[region(n)] = 1;
      }
      if (t.exitPlace != nullptr) {
        const auto it = locationIndex.find(t.exitPlace);
        if (it == locationIndex.end()) error = std::string("pc_world.h: a trip's end \"") + t.exitPlace + "\"";
        else trip.exitPlace = it->second;
      }
      if (t.exitRegion != nullptr) trip.exitRegion = region(t.exitRegion);
      trip.revisit = t.revisit;
      if (trip.entry < 0 || (trip.exitPlace < 0 && trip.exitRegion < 0)) {
        error = std::string("pc_world.h: the trip to ") + t.entry;
        continue;
      }
      tripOf[trip.entry] = int(trips.size());
      trips.push_back(trip);
    }
  }
};

// The tricks the logic being run counts (generatePaperCut and pcTripCanEnd set
// it for their call: TricksScope); each set of tricks has its own PcLogic.
thread_local unsigned tTricks = 0;
thread_local unsigned tGates = 0; // the chapter gates' permutation (2 bits a gate), 0: as the game has them
thread_local std::vector<int> tDoors; // the doors' targets (doorTargets), empty: as the game has them
thread_local unsigned tRides = 0;     // the rides' permutation (2 bits a ride), 0: as the game has them

// The gates a seed's options shuffle to: a permutation of the four, from the
// seed alone (its own numbers), 0 when they aren't shuffled or it is the
// game's own order.
unsigned gatesOf(const Options& o) {
  if (o.logic != kPaperCut || !(o.entrances & kEntranceGates)) return 0;
  Rng rng(seedHash(o.seed) ^ 0x4741544553474154ull);
  std::vector<int> p = {0, 1, 2, 3};
  rng.shuffle(p);
  const unsigned code = unsigned(p[0] | p[1] << 2 | p[2] << 4 | p[3] << 6);
  return code == 0xE4u ? 0u : code; // (0 1 2 3: the game's own)
}

std::vector<int> doorsOf(const Options& o);
unsigned ridesOf(const Options& o);

// The PcLogic of the tricks, gates, doors and rides in force. Only the last
// few made are kept: with the doors shuffled every seed has its own, and
// keeping them all would grow for as long as the game runs (each check, new
// seed and tracker refresh). The scope in force holds its own (tLogic), so one
// dropped from the cache lives on while a call still uses it.
thread_local std::shared_ptr<const PcLogic> tLogic;

std::shared_ptr<const PcLogic> logicInForce() {
  constexpr size_t kKept = 8;
  static std::map<std::string, std::shared_ptr<const PcLogic>> byKey;
  static std::deque<std::string> made; // (oldest first)
  static std::mutex lock;
  std::lock_guard<std::mutex> hold(lock);
  std::string key = std::to_string(tTricks) + "/" + std::to_string(tGates) + "/" + std::to_string(tRides) + "/";
  for (int d : tDoors) key += std::to_string(d) + ",";
  const auto it = byKey.find(key);
  if (it != byKey.end()) return it->second;
  std::shared_ptr<const PcLogic> l = std::make_shared<const PcLogic>(tTricks, tGates, tDoors, tRides);
  byKey[key] = l;
  made.push_back(key);
  while (made.size() > kKept) {
    byKey.erase(made.front());
    made.pop_front();
  }
  return l;
}

struct TricksScope {
  unsigned saved, savedGates, savedRides;
  std::vector<int> savedDoors;
  std::shared_ptr<const PcLogic> savedLogic;
  explicit TricksScope(const Options& o)
      : saved(tTricks), savedGates(tGates), savedRides(tRides), savedDoors(tDoors), savedLogic(tLogic) {
    tTricks = o.logicLevel == kTricks ? o.tricks : 0u;
    tGates = gatesOf(o);
    tDoors = doorsOf(o);
    tRides = ridesOf(o);
    tLogic = logicInForce();
  }
  ~TricksScope() {
    tTricks = saved;
    tGates = savedGates;
    tDoors.swap(savedDoors); // (a swap: a copy could throw in a destructor)
    tRides = savedRides;
    tLogic.swap(savedLogic);
  }
};

const PcLogic& pcLogic() {
  if (!tLogic) tLogic = logicInForce(); // (outside any scope: the game's own logic)
  return *tLogic;
}

// The entrance shuffle's doors (pc_doors.h), from the seed alone (their own
// numbers, so the items' placements are drawn as without them): the room
// doors paired anew within each area until every region's rooms still join up
// through them (else as the game has them), and the ways between areas until
// every region can be reached from the start with everything in hand. Empty:
// nothing moved.
std::vector<int> doorsOf(const Options& o) {
  if (o.logic != kPaperCut || !(o.entrances & (kEntranceRooms | kEntranceMix))) return {};
  const std::vector<DoorHalf>& halves = doorHalves();
  const std::vector<PcRoomDoor>& rooms = pcRoomDoors();
  const int roomHalves = int(rooms.size()) * 2;
  std::vector<int> t(halves.size());
  for (size_t h = 0; h < t.size(); h++) t[h] = int(h ^ 1);
  const std::vector<int> game = t;
  if (o.entrances & kEntranceRooms) {
    Rng rng(seedHash(o.seed) ^ 0x524F4F4D444F4F52ull);
    std::map<std::string, std::vector<int>> areas; // (by name: the same order everywhere)
    for (int h = 0; h < roomHalves; h++) areas[rooms[h / 2].area].push_back(h);
    for (const auto& [area, list] : areas) {
      for (int attempt = 0; attempt < 200; attempt++) {
        std::vector<int> order = list;
        rng.shuffle(order);
        bool ok = true;
        std::map<std::string, std::string> root;
        std::function<std::string(const std::string&)> find = [&](const std::string& m) -> std::string {
          auto it = root.find(m);
          if (it == root.end() || it->second == m) return m;
          return it->second = find(it->second);
        };
        for (size_t k = 0; k + 1 < order.size(); k += 2) {
          const std::string a = halves[order[k]].map, b = halves[order[k + 1]].map;
          if (a == b) ok = false; // (a door into its own room)
          root.emplace(a, a);
          root.emplace(b, b);
          root[find(a)] = find(b);
        }
        // each region's rooms joined up
        std::map<std::string, std::string> regionRoot;
        for (int h : list) {
          const std::string r = find(halves[h].map);
          auto [it, fresh] = regionRoot.emplace(rooms[h / 2].region, r);
          if (!fresh && it->second != r) ok = false;
        }
        if (!ok) continue;
        for (size_t k = 0; k + 1 < order.size(); k += 2) {
          t[order[k]] = order[k + 1];
          t[order[k + 1]] = order[k];
        }
        break;
      }
    }
  }
  if (o.entrances & kEntranceMix) {
    const std::vector<PcWay>& ways = pcWays();
    const PcWorld& w = pcWorld();
    Rng rng(seedHash(o.seed) ^ 0x57415953574159ull);
    std::vector<int> perm(ways.size());
    for (size_t k = 0; k < perm.size(); k++) perm[k] = int(k);
    for (int attempt = 0; attempt < 200; attempt++) {
      rng.shuffle(perm);
      // every region reached from the start, by the game's edges with the
      // ways moved (what they ask for assumed in hand)
      std::vector<std::pair<int, int>> edges;
      for (const PcEdge& e : w.edges) {
        bool way = false;
        for (const PcWay& x : ways) way = way || (std::string(x.from) == e.from && std::string(x.to) == e.to);
        if (!way) edges.push_back({PcLogic::region(e.from), PcLogic::region(e.to)});
      }
      for (size_t k = 0; k < ways.size(); k++) {
        edges.push_back({PcLogic::region(ways[k].from), PcLogic::region(ways[perm[k]].to)});
      }
      std::vector<char> seen(RANDO_REGION_COUNT, 0);
      seen[RANDO_REGION_MENU] = 1;
      for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [a, b] : edges) {
          if (a >= 0 && b >= 0 && seen[a] && !seen[b]) seen[b] = changed = true;
        }
      }
      bool ok = true;
      for (const char* n : w.regions) ok = ok && seen[PcLogic::region(n)];
      if (!ok) continue;
      for (size_t k = 0; k < ways.size(); k++) {
        const int exit = roomHalves + int(k) * 2, entrance = roomHalves + perm[k] * 2 + 1;
        t[exit] = entrance;
        t[entrance] = exit;
      }
      break;
    }
  }
  return t == game ? std::vector<int>() : t;
}

// The rides' permutation (2 bits a ride, pcRides' order), 0: as the game has them
unsigned ridesOf(const Options& o) {
  if (o.logic != kPaperCut || !(o.entrances & kEntranceTransports)) return 0;
  Rng rng(seedHash(o.seed) ^ 0x5249444553524944ull);
  std::vector<int> p = {0, 1, 2};
  rng.shuffle(p);
  const unsigned code = unsigned(p[0] | p[1] << 2 | p[2] << 4);
  return code == 0x24u ? 0u : code; // (0 1 2: the game's own)
}

struct PcSweep : Sweep {
  const PcLogic& L = pcLogic();
  int need = 0;                  // the goal's Crystal Stars (0: all of them)
  std::map<int64_t, int> other;  // items in hand that no apworld rule names

  void collect(int64_t item) {
    Sweep::collect(item);
    for (const auto& [name, id] : L.otherItem) other[id] += id == item;
    version++;
  }

  int goalStars() const { return need > 0 && need < int(L.goal.size()) ? need : int(L.goal.size()); }

  bool evalPc(const PcRule& r) const {
    switch (r.op) {
    case PcRule::kTrue: return true;
    case PcRule::kHas: {
      const auto it = L.itemIndex.find(r.name);
      if (it != L.itemIndex.end()) return have[it->second] >= r.count;
      const auto o = other.find(L.otherItem.at(r.name));
      return o != other.end() && o->second >= r.count;
    }
    case PcRule::kGoal: return have[kStarsItem] >= goalStars();
    case PcRule::kReach: return reached[L.locationIndex.at(r.name)] != 0;
    case PcRule::kRegion: return region[PcLogic::region(r.name.c_str())] != 0;
    case PcRule::kAll:
      for (const PcRule& k : r.kids) {
        if (!evalPc(k)) return false;
      }
      return true;
    case PcRule::kAny:
      for (const PcRule& k : r.kids) {
        if (evalPc(k)) return true;
      }
      return false;
    }
    return false;
  }

  // the one-way trips (pc_world.h's trips): taken, and when last tried
  const std::vector<int64_t>* items = nullptr; // (the run's)
  const std::vector<char>* logic = nullptr;
  std::vector<char> tripDone = std::vector<char>(L.trips.size(), 0);
  std::vector<int> tripTried = std::vector<int>(L.trips.size(), -1);
  std::vector<int> tripFound; // places a trip reached (a sphere's, for the playthrough)
  int version = 0;            // bumped by whatever could let a trip end
  bool tripsOff = false;      // no trip taken (pcTripCanEnd: only what is in hand)
  bool legacy = false;        // the logic before the trips (Options::pcLogic 0): their ways as any others

  // A trip taken from here: if it can be finished with what is in hand and
  // what it finds on the way, it is, and what it reached is collected.
  bool takeTrip(int k) {
    const PcLogic::Trip& t = L.trips[k];
    if (tripTried[k] == version) return false; // (nothing new since it last couldn't)
    tripTried[k] = version;
    PcSweep s(*this);
    s.region[t.entry] = 1;
    std::vector<int> found;
    bool ends = false;
    for (bool changed = true; changed;) {
      changed = false;
      for (const PcLogic::Edge& e : L.edges) {
        if (!s.region[e.from] || !t.inside[e.from]) continue;
        if (e.to == t.exitRegion) {
          ends = ends || s.evalPc(*e.rule);
          continue;
        }
        if (!t.inside[e.to] || s.region[e.to] || !s.evalPc(*e.rule)) continue;
        s.region[e.to] = 1;
        changed = true;
      }
      for (int i = 0; i < kLocationCount; i++) {
        if (!(*logic)[i] || s.reached[i] || !t.inside[kTtydRandoLocations[i].region] || !s.placeOpen(i)) continue;
        s.reached[i] = 1;
        found.push_back(i);
        changed = true;
        if ((*items)[i] != 0) s.collect((*items)[i]);
      }
    }
    ends = ends || (t.exitPlace >= 0 && s.reached[t.exitPlace]);
    if (!ends) return false;
    have = s.have;
    other = s.other;
    reached = s.reached;
    for (int r = 0; r < RANDO_REGION_COUNT; r++) {
      if (t.revisit && t.inside[r]) region[r] = 1;
    }
    if (t.exitRegion >= 0) region[t.exitRegion] = 1;
    tripDone[k] = 1;
    tripFound.insert(tripFound.end(), found.begin(), found.end());
    version++;
    return true;
  }

  // The regions reached by the ways open; true if any new. A trip that can be
  // gone back to is taken as soon as it can be finished; one that can't
  // (late), only once nothing else opens, so what lies in it waits for all
  // that can be found before.
  bool pcRegions(bool late = false) {
    region[RANDO_REGION_MENU] = 1;
    bool any = false;
    for (bool changed = true; changed;) {
      changed = false;
      for (const PcLogic::Edge& e : L.edges) {
        if (region[e.from] && !region[e.to] && evalPc(*e.rule)) {
          const int k = legacy ? -1 : L.tripOf[e.to];
          if (k >= 0 && tripsOff) continue;
          if (k >= 0 && !tripDone[k]) {
            if (L.trips[k].revisit || late) changed = takeTrip(k) || changed;
            continue;
          }
          if (k >= 0 && !L.trips[k].revisit) continue; // (done, and not to be gone back to)
          region[e.to] = 1;
          version++;
          changed = true;
        }
      }
      any = any || changed;
    }
    return any;
  }

  bool placeOpen(int i) const {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    const bool own = L.replaced[i] != nullptr ? evalPc(*L.replaced[i]) : eval(l.rule);
    return region[l.region] && own && (L.extra[i] == nullptr || evalPc(*L.extra[i]));
  }

  // As Sweep::run, by this world's ways
  std::vector<int> run(const std::vector<int64_t>& item, const std::vector<char>& inLogic, bool sphere = false) {
    items = &item;
    logic = &inLogic;
    for (bool changed = true; changed;) {
      tripFound.clear();
      pcRegions();
      std::vector<int> round = tripFound;
      changed = !tripFound.empty();
      for (int i = 0; i < kLocationCount; i++) {
        if (!inLogic[i] || reached[i] || !placeOpen(i)) continue;
        reached[i] = 1;
        version++;
        round.push_back(i);
        changed = true;
        if (!sphere && item[i] != 0) collect(item[i]);
      }
      if (sphere) {
        for (size_t k = tripFound.size(); k < round.size(); k++) {
          if (item[round[k]] != 0) collect(item[round[k]]);
        }
        tripFound.clear();
        pcRegions();
        if (round.empty() && tripFound.empty()) pcRegions(true); // (nothing else: a trip with no way back)
        round.insert(round.end(), tripFound.begin(), tripFound.end());
        return round;
      }
      if (!changed) {
        tripFound.clear();
        changed = pcRegions(true); // (nothing else opens: a trip with no way back, if one ends)
      }
    }
    return {};
  }

  // the goal's Crystal Stars reached
  bool goal() const {
    int n = 0;
    for (int i : L.goal) n += reached[i] != 0;
    return n >= goalStars();
  }
};

// A hub place's item is the room's own (an init script's item, block or
// chest), not a scene's that waits for a later chapter: there a key item is
// sure to be found while the hub's story stands still.
bool roomOwnItem(const TtydApLocation& loc) {
  for (int i = loc.first; i < loc.first + loc.count; i++) {
    const std::string name = kTtydApTargets[i].name;
    const size_t colon = name.rfind("::");
    const std::string sym = colon == std::string::npos ? name : name.substr(colon + 2);
    const bool init = sym.size() > 9 && sym.compare(sym.size() - 9, 9, "_init_evt") == 0;
    if (!init && sym.rfind("mobj_box", 0) != 0) return false;
  }
  return loc.count > 0;
}

// The items this logic's rules name that a player can eat, sell or use up:
// not key items, partners or Crystal Stars (the Coconut that Keelhaul Key's
// Chuckola Cola and the Grotto's Skull Gem ask for). The generator counts an
// item as in hand for good once found, the game only while it is carried
// (heldOf), so from logic 2 their places keep them (generatePaperCut): their
// item is where the logic counts on it, and the game's one-way trips' check
// can't refuse for good because one was eaten. Only the places that can hold
// the story's items count (cooking, shops, Dazzle, the Pit and troubles never do).
std::set<int64_t> eatenNeeds(const PcLogic& L) {
  std::set<std::string> names;
  const std::function<void(const PcRule&)> pc = [&](const PcRule& r) {
    if (r.op == PcRule::kHas) names.insert(r.name);
    for (const PcRule& k : r.kids) pc(k);
  };
  const std::function<void(int)> node = [&](int n) {
    if (n < 0) return;
    const TtydRandoNode& x = kTtydRandoNodes[n];
    if (x.op == RANDO_HAS) {
      names.insert(kTtydRandoItems[x.first].name);
    } else if (x.op != RANDO_REACH_LOCATION && x.op != RANDO_REACH_REGION) {
      for (int k = 0; k < x.n; k++) node(kTtydRandoKids[x.first + k]);
    }
  };
  for (const PcLogic::Edge& e : L.edges) pc(*e.rule);
  for (int i = 0; i < kLocationCount; i++) {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    if (!L.regionOn[l.region] || (l.tags & (RANDO_TAG_TROUBLE | RANDO_TAG_SHOP | RANDO_TAG_DAZZLE |
                                            RANDO_TAG_PIT_FLOOR | RANDO_TAG_COOKING))) {
      continue;
    }
    if (L.replaced[i] != nullptr) pc(*L.replaced[i]);
    else node(l.rule);
    if (L.extra[i] != nullptr) pc(*L.extra[i]);
  }
  std::set<int64_t> out;
  for (const std::string& n : names) {
    const int64_t id = itemByName(n.c_str());
    if (id != 0 && !isStar(id) && partnerOf(id) == 0 && paperCutPoolOf(id) != kKeyItems) out.insert(id);
  }
  return out;
}

Result generatePaperCut(const Options& o) {
  const TricksScope scope(o);
  Result r;
  r.hash = seedHash(o.seed);
  const PcLogic& L = pcLogic();
  if (!L.error.empty()) {
    r.error = L.error;
    return r;
  }
  const unsigned pools = o.pools | kKeyItems; // key items and partners always move here
  if ((pools & (kBadges | kItems | kStarPieces)) == 0) {
    r.error = "the open world needs badges, items or Star Pieces shuffled too";
    return r;
  }
  const int stars = int(L.goal.size());
  const int need = o.goalStars > 0 && o.goalStars < stars ? o.goalStars : stars;
  const bool legacy = o.pcLogic < 1; // (a file made before the one-way trips: its placements as then)
  // Logic 2: the places of what a player can eat (eatenNeeds) and the places
  // a disc can't write (onEveryDisc) keep their items, on every disc alike
  const bool keepEaten = o.pcLogic >= 2;
  const std::set<int64_t> eaten = keepEaten ? eatenNeeds(L) : std::set<int64_t>();
  const auto kept = [&](const TtydRandoLocation& l) {
    return keepEaten && (!onEveryDisc(l.id) || eaten.count(l.vanilla) != 0);
  };

  // In logic: this stage's places but troubles (the game gives them, nothing
  // counts on them). Written: those whose item sits in a REL's script, and
  // the partners' own (their scene gives what is placed there). Crystal
  // Stars, shops, cooking, Dazzle and the Pit keep theirs.
  std::vector<char> inLogic(kLocationCount, 0), part(kLocationCount, 0), progOk(kLocationCount, 0);
  std::vector<char> trouble(kLocationCount, 0);
  std::vector<int> ap(kLocationCount, -1);
  std::vector<int64_t> vanilla(kLocationCount);
  for (int i = 0; i < kLocationCount; i++) {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    vanilla[i] = l.vanilla;
    ap[i] = apLocation(l.id);
    if (ap[i] < 0) {
      r.error = "location " + std::to_string(l.id) + " is not in ttyd_ap_data.h (regenerate both from one apworld)";
      return r;
    }
    if (L.regionOn[l.region] && (l.tags & RANDO_TAG_TROUBLE) && (o.extra & kExtraTroubles)) {
      // a trouble's reward: out of the logic (nothing counts on a trouble),
      // so it only takes the rest's items, never a key item
      const unsigned pool = paperCutPoolOf(l.vanilla) & pools & ~unsigned(kKeyItems);
      trouble[i] = pool != 0 && plainScriptLocation(kTtydApLocations[ap[i]], l, true) && !kept(l);
      part[i] = trouble[i];
      continue;
    }
    if (!L.regionOn[l.region] || (l.tags & RANDO_TAG_TROUBLE)) continue;
    inLogic[i] = 1;
    const TtydApLocation& loc = kTtydApLocations[ap[i]];
    const bool partner = (l.tags & RANDO_TAG_PARTNER) != 0 || partnerOf(l.vanilla) != 0;
    const bool script = plainScriptLocation(loc, l);
    if (!(script || partner) || (l.tags & RANDO_TAG_STAR) || !placedHere(loc) || kept(l)) continue;
    if (std::find(L.w.fixed.begin(), L.w.fixed.end(), l.id) != L.w.fixed.end()) continue;
    if ((paperCutPoolOf(l.vanilla) & pools) == 0) continue;
    part[i] = 1;
    progOk[i] = !L.hub[l.region] || roomOwnItem(loc) || L.hubScene[i];
  }

  // What can't be reached even with everything in hand stays as it is (it
  // belongs to a later stage: Yoshi, Bobbery, the Ultra Boots...).
  std::vector<int64_t> progression, rest;
  {
    std::vector<int64_t> none = vanilla;
    PcSweep s;
    s.legacy = legacy;
    s.need = need;
    for (int i = 0; i < kLocationCount; i++) {
      if (part[i]) {
        none[i] = 0;
        s.collect(vanilla[i]);
      }
    }
    for (const char* n : L.w.extraItems) s.collect(itemByName(n));
    s.run(none, inLogic);
    if (!s.goal()) {
      r.error = "the logic can't reach the goal's Crystal Stars even with every key item in hand";
      return r;
    }
    for (int i = 0; i < kLocationCount; i++) {
      if (part[i] && !trouble[i] && !s.reached[i]) part[i] = 0;
    }
  }
  std::vector<int> slots;
  for (int i = 0; i < kLocationCount; i++) {
    if (!part[i]) continue;
    slots.push_back(i);
    r.shuffled++;
    (paperCutPoolOf(vanilla[i]) == kKeyItems ? progression : rest).push_back(vanilla[i]);
  }
  // A key item whose own place this stage can't reach (the Old Letter) is
  // placed anyway, in the place of one other item.
  for (const char* name : L.w.extraItems) {
    const int64_t id = itemByName(name);
    if (std::find(progression.begin(), progression.end(), id) != progression.end()) continue;
    if (rest.empty()) {
      r.error = std::string("no room for ") + name;
      return r;
    }
    rest.pop_back();
    progression.push_back(id);
  }

  Rng rng(r.hash ^ (uint64_t(pools) << 56) ^ (uint64_t(kPaperCut) << 62) ^ (uint64_t(L.w.stage) << 48) ^
          (uint64_t(need) << 40) ^ logicSalt(o));
  const bool noLogic = o.logicLevel == kNoLogic;
  std::vector<int64_t> at;
  for (r.attempts = 1; r.attempts <= kMaxAttempts; r.attempts++) {
    at = vanilla;
    for (int i : slots) at[i] = 0;
    if (noLogic) {
      // no logic: every item anywhere among the places, nothing checked
      std::vector<int64_t> items = progression;
      items.insert(items.end(), rest.begin(), rest.end());
      rng.shuffle(items);
      for (size_t k = 0; k < slots.size(); k++) at[slots[k]] = items[k];
      r.chaptersReached = 0;
      break;
    }
    std::vector<int64_t> prog = progression;
    rng.shuffle(prog);
    bool failed = false;
    while (!prog.empty()) {
      const int64_t item = prog.back();
      prog.pop_back();
      PcSweep s;
      s.legacy = legacy;
      s.need = need;
      for (int64_t p : prog) s.collect(p);
      s.run(at, inLogic);
      std::vector<int> open;
      for (int i : slots) {
        if (at[i] == 0 && progOk[i] && s.reached[i]) open.push_back(i);
      }
      if (open.empty()) {
        failed = true;
        break;
      }
      at[open[rng.below(uint32_t(open.size()))]] = item;
    }
    if (failed) continue;
    std::vector<int> empty;
    for (int i : slots) {
      if (at[i] == 0) empty.push_back(i);
    }
    std::vector<int64_t> items = rest;
    rng.shuffle(items);
    for (size_t k = 0; k < empty.size(); k++) at[empty[k]] = items[k];
    // the goal, and every chapter's way in (what lies past the goal too:
    // assumed fill leaves every key item reachable; this checks it)
    PcSweep s;
    s.legacy = legacy;
    s.need = need;
    s.run(at, inLogic);
    bool ways = true;
    for (const char* way : L.w.ways) ways = ways && s.region[PcLogic::region(way)];
    // and the end: the Shadow Queen (her chapter needs no more stars than the goal)
    ways = ways && (legacy || s.region[PcLogic::region("Shadow Queen")]);
    if (s.goal() && ways) {
      r.chaptersReached = int(L.w.ways.size());
      break;
    }
  }
  if (r.attempts > kMaxAttempts) {
    r.error = "no placement found in " + std::to_string(kMaxAttempts) + " tries";
    return r;
  }
  for (int i : slots) {
    r.placements.push_back({ap[i], at[i], true});
    r.moved += at[i] != vanilla[i];
  }

  std::string& t = r.spoiler;
  t += "Paper Cut built-in randomizer\n";
  t += "Seed: " + o.seed + " (" + hex(r.hash) + ")\n";
  t += "Logic: Paper Cut's open world, stage " + std::to_string(L.w.stage) + " (Rogueport, chapters";
  for (size_t c = 0; c < L.w.chapters.size(); c++) t += (c ? ", " : " ") + std::to_string(L.w.chapters[c]);
  t += ")\nLogic version: " + std::to_string(o.pcLogic) +
       (legacy ? " (before the one-way trips: the ship, the train's first ride and the moon)" : "") + "\n";
  t += logicLine(o);
  if (const unsigned g = gatesOf(o)) {
    static const char* const kPipes[4] = {"tik_02's pipe", "tik_03's pipe", "tik_08's pipe", "tik_07's pipe"};
    t += "Chapter gates:";
    for (int k = 0; k < 4; k++) {
      t += std::string(k ? ";" : "") + " " + kPipes[k] + " -> " + pcGateRegions()[g >> (k * 2) & 3];
    }
    t += "\n";
  }
  if (const std::vector<int> d = doorsOf(o); !d.empty()) {
    const std::vector<DoorHalf>& h = doorHalves();
    const int roomHalves = int(pcRoomDoors().size()) * 2;
    std::string rooms, ways;
    for (int k = 0; k < roomHalves; k++) {
      if (d[k] != (k ^ 1) && k < d[k]) {
        rooms += std::string(rooms.empty() ? "" : ";") + " " + h[k].map + "." + h[k].bero + " <-> " + h[d[k]].map + "." +
                 h[d[k]].bero;
      }
    }
    for (size_t k = 0; k < pcWays().size(); k++) {
      const int p = (d[roomHalves + int(k) * 2] - roomHalves) / 2;
      if (p != int(k)) ways += std::string(ways.empty() ? "" : ";") + " " + pcWays()[k].name + " -> " + pcWays()[p].to;
    }
    if (!rooms.empty()) t += "Room doors:" + rooms + "\n";
    if (!ways.empty()) t += "Ways between areas:" + ways + "\n";
  }
  if (const unsigned rides = ridesOf(o)) {
    t += "Rides:";
    for (int k = 0; k < 3; k++) {
      t += std::string(k ? ";" : "") + " " + pcRides()[k].name + " -> " + pcRides()[rides >> (k * 2) & 3].to;
    }
    t += "\n";
  }
  t += "Goal: " + std::to_string(need) + " of the " + std::to_string(stars) + " Crystal Stars\n";
  t += "Shuffled: " + poolsText(pools) + "\n";
  t += std::to_string(r.shuffled) + " locations, " + std::to_string(r.moved) + " items moved\n";
  if (!L.w.extraItems.empty()) {
    t += "Placed without a place of their own here:";
    for (const char* n : L.w.extraItems) t += std::string(" ") + n;
    t += "\n";
  }
  t += "\nPlaythrough (what the logic expects, sphere by sphere; key items and partners):\n";
  {
    PcSweep s;
    s.legacy = legacy;
    s.need = need;
    for (int sphere = 1;; sphere++) {
      const std::vector<int> round = s.run(at, inLogic, true);
      if (round.empty()) break;
      std::string line;
      for (int i : round) {
        if (part[i] && paperCutPoolOf(at[i]) == kKeyItems) line += "  " + locationName(i) + ": " + itemName(at[i]) + "\n";
      }
      if (!line.empty()) t += "Sphere " + std::to_string(sphere) + "\n" + line;
    }
  }
  t += "\nLocations:\n";
  for (int region = 0; region < RANDO_REGION_COUNT; region++) {
    bool header = false;
    for (int i : slots) {
      if (kTtydRandoLocations[i].region != region) continue;
      if (!header) {
        t += std::string("[") + kTtydRandoRegions[region] + "]\n";
        header = true;
      }
      t += "  " + locationName(i) + ": " + itemName(at[i]) + (at[i] == vanilla[i] ? " (unchanged)" : "") + "\n";
    }
  }
  r.ok = true;
  return r;
}

} // namespace

bool pcTripCanEnd(const Options& o, const std::vector<Placement>& placements, int trip,
                  const std::vector<int64_t>& held, const std::vector<int>& found) {
  const TricksScope scope(o);
  if (o.logicLevel == kNoLogic) return true; // (no logic: Mario goes where he likes)
  const PcLogic& L = pcLogic();
  if (!L.error.empty() || trip < 0 || trip >= int(L.trips.size())) return true;
  static const std::map<int64_t, int> kIndex = [] { // Archipelago location id -> kTtydRandoLocations index
    std::map<int64_t, int> m;
    for (int i = 0; i < kLocationCount; i++) m[kTtydRandoLocations[i].id] = i;
    return m;
  }();
  // the places in the logic and what they hold, as generatePaperCut has them
  std::vector<int64_t> at(kLocationCount);
  std::vector<char> inLogic(kLocationCount, 0);
  for (int i = 0; i < kLocationCount; i++) {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    at[i] = l.vanilla;
    inLogic[i] = L.regionOn[l.region] && !(l.tags & RANDO_TAG_TROUBLE);
  }
  for (const Placement& p : placements) {
    const auto it = kIndex.find(kTtydApLocations[p.location].id);
    if (it != kIndex.end()) at[it->second] = p.item;
  }
  PcSweep s;
  s.need = o.goalStars;
  s.tripsOff = true;
  s.items = &at;
  s.logic = &inLogic;
  for (int64_t h : held) s.collect(h);
  for (int f : found) {
    const auto it = kIndex.find(kTtydApLocations[f].id);
    if (it != kIndex.end()) s.reached[it->second] = 1;
  }
  s.pcRegions(); // (the regions what is in hand opens: some rules on the way ask for one)
  return s.takeTrip(trip);
}

std::vector<int64_t> heldOf(const Options& o, const std::vector<Placement>& placements, const FileView& f) {
  (void)o;
  static const std::map<int64_t, int> kIndex = [] { // Archipelago location id -> kTtydRandoLocations index
    std::map<int64_t, int> m;
    for (int i = 0; i < kLocationCount; i++) m[kTtydRandoLocations[i].id] = i;
    return m;
  }();
  // the copies the places found gave: their placements, else their own items
  const int apCount = int(sizeof(kTtydApLocations) / sizeof(kTtydApLocations[0]));
  std::map<int, int64_t> placedAt;
  for (const Placement& p : placements) placedAt[p.location] = p.item;
  std::map<int64_t, int> given;
  for (int f0 : f.found) {
    if (f0 < 0 || f0 >= apCount) continue;
    const auto p = placedAt.find(f0);
    if (p != placedAt.end()) {
      given[p->second]++;
    } else if (const auto it = kIndex.find(kTtydApLocations[f0].id); it != kIndex.end()) {
      given[kTtydRandoLocations[it->second].vanilla]++;
    }
  }
  std::vector<int64_t> held;
  std::set<int64_t> seen;
  for (const TtydRandoItem& i : kTtydRandoItemNames) {
    if (i.id == 0 || isStar(i.id) || !seen.insert(i.id).second) continue;
    if (const int p = partnerOf(i.id)) {
      if (f.partner && f.partner(p)) held.push_back(i.id);
      continue;
    }
    int count = 0;
    if (f.pouch) {
      if (std::strcmp(i.name, "Progressive Boots") == 0) {
        count = (f.pouch(0x07) > 0) + (f.pouch(0x08) > 0); // the Super, the Ultra Boots
      } else if (std::strcmp(i.name, "Progressive Hammer") == 0) {
        count = (f.pouch(0x0A) > 0) + (f.pouch(0x0B) > 0); // the Super, the Ultra Hammer
      } else if (const int rom = gameItemOf(i.id); rom > 0) {
        count = f.pouch(rom);
      }
    }
    if (paperCutPoolOf(i.id) == kKeyItems) {
      const auto g = given.find(i.id);
      if (g != given.end()) count = std::max(count, g->second);
    }
    for (int k = 0; k < count; k++) held.push_back(i.id);
  }
  const int stars = int(sizeof(kTtydRandoStars) / sizeof(kTtydRandoStars[0]));
  for (int k = 0; k < f.stars && k < stars; k++) held.push_back(kTtydRandoStars[k]);
  return held;
}

namespace {

// Shop stock (Options::extra's kExtraShops): the shops' shelves of items
// (not badges, which a shop would sell again and again) swap their items
// among themselves; the price on a shelf becomes its new item's own (the
// game side writes the item table's buy price where the apworld has the
// shelf's price). The shelves are the shop tables in the areas' RELs.
void shuffleShops(const Options& o, Result& r) {
  if (!(o.extra & kExtraShops) || o.logic == kOpenWorld) return;
  std::vector<int> shelves;
  std::vector<int64_t> stock;
  for (int i = 0; i < kLocationCount; i++) {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    const int ap = apLocation(l.id);
    if (ap < 0 || !(l.tags & RANDO_TAG_SHOP)) continue;
    const TtydApLocation& loc = kTtydApLocations[ap];
    if (!(loc.flags & TTYD_AP_SHOP) || loc.count == 0 || !placedHere(loc)) continue;
    bool ok = true;
    for (int t = loc.first; t < loc.first + loc.count; t++) {
      ok = ok && kTtydApTargets[t].kind == TTYD_AP_DATA && kTtydApTargets[t].area[0] != '\0';
    }
    const TtydApItem* it = apItem(l.vanilla);
    if (!ok || it == nullptr || it->romId < 0x79 || it->romId >= 0xF0) continue;
    shelves.push_back(ap);
    stock.push_back(l.vanilla);
  }
  const std::vector<int64_t> own = stock;
  Rng rng(r.hash ^ 0x53484F5053484F50ull ^ (uint64_t(o.logic) << 60)); // (its own numbers: the rest stays as it was)
  rng.shuffle(stock);
  for (size_t k = 0; k < shelves.size(); k++) {
    r.placements.push_back({shelves[k], stock[k], true});
    r.moved += stock[k] != own[k];
  }
  r.shuffled += int(shelves.size());
  r.spoiler += "\nShop stock (" + std::to_string(shelves.size()) + " shelves):\n";
  for (size_t k = 0; k < shelves.size(); k++) {
    r.spoiler += std::string("  ") + kTtydApLocations[shelves[k]].name + ": " + itemName(stock[k]) + "\n";
  }
}

} // namespace

int gateTarget(const Options& o, int gate) {
  const unsigned g = gatesOf(o);
  if (gate < 0 || gate > 3 || g == 0) return gate;
  return int(g >> (gate * 2) & 3);
}

const std::vector<DoorHalf>& doorHalves() {
  static const std::vector<DoorHalf> h = [] {
    std::vector<DoorHalf> v;
    for (const PcRoomDoor& d : pcRoomDoors()) {
      v.push_back({d.a.map, d.a.bero});
      v.push_back({d.b.map, d.b.bero});
    }
    for (const PcWay& w : pcWays()) {
      v.push_back({w.exit.map, w.exit.bero});
      v.push_back({w.entrance.map, w.entrance.bero});
    }
    return v;
  }();
  return h;
}

std::vector<int> doorTargets(const Options& o) {
  std::vector<int> t = doorsOf(o);
  if (t.empty()) {
    t.resize(doorHalves().size());
    for (size_t h = 0; h < t.size(); h++) t[h] = int(h ^ 1);
  }
  return t;
}

int rideTarget(const Options& o, int ride) {
  const unsigned r = ridesOf(o);
  if (ride < 0 || ride > 2 || r == 0) return ride;
  return int(r >> (ride * 2) & 3);
}

const char* regionName(int region) {
  return region >= 0 && region < RANDO_REGION_COUNT ? kTtydRandoRegions[region] : "?";
}

std::vector<Tracked> track(const Options& o, const std::vector<Placement>& placements,
                           const std::vector<int64_t>& held, const std::vector<int>& found) {
  const TricksScope scope(o);
  static const std::map<int64_t, int> kIndex = [] { // Archipelago location id -> kTtydRandoLocations index
    std::map<int64_t, int> m;
    for (int i = 0; i < kLocationCount; i++) m[kTtydRandoLocations[i].id] = i;
    return m;
  }();
  std::vector<char> isFound(sizeof(kTtydApLocations) / sizeof(kTtydApLocations[0]), 0);
  for (int f : found) {
    if (f >= 0 && size_t(f) < isFound.size()) isFound[size_t(f)] = 1;
  }
  const bool logic = o.logicLevel != kNoLogic && (o.logic == kPaperCut || o.logic == kOpenWorld);
  std::vector<char> reached(kLocationCount, 1);
  if (logic) {
    const std::vector<int64_t> none(kLocationCount, 0); // (nothing more collected: what is in hand now)
    std::vector<char> inLogic(kLocationCount, 0);
    if (o.logic == kPaperCut) {
      const PcLogic& L = pcLogic();
      for (int i = 0; i < kLocationCount; i++) {
        const TtydRandoLocation& l = kTtydRandoLocations[i];
        inLogic[i] = L.regionOn[l.region] && !(l.tags & RANDO_TAG_TROUBLE);
      }
      PcSweep s;
      s.need = o.goalStars;
      s.legacy = o.pcLogic < 1;
      for (int64_t h : held) s.collect(h);
      for (int f : found) {
        const auto it = kIndex.find(kTtydApLocations[f].id);
        if (it != kIndex.end()) s.reached[it->second] = 1;
      }
      s.run(none, inLogic);
      reached = s.reached;
    } else {
      for (int i = 0; i < kLocationCount; i++) inLogic[i] = (kTtydRandoLocations[i].tags & RANDO_TAG_TROUBLE) == 0;
      Sweep s;
      for (int64_t h : held) s.collect(h);
      s.run(none, inLogic);
      reached = s.reached;
    }
  }
  std::vector<Tracked> out;
  for (const Placement& p : placements) {
    if (!p.shuffled) continue;
    const auto it = kIndex.find(kTtydApLocations[p.location].id);
    if (it == kIndex.end()) continue;
    const bool f = isFound[size_t(p.location)] != 0;
    out.push_back({p.location, kTtydRandoLocations[it->second].region, f, f || reached[it->second] != 0});
  }
  return out;
}

Result generate(const Options& o) {
  if (o.logic == kPaperCut) {
    Result pc = generatePaperCut(o);
    if (pc.ok) shuffleShops(o, pc);
    return pc;
  }
  Result r;
  r.hash = seedHash(o.seed);
  const bool open = o.logic == kOpenWorld;
  // The open world's logic has every key item and partner in the shuffle (the
  // apworld never keeps them where the game has them: there, they would lock
  // each other away); the vanilla story none.
  const unsigned pools = open ? o.pools | kKeyItems : o.pools & ~unsigned(kKeyItems);
  if (open && (pools & (kBadges | kItems | kStarPieces)) == 0) {
    // the key items' own places alone are too few early on for them all
    r.error = "the open world needs badges, items or Star Pieces shuffled too";
    return r;
  }

  // Which locations count in the logic, which get written, which take part
  std::vector<char> inLogic(kLocationCount, 0), written(kLocationCount, 0), part(kLocationCount, 0);
  std::vector<int> ap(kLocationCount, -1);
  for (int i = 0; i < kLocationCount; i++) {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    ap[i] = apLocation(l.id);
    if (ap[i] < 0) {
      r.error = "location " + std::to_string(l.id) + " is not in ttyd_ap_data.h (regenerate both from one apworld)";
      return r;
    }
    const TtydApLocation& loc = kTtydApLocations[ap[i]];
    const unsigned pool = poolOf(l.vanilla);
    if (open) {
      // As the apworld with its defaults and these off: shops, troubles,
      // cooking, the Pit's floors, Dazzle's rewards keep their items (the
      // game gives them; troubles aren't counted on); the rest is written.
      inLogic[i] = (l.tags & RANDO_TAG_TROUBLE) == 0;
      written[i] = inLogic[i] && loc.count > 0 && placedHere(loc) && !(loc.flags & (TTYD_AP_SHOP | TTYD_AP_TATTLE)) &&
                   !(l.tags & (RANDO_TAG_SHOP | RANDO_TAG_COOKING | RANDO_TAG_PIT_FLOOR | RANDO_TAG_DAZZLE));
      // (the first partner stays: the apworld's starting partner, Goombella)
      part[i] = written[i] && (pool & pools) != 0 && l.id != kStartingPartnerLocation;
    } else {
      inLogic[i] = 1;
      // (a place some disc can't write is a slot on every disc, as in 0.4.1:
      // the same draws, and the same placements, everywhere; only the disc
      // that can't write it keeps its own item there)
      part[i] = (pool & pools) != 0 && plainScriptLocation(loc, l, (o.extra & kExtraTroubles) != 0, true) &&
                vanillaSafe(l.vanilla);
      written[i] = part[i] && placedHere(loc);
    }
    r.shuffled += part[i];
  }
  if (r.shuffled == 0) {
    r.error = "nothing to shuffle";
    return r;
  }

  // The items that take part, the ones the logic asks for first
  std::vector<int> slots;
  std::vector<int64_t> progression, rest;
  for (int i = 0; i < kLocationCount; i++) {
    if (!part[i]) continue;
    slots.push_back(i);
    const int64_t item = kTtydRandoLocations[i].vanilla;
    (open && o.logicLevel != kNoLogic && (logicItem(item) >= 0 || isStar(item)) ? progression : rest).push_back(item);
  }

  // With every one of them in hand, the goal must be reachable (the data's sanity)
  std::vector<int64_t> vanilla(kLocationCount);
  for (int i = 0; i < kLocationCount; i++) vanilla[i] = kTtydRandoLocations[i].vanilla;
  if (open && o.logicLevel != kNoLogic) {
    std::vector<int64_t> none = vanilla;
    for (int i : slots) none[i] = 0;
    Sweep s;
    for (int64_t p : progression) s.collect(p);
    s.run(none, inLogic);
    if (!s.region[RANDO_REGION_GOAL]) {
      r.error = "the logic can't reach the Shadow Queen even with every key item in hand";
      return r;
    }
  }

  Rng rng(r.hash ^ (uint64_t(pools) << 56) ^ (uint64_t(o.logic) << 62) ^ (open ? logicSalt(o) : 0));
  std::vector<int64_t> at;
  for (r.attempts = 1; r.attempts <= kMaxAttempts; r.attempts++) {
    at = vanilla;
    for (int i : slots) at[i] = 0;
    std::vector<int64_t> prog = progression;
    rng.shuffle(prog);
    bool failed = false;
    // assumed fill: the last item first, with all the others in hand
    while (!prog.empty() && !failed) {
      const int64_t item = prog.back();
      prog.pop_back();
      Sweep s;
      for (int64_t p : prog) s.collect(p);
      s.run(at, inLogic);
      std::vector<int> open_;
      for (int i : slots) {
        if (at[i] == 0 && s.reached[i]) open_.push_back(i);
      }
      if (open_.empty()) {
        failed = true;
        break;
      }
      at[open_[rng.below(uint32_t(open_.size()))]] = item;
    }
    if (failed) continue;
    std::vector<int> empty;
    for (int i : slots) {
      if (at[i] == 0) empty.push_back(i);
    }
    std::vector<int64_t> items = rest;
    rng.shuffle(items);
    for (size_t k = 0; k < empty.size(); k++) at[empty[k]] = items[k];
    if (open && o.logicLevel != kNoLogic) {
      Sweep s;
      s.run(at, inLogic);
      if (!s.region[RANDO_REGION_GOAL]) continue;
    }
    break;
  }
  if (r.attempts > kMaxAttempts) {
    r.error = "no placement found in " + std::to_string(kMaxAttempts) + " tries";
    return r;
  }

  for (int i = 0; i < kLocationCount; i++) {
    if (!written[i]) continue;
    r.placements.push_back({ap[i], at[i], part[i] != 0});
    r.moved += part[i] && at[i] != vanilla[i];
  }

  // The spoiler log
  std::string& t = r.spoiler;
  t += "Paper Cut built-in randomizer\n";
  t += "Seed: " + o.seed + " (" + hex(r.hash) + ")\n";
  t += std::string("Logic: ") + (open ? "open world (the TTYD AP mod)" : "vanilla story") + "\n";
  if (open) t += logicLine(o);
  t += "Shuffled: " + poolsText(pools) + "\n";
  t += std::to_string(r.shuffled) + " locations, " + std::to_string(r.moved) + " items moved\n";
  if (open) {
    t += "\nPlaythrough (what the logic expects, sphere by sphere; key items only):\n";
    Sweep s;
    for (int sphere = 1;; sphere++) {
      const std::vector<int> round = s.run(at, inLogic, true);
      if (round.empty()) break;
      std::string line;
      for (int i : round) {
        if (part[i] && (logicItem(at[i]) >= 0 || isStar(at[i])) && poolOf(at[i]) == kKeyItems) {
          line += "  " + locationName(i) + ": " + itemName(at[i]) + "\n";
        }
      }
      if (!line.empty()) t += "Sphere " + std::to_string(sphere) + "\n" + line;
    }
  }
  t += "\nLocations:\n";
  for (int region = 0; region < RANDO_REGION_COUNT; region++) {
    bool header = false;
    for (int i = 0; i < kLocationCount; i++) {
      // (a slot this disc can't write keeps the game's item: not listed)
      if (!part[i] || !written[i] || kTtydRandoLocations[i].region != region) continue;
      if (!header) {
        t += std::string("[") + kTtydRandoRegions[region] + "]\n";
        header = true;
      }
      t += "  " + locationName(i) + ": " + itemName(at[i]);
      if (at[i] == vanilla[i]) t += " (unchanged)";
      t += "\n";
    }
  }
  r.ok = true;
  shuffleShops(o, r);
  return r;
}

} // namespace ttyd_rando
