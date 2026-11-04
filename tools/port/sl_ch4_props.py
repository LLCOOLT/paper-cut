"""Chapter 4's props (Super Luigi, the Drama Slam; docs/super_luigi/ch4.md, New
assets 3-6), from the art in port/assets/super_luigi/art/ch4_props/,
ch4_sets/ and ch4_fans/ (sl_rig.py; docs/LUIGI_ANIMS.md, "Chapter 4's
props"). Each a model of its own: an NPC, or the battle stage's prop
(`BattleStageObjectData`'s model) for the sets and the chandelier.

- `c_sl_fiery_hat`: the Fiery Hat, its origin the brim's middle (set it on
  Jerry's top, in front of his fuse cap): `hat` > `flames` (on the crown's
  top, binds 0 and 1 the two flame frames, flickering).
- `c_sl_chandelier`: its origin on the floor under it; the chandelier hangs
  from its top ring at y 44 on a chain of four cards up to y 76 (the
  ceiling), `chain_1`..`4`; `lamp` (the mover) > `chandelier`, and
  `shard_1`..`6` (hidden, thrown out in the crash).
- `c_sl_set_road`, `c_sl_set_ballroom`, `c_sl_set_volcano`: the three acts'
  painted backdrops, 64 x 32 units, standing on their bottom middle (the
  stage floor at the back). The road has `sun` (the cardboard sun on its
  stick, at x 18, raised from below the floor by a stagehand); the volcano
  has `flame` (the eruption's card, rising out of the crater at (-2.25, 22.625),
  binds 0 and 1 its two frames).
- `c_sl_leaf`: a fan's paper leaf on a stick, its origin the grip (binds 0 a
  leaf, 1 the Grass's own fans' paper grass). `c_sl_grass_banner`: "WE ♥ THE
  GRASS", hanging from the middle of its top edge.
- `c_sl_dazzlina_tiara` (her sequin tiara, on the Amazy Dayzee's head at
  1.2x with her) and `c_sl_baron_hat` (the Baron's tiny top hat, on Hayzee's
  head, tipped a little): each its origin at its foot.
"""
import math

from sl_rig import P, Rig, loop, once

ROOT = ("|root", None, None, 0.0, [], True, None)


def _flicker(group, end, every=5, frames=(0, 1)):
    pts, f, k = [], 0, 0
    while f < end:
        pts.append((f, frames[k % len(frames)], "step"))
        f += every
        k += 1
    return {group: {"tex": pts}}


# -------------------------------------------------------------- fiery hat ----
HAT_LAYOUT = [ROOT, ("hat", "|root", "fiery_hat", 0.0, [], True, (0.0, 0.0)),
              ("flames", "hat", "fiery_hat_flames_0", 0.2, ["fiery_hat_flames_1"], True, None)]
HM = {}
HM["S_1"] = loop(20, [(0, {"flames": {"sy": 1.0}}), (5, {"flames": {"sy": 1.0625, "rz": 2.0}}),
                      (10, {"flames": {"sy": 0.9375}}), (15, {"flames": {"sy": 1.0625, "rz": -2.0}}),
                      (20, {"flames": {"sy": 1.0}})], tracks=_flicker("flames", 20))
# B_1: blazing (the hat's big line, "Because I CARE"): the flames leaping (loop 12)
HM["B_1"] = loop(12, [(0, {"flames": {"sy": 1.125}}), (3, {"flames": {"sy": 1.375, "sx": 0.9375}}),
                      (6, {"flames": {"sy": 1.125}}), (9, {"flames": {"sy": 1.3125, "sx": 1.0625}}),
                      (12, {"flames": {"sy": 1.125}})], tracks=_flicker("flames", 12, every=3))
HAT_POSES = {"S_1": "the flames flickering (two frames every 5; loop 20)",
             "B_1": "blazing: the flames leaping (loop 12)"}
FIERY_HAT = Rig("c_sl_fiery_hat", "ch4_props", HAT_LAYOUT, HM,
                camera={"height": 28.0, "width": 24.0, "center": (0.0, -4.0), "res": (192, 224)}, poses=HAT_POSES)

