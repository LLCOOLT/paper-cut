#pragma once
/* The pause menu's Settings tab (port/src/aurora/pause_settings_aurora.c): a
 * sixth tab after the Journal, drawn with the game's own windows and font,
 * holding the most-used settings and "All settings" (the full page). */
#include "win/win_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the tab's id (WinTab.id; the game's are 0-4) */
#define PORT_PAUSE_SETTINGS_TAB 5
/* the Wardrobe's (port/src/aurora/wardrobe_aurora.c): Mario's, the partner's
 * and the status bar's colours, shown on him (and them) as they're picked */
#define PORT_PAUSE_WARDROBE_TAB 6
/* Online's (port/src/aurora/pause_online_aurora.c): online co-op, from the
 * game itself (port/include/port/online_menu.h) */
#define PORT_PAUSE_ONLINE_TAB 7

/* a tab's x in a row of n (the game's five: 105 apart; six: closer) and
 * the labels' scale */
float portPauseTabX(int n, int i);
float portPauseTabScale(int n);
/* headless (port/src/headless/platform/headless.c): Paper Cut's tags that its own code asks for
 * there and only Aurora's sources answer (the tab row's help: src/window/win_root.c; Dizzy Stomp's
 * name: pm64_badges_pc.c), in Aurora's words; NULL for any other (msgSearch) */
const char* portHeadlessMsg(const char* tag);
/* the tab's label at (x, y), in color c (white selected, grey not) */
void portPauseSettingsTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c);
/* a flat box of colour c (its alpha blended) from (x, y) down h and across w, in the menu's 2D
 * camera: a highlight band, a rule (port/src/aurora/pause_pages_aurora.c) */
void portPauseBox(float x, float y, float w, float h, GXColor c);
/* the menu opens; the tab is entered (A); its page each frame (0 open,
 * -1 back to the tabs); its page drawn in menu slot idx */
void portPauseSettingsInit(WindowWork* wp);
void portPauseSettingsInit2(WindowWork* wp);
s32 portPauseSettingsMain(WindowWork* wp);
void portPauseSettingsDisp(s32 camId, WindowWork* wp, s32 idx);
/* Start closes the Start menu from Paper Cut's tabs as from the game's (win_root.c), except where it is
 * taken: a text field's OK (the Multiplayer page's keyboard, a form's text page), a row's question waiting
 * for its second A, a key or button being asked for */
int portPauseStartTaken(void);
int portPauseOnlineTakesStart(void); /* pause_online_aurora.c: its keyboard up, or a row asking */
int portPauseTextUp(void);           /* pause_pages_aurora.c: the text page up */
/* a category's page (its English name: "Multiplayer") opened from another tab's page, which then runs
 * portPauseSettingsMain and portPauseSettingsDisp in its place: -1 from Main is B on it (back to that page,
 * then CloseHosted). Open: 1 opened, 0 no such category */
int portPauseSettingsOpenOn(WindowWork* wp, const char* cat);
void portPauseSettingsCloseHosted(void);
/* text in the game's encoding wrapped for the menu's help box (about 30
 * columns; between words, Japanese ones too), its '<' and '>' made ( ) */
void portPauseWrap(const char* text, char* out, size_t size);
/* text in the game's encoding broken into lines no wider than w at scale s (as FontGetMessageWidth
 * measures): between words (the JP release's full-width space too; inside a word only when it is
 * longer than a line; the text's own line breaks kept). Never cut: the last of max lines takes all
 * that's left (callers make room for every line, or turn pages). The count of lines, each at most
 * PORT_PAUSE_WRAP_LINE - 1 bytes. */
#define PORT_PAUSE_WRAP_LINE 256
int portPauseWrapLines(const char* text, float w, float s, char lines[][PORT_PAUSE_WRAP_LINE], int max);
/* each frame on the port's tabs (win_root.c): a help text longer than the
 * box's two lines turns its page by itself now and then */
void portPauseHelpTurn(WindowWork* wp);
/* msgSearch: the tab's own messages ("pc_menu_..."), or NULL */
const char* portPauseSettingsMsg(const char* tag);

/* the Wardrobe tab: as the Settings tab's above; Exit as the menu closes
 * (its models freed), Msg its "pc_menu_wardrobe..." messages */
void portPauseWardrobeTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c);
void portPauseWardrobeInit(WindowWork* wp);
void portPauseWardrobeInit2(WindowWork* wp);
s32 portPauseWardrobeMain(WindowWork* wp);
void portPauseWardrobeDisp(s32 camId, WindowWork* wp, s32 idx);
void portPauseWardrobeExit(WindowWork* wp);
const char* portPauseWardrobeMsg(const char* tag);

/* the Online tab: as the Wardrobe's above */
void portPauseOnlineTabDisp(WindowWork* wp, float x, float y, float scale, GXColor c);
void portPauseOnlineInit(WindowWork* wp);
void portPauseOnlineInit2(WindowWork* wp);
s32 portPauseOnlineMain(WindowWork* wp);
void portPauseOnlineDisp(s32 camId, WindowWork* wp, s32 idx);
void portPauseOnlineExit(WindowWork* wp);
const char* portPauseOnlineMsg(const char* tag);

#ifdef __cplusplus
}
#endif
