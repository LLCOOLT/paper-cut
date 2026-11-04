"""Chapter 5's props (Super Luigi, Grimble Forest and the Luff ruins;
docs/super_luigi/ch5.md, New assets 5-8), from the art in
port/assets/super_luigi/art/lenses/, murals/, look_edges/ and ch5_props/
(sl_rig.py; docs/LUIGI_ANIMS.md, "Chapter 5's props"). Each a model of its
own, put as an NPC or a map object's model.

- `c_sl_socket_moon`, `c_sl_socket_sun`, `c_sl_socket_tear`: a stone stand
  round the dais, its origin on the ground at its middle: `socket`, `lens`
  (hidden until set in; its foot in the socket's cap at (0, 6.6)) and `beam`
  (hidden: the light rising from the lens, 8 x 32 units from (0, 9) up;
  scale its sy for a taller room).
- `c_sl_mural`: a mural, a card 100 x 50 units (the wall's own grain, 2.56
  texels a unit), its origin on the bay's floor, the card from y 45 to 95
  (centred at 70, murals/README.md): binds 0..5 the six panels, 6 the sixth
  broken by Jerry's Blast. A pose per panel. Placed in hei_09's six bays at
  x -400, -240, -80, 80, 240, 400, z -103.5.
- `c_sl_look_edge_25`, `_50`, `_100`, `_200`: Look's glowing edge strips for
  the Room of Nothing, that many units long along x, 2 units wide, centred on
  their origin, with a round cap at each end; `_v` versions stand upright
  (along y, from the origin up). Turn an NPC 90 degrees about y for an edge
  along z. Binds 0 the glow, 1 the pulse (as the Look lands).
- `c_sl_gear_gate`: the Luff gear-gate, its origin on the ground at the
  arch's middle: `gate_teeth` (behind the arch, hung from `lift` at the
  arch's crown: the teeth draw up into the stone as it rises, a notch of 3
  units per push), `gate_frame`, and `gear` on the wall to its left (binds 0
  jammed with grit, 1 clean after the Tune-up).
- `c_sl_signpost`: the Luff signpost, its origin on the ground: `post`,
  `pointer` (on its axle at (0, 10.4): drooping while broken, swinging round
  to point when fixed), `crank` (on the axle, in front).

The beams and the glow strips are soft light in the art; the model's
textures have 1-bit alpha (CMPR, as every model's), so their soft alpha is
ordered-dithered (a 4x4 Bayer screen), which reads as translucent light at
the game's distance.
"""
import json
import math
import os

import paper_model as PM
from sl_rig import ART, PX, P, Rig, loop, once

ROOT = ("|root", None, None, 0.0, [], True, None)
_BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def dither(rows):
    """soft alpha to 1-bit by a 4x4 ordered screen (colour kept, full where shown)"""
    out = []
    for y, row in enumerate(rows):
        out.append([(r, g, b, 255) if a > (_BAYER[y & 3][x & 3] + 0.5) * 16 else (0, 0, 0, 0)
                    for x, (r, g, b, a) in enumerate(row)])
    return out


def _png(folder, card, soft=False):
    rows = PM.read_png(os.path.join(ART, folder, card + ".png"))
    return PM.pad8(dither(rows) if soft else rows)


class Cards:
    """a model of cards placed by hand: [(name, parent, [(folder, card)...] or
    None, rect, z, pivot, visible, soft)]"""

    def __init__(self, name, layout, motions, poses, camera):
        self.NAME, self.layout, self.MOTIONS, self.POSES, self.CAMERA = name, layout, motions, poses, camera

    def parts(self):
        out = []
        for name, parent, imgs, rect, z, pivot, vis, soft in self.layout:
            if not imgs:
                out.append(PM.Part(name, parent, None, None, z, pivot, (), vis))
                continue
            ims = [_png(f, c, soft) for f, c in imgs]
            out.append(PM.Part(name, parent, ims[0], rect, z, pivot, ims[1:], vis))
        return out

    def build(self, anims=()):
        anims = list(anims)
        if not any(x.name == "Z_1" for x in anims):
            anims.insert(0, PM.still("Z_1"))
        return PM.build(self.NAME, self.parts(), anims)


# ---------------------------------------------------------- lens sockets ----
LENS_AT = (0.0, 6.6)
BEAM_FOOT = (0.0, 9.0)


class Socket(Rig):
    """a socket's Rig, its beam's card dithered"""

    def img(self, card):
        return _png(self.art, card, soft=card.startswith("beam_"))