# ------------------------------------------------------------- chandelier ----
TOP = 44.0
SHARDS = [((-12.0, 4.0), (-14.0, 9.0, -400.0)), ((10.0, 3.0), (13.0, 10.0, 380.0)), ((-4.0, 2.0), (-7.0, 13.0, 300.0)),
          ((5.0, 2.0), (8.0, 12.0, -320.0)), ((-17.0, 1.0), (-20.0, 5.0, 260.0)), ((16.0, 1.0), (20.0, 6.0, -280.0))]
CH_LAYOUT = [ROOT] + [("chain_%d" % (k + 1), "|root", "chain", -0.2, [], True, (0.0, TOP + 8.0 * k))
                      for k in range(4)] + [
    ("lamp", "|root", None, 0.0, [], True, (0.0, TOP)),
    ("chandelier", "lamp", "chandelier", 0.0, [], True, (0.0, TOP))] + [
    ("shard_%d" % (k + 1), "|root", "crystal_shard_%d" % (k % 3), 0.5, [], False, at) for k, (at, _f) in enumerate(SHARDS)]


def _swing(a):
    return {"lamp": {"rz": a}}


def _shards(k):
    out = {}
    for i, (_at, (dx, dy, spin)) in enumerate(SHARDS):
        out["shard_%d" % (i + 1)] = {"vis": 1 if k > 0 else 0, "tx": dx * k, "ty": dy * k - 14.0 * k * k,
                                     "rz": spin * k}
    return out


CM = {}
CM["S_1"] = loop(120, [(0, _swing(1.0)), (60, _swing(-1.0)), (120, _swing(1.0))])
# W_1: swinging hard (Bruno charging, the stage shaking; loop 30)
CM["W_1"] = loop(30, [(0, _swing(6.0)), (15, _swing(-6.0)), (30, _swing(6.0))])
# F_1: the Chandelier Drop: a jolt, the chain snaps, it falls 44 units to the
# floor (contact at 16), smashes, crystals flying, and settles askew (48, held)
_DOWN = {"lamp": {"ty": -TOP + 16.0}}  # its bottom (28 below the ring) on the floor
CM["F_1"] = once(48, [(0, _swing(0.0)), (3, P(_swing(-4.0), {"lamp": {"ty": 0.8}})),
                      (6, P(_swing(2.0), {"lamp": {"ty": 0.0}})),
                      (16, P(_DOWN, {"lamp": {"rz": -3.0}}), {"ease": "vector"}),
                      (17, P(_DOWN, {"chandelier": {"sy": 0.875, "sx": 1.0625}}, _shards(0.05))),
                      (22, P(_DOWN, {"lamp": {"rz": 6.0, "ty": -TOP + 17.0}}, _shards(0.5))),
                      (28, P(_DOWN, {"lamp": {"rz": 4.0}}, _shards(1.0)), {"ease": "vector"}),
                      (30, P(_DOWN, {"lamp": {"rz": 4.0}}, {k: {"vis": 0} for k in _shards(1.0)})),
                      (48, P(_DOWN, {"lamp": {"rz": 4.0}}, {k: {"vis": 0} for k in _shards(1.0)}))])
_FALLEN = P(_DOWN, {"lamp": {"rz": 4.0}})
CM["F_2"] = loop(60, [(0, _FALLEN), (60, _FALLEN)])
CH_POSES = {"S_1": "hanging, swaying a little (loop 120)", "W_1": "swinging hard (loop 30)",
            "F_1": "the drop: the chain snaps, it falls to the floor (contact at 16), smashes, crystals flying, "
                   "settling askew (48, held)",
            "F_2": "fallen (loop 60)"}
CHANDELIER = Rig("c_sl_chandelier", "ch4_props", CH_LAYOUT, CM,
                 camera={"height": 84.0, "width": 60.0, "center": (0.0, -4.0), "res": (300, 420)}, poses=CH_POSES)

# ------------------------------------------------------------------- sets ----
SET_CAM = {"height": 40.0, "width": 72.0, "center": (0.0, -4.0), "res": (576, 320)}
SUN_AT = (18.0, 0.0)
ROAD_LAYOUT = [ROOT, ("set", "|root", "set_road", 0.0, [], True, (0.0, 0.0)),
               ("sun", "|root", "cardboard_sun", 0.3, [], True, SUN_AT)]
_SUN_DOWN = {"sun": {"ty": -24.0}}
RM = {}
RM["S_1"] = loop(120, [(0, P(_SUN_DOWN, {"sun": {"vis": 0}})), (120, P(_SUN_DOWN, {"sun": {"vis": 0}}))])


