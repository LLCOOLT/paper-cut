// rando_solve: a seed solver for the built-in randomizer (docs/RANDOMIZER.md),
// on the host, without the game. For each seed and settings combination it
// takes the generator's placements (rando_gen.cpp's Result, nothing else of
// it) and plays them with two models of its own:
//
// - "gen": the generator's own logic, written again here from the same tables
//   (ttyd_rando_logic.h, pc_world.h): collect what each reachable place holds
//   until nothing more opens; the goal's Crystal Stars, every chapter's way
//   in, the Thousand-Year Door and the Shadow Queen must be reached.
// - "game": the same, plus what the decomp's map scripts do that the
//   generator's logic doesn't model (kGameTraps below, each with the script
//   that shows it): trips with no way back until a scene ends. A player who
//   takes such a trip while missing something its way out needs is stuck
//   there for good. The solver withholds each key item in turn (every copy,
//   or all copies past the k-th of a progressive one), lets the player take
//   every trip it can without it, and checks the trip can still be finished
//   from what was in hand and what lies on the way.
//
// It also checks what every location gives: each location at most one
// placement, the items placed are the items taken, and no place written that
// the game doesn't give from its own script; and that the same seed and
// settings give the same placements again (online: both sides generate).
//
//   rando_solve [seeds per combination] [--threads N] [--logic pc|ap|vanilla|all]
//   rando_solve --seed TEXT [--pools N] [--goal N] [--logic pc]   (one seed, in detail)
//   rando_solve --digests N   (print a digest per seed: compare two machines' runs, or two discs' builds)
//   rando_solve --ingame N [--goal N] [--pools N] [--pclogic N]
//     (N open-world seeds played as the game plays them: each trip taken when the game's own
//     check, heldOf and pcTripCanEnd from what the game sees, lets him; every seed must finish)
//
// The guard comparisons build what Mario holds as the game does (gameHeld: heldOf, the code
// open_world_trips.cpp runs), from a pouch of keys used up and everything else eaten.
#include "../../port/archipelago/rando_gen.h"
#include "../../port/archipelago/ttyd_ap_data.h"
#include "../../port/archipelago/ttyd_rando_logic.h"
#include "../../port/archipelago/pc_world.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace ttyd_rando;

namespace {

constexpr int kLocs = int(sizeof(kTtydRandoLocations) / sizeof(kTtydRandoLocations[0]));
constexpr int kItemN = int(sizeof(kTtydRandoItems) / sizeof(kTtydRandoItems[0]));
constexpr int kApLocs = int(sizeof(kTtydApLocations) / sizeof(kTtydApLocations[0]));

int regionOf(const char* name) {
  for (int i = 0; i < RANDO_REGION_COUNT; i++) {
    if (std::strcmp(kTtydRandoRegions[i], name) == 0) return i;
  }
  std::fprintf(stderr, "rando_solve: no region \"%s\"\n", name);
  std::exit(2);
}

// --- the tables, by name and id ---
struct Tables {
  std::map<int64_t, int> logicItem;     // Archipelago id -> kTtydRandoItems index
  std::map<std::string, int> itemIndex; // name -> kTtydRandoItems index
  std::map<std::string, int> locIndex;  // kTtydApLocations' name -> kTtydRandoLocations index
  std::map<int64_t, int> locById;       // id -> kTtydRandoLocations index
  std::vector<int> apOf = std::vector<int>(kLocs, -1);
  std::set<int64_t> stars;
  // the items a rule asks for that the game's own story counts on: the ways'
  // rules and the places' but troubles, shops, Dazzle, the Pit and cooking
  // (Gob's trouble asks for the L Emblem, Dazzle for Star Pieces)
  std::set<int64_t> storyNeeds;
  void needs(int n) {
    if (n < 0) return;
    const TtydRandoNode& x = kTtydRandoNodes[n];
    if (x.op == RANDO_HAS) {
      if (kTtydRandoItems[x.first].id) storyNeeds.insert(kTtydRandoItems[x.first].id);
    } else if (x.op == RANDO_ALL || x.op == RANDO_ANY) {
      for (int k = 0; k < x.n; k++) needs(kTtydRandoKids[x.first + k]);
    }
  }
  Tables() {
    for (int i = 0; i < kItemN; i++) {
      itemIndex[kTtydRandoItems[i].name] = i;
      if (kTtydRandoItems[i].id) logicItem[kTtydRandoItems[i].id] = i;
    }
    std::map<int64_t, int> ap;
    for (int i = 0; i < kApLocs; i++) ap[kTtydApLocations[i].id] = i;
    for (int i = 0; i < kLocs; i++) {
      locById[kTtydRandoLocations[i].id] = i;
      const auto it = ap.find(kTtydRandoLocations[i].id);
      if (it != ap.end()) {
        apOf[i] = it->second;
        locIndex[kTtydApLocations[it->second].name] = i;
      }
    }
    for (int64_t s : kTtydRandoStars) stars.insert(s);
    for (const TtydRandoEdge& e : kTtydRandoEdges) needs(e.rule);
    for (const TtydRandoLocation& l : kTtydRandoLocations) {
      if (!(l.tags & (RANDO_TAG_TROUBLE | RANDO_TAG_SHOP | RANDO_TAG_DAZZLE | RANDO_TAG_PIT_FLOOR | RANDO_TAG_COOKING)))
        needs(l.rule);
    }
  }
};
const Tables& T() {
  static const Tables t;
  return t;
}

std::string locName(int i) { return T().apOf[i] >= 0 ? kTtydApLocations[T().apOf[i]].name : "?"; }

// an apworld rule as text, for the detail output
std::string nodeText(int n) {
  if (n < 0) return "true";
  const TtydRandoNode& x = kTtydRandoNodes[n];
  switch (x.op) {
  case RANDO_HAS: return std::string(kTtydRandoItems[x.first].name) + (x.count > 1 ? " x" + std::to_string(x.count) : "");
  case RANDO_REACH_LOCATION: return "reach(" + locName(x.first) + ")";
  case RANDO_REACH_REGION: return std::string("region(") + kTtydRandoRegions[x.first] + ")";
  default: {
    std::string s = x.op == RANDO_ALL ? "all(" : x.count > 1 ? std::to_string(x.count) + " of(" : "any(";
    for (int k = 0; k < x.n; k++) s += (k ? ", " : "") + nodeText(kTtydRandoKids[x.first + k]);
    return s + ")";
  }
  }
}

// --- a state: what is in hand, what is reached ---
struct State {
  std::vector<int> have = std::vector<int>(kItemN, 0);
  std::map<int64_t, int> other; // items no apworld rule names (the Briefcase...), by id
  std::vector<char> region = std::vector<char>(RANDO_REGION_COUNT, 0);
  std::vector<char> reached = std::vector<char>(kLocs, 0);
  std::vector<char> left = std::vector<char>(kLocs, 0); // reached, its item left lying (the withholding)
  int goal = 7;
  unsigned tripDone = 0; // the game model's trips, by kGameTraps index

