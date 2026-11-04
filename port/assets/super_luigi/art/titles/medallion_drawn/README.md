# Luigi's chapter-card medallion

The chapter card (map `sys_01`, `rels/sys/src/sys.c`) spins a gold coin
under "Chapter N". It isn't a map texture (`sys_01` has no `t` file): the
code draws images 0 to 3 of `e/<lang>/effect_sys.tpl` on a quad, turning it
about Y and swapping to the next image every half turn, in the order Mario,
Peach, Bowser, the Crystal Star emblem. Super Luigi's cards need Luigi in
Mario's place, so this replaces image 0. The other three aren't Mario and
are left as they are.

- **Source:** made by [`../../tools/medallion.py`](../../tools/medallion.py),
  which also writes `preview.png` (the new coin at 3x and at game size).
- **Measured on the original**, decoded from the disc into scratch. Nothing
  from the disc is in the repo, so the preview doesn't include the original.
  To compare, run `medallion.py ORIGINAL.png COMPARE.png` with the decoded
  image 0.

## Files

| File | Replaces | Size | Format | Alpha |
|---|---|---|---|---|
| `tex1_160x160_93898cc57cbf557a_14.png` (`.svg` is its source) | `effect_sys.tpl` image 0, Mario's face | 160x160 | CMPR (14), no mipmaps, linear filter | 1-bit: a round coin with transparent corners |

The name is Dolphin's: the XXH64 of the image's CMPR data (12800 bytes),
then format 14. That data is the same in `e/jp`, `e/us`, `e/ge`, `e/fr`,
`e/sp` and `e/it` on the JP, US and EU discs, so one file covers every
version and language. Put it in a texture set scoped to `sys_01`.

## Against the original

| | Original (Mario) | New (Luigi) |
|---|---|---|
| Size, alpha | 160x160, 2 alpha levels, 0 partial texels | the same |
| Coin cut | round | the circle that best fits the original's (centre 80.1, radius 80.2). It differs from it on 148 of 25600 texels, all on the dark edge. |
| Outline (check_style) | median 4 px, 3 to 7, `#664f00` | median 4 px, 3 to 7, `#634d00` |
| Light areas (check_style's sheen finder) | 32.8% at (0.48, 0.53) | 25.6% at (0.48, 0.53) |

Both are flat engraving: every tone is flat, with no gradient, blur or
airbrushed shading. The texels measured across each band of the original
give these tones and widths:

- **The coin:** a dark line (`#634d00`, 3 to 4 px) and a bright rim
  (`#ceb23c`, 5 to 6 px). The rim's inner bevel is a 2 px ring, dark
  (`#635510`) on the top and left and cream (`#fff7c6`) on the bottom and
  right, split hard along the diagonal. Inside is a flat olive field
  (`#99831e`).
- **The plate:** the head stands on a raised plate that follows its
  silhouette. From the outside in, it has a dark line (3 px), then a gold band
  (4 px), lit (`#dcc066`) on the top left and dark (`#a08218`) on the bottom
  right, split hard. Last is a cream bevel (`#fff1bd`, 2 to 3 px, a little
  wider on the top left) hugging the head's line.
- **The head:** a heavy dark line (`#634d00`, about 4 px). The cap, visor,
  hair and moustache are dark gold (`#967300`), and the skin is cream
  (`#fff1bd`).

Luigi is posed and framed as Mario is. He's in three-quarter profile facing
left, with the nose to the left, the visor pointing left and the ear on the
right, and his plate leaves the same ring of field inside the rim. What
makes him Luigi:

- the taller cap, with the L badge on its front panel
- the longer face, with the chin below the moustache
- his own moustache, which is longer and curls up at the back end

**The L is monochrome.** All four faces are struck in one gold, so his badge
is a cream oval with the L in the line colour, as Mario's M is. A green L
would be the only colour on the coin.
