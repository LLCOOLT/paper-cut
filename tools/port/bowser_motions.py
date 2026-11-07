"""Bowser's new motions for Play as (runs inside Blender through
char_anims.py; docs/PLAY_AS.md). The format is char_motions.py's; Mario's
names and timing.

Bowser's rig (c_koopa; he faces -x):
  locator2   the whole body (pivot at his middle); L_foot (front), R_foot
             (back): the feet, children of it
  body       belly and all above (pivot at the hips): koura1/2 the shell
             (toge* its spikes), teil1 > teil2 the tail
  L_udewa    front shoulder (spiked band) > arme (arm; 0 bare, 1 bent) >
             L_hand (0 claws, 1 claws spread, 2 a fist, 3 thumb out)
  R_udewa    back shoulder > arme1 > R_hand (0 claws, 1 a fist)
  head       (pivot at the neck): eye (0 glaring, 1 a happy arc, 2 angry,
             3 wide, 4 shut), L_mayu R_mayu brows, hear hair, horns, and
             locator3 the jaw: lip > kuti, yoko_ago > ago, sita_kuti
  Hammer_pc  the hammer (at 1.6 Luigi's size: he is drawn at 0.7), a child
             of the front hand, its handle in his fist
His back view (c_koopa_r_pc, bowser_back.py) is the same rig with his
parts placed as seen from behind (the face hidden, the shell's other half
and more spikes copies): Mario's back-facing animations play there, ours
(MOTIONS_R) and his disc walk, run, stand and jump (luigi_pc.c kBowserMap).
"""
from char_motions import (Rig, levels, field_hammer_name, merged, held, tracks, steps, blink_pairs, talk_pairs,
                          slowed, hammer_swing_pts, battle_hammer_set)

RIG = Rig("c_koopa_pc", "bowser", ["locator2", "body", "L_udewa", "arme", "L_hand"])
FRONT_ARM = 118.0  # his front arm's direction at rest (0 up, 90 his front), shoulder to fist
BACK_ARM = 113.0


def P(*parts):
    return merged(*parts)


def arms(front=None, back=None, bend=0.0, root=0.0, hf=None, hb=None):
    """the arms pointing in world directions (as hw), the body bent `bend`
    and the whole turned `root`; hf, hb: the hands' drawings"""
    p = {}
    if front is not None:
        p["L_udewa"] = {"rz": front - FRONT_ARM - bend - root}
    if back is not None:
        p["R_udewa"] = {"rz": back - BACK_ARM - bend - root}
    if hf is not None:
        p.setdefault("L_hand", {})["tex"] = hf
    if hb is not None:
        p.setdefault("R_hand", {})["tex"] = hb
    return p


def bend(t, head=0.0):
    """leaning in from the hips by t degrees (+ toward his front), the head nodding `head` more"""
    p = {"body": {"rz": t}}
    if head:
        p["head"] = {"rz": head}
    return p


def jaw(o, eyes=None):
    """the mouth opened by o (1: a roar)"""
    p = {"ago": {"rz": 24.0 * o}, "yoko_ago": {"rz": -26.0 * o}, "sita_kuti": {"rz": 14.0 * o},
         "lip": {"rz": 4.0 * o}}
    if eyes is not None:
        p["eye"] = {"tex": eyes}
    return p


def feet(front=(0.0, 0.0, 0.0), back=(0.0, 0.0, 0.0)):
    """(tx, ty, rz) of each foot"""
    return {"L_foot": {"tx": front[0], "ty": front[1], "rz": front[2]},
            "R_foot": {"tx": back[0], "ty": back[1], "rz": back[2]}}


def tail(t):
    return {"teil1": {"rz": t}, "teil2": {"rz": t * 0.6}}


def hammer(h, tex):
    return {"Hammer_pc": {"hw": h, "vis": 1, "tex": tex}}


MOTIONS = {}

# ------------------------------------------------ the field hammer swing
# (M_H_1 / M_H_4 / M_H_7, Mario's 10 frames.) Hauled back over his shoulder
# with a growl, then brought down with his whole weight, a stamp of the front
# foot and a roar, the tail swinging up behind.


def _swing(h, tex, lean, back, o=0.0, eyes=2, step=0.0, t=0.0, lift=0.0):
    p = P(bend(lean, lean * 0.3), arms(h, back, lean, hf=2), hammer(h, tex), jaw(o, eyes),
          feet((-step, 0.0, -step * 3), (0.0, 0.0, 0.0)), tail(t))
    if lift:
        p["locator2"] = {"ty": lift}
    return p


MOTIONS["M_H_1"] = {"end": 10, "keys": [
    (0, _swing(-55, 0, -8, 70, 0.2, t=-4)),
    (1, _swing(-25, 0, -12, 60, 0.3, t=-6, lift=0.3)),
    (2, _swing(10, 0, -9, 70, 0.3, t=-4, lift=0.6)),
    (3, _swing(45, 2, 0, 110, 0.5, t=4, lift=0.6)),
    (4, _swing(85, 2, 10, 160, 0.8, step=1.5, t=10, lift=0.2), {"ease": "linear"}),
    (5, P(_swing(116, 3, 20, 196, 1.0, step=3.0, t=16), {"locator2": {"sy": 0.9375}}), {"ease": "vector"}),
    (6, _swing(112, 3, 18, 192, 0.9, step=3.0, t=14)),
    (8, _swing(114, 3, 19, 194, 0.9, step=3.0, t=15)),
    (10, _swing(114, 3, 19, 194, 0.8, step=3.0, t=15)),
]}
# hitting a wall: stopped short up in front, jolted back, eyes shut
MOTIONS["M_H_2"] = {"end": 4, "keys": MOTIONS["M_H_1"]["keys"][:4] + [
    (4, _swing(40, 2, -6, 90, 0.6, eyes=4, t=-6, lift=0.4), {"ease": "vector"})]}