  void collect(int64_t item) {
    const auto it = T().logicItem.find(item);
    if (it != T().logicItem.end()) have[it->second]++;
    if (T().stars.count(item)) have[0]++;
    other[item]++;
  }
  int count(const std::string& name) const {
    const auto it = T().itemIndex.find(name);
    if (it != T().itemIndex.end()) return have[it->second];
    const auto o = other.find(itemByName(name.c_str()));
    return o == other.end() ? 0 : o->second;
  }
  bool node(int n) const {
    if (n < 0) return true;
    const TtydRandoNode& x = kTtydRandoNodes[n];
    switch (x.op) {
    case RANDO_HAS: return have[x.first] >= x.count;
    case RANDO_REACH_LOCATION: return reached[x.first] != 0;
    case RANDO_REACH_REGION: return region[x.first] != 0;
    default: {
      int ok = 0;
      for (int k = 0; k < x.n; k++) ok += node(kTtydRandoKids[x.first + k]) ? 1 : 0;
      return ok >= x.count;
    }
    }
  }
  bool rule(const PcRule& r) const {
    switch (r.op) {
    case PcRule::kTrue: return true;
    case PcRule::kHas: return count(r.name) >= r.count;
    case PcRule::kGoal: return have[0] >= goal;
    case PcRule::kReach: return reached[T().locIndex.at(r.name)] != 0;
    case PcRule::kRegion: return region[regionOf(r.name.c_str())] != 0;
    case PcRule::kAll:
      for (const PcRule& k : r.kids)
        if (!rule(k)) return false;
      return true;
    case PcRule::kAny:
      for (const PcRule& k : r.kids)
        if (rule(k)) return true;
      return false;
    }
    return false;
  }
};

// --- Paper Cut's world, from pc_world.h ---
struct PcModel {
  const PcWorld& w = pcWorld();
  std::vector<char> on = std::vector<char>(RANDO_REGION_COUNT, 0);
  struct Edge {
    int from, to;
    const PcRule* rule;
  };
  std::vector<Edge> edges;
  std::vector<const PcRule*> extra = std::vector<const PcRule*>(kLocs, nullptr);
  std::vector<int> goalPlaces;
  PcModel() {
    for (const char* r : w.regions) on[regionOf(r)] = 1;
    for (const PcEdge& e : w.edges) edges.push_back({regionOf(e.from), regionOf(e.to), &e.rule});
    for (const auto* list : {&w.places, &w.hubScenes}) {
      for (const PcPlaceRule& p : *list) extra[T().locIndex.at(p.location)] = &p.rule;
    }
    for (int64_t id : w.goal) goalPlaces.push_back(T().locById.at(id));
  }
};
const PcModel& PC() {
  static const PcModel m;
  return m;
}

// --- the game's one-way trips (the "game" model) ---
// Written from the decomp's map scripts, apart from pc_world.h's own list (the
// generator's), so the two can be compared. A trip: the region its way in
// leads to, the regions it keeps Mario in until it ends, how it ends (a place
// reached, or a region reached from inside), and whether its regions can be
// gone back to afterwards.
struct Trap {
  const char* name;
  const char* entry;                 // the region the one-way way in leads to
  std::vector<const char*> inside;   // the regions there is no other way out of
  const char* exitPlace;             // the place whose scene ends it (or nullptr)
  const char* exitRegion;            // or the region whose arrival ends it
  bool revisit;                      // open freely once it has ended
  const char* why;                   // the decomp's evidence
};
const std::vector<Trap>& kGameTraps() {
  static const std::vector<Trap> t = {
      {"Keelhaul Key (Flavio's ship)", "Keelhaul Key", {"Keelhaul Key", "Pirate's Grotto"},
       "Pirate's Grotto Cortez' Hoard: Sapphire Star", nullptr, true,
       "muj_00's ship home sails only at STORY_STG5_58_FAIR_WINDS (after Cortez and Crump); muj_02's pipe to "
       "tik_17 shows only once GSWF_TIK17_PIPE_MUJ_OPEN, whose tornado switch is in tik_17 (Rogueport's side)"},
      {"the Excess Express (first ride)", "Excess Express", {"Excess Express", "Riverside Station"}, nullptr,
       "Poshley Heights", true,
       "rsh_00/rsh_01's first-ride maps have no exit off the train; it stops at Riverside (hom_00) only after "
       "Zip Toad, and the first way back to Rogueport is from Poshley Heights (pik_00, rsh_01's second visit)"},
      {"the moon (the cannon)", "X-Naut Fortress", {"X-Naut Fortress"}, "X-Naut Fortress Boss Room: Crystal Star",
       nullptr, false,
       "moo_*/aji_* have no exit to Fahr Outpost; moo_04 sends Mario to tik_00 only after the fortress "
       "(STORY_STG7_26_THE_HIDEOUT_EXPLODES); bom_01's cannon is a one-time scene"},
  };
  return t;
}

// pc_world.h's own trips (the generator's), as Traps
const std::vector<Trap>& genTraps() {
  static const std::vector<Trap> t = [] {
    std::vector<Trap> v;
    for (const PcTrip& p : pcWorld().trips)
      v.push_back({p.entry, p.entry, p.inside, p.exitPlace, p.exitRegion, p.revisit, "pc_world.h"});
    return v;
  }();
  return t;
}

struct Seed {
  Options o;
  Result r;
  std::vector<int64_t> at = std::vector<int64_t>(kLocs, 0); // what each logic location gives
  std::vector<char> inLogic = std::vector<char>(kLocs, 0);
  std::vector<char> placed = std::vector<char>(kLocs, 0);
};

int goalOf(const Options& o) {
  const int all = int(pcWorld().goal.size());
  return o.goalStars > 0 && o.goalStars < all ? o.goalStars : all;
}

// The sweep. limit: per item id, the most copies collected (the withholding);
// fence: regions not to enter (the trip under test); trips: the "game"
// model, where a trip is taken only when it can be finished, and what it
// reaches is what the trip itself reaches.
struct Sweeper {
  const Seed& s;
  bool pc;
  explicit Sweeper(const Seed& seed) : s(seed), pc(seed.o.logic == kPaperCut) {}
  bool verbose = false;
  // the "ingame" model: the game's own check decides each trip (gameGuard)
  std::function<bool(const State&, int)> guard;
  mutable std::vector<std::string> stuck; // trips the guard let him take that can't be finished

