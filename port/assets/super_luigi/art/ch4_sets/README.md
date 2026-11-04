# The Drama Slam's sets (Super Luigi, chapter 4)

The three painted sets of "The Mystery of the Fiery Hat of Social
Awareness", for the stage battle (`docs/super_luigi/ch4.md`, New assets 4:
"flat painted backdrops (a country road with a cardboard sun; a ballroom; a
volcano that 'erupts': a flame card that rises)", as battle stage props in
the style of the game's stages).

- **Sources:** made by [`../tools/ch4_sets.py`](../tools/ch4_sets.py), which
  also writes `parts.json` and `preview.png`. The ballroom and the volcano are
  the style model's painted backdrops (`../designs/sets/`, ART_PLAN wave 3):
  the ballroom is `ballroom_seed1.png` with its chandelier filled in by
  `no_chandelier()` (the chandelier is a prop of its own that drops), the
  volcano is Qwen's quiet edit `volcano_quiet_s1.png`. Each is cropped to its
  band, median-3, 48 colours, opaque, given the road's battens and CMPR'd.
  In game the road is the recipe's (Petal Meadows' own backdrop).
- **Scale:** a backdrop is 64 units wide and 32 tall, the width of the battle
  stage's back: the road at 8 texels per unit (512x256), the painted ballroom
  and volcano at `tools/cutout.py`'s density (2048x1024, 32 texels per unit),
  median-3 at that size. The cardboard sun is cut at the same density (512x768);
  the flames are drawn at 8. Each card's `texels_per_unit` is in parts.json,
  and sl_rig reads it.
- **Style:** the backdrops are scenery canvases, so they follow STYLE.md R10
  (map textures): power-of-two sizes, opaque, flat tones with hard edges, no
  outlines, and a dark drop line under each shape. A wooden batten runs along
  the top and bottom of each, so it reads as stage scenery. The cut-outs that
  move in front of them are outlined prop cards.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `set_road` | 512x256 | (256, 256) | Act 1: a country road winding between green hills to the horizon, a fence, two round trees, flat clouds on a blue sky. The pivot is the bottom middle, on the stage floor at the back. |
| `set_ballroom` | 512x256 | (256, 256) | Act 2 (painted): a gold-walled ballroom with wall sconces, red drapes swagged across three starry windows and tied back, a red carpet on a checkered floor; no chandelier (`c_sl_chandelier` is the falling prop) |
| `set_volcano` | 512x256 | (256, 256) | Act 3 (painted): a grey volcano glowing at its crater, (238, 75), on a sunset sky over a lava lake and rocks; quiet, the flame card erupts |
| `cardboard_sun` | 128x192 | (64, 188) | Act 1's cardboard sun on a stick, smiling, with a couple of scuffs. A stagehand raises it from behind the set. The pivot is the stick's foot. |
| `volcano_flame_0`, `volcano_flame_1` | 128x192 | (64, 188) | The eruption: a flame card in two flicker frames. Slide it up out of the crater from behind the backdrop, and alternate the frames. |

Swap the backdrop per act; the stage's front (the proscenium and curtains)
is the game's own Poshley battle stage.

## Checks

| Check | Status | Measured |
|---|---|---|
| R10: power-of-two backdrops | **pass** | 512x256 |
| R10: opaque, flat tones, no outlines, drop lines | **pass** (road) | Alpha 255 throughout. Each material has 2 to 4 flat tones (hills `#7cb45a` / `#5ea042` / light blobs `#86c066`, drop `#447c2e`); the drop line is 2.5 px under each shape. |
| R1: cut-outs in multiples of 8 | **pass** | 128x192 |
| R3: 1-bit alpha on the cut-outs | **pass** | 2 levels, 0 partial texels |
| R4: outline at 4 texels/unit, 3 to 5 px, dark tint | **pass** | Sun and flames: see `check_style.py` (median 4 to 5); lines `#5a2e04` and `#5a1604` |

## Rough edges

- The painted ballroom and volcano have the style model's inked lines and 48
  colours, not R10's few flat tones: they were judged against the drawn sets
  in game and read as painted stage scenery where the drawn ones read as a
  collage (ART_PLAN rows 6 and 7).
- The ballroom's cornice is plain where the chandelier was.
