# Look's glowing edge outlines (Super Luigi, chapter 5)

The look of the outlines that Screamy's **Look** reveals in the Room of
Nothing (tik_52), where walls, floor and ceiling are all one pure white
(`docs/super_luigi/ch5.md`, New assets 6: "thin glowing strip props, straight
segments in a few lengths, to lay along the room's edges"; the reveal group:
"thin blue-white strips ... visible for 8 seconds after a Look, then fade").

- **Sources:** made by [`../tools/look_edges.py`](../tools/look_edges.py),
  which also writes `preview.png` (outlines laid on white).
- **Why blue:** a white glow vanishes on the room's white, so each strip is a
  white-hot core inside a saturated blue halo (`#4682f0`). It reads on white
  and on dark.
- **Effect textures:** like the game's effects (STYLE.md R10: IA4, soft
  alpha), these keep a soft alpha ramp; they're not 1-bit cards.

## Textures

| File | Size | Use |
|---|---|---|
| `edge_glow` | 128x16 | The strip. It tiles along its length (u), so one texture covers segments of any length: stretch or repeat the UVs to the segment. The core (about 3 texels) runs down the middle row; the halo fades out over 7 to 8 texels each side. |
| `edge_glow_pulse` | 128x16 | The same, brighter and about a third wider, to swap in for a few frames as the Look lands, then back to `edge_glow` before the 8-second fade |
| `edge_glow_end` | 16x16 | A round cap for a segment's ends and corners, so the joins glow without a seam |

**Lengths:** the plan asks for straight segments in a few lengths; the
models agent can cut them at, say, 25, 50, 100 and 200 units, all mapping
this one strip. **The fade:** drive the props' alpha from 1 to 0 over the
last second of the 8.

## Rough edges

- Seen only in the flat preview, not lit in the room.
