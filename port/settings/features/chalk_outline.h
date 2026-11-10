// Rogueport's chalk outline without a Japanese disc (restorations.cpp): this
// disc's own m/gor_01/d with the Japanese release's floor marks put back.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The room `room` (this disc's m/gor_01/d, as read from the disc) with the
// chalk outline and the floor marks added, into `out`. The outline is traced
// from the art recipe image art/restorations/chalk_body.png (Mario's own
// texture on this disc, port/assets/luigi/art/recipes/restorations.recipe)
// and drawn as the Japanese release draws its own: a chalk-white strip on the
// floor, under the flies. The marks are this disc's own stain texture
// (gor_yogosi_01, still on every disc) where the Japanese map puts them.
// False, and why, if it can't be done.
bool portChalkOutlineRoom(const std::vector<uint8_t>& room, std::vector<uint8_t>& out, std::string& why);
