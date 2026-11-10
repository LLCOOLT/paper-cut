// Paper Cut's own open world, the seed side (port/include/port/open_world.h):
// an open-world file's placements, made again from the seed its GSW block
// keeps (rando_gen.cpp, kPaperCut), written into each area's scripts as the
// area is linked; what a partner's own place holds; the partners' items'
// text. The game side is port/src/common/open_world_pc.c. Every backend:
// headless test runs play a seed with its real placements.
#include "port/open_world.h"
#include "open_world_file.h"
#include "rando_gen.h"
#include "ap_log.h"
#include "ttyd_ap_data.h"

#include "port/mods.h"
#include "port/tr.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// A speech bubble's text laid out as the game's (its font never wraps): lines
// of at most 30 columns (a Japanese letter is two, a word of its own). The
// text's own <p> starts a page after a key wait, and its <k> at the end is
// kept; no other page is made, so every language's window takes the same
// presses (online). (As battlex_aurora.c's gameLayout, without its pages.)
const char* speech(const char* in) {
  static std::string out[2];
  static int which;
  std::string& o = out[which ^= 1];
  o.clear();
  constexpr int kCols = 30;
  int line = 0;
  bool wait = false, prevCjk = false;
  while (*in != '\0') {
    if (*in == ' ' || *in == '\n') {
      in++;
      continue;
    }
    if ((unsigned char)in[0] == 0xE3 && (unsigned char)in[1] == 0x80 && (unsigned char)in[2] == 0x80) {
      in += 3; // (an ideographic space: a word's end)
      continue;
    }
    if (*in == '<') {
      const char* end = std::strchr(in, '>');
      const size_t len = end != nullptr ? size_t(end - in) + 1 : std::strlen(in);
      if (len == 3 && in[1] == 'k') {
        wait = true;
      } else if (len == 3 && in[1] == 'p') {
        o += "\n<k>\n<p>\n";
        wait = false;
        line = 0;
      } else {
        o.append(in, len);
      }
      in += len;
      continue;
    }
    const char* w = in;
    int width = 0;
    if ((unsigned char)*in >= 0xE0) {
      in += (unsigned char)*in >= 0xF0 ? 4 : 3;
      while ((unsigned char)*in >= 0x80 && (unsigned char)*in < 0xC0) in++;
      width = 2;
    } else {
      while (*in != '\0' && *in != ' ' && *in != '\n' && *in != '<' && (unsigned char)*in < 0xE0) {
        width += ((unsigned char)*in & 0xC0) != 0x80;
        in++;
      }
    }
    const bool cjk = (unsigned char)*w >= 0xE0;
    const bool gap = line > 0 && !cjk && !prevCjk;
    if (line > 0 && line + int(gap) + width > kCols) {
      o += "\n";
      line = 0;
    } else if (gap) {
      o += " ";
      line++;
    }
    o.append(w, size_t(in - w));
    line += width;
    prevCjk = cjk;
  }
  if (wait) o += "\n<k>";
  return o.c_str();
}

struct Made {
  ttyd_rando::Options o;
  bool ok = false, tried = false;
  std::map<int, int64_t> byLocation; // kTtydApLocations index -> Archipelago item
} sMade;

// The placements of the file in play (made once per seed and rules)
const Made& made() {
  ttyd_rando::Options o;
  portOpenFileOptions(o);
  const std::string seed = o.seed;
  const int logic = o.pcLogic;
  const ttyd_rando::Options& m = sMade.o;
  if (sMade.tried && m.seed == o.seed && m.pools == o.pools && m.goalStars == o.goalStars && m.pcLogic == o.pcLogic &&
      m.logicLevel == o.logicLevel && m.tricks == o.tricks && m.extra == o.extra && m.entrances == o.entrances) {
    return sMade;
  }
  sMade = Made();
  sMade.o = o;
  sMade.tried = true;
  const ttyd_rando::Result r = ttyd_rando::generate(o);
  sMade.ok = r.ok;
  if (!r.ok) {
    portApLog("open world: seed %s: %s", seed.c_str(), r.error.c_str());
    return sMade;
  }
  for (const ttyd_rando::Placement& p : r.placements) sMade.byLocation[p.location] = p.item;
  portApLog("open world: seed %s: %d places, %d moved%s", seed.c_str(), r.shuffled, r.moved,
          logic < ttyd_rando::Options::kPcLogicVersion ? " (made with an older version of the logic: its placements as then)" : "");
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

} // namespace

