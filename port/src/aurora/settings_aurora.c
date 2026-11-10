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
#include "port/tr.h"
#include "port/widescreen.h"
#include "port/ac_cue.h"
#include "port/cloud.h"
#include "manager/cardmgr.h"
#include "port/gameplay.h"
#include "port/speed.h"
#include "port/toggles.h"
#include "port/layout.h"
#include "port/platform.h"
#include "port/audio_mix.h"
#include "port/scene_skip.h"
#include "port/handheld.h"
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
    1,    /* overlayFps */
    0,    /* overlayClock */
    0,    /* overlayBattery */
    1,    /* noPopIn */
    0,    /* texFilter */
    0,    /* msaa */
    0,    /* sideBars */
    1,    /* wideHud: the HUD at a wide frame's edges */
    100,  /* volume */
    0,    /* muteUnfocused */
    0,    /* pauseUnfocused */
    0,    /* backgroundInput */
    0,    /* cloudSync */
    1,    /* rumble */
    100,  /* rumbleStrength */
    1,    /* phoneRumble */
    1,    /* touchHaptics */
    100,  /* touchOpacity */
    100,  /* touchSize */
    0,    /* autoRun */
    0,    /* stickFixed */
    0,    /* tapA */
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    1,    /* orientation: landscape on phones and tablets (Settings > Display turns it) */
#else
    0,    /* orientation */
#endif
    0,    /* safeArea */
    3,    /* ffSpeed */
    0,    /* ffToggle */
    0,    /* textSpeed */
    0,    /* skipIntro */
    100,  /* runSpeed */
    0,    /* acCue */
    0,    /* language */
    0,    /* pal50Hz */
    0,    /* launcherSkip */
    0,    /* dumpTextures */
};
static char sUserPath[1024];
static const char* cardRegion(void);
void portSettingsCloudPush(int wait);
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
    {"fullscreen", &sSettings.fullscreen, 0, 2}, /* 0 a window, 1 borderless, 2 full screen (window.cpp) */
    {"widescreen", &sSettings.widescreen, 0, 1},
    {"resolution", &sSettings.resolution, 0, 8},
    {"fps", &sSettings.fps, 0, 4},
    {"overlay_fps", &sSettings.overlayFps, 0, 1},
    {"overlay_clock", &sSettings.overlayClock, 0, 1},
    {"overlay_battery", &sSettings.overlayBattery, 0, 1},
    {"no_pop_in", &sSettings.noPopIn, 0, 1},
    {"tex_filter", &sSettings.texFilter, 0, 1},
    {"msaa", &sSettings.msaa, 0, 1},
    {"side_bars", &sSettings.sideBars, 0, 1},
    {"wide_hud", &sSettings.wideHud, 0, 1},
    {"volume", &sSettings.volume, 0, 100},
    {"mute_unfocused", &sSettings.muteUnfocused, 0, 1},
    {"pause_unfocused", &sSettings.pauseUnfocused, 0, 1},
    {"background_input", &sSettings.backgroundInput, 0, 1},
    {"cloud_sync", &sSettings.cloudSync, 0, 1},
    {"rumble", &sSettings.rumble, 0, 1},
    {"rumble_strength", &sSettings.rumbleStrength, 0, 100},
    {"phone_rumble", &sSettings.phoneRumble, 0, 1},
    {"touch_haptics", &sSettings.touchHaptics, 0, 1},
    {"touch_opacity", &sSettings.touchOpacity, 20, 100},
    {"touch_size", &sSettings.touchSize, 50, 200},
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
    {"ac_cue", &sSettings.acCue, 0, 2},
    {"language", &sSettings.language, 0, 5},
    {"pal_50hz", &sSettings.pal50Hz, 0, 1},
    {"launcher_skip", &sSettings.launcherSkip, 0, 1},
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
           strcmp(key, "safe_area") == 0 || strcmp(key, "no_pop_in") == 0 || strcmp(key, "msaa") == 0 || strcmp(key, "cloud_sync") == 0 || strcmp(key, "overlay_fps") == 0 ||
           strcmp(key, "overlay_clock") == 0 || strcmp(key, "overlay_battery") == 0 ||
           strcmp(key, "side_bars") == 0 || strcmp(key, "wide_hud") == 0 || strcmp(key, "ff_toggle") == 0 || strcmp(key, "skip_intro") == 0 || strcmp(key, "pal_50hz") == 0 ||
           strcmp(key, "launcher_skip") == 0;
}

