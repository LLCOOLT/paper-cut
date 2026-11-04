# The Chestnut King (`c_sl_chestnut_king`), chapter 6's first boss

The Chestnut King is King Marron of the Chestnut Marches under Minister
Crepe's curse. He's drawn to `docs/super_luigi/ch6.md`, New assets 1:

- a huge round body of spiky brown husk (a chestnut burr)
- a smaller round head on top, with a mouth of fangs and two small, sad eyes
- short, thick legs
- green goo dripping from the jaws

The husk splits open down the front and closes again, and when the curse
breaks it falls away in pieces. He's regal, with a crown and a cape. He's
also Goomba-like: Goombas are chestnuts (*kuribo*), so his head is a glossy
chestnut with a pale base, shaped like a Goomba's.

- **Scale:** 8 texels per unit, facing -x, feet at y 0. About 49 units tall
  to the crown's top, twice Luigi's height.
- **Sources:** made by
  [`../tools/chestnut_king.py`](../tools/chestnut_king.py). It also writes
  `parts.json` (sizes, pivots, rest placement and draw order) and
  `preview.png`.

## Cards

The pivot is in pixels from the PNG's top-left corner. "At rest" is where
that pivot sits, in model units.

| Card | Size | Pivot | At rest | What it is |
|---|---|---|---|---|
| `husk_left` | 152x288 | (132, 260) | (−2, 5.5) | The burr's front half on the -x side, spiky, with short spines all over. To **split open**, rotate it about its pivot, near the bottom of the seam. |
| `husk_right` | 152x288 | (20, 260) | (2, 5.5) | The other half. The two meet at x 0 along a jagged seam. |
| `nut` | 200x232 | (100, 224) | (0, 6) | The chestnut body inside, glossy red-brown with its pale, speckled base. It shows when the husk splits, and stays when the husk falls away. |
| `head` | 160x128 | (88, 116) | (0, 31.5) | Idle: small sad eyes with brows slanting up, upper and lower fangs, and goo at the jaw's corner. The pivot is at the neck. |
| `head_roar` | 160x128 | (88, 116) | (0, 31.5) | Mouth wide, brows furious |
| `head_hurt` | 160x128 | (88, 116) | (0, 31.5) | Eyes squeezed shut (`> <`) |
| `head_sad` | 160x128 | (88, 116) | (0, 31.5) | A tear, for hesitating and kneeling |
| `head_calm` | 160x128 | (88, 116) | (0, 31.5) | Calm, with no fangs or drool. This is ch6's fallback for King Marron restored ("the King's model with a calm face"). |
| `crown` | 96x56 | (48, 49.6) | (0, 42.8) | A small gold crown with pearls and three rubies, sitting on top of the head |
| `cape` | 224x288 | (56, 32) | (1, 35) | Behind everything: a crimson cape with an ermine collar and hem. The pivot is at the shoulders. |
| `leg_front` | 64x64 | (33.6, 9.6) | (−4.8, 6.8) | A short, thick leg with a broad foot. The pivot is at the hip, under the husk. |
| `leg_back` | 64x64 | (25.6, 9.6) | (4.2, 6.8) | The same, a tone darker |
| `goo_drip_1`..`3` | 32x56 | (16, 8) | at the jaw's corner: head (−9.8, 34.4) | The drip loop: a short drip, a long one, then the drop about to fall |
| `husk_shard_1`..`4` | 96x96 | (48, 48) | | Pieces of husk falling away as the curse breaks. Scatter them from where the husk was. |
| `chestnut` | 40x40 | (20, 20) | | A falling chestnut, for Chestnut Rain |

**Draw order, back to front:** `cape`, `leg_back`, `nut`, `husk_right`,
`husk_left`, `leg_front`, `head`, `crown`. The goo drips go in front of the
head.

Some of ch6's poses come from these cards:

- **Skull low:** tip the head forward about its neck pivot.
- **Curse breaking:** hide the husk halves and fly the shards out, then
  swap in `head_calm`.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All 20 cards |
| R2: 8 texels/unit | **pass** | 49 units tall: a big boss at character density, like Hizza |
| R3: 1-bit alpha | **pass** | Every card has 2 alpha levels and 0 partial texels. |
| R4: outline 4 to 5 px median (3 to 9), heavier bottom/right | **pass** | Husk: median 5 to 6, L4–5 T5, R6 B6–7. Head: median 6, L5 T5, R6 B6. Nut: median 6. Legs and crown: median 5. Drips: 2.5 to 5, thin strands. |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Husk `#24140a` (25), nut `#2a0e04` (22), cape `#2a0610` (55), gold `#4a3006` (51), goo `#163808` (41). |
| R5: fills s 0.35–0.85, v 0.65–0.95 | **pass** | Husk `#a8743c`: s .64, v .66. Nut `#a64c22`: s .80, v .65. Face base `#e6c48e`: s .38, v .90. Cape `#a82a3c`: s .75, v .66. Goo `#7ccf3c`: s .71, v .81. The legs (v .58) are a darker accent under the body. |
| R6: soft shading, lit from the top left | **pass** | Bands with a 3 px blur. Spines are flat strokes, not texture noise. |
| R6b: sheen 7–25%, upper half | **pass** (see note) | Husk halves 8.8% and 9.0% at y 0.23 to 0.26, white 0.18 to 0.22 (matte). Nut 12.8%. Cape 8.2%. Legs 13 to 14%. Shards 11.5 to 12%. |
| R7: eyes baked into the head | **pass** | Small pale eyes with a thin line and a dark pupil, set toward the front, with a white dot. The brows carry the expression. Hurt is `> <`. |
| R8: separate cards, overlap | **pass** | The husk halves overlap the nut's edges, the head sits over the husk's top, and the crown overlaps the dome. |

**Note on the heads:** the sheen finder reads 37 to 40%. The pale face base
(hue 37) is within its hue window of the nut's red-brown, so it's counted as
sheen. The sheen itself is a 0.26x0.12 oval on the dome.

## Rough edges

- King Marron restored (ch6, optional: "tall, gentle, leaf crown") is its
  own model, in the chapter 6 queue. `head_calm` is the fallback ch6 names.
