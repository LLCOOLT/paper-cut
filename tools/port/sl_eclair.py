"""Princess Eclair of the Waffle Kingdom (Super Luigi, chapter 6; docs/super_luigi/ch6.md), `c_sl_eclair`: cards cut from
the style model's design split into pieces by Qwen-Image-Edit (port/assets/super_luigi/art/tools/cut_eclair.py,
art/eclair/qwen/), on a rig of her own, keyed by hand to MANIFESTO.md section 8's numbers. Faces -x, about 28 units to her
tiara. Her face swaps are overlays of Qwen's own blinking, talking and smiling heads. The tiara is its own card: on her
hair, and when she lifts it, the same card in her hand.

The gown is one card from her hem to her shoulders, so she sways from the hem, a little. Parts:
eclair (the mover) > gown > hair (behind), arm_r, arm_l > tiara (hidden; binds 0 still, 1 glowing),
head > crown (the tiara on her hair; hidden while it is in her hand), eyes (0 open, 1 shut, 2 happy),
mouth (0 shut, 1 open, 2 smile).
arm_l is the arm on the side she faces; an arm's rz < 0 swings it forward (toward -x).
"""
import json
import math
import os

from sl_rig import ART, P, Rig, blinks, loop, merge_tracks, once, talk

SHUT, HAPPY = 1, 2
OPEN, SMILE = 1, 2
GLOW = 1


def _layout():
    meta = json.load(open(os.path.join(ART, "eclair", "qwen", "parts.json")))
    hand = tuple(meta["anchors"]["hand_l"])
    on = tuple(meta["anchors"]["tiara_on"])
    return [
        ("|root", None, None, 0.0, [], True, None),
        ("eclair", "|root", None, 0.0, [], True, (0.0, 0.0)),
        ("gown", "eclair", "gown", 0.0, [], True, None),
        ("hair", "gown", "hair", -0.5, [], True, None),
        ("arm_r", "gown", "arm_r", 0.1, [], True, None),
        ("arm_l", "gown", "arm_l", 0.2, [], True, None),
        ("tiara", "arm_l", "tiara", 0.22, ["tiara_glow"], False, hand),
        ("head", "gown", "head", 0.4, [], True, None),
        ("crown", "head", "tiara", 0.46, ["tiara_glow"], True, on),
        ("eyes", "head", "eyes", 0.42, ["eyes_shut", "eyes_happy"], True, None),
        ("mouth", "head", "mouth_shut", 0.42, ["mouth", "mouth_smile"], True, None),
    ]


M = {}


def _sway(f, end=120, k=1.0):
    p = 2 * math.pi * f / end
    return {"gown": {"rz": 0.6 * k * math.sin(p)}, "head": {"rz": 2.5 * k * math.sin(p + 0.9)},
            "hair": {"rz": 1.8 * k * math.sin(p + 1.8)},
            "arm_l": {"rz": 2.0 * k * math.sin(p + 2.2)}, "arm_r": {"rz": -2.0 * k * math.sin(p + 0.5)}}


# stand: a light, upright sway; the hair a beat behind; blinks out of step
M["S_1"] = loop(120, [(f, _sway(f)) for f in range(0, 121, 10)], tracks=blinks("eyes", (40, 92)))
M["Z_1"] = M["S_1"]


def _walk(f, end, lean, bob, arm, hair):
    p = 2 * math.pi * f / end
    s = math.sin(p)
    return {"eclair": {"ty": bob * abs(math.sin(p))},
            "gown": {"rz": lean + 1.0 * math.sin(2 * p)},
            "arm_l": {"rz": -arm * s}, "arm_r": {"rz": arm * s},
            "head": {"rz": -lean * 0.5 + 1.5 * math.sin(2 * p + 1.0)},
            "hair": {"rz": hair + 2.0 * math.sin(2 * p - 0.8)}}


