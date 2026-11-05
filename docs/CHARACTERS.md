Characters: export, edit, import
================================

A character in TTYD is a paper cut-out: a **model file** `a/<name>` on the disc (its flat shapes, the tree of parts that move them, and every animation it has) and a **texture file** `a/<texture>-` (the drawings on the shapes). This page shows how to take one out of the game, change it, and put it back as a [mod](MODDING.md) that replaces it, on every platform and in the JP, US and EU versions.

What you can change:

| | How |
|---|---|
| The drawings (textures) | PNG files: any image editor. Bigger images are fine (up to 1024×1024). |
| Animations: timing, poses, loops, new ones | Blender (models made of plain parts, like Luigi's) or the JSON files (every model, Mario's too) |
| The rest pose, what is shown or hidden | `model.json` |
| Where the vertices and texture corners are | Blender, or `geometry.json` |
| One character in place of another | `pack --as` |

Not here (yet): adding or removing parts, vertices or drawings. The format is in `src/driver/animdrv.c`; [LUIGI_ANIMS.md](LUIGI_ANIMS.md) describes it and how Luigi's 156 new animations were made with the same tools.

You need [Python 3](https://www.python.org) (or Blender, which has it) and the Paper Cut repository's `tools/port` folder. Nothing taken from the disc is in the repository: everything below is made from your own copy of the game.

## 1. Export

**From the game** (any platform, no disc image needed): settings page (F1) → **Mods** → **Export characters** (shown while a game is running). Filter the list (`mario`, `c_luigi`, `kino`...), pick a model and press **Export**, or **Export the N shown** for a whole set. A model the list doesn't have (one a mod adds) can be typed in the filter whole and exported. Each goes to `exports/<name>/` in the user folder (next to `mods/`; [MODDING.md](MODDING.md) lists where that is; "Open the exports folder" on desktop). It holds the two files as the game read them (`disc/`) and `manifest.json`. If a mod already replaces the model, the export is the mod's file and the manifest says so.

Then make it editable:

```bash
python tools/port/character_tool.py export ~/.local/share/ttyd/exports/c_luigi
```

**From a disc image or an extracted disc** (`.iso`/`.gcm`, Dolphin's "Extract Entire Disc" folder, or `tools/port/extract_disc.py`'s):

```bash
python tools/port/character_tool.py list game.iso --filter mario
python tools/port/character_tool.py export game.iso c_luigi --out exports
python tools/port/character_tool.py export game.iso mario --out exports    # every Mario file
```

Either way, `exports/<name>/` now holds:

```
manifest.json    what was exported (and checksums, so `build` knows what you changed)
disc/            the model and texture files as on the disc (don't edit these)
textures/        000.png, 001.png ... every drawing
model.json       the parts (groups), their rest pose, visibility, the animation list
geometry.json    vertex positions, texture coordinates, vertex colours
anims/           one JSON file per animation
```

Which file is which character: Mario is `a_mario` (in the field; `a_mario_r` his back), `e_mario` (story scenes) and `b_mario` (battle), with variants (`_l`, `_w`...). A partner or NPC is usually `c_<name>`: `src/data/npc_data.c` names the NPCs' models (`c_kinopio` is a Toad), `src/party` the partners'. A file that starts with `p_` is a paper form.

## 2. Edit

### Drawings

Edit the PNGs in `textures/`, keeping transparency (the game cuts the paper out where alpha is under half). Save as PNG, not interlaced. A drawing can be redrawn at a larger size: it fills the same shape. It's stored in the texture's own format again (most are CMPR, a 4-colour-per-block compression: fine detail can come out blotchy; doubling the size helps). The texture's mipmaps are made again from it.

Only drawings whose pixels changed are rewritten; the rest stay byte for byte.

For textures alone, a [texture pack](MODDING.md#textures-texture-packs) is the other way: it replaces a drawing wherever it is drawn, by its hash, without touching the model.

### Animations in Blender

Blender 4.4 or later (the tools were made with Blender 5.2). Open the folder as a scene:

```bash
blender --python tools/port/character_tool.py -- blend exports/c_luigi --save c_luigi.blend
```

- The model is a tree of objects, one per part (a textured plane, or an empty), under a root object named after the model. Location, rotation (XYZ Euler) and scale are the game's values; the root turns the game's Y up into Blender's Z up. 60 frames a second.
- Every animation is an **Action** named `<model>:<animation>` (`c_luigi:W_1`); pick it in the Action editor (or run `ttyd_blender.set_anim(root, "W_1")` in the Python console). Keys are Bézier keys whose handles are the game's curves exactly; Constant keys are steps. Change keys, add keys, change the frame range (that is the loop range; the Action's `ttyd_loop` property is the loop flag).
- A part's visibility is its `["vis"]` property (keyed, Constant); the drawing a part shows is the root's `["tex<n>"]` (keyed, Constant: 0 is its first drawing, 1 the next...).
- **A new animation**: duplicate an Action and rename it `<model>:<NAME>`. The game asks for animations by name: Mario's field animations are `M_S_1` (standing), `M_W_1` (walking), `M_R_1` (running)... ; `manifest.json` lists the model's.
- Moving vertices or UVs of a plane (Edit Mode) is kept too. Adding or deleting vertices isn't.
- The images are the PNGs in `textures/`: an image painted in Blender and saved (Image → Save) is a texture edit.

Then write the scene back into the folder (with the scene open in Blender, or from the saved file):

```bash
blender c_luigi.blend --background --python tools/port/character_tool.py -- import exports/c_luigi
```

Each Action is read back through the game's curve: where the game's curve can't follow Blender's closely enough, a key is added (the output says how many). Actions you didn't change are written back exactly.

**Mario's models** (and other jointed rigs) can't be edited as objects in Blender: `blend` brings them in baked, a mesh that follows the game frame by frame, to look at. Their animations are edited in the JSON files.

### Animations as JSON (any model)

`anims/<name>.json` is one animation:

```json
{
 "name": "W_1",
 "loop_info": [[1, 0.0, 40.0]],      // [loop flag, first frame, last frame]: the first is used
 "keys": [
  {"time": 0.0, "nodes": [
    {"node": "pPlane3.r1z", "value": 12.5, "slope": [0.0, -0.4]}
  ]},
  {"time": 20.0, "nodes": [
    {"node": "pPlane3.r1z", "value": -12.5, "slope": [0.0, 0.0]},
    {"node": "pPlane5.tx", "value": 1.0, "step": true}
  ], "vis": [[4, 1]], "tex": [[2, 1, 0.0, 0.0, 0]]}
 ]
}
```

(Comments aren't allowed in JSON; they're here to explain.)

- `node` is `<part>.<value>`, the part's name as in `model.json`. Values: `tx ty tz` position, `sx sy sz` scale, `r1x r1y r1z` rotation **in half degrees** (the game doubles them), the others pivots and joint values.
- `value` is the value at that key. Between two keys a value follows a curve whose slopes (value units per frame) at the start and the end of the stretch are `slope`; `"step": true` jumps at the key instead. Values are kept in sixteenths and slopes in whole-degree angles, so they're rounded a little.
- A value not listed at a key carries on from the last key that lists it.
- `vis` (`[visibility index, amount added]`), `tex` (`[slot step, drawing step, offset s, offset t, pad]`), `pos`/`nrm` (vertex moves in sixteenths) are as stored: changes added at that key. `raw*` entries are the disc's exact bytes, used only while the values beside them are unchanged.
- `bbox` is the animation's bounds for the paper forms (only they use it).

To **add** an animation, copy a file, change its `name`, and add `{"name": "...", "file": "anims/....json"}` to `animations` in `model.json`. To **remove** one, take it out of that list. The list's order is the model's table; the game finds animations by name.

### Rest pose, visibility, geometry

`model.json`'s `groups` give each part's rest values (`node`, the same names as above); `vis` is each visibility index's starting value (1 shown, 0 hidden). `geometry.json` has every vertex position (model space), texture coordinate and vertex colour by index. Animations are stored as changes from the rest pose, so moving a part's rest moves it in every animation.

## 3. Import: build the mod

```bash
python tools/port/character_tool.py build exports/c_luigi                 # optional: check it, see what changed
python tools/port/character_tool.py pack exports/c_luigi --out ~/.local/share/ttyd/mods/my-luigi \
        --name "My Luigi" --author "You"
```

`build` writes the game's files into `exports/<name>/build/a/` and lists what changed; `pack` does the same and makes a mod: `mod.json` and `overlay/a/<model>` and `overlay/a/<texture>-`. Turn it on in the Mods tab (it's on when added). The game reads a model when it next loads it: walk into another room, start a battle, or restart.

