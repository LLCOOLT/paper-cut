"""Peach's new motions for Play as (runs inside Blender through char_anims.py;
docs/PLAY_AS.md). The format is char_motions.py's; Mario's names and timing.

Peach's rig (c_peach, unnamed: these are its parts; she faces -x):
  locator2   the whole body (pivot at her feet)
  locator3   head (pivot at the neck): pPlane7 face, pPlane2 crown, group3
             bangs (pPlane3 5 6), group2 eyes (pPlane14, pPlane13: drawings
             0 open, 3 shut, 5 wide), pPlane15 mouth (0 small, 1 smile,
             2 open wide, 4 "o", 5 open round), pPlane12 ear, pPlane9 pPlane11
             earring, group4 the hair down her back (pPlane16 17 18)
  locator4   torso (pivot at the waist): pPlane26 bodice; front arm pPlane21
             (puffed sleeve, shoulder pivot) > pPlane23 > pPlane25 (glove:
             forearm and hand drawn as one, 13 drawings, 0 at rest, 1 hand
             open, 8 a fist); back arm pPlane22 > pPlane24 > pPlane19 (14)
  group1     skirt (pivot at the waist): pPlane27 front, pPlane28 middle,
             pPlane29 pPlane30 back
  Hammer_pc  the hammer, a child of the front glove, its handle in her hand
Each glove drawing has its forearm drawn at its own angle: the elbow angles
(arms' ff, fb) are measured for drawing 0 and are near enough for 1 and 8
(12 for the back glove); check others on a sheet (char_sheet.py --motions).

Her back-facing model (c_peach_r, for Mario's R animations):
  locator3   head: pPlane8 crown, pPlane14 head > group2 hair (pPlane6 the
             long hair over her back; pPlane2 3 4 5)
  locator4   torso: pPlane9 bodice; left arm pPlane16 > pPlane18 > pPlane20
             (glove, 4 drawings); right arm pPlane15 > pPlane17 > pPlane19
  group1     skirt: pPlane21 pPlane23 pPlane24 pPlane22 (left to right)
  Hammer_pc  in the left glove (pPlane20), behind her (she holds it in front)
"""
import math

from char_motions import (Rig, levels, field_hammer_name, merged, held, tracks, steps, blink_pairs, talk_pairs,
                          slowed, hammer_swing_pts, battle_hammer_set)

DEG = math.pi / 180.0
FRONT = Rig("c_peach_pc", "peach", ["locator2", "locator4", "pPlane21", "pPlane23", "pPlane25"])
BACK = Rig("c_peach_r_pc", "peach_r", ["locator2", "locator4", "pPlane16", "pPlane18", "pPlane20"])

# the world direction (0 up, 90 her front, 180 down) of each arm at rest,
# shoulder to hand
FRONT_ARM = 139.0
BACK_ARM = 186.0
# the same for the upper arms (shoulder to elbow, the glove's pivot) and the
# forearms (elbow to hand: drawing 0's, measured on a sheet) on their own
FRONT_UPPER, FRONT_FORE = 149.0, 132.0
BACK_UPPER, BACK_FORE = 180.0, 239.0
# her back-facing model's arms (the left one, x < 0, takes the hammer)
R_LEFT_ARM = 189.6
R_RIGHT_ARM = 166.8
NECK = (-0.17, 2.12)  # the neck from the waist's pivot


def lean(t, nod=0.0, neck=NECK):
    """bent at the waist by t degrees (+ toward her front): the torso, and the
    head carried round with the neck, nodding `nod` more (+ down to her front)"""
    c, s = math.cos(t * DEG), math.sin(t * DEG)
    nx, ny = neck
    return {"locator4": {"rz": t},
            "locator3": {"rz": t + nod, "tx": round(nx * c - ny * s - nx, 3), "ty": round(nx * s + ny * c - ny, 3)}}


def arms(front=None, back=None, bend=0.0, root=0.0, gf=None, gb=None, ff=None, fb=None):
    """the arms pointing in world directions `front`, `back` (degrees as hw),
    with the body bent `bend` and the whole turned `root`; gf, gb: glove
    drawings. With ff (fb), the arm bends at the elbow: `front` (`back`) is
    then the upper arm's direction and ff (fb) the forearm's."""
    p = {}
    if front is not None and ff is not None:
        p["pPlane21"] = {"rz": front - FRONT_UPPER - bend - root}
        p["pPlane25"] = {"rz": ff - front - (FRONT_FORE - FRONT_UPPER)}
    elif front is not None:
        p["pPlane21"] = {"rz": front - FRONT_ARM - bend - root}
    if back is not None and fb is not None:
        p["pPlane22"] = {"rz": back - BACK_UPPER - bend - root}
        p["pPlane19"] = {"rz": fb - back - (BACK_FORE - BACK_UPPER)}
    elif back is not None:
        p["pPlane22"] = {"rz": back - BACK_ARM - bend - root}
    if gf is not None:
        p.setdefault("pPlane25", {})["tex"] = gf
    if gb is not None:
        p.setdefault("pPlane19", {})["tex"] = gb
    return p


def face(eyes=None, mouth=None):
    p = {}
    if eyes is not None:
        p["pPlane14"] = {"tex": eyes}
        p["pPlane13"] = {"tex": eyes}
    if mouth is not None:
        p["pPlane15"] = {"tex": mouth}
    return p


def skirt(f, lift=0.0):
    """the skirt flared out by f degrees (- drawn in): its sides turned out a
    little and the whole widened from the waist (so no gaps open between its
    panels), shortened (sy) by `lift`"""
    p = {"pPlane27": {"rz": -f * 0.4}, "pPlane29": {"rz": f * 0.2}, "pPlane30": {"rz": f * 0.4},
         "group1": {"sx": 1.0 + f / 80.0}}
    if lift:
        p["group1"]["sy"] = 1.0 - lift
    return p


def hair(f):
    """the hair down her back swung out behind her by f degrees"""
    return {"group4": {"rz": f}}


def hammer(h, tex):
    return {"Hammer_pc": {"hw": h, "vis": 1, "tex": tex}}


def P(*parts):
    return merged(*parts)


MOTIONS = {}
MOTIONS_R = {}

# ------------------------------------------------ the field hammer swing
# (mot_hammer.c: M_H_1 wooden, M_H_4 Super, M_H_7 Ultra; Mario's 10 frames,
# held at the end.) One-handed, her free arm out for balance: drawn back
# over her shoulder, a rise onto her toes, then brought down with a bend at
# the waist, the skirt and hair swinging after it.


# Her near arm (the front glove's, nearer the camera) is drawn over her face
# wherever it points between about -15 and 70 degrees, so a raised hand
# keeps to NEAR_UP or further back, the far arm's raised hand to FAR_UP or
# further forward (its hand shows above and before her face; between, her
# head hides it), and a swing's near arm goes from up behind her head
# straight to below her chin, the wrist turning the hammer over the top.
NEAR_UP = -24.0
FAR_UP = 32.0


def near_arm(hw):
    """the near arm's direction for the hammer at world angle hw: on the line
    of it behind and below, held up behind her head while the hammer goes
    over the top (to 70), then down in front, below her chin (100 from 70,
    the hammer's own angle from 85)"""
    a = ((hw + 180.0) % 360.0) - 180.0
    if a <= NEAR_UP or a >= 85.0:
        return a
    return NEAR_UP if a < 70.0 else 100.0


def reach(hw):
    """Mario's hammer angle made hers: down before her (past 85) it is
    turned on, up to 24 degrees more, so that from her higher hand it
    reaches the ground where his does"""
    a = ((hw + 180.0) % 360.0) - 180.0
    return hw + max(0.0, min(24.0, (a - 85.0) * 1.2))


def _swing_key(h, tex, bend, back, lift=0.0, flare=0.0, hr=0.0, extra=None, arm=None, gb=None, fb=None):
    """a hammer key: the hammer at world angle h (drawing tex), her near arm
    on it (or at `arm`), the far arm at `back` (its glove drawing gb), bent
    `bend` at the waist, up `lift` on her toes; fb: the far forearm's own
    direction (`back` then the upper arm's)"""
    return P(lean(bend), arms(near_arm(h) if arm is None else arm, back, bend, gb=gb, fb=fb), hammer(h, tex), skirt(flare),
             hair(hr), {"locator2": {"ty": lift}} if lift else {}, extra or {})


