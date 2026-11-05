// Paper Cut's own open world (docs/RANDOMIZER.md, "Paper Cut's open world"):
// the regions of the game a stage of it opens, the ways between them and
// the story's own conditions on its places, written from the decomp's map
// scripts. The places themselves, their items and the item rules inside a
// chapter are the TTYD apworld's data (ttyd_rando_logic.h); what is here is
// what differs because the game is Paper Cut's, not the TTYD AP mod's:
//
// - each chapter's story runs on its own (port/src/common/open_world_pc.c),
//   so a chapter opens by what its first room needs, and inside it the story
//   still goes in the game's order;
// - the prologue is the game's own, so what it needs stays where it is
//   (Goombella, the Black Key and the Plane Curse in the sewers);
// - a place the game only fills once a scene of its chapter has played asks
//   for that scene's places too (Koops waits for both stones to be taken).
//
// A later stage adds its chapters' regions, ways, story conditions and goal
// places here; nothing else changes.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace ttyd_rando {

// A condition: an item (a number of times), all of, any of, a place reached.
struct PcRule {
  enum Op { kTrue, kHas, kAll, kAny, kReach } op = kTrue;
  std::string name; // an item (kTtydRandoItems) or a place (kTtydApLocations' name)
  int count = 1;
  std::vector<PcRule> kids;
};

inline PcRule pcTrue() { return {}; }
inline PcRule pcHas(const char* item, int count = 1) {
  PcRule r;
  r.op = PcRule::kHas;
  r.name = item;
  r.count = count;
  return r;
}
inline PcRule pcReach(const char* location) {
  PcRule r;
  r.op = PcRule::kReach;
  r.name = location;
  return r;
}
inline PcRule pcAll(std::initializer_list<PcRule> kids) {
  PcRule r;
  r.op = PcRule::kAll;
  r.kids = kids;
  return r;
}
inline PcRule pcAny(std::initializer_list<PcRule> kids) {
  PcRule r;
  r.op = PcRule::kAny;
  r.kids = kids;
  return r;
}

struct PcEdge {
  const char* from; // kTtydRandoRegions' names
  const char* to;
  PcRule rule;
};

struct PcPlaceRule {
  const char* location; // its own rule (the apworld's) and this
  PcRule rule;
};

struct PcWorld {
  int stage;
  std::vector<const char*> regions;    // the regions this stage plays
  std::vector<const char*> hubRegions; // where only a room's own items may hold key items
  std::vector<PcEdge> edges;
  std::vector<PcPlaceRule> places;
  std::vector<int64_t> fixed;          // places that keep their item (the prologue needs it)
  std::vector<int64_t> goal;           // places that end this stage (its Crystal Stars)
  std::vector<int> chapters;           // the chapters it opens
};

// Stage 1: Rogueport, the sewers, chapters 1 and 2.
inline const PcWorld& pcWorld() {
  static const char* const kSunStone = "Petal Meadows Sewers East Key Room: Sun Stone";
  static const char* const kMoonStone = "Petal Meadows Sewers West Key Room: Moon Stone";
  static const PcWorld w = {
      1,
      {"Menu", "Rogueport", "Rogueport Sewers", "Petal Meadows (Left)", "Petal Meadows (Right)",
       "Hooktail's Castle", "Boggly Woods", "Great Tree"},
      {"Rogueport", "Rogueport Sewers"},
      {
          // After the prologue (the game's own) Mario is in Rogueport with
          // Goombella and the Plane Curse.
          {"Menu", "Rogueport", pcTrue()},
          {"Rogueport", "Rogueport Sewers", pcTrue()},
          // tik_02's pipe, past the Thousand-Year Door's room (the prologue
          // flew there): chapter 1's title, then hei_00.
          {"Rogueport Sewers", "Petal Meadows (Left)", pcHas("Plane Mode")},
          {"Petal Meadows (Left)", "Petal Meadows (Right)", pcTrue()},
          // hei_00: both stones taken (the sockets show only then,
          // GSWF_HEI_*_STONE_TAKEN), both placed, and the two timed switches
          // pressed at once (a shell).
          {"Petal Meadows (Left)", "Hooktail's Castle",
           pcAll({pcHas("Sun Stone"), pcHas("Moon Stone"), pcAny({pcHas("Koops"), pcHas("Bobbery")}),
                  pcReach(kSunStone), pcReach(kMoonStone)})},
          // tik_03's pipe (chapter 2's title, then win_06): the room's narrow
          // gap needs the Paper Curse.
          {"Rogueport Sewers", "Boggly Woods", pcHas("Paper Mode")},
          // mri_00's way in opens in the scene after Flurrie's (story 87 to 88).
          {"Boggly Woods", "Great Tree", pcAll({pcHas("Flurrie"), pcReach("Boggly Woods Flurrie's House: Flurrie")})},
      },
      {
          // hei_01's nokotaro_nakama: Koops waits until both stones are taken.
          {"Petal Meadows Bridge Room: Koops", pcAll({pcReach(kSunStone), pcReach(kMoonStone)})},
      },
      {78780002 /* Rogueport Center: Goombella */, 78780136 /* Black Key (Plane) */,
       78780153 /* Black Chest Room: Plane Mode */},
      {78780209 /* Hooktail's Room: Diamond Star */, 78780232 /* Great Tree Entrance: Emerald Star */},
      {1, 2},
  };
  return w;
}

} // namespace ttyd_rando
