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

This covers phases M1–M3 of [MODDING_PLAN.md](MODDING_PLAN.md): files, textures, messages, game data and code. Scripts come later.

## A mod

A mod is a folder, or a `.ttydmod` (or `.zip`) file, holding:

```
mod.json     what it is
overlay/     disc files it replaces or adds
textures/    textures it replaces (Dolphin's names)
messages/    messages it adds or replaces
patches/     game data it changes (items, attacks, battle units)
lib/         its code, a library per platform (see "Code mods")
```

A zip may hold everything in one top-level folder (zipping the mod's folder does that). Zip entries must be stored or deflated (any zip tool's default), without encryption or zip64.

[`examples/mods/hello-text`](../examples/mods/hello-text) (messages) and [`examples/mods/shroom-tweaks`](../examples/mods/shroom-tweaks) (data) are complete small mods.

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

### overlay/: disc files

A file at `overlay/<path>` replaces the disc's file at `/<path>`, or adds it if the disc has none. Paths are the disc's, case doesn't matter: `overlay/msg/US/global.txt` replaces the US English messages, `overlay/a/...` models, `overlay/rel/...` an area's data, `overlay/sound/...` sounds and music.

The files are in the disc's own formats, as the game reads them. A file the game has already loaded stays until it loads it again: a map's files on the next room, a battle's on the next battle, `global.txt` at the next start.

### textures/: texture packs

Replacement textures named as **Dolphin** names them: `tex1_{w}x{h}[_m]_{hash}[_{tlut hash}]_{format}.png` or `.dds`, with `_mip1`, `_mip2`... files for mipmaps and `$` in place of a hash to match any. The hashes are Dolphin's (XXH64 of the GameCube texture data), so a **Dolphin texture pack for this game works as it is**: copy its files into a mod's `textures/` folder, or straight into the user folder's `textures/` folder (any folders inside are fine). Replacements can be larger than the original. DDS can be BC1–BC7 compressed.

