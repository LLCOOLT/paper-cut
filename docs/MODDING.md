Mods
====

The port loads mods from the **mods folder** in the user folder (next to `settings.txt` and the memory cards):

| Platform | Mods folder |
|---|---|
| Windows | `%APPDATA%\ttyd\mods` |
| macOS | `~/Library/Application Support/ttyd/mods` |
| Linux | `~/.local/share/ttyd/mods` |
| iOS | Files > TTYD > Saves > mods |
| Android | the app's internal storage: not reachable yet (an import is still to come) |

The settings page (F1) has a **Mods** tab: each mod with a switch, arrows to change the order, what it changes, and any error. "Open the mods folder" opens it on desktop. The order and switches are kept in `mods/mods.txt`.

This covers files, textures, messages, game data, code (including porting GameCube mods), scripts and [Archipelago](#archipelago).

## A mod

A mod is a folder, or a `.ttydmod` (or `.zip`) file, holding:

```
mod.json     what it is
overlay/     disc files it replaces or adds
textures/    textures it replaces (Dolphin's names)
messages/    messages it adds or replaces
patches/     game data it changes (items, attacks, battle units)
evt/         scripts in the game's script language, and maps (see "Scripts")
lib/         its code, a library per platform (see "Code mods")
```

A zip may hold everything in one top-level folder (zipping the mod's folder does that). Zip entries must be stored or deflated (any zip tool's default), without encryption or zip64.

[`examples/mods/hello-text`](../examples/mods/hello-text) (messages), [`examples/mods/shroom-tweaks`](../examples/mods/shroom-tweaks) (data) and [`examples/mods/hello-room`](../examples/mods/hello-room) (scripts and a new map) are complete small mods.

### mod.json

```json
{
  "id": "com.example.hello",
  "name": "Hello",
  "version": "1.0",
  "author": "You",
  "description": "Renames the Mushroom.",
  "games": ["G8ME01", "G8MJ01"]
}
```

| Field | |
|---|---|
| `id` | required; unique; reverse-DNS style is a good habit |
| `name`, `version`, `author`, `description` | shown on the Mods tab |
| `games` | the versions it works on: `G8ME01` (US), `G8MJ01` (JP). Without it, every version. A mod for another version is listed with a note and not used. |
| `maps` | maps its scripts set up (see "Scripts") |
| `options` | settings for its code (see "Code mods": Options) |

### overlay/: disc files

A file at `overlay/<path>` replaces the disc's file at `/<path>`, or adds it if the disc has none. Paths are the disc's, case doesn't matter: `overlay/msg/US/global.txt` replaces the US English messages, `overlay/a/...` models, `overlay/rel/...` an area's data, `overlay/sound/...` sounds and music.

The files are in the disc's own formats, as the game reads them. A file the game has already loaded stays until it loads it again: a map's files on the next room, a battle's on the next battle, `global.txt` at the next start.

### textures/: texture packs

Replacement textures named as **Dolphin** names them: `tex1_{w}x{h}[_m]_{hash}[_{tlut hash}]_{format}.png` or `.dds`, with `_mip1`, `_mip2`... files for mipmaps and `$` in place of a hash to match any. The hashes are Dolphin's (XXH64 of the GameCube texture data), so a **Dolphin texture pack for this game works as it is**: copy its files into a mod's `textures/` folder, or straight into the user folder's `textures/` folder (any folders inside are fine). Replacements can be larger than the original. DDS can be BC1–BC7 compressed.

To make a pack: turn on **Dump textures** (Mods tab, Texture packs; it takes effect at once, while you play) or set `TTYD_DUMP_TEXTURES=1`, and play. Every texture drawn that has no replacement yet is saved under its replacement name, in `texture_dumps` in the cache folder (the user folder on desktop; "Open the dump folder" on the Mods tab). Edit one, put it in `textures/`, press **Reload the packs**, and it shows at once.

The Texture packs section also shows a pack's **coverage**: how many textures the packs replace, and of the textures in the dump folder (each one the game drew while dumping was on, unreplaced then) how many a pack replaces now. Play a while with dumping on to see more of the game; the count follows as files are added.

**Name collisions.** A name is the texture's size, format and a hash of its pixels (and palette), as Dolphin makes it, so two files with the same name replace the same texture and only one is used: between mods the one higher in the list, a mod's before the textures folder's, and inside one folder either of them (keep one). The section lists the names given by more than one file and the packs giving them. Textures with the same pixels share a name, so one file replaces every use of them; a name with `$` for a hash covers every texture it stands for.

**Textures for some maps only.** A folder `textures/@<maps>/` in a mod holds a set that applies only while the map's name matches `<maps>`: names separated by commas, with `~` for any run of letters (a folder name can't hold `*`) and `?` for one, so `textures/@muj_0~,dou_00/` re-skins Keelhaul Key's jungle and the grotto's mouth and leaves the same textures alone everywhere else. The set is swapped in as the map changes to one it matches and out as it leaves. A mod's map sets come above every mod's global textures and above the game's own map sets (Super Luigi's volcano), the higher mod first. (The loose `textures/` folder in the user folder has no map sets: a folder there named `@...` is a plain pack.)

