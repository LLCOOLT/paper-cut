"""Petunia Primrose (Super Luigi, chapter 4; docs/super_luigi/ch4.md):
`c_sl_petunia`, the disc's Crazee Dayzee (`c_pansy`) with pink petals, a
variant made at startup (sl_attach.py, a texture swap): her petal drawing
(the disc's image 8, 112x128) shows ours,
port/assets/super_luigi/art/ch4_fans/petunia_petals.png, drawn in its layout.
Only her model changes, so every other Crazee Dayzee keeps its lilac; her arms
(images 0 to 3) stay lilac too. Every pose of c_pansy plays on her as it is.
"""
import os

import paper_model as PM
from sl_rig import ART

NAME = "c_sl_petunia"
BASE = "c_pansy"
PETALS = 8            # c_pansy-'s petal drawing
MOTIONS = {}
POSES = {}
SWAPS = {PETALS: PM.read_png(os.path.join(ART, "ch4_fans", "petunia_petals.png"))}


def cards():
    return []
