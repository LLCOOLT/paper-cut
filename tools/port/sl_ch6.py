"""Chapter 6's models (Super Luigi, the tower; docs/super_luigi/ch6.md, New assets
5, 7 and 3), from the art in port/assets/super_luigi/art/nightmare/, goo/ and
king_marron/ (sl_rig.py; docs/LUIGI_ANIMS.md, "Chapter 6's models").

- `c_sl_nightmare`: Crepe joined with the six pieces, battle only: a tall
  shadow 48 units to the top of its compass face (the art is 4 texels a
  unit). Parts: nm (the mover) > body (the hem's middle; binds 0, 1
  cracked), arm_back, arm_front (the shoulders; the arm reaching forward and
  down; rz < 0 raises it), stub_back, stub_front (the torn stumps, hidden:
  shown when an arm is lost), face (the dial's centre; binds 0 the dial, 1
  open (Hatesong), 2 cracked) > needle (rz 0 points up, 90 forward (-x) at
  the target), shard_1..6 (glowing shards of the shatter, hidden), and the
  locators target_body (the face) and target_arm_front / target_arm_back
  (each hand) for the battle's three parts.
- `c_sl_goo_pit` (48 x 48 units) and `c_sl_goo_moat` (192 x 48): the green
  goo, a floor of tiles lying flat (its origin at the middle, at floor
  height), churning between the surface's two frames, bubbles swelling and
  popping, wisps of pale smoke rising (dithered: soft in the art).
- `c_sl_marron`: King Marron restored, about 31 units to the leaf crown:
  marron (the mover) > boots, coat (the hips) > cape (behind), arm_back,
  arm_front (shoulders; rz < 0 swings forward), head (binds 0 rest, 1 blink,
  2 talk, 3 happy) > crown.
"""
import json
import math
import os

import paper_model as PM
from sl_ch5_props import Cards, dither
from sl_rig import ART, P, Rig, blinks, loop, once, talk

ROOT = ("|root", None, None, 0.0, [], True, None)


# -------------------------------------------------------------- nightmare ----
class Rig4(Rig):
    """a Rig whose art is drawn at 4 texels a unit"""

    def parts(self):
        meta = self.meta()
        out = []
        for name, parent, card, z, binds, vis, at in self.layout:
            if card is None:
                out.append(PM.Part(name, parent, None, None, z, at or (0.0, 0.0), (), vis))
                continue
            m = meta[card]
            px, py = m["pivot"]
            w, h = m["size"]
            ax, ay = at if at is not None else m["model_xy"]
            x0, y1 = ax - px / 4.0, ay + py / 4.0
            rect = (x0, y1 - h / 4.0, x0 + w / 4.0, y1)
            out.append(PM.Part(name, parent, self.img(card), rect, z, (ax, ay), [self.img(b) for b in binds], vis))
        return out


SHARDS = [((-6.0, 40.0), (-18.0, 14.0, -500.0)), ((6.0, 44.0), (16.0, 18.0, 460.0)), ((-2.0, 28.0), (-12.0, 4.0, 380.0)),
          ((4.0, 20.0), (14.0, 2.0, -420.0)), ((-8.0, 12.0), (-16.0, -4.0, 300.0)), ((7.0, 8.0), (18.0, -6.0, -340.0))]
NM_ART = "nightmare/qwen"  # cut from Qwen's split of the style model's design (art/tools/cut_nightmare.py)


def _nm_layout():
    meta = json.load(open(os.path.join(ART, NM_ART, "parts.json")))
    an = {k: tuple(v) for k, v in meta["anchors"].items()}
    return [ROOT, ("nm", "|root", None, 0.0, [], True, (0.0, 0.0)),
            ("arm_back", "nm", "arm_back", -0.6, [], True, None),
            ("stub_back", "nm", "stub_back", -0.6, [], False, None),
            ("body", "nm", "body", 0.0, ["body_cracked"], True, None),
            ("arm_front", "nm", "arm_front", 0.6, [], True, None),
            ("stub_front", "nm", "stub_front", 0.6, [], False, None),
            ("face", "nm", "face", 0.8, ["face_open", "face_cracked", "face_narrow"], True, None),
            ("needle", "body", "needle", 1.0, [], True, None),
            ("target_body", "face", None, 0.0, [], True, an["face"]),
            ("target_arm_front", "arm_front", None, 0.0, [], True, an["hand_front"]),
            ("target_arm_back", "arm_back", None, 0.0, [], True, an["hand_back"])] + [
        ("shard_%d" % (k + 1), "|root", "shard_%d" % (k % 3), 1.2, [], False, at) for k, (at, _f) in enumerate(SHARDS)]


