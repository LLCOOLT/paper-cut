"""Minister Crepe (Super Luigi: chapter 6's villain; docs/super_luigi/ch6.md), `c_sl_crepe`: cards cut from the style
model's design split into pieces by Qwen-Image-Edit (port/assets/super_luigi/art/tools/cut_crepe.py, art/crepe/qwen/), on
a rig of his own, keyed by hand to MANIFESTO.md section 8's numbers. Faces -x, about 37 units to the top of his hat. His
face swaps are overlays of Qwen's own blinking, talking and gold-eyed heads. The drawn man (crepe_ink.py) is CREPE_INK.

Thin, folded and polite: `top` is everything above his waist, and it folds at the waist, which is how he bows. Parts:
crepe (the mover) > foot_front, foot_back, robe > top > cape, arm_back, roll, head > (hat, eyes, nose, tache, mouth),
arm_front > hand (binds 0 open, 1 finger, 2 fist, 3 palm) > scroll, compass (hidden).
Eyes: binds 0 open, 1 shut, 2 gold. Mouth: 0 shut, 1 open.
"""
import math

from sl_rig import P, Rig, blinks, loop, merge_tracks, once, talk

HAND_OPEN, HAND_POINT, HAND_FIST, HAND_PALM = 0, 1, 2, 3
SHUT, GOLD = 1, 2

LAYOUT = [
    ("|root", None, None, 0.0, [], True, None),
    ("crepe", "|root", None, 0.0, [], True, (0.0, 0.0)),
    ("foot_back", "crepe", "foot_back", -0.3, [], True, None),
    ("robe", "crepe", "robe", 0.0, [], True, None),
    ("foot_front", "crepe", "foot_front", 0.05, [], True, None),
    ("top", "robe", None, 0.0, [], True, (0.0, 12.0)),
    ("cape", "top", "cape", -0.6, [], True, None),
    ("arm_back", "top", "arm_back", -0.4, [], True, None),
    ("arm_front", "top", "arm_front", 0.2, [], True, None),
    ("hand", "arm_front", "hand_open", 0.22, ["hand_point", "hand_fist", "hand_palm"], True, (-1.0, 13.0)),
    ("scroll", "hand", "scroll", 0.24, [], False, (-1.0, 9.6)),
    ("compass", "hand", "compass", 0.24, [], False, (-1.0, 9.2)),
    ("roll", "top", "roll", 0.3, [], True, None),
    ("head", "top", "head", 0.4, [], True, None),
    ("hat", "head", "hat", 0.46, [], True, None),
    ("eyes", "head", "eyes", 0.42, ["eyes_shut", "eyes_gold"], True, (-2.8, 26.4)),
    ("mouth", "head", "mouth_shut", 0.42, ["mouth"], True, (-4.0, 22.6)),
    ("nose", "head", "nose", 0.45, [], True, (-4.6, 24.6)),
    ("tache", "head", "tache", 0.44, [], True, (-4.0, 23.2)),
]


def _q_layout():
    """the same groups for the cards cut from the model's own pieces (art/tools/cut_crepe.py, art/crepe/qwen/): every card
    where the cut put it; his round coat bows whole, from the hips (`top`); his face is the head card's own, so the face
    groups hold blank cards; the far hand is its own card"""
    import json
    import os
    from sl_rig import ART
    meta = json.load(open(os.path.join(ART, "crepe", "qwen", "parts.json")))
    top = tuple(meta["anchors"]["top"])
    hx, hy = meta["cards"]["hand_open"]["model_xy"]
    return [
        ("|root", None, None, 0.0, [], True, None),
        ("crepe", "|root", None, 0.0, [], True, (0.0, 0.0)),
        ("foot_back", "crepe", "foot_back", -0.3, [], True, None),
        ("foot_front", "crepe", "foot_front", -0.2, [], True, None),
        ("top", "crepe", None, 0.0, [], True, top),
        ("robe", "top", "robe", 0.0, [], True, None),
        ("cape", "top", None, -0.6, [], True, top),
        ("arm_back", "top", "arm_back", -0.4, [], True, None),
        ("hand_b", "arm_back", "hand_back", -0.38, [], True, None),
        ("arm_front", "top", "arm_front", 0.2, [], True, None),
        ("hand", "arm_front", "hand_open", 0.22, ["hand_point", "hand_fist", "hand_palm"], True, None),
        ("scroll", "hand", "scroll", 0.24, [], False, (hx, hy - 3.4)),
        ("compass", "hand", "compass", 0.24, [], False, (hx, hy - 3.8)),
        ("roll", "top", "roll", 0.3, [], True, None),
        ("head", "top", "head", 0.4, [], True, None),
        ("hat", "head", "hat", 0.46, [], True, None),
        ("eyes", "head", "eyes", 0.42, ["eyes_shut", "eyes_gold"], True, None),
        ("mouth", "head", "mouth_shut", 0.42, ["mouth"], True, None),
        ("nose", "head", "nose", 0.45, [], True, None),
        ("tache", "head", "tache", 0.44, [], True, None),
    ]


