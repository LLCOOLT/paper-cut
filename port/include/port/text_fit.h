#pragma once
/* Tests: does the port's text fit its box? (port/src/common/text_fit_pc.c)
 *
 * $TTYD_TEXT_FIT=<file>: wherever the port draws its own text into a box (the
 * Start menu's rows, buttons and help box, the title menu's band, the versus
 * set-up, What's new, the recap, notices, battle text), the text's measured
 * width at the scale it wants is compared with the room it has, and each text
 * that doesn't fit is written to the file once: a tab-separated line of
 *   kind (over: drawn past the box; squeeze: narrowed to fit, below 80% of its
 *   width; lines: more lines than the box shows; english: shown in English, the
 *   game's font having no translation it can show), menu language, disc, screen,
 *   where (the drawing site), how much of it fits (room / width), the width,
 *   the room, the English text it came from (its key, when known) and the text
 *   as drawn (in the game's encoding: Latin-1, or Shift-JIS on the JP disc).
 * Off by default; nothing is measured or kept without it. Draw-only: it never
 * touches the game's state or its random numbers. */
#ifdef __cplusplus
extern "C" {
#endif

/* A line narrowed to fit its box (kx of scale s across) keeps its letters' shape: no thinner than
 * PORT_TEXT_SQUEEZE of their height; past that the whole line is smaller (its height follows kx), never
 * squashed into slivers. Long text wraps instead (port/include/port/pause_settings.h's
 * portPauseWrapLines): this is for a label or a value of one line. The height to draw with: */
#define PORT_TEXT_SQUEEZE 0.8f
static inline float portTextFitHeight(float kx, float s) {
    return kx < s * PORT_TEXT_SQUEEZE ? kx / PORT_TEXT_SQUEEZE : s;
}

/* $TTYD_TEXT_FIT is set */
int portTextFitOn(void);
/* the screen being drawn, for the lines that follow ("Settings/Display", "Title menu") */
void portTextFitScreen(const char* screen);
/* text (drawn) wants width at its scale and has room: logged when it doesn't fit; shrunk: the site
 * narrows it to fit (a squeeze), else it overflows */
void portTextFit(const char* where, const char* text, float width, float room, int shrunk);
/* text shows lines of the box's most */
void portTextFitLines(const char* where, const char* text, int lines, int most);
/* the help box (win_root.c): its note's lines against its width, for the port's tags ("pc_...") */
void portTextFitHelp(const char* tag, const char* text);
/* (port/settings/tr.cpp) english drawn with the game's font as it is, in a menu language that isn't
 * English: no translation, or one that font can't show (kind "english") */
void portTextFitEnglish(const char* english, int translated);
/* (port/settings/tr.cpp) shown is english's translation, for the log's key */
void portTextFitKey(const char* english, const char* shown);

#ifdef __cplusplus
}
#endif