def _hover(f, period=60, amp=1.0):
    s = math.sin(2 * math.pi * f / period)
    c = math.cos(2 * math.pi * f / period)
    return {"nm": {"ty": amp * (0.5 + 0.5 * s)}, "body": {"sx": 1.0 + 0.03125 * c, "rz": 1.0 * s},
            "arm_front": {"rz": 4.0 * s}, "arm_back": {"rz": -3.0 * s}, "face": {"rz": -1.5 * s}}


def _spin(end, turns_per_60=2.0, start=0.0):
    """the needle spinning (a "linear" track of rz)"""
    return [(f, {"needle": {"rz": start - 360.0 * turns_per_60 * f / 60.0}}, {"ease": "linear"}) for f in
            range(0, end + 1, 10)]


def _merge_keys(*lists):
    """key lists merged frame by frame (later poses winning per channel)"""
    frames = {}
    for keys in lists:
        for k in keys:
            fr = frames.setdefault(k[0], [{}, {}])
            fr[0] = P(fr[0], k[1])
            if len(k) > 2:
                fr[1].update(k[2])
    return [(f, p, o) if o else (f, p) for f, (p, o) in sorted(frames.items())]


def _idle(end=60, extra=None):
    return _merge_keys([(f, P(_hover(f), extra or {})) for f in range(0, end + 1, 15)], _spin(end))


LOST = {"F": {"arm_front": {"vis": 0}, "stub_front": {"vis": 1}},
        "B": {"arm_back": {"vis": 0}, "stub_back": {"vis": 1}},
        "N": {"arm_front": {"vis": 0}, "stub_front": {"vis": 1}, "arm_back": {"vis": 0}, "stub_back": {"vis": 1}}}
NM = {}
# S_1: idle: hovering, swaying, the needle spinning (loop 60); S_1F / S_1B /
# S_1N with the front, the back or both arms lost
NM["S_1"] = loop(60, _idle())
NM["Z_1"] = NM["S_1"]
for _s, _l in LOST.items():
    NM["S_1" + _s] = loop(60, _idle(extra=_l))
# T_1: talking ("ALL... TOMORROWS... MINE..."): the face swelling on each
# word, the needle twitching (loop 32)
NM["T_1"] = loop(32, [(0, _hover(0)), (6, P(_hover(6), {"face": {"sx": 1.0625, "sy": 1.0625, "tex": 1}, "needle": {"vis": 0}})),
                      (12, P(_hover(12), {"needle": {"vis": 1}})), (22, P(_hover(22), {"face": {"sx": 1.0625, "sy": 1.0625, "tex": 1},
                                                           "needle": {"vis": 0}})), (32, P(_hover(32), {"needle": {"vis": 1}}))])
# W_1, R_1: gliding (the battle's walk; loop 40, 20)
NM["W_1"] = loop(40, _merge_keys([(f, P(_hover(f, 40, 0.6), {"nm": {"rz": 4.0}})) for f in (0, 10, 20, 30, 40)],
                                 _spin(40)))
NM["R_1"] = loop(20, _merge_keys([(f, P(_hover(f, 20, 0.6), {"nm": {"rz": 8.0}})) for f in (0, 5, 10, 15, 20)],
                                 _spin(20, 4.0)))


def _hurt(lost=None):
    return loop(12, [(0, P({"nm": {"tx": 0.0}}, lost or {})),
                     (2, P({"nm": {"tx": 1.5, "rz": -4.0}, "body": {"sx": 0.9375}, "face": {"rz": 10.0},
                            "needle": {"rz": 40.0}}, lost or {}), {"ease": "vector"}),
                     (6, P({"nm": {"tx": 0.6, "rz": -2.0}, "needle": {"rz": -30.0}}, lost or {})),
                     (12, P({"nm": {"tx": 0.0}}, lost or {}))])


NM["D_1"] = _hurt()
for _s, _l in LOST.items():
    NM["D_1" + _s] = _hurt(_l)