# Back over her shoulder, the free hand out before her for balance; up on
# her toes as it rises (the wrist turning it over her head, the arm kept up
# behind), then brought down with a bend at the waist, the free hand
# joining the handle for the blow, the skirt and hair swinging after it.
_GRIP = 12  # the far glove's fist, on the handle
MOTIONS["M_H_1"] = {"end": 10, "keys": [
    (0, _swing_key(-60, 0, -6, 100, fb=72, arm=-64, hr=-3, extra=face(mouth=3))),
    (1, _swing_key(-25, 0, -9, 96, fb=64, arm=-46, lift=0.2, hr=-5, flare=-1, extra=face(mouth=4))),
    (2, _swing_key(10, 0, -7, 94, fb=62, arm=-34, lift=0.45, flare=-3, hr=-4, extra=face(mouth=4))),
    (3, _swing_key(45, 2, 2, 98, fb=74, arm=-26, lift=0.5, flare=-4, hr=-1, extra=face(mouth=4))),
    (4, _swing_key(85, 2, 13, 100, arm=80, lift=0.1, hr=4, extra=face(eyes=3, mouth=2)), {"ease": "linear"}),
    (5, _swing_key(128, 3, 24, 118, gb=_GRIP, flare=7, hr=8, extra=merged(face(eyes=3, mouth=2),
                                                                          {"locator2": {"sy": 0.9375}})),
     {"ease": "vector"}),
    (6, _swing_key(124, 3, 21, 114, gb=_GRIP, flare=4, hr=12, extra=face(mouth=2))),
    (8, _swing_key(126, 3, 23, 116, gb=_GRIP, flare=5, hr=9, extra=face(mouth=2))),
    (10, _swing_key(126, 3, 23, 116, gb=_GRIP, flare=5, hr=8, extra=face(mouth=2))),
]}

# hitting a wall (M_H_2, kind 7): the first three frames, then stopped short
# up before her, jolted back on her heels, eyes screwed shut
MOTIONS["M_H_2"] = {"end": 4, "keys": MOTIONS["M_H_1"]["keys"][:3] + [
    (3, _swing_key(40, 2, 0, 98, fb=74, arm=-26, lift=0.5, flare=-4, extra=face(mouth=4))),
    (4, _swing_key(30, 2, -6, 92, fb=60, arm=-30, lift=0.2, flare=-5, hr=-6, extra=face(eyes=3, mouth=2)),
     {"ease": "vector"}),
]}

# ------------------------------------------------ jumping (her own: the disc has none)
# M_J_1A going up (6 frames, held), M_J_1B at the top (2, held), M_J_1C coming
# down (an 8-frame loop). A little bob down to spring, then up with her arms
# lifting out and her skirt drawn in; at the top it billows, the hair lifts.

# (Her far hand reaches up before her, where Mario's fist goes, and the near
# one sweeps down behind.)
_RISE = P(lean(-5, -3), arms(214, FAR_UP + 10, -5, gf=1, gb=1), skirt(-6, 0.0625), hair(-10),
          {"locator2": {"sy": 1.0625}}, face(mouth=4))
_TOP = P(lean(-2, -2), arms(226, FAR_UP + 4, -2, gf=1, gb=1), skirt(14), hair(22), face(mouth=4))
MOTIONS["M_J_1A"] = {"end": 6, "keys": [
    (0, P(lean(6, 3), arms(168, 150, 6, gb=1), skirt(8), {"locator2": {"sy": 0.9375}}, hair(6), face(eyes=3))),
    (3, _RISE),
    (6, P(_RISE, arms(220, FAR_UP + 7, -5, gf=1, gb=1), skirt(4), hair(6))),
]}
MOTIONS["M_J_1B"] = {"end": 2, "keys": [(0, _TOP), (2, _TOP)]}
MOTIONS["M_J_1C"] = {"end": 8, "loop": True, "keys": [
    (0, _TOP),
    (4, P(_TOP, arms(234, FAR_UP + 1, gf=1, gb=1), skirt(18), hair(26))),
    (8, _TOP),
]}

# ------------------------------------------------ spin jump (mot_hip.c)
# The game spins and drops the pose: tucked in, skirt gathered, fists
# together, eyes shut (M_A_1A, held); landing from it (M_A_1B).
_TUCK = P(arms(200, 160, gf=8, gb=12), skirt(-8, 0.125), hair(20), face(eyes=3, mouth=3),
          {"locator2": {"sy": 0.875, "ty": 0.6}}, lean(8))
MOTIONS["M_A_1A"] = {"end": 2, "keys": [(0, _TUCK), (2, _TUCK)]}
MOTIONS["M_A_1B"] = {"end": 4, "keys": [
    (0, _TUCK), (2, _TUCK),
    (4, P(arms(160, 190, gf=0, gb=0), skirt(6), hair(8), face(eyes=0, mouth=3), {"locator2": {"sy": 0.9375}},
          lean(4))),
]}

# ------------------------------------------------ hurt
# M_D_1 (6 frames): hit from the front, knocked back with a hop, hands up,
# eyes shut, mouth open, hair and skirt flung forward.
_HURT = P(lean(-14, -6), arms(360 + NEAR_UP - 30, FAR_UP + 30, -14, gf=1, gb=1), skirt(-8), hair(-16),
          face(eyes=3, mouth=2), {"locator2": {"tx": 1.0, "ty": 1.2}})
MOTIONS["M_D_1"] = {"end": 6, "keys": [
    (0, P(lean(-4), face(eyes=3))),
    (2, P(lean(-10, -4), arms(360 + NEAR_UP - 50, FAR_UP + 50, -10, gf=1, gb=1), skirt(-5), hair(-10),
          face(eyes=3, mouth=2), {"locator2": {"tx": 0.5, "ty": 0.8}})),
    (4, _HURT), (6, _HURT)]}

# M_D_2 (8 frames): pitched over, face down, a bounce; M_U_3 (12): up again.
# Turned about her feet, lifted so her skirt rests on the ground.
_FLAT = P({"locator2": {"rz": 90, "tx": 13.0, "ty": 8.0}}, arms(270, 250, 0, 90, gf=1, gb=1), skirt(-6),
          hair(-30), face(eyes=3, mouth=2))
MOTIONS["M_D_2"] = {"end": 8, "keys": [
    (0, P(lean(-6), arms(165, 205), face(eyes=3, mouth=2))),
    (2, P({"locator2": {"rz": 30, "tx": 2.5, "ty": 1.6}}, arms(200, 220, 0, 30, gf=1, gb=1), skirt(-4), hair(-12),
          face(eyes=3, mouth=2))),
    (4, _FLAT, {"ease": "vector"}),
    (6, P(_FLAT, {"locator2": {"rz": 86, "tx": 12.6, "ty": 8.6}})),
    (8, _FLAT)]}
MOTIONS["M_U_3"] = {"end": 12, "keys": [
    (0, _FLAT),
    (3, P({"locator2": {"rz": 60, "tx": 8.5, "ty": 5.0}}, arms(230, 230, 0, 60, gf=1, gb=1), skirt(-3), hair(-18),
          face(eyes=3))),
    (6, P({"locator2": {"rz": 25, "tx": 2.8, "ty": 0.8, "sy": 0.9375}}, arms(190, 205, 0, 25), hair(-8),
          face(eyes=0, mouth=3))),
    (9, P(lean(-3), {"locator2": {"sy": 1.0625}}, hair(4), face(mouth=3))),
    (12, {})]}

# ------------------------------------------------ battle (as "<Mario's name>@b")

# guard, ready for the next move (M_C_3, a 180-frame loop): fists clasped
# low before her, leaning in, chin up, breathing
_GUARD = P(lean(5, -4), arms(150, 146, 5, gf=8, gb=12), face(mouth=3), {"locator2": {"sy": 1.0}})
MOTIONS["M_C_3@b"] = {"end": 180, "loop": True, "keys": [
    (0, _GUARD),
    (90, P(_GUARD, lean(3, -3), arms(147, 143, 3, gf=8, gb=12), hair(3), {"locator2": {"sy": 1.0625}})),
    (180, _GUARD)]}

