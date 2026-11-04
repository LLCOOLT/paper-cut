"""Hizza, the two-headed snake: the boss of Super Luigi's chapter 2
(Plumpbelly Village; docs/SUPER_LUIGI.md), `c_sl_hizza` (and _tail, _whole):
cards cut from the style model's design split into pieces by Qwen-Image-Edit
(port/assets/super_luigi/art/tools/cut_hizza.py, art/hizza/qwen/), built by
paper_model.py.

A giant snake on a coil of its own body, its front rearing up into one head
and its tail rising behind into a second, both faced front (toward -x and
Luigi); the front head about 61 units up, the tail's about 69 (Luigi is 24:
2.5x and 2.9x). Both heads are in the one model, so the finale's crash (the
heads lunge at Luigi from two sides, he dodges, they smash into each other) is
one animation, and battle code finds each head by its locator (target_a,
target_b).

Parts (groups), parents first:
  |root                     the root
    body                    the whole snake (the mover)
      coil_back coil_front  the coil: the ring's far half, its near half
      ja1 > ja2 > ja3       the front neck's joints, each carrying its card
                            (neck_a1..3: bands of the drawn neck)
        collar              the band with the compass piece (a drawn card)
        head_a              the main head, Hiss; binds: 0 eyes open, 1 shut, 2 hurt
          jaw_a mouth_a tongue_a  locators (the mouth is drawn open)
          stars_a           dizzy stars (a drawn card, hidden at rest)
          target_a          a locator in the head's middle
      jb1 > jb2 > jb3 > head_b   the tail (tail_b1..3) and its head, Zah: the same parts
    tail_base               Zah's model's foot, cut level at the floor
"""
import json
import math
import os

import paper_model as PM

NAME = "c_sl_hizza"
PX = 8  # texels a unit of the drawn cards (the collar, the dizzy stars)
ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "port", "assets", "super_luigi", "art",
                   "hizza")
# the snake's own cards, cut from Qwen's split of the style model's design (art/tools/cut_hizza.py): each card's
# pivot in texels, where it sits in the model (model_xy), and the joints and locators as model points (anchors)
QART = os.path.join(ART, "qwen")
_CUT = json.load(open(os.path.join(QART, "parts.json")))
_AN = {k: tuple(v) for k, v in _CUT["anchors"].items()}

# Each part: name, parent, its card (in QART, or the drawn ART) or None for a locator, the pivot's place in the
# model (game units: x the back, the snake faces -x; y up), z (back to front), more cards (binds), shown at rest.
# The rest pose is the cut's own: the front neck stands as Qwen drew it, its joints on its middle at the rig's
# spacing; the tail is the S's rising stroke. The necks are chains of joints (locators) each carrying its card, so
# a segment can stretch (its card's sy, the next joint's ty) without stretching the head. The heads' mouths are
# drawn open: jaw, mouth and tongue are locators (the motions' jaw and tongue channels move nothing seen).
_HA, _HB = _AN["head_a"], _AN["head_b"]  # the heads' pivots (on their necks' tops)


def _xy(card):
    return tuple(_CUT["cards"][card]["model_xy"])


LAYOUT = [
    ("|root", None, None, (0.0, 0.0), 0.0, (), True),
    ("body", "|root", None, (4.0, 0.0), 0.0, (), True),
    ("coil_back", "body", "coil_back", _xy("coil_back"), -2.0, (), True),
    ("tail_base", "|root", "tail_base", _xy("tail_base"), -1.55, (), True),
    ("jb1", "body", None, _AN["jb1"], -1.6, (), True),
    ("tail_b1", "jb1", "tail_b1", _AN["jb1"], -1.6, (), True),
    ("jb2", "jb1", None, _AN["jb2"], -1.5, (), True),
    ("tail_b2", "jb2", "tail_b2", _AN["jb2"], -1.5, (), True),
    ("jb3", "jb2", None, _AN["jb3"], -1.4, (), True),
    ("tail_b3", "jb3", "tail_b3", _AN["jb3"], -1.4, (), True),
    ("head_b", "jb3", "head_b", _HB, -1.1, ("head_b_blink", "head_b_hurt"), True),
    ("tongue_b", "head_b", None, _AN["tongue_b"], -1.25, (), True),
    ("mouth_b", "head_b", None, _AN["mouth_b"], -1.2, (), False),
    ("jaw_b", "head_b", None, _AN["jaw_b"], -1.15, (), True),
    ("stars_b", "head_b", "dizzy_stars", _AN["stars_b"], -1.0, (), False),
    ("target_b", "head_b", None, _AN["target_b"], -1.1, (), True),
    ("ja1", "body", None, _AN["ja1"], -0.4, (), True),
    ("neck_a1", "ja1", "neck_a1", _AN["ja1"], -0.4, (), True),
    ("ja2", "ja1", None, _AN["ja2"], -0.3, (), True),
    ("neck_a2", "ja2", "neck_a2", _AN["ja2"], -0.3, (), True),
    ("ja3", "ja2", None, _AN["ja3"], -0.2, (), True),
    ("neck_a3", "ja3", "neck_a3", _AN["ja3"], -0.2, (), True),
    ("collar", "ja3", "collar", _AN["collar_a"], 0.15, (), True),
    ("head_a", "ja3", "head_a", _HA, 0.2, ("head_a_blink", "head_a_hurt"), True),
    ("tongue_a", "head_a", None, _AN["tongue_a"], 0.0, (), True),
    ("mouth_a", "head_a", None, _AN["mouth_a"], 0.05, (), False),
    ("jaw_a", "head_a", None, _AN["jaw_a"], 0.1, (), True),
    ("stars_a", "head_a", "dizzy_stars", _AN["stars_a"], 0.3, (), False),
    ("target_a", "head_a", None, _AN["target_a"], 0.2, (), True),
    ("coil_front", "body", "coil_front", _xy("coil_front"), 1.0, (), True),
]
# each neck: its joints, their cards, the head; the bite's point (the open mouth's middle, for aiming)
NECK = {"a": (["ja1", "ja2", "ja3"], ["neck_a1", "neck_a2", "neck_a3"], "head_a", _AN["mouth_a"]),
        "b": (["jb1", "jb2", "jb3"], ["tail_b1", "tail_b2", "tail_b3"], "head_b", _AN["mouth_b"])}


def _card(card):
    """(rows, pivot in texels, texels a unit) of a card: the cut's, else the drawn one's"""
    if card in _CUT["cards"]:
        m = _CUT["cards"][card]
        return (PM.pad8(PM.read_png(os.path.join(QART, card + ".png"))), m["pivot"],
                float(m.get("texels_per_unit", _CUT["texels_per_unit"])))
    m = json.load(open(os.path.join(ART, "parts.json")))["cards"][card]
    return PM.pad8(PM.read_png(os.path.join(ART, card + ".png"))), m["pivot"], float(PX)


def parts():
    """paper_model.Parts from the art"""
    out = []
    for name, parent, card, pivot, z, binds, vis in LAYOUT:
        if card is None:
            out.append(PM.Part(name, parent, None, None, z, pivot, (), vis))
            continue
        rows, (px, py), tpu = _card(card)
        h, w = len(rows), len(rows[0])
        x0, y1 = pivot[0] - px / tpu, pivot[1] + py / tpu
        rect = (x0, y1 - h / tpu, x0 + w / tpu, y1)
        more = [_card(b)[0] for b in binds]
        out.append(PM.Part(name, parent, rows, rect, z, pivot, more, vis))
    return out


def build(anims=()):
    """(model, textures) of c_sl_hizza"""
    if MISSES:
        raise ValueError("necks that can't reach their aims: %s" % MISSES)
    anims = list(anims)
    if not any(x.name == "Z_1" for x in anims):
        anims.insert(0, PM.still("Z_1"))
    return PM.build(NAME, parts(), anims)


# --------------------------------------------------------------- the rig ----
# 2D forward kinematics, as the game's nodes turn (each part about its pivot,
# in its parent's frame): the motions below aim the heads with it.

_INFO = {n: (p, piv) for n, p, _c, piv, _z, _b, _v in LAYOUT}


def _mat(rz, sx, sy, piv, t=(0.0, 0.0)):
    c, s = math.cos(math.radians(rz)), math.sin(math.radians(rz))
    px, py = piv
    # T(t) T(p) R S T(-p)
    a, b, cc, d = c * sx, -s * sy, s * sx, c * sy
    return (a, b, t[0] + px - (a * px + b * py), cc, d, t[1] + py - (cc * px + d * py))


def _mul(m, n):
    a, b, c, d, e, f = m
    g, h, i, j, k, l = n
    return (a * g + b * j, a * h + b * k, a * i + b * l + c, d * g + e * j, d * h + e * k, d * i + e * l + f)


def _app(m, p):
    return (m[0] * p[0] + m[1] * p[1] + m[2], m[3] * p[0] + m[4] * p[1] + m[5])