# A_1A: Doom Foretold: the needle spinning fast, slowing, and stopping
# pointing forward at its target (40, held); A_1C: the count, the needle
# quivering on the target (loop 20); A_1B: the strike: rearing, both arms
# slammed down on the target (hit at 10; 40)
_POINT = {"needle": {"rz": 90.0}}
NM["A_1A"] = once(40, [(0, P(_hover(0), {"needle": {"rz": 0.0}})),
                       (14, P(_hover(15), {"needle": {"rz": -1080.0}}), {"ease": "linear"}),
                       (28, P(_hover(30), {"needle": {"rz": -1340.0}})),
                       (34, P(_hover(30), {"needle": {"rz": -1356.0}})),
                       (40, P(_hover(30), {"needle": {"rz": -1350.0}, "face": {"sx": 1.0625, "sy": 1.0625}}))])
NM["A_1C"] = loop(20, [(0, P(_hover(0), _POINT)), (5, P(_hover(15), {"needle": {"rz": 93.0}})),
                       (10, P(_hover(30), _POINT)), (15, P(_hover(45), {"needle": {"rz": 87.0}})),
                       (20, P(_hover(60), _POINT))])
_RAISE = {"arm_front": {"rz": -70.0}, "arm_back": {"rz": -60.0}, "body": {"rz": -6.0, "sy": 1.0625},
          "face": {"rz": -8.0}}
_SLAM = {"arm_front": {"rz": 25.0}, "arm_back": {"rz": 20.0}, "body": {"rz": 8.0, "sy": 0.9375}, "nm": {"tx": -6.0},
         "face": {"rz": 10.0}}
NM["A_1B"] = once(40, [(0, _POINT), (7, P(_POINT, _RAISE)), (10, P(_POINT, _SLAM), {"ease": "vector"}),
                       (16, P(_POINT, _SLAM, {"body": {"sy": 0.96875}})), (28, P(_POINT, {"nm": {"tx": -2.0}})),
                       (40, {"needle": {"rz": 90.0}})])
# A_2A: Hatesong: the dial splitting open into a howling mouth, rising (24,
# held); A_2B: the howl, shaking (loop 12)
_HOWL = {"face": {"tex": 1, "sx": 1.125, "sy": 1.125}, "needle": {"vis": 0}, "body": {"sy": 1.0625}, "arm_front": {"rz": -40.0},
         "arm_back": {"rz": -35.0}}
NM["A_2A"] = once(24, [(0, _hover(0)), (8, P(_hover(8), {"face": {"tex": 1, "sx": 0.9375, "sy": 0.9375}, "needle": {"vis": 0}})),
                       (24, P(_HOWL, {"nm": {"ty": 2.0}}))])
NM["A_2B"] = loop(12, [(0, P(_HOWL, {"nm": {"ty": 2.0}})), (3, P(_HOWL, {"nm": {"ty": 2.0, "tx": 0.5}, "face": {"rz": 4.0}})),
                       (6, P(_HOWL, {"nm": {"ty": 2.0}})), (9, P(_HOWL, {"nm": {"ty": 2.0, "tx": -0.5}, "face": {"rz": -4.0}})),
                       (12, P(_HOWL, {"nm": {"ty": 2.0}}))])


# A_3: A Thousand Tomorrows: five quick page-slaps with the front arm, hits
# at 8, 14, 20, 26, 32 (48)
def _slaps():
    keys = [(0, {}), (4, {"arm_front": {"rz": -60.0}, "nm": {"tx": -2.0}})]
    for k, f in enumerate((8, 14, 20, 26, 32)):
        keys += [(f, {"arm_front": {"rz": 20.0}, "nm": {"tx": -4.0}, "body": {"rz": 4.0}}, {"ease": "vector"}),
                 (f + 3, {"arm_front": {"rz": -55.0 + 5 * k}, "nm": {"tx": -4.0}})]
    keys += [(40, {"nm": {"tx": -1.0}}), (48, {})]
    return once(48, keys)


NM["A_3"] = _slaps()
# A_4A: Page Grab: the back arm lunging out and closing on the partner (24,
# held); A_4B: holding them (loop 30); A_4C: letting go (20)
_GRAB = {"arm_back": {"rz": 10.0, "sx": 1.25}, "nm": {"tx": -3.0}, "body": {"rz": 5.0}}
NM["A_4A"] = once(24, [(0, {}), (8, {"arm_back": {"rz": -45.0}}), (16, P(_GRAB, {"arm_back": {"sx": 1.375}}),
                                                                          {"ease": "vector"}), (24, _GRAB)])