# bouncing off an enemy after a jump (M_J_1D, 6 frames): squashed on it,
# then up again, her hand up and the skirt billowing
MOTIONS["M_J_1D@b"] = {"end": 6, "keys": [
    (0, P({"locator2": {"sy": 0.875}}, skirt(12), arms(200, 120, gb=1), hair(10), face(eyes=3, mouth=4))),
    (3, _RISE),
    (6, _TOP)]}

# a jump attack in the air: going up (M_A_3A, held), a stamp (M_A_3B): her
# far fist up, the near hand thrown back, the skirt flung out
MOTIONS["M_A_3A@b"] = {"end": 0, "keys": [(0, _RISE)]}
MOTIONS["M_A_3B@b"] = {"end": 4, "keys": [
    (0, _TOP),
    (2, P(_TOP, lean(4, 2), arms(246, FAR_UP + 2, 4, gf=1, gb=12), skirt(16), hair(26), face(mouth=2))),
    (4, P(_TOP, lean(3, 2), arms(242, FAR_UP + 4, 3, gf=1, gb=12), skirt(14), hair(24), face(mouth=2)))]}

# the battle hammer: Mario's hammer angles at each of his keys (and his
# drawing), her arm on the line of it, bending into the swing

def _battle_swing(end, pts, loop=False, add=0, slam=False):
    """her near arm as near_arm() has it (each key turned the way the hammer
    turns from the last), the far hand joining the handle while the hammer
    is down before her and held out for balance while it is behind or up;
    the hair swinging a key behind the body"""
    keys = []
    prev = None  # (hw, arm, bend) at the last key
    strike = pts[-1][1] is not None and ((pts[-1][1] + 180) % 360) - 180 >= 85  # it ends down before her
    for f, hw, shown, tex in hammer_swing_pts(pts):
        hw = reach(hw) if strike else hw
        a = ((hw + 180) % 360) - 180
        bend = max(-10.0, min(18.0, a / 7.0))
        arm = near_arm(hw)
        if not shown:  # thrown: the arm follows through, down before her, the hand open
            arm, bend = 112.0, 12.0
        if prev is not None:
            want = prev[1] + (hw - prev[0] if shown else 0.0)
            while arm - want > 180:
                arm -= 360
            while want - arm > 180:
                arm += 360
        two = shown and a >= 95
        back, gb, fb = (arm - 12.0, _GRIP, None) if two else (100.0, None, 72.0)
        hr = (prev[2] if prev else bend) * 0.6
        k = _swing_key(hw, tex + add, bend, back, flare=bend * 0.3, hr=hr, arm=arm, gb=gb, fb=fb,
                       extra=face(mouth=2) if bend > 10 else {})
        if not shown:
            k = P(k, {"Hammer_pc": {"hw": hw, "vis": 0, "tex": tex + add}}, arms(gf=1))
        if slam and a > 0:  # the big overhead blow: up off her feet, thrown forward into it
            t = min(1.0, a / 100.0)
            k = P(k, {"locator2": {"ty": round(1.0 * t, 2)}}, lean(bend + 14 * t, 4 * t),
                  arms(arm, back + 20 * t if not two else back, bend + 14 * t, gb=gb, fb=fb), hair(14 * t))
        if prev is not None and shown and keys and math.floor((prev[0] + 180) / 360) != math.floor((hw + 180) / 360):
            # swung round under her between two of Mario's keys: halfway,
            # the hammer straight down, she hops and draws her hand back up
            # behind her so that its head clears the ground
            down = 180.0 + 360.0 * min(math.floor((prev[0] + 180) / 360), math.floor((hw + 180) / 360))
            pf = keys[-1][0]
            mid_arm = prev[1] + (arm - prev[1]) * 0.5
            keys.append(((pf + f) / 2.0, P(_swing_key(down, tex + add, 0.0, 100.0, arm=mid_arm, fb=72),
                                           {"locator2": {"ty": 4.0}}, skirt(-6, 0.0625), hair(-10))))
        keys.append((f, k))
        prev = (hw, arm, bend)
    return {"end": end, "loop": loop, "keys": keys}


battle_hammer_set(MOTIONS, _battle_swing)

# victory (M_V_1, 60 frames): a curtsy, then a wave to the crowd
_CURTSY = P(lean(14), {"locator2": {"sy": 0.875}}, skirt(10), arms(170, 200, 14, gf=0, gb=0), face(eyes=3, mouth=1))
MOTIONS["M_V_1@b"] = {"end": 60, "keys": [
    (0, {}),
    (12, _CURTSY), (28, _CURTSY),
    (36, P(lean(-3, -4), arms(140, FAR_UP + 8, -3, gb=1), hair(-4), face(mouth=1))),
    (42, P(lean(-2, -4), arms(140, FAR_UP + 26, -2, gb=1), face(mouth=1))),
    (48, P(lean(-3, -4), arms(140, FAR_UP + 6, -3, gb=1), hair(-3), face(mouth=1))),
    (54, P(lean(-1, -2), arms(140, FAR_UP + 22, -1, gb=1), face(mouth=1))),
    (60, {})]}

# ------------------------------------------------ batch 3: the rest of the field set
# (luigi_motions.py has what each of Mario's is for and where it comes from)

EYES = [("pPlane14", "tex"), ("pPlane13", "tex")]
MOUTH = [("pPlane15", "tex")]
SHUT, OPEN, WIDE = 3, 0, 5


def blinks(end, *pairs, base=OPEN):
    return steps(EYES, blink_pairs(*pairs, shut=SHUT, open_=base), base, end)


def talking(times, end, open_=2, shut=0):
    return steps(MOUTH, talk_pairs(times, open_, shut), shut, end)


def head_track(pts):
    """the head nodding (+ down to her front) through (frame, degrees[, ease])"""
    return {"locator3": {"rz": [p if len(p) > 2 else (p[0], p[1]) for p in pts]}}


def spun(t, c=(-1.0, 12.0)):
    """the whole of her turned t degrees about her middle (c), not her feet"""
    r = t * DEG
    cx, cy = c
    return {"locator2": {"rz": t, "tx": round(cx - (cx * math.cos(r) - cy * math.sin(r)), 3),
                         "ty": round(cy - (cx * math.sin(r) + cy * math.cos(r)), 3)}}


def bob(end, step, up=0.4, sway=2.0, hr=3.0):
    """a step's bob every `step` frames, the skirt swishing and the hair
    swinging after it (she has no legs to step with: the skirt walks)"""
    n = int(round(end / step))
    return {"locator2": {"ty": [(i * step, up if i % 2 else 0.0) for i in range(n + 1)]},
            "group1": {"rz": [(i * step, 0.0 if i % 2 else (sway if i % 4 == 0 else -sway)) for i in range(n + 1)]},
            "group4": {"rz": [(i * step, hr if i % 2 else -hr * 0.5) for i in range(n + 1)]}}


# the spin hammer (mot_hammer.c, hammer2; the spinning paper form draws the
# pose): wound up, crouched, the hammer low behind her in both hands (M_H_3);
# braced, holding it up behind her head while it charges (M_H_3A); whirled
# round, the hammer straight out before her, hair and skirt flung out
# (M_H_3B). Mario's drawings: 0 (wound up and braced), 1 (end-on, whirling).
_WIND = P(lean(10, 5), _swing_key(-108, 0, 10, -128, arm=-124, gb=_GRIP), skirt(5), hair(-4),
          {"locator2": {"sy": 0.9375}}, face(mouth=3))
_BRACE = P(lean(-5, -3), _swing_key(14, 0, -5, FAR_UP + 4, arm=NEAR_UP - 12, gb=_GRIP), skirt(-4, 0.0625),
           hair(-8), {"locator2": {"sy": 1.0625}}, face(eyes=SHUT, mouth=3))