# ------------------------------------------------ spin jump (mot_hip.c)
# Into his shell: head and limbs drawn in, curled forward (M_A_1A, held);
# landing out of it (M_A_1B).
_SHELL = P(bend(40, 10), {"head": {"rz": 10, "tx": 7.0, "ty": -6.0, "tz": -1.6, "sx": 0.75, "sy": 0.75}},
           arms(185, 175, 40, hf=2, hb=1), {"L_udewa": {"tz": -2.6, "rz": 185 - 118 - 40, "tx": 4.0, "ty": -2.0}},
           feet((6.0, 7.0, 70.0), (5.0, 7.0, 60.0)), {"L_foot": {"tx": 6.0, "ty": 7.0, "rz": 70.0, "tz": -0.6}},
           tail(-30), jaw(0.0, 4), {"locator2": {"ty": 2.0, "sy": 0.9375}})
MOTIONS["M_A_1A"] = {"end": 2, "keys": [(0, _SHELL), (2, _SHELL)]}
MOTIONS["M_A_1B"] = {"end": 4, "keys": [
    (0, _SHELL), (2, _SHELL),
    (4, P(bend(8, 4), arms(140, 120, 8, hf=1, hb=0), feet((0.6, 1.0, 10.0), (0.4, 1.0, 8.0)), jaw(0.4, 2),
          {"locator2": {"sy": 0.9375}})),
]}

# ------------------------------------------------ holding up an item (M_I_1, 4 frames, held)
# Thrust up high in his fist, his head back, a toothy grin.
_HOLD = P(bend(-12, -8), arms(-4, 150, -12, hf=2), {"L_udewa": {"rz": -4 - FRONT_ARM + 12, "ty": 1.5}},
          jaw(0.6, 1), tail(8), {"locator2": {"sy": 1.0625}})
MOTIONS["M_I_1"] = {"end": 4, "keys": [
    (0, P(bend(-2, -4), arms(60, 130, -2, hf=2), jaw(0.3, 0))),
    (2, P(_HOLD, {"locator2": {"ty": 0.8, "sy": 1.125}})),
    (4, _HOLD)]}

# ------------------------------------------------ battle (as "<Mario's name>@b")

# guard, ready (M_C_3, a 180-frame loop): hunched forward, claws out, a slow
# heavy breath
_GUARD = P(bend(10, -4), arms(100, 95, 10, hf=1, hb=0), jaw(0.15, 2), feet((-1.0, 0.0, -4.0)))
MOTIONS["M_C_3@b"] = {"end": 180, "loop": True, "keys": [
    (0, _GUARD),
    (90, P(_GUARD, bend(7, -3), arms(104, 99, 7, hf=1, hb=0), jaw(0.3, 2), {"locator2": {"sy": 1.0625}})),
    (180, _GUARD)]}

# bouncing off an enemy after a stomp (M_J_1D, 6 frames)
MOTIONS["M_J_1D@b"] = {"end": 6, "keys": [
    (0, P({"locator2": {"sy": 0.875}}, bend(8), arms(150, 140, 8), feet((0.5, 1.0, 10.0), (0.5, 1.0, 10.0)))),
    (3, P({"locator2": {"sy": 1.0625}}, bend(-6, -6), arms(40, 30, -6, hf=1, hb=0), jaw(0.8, 3),
          feet((0.2, 2.0, 30.0), (-1.0, -2.0, -20.0)), tail(10))),
    (6, P(bend(-4, -4), arms(55, 45, -4, hf=1, hb=0), jaw(0.6, 0), feet((0.2, 2.0, 30.0), (-1.0, -2.0, -20.0)),
          tail(8)))]}

# a stomp attack in the air: going up (M_A_3A, held), arms up for the stomp (M_A_3B)
_UP = P(bend(-4, -6), arms(70, 60, -4, hf=1, hb=0), jaw(0.5, 2), feet((0.2, 3.0, 40.0), (-2.0, -3.0, -30.0)))
MOTIONS["M_A_3A@b"] = {"end": 0, "keys": [(0, _UP)]}
MOTIONS["M_A_3B@b"] = {"end": 4, "keys": [
    (0, _UP),
    (2, P(bend(-8, -8), arms(10, 0, -8, hf=2, hb=1), jaw(1.0, 2), feet((0.5, 3.5, 45.0), (-1.0, 2.0, 20.0)),
          tail(12))),
    (4, P(bend(-8, -8), arms(14, 4, -8, hf=2, hb=1), jaw(0.9, 2), feet((0.5, 3.5, 45.0), (-1.0, 2.0, 20.0)),
          tail(12)))]}

# the battle hammer: Mario's hammer angles at each of his keys (and his
# drawing), Bowser's arm on the line of it, leaning his bulk into it


def _battle_swing(end, pts, loop=False, add=0, slam=False):
    keys = []
    for f, hw, shown, tex in hammer_swing_pts(pts):
        lean = max(-12.0, min(18.0, (((hw + 180) % 360) - 180) / 6.0))
        k = _swing(hw, tex + add, lean, 120.0 + lean * 4, max(0.2, lean / 18.0),
                   step=3.0 if lean > 12 else 0.0, t=lean * 0.8)
        if not shown:  # thrown: his claws open after it
            k = P(k, {"Hammer_pc": {"hw": hw, "vis": 0, "tex": tex + add}}, arms(hf=1))
        if slam and hw > 0:  # the big overhead blow: his whole weight off the ground and into it
            t = min(1.0, hw / 100.0)
            k = P(k, {"locator2": {"ty": round(1.6 * t, 2)}}, bend(lean + 10 * t, (lean + 10 * t) * 0.3),
                  arms(hw, 120.0 + lean * 4 + 20 * t, lean + 10 * t, hf=2), tail(lean * 0.8 + 10 * t))
        keys.append((f, k))
    return {"end": end, "loop": loop, "keys": keys}


