/* The Journal's "where next?" hint (port/include/port/journal_hint.h): on the
 * Start menu's Journal tab the help box says Y gives a hint; Y shows where
 * the story goes next: the area of the first step of the story past where it
 * is now (story_hints.inc, from include/evt/evt_story.h), named as the game
 * names it in its own language (menu_monosiri_<area>), and with the menus in
 * English the step's name too. Nothing is shown until it is asked for. */
#include "port/journal_hint.h"
#include "port/pause_settings.h"
#include "driver/msgdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mariost.h"
#include "port/tr.h"
#include <stdio.h>
#include <string.h>

typedef struct StoryStep {
    short value;
    const char* area;
    const char* name;
} StoryStep;

#include "story_hints.inc"

/* the first step past value whose area the game names (not the prologue's
 * house, Bowser's interludes or the credits), or NULL at the story's end */
static const StoryStep* nextStep(int value) {
    size_t i;
    for (i = 0; i < sizeof(kStorySteps) / sizeof(kStorySteps[0]); i++) {
        const StoryStep* s = &kStorySteps[i];
        if (s->value > value && strcmp(s->area, "aaa") != 0 && strcmp(s->area, "kpa") != 0 &&
            strcmp(s->area, "end") != 0) {
            return s;
        }
    }
    return NULL;
}

const char* portJournalHintMsg(const char* tag) {
    static char sText[512];
    char raw[512];
    if (strcmp(tag, "pc_menu_kiroku") == 0) {
        /* how to ask for a hint first (the box shows two lines), then the
         * Journal tab's own help */
        portPauseWrap(portTrGame("Y: a hint for where to go next."), raw, sizeof(raw));
        snprintf(sText, sizeof(sText), "%s\n%s", raw, msgSearch("msg_menu_kiroku"));
        return sText;
    }
    if (strcmp(tag, "pc_menu_hint") == 0) {
        const StoryStep* s = gp != NULL ? nextStep(swByteGet(GSW_STORY_PROGRESS)) : NULL;
        char tagArea[40];
        if (s == NULL) {
            portPauseWrap(portTrGame("The story is over: go wherever you like."), sText, sizeof(sText));
            return sText;
        }
        snprintf(tagArea, sizeof(tagArea), "menu_monosiri_%s", s->area);
        if (portTrLanguage() == 0) { /* the step's name is English only */
            snprintf(raw, sizeof(raw), "%s %s (%s)", portTrGame("Where next:"), msgSearch(tagArea), s->name);
        } else {
            snprintf(raw, sizeof(raw), "%s %s", portTrGame("Where next:"), msgSearch(tagArea));
        }
        portPauseWrap(raw, sText, sizeof(sText));
        return sText;
    }
    return NULL;
}
