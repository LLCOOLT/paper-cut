/* The port's settings (port/include/port/settings.h): settings.txt in
 * Aurora's user folder, read at start, written by the settings page. */
#include <SDL3/SDL.h>
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/settings.h"
#include "port/widescreen.h"
#include "port/gameplay.h"
#include "port/speed.h"
#include "port/layout.h"
#include "port/platform.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
#include <CoreFoundation/CoreFoundation.h>
#define SETTINGS_APP_PREFS 1
#endif

void portInterpSetRate(const char* rate); /* interp_aurora.c */
void salPcSetVolume(float gain);          /* hw_pc.c */

static PortSettings sSettings = {
    "60", /* framerate */
    1,    /* vsync */
    0,    /* fullscreen */
    -1,   /* widescreen: not in the file yet */
    0,    /* resolution */
    0,    /* fps */
    1,    /* noPopIn */
    100,  /* volume */
    0,    /* muteUnfocused */
    0,    /* pauseUnfocused */
    0,    /* backgroundInput */
    1,    /* rumble */
    100,  /* rumbleStrength */
    1,    /* phoneRumble */
    1,    /* touchHaptics */
    100,  /* touchOpacity */
    100,  /* touchSize */
    0,    /* autoRun */
    0,    /* stickFixed */
    0,    /* tapA */
    0,    /* orientation */
    0,    /* safeArea */
    3,    /* ffSpeed */
    0,    /* ffToggle */
    0,    /* textSpeed */
    0,    /* skipIntro */
    100,  /* runSpeed */
};
static char sUserPath[1024];
static char sPath[1100];
static int sFocused = 1;

typedef struct {
    const char* key;
    int* value; /* NULL: the frame rate, a string */
    int min, max;
} Field;

static const Field kFields[] = {
    {"framerate", NULL, 0, 0},
    {"vsync", &sSettings.vsync, 0, 1},
    {"fullscreen", &sSettings.fullscreen, 0, 1},
    {"widescreen", &sSettings.widescreen, 0, 1},
    {"resolution", &sSettings.resolution, 0, 8},
    {"fps", &sSettings.fps, 0, 4},
    {"no_pop_in", &sSettings.noPopIn, 0, 1},
    {"volume", &sSettings.volume, 0, 100},
    {"mute_unfocused", &sSettings.muteUnfocused, 0, 1},
    {"pause_unfocused", &sSettings.pauseUnfocused, 0, 1},
    {"background_input", &sSettings.backgroundInput, 0, 1},
    {"rumble", &sSettings.rumble, 0, 1},
    {"rumble_strength", &sSettings.rumbleStrength, 0, 100},
    {"phone_rumble", &sSettings.phoneRumble, 0, 1},
    {"touch_haptics", &sSettings.touchHaptics, 0, 1},
    {"touch_opacity", &sSettings.touchOpacity, 20, 100},
    {"touch_size", &sSettings.touchSize, 70, 150},
    {"auto_run", &sSettings.autoRun, 0, 1},
    {"stick_fixed", &sSettings.stickFixed, 0, 1},
    {"tap_a", &sSettings.tapA, 0, 1},
    {"orientation", &sSettings.orientation, 0, 2},
    {"safe_area", &sSettings.safeArea, 0, 1},
    {"ff_speed", &sSettings.ffSpeed, 2, 8},
    {"ff_toggle", &sSettings.ffToggle, 0, 1},
    {"text_speed", &sSettings.textSpeed, 0, 3},
    {"skip_intro", &sSettings.skipIntro, 0, 1},
    {"run_speed", &sSettings.runSpeed, 100, 150},
    {"dump_textures", &sSettings.dumpTextures, 0, 1},
};
#define FIELD_COUNT (sizeof(kFields) / sizeof(kFields[0]))

PortSettings* portSettings(void) {
    return &sSettings;
}

const char* portSettingsUserPath(void) {
    return sUserPath;
}

static void set(const Field* f, const char* value) {
    if (f->value == NULL) {
        snprintf(sSettings.framerate, sizeof(sSettings.framerate), "%s", value);
    } else {
        int v = atoi(value);
        *f->value = v < f->min ? f->min : v > f->max ? f->max : v;
    }
}

#ifdef SETTINGS_APP_PREFS
/* iOS: the Settings app's page (port/ios/Settings.bundle) uses the same keys.
 * Its values override settings.txt at launch and when the game comes back to
 * the foreground; the in-game page's saves are written back to it. */
static int isToggle(const char* key) {
    return strcmp(key, "vsync") == 0 || strcmp(key, "fullscreen") == 0 || strcmp(key, "widescreen") == 0 ||
           strcmp(key, "mute_unfocused") == 0 || strcmp(key, "pause_unfocused") == 0 ||
           strcmp(key, "background_input") == 0 || strcmp(key, "rumble") == 0 ||
           strcmp(key, "phone_rumble") == 0 || strcmp(key, "touch_haptics") == 0 ||
           strcmp(key, "auto_run") == 0 || strcmp(key, "stick_fixed") == 0 || strcmp(key, "tap_a") == 0 ||
           strcmp(key, "safe_area") == 0 || strcmp(key, "no_pop_in") == 0 || strcmp(key, "ff_toggle") == 0 || strcmp(key, "skip_intro") == 0;
}

