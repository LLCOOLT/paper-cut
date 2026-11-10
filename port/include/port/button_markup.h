#pragma once
/* Button icons in Paper Cut's own text (port/settings/features/button_markup.cpp).
 *
 * The English texts (and their translations, port/assets/lang) name buttons
 * as {A}, {B}, {X}, {Y}, {Z}, {L}, {R}, {START}, {STICK}, {CSTICK} and {DPAD}
 * (the game's buttons), and Paper Cut's actions by their bindings: {SETTINGS},
 * {FF}, {REWIND}, {PHOTO}, {SCREENSHOT}, {LOG}, {SWAP}, {BOOST}, {PING},
 * {SAVESTATE}, {LOADSTATE}, {LUA}, {LAUNCHER}; {NOTE}, what opens a paper note (its
 * action's binding where that controller has it free, else the game's L). Each shows as what does it now for player
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
 * {PADA}, {PADB} and {PADSTART} are for text about any controller ("A
 * controller joins with {PADSTART}"): as {A}, {B} and {START} while player 1
 * plays on a controller, and a controller's button (never a key) while they
 * play on the keyboard: the first controller connected's, else the GameCube's.
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
/* the game's icons' size in what portButtonsGame expands from now on, a capital letter's height (0.40: the
 * help box's letters, at scale 1; for smaller text that much less, as the Bindings page's rows at 0.8: 0.32);
 * the size before. 0 or less: only asks. */
float portButtonsGameIconScale(float scale);
/* the scale the text with those icons is drawn at (winFontSet's), so each icon sits centred on its capitals
 * at whatever size it is: what a text helper drawing at scale s sets around its measuring and drawing (left at
 * its default, an icon is placed for the text its size is a capital's height of: at 0.40, text at 1, and at
 * 0.5 it sat high). 0 or less: back to that default. The scale before. */
float portButtonsGameTextScale(float scale);
/* a token at p (its length in *len) as the game's font shows it now, in the Start menu's wrap
 * columns (an icon 1, a word its letters); -1 when p is at none */
int portButtonsGameColumns(const char* p, int* len);
/* Per player: from now on (on this thread) the tokens show the buttons of the controller in that port (0-3:
 * players 2-4's cards, hints and notes their own controller's, an Xbox, PlayStation, Switch or GameCube
 * one's, by its SDL type; Controls > Button prompts' GameCube keeps everyone's the game's own). The game's
 * font then draws a port's button icons with drawings of their own (PORT_ICON_PAD_BASE, icondrv.c; the
 * button prompts' glyphs) where they differ from player 1's. The port before. */
int portButtonsPlayer(int port);
/* icondrv.c: the port's own button icons, PORT_ICON_PAD_BASE + port * 16 + button * 2 + pressed (button 0-7:
 * A B X Y Z L R Start): the GameCube icon's frame they borrow, and (portIconTexOverride) their picture */
#define PORT_ICON_PAD_BASE 0x7D00
#define PORT_ICON_PAD_COUNT 64
int portPadIconTex(unsigned iconId, void* texObj);
/* the page's fonts are all added (launcher_style.cpp's portUiInit): the icons
 * put in each of maxPx pixels or less (the text's, not the titles') and the atlas built */
void portButtonGlyphsInit(float maxPx);

#ifdef __cplusplus
}
#endif
