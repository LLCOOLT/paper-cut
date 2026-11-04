# Cranberry (`c_sl_cranberry`), the last of the Luffs (chapter 5)

Cranberry is a small boy in white robes, with short reddish hair, a
red-berry clasp at his collar and bare feet. He sits cross-legged on the
dais, asleep for a thousand years, until a sneeze wakes him.

- **Brief:** `docs/super_luigi/ch5.md`, New assets 1.
- **His poses there:** sleeping seated, opening his eyes (a blink, then a
  smile), talking seated, fading (done in script, with alpha), and the
  hand-over.
- **The Luffs** were an ancient, white-domed civilisation. Their round
  glyphs mark his robe: rings on the gold trim, and a ringed disc on the
  chest.
- **Scale:** 8 texels per unit, facing -x, seated on y 0 (the dais is part
  of the map). About 15 units to the top of his hair.
- **Sources:** made by [`../tools/cranberry.py`](../tools/cranberry.py). It
  also writes `parts.json` (sizes, pivots, rest placement and draw order)
  and `preview.png`.

## Cards

The pivot is in pixels from the PNG's top-left corner. "At rest" is where
that pivot sits, in model units.

| Card | Size | Pivot | At rest | What it is |
|---|---|---|---|---|
| `body_seated` | 104x72 | (56, 68) | (0, 0.5) | Sitting cross-legged in a white robe with gold glyph trim, bare feet forward, and the red-berry clasp (three berries and a leaf) at the collar. It's the root card. |
| `head_sleep` | 64x64 | (36, 55.2) | (0, 8.6) | Asleep: eyes closed, peaceful. The pivot is at the neck. |
| `head_wake` | 64x64 | (36, 55.2) | (0, 8.6) | Waking: heavy, half-open eyes |
| `head` | 64x64 | (36, 55.2) | (0, 8.6) | Awake: gentle reddish-brown eyes, a small smile |
| `head_smile` | 64x64 | (36, 55.2) | (0, 8.6) | Smiling, eyes closed happily |
| `head_talk` | 64x64 | (36, 55.2) | (0, 8.6) | Talking |
| `arm_front` | 48x64 | (30.4, 11.2) | (−1.2, 7.6) | The front arm, resting on his lap. The pivot is at the shoulder. |
| `arm_back` | 48x64 | (30.4, 11.2) | (1.8, 7.6) | The same, a tone darker |
| `arm_reach` | 72x40 | (62.4, 11.2) | (−1.2, 7.6) | The hand-over: the front arm held out, palm up. Swap it in for `arm_front`. |
| `compass_piece` | 40x40 | (20, 20) | on the palm, about (−7.6, 7.2) | The piece he guarded: a curved golden plate with a pale-blue jewel |

**Draw order, back to front:** `arm_back`, `body_seated`, `arm_front`,
`head`.

The poses come from swapping cards and moving them:

- **Breathing:** scale `body_seated` gently, about 2% in y.
- **Waking:** `head_sleep`, then `head_wake`, then `head` (with a blink),
  then `head_smile`.
- **Fading:** the model's alpha, done in script.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All 10 cards |
| R2: 8 texels/unit | **pass** | A child: 15 units seated, against Luigi's 24 standing |
| R3: 1-bit alpha | **pass** | Every card has 2 alpha levels and 0 partial texels. |
| R4: outline 4 to 5 px median (3 to 9), heavier bottom/right | **pass** | Body: median 5, L5 T4, R5 B6. Heads: median 5 to 6, L4 T4, R6 B6. Arms: 6. |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Robe `#34344a`, a cool grey for a white robe, like Toad's (lum p90 55 to 56). Hair `#3a0c08`, skin `#5a2a1a`, gold `#4a3006` (52). |
| R5: fills s 0.35–0.85, v 0.65–0.95; whites s ≤ 0.25 | **pass** | Robe `#eeeef4`: s .02, v .96 (white cloth). Hair `#c8503a`: s .71, v .78. Trim `#e0b040`: s .71, v .88. Berries `#cc2a40`. Skin `#f7dcc4`, a pale skin. |
| R6: soft shading, lit from the top left | **pass** | Bands with a 3 px blur. Folds are shadow-tone strokes. |
| R6b: sheen 7–25%, upper half; near-white at 0.6–0.7 | **pass** | Robe 8.0% at (0.44, 0.28), white 0.72. Arms 6.7 to 8.9%. |
| R7: eyes baked into the head | **pass** | A white eye with a thin line, a reddish-brown iris (`#b8443a`, his berry colour) and a dark pupil, set toward the front, with a white dot. Asleep and smiling are arcs 2.8 px wide. |
| R8: separate cards, overlap | **pass** | The sleeve covers the shoulder, the lap covers the feet's heels, and the hair covers the back of the head. |

## Rough edges

- The heads' sheen reads 17 to 22%, at y 0.55 to 0.61. On the pale skin,
  the finder counts the light band with the sheen.
- He has no standing card. ch5 needs him seated only.
