# The goo (Super Luigi, chapter 6)

The Chestnut King's green goo: the smoking pit in the hall and the moat in
jin_70 (`docs/super_luigi/ch6.md`, New assets 7: "a smoking green surface
prop with a bubbling texture ... the same material, a wide plane").

- **Sources:** made by [`../tools/goo.py`](../tools/goo.py), which also
  writes `preview.png`.
- **Style:** the surface is a map texture (STYLE.md R10): a power-of-two
  tile that wraps both ways, opaque, flat tones close in hue, light blobs
  and a dark drop line under the bubbles, no outlines. The bubbles are 1-bit
  outlined cards; the smoke is an effect texture with soft alpha.

## Files

| File | Size | Use |
|---|---|---|
| `goo_surface_0`, `goo_surface_1` | 128x128 | The surface: slow dark troughs and light crests, bubbles with a rim, a hollow and a glint. Tiles both ways (pit and moat use the same material, the moat at a larger UV repeat). Swap the two frames every 20 to 30 frames, or scroll the UVs slowly, to make it churn. |
| `goo_bubble_0`, `_1`, `_2` | 32x32 | A bubble swelling, full, and popping into droplets. Play them in order at random spots on the surface. |
| `goo_smoke` | 64x64 | A soft pale-green wisp (effect texture, soft alpha). Spawn puffs that drift up, grow and fade. |

## Checks

| Check | Status | Measured |
|---|---|---|
| R10: power of two | **pass** | 128x128 |
| R10: wraps both ways | **pass** | Seam difference 7.1 (left/right) and 11.8 (top/bottom), against 8.6 between neighbouring texels mid-tile |
| R10: flat tones, 3 to 5 per material | **pass** | Base `#5aa82a`, light `#86cc3e`, dark `#3a7a1c`, deep drop `#24520e`, glint `#c4ee70` |
| R10: opaque | **pass** | Alpha 255 throughout the surface; the bubbles 1-bit |

## Rough edges

- At a distance the troughs and crests read a little like grass; the
  bubbles and the smoke sell it as goo. A more sickly yellow tint is a
  one-line change in `goo.py` if it needs it.
