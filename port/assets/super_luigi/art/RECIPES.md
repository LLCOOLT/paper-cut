# Art recipes: art made from the player's own disc

Super Luigi's art should look like the game's, and the surest way is to start
from the game's own pixels: Mario's icons recoloured and repainted as Luigi's,
a character's texture sheet in new colours, a prop put together from two disc
textures. But **nothing from the disc may be committed** to this public repo,
and a PNG that is mostly the disc's texels is disc data.

So the repo holds **recipes**: short text files that say how to make each
image from the disc. The game runs them on the player's own disc when it first
starts, keeps the results in the user folder, and then loads them exactly where
it loads committed art.

- **Recipes:** `art/recipes/*.recipe` (built into the game).
- **Examples:** `art/recipes/examples/*.recipe` (not built in; worked examples
  below).
- **Runner (the truth):** `port/src/common/art_recipe.c`, C, in the game.
- **Authoring and preview tool:** `art/tools/recipes.py`, the same format and
  the same arithmetic in Python.
- **Check:** `tests/port/art_recipe_check.py` runs both on every recipe on each
  disc and compares them texel for texel.

## Writing a recipe

1. Find your sources on the disc: extract it (`tools/port/extract_disc.py`) and
   look at a TPL's images with a throwaway recipe:

   ```
   image look/0.png
     load model c_kinopio 8
   end
   ```

   ```
   python art/tools/recipes.py look.recipe --disc ../ttyd-port/data/G8ME01 --out SCRATCH --zoom 4
   ```

   It writes each image at its path under `--out`, and `preview.png` with all of
   them at 4x. **Keep `--out` outside the repo**: what it writes is made from
   the disc.
2. Write the recipe, preview it the same way, and iterate.
3. Put it in `art/recipes/` (a new file, or add images to one). Re-run CMake so
   the build picks it up.
4. Check that the C runner agrees: build `art_recipe_test` (`ninja
   art_recipe_test` in a build folder) and run `python
   tests/port/art_recipe_check.py`. It must report `0 differences` on every
   disc.
5. Run the game with a fresh `TTYD_USER_DIR`: the log says
   `art: N images from the disc's (...)`, and the images are in
   `<user dir>/art_cache/<game id>/<path>`. Look at them there.

**Never commit** the preview, the outputs, or a PNG traced from them. Commit the
recipe (and, if you like, a drawn fallback: see `fallback`).

## The format

A recipe file is lines of words separated by spaces. `#` followed by a space
(or at the end of a line) starts a comment; `#` followed by hex digits is a
colour. A line starting with `|` is a **row** (pixel data for `paint` and `mask
... rows`); rows belong to the line above them.

```
gradient greens 0.0 #0a1206 0.5 #287126 1.0 #39a639     # shared lines: gradients, colours, masks

image art/icons/luigi/luigi_head.png                      # one image, made at this asset path
  fallback art/icons/luigi/drawn/luigi_head.png           # what to use if it can't be made
  load icon ICON_19E                                      # the canvas: a disc image
  mask cap rect 0 0 39 24                                 # steps, in order
  ramp in cap where h<=10 s>=0.45 base #b52021 to greens
  alpha cut 128
end
```

Lines outside `image ... end` (gradients, colours, masks) are shared: every
image after them in the file sees them. Inside an image, the steps run in
order on the **canvas** (or on a **piece**, with `@name`).

### Images and their names

| Line | What it does |
|---|---|
| `image PATH` ... `end` | Makes one image. `PATH` is the asset path the game asks for (`art/icons/luigi/luigi_hud.png`, or a Dolphin texture name in a folder the game registers, such as `art/icons/luigi/ui/tex1_96x40_b3df1434fe52ff08_14.png`). |
| `fallback PATH` | A committed asset (drawn art) the game uses if this image can't be made: the disc lacks a source, or the recipe is wrong. Without one, the game shows its own (Mario's) picture. |
| `drawn ID ...` | On these discs the image isn't made: its `fallback` stands in (or, with none, the packed model's or the game's own picture), and nothing is logged. For an image whose sources only some versions have (the English letters of `m/yuu_02/t` are on US and EU only). Put it first. |
| `discs ID ...` | Makes the image only on these discs (`G8MJ01` JP, `G8ME01` US, `G8MP01` EU). On the others it doesn't exist (no fallback either): for an image that only fits one version, such as a Japanese tab. Put it first. |

