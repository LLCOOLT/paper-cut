"""Cranberry's cards cut from the model's own pieces (`python cutout.py cut_cranberry`).

The design: the style model's red-haired boy in white robes, seated cross-legged, asleep (designs/cranberry/
concept_cranberry_a_seed3.png). Qwen-Image-Edit split it into flat pieces (designs/cranberry/parts_qwen.png: the sleeping
head, the robe with its wide sleeves held out, the folded legs); his waking, awake, smiling and talking heads are Qwen's
edits of that sheet (parts_qwen_head_*.png). Here: the sleeves are cut from the robe at their seams and hung down, his
hands on his knees; the same sleeve as drawn, held out, is the arm he offers the piece with; the head sits on the
collar; the robe's skirt covers the top of his folded legs. The compass piece keeps its drawn card (art/cranberry/).
Points are in the piece sheet's pixels (1024 square); the model's (0, 0) is under him, on the floor.
"""
import os

import numpy as np
from PIL import Image

_ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = "designs/cranberry/parts_qwen.png"
FACE = "designs/cranberry/parts_qwen_head_%s.png"  # Qwen's sheet with him waking, awake, smiling or talking
OUT = "cranberry/qwen"
MODEL = "c_sl_cranberry"
PX = 4.0  # 32 sheet pixels a unit (one texel a sheet pixel): about 24 units to the top of his hair, seated, a standing Toad's height
LEGS_D = (0, -75)  # the folded legs moved up under the robe's skirt
ORIGIN = (515, 965 + LEGS_D[1])
LINE = "#2a1414"

HEAD_D = (25, 45)  # the head moved down onto the collar
HEAD_PIVOT = (490, 350)  # the chin
SH_L, SH_R = (440, 420), (595, 420)  # the shoulders: the sheet's left (the -x side, the way he faces) and right
HANG = 62.0  # degrees a sleeve is turned from held out to hanging, its hand on his knee


def have(path, fallback):
    """a Qwen edit if it has been made, else the piece sheet it was made from"""
    return path if os.path.exists(os.path.join(_ART, path)) else fallback


def moved(p, d):
    return (p[0] + d[0], p[1] + d[1])


SLEEVE_L = [(455, 395), (455, 420), (420, 470), (412, 535), (330, 575), (237, 628), (200, 628), (135, 565), (135, 495),
            (200, 468), (320, 440), (400, 405)]
SLEEVE_R = [(578, 395), (640, 405), (720, 440), (830, 470), (905, 500), (905, 565), (840, 628), (800, 628), (700, 580),
            (625, 535), (620, 470), (585, 420)]
HEAD_POLY = [(355, 40), (705, 40), (705, 368), (355, 368)]

CARDS = {
    "body_seated": dict(poly=[(455, 378), (580, 378), (580, 395), (620, 470), (622, 540), (625, 585), (678, 650), (684, 716),
                              (366, 716), (370, 650), (415, 585), (412, 540), (418, 470), (455, 395)],
                        pivot=(515, 700), desc="the robe, the sleeves cut away at their seams; pivot at the hips"),
    "legs": dict(poly=[(280, 735), (730, 735), (730, 975), (280, 975)], pivot=(515, 800), at=moved((515, 800), LEGS_D),
                 desc="the folded legs, bare feet crossed"),
    "arm_front": dict(poly=SLEEVE_L, pivot=SH_L, rotate=HANG, desc="the near sleeve, hung down, the hand on his knee; shoulder pivot"),
    "arm_back": dict(poly=SLEEVE_R, pivot=SH_R, rotate=-0.7 * HANG, desc="the far sleeve, hung down; shoulder pivot"),
    "arm_reach": dict(poly=SLEEVE_L, pivot=SH_L, rotate=8.0, desc="the near sleeve held out, the hand open, offering the piece"),
    "head_sleep": dict(poly=HEAD_POLY, pivot=HEAD_PIVOT, at=moved(HEAD_PIVOT, HEAD_D), air=0, desc="the head, asleep"),
}
for _n, _v, _d in (("head_wake", "wake", "waking, eyes wide"), ("head", "awake", "awake, calm"),
                   ("head_smile", "smile", "smiling"), ("head_talk", "talk", "talking")):
    CARDS[_n] = dict(CARDS["head_sleep"], src=have(FACE % _v, SRC), desc="the head, " + _d)
BINDS = [["head_sleep", "head_wake", "head", "head_smile", "head_talk"]]
ANCHORS = {"neck": moved(HEAD_PIVOT, HEAD_D), "hips": (515, 700), "hand_reach": (150, 560)}


def _piece():
    """the compass piece's drawn card, as it is"""
    return np.array(Image.open(os.path.join(_ART, "cranberry", "compass_piece.png")).convert("RGBA"))


EXTRA = {"compass_piece": _piece}
EXTRA_META = {"compass_piece": dict(pivot=(20.0, 20.0), at=(172, 518), desc="the compass piece he guarded, on his palm")}
