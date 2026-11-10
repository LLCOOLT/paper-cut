# Art recipes: art made from the player's own disc

The port's new art should look like the game's, and the surest way is to start
from the game's own pixels: Luigi's faces for Play as Luigi made from his own
head on the disc, framed as Mario's icons are. But **nothing from the disc may
be committed** to this public repo, and a PNG that is mostly the disc's texels
is disc data.

So the repo holds **recipes**: short text files that say how to make each
image from the disc. The game runs them on the player's own disc when it first
starts, keeps the results in the user folder, and then loads them exactly where
it loads committed art.

- **Recipes:** `art/recipes/*.recipe` (built into the game; paths here are
  under `port/assets/luigi/`).
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

### Shared steps

| Line | What it does |
|---|---|
| `steps NAME` ... `end` | Names a block of steps, outside any image. Nothing runs where it is written. |
| `use NAME` | In an image (or in another block, up to 8 deep), runs the block's lines here, as if written in their place: its pieces, masks, colours and gradients are the image's own. An error names the block's line. Define a block before the images that use it; a later block of the same name replaces it. |

Several images that start the same way share one block: every Luigi face
builds his head with `use luigi_cards` and `use luigi_head`, and then each
image makes that head its icon. An image can work on a block's pieces between
two `use`s (`@c5 flip x` between loading the cards and pasting them).

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
| `mask NAME circle CX CY R` | A disc: the texels whose centre is within R of (CX, CY) (texel X's centre is X + 0.5). For a coin's face. |
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
| `scale W H smooth` | To W x H, each texel bilinear between the four source texels round its centre, weighted by alpha, with 1-bit alpha (in where half is covered). For taking a disc texture up a long way (4x): the outline stays smooth where `texel` would show the source's blocks. |
| `grow R COLOUR` | A border R texels wide round the opaque shape (a round brush), in the colour: only clear texels change. A 1-texel line round a scaled-down card (`grow 1 #392821`), or a sticker's edge (`grow 1 #e7e7e7`, then `grow 1 #a5a5a5`). |
| `paste PIECE X Y [over/copy]` | Draws a piece onto the canvas (or onto `@another`) with its top left at X, Y. `over` (the default) lays it over by its alpha; `copy` replaces the texels, clear ones too. |
| `slide down/up/right/left AT FROM-TO:BY ...` | Moves part of the image: `slide down 38 15-31:2` moves columns 15 to 31 down 2 texels from row 38, the row above repeated into the gap, the line and border with it (a longer jaw). `up`, `right` and `left` likewise (`AT` a row or a column). |

### Painting

| Step | What it does |
|---|---|
| `paint X Y` + rows | Sets texels from rows, top left at X, Y: each character a one-character colour name, `.` keeps the texel, `_` clears it. For small edits (an emblem's letter, a mouth): keep them small, and in the source's own colours. |
| `stamp MASK COLOUR` | Fills a mask in one colour. |
| `stamp MASK edge C top C bottom C fill C` | Paints a mask as a shaded band, the way the game's moustaches are: its outline (any texel with a side outside the mask) in `edge`, the row under the top edge in `top`, the row over the bottom edge in `bottom`, the rest in `fill`. |

### Drawing a map's object

| Step | What it does |
|---|---|
| `mesh MAP OBJECT pole PART [closed] [turn D] [pitch D] size K at X Y [line COLOUR]` | Draws object `OBJECT` of the map `MAP` (its `MAP/d`, textures `MAP/t`: `m/muj_01`) onto the image, side-on, as a hand-drawn card: the object's parts as the map places them (scale and translation; a turned part is refused), its part `PART` taken as a pole and stood upright (its line from its middle to its far ends, pointing toward the other parts), its foot at `X`, `Y`, `K` texels a map unit. `closed` folds the other parts (a canopy) down the pole as a furled parasol. `turn D` turns it about the pole, `pitch D` looks down on it from `D` degrees above (whole degrees). Each triangle is one flat colour: its lightest vertex colour times its texture's texel under its middle, in one of three light bands by its facing (the inside of a canopy darker): no smooth shading, no texture detail. A z-buffer keeps the nearest; `line` draws a 1-texel line on a part where another part lies in front of it. Only the texels it covers change (opaque); add the outline with `grow`. |

Its sines are taken on 1/65536ths and its arithmetic is sums, products, quotients and square
roots in doubles in a fixed order, so both runners draw the same texels. Peach's parasol
(`art/recipes/peach_parasol.recipe`) is made with it.

## A worked example

### The icons (`art/recipes/luigi_icons.recipe`)

Every face of Luigi's (`ICON_1A3` the HP face, `ICON_19E` the pause menu's
head, `ICON_1A1` the level row's coin and level-up's BP panel) is **Luigi's
own head**: the cards of `c_luigi` (his field
model) put together as pose `Z_1` stands them, then framed as Mario's face is
in that icon. Earlier versions recoloured Mario's icons, and kept Mario's face
shape: round and wide, Mario's nose and cheeks. Luigi's face is longer and
narrower, with a longer chin and his own nose and moustache.

The head is two blocks. `luigi_cards` loads the ten head cards as pieces
(the face with its chin, eyes, moustache, nose, ear, nape hair, cap, its L,
the dark under the brim) and samples his line colour. `luigi_head` pastes
them, back to front, where the model has them (8 texels a unit, the disc's own
density), and cuts off the neck under the chin:

```
steps luigi_head
  piece head canvas 96 116
  @head paste c4 4 27       # the dark under the brim
  @head paste c7 53 73      # the hair at the nape
  @head paste c14 18 28     # the face
  ...
  @head paste c6 0 0        # the cap
  @head paste c5 31 13      # its L
  @head paste c11 64 54     # the ear
  mask neck poly 0,116 0,97 30,97 ... 96,92 96,116
  @head alpha set 0 in neck
  @head crop 0 0 96 104
end
```

The card positions come from the model: each card's quad in `Z_1`, frame 0,
evaluated as the game does (`tools/port/ttyd_anim.py`). Then each image frames
the head:

```
image art/icons/luigi/luigi_hud.png
  fallback art/icons/luigi/drawn/luigi_hud.png
  canvas 52 52
  piece heart icon ICON_19F                       # the HP heart behind it, top right
  @heart crop 2 3 36 33
  @heart scale 22 20 texel
  paste heart 23 6
  use luigi_cards
  @c5 flip x                                      # the L reads right way round once flipped
  use luigi_head
  @head grow 2 line                               # his line, heavier at icon size
  @head crop -14 -12 124 128                      # room to turn
  @head flip x                                    # facing right, as Mario's
  @head rotate 15                                 # tilted forward, as Mario's
  @head scale 36 37 texel
  @head ramp in solid where v<0.45 base #312410 to hudline   # Mario's dark line weight
  @head grow 1 #2a1408
  paste head 0 5
  grow 2 #e7e7e7                                  # the sticker, round heart and head as one
  grow 1 #a5a5a5
  ...                                             # its dark grey shadow down and right
  alpha cut 128
end
```

- **The HUD and the menu head** are in his own colours. The menu head is the
  same head, flipped, filling 40x40, with a 3-texel line.
- **The gold faces** (the coin and the panel) use
  `luigi_gold_cards` and `luigi_gold_head`. The L is filled in the line's
  colour, and the moustache's fill is marked, because it is the line's own
  brown. Each part then takes the face's tones by its colour: skin and the L's
  disc on `gskin`, cap and hair on `gdark`, the moustache `gmoustache`, every
  line `gline`. The image sets these before `use`. After scaling, a stepped
  `ramp` posterises the head to the face's few tones, as the game's are
  engraved: the coin to its line and two golds, the panel to its brown and
  line.
- **Mario's head comes off** each gold face first. The coin's plate is a
  `circle` filled with its own sampled gold. Where his lines cross the coin's
  bevel, they are replaced from the coin mirrored about its lit diagonal
  (`flip x`, `rotate 270`: the light comes from the top left, so the bevel's
  shading is the same either side of that diagonal). On the panel, the middle
  is filled flat. Round the edge, only Mario's own colours are filled, so the
  coin's highlight and shading stay.

The game draws these in place of Mario's while Play as Luigi is on
(`port/src/aurora/luigi_faces_aurora.c`: the three icons through
`portIconTexOverride`, the panel as a Dolphin-named texture in the `*#luigi`
texture set). The drawn fallbacks (`icons/luigi/drawn/`) are
`tools/luigi_icons.py`'s drawing of the same head, laid out the same way.
For the game to use any other recoloured texture, name it as Dolphin names a
replacement (`tex1_WxH_<XXH64>_14.png`, from a texture dump: Settings, Mods,
dump textures) and add it to a texture set as the panel is
(`portModsAddMapTexture`): the set's pattern is matched against the map's
name, `*#luigi` meaning "any map, while Luigi is the player".

### Lettering from the game's own letters (`art/recipes/luigi_tabs.recipe`)

The pause menu's first tab, "Mario", as "Luigi" (and ルイージ): the tab's own
board with its lettering filled over, and each letter cut from another of the
game's tabs or bars. `alpha set 0` clears that tab's board colour, and `map`
moves its lettering, line and blend to the Mario tab's colours. Where the game
has no glyph, join strokes of others: ル is リ's long stroke and し. It goes in
the `*#luigi` texture set as the panel does; its drawn fallbacks are
`tools/luigi_icons.py`'s (`tab_us`) and `tools/luigi_tab_jp.py`'s (`tab_jp`).

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
  fast (the icons take a few ms).
- **When it can't be made** (a missing source, a bad line), the log says why
  (`art: PATH can't be made from this disc (line N: ...)`) and the image's
  `fallback` stands in, or the game's own picture where there is none.
- **Testing switches.** `TTYD_ART=lazy`: no Preparing step, each image made at
  its first use. `TTYD_ART=off`: none made, the fallbacks everywhere.
- **Coordinates are texels**, x right and y down from the top left, ranges
  inclusive.
- **Keep the game's look**: 1-bit alpha (`alpha cut 128` at the end),
  sizes as the image you replace, edits in the source's own colours.
