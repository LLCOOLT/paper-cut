// Controllers' names as players know them (button_glyphs.h): "Xbox
// controller", "DualSense", "Switch Pro Controller", "Steam Deck"... from the
// kind SDL knows (its gamepad type and USB ids), with a number when two
// connected ones have the same name; the driver's name ("XInput Controller
// #1") only for a kind SDL doesn't know. The Controllers list and the Players
// section show these, the driver's name as their tooltip.
#include "port/tr.h"
#include "../button_glyphs.h"

#include <SDL3/SDL.h>
#include <dolphin/pad.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// the English name of the controller pad is (nullptr: SDL doesn't know its kind)
const char* kindOf(SDL_Gamepad* pad) {
  const unsigned vendor = SDL_GetGamepadVendor(pad), product = SDL_GetGamepadProduct(pad);
  if (glyphStyleOf(SDL_GetGamepadType(pad), vendor, product) == GlyphStyle::SteamDeck) {
    return "Steam Deck";
  }
  if (vendor == 0x28DE && (product == 0x1102 || product == 0x1142)) {
    return "Steam Controller";
  }
  switch (SDL_GetGamepadType(pad)) {
  case SDL_GAMEPAD_TYPE_XBOX360: return "Xbox 360 controller";
  case SDL_GAMEPAD_TYPE_XBOXONE: return "Xbox controller";
  case SDL_GAMEPAD_TYPE_PS3: return "DualShock 3";
  case SDL_GAMEPAD_TYPE_PS4: return "DualShock 4";
  case SDL_GAMEPAD_TYPE_PS5: return vendor == 0x054C && product == 0x0DF2 ? "DualSense Edge" : "DualSense";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: return "Switch Pro Controller";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT: return "Joy-Con (L)";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return "Joy-Con (R)";
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "Joy-Con pair";
  case SDL_GAMEPAD_TYPE_GAMECUBE: return "GameCube controller";
  default: return nullptr;
  }
}

// the name, in the menus' UTF-8 (portTr) or the game font's encoding (portTrGame)
std::string nameOf(unsigned index, bool game) {
  const auto tr = game ? portTrGame : portTr;
  SDL_Gamepad* pad = index < PADCount() ? PADGetSDLGamepadForIndex(index) : nullptr;
  const char* kind = pad != nullptr ? kindOf(pad) : nullptr;
  if (kind == nullptr) {
    // the driver's, as SDL has it (UTF-8); for the game's font in its encoding
    const char* driver = PADGetNameForControllerIndex(index);
    char shown[64] = "";
    if (driver != nullptr) {
      if (game) {
        portTextForGame(driver, shown, sizeof(shown));
      } else {
        std::snprintf(shown, sizeof(shown), "%s", driver);
      }
    }
    return shown[0] != '\0' ? std::string(shown) : std::string(tr("Controller"));
  }
  // a number when another connected controller has the same name: its place among them
  int same = 0, place = 0;
  for (u32 i = 0; i < PADCount(); i++) {
    SDL_Gamepad* other = PADGetSDLGamepadForIndex(i);
    if (const char* k = other != nullptr ? kindOf(other) : nullptr; k != nullptr && std::strcmp(k, kind) == 0) {
      same++;
      if (i == index) {
        place = same;
      }
    }
  }
  std::string name = tr(kind);
  if (same > 1) {
    name += " " + std::to_string(place);
  }
  return name;
}

} // namespace

std::string controllerName(unsigned index) { return nameOf(index, false); }
std::string controllerNameGame(unsigned index) { return nameOf(index, true); }

std::string controllerDriverName(unsigned index) {
  const char* driver = index < PADCount() ? PADGetNameForControllerIndex(index) : nullptr;
  return driver != nullptr ? driver : "";
}
