// rando_test: the built-in randomizer's seed generator (port/archipelago/
// rando_gen.cpp) on the host, without the game: the same seed gives the same
// placements, every item placed is one that was there, the vanilla story
// moves only what the story never asks for, and the open world's placements
// leave the Shadow Queen reachable (checked here again, by a sweep of its own
// over the same logic tables).
//
//   clang++ -std=c++20 -Iport/include tests/port/rando_test.cpp port/archipelago/rando_gen.cpp -o rando_test
//   ./rando_test [seeds]
#include "../../port/archipelago/rando_gen.h"
#include "../../port/archipelago/ttyd_ap_data.h"
#include "../../port/archipelago/ttyd_rando_logic.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace ttyd_rando;

namespace {

int sFailures = 0;

void check(bool ok, const std::string& what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what.c_str());
    sFailures++;
  }
}

uint64_t digest(const Result& r) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (const Placement& p : r.placements) {
    for (uint64_t v : {uint64_t(p.location), uint64_t(p.item)}) {
      for (int b = 0; b < 8; b++) {
        h ^= (v >> (b * 8)) & 0xFF;
        h *= 0x100000001B3ull;
      }
    }
  }
  return h;
}

const TtydRandoLocation* randoLocation(int apIndex) {
  for (const TtydRandoLocation& l : kTtydRandoLocations) {
    if (l.id == kTtydApLocations[apIndex].id) return &l;
  }
  return nullptr;
}

// A sweep of its own: the regions and locations reachable from nothing with
// the placements (unwritten locations keep the game's items; troubles count
// for nothing), as the apworld's logic has it.
bool goalReachable(const Result& r) {
  const int n = int(sizeof(kTtydRandoLocations) / sizeof(kTtydRandoLocations[0]));
  std::map<int64_t, int64_t> placed;
  for (const Placement& p : r.placements) placed[kTtydApLocations[p.location].id] = p.item;
  std::map<int64_t, int> logicIndex;
  for (int i = 0; i < int(sizeof(kTtydRandoItems) / sizeof(kTtydRandoItems[0])); i++) {
    if (kTtydRandoItems[i].id) logicIndex[kTtydRandoItems[i].id] = i;
  }
  std::vector<int> have(sizeof(kTtydRandoItems) / sizeof(kTtydRandoItems[0]), 0);
  std::vector<char> region(RANDO_REGION_COUNT, 0), reached(n, 0);
  std::function<bool(int)> eval = [&](int node) -> bool {
    if (node < 0) return true;
    const TtydRandoNode& x = kTtydRandoNodes[node];
    if (x.op == RANDO_HAS) return have[x.first] >= x.count;
    if (x.op == RANDO_REACH_LOCATION) return reached[x.first];
    if (x.op == RANDO_REACH_REGION) return region[x.first];
    int ok = 0;
    for (int k = 0; k < x.n; k++) ok += eval(kTtydRandoKids[x.first + k]);
    return ok >= x.count;
  };
  region[RANDO_REGION_MENU] = 1;
  for (bool changed = true; changed;) {
    changed = false;
    for (const TtydRandoEdge& e : kTtydRandoEdges) {
      if (region[e.from] && !region[e.to] && eval(e.rule)) region[e.to] = 1, changed = true;
    }
    for (int i = 0; i < n; i++) {
      const TtydRandoLocation& l = kTtydRandoLocations[i];
      if (reached[i] || (l.tags & RANDO_TAG_TROUBLE) || !region[l.region] || !eval(l.rule)) continue;
      reached[i] = 1;
      changed = true;
      const auto it = placed.find(l.id);
      const int64_t item = it != placed.end() ? it->second : l.vanilla;
      if (logicIndex.count(item)) have[logicIndex[item]]++;
      for (int64_t s : kTtydRandoStars) have[0] += s == item;
    }
  }
  return region[RANDO_REGION_GOAL] != 0;
}

