#ifndef PORT_SETTINGS_H
#define PORT_SETTINGS_H

/* The port's own settings (Aurora backend): settings.txt in Aurora's user
 * folder, by the memory cards, one "key=value" per line, written by the
 * settings page (port/settings/settings_ui.cpp: F1, a controller's Back or
 * a three-finger tap). An environment variable named in a setting's comment
 * overrides the file for that run. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortSettings {
    /* Video */
    char framerate[16]; /* "60" (off), "120", "144", "240" or "display":
                           frames drawn between the game's ($TTYD_FRAMERATE) */
    int vsync;          /* present at the display's refresh */
    int fullscreen;
    int widescreen;     /* 16:9 ($TTYD_WIDESCREEN; on Apple, also the
                           system preference when the file has no value) */
    int resolution;     /* 0: the window's; 1-8: that multiple of 640x480 */
    int fps;            /* the overlay (frame rate, clock, battery): 0 off, 1-4 a corner (TL TR BL BR) */
    int overlayFps;     /* the overlay shows the frame rate */
    int overlayClock;   /* ... the time */
    int overlayBattery; /* ... the battery's charge (where there is one) */
    int noPopIn;        /* a draw whose GPU pipeline is not built yet waits for it (a short
                           pause the first time) instead of being skipped (popping in later) */
    int texFilter;      /* textures: 0 the game's filtering, 1 sharp (their pixels shown) */
    int msaa;           /* 4x multisample anti-aliasing (from the next start) */
    int sideBars;       /* the bars beside a picture that doesn't fill the screen: the picture blurred, not black */
    int wideHud;        /* widescreen: the status bar's parts at the screen's edges, not the 4:3 middle */
    /* Audio */
    int volume;         /* 0-100 */
    int muteUnfocused;  /* silent while another window has the focus */
    /* System */
    int pauseUnfocused; /* the game waits while another window has the focus
                           ($TTYD_PAUSE_UNFOCUSED) */
    int backgroundInput; /* controllers still work while unfocused */
    int cloudSync;      /* Apple: saves kept the same as in iCloud Drive (port/include/port/cloud.h) */
    /* Controls */
    int rumble;         /* the game's rumble at all (its own option still applies) */
    int rumbleStrength; /* 0-100: PADSetRumbleIntensity on every port */
    int phoneRumble;    /* phone/tablet: vibrate the device when no controller has a motor */
    int touchHaptics;   /* a light tap when an on-screen button is pressed */
    int touchOpacity;   /* on-screen controls, 20-100 % */
    int touchSize;      /* on-screen controls, 70-150 % */
    int autoRun;        /* on-screen stick: past a small tilt, always a full one (run) */
    int stickFixed;     /* on-screen stick: fixed at its place, not where the finger lands */
    int tapA;           /* a tap on the picture presses A */
    /* Layout (port/include/port/layout.h) */
    int orientation;    /* phone/tablet: 0 turns with the device, 1 landscape, 2 portrait */
    int safeArea;       /* the picture kept clear of the notch and home indicator */
    /* Gameplay (port/include/port/gameplay.h) */
    int ffSpeed;        /* fast-forward speed, 2-8 x (port/src/aurora/speed_aurora.c) */
    int ffToggle;       /* the fast-forward control toggles instead of being held */
    int textSpeed;      /* 0 original, 1 fast (2x), 2 faster (4x), 3 instant */
    int skipIntro;      /* boot logos fast, then the title without the story demo */
    int runSpeed;       /* Mario's run speed, 100-150 % */
    int acCue;          /* Action Command cue: 0 off, 1 on screen, 2 also a tap (phones) */
    int language;       /* European release: the game's text, 0 the computer's language, 1-5 English
                           German French Spanish Italian (from the next start; $TTYD_LANGUAGE) */
    int pal50Hz;        /* European release: run at 50 Hz, as a PAL TV, not 60 (from the next start; $TTYD_50HZ) */
    /* Mods (port/include/port/mods.h) */
    int dumpTextures;   /* textures without a replacement are saved, under the names packs use
                           (from the next start: $TTYD_DUMP_TEXTURES, portSettingsPeek) */
} PortSettings;

/* PADControlMotor for the game (aurora_overlay/dolphin/pad.h): the
 * rumble settings applied, then Aurora's. */
void portPADControlMotor(unsigned int chan, unsigned int cmd);

PortSettings* portSettings(void);
/* Reads settings.txt (main_aurora.c, once Aurora is up) and applies it. */
void portSettingsLoad(const char* userPath);
/* One setting's value before the settings are loaded (Aurora's start-up options,
 * e.g. msaa): settings.txt in userPath (NULL: Aurora's default folder), then on
 * iOS the Settings app; def when neither has it. */
int portSettingsPeek(const char* userPath, const char* key, int def);
/* Applies the settings as they are now, then writes settings.txt. */
void portSettingsSave(void);
void portSettingsApply(void);
/* iCloud Drive save sync: once a frame (pushes the card's changes), and when
 * the app goes to the background or quits (wait: until done). */
void portSettingsCloudTick(void);
void portSettingsCloudPush(int wait);
/* The window gained or lost the focus. */
void portSettingsFocus(int focused);
/* Aurora's user folder (the memory cards, settings.txt). */
const char* portSettingsUserPath(void);
/* The last crash's report (last_crash.txt in the user folder), or NULL. */
const char* portCrashReportLast(void);
/* The last run crashed and the report was not looked at yet. */
int portCrashReportPending(void);
void portCrashReportDismiss(void);

#ifdef __cplusplus
}
#endif

#endif
