// Paper Cut's own open world, the seed side (port/include/port/open_world.h):
// an open-world file's placements, made again from the seed its GSW block
// keeps (rando_gen.cpp, kPaperCut), written into each area's scripts as the
// area is linked; what a partner's own place holds; the partners' items'
// text. The game side is port/src/common/open_world_pc.c.
#include "port/open_world.h"
#include "rando_gen.h"
#include "ttyd_ap.h"
#include "ttyd_ap_data.h"

#include "port/mods.h"
#include "port/tr.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <map>
#include <string>

namespace {

struct Made {
  std::string seed;
  unsigned pools = 0;
  int goal = 0;
  bool ok = false, tried = false;
  std::map<int, int64_t> byLocation; // kTtydApLocations index -> Archipelago item
} sMade;

// The placements of the file in play (made once per seed)
const Made& made() {
  const uint8_t* gsw = portApGswBytes();
  char seed[PORT_OPEN_SEED_MAX] = {};
  std::memcpy(seed, gsw + PORT_OPEN_SEED, PORT_OPEN_SEED_MAX - 1);
  const unsigned pools = gsw[PORT_OPEN_POOLS];
  const int goal = gsw[PORT_OPEN_GOAL];
  if (sMade.seed == seed && sMade.pools == pools && sMade.goal == goal && sMade.tried) return sMade;
  sMade = Made();
  sMade.seed = seed;
  sMade.pools = pools;
  sMade.goal = goal;
  sMade.tried = true;
  ttyd_rando::Options o;
  o.seed = seed;
  o.pools = pools;
  o.goalStars = goal;
  o.logic = ttyd_rando::kPaperCut;
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  sMade.ok = r.ok;
  if (!r.ok) {
    SDL_Log("open world: seed %s: %s", seed, r.error.c_str());
    return sMade;
  }
  for (const ttyd_rando::Placement& p : r.placements) sMade.byLocation[p.location] = p.item;
  SDL_Log("open world: seed %s: %d places, %d moved", seed, r.shuffled, r.moved);
  return sMade;
}

// The game's item id an Archipelago item is in this file
int romOf(int64_t item) {
  if (const int partner = ttyd_rando::partnerOf(item)) return portOpenPartnerToken(partner);
  const char* name = ttyd_rando::itemName(item);
  if (std::strcmp(name, "Progressive Boots") == 0) return 0x07;  // the Super Boots, then the Ultra Boots
  if (std::strcmp(name, "Progressive Hammer") == 0) return 0x0A; // the Super Hammer, then the Ultra Hammer
  // The TTYD AP mod's own "10 Coins" is its item 0x53, an unused key item of
  // the game's that is Bobbery's here (portOpenPartnerToken): the game's coin
  if (std::strcmp(name, "10 Coins") == 0) return 0x79; // ITEM_COIN
  return ttyd_rando::gameItemOf(item); // (the game's Briefcase, not the TTYD AP mod's)
}

const char* const kPartnerNames[] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};

// The partner the game gives at a place ("Petal Meadows Bridge Room: Koops"), or 0
int partnerPlace(const TtydApLocation& l) {
  const std::string name = l.name;
  for (int p = 1; p <= 7; p++) {
    const std::string suffix = std::string(": ") + kPartnerNames[p];
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) return p;
  }
  return 0;
}

// A black chest's place (the curse the game has there: the Plane, Paper,
// Tube and Boat Curses are items 2 to 5). Its word in the apworld's data is
// the scene's first call's argument, not the curse; the scene asks instead
// (portEvtOpenChest).
bool chestPlace(const TtydApLocation& l) { return l.vanilla >= 2 && l.vanilla <= 5; }

// What each script word the open world wrote held before it first did
std::map<intptr_t*, intptr_t> sOriginal;

} // namespace

extern "C" {

void portOpenAreaLinked(const char* area) {
  if (area == nullptr) return;
  if (!portOpenOn()) {
    // A file that isn't an open-world one, after one in the same run: the
    // words the open world wrote hold what they held before (the areas'
    // scripts are the program's own data and stay written otherwise)
    for (const auto& [word, value] : sOriginal) *word = value;
    if (!sOriginal.empty()) SDL_Log("open world: %d script words as they were", int(sOriginal.size()));
    sOriginal.clear();
    return;
  }
  std::string why;
  portOpenSetLecture(portModsGameAddress(std::string(area) + "::all_party_lecture", why));
  const Made& m = made();
  if (!m.ok) return;
  int written = 0;
  for (const auto& [index, item] : m.byLocation) {
    const TtydApLocation& l = kTtydApLocations[index];
    const int rom = romOf(item);
    if (partnerPlace(l)) continue; // (a partner's own place is its scene's, not a word: portOpenPartnerReward)
    if (chestPlace(l)) continue;   // (a black chest's word isn't its item: portOpenChestReward)
    for (int t = l.first; t < l.first + l.count; t++) {
      const TtydApTarget& target = kTtydApTargets[t];
      if (std::strcmp(target.area, area) != 0 || target.kind != TTYD_AP_SCRIPT) continue;
      uint8_t* base = static_cast<uint8_t*>(portModsGameAddress(target.name, why));
      if (base == nullptr) {
        SDL_Log("open world: %s: %s", target.name, why.c_str());
        continue;
      }
      intptr_t* word = reinterpret_cast<intptr_t*>(base + target.offset);
      sOriginal.emplace(word, *word); // (the first time only: what it held before the open world)
      *word = rom;
      written++;
    }
  }
  if (written) SDL_Log("open world: %s: %d items written", area, written);
}

int portOpenPartnerReward(int partner) {
  if (!portOpenOn()) return 0;
  const Made& m = made();
  if (!m.ok) return 0;
  if (partner <= 0 || partner > 7) return 0;
  for (const auto& [index, item] : m.byLocation) {
    if (partnerPlace(kTtydApLocations[index]) != partner) continue;
    if (const int p = ttyd_rando::partnerOf(item)) return 0x1000 + p;
    return romOf(item);
  }
  return 0;
}

int portOpenChestReward(int curse) {
  if (!portOpenOn()) return 0;
  const Made& m = made();
  if (!m.ok) return 0;
  for (const auto& [index, item] : m.byLocation) {
    const TtydApLocation& l = kTtydApLocations[index];
    if (!chestPlace(l) || l.vanilla != curse) continue;
    const int rom = romOf(item);
    return rom == curse ? 0 : rom; // (its own curse: the scene as the game has it)
  }
  return 0;
}

const char* portOpenText(const char* english) { return portTr(english); }

const char* portOpenMsg(const char* tag) {
  if (tag == nullptr || std::strcmp(tag, "pc_open_partner_item") != 0) return nullptr;
  return portTrGame("A partner who joins Mario's party.");
}

} // extern "C"
