# Chapter 4's props: the Fiery Hat and the chandelier

From `docs/super_luigi/ch4.md`, New assets 3 and 4: the Fiery Hat ("a tall
red top hat with paper flames, flickering (two-frame flame loop). Jerry
wears it in the play") and "a chandelier prop that can fall", for the Drama
Slam's stage. The painted backdrops are in [`../ch4_sets/`](../ch4_sets/).

- **Scale:** 8 texels per unit, outlined, lit from the top left.
- **Sources:** made by [`../tools/ch4_props.py`](../tools/ch4_props.py),
  which also writes `parts.json` and `preview.png`.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `fiery_hat` | 96x128 | (48, 120) | A tall red top hat (crown 13 units, brim 11 wide) with a black band and a little flame badge. The pivot is the brim's middle: set it on Jerry's top, in front of his fuse cap (Jerry's body is about 16 units across). |
| `fiery_hat_flames_0` | 88x72 | (40, 64) | Paper flames out of the crown's top, frame 0. The pivot is their foot, at the crown's top: 13 units above the hat's pivot. |
| `fiery_hat_flames_1` | 88x72 | (40, 64) | Frame 1: the tips swung back and one licking higher. Alternate the two every 4 to 6 frames. Draw them behind the hat. |
| `chandelier` | 320x224 | (160, 3.2) | A gold chandelier, 40 by 28 units: a column, two tiers of curved arms, eight candles with flames, and crystal drops. The pivot is the top ring, where the chain hooks on. To fall, drop it from there. |
| `chain` | 24x64 | (12, 64) | Four gold links, 8 units. Stack copies up to the flies. The pivot is the bottom link. |
| `crystal_shard_0`..`2` | 24x24 | (12, 12) | Crystal shards for the crash: hide the drops' sparkle and throw these out spinning |

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 24 to 320 px |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 4–5 px, 3–9 | **pass** | Hat median 4 (L4 T4, R6 B10: the brim's underside). Chandelier 6 (L4, R6 B8). The flames measure 7, because their deep orange shadow band reads as line; the line is 4 to 5. Chain links and shards 2 to 5 (small parts). |
| R4: line colour, lum ≤ 60 | **pass** | Hat `#3e0610` (lum p90 24), gold `#4a3006` (51), flames `#5a1a04` (44), crystal `#1a3450` (47) |
| R5: fills | **pass** | Hat `#d0283a` s .81 v .82. Gold `#e8b440` s .72 v .91. Flames are emissive (v up to 1.0). Crystal `#c8e4f4` s .18 v .96, a cool neutral. |
| R6b: sheen | **pass** | Hat 18.3% at (0.35, 0.33), white 0.24. The chandelier's sheen is on the column (4.3% of the whole card, which is mostly thin arms). Each drop has a white glint (glossy). |

## Rough edges

- The flames are two swap frames, as the plan asks; the game's own fire
  effect could add sparks on top.
