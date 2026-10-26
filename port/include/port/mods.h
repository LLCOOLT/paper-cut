#ifndef PORT_MODS_H
#define PORT_MODS_H

/* Mods (docs/MODDING.md): the mods folder in Aurora's user folder, one mod
 * per folder or .ttydmod/.zip file, each with a mod.json. A mod can replace
 * or add disc files (overlay/), textures (textures/, Dolphin's names),
 * messages (messages/) and game data by name (patches/). The order and
 * on/off state are in mods/mods.txt; the first mod listed wins over the ones
 * after it. port/mods/mods.cpp. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortModInfo {
    const char* id;
    const char* name;
    const char* version;
    const char* author;
    const char* description;
    const char* path;  /* the folder or file, for display */
    const char* error; /* why it can't be used, or NULL */
    int enabled;
    int files;         /* overlay files */
    int textures;      /* texture replacements */
    int messages;      /* messages added or replaced */
    int patches;       /* data patch lines applied */
    const char* warning; /* the first patch line that could not be applied, or NULL */
    int code;          /* PORT_MOD_CODE_*: its code (a library or built in) */
    const char* codeError; /* why its code didn't start, or NULL */
    int optionCount;   /* options (mod.json "options"): portModsGetOption */
} PortModInfo;

enum {
    PORT_MOD_CODE_NONE,             /* no code */
    PORT_MOD_CODE_RUNNING,
    PORT_MOD_CODE_OFF,              /* turned off (and not running) */
    PORT_MOD_CODE_STARTS_NEXT_START, /* turned on since the game started */
    PORT_MOD_CODE_STOPS_NEXT_START, /* turned off since: still running until then */
    PORT_MOD_CODE_FAILED,
};

enum { PORT_MOD_OPTION_BOOL, PORT_MOD_OPTION_INT, PORT_MOD_OPTION_CHOICE };

typedef struct PortModOption {
    const char* key;
    const char* label;
    int type;          /* PORT_MOD_OPTION_* */
    int min, max;
    int value;
    int choiceCount;
    const char* const* choices;
} PortModOption;

const PortModOption* portModsGetOption(int index, int option);
/* Written to mods/options.txt; the mod's code reads it (api->optionInt). */
void portModsSetOption(int index, int option, int value);

/* Reads the mods folder and applies the enabled mods (main_aurora.c, once
 * the disc is open), and the loose textures folder. $TTYD_NO_MODS: neither
 * is applied. */
void portModsInit(const char* userPath, const char* cachePath);
/* The mods in order, the first winning. */
int portModsCount(void);
const PortModInfo* portModsGet(int index);
/* Changes are written to mods.txt and applied at once: files the game reads
 * from then on (the next map, the next battle), textures right away. */
void portModsSetEnabled(int index, int enabled);
void portModsMove(int index, int delta);
/* Reads the mods folder again (a mod added or edited) and applies it. */
void portModsReload(void);
const char* portModsDir(void);
/* Where texture dumps go (the settings' "Dump textures"). */
const char* portModsDumpDir(void);
/* msgSearch (src/driver/msgdrv.c): an enabled mod's message for tag in the
 * language whose message folder is langDir ("msg/US", "msg", "emsg"), or
 * NULL. */
const char* portModMsgSearch(const char* tag, const char* langDir);

/* A map's init script from an enabled mod's evt/ (mod.json "maps"), or NULL:
 * the game's MapData for it (port/src/aurora/mod_game_aurora.c). */
void* portModsMapScript(const char* map);

/* DVDMgrOpen (src/manager/dvdmgr.c): path, or the path a mod's new map reads
 * its files from (mod.json "maps": "files": another map's). */
const char* portModsPathAlias(const char* path);

/* A disc file the host itself adds (an Archipelago seed's), below every mod's
 * overlay; in use from the next portModsReload. */
void portModsSetHostFile(const char* discPath, const void* data, size_t size);

#ifdef __cplusplus
}
#include <string>
#include <vector>
/* One file of a zip (an .apttyd seed), by its path in the zip. */
bool portModsReadZip(const std::string& zip, const std::string& member, std::vector<uint8_t>& out);
/* A game name's address, as a mod's script names it (a function, a variable,
 * file::name for a file's static); nullptr and why if there's none. */
void* portModsGameAddress(const std::string& name, std::string& why);
#endif

#endif
