"""Hizza's cards cut from the model's own pieces (`python cutout.py cut_hizza`).

The design: the style model's green two-headed snake (designs/hizza/concept_hizza.png); Qwen-Image-Edit set it out on
white (designs/hizza/parts_qwen.png): one front-facing head, fangs bared, mouth open, on a long cream-bellied neck that
curls at its foot into an S of body, and a ring of coiled body beside it. His blink and his eyes squeezed shut are Qwen's
edits of that sheet (parts_qwen_shut.png, parts_qwen_hurt.png); the tail's head, sly-eyed, is its sly edit
(parts_qwen_sly.png: the same head, the same place). Here: the front neck is cut into three bands along its own middle
(neck_a1..3, joints where the rig turns them), the head off its top; the S's rising stroke is the tail (tail_b1..3) and
the tail's head sits on it; the S's foot is the tail's base in the dirt (Zah's model); the ring is split at its middle
into the coil's far half (behind the necks) and near half (in front). Points are in the sheet's pixels (1024 square);
the model's (0, 0) is on the floor under the coil, and the front neck's foot (ja1) is the rig's (-8.75, 10).
"""
import math
import os

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/hizza/parts_qwen.png"
FACE = "designs/hizza/parts_qwen_%s.png"
OUT = "hizza/qwen"
MODEL = "c_sl_hizza"
PX = 2.0  # 16 sheet pixels a unit: the front head about 61 units up, the tail's about 69 (Luigi is 24)
DENSITY = 2  # 16 texels a unit: one texel a sheet pixel
LINE = "#10190c"
SPECKS = 0.05  # Qwen's pain marks beside the hurt head, slivers of the next piece
HOLES = [(800, 800)]  # the ring's middle


def have(path, fallback=SRC):
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


# ---- the middles, traced from the sheet's runs (rows' opaque spans), head end first ----
NECK_MID = [(285, 420), (290, 470), (285, 500), (270, 530), (256, 560), (235, 590), (213, 620), (193, 650), (174, 680),
            (162, 710), (155, 740), (158, 770), (165, 800), (175, 840), (195, 880), (230, 905), (275, 918)]
TAIL_MID = [(448, 300), (448, 320), (455, 360), (470, 400), (479, 420), (497, 460), (513, 500), (524, 540), (528, 580),
            (526, 620), (518, 660), (504, 700), (481, 740), (465, 760), (440, 790), (410, 815),
            (385, 840), (360, 865), (340, 885)]
# the rig's joint spacing (ja1..head 12.85, 11.77, 10.99; jb1..head 11.29, 10.51, 9.66 units), from the head down
NECK_SEG = (10.99, 11.77, 12.85)
TAIL_SEG = (9.66, 10.51, 11.29)
UNIT = PX * 8.0
SEG_PX = 14.4  # sheet pixels a unit of the joint spacing: the joints sit on the sheet's middles at 0.9 of the rig's


def _lengths(mid):
    L = [0.0]
    for a, b in zip(mid, mid[1:]):
        L.append(L[-1] + math.dist(a, b))
    return L


def _at(mid, s):
    """the middle's point and its unit direction (toward the head end) at arc length s"""
    L = _lengths(mid)
    if s < 0:  # past the head end: straight on along the first stretch (a band's top hidden under the head)
        (x0, y0), (x1, y1) = mid[0], mid[1]
        d = math.dist(mid[0], mid[1])
        u = ((x0 - x1) / d, (y0 - y1) / d)
        return (x0 - s * u[0], y0 - s * u[1]), u
    s = min(L[-1] - 1e-6, s)
    for i in range(len(mid) - 1):
        if L[i + 1] >= s:
            t = (s - L[i]) / (L[i + 1] - L[i])
            (x0, y0), (x1, y1) = mid[i], mid[i + 1]
            d = math.dist(mid[i], mid[i + 1])
            return (x0 + t * (x1 - x0), y0 + t * (y1 - y0)), ((x0 - x1) / d, (y0 - y1) / d)


def _band(mid, s0, s1, half=150, step=12):
    """the polygon of a band along the middle from arc length s0 (head end) to s1, `half` either side"""
    left, right = [], []
    n = max(2, int(abs(s1 - s0) / step) + 1)
    for k in range(n):
        p, d = _at(mid, s0 + (s1 - s0) * k / (n - 1.0))
        nx, ny = -d[1], d[0]
        left.append((round(p[0] + nx * half), round(p[1] + ny * half)))
        right.append((round(p[0] - nx * half), round(p[1] - ny * half)))
    return left + right[::-1]


