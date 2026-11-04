"""The Chestnut King (Super Luigi, chapter 6's boss; docs/super_luigi/ch6.md),
`c_sl_chestnut_king`: cards cut from the style model's design split into pieces by
Qwen-Image-Edit (port/assets/super_luigi/art/tools/cut_chestnut.py, art/chestnut_king/qwen/;
sl_rig.py). A huge round chestnut husk of a king with a face, on two green feet, about 45
units to the top spike and 49 to the crown (Luigi is 24); faces -x. Under the curse he is
King Marron, Eclair's beloved: the curse breaking leaves him calm, the husk gone.

Parts: king (the mover) > leg_back, leg_front (the feet), nut (the chestnut inside the husk,
pivot at its base) > husk_left, husk_right (the husk's halves, meeting at its middle; rz > 0
swings the left half open, < 0 the right), head (his face, an overlay on the husk; binds 0
rest, 1 roar, 2 hurt, 3 sad, 4 calm) > crown, target_skull (a locator); target_husk (a
locator in the husk's middle); shard_1..4 (the husk's pieces, hidden), nut_1..3 (chestnuts
thrown up in Chestnut Rain, hidden).

The battle's two parts (ch6: Husk and Skull) follow the locators
`target_husk` and `target_skull`.
"""
import json
import math
import os

from sl_rig import ART, P, Rig, loop, once

ART_DIR = "chestnut_king/qwen"  # cut from Qwen's split of the style model's design (art/tools/cut_chestnut.py)


def _layout():
    an = {k: tuple(v) for k, v in json.load(open(os.path.join(ART, ART_DIR, "parts.json")))["anchors"].items()}
    return [
        ("|root", None, None, 0.0, [], True, None),
        ("king", "|root", None, 0.0, [], True, (0.0, 0.0)),
        ("leg_back", "king", "leg_back", -0.6, [], True, None),
        ("nut", "king", "nut", -0.3, [], True, None),
        ("target_husk", "nut", None, 0.0, [], True, an["husk"]),
        ("husk_right", "nut", "husk_right", 0.1, ["husk_right_open"], True, None),
        ("husk_left", "nut", "husk_left", 0.2, ["husk_left_open"], True, None),
        ("leg_front", "king", "leg_front", 0.4, [], True, None),
        ("head", "nut", "head", 0.6, ["head_roar", "head_hurt", "head_sad", "head_calm"], True, None),
        ("crown", "head", "crown", 0.7, [], True, None),
        ("target_skull", "head", None, 0.6, [], True, an["skull"]),
        ("shard_1", "king", "husk_shard_1", 0.8, [], False, None),
        ("shard_2", "king", "husk_shard_2", 0.8, [], False, None),
        ("shard_3", "king", "husk_shard_3", 0.8, [], False, None),
        ("shard_4", "king", "husk_shard_4", 0.8, [], False, None),
        ("nut_1", "king", "chestnut", 0.9, [], False, (-6.0, 30.0)),
        ("nut_2", "king", "chestnut", 0.9, [], False, (2.0, 33.0)),
        ("nut_3", "king", "chestnut", 0.9, [], False, (9.0, 29.0)),
    ]


ROAR, HURT, SAD, CALM = 1, 2, 3, 4
M = {}


def _drip(end, every=8):
    """(the drawn goo's drip loop: the cut design has no goo; Goo Drool's stream is the battle's effect)"""
    return {}


def _breath(f, period=96, amp=1.0):
    s = math.sin(2 * math.pi * f / period)
    return {"nut": {"sy": 1.0 + 0.0625 * amp * (0.5 + 0.5 * s)}, "head": {"rz": 2.0 * s * amp, "ty": 0.4 * s * amp},
            "husk_left": {"rz": 1.0 * amp * (0.5 + 0.5 * s)}, "husk_right": {"rz": -1.0 * amp * (0.5 + 0.5 * s)},
            "cape": {"rz": -1.0 * s}}