  bool placeOpen(const State& st, int i) const {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    if (!st.region[l.region] || !st.node(l.rule)) return false;
    return !pc || PC().extra[i] == nullptr || st.rule(*PC().extra[i]);
  }

  struct Limit {
    const std::map<int64_t, int>* most = nullptr;
    std::map<int64_t, int> taken;
    bool take(int64_t item) {
      if (most) {
        const auto l = most->find(item);
        if (l != most->end() && taken[item] >= l->second) return false; // (left lying there)
      }
      taken[item]++;
      return true;
    }
  };

  static int trapOf(const std::vector<Trap>& traps, int region) {
    for (size_t k = 0; k < traps.size(); k++) {
      if (regionOf(traps[k].entry) == region) return int(k);
    }
    return -1;
  }

  // A trip taken from `from`: finished from what is in hand and what lies on
  // the way? If so, *after is the state once it is.
  bool tripEnds(const State& from, const Trap& t, State* after = nullptr, Limit* lim = nullptr) const {
    State st = from;
    Limit none;
    Limit& l = lim ? *lim : none;
    const Limit saved = l;
    std::vector<char> inside(RANDO_REGION_COUNT, 0);
    for (const char* r : t.inside) inside[regionOf(r)] = 1;
    const int exitRegion = t.exitRegion ? regionOf(t.exitRegion) : -1;
    const int exitPlace = t.exitPlace ? T().locIndex.at(t.exitPlace) : -1;
    st.region[regionOf(t.entry)] = 1;
    bool ends = false;
    for (bool changed = true; changed;) {
      changed = false;
      for (const PcModel::Edge& e : PC().edges) {
        if (!st.region[e.from] || !inside[e.from]) continue;
        if (e.to == exitRegion) {
          ends = ends || st.rule(*e.rule); // (by the trip's own way: reaching it otherwise doesn't count)
          continue;
        }
        if (!inside[e.to] || st.region[e.to] || !st.rule(*e.rule)) continue;
        st.region[e.to] = 1;
        changed = true;
      }
      for (int i = 0; i < kLocs; i++) {
        const int reg = kTtydRandoLocations[i].region;
        if (!s.inLogic[i] || st.reached[i] || !inside[reg] || !placeOpen(st, i)) continue;
        st.reached[i] = 1;
        changed = true;
        if (s.at[i] != 0 && l.take(s.at[i])) st.collect(s.at[i]);
        else if (s.at[i] != 0) st.left[i] = 1;
      }
    }
    ends = ends || (exitPlace >= 0 && st.reached[exitPlace]);
    if (!ends) {
      l = saved;
      if (verbose) {
        std::printf("  trip %s stops: inside, not reached:\n", t.name);
        for (int i = 0; i < kLocs; i++) {
          if (s.inLogic[i] && inside[kTtydRandoLocations[i].region] && !st.reached[i])
            std::printf("    %s (holds %s)%s rule %s; lacking%s\n", locName(i).c_str(), itemName(s.at[i]),
                        st.region[kTtydRandoLocations[i].region] ? "" : " [region not reached]",
                        nodeText(kTtydRandoLocations[i].rule).c_str(), lacking(st, kTtydRandoLocations[i].rule).c_str());
        }
      }
      return false;
    }
    if (after) {
      for (int r = 0; r < RANDO_REGION_COUNT; r++) {
        if (!(t.revisit && inside[r])) st.region[r] = from.region[r]; // (gone back to only if it can be)
      }
      if (exitRegion >= 0) st.region[exitRegion] = 1;
      *after = st;
    }
    return true;
  }

  // late: a trip with no way back is taken too (only once nothing else opens:
  // a careful player takes it last)
  bool regions(State& st, const std::vector<char>* fence, const std::vector<Trap>* trips, Limit& lim,
               bool late = false) const {
    st.region[RANDO_REGION_MENU] = 1;
    bool any = false;
    for (bool changed = true; changed; any = any || changed) {
      changed = false;
      if (pc) {
        for (const PcModel::Edge& e : PC().edges) {
          if (!st.region[e.from] || st.region[e.to] || (fence && (*fence)[e.to])) continue;
          if (!st.rule(*e.rule)) continue;
          const int k = trips ? trapOf(*trips, e.to) : -1;
          if (k >= 0) {
            if (st.tripDone & (1u << k)) continue; // (done; a trip gone back to has its regions set)
            if (!(*trips)[k].revisit && !late) continue;
            State after;
            if (guard) {
              // the game asks before the trip (portEvtOpenTripReady): taken when it lets him go
              if (!guard(st, k)) continue;
              if (!tripEnds(st, (*trips)[k], &after, &lim)) {
                stuck.push_back(std::string("the game let him take ") + (*trips)[k].name + ", which can't be finished");
                continue;
              }
            } else if (!tripEnds(st, (*trips)[k], &after, &lim)) {
              continue;
            }
            st = after;
            st.tripDone |= 1u << k;
            changed = true;
            continue;
          }
          st.region[e.to] = 1;
          changed = true;
        }
      } else {
        for (const TtydRandoEdge& e : kTtydRandoEdges) {
          if (st.region[e.from] && !st.region[e.to] && st.node(e.rule)) {
            st.region[e.to] = 1;
            changed = true;
          }
        }
      }
    }
    return any;
  }

