# Fuselle (`c_sl_fuselle`)

Fuselle is Plumpbelly's pink Bob-omb (chapter 2, `docs/super_luigi/ch2.md`,
New assets 5: "a Bob-omb with a pink body tint, a bow on the fuse,
eyelashes"). The plan allows a texture job on the game's Bob-omb model; these
cards are a full set in the same construction (round body, face layer, fuse
cap, wind-up key, two feet), so the models agent can rig her either way and
nothing is taken from the disc.

- **Scale:** 8 texels per unit, facing -x, feet at y 0. About 21 units to
  the bow's top (Luigi is 24).
- **Sources:** made by [`../tools/fuselle.py`](../tools/fuselle.py), which
  also writes `parts.json` and `preview.png`.

## Look

- A pink bomb (`#ea8cb8`) with a dark berry line (`#4c1430`), a soft seam
  round the middle, one sheen top left.
- Big white eyes with low dark pupils, a white dot, and three swept-back
  lashes on each.
- A grey metal fuse cap; the wick carries a red bow (`#d8304e`).
- A brass wind-up key on the back and round orange shoes, like every
  Bob-omb.

## Cards

Pivot: pixels from the PNG's top-left. At rest: where the pivot sits, in
model units.

| Card | Size | Pivot | At rest | What it is |
|---|---|---|---|---|
| `body` | 144x144 | (72, 136) | (0, 2) | The round body. Root card. |
| `face` | 64x80 | (64, 80) | (0, 6) | Face layer: both eyes, open, with lashes |
| `face_blink` | 64x80 | (64, 80) | (0, 6) | Eyes shut, lashes down |
| `face_hurt` | 64x80 | (64, 80) | (0, 6) | Squeezed shut, `> <` |
| `face_talk` | 64x80 | (64, 80) | (0, 6) | Open, with a blush under the eyes (her shy lines) |
| `fuse` | 64x72 | (32, 64) | (0, 16) | Metal cap, wick and bow. The pivot is the cap's foot, sunk into the body's top; sway it from there. |
| `spark` | 32x32 | (16, 16) | (0.75, 22.5) | The lit fuse's spark, at the wick's tip. Flicker by scale and spin. |
| `key` | 56x72 | (4.8, 35.2) | (6.6, 10.6) | The wind-up key. Spin it round x (or swap a mirrored copy) on its shaft root. |
| `foot_front` | 48x24 | (24, 9.6) | (−1.8, 1.4) | Round orange shoe |
| `foot_back` | 48x24 | (24, 9.6) | (1.8, 1.4) | The same, a tone darker |

**Draw order, back to front:** `foot_back`, `key`, `fuse`, `body`, `face`,
`foot_front`, then `spark` when lit.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 24 to 144 px |
| R2: 8 texels/unit | **pass** | Body 18 units across |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels on every card |
| R4: outline 4–5 px, 3–9 | **pass** | Body median 6 (p10–p90 4–8), fuse, key and feet 5. Spark 7 (a small burst, its line is its shape). |
| R4: heavier bottom/right | **pass** | Body L5 T5, R6 B6. Key L4 T4, R6 B7. Fuse L4 T5, R6 B6. |
| R4: line colour in the family, lum ≤ 60 | **pass** | Body `#4c1430` (lum p90 41), bow `#4a0816`, metal `#262834` (41), key `#4a3006` (51), shoes `#4e2c04` (50) |
| R4: thinner, lighter eye line | **pass** | Eyes `#6a2a48`, about 3 px against the body's 6. The lashes lift the face card's lum p90 to 65 to 72, as eye lines may. |
| R5: fills | **pass** | Body s .40 v .92. Bow s .78 v .85. Shoes s .78 v .94, back shoe v .83. Metal `#b4b6c2` s .07 v .76 (neutral, cool). |
| R6: soft top-left shading | **pass** | Light and shadow bands 7 to 9 px with a 3 px blur on the body |
| R6b: one sheen per round part | **pass** | Body 18.1% at (0.37, 0.25), white 0.31. Shoes 7.2–7.5%. Fuse cap 7.4%. Key wings 19.4% (brass, glossy; the finder's base there is the dark key hole). |
| R7: baked eyes | **pass** | Sclera v .98 with the thin line, a dark vertical pupil (v .1) low and to the front, a white dot top left. Blink and hurt are single strokes 4.5–5 px with round caps. |
| R8: separate moving parts, overlap | **pass** | Fuse, key and feet tuck under the body's line; the face layer swaps whole. |

## Rough edges

- The bow sits well up the wick, so the cap shows; on the real Bob-omb model
  the fuse may be shorter. Scale `fuse` down (it pivots at the cap's foot) if
  it reads tall next to Jerry.