battle_hammer_set(MOTIONS, _battle_swing)

# the Super and Ultra Hammer's versions; the back-facing names, front-on
levels(MOTIONS, ["M_H_1", "M_H_2"], rename=field_hammer_name)

# ------------------------------------------------ batch 3: the rest of the field set
# (luigi_motions.py has what each of Mario's is for. His disc set covers some,
# luigi_pc.c kBowserMap: getting up, keeling over, lying dazed and asleep.)

EYE = [("eye", "tex")]
GLARE, HAPPY, ANGRY, WIDE, SHUT = 0, 1, 2, 3, 4


def head_track(pts):
    """his head nodding (+ down to his front) through (frame, degrees)"""
    return {"head": {"rz": list(pts)}}


def blinks(end, *pairs, base=GLARE):
    return steps(EYE, blink_pairs(*pairs, shut=SHUT, open_=base), base, end)


def talking(times, end, nod=None, eyes=GLARE):
    """the jaw working at Mario's mouth flaps: keys, open at each even time"""
    keys = [(0, P(jaw(0.1, eyes), bend(0, 0)))]
    for i, t in enumerate(times):
        keys.append((t, P(jaw(0.6 if i % 2 == 0 else 0.1, eyes), bend(0, nod(t) if nod else 0.0))))
    if keys[-1][0] != end:
        keys.append((end, keys[-1][1]))
    return {"end": end, "keys": keys}


def stomp(end, step, lift=2.5, bob=0.6, sway=3.0):
    """his feet stamping in turn every `step` frames, a heavy bob and the tail swinging"""
    n = int(round(end / step))
    return {"L_foot": {"ty": [(i * step, lift if i % 4 == 1 else 0.0) for i in range(n + 1)],
                       "rz": [(i * step, -12.0 if i % 4 == 1 else 0.0) for i in range(n + 1)]},
            "R_foot": {"ty": [(i * step, lift if i % 4 == 3 else 0.0) for i in range(n + 1)],
                       "rz": [(i * step, -12.0 if i % 4 == 3 else 0.0) for i in range(n + 1)]},
            "locator2": {"ty": [(i * step, bob if i % 2 else 0.0) for i in range(n + 1)]},
            "teil1": {"rz": [(i * step, sway if i % 4 == 1 else -sway if i % 4 == 3 else 0.0) for i in range(n + 1)]}}


def spun(t):
    """the whole of him turned t degrees about his middle (locator2's pivot)"""
    return {"locator2": {"rz": t}}


# the spin hammer (M_H_3 crouched to wind up, M_H_3A braced, M_H_3B whirled
# round flat out, dizzy)
_WIND = P(bend(14, 6), arms(150, 140, 14, hf=2, hb=1), jaw(0.3, ANGRY), feet((-1.5, 0.0, -6.0), (1.0, 0.0, 4.0)),
          tail(-6), {"locator2": {"sy": 0.9375}})
_BRACE = P(bend(-6, -4), arms(160, 150, -6, hf=2, hb=1), jaw(0.8, SHUT), feet((-2.0, 0.0, -8.0), (1.5, 0.0, 6.0)),
           tail(8))
_DIZZY = P(_SHELL, jaw(0.5, SHUT))  # (whirled round in his shell, as his spin jump)
MOTIONS["M_H_3"] = {"end": 2, "keys": [(0, _WIND), (2, _WIND)]}
MOTIONS["M_H_3A"] = {"end": 2, "keys": [(0, _BRACE), (2, _BRACE)]}
MOTIONS["M_H_3B"] = {"end": 2, "keys": [(0, _DIZZY), (2, _DIZZY)]}
for _n in ("M_H_3", "M_H_3A", "M_H_3B"):
    for _lv in (1, 2):
        MOTIONS[field_hammer_name(_n, _lv)] = MOTIONS[_n]

# something catches his eye (M_I_Y, 8 frames, held): a jolt, eyes wide
_START = P({"locator2": {"ty": 0.8, "sy": 1.0625}}, bend(-6, -8), arms(70, 60, -6, hf=1, hb=0), jaw(0.5, WIDE),
           tail(10))
MOTIONS["M_I_Y"] = {"end": 8, "keys": [(0, {}), (3, _START), (8, P(bend(-3, -4), arms(95, 90, -3, hf=1), jaw(0.3, WIDE)))]}

# riding Yoshi (M_Y_1, M_Y_2; M_Y_3 the flutter): sat up astride his back,
# his legs hanging down either side, claws forward on his neck, his tail up
# off the ground behind. The game sets the rider a few units over the floor,
# where Mario sits; Bowser is three times Yoshi's size, so he sits higher
# and further back, for Yoshi's head and legs to show before and under him
_SEAT = {"locator2": {"ty": 1.5, "tx": 5.0}}
_RIDE = P(_SEAT, bend(-2, -4), arms(100, 95, -2, hf=0, hb=0), jaw(0.3, HAPPY),
          feet((-3.0, -0.5, -14.0), (-1.5, -0.5, -8.0)), tail(4))
MOTIONS["M_Y_1"] = {"end": 0, "keys": [(0, _RIDE)]}
MOTIONS["M_Y_2"] = {"end": 0, "loop": True, "keys": [(0, _RIDE)]}
MOTIONS["M_Y_3"] = {"end": 0, "keys": [(0, P(_RIDE, _SEAT, arms(40, 30, -2, hf=1, hb=0),
                                             jaw(0.9, WIDE), tail(10)))]}

# crouching to spring (M_P_1, 8 frames), and springing, a fist high (M_P_2)
MOTIONS["M_P_1"] = {"end": 8, "keys": [
    (0, {}),
    (4, P(bend(10, 4), {"locator2": {"ty": -1.0, "sy": 0.9375}}, arms(140, 130, 10), feet((-1.0, 0.0, -6.0)))),
    (8, P(bend(16, 6), {"locator2": {"ty": -1.8, "sy": 0.875}}, arms(150, 140, 16, hf=2, hb=1), jaw(0.2, SHUT),
          feet((-1.5, 0.0, -10.0), (1.0, 0.0, 6.0)), tail(-8)))]}
