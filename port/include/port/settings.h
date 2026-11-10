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
    /* General (and Controls, Saves) */
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
    int touchSize;      /* on-screen controls, 50-200 % of the size for the screen (port/include/port/layout.h) */
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
    /* Launcher (port/settings/launcher_ui.cpp) */
    int launcherSkip;   /* start with the last disc played, without the launcher */
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
/* A Steam Deck (Linux; $TTYD_STEAM_DECK=0/1 for tests): its first start, with
 * no settings.txt yet, takes settings for its screen (settings_aurora.c). */
int portSteamDeck(void);
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
/* The desktop window (port/settings/features/window.cpp): before
 * aurora_initialize, its saved size and place and full screen into Aurora's
 * config (left as they are when there are none); after the settings are
 * loaded, checked against the screens and maximized as it was (keepPlace:
 * $TTYD_WINDOW_POS placed it). */
void portWindowConfig(const char* userPath, int* x, int* y, unsigned* w, unsigned* h, int* fullscreen);
void portWindowRestore(int keepPlace);
/* The display mode (the fullscreen setting: 0 a window, 1 borderless, 2 full
 * screen, exclusive) put in place; F11 and Alt+Enter: the window, or the
 * full-screen kind last chosen. */
void portWindowSetDisplayMode(int mode);
void portWindowToggleFullscreen(void);
/* The window gained or lost the focus. */
void portSettingsFocus(int focused);
/* Aurora's user folder (the memory cards, settings.txt). */
const char* portSettingsUserPath(void);
/* The last crash's report (last_crash.txt in the user folder), or NULL. */
const char* portCrashReportLast(void);
/* The last run crashed and the report was not looked at yet (the recovery screen: once per crash). */
int portCrashReportPending(void);
void portCrashReportDismiss(void);
/* Before Aurora starts (main_aurora.c): the report's place in userPath (NULL: Aurora's default
 * folder), the last run's report kept as last_crash.txt; portSettingsLoad does it if this didn't. */
void portCrashReportEarly(const char* userPath);

/* A setting of a feature's own, declared from the feature's file instead of a
 * PortSettings field (port/src/common/settings_extra.c): the address of its
 * value, stable for the run, in [min, max]; def until settings.txt or
 * $TTYD_SET_<key> (this run, any build) says otherwise. key: lower_case, as
 * in settings.txt. Declare it once, at start or first use. */
int* portSettingsDeclare(const char* key, int def, int min, int max);
/* A declared setting's value address by key, or NULL. */
int* portSettingsFind(const char* key);
/* This run's override for a setting (fixed or declared): $TTYD_SET_<key>, else under $TTYD_CLEAN_FRAME=1
 * (media captures) the clean frame's value for the settings that draw over the picture; NULL: none. */
const char* portSettingsRunOverride(const char* key);
/* $TTYD_CLEAN_FRAME=1: nothing over the picture that the shot doesn't ask for (settings_extra.c) */
int portCleanFrame(void);
/* Why a setting (by its value's address, as portSettingsFind's) can't change
 * now for a reason other than console-accurate mode (Play as while Brothers
 * mode is on), in English for portTr / portTrGame, or NULL: the page dims it
 * and says this (port/settings/features/gameplus.cpp). */
const char* portSettingWhyLocked(const int* value);
/* A one-time upgrade step (settings_aurora.c's portSettingsLoad): mark is a setting of its own, 0..1,
 * 0 until the step is done and 1 from then on (written with the next save). 1 once: when a settings.txt
 * was read (fileFound) that hasn't the mark (a release before the step's wrote it); a first start (no
 * file) only sets the mark, the defaults being the step's already. */
int portSettingsUpgradeOnce(const char* mark, int fileFound);
/* For settings.txt (settings_aurora.c): a key=value line that isn't a fixed
 * field, and the declared and read ones to write back. */
int portSettingsExtraSet(const char* key, const char* value);
void portSettingsExtraForce(const char* key, const char* value);
/* Online co-op's guest, a replay's playback: a setting of this player's own
 * that the run's set doesn't have, at its default for this run: now if it is
 * declared, else once it is (one declared on first use, whose default isn't
 * known yet). 0: no such setting. */