def _joints(mid, segs):
    s, out = 0.0, [0.0]
    for g in segs:
        s += g * SEG_PX
        out.append(s)
    return out  # arc lengths of: head joint, j3, j2, j1


def _pt(mid, s):
    p, _d = _at(mid, s)
    return (round(p[0], 1), round(p[1], 1))


SN = _joints(NECK_MID, NECK_SEG)
ST = _joints(TAIL_MID, TAIL_SEG)
HA, JA3, JA2, JA1 = (_pt(NECK_MID, s) for s in SN)
HB0, JB3, JB2, JB1 = (_pt(TAIL_MID, s) for s in ST)
# the model's origin: ja1 is the rig's (-8.75, 10)
ORIGIN = (JA1[0] + 8.75 * UNIT, JA1[1] + 10.0 * UNIT)


def _sheet(model):
    return (ORIGIN[0] + model[0] * UNIT, ORIGIN[1] - model[1] * UNIT)


# the tail stands where the sheet has it, its foot one with the front neck's curl (the S's bottom), but turned back
# (+x) about its foot's joint so the two heads stand apart: every tail card turned by LEAN about its own joint, set at
# its joint turned about jb1 (one rigid turn)
LEAN = -9.0  # degrees, counter-clockwise as seen


def _rot(p, c, a):
    a = math.radians(a)
    dx, dy = p[0] - c[0], p[1] - c[1]
    return (round(c[0] + dx * math.cos(a) + dy * math.sin(a), 1), round(c[1] - dx * math.sin(a) + dy * math.cos(a), 1))


def _tail(p):
    """a point of the tail where it stands"""
    return _rot(p, JB1, LEAN)


JB3_AT, HB0_AT = _tail(JB3), _tail(HB0)


CHIN = (210, 432)  # the tail's head sits on its stroke by its chin (the stroke's top hidden under the jaw)


def _on_b(p):
    """a point of the tail's head (not mirrored) where it stands"""
    hb = HB0_AT
    return (round(p[0] - CHIN[0] + hb[0], 1), round(p[1] - CHIN[1] + hb[1], 1))


# what lies beside each piece, erased from its band: the gap between the neck and the S, its right side the S's
GAP = [(350, 0), (350, 260), (352, 300), (370, 360), (382, 400), (394, 440), (405, 500), (401, 560), (382, 620),
       (352, 680), (321, 740), (302, 780), (320, 1024)]
S_SIDE = GAP + [(1024, 1024), (1024, 0)]
NECK_SIDE = GAP + [(0, 1024), (0, 0)]
# the hole of the S's hump and the hump's falling stroke, right of the tail's rising one; the ring below
HUMP = [(560, 250), (560, 360), (566, 400), (572, 420), (586, 460), (603, 500), (615, 520), (630, 560), (625, 600),
        (605, 650), (590, 700), (584, 740), (578, 780), (580, 1024), (1024, 1024), (1024, 0), (560, 0)]
# the head: its skull, eyes and open jaws, cut off the neck under the jaw
HEAD = [(50, 20), (352, 20), (356, 230), (348, 320), (322, 375), (296, 405), (262, 438), (232, 452), (160, 455),
        (110, 400), (92, 300), (82, 240), (50, 230)]
# the tail's head: the same, cut off along its lower jaw's own line (no throat: the tail's stroke is its neck)
HEAD_B = [(50, 20), (352, 20), (352, 230), (335, 250), (322, 330), (292, 390), (242, 440), (175, 452), (160, 455),
          (110, 400), (92, 300), (82, 240), (50, 230)]
# the head's jaws and all above them: off the neck's top band (it keeps only the throat beside the jaw), so no copy
# of the teeth shows when the head turns on its neck
JAWS = [(0, 0), (352, 0), (352, 230), (335, 250), (322, 330), (292, 390), (242, 440), (175, 452), (0, 452)]
OVER = 40  # sheet pixels a band runs on past its lower joint, over the band below (no line: the seam is hidden)
UNDER = 60  # and the band below runs on past that joint, behind it: a bent joint shows the neck, not a wedge of air

RING_MID = 787  # the ring's middle row: the far half above, the near half below
RING_X = (590, 985)
RING_FOOT = (782, 966)  # the ring's bottom, on the floor
_RF = _sheet((8.0, 0.0))  # the ring's bottom: the front neck's curl over its near side, the tail's foot in its far side

