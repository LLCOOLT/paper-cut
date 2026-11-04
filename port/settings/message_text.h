// The message windows' text page by page, for the features that follow it:
// text to speech (features/speech.cpp) and the message log
// (features/message_log.cpp). msgdrv.c's hooks (port/include/port/speech.h)
// come to features/message_text.cpp, which splits each window's text at the
// game's page breaks (<p>) and hands each page on as it shows.
#pragma once

#include <string>

namespace msgtext {

// the game's text is Shift-JIS (the Japanese release), else Latin-1 (Windows-1252)
bool japanese();

// a page's words: the tags out, the lines joined (with a space, or none in
// Japanese), the spaces single; still in the game's encoding
std::string plain(const std::string& raw);

// the game's text (its encoding, tags and all) as UTF-8 words, on one line
std::string utf8(const char* game);

// A page shown: its words (plain), who says it (the speaking NPC's name in the
// message data, the game's encoding; empty when none), and whether it is new
// (false: the player paged back to it). closed: the window went.
using PageFn = void (*)(const void* window, const std::string& words, const char* speaker, bool fresh);
using ClosedFn = void (*)(const void* window);
// Call once, at start (a feature's static initializer).
void listen(PageFn page, ClosedFn closed);

} // namespace msgtext