int portSettingsExtraToDefault(const char* key);
/* Settings presets (port/settings/features/presets.cpp): a setting's default
 * (a field's before settings.txt, a declared one's def), 0 when unknown; and a
 * setting changed by key, kept (saved with the next portSettingsSave). */
int portSettingsExtraDefault(const char* key, int* value);
/* a setting's value, declared or only read so far (settings_extra.c): 1 when there is one */
int portSettingsExtraRaw(const char* key, int* value);
int portSettingsDefault(const char* key, int* value);
int portSettingsSetKey(const char* key, const char* value);
/* A setting's value for this run (a field or a declared one): online co-op's
 * guest (port/settings/online_lobby.cpp), a replay, the graphics watchdog. A
 * field's is never saved while it stays at the run's value (settings.txt keeps
 * the player's; their own change makes it theirs again); settings.txt is left
 * alone altogether while hold is set. */
void portSettingsSetForRun(const char* key, const char* value);
void portSettingsHoldSaves(int hold);
/* The backend's fixed fields one by one ("sync what matters" online's shared
 * battles): key i and its value now, "" for one with no number, NULL past the
 * last. The declared ones: portSettingsExtraAt. */
const char* portSettingsFieldAt(int i, int* value);
/* A declared setting changed for a while (a mode's run): settings.txt keeps
 * value meanwhile (on), whatever it is in memory; off, its own again. */
void portSettingsExtraSaveAs(const char* key, int on, int value);
int portSettingsExtraCount(void);
const char* portSettingsExtraAt(int i, int* value);
/* Tests: how many declared settings hold a value outside their range (the
 * first written in bad); $TTYD_SETTINGS_DUMP=<file> writes every setting and
 * its range as the run ends (settings_extra.c; an input script's end too). portSettingsDumpFields: the
 * backend's fixed fields into that file (a FILE*). */
int portSettingsExtraCheck(char* bad, int size);
void portSettingsDumpFields(void* file);
/* the dump now (the test exits leave with _Exit, past atexit) */
void portSettingsDumpNow(void);

/* Whether settings.txt gets entry i: not a declared setting at its default.
 * Older releases (0.2.0, 0.3.0) abort with more than 256 such keys in the
 * file, so only the changed ones are written; a missing key reads as its
 * default in every release, so nothing changes meaning. */
int portSettingsExtraWritten(int i);
/* settings.txt was there at start (an update or a later start, not the
 * first one ever). */
int portSettingsFileFound(void);

/* A feature's control on the settings page (port/settings/settings_ui.cpp),
 * for a setting it declared: tab ("Gameplay", "Accessibility", ... or a new
 * tab's name), label, note (or NULL), flags, and words search finds it by (or
 * NULL). Call before the page first shows (a C++ static initializer in the
 * feature's settings file does). */
enum {
    PORT_SETTING_NEXT_START = 1,    /* takes effect the next time the game starts */
    PORT_SETTING_CHANGES_PLAY = 2,  /* changes how the game plays */
    PORT_SETTING_CHANGES_SAVES = 4, /* changes what goes in a save */
    PORT_SETTING_IN_GAME = 8,       /* only shown while a game runs */
};
void portSettingsAddToggle(const char* tab, const char* label, const char* note, unsigned flags, const char* key,
                           const char* keywords);
void portSettingsAddChoice(const char* tab, const char* label, const char* note, unsigned flags, const char* key,
                           const char* const* choices, int count, const char* keywords);
void portSettingsAddSlider(const char* tab, const char* label, const char* note, unsigned flags, const char* key,
                           int min, int max, const char* format, const char* keywords);
/* A choice made of other settings (one row for several keys): get() reads the
 * choice from them, set() writes it back into them. It has no key of its own
 * in settings.txt, presets or replays; id names it for portSettingsSetShown
 * and the like. Its controls' value (PortSettingsControl) is a copy, read
 * again from the keys whenever the controls are listed or drawn, and written
 * back by set() at portSettingsApply when a page changed it. */
void portSettingsAddChoiceOf(const char* tab, const char* label, const char* note, unsigned flags, const char* id,
                             int (*get)(void), void (*set)(int), const char* const* choices, int count,
                             const char* keywords);
