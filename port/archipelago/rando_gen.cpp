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

#include <algorithm>
#include <cstdio>
#include <map>
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

// A vanilla-story location: its item given by the game's own script, nothing
// of the TTYD AP mod's (its low-memory tables, the DOL's shop and Dazzle
// tables), and nothing the story itself hands over.
bool plainScriptLocation(const TtydApLocation& loc, const TtydRandoLocation& r) {
  if (loc.count == 0 || (loc.flags & (TTYD_AP_SHOP | TTYD_AP_TATTLE))) return false;
  if (r.tags & (RANDO_TAG_SHOP | RANDO_TAG_TROUBLE | RANDO_TAG_COOKING | RANDO_TAG_DAZZLE | RANDO_TAG_PIT_FLOOR |
                RANDO_TAG_STAR | RANDO_TAG_PARTNER)) {
    return false;
  }
  for (int i = loc.first; i < loc.first + loc.count; i++) {
    const TtydApTarget& t = kTtydApTargets[i];
    if (t.kind != TTYD_AP_SCRIPT || t.area[0] == '\0' || t.price) return false;
  }
  return true;
}

// An item the vanilla game can be given anywhere and never needs for its story
bool vanillaSafe(int64_t item) {
  const TtydApItem* it = apItem(item);
  if (it == nullptr) return false;
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

std::string locationName(int i) {
  const int ap = apLocation(kTtydRandoLocations[i].id);
  return ap >= 0 ? kTtydApLocations[ap].name : "?";
}

} // namespace

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
  std::vector<char> hubScene = std::vector<char>(kLocationCount, 0); // a Rogueport scene that may hold a key item
  std::map<std::string, int> locationIndex; // by name: kTtydRandoLocations' index
  std::map<std::string, int> itemIndex;     // by name: kTtydRandoItems' index
  std::vector<int> goal;                    // the goal places' indexes
  std::string error;

  static int region(const char* name) {
    for (int i = 0; i < RANDO_REGION_COUNT; i++) {
      if (std::string(kTtydRandoRegions[i]) == name) return i;
    }
    return -1;
  }

  bool check(const PcRule& r) {
    if (r.op == PcRule::kHas && !itemIndex.count(r.name)) {
      error = "pc_world.h: no item \"" + r.name + "\" in the logic";
    } else if (r.op == PcRule::kReach && !locationIndex.count(r.name)) {
      error = "pc_world.h: no place \"" + r.name + "\"";
    } else if (r.op == PcRule::kRegion && region(r.name.c_str()) < 0) {
      error = "pc_world.h: no region \"" + r.name + "\"";
    }
    for (const PcRule& k : r.kids) check(k);
    return error.empty();
  }

  PcLogic() {
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
      const int a = region(e.from), b = region(e.to);
      if (a < 0 || b < 0) {
        error = std::string("pc_world.h: an edge's region ") + e.from + " -> " + e.to;
        continue;
      }
      check(e.rule);
      edges.push_back({a, b, &e.rule});
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
  }
};

const PcLogic& pcLogic() {
  static const PcLogic l;
  return l;
}

struct PcSweep : Sweep {
  const PcLogic& L = pcLogic();

