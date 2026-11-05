Costumes, partner skins and stickers
====================================

Three ways to change how the game looks, all in the Start menu's **Wardrobe** tab. They change how things look only: the game itself plays the same, so they're fine in online co-op and in replays (the other player sees their own choices, not yours).

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

A costume is unlocked once its enemy is in the Tattle Log of the file you're playing, and chosen in the Wardrobe: **Mario → Costume** (left and right step through the ones unlocked). It shows in the field, in battle and on the menus' figures, on Luigi too when he stands in for Mario (Play as Luigi). Paper forms (the plane, the boat...) don't wear it.

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

How it works: `port/src/aurora/skins_aurora.c` makes each skin into `a/pc_skin_<n>` and its drawings `a/pc_skin_<n>-` at startup, before the mods apply; `port/src/common/skin_pc.c` draws a skin's pose in the partner's place, following the partner's own pose (as Play as Luigi does for Mario).

## Stickers

Stickers are partners' faces, items and enemies, stuck anywhere in the world:

- a partner's face, once they've joined (Mario's from the start);
- an item, once the file has had one (in the pouch, the key items or stored), a badge once found, a dish once cooked;
- an enemy, once it's in the Tattle Log.

In the Wardrobe, **Stickers**: **Kind** picks faces, items or enemies (with how many are unlocked), **Sticker** one of them (the stage shows it), **Stick it here** (A) sticks it where Mario stands, a step behind him, facing the camera; **Peel the nearest** takes the one nearest him off. Up to 24 in a room. An enemy's sticker is its paper model, small, in its standing pose; the others are the game's icons.

They're kept with the save file, in the user folder: `Stickers/<game id>/file<n>.txt` (beside the achievements' `Achievements/` folder), with the items the file has had. They're part of the picture, so photos have them. Loading a save state doesn't take them back off.

How it works: `port/src/common/sticker_pc.c`.

## Tests

`TTYD_COSTUMES_ALL=1` unlocks every costume and `TTYD_STICKERS_ALL=1` every sticker for a run; `TTYD_NO_SKINS=1` reads no skins.