_SPIN = P(lean(-6, -2), _swing_key(90, 1, -6, 84, arm=94, gb=_GRIP), skirt(16), hair(30), face(mouth=4))
MOTIONS["M_H_3"] = {"end": 2, "keys": [(0, _WIND), (2, _WIND)]}
MOTIONS["M_H_3A"] = {"end": 2, "keys": [(0, _BRACE), (2, P(_BRACE, {"locator2": {"sy": 1.0}}))]}
MOTIONS["M_H_3B"] = {"end": 2, "keys": [(0, _SPIN), (2, _SPIN)]}
levels(MOTIONS, ["M_H_3", "M_H_3A", "M_H_3B"], rename=field_hammer_name)

# hurt and worried (M_D_6, an 8-frame loop): drawn back, her hands to her
# cheeks (the far one behind her face, its fingers showing), eyes shut,
# mouth open, a sobbing bob
_WORRY = P(lean(-6, -4), arms(165, 160, -6, gf=1, gb=1, ff=0, fb=25), face(eyes=SHUT, mouth=2), hair(-4),
           skirt(-2))
MOTIONS["M_D_6"] = {"end": 8, "loop": True, "keys": [
    (0, _WORRY), (4, P(_WORRY, lean(-8, -5), arms(167, 162, -8, gf=1, gb=1, ff=-3, fb=22), hair(-6),
                       {"locator2": {"sy": 0.9375}})),
    (8, _WORRY)]}

# holding up an item (M_I_1, 4 frames, held): high in her far hand, open,
# the near one on her chest, a smile, up on her toes
_HOLD = P(lean(-4, -6), arms(150, FAR_UP + 6, -4, gf=0, gb=1, ff=40), face(mouth=1), skirt(-2, 0.0625), hair(-4))
MOTIONS["M_I_1"] = {"end": 4, "keys": [
    (0, P(lean(-2, -2), arms(145, 90, -2, gb=1), face(mouth=4))),
    (2, P(_HOLD, {"locator2": {"ty": 0.4, "sy": 1.0625}})),
    (4, _HOLD)]}

# something catches her eye (M_I_Y, 8 frames, held): a little start, eyes wide
_START = P({"locator2": {"ty": 0.5, "sy": 1.0625}}, lean(-5, -4), arms(118, 232, -5, gf=1, gb=1),
           face(eyes=WIDE, mouth=4), hair(6), skirt(3))
MOTIONS["M_I_Y"] = {"end": 8, "keys": [
    (0, {}), (3, _START), (8, P(lean(-2, -2), arms(140, 205, -2, gf=1), face(eyes=WIDE, mouth=4)))]}

# riding Yoshi (M_Y_1, M_Y_2; M_Y_3 his flutter): sat side-saddle, the skirt
# gathered up over his back, leaning in, hands forward on his neck
_RIDE = P({"locator2": {"ty": -4.5}}, {"group1": {"sx": 1.25, "sy": 0.5}, "pPlane27": {"rz": -8},
                                       "pPlane30": {"rz": 6}},
          lean(10, -6), arms(112, 122, 10, gf=8, gb=12), hair(10))
MOTIONS["M_Y_1"] = {"end": 0, "keys": [(0, _RIDE)]}
MOTIONS["M_Y_2"] = {"end": 0, "loop": True, "keys": [(0, _RIDE)]}
MOTIONS["M_Y_3"] = {"end": 0, "keys": [(0, P(_RIDE, {"locator2": {"ty": -4.0}}, arms(222, FAR_UP + 30, 10, gf=1, gb=1),
                                             hair(24), skirt(4), face(mouth=4)))]}

# bending low to spring (M_P_1, 8 frames), and springing up, a hand high (M_P_2)
MOTIONS["M_P_1"] = {"end": 8, "keys": [
    (0, arms(155, 180)),
    (4, P(lean(10, 4), {"locator2": {"sy": 0.9375}}, skirt(4), arms(168, 192, 10))),
    (8, P(lean(16, 6), {"locator2": {"sy": 0.875}}, skirt(8), arms(176, 200, 16, gf=8, gb=12), face(eyes=SHUT)))]}
_SPRING = P({"locator2": {"ty": 0.6, "sy": 1.0625}}, lean(-6, -4), arms(222, FAR_UP + 8, -6, gf=1, gb=12),
            skirt(-6, 0.0625), hair(-12), face(mouth=1))
MOTIONS["M_P_2"] = {"end": 0, "keys": [(0, _SPRING)]}

# arms up, overhead (lifting Bobbery, party_bomhei.c): her reach is short of
# her crown, so her arms open in a V over her head, the shoulders lifted,
# the sleeves and long gloves drawn out to hold him where Mario does; the
# near arm comes up behind her (round by her back: 338 is NEAR_UP's 0..360
# turn), the far one before her face, hidden by her head as it passes
_UP = P(arms(360 + NEAR_UP - 14, FAR_UP, gf=1, gb=1),
        {"pPlane21": {"ty": 3.0}, "pPlane22": {"ty": 3.0}, "pPlane25": {"sy": 1.625}, "pPlane19": {"sy": 1.625}},
        face(mouth=3))
MOTIONS["M_U_2"] = {"end": 10, "keys": [
    (0, P(lean(8, 2), {"locator2": {"sy": 0.9375}}, arms(166, 176, 8), skirt(3), face(mouth=3))),
    (4, P({"locator2": {"sy": 1.0625}}, arms(236, 80, gf=1, gb=1), hair(-6), skirt(-3), face(mouth=4))),
    (7, P(_UP, {"locator2": {"ty": 0.2, "sy": 1.0625}}, hair(4))), (10, _UP)]}
MOTIONS["M_W_7"] = {"end": 40, "loop": True, "tracks": tracks(held(_UP, 40), bob(40, 10))}
MOTIONS["M_S_4"] = {"end": 2, "keys": [(0, _UP), (2, _UP)]}
MOTIONS["M_W_6"] = {"end": 32, "loop": True, "tracks": tracks(held(_UP, 32), bob(32, 8))}
MOTIONS["M_J_5A"] = {"end": 2, "keys": [(0, _SPRING), (2, _SPRING)]}
MOTIONS["M_J_5B"] = {"end": 14, "keys": [
    (0, P(_UP, {"locator2": {"sy": 0.9375}})),
    (5, P(_UP, {"locator2": {"ty": 1.8, "sy": 1.0625}}, skirt(-6, 0.0625), hair(-10), face(mouth=2))),
    (10, P(_UP, {"locator2": {"ty": 0.6}}, skirt(10), hair(12), face(mouth=2))),
    (14, _UP)]}

# a throw (M_C_1, 8 frames; also battle's): an underhand toss, as a lady
# would: her hand swung back low, then forward and up, let go before her at
# chest height, the free hand out behind, a step of the skirt after it
MOTIONS["M_C_1"] = {"end": 8, "keys": [
    (0, P(lean(-6, -3), arms(232, 120, -6, gf=8), skirt(-2), hair(-6))),
    (2, P(lean(-4, -2), arms(246, 116, -4, gf=8), {"locator2": {"sy": 1.0625}}, hair(-8))),
    (3, P(lean(4, 1), arms(170, 160, 4, gf=8), face(mouth=4)), {"ease": "linear"}),
    (5, P(lean(10, 2), arms(100, 214, 10, gf=1, gb=1), skirt(6), hair(10), face(mouth=1),
          {"locator2": {"ty": 0.3, "sy": 1.0625}}), {"ease": "vector"}),
    (8, P(lean(8, 2), arms(104, 210, 8, gf=1, gb=1), skirt(4), hair(7), face(mouth=1)))]}
MOTIONS["M_C_1@b"] = MOTIONS["M_C_1"]

# shading her eyes to look far off (M_Z_9, 2 frames, held): the far hand
# flat at her brow, reaching forward past it, up on her toes, leaning in
MOTIONS["M_Z_9"] = {"end": 2, "keys": [(0, {}), (2, P(lean(4, -6), arms(160, 6, 4, gb=1, fb=88),
                                                      {"locator2": {"ty": 0.3, "sy": 1.0625}}, hair(3)))]}

