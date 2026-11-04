# Rumblebump Volcano: lava and rock textures (chapter 1)

Chapter 1 borrows Keelhaul Key's jungle and caves (`muj_*`) and tints them
toward lava. These textures turn the caves (`muj_10`..`muj_12`) into the
volcano's insides.

- **Style:** map textures follow STYLE.md R10, measured on `muj_10`'s own
  textures: power-of-two sizes, tiling, flat tones with stippled grout and
  a dark drop line under each stone, no outlines, and 1-bit alpha only on
  edge strips.
- **Sources:** made by [`../tools/volcano.py`](../tools/volcano.py) with
  seeded procedural shapes (tileable noise and wrapped Voronoi cells), so a
  rerun gives the same texels. `preview.png` shows each texture tiled 2x2,
  or 2x1 for the strips.
- **Texel density:** each texture has the size of the `muj_10` texture it's
  meant to replace, so the map's density doesn't change.
- **Hooking them up:** the indices below are `muj_10`'s TPL (`m/muj_10/t`).
  The swap itself goes through the built-in mod's texture pack (the
  textures' Dolphin names). That wiring is the integrator's job, not part of
  this folder.

## Textures

| File | Size | Tiles | Replaces (`muj_10`) | What it is |
|---|---|---|---|---|
| `volcano_rock_wall.png` | 256x256 | both ways | `t_6`, the golden flagstone cave wall | Red-brown basalt flagstones (`#a8583a`, light `#cc7c52`, dark `#7a3622`) with stippled dark grout (`#5c2618`, dots `#3c140c`). A few cracks glow with lava (`#f07222`, core `#ffc848`). |
| `volcano_rock_edge.png` | 256x128 | horizontally | `t_2` / `t_16`, the rock clumps along ledges | The same basalt as a hanging edge. 1-bit alpha below the clumps. |
| `volcano_cave_streaks.png` | 256x256 | horizontally | `t_21`, the streaked cave wall | Dark at the top, deepening to ember orange at the bottom in jagged flat steps, with drips. |
| `volcano_ash_ground.png` | 128x128 | both ways | `t_12`, the pebble floor | Ash-grey pebbles in three tones, a lit top edge and a dark drop line, and the odd glowing ember. |
| `volcano_lava_1.png` | 256x256 | both ways | (new: lava pools, or the water planes) | Molten lava in four flat glow tones (`#e04a1a` to `#ffe680`) with floating crust plates. Each plate has a bright rim, a stippled top and a drop line. |
| `volcano_lava_2.png` | 256x256 | both ways | (new) | Frame 2: the crust drifted 6, 3 texels and the glow turned. Alternate it with frame 1 every 20 to 30 frames, or scroll either one slowly. |
| `volcano_lava_edge.png` | 256x64 | horizontally | (new: the shore of a lava pool) | A crusted lip over a bright molten band. 1-bit alpha above the lip. |

## STYLE.md R10 checklist

| Rule | Status | Measured |
|---|---|---|
| Power-of-two size, the same as the texture replaced | **pass** | 256x256, 256x128, 128x128 and 256x64, matching `t_6`, `t_2`, `t_21` and `t_12` |
| Tiles | **pass** | Generated on a torus (wrapped noise and wrapped Voronoi distances), so the seams match by construction. The preview shows each one tiled. |
| No outlines; a drop line under the stones | **pass** | The drop line is 2 texels deep under stones and crust (1 under pebbles). |
| Flat tones, 3 to 5 per material | **pass** | Rock: 3 tones plus grout, dots and drop line. Lava: 4 glow tones. Crust: 2 tones plus dots and drop line. Ash: 3 pebble tones. |
| Stippled grout | **pass** | 2x2 dots on an 8-texel grid, alternate rows offset (crust: a 10-texel grid). |
| Alpha | **pass** | Fills are opaque. The two edge strips have 1-bit alpha. |
| Saturation and value | **pass** | Rock s .66 v .66, glow s .86 v .94, ash s .29 v .36 (dull, like `t_12`'s s .28 v .35). |

## Rough edges

- The texture swaps aren't wired in, and I haven't seen them on the map.
  The ceiling and floor might want their own variants once they're checked
  in the game.
- `muj_11` and `muj_12` have their own TPLs. Where they reuse these
  textures' looks, the same files serve at the same sizes.
