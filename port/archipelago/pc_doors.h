// Paper Cut's open world: the doors, pipes and rides the entrance shuffle
// moves (docs/RANDOMIZER.md, "The entrance shuffle"), beyond the chapter gates
// (pc_world.h's pcGateRegions). Each door is the decomp's own BeroEntry (the
// map's hit name and where it leads; tools/port/gen_pc_doors.py lists them all
// and --check checks these against the map sources).
//
// Which doors take part is what keeps every seed sound with the logic's
// regions (the apworld's, a whole area each):
//
// - Room doors (kEntranceRooms): doors between two rooms of one region that
//   Mario walks through with nothing, in rooms he crosses with nothing (the
//   towns' streets and the open fields). They swap among the doors of their
//   area, and a shuffle only counts if each region's rooms still join up
//   through them (the generator draws again otherwise): then a region is
//   still one place to the logic. Doors anywhere else (a gap, a switch, a
//   story gate on either side) stay as they are.
// - The ways between areas (kEntranceMix): a door that is the one way from
//   one region into the next (pc_world.h's edge from -> to is this door
//   alone). Mixed, exit k leads to the entrance of way perm[k], and that
//   entrance back to exit k. Mario still arrives where the region is entered
//   in the game, so the region is the same place to the logic; the way's
//   rule is both ways' own (what the exit's side asked for and what the
//   entrance's side did), which is never less than either side needs. The
//   logic counts only these ways forward; the way back is the game's bonus.
// - The rides (kEntranceTransports): the blimp, Flavio's ship and the train
//   lead to one another's destinations; a ride's rule is its ticket (what its
//   own attendant asks for) and what its destination's side asked for.
//
// Nothing here is in a one-way trip (pc_world.h's trips: Keelhaul Key, the
// Pirate's Grotto, the train, Riverside Station, the moon): those keep their
// own rooms.
#pragma once

#include "pc_world.h"

namespace ttyd_rando {

struct PcDoor {
  const char* map;  // the room (the map's name)
  const char* bero; // the door (its BeroEntry hit name)
};

// A room door: two doors of one region that lead to each other, in an area
// (the shuffle's group).
struct PcRoomDoor {
  PcDoor a, b;
  const char* region; // kTtydRandoRegions' name
  const char* area;
};

inline const std::vector<PcRoomDoor>& pcRoomDoors() {
  static const std::vector<PcRoomDoor> d = {
      // Rogueport: the docks, the plaza, the east side (the west gate is Zess
      // T.'s, the sewers' pipe the prologue's: they stay)
      {{"gor_00", "n_bero"}, {"gor_01", "s_bero"}, "Rogueport", "Rogueport"},
      {{"gor_01", "e_bero"}, {"gor_02", "w_bero"}, "Rogueport", "Rogueport"},
      // Boggly Woods: the pipe's clearing to the Great Tree's path and on
      {{"win_06", "e_bero"}, {"win_00", "w_bero"}, "Boggly Woods", "Boggly Woods"},
      {{"win_00", "e_bero"}, {"win_01", "w_bero"}, "Boggly Woods", "Boggly Woods"},
      {{"win_01", "e_bero"}, {"win_02", "w_bero"}, "Boggly Woods", "Boggly Woods"},
      // Poshley Heights: the station, the houses, the square
      {{"pik_00", "se_bero"}, {"pik_04", "nw_bero"}, "Poshley Heights", "Poshley Heights"},
      {{"pik_04", "se_bero"}, {"pik_01", "nw_bero"}, "Poshley Heights", "Poshley Heights"},
      // Fahr Outpost: the pipe's field and the two snowfields before the town
      {{"bom_00", "e_bero"}, {"bom_03", "w_bero"}, "Fahr Outpost", "Fahr Outpost"},
      {{"bom_03", "e_bero"}, {"bom_04", "w_bero4"}, "Fahr Outpost", "Fahr Outpost"},
  };
  return d;
}

// A way between areas: the exit in region `from` and the entrance in region
// `to` it leads to (pc_world.h's edge from -> to, which is this door alone).
struct PcWay {
  PcDoor exit, entrance;
  const char* from;
  const char* to;
  const char* name; // for the spoiler log
};

inline const std::vector<PcWay>& pcWays() {
  static const std::vector<PcWay> w = {
      {{"hei_00", "a_usiro_bero"}, {"gon_00", "w_bero"}, "Petal Meadows (Left)", "Hooktail's Castle",
       "Petal Meadows' road to Hooktail's Castle"},
      {{"win_01", "n_bero"}, {"mri_00", "w_bero"}, "Boggly Woods", "Great Tree", "Boggly Woods' path to the Great Tree"},
      {{"usu_01", "e_bero"}, {"gra_00", "w_bero"}, "Twilight Town", "Twilight Trail", "Twilight Town's east gate"},
      {{"gra_06", "n_bero"}, {"jin_00", "w_bero"}, "Twilight Trail", "Creepy Steeple", "Twilight Trail's steeple door"},
      {{"tik_17", "dokan_1"}, {"pik_01", "dokan_1"}, "Rogueport Sewers", "Poshley Heights",
       "the sewers' pipe to Poshley Heights"},
  };
  return w;
}

// The rides (kEntranceTransports), in this order: 0 the blimp, 1 Flavio's
// ship, 2 the train. Shuffled, ride r goes where ride perm[r] goes in the
// game: its rule is its own ticket (`leave`) and the destination's `arrive`.
struct PcRide {
  const char* from; // pc_world.h's edge from -> to that is the ride
  const char* to;
  PcRule leave, arrive;
  const char* name;
};

inline const std::vector<PcRide>& pcRides() {
  static const std::vector<PcRide> r = {
      {"Rogueport (Westside)", "Glitzville", pcHas("Blimp Ticket"), pcTrue(), "the blimp"},
      // (Keelhaul Key's own first steps ask for Yoshi and the Tube Curse: the apworld's)
      {"Rogueport", "Keelhaul Key", pcHas("Old Letter"), pcAll({pcHas("Yoshi"), pcHas("Paper Mode"), pcHas("Tube Mode")}),
       "Flavio's ship"},
      {"Rogueport (Westside)", "Excess Express", pcHas("Train Ticket"), pcTrue(), "the train"},
  };
  return r;
}

} // namespace ttyd_rando