M = {}


def _sway(f):
    p = 2 * math.pi * f / 120.0
    return {"top": {"rz": 1.6 * math.sin(p)}, "head": {"rz": 3.0 * math.sin(p + 0.8)},
            "cape": {"rz": 2.5 * math.sin(p + 1.6)}, "arm_front": {"rz": -7.0 + 3.0 * math.sin(p + 2.2)},
            "arm_back": {"rz": -3.0 - 3.0 * math.sin(p + 0.4)}, "hat": {"rz": 1.2 * math.sin(p + 2.8)},
            "tache": {"rz": 1.0 * math.sin(2 * p)}}


# stand: a long, pompous, polite sway; hands in front of the coat; blinks out of step with it
M["S_1"] = loop(120, [(f, _sway(f)) for f in range(0, 121, 15)], tracks=blinks("eyes", (46, 98)))
M["Z_1"] = M["S_1"]


def _walk(f, end, lean, stride, bob, arm):
    p = 2 * math.pi * f / end
    s = math.sin(p)
    return {"crepe": {"ty": bob * abs(math.sin(p))},
            "top": {"rz": lean + 1.5 * math.sin(2 * p)},
            "foot_front": {"rz": -stride * 5.0 * s},
            "foot_back": {"rz": stride * 5.0 * s},
            "arm_front": {"rz": -8.0 + arm * s}, "arm_back": {"rz": -4.0 - arm * s},
            "cape": {"rz": 3.0 * math.sin(p - 0.9) - lean * 0.5}, "head": {"rz": -lean * 0.4 + 1.5 * math.sin(2 * p + 1.0)},
            "hat": {"rz": 1.2 * math.sin(2 * p + 1.8)}}


# walk 40 and run 20: as the whole cast. A glide with his nose in the air; the run, leaning into it, the cape out behind
M["W_1"] = loop(40, [(f, _walk(f, 40, 3.0, 2.8, 0.35, 9.0)) for f in range(0, 41, 4)])
M["W_2"] = loop(40, [(f, _walk(f, 40, 6.0, 2.0, 0.5, 5.0)) for f in range(0, 41, 4)])  # down the stairs, looking at his feet
M["R_1"] = loop(20, [(f, P(_walk(f, 20, 12.0, 3.6, 0.8, 26.0), {"cape": {"rz": -12.0 + 4.0 * math.sin(2 * math.pi * f / 20)}}))
                     for f in range(0, 21, 2)])

# talk: 14 frames, a finger up, the moustache going
_T1 = {"arm_front": {"rz": -92.0}, "hand": {"tex": HAND_POINT}}
M["T_1"] = loop(14, [(0, P(_T1, {"head": {"rz": -2.0}, "top": {"rz": -0.8}})),
                     (4, P(_T1, {"arm_front": {"rz": -104.0}, "head": {"rz": 2.0}, "top": {"rz": 0.8}, "tache": {"rz": 4.0}})),
                     (7, P(_T1, {"arm_front": {"rz": -88.0}, "head": {"rz": -1.0}, "tache": {"rz": -3.0}})),
                     (11, P(_T1, {"arm_front": {"rz": -100.0}, "head": {"rz": 2.0}, "tache": {"rz": 3.0}})),
                     (14, P(_T1, {"head": {"rz": -2.0}, "top": {"rz": -0.8}}))],
                 tracks=talk("mouth", 14, every=3, talk_tex=1))
