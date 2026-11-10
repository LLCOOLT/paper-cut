# The map editor and map edits

A **map edit** puts new things into a room of the game, or changes what the room already has: a pipe to another room, a Save Block, a ? block with an item, a switch, a sign, an NPC who talks or keeps a shop, a loading zone, a collision wall taken away. Map edits live in a mod, as `maps/<room>.json`, and are made either by hand or with the **map editor** in the game. Everything placed is the game's own: its mobjs (pipes, blocks, switches, jump stands, signs), its field items and its NPC models from your disc. A map edit names them; nothing from the disc is ever copied into a mod.

The remake's map changes ([REMAKE_MAPS.md](REMAKE_MAPS.md)) are map edits too, a set built into Paper Cut.

## The map editor

**Modes → Map editor → Open the map editor** on the settings page, or **Map editor** on the Start menu's Modes page. It works in the field, in the room Mario is in, and not online or in a replay.

The game goes on, with Mario and the room's NPCs held still, and the camera flies free. The panel shows the room, the mod the room's things are saved to (**Mod**, a folder name in your mods folder; `my-maps` at first), what to place, and the room's things.

| Mouse and keyboard | |
|---|---|
| Left click | pick a thing by its marker, or place the chosen kind where the click meets the room's floor or walls |
| Left drag | move the picked thing over the floor |
| Right drag | look around |
| W A S D, Q / E | fly; Shift faster |
| Arrows, Page Up / Down | nudge the picked thing (Shift: by 1) |
| R | turn it (an NPC's facing) |
| F | put it down on the floor |
| Delete | take it away |
| Ctrl+Z, Ctrl+S | undo, save |
| Tab | test play, and back |
| Esc | close (changes are saved) |

| Controller | |
|---|---|
| Left stick, triggers | fly; down and up |
| Right stick | look around |
| A | place the chosen kind at the picture's middle (the small circle) |
| X | take away the picked thing |
| LB / RB | pick the previous / next thing |
| Y | the next kind to place |
| D-pad | nudge the picked thing |
| Back | test play, and back |
| B | close |

- **Placing.** Choose the kind in **Place**; for an NPC, its model (every character model the game has a name for). Things that change the room's own (hide a collision object, a model, take away a mobj, an NPC or an item, point an exit elsewhere) have no place: **Add the change**, then name what it changes.
- **The picked thing's panel.** Its place (drag the numbers), its links, its item, price, power or words, a shop's items, an id and whether it waits for a switch, and the story's step it appears from.
- **Links.** Pipes, zones, jump stands and exits lead to a room and an entrance there, picked from the area maps' rooms and their entrances (this room's own too). A new pipe or zone leads to this room's first entrance, so it works at once.
- **Test play** gives Mario back with the game's camera; the room's things are there to try, and a pipe or a zone takes him to the next room, where the editor goes on (its own things, saved with the rest).
- **Undo** goes back one change at a time (100). **Discard this room's changes** goes back to what was saved.
- **Save** writes `mods/<mod>/maps/<room>.json` for each room changed, and `mods/<mod>/mod.json` if the mod has none yet (id `local.mapedit.<mod>`), then reads the mods again: the mod manager lists it, and it can be turned off, ordered, zipped and shared as any mod. Closing the editor saves too.

What the editor shows while it is open is local: the room's things come from the saved files again once it closes.

## Map edits by hand

```json
{
  "room": "gor_01",
  "things": [
    {"type": "pipe", "pos": [100, 0, 50], "to": "gor_02", "entrance": "w_bero"},
    {"type": "heart_block", "pos": [0, 60, 0], "price": 10, "when": {"story": 20}},
    {"type": "block", "pos": [-60, 60, 0], "item": "ITEM_MUSHROOM", "flag": {"gswf": 6000}},
    {"type": "switch", "pos": [150, 0, 0], "shows": ["secret"]},
    {"type": "pipe", "id": "secret", "hidden": true, "pos": [200, 0, 0], "to": "tik_01", "entrance": "dokan_1"},
    {"type": "npc", "model": "c_kinopio", "pos": [-100, 0, 80], "ry": 90, "say": "Welcome!<p>Have a look.", "shop": ["ITEM_MUSHROOM", "ITEM_HONEY_SYRUP"]},
    {"type": "hide_collision", "target": "a_kaidan_saku01"},
    {"type": "exit", "target": "e_bero", "to": "gor_03", "entrance": "w_bero"}
  ]
}
```

