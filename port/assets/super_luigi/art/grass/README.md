# Luigi as the Grass (Super Luigi, chapter 4)

Luigi's part in "The Mystery of the Fiery Hat of Social Awareness": the
grass by the side of the road (`docs/super_luigi/ch4.md`, New assets 1: "a
bush of long grass blades over Luigi, his face and moustache peeking out of
a gap, his shoes at the bottom").

Like the bride's veil and gown ([`../veil/README.md`](../veil/README.md)),
these are **accessory cards pinned to `c_luigi`'s parts**, so they ride his
own field and battle poses: stand, the walk shuffle, the lie-flat pose on the
stage, the rustle, and his battle set. `c_luigi`'s cards are double-sided
(cull mode 3), so these flip with him when he turns, as the veil's do.

- **Scale:** 8 texels per unit, facing -x like the model.
- **Sources:** made by [`../tools/grass.py`](../tools/grass.py), which also
  writes `parts.json` and `preview.png`. The preview puts the cards on a grey
  stand-in for Luigi, since his textures are disc data.

## Look

Long tapered paper blades in a spring green (`#74c23e`), a few with
yellower tips (`#9ccc44`), each outlined in a dark green (`#163a0c`) with a
shadow-tone midrib. The far blades behind him are a deeper green
(`#4fa036`). A leafy mound covers his cap under the crown of blades.

## Cards

Pins are as in the veil's README: **model** is the pin in `c_luigi`'s bind
pose (feet at y 0, cap top at y 24.3); **on the part** is the same point in
the texels of the card it pins to, from that card's top-left. The head card
is `pPlaneShape14` (64x88, x −3.7..4.3, y 9.8..20.8), the body card
`pPlaneShape17` (64x72, x −3.7..4.3, y 3.6..12.6).

| Card | Size | Pivot | Pin, model | Pins to | Pin, on the part | Layer | What it is |
|---|---|---|---|---|---|---|---|
| `grass_back` | 184x264 | (92, 208) | (0.5, 8.0) | body, pPlane17 | (33.6, 36.8) | Behind all of his cards | A tall fan of far blades, reaching above the cap and out past his sides |
| `grass_front` | 184x144 | (84, 80) | (0.5, 8.0) | body, pPlane17 | (33.6, 36.8) | In front of the body and legs, behind the front glove | The near blades over his body and legs. They're low on the face side (up to y 7 to 10 at the front), so the moustache shows, and taller toward the back. His shoes show below y 1. |
| `grass_front_sway` | 184x144 | (84, 80) | (0.5, 8.0) | body, pPlane17 | (33.6, 36.8) | The same | The same blades swept back (+x), for the walk shuffle and the rustle: swap it in and out |
| `grass_hood` | 168x184 | (76, 80) | (0.5, 22.0) | head, pPlane14 | (33.6, −9.6) | In front of the cap | A leafy mound over the cap and a crown of blades: forward over the brim (stopping above the eye), up, and back down the head, with a curtain of five blades hanging down the back of the head behind the ear to y 11, where the front blades take over |
| `grass_hood_sway` | 168x184 | (76, 80) | (0.5, 22.0) | head, pPlane14 | (33.6, −9.6) | The same | Swept back, to swap with `grass_hood` |

**The rustle** (the happy shimmy for the fans): alternate `grass_front` and
`grass_hood` with their `_sway` copies every 4 to 6 frames while he wiggles.
**Lie flat:** nothing extra; the cards ride his lying pose (the preview
turns the whole stand-in by 90°).

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 144 to 264 px |
| R2: 8 texels/unit | **pass** | Sized against `c_luigi`'s bind pose |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 4–5 px, 3–9 | **pass** | Median 6 to 7 (p10–p90 4–10). Where blades cross, the finder counts the line of the blade behind with the front one's, which lifts the median; a single blade's line is 4 to 5. |
| R4: heavier bottom/right | **pass** | L4 against R6 to R10 on every card |
| R4: line colour, lum ≤ 60 | **pass** | `#163a0c` (lum p90 43 to 48), the far blades `#123010` (36) |
| R5: fills | **pass** | `#74c23e` s .68 v .76. Yellow tips `#9ccc44` s .67 v .80. Far blades `#4fa036` s .66 v .63, one tone darker, as the far side of a pair. |
| R6: soft top-left shading | **pass** | Bands 3 to 4 px with a 1.5 px blur on each blade |
| R6b: sheen | **pass** | One small sheen on each long blade, white 0.30. The finder reads the card as one part: 18 to 29%, because the light bands of many thin blades count too. |

## Rough edges

- The back of his head (hair, ear and cap back, x 0.5 to 6, y 11 to 20)
  used to show between the hood and the front blades. The hood's curtain of
  hanging blades now covers it; checked over `c_luigi`'s own bind pose
  (decoded into scratch), where only his face, from the eye to the
  moustache, shows.
- Not checked in his animations.