NM["A_4B"] = loop(30, [(0, _GRAB), (15, P(_GRAB, {"arm_back": {"rz": 13.0, "sy": 0.9375}})), (30, _GRAB)])
NM["A_4C"] = once(20, [(0, _GRAB), (6, {"arm_back": {"rz": -30.0}}), (20, {})])


# L_1F / L_1B: an arm lost: torn off, flying back and gone, the stump left
# (24, held); G_1F / G_1B: the arm regrowing from the stump, unfolding (30)
def _lose(side):
    arm, stub = "arm_" + {"F": "front", "B": "back"}[side], "stub_" + {"F": "front", "B": "back"}[side]
    return once(24, [(0, {}), (4, {arm: {"rz": -30.0, "tx": 2.0}, "nm": {"tx": 1.0}}),
                     (5, {arm: {"rz": -30.0, "tx": 2.0}, stub: {"vis": 1}}),
                     (14, {arm: {"rz": -200.0, "tx": 14.0, "ty": 10.0}, stub: {"vis": 1}}),
                     (20, {arm: {"rz": -280.0, "tx": 20.0, "ty": -20.0}, stub: {"vis": 1}}),
                     (21, {arm: {"vis": 0}, stub: {"vis": 1}}), (24, {arm: {"vis": 0}, stub: {"vis": 1}})])


def _regrow(side):
    arm, stub = "arm_" + {"F": "front", "B": "back"}[side], "stub_" + {"F": "front", "B": "back"}[side]
    return once(30, [(0, {arm: {"vis": 0}, stub: {"vis": 1}}),
                     (8, {arm: {"vis": 1, "sx": 0.0625, "sy": 0.25}, stub: {"vis": 0}}),
                     (22, {arm: {"sx": 1.125, "sy": 1.0625}}, {"ease": "vector"}), (26, {arm: {"sx": 0.9375}}),
                     (30, {})])


for _s in ("F", "B"):
    NM["L_1" + _s] = _lose(_s)
    NM["G_1" + _s] = _regrow(_s)

# X_1: stunned (a Superguard at the cue): drooping, the needle wobbling (loop 48)
NM["X_1"] = loop(48, [(0, {"nm": {"ty": -1.0, "rz": 6.0}, "arm_front": {"rz": 30.0}, "arm_back": {"rz": 25.0},
                           "needle": {"rz": 30.0}, "face": {"rz": 8.0}}),
                      (24, {"nm": {"ty": -1.0, "rz": 2.0}, "arm_front": {"rz": 28.0}, "arm_back": {"rz": 22.0},
                            "needle": {"rz": -30.0}, "face": {"rz": 4.0}}),
                      (48, {"nm": {"ty": -1.0, "rz": 6.0}, "arm_front": {"rz": 30.0}, "arm_back": {"rz": 25.0},
                            "needle": {"rz": 30.0}, "face": {"rz": 8.0}})])
# E_1: its entrance: unfolding up out of Crepe's crumple, the arms opening
# out, the needle starting to spin (48, held in S_1's first pose)
NM["E_1"] = once(48, [(0, {"nm": {"sy": 0.0625, "sx": 0.5}, "arm_front": {"rz": 40.0}, "arm_back": {"rz": 40.0}}),
                      (20, {"nm": {"sy": 1.125, "sx": 0.875}, "arm_front": {"rz": -40.0}, "arm_back": {"rz": -40.0},
                            "needle": {"rz": -360.0}}, {"ease": "vector"}),
                      (28, {"nm": {"sy": 0.9375, "sx": 1.0625}, "needle": {"rz": -540.0}}),
                      (36, {"nm": {"sy": 1.0}, "needle": {"rz": -720.0}}), (48, P(_hover(0), {"needle": {"rz": -720.0}}))])


# C_1: Smite lands: the crack: light breaking through body and face, a
# shudder (40, held); C_2: the shatter: it bursts into shards of light that
# fly out and vanish, nothing left (60, held)
def _shards(k):
    out = {}
    for i, (_at, (dx, dy, spin)) in enumerate(SHARDS):
        out["shard_%d" % (i + 1)] = {"vis": 1 if 0 < k < 1 else 0, "tx": dx * k, "ty": dy * k - 6.0 * k * k,
                                     "rz": spin * k, "sx": 1.0 - 0.5 * k, "sy": 1.0 - 0.5 * k}
    return out


