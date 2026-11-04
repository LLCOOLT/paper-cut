#ifndef PORT_SPEECH_H
#define PORT_SPEECH_H

/* Text to speech (Accessibility > Read text aloud;
 * port/settings/features/speech.cpp): the game's message windows read page by
 * page as they show, and the Start menu's help line and Settings rows as the
 * cursor moves, by the system's own voice (Windows' SAPI, Apple's
 * AVSpeechSynthesizer, Android's TextToSpeech, Linux's speech-dispatcher).
 * A new page, a closed window or a new menu line cuts off what was being said. */

#ifdef __cplusplus
extern "C" {
#endif

/* msgdrv.c: a message window opened with its text (the game's own, tags and
 * all), more text added to it, the page it shows now, and its closing. */
void portSpeechMessage(const void* window, const char* text);
void portSpeechAppend(const void* window, const char* text);
void portSpeechPage(const void* window, int page);
void portSpeechClosed(const void* window);
/* win_root.c, pause_settings_aurora.c: a menu's help line (the game's text,
 * or the port's English), read when it changes */
void portSpeechMenu(const char* text);

#ifdef __cplusplus
}
#endif

#endif