`"room"` may be left out when the file is named after it (`maps/gor_01.json`). Places are the room's own coordinates, as the game's scripts have them (the map editor shows them; blocks float 60 above the floor, as the game's do).

| type | what | fields |
|---|---|---|
| `pipe` | a green pipe; Mario goes down it by holding the stick down on it | `to`, `entrance` |
| `save_block`, `heart_block` | the game's Save and Heart Blocks | `price` (Heart Block, coins) |
| `jump_stand` | a blue jump stand | `power` (130); `to`, `entrance`: it throws Mario over to another room |
| `block`, `badge_block` | a ? block, a badge block | `item`, `flag` |
| `brick`, `hidden_block` | a brick block (an item or none), a hidden ? block (a coin unless `item`) | `item`, `flag` |
| `switch` | a blue switch | `flag` (set when pressed), `shows` (ids placed at once when pressed) |
| `sign` | a signboard | `say` or `text` |
| `item` | an item on the floor | `item`, `flag` (set once taken: then it isn't placed again) |
| `npc` | a character, by its model's name (`c_kinopio`, `c_botta`...) | `model`, `ry`, `say` or `text`, `shop` (up to 8 items: the shop's window, at the items' prices) |
| `zone` | a loading zone: walking into it changes rooms | `radius` (40), `to`, `entrance` |
| `hide_collision`, `hide_model` | the room's collision object or model (and its children) off | `target` |
| `remove_mobj`, `remove_npc`, `remove_item` | the room's own mobj, NPC or item taken away | `target` |
| `exit` | the room's entrance (its hit name) leads elsewhere | `target`, `to`, `entrance` |

For every thing:

- `pos` `[x, y, z]` (placed things), `id` (a name of its own, for `shows` and Lua), `hidden` (true: only placed when a switch shows it).
- `when`: `story` (the story's progress, GSW 0, at least), `story_below` (less than), `gswf` / `gswf_off` (a saved flag set / not set), `gsw` with `gsw_min` (a saved byte at least). Things are placed as the room loads, by the save's state then.
- `uses`: `{"gsw": n, "value": v}`, a saved byte written when Mario goes down the pipe, through the zone or off the jump stand.
- `say`: the words, UTF-8, with the game's tags (`<p>` a new box after a wait for A, `<k>` wait for A; a `<k>` is added at the end), laid out in the game's lines of 30 columns (its font never wraps). `text` instead names a message tag, the mod's own (`messages/`, [MODDING.md](MODDING.md#messages-text)) or the game's, for words in more than one language.
- Numbers may be the game headers' names: items (`ITEM_MUSHROOM`), flags (`GSWF_...`), story steps (`STORY_...`).
- `flag`: `{"gswf": n}` or `{"lswf": n}`, as the game's scripts name saved flags. A block without one comes back each time the room loads. Pick flags the game doesn't use for your own.

Rooms take up to 128 things. A thing with a mistake (an unknown type, a missing field, a number out of range) is left out and the rest are used; the log says which file and thing, and the mod manager shows the first one. A target the room doesn't have is reported in the log when the room loads.

All mods' map edits add up, in the mods' order, with the built-in remake set's.

## Lua

`map.place{...}` puts a thing in the room now, with the same fields (`type`, `pos` or `x`/`y`/`z`, `to`, `say`, `item`, `flag_gswf`...), until the room changes; it returns true if it was placed. `map.remove(id)` takes away a placed thing by its id (a map edit's or map.place's). `map.edits()` counts the room's map edits. They change the game, so they do nothing while `ttyd.can_change()` is false ([LUA_MODDING.md](LUA_MODDING.md)).

```lua
ttyd.on("map_load", function(room)
  if room == "gor_01" and flags.gsw(0) >= 10 then
    map.place{type = "save_block", pos = {0, 60, 100}}
  end
end)
```

## Online and replays

A mod with map edits is one every player needs on, as any mod that changes the game; the online hello carries its maps' files' hash, so the same mod with other map edits doesn't play together. Placing follows from the files and the save's state alone, so both games place the same. The editor is off online and in replays.

## Not in this version

- Field enemies with battles (an existing group, or the Enemy maker's), chests and camera changes. Field enemies will go the way Director's Cut adds its cut ones.
- Picking a room's own collision objects and models by clicking (their names are typed for now).
- Touch controls in the editor.

Code: `port/include/port/map_edits.h`, `port/src/common/map_edits_pc.c` (placing), `port/mods/map_edits.cpp` (the files), `port/settings/features/map_editor.cpp` (the editor).