- `--zip` writes one `.ttydmod` file instead of a folder (for sharing; iOS: put it in Files → TTYD → Saves → mods).
- Several folders in one mod: `pack exports/a_mario exports/a_mario_r exports/e_mario --out ...`.
- `--id`, `--version`, `--description` fill `mod.json`. `--games G8ME01,G8MJ01,G8MP01` limits it to versions; without it the mod is used on all three. The model files are data the versions share (`c_luigi` is byte for byte the same on all three discs); a model whose animations differ between versions is best exported from each.

### One character in place of another

`--as` writes the model under another name, so the game loads it in that one's place:

```bash
python tools/port/character_tool.py export game.iso a_mario --out exports      # to compare against
python tools/port/character_tool.py pack exports/c_luigi --as a_mario --target exports/a_mario --out mods/luigi-as-mario
```

The game asks the model for animations by name; one it doesn't have plays the model's first animation instead. `--target` (an export of the model being replaced) lists the names missing. Its textures are written as `a/<as>-` (`--texture-as` to name them otherwise): if the replaced model shares its texture file with others (check its `manifest.json`'s `texture`), they'd get yours too.

**A partner's skin** needs no `--as` and replaces nothing: put the pack (or the export folder) in the user folder's `skins/` folder and pick it on the partner's Skin row in the Start menu's Wardrobe ([COSTUMES.md](COSTUMES.md#partner-skins)).

**Play as Luigi** builds Luigi's model `a/c_luigi_pc` (his own and our new animations, and his hammer) from the disc at start, before mods; a mod that replaces `a/c_luigi` doesn't change him. To change the playable Luigi, replace `c_luigi_pc`: export it from the game (it's in the list: the port makes it at every start) and pack that folder; the mod's file wins over the port's.

## Checking

- `character_tool.py check game.iso [NAME...]` exports and builds every model (or the named ones) without changes and checks they come back byte for byte, including every animation through its JSON.
- `character_tool.py selftest` does the same with a small model made from the repository's own Luigi animations (`port/assets/luigi`), plus an edit of each kind, a bigger texture and both kinds of mod. Local CI runs it (`audits`).
- In the game, a model file that doesn't make sense is refused when loaded, with its name and the reason in the log (`port/src/common/anim_pc.c`), instead of crashing.

## How it works

- `tools/port/character_tool.py`: the commands above. `tools/port/ttyd_anim.py` reads and writes the model format byte for byte, `tools/port/ttyd_tpl.py` the textures (every GameCube format; PNG reading and writing in pure Python), `tools/port/ttyd_blender.py` the Blender side ([LUIGI_ANIMS.md](LUIGI_ANIMS.md)).
- `port/settings/features/character_export.cpp`: the Mods tab's export: the disc's `a/` folder listed, a model read as the game reads it (through the mods' overlay), with its texture file and its animation names, written with a manifest.
- The mod is a plain overlay mod ([MODDING.md](MODDING.md#overlay-disc-files)): nothing about characters is special in the mod loader.
