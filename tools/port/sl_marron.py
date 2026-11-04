"""King Marron restored (Super Luigi, chapter 6's ending; docs/super_luigi/ch6.md), `c_sl_marron`: cards cut from the
style model's design split into pieces by Qwen-Image-Edit (port/assets/super_luigi/art/tools/cut_marron.py,
art/king_marron/qwen/), on a rig of his own, keyed by hand to MANIFESTO.md section 8's numbers. Faces -x, about 38 units
to the top of his crown. His face swaps are whole heads, Qwen's own blinking, talking and smiling edits. He was a Luigi
(c_luigi) variant; the scripts' poses keep their names: S_1, T_1, W_1, R_1, WV_1, H_1, U_1, B_1.

The robe is two cards that meet at the clasp, moving as one from the waist (`top`). Parts:
marron (the mover) > foot_back, foot_front, top (the waist) > robe_l, robe_r, arm_r, head (binds 0 open, 1 blinking,
2 talking, 3 smiling), arm_l. arm_l is the arm on the side he faces; an arm's rz < 0 swings it
forward and up (toward -x).
"""
import json
import math
import os

from sl_rig import ART, P, Rig, blinks, loop, once, talk

SHUT, OPEN, HAPPY = 1, 2, 3  # the head's binds: open, blinking, talking, smiling


def _layout():
    meta = json.load(open(os.path.join(ART, "king_marron", "qwen", "parts.json")))
    waist = tuple(meta["anchors"]["waist"])
    return [
        ("|root", None, None, 0.0, [], True, None),
        ("marron", "|root", None, 0.0, [], True, (0.0, 0.0)),
        ("foot_back", "marron", "foot_back", -0.3, [], True, None),
        ("foot_front", "marron", "foot_front", -0.2, [], True, None),
        ("top", "marron", None, 0.0, [], True, waist),
        ("robe_l", "top", "robe_l", 0.0, [], True, None),
        ("robe_r", "top", "robe_r", 0.05, [], True, None),
        ("arm_r", "top", "arm_r", 0.1, [], True, None),
        ("head", "top", "head", 0.3, ["head_blink", "head_talk", "head_smile"], True, None),
        ("arm_l", "top", "arm_l", 0.4, [], True, None),
    ]


M = {}


def _breath(f, end=96, k=1.0):
    p = 2 * math.pi * f / end
    return {"top": {"rz": 0.8 * k * math.sin(p)}, "head": {"rz": 2.0 * k * math.sin(p + 0.8)},
            "arm_l": {"rz": 2.5 * k * math.sin(p + 1.6)}, "arm_r": {"rz": -2.5 * k * math.sin(p + 0.4)}}


# stand: a king's slow, settled sway, blinking out of step (loop 96)
M["S_1"] = loop(96, [(f, _breath(f)) for f in range(0, 97, 8)], tracks=blinks("head", (36, 80)))
M["Z_1"] = M["S_1"]


def _walk(f, end, lean, bob, arm, step):
    p = 2 * math.pi * f / end
    s, c = math.sin(p), math.cos(p)
    return {"marron": {"ty": bob * abs(s)}, "top": {"rz": lean + 1.5 * math.sin(2 * p)},
            "foot_front": {"tx": -step * c, "ty": 0.6 * max(0.0, -s)}, "foot_back": {"tx": step * c, "ty": 0.6 * max(0.0, s)},
            "arm_l": {"rz": -arm * c}, "arm_r": {"rz": arm * c}, "head": {"rz": -0.5 * lean + 1.5 * math.sin(2 * p + 1.0)}}


# walk 40 and run 20, as the whole cast: a stout, rolling walk; the run leaning in, arms pumping
M["W_1"] = loop(40, [(f, _walk(f, 40, 1.0, 0.4, 12.0, 1.4)) for f in range(0, 41, 4)], tracks=blinks("head", (24,)))
M["R_1"] = loop(20, [(f, _walk(f, 20, 5.0, 0.8, 28.0, 2.2)) for f in range(0, 21, 2)])

