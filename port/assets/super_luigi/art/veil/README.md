# Luigi the bride: veil and gown (Super Luigi, chapter 2)

Luigi disguises himself as Hizza's bride (`docs/super_luigi/ch2.md`, New
assets 2), wearing a white veil over the cap and down the back, and a full
white gown from the waist down with his boots peeking out. These are
**accessory cards** that pin to `c_luigi`'s parts and ride his existing
poses.

- **Scale:** 8 texels per unit, like Luigi's cap and head.
- **Facing:** drawn facing -x, like the model.
- **Sources:** made by [`../tools/veil.py`](../tools/veil.py). Rerun it to
  rebuild the cards, `parts.json` and `preview.png`.
- **The committed preview** puts the cards on a grey stand-in for Luigi,
  because his real textures are disc data. A version on the real `c_luigi`
  bind pose was checked in scratch.

## How they read as Luigi turns

`c_luigi`'s cards are all drawn with cull mode 3, which `animdrv.c`
(`animSetCullMode`) maps to `GX_CULL_NONE`. So they're double-sided: when
Luigi turns, each card flips, and from the other side the game shows the
same texture mirrored. He has no front or back textures of his own.

- **Side view:** the veil and gown cards ride the same flip, so the side
  cards cover both facings (left, and right mirrored). Edge-on, mid-turn,
  they read as a line, just as his own cards do.
- **Back view:** `veil_back` is for poses that face away from the camera,
  if the rig or the playable Luigi pack has any. It replaces the side veil
  cards there. The gown has no back card, because the double-sided side
  card serves.

**Nothing covers his face.** The veil hangs from the back of the cap: a
flower and pearl band where it meets the cap, a lace edge, and a train down
over his back and shoulders to about y 7.

## Cards

Each pivot is at the card's pin point.

- **Pivot:** pixels from the PNG's top-left corner.
- **Pin, model:** the pin point in `c_luigi`'s bind-pose units (feet at y 0,
  cap top at y 24.3).
- **Pin, on the part:** the same point in the texels of the part it pins to.
  That's the head card `pPlaneShape14` (64x88, x −3.7..4.3, y 9.8..20.8) or
  the body card `pPlaneShape17` (64x72, x −3.7..4.3, y 3.6..12.6), measured
  from that card's top-left. A negative y is above the card's top edge,
  because the cap sits above the head card.

| Card | Size | Pivot | Pin, model (x, y) | Pins to | Pin, on the part | Layer | What it is |
|---|---|---|---|---|---|---|---|
| `veil_side` | 112x152 | (24, 14) | (1.0, 24.3) | head, pPlane14 | (37.6, −28.0) | Behind the cap and head | The tulle train from the back of the crown down over the back and shoulders. For standing and walking. |
| `veil_side_flutter` | 112x152 | (24, 14) | (1.0, 24.3) | head, pPlane14 | (37.6, −28.0) | Behind the cap and head | Blown back, for running, jumping and falling. Swap it with `veil_side`. |
| `veil_band` | 80x40 | (40, 34) | (0.6, 22.2) | head, pPlane14 | (34.4, −11.2) | In front of the cap | Three flowers with pink hearts, two leaves and a pearl band where the veil meets the cap |
| `veil_back` | 120x152 | (60, 14) | (0.0, 24.3) | head, pPlane14 | (29.6, −28.0) | In front of everything | The veil from behind. Replaces `veil_side` and `veil_band`. |
| `gown_side` | 160x80 | (76, 13) | (0.5, 10.0) | waist, body pPlane17 | (33.6, 20.8) | In front of the body and legs, behind the front glove | A full wedding skirt from the waist (y 10) to a lace hem at y 1.5, so the shoes show. It's wide enough (x −8.3..10) to cover both legs in a stride. It has two ruffled tiers and a pink sash, with the bow at the back. |
| `gown_side_swish` | 160x80 | (76, 13) | (0.5, 10.0) | waist, body pPlane17 | (33.6, 20.8) | The same | The same skirt with the hem swung back (+x), for walking and running |

**For the faint** (ch2: "falls flat, the veil settling over his face"), lay
`veil_back` over the head card as he lands. Its lace edge reads as the veil
falling over him.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | 112x152, 112x152, 80x40, 120x152, 160x80, 160x80 |
| R2: 8 texels/unit | **pass** | Sized against `c_luigi`: train 14x19 units, band 10x5, skirt 20x10. |
| R3: 1-bit alpha | **pass** | Every card has 2 alpha levels and 0 partial texels. Tulle is drawn opaque, as the game would. |
| R4: outline 4 to 5 px median (3 to 9) | **pass** | Veil: median 5, p10–p90 3–7. Gown: median 4, p10–p90 2–7. Band: median 4, with small pieces lined at 2 to 3 px. |
| R4: heavier bottom/right | **pass** | Veil: L4 T4, R5 B6. Gown: L4 T3, R5 B4. Band: L3 T3, R4 B5. |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Tulle and gown `#3a3646`, a cool grey like Toad's `#292831`, lum p90 58. Band lum p90 49: petals `#45262f`, pearls `#352a24`, leaves `#1d3812`. Sash `#4a1a2c`. |
| R5: whites s 0.02–0.25 (neutral material) | **pass** | Tulle `#ebe9f2` s .04 v .95. Gown `#efedf3` s .02 v .95. Sash `#f2a9c4` s .30 v .95. Hearts `#ef8fb2`. Leaves `#5daa48`. |
| R6: soft shading, lit from the top left | **pass** | Shadow `#c3bfd6` / `#c9c4d8`, light white, 3 px blur. Folds are shadow-tone strokes `#cdc9de`, 3.5 to 4 px wide, not lines. |
| R6b: sheen 7–25%, upper half; near-white at 0.6–0.7 | **pass** | `veil_side` 12.3% at (0.53, 0.20). `veil_side_flutter` 13.1% at (0.51, 0.22). `veil_back` 7.4% at (0.41, 0.18), all at 0.70 white. Gown 9.4% (side) and 10.8% (swish), at y 0.35 to 0.36, white 0.74 to 0.75. |
| R7: eyes | n/a | |
| R8: separate cards, overlap | **pass** | The train goes behind the head, and the band in front, overlapping the cap's crown by about 1 unit. The skirt covers the body from y 10 down. |

## Rough edges

- The pins are checked against `c_luigi`'s bind pose only, in a scratch
  composite of his real textures. They haven't been checked in his
  animations.
- The flutter is one swap card, not cloth. If it needs more movement, the
  rig can bend its pivot a few degrees.
- In battle, the plan allows the veil alone if the gown is too costly. The
  cards work either way.