static int syncFromPreferences(void);

static void writePreferences(void);
#endif

/* the settings changed for this run only ($TTYD_SET_<key>, portSettingsSetForRun), their saved values and the
 * run's: settings.txt keeps the saved one while the setting is still at the run's (the player changing it makes
 * their value theirs, saved from then on). The frame rate, a string, alike. */
static char sOverridden[FIELD_COUNT];
static int sFileValue[FIELD_COUNT];
static int sRunValue[FIELD_COUNT];
static char sFramerateOverridden;
static char sFramerateFile[16];
static char sFramerateRun[16];

/* what settings.txt (and the iOS Settings app) gets for field i */
static int savedValue(size_t i) {
    if (sOverridden[i] && *kFields[i].value != sRunValue[i]) {
        sOverridden[i] = 0; /* (the player's own now) */
    }
    return sOverridden[i] ? sFileValue[i] : *kFields[i].value;
}

static const char* savedFramerate(void) {
    if (sFramerateOverridden && strcmp(sSettings.framerate, sFramerateRun) != 0) {
        sFramerateOverridden = 0;
    }
    return sFramerateOverridden ? sFramerateFile : sSettings.framerate;
}

/* tests: $TTYD_SETTINGS_DUMP's fixed fields (port/src/common/settings_extra.c) */
void portSettingsDumpFields(void* file) {
    size_t i;
    for (i = 0; i < FIELD_COUNT; i++) {
        if (kFields[i].value != NULL) {
            fprintf((FILE*)file, "field\t%s\t%d\t%d\t%d\n", kFields[i].key, *kFields[i].value, kFields[i].min,
                    kFields[i].max);
        }
    }
}

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
 * one found at start is the last run's, kept as last_crash.txt, shown once
 * by the next start's recovery screen (crash_report.cpp) and offered for
 * copying by the settings page. Only a report counts: a run killed or a
 * power cut writes none, so it brings no screen (quick resume's Continue is
 * on the launcher as ever), and a phone's system ending an app in the
 * background is never called a crash. */
static char sLastCrash[1100];
static char sCrashDir[1024];
static int sCrashPending;

static void crashReportInit(const char* dir) {
    char current[1100], norm[1024];
    size_t n;
    snprintf(norm, sizeof(norm), "%s", dir);
    n = strlen(norm);
    while (n > 1 && (norm[n - 1] == '/' || norm[n - 1] == '\\')) {
        norm[--n] = '\0';
    }
    if (strcmp(norm, sCrashDir) == 0) {
        return; /* (portCrashReportEarly found it already: the same folder) */
    }
    snprintf(sCrashDir, sizeof(sCrashDir), "%s", norm);
    snprintf(current, sizeof(current), "%s/crash_report.txt", norm);
    snprintf(sLastCrash, sizeof(sLastCrash), "%s/last_crash.txt", norm);
    if (SDL_GetPathInfo(current, NULL)) {
        SDL_RemovePath(sLastCrash);
        if (SDL_RenamePath(current, sLastCrash)) {
            sCrashPending = 1;
            printf("[crash] the last run's report kept as last_crash.txt\n");
            fflush(stdout);
        }
    }
    portCrashSetReportPath(current);
}

/* main_aurora.c, before portCrashInit and aurora_initialize: the report's place from the start, so a crash
 * in Aurora's start (the graphics adapter, its driver) has one too. userPath: Aurora's (NULL: its default) */
void portCrashReportEarly(const char* userPath) {
    char* pref = NULL;
    if (userPath == NULL) {
        pref = SDL_GetPrefPath(NULL, "ttyd"); /* Aurora's default (its appName) */
        userPath = pref;
    }
    if (userPath != NULL && userPath[0] != '\0') {
        crashReportInit(userPath);
    }
    SDL_free(pref);
}

const char* portCrashReportLast(void) {
    return SDL_GetPathInfo(sLastCrash, NULL) ? sLastCrash : NULL;
}

int portCrashReportPending(void) {
    /* (Crash notice at start off, crash_report.cpp: the report kept for the settings page only) */
    const int* notice = sCrashPending ? portSettingsFind("crash_notice") : NULL;
    return sCrashPending && (notice == NULL || *notice != 0);
}

void portCrashReportDismiss(void) {
    sCrashPending = 0;
}

