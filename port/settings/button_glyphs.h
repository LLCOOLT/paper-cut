// Button glyphs for the game's button prompts (port/settings/features/button_prompts.cpp):
// drawn here, in software, as RGBA8 images. Paper Cut's own drawings (shapes and the
// Fredoka typeface), nothing from a console maker's art.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class GlyphStyle { Xbox, PlayStation, Switch, Keyboard, Generic, SteamDeck };
// the GameCube buttons the game shows: A, B, X, Y, Z, L, R, Start
enum class GlyphButton { A, B, X, Y, Z, L, R, Start };
// the native button a GameCube one is on (button_prompts.cpp decides; the label
// is drawn for the keyboard, the shapes for the face buttons of a pad)
enum class GlyphFace { South, East, West, North, LeftShoulder, RightShoulder, LeftTrigger, RightTrigger, Start, Back,
                       Other };

struct GlyphSpec {
  GlyphStyle style = GlyphStyle::Generic;
  GlyphFace face = GlyphFace::South;
  const char* label = nullptr; // a key's name (keyboard), or the text on an Other button
  bool pressed = false;
};

// An image of w x h pixels, RGBA8 (straight alpha), top row first.
std::vector<uint8_t> drawButtonGlyph(const GlyphSpec& spec, int w, int h);
// The font the labels are drawn in (TrueType bytes; set once, before drawing).
void setGlyphFont(const unsigned char* ttf, unsigned size);

// The style of an SDL gamepad type (SDL_GamepadType), with its USB ids: a
// Steam Deck's own controller (or Steam's virtual one, on a Deck) is the Deck's.
GlyphStyle glyphStyleOf(int sdlGamepadType, unsigned vendor, unsigned product);
// The style of the controller a player (0-3) has now (Xbox when none).
GlyphStyle glyphStyleOfPort(unsigned port);
// A native button's name as printed on that style's controller (SDL_GamepadButton;
// PAD_NATIVE_BUTTON_INVALID with the GameCube's L or R: the trigger it reads),
// English ("D-pad up", "Left stick press") for the port's translations to take.
const char* nativeButtonLabel(GlyphStyle style, uint32_t native, uint16_t padButton);

// The controller at Aurora's index (PADGetSDLGamepadForIndex) as players know it,
// in the menu language: "Xbox controller", "DualSense", "Switch Pro Controller",
// "Steam Deck"..., with a number when two connected ones are alike ("Xbox
// controller 2"); the driver's name for a kind SDL doesn't know
// (port/settings/features/controller_names.cpp). And the driver's name itself
// ("XInput Controller #1"), for a tooltip.
std::string controllerName(unsigned index);
std::string controllerDriverName(unsigned index);
