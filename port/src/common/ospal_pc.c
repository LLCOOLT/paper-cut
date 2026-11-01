/* The GameCube SRAM settings the European release reads at start
 * (marioStInit, seq_logo): the console's language, which picks the game's
 * text (msg/<language>), and the EURGB60 flag, which preselects 60 Hz in its
 * 50/60 Hz prompt. The language is $TTYD_LANGUAGE (en de fr es it), else the
 * settings page's (Aurora, from the next start), else the computer's. */
#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "mario/mariost.h"
#include "port/platform.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/settings.h"
#endif

/* OS_LANG_* for a language code ("de", "de_DE.UTF-8"); -1 if not one */
static int langFromCode(const char* code) {
    static const struct {
        const char* code;
        int lang;
    } kLangs[] = {{"en", OS_LANG_ENGLISH}, {"de", OS_LANG_GERMAN},  {"fr", OS_LANG_FRENCH},
                  {"es", OS_LANG_SPANISH}, {"it", OS_LANG_ITALIAN}, {"nl", OS_LANG_DUTCH}};
    size_t i;
    if (code == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(kLangs) / sizeof(kLangs[0]); i++) {
        if (strncmp(code, kLangs[i].code, 2) == 0) {
            return kLangs[i].lang;
        }
    }
    return -1;
}

static int hostLanguage(void) {
#ifdef _WIN32
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_GERMAN:
            return OS_LANG_GERMAN;
        case LANG_FRENCH:
            return OS_LANG_FRENCH;
        case LANG_SPANISH:
            return OS_LANG_SPANISH;
        case LANG_ITALIAN:
            return OS_LANG_ITALIAN;
        case LANG_DUTCH:
            return OS_LANG_DUTCH;
        default:
            return OS_LANG_ENGLISH;
    }
#else
    const char* vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    size_t i;
    for (i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        int lang = langFromCode(getenv(vars[i]));
        if (lang >= 0) {
            return lang;
        }
    }
    return OS_LANG_ENGLISH;
#endif
}

u8 OSGetLanguage(void) {
    int lang = langFromCode(getenv("TTYD_LANGUAGE"));
    if (lang >= 0) {
        return (u8)lang;
    }
#ifdef TTYD_BACKEND_AURORA
    /* the settings page: 0 the computer's, 1-5 English German French Spanish Italian */
    if (portSettings()->language > 0) {
        return (u8)(portSettings()->language - 1);
    }
#endif
    return (u8)hostLanguage();
}

int portPal50Hz(void) {
    const char* v = getenv("TTYD_50HZ");
    if (v != NULL) {
        return atoi(v) != 0;
    }
#ifdef TTYD_BACKEND_AURORA
    return portSettings()->pal50Hz;
#else
    return 0;
#endif
}

/* The video field rate the game runs at: 50 when the European release was
 * answered 50 Hz in its prompt (gp->framerate), else 60. The frame pacing,
 * the audio rendered per frame and the fast-run clock follow it. */
int portFieldRate(void) {
    return gp != NULL && gp->framerate == 50 ? 50 : 60;
}

/* On: 60 Hz (EURGB60), which the prompt then preselects. The game sets it
 * from the player's answer; the PC keeps it for the run. */
static u32 sEuRgb60 = 1;

u32 OSGetEuRgb60Mode(void) {
    return sEuRgb60;
}

void OSSetEuRgb60Mode(u32 on) {
    sEuRgb60 = on;
}