Not every image can be replaced this way: pictures the game draws into itself (paper effects, captures) have no fixed texture.

### messages/: text

Messages are added or replaced by **tag**, without replacing the whole message file:

```
Lines before the first tag are comments.

[in_kinoko]
Mushroom (modded)

[msg_kinoko]
A modded mushroom.
Replenishes 5 HP.
```

Each `[tag]` line starts a message; its text runs to the next tag (the last line break before it isn't part of it). The text uses the game's own control tags, e.g. `<p>` for a new box, `<k>` to wait for A.

- `messages/*.txt`: for every language.
- `messages/<LANG>/*.txt`: for one: `US`, `DE`, `FR`, `SP`, `IT` (and `JP`) on the US build; `JP` and `EN` (its English mode) on the JP build.

Write the files in UTF-8. On the US build, accented letters are converted to the game's encoding; characters it has no glyph for become `?`. On the JP build the text is used as it is, so Japanese text must be in Shift-JIS.

The game's message tags can be found in the disc's `msg/` files (tag and text alternate, separated by zero bytes).

### patches/: game data

The game's own tables, changed by name, one change per line in `patches/*.txt`:

```
# Mushrooms heal 10 HP and cost 3 coins
itemDataTable[ITEM_MUSHROOM].hp_restored = 10
itemDataTable[ITEM_MUSHROOM].buy_price = 3

itemDataTable[*].sell_price = 1          # [*]: every element
unitdata_Mario.maxHp = 15                 # a battle unit
itemDataTable[ITEM_MUSHROOM].name = "in_ultra_kinoko"   # text fields take "text" (a message tag here)
```

- **Targets** are the decomp's names: a table, an index, then fields (`.field`, `[index]`), as far down as needed. Indexes are numbers or the decomp's constants (`ITEM_MUSHROOM`). [MOD_DATA.md](MOD_DATA.md) lists every table and field that can be changed: items and badges (`itemDataTable`), every item, badge and move attack (`ItemWeaponData_*`, `marioWeapon_*`, the partners' weapons), and Mario's and the partners' battle units.
- **Values** are whole numbers (`12`, `-3`, `0x1F`), numbers (`1.5`), constants, `true`/`false`, or `"text"` for text fields (message tags, sound names). A value that doesn't fit the field (300 in a one-byte field) is refused.
- Pointers other than text can't be changed.
- A line that can't be applied is skipped; the Mods tab shows the first one, with its file and line.

Patches apply when the game starts and whenever the mods change; turning a mod off puts the original values back. A save state keeps the data as it was when it was made.

The data an area brings with it (its shops, its enemies' units) isn't in the tables: most of it is private to each area's code. A code mod reaches it where the game uses it: `onShop` and `onBattleStart` below.

## Scripts

Most of what happens in TTYD is the game's own script language (evt): a map's set-up, its people and what they say, doors, cutscenes, shops. A mod can ship scripts in `evt/*.evt`, written in the game's script macros exactly as the decomp's sources write them, so a script can be copied from any map's source (`rels/gor/src/gor_01.c`) and changed. They're assembled when the game starts (no compiler, on every platform including iOS).

```c
// evt/gor_99.evt
EVT_BEGIN(gor_99_init)
    CALL(evt_mod_log, STRING("entered gor_99"))
    CALL_CHILD(gor_01_init_evt)          // the game's own set-up of gor_01
    IF_INT_GREATER_OR_EQUAL(GSW(0), 10)
        SPAWN_EVENT(gor_99_hello)
    END_IF
    RETURN
EVT_END()

EVT_BEGIN(gor_99_hello)
    WAIT_MS(800)
    CALL(evt_mario_key_onoff, 0)
    CALL(evt_msg_print, 0, STRING("hello_room"), 0, 0)
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()
```

- **Commands** are the macros of [`include/evt/evt_cmd.h`](../include/evt/evt_cmd.h) (`SET_VALUE`, `IF_INT_EQUAL`, `CALL`, `LOOP`, `SPAWN_EVENT`...) and the opcodes by their own names (`SET`, `USER_FUNC`...), so ttyd-tools' names mostly work too. With no arguments the parentheses are optional (`RETURN`, `END_IF`).
- **Values:** numbers (`10`, `0x10`, `-1`), arithmetic, `LW(n)` `GW` `LF` `GF` `LSW` `GSW` `LSWF` `GSWF` `UW` `UF`, `FLOAT(1.5)`, strings (`STRING("...")` or plain `"..."`), the game headers' constants (`ITEM_MUSHROOM`, about 3,500 of them), the mod's own scripts by name, and any game function or variable by name: the game's (`evt_msg_print`, `gor_01_init_evt`) and a file's statics too. Where a static's name is in more than one file, write `file::name` (`gor_01::evt_door`).
- **Port helpers:** `CALL(evt_mod_log, "text")` writes a line to the log.
- **Maps:** `mod.json`'s `"maps"` gives a map its init script, the script the game runs when the map is entered. A new map is named after an area (`gor_99` belongs to Rogueport, `gor`) and reads another map's files with `"files"`; a map of the game's gets the mod's script in place of its own (which the mod's can still run, `CALL_CHILD(gor_01_init_evt)`).

```json
"maps": {
  "gor_99": {"init": "gor_99_init", "files": "gor_01"},
  "gor_01": "my_gor_01_init"
}
```

The debug warp reaches a new map (`TTYD_MAP=gor_99`); in the game, a script of a map next to it can send Mario there (the game's `evt_bero` doors and `evt_seq` map changes).

A script that doesn't assemble, or a name that isn't found, is written to the log with its file and line, and on the Mods tab. The tables come from the decomp's headers ([`tools/port/gen_evt_table.py`](../tools/port/gen_evt_table.py), `port/mods/evt_table_gen.h`); the assembler is [`port/mods/evt_asm.cpp`](../port/mods/evt_asm.cpp).

## Code mods

A mod can carry code: C built against the game's own headers, so it sees the game exactly as the decomp names it (`pouchGetPtr()`, `BattleUnit`, `itemDataTable`). [`examples/mods/hello-native`](../examples/mods/hello-native) is one: it draws a coin counter, halves shop prices, multiplies Mario's damage, and renames the Mushroom.

```c
#include "ttyd_mod/ttyd_mod.h"

static const TtydModApi* api;

static void damagePost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part,
                       BattleWeapon* weapon, u32* flags, u32 unused) {
    if (attacker->mAlliance == 0) {          /* Mario's side */
        *result *= api->optionInt("damage", 2);
    }
}

TTYD_MOD_ENTRY(a) {
    api = a;
    api->hookPost(TTYD_HOOK_BattleCalculateDamage, (void*)damagePost);
    return 0;
}
```

### What the code gets

[`ttyd_mod/ttyd_mod.h`](../port/include/ttyd_mod/ttyd_mod.h) is the API, passed to the entry point once at start:

| | |
|---|---|
| `hookPre`, `hookPost`, `hookReplace`, `original` | hooks on 61 of the game's functions, the ones the GameCube mods hook most: each frame, sequences and maps, messages and menus, items, coins, badges and story flags, shops, battle set-up, units, damage, action commands, audience, rewards, music ([`ttyd_mod/hooks.h`](../port/include/ttyd_mod/hooks.h) lists them with their types). A pre hook gets pointers to the arguments and can skip the function; a post hook can change the result; one mod can replace a function and call the original. Several mods can hook one function, in the mods' order. |
| `onFrame`, `onFrameEnd` | each game frame (60 a second, also at 120 Hz), before and after the game's |
| `onDraw`, `drawText`, `drawRect` | drawing over the picture in the game's 640×480, every frame shown |
| `setMessage` | a message by tag, above every file's |
| `onArea` | an area's code linked or unlinked (`gor`, `hei`...) |
| `onShop` | a shop being set up: its goods, to change in place |
| `onBattleStart` | a battle starting, before its units are made: the encounter, to change |
| `optionInt` | the mod's options (below) |
| `find` | any game function or variable by name |
| `log`, `dataDir`, `gameId` | the log (with the mod's id), a folder for its files, the version |

Game functions are called directly. Game **variables** go through `api->find("name")` on Windows (a DLL can't import data); elsewhere they can be used directly too. In C++, [`ttyd_mod/vars.h`](../port/include/ttyd_mod/vars.h)'s `ttydGameVar<T>("name")` binds a reference once, when the library loads. A file's `static` variables and functions are exported too, as a pointer named `port_static__<path>__<name>` (`port_static__src_manager_cardmgr__wp` for `cardmgr.c`'s `wp`); a REL's symbols carry its prefix (`rel_tou__...`).

### Options

`mod.json` can declare options; the Mods tab shows them under the mod, and the code reads them with `optionInt` whenever it needs them (they change while the game runs):

```json
"options": [
  {"key": "counter", "label": "Show the coin counter", "type": "bool", "default": true},
  {"key": "damage", "label": "Damage multiplier", "type": "int", "min": 1, "max": 4, "default": 2},
  {"key": "shops", "label": "Shop prices", "type": "choice", "choices": ["Original", "Half"], "default": 1}
]
```

Each option may also have a `"note"` (shown under it: what it does) and `"restart": true` (the code reads it only at the game's start, and the tab says "From the next start"). The Mods tab shows a **Default** button beside an option that isn't at its default. They're kept in `mods/options.txt`.

### Building one

Code mods are built with the port's own build, which gives them the game's headers, flags and layout:

```bash
cmake -S . -B build-pc-us-aurora -DTTYD_LIBRARY_MODS=path/to/mymod
```

- **Building:** `ninja -C build-pc-us-aurora` builds `path/to/mymod/src/*.c` and `*.cpp` into `build-pc-us-aurora/mods/mymod.dll` (`.so` on Linux and macOS). A mod folder with its own `CMakeLists.txt` is added as a subdirectory instead, and calls `ttyd_add_mod(<target> <sources>)` itself.
- **Packaging:** put the library in the mod's `lib/<platform>/`: `windows-x64`, `linux-x64`, `linux-arm64`, `macos`, `android-arm64`.
- **Versions:** a library is built for one version (JP, US or EU), because the versions' structs differ. A mod for more than one ships a build per version (`lib/<platform>/<game ID>/`), or a mod per version.
- **Starting and stopping:** a mod's code starts when the game starts, and can't be unloaded. Turning a mod with code on or off on the Mods tab takes effect at the next start, and the tab says so.
- **Windows:** the game exports every function and variable, and the build writes the import library mods link against beside the game (`ttyd-us.lib`).

**iOS** can't load code at run time, so there mods are built into the app. `-DTTYD_BUILTIN_MODS=path/to/mymod;...` compiles them into the game (the entry point gets the mod's own name). A built-in mod is listed even without a folder in `mods/`; its `mod.json` comes with it. This works on every platform.

Not yet tested: libraries on Linux, macOS and Android (the code is there: `dlopen` and the game's exported symbols). Windows and built-in mods are tested.

### Things to know

- Hooks run inside the game's logic, at 60 Hz. With 120 Hz on, the frames in between draw the game without running it, so a hook or frame callback that changes the game's state doesn't run there. Only `onDraw` runs on every frame shown.
- A save state holds the game's memory, not a mod's own variables.
- The hook slots are part of the game's code (`include/port_hook.h`, `port/mods/hooks.spec`, `tools/port/gen_hooks.py`); see "Hook slots" below.

### Hook slots

[`ttyd_mod/hooks.h`](../port/include/ttyd_mod/hooks.h) has a slot, `TTYD_HOOK_<name>`, for each game function a mod can hook: about 1,600, generated from the decomp. [`port/mods/hooks.spec`](../port/mods/hooks.spec) lists them:

- **Hand-picked** names (the first 61): the functions the GameCube mods hook most and the ones the port's own services stand on. Each must be hookable, or the generator fails.
- **`@files` sets**: every function a mod can name defined in those sources: battles (`src/battle`, units, action commands, Special moves), the field (Mario, his motions, partners, NPCs, items, objects), events (the scripts' user functions, menus, sequences) and the data tables. "Can name" is: not `static` or `inline`, declared in a game header a C++ mod can include ([`ttyd_mod/game_all.h`](../port/include/ttyd_mod/game_all.h)), not variadic, and the same signature in all three versions. `python tools/port/gen_hooks.py --report` lists what a set leaves out, and why.
- **`-name`** leaves out a hot function. The cost of a slot is a load and a branch per call while nothing hooks it (and the function no longer inlines into its callers in its file); for the functions called many times every frame (the accessors `marioGetPtr`, `BattleAudienceGetPtr`, `BtlUnit_GetStatus`...) that buys nothing, so they're out. The animation, display-list and event-interpreter files (`animdrv`, `dispdrv`, `evtmgr_cmd`...) aren't in any set.

**Slot numbers never change.** [`port/mods/hook_slots.txt`](../port/mods/hook_slots.txt) gives each function its number once: new ones go at the end, and a function that leaves the list keeps its number unused (a hook on it is refused, with a line in the log). A mod built against an older list keeps hooking the same functions. Keep the file in the repository.

**Extending the list:** add an `@files` line (or a name) to `hooks.spec`, configure the three Aurora builds (`build-pc-jp-aurora`, `build-pc-us-aurora`, `build-pc-eu-aurora`: the generator reads their compile flags), run `python tools/port/gen_hooks.py --apply` (it marks the definitions `PORT_HOOKABLE`, unmarks the ones that left, and writes the dispatchers), and rebuild. `GEN_HOOKS_CACHE=<folder>` keeps clang's dumps between runs while editing the list. A function the sets can't take yet: one without a prototype in a mod-includable header (about 1,370 of them: declare it in its header first), one whose definition's first line isn't `type name(`, and the RELs' (`rels/`) functions, which load and unload with their area.

**Measuring:** configure a build with `-DCMAKE_C_FLAGS_RELWITHDEBINFO="-O2 -g -DNDEBUG -DPORT_HOOK_PROFILE=1"` (the same for `CMAKE_CXX_FLAGS_RELWITHDEBINFO`) and run it with `TTYD_HOOK_PROFILE=<file>`: every 600 frames the file gets each slot's calls per frame, the most first. Put what is called more than a few times a frame in the `-name` list.

## Porting a GameCube mod

GameCube mods (the [Practice Codes](https://github.com/Zephiles/TTYD-Practice-Codes), [Infinite Pit](https://github.com/jdaster64/ttyd-infinite-pit), most mods built on [ttyd-tools](https://github.com/PistonMiner/ttyd-tools)) are C++ written against the game's machine code: their own copies of the game's structs at GameCube offsets, functions found by address from a symbol list, hooks that patch instructions. The port's tools turn such a mod's source into a code mod for the PC build, with the mod's code itself mostly unchanged. The source stays wherever its license allows; nothing of it goes in this repository.

The steps, from a clean checkout of the mod (the Practice Codes need about a dozen hand edits; see the end):

1. **Fields.** [`tools/port/mod_fields.py`](../tools/port/mod_fields.py) lays out the mod's structs and the decomp's for the GameCube with clang and matches fields by offset and size, then renames every access in the mod's sources (from clang's AST, so `unk_8` in one struct is told from another): `player->playerPosition` becomes `player->position`. Where the decomp's field has another type (its `s16` for the mod's `ItemId`, `Vec` for `Vec3`), the access is wrapped so the mod still sees its own type: `ttydAs<ItemId[121]>(pouch->items)`. A word the mod split differently becomes `ttydPart<int32_t, 0>(item->timer)` (the GameCube was big-endian). Give `--alias ModStruct=DecompStruct` for structs named differently; `--report` lists the rest.
2. **Headers.** [`tools/port/mod_headers.py`](../tools/port/mod_headers.py) turns the mod's copies of the game's headers into shims over the decomp's: a struct the decomp has is the decomp's; a function or variable is found by its GameCube address (the mod's `.lst` against `config/<version>/symbols.txt`; a REL's by module, section and offset, with `--rel-dir` for the module ids) and named as the game exports it (`--exports` the build's `ttyd_exports.def`); a variable becomes a reference bound at load; the mod's enums map onto the decomp's by value; ttyd-tools' `EVT_DECLARE*` and its script macros (`--evt-header`) build the PC's 64-bit script words. The mod's sources get the same for their own `extern` lines: `extern int32_t evt_x[];` over a game script is the PC's script words, over a game function (its instructions, to patch) words of nowhere (`TTYD_GC_CODE`); a user function's argument array (`evt->args = args`) is script words too. With `--widen-pointer-casts`, pointer arithmetic done in `uint32_t` is done in `uintptr_t`, and a word's bytes passed as a color are put in the GameCube's order (`ttydBytesBE`). With `--gc-lowmem`, a fixed low-memory address (`0x80003220`, where patchers left settings and mailboxes) is the port's stand-in for that memory (`TTYD_GC_ADDR`); with `--gc-data`, a fixed address of the game's data is the same field on the PC (`TTYD_GC_DATA`, translated by [`tools/port/gc_offsets.py`](../tools/port/gc_offsets.py)). A namespace of the mod named like a game type (`namespace BattleUnitType` of constants) is renamed `_ns`. Size `static_assert`s go.
3. **Assembly.** [`tools/port/mod_asm_stubs.py`](../tools/port/mod_asm_stubs.py) gives each `.global` of the mod's PowerPC assembly a stub that logs if it runs: they're what the mod's instruction patches branched to, and the port skips those (below). A label over data (`.4byte`, `.rept`, strings) is that data instead, with twice the room for tables the C++ lays out with the PC's pointers.
4. **Build.** The mod's own `CMakeLists.txt` (added with `-DTTYD_LIBRARY_MODS=path`) calls `ttyd_add_mod` and force-includes [`ttyd_mod/game_loose.h`](../port/include/ttyd_mod/game_loose.h) (every game header, plus C's argument conversions: an `int` where the decomp has an enum, a pointer where it passes a small struct by value, and arguments in the PowerPC ABI's order, which passed floats apart from integers, so a mod's `iconDispGx(float scale, Vec* pos, ...)` reaches the game's `iconDispGx(Vec* pos, ..., f32 scale)`) and `ttyd_mod/vars.h`. Its `patch.h` is replaced by one on [`ttyd_mod/compat.h`](../port/include/ttyd_mod/compat.h): `hookFunction(fn, dest)` is `TTYD_HOOK_FUNCTION`, a hook slot's replacement that returns the game's own function as the trampoline did; `writeBranch`, `applyAssemblyPatch` and the like log and do nothing (a script word, which is data, is still written; a write to code or to a GameCube address the mod kept as a number is skipped).

What the tools can't port, and why:

| | |
|---|---|
| Instruction patches | The PC runs native code. A patch that changed what a function does needs a hook slot on that function (a line in `port/mods/hooks.spec`); the mod's function goes in its place with `hookFunction`. |
| Raw offsets | `*(int32_t *)(ptr + 0x23C)` is a GameCube offset; on the PC the struct may be laid out otherwise (8-byte pointers). Where no pointer comes before the field it's the same; otherwise use the decomp's field. |
| The GameCube's memory | The heaps' chunk lists (the PC's allocator is Aurora's), RAM ranges (`0x80000000`), the FIFO register, the EXI bus, REL module images: features built on them are left out on the PC. |
| Compiler literals | `@863`, `float_0p125_80420084`: the GameCube's constant pool, which a mod patched in place. A variable the game doesn't export is bound to zeroes and listed in the log at start. |
| Structs over low memory | A mod's struct laid over a mailbox (`reinterpret_cast<Settings *>(0x80003220)`) gets the PC's layout, so a pointer field in it moves the fields after it. Keep such a field a 32-bit GameCube address (`TTYD_GC_ADDR` reads it) and `static_assert` the offsets. |
| Its own RELs | Code the mod shipped only as binary RELs (no source) isn't there; what it declared from them binds to zeroes. |
| When it starts | A REL loader ran the mod once the game was set up; a library starts before the game's first frame. Code that calls the game at start (fonts, the disc) belongs in an `onFrame` callback that runs once. |

The Practice Codes as ported locally: the menu, its pages and the displays work; battles run with its hooks. The TTYD Archipelago mod (TTYDAP) as ported locally builds and runs a session (see [Archipelago](#archipelago)); its instruction patches and its hooks on functions without a slot are skipped (the log lists each), and its enemy and boss randomizers, which live in its binary `custom.rel`, are absent. Per mod, a small script of exact text edits after the tools (`port/manual.py` in the mod's checkout) holds the hand-ported pieces, so the conversion can be rerun from a clean checkout.

## Archipelago

[Archipelago](https://archipelago.gg) is a multiworld randomizer; TTYD's part is the [TTYD apworld](https://github.com/jamesbrq/ArchipelagoTTYD) (seeds, logic, the client) and the [TTYD AP mod](https://github.com/jamesbrq/TTYDAP) (the game side). On the GameCube its patcher writes a seed into a disc image and its client reads and writes Dolphin's memory. The US build does both itself (Windows, macOS, Linux, Android; iOS has no file chooser yet):

- **The seed** (the `.apttyd` file the apworld generates for a slot) is applied at the game's start, as the patcher applies it to a disc: each location's item goes into the script, table or shop that gives it (721 places; local CI checks the 629 in RELs against the disc), shop prices and sell prices, the seed's options in the low memory the mod reads, and its files (`enemies.bin`, `bosses.bin`, `tracker.bin`, `msg/US/desc.txt`) over the disc's. The mod's own messages (`msg/US/mod.txt`) come from the installed `ttyd.apworld`: `$TTYD_APWORLD_FILE`, beside the seed, or Archipelago's `custom_worlds` folder.
- **The server.** A built-in client (WebSocket: `ws://` everywhere, `wss://` on Windows) connects to a room: items the slot receives go to the game, a location's check goes out when its flag is set, the goal and DeathLink both ways, and the current room for trackers.
- **The game side** is the TTYD AP mod, ported as a code mod ([above](#porting-a-gamecube-mod)): it gives the received items, shows who sent them and runs the seed's options. It's GPL, so it isn't part of this repository; build it from its source with the porting tools and add it to the mods folder.

The settings page's **Archipelago** tab chooses the seed (it applies at the next start) and holds the server's address, the slot (the seed's player by default) and the password, with Connect and Disconnect; they're kept in `archipelago.json` in the user folder, and the game connects at start once there's a server. `$TTYD_AP_SEED` and `$TTYD_AP_SERVER` override them for a run.

The data comes from the apworld: [`tools/port/gen_ttyd_ap.py`](../tools/port/gen_ttyd_ap.py) reads its location, item and tattle tables and writes `port/archipelago/ttyd_ap_data.h`, each location's places translated from GameCube file offsets to the PC build's symbols and fields by [`tools/port/gc_offsets.py`](../tools/port/gc_offsets.py) (`--check` with `$TTYD_APWORLD` in local CI keeps it current). `TTYD_AP_VERIFY=1` compares every place with the disc's REL at start. Local CI also plays a session against a stand-in server ([`tests/port/ap_mock_server.py`](../tests/port/ap_mock_server.py), with a seed from [`tests/port/ap_test_seed.py`](../tests/port/ap_test_seed.py)); `TTYD_AP_NO_MOD=1` lets a test run without the mod.

## Order and conflicts

The first mod in the list wins: where two mods replace the same disc file, texture, message or data field, the higher one's is used. Loose textures in the user folder's `textures/` are below every mod. Changes made on the Mods tab apply at once: textures right away, files and messages the next time the game reads them.

The Mods tab (also in the launcher's settings) shows, for each mod, **what it changes** (its files, textures, messages, data changes, the hook slots its code took, its maps) and **where another enabled mod changes the same thing**: "Also changed by B: 3 files (...). Those of B are used (it is higher in the list)." Two mods' pre and post hooks on one slot both run; of two replacements only the higher mod's does. Data changes meet where they write the same place, even under different names (`itemDataTable[*]` and `itemDataTable[ITEM_MUSHROOM]`).

`mods/mods.txt` (written by the Mods tab) is one id per line, in order, with `-` in front of a mod that is off. Mods it doesn't name yet are added at the end, turned on.

## Switches

| Variable | Effect |
|---|---|
| `TTYD_NO_MODS` | no mods and no loose textures (tests run the game as it is) |
| `TTYD_DUMP_TEXTURES` | dump textures (see above) |
| `TTYD_AP_SEED`, `TTYD_AP_SERVER` | an Archipelago seed and server for this run (see [Archipelago](#archipelago)) |
| `TTYD_APWORLD_FILE` | the `ttyd.apworld` to take the TTYD AP mod's messages from |
| `TTYD_AP_VERIFY` | check every place a seed's items go against the disc's RELs at start |
| `TTYD_AP_NO_MOD` | Archipelago without the TTYD AP mod (tests): a file is played whenever the game's sequence is one |

## How it works

`port/mods/mods.cpp`, started from `main_aurora.c` once the disc is open:

- Disc files: Aurora's DVD overlay (`aurora_dvd_overlay_files`), so every read the game makes, through any path, sees the mod's file.
- Textures: Aurora's texture replacement (`aurora::texture`), which implements Dolphin's names and hashes; folders load from disk, zips through virtual files.
- Messages: `msgSearch` (`src/driver/msgdrv.c`) asks `portModMsgSearch` first.
- Code: `port/mods/mod_api.cpp` (hook slots, the API, loading libraries), `port/src/aurora/mod_game_aurora.c` (the shop and battle services). Each hookable function's definition is renamed `name__body` (`PORT_HOOKABLE`, only with mods), and a generated dispatcher named `name` (`port/include/port/hooks/*.inc`, included at the end of its file) calls the hooks around it. There is no code patching, so it works on iOS. The GameCube build gets the plain name.
- Data: `port/mods/reflect.spec` lists the variables; `tools/port/gen_reflect.py` reads their types from the headers with clang and writes `port/src/aurora/reflect_gen.c` (addresses, fields by name with `offsetof`/`sizeof`, enum constants) and [MOD_DATA.md](MOD_DATA.md). Run it after changing the spec; local CI checks both are current.

- Scripts: `port/mods/evt_asm.cpp` assembles a mod's `evt/*.evt` when mods apply; names resolve through the executable's exports (`port/statics_<version>.txt` indexes the statics). A mod's map is a `MapData` of the host's, returned by a post hook on `mapDataPtr` (`port/src/aurora/mod_game_aurora.c`), and `DVDMgrOpen` (`src/manager/dvdmgr.c`) reads a new map's files from the map it names.
- Statics: [`tools/port/gen_statics.py`](../tools/port/gen_statics.py) lists each game source's file-scope statics (`port/statics_<version>.txt`, from clang's AST of the Aurora build); the build compiles a source with statics through a wrapper that includes it and exports a pointer to each. Run it after adding or renaming a static; local CI checks the lists are current.

Only the Aurora backend has mods (`PORT_MODS`); the headless test build and the GameCube build are unchanged.
