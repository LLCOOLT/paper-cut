# Chapter 5's props: Mossbeard's button, the Luff gear-gate and the Luff signpost

From `docs/super_luigi/ch5.md`, New assets 8 and 9: "The Luff gear-gate
(prop: a gate of stone teeth with a big wall gear) and the Luff signpost (a
stone post with a crank), optional", and "Mossbeard's buttons (an icon: a
big button)". The puzzles they serve: the gear is "jammed with a thousand
years of grit" until Torque's Tune-up, then each push raises the gate a
notch (it falls back if Luigi stops); the broken signpost, once fixed,
"turns and points at a stump".

- **Scale:** props at 8 texels per unit, outlined, lit from the top left,
  in the Luffs' white stone with the teal glyph inlay of the murals
  ([`../murals/`](../murals/)). The button is a 40x40 item icon, the size of
  `icon.tpl`'s item icons.
- **Sources:** made by [`../tools/ch5_props.py`](../tools/ch5_props.py),
  which also writes `parts.json` and `preview.png`.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `gate_frame` | 144x160 | (72, 160) | The gate's stone arch, 18 by 20 units, with an opening 10.8 wide and 16 tall, voussoir joints and the Luffs' compass mark on the keystone. In front of the teeth. Pivot on the ground at its middle. |
| `gate_teeth` | 96x136 | (48, 136) | The gate itself: a stone slab ending in a row of six fangs. Behind the arch. At rest it's down (shut); raise it about 3 units a notch, four notches to open. Set the arch against a wall so the raised slab goes up behind it. |
| `gear` | 72x72 | (36, 36) | The big wall gear, clean: ten teeth, four spoke holes and a dashed glyph ring. Turn it about its hub as the gate rises. Set it on the wall beside the arch (the preview has it 11 units right, 8 up). |
| `gear_jammed` | 72x72 | (36, 36) | The same packed with grit, before the Tune-up. Swap it for `gear` when Torque is done. |
| `signpost` | 32x104 | (16, 104) | The Luff signpost's stone post, 13 units tall, with a teal-ringed axle socket at (0, 10.4) |
| `signpost_pointer` | 80x24 | (8, 12) | The pointer, a stone arrow with glyphs, pivoting on its axle. Broken, it hangs (about −100°); fixed, it swings round to point at the stump. |
| `signpost_crank` | 32x32 | (8, 24) | The crank: a wooden handle on the post's side, in front at the axle. Turn it about its hub while Torque fixes the post. |
| `icon_button` | 40x40 | | Mossbeard's Button (key item): a big red sewing button with a raised rim, four holes and a scrap of thread |

**Draw order:** the gate: `gate_teeth`, `gate_frame`, then the gear on the
wall. The post: `signpost`, `signpost_pointer`, `signpost_crank`.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All eight |
| R2: 8 texels/unit | **pass** | |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels on every card |
| R4: dark outline in the family | **pass** | Stone `#3a3226` and `#2e281c`, grit `#2a1a08`, button `#3e0a08` |
| R5: fills | **pass** | Stone `#e2dcce` s .09 v .89 (a neutral, warm), gear stone `#c8c0ae`, button `#d8483a` s .73 v .85 |

## Rough edges

- Not seen in the game. The plan says an existing gate prop will do; these
  are for when the real thing is wanted.
