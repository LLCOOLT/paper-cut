# The walking stone statue (`c_lp_statue`), chapter 1's boss

The statue is an ancient idol of Rumblebump Volcano, 68 units tall. These
drawings replace the first, flat clip-art ones. They follow STYLE.md: heavier,
uneven outlines; painted stone with soft top-left light and a matte sheen;
carved grooves with a lit lower lip; lava glowing in the cracks; and the red
gem in the forehead as the focal point.

- **Sources:** the `.svg` files are made by
  [`../tools/statue.py`](../tools/statue.py). Rerun it to rebuild the PNGs
  and `preview.png`.
- **Where the part list lives:** `tools/port/statue_model.py` and
  `docs/LUIGI_ANIMS.md` on branch `super-luigi-anims`. That code reads each
  `<file>.png` from this folder by name, and uses its own drawing for any
  file that's missing.
- **Layout:** every PNG has exactly the size, and covers exactly the
  rectangle, of the drawing it replaces. They use the same canvas: 14.4
  texels per drawing unit (8 per model unit), padded to a multiple of 8 at
  the right and top. So the bottom-left corners, pivots and z values in that
  table apply unchanged.

## Parts

All the names match `statue_model.py`.

| File | Size | Part | What it is |
|---|---|---|---|
| `head` | 416x368 | `head` | A stone block with a chipped crown and a heavy brow ledge casting shadow over the eye sockets. It has a long wedge nose, a grim mouth with carved teeth, zigzag cheek glyphs and two lava cracks. The **gem's setting**, a carved ring of eight rays around an empty socket, is drawn on it. |
| `eyes` | 288x88 | `eyes`, lit | Glowing slits in the sockets: orange `#ffb347` with a pale core `#fff3b0` and a white glint. They're opaque (1-bit alpha), in place of the old half-transparent glow. |
| `eyes_dark` | 288x88 | `eyes`, bind 2 | The slits gone dark: stone-black with a faint ridge |
| `gem` | 144x144 | `gem`, whole | A brilliant-cut ruby seen from the front. Its eight facets are lit from the top left in four tones (`#ff7a68`, `#df2f2a`, `#9a1713`, `#640b09`), with a sheen on the table and a white glint. |
| `gem_glow` | 144x144 | `gem`, bind 2 | Charging: a brighter gem (`#ff4a34`), a touch smaller, in a 16-point burst of light |
| `gem_cracked` | 144x144 | `gem`, bind 3, and `gem_loose` | Dulled (`#b8312b`), split by a crack with a pale edge, and chipped at the upper right |
| `gem_glint` | 144x144 | `gem_loose`, bind 2 | The loose gem catching the light: `gem_cracked` on a pale gold halo, with a white four-pointed sparkle at its top left |
| `body` | 424x200 | `body` | The torso: a studded belt, the volcano spiral carved on the chest, lava cracks, and soot at the hips |
| `arm_f` | 144x224 | `arm_f` | The upper arm block and fist, with carved knuckles |
| `arm_b` | 144x224 | `arm_b` | The same in the far-side tone (`#978a76`) |
| `foot_f` | 176x80 | `foot_f` | A block foot with carved toes |
| `foot_b` | 176x80 | `foot_b` | The same, far-side tone |
| `rubble_1`..`rubble_6` | 208x176 | `rubble_1`..`6` | Six chunks of the body with broken, notched edges, each keeping a scrap of the carving: `rubble_1` a lava crack, `rubble_2` to `rubble_4` grooves, `rubble_5` a corner of a zigzag glyph, `rubble_6` a hairline lava crack. Each card is 14 by 12 drawing units round the chunk's middle, with the outline at 1.45x, as `statue_model.py` lays them out. The odd ones are the near stone, the even ones the far stone's darker tone. |

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All 15 files have exactly the sizes in the spec. |
| R2: 8 texels a model unit | **pass** | 14.4 texels per drawing unit (1.8 model units), as `statue_model.py` lays it out |
| R3: 1-bit alpha | **pass** | Every file has 2 alpha levels and 0 partial texels. The old eyes and glow had partial alpha; they're now opaque. |
| R4: outline 4 to 5 px median (3 to 9), heavier bottom/right | **pass** | Stone parts: median 5 to 6, p10–p90 4–7, L4–5 T4 against R6–7 B6–7. The old outlines were an even 3.5 px. Eyes and glow: median 4. |
| R4: hand-cut edge | **pass** | Seeded jitter of 0.6 px, chips in the crown and notched rubble edges |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Stone `#33291f` (lum p90 44); far side `#2c231b` (37); gem `#3c0705` (24); glow `#5a2204` (48). |
| R5: neutral materials s 0.02–0.25; far side a tone darker; emissive v ≤ 1 | **pass** | Stone `#a99b86`: s .21, v .66, warm. Far side `#978a76`: v .59, 0.07 darker. Gem `#df2f2a`: s .81, v .87. The glows reach v 1.0. |
| R6: soft shading lit from the top left; no texture noise | **pass** | Stone shadow `#76695a`, light `#c8bca6`, 4 px blur. The old per-pixel speckle is gone. Weathering is a few soft painted patches and soot bands. |
| R6b: sheen 7–25%, upper half, matte stone at about 0.2 | **pass** (see note) | Head 16.7% at (0.39, 0.37). Body 13.3% at (0.39, 0.17). Arms 19%, feet 18%, rubble 9.7–11.4% (`rubble_4` and `rubble_6` 1–2%, see the note). White strength 0.18 to 0.27. Gem 16% with a 0.50 glint, glossy. |
| R7: eyes as expression strokes or swaps | **pass** | `eyes` and `eyes_dark` are whole swaps. |
| R8: separate cards, overlap | **pass** | The same parts and placement as the rig, with the brow ledge overlapping the eyes card's top. |

**Note on the rubble:** each chunk carries the same small sheen oval. On
`rubble_4` and `rubble_6` (the far stone's darker tone) the finder takes
part of the top-left light band as the base, so it reads only 1 to 2%; the
sheen is there in the preview.

## Rough edges

- The preview assembles the parts at rest from `statue_model.py`'s
  placement, which I copied here and checked against the old drawings'
  layout. I haven't built or seen them in the model or the game.