def _sun_up(f, period=60):
    return {"sun": {"ty": 0.4 * math.sin(2 * math.pi * f / period), "rz": 2.0 * math.sin(2 * math.pi * f / period)}}


# U_1: the sun raised by a stagehand, jerkily, from below the floor (60, held)
RM["U_1"] = once(60, [(0, _SUN_DOWN), (12, {"sun": {"ty": -16.0, "rz": -3.0}}), (18, {"sun": {"ty": -15.0, "rz": 2.0}}),
                      (32, {"sun": {"ty": -6.0, "rz": -2.0}}), (38, {"sun": {"ty": -5.0, "rz": 3.0}}),
                      (50, {"sun": {"ty": 0.5, "rz": -1.0}}), (60, _sun_up(0))])
RM["S_2"] = loop(60, [(f, _sun_up(f)) for f in (0, 15, 30, 45, 60)])
# K_1: knocked over (the Understudy's Prop Smash): toppling off to the side,
# down out of sight (30, held)
RM["K_1"] = once(30, [(0, _sun_up(0)), (4, {"sun": {"rz": 8.0, "tx": -0.5}}),
                      (16, {"sun": {"rz": -70.0, "tx": 2.0, "ty": -2.0}}, {"ease": "linear"}),
                      (22, {"sun": {"rz": -95.0, "tx": 4.0, "ty": -18.0}}),
                      (23, {"sun": {"rz": -95.0, "tx": 4.0, "ty": -18.0, "vis": 0}}),
                      (30, {"sun": {"rz": -95.0, "tx": 4.0, "ty": -18.0, "vis": 0}})])
ROAD_POSES = {"S_1": "the road, the sun not up (loop 120)", "U_1": "the sun raised by a stagehand, jerkily (60, held)",
              "S_2": "the sun up, bobbing on its stick (loop 60)",
              "K_1": "the sun knocked over, out of sight (30, held; Prop Smash)"}
SET_ROAD = Rig("c_sl_set_road", "ch4_sets", ROAD_LAYOUT, RM, camera=SET_CAM, poses=ROAD_POSES)

BALL_LAYOUT = [ROOT, ("set", "|root", "set_ballroom", 0.0, [], True, (0.0, 0.0))]
SET_BALLROOM = Rig("c_sl_set_ballroom", "ch4_sets", BALL_LAYOUT, {"S_1": loop(60, [(0, {}), (60, {})])},
                   camera=SET_CAM, poses={"S_1": "the ballroom (loop 60)"})

CRATER = ((238 - 256) / 8.0, (256 - 75) / 8.0)   # the crater in set_volcano.png (ch4_sets.py CRATER_PX)
VOLC_LAYOUT = [ROOT, ("set", "|root", "set_volcano", 0.0, [], True, (0.0, 0.0)),
               ("flame", "|root", "volcano_flame_0", 0.3, ["volcano_flame_1"], True, CRATER)]
_OUT = {"flame": {"sy": 0.0625, "sx": 0.5, "vis": 0}}
VM = {}
VM["S_1"] = loop(60, [(0, _OUT), (60, _OUT)])
# E_1: the eruption: a rumble, the flame bursting up out of the crater, then
# roaring (48, held, flickering); E_2: erupting (loop 12)
VM["E_1"] = once(48, [(0, P(_OUT, {"set": {"tx": 0.0}})), (3, P(_OUT, {"set": {"tx": 0.3}})),
                      (6, P(_OUT, {"set": {"tx": -0.3}})), (9, P(_OUT, {"set": {"tx": 0.3}})),
                      (12, {"set": {"tx": 0.0}, "flame": {"sy": 0.0625, "sx": 0.5, "vis": 1}}),
                      (20, {"flame": {"sy": 1.25, "sx": 1.0625}}, {"ease": "vector"}),
                      (26, {"flame": {"sy": 0.9375, "sx": 1.0}}), (32, {"flame": {"sy": 1.0625}}),
                      (48, {"flame": {"sy": 1.0}})], tracks=_flicker("flame", 48, every=4))
VM["E_2"] = loop(12, [(0, {"flame": {"sy": 1.0}}), (6, {"flame": {"sy": 1.0625, "sx": 0.96875}}),
                      (12, {"flame": {"sy": 1.0}})], tracks=_flicker("flame", 12, every=4))