def world(pose, name):
    """the world matrix of a part in a pose ({part: {rz, sx, sy, tx, ty}})"""
    chain = []
    while name is not None:
        chain.append(name)
        name = _INFO[name][0]
    m = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    for n in reversed(chain):
        q = pose.get(n, {})
        m = _mul(m, _mat(q.get("rz", 0.0), q.get("sx", 1.0), q.get("sy", 1.0), _INFO[n][1],
                         (q.get("tx", 0.0), q.get("ty", 0.0))))
    return m


def point(pose, name, p=None):
    """where a part's point (default its pivot) is in a pose"""
    return _app(world(pose, name), p or _INFO[name][1])




def _ancestors(name):
    out = []
    name = _INFO[name][0]
    while name is not None:
        out.append(name)
        name = _INFO[name][0]
    return out


def _copy(pose):
    return {k: dict(v) for k, v in pose.items()}


def reach(pose, neck, tip_at, angle, stretch=1.0, flip=False, limit=100.0):
    """the pose with a neck aimed so its snout's tip is at `tip_at`, the head
    turned `angle` degrees in the world (its own frame; flipped, the head faces
    +x); each segment stretched by `stretch` (its card's sy, the next joint
    moved along). The joints' turns are the smallest, most even bend that gets
    there (coordinate descent; each joint within +-limit of its pose's).
    Returns (pose, miss)."""
    joints, cards, head, tip = NECK[neck]
    pose = _copy(pose)
    for k, (j, c) in enumerate(zip(joints, cards)):
        nxt = joints[k + 1] if k + 1 < len(joints) else head
        a, b = _INFO[j][1], _INFO[nxt][1]
        pose.setdefault(c, {})["sy"] = stretch
        pose.setdefault(nxt, {})["ty"] = (stretch - 1.0) * math.hypot(b[0] - a[0], b[1] - a[1])
    pose.setdefault(head, {})["sx"] = -1.0 if flip else 1.0
    start = [pose.get(j, {}).get("rz", 0.0) for j in joints]

    def apply(v):
        for j, x in zip(joints, v):
            pose.setdefault(j, {})["rz"] = x
        pose[head]["rz"] = angle - sum(pose.get(n, {}).get("rz", 0.0) for n in _ancestors(head))

    def cost(v):
        apply(v)
        e = point(pose, head, tip)
        miss = (e[0] - tip_at[0]) ** 2 + (e[1] - tip_at[1]) ** 2
        d = [x - s for x, s in zip(v, start)]
        mean = sum(d) / len(d)
        return miss * 4.0 + 0.002 * sum((x - mean) ** 2 for x in d) + 0.0005 * sum(x * x for x in d)
    v = list(start)
    best = cost(v)
    step = 32.0
    while step > 0.05:
        moved = False
        for i in range(len(v)):
            for sgn in (1, -1):
                w = list(v)
                w[i] = max(start[i] - limit, min(start[i] + limit, w[i] + sgn * step))
                c = cost(w)
                if c < best:
                    v, best, moved = w, c, True
        if not moved:
            step /= 2
    apply([round(x, 1) for x in v])
    pose[head]["rz"] = round(pose[head]["rz"], 1)
    e = point(pose, head, tip)
    return pose, math.hypot(e[0] - tip_at[0], e[1] - tip_at[1])


MISSES = []  # aims a neck can't reach (checked when the model is built)


def aimed(base, a=None, b=None, **extra):
    """a pose: `base` with neck a and b aimed ((tip, angle[, stretch[, flip]]) or None)
    and any other parts' channels merged in"""
    pose = _copy(base)
    for neck, spec in (("a", a), ("b", b)):
        if spec is None:
            continue
        tip, ang = spec[0], spec[1]
        st = spec[2] if len(spec) > 2 else 1.0
        fl = spec[3] if len(spec) > 3 else False
        # the stretch asked for, else the nearest that reaches (sixteenths, 0.75 to 1.75)
        tries = sorted({st} | {k / 16.0 for k in range(12, 29)}, key=lambda x: abs(x - st))
        for s2 in tries:
            got, miss = reach(pose, neck, tip, ang, s2, fl)
            if miss <= 0.75:
                break
        pose = got
        if miss > 0.75:
            MISSES.append((neck, tip, ang, st, round(miss, 1)))
    for g, ch in extra.items():
        pose.setdefault(g, {}).update(ch)
    return pose


# ---------------------------------------------------------------- motions ----
# Key poses as luigi_motions.py's (offsets from rest per part: tx ty in game
# units, rz in degrees (counter-clockwise seen from the front: a neck's
# positive rz leans it toward the front, -x), sx sy multiples of rest, snapped
# to sixteenths; vis; tex the drawing). 60 frames a second, the game's big
# enemies' timing: idles 60-80, hits 8-16, wind-ups 20-36 then a sharp strike.
#
# Where Luigi stands for the attacks: SPOT, 30 units in front of the model's
# origin, his middle 12 up. The battle script brings Hizza there first.

SPOT = (-30.0, 12.0)
MOTIONS = {}


def _m(end, keys, loop=False, tracks=None):
    m = {"end": end, "loop": loop, "keys": keys}
    if tracks:
        m["tracks"] = tracks
    return m


def _merge(*ps):
    out = {}
    for p in ps:
        for g, ch in (p or {}).items():
            out.setdefault(g, {}).update(ch)
    return out


def _sway(phase, amp=1.0):
    """the necks' idle sway, the two out of step"""
    s, c = math.sin(phase), math.sin(phase + 1.9)
    return {"ja1": {"rz": 2.0 * s * amp}, "ja2": {"rz": 1.8 * s * amp}, "ja3": {"rz": -1.2 * s * amp},
            "head_a": {"rz": -1.5 * s * amp},
            "jb1": {"rz": -2.0 * c * amp}, "jb2": {"rz": -1.5 * c * amp}, "jb3": {"rz": 1.3 * c * amp},
            "head_b": {"rz": 1.5 * c * amp}}


def _breath(k):
    """the coil's breathing: k 0 out .. 1 in"""
    return {"coil_back": {"sy": 1.0 + 0.0625 * k}, "coil_front": {"sy": 1.0 + 0.03125 * k}}


def _stand(f, period=80, amp=1.0):
    ph = 2 * math.pi * f / period
    return _merge(_sway(ph, amp), _breath(0.5 - 0.5 * math.cos(ph)))


def _jaws(a=None, b=None):
    """the jaws open (degrees); past 20 the mouth's inside shows (the art's mouth_x)"""
    out = {}
    for k, v in (("a", a), ("b", b)):
        if v is not None:
            out["jaw_" + k] = {"rz": v}
            out["mouth_" + k] = {"vis": 1 if v > 20 else 0}
    return out


def _eyes(tex, *heads):
    return {h: {"tex": tex} for h in heads}


TONGUE_OUT = -5.75  # the tongue slid out of the mouth (the art's 46 texels)


def _tongue(which, out=1.0):
    """the tongue slid out (0 in, 1 all the way)"""
    return {"tongue_" + which: {"tx": TONGUE_OUT * out}}


def _stars(*which):
    return {"stars_" + w: {"vis": 1} for w in which}


REST = {}

# ---- standing, idle, talking, moving (field and battle) ----

# S_1: standing: the coil breathes, the necks sway out of step. Loop 80.
MOTIONS["S_1"] = _m(80, [(f, _stand(f)) for f in (0, 20, 40, 60, 80)], loop=True)

# Z_1: S_1 with the heads blinking apart and flicking their tongues.
_BLINKS = {"head_a": {"tex": [(0, 0, "step"), (30, 1, "step"), (34, 0, "step")]},
           "head_b": {"tex": [(0, 0, "step"), (58, 1, "step"), (62, 0, "step")]}}
_FLICKS = {"tongue_a": {"tx": [(0, 0.0), (10, 0.0), (13, TONGUE_OUT), (15, TONGUE_OUT * 0.6), (17, TONGUE_OUT),
                               (20, 0.0)]},
           "tongue_b": {"tx": [(0, 0.0), (44, 0.0), (47, TONGUE_OUT), (49, TONGUE_OUT * 0.6), (51, TONGUE_OUT),
                               (54, 0.0)]}}
MOTIONS["Z_1"] = _m(80, MOTIONS["S_1"]["keys"], loop=True, tracks=_merge(_BLINKS, _FLICKS))


def _talk(which, end=32):
    jaw = "jaw_" + which
    head = "head_" + which
    flap = [(0, 0), (5, 14), (10, 3), (15, 11), (21, 2), (26, 13), (32, 0)]
    nod = [(0, 0), (5, -3), (10, 0), (15, -2), (21, 0), (26, -3), (32, 0)]
    keys = [(f, _merge(_stand(f * 2.5), {jaw: {"rz": j}, head: {"rz": _stand(f * 2.5)[head]["rz"] + n}}))
            for (f, j), (_f, n) in zip(flap, nod)]
    return _m(end, keys, loop=True)