def _socket(kind):
    layout = [ROOT, ("socket", "|root", "socket_" + kind, 0.0, [], True, None),
              ("lens", "|root", "lens_" + kind, 0.3, [], False, LENS_AT),
              ("beam", "|root", "beam_" + kind, 0.5, [], False, BEAM_FOOT)]
    off = {"lens": {"vis": 0}, "beam": {"vis": 0, "sy": 0.0625}}
    set_ = {"lens": {"vis": 1}, "beam": {"vis": 0, "sy": 0.0625}}
    lit = {"lens": {"vis": 1}, "beam": {"vis": 1, "sy": 1.0}}
    M = {}
    M["S_1"] = loop(60, [(0, off), (60, off)])
    # P_1: the lens set in: lowered into the cap, a click, a settle (30, held)
    M["P_1"] = once(30, [(0, P(set_, {"lens": {"ty": 5.0}})), (16, P(set_, {"lens": {"ty": 0.0}}), {"ease": "vector"}),
                         (18, P(set_, {"lens": {"sy": 0.875, "sx": 1.0625}, "socket": {"sy": 0.96875}})),
                         (22, P(set_, {"lens": {"sy": 1.0625}})), (30, set_)])
    M["S_2"] = loop(60, [(0, set_), (60, set_)])
    # L_1: lit: the beam shooting up from the lens to the ceiling (40, held,
    # pulsing); L_2: shining (loop 30)
    M["L_1"] = once(40, [(0, P(set_, {"beam": {"vis": 1, "sy": 0.0625, "sx": 0.5}})),
                         (12, P(lit, {"beam": {"sy": 1.0, "sx": 1.25}}), {"ease": "vector"}),
                         (20, P(lit, {"beam": {"sx": 0.875}})), (30, P(lit, {"beam": {"sx": 1.0625}})), (40, lit)])
    M["L_2"] = loop(30, [(0, lit), (15, P(lit, {"beam": {"sx": 1.125}})), (30, lit)])
    poses = {"S_1": "the empty socket (loop 60)", "P_1": "the lens set in: lowered, a click, a settle (30, held)",
             "S_2": "the lens in place (loop 60)", "L_1": "lit: the beam shooting up to the ceiling (40, held)",
             "L_2": "shining, the beam pulsing (loop 30)"}
    return Socket("c_sl_socket_" + kind, "lenses", layout, M, poses=poses,
                  camera={"height": 48.0, "width": 24.0, "center": (0.0, -2.0), "res": (192, 384)})


SOCKET_MOON, SOCKET_SUN, SOCKET_TEAR = _socket("moon"), _socket("sun"), _socket("tear")

# ---------------------------------------------------------------- murals ----
MURAL_W, MURAL_H, MURAL_Y = 100.0, 50.0, 70.0
PANELS = ["mural_%d" % k for k in range(1, 7)] + ["mural_6_broken"]
_MRECT = (-MURAL_W / 2, MURAL_Y - MURAL_H / 2, MURAL_W / 2, MURAL_Y + MURAL_H / 2)
MM = {}
for _k in range(6):
    MM["M_%d" % (_k + 1)] = loop(60, [(0, {"mural": {"tex": _k}}), (60, {"mural": {"tex": _k}})])
MM["M_6B"] = loop(60, [(0, {"mural": {"tex": 6}}), (60, {"mural": {"tex": 6}})])
MM["S_1"] = MM["M_1"]
# B_1: the Blast on the sixth: a shudder, then the hole (20, held broken)
MM["B_1"] = once(20, [(0, {"mural": {"tex": 5}}), (2, {"mural": {"tex": 5, "tx": 0.8}}), (4, {"mural": {"tex": 5, "tx": -0.8}}),
                      (5, {"mural": {"tex": 6, "tx": 0.5, "sx": 1.03125, "sy": 1.03125}}), (10, {"mural": {"tex": 6}}),
                      (20, {"mural": {"tex": 6}})])
MURAL = Cards("c_sl_mural", [("|root", None, None, None, 0.0, (0.0, 0.0), True, False),
                             ("mural", "|root", [("murals", p) for p in PANELS], _MRECT, 0.0, (0.0, MURAL_Y), True,
                              False)], MM,
              dict({"M_%d" % (k + 1): "panel %d (loop 60)" % (k + 1) for k in range(6)},
                   M_6B="the sixth, broken through by the Blast (loop 60)", S_1="as M_1",
                   B_1="the Blast on the sixth: a shudder, then the hole (20, held)"),
              {"height": 60.0, "width": 110.0, "center": (0.0, 40.0), "res": (440, 240)})

