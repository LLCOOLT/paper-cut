"""Chapter 2's props (Super Luigi; docs/super_luigi/ch2.md, New assets 6 and 7),
from the art in port/assets/super_luigi/art/ch2_props/ (sl_rig.py;
docs/LUIGI_ANIMS.md, "Chapter 2's props"). Put each as an NPC (or a map
object's model) where the scene wants it.

- `c_sl_banner`: the "CONGRATULATIONS" banner over the mayor's door, 64 x 11
  units, its origin the middle of its top edge (the rope line: set the NPC's
  position at the lintel, the banner hangs below it). One card, `banner`,
  whose drawing is the story's state: binds 0 "CONGRATULATIONS FUS" (half
  painted, at arrival), 1 "... LUIGI" (after the boss), 2 "... LUIGI & FUS"
  (after the proposal). A pose per state, and the turn from one to the next.
- `c_sl_cage`: the brides' bone cage, 24 x 29 units, its origin the middle of
  its base: `cage_back` (the far bars, z -3) behind the brides and
  `cage_front` (the near bars, z +3) in front, so brides placed at the cage's
  position stand inside it. The near bars hang from `door`, a hinge at the
  cage's front edge (x -11), and swing open about it.

Sergeant Bulkhead's stripe is sl_bulkhead.py (a variant of the disc's c_heavy).
"""
import math

from sl_rig import P, Rig, loop, once

# ----------------------------------------------------------------- banner ----

BANNER_LAYOUT = [
    ("|root", None, None, 0.0, [], True, None),
    ("banner", "|root", "banner_fus", 0.0, ["banner_luigi", "banner_luigi_fus"], True, (0.0, 0.0)),
]


def _sway(f, tex, period=120):
    s = math.sin(2 * math.pi * f / period)
    return {"banner": {"rz": 0.6 * s, "sy": 1.0 + 0.015625 * (0.5 + 0.5 * math.cos(2 * math.pi * f / period)),
                       "tex": tex}}


def _turn(old, new):
    """the banner turned over to its new drawing: lifted on a gust, a paper flip
    (edge on at 8, the new side coming round), settling with a swing (30, held)"""
    return once(30, [(0, _sway(0, old)),
                     (4, {"banner": {"rz": -2.0, "sy": 0.9375, "tex": old}}),
                     (8, {"banner": {"sx": 0.0625, "rz": -1.0, "sy": 0.9375, "tex": old}}, {"ease": "linear"}),
                     (9, {"banner": {"sx": 0.0625, "rz": -1.0, "sy": 0.9375, "tex": new}}),
                     (16, {"banner": {"sx": 1.0, "rz": 2.5, "sy": 1.03125, "tex": new}}),
                     (22, {"banner": {"rz": -1.2, "tex": new}}),
                     (30, _sway(0, new))])


BM = {}
for _n, _t in (("S_1", 0), ("S_2", 1), ("S_3", 2)):
    BM[_n] = loop(120, [(f, _sway(f, _t)) for f in (0, 30, 60, 90, 120)])
BM["Z_1"] = BM["S_1"]
BM["C_2"] = _turn(0, 1)
BM["C_3"] = _turn(1, 2)

BANNER_POSES = {
    "S_1": "\"CONGRATULATIONS FUS\", half painted, swaying a little (loop 120)",
    "S_2": "\"CONGRATULATIONS LUIGI\" (after the boss; loop 120)",
    "S_3": "\"CONGRATULATIONS LUIGI & FUS\" (after the proposal; loop 120)",
    "C_2": "turning over from FUS to LUIGI on a gust, a paper flip (30, held; then S_2)",
    "C_3": "turning over from LUIGI to LUIGI & FUS (30, held; then S_3)",
}

BANNER = Rig("c_sl_banner", "ch2_props", BANNER_LAYOUT, BM,
             camera={"height": 20.0, "width": 72.0, "center": (0.0, -15.0), "res": (576, 160)}, poses=BANNER_POSES)

# ------------------------------------------------------------------- cage ----

HINGE = (-11.0, 0.0)
CAGE_LAYOUT = [
    ("|root", None, None, 0.0, [], True, None),
    ("cage", "|root", None, 0.0, [], True, (0.0, 0.0)),
    ("cage_back", "cage", "cage_back", -3.0, [], True, None),
    ("door", "cage", None, 3.0, [], True, HINGE),
    ("cage_front", "door", "cage_front", 3.0, [], True, None),
]

CM = {}
CM["S_1"] = loop(60, [(0, {}), (60, {})])
CM["Z_1"] = CM["S_1"]
# H_1: rattled (the brides cheering, shaking the bars): a jitter (loop 16)
_R = lambda dx, rz: {"cage": {"tx": dx, "rz": rz}, "cage_front": {"tx": dx * 0.5}}
CM["H_1"] = loop(16, [(0, _R(0.0, 0.0)), (2, _R(0.35, 0.8)), (4, _R(-0.3, -0.6)), (6, _R(0.25, 0.5)),
                      (8, _R(0.0, 0.0)), (10, _R(-0.3, -0.7)), (12, _R(0.3, 0.6)), (14, _R(-0.15, -0.3)),
                      (16, _R(0.0, 0.0))])
# O_1: opening (after the boss): a rattle, then the near bars swing open on
# their hinge (a paper turn about the front edge, edge on), wobbling to rest
# (36, held). O_2: standing open (loop 60).
_OPEN = {"door": {"sx": 0.0625, "ty": 0.0}}
CM["O_1"] = once(36, [(0, {}), (3, _R(0.3, 0.6)), (6, _R(-0.3, -0.6)), (9, {}),
                      (12, {"door": {"sx": 0.875, "rz": -1.5}}),
                      (24, {"door": {"sx": 0.0625, "rz": 0.0}}, {"ease": "vector"}),
                      (28, {"door": {"sx": 0.1875}}), (32, _OPEN), (36, _OPEN)])
CM["O_2"] = loop(60, [(0, _OPEN), (60, _OPEN)])

CAGE_POSES = {
    "S_1": "standing shut (loop 60)",
    "H_1": "rattled: the brides shaking the bars (loop 16)",
    "O_1": "opening: a rattle, the near bars swing open on their hinge at the front edge (36, held)",
    "O_2": "standing open (loop 60)",
}

CAGE = Rig("c_sl_cage", "ch2_props", CAGE_LAYOUT, CM,
           camera={"height": 34.0, "width": 40.0, "center": (0.0, -2.0), "res": (320, 272)}, poses=CAGE_POSES)