### Sources

| Source | The image |
|---|---|
| `tpl PATH N` | Image `N` of a TPL on the disc, by its path under `files/` (`w/us/win.tpl 1`, `battle/common/battle_common.tpl 51`). |
| `model NAME N` | Texture `N` of a model's textures (`model c_kinopio 8` is `tpl a/c_kinopio- 8`). |
| `icon ICON_xxx [frame F]` | An icon by its name in `include/icon_ids.h` (named by the JP id; the runner finds the version's id and its image in `icon.tpl`). `icon 0x1AB` is a raw id of this disc. |

Every format the disc uses is decoded (I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8,
CMPR; C4, C8 and C14X2 too, though TTYD has none), exactly as
`tools/port/tpl2png.py` decodes them.

| Line | What it does |
|---|---|
| `load SOURCE` | The canvas is a copy of the source. |
| `canvas W H [COLOUR]` | The canvas is a new blank image (clear, or the colour). |
| `piece NAME SOURCE` | A named image to work on and paste: a source, `canvas W H [COLOUR]`, or `copy` (a copy of the canvas as it is now). |
| `@NAME STEP ...` | Runs a step on piece `NAME` instead of the canvas. |

### Colours

`#rrggbb` (opaque) or `#rrggbbaa`, or a name.

| Line | What it does |
|---|---|
| `colours NAME COLOUR [NAME COLOUR ...]` | Names colours. A one-character name is also a `paint` colour. A later `colours` can change a name. |
| `colours NAME sample X Y` | A colour taken from the canvas (or `@piece`) at X, Y: draw edits in the source's own colours. |
| `gradient NAME T COLOUR T COLOUR ...` | A ramp of colours by lightness: 2 to 32 stops, `T` going up. Used by `ramp`. |

### Masks

A mask says which texels a step works on. It is built from shapes, each added
(`+`), taken away (`-`) or intersected (`&`), and is worked out on the image
when a step uses it.

| Line | Shape |
|---|---|
| `mask NAME rect X0 Y0 X1 Y1` | A rectangle, both corners included. |
| `mask NAME poly X,Y X,Y X,Y ...` | A polygon: the texels whose centre is inside. |
| `mask NAME spans X:Y0-Y1 X0-X1:Y0-Y1 ...` | Per column, a run of rows (a moustache's band, say). |
| `mask NAME rows X Y` + rows | A stencil: rows of `x` (in) and `.` (out), its top left at X, Y. |
| `mask NAME alpha` | The opaque texels (alpha 128 or more). |
| `mask NAME mask OTHER` | Another mask as it is now. |
| `mask NAME + SHAPE` / `- SHAPE` / `& SHAPE` | Adds to, takes from, or intersects the mask. |

### Selecting texels

The recolouring steps take `[in MASK] [where CLAUSES] [:]` first. `where`
selects by each texel's own colour:

- `h` hue in degrees (0 to 360), `s` saturation and `v` value (0 to 1), as
  Python's `colorsys` computes them; `l` lightness 0.299 R + 0.587 G + 0.114 B
  (0 to 255); `r g b a` (0 to 255).
- A clause is `KEY<N`, `<=`, `>`, `>=` or `=`; and `h=A..B`, a hue range, which
  wraps past 360 when A > B (`h=330..16` is pinks and reds).
- Clauses side by side must all hold; `or` separates alternatives:
  `where h<=10 s>=0.45 or h>=340 s>=0.45`.
- `ramp`, `map` and `hsv` leave fully clear texels (alpha 0) alone; `fill` and
  `alpha set` don't.

### Recolouring

| Step | What it does |
|---|---|
| `ramp [in M] [where ...] base COLOUR to GRADIENT` | Each texel's lightness against the base colour's (`l / l(base)`, 1.0 is the base's own lightness) picks its colour on the gradient. This is the palette map that keeps the bands: Mario's cap red in its five shades lands on five greens, each where its lightness says. Alpha is kept. |
| `map [in M] [where ...] FROM -> TO [FROM -> TO ...]` | Each texel takes the nearest `FROM` colour (in RGB) and becomes its `TO`, scaled by the texel's lightness against `FROM`'s: a few-colour sheet recoloured with its shading kept. |
| `hsv [in M] [where ...] [hue =D/+D/-D] [sat =K/*K] [val =K/*K/+K/-K]` | Hue set or turned (degrees), saturation set or scaled, value set, scaled or moved; the rest of the colour kept. |
| `fill [in M] [where ...] COLOUR` | Sets the texels. |
| `alpha cut T` | Alpha 255 where it is T or more, else 0 (1-bit alpha, as the game's art; the colour of cleared texels is kept). |
| `alpha set A [in M] [where ...]` | Sets alpha: `alpha set 0 where ...` makes a background clear before a paste. |

### Geometry

| Step | What it does |
|---|---|
| `crop X Y W H` | The W x H texels from X, Y (outside the image: clear). |
| `flip x` / `flip y` | Mirrors left-right / top-bottom. |
| `rotate D` | Turned D degrees clockwise about the middle, each texel the source texel under its centre. The size stays: pad first with `crop` so the corners aren't lost. |
| `scale W H nearest` | To W x H, each texel the source texel under its centre: whole multiples stay crisp. |
| `scale W H texel` | To W x H keeping the game's texel look: each texel the box of source texels under it, weighted by overlap and alpha, with 1-bit alpha (in where half the box is). Use it to shrink or grow by a little. |
| `scale W H smooth` | To W x H, each texel bilinear between the four source texels round its centre, weighted by alpha, with 1-bit alpha (in where half is covered). For taking a disc texture up to a card cut at `cutout.py`'s density (c_sl_gear_gate at 4x): the outline stays smooth where `texel` would show the source's blocks. |
| `grow R COLOUR` | A border R texels wide round the opaque shape (a round brush), in the colour: only clear texels change. A 1-texel line round a scaled-down card (`grow 1 #392821`), or a sticker's edge (`grow 1 #e7e7e7`, then `grow 1 #a5a5a5`). |
| `paste PIECE X Y [over/copy]` | Draws a piece onto the canvas (or onto `@another`) with its top left at X, Y. `over` (the default) lays it over by its alpha; `copy` replaces the texels, clear ones too. |
| `slide down/up/right/left AT FROM-TO:BY ...` | Moves part of the image: `slide down 38 15-31:2` moves columns 15 to 31 down 2 texels from row 38, the row above repeated into the gap, the line and border with it (a longer jaw). `up`, `right` and `left` likewise (`AT` a row or a column). |

### Painting

| Step | What it does |
|---|---|
| `paint X Y` + rows | Sets texels from rows, top left at X, Y: each character a one-character colour name, `.` keeps the texel, `_` clears it. For small edits (an emblem's letter, a mouth): keep them small, and in the source's own colours. |
| `stamp MASK COLOUR` | Fills a mask in one colour. |
| `stamp MASK edge C top C bottom C fill C` | Paints a mask as a shaded band, the way the game's moustaches are: its outline (any texel with a side outside the mask) in `edge`, the row under the top edge in `top`, the row over the bottom edge in `bottom`, the rest in `fill`. |

## Worked examples

### The icons (`art/recipes/luigi_icons.recipe`)

Luigi's three faces are Mario's icons edited: `ICON_1A3` (the HP face),
`ICON_19E` (the pause menu's head) and `ICON_1A1` (the level row's coin). The
HUD face:

```
gradient greens 0.0 #0a1206 0.3 #182810 0.45 #1e4a1c 0.7 #287126 0.86 #298a29 1.0 #39a639 1.2 #52b639 1.4 #63c339 1.9 #addf9c

image art/icons/luigi/luigi_hud.png
  fallback art/icons/luigi/drawn/luigi_hud.png
  load icon ICON_1A3
  slide down 38 15-31:2 13-14:1 32-33:1          # the jaw 2 texels longer
  mask cap rect 0 0 51 31                        # the cap: above row 31 ...
  mask cap - rect 22 0 51 12                     # ... but not the HP heart behind it
  ...
  mask disc mask cap
  mask disc & rect 22 13 30 20                   # the emblem's disc
  hsv in disc where h=330..16 s<0.55 v>0.35 : hue =105 sat *0.6       # its pink whites, green-lit
  ramp in cap where h<=10 s>=0.45 v>=0.12 or h>=340 s>=0.45 v>=0.12 or h<=16 s>=0.45 v>=0.3 base #f73110 to greens
  colours W #f2f6f0 w #c6d6ce G #39a639 g #287927 s #a8b8ac
  paint 24 16                                    # the L on the disc
  | wGwWw
  | WGWWw
  | WGGGs
  | wsgss
  ...
  mask tache spans 18:33-35 19:33-36 20:34-37 ...
  stamp tache edge #311000 top #632910 bottom #4a2108 fill #5a2910   # his moustache
  alpha cut 128
end
```

- The `ramp` puts each of Mario's cap reds on Luigi's greens by its lightness
  against Mario's fill (`#f73110`), so the shading bands survive.
- The `where` clauses pick the cap's reds and not the hair's or the line's
  browns; `in cap` keeps the heart's pinks out.
- The edits (the L, the moustache, the longer chin) are a few dozen texels,
  painted in colours taken from Mario's own.

The game draws these where it drew the committed PNGs
(`sl_assets_aurora.c`, `portIconTexOverride`), at the same paths.

### Recolouring a whole texture sheet (`examples/green_toad.recipe`)

Toad's cap and vest in Luigi's colours. The cap's sheet (`c_kinopio`, image 8)
is white with red spots; a ramp takes every red (the spots, their shading and
the pink blends at their edges) to the greens:

```
image examples/green_toad/cap.png
  load model c_kinopio 8
  ramp where h=335..12 s>=0.25 v>=0.2 base #d82820 to greens
end

image examples/green_toad/vest.png
  load model c_kinopio 10
  hsv where h=180..250 s>=0.3 : hue +70 sat *0.9
end
```

- No mask is needed: the `where` says which colours, everywhere on the sheet.
  The face's skin is an orange (about 25 degrees), past `h=335..12`.
- The vest shows the other way: a hue turn, the blue's shading kept as it is.
- For a few flat colours, `map` is simpler:
  `map #d82820 -> #39a639 #f0f0f0 -> #f0f0ff`.
- **For the game to use a recoloured texture**, name the output as Dolphin
  names a replacement, in a folder the game registers as a texture set. For
  example `textures/<set>/tex1_120x112_<XXH64>_14.png`, where the name comes
  from a texture dump (Settings, Mods, dump textures) or an existing pack. See
  `portSlMapTextures` in `superluigi_aurora.c`: the folder is matched by map
  pattern, `*#luigi` for "whenever Luigi is the player".

### A new prop from two disc textures (`examples/winged_cap.recipe`)

A shop sign: Toad's head with a Paragoomba's wings, on a new canvas.

```
image examples/winged_cap.png
  canvas 112 64
  piece head model c_kinopio 8
  @head scale 60 56 texel                 # half size, the texel look kept
  piece left model c_kuribo 5
  @left crop 0 0 32 64                    # the sheet's first wing
  @left scale 24 48 texel
  piece right model c_kuribo 5
  @right crop 0 0 32 64
  @right scale 24 48 texel
  @right flip x                           # the other side
  paste left 6 0                          # the wings first,
  paste right 82 0
  paste head 26 6                         # the head over their roots
end
```

Pieces are worked on with `@name` and then pasted in order: later pastes go
over earlier ones.

### Lettering from the game's own letters (`art/recipes/luigi_tabs.recipe`)

The pause menu's first tab, "Mario", as "Luigi" (and ルイージ): the tab's own
board with its lettering filled over, and each letter cut from another of the
game's tabs or bars. `alpha set 0` clears that tab's board colour, and `map`
moves its lettering, line and blend to the Mario tab's colours. Where the game
has no glyph, join strokes of others: ル is リ's long stroke and し.

### A model's cards from the disc (`art/recipes/model_statue.recipe`)

The models in `sl_models.bin` are packed when they're authored, so that file can't hold anything made
from the disc. It holds the geometry, the motions and the **drawn cards**. A recipe image named

```
image art/models/c_lp_statue/8.png
```

replaces **image 8 of `a/c_lp_statue-`** when the game starts (`portSuperLuigiModelArt`,
`sl_models_aurora.c`, run after Preparing graphics and before the mods take the host files). The rest of
the model doesn't change.

- **Which number:** `python tools/port/sl_models.py cards c_lp_statue` lists each image, its size and
  the card that shows it. A variant's images come after its base's (`c_luigi_bride`), and a disc model's
  are its own numbers (`a:c_kanbu_49` is `art/models/c_sl_crepe/49.png` on a variant of `c_kanbu`).
- **Any model:** the texture file is taken from whatever made it: our container, `friend_anims.bin`, a
  variant, or the disc. So a variant with no cards of its own is a recolour of a disc model on that
  model's own rig: an empty pack, `SWAPS` not needed (see `sl_crepe.py`).
- **The fallback is automatic:** an image that can't be made leaves the drawn card in place, so these
  recipes need no `fallback` line. `TTYD_ART=off` shows the drawn cards.
- **Sizes:** sides must be multiples of 8. The size needn't match the drawn card's: the card's quad stays
  where the model puts it, and its texture fills it. Giant bosses use half the texels (4 a unit, as
  Hooktail's) on the same quads.
- **Formats:** the runner's RGBA is encoded in the game: CMPR with 1-bit alpha (as the disc's art), or
  RGB5A3 if the image has soft alpha. Encoding takes a few ms.
- **To see it:** `$TTYD_SL_GALLERY="c_lp_statue:S_1:70 c_sl_crepe:W_1"` stands those models beside the
  player in any room (`NAME[:POSE[:X[:SCALE]]]`), in the game's own light.

`tpl` paths under `/us/` fall back to the same path under `/jp/` on the Japanese disc. Its
`e/jp/effect.tpl` matches images 0 to 99 of `e/us/effect.tpl`, and its `effect_sys.tpl` 0 to 3.

## Things to know

- **The C runner is the truth; the Python tool must agree.** Both do the same
  arithmetic in the same order, so they make the same texels on any platform.
  When you add a step, add it to both and to `tests/port/art_recipes/parity.recipe`,
  then run `art_recipe_check.py` on all three discs. If a step's arithmetic
  changes, raise `PORT_ART_RUNNER_VERSION` (`port/include/port/art_recipe.h`)
  so players' caches are made again.
- **Versions differ.** Icon ids differ between JP and US/EU (use `ICON_xxx`
  names: the runner maps them). File paths differ too (`w/us/win.tpl`,
  `w/jp/win.tpl`), as do some images and their Dolphin names; use `discs` for an
  image that only fits some. Run the check on all three discs.
- **When it's made.** "Preparing graphics" (`portArtPrepare`, before the game
  starts) makes every image not already in `<user>/art_cache/<game id>/`. An
  image's cache key is its recipe file, its path, the disc's id and the
  runner's version: edit a recipe and the next start makes it again. Making is
  fast (the icons and tabs together take about 10 ms).
- **When it can't be made** (a missing source, a bad line), the log says why
  (`art: PATH can't be made from this disc (line N: ...)`) and the image's
  `fallback` stands in, or the game's own picture where there is none.
- **Testing switches.** `TTYD_ART=lazy`: no Preparing step, each image made at
  its first use. `TTYD_ART=off`: none made, the fallbacks everywhere.
- **Coordinates are texels**, x right and y down from the top left, ranges
  inclusive.
- **Keep the game's look**: 1-bit alpha (`alpha cut 128` at the end),
  sizes as the image you replace, edits in the source's own colours.

## Progress: remaking Super Luigi's art from the disc

Every row of `docs/super_luigi/ART_PLAN.md` has a recipe (ticked there). The recipes:

- **Icons and UI:** `friend_icons`, `luigi_icons`, `luigi_medallion`, `luigi_levelup`, `luigi_tabs`.
- **Maps:** `volcano_maps` (Keelhaul Key's textures recoloured where they lie).
- **Models:** `model_statue`, `model_grass`, `model_kart`,
  `model_props`, `model_late` (murals, the Slam's sets),
  `model_letters` (sign, banners, scoreboard, posters).
- **The chapter cards and Future Sight:** `titles`, `fs_small` (also the stripe, trophy, shutter and button).

US is the disc these were written for (the user's call): `art_recipe_check` is run on G8ME01. The sources are the same on JP and
EU unless the plan says so; on a disc that lacks one, the image's drawn fallback stands in.

**Crepe is cut from the style model's pieces.** His cards come from a generated design split into pieces by Qwen-Image-Edit (`art/tools/cutout.py`, `cut_crepe.py`, `art/crepe/qwen/`, the sources in `art/designs/crepe/`), on his own rig in `tools/port/sl_crepe.py`; none is a recipe. The drawn set (`crepe_ink.py`, `art/crepe/ink/`) and the older files directly in `art/crepe/` are unused.

**Eclair is cut from the style model's pieces** the same way (`cut_eclair.py`, `art/eclair/qwen/`, `art/designs/eclair/`), on her own rig in `tools/port/sl_eclair.py`; none is a recipe.

**King Marron is cut from the style model's pieces** too (`cut_marron.py`, `art/king_marron/qwen/`, `art/designs/king_marron/`), on his own rig in `tools/port/sl_marron.py`; none is a recipe. The older drawn cards directly in `art/king_marron/` are unused.

**Cranberry is cut from the style model's pieces** too (`cut_cranberry.py`, `art/cranberry/qwen/`, `art/designs/cranberry/`), on his rig in `tools/port/sl_cranberry.py`; none is a recipe. His compass piece is the drawn `art/cranberry/compass_piece.png`; the other drawn files directly in `art/cranberry/` are unused.

**The Nightmare is cut from the style model's pieces** too (`cut_nightmare.py`, `art/nightmare/qwen/`, `art/designs/nightmare/`), on its rig in `tools/port/sl_ch6.py`; none is a recipe. Its needle is Qwen's arrow and its shards are pieces of Qwen's cracked head. The drawn cards directly in `art/nightmare/` are unused. Its robe and head are larger than the GameCube's 1024-texel texture, so they are cut at half the density (their `texels_per_unit` in parts.json).

**Fuselle is cut from the style model's pieces** too (`cut_fuselle.py`, `art/fuselle/qwen/`, `art/designs/fuselle/`), on her own rig in `tools/port/sl_fuselle.py` (no longer a `c_bom_n` variant); `model_rigs.recipe` is gone. The drawn cards directly in `art/fuselle/` are unused.

**Luigi the bride's veil and skirt are cut from the style model's bridal pieces** (`cut_bride.py`, `art/veil/qwen/`, `art/designs/bride/`), hung on c_luigi_pc by `tools/port/sl_veil.py` at the drawn cards' attach points; `model_bride.recipe` is gone. The drawn cards directly in `art/veil/` are unused (their parts.json keeps the attach points).

**The Chestnut King is cut from the style model's pieces** (`cut_chestnut.py`, `art/chestnut_king/qwen/`, `art/designs/chestnut_king/`), on his rig in `tools/port/sl_chestnut.py`; `model_chestnut.recipe` is gone. His husk halves, faces, crown and the nut inside are Qwen's; the cut design has no cape and no goo card (Goo Drool's stream is the battle's effect). The drawn cards directly in `art/chestnut_king/` are unused.

**A rig move's fallback.** A variant of a disc model with only a few cards of its own. If
one of its recipes can't be made, that card shows the disc model's own texture (Crump's, Peach's ...) rather than a drawn one.

**Still drawn.** Cranberry's compass piece and Hizza's collar keep their drawn cards (the collar's recipe used Bobbery's ship's wheel, which read as a stray wheel on the snake). A poster's banner strip is
still the disc's "Glitz Pit" lettering: only its fighter is replaced.

**Hizza is cut from the style model's pieces** (`cut_hizza.py`, `art/hizza/qwen/`, `art/designs/hizza/`): the front neck in three bands along its own middle, the head and its shut and hurt edits, the tail's head from Qwen's sly edit, the S's rising stroke as the tail and the ring as the coil, on the rig in `tools/port/sl_hizza.py` (its three models: c_sl_hizza, _tail, _whole); `model_hizza.recipe` (Hooktail's cards recoloured) is gone. Only the collar with the compass piece and the dizzy stars are still the drawn cards in `art/hizza/`.