_SPRING = P({"locator2": {"ty": 1.0, "sy": 1.0625}}, bend(-8, -8), arms(5, 150, -8, hf=2, hb=1), jaw(0.8, ANGRY),
            feet((0.5, 3.0, 30.0), (-1.0, -2.0, -20.0)), tail(14))
MOTIONS["M_P_2"] = {"end": 0, "keys": [(0, _SPRING)]}

# arms up, overhead (lifting Bobbery; mot_jabara.c): both claws high
_UP = P(arms(8, 2, hf=1, hb=0), bend(-4, -6), jaw(0.4, ANGRY))
MOTIONS["M_U_2"] = {"end": 10, "keys": [
    (0, P(bend(10, 4), {"locator2": {"sy": 0.9375}}, arms(140, 130, 10))),
    (4, P({"locator2": {"sy": 1.0625}}, arms(60, 55, hf=1))),
    (7, P(_UP, {"locator2": {"ty": 0.3, "sy": 1.0625}})), (10, _UP)]}
# from behind his arms hang behind his shell (bowser_back.py): lifted, his
# shoulders rise with them, so his claws show high over his hair
_UP_R = P(_UP, {"L_udewa": {"ty": 9.0, "tx": 3.0, "rz": 8 - FRONT_ARM + 4 - 10.0},
                "R_udewa": {"ty": 9.0, "tx": -1.0, "rz": 2 - BACK_ARM + 4 + 12.0}})
# (Mario's from behind is 8 frames, his front 10: the same lift, quicker)
MOTIONS["M_U_2R"] = {"end": 8, "keys": [
    (0, P(bend(10, 4), {"locator2": {"sy": 0.9375}}, arms(140, 130, 10))),
    (3, P({"locator2": {"sy": 1.0625}}, arms(60, 55, hf=1), {"L_udewa": {"ty": 4.0}, "R_udewa": {"ty": 4.0}})),
    (6, P(_UP_R, {"locator2": {"ty": 0.3, "sy": 1.0625}})), (8, _UP_R)]}
MOTIONS["M_W_7"] = {"end": 40, "loop": True, "tracks": tracks(held(_UP, 40), stomp(40, 10))}
MOTIONS["M_S_4"] = {"end": 2, "keys": [(0, _UP), (2, _UP)]}
MOTIONS["M_W_6"] = {"end": 32, "loop": True, "tracks": tracks(held(_UP, 32), stomp(32, 8))}
MOTIONS["M_J_5A"] = {"end": 2, "keys": [(0, _SPRING), (2, _SPRING)]}
MOTIONS["M_J_5B"] = {"end": 14, "keys": [
    (0, P(_UP, {"locator2": {"sy": 0.9375}})),
    (5, P(_UP, {"locator2": {"ty": 2.0, "sy": 1.0625}}, feet((0.5, 3.0, 30.0), (-1.0, -2.0, -20.0)), jaw(0.9, ANGRY))),
    (10, P(_UP, {"locator2": {"ty": 0.8}}, feet((0.2, 1.5, 15.0), (-0.5, -1.0, -10.0)), jaw(0.7, ANGRY))),
    (14, _UP)]}

# a throw (M_C_1, 8 frames; also battle's and the back-facing one): claw back
# over his shoulder, a heave over the top, his weight forward on the front foot
_C1 = {"end": 8, "keys": [
    (0, P(bend(-8, -4), arms(-30, 130, -8, hf=2), jaw(0.3, ANGRY), tail(-6))),
    (2, P(bend(-6, -4), arms(-10, 130, -6, hf=2), {"locator2": {"sy": 1.0625}}, jaw(0.4, ANGRY))),
    (3, P(bend(4, 2), arms(50, 140, 4, hf=2), jaw(0.6, ANGRY)), {"ease": "linear"}),
    (5, P(bend(16, 6), arms(108, 160, 16, hf=1), jaw(1.0, ANGRY), feet((-3.0, 0.0, -9.0)), tail(14),
          {"locator2": {"sy": 0.9375}}), {"ease": "vector"}),
    (8, P(bend(14, 5), arms(104, 156, 14, hf=1), jaw(0.8, ANGRY), feet((-3.0, 0.0, -9.0)), tail(12)))]}
MOTIONS["M_C_1"] = _C1
MOTIONS["M_C_1@b"] = _C1
MOTIONS["M_C_1R"] = _C1

# shading his eyes to look far off (M_Z_9, 2 frames, held)
MOTIONS["M_Z_9"] = {"end": 2, "keys": [(0, {}), (2, P(bend(4, -8), arms(48, None, 4, hf=0), jaw(0.1, GLARE)))]}

# low on HP (M_S_3, a 120-frame loop): a tired slump, head down, panting
_LOW = P(bend(8, 6), arms(150, 140, 8), jaw(0.4, SHUT), tail(-4))
MOTIONS["M_S_3"] = {"end": 120, "loop": True, "keys": [
    (0, _LOW), (30, P(_LOW, bend(5, 4), jaw(0.2, SHUT))), (60, P(_LOW, bend(10, 8))),
    (90, P(_LOW, bend(5, 4), jaw(0.2, SHUT))), (120, _LOW)]}

# in peril (M_I_S, a 96-frame loop, field and battle): hunched low, gasping
_TIRED = P(bend(16, 10), arms(160, 150, 16), jaw(0.7, SHUT), tail(-8), {"locator2": {"sy": 0.9375}})
MOTIONS["M_I_S"] = {"end": 96, "loop": True, "keys": [
    (0, _TIRED), (24, P(_TIRED, bend(12, 7), jaw(0.4, SHUT), {"locator2": {"sy": 1.0}})), (48, P(_TIRED, bend(17, 11))),
    (72, P(_TIRED, bend(12, 7), jaw(0.4, SHUT), {"locator2": {"sy": 1.0}})), (96, _TIRED)]}
