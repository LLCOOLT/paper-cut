"""Chapter 3's props (Super Luigi; docs/super_luigi/ch3.md, New assets 5-7), from
the art in port/assets/super_luigi/art/ch3_props/ and scoreboard/ (sl_rig.py;
docs/LUIGI_ANIMS.md, "Chapter 3's props"). Each is a model of its own, put as
an NPC (or a map object's model) where the scene wants it:

- `c_sl_crown`: the King's crown, 8 x 6 units, its origin the middle of its
  base: on Revvo's head (`togenoko_ace`) at 1:1, the NPC placed on his head.
  Luigi's crowning wears the same card on his cap: `c_luigi_crown`
  (sl_crown_luigi.py).
- `c_sl_mic`: Lakky's microphone, its origin at the grip (the Lakitu's hand).
- `c_sl_lights`: the start lights, 10 x 5 units, hanging from their origin
  (the hooks, where the fishing rod's line ties on). One card, `lights`,
  whose drawing is the count: binds 0 off, 1, 2, 3 red, 4 green.
- `c_sl_scoreboard`: the big board on legs behind the first corner, 60 x 34
  units on legs 22 tall, its origin on the ground at its middle: `sign` (the
  mover, tipping about its feet: rx < 0 forward, toward the camera) > `legs`
  and `board` (hinged at its bottom edge (0, 22), to flex) > `board_face`
  and `board_back` (turned round, so it shows when the board lies face
  down). It tips over forward onto the pile.
"""
import math

from sl_rig import P, Rig, loop, once

ROOT = ("|root", None, None, 0.0, [], True, None)

# ------------------------------------------------------------------ crown ----
CROWN_LAYOUT = [ROOT, ("crown", "|root", "crown", 0.0, [], True, (0.0, 0.0))]
CR = {}
CR["S_1"] = loop(60, [(0, {}), (60, {})])
# P_1: put on: lowered onto the head from 6 above, a little settle (24, held)
CR["P_1"] = once(24, [(0, {"crown": {"ty": 6.0, "rz": 6.0}}), (14, {"crown": {"ty": 0.0, "rz": 0.0}}, {"ease": "vector"}),
                      (17, {"crown": {"sy": 0.875, "sx": 1.0625}}), (21, {"crown": {"sy": 1.0625}}), (24, {})])
# K_1: knocked off: flung up and back, spinning, falling out of sight (36, held)
CR["K_1"] = once(36, [(0, {}), (10, {"crown": {"tx": 5.0, "ty": 9.0, "rz": -200.0}}),
                      (24, {"crown": {"tx": 10.0, "ty": 2.0, "rz": -420.0}}, {"ease": "linear"}),
                      (34, {"crown": {"tx": 13.0, "ty": -8.0, "rz": -560.0}}),
                      (35, {"crown": {"tx": 13.0, "ty": -8.0, "rz": -560.0, "vis": 0}}),
                      (36, {"crown": {"tx": 13.0, "ty": -8.0, "rz": -560.0, "vis": 0}})])
CROWN_POSES = {"S_1": "still (loop 60)", "P_1": "put on: lowered onto the head, settling (24, held)",
               "K_1": "knocked off: flung up and back, spinning, gone (36, held)"}
CROWN = Rig("c_sl_crown", "ch3_props", CROWN_LAYOUT, CR,
            camera={"height": 20.0, "width": 24.0, "center": (0.0, -4.0), "res": (240, 200)}, poses=CROWN_POSES)

# -------------------------------------------------------------- microphone ----
MIC_LAYOUT = [ROOT, ("mic", "|root", "microphone", 0.0, [], True, (0.0, 0.0))]
MI = {}
MI["S_1"] = loop(60, [(0, {}), (30, {"mic": {"rz": 3.0}}), (60, {})])
# T_1: Lakky shouting into it, waving it (loop 16)
MI["T_1"] = loop(16, [(0, {"mic": {"rz": -10.0}}), (4, {"mic": {"rz": 6.0, "ty": 0.4}}), (8, {"mic": {"rz": -12.0}}),
                      (12, {"mic": {"rz": 8.0, "ty": 0.4}}), (16, {"mic": {"rz": -10.0}})])
MIC_POSES = {"S_1": "held (loop 60)", "T_1": "shouting into it, waving it (loop 16)"}
MIC = Rig("c_sl_mic", "ch3_props", MIC_LAYOUT, MI,
          camera={"height": 12.0, "width": 12.0, "center": (0.0, -6.0), "res": (160, 160)}, poses=MIC_POSES)

# ------------------------------------------------------------ start lights ----
LIGHTS_LAYOUT = [ROOT, ("lights", "|root", "lights_0", 0.0, ["lights_1", "lights_2", "lights_3", "lights_go"], True,
                        (0.0, 0.0))]


def _hang(f, tex, amp=2.0, period=60):
    return {"lights": {"rz": amp * math.sin(2 * math.pi * f / period), "tex": tex}}


