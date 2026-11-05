#ifndef PORT_PORT_TEXT_H
#define PORT_PORT_TEXT_H

/* Paper Cut's own text in the game's font (port/src/common/port_text_pc.c).
 *
 * The game's font takes two of the game's random numbers for every letter it
 * draws (fontmgr.c, _JUTFont_DrawPos; the shaking text more). Paper Cut's own
 * texts differ from player to player (each one's menu language, button
 * prompts, Play-as names), so drawn as they are they would move the numbers
 * differently in each game of an online session, or of a replay and its
 * recording. Every draw of such a text therefore puts the numbers back as
 * they were; the game's own text takes them exactly as on the GameCube.
 *
 * What counts as Paper Cut's:
 *  - every draw made by Paper Cut's code (port/src): its sources are built
 *    with TTYD_PORT_SOURCE, and manager/fontmgr.h and win/win_main.h wrap
 *    their calls to the font in portTextBegin/portTextEnd;
 *  - text the game draws that came from Paper Cut: what msgSearch answers
 *    from the port's own tables (pc_* tags, mods' messages), what portTrGame
 *    gives, and copies the game makes of them (portTextCopied). The font's
 *    string functions, the message windows and winFontSet* look the text up
 *    here (portTextIsOwn).
 */

#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* text (and every letter after its start, to its end) is Paper Cut's from
 * now on, while it reads the same; text itself returned */
const char* portTextOwn(const char* text);
/* text, or a pointer into one, is Paper Cut's (as given to portTextOwn and
 * unchanged since) */
int portTextIsOwn(const char* text);
/* copy was made from a or b (either may be NULL): Paper Cut's if either is */
void portTextCopied(const char* copy, const char* a, const char* b);
/* the format, or a %s argument of it, is Paper Cut's (args itself left as it was) */
int portTextFormatOwn(const char* fmt, va_list args);
/* vsprintf, its result Paper Cut's when the format or a %s of it is */
#define PORT_TEXT_VSPRINTF(buf, fmt, args)                                                                            \
    (portTextFormatOwn((fmt), (args)) ? (void)(vsprintf((buf), (fmt), (args)), portTextOwn(buf))                       \
                                      : (void)vsprintf((buf), (fmt), (args)))

/* Paper Cut draws: the game's random numbers saved at the outermost begin,
 * put back at its end (nests) */
void portTextBegin(void);
void portTextEnd(void);
/* inside a begin/end */
int portTextDrawing(void);

/* a call drawing Paper Cut's text, its random numbers put back (an
 * expression; the call's value is dropped) */
#define PORT_TEXT_CALL(call) (portTextBegin(), (void)(call), portTextEnd())

#ifdef __cplusplus
}
#endif

#endif