# low on HP (M_S_3, a 120-frame loop): a tired sway, her head low
_LOW = P(lean(6, 6), arms(170, 182, 6), face(mouth=3), hair(3))
MOTIONS["M_S_3"] = {"end": 120, "loop": True, "keys": [
    (0, _LOW), (30, P(_LOW, lean(3, 4), {"locator2": {"sy": 1.0}})), (60, P(_LOW, lean(8, 7))),
    (90, P(_LOW, lean(4, 4))), (120, _LOW)]}

# in peril, worn out (M_I_S, a 96-frame loop, field and battle): hunched,
# breathing hard, mouth open
_TIRED = P(lean(12, 8), arms(180, 190, 12), face(mouth=2), hair(6), {"locator2": {"sy": 0.9375}})
MOTIONS["M_I_S"] = {"end": 96, "loop": True, "keys": [
    (0, _TIRED), (24, P(_TIRED, lean(8, 6), {"locator2": {"sy": 1.0}})), (48, P(_TIRED, lean(13, 9))),
    (72, P(_TIRED, lean(8, 6), {"locator2": {"sy": 1.0}})), (96, _TIRED)]}
MOTIONS["M_I_S@b"] = MOTIONS["M_I_S"]

# pushing (M_O_1 a shoulder to it, M_O_2 both hands flat; 32-frame loops)
_PUSH1 = P(lean(16, -4), arms(100, 150, 16, gf=1, gb=12), face(eyes=SHUT, mouth=3))
_PUSH2 = P(lean(20, -6), arms(92, 96, 20, gf=1, gb=1), face(eyes=SHUT, mouth=3))
MOTIONS["M_O_1"] = {"end": 32, "loop": True, "tracks": tracks(held(_PUSH1, 32), bob(32, 8, up=0.2))}
MOTIONS["M_O_2"] = {"end": 32, "loop": True, "tracks": tracks(held(_PUSH2, 32), bob(32, 8, up=0.2))}

# ducking, hands over her head (M_B_1, 6 frames; M_B_2 held there): Mario
# cowers; she bobs down, the near hand up over her crown (round behind her
# head, as NEAR_UP), eyes screwed shut. Startled back, a hand up before her
# (M_B_3, held).
_COWER = P(lean(12, 10), {"locator2": {"sy": 0.875}}, arms(360 + NEAR_UP - 6, 170, 12, gf=1, gb=1, ff=72),
           skirt(8), hair(10), face(eyes=SHUT, mouth=2))
MOTIONS["M_B_1"] = {"end": 6, "keys": [
    (0, {}), (2, P(lean(4, 4), arms(250, 160, 4, gf=1), face(eyes=SHUT, mouth=4), {"locator2": {"ty": 0.3}})),
    (4, _COWER, {"ease": "vector"}), (6, P(_COWER, {"locator2": {"sy": 0.9375}}))]}
MOTIONS["M_B_2"] = {"end": 2, "keys": [(0, P(_COWER, {"locator2": {"sy": 0.9375}})),
                                     (2, P(_COWER, {"locator2": {"sy": 0.9375}}))]}
_CLASPED = P(arms(108, 132, gf=8, gb=12), lean(0, 3))
_WHOA = P(lean(-10, -6), arms(214, 72, -10, gf=1, gb=1, fb=48), skirt(-4), hair(-8), face(eyes=WIDE, mouth=4),
          {"locator2": {"sy": 1.0625}})
MOTIONS["M_B_3"] = {"end": 2, "keys": [(0, _WHOA), (2, _WHOA)]}

# thinking, a finger to her chin (M_X_1, a 64-frame loop)
_CHIN = P(arms(118, 178, gf=8, ff=22))
MOTIONS["M_X_1"] = {"end": 64, "loop": True, "tracks": tracks(
    held(_CHIN, 64), head_track([(0, -6.0), (16, -9.0), (32, -6.0), (48, -9.0), (64, -6.0)]),
    blinks(64, (40, 42)))}

# a fist raised by her face, resolved (M_I_U, held): the far fist up before
# her, the near hand on her chest
MOTIONS["M_I_U"] = {"end": 0, "keys": [(0, P(arms(150, FAR_UP + 30, gf=0, gb=12, ff=40, fb=FAR_UP), lean(-2, -4),
                                             face(mouth=3)))]}

# a hand to the crowd (M_C_2, 36 frames, and battle's M_C_2C): her far hand
# up high, waving, the near one on her chest, a lean and a little bounce of
# the skirt before settling
_CROWD = P(lean(-8, -6), arms(150, FAR_UP + 4, -8, gf=0, gb=1, ff=40), face(mouth=1), skirt(4), hair(-6))
MOTIONS["M_C_2"] = {"end": 36, "keys": [
    (0, _CROWD, {"ease": "vector"}),
    (7, P(_CROWD, lean(-4, -4), arms(150, FAR_UP + 22, -4, gf=0, gb=1, ff=40))),
    (14, P(_CROWD, lean(2, -2), arms(150, FAR_UP + 2, 2, gf=0, gb=1, ff=40), skirt(-2))),
    (22, P(_CROWD, lean(3, -2), arms(150, FAR_UP + 20, 3, gf=0, gb=1, ff=40))),
    (29, P(_CROWD, lean(2, -2), arms(150, FAR_UP + 6, 2, gf=0, gb=1, ff=40))),
    (36, {})]}

# riding along with Flurrie (M_A_2A 12 frames, M_A_2B a 4-frame loop,
# M_A_2C 8): hands clasped, lifted a little as she carries her
MOTIONS["M_A_2A"] = {"end": 12, "keys": [(0, {}), (12, P(_CLASPED, {"locator2": {"sy": 1.0625}}, skirt(-4)))]}
MOTIONS["M_A_2B"] = {"end": 4, "loop": True, "keys": [
    (0, P(_CLASPED, {"locator2": {"sy": 1.0625}}, skirt(-4))), (2, P(_CLASPED, skirt(-2))),
    (4, P(_CLASPED, {"locator2": {"sy": 1.0625}}, skirt(-4)))]}
MOTIONS["M_A_2C"] = {"end": 8, "keys": [(0, P(_CLASPED, {"locator2": {"sy": 1.0625}}, skirt(-4))), (8, {})]}

# a victory fist (M_V_2, 6 frames, field and battle): a hop, her far fist
# punched up before her, the near hand swept back, eyes shut in a smile
_FIST = P({"locator2": {"ty": 0.5, "sy": 1.0625}}, lean(-4, -6), arms(218, FAR_UP + 6, -4, gf=1, gb=12),
          face(eyes=SHUT, mouth=1), skirt(-4, 0.0625), hair(-6))
MOTIONS["M_V_2"] = {"end": 6, "keys": [(0, P(arms(160, 120, gb=12), {"locator2": {"sy": 0.9375}}, skirt(4))),
                                     (3, _FIST, {"ease": "vector"}),
                                     (6, P(_FIST, {"locator2": {"ty": 0.0, "sy": 1.0}}, skirt(6), hair(6)))]}

# ------------------------------------------- batch 4: event poses (e_mario)
# The poses the story scripts give Mario. Timing is Mario's to the frame:
# where his face tilts, her head turns as far; where his drawings swap for
# talking or a blink, her mouth opens or her eyes shut on the same frames.

MOTIONS["M_N_1"] = {"end": 12, "tracks": head_track([(0, 0.0), (12, -6.0)])}
MOTIONS["M_N_2"] = {"end": 18, "tracks": head_track([
    (0, 0.0), (2, 13.0), (4, 13.0), (7, -1.2), (9, 0.0), (11, 13.0), (13, 13.0), (16, -1.2), (18, 0.0)])}
MOTIONS["M_N_3"] = {"end": 32, "tracks": tracks(head_track([(0, -2.1), (2, -2.8), (3, -3.1), (6, -4.6), (32, -4.6)]),
                                                blinks(32, (20, 22), (26, 28)))}
MOTIONS["M_N_B"] = {"end": 34, "tracks": tracks(
    talking([4, 6, 10, 12, 16, 18, 22, 24, 28, 30, 34], 34),
    head_track([(0, 0.0), (4, 2.0), (10, -1.0), (16, 2.0), (22, -1.0), (28, 2.0), (34, 0.0)]))}