/* key=value lines (settings.txt's), each set: a fixed field or a feature's own */
static void readLines(char* text) {
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
            } else {
                portSettingsExtraSet(line, eq + 1); /* a feature's own (settings_extra.c) */
            }
        }
        *end = saved;
        line = end + strspn(end, "\r\n");
    }
}

int portSteamDeck(void) {
    const char* test = getenv("TTYD_STEAM_DECK");
    if (test != NULL) {
        return atoi(test) != 0;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    {
        /* Steam sets SteamDeck=1 for what it starts on one; started otherwise
         * (desktop mode), the board says: Valve's Jupiter (LCD) or Galileo (OLED) */
        const char* steam = getenv("SteamDeck");
        char* vendor;
        char* product;
        int deck;
        if (steam != NULL && atoi(steam) != 0) {
            return 1;
        }
        vendor = SDL_LoadFile("/sys/devices/virtual/dmi/id/board_vendor", NULL);
        product = SDL_LoadFile("/sys/devices/virtual/dmi/id/product_name", NULL);
        deck = vendor != NULL && product != NULL && strncmp(vendor, "Valve", 5) == 0 &&
               (strncmp(product, "Jupiter", 7) == 0 || strncmp(product, "Galileo", 7) == 0);
        SDL_free(vendor);
        SDL_free(product);
        return deck;
    }
#else
    return 0;
#endif
}

/* the fixed fields' values before settings.txt: their defaults (settings presets) */
static PortSettings sDefaults;
static int sDefaultsTaken;

int portSettingsDefault(const char* key, int* value) {
    const Field* f = field(key);
    if (f == NULL) {
        return portSettingsExtraDefault(key, value);
    }
    if (f->value == NULL || !sDefaultsTaken) {
        return 0;
    }
    *value = *(const int*)((const char*)&sDefaults + ((const char*)f->value - (const char*)&sSettings));
    return 1;
}

/* a setting changed by key, kept (settings presets): a field's or a declared one's */
int portSettingsSetKey(const char* key, const char* value) {
    const Field* f = field(key);
    if (f != NULL) {
        set(f, value);
        sOverridden[f - kFields] = 0;
        if (f->value == NULL) {
            sFramerateOverridden = 0;
        }
        return 1;
    }
    return portSettingsExtraSet(key, value);
}

static int sFileFound;

int portSettingsFileFound(void) {
    return sFileFound;
}

void portSettingsLoad(const char* userPath) {
    char* text;
    if (!sDefaultsTaken) {
        /* a weak computer (port/handheld.h): pipelines compile in the background by default, a moment of
         * pop-in rather than the frame held for each new one */
        if (portLowEndPc()) {
            sSettings.noPopIn = 0;
        }
        sDefaults = sSettings;
        sDefaultsTaken = 1;
    }
    snprintf(sUserPath, sizeof(sUserPath), "%s", userPath != NULL ? userPath : ".");
    /* no trailing separator (SDL_GetPrefPath ends in one): every "<user>/<name>" join
     * then has exactly one. A root ("/", "C:/") keeps its own. */
    {
        size_t n = strlen(sUserPath);
        while (n > 1 && (sUserPath[n - 1] == '/' || sUserPath[n - 1] == '\\') && sUserPath[n - 2] != ':') {
            sUserPath[--n] = '\0';
        }
    }
    snprintf(sPath, sizeof(sPath), "%s/settings.txt", sUserPath);
    crashReportInit(sUserPath);
    text = SDL_LoadFile(sPath, NULL);
    sFileFound = text != NULL;
    if (text != NULL) {
        readLines(text);
        SDL_free(text);
    } else if (portSteamDeck()) {
        /* the first start on a Steam Deck (no settings.txt yet): its screen's
         * settings, which the page changes like any other. Fullscreen, the
         * 16:10 picture with the HUD at its edges, the message windows a
         * little larger for the 7-inch screen. */
        char profile[] = "fullscreen=1\nwidescreen=1\naspect_ratio=3\nwide_hud=1\ntext_scale=115\n";
        readLines(profile);
        SDL_Log("settings: the Steam Deck's first-start settings");
    }
#ifdef SETTINGS_APP_PREFS
    syncFromPreferences();
#endif
    /* 0.5.0: the widescreen HUD at the edges is on by default, and 0.4.x wrote wide_hud=0 (its default then)
     * into every settings.txt, chosen or not: on once for a file from before (no wide_hud_050 in it); the
     * player's choice from then on is kept (tests/port/upgrade/upgrade_test.c's exemption) */
    {
        const int before050 = portSettingsUpgradeOnce("wide_hud_050", sFileFound);
        if (before050 && !sSettings.wideHud) {
            sSettings.wideHud = 1;
            printf("[settings] wide_hud: on, once, for a settings file from before 0.5.0\n");
            fflush(stdout);
#ifdef SETTINGS_APP_PREFS
            writePreferences(); /* (the Settings app's page too: its old 0 would come back at the next foreground) */
#endif
        }
        /* and on a weak computer, no_pop_in off once (0.4.x wrote its 1, the default, into every file; its
         * blocking compiles are seconds of stalls on a 2-core CPU, a community report's "lags at the start") */
        if (before050 && portLowEndPc() && sSettings.noPopIn) {
            sSettings.noPopIn = 0;
            printf("[settings] no_pop_in: off, once, on a low-end computer, for a settings file from before 0.5.0\n");
            fflush(stdout);
        }
    }
    /* this run's overrides: $TTYD_SET_<key> for any setting (tests, as the
     * features' own take it, settings_extra.c), kept out of settings.txt */
    {
        size_t i;
        const char* over;
        for (i = 0; i < FIELD_COUNT; i++) {
            if (kFields[i].value != NULL && (over = portSettingsRunOverride(kFields[i].key)) != NULL) {
                sFileValue[i] = *kFields[i].value;
                sOverridden[i] = 1;
                set(&kFields[i], over);
                sRunValue[i] = *kFields[i].value;
            }
        }
    }
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
#ifdef PORT_CLOUD_SYNC
    if (sSettings.cloudSync) {
        portCloudSync(sUserPath, cardRegion(), 1); /* before the game mounts the card */
        portSettingsCloudPush(0);                  /* and what only this device has */
    }
#endif
}

/* The card's folder, by the game's region (Aurora's GCI folder card) */
static const char* cardRegion(void) {
    const char id = PORT_GAME_ID[3];
    return id == 'J' ? "JAP" : id == 'P' ? "EUR" : "USA";
}

/* the card folder's newest modification time (0: none) */
static Sint64 cardStamp(void) {
    char dir[1200];
    Sint64 newest = 0;
    int count = 0;
    char** names;
    int i;
    snprintf(dir, sizeof(dir), "%s/%s/Card A", sUserPath, cardRegion());
    names = SDL_GlobDirectory(dir, "*.gci", 0, &count);
    for (i = 0; names != NULL && i < count; i++) {
        char path[1500];
        SDL_PathInfo info;
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if (SDL_GetPathInfo(path, &info) && info.modify_time > newest) {
            newest = info.modify_time;
        }
    }
    SDL_free(names);
    return newest + count; /* a file gone or added changes it too */
}

static Sint64 sPushedStamp = -1;

void portSettingsCloudPush(int wait) {
#ifdef PORT_CLOUD_SYNC
    if (sSettings.cloudSync && sUserPath[0] != '\0') {
        sPushedStamp = cardStamp();
        portCloudSync(sUserPath, cardRegion(), 0);
        if (wait) {
            portCloudFlush();
        }
    }
#else
    (void)wait;
#endif
}

void portSettingsCloudTick(void) {
#ifdef PORT_CLOUD_SYNC
    static Uint64 sNext;
    const Uint64 now = SDL_GetTicks();
    if (!sSettings.cloudSync || now < sNext || cardIsExec()) {
        return;
    }
    sNext = now + 2000; /* every 2 s: a stat of each save file */
    if (sPushedStamp < 0) {
        sPushedStamp = cardStamp(); /* as pulled at launch */
    } else if (cardStamp() != sPushedStamp) {
        portSettingsCloudPush(0);
    }
#endif
}

int portSettingsPeek(const char* userPath, const char* key, int def) {
    char path[1100];
    char* pref = NULL;
    char* text;
    int value = def;
    size_t keyLen = strlen(key);
    if (userPath == NULL) {
        pref = SDL_GetPrefPath(NULL, "ttyd"); /* Aurora's default (its appName) */
        userPath = pref;
    }
    if (userPath != NULL) {
        snprintf(path, sizeof(path), "%s%ssettings.txt", userPath,
                 userPath[0] != '\0' && userPath[strlen(userPath) - 1] == '/' ? "" : "/");
        text = (char*)SDL_LoadFile(path, NULL);
        if (text != NULL) {
            const char* line = text;
            while (*line != '\0') {
                if (strncmp(line, key, keyLen) == 0 && line[keyLen] == '=') {
                    value = atoi(line + keyLen + 1);
                }
                line += strcspn(line, "\n");
                line += *line == '\n';
            }
            SDL_free(text);
        }
    }
    SDL_free(pref);
#ifdef SETTINGS_APP_PREFS
    {
        CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
        CFPropertyListRef v = CFPreferencesCopyAppValue(k, kCFPreferencesCurrentApplication);
        if (v != NULL) {
            if (CFGetTypeID(v) == CFBooleanGetTypeID()) {
                value = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
            } else if (CFGetTypeID(v) == CFNumberGetTypeID()) {
                CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &value);
            }
            CFRelease(v);
        }
        CFRelease(k);
    }
#endif
    {
        /* the run's override, as portSettingsLoad takes it */
        const char* over = portSettingsRunOverride(key);
        if (over != NULL) {
            value = atoi(over);
        }
    }
    return value;
}