# talking smugly, both hands rubbing together at the coat
M["T_2"] = loop(14, [(0, {"arm_front": {"rz": -48.0}, "arm_back": {"rz": -52.0}, "head": {"rz": 4.0}, "top": {"rz": 3.0}}),
                     (4, {"arm_front": {"rz": -54.0, "ty": 0.3}, "arm_back": {"rz": -48.0}, "head": {"rz": 6.0}, "top": {"rz": 4.0}, "tache": {"rz": 4}}),
                     (7, {"arm_front": {"rz": -48.0}, "arm_back": {"rz": -54.0, "ty": 0.3}, "head": {"rz": 4.0}, "top": {"rz": 3.0}}),
                     (11, {"arm_front": {"rz": -54.0, "ty": 0.3}, "arm_back": {"rz": -48.0}, "head": {"rz": 6.0}, "top": {"rz": 4.0}, "tache": {"rz": -3}}),
                     (14, {"arm_front": {"rz": -48.0}, "arm_back": {"rz": -52.0}, "head": {"rz": 4.0}, "top": {"rz": 3.0}})],
                 tracks=talk("mouth", 14, every=3, talk_tex=1))

# a slow clap (loop 40): the hands meet in front, apart, meet
_OPEN = {"arm_front": {"rz": -74.0}, "arm_back": {"rz": -44.0}, "head": {"rz": 4.0}, "hand": {"tex": HAND_PALM}}
_SHUT = {"arm_front": {"rz": -58.0}, "arm_back": {"rz": -64.0}, "head": {"rz": 6.0}, "top": {"rz": 4.0}, "hand": {"tex": HAND_PALM}}
M["CL_1"] = loop(40, [(0, _OPEN), (15, _SHUT, {"ease": "vector"}), (19, _SHUT), (36, _OPEN), (40, _OPEN)])

# holding out the letter; presenting the compass's base: a folded bow from the waist, palm up (held)
_OFFER = {"arm_front": {"rz": -84.0}, "head": {"rz": 6.0}, "top": {"rz": 8.0}, "hand": {"tex": HAND_PALM}, "scroll": {"vis": 1, "rz": 84.0}}
M["I_1"] = once(24, [(0, {"scroll": {"vis": 1}}), (10, _OFFER), (24, _OFFER)])
_PRESENT = {"arm_front": {"rz": -92.0}, "arm_back": {"rz": -92.0}, "head": {"rz": 6.0}, "top": {"rz": 24.0}, "hat": {"rz": 3.0},
            "hand": {"tex": HAND_PALM}, "compass": {"vis": 1, "rz": 92.0}, "cape": {"rz": -8.0}}
M["I_2"] = once(36, [(0, {"compass": {"vis": 1}}), (12, P(_PRESENT, {"top": {"rz": 30.0}})), (20, _PRESENT), (36, _PRESENT)])

# the pull: his hand out, the compass flies to him; he snatches it and draws it in. Then looking into it, eyes gold
M["PU_1"] = once(40, [
    (0, {}),
    (8, {"arm_front": {"rz": -98.0}, "top": {"rz": -4.0}, "head": {"rz": -6.0}, "hand": {"tex": HAND_PALM}}),
    (20, {"arm_front": {"rz": -104.0}, "top": {"rz": -6.0}, "head": {"rz": -8.0}, "hand": {"tex": HAND_PALM}}),
    (24, {"arm_front": {"rz": -92.0}, "top": {"rz": 4.0}, "hand": {"tex": HAND_PALM}, "compass": {"vis": 1, "rz": 92.0}}, {"ease": "vector"}),
    (34, {"arm_front": {"rz": -62.0}, "head": {"rz": 12.0}, "hand": {"tex": HAND_PALM}, "compass": {"vis": 1, "rz": 62.0}}),
    (40, {"arm_front": {"rz": -62.0}, "head": {"rz": 12.0}, "hand": {"tex": HAND_PALM}, "compass": {"vis": 1, "rz": 62.0}}),
])
_GAZE = {"arm_front": {"rz": -72.0}, "head": {"rz": 14.0}, "eyes": {"tex": GOLD}, "hand": {"tex": HAND_PALM},
         "compass": {"vis": 1, "rz": 72.0}, "top": {"rz": 6.0}}
