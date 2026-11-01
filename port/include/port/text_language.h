#ifndef PORT_TEXT_LANGUAGE_H
#define PORT_TEXT_LANGUAGE_H

/* The US build's text in another language, from the European disc
 * (port/settings/features/text_language.cpp). */
#ifdef __cplusplus
extern "C" {
#endif

/* Called once this build's disc (at path) is open, before the game reads
 * it: with a language chosen and a European disc in the launcher's library,
 * that disc's message files for it become this disc's (mods' host files). */
void portTextLanguageLoad(const char* path);

#ifdef __cplusplus
}
#endif

#endif