MOTIONS["M_I_S@b"] = MOTIONS["M_I_S"]

# pushing (M_O_1 a shoulder to it, M_O_2 both claws flat; 32-frame loops)
_PUSH1 = P(bend(18, -6), arms(100, 140, 18, hf=1), jaw(0.5, ANGRY))
_PUSH2 = P(bend(22, -8), arms(92, 88, 22, hf=1, hb=0), jaw(0.6, ANGRY))
MOTIONS["M_O_1"] = {"end": 32, "loop": True, "tracks": tracks(held(_PUSH1, 32), stomp(32, 8, lift=1.6, bob=0.3))}
MOTIONS["M_O_2"] = {"end": 32, "loop": True, "tracks": tracks(held(_PUSH2, 32), stomp(32, 8, lift=1.6, bob=0.3))}
MOTIONS["M_O_2R"] = MOTIONS["M_O_2"]

# a claw scratching his head (M_B_1, 6 frames); claws together (M_B_2, M_B_3)
MOTIONS["M_B_1"] = {"end": 6, "keys": [(0, {}), (3, P(arms(20, None, hf=1), bend(0, 6), jaw(0.2, HAPPY))),
                                     (6, P(arms(26, None, hf=1), bend(0, 5), jaw(0.2, HAPPY)))]}
_CLASP = P(arms(100, 96, hf=2, hb=1), bend(2, 3))
MOTIONS["M_B_2"] = {"end": 2, "keys": [(0, _CLASP), (2, _CLASP)]}
MOTIONS["M_B_3"] = {"end": 2, "keys": [(0, P(_CLASP, bend(2, -4))), (2, P(_CLASP, bend(2, -4)))]}

# thinking it over, claw to his chin (M_X_1, a 64-frame loop)
_CHIN = P(arms(62, 150, hf=3), jaw(0.0))
MOTIONS["M_X_1"] = {"end": 64, "loop": True, "tracks": tracks(
    held(_CHIN, 64), head_track([(0, -6.0), (16, -10.0), (32, -6.0), (48, -10.0), (64, -6.0)]),
    blinks(64, (40, 42)))}

# a fist by his face (M_I_U, held)
MOTIONS["M_I_U"] = {"end": 0, "keys": [(0, P(arms(50, None, hf=2), bend(0, -4), jaw(0.3, ANGRY)))]}

# a fist to the crowd (M_C_2, 36 frames, and battle's M_C_2C): a roar and a
# stamp before settling
_CROWD = P(bend(-8, -10), arms(10, 140, -8, hf=2), jaw(1.0, ANGRY), feet((-2.0, 0.0, -8.0)), tail(10))
MOTIONS["M_C_2"] = {"end": 36, "keys": [
    (0, _CROWD, {"ease": "vector"}),
    (7, P(_CROWD, bend(-4, -6), arms(16, 140, -4, hf=2), jaw(0.8, ANGRY))),
    (14, P(_CROWD, bend(2, -4), arms(4, 140, 2, hf=2), jaw(1.0, ANGRY))),
    (22, P(_CROWD, bend(3, -2), arms(14, 140, 3, hf=2), jaw(0.6, HAPPY))),
    (29, P(_CROWD, bend(2, -2), arms(8, 140, 2, hf=2), jaw(0.7, HAPPY))),
    (36, {})]}

# riding along with Flurrie (M_A_2A 12 frames, M_A_2B a 4-frame loop, M_A_2C
# 8): claws together, feet off the ground as she carries him
_CARRIED = P(_CLASP, {"locator2": {"sy": 1.0625}}, feet((0.5, 2.0, 15.0), (0.5, 2.0, 12.0)), tail(-10))
MOTIONS["M_A_2A"] = {"end": 12, "keys": [(0, {}), (12, _CARRIED)]}
MOTIONS["M_A_2B"] = {"end": 4, "loop": True, "keys": [
    (0, _CARRIED), (2, P(_CARRIED, {"locator2": {"sy": 1.0}}, feet((0.5, 2.6, 18.0), (0.5, 2.6, 15.0)))),
    (4, _CARRIED)]}
MOTIONS["M_A_2C"] = {"end": 8, "keys": [(0, _CARRIED), (8, {})]}

# a victory fist (M_V_2, 6 frames, field and battle)
_FIST = P({"locator2": {"ty": 0.4, "sy": 1.0625}}, bend(-6, -8), arms(10, 140, -6, hf=2), jaw(0.9, HAPPY), tail(8))
MOTIONS["M_V_2"] = {"end": 6, "keys": [(0, arms(110, None, hf=2)), (3, _FIST, {"ease": "vector"}),
                                     (6, P(_FIST, arms(16, 140, -6, hf=2)))]}

# hurt and worried (M_D_6, an 8-frame loop): his disc hurt pose is kept for
# the hit (M_D_1); this one cowers, claws up by his face, a bob
_COWER = P(bend(-8, -6), arms(56, 40, -8, hf=1, hb=0), jaw(0.7, SHUT), tail(-10))
MOTIONS["M_D_6"] = {"end": 8, "loop": True, "keys": [
    (0, _COWER), (4, P(_COWER, bend(-10, -7), {"locator2": {"sy": 0.9375}})), (8, _COWER)]}

# ------------------------------------------- batch 4: event poses (e_mario)
# Mario's timing to the frame: his head turns where Mario's face tilts, his
# jaw works and his eyes shut on Mario's mouth and blink frames.

MOTIONS["M_N_1"] = {"end": 12, "tracks": head_track([(0, 0.0), (12, -7.0)])}
MOTIONS["M_N_2"] = {"end": 18, "tracks": head_track([
    (0, 0.0), (2, 12.0), (4, 12.0), (7, -1.2), (9, 0.0), (11, 12.0), (13, 12.0), (16, -1.2), (18, 0.0)])}