M["G_1"] = loop(40, [(0, _GAZE), (20, P(_GAZE, {"head": {"rz": 16.0}, "top": {"rz": 8.0}})), (40, _GAZE)])

# ---- battle: frames of impact as the battle scripts wait on them (Flambé's flame at 4, Fold's hit at 6, Batter's at 4, 10, 16,
# Compass Bolt's at 4), everything snappy ----
# Flambé: the fan back (16, held), swept forward (18)
_BACK = {"arm_front": {"rz": 118.0}, "top": {"rz": -9.0}, "head": {"rz": -7.0}, "hand": {"tex": HAND_PALM}}
M["A_1A"] = once(16, [(0, {}), (10, _BACK), (16, P(_BACK, {"arm_front": {"rz": 124.0}, "top": {"rz": -12.0}}))])
_SWEEP = {"arm_front": {"rz": -108.0}, "top": {"rz": 14.0}, "head": {"rz": 6.0}, "mouth": {"tex": 1}, "hand": {"tex": HAND_PALM}, "cape": {"rz": -12.0}}
M["A_1B"] = once(18, [(0, P(_BACK, {"arm_front": {"rz": 124.0}, "top": {"rz": -12.0}})), (4, _SWEEP, {"ease": "vector"}),
                      (10, P(_SWEEP, {"arm_front": {"rz": -98.0}, "top": {"rz": 10.0}})), (18, {})])
# Fold: gathering the cape (20, held); flung round, wrapping (hit at 6), shaken back (22)
_GATHER = {"cape": {"rz": 42.0}, "arm_back": {"rz": 60.0}, "top": {"rz": -7.0}, "head": {"rz": -4.0}}
M["A_2A"] = once(20, [(0, {}), (12, P(_GATHER, {"cape": {"rz": 38.0}})), (20, _GATHER)])
_WRAP = {"cape": {"rz": -92.0, "tz": 1.6}, "arm_back": {"rz": -100.0}, "top": {"rz": 10.0}, "mouth": {"tex": 1}}
M["A_2B"] = once(22, [(0, _GATHER), (6, _WRAP, {"ease": "vector"}), (14, P(_WRAP, {"cape": {"rz": -86.0, "tz": 1.6}})),
                      (18, {"cape": {"rz": 10.0}, "top": {"rz": -2.0}}), (22, {})])
# Batter: arms up (16, held); both fists, three times
_UP = {"arm_front": {"rz": -168.0}, "arm_back": {"rz": -158.0}, "top": {"rz": -7.0}, "head": {"rz": -6.0}, "hand": {"tex": HAND_FIST}}
_DOWN = {"arm_front": {"rz": -64.0}, "arm_back": {"rz": -58.0}, "top": {"rz": 16.0}, "head": {"rz": 8.0}, "mouth": {"tex": 1}, "hand": {"tex": HAND_FIST}}
M["A_3A"] = once(16, [(0, {}), (10, _UP), (16, _UP)])
M["A_3B"] = once(22, [(0, _UP), (4, _DOWN, {"ease": "vector"}), (7, _UP), (10, _DOWN, {"ease": "vector"}), (13, _UP),
                      (16, _DOWN, {"ease": "vector"}), (22, {})])
# Cabinet Reshuffle: a paper turn, an arm swept up, a deep folded bow (48)
_BOW = {"arm_front": {"rz": 40.0}, "top": {"rz": 42.0}, "head": {"rz": 6.0}, "hat": {"rz": 6.0}, "cape": {"rz": -10.0}}
M["A_4"] = once(48, [(0, {}), (6, {"crepe": {"sx": 0.0625}, "arm_front": {"rz": -40.0}}, {"ease": "linear"}),
                     (12, {"crepe": {"sx": -1.0}, "arm_front": {"rz": -120.0}}, {"ease": "linear"}),
                     (18, {"crepe": {"sx": 0.0625}, "arm_front": {"rz": -160.0}}, {"ease": "linear"}),
                     (24, {"crepe": {"sx": 1.0}, "arm_front": {"rz": -170.0}, "head": {"rz": -6.0}}),
                     (34, _BOW), (42, _BOW), (48, {})])
