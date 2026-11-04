"""Fuselle, the mayor of Plumpbelly's daughter (Super Luigi, chapter 2; docs/super_luigi/ch2.md),
`c_sl_fuselle`: cards cut from the style model's design split into pieces by Qwen-Image-Edit
(port/assets/super_luigi/art/tools/cut_fuselle.py, art/fuselle/qwen/; sl_rig.py). A pink Bob-omb girl with
a red bow; faces -x. Her happy-shut and talking faces are Qwen's edits: whole-body swaps. Field only.

Parts: fus (the mover) > shoe_back, shoe_front (behind the body), body (the bottom of the body; binds 0 rest,
1 eyes shut, 2 talking) > key (her back, +x; turning: its width squashes), bow (her crown).
"""
import math

from sl_rig import P, Rig, blinks, loop, once, talk

REST, SHUT, TALK = 0, 1, 2

LAYOUT = [
    ("|root", None, None, 0.0, [], True, None),
    ("fus", "|root", None, 0.0, [], True, (0.0, 0.0)),
    ("shoe_back", "fus", "shoe_back", -0.3, [], True, None),
    ("shoe_front", "fus", "shoe_front", -0.2, [], True, None),
    ("body", "fus", "body", 0.0, ["body_blink", "body_talk"], True, None),
    ("key", "body", "key", -0.1, [], True, None),
    ("bow", "body", "bow", 0.2, [], True, None),
]


def _key(f, period=40):
    """the key turning: one full turn every period frames, seen edge-on twice"""
    return {"key": {"sx": 0.25 + 0.75 * abs(math.cos(math.pi * f / period))}}


def _stand(f, period=80):
    s = math.sin(2 * math.pi * f / period)
    return P(_key(f), {"body": {"rz": 1.5 * s, "ty": 0.15 * (1 + s)}, "bow": {"rz": -2.0 * s}})


def _walk(f, period, hop=0.8, sway=5.0, stride=1.2):
    ph = 2 * math.pi * f / period
    s, c = math.sin(ph), math.cos(ph)
    return P(_key(f, period), {"fus": {"ty": hop * abs(s)}, "body": {"rz": sway * s}, "bow": {"rz": -1.5 * sway * s},
                               "shoe_front": {"tx": stride * c, "ty": 0.5 * max(0.0, s)},
                               "shoe_back": {"tx": -stride * c, "ty": 0.5 * max(0.0, -s)}})


M = {}
# S_1: standing, a little sway, the key turning, blinking (loop 80)
M["S_1"] = loop(80, [(f, _stand(f)) for f in range(0, 81, 10)], tracks=blinks("body", (30, 70), shut=SHUT))
M["Z_1"] = M["S_1"]
# W_1, R_1: a waddle and a quick trot (loop 32, 16)
M["W_1"] = loop(32, [(f, _walk(f, 32)) for f in range(0, 33, 4)])
M["R_1"] = loop(16, [(f, _walk(f, 16, 1.4, 8.0, 2.0)) for f in range(0, 17, 2)])
# T_1: talking, a nod on each phrase (loop 32)
M["T_1"] = loop(32, [(f, P(_key(f), {"body": {"rz": 3.0 * math.sin(2 * math.pi * f / 16)}})) for f in range(0, 33, 4)],
                tracks=talk("body", 32, every=6, talk_tex=TALK))
# T_2: talking, earnest: little hops (loop 16)
M["T_2"] = loop(16, [(0, P(_key(0), {"fus": {"ty": 0.0}, "body": {"rz": 2.0}})),
                     (4, P(_key(4), {"fus": {"ty": 1.2}, "body": {"rz": -4.0}})),
                     (8, P(_key(8), {"fus": {"ty": 0.0}, "body": {"rz": 2.0}}), {"ease": "vector"}),
                     (12, P(_key(12), {"fus": {"ty": 0.8}, "body": {"rz": 5.0}})),
                     (16, P(_key(16), {"fus": {"ty": 0.0}, "body": {"rz": 2.0}}))],
                tracks=talk("body", 16, every=4, talk_tex=TALK))
# C_1: crying for joy: eyes shut, rocking (loop 24)
M["C_1"] = loop(24, [(0, {"body": {"rz": -6.0, "tex": SHUT}}), (12, {"body": {"rz": 6.0, "tex": SHUT}, "fus": {"ty": 0.6}}),
                     (24, {"body": {"rz": -6.0, "tex": SHUT}})])
# C_2: the fuse lit with joy: eyes shut, quick shivers, the key spinning fast (loop 8)
M["C_2"] = loop(8, [(0, P(_key(0, 8), {"body": {"rz": -3.0, "tex": SHUT}})), (4, P(_key(4, 8), {"body": {"rz": 3.0, "tex": SHUT}})),
                    (8, P(_key(8, 8), {"body": {"rz": -3.0, "tex": SHUT}}))])
# G_1: giving (the pie): a hop toward him, leaning in, back (16, held)
M["G_1"] = once(16, [(0, {}), (5, {"fus": {"tx": -2.0, "ty": 1.5}, "body": {"rz": 12.0, "tex": SHUT}}),
                     (9, {"fus": {"tx": -2.5, "ty": 0.0}, "body": {"rz": 8.0, "tex": SHUT}}, {"ease": "vector"}),
                     (16, {"fus": {"tx": -2.5}, "body": {"tex": REST}})])
# Q_1: a gasp: a jump back, the mouth open (14, held)
M["Q_1"] = once(14, [(0, {}), (4, {"fus": {"ty": 2.0, "tx": 1.0}, "body": {"rz": -10.0, "tex": TALK}}),
                     (8, {"fus": {"tx": 1.6}, "body": {"rz": -6.0, "tex": TALK}}, {"ease": "vector"}),
                     (14, {"fus": {"tx": 1.6}, "body": {"rz": -4.0, "tex": TALK}})])

POSES = {"S_1": "standing, the key turning, blinking (loop 80)", "W_1": "walking (loop 32)", "R_1": "running (loop 16)",
         "T_1": "talking (loop 32)", "T_2": "talking, earnest: little hops (loop 16)",
         "C_1": "crying for joy: eyes shut, rocking (loop 24)", "C_2": "the fuse lit with joy: shivers (loop 8)",
         "G_1": "giving: a hop toward him, leaning in (16, held)", "Q_1": "a gasp: a jump back (14, held)"}

FUSELLE = Rig("c_sl_fuselle", "fuselle/qwen", LAYOUT, M, poses=POSES,
              camera={"height": 26.0, "width": 30.0, "center": (0.0, -2.0), "res": (300, 260)})
