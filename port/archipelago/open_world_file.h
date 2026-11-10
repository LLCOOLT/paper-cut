// Paper Cut's open world: the file in play as the generator sees it
// (open_world_trips.cpp, every backend). Its options from the file's stamp
// and rules, what Mario holds and which places were found; the trips' check,
// the open world's placements (open_world.cpp) and the tracker share them.
#pragma once

#include "rando_gen.h"

#include <cstdint>
#include <vector>

// The file in play's options (seed, pools, goal, logic version and level,
// tricks); false when it isn't an open-world file.
bool portOpenFileOptions(ttyd_rando::Options& o);
// What Mario holds, as the logic counts it (ttyd_rando::heldOf over the
// file's pouch, party, Crystal Stars and the places found, on the seed's
// `placements`), and the places whose flags are set (kTtydApLocations
// indexes).
void portOpenFileState(const std::vector<ttyd_rando::Placement>& placements, std::vector<int64_t>& held,
                       std::vector<int>& found);
