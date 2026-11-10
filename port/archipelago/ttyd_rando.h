// The built-in randomizer (docs/RANDOMIZER.md): a single-player item
// randomizer without an Archipelago server. Its settings (<user>/
// randomizer.json) name a seed and what is shuffled; at the game's start the
// seed is generated here (port/archipelago/rando_gen.cpp) and applied by the
// Archipelago support's own machinery (ttyd_ap.cpp: each location's item
// written where the game gives it), and its spoiler log goes in
// <user>/randomizer/. The location data is the TTYD apworld's, made for the
// US disc; ttyd_ap_data.h maps it onto the JP and PAL discs too.
#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
extern "C" {
#endif

typedef struct PortRandoSettings {
    int enabled;      /* applies from the next start */
    char seed[48];
    int badges, items, starPieces, shineSprites;
    int keyItems;     /* key items too: the TTYD AP mod's open world */
    int spoiler;      /* write the spoiler log */
    int openWorld;    /* Paper Cut's own open world: each new file stamped with the seed (port/open_world.h) */
    int openGoal;     /* its goal: the Crystal Stars it asks for (0: all the chapters it has) */
    /* 0.5.0 */
    int logicLevel;   /* 0 glitchless, 1 glitchless plus the chosen tricks, 2 no logic (ttyd_rando::LogicLevel) */
    int tricks;       /* the tricks counted at level 1 (ttyd_rando::tricks()' bits) */
    int shops;        /* shop stock moves among the shops' shelves */
    int troubles;     /* troubles' rewards take part */
    int mystery;      /* items lying about show as a Mystery until picked up */
    int music;        /* the music shuffled (port/archipelago/sound_shuffle.cpp) */
    int sounds;       /* the sound effects shuffled */
    int gates;        /* the open world's chapter pipes lead to other chapters (the entrance shuffle) */
    int rooms;        /* its room doors swap within their area (port/archipelago/pc_doors.h) */
    int mixWays;      /* its ways between areas lead to one another's areas */
    int transports;   /* the blimp, Flavio's ship and the train lead to one another's destinations */
} PortRandoSettings;

/* A permalink: one text for the seed and every setting above that makes the
 * game ("PC1-<settings>-<seed>"); the spoiler log's choice isn't in it. */
const char* portRandoPermalink(void);
/* The settings and seed from a permalink (saved); NULL, or why it isn't one. */
const char* portRandoApplyPermalink(const char* text);
/* Named presets: the built-in ones, then the player's (randomizer.json).
 * Applying one keeps the seed. */
int portRandoPresetCount(void);
const char* portRandoPresetName(int i);
const char* portRandoPresetNote(int i); /* a built-in's one line ("" for the player's) */
int portRandoPresetBuiltIn(int i);
void portRandoApplyPreset(int i);
/* the settings now as a preset of this name (replacing one of the player's of that name) */
void portRandoSavePreset(const char* name);
void portRandoDeletePreset(int i); /* the player's only */
/* The sound editor (<user>/sound_editor.json; port/include/port/sound_shuffle.h): music
 * (sfx 0) or sound effects (1) replaced by name. Applied at once and at every start. */
int portRandoSoundEditCount(int sfx);
const char* portRandoSoundEditFrom(int sfx, int i);
const char* portRandoSoundEditTo(int sfx, int i);
/* from -> to (to "" or NULL: the replacement taken out) */
void portRandoSoundEditSet(int sfx, const char* from, const char* to);
/* At start (portApInit): the music and sound shuffle and the editor's choices applied. */
void portRandoApplySound(const char* userPath);

/* This build has the location data (every version now: 1). */
int portRandoSupported(void);
/* The TTYD AP mod's open world (key items without Paper Cut's): the US version only. */
int portRandoApWorldSupported(void);
/* The settings, read from <userPath>/randomizer.json the first time. */
PortRandoSettings* portRandoSettings(const char* userPath);
void portRandoSaveSettings(void);
/* A new random seed in the settings (saved). */
void portRandoNewSeed(void);
/* Generates the settings' seed now, without applying it: what it would do
 * ("403 locations, 370 moved"), or why it can't. */
const char* portRandoCheck(void);
/* This run's seed and what it shuffled, or "" when the game started without one. */
const char* portRandoActive(void);
/* An enabled code mod that looks like the TTYD AP mod (its id or name). */
int portRandoApModFound(void);
/* Online and in replays the settings are the session's (the host's, the
 * recording's: portRandoSettingsForRun), not this player's randomizer.json:
 * 1 while they are, so the open world can stamp a new file there. */
int portRandoShared(void);
/* At start-up, as the built-in randomizer would apply (ttyd_ap.cpp's portApInit). */
void portRandoBoot(void);

#ifdef __cplusplus
}

/* For ttyd_ap.cpp, at start: the settings' seed, generated, when the
 * randomizer is on (and the spoiler log written). */
struct PortRandoRun {
    std::string seed;
    bool openWorld = false;
    std::vector<std::pair<int, int64_t>> placements; /* kTtydApLocations index, Archipelago item id */
};
bool portRandoStartRun(const char* userPath, PortRandoRun& out);
/* What portRandoStartRun applied this run (the tracker's): false if nothing. */
namespace ttyd_rando {
struct Options;
struct Placement;
}
bool portRandoRunSeed(ttyd_rando::Options& options, std::vector<ttyd_rando::Placement>& placements);

/* The settings as text, for online's setup and a replay's "RAND" trailer (a
 * seed made first when it is on with none, so every side has the same). */
std::string portRandoSessionText(const char* userPath);
/* What the classic randomizer applied at this start-up, as settings text
 * ("" none); booted false: it hasn't started yet. */
std::string portRandoRunText(bool* booted);
/* What the classic randomizer would apply at a start-up on settings text ("" none). */
std::string portRandoRunTextOf(const std::string& settingsText);
/* The session's settings for this run, never saved (text from
 * portRandoSessionText); "": this player's randomizer.json again. A
 * playback's come before the classic randomizer applies (replay.cpp's
 * portReplayEarly, at start-up). */
void portRandoSettingsForRun(const std::string& text);
#endif