# talk: 14 frames, the near hand lifted and open
_T1 = {"arm_l": {"rz": -40.0}, "arm_r": {"rz": 4.0}}
M["T_1"] = loop(14, [(0, P(_T1, {"head": {"rz": -1.5}})),
                     (4, P(_T1, {"arm_l": {"rz": -50.0}, "head": {"rz": 2.0}})),
                     (7, P(_T1, {"arm_l": {"rz": -36.0}, "head": {"rz": -0.5}})),
                     (11, P(_T1, {"arm_l": {"rz": -48.0}, "head": {"rz": 2.0}})),
                     (14, P(_T1, {"head": {"rz": -1.5}}))],
                 tracks=talk("head", 14, every=3, talk_tex=OPEN))

# WV_1: waving goodbye, the near arm up, the hand above his brow, rocking; smiling (loop 24)
_WAVE = {"head": {"rz": -5.0, "tex": HAPPY}, "top": {"rz": -2.0}, "arm_r": {"rz": 6.0}}
M["WV_1"] = loop(24, [(0, P(_WAVE, {"arm_l": {"rz": -120.0}})), (6, P(_WAVE, {"arm_l": {"rz": -140.0}})),
                      (12, P(_WAVE, {"arm_l": {"rz": -120.0}})), (18, P(_WAVE, {"arm_l": {"rz": -140.0}})),
                      (24, P(_WAVE, {"arm_l": {"rz": -120.0}}))])

# H_1: the embrace with Eclair: both arms forward and in, eyes happy-shut, the head tipped down (loop 60)
_HOLD = {"arm_l": {"rz": -62.0}, "arm_r": {"rz": -48.0}, "head": {"rz": 8.0, "tex": HAPPY}, "top": {"rz": 3.0}}
M["H_1"] = loop(60, [(0, _HOLD), (15, P(_HOLD, {"head": {"rz": 9.5, "tex": HAPPY}})),
                     (30, P(_HOLD, {"head": {"rz": 10.5, "tex": HAPPY}, "top": {"rz": 4.0}})),
                     (45, P(_HOLD, {"head": {"rz": 9.5, "tex": HAPPY}})), (60, _HOLD)])

# U_1: restored, where the husk fell away: bowed, eyes shut; blinking, coming to; looking down at his hands; then up,
# smiling (90, held)
_DOWN = {"top": {"rz": 10.0}, "head": {"rz": 12.0, "tex": SHUT}, "arm_l": {"rz": 8.0}, "arm_r": {"rz": -6.0},
         "marron": {"ty": -0.4}}
M["U_1"] = once(90, [(0, _DOWN), (20, P(_DOWN, {"top": {"rz": 8.0}, "head": {"rz": 10.0, "tex": SHUT}})),
                     (30, {"top": {"rz": 5.0}, "head": {"rz": 6.0}}),
                     (33, {"top": {"rz": 5.0}, "head": {"rz": 6.0, "tex": SHUT}}),
                     (37, {"top": {"rz": 5.0}, "head": {"rz": 6.0}}),
                     (54, {"arm_l": {"rz": -55.0}, "arm_r": {"rz": -40.0}, "head": {"rz": 16.0}, "top": {"rz": 4.0}}),
                     (72, {"arm_l": {"rz": -50.0}, "arm_r": {"rz": -36.0}, "head": {"rz": 14.0}, "top": {"rz": 4.0}}),
                     (84, {"head": {"rz": -4.0, "tex": HAPPY}}), (90, {"head": {"rz": -2.0, "tex": HAPPY}})])

# B_1: a bow of thanks, from the waist, eyes shut (30)
_BOW = {"top": {"rz": 20.0}, "head": {"rz": 8.0, "tex": SHUT}, "arm_l": {"rz": -24.0}, "arm_r": {"rz": 10.0}}
M["B_1"] = once(30, [(0, {}), (10, _BOW), (20, _BOW), (30, {})])

POSES = {
    "S_1": "stand, blinking (loop 96)", "Z_1": "idle: as the stand", "T_1": "talking, a hand raised (loop 14)",
    "W_1": "walk (loop 40)", "R_1": "run, to her (loop 20)", "WV_1": "waving goodbye (loop 24)",
    "H_1": "the embrace with Eclair: arms forward and in, eyes happy (loop 60)",
    "U_1": "restored: coming to where the husk fell away, looking at his hands, then up (90, held)",
    "B_1": "a bow of thanks (30)",
}

MARRON = Rig("c_sl_marron", "king_marron/qwen", _layout(), M,
             camera={"height": 48.0, "width": 60.0, "center": (0.0, 22.0), "res": (480, 384)}, poses=POSES)
