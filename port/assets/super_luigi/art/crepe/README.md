# Minister Crepe (`c_sl_crepe`)

Minister Crepe is the Waffle Kingdom's portly, scheming minister. He sends
Luigi the letter and the compass base in chapter 0, and he's the villain of
chapter 6.

- **Card list:** the models agent's, plus `head_gold` for chapter 6's "look
  into the compass (eyes gold)".
- **Scale:** 8 texels per unit, facing -x, feet at y 0. About 27 units to
  the top of the hat; Luigi is 24.
- **Sources:** made by [`../tools/crepe.py`](../tools/crepe.py). It also
  writes `parts.json` (sizes, pivots, rest placement and draw order) and
  `preview.png`.

## Look

- **Crepe motifs:** the cape is a golden crepe (`#e8b860`) with browned
  spots and a scalloped, folded edge. A folded-crepe ruff sits under his
  chin, and a crepe band goes round his hat.
- **Dessert touches:** strawberry-red trim (`#d8384a`), and a strawberry
  jewel on the hat.
- **His robe:** cream (`#efe3c4`) with a gold sash and gold buttons.
- **His hat:** a tall plum minister's hat (`#6a2c5c`).
- **His face:** a big round nose, a dark curled moustache swept forward and
  up, an arched brow, and a heavy, sly lid over a small dark eye.

The Nightmare form is its own model (`c_sl_nightmare`, chapter 6) and comes
later in the queue.

## Cards

The pivot is in pixels from the PNG's top-left corner. "At rest" is where
that pivot sits, in model units.

| Card | Size | Pivot | At rest | What it is |
|---|---|---|---|---|
| `head` | 72x72 | (48, 64) | (0, 13) | The head, with the eye open. The pivot is at the neck, and the ruff hides it. |
| `head_blink` | 72x72 | (48, 64) | (0, 13) | Eye shut |
| `head_talk` | 72x72 | (48, 64) | (0, 13) | Mouth open, behind the end of the moustache |
| `head_gold` | 72x72 | (48, 64) | (0, 13) | Eyes gone gold (chapter 6) |
| `mustache` | 48x24 | (28.8, 8.8) | (−4.4, 14.9) | The curled moustache. The pivot is under the nose, for the talk wiggle. |
| `hat` | 64x80 | (32, 73.6) | (−0.5, 19.8) | The tall hat. The pivot is at the brow. |
| `robe` | 112x112 | (64, 108) | (0, 1.5) | The body: the portly robe and ruff. It's the root card. |
| `cape` | 88x120 | (24, 12) | (1, 14.5) | Behind the robe. The pivot is at the neck. |
| `arm_front` | 48x80 | (35.2, 8) | (−2.6, 11.8) | A wide cream sleeve, red cuff and white glove. The pivot is at the shoulder. |
| `arm_back` | 48x80 | (30.4, 8) | (3.8, 11.8) | The same, a tone darker |
| `foot_front` | 40x24 | (30.4, 9.6) | (−2.2, 1.8) | A plum court shoe with a curled toe. The pivot is at the ankle. |
| `foot_back` | 40x24 | (27.2, 9.6) | (2.4, 1.8) | The same, a tone darker |
| `scroll` | 40x56 | (20, 28.8) | in the front glove | The letter, a scroll with a red wax seal |
| `compass_base` | 56x56 | (28, 28) | in the front glove | The Marvelous Compass's base: a gold ring of seven sockets, one section intact (a blue jewelled wedge) |

**Draw order, back to front:** `cape`, `arm_back`, `foot_back`, `robe`,
`foot_front`, `arm_front`, `head`, `mustache`, `hat`. The props go in front
of the front glove.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All 14 cards |
| R2: 8 texels/unit | **pass** | 27 units tall, against Luigi's 24 |
| R3: 1-bit alpha | **pass** | Every card has 2 alpha levels and 0 partial texels. |
| R4: outline 4 to 5 px median (3 to 9), heavier bottom/right | **pass** | Head: median 6, L5 T4, R7 B6. Robe: 5.5. Cape: 5, L4 T4, R6 B8. Arms: 5 to 6. Small parts (moustache, scroll, compass): 4. |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Skin `#4a2414` (55), robe `#3e2e16` (49 to 50), crepe `#3e2408` (41), plum `#1e0818` (17 to 40), gold `#4a3006` (51), moustache `#1c0e06` (18). |
| R5: fills s 0.35–0.85, v 0.65–0.95; neutrals and whites s ≤ 0.25 | **pass** | Skin `#f3c79c`: s .36, v .95. Crepe `#e8b860`: s .59, v .91. Trim `#d8384a`: s .74, v .85. Robe cream `#efe3c4`: s .18 (a white cloth). Plum `#6a2c5c` (v .42) and moustache (v .37) are dark accents. The back arm and foot are a tone darker. |
| R6: soft shading, lit from the top left | **pass** | Bands with a 3 px blur. Folds are shadow-tone strokes. |
| R6b: sheen 7–25%, upper half | **pass** (see note) | Cape 13.2% at (0.32, 0.22). Hat 10.5% at (0.36, 0.24). Heads 6.6–8.2% at y 0.20–0.22, white 0.41. Arm 17.4%. |
| R7: eyes baked into the head | **pass** | A pale almond eye under a heavy lid line. The pupil is small and dark, set toward the front, with a white dot. Blink is an arc 3.2 px wide. Gold is an amber iris `#f6c83a`. |
| R8: separate cards, overlap | **pass** | The ruff covers the neck joint, the cuffs and gloves overlap the robe, and the hat brim overlaps the head. |

**Note on `robe`:** the sheen finder reads 31%. The robe's wide shadow band
pulls its median colour down, so the plain cream counts as lighter. The
sheen itself is a 0.2x0.1 oval, like the others.

## Rough edges

- No chapter 0 plan was available, so his look follows the books (a scheming
  minister) and the motifs asked for.