void testOne(const Options& o, bool deep) {
  const std::string what = "seed " + o.seed + " (" + poolsText(o.pools) + ", " +
                           (o.logic == kOpenWorld ? "open world" : "vanilla story") + ")";
  const Result r = generate(o);
  check(r.ok, what + ": " + r.error);
  if (!r.ok) return;
  // the items placed are the ones that were there
  std::vector<int64_t> before, after;
  for (const Placement& p : r.placements) {
    const TtydRandoLocation* l = randoLocation(p.location);
    check(l != nullptr, what + ": a placement outside the logic's locations");
    if (l == nullptr) continue;
    if (p.shuffled) {
      before.push_back(l->vanilla);
      after.push_back(p.item);
      const unsigned pools = o.logic == kOpenWorld ? o.pools | kKeyItems : o.pools;
      check((poolOf(l->vanilla) & pools) != 0, what + ": " + kTtydApLocations[p.location].name + " took part");
    } else {
      check(p.item == l->vanilla, what + ": an unshuffled location changed");
    }
    if (o.logic == kVanillaStory) {
      check(poolOf(p.item) != kKeyItems, what + ": a key item moved in the vanilla story");
      check(kTtydApLocations[p.location].flags == 0, what + ": a shop or tattle in the vanilla story");
    }
  }
  std::sort(before.begin(), before.end());
  std::sort(after.begin(), after.end());
  check(before == after, what + ": the items placed are not the ones taken");
  if (o.logic == kOpenWorld && deep) {
    check(goalReachable(r), what + ": the Shadow Queen can't be reached");
  }
  if (deep) {
    // the same seed, the same placements
    check(digest(generate(o)) == digest(r), what + ": not deterministic");
  }
}

} // namespace

int main(int argc, char** argv) {
  const int seeds = argc > 1 ? std::atoi(argv[1]) : 40;
  const auto start = std::chrono::steady_clock::now();

  // the data: every logic location is a known one
  for (const TtydRandoLocation& l : kTtydRandoLocations) {
    bool found = false;
    for (const TtydApLocation& a : kTtydApLocations) found |= a.id == l.id;
    check(found, "location " + std::to_string(l.id) + " is not in ttyd_ap_data.h");
  }
  check(seedHash("abc") == 0xE71FA2190541574Bull, "FNV-1a");
  check(seedText(1) == seedText(1) && seedText(1) != seedText(2), "seed texts");

  Options all;
  all.pools = kBadges | kItems | kKeyItems | kStarPieces | kShineSprites;
  all.logic = kOpenWorld;
  for (int i = 0; i < seeds; i++) {
    all.seed = seedText(uint64_t(i) + 1);
    testOne(all, true);
  }
  // the fewest kinds the open world takes: key items and one more
  for (unsigned more : {unsigned(kBadges), unsigned(kItems), unsigned(kStarPieces)}) {
    Options keys;
    keys.pools = kKeyItems | more;
    keys.logic = kOpenWorld;
    for (int i = 0; i < seeds / 4 + 1; i++) {
      keys.seed = "keys" + std::to_string(i);
      testOne(keys, true);
    }
  }
  for (unsigned alone : {unsigned(kKeyItems), unsigned(kKeyItems | kShineSprites)}) {
    Options o;
    o.seed = "alone";
    o.pools = alone;
    o.logic = kOpenWorld;
    check(!generate(o).ok, "the open world with " + poolsText(alone) + " only is refused");
  }
  Options vanilla;
  vanilla.logic = kVanillaStory;
  vanilla.pools = kBadges | kItems | kKeyItems | kStarPieces | kShineSprites; // key items are dropped
  for (int i = 0; i < seeds; i++) {
    vanilla.seed = "vanilla" + std::to_string(i);
    testOne(vanilla, true);
  }
  for (unsigned pools = 1; pools < 32; pools++) {
    Options o;
    o.seed = "pools";
    o.pools = pools;
    o.logic = kOpenWorld;
    if ((pools & (kBadges | kItems | kStarPieces)) != 0) testOne(o, false);
    o.logic = kVanillaStory;
    if ((pools & ~unsigned(kKeyItems)) != 0) testOne(o, false);
  }

  // a fixed seed's placements, so a change to the generator or its data shows
  vanilla.seed = all.seed = "PaperCut";
  const Result v = generate(vanilla), w = generate(all);
  std::printf("PaperCut: vanilla story %d locations, %d moved, digest %016llX; open world %d locations, %d moved, "
              "%d tries, digest %016llX\n",
              v.shuffled, v.moved, (unsigned long long)digest(v), w.shuffled, w.moved, w.attempts,
              (unsigned long long)digest(w));
  if (getenv("RANDO_TEST_SPOILER")) std::fputs(w.spoiler.c_str(), stdout);
  // (these change with the generator or its data, on purpose: update them then,
  // and say so, since a seed shared before no longer gives the same game)
  check(digest(v) == 0x3D3879B549D1C531ull, "the vanilla story's PaperCut placements changed");
  check(digest(w) == 0xBF998C1C555E6325ull, "the open world's PaperCut placements changed");

  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("rando_test: %s (%d seeds, %.1f s)\n", sFailures ? "FAILED" : "ok", seeds, s);
  return sFailures ? 1 : 0;
}