  // collect everything reachable
  void run(State& st, const std::map<int64_t, int>* limit = nullptr, const std::vector<char>* fence = nullptr,
           const std::vector<Trap>* trips = nullptr) const {
    Limit lim;
    lim.most = limit;
    for (bool changed = true; changed;) {
      changed = false;
      const int before = std::count(st.reached.begin(), st.reached.end(), 1);
      regions(st, fence, trips, lim);
      changed = std::count(st.reached.begin(), st.reached.end(), 1) != before;
      for (int i = 0; i < kLocs; i++) {
        if (!s.inLogic[i] || st.reached[i] || !placeOpen(st, i)) continue;
        st.reached[i] = 1;
        changed = true;
        if (s.at[i] != 0 && lim.take(s.at[i])) st.collect(s.at[i]);
        else if (s.at[i] != 0) st.left[i] = 1;
      }
      if (!changed && trips) changed = regions(st, fence, trips, lim, true); // (a trip with no way back, last)
    }
  }
  // the items an apworld rule names that aren't in hand (as many as it asks)
  static std::string lacking(const State& st, int n) {
    if (n < 0) return "";
    const TtydRandoNode& x = kTtydRandoNodes[n];
    if (x.op == RANDO_HAS)
      return st.have[x.first] >= x.count ? "" : std::string(" ") + kTtydRandoItems[x.first].name + " x" + std::to_string(x.count);
    if (x.op != RANDO_ALL && x.op != RANDO_ANY) return st.node(n) ? "" : " " + nodeText(n);
    std::string s;
    if (st.node(n)) return s;
    for (int k = 0; k < x.n; k++) s += lacking(st, kTtydRandoKids[x.first + k]);
    return x.op == RANDO_ANY ? " any(" + s + " )" : s;
  }
};

struct Verdict {
  bool genGoal = false, genWays = false, genDoor = false, genQueen = false;
  bool gameQueen = false, gameGoal = false;
  std::vector<std::string> problems;  // the data's (what a location gives...)
  std::vector<std::string> softlocks; // "game": a trip that can't be finished
  std::vector<std::string> blocked;   // what the sweeps couldn't reach, for a failed seed
};

std::string itemsAt(const Seed& s, const State& st) {
  // the key items placed where the sweep didn't reach
  std::string out;
  for (int i = 0; i < kLocs; i++) {
    if (!s.inLogic[i] || st.reached[i] || s.at[i] == 0) continue;
    if (paperCutPoolOf(s.at[i]) != kKeyItems && !T().stars.count(s.at[i])) continue;
    out += std::string("    ") + itemName(s.at[i]) + " at " + locName(i) + "\n";
  }
  return out;
}

Seed makeSeed(const Options& o) {
  Seed s;
  s.o = o;
  s.r = generate(o);
  if (!s.r.ok) return s;
  for (int i = 0; i < kLocs; i++) {
    const TtydRandoLocation& l = kTtydRandoLocations[i];
    s.at[i] = l.vanilla;
    if (o.logic == kPaperCut) {
      s.inLogic[i] = PC().on[l.region] && !(l.tags & RANDO_TAG_TROUBLE);
    } else if (o.logic == kOpenWorld) {
      s.inLogic[i] = !(l.tags & RANDO_TAG_TROUBLE);
    } else {
      s.inLogic[i] = 1;
    }
  }
  return s;
}

// --- the game's own check (open_world_trips.cpp's portOpenTripCanEnd) ---
// What the game would see on a file in state `st`: the places found (reached,
// their item taken), the pouch and party those give. The pouch is the worst a
// player can leave it: every key item used up at its door (heldOf counts them
// from the places found), and, with `ate`, everything else eaten or sold.
// What Mario holds is then made by heldOf, the same code the game runs.
std::vector<int64_t> gameHeld(const Seed& s, const State& st, bool ate, std::vector<int>& found) {
  found.clear();
  std::map<int, int> pouch; // game item -> copies carried
  std::set<int> party;
  for (int i = 0; i < kLocs; i++) {
    if (!st.reached[i] || st.left[i] || T().apOf[i] < 0) continue;
    found.push_back(T().apOf[i]);
    const int64_t item = s.at[i];
    if (item == 0) continue;
    if (const int p = partnerOf(item)) party.insert(p);
    else if (!ate && paperCutPoolOf(item) != kKeyItems && gameItemOf(item) > 0) pouch[gameItemOf(item)]++;
  }
  FileView f;
  f.pouch = [pouch](int item) {
    const auto it = pouch.find(item);
    return it == pouch.end() ? 0 : it->second;
  };
  f.partner = [party](int p) { return party.count(p) != 0; };
  f.stars = st.have[0];
  f.found = found;
  return heldOf(s.o, s.r.placements, f);
}

bool gameGuard(const Seed& s, const State& st, int trip, bool ate) {
  std::vector<int> found;
  const std::vector<int64_t> held = gameHeld(s, st, ate, found);
  return pcTripCanEnd(s.o, s.r.placements, trip, held, found);
}

Verdict solve(Seed& s, bool detail) {
  Verdict v;
  const Result& r = s.r;
  // --- what every location gives ---
  std::set<int> seen;
  std::multiset<int64_t> before, after;
  for (const Placement& p : r.placements) {
    if (p.location < 0 || p.location >= kApLocs) {
      v.problems.push_back("a placement outside kTtydApLocations");
      continue;
    }
    if (!seen.insert(p.location).second) v.problems.push_back(std::string("two placements at ") + kTtydApLocations[p.location].name);
    const auto it = T().locById.find(kTtydApLocations[p.location].id);
    if (it == T().locById.end()) {
      v.problems.push_back(std::string("a placement outside the logic: ") + kTtydApLocations[p.location].name);
      continue;
    }
    const int i = it->second;
    const TtydApLocation& loc = kTtydApLocations[p.location];
    if (loc.count == 0) v.problems.push_back(std::string("a placement where the game gives nothing: ") + loc.name);
    if (p.shuffled) {
      before.insert(kTtydRandoLocations[i].vanilla);
      after.insert(p.item);
      s.placed[i] = 1;
    } else if (p.item != kTtydRandoLocations[i].vanilla) {
      v.problems.push_back(std::string("an unshuffled place changed: ") + loc.name);
    }
    s.at[i] = p.item;
    if (s.o.logic == kVanillaStory) {
      if (poolOf(p.item) == kKeyItems) v.problems.push_back(std::string("a key item moved: ") + loc.name);
      // nothing any rule asks for moves in the game's own story
      if (p.item != kTtydRandoLocations[i].vanilla && T().storyNeeds.count(p.item))
        v.problems.push_back(std::string("an item the logic names moved in the vanilla story: ") + itemName(p.item) +
                             " to " + loc.name);
      if (p.item != kTtydRandoLocations[i].vanilla && T().storyNeeds.count(kTtydRandoLocations[i].vanilla))
        v.problems.push_back(std::string("an item the logic names left its place in the vanilla story: ") + loc.name);
      for (int k = loc.first; k < loc.first + loc.count; k++) {
        if (kTtydApTargets[k].kind != TTYD_AP_SCRIPT || kTtydApTargets[k].area[0] == '\0' || kTtydApTargets[k].price)
          v.problems.push_back(std::string("a vanilla-story place not in a REL's script: ") + loc.name);
      }
    }
    if (s.o.logic == kPaperCut) {
      if (loc.flags & (TTYD_AP_SHOP | TTYD_AP_TATTLE)) v.problems.push_back(std::string("a shop or tattle took part: ") + loc.name);
      const int64_t id = loc.id;
      for (int64_t f : pcWorld().fixed)
        if (f == id && p.item != kTtydRandoLocations[i].vanilla) v.problems.push_back(std::string("a prologue place moved: ") + loc.name);
    }
  }
  if (s.o.logic == kPaperCut) {
    // the extra items (the Old Letter, the Train Ticket) come in for others
    for (const char* n : pcWorld().extraItems) {
      const auto it = after.find(itemByName(n));
      if (it == after.end()) v.problems.push_back(std::string(n) + " is not placed");
      else after.erase(it);
    }
    for (int64_t a : after) {
      const auto it = before.find(a);
      if (it == before.end()) v.problems.push_back(std::string("an item placed that wasn't taken: ") + itemName(a));
      else before.erase(it);
    }
    if (before.size() != pcWorld().extraItems.size()) v.problems.push_back("the items placed are not the ones taken");
  } else {
    // (the vanilla story keeps a slot at kTtydApNotEveryDisc's places on every
    // disc, as 0.4.1 did; a disc that can't write one leaves it unwritten: its
    // own item stays, and the item drawn there goes unplaced)
    size_t unwritten = 0;
    if (s.o.logic == kVanillaStory) {
      for (int64_t id : kTtydApNotEveryDisc) {
        const auto it = T().locById.find(id);
        if (it == T().locById.end() || s.placed[it->second]) continue;
        const int64_t own = kTtydRandoLocations[it->second].vanilla;
        if ((poolOf(own) & s.o.pools) == 0) continue;
        before.insert(own);
        unwritten++;
      }
    }
    bool same = before.size() == after.size() + unwritten;
    for (int64_t a : after) {
      const auto it = before.find(a);
      if (it == before.end()) same = false;
      else before.erase(it);
    }
    if (!same) v.problems.push_back("the items placed are not the ones taken");
  }

  Sweeper sw(s);
  if (s.o.logic == kVanillaStory) {
    v.genGoal = v.genWays = v.genDoor = v.genQueen = v.gameGoal = v.gameQueen = v.problems.empty();
    return v;
  }
  // --- "gen": the generator's logic ---
  State st;
  st.goal = goalOf(s.o);
  sw.run(st, nullptr, nullptr, &genTraps());
  if (s.o.logic == kOpenWorld) {
    v.genGoal = v.genWays = v.genDoor = v.genQueen = v.gameGoal = v.gameQueen = st.region[RANDO_REGION_GOAL] != 0;
    if (!v.genQueen) v.blocked.push_back("gen: the Shadow Queen isn't reached; key items not reached:\n" + itemsAt(s, st));
    return v;
  }
  int stars = 0;
  for (int g : PC().goalPlaces) stars += st.reached[g] != 0;
  v.genGoal = stars >= st.goal;
  v.genWays = true;
  for (const char* way : pcWorld().ways) v.genWays = v.genWays && st.region[regionOf(way)];
  v.genDoor = st.region[regionOf("Palace of Shadow")] != 0;
  v.genQueen = st.region[regionOf("Shadow Queen")] != 0;
  if (!(v.genGoal && v.genWays && v.genDoor && v.genQueen)) {
    std::string what = "gen:";
    if (!v.genGoal) what += " the goal's stars (" + std::to_string(stars) + " of " + std::to_string(st.goal) + ")";
    for (const char* way : pcWorld().ways)
      if (!st.region[regionOf(way)]) what += std::string(" [") + way + "]";
    if (!v.genQueen) what += " the Shadow Queen";
    v.blocked.push_back(what + " not reached; key items not reached:\n" + itemsAt(s, st));
  }

  // --- "game": a player who takes a trip only when it can be finished ---
  const auto& traps = kGameTraps();
  State g;
  g.goal = goalOf(s.o);
  sw.run(g, nullptr, nullptr, &traps);
  int gstars = 0;
  for (int i : PC().goalPlaces) gstars += g.reached[i] != 0;
  v.gameGoal = gstars >= g.goal;
  v.gameQueen = g.region[regionOf("Shadow Queen")] != 0;
  if (!v.gameGoal || !v.gameQueen) v.blocked.push_back("game: not reached; key items not reached:\n" + itemsAt(s, g));
  if (detail) {
    // each trip from where the smart player stopped
    sw.verbose = std::getenv("RANDO_SOLVE_DEBUG") != nullptr;
    for (const Trap& t : traps) {
      const bool done = (g.tripDone >> (&t - &traps[0])) & 1;
      std::printf("trip %s: %s\n", t.name, done ? "taken" : sw.tripEnds(g, t) ? "not taken, would end" : "not taken, can't end");
    }
    // and from everything the generator's logic reaches outside the trip
    for (const Trap& t : traps) {
      State full;
      full.goal = goalOf(s.o);
      std::vector<char> fence(RANDO_REGION_COUNT, 0);
      for (const char* r : t.inside) fence[regionOf(r)] = 1;
      sw.run(full, nullptr, &fence);
      std::printf("trip %s from all of the rest: ends %d\n", t.name, sw.tripEnds(full, t));
    }
    for (int r = 0; r < RANDO_REGION_COUNT; r++) std::printf("  region %s: %d\n", kTtydRandoRegions[r], g.region[r]);
    if (std::getenv("RANDO_SOLVE_DEBUG"))
      for (int i = 0; i < kLocs; i++)
        if (s.inLogic[i]) std::printf("  %s %s: %s\n", g.reached[i] ? "got " : "    ", locName(i).c_str(), itemName(s.at[i]));
  }

  // --- "game": softlocks. Withhold each key item (or its copies past the
  // k-th), take every trip whose way in opens, and see that it ends.
  std::map<int64_t, int> copies; // key items placed (in logic), how many
  std::map<int64_t, std::vector<int>> where;
  for (int i = 0; i < kLocs; i++) {
    if (!s.inLogic[i] || s.at[i] == 0) continue;
    if (paperCutPoolOf(s.at[i]) != kKeyItems) continue;
    copies[s.at[i]]++;
    where[s.at[i]].push_back(i);
  }
  std::set<std::string> said;
  for (const auto& [item, n] : copies) {
    for (int keep = 0; keep < n; keep++) {
      std::map<int64_t, int> limit = {{item, keep}};
      for (const Trap& t : traps) {
        std::vector<char> fence(RANDO_REGION_COUNT, 0);
        for (const char* r : t.inside) fence[regionOf(r)] = 1;
        State out;
        out.goal = goalOf(s.o);
        // (the other trips taken only when they end: one trip at a time)
        sw.run(out, &limit, &fence, &traps);
        // the trip's way in, open without the item?
        bool open = false;
        for (const PcModel::Edge& e : PC().edges) {
          if (e.to == regionOf(t.entry) && out.region[e.from] && out.rule(*e.rule)) open = true;
        }
        if (!open) continue;
        const bool ends = sw.tripEnds(out, t);
        // the game's guard (rando_gen.cpp's pcTripCanEnd, which the train attendant, Flavio and
        // the mayor ask) must agree with this model: let the player go exactly when it ends
        // (from what the game itself sees, heldOf's model: keys used up, everything else eaten)
        {
          const int k = int(&t - &traps[0]);
          if (gameGuard(s, out, k, true) != ends) {
            v.problems.push_back(std::string("the game's guard for ") + t.name + (ends ? " refuses" : " lets go") +
                                 " with " + std::to_string(keep) + " " + itemName(item) + ", this model " +
                                 (ends ? "finishes the trip" : "doesn't"));
          }
        }
        if (ends) continue;
        std::string at;
        for (int i : where[item]) at += (at.empty() ? "" : ", ") + locName(i);
        const std::string text = std::string("taking ") + t.name + " with " + std::to_string(keep) + " of " +
                                 std::to_string(n) + " " + itemName(item) + " (at " + at + ") can't be finished";
        if (said.insert(text).second) v.softlocks.push_back(text);
      }
    }
  }
  if (detail && std::getenv("RANDO_SOLVE_SPOILER")) {
    std::printf("%s", s.r.spoiler.c_str());
  }
  return v;
}

const char* logicName(int l) { return l == kPaperCut ? "pc" : l == kOpenWorld ? "ap" : "vanilla"; }

// (a place some disc can't write is left out: that disc keeps its own item
// there, and the digests of every disc must match)
uint64_t digest(const Result& r) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (const Placement& p : r.placements) {
    if (std::find(std::begin(kTtydApNotEveryDisc), std::end(kTtydApNotEveryDisc), kTtydApLocations[p.location].id) !=
        std::end(kTtydApNotEveryDisc))
      continue;
    for (uint64_t v : {uint64_t(p.location), uint64_t(p.item), uint64_t(p.shuffled)}) {
      for (int b = 0; b < 8; b++) {
        h ^= (v >> (b * 8)) & 0xFF;
        h *= 0x100000001B3ull;
      }
    }
  }
  return h;
}