CARDS = {
    "coil_back": dict(poly=[(RING_X[0], 595), (RING_X[1], 595), (RING_X[1], RING_MID + 24), (RING_X[0], RING_MID + 24)],
                      pivot=RING_FOOT, at=_RF, rim=False, desc="the coil's far half (the ring's top), behind the necks"),
    "coil_front": dict(poly=[(RING_X[0], RING_MID), (RING_X[1], RING_MID), (RING_X[1], 985), (RING_X[0], 985)],
                       pivot=RING_FOOT, at=_RF, rim=False, desc="the coil's near half (the ring's bottom), in front"),
    "neck_a1": dict(poly=_band(NECK_MID, SN[2] - UNDER, _lengths(NECK_MID)[-1] + 60), erase=[S_SIDE], pivot=JA1, rim=False,
                    desc="the front neck's foot, its curl into the coil"),
    "neck_a2": dict(poly=_band(NECK_MID, SN[1] - UNDER, SN[2] + OVER), erase=[S_SIDE], pivot=JA2, rim=False,
                    desc="the front neck's middle"),
    "neck_a3": dict(poly=_band(NECK_MID, SN[0] - 100, SN[1] + OVER), erase=[S_SIDE, JAWS], pivot=JA3, rim=False,
                    desc="the front neck's top, under the head"),
    "head_a": dict(poly=HEAD, pivot=HA, air=0.2, desc="Hiss, the front head: wide eyes, fangs bared, mouth open"),
    "head_a_blink": dict(poly=HEAD, pivot=HA, air=0.2, src=have(FACE % "shut"), desc="the same, eyes shut (a blink)"),
    "head_a_hurt": dict(poly=HEAD, pivot=HA, air=0.2, src=have(FACE % "hurt"), desc="the same, eyes squeezed shut"),
    "head_b": dict(poly=HEAD_B, pivot=CHIN, at=HB0_AT, air=0.2, src=have(FACE % "sly"),
                   desc="Zah, the tail's head: the same head, sly slits for eyes"),
    "head_b_blink": dict(poly=HEAD_B, pivot=CHIN, at=HB0_AT, air=0.2, src=have(FACE % "shut"), desc="eyes shut"),
    "head_b_hurt": dict(poly=HEAD_B, pivot=CHIN, at=HB0_AT, air=0.2, src=have(FACE % "hurt"), desc="eyes squeezed shut"),
    "tail_b1": dict(poly=_band(TAIL_MID, ST[2] - UNDER, _lengths(TAIL_MID)[-1], 120), erase=[NECK_SIDE, HUMP],
                    pivot=JB1, rotate=LEAN, rim=False, desc="the tail's foot"),
    "tail_b2": dict(poly=_band(TAIL_MID, ST[1] - UNDER, ST[2] + OVER, 120), erase=[NECK_SIDE, HUMP], pivot=JB2,
                    at=_tail(JB2), rotate=LEAN, rim=False, desc="the tail's middle"),
    "tail_b3": dict(poly=_band(TAIL_MID, ST[0] - 70, ST[1] + OVER, 85), erase=[NECK_SIDE, HUMP], pivot=JB3,
                    at=JB3_AT, rotate=LEAN, rim=False, desc="the tail's top, under its head"),
}
# the tail's base in the dirt (Zah's model rises on it): the tail's foot band again, its bottom cut level on the
# ground 8 units under jb1
_GROUND = JB1[1] + 8.0 * UNIT
CARDS["tail_base"] = dict(poly=_band(TAIL_MID, ST[2] - UNDER, _lengths(TAIL_MID)[-1], 120),
                          erase=[NECK_SIDE, HUMP, [(0, _GROUND), (1024, _GROUND), (1024, 1024), (0, 1024)]],
                          pivot=(JB1[0], _GROUND), at=_tail((JB1[0], _GROUND)), rotate=LEAN,
                          desc="Zah's model's tail foot (in place of tail_b1), cut level where it comes out of the floor")
BINDS = [["head_a", "head_a_blink", "head_a_hurt"], ["head_b", "head_b_blink", "head_b_hurt"]]


ANCHORS = {"ja1": JA1, "ja2": JA2, "ja3": JA3, "head_a": HA,
           "jb1": _tail(JB1), "jb2": _tail(JB2), "jb3": JB3_AT, "head_b": HB0_AT}
for _w, _o in (("a", lambda p: p), ("b", _on_b)):
    ANCHORS["mouth_" + _w] = _o((200, 345))  # the open mouth's middle: the bite's point (the rig's aim)
    ANCHORS["jaw_" + _w] = _o((205, 300))
    ANCHORS["tongue_" + _w] = _o((205, 345))
    ANCHORS["stars_" + _w] = _o((200, 40))  # over the crown
    ANCHORS["target_" + _w] = _o((200, 200))
ANCHORS["collar_a"] = (262, 520)  # round the front neck, under the head