# Compass Bolt: raising it, eyes gold (loop 20); released at 4 (18)
_RAISE = {"arm_front": {"rz": -150.0}, "head": {"rz": -10.0}, "eyes": {"tex": GOLD}, "hand": {"tex": HAND_PALM},
          "compass": {"vis": 1, "rz": 150.0}, "top": {"rz": -4.0}}
M["A_5A"] = loop(20, [(0, _RAISE), (10, P(_RAISE, {"compass": {"vis": 1, "rz": 150.0, "sx": 1.125, "sy": 1.125}, "top": {"rz": -6.0}})), (20, _RAISE)])
M["A_5B"] = once(18, [(0, _RAISE), (4, P(_RAISE, {"arm_front": {"rz": -112.0}, "compass": {"vis": 1, "rz": 112.0}, "top": {"rz": 8.0}}), {"ease": "vector"}),
                      (10, P(_RAISE, {"arm_front": {"rz": -102.0}, "compass": {"vis": 1, "rz": 102.0}})),
                      (18, {"compass": {"vis": 1}})])
# the sidestep (Foresight): a quick hop back and aside, smug (24, held there)
M["E_1"] = once(24, [(0, {}), (4, {"crepe": {"tx": 4.0, "ty": 1.5}, "top": {"rz": -12.0}}),
                     (8, {"crepe": {"tx": 8.0, "ty": 0.0}, "top": {"rz": -4.0}}, {"ease": "vector"}),
                     (16, {"crepe": {"tx": 8.0}, "head": {"rz": 6.0}, "tache": {"rz": 8.0}, "arm_front": {"rz": -30.0}}),
                     (24, {"crepe": {"tx": 8.0}, "head": {"rz": 2.0}, "arm_front": {"rz": -26.0}})])
# hurt (loop 10): folded backward, eyes shut, the hat askew
_HIT = {"top": {"rz": -18.0}, "head": {"rz": -9.0}, "eyes": {"tex": SHUT}, "mouth": {"tex": 1}, "hat": {"rz": -12.0, "tx": 0.6},
        "arm_front": {"rz": 44.0}, "arm_back": {"rz": 30.0}, "hand": {"tex": HAND_OPEN}, "cape": {"rz": 10.0}}
M["D_1"] = loop(10, [(0, {"eyes": {"tex": SHUT}}), (3, _HIT, {"ease": "vector"}), (7, P(_HIT, {"top": {"rz": -11.0}, "head": {"rz": -6.0}})), (10, {"eyes": {"tex": SHUT}})])
# unfolding into the Nightmare (the battle swaps the model at its end): a shudder, gold eyes, the cape rising like wings, tall (60, held)
_RISE = {"robe": {"sy": 1.5, "sx": 0.875}, "cape": {"rz": 88.0, "sy": 1.5, "sx": 1.25}, "arm_front": {"rz": -170.0}, "arm_back": {"rz": 170.0},
         "head": {"rz": -14.0}, "eyes": {"tex": GOLD}, "hat": {"ty": 1.5}}
M["D_2"] = once(60, [(0, {"eyes": {"tex": GOLD}}),
                     (8, {"top": {"rz": 3.0}, "head": {"rz": -6.0}, "eyes": {"tex": GOLD}}),
                     (12, {"top": {"rz": -3.0}, "head": {"rz": 6.0}, "eyes": {"tex": GOLD}}),
                     (16, {"top": {"rz": 3.0}, "head": {"rz": -6.0}, "eyes": {"tex": GOLD}}),
                     (30, {"robe": {"sy": 1.25}, "cape": {"rz": 68.0, "sy": 1.25}, "arm_front": {"rz": -150.0}, "arm_back": {"rz": 150.0},
                           "head": {"rz": -10.0}, "eyes": {"tex": GOLD}}),
                     (50, _RISE), (60, _RISE)])
# after it all, kneeling small: folded right over, head bowed, the hat sliding over his eyes (loop 60)
_KNEEL = {"crepe": {"ty": -0.6, "sx": 0.875, "sy": 0.875}, "top": {"rz": 34.0}, "foot_front": {"rz": 40.0, "ty": 0.8}, "foot_back": {"rz": 40.0, "ty": 0.8},
          "head": {"rz": 16.0}, "eyes": {"tex": SHUT}, "hat": {"ty": -1.2, "rz": 6.0}, "arm_front": {"rz": -30.0}, "arm_back": {"rz": -25.0}, "cape": {"rz": -14.0}}
