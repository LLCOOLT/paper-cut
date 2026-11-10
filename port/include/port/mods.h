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
    unsigned kinds;    /* PORT_MOD_KIND_*: what it holds, for the mod manager's badges */
    int pack;          /* a language pack only (a folder with lang/pack.txt and no mod.json) */
    int online;        /* PORT_MOD_ONLINE_*: what online play asks of it */
    const char* folder; /* its folder's or file's name in the mods folder ("" built in) */
} PortModInfo;

/* What a mod holds (PortModInfo.kinds). */
enum {
    PORT_MOD_KIND_FILES = 1 << 0,    /* overlay/ */
    PORT_MOD_KIND_TEXTURES = 1 << 1, /* textures/ */
    PORT_MOD_KIND_TEXT = 1 << 2,     /* messages/ */
    PORT_MOD_KIND_DATA = 1 << 3,     /* patches/ */
    PORT_MOD_KIND_SCRIPTS = 1 << 4,  /* evt/ */
    PORT_MOD_KIND_LUA = 1 << 5,      /* lua/main.lua */
    PORT_MOD_KIND_CODE = 1 << 6,     /* lib/ or built in */
    PORT_MOD_KIND_LANG = 1 << 7,     /* lang/pack.txt: a language pack for Paper Cut's menus */
    PORT_MOD_KIND_MAPS = 1 << 8,     /* maps/<room>.json: map edits (port/include/port/map_edits.h) */
};

/* Online play and a mod (PortModInfo.online): draw-only mods (textures, a
 * language pack) are each player's own; Lua-only mods run when every player
 * has the same Lua (lua_mods.h); every other mod that is on must be on for
 * every player (the online hello's list, portModsOnlineList). */
enum { PORT_MOD_ONLINE_OWN, PORT_MOD_ONLINE_LUA, PORT_MOD_ONLINE_SHARED };
/* The online hello's mods: the ids of the usable PORT_MOD_ONLINE_SHARED
 * mods, sorted, comma-separated ("" for none). */
const char* portModsOnlineList(void);

/* Safe mode (the mod manager): a start with every mod off when the last one
 * with mods on didn't get going. Mods stay as they are in mods.txt.
 * <user>/mods_starting.txt names the mods on while a start is under way; it
 * goes once the game has run a while (portModsStartedOk) or quits. */
/* The mods on at the start that didn't get going, a name a line, or NULL
 * (reads the file: the launcher asks before portModsInit). */
const char* portModsLastStartFailed(const char* userPath);
/* This start: every mod off (1), or as they are (0); decided, so
 * portModsInit doesn't ask again. In the launcher the list's preview follows. */
void portModsSetSafeMode(int on);
int portModsSafeMode(void);
/* The game got going: the start's file goes (the UI calls it a while into play). */
void portModsStartedOk(void);
/* Asked by portModsInit when the last start failed and nothing decided yet
 * (no launcher, or it skipped to the disc): names as above; returns 1 for
 * safe mode. Set by the mod manager (port/settings/features/mods_manager.cpp). */
void portModsSetSafeAsk(int (*ask)(const char* names));

/* Language packs and mods' lang/ (port/settings/tr.cpp): the folder in the
 * mods folder is turned off in mods.txt (by its mod.json id, else its name). */
int portModsFolderOff(const char* userPath, const char* folder);

/* The mod's whole path (its folder or file), "" for a built-in one. */
const char* portModsPath(int index);
/* Moves mod index out of the mods folder into <user>/mods_old/ (kept there,
 * not deleted) and reads the folder again; NULL, or why it couldn't. */
const char* portModsRemove(int index);

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
/* Moves on whenever the packs' textures change (applied, reloaded, a map's
 * own set on or off): sharp text looks again for a pack's font. */
int portModsTexturesGeneration(void);

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
/* A mod's message (portModMsgSearch's) as the game may print it: its %
 * conversions kept where they are the game's own message's (original: the
 * disc's text for the tag; in order and as written: "%s", "%04d"...), every
 * other % shown as it is; with no original (a tag of the mod's own), one %s
 * or %d. msgSearch (src/driver/msgdrv.c) calls it; the text stays valid. */
const char* portModsFitMessage(const char* modded, const char* original);

/* A map's init script from an enabled mod's evt/ (mod.json "maps"), or NULL:
 * the game's MapData for it (port/src/aurora/mod_game_aurora.c). */
void* portModsMapScript(const char* map);

/* A map the host itself adds (the modes', port/include/port/modes.h), below
 * every mod's: its init script and the map whose files it reads. Before
 * portModsInit (or followed by portModsReload). */
void portModsAddHostMap(const char* map, void* initScript, const char* files);
/* map is one of the host's own (a mode's: no way out, on no area map). */
int portModsIsHostMap(const char* map);
/* A host map's messages from another room than its files (msg/US/<text>.txt):
 * a boss's speeches are in its own room's text, not always in the room whose
 * ground the mode borrows (Magnus 2.0's in aji_14, the ground aji_00's). */
void portModsSetHostMapText(const char* map, const char* text);
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
/* A zip's files ('/' paths) and their sizes; false and why if it can't be read. */
bool portModsZipList(const std::string& zip, std::vector<std::pair<std::string, uint32_t>>& out, std::string& why);
/* The zip's files under from (a path from the zip's top, "" for all, ending
 * in '/'), written under the folder to (made as needed); progress(bytes so
 * far) after each file, false to stop. Names leaving the folder (.., a drive)
 * are refused. false and why when it couldn't. */
#include <functional>
bool portModsZipExtract(const std::string& zip, const std::string& from, const std::string& to,
                        const std::function<bool(uint64_t)>& progress, std::string& why);
/* What a folder or zip would be as a mod, read without installing it (the
 * mod manager's installs). */
struct PortModProbe {
    bool mod = false;   /* has mod.json (at its top, or in its one top folder) */
    bool pack = false;  /* has lang/pack.txt and no mod.json: a language pack */
    std::string id, name, version, error; /* error: mod.json unreadable, the zip damaged */
    std::string prefix; /* the zip's top folder holding it ("" at the top) */
};
PortModProbe portModsProbe(const std::string& path, bool isZip);
/* Mod index's icon file (mod.json "icon", else icon.png), read; false if none. */
bool portModsReadIcon(int index, std::vector<uint8_t>& out);
/* A game name's address, as a mod's script names it (a function, a variable,
 * file::name for a file's static); nullptr and why if there's none. */
void* portModsGameAddress(const std::string& name, std::string& why);
#endif

#endif
