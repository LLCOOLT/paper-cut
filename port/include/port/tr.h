#pragma once
/* The port's own menus in the player's language (port/settings/tr.cpp):
 * English, Deutsch, Français, Español, Italiano or Japanese, from System >
 * Menu language (Auto: the JP release's Japanese, the EU release's text
 * language, otherwise the computer's). The translations are kept by their
 * English text (port/assets/lang/<code>.txt, tools/port/gen_lang.py), so a
 * text with none stays English.
 *
 * $TTYD_TR_DUMP=<file>: each English text asked for is added to the file
 * once (the list to translate). */
#ifdef __cplusplus
extern "C" {
#endif

/* the menu language: 0 English, 1 Deutsch, 2 Français, 3 Español, 4 Italiano, 5 Japanese */
int portTrLanguage(void);
/* english in the menu language (UTF-8); english itself without a translation */
const char* portTr(const char* english);
/* a Dear ImGui label ("Text" or "Text##id") in the menu language, its English
 * kept as the ID ("###"), so a control is the same control in any language
 * (valid until 16 more calls) */
const char* portTrLabel(const char* label);
/* the same in the game's encoding, for the game's font (the Start menu's
 * Settings tab): Latin-1 on the US and EU releases, Shift-JIS on the JP one;
 * english where the font can't show the translation */
const char* portTrGame(const char* english);

#ifdef __cplusplus
}
#endif
