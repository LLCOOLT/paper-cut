"""Luigi's bride veil (Super Luigi, chapter 2: he goes to Hizza dressed as its
bride): `c_luigi_bride`, a variant of c_luigi_pc (sl_attach.py) with the veil
from port/assets/super_luigi/art/veil/ hung from his cap (c_luigi's pPlane4),
so every pose of his, the disc's and ours, plays with the veil on. Its cards:

  veil_drape  the tulle falling behind his cap and head to his shoulders;
              binds: 0 hanging (veil_side), 1 blown back (veil_side_flutter)
  veil_band   the pearl band and flowers, in front of the cap's crown
  veil_back   the veil from behind (hidden: for a script that turns him away)
  gown        the bride's skirt, pinned at his waist (on his body, pPlane17), in
              front of his legs, the shoes showing below the hem; binds: 0
              standing (gown_side), 1 swung back (gown_side_swish)

Luigi's own poses don't key the veil, so it hangs in all of them; the
flutter and the back view are there for scenes of our own to use (the "tex"
channel of veil_drape, the "vis" of veil_back).
"""
import json
import os

import paper_model as PM

NAME = "c_luigi_bride"
BASE = "c_luigi_pc"
PX = 8.0
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "port", "assets", "super_luigi", "art",
                   "veil")
CUT = os.path.join(ART, "qwen")  # the cards: cut from the style model's bridal pieces (art/tools/cut_bride.py)
CAP = "pPlane4"
BODY = "pPlane17"


def _card(meta, name, card, z, binds=(), visible=True, parent=CAP):
    """a cut card, its pivot on the attach point the drawn card had on c_luigi (art/veil/parts.json)"""
    cut = json.load(open(os.path.join(CUT, "parts.json")))
    m = cut["cards"][card]
    tpu = float(m.get("texels_per_unit", cut.get("texels_per_unit", PX)))
    rows = PM.pad8(PM.read_png(os.path.join(CUT, card + ".png")))
    h, w = len(rows), len(rows[0])
    ax, ay = meta[card]["attach_model_xy"]
    px, py = m["pivot"]
    x0, y1 = ax - px / tpu, ay + py / tpu
    return {"name": name, "parent": parent, "rect": (x0, y1 - h / tpu, x0 + w / tpu, y1), "z": z,
            "pivot": (ax, ay, z), "visible": visible,
            "images": [rows] + [PM.pad8(PM.read_png(os.path.join(CUT, b + ".png"))) for b in binds]}


def cards():
    """the veil's cards (sl_attach's), by c_luigi's bind pose: the drape behind
    his head (his hair is at z -0.5), the band before the cap (z 0.2) and behind
    its L (0.3), the back view in front of everything"""
    meta = json.load(open(os.path.join(ART, "parts.json")))["cards"]
    return [
        _card(meta, "veil_drape", "veil_side", -0.9, binds=("veil_side_flutter",)),
        _card(meta, "veil_band", "veil_band", 0.25),
        _card(meta, "veil_back", "veil_back", 0.9, visible=False),
        _card(meta, "gown", "gown_side", 0.35, binds=("gown_side_swish",), parent=BODY),
    ]


# ---------------------------------------------------------------- motions ----
# Luigi's parts by c_luigi's names (luigi_motions.py's docstring), key poses
# as luigi_motions.py's. TW_1: a twirl, showing off the gown: arms out, two
# paper turns on a little hop, the skirt and veil swinging out, ending with a
# coy head tilt (48). FA_1: fainting: a wobble, eyes rolling shut, then flat
# on his back, the veil settling over his face (50, held). FA_2: lying in a
# faint (loop 60): breathing, a twitch of a foot.

_ARMS_OUT = {"pPlane16": {"rz": -75.0}, "pPlane15": {"rz": 70.0}, "pPlane3": {"rz": -20.0}, "pPlane2": {"rz": 15.0}}


def _p(*ds):
    out = {}
    for d in ds:
        for g, ch in d.items():
            out.setdefault(g, {}).update(ch)
    return out