# T_1: the main head talks; T_2: the tail's head talks. Loop 32.
MOTIONS["T_1"] = _talk("a")
MOTIONS["T_2"] = _talk("b")


def _slither(f, period):
    ph = 2 * math.pi * f / period
    s, c = math.sin(ph), math.cos(ph)
    return _merge(_sway(ph * 2, 1.6), {
        "body": {"tx": 1.2 * s, "ty": 0.4 * abs(c), "rz": -1.5 * s},
        "coil_back": {"sx": 1.0 + 0.0625 * s, "sy": 1.0 - 0.0625 * s},
        "coil_front": {"sx": 1.0 - 0.0625 * s, "sy": 1.0 + 0.0625 * s}})


# W_1: moving (slithering): the coils roll against each other, the whole body
# sways and the necks swing with it. Loop 40. R_1: the same, faster (loop 24).
MOTIONS["W_1"] = _m(40, [(f, _slither(f, 40)) for f in (0, 10, 20, 30, 40)], loop=True)
MOTIONS["R_1"] = _m(24, [(f, _slither(f * 40 / 24.0, 40)) for f in (0, 6, 12, 18, 24)], loop=True)

# ---- the pincer: both heads at once (the finale's move) ----
# A_1A: the set-up, held at its end: both heads draw back and up, then the
# main head swings out low past Luigi's spot and turns round (the paper flip)
# to face him from the far side, the tail's head high on the near side.
# A_1B: the strike, both heads bite Luigi from both sides. A_1C: back to S_1.
# X_1: the crash: the strike with Luigi gone (he dodged): the mouths smash
# into each other, the heads squash, recoil and sag, seeing stars; held.
# X_2: dazed (loop 48), from X_1's end. X_3: shaking it off, back to S_1.
_UP = aimed(REST, a=((-18.0, 55.0), -25.0, 1.15), b=((14.0, 55.0), -20.0, 1.1),
            body={"ty": 0.5}, coil_back={"sy": 0.9375}, coil_front={"sy": 0.9375})
_OUT = aimed(REST, a=((-40.0, 34.0), 15.0, 1.2), b=((10.0, 56.0), -30.0, 1.1))
_SET = aimed(REST, a=((-42.0, 28.0), -40.0, 1.25, True), b=((-16.0, 30.0), 40.0, 1.2),
             **_merge(_jaws(26, 26), {"body": {"tx": -2.0}}))
MOTIONS["A_1A"] = _m(36, [
    (0, REST),
    (10, _UP),
    (20, _merge(_OUT, _jaws(10, 6)), {"ease": "linear"}),
    (24, _merge(aimed(REST, a=((-46.0, 32.0), 0.0, 1.25), b=((-6.0, 44.0), 15.0, 1.15)),
                {"head_a": {"sx": 0.0625}}, _jaws(14, 14)), {"ease": "linear"}),
    (28, _SET),
    (36, _SET),
])
_BITE_A = aimed(REST, a=((-33.0, 15.0), -30.0, 1.25, True), b=((-27.0, 16.0), 30.0, 1.2),
                **_merge(_jaws(0, 0), {"body": {"tx": -2.0}}))
_BITE_HOLD = _merge(aimed(REST, a=((-36.0, 18.0), -25.0, 1.25, True), b=((-24.0, 19.0), 25.0, 1.2)),
                    _jaws(4, 4), {"body": {"tx": -2.0}})
MOTIONS["A_1B"] = _m(20, [
    (0, _SET),
    (4, _BITE_A, {"ease": "vector"}),
    (7, _merge(aimed(REST, a=((-34.0, 14.0), -34.0, 1.25, True), b=((-26.0, 15.0), 34.0, 1.2)),
               _jaws(0, 0), {"body": {"tx": -2.0, "ty": -0.4}}, {"coil_back": {"sy": 0.9375}})),
    (12, _BITE_HOLD),
    (20, _BITE_HOLD),
])
MOTIONS["A_1C"] = _m(30, [
    (0, _BITE_HOLD),
    (8, _merge(aimed(REST, a=((-44.0, 28.0), 5.0, 1.2), b=((-4.0, 40.0), 0.0, 1.1)), {"head_a": {"sx": 0.0625}})),
    (12, aimed(REST, a=((-40.0, 30.0), 10.0, 1.15), b=((4.0, 46.0), -5.0, 1.05))),
    (22, _merge(_stand(0), {"ja1": {"rz": -4.0}, "ja2": {"rz": -2.0}, "jb1": {"rz": 3.0}})),
    (30, _stand(0)),
])

_MEET = aimed(REST, a=((-31.0, 18.0), -20.0, 1.25, True), b=((-29.0, 18.0), 20.0, 1.2),
              **_merge(_jaws(30, 30), {"body": {"tx": -2.0}}))
_SQUASH = _merge(aimed(REST, a=((-32.0, 18.5), -12.0, 1.25, True), b=((-28.0, 18.5), 12.0, 1.2)),
                 {"head_a": {"sx": -0.8125, "sy": 1.1875}, "head_b": {"sx": 0.8125, "sy": 1.1875}},
                 _jaws(0, 0), _eyes(2, "head_a", "head_b"), _stars("a", "b"), {"body": {"tx": -2.0}})
_RECOIL = _merge(aimed(REST, a=((-42.0, 30.0), 10.0, 1.2, True), b=((-16.0, 32.0), -10.0, 1.15)),
                 _jaws(8, 8), _eyes(2, "head_a", "head_b"), _stars("a", "b"), {"body": {"tx": -1.0}},
                 {"coil_back": {"sy": 1.0625}})
_DAZED = _merge(aimed(REST, a=((-40.0, 14.0), -20.0, 1.2, True), b=((-20.0, 16.0), 20.0, 1.15)),
                _jaws(10, 10), _eyes(2, "head_a", "head_b"), _tongue("a", 0.6), _tongue("b", 0.6), _stars("a", "b"),
                {"body": {"tx": -1.0}, "coil_back": {"sy": 0.9375}, "coil_front": {"sy": 0.9375}})
MOTIONS["X_1"] = _m(60, [
    (0, _SET),
    (5, _MEET, {"ease": "vector"}),
    (8, _SQUASH, {"ease": "vector"}),
    (16, _RECOIL),
    (24, _merge(aimed(REST, a=((-40.0, 24.0), -30.0, 1.2, True), b=((-20.0, 26.0), 30.0, 1.15)),
                _jaws(10, 10), _eyes(2, "head_a", "head_b"), _stars("a", "b"), {"body": {"tx": -1.0}})),
    (34, _merge(aimed(REST, a=((-41.0, 12.0), -28.0, 1.2, True), b=((-19.0, 14.0), 28.0, 1.15)),
                _jaws(16, 14), _eyes(2, "head_a", "head_b"), _stars("a", "b"), {"body": {"tx": -1.0}})),
    (46, _DAZED),
    (60, _DAZED),
])


def _daze(k):
    ph = 2 * math.pi * k / 4.0
    dx, dy = 2.0 * math.cos(ph), 1.5 * math.sin(ph)
    return _merge(aimed(REST, a=((-40.0 + dx, 14.0 + dy), -20.0 + 6 * math.sin(ph), 1.2, True),
                        b=((-20.0 - dx, 16.0 + dy), 20.0 - 6 * math.sin(ph), 1.15)),
                  _jaws(10, 10), _eyes(2, "head_a", "head_b"), _tongue("a", 0.6 + 0.25 * math.sin(ph)),
                  _tongue("b", 0.6 - 0.25 * math.sin(ph)), _stars("a", "b"),
                  {"stars_a": {"vis": 1, "rz": 12 * math.sin(ph)}, "stars_b": {"vis": 1, "rz": -12 * math.sin(ph)}},
                  {"body": {"tx": -1.0}, "coil_back": {"sy": 0.9375}, "coil_front": {"sy": 0.9375}})


MOTIONS["X_2"] = _m(48, [(12 * k, _daze(k)) for k in range(5)], loop=True)
MOTIONS["X_3"] = _m(36, [
    (0, _daze(0)),
    (8, _merge(aimed(REST, a=((-40.0, 20.0), -10.0, 1.2, True), b=((-18.0, 24.0), 10.0, 1.15)),
               _eyes(1, "head_a", "head_b"))),
    (12, _merge(aimed(REST, a=((-40.0, 26.0), 5.0, 1.2), b=((-10.0, 36.0), 0.0, 1.1)),
                {"head_a": {"sx": 0.0625}})),
    (16, aimed(REST, a=((-36.0, 32.0), 10.0, 1.1), b=((0.0, 44.0), -5.0, 1.05))),
    (26, _merge(_stand(0), {"ja1": {"rz": -4.0}, "jb1": {"rz": 3.0}})),
    (36, _stand(0)),
])


# ---- one head at a time ----