struct Totals {
  std::atomic<long> seeds{0}, made{0}, refused{0}, genFail{0}, gameFail{0}, softlocked{0}, problems{0}, nondet{0};
  // refusals of open-world and Paper Cut seeds: the sweep leaves out the pool sets those must refuse, so each
  // of these is the generator failing (no placement found, a pc_world.h data error), never expected. The
  // vanilla jobs' refusals (key items only, masked to nothing) are.
  std::atomic<long> refusedUnexpected{0};
  std::atomic<long> attempts{0};
  std::mutex m;
  std::map<std::string, long> softlockKinds; // trip + item -> seeds
  std::vector<std::string> examples;
};

std::string describe(const Options& o) {
  return "seed " + o.seed + " logic " + logicName(o.logic) + " pools " + std::to_string(o.pools) + " goal " +
         std::to_string(o.goalStars);
}

void one(const Options& o, Totals& t) {
  t.seeds++;
  Seed s = makeSeed(o);
  if (!s.r.ok) {
    t.refused++;
    if (o.logic != kVanillaStory) t.refusedUnexpected++;
    std::lock_guard<std::mutex> l(t.m);
    if (t.examples.size() < 200) t.examples.push_back("REFUSED " + describe(o) + ": " + s.r.error);
    return;
  }
  t.made++;
  t.attempts += s.r.attempts;
  // the same seed again: the same placements (both sides of an online game make it)
  if (digest(generate(o)) != digest(s.r)) t.nondet++;
  const Verdict v = solve(s, false);
  std::lock_guard<std::mutex> l(t.m);
  if (!v.problems.empty()) {
    t.problems++;
    if (t.examples.size() < 200) t.examples.push_back("PROBLEM " + describe(o) + ": " + v.problems[0]);
  }
  if (!(v.genGoal && v.genWays && v.genDoor && v.genQueen)) {
    t.genFail++;
    if (t.examples.size() < 200) t.examples.push_back("UNWINNABLE(gen) " + describe(o) + "\n" + (v.blocked.empty() ? std::string("(its problem above)") : v.blocked[0]));
  }
  if (!(v.gameGoal && v.gameQueen)) {
    t.gameFail++;
    if (t.examples.size() < 200)
      t.examples.push_back("UNWINNABLE(game) " + describe(o) + "\n" + (v.blocked.empty() ? std::string("(its problem above)") : v.blocked.back()));
  }
  if (!v.softlocks.empty()) {
    t.softlocked++;
    std::set<std::string> kinds;
    for (const std::string& sl : v.softlocks) {
      // "taking <trip> with k of n <item> (at ...)": the trip and the item
      const size_t with = sl.find(" with "), at = sl.find(" (at ");
      const size_t of = sl.find(" of ", with);
      const size_t item = sl.find(' ', of + 4);
      kinds.insert(sl.substr(7, with - 7) + " without " + sl.substr(item + 1, at - item - 1));
    }
    for (const std::string& k : kinds) t.softlockKinds[k]++;
    if (t.examples.size() < 200) t.examples.push_back("SOFTLOCK " + describe(o) + ": " + v.softlocks[0]);
  }
}

} // namespace

