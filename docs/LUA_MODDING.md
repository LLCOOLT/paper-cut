Lua mods
========

A mod can carry Lua scripts: a `lua/` folder with a `main.lua`. They run inside the game, see its events (each frame, each map, each battle, each hit) and can read and change the game through a small API. They are a lighter way than [code mods](MODDING.md#code-mods): nothing to compile, the same mod works on every platform and every version of the game, and it is **sandboxed**, so a Lua mod from someone else can't touch your files or the network.

Everything else about a mod (its `mod.json`, the mods folder, the Mods tab, the order) is as in [MODDING.md](MODDING.md); a mod may have Lua and any of the other parts together.

```
my-mod/
  mod.json
  lua/
    main.lua        runs when the game starts
    hud/panel.lua   require "hud.panel"
```

```lua
-- lua/main.lua
local hits = 0
ttyd.on("damage", function(attacker, target, damage)
  hits = hits + 1
  return damage          -- (return a number to change it)
end)
ttyd.on("hud", function()
  hud.text(20, 20, "Hits: " .. hits)
end)
```

Three complete small mods: [`examples/mods/lua-superguard-counter`](../examples/mods/lua-superguard-counter) (events and the HUD), [`examples/mods/lua-enemy-hp`](../examples/mods/lua-enemy-hp) (battle units, a module of its own) and [`examples/mods/lua-glass-cannon`](../examples/mods/lua-glass-cannon) (changing damage, challenge rules and HP, an option).

## Running, errors and the console

`main.lua` runs once when the game starts; it sets up its handlers with `ttyd.on`. Each enabled mod has a Lua state of its own: mods don't see each other's variables.

**Lua console**: F4 (Controls > Bindings, "Lua console"), or the Mods tab's *Lua mods* section. It shows what the mods `print` and their errors, with **Reload** (every Lua mod started again from its files).

An error (a bug, a call that took too long, too much memory) **stops that mod alone**: its message and where it happened go to the console and the log, the Mods tab shows it, and a notice says so for a moment. The game goes on. Fix the file and it starts again by itself (below), or Reload.

**Hot reload**: a mod that is a folder (not a zip) starts again by itself within half a second of one of its `lua/` files changing. Its Lua state starts over (variables are lost). Not during online play.

## The sandbox

| | |
|---|---|
| Libraries | `string` (no `string.dump`), `table`, `math`, `utf8`, `coroutine`, and the base functions without `dofile` and `loadfile`; `load` takes text only (no bytecode) |
| Not there | `io`, `os`, `package`, `debug`: no files, no network, no clock, no C modules |
| `require` | `require "a.b"` runs the mod's own `lua/a/b.lua` once and returns what it returned |
| `print` | to the Lua console and the log |
| Memory | 32 MB per mod |
| Time | each call into a mod (its `main.lua`, each event's handler) may run 1 million Lua instructions (`main.lua` and what it requires, 10 million). Past that it errors on every instruction, so a `pcall` can't keep it going. |
| `math.random` | seeded from the mod's id: the same numbers every run (and for both players online). `math.randomseed(n)` seeds it again; `math.randomseed()` goes back to the mod's seed. |

Lua is 5.4 ([lua.org manual](https://www.lua.org/manual/5.4/)).

## Events

`ttyd.on(event, fn)` adds a handler, `ttyd.off(event, fn)` takes it away (`ttyd.off(event)` all of the event's). Handlers run in the mods' order (the Mods tab's, the first first). Battle units are given by id (see `battle`).

| Event | Arguments | |
|---|---|---|
| `frame` | | each game frame, before the game's (60 a second) |
| `frame_end` | | each game frame, after the game's |
| `hud` | | each game frame, last: the time to draw (`hud`) |
| `map_load` | map | a map was entered (`"gor_01"`); within a frame of the game's own change |
| `map_unload` | map | the map before it, just before its `map_load` |
| `area_enter`, `area_leave` | area | an area's code was loaded or is about to be unloaded (`"gor"`) |
| `battle_start` | | a battle is setting up its units |
| `unit_spawn` | unit, kind, alliance | a unit was made (Mario, the partner, each enemy, stage objects): its stats can still be changed |
| `turn_start` | turn | a battle's turn begins (1, 2...) |
| `damage` | attacker, target, damage | a hit's HP damage, after the game worked it out; **return a number to change it** (each handler gets the one before's) |
| `fp_damage` | attacker, target, damage | the same for FP |
| `guard` | unit, superguard | Mario or the partner guarded (superguard: `true` for a Superguard) |
| `guard_result` | result | a guard's result (`battle.GUARD_NONE`, `battle.GUARD`, `battle.SUPERGUARD`); return another to change it |
| `command_level` | level | an attack command's difficulty (0-6, higher is harder); return another to change it |
| `battle_end` | | the battle ends |
| `item_get`, `item_lose` | item | an item went into the pouch, or out of it (used, sold, thrown away) |
| `badge_equip` | badge, on | a badge was put on (`on` true) or taken off |

Numbers changed by `damage`, `guard_result` and `command_level` are whole numbers; a handler that returns nothing leaves the number as it is.

## The API

Item, partner and unit kinds are the game's own numbers; `ttyd.const(name)` gives them by the decomp's names: `ttyd.const("ITEM_MUSHROOM")`, `ttyd.const("PARTNER_GOOMBELLA")`, `ttyd.const("UNIT_GOOMBA")` (nil for a name it doesn't know). Functions that change the game do nothing when changes aren't allowed (online, below).

### ttyd

| | |
|---|---|
| `ttyd.on(event, fn)`, `ttyd.off(event [, fn])` | events (above) |
| `ttyd.log(...)` | the same as `print` |
| `ttyd.option(key [, default])` | one of the mod's options (mod.json `"options"`, the Mods tab), as a number |
| `ttyd.const(name)` | a constant of the game's headers |
| `ttyd.message(tag, text)` | a message of the game's replaced (UTF-8; nil text: the game's own again) |
| `ttyd.frame()` | frames since the Lua mods started |
| `ttyd.online()` | an online session runs |
| `ttyd.can_change()` | changes to the game count now (not in an online session whose Lua mods differ) |
| `ttyd.game`, `ttyd.mod_id`, `ttyd.api` | the version (`"G8ME01"`), the mod's id, the API's version (1) |

### player

| | |
|---|---|
| `player.stats()` | a table: `hp`, `max_hp`, `fp`, `max_fp`, `coins`, `sp`, `max_sp`, `level`, `rank`, `star_points`, `bp`, `max_bp`, `star_pieces`, `shine_sprites`, `jump_level`, `hammer_level` |
| `player.set_hp(n)`, `set_fp(n)`, `set_max_hp(n)`, `set_max_fp(n)` | Mario's |
| `player.set_coins(n)`, `player.add_coins(n)` | coins (0-999) |
| `player.partner()` | the partner out now (0 none) |
| `player.partner_stats([id])` | `id`, `joined`, `hp`, `max_hp`, `level` (the partner out now without an id) |
| `player.set_partner_hp(id, n)` | |

### items

| | |
|---|---|
| `items.count(id)` | how many Mario has (items, key items, badges) |
| `items.give(id)`, `items.take(id)` | one into or out of the pouch; true when it fit or was there |
| `items.held()`, `items.stored()`, `items.key_items()`, `items.badges()`, `items.equipped()` | lists of ids |

### flags

The story's saved words and flags, as the game's scripts name them: `GSW(n)` (0-2047) and `GSWF(n)` (0-8191).

| | |
|---|---|
| `flags.gsw(n)`, `flags.set_gsw(n, value)` | `GSW(0)` is the story's progress |
| `flags.gswf(n)`, `flags.set_gswf(n [, on])` | |

### map

`map.name()` (`"gor_01"`), `map.area()` (`"gor"`).

### battle

Units are given by id, as the events give them; `battle.units()` lists the battle's.

| | |
|---|---|
| `battle.active()` | a battle runs |
| `battle.turn()` | its turn (0 before the first) |
| `battle.units()` | the units' ids |
| `battle.unit(id)` | a table: `id`, `kind` (`UNIT_*`), `alliance` (`battle.MARIO_SIDE` 0, `battle.ENEMY_SIDE` 1, 2 stage objects), `level`, `hp`, `max_hp`, `fp`, `max_fp`, `x`, `y`, `z`; nil when there's none |
| `battle.mario()`, `battle.partner()` | their ids (-1 when not in the battle) |
| `battle.set_hp(id, n)`, `battle.set_max_hp(id, n)`, `battle.set_fp(id, n)` | |
| `battle.GUARD_NONE`, `battle.GUARD`, `battle.SUPERGUARD` | `guard_result`'s values |

### hud

Drawn over the game, in the game's own 640 x 480 coordinates (the picture's, whatever the window's size). Draw in the `hud` event: what was drawn in a frame shows until the next frame's.

| | |
|---|---|
| `hud.text(x, y, text [, size [, color [, shadow]]])` | size in the game's pixels (18), color `0xRRGGBBAA` (white), a shadow unless `shadow` is false |
| `hud.rect(x, y, w, h [, color])` | a filled box (`0x000000A0`) |
| `hud.width`, `hud.height` | 640, 480 |

### rules

The challenge rules (Settings > Challenges, [MODES.md](MODES.md)) set by the mod while it runs, above the settings: `rules.set("challenge_ohko", 1)`, `rules.set(key, nil)` gives the setting back; `rules.get(key)` is the mods' value or nil. The keys: `challenge_ohko` (0, 1), `challenge_guard` (0 off, 1 no Guard, 2 no Guard or Superguard), `challenge_commands` (0-2), `challenge_no_battle_items` (0, 1). The first mod in the order wins.

## Online

In online play both games play in lockstep: every Lua mod must do the same thing on both computers. So:

- The online hello carries a hash of each side's Lua mods (their files, their order, their options' values). **They run only when both players (and a watcher's two) have the same.** Otherwise they are paused, and the Mods tab says so.
- A mod that only draws can say so: mod.json `"lua": {"online": "hud"}`. It then runs even when the Lua mods differ, its changes to the game ignored (`ttyd.can_change()` is false, a `damage` handler's number doesn't count).
- Lua's own hashing and `table.sort` are seeded the same everywhere, `math` is the game's own (the same results on every computer), and `math.random` starts from the mod's id. Iterating with `pairs` over a table whose keys are tables or functions goes in an order that differs between computers: online, keep such keys out of anything that changes the game.
- No hot reload or Reload while online.

Replays keep the Lua mods' hash with the mods; a replay made with other Lua mods may go out of step.

## Things to know

- A Lua mod's state isn't in save states or rewind: after loading a state, its variables are as they were, not as at the state.
- The HUD is drawn with the settings page's font, not the game's.
- Events and the API come from `port/src/aurora/lua_game_aurora.c`; the sandbox and the host's part are `port/mods/lua_host.cpp`. New events for Paper Cut's own features go through `portLuaEvent` (`port/include/port/lua_mods.h`), so Lua mods can follow them too.
