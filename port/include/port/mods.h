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
    int lua;           /* its lua/ files (*.lua): a Lua mod (port/include/port/lua_mods.h;
                          portLuaModState says how it runs) */
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
    const char* note;  /* mod.json "note": what it does, or "" */
    int fallback;      /* mod.json "default" */
    int nextStart;     /* mod.json "restart": true: the mod's code reads it at the game's start */
} PortModOption;

const PortModOption* portModsGetOption(int index, int option);
/* Written to mods/options.txt; the mod's code reads it (api->optionInt). */
void portModsSetOption(int index, int option, int value);

/* What a mod changes, by kind, and where another mod changes the same thing
 * (the mod manager on the settings page). Whether or not the mod wins there:
 * the first in the list does. Hooks are known once its code runs. */
enum {
    PORT_MOD_CHANGE_FILE,    /* disc files (overlay/), by disc path */
    PORT_MOD_CHANGE_TEXTURE, /* textures (textures/), by file name */
    PORT_MOD_CHANGE_MESSAGE, /* messages, by tag ("DE/tag" for one language) */
    PORT_MOD_CHANGE_PATCH,   /* data patches, by target */
    PORT_MOD_CHANGE_HOOK,    /* hook slots its code took: "name (pre|post|replace)" */
    PORT_MOD_CHANGE_MAP,     /* maps given an init script (mod.json "maps") */
    PORT_MOD_CHANGE_COUNT
};
int portModsChangeCount(int index, int kind);
const char* portModsChange(int index, int kind, int i);

/* The things of one kind both this mod and another one change. */
typedef struct PortModConflict {
    int kind;          /* PORT_MOD_CHANGE_* */
    int count;         /* how many */
    const char* what;  /* the first few, for display ("a, b, c") */
    const char* other; /* the other mod's name */
    int wins;          /* this mod's are the ones in use (it is first in the list); hooks:
                          1 both run (pre/post), 0 only the first's replacement runs */
} PortModConflict;
int portModsConflictCount(int index);
const PortModConflict* portModsGetConflict(int index, int i);

/* The mods folder read without a running game (the launcher's settings):
 * listed, turned on and off and ordered; nothing is applied. Does nothing
 * once portModsInit ran. */
void portModsPreview(const char* userPath);

/* Texture packs: the loose textures folder and the enabled mods' textures,
 * against the textures seen while dumping (the dump folder). */
typedef struct PortTextureStats {
    int replacements;  /* textures replaced (distinct names over every pack) */
    int files;         /* texture files over every pack */
    int collisions;    /* names given by more than one file (the first pack wins) */
    int dumped;        /* textures in the dump folder (seen in play, unreplaced when dumped) */
    int dumpedCovered; /* of those, now replaced by a pack */
    int dumping;       /* dumping is on now */
} PortTextureStats;
/* Counts again (reads the folders): call on demand, not every frame. */
void portModsTextureStats(PortTextureStats* out);
/* A colliding name and the packs giving it, the first winning ("a, b"), for
 * i < collisions; NULL past the end. */
const char* portModsTextureCollision(int i, const char** packs);
/* Texture dumping on or off while the game runs (the dump_textures setting). */
void portModsSetTextureDumps(int on);
/* The texture packs read again (files added or changed while playing). */
void portModsReloadTextures(void);

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

/* A map the host itself adds (the modes', port/include/port/modes.h), below
 * every mod's: its init script and the map whose files it reads. Before
 * portModsInit (or followed by portModsReload). */
void portModsAddHostMap(const char* map, void* initScript, const char* files);
/* The map whose files a mod's or the host's new map reads, or NULL
 * (seq_mapchange.c: a new map borrowing tou_03's loads tou_03's REL). */
const char* portModsMapFiles(const char* map);

/* DVDMgrOpen (src/manager/dvdmgr.c): path, or the path a mod's new map reads
 * its files from (mod.json "maps": "files": another map's). */
const char* portModsPathAlias(const char* path);

/* A disc file the host itself adds (an Archipelago seed's), below every mod's
 * overlay; in use from the next portModsReload. */
void portModsSetHostFile(const char* discPath, const void* data, size_t size);
/* A texture of a map-scoped set the host adds: it applies only while the
 * map's name matches pattern ("muj_l1,muj_l2", "muj_l*": alternatives, with
 * * and ? wildcards), above every mod's global textures (a mod's own
 * textures/@<pattern>/ set still wins). path's last part is its pack name
 * (tex1_...png); data stays the caller's. Sets swap as the map changes. */
void portModsAddMapTexture(const char* pattern, const char* path, const void* data, size_t size);
/* Each frame (mod_game_aurora.c): the map-scoped sets for the map now */
void portModsMapTextures(const char* map);

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