# walk 40 and run 20, as the whole cast: a gliding walk, her gown to the ground; the run, leaning in, hair streaming back
M["W_1"] = loop(40, [(f, _walk(f, 40, 1.0, 0.3, 10.0, 2.0)) for f in range(0, 41, 4)], tracks=blinks("eyes", (22,)))
M["R_1"] = loop(20, [(f, _walk(f, 20, 4.0, 0.7, 24.0, 7.0)) for f in range(0, 21, 2)])

# talk: 14 frames, the hand she faces with open toward whoever listens
_T1 = {"arm_l": {"rz": -38.0}, "arm_r": {"rz": 6.0}}
M["T_1"] = loop(14, [(0, P(_T1, {"head": {"rz": -1.5}})),
                     (4, P(_T1, {"arm_l": {"rz": -46.0}, "head": {"rz": 2.0}, "hair": {"rz": 1.0}})),
                     (7, P(_T1, {"arm_l": {"rz": -34.0}, "head": {"rz": -0.5}})),
                     (11, P(_T1, {"arm_l": {"rz": -44.0}, "head": {"rz": 2.0}, "hair": {"rz": 1.0}})),
                     (14, P(_T1, {"head": {"rz": -1.5}}))],
                 tracks=talk("mouth", 14, every=3, talk_tex=OPEN))

# pleading: both hands reaching out to him, leaning in, her head tipped (loop 40)
_PLEAD = {"arm_l": {"rz": -72.0}, "arm_r": {"rz": -58.0}, "gown": {"rz": 1.5}, "head": {"rz": 8.0}, "hair": {"rz": 2.0}}
M["P_1"] = loop(40, [(0, _PLEAD), (10, P(_PLEAD, {"arm_l": {"rz": -76.0}, "head": {"rz": 9.0}, "hair": {"rz": 3.0}})),
                     (20, P(_PLEAD, {"arm_l": {"rz": -78.0}, "arm_r": {"rz": -64.0}, "head": {"rz": 10.0}})),
                     (30, P(_PLEAD, {"arm_r": {"rz": -62.0}, "head": {"rz": 9.0}, "hair": {"rz": 1.0}})), (40, _PLEAD)],
                tracks=merge_tracks(talk("mouth", 40, every=4, talk_tex=OPEN), blinks("eyes", (30,))))

# at Marron's side: arms round him, leaning in, eyes happy-shut (loop 60)
_SIDE = {"arm_l": {"rz": -88.0}, "arm_r": {"rz": -80.0}, "gown": {"rz": 2.0}, "head": {"rz": 12.0}, "eyes": {"tex": HAPPY},
         "mouth": {"tex": SMILE}, "hair": {"rz": 2.0}}
M["E_1"] = loop(60, [(0, _SIDE), (15, P(_SIDE, {"head": {"rz": 13.0}, "hair": {"rz": 3.0}})),
                     (30, P(_SIDE, {"head": {"rz": 14.0}, "gown": {"rz": 2.4}, "arm_l": {"rz": -90.0}})),
                     (45, P(_SIDE, {"head": {"rz": 13.0}, "hair": {"rz": 1.0}})), (60, _SIDE)])

# battle, at the back of the stage: worried, her hands clasped at her chest, a quick nervous sway (loop 60)
_WORRY = {"arm_l": {"rz": 62.0}, "arm_r": {"rz": -60.0}, "head": {"rz": 4.0}}
M["B_1"] = loop(60, [(0, P(_WORRY, {"gown": {"rz": -0.6}, "hair": {"rz": -1.0}})),
                     (15, P(_WORRY, {"gown": {"rz": 0.6}, "head": {"rz": 6.0}, "hair": {"rz": 1.0}})),
                     (30, P(_WORRY, {"gown": {"rz": -0.6}, "hair": {"rz": -1.0}})),
                     (45, P(_WORRY, {"gown": {"rz": 0.6}, "head": {"rz": 6.0}, "hair": {"rz": 1.0}})),
                     (60, P(_WORRY, {"gown": {"rz": -0.6}, "hair": {"rz": -1.0}}))],
                 tracks=blinks("eyes", (24,)))