# ------------------------------------------------------------- look edges ----
GLOW = [("look_edges", "edge_glow"), ("look_edges", "edge_glow_pulse")]
CAP = [("look_edges", "edge_glow_end")]
EM = {"S_1": loop(60, [(0, {}), (60, {})])}
# L_1: the Look landing: the pulse drawing for 8 frames, then the glow (40, held)
_PULSE = {"strip": {"tex": 1, "sy": 1.25}, "cap_a": {"sx": 1.25, "sy": 1.25}, "cap_b": {"sx": 1.25, "sy": 1.25}}
EM["L_1"] = once(40, [(0, P(_PULSE, {"strip": {"sy": 0.5}})), (4, _PULSE), (8, _PULSE),
                      (9, {"strip": {"tex": 0, "sy": 1.0}}), (40, {"strip": {"tex": 0}})])
EM["Z_1"] = EM["S_1"]
EDGE_POSES = {"S_1": "glowing (loop 60)",
              "L_1": "the Look landing: the brighter, wider pulse for 8 frames, then the glow (40, held); the fade "
                     "over the last second of the 8 is the NPC's alpha (the story code's)"}


def _edge(length, upright=False):
    h = 1.0  # half the strip's width
    if upright:
        strip, a, b, piv = (-h, 0.0, h, length), (0.0, 0.0), (0.0, length), (0.0, length / 2)
        rot = {"strip": {"rz": 0.0}}
    else:
        strip, a, b, piv = (-length / 2, -h, length / 2, h), (-length / 2, 0.0), (length / 2, 0.0), (0.0, 0.0)
        rot = None
    layout = [("|root", None, None, None, 0.0, (0.0, 0.0), True, False)]
    if upright:  # the strip's texture runs along u (x): an upright strip is the flat one turned 90 about its middle
        layout.append(("turn", "|root", None, None, 0.0, piv, True, False))
        layout.append(("strip", "turn", GLOW, (-length / 2, length / 2 - h, length / 2, length / 2 + h), 0.2, piv,
                       True, True))
    else:
        layout.append(("strip", "|root", GLOW, strip, 0.2, piv, True, True))
    for nm, (x, y) in (("cap_a", a), ("cap_b", b)):
        layout.append((nm, "|root", CAP, (x - h, y - h, x + h, y + h), 0.25, (x, y), True, True))
    motions = EM
    if upright:
        motions = {k: dict(v, keys=[(kk[0], P(kk[1], {"turn": {"rz": 90.0}}),) + tuple(kk[2:]) for kk in v["keys"]])
                   for k, v in EM.items()}
    name = "c_sl_look_edge_%d%s" % (length, "_v" if upright else "")
    cam = {"height": (length + 4.0) if upright else 8.0, "width": 8.0 if upright else length + 4.0,
           "center": (0.0, -2.0) if upright else (0.0, -4.0),
           "res": (64, 256) if upright else (256, 64)}
    return Cards(name, layout, motions, EDGE_POSES, cam)


EDGES = {"%d%s" % (n, "_V" if v else ""): _edge(n, v) for n in (25, 50, 100, 200) for v in (False, True)}
for _k, _m in EDGES.items():
    globals()["EDGE_" + _k] = _m

# --------------------------------------------------------------- gear gate ----
CROWN = (0.0, 16.5)
GEAR_AT = (-13.0, 10.0)
GATE_LAYOUT = [ROOT, ("lift", "|root", None, -0.4, [], True, CROWN),
               ("gate_teeth", "lift", "gate_teeth", -0.4, [], True, None),
               ("gate_frame", "|root", "gate_frame", 0.0, [], True, None),
               ("gear", "|root", "gear_jammed", 0.2, ["gear"], True, GEAR_AT)]
TEETH_H = 17.0


def _notch(k, gear_rz=None, tex=0):
    """the gate risen k notches (3 units each; 5 is fully open, the teeth
    drawn up into the arch)"""
    sy = max(0.0625, (TEETH_H - 3.0 * k) / TEETH_H) if k < 5 else 0.0625
    return {"lift": {"sy": sy}, "gate_teeth": {"vis": 0 if k >= 5 else 1},
            "gear": {"rz": -45.0 * k if gear_rz is None else gear_rz, "tex": tex}}