/* online co-op's guest plays on the host's settings: nothing written while
 * that lasts (online_lobby.cpp) */
static int sHoldSaves;

void portSettingsHoldSaves(int hold) {
    sHoldSaves = hold;
}

/* the fixed fields one by one ("sync what matters" online's battle settings, statesync_battle_pc.c):
 * key i and its value now, "" for one with no number (the frame rate), NULL past the last */
const char* portSettingsFieldAt(int i, int* value) {
    if (i < 0 || i >= (int)FIELD_COUNT) {
        return NULL;
    }
    if (kFields[i].value == NULL) {
        *value = 0;
        return "";
    }
    *value = *kFields[i].value;
    return kFields[i].key;
}

/* a setting's value for this run, never saved (online co-op's guest, a replay, the graphics watchdog):
 * settings.txt keeps the player's value until they change the setting themselves */
void portSettingsSetForRun(const char* key, const char* value) {
    const Field* f = field(key);
    if (f != NULL) {
        const size_t i = (size_t)(f - kFields);
        if (f->value == NULL) {
            if (!sFramerateOverridden) {
                snprintf(sFramerateFile, sizeof(sFramerateFile), "%s", sSettings.framerate);
                sFramerateOverridden = 1;
            }
            set(f, value);
            snprintf(sFramerateRun, sizeof(sFramerateRun), "%s", sSettings.framerate);
            return;
        }
        if (!sOverridden[i]) {
            sFileValue[i] = *f->value;
            sOverridden[i] = 1;
        }
        set(f, value);
        sRunValue[i] = *f->value;
    } else {
        portSettingsExtraForce(key, value);
    }
}