  bool evalPc(const PcRule& r) const {
    switch (r.op) {
    case PcRule::kTrue: return true;
    case PcRule::kHas: return have[L.itemIndex.at(r.name)] >= r.count;
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

  void pcRegions() {
    region[RANDO_REGION_MENU] = 1;
    for (bool changed = true; changed;) {
      changed = false;
      for (const PcLogic::Edge& e : L.edges) {
        if (region[e.from] && !region[e.to] && evalPc(*e.rule)) {
          region[e.to] = 1;
          changed = true;
        }
      }
    }
  }

  bool placeOpen(int i) const {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    return region[l.region] && eval(l.rule) && (L.extra[i] == nullptr || evalPc(*L.extra[i]));
  }

  // As Sweep::run, by this world's ways
  std::vector<int> run(const std::vector<int64_t>& item, const std::vector<char>& inLogic, bool sphere = false) {
    for (bool changed = true; changed;) {
      changed = false;
      pcRegions();
      std::vector<int> round;
      for (int i = 0; i < kLocationCount; i++) {
        if (!inLogic[i] || reached[i] || !placeOpen(i)) continue;
        reached[i] = 1;
        round.push_back(i);
        changed = true;
        if (!sphere && item[i] != 0) collect(item[i]);
      }
      if (sphere) {
        for (int i : round) {
          if (item[i] != 0) collect(item[i]);
        }
        pcRegions();
        return round;
      }
    }
    return {};
  }

  // need of the goal's Crystal Stars reached (0: all of them)
  bool goal(int need = 0) const {
    int n = 0;
    for (int i : L.goal) n += reached[i] != 0;
    return n >= (need > 0 && need < int(L.goal.size()) ? need : int(L.goal.size()));
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

Result generatePaperCut(const Options& o) {
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

  // In logic: this stage's places but troubles (the game gives them, nothing
  // counts on them). Written: those whose item sits in a REL's script, and
  // the partners' own (their scene gives what is placed there). Crystal
  // Stars, shops, cooking, Dazzle and the Pit keep theirs.
  std::vector<char> inLogic(kLocationCount, 0), part(kLocationCount, 0), progOk(kLocationCount, 0);
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
    if (!L.regionOn[l.region] || (l.tags & RANDO_TAG_TROUBLE)) continue;
    inLogic[i] = 1;
    const TtydApLocation& loc = kTtydApLocations[ap[i]];
    const bool partner = (l.tags & RANDO_TAG_PARTNER) != 0 || partnerOf(l.vanilla) != 0;
    const bool script = plainScriptLocation(loc, l);
    if (!(script || partner) || (l.tags & RANDO_TAG_STAR)) continue;
    if (std::find(L.w.fixed.begin(), L.w.fixed.end(), l.id) != L.w.fixed.end()) continue;
    if ((poolOf(l.vanilla) & pools) == 0) continue;
    part[i] = 1;
    progOk[i] = !L.hub[l.region] || roomOwnItem(loc) || L.hubScene[i];
  }

  // What can't be reached even with everything in hand stays as it is (it
  // belongs to a later stage: Yoshi, Bobbery, the Ultra Boots...).
  std::vector<int64_t> progression, rest;
  {
    std::vector<int64_t> none = vanilla;
    PcSweep s;
    for (int i = 0; i < kLocationCount; i++) {
      if (part[i]) {
        none[i] = 0;
        s.collect(vanilla[i]);
      }
    }
    for (const char* n : L.w.extraItems) s.collect(itemByName(n));
    s.run(none, inLogic);
    if (!s.goal(need)) {
      r.error = "the logic can't reach the goal's Crystal Stars even with every key item in hand";
      return r;
    }
    for (int i = 0; i < kLocationCount; i++) {
      if (part[i] && !s.reached[i]) part[i] = 0;
    }
  }
  std::vector<int> slots;
  for (int i = 0; i < kLocationCount; i++) {
    if (!part[i]) continue;
    slots.push_back(i);
    r.shuffled++;
    (poolOf(vanilla[i]) == kKeyItems ? progression : rest).push_back(vanilla[i]);
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
          (uint64_t(need) << 40));
  std::vector<int64_t> at;
  for (r.attempts = 1; r.attempts <= kMaxAttempts; r.attempts++) {
    at = vanilla;
    for (int i : slots) at[i] = 0;
    std::vector<int64_t> prog = progression;
    rng.shuffle(prog);
    bool failed = false;
    while (!prog.empty()) {
      const int64_t item = prog.back();
      prog.pop_back();
      PcSweep s;
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
    PcSweep s;
    s.run(at, inLogic);
    if (s.goal(need)) break;
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
  t += ")\nGoal: " + std::to_string(need) + " of the " + std::to_string(stars) + " Crystal Stars\n";
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
    for (int sphere = 1;; sphere++) {
      const std::vector<int> round = s.run(at, inLogic, true);
      if (round.empty()) break;
      std::string line;
      for (int i : round) {
        if (part[i] && poolOf(at[i]) == kKeyItems) line += "  " + locationName(i) + ": " + itemName(at[i]) + "\n";
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

Result generate(const Options& o) {
  if (o.logic == kPaperCut) return generatePaperCut(o);
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
      written[i] = inLogic[i] && loc.count > 0 && !(loc.flags & (TTYD_AP_SHOP | TTYD_AP_TATTLE)) &&
                   !(l.tags & (RANDO_TAG_SHOP | RANDO_TAG_COOKING | RANDO_TAG_PIT_FLOOR | RANDO_TAG_DAZZLE));
      // (the first partner stays: the apworld's starting partner, Goombella)
      part[i] = written[i] && (pool & pools) != 0 && l.id != kStartingPartnerLocation;
    } else {
      inLogic[i] = 1;
      part[i] = (pool & pools) != 0 && plainScriptLocation(loc, l) && vanillaSafe(l.vanilla);
      written[i] = part[i];
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
    (open && (logicItem(item) >= 0 || isStar(item)) ? progression : rest).push_back(item);
  }

  // With every one of them in hand, the goal must be reachable (the data's sanity)
  std::vector<int64_t> vanilla(kLocationCount);
  for (int i = 0; i < kLocationCount; i++) vanilla[i] = kTtydRandoLocations[i].vanilla;
  if (open) {
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

  Rng rng(r.hash ^ (uint64_t(pools) << 56) ^ (uint64_t(o.logic) << 62));
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
    if (open) {
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
      if (!part[i] || kTtydRandoLocations[i].region != region) continue;
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
  return r;
}

} // namespace ttyd_rando
