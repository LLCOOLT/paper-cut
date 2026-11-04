"""Sergeant Bulkhead (Super Luigi, chapter 2's mid-boss; docs/super_luigi/ch2.md):
`c_sl_bulkhead`, the disc's Bulky Bob-omb (`c_heavy`) with a sergeant's
stripe pinned to his body, a variant made at startup as Luigi's are
(sl_attach.py; port/src/aurora/sl_models_aurora.c reads the base from the
disc). Art: port/assets/super_luigi/art/ch2_props/sergeant_stripe.png.

The stripe hangs from the body's plane (`pPlane2`, the round body: x -15..17,
y 1.3..33.3 at rest), so it rides every one of the disc's poses (every one
plays on the variant as it is); it sits high on his back half, clear of the
eyes (x -8.4..-0.3) and the wind-up key, like a sleeve's chevrons. The card is
drawn for the world at 8 texels a unit, and the battle unit is the model at
1.4x, so in the model it is drawn 1/1.4 the size. No poses of its own.
"""
import json
import os

import paper_model as PM
from sl_rig import ART, PX

NAME = "c_sl_bulkhead"
BASE = "c_heavy"
SCALE = 1.4           # the unit's scale (ch2.md: the Bulky Bob-omb at 1.4x)
AT = (4.5, 25.5)      # the stripe's middle on the body, in the model's units
Z = 0.35             # in front of the body (0.1) and its eyes (0.2, 0.3)
MOTIONS = {}
POSES = {}


def cards():
    m = json.load(open(os.path.join(ART, "ch2_props", "parts.json")))["cards"]["sergeant_stripe"]
    w, h = m["size"][0] / PX / SCALE, m["size"][1] / PX / SCALE
    img = PM.pad8(PM.read_png(os.path.join(ART, "ch2_props", "sergeant_stripe.png")))
    x, y = AT
    return [{"name": "sgt_stripe", "parent": "pPlane2", "images": [img],
             "rect": (x - w / 2, y - h / 2, x + w / 2, y + h / 2), "z": Z, "pivot": (x, y), "visible": True}]