# S_1: standing: heavy, wet breathing, the husk heaving, goo dripping (loop 96).
M["S_1"] = loop(96, [(f, _breath(f)) for f in (0, 24, 48, 72, 96)], tracks=_drip(96))


def _lumber(f, period):
    ph = 2 * math.pi * f / period
    s, c = math.sin(ph), math.cos(ph)
    return {"leg_front": {"tx": -2.5 * c, "ty": 1.2 * max(0.0, s), "rz": -6.0 * c},
            "leg_back": {"tx": 2.5 * c, "ty": 1.2 * max(0.0, -s), "rz": 6.0 * c},
            "nut": {"rz": 4.0 * s, "ty": 0.8 * abs(s)}, "head": {"rz": -2.5 * s}, "cape": {"rz": -3.0 * s}}


# W_1: a lumbering walk, every step a thud (loop 60).
M["W_1"] = loop(60, [(f, _lumber(f, 60)) for f in (0, 15, 30, 45, 60)], tracks=_drip(60))

# Fang Lunge: A_1A rearing back, jaws wide (30, held); A_1B the lunge (hit at
# 5, 30); O_1 the overreach (when Luigi superguards): he crashes past, off
# balance, the husk split open down the front, the skull low within the
# hammer's reach (40, held); O_2 recovering from it, the husk closing (30).
_REAR = {"nut": {"rz": -12.0}, "head": {"rz": -16.0, "tex": ROAR}, "leg_front": {"tx": -1.0},
         "husk_left": {"rz": -2.0}, "husk_right": {"rz": 2.0}}
_BITE = {"king": {"tx": -6.0}, "nut": {"rz": 22.0}, "head": {"rz": 18.0, "tex": ROAR, "tx": -2.0},
         "leg_front": {"tx": -3.0}, "leg_back": {"tx": 2.0}}
M["A_1A"] = once(30, [(0, {}), (18, _REAR), (30, _REAR)], tracks=_drip(30))
M["A_1B"] = once(30, [(0, _REAR), (5, _BITE, {"ease": "vector"}),
                      (9, P(_BITE, {"head": {"rz": 14.0, "tex": 0, "tx": -2.0}})),
                      (20, {"king": {"tx": -2.0}, "nut": {"rz": 4.0}}), (30, {})], tracks=_drip(30))
# open, the halves are Qwen's faceless husk (tex 1: no half of the face on the open right half); shut, his own
_OPEN = {"husk_left": {"rz": 26.0, "tex": 1}, "husk_right": {"rz": -26.0, "tex": 1}}
_SHUT = {"husk_left": {"tex": 0}, "husk_right": {"tex": 0}}
_LOW = {"king": {"tx": -10.0}, "nut": {"rz": 18.0, "sy": 0.9375}, "head": {"rz": 30.0, "ty": -8.0, "tex": HURT},
        "leg_front": {"tx": -4.0, "rz": -20.0}, "leg_back": {"tx": 3.0, "rz": 25.0}, "crown": {"rz": 20.0, "tx": -1.0}}
M["O_1"] = once(40, [(0, P(_BITE, _SHUT)),
                     (8, P(_LOW, _SHUT, {"king": {"tx": -12.0, "ty": 1.0}, "nut": {"rz": 34.0}})),
                     (14, P(_LOW, _OPEN), {"ease": "vector"}),
                     (20, P(_LOW, _OPEN, {"husk_left": {"rz": 36.0}, "husk_right": {"rz": -36.0}})),
                     (40, P(_LOW, _OPEN))], tracks=_drip(40))
M["O_2"] = once(30, [(0, P(_LOW, _OPEN)), (12, P(_SHUT, {"king": {"tx": -5.0}, "nut": {"rz": 6.0}, "head": {"tex": HURT}})),
                     (20, P(_SHUT, {"king": {"tx": 0.0}, "nut": {"rz": -4.0}})), (30, _SHUT)], tracks=_drip(30))
