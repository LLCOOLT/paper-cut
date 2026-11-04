#pragma once
/* The port's own menus in the player's language (port/settings/tr.cpp):
 * English, Deutsch, Français, Español, Italiano or Japanese, from System >
 * Menu language (Auto: the JP release's Japanese, the EU release's text
 * language, otherwise the computer's). The translations are kept by their
 * English text (port/assets/lang/<code>.txt, tools/port/gen_lang.py), so a
 * text with none stays English.
 *
 * Language packs (docs/LANGUAGE_PACKS.md): <user>/mods/<pack>/lang/ holds
 * pack.txt (name=, code=, for=menus, font=, fallback=) and .txt files in the
 * same format, read at the start (and on Reload): a new language in the
 * choice, or one's texts changed (code=de); a pack's text first, then the
 * built-in language's, then English.
 *
 * $TTYD_TR_DUMP=<file>: each English text asked for is added to the file
 * once (the list to translate). */
#ifdef __cplusplus
extern "C" {
#endif

/* the menu language: 0 English, 1 Deutsch, 2 Français, 3 Español, 4 Italiano, 5 Japanese,
 * 6 and on the language packs' own languages */
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

/* The language packs read (port/settings/features/lang_packs.cpp shows them). */
typedef struct PortTrPackInfo {
    const char* folder; /* its folder in the mods folder */
    const char* name;   /* pack.txt name= */
    const char* code;   /* pack.txt code= */
    int adds;           /* 1: a language of its own; 0: changes a built-in one's texts */
    int strings;        /* texts it translates (and uses) */
    int untranslated;   /* English lines with no translation yet */
    const char* problems; /* what is wrong, a line each, or "" */
} PortTrPackInfo;
/* the user folder, before settings.txt is read (main_aurora.c: before the fonts) */
void portTrInit(const char* userPath);
int portTrPackCount(void);
const PortTrPackInfo* portTrPack(int i);
/* read them again (the texts at once; letters new to the fonts from the next start) */
void portTrReloadPacks(void);
/* the packs have letters the page's fonts were not given (a restart shows them) */
int portTrPacksNeedRestart(void);
/* <user>/mods/my-language/lang/strings.txt: every English text to translate,
 * an empty line under each, and pack.txt; an existing one only gets the texts
 * it lacks. Its path (or NULL), and how many texts were added. */
const char* portTrWriteTemplate(int* added);
/* before settings.txt is written (settings_aurora.c): the pack language kept by its code */
void portTrSettingsSync(void);

#ifdef __cplusplus
}
#endif
