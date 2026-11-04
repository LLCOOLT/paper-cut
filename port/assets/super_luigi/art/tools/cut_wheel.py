"""Big Green 01's stolen steering wheel (c_sl_wheel, chapter 3's King's Suite), cut from the style model's design
(`python ch3_wheel.py`, which runs `cutout.py cut_wheel` and writes ch3_props/wheel.png).

The design: the style model (SDXL + ttydstyle, "paper cutout prop"), "a go-kart steering wheel seen from the front,
round black rubber rim, three silver spokes, a green centre hub", seed 2 (designs/props/wheel_seed2.png). It replaced
the recipe's ship's wheel (a Bob-omb's key recoloured), which read as a ship's wheel, not a kart's (ART_PLAN row 12).
One card, about 7 units across (56 game texels; at cutout.py's DENSITY 4, 208x200, its texels_per_unit in
ch3_props/parts.json), its pivot at its middle.
"""
import math

SRC = "designs/props/wheel_seed2.png"
OUT = "../../../../build-art2/cut_wheel"
MODEL = "c_sl_wheel"
PX = 21.5  # the rim is 980 design pixels across: about 46 texels
ORIGIN = (515, 515)
LINE = "#1e1a16"
THRESH = 30

CARDS = {
    "wheel": dict(poly=[(515 + 500 * math.cos(2 * math.pi * i / 64), 515 + 500 * math.sin(2 * math.pi * i / 64))
                        for i in range(64)], pivot=(515, 515), rim=False,
                  desc="Big Green 01's steering wheel, stolen: black rim, steel spokes, a green boss; pivot at its middle"),
}
