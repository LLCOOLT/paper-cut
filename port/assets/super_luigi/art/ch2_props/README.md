# Chapter 2's smaller props (Super Luigi)

The optional props from `docs/super_luigi/ch2.md`, New assets 6 and 7. The
ice wall has its own folder ([`../ice_wall/`](../ice_wall/)), and the
Plumpbelly Pie uses the game's Mousse Cake icon, as the plan allows.

- **Scale:** 8 texels per unit, outlined paper props lit from the top left.
- **Sources:** made by [`../tools/ch2_props.py`](../tools/ch2_props.py), which
  also writes `parts.json` and `preview.png`.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | What it is |
|---|---|---|---|
| `banner_fus` | 512x88 | (256, 12.8) | The banner over the mayor's door at arrival: "CONGRATULATIONS FUS" painted in red, and the rest of her name, "ELLE", only pencilled in (they stopped painting when the brides were taken). The pivot is the middle of its top edge. |
| `banner_luigi` | 512x88 | (256, 12.8) | After the boss: "CONGRATULATIONS LUIGI" |
| `banner_luigi_fus` | 512x88 | (256, 12.8) | After the proposal: "CONGRATULATIONS LUIGI & FUS" |
| `cage_back` | 192x232 | (96, 224) | The brides' bone cage, far bars and floor ring, a tone darker. Behind the bride. |
| `cage_front` | 192x232 | (96, 224) | The near bars: bone ribs curving in to a skull on the crown, a hoop and the base ring. In front of the bride. Both pivot at the base's middle. |
| `sergeant_stripe` | 48x56 | (24, 28) | Sergeant Bulkhead's badge: three gold chevrons on a blue shield. Pin it to the front of the Bulky Bob-omb's body, upper left of centre, and scale it with the 1.4x model, so the game's own texture isn't redrawn. |

The banners are 64 units wide and 11 tall, with a rope loop at each end to
tie over the door. The letters are the hand-cut capitals of
[`../tools/lettering.py`](../tools/lettering.py) (STYLE.md R12), each with a
small tilt and bob, a darker paint edge below right and a few drips, so they
read as painted by hand.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 48 to 512 px (under the 528 limit) |
| R2: 8 texels/unit | **pass** | |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 4–5 px, 3–9 | **pass** | Banners median 4–5 (p10–p90 3–6), cage 5, stripe 4 |
| R4: heavier bottom/right | **pass** | Banners L5 T4, R7 B6. Cage front L4, R6. Stripe L3 T3, R6 B7. |
| R4: line colour, lum ≤ 60 | **pass** | Cloth `#3e2e14` (lum p90 49), bone `#3a3020` (50), badge `#0e1638` (24) |
| R5: fills | **pass** | Cloth `#ece0c2` s .18 v .93 (neutral, warm). Paint `#c42a36` s .79 v .77. Bone `#e4dcc4` s .14 v .89, the far bars v .79. Badge `#3a5aa8` s .65 v .66, gold s .73 v .90. |
| R6: soft top-left shading | **pass** | Bands 5 to 6 px on the cloth, 3 to 4 px on each bone |
| R6b: sheen | **pass** | Banners 7.7–7.8% at (0.35, 0.34), white 0.61 on the near-white cloth. (`banner_luigi` reads the red paint as its base, so the finder's 36% there is the cloth itself.) Each bone carries its own small sheen; the cage card's finder value (8.3%) is for the whole tangle. |

## Rough edges

- The cage's ribs meet in a busy knot under the skull. It reads as a cage of
  bones at game distance; a cleaner crown would need fewer ribs.
