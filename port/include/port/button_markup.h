#pragma once
/* Button icons in Paper Cut's own text (port/settings/features/button_markup.cpp).
 *
 * The English texts (and their translations, port/assets/lang) name buttons
 * as {A}, {B}, {X}, {Y}, {Z}, {L}, {R}, {START}, {STICK}, {CSTICK} and {DPAD}
 * (the game's buttons), and Paper Cut's actions by their bindings: {SETTINGS},
 * {FF}, {REWIND}, {PHOTO}, {SCREENSHOT}, {LOG}, {SWAP}, {BOOST}, {PING},
 * {SAVESTATE}, {LOADSTATE}, {LUA}. Each shows as what does it now for player
 * 1, as Controls > Button prompts draws the game's own (its style: the
 * GameCube's, an Xbox, PlayStation, Switch or Steam Deck controller's, or the
 * keyboard's keys), and where it is bound now (Controls > Bindings):
 *
 *  - Dear ImGui (portTr's text): the icons as letters of their own in the
 *    page's fonts (private-use code points, drawn into the font atlas once, at
 *    start: portButtonGlyphsInit), a key's name on a key cap; a binding with
 *    no icon (a paddle, a key with Ctrl) as its name.
 *  - The game's font (portTrGame's text): expanded as it is drawn or measured
 *    (fontmgr.c's FontDrawMessage, FontDrawMessageMtx, FontGetMessageWidthLine):
 *    the game's buttons as its own <icon PAD_A> & co. (whose pictures the
 *    button prompts redraw), the C-stick as the help box's, the rest as words.
 *    Draw only: the port's text is drawn with the game's random numbers put
 *    back after (an icon is not two random numbers a letter, as a word is).
 *  - Words (text to speech, fonts with no icons): the buttons' names.
 *
 * A token not in the list stays as it is. */
#ifdef __cplusplus
extern "C" {
#endif

/* text (UTF-8) with its tokens as the page's icons: text itself when it has
 * none; else the same text for as long as the program runs */
const char* portButtonsUi(const char* text);
/* text in the game's encoding with its tokens as the game's icons or words
 * (until 8 more calls on this thread): text itself when it has none */
const char* portButtonsGame(const char* text);
/* text (UTF-8, game 0; the game's encoding, game 1) with its tokens as words
 * (until 8 more calls on this thread): text itself when it has none */
const char* portButtonsWords(const char* text, int game);
/* a token at p (its length in *len) as the game's font shows it now, in the Start menu's wrap
 * columns (an icon 2, a word its letters); -1 when p is at none */
int portButtonsGameColumns(const char* p, int* len);
/* the page's fonts are all added (launcher_style.cpp's portUiInit): the icons
 * put in each of maxPx pixels or less (the text's, not the titles') and the atlas built */
void portButtonGlyphsInit(float maxPx);

#ifdef __cplusplus
}
#endif
