#ifndef PORT_TEXT_LANGUAGE_H
#define PORT_TEXT_LANGUAGE_H

/* The US build's text in another language, from the European disc
 * (port/settings/features/text_language.cpp). */
#ifdef __cplusplus
extern "C" {
#endif

/* Called once this build's disc (at path) is open, before the game reads
 * it: with a language chosen and a European disc in the launcher's library,
 * that disc's message files for it become this disc's (mods' host files).
 * 0 if the disc at path couldn't be opened again after reading the other
 * (the game then has none: the caller opens it again or fails), else 1. */
int portTextLanguageLoad(const char* path);
/* The European disc's message folder whose text this run shows in msg/US's
 * place ("DE", "FR", "SP", "IT"), or NULL (English, or none was found): mods'
 * messages/<LANG>/ files for that language apply to it (port/mods/mods.cpp). */
const char* portTextLanguageDir(void);

#ifdef __cplusplus
}
#endif

#endif
