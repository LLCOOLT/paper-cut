"""Luigi as the Grass (Super Luigi, chapter 4: the Drama Slam; docs/super_luigi/ch4.md):
`c_luigi_grass`, a variant of c_luigi_pc (sl_attach.py, as the bride) in the
grass costume from port/assets/super_luigi/art/grass/: a bush of paper blades
with his face and moustache peeking out and his shoes at the bottom. For the
field (rehearsals, the fans in the alley) and the battle (the play: his
battle model).

Cards, pinned as the art's README has them (c_luigi's bind pose):
  grass_back    the far blades, behind all of his cards (his body, pPlane17)
  grass_front   the near blades over his body and legs, in front of them and
                behind his front glove (pPlane17); binds 0 still, 1 swept back
  grass_hood    the mound and crown of blades over his cap and down the back
                of his head (his head, pPlane14); binds 0 still, 1 swept back
Every pose of Luigi's plays with the costume on (the battle's jump and hammer,
hurt, KO...), the blades following his body and head; its own poses below.
"""
import json
import os

import paper_model as PM
from sl_rig import ART, PX

NAME = "c_luigi_grass"
BASE = "c_luigi_pc"
BODY, HEAD = "pPlane17", "pPlane14"
# the near blades sit this much higher than the art's pin, so his shirt doesn't
# show between them and the hood on his face side (in his stand and walks)
FRONT_LIFT = 1.5


def _card(meta, name, card, parent, z, binds=(), lift=0.0):
    m = meta[card]
    rows = PM.pad8(PM.read_png(os.path.join(ART, "grass", card + ".png")))
    h, w = len(rows), len(rows[0])
    ax, ay = m["model_xy"]
    ay += lift
    px, py = m["pivot"]
    x0, y1 = ax - px / PX, ay + py / PX
    return {"name": name, "parent": parent, "rect": (x0, y1 - h / PX, x0 + w / PX, y1), "z": z,
            "pivot": (ax, ay, z), "visible": True,
            "images": [rows] + [PM.pad8(PM.read_png(os.path.join(ART, "grass", b + ".png"))) for b in binds]}


def cards():
    meta = json.load(open(os.path.join(ART, "grass", "parts.json")))["cards"]
    return [
        _card(meta, "grass_back", "grass_back", BODY, -1.4),
        _card(meta, "grass_front", "grass_front", BODY, 0.35, ("grass_front_sway",), lift=FRONT_LIFT),
        _card(meta, "grass_hood", "grass_hood", HEAD, 0.45, ("grass_hood_sway",)),
    ]


# ---------------------------------------------------------------- motions ----
# Luigi's parts by c_luigi's names (luigi_motions.py): locator2 the whole,
# locator3 the body, pPlane14 the head, group1 the eyes, pPlane16 / pPlane15
# the arms, locator4 / locator5 the legs.

def _p(*ds):
    out = {}
    for d in ds:
        for g, ch in d.items():
            out.setdefault(g, {}).update(ch)
    return out


_SWAY = {"grass_front": {"tex": 1}, "grass_hood": {"tex": 1}}
_STILL = {"grass_front": {"tex": 0}, "grass_hood": {"tex": 0}}
# arms tucked in the blades: only the front glove's tip shows
_TUCK = {"pPlane16": {"rz": -6.0}, "pPlane15": {"rz": 4.0}}
_LIE = {"locator2": {"rz": -90.0, "ty": 2.0}, "pPlane14": {"rz": -4.0}, "pPlane16": {"rz": 10.0},
        "pPlane15": {"rz": -10.0}}

