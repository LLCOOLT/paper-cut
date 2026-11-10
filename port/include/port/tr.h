#pragma once
/* The port's own menus in the player's language (port/settings/tr.cpp):
 * English, Deutsch, Français, Español, Italiano or Japanese, from Language >
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
 * once (the list to translate). $TTYD_TR_MISS=<file>: each text a built-in
 * language has no translation for, once ("de\tpage\t<English>"; game: the
 * game font's, font: translated but with letters the game's font lacks). */
#include <stddef.h>

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
/* printf with the format looked up first (portTr(english), then filled in: a translation keeps
 * the English's conversions in order), so the format is translated, never the formatted text
 * (valid until 16 more calls) */
const char* portTrf(const char* english, ...);
/* each of count English texts in the menu language, for a list drawn at once (Dear ImGui's Combo)
 * (valid until 8 more calls) */
const char* const* portTrList(const char* const* english, int count);
/* the same in the game's encoding, for the game's font (the Start menu's
 * Settings tab): Latin-1 on the US and EU releases, Shift-JIS on the JP one;
 * english where the font can't show the translation */
const char* portTrGame(const char* english);
/* Any UTF-8 text not in the tables (a controller's, a file's or a pack's name) in the game's
 * encoding, into out: Shift-JIS on the JP release where its font has every letter, Latin-1 on the
 * others; else plain ASCII (accents dropped, '?' for the rest). '<' and '>' (the game's tags) become
 * '(' and ')'. */
void portTextForGame(const char* utf8, char* out, size_t size);
/* The other way: text in the game's encoding (a name typed in the Start menu: Latin-1 on the US
 * and EU releases, the JP font's Shift-JIS on the JP one) as UTF-8, for Paper Cut's own pages and
 * the relay; a character the font map doesn't know is a '?'. */
void portTextFromGame(const char* game, char* out, size_t size);
/* Play as (docs/PLAY_AS.md): portTr and portTrGame give the pick's name for
 * Mario's (and Mario's for the pick's), as the game's text does, but in the
 * few texts that keep Mario's (tr.cpp's keepsMario); these two never do */
const char* portTrNoPlayAs(const char* english);
const char* portTrGameNoPlayAs(const char* english);

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