def _bite(neck, up, hit, end=24):
    """wind-up (A, held at its end) and bite (B) for one head"""
    jaw, head = "jaw_" + neck, "head_" + neck
    other = "b" if neck == "a" else "a"
    look = {("j%s1" % other): {"rz": 6.0 if other == "a" else -6.0}}
    wind = _merge(aimed(REST, **{neck: up}), look, {"coil_back": {"sy": 0.9375}})
    open_ = _merge(wind, {jaw: {"rz": 38.0}, "mouth_" + neck: {"vis": 1}})
    a = _m(24, [(0, REST), (10, _merge(wind, {jaw: {"rz": 8.0}})), (18, open_), (24, open_)])
    strike = _merge(aimed(REST, **{neck: hit}), look, {jaw: {"rz": 0.0}, "mouth_" + neck: {"vis": 0}})
    over = _merge(aimed(REST, **{neck: ((hit[0][0] - 0.5, hit[0][1] - 1.0),) + tuple(hit[1:])}),
                  look, {jaw: {"rz": 0.0}, head: {"sy": 0.875}}, {"coil_back": {"sy": 1.0625}})
    back = aimed(REST, **{neck: ((hit[0][0] * 0.5 + up[0][0] * 0.5, hit[0][1] * 0.5 + up[0][1] * 0.5),
                                 (hit[1] + up[1]) / 2.0, 1.0 + (hit[2] - 1.0) * 0.4)})
    b = _m(end, [(0, open_), (3, strike, {"ease": "vector"}), (6, over), (14, _merge(back, {jaw: {"rz": 4.0}})),
                 (end, _stand(0))])
    return a, b


# A_2A / A_2B: the main head's bite: it rears back, mouth wide (held), then
# strikes down at Luigi's spot and snaps shut, and pulls back.
MOTIONS["A_2A"], MOTIONS["A_2B"] = _bite("a", ((-6.0, 56.0), -35.0, 1.1), ((-30.0, 14.0), 35.0, 0.8125))
# A_3A / A_3B: the tail's head: rears back high, then arcs over the coil.
MOTIONS["A_3A"], MOTIONS["A_3B"] = _bite("b", ((26.0, 54.0), -30.0, 1.15), ((-28.0, 15.0), 30.0, 1.3))

# ---- the body slam ----
# A_4A: coiling down to spring (held); A_4B: up off the floor and down with a
# crash (the battle's shockwave at frame 16).
_COIL = _merge(aimed(REST, a=((-24.0, 30.0), 15.0, 0.9375), b=((14.0, 30.0), 10.0, 0.9375)),
               {"body": {"ty": -1.0}, "coil_back": {"sy": 0.8125, "sx": 1.0625},
                "coil_front": {"sy": 0.8125, "sx": 1.0625}}, _jaws(10, 10))
MOTIONS["A_4A"] = _m(30, [(0, REST), (12, _merge(_COIL, {"body": {"ty": -0.5}})), (22, _COIL), (30, _COIL)])
_AIR = _merge(aimed(REST, a=((-26.0, 48.0), -10.0, 1.1), b=((16.0, 46.0), -10.0, 1.1)),
              {"body": {"ty": 18.0}, "coil_back": {"sy": 1.125, "sx": 0.9375},
               "coil_front": {"sy": 1.125, "sx": 0.9375}}, _jaws(22, 22))
_LAND = _merge(aimed(REST, a=((-28.0, 28.0), 20.0, 1.0), b=((16.0, 34.0), 15.0, 1.0)),
               {"body": {"ty": 0.0}, "coil_back": {"sy": 0.75, "sx": 1.125},
                "coil_front": {"sy": 0.75, "sx": 1.125}}, _jaws(0, 0))
MOTIONS["A_4B"] = _m(40, [
    (0, _COIL),
    (6, _merge(_AIR, {"body": {"ty": 12.0}}), {"ease": "linear"}),
    (11, _AIR),
    (16, _LAND, {"ease": "vector"}),
    (20, _merge(_stand(0), {"coil_back": {"sy": 1.0625}, "coil_front": {"sy": 1.0625}, "body": {"ty": 0.0}})),
    (28, _merge(_stand(0), {"coil_back": {"sy": 0.9375}})),
    (40, _stand(0)),
])


# ---- hurt ----
# D_1: both heads hit (loop 12); D_2: the main head only; D_3: the tail's head.

def _hurt(which):
    ks = []
    for f, k in ((0, 0.0), (3, 1.0), (6, 0.5), (9, 0.85), (12, 0.0)):
        p = {}
        if "a" in which:
            p = _merge(p, {"ja2": {"rz": -8 * k}, "ja3": {"rz": -10 * k}, "head_a": {"rz": -12 * k, "tex": 2},
                           "jaw_a": {"rz": 16 * k}})
        if "b" in which:
            p = _merge(p, {"jb2": {"rz": 8 * k}, "jb3": {"rz": -10 * k}, "head_b": {"rz": -10 * k, "tex": 2},
                           "jaw_b": {"rz": 14 * k}})
        p = _merge(p, {"body": {"tx": 1.0 * k}, "coil_back": {"sx": 1.0 + 0.0625 * k, "sy": 1.0 - 0.0625 * k}})
        ks.append((f, p))
    return _m(12, ks, loop=True)


MOTIONS["D_1"] = _hurt("ab")
MOTIONS["D_2"] = _hurt("a")
MOTIONS["D_3"] = _hurt("b")

# ---- the phase change and the angry stand ----
# P_1: enraged (held at its end): the heads sink, then rear up high and wide
# with their mouths open and tongues out, hissing and shaking; S_2: the angry
# stand after it (loop 48): faster sway, mouths ajar, tongues flicking.
_REAR = aimed(REST, a=((-26.0, 62.0), -35.0, 1.25), b=((22.0, 60.0), -40.0, 1.25),
              **_merge(_jaws(36, 36), _tongue("a"), _tongue("b"),
                       {"coil_back": {"sy": 1.125}, "coil_front": {"sy": 1.0625}, "body": {"ty": 1.0}}))
_SINK = _merge(aimed(REST, a=((-28.0, 30.0), 25.0, 0.9375), b=((12.0, 30.0), 20.0, 0.9375)),
               {"coil_back": {"sy": 0.875}, "coil_front": {"sy": 0.875}}, _eyes(1, "head_a", "head_b"))


def _shake(p, k):
    return _merge(p, {"ja1": {"rz": p.get("ja1", {}).get("rz", 0.0) + (2.5 if k % 2 else -2.5)},
                      "jb1": {"rz": p.get("jb1", {}).get("rz", 0.0) + (-2.5 if k % 2 else 2.5)},
                      "tongue_a": {"tx": TONGUE_OUT * (0.75 if k % 2 else 1.0)},
                      "tongue_b": {"tx": TONGUE_OUT * (1.0 if k % 2 else 0.75)}})


_ANGRY = _merge(aimed(REST, a=((-24.0, 52.0), -10.0, 1.1), b=((16.0, 52.0), -12.0, 1.1)), _jaws(12, 12))
MOTIONS["P_1"] = _m(90, [(0, REST), (14, _SINK), (26, _REAR, {"ease": "vector"})] +
                    [(30 + 4 * k, _shake(_REAR, k)) for k in range(11)] +
                    [(80, _ANGRY), (90, _ANGRY)])


def _angry(f):
    sw = _sway(2 * math.pi * f / 48, 1.8)
    return _merge(_ANGRY, {g: {"rz": _ANGRY.get(g, {}).get("rz", 0.0) + v["rz"]} for g, v in sw.items()},
                  _breath(0.5 - 0.5 * math.cos(2 * math.pi * f / 48)))


_T = TONGUE_OUT
MOTIONS["S_2"] = _m(48, [(f, _angry(f)) for f in (0, 12, 24, 36, 48)], loop=True, tracks={
    "tongue_a": {"tx": [(0, 0.0), (4, _T), (8, _T * 0.6), (12, _T), (18, 0.0), (28, 0.0), (32, _T), (40, _T * 0.7),
                        (46, 0.0), (48, 0.0)]},
    "tongue_b": {"tx": [(0, 0.0), (12, 0.0), (16, _T), (20, _T * 0.6), (24, _T), (30, _T * 0.7), (36, 0.0),
                        (48, 0.0)]}})

# ---- defeat ----
# D_4: beaten (held): a last jolt, a wobble, then both heads sag to the floor
# and the coil slumps, eyes shut tight. (The smoke and the vanishing are the
# battle's: the game's effect and the unit's fade.)
_SLUMP = _merge(aimed(REST, a=((-40.0, 4.0), 5.0, 1.0), b=((-14.0, 8.0), 10.0, 1.05)),
                {"coil_back": {"sy": 0.75, "sx": 1.0625}, "coil_front": {"sy": 0.8125, "sx": 1.0625},
                 "body": {"ty": -0.5}}, _jaws(26, 22), _eyes(2, "head_a", "head_b"), _tongue("a", 0.65),
                _tongue("b", 0.5))
