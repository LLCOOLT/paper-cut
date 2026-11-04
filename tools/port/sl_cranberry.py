"""Cranberry, the last of the Luffs (Super Luigi, chapter 5;
docs/super_luigi/ch5.md), `c_sl_cranberry`: cards cut from the style model's
design split into pieces by Qwen-Image-Edit (port/assets/super_luigi/art/tools/
cut_cranberry.py, art/cranberry/qwen/; sl_rig.py). A small red-haired boy in
white robes, seated cross-legged, about 24 units to the top of his hair (a standing Toad's height); faces
-x. His heads are Qwen's own sleeping, waking, awake, smiling and talking edits.
Field only. He fades away at the end: the scene's own alpha (the NPC's colour),
not a pose.

Parts: boy (the mover) > legs (folded, behind), body (seated, the hips) > arm_back, arm_front (the
shoulders; rz < 0 swings forward), reach (the arm held out, palm up; hidden)
> piece (the compass piece on the palm; hidden), head (the neck; binds 0
asleep, 1 waking, 2 awake, 3 smiling, 4 talking).
"""
import json
import math
import os

from sl_rig import ART, P, Rig, blinks, loop, once

SLEEP, WAKE, AWAKE, SMILE, TALK = 0, 1, 2, 3, 4


def _layout():
    meta = json.load(open(os.path.join(ART, "cranberry", "qwen", "parts.json")))
    piece = tuple(meta["cards"]["compass_piece"]["model_xy"])
    return [
        ("|root", None, None, 0.0, [], True, None),
        ("boy", "|root", None, 0.0, [], True, (0.0, 0.0)),
        ("legs", "boy", "legs", -0.1, [], True, None),
        ("body", "boy", "body_seated", 0.0, [], True, None),
        ("arm_back", "body", "arm_back", -0.4, [], True, None),
        ("head", "body", "head_sleep", 0.3, ["head_wake", "head", "head_smile", "head_talk"], True, None),
        ("arm_front", "body", "arm_front", 0.5, [], True, None),
        ("reach", "body", "arm_reach", 0.55, [], False, None),
        ("piece", "reach", "compass_piece", 0.6, [], False, piece),
    ]


M = {}


def _breath(f, period, amp=1.0, tex=AWAKE):
    s = math.sin(2 * math.pi * f / period)
    return {"body": {"rz": 1.2 * amp * s}, "head": {"rz": 4.0 * amp * s, "ty": 0.2 * amp * s, "tex": tex},
            "arm_front": {"rz": 1.0 * s}}


# N_1: asleep, seated: slow breathing, the head nodding forward (loop 120).
M["N_1"] = loop(120, [(f, P(_breath(f, 120, 1.2, SLEEP), {"head": {"rz": 10.0 + 3.0 * math.sin(2 * math.pi * f / 120),
                                                                    "tex": SLEEP}}))
                      for f in (0, 30, 60, 90, 120)])
# U_1: waking (the sneeze wakes him): a start, eyes heavy, a blink, open,
# then a smile (90, held smiling).
M["U_1"] = once(90, [(0, P(_breath(0, 120, 1.2, SLEEP), {"head": {"rz": 10.0, "tex": SLEEP}})),
                     (6, {"head": {"rz": -4.0, "ty": 0.6, "tex": SLEEP}, "boy": {"ty": 0.3}}, {"ease": "vector"}),
                     (14, {"head": {"rz": 4.0, "tex": WAKE}}),
                     (34, {"head": {"rz": 2.0, "tex": WAKE}}),
                     (38, {"head": {"rz": 2.0, "tex": SLEEP}}),
                     (42, {"head": {"rz": 0.0, "tex": WAKE}}),
                     (54, {"head": {"rz": -2.0, "tex": AWAKE}}),
                     (70, {"head": {"rz": -4.0, "tex": SMILE}, "body": {"rz": -1.0}}),
                     (90, {"head": {"rz": -4.0, "tex": SMILE}})])
# S_1: awake, seated, a gentle breath (loop 80), blinking.
M["S_1"] = loop(80, [(f, _breath(f, 80)) for f in (0, 20, 40, 60, 80)],
                tracks=blinks("head", (34,), shut=SLEEP, open_=AWAKE))
# S_2: smiling (loop 80).
M["S_2"] = loop(80, [(f, P(_breath(f, 80, tex=SMILE), {"head": {"rz": -3.0, "tex": SMILE}})) for f in (0, 40, 80)])


# T_1: talking, seated, softly: the mouth, small nods (loop 32).
def _talk(f):
    k = (f // 6) % 2
    return {"head": {"rz": (-2.0 if k else 2.0), "tex": TALK if k else AWAKE}, "arm_front": {"rz": -10.0 if k else 0.0}}


M["T_1"] = loop(36, [(f, _talk(f)) for f in (0, 6, 12, 18, 24, 30, 36)])
# H_1: the hand-over: his arm held out, the piece on his palm, a smile (40, held).
_GIVE = {"arm_front": {"vis": 0}, "reach": {"vis": 1}, "piece": {"vis": 1}, "head": {"rz": 6.0, "tex": SMILE},
         "body": {"rz": 4.0}}
M["H_1"] = once(40, [(0, {"head": {"tex": AWAKE}}),
                     (10, {"arm_front": {"rz": -60.0}, "head": {"rz": 4.0, "tex": AWAKE}}),
                     (14, P(_GIVE, {"reach": {"vis": 1, "rz": 20.0}, "head": {"rz": 4.0, "tex": AWAKE}})),
                     (26, _GIVE), (40, _GIVE)])
# F_1: fading away (the scene fades him; this lifts him gently, eyes closing
# happily, the head tipped back: 120, held). The piece stays on the floor (a prop of the scene's).
M["F_1"] = once(120, [(0, {"head": {"tex": SMILE, "rz": -4.0}}),
                      (60, {"boy": {"ty": 1.5}, "head": {"tex": SMILE, "rz": -8.0}, "body": {"rz": -1.5}}),
                      (120, {"boy": {"ty": 3.0}, "head": {"tex": SMILE, "rz": -10.0}, "body": {"rz": -2.0},
                             "arm_front": {"rz": -20.0}, "arm_back": {"rz": -20.0}})])

POSES = {
    "N_1": "asleep, seated (loop 120)", "U_1": "waking: a start, a blink, a smile (90, held)",
    "S_1": "awake, seated, blinking (loop 80)", "S_2": "smiling (loop 80)", "T_1": "talking softly (loop 36)",
    "H_1": "the hand-over: arm out, the piece on his palm (40, held)",
    "F_1": "fading: rising a little, eyes closed, smiling (120, held; the fade is the scene's alpha)",
}

CRANBERRY = Rig("c_sl_cranberry", "cranberry/qwen", _layout(), M,
                camera={"height": 30.0, "width": 40.0, "center": (0.0, -2.0), "res": (400, 300)}, poses=POSES)
