// Paper Cut's own open world (docs/RANDOMIZER.md, "Paper Cut's open world"):
// the regions of the game a stage of it opens, the ways between them and
// the story's own conditions on its places, written from the decomp's map
// scripts. The places themselves, their items and the item rules inside a
// chapter are the TTYD apworld's data (ttyd_rando_logic.h); what is here is
// what differs because the game is Paper Cut's, not the TTYD AP mod's:
//
// - each chapter's story runs on its own (port/src/common/open_world_pc.c),
//   so a chapter opens by what its first room needs, and inside it the story
//   still goes in the game's order: a place the game only fills once a scene
//   of its chapter has played asks for what that scene's way there needs
//   (Koops waits for both stones to be taken; the Storage Key 2 falls only
//   after the storage room was opened with the first);
// - Rogueport's own story follows how many chapters are done (the "hub
//   stretches", open_world_pc.c), so a Rogueport scene that waits for the
//   n-th chapter's end asks for n Crystal Stars;
// - the prologue is the game's own, so what it needs stays where it is
//   (Goombella, the Black Key and the Plane Curse in the sewers);
// - the goal is a number of Crystal Stars, and the Thousand-Year Door opens
//   with that many (chapter 8's way in): pcGoal().
//
// A later stage adds its chapters' regions, ways, story conditions and goal
// places here; nothing else changes.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace ttyd_rando {