extern "C" {

// Every area's words are written afresh on each link: the link itself starts
// from the REL's own data (rel_pc.c: a static build copies its snapshot back,
// a module build loads the module anew), so a file that isn't an open-world
// one, after one in the same run, finds them as the game has them. (Nothing
// is written back: in a module build a word an earlier link wrote is in a
// module unloaded since.)
void portOpenAreaLinked(const char* area) {
  if (area == nullptr) return;
  if (!portOpenOn()) {
    portOpenSetLecture(nullptr); // (an earlier link's: in a module build, in a module unloaded since)
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
      const bool shelf = target.kind == TTYD_AP_DATA && (l.flags & TTYD_AP_SHOP); // (a shop's: its item, its price)
      if (target.name[0] == '\0' || std::strcmp(target.area, area) != 0 || (target.kind != TTYD_AP_SCRIPT && !shelf)) {
        continue;
      }
      uint8_t* base = static_cast<uint8_t*>(portModsGameAddress(target.name, why));
      if (base == nullptr) {
        portApLog("open world: %s: %s", target.name, why.c_str());
        continue;
      }
      if (shelf) {
        int32_t* value = reinterpret_cast<int32_t*>(base + target.offset);
        *value = target.price ? portApBuyPrice(rom) : rom;
        written++;
        continue;
      }
      intptr_t* word = reinterpret_cast<intptr_t*>(base + target.offset);
      *word = rom;
      written++;
    }
  }
  if (written) portApLog("open world: %s: %d items written", area, written);
}

int portOpenGateTarget(int gate) {
  if (!portOpenOn()) return gate;
  const Made& m = made();
  return m.ok ? ttyd_rando::gateTarget(m.o, gate) : gate;
}

int portOpenRideTarget(int ride) {
  if (!portOpenOn()) return ride;
  const Made& m = made();
  return m.ok ? ttyd_rando::rideTarget(m.o, ride) : ride;
}

int portOpenDoorLead(const char* room, const char* map, const char* bero, const char** toMap, const char** toBero) {
  if (!portOpenOn() || room == nullptr || map == nullptr) return 0;
  const Made& m = made();
  if (!m.ok || !(m.o.entrances & (ttyd_rando::kEntranceRooms | ttyd_rando::kEntranceMix))) return 0;
  static ttyd_rando::Options cachedFor;
  static std::vector<int> targets;
  if (targets.empty() || cachedFor.seed != m.o.seed || cachedFor.entrances != m.o.entrances) {
    cachedFor = m.o;
    targets = ttyd_rando::doorTargets(m.o);
  }
  const std::vector<ttyd_rando::DoorHalf>& halves = ttyd_rando::doorHalves();
  for (size_t h = 0; h < halves.size() && h < targets.size(); h++) {
    // the door taken: in this room, leading where the game has it lead
    const ttyd_rando::DoorHalf& there = halves[h ^ 1];
    if (std::strncmp(room, halves[h].map, std::strlen(halves[h].map)) != 0 || std::strcmp(map, there.map) != 0 ||
        bero == nullptr || std::strcmp(bero, there.bero) != 0) {
      continue;
    }
    if (targets[h] == int(h ^ 1)) return 0;
    *toMap = halves[targets[h]].map;
    *toBero = halves[targets[h]].bero;
    return 1;
  }
  return 0;
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

const char* portOpenSpeech(const char* text) { return speech(text); }

const char* portOpenMsg(const char* tag) {
  if (tag == nullptr) return nullptr;
  if (std::strcmp(tag, "pc_open_partner_item") == 0) return portTrGame("A partner who joins Mario's party.");
  // the curses' names, for a place's pickup (open_world_pc.c's prepareTokens)
  if (std::strcmp(tag, "pc_open_curse_paper") == 0) return portTrGame("Paper Mode");
  if (std::strcmp(tag, "pc_open_curse_tube") == 0) return portTrGame("Tube Mode");
  if (std::strcmp(tag, "pc_open_curse_plane") == 0) return portTrGame("Plane Mode");
  if (std::strcmp(tag, "pc_open_curse_boat") == 0) return portTrGame("Boat Mode");
  // the one-way trips' refusals (portEvtOpenTripReady): speech, laid out as the game's
  // (the pages are the text's own, the same in every language: online, every player's window
  // takes the same presses)
  if (std::strcmp(tag, "pc_open_trip_ship") == 0) {
    return speech(portTrGame("Hold on!<p>Once we set sail, there's no coming back until the job's done.<p>Make sure "
                             "you've got everything you'll need out there first!<k>"));
  }
  if (std::strcmp(tag, "pc_open_trip_train") == 0) {
    return speech(portTrGame("Once we leave, this train doesn't stop until the end of the line.<p>Better get ready "
                             "first.<p>Something you'll need on the way isn't with you yet!<k>"));
  }
  if (std::strcmp(tag, "pc_open_trip_blimp") == 0) {
    return speech(portTrGame("Hold it!<p>Where this blimp is headed today, there's no flight back until the job's "
                             "done.<p>Something you'll need out there isn't with you yet!<k>"));
  }
  if (std::strcmp(tag, "pc_open_trip_moon") == 0) {
    return speech(portTrGame("Once you're up there, there's no way back down until the job's done.<p>Get "
                             "everything you'll need first,<p>then come see me!<k>"));
  }
  // jin_08's Letter "p" chest before Vivian's scene (its evt_open_wait)
  if (std::strcmp(tag, "pc_open_letter_p") == 0) {
    return speech(portTrGame("The lid won't budge.<p>Maybe it will open once things around here have settled down.<k>"));
  }
  return nullptr;
}

} // extern "C"