void portSettingsSave(void) {
    SDL_IOStream* io;
    size_t i;
    char tmp[sizeof(sPath) + 8];
    int ok = 1;
    portSettingsApply();
    portTrSettingsSync(); /* the menu language's pack, by its code */
    if (sHoldSaves) {
        return;
    }
    /* written whole or not at all: to settings.txt.tmp, then renamed over it (a process killed half way,
       on a phone in the background or a flat battery, leaves the old file, not a cut-short one) */
    snprintf(tmp, sizeof(tmp), "%s.tmp", sPath);
    io = SDL_IOFromFile(tmp, "w");
    if (io == NULL) {
        return;
    }
    for (i = 0; i < FIELD_COUNT; i++) {
        if (kFields[i].value == NULL) {
            ok = ok && SDL_IOprintf(io, "%s=%s\n", kFields[i].key, savedFramerate()) > 0;
        } else {
            ok = ok && SDL_IOprintf(io, "%s=%d\n", kFields[i].key, savedValue(i)) > 0;
        }
    }
    for (i = 0; i < (size_t)portSettingsExtraCount(); i++) {
        int value;
        const char* key = portSettingsExtraAt((int)i, &value);
        if (portSettingsExtraWritten((int)i)) { /* the changed ones (settings.h) */
            ok = ok && SDL_IOprintf(io, "%s=%d\n", key, value) > 0;
        }
    }
    ok = SDL_CloseIO(io) && ok;
    if (!ok || !SDL_RenamePath(tmp, sPath)) {
        SDL_RemovePath(tmp);
    }
#ifdef SETTINGS_APP_PREFS
    writePreferences();
#endif
}