To make a pack: turn on **Dump textures** (Mods tab, from the next start) or set `TTYD_DUMP_TEXTURES=1`, and play. Every texture drawn that has no replacement yet is saved under its replacement name, in `texture_dumps` in the cache folder (the user folder on desktop; "Open the dump folder" on the Mods tab). Edit one, put it in `textures/`, and it shows the next time it is drawn.

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
| `hookPre`, `hookPost`, `hookReplace`, `original` | hooks on 40 of the game's functions, the ones the GameCube mods hook most: each frame, sequences and maps, messages and menus, items, coins, badges and story flags, shops, battle set-up, units, damage, action commands, audience, rewards, music ([`ttyd_mod/hooks.h`](../port/include/ttyd_mod/hooks.h) lists them with their types). A pre hook gets pointers to the arguments and can skip the function; a post hook can change the result; one mod can replace a function and call the original. Several mods can hook one function, in the mods' order. |
| `onFrame`, `onFrameEnd` | each game frame (60 a second, also at 120 Hz), before and after the game's |
| `onDraw`, `drawText`, `drawRect` | drawing over the picture in the game's 640×480, every frame shown |
| `setMessage` | a message by tag, above every file's |
| `onArea` | an area's code linked or unlinked (`gor`, `hei`...) |
| `onShop` | a shop being set up: its goods, to change in place |
| `onBattleStart` | a battle starting, before its units are made: the encounter, to change |
| `optionInt` | the mod's options (below) |
| `find` | any game function or variable by name |
| `log`, `dataDir`, `gameId` | the log (with the mod's id), a folder for its files, the version |

Game functions are called directly. Game **variables** go through `api->find("name")` on Windows (a DLL can't import data); elsewhere they can be used directly too.

### Options

`mod.json` can declare options; the Mods tab shows them under the mod, and the code reads them with `optionInt` whenever it needs them (they change while the game runs):

```json
"options": [
  {"key": "counter", "label": "Show the coin counter", "type": "bool", "default": true},
  {"key": "damage", "label": "Damage multiplier", "type": "int", "min": 1, "max": 4, "default": 2},
  {"key": "shops", "label": "Shop prices", "type": "choice", "choices": ["Original", "Half"], "default": 1}
]
```

They're kept in `mods/options.txt`.

### Building one

Code mods are built with the port's own build, which gives them the game's headers, flags and layout:

```bash
cmake -S . -B build-pc-us-aurora -DTTYD_LIBRARY_MODS=path/to/mymod
```

- **Building:** `ninja -C build-pc-us-aurora` builds `path/to/mymod/src/*.c` into `build-pc-us-aurora/mods/mymod.dll` (`.so` on Linux and macOS).
- **Packaging:** put the library in the mod's `lib/<platform>/`: `windows-x64`, `linux-x64`, `linux-arm64`, `macos`, `android-arm64`.
- **Versions:** a library is built for one version (US or JP), because the two versions' structs differ. A mod for both ships two builds, or two mods.
- **Starting and stopping:** a mod's code starts when the game starts, and can't be unloaded. Turning a mod with code on or off on the Mods tab takes effect at the next start, and the tab says so.
- **Windows:** the game exports every function and variable, and the build writes the import library mods link against beside the game (`ttyd-us.lib`).

**iOS** can't load code at run time, so there mods are built into the app. `-DTTYD_BUILTIN_MODS=path/to/mymod;...` compiles them into the game (the entry point gets the mod's own name). A built-in mod is listed even without a folder in `mods/`; its `mod.json` comes with it. This works on every platform.

Not yet tested: libraries on Linux, macOS and Android (the code is there: `dlopen` and the game's exported symbols). Windows and built-in mods are tested.

### Things to know

- Hooks run inside the game's logic, at 60 Hz. With 120 Hz on, the frames in between draw the game without running it, so a hook or frame callback that changes the game's state doesn't run there. Only `onDraw` runs on every frame shown.
- A save state holds the game's memory, not a mod's own variables.
- The hook slots are part of the game's code (`include/port_hook.h`, `port/mods/hooks.spec`, `tools/port/gen_hooks.py`). Adding one is a line in `hooks.spec` and a rebuild.

## Order and conflicts

The first mod in the list wins: where two mods replace the same disc file, texture, message or data field, the higher one's is used. Loose textures in the user folder's `textures/` are below every mod. Changes made on the Mods tab apply at once: textures right away, files and messages the next time the game reads them.

`mods/mods.txt` (written by the Mods tab) is one id per line, in order, with `-` in front of a mod that is off. Mods it doesn't name yet are added at the end, turned on.

## Switches

| Variable | Effect |
|---|---|
| `TTYD_NO_MODS` | no mods and no loose textures (tests run the game as it is) |
| `TTYD_DUMP_TEXTURES` | dump textures (see above) |

## How it works

`port/mods/mods.cpp`, started from `main_aurora.c` once the disc is open:

- Disc files: Aurora's DVD overlay (`aurora_dvd_overlay_files`), so every read the game makes, through any path, sees the mod's file.
- Textures: Aurora's texture replacement (`aurora::texture`), which implements Dolphin's names and hashes; folders load from disk, zips through virtual files.
- Messages: `msgSearch` (`src/driver/msgdrv.c`) asks `portModMsgSearch` first.
- Code: `port/mods/mod_api.cpp` (hook slots, the API, loading libraries), `port/src/aurora/mod_game_aurora.c` (the shop and battle services). Each hookable function's definition is renamed `name__body` (`PORT_HOOKABLE`, only with mods), and a generated dispatcher named `name` (`port/include/port/hooks/*.inc`, included at the end of its file) calls the hooks around it. There is no code patching, so it works on iOS. The GameCube build gets the plain name.
- Data: `port/mods/reflect.spec` lists the variables; `tools/port/gen_reflect.py` reads their types from the headers with clang and writes `port/src/aurora/reflect_gen.c` (addresses, fields by name with `offsetof`/`sizeof`, enum constants) and [MOD_DATA.md](MOD_DATA.md). Run it after changing the spec; local CI checks both are current.

Only the Aurora backend has mods (`PORT_MODS`); the headless test build and the GameCube build are unchanged.