static int syncFromPreferences(void);

static void writePreferences(void);
#endif

static const Field* field(const char* key) {
    size_t i;
    for (i = 0; i < FIELD_COUNT; i++) {
        if (strcmp(key, kFields[i].key) == 0) {
            return &kFields[i];
        }
    }
    return NULL;
}

/* Crash reports (port/src/common/crash_pc.c): written to crash_report.txt;
 * one found at start is the last run's, kept as last_crash.txt and offered
 * for copying by the settings page. */
static char sLastCrash[1100];
static int sCrashPending;

static void crashReportInit(void) {
    char current[1100];
    snprintf(current, sizeof(current), "%s/crash_report.txt", sUserPath);
    snprintf(sLastCrash, sizeof(sLastCrash), "%s/last_crash.txt", sUserPath);
    if (SDL_GetPathInfo(current, NULL)) {
        SDL_RemovePath(sLastCrash);
        if (SDL_RenamePath(current, sLastCrash)) {
            sCrashPending = 1;
        }
    }
    portCrashSetReportPath(current);
}

const char* portCrashReportLast(void) {
    return SDL_GetPathInfo(sLastCrash, NULL) ? sLastCrash : NULL;
}

int portCrashReportPending(void) {
    return sCrashPending;
}

void portCrashReportDismiss(void) {
    sCrashPending = 0;
}

int portSettingsPeek(const char* userPath, const char* key) {
    char path[1100];
    char* pref = NULL;
    char* text;
    int value = 0;
    size_t keyLen = strlen(key);
    if (userPath == NULL) {
        pref = SDL_GetPrefPath(NULL, "ttyd"); /* Aurora's default (appName "ttyd") */
        userPath = pref != NULL ? pref : ".";
    }
    snprintf(path, sizeof(path), "%s/settings.txt", userPath);
    SDL_free(pref);
    text = SDL_LoadFile(path, NULL);
    if (text != NULL) {
        const char* line = text;
        while (*line != '\0') {
            if (strncmp(line, key, keyLen) == 0 && line[keyLen] == '=') {
                value = atoi(line + keyLen + 1);
            }
            line += strcspn(line, "\r\n");
            line += strspn(line, "\r\n");
        }
        SDL_free(text);
    }
    return value;
}

void portSettingsLoad(const char* userPath) {
    char* text;
    snprintf(sUserPath, sizeof(sUserPath), "%s", userPath != NULL ? userPath : ".");
    snprintf(sPath, sizeof(sPath), "%s/settings.txt", sUserPath);
    crashReportInit();
    text = SDL_LoadFile(sPath, NULL);
    if (text != NULL) {
        char* line = text;
        while (*line != '\0') {
            char* end = line + strcspn(line, "\r\n");
            char* eq = strchr(line, '=');
            char saved = *end;
            *end = '\0';
            if (eq != NULL && eq < end) {
                const Field* f;
                *eq = '\0';
                if ((f = field(line)) != NULL) {
                    set(f, eq + 1);
                }
            }
            *end = saved;
            line = end + strspn(end, "\r\n");
        }
        SDL_free(text);
    }
#ifdef SETTINGS_APP_PREFS
    syncFromPreferences();
#endif
    /* this run's overrides */
    if (getenv("TTYD_FRAMERATE") != NULL) {
        set(field("framerate"), getenv("TTYD_FRAMERATE"));
    }
    if (getenv("TTYD_PAUSE_UNFOCUSED") != NULL) {
        sSettings.pauseUnfocused = 1;
    }
    if (sSettings.widescreen < 0 || getenv("TTYD_WIDESCREEN") != NULL) {
        sSettings.widescreen = portWidescreenEnabled(); /* the variable, or Apple's preference */
    }
    portSettingsApply();
}

void portSettingsSave(void) {
    SDL_IOStream* io;
    size_t i;
    portSettingsApply();
    io = SDL_IOFromFile(sPath, "w");
    if (io == NULL) {
        return;
    }
    for (i = 0; i < FIELD_COUNT; i++) {
        if (kFields[i].value == NULL) {
            SDL_IOprintf(io, "%s=%s\n", kFields[i].key, sSettings.framerate);
        } else {
            SDL_IOprintf(io, "%s=%d\n", kFields[i].key, *kFields[i].value);
        }
    }
    SDL_CloseIO(io);
#ifdef SETTINGS_APP_PREFS
    writePreferences();
#endif
}

