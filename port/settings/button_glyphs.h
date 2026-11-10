// Button glyphs for the game's button prompts (port/settings/features/button_prompts.cpp):
// drawn here, in software, as RGBA8 images. Paper Cut's own drawings (shapes and the
// Fredoka typeface), nothing from a console maker's art.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class GlyphStyle { Xbox, PlayStation, Switch, Keyboard, Generic, SteamDeck, GameCube };
// the GameCube buttons the game shows: A, B, X, Y, Z, L, R, Start
enum class GlyphButton { A, B, X, Y, Z, L, R, Start };
// the native button a GameCube one is on (button_prompts.cpp decides; the label
// is drawn for the keyboard, the shapes for the face buttons of a pad)
// (the text's button icons, port/settings/features/button_markup.cpp, add the sticks
// and the D-pad; with GlyphStyle::GameCube, GcA to GcStart are the GameCube's own)
enum class GlyphFace { South, East, West, North, LeftShoulder, RightShoulder, LeftTrigger, RightTrigger, Start, Back,
                       Other, LeftStick, RightStick, DPad, GcA, GcB, GcX, GcY, GcZ, GcL, GcR, GcStart };

struct GlyphSpec {
  GlyphStyle style = GlyphStyle::Generic;
  GlyphFace face = GlyphFace::South;
  const char* label = nullptr; // a key's name (keyboard), or the text on an Other button
  bool pressed = false;
  float textScale = 0.f; // a key cap's name: its capitals' height in the glyph's (0: as the prompts draw it)
};

// An image of w x h pixels, RGBA8 (straight alpha), top row first.
std::vector<uint8_t> drawButtonGlyph(const GlyphSpec& spec, int w, int h);
// The font the labels are drawn in (TrueType bytes; set once, before drawing).
void setGlyphFont(const unsigned char* ttf, unsigned size);

// The button prompts now (port/settings/features/button_prompts.cpp): the style
// they draw, or false for the game's own GameCube pictures (the setting, or
// Auto with a GameCube controller or touch); and each GameCube button's glyph
// (A, B, X, Y, Z, L, R, Start) as they draw it, from player 1's bindings.
bool buttonPromptsStyle(GlyphStyle* out);
void buttonPromptsPlan(GlyphStyle style, GlyphSpec specs[8], std::string labels[8]);
// The same for a controller port (0-3): players 2-4 their own controller's (Auto or a fixed style alike;
// GameCube: the game's own for everyone), from that port's bindings; and whether the game's font draws
// that port's buttons with icons of their own (PORT_ICON_PAD_BASE: they look unlike player 1's).
bool buttonPromptsStyleFor(unsigned port, GlyphStyle* out);
void buttonPromptsPlanFor(unsigned port, GlyphStyle style, GlyphSpec specs[8], std::string labels[8]);
bool buttonPromptsOwnIcons(unsigned port);
// Text about any controller ("A controller joins with {PADSTART}", button_markup.cpp) while player 1 plays on
// the keyboard: true, with the look a controller's buttons have there (the first one connected's, out of the
// box; none: GameCube, the game's own), drawn as port 0's own icons (buttonPromptsOwnIcons(0)). False: player
// 1's look is a controller's or the game's own, which the plain tokens show.
bool buttonPromptsAnyPad(GlyphStyle* style);

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
// The same in the game font's encoding (portTrGame; a driver's name only its ASCII), for the Start menu
std::string controllerNameGame(unsigned index);
std::string controllerDriverName(unsigned index);