// A condition: an item (a number of times), all of, any of, a place or a
// region reached. An "any of" with nothing in it never holds (pcNever).
struct PcRule {
  enum Op { kTrue, kHas, kAll, kAny, kReach, kRegion, kGoal } op = kTrue;
  // an item (kTtydRandoItems, or any item kTtydRandoItemNames names), a place
  // (kTtydApLocations' name) or a region; kGoal: the goal's Crystal Stars held
  std::string name;
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
inline PcRule pcRegion(const char* region) {
  PcRule r;
  r.op = PcRule::kRegion;
  r.name = region;
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
// the Crystal Stars the seed's goal asks for (Options::goalStars)
inline PcRule pcGoal() {
  PcRule r;
  r.op = PcRule::kGoal;
  return r;
}
inline PcRule pcNever() {
  PcRule r;
  r.op = PcRule::kAny;
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
  std::vector<const char*> hubRegions; // where only a room's own items may hold key items...
  std::vector<PcPlaceRule> hubScenes;  // ...and these Rogueport scenes, with their conditions
  std::vector<PcEdge> edges;
  std::vector<PcPlaceRule> places;
  std::vector<int64_t> fixed;          // places that keep their item (the prologue needs it)
  std::vector<int64_t> goal;           // the Crystal Stars' places, one per chapter
  std::vector<int> chapters;           // the chapters it opens
  std::vector<const char*> extraItems; // key items whose own place can't be reached here, placed anyway
  std::vector<const char*> ways;       // each chapter's first region, in chapters' order
};

// Stage 3: Rogueport and its sewers (both sides), chapters 1 to 8.
inline const PcWorld& pcWorld() {
  static const char* const kAutograph = "Excess Express Locomotive: Autograph";
  static const char* const kDiary = "Excess Express Storage Car: Ragged Diary";
  static const char* const kBlanket = "Excess Express Middle Passenger Car: Blanket";
  static const char* const kVitalPaper = "Excess Express Front Passenger Car: Vital Paper";
  static const char* const kBriefcase = "Excess Express Middle Passenger Car: Briefcase";
  static const char* const kGoldbob = "Poshley Heights Station: Goldbob Guide";
  static const char* const kSunStone = "Petal Meadows Sewers East Key Room: Sun Stone";
  static const char* const kMoonStone = "Petal Meadows Sewers West Key Room: Moon Stone";
  static const char* const kVivian = "Twilight Town Leftside: Vivian";
  static const char* const kLetterP = "Creepy Steeple Parrot Room: The Letter \"p\"";
  static const char* const kCola = "Keelhaul Key Town: Chuckola Cola";
  static const char* const kBobbery = "Keelhaul Key Grotto Entrance: Bobbery";
  static const char* const kSkullGem = "Keelhaul Key Grotto Entrance: Skull Gem";
  // The Contact Lens Zess T. waits for (gor_01's nancy_talk keeps the west
  // gate shut until she has it) is ordered at the shop and only comes once
  // the story is past Hooktail (GSWF_GOR01_LENS_ARRIVED at 56 or later):
  // in Rogueport that is once one chapter is done. It has no place of its
  // own in the apworld, so it never moves.
  static const PcRule kLens = pcHas("stars", 1);
  static const PcRule kTube = pcAll({pcHas("Paper Mode"), pcHas("Tube Mode")});
  // Zip Toad's catch (rsh_02, story 309): the night's scenes done, the Vital
  // Paper handed over (rsh_04's item_tbl)
  static const PcRule kZipToad = pcAll({pcHas("Autograph"), pcHas("Vivian"), pcHas("Ragged Diary"),
                                        pcHas("Blanket"), pcHas("Vital Paper"), pcReach(kVitalPaper)});
  static const PcWorld w = {
      3,
      {"Menu", "Rogueport", "Rogueport (Westside)", "Rogueport Sewers", "Rogueport Sewers Westside",
       "Rogueport Sewers Westside Ground", "Petal Meadows (Left)", "Petal Meadows (Right)", "Hooktail's Castle",
       "Boggly Woods", "Great Tree", "Glitzville", "Twilight Town", "Twilight Trail", "Creepy Steeple", "Keelhaul Key",
       "Pirate's Grotto", "Excess Express", "Riverside Station", "Poshley Heights", "Fahr Outpost", "X-Naut Fortress",
       "Palace of Shadow", "Palace of Shadow (Post-Riddle Tower)", "Shadow Queen"},
      {"Rogueport", "Rogueport (Westside)", "Rogueport Sewers", "Rogueport Sewers Westside",
       "Rogueport Sewers Westside Ground"},
      {
          // gor_03's maffiaboss_ticket_put: Don Pianta's ticket, in the
          // stretch after the second chapter (story 122 to 125). Its way
          // there passes tik_04's Puni scene, past the paper gap.
          {"Rogueport Westside: Blimp Ticket", pcAll({pcHas("stars", 2), pcHas("Paper Mode")})},
          // gor_01's Ultra Hammer chest sits on a roof from the start and
          // falls for a spin jump (the apworld's rule); in an open-world file
          // it stays whatever hammer Mario has (gor_01.c), so it is the
          // room's own whenever.
          {"Rogueport Center: Ultra Hammer", pcTrue()},
      },
      {
          // After the prologue (the game's own) Mario is in Rogueport with
          // Goombella and the Plane Curse.
          {"Menu", "Rogueport", pcTrue()},
          {"Rogueport", "Rogueport Sewers", pcTrue()},
          // The west side: Zess T.'s gate (the lens), or the apworld's other
          // ways round through the sewers.
          {"Rogueport", "Rogueport (Westside)",
           pcAny({kLens, pcHas("Bobbery"), kTube, pcHas("Progressive Hammer", 2)})},
          {"Rogueport", "Rogueport Sewers Westside",
           pcAny({kTube, pcHas("Bobbery"), pcAll({pcHas("Paper Mode"), kLens}),
                  pcAll({pcHas("Progressive Hammer", 2),
                         pcAny({pcHas("Paper Mode"), pcAll({pcHas("Progressive Boots", 2), pcHas("Yoshi")})})})})},
          {"Rogueport", "Rogueport Sewers Westside Ground",
           pcAny({pcAll({kLens, pcHas("Paper Mode")}), pcHas("Bobbery"), kTube, pcHas("Progressive Hammer", 2)})},
          {"Rogueport (Westside)", "Rogueport Sewers Westside", pcHas("Paper Mode")},
          {"Rogueport Sewers Westside Ground", "Rogueport (Westside)", pcTrue()},
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
          // gor_04's blimp: its attendant only asks for the ticket; the
          // flight goes to chapter 3's start (tou_20) the first time.
          {"Rogueport (Westside)", "Glitzville", pcHas("Blimp Ticket")},
          // tik_08's pipe (chapter 4's title, then usu_00), past the gap
          // Yoshi flutters over, or by the spin-jump way below.
          {"Rogueport Sewers Westside", "Twilight Town", pcHas("Yoshi")},
          {"Rogueport Sewers Westside Ground", "Twilight Town", pcHas("Progressive Boots", 2)},
          {"Twilight Town", "Twilight Trail", kTube},
          {"Twilight Trail", "Creepy Steeple", pcAll({pcHas("Paper Mode"), pcHas("Flurrie"), pcHas("Progressive Boots")})},
          // gor_00's harbor: Flavio sails (story 228) once Mario holds the
          // Old Letter (open_world_pc.c). The rest is the apworld's way.
          {"Rogueport", "Keelhaul Key", pcAll({pcHas("Old Letter"), pcHas("Yoshi"), kTube})},
          // Skull Rock opens with the Skull Gem, which Flavio gives only after
          // Bobbery joined, which he does only after the Chuckola Cola.
          {"Keelhaul Key", "Pirate's Grotto",
           pcAll({pcHas("Yoshi"), pcHas("Bobbery"), pcHas("Skull Gem"), pcHas("Progressive Boots"), pcReach(kBobbery),
                  pcReach(kSkullGem)})},
          // gor_04's train: its attendant only asks for the Train Ticket
          // (ekiin_talk); the first ride goes through chapter 6's title.
          {"Rogueport (Westside)", "Excess Express", pcHas("Train Ticket")},
          // The train stops at Riverside (story 309 to 310) once Zip Toad is
          // caught, and that scene doesn't end before Mario hands over the
          // Briefcase (rsh_02's evt_toron_catch2 asks until he does): the
          // Briefcase is the game's need here, not the apworld's.
          {"Excess Express", "Riverside Station",
           pcAll({kZipToad, pcHas("Briefcase"), pcReach(kBriefcase)})},
          {"Riverside Station", "Poshley Heights",
           pcAll({pcHas("Station Key 1"), pcHas("Elevator Key (Station)"), pcHas("Progressive Hammer"),
                  pcHas("Progressive Boots", 2)})},
          // tik_17's pipe up to the Poshley Sanctum (the apworld's other way)
          {"Rogueport Sewers", "Poshley Heights", pcAll({pcHas("Progressive Hammer", 2), pcHas("Progressive Boots")})},
          // tik_07's pipe (chapter 7's title, then bom_00), behind the stone
          // the Ultra Hammer breaks, reached as tik_08's is.
          {"Rogueport Sewers Westside", "Fahr Outpost", pcAll({pcHas("Progressive Hammer", 2), pcHas("Yoshi")})},
          {"Rogueport Sewers Westside Ground", "Fahr Outpost",
           pcAll({pcHas("Progressive Hammer", 2), pcHas("Progressive Boots", 2)})},
          // The cannon to the moon (357) fires once General White is woken
          // and Goldbob agreed (355): Mayor Dour's hunt (354) asks around
          // Petalburg, Glitzville, the Great Tree's Punies and Twilight Town
          // before White sleeps in Fahr Outpost (bom_02's urotuku_white), and
          // Goldbob is at Poshley's station (pik_00). Both happen in those
          // rooms while chapter 7's story is in the hunt (open_world_pc.c).
          // The Goldbob Guide itself is never asked for by the game. Bobbery
          // is the apworld's for the moon.
          {"Fahr Outpost", "X-Naut Fortress",
           pcAll({pcHas("Bobbery"), pcReach(kGoldbob), pcRegion("Petal Meadows (Right)"), pcRegion("Glitzville"),
                  pcRegion("Great Tree"), pcRegion("Twilight Town")})},
          // The Thousand-Year Door (tik_05) opens with the goal's Crystal
          // Stars: Frankly waits in tik_00, then the pedestal, then chapter
          // 8's title and las_00. The way to the door is the apworld's. The
          // door's rooms are chapter 8's only once Rogueport's own story has
          // caught up with the chapters done (open_world_pc.c), and its
          // stretches pass tik_04's Puni scene (the paper gap) and Peach's
          // mail in tik_07 (the west sewers).
          {"Rogueport Sewers", "Palace of Shadow",
           pcAll({pcGoal(), pcHas("Paper Mode"),
                  pcAny({pcRegion("Rogueport Sewers Westside"), pcRegion("Rogueport Sewers Westside Ground")}),
                  pcAny({pcHas("Plane Mode"), pcHas("Progressive Hammer"),
                                   pcAll({pcHas("Flurrie"), pcAny({pcHas("Bobbery"), kTube,
                                                                   pcAll({kLens, pcHas("Paper Mode")})})})})})},
          {"Palace of Shadow", "Palace of Shadow (Post-Riddle Tower)",
           pcAll({kTube, pcHas("Palace Key"), pcHas("Bobbery"), pcHas("Boat Mode"), pcHas("Star Key"),
                  pcHas("Palace Key (Tower)", 8)})},
          {"Palace of Shadow (Post-Riddle Tower)", "Shadow Queen",
           pcAll({pcReach("Palace of Shadow Final Staircase: Ultra Shroom"), pcGoal()})},
      },
      {
          // hei_01's nokotaro_nakama: Koops waits until both stones are taken.
          {"Petal Meadows Bridge Room: Koops", pcAll({pcReach(kSunStone), pcReach(kMoonStone)})},
          // Chapter 3's story: the yellow block (142) needs a hammer, the
          // storage room (147) the first key, the containers (149) Flurrie,
          // the attic (155) the second key, the air duct (160) the paper
          // slit. Yoshi is the apworld's for the arena's last steps.
          {"Glitzville Main Square: Storage Key 1", pcHas("Progressive Hammer")},
          {"Glitzville Lobby: Storage Key 2", pcAll({pcHas("Progressive Hammer"), pcHas("Storage Key 1"), pcHas("Flurrie")})},
          {"Glitzville Major-League Room: Champ's Belt",
           pcAll({pcHas("Progressive Hammer"), pcHas("Storage Key 1"), pcHas("Storage Key 2"), pcHas("Flurrie"), pcHas("Yoshi")})},
          {"Glitzville Arena: Gold Star", pcAll({pcHas("Progressive Hammer"), pcHas("Storage Key 1"), pcHas("Storage Key 2"),
                                                 pcHas("Flurrie"), pcHas("Yoshi"), pcHas("Paper Mode")})},
          // Chapter 4's story: the Superbombomb rolls away (202) only after
          // Doopliss's first battle (200), past the steeple's locked door
          // (199); Vivian joins (206) once it is found; the parrot's room
          // (jin_08, 207 on) is hers to hide in; the name (210), the Ruby
          // Star's battle, needs its "p".
          {"Twilight Town Leftside: Superbombomb", pcAll({pcRegion("Creepy Steeple"), pcHas("Steeple Key")})},
          {kVivian, pcAll({pcRegion("Creepy Steeple"), pcHas("Steeple Key"), pcHas("Superbombomb")})},
          {kLetterP, pcReach(kVivian)},
          {"Creepy Steeple Parrot Room: Steeple Key 2", pcReach(kVivian)},
          {"Creepy Steeple Parrot Room: Mr. Softener", pcReach(kVivian)},
          {"Creepy Steeple Parrot Room: Power Plus", pcReach(kVivian)},
          {"Creepy Steeple Upper Room: Ruby Star", pcReach(kLetterP)},
          // Chapter 5's story: Bobbery's rescue (238) needs Flavio's cola
          // (237); the Skull Gem (241) comes after Bobbery joined (239).
          {kBobbery, pcReach(kCola)},
          {kSkullGem, pcReach(kBobbery)},
          // Podley gives the Old Letter at story 225, after Bobbery refused;
          // Rogueport's own story stops at 222 (Frankly's after chapter 4),
          // since the harbor is chapter 5's from the Old Letter on. Its place
          // is out and the letter is placed among the others (extraItems).
          {"Rogueport Center: Old Letter", pcNever()},
          // After chapter 5 the game goes Keelhaul Key's way before the
          // train: Don Pianta ill (273), Keelhaul at any time (274), the lost
          // wedding ring (275 to 277), Frankie and Francesca home (278), then
          // the Train Ticket (279). Here Rogueport's stretch after the fifth
          // chapter stops at Don Pianta ill and chapter 5's own story at the
          // ship's return (270), so that chain never plays: the ticket's own
          // place is out and the ticket is placed among the others
          // (extraItems), and the ring in the grotto (muj_05, from 274) never
          // shows. Nothing else asks for the ring.
          {"Rogueport Westside: Train Ticket", pcNever()},
          {"Keelhaul Key Grotto Entrance: Wedding Ring", pcNever()},
          // Chapter 6's story on the train, in the game's order: the
          // Autograph (293) goes to Bub (294), the ghost (296, with Vivian)
          // lets Mario into the storage car for her diary (298 to 299), the
          // diary buys the Blanket (300), the Blanket is the Vital Paper's way
          // (301 to 305), the Vital Paper Zip Toad's (306 to 309).
          {kDiary, pcAll({pcHas("Autograph"), pcHas("Vivian"), pcReach(kAutograph)})},
          {kBlanket, pcReach(kDiary)},
          {kVitalPaper, pcReach(kBlanket)},
          {kBriefcase, kZipToad},
          {"Excess Express Middle Passenger Car: Gold Ring", kZipToad},
          {"Excess Express Middle Passenger Car: Shell Earrings", kZipToad},
          // Goldbob's cannon go-ahead: from Mayor Dour's talk on (354), at
          // pik_00 while chapter 7's story is in the hunt.
          {kGoldbob, pcRegion("Fahr Outpost")},
      },
      {78780002 /* Rogueport Center: Goombella */, 78780136 /* Black Key (Plane) */,
       78780153 /* Black Chest Room: Plane Mode */},
      {78780209 /* Hooktail's Room: Diamond Star */, 78780232 /* Great Tree Entrance: Emerald Star */,
       78780287 /* Glitzville Arena: Gold Star */, 78780437 /* Creepy Steeple Upper Room: Ruby Star */,
       78780511 /* Pirate's Grotto Cortez' Hoard: Sapphire Star */, 78780554 /* Poshley Sanctum: Garnet Star */,
       78780604 /* X-Naut Fortress Boss Room: Crystal Star */},
      {1, 2, 3, 4, 5, 6, 7, 8},
      {"Old Letter", "Train Ticket"},
      {"Petal Meadows (Left)", "Boggly Woods", "Glitzville", "Twilight Town", "Keelhaul Key", "Excess Express",
       "Fahr Outpost", "Palace of Shadow"},
  };
  return w;
}

} // namespace ttyd_rando