void portSettingsApply(void) {
    static int sFullscreen = -1, sResolution = -1;
    portInterpSetRate(sSettings.framerate);
    aurora_enable_vsync(portSpeedVsync(sSettings.vsync != 0));
    if (sFullscreen != sSettings.fullscreen) {
        /* only on a change: the window's own toggles (F11) stay in charge */
        if (sFullscreen >= 0 || sSettings.fullscreen) {
            VISetWindowFullscreen(sSettings.fullscreen != 0);
        }
        sFullscreen = sSettings.fullscreen;
    }
    if (sResolution != sSettings.resolution) {
        VISetFrameBufferScale((float)sSettings.resolution);
        sResolution = sSettings.resolution;
    }
    portWidescreenSet(sSettings.widescreen);
    portLayoutOrientation(sSettings.orientation);
    /* $TTYD_FAST (tests) always waits, so every frame is complete */
    AuroraSetDrawsBlocking(sSettings.noPopIn != 0 || getenv("TTYD_FAST") != NULL);
    {
        static const int kTextSpeeds[] = {100, 200, 400, 10000};
        gPortTextSpeed = kTextSpeeds[sSettings.textSpeed];
        gPortSkipIntro = sSettings.skipIntro;
        gPortRunSpeed = sSettings.runSpeed / 100.0f;
    }
    aurora_set_pause_on_focus_lost(sSettings.pauseUnfocused != 0);
    aurora_set_background_input(sSettings.backgroundInput != 0);
    salPcSetVolume(sFocused || !sSettings.muteUnfocused ? sSettings.volume / 100.0f : 0.0f);
    {
        u32 port;
        const u16 level = (u16)(0xFFFFu * (u32)sSettings.rumbleStrength / 100u);
        for (port = 0; port < 4; port++) {
            PADSetRumbleIntensity(port, level, level);
        }
    }
}

#undef PADControlMotor
void portPADControlMotor(unsigned int chan, unsigned int cmd) {
    if (cmd == PAD_MOTOR_RUMBLE) {
        int pads = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&pads);
        SDL_free(ids);
        if (!sSettings.rumble || sSettings.rumbleStrength == 0 || (pads == 0 && !sSettings.phoneRumble)) {
            cmd = PAD_MOTOR_STOP;
        }
    }
    PADControlMotor(chan, cmd);
}

void portSettingsFocus(int focused) {
#ifdef SETTINGS_APP_PREFS
    if (focused && !sFocused && syncFromPreferences()) {
        portSettingsApply(); /* changed in the Settings app while away */
        portSettingsSave();
    }
#endif
    if (sFocused != focused) {
        sFocused = focused;
        salPcSetVolume(sFocused || !sSettings.muteUnfocused ? sSettings.volume / 100.0f : 0.0f);
    }
}

#ifdef SETTINGS_APP_PREFS
/* Returns 1 if a value changed. */
static int syncFromPreferences(void) {
    size_t i;
    int changed = 0;
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
    for (i = 0; i < FIELD_COUNT; i++) {
        CFStringRef key = CFStringCreateWithCString(NULL, kFields[i].key, kCFStringEncodingUTF8);
        CFPropertyListRef v = CFPreferencesCopyAppValue(key, kCFPreferencesCurrentApplication);
        char text[32] = "";
        CFRelease(key);
        if (v == NULL) {
            continue;
        }
        if (CFGetTypeID(v) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)v, text, sizeof(text), kCFStringEncodingUTF8);
        } else if (CFGetTypeID(v) == CFBooleanGetTypeID()) {
            snprintf(text, sizeof(text), "%d", CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0);
        } else if (CFGetTypeID(v) == CFNumberGetTypeID()) {
            double d = 0;
            CFNumberGetValue((CFNumberRef)v, kCFNumberDoubleType, &d);
            snprintf(text, sizeof(text), "%d", (int)(d + 0.5));
        }
        CFRelease(v);
        if (text[0] != '\0') {
            char before[32];
            if (kFields[i].value == NULL) {
                snprintf(before, sizeof(before), "%s", sSettings.framerate);
            } else {
                snprintf(before, sizeof(before), "%d", *kFields[i].value);
            }
            set(&kFields[i], text);
            if (kFields[i].value == NULL ? strcmp(before, sSettings.framerate) != 0
                                         : atoi(before) != *kFields[i].value) {
                changed = 1;
            }
        }
    }
    return changed;
}

static void writePreferences(void) {
    size_t i;
    for (i = 0; i < FIELD_COUNT; i++) {
        CFStringRef key = CFStringCreateWithCString(NULL, kFields[i].key, kCFStringEncodingUTF8);
        CFPropertyListRef v;
        if (kFields[i].value == NULL) {
            v = CFStringCreateWithCString(NULL, sSettings.framerate, kCFStringEncodingUTF8);
        } else if (isToggle(kFields[i].key)) {
            v = CFRetain(*kFields[i].value ? kCFBooleanTrue : kCFBooleanFalse);
        } else {
            int n = *kFields[i].value;
            v = CFNumberCreate(NULL, kCFNumberIntType, &n);
        }
        CFPreferencesSetAppValue(key, v, kCFPreferencesCurrentApplication);
        CFRelease(v);
        CFRelease(key);
    }
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
}
#endif