_SWING = {"gown": {"tex": 1}, "veil_drape": {"tex": 1}}
_END = {"pPlane16": {"rz": -30.0}, "pPlane15": {"rz": -25.0}, "pPlane14": {"rz": -10.0}, "locator3": {"rz": -4.0}}
_LIE = {"locator2": {"rz": -90.0, "ty": 2.0}, "pPlane14": {"rz": -4.0}, "group1": {"sy": 0.125},
        "pPlane16": {"rz": 75.0}, "pPlane15": {"rz": -55.0}, "locator4": {"rz": 25.0},
        "locator5": {"rz": -10.0}, "veil_drape": {"rz": -8.0, "tz": 1.6, "sx": -1.0, "tx": 4.0}}
MOTIONS = {
    "TW_1": {"end": 48, "keys": [
        (0, {}),
        (4, _p(_ARMS_OUT, {"locator3": {"sy": 0.9375}})),
        (8, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": 0.0625, "ty": 1.5}, "locator3": {"sy": 1.0625}}),
         {"ease": "linear"}),
        (12, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": -1.0, "ty": 2.0}}), {"ease": "linear"}),
        (16, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": 0.0625, "ty": 2.0}}), {"ease": "linear"}),
        (20, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": 1.0, "ty": 1.5}}), {"ease": "linear"}),
        (24, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": 0.0625, "ty": 1.0}}), {"ease": "linear"}),
        (28, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": -1.0, "ty": 0.5}}), {"ease": "linear"}),
        (32, _p(_ARMS_OUT, _SWING, {"locator2": {"sx": 0.0625}}), {"ease": "linear"}),
        (36, _p(_ARMS_OUT, {"locator2": {"sx": 1.0}, "locator3": {"sy": 0.9375}}), {"ease": "vector"}),
        (42, _p(_END, {"group1": {"sy": 0.875}})),
        (48, _END),
    ]},
    "FA_1": {"end": 50, "keys": [
        (0, {}),
        (6, {"locator3": {"rz": 5.0, "ty": -0.3}, "pPlane14": {"rz": 10.0}, "group1": {"sy": 0.5},
             "pPlane16": {"rz": 10.0}, "pPlane15": {"rz": -8.0}}),
        (12, {"locator3": {"rz": -4.0}, "pPlane14": {"rz": -12.0}, "group1": {"sy": 0.25},
              "pPlane16": {"rz": -15.0}, "pPlane15": {"rz": 12.0}, "pPlane12": {"vis": 1}}),
        (18, {"locator2": {"rz": -12.0}, "locator3": {"rz": 3.0}, "pPlane14": {"rz": 6.0}, "group1": {"sy": 0.125},
              "pPlane16": {"rz": 40.0}, "pPlane15": {"rz": -30.0}, "locator4": {"rz": 10.0}}),
        (28, _p(_LIE, {"pPlane14": {"rz": -6.0}, "pPlane16": {"rz": 80.0}, "pPlane15": {"rz": -60.0},
                       "veil_drape": {"rz": -25.0, "tz": 1.6, "sx": -1.0, "tx": 4.0}}), {"ease": "vector"}),
        (33, _p(_LIE, {"locator2": {"rz": -84.0, "ty": 2.6}, "pPlane14": {"rz": -2.0}, "pPlane16": {"rz": 70.0},
                       "pPlane15": {"rz": -50.0}, "locator4": {"rz": 30.0}, "veil_drape": {"rz": -15.0, "tz": 1.6, "sx": -1.0, "tx": 4.0}})),
        (40, _LIE, {"ease": "vector"}),
        (50, _LIE),
    ]},
    "FA_2": {"end": 60, "loop": True, "keys": [
        (0, _LIE),
        (20, _p(_LIE, {"locator3": {"sx": 1.0625}, "veil_drape": {"rz": -5.0, "tz": 1.6, "sx": -1.0, "tx": 4.0}})),
        (40, _LIE),
        (44, _p(_LIE, {"locator4": {"rz": 35.0}})),
        (48, _LIE),
        (60, _LIE),
    ]},
}
POSES = {"TW_1": "a twirl in the gown (48)", "FA_1": "fainting flat on his back, the veil over his face (50, held)",
         "FA_2": "lying in a faint (loop 60)"}
CAMERA = {"height": 32.0, "width": 48.0, "center": (6.0, -6.0), "res": (384, 256)}