MOTIONS["M_N_C"] = {"end": 70, "tracks": tracks(
    talking(list(range(2, 71, 2)), 70),
    head_track([(0, 0.0), (8, 2.0), (18, -1.5), (28, 2.0), (38, -1.5), (48, 2.0), (58, -1.5), (70, 0.0)]))}
MOTIONS["M_I_N"] = {"end": 120, "loop": True, "tracks": tracks(head_track([(0, -3.0), (120, -3.0)]),
                                                               blinks(120, (20, 22), (26, 28)))}
# hangs her head, hands clasped low (M_I_Z, 12 frames)
MOTIONS["M_I_Z"] = {"end": 12, "keys": [(0, {}), (12, P(lean(4, 12), arms(160, 176, 4), face(eyes=SHUT),
                                                         {"locator2": {"sy": 0.9375}}))]}
# fists clenched, determined (M_I_G, 6 frames)
_DETERMINED = P(lean(4, -3), arms(112, 136, 4, gf=8, gb=12), face(mouth=3), {"locator2": {"sy": 1.0625}})
MOTIONS["M_I_G"] = {"end": 6, "keys": [(0, {}), (2, _DETERMINED), (6, P(_DETERMINED, lean(3, -3)))]}
# a hand to her mouth (M_N_10, 6 frames): a little step back, eyes wide
_GASP = P(lean(-4, 5), arms(150, None, -4, gf=1, ff=22), face(eyes=WIDE, mouth=4), hair(-3))
MOTIONS["M_N_10"] = {"end": 6, "keys": [(0, {}), (3, P(_GASP, lean(-2, 4))), (6, _GASP)]}
# thinking, a finger to her chin (M_I_5, 50 frames)
MOTIONS["M_I_5"] = {"end": 50, "tracks": tracks(held(_CHIN, 50), head_track([
    (0, -10.0), (4, -12.0), (6, -11.0), (8, -10.0), (12, -12.0), (14, -11.0), (16, -10.0), (20, -12.0), (22, -11.0),
    (24, -10.0), (28, -12.0), (30, -11.0), (32, -10.0), (36, -12.0), (38, -11.3), (40, -10.0), (42, -9.3),
    (50, -9.3)]))}
# marching on the spot (M_N_7, an 18-frame loop): a step every 4.5 frames, arms swinging
MOTIONS["M_N_7"] = {"end": 18, "loop": True, "tracks": tracks(bob(18, 4.5, up=0.6), {
    "pPlane21": {"rz": [(0, -40.0), (4.5, 30.0), (9, -40.0), (13.5, 30.0), (18, -40.0)]},
    "pPlane22": {"rz": [(0, 30.0), (4.5, -40.0), (9, 30.0), (13.5, -40.0), (18, 30.0)]}},
    held(P(lean(0, -6), face(mouth=3)), 18))}
# stamping, cross (M_I_O, a 24-frame loop): quick little stamps, fists down
_ST = [i * 1.5 for i in range(17)]
MOTIONS["M_I_O"] = {"end": 24, "loop": True, "tracks": tracks(
    held(P(arms(186, 196, gf=8, gb=12), face(mouth=3)), 24),
    {"locator2": {"ty": [(t, 0.5 if i % 2 else 0.0, "linear") for i, t in enumerate(_ST)]},
     "group1": {"rz": [(t, 2.0 if i % 2 else -2.0, "linear") for i, t in enumerate(_ST)]}},
    head_track([(0, 4.0), (3, 6.0), (6, 4.0), (9, 6.0), (12, 4.0), (15, 6.0), (18, 4.0), (21, 6.0), (24, 4.0)]))}
# jumps in surprise (M_N_5B, 32 frames): squash, up, land, settle, eyes wide
MOTIONS["M_N_5B"] = {"end": 32, "keys": [
    (0, P({"locator2": {"ty": -0.3, "sy": 0.875}}, skirt(6), face(eyes=WIDE))),
    (2, P({"locator2": {"sy": 1.0625}}, arms(210, 90, gf=1, gb=1), face(eyes=WIDE, mouth=2))),
    (8, P({"locator2": {"ty": 2.3, "sy": 1.125}}, lean(-6, -4), arms(250, FAR_UP + 14, -6, gf=1, gb=1),
          skirt(-8, 0.0625), hair(14), face(eyes=WIDE, mouth=2))),
    (13, P({"locator2": {"ty": 0.6, "sy": 1.0625}}, lean(-3, -2), arms(232, 70, -3, gf=1, gb=1), skirt(10),
           hair(18), face(eyes=WIDE, mouth=2))),
    (15, P({"locator2": {"sy": 0.875}}, arms(200, 110, gf=1, gb=1), skirt(12), hair(8), face(eyes=WIDE, mouth=2)),
     {"ease": "vector"}),
    (16, P({"locator2": {"sy": 0.9375}}, arms(180, 130, gf=1, gb=1), skirt(6), face(eyes=WIDE, mouth=2))),
    (32, face(eyes=WIDE))]}
# standing, breathing (M_N_5A, a 140-frame loop): a slow sway
MOTIONS["M_N_5A"] = {"end": 140, "loop": True, "tracks": {
    "locator4": {"rz": [(0, 0.0), (26, 3.0), (40, 4.0), (50, 4.5), (60, 4.0), (74, 2.0), (110, 0.8), (140, 0.0)]},
    "locator3": {"rz": [(0, 0.0), (50, 2.0), (110, 0.0), (140, 0.0)]},
    "group4": {"rz": [(0, 0.0), (60, 3.0), (140, 0.0)]}}}
# fists together, ready (M_C_3, 16 frames)
_READY = P({"locator2": {"ty": 0.3, "sy": 1.0625}}, lean(2, -5), arms(108, 128, 2, gf=8, gb=12), face(mouth=3))
MOTIONS["M_C_3"] = {"end": 16, "keys": [(0, P(arms(130, 150, gf=8, gb=12), lean(0, -4))), (10, _READY), (16, _READY)]}
# a fist thrust out before her (M_N_9, 2 frames, held), leaning into it
_PUNCH = P(lean(8, 2), arms(200, 84, 8, gf=8, gb=12), skirt(5), hair(6), face(mouth=3))
MOTIONS["M_N_9"] = {"end": 2, "keys": [(0, _PUNCH), (2, _PUNCH)]}
# mid-stride, held (M_N_H, 2 frames), the far hand forward, the near swung
# back; a hop, her far hand up (M_F_2, as Mario's jump drawing)
_STRIDE = P({"locator2": {"ty": 0.4}}, lean(6, 2), arms(214, 96, 6, gf=0, gb=1), {"group1": {"rz": 4}}, hair(6),
            skirt(3))
MOTIONS["M_N_H"] = {"end": 2, "keys": [(0, _STRIDE), (2, _STRIDE)]}
MOTIONS["M_F_2"] = {"end": 2, "keys": [(0, P(_TOP, {"locator2": {"ty": 0.6}})), (2, P(_TOP, {"locator2": {"ty": 0.6}}))]}
# sitting on the ground (M_S_5, a 180-frame loop, breathing; M_N_11, looking
# about, 18 frames): the skirt spread round her, hands in her lap
_SIT = P({"locator2": {"ty": -7.6}}, {"group1": {"sx": 1.375, "sy": 0.3125}, "pPlane27": {"rz": -14},
                                      "pPlane30": {"rz": 12}, "pPlane29": {"rz": 6}},
         lean(-2, 2), arms(152, 168, -2, gf=8, gb=12), hair(-4))
MOTIONS["M_S_5"] = {"end": 180, "loop": True, "keys": [
    (0, _SIT), (90, P(_SIT, lean(-3, 1), {"locator2": {"ty": -7.6, "sy": 1.0625}})), (180, _SIT)]}
MOTIONS["M_N_11"] = {"end": 18, "keys": [
    (0, _SIT), (6, P(_SIT, lean(-2, 10))), (12, P(_SIT, lean(-2, -4))), (18, _SIT)]}