# calling out: a hand up high, waving, her mouth open (loop 30)
_CALL = {"arm_l": {"rz": -142.0}, "arm_r": {"rz": 10.0}, "head": {"rz": -6.0}, "mouth": {"tex": OPEN}, "gown": {"rz": -1.0}}
M["Y_1"] = loop(30, [(0, _CALL), (8, P(_CALL, {"arm_l": {"rz": -158.0}})), (15, _CALL), (23, P(_CALL, {"arm_l": {"rz": -158.0}})),
                     (30, _CALL)])

# the tiara: her hand to her head, the tiara off, lifted high (24, held); then it glows (loop 24)
_HIGH = {"arm_l": {"rz": -136.0}, "arm_r": {"rz": 14.0}, "head": {"rz": -8.0}, "gown": {"rz": -1.2},
         "tiara": {"vis": 1, "rz": 136.0}, "crown": {"vis": 0}, "hair": {"rz": -1.5}}
M["TI_1"] = once(24, [(0, {}), (7, {"arm_l": {"rz": -165.0}, "head": {"rz": -3.0}}),
                      (8, {"arm_l": {"rz": -165.0}, "head": {"rz": -3.0}, "crown": {"vis": 0}, "tiara": {"vis": 1, "rz": 165.0}}, {"ease": "linear"}),
                      (18, P(_HIGH, {"arm_l": {"rz": -132.0}, "tiara": {"vis": 1, "rz": 132.0}})), (24, _HIGH)])
_GLOWING = P(_HIGH, {"tiara": {"vis": 1, "rz": 136.0, "tex": GLOW}})
M["TI_2"] = loop(24, [(0, _GLOWING), (12, P(_GLOWING, {"tiara": {"vis": 1, "rz": 136.0, "tex": GLOW, "sx": 1.125, "sy": 1.125}})),
                      (24, _GLOWING)])

# kneeling at Marron's side: down low, the gown pooled round her, her head bowed, a hand on him (loop 60)
_KNEEL = {"eclair": {"ty": -2.4}, "gown": {"sy": 0.875, "sx": 1.0625, "rz": 2.0}, "head": {"rz": 14.0}, "arm_l": {"rz": -48.0},
          "arm_r": {"rz": -26.0}, "hair": {"rz": 3.0}}
M["K_1"] = loop(60, [(0, _KNEEL), (15, P(_KNEEL, {"head": {"rz": 15.0}, "hair": {"rz": 4.0}})),
                     (30, P(_KNEEL, {"head": {"rz": 16.0}, "arm_l": {"rz": -52.0}})),
                     (45, P(_KNEEL, {"head": {"rz": 15.0}, "hair": {"rz": 2.0}})), (60, _KNEEL)],
                tracks=blinks("eyes", (20,), length=10))

for _name in ("Y_1", "TI_1", "TI_2"):  # a hand raised high passes in front of her head and hair, not behind them
    M[_name] = dict(M[_name], keys=[(k[0], P(k[1], {"arm_l": dict(k[1].get("arm_l", {}), tz=0.3)})) + tuple(k[2:])
                                    for k in M[_name]["keys"]])

POSES = {
    "S_1": "stand (loop 120)", "Z_1": "idle: as the stand", "T_1": "talking, a hand open (loop 14)",
    "W_1": "walk (loop 40)", "R_1": "run, to Marron (loop 20)", "P_1": "pleading, both hands out (loop 40)",
    "E_1": "at Marron's side, arms round him (loop 60)", "B_1": "battle: worried, hands clasped (loop 60)",
    "Y_1": "calling out, a hand up waving (loop 30)", "TI_1": "taking off the tiara and lifting it high (24, held)",
    "TI_2": "the tiara held high, glowing (loop 24)", "K_1": "kneeling at Marron's side (loop 60)",
}

ECLAIR = Rig("c_sl_eclair", "eclair/qwen", _layout(), M,
             camera={"height": 40.0, "width": 60.0, "center": (0.0, 18.0), "res": (480, 320)}, poses=POSES)