# H_2: the husk split open and the skull low, as a loop (the overreach's turn: loop 48).
M["H_2"] = loop(48, [(0, P(_LOW, _OPEN, {"king": {"tx": 0.0}})),
                     (24, P(_LOW, _OPEN, {"king": {"tx": 0.0}, "nut": {"rz": 28.0, "sy": 0.875},
                                          "head": {"rz": 30.0, "ty": -9.5, "tex": HURT}})),
                     (48, P(_LOW, _OPEN, {"king": {"tx": 0.0}}))], tracks=_drip(48))

# Goo Drool: A_2A head forward, jaws opening (24, held); A_2B the stream (the
# goo is the battle's effect from 6; 40).
_DROOL = {"nut": {"rz": 10.0}, "head": {"rz": 24.0, "tex": ROAR, "ty": -2.0}, "goo": {"sy": 1.5, "vis": 1}}
M["A_2A"] = once(24, [(0, {}), (16, P(_DROOL, {"head": {"rz": 18.0, "tex": ROAR, "ty": -1.0}})), (24, _DROOL)],
                 tracks=_drip(24))
M["A_2B"] = once(40, [(0, _DROOL), (6, P(_DROOL, {"goo": {"sy": 2.5, "vis": 1}}), {"ease": "vector"}),
                      (28, P(_DROOL, {"goo": {"sy": 2.25, "vis": 1}, "head": {"rz": 26.0, "tex": ROAR, "ty": -2.0}})),
                      (40, {})], tracks=_drip(40, 4))


def _pop(name, k, dx):
    """a chestnut thrown up out of the husk (k 0 .. 1)"""
    return {name: {"vis": 1, "ty": 14.0 * math.sin(math.pi * min(k, 1.0) * 0.5) + 2.0, "tx": dx * k, "rz": 200 * k}}


# A_3: Chestnut Rain: he shakes himself, spikes rattling, and chestnuts fly up
# out of the husk (they come down on everyone as the battle's own; 48).
M["A_3"] = once(48, [(0, {}),
                     (6, {"nut": {"rz": 6.0}, "husk_left": {"rz": 4.0}, "husk_right": {"rz": -4.0}}),
                     (10, P({"nut": {"rz": -6.0}, "husk_left": {"rz": 2.0}, "husk_right": {"rz": -6.0}},
                            _pop("nut_1", 0.2, -3.0))),
                     (14, P({"nut": {"rz": 6.0}, "husk_left": {"rz": 6.0}, "husk_right": {"rz": -2.0}},
                            _pop("nut_1", 0.5, -3.0), _pop("nut_2", 0.2, 1.0))),
                     (18, P({"nut": {"rz": -6.0}, "husk_left": {"rz": 2.0}, "husk_right": {"rz": -6.0}},
                            _pop("nut_1", 0.8, -3.0), _pop("nut_2", 0.5, 1.0), _pop("nut_3", 0.2, 4.0))),
                     (22, P({"nut": {"rz": 6.0}}, _pop("nut_1", 1.0, -3.0), _pop("nut_2", 0.8, 1.0),
                            _pop("nut_3", 0.5, 4.0))),
                     (26, P({"nut": {"rz": -3.0}}, _pop("nut_2", 1.0, 1.0), _pop("nut_3", 0.8, 4.0))),
                     (30, P({"nut": {"rz": 2.0}}, _pop("nut_3", 1.0, 4.0))),
                     (36, {}), (48, {})], tracks=_drip(48))
# R_2: Roar: rearing up, head thrown back, jaws wide, shaking (60).
_ROAR = {"nut": {"rz": -8.0, "sy": 1.0625}, "head": {"rz": -22.0, "tex": ROAR, "ty": 1.0},
         "husk_left": {"rz": 4.0}, "husk_right": {"rz": -4.0}, "cape": {"rz": -6.0}}
