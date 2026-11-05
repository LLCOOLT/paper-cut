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
/* text in the game's encoding wrapped for the menu's help box (about 30
 * columns; between words, Japanese ones too), its '<' and '>' made ( ) */
void portPauseWrap(const char* text, char* out, size_t size);
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
