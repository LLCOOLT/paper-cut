# The three lenses, their sockets and beams (Super Luigi, chapter 5)

From `docs/super_luigi/ch5.md`, New assets 5: "crescent, circle and
teardrop discs of glass in stone rims (silver, gold, blue), and the three
sockets (stone stands) round the dais. Each lens's light beam (a tall
translucent cylinder of colour)". The Moon Lens is the crescent, the Sun Lens
the circle and the Tear Lens the teardrop.

- **Scale:** 8 texels per unit. Lenses and sockets are outlined props lit
  from the top left, with 1-bit alpha.
- **Sources:** made by [`../tools/lenses.py`](../tools/lenses.py), which also
  writes `parts.json` and `preview.png`.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `lens_moon` | 56x72 | (28, 68) | A crescent of pale glass in a silver rim. The pivot is the lens's foot. |
| `lens_sun` | 56x72 | (28, 68) | A disc of warm glass in a gold rim |
| `lens_tear` | 56x72 | (28, 68) | A teardrop of blue glass in a blue rim |
| `socket_moon`, `socket_sun`, `socket_tear` | 88x112 | (44, 104) | A carved stone stand (base, waisted post, a cap with two Luff swirls), whose cap holds the empty recess in its lens's shape. The pivot is the base's middle. Set the lens's pivot at the socket's (0, 6.6) in model units, which puts the lens over the recess. |
| `beam_moon`, `beam_sun`, `beam_tear` | 64x256 | (32, 256) | Each lens's beam: an **effect texture** with a soft alpha ramp (like the game's IA4 effects, STYLE.md R10), a bright core and a colour (silver-white, gold, blue). Map it round a tall cylinder from the lens up to the ceiling, alpha-blended or additive. The bright end is the bottom. |

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 56x72, 88x112, 64x256 |
| R3: 1-bit alpha | **pass** (lenses, sockets) | 2 levels, 0 partial texels. The beams are effects and keep soft alpha on purpose. |
| R4: outline 4–5 px, 3–9 | **pass** | Sockets median 5 (L4 T4, R6 B6). Lenses 6 to 7: the finder counts the metal rim's shadow as line; the line itself is 4 to 5. |
| R4: line colour, lum ≤ 60 | **pass** | Stone `#2e2a24` (43), silver `#1e2230` (35), gold `#4a3006` (52), blue `#0c1a48` (28) |
| R5: fills | **pass** | Stone `#c8c2b4` s .10 v .78 (neutral, warm). Rims: silver `#c4c8d4`, gold `#e6b43c`, blue `#4a7ad0`. Glass is pale (v .96 to 1.0) as a lit gem may be. |
| R6b: sheen, specular dot | **pass** | A glint streak and a white specular dot on each lens (glossy props). Sockets 26.7 to 26.9% (the finder counts the cap's lit band with the sheen). |

## Rough edges

- The lenses read best at 2x; at 1:1 on the stand the crescent's rim is
  thin.
