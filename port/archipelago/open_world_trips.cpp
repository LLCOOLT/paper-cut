// Paper Cut's open world: the one-way trips' check (port/include/port/open_world.h,
// portOpenTripCanEnd). Flavio's ship, the train's first ride and the cannon to
// the moon have no way back until their chapter's scene ends; the game asks
// here before Mario takes one (open_world_pc.c's portEvtOpenTripReady): can it
// be finished from what he holds and what lies on the way, on this file's
// seed? Only the generator (rando_gen.cpp) and the game's state through the C
// side, so it builds on every backend (headless too: the story tests).
#include "port/open_world.h"
#include "rando_gen.h"
#include "ttyd_ap.h"
#include "ttyd_ap_data.h"
#include "ttyd_rando_logic.h"

#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

// The file's placements, made once per stamp
struct Trips {
  std::string seed;
  unsigned pools = 0;
  int goal = -1, logic = -1;
  bool ok = false;
  std::vector<ttyd_rando::Placement> placements;
};

const Trips& trips() {
  static Trips t;
  char seed[PORT_OPEN_SEED_MAX] = {};
  unsigned pools = 0;
  int goal = 0, logic = 0;
  portOpenStampRead(seed, sizeof(seed), &pools, &goal, &logic);
  if (t.seed == seed && t.pools == pools && t.goal == goal && t.logic == logic) return t;
  t = Trips();
  t.seed = seed;
  t.pools = pools;
  t.goal = goal;
  t.logic = logic;
  ttyd_rando::Options o;
  o.seed = seed;
  o.pools = pools;
  o.goalStars = goal;
  o.pcLogic = logic;
  o.logic = ttyd_rando::kPaperCut;
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  t.ok = r.ok;
  t.placements = r.placements;
  return t;
}

// the game's item id an item is (as open_world.cpp's romOf, for the key items)
int romOf(int64_t item) {
  const char* name = ttyd_rando::itemName(item);
  if (std::strcmp(name, "Progressive Boots") == 0) return 0x07;
  if (std::strcmp(name, "Progressive Hammer") == 0) return 0x0A;
  return ttyd_rando::gameItemOf(item);
}

} // namespace

extern "C" int portOpenTripCanEnd(int trip) {
  if (!portOpenOn()) return 1;
  const Trips& t = trips();
  if (!t.ok) return 1;
  // what Mario holds, as the logic names it (one id per copy)
  std::vector<int64_t> held;
  std::set<int64_t> seen;
  for (const TtydRandoItem& i : kTtydRandoItemNames) {
    if (i.id == 0 || !seen.insert(i.id).second) continue;
    if (const int p = ttyd_rando::partnerOf(i.id)) {
      if (portOpenPartnerIn(p)) held.push_back(i.id);
      continue;
    }
    if (ttyd_rando::paperCutPoolOf(i.id) != ttyd_rando::kKeyItems) continue; // (the stars below; no rule asks for the rest)
    int count;
    if (std::strcmp(i.name, "Progressive Boots") == 0) {
      count = (portOpenHeldCount(0x07) > 0) + (portOpenHeldCount(0x08) > 0); // the Super, the Ultra Boots
    } else if (std::strcmp(i.name, "Progressive Hammer") == 0) {
      count = (portOpenHeldCount(0x0A) > 0) + (portOpenHeldCount(0x0B) > 0); // the Super, the Ultra Hammer
    } else {
      count = romOf(i.id) > 0 ? portOpenHeldCount(romOf(i.id)) : 0;
    }
    for (int k = 0; k < count; k++) held.push_back(i.id);
  }
  const int stars = portOpenStarsIn();
  for (int k = 0; k < stars && k < int(sizeof(kTtydRandoStars) / sizeof(kTtydRandoStars[0])); k++) {
    held.push_back(kTtydRandoStars[k]);
  }
  // the places found (their flags)
  std::vector<int> found;
  const int n = int(sizeof(kTtydApLocations) / sizeof(kTtydApLocations[0]));
  for (int i = 0; i < n; i++) {
    const TtydApLocation& l = kTtydApLocations[i];
    if (l.gswOffset != 0 && portOpenPlaceFound(l.gswType, l.gswOffset, l.gswValue)) found.push_back(i);
  }
  ttyd_rando::Options o;
  o.seed = t.seed;
  o.pools = t.pools;
  o.goalStars = t.goal;
  o.pcLogic = t.logic;
  o.logic = ttyd_rando::kPaperCut;
  return ttyd_rando::pcTripCanEnd(o, t.placements, trip, held, found) ? 1 : 0;
}