LI = {}
for _n, _t in (("L_0", 0), ("L_1", 1), ("L_2", 2), ("L_3", 3), ("L_G", 4)):
    LI[_n] = loop(60, [(f, _hang(f, _t)) for f in (0, 15, 30, 45, 60)])
LI["S_1"] = LI["L_0"]


def _count():
    """C_1: the countdown: off, then a red light every 40 frames, each with a
    small jolt as it lights, then green (160, held green)"""
    keys = [(0, _hang(0, 0))]
    for k, f in enumerate((20, 60, 100, 140)):
        keys += [(f - 1, _hang(f - 1, k)), (f, P(_hang(f, k + 1), {"lights": {"sx": 1.0625, "sy": 1.0625}})),
                 (f + 4, _hang(f + 4, k + 1))]
    keys.append((160, _hang(160, 4)))
    return once(160, keys)


LI["C_1"] = _count()
LIGHTS_POSES = {"L_0": "off, hanging, swaying a little (loop 60)", "L_1": "one red (loop 60)",
                "L_2": "two red (loop 60)", "L_3": "three red (loop 60)", "L_G": "green: go (loop 60)",
                "S_1": "as L_0",
                "C_1": "the countdown: red at 20, 60 and 100, green at 140, each with a jolt (160, held green)"}
LIGHTS = Rig("c_sl_lights", "ch3_props", LIGHTS_LAYOUT, LI,
             camera={"height": 12.0, "width": 16.0, "center": (0.0, -10.0), "res": (256, 192)}, poses=LIGHTS_POSES)

# -------------------------------------------------------------- scoreboard ----
HINGE = (0.0, 22.0)
BOARD_LAYOUT = [
    ROOT,
    ("sign", "|root", None, 0.0, [], True, (0.0, 0.0)),
    ("legs", "sign", "legs", -0.6, [], True, None),
    ("board", "sign", None, 0.0, [], True, HINGE),
    ("board_back", "board", "board_back", -0.3, [], True, None),
    ("board_face", "board", "board_face", 0.0, [], True, None),
]
# the back card is turned round (sx -1, about the board's middle): it faces
# away, so it shows when the board lies face down
_BACK = {"board_back": {"sx": -1.0}}


def _tip(a, board=0.0, ty=0.0, rz=0.0):
    """the whole sign tipped forward a degrees about its feet (rx, toward the
    camera: in the game rx < 0 tips the top toward it), the board flexing on
    its hinge"""
    return P(_BACK, {"sign": {"rx": -a, "ty": ty}, "board": {"rx": -board, "rz": rz}})


SB = {}
SB["S_1"] = loop(60, [(0, _tip(0.0)), (60, _tip(0.0))])
SB["Z_1"] = SB["S_1"]
# W_1: shaken (the crash's pile-up hitting the legs): a wobble (loop 12)
SB["W_1"] = loop(12, [(0, _tip(0.0)), (3, _tip(2.0, 4.0, rz=1.0)), (6, _tip(-1.0, -2.0)),
                      (9, _tip(1.5, 3.0, rz=-1.0)), (12, _tip(0.0))])
# F_1: tipping over onto the pile: rocking, then falling forward face down,
# slamming onto the pile at 60 degrees, a bounce, the board flexing (48,
# held). F_2: lying on the pile (loop 60). Revvo is buried under it.
SB["F_1"] = once(48, [(0, _tip(0.0)), (4, _tip(4.0, 2.0)), (8, _tip(-2.0, -1.0)), (12, _tip(7.0, 3.0)),
                      (24, _tip(35.0, -4.0), {"ease": "linear"}), (30, _tip(60.0, 6.0), {"ease": "vector"}),
                      (34, _tip(53.0, -3.0, ty=0.5)), (38, _tip(60.0, 2.0), {"ease": "vector"}),
                      (42, _tip(58.0, 0.0)), (48, _tip(60.0, 0.0))])
SB["F_2"] = loop(60, [(0, _tip(60.0)), (60, _tip(60.0))])
BOARD_POSES = {"S_1": "standing (loop 60)", "W_1": "shaken: a wobble (loop 12)",
               "F_1": "tipping over forward onto the pile: rocking, falling face down, slamming onto the pile at 60 "
                      "degrees, a bounce (48, held)",
               "F_2": "lying on the pile, face down (loop 60)"}
SCOREBOARD = Rig("c_sl_scoreboard", "scoreboard", BOARD_LAYOUT, SB,
                 camera={"height": 64.0, "width": 72.0, "center": (0.0, -8.0), "res": (360, 320)}, poses=BOARD_POSES)

# ----------------------------------------------------------------- trophy ----
# The Grand Prix's trophy on the awards stage and at the crowning, the compass
# piece standing on its lid (a card of its own, so Luigi can take it off).
TROPHY_LAYOUT = [ROOT, ("trophy", "|root", "trophy", 0.0, [], True, (0.0, 0.0)),
                 ("piece", "|root", "trophy_piece", 0.1, [], True, (0.0, 13.6))]