M["R_2"] = once(60, [(0, {}), (12, P(_ROAR, {"head": {"rz": -10.0, "tex": 0}, "nut": {"sy": 0.9375}})),
                     (20, _ROAR, {"ease": "vector"})] +
                [(24 + 4 * k, P(_ROAR, {"head": {"rz": -22.0 + (3 if k % 2 else -3), "tex": ROAR, "ty": 1.0}}))
                 for k in range(7)] + [(60, {})], tracks=_drip(60, 5))
# Q_1: he hesitates: looks back over his shoulder at the curtain, a tear (60).
_LOOK = {"head": {"rz": -6.0, "tex": SAD, "sx": -1.0}, "nut": {"rz": -4.0}}
M["Q_1"] = once(60, [(0, {}), (10, P(_LOOK, {"head": {"rz": -6.0, "tex": 0, "sx": 0.0625}})),
                     (14, _LOOK), (44, P(_LOOK, {"head": {"rz": -2.0, "tex": SAD, "sx": -1.0}})),
                     (50, {"head": {"sx": 0.0625, "tex": SAD}}), (54, {}), (60, {})], tracks=_drip(60))
# D_1: hurt (loop 12): a jolt, eyes squeezed shut.
M["D_1"] = loop(12, [(0, {"head": {"tex": HURT}}),
                     (3, {"nut": {"rz": -10.0, "sx": 1.0625}, "head": {"rz": -12.0, "tex": HURT},
                          "crown": {"rz": -10.0}, "king": {"tx": 1.5}}),
                     (8, {"nut": {"rz": -5.0}, "head": {"rz": -6.0, "tex": HURT}, "king": {"tx": 0.8}}),
                     (12, {"head": {"tex": HURT}})])
# K_1: staggering to his knees (the last turn): sunk low, swaying (loop 60).
_KNEEL = {"king": {"ty": -3.0}, "nut": {"rz": 10.0, "sy": 0.9375}, "head": {"rz": 14.0, "tex": SAD, "ty": -1.0},
          "leg_front": {"rz": -60.0, "ty": 2.0}, "leg_back": {"rz": 50.0, "ty": 2.0}, "crown": {"rz": 12.0}}
M["K_1"] = loop(60, [(0, _KNEEL), (30, P(_KNEEL, {"nut": {"rz": 6.0, "sy": 0.9375}, "head": {"rz": 10.0, "tex": SAD,
                                                                                         "ty": -1.0}})),
                     (60, _KNEEL)], tracks=_drip(60, 12))


def _fall(name, k, dx, dy, spin):
    return {name: {"vis": 1, "tx": dx * k, "ty": dy * k * k - 6.0 * k * (1 - k), "rz": spin * k}}


# C_1: the curse breaking: a great shudder, the husk splits and falls away in
# pieces, the goo stops, the face goes calm (80, held). S_3: King Marron, the
# husk gone, calm (loop 96): the fallback for the restored King.
_GONE = P(*[{n: dict(_fall(n, 1.0, dx, dy, sp)[n], vis=0)} for n, dx, dy, sp in
             (("shard_1", -10, -24, -90), ("shard_2", 10, -24, 90), ("shard_3", -12, -10, -60),
              ("shard_4", 12, -10, 60))])
