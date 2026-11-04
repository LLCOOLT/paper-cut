# Super Luigi art: style rules

Every texture drawn for Super Luigi follows these rules. Each one is measured
from the game's own textures (decoded from the user's disc into a scratch
folder, never committed): `c_luigi`, `c_kuribo` (Goomba), `c_kuribo_y`
(Gloomba), `c_kamek` (Kammy), `c_kinopio` (Toad), `c_peach`, `c_gonbaba`
(Hooktail), `c_gesso` (Blooper) and `c_koopa` (Bowser). The numbers come from
small measuring scripts (outline width along rows and columns from the
transparent edge inward, colour histograms, a least-squares lightness
gradient, and texel density from the models' UVs and positions).

Each character's README checks its art against this list, rule by rule, with
the values measured from the new art.

## R1. Texture size

- Both sides are **multiples of 8** (the game's CMPR tiles). The game does
  **not** use powers of two: 40x40, 56x64, 104x96, 344x288, 440x392 and
  528x128 are all real sizes.
- Smallest is 16x16 (an eye or pupil), largest is 528 px on a side.
- One card per PNG here (the rig asks for that). The game packs several parts
  into one atlas texture; that's a packing choice, not part of the look.

## R2. Texel density

- **8.0 texels per game unit** for characters at Luigi's scale. Measured on
  every card of `c_luigi`: 8.00. Use this by default.
- **4.0 texels per unit** only for giant bosses (Hooktail measured 3.7 to 4.0).
- Size the card from its size in the world: px = units x density, rounded up
  to a multiple of 8.

## R3. Alpha

- **1-bit**: alpha is 0 or 255, with no partial alpha (all the game's character
  textures are CMPR. Measured: 2 alpha levels and 0 partial texels in every
  reference).
- Edges are hard. The only anti-aliasing is in colour, where the line meets
  the fill (about 1 px of blend). Transparent texels are black (0,0,0,0).

## R4. Outline

- **Width at 8 texels/unit: median 4 to 5 px, never under 3 or over 9.**
  Measured medians: Luigi 4 to 5, Goomba 5, Gloomba 5, Toad 5, Peach 3 to 4,
  Kammy 2 to 5. The 10th to 90th percentile spread is 2 to 9, so the line is
  not constant.
- At 4 texels/unit (giant bosses): 3 to 5 px (Hooktail 3 to 4). In the world
  it is about twice as thick as at 8/unit.
- **Heavier on the bottom and right**: the bottom/right side is 1 to 3 px
  wider than the top/left side (Hooktail parts: L3 R4 T5 B7, and L4 R10).
  Light comes from the top left.
- **Hand-cut, not geometric**: the width wobbles along the contour, and the
  shapes are slightly irregular (no perfect circles or straight edges).
- **Colour: a dark tint of the fill's own family, never pure black.** The
  line's luminance is ≤ 60 (the 90th percentile was 24 to 59), HSV value 0.15
  to 0.35, saturation 0.5 to 0.9. Measured: on red `#522808` / `#521014`, on
  pale green `#294921`, Blooper navy `#10184a`, Luigi green `#182810`, Kammy
  `#392010`, white gloves `#313039`.
- **A part with two colour regions changes line colour with the region**
  (Hooktail's head: brown line on the red, dark green on the green belly).
- **Eye whites have a thinner, lighter line**, about half the body's: Goomba
  eye 2 to 3 px `#735929`, against 5 px on its body.
- Interior lines (mouth corners, seams, scales) use the outline colour, at 2.5
  to 4 px.

## R5. Fill colour

- Base fills: **saturation 0.35 to 0.85, value 0.65 to 0.95.** Measured:
  Hooktail red `#bd2c39` (s .77 v .74), Hooktail belly `#a5b66b` (s .41 v .71),
  Luigi green `#39a639` (s .66 v .65), Blooper `#dee7e7` (whites up to v 0.95).
- Markings and darker accents: value 0.30 to 0.50 (Hooktail wing membrane
  `#423452`).
- No pure white body fills except highlights and the whites of eyes. Toad and
  Blooper's "white" is `#dee7e7` / `#dedfde`.
- **Neutral materials** (white cloth, stone, metal, bone): saturation 0.02
  to 0.25. Their hue leans warm or cool, never a dead grey. Measured:
  Goomba wing `#bdbcb5` (s .04 v .74), Hooktail tooth `#dedfde`, and Luigi's
  glove line `#313039`.
- **The far side of a pair** (back arm, back foot): one tone darker, with
  value 0.05 to 0.10 lower. It may drop just under 0.65.
- **Emissive parts** (lava, glowing eyes, a lit gem): value up to 1.0.
  Their line is a dark burnt tint of the glow, lum ≤ 60.

## R6. Shading

- **Light from the top left.** Lightness falls toward the bottom right on
  every shaded reference (gradient in lum per 100 px: Goomba −23/−21, Kammy
  −26/−76, Luigi's cap −329/−384).
- **At 8 texels/unit: soft cel shading.** A flat base, a soft shadow band
  along the bottom/right inside edge (about 0.80x the base value), and a
  softer, lighter band on the top left (about 1.1x to 1.2x). The bands are about 6 to
  10 px wide with a 3 to 4 px soft edge. The base colour covers at least half
  the part. There is no full-body gradient.
- **At 4 texels/unit (giant bosses): mostly flat, up to 3 hard tones.** Base,
  light (about +0.07 value) and shadow (about −0.09 value, +0.08 saturation),
  for example Hooktail `#bd2c39` / `#ce3c42` / `#a51821`.
- No texture noise, no hatching, no rim light.

## R6b. Highlight (sheen)

The game's rounded parts carry one distinct sheen. It was measured with a
hue-aware finder: texels of the part's own hue, more than 12 lum above its
median, at least 5 px inside the edge. "White strength" is how much white
over the base gives the sheen colour.

| Reference | Area | Centroid (x, y) | Extent (w, h) | Value change | Saturation change | White strength | Edge |
|---|---|---|---|---|---|---|---|
| Luigi's cap `c_luigi t_6`, `#3caa39` → `#6cc34e` | 7% | 0.49, 0.21 | 0.38, 0.42 | +0.10 | −0.07 | 0.22 | ≤ 1 px |
| Peach's sleeves `t_12`/`t_14`, `#ff80bd` → `#ffb8dd` | 14–23% | 0.34–0.49, 0.32 | 0.48–0.60, 0.40–0.46 | 0 (at v 1.0) | −0.22 | 0.31 | ≤ 1 px |
| Peach `t_0` (near-white base) | 10% | 0.47, 0.31 | 0.41, 0.32 | +0.04 | −0.21 | 0.66 | ≤ 1 px |
| Gloomba's head `c_kuribo_y t_8` (matte) | 8% | 0.39, 0.21 | 0.49, 0.36 | +0.07 | −0.04 | 0.11 | about 20 px ramp |
| Goomba, Kammy's robe, Toad's face, Bowser's arm | none | | | | | | |

- **Where:** one sheen on each rounded or shiny volume, such as heads,
  caps, sleeves, bodies, domes and the segments of a body. Flat cloth and
  skin can go without.
- **Shape:** one soft-cornered oval or band following the upper contour.
  - Centre at x 0.30 to 0.50 and y 0.20 to 0.35 of the part's box. Always in
    the upper half (≤ 0.55), even when something else sits on top, such as
    eyes on a head.
  - Extent 0.4 to 0.6 of the width and 0.3 to 0.45 of the height, covering
    **7 to 25% of the part**.
- **Inside the line:** leave 2 to 3 px of base colour between the sheen and
  the outline.
- **Colour:** the base with **0.20 to 0.35 of white over it** (0.30 by
  default). On near-white bases (value 0.9 or more) use 0.6 to 0.7 of white, as on Peach `t_0` (0.66), or the sheen disappears.
  - Saturation drops 0.07 to 0.22.
  - Value rises up to 0.10, capped at 1.0.
  - Never pure white.
- **Alpha:** opaque texels, since the sheen is colour only (R3).
- **Edge:** crisp, with a 1 to 2 px transition (a blur of about 0.7 px). A
  soft dome like Gloomba's is the variant for dull, matte surfaces.
- **Specular dot:** only on eyes and glossy props. It's a pure-white dot
  about 0.12x the eye's width, in the eye's top left.
- **Check:** `tools/check_style.py` prints the sheen's area %, centroid and
  white strength for the largest part of each card.

## R7. Eyes

- **Two constructions**, depending on how the eye is used:
  - **Baked into the head card** (small characters, and any head that swaps
    whole for its expressions): a pale sclera oval, value about 0.95, with the
    thinner eye line (R4); a dark vertical oval pupil (value under 0.15) about
    0.25x the eye's width and 0.6x its height, set low and toward the front;
    and a white dot in the top left.
  - **Swappable iris** (Hooktail): a sclera card with a separate iris
    texture. The iris's ring line is about 6 px at 48 px, and the iris is
    about 0.42x the eyeball's diameter. Expressions swap the iris for
    strokes.
- **Expression strokes** (shut, squeezed, KO) are single dark strokes, 4 to
  7 px wide with round caps, in the line colour or navy `#10182e`: an arc for
  shut, `>` and `<` for squeezed shut, an X for knocked out (Hooktail `t_6`
  to `t_9`, Blooper `t_6`).
- Lids are the body colour, lined like the body.

## R8. Construction

- Separate cards for parts that move: head, jaw, eyes or face, arms,
  hands, feet, props, and body segments. Long bodies are chains of
  overlapping round discs, as on Hooktail's neck.
- **Overlap**: each card overlaps its neighbour by about 15 to 30% of its
  size, so the line of the card in front covers the joint. No seam shows
  through.
- Expressions are whole-card swaps (`head`, `head_blink`, `head_hurt`), all
  the same size with the same pivot, or swaps of the face layer.
- Pivots are given in pixels from the card's top-left corner.

## R9. Process

- The source is SVG, built by `tools/papercut.py` (smoothed point lists with
  a seeded jitter for the hand-cut edge, a biased double outline for R4, and
  clipped light and shadow crescents for R6). The PNG is resvg's render,
  hardened to 1-bit alpha (R3).
- Before committing, check each card: size (R1), alpha (R3), outline width
  and colour (R4), and fill and value ranges (R5). Look at a preview sheet
  beside the game's references at the same texel scale.

## R10. Map textures (walls, floors, lava)

Map textures follow different rules from character cards. These were
measured on Keelhaul Key's caves (`m/muj_10/t`, 26 textures).

- **Sizes are powers of two:** 32, 64, 128 or 256 on a side (the rock wall
  `t_6` is 256x256, the pebble floor `t_12` is 128x128). A replacement
  keeps the size of the texture it replaces, so the map's texel density
  stays the same.
- **They tile.**
  - Wall and floor fills wrap both ways. Measured on `t_6`, the left/right
    seam differs by 16.2 and the top/bottom by 16.0, against 13.4 between
    neighbouring texels. `t_12`: 20.6 and 17.8.
  - Band textures, such as the streaked cave wall `t_21`, wrap
    horizontally only (top/bottom 168.7).
- **No outlines.** Shapes are separated by flat tones and a **dark drop
  line** two texels deep along the lower edge of each stone.
- **Flat tones, 3 to 5 per material**, all close in hue. `t_6`: base
  `#ce8e18` 46%, light `#efae42` 19%, dark `#9c6500` 18%, plus deep accents
  `#6b4100` and `#6b2c00` at 3 to 4%.
  - Light patches are blobs inside each stone, not crescents.
  - There are no blurred gradients, except in band textures (`t_21`'s
    vertical drips are flat steps too).
- **Grout** is a darker tone with a **stipple** of darker dots: a 2x2 dot on
  an 8-texel grid, with alternate rows offset by half a step.
- **Alpha:** CMPR fills are opaque. Edge strips (`t_2`, `t_16`: rock clumps
  along a ledge) have 1-bit alpha. Effects use IA4.
- **Saturation runs high** (s 0.7 to 1.0 on `t_6`'s rock). Values run 0.35
  to 0.95.

## R11. Chapter title props (the storybook diorama)

Measured on the game's own title-card props (`a/TIZ_stg_1`, `a/TIK_stg_2`, `a/TIK_stg_3`): the flat paper cutouts that stand behind "Chapter N" in the pop-up book.

- **Power-of-two textures**, 16 to 128 on a side, with 1-bit alpha (CMPR).
- **No dark outline.** The darkest edge tone measured lum 134 to 153, against a character line's 60 or less. Shapes read by their silhouette alone.
- **One or two flat tones per piece**, with no blur. `TIZ_stg_1`'s castle is one tone, `#738ea5` (s .30, v .65), for 84% of the piece. A second, harder tone falls on the lower right.
- **Muted large shapes** (s 0.0 to 0.35: grey trees, white clouds, blue-grey stone) with **a few saturated accents** (a green roof at s 1.0, an orange door).
- **Dark holes** for doors and windows (`#103c5a`, v .35), and no other interior detail.
- **The chapter's name is game text** in the game's font, from the message file, not a texture.

## R12. Lettering (signs, banners, posters)

Measured on the game's own painted text (`m/tou_02/t` image 19, "WELCOME";
`m/yuu_02/t` images 17, 18, 20 and 21, the Pianta Parlor and casino signs;
the Glitz Pit posters in `m/tou_02/t` images 3, 18 and 21).

- **Chunky capitals cut from paper.** Stems about a third of the cap height,
  small square counters, corners chamfered rather than rounded, no thin
  strokes and no serifs to speak of.
- **Hand-cut, not typeset.** Each letter is a little off square: its own
  slight tilt (up to about 4 degrees), size and bob on the line.
- **On a sign, each letter turns to the arch** it's set on (PIANTA over
  PARLOR), faced in a ramp from red at the top to yellow at the foot, with a
  light grey edge and a darker drop below right.
- **On posters and boards, a flat face with a dark edge.**
- **No system fonts.** Paper Cut's lettering is
  [`tools/lettering.py`](tools/lettering.py): every glyph is a few polygons
  on a 12-unit grid, jittered from a seed, so it renders the same on any
  machine and needs no font licence.

