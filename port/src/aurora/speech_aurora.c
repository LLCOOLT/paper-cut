/* Text to speech's view of the game (port/settings/features/speech.cpp): the
 * language its text is in (the European release's choice; the others' own). */
#include "mario/mariost.h"

/* 0 English, 1 German, 2 French, 3 Spanish, 4 Italian; -1 unknown (gp->language is msgdrv.c's language_dir
 * index: 0 Japanese, 1 English .. 5 Italian) */
int portSpeechGameLanguage(void) {
    return gp != NULL && gp->language >= 1 && gp->language <= 5 ? (int)gp->language - 1 : -1;
}