MOTIONS["D_4"] = _m(80, [
    (0, _merge(REST, _eyes(2, "head_a", "head_b"))),
    (8, _merge(aimed(REST, a=((-22.0, 54.0), -20.0, 1.1), b=((14.0, 52.0), -20.0, 1.1)),
               _jaws(24, 24), _eyes(2, "head_a", "head_b"))),
    (22, _merge(aimed(REST, a=((-30.0, 44.0), 10.0, 1.05), b=((4.0, 46.0), 15.0, 1.05)),
                _jaws(10, 10), _eyes(2, "head_a", "head_b"))),
    (34, _merge(aimed(REST, a=((-22.0, 40.0), -5.0, 1.0), b=((10.0, 38.0), -5.0, 1.0)),
                _jaws(14, 14), _eyes(2, "head_a", "head_b"))),
    (48, _merge(aimed(REST, a=((-34.0, 24.0), 20.0, 1.0), b=((-4.0, 26.0), 25.0, 1.0)),
                _jaws(8, 8), _eyes(2, "head_a", "head_b"), {"coil_back": {"sy": 0.875}})),
    (56, _SLUMP, {"ease": "vector"}),
    (62, _merge(_SLUMP, {"body": {"ty": 0.4}, "coil_back": {"sy": 0.8125}})),
    (70, _SLUMP),
    (80, _SLUMP),
])

# ---- the plan's other moves (docs/super_luigi/ch2.md: Hiss the main head, Zah the tail's) ----

def _spit(neck):
    """Venom Spit: the head draws back and puffs up (held), then spits: a thrust,
    the mouth wide, the tongue out (the spit itself is the battle's effect, at 4)"""
    head, jaw, other = "head_" + neck, "jaw_" + neck, ("b" if neck == "a" else "a")
    up = ((-16.0, 52.0), -20.0) if neck == "a" else ((18.0, 52.0), -20.0)
    out = ((-24.0, 46.0), 10.0) if neck == "a" else ((6.0, 46.0), 10.0)
    puff = _merge(aimed(REST, **{neck: up}), {head: {"sx": 1.0625, "sy": 1.125}}, _jaws(**{neck: 0}),
                  {"j%s1" % other: {"rz": 4.0 if other == "a" else -4.0}})
    a = _m(24, [(0, REST), (10, _merge(aimed(REST, **{neck: up}), _jaws(**{neck: 4}))), (18, puff), (24, puff)])
    spit = _merge(aimed(REST, **{neck: out}), {head: {"sx": 0.9375, "sy": 0.875}}, _jaws(**{neck: 32}),
                  _tongue(neck, 1.0))
    b = _m(30, [(0, puff), (4, spit, {"ease": "vector"}), (8, _merge(spit, {head: {"sx": 1.0, "sy": 1.0}})),
                (16, _merge(aimed(REST, **{neck: ((out[0][0] + 2, out[0][1] + 3), 0.0)}), _jaws(**{neck: 10}),
                            _tongue(neck, 0.3))),
                (30, _stand(0))])
    return a, b


# V_1A / V_1B: Hiss's Venom Spit (puffing up, held; the spit, at 4). V_2A / V_2B: Zah's.
MOTIONS["V_1A"], MOTIONS["V_1B"] = _spit("a")
MOTIONS["V_2A"], MOTIONS["V_2B"] = _spit("b")

# C_1A: Coil Squeeze, tightening (held): the loops draw in and stand taller, the
# heads rise and glare. C_1B: squeezing (loop 16), the loops pulsing.
_TIGHT = _merge(aimed(REST, a=((-20.0, 56.0), -15.0, 1.1), b=((18.0, 54.0), -15.0, 1.1)),
                {"coil_back": {"sx": 0.875, "sy": 1.1875}, "coil_front": {"sx": 0.875, "sy": 1.125}},
                _jaws(14, 14))
MOTIONS["C_1A"] = _m(30, [(0, REST), (8, _merge(REST, {"coil_back": {"sx": 1.0625, "sy": 0.9375},
                                                        "coil_front": {"sx": 1.0625, "sy": 0.9375}})),
                          (20, _TIGHT, {"ease": "vector"}), (30, _TIGHT)])
MOTIONS["C_1B"] = _m(16, [(0, _TIGHT),
                          (4, _merge(_TIGHT, {"coil_back": {"sx": 0.8125, "sy": 1.25},
                                              "coil_front": {"sx": 0.8125, "sy": 1.1875},
                                              "ja1": {"rz": _TIGHT["ja1"]["rz"] + 2},
                                              "jb1": {"rz": _TIGHT["jb1"]["rz"] - 2}}), {"ease": "vector"}),
                          (10, _TIGHT), (16, _TIGHT)], loop=True)

# B_1: the bicker (loop 40): the heads turn to face each other (Hiss turned
# round), hissing in turn. B_2: Hiss bites Zah (hit at 6); B_3: Zah bites Hiss.
_FACE = aimed(REST, a=((-6.0, 46.0), 8.0, 1.0, True), b=((5.0, 46.0), -8.0, 1.0))


def _hiss_at(k):
    return _merge(_FACE, _jaws(a=18 if k == 0 else 4, b=4 if k == 0 else 18),
                  _tongue("a" if k == 0 else "b", 0.7),
                  {"head_a": {"rz": _FACE["head_a"]["rz"] + (4 if k == 0 else 0)},
                   "head_b": {"rz": _FACE["head_b"]["rz"] + (0 if k == 0 else -4)}})


MOTIONS["B_1"] = _m(40, [(0, _merge(_FACE, _jaws(4, 4))), (6, _hiss_at(0)), (16, _merge(_FACE, _jaws(4, 4))),
                         (26, _hiss_at(1)), (34, _merge(_FACE, _jaws(4, 4))), (40, _merge(_FACE, _jaws(4, 4)))],
                    loop=True)


def _bite_other(neck):
    other = "b" if neck == "a" else "a"
    tgt = ((6.0, 47.0), 0.0, 1.0, True) if neck == "a" else ((-8.0, 47.0), 0.0, 1.0)
    back = ((-12.0, 50.0), -20.0, 1.0, True) if neck == "a" else ((10.0, 50.0), 20.0, 1.0)
    face = {"a": _FACE, "b": _FACE}[neck]
    wind = _merge(aimed(face, **{neck: back}), _jaws(**{neck: 30}))
    hit = _merge(aimed(face, **{neck: tgt}), _jaws(**{neck: 0}), {"head_" + other: {"tex": 2}},
                 {"head_" + other: {"rz": face["head_" + other]["rz"] + (-10 if other == "b" else 10)}})
    return _m(30, [(0, _merge(_FACE, _jaws(4, 4))), (4, wind), (6, hit, {"ease": "vector"}),
                   (12, _merge(hit, _stars(other))), (22, _merge(_FACE, {"head_" + other: {"tex": 2}}, _stars(other))),
                   (30, _merge(_FACE, _jaws(4, 4)))])


MOTIONS["B_2"] = _bite_other("a")
MOTIONS["B_3"] = _bite_other("b")

# N_1: coiled up asleep on the heap (loop 120): both heads laid on the coil,
# eyes shut, slow breathing. U_1: waking (60, held at S_1's pose): one head
# rises, then the other end of the coil.
_SLEEP = _merge(aimed(REST, a=((-26.0, 22.0), 5.0, 0.875), b=((8.0, 24.0), -5.0, 0.9375)),
                _eyes(1, "head_a", "head_b"), {"coil_back": {"sy": 0.9375}})
MOTIONS["N_1"] = _m(120, [(0, _SLEEP), (60, _merge(_SLEEP, _breath(1.0))), (120, _SLEEP)], loop=True)
_HALF = _merge(aimed(REST, a=((-26.0, 44.0), -10.0, 1.0), b=((8.0, 24.0), -5.0, 0.9375)), _eyes(1, "head_b"))
MOTIONS["U_1"] = _m(60, [
    (0, _SLEEP),
    (14, _merge(aimed(REST, a=((-28.0, 28.0), 10.0, 0.9375), b=((8.0, 24.0), -5.0, 0.9375)), _eyes(1, "head_b"))),
    (26, _HALF), (30, _merge(_HALF, _jaws(a=16), _tongue("a", 0.8))),
    (36, _merge(_HALF, {"jb1": {"rz": _HALF["jb1"]["rz"] - 4}})),
    (48, _merge(aimed(REST, a=((-26.0, 46.0), -5.0, 1.0), b=((12.0, 50.0), -10.0, 1.05)), _jaws(b=10))),
    (60, _stand(0)),
])

CAMERA = {"height": 76.0, "width": 126.0, "center": (-10.0, -4.0), "res": (600, 362)}

