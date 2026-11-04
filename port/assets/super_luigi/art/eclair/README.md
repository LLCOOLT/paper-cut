# Princess Eclair (`c_sl_eclair`)

Princess Eclair is the Waffle Kingdom's princess, and the voice that calls
to Luigi from chapter 2 on. In chapter 6 she reveals the truth about King
Marron, and her tiara holds the seventh piece.

- **Card list:** the models agent's, plus `tiara_glow` for chapter 6's
  "raise the tiara".
- **Scale:** 8 texels per unit, facing -x, feet at y 0. About 25 units to
  the top of the tiara.
- **Sources:** made by [`../tools/eclair.py`](../tools/eclair.py). It also
  writes `parts.json` (sizes, pivots, rest placement and draw order) and
  `preview.png`.

## Look

She follows `docs/super_luigi/ch6.md`: a cream-and-gold gown (`#f4e8c8`,
gold `#e6b63e`), golden hair (`#f2c84a`), and a tiara with a curved golden
plate set in it.

- **Waffle Kingdom touches:**
  - a waffle-grid panel down the front of the skirt
  - a whipped-cream ruffle hem
  - a chocolate-glaze sash, an éclair's icing, with its bow at the back
- **Her face:** a big blue eye with lashes, a rosy cheek, and a small smile.
  She's brave and kind, and shouldn't look like Peach's twin, so the waffle
  gown and golden fringe set her apart.

## Cards

The pivot is in pixels from the PNG's top-left corner. "At rest" is where
that pivot sits, in model units.

| Card | Size | Pivot | At rest | What it is |
|---|---|---|---|---|
| `head` | 64x64 | (36, 56) | (0, 16) | The face and fringe, with the hair over the back of the head. The pivot is at the neck. |
| `head_blink` | 64x64 | (36, 56) | (0, 16) | Eye shut, a lashed arc |
| `head_talk` | 64x64 | (36, 56) | (0, 16) | Mouth open |
| `hair` | 72x128 | (28, 16) | (0.5, 21) | Long hair down her back, behind the head and dress. The pivot is at the crown. |
| `tiara` | 56x40 | (28, 30.4) | (−0.5, 22.2) | A gold band with two pearls and the **seventh piece**: an upright crescent of gold plate |
| `tiara_glow` | 56x40 | (28, 30.4) | (−0.5, 22.2) | The plate blazing in a burst of light, for raising the tiara. Swap it with `tiara`. |
| `dress` | 120x128 | (60, 124) | (0, 1) | The body: the bell skirt, the bodice with a gold neckline and star, and the sash and bow. It's the root card. |
| `arm_front` | 48x72 | (34.4, 9.6) | (−1.2, 14.8) | A cream puff sleeve with gold trim, and a long white glove. The pivot is at the shoulder. |
| `arm_back` | 48x72 | (33.6, 9.6) | (2.2, 14.8) | The same, a tone darker |
| `feet` | 56x16 | (28, 9.6) | (−0.5, 0.8) | Two gold slippers peeking from under the hem |

**Draw order, back to front:** `hair`, `arm_back`, `feet`, `dress`,
`arm_front`, `head`, `tiara`.

For "arms out" and the embrace, swing the arms from their shoulder pivots.
To raise the tiara, lift `tiara_glow` off the head with the front glove.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All 10 cards |
| R2: 8 texels/unit | **pass** | 25 units tall |
| R3: 1-bit alpha | **pass** | Every card has 2 alpha levels and 0 partial texels. |
| R4: outline 4 to 5 px median (3 to 9), heavier bottom/right | **pass** | Head: median 6, L5 T5, R6 B6. Dress: median 5. Arms: median 6, L4, R7 B7. Tiara: median 4, a small part. |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Gown `#4a3818` (58), hair `#462a04`, gold `#4a3006` (51), skin `#5a2a1a`, glove `#3a3640`. The glow's line is darkened to `#4e2e04`. |
| R5: fills s 0.35–0.85, v 0.65–0.95; whites and cloth s ≤ 0.25 | **pass** | Hair `#f2c84a`: s .69, v .95. Gold `#e6b63e`: s .73, v .90. Waffle `#e2ac52`: s .64, v .89. Gown `#f4e8c8`: s .18 (a white cloth). Skin `#fcd8b8`: v .99, a light skin like Peach's. |
| R6: soft shading, lit from the top left | **pass** | Bands with a 3 px blur. Face shading is kept narrow (offsets of 4 to 5 px). |
| R6b: sheen 7–25%, upper half; near-white at 0.6–0.7 | **pass** | Hair 15.1% at (0.38, 0.21). Arms 7.2–7.9%. Dress 18.9%. On the cream gown the white strength is 0.65. |
| R7: eyes baked into the head | **pass** | A tall white eye with a thin line, a blue iris `#3a72d0` and a dark pupil, set toward the front, with a white dot and lash flicks. Blink is a lashed arc. |
| R8: separate cards, overlap | **pass** | The puff sleeves cover the shoulder joints, the hem covers the slippers, the fringe and back hair cover the head's edge, and the tiara overlaps the crown. |

## Rough edges

- The heads' sheen reads 18 to 20% with its centroid at y 0.52. The finder
  counts the light skin's light band along with the sheen.