int main(int argc, char** argv) {
  int perCombo = 20, threads = 4, digests = 0, ingame = 0, entrances = 0, pcLogic = -1;
  std::string only = "all", seed;
  int pools = -1, goal = 0;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--threads") threads = std::atoi(next().c_str());
    else if (a == "--logic") only = next();
    else if (a == "--seed") seed = next();
    else if (a == "--pools") pools = std::atoi(next().c_str());
    else if (a == "--goal") goal = std::atoi(next().c_str());
    else if (a == "--digests") digests = std::atoi(next().c_str());
    else if (a == "--ingame") ingame = std::atoi(next().c_str());
    else if (a == "--entrances") entrances = std::atoi(next().c_str());
    else if (a == "--pclogic") pcLogic = std::atoi(next().c_str());
    else if (!a.empty() && a.find_first_not_of("0123456789") == std::string::npos) perCombo = std::atoi(a.c_str());
    else {
      // (a mistyped flag once ran nothing and passed: its argument became the count)
      std::fprintf(stderr, "rando_solve: unknown argument %s (see the usage at the top of rando_solve.cpp)\n",
                   a.c_str());
      return 2;
    }
  }
  if (only != "all" && only != "vanilla" && only != "ap" && only != "pc") {
    std::fprintf(stderr, "rando_solve: --logic %s: not all, vanilla, ap or pc\n", only.c_str());
    return 2;
  }
  const unsigned kAll = kBadges | kItems | kStarPieces | kShineSprites;

  if (!seed.empty()) { // one seed, in detail
    Options o;
    o.seed = seed;
    o.logic = only == "ap" ? kOpenWorld : only == "vanilla" ? kVanillaStory : kPaperCut;
    o.pools = pools >= 0 ? unsigned(pools) : kAll;
    o.goalStars = goal;
    Seed s = makeSeed(o);
    if (!s.r.ok) {
      std::printf("refused: %s\n", s.r.error.c_str());
      return 1;
    }
    const Verdict v = solve(s, true);
    std::printf("%s: %d tries; gen goal %d ways %d door %d queen %d; game goal %d queen %d\n", describe(o).c_str(),
                s.r.attempts, v.genGoal, v.genWays, v.genDoor, v.genQueen, v.gameGoal, v.gameQueen);
    for (const std::string& p : v.problems) std::printf("PROBLEM %s\n", p.c_str());
    for (const std::string& b : v.blocked) std::printf("BLOCKED %s", b.c_str());
    for (const std::string& sl : v.softlocks) std::printf("SOFTLOCK %s\n", sl.c_str());
    return v.problems.empty() && v.softlocks.empty() && v.genQueen && v.gameQueen ? 0 : 1;
  }

  if (ingame > 0) {
    // Paper Cut's open world as the game plays it: a player who takes each one-way trip when the
    // game's own check (portOpenTripCanEnd: heldOf and pcTripCanEnd, from what the game sees)
    // lets him, with a pouch of keys used up and, the second time, everything else eaten too.
    // Every seed must reach the goal and the Shadow Queen, and no trip the game lets him take
    // may be one he can't finish.
    std::atomic<long> made{0}, refused{0}, failed{0}, problems{0};
    std::mutex m;
    std::vector<std::string> bad;
    std::atomic<int> next{0};
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    for (int k = 0; k < std::max(1, threads); k++) {
      pool.emplace_back([&]() {
        for (int i; (i = next++) < ingame;) {
          Options o;
          o.logic = kPaperCut;
          o.pools = pools >= 0 ? unsigned(pools) : kAll;
          o.goalStars = goal;
          o.entrances = unsigned(entrances);
          if (pcLogic >= 0) o.pcLogic = pcLogic; // (an older file's logic)
          o.seed = seedText(uint64_t(i) * 104729 + 17);
          Seed s = makeSeed(o);
          if (!s.r.ok) {
            refused++;
            std::lock_guard<std::mutex> l(m);
            bad.push_back("REFUSED " + describe(o) + ": " + s.r.error);
            continue;
          }
          made++;
          const Verdict v = solve(s, false); // (also places the seed's items in s.at)
          if (!v.problems.empty()) {
            problems++;
            std::lock_guard<std::mutex> l(m);
            bad.push_back("PROBLEM " + describe(o) + ": " + v.problems[0]);
          }
          for (const bool ate : {false, true}) {
            Sweeper sw(s);
            sw.guard = [&s, ate](const State& st, int trip) { return gameGuard(s, st, trip, ate); };
            State g;
            g.goal = goalOf(o);
            sw.run(g, nullptr, nullptr, &kGameTraps());
            int stars = 0;
            for (int p : PC().goalPlaces) stars += g.reached[p] != 0;
            const bool queen = g.region[regionOf("Shadow Queen")] != 0;
            if (stars >= g.goal && queen && sw.stuck.empty()) continue;
            failed++;
            std::string why = sw.stuck.empty() ? std::string() : " " + sw.stuck[0] + ";";
            for (size_t t = 0; t < kGameTraps().size(); t++) {
              if (!(g.tripDone >> t & 1)) why += std::string(" not taken: ") + kGameTraps()[t].name + ";";
            }
            std::lock_guard<std::mutex> l(m);
            bad.push_back("UNFINISHED(ingame" + std::string(ate ? ", all eaten" : "") + ") " + describe(o) + ": stars " +
                          std::to_string(stars) + "/" + std::to_string(g.goal) + ", queen " + std::to_string(queen) +
                          ";" + why + "\n" + itemsAt(s, g));
            break;
          }
        }
      });
    }
    for (std::thread& th : pool) th.join();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (size_t k = 0; k < bad.size() && k < 40; k++) std::printf("%s\n", bad[k].c_str());
    std::printf("rando_solve --ingame: %d seeds (pools %u, goal %d, entrances %d): %ld made, %ld refused; "
                "%ld not finishable by the game's own trip checks; %ld with placement problems; %.1f s\n",
                ingame, pools >= 0 ? unsigned(pools) : kAll, goal, entrances, long(made), long(refused), long(failed),
                long(problems), secs);
    return failed || problems || refused ? 1 : 0;
  }

  if (digests > 0) { // for comparing two machines (online: both sides generate)
    for (int i = 0; i < digests; i++) {
      Options o;
      o.seed = seedText(uint64_t(i) * 7919 + 1);
      o.logic = i % 3 == 0 ? kVanillaStory : i % 3 == 1 ? kOpenWorld : kPaperCut;
      o.pools = (unsigned(i) % 31 + 1) | (o.logic == kVanillaStory ? 0u : unsigned(kBadges));
      o.goalStars = o.logic == kPaperCut ? i % 8 : 0;
      const Result r = generate(o);
      std::printf("%s %s %u %d %d %016llX\n", o.seed.c_str(), logicName(o.logic), o.pools, o.goalStars, r.ok,
                  (unsigned long long)digest(r));
    }
    return 0;
  }

  // every combination: the logic, which pools are shuffled, the goal
  std::vector<Options> jobs;
  auto add = [&](int logic, unsigned p, int g, int n) {
    for (int i = 0; i < n; i++) {
      Options o;
      o.logic = logic;
      o.pools = p;
      o.goalStars = g;
      o.seed = seedText((uint64_t(logic) << 40) ^ (uint64_t(p) << 32) ^ (uint64_t(g) << 24) ^ uint64_t(i));
      jobs.push_back(o);
    }
  };
  for (unsigned p = 1; p < 32; p++) {
    if (only == "all" || only == "vanilla") add(kVanillaStory, p, 0, std::max(1, perCombo / 4));
    if ((p & (kBadges | kItems | kStarPieces)) == 0) continue; // (refused: checked below)
    if (only == "all" || only == "ap") add(kOpenWorld, p, 0, std::max(1, perCombo / 4));
    if (only == "all" || only == "pc") {
      for (int g = 0; g <= 7; g++) add(kPaperCut, p, g, perCombo);
    }
  }
  Totals t;
  const auto start = std::chrono::steady_clock::now();
  std::atomic<size_t> next{0};
  std::vector<std::thread> pool;
  for (int k = 0; k < std::max(1, threads); k++) {
    pool.emplace_back([&]() {
      for (size_t j; (j = next++) < jobs.size();) one(jobs[j], t);
    });
  }
  for (std::thread& th : pool) th.join();
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  for (const std::string& e : t.examples) std::printf("%s\n", e.c_str());
  std::printf("\nsoftlock kinds (seeds):\n");
  std::vector<std::pair<long, std::string>> kinds;
  for (const auto& [k, n] : t.softlockKinds) kinds.push_back({n, k});
  std::sort(kinds.rbegin(), kinds.rend());
  for (const auto& [n, k] : kinds) std::printf("  %6ld  %s\n", n, k.c_str());
  const long made = t.made;
  std::printf("\nrando_solve: %ld seeds (%ld made, %ld refused), %.1f tries each, %.1f s\n", long(t.seeds), made,
              long(t.refused), made ? double(t.attempts) / made : 0.0, secs);
  std::printf("  unwinnable by the generator's logic: %ld (%.2f%%)\n", long(t.genFail), made ? 100.0 * t.genFail / made : 0.0);
  std::printf("  unwinnable by the game's (smart player): %ld (%.2f%%)\n", long(t.gameFail), made ? 100.0 * t.gameFail / made : 0.0);
  std::printf("  a one-way trip that could be taken too early but for the game's guards: %ld (%.2f%%)\n",
              long(t.softlocked),
              made ? 100.0 * t.softlocked / made : 0.0);
  std::printf("  placement problems: %ld; not deterministic: %ld\n", long(t.problems), long(t.nondet));
  std::printf("  open-world and Paper Cut seeds refused: %ld\n", long(t.refusedUnexpected));
  if (made == 0) std::printf("rando_solve: no seed was made: a failure\n");
  // (a trip taken too early is what the game's guards refuse, portEvtOpenTripReady, by the logic's own
  // check: listed, not a failure)
  return t.genFail || t.gameFail || t.problems || t.nondet || t.refusedUnexpected || made == 0 ? 1 : 0;
}