_BARE = {"husk_left": {"vis": 0}, "husk_right": {"vis": 0}, "goo": {"vis": 0}, "head": {"tex": CALM}}
M["C_1"] = once(80, [(0, P(_SHUT, {"head": {"tex": SAD}})),
                     (6, P(_SHUT, {"nut": {"rz": 3.0}, "head": {"tex": SAD}})),
                     (10, P(_SHUT, {"nut": {"rz": -3.0}, "head": {"tex": SAD}})),
                     (14, P(_SHUT, {"nut": {"rz": 3.0}, "head": {"tex": SAD}})),
                     (22, P(_OPEN, {"head": {"tex": SAD}, "nut": {"sy": 1.0625}}), {"ease": "vector"}),
                     (24, P(_BARE, _fall("shard_1", 0.0, -10, -24, -90), _fall("shard_2", 0.0, 10, -24, 90),
                            _fall("shard_3", 0.0, -12, -10, -60), _fall("shard_4", 0.0, 12, -10, 60))),
                     (34, P(_BARE, _fall("shard_1", 0.5, -10, -24, -90), _fall("shard_2", 0.5, 10, -24, 90),
                            _fall("shard_3", 0.5, -12, -10, -60), _fall("shard_4", 0.5, 12, -10, 60)),
                      {"ease": "linear"}),
                     (44, P(_BARE, _fall("shard_1", 1.0, -10, -24, -90), _fall("shard_2", 1.0, 10, -24, 90),
                            _fall("shard_3", 1.0, -12, -10, -60), _fall("shard_4", 1.0, 12, -10, 60)),
                      {"ease": "vector"}),
                     (52, P(_BARE, _GONE)),
                     (80, P(_BARE, _GONE))])
M["S_3"] = loop(96, [(f, P(_BARE, _breath(f, amp=0.5))) for f in (0, 24, 48, 72, 96)])
# T_3: King Marron talking, gently (loop 32): no fangs, a nod.
M["T_3"] = loop(32, [(0, _BARE), (8, P(_BARE, {"head": {"rz": 5.0, "tex": CALM}})), (16, _BARE),
                     (24, P(_BARE, {"head": {"rz": 3.0, "tex": CALM}})), (32, _BARE)])

POSES = {
    "S_1": "standing: heavy breathing, goo dripping (loop 96)", "W_1": "a lumbering walk (loop 60)",
    "A_1A": "Fang Lunge, rearing back (30, held)", "A_1B": "Fang Lunge (hit at 5; 30)",
    "O_1": "the overreach: past, off balance, the husk split open, the skull low (40, held)",
    "O_2": "recovering from it, the husk closing (30)", "H_2": "the overreach's turn: open and low (loop 48)",
    "A_2A": "Goo Drool, leaning in (24, held)", "A_2B": "Goo Drool (the stream from 6; 40)",
    "A_3": "Chestnut Rain: a shake, chestnuts flying up (48)", "R_2": "Roar (60)",
    "Q_1": "hesitating: a look back at the curtain, a tear (60)", "D_1": "hurt (loop 12)",
    "K_1": "on his knees, staggering (loop 60)",
    "C_1": "the curse breaking: the husk falls away in pieces, the face calm (80, held)",
    "S_3": "King Marron (the husk gone, calm), standing (loop 96)", "T_3": "King Marron talking (loop 32)",
}

# the body's squash halved, but for the roar, the bite and the hurt (ART_PLAN: the game's bodies rarely squash)
_SQUASH_KEEP = {"R_2", "A_1B", "D_1"}
for _n, _m in M.items():
    if _n not in _SQUASH_KEEP:
        _m["keys"] = [(k[0], {g: {c: (1.0 + round((v - 1.0) * 8.0) / 16.0 if c in ("sx", "sy") and g in ("nut", "king", "cape", "leg_back", "leg_front") and abs(v) > 0.2 else v)
                                  for c, v in ch.items()} for g, ch in k[1].items()}) + tuple(k[2:]) for k in _m["keys"]]

# the cut design has no cape and no goo card: their channels go
for _m in M.values():
    _m["keys"] = [(k[0], {g: ch for g, ch in k[1].items() if g not in ("cape", "goo")}) + tuple(k[2:]) for k in _m["keys"]]

KING = Rig("c_sl_chestnut_king", ART_DIR, _layout(), M,
           camera={"height": 58.0, "width": 72.0, "center": (-4.0, -4.0), "res": (432, 348)}, poses=POSES)