MOTIONS["M_N_3"] = {"end": 32, "tracks": tracks(head_track([(0, -2.0), (3, -3.0), (6, -5.0), (32, -5.0)]),
                                                blinks(32, (20, 22), (26, 28)))}
MOTIONS["M_N_B"] = talking([4, 6, 10, 12, 16, 18, 22, 24, 28, 30, 34], 34,
                           nod=lambda t: [2.0, -1.0][(t // 6) % 2])
MOTIONS["M_N_C"] = talking(list(range(2, 71, 2)), 70, nod=lambda t: [2.0, -1.5][(t // 10) % 2])
MOTIONS["M_I_N"] = {"end": 120, "loop": True, "tracks": tracks(head_track([(0, -3.0), (120, -3.0)]),
                                                               blinks(120, (20, 22), (26, 28)))}
# hangs his head (M_I_Z, 12 frames)
MOTIONS["M_I_Z"] = {"end": 12, "keys": [(0, {}), (12, P(bend(6, 14), arms(140, 130, 6), jaw(0.0, SHUT), tail(-8),
                                                         {"locator2": {"sy": 0.9375}}))]}
# fists clenched, determined (M_I_G, 6 frames)
_DETERMINED = P(bend(6, -4), arms(100, 96, 6, hf=2, hb=1), jaw(0.3, ANGRY), {"locator2": {"sy": 1.0625}})
MOTIONS["M_I_G"] = {"end": 6, "keys": [(0, {}), (2, _DETERMINED), (6, P(_DETERMINED, bend(5, -4)))]}
# a claw to his mouth (M_N_10, 6 frames)
_GASP = P(bend(-4, 6), arms(56, None, -4, hf=1), jaw(0.4, WIDE))
MOTIONS["M_N_10"] = {"end": 6, "keys": [(0, {}), (3, P(_GASP, bend(-4, 4))), (6, _GASP)]}
# thinking, claw to his chin (M_I_5, 50 frames)
MOTIONS["M_I_5"] = {"end": 50, "tracks": tracks(held(_CHIN, 50), head_track([
    (0, -10.0), (4, -12.0), (8, -10.0), (12, -12.0), (16, -10.0), (20, -12.0), (24, -10.0), (28, -12.0),
    (32, -10.0), (36, -12.0), (40, -10.0), (42, -9.0), (50, -9.0)]))}
# marching on the spot (M_N_7, an 18-frame loop)
MOTIONS["M_N_7"] = {"end": 18, "loop": True, "tracks": tracks(stomp(18, 4.5, lift=3.0), {
    "L_udewa": {"rz": [(0, -40.0), (4.5, 20.0), (9, -40.0), (13.5, 20.0), (18, -40.0)]},
    "R_udewa": {"rz": [(0, 20.0), (4.5, -40.0), (9, 20.0), (13.5, -40.0), (18, 20.0)]}},
    head_track([(0, -6.0), (18, -6.0)]))}
# stamping, furious (M_I_O, a 24-frame loop): quick stamps, fists down
_ST = [i * 1.5 for i in range(17)]
MOTIONS["M_I_O"] = {"end": 24, "loop": True, "tracks": tracks(
    held(P(arms(150, 140, hf=2, hb=1), jaw(0.8, ANGRY)), 24),
    {"L_foot": {"ty": [(t, 2.0 if i % 2 else 0.0, "linear") for i, t in enumerate(_ST)]},
     "R_foot": {"ty": [(t, 0.0 if i % 2 else 2.0, "linear") for i, t in enumerate(_ST)]},
     "locator2": {"ty": [(t, 0.6 if i % 2 else 0.0, "linear") for i, t in enumerate(_ST)]}},
    head_track([(0, 4.0), (3, 7.0), (6, 4.0), (9, 7.0), (12, 4.0), (15, 7.0), (18, 4.0), (21, 7.0), (24, 4.0)]))}
# jumps in surprise (M_N_5B, 32 frames)
MOTIONS["M_N_5B"] = {"end": 32, "keys": [
    (0, P({"locator2": {"ty": -0.6, "sy": 0.875}}, jaw(0.2, WIDE))),
    (2, P({"locator2": {"sy": 1.0625}}, arms(70, 60, hf=1, hb=0), jaw(0.8, WIDE))),
    (8, P({"locator2": {"ty": 3.0, "sy": 1.125}}, bend(-6, -6), arms(30, 20, -6, hf=1, hb=0), jaw(1.0, WIDE),
          feet((0.5, 3.0, 30.0), (-1.0, -2.0, -20.0)), tail(14))),
    (13, P({"locator2": {"ty": 0.8, "sy": 1.0625}}, bend(-3, -3), arms(50, 40, -3, hf=1, hb=0), jaw(0.8, WIDE),
           feet((0.2, 1.5, 15.0), (-0.5, -1.0, -10.0)), tail(10))),
    (15, P({"locator2": {"ty": -0.4, "sy": 0.875}}, arms(90, 80, hf=1), jaw(0.6, WIDE)), {"ease": "vector"}),
    (16, P({"locator2": {"sy": 0.9375}}, arms(100, 95, hf=1), jaw(0.5, WIDE))),
    (32, jaw(0.2, WIDE))]}
# standing, breathing (M_N_5A, a 140-frame loop): a slow heave
MOTIONS["M_N_5A"] = {"end": 140, "loop": True, "tracks": {
    "body": {"rz": [(0, 0.0), (26, 3.0), (50, 4.0), (74, 2.0), (110, 0.8), (140, 0.0)]},
    "head": {"rz": [(0, 0.0), (50, -2.0), (110, 0.0), (140, 0.0)]},
    "teil1": {"rz": [(0, 0.0), (60, 4.0), (140, 0.0)]}}}
# fists together, ready (M_C_3, 16 frames)
_READY = P({"locator2": {"ty": 0.4, "sy": 1.0625}}, bend(4, -6), arms(96, 92, 4, hf=2, hb=1), jaw(0.3, ANGRY))
MOTIONS["M_C_3"] = {"end": 16, "keys": [(0, P(arms(110, 105, hf=2, hb=1), bend(0, -4))), (10, _READY), (16, _READY)]}
# braced, fists low (M_N_9, 2 frames, held)
_LOWFISTS = P(bend(8, 2), {"locator2": {"ty": -0.8, "sy": 0.9375}}, arms(150, 140, 8, hf=2, hb=1), jaw(0.3, ANGRY),
              feet((-2.0, 0.0, -8.0), (1.5, 0.0, 6.0)))
MOTIONS["M_N_9"] = {"end": 2, "keys": [(0, _LOWFISTS), (2, _LOWFISTS)]}
# mid-stride, held (M_N_H, 2 frames; M_F_2)
_STRIDE = P({"locator2": {"ty": 0.6}}, bend(6, 2), arms(90, 160, 6), feet((-3.0, 2.0, -20.0), (2.0, 0.0, 10.0)),
            tail(6))
MOTIONS["M_N_H"] = {"end": 2, "keys": [(0, _STRIDE), (2, _STRIDE)]}
MOTIONS["M_F_2"] = MOTIONS["M_N_H"]
# sitting on the ground (M_S_5, a 180-frame loop; M_N_11, looking about)
_SIT = P({"locator2": {"ty": -6.0}}, bend(-8, 4), arms(140, 130, -8), feet((-6.0, 5.0, -50.0), (-5.0, 4.5, -45.0)),
         tail(-16))
MOTIONS["M_S_5"] = {"end": 180, "loop": True, "keys": [
    (0, _SIT), (90, P(_SIT, bend(-9, 3), {"locator2": {"ty": -6.0, "sy": 1.0625}})), (180, _SIT)]}
MOTIONS["M_N_11"] = {"end": 18, "keys": [(0, _SIT), (6, P(_SIT, bend(-8, 12))), (12, P(_SIT, bend(-8, -4))), (18, _SIT)]}
MOTIONS["M_D_7B"] = {"end": 8, "keys": [
    (0, P(_SIT, jaw(0.6, SHUT))), (4, P(_SIT, bend(-8, -6), jaw(0.3, WIDE))), (8, P(_SIT, bend(-8, -4), jaw(0.2, WIDE)))]}
# eating (M_L_2, M_L_3, 130-frame loops): claws to his mouth, chomping
_EAT = P(arms(58, 50, hf=1, hb=0), bend(0, 4), jaw(0.2, HAPPY))
MOTIONS["M_L_2"] = {"end": 130, "loop": True, "keys": [
    (t, P(_EAT, jaw(0.7 if i % 2 else 0.1, HAPPY), bend(0, 4 + 3 * (i % 2)))) for i, t in enumerate(range(0, 131, 10))]}
MOTIONS["M_L_3"] = MOTIONS["M_L_2"]
MOTIONS["M_L_4"] = {"end": 0, "keys": [(0, _EAT)]}
# hopping for joy (M_F_1, a 16-frame loop): two heavy hops, fists up in turn
MOTIONS["M_F_1"] = {"end": 16, "loop": True, "keys": [
    (0, P({"locator2": {"sy": 0.9375}}, arms(20, 120, hf=2), jaw(0.9, HAPPY))),
    (4, P({"locator2": {"ty": 2.0, "sy": 1.0625}}, arms(10, 100, hf=2), jaw(1.0, HAPPY),
          feet((0.5, 2.0, 20.0), (0.5, 2.0, 15.0)), tail(10))),
    (8, P({"locator2": {"sy": 0.9375}}, arms(120, 20, hb=1), jaw(0.9, HAPPY))),
    (12, P({"locator2": {"ty": 2.0, "sy": 1.0625}}, arms(100, 10, hb=1), jaw(1.0, HAPPY),
           feet((0.5, 2.0, 20.0), (0.5, 2.0, 15.0)), tail(10))),
    (16, P({"locator2": {"sy": 0.9375}}, arms(20, 120, hf=2), jaw(0.9, HAPPY)))]}
# a wind-up and throw (M_W_4B, 8 frames)
MOTIONS["M_W_4B"] = {"end": 8, "keys": [
    (0, P(bend(-8, -2), arms(-20, None, -8, hf=2))), (3, P(bend(-12, -3), arms(-40, None, -12, hf=2))),
    (5, P(bend(12, 3), arms(100, None, 12, hf=1), jaw(0.9, ANGRY), feet((-3.0, 0.0, -9.0))), {"ease": "vector"}),
    (8, P(bend(8, 2), arms(110, None, 8, hf=1), jaw(0.6, ANGRY), feet((-3.0, 0.0, -9.0))))]}
# the rest of e_mario's: a march (M_W_4A, a 40-frame loop) and its end
# (M_W_4C), standing (M_I_7, M_I_M), pointing ahead (M_L_1), walking with
# something held in front (M_W_5, a 40-frame loop)
MOTIONS["M_W_4A"] = {"end": 40, "loop": True, "tracks": tracks(stomp(40, 10), {
    "L_udewa": {"rz": [(0, -30.0), (10, 20.0), (20, -30.0), (30, 20.0), (40, -30.0)]},
    "R_udewa": {"rz": [(0, 20.0), (10, -30.0), (20, 20.0), (30, -30.0), (40, 20.0)]}})}
MOTIONS["M_W_4C"] = {"end": 6, "keys": [(0, _STRIDE), (6, {})]}
MOTIONS["M_I_7"] = {"end": 0, "keys": [(0, {})]}
MOTIONS["M_I_M"] = {"end": 0, "keys": [(0, bend(0, -3))]}
MOTIONS["M_L_1"] = {"end": 0, "keys": [(0, P(bend(4, -2), arms(90, None, 4, hf=3), jaw(0.5, ANGRY)))]}
MOTIONS["M_W_5"] = {"end": 40, "loop": True, "tracks": tracks(held(arms(96, 92, hf=0, hb=0), 40), stomp(40, 10))}

# ------------------------------------------------ batch 5: the rest of battle

for _n in ("M_N_2", "M_I_Y", "M_I_O", "M_N_C", "M_A_1A", "M_A_1B", "M_D_6"):
    MOTIONS[_n + "@b"] = MOTIONS[_n]
MOTIONS["M_N_5@b"] = MOTIONS["M_N_5A"]
MOTIONS["M_P_1@b"] = MOTIONS["M_P_1"]
MOTIONS["M_P_2@b"] = MOTIONS["M_P_2"]

# a cheer, a fist up for a moment (M_N_8, 60 frames)
MOTIONS["M_N_8@b"] = {"end": 60, "keys": [
    (0, {}), (10, _FIST, {"ease": "vector"}), (45, P(_FIST, arms(16, 140, -6, hf=2))), (60, {})]}

# in the air: tucked into his shell and rolling (M_A_4, 4 frames, held at
# the end as Mario's: one turn, ending as it starts), upside down (M_A_5),
# knocked flying backwards (M_A_6)
MOTIONS["M_A_4@b"] = {"end": 4, "keys": [
    (f, P(_SHELL, spun(-90.0 * f)), {"ease": "linear"}) for f in range(5)]}
MOTIONS["M_A_5@b"] = {"end": 2, "keys": [(0, P(_SHELL, spun(180))), (2, P(_SHELL, spun(180)))]}
_FLYING = P(spun(-70), bend(-10, -14), arms(40, 30, -10, -70, hf=1, hb=0), jaw(1.0, SHUT),
            feet((1.0, 3.0, 30.0), (1.0, 3.0, 25.0)), tail(16))
MOTIONS["M_A_6@b"] = {"end": 0, "keys": [(0, _FLYING)]}

# using an item, held out in front (M_C_4, 16 frames)
_ITEM = P(arms(90, 100, hf=1, hb=0), bend(2, 3), jaw(0.4, HAPPY))
MOTIONS["M_C_4@b"] = {"end": 16, "keys": [
    (0, _ITEM), (8, P(_ITEM, {"locator2": {"ty": 0.4, "sy": 1.0625}}, arms(80, 90, hf=1, hb=0))), (16, _ITEM)]}

# the Appeal (M_C_2A, 14 frames: claws to his chest; M_C_2B, a 6-frame loop;
# M_C_2C, 36 frames: a roar to the crowd)
MOTIONS["M_C_2A@b"] = {"end": 14, "keys": [(0, {}), (6, P(_CLASP, arms(110, 104, hf=2, hb=1))), (14, _CLASP)]}
MOTIONS["M_C_2B@b"] = {"end": 6, "loop": True, "keys": [
    (0, _CLASP), (3, P(_CLASP, {"locator2": {"sy": 0.9375}}, jaw(0.3, ANGRY))), (6, _CLASP)]}
MOTIONS["M_C_2C@b"] = MOTIONS["M_C_2"]

# (a tumble and up again, M_H_16: his disc getting-up pose, luigi_pc.c)
# the spin hammer's poses in battle (Super's are the field's M_H_6A/6B)
MOTIONS["M_H_2A@b"] = MOTIONS["M_H_3A"]
MOTIONS["M_H_2B@b"] = MOTIONS["M_H_3B"]
MOTIONS["M_H_10A@b"] = MOTIONS["M_H_9A"]
MOTIONS["M_H_10B@b"] = MOTIONS["M_H_9B"]
MOTIONS["M_H_10@b"] = MOTIONS["M_H_3A"]
MOTIONS["M_H_2@b"] = MOTIONS["M_H_3A"]
for _n in ("M_H_1", "M_H_4", "M_H_7"):
    MOTIONS[_n + "R"] = MOTIONS[_n]
for _n in ("M_H_2", "M_H_5", "M_H_8"):  # (Mario's back wall hit holds 6 frames, not 4)
    _k = MOTIONS[_n]["keys"]
    MOTIONS[_n + "R"] = dict(MOTIONS[_n], end=6, keys=_k + [(6,) + tuple(_k[-1][1:])])

# ------------------------------------------------ feet on the ground
# locator2 scales about his middle (y 16.8 at rest), so a squash lifts his
# feet off the floor and a stretch sinks them: each key's lift is made up so
# his feet stay where its ty puts them.
PIVOT_Y = 16.82


def grounded(motions):
    done = set()
    for name, m in motions.items():
        if id(m) in done:
            continue
        done.add(id(m))
        if "sy" in m.get("tracks", {}).get("locator2", {}):
            raise ValueError("%s: a squash in tracks isn't kept on the ground" % name)
        keys = []
        for f, pose, *opt in m.get("keys", []):
            loc = pose.get("locator2", {})
            if "sy" in loc:
                pose = {g: dict(c) for g, c in pose.items()}
                pose["locator2"]["ty"] = round(loc.get("ty", 0.0) + PIVOT_Y * (loc["sy"] - 1.0), 3)
            keys.append((f, pose, *opt))
        if "keys" in m:
            m["keys"] = keys


grounded(MOTIONS)

# his back view: Mario's back-facing animations of ours, on c_koopa_r_pc
RIG_R = Rig("c_koopa_r_pc", "bowser_r", RIG.chain)
MOTIONS_R = {n: MOTIONS[n] for n in ("M_H_1R", "M_H_2R", "M_H_4R", "M_H_5R", "M_H_7R", "M_H_8R", "M_O_2R",
                                     "M_U_2R", "M_C_1R")}

MODELS = [(RIG, MOTIONS), (RIG_R, MOTIONS_R)]
