#ifndef PORT_TITLE_MENU_H
#define PORT_TITLE_MENU_H
/* The title menu (port/src/aurora/title_menu_aurora.c, docs/PC_PORT.md): once
 * "Press Start" is pressed, a row of four in the game's windows instead of
 * the file select at once: Play (the file select, as before), Modes, Online
 * and Settings, before any save file is loaded.
 *
 * - Settings and Online open the Start menu's own tabs over the title, with
 *   no file (scope PORT_TITLE_NO_FILE): the same book pages, the rows that
 *   need a file in play greyed with a note.
 * - Modes starts a temporary file (scope PORT_TITLE_TEMP_FILE): a new file in
 *   memory, never saved, in Rogueport's Westside with the Tattle Log lent,
 *   the Start menu opened there on the Modes hub with only the modes that
 *   don't need the player's own file. Each mode starts as from the Start
 *   menu; when it's over (or the hub is left), back to the title.
 *
 * Off (the game's own Start: straight to the file select) online and in
 * replays, so a session's games stay in step as before, in tests with an
 * input script, and with the setting "Title menu" off. */
#ifdef __cplusplus
extern "C" {
#endif

enum { PORT_TITLE_NONE = 0, PORT_TITLE_NO_FILE = 1, PORT_TITLE_TEMP_FILE = 2 };

/* seq_title.c, each frame while "Press Start" is up (state 3, past its
 * first second): 0 the game's own (no menu), 1 the menu has the frame,
 * 2 Play was chosen (on to the file select, as A did). */
int portTitleMenuMain(void);
/* The Start menu's scope now (win_root.c, pause_settings_aurora.c). */
int portTitleScope(void);
/* A temporary file is in play: never saved, nor counted (autosave, quick
 * resume, the Records, achievements). */
int portTitleTempFile(void);
/* win_main.c, with the Start menu closed and Mario free: open it now on the
 * Settings tab (the temporary file's first moment in the room). */
int portTitleModesPoll(void);
/* pause_settings_aurora.c: B on the temporary file's Modes hub (the menu
 * closes): back to the title at once. */
void portTitleModesBack(void);
/* pause_settings_aurora.c: the Settings tab straight on the Modes hub; and
 * the title menu's buttons, painted as the Settings tab's front page's (the
 * Journal's) at s times their size, with one or two of the game's icons
 * (-1 none) and a word in the game's encoding, at alpha a; wp: the menu's
 * work, win.tpl loaded. */
void portPauseSettingsOpenModes(void);
struct WindowWork;
void portPauseTitleButton(struct WindowWork* wp, float x, float y, float s, const char* name, int icon, int icon2,
                          int selected, int a);
/* At start (modes_aurora.c): the setting and the frame hook. */
void portTitleMenuInit(void);

#ifdef __cplusplus
}
#endif
#endif
