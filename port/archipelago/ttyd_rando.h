// The built-in randomizer (docs/RANDOMIZER.md): a single-player item
// randomizer without an Archipelago server. Its settings (<user>/
// randomizer.json) name a seed and what is shuffled; at the game's start the
// seed is generated here (port/archipelago/rando_gen.cpp) and applied by the
// Archipelago support's own machinery (ttyd_ap.cpp: each location's item
// written where the game gives it), and its spoiler log goes in
// <user>/randomizer/. The location data is the US version's (the TTYD
// apworld's), so it works on the US build only.
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
} PortRandoSettings;

/* This build has the location data (the US version). */
int portRandoSupported(void);
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
#endif
