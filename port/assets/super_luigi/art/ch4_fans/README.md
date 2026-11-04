# Chapter 4's optional props: the fans' leaves and banner, Dazzlina's tiara, the Baron's hat

From `docs/super_luigi/ch4.md`, New assets 5 and 6 and Hayzee's battle
poses, all marked optional: "Paper leaves for the fans (a held prop) and
the 'WE ♥ THE GRASS' banner (a texture)", "Dazzlina's sparkle: the Amazy
Dayzee at 1.2x with a tiara of sequins", and "the Baron's costume (a tiny
top hat: a prop)".

- **Scale:** 8 texels per unit, outlined, lit from the top left.
- **Sources:** made by [`../tools/ch4_fans.py`](../tools/ch4_fans.py), which
  also writes `parts.json` and `preview.png`.
- **Lettering:** the banner's words are the hand-cut capitals of
  [`../tools/lettering.py`](../tools/lettering.py) (STYLE.md R12), painted
  in green with the heart in red, as a fan would paint it.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `paper_leaf` | 40x64 | (20, 56) | A fan's paper leaf on a stick: a broad green leaf with a midrib and veins. The pivot is the grip; wave it from there. |
| `paper_grass` | 40x64 | (20, 56) | The same with three grass blades instead (the Grass's own fans). Mix the two in the crowd. |
| `banner_grass` | 288x72 | (144, 5.6) | "WE ♥ THE GRASS" painted on a hand-cut strip of paper, taped at the top corners, 36 by 9 units. The pivot is the middle of its top edge: hang it from the balcony, or have two fans hold its ends. |
| `dazzlina_tiara` | 48x32 | (24, 27.2) | A silver tiara set with sequins (pink, cyan, yellow, violet, green, Amazy Dayzee's colours) and a sparkle star at the peak. Pin it on her head between the petals and scale it 1.2x with her. |
| `baron_hat` | 40x40 | (20, 35.2) | The Baron's tiny top hat for Hayzee: black with a purple band. Set it on top of his head card, tipped a little. |
| `petunia_petals` | 112x128 | | **Petunia Primrose's pink petals**, a replacement for the Crazee Dayzee's petal texture (`a/c_pansy-` image 8, 112x128), drawn new in its layout: four outlined petals (up, left, right, down) round the face's place. Swap it on **her model only** (a per-NPC texture override): a texture-pack swap by the texture's name would turn every Crazee Dayzee pink. Her arms (images 0 to 3) stay lilac. |

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: sides are multiples of 8 | **pass** | All six |
| R2: 8 texels/unit | **pass** | Leaves 5x8 units, banner 36x9 |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels on every card |
| R4: dark outline in the family | **pass** | Leaves `#163a0c`, paper `#3e3218`, silver `#262a38`, hat `#0c0a10` |
| R5: fills | **pass** | Leaf `#6cbc3e`, paper `#f2ead2` (a neutral, s .13), band `#8a3ab0`; the top hat's black is a dark accent |
| R12: lettering | **pass** | lettering.py, no font file |

## Rough edges

- Not seen in the game. The sequins are dots of colour at 48 px; at
  Dazzlina's 1.2x they read as a sparkle rather than single sequins.
