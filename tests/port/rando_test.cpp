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
#include "../../port/archipelago/pc_world.h"

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

// (a place some disc can't write is left out: that disc keeps its own item
// there, and every disc's digests of one seed match)
uint64_t digest(const Result& r) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (const Placement& p : r.placements) {
    if (std::find(std::begin(kTtydApNotEveryDisc), std::end(kTtydApNotEveryDisc), kTtydApLocations[p.location].id) !=
        std::end(kTtydApNotEveryDisc))
      continue;
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
                           (o.logic == kOpenWorld ? "open world" : o.logic == kPaperCut ? "Paper Cut open world" : "vanilla story") + ")";
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
      const unsigned pools = o.logic != kVanillaStory ? o.pools | kKeyItems : o.pools;
      const unsigned pool = o.logic == kPaperCut ? paperCutPoolOf(l->vanilla) : poolOf(l->vanilla);
      check((pool & pools) != 0, what + ": " + kTtydApLocations[p.location].name + " took part");
    } else {
      check(p.item == l->vanilla, what + ": an unshuffled location changed");
    }
    if (o.logic == kVanillaStory) {
      check(poolOf(p.item) != kKeyItems, what + ": a key item moved in the vanilla story");
      check(std::string(itemName(p.item)) != "Briefcase", what + ": the Briefcase (Zip Toad asks for it) moved");
      check(kTtydApLocations[p.location].flags == 0, what + ": a shop or tattle in the vanilla story");
    }
  }
  if (o.logic == kPaperCut) {
    // every chapter's way in is reached by the generator's final sweep
    check(r.chaptersReached == int(pcWorld().ways.size()),
          what + ": " + std::to_string(r.chaptersReached) + " chapters reached");
    // Paper Cut's open world places the key items its stage can't reach the
    // own places of (pc_world.h's extraItems), each instead of another item
    for (const char* name : pcWorld().extraItems) {
      const auto it = std::find(after.begin(), after.end(), itemByName(name));
      check(it != after.end(), what + ": " + name + " not placed");
      if (it != after.end()) after.erase(it);
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    check(before.size() == after.size() + pcWorld().extraItems.size() &&
              std::includes(before.begin(), before.end(), after.begin(), after.end()),
          what + ": the items placed are not the ones taken");
  } else {
    // (the vanilla story keeps a slot at kTtydApNotEveryDisc's places on every
    // disc; a disc that can't write one leaves it out, its own item stays, and
    // the item drawn there goes unplaced)
    size_t unwritten = 0;
    if (o.logic == kVanillaStory) {
      for (int64_t id : kTtydApNotEveryDisc) {
        const bool placed = std::any_of(r.placements.begin(), r.placements.end(),
                                        [&](const Placement& p) { return kTtydApLocations[p.location].id == id; });
        for (const TtydRandoLocation& l : kTtydRandoLocations) {
          if (l.id == id && !placed && (poolOf(l.vanilla) & o.pools) != 0) {
            before.push_back(l.vanilla);
            unwritten++;
          }
        }
      }
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    check(before.size() == after.size() + unwritten &&
              std::includes(before.begin(), before.end(), after.begin(), after.end()),
          what + ": the items placed are not the ones taken");
  }
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

  // Paper Cut's own open world: the same checks, its Crystal Stars reached by
  // the generator's own sweep (r.ok), the prologue's places untouched, both
  // stage 1 partners placed, in different places from seed to seed
  {
    Options pc;
    pc.logic = kPaperCut;
    pc.pools = kBadges | kItems | kStarPieces | kShineSprites;
    std::map<std::string, int> koopsAt;
    for (int i = 0; i < seeds; i++) {
      pc.seed = "pc" + std::to_string(i);
      testOne(pc, true);
      const Result r = generate(pc);
      int partners = 0;
      for (const Placement& p : r.placements) {
        const int64_t id = kTtydApLocations[p.location].id;
        check(id != 78780002 && id != 78780136 && id != 78780153, pc.seed + ": a prologue place moved");
        check(id != 78780004, pc.seed + ": the Old Letter's own place (after Rogueport's story stops) took part");
        check(id != 78780070, pc.seed + ": the Train Ticket's own place (after Rogueport's story stops) took part");
        check(id != 78780492, pc.seed + ": the grotto's Wedding Ring (never shown here) took part");
        if (partnerOf(p.item) != 0) partners++;
        if (partnerOf(p.item) == 2) koopsAt[kTtydApLocations[p.location].name]++;
      }
      // stage 2: Koops, Flurrie, Yoshi, Vivian, Bobbery (Goombella stays;
      // Ms. Mowz's place is a trouble, never shuffled)
      check(partners == 5, pc.seed + ": the five partners placed (" + std::to_string(partners) + ")");
    }
    check(koopsAt.size() > 3, "Koops goes to different places");
    // the goal: fewer Crystal Stars, still a seed (and another one), every
    // chapter still reachable past it
    for (int goal = 1; goal <= 7; goal++) {
      Options g = pc;
      g.goalStars = goal;
      g.seed = "goal" + std::to_string(goal);
      testOne(g, true);
      check(generate(g).spoiler.find("Goal: " + std::to_string(goal) + " of the 7") != std::string::npos,
            g.seed + ": the spoiler names the goal");
    }
    pc.seed = "PaperCut";
    const Result f = generate(pc);
    std::printf("PaperCut: Paper Cut open world %d locations, %d moved, %d tries, digest %016llX\n", f.shuffled, f.moved,
                f.attempts, (unsigned long long)digest(f));
    if (getenv("RANDO_TEST_PC_SPOILER")) std::fputs(f.spoiler.c_str(), stdout);
    // the logic levels: every trick on (each seed still reaches its goal by
    // the generator's own sweep, which counts them), and no logic at all
    // (every item placed, nothing checked)
    for (int i = 0; i < seeds / 4 + 1; i++) {
      Options t = pc;
      t.seed = "tricks" + std::to_string(i);
      t.logicLevel = kTricks;
      for (const TrickInfo& k : tricks()) t.tricks |= k.bit;
      testOne(t, true);
      check(generate(t).spoiler.find("Logic level: tricks (Yoshi Skip") != std::string::npos,
            t.seed + ": the spoiler names the tricks");
      Options n = pc;
      n.seed = "nologic" + std::to_string(i);
      n.logicLevel = kNoLogic;
      const Result nr = generate(n);
      check(nr.ok && nr.attempts == 1, n.seed + ": no logic makes a seed at once");
    }
    {
      Options a = pc, b = pc;
      a.seed = b.seed = "levels";
      b.logicLevel = kTricks;
      b.tricks = 1;
      check(digest(generate(a)) != digest(generate(b)), "a trick changes the seed's placements");
    }
    // the tracker: at the start (nothing held, nothing found) some places can
    // be reached and some can't; with every key item in hand, more
    {
      const Result r = generate(pc);
      const std::vector<Tracked> start = track(pc, r.placements, {}, {});
      int reach0 = 0, reachAll = 0, shuffled = 0;
      for (const Placement& p : r.placements) shuffled += p.shuffled;
      for (const Tracked& t : start) reach0 += t.reachable;
      std::vector<int64_t> all;
      for (const Placement& p : r.placements) {
        if (paperCutPoolOf(p.item) == kKeyItems) all.push_back(p.item);
      }
      for (const Tracked& t : track(pc, r.placements, all, {})) reachAll += t.reachable;
      check(int(start.size()) == shuffled, "the tracker lists every shuffled place");
      check(reach0 > 0 && reach0 < int(start.size()), "the tracker: some places reachable at the start, not all");
      check(reachAll > reach0, "the tracker: more reachable with the key items in hand");
    }
    // the chapter gates: a permutation of the four, other than the game's for
    // some seeds, every seed still reaching its goal and every chapter's way
    // in through them (the generator's own sweep, by the gates' ways)
    {
      int moved = 0;
      for (int i = 0; i < seeds / 4 + 1; i++) {
        Options g = pc;
        g.seed = "gates" + std::to_string(i);
        g.entrances = kEntranceGates;
        int seen = 0;
        for (int k = 0; k < 4; k++) seen |= 1 << gateTarget(g, k);
        check(seen == 15, g.seed + ": the gates are a permutation");
        moved += gateTarget(g, 0) != 0 || gateTarget(g, 1) != 1;
        testOne(g, true);
      }
      check(moved > 0, "the gates move for some seeds");
    }
    // the rest of the entrance shuffle (pc_doors.h): the doors' targets
    // coupled both ways, the room doors within their areas, each option moving
    // something for some seeds, every seed reaching its goal and every
    // chapter's way in; with it off, the doors as the game has them
    {
      const std::vector<DoorHalf>& halves = doorHalves();
      const int roomHalves = int(halves.size()) - int(2 * 5); // (pc_doors.h: five ways)
      {
        Options off = pc;
        off.seed = "doors-off";
        const std::vector<int> t = doorTargets(off);
        bool game = true;
        for (size_t h = 0; h < t.size(); h++) game = game && t[h] == int(h ^ 1);
        check(game, "entrances off: the doors as the game has them");
        check(rideTarget(off, 0) == 0 && rideTarget(off, 1) == 1 && rideTarget(off, 2) == 2,
              "entrances off: the rides as the game has them");
      }
      int roomsMoved = 0, waysMoved = 0, ridesMoved = 0;
      for (int i = 0; i < seeds / 4 + 1; i++) {
        for (unsigned e : {unsigned(kEntranceRooms), unsigned(kEntranceRooms | kEntranceMix),
                           unsigned(kEntranceTransports),
                           unsigned(kEntranceGates | kEntranceRooms | kEntranceMix | kEntranceTransports)}) {
          Options g = pc;
          g.seed = "doors" + std::to_string(i);
          g.entrances = e;
          const std::vector<int> t = doorTargets(g);
          bool coupled = true, areas = true;
          for (size_t h = 0; h < t.size(); h++) {
            coupled = coupled && t[t[h]] == int(h) && t[h] != int(h);
            if (int(h) < roomHalves) areas = areas && t[h] < roomHalves;
            else areas = areas && t[h] >= roomHalves && (t[h] - roomHalves) % 2 != (int(h) - roomHalves) % 2;
          }
          check(coupled, g.seed + ": the doors lead both ways");
          check(areas, g.seed + ": room doors stay room doors, an exit leads to an entrance");
          for (int h = 0; h < roomHalves; h++) roomsMoved += t[h] != (h ^ 1);
          for (size_t h = roomHalves; h < t.size(); h++) waysMoved += t[h] != int(h ^ 1);
          int seen = 0;
          for (int k = 0; k < 3; k++) seen |= 1 << rideTarget(g, k);
          check(seen == 7, g.seed + ": the rides are a permutation");
          ridesMoved += rideTarget(g, 0) != 0;
          testOne(g, true);
        }
      }
      check(roomsMoved > 0, "room doors move for some seeds");
      check(waysMoved > 0, "the ways between areas move for some seeds");
      check(ridesMoved > 0, "the rides move for some seeds");
    }
    pc.pools = kStarPieces;
    pc.seed = "pc-stars";
    testOne(pc, true);
    pc.pools = kShineSprites;
    check(!generate(pc).ok, "Paper Cut's open world with Shine Sprites only is refused");
  }

  // more pools: troubles' rewards and the shop stock take part when chosen,
  // and nothing else changes kind (the shelves keep items, no badges)
  for (int logic : {int(kVanillaStory), int(kPaperCut)}) {
    Options base;
    base.logic = logic;
    base.seed = "more";
    Options more = base;
    more.extra = kExtraShops | kExtraTroubles;
    const Result a = generate(base), b = generate(more);
    check(a.ok && b.ok, "more pools: seeds made");
    check(b.shuffled > a.shuffled, "more pools: more places take part");
    std::map<int, int> seen;
    for (const Placement& p : b.placements) {
      check(++seen[p.location] == 1, "more pools: a place given twice");
      if (kTtydApLocations[p.location].flags & TTYD_AP_SHOP) {
        check(poolOf(p.item) == kItems, "more pools: a shelf holds an item");
      }
    }
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
  // and say so, since a seed shared before no longer gives the same game. The
  // game's own story and Paper Cut's open world are the same on every disc,
  // kTtydApNotEveryDisc's places left out of the digest (the vanilla story
  // still draws for them everywhere, as 0.4.1 did); the TTYD AP mod's open
  // world is the US disc's, and JP's leaves out one place, ttyd_ap_data.h)
  check(digest(v) == 0xE901D976972737AAull, "the vanilla story's PaperCut placements changed");
  {
    // The vanilla story's seeds as 0.4.1 placed them (a run in progress keeps
    // its items after the update, and a shared seed gives one game): digests
    // over the places' ids and items, made by 0.4.1's own rando_gen.cpp on US.
    // Every place on the discs that write them all; without
    // kTtydApNotEveryDisc's on every disc (JP keeps its own item there).
    struct Pin041 {
      const char* seed;
      unsigned pools;
      uint64_t all, everyDisc;
    };
    static const Pin041 kPins041[] = {
        {"PaperCut", 27u, 0x9377E8DA9A332FA7ull, 0x99E57EE24577E06Full},
        {"abc", 27u, 0xF593375E9A4CD1ABull, 0xA18838E314EAE30Full},
        {"12345", 27u, 0x20BC2B8FD2691FC3ull, 0x402A08AE336C0647ull},
        {"Luigi", 27u, 0x6ECDD2D4DE8CE76Bull, 0x589ABA11B34F8F26ull},
        {"PaperCut", 1u, 0x40F43276D018B8FAull, 0xEFB5D698FAEC102Eull},
        {"abc", 1u, 0xDD22759D3947B9AAull, 0x9C7D6F0D286F6D8Dull},
        {"12345", 1u, 0xE0E4688882C0D40Aull, 0xEEE0B29A16E868FCull},
        {"Luigi", 1u, 0x86CA4EABFDCD6896ull, 0x5C654D9251E2C1A0ull},
        {"PaperCut", 10u, 0x51A9DE0EE1FA2B4Eull, 0x51A9DE0EE1FA2B4Eull},
        {"12345", 10u, 0x2ACB81D700D896C6ull, 0x2ACB81D700D896C6ull},
    };
    const auto idDigest = [](const Result& r, bool everyDisc) {
      uint64_t h = 0xCBF29CE484222325ull;
      for (const Placement& p : r.placements) {
        const int64_t id = kTtydApLocations[p.location].id;
        if (everyDisc && std::find(std::begin(kTtydApNotEveryDisc), std::end(kTtydApNotEveryDisc), id) !=
                             std::end(kTtydApNotEveryDisc))
          continue;
        for (uint64_t x : {uint64_t(id), uint64_t(p.item)}) {
          for (int b = 0; b < 8; b++) {
            h ^= (x >> (b * 8)) & 0xFF;
            h *= 0x100000001B3ull;
          }
        }
      }
      return h;
    };
    for (const Pin041& pin : kPins041) {
      Options o;
      o.seed = pin.seed;
      o.pools = pin.pools;
      const Result r = generate(o);
      const std::string what = std::string("the vanilla story's seed \"") + pin.seed + "\" (pools " +
                               std::to_string(pin.pools) + ") no longer places as 0.4.1 did";
      check(r.ok && idDigest(r, true) == pin.everyDisc, what);
#if !defined(VERSION_G8MJ01)
      check(r.ok && idDigest(r, false) == pin.all, what + " (with every place)");
#endif
    }
  }
  {
    Options pcPinned;
    pcPinned.logic = kPaperCut;
    pcPinned.seed = "PaperCut";
    check(digest(generate(pcPinned)) == 0x573DAED1494A0547ull, "Paper Cut's open world's PaperCut placements changed");
  }
#if defined(VERSION_G8MJ01)
  check(digest(w) == 0x3F8D789B78975656ull, "the open world's PaperCut placements changed");
#else
  check(digest(w) == 0x23F0CB2040C2B904ull, "the open world's PaperCut placements changed");
#endif

  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf("rando_test: %s (%d seeds, %.1f s)\n", sFailures ? "FAILED" : "ok", seeds, s);
  return sFailures ? 1 : 0;
}
