"""King Luigi (Super Luigi, chapter 3's crowning; docs/super_luigi/ch3.md beat 16):
`c_luigi_crown`, a variant of c_luigi_pc (sl_attach.py, as the bride) with
the King's crown (port/assets/super_luigi/art/ch3_props/crown.png) on his cap:
the card at 0.8x, tilted a little back, sitting on the cap's crown, hung
from the cap (`pPlane4`) so it rides every pose of his. No poses of its own:
every one of Luigi's plays with the crown on.
"""
import json
import math
import os

import paper_model as PM
from sl_rig import ART, PX

NAME = "c_luigi_crown"
BASE = "c_luigi_pc"
CAP = "pPlane4"
SCALE = 0.8
TILT = 10.0          # degrees, the top tipped back (+x: he faces -x)
BASE_AT = (1.2, 23.4)  # the crown's base on the cap's crown, in c_luigi's bind-pose units
Z = 0.45             # in front of the cap and its L
MOTIONS = {}
POSES = {}
CAMERA = {"height": 34.0, "width": 34.0, "center": (0.0, -2.0), "res": (272, 272)}


def _image():
    """(rows, pivot in texels): the crown at SCALE, turned TILT back about its
    base (bilinear, alpha-weighted, then 1-bit alpha), sides of 8, the pivot
    where the base's middle lands"""
    src = PM.read_png(os.path.join(ART, "ch3_props", "crown.png"))
    sh, sw = len(src), len(src[0])
    m = json.load(open(os.path.join(ART, "ch3_props", "parts.json")))["cards"]["crown"]
    spx, spy = m["pivot"]
    a = math.radians(TILT)  # image y down: a clockwise turn on screen tips the top to +x
    ca, sa = math.cos(a), math.sin(a)
    W = H = 64  # room for the turned, scaled crown
    px, py = W / 2.0, H - 8.0  # where the pivot goes
    out = []
    for y in range(H):
        row = []
        for x in range(W):
            dx, dy = x + 0.5 - px, y + 0.5 - py
            # undo the turn (clockwise by a on screen), then the scale
            ux, uy = ca * dx + sa * dy, -sa * dx + ca * dy
            fx, fy = spx + ux / SCALE - 0.5, spy + uy / SCALE - 0.5
            acc, wa = [0.0, 0.0, 0.0], 0.0
            x0, y0 = math.floor(fx), math.floor(fy)
            for yy in (y0, y0 + 1):
                for xx in (x0, x0 + 1):
                    if 0 <= xx < sw and 0 <= yy < sh:
                        k = (1 - abs(fx - xx)) * (1 - abs(fy - yy))
                        r, g, b, al = src[yy][xx]
                        wa += al * k
                        for c, v in enumerate((r, g, b)):
                            acc[c] += v * al * k
            row.append(tuple(int(v / wa) for v in acc) + (255,) if wa >= 128 else (0, 0, 0, 0))
        out.append(row)
    return out, (px, py)


def cards():
    rows, (px, py) = _image()
    h, w = len(rows), len(rows[0])
    ax, ay = BASE_AT
    x0, y1 = ax - px / PX, ay + py / PX
    return [{"name": "crown", "parent": CAP, "images": [rows], "rect": (x0, y1 - h / PX, x0 + w / PX, y1), "z": Z,
             "pivot": (ax, ay, Z), "visible": True}]