# what each pose is, for the docs (docs/LUIGI_ANIMS.md)
POSES = {
    "S_1": "stand (loop 80)", "Z_1": "idle: blinks, tongue flicks (loop 80)",
    "T_1": "talk, the main head (loop 32)", "T_2": "talk, the tail's head (loop 32)",
    "W_1": "slither (loop 40)", "R_1": "slither fast (loop 24)",
    "A_1A": "the pincer's set-up: the main head round the far side, flipped (held)",
    "A_1B": "the pincer: both heads bite at SPOT (hit at 4)", "A_1C": "from A_1B back to S_1",
    "X_1": "the crash: the heads smash into each other at SPOT (hit at 5), then dazed (held)",
    "X_2": "dazed (loop 48)", "X_3": "from X_2 back to S_1",
    "A_2A": "the main head's bite, wind-up (held)", "A_2B": "the main head's bite (hit at 3)",
    "A_3A": "the tail head's bite, wind-up (held)", "A_3B": "the tail head's bite (hit at 3)",
    "A_4A": "body slam, coiling (held)", "A_4B": "body slam (lands at 16)",
    "D_1": "hurt, both heads (loop 12)", "D_2": "hurt, the main head (loop 12)",
    "D_3": "hurt, the tail head (loop 12)",
    "P_1": "enraged: rears and hisses (the phase change; held)", "S_2": "angry stand (loop 48)",
    "D_4": "defeated: slumps to the floor (held)",
    "V_1A": "Hiss's Venom Spit, puffing up (held)", "V_1B": "Hiss spits (the spit leaves at 4)",
    "V_2A": "Zah's Venom Spit, puffing up (held)", "V_2B": "Zah spits (at 4)",
    "C_1A": "Coil Squeeze, tightening (held)", "C_1B": "squeezing (loop 16)",
    "B_1": "the bicker: the heads face each other, hissing in turn (loop 40)",
    "B_2": "Hiss bites Zah (hit at 6)", "B_3": "Zah bites Hiss (hit at 6)",
    "N_1": "coiled up asleep (loop 120)", "U_1": "waking: one head rises, then the other (60, held)",
}


# =================================================================== battle ====
# The battle (docs/super_luigi/ch2.md, "The boss: Hizza") is one unit of two
# parts, a model each: HISS (c_sl_hizza: the coil and the front head, Hiss, on
# the enemy side, facing -x) and ZAH (c_sl_hizza_tail: the tail rising out of
# the floor at the stage's far left, behind Luigi, with the tail's head, Zah,
# facing +x). Both are cut from the rig above; the one-model snake is WHOLE
# (c_sl_hizza_whole), for the field's scenes.
#
# Where they meet: an attack on Luigi, or the crash when he dodges, puts Hiss's
# snout tip HISS_REACH units in front of its origin and Zah's ZAH_REACH units in
# front of its own, both MEET_Y up, at the contact frame (CONTACT). So for a
# pincer on Luigi at x = P, the battle places Hiss's part at P + HISS_REACH and
# Zah's at P - ZAH_REACH (Zah's home can be further left: Q_1 sinks it into
# the floor, U_2 brings it up again where the script has moved it).

HISS_REACH, ZAH_REACH, MEET_Y = 38.0, 45.0, 17.0
CONTACT = 5

_TAIL_PARTS = {"jb1", "tail_b1", "jb2", "tail_b2", "jb3", "tail_b3", "head_b", "tongue_b", "mouth_b", "jaw_b",
               "stars_b", "target_b"}
_HISS_PARTS = {n for n, *_r in LAYOUT} - _TAIL_PARTS - {"tail_base"}
_ZAH_PARTS = (_TAIL_PARTS - {"tail_b1"}) | {"|root", "body", "tail_base"}  # the foot cut level at the floor
# Zah's model: the whole turned round to face +x (body's sx -1, about its pivot
# x 4) and the tail's foot cut level at the floor (tail_base, its ground line
# 8 units under jb1; the tail stacks on it at jb1), turned round with it, jb1
# moved to the origin
_JB1, _TBASE = _AN["jb1"], _xy("tail_base")
ZB = {"body": {"sx": -1.0, "tx": _JB1[0] - 2 * 4.0, "ty": -_TBASE[1]}, "tail_base": {"sx": -1.0, "tx": _JB1[0] - 2 * _TBASE[0], "ty": -_TBASE[1]}}


def _only(motions, keep, base=None):
    """the motions with only the parts in `keep`; `base` merged under every key"""
    out = {}
    for name, m in motions.items():
        keys = []
        for k in m["keys"]:
            pose = {g: dict(ch) for g, ch in k[1].items() if g in keep}
            if base:
                for g, ch in base.items():
                    for c, v in ch.items():
                        pose.setdefault(g, {}).setdefault(c, v)
            keys.append((k[0], pose) + tuple(k[2:]))
        mm = dict(m, keys=keys)
        if "tracks" in m:
            mm["tracks"] = {g: t for g, t in m["tracks"].items() if g in keep}
        out[name] = mm
    return out


def _z(pose):
    """a pose of Zah's model: ZB under it (its own body offsets added on)"""
    p = _copy(pose)
    b = p.setdefault("body", {})
    b["tx"] = ZB["body"]["tx"] + b.get("tx", 0.0)
    b["ty"] = ZB["body"]["ty"] + b.get("ty", 0.0)
    b["sx"] = -1.0
    p.setdefault("tail_base", {}).update(ZB["tail_base"])  # the dirt stays on the floor when the tail sinks
    return p


def zaim(pose=None, b=None, **extra):
    """aimed() for Zah, in its model's own frame (+x toward Luigi): the base and
    the mirror are in the pose the aim is solved on"""
    return aimed(_z(pose or {}), b=b, **extra)


ZREST = _z({})

# ---- Hiss (c_sl_hizza) ----
HISS = {}
for _n in ("S_1", "Z_1", "T_1", "W_1", "R_1", "A_2A", "A_2B", "A_4A", "A_4B", "D_2", "P_1", "S_2", "D_4",
           "V_1A", "V_1B", "C_1A", "C_1B", "N_1", "U_1"):
    HISS[_n] = MOTIONS[_n]

_H_UP = aimed(REST, a=((-14.0, 56.0), -28.0, 1.15), **_merge(_jaws(a=20), {"coil_back": {"sy": 0.9375},
                                                                             "coil_front": {"sy": 0.9375}}))
_H_SET = _merge(_H_UP, _jaws(a=30))
_H_HIT = aimed(REST, a=((-HISS_REACH - 2.0, MEET_Y - 1.0), 15.0), **_merge(_jaws(a=0), {"body": {"tx": -1.0}}))
_H_HOLD = _merge(aimed(REST, a=((-HISS_REACH + 1.0, MEET_Y + 1.0), 8.0)), _jaws(a=4), {"body": {"tx": -1.0}})
# A_1A: the pincer's draw-back (36, held): the head rises high and fixes on Luigi.
# A_1B: the strike (hit at CONTACT, 20). A_1C: back to S_1 (30).
HISS["A_1A"] = _m(36, [(0, REST), (8, _merge(REST, {"ja1": {"rz": 6.0}, "coil_back": {"sy": 0.9375}})),
                       (22, _H_UP), (30, _H_SET), (36, _H_SET)])
HISS["A_1B"] = _m(20, [(0, _H_SET), (CONTACT, _H_HIT, {"ease": "vector"}),
                       (8, _merge(_H_HIT, {"coil_back": {"sy": 0.9375}, "body": {"tx": -1.0, "ty": -0.4}})),
                       (13, _H_HOLD), (20, _H_HOLD)])
HISS["A_1C"] = _m(30, [(0, _H_HOLD), (10, aimed(REST, a=((-28.0, 36.0), 10.0))), (22, _stand(0)), (30, _stand(0))])

_H_MEET = aimed(REST, a=((-HISS_REACH, MEET_Y), 5.0), **_merge(_jaws(a=34), {"body": {"tx": -1.0}}))
_H_SQUASH = _merge(aimed(REST, a=((-HISS_REACH + 1.0, MEET_Y + 0.5), -2.0)),
                   {"head_a": {"sx": 0.8125, "sy": 1.1875}}, _jaws(a=0), _eyes(2, "head_a"), _stars("a"),
                   {"body": {"tx": -1.0}})
_H_RECOIL = _merge(aimed(REST, a=((-26.0, 34.0), -25.0)), _jaws(a=8), _eyes(2, "head_a"), _stars("a"),
                   {"coil_back": {"sy": 1.0625}})
_H_DAZED = _merge(aimed(REST, a=((-40.0, 5.0), 15.0)), _jaws(a=10), _eyes(2, "head_a"), _tongue("a", 0.6),
                  _stars("a"), {"coil_back": {"sy": 0.9375}, "coil_front": {"sy": 0.9375}})


def _h_daze(k):
    ph = 2 * math.pi * k / 4.0
    return _merge(aimed(REST, a=((-40.0 + 1.5 * math.cos(ph), 5.0 + 1.2 * math.sin(ph)), 15.0 + 6 * math.sin(ph))),
                  _jaws(a=10), _eyes(2, "head_a"), _tongue("a", 0.6 + 0.25 * math.sin(ph)),
                  {"stars_a": {"vis": 1, "rz": 12 * math.sin(ph)}},
                  {"coil_back": {"sy": 0.9375}, "coil_front": {"sy": 0.9375}})