# dazed on the ground, in tears (M_D_7, an 8-frame loop); then coming round (M_D_7B)
_DAZED = P(_SIT, lean(4, 8), arms(165, 160, 4, gf=1, gb=1, ff=0, fb=25), face(eyes=SHUT, mouth=2))
MOTIONS["M_D_7"] = {"end": 8, "loop": True, "keys": [
    (0, _DAZED), (4, P(_DAZED, lean(2, 2))), (8, _DAZED)]}
MOTIONS["M_D_7B"] = {"end": 8, "keys": [
    (0, _DAZED), (4, P(_SIT, lean(-2, -6), face(eyes=WIDE))), (8, P(_SIT, lean(-2, -4), face(eyes=WIDE)))]}
# asleep on her back (M_I_6): lying face up, her crown toward her back
_ASLEEP = P({"locator2": {"rz": -90, "tx": -13.0, "ty": 9.8}}, lean(0, 10), arms(160, 175, 0, -90, gf=8, gb=12),
            face(eyes=SHUT), skirt(-4), hair(20))
MOTIONS["M_I_6"] = {"end": 0, "keys": [(0, _ASLEEP)]}
# keels over (M_D_8, 16 frames): knocked flat at half speed
MOTIONS["M_D_8"] = slowed(MOTIONS["M_D_2"], 2)
# reading a letter (M_L_2, M_L_3, 130-frame loops; Mario's drawings hold the
# paper): both hands before her at her chest as if holding it, her head bent
# to it, her eyes going down the lines (a nod down each line, back up for
# the next), a blink now and then; M_L_4 the pose held
_READ = P(arms(150, 140, gf=0, gb=0, ff=70, fb=74), lean(2, 8))
MOTIONS["M_L_2"] = {"end": 130, "loop": True, "tracks": tracks(
    held(_READ, 130),
    head_track([(0, 8.0), (14, 10.0), (16, 8.0), (30, 10.0), (32, 8.0), (46, 10.5), (50, 8.0), (64, 10.0),
                (66, 8.0), (80, 10.0), (82, 8.0), (96, 10.5), (100, 8.0), (114, 10.0), (116, 8.0), (130, 8.0)]),
    blinks(130, (40, 42), (104, 106)))}
MOTIONS["M_L_3"] = MOTIONS["M_L_2"]
MOTIONS["M_L_4"] = {"end": 0, "keys": [(0, P(_READ, {"locator3": {"rz": 10.0}}))]}
# hopping (M_F_1, a 16-frame loop; Mario stamps and waves): two hops, her
# far hand waving high in turn with the near one flung back
_JOY = face(eyes=SHUT, mouth=1)
MOTIONS["M_F_1"] = {"end": 16, "loop": True, "keys": [
    (0, P({"locator2": {"sy": 0.9375}}, arms(200, FAR_UP + 8, gf=1, gb=1), skirt(6), _JOY)),
    (4, P({"locator2": {"ty": 1.6, "sy": 1.0625}}, arms(236, FAR_UP + 24, gf=1, gb=1), skirt(-6, 0.0625), hair(-10),
          _JOY)),
    (8, P({"locator2": {"sy": 0.9375}}, arms(196, FAR_UP + 6, gf=1, gb=1), skirt(6), hair(8), _JOY)),
    (12, P({"locator2": {"ty": 1.6, "sy": 1.0625}}, arms(236, FAR_UP + 24, gf=1, gb=1), skirt(-6, 0.0625),
           hair(-10), _JOY)),
    (16, P({"locator2": {"sy": 0.9375}}, arms(200, FAR_UP + 8, gf=1, gb=1), skirt(6), _JOY))]}
# a wind-up and throw (M_W_4B, 8 frames): the near hand back up behind her
# head, then over (the near arm's jump past her face) and out before her
MOTIONS["M_W_4B"] = {"end": 8, "keys": [
    (0, P(lean(-8, -2), arms(360 + NEAR_UP - 16, 120, -8, gf=8))),
    (3, P(lean(-12, -3), arms(360 + NEAR_UP - 36, 116, -12, gf=8), hair(-6))),
    (5, P(lean(12, 3), arms(460, 220, 12, gf=1), skirt(5), hair(8)), {"ease": "vector"}),
    (8, P(lean(8, 2), arms(470, 214, 8, gf=1), skirt(4), hair(6)))]}
# e_mario's poses no script uses, so none falls back: a march (M_W_4A, a
# 40-frame loop) and its end (M_W_4C), standing (M_I_7, M_I_M), pointing
# ahead (M_L_1), walking with something held in front (M_W_5, a 40-frame loop)
MOTIONS["M_W_4A"] = {"end": 40, "loop": True, "tracks": tracks(bob(40, 10), {
    "pPlane21": {"rz": [(0, -30.0), (10, 30.0), (20, -30.0), (30, 30.0), (40, -30.0)]},
    "pPlane22": {"rz": [(0, 30.0), (10, -30.0), (20, 30.0), (30, -30.0), (40, 30.0)]}})}
MOTIONS["M_W_4C"] = {"end": 6, "keys": [(0, _STRIDE), (6, {})]}
MOTIONS["M_I_7"] = {"end": 0, "keys": [(0, {})]}
MOTIONS["M_I_M"] = {"end": 0, "keys": [(0, lean(0, -3))]}
MOTIONS["M_L_1"] = {"end": 0, "keys": [(0, P(lean(3, -2), arms(150, 84, 3, gb=12, fb=78)))]}
MOTIONS["M_W_5"] = {"end": 40, "loop": True, "tracks": tracks(held(arms(126, 120, gf=1, gb=1), 40), bob(40, 10))}

# ------------------------------------------------ batch 5: the rest of battle

for _n in ("M_N_2", "M_I_Y", "M_I_O", "M_N_C", "M_U_3", "M_A_1A", "M_A_1B", "M_D_7", "M_D_6"):
    MOTIONS[_n + "@b"] = MOTIONS[_n]
MOTIONS["M_N_5@b"] = MOTIONS["M_N_5A"]
MOTIONS["M_P_1@b"] = MOTIONS["M_P_1"]
MOTIONS["M_P_2@b"] = MOTIONS["M_P_2"]

# a cheer, a fist up for a moment (M_N_8, 60 frames)
MOTIONS["M_N_8@b"] = {"end": 60, "keys": [
    (0, {}), (10, _FIST, {"ease": "vector"}), (16, P(_FIST, {"locator2": {"ty": 0.0, "sy": 1.0}}, skirt(6))),
    (45, P(_FIST, {"locator2": {"ty": 0.0, "sy": 1.0}}, arms(214, FAR_UP + 10, -4, gf=1, gb=12))), (60, {})]}

# in the air: tucked (M_A_4, a 4-frame loop: Mario's one drawing, the game
# turns him), upside down (M_A_5), knocked flying backwards (M_A_6)
_BALL = P(arms(200, 160, gf=8, gb=12), skirt(-8, 0.125), hair(20), face(eyes=SHUT, mouth=3),
          {"locator2": {"sy": 0.875}}, lean(8))
MOTIONS["M_A_4@b"] = {"end": 4, "loop": True, "keys": [(0, _BALL), (2, P(_BALL, hair(16), skirt(-6, 0.125))),
                                                       (4, _BALL)]}
_UPENDED = P(_BALL, spun(180, (-1.0, 11.0)), {"locator2": {"rz": 180, "tx": -2.0, "ty": 24.0}})  # (spun 180, 2 higher)
MOTIONS["M_A_5@b"] = {"end": 2, "keys": [(0, _UPENDED), (2, _UPENDED)]}
_FLYING = P({"locator2": {"rz": -90, "tx": -13.0, "ty": 6.0}}, lean(0, 14), arms(40, 70, 0, -90, gf=1, gb=1),
            skirt(-6), hair(30), face(eyes=SHUT, mouth=2))
MOTIONS["M_A_6@b"] = {"end": 0, "keys": [(0, _FLYING)]}

# stunned, reeling back (M_Z_10) and laid out face down (M_Z_11)
MOTIONS["M_Z_10@b"] = {"end": 0, "keys": [(0, P(lean(-12, -6), arms(360 + NEAR_UP - 40, FAR_UP + 44, -12, gf=1, gb=1),
                                              skirt(-4), hair(-10), face(eyes=SHUT, mouth=2)))]}
