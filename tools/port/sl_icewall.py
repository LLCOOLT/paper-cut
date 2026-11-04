"""The ice wall (Super Luigi, chapter 2: Jerry's Blast tutorial across the lane
to the tailor; docs/super_luigi/ch2.md), from the art in
port/assets/super_luigi/art/ice_wall/ (sl_rig.py; docs/LUIGI_ANIMS.md, "The
ice wall"): `c_sl_ice_wall` (20 x 22 units) and `c_sl_ice_wall_large`
(40 x 30), a packed-snow wall standing on its foot's middle, with chunks
that fly out when it bursts. A prop: put it as an NPC (or a map object's
model) where the lane is blocked; the burst ends with nothing showing, so
the script can then remove it.

Parts: wall (the wall card) and chunk_1..6 (hidden, thrown out in the burst).
"""
from sl_rig import P, Rig, loop, once

CHUNKS = [("chunk_1", "chunk_a", (-5.0, 16.0), (-14.0, 10.0, -300.0)),
          ("chunk_2", "chunk_b", (4.0, 18.0), (12.0, 12.0, 280.0)),
          ("chunk_3", "chunk_c", (-2.0, 9.0), (-10.0, -4.0, -200.0)),
          ("chunk_4", "chunk_a", (6.0, 8.0), (15.0, -3.0, 240.0)),
          ("chunk_5", "chunk_b", (0.0, 21.0), (-3.0, 16.0, 180.0)),
          ("chunk_6", "chunk_c", (-7.0, 4.0), (-16.0, -2.0, -160.0))]


def _layout(card, scale):
    out = [("|root", None, None, 0.0, [], True, None),
           ("wall", "|root", card, 0.0, [], True, None)]
    for name, c, (x, y), _f in CHUNKS:
        out.append((name, "|root", c, 0.5, [], False, (x * scale, y * scale)))
    return out


def _motions(scale):
    def thrown(k):
        out = {}
        for name, _c, _at, (dx, dy, spin) in CHUNKS:
            out[name] = {"vis": 1, "tx": dx * scale * k, "ty": dy * scale * k - 30.0 * k * k, "rz": spin * k}
        return out
    gone = {n: dict(v, vis=0) for n, v in thrown(1.0).items()}
    M = {}
    M["S_1"] = loop(60, [(0, {}), (60, {})])
    # H_1: struck (a hammer, a bump): a shudder (16)
    M["H_1"] = once(16, [(0, {}), (2, {"wall": {"sx": 1.0625, "sy": 0.9375, "tx": 0.4}}, {"ease": "vector"}),
                         (5, {"wall": {"tx": -0.3}}), (8, {"wall": {"tx": 0.2}}), (16, {})])
    # B_1: the burst (Blast): a bulge, then the wall gone in a spray of
    # chunks that fall away out of sight (40, held: nothing showing)
    M["B_1"] = once(40, [(0, {}), (3, {"wall": {"sx": 1.125, "sy": 1.0625}}, {"ease": "vector"}),
                         (4, P({"wall": {"vis": 0}}, thrown(0.0))),
                         (14, P({"wall": {"vis": 0}}, thrown(0.5)), {"ease": "linear"}),
                         (26, P({"wall": {"vis": 0}}, thrown(1.0)), {"ease": "linear"}),
                         (28, P({"wall": {"vis": 0}}, gone)), (40, P({"wall": {"vis": 0}}, gone))])
    # N_1: gone (after the burst: loop 2)
    M["N_1"] = loop(2, [(0, P({"wall": {"vis": 0}}, gone)), (2, P({"wall": {"vis": 0}}, gone))])
    return M


POSES = {"S_1": "standing (loop 60)", "H_1": "struck: a shudder (16)",
         "B_1": "bursting: chunks thrown out, nothing left (40, held)", "N_1": "gone (loop 2)"}

WALL = Rig("c_sl_ice_wall", "ice_wall", _layout("ice_wall_small", 1.0), _motions(1.0),
           camera={"height": 34.0, "width": 48.0, "center": (0.0, -6.0), "res": (384, 272)}, poses=POSES)
WALL_LARGE = Rig("c_sl_ice_wall_large", "ice_wall", _layout("ice_wall_large", 1.6), _motions(1.6),
                 camera={"height": 50.0, "width": 72.0, "center": (0.0, -8.0), "res": (432, 300)}, poses=POSES)