_CRACK = {"body": {"tex": 1}, "face": {"tex": 2}, "needle": {"rz": 160.0}}
NM["C_1"] = once(40, [(0, {}), (2, P(_CRACK, {"nm": {"tx": 1.5, "sy": 0.9375}}), {"ease": "vector"}),
                      (6, P(_CRACK, {"nm": {"tx": -1.0}})), (10, P(_CRACK, {"nm": {"tx": 0.6}})),
                      (14, P(_CRACK, {"nm": {"tx": -0.4}})), (20, _CRACK),
                      (30, P(_CRACK, {"nm": {"sy": 1.03125}, "arm_front": {"rz": 20.0}, "arm_back": {"rz": 20.0}})),
                      (40, P(_CRACK, {"arm_front": {"rz": 25.0}, "arm_back": {"rz": 25.0}}))])
_GONE = {"nm": {"vis": 0}}
NM["C_2"] = once(60, [(0, P(_CRACK, {"arm_front": {"rz": 25.0}, "arm_back": {"rz": 25.0}}, _shards(0.0))),
                      (4, P(_CRACK, {"nm": {"sx": 1.125, "sy": 1.0625}}, _shards(0.0))),
                      (6, P(_GONE, _shards(0.05))), (24, P(_GONE, _shards(0.6)), {"ease": "linear"}),
                      (40, P(_GONE, _shards(0.99))), (41, P(_GONE, _shards(1.0))), (60, P(_GONE, _shards(1.0)))])

NM_POSES = {
    "S_1": "idle: hovering, swaying, the needle spinning (loop 60); `S_1F` `S_1B` `S_1N` the same with the front, the "
           "back or both arms lost (the stumps showing)",
    "T_1": "talking: the face swelling on each word (loop 32)",
    "W_1": "gliding (loop 40)", "R_1": "gliding fast (loop 20)",
    "D_1": "hurt (loop 12); `D_1F` `D_1B` `D_1N` with arms lost",
    "A_1A": "Doom Foretold: the needle spinning, slowing, stopping on its target, forward (40, held)",
    "A_1C": "the count: the needle quivering on the target (loop 20)",
    "A_1B": "the strike: rearing, both arms slammed down on the target (hit at 10; 40)",
    "A_2A": "Hatesong: the dial splitting open into a howling mouth, rising (24, held)",
    "A_2B": "the howl, shaking (loop 12)",
    "A_3": "A Thousand Tomorrows: five quick page-slaps (hits at 8, 14, 20, 26, 32; 48)",
    "A_4A": "Page Grab: the back arm lunging out, closing on the partner (24, held)",
    "A_4B": "holding them (loop 30)", "A_4C": "letting go (20)",
    "L_1F": "the front arm lost: torn off, flung back, gone, the stump left (24, held; `L_1B` the back arm)",
    "G_1F": "the front arm regrowing from the stump (30; `G_1B` the back arm)",
    "X_1": "stunned: drooping, the needle wobbling (loop 48)",
    "E_1": "its entrance: unfolding up out of Crepe's crumple, the needle starting (48, held)",
    "C_1": "Smite lands: cracking, light breaking through, a shudder (40, held)",
    "C_2": "the shatter: bursting into shards of light that fly out and vanish (60, held: nothing left)",
}
# A paper robe doesn't squash (the Shadow Queen's form never does, ART_PLAN): only its entrance keeps a stretch.
for _n, _m in NM.items():
    if _n not in ("E_1",):
        _m["keys"] = [(k[0], {g: {c: v for c, v in ch.items() if not (g in ("body", "nm") and c in ("sx", "sy"))}
                              for g, ch in k[1].items()}) + tuple(k[2:]) for k in _m["keys"]]

