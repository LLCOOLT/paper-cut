/* Text to speech's view of the game (port/settings/features/speech.cpp): the
 * language its text is in (the European release's choice; the others' own). */
#include "mario/mariost.h"

/* 0 English, 1 German, 2 French, 3 Spanish, 4 Italian (gp->language); -1 unknown */
int portSpeechGameLanguage(void) {
    return gp != NULL ? (int)gp->language : -1;
}
