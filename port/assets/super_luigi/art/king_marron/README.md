# King Marron restored (`c_sl_marron`), chapter 6

King Marron of the Chestnut Marches, Princess Eclair's betrothed: the
Chestnut King with Minister Crepe's curse broken. He's drawn to
`docs/super_luigi/ch6.md` (Characters, and New assets 2, optional): "a tall,
kind-faced king with a chestnut-brown cape and a crown of leaves", for the
scene where the husk falls away, the embrace with Eclair, and the goodbye
on the cliff path. Animations the plan asks for: stand, talk, hold (the
embrace) and wave.

- **Scale:** 8 texels per unit, facing -x, feet at y 0. About 31 units to the
  top of the crown: a head taller than Eclair (25), and a little over
  Luigi's 24.
- **Sources:** made by
  [`../tools/king_marron.py`](../tools/king_marron.py). It also writes
  `parts.json` (sizes, pivots, rest placement and draw order) and
  `preview.png` (every expression, and beside Eclair at the same scale).

## Look

He steps out of the Chestnut King's husk, so he keeps its colours:

- **Chestnut-brown hair** (`#9a4a22`, the King's head colour) swept over the
  forehead, and a neat chestnut moustache.
- **A kind face:** Eclair's face construction (a tall white eye with a thin
  line, set toward the front), with a warm brown iris, a soft brow raised a
  little in the middle, and a small smile.
- **A crown of chestnut leaves:** five long serrated leaves fanned up from a
  gold band, with one chestnut at the front.
- **A chestnut-brown cape** (`#a4582c`), its lining the nut's pale cream
  (`#efe0bc`), turned at the front edge and rolled as a collar.
- **A coat in chestnut-leaf green** (`#5ca844`) with a gold placket and hem,
  three chestnut buttons and a brown belt with a gold buckle; brown riding
  boots.

## Parts list (for the models agent)

The pivot is in pixels from the PNG's top-left corner. "At rest" is where
that pivot sits, in model units.

| Card | Size | Pivot | At rest | What it is |
|---|---|---|---|---|
| `head` | 80x72 | (48, 64) | (0, 20) | The face, moustache and hair. The pivot is at the neck. |
| `head_blink` | 80x72 | (48, 64) | (0, 20) | Eye shut (an arc) |
| `head_talk` | 80x72 | (48, 64) | (0, 20) | Mouth open under the moustache |
| `head_happy` | 80x72 | (48, 64) | (0, 20) | Eye squeezed up in a smile, a wider smile: for the embrace and the goodbye |
| `crown` | 72x48 | (36, 41.6) | (−0.5, 25.8) | The crown of chestnut leaves. The pivot is at its foot on the head; it rides the head. |
| `cape` | 104x152 | (36.8, 11.2) | (0.6, 20.6) | Behind everything: the cape and its cream collar. The pivot is at the shoulders; sway it from there. |
| `coat` | 96x144 | (48, 120) | (0, 6) | The body, from the shoulders to the coat's hem at the knee. It's the root card. |
| `arm_front` | 56x96 | (40, 14.4) | (−1, 19.2) | A green sleeve with a gold cuff and a bare hand. The pivot is at the shoulder. |
| `arm_back` | 56x96 | (40, 14.4) | (2.4, 19.2) | The same, a tone darker |
| `boots` | 80x56 | (48, 56) | (0, 0) | Two riding boots, the far one a tone darker. The pivot is on the ground between them. |

**Draw order, back to front:** `cape`, `arm_back`, `boots`, `coat`,
`arm_front`, `head`, `crown`.

**Poses from these cards:**

- **Stand / talk:** at rest; swap `head` for `head_talk` and `head_blink`.
- **Wave:** raise `arm_front` from its shoulder pivot (about −150°, the hand
  above the crown) and rock it ±15°.
- **Hold (the embrace with Eclair):** both arms forward and in (about −60°),
  `head_happy`, and the head tipped down 5 to 10°. Eclair stands in front
  of him (to -x) with her own arms out.
- **The curse breaking:** the Chestnut King's husk shards fly out
  (`../chestnut_king/`), and this model takes the King's place where he
  stood. If this model isn't built, the plan's fallback is the King's own
  `head_calm` with the husk hidden.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All 10 cards |
| R2: 8 texels/unit | **pass** | 31 units tall |
| R3: 1-bit alpha | **pass** | 2 alpha levels, 0 partial texels on every card |
| R4: outline 4 to 5 px median (3 to 9), heavier bottom/right | **pass** | Coat, cape, arms median 5 (L4 against R6 to R7); head 6; boots 4 |
| R4: line colour, dark and in the family, lum ≤ 60 | **pass** | Coat `#122a0a` (32), hair `#2a0e04` (55 on the head), cape `#2a0e02`, boots `#1a0a02` (15), leaves `#18320a` |
| R5: fills s 0.35–0.85, v 0.65–0.95 | **pass**, one darker accent | Coat `#5ca844` s .60 v .66; cape `#a4582c` s .73 v .64; skin `#f8d0a8` v .97 (a light skin, as Eclair's); lining `#efe0bc` s .21 (a cloth). The boots, `#5e3618`, are leather and kept dark, as Luigi's own shoes are. |
| R6: soft shading, lit from the top left | **pass** | Bands with a 3 px blur |
| R6b: one sheen per rounded part, upper half | **pass** | Coat 12.4% at (0.37, 0.29), white 0.26; cape 10.9% at (0.39, 0.25); sleeves 5.2%. On the head the finder takes the face's light band with the sheen (32%, centroid y 0.58), as the light skin is close to its own sheen. |
| R7: eyes baked into the head | **pass** | As Eclair's, without lashes; blink is an arc, happy an upturned arc |
| R8: separate cards, overlap | **pass** | The sleeves cover the shoulders, the coat's hem covers the boots' tops, the hair covers the head's edge and the crown overlaps the hair |

## Rough edges

- Not built or seen in the game. The poses above are suggestions from the
  plan's list; the models agent picks the angles.
- At game distance the leaf crown reads as a green spray; the chestnut at
  its front is what says "chestnut".