GM = {}
GM["S_1"] = loop(60, [(0, _notch(0)), (60, _notch(0))])
GM["S_2"] = loop(60, [(0, _notch(0, tex=1)), (60, _notch(0, tex=1))])
GM["J_1"] = once(16, [(0, _notch(0)), (3, _notch(0, gear_rz=-6.0)), (6, _notch(0, gear_rz=2.0)), (9, _notch(0, gear_rz=-3.0)),
                      (16, _notch(0))])
for _k in range(1, 6):
    # P_k: a push of the clean gear: an eighth of a turn, the gate up to notch k (20, held)
    GM["P_%d" % _k] = once(20, [(0, _notch(_k - 1, tex=1)), (14, _notch(_k, tex=1), {"ease": "vector"}),
                                (17, P(_notch(_k, tex=1), {"lift": {"sy": _notch(_k)["lift"]["sy"] * 1.03}})),
                                (20, _notch(_k, tex=1))])
    GM["N_%d" % _k] = loop(60, [(0, _notch(_k, tex=1)), (60, _notch(_k, tex=1))])


def _fall(k):
    """D_k: the gate slamming back down from notch k, the gear spinning back (16, held shut)"""
    return once(16, [(0, _notch(k, tex=1)), (8, _notch(0, tex=1), {"ease": "vector"}),
                     (10, P(_notch(0, tex=1), {"gate_frame": {"sy": 0.96875}})), (16, _notch(0, tex=1))])


for _k in range(1, 5):
    GM["D_%d" % _k] = _fall(_k)
GATE_POSES = {"S_1": "shut, the gear jammed with grit (loop 60)", "J_1": "the jammed gear shuddering, stuck (16)",
              "S_2": "shut, the gear clean (loop 60)",
              "P_1": "a push: the gear an eighth of a turn, the teeth up a notch (20, held; P_2..P_5 from the notch "
                     "below; P_5 opens it)",
              "N_1": "at notch 1 (loop 60; N_2..N_4, N_5 open)",
              "D_1": "falling back shut from notch 1 (16, held; D_2..D_4 from higher)"}
GEAR_GATE = Rig("c_sl_gear_gate", "ch5_props", GATE_LAYOUT, GM, poses=GATE_POSES,
                camera={"height": 26.0, "width": 34.0, "center": (-3.0, -2.0), "res": (272, 208)})

# --------------------------------------------------------------- signpost ----
AXLE = (0.0, 10.4)
POST_LAYOUT = [ROOT, ("post", "|root", "signpost", 0.0, [], True, None),
               ("pointer", "|root", "signpost_pointer", 0.2, [], True, AXLE),
               ("crank", "|root", "signpost_crank", 0.4, [], True, AXLE)]
_BROKEN = {"pointer": {"rz": -100.0}}
SM = {}
SM["S_1"] = loop(80, [(0, _BROKEN), (40, {"pointer": {"rz": -97.0}}), (80, _BROKEN)])
# F_1: fixed (Torque's Tune-up): the crank wound round twice, the pointer
# hauled up and swinging round to point, overshooting and settling (60, held)
SM["F_1"] = once(60, [(0, _BROKEN), (20, {"crank": {"rz": -360.0}, "pointer": {"rz": -60.0}}, {"ease": "linear"}),
                      (36, {"crank": {"rz": -720.0}, "pointer": {"rz": 12.0}}, {"ease": "linear"}),
                      (42, {"crank": {"rz": -720.0}, "pointer": {"rz": -6.0}}),
                      (48, {"crank": {"rz": -720.0}, "pointer": {"rz": 3.0}}), (60, {"crank": {"rz": -720.0}})])
SM["S_2"] = loop(60, [(0, {}), (60, {})])
SM["T_1"] = loop(30, [(0, P(_BROKEN, {"crank": {"rz": 0.0}})), (30, P(_BROKEN, {"crank": {"rz": -360.0}}))])
POST_POSES = {"S_1": "broken: the pointer drooping (loop 80)", "T_1": "the crank turning, still broken (loop 30)",
              "F_1": "fixed: the crank wound, the pointer swinging round to point (60, held)",
              "S_2": "fixed, pointing (loop 60)"}
SIGNPOST = Rig("c_sl_signpost", "ch5_props", POST_LAYOUT, SM, poses=POST_POSES,
               camera={"height": 20.0, "width": 24.0, "center": (2.0, -2.0), "res": (240, 200)})