# The game's bosses strike in 6 to 16 frames (ART_PLAN): wind-ups reach their pose sooner and hold, recoveries
# are short. The hit frames stay: the battle scripts wait on them.
def _fast(m, hold_from=None, hit=0, end=None):
    k = m["end"]
    out = dict(m)
    if hold_from is not None:  # a wind-up: squeezed to hold_from frames (the script's wait holds the last pose)
        out["end"] = hold_from
        out["keys"] = [(int(round(x[0] * hold_from / float(k))),) + tuple(x[1:]) for x in m["keys"]]
    else:  # after the hit: squeezed into hit..end
        out["end"] = end
        out["keys"] = [((hit + (x[0] - hit) * (end - hit) // (k - hit)) if x[0] > hit else x[0],) + tuple(x[1:]) for x in m["keys"]]
    seen, keys = set(), []
    for x in out["keys"]:
        if x[0] not in seen:
            keys.append(x)
            seen.add(x[0])
    out["keys"] = keys
    return out


NM["A_1A"] = _fast(NM["A_1A"], hold_from=16)
NM["A_1B"] = _fast(NM["A_1B"], hit=10, end=20)
NM["A_2A"] = _fast(NM["A_2A"], hold_from=10)
NM["A_4A"] = _fast(NM["A_4A"], hold_from=12)
NM["A_4C"] = _fast(NM["A_4C"], hold_from=10)

# The cut figure faces the camera, its far arm on the +x side: that arm's turns are mirrored (rz < 0 still raises it
# for the motions above), and Page Grab reaches with the near arm, toward the party on -x.
def _mirror_back(m, grab=False):
    out = dict(m)

    def fix(pose):
        pose = {g: dict(ch) for g, ch in pose.items()}
        if grab and "arm_back" in pose:
            pose["arm_front"] = dict(pose.get("arm_front", {}), **pose.pop("arm_back"))
        elif "arm_back" in pose and "rz" in pose["arm_back"]:
            pose["arm_back"]["rz"] = -pose["arm_back"]["rz"]
        return pose
    out["keys"] = [(k[0], fix(k[1])) + tuple(k[2:]) for k in m["keys"]]
    return out


for _n in list(NM):
    NM[_n] = _mirror_back(NM[_n], grab=_n.startswith("A_4"))
# Doom Foretold and its count: the eyes narrowed to slits (face 3) as the needle finds its mark
NM["A_1A"]["keys"] = [k if k[0] < NM["A_1A"]["end"] // 2 else (k[0], P(k[1], {"face": {"tex": 3}})) + tuple(k[2:])
                      for k in NM["A_1A"]["keys"]]
NM["A_1C"]["keys"] = [(k[0], P(k[1], {"face": {"tex": 3}})) + tuple(k[2:]) for k in NM["A_1C"]["keys"]]

NIGHTMARE = Rig("c_sl_nightmare", NM_ART, _nm_layout(), NM, poses=NM_POSES,
                 camera={"height": 64.0, "width": 72.0, "center": (-8.0, -6.0), "res": (432, 384)})

# -------------------------------------------------------------------- goo ----
TILE = 24.0


def _goo(name, nx, ny):
    """a floor of nx x ny tiles lying flat (the `floor` locator turned rx 90:
    its y is the floor's depth), with three bubbles and two wisps over it"""
    w, d = nx * TILE, ny * TILE
    layout = [("|root", None, None, None, 0.0, (0.0, 0.0), True, False),
              ("floor", "|root", None, None, 0.0, (0.0, 0.0), True, False)]
    tiles = []
    for i in range(nx):
        for j in range(ny):
            x0, y0 = -w / 2 + i * TILE, -d / 2 + j * TILE
            n = "tile_%d_%d" % (i, j)
            tiles.append(n)
            layout.append((n, "floor", [("goo", "goo_surface_0"), ("goo", "goo_surface_1")],
                           (x0, y0, x0 + TILE, y0 + TILE), 0.0, (x0 + TILE / 2, y0 + TILE / 2), True, False))
    spots = [(-w * 0.3, 0.0), (w * 0.05, 0.5), (w * 0.3, 0.2)]
    bubbles = []
    for k, (x, y) in enumerate(spots):
        n = "bubble_%d" % (k + 1)
        bubbles.append(n)
        layout.append((n, "|root", [("goo", "goo_bubble_%d" % b) for b in range(3)], (x - 2.0, y, x + 2.0, y + 4.0),
                       0.5 + 0.1 * k, (x, y), False, False))
    wisps = []
    for k, x in enumerate((-w * 0.15, w * 0.2)):
        n = "smoke_%d" % (k + 1)
        wisps.append(n)
        layout.append((n, "|root", [("goo", "goo_smoke")], (x - 4.0, 0.0, x + 4.0, 8.0), 0.8 + 0.1 * k, (x, 0.0),
                       True, True))
    flat = {"floor": {"rx": 90.0}}
    end = 120
    tracks = {t: {"tex": [(0, 0, "step"), (30, 1, "step"), (60, 0, "step"), (90, 1, "step")]} for t in tiles}
    for k, n in enumerate(bubbles):
        s = 20 + 33 * k
        tracks[n] = {"vis": [(0, 0, "step"), (s, 1, "step"), (s + 24, 0, "step")],
                     "tex": [(0, 0, "step"), (s + 12, 1, "step"), (s + 20, 2, "step")]}
    keys = []
    for f in range(0, end + 1, 20):
        pose = dict(flat)
        for k, n in enumerate(wisps):
            ph = ((f / float(end)) + 0.5 * k) % 1.0
            pose[n] = {"ty": 10.0 * ph, "sx": 0.6 + 0.8 * ph, "sy": 0.6 + 0.8 * ph, "tx": 2.0 * math.sin(6.28 * ph)}
        keys.append((f, pose, {"ease": "linear"}))
    M = {"S_1": loop(end, keys, tracks=tracks)}
    M["Z_1"] = M["S_1"]
    poses = {"S_1": "churning: the surface's two frames every 30, bubbles swelling and popping, wisps rising "
                    "(loop 120)"}
    return Cards(name, layout, M, poses, {"height": d + 16.0, "width": w + 8.0, "center": (0.0, -d / 2 - 4.0),
                                          "res": (int(4 * (w + 8)), int(4 * (d + 16)))})


GOO_PIT = _goo("c_sl_goo_pit", 2, 2)
GOO_MOAT = _goo("c_sl_goo_moat", 8, 2)

# ----------------------------------------------------------------- marron ----
MA_LAYOUT = [ROOT, ("marron", "|root", None, 0.0, [], True, (0.0, 0.0)),
             ("cape", "marron", "cape", -0.8, [], True, None),
             ("arm_back", "marron", "arm_back", -0.4, [], True, None),
             ("boots", "marron", "boots", -0.2, [], True, None),
             ("coat", "marron", "coat", 0.0, [], True, None),
             ("arm_front", "coat", "arm_front", 0.5, [], True, None),
             ("head", "coat", "head", 0.3, ["head_blink", "head_talk", "head_happy"], True, None),
             ("crown", "head", "crown", 0.4, [], True, None)]


def _breath(f, period=96):
    s = math.sin(2 * math.pi * f / period)
    return {"coat": {"sy": 1.0 + 0.03125 * (0.5 + 0.5 * s)}, "head": {"rz": 1.2 * s}, "cape": {"rz": -1.0 * s},
            "arm_front": {"rz": -4.0 + 1.5 * s}, "arm_back": {"rz": -3.0 - 1.5 * s}}


MM = {}
MM["S_1"] = loop(96, [(f, _breath(f)) for f in (0, 24, 48, 72, 96)], tracks=blinks("head", (40, 88)))
MM["Z_1"] = MM["S_1"]
MM["T_1"] = loop(32, [(0, {"arm_front": {"rz": -25.0}, "head": {"rz": -2.0}}),
                      (8, {"arm_front": {"rz": -40.0}, "head": {"rz": 2.0}}),
                      (16, {"arm_front": {"rz": -30.0}, "head": {"rz": -1.0}}),
                      (24, {"arm_front": {"rz": -45.0}, "head": {"rz": 3.0}}),
                      (32, {"arm_front": {"rz": -25.0}, "head": {"rz": -2.0}})], tracks=talk("head", 32, every=7))


def _walk(f, period, bob=0.5, sway=2.5, swing=14.0):
    ph = 2 * math.pi * f / period
    s, c = math.sin(ph), math.cos(ph)
    return {"marron": {"ty": bob * abs(s)}, "coat": {"rz": sway * s}, "boots": {"tx": 1.0 * c},
            "cape": {"rz": -3.0 - 3.0 * abs(s)}, "arm_front": {"rz": swing * c}, "arm_back": {"rz": -swing * c},
            "head": {"rz": -0.4 * sway * s}}


MM["W_1"] = loop(48, [(f, _walk(f, 48)) for f in (0, 12, 24, 36, 48)])
MM["R_1"] = loop(24, [(f, P(_walk(f, 24, 1.0, 4.0, 30.0), {"coat": {"rz": 8.0}, "cape": {"rz": -14.0}}))
                      for f in (0, 6, 12, 18, 24)])
# WV_1: waving goodbye: the front arm raised up and out in front of him,
# drawn behind his face (tz), the hand above his brow, rocking (loop 24)
_WAVE = {"head": {"rz": -6.0, "tx": 0.6, "tex": 3}, "coat": {"rz": -3.0}, "arm_front": {"tz": -0.4}}  # the arm behind his face
MM["WV_1"] = loop(24, [(0, P(_WAVE, {"arm_front": {"rz": -92.0}})), (12, P(_WAVE, {"arm_front": {"rz": -114.0}})),
                       (24, P(_WAVE, {"arm_front": {"rz": -92.0}}))])
# H_1: the embrace with Eclair: both arms forward and in, eyes happy, the
# head tipped down (loop 60)
_HOLD = {"arm_front": {"rz": -60.0}, "arm_back": {"rz": -62.0}, "head": {"rz": 8.0, "tex": 3}, "coat": {"rz": 4.0}}
MM["H_1"] = loop(60, [(0, _HOLD), (30, P(_HOLD, {"coat": {"rz": 5.0, "sy": 1.03125}, "head": {"rz": 10.0, "tex": 3}})),
                      (60, _HOLD)])
# U_1: restored: where the husk fell away, blinking, coming to, looking
# down at his hands, then up (90, held)
MM["U_1"] = once(90, [(0, {"marron": {"sy": 0.9375}, "head": {"rz": 12.0, "tex": 1}, "arm_front": {"rz": 10.0},
                           "arm_back": {"rz": 8.0}}),
                      (20, {"head": {"rz": 10.0, "tex": 1}, "arm_front": {"rz": 6.0}}),
                      (30, {"head": {"rz": 6.0, "tex": 0}}), (34, {"head": {"rz": 6.0, "tex": 1}}),
                      (38, {"head": {"rz": 6.0, "tex": 0}}),
                      (54, {"arm_front": {"rz": -55.0}, "arm_back": {"rz": -50.0}, "head": {"rz": 14.0}}),
                      (72, {"arm_front": {"rz": -50.0}, "arm_back": {"rz": -45.0}, "head": {"rz": 12.0}}),
                      (84, {"head": {"rz": -4.0}}), (90, {"head": {"rz": -2.0}})])
# B_1: a bow of thanks (40, held at the top again)
MM["B_1"] = once(40, [(0, {}), (14, {"coat": {"rz": 18.0}, "head": {"rz": 10.0, "tex": 1}, "arm_front": {"rz": -30.0},
                                     "cape": {"rz": 8.0}}),
                      (26, {"coat": {"rz": 18.0}, "head": {"rz": 10.0, "tex": 1}, "arm_front": {"rz": -30.0},
                            "cape": {"rz": 8.0}}), (40, {})])
MA_POSES = {"S_1": "stand, blinking (loop 96)", "T_1": "talking, a hand raised (loop 32)", "W_1": "walking (loop 48)",
            "R_1": "running to her (loop 24)", "WV_1": "waving goodbye (loop 24)",
            "H_1": "the embrace with Eclair: arms forward and in, eyes happy (loop 60)",
            "U_1": "restored: coming to where the husk fell away, looking at his hands, then up (90, held)",
            "B_1": "a bow of thanks (40)"}
MARRON = Rig("c_sl_marron", "king_marron", MA_LAYOUT, MM, poses=MA_POSES,
             camera={"height": 40.0, "width": 40.0, "center": (0.0, -3.0), "res": (320, 320)})

# ------------------------------------------------------------------ torch ----
# The Choir's two more torches (port/assets/super_luigi/art/ch6_torch/): a post, the cup (binds 0 lit,
# 1 dark) and the flame (binds 0, 1: it leans left, right), beside jin_07's own four.
TORCH_LAYOUT = [ROOT, ("post", "|root", "torch_post", 0.0, [], True, None),
                ("cup", "|root", "torch_cup", 0.2, ["torch_cup_dark"], True, (0.0, 52.4)),
                ("flame", "|root", "torch_flame_0", 0.4, ["torch_flame_1"], True, (0.0, 55.6))]
TM = {}
TM["S_1"] = loop(16, [(0, {}), (16, {})], tracks={"flame": {"tex": [(0, 0, "step"), (8, 1, "step")]}})
TM["S_2"] = loop(60, [(0, {"flame": {"vis": 0}, "cup": {"tex": 1}}), (60, {"flame": {"vis": 0}, "cup": {"tex": 1}})])
TORCH_POSES = {"S_1": "lit: the flame flickering, its two frames every 8 (loop 16)",
               "S_2": "dark: no flame, the cup cold (loop 60)"}
TORCH = Rig("c_sl_torch", "ch6_torch", TORCH_LAYOUT, TM, poses=TORCH_POSES,
            camera={"height": 72.0, "width": 24.0, "center": (0.0, -2.0), "res": (192, 576)})