/* settings_aurora.c's portSettingsApply: the choices of other settings a page
 * changed, written into their keys. */
void portSettingsSyncChoicesOf(void);
/* A feature's control (by its key) shown only while shown() says so (a
 * device's second display, say); call after adding it. */
void portSettingsSetShown(const char* key, int (*shown)(void));
/* A choice's names changed (by its key): the menu language's, as language
 * packs are read (port/settings/tr.cpp). names stays valid; the value is kept
 * below count by the caller. */
void portSettingsSetChoices(const char* key, const char* const* names, int count);
/* A section the feature draws itself with Dear ImGui (buttons, lists): draw is
 * called where the item goes (inGame: the page is open over a running game,
 * not the launcher's); search finds it by label, note and keywords. */
void portSettingsAddSection(const char* tab, const char* label, const char* note, unsigned flags,
                            void (*draw)(int inGame), const char* keywords);
/* The sections added (their tabs and labels, in order), for the Start menu's
 * Settings tab (port/include/port/pause_forms.h); returns how many, up to max. */
int portSettingsSections(const char** tabs, const char** labels, int max);
/* A feature's drawing over the game (a timer, a toast) each presented frame,
 * with Dear ImGui's foreground draw list (pageOpen: the settings page is
 * open), and its SDL events (a key of its own) before the page's: event
 * returns nonzero for one it used. Either may be NULL. Call as AddSection. */
void portSettingsAddOverlay(void (*draw)(int pageOpen), int (*event)(const void* sdlEvent));
/* An event handler asked before Paper Cut's own keys and buttons (Bindings: Tab's
 * fast-forward, Backspace's rewind, a controller's Back for this page): for a
 * mode that takes the controls over while it is open (the map editor). It
 * returns nonzero for an event it used, and 0 for every event while it is
 * closed or the settings page is open. */
void portSettingsAddEventFirst(int (*event)(const void* sdlEvent));

/* The page's plain controls (an on/off, a choice of names or a range of
 * numbers), for the pause menu's Settings tab (port/src/common/
 * pause_settings_aurora.c): the built-in ones and every feature's, those shown
 * now, in the page's order of tabs. value: its int (a field or a declared
 * setting); step: a slider's step. Fills up to max, returns how many. */
typedef struct PortSettingsControl {
    const char* tab;
    const char* label;
    const char* note;
    unsigned flags;
    int kind; /* 0 on/off, 1 a choice of choices[count], 2 a slider min..max */
    int* value;
    const char* const* choices;
    int count, min, max, step;
    const char* format; /* a slider's printf format, e.g. "%d%%" */
} PortSettingsControl;
int portSettingsControls(PortSettingsControl* out, int max);

/* Where each setting is shown (port/settings/settings_layout.cpp): the tab,
 * the group heading it sits under (or NULL) and its place in the order, for
 * the control a feature added to tab with this English label. Returns 0 for
 * one not placed there (out: its own tab, no group, last). A setting's key
 * never depends on its place. */
typedef struct PortSettingsPlaceInfo {
    const char* tab;
    const char* group;
    int order;
    int isNew; /* new in this version: marked on the full page */
} PortSettingsPlaceInfo;
int portSettingsPlace(const char* tab, const char* label, PortSettingsPlaceInfo* out);
/* The tabs in their order: the settings, then the modes and tools. A tab with
 * pages under it (Enhancements) is not listed itself: its pages are, in its
 * place. */
int portSettingsTabOrder(const char** tabs, int max);
/* The tab a page is under (Enhancements for Field, Battle...), or NULL for a
 * tab of its own. English names, as portSettingsTabOrder's. */
const char* portSettingsTabParent(const char* tab);

/* A category's page in the Start menu, in the page's order: its plain
 * controls (kind 0-2) and its sections (kind 3, value NULL: a section of the
 * full page, by its label), each with its group heading or NULL. */
typedef struct PortSettingsPageRow {
    PortSettingsControl c;
    const char* group;
} PortSettingsPageRow;
int portSettingsPageRows(const char* tab, PortSettingsPageRow* out, int max);

#ifdef __cplusplus
}
#endif

#endif
