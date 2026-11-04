# The race scoreboard (Super Luigi, chapter 3)

The giant board behind the first corner. In the race's pile-up it tips over
onto the wreck and buries Revvo (`docs/super_luigi/ch3.md`, New assets 7: "a
big flat board on legs, with a fall animation: can be a plain textured box
with a fall done in script").

- **Scale:** 8 texels per unit, outlined, lit from the top left. The board
  is 58 by 32 units and stands on legs 22 units tall, so its top is about
  54 units up (Luigi is 24).
- **Sources:** made by [`../tools/scoreboard.py`](../tools/scoreboard.py),
  which also writes `parts.json` and `preview.png`.

## Look

A red frame studded with bulbs round a navy panel. "CIRCUIT BREAK" in gold
between two checkered flags, then the starting grid in two columns: a
position, the kart's colour and number (the kart recolours' colours), and
the driver. Revvo is first in red #13 with a little crown; Luigi is eighth
in Big Green 01. ch3.md names no other drivers, so the other six show as
"KART". The back is plain planks and struts. The legs are steel with an X
brace.

## Cards

Each pivot is in pixels from the PNG's top-left.

| Card | Size | Pivot | Standing at | What it is |
|---|---|---|---|---|
| `board_face` | 480x272 | (240, 264) | (0, 22) | The face. The pivot is the middle of the board's bottom edge: rotate it about x there to tip it over. |
| `board_back` | 480x272 | (240, 264) | (0, 22) | The back, for a box's rear face or when it lies face down |
| `legs` | 480x192 | (240, 184) | (0, 0) | The legs. Their tops are at y 22, under the board's edge. They stay standing (or fall after it). |

For a box, put `board_face` on the front and `board_back` on the back, with
a red edge between them.

## STYLE.md checklist

Measured with [`../tools/check_style.py`](../tools/check_style.py).

| Rule | Status | Measured |
|---|---|---|
| R1: multiples of 8 | **pass** | 192 to 480 px (under 528) |
| R2: 8 texels/unit | **pass** | |
| R3: 1-bit alpha | **pass** | 2 levels, 0 partial texels |
| R4: outline 4–5 px, 3–9 | **pass** | Face and back median 4 (p10–p90 3–6). The legs measure 8, because the shadow band on a 2-unit steel tube is counted as line; the line is 4 to 5. |
| R4: heavier bottom/right | **pass** | Face and back B6 against T4. Legs B7 R7 against T4. |
| R4: line colour, lum ≤ 60 | **pass** | Frame `#3e0a0e` (lum p90 27), wood `#3a220c` (40), steel `#1e2230` (35) |
| R5: fills | **pass** | Frame `#d23a3a` s .72 v .82. Wood `#b8875a` s .51 v .72. Steel `#9aa0b0` s .13 v .69 (neutral, cool). The panel `#1e2a52` is a dark marking (v .32). |
| R6: soft top-left shading | **pass** | Bands 6 to 8 px with a 3 px blur |
| R6b: sheen | **pass** | Back 9.6% at (0.33, 0.21), white 0.27. On the face the sheen is a band along the frame's top (the panel is a screen, not a rounded part), 1.5% of the card. |

## Rough edges

- The lettering is lettering.py's hand-cut capitals (STYLE.md R12). The
  standings rows are only 15 to 17 px tall, so the letters are set with less
  wobble than on the signs.
