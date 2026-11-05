# The Wardrobe: colours, costumes, partner skins and stickers

Ways to change how the game looks, all in the Start menu's **Wardrobe** tab: colours and themes, costumes, partner skins and stickers. They change how things look only: the game itself plays the same, so they're fine in online co-op and in replays (the looks made from textures are each player's own). Costumes, stickers and skins load models into the game's memory, so every game of an online session must load the same ones on the same frames (they're loaded for what the game shows, never for what one screen happened to draw). Costumes are Mario's, so online every game wears the host's, the player who plays Mario. Stickers are the host's online: its sticker files go with its setup, every game shows and sticks the same ones (from the Wardrobe, which every game runs together), and a guest's own files aren't touched. Partner skins come from your own user folder, a character pack the other player may not have, so they're off in online co-op. Skins and stickers are off in replays (they're your own folder's).

## Colours and themes

The Wardrobe's page shows the one being dressed on the left, turning slowly in
his idle animation, and the rows on the right (`port/src/aurora/wardrobe_aurora.c`):

- **Mario:** **Looks** (Mario as Luigi, Wario, Waluigi or Fire Mario in one
  press), **Cap and shirt**, **Overalls**, **Costume** (below).
- **Partners**, in the order they join, each with their parts: Goombella's
  Hair and Helmet, Koops's Shell and Hoodie, Flurrie's Hair and Dress,
  Vivian's Body, Hat and Hair, Bobbery's Body and Fuse, Ms. Mowz's Mask and
  Cape, and a **Skin** row each (below). The selected row's partner stands on
  the page, met yet or not.
- **Yoshi's colour** is the colour he hatched in, stepped through the game's
  seven. It's the game's own state (kept in the save), so it can't change
  during online co-op or a replay.
- **Status bar:** **Theme** (Original, Night, Forest, Ocean, Candy, High
  contrast) colours the HP, FP and Star Power banners and the menus' pages to
  go together; the **HP banner**, **FP banner**, **Star Power banner** and
  **Menus** rows after it set one part, and a part with a colour of its own
  keeps it over the theme.
- **Colour picker...** opens the settings page's Cosmetics tab, where each
  part also has a custom colour (a swatch, or red, green and blue).

Left and right (A: the next) step through a row's colours: the game's own,
13 named ones, then the part's custom one. A colour shows at once: the models'
textures are recoloured as they're drawn. The same settings are on the
settings page's **Cosmetics** tab (Theme, each part, and Looks and colour
picker). Colours and themes are each player's own, online too.

## Costumes

Mario can wear a paper version of an enemy's outfit:

| Costume | Unlocked by Tattling |
|---|---|
| Koopa shell | a Koopa Troopa (or a KP Koopa) |
| Paratroopa wings | a Paratroopa (or a KP Paratroopa) |
| Spiky Goomba helmet | a Spiky Goomba (or a Hyper Spiky Goomba) |
| Paragoomba wings | a Paragoomba (or a Hyper Paragoomba) |
| Boo sheet | a Boo |
| X-Naut hood | an X-Naut |
| Bob-omb key | a Bob-omb (or a Bulky Bob-omb) |
| Buzzy Beetle shell | a Buzzy Beetle |
| Spiny shell | a Spiny |

A costume is unlocked once its enemy is in the Tattle Log of the file you're playing, and chosen in the Wardrobe: **Mario → Costume** (left and right step through the ones unlocked). It shows in the field, in battle and on the menus' figures, on whoever **Play as** puts in Mario's place ([PLAY_AS.md](PLAY_AS.md#the-wardrobe)): each costume is fitted to Luigi's, Peach's and Bowser's own head and body as it is to Mario's, at their height and Bowser's bulk. The Wardrobe's row is under the pick's name. Paper forms (the plane, the boat...) don't wear it.

Nothing is drawn for it: each costume is the enemy's own model from your disc, showing only the parts that make the outfit (the Koopa's shell, the X-Naut's hood and goggles...), fitted to Mario's hat, body or whole figure as he moves, and lit and faded as he is.

How it works: `port/src/common/costume_pc.c`. Each costume is a row of its table: the enemy's model and the animation it stands in, the names of the model's parts that make the outfit, what it sits on (Mario's hat, body or whole figure), its size against his height, where it sits (toward his back, up) and in front of or behind him. The parts' names are the model's groups as the character tools list them ([CHARACTERS.md](CHARACTERS.md): `model.json`'s `groups`). The animation driver draws only those parts while the costume is drawn (`gPortAnimShapeMask` in `src/driver/animdrv.c`) and gives each part's place (`portAnimPoseGroupMatrices`), which is how the outfit is measured and fitted.

## Partner skins

A character made with the [character tools](CHARACTERS.md) can be drawn in a partner's place, with its own animations. Put the character in the **skins** folder in the user folder (next to `mods/`; [MODDING.md](MODDING.md) lists where that is), one folder per character, in any of the forms the tools make:

```
skins/My Luigi/mod.json            a pack (character_tool.py pack ... --out "skins/My Luigi"):
skins/My Luigi/overlay/a/c_luigi       the model
skins/My Luigi/overlay/a/c_luigi-      its drawings
```

or an export folder as it is (`exports/<name>/`, with `disc/` or, once built, `build/a/`). The folder is read at the next start. Its name in the Wardrobe is the pack's `mod.json` name, or the folder's.

Then in the Wardrobe, each partner has a **Skin** row (Yoshi's is under his colour): left and right step through their own look and each skin. The skin plays the animation the partner plays when it has one of the same name (`PKR_W_1`), else the same move without the model's prefix (`W_1`, or `KUR_W_1` in an enemy's model), else that move's first (`W_1` for `W_2`), else standing (`S_1`), else its first. It is drawn at the partner's height. Paper forms stay the partner's own.

Skins are local files only: nothing is fetched. A skin is not a mod: it replaces nothing on the disc, and the same character can be every partner's skin at once. A model file that doesn't make sense is refused when the game loads it, with its name in the log.

How it works: `port/src/aurora/skins_aurora.c` makes each skin into `a/pc_skin_<n>` and its drawings `a/pc_skin_<n>-` at startup, before the mods apply; `port/src/common/skin_pc.c` draws a skin's pose in the partner's place, following the partner's own pose (as Play as does for Mario).

## Stickers

Stickers are partners' faces, items and enemies, stuck anywhere in the world:

- a partner's face, once they've joined (Mario's from the start);
- an item, once the file has had one (in the pouch, the key items or stored), a badge once found, a dish once cooked;
- an enemy, once it's in the Tattle Log.

In the Wardrobe, **Stickers**: **Kind** picks faces, items or enemies (with how many are unlocked), **Sticker** one of them (the stage shows it), **Stick it here** (A) sticks it where Mario stands, a step behind him, facing the camera; **Peel the nearest** takes the one nearest him off. Up to 24 in a room. An enemy's sticker is its paper model, small, in its standing pose; the others are the game's icons.

They're kept with the save file, in the user folder: `Stickers/<game id>/file<n>.txt` (beside the achievements' `Achievements/` folder), with the items the file has had. They're part of the picture, so photos have them. Loading a save state doesn't take them back off.

How it works: `port/src/common/sticker_pc.c`.

## From Lua mods

A [Lua mod](LUA_MODDING.md#looks) can choose a partner's skin and Mario's costume (`looks.set_skin`, `looks.set_costume`): its choice is worn over the Wardrobe's while the mod runs, and the Wardrobe's comes back when it stops. Nothing is saved.

## Tests

`TTYD_COSTUMES_ALL=1` unlocks every costume and `TTYD_STICKERS_ALL=1` every sticker for a run; `TTYD_NO_SKINS=1` reads no skins.