TR = {}
# S_1: on show, the piece catching the light: a slow bob of the piece (loop 60)
TR["S_1"] = loop(60, [(0, {"piece": {"ty": 0.0}}), (30, {"piece": {"ty": 0.25, "rz": 2.0}}), (60, {"piece": {"ty": 0.0}})])
# S_2: the lid bare, the piece taken (loop 60)
TR["S_2"] = loop(60, [(0, {"piece": {"vis": 0}}), (60, {"piece": {"vis": 0}})])
# P_1: the piece lifted off the lid and away up out of sight (24, held)
TR["P_1"] = once(24, [(0, {}), (8, {"piece": {"ty": 3.0, "rz": -8.0}}), (20, {"piece": {"ty": 9.0, "rz": 10.0, "sx": 0.5, "sy": 0.5}}),
                      (21, {"piece": {"vis": 0}}), (24, {"piece": {"vis": 0}})])
TROPHY_POSES = {"S_1": "on show, the piece on its lid (loop 60)", "S_2": "the lid bare (loop 60)",
                "P_1": "the piece lifted off and away (24, held)"}
TROPHY = Rig("c_sl_trophy", "ch3_props", TROPHY_LAYOUT, TR,
             camera={"height": 20.0, "width": 20.0, "center": (0.0, -2.0), "res": (200, 200)}, poses=TROPHY_POSES)

# ------------------------------------------------------- steering wheel ----
WHEEL_LAYOUT = [ROOT, ("wheel", "|root", "wheel", 0.0, [], True, (0.0, 3.0))]
WH = {}
WH["S_1"] = loop(60, [(0, {}), (60, {})])
# T_1: turning on its hook (loop 120)
WH["T_1"] = loop(120, [(0, {"wheel": {"rz": -6.0}}), (60, {"wheel": {"rz": 6.0}}), (120, {"wheel": {"rz": -6.0}})])
WHEEL = Rig("c_sl_wheel", "ch3_props", WHEEL_LAYOUT, WH,
            camera={"height": 10.0, "width": 10.0, "center": (0.0, -2.0), "res": (120, 120)},
            poses={"S_1": "still (loop 60)", "T_1": "swinging a little on its hook (loop 120)"})

# ------------------------------------------------------------------- tarp ----
TARP_LAYOUT = [ROOT, ("tarp", "|root", "tarp", 0.0, [], True, (0.0, 0.0))]
TA = {}
TA["S_1"] = loop(90, [(0, {}), (45, {"tarp": {"sy": 1.02}}), (90, {})])
# K_1: whisked off: up, back and away, folding (30, held)
TA["K_1"] = once(30, [(0, {}), (8, {"tarp": {"ty": 6.0, "rz": 6.0, "sy": 0.9}}),
                      (20, {"tarp": {"ty": 16.0, "tx": 12.0, "rz": 20.0, "sx": 0.6, "sy": 0.5}}),
                      (24, {"tarp": {"ty": 18.0, "tx": 14.0, "rz": 24.0, "sx": 0.5, "sy": 0.4, "vis": 0}}),
                      (30, {"tarp": {"vis": 0}})])
TARP = Rig("c_sl_tarp", "ch3_props", TARP_LAYOUT, TA,
           camera={"height": 24.0, "width": 44.0, "center": (0.0, -2.0), "res": (352, 192)},
           poses={"S_1": "draped over the kart (loop 90)", "K_1": "whisked off (30, held)"})

# ---------------------------------------------------------------- shutter ----
SHUTTER_LAYOUT = [ROOT, ("shutter", "|root", "shutter", 0.0, [], True, (0.0, 30.0))]
SH = {}
# S_1: jammed: a judder now and then (loop 90)
SH["S_1"] = loop(90, [(0, {}), (70, {}), (73, {"shutter": {"ty": 0.6, "rz": 0.6}}), (76, {"shutter": {"ty": -0.2}}),
                      (79, {"shutter": {"ty": 0.4, "rz": -0.4}}), (82, {}), (90, {})])
# O_1: rolling up into its housing (40, held)
SH["O_1"] = once(40, [(0, {}), (4, {"shutter": {"ty": 0.8}}), (36, {"shutter": {"sy": 0.05}}, {"ease": "linear"}),
                      (37, {"shutter": {"sy": 0.05, "vis": 0}}), (40, {"shutter": {"vis": 0}})])
SHUTTER = Rig("c_sl_shutter", "ch3_props", SHUTTER_LAYOUT, SH,
              camera={"height": 36.0, "width": 40.0, "center": (0.0, -2.0), "res": (320, 288)},
              poses={"S_1": "jammed, juddering now and then (loop 90)", "O_1": "rolling up (40, held)"})