# X_1: the crash (when Luigi dodges, the heads meet where he stood: contact at
# CONTACT): squash, recoil, then the head droops to the floor, seeing stars (60, held).
# X_2: dazed on the floor (loop 48). X_3: shaking it off, back to S_1 (36).
HISS["X_1"] = _m(60, [(0, _H_SET), (CONTACT, _H_MEET, {"ease": "vector"}), (8, _H_SQUASH, {"ease": "vector"}),
                      (16, _H_RECOIL), (26, _merge(aimed(REST, a=((-32.0, 22.0), 10.0)), _jaws(a=12), _eyes(2, "head_a"),
                                                   _stars("a"))),
                      (38, _merge(aimed(REST, a=((-40.0, 8.0), 20.0)), _jaws(a=14), _eyes(2, "head_a"), _stars("a"))),
                      (48, _H_DAZED), (60, _H_DAZED)])
HISS["X_2"] = _m(48, [(12 * k, _h_daze(k)) for k in range(5)], loop=True)
HISS["X_3"] = _m(36, [(0, _h_daze(0)), (8, _merge(aimed(REST, a=((-30.0, 14.0), 10.0)), _eyes(1, "head_a"))),
                      (16, aimed(REST, a=((-30.0, 34.0), 5.0))), (26, _merge(_stand(0), {"ja1": {"rz": -4.0}})),
                      (36, _stand(0))])

# D_1: hurt (loop 12). B_1: bickering with Zah across the stage: snapping and
# hissing to the left (loop 40); his bite on Zah is the Snap (A_2A, A_2B) aimed at Zah.
HISS["D_1"] = MOTIONS["D_2"]
HISS["B_1"] = _m(40, [(0, _stand(0)),
                      (6, _merge(_stand(6), aimed(REST, a=((-28.0, 46.0), -5.0)), _jaws(a=20), _tongue("a", 0.8))),
                      (14, _merge(_stand(14), _jaws(a=4))),
                      (22, _merge(_stand(22), aimed(REST, a=((-27.0, 47.0), -8.0)), _jaws(a=16))),
                      (30, _merge(_stand(30), _jaws(a=2))), (40, _stand(0))], loop=True)

# ---- Zah (c_sl_hizza_tail) ----
# Its frame: the tail comes out of the floor at the origin, Zah faces +x.
ZAH = {}


def _zsway(f, period=80, amp=1.0):
    ph = 2 * math.pi * f / period
    c = math.sin(ph + 1.9)
    return _z({"jb1": {"rz": -2.0 * c * amp}, "jb2": {"rz": -1.5 * c * amp}, "jb3": {"rz": 1.3 * c * amp},
               "head_b": {"rz": 1.5 * c * amp}})


ZAH["S_1"] = _m(80, [(f, _zsway(f)) for f in (0, 20, 40, 60, 80)], loop=True)
ZAH["Z_1"] = _m(80, ZAH["S_1"]["keys"], loop=True, tracks={"head_b": _BLINKS["head_b"], "tongue_b": _FLICKS["tongue_b"]})
ZAH["T_1"] = _m(32, [(f, _merge(_zsway(f * 2.5), {"jaw_b": {"rz": j}}))
                     for f, j in ((0, 0), (5, 14), (10, 3), (15, 11), (21, 2), (26, 13), (32, 0))], loop=True)

# Q_1: sinking into the floor (20, held, out of sight); U_2: bursting up out of
# it (24): the script moves the part while it's down.
_Z_DOWN = _merge(ZREST, {"body": {"ty": ZB["body"]["ty"] - 48.0}})
ZAH["Q_1"] = _m(20, [(0, ZREST), (6, _merge(zaim(b=((10.0, 26.0), 20.0)), {"body": {"ty": ZB["body"]["ty"] + 1.0}})),
                     (20, _Z_DOWN, {"ease": "linear"})])
ZAH["U_2"] = _m(24, [(0, _Z_DOWN), (10, _merge(zaim(b=((6.0, 40.0), -15.0, 1.1)), _jaws(b=16)), {"ease": "linear"}),
                     (14, _merge(zaim(b=((5.0, 38.0), -10.0, 1.1)), {"body": {"ty": ZB["body"]["ty"] + 2.0}},
                                 _jaws(b=12))),
                     (24, _zsway(0))])

_Z_UP = zaim(b=((-6.0, 44.0), -28.0, 1.15), **_jaws(b=20))
_Z_SET = _merge(_Z_UP, _jaws(b=30))
_Z_HIT = _merge(zaim(b=((ZAH_REACH + 2.0, MEET_Y - 1.0), 15.0)), _jaws(b=0))
_Z_HOLD = _merge(zaim(b=((ZAH_REACH - 2.0, MEET_Y + 1.0), 8.0)), _jaws(b=4))
ZAH["A_1A"] = _m(36, [(0, ZREST), (8, _merge(ZREST, {"jb1": {"rz": -6.0}})), (22, _Z_UP), (30, _Z_SET), (36, _Z_SET)])
ZAH["A_1B"] = _m(20, [(0, _Z_SET), (CONTACT, _Z_HIT, {"ease": "vector"}), (8, _Z_HIT), (13, _Z_HOLD), (20, _Z_HOLD)])
ZAH["A_1C"] = _m(30, [(0, _Z_HOLD), (10, zaim(b=((20.0, 30.0), 10.0))), (22, _zsway(0)), (30, _zsway(0))])

_Z_MEET = _merge(zaim(b=((ZAH_REACH, MEET_Y), 5.0)), _jaws(b=34))
_Z_SQUASH = _merge(zaim(b=((ZAH_REACH - 1.5, MEET_Y + 0.5), -2.0)), {"head_b": {"sx": 0.8125, "sy": 1.1875}},
                   _jaws(b=0), _eyes(2, "head_b"), _stars("b"))
_Z_RECOIL = _merge(zaim(b=((28.0, 32.0), -25.0)), _jaws(b=8), _eyes(2, "head_b"), _stars("b"))
_Z_DAZED = _merge(zaim(b=((34.0, 4.0), 20.0)), _jaws(b=10), _eyes(2, "head_b"), _tongue("b", 0.6), _stars("b"))


def _z_daze(k):
    ph = 2 * math.pi * k / 4.0
    return _merge(zaim(b=((34.0 - 1.5 * math.cos(ph), 4.0 + 1.2 * math.sin(ph)), 20.0 - 6 * math.sin(ph))),
                  _jaws(b=10), _eyes(2, "head_b"), _tongue("b", 0.6 - 0.25 * math.sin(ph)),
                  {"stars_b": {"vis": 1, "rz": -12 * math.sin(ph)}})


# X_1, X_2, X_3: the crash from Zah's side (contact at CONTACT), dazed lying on
# the floor right behind Luigi (where the hammer reaches it), shaking it off.
ZAH["X_1"] = _m(60, [(0, _Z_SET), (CONTACT, _Z_MEET, {"ease": "vector"}), (8, _Z_SQUASH, {"ease": "vector"}),
                     (16, _Z_RECOIL), (26, _merge(zaim(b=((32.0, 22.0), 10.0)), _jaws(b=12), _eyes(2, "head_b"),
                                                  _stars("b"))),
                     (38, _merge(zaim(b=((34.0, 8.0), 24.0)), _jaws(b=14), _eyes(2, "head_b"), _stars("b"))),
                     (48, _Z_DAZED), (60, _Z_DAZED)])
ZAH["X_2"] = _m(48, [(12 * k, _z_daze(k)) for k in range(5)], loop=True)
ZAH["X_3"] = _m(36, [(0, _z_daze(0)), (8, _merge(zaim(b=((30.0, 14.0), 10.0)), _eyes(1, "head_b"))),
                     (16, zaim(b=((20.0, 32.0), 5.0))), (26, _merge(_zsway(0), {"jb1": {"rz": 4.0}})),
                     (36, _zsway(0))])


def _zsneak():
    """Sneak Bite: the head sways side to side (the telegraph) and rears, mouth
    wide (A_3A, held); the bite at Luigi's back (A_3B, hit at 3)"""
    up = zaim(b=((-4.0, 44.0), -30.0, 1.1))
    sw1 = zaim(b=((4.0, 38.0), -10.0))
    sw2 = zaim(b=((-6.0, 38.0), -20.0))
    open_ = _merge(up, _jaws(b=38))
    a = _m(40, [(0, ZREST), (8, sw1), (16, sw2), (24, sw1), (32, _merge(up, _jaws(b=10))), (36, open_), (40, open_)])
    hit = _merge(zaim(b=((ZAH_REACH, 14.0), 30.0)), _jaws(b=0))
    over = _merge(zaim(b=((ZAH_REACH - 0.5, 13.0), 30.0)), _jaws(b=0), {"head_b": {"sy": 0.875}})
    b = _m(24, [(0, open_), (3, hit, {"ease": "vector"}), (6, over),
                (14, _merge(zaim(b=((20.0, 30.0), 0.0)), _jaws(b=4))), (24, _zsway(0))])
    return a, b


