// Button glyphs for the game's button prompts (port/settings/features/button_prompts.cpp):
// drawn here, in software, as RGBA8 images. Paper Cut's own drawings (shapes and the
// Fredoka typeface), nothing from a console maker's art.
#pragma once

#include <cstdint>
#include <vector>

enum class GlyphStyle { Xbox, PlayStation, Switch, Keyboard, Generic };
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