MOTIONS["M_Z_11@b"] = {"end": 0, "keys": [(0, _FLAT)]}

# using an item, held out in front (M_C_4, 16 frames)
_ITEM = P(arms(130, 112, gf=1, gb=1, ff=96, fb=92), lean(2, 4), face(mouth=1))
MOTIONS["M_C_4@b"] = {"end": 16, "keys": [
    (0, _ITEM), (8, P(_ITEM, {"locator2": {"ty": 0.4, "sy": 1.0625}}, arms(124, 106, gf=1, gb=1, ff=88, fb=84),
                      hair(-3))), (16, _ITEM)]}

# the Appeal (M_C_2A, 14 frames: hands to her chest; M_C_2B, a 6-frame loop,
# holding; M_C_2C, 36 frames: a hand to the crowd)
MOTIONS["M_C_2A@b"] = {"end": 14, "keys": [(0, {}), (6, P(_CLASPED, arms(124, 140, gf=8, gb=12))), (14, _CLASPED)]}
# (M_C_2B: Mario spreads his arms to the crowd; hers open wide, a dip of
# the skirt as if to curtsy)
_TADA = P(arms(360 + NEAR_UP - 40, FAR_UP + 34, gf=1, gb=1), lean(-3, -4), face(mouth=1), skirt(4))
MOTIONS["M_C_2B@b"] = {"end": 6, "loop": True, "keys": [
    (0, _TADA), (3, P(_TADA, {"locator2": {"sy": 0.9375}}, skirt(7), hair(4))), (6, _TADA)]}
MOTIONS["M_C_2C@b"] = MOTIONS["M_C_2"]

# a tumble and back on her feet, hammer away (M_H_16, 10 frames)
MOTIONS["M_H_16@b"] = {"end": 10, "keys": [(f * 10.0 / 12.0, pose, *opt)
                                           for f, pose, *opt in MOTIONS["M_U_3"]["keys"]]}
# the spin hammer's poses in battle (Super's are the field's M_H_6A/6B)
MOTIONS["M_H_2A@b"] = MOTIONS["M_H_3A"]
MOTIONS["M_H_2B@b"] = MOTIONS["M_H_3B"]
MOTIONS["M_H_10A@b"] = MOTIONS["M_H_9A"]
MOTIONS["M_H_10B@b"] = MOTIONS["M_H_9B"]
MOTIONS["M_H_10@b"] = MOTIONS["M_H_9"]
MOTIONS["M_H_2@b"] = MOTIONS["M_H_3"]

# ------------------------------------------------ her back-facing model (R)


def r_arms(left=None, right=None, gl=None):
    p = {}
    if left is not None:
        p["pPlane16"] = {"rz": left - R_LEFT_ARM}
    if right is not None:
        p["pPlane15"] = {"rz": right - R_RIGHT_ARM}
    if gl is not None:
        p["pPlane20"] = {"tex": gl}
    return p


def r_skirt(f):
    return {"pPlane21": {"rz": -f}, "pPlane23": {"rz": -f * 0.5}, "pPlane24": {"rz": f * 0.25},
            "pPlane22": {"rz": f}}


# Seen from behind, Mario's front is to the left still (the paper turn
# mirrors her back model): the hammer goes up over her head and comes down
# in front of her, out of sight behind her, the hair swinging.
def _r_swing_key(h, tex, back, lift=0.0, flare=0.0, hr=0.0):
    p = P({"locator3": {"rz": h * 0.06}, "locator4": {"rz": h * 0.06}}, r_arms(h, back), hammer(h, tex),
          r_skirt(flare), {"group2": {"rz": hr}})
    if lift:
        p["locator2"] = {"ty": lift}
    return p


MOTIONS_R["M_H_1R"] = {"end": 10, "keys": [
    (0, _r_swing_key(-60, 0, 150)), (1, _r_swing_key(-25, 0, 150, 0.2)),
    (2, _r_swing_key(10, 0, 160, 0.4, -2)), (3, _r_swing_key(45, 2, 180, 0.5, -3)),
    (4, _r_swing_key(85, 2, 200, 0.1, 0, 3), {"ease": "linear"}),
    (5, _r_swing_key(118, 3, 225, 0, 6, 6), {"ease": "vector"}),
    (6, _r_swing_key(114, 3, 222, 0, 4, 4)), (10, _r_swing_key(116, 3, 223, 0, 5, 5))]}
MOTIONS_R["M_H_2R"] = {"end": 6, "keys": MOTIONS_R["M_H_1R"]["keys"][:4] + [
    (4, _r_swing_key(42, 2, 170, 0.3, -4, -3), {"ease": "vector"}), (6, _r_swing_key(42, 2, 170, 0.3, -4, -3))]}

# jumping, from behind (M_J_1BR at the top, M_J_1CR coming down)
# (as from the front: one hand up before her, the other swept down behind)
_R_TOP = P(r_arms(24, 210, gl=1), r_skirt(10), {"group2": {"rz": 6}})
MOTIONS_R["M_J_1BR"] = {"end": 2, "keys": [(0, _R_TOP), (2, _R_TOP)]}
MOTIONS_R["M_J_1CR"] = {"end": 8, "loop": True, "keys": [
    (0, _R_TOP), (4, P(_R_TOP, r_arms(20, 218, gl=1), r_skirt(14), {"group2": {"rz": 9}})), (8, _R_TOP)]}


def r_lean(t, nod=0.0):
    """bent at the waist (her back model: the torso and head about the waist)"""
    return {"locator4": {"rz": t}, "locator3": {"rz": t + nod}}


def r_bob(end, step, up=0.4, sway=2.0):
    n = int(round(end / step))
    return {"locator2": {"ty": [(i * step, up if i % 2 else 0.0) for i in range(n + 1)]},
            "group1": {"rz": [(i * step, 0.0 if i % 2 else (sway if i % 4 == 0 else -sway)) for i in range(n + 1)]},
            "group2": {"rz": [(i * step, 3.0 if i % 2 else -1.5) for i in range(n + 1)]}}


# pushing, from behind (M_O_2R, a 32-frame loop): both hands flat to it
MOTIONS_R["M_O_2R"] = {"end": 32, "loop": True, "tracks": tracks(
    held(P(r_lean(14, -4), r_arms(96, 96, gl=1)), 32), r_bob(32, 8, up=0.2))}

# lifting overhead, from behind (M_U_2R, 8 frames): arms stretched up
_R_UP = P(r_arms(30, -30, gl=1), {"pPlane16": {"ty": 3.0}, "pPlane15": {"ty": 3.0}, "pPlane20": {"sy": 1.625},
                                  "pPlane19": {"sy": 1.625}})
MOTIONS_R["M_U_2R"] = {"end": 8, "keys": [
    (0, P(r_lean(6), {"locator2": {"sy": 0.9375}}, r_arms(175, 170))),
    (3, P({"locator2": {"sy": 1.0625}}, r_arms(70, 80, gl=1))),
    (6, P(_R_UP, {"locator2": {"ty": 0.2, "sy": 1.0625}})), (8, _R_UP)]}

# a throw, from behind (M_C_1R, 8 frames)
MOTIONS_R["M_C_1R"] = {"end": 8, "keys": [
    (0, P(r_lean(-6), r_arms(-30, 130), {"group2": {"rz": -6}})),
    (2, P(r_lean(-3), r_arms(-5, 140), {"locator2": {"sy": 1.0625}})),
    (3, P(r_lean(5), r_arms(50, 170)), {"ease": "linear"}),
    (5, P(r_lean(12), r_arms(108, 220, gl=1), r_skirt(6), {"group2": {"rz": 10}}), {"ease": "vector"}),
    (8, P(r_lean(10), r_arms(104, 216, gl=1), r_skirt(5), {"group2": {"rz": 8}}))]}

# the Super and Ultra Hammer's versions
levels(MOTIONS, ["M_H_1", "M_H_2"], rename=field_hammer_name)
levels(MOTIONS_R, ["M_H_1R", "M_H_2R"], rename=field_hammer_name)

MODELS = [(FRONT, MOTIONS), (BACK, MOTIONS_R)]