VOLC_POSES = {"S_1": "the volcano, quiet (loop 60)",
              "E_1": "the eruption: a rumble, the flame bursting up out of the crater (48, held, flickering)",
              "E_2": "erupting, the flame flickering (loop 12)"}
SET_VOLCANO = Rig("c_sl_set_volcano", "ch4_sets", VOLC_LAYOUT, VM, camera=dict(SET_CAM, height=56.0, center=(0.0, -2.0),
                                                                               res=(576, 448)), poses=VOLC_POSES)

# ------------------------------------------------------------------- fans ----
LEAF_LAYOUT = [ROOT, ("leaf", "|root", "paper_leaf", 0.0, ["paper_grass"], True, (0.0, 0.0))]
LM = {"S_1": loop(60, [(0, {}), (30, {"leaf": {"rz": 4.0}}), (60, {})]),
      "W_1": loop(16, [(0, {"leaf": {"rz": -20.0}}), (8, {"leaf": {"rz": 20.0}}), (16, {"leaf": {"rz": -20.0}})]),
      "S_2": loop(60, [(0, {"leaf": {"tex": 1}}), (30, {"leaf": {"rz": 4.0, "tex": 1}}), (60, {"leaf": {"tex": 1}})]),
      "W_2": loop(16, [(0, {"leaf": {"rz": -20.0, "tex": 1}}), (8, {"leaf": {"rz": 20.0, "tex": 1}}),
                       (16, {"leaf": {"rz": -20.0, "tex": 1}})])}
LEAF_POSES = {"S_1": "the leaf held up (loop 60)", "W_1": "the leaf waved (loop 16)",
              "S_2": "the paper grass held up (loop 60)", "W_2": "the paper grass waved (loop 16)"}
LEAF = Rig("c_sl_leaf", "ch4_fans", LEAF_LAYOUT, LM,
           camera={"height": 10.0, "width": 10.0, "center": (0.0, -1.0), "res": (160, 160)}, poses=LEAF_POSES)

GB_LAYOUT = [ROOT, ("banner", "|root", "banner_grass", 0.0, [], True, (0.0, 0.0))]
GBM = {"S_1": loop(90, [(0, {"banner": {"rz": 0.8}}), (45, {"banner": {"rz": -0.8, "sy": 1.03125}}),
                        (90, {"banner": {"rz": 0.8}})]),
       "W_1": loop(24, [(0, {"banner": {"ty": 0.0, "rz": 2.0}}), (12, {"banner": {"ty": 1.5, "rz": -2.0}}),
                        (24, {"banner": {"ty": 0.0, "rz": 2.0}})])}
GRASS_BANNER = Rig("c_sl_grass_banner", "ch4_fans", GB_LAYOUT, GBM,
                   camera={"height": 14.0, "width": 40.0, "center": (0.0, -12.0), "res": (400, 140)},
                   poses={"S_1": "hanging, swaying (loop 90)", "W_1": "held up by two fans, bouncing (loop 24)"})

TIARA = Rig("c_sl_dazzlina_tiara", "ch4_fans", [ROOT, ("tiara", "|root", "dazzlina_tiara", 0.0, [], True, (0.0, 0.0))],
            {"S_1": loop(40, [(0, {}), (20, {"tiara": {"sy": 1.0625}}), (40, {})])},
            camera={"height": 6.0, "width": 8.0, "center": (0.0, -1.0), "res": (128, 96)},
            poses={"S_1": "glittering, a small pulse (loop 40)"})
BARON_HAT = Rig("c_sl_baron_hat", "ch4_fans", [ROOT, ("hat", "|root", "baron_hat", 0.0, [], True, (0.0, 0.0))],
                {"S_1": loop(60, [(0, {"hat": {"rz": -8.0}}), (60, {"hat": {"rz": -8.0}})]),
                 "T_1": loop(20, [(0, {"hat": {"rz": -8.0}}), (10, {"hat": {"rz": -2.0, "ty": 0.3}}),
                                  (20, {"hat": {"rz": -8.0}})])},
                camera={"height": 8.0, "width": 8.0, "center": (0.0, -1.0), "res": (128, 128)},
                poses={"S_1": "tipped a little (loop 60)", "T_1": "doffed and bobbing as he talks (loop 20)"})
