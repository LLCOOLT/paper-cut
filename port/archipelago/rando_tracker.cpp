// The randomizer's check tracker (port/include/port/rando_tracker.h): the
// places the seed shuffled, by area, from the generator's own tracking
// (rando_gen.cpp's track) over this file's open world or this run's seed.
#include "port/rando_tracker.h"
#include "port/open_world.h"
#include "open_world_file.h"
#include "rando_gen.h"
#include "ttyd_ap.h"
#include "ttyd_ap_data.h"
#include "ttyd_rando.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Place {
  std::string room, name;
  int state;
};

struct Area {
  PortTrackerArea info;
  std::string name;
  std::vector<Place> places;
};

std::vector<Area> sAreas;
bool sAvailable = false, sLogic = false;

// The seed in play: this file's open world, else this run's
bool seedInPlay(ttyd_rando::Options& o, std::vector<ttyd_rando::Placement>& placements) {
  if (portOpenOn()) {
    portOpenFileOptions(o);
    const ttyd_rando::Result r = ttyd_rando::generate(o);
    if (!r.ok) return false;
    placements = r.placements;
    return true;
  }
  return portRandoRunSeed(o, placements);
}

// (made again only when the seed changes: the generator takes a while; a
// file's seed that can't be made is kept too, or every refresh would try it
// again, up to the generator's every try)
struct Cache {
  ttyd_rando::Options o;
  std::vector<ttyd_rando::Placement> placements;
  bool open = false, ok = false;
} sCache;

bool cached(ttyd_rando::Options& o, std::vector<ttyd_rando::Placement>& placements) {
  if (portOpenOn()) {
    ttyd_rando::Options now;
    portOpenFileOptions(now);
    const ttyd_rando::Options& c = sCache.o;
    if (sCache.open && c.seed == now.seed && c.pools == now.pools && c.goalStars == now.goalStars &&
        c.pcLogic == now.pcLogic && c.logicLevel == now.logicLevel && c.tricks == now.tricks && c.extra == now.extra &&
        c.entrances == now.entrances) {
      o = sCache.o;
      placements = sCache.placements;
      return sCache.ok;
    }
  } else if (!sCache.open && sCache.ok) {
    o = sCache.o;
    placements = sCache.placements;
    return true;
  }
  sCache = Cache();
  sCache.open = portOpenOn() != 0;
  sCache.ok = seedInPlay(sCache.o, sCache.placements);
  o = sCache.o;
  placements = sCache.placements;
  return sCache.ok;
}

} // namespace

extern "C" {

int portTrackerRefresh(void) {
  sAreas.clear();
  sAvailable = false;
  sLogic = false;
  if (!portApInFile()) return 0;
  ttyd_rando::Options o;
  std::vector<ttyd_rando::Placement> placements;
  if (!cached(o, placements)) return 0;
  std::vector<int64_t> held;
  std::vector<int> found;
  portOpenFileState(placements, held, found);
  const std::vector<ttyd_rando::Tracked> t = ttyd_rando::track(o, placements, held, found);
  sLogic = o.logicLevel != ttyd_rando::kNoLogic && (o.logic == ttyd_rando::kPaperCut || o.logic == ttyd_rando::kOpenWorld);
  std::vector<int> order; // areas in the logic's region order
  for (const ttyd_rando::Tracked& k : t) {
    if (std::find(order.begin(), order.end(), k.region) == order.end()) order.push_back(k.region);
  }
  std::sort(order.begin(), order.end());
  for (int region : order) {
    Area a;
    a.name = ttyd_rando::regionName(region);
    a.info = {nullptr, 0, 0, 0};
    for (const ttyd_rando::Tracked& k : t) {
      if (k.region != region) continue;
      const std::string full = kTtydApLocations[k.location].name;
      const size_t colon = full.find(": ");
      Place p;
      p.room = colon == std::string::npos ? std::string() : full.substr(0, colon);
      p.name = colon == std::string::npos ? full : full.substr(colon + 2);
      p.state = k.found ? PORT_TRACKER_FOUND : k.reachable ? PORT_TRACKER_REACHABLE : PORT_TRACKER_LOCKED;
      a.info.total++;
      a.info.found += k.found;
      a.info.reachable += !k.found && k.reachable;
      a.places.push_back(p);
    }
    sAreas.push_back(std::move(a));
  }
  for (Area& a : sAreas) a.info.name = a.name.c_str();
  sAvailable = !sAreas.empty();
  return sAvailable ? 1 : 0;
}

int portTrackerAvailable(void) { return sAvailable ? 1 : 0; }
int portTrackerHasLogic(void) { return sLogic ? 1 : 0; }
int portTrackerAreaCount(void) { return int(sAreas.size()); }

const PortTrackerArea* portTrackerArea(int i) {
  return i >= 0 && size_t(i) < sAreas.size() ? &sAreas[size_t(i)].info : nullptr;
}

int portTrackerPlaceCount(int area) {
  return area >= 0 && size_t(area) < sAreas.size() ? int(sAreas[size_t(area)].places.size()) : 0;
}

static const Place* placeAt(int area, int i) {
  if (area < 0 || size_t(area) >= sAreas.size()) return nullptr;
  const std::vector<Place>& p = sAreas[size_t(area)].places;
  return i >= 0 && size_t(i) < p.size() ? &p[size_t(i)] : nullptr;
}

const char* portTrackerPlaceName(int area, int i) {
  const Place* p = placeAt(area, i);
  return p != nullptr ? p->name.c_str() : "";
}

const char* portTrackerPlaceRoom(int area, int i) {
  const Place* p = placeAt(area, i);
  return p != nullptr ? p->room.c_str() : "";
}

int portTrackerPlaceState(int area, int i) {
  const Place* p = placeAt(area, i);
  return p != nullptr ? p->state : PORT_TRACKER_LOCKED;
}

int portTrackerTotals(int* found, int* reachable) {
  int total = 0, f = 0, r = 0;
  for (const Area& a : sAreas) {
    total += a.info.total;
    f += a.info.found;
    r += a.info.reachable;
  }
  if (found != nullptr) *found = f;
  if (reachable != nullptr) *reachable = r;
  return total;
}

} // extern "C"
