# The Nightmare (`c_sl_nightmare`)

Minister Crepe unfolded: chapter 6's last battle form (`docs/super_luigi/ch6.md`,
New assets 5: "a tall shadow (deep violet-black paper with gold edges), a
round compass for a face whose needle spins and stops, two long arms made of
folded pages. Battle only"). Crepe's own cards are in [`../crepe/`](../crepe/).

- **Scale:** a giant boss, so **4 texels per unit** (STYLE.md R2), mostly
  flat with up to three hard tones (R6), lines 3 to 5 px (R4). About 56 units
  tall to the top of the face (Luigi is 24). Facing -x, hem at y 0.
- **Sources:** made by [`../tools/nightmare.py`](../tools/nightmare.py),
  which also writes `parts.json` and `preview.png`.

## Look

A tall cloak of violet-black paper (`#3a2650`) with a gilt edge, long hard
creases and a torn hem; a round compass face in a gold rim, its dial ticked
like a clock, with a red-and-dark needle; two arms that are accordions of
folded cream pages, ending in flat page-hands with lines of writing on them.

## Cards

Pivot: pixels from the PNG's top-left. At rest: model units.

| Card | Size | Pivot | At rest | What it is / serves |
|---|---|---|---|---|
| `body` | 128x192 | (64, 188) | (0, 0) | The shadow cloak. Root card. |
| `body_cracked` | 128x192 | (64, 188) | (0, 0) | Lines of light breaking through: the **crack and shatter** |
| `face` | 80x80 | (40, 40) | (0, 47) | The compass face, closed (idle, Doom Foretold) |
| `face_open` | 80x80 | (40, 40) | (0, 47) | **Hatesong**: the dial split into a dark howling mouth with gold teeth. Hide the needle. |
| `face_cracked` | 80x80 | (40, 40) | (0, 47) | The face cracking with light |
| `needle` | 16x56 | (8, 28) | (0, 47) | The needle (red north). **Idle:** spin it. **Doom Foretold:** stop it pointing at the target. |
| `arm_front` | 128x128 | (120, 16) | (−3, 37) | The page-arm, from the shoulder. **Page-slaps** and **Page Grab** swing it from the shoulder. |
| `arm_back` | 128x128 | (120, 16) | (3, 38) | The same, a tone darker, behind the body |
| `arm_stub` | 40x40 | (32, 16) | (−3, 37) | **Arm lost:** a torn stump of pages; swap for either arm until it regrows (3 turns) |
| `light_shard_0`..`2` | 24x24 | (12, 12) | | Shards of light for the **shatter**: throw out and fade |

**Draw order, back to front:** `arm_back`, `body`, `arm_front`, `face`,
`needle`. **Hurt:** a flash and a shake; no extra card.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 16 to 192 px |
| R2: 4 texels/unit (giant boss) | **pass** | |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 3 to 5 px at 4/unit | **pass** | Body 3 (p10–p90 2–8; T9 is the face's shoulder notch), face 4, arms 4 (L3 T3, R5 B5 to B6) |
| R4: line colour, lum ≤ 60 | **pass** | Body `#0e0614` (lum p90 11), pages `#2a1a10` (31), gold rim `#3a2606` (41) |
| R5: fills | **pass** | The shadow is a dark marking-range fill by design (v .31). Pages `#e8dcc0` s .17 v .91, the back arm v .81. Gold `#e2b444` s .70 v .89. |
| R6: flat, up to three hard tones | **pass** | Base, light and shadow folds as hard shapes, no blur |

## Rough edges

- The arms are single cards; the page-slaps swing them whole. A two-part
  arm (upper leaves and the page-hand) would let the hand flap, if the
  models agent wants it.