/* The master volume's gain: the slider's 0-100 on an audio taper (portVolumeGain,
 * so half way sounds about half as loud), silent while muted (the mute
 * setting: the speaker button on the Audio tab, or the mute key) or, with
 * "mute while in the background", while another window has the focus. */
static float masterGain(void) {
    static int* sMute;
    if (sMute == NULL) {
        sMute = portSettingsDeclare("mute", 0, 0, 1);
    }
    if (*sMute || (!sFocused && sSettings.muteUnfocused)) {
        return 0.0f;
    }
    if (portSceneSkipMuted()) {
        return 0.0f; /* a scene skipped runs silent (port/scene_skip.h; speed_aurora.c applies it) */
    }
    return portVolumeGain(sSettings.volume);
}

void portSettingsApply(void) {
    static int sFullscreen = -1, sResolution = -1;
    portSettingsSyncChoicesOf(); /* a row made of other settings, changed: into their keys */
    portInterpSetRate(portBatteryFramerate(sSettings.framerate)); /* (battery saver: port/toggles.h) */
    aurora_enable_vsync(portSpeedVsync(sSettings.vsync != 0));
#if defined(__ANDROID__)
    {
        /* Android: SDL's full screen is the immersive one (the status and
         * navigation bars hidden, TtydActivity.java); system_bars keeps them
         * (port/settings/features/window.cpp). The window starts as the key
         * says (portWindowConfig), so only a change is passed on. */
        static int* sBars;
        static int sLastBars = -1;
        if (sBars == NULL) {
            sBars = portSettingsDeclare("system_bars", 0, 0, 1);
        }
        if (sLastBars >= 0 && sLastBars != *sBars) {
            VISetWindowFullscreen(*sBars == 0);
        }
        sLastBars = *sBars;
        sFullscreen = sSettings.fullscreen; /* (the desktop's display mode: not on Android) */
    }
#endif
    if (sFullscreen != sSettings.fullscreen) {
        /* only on a change: the window's own toggles (F11) stay in charge */
        if (sFullscreen >= 0 || sSettings.fullscreen) {
            portWindowSetDisplayMode(sSettings.fullscreen);
        }
        sFullscreen = sSettings.fullscreen;
    }
    if (sResolution != portBatteryResolution(sSettings.resolution)) {
        sResolution = portBatteryResolution(sSettings.resolution);
        VISetFrameBufferScale((float)sResolution);
    }
    portWidescreenSet(sSettings.widescreen);
    portLayoutOrientation(sSettings.orientation);
    /* $TTYD_FAST (tests) always waits, so every frame is complete */
    AuroraSetDrawsBlocking(sSettings.noPopIn != 0 || getenv("TTYD_FAST") != NULL);
    AuroraSetSharpTextures(sSettings.texFilter != 0);
    AuroraSetPresentBackdrop(sSettings.sideBars != 0);
    gPortWideHud = sSettings.wideHud;
    gPortAcCue = sSettings.acCue;
    {
        static const int kTextSpeeds[] = {100, 200, 400, 10000};
        gPortTextSpeed = kTextSpeeds[sSettings.textSpeed];
        gPortSkipIntro = sSettings.skipIntro;
        gPortRunSpeed = sSettings.runSpeed / 100.0f;
    }
    aurora_set_pause_on_focus_lost(sSettings.pauseUnfocused != 0);
    aurora_set_background_input(sSettings.backgroundInput != 0);
    salPcSetVolume(masterGain());
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
        salPcSetVolume(masterGain());
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
        /* a setting set for this run keeps the run's value while the page still has the saved one */
        if (kFields[i].value == NULL ? sFramerateOverridden && strcmp(text, sFramerateFile) == 0
                                     : sOverridden[i] && atoi(text) == sFileValue[i]) {
            continue;
        }
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
            v = CFStringCreateWithCString(NULL, savedFramerate(), kCFStringEncodingUTF8);
        } else if (isToggle(kFields[i].key)) {
            v = CFRetain(savedValue(i) ? kCFBooleanTrue : kCFBooleanFalse);
        } else {
            int n = savedValue(i);
            v = CFNumberCreate(NULL, kCFNumberIntType, &n);
        }
        CFPreferencesSetAppValue(key, v, kCFPreferencesCurrentApplication);
        CFRelease(v);
        CFRelease(key);
    }
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
}
#endif