MOTIONS = {
    # GR_S: standing still, being grass: the slightest breath, a blink (loop 120)
    "GR_S": {"end": 120, "loop": True, "keys": [
        (0, _p(_TUCK, _STILL)), (60, _p(_TUCK, _STILL, {"locator3": {"sy": 1.03125}})),
        (90, _p(_TUCK, _STILL, {"group1": {"sy": 1.0}})), (92, _p(_TUCK, _STILL, {"group1": {"sy": 0.125}})),
        (96, _p(_TUCK, _STILL, {"group1": {"sy": 1.0}})), (120, _p(_TUCK, _STILL))]},
    # GR_W: the shuffle: small hops in the costume, the blades swinging back on
    # each (loop 32; the walk's own steps are hidden in the blades)
    "GR_W": {"end": 32, "loop": True, "keys": [
        (0, _p(_TUCK, _STILL, {"locator2": {"ty": 0.0}, "locator3": {"sy": 0.9375}})),
        (6, _p(_TUCK, _SWAY, {"locator2": {"ty": 0.8}, "locator3": {"rz": -3.0, "sy": 1.03125}})),
        (12, _p(_TUCK, _STILL, {"locator2": {"ty": 0.0}, "locator3": {"sy": 0.9375}}), {"ease": "vector"}),
        (16, _p(_TUCK, _STILL, {"locator2": {"ty": 0.0}, "locator3": {"sy": 0.9375}})),
        (22, _p(_TUCK, _SWAY, {"locator2": {"ty": 0.8}, "locator3": {"rz": -3.0, "sy": 1.03125}})),
        (28, _p(_TUCK, _STILL, {"locator2": {"ty": 0.0}, "locator3": {"sy": 0.9375}}), {"ease": "vector"}),
        (32, _p(_TUCK, _STILL, {"locator2": {"ty": 0.0}, "locator3": {"sy": 0.9375}}))]},
    # GR_L: lying flat: down onto his front on the stage (24, held); GR_L2:
    # lying there, one eye opening now and then (loop 90); Lie Low too
    "GR_L": {"end": 24, "keys": [
        (0, _p(_TUCK, _STILL)), (6, _p(_TUCK, _SWAY, {"locator2": {"rz": -20.0, "ty": 0.5}})),
        (16, _p(_LIE, _SWAY), {"ease": "vector"}), (20, _p(_LIE, _STILL, {"locator2": {"rz": -86.0, "ty": 2.4}})),
        (24, _p(_LIE, _STILL))]},
    "GR_L2": {"end": 90, "loop": True, "keys": [
        (0, _p(_LIE, _STILL, {"group1": {"sy": 0.125}})), (50, _p(_LIE, _STILL, {"group1": {"sy": 0.125}})),
        (54, _p(_LIE, _STILL, {"group1": {"sy": 1.0}})), (70, _p(_LIE, _STILL, {"group1": {"sy": 1.0}})),
        (74, _p(_LIE, _STILL, {"group1": {"sy": 0.125}})), (90, _p(_LIE, _STILL, {"group1": {"sy": 0.125}}))]},
    # GR_R: the rustle, meaningfully: a happy shimmy, the blades flicking
    # between still and swept every 5 frames (loop 20)
    "GR_R": {"end": 20, "loop": True, "keys": [
        (0, _p(_TUCK, _STILL, {"locator3": {"rz": 4.0, "sx": 1.03125}})),
        (5, _p(_TUCK, _SWAY, {"locator3": {"rz": -4.0, "sx": 0.96875}, "locator2": {"ty": 0.4}})),
        (10, _p(_TUCK, _STILL, {"locator3": {"rz": 4.0, "sx": 1.03125}})),
        (15, _p(_TUCK, _SWAY, {"locator3": {"rz": -4.0, "sx": 0.96875}, "locator2": {"ty": 0.4}})),
        (20, _p(_TUCK, _STILL, {"locator3": {"rz": 4.0, "sx": 1.03125}}))]},
    # GR_J: joy (the fans): rustling with little hops, tears of joy (loop 16)
    "GR_J": {"end": 16, "loop": True, "keys": [
        (0, _p(_TUCK, _STILL, {"locator3": {"sy": 0.9375}})),
        (4, _p(_TUCK, _SWAY, {"locator2": {"ty": 2.0}, "locator3": {"rz": -5.0, "sy": 1.0625}})),
        (8, _p(_TUCK, _STILL, {"locator3": {"sy": 0.9375}}), {"ease": "vector"}),
        (12, _p(_TUCK, _SWAY, {"locator2": {"ty": 2.0}, "locator3": {"rz": 5.0, "sy": 1.0625}})),
        (16, _p(_TUCK, _STILL, {"locator3": {"sy": 0.9375}}))]},
}
POSES = {
    "GR_S": "standing still, being grass: a breath, a blink (loop 120)",
    "GR_W": "the shuffle: small hops, the blades swinging back on each (loop 32)",
    "GR_L": "lying flat on his front (24, held; Lie Low)",
    "GR_L2": "lying flat, one eye opening now and then (loop 90)",
    "GR_R": "the rustle, meaningfully: a shimmy, the blades flicking (loop 20; the bow he can't do)",
    "GR_J": "joy (the fans): rustling, hopping (loop 16)",
}
CAMERA = {"height": 34.0, "width": 40.0, "center": (2.0, -4.0), "res": (320, 272)}
