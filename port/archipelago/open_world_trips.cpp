// Paper Cut's open world: the one-way trips' check (port/include/port/open_world.h,
// portOpenTripCanEnd). Flavio's ship, the train's first ride and the cannon to
// the moon have no way back until their chapter's scene ends; the game asks
// here before Mario takes one (open_world_pc.c's portEvtOpenTripReady): can it
// be finished from what he holds and what lies on the way, on this file's
// seed? Only the generator (rando_gen.cpp) and the game's state through the C
// side, so it builds on every backend (headless too: the story tests). The
// file's options and state (open_world_file.h) live here for the same reason.
#include "open_world_file.h"
#include "port/open_world.h"
#include "rando_gen.h"
#include "ttyd_ap.h"
#include "ttyd_ap_data.h"
#include "ttyd_rando_logic.h"

#include <string>
#include <vector>

namespace {

// The file's placements, made once per stamp
struct Trips {
  ttyd_rando::Options o;
  bool made = false, ok = false;
  std::vector<ttyd_rando::Placement> placements;
};

bool same(const ttyd_rando::Options& a, const ttyd_rando::Options& b) {
  return a.seed == b.seed && a.pools == b.pools && a.goalStars == b.goalStars && a.pcLogic == b.pcLogic &&
         a.logicLevel == b.logicLevel && a.tricks == b.tricks && a.extra == b.extra && a.entrances == b.entrances;
}

const Trips& trips() {
  static Trips t;
  ttyd_rando::Options o;
  portOpenFileOptions(o);
  if (t.made && same(t.o, o)) return t;
  t = Trips();
  t.o = o;
  t.made = true;
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  t.ok = r.ok;
  t.placements = r.placements;
  return t;
}

} // namespace

bool portOpenFileOptions(ttyd_rando::Options& o) {
  char seed[PORT_OPEN_SEED_MAX] = {};
  unsigned pools = 0;
  int goal = 0, logic = 0; // (logic 0: a file from before the one-way trips keeps its placements)
  portOpenStampRead(seed, sizeof(seed), &pools, &goal, &logic);
  PortOpenRules rules;
  portOpenRulesRead(&rules);
  o.seed = seed;
  o.pools = pools;
  o.goalStars = goal;
  o.pcLogic = logic;
  o.logic = ttyd_rando::kPaperCut;
  o.logicLevel = int(rules.level);
  o.tricks = rules.tricks;
  o.extra = rules.extra;
  o.entrances = rules.entrances;
  return portOpenOn() != 0;
}

void portOpenFileState(const std::vector<ttyd_rando::Placement>& placements, std::vector<int64_t>& held,
                       std::vector<int>& found) {
  found.clear();
  const int n = int(sizeof(kTtydApLocations) / sizeof(kTtydApLocations[0]));
  for (int i = 0; i < n; i++) {
    const TtydApLocation& l = kTtydApLocations[i];
    if (l.gswOffset != 0 && portOpenPlaceFound(l.gswType, l.gswOffset, l.gswValue)) found.push_back(i);
  }
  ttyd_rando::FileView f;
  f.pouch = [](int item) { return portOpenHeldCount(item); };
  f.partner = [](int partner) { return portOpenPartnerIn(partner) != 0; };
  f.stars = portOpenStarsIn();
  f.found = found;
  ttyd_rando::Options o;
  portOpenFileOptions(o);
  held = ttyd_rando::heldOf(o, placements, f);
}

extern "C" int portOpenTripCanEnd(int trip) {
  if (!portOpenOn()) return 1;
  const Trips& t = trips();
  if (!t.ok) return 1;
  std::vector<int64_t> held;
  std::vector<int> found;
  portOpenFileState(t.placements, held, found);
  return ttyd_rando::pcTripCanEnd(t.o, t.placements, trip, held, found) ? 1 : 0;
}