M["K_1"] = loop(60, [(0, _KNEEL), (30, P(_KNEEL, {"top": {"rz": 38.0}})), (60, _KNEEL)])
# defeated: flat on his back (40, held)
_FLAT = {"crepe": {"rz": -90.0, "ty": 6.0, "tx": 2.0}, "eyes": {"tex": SHUT}, "arm_front": {"rz": 80.0}, "arm_back": {"rz": -60.0}, "hat": {"tx": -3.0, "rz": 50.0}}
M["D_3"] = once(40, [(0, {"eyes": {"tex": SHUT}}),
                     (10, {"crepe": {"rz": -20.0}, "top": {"rz": -8.0}, "eyes": {"tex": SHUT}, "arm_front": {"rz": 60.0}}),
                     (22, P(_FLAT, {"hat": {"tx": -2.0, "rz": 30.0}}), {"ease": "vector"}),
                     (27, P(_FLAT, {"crepe": {"rz": -86.0, "ty": 6.5, "tx": 2.0}})),
                     (32, _FLAT, {"ease": "vector"}), (40, _FLAT)])

RAISED = ("T_1", "T_2", "CL_1", "I_1", "I_2", "PU_1", "G_1", "A_1B", "A_3A", "A_3B", "A_4", "A_5A", "A_5B")
for _name in RAISED:  # an arm raised forward passes in front of his head, not behind it
    for _key in M[_name]["keys"]:
        _key[1].setdefault("arm_front", {})
    M[_name] = dict(M[_name], keys=[(k[0], P(k[1], {"arm_front": dict(k[1].get("arm_front", {}), tz=0.5)})) + tuple(k[2:]) for k in M[_name]["keys"]])

POSES = {
    "S_1": "stand (loop 120)", "Z_1": "idle: as the stand",
    "T_1": "talking, a finger raised (loop 14)", "T_2": "talking smugly, rubbing his hands (loop 14)",
    "W_1": "a glide, nose up (loop 40)", "R_1": "hurrying (loop 20)", "W_2": "walking down stairs (loop 40)",
    "CL_1": "a slow clap (loop 40)", "I_1": "holding out the letter (24, held)",
    "I_2": "presenting the compass's base with a bow (36, held)",
    "PU_1": "the pull: hand out, the compass flies to him (40, held, the compass in hand)",
    "G_1": "looking into the compass, eyes gold (loop 40)",
    "A_1A": "Flambé, the wind-up (16, held)", "A_1B": "Flambé, the fan swept (the flame at 4; 18)",
    "A_2A": "Fold, gathering the cape (20, held)", "A_2B": "Fold, the cape flung round (hit at 6; 22)",
    "A_3A": "Batter, arms up (16, held)", "A_3B": "Batter, three pounds (hits at 4, 10, 16; 22)",
    "A_4": "Cabinet Reshuffle: a turn, a flourish, a bow (48)",
    "A_5A": "Compass Bolt, charging, eyes gold (loop 20)", "A_5B": "Compass Bolt, released (at 4; 18)",
    "E_1": "the sidestep (Foresight): a hop 8 units back (24, held there)",
    "D_1": "hurt (loop 10)", "D_2": "unfolding into the Nightmare (60, held)",
    "K_1": "kneeling small, after it all (loop 60)", "D_3": "defeated, flat on his back (40, held)",
}

# the drawn man (crepe_ink.py), kept for reference; not packed
CREPE_INK = Rig("c_sl_crepe", "crepe/ink", LAYOUT, M,
                camera={"height": 38.0, "width": 60.0, "center": (2.0, 16.0), "res": (480, 304)}, poses=POSES)
# the man from the model's own pieces (art/tools/cut_crepe.py), picked over the drawn one
CREPE = Rig("c_sl_crepe", "crepe/qwen", _q_layout(), M,
            camera={"height": 40.0, "width": 60.0, "center": (0.0, 18.0), "res": (480, 320)}, poses=POSES)