ZAH["A_3A"], ZAH["A_3B"] = _zsneak()

_Z_PUFF_UP = ((4.0, 40.0), -20.0)
_z_puff = _merge(zaim(b=_Z_PUFF_UP), {"head_b": {"sx": 1.0625, "sy": 1.125}}, _jaws(b=0))
_z_spit = _merge(zaim(b=((22.0, 36.0), 10.0)), {"head_b": {"sx": 0.9375, "sy": 0.875}}, _jaws(b=32), _tongue("b", 1.0))
# V_2A / V_2B: Zah's Venom Spit (puffing up, held; the spit leaves at 4)
ZAH["V_2A"] = _m(24, [(0, ZREST), (10, _merge(zaim(b=_Z_PUFF_UP), _jaws(b=4))), (18, _z_puff), (24, _z_puff)])
ZAH["V_2B"] = _m(30, [(0, _z_puff), (4, _z_spit, {"ease": "vector"}),
                      (8, _merge(_z_spit, {"head_b": {"sx": 1.0, "sy": 1.0}})),
                      (16, _merge(zaim(b=((20.0, 38.0), 0.0)), _jaws(b=10), _tongue("b", 0.3))), (30, _zsway(0))])


def _zhurt():
    ks = []
    for f, k in ((0, 0.0), (3, 1.0), (6, 0.5), (9, 0.85), (12, 0.0)):
        ks.append((f, _z({"jb2": {"rz": 8 * k}, "jb3": {"rz": -10 * k}, "head_b": {"rz": -10 * k, "tex": 2},
                          "jaw_b": {"rz": 14 * k}})))
    return _m(12, ks, loop=True)


ZAH["D_1"] = _zhurt()
ZAH["B_1"] = _m(40, [(0, _zsway(0)), (6, _merge(_zsway(6), _jaws(b=4))),
                     (14, _merge(_zsway(14), zaim(b=((14.0, 36.0), -5.0)), _jaws(b=18), _tongue("b", 0.8))),
                     (22, _merge(_zsway(22), _jaws(b=4))),
                     (30, _merge(_zsway(30), zaim(b=((13.0, 37.0), -8.0)), _jaws(b=14))), (40, _zsway(0))], loop=True)
_Z_REAR = _merge(zaim(b=((-2.0, 50.0), -35.0, 1.25)), _jaws(b=36), _tongue("b"))
_Z_SINK = _merge(zaim(b=((12.0, 18.0), 25.0, 0.9375)), _eyes(1, "head_b"))
_Z_ANGRY = _merge(zaim(b=((4.0, 42.0), -12.0, 1.1)), _jaws(b=12))
ZAH["P_1"] = _m(90, [(0, ZREST), (14, _Z_SINK), (26, _Z_REAR, {"ease": "vector"})] +
                [(30 + 4 * k, _merge(_Z_REAR, {"jb1": {"rz": _Z_REAR["jb1"]["rz"] + (2.5 if k % 2 else -2.5)},
                                               "tongue_b": {"tx": TONGUE_OUT * (1.0 if k % 2 else 0.75)}}))
                 for k in range(11)] + [(80, _Z_ANGRY), (90, _Z_ANGRY)])
ZAH["S_2"] = _m(48, [(f, _merge(_Z_ANGRY, {g: {"rz": _Z_ANGRY.get(g, {}).get("rz", 0.0) + v["rz"]}
                                           for g, v in (_zsway(f * 80 / 48.0, 1.8)).items() if g.startswith(("jb", "head"))}))
                     for f in (0, 12, 24, 36, 48)], loop=True, tracks={"tongue_b": MOTIONS["S_2"]["tracks"]["tongue_b"]})
# D_4: defeated: a jolt, a wobble, the head sags to the floor, then the tail
# sinks into it (80, held, out of sight)
ZAH["D_4"] = _m(80, [(0, _merge(ZREST, _eyes(2, "head_b"))),
                     (8, _merge(zaim(b=((2.0, 42.0), -20.0, 1.1)), _jaws(b=24), _eyes(2, "head_b"))),
                     (22, _merge(zaim(b=((14.0, 34.0), 15.0)), _jaws(b=10), _eyes(2, "head_b"))),
                     (40, _merge(zaim(b=((28.0, 6.0), 10.0)), _jaws(b=22), _eyes(2, "head_b"), _tongue("b", 0.5)),
                      {"ease": "vector"}),
                     (56, _merge(zaim(b=((28.0, 6.0), 10.0)), _jaws(b=22), _eyes(2, "head_b"), _tongue("b", 0.5))),
                     (80, _merge(_Z_DOWN, _eyes(2, "head_b")), {"ease": "linear"})])
# N_1: out of sight under the floor (the field's or a turn's start). U_1: rising
# slowly out of it, eyes opening (60, held at S_1's pose).
ZAH["N_1"] = _m(2, [(0, _Z_DOWN), (2, _Z_DOWN)])
ZAH["U_1"] = _m(60, [(0, _Z_DOWN), (30, _merge(zaim(b=((8.0, 22.0), 10.0, 0.9375)), {"body": {"ty": ZB["body"]["ty"] - 8.0}},
                                               _eyes(1, "head_b")), {"ease": "linear"}),
                     (46, _merge(zaim(b=((6.0, 34.0), -5.0)), _eyes(1, "head_b"))), (52, zaim(b=((5.0, 35.0), -5.0), **_jaws(b=10))),
                     (60, _zsway(0))])


# ---- the polish (ART_PLAN, "needs polish"), the same on all three models ----
# As Hooktail: the idle is one long sway down the neck chains (240 frames), talk is quick (14),
# and the body doesn't squash but for the bites and the crash.
_SQUASH_KEEP = {"A_1B", "A_2B", "A_4B", "X_3", "X_3B", "V_1B"}


def _long_sway(motion):
    keys = []
    for f in range(0, 241, 20):
        ph = 2 * math.pi * f / 240.0
        pose = None
        for k in motion["keys"]:
            if k[0] == f % motion["end"]:
                pose = {g: dict(c) for g, c in k[1].items()}
        if pose is None:
            continue
        for g, amp, off in (("ja1", 5.0, 0.0), ("ja2", 4.0, 0.7), ("ja3", 3.0, 1.4), ("head_a", 4.0, 2.1),
                            ("jb1", 4.0, 2.5), ("jb2", 3.0, 3.2), ("jb3", 3.0, 3.9), ("head_b", 4.0, 4.6)):
            if g in pose or any(g in k[1] for k in motion["keys"]):
                c = pose.setdefault(g, {})
                c["rz"] = c.get("rz", 0.0) + amp * math.sin(ph + off)
        keys.append((f, pose))
    return dict(motion, end=240, keys=keys)


def _retime(motion, end):
    k = float(end) / motion["end"]
    return dict(motion, end=end, keys=[(int(round(x[0] * k)),) + tuple(x[1:]) for x in motion["keys"]])


def _unsquash(motions):
    for name, m in motions.items():
        if name in _SQUASH_KEEP:
            continue
        for key in m.get("keys", []):
            for g, ch in key[1].items():
                if g == "body" or g.startswith(("coil_", "head_")):
                    ch.pop("sx", None)
                    ch.pop("sy", None)


for _set in (MOTIONS, HISS, ZAH):
    for _k in _set:
        _set[_k] = dict(_set[_k], keys=[(k[0], {g: dict(c) for g, c in k[1].items()}) + tuple(k[2:])
                                        for k in _set[_k].get("keys", [])])
    _unsquash(_set)
    for _k in ("T_1", "T_2"):
        if _k in _set:
            _set[_k] = _retime(_set[_k], 14)
    for _k in ("S_1",):
        _set[_k] = _long_sway(_set[_k])


class Model:
    """one of Hizza's models, as sl_models.py builds them"""

    def __init__(self, name, keep, motions, base=None, camera=None):
        self.NAME, self._keep = name, keep
        self.MOTIONS = _only(motions, keep, base)
        self.CAMERA = camera or CAMERA

    def parts(self):
        return [p for p in parts() if p.name in self._keep]

    def build(self, anims=()):
        if MISSES:
            raise ValueError("necks that can't reach their aims: %s" % MISSES)
        anims = list(anims)
        if not any(x.name == "Z_1" for x in anims):
            anims.insert(0, PM.still("Z_1"))
        return PM.build(self.NAME, self.parts(), anims)


WHOLE = Model("c_sl_hizza_whole", {n for n, *_r in LAYOUT} - {"tail_base"}, MOTIONS)
FRONT = Model("c_sl_hizza", _HISS_PARTS, HISS)
TAIL = Model("c_sl_hizza_tail", _ZAH_PARTS, ZAH, base=ZB,
             camera={"height": 64.0, "width": 90.0, "center": (14.0, -6.0), "res": (450, 320)})
